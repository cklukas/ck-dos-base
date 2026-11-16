/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/lib_core.h"

#if defined(M_I86) && !defined(_MSC_VER)
int sprintf(char *dst, const char *fmt, ...);
#endif

#define CK_LIB_HEADER 0xF0u
#define CK_LIB_END    0xF1u
#define CK_LIB_EXT    0xF2u
#define CK_OMF_COMMENT 0x88u
#define CK_OMF_LIBMOD  0xA3u
#define CK_DICT_BLOCK_BYTES 512u

static char *ck_lib_strdup(const char *s) {
    ck_usize n = strlen(s);
    char *p = (char *)malloc((unsigned int)n + 1u);
    if (!p) return NULL;
    if (n > 0) memcpy(p, s, (unsigned int)n);
    p[n] = '\0';
    return p;
}

static char *ck_lib_strdup_n(const char *s, ck_usize n) {
    char *p = (char *)malloc((unsigned int)n + 1u);
    if (!p) return NULL;
    if (n > 0) memcpy(p, s, (unsigned int)n);
    p[n] = '\0';
    return p;
}

static ck_u32 round_up_u32(ck_u32 v, ck_u32 m) {
    return ((v + m - 1u) / m) * m;
}

static int upper_eq(const char *a, const char *b) {
    return strcmpi(a, b) == 0;
}

void ck_lib_build_out_free(ck_lib_build_out *out) {
    ck_usize i, j;
    if (!out) return;
    free(out->bytes);
    free(out->pages);
    free(out->sizes);
    if (out->symbols) {
        for (i = 0; i < out->symbol_count; ++i) free(out->symbols[i].name);
        free(out->symbols);
    }
    if (out->publics) {
        for (i = 0; i < out->member_count; ++i) {
            if (out->publics[i]) {
                for (j = 0; j < out->public_counts[i]; ++j) free(out->publics[i][j]);
                free(out->publics[i]);
            }
        }
        free(out->publics);
    }
    free(out->public_counts);
    memset(out, 0, sizeof(*out));
}

void ck_lib_request_free(ck_lib_request *req) {
    ck_usize i;
    if (!req) return;
    free(req->library);
    free(req->listing);
    if (req->additions) {
        for (i = 0; i < req->addition_count; ++i) free(req->additions[i]);
        free(req->additions);
    }
    memset(req, 0, sizeof(*req));
}

