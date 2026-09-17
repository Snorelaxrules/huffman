#include "bitwriter.h"

void bit_writer_init(BitWriter *a_writer, FILE *file) {
    a_writer->file = file;
    a_writer->buffer = 0;
    a_writer->count = 0;
}

void write_bits(BitWriter *a_writer, uint8_t bits, uint8_t nbits) {
    for (int i = nbits - 1; i >= 0; i--) {
        a_writer->buffer = (uint8_t)((a_writer->buffer << 1) | ((bits >> i) & 1));
        if (++a_writer->count == 8) {
            fputc(a_writer->buffer, a_writer->file);
            a_writer->buffer = 0;
            a_writer->count = 0;
        }
    }
}

void bit_writer_flush(BitWriter *a_writer) {
    if (a_writer->count == 0) return;

    // Left-align the leftover bits; the padding is harmless because the
    // header's length field tells the reader how many bytes to decode.
    fputc(a_writer->buffer << (8 - a_writer->count), a_writer->file);
    a_writer->buffer = 0;
    a_writer->count = 0;
}

void bit_reader_init(BitReader *a_reader, FILE *file) {
    a_reader->file = file;
    a_reader->buffer = 0;
    a_reader->count = 0;
}

bool read_bits(BitReader *a_reader, uint8_t nbits, uint8_t *a_bits) {
    uint8_t out = 0;
    for (int i = 0; i < nbits; i++) {
        if (a_reader->count == 0) {
            int c = fgetc(a_reader->file);
            if (c == EOF) return false;
            a_reader->buffer = (uint8_t)c;
            a_reader->count = 8;
        }
        out = (uint8_t)((out << 1) | (a_reader->buffer >> 7));
        a_reader->buffer = (uint8_t)(a_reader->buffer << 1);
        a_reader->count--;
    }
    *a_bits = out;
    return true;
}
