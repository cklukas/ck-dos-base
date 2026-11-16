/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_DOS_MSG_DB_H
#define DOSBASE_DEVTOOLS_DOS_MSG_DB_H

#include "dos_cli.h"
#include "dosbase/devtools/core/msgtools_core.h"

#define DOS_MSG_MAX_SECTIONS 64

typedef struct dos_msg_sec {
    char name[16];
    long offset;
} dos_msg_sec;

typedef struct dos_msg_db {
    int fd;
    dos_msg_sec sections[DOS_MSG_MAX_SECTIONS];
    int sec_count;
} dos_msg_db;

static int is_sec_hdr(const char *line, ck_usize len, char *out_name) {
    ck_usize i = 0;
    ck_usize name_end;
    ck_usize k;
    if (i >= len || line[i] < 'A' || line[i] > 'Z') return 0;
    while (i < len && ((line[i] >= 'A' && line[i] <= 'Z') || (line[i] >= '0' && line[i] <= '9'))) {
        ++i;
    }
    name_end = i;
    if (i == 0 || i >= len || line[i] != ' ') return 0;
    while (i < len && line[i] == ' ') ++i;
    if (len - i != 9u) return 0;
    for (k = 0; k < 4u; ++k) {
        char c = line[i + k];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    if (line[i + 4] != ' ') return 0;
    for (k = 5u; k < 9u; ++k) {
        if (line[i + k] < '0' || line[i + k] > '9') return 0;
    }
    if (out_name) {
        ck_usize n = name_end < 15u ? name_end : 15u;
        memcpy(out_name, line, (unsigned int)n);
        out_name[n] = '\0';
    }
    return 1;
}

static int is_msg_line(const char *line, ck_usize len, int *out_num, const char **out_text, ck_usize *out_tlen) {
    ck_usize k;
    int num = 0;
    if (len < 11u) return 0;
    for (k = 0; k < 4u; ++k) {
        if (line[k] < '0' || line[k] > '9') return 0;
        num = num * 10 + (int)(line[k] - '0');
    }
    if (line[4] != ' ' || line[6] != ' ') return 0;
    for (k = 7u; k < 11u; ++k) {
        if (line[k] < '0' || line[k] > '9') return 0;
    }
    if (len > 11u && line[11] != ' ') return 0;
    *out_num = num;
    if (len > 12u) {
        *out_text = line + 12;
        *out_tlen = len - 12u;
    } else {
        *out_text = "";
        *out_tlen = 0;
    }
    return 1;
}

typedef struct dos_line_reader {
    int fd;
    char buf[512];
    int pos;
    int len;
    long buf_file_off;
} dos_line_reader;

static void lr_init(dos_line_reader *r, int fd, long off) {
    r->fd = fd;
    r->pos = 0;
    r->len = 0;
    r->buf_file_off = off;
    lseek(fd, off, 0);
}

static int lr_next(dos_line_reader *r, char *out, ck_usize cap, ck_usize *out_len, long *out_off) {
    ck_usize n = 0;
    int saw_any = 0;
    if (out_off) *out_off = r->buf_file_off + (long)r->pos;
    for (;;) {
        if (r->pos >= r->len) {
            r->buf_file_off = r->buf_file_off + (long)r->len;
            r->len = read(r->fd, r->buf, sizeof(r->buf));
            r->pos = 0;
            if (r->len <= 0) {
                r->len = 0;
                if (!saw_any) return 0;
                break;
            }
        }
        {
            char c = r->buf[r->pos++];
            saw_any = 1;
            if (c == '\n') break;
            if (c != '\r' && n + 1u < cap) {
                out[n++] = c;
            }
        }
    }
    out[n] = '\0';
    if (out_len) *out_len = n;
    return 1;
}

static int dos_msg_db_open(dos_msg_db *db, const char *path) {
    dos_line_reader lr;
    char line[512];
    ck_usize llen;
    long off;
    char sname[16];

    db->fd = open(path, 0);
    if (db->fd < 0) return -1;
    db->sec_count = 0;
    lr_init(&lr, db->fd, 0L);
    /* skip level line */
    if (!lr_next(&lr, line, sizeof(line), &llen, &off)) return 0;
    while (lr_next(&lr, line, sizeof(line), &llen, &off)) {
        if (is_sec_hdr(line, llen, sname) && db->sec_count < DOS_MSG_MAX_SECTIONS) {
            strcpy(db->sections[db->sec_count].name, sname);
            db->sections[db->sec_count].offset = lr.buf_file_off + (long)lr.pos;
            db->sec_count++;
        }
    }
    return 0;
}

static void dos_msg_db_close(dos_msg_db *db) {
    if (db->fd >= 0) close(db->fd);
    db->fd = -1;
}

static int dos_msg_lookup(void *ctx, const char *component, int number, ck_msg_lines *out_lines) {
    dos_msg_db *db = (dos_msg_db *)ctx;
    int i;
    long sec_off = -1L;
    dos_line_reader lr;
    char line[512];
    ck_usize llen;
    int found = 0;
    int cap = 0;

    for (i = 0; i < db->sec_count; ++i) {
        if (strcmpi(db->sections[i].name, component) == 0) {
            sec_off = db->sections[i].offset;
            break;
        }
    }
    if (sec_off < 0L) return 1;

    lr_init(&lr, db->fd, sec_off);
    while (lr_next(&lr, line, sizeof(line), &llen, NULL)) {
        int mnum = -1;
        const char *tptr;
        ck_usize tlen = 0;
        int im;
        if (llen == 0) continue;
        if (is_sec_hdr(line, llen, NULL)) break;
        im = is_msg_line(line, llen, &mnum, &tptr, &tlen);
        if (im) {
            if (found) break;
            if (mnum == number) {
                found = 1;
                cap = 4;
                out_lines->lines = (char **)malloc((unsigned int)(cap * sizeof(char *)));
                if (!out_lines->lines) return -1;
                out_lines->lines[0] = (char *)malloc((unsigned int)tlen + 1u);
                if (!out_lines->lines[0]) { ck_msg_lines_free(out_lines); return -1; }
                if (tlen > 0) memcpy(out_lines->lines[0], tptr, (unsigned int)tlen);
                out_lines->lines[0][tlen] = '\0';
                out_lines->count = 1;
            }
        } else if (found && line[0] == '\t') {
            if ((int)out_lines->count == cap) {
                cap *= 2;
                out_lines->lines = (char **)realloc(out_lines->lines, (unsigned int)(cap * sizeof(char *)));
                if (!out_lines->lines) return -1;
            }
            out_lines->lines[out_lines->count] = (char *)malloc((unsigned int)llen + 1u);
            if (!out_lines->lines[out_lines->count]) { ck_msg_lines_free(out_lines); return -1; }
            memcpy(out_lines->lines[out_lines->count], line, (unsigned int)llen);
            out_lines->lines[out_lines->count][llen] = '\0';
            out_lines->count++;
        }
    }
    return found ? 0 : 1;
}

static void extract_base_name(const char *path, char *out, ck_usize cap) {
    const char *p = path;
    const char *slash1 = strrchr(path, '\\');
    const char *slash2 = strrchr(path, '/');
    const char *dot;
    ck_usize len;
    if (slash1 && (!slash2 || slash1 > slash2)) p = slash1 + 1;
    else if (slash2) p = slash2 + 1;
    dot = strrchr(p, '.');
    len = dot ? (ck_usize)(dot - p) : strlen(p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, (unsigned int)len);
    out[len] = '\0';
}

#endif /* DOSBASE_DEVTOOLS_DOS_MSG_DB_H */
