#include "huffman.h"

bool calc_frequencies(Frequencies freqs, const char *path, const char **a_error) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        *a_error = strerror(errno);
        return false;
    }

    int x;
    while ((x = fgetc(file)) != EOF) {
        freqs[(uchar)x]++;
    }

    fclose(file);
    return true;
}

// Orders by frequency, breaking ties by character so the tree is deterministic.
static int huffman_cmp(const void *a, const void *b) {
    const TreeNode *node_a = a;
    const TreeNode *node_b = b;
    if (node_a->frequency < node_b->frequency) return -1;
    if (node_a->frequency > node_b->frequency) return 1;
    return (int)node_a->character - (int)node_b->character;
}

static TreeNode *new_node(uchar character, size_t frequency, TreeNode *left, TreeNode *right) {
    TreeNode *node = malloc(sizeof(TreeNode));
    node->character = character;
    node->frequency = frequency;
    node->left = left;
    node->right = right;
    return node;
}

TreeNode *make_huffman_tree(Frequencies freq) {
    PQNode *pq = NULL;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            pq_enqueue(&pq, new_node((uchar)i, freq[i], NULL, NULL), huffman_cmp);
        }
    }

    if (pq == NULL) return NULL;

    // Repeatedly merge the two lowest-frequency subtrees until one remains.
    while (pq->next != NULL) {
        PQNode *node1 = pq_dequeue(&pq);
        PQNode *node2 = pq_dequeue(&pq);
        TreeNode *left = node1->a_value;
        TreeNode *right = node2->a_value;
        free(node1);
        free(node2);

        pq_enqueue(&pq, new_node('\0', left->frequency + right->frequency, left, right),
                   huffman_cmp);
    }

    PQNode *pq_root = pq_dequeue(&pq);
    TreeNode *root = pq_root->a_value;
    free(pq_root);
    return root;
}

void destroy_huffman_tree(TreeNode **a_root) {
    if (a_root == NULL || *a_root == NULL) return;

    TreeNode *root = *a_root;
    destroy_huffman_tree(&root->left);
    destroy_huffman_tree(&root->right);

    free(root);
    *a_root = NULL;
}

static bool is_leaf(const TreeNode *node) {
    return node->left == NULL && node->right == NULL;
}

static void collect_lengths(const TreeNode *node, uint8_t lengths[256], int depth) {
    if (is_leaf(node)) {
        // A tree of one leaf has depth 0, but its symbol still needs a bit.
        lengths[node->character] = (uint8_t)(depth > 0 ? depth : 1);
        return;
    }

    collect_lengths(node->left, lengths, depth + 1);
    collect_lengths(node->right, lengths, depth + 1);
}

void get_code_lengths(TreeNode *root, uint8_t lengths[256]) {
    if (root == NULL) return;
    collect_lengths(root, lengths, 0);
}

void limit_code_lengths(uint8_t lengths[256]) {
    // Kraft's inequality in fixed point: a code of length L claims
    // 2^(MAX_CODE_LENGTH - L) of the 2^MAX_CODE_LENGTH available windows, and a
    // valid prefix code claims no more than all of them.
    const uint32_t capacity = UINT32_C(1) << MAX_CODE_LENGTH;
    uint32_t claimed = 0;
    for (int i = 0; i < 256; i++) {
        if (lengths[i] == 0) continue;
        if (lengths[i] > MAX_CODE_LENGTH) lengths[i] = MAX_CODE_LENGTH;
        claimed += capacity >> lengths[i];
    }

    // Clamping overfilled the code space. Lengthening the deepest symbol that
    // still has room frees the most space for the least compression lost.
    while (claimed > capacity) {
        int deepest = -1;
        for (int i = 0; i < 256; i++) {
            if (lengths[i] == 0 || lengths[i] >= MAX_CODE_LENGTH) continue;
            if (deepest < 0 || lengths[i] > lengths[deepest]) deepest = i;
        }
        if (deepest < 0) break; // every symbol is already at the cap

        claimed -= capacity >> (lengths[deepest] + 1);
        lengths[deepest]++;
    }
}

