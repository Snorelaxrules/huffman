#include "archive.h"

#include "huffman.h"
#include "lz77.h"

#include <errno.h>

static void write_be(FILE *file, uint64_t value, int nbytes) {
    for (int i = nbytes - 1; i >= 0; i--) {
        fputc((int)((value >> (i * 8)) & 0xff), file);
    }
}

static bool read_be(FILE *file, uint64_t *a_value, int nbytes) {
    uint64_t value = 0;
    for (int i = 0; i < nbytes; i++) {
        int c = fgetc(file);
        if (c == EOF) return false;
        value = (value << 8) | (uint64_t)c;
    }
    *a_value = value;
    return true;
}

// Reads a whole file into a fresh buffer, reporting its size in `*a_len`.
static uint8_t *read_file(const char *path, uint64_t *a_len, const char **a_error) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        *a_error = strerror(errno);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size < 0) {
        *a_error = strerror(errno);
        fclose(file);
        return NULL;
    }
    rewind(file);

    // malloc(0) may return NULL, which would be indistinguishable from failure.
    uint8_t *bytes = malloc((size_t)size + 1);
    if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        *a_error = bytes == NULL ? "out of memory" : "input file changed while reading";
        free(bytes);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *a_len = (uint64_t)size;
    return bytes;
}

// Tallies how often each literal/length and distance symbol is used, which is
// what the two Huffman codes are built from.
static void count_symbols(const Token *tokens, size_t ntokens, uint64_t *litlen_freqs,
                          uint64_t *distance_freqs) {
    for (size_t i = 0; i < ntokens; i++) {
        if (tokens[i].length == 0) {
            litlen_freqs[tokens[i].distance]++;
            continue;
        }
        litlen_freqs[LZ_FIRST_LENGTH_SYMBOL + lz77_length_index(tokens[i].length)]++;
        distance_freqs[lz77_distance_index(tokens[i].distance)]++;
    }
}

static void write_tokens(BitWriter *a_writer, const Token *tokens, size_t ntokens,
                         const CodeTable *litlen, const CodeTable *distance) {
    for (size_t i = 0; i < ntokens; i++) {
        if (tokens[i].length == 0) {
            write_symbol(a_writer, litlen, tokens[i].distance);
            continue;
        }

        // A length or distance code names a range, so the exact value is the
        // code's base plus a few literal bits that follow it.
        int li = lz77_length_index(tokens[i].length);
        write_symbol(a_writer, litlen, LZ_FIRST_LENGTH_SYMBOL + li);
        write_bits(a_writer, (uint64_t)(tokens[i].length - lz_length_base[li]),
                   lz_length_extra[li]);

        int di = lz77_distance_index(tokens[i].distance);
        write_symbol(a_writer, distance, di);
        write_bits(a_writer, (uint64_t)(tokens[i].distance - lz_distance_base[di]),
                   lz_distance_extra[di]);
    }
}

bool huffman_compress(const char *in_path, const char *out_path, const char **a_error) {
    uint64_t length = 0;
    uint8_t *bytes = read_file(in_path, &length, a_error);
    if (bytes == NULL) return false;

    Token *tokens = NULL;
    size_t ntokens = 0;
    if (length > 0) {
        tokens = malloc((size_t)length * sizeof(Token));
        if (tokens == NULL) {
            *a_error = "out of memory";
            free(bytes);
            return false;
        }
        bool failed = false;
        ntokens = lz77_tokenize(bytes, (size_t)length, tokens, &failed);
        if (failed) {
            *a_error = "out of memory";
            free(tokens);
            free(bytes);
            return false;
        }
    }
    free(bytes);

    uint64_t litlen_freqs[LZ_LITLEN_SYMBOLS] = {0};
    uint64_t distance_freqs[LZ_DIST_SYMBOLS] = {0};
    count_symbols(tokens, ntokens, litlen_freqs, distance_freqs);

    uint8_t litlen_lengths[LZ_LITLEN_SYMBOLS];
    uint8_t distance_lengths[LZ_DIST_SYMBOLS];
    build_code_lengths(litlen_freqs, LZ_LITLEN_SYMBOLS, litlen_lengths);
    build_code_lengths(distance_freqs, LZ_DIST_SYMBOLS, distance_lengths);

    CodeTable litlen, distance;
    build_canonical_codes(&litlen, litlen_lengths, LZ_LITLEN_SYMBOLS);
    build_canonical_codes(&distance, distance_lengths, LZ_DIST_SYMBOLS);

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        *a_error = strerror(errno);
        free(tokens);
        return false;
    }

    fwrite(ARCHIVE_MAGIC, 1, ARCHIVE_MAGIC_LEN, out);
    fputc(ARCHIVE_VERSION, out);
    write_be(out, length, 8);

    bool flushed = true;
    if (ntokens > 0) {
        BitWriter writer;
        bit_writer_init(&writer, out);
        write_code_lengths(&writer, litlen_lengths, LZ_LITLEN_SYMBOLS);
        write_code_lengths(&writer, distance_lengths, LZ_DIST_SYMBOLS);
        write_tokens(&writer, tokens, ntokens, &litlen, &distance);
        flushed = bit_writer_flush(&writer);
    }

    free(tokens);

    if (!flushed) {
        *a_error = "failed to write compressed data";
        fclose(out);
        return false;
    }
    if (fclose(out) != 0) {
        *a_error = strerror(errno);
        return false;
    }
    return true;
}

