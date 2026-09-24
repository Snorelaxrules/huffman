#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stdbool.h>

/**
 * Compressed file layout. The header is byte-aligned and written first; the
 * two code length lists and the token stream follow as one bit stream.
 *
 *   offset  size  field
 *        0     4  magic "HUFF"
 *        4     1  format version
 *        5     8  original length in bytes, big-endian
 *       13     -  literal/length code lengths, distance code lengths, tokens
 *
 * Storing the original length is what lets the reader stop exactly at the end
 * of the data and ignore the padding bits in the final byte.
 *
 * Version 3 added the LZ77 pass, so the token stream replaced the plain
 * Huffman-coded bytes of version 2. The versions are not interchangeable.
 */
#define ARCHIVE_MAGIC "HUFF"
#define ARCHIVE_MAGIC_LEN 4
#define ARCHIVE_VERSION 3
#define ARCHIVE_HEADER_LEN 13

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

#endif // ARCHIVE_H
