/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_cli.h"
#include "dosbase/devtools/core/exefix_core.h"

static int parse_hex_opt(const char *s, ck_u16 *out) {
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
        else return -1;
        val = (val << 4) | d;
        if (val > 0xFFFFu) return -1;
        any = 1;
    }
    if (!any) return -1;
    *out = (ck_u16)val;
    return 0;
}

int main(int argc, char **argv) {
    const char *path = NULL;
    ck_exefix_opts opts;
    ck_exefix_info info;
    ck_error err;
    int i;
    int fd;
    long flen;
    ck_u8 hdr[64];
    int n;

    opts.has_min_alloc = 0;
    opts.min_alloc = 0;
    opts.has_max_alloc = 0;
    opts.max_alloc = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmpi(argv[i], "/MIN") == 0 && i + 1 < argc) {
            if (parse_hex_opt(argv[++i], &opts.min_alloc) != 0) return 2;
            opts.has_min_alloc = 1;
        } else if (strcmpi(argv[i], "/MAX") == 0 && i + 1 < argc) {
            if (parse_hex_opt(argv[++i], &opts.max_alloc) != 0) return 2;
            opts.has_max_alloc = 1;
        } else if (!path) {
            path = argv[i];
        } else if (!opts.has_max_alloc) {
            if (parse_hex_opt(argv[i], &opts.max_alloc) != 0) return 2;
            opts.has_max_alloc = 1;
        } else if (!opts.has_min_alloc) {
            if (parse_hex_opt(argv[i], &opts.min_alloc) != 0) return 2;
            opts.has_min_alloc = 1;
        } else {
            printf("usage: ckexefix <file.exe> [<max-alloc> [<min-alloc>]]\n");
            return 2;
        }
    }
    if (!path) {
        printf("usage: ckexefix <file.exe> [/MIN hex] [/MAX hex]\n");
        return 2;
    }

    fd = open(path, 2);
    if (fd < 0) {
        printf("ckexefix: cannot open %s\n", path);
        return 1;
    }
    flen = filelength(fd);
    lseek(fd, 0L, 0);
    n = read(fd, hdr, sizeof(hdr));
    if (flen < 28L || n < 28) {
        close(fd);
        printf("ckexefix: truncated MZ header\n");
        return 1;
    }
    if (ck_exefix_patch(hdr, (ck_u32)n, (ck_u32)flen, &opts, &info, &err) != 0) {
        close(fd);
        printf("ckexefix: %s\n", err.message);
        return 1;
    }
    lseek(fd, 0L, 0);
    write(fd, hdr, 28u);
    close(fd);
    return 0;
}
