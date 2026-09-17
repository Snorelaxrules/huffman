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
 * Compressed file layout. The header is byte-aligned and written first; the
 * coding table and the compressed data follow as one continuous bit stream.
 *
 *   offset  size  field
 *        0     4  magic "HUFF"
 *        4     1  format version
 *        5     2  number of distinct symbols, big-endian (0 for an empty file)
 *        7     8  original length in bytes, big-endian
 *       15     -  coding table, then compressed data, zero-padded to a byte
 *
 * Storing the original length is what lets the reader stop exactly at the end
 * of the data and ignore the padding bits in the final byte.
 */
#define HUFFMAN_MAGIC "HUFF"
#define HUFFMAN_MAGIC_LEN 4
#define HUFFMAN_VERSION 1
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
 * @brief Write the coding table encoded by `root` to `a_writer`.
 *
 * Post-order: a leaf emits 1 then its 8-bit character, an interior node emits
 * its two subtrees then 0.
 */
void write_coding_table(TreeNode *root, BitWriter *a_writer);

/**
 * @brief Rebuild the tree written by write_coding_table(...).
 *
 * @param nsymbols the leaf count recorded in the header, which is what tells
 * this function where the table ends
 * @return the root, or NULL if the table is malformed or truncated
 */
TreeNode *read_coding_table(BitReader *a_reader, uint16_t nsymbols);

/**
 * @brief Compress `len` bytes of `uncompressed_bytes` using `root`.
 */
void write_compressed(BitWriter *a_writer, const uint8_t *uncompressed_bytes, uint64_t len,
                      TreeNode *root);

/**
 * @brief Decode exactly `len` bytes from `a_reader` using `root` into `file`.
 *
 * @return false if the bit stream ran out or led somewhere invalid
 */
bool read_compressed(BitReader *a_reader, FILE *file, TreeNode *root, uint64_t len);

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
