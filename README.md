# libdictzip

A C library for reading dictzip (.dz) compressed files with random access support.

## Features

- Parse dictzip (gzip extension) file headers per RFC 1952
- Random access decompression by byte offset
- LRU chunk cache for efficient repeated reads
- Minimal API: open, read, close
- Cross-platform: Linux, macOS, Windows

## Prerequisites

- C11 compiler
- zlib
- CMake 3.14+

### Install dependencies

**macOS**
```bash
brew install cmake zlib
```

**Ubuntu / Debian**
```bash
sudo apt-get install cmake zlib1g-dev
```

**Windows (vcpkg)**
```
vcpkg install zlib:x64-windows
```

## Build

```bash
git clone https://github.com/kejinlu/dictzip.git
cd dictzip
cmake -B build
cmake --build build
```

## Test

```bash
ctest --test-dir build --verbose
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `DICTZIP_BUILD_TESTS` | ON | Build test executable |
| `BUILD_SHARED_LIBS` | OFF | Build shared library instead of static |

## Quick Start

```c
#include <stdio.h>
#include <stdlib.h>
#include "dictzip.h"

int main(void) {
    dictzip *dz = dictzip_open("dictionary.dict.dz");
    if (!dz) return 1;

    uint32_t total = dictzip_get_uncompressed_size(dz);
    printf("Uncompressed size: %u\n", total);

    uint32_t out_size = 0;
    uint8_t *data = dictzip_read(dz, 0, 256, &out_size);
    if (data) {
        fwrite(data, 1, out_size, stdout);
        free(data);
    }

    dictzip_close(dz);
    return 0;
}
```

## API

### `dictzip *dictzip_open(const char *filename)`

Open a dictzip file and parse its header. Returns a handle on success, `NULL` on failure.

### `void dictzip_close(dictzip *dz)`

Close the file handle and free all resources.

### `uint8_t *dictzip_read(dictzip *dz, uint32_t offset, uint32_t size, uint32_t *out_size)`

Read and decompress `size` bytes starting at `offset` in the uncompressed data. Returns a heap-allocated buffer (caller must `free`), or `NULL` on failure. `out_size` receives the actual number of bytes read.

### `uint32_t dictzip_get_uncompressed_size(dictzip *dz)`

Returns the total size of the uncompressed data.

## Architecture

| File | Responsibility |
|------|---------------|
| `src/dictzip.h` | Public API header |
| `src/dictzip.c` | Header parsing, chunk decompression, LRU cache, read logic |

## License

MIT License

Copyright (c) 2026 kejinlu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
