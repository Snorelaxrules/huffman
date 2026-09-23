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

void write_coding_table(TreeNode *root, BitWriter *a_writer) {
    if (root == NULL) return;

    if (is_leaf(root)) {
        write_bits(a_writer, 1, 1);
        write_bits(a_writer, root->character, 8);
    }
    else {
        write_coding_table(root->left, a_writer);
        write_coding_table(root->right, a_writer);
        write_bits(a_writer, 0, 1);
    }
}

// Frees a stack of partially built subtrees left behind by a failed read.
static void destroy_node_stack(PQNode **a_stack) {
    while (*a_stack != NULL) {
        PQNode *node = stack_pop(a_stack);
        TreeNode *subtree = node->a_value;
        destroy_huffman_tree(&subtree);
        free(node);
    }
}

TreeNode *read_coding_table(BitReader *a_reader, uint16_t nsymbols) {
    if (nsymbols == 0) return NULL;

    // The table is post-order, so it replays as a stack machine: leaves are
    // pushed, and a 0 pops the two subtrees it joins. It is complete once every
    // declared leaf has been seen and the stack has collapsed to a single root.
    PQNode *stack = NULL;
    uint16_t leaves = 0;
    while (leaves < nsymbols || stack == NULL || stack->next != NULL) {
        uint64_t bit;
        if (!read_bits(a_reader, 1, &bit)) goto fail;

        if (bit == 1) {
            uint64_t character;
            if (leaves == nsymbols) goto fail;
            if (!read_bits(a_reader, 8, &character)) goto fail;
            stack_push(&stack, new_node((uchar)character, 0, NULL, NULL));
            leaves++;
        }
        else {
            // The right subtree was written second, so it pops first.
            PQNode *right = stack_pop(&stack);
            PQNode *left = stack_pop(&stack);
            if (right == NULL || left == NULL) {
                free(right);
                free(left);
                goto fail;
            }
            stack_push(&stack, new_node('\0', 0, left->a_value, right->a_value));
            free(right);
            free(left);
        }
    }

    PQNode *top = stack_pop(&stack);
    TreeNode *root = top->a_value;
    free(top);
    return root;

fail:
    destroy_node_stack(&stack);
    return NULL;
}

// Sets or clears the bit at `index` in a packed MSB-first bit string.
static void set_bit(uint8_t *code, int index, int value) {
    uint8_t mask = (uint8_t)(1 << (7 - (index & 7)));
    if (value) code[index >> 3] |= mask;
    else code[index >> 3] &= (uint8_t)~mask;
}

static int get_bit(const uint8_t *code, int index) {
    return (code[index >> 3] >> (7 - (index & 7))) & 1;
}

static void build_codes(TreeNode *node, CodeTable *a_table, uint8_t *code, int depth) {
    if (is_leaf(node)) {
        uchar c = node->character;
        if (depth == 0) {
            // A tree of one leaf still needs a code, so give it a single 0 bit.
            set_bit(a_table->codes[c], 0, 0);
            a_table->lengths[c] = 1;
        }
        else {
            memcpy(a_table->codes[c], code, (size_t)(depth + 7) / 8);
            a_table->lengths[c] = (uint16_t)depth;
        }
        return;
    }

    set_bit(code, depth, 0);
    build_codes(node->left, a_table, code, depth + 1);
    set_bit(code, depth, 1);
    build_codes(node->right, a_table, code, depth + 1);
}

void build_code_table(CodeTable *a_table, TreeNode *root) {
    memset(a_table, 0, sizeof(*a_table));
    if (root == NULL) return;

    uint8_t code[32] = {0};
    build_codes(root, a_table, code, 0);
}

void write_compressed(BitWriter *a_writer, const uint8_t *uncompressed_bytes, uint64_t len,
                      TreeNode *root) {
    if (root == NULL) return;

    CodeTable table;
    build_code_table(&table, root);

    for (uint64_t i = 0; i < len; i++) {
        const uint8_t *code = table.codes[uncompressed_bytes[i]];
        int nbits = table.lengths[uncompressed_bytes[i]];

        if (nbits <= BIT_MAX_RUN) {
            // Assemble the code from whole bytes and shift off the bits past
            // its length, so the common case is one write_bits call.
            int nbytes = (nbits + 7) / 8;
            uint64_t bits = 0;
            for (int b = 0; b < nbytes; b++) {
                bits = (bits << 8) | code[b];
            }
            write_bits(a_writer, bits >> (nbytes * 8 - nbits), nbits);
        }
        else {
            // Only reachable for pathological frequency distributions.
            for (int b = 0; b < nbits; b++) {
                write_bits(a_writer, (uint64_t)get_bit(code, b), 1);
            }
        }
    }
}

bool read_compressed(BitReader *a_reader, FILE *file, TreeNode *root, uint64_t len) {
    if (root == NULL) return len == 0;

    for (uint64_t i = 0; i < len; i++) {
        TreeNode *node = root;
        while (!is_leaf(node)) {
            uint64_t bit;
            if (!read_bits(a_reader, 1, &bit)) return false;
            node = bit ? node->right : node->left;
            if (node == NULL) return false;
        }

        // A one-leaf tree encodes each byte as a single bit that carries no
        // information, so consume it here rather than walking for it above.
        if (node == root && !consume_bits(a_reader, 1)) return false;

        fputc(node->character, file);
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

    TreeNode *root = make_huffman_tree(freqs);
    if (root != NULL) {
        BitWriter writer;
        bit_writer_init(&writer, out);
        write_coding_table(root, &writer);
        write_compressed(&writer, bytes, length, root);
        bool flushed = bit_writer_flush(&writer);
        destroy_huffman_tree(&root);
        if (!flushed) {
            *a_error = "failed to write compressed data";
            free(bytes);
            fclose(out);
            return false;
        }
    }

    free(bytes);

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

        TreeNode *root = read_coding_table(&reader, (uint16_t)nsymbols);
        if (root == NULL) {
            *a_error = "corrupt coding table";
            ok = false;
        }
        else {
            ok = read_compressed(&reader, out, root, length);
            if (!ok) *a_error = "truncated compressed data";
            destroy_huffman_tree(&root);
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