int ck_lib_parse_response(const char *text,
                          ck_u32 size,
                          ck_lib_request *out,
                          ck_error *err) {
    char **lines = NULL;
    ck_usize line_count = 0;
    ck_usize line_cap = 0;
    ck_u32 i = 0;
    ck_usize idx = 0;
    char *ops = NULL;
    ck_usize ops_len = 0;
    ck_usize ops_cap = 0;
    char *cur = NULL;
    ck_usize cur_len = 0;
    ck_usize cur_cap = 0;
    ck_usize add_cap = 0;

    memset(out, 0, sizeof(*out));
    for (i = 0; i <= size; ++i) {
        int is_end = (i == size) || (text[i] == '\x1A');
        char c = is_end ? '\n' : text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (!is_end || cur_len > 0) {
                char *ln = ck_lib_strdup_n(cur ? cur : "", cur_len);
                if (!ln) goto oom;
                if (line_count == line_cap) {
                    ck_usize nc = line_cap ? line_cap * 2u : 8u;
                    char **np = (char **)realloc(lines, (unsigned int)(nc * sizeof(char *)));
                    if (!np) { free(ln); goto oom; }
                    lines = np;
                    line_cap = nc;
                }
                lines[line_count++] = ln;
                cur_len = 0;
            }
            if (is_end) break;
        } else {
            if (cur_len + 1u >= cur_cap) {
                ck_usize nc = cur_cap ? cur_cap * 2u : 64u;
                char *np = (char *)realloc(cur, (unsigned int)nc);
                if (!np) goto oom;
                cur = np;
                cur_cap = nc;
            }
            cur[cur_len++] = c;
        }
    }
    free(cur);
    cur = NULL;

    while (idx < line_count && lines[idx][0] == '\0') ++idx;
    if (idx < line_count) {
        out->library = ck_lib_strdup(lines[idx++]);
        if (!out->library) goto oom;
    }
    if (idx < line_count && (strcmp(lines[idx], "y") == 0 || strcmp(lines[idx], "Y") == 0)) {
        ++idx;
    }

    for (; idx < line_count; ++idx) {
        char *ln = lines[idx];
        ck_usize llen = strlen(ln);
        int continued;
        while (llen > 0 && (ln[llen - 1] == ' ' || ln[llen - 1] == '\t')) ln[--llen] = '\0';
        if (llen == 0) continue;
        continued = (ln[llen - 1] == '&');
        if (continued) ln[--llen] = '\0';
        if (ops_len + llen + 1u > ops_cap) {
            ck_usize nc = ops_cap ? ops_cap * 2u : 128u;
            char *np;
            while (nc < ops_len + llen + 1u) nc *= 2u;
            np = (char *)realloc(ops, (unsigned int)nc);
            if (!np) goto oom;
            ops = np;
            ops_cap = nc;
        }
        if (llen > 0) memcpy(ops + ops_len, ln, (unsigned int)llen);
        ops_len += llen;
        ops[ops_len] = '\0';
        if (!continued) {
            ++idx;
            break;
        }
    }

    for (; idx < line_count; ++idx) {
        char *ln = lines[idx];
        ck_usize llen = strlen(ln);
        while (llen > 0 && (ln[llen - 1] == ' ' || ln[llen - 1] == '\t')) ln[--llen] = '\0';
        if (llen == 0) continue;
        if (ln[llen - 1] == ';') ln[--llen] = '\0';
        out->listing = ck_lib_strdup(ln);
        if (!out->listing) goto oom;
        break;
    }

    if (ops) {
        ck_usize p = 0;
        while (p < ops_len) {
            ck_usize start;
            while (p < ops_len && (ops[p] == '+' || ops[p] == ' ' || ops[p] == '\t' || ops[p] == ';')) ++p;
            start = p;
            while (p < ops_len && ops[p] != '+' && ops[p] != ' ' && ops[p] != '\t' && ops[p] != ';') ++p;
            if (p > start) {
                char *tok = ck_lib_strdup_n(ops + start, p - start);
                if (!tok) goto oom;
                if (out->addition_count == add_cap) {
                    ck_usize nc = add_cap ? add_cap * 2u : 8u;
                    char **np = (char **)realloc(out->additions, (unsigned int)(nc * sizeof(char *)));
                    if (!np) { free(tok); goto oom; }
                    out->additions = np;
                    add_cap = nc;
                }
                out->additions[out->addition_count++] = tok;
            }
        }
    }

    for (idx = 0; idx < line_count; ++idx) free(lines[idx]);
    free(lines);
    free(ops);
    return 0;

oom:
    free(cur);
    free(ops);
    for (idx = 0; idx < line_count; ++idx) free(lines[idx]);
    free(lines);
    ck_lib_request_free(out);
    ck_set_error(err, 0, "out of memory");
    return -1;
}

static int add_str(char ***arr, ck_usize *count, ck_usize *cap, const char *s) {
    char *dup = ck_lib_strdup(s);
    if (!dup) return -1;
    if (*count == *cap) {
        ck_usize nc = *cap ? (*cap) * 2u : 8u;
        char **np = (char **)realloc(*arr, (unsigned int)(nc * sizeof(char *)));
        if (!np) { free(dup); return -1; }
        *arr = np;
        *cap = nc;
    }
    (*arr)[(*count)++] = dup;
    return 0;
}

static int add_sym(ck_dict_sym **syms, ck_usize *count, ck_usize *cap, const char *name, ck_u16 page) {
    char *dup = ck_lib_strdup(name);
    if (!dup) return -1;
    if (*count == *cap) {
        ck_usize nc = *cap ? (*cap) * 2u : 16u;
        ck_dict_sym *np = (ck_dict_sym *)realloc(*syms, (unsigned int)(nc * sizeof(ck_dict_sym)));
        if (!np) { free(dup); return -1; }
        *syms = np;
        *cap = nc;
    }
    (*syms)[*count].name = dup;
    (*syms)[*count].page = page;
    (*count)++;
    return 0;
}

