/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_cli.h"
#include "dosbase/devtools/core/convert_core.h"

int main(int argc, char **argv) {
    ck_u8 *in_data = NULL;
    ck_u32 in_size = 0;
    ck_u8 *out_data = NULL;
    ck_u32 out_size = 0;
    ck_error err;

    if (argc != 3) {
        printf("usage: ckconv <input.exe> <output.com>\n");
        return 2;
    }
    if (dos_read_all(argv[1], &in_data, &in_size) != 0) {
        printf("ckconv: cannot read %s\n", argv[1]);
        return 1;
    }
    if (ck_convert_exe_to_com(in_data, in_size, &out_data, &out_size, &err) != 0) {
        printf("ckconv: %s\n", err.message);
        free(in_data);
        return 1;
    }
    free(in_data);
    if (dos_write_all(argv[2], out_data, out_size) != 0) {
        printf("ckconv: cannot write %s\n", argv[2]);
        free(out_data);
        return 1;
    }
    free(out_data);
    return 0;
}
