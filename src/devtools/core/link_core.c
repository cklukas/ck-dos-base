/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/link_core.h"

#if defined(M_I86) && !defined(_MSC_VER)
int sprintf(char *dst, const char *fmt, ...);
#else
#include <stdio.h>
#endif

static char *lk_strdup_n(const char *s, ck_usize n) {
    char *p = (char *)malloc((unsigned int)n + 1u);
    if (!p) return NULL;
    if (n > 0) memcpy(p, s, (unsigned int)n);
    p[n] = '\0';
    return p;
}

static char *lk_strdup(const char *s) {
    return lk_strdup_n(s ? s : "", s ? strlen(s) : 0u);
}

static void to_upper_buf(char *dst, const char *src, ck_usize cap) {
    ck_usize i = 0;
    while (src[i] && i + 1u < cap) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        dst[i++] = c;
    }
    dst[i] = '\0';
}

static ck_u32 align_of_u8(ck_u8 a) {
    switch (a) {
        case 1: return 1u;
        case 2: return 2u;
        case 3: return 16u;
        case 4: return 256u;
        case 5: return 4u;
        default: return 1u;
    }
}

static ck_u8 link_bucket_of(const char *name) {
    ck_usize len = strlen(name);
    ck_u16 h = (ck_u16)len;
    ck_usize i;
    for (i = len; i >= 1u; --i) {
        ck_u8 b = (ck_u8)((ck_u8)name[i - 1u] | 0x20u);
        h = (ck_u16)(h + ((ck_u16)b << (i & 3u)));
    }
    return (ck_u8)(h & 0xFFu);
}

/* ---- OMF module parser ---- */

static int read_var_len(const ck_u8 *buf, ck_u16 len, ck_u16 *pos, ck_u32 *out) {
    ck_u8 lead;
    if (*pos >= len) return -1;
    lead = buf[(*pos)++];
    if (lead < 0x80u) { *out = (ck_u32)lead; return 0; }
    if (lead == 0x81u && (ck_u32)(*pos) + 2u <= len) {
        *out = ck_read_u16_le(buf + *pos);
        *pos = (ck_u16)(*pos + 2u);
        return 0;
    }
    if (lead == 0x84u && (ck_u32)(*pos) + 3u <= len) {
        ck_u32 lo = ck_read_u16_le(buf + *pos);
        *out = lo | ((ck_u32)buf[*pos + 2] << 16);
        *pos = (ck_u16)(*pos + 3u);
        return 0;
    }
    if (lead == 0x88u && (ck_u32)(*pos) + 4u <= len) {
        *out = ck_read_u32_le(buf + *pos);
        *pos = (ck_u16)(*pos + 4u);
        return 0;
    }
    return -1;
}

static int read_fix_frame(const ck_u8 *buf, ck_u16 len, ck_u16 *pos,
                          ck_u8 method, ck_link_fixref *out) {
    ck_u16 idx = 0;
    memset(out, 0, sizeof(*out));
    switch (method) {
        case 0:
            if (ck_omf_read_index(buf, len, pos, &idx) != 0) return -1;
            out->kind = CK_FIX_SEGMENT;
            out->index = (ck_usize)(idx - 1u);
            return 0;
        case 1:
            if (ck_omf_read_index(buf, len, pos, &idx) != 0) return -1;
            out->kind = CK_FIX_GROUP;
            out->index = (ck_usize)(idx - 1u);
            return 0;
        case 2:
            if (ck_omf_read_index(buf, len, pos, &idx) != 0) return -1;
            out->kind = CK_FIX_EXTERNAL;
            out->index = (ck_usize)(idx - 1u);
            return 0;
        case 3:
            if ((ck_u32)(*pos) + 2u > len) return -1;
            out->kind = CK_FIX_FRAME_NUM;
            out->frame_number = ck_read_u16_le(buf + *pos);
            *pos = (ck_u16)(*pos + 2u);
            return 0;
        case 4: out->kind = CK_FIX_LOCATION; return 0;
        case 5: out->kind = CK_FIX_TARGET; return 0;
        case 6: out->kind = CK_FIX_NONE; return 0;
        default: return -1;
    }
}

static int read_fix_target(const ck_u8 *buf, ck_u16 len, ck_u16 *pos,
                           ck_u8 method, ck_link_fixref *out) {
    ck_u16 idx = 0;
    memset(out, 0, sizeof(*out));
    switch (method & 3u) {
        case 0:
            if (ck_omf_read_index(buf, len, pos, &idx) != 0) return -1;
            out->kind = CK_FIX_SEGMENT;
            out->index = (ck_usize)(idx - 1u);
            return 0;
        case 1:
            if (ck_omf_read_index(buf, len, pos, &idx) != 0) return -1;
            out->kind = CK_FIX_GROUP;
            out->index = (ck_usize)(idx - 1u);
            return 0;
        case 2:
            if (ck_omf_read_index(buf, len, pos, &idx) != 0) return -1;
            out->kind = CK_FIX_EXTERNAL;
            out->index = (ck_usize)(idx - 1u);
            return 0;
        case 3:
            if ((ck_u32)(*pos) + 2u > len) return -1;
            out->kind = CK_FIX_FRAME_NUM;
            out->frame_number = ck_read_u16_le(buf + *pos);
            *pos = (ck_u16)(*pos + 2u);
            return 0;
        default: return -1;
    }
}

typedef struct u8_vec {
    ck_u8 *data;
    ck_u32 len;
    ck_u32 cap;
} u8_vec;

static int u8_vec_push(u8_vec *v, const ck_u8 *p, ck_u32 n) {
    if (v->len + n > v->cap) {
        ck_u32 nc = v->cap ? v->cap * 2u : 64u;
        ck_u8 *np;
        while (nc < v->len + n) nc *= 2u;
        np = (ck_u8 *)realloc(v->data, (unsigned int)nc);
        if (!np) return -1;
        v->data = np;
        v->cap = nc;
    }
    if (n > 0) memcpy(v->data + v->len, p, (unsigned int)n);
    v->len += n;
    return 0;
}

typedef struct u32_vec {
    ck_u32 *data;
    ck_usize len;
    ck_usize cap;
} u32_vec;

static int u32_vec_push(u32_vec *v, ck_u32 val) {
    if (v->len == v->cap) {
        ck_usize nc = v->cap ? v->cap * 2u : 8u;
        ck_u32 *np = (ck_u32 *)realloc(v->data, (unsigned int)(nc * sizeof(ck_u32)));
        if (!np) return -1;
        v->data = np;
        v->cap = nc;
    }
    v->data[v->len++] = val;
    return 0;
}

static int expand_iterated(const ck_u8 *buf, ck_u16 len, ck_u16 *pos,
                           u8_vec *out, int depth, int is32) {
    ck_u32 repeat;
    ck_u16 blocks;
    if (depth > 16) return -1;
    if (is32) {
        if ((ck_u32)(*pos) + 4u > len) return -1;
        repeat = ck_read_u32_le(buf + *pos);
        *pos = (ck_u16)(*pos + 4u);
    } else {
        if ((ck_u32)(*pos) + 2u > len) return -1;
        repeat = ck_read_u16_le(buf + *pos);
        *pos = (ck_u16)(*pos + 2u);
    }
    if ((ck_u32)(*pos) + 2u > len) return -1;
    blocks = ck_read_u16_le(buf + *pos);
    *pos = (ck_u16)(*pos + 2u);
    if (blocks != 0) {
        ck_u32 mark = out->len;
        ck_u16 b;
        ck_u32 blen, k;
        for (b = 0; b < blocks; ++b) {
            if (expand_iterated(buf, len, pos, out, depth + 1, is32) != 0) return -1;
        }
        blen = out->len - mark;
        for (k = 1u; k < repeat; ++k) {
            ck_u8 *tmp = blen > 0 ? (ck_u8 *)malloc((unsigned int)blen) : NULL;
            if (blen > 0 && !tmp) return -1;
            if (blen > 0) memcpy(tmp, out->data + mark, (unsigned int)blen);
            if (u8_vec_push(out, tmp, blen) != 0) { free(tmp); return -1; }
            free(tmp);
        }
        return 0;
    }
    {
        ck_u8 cnt;
        ck_u32 k;
        if (*pos >= len) return -1;
        cnt = buf[(*pos)++];
        if ((ck_u32)(*pos) + cnt > len) return -1;
        for (k = 0; k < repeat; ++k) {
            if (u8_vec_push(out, buf + *pos, cnt) != 0) return -1;
        }
        *pos = (ck_u16)(*pos + cnt);
    }
    return 0;
}

static ck_u32 lidata_hits(const ck_u8 *raw, ck_u16 *pos, ck_u16 target,
                          ck_u32 expanded_base, u32_vec *hits, int is32) {
    ck_u32 repeat;
    ck_u16 blocks;
    if (is32) {
        repeat = ck_read_u32_le(raw + *pos);
        *pos = (ck_u16)(*pos + 4u);
    } else {
        repeat = ck_read_u16_le(raw + *pos);
        *pos = (ck_u16)(*pos + 2u);
    }
    blocks = ck_read_u16_le(raw + *pos);
    *pos = (ck_u16)(*pos + 2u);
    if (blocks == 0) {
        ck_u8 count = raw[*pos];
        ck_u16 data_start = (ck_u16)(*pos + 1u);
        *pos = (ck_u16)(*pos + 1u + count);
        if (target >= data_start && target < (ck_u16)(data_start + count)) {
            ck_u32 k;
            for (k = 0; k < repeat; ++k) {
                u32_vec_push(hits, expanded_base + k * count + (ck_u32)(target - data_start));
            }
        }
        return repeat * count;
    }
    {
        u32_vec inner;
        ck_u32 once = 0;
        ck_u16 b;
        ck_u32 k;
        ck_usize hi;
        memset(&inner, 0, sizeof(inner));
        for (b = 0; b < blocks; ++b) {
            once += lidata_hits(raw, pos, target, expanded_base + once, &inner, is32);
        }
        for (hi = 0; hi < inner.len; ++hi) {
            for (k = 0; k < repeat; ++k) {
                u32_vec_push(hits, inner.data[hi] + k * once);
            }
        }
        free(inner.data);
        return repeat * once;
    }
}

void ck_link_module_free(ck_link_module *mod) {
    ck_usize i;
    if (!mod) return;
    free(mod->name);
    if (mod->lnames) {
        for (i = 0; i < mod->lname_count; ++i) free(mod->lnames[i]);
        free(mod->lnames);
    }
    if (mod->segments) {
        for (i = 0; i < mod->seg_count; ++i) {
            free(mod->segments[i].name);
            free(mod->segments[i].class_name);
            free(mod->segments[i].overlay_name);
        }
        free(mod->segments);
    }
    if (mod->groups) {
        for (i = 0; i < mod->group_count; ++i) {
            free(mod->groups[i].name);
            free(mod->groups[i].segments);
        }
        free(mod->groups);
    }
    if (mod->publics) {
        for (i = 0; i < mod->pub_count; ++i) free(mod->publics[i].name);
        free(mod->publics);
    }
    if (mod->externals) {
        for (i = 0; i < mod->ext_count; ++i) free(mod->externals[i].name);
        free(mod->externals);
    }
    if (mod->data) {
        for (i = 0; i < mod->data_count; ++i) free(mod->data[i].bytes);
        free(mod->data);
    }
    free(mod->fixups);
    memset(mod, 0, sizeof(*mod));
}