int ck_lib_build(const ck_lib_member_in *members,
                 ck_usize member_count,
                 ck_usize page_size,
                 ck_lib_build_out *out,
                 ck_error *err) {
    ck_u8 **bodies = NULL;
    ck_u32 *body_lens = NULL;
    char ***externals = NULL;
    ck_usize *ext_counts = NULL;
    ck_usize sym_cap = 0;
    ck_u32 offset;
    ck_usize i, j;
    ck_u16 blocks = 0;
    ck_u8 *dict = NULL;
    ck_u32 dict_size = 0;
    ck_u32 dict_offset;
    ck_u32 end_rec_bytes;
    ck_u8 *pairs = NULL;
    ck_u32 pairs_len = 0;
    ck_u8 *payload = NULL;
    ck_u32 payload_len = 0;
    ck_u32 payload_cap = 0;
    ck_u32 entry_off;
    ck_u32 ext_rec_len;
    ck_u32 total_bytes;
    ck_u32 pos;

    memset(out, 0, sizeof(*out));
    out->member_count = member_count;
    if (member_count > 0) {
        bodies = (ck_u8 **)calloc((unsigned int)member_count, sizeof(ck_u8 *));
        body_lens = (ck_u32 *)calloc((unsigned int)member_count, sizeof(ck_u32));
        externals = (char ***)calloc((unsigned int)member_count, sizeof(char **));
        ext_counts = (ck_usize *)calloc((unsigned int)member_count, sizeof(ck_usize));
        out->pages = (ck_u16 *)calloc((unsigned int)member_count, sizeof(ck_u16));
        out->sizes = (ck_u16 *)calloc((unsigned int)member_count, sizeof(ck_u16));
        out->publics = (char ***)calloc((unsigned int)member_count, sizeof(char **));
        out->public_counts = (ck_usize *)calloc((unsigned int)member_count, sizeof(ck_usize));
        if (!bodies || !body_lens || !externals || !ext_counts ||
            !out->pages || !out->sizes || !out->publics || !out->public_counts) {
            goto oom;
        }
    }

    offset = (ck_u32)page_size;
    for (i = 0; i < member_count; ++i) {
        ck_omf_record *recs = NULL;
        ck_usize rec_count = 0;
        ck_omf_record *new_recs = NULL;
        ck_usize mlen = strlen(members[i].module_name);
        ck_u8 *libmod_body = NULL;
        ck_u8 *raw_body = NULL;
        ck_u32 raw_len = 0;
        ck_u32 padded;
        ck_u16 page;
        ck_usize pub_cap = 0;
        ck_usize ext_cap = 0;
        ck_u16 msize = 0;
        char mod_bang[132];

        if (ck_omf_parse_records(members[i].object, members[i].object_size, 0, 0, &recs, &rec_count, err) != 0) {
            goto fail;
        }
        if (rec_count == 0) {
            ck_omf_free_records(recs, rec_count);
            ck_set_error(err, 0, "member has no OMF records");
            goto fail;
        }

        new_recs = (ck_omf_record *)malloc((unsigned int)((rec_count + 1u) * sizeof(ck_omf_record)));
        libmod_body = (ck_u8 *)malloc((unsigned int)(3u + mlen));
        if (!new_recs || !libmod_body) {
            free(new_recs);
            free(libmod_body);
            ck_omf_free_records(recs, rec_count);
            goto oom;
        }
        libmod_body[0] = 0x00u;
        libmod_body[1] = (ck_u8)CK_OMF_LIBMOD;
        libmod_body[2] = (ck_u8)mlen;
        if (mlen > 0) memcpy(libmod_body + 3, members[i].module_name, (unsigned int)mlen);

        memcpy(&new_recs[0], &recs[0], sizeof(ck_omf_record));
        new_recs[1].type = (ck_u8)CK_OMF_COMMENT;
        new_recs[1].declared_length = (ck_u16)(4u + mlen);
        new_recs[1].body = libmod_body;
        new_recs[1].body_len = (ck_u16)(3u + mlen);
        new_recs[1].checksum = 0;
        if (rec_count > 1u) {
            memcpy(&new_recs[2], &recs[1], (unsigned int)((rec_count - 1u) * sizeof(ck_omf_record)));
        }

        if (ck_omf_serialize_records(new_recs, rec_count + 1u, &raw_body, &raw_len) != 0) {
            free(new_recs);
            free(libmod_body);
            ck_omf_free_records(recs, rec_count);
            goto oom;
        }

        page = (ck_u16)(offset / (ck_u32)page_size);
        out->pages[i] = page;
        sprintf(mod_bang, "%s!", members[i].module_name);
        if (add_sym(&out->symbols, &out->symbol_count, &sym_cap, mod_bang, page) != 0) {
            free(raw_body);
            free(new_recs);
            free(libmod_body);
            ck_omf_free_records(recs, rec_count);
            goto oom;
        }

        for (j = 0; j < rec_count + 1u; ++j) {
            ck_u8 base = (ck_u8)(new_recs[j].type & 0xFEu);
            ck_u16 rpos = 0;
            const ck_u8 *rb = new_recs[j].body;
            ck_u16 rlen = new_recs[j].body_len;
            char namebuf[132];
            if (base == 0x90u) {
                ck_u16 grp, seg, dummy;
                if (ck_omf_read_index(rb, rlen, &rpos, &grp) == 0 &&
                    ck_omf_read_index(rb, rlen, &rpos, &seg) == 0) {
                    if (seg == 0 && rpos + 2u <= rlen) rpos += 2u;
                    while (rpos < rlen) {
                        if (ck_omf_read_name(rb, rlen, &rpos, namebuf, sizeof(namebuf)) != 0) break;
                        if (rpos + 2u > rlen) break;
                        rpos += 2u;
                        if (ck_omf_read_index(rb, rlen, &rpos, &dummy) != 0) break;
                        if (add_str(&out->publics[i], &out->public_counts[i], &pub_cap, namebuf) != 0) {
                            free(raw_body); free(new_recs); free(libmod_body);
                            ck_omf_free_records(recs, rec_count);
                            goto oom;
                        }
                    }
                }
            } else if (base == 0x8Cu) {
                ck_u16 dummy;
                while (rpos < rlen) {
                    if (ck_omf_read_name(rb, rlen, &rpos, namebuf, sizeof(namebuf)) != 0) break;
                    if (ck_omf_read_index(rb, rlen, &rpos, &dummy) != 0) break;
                    if (add_str(&externals[i], &ext_counts[i], &ext_cap, namebuf) != 0) {
                        free(raw_body); free(new_recs); free(libmod_body);
                        ck_omf_free_records(recs, rec_count);
                        goto oom;
                    }
                }
            } else if (base == 0x98u && rpos < rlen) {
                ck_u8 acbp = rb[rpos++];
                if ((acbp >> 5) == 0 && rpos + 3u <= rlen) rpos += 3u;
                if (rpos + 2u <= rlen) {
                    ck_u16 slen = ck_read_u16_le(rb + rpos);
                    msize = (ck_u16)(msize + slen);
                }
            }
        }

        for (j = 0; j < out->public_counts[i]; ++j) {
            if (add_sym(&out->symbols, &out->symbol_count, &sym_cap, out->publics[i][j], page) != 0) {
                free(raw_body); free(new_recs); free(libmod_body);
                ck_omf_free_records(recs, rec_count);
                goto oom;
            }
        }
        out->sizes[i] = msize;

        free(new_recs);
        free(libmod_body);
        ck_omf_free_records(recs, rec_count);

        padded = round_up_u32(raw_len, (ck_u32)page_size);
        bodies[i] = (ck_u8 *)calloc(1u, (unsigned int)padded);
        if (!bodies[i]) { free(raw_body); goto oom; }
        memcpy(bodies[i], raw_body, (unsigned int)raw_len);
        free(raw_body);
        body_lens[i] = padded;
        offset += padded;
    }

    if (ck_omf_build_dictionary(out->symbols, out->symbol_count, &blocks, &dict, &dict_size, err) != 0) {
        goto fail;
    }

    dict_offset = round_up_u32(offset + 3u, (ck_u32)CK_DICT_BLOCK_BYTES);
    end_rec_bytes = dict_offset - offset;

    pairs = (ck_u8 *)malloc((unsigned int)(4u * (member_count + 1u)));
    if (!pairs) goto oom;
    entry_off = (ck_u32)(4u * (member_count + 1u));
    for (i = 0; i < member_count; ++i) {
        ck_u16 *uses = NULL;
        ck_usize use_count = 0;
        ck_usize use_cap = 0;
        ck_usize ei, mi, pi;
        for (ei = 0; ei < ext_counts[i]; ++ei) {
            const char *ename = externals[i][ei];
            ck_usize found_m = (ck_usize)-1;
            for (mi = 0; mi < member_count; ++mi) {
                for (pi = 0; pi < out->public_counts[mi]; ++pi) {
                    if (upper_eq(ename, out->publics[mi][pi])) {
                        found_m = mi;
                        break;
                    }
                }
                if (found_m != (ck_usize)-1) break;
            }
            if (found_m != (ck_usize)-1) {
                ck_u16 val = (ck_u16)found_m;
                int dup = 0;
                for (pi = 0; pi < use_count; ++pi) {
                    if (uses[pi] == val) { dup = 1; break; }
                }
                if (!dup) {
                    if (use_count == use_cap) {
                        ck_usize nc = use_cap ? use_cap * 2u : 8u;
                        ck_u16 *np = (ck_u16 *)realloc(uses, (unsigned int)(nc * sizeof(ck_u16)));
                        if (!np) { free(uses); goto oom; }
                        uses = np;
                        use_cap = nc;
                    }
                    uses[use_count++] = val;
                }
            }
        }
        for (ei = 1; ei < use_count; ++ei) {
            ck_u16 key = uses[ei];
            mi = ei;
            while (mi > 0 && uses[mi - 1] > key) {
                uses[mi] = uses[mi - 1];
                --mi;
            }
            uses[mi] = key;
        }
        ck_write_u16_le(pairs + pairs_len, out->pages[i]);
        ck_write_u16_le(pairs + pairs_len + 2u, (ck_u16)entry_off);
        pairs_len += 4u;

        if (payload_len + 2u + 2u * (ck_u32)use_count > payload_cap) {
            ck_u32 nc = payload_cap ? payload_cap * 2u : 64u;
            ck_u8 *np;
            while (nc < payload_len + 2u + 2u * (ck_u32)use_count) nc *= 2u;
            np = (ck_u8 *)realloc(payload, (unsigned int)nc);
            if (!np) { free(uses); goto oom; }
            payload = np;
            payload_cap = nc;
        }
        ck_write_u16_le(payload + payload_len, (ck_u16)use_count);
        payload_len += 2u;
        for (ei = 0; ei < use_count; ++ei) {
            ck_write_u16_le(payload + payload_len, uses[ei]);
            payload_len += 2u;
        }
        entry_off += 2u + 2u * (ck_u32)use_count;
        free(uses);
    }
    ck_write_u16_le(pairs + pairs_len, 0);
    ck_write_u16_le(pairs + pairs_len + 2u, 0);
    pairs_len += 4u;

    ext_rec_len = 2u + pairs_len + payload_len;
    total_bytes = dict_offset + dict_size + 3u + ext_rec_len;
    out->bytes = (ck_u8 *)calloc(1u, (unsigned int)total_bytes);
    if (!out->bytes) goto oom;
    out->size = total_bytes;

    out->bytes[0] = (ck_u8)CK_LIB_HEADER;
    ck_write_u16_le(out->bytes + 1u, (ck_u16)(page_size - 3u));
    ck_write_u32_le(out->bytes + 3u, dict_offset);
    ck_write_u16_le(out->bytes + 7u, blocks);
    out->bytes[9] = 0x00u;

    pos = (ck_u32)page_size;
    for (i = 0; i < member_count; ++i) {
        memcpy(out->bytes + pos, bodies[i], (unsigned int)body_lens[i]);
        pos += body_lens[i];
    }
    out->bytes[pos] = (ck_u8)CK_LIB_END;
    ck_write_u16_le(out->bytes + pos + 1u, (ck_u16)(end_rec_bytes - 3u));

    memcpy(out->bytes + dict_offset, dict, (unsigned int)dict_size);
    pos = dict_offset + dict_size;
    out->bytes[pos] = (ck_u8)CK_LIB_EXT;
    ck_write_u16_le(out->bytes + pos + 1u, (ck_u16)ext_rec_len);
    ck_write_u16_le(out->bytes + pos + 3u, (ck_u16)member_count);
    memcpy(out->bytes + pos + 5u, pairs, (unsigned int)pairs_len);
    if (payload_len > 0) {
        memcpy(out->bytes + pos + 5u + pairs_len, payload, (unsigned int)payload_len);
    }

    for (i = 0; i < member_count; ++i) {
        free(bodies[i]);
        for (j = 0; j < ext_counts[i]; ++j) free(externals[i][j]);
        free(externals[i]);
    }
    free(bodies);
    free(body_lens);
    free(externals);
    free(ext_counts);
    free(dict);
    free(pairs);
    free(payload);
    return 0;

oom:
    ck_set_error(err, 0, "out of memory");
fail:
    if (bodies) {
        for (i = 0; i < member_count; ++i) free(bodies[i]);
        free(bodies);
    }
    free(body_lens);
    if (externals) {
        for (i = 0; i < member_count; ++i) {
            for (j = 0; j < ext_counts[i]; ++j) free(externals[i][j]);
            free(externals[i]);
        }
        free(externals);
    }
    free(ext_counts);
    free(dict);
    free(pairs);
    free(payload);
    ck_lib_build_out_free(out);
    return -1;
}
