/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_DOS_CLI_H
#define DOSBASE_DEVTOOLS_DOS_CLI_H

#include "dosbase/devtools/core/c_compat.h"

int open(const char *path, int oflag);
int creat(const char *path, int mode);
int close(int fd);
int read(int fd, void *buf, unsigned int count);
int write(int fd, const void *buf, unsigned int count);
long lseek(int fd, long offset, int origin);
long filelength(int fd);
int printf(const char *fmt, ...);

static int dos_read_all(const char *path, ck_u8 **out_buf, ck_u32 *out_size) {
    int fd;
    long flen;
    ck_u32 sz;
    ck_u8 *buf;
    ck_u32 got = 0;
    int n;

    fd = open(path, 0);
    if (fd < 0) { printf("[open fail %s]\n", path); return -1; }
    flen = filelength(fd);
    if (flen < 0L || flen > 65000L) {
        printf("[flen fail %s %ld]\n", path, flen);
        close(fd);
        return -1;
    }
    lseek(fd, 0L, 0);
    sz = (ck_u32)flen;
    buf = (ck_u8 *)malloc(sz > 0 ? (unsigned int)sz : 1u);
    if (!buf) {
        printf("[malloc fail %s %u]\n", path, (unsigned int)sz);
        close(fd);
        return -1;
    }
    while (got < sz) {
        unsigned int chunk = (sz - got > 16384u) ? 16384u : (unsigned int)(sz - got);
        n = read(fd, buf + got, chunk);
        if (n <= 0) break;
        got += (ck_u32)n;
    }
    close(fd);
    if (got != sz) {
        free(buf);
        return -1;
    }
    *out_buf = buf;
    *out_size = sz;
    return 0;
}

static int dos_write_all(const char *path, const ck_u8 *buf, ck_u32 size) {
    int fd;
    ck_u32 off = 0;
    unsigned int chunk;
    int n;

    fd = creat(path, 0);
    if (fd < 0) return -1;
    while (off < size) {
        chunk = (size - off > 32768u) ? 32768u : (unsigned int)(size - off);
        n = write(fd, buf + off, chunk);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        off += (ck_u32)n;
    }
    close(fd);
    return 0;
}

#endif /* DOSBASE_DEVTOOLS_DOS_CLI_H */
