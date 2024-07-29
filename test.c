#ifdef _MSC_VER // /Wall is very useful but yet a bit overreaching:
#pragma warning(disable: 4820) // 'bytes' bytes padding added after construct 'name'
#pragma warning(disable: 5045) // Spectre mitigation for memory load
#pragma warning(disable: 4710) // function not inlined
#pragma warning(disable: 4711) // function selected for automatic inline expansion
#endif

#include "lz77.h"
#include "rt.h"

#if defined(_MSC_VER) && defined(NEED_WIN32_API)
#define STRICT // opposite is SLOPPY, right?
#define WIN32_LEAN_AND_MEAN // exclude stuff which no one needs/wants
#define VC_EXTRALEAN        // exclude even more stuff
#include <Windows.h>
#endif

enum { lzn_window_bits = 11 };

#define FILE_NAME __FILE__

static uint64_t file_read(lz77_t* lz) {
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

static void file_write(lz77_t* lz, uint64_t buffer) {
    if (lz->error == 0) {
        FILE* f = (FILE*)lz->that;
        const size_t bytes = sizeof(buffer);
        if (fwrite(&buffer, 1, bytes, f) != bytes) {
            lz->error = errno;
        }
    }
}

static bool file_exist(const char* filename) {
    struct stat st = {0};
    return stat(filename, &st) == 0;
}

static const char* input_file;

static errno_t compress(const char* fn, const uint8_t* data, size_t bytes) {
    FILE* out = null; // compressed file
    errno_t r = fopen_s(&out, fn, "wb") != 0;
    if (r != 0 || out == null) {
        rt_println("Failed to create \"%s\": %s", fn, strerror(r));
        return r;
    }
    lz77_t lz = {
        .that = (void*)out,
        .write = file_write
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
            if (input_file != null) {
                rt_println("%7lld -> %7lld %5.1f%% of \"%s\"",
                            bytes, lz.written, percent, input_file);
            } else {
                rt_println("%7lld -> %7lld %5.1f%%",
                            bytes, lz.written, percent);
            }
        }
    }
    return r;
}

static errno_t verify(const char* fn, const uint8_t* input, size_t size) {
    // decompress and compare
    FILE* in = null; // compressed file
    errno_t r = fopen_s(&in, fn, "rb");
    if (r != 0 || in == null) {
        rt_println("Failed to open \"%s\"", fn);
        return r;
    }
    lz77_t lz = {
        .that = (void*)in,
        .read = file_read
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
        if (!same) {
            rt_println("compress() and decompress() are not the same");
            // ENODATA is not original posix error but is OpenGroup error
            r = ENODATA; // or EIO
        } else if (bytes < 128) {
            rt_println("decompressed: %s", data);
        }
    }
    free(data);
    if (r != 0) {
        rt_println("Failed to decompress");
    }
    return r;
}

static errno_t file_size(FILE* f, size_t* size) {
    // on error returns (fpos_t)-1 and sets errno
    fpos_t pos = 0;
    if (fgetpos(f, &pos) != 0) { return errno; }
    if (fseek(f, 0, SEEK_END) != 0) { return errno; }
    fpos_t eof = 0;
    if (fgetpos(f, &eof) != 0) { return errno; }
    if (fseek(f, 0, SEEK_SET) != 0) { return errno; }
    if ((uint64_t)eof > SIZE_MAX) { return E2BIG; }
    *size = (size_t)eof;
    return 0;
}

static errno_t read_fully(FILE* f, const uint8_t* *data, size_t *bytes) {
    size_t size = 0;
    errno_t r = file_size(f, &size);
    if (r != 0) { return r; }
    if (size > SIZE_MAX) { return E2BIG; }
    uint8_t* p = (uint8_t*)malloc(size); // does set errno on failure
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
    r = read_fully(f, data, bytes); // to the heap
    (void)fclose(f); // file was open for reading fclose() should not fail
    if (r != 0) {
        rt_println("Failed to read file \"%s\": %s", FILE_NAME, strerror(r));
        fclose(f);
        return r;
    }
    return fclose(f) == 0 ? 0 : errno;
}

static errno_t test(const uint8_t* data, size_t bytes) {
    const char* compressed = "~compressed~.bin";
    errno_t r = compress(compressed, data, bytes);
    if (r == 0) {
        r = verify(compressed, data, bytes);
    }
    (void)remove(compressed);
    return r;
}

static errno_t test_compression(const char* fn) {
    errno_t r = 0;
    uint8_t* data = null;
    size_t bytes = 0;
    r = read_whole_file(fn, &data, &bytes);
    if (r != 0) { return r; }
    input_file = fn;
    return test(data, bytes);
}

int main(int argc, const char* argv[]) {
    (void)argc; (void)argv;
    errno_t r = 0;
    if (r == 0) {
        uint8_t data[1024] = {0};
        r = test(data, sizeof(data));
        // lz77 deals with run length encoding in amazing overlapped way
        for (int32_t i = 0; i < sizeof(data); i += 4) {
            memcpy(data + i, "\x01\x02\x03\x04", 4);
        }
        r = test(data, sizeof(data));
    }
    if (r == 0) {
        const char* data = "Hello World Hello.World Hello World";
        size_t bytes = strlen((const char*)data);
        r = test((const uint8_t*)data, bytes);
    }
 #ifdef FILE_NAME
    if (r == 0) {
        r = test_compression(FILE_NAME);
    }
#endif
    if (file_exist("test/ut.h")) {
        r = test_compression("test/ut.h");
    }
    if (file_exist("test/ui.h")) {
        r = test_compression("test/ui.h");
    }
    if (file_exist("test/sqlite3.c")) {
        r = test_compression("test/sqlite3.c");
    }
    return r;
}

#define lz77_assert(b, ...) rt_assert(b, __VA_ARGS__)
#define lz77_println(...)   rt_println(__VA_ARGS__)

#define lz77_historgram // to dump histograms on compress
#undef  lz77_historgram // no histograms

#define lz77_implementation // this will include the implementation of lz77
#include "lz77.h"
