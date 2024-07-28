#ifndef lz77_definition
#define lz77_definition

#include <errno.h>
#include <stdint.h>

// Naive LZ77 implementation inspired by CharGPT discussion
// and my personal passion to compressors in 198x

typedef struct lz77_s lz77_t;

typedef struct lz77_s {
    void*    that;  // caller supplied data
    errno_t  error; // sticky; for read()/write() compress() and decompress()
    // caller supplied read()/write() must error via .error field
    uint64_t (*read)(lz77_t*); //  reads 64 bits
    void     (*write)(lz77_t*, uint64_t b64); // writes 64 bits
    uint64_t written;
} lz77_t;

typedef struct lz77_if {
    // `window_bits` is a log2 of window size in bytes must be in range [10..20]
    void (*write_header)(lz77_t* lz77, size_t bytes, uint8_t window_bits);
    void (*compress)(lz77_t* lz77, const uint8_t* data, size_t bytes,
                     uint8_t window_bits);
    void (*read_header)(lz77_t* lz77, size_t *bytes, uint8_t *window_bits);
    void (*decompress)(lz77_t* lz77, uint8_t* data, size_t bytes,
                       uint8_t window_bits);
    // Writing and reading envelope of source data `bytes` and
    // `window_bits` is caller's responsibility.
} lz77_if;

extern lz77_if lz77;

#endif // lz77_definition

#if defined(lz77_implementation) && !defined(lz77_implemented)

#define lz77_implemented

#ifndef lz77_assert
#define lz77_assert(...) do {] while (0)
#endif
#ifndef lz77_swear
#define lz77_swear(...) do {] while (0)
#endif
#ifndef lz77_println
#define lz77_println(...) do {] while (0)
#endif

#ifdef lz77_historgram

static inline uint32_t lz77_bit_count(size_t v) {
    uint32_t count = 0;
    while (v) { count++; v >>= 1; }
    return count;
}

static size_t lz77_hist_len[64];
static size_t lz77_hist_pos[64];

#define lz77_init_histograms() do {                             \
    memset(lz77_hist_pos, 0x00, sizeof(lz77_hist_pos));         \
    memset(lz77_hist_len, 0x00, sizeof(lz77_hist_len));         \
} while (0)

#define lz77_histogram_pos_len(pos, len) do {                   \
    lz77_hist_pos[lz77_bit_count(pos)]++;                       \
    lz77_hist_len[lz77_bit_count(len)]++;                       \
} while (0)

#define lz77_dump_histograms() do {                             \
    rt_println("Histogram log2(len):");                         \
    for (int8_t i = 0; i < 64; i++) {                           \
        if (lz77_hist_len[i] > 0) {                             \
            rt_println("len[%d]: %lld", i, lz77_hist_len[i]);   \
        }                                                       \
    }                                                           \
    rt_println("Histogram log2(pos):");                         \
    for (int8_t i = 0; i < 64; i++) {                           \
        if (lz77_hist_pos[i] > 0) {                             \
            rt_println("pos[%d]: %lld", i, lz77_hist_pos[i]);   \
        }                                                       \
    }                                                           \
} while (0)

#else

#define lz77_init_histograms() do { } while (0)
#define lz77_histogram_pos_len(pos, len) do { } while (0)
#define lz77_dump_histograms() do { } while (0)

#endif

static inline void lz77_write_bit(lz77_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bit) {
    if (*bp == 64) {
        lz->write(lz, *b64);
        *b64 = 0;
        *bp = 0;
        lz->written += 8;
    }
    *b64 |= bit << *bp;
    (*bp)++;
}

static inline void lz77_write_bits(lz77_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bits, uint32_t n) {
    rt_assert(n <= 64);
    while (n > 0) {
        lz77_write_bit(lz, b64, bp, bits & 1);
        bits >>= 1;
        n--;
    }
}

