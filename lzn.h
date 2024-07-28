#pragma once
#ifndef lzn_definition
#define lzn_definition

// Naive LZ77 implementation inspired by CharGPT discussion
// and my personal passion to compressors in 198x

#include "rt.h"

typedef struct lzn_s lzn_t;

typedef struct lzn_s {
    void*    that;  // caller supplied data
    errno_t  error; // sticky; for read()/write() compress() and decompress()
    // caller supplied read()/write() must error via .error field
    uint64_t (*read)(lzn_t*); //  reads 64 bits
    void     (*write)(lzn_t*, uint64_t  data); // writes 64 bits
    // stream specific stats
    uint64_t bytes_written;
    uint64_t bytes_read;
} lzn_t;

typedef struct lzn_if {
    // `window_bits` [10..20] is a log2 of window size in bytes
    void (*write_header)(lzn_t* lzn, size_t bytes, uint8_t window_bits);
    void (*compress)(lzn_t* lzn, const uint8_t* data, size_t bytes,
                     uint8_t window_bits);
    void (*read_header)(lzn_t* lzn, size_t *bytes, uint8_t *window_bits);
    void (*decompress)(lzn_t* lzn, uint8_t* data, size_t bytes,
                       uint8_t window_bits);
    // Writing and reading envelope of source data `bytes` and
    // `window_bits` is caller's responsibility.
} lzn_if;

extern lzn_if lzn;

#endif // lzn_definition

#ifdef lzn_implementation

#define lzn_historgram

#ifdef lzn_historgram

static inline uint32_t lzn_bit_count(size_t v) {
    uint32_t count = 0;
    while (v) { count++; v >>= 1; }
    return count;
}


static size_t lzn_hist_len[64];
static size_t lzn_hist_pos[64];

#define lzn_histogram_pos_len(pos, len) do {                    \
    lzn_hist_pos[lzn_bit_count(pos)]++;                         \
    lzn_hist_len[lzn_bit_count(len)]++;                         \
} while (0)

#define lzn_dump_histograms() do {                              \
    rt_println("Histogram log2(len):");                         \
    for (int8_t i = 0; i < 64; i++) {                           \
        if (lzn_hist_len[i] > 0) {                              \
            rt_println("len[%d]: %lld", i, lzn_hist_len[i]);    \
        }                                                       \
    }                                                           \
    rt_println("Histogram log2(pos):");                         \
    for (int8_t i = 0; i < 64; i++) {                           \
        if (lzn_hist_pos[i] > 0) {                              \
            rt_println("pos[%d]: %lld", i, lzn_hist_pos[i]);    \
        }                                                       \
    }                                                           \
} while (0)

#else

#define lzn_histogram_pos_len(pos, len) do { } while (0)
#define lzn_dump_histograms()  do { } while (0)

#endif

static inline void lzn_write_bit(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp, uint64_t bit) {
    errno_t r = 0;
    if (*bp == 64) {
        lzn->write(lzn, *buffer);
        *buffer = 0;
        *bp = 0;
        lzn->bytes_written += 8;
    }
    *buffer |= bit << *bp;
    (*bp)++;
}

static inline void lzn_write_bits(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp, uint64_t bits, uint32_t n) {
    rt_assert(n <= 64);
    while (n > 0) {
        lzn_write_bit(lzn, buffer, bp, bits & 1);
        bits >>= 1;
        n--;
    }
}

static inline void lzn_write_as_chunks(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp, uint64_t bits, uint8_t chunk) {
    do {
        lzn_write_bits(lzn, buffer, bp, bits, chunk);
        bits >>= chunk;
        lzn_write_bit(lzn, buffer, bp, bits != 0); // stop bit
    } while (bits != 0);
}

#pragma push_macro("write_bit")
#pragma push_macro("write_bits")
#pragma push_macro("write_chunked")
#pragma push_macro("write_pos_len")

#pragma push_macro("read_bit")
#pragma push_macro("read_bits")
#pragma push_macro("read_chunked")
#pragma push_macro("read_pos_len")

