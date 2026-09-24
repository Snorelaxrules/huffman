#include "huffman.h"

// Orders by frequency, breaking ties by symbol so the tree is deterministic.
static int huffman_cmp(const void *a, const void *b) {
    const TreeNode *node_a = a;
    const TreeNode *node_b = b;
    if (node_a->frequency < node_b->frequency) return -1;
    if (node_a->frequency > node_b->frequency) return 1;
    return (int)node_a->symbol - (int)node_b->symbol;
}

static TreeNode *new_node(uint16_t symbol, uint64_t frequency, TreeNode *left, TreeNode *right) {
    TreeNode *node = malloc(sizeof(TreeNode));
    node->symbol = symbol;
    node->frequency = frequency;
    node->left = left;
    node->right = right;
    return node;
}

TreeNode *make_huffman_tree(const uint64_t *freqs, int nsymbols) {
    PQNode *pq = NULL;
    for (int i = 0; i < nsymbols; i++) {
        if (freqs[i] > 0) {
            pq_enqueue(&pq, new_node((uint16_t)i, freqs[i], NULL, NULL), huffman_cmp);
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

        pq_enqueue(&pq, new_node(0, left->frequency + right->frequency, left, right), huffman_cmp);
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

static void collect_lengths(const TreeNode *node, uint8_t *lengths, int depth) {
    if (is_leaf(node)) {
        // A tree of one leaf has depth 0, but its symbol still needs a bit.
        lengths[node->symbol] = (uint8_t)(depth > 0 ? depth : 1);
        return;
    }

    collect_lengths(node->left, lengths, depth + 1);
    collect_lengths(node->right, lengths, depth + 1);
}

void get_code_lengths(TreeNode *root, uint8_t *lengths) {
    if (root == NULL) return;
    collect_lengths(root, lengths, 0);
}

void limit_code_lengths(uint8_t *lengths, int nsymbols) {
    // Kraft's inequality in fixed point: a code of length L claims
    // 2^(MAX_CODE_LENGTH - L) of the 2^MAX_CODE_LENGTH available windows, and a
    // valid prefix code claims no more than all of them.
    const uint32_t capacity = UINT32_C(1) << MAX_CODE_LENGTH;
    uint32_t claimed = 0;
    for (int i = 0; i < nsymbols; i++) {
        if (lengths[i] == 0) continue;
        if (lengths[i] > MAX_CODE_LENGTH) lengths[i] = MAX_CODE_LENGTH;
        claimed += capacity >> lengths[i];
    }

    // Clamping overfilled the code space. Lengthening the deepest symbol that
    // still has room frees the most space for the least compression lost.
    while (claimed > capacity) {
        int deepest = -1;
        for (int i = 0; i < nsymbols; i++) {
            if (lengths[i] == 0 || lengths[i] >= MAX_CODE_LENGTH) continue;
            if (deepest < 0 || lengths[i] > lengths[deepest]) deepest = i;
        }
        if (deepest < 0) break; // every symbol is already at the cap

        claimed -= capacity >> (lengths[deepest] + 1);
        lengths[deepest]++;
    }
}

void build_code_lengths(const uint64_t *freqs, int nsymbols, uint8_t *lengths) {
    memset(lengths, 0, (size_t)nsymbols);

    TreeNode *root = make_huffman_tree(freqs, nsymbols);
    if (root == NULL) return;

    get_code_lengths(root, lengths);
    destroy_huffman_tree(&root);
    limit_code_lengths(lengths, nsymbols);
}

void build_canonical_codes(CodeTable *a_table, const uint8_t *lengths, int nsymbols) {
    memset(a_table, 0, sizeof(*a_table));

    uint32_t code = 0;
    for (int length = 1; length <= MAX_CODE_LENGTH; length++) {
        for (int symbol = 0; symbol < nsymbols; symbol++) {
            if (lengths[symbol] != length) continue;
            a_table->codes[symbol] = (uint16_t)code++;
            a_table->lengths[symbol] = (uint8_t)length;
        }
        code <<= 1;
    }
}

void write_symbol(BitWriter *a_writer, const CodeTable *table, int symbol) {
    write_bits(a_writer, table->codes[symbol], table->lengths[symbol]);
}

// Lengths are 4 bits each; a zero is followed by a count of further zeros, so
// the long unused stretches of a sparse alphabet cost 12 bits in total.
#define LENGTH_RUN_MAX 255

void write_code_lengths(BitWriter *a_writer, const uint8_t *lengths, int nsymbols) {
    for (int i = 0; i < nsymbols; ) {
        write_bits(a_writer, lengths[i], 4);
        if (lengths[i] != 0) {
            i++;
            continue;
        }

        int run = 0;
        while (i + 1 + run < nsymbols && lengths[i + 1 + run] == 0 && run < LENGTH_RUN_MAX) run++;
        write_bits(a_writer, (uint64_t)run, 8);
        i += 1 + run;
    }
}

bool read_code_lengths(BitReader *a_reader, uint8_t *lengths, int nsymbols) {
    memset(lengths, 0, (size_t)nsymbols);

    for (int i = 0; i < nsymbols; ) {
        uint64_t length;
        if (!read_bits(a_reader, 4, &length)) return false;
        if (length != 0) {
            lengths[i++] = (uint8_t)length;
            continue;
        }

        uint64_t run;
        if (!read_bits(a_reader, 8, &run)) return false;
        i += 1 + (int)run;
        if (i > nsymbols) return false;
    }
    return true;
}

DecodeTable *decode_table_create(const uint8_t *lengths, int nsymbols) {
    CodeTable codes;
    build_canonical_codes(&codes, lengths, nsymbols);

    // calloc leaves unclaimed windows at length 0, which decode_symbol reads as
    // "no code matches" and reports as corrupt input.
    DecodeTable *table = calloc(1, sizeof(DecodeTable));
    if (table == NULL) return NULL;

    for (int symbol = 0; symbol < nsymbols; symbol++) {
        int length = codes.lengths[symbol];
        if (length == 0) continue;

        uint32_t span = UINT32_C(1) << (MAX_CODE_LENGTH - length);
        uint32_t start = (uint32_t)codes.codes[symbol] << (MAX_CODE_LENGTH - length);
        for (uint32_t i = 0; i < span; i++) {
            table->entries[start + i].symbol = (uint16_t)symbol;
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

bool decode_symbol(BitReader *a_reader, const DecodeTable *table, uint16_t *a_symbol) {
    DecodeEntry entry = table->entries[peek_bits(a_reader, MAX_CODE_LENGTH)];
    if (entry.length == 0) return false;
    if (!consume_bits(a_reader, entry.length)) return false;

    *a_symbol = entry.symbol;
    return true;
}
