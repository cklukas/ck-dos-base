/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_cli.h"
#include "dosbase/devtools/core/exe2bin_core.h"

static int parse_hex_u16(const char *s, ck_u16 *out) {
    ck_u32 val = 0;
    int any = 0;
    if (!s || !*s) return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        ck_u32 d;
        if (c >= '0' && c <= '9') d = (ck_u32)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (ck_u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (ck_u32)(c - 'A' + 10);
        else if ((c == 'h' || c == 'H') && *s == '\0' && any) break;
        else return -1;
        val = (val << 4) | d;
        if (val > 0xFFFFu) return -1;
        any = 1;
    }
    if (!any) return -1;
    *out = (ck_u16)val;
    return 0;
}

static void default_bin_name(const char *in_path, char *out_path) {
    const char *dot;
    const char *slash;
    ck_usize prefix_len;
    strcpy(out_path, in_path);
    slash = strrchr(out_path, '\\');
    dot = strrchr(out_path, '.');
    if (dot && (!slash || dot > slash)) {
        prefix_len = (ck_usize)(dot - out_path);
        out_path[prefix_len] = '\0';
    }
    strcat(out_path, ".BIN");
}

int main(int argc, char **argv) {
    const char *in_path;
    char out_buf[128];
    const char *out_path;
    int has_seg = 0;
    ck_u16 seg = 0;
    ck_u8 *in_data = NULL;
    ck_u32 in_size = 0;
    ck_exe2bin_out res;
    ck_error err;

    if (argc < 2 || argc > 4) {
        printf("usage: ckexe2bn <input.exe> [output.bin] [seg_hex]\n");
        return 2;
    }
    in_path = argv[1];
    if (argc == 2) {
        default_bin_name(in_path, out_buf);
        out_path = out_buf;
    } else if (argc == 3) {
        if (parse_hex_u16(argv[2], &seg) == 0) {
            has_seg = 1;
            default_bin_name(in_path, out_buf);
            out_path = out_buf;
        } else {
            out_path = argv[2];
        }
    } else {
        out_path = argv[2];
        if (parse_hex_u16(argv[3], &seg) != 0) {
            printf("ckexe2bn: invalid hex segment: %s\n", argv[3]);
            return 2;
        }
        has_seg = 1;
    }

    if (dos_read_all(in_path, &in_data, &in_size) != 0) {
        printf("ckexe2bn: cannot read %s\n", in_path);
        return 1;
    }
    if (ck_exe2bin(in_data, in_size, has_seg, seg, &res, &err) != 0) {
        printf("ckexe2bn: %s\n", err.message);
        free(in_data);
        return 1;
    }
    free(in_data);
    if (dos_write_all(out_path, res.bytes, res.size) != 0) {
        printf("ckexe2bn: cannot write %s\n", out_path);
        free(res.bytes);
        return 1;
    }
    free(res.bytes);
    return 0;
}
