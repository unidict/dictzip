//
//  dictzip.c
//
//  Created by kejinlu on 2026-04-08
//


#include "dictzip.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <zlib.h>

// ============================================================
// Internal data structures
// ============================================================

/**
 * dictzip file info
 */
typedef struct {
    uint32_t header_size;        // Total header size
    uint8_t method;              // Compression method (8 = deflate)
    uint8_t flags;               // GZIP flags
    uint32_t mtime;              // Modification time
    uint8_t extra_flags;         // Extra flags
    uint8_t os;                  // Operating system
    uint16_t version;            // dictzip version
    uint16_t chunk_size;         // Uncompressed chunk size
    uint16_t chunk_count;        // Total chunk count
    uint16_t *chunks;            // Compressed size array for each chunk
    uint32_t *offsets;           // File offset for each chunk
    char orig_filename[256];     // Original filename
    char comment[256];           // Comment
    uint32_t uncompressed_size;  // Original file size (ISIZE)
} dictzip_header;

/**
 * Chunk cache entry
 */
typedef struct {
    int chunk_index;         // Chunk index, -1 means unused
    uint8_t *data;           // Decompressed data
    size_t size;             // Actual data size
    uint32_t stamp;          // Timestamp (for LRU eviction)
} dictzip_cache_entry;

/**
 * dictzip file handle
 */
struct dictzip {
    // File info
    char *filename;          // Filename
    FILE *file;              // File handle

    // Header info
    dictzip_header header;

    // Cache
    dictzip_cache_entry *cache;
    int cache_size;
    uint32_t stamp;

    // zlib inflate stream
    z_stream zstream;
    bool zstream_initialized;
};

// ============================================================
// Constants
// ============================================================
// GZIP header identification (RFC 1952)
#define GZ_HEADER_SIZE  10

#define GZ_ID1     0x1F
#define GZ_ID2     0x8B

#define GZ_FEXTRA  0x04
#define GZ_FNAME   0x08
#define GZ_COMMENT 0x10
#define GZ_FHCRC   0x02
#define GZ_SI1     'R'
#define GZ_SI2     'A'

// Default cache size
#define DICTZIP_CHUNK_CACHE_SIZE  5

// ============================================================
// Internal functions
// ============================================================

static void dictzip_clear_cache(dictzip *dz);

/**
 * Read 16-bit integer from memory (direct memcpy on little-endian)
 */
static uint16_t read_le16(const uint8_t **p) {
    uint16_t value;
    memcpy(&value, *p, 2);
    *p += 2;
    return value;
}

/**
 * Read 32-bit integer from memory (direct memcpy on little-endian)
 */
static uint32_t read_le32(const uint8_t **p) {
    uint32_t value;
    memcpy(&value, *p, 4);
    *p += 4;
    return value;
}

/**
 * Parse dictzip file header
 */
