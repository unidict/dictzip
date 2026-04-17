//
//  dictzip.h
//
//  Created by kejinlu on 2026-04-09
//


#ifndef dictzip_h
#define dictzip_h

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// dictzip file handle (opaque pointer)
// ============================================================

typedef struct dictzip dictzip;

// ============================================================
// API functions
// ============================================================

/**
 * Open dictzip file
 * @param filename File path
 * @return dictzip handle on success, NULL on failure
 */
dictzip *dictzip_open(const char *filename);

/**
 * Close dictzip file
 * @param dz dictzip handle
 */
void dictzip_close(dictzip *dz);

/**
 * Read and decompress data
 * @param dz dictzip handle
 * @param offset Data offset (relative to decompressed data)
 * @param size Data size
 * @param out_size Output actual bytes read
 * @return Decompressed data pointer on success (caller must free), NULL on failure
 */
uint8_t *dictzip_read(dictzip *dz, uint32_t offset, uint32_t size,
                      uint32_t *out_size);

/**
 * Get uncompressed file size
 * @param dz dictzip handle
 * @return Uncompressed file size
 */
uint32_t dictzip_get_uncompressed_size(dictzip *dz);

#ifdef __cplusplus
}
#endif

#endif /* dictzip_h */