int ck_omf_parse_module(const ck_omf_record *recs,
                        ck_usize rec_count,
                        ck_link_module *out_mod,
                        ck_error *err) {
    ck_usize ri;
    ck_usize lname_cap = 0, seg_cap = 0, grp_cap = 0, pub_cap = 0, ext_cap = 0, data_cap = 0, fix_cap = 0;
    ck_usize last_data_seg = CK_SIZE_NONE;
    ck_u32 last_data_off = 0;
    int last_was_lidata = 0;
    int last_lidata_is32 = 0;
    const ck_u8 *last_lidata_raw = NULL;
    ck_u16 last_lidata_raw_len = 0;
    ck_u32 mention_seq = 0;
    u32_vec typdef_comm;
    ck_link_fixref t_frames[4];
    ck_link_fixref t_targets[4];
    int t_frame_ok[4];
    int t_target_ok[4];

    memset(out_mod, 0, sizeof(*out_mod));
    memset(&typdef_comm, 0, sizeof(typdef_comm));
    memset(t_frames, 0, sizeof(t_frames));
    memset(t_targets, 0, sizeof(t_targets));
    memset(t_frame_ok, 0, sizeof(t_frame_ok));
    memset(t_target_ok, 0, sizeof(t_target_ok));

    for (ri = 0; ri < rec_count; ++ri) {
        const ck_omf_record *rec = &recs[ri];
        const ck_u8 *rb = rec->body;
        ck_u16 rlen = rec->body_len;
        ck_u16 pos = 0;
        ck_u8 base = (ck_u8)(rec->type & 0xFEu);
        char namebuf[256];

        if (base == 0x80u || base == 0x82u) {
            if (ck_omf_read_name(rb, rlen, &pos, namebuf, sizeof(namebuf)) != 0) goto bad;
            free(out_mod->name);
            out_mod->name = lk_strdup(namebuf);
            if (!out_mod->name) goto oom;
        } else if (base == 0x96u) {
            while (pos < rlen) {
                char *s;
                if (ck_omf_read_name(rb, rlen, &pos, namebuf, sizeof(namebuf)) != 0) goto bad;
                s = lk_strdup(namebuf);
                if (!s) goto oom;
                if (out_mod->lname_count == lname_cap) {
                    ck_usize nc = lname_cap ? lname_cap * 2u : 16u;
                    char **np = (char **)realloc(out_mod->lnames, (unsigned int)(nc * sizeof(char *)));
                    if (!np) { free(s); goto oom; }
                    out_mod->lnames = np;
                    lname_cap = nc;
                }
                out_mod->lnames[out_mod->lname_count++] = s;
            }
        } else if (base == 0x98u) {
            ck_link_segment *seg;
            ck_u8 acbp;
            ck_u16 ni, ci, oi;
            if (pos >= rlen) goto bad;
            acbp = rb[pos++];
            if (out_mod->seg_count == seg_cap) {
                ck_usize nc = seg_cap ? seg_cap * 2u : 8u;
                ck_link_segment *np = (ck_link_segment *)realloc(out_mod->segments, (unsigned int)(nc * sizeof(ck_link_segment)));
                if (!np) goto oom;
                out_mod->segments = np;
                seg_cap = nc;
            }
            seg = &out_mod->segments[out_mod->seg_count++];
            memset(seg, 0, sizeof(*seg));
            seg->alignment = (ck_u8)(acbp >> 5);
            seg->combine = (ck_u8)((acbp >> 2) & 7u);
            seg->big = (acbp & 0x02u) != 0;
            seg->use32 = (acbp & 0x01u) != 0;
            if (seg->alignment == 0) {
                if ((ck_u32)pos + 3u > rlen) goto bad;
                seg->absolute_frame = ck_read_u16_le(rb + pos);
                seg->absolute_offset = rb[pos + 2];
                pos += 3u;
            }
            if ((ck_u32)pos + 2u > rlen) goto bad;
            seg->length = ck_read_u16_le(rb + pos);
            pos += 2u;
            if (seg->big && seg->length == 0) seg->length = 0x10000UL;
            if (ck_omf_read_index(rb, rlen, &pos, &ni) != 0 ||
                ck_omf_read_index(rb, rlen, &pos, &ci) != 0 ||
                ck_omf_read_index(rb, rlen, &pos, &oi) != 0) goto bad;
            seg->name = lk_strdup((ni > 0 && ni <= out_mod->lname_count) ? out_mod->lnames[ni - 1u] : "");
            seg->class_name = lk_strdup((ci > 0 && ci <= out_mod->lname_count) ? out_mod->lnames[ci - 1u] : "");
            seg->overlay_name = lk_strdup((oi > 0 && oi <= out_mod->lname_count) ? out_mod->lnames[oi - 1u] : "");
            if (!seg->name || !seg->class_name || !seg->overlay_name) goto oom;
        } else if (base == 0x9Au) {
            ck_link_group *grp;
            ck_u16 ni;
            ck_usize gcap = 0;
            if (ck_omf_read_index(rb, rlen, &pos, &ni) != 0) goto bad;
            if (out_mod->group_count == grp_cap) {
                ck_usize nc = grp_cap ? grp_cap * 2u : 4u;
                ck_link_group *np = (ck_link_group *)realloc(out_mod->groups, (unsigned int)(nc * sizeof(ck_link_group)));
                if (!np) goto oom;
                out_mod->groups = np;
                grp_cap = nc;
            }
            grp = &out_mod->groups[out_mod->group_count++];
            memset(grp, 0, sizeof(*grp));
            grp->name = lk_strdup((ni > 0 && ni <= out_mod->lname_count) ? out_mod->lnames[ni - 1u] : "");
            if (!grp->name) goto oom;
            while (pos < rlen) {
                ck_u16 si;
                pos++;
                if (ck_omf_read_index(rb, rlen, &pos, &si) != 0) goto bad;
                if (grp->seg_count == gcap) {
                    ck_usize nc = gcap ? gcap * 2u : 8u;
                    ck_usize *np = (ck_usize *)realloc(grp->segments, (unsigned int)(nc * sizeof(ck_usize)));
                    if (!np) goto oom;
                    grp->segments = np;
                    gcap = nc;
                }
                grp->segments[grp->seg_count++] = (ck_usize)(si - 1u);
            }
        } else if (base == 0x90u || rec->type == 0xB6u || rec->type == 0xB7u) {
            int is_local = (rec->type == 0xB6u || rec->type == 0xB7u);
            ck_u16 gi, si, fnum = 0;
            if (ck_omf_read_index(rb, rlen, &pos, &gi) != 0 ||
                ck_omf_read_index(rb, rlen, &pos, &si) != 0) goto bad;
            if (si == 0) {
                if ((ck_u32)pos + 2u > rlen) goto bad;
                fnum = ck_read_u16_le(rb + pos);
                pos += 2u;
            }
            while (pos < rlen) {
                ck_link_public *pub;
                ck_u16 ti;
                if (ck_omf_read_name(rb, rlen, &pos, namebuf, sizeof(namebuf)) != 0) goto bad;
                if ((ck_u32)pos + 2u > rlen) goto bad;
                if (out_mod->pub_count == pub_cap) {
                    ck_usize nc = pub_cap ? pub_cap * 2u : 16u;
                    ck_link_public *np = (ck_link_public *)realloc(out_mod->publics, (unsigned int)(nc * sizeof(ck_link_public)));
                    if (!np) goto oom;
                    out_mod->publics = np;
                    pub_cap = nc;
                }
                pub = &out_mod->publics[out_mod->pub_count++];
                pub->name = lk_strdup(namebuf);
                if (!pub->name) goto oom;
                pub->offset = ck_read_u16_le(rb + pos);
                pos += 2u;
                if (ck_omf_read_index(rb, rlen, &pos, &ti) != 0) goto bad;
                pub->segment = (si == 0) ? CK_SIZE_NONE : (ck_usize)(si - 1u);
                pub->group = (gi == 0) ? CK_SIZE_NONE : (ck_usize)(gi - 1u);
                pub->frame = fnum;
                pub->local = is_local;
                pub->mention = mention_seq++;
            }
        } else if (base == 0x8Eu) {
            ck_u32 comm = 0;
            if ((ck_u32)rlen - pos >= 4u) {
                ck_omf_read_name(rb, rlen, &pos, namebuf, sizeof(namebuf));
                if ((ck_u32)pos + 2u <= rlen) {
                    ck_u8 leaf;
                    pos++; /* en */
                    leaf = rb[pos++];
                    if (namebuf[0] == '\0' && leaf == 0x62u && (ck_u32)rlen - pos >= 2u) {
                        ck_u8 num;
                        ck_u32 bits = 0;
                        pos++;
                        num = rb[pos++];
                        if (num <= 0x80u) bits = num;
                        else if (num == 0x81u && (ck_u32)pos + 2u <= rlen) { bits = ck_read_u16_le(rb + pos); pos += 2u; }
                        else if (num == 0x88u && (ck_u32)pos + 4u <= rlen) { bits = ck_read_u32_le(rb + pos); pos += 4u; }
                        comm = (bits + 7u) / 8u;
                    }
                }
            }
            if (u32_vec_push(&typdef_comm, comm) != 0) goto oom;
        } else if (base == 0x8Cu || rec->type == 0xB4u || rec->type == 0xB5u) {
            int is_local = (rec->type == 0xB4u || rec->type == 0xB5u);
            while (pos < rlen) {
                ck_link_external *ext;
                ck_u16 ti;
                if (ck_omf_read_name(rb, rlen, &pos, namebuf, sizeof(namebuf)) != 0) goto bad;
                if (ck_omf_read_index(rb, rlen, &pos, &ti) != 0) goto bad;
                if (out_mod->ext_count == ext_cap) {
                    ck_usize nc = ext_cap ? ext_cap * 2u : 16u;
                    ck_link_external *np = (ck_link_external *)realloc(out_mod->externals, (unsigned int)(nc * sizeof(ck_link_external)));
                    if (!np) goto oom;
                    out_mod->externals = np;
                    ext_cap = nc;
                }
                ext = &out_mod->externals[out_mod->ext_count++];
                memset(ext, 0, sizeof(*ext));
                ext->name = lk_strdup(namebuf);
                if (!ext->name) goto oom;
                ext->local = is_local;
                if (ti > 0 && ti <= typdef_comm.len && typdef_comm.data[ti - 1u] > 0) {
                    ext->communal = 1;
                    ext->communal_length = typdef_comm.data[ti - 1u];
                }
                ext->mention = mention_seq++;
            }
        } else if (base == 0xB0u || rec->type == 0xB8u) {
            while (pos < rlen) {
                ck_link_external *ext;
                ck_u16 ti;
                ck_u8 dt;
                if (ck_omf_read_name(rb, rlen, &pos, namebuf, sizeof(namebuf)) != 0) goto bad;
                if (ck_omf_read_index(rb, rlen, &pos, &ti) != 0) goto bad;
                if (pos >= rlen) goto bad;
                dt = rb[pos++];
                if (out_mod->ext_count == ext_cap) {
                    ck_usize nc = ext_cap ? ext_cap * 2u : 16u;
                    ck_link_external *np = (ck_link_external *)realloc(out_mod->externals, (unsigned int)(nc * sizeof(ck_link_external)));
                    if (!np) goto oom;
                    out_mod->externals = np;
                    ext_cap = nc;
                }
                ext = &out_mod->externals[out_mod->ext_count++];
                memset(ext, 0, sizeof(*ext));
                ext->name = lk_strdup(namebuf);
                if (!ext->name) goto oom;
                ext->communal = 1;
                if (dt == 0x61u) {
                    ck_u32 cnt, elem;
                    if (read_var_len(rb, rlen, &pos, &cnt) != 0 ||
                        read_var_len(rb, rlen, &pos, &elem) != 0) goto bad;
                    ext->communal_length = cnt * elem;
                } else {
                    if (read_var_len(rb, rlen, &pos, &ext->communal_length) != 0) goto bad;
                }
                ext->mention = mention_seq++;
            }
        } else if (base == 0xA0u) {
            int is32 = (rec->type & 1u) != 0;
            ck_u16 si;
            ck_u32 off, plen;
            ck_link_data *dc;
            if (ck_omf_read_index(rb, rlen, &pos, &si) != 0) goto bad;
            if (is32) {
                if ((ck_u32)pos + 4u > rlen) goto bad;
                off = ck_read_u32_le(rb + pos);
                pos += 4u;
            } else {
                if ((ck_u32)pos + 2u > rlen) goto bad;
                off = ck_read_u16_le(rb + pos);
                pos += 2u;
            }
            plen = (ck_u32)(rlen - pos);
            if (out_mod->data_count == data_cap) {
                ck_usize nc = data_cap ? data_cap * 2u : 16u;
                ck_link_data *np = (ck_link_data *)realloc(out_mod->data, (unsigned int)(nc * sizeof(ck_link_data)));
                if (!np) goto oom;
                out_mod->data = np;
                data_cap = nc;
            }
            dc = &out_mod->data[out_mod->data_count++];
            dc->segment = (ck_usize)(si - 1u);
            dc->offset = off;
            dc->len = plen;
            if (plen > 0) {
                dc->bytes = (ck_u8 *)malloc((unsigned int)plen);
                if (!dc->bytes) goto oom;
                memcpy(dc->bytes, rb + pos, (unsigned int)plen);
            } else {
                dc->bytes = NULL;
            }
            last_data_seg = dc->segment;
            last_data_off = dc->offset;
            last_was_lidata = 0;
        } else if (base == 0xA2u) {
            int is32 = (rec->type & 1u) != 0;
            ck_u16 si, rpos;
            ck_u32 off;
            u8_vec exp;
            ck_link_data *dc;
            if (ck_omf_read_index(rb, rlen, &pos, &si) != 0) goto bad;
            if (is32) {
                if ((ck_u32)pos + 4u > rlen) goto bad;
                off = ck_read_u32_le(rb + pos);
                pos += 4u;
            } else {
                if ((ck_u32)pos + 2u > rlen) goto bad;
                off = ck_read_u16_le(rb + pos);
                pos += 2u;
            }
            last_lidata_raw = rb + pos;
            last_lidata_raw_len = (ck_u16)(rlen - pos);
            last_lidata_is32 = is32;
            memset(&exp, 0, sizeof(exp));
            rpos = 0;
            while (rpos < last_lidata_raw_len) {
                if (expand_iterated(last_lidata_raw, last_lidata_raw_len, &rpos, &exp, 0, is32) != 0) {
                    free(exp.data);
                    goto bad;
                }
            }
            if (out_mod->data_count == data_cap) {
                ck_usize nc = data_cap ? data_cap * 2u : 16u;
                ck_link_data *np = (ck_link_data *)realloc(out_mod->data, (unsigned int)(nc * sizeof(ck_link_data)));
                if (!np) { free(exp.data); goto oom; }
                out_mod->data = np;
                data_cap = nc;
            }
            dc = &out_mod->data[out_mod->data_count++];
            dc->segment = (ck_usize)(si - 1u);
            dc->offset = off;
            dc->bytes = exp.data;
            dc->len = exp.len;
            last_data_seg = dc->segment;
            last_data_off = dc->offset;
            last_was_lidata = 1;
        } else if (base == 0x9Cu) {
            while (pos < rlen) {
                ck_u8 lead = rb[pos++];
                if ((lead & 0x80u) == 0) {
                    int is_frame = (lead & 0x40u) != 0;
                    ck_u8 method = (ck_u8)((lead >> 2) & 7u);
                    ck_u8 num = (ck_u8)(lead & 3u);
                    if (is_frame) {
                        if (read_fix_frame(rb, rlen, &pos, method, &t_frames[num]) != 0) goto bad;
                        t_frame_ok[num] = 1;
                    } else {
                        if (read_fix_target(rb, rlen, &pos, method, &t_targets[num]) != 0) goto bad;
                        t_target_ok[num] = 1;
                    }
                } else {
                    ck_link_fixup fx;
                    ck_u8 second, fix_data;
                    ck_u16 data_off;
                    int fthr, tthr, nodisp;
                    ck_u8 fmeth, tmeth;
                    if (pos >= rlen || last_data_seg == CK_SIZE_NONE) goto bad;
                    second = rb[pos++];
                    memset(&fx, 0, sizeof(fx));
                    fx.segment_relative = (lead & 0x40u) != 0;
                    fx.location = (ck_u8)((lead >> 2) & 0x0Fu);
                    data_off = (ck_u16)(((ck_u16)(lead & 3u) << 8) | second);
                    fx.segment = last_data_seg;
                    fx.offset = last_data_off + data_off;
                    if (pos >= rlen) goto bad;
                    fix_data = rb[pos++];
                    fthr = (fix_data & 0x80u) != 0;
                    fmeth = (ck_u8)((fix_data >> 4) & 7u);
                    tthr = (fix_data & 0x08u) != 0;
                    nodisp = (fix_data & 0x04u) != 0;
                    tmeth = (ck_u8)(fix_data & 3u);
                    if (fthr) {
                        ck_u8 tn = (ck_u8)((fix_data >> 4) & 3u);
                        if (!t_frame_ok[tn]) goto bad;
                        memcpy(&fx.frame, &t_frames[tn], sizeof(ck_link_fixref));
                    } else {
                        if (read_fix_frame(rb, rlen, &pos, fmeth, &fx.frame) != 0) goto bad;
                    }
                    if (tthr) {
                        ck_u8 tn = (ck_u8)(fix_data & 3u);
                        if (!t_target_ok[tn]) goto bad;
                        memcpy(&fx.target, &t_targets[tn], sizeof(ck_link_fixref));
                    } else {
                        if (read_fix_target(rb, rlen, &pos, tmeth, &fx.target) != 0) goto bad;
                    }
                    fx.has_displacement = !nodisp;
                    if (!nodisp) {
                        if ((ck_u32)pos + 2u > rlen) goto bad;
                        fx.displacement = ck_read_u16_le(rb + pos);
                        pos += 2u;
                    }
                    if (last_was_lidata) {
                        u32_vec hits;
                        ck_u16 rpos = 0;
                        ck_u32 exp_off = 0;
                        ck_usize hi;
                        memset(&hits, 0, sizeof(hits));
                        while (rpos < last_lidata_raw_len) {
                            exp_off += lidata_hits(last_lidata_raw, &rpos, data_off, exp_off, &hits, last_lidata_is32);
                        }
                        for (hi = 0; hi < hits.len; ++hi) {
                            if (out_mod->fixup_count == fix_cap) {
                                ck_usize nc = fix_cap ? fix_cap * 2u : 16u;
                                ck_link_fixup *np = (ck_link_fixup *)realloc(out_mod->fixups, (unsigned int)(nc * sizeof(ck_link_fixup)));
                                if (!np) { free(hits.data); goto oom; }
                                out_mod->fixups = np;
                                fix_cap = nc;
                            }
                            memcpy(&out_mod->fixups[out_mod->fixup_count], &fx, sizeof(ck_link_fixup));
                            out_mod->fixups[out_mod->fixup_count].offset = last_data_off + hits.data[hi];
                            out_mod->fixup_count++;
                        }
                        free(hits.data);
                    } else {
                        if (out_mod->fixup_count == fix_cap) {
                            ck_usize nc = fix_cap ? fix_cap * 2u : 16u;
                            ck_link_fixup *np = (ck_link_fixup *)realloc(out_mod->fixups, (unsigned int)(nc * sizeof(ck_link_fixup)));
                            if (!np) goto oom;
                            out_mod->fixups = np;
                            fix_cap = nc;
                        }
                        memcpy(&out_mod->fixups[out_mod->fixup_count++], &fx, sizeof(ck_link_fixup));
                    }
                }
            }
        } else if (base == 0x8Au) {
            ck_u8 mtype;
            if (pos >= rlen) goto bad;
            mtype = rb[pos++];
            out_mod->end.is_main = (mtype & 0x80u) != 0;
            out_mod->end.has_start = (mtype & 0x40u) != 0;
            if (out_mod->end.has_start) {
                ck_u8 edata;
                int fthr, tthr, nodisp;
                ck_u8 fmeth, tmeth;
                if (pos >= rlen) goto bad;
                edata = rb[pos++];
                fthr = (edata & 0x80u) != 0;
                fmeth = (ck_u8)((edata >> 4) & 7u);
                tthr = (edata & 0x08u) != 0;
                nodisp = (edata & 0x04u) != 0;
                tmeth = (ck_u8)(edata & 3u);
                if (fthr) {
                    ck_u8 tn = (ck_u8)((edata >> 4) & 3u);
                    if (!t_frame_ok[tn]) goto bad;
                    memcpy(&out_mod->end.start_frame, &t_frames[tn], sizeof(ck_link_fixref));
                } else {
                    if (read_fix_frame(rb, rlen, &pos, fmeth, &out_mod->end.start_frame) != 0) goto bad;
                }
                if (tthr) {
                    ck_u8 tn = (ck_u8)(edata & 3u);
                    if (!t_target_ok[tn]) goto bad;
                    memcpy(&out_mod->end.start_target, &t_targets[tn], sizeof(ck_link_fixref));
                } else {
                    if (read_fix_target(rb, rlen, &pos, tmeth, &out_mod->end.start_target) != 0) goto bad;
                }
                out_mod->end.start_has_displacement = !nodisp;
                if (!nodisp) {
                    if ((ck_u32)pos + 2u > rlen) goto bad;
                    out_mod->end.start_displacement = ck_read_u16_le(rb + pos);
                    pos += 2u;
                }
            }
        } else if (base == 0x88u) {
            if ((ck_u32)rlen - pos >= 2u && rb[pos + 1] == 0x9Eu) {
                out_mod->dosseg = 1;
            }
        }
    }

    free(typdef_comm.data);
    return 0;

oom:
    ck_set_error(err, 0, "out of memory");
    free(typdef_comm.data);
    ck_link_module_free(out_mod);
    return -1;

bad:
    ck_set_error(err, 0, "malformed OMF module record");
    free(typdef_comm.data);
    ck_link_module_free(out_mod);
    return -1;
}

