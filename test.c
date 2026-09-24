// Round-trip and edge-case tests. Build with `make test`.
#include "archive.h"
#include "huffman.h"
#include "lz77.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TMP_IN "/tmp/huff-test.in"
#define TMP_HUF "/tmp/huff-test.huf"
#define TMP_OUT "/tmp/huff-test.out"

static int failures = 0;

static void check(bool ok, const char *name, const char *detail) {
    printf("%s %-26s %s\n", ok ? "ok  " : "FAIL", name, detail);
    if (!ok) failures++;
}

static void write_file(const char *path, const uint8_t *data, size_t len) {
    FILE *file = fopen(path, "wb");
    fwrite(data, 1, len, file);
    fclose(file);
}

static long size_of(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return -1;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size;
}

static void roundtrip(const char *name, const uint8_t *data, size_t len) {
    write_file(TMP_IN, data, len);

    const char *error = NULL;
    char detail[128];
    if (!huffman_compress(TMP_IN, TMP_HUF, &error)) {
        snprintf(detail, sizeof detail, "compress failed: %s", error);
        check(false, name, detail);
        return;
    }
    if (!huffman_decompress(TMP_HUF, TMP_OUT, &error)) {
        snprintf(detail, sizeof detail, "decompress failed: %s", error);
        check(false, name, detail);
        return;
    }

    uint8_t *got = malloc(len + 1);
    FILE *file = fopen(TMP_OUT, "rb");
    size_t n = fread(got, 1, len + 1, file);
    fclose(file);

    bool ok = n == len && (len == 0 || memcmp(got, data, len) == 0);
    snprintf(detail, sizeof detail, ok ? "%7zu -> %ld bytes" : "got %zu bytes, want %zu",
             ok ? len : n, ok ? size_of(TMP_HUF) : (long)len);
    check(ok, name, detail);
    free(got);
}

// Fibonacci frequencies are the worst case for code length: each symbol sits
// one level deeper than the last, so this forces limit_code_lengths to act.
static void test_length_limiting(void) {
    uint64_t freqs[LZ_LITLEN_SYMBOLS] = {0};
    uint64_t a = 1, b = 1;
    for (int i = 0; i < 24; i++) {
        freqs[i] = a;
        uint64_t next = a + b;
        a = b;
        b = next;
    }

    uint8_t lengths[LZ_LITLEN_SYMBOLS] = {0};
    TreeNode *root = make_huffman_tree(freqs, LZ_LITLEN_SYMBOLS);
    get_code_lengths(root, lengths);
    destroy_huffman_tree(&root);

    int unlimited = 0;
    for (int i = 0; i < LZ_LITLEN_SYMBOLS; i++)
        if (lengths[i] > unlimited) unlimited = lengths[i];

    limit_code_lengths(lengths, LZ_LITLEN_SYMBOLS);

    int limited = 0;
    uint32_t claimed = 0;
    for (int i = 0; i < LZ_LITLEN_SYMBOLS; i++) {
        if (lengths[i] == 0) continue;
        if (lengths[i] > limited) limited = lengths[i];
        claimed += (UINT32_C(1) << MAX_CODE_LENGTH) >> lengths[i];
    }

    char detail[128];
    snprintf(detail, sizeof detail, "depth %d -> %d, kraft %u/%u", unlimited, limited, claimed,
             UINT32_C(1) << MAX_CODE_LENGTH);
    check(unlimited > MAX_CODE_LENGTH && limited <= MAX_CODE_LENGTH &&
              claimed <= (UINT32_C(1) << MAX_CODE_LENGTH),
          "length limiting", detail);

    size_t len = 0;
    static uint8_t data[200000];
    for (int i = 0; i < 24 && len < sizeof data; i++)
        for (uint64_t j = 0; j < freqs[i] && len < sizeof data; j++) data[len++] = (uint8_t)i;
    roundtrip("fibonacci corpus", data, len);
}

