#include <Windows.h>
#include "rt.h"
#include "lzn.h"

// #define FROM_FILE "c:/tmp/ut.h"
// #define FROM_FILE "c:/tmp/ui.h"
// #define FROM_FILE "c:/tmp/program.exe"
// #define FROM_FILE "c:/tmp/sqlite3.c"

#define FROM_FILE __FILE__

// #undef  FROM_FILE

uint64_t lzn_read(lzn_t* lzn) {
    uint64_t buffer = 0;
    if (lzn->stream->error == 0) {
        FILE* f = (FILE*)lzn->stream->that;
        const size_t bytes = sizeof(buffer);
        if (fread(&buffer, 1, bytes, f) != bytes) {
            // reading past end of file does not set errno
            lzn->stream->error = errno == 0 ? EBADF : errno;
            rt_swear(lzn->stream->error != 0);
        } else {
            lzn->stream->bytes_read += bytes;
        }
    }
    return buffer;
}

void lzn_write(lzn_t* lzn, uint64_t buffer) {
    if (lzn->stream->error == 0) {
        FILE* f = (FILE*)lzn->stream->that;
        const size_t bytes = sizeof(buffer);
        if (fwrite(&buffer, 1, bytes, f) != bytes) {
            lzn->stream->error = errno;
            rt_swear(lzn->stream->error != 0);
        } else {
            lzn->stream->bytes_written += bytes;
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
    lzn_stream_t stream = { .that = (void*)out, .write = lzn_write };
    lzn_t lz = {
        .window_bits = 11, // 15 is much better but much slower
        .stream = &stream,
    };
    lzn.compress(&lz, data, bytes);
    fclose(out);
    rt_assert(lz.stream->error == 0);
    if (lz.stream->error != 0) {
        rt_println("Failed to compress");
    } else {
        rt_println("%ld -> %ld %.1f%%",
                bytes, lz.stream->bytes_written,
                lz.stream->bytes_written * 100.0 / bytes);
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
    size_t bytes = 0;
    if (fread(&bytes, sizeof(bytes), 1, in) != 1) {
        r = errno;
        rt_println("Failed to read decompressed length");
        fclose(in);
        return r;
    }
    rewind(in);
    rt_assert(bytes == size);
    lzn_stream_t stream = { .that = (void*)in, .read = lzn_read };
    lzn_t lz = { .stream = &stream };
    uint8_t* data = (uint8_t*)malloc(bytes + 1);
    if (data == null) {
        rt_println("Failed to allocate memory for decompressed data");
        fclose(in);
        return ENOMEM;
    }
    data[bytes] = 0x00;
    lzn.decompress(&lz, data, bytes);
    fclose(in);
    rt_assert(lz.stream->error == 0);
    if (lz.stream->error == 0) {
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
