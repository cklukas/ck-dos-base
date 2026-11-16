/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/msgtools_core.h"

#if defined(M_I86) && !defined(_MSC_VER)
int sprintf(char *dst, const char *fmt, ...);
#endif

static const char k_crlf[] = "\r\n";
static const char k_sep[] = "; ----------------------------------------------------------";

typedef struct ck_str {
    char *data;
    ck_u32 len;
    ck_u32 cap;
} ck_str;

static int str_reserve(ck_str *s, ck_u32 extra) {
    ck_u32 needed = s->len + extra + 1u;
    ck_u32 new_cap;
    char *np;
    if (needed <= s->cap) return 0;
    new_cap = s->cap ? s->cap : 128u;
    while (new_cap < needed) {
        if (new_cap > 0x7FFFFFFFu / 2u) {
            new_cap = needed;
            break;
        }
        new_cap *= 2u;
    }
    np = (char *)realloc(s->data, (unsigned int)new_cap);
    if (!np) return -1;
    s->data = np;
    s->cap = new_cap;
    return 0;
}

static int str_append_n(ck_str *s, const char *p, ck_u32 n) {
    if (str_reserve(s, n) != 0) return -1;
    if (n > 0) memcpy(s->data + s->len, p, (unsigned int)n);
    s->len += n;
    s->data[s->len] = '\0';
    return 0;
}

static int str_append(ck_str *s, const char *p) {
    return str_append_n(s, p, (ck_u32)strlen(p));
}

static int str_append_line(ck_str *s, const char *p) {
    if (str_append(s, p) != 0) return -1;
    return str_append_n(s, k_crlf, 2u);
}

static int str_append_sep(ck_str *s) {
    if (str_append_n(s, k_crlf, 2u) != 0) return -1;
    if (str_append_line(s, k_sep) != 0) return -1;
    return str_append_n(s, k_crlf, 2u);
}

static char *ck_strdup_n(const char *p, ck_usize n) {
    char *d = (char *)malloc((unsigned int)n + 1u);
    if (!d) return NULL;
    if (n > 0) memcpy(d, p, (unsigned int)n);
    d[n] = '\0';
    return d;
}

static void to_lower_str(char *s) {
    while (*s) {
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
        ++s;
    }
}

static void to_upper_str(char *s) {
    while (*s) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 'a' + 'A');
        ++s;
    }
}

void ck_msg_lines_free(ck_msg_lines *ml) {
    ck_usize i;
    if (!ml) return;
    if (ml->lines) {
        for (i = 0; i < ml->count; ++i) {
            free(ml->lines[i]);
        }
        free(ml->lines);
    }
    ml->lines = NULL;
    ml->count = 0;
}

static int split_fields(const char *line, ck_usize len, const char **ptrs, ck_usize *lens, ck_usize max_fields) {
    ck_usize i = 0;
    ck_usize count = 0;
    while (i < len) {
        while (i < len && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= len) break;
        if (count + 1u == max_fields) {
            ptrs[count] = line + i;
            lens[count] = len - i;
            ++count;
            break;
        }
        ptrs[count] = line + i;
        while (i < len && line[i] != ' ' && line[i] != '\t') ++i;
        lens[count] = (ck_usize)((line + i) - ptrs[count]);
        ++count;
    }
    return (int)count;
}

static int parse_int_n(const char *p, ck_usize n) {
    int neg = 0;
    int val = 0;
    ck_usize i = 0;
    if (n > 0 && p[0] == '-') {
        neg = 1;
        i = 1;
    }
    for (; i < n; ++i) {
        if (p[i] >= '0' && p[i] <= '9') {
            val = val * 10 + (int)(p[i] - '0');
        }
    }
    return neg ? -val : val;
}

static int split_use_reference(const char *p, ck_usize n, char *comp_out, ck_usize comp_cap, int *num_out) {
    ck_usize split = n;
    while (split > 0 && p[split - 1] >= '0' && p[split - 1] <= '9') --split;
    if (split == 0 || split == n) return 0;
    if (split >= comp_cap) split = comp_cap - 1u;
    memcpy(comp_out, p, (unsigned int)split);
    comp_out[split] = '\0';
    *num_out = parse_int_n(p + split, n - split);
    return 1;
}

