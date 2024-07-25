#include <Windows.h>
#include "rt.h"
#include "lzn.h"

   #define FROM_FILE __FILE__
// #define FROM_FILE "c:/tmp/sqlite3.c"
// #undef  FROM_FILE

errno_t lzn_read(lzn_t* lzn) {
    FILE* f = (FILE*)lzn->stream->that;
    const size_t bytes = sizeof(lzn->buffer);
    return fread(&lzn->buffer, 1, bytes, f) == bytes ? 0 : errno;
}

errno_t lzn_write(lzn_t* lzn) {
    FILE* f = (FILE*)lzn->stream->that;
    const size_t bytes = sizeof(lzn->buffer);
    return fwrite(&lzn->buffer, 1, bytes, f) == bytes ? 0 : errno;
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
        .min_match  =  3,
        .window     = 14,
        .lookahead  =  4,
        .stream = &stream,
        .stats = true
    };
    r = lzn.compress(&lz, data, bytes);
    fclose(out);
    rt_assert(r == 0);
    if (r != 0) {
        rt_println("Failed to compress");
    } else {
        rt_println("%ld -> %ld %.1f%%",
                bytes, lz.compressed_bytes,
                lz.compressed_bytes * 100.0 / bytes);
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
    r = lzn.decompress(&lz, data, bytes);
    fclose(in);
    rt_assert(r == 0);
    if (r == 0) {
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
    errno_t r = compress("compressed.bin", data, bytes);
    if (r == 0) {
        r = decompress_and_compare("compressed.bin", data, bytes);
    }
#ifdef FROM_FILE
    free(data);
#endif
    return 0;
}

/*

Possible optimizations (see bits distribution below)
   position can be bit inverted:
   position ^ ((1ULL << lz.window) -  1)
   and both position and length can be
   using Golumb Rice like encoding in less bits.

For:
    lz.min_match  =  3,
    lz.window     = 14,
    lz.lookahead  =  4,

sqlite3.c file

2,940,336:9,089,040 32.4%

max_match_ofs: 16383 max_match_len: 18

log2(length):
[ 0]  16.994092  16.994092 179763
[ 1]  13.223237  30.217329 139875
[ 2]  19.757288  49.974617 208992
[ 3]  22.327819  72.302436 236183
[ 4]  27.697564 100.000000 292984

log2(positions)
[14]  18.774774  18.774774 198599
[13]  15.722771  34.497545 166315
[12]  13.037095  47.534640 137906
[11]  10.910695  58.445335 115413
[10]   9.338370  67.783705 98781
[ 9]   8.098151  75.881856 85662
[ 8]   7.066857  82.948713 74753
[ 7]   7.527153  90.475866 79622
[ 6]   5.441498  95.917364 57560
[ 5]   2.433926  98.351290 25746
[ 4]   0.933355  99.284645 9873
[ 3]   0.367556  99.652202 3888
[ 2]   0.347798 100.000000 3679
[ 1]   0.000000 100.000000 0
[ 0]   0.000000 100.000000 0

*/