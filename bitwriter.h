#ifndef BITWRITER_H
#define BITWRITER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/** Bytes buffered before hitting the file. */
#define BIT_BUFFER_SIZE 16384

/** Most bits that may be moved in a single call. */
#define BIT_MAX_RUN 56

/**
 * Bit-level output stream. Bits are packed into bytes most significant first.
 *
 * Bits land in a 64-bit accumulator and leave it a whole byte at a time, so the
 * per-bit cost is a shift and a mask rather than a call into stdio.
 */
typedef struct _BitWriter
{
  FILE *file;
  uint64_t accumulator; // pending bits, right-aligned
  int nbits;            // bits held in accumulator, always 0-7 between calls
  uint8_t bytes[BIT_BUFFER_SIZE];
  size_t nbytes; // bytes held in bytes[]
  bool failed;   // a write to file failed
} BitWriter;

/** @brief Prepare `a_writer` to write to `file`. */
void bit_writer_init(BitWriter *a_writer, FILE *file);

/**
 * @brief Write the low `nbits` bits of `bits`, most significant first.
 *
 * `nbits` must be between 0 and BIT_MAX_RUN.
 */
void write_bits(BitWriter *a_writer, uint64_t bits, int nbits);

/**
 * @brief Flush the accumulator, zero-padding the final byte, then the buffer.
 *
 * Must be called before closing the file or the trailing bits are lost.
 *
 * @return false if any write to the file failed
 */
bool bit_writer_flush(BitWriter *a_writer);

/**
 * Bit-level input stream, the mirror of BitWriter.
 *
 * Past the end of the file the accumulator is fed zero bits, so peeking never
 * needs a bounds check; requests that consume those bits report failure.
 */
typedef struct _BitReader
{
  FILE *file;
  uint64_t accumulator; // unread bits, right-aligned
  int nbits;            // bits held in accumulator
  int padding;          // how many of those bits are past end of file
  bool truncated;       // a read consumed bits past end of file
  uint8_t bytes[BIT_BUFFER_SIZE];
  size_t nbytes, pos;
} BitReader;

/** @brief Prepare `a_reader` to read from `file`. */
void bit_reader_init(BitReader *a_reader, FILE *file);

/**
 * @brief Read and consume `nbits` bits into `*a_bits`, most significant first.
 *
 * @return false if the stream ended before `nbits` bits were available
 */
bool read_bits(BitReader *a_reader, int nbits, uint64_t *a_bits);

/**
 * @brief Return the next `nbits` bits without consuming them.
 *
 * Reading past the end yields zeros rather than failing, so the caller can
 * inspect a fixed-width window and then consume only the part it used.
 */
uint64_t peek_bits(BitReader *a_reader, int nbits);

/**
 * @brief Discard `nbits` bits, which must already have been peeked.
 *
 * @return false if those bits were past the end of the stream
 */
bool consume_bits(BitReader *a_reader, int nbits);

#endif // BITWRITER_H