static ck_usize strip_comment(const char *line, ck_usize len) {
    char quote = '\0';
    ck_usize end = len;
    ck_usize i;
    for (i = 0; i < len; ++i) {
        char c = line[i];
        if (quote != '\0') {
            if (c == quote) quote = '\0';
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == ';') {
            end = i;
            break;
        }
    }
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) --end;
    return end;
}

static int next_line(const char *raw, ck_u32 size, ck_u32 *pos, const char **out_ptr, ck_usize *out_len) {
    ck_u32 start = *pos;
    ck_u32 end;
    ck_u32 stop;
    if (start >= size) return 0;
    end = start;
    while (end < size && raw[end] != '\n') ++end;
    stop = end;
    if (stop > start && raw[stop - 1] == '\r') --stop;
    *out_ptr = raw + start;
    *out_len = (ck_usize)(stop - start);
    *pos = (end < size) ? (end + 1u) : size;
    return 1;
}

typedef struct nosrv_file {
    char id[16];
    ck_str body;
} nosrv_file;

int ck_nosrvbld_core(const char *skl_raw,
                     ck_u32 skl_size,
                     const char *base_name,
                     ck_msg_lookup_fn lookup,
                     void *lookup_ctx,
                     ck_msg_emit_fn emit,
                     void *emit_ctx,
                     ck_error *err) {
    char base_lower[64];
    char base_upper[64];
    nosrv_file *files = NULL;
    ck_usize file_count = 0;
    ck_usize file_cap = 0;
    ck_u32 pos = 0;
    const char *raw_line;
    ck_usize line_len;
    ck_usize i;

    strncpy(base_lower, base_name ? base_name : "", sizeof(base_lower) - 1);
    base_lower[sizeof(base_lower) - 1] = '\0';
    to_lower_str(base_lower);
    strncpy(base_upper, base_name ? base_name : "", sizeof(base_upper) - 1);
    base_upper[sizeof(base_upper) - 1] = '\0';
    to_upper_str(base_upper);

    while (next_line(skl_raw, skl_size, &pos, &raw_line, &line_len)) {
        const char *fptrs[4];
        ck_usize flens[4];
        int fcnt;
        while (line_len > 0 && (raw_line[line_len - 1] == ' ' || raw_line[line_len - 1] == '\t')) {
            --line_len;
        }
        if (line_len == 0 || raw_line[0] == ';') continue;
        if (line_len >= 6 && strncmp(raw_line, ":class", 6) == 0) {
            nosrv_file *nf;
            char id_buf[16];
            char id_lower[16];
            char hdr[128];
            fcnt = split_fields(raw_line, line_len, fptrs, flens, 2);
            if (fcnt < 2) {
                ck_set_error(err, 0, ":class without a class id");
                goto fail;
            }
            if (file_count == file_cap) {
                ck_usize ncap = file_cap ? file_cap * 2u : 4u;
                nosrv_file *np = (nosrv_file *)realloc(files, (unsigned int)(ncap * sizeof(nosrv_file)));
                if (!np) goto oom;
                files = np;
                file_cap = ncap;
            }
            nf = &files[file_count++];
            memset(nf, 0, sizeof(*nf));
            if (flens[1] >= sizeof(id_buf)) flens[1] = sizeof(id_buf) - 1;
            memcpy(id_buf, fptrs[1], (unsigned int)flens[1]);
            id_buf[flens[1]] = '\0';
            strcpy(nf->id, id_buf);
            strcpy(id_lower, id_buf);
            to_lower_str(id_lower);
            sprintf(hdr, "; %s.cl%s ", base_lower, id_lower);
            if (str_append_line(&nf->body, hdr) != 0) goto oom;
            if (str_append_n(&nf->body, k_crlf, 2u) != 0) goto oom;
            continue;
        }
        if ((line_len >= 4 && strncmp(raw_line, ":use", 4) == 0) ||
            (line_len >= 4 && strncmp(raw_line, ":def", 4) == 0)) {
            int is_use = (strncmp(raw_line, ":use", 4) == 0);
            int number;
            char comp[32];
            char label[64];
            ck_msg_lines ml;
            int rc;
            nosrv_file *cur;
            ck_usize k;

            if (file_count == 0) {
                ck_set_error(err, 0, "directive before the first :class");
                goto fail;
            }
            fcnt = split_fields(raw_line, line_len, fptrs, flens, 4);
            if (fcnt < 3) {
                ck_set_error(err, 0, "directive with too few fields");
                goto fail;
            }
            number = parse_int_n(fptrs[1], flens[1]);
            if (is_use) {
                ck_usize clen = flens[2] < sizeof(comp) - 1 ? flens[2] : sizeof(comp) - 1;
                memcpy(comp, fptrs[2], (unsigned int)clen);
                comp[clen] = '\0';
                if (fcnt >= 4) {
                    ck_usize llen = flens[3] < sizeof(label) - 1 ? flens[3] : sizeof(label) - 1;
                    memcpy(label, fptrs[3], (unsigned int)llen);
                    label[llen] = '\0';
                } else {
                    label[0] = '\0';
                }
            } else {
                ck_usize llen = flens[2] < sizeof(label) - 1 ? flens[2] : sizeof(label) - 1;
                strcpy(comp, base_upper);
                memcpy(label, fptrs[2], (unsigned int)llen);
                label[llen] = '\0';
            }
            memset(&ml, 0, sizeof(ml));
            rc = lookup(lookup_ctx, comp, number, &ml);
            if (rc != 0 || ml.count == 0) {
                char msgbuf[96];
                ck_msg_lines_free(&ml);
                sprintf(msgbuf, "no message %s %d", comp, number);
                ck_set_error(err, 0, msgbuf);
                goto fail;
            }
            cur = &files[file_count - 1];
            if (str_append_n(&cur->body, k_crlf, 2u) != 0) { ck_msg_lines_free(&ml); goto oom; }
            if (str_append_line(&cur->body, ";_______________________") != 0) { ck_msg_lines_free(&ml); goto oom; }
            if (str_append_n(&cur->body, k_crlf, 2u) != 0) { ck_msg_lines_free(&ml); goto oom; }
            if (str_append(&cur->body, label) != 0) { ck_msg_lines_free(&ml); goto oom; }
            if (str_append(&cur->body, " DB    ") != 0) { ck_msg_lines_free(&ml); goto oom; }
            if (str_append_line(&cur->body, ml.lines[0]) != 0) { ck_msg_lines_free(&ml); goto oom; }
            for (k = 1; k < ml.count; ++k) {
                if (str_append(&cur->body, "\tDB   ") != 0) { ck_msg_lines_free(&ml); goto oom; }
                if (str_append_line(&cur->body, ml.lines[k]) != 0) { ck_msg_lines_free(&ml); goto oom; }
            }
            ck_msg_lines_free(&ml);
            continue;
        }
        if (line_len >= 4 && strncmp(raw_line, ":end", 4) == 0) break;
    }

    for (i = 0; i < file_count; ++i) {
        if (emit(emit_ctx, files[i].id, files[i].body.data ? files[i].body.data : "", files[i].body.len) != 0) {
            ck_set_error(err, 0, "emit failed");
            goto fail;
        }
    }
    for (i = 0; i < file_count; ++i) free(files[i].body.data);
    free(files);
    return 0;

oom:
    ck_set_error(err, 0, "out of memory");
fail:
    for (i = 0; i < file_count; ++i) free(files[i].body.data);
    free(files);
    return -1;
}