static int parse_dictzip_header(dictzip *dz) {
    FILE *file = dz->file;
    dictzip_header *header = &dz->header;

    // Read GZIP fixed header
    uint8_t buf[GZ_HEADER_SIZE];
    if (fread(buf, 1, GZ_HEADER_SIZE, file) != GZ_HEADER_SIZE) {
        fprintf(stderr, "Error: Failed to read GZIP header\n");
        return -1;
    }

    // Verify GZIP identification (RFC 1952: ID1=0x1F, ID2=0x8B)
    if (buf[0] != GZ_ID1 || buf[1] != GZ_ID2) {
        fprintf(stderr, "Error: Not a GZIP file\n");
        return -1;
    }

    header->method = buf[2];
    if (header->method != 8) {
        fprintf(stderr, "Error: Unsupported compression method: %d (expected 8)\n", header->method);
        return -1;
    }

    header->flags = buf[3];
    const uint8_t *p = buf + 4;
    header->mtime = read_le32(&p);
    header->extra_flags = *p++;
    header->os = *p++;
    header->header_size = GZ_HEADER_SIZE;

    // https://man.archlinux.org/man/dictzip.1.en
    // Check extra field (required for dictzip)
    if (!(header->flags & GZ_FEXTRA)) {
        fprintf(stderr, "Error: Not a dictzip file (missing FEXTRA flag)\n");
        return -1;
    }

    uint8_t extra_len_buf[2];
    if (fread(extra_len_buf, 1, 2, file) != 2) {
        fprintf(stderr, "Error: Failed to read extra length\n");
        return -1;
    }
    const uint8_t *elp = extra_len_buf;
    uint16_t extra_length = read_le16(&elp);
    header->header_size += extra_length + 2;

    // Read entire extra field
    uint8_t *extra_buf = (uint8_t *)malloc(extra_length);
    if (!extra_buf) {
        fprintf(stderr, "Error: Memory allocation failed for extra field\n");
        return -1;
    }
    if (fread(extra_buf, 1, extra_length, file) != extra_length) {
        fprintf(stderr, "Error: Failed to read extra field\n");
        free(extra_buf);
        return -1;
    }

    const uint8_t *ep = extra_buf;

    // Check dictzip signature 'R','A'
    if (ep[0] != GZ_SI1 || ep[1] != GZ_SI2) {
        fprintf(stderr, "Error: Not a dictzip file (missing 'RA' signature)\n");
        free(extra_buf);
        return -1;
    }
    ep += 2;

    uint16_t sub_length = read_le16(&ep);
    header->version = read_le16(&ep);

    if (header->version != 1) {
        fprintf(stderr, "Error: Unsupported dictzip version: %d\n", header->version);
        free(extra_buf);
        return -1;
    }

    header->chunk_size = read_le16(&ep);
    header->chunk_count = read_le16(&ep);

    // Validate sub_length: should be 2(ver) + 2(chunk_size) + 2(chunk_count) + 2*chunk_count
    uint16_t expected_sub_length = 6 + 2 * header->chunk_count;
    if (sub_length != expected_sub_length) {
        fprintf(stderr, "Error: Invalid RA sub-field length: %d (expected %d)\n",
                sub_length, expected_sub_length);
        free(extra_buf);
        return -1;
    }

    if (header->chunk_count == 0) {
        fprintf(stderr, "Error: Invalid chunk count: 0\n");
        free(extra_buf);
        return -1;
    }

    if (header->chunk_size == 0) {
        fprintf(stderr, "Error: Invalid chunk size: 0\n");
        free(extra_buf);
        return -1;
    }

    // Allocate chunks array
    header->chunks = (uint16_t *)calloc(header->chunk_count, sizeof(uint16_t));
    if (!header->chunks) {
        fprintf(stderr, "Error: Memory allocation failed for chunks\n");
        free(extra_buf);
        return -1;
    }

    // Read compressed size for each chunk
    for (uint16_t i = 0; i < header->chunk_count; i++) {
        header->chunks[i] = read_le16(&ep);
    }

    free(extra_buf);

    // Read filename (if present)
    if (header->flags & GZ_FNAME) {
        uint32_t len = 0;
        int c;
        while ((c = fgetc(file)) != EOF && c != '\0') {
            if (len < 255) {
                header->orig_filename[len] = (char)c;
            }
            len++;
        }
        header->orig_filename[len < 255 ? len : 255] = '\0';
        header->header_size += len + 1;
    } else {
        header->orig_filename[0] = '\0';
    }

    // Read comment (if present)
    if (header->flags & GZ_COMMENT) {
        uint32_t len = 0;
        int c;
        while ((c = fgetc(file)) != EOF && c != '\0') {
            if (len < 255) {
                header->comment[len] = (char)c;
            }
            len++;
        }
        header->comment[len < 255 ? len : 255] = '\0';
        header->header_size += len + 1;
    } else {
        header->comment[0] = '\0';
    }

    // Skip CRC16 (if present)
    if (header->flags & GZ_FHCRC) {
        fseek(file, 2, SEEK_CUR);
        header->header_size += 2;
    }

    // Read ISIZE at end of file (last 4 bytes, RFC 1952)
    uint8_t tail[4];
    fseek(file, -4, SEEK_END);
    if (fread(tail, 1, 4, file) != 4) {
        fprintf(stderr, "Error: Failed to read file tail\n");
        return -1;
    }
    const uint8_t *tp = tail;
    header->uncompressed_size = read_le32(&tp);

    // Calculate offset for each chunk
    header->offsets = (uint32_t *)calloc(header->chunk_count, sizeof(uint32_t));
    if (!header->offsets) {
        fprintf(stderr, "Error: Memory allocation failed for offsets\n");
        free(header->chunks);
        header->chunks = NULL;
        return -1;
    }

    uint32_t offset = header->header_size;
    for (uint16_t i = 0; i < header->chunk_count; i++) {
        header->offsets[i] = offset;
        offset += header->chunks[i];
    }

    return 0;
}