static inline void lz77_write_number(lz77_t* lz, uint64_t* b64,
        uint32_t* bp, uint64_t bits, uint8_t base) {
    do {
        lz77_write_bits(lz, b64, bp, bits, base);
        bits >>= base;
        lz77_write_bit(lz, b64, bp, bits != 0); // stop bit
    } while (bits != 0);
}

#pragma push_macro("write_bit")
#pragma push_macro("write_bits")
#pragma push_macro("write_number")

#pragma push_macro("read_bit")
#pragma push_macro("read_bits")
#pragma push_macro("read_number")

#pragma push_macro("return_invalid")
#pragma push_macro("lz77_if_error_return")

#define lz77_if_error_return(lz);do {                   \
    if (lz->error) { return; }                          \
} while (0)


#define return_invalid(lz) do {                         \
    lz->error = EINVAL;                                 \
    return;                                             \
} while (0)

#define write_bit(lz, bit) do {                         \
    lz77_write_bit(lz, &b64, &bp, bit);                 \
    lz77_if_error_return(lz);                           \
} while (0)

#define write_bits(lz, bits, n) do {                    \
    lz77_write_bits(lz, &b64, &bp, bits, n);            \
    lz77_if_error_return(lz);                           \
} while (0)

#define write_number(lz, bits, base) do {               \
    lz77_write_number(lz, &b64, &bp, bits, base);       \
    lz77_if_error_return(lz);                           \
} while (0)

static void lz77_write_header(lz77_t* lz, size_t bytes, uint8_t window_bits) {
    lz77_if_error_return(lz);
    if (window_bits < 10 || window_bits > 20) { return_invalid(lz); }
    lz->write(lz, (uint64_t)bytes);
    lz77_if_error_return(lz);
    lz->write(lz, (uint64_t)window_bits);
}

static void lz77_compress(lz77_t* lz, const uint8_t* data, size_t bytes,
        uint8_t window_bits) {
    lz77_if_error_return(lz);
    if (window_bits < 10 || window_bits > 20) { return_invalid(lz); }
    lz77_init_histograms();
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t base = (window_bits - 4) / 2;
    rt_println("base: %d", base);
    uint64_t b64 = 0;
    uint32_t bp = 0;
    // for parameter verification in decompress()
    write_bits(lz, (uint64_t)window_bits, 8);
    size_t i = 0;
    while (i < bytes) {
        // length and position of longest matching sequence
        size_t len = 0;
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
            rt_assert(0 < pos && pos < window);
            rt_assert(0 < len);
            write_bits(lz, 0b11, 2); /* flags */
            write_number(lz, pos, base);
            write_number(lz, len, base);
//          rt_println("[%5lld] pos: %5lld len: %4lld bytes: %lld",
//                      i, pos, len, lz->bytes_written);
            lz77_histogram_pos_len(pos, len);
            i += len;
        } else {
            const uint8_t b = data[i];
            // European texts are predominantly spaces and small ASCII letters:
            if (b < 0x80) {
                write_bit(lz, 0); /* flags */
                write_bits(lz, b, 7); // ASCII byte < 0x80 with 8th but set to `0`
//              rt_println("[%05lld] char: %c 0x%02X %d", i, b, b, b);
            } else {
                write_bits(lz, 0b10, 2); /* flags */
                write_bits(lz, b, 7); // only 7 bit because 8th bit is `1`
            }
            i++;
        }
    }
    if (bp > 0) {
        lz->write(lz, b64);
        lz->written += 8;
    }
    lz77_dump_histograms();
}

static inline uint64_t lz77_read_bit(lz77_t* lz, uint64_t* b64, uint32_t* bp) {
    if (*bp == 0) { *b64 = lz->read(lz); }
    uint64_t bit = (*b64 >> *bp) & 1;
    *bp = *bp == 63 ? 0 : *bp + 1;
    return bit;
}

static inline uint64_t lz77_read_bits(lz77_t* lz, uint64_t* b64,
        uint32_t* bp, uint32_t n) {
    rt_assert(n <= 64);
    uint64_t bits = 0;
    for (uint32_t i = 0; i < n && lz->error == 0; i++) {
        uint64_t bit = lz77_read_bit(lz, b64, bp);
        bits |= bit << i;
    }
    return bits;
}

