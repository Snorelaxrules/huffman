#ifndef HUFFMAN_H
#define HUFFMAN_H

#include "bitwriter.h"
#include "priority_queue.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Largest alphabet this codec handles. */
#define MAX_SYMBOLS 288

/**
 * Longest code this implementation emits.
 *
 * Capping the length is what makes a single-lookup decode table possible: the
 * table has one entry per 15-bit window, so any code is recognized by one
 * indexed load. Huffman can naturally produce much longer codes, so
 * limit_code_lengths(...) trades a sliver of compression to enforce this.
 */
#define MAX_CODE_LENGTH 15

/** A node in a Huffman tree. Interior nodes have both children set. */
typedef struct _TreeNode
{
  uint16_t symbol;
  uint64_t frequency;
  struct _TreeNode *left;
  struct _TreeNode *right;
} TreeNode;

/**
 * @brief Build a Huffman tree over `nsymbols` symbols weighted by `freqs`.
 *
 * @return the root, or NULL if every frequency is zero
 */
TreeNode *make_huffman_tree(const uint64_t *freqs, int nsymbols);

/** @brief Free a tree from make_huffman_tree(...) and set `*a_root` to NULL. */
void destroy_huffman_tree(TreeNode **a_root);

/**
 * @brief Record the depth of every leaf in `root` into `lengths`.
 *
 * The depth of a symbol's leaf is the length of its code. Once lengths are
 * known the tree carries no further information and can be freed.
 *
 * @param lengths zero-initialized by the caller, `nsymbols` entries
 */
void get_code_lengths(TreeNode *root, uint8_t *lengths);

/**
 * @brief Shorten any code longer than MAX_CODE_LENGTH, keeping `lengths` valid.
 *
 * Clamping alone would overfill the code space, so codes are then lengthened
 * until Kraft's inequality holds again. Costs a little compression and only
 * does anything for very skewed frequency distributions.
 */
void limit_code_lengths(uint8_t *lengths, int nsymbols);

/**
 * @brief Frequencies to length-limited code lengths, in one call.
 *
 * Builds a tree, reads off the depths and discards it.
 */
void build_code_lengths(const uint64_t *freqs, int nsymbols, uint8_t *lengths);

/**
 * A code per symbol. Codes are right-aligned and at most MAX_CODE_LENGTH bits;
 * a length of 0 means the symbol does not occur.
 */
typedef struct _CodeTable
{
  uint16_t codes[MAX_SYMBOLS];
  uint8_t lengths[MAX_SYMBOLS];
} CodeTable;

/**
 * @brief Derive the canonical code for every symbol from `lengths` alone.
 *
 * Symbols are ordered by (length, symbol value) and assigned consecutive
 * integers, shifting left whenever the length grows. Encoder and decoder run
 * this same function, which is why only the lengths need to be transmitted.
 */
void build_canonical_codes(CodeTable *a_table, const uint8_t *lengths, int nsymbols);

/** @brief Emit `symbol` using `table`. */
void write_symbol(BitWriter *a_writer, const CodeTable *table, int symbol);

/** @brief Write `lengths` to `a_writer`, run-length encoding absent symbols. */
void write_code_lengths(BitWriter *a_writer, const uint8_t *lengths, int nsymbols);

/**
 * @brief Read back what write_code_lengths(...) wrote.
 *
 * @return false if the stream ended early or the encoding is malformed
 */
bool read_code_lengths(BitReader *a_reader, uint8_t *lengths, int nsymbols);

/** One decoded symbol; a length of 0 marks a window no code matches. */
typedef struct _DecodeEntry
{
  uint16_t symbol;
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
DecodeTable *decode_table_create(const uint8_t *lengths, int nsymbols);

/** @brief Free a table from decode_table_create(...) and NULL `*a_table`. */
void decode_table_destroy(DecodeTable **a_table);

/**
 * @brief Decode one symbol from `a_reader`.
 *
 * @return false if the stream ran out or the next bits match no code
 */
bool decode_symbol(BitReader *a_reader, const DecodeTable *table, uint16_t *a_symbol);

#endif // HUFFMAN_H
