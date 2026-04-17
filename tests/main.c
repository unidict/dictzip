//
//  main.c
//  tests
//
//  Created by kejinlu on 2026/4/16.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../src/dictzip.h"

// offset and size decoded from eng-zho.index (StarDict base64)
typedef struct {
    const char *word;
    uint32_t offset;
    uint32_t size;
} dict_entry;

int main(int argc, const char * argv[]) {
    // Build path relative to this source file
    const char *src_file = __FILE__;
    const char *last_slash = strrchr(src_file, '/');
    size_t dir_len = last_slash ? (size_t)(last_slash - src_file + 1) : 0;

    char default_path[512];
    snprintf(default_path, sizeof(default_path), "%.*sdata/eng-zho.dict.dz", (int)dir_len, src_file);

    const char *filename = default_path;
    if (argc > 1) {
        filename = argv[1];
    }

    dictzip *dz = dictzip_open(filename);
    if (!dz) {
        fprintf(stderr, "Failed to open: %s\n", filename);
        return EXIT_FAILURE;
    }

    uint32_t total_size = dictzip_get_uncompressed_size(dz);
    printf("Uncompressed size: %u\n\n", total_size);

    // Two sample entries, size calculated from adjacent index entries
    dict_entry entries[] = {
        {"abandon", 654507, 406},
        {"abacus",  653926, 343},
    };

    int count = sizeof(entries) / sizeof(entries[0]);
    for (int i = 0; i < count; i++) {
        dict_entry *e = &entries[i];

        uint32_t out_size = 0;
        uint8_t *data = dictzip_read(dz, e->offset, e->size, &out_size);
        if (!data) {
            fprintf(stderr, "Failed to read \"%s\" (offset=%u, size=%u)\n",
                    e->word, e->offset, e->size);
            dictzip_close(dz);
            return EXIT_FAILURE;
        }

        printf("[%s] offset=%u, size=%u, read=%u\n", e->word, e->offset, e->size, out_size);
        fwrite(data, 1, out_size, stdout);
        printf("\n\n");

        free(data);
    }

    dictzip_close(dz);

    return EXIT_SUCCESS;
}