// Expands the token stream into `out`, which must have room for `len` bytes.
static bool read_tokens(BitReader *a_reader, uint8_t *out, uint64_t len,
                        const DecodeTable *litlen, const DecodeTable *distance,
                        const char **a_error) {
    uint64_t pos = 0;
    while (pos < len) {
        uint16_t symbol;
        if (!decode_symbol(a_reader, litlen, &symbol)) {
            *a_error = "truncated or corrupt compressed data";
            return false;
        }

        if (symbol < 256) {
            out[pos++] = (uint8_t)symbol;
            continue;
        }
        if (symbol < LZ_FIRST_LENGTH_SYMBOL || symbol >= LZ_LITLEN_SYMBOLS) {
            *a_error = "invalid length symbol";
            return false;
        }

        int li = symbol - LZ_FIRST_LENGTH_SYMBOL;
        uint64_t extra;
        if (!read_bits(a_reader, lz_length_extra[li], &extra)) {
            *a_error = "truncated or corrupt compressed data";
            return false;
        }
        uint64_t match_len = lz_length_base[li] + extra;

        uint16_t dist_symbol;
        if (!decode_symbol(a_reader, distance, &dist_symbol) ||
            dist_symbol >= LZ_DIST_SYMBOLS) {
            *a_error = "truncated or corrupt compressed data";
            return false;
        }
        if (!read_bits(a_reader, lz_distance_extra[dist_symbol], &extra)) {
            *a_error = "truncated or corrupt compressed data";
            return false;
        }
        uint64_t match_dist = lz_distance_base[dist_symbol] + extra;

        if (match_dist > pos || pos + match_len > len) {
            *a_error = "back-reference outside the output";
            return false;
        }

        // Copied one byte at a time on purpose: when the distance is shorter
        // than the length the copy overlaps itself, which is how a run like
        // "aaaaaa" is stored as one byte plus a back-reference of distance 1.
        uint64_t from = pos - match_dist;
        for (uint64_t i = 0; i < match_len; i++) out[pos++] = out[from + i];
    }
    return true;
}

bool huffman_decompress(const char *in_path, const char *out_path, const char **a_error) {
    FILE *in = fopen(in_path, "rb");
    if (in == NULL) {
        *a_error = strerror(errno);
        return false;
    }

    char magic[ARCHIVE_MAGIC_LEN];
    uint64_t version, length;
    if (fread(magic, 1, sizeof(magic), in) != sizeof(magic) ||
        memcmp(magic, ARCHIVE_MAGIC, sizeof(magic)) != 0) {
        *a_error = "not a huffman file";
        fclose(in);
        return false;
    }
    if (!read_be(in, &version, 1) || !read_be(in, &length, 8)) {
        *a_error = "truncated header";
        fclose(in);
        return false;
    }
    if (version != ARCHIVE_VERSION) {
        *a_error = "unsupported format version";
        fclose(in);
        return false;
    }

    uint8_t *out_bytes = NULL;
    DecodeTable *litlen = NULL;
    DecodeTable *distance = NULL;
    bool ok = true;

    if (length > 0) {
        BitReader reader;
        bit_reader_init(&reader, in);

        uint8_t litlen_lengths[LZ_LITLEN_SYMBOLS];
        uint8_t distance_lengths[LZ_DIST_SYMBOLS];
        if (!read_code_lengths(&reader, litlen_lengths, LZ_LITLEN_SYMBOLS) ||
            !read_code_lengths(&reader, distance_lengths, LZ_DIST_SYMBOLS)) {
            *a_error = "corrupt code lengths";
            ok = false;
        }
        else {
            litlen = decode_table_create(litlen_lengths, LZ_LITLEN_SYMBOLS);
            distance = decode_table_create(distance_lengths, LZ_DIST_SYMBOLS);
            out_bytes = malloc((size_t)length);
            if (litlen == NULL || distance == NULL || out_bytes == NULL) {
                *a_error = "out of memory";
                ok = false;
            }
            else {
                ok = read_tokens(&reader, out_bytes, length, litlen, distance, a_error);
            }
        }
    }

    fclose(in);
    decode_table_destroy(&litlen);
    decode_table_destroy(&distance);

    if (!ok) {
        free(out_bytes);
        return false;
    }

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        *a_error = strerror(errno);
        free(out_bytes);
        return false;
    }
    if (length > 0 && fwrite(out_bytes, 1, (size_t)length, out) != (size_t)length) {
        *a_error = "failed to write output";
        free(out_bytes);
        fclose(out);
        return false;
    }
    free(out_bytes);

    if (fclose(out) != 0) {
        *a_error = strerror(errno);
        return false;
    }
    return true;
}
