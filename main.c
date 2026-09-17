#include "huffman.h"

#define DEFAULT_SUFFIX ".huf"

static void usage(FILE *file) {
    fprintf(file,
            "usage: huff c <input> [output]   compress   (default output: <input>%s)\n"
            "       huff d <input> [output]   decompress (default output: <input> minus %s)\n",
            DEFAULT_SUFFIX, DEFAULT_SUFFIX);
}

static bool ends_with_suffix(const char *path) {
    size_t len = strlen(path), suffix = strlen(DEFAULT_SUFFIX);
    return len > suffix && strcmp(path + len - suffix, DEFAULT_SUFFIX) == 0;
}

// Derives the output path from the input when the caller did not give one.
static char *default_output(const char *in_path, bool compressing) {
    if (compressing) {
        char *out = malloc(strlen(in_path) + strlen(DEFAULT_SUFFIX) + 1);
        if (out != NULL) {
            strcpy(out, in_path);
            strcat(out, DEFAULT_SUFFIX);
        }
        return out;
    }

    if (!ends_with_suffix(in_path)) return NULL;
    size_t len = strlen(in_path) - strlen(DEFAULT_SUFFIX);
    char *out = malloc(len + 1);
    if (out != NULL) {
        memcpy(out, in_path, len);
        out[len] = '\0';
    }
    return out;
}

static long file_size(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return -1;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size;
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4 || strlen(argv[1]) != 1 ||
        (argv[1][0] != 'c' && argv[1][0] != 'd')) {
        usage(stderr);
        return 2;
    }

    bool compressing = argv[1][0] == 'c';
    const char *in_path = argv[2];

    char *allocated = NULL;
    const char *out_path = argv[3];
    if (argc == 3) {
        allocated = default_output(in_path, compressing);
        if (allocated == NULL) {
            fprintf(stderr, "huff: cannot infer an output name for '%s', pass one\n", in_path);
            return 2;
        }
        out_path = allocated;
    }

    const char *error = NULL;
    bool ok = compressing ? huffman_compress(in_path, out_path, &error)
                          : huffman_decompress(in_path, out_path, &error);
    if (!ok) {
        fprintf(stderr, "huff: %s: %s\n", in_path, error);
        free(allocated);
        return 1;
    }

    long in_size = file_size(in_path), out_size = file_size(out_path);
    if (compressing && in_size > 0 && out_size >= 0) {
        fprintf(stderr, "%s: %.1f%% of original (%ld -> %ld bytes)\n", out_path,
                100.0 * (double)out_size / (double)in_size, in_size, out_size);
    }

    free(allocated);
    return 0;
}
