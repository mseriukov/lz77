#pragma once
#ifndef lzn_H
#define lzn_H

// Naive LZ77 implementation inspired by CharGPT discussion
// and my personal passion to compressors in 198x

#include "rt.h"

typedef struct lzn_stream_s lzn_stream_t;
typedef struct lzn_s lzn_t;

typedef struct lzn_stream_s {
    void* that;
    errno_t (*read)(lzn_t*);  // reads 64 bits into lz->buffer
    errno_t (*write)(lzn_t*); // writes 64 bits into lz->buffer
} lzn_stream_t;

typedef struct lzn_s {
    lzn_stream_t* stream;
    uint64_t buffer;
    uint32_t bp; // bit position 0..63
    // compression parameters;
    uint8_t min_match; // default:  3  range [3.. 5]
    uint8_t window;    // default: 14  range [8..20] log2(window)
    uint8_t lookahead; // default:  4  range [4.. 8] log2(lookahead)
    // stats and histograms
    bool stats;
    size_t compressed_bytes;
    size_t compressed_count;
    size_t compressed_bits;
    size_t uncompressed_bits;
    size_t match_len_histogram[32];
    size_t match_pos_histogram[32];
} lzn_t;

typedef struct lzn_if {
    errno_t (*compress)(lzn_t* lzn, const uint8_t* data, size_t bytes);
    errno_t (*decompress_and_compare)(lzn_t* lzn, uint8_t* data, size_t bytes);
} lzn_if;

extern lzn_if lzn;

static inline errno_t lzn_write_bit(lzn_t* lzn, bool bit) {
    errno_t r = 0;
    if (lzn->bp == 64) {
        r = lzn->stream->write(lzn);
        lzn->buffer = 0;
        lzn->bp = 0;
        lzn->compressed_bytes += 8;
        if (lzn->compressed_bytes % (256 * 1024) == 0) {
            rt_println("%ld", lzn->compressed_bytes);
        }
    }
    lzn->buffer |= (uint64_t)bit << lzn->bp;
    lzn->bp++;
    return r;
}

static inline errno_t lzn_write_bits(lzn_t* lzn, uint64_t bits, uint8_t n) {
    errno_t r = 0;
    rt_assert(n <= 64);
    for (int i = 0; i < n && r == 0; i++) {
        r = lzn_write_bit(lzn, bits & 1);
        bits >>= 1;
    }
    return r;
}

static int lzn_bit_count(size_t v) {
    int count = 0;
    while (v) {
        count++;
        v >>= 1;
    }
    return count;
}

static errno_t lzn_compress(lzn_t* lzn, const uint8_t* data, size_t bytes) {
    if (lzn->min_match      == 0) { lzn->min_match      =  3; }
    if (lzn->window    == 0) { lzn->window    = 14; }
    if (lzn->lookahead == 0) { lzn->lookahead =  4; }
    // acceptable ranges:
    rt_assert(3 <= lzn->min_match && lzn->min_match <= 5);
    rt_assert(8 <= lzn->window && lzn->window <= 20);
    rt_assert(4 <= lzn->lookahead && lzn->lookahead <= 8);
    lzn->buffer = 0;
    lzn->bp = 0;
    // Write the length of source data so decompressor knows how much memory
    // to allocate for decompression
    errno_t r = lzn_write_bits(lzn, bytes, sizeof(bytes) * 8);
    if (r == 0) {
        r = lzn_write_bits(lzn, lzn->min_match, sizeof(lzn->min_match) * 8);
    }
    if (r == 0) {
        r = lzn_write_bits(lzn, lzn->window, sizeof(lzn->window) * 8);
    }
    if (r == 0) {
        r = lzn_write_bits(lzn, lzn->lookahead, sizeof(lzn->lookahead) * 8);
    }
    lzn->compressed_count = 0;
    lzn->compressed_bits = 0;
    lzn->uncompressed_bits = 0;
    memset(lzn->match_len_histogram, 0x00, sizeof(lzn->match_len_histogram));
    memset(lzn->match_pos_histogram, 0x00, sizeof(lzn->match_pos_histogram));
    size_t max_match_ofs = 0;
    size_t max_match_len = 0;
    lzn->compressed_bytes = 0;
    if (r == 0) {
        const size_t window = (1ULL << lzn->window);
        const size_t lzn_lookahead = (1U << lzn->lookahead) + lzn->min_match - 1;
        size_t i = 0;
        while (i < bytes && r == 0) {
            size_t match_len = 0;
            size_t match_pos = 0;
            if (i >= lzn->min_match) {
                size_t j = i - lzn->min_match;
                for (;;) {
                    uint32_t k = 0;
                    while (k < lzn_lookahead &&
                           i + k < bytes &&
                           data[j + k] == data[i + k]) {
                        k++;
                    }
                    if (k > match_len) {
                        match_len = k;
                        match_pos = i - j;
                    }
                    if (j == 0 || (i - j) >= window - 1) { break; }
                    j--;
                }
            }
            if (match_len >= lzn->min_match) {
                max_match_ofs = match_pos > max_match_ofs ?
                                match_pos : max_match_ofs;
                max_match_len = match_len > max_match_len ?
                                match_len : max_match_len;
                // TODO: Golumb Rice encode offset and len
                r = lzn_write_bit(lzn, 1); // flag bit
                if (r == 0) {
                    rt_assert(match_pos < (1ULL << lzn->window));
                    r = lzn_write_bits(lzn, match_pos, lzn->window);
                }
                if (r == 0) {
                    rt_assert(match_len - lzn->min_match < (1ULL << lzn->lookahead));
                    r = lzn_write_bits(lzn, match_len - lzn->min_match, lzn->lookahead);
                }
                if (r == 0) {
                    i += match_len;
                    if (lzn->stats) {
                        int bc = lzn_bit_count(match_len - lzn->min_match);
                        rt_assert(0 <= bc && bc < rt_countof(lzn->match_len_histogram));
                        lzn->match_len_histogram[bc]++;
                        bc = lzn_bit_count(match_pos);
                        rt_assert(0 <= bc && bc < rt_countof(lzn->match_pos_histogram));
                        lzn->match_pos_histogram[bc]++;
                        lzn->compressed_bits += 19;
                        lzn->compressed_count++;
                    }
                }
            } else { // Emit as 9-bit character
                r = lzn_write_bit(lzn, 0); // flag bit
                if (r == 0) {
                    r = lzn_write_bits(lzn, data[i], 8);
                }
                if (r == 0) {
                    i++;
                    lzn->uncompressed_bits += 9;
                }
            }
        }
        if (r == 0 && lzn->bp > 0) {
            r = lzn->stream->write(lzn);
        }
        if (r == 0 && lzn->stats) {
            rt_println("%ld:%ld %.1f%%", lzn->compressed_bytes, bytes,
                       lzn->compressed_bytes * 100.0 / bytes);
            rt_println("%.3f", (double)lzn->compressed_bits /
                                       lzn->uncompressed_bits);
            rt_println("max_match_ofs: %d max_match_len: %d",
                        max_match_ofs, max_match_len);
            rt_println("log2(length):");
            double sum = 0;
            for (int32_t k = 0; k < lzn->lookahead; k++) {
                double percent = (double)lzn->match_len_histogram[k] * 100.0 /
                                         lzn->compressed_count;
                sum += percent;
                rt_println("[%2d] %10.6f %10.6f %lld", k, percent, sum, lzn->match_len_histogram[k]);
            }
            rt_println("log2(positions)");
            sum = 0;
            const int m = rt_countof(lzn->match_pos_histogram);
            for (int32_t k = lzn->window; k >= 0; k--) {
                double percent = (double)lzn->match_pos_histogram[k] * 100.0 /
                                         lzn->compressed_count;
                sum += percent;
                rt_println("[%2d] %10.6f %10.6f %lld", k, percent, sum, lzn->match_pos_histogram[k]);
            }
        }
    }
    return r;
}

