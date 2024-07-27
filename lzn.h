#pragma once
#ifndef lzn_h
#define lzn_h

// Naive LZ77 implementation inspired by CharGPT discussion
// and my personal passion to compressors in 198x

#include "rt.h"

enum { lzn_chunking = 4 };

typedef struct lzn_stream_s lzn_stream_t;
typedef struct lzn_s lzn_t;

typedef struct lzn_stream_s {
    void* that;
    errno_t error; // sticky
    uint64_t (*read)(lzn_t*);
    void     (*write)(lzn_t*, uint64_t  data);
    uint64_t bytes_written;
    uint64_t bytes_read;
} lzn_stream_t;

typedef struct lzn_s {
    lzn_stream_t* stream;
    uint8_t window_bits; // default: 11  range [10..20] log2(window)
} lzn_t;

typedef struct lzn_if {
    void (*compress)(lzn_t* lzn, const uint8_t* data, size_t bytes);
    void (*decompress)(lzn_t* lzn, uint8_t* data, size_t bytes);
} lzn_if;

extern lzn_if lzn;

static inline void lzn_write_bit(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp, uint64_t bit) {
    errno_t r = 0;
    if (*bp == 64) {
        lzn->stream->write(lzn, *buffer);
        *buffer = 0;
        *bp = 0;
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

static inline void lzn_write_chunked(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp, uint64_t bits) {
    do {
        lzn_write_bits(lzn, buffer, bp, bits, lzn_chunking);
        bits >>= lzn_chunking;
        lzn_write_bit(lzn, buffer, bp, bits != 0); // stop bit
    } while (bits != 0);
}

static inline uint32_t lzn_bit_count(size_t v) {
    uint32_t count = 0;
    while (v) { count++; v >>= 1; }
    return count;
}

static void lzn_compress(lzn_t* lzn, const uint8_t* data, size_t bytes) {
    #pragma push_macro("write_bit")
    #pragma push_macro("write_bits")
    #pragma push_macro("write_chunked")
    #pragma push_macro("write_pos_len")

    #define write_bit(bit) do {                         \
        lzn_write_bit(lzn, &buffer, &bp, bit);          \
        if (lzn->stream->error) { return; }             \
    } while (0)

    #define write_bits(bits, n) do {                    \
        lzn_write_bits(lzn, &buffer, &bp, bits, n);     \
        if (lzn->stream->error) { return; }             \
    } while (0)

    #define write_chunked(bits) do {                    \
        lzn_write_chunked(lzn, &buffer, &bp, bits);     \
        if (lzn->stream->error) { return; }             \
    } while (0)

    #define write_pos_len(pos, len) do {                \
        rt_assert(0 < pos && pos < window);             \
        rt_assert(0 < len);                             \
        write_bit(1); /* flag */                        \
        write_chunked(pos);                             \
        write_chunked(len);                             \
    } while (0)

    lzn->stream->error = 0;
    lzn->stream->bytes_written = 0;
    lzn->stream->bytes_read = 0;
    if (lzn->window_bits == 0) { lzn->window_bits = 11; };
    const uint32_t window_bits = lzn->window_bits;
    rt_swear(10 <= window_bits && window_bits <= 20);
    const size_t window = ((size_t)1U) << window_bits;
    const uint32_t pos_bc = lzn_bit_count(window - 1);
    uint64_t buffer = 0;
    uint32_t bp = 0;
    write_bits((uint64_t)bytes, 64);
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
            write_pos_len(pos, len);
            i += len;
        } else {
            write_bit(0); // flag bit
            write_bits(data[i], 8);
            i++;
        }
    }
    if (bp > 0) {
        lzn->stream->write(lzn, buffer);
    }
    #pragma pop_macro("write_pos_len")
    #pragma pop_macro("write_chunked")
    #pragma pop_macro("write_bits")
    #pragma pop_macro("write_bit")
}

