#ifndef BITWRITER_H
#define BITWRITER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/** Bit-level output stream. Bits are packed into bytes MSB-first. */
typedef struct _BitWriter
{
  FILE *file;
  uint8_t buffer;
  int count; // bits currently buffered, always 0-7
} BitWriter;

/** @brief Prepare `a_writer` to write to `file`. */
void bit_writer_init(BitWriter *a_writer, FILE *file);

/** @brief Write the low `nbits` (at most 8) bits of `bits`, most significant first. */
void write_bits(BitWriter *a_writer, uint8_t bits, uint8_t nbits);

/**
 * @brief Flush any partial byte, zero-padding it.
 *
 * Must be called before closing the file or the trailing bits are lost.
 */
void bit_writer_flush(BitWriter *a_writer);

/** Bit-level input stream, the mirror of BitWriter. */
typedef struct _BitReader
{
  FILE *file;
  uint8_t buffer;
  int count; // bits still unread in buffer, always 0-8
} BitReader;

/** @brief Prepare `a_reader` to read from `file`. */
void bit_reader_init(BitReader *a_reader, FILE *file);

/**
 * @brief Read `nbits` (at most 8) bits into `*a_bits`, most significant first.
 *
 * @return false if the stream ran out before `nbits` bits were available
 */
bool read_bits(BitReader *a_reader, uint8_t nbits, uint8_t *a_bits);

#endif // BITWRITER_H
