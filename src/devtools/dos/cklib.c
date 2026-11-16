/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_cli.h"
#include "dosbase/devtools/core/lib_core.h"

int sprintf(char *dst, const char *fmt, ...);

static void get_stem(const char *path, char *out, ck_usize cap) {
    const char *p = path;
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    const char *dot;
    ck_usize len;
    if (s1 && (!s2 || s1 > s2)) p = s1 + 1;
    else if (s2) p = s2 + 1;
    dot = strrchr(p, '.');
    len = dot ? (ck_usize)(dot - p) : strlen(p);
    if (len >= cap) len = cap - 1u;
    if (len > 0) memcpy(out, p, (unsigned int)len);
    out[len] = '\0';
}

static int has_ext(const char *path) {
    const char *p = path;
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    if (s1 && (!s2 || s1 > s2)) p = s1 + 1;
    else if (s2) p = s2 + 1;
    return strrchr(p, '.') != NULL;
}

int main(int argc, char **argv) {
    ck_lib_request req;
    ck_error err;
    ck_lib_member_in *members = NULL;
    ck_u8 **obj_bufs = NULL;
    char **mod_names = NULL;
    ck_usize i;
    ck_lib_build_out build;
    int rc = 1;

    memset(&req, 0, sizeof(req));
    memset(&build, 0, sizeof(build));
    if (argc < 2) {
        printf("usage: cklib @RESPONSE | cklib LIB [+-]OBJ...\n");
        return 2;
    }

    if (argc == 2 && argv[1][0] == '@') {
        ck_u8 *rtext = NULL;
        ck_u32 rsz = 0;
        if (dos_read_all(argv[1] + 1, &rtext, &rsz) != 0) {
            printf("cklib: cannot read %s\n", argv[1] + 1);
            return 1;
        }
        if (ck_lib_parse_response((const char *)rtext, rsz, &req, &err) != 0) {
            free(rtext);
            printf("cklib: %s\n", err.message);
            return 1;
        }
        free(rtext);
        if (!req.library || !req.library[0] || req.addition_count == 0) {
            ck_lib_request_free(&req);
            printf("cklib: response file names no library or no members\n");
            return 1;
        }
    } else {
        char joined[512];
        ck_usize pos = 0;
        ck_usize len;
        ck_usize cap = 8u;

        joined[0] = '\0';
        for (i = 1; i < (ck_usize)argc; ++i) {
            if (i > 1u && pos + 1u < sizeof(joined)) joined[pos++] = ' ';
            {
                ck_usize alen = strlen(argv[i]);
                if (pos + alen < sizeof(joined)) {
                    memcpy(joined + pos, argv[i], (unsigned int)alen);
                    pos += alen;
                }
            }
        }
        joined[pos] = '\0';
        len = pos;
        for (pos = 0; pos < len; ++pos) {
            if (joined[pos] == ',' || joined[pos] == ';') { len = pos; break; }
        }
        pos = 0;
        while (pos < len && (joined[pos] == ' ' || joined[pos] == '\t')) ++pos;
        {
            ck_usize start = pos;
            char lbuf[128];
            ck_usize llen;
            while (pos < len && joined[pos] != ' ' && joined[pos] != '\t' &&
                   joined[pos] != '+' && joined[pos] != '-') ++pos;
            llen = pos - start;
            if (llen >= sizeof(lbuf) - 5u) llen = sizeof(lbuf) - 5u;
            memcpy(lbuf, joined + start, (unsigned int)llen);
            lbuf[llen] = '\0';
            if (llen > 0 && !has_ext(lbuf)) strcat(lbuf, ".LIB");
            req.library = (char *)malloc((unsigned int)strlen(lbuf) + 1u);
            if (!req.library) { printf("cklib: out of memory\n"); return 1; }
            strcpy(req.library, lbuf);
        }
        req.additions = (char **)malloc((unsigned int)(cap * sizeof(char *)));
        if (!req.additions) { ck_lib_request_free(&req); printf("cklib: out of memory\n"); return 1; }
        while (pos < len) {
            ck_usize start;
            while (pos < len && (joined[pos] == ' ' || joined[pos] == '\t' ||
                                 joined[pos] == '+' || joined[pos] == '-')) ++pos;
            start = pos;
            while (pos < len && joined[pos] != ' ' && joined[pos] != '\t' &&
                   joined[pos] != '+' && joined[pos] != '-') ++pos;
            if (pos > start) {
                ck_usize tlen = pos - start;
                char *tok = (char *)malloc((unsigned int)tlen + 1u);
                if (!tok) { ck_lib_request_free(&req); return 1; }
                memcpy(tok, joined + start, (unsigned int)tlen);
                tok[tlen] = '\0';
                if (req.addition_count == cap) {
                    cap *= 2u;
                    req.additions = (char **)realloc(req.additions, (unsigned int)(cap * sizeof(char *)));
                    if (!req.additions) { free(tok); ck_lib_request_free(&req); return 1; }
                }
                req.additions[req.addition_count++] = tok;
            }
        }
    }

    members = (ck_lib_member_in *)calloc((unsigned int)req.addition_count, sizeof(ck_lib_member_in));
    obj_bufs = (ck_u8 **)calloc((unsigned int)req.addition_count, sizeof(ck_u8 *));
    mod_names = (char **)calloc((unsigned int)req.addition_count, sizeof(char *));
    if (!members || !obj_bufs || !mod_names) {
        printf("cklib: out of memory\n");
        goto done;
    }

    for (i = 0; i < req.addition_count; ++i) {
        char path[128];
        ck_u32 osz = 0;
        mod_names[i] = (char *)malloc(64u);
        if (!mod_names[i]) goto done;
        get_stem(req.additions[i], mod_names[i], 64u);
        strcpy(path, req.additions[i]);
        if (!has_ext(path)) strcat(path, ".OBJ");
        if (dos_read_all(path, &obj_bufs[i], &osz) != 0) {
            printf("cklib: cannot read %s\n", path);
            goto done;
        }
        members[i].module_name = mod_names[i];
        members[i].object = obj_bufs[i];
        members[i].object_size = osz;
    }

    if (ck_lib_build(members, req.addition_count, 16u, &build, &err) != 0) {
        printf("cklib: %s\n", err.message);
        goto done;
    }
    if (dos_write_all(req.library, build.bytes, build.size) != 0) {
        printf("cklib: cannot write %s\n", req.library);
        goto done;
    }
    rc = 0;

done:
    ck_lib_build_out_free(&build);
    if (obj_bufs) {
        for (i = 0; i < req.addition_count; ++i) free(obj_bufs[i]);
        free(obj_bufs);
    }
    if (mod_names) {
        for (i = 0; i < req.addition_count; ++i) free(mod_names[i]);
        free(mod_names);
    }
    free(members);
    ck_lib_request_free(&req);
    return rc;
}
