#ifndef HUFFMAN_H
#define HUFFMAN_H

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
 * Bit-level output stream. Declared here rather than included; the definition
 * lives with the implementation.
 */
typedef struct _BitWriter BitWriter;

/** @brief Write the low `nbits` bits of `bits` to `a_writer`. */
void write_bits(BitWriter *a_writer, uint8_t bits, uint8_t nbits);

/** Character frequencies, indexed by byte value. */
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
 * @brief Write the coding table encoded by `root` to `a_writer`.
 */
void write_coding_table(TreeNode *root, BitWriter *a_writer);

/**
 * @brief Compress the NUL-terminated `uncompressed_bytes` using `root` and
 * write the resulting bits to `a_writer`.
 */
void write_compressed(BitWriter *a_writer, uint8_t *uncompressed_bytes, TreeNode *root);

#endif // HUFFMAN_H
