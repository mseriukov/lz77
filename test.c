#ifdef _MSC_VER
#pragma warning(disable: 4820) // 'bytes' bytes padding added after construct 'name'
#pragma warning(disable: 5045) // Spectre mitigation for memory load
#pragma warning(disable: 4710) // function not inlined
#pragma warning(disable: 4711) // function selected for automatic inline expansion
#endif
#include "lz77.h"

#ifdef _MSC_VER
#pragma warning(disable: 4668) // preprocessor macro replacing with '0' for '#if/#elif'
#include <Windows.h>
#endif

#include "rt.h"

enum { lzn_window_bits = 13 };

// #define FILE_NAME "c:/tmp/ut.h"  // 11,4 36.5%
// #define FILE_NAME "c:/tmp/ui.h"
// #define FILE_NAME "c:/tmp/program.exe"
// #define FILE_NAME "c:/tmp/sqlite3.c" // 13,5 38.2% 13,6 39.4% 13,4 %37.1 12,4 39.0% 11,3 42.9 11,4 42.9

   #define FILE_NAME __FILE__

// #undef  FILE_NAME

uint64_t lz_read_file(lz77_t* lz) {
    uint64_t buffer = 0;
    if (lz->error == 0) { // sticky
        FILE* f = (FILE*)lz->that;
        const size_t bytes = sizeof(buffer);
        if (fread(&buffer, 1, bytes, f) != bytes) {
            // reading past end of file does not set errno
            lz->error = errno == 0 ? EBADF : errno;
        }
    }
    return buffer;
}

void lzn_write_file(lz77_t* lz, uint64_t buffer) {
    if (lz->error == 0) {
        FILE* f = (FILE*)lz->that;
        const size_t bytes = sizeof(buffer);
        if (fwrite(&buffer, 1, bytes, f) != bytes) {
            lz->error = errno;
        }
    }
}

static errno_t compress(const char* fn, const uint8_t* data, size_t bytes) {
    FILE* out = null; // compressed file
    errno_t r = fopen_s(&out, fn, "wb") != 0;
    if (r != 0 || out == null) {
        rt_println("Failed to create \"%s\": %s", fn, strerror(r));
        return r;
    }
    lz77_t lz = {
        .that = (void*)out,
        .write = lzn_write_file
    };
    lz77.write_header(&lz, bytes, lzn_window_bits);
    lz77.compress(&lz, data, bytes, lzn_window_bits);
    rt_assert(lz.error == 0);
    r = fclose(out) == 0 ? 0 : errno; // e.g. overflow writing buffered output
    if (r != 0) {
        rt_println("Failed to flush on file close: %s", strerror(r));
    } else {
        r = lz.error;
        if (r) {
            rt_println("Failed to compress: %s", strerror(r));
        } else {
            double percent = lz.written * 100.0 / bytes;
            rt_println("%ld -> %ld %.1f%%", bytes, lz.written, percent);
        }
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
    lz77_t lz = {
        .that = (void*)in,
        .read = lz_read_file
    };
    size_t bytes = 0;
    uint8_t window_bits = 0;
    lz77.read_header(&lz, &bytes, &window_bits);
    rt_assert(lz.error == 0 && bytes == size && window_bits == lzn_window_bits);
    uint8_t* data = (uint8_t*)malloc(bytes + 1);
    if (data == null) {
        rt_println("Failed to allocate memory for decompressed data");
        fclose(in);
        return ENOMEM;
    }
    data[bytes] = 0x00;
    lz77.decompress(&lz, data, bytes, lzn_window_bits);
    fclose(in);
    rt_assert(lz.error == 0);
    if (lz.error == 0) {
        const bool same = size == bytes && memcmp(input, data, bytes) == 0;
        rt_assert(same);
        rt_println("same: %s", same ? "true" : "false");
        if (bytes < 128) {
            rt_println("Decompressed: %s", data);
        }
    }
    free(data);
    if (r != 0) {
        rt_println("Failed to decompress");
    }
    return r;
}

// assumption: fpos_t is 64 bit signed integer (if not code need rework)

static_assert(sizeof(fpos_t) == sizeof(uint64_t), "fpos_t is not 64 bit");
static_assert(((fpos_t)(uint64_t)(-1LL)) < 0, "fpos_t is not signed type");

static fpos_t file_size(FILE* f) {
    // on error returns (fpos_t)-1 and sets errno
    fpos_t pos = 0;
    if (fgetpos(f, &pos) != 0) { return (fpos_t)-1; }
    if (fseek(f, 0, SEEK_END) != 0) { return (fpos_t)-1; }
    fpos_t size = 0;
    if (fgetpos(f, &size) != 0) { return (fpos_t)-1; }
    if (fseek(f, 0, SEEK_SET) != 0) { return (fpos_t)-1; }
    return size;
}

static errno_t read_file_to_heap(FILE* f, const uint8_t* *data, size_t *bytes) {
    fpos_t size = file_size(f);
    if (size < 0) { return errno; }
    if (size > SIZE_MAX) { return E2BIG; }
    uint8_t* p = (uint8_t*)malloc(size);
    if (p == null) { return errno; }
    if (fread(p, 1, size, f) != (size_t)size) { free(p); return errno; }
    *data = p;
    *bytes = (size_t)size;
    return 0;
}

static errno_t read_whole_file(const char* fn, const uint8_t* *data, size_t *bytes) {
    FILE* f = null;
    errno_t r = fopen_s(&f, fn, "rb");
    if (r != 0) {
        rt_println("Failed to open file \"%s\": %s", FILE_NAME, strerror(r));
        return r;
    }
    r = read_file_to_heap(f, data, bytes);
    (void)fclose(f); // file was open for reading fclose() should not fail
    if (r != 0) {
        rt_println("Failed to read file \"%s\": %s", FILE_NAME, strerror(r));
        fclose(f);
        return r;
    }
    return fclose(f) == 0 ? 0 : errno;
}

static errno_t test(const uint8_t* data, size_t bytes) {
    errno_t r = compress("compressed.bin", data, bytes);
    if (r == 0) {
        r = decompress_and_compare("compressed.bin", data, bytes);
    }
    return r;
}

static errno_t test_file_compress(const char* fn) {
    errno_t r = 0;
    uint8_t* data = null;
    size_t bytes = 0;
    r = read_whole_file(fn, &data, &bytes);
    if (r != 0) { return r; }
    return test(data, bytes);
}

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    errno_t r = 0;
    if (r == 0) {
        const uint8_t data[1024] = {0};
        r = test(data, sizeof(data));
    }
    if (r == 0) {
        const char* data = "Hello World Hello.World Hello World";
        size_t bytes = strlen((const char*)data);
        r = test((const uint8_t*)data, bytes);
    }
 #ifdef FILE_NAME
    if (r == 0) {
        r = test_file_compress(FILE_NAME);
    }
#endif
    return r;
}

#define lz77_assert(b, ...) rt_assert(b, __VA_ARGS__)
#define lz77_swear(b, ...)  rt_swear(b, __VA_ARGS__)
#define lz77_println(...)   rt_println(__VA_ARGS__)

#define lz77_historgram // to dump histograms on compress
#undef  lz77_historgram // no histograms

#define lz77_implementation
#include "lz77.h"