/* ---- Linker core ---- */

#if !defined(CK_DOS_LEAN)
static const ck_u8 kExepackStub[] = {
    0x8C, 0xC0, 0x05, 0x10, 0x00, 0x0E, 0x1F, 0xA3, 0x04, 0x00, 0x03, 0x06,
    0x0C, 0x00, 0x8E, 0xC0, 0x8B, 0x0E, 0x06, 0x00, 0x8B, 0xF9, 0x4F, 0x8B,
    0xF7, 0xFD, 0xF3, 0xA4, 0x50, 0xB8, 0x32, 0x00, 0x50, 0xCB, 0x8C, 0xC3,
    0x8C, 0xD8, 0x48, 0x8E, 0xD8, 0x8E, 0xC0, 0xBF, 0x0F, 0x00, 0xB9, 0x10,
    0x00, 0xB0, 0xFF, 0xF3, 0xAE, 0x47, 0x8B, 0xF7, 0x8B, 0xC3, 0x48, 0x8E,
    0xC0, 0xBF, 0x0F, 0x00, 0xB1, 0x04, 0x8B, 0xC6, 0xF7, 0xD0, 0xD3, 0xE8,
    0x74, 0x09, 0x8C, 0xDA, 0x2B, 0xD0, 0x8E, 0xDA, 0x83, 0xCE, 0xF0, 0x8B,
    0xC7, 0xF7, 0xD0, 0xD3, 0xE8, 0x74, 0x09, 0x8C, 0xC2, 0x2B, 0xD0, 0x8E,
    0xC2, 0x83, 0xCF, 0xF0, 0xAC, 0x8A, 0xD0, 0x4E, 0xAD, 0x8B, 0xC8, 0x46,
    0x8A, 0xC2, 0x24, 0xFE, 0x3C, 0xB0, 0x75, 0x06, 0xAC, 0xF3, 0xAA, 0xEB,
    0x07, 0x90, 0x3C, 0xB2, 0x75, 0x6B, 0xF3, 0xA4, 0x8A, 0xC2, 0xA8, 0x01,
    0x74, 0xBA, 0xBE, 0x25, 0x01, 0x0E, 0x1F, 0x8B, 0x1E, 0x04, 0x00, 0xFC,
    0x33, 0xD2, 0xAD, 0x8B, 0xC8, 0xE3, 0x13, 0x8B, 0xC2, 0x03, 0xC3, 0x8E,
    0xC0, 0xAD, 0x8B, 0xF8, 0x83, 0xFF, 0xFF, 0x74, 0x11, 0x26, 0x01, 0x1D,
    0xE2, 0xF3, 0x81, 0xFA, 0x00, 0xF0, 0x74, 0x16, 0x81, 0xC2, 0x00, 0x10,
    0xEB, 0xDC, 0x8C, 0xC0, 0x40, 0x8E, 0xC0, 0x83, 0xEF, 0x10, 0x26, 0x01,
    0x1D, 0x48, 0x8E, 0xC0, 0xEB, 0xE2, 0x8B, 0xC3, 0x8B, 0x3E, 0x08, 0x00,
    0x8B, 0x36, 0x0A, 0x00, 0x03, 0xF0, 0x01, 0x06, 0x02, 0x00, 0x2D, 0x10,
    0x00, 0x8E, 0xD8, 0x8E, 0xC0, 0xBB, 0x00, 0x00, 0xFA, 0x8E, 0xD6, 0x8B,
    0xE7, 0xFB, 0x2E, 0xFF, 0x2F, 0xB4, 0x40, 0xBB, 0x02, 0x00, 0xB9, 0x16,
    0x00, 0x8C, 0xCA, 0x8E, 0xDA, 0xBA, 0x0F, 0x01, 0xCD, 0x21, 0xB8, 0xFF,
    0x4C, 0xCD, 0x21, 0x50, 0x61, 0x63, 0x6B, 0x65, 0x64, 0x20, 0x66, 0x69,
    0x6C, 0x65, 0x20, 0x69, 0x73, 0x20, 0x63, 0x6F, 0x72, 0x72, 0x75, 0x70,
    0x74
};
#endif