typedef struct skl_entry {
    int local;
    char component[32];
    int number;
    int ensure_crlf;
} skl_entry;

typedef struct skl_class {
    char id[16];
    skl_entry *entries;
    ck_usize count;
    ck_usize cap;
} skl_class;

static int class_add_entry(skl_class *c, const skl_entry *e) {
    if (c->count == c->cap) {
        ck_usize ncap = c->cap ? c->cap * 2u : 8u;
        skl_entry *np = (skl_entry *)realloc(c->entries, (unsigned int)(ncap * sizeof(skl_entry)));
        if (!np) return -1;
        c->entries = np;
        c->cap = ncap;
    }
    memcpy(&c->entries[c->count++], e, sizeof(skl_entry));
    return 0;
}

static const char *builtin_text(const char *class_id) {
    if (strcmp(class_id, "2") == 0 || strcmp(class_id, "4") == 0) {
        return "\"Parse Error %1\"";
    }
    return "\"Extended Error %1\"";
}

typedef struct nosrv_wrap_ctx {
    ck_msg_emit_fn inner;
    void *inner_ctx;
} nosrv_wrap_ctx;

static int nosrv_wrap_emit(void *ctx, const char *id, const char *data, ck_u32 size) {
    nosrv_wrap_ctx *w = (nosrv_wrap_ctx *)ctx;
    char tag[24];
    sprintf(tag, "CL%s", id);
    return w->inner(w->inner_ctx, tag, data, size);
}