/**
 * Initialize zlib stream
 */
static int init_zstream(dictzip *dz) {
    if (dz->zstream_initialized) {
        inflateReset(&dz->zstream);
        return 0;
    }

    memset(&dz->zstream, 0, sizeof(z_stream));
    if (inflateInit2(&dz->zstream, -15) != Z_OK) {
        fprintf(stderr, "Error: Failed to initialize zlib stream\n");
        return -1;
    }

    dz->zstream_initialized = true;
    return 0;
}

/**
 * Decompress a chunk
 */
static uint8_t *inflate_chunk(dictzip *dz, int chunk_index,
                                     size_t *out_size) {
    dictzip_header *header = &dz->header;

    if (chunk_index < 0 || chunk_index >= header->chunk_count) {
        fprintf(stderr, "Error: Invalid chunk index: %d\n", chunk_index);
        return NULL;
    }

    // Seek to chunk start position
    if (fseek(dz->file, header->offsets[chunk_index], SEEK_SET) != 0) {
        fprintf(stderr, "Error: Failed to seek to chunk %d: %s\n", chunk_index, strerror(errno));
        return NULL;
    }

    // Read compressed data
    int compressed_size = header->chunks[chunk_index];
    if (compressed_size <= 0) {
        fprintf(stderr, "Error: Invalid compressed size for chunk %d: %d\n", chunk_index, compressed_size);
        return NULL;
    }

    uint8_t *compressed_data = (uint8_t *)malloc(compressed_size);
    if (!compressed_data) {
        fprintf(stderr, "Error: Memory allocation failed for compressed data\n");
        return NULL;
    }

    size_t read_size = fread(compressed_data, 1, compressed_size, dz->file);
    if (read_size != (size_t)compressed_size) {
        fprintf(stderr, "Error: Failed to read compressed data\n");
        free(compressed_data);
        return NULL;
    }

    // Initialize zlib stream
    if (init_zstream(dz) != 0) {
        free(compressed_data);
        return NULL;
    }

    // Prepare output buffer (size = chunk_size, i.e. uncompressed chunk size)
    uint8_t *out_buf = (uint8_t *)malloc(header->chunk_size);
    if (!out_buf) {
        fprintf(stderr, "Error: Memory allocation failed for output buffer\n");
        free(compressed_data);
        return NULL;
    }

    // Set zlib parameters
    dz->zstream.next_in = compressed_data;
    dz->zstream.avail_in = compressed_size;
    dz->zstream.next_out = out_buf;
    dz->zstream.avail_out = header->chunk_size;

    // Decompress (use Z_PARTIAL_FLUSH since chunks are partial deflate blocks)
    int ret = inflate(&dz->zstream, Z_PARTIAL_FLUSH);
    free(compressed_data);

    if (ret != Z_OK && ret != Z_STREAM_END) {
        fprintf(stderr, "Error: Failed to inflate chunk %d: %d\n", chunk_index, ret);
        inflateEnd(&dz->zstream);
        dz->zstream_initialized = false;
        free(out_buf);
        return NULL;
    }

    // Check if fully decompressed
    if (dz->zstream.avail_in != 0) {
        fprintf(stderr, "Error: Incomplete inflate: %d bytes remaining\n", dz->zstream.avail_in);
        inflateEnd(&dz->zstream);
        dz->zstream_initialized = false;
        free(out_buf);
        return NULL;
    }

    // Each chunk is an independent deflate block, reset state before next inflate

    *out_size = header->chunk_size - dz->zstream.avail_out;
    return out_buf;
}