typedef struct lk_place {
    int absolute;
    ck_u16 absolute_frame;
    ck_usize combined;
    ck_u32 offset;
} lk_place;

typedef struct lk_combined {
    char *name;
    char *class_name;
    ck_u8 alignment;
    ck_u32 length;
    ck_u32 base;
    ck_u16 frame;
    int has_data;
} lk_combined;

typedef struct lk_group {
    char *name;
    ck_u16 frame;
} lk_group;

typedef struct lk_pub {
    char *key;
    char *name;
    int absolute;
    ck_u16 frame;
    ck_u16 offset;
    ck_u32 linear;
    ck_u32 mention_rank;
} lk_pub;

typedef struct lk_reloc {
    ck_u32 linear;
    ck_u16 frame;
} lk_reloc;

typedef struct lk_str_u32 {
    char *key;
    ck_u32 val;
} lk_str_u32;

typedef struct lk_str_str {
    char *key;
    char *val;
} lk_str_str;

static lk_pub *find_pub(lk_pub *arr, ck_usize cnt, const char *key) {
    ck_usize i;
    for (i = 0; i < cnt; ++i) {
        if (strcmp(arr[i].key, key) == 0) return &arr[i];
    }
    return NULL;
}

static const char *find_str_str(const lk_str_str *arr, ck_usize cnt, const char *key) {
    ck_usize i;
    for (i = 0; i < cnt; ++i) {
        if (strcmp(arr[i].key, key) == 0) return arr[i].val;
    }
    return NULL;
}

static int add_str_str(lk_str_str **arr, ck_usize *cnt, ck_usize *cap,
                       const char *key, const char *val) {
    if (find_str_str(*arr, *cnt, key)) return 0;
    if (*cnt == *cap) {
        ck_usize nc = *cap ? (*cap) * 2u : 16u;
        lk_str_str *np = (lk_str_str *)realloc(*arr, (unsigned int)(nc * sizeof(lk_str_str)));
        if (!np) return -1;
        *arr = np;
        *cap = nc;
    }
    (*arr)[*cnt].key = lk_strdup(key);
    (*arr)[*cnt].val = lk_strdup(val);
    if (!(*arr)[*cnt].key || !(*arr)[*cnt].val) return -1;
    (*cnt)++;
    return 0;
}

static void link_qsort_pubs(const lk_pub **a, long lo, long hi) {
    while (lo < hi) {
        ck_u32 pivot = a[lo]->linear;
        long di = lo;
        long si = hi + 1;
        for (;;) {
            do { ++di; } while (di < hi && a[di]->linear <= pivot);
            do { --si; } while (si > lo && a[si]->linear >= pivot);
            if (si > di) {
                const lk_pub *tmp = a[di];
                a[di] = a[si];
                a[si] = tmp;
            } else {
                break;
            }
        }
        {
            const lk_pub *tmp = a[lo];
            a[lo] = a[si];
            a[si] = tmp;
        }
        if (si - lo < hi - si) {
            link_qsort_pubs(a, lo, si - 1);
            lo = si + 1;
        } else {
            link_qsort_pubs(a, si + 1, hi);
            hi = si - 1;
        }
    }
}

typedef struct map_str {
    char *data;
    ck_u32 len;
    ck_u32 cap;
} map_str;

static int ms_append_n(map_str *s, const char *p, ck_u32 n) {
    if (s->len + n + 1u > s->cap) {
        ck_u32 nc = s->cap ? s->cap * 2u : 256u;
        char *np;
        while (nc < s->len + n + 1u) nc *= 2u;
        np = (char *)realloc(s->data, (unsigned int)nc);
        if (!np) return -1;
        s->data = np;
        s->cap = nc;
    }
    if (n > 0) memcpy(s->data + s->len, p, (unsigned int)n);
    s->len += n;
    s->data[s->len] = '\0';
    return 0;
}

static int ms_append(map_str *s, const char *p) {
    return ms_append_n(s, p, (ck_u32)strlen(p));
}

static int ends_with_ci(const char *s, const char *suf) {
    ck_usize ls = strlen(s);
    ck_usize lf = strlen(suf);
    if (ls < lf) return 0;
    return strcmpi(s + ls - lf, suf) == 0;
}

static int dosseg_bucket(const lk_combined *c, const int *is_dgroup, ck_usize idx) {
    if (ends_with_ci(c->class_name, "CODE")) return 0;
    if (!is_dgroup[idx]) return 1;
    if (strcmpi(c->class_name, "BEGDATA") == 0) return 2;
    if (strcmpi(c->class_name, "BSS") == 0) return 4;
    if (strcmpi(c->class_name, "STACK") == 0) return 6;
    return 3;
}