void build_canonical_codes(CodeTable *a_table, const uint8_t lengths[256]) {
    memset(a_table, 0, sizeof(*a_table));

    uint32_t code = 0;
    for (int length = 1; length <= MAX_CODE_LENGTH; length++) {
        for (int symbol = 0; symbol < 256; symbol++) {
            if (lengths[symbol] != length) continue;
            a_table->codes[symbol] = (uint16_t)code++;
            a_table->lengths[symbol] = (uint8_t)length;
        }
        code <<= 1;
    }
}

// Lengths are 4 bits each; a zero is followed by a count of further zeros, so
// the long unused stretches of a sparse alphabet cost 12 bits in total.
#define LENGTH_RUN_MAX 255

void write_code_lengths(BitWriter *a_writer, const uint8_t lengths[256]) {
    for (int i = 0; i < 256; ) {
        write_bits(a_writer, lengths[i], 4);
        if (lengths[i] != 0) {
            i++;
            continue;
        }

        int run = 0;
        while (i + 1 + run < 256 && lengths[i + 1 + run] == 0 && run < LENGTH_RUN_MAX) run++;
        write_bits(a_writer, (uint64_t)run, 8);
        i += 1 + run;
    }
}

bool read_code_lengths(BitReader *a_reader, uint8_t lengths[256]) {
    memset(lengths, 0, 256);

    for (int i = 0; i < 256; ) {
        uint64_t length;
        if (!read_bits(a_reader, 4, &length)) return false;
        if (length != 0) {
            lengths[i++] = (uint8_t)length;
            continue;
        }

        uint64_t run;
        if (!read_bits(a_reader, 8, &run)) return false;
        i += 1 + (int)run;
        if (i > 256) return false;
    }
    return true;
}

DecodeTable *decode_table_create(const uint8_t lengths[256]) {
    CodeTable codes;
    build_canonical_codes(&codes, lengths);

    // calloc leaves unclaimed windows at length 0, which read_compressed reads
    // as "no code matches" and reports as corrupt input.
    DecodeTable *table = calloc(1, sizeof(DecodeTable));
    if (table == NULL) return NULL;

    for (int symbol = 0; symbol < 256; symbol++) {
        int length = codes.lengths[symbol];
        if (length == 0) continue;

        uint32_t span = UINT32_C(1) << (MAX_CODE_LENGTH - length);
        uint32_t start = (uint32_t)codes.codes[symbol] << (MAX_CODE_LENGTH - length);
        for (uint32_t i = 0; i < span; i++) {
            table->entries[start + i].symbol = (uint8_t)symbol;
            table->entries[start + i].length = (uint8_t)length;
        }
    }
    return table;
}

void decode_table_destroy(DecodeTable **a_table) {
    if (a_table == NULL) return;
    free(*a_table);
    *a_table = NULL;
}

void write_compressed(BitWriter *a_writer, const uint8_t *uncompressed_bytes, uint64_t len,
                      const CodeTable *table) {
    for (uint64_t i = 0; i < len; i++) {
        uchar c = uncompressed_bytes[i];
        write_bits(a_writer, table->codes[c], table->lengths[c]);
    }
}

bool read_compressed(BitReader *a_reader, FILE *file, const DecodeTable *table, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        DecodeEntry entry = table->entries[peek_bits(a_reader, MAX_CODE_LENGTH)];
        if (entry.length == 0) return false;
        if (!consume_bits(a_reader, entry.length)) return false;
        fputc(entry.symbol, file);
    }
    return true;
}

static void write_be(FILE *file, uint64_t value, int nbytes) {
    for (int i = nbytes - 1; i >= 0; i--) {
        fputc((int)((value >> (i * 8)) & 0xff), file);
    }
}

static bool read_be(FILE *file, uint64_t *a_value, int nbytes) {
    uint64_t value = 0;
    for (int i = 0; i < nbytes; i++) {
        int c = fgetc(file);
        if (c == EOF) return false;
        value = (value << 8) | (uint64_t)c;
    }
    *a_value = value;
    return true;
}

