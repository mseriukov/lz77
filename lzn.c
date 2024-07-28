#include <Windows.h>
#include "rt.h"
#define lzn_implementation
#include "lzn.h"

enum { lzn_window_bits = 13 };

// #define FROM_FILE "c:/tmp/ut.h"  // 11,4 36.5%
// #define FROM_FILE "c:/tmp/ui.h"
// #define FROM_FILE "c:/tmp/program.exe"
   #define FROM_FILE "c:/tmp/sqlite3.c" // 13,5 38.2% 13,6 39.4% 13,4 %37.1 12,4 39.0% 11,3 42.9 11,4 42.9

// #define FROM_FILE __FILE__

// #undef  FROM_FILE

uint64_t lzn_read(lzn_t* lzn) {
    uint64_t buffer = 0;
    if (lzn->error == 0) { // sticky
        FILE* f = (FILE*)lzn->that;
        const size_t bytes = sizeof(buffer);
        if (fread(&buffer, 1, bytes, f) != bytes) {
            // reading past end of file does not set errno
            lzn->error = errno == 0 ? EBADF : errno;
            rt_swear(lzn->error != 0);
        }
    }
    return buffer;
}

void lzn_write(lzn_t* lzn, uint64_t buffer) {
    if (lzn->error == 0) {
        FILE* f = (FILE*)lzn->that;
        const size_t bytes = sizeof(buffer);
        if (fwrite(&buffer, 1, bytes, f) != bytes) {
            lzn->error = errno;
            rt_swear(lzn->error != 0);
        }
    }
}

static errno_t compress(const char* fn, const uint8_t* data, size_t bytes) {
    FILE* out = null; // compressed file
    errno_t r = fopen_s(&out, fn, "wb") != 0;
    if (r != 0 || out == null) {
        rt_println("Failed to create \"%s\"", fn);
        return r;
    }
    lzn_t lz = {
        .that = (void*)out,
        .write = lzn_write
    };
    lzn.write_header(&lz, bytes, lzn_window_bits);
    lzn.compress(&lz, data, bytes, lzn_window_bits);
    fclose(out);
    rt_assert(lz.error == 0);
    if (lz.error != 0) {
        rt_println("Failed to compress");
    } else {
        rt_println("%ld -> %ld %.1f%%",
                bytes, lz.bytes_written,
                lz.bytes_written * 100.0 / bytes);
    }
    return r;
}

static errno_t decompress_and_compare(const char* fn, const uint8_t* input,
        size_t size) {
    FILE* in = null; // compressed file
    errno_t r = fopen_s(&in, fn, "rb");
    if (r != 0 || in == null) {
        rt_println("Failed to open \"%s\"", fn);
        return r;
    }
    lzn_t lz = {
        .that = (void*)in,
        .read = lzn_read
    };
    size_t bytes = 0;
    uint8_t window_bits = 0;
    lzn.read_header(&lz, &bytes, &window_bits);
    rt_assert(lz.error == 0 && bytes == size && window_bits == lzn_window_bits);
    uint8_t* data = (uint8_t*)malloc(bytes + 1);
    if (data == null) {
        rt_println("Failed to allocate memory for decompressed data");
        fclose(in);
        return ENOMEM;
    }
    data[bytes] = 0x00;
    lzn.decompress(&lz, data, bytes, lzn_window_bits);
    fclose(in);
    rt_assert(lz.error == 0);
    if (lz.error == 0) {
        rt_println("same: %s", memcmp(data, data, bytes) == 0 ?
                   "true" : "false");
    }
    if (bytes < 128) {
        rt_println("Decompressed: %s", data);
    }
    free(data);
    if (r != 0) {
        rt_println("Failed to decompress");
    }
    return r;
}

int main(int argc, const char* argv[]) {
    uint8_t* data = null;
    size_t bytes = 0;
#ifdef FROM_FILE
    {
        FILE* in = null;
        if (fopen_s(&in, FROM_FILE, "rb") != 0 || in == null) {
            rt_println("Failed to open inout file");
            return 1;
        }
        fseek(in, 0, SEEK_END);
        bytes = _ftelli64(in);
        fseek(in, 0, SEEK_SET);
        data = (uint8_t*)malloc(bytes);
        if (data == null) {
            rt_println("Failed to allocate");
            return ENOMEM;
        }
        fread(data, 1, bytes, in);
        fclose(in);
    }
#else
    data = (uint8_t*)"Hello World Hello WorlD Hello World";
    bytes = strlen((const char*)data);
#endif
    rt_println();
    errno_t r = compress("compressed.bin", data, bytes);
    if (r == 0) {
        r = decompress_and_compare("compressed.bin", data, bytes);
    }
#ifdef FROM_FILE
    free(data);
#endif
    return 0;
}