static inline uint64_t lz77_read_number(lz77_t* lz, uint64_t* b64,
        uint32_t* bp, uint8_t base) {
    uint64_t bits = 0;
    uint64_t bit = 0;
    uint32_t shift = 0;
    do {
        bits |= (lz77_read_bits(lz, b64, bp, base) << shift);
        shift += base;
        bit = lz77_read_bit(lz, b64, bp);
    } while (bit && lz->error == 0);
    return bits;
}

#define read_bit(lz, bit) do {                      \
    bit = lz77_read_bit(lz, &b64, &bp);             \
    lz77_if_error_return(lz);                       \
} while (0)

#define read_bits(lz, bits, n) do {                 \
    bits = lz77_read_bits(lz, &b64, &bp, n);        \
    lz77_if_error_return(lz);                       \
} while (0)

#define read_number(lz, bits, base) do {            \
    bits = lz77_read_number(lz, &b64, &bp, base);   \
    lz77_if_error_return(lz);                       \
} while (0)

static void lz77_read_header(lz77_t* lz, size_t *bytes, uint8_t *window_bits) {
    lz77_if_error_return(lz);
    *bytes = (size_t)lz->read(lz);
    *window_bits = (uint8_t)lz->read(lz);
    if (*window_bits < 10 || *window_bits > 20) { return_invalid(lz); }
}

static void lz77_decompress(lz77_t* lz, uint8_t* data, size_t bytes,
        uint8_t window_bits) {
    lz77_if_error_return(lz);
    uint64_t b64 = 0;
    uint32_t bp = 0;
    uint64_t verify_window_bits;
    read_bits(lz, verify_window_bits, 8);
    if (window_bits != verify_window_bits) { return_invalid(lz); }
    if (window_bits < 10 || window_bits > 20) { return_invalid(lz); }
    const size_t window = ((size_t)1U) << window_bits;
    const uint8_t base = (window_bits - 4) / 2;
    size_t i = 0; // output data[i]
    while (i < bytes) {
        uint64_t bit0 = 0;
        read_bit(lz, bit0);
        if (bit0) {
            uint64_t bit1 = 0;
            read_bit(lz, bit1);
            if (bit1) {
                uint64_t pos = 0;
                read_number(lz, pos, base);
                uint64_t len = 0;
                read_number(lz, len, base);
                rt_assert(0 < pos && pos < window);
                if (!(0 < pos && pos < window)) { return_invalid(lz); }
                rt_assert(0 < len);
                if (len == 0) { return_invalid(lz); }
                // Cannot do memcpy() here because of possible overlap.
                // memcpy() may read more than one byte at a time.
                uint8_t* s = data - (size_t)pos;
                const size_t n = i + (size_t)len;
                while (i < n) { data[i] = s[i]; i++; }
            } else {
                size_t b = 0; // byte >= 0x80
                read_bits(lz, b, 7);
                data[i] = (uint8_t)b | 0x80;
                i++;
            }
        } else { // literal byte
            size_t b = 0; // ASCII byte < 0x80
            read_bits(lz, b, 7);
//          rt_println("[%05lld] char: %c 0x%02X %d", i, b, b, b);
            data[i] = (uint8_t)b;
            i++;
        }
    }
}

lz77_if lz77 = {
    .write_header = lz77_write_header,
    .compress     = lz77_compress,
    .read_header  = lz77_read_header,
    .decompress   = lz77_decompress,
};

#pragma pop_macro("lz77_if_error_return")
#pragma pop_macro("return_invalid")

#pragma pop_macro("write_number")
#pragma pop_macro("write_bits")
#pragma pop_macro("write_bit")

#pragma pop_macro("read_number")
#pragma pop_macro("read_bits")
#pragma pop_macro("read_bit")

#endif lz77_implementation