// Reads the whole file into a fresh buffer of `len` bytes.
static uint8_t *read_file(const char *path, uint64_t len, const char **a_error) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        *a_error = strerror(errno);
        return NULL;
    }

    uint8_t *bytes = malloc(len);
    if (bytes == NULL || fread(bytes, 1, len, file) != len) {
        *a_error = bytes == NULL ? "out of memory" : "input file changed while reading";
        free(bytes);
        fclose(file);
        return NULL;
    }

    fclose(file);
    return bytes;
}

bool huffman_compress(const char *in_path, const char *out_path, const char **a_error) {
    Frequencies freqs = {0};
    if (!calc_frequencies(freqs, in_path, a_error)) return false;

    uint64_t length = 0;
    uint16_t nsymbols = 0;
    for (int i = 0; i < 256; i++) {
        if (freqs[i] > 0) {
            length += freqs[i];
            nsymbols++;
        }
    }

    uint8_t *bytes = NULL;
    if (length > 0) {
        bytes = read_file(in_path, length, a_error);
        if (bytes == NULL) return false;
    }

    // The tree exists only to produce code lengths; the canonical codes are
    // derived from those, so it is discarded before anything is written.
    uint8_t lengths[256] = {0};
    TreeNode *root = make_huffman_tree(freqs);
    get_code_lengths(root, lengths);
    destroy_huffman_tree(&root);
    limit_code_lengths(lengths);

    CodeTable table;
    build_canonical_codes(&table, lengths);

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        *a_error = strerror(errno);
        free(bytes);
        return false;
    }

    fwrite(HUFFMAN_MAGIC, 1, HUFFMAN_MAGIC_LEN, out);
    fputc(HUFFMAN_VERSION, out);
    write_be(out, nsymbols, 2);
    write_be(out, length, 8);

    bool flushed = true;
    if (nsymbols > 0) {
        BitWriter writer;
        bit_writer_init(&writer, out);
        write_code_lengths(&writer, lengths);
        write_compressed(&writer, bytes, length, &table);
        flushed = bit_writer_flush(&writer);
    }

    free(bytes);

    if (!flushed) {
        *a_error = "failed to write compressed data";
        fclose(out);
        return false;
    }
    if (fclose(out) != 0) {
        *a_error = strerror(errno);
        return false;
    }
    return true;
}

bool huffman_decompress(const char *in_path, const char *out_path, const char **a_error) {
    FILE *in = fopen(in_path, "rb");
    if (in == NULL) {
        *a_error = strerror(errno);
        return false;
    }

    char magic[HUFFMAN_MAGIC_LEN];
    uint64_t version, nsymbols, length;
    if (fread(magic, 1, sizeof(magic), in) != sizeof(magic) ||
        memcmp(magic, HUFFMAN_MAGIC, sizeof(magic)) != 0) {
        *a_error = "not a huffman file";
        fclose(in);
        return false;
    }
    if (!read_be(in, &version, 1) || !read_be(in, &nsymbols, 2) || !read_be(in, &length, 8)) {
        *a_error = "truncated header";
        fclose(in);
        return false;
    }
    if (version != HUFFMAN_VERSION) {
        *a_error = "unsupported format version";
        fclose(in);
        return false;
    }

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        *a_error = strerror(errno);
        fclose(in);
        return false;
    }

    bool ok = true;
    if (nsymbols > 0) {
        BitReader reader;
        bit_reader_init(&reader, in);

        uint8_t lengths[256];
        DecodeTable *table = NULL;
        if (!read_code_lengths(&reader, lengths)) {
            *a_error = "corrupt code lengths";
            ok = false;
        }
        else if ((table = decode_table_create(lengths)) == NULL) {
            *a_error = "out of memory";
            ok = false;
        }
        else {
            ok = read_compressed(&reader, out, table, length);
            if (!ok) *a_error = "truncated or corrupt compressed data";
            decode_table_destroy(&table);
        }
    }
    else if (length != 0) {
        *a_error = "header declares data but no symbols";
        ok = false;
    }

    fclose(in);
    if (fclose(out) != 0 && ok) {
        *a_error = strerror(errno);
        ok = false;
    }
    return ok;
}
