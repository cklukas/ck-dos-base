/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_cli.h"
#include "dosbase/devtools/core/compress_core.h"

int main(int argc, char **argv) {
    int decompress = 0;
    const char *in_path = NULL;
    const char *out_path = NULL;
    ck_u8 *in_data = NULL;
    ck_u32 in_size = 0;
    ck_u8 *out_data = NULL;
    ck_u32 out_size = 0;
    ck_error err;
    int rc;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmpi(argv[i], "-d") == 0 || strcmpi(argv[i], "/d") == 0) {
            decompress = 1;
        } else if (!in_path) {
            in_path = argv[i];
        } else if (!out_path) {
            out_path = argv[i];
        } else {
            printf("usage: ckcmprss [-d] <input> <output>\n");
            return 2;
        }
    }
    if (!in_path || !out_path) {
        printf("usage: ckcmprss [-d] <input> <output>\n");
        return 2;
    }
    if (dos_read_all(in_path, &in_data, &in_size) != 0) {
        printf("ckcmprss: cannot read %s\n", in_path);
        return 1;
    }
    if (decompress) {
        rc = ck_decompress_rle(in_data, in_size, &out_data, &out_size, &err);
    } else {
        rc = ck_compress_rle(in_data, in_size, &out_data, &out_size, &err);
    }
    free(in_data);
    if (rc != 0) {
        printf("ckcmprss: %s\n", err.message);
        return 1;
    }
    if (dos_write_all(out_path, out_data, out_size) != 0) {
        printf("ckcmprss: cannot write %s\n", out_path);
        free(out_data);
        return 1;
    }
    free(out_data);
    return 0;
}