int ck_buildmsg_core(const char *skl_raw,
                     ck_u32 skl_size,
                     ck_msg_lookup_fn lookup,
                     void *lookup_ctx,
                     ck_msg_emit_fn emit,
                     void *emit_ctx,
                     char *out_util,
                     ck_usize out_util_cap,
                     ck_error *err) {
    char util[64];
    skl_class *classes = NULL;
    ck_usize class_count = 0;
    ck_usize class_cap = 0;
    ck_u32 pos = 0;
    const char *raw_line;
    ck_usize line_len;
    ck_usize i;
    int resident_sequence = 0;
    int resident_count = 0;
    const char *transients[2];
    int t;
    char ctl_buf[64];

    util[0] = '\0';
    while (next_line(skl_raw, skl_size, &pos, &raw_line, &line_len)) {
        ck_usize len;
        char head[8];
        ck_usize hlen;
        const char *fptrs[4];
        ck_usize flens[4];
        int fcnt;
        int is_use, is_def;
        skl_entry entry;

        if (line_len > 0 && (raw_line[0] == '\t' || raw_line[0] == ' ')) continue;
        len = strip_comment(raw_line, line_len);
        if (len == 0) continue;

        hlen = len < 6u ? len : 6u;
        memcpy(head, raw_line, (unsigned int)hlen);
        head[hlen] = '\0';
        to_lower_str(head);

        if (strncmp(head, ":util", 5) == 0) {
            fcnt = split_fields(raw_line, len, fptrs, flens, 2);
            if (fcnt < 2) { ck_set_error(err, 0, ":util without a name"); goto fail; }
            if (flens[1] >= sizeof(util)) flens[1] = sizeof(util) - 1;
            memcpy(util, fptrs[1], (unsigned int)flens[1]);
            util[flens[1]] = '\0';
            continue;
        }
        if (strncmp(head, ":class", 6) == 0) {
            skl_class *nc;
            fcnt = split_fields(raw_line, len, fptrs, flens, 2);
            if (fcnt < 2) { ck_set_error(err, 0, ":class without a class id"); goto fail; }
            if (class_count == class_cap) {
                ck_usize ncap = class_cap ? class_cap * 2u : 4u;
                skl_class *np = (skl_class *)realloc(classes, (unsigned int)(ncap * sizeof(skl_class)));
                if (!np) goto oom;
                classes = np;
                class_cap = ncap;
            }
            nc = &classes[class_count++];
            memset(nc, 0, sizeof(*nc));
            if (flens[1] >= sizeof(nc->id)) flens[1] = sizeof(nc->id) - 1;
            memcpy(nc->id, fptrs[1], (unsigned int)flens[1]);
            nc->id[flens[1]] = '\0';
            continue;
        }
        if (strncmp(head, ":end", 4) == 0) break;

        is_use = (strncmp(head, ":use", 4) == 0);
        is_def = (strncmp(head, ":def", 4) == 0);
        if (!is_use && !is_def) continue;
        if (class_count == 0) { ck_set_error(err, 0, "directive before the first :class"); goto fail; }

        fcnt = split_fields(raw_line, len, fptrs, flens, 4);
        if (util[0] == '\0' && is_use && fcnt == 4) {
            char dcomp[32];
            int dnum = 0;
            if (!split_use_reference(fptrs[2], flens[2], dcomp, sizeof(dcomp), &dnum)) {
                nosrv_wrap_ctx wctx;
                for (i = 0; i < class_count; ++i) free(classes[i].entries);
                free(classes);
                classes = NULL;
                class_count = 0;
                wctx.inner = emit;
                wctx.inner_ctx = emit_ctx;
                if (ck_nosrvbld_core(skl_raw, skl_size, "", lookup, lookup_ctx, nosrv_wrap_emit, &wctx, err) != 0) {
                    return -1;
                }
                if (emit(emit_ctx, "CTL", "", 0u) != 0) {
                    ck_set_error(err, 0, "emit CTL failed");
                    return -1;
                }
                if (out_util && out_util_cap > 0) out_util[0] = '\0';
                return 0;
            }
        }

        memset(&entry, 0, sizeof(entry));
        if (is_def) {
            if (fcnt < 2) { ck_set_error(err, 0, ":def without a message id"); goto fail; }
            entry.local = parse_int_n(fptrs[1], flens[1]);
            entry.number = entry.local;
        } else if (fcnt == 2) {
            if (!split_use_reference(fptrs[1], flens[1], entry.component, sizeof(entry.component), &entry.number)) {
                ck_set_error(err, 0, "unparsable :use reference");
                goto fail;
            }
            entry.local = entry.number;
        } else if (fcnt >= 4 &&
                   !split_use_reference(fptrs[2], flens[2], entry.component, sizeof(entry.component), &entry.number)) {
            ck_usize clen = flens[2] < sizeof(entry.component) - 1 ? flens[2] : sizeof(entry.component) - 1;
            entry.local = parse_int_n(fptrs[1], flens[1]);
            entry.number = entry.local;
            memcpy(entry.component, fptrs[2], (unsigned int)clen);
            entry.component[clen] = '\0';
            to_upper_str(entry.component);
            entry.ensure_crlf = 1;
        } else {
            if (fcnt < 3) { ck_set_error(err, 0, "unparsable :use reference"); goto fail; }
            entry.local = parse_int_n(fptrs[1], flens[1]);
            if (!split_use_reference(fptrs[2], flens[2], entry.component, sizeof(entry.component), &entry.number)) {
                ck_set_error(err, 0, "unparsable :use reference");
                goto fail;
            }
            entry.ensure_crlf = 1;
        }
        if (class_add_entry(&classes[class_count - 1], &entry) != 0) goto oom;
    }

    if (class_count > 0 && util[0] != '\0') {
        int last_id = 0;
        skl_class *last_cls = &classes[class_count - 1];
        for (i = 0; i < last_cls->count; ++i) {
            if (last_cls->entries[i].local > last_id) last_id = last_cls->entries[i].local;
        }
        for (;;) {
            ck_msg_lines ml;
            memset(&ml, 0, sizeof(ml));
            if (lookup(lookup_ctx, util, last_id + 1, &ml) != 0) break;
            ck_msg_lines_free(&ml);
            {
                skl_entry e;
                memset(&e, 0, sizeof(e));
                e.local = last_id + 1;
                e.number = last_id + 1;
                if (class_add_entry(last_cls, &e) != 0) goto oom;
                ++last_id;
            }
        }
    }

    transients[0] = "2";
    transients[1] = "1";
    for (t = 0; t < 2; ++t) {
        int declared = 0;
        for (i = 0; i < class_count; ++i) {
            if (strcmp(classes[i].id, transients[t]) == 0) { declared = 1; break; }
        }
        if (!declared) {
            if (class_count == class_cap) {
                ck_usize ncap = class_cap ? class_cap * 2u : 4u;
                skl_class *np = (skl_class *)realloc(classes, (unsigned int)(ncap * sizeof(skl_class)));
                if (!np) goto oom;
                classes = np;
                class_cap = ncap;
            }
            memmove(classes + 1, classes, (unsigned int)(class_count * sizeof(skl_class)));
            memset(&classes[0], 0, sizeof(skl_class));
            strcpy(classes[0].id, transients[t]);
            ++class_count;
        }
    }

    if (out_util && out_util_cap > 0) {
        strncpy(out_util, util, (unsigned int)(out_util_cap - 1));
        out_util[out_util_cap - 1] = '\0';
    }

    for (i = 0; i < class_count; ++i) {
        if (strcmp(classes[i].id, "1") != 0 && strcmp(classes[i].id, "2") != 0) ++resident_count;
    }

    for (i = 0; i < class_count; ++i) {
        skl_class *sc = &classes[i];
        const char *cid = sc->id;
        int transient = (strcmp(cid, "1") == 0 || strcmp(cid, "2") == 0);
        char pub_name[32];
        ck_str out;
        ck_msg_lines *resolved = NULL;
        ck_u16 *locals = NULL;
        ck_usize j, k;
        char linebuf[256];
        char tag[24];

        if (!transient) ++resident_sequence;
        if (transient) sprintf(pub_name, "$M_MSGSERV_%s", cid);
        else sprintf(pub_name, "$M_CLS_%d", resident_sequence);

        memset(&out, 0, sizeof(out));
        if (sc->count > 0) {
            resolved = (ck_msg_lines *)calloc((unsigned int)sc->count, sizeof(ck_msg_lines));
            locals = (ck_u16 *)calloc((unsigned int)sc->count, sizeof(ck_u16));
            if (!resolved || !locals) { free(resolved); free(locals); goto oom; }
        }

        for (j = 0; j < sc->count; ++j) {
            skl_entry *e = &sc->entries[j];
            const char *comp = e->component[0] ? e->component : util;
            ck_msg_lines ml;
            int rc;
            char upper_comp[32];
            int crlf_comp;

            locals[j] = (e->local == -1) ? 0xFFFFu : (ck_u16)e->local;
            memset(&ml, 0, sizeof(ml));
            rc = lookup(lookup_ctx, comp, e->number, &ml);
            if (rc != 0 && e->local == -1) {
                ml.lines = (char **)malloc(sizeof(char *));
                if (!ml.lines) { free(resolved); free(locals); goto oom; }
                ml.lines[0] = ck_strdup_n(builtin_text(cid), strlen(builtin_text(cid)));
                if (!ml.lines[0]) { free(ml.lines); free(resolved); free(locals); goto oom; }
                ml.count = 1;
            }
            strcpy(upper_comp, e->component);
            to_upper_str(upper_comp);
            crlf_comp = (strcmp(upper_comp, "EXTEND") == 0 || strcmp(upper_comp, "PARSE") == 0);
            if (e->ensure_crlf && crlf_comp && ml.count > 0) {
                char *last = ml.lines[ml.count - 1];
                ck_usize llen = strlen(last);
                if (llen < 5u || strcmp(last + llen - 5u, "CR,LF") != 0) {
                    char *np = (char *)realloc(last, (unsigned int)(llen + 7u));
                    if (!np) { ck_msg_lines_free(&ml); free(resolved); free(locals); goto oom; }
                    strcat(np, ",CR,LF");
                    ml.lines[ml.count - 1] = np;
                }
            }
            memcpy(&resolved[j], &ml, sizeof(ck_msg_lines));
        }

        if (str_append_sep(&out) != 0) goto cls_oom;
        sprintf(linebuf, "        PUBLIC  %s", pub_name);
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (str_append_line(&out, "        IF1") != 0) goto cls_oom;
        sprintf(linebuf, "        %%out    ... Including message Class %s", cid);
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (str_append_line(&out, "        ENDIF") != 0) goto cls_oom;
        if (str_append_sep(&out) != 0) goto cls_oom;

        sprintf(linebuf, "$M_CLASS_%s_STRUC LABEL BYTE", cid);
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (transient) {
            sprintf(linebuf, "        $M_CLASS_ID <00%sH,EXPECTED_VERSION,Class_%s_MessageCount>", cid, cid);
        } else {
            sprintf(linebuf, "        $M_CLASS_ID <0FFH,EXPECTED_VERSION,Class_%s_MessageCount>", cid);
        }
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (str_append_sep(&out) != 0) goto cls_oom;
        if (str_append_n(&out, k_crlf, 2u) != 0) goto cls_oom;

        for (j = 0; j < sc->count; ++j) {
            char mtag[32];
            sprintf(mtag, "$M_%s_%05XH", cid, (unsigned int)locals[j]);
            sprintf(linebuf, "%s_STRUC LABEL BYTE", mtag);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            sprintf(linebuf, "        $M_ID   <%05XH,%s_MSG-%s_STRUC>", (unsigned int)locals[j], mtag, mtag);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            if (j + 1u < sc->count) {
                if (str_append_n(&out, k_crlf, 2u) != 0) goto cls_oom;
            }
        }
        if (transient) {
            if (str_append_sep(&out) != 0) goto cls_oom;
            sprintf(linebuf, "$M_%s_FF_STRUC LABEL BYTE", cid);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            sprintf(linebuf, "        $M_ID <0FFFFH,$M_%s_FF_MSG-$M_%s_FF_STRUC>", cid, cid);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        }
        if (str_append_sep(&out) != 0) goto cls_oom;
        if (str_append_n(&out, k_crlf, 2u) != 0) goto cls_oom;

        for (j = 0; j < sc->count; ++j) {
            char mtag[32];
            sprintf(mtag, "$M_%s_%05XH", cid, (unsigned int)locals[j]);
            sprintf(linebuf, "%s_MSG LABEL BYTE", mtag);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            sprintf(linebuf, "        DB      %s_END-%s_MSG-1", mtag, mtag);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            for (k = 0; k < resolved[j].count; ++k) {
                if (str_append(&out, "        DB      ") != 0) goto cls_oom;
                if (str_append_line(&out, resolved[j].lines[k]) != 0) goto cls_oom;
            }
            sprintf(linebuf, "%s_END LABEL BYTE", mtag);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            if (str_append_line(&out, "  ") != 0) goto cls_oom;
        }
        if (transient) {
            if (str_append_line(&out, k_sep) != 0) goto cls_oom;
            if (str_append_n(&out, k_crlf, 2u) != 0) goto cls_oom;
            sprintf(linebuf, "$M_%s_FF_MSG LABEL BYTE", cid);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            sprintf(linebuf, "        DB      $M_%s_FF_END-$M_%s_FF_MSG-1", cid, cid);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            sprintf(linebuf, "        DB      %s", builtin_text(cid));
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            sprintf(linebuf, "$M_%s_FF_END LABEL BYTE", cid);
            if (str_append_line(&out, linebuf) != 0) goto cls_oom;
            if (str_append_line(&out, "  ") != 0) goto cls_oom;
        }
        if (str_append_line(&out, k_sep) != 0) goto cls_oom;
        if (str_append_n(&out, k_crlf, 2u) != 0) goto cls_oom;
        sprintf(linebuf, "Class_%s_MessageCount EQU     %u", cid, (unsigned int)(sc->count + (transient ? 1u : 0u)));
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (str_append_sep(&out) != 0) goto cls_oom;
        if (str_append_line(&out, "        IF      FARmsg") != 0) goto cls_oom;
        sprintf(linebuf, "%s PROC FAR", pub_name);
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (str_append_line(&out, "        ELSE") != 0) goto cls_oom;
        sprintf(linebuf, "%s PROC NEAR", pub_name);
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (str_append_line(&out, "        ENDIF") != 0) goto cls_oom;
        if (str_append_n(&out, k_crlf, 2u) != 0) goto cls_oom;
        if (str_append_line(&out, "        PUSH    CS") != 0) goto cls_oom;
        if (str_append_line(&out, "        POP     ES") != 0) goto cls_oom;
        sprintf(linebuf, "        LEA     DI,$M_CLASS_%s_STRUC", cid);
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        sprintf(linebuf, "        ADD     CX,$-$M_CLASS_%s_STRUC", cid);
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (str_append_line(&out, "        RET") != 0) goto cls_oom;
        if (str_append_n(&out, k_crlf, 2u) != 0) goto cls_oom;
        sprintf(linebuf, "%s %s", pub_name, transient ? "Endp" : "ENDP");
        if (str_append_line(&out, linebuf) != 0) goto cls_oom;
        if (str_append_sep(&out) != 0) goto cls_oom;

        sprintf(tag, "CL%s", cid);
        if (emit(emit_ctx, tag, out.data ? out.data : "", out.len) != 0) {
            free(out.data);
            for (j = 0; j < sc->count; ++j) ck_msg_lines_free(&resolved[j]);
            free(resolved);
            free(locals);
            ck_set_error(err, 0, "emit failed");
            goto fail;
        }
        free(out.data);
        for (j = 0; j < sc->count; ++j) ck_msg_lines_free(&resolved[j]);
        free(resolved);
        free(locals);
        continue;

    cls_oom:
        free(out.data);
        for (j = 0; j < sc->count; ++j) ck_msg_lines_free(&resolved[j]);
        free(resolved);
        free(locals);
        goto oom;
    }

    sprintf(ctl_buf, "$M_NUM_CLS  EQU %d\r\n", resident_count);
    if (emit(emit_ctx, "CTL", ctl_buf, (ck_u32)strlen(ctl_buf)) != 0) {
        ck_set_error(err, 0, "emit CTL failed");
        goto fail;
    }

    for (i = 0; i < class_count; ++i) free(classes[i].entries);
    free(classes);
    return 0;

oom:
    ck_set_error(err, 0, "out of memory");
fail:
    for (i = 0; i < class_count; ++i) free(classes[i].entries);
    free(classes);
    return -1;
}