int ck_link_modules(const ck_link_module *modules,
                    ck_usize mod_count,
                    int map_publics,
                    ck_u32 stack_override,
                    int exepack,
                    const ck_usize *placement_order,
                    ck_usize placement_count,
                    ck_u8 **out_exe,
                    ck_u32 *out_exe_size,
                    char **out_map,
                    ck_u32 *out_map_size,
                    ck_error *err) {
    lk_place **placements = NULL;
    lk_combined *combined = NULL;
    ck_usize comb_count = 0, comb_cap = 0;
    int dosseg = 0;
    lk_str_u32 *comm_sizes = NULL;
    ck_usize comm_cnt = 0, comm_cap = 0;
    ck_usize *comm_order = NULL;
    ck_usize comm_ord_cnt = 0;
    ck_usize c_common_idx = CK_SIZE_NONE;
    lk_str_u32 *comm_layout = NULL;
    ck_usize comm_lay_cnt = 0;
    lk_group *groups = NULL;
    ck_usize grp_cnt = 0, grp_cap = 0;
    ck_usize **mod_groups = NULL;
    lk_str_str *first_spell = NULL;
    ck_usize fs_cnt = 0, fs_cap = 0;
    lk_pub *publics = NULL;
    ck_usize pub_cnt = 0, pub_cap = 0;
    lk_pub **local_pubs = NULL;
    ck_usize *local_pub_cnts = NULL;
    ck_u8 *image = NULL;
    ck_u32 image_size = 0;
    lk_reloc *relocs = NULL;
    ck_usize reloc_cnt = 0, reloc_cap = 0;
    ck_u8 *exe = NULL;
    ck_u32 file_size = 0;
    map_str map;
    ck_usize i, m, s;
    ck_u16 start_cs = 0, start_ip = 0, stack_ss = 0, stack_sp = 0;
    int have_start = 0;
    ck_u32 kept = 0, reloc_bytes, header_size, memory_end;
    ck_u16 sum = 0;
    char keybuf[256];

    *out_exe = NULL;
    *out_exe_size = 0;
    if (out_map) { *out_map = NULL; *out_map_size = 0; }
    memset(&map, 0, sizeof(map));

    placements = (lk_place **)calloc(mod_count ? (unsigned int)mod_count : 1u, sizeof(lk_place *));
    mod_groups = (ck_usize **)calloc(mod_count ? (unsigned int)mod_count : 1u, sizeof(ck_usize *));
    local_pubs = (lk_pub **)calloc(mod_count ? (unsigned int)mod_count : 1u, sizeof(lk_pub *));
    local_pub_cnts = (ck_usize *)calloc(mod_count ? (unsigned int)mod_count : 1u, sizeof(ck_usize));
    if (!placements || !mod_groups || !local_pubs || !local_pub_cnts) goto oom;

    for (m = 0; m < mod_count; ++m) {
        if (modules[m].seg_count > 0) {
            placements[m] = (lk_place *)calloc((unsigned int)modules[m].seg_count, sizeof(lk_place));
            if (!placements[m]) goto oom;
        }
        if (modules[m].dosseg) dosseg = 1;
    }

    /* Combine segments */
    {
        ck_usize step_cnt = (placement_count > 0) ? placement_count : mod_count;
        ck_usize step;
        for (step = 0; step < step_cnt; ++step) {
            m = (placement_count > 0) ? placement_order[step] : step;
            for (s = 0; s < modules[m].seg_count; ++s) {
                const ck_link_segment *seg = &modules[m].segments[s];
                lk_place *pl = &placements[m][s];
                ck_usize cidx = comb_count;
                ck_u32 align;
                if (seg->alignment == 0) {
                    pl->absolute = 1;
                    pl->absolute_frame = seg->absolute_frame;
                    continue;
                }
                if (seg->combine != 0) {
                    ck_usize c;
                    for (c = 0; c < comb_count; ++c) {
                        if (strcmp(combined[c].name, seg->name) == 0 &&
                            strcmp(combined[c].class_name, seg->class_name) == 0) {
                            cidx = c;
                            break;
                        }
                    }
                }
                if (cidx == comb_count) {
                    lk_combined *nc;
                    if (comb_count == comb_cap) {
                        ck_usize ncap = comb_cap ? comb_cap * 2u : 16u;
                        lk_combined *np = (lk_combined *)realloc(combined, (unsigned int)(ncap * sizeof(lk_combined)));
                        if (!np) goto oom;
                        combined = np;
                        comb_cap = ncap;
                    }
                    nc = &combined[comb_count++];
                    memset(nc, 0, sizeof(*nc));
                    nc->name = lk_strdup(seg->name);
                    nc->class_name = lk_strdup(seg->class_name);
                    nc->alignment = 1;
                    if (!nc->name || !nc->class_name) goto oom;
                }
                if (align_of_u8(seg->alignment) > align_of_u8(combined[cidx].alignment)) {
                    combined[cidx].alignment = seg->alignment;
                }
                pl->combined = cidx;
                if (seg->combine == 6) {
                    pl->offset = 0;
                    if (seg->length > combined[cidx].length) combined[cidx].length = seg->length;
                } else if (seg->combine == 5 && combined[cidx].length > 0) {
                    pl->offset = combined[cidx].length;
                    combined[cidx].length += seg->length;
                } else {
                    align = align_of_u8(seg->alignment);
                    combined[cidx].length = (combined[cidx].length + align - 1u) & ~(align - 1u);
                    pl->offset = combined[cidx].length;
                    combined[cidx].length += seg->length;
                }
            }
        }
    }

    /* Communals */
    for (m = 0; m < mod_count; ++m) {
        for (i = 0; i < modules[m].ext_count; ++i) {
            const ck_link_external *ex = &modules[m].externals[i];
            int is_def = 0;
            ck_usize dm, dp, ci;
            if (ex->communal_length == 0) continue;
            to_upper_buf(keybuf, ex->name, sizeof(keybuf));
            for (dm = 0; dm < mod_count && !is_def; ++dm) {
                for (dp = 0; dp < modules[dm].pub_count; ++dp) {
                    if (!modules[dm].publics[dp].local && strcmpi(modules[dm].publics[dp].name, keybuf) == 0) {
                        is_def = 1;
                        break;
                    }
                }
            }
            if (is_def) continue;
            for (ci = 0; ci < comm_cnt; ++ci) {
                if (strcmp(comm_sizes[ci].key, keybuf) == 0) break;
            }
            if (ci == comm_cnt) {
                if (comm_cnt == comm_cap) {
                    ck_usize nc = comm_cap ? comm_cap * 2u : 16u;
                    lk_str_u32 *np = (lk_str_u32 *)realloc(comm_sizes, (unsigned int)(nc * sizeof(lk_str_u32)));
                    if (!np) goto oom;
                    comm_sizes = np;
                    comm_cap = nc;
                }
                comm_sizes[comm_cnt].key = lk_strdup(keybuf);
                comm_sizes[comm_cnt].val = ex->communal_length;
                if (!comm_sizes[comm_cnt].key) goto oom;
                comm_cnt++;
            } else if (ex->communal_length > comm_sizes[ci].val) {
                comm_sizes[ci].val = ex->communal_length;
            }
        }
    }

    if (comm_cnt > 0) {
        ck_u16 b;
        ck_usize ci;
        comm_order = (ck_usize *)malloc((unsigned int)(comm_cnt * sizeof(ck_usize)));
        if (!comm_order) goto oom;
        for (b = 0; b < 256u; ++b) {
            for (ci = comm_cnt; ci > 0; --ci) {
                if (link_bucket_of(comm_sizes[ci - 1u].key) == (ck_u8)b) {
                    comm_order[comm_ord_cnt++] = ci - 1u;
                }
            }
        }
    }

    /* DOSSEG reorder */
    if (dosseg) {
        int *is_dg = (int *)calloc(comb_count ? (unsigned int)comb_count : 1u, sizeof(int));
        ck_usize *order = (ck_usize *)malloc(comb_count ? (unsigned int)(comb_count * sizeof(ck_usize)) : 1u);
        ck_usize *remap = (ck_usize *)malloc(comb_count ? (unsigned int)(comb_count * sizeof(ck_usize)) : 1u);
        lk_combined *reord = NULL;
        ck_usize reord_cnt = 0;
        ck_usize pos;
        int has_dgroup = 0;
        if (!is_dg || !order || !remap) { free(is_dg); free(order); free(remap); goto oom; }
        for (m = 0; m < mod_count; ++m) {
            for (i = 0; i < modules[m].group_count; ++i) {
                if (strcmpi(modules[m].groups[i].name, "DGROUP") == 0) {
                    ck_usize k;
                    has_dgroup = 1;
                    for (k = 0; k < modules[m].groups[i].seg_count; ++k) {
                        const lk_place *pl = &placements[m][modules[m].groups[i].segments[k]];
                        if (!pl->absolute) is_dg[pl->combined] = 1;
                    }
                }
            }
        }
        for (i = 0; i < comb_count; ++i) order[i] = i;
        /* Stable insertion sort by (bucket, class_rank) */
        for (i = 1; i < comb_count; ++i) {
            ck_usize key = order[i];
            int kb = dosseg_bucket(&combined[key], is_dg, key);
            ck_usize j = i;
            while (j > 0) {
                ck_usize prev = order[j - 1u];
                int pb = dosseg_bucket(&combined[prev], is_dg, prev);
                int swap = 0;
                if (pb != kb) {
                    swap = (pb > kb);
                } else if (pb == 3 && strcmpi(combined[prev].class_name, combined[key].class_name) != 0) {
                    /* First appearance rank of class_name among bucket 3 */
                    ck_usize r1 = comb_count, r2 = comb_count, t;
                    for (t = 0; t < comb_count; ++t) {
                        if (dosseg_bucket(&combined[t], is_dg, t) != 3) continue;
                        if (r1 == comb_count && strcmpi(combined[t].class_name, combined[prev].class_name) == 0) r1 = t;
                        if (r2 == comb_count && strcmpi(combined[t].class_name, combined[key].class_name) == 0) r2 = t;
                    }
                    swap = (r1 > r2);
                }
                if (!swap) break;
                order[j] = prev;
                --j;
            }
            order[j] = key;
        }
        reord = (lk_combined *)malloc((unsigned int)((comb_count + (comm_ord_cnt > 0 ? 1u : 0u)) * sizeof(lk_combined)));
        if (!reord) { free(is_dg); free(order); free(remap); goto oom; }
        for (pos = 0; pos < comb_count; ++pos) {
            if (comm_ord_cnt > 0 && c_common_idx == CK_SIZE_NONE &&
                dosseg_bucket(&combined[order[pos]], is_dg, order[pos]) > 4) {
                c_common_idx = reord_cnt;
                memset(&reord[reord_cnt], 0, sizeof(lk_combined));
                reord[reord_cnt].name = lk_strdup("c_common");
                reord[reord_cnt].class_name = lk_strdup("BSS");
                reord[reord_cnt].alignment = 3;
                reord_cnt++;
            }
            remap[order[pos]] = reord_cnt;
            memcpy(&reord[reord_cnt++], &combined[order[pos]], sizeof(lk_combined));
        }
        if (comm_ord_cnt > 0 && c_common_idx == CK_SIZE_NONE) {
            c_common_idx = reord_cnt;
            memset(&reord[reord_cnt], 0, sizeof(lk_combined));
            reord[reord_cnt].name = lk_strdup("c_common");
            reord[reord_cnt].class_name = lk_strdup("BSS");
            reord[reord_cnt].alignment = 3;
            reord_cnt++;
        }
        free(combined);
        combined = reord;
        comb_count = reord_cnt;
        comb_cap = reord_cnt;
        for (m = 0; m < mod_count; ++m) {
            for (s = 0; s < modules[m].seg_count; ++s) {
                if (!placements[m][s].absolute) placements[m][s].combined = remap[placements[m][s].combined];
            }
        }
        if (has_dgroup) {
            for (i = 0; i < comb_count; ++i) {
                if (!ends_with_ci(combined[i].class_name, "CODE")) continue;
                combined[i].length += 16u;
                for (m = 0; m < mod_count; ++m) {
                    for (s = 0; s < modules[m].seg_count; ++s) {
                        if (!placements[m][s].absolute && placements[m][s].combined == i) {
                            placements[m][s].offset += 16u;
                        }
                    }
                }
                break;
            }
        }
        free(is_dg);
        free(order);
        free(remap);
    } else if (comm_ord_cnt > 0) {
        if (comb_count == comb_cap) {
            ck_usize ncap = comb_cap ? comb_cap * 2u : 8u;
            lk_combined *np = (lk_combined *)realloc(combined, (unsigned int)(ncap * sizeof(lk_combined)));
            if (!np) goto oom;
            combined = np;
            comb_cap = ncap;
        }
        c_common_idx = comb_count++;
        memset(&combined[c_common_idx], 0, sizeof(lk_combined));
        combined[c_common_idx].name = lk_strdup("c_common");
        combined[c_common_idx].class_name = lk_strdup("BSS");
        combined[c_common_idx].alignment = 3;
    }

    if (comm_ord_cnt > 0 && c_common_idx != CK_SIZE_NONE) {
        comm_layout = (lk_str_u32 *)malloc((unsigned int)(comm_ord_cnt * sizeof(lk_str_u32)));
        if (!comm_layout) goto oom;
        for (i = 0; i < comm_ord_cnt; ++i) {
            ck_usize ci = comm_order[i];
            ck_u32 sz = comm_sizes[ci].val;
            if ((sz % 2u) == 0) combined[c_common_idx].length = (combined[c_common_idx].length + 1u) & ~1u;
            comm_layout[comm_lay_cnt].key = comm_sizes[ci].key;
            comm_layout[comm_lay_cnt].val = combined[c_common_idx].length;
            comm_lay_cnt++;
            combined[c_common_idx].length += sz;
        }
    }

    for (i = 0; i < comb_count; ++i) {
        ck_u32 align = align_of_u8(combined[i].alignment);
        image_size = (image_size + align - 1u) & ~(align - 1u);
        combined[i].base = image_size;
        combined[i].frame = (ck_u16)(image_size / 16u);
        image_size += combined[i].length;
    }

    /* Groups */
    for (m = 0; m < mod_count; ++m) {
        if (modules[m].group_count > 0) {
            mod_groups[m] = (ck_usize *)malloc((unsigned int)(modules[m].group_count * sizeof(ck_usize)));
            if (!mod_groups[m]) goto oom;
        }
        for (i = 0; i < modules[m].group_count; ++i) {
            const ck_link_group *mg = &modules[m].groups[i];
            ck_usize gi = grp_cnt, k;
            for (k = 0; k < grp_cnt; ++k) {
                if (strcmp(groups[k].name, mg->name) == 0) { gi = k; break; }
            }
            if (gi == grp_cnt) {
                if (grp_cnt == grp_cap) {
                    ck_usize nc = grp_cap ? grp_cap * 2u : 8u;
                    lk_group *np = (lk_group *)realloc(groups, (unsigned int)(nc * sizeof(lk_group)));
                    if (!np) goto oom;
                    groups = np;
                    grp_cap = nc;
                }
                groups[grp_cnt].name = lk_strdup(mg->name);
                groups[grp_cnt].frame = 0xFFFFu;
                if (!groups[grp_cnt].name) goto oom;
                grp_cnt++;
            }
            mod_groups[m][i] = gi;
            for (k = 0; k < mg->seg_count; ++k) {
                const lk_place *pl = &placements[m][mg->segments[k]];
                if (!pl->absolute && combined[pl->combined].frame < groups[gi].frame) {
                    groups[gi].frame = combined[pl->combined].frame;
                }
            }
        }
    }

    /* First spelling */
    for (m = 0; m < mod_count; ++m) {
        for (i = 0; i < modules[m].ext_count; ++i) {
            to_upper_buf(keybuf, modules[m].externals[i].name, sizeof(keybuf));
            if (add_str_str(&first_spell, &fs_cnt, &fs_cap, keybuf, modules[m].externals[i].name) != 0) goto oom;
        }
        for (i = 0; i < modules[m].pub_count; ++i) {
            if (!modules[m].publics[i].local) {
                to_upper_buf(keybuf, modules[m].publics[i].name, sizeof(keybuf));
                if (add_str_str(&first_spell, &fs_cnt, &fs_cap, keybuf, modules[m].publics[i].name) != 0) goto oom;
            }
        }
    }

    /* Publics */
    for (m = 0; m < mod_count; ++m) {
        for (i = 0; i < modules[m].pub_count; ++i) {
            const ck_link_public *item = &modules[m].publics[i];
            lk_pub entry;
            lk_pub *hit;
            const char *sp;
            if (item->local) continue;
            memset(&entry, 0, sizeof(entry));
            to_upper_buf(keybuf, item->name, sizeof(keybuf));
            sp = find_str_str(first_spell, fs_cnt, keybuf);
            entry.name = lk_strdup(sp ? sp : item->name);
            entry.key = lk_strdup(keybuf);
            if (!entry.name || !entry.key) { free(entry.name); free(entry.key); goto oom; }
            if (item->segment == CK_SIZE_NONE) {
                entry.absolute = 1;
                entry.frame = item->frame;
                entry.offset = (ck_u16)item->offset;
                entry.linear = (ck_u32)item->frame * 16u + item->offset;
            } else {
                const lk_place *pl = &placements[m][item->segment];
                if (pl->absolute) {
                    entry.absolute = 1;
                    entry.frame = pl->absolute_frame;
                    entry.offset = (ck_u16)item->offset;
                    entry.linear = (ck_u32)pl->absolute_frame * 16u + item->offset;
                } else {
                    const lk_combined *home = &combined[pl->combined];
                    ck_u32 lin = home->base + pl->offset + item->offset;
                    entry.frame = (item->group != CK_SIZE_NONE) ? groups[mod_groups[m][item->group]].frame : home->frame;
                    entry.offset = (ck_u16)(lin - (ck_u32)entry.frame * 16u);
                    entry.linear = lin;
                }
            }
            hit = find_pub(publics, pub_cnt, keybuf);
            if (hit) {
                free(hit->key); free(hit->name);
                memcpy(hit, &entry, sizeof(lk_pub));
            } else {
                if (pub_cnt == pub_cap) {
                    ck_usize nc = pub_cap ? pub_cap * 2u : 32u;
                    lk_pub *np = (lk_pub *)realloc(publics, (unsigned int)(nc * sizeof(lk_pub)));
                    if (!np) { free(entry.name); free(entry.key); goto oom; }
                    publics = np;
                    pub_cap = nc;
                }
                memcpy(&publics[pub_cnt++], &entry, sizeof(lk_pub));
            }
        }
    }

    /* Local publics */
    for (m = 0; m < mod_count; ++m) {
        ck_usize lcap = 0;
        for (i = 0; i < modules[m].pub_count; ++i) {
            const ck_link_public *item = &modules[m].publics[i];
            lk_pub entry;
            const lk_place *pl;
            if (!item->local || item->segment == CK_SIZE_NONE) continue;
            pl = &placements[m][item->segment];
            memset(&entry, 0, sizeof(entry));
            to_upper_buf(keybuf, item->name, sizeof(keybuf));
            entry.key = lk_strdup(keybuf);
            entry.name = lk_strdup(item->name);
            if (!entry.key || !entry.name) { free(entry.key); free(entry.name); goto oom; }
            if (pl->absolute) {
                entry.absolute = 1;
                entry.frame = pl->absolute_frame;
                entry.offset = (ck_u16)item->offset;
                entry.linear = (ck_u32)pl->absolute_frame * 16u + item->offset;
            } else {
                const lk_combined *home = &combined[pl->combined];
                ck_u32 lin = home->base + pl->offset + item->offset;
                entry.frame = (item->group != CK_SIZE_NONE) ? groups[mod_groups[m][item->group]].frame : home->frame;
                entry.offset = (ck_u16)(lin - (ck_u32)entry.frame * 16u);
                entry.linear = lin;
            }
            if (local_pub_cnts[m] == lcap) {
                ck_usize nc = lcap ? lcap * 2u : 8u;
                lk_pub *np = (lk_pub *)realloc(local_pubs[m], (unsigned int)(nc * sizeof(lk_pub)));
                if (!np) { free(entry.key); free(entry.name); goto oom; }
                local_pubs[m] = np;
                lcap = nc;
            }
            memcpy(&local_pubs[m][local_pub_cnts[m]++], &entry, sizeof(lk_pub));
        }
    }

    /* Linker-generated _edata, _end, communals */
    if (dosseg) {
        ck_u16 dg_frame = 0;
        ck_u32 bss_start = 0, bss_end = 0;
        int seen_bss = 0;
        for (i = 0; i < grp_cnt; ++i) {
            if (strcmpi(groups[i].name, "DGROUP") == 0) dg_frame = groups[i].frame;
        }
        for (i = 0; i < comb_count; ++i) {
            if (strcmpi(combined[i].class_name, "BSS") != 0) continue;
            if (!seen_bss) bss_start = combined[i].base;
            seen_bss = 1;
            bss_end = combined[i].base + combined[i].length;
        }
        {
            const char *names[2];
            ck_u32 lins[2];
            int k;
            names[0] = "_edata"; names[1] = "_end";
            lins[0] = bss_start; lins[1] = (bss_end + 15u) & ~15u;
            for (k = 0; k < 2; ++k) {
                to_upper_buf(keybuf, names[k], sizeof(keybuf));
                if (!find_pub(publics, pub_cnt, keybuf)) {
                    lk_pub entry;
                    const char *sp = find_str_str(first_spell, fs_cnt, keybuf);
                    memset(&entry, 0, sizeof(entry));
                    entry.key = lk_strdup(keybuf);
                    entry.name = lk_strdup(sp ? sp : names[k]);
                    if (!entry.key || !entry.name) { free(entry.key); free(entry.name); goto oom; }
                    if (seen_bss) {
                        entry.frame = dg_frame;
                        entry.linear = lins[k];
                        entry.offset = (ck_u16)(lins[k] - (ck_u32)dg_frame * 16u);
                    } else {
                        entry.absolute = 1;
                    }
                    if (pub_cnt == pub_cap) {
                        ck_usize nc = pub_cap ? pub_cap * 2u : 32u;
                        lk_pub *np = (lk_pub *)realloc(publics, (unsigned int)(nc * sizeof(lk_pub)));
                        if (!np) { free(entry.key); free(entry.name); goto oom; }
                        publics = np;
                        pub_cap = nc;
                    }
                    memcpy(&publics[pub_cnt++], &entry, sizeof(lk_pub));
                }
            }
        }
        for (i = 0; i < comm_lay_cnt; ++i) {
            to_upper_buf(keybuf, comm_layout[i].key, sizeof(keybuf));
            if (!find_pub(publics, pub_cnt, keybuf)) {
                lk_pub entry;
                const char *sp = find_str_str(first_spell, fs_cnt, keybuf);
                ck_u32 lin = combined[c_common_idx].base + comm_layout[i].val;
                memset(&entry, 0, sizeof(entry));
                entry.key = lk_strdup(keybuf);
                entry.name = lk_strdup(sp ? sp : comm_layout[i].key);
                if (!entry.key || !entry.name) { free(entry.key); free(entry.name); goto oom; }
                entry.frame = dg_frame;
                entry.linear = lin;
                entry.offset = (ck_u16)(lin - (ck_u32)dg_frame * 16u);
                if (pub_cnt == pub_cap) {
                    ck_usize nc = pub_cap ? pub_cap * 2u : 32u;
                    lk_pub *np = (lk_pub *)realloc(publics, (unsigned int)(nc * sizeof(lk_pub)));
                    if (!np) { free(entry.key); free(entry.name); goto oom; }
                    publics = np;
                    pub_cap = nc;
                }
                memcpy(&publics[pub_cnt++], &entry, sizeof(lk_pub));
            }
        }
    } else {
        for (i = 0; i < comm_lay_cnt; ++i) {
            to_upper_buf(keybuf, comm_layout[i].key, sizeof(keybuf));
            if (!find_pub(publics, pub_cnt, keybuf)) {
                lk_pub entry;
                const char *sp = find_str_str(first_spell, fs_cnt, keybuf);
                ck_u32 lin = combined[c_common_idx].base + comm_layout[i].val;
                memset(&entry, 0, sizeof(entry));
                entry.key = lk_strdup(keybuf);
                entry.name = lk_strdup(sp ? sp : comm_layout[i].key);
                if (!entry.key || !entry.name) { free(entry.key); free(entry.name); goto oom; }
                entry.frame = combined[c_common_idx].frame;
                entry.linear = lin;
                entry.offset = (ck_u16)(lin - (ck_u32)entry.frame * 16u);
                if (pub_cnt == pub_cap) {
                    ck_usize nc = pub_cap ? pub_cap * 2u : 32u;
                    lk_pub *np = (lk_pub *)realloc(publics, (unsigned int)(nc * sizeof(lk_pub)));
                    if (!np) { free(entry.key); free(entry.name); goto oom; }
                    publics = np;
                    pub_cap = nc;
                }
                memcpy(&publics[pub_cnt++], &entry, sizeof(lk_pub));
            }
        }
        for (m = 0; m < mod_count; ++m) {
            for (i = 0; i < modules[m].ext_count; ++i) {
                to_upper_buf(keybuf, modules[m].externals[i].name, sizeof(keybuf));
                if (strcmp(keybuf, "_EDATA") != 0 && strcmp(keybuf, "_END") != 0) continue;
                if (!find_pub(publics, pub_cnt, keybuf)) {
                    lk_pub entry;
                    const char *sp = find_str_str(first_spell, fs_cnt, keybuf);
                    memset(&entry, 0, sizeof(entry));
                    entry.key = lk_strdup(keybuf);
                    entry.name = lk_strdup(sp ? sp : keybuf);
                    if (!entry.key || !entry.name) { free(entry.key); free(entry.name); goto oom; }
                    entry.absolute = 1;
                    if (pub_cnt == pub_cap) {
                        ck_usize nc = pub_cap ? pub_cap * 2u : 32u;
                        lk_pub *np = (lk_pub *)realloc(publics, (unsigned int)(nc * sizeof(lk_pub)));
                        if (!np) { free(entry.key); free(entry.name); goto oom; }
                        publics = np;
                        pub_cap = nc;
                    }
                    memcpy(&publics[pub_cnt++], &entry, sizeof(lk_pub));
                }
            }
        }
    }

    /* Image buffer */
    image = (ck_u8 *)calloc(image_size > 0 ? (unsigned int)image_size : 1u, 1u);
    if (!image) goto oom;
    for (m = 0; m < mod_count; ++m) {
        for (i = 0; i < modules[m].data_count; ++i) {
            const ck_link_data *dc = &modules[m].data[i];
            const lk_place *pl = &placements[m][dc->segment];
            ck_u32 at;
            if (pl->absolute) continue;
            combined[pl->combined].has_data = 1;
            at = combined[pl->combined].base + pl->offset + dc->offset;
            if (dc->len > 0) memcpy(image + at, dc->bytes, (unsigned int)dc->len);
        }
    }

    /* Fixups */
    for (m = 0; m < mod_count; ++m) {
        for (i = 0; i < modules[m].fixup_count; ++i) {
            const ck_link_fixup *fx = &modules[m].fixups[i];
            const lk_place *home = &placements[m][fx->segment];
            const lk_combined *container;
            ck_u32 at, target_linear = 0, addend;
            ck_u16 target_frame = 0;
            int target_reloc = 0, target_abs = 0;
            if (home->absolute) continue;
            container = &combined[home->combined];
            at = container->base + home->offset + fx->offset;
            addend = fx->has_displacement ? (ck_u32)fx->displacement : 0u;

            if (fx->target.kind == CK_FIX_GROUP) {
                target_frame = groups[mod_groups[m][fx->target.index]].frame;
                target_linear = (ck_u32)target_frame * 16u + addend;
                target_reloc = 1;
            } else if (fx->target.kind == CK_FIX_SEGMENT) {
                const lk_place *pl = &placements[m][fx->target.index];
                if (pl->absolute) {
                    target_frame = pl->absolute_frame;
                    target_linear = (ck_u32)pl->absolute_frame * 16u + addend;
                    target_abs = 1;
                } else {
                    const lk_combined *t = &combined[pl->combined];
                    target_frame = t->frame;
                    target_linear = t->base + pl->offset + addend;
                    target_reloc = 1;
                }
            } else if (fx->target.kind == CK_FIX_EXTERNAL) {
                const ck_link_external *ex = &modules[m].externals[fx->target.index];
                lk_pub *hit = NULL;
                to_upper_buf(keybuf, ex->name, sizeof(keybuf));
                if (ex->local) hit = find_pub(local_pubs[m], local_pub_cnts[m], keybuf);
                if (!hit) hit = find_pub(publics, pub_cnt, keybuf);
                if (!hit) {
                    char msg[160];
                    sprintf(msg, "unresolved external %.120s", ex->name);
                    ck_set_error(err, 0, msg);
                    goto fail;
                }
                target_frame = hit->frame;
                target_linear = hit->linear + addend;
                target_reloc = !hit->absolute;
                target_abs = hit->absolute;
            } else {
                ck_set_error(err, 0, "unsupported fixup target kind");
                goto fail;
            }

            if (!target_abs) {
                if (fx->frame.kind == CK_FIX_GROUP) {
                    target_frame = groups[mod_groups[m][fx->frame.index]].frame;
                } else if (fx->frame.kind == CK_FIX_SEGMENT) {
                    const lk_place *pl = &placements[m][fx->frame.index];
                    target_frame = pl->absolute ? pl->absolute_frame : combined[pl->combined].frame;
                }
            }

            if (fx->location == 1 || fx->location == 5) {
                ck_u16 cur = ck_read_u16_le(image + at);
                ck_u32 v = fx->segment_relative ? (cur + target_linear - (ck_u32)target_frame * 16u)
                                                : (cur + target_linear - (at + 2u));
                ck_write_u16_le(image + at, (ck_u16)v);
            } else if (fx->location == 9) {
                ck_u32 cur = ck_read_u32_le(image + at);
                ck_u32 v = fx->segment_relative ? (cur + target_linear - (ck_u32)target_frame * 16u)
                                                : (cur + target_linear - (at + 4u));
                ck_write_u32_le(image + at, v);
            } else if (fx->location == 2 || fx->location == 3) {
                ck_u32 rlin = at;
                if (fx->location == 3) {
                    ck_u16 cur = ck_read_u16_le(image + at);
                    ck_write_u16_le(image + at, (ck_u16)(cur + target_linear - (ck_u32)target_frame * 16u));
                    rlin = at + 2u;
                }
                ck_write_u16_le(image + rlin, (ck_u16)(ck_read_u16_le(image + rlin) + target_frame));
                if (target_reloc) {
                    if (reloc_cnt == reloc_cap) {
                        ck_usize nc = reloc_cap ? reloc_cap * 2u : 32u;
                        lk_reloc *np = (lk_reloc *)realloc(relocs, (unsigned int)(nc * sizeof(lk_reloc)));
                        if (!np) goto oom;
                        relocs = np;
                        reloc_cap = nc;
                    }
                    relocs[reloc_cnt].linear = rlin;
                    relocs[reloc_cnt].frame = container->frame;
                    reloc_cnt++;
                }
            } else if (fx->location == 0) {
                image[at] = (ck_u8)(image[at] + ((target_linear - (ck_u32)target_frame * 16u) & 0xFFu));
            } else if (fx->location == 4) {
                image[at] = (ck_u8)(image[at] + (((target_linear - (ck_u32)target_frame * 16u) >> 8) & 0xFFu));
            } else {
                ck_set_error(err, 0, "unsupported fixup location");
                goto fail;
            }
        }
    }

    /* Entry point */
    for (m = 0; m < mod_count; ++m) {
        const ck_link_end *end = &modules[m].end;
        const lk_place *pl;
        ck_u32 disp;
        if (!end->has_start || have_start) continue;
        have_start = 1;
        if (end->start_target.kind != CK_FIX_SEGMENT) {
            ck_set_error(err, 0, "start address beyond segment target");
            goto fail;
        }
        pl = &placements[m][end->start_target.index];
        disp = end->start_has_displacement ? (ck_u32)end->start_displacement : 0u;
        if (pl->absolute) {
            start_cs = pl->absolute_frame;
            start_ip = (ck_u16)disp;
        } else {
            const lk_combined *home = &combined[pl->combined];
            ck_u32 lin = home->base + pl->offset + disp;
            ck_u16 frame = home->frame;
            if (end->start_frame.kind == CK_FIX_GROUP) {
                frame = groups[mod_groups[m][end->start_frame.index]].frame;
            } else if (end->start_frame.kind == CK_FIX_SEGMENT) {
                const lk_place *fpl = &placements[m][end->start_frame.index];
                frame = fpl->absolute ? fpl->absolute_frame : combined[fpl->combined].frame;
            }
            start_cs = frame;
            start_ip = (ck_u16)(lin - (ck_u32)frame * 16u);
        }
    }

    /* MZ header & image */
    for (i = 0; i < comb_count; ++i) {
        if (combined[i].length > 0 && combined[i].has_data) {
            ck_u32 e = combined[i].base + combined[i].length;
            if (e > kept) kept = e;
        }
    }
    if (kept == 0) kept = image_size;
    reloc_bytes = (ck_u32)reloc_cnt * 4u;
    header_size = (0x1Eu + reloc_bytes + 511u) & ~511u;
    file_size = header_size + kept;
    exe = (ck_u8 *)calloc(1u, (unsigned int)file_size);
    if (!exe) goto oom;

    for (m = 0; m < mod_count; ++m) {
        for (s = 0; s < modules[m].seg_count; ++s) {
            const lk_place *pl;
            const lk_combined *home;
            if (modules[m].segments[s].combine != 5) continue;
            pl = &placements[m][s];
            if (pl->absolute) continue;
            home = &combined[pl->combined];
            stack_ss = home->frame;
            stack_sp = (ck_u16)(home->base + home->length - (ck_u32)home->frame * 16u);
        }
    }
    memory_end = image_size;
    if (stack_override != 0) {
        ck_u32 top;
        stack_sp = (ck_u16)((stack_override + 1u) & ~1u);
        top = (ck_u32)stack_ss * 16u + (ck_u32)stack_sp;
        if (top > memory_end) memory_end = top;
    }

    ck_write_u16_le(exe + 0x00, 0x5A4Du);
    ck_write_u16_le(exe + 0x02, (ck_u16)(file_size % 512u));
    ck_write_u16_le(exe + 0x04, (ck_u16)((file_size + 511u) / 512u));
    ck_write_u16_le(exe + 0x06, (ck_u16)reloc_cnt);
    ck_write_u16_le(exe + 0x08, (ck_u16)(header_size / 16u));
    ck_write_u16_le(exe + 0x0A, (ck_u16)((memory_end - kept + 15u) / 16u));
    ck_write_u16_le(exe + 0x0C, 0xFFFFu);
    ck_write_u16_le(exe + 0x0E, stack_ss);
    ck_write_u16_le(exe + 0x10, stack_sp);
    ck_write_u16_le(exe + 0x14, start_ip);
    ck_write_u16_le(exe + 0x16, start_cs);
    ck_write_u16_le(exe + 0x18, 0x001Eu);
    ck_write_u16_le(exe + 0x1C, 0x0001u);
    for (i = 0; i < reloc_cnt; ++i) {
        ck_write_u16_le(exe + 0x1Eu + i * 4u, (ck_u16)(relocs[i].linear - (ck_u32)relocs[i].frame * 16u));
        ck_write_u16_le(exe + 0x20u + i * 4u, relocs[i].frame);
    }
    if (kept > 0) memcpy(exe + header_size, image, (unsigned int)kept);
    for (i = 0; i + 1u < file_size; i += 2u) {
        sum = (ck_u16)(sum + ck_read_u16_le(exe + i));
    }
    if ((file_size % 2u) != 0) sum = (ck_u16)(sum + exe[file_size - 1u]);
    ck_write_u16_le(exe + 0x12, (ck_u16)(0xFFFFu - sum));

    /* /EXEPACK */
#if !defined(CK_DOS_LEAN)
    if (exepack) {
        ck_u32 dest_bytes = (kept + 15u) & ~15u;
        ck_u32 prefix = kept;
        ck_u32 at;
        u8_vec packed;
        int first_cmd = 1, overlap = 0;
        memset(&packed, 0, sizeof(packed));
        for (at = 0; at + 8u <= kept; ++at) {
            ck_u32 j;
            int run = 1;
            for (j = 1u; j < 8u; ++j) {
                if (image[at + j] != image[at]) { run = 0; break; }
            }
            if (run) { prefix = at; break; }
        }
        if (prefix > 0 && u8_vec_push(&packed, image, prefix) != 0) { free(packed.data); goto oom; }
        at = prefix;
        while (at < kept) {
            ck_u32 run_end = at + 1u;
            while (run_end < kept && image[run_end] == image[at] && (run_end - at) < 0x4000u) ++run_end;
            if (run_end - at >= 8u) {
                ck_u8 cmd[4];
                ck_u32 len = run_end - at;
                cmd[0] = image[at];
                cmd[1] = (ck_u8)(len & 0xFFu);
                cmd[2] = (ck_u8)((len >> 8) & 0xFFu);
                cmd[3] = (ck_u8)(0xB0u | (first_cmd ? 1u : 0u));
                first_cmd = 0;
                if (u8_vec_push(&packed, cmd, 4u) != 0) { free(packed.data); goto oom; }
                at = run_end;
                if (packed.len > at) { overlap = 1; break; }
                continue;
            }
            {
                ck_u32 copy_end = at;
                ck_u8 cmd[3];
                ck_u32 len;
                while (copy_end < kept && (copy_end - at) < 0x4000u) {
                    ck_u32 probe = copy_end + 1u;
                    while (probe < kept && image[probe] == image[copy_end]) ++probe;
                    if (probe - copy_end >= 8u) break;
                    copy_end = probe;
                }
                len = copy_end - at;
                if (len > 0 && u8_vec_push(&packed, image + at, len) != 0) { free(packed.data); goto oom; }
                cmd[0] = (ck_u8)(len & 0xFFu);
                cmd[1] = (ck_u8)((len >> 8) & 0xFFu);
                cmd[2] = (ck_u8)(0xB2u | (first_cmd ? 1u : 0u));
                first_cmd = 0;
                if (u8_vec_push(&packed, cmd, 3u) != 0) { free(packed.data); goto oom; }
                at = copy_end;
                if (packed.len > at) { overlap = 1; break; }
            }
        }
        if (!overlap) {
            u8_vec stub;
            ck_u16 packed_paras;
            ck_u32 exepack_size = 16u + (ck_u32)sizeof(kExepackStub);
            ck_u32 packed_file, packed_ss, packed_top;
            ck_u8 *pexe;
            int sec;
            if (dest_bytes > kept) {
                ck_u8 cmd[4];
                ck_u32 len = dest_bytes - kept;
                cmd[0] = kept > 0 ? image[kept - 1u] : 0u;
                cmd[1] = (ck_u8)(len & 0xFFu);
                cmd[2] = (ck_u8)((len >> 8) & 0xFFu);
                cmd[3] = (ck_u8)(0xB0u | (first_cmd ? 1u : 0u));
                first_cmd = 0;
                if (u8_vec_push(&packed, cmd, 4u) != 0) { free(packed.data); goto oom; }
            }
            while ((packed.len % 16u) != 0) {
                ck_u8 ff = 0xFFu;
                if (u8_vec_push(&packed, &ff, 1u) != 0) { free(packed.data); goto oom; }
            }
            packed_paras = (ck_u16)(packed.len / 16u);
            memset(&stub, 0, sizeof(stub));
            for (sec = 0; sec < 16; ++sec) {
                ck_u32 cnt = 0;
                for (i = 0; i < reloc_cnt; ++i) {
                    if ((int)(relocs[i].linear >> 16) == sec) ++cnt;
                }
                exepack_size += 2u + 2u * cnt;
            }
            {
                ck_u8 hdr16[16];
                ck_write_u16_le(hdr16 + 0, start_ip);
                ck_write_u16_le(hdr16 + 2, start_cs);
                ck_write_u16_le(hdr16 + 4, 0);
                ck_write_u16_le(hdr16 + 6, (ck_u16)exepack_size);
                ck_write_u16_le(hdr16 + 8, stack_sp);
                ck_write_u16_le(hdr16 + 10, stack_ss);
                ck_write_u16_le(hdr16 + 12, (ck_u16)(dest_bytes / 16u));
                hdr16[14] = 'R'; hdr16[15] = 'B';
                if (u8_vec_push(&stub, hdr16, 16u) != 0 ||
                    u8_vec_push(&stub, kExepackStub, (ck_u32)sizeof(kExepackStub)) != 0) {
                    free(packed.data); free(stub.data); goto oom;
                }
            }
            for (sec = 0; sec < 16; ++sec) {
                ck_u16 *offs = NULL;
                ck_usize ocnt = 0, ocap = 0, j;
                ck_u8 w[2];
                for (i = 0; i < reloc_cnt; ++i) {
                    if ((int)(relocs[i].linear >> 16) == sec) {
                        if (ocnt == ocap) {
                            ck_usize nc = ocap ? ocap * 2u : 16u;
                            ck_u16 *np = (ck_u16 *)realloc(offs, (unsigned int)(nc * sizeof(ck_u16)));
                            if (!np) { free(offs); free(packed.data); free(stub.data); goto oom; }
                            offs = np; ocap = nc;
                        }
                        offs[ocnt++] = (ck_u16)(relocs[i].linear & 0xFFFFu);
                    }
                }
                for (i = 1; i < ocnt; ++i) {
                    ck_u16 k = offs[i];
                    j = i;
                    while (j > 0 && offs[j - 1] > k) { offs[j] = offs[j - 1]; --j; }
                    offs[j] = k;
                }
                ck_write_u16_le(w, (ck_u16)ocnt);
                if (u8_vec_push(&stub, w, 2u) != 0) { free(offs); free(packed.data); free(stub.data); goto oom; }
                for (i = 0; i < ocnt; ++i) {
                    ck_write_u16_le(w, offs[i]);
                    if (u8_vec_push(&stub, w, 2u) != 0) { free(offs); free(packed.data); free(stub.data); goto oom; }
                }
                free(offs);
            }
            packed_file = 512u + packed.len + stub.len;
            pexe = (ck_u8 *)calloc(1u, (unsigned int)packed_file);
            if (!pexe) { free(packed.data); free(stub.data); goto oom; }
            packed_ss = dest_bytes / 16u + (exepack_size + 15u) / 16u;
            packed_top = packed_ss * 16u + 0x80u;
            if ((ck_u32)stack_ss * 16u + (ck_u32)stack_sp > packed_top) {
                packed_top = (ck_u32)stack_ss * 16u + (ck_u32)stack_sp;
            }
            ck_write_u16_le(pexe + 0x00, 0x5A4Du);
            ck_write_u16_le(pexe + 0x02, (ck_u16)(packed_file % 512u));
            ck_write_u16_le(pexe + 0x04, (ck_u16)((packed_file + 511u) / 512u));
            ck_write_u16_le(pexe + 0x06, 0);
            ck_write_u16_le(pexe + 0x08, 32u);
            ck_write_u16_le(pexe + 0x0A, (ck_u16)((packed_top - (packed_file - 512u)) / 16u));
            ck_write_u16_le(pexe + 0x0C, 0xFFFFu);
            ck_write_u16_le(pexe + 0x0E, (ck_u16)packed_ss);
            ck_write_u16_le(pexe + 0x10, 0x80u);
            ck_write_u16_le(pexe + 0x14, 0x10u);
            ck_write_u16_le(pexe + 0x16, packed_paras);
            ck_write_u16_le(pexe + 0x18, 0x001Eu);
            ck_write_u16_le(pexe + 0x1C, 0x0001u);
            if (packed.len > 0) memcpy(pexe + 512, packed.data, (unsigned int)packed.len);
            if (stub.len > 0) memcpy(pexe + 512 + packed.len, stub.data, (unsigned int)stub.len);
            free(exe);
            exe = pexe;
            file_size = packed_file;
            free(stub.data);
        }
        free(packed.data);
    }
#else
    (void)exepack;
#endif

    /* Optional .MAP */
#if !defined(CK_DOS_LEAN)
    if (out_map) {
        int has_stack = 0;
        char line[256];
        for (m = 0; m < mod_count && !has_stack; ++m) {
            for (s = 0; s < modules[m].seg_count; ++s) {
                if (modules[m].segments[s].combine == 5) { has_stack = 1; break; }
            }
        }
        if (stack_override != 0) {
            sprintf(line, "Stack Allocation = %lu bytes\r\n", (unsigned long)stack_override);
            if (ms_append(&map, line) != 0) goto oom;
        }
        if (!has_stack && ms_append(&map, "LINK : warning L4021: no stack segment\r\n") != 0) goto oom;
        if (ms_append(&map, "\r\n Start  Stop   Length Name                   Class\r\n") != 0) goto oom;
        for (i = 0; i < comb_count; ++i) {
            ck_u32 shown = combined[i].length;
            ck_u32 stop;
            char padded[32];
            ck_usize nlen;
            if (stack_override != 0 && strcmp(combined[i].class_name, "STACK") == 0) shown = stack_override;
            stop = (shown == 0) ? combined[i].base : combined[i].base + shown - 1u;
            nlen = strlen(combined[i].name);
            if (nlen < 23u) {
                memcpy(padded, combined[i].name, (unsigned int)nlen);
                memset(padded + nlen, ' ', (unsigned int)(23u - nlen));
                padded[23] = '\0';
            } else {
                sprintf(padded, "%s", combined[i].name);
            }
            sprintf(line, " %05lXH %05lXH %05lXH %s%s\r\n",
                    (unsigned long)combined[i].base, (unsigned long)stop,
                    (unsigned long)shown, padded, combined[i].class_name);
            if (ms_append(&map, line) != 0) goto oom;
        }
        if (grp_cnt > 0) {
            if (ms_append(&map, "\r\n Origin   Group\r\n") != 0) goto oom;
            for (i = 0; i < grp_cnt; ++i) {
                sprintf(line, " %04X:%X   %s\r\n", (unsigned int)groups[i].frame, 0u, groups[i].name);
                if (ms_append(&map, line) != 0) goto oom;
            }
        }
        if (map_publics && pub_cnt > 0) {
            const lk_pub **by_name = (const lk_pub **)malloc((unsigned int)(pub_cnt * sizeof(const lk_pub *)));
            const lk_pub **by_val = (const lk_pub **)malloc((unsigned int)(pub_cnt * sizeof(const lk_pub *)));
            ck_u32 seq = 0;
            ck_usize j;
            if (!by_name || !by_val) { free(by_name); free(by_val); goto oom; }
            for (i = 0; i < pub_cnt; ++i) {
                by_name[i] = &publics[i];
                publics[i].mention_rank = 0xFFFFFFFFUL;
            }
            for (i = 1; i < pub_cnt; ++i) {
                const lk_pub *k = by_name[i];
                j = i;
                while (j > 0 && strcmp(by_name[j - 1]->key, k->key) > 0) {
                    by_name[j] = by_name[j - 1];
                    --j;
                }
                by_name[j] = k;
            }
            if (ms_append(&map, "\r\n  Address         Publics by Name\r\n\r\n") != 0) {
                free(by_name); free(by_val); goto oom;
            }
            for (i = 0; i < pub_cnt; ++i) {
                sprintf(line, " %04X:%04X%s%s\r\n",
                        (unsigned int)by_name[i]->frame, (unsigned int)by_name[i]->offset,
                        by_name[i]->absolute ? "  Abs  " : "       ", by_name[i]->name);
                if (ms_append(&map, line) != 0) { free(by_name); free(by_val); goto oom; }
            }
            /* Compute first mention rank per module */
            for (m = 0; m < mod_count; ++m) {
                typedef struct m_item { ck_u32 mention; char *key; } m_item;
                ck_usize tot = modules[m].ext_count + modules[m].pub_count;
                m_item *items = tot ? (m_item *)malloc((unsigned int)(tot * sizeof(m_item))) : NULL;
                ck_usize icnt = 0;
                if (tot && !items) { free(by_name); free(by_val); goto oom; }
                for (i = 0; i < modules[m].ext_count; ++i) {
                    to_upper_buf(keybuf, modules[m].externals[i].name, sizeof(keybuf));
                    items[icnt].mention = modules[m].externals[i].mention;
                    items[icnt].key = lk_strdup(keybuf);
                    icnt++;
                }
                for (i = 0; i < modules[m].pub_count; ++i) {
                    if (modules[m].publics[i].local) continue;
                    to_upper_buf(keybuf, modules[m].publics[i].name, sizeof(keybuf));
                    items[icnt].mention = modules[m].publics[i].mention;
                    items[icnt].key = lk_strdup(keybuf);
                    icnt++;
                }
                for (i = 1; i < icnt; ++i) {
                    m_item k;
                    memcpy(&k, &items[i], sizeof(m_item));
                    j = i;
                    while (j > 0 && (items[j - 1].mention > k.mention ||
                                     (items[j - 1].mention == k.mention && strcmp(items[j - 1].key, k.key) > 0))) {
                        memcpy(&items[j], &items[j - 1], sizeof(m_item));
                        --j;
                    }
                    memcpy(&items[j], &k, sizeof(m_item));
                }
                for (i = 0; i < icnt; ++i) {
                    lk_pub *p = find_pub(publics, pub_cnt, items[i].key);
                    if (p && p->mention_rank == 0xFFFFFFFFUL) p->mention_rank = seq;
                    ++seq;
                    free(items[i].key);
                }
                free(items);
            }
            {
                ck_u32 after = seq + 1u;
                ck_u32 enc = 0;
                int already_sorted = 1;
                for (i = 0; i < pub_cnt; ++i) {
                    if (publics[i].mention_rank == 0xFFFFFFFFUL) publics[i].mention_rank = after + enc;
                    ++enc;
                    by_val[i] = &publics[i];
                }
                for (i = 1; i < pub_cnt; ++i) {
                    const lk_pub *k = by_val[i];
                    ck_u8 kb = link_bucket_of(k->key);
                    j = i;
                    while (j > 0) {
                        ck_u8 pb = link_bucket_of(by_val[j - 1]->key);
                        if (pb < kb) break;
                        if (pb == kb && by_val[j - 1]->mention_rank > k->mention_rank) break;
                        by_val[j] = by_val[j - 1];
                        --j;
                    }
                    by_val[j] = k;
                }
                for (i = 1; i < pub_cnt; ++i) {
                    if (by_val[i - 1]->linear > by_val[i]->linear) { already_sorted = 0; break; }
                }
                if (!already_sorted) link_qsort_pubs(by_val, 0, (long)pub_cnt - 1);
                if (ms_append(&map, "\r\n  Address         Publics by Value\r\n\r\n") != 0) {
                    free(by_name); free(by_val); goto oom;
                }
                for (i = 0; i < pub_cnt; ++i) {
                    sprintf(line, " %04X:%04X%s%s\r\n",
                            (unsigned int)by_val[i]->frame, (unsigned int)by_val[i]->offset,
                            by_val[i]->absolute ? "  Abs  " : "       ", by_val[i]->name);
                    if (ms_append(&map, line) != 0) { free(by_name); free(by_val); goto oom; }
                }
            }
            free(by_name);
            free(by_val);
        } else if (map_publics) {
            if (ms_append(&map, "\r\n  Address         Publics by Name\r\n\r\n") != 0 ||
                ms_append(&map, "\r\n  Address         Publics by Value\r\n\r\n") != 0) goto oom;
        }
        sprintf(line, "\r\nProgram entry point at %04X:%04X\r\n", (unsigned int)start_cs, (unsigned int)start_ip);
        if (ms_append(&map, line) != 0) goto oom;
        *out_map = map.data;
        *out_map_size = map.len;
    }
#else
    (void)map_publics;
    (void)ms_append;
    (void)link_qsort_pubs;
#endif

    *out_exe = exe;
    *out_exe_size = file_size;
    exe = NULL;
    map.data = NULL;
    goto cleanup;

oom:
    ck_set_error(err, 0, "out of memory");
fail:
    free(exe);
    free(map.data);
    if (out_map) { *out_map = NULL; *out_map_size = 0; }
    goto cleanup_err;

cleanup:
    for (m = 0; m < mod_count; ++m) {
        free(placements[m]);
        free(mod_groups[m]);
        for (i = 0; i < local_pub_cnts[m]; ++i) { free(local_pubs[m][i].key); free(local_pubs[m][i].name); }
        free(local_pubs[m]);
    }
    free(placements); free(mod_groups); free(local_pubs); free(local_pub_cnts);
    for (i = 0; i < comb_count; ++i) { free(combined[i].name); free(combined[i].class_name); }
    free(combined);
    for (i = 0; i < comm_cnt; ++i) free(comm_sizes[i].key);
    free(comm_sizes); free(comm_order); free(comm_layout);
    for (i = 0; i < grp_cnt; ++i) free(groups[i].name);
    free(groups);
    for (i = 0; i < fs_cnt; ++i) { free(first_spell[i].key); free(first_spell[i].val); }
    free(first_spell);
    for (i = 0; i < pub_cnt; ++i) { free(publics[i].key); free(publics[i].name); }
    free(publics);
    free(image); free(relocs);
    return 0;

cleanup_err:
    for (m = 0; m < mod_count; ++m) {
        free(placements[m]);
        free(mod_groups[m]);
        for (i = 0; i < local_pub_cnts[m]; ++i) { free(local_pubs[m][i].key); free(local_pubs[m][i].name); }
        free(local_pubs[m]);
    }
    free(placements); free(mod_groups); free(local_pubs); free(local_pub_cnts);
    for (i = 0; i < comb_count; ++i) { free(combined[i].name); free(combined[i].class_name); }
    free(combined);
    for (i = 0; i < comm_cnt; ++i) free(comm_sizes[i].key);
    free(comm_sizes); free(comm_order); free(comm_layout);
    for (i = 0; i < grp_cnt; ++i) free(groups[i].name);
    free(groups);
    for (i = 0; i < fs_cnt; ++i) { free(first_spell[i].key); free(first_spell[i].val); }
    free(first_spell);
    for (i = 0; i < pub_cnt; ++i) { free(publics[i].key); free(publics[i].name); }
    free(publics);
    free(image); free(relocs);
    return -1;
}