/**
 * Find chunk in cache, return cache index
 * Also find LRU slot (smallest stamp)
 */
static int find_cache_entry(dictzip *dz, int chunk_index, int *lru_index) {
    int found_index = -1;
    uint32_t min_stamp = UINT32_MAX;

    for (int i = 0; i < dz->cache_size; i++) {
        // Check for cache hit
        if (dz->cache[i].chunk_index == chunk_index) {
            found_index = i;
        }

        // Find LRU (smallest stamp)
        if (dz->cache[i].stamp < min_stamp) {
            min_stamp = dz->cache[i].stamp;
            *lru_index = i;
        }
    }

    return found_index;
}

/**
 * Add chunk to cache using pre-allocated buffer and stamp mechanism.
 * Copies data into cache buffer; caller retains ownership of input data.
 */
static dictzip_cache_entry *add_cache_entry(dictzip *dz, int cache_index,
                                               int chunk_index,
                                               const uint8_t *data, size_t size) {
    dictzip_cache_entry *entry = &dz->cache[cache_index];

    // Allocate buffer if slot has none
    if (!entry->data) {
        entry->data = (uint8_t *)malloc(dz->header.chunk_size);
        if (!entry->data) {
            fprintf(stderr, "Error: Memory allocation failed for cache buffer\n");
            return NULL;
        }
    }

    // Copy decompressed data to cache buffer
    memcpy(entry->data, data, size);

    // Update metadata
    entry->chunk_index = chunk_index;
    entry->size = size;
    entry->stamp = ++dz->stamp;

    return entry;
}

// ============================================================
// Public API implementation
// ============================================================

dictzip *dictzip_open(const char *filename) {
    // Runtime little-endian check: low byte of 0x8B1F should be 0x1F
    const uint16_t endian_test = 0x8B1F;
    if (*(const uint8_t *)&endian_test != 0x1F) {
        fprintf(stderr, "Error: dictzip requires little-endian platform\n");
        return NULL;
    }

    if (!filename) {
        fprintf(stderr, "Error: filename is NULL\n");
        return NULL;
    }

    // Allocate dictzip structure
    dictzip *dz = (dictzip *)calloc(1, sizeof(dictzip));
    if (!dz) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    // Save filename
    dz->filename = strdup(filename);
    if (!dz->filename) {
        fprintf(stderr, "Error: Memory allocation failed for filename\n");
        free(dz);
        return NULL;
    }

    // Open file
    dz->file = fopen(filename, "rb");
    if (!dz->file) {
        fprintf(stderr, "Error: Failed to open file: %s\n", strerror(errno));
        free(dz->filename);
        free(dz);
        return NULL;
    }

    // Parse header
    if (parse_dictzip_header(dz) != 0) {
        fclose(dz->file);
        free(dz->filename);
        free(dz);
        return NULL;
    }

    // Initialize cache
    dz->cache_size = 0;
    dz->stamp = 0;

    dz->cache = (dictzip_cache_entry *)calloc(DICTZIP_CHUNK_CACHE_SIZE,
                                                  sizeof(dictzip_cache_entry));
    if (!dz->cache) {
        fprintf(stderr, "Error: Memory allocation failed for cache\n");
        free(dz->header.chunks);
        free(dz->header.offsets);
        fclose(dz->file);
        free(dz->filename);
        free(dz);
        return NULL;
    }

    // Initialize all cache slots
    for (int i = 0; i < DICTZIP_CHUNK_CACHE_SIZE; i++) {
        dz->cache[i].chunk_index = -1;
        dz->cache[i].data = NULL;
        dz->cache[i].stamp = 0;
        dz->cache[i].size = 0;
    }

    return dz;
}

void dictzip_close(dictzip *dz) {
    if (!dz) {
        return;
    }

    // Clear cache
    dictzip_clear_cache(dz);
    free(dz->cache);

    // Free header resources
    if (dz->header.chunks) {
        free(dz->header.chunks);
    }
    if (dz->header.offsets) {
        free(dz->header.offsets);
    }

    // Free zlib stream
    if (dz->zstream_initialized) {
        inflateEnd(&dz->zstream);
    }

    // Close file
    if (dz->file) {
        fclose(dz->file);
    }

    // Free filename
    if (dz->filename) {
        free(dz->filename);
    }

    free(dz);
}

