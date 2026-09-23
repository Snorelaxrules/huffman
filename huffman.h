#ifndef HUFFMAN_H
#define HUFFMAN_H

#include "bitwriter.h"
#include "priority_queue.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char uchar;

/**
 * Longest code this implementation emits.
 *
 * Capping the length is what makes a single-lookup decode table possible: the
 * table has one entry per 15-bit window, so any code can be recognized by one
 * indexed load. Huffman can naturally produce codes up to 255 bits, so
 * limit_code_lengths(...) trades a sliver of compression to enforce this.
 */
#define MAX_CODE_LENGTH 15

/**
 * Compressed file layout. The header is byte-aligned and written first; the
 * code lengths and the compressed data follow as one continuous bit stream.
 *
 *   offset  size  field
 *        0     4  magic "HUFF"
 *        4     1  format version
 *        5     2  number of distinct symbols, big-endian (0 for an empty file)
 *        7     8  original length in bytes, big-endian
 *       15     -  code lengths, then compressed data, zero-padded to a byte
 *
 * Storing the original length is what lets the reader stop exactly at the end
 * of the data and ignore the padding bits in the final byte.
 *
 * Version 2 replaced the serialized tree of version 1 with a list of code
 * lengths; the two are not interchangeable.
 */
#define HUFFMAN_MAGIC "HUFF"
#define HUFFMAN_MAGIC_LEN 4
#define HUFFMAN_VERSION 2
#define HUFFMAN_HEADER_LEN 15

typedef uint64_t Frequencies[256];

/**
 * @brief Count the byte frequencies of the file at `path` into `freqs`.
 *
 * @param freqs caller must zero-initialize every entry first
 * @param a_error set to strerror(errno) if the file cannot be opened
 * @return false if the file could not be opened, true otherwise
 */
bool calc_frequencies(Frequencies freqs, const char *path, const char **a_error);

/** A node in a Huffman tree. Interior nodes have both children set. */
typedef struct _TreeNode
{
  uchar character;
  size_t frequency;
  struct _TreeNode *left;
  struct _TreeNode *right;
} TreeNode;

/**
 * @brief Build a Huffman tree from `freq`.
 *
 * @return the root, or NULL if no character has a nonzero frequency
 */
TreeNode *make_huffman_tree(Frequencies freq);

/**
 * @brief Free a tree from make_huffman_tree(...) and set `*a_root` to NULL.
 */
void destroy_huffman_tree(TreeNode **a_root);

/**
 * @brief Record the depth of every leaf in `root` into `lengths`.
 *
 * The depth of a symbol's leaf is the length of its code. Absent symbols are
 * left at 0. Once lengths are known the tree carries no further information
 * and can be freed.
 *
 * @param lengths a 256-entry array, zero-initialized by the caller
 */
void get_code_lengths(TreeNode *root, uint8_t lengths[256]);

/**
 * @brief Shorten any code longer than MAX_CODE_LENGTH, keeping `lengths` valid.
 *
 * Clamping alone would overfill the code space, so codes are then lengthened
 * until Kraft's inequality holds again. Costs a little compression and only
 * does anything for very skewed frequency distributions.
 */
void limit_code_lengths(uint8_t lengths[256]);

/**
 * A code per symbol. Codes are right-aligned in `codes` and at most
 * MAX_CODE_LENGTH bits; a length of 0 means the symbol does not occur.
 */
typedef struct _CodeTable
{
  uint16_t codes[256];
  uint8_t lengths[256];
} CodeTable;

/**
 * @brief Derive the canonical code for every symbol from `lengths` alone.
 *
 * Symbols are ordered by (length, symbol value) and assigned consecutive
 * integers, shifting left whenever the length grows. Encoder and decoder run
 * this same function, which is why only the lengths need to be transmitted.
 */
void build_canonical_codes(CodeTable *a_table, const uint8_t lengths[256]);

/** @brief Write `lengths` to `a_writer`, run-length encoding absent symbols. */
void write_code_lengths(BitWriter *a_writer, const uint8_t lengths[256]);

/**
 * @brief Read back what write_code_lengths(...) wrote.
 *
 * @return false if the stream ended early or the encoding is malformed
 */
bool read_code_lengths(BitReader *a_reader, uint8_t lengths[256]);

/** One decoded symbol; a length of 0 marks a window no code matches. */
typedef struct _DecodeEntry
{
  uint8_t symbol;
  uint8_t length;
} DecodeEntry;

/**
 * Maps every MAX_CODE_LENGTH-bit window to the symbol whose code starts it.
 *
 * Because canonical codes are prefix-free, a code of length L owns the
 * 2^(MAX_CODE_LENGTH - L) windows that begin with it, so one indexed load
 * decodes a whole symbol however many bits it actually spans.
 */
typedef struct _DecodeTable
{
  DecodeEntry entries[1 << MAX_CODE_LENGTH];
} DecodeTable;

/**
 * @brief Build the decode table for `lengths`.
 *
 * @return an owned table to release with decode_table_destroy(...), or NULL if
 * memory ran out
 */
DecodeTable *decode_table_create(const uint8_t lengths[256]);

/** @brief Free a table from decode_table_create(...) and NULL `*a_table`. */
void decode_table_destroy(DecodeTable **a_table);

/**
 * @brief Compress `len` bytes of `uncompressed_bytes` using `table`.
 */
void write_compressed(BitWriter *a_writer, const uint8_t *uncompressed_bytes, uint64_t len,
                      const CodeTable *table);

/**
 * @brief Decode exactly `len` bytes from `a_reader` using `table` into `file`.
 *
 * @return false if the bit stream ran out or held a code the table rejects
 */
bool read_compressed(BitReader *a_reader, FILE *file, const DecodeTable *table, uint64_t len);

/**
 * @brief Compress `in_path` to `out_path`.
 *
 * @param a_error set to a message if the operation fails
 */
bool huffman_compress(const char *in_path, const char *out_path, const char **a_error);

/**
 * @brief Decompress `in_path` to `out_path`, reversing huffman_compress(...).
 *
 * @param a_error set to a message if the operation fails
 */
bool huffman_decompress(const char *in_path, const char *out_path, const char **a_error);

#endif // HUFFMAN_H
