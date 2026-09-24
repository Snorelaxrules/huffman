#ifndef LZ77_H
#define LZ77_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * LZ77 replaces repeated text with a reference to where it appeared before.
 * The output is a stream of tokens: either a literal byte, or "go back
 * `distance` bytes and copy `length` of them".
 */

/** How far back a match may reach. */
#define LZ_WINDOW_SIZE 32768

/** Shorter matches cost more to encode than the bytes they replace. */
#define LZ_MIN_MATCH 3
#define LZ_MAX_MATCH 258

/** Hash chain buckets, and how many candidates a search will walk. */
#define LZ_HASH_BITS 15
#define LZ_MAX_CHAIN 128

/**
 * Literal/length alphabet: 0-255 are literal bytes, 256 is unused, and 257-285
 * are length codes. Distances use their own 30-symbol alphabet.
 */
#define LZ_LITLEN_SYMBOLS 286
#define LZ_DIST_SYMBOLS 30
#define LZ_FIRST_LENGTH_SYMBOL 257

/** A literal byte (`length` 0, byte in `distance`) or a back-reference. */
typedef struct _Token
{
  uint16_t length;
  uint16_t distance;
} Token;

/**
 * @brief Split `len` bytes of `data` into tokens, writing them to `tokens`.
 *
 * `tokens` must have room for `len` entries, the all-literals worst case.
 *
 * @return the number of tokens written, or 0 with `*a_failed` set if the match
 * finder could not allocate
 */
size_t lz77_tokenize(const uint8_t *data, size_t len, Token *tokens, bool *a_failed);

/**
 * Length and distance codes carry a base value plus a few literal "extra" bits,
 * so one symbol covers a range. These are the DEFLATE tables.
 */
extern const uint16_t lz_length_base[29];
extern const uint8_t lz_length_extra[29];
extern const uint16_t lz_distance_base[LZ_DIST_SYMBOLS];
extern const uint8_t lz_distance_extra[LZ_DIST_SYMBOLS];

/** @brief Index into the length tables for a match of `length` bytes. */
int lz77_length_index(int length);

/** @brief Index into the distance tables for a match `distance` bytes back. */
int lz77_distance_index(int distance);

#endif // LZ77_H