static inline errno_t lzn_read_bit(lzn_t* lzn, bool* bit) {
    errno_t r = 0;
    if (lzn->bp == 0) {
        r = lzn->stream->read(lzn);
    }
    *bit = (lzn->buffer >> lzn->bp) & 1;
    lzn->bp++;
    if (lzn->bp == 64) { lzn->bp = 0; }
    return r;
}

static inline errno_t lzn_read_bits(lzn_t* lzn, size_t* data, uint8_t n) {
    errno_t r = 0;
    rt_assert(n <= sizeof(*data) * 8);
    if (n <= 64) {
        uint64_t bits = 0;
        for (uint32_t i = 0; i < n && r == 0; i++) {
            bool bit;
            r = lzn_read_bit(lzn, &bit) << i;
            bits |= (((uint64_t)bit) << i);
        }
        *data = bits;
    } else {
        r = EINVAL;
    }
    return r;
}

static inline errno_t lzn_read_byte(lzn_t* lzn, uint8_t* data) {
    size_t v = 0;
    errno_t r = lzn_read_bits(lzn, &v, 8);
    *data = (uint8_t)v;
    return r;
}

static errno_t lzn_decompress(lzn_t* lzn, uint8_t* data, size_t bytes) {
    lzn->buffer = 0;
    lzn->bp = 0;
    size_t data_size = 0; // original source data size
    errno_t r = lzn_read_bits(lzn, &data_size, 64);
    if (r == 0 && data_size != bytes) {
        rt_println("Data size mismatch %ld != %ld", data_size, bytes);
        r = EINVAL;
    }
    if (r == 0) { r = lzn_read_byte(lzn, &lzn->min_match); }
    if (r == 0) { r = lzn_read_byte(lzn, &lzn->window); }
    if (r == 0) { r = lzn_read_byte(lzn, &lzn->lookahead); }
    size_t index = 0; // output data[index]
    while (index < bytes && r == 0) {
        bool bit;
        r = lzn_read_bit(lzn, &bit);
        if (r != 0) {
            rt_println("Failed to read flag bit");
            break;
        }
        if (bit) { // It's data compressed sequence
            size_t offset = 0;
            size_t length = 0;
            r = lzn_read_bits(lzn, &offset, lzn->window);
            if (r != 0) {
                rt_println("Failed to read offset");
            } else {
                r = lzn_read_bits(lzn, &length, lzn->lookahead);
                if (r != 0) {
                    rt_println("Failed to read length");
                }
                length += lzn->min_match;
            }
            if (r == 0) { // TODO: memcpy
                for (uint32_t i = 0; i < length; i++) {
                    data[index] = data[index - offset];
                    index++;
                }
            }
        } else { // literal byte
            size_t b; // byte
            r = lzn_read_bits(lzn, &b, 8);
            if (r == 0) {
                data[index++] = (uint8_t)b;
            } else {
                rt_println("Failed to read byte");
            }
        }
    }
    return r;
}

lzn_if lzn = {
    .compress   = lzn_compress,
    .decompress_and_compare = lzn_decompress,
};

#endif // lzn_H
