#include "lz77.h"

#include <string.h>

const uint16_t lz_length_base[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11,  13,
                                     15, 17, 19, 23,  27,  31,  35,  43,  51,  59,
                                     67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t lz_length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

const uint16_t lz_distance_base[LZ_DIST_SYMBOLS] = {
    1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
    33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
    1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
const uint8_t lz_distance_extra[LZ_DIST_SYMBOLS] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

int lz77_length_index(int length) {
    int i = 28;
    while (i > 0 && lz_length_base[i] > length) i--;
    return i;
}

int lz77_distance_index(int distance) {
    int i = LZ_DIST_SYMBOLS - 1;
    while (i > 0 && lz_distance_base[i] > distance) i--;
    return i;
}

// Hashes the three bytes at `p` into a bucket. Three because that is the
// shortest match worth emitting, so every match starts with a colliding triple.
static uint32_t hash3(const uint8_t *p) {
    uint32_t key = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return (key * UINT32_C(2654435761)) >> (32 - LZ_HASH_BITS);
}

/**
 * Hash chains: `head[h]` is the most recent position whose triple hashed to h,
 * and `prev[i]` is the position before `i` in that same bucket. Following the
 * chain from a position visits earlier occurrences of the same three bytes,
 * newest first, so the search can stop as soon as it leaves the window.
 */
typedef struct
{
  int32_t head[1 << LZ_HASH_BITS];
  int32_t *prev;
} MatchFinder;

static size_t match_length(const uint8_t *data, size_t len, size_t a, size_t b, size_t limit) {
    size_t n = 0;
    while (n < limit && b + n < len && data[a + n] == data[b + n]) n++;
    return n;
}

// Finds the longest match for `pos`, then links `pos` into its bucket.
static size_t find_match(MatchFinder *finder, const uint8_t *data, size_t len, size_t pos,
                         size_t *a_distance) {
    uint32_t bucket = hash3(data + pos);
    int32_t candidate = finder->head[bucket];

    size_t best = 0;
    size_t limit = len - pos < LZ_MAX_MATCH ? len - pos : LZ_MAX_MATCH;
    size_t oldest = pos > LZ_WINDOW_SIZE ? pos - LZ_WINDOW_SIZE : 0;

    for (int chain = 0; chain < LZ_MAX_CHAIN && candidate >= 0; chain++) {
        if ((size_t)candidate < oldest) break; // the rest of the chain is older still

        size_t n = match_length(data, len, (size_t)candidate, pos, limit);
        if (n > best) {
            best = n;
            *a_distance = pos - (size_t)candidate;
            if (n == limit) break; // cannot do better
        }
        candidate = finder->prev[candidate];
    }

    finder->prev[pos] = finder->head[bucket];
    finder->head[bucket] = (int32_t)pos;
    return best;
}

size_t lz77_tokenize(const uint8_t *data, size_t len, Token *tokens, bool *a_failed) {
    *a_failed = false;
    if (len == 0) return 0;

    MatchFinder *finder = malloc(sizeof(MatchFinder));
    if (finder == NULL) {
        *a_failed = true;
        return 0;
    }
    memset(finder->head, 0xff, sizeof(finder->head)); // -1 means "empty bucket"
    finder->prev = malloc(len * sizeof(int32_t));
    if (finder->prev == NULL) {
        free(finder);
        *a_failed = true;
        return 0;
    }

    size_t ntokens = 0;
    size_t pos = 0;
    while (pos < len) {
        size_t distance = 0;
        size_t length = 0;

        // The last two bytes cannot start a match, and hash3 would read past
        // the end of the buffer.
        if (pos + LZ_MIN_MATCH <= len) {
            length = find_match(finder, data, len, pos, &distance);
        }

        if (length >= LZ_MIN_MATCH) {
            tokens[ntokens].length = (uint16_t)length;
            tokens[ntokens].distance = (uint16_t)distance;
            ntokens++;

            // Every position inside the match still needs to enter the chains,
            // or later searches would never find matches starting there.
            for (size_t i = 1; i < length; i++) {
                if (pos + i + LZ_MIN_MATCH <= len) {
                    uint32_t bucket = hash3(data + pos + i);
                    finder->prev[pos + i] = finder->head[bucket];
                    finder->head[bucket] = (int32_t)(pos + i);
                }
            }
            pos += length;
        }
        else {
            tokens[ntokens].length = 0;
            tokens[ntokens].distance = data[pos];
            ntokens++;
            pos++;
        }
    }

    free(finder->prev);
    free(finder);
    return ntokens;
}