uint8_t *dictzip_read(dictzip *dz, uint32_t offset, uint32_t size,
                      uint32_t *out_size) {
    if (!dz) {
        return NULL;
    }

    if (out_size) {
        *out_size = 0;
    }

    dictzip_header *header = &dz->header;

    // Check if offset is beyond file range
    if (offset >= header->uncompressed_size) {
        fprintf(stderr, "Error: Offset beyond file size: %u >= %u\n", offset, header->uncompressed_size);
        return NULL;
    }

    // Adjust size to not exceed file range
    uint32_t remaining = header->uncompressed_size - offset;
    if (size > remaining) {
        size = remaining;
    }

    // Allocate output buffer
    uint8_t *result = (uint8_t *)malloc(size);
    if (!result) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }

    uint32_t bytes_copied = 0;
    uint32_t current_offset = offset;

    while (bytes_copied < size) {
        // Calculate current chunk index
        int chunk_index = current_offset / header->chunk_size;

        // Calculate offset within chunk
        int chunk_offset = current_offset % header->chunk_size;

        // Calculate bytes to copy this iteration
        int bytes_in_chunk = header->chunk_size - chunk_offset;
        int bytes_needed = size - bytes_copied;
        int bytes_to_copy = (bytes_in_chunk < bytes_needed) ? bytes_in_chunk : bytes_needed;

        // Search cache and find LRU slot
        int lru_index;
        int cache_index = find_cache_entry(dz, chunk_index, &lru_index);

        uint8_t *chunk_data = NULL;
        size_t chunk_size = 0;

        if (cache_index >= 0) {
            // Cache hit
            dictzip_cache_entry *cache_entry = &dz->cache[cache_index];
            chunk_data = cache_entry->data;
            chunk_size = cache_entry->size;

            // Update access timestamp
            cache_entry->stamp = ++dz->stamp;
        } else {
            // Cache miss, decompress chunk
            chunk_data = inflate_chunk(dz, chunk_index, &chunk_size);
            if (!chunk_data) {
                free(result);
                return NULL;
            }

            // Determine which cache slot to use
            int target_index;
            if (dz->cache_size < DICTZIP_CHUNK_CACHE_SIZE) {
                // Cache not full, use new slot
                target_index = dz->cache_size;
                dz->cache_size++;
            } else {
                // Cache full, evict LRU slot
                target_index = lru_index;
            }

            // Add to cache (copies data; we still own chunk_data)
            dictzip_cache_entry *cache_entry = add_cache_entry(dz, target_index,
                                                                  chunk_index,
                                                                  chunk_data, chunk_size);
            if (!cache_entry) {
                // Cache failed, use decompressed data directly
                memcpy(result + bytes_copied, chunk_data + chunk_offset, bytes_to_copy);
                bytes_copied += bytes_to_copy;
                current_offset += bytes_to_copy;
                free(chunk_data);
                continue;
            }

            free(chunk_data);
            chunk_data = cache_entry->data;
            chunk_size = cache_entry->size;
        }

        // Copy data
        memcpy(result + bytes_copied, chunk_data + chunk_offset, bytes_to_copy);
        bytes_copied += bytes_to_copy;
        current_offset += bytes_to_copy;
    }

    if (out_size) {
        *out_size = bytes_copied;
    }

    return result;
}

uint32_t dictzip_get_uncompressed_size(dictzip *dz) {
    if (!dz) {
        return 0;
    }
    return dz->header.uncompressed_size;
}

static void dictzip_clear_cache(dictzip *dz) {
    if (!dz) {
        return;
    }

    for (int i = 0; i < dz->cache_size; i++) {
        if (dz->cache[i].data) {
            free(dz->cache[i].data);
            dz->cache[i].data = NULL;
        }
    }

    dz->cache_size = 0;
}

