#include <Windows.h>
#include "rt.h"
#include "lzn.h"

// #define FROM_FILE __FILE__
   #define FROM_FILE "c:/tmp/ui_app.c"
// #define FROM_FILE "c:/tmp/program.exe"
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

static double compress_ratio(const char* fn,
        const uint8_t* data, size_t bytes, uint8_t window, uint8_t lookahead) {
    FILE* out = null; // compressed file
    errno_t r = fopen_s(&out, fn, "wb") != 0;
    if (r != 0 || out == null) {
        rt_println("Failed to create \"%s\"", fn);
        return r;
    }
    lzn_stream_t stream = { .that = (void*)out, .write = lzn_write };
    lzn_t lz = {
        .bits = {
            .min_match  =  3,
            .window     = window,
            .lookahead  = lookahead
        },
        .stream = &stream,
        .stats  = false
    };
    r = lzn.compress(&lz, data, bytes);
    fclose(out);
    rt_assert(r == 0);
    if (r != 0) {
        rt_println("Failed to compress");
    }
    return lz.compressed_bytes * 100.0 / bytes;
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
        .bits = {
            .min_match  =  3,
            .window     = 16,
            .lookahead  =  5
        },
        .stream = &stream,
        .stats  = true
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
        bool same = size == bytes && memcmp(input, data, bytes) == 0;
        rt_println("same: %s", same ? "true" : "false");
        rt_assert(same);
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
#if 0
    int8_t min_window = 0xFF;
    int8_t min_lookahead = 0xFF;
    double min_percentage = 100.0;
    rt_println("window, lookahead, compression");
    for (uint8_t window = 8; window <= 17; window++) {
        for (uint8_t lookahead = 3; lookahead <= 9; lookahead++) {
            double percentage =  compress_ratio("compressed.bin",
                                 data, bytes, window, lookahead);
            rt_println("%2d, %2d, %.3f",
                (int)window, (int)lookahead, percentage);
            if (percentage < min_percentage) {
                min_window = window;
                min_lookahead = lookahead;
                min_percentage = percentage;
            }
        }
    }
    rt_println("min window: %d min lookahead: %d %.1f%%",
        (int)min_window, (int)min_lookahead, min_percentage);
#endif
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

w la %%
16,5,29.678 ***
17,5,29.977
15,5,30.015
16,6,30.225
17,6,30.441
15,6,30.625
14,5,30.861
16,4,31.026
15,4,31.166
16,7,31.261
17,7,31.446
14,6,31.528
17,4,31.535
15,7,31.694
14,4,31.815 ***
16,8,32.387
17,8,32.54
14,7,32.622
13,5,32.692
15,8,32.845
13,6,33.43
13,4,33.462
16,9,33.512
17,9,33.646
14,8,33.799
15,9,33.996
12,5,34.454
13,7,34.549
14,9,34.981
12,4,35.07
12,6,35.191
13,8,35.732
15,3,36.126
14,3,36.234
12,7,36.285
16,3,36.552
11,5,36.597
13,9,36.927
11,4,37.074
13,3,37.258
11,6,37.328
12,8,37.442
17,3,37.531
11,7,38.39
12,3,38.473
12,9,38.6
11,8,39.503
10,5,39.973
11,3,40.069
10,4,40.317
11,9,40.622
10,6,40.717
10,7,41.747
10,8,42.822
10,3,42.854
10,9,43.903
9,5,44.247
9,4,44.399
9,6,44.997
9,7,45.995
9,3,46.466
9,8,47.019
9,9,48.056
8,5,49.703
8,4,49.735
8,6,50.402
8,7,51.324
8,3,51.362 ***
8,8,52.285
8,9,53.238
*/