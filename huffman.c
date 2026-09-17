#include "huffman.h"
#include "memcheck.h"

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

TreeNode *make_huffman_tree(Frequencies freq) {
    PQNode *pq = NULL;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;

        TreeNode *leaf = my_malloc(sizeof(TreeNode));
        leaf->character = (uchar)i;
        leaf->frequency = freq[i];
        leaf->left = NULL;
        leaf->right = NULL;
        pq_enqueue(&pq, leaf, huffman_cmp);
    }

    if (pq == NULL) return NULL;

    // Repeatedly merge the two lowest-frequency subtrees until one remains.
    while (pq->next != NULL) {
        PQNode *node1 = pq_dequeue(&pq);
        PQNode *node2 = pq_dequeue(&pq);
        TreeNode *left = node1->a_value;
        TreeNode *right = node2->a_value;
        my_free(node1);
        my_free(node2);

        TreeNode *merge = my_malloc(sizeof(TreeNode));
        merge->character = '\0';
        merge->frequency = left->frequency + right->frequency;
        merge->left = left;
        merge->right = right;
        pq_enqueue(&pq, merge, huffman_cmp);
    }

    PQNode *pq_root = pq_dequeue(&pq);
    TreeNode *root = pq_root->a_value;
    my_free(pq_root);
    return root;
}

void destroy_huffman_tree(TreeNode **a_root) {
    if (a_root == NULL || *a_root == NULL) return;

    TreeNode *root = *a_root;
    destroy_huffman_tree(&root->left);
    destroy_huffman_tree(&root->right);

    my_free(root);
    *a_root = NULL;
}

// Post-order: leaves emit 1 followed by their byte, interior nodes emit 0.
void write_coding_table(TreeNode *root, BitWriter *a_writer) {
    if (root == NULL) return;

    if (root->left == NULL && root->right == NULL) {
        write_bits(a_writer, 1, 1);
        write_bits(a_writer, root->character, 8);
    }
    else {
        write_coding_table(root->left, a_writer);
        write_coding_table(root->right, a_writer);
        write_bits(a_writer, 0, 1);
    }
}

// One bit per element; a code is at most 255 bits long.
static uint8_t code_bits[256][256];
static int code_len[256];

static void build_codes(TreeNode *root, uint8_t *path, int depth) {
    if (root->left == NULL && root->right == NULL) {
        uchar c = root->character;
        memcpy(code_bits[c], path, depth);
        // A tree with a single leaf still needs a one-bit code for it.
        code_len[c] = depth > 0 ? depth : 1;
        return;
    }

    path[depth] = 0;
    build_codes(root->left, path, depth + 1);
    path[depth] = 1;
    build_codes(root->right, path, depth + 1);
}

void write_compressed(BitWriter *a_writer, uint8_t *uncompressed_bytes, TreeNode *root) {
    if (root == NULL) return;

    uint8_t path[256];
    memset(code_len, 0, sizeof(code_len));
    build_codes(root, path, 0);

    for (int i = 0; uncompressed_bytes[i] != '\0'; i++) {
        uint8_t *bits = code_bits[uncompressed_bytes[i]];
        int len = code_len[uncompressed_bytes[i]];

        // write_bits takes at most 8 bits at a time.
        for (int written = 0; written < len; ) {
            int chunk = len - written > 8 ? 8 : len - written;
            uint8_t byte = 0;
            for (int j = 0; j < chunk; j++) {
                byte = (byte << 1) | bits[written + j];
            }
            write_bits(a_writer, byte, chunk);
            written += chunk;
        }
    }
}
