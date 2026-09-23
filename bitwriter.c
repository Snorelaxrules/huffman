#include "bitwriter.h"

#include <assert.h>

// Low `nbits` bits set. Undefined for nbits == 64, which BIT_MAX_RUN excludes.
static uint64_t low_mask(int nbits) {
    return (UINT64_C(1) << nbits) - 1;
}

void bit_writer_init(BitWriter *a_writer, FILE *file) {
    a_writer->file = file;
    a_writer->accumulator = 0;
    a_writer->nbits = 0;
    a_writer->nbytes = 0;
    a_writer->failed = false;
}

static void flush_buffer(BitWriter *a_writer) {
    if (a_writer->nbytes == 0) return;

    if (fwrite(a_writer->bytes, 1, a_writer->nbytes, a_writer->file) != a_writer->nbytes) {
        a_writer->failed = true;
    }
    a_writer->nbytes = 0;
}

void write_bits(BitWriter *a_writer, uint64_t bits, int nbits) {
    assert(nbits >= 0 && nbits <= BIT_MAX_RUN);

    a_writer->accumulator = (a_writer->accumulator << nbits) | (bits & low_mask(nbits));
    a_writer->nbits += nbits;

    while (a_writer->nbits >= 8) {
        a_writer->nbits -= 8;
        a_writer->bytes[a_writer->nbytes++] = (uint8_t)(a_writer->accumulator >> a_writer->nbits);
        if (a_writer->nbytes == BIT_BUFFER_SIZE) flush_buffer(a_writer);
    }
}

bool bit_writer_flush(BitWriter *a_writer) {
    if (a_writer->nbits > 0) {
        // Left-align the leftover bits. The padding is harmless because the
        // header's length field tells the reader how many bytes to decode.
        write_bits(a_writer, 0, 8 - a_writer->nbits);
    }
    flush_buffer(a_writer);
    return !a_writer->failed;
}

void bit_reader_init(BitReader *a_reader, FILE *file) {
    a_reader->file = file;
    a_reader->accumulator = 0;
    a_reader->nbits = 0;
    a_reader->padding = 0;
    a_reader->truncated = false;
    a_reader->nbytes = 0;
    a_reader->pos = 0;
}

// Grows the accumulator to at least `nbits` bits, padding with zeros at EOF.
static void fill(BitReader *a_reader, int nbits) {
    while (a_reader->nbits < nbits) {
        if (a_reader->pos == a_reader->nbytes) {
            a_reader->nbytes = fread(a_reader->bytes, 1, BIT_BUFFER_SIZE, a_reader->file);
            a_reader->pos = 0;
            if (a_reader->nbytes == 0) {
                a_reader->accumulator <<= 8;
                a_reader->nbits += 8;
                a_reader->padding += 8;
                continue;
            }
        }
        a_reader->accumulator = (a_reader->accumulator << 8) | a_reader->bytes[a_reader->pos++];
        a_reader->nbits += 8;
    }
}

uint64_t peek_bits(BitReader *a_reader, int nbits) {
    assert(nbits >= 0 && nbits <= BIT_MAX_RUN);

    fill(a_reader, nbits);
    return (a_reader->accumulator >> (a_reader->nbits - nbits)) & low_mask(nbits);
}

bool consume_bits(BitReader *a_reader, int nbits) {
    assert(nbits >= 0 && nbits <= BIT_MAX_RUN);

    fill(a_reader, nbits);

    // Padding always sits at the tail of the accumulator, so the request is
    // satisfied by real data only if it fits in what precedes the padding.
    bool real = nbits <= a_reader->nbits - a_reader->padding;
    a_reader->nbits -= nbits;
    if (a_reader->padding > a_reader->nbits) a_reader->padding = a_reader->nbits;
    if (!real) a_reader->truncated = true;
    return real;
}

bool read_bits(BitReader *a_reader, int nbits, uint64_t *a_bits) {
    *a_bits = peek_bits(a_reader, nbits);
    return consume_bits(a_reader, nbits);
}