static inline uint64_t lzn_read_bit(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp) {
    if (*bp == 0) {
        *buffer = lzn->stream->read(lzn);
    }
    uint64_t bit = (*buffer >> *bp) & 1;
    *bp = *bp == 63 ? 0 : *bp + 1;
    return bit;
}

static inline uint64_t lzn_read_bits(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp, uint32_t n) {
    rt_assert(n <= 64);
    uint64_t bits = 0;
    for (uint32_t i = 0; i < n && lzn->stream->error == 0; i++) {
        uint64_t bit = lzn_read_bit(lzn, buffer, bp);
        bits |= bit << i;
    }
    return bits;
}

static inline uint64_t lzn_read_chunked(lzn_t* lzn, uint64_t* buffer,
        uint32_t* bp) {
    uint64_t bits = 0;
    uint64_t bit = 0;
    uint32_t shift = 0;
    do {
        bits |= (lzn_read_bits(lzn, buffer, bp, lzn_chunking) << shift);
        shift += lzn_chunking;
        bit = lzn_read_bit(lzn, buffer, bp);
    } while (bit && lzn->stream->error == 0);
    return bits;
}

static void lzn_decompress(lzn_t* lzn, uint8_t* data, size_t bytes) {
    #pragma push_macro("read_bit")
    #pragma push_macro("read_bits")
    #pragma push_macro("read_chunked")
    #pragma push_macro("read_pos_len")

    #define read_bit(bit) do {                       \
        bit = lzn_read_bit(lzn, &buffer, &bp);       \
        if (lzn->stream->error) { return; }          \
    } while (0)

    #define read_bits(bits, n) do {                 \
        bits = lzn_read_bits(lzn, &buffer, &bp, n); \
        if (lzn->stream->error) { return; }         \
    } while (0)

    #define read_chunked(bits) do {                 \
        bits = lzn_read_chunked(lzn, &buffer, &bp); \
        if (lzn->stream->error) { return; }         \
    } while (0)

    #define read_pos_len(pos, len) do {             \
        read_chunked(pos);                          \
        rt_assert(0 < pos && pos < window);         \
        read_chunked(len);                          \
        rt_assert(0 < len);                         \
    } while (0)

    lzn->stream->error = 0;
    lzn->stream->bytes_written = 0;
    lzn->stream->bytes_read = 0;
    uint64_t buffer = 0;
    uint32_t bp = 0;
    uint64_t data_size = 0; // original source data size
    read_bits(data_size, 64);
    uint64_t window_bits;
    read_bits(window_bits, 8);
    lzn->window_bits = (uint32_t)window_bits;
    rt_assert(10 <= window_bits && window_bits <= 20);
    if (!(10 <= window_bits && window_bits <= 20)) {
        lzn->stream->error = EINVAL;
        return;
    }
    const size_t window = ((size_t)1U) << (uint32_t)window_bits;
    const uint32_t pos_bc = lzn_bit_count(window - 1);
    size_t i = 0; // output data[i]
    while (i < bytes) {
        uint64_t bit;
        read_bit(bit);
        if (bit) {
            uint64_t pos = 0;
            uint64_t len = 0;
            read_pos_len(pos, len);
            // Cannot do memcpy() here because of possible overlap.
            // memcpy() may read more than one byte at a time.
            uint8_t* s = data - (size_t)pos;
            const size_t n = i + (size_t)len;
            while (i < n) { data[i] = s[i]; i++; }
        } else { // literal byte
            size_t b; // byte
            read_bits(b, 8);
            data[i] = (uint8_t)b;
            i++;
        }
    }
    #pragma pop_macro("read_pos_len")
    #pragma pop_macro("read_chunked")
    #pragma pop_macro("read_bits")
    #pragma pop_macro("read_bit")
}

lzn_if lzn = {
    .compress   = lzn_compress,
    .decompress = lzn_decompress,
};

#endif // lzn_h