// Every distinct match shape LZ77 can emit needs to survive a round trip.
static void test_lz77_shapes(void) {
    // Overlapping copy: distance 1, length far greater, the run-length case.
    static uint8_t run[100000];
    memset(run, 'q', sizeof run);
    roundtrip("overlapping run", run, sizeof run);

    // A match at the longest distance the window allows.
    static uint8_t far[LZ_WINDOW_SIZE + 64];
    srand(11);
    for (size_t i = 0; i < sizeof far; i++) far[i] = (uint8_t)(rand() % 251 + 1);
    memcpy(far + sizeof far - 32, far, 32);
    roundtrip("match at window edge", far, sizeof far);

    // A match longer than LZ_MAX_MATCH, which must split into several tokens.
    static uint8_t longrep[4096];
    for (size_t i = 0; i < sizeof longrep; i++) longrep[i] = (uint8_t)(i % 7);
    roundtrip("match beyond max len", longrep, sizeof longrep);

    // Matches of exactly the minimum length, and just under it.
    roundtrip("min length matches", (const uint8_t *)"abcXabcYabYZabc", 15);

    // Data with no matches at all, so the distance alphabet stays empty.
    uint8_t distinct[200];
    for (int i = 0; i < 200; i++) distinct[i] = (uint8_t)i;
    roundtrip("no matches at all", distinct, sizeof distinct);
}

static void test_rejects(const char *name, const uint8_t *data, size_t len, const char *want) {
    write_file(TMP_IN, data, len);
    const char *error = "(none)";
    bool rejected = !huffman_decompress(TMP_IN, TMP_OUT, &error);
    char detail[128];
    snprintf(detail, sizeof detail, "%s", error);
    check(rejected && strstr(error, want) != NULL, name, detail);
}

int main(void) {
    roundtrip("empty", (const uint8_t *)"", 0);
    roundtrip("one byte", (const uint8_t *)"A", 1);
    roundtrip("two symbols", (const uint8_t *)"abababab", 8);
    roundtrip("embedded NULs", (const uint8_t *)"a\0b\0c", 5);

    static uint8_t same[5000];
    memset(same, 'z', sizeof same);
    roundtrip("single symbol x5000", same, sizeof same);

    uint8_t all[256];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
    roundtrip("all 256 byte values", all, sizeof all);

    static uint8_t random_bytes[300000];
    srand(7);
    for (size_t i = 0; i < sizeof random_bytes; i++) random_bytes[i] = (uint8_t)rand();
    roundtrip("random binary 300k", random_bytes, sizeof random_bytes);

    static uint8_t text[400000];
    const char *phrase = "the quick brown fox jumps over a lazy dog. ";
    for (size_t i = 0; i < sizeof text; i++) text[i] = (uint8_t)phrase[i % strlen(phrase)];
    roundtrip("english-ish text 400k", text, sizeof text);

    test_length_limiting();
    test_lz77_shapes();

    test_rejects("rejects foreign file", (const uint8_t *)"NOPE12345678901", 15, "not a huffman");
    test_rejects("rejects short header", (const uint8_t *)"HUFF\x03\x00", 6, "truncated header");

    uint8_t wrong_version[ARCHIVE_HEADER_LEN] = {'H', 'U', 'F', 'F', 2};
    test_rejects("rejects old version", wrong_version, sizeof wrong_version, "unsupported");

    // Truncate a real archive partway through its compressed data.
    write_file(TMP_IN, text, 4000);
    const char *error = NULL;
    huffman_compress(TMP_IN, TMP_HUF, &error);
    uint8_t truncated[64];
    FILE *file = fopen(TMP_HUF, "rb");
    size_t n = fread(truncated, 1, sizeof truncated, file);
    fclose(file);
    test_rejects("rejects truncated data", truncated, n, "truncated or corrupt");

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all passed", failures,
           failures == 1 ? "" : "s");
    return failures != 0;
}