#pragma push_macro("return_invalid")

#define return_invalid do {                                 \
    lzn->error = EINVAL;                                    \
    return;                                                 \
} while (0)

#define write_bit(bit) do {                                 \
    lzn_write_bit(lzn, &buffer, &bp, bit);                  \
    if (lzn->error) { return; }                             \
} while (0)

#define write_bits(bits, n) do {                            \
    lzn_write_bits(lzn, &buffer, &bp, bits, n);             \
    if (lzn->error) { return; }                             \
} while (0)

#define write_chunked(bits, chunk) do {                     \
    lzn_write_as_chunks(lzn, &buffer, &bp, bits, chunk);    \
    if (lzn->error) { return; }                             \
} while (0)

#define write_pos_len(pos, len, window_bits, chunk) do {    \
    rt_assert(0 < pos && pos < window);                     \
    rt_assert(0 < len);                                     \
    write_chunked(pos, chunk);                              \
    write_chunked(len, chunk);                              \
} while (0)

static void lzn_write_header(lzn_t* lzn, size_t bytes, uint8_t window_bits) {
    if (lzn->error != 0) { return; }
    if (window_bits < 10 || window_bits > 20) { return_invalid; }
    lzn->write(lzn, (uint64_t)bytes);
    if (lzn->error != 0) { return; }
    lzn->write(lzn, (uint64_t)window_bits);
}

static void lzn_compress(lzn_t* lzn, const uint8_t* data, size_t bytes,
        uint8_t window_bits) {
    if (lzn->error != 0) { return; }
    if (window_bits < 10 || window_bits > 20) { return_invalid; }
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t chunk = (window_bits - 4) / 2;
    rt_println("chunk: %d", chunk);
    uint64_t buffer = 0;
    uint32_t bp = 0;
    // for parameter verification in decompress()
    write_bits((uint64_t)window_bits, 8);
    size_t i = 0;
    while (i < bytes) {
        size_t len = 0; // match length and position
        size_t pos = 0;
        if (i >= 1) {
            size_t j = i - 1;
            size_t min_j = i > window ? i - window : 0;
            while (j > min_j) {
                rt_assert((i - j) < window);
                const size_t n = bytes - i;
                size_t k = 0;
                while (k < n && data[j + k] == data[i + k]) {
                    k++;
                }
                if (k > len) {
                    len = k;
                    pos = i - j;
                }
                j--;
            }
        }
        if (len > 2) {
            write_bits(0b11, 2); /* flags */
            lzn_histogram_pos_len(pos, len);
            write_pos_len(pos, len, window_bits, chunk);
//          rt_println("[%5lld] pos: %5lld len: %4lld bytes: %lld",
//                      i, pos, len, lzn->bytes_written);
            i += len;
        } else {
            const uint8_t b = data[i];
            // European texts are predominantly spaces and small ASCII letters:
            if (b < 0x80) {
                write_bit(0); /* flags */
                write_bits(b, 7); // ASCII byte < 0x80 with 8th but set to `0`
//              rt_println("[%05lld] char: %c 0x%02X %d", i, b, b, b);
            } else {
                write_bits(0b10, 2); /* flags */
                write_bits(b, 7); // only 7 bit because 8th bit is `1`
            }
            i++;
        }
    }
    if (bp > 0) {
        lzn->write(lzn, buffer);
    }
    lzn_dump_histograms();
}

static inline uint64_t lzn_read_bit(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp) {
    if (*bp == 0) {
        *buffer = lzn->read(lzn);
        lzn->bytes_read += 8;
    }
    uint64_t bit = (*buffer >> *bp) & 1;
    *bp = *bp == 63 ? 0 : *bp + 1;
    return bit;
}

static inline uint64_t lzn_read_bits(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp, uint32_t n) {
    rt_assert(n <= 64);
    uint64_t bits = 0;
    for (uint32_t i = 0; i < n && lzn->error == 0; i++) {
        uint64_t bit = lzn_read_bit(lzn, buffer, bp);
        bits |= bit << i;
    }
    return bits;
}

static inline uint64_t lzn_read_in_chunks(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp, uint8_t chunk) {
    uint64_t bits = 0;
    uint64_t bit = 0;
    uint32_t shift = 0;
    do {
        bits |= (lzn_read_bits(lzn, buffer, bp, chunk) << shift);
        shift += chunk;
        bit = lzn_read_bit(lzn, buffer, bp);
    } while (bit && lzn->error == 0);
    return bits;
}

#define read_bit(bit) do {                                  \
    bit = lzn_read_bit(lzn, &buffer, &bp);                  \
    if (lzn->error) { return; }                             \
} while (0)

#define read_bits(bits, n) do {                             \
    bits = lzn_read_bits(lzn, &buffer, &bp, n);             \
    if (lzn->error) { return; }                             \
} while (0)

#define read_chunked(bits, chunk) do {                      \
    bits = lzn_read_in_chunks(lzn, &buffer, &bp, chunk);    \
    if (lzn->error) { return; }                             \
} while (0)

#define read_pos_len(pos, len, window, chunk) do {          \
    read_chunked(pos, chunk);                               \
    read_chunked(len, chunk);                               \
} while (0)

static void lzn_read_header(lzn_t* lzn, size_t *bytes, uint8_t *window_bits) {
    if (lzn->error != 0) { return; }
    *bytes = (size_t)lzn->read(lzn);
    *window_bits = (uint8_t)lzn->read(lzn);
    if (*window_bits < 10 || *window_bits > 20) { return_invalid; }
}

static void lzn_decompress(lzn_t* lzn, uint8_t* data, size_t bytes,
        uint8_t window_bits) {
    if (lzn->error != 0) { return; }
    uint64_t buffer = 0;
    uint32_t bp = 0;
    uint64_t data_size = 0; // original source data size
    uint64_t verify_window_bits;
    read_bits(verify_window_bits, 8);
    if (window_bits != verify_window_bits) { return_invalid; }
    if (window_bits < 10 || window_bits > 20) { return_invalid; }
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t chunk = (window_bits - 4) / 2;
    size_t i = 0; // output data[i]
    while (i < bytes) {
        uint64_t bit = 0;
        read_bit(bit);
        if (bit) {
            bit = 0;
            read_bit(bit);
            if (bit) {
                uint64_t pos = 0;
                uint64_t len = 0;
                read_pos_len(pos, len, window, chunk);
                rt_assert(0 < pos && pos < window);
                if (!(0 < pos && pos < window)) { return_invalid; }
                rt_assert(0 < len);
                if (len == 0) { return_invalid; }
                // Cannot do memcpy() here because of possible overlap.
                // memcpy() may read more than one byte at a time.
                uint8_t* s = data - (size_t)pos;
                const size_t n = i + (size_t)len;
                while (i < n) { data[i] = s[i]; i++; }
            } else {
                size_t b = 0; // byte >= 0x80
                read_bits(b, 7);
                data[i] = (uint8_t)b | 0x80;
                i++;
            }
        } else { // literal byte
            size_t b = 0; // ASCII byte < 0x80
            read_bits(b, 7);
//          rt_println("[%05lld] char: %c 0x%02X %d", i, b, b, b);
            data[i] = (uint8_t)b;
            i++;
        }
    }
}

lzn_if lzn = {
    .write_header = lzn_write_header,
    .compress     = lzn_compress,
    .read_header  = lzn_read_header,
    .decompress   = lzn_decompress,
};

#pragma pop_macro("return_invalid")

#pragma pop_macro("write_pos_len")
#pragma pop_macro("write_chunked")
#pragma pop_macro("write_bits")
#pragma pop_macro("write_bit")

#pragma pop_macro("read_pos_len")
#pragma pop_macro("read_chunked")
#pragma pop_macro("read_bits")
#pragma pop_macro("read_bit")

#endif lzn_implementation

