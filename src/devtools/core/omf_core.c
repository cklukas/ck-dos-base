/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/omf_core.h"

#define CK_OMF_BUCKETS 37u
#define CK_OMF_BLOCK_BYTES 512u
#define CK_OMF_BLOCK_HDR 38u

ck_u8 ck_omf_checksum(const ck_u8 *buf, ck_u32 len) {
    ck_u32 sum = 0;
    ck_u32 i;
    for (i = 0; i < len; ++i) sum += buf[i];
    return (ck_u8)((0x100u - (sum & 0xFFu)) & 0xFFu);
}

int ck_omf_parse_records(const ck_u8 *data,
                         ck_u32 len,
                         ck_u32 offset,
                         int stop_on_modend,
                         ck_omf_record **out_recs,
                         ck_usize *out_count,
                         ck_error *err) {
    ck_u32 pos = offset;
    ck_omf_record *recs = NULL;
    ck_usize count = 0;
    ck_usize cap = 0;

    *out_recs = NULL;
    *out_count = 0;

    while (pos < len) {
        ck_u8 rtype;
        ck_u16 decl_len;
        ck_u16 body_len;
        ck_u8 cksum;
        ck_omf_record *nr;

        if (pos + 3u > len) {
            ck_set_error(err, pos, "truncated OMF record header");
            goto fail;
        }
        rtype = data[pos];
        decl_len = ck_read_u16_le(data + pos + 1u);
        if (decl_len == 0) {
            ck_set_error(err, pos, "zero-length OMF record");
            goto fail;
        }
        if (pos + 3u + (ck_u32)decl_len > len) {
            ck_set_error(err, pos, "truncated OMF record body");
            goto fail;
        }
        body_len = (ck_u16)(decl_len - 1u);
        cksum = data[pos + 3u + body_len];

        if (count == cap) {
            ck_usize ncap = cap ? cap * 2u : 16u;
            ck_omf_record *np = (ck_omf_record *)realloc(recs, (unsigned int)(ncap * sizeof(ck_omf_record)));
            if (!np) {
                ck_set_error(err, pos, "out of memory");
                goto fail;
            }
            recs = np;
            cap = ncap;
        }
        nr = &recs[count++];
        nr->type = rtype;
        nr->declared_length = decl_len;
        nr->body_len = body_len;
        nr->checksum = cksum;
        if (body_len > 0) {
            nr->body = (ck_u8 *)malloc(body_len);
            if (!nr->body) {
                ck_set_error(err, pos, "out of memory");
                goto fail;
            }
            memcpy(nr->body, data + pos + 3u, body_len);
        } else {
            nr->body = NULL;
        }

        pos += 3u + (ck_u32)decl_len;
        if (stop_on_modend && (rtype == 0x8Au || rtype == 0x8Bu)) {
            break;
        }
    }

    *out_recs = recs;
    *out_count = count;
    return 0;

fail:
    ck_omf_free_records(recs, count);
    return -1;
}

void ck_omf_free_records(ck_omf_record *recs, ck_usize count) {
    ck_usize i;
    if (!recs) return;
    for (i = 0; i < count; ++i) {
        free(recs[i].body);
    }
    free(recs);
}

int ck_omf_serialize_records(const ck_omf_record *recs,
                             ck_usize count,
                             ck_u8 **out_buf,
                             ck_u32 *out_size) {
    ck_u32 total = 0;
    ck_usize i;
    ck_u8 *buf;
    ck_u32 pos = 0;

    for (i = 0; i < count; ++i) {
        total += 3u + (ck_u32)recs[i].body_len + 1u;
    }
    buf = (ck_u8 *)malloc(total > 0 ? (unsigned int)total : 1u);
    if (!buf) return -1;
    for (i = 0; i < count; ++i) {
        ck_u16 decl = (ck_u16)(recs[i].body_len + 1u);
        buf[pos] = recs[i].type;
        ck_write_u16_le(buf + pos + 1u, decl);
        if (recs[i].body_len > 0) {
            memcpy(buf + pos + 3u, recs[i].body, recs[i].body_len);
        }
        buf[pos + 3u + recs[i].body_len] = ck_omf_checksum(buf + pos, 3u + (ck_u32)recs[i].body_len);
        pos += 4u + (ck_u32)recs[i].body_len;
    }
    *out_buf = buf;
    *out_size = total;
    return 0;
}

int ck_omf_read_index(const ck_u8 *buf, ck_u16 len, ck_u16 *pos, ck_u16 *out_val) {
    ck_u8 hi;
    if (*pos >= len) return -1;
    hi = buf[(*pos)++];
    if ((hi & 0x80u) == 0) {
        *out_val = (ck_u16)hi;
        return 0;
    }
    if (*pos >= len) return -1;
    *out_val = (ck_u16)(((ck_u16)(hi & 0x7Fu) << 8) | (ck_u16)buf[(*pos)++]);
    return 0;
}

int ck_omf_read_name(const ck_u8 *buf, ck_u16 len, ck_u16 *pos, char *out_name, ck_usize cap) {
    ck_u8 nlen;
    ck_usize copy_len;
    if (*pos >= len) return -1;
    nlen = buf[(*pos)++];
    if ((ck_u32)(*pos) + (ck_u32)nlen > (ck_u32)len) return -1;
    copy_len = (ck_usize)nlen < (cap - 1u) ? (ck_usize)nlen : (cap - 1u);
    if (copy_len > 0) memcpy(out_name, buf + *pos, (unsigned int)copy_len);
    out_name[copy_len] = '\0';
    *pos = (ck_u16)(*pos + nlen);
    return 0;
}

static ck_u16 ror16(ck_u16 v, int amt) {
    return (ck_u16)((v >> amt) | (v << (16 - amt)));
}

static ck_u16 rol16(ck_u16 v, int amt) {
    return (ck_u16)((v << amt) | (v >> (16 - amt)));
}

static ck_u8 fold_ch(char c) {
    return (ck_u8)((ck_u8)c | 0x20u);
}

int ck_omf_fold_cmp(const char *a, const char *b) {
    ck_usize la = strlen(a);
    ck_usize lb = strlen(b);
    ck_usize shared = la < lb ? la : lb;
    ck_usize i;
    for (i = 0; i < shared; ++i) {
        ck_u8 fa = fold_ch(a[i]);
        ck_u8 fb = fold_ch(b[i]);
        if (fa != fb) return fa < fb ? -1 : 1;
    }
    if (la != lb) return la < lb ? -1 : 1;
    return 0;
}

void ck_omf_dict_hash(const char *name, ck_dict_hash *out_hash) {
    ck_usize len = strlen(name);
    ck_usize i;
    ck_u16 seed;

    out_hash->bucket_index = 0;
    out_hash->block_step = 0;
    for (i = len; i > 0; --i) {
        ck_u8 f = fold_ch(name[i - 1]);
        out_hash->bucket_index = (ck_u16)(ror16(out_hash->bucket_index, 2) ^ f);
        out_hash->block_step = (ck_u16)(rol16(out_hash->block_step, 2) ^ f);
    }
    seed = (ck_u16)(len | 0x20u);
    out_hash->bucket_step = seed;
    out_hash->block_index = seed;
    if (len > 0) {
        for (i = 0; i + 1u < len; ++i) {
            ck_u8 f = fold_ch(name[i]);
            out_hash->bucket_step = (ck_u16)(ror16(out_hash->bucket_step, 2) ^ f);
            out_hash->block_index = (ck_u16)(rol16(out_hash->block_index, 2) ^ f);
        }
    }
}

static ck_usize entry_bytes_n(ck_usize name_len) {
    ck_usize sz = 1u + name_len + 2u;
    return sz + (sz % 2u);
}

static int try_layout_dict(const ck_dict_sym *syms,
                           const ck_usize *order,
                           ck_usize sym_count,
                           ck_u16 blocks,
                           ck_u16 *out_blk,
                           ck_u8 *out_bkt,
                           ck_u8 *out_declined) {
    int *occ = NULL;
    ck_u16 *used = NULL;
    ck_usize oi;
    ck_u16 b;

    if (blocks == 0) return -1;
    occ = (int *)malloc((unsigned int)((ck_u32)blocks * CK_OMF_BUCKETS * sizeof(int)));
    used = (ck_u16 *)malloc((unsigned int)(blocks * sizeof(ck_u16)));
    if (!occ || !used) {
        free(occ);
        free(used);
        return -2;
    }
    for (b = 0; b < blocks; ++b) {
        ck_usize k;
        for (k = 0; k < CK_OMF_BUCKETS; ++k) occ[(ck_u32)b * CK_OMF_BUCKETS + k] = -1;
        used[b] = (ck_u16)CK_OMF_BLOCK_HDR;
        if (out_declined) out_declined[b] = 0;
    }

    for (oi = 0; oi < sym_count; ++oi) {
        ck_usize idx = order[oi];
        const char *name = syms[idx].name;
        ck_usize nlen = strlen(name);
        ck_usize need = entry_bytes_n(nlen);
        ck_dict_hash h;
        ck_u16 bucket;
        ck_u16 bucket_step;
        ck_u16 block;
        ck_u16 block_step;
        ck_u16 attempt;
        int placed = 0;

        ck_omf_dict_hash(name, &h);
        bucket = (ck_u16)(h.bucket_index % CK_OMF_BUCKETS);
        bucket_step = (ck_u16)((h.bucket_step % CK_OMF_BUCKETS) != 0 ? (h.bucket_step % CK_OMF_BUCKETS) : 1u);
        block = (ck_u16)(h.block_index % blocks);
        block_step = (ck_u16)((h.block_step % blocks) != 0 ? (h.block_step % blocks) : 1u);

        for (attempt = 0; attempt < blocks && !placed; ++attempt) {
            ck_usize probe;
            for (probe = 0; probe < CK_OMF_BUCKETS; ++probe) {
                if (occ[(ck_u32)block * CK_OMF_BUCKETS + bucket] >= 0) {
                    bucket = (ck_u16)((bucket + bucket_step) % CK_OMF_BUCKETS);
                    continue;
                }
                if ((ck_usize)used[block] + need > CK_OMF_BLOCK_BYTES) {
                    if (out_declined) out_declined[block] = 1;
                    break;
                }
                occ[(ck_u32)block * CK_OMF_BUCKETS + bucket] = (int)idx;
                used[block] = (ck_u16)(used[block] + need);
                if (out_blk) out_blk[idx] = block;
                if (out_bkt) out_bkt[idx] = (ck_u8)bucket;
                placed = 1;
                break;
            }
            block = (ck_u16)((block + block_step) % blocks);
        }
        if (!placed) {
            free(occ);
            free(used);
            return -1;
        }
    }

    free(occ);
    free(used);
    return 0;
}

static int is_prime_u16(ck_u16 n) {
    ck_u16 f;
    if (n <= 2u) return 1;
    if ((n % 2u) == 0) return 0;
    for (f = 3u; (ck_u32)f * (ck_u32)f <= (ck_u32)n; f += 2u) {
        if ((n % f) == 0) return 0;
    }
    return 1;
}

int ck_omf_build_dictionary(const ck_dict_sym *syms,
                            ck_usize sym_count,
                            ck_u16 *out_blocks,
                            ck_u8 **out_dict,
                            ck_u32 *out_dict_size,
                            ck_error *err) {
    ck_usize *order = NULL;
    ck_u16 *blk_spot = NULL;
    ck_u8 *bkt_spot = NULL;
    ck_u8 *declined = NULL;
    ck_u16 *free_off = NULL;
    ck_u8 *bytes = NULL;
    ck_usize i, j;
    ck_u16 blocks = 0;
    ck_u16 try_b;
    ck_u32 dict_sz;

    order = (ck_usize *)malloc(sym_count > 0 ? (unsigned int)(sym_count * sizeof(ck_usize)) : 1u);
    if (!order) goto oom;
    for (i = 0; i < sym_count; ++i) order[i] = i;

    /* Stable insertion sort by folded name */
    for (i = 1; i < sym_count; ++i) {
        ck_usize key = order[i];
        j = i;
        while (j > 0 && ck_omf_fold_cmp(syms[order[j - 1]].name, syms[key].name) > 0) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = key;
    }

    for (try_b = 1; try_b <= 8192u; ++try_b) {
        int rc;
        if (!is_prime_u16(try_b)) continue;
        rc = try_layout_dict(syms, order, sym_count, try_b, NULL, NULL, NULL);
        if (rc == 0) {
            blocks = try_b;
            break;
        }
        if (rc == -2) goto oom;
    }
    if (blocks == 0) {
        free(order);
        ck_set_error(err, 0, "the dictionary does not fit the symbols");
        return -1;
    }

    blk_spot = (ck_u16 *)malloc(sym_count > 0 ? (unsigned int)(sym_count * sizeof(ck_u16)) : 1u);
    bkt_spot = (ck_u8 *)malloc(sym_count > 0 ? (unsigned int)sym_count : 1u);
    declined = (ck_u8 *)malloc(blocks);
    free_off = (ck_u16 *)malloc((unsigned int)(blocks * sizeof(ck_u16)));
    dict_sz = (ck_u32)blocks * CK_OMF_BLOCK_BYTES;
    bytes = (ck_u8 *)calloc(1u, (unsigned int)dict_sz);
    if (!blk_spot || !bkt_spot || !declined || !free_off || !bytes) goto oom;

    if (try_layout_dict(syms, order, sym_count, blocks, blk_spot, bkt_spot, declined) != 0) {
        ck_set_error(err, 0, "dictionary layout failed");
        goto fail;
    }

    for (try_b = 0; try_b < blocks; ++try_b) {
        free_off[try_b] = (ck_u16)CK_OMF_BLOCK_HDR;
    }
    for (i = 0; i < sym_count; ++i) {
        ck_usize idx = order[i];
        const char *name = syms[idx].name;
        ck_usize nlen = strlen(name);
        ck_u16 blk = blk_spot[idx];
        ck_u8 bkt = bkt_spot[idx];
        ck_u32 base = (ck_u32)blk * CK_OMF_BLOCK_BYTES;
        ck_u16 at = free_off[blk];
        bytes[base + bkt] = (ck_u8)(at / 2u);
        bytes[base + at] = (ck_u8)nlen;
        if (nlen > 0) memcpy(bytes + base + at + 1u, name, (unsigned int)nlen);
        ck_write_u16_le(bytes + base + at + 1u + nlen, syms[idx].page);
        free_off[blk] = (ck_u16)(at + entry_bytes_n(nlen));
    }
    for (try_b = 0; try_b < blocks; ++try_b) {
        bytes[(ck_u32)try_b * CK_OMF_BLOCK_BYTES + CK_OMF_BUCKETS] =
            declined[try_b] ? 0xFFu : (ck_u8)(free_off[try_b] / 2u);
    }

    free(order);
    free(blk_spot);
    free(bkt_spot);
    free(declined);
    free(free_off);
    *out_blocks = blocks;
    *out_dict = bytes;
    *out_dict_size = dict_sz;
    return 0;

oom:
    ck_set_error(err, 0, "out of memory");
fail:
    free(order);
    free(blk_spot);
    free(bkt_spot);
    free(declined);
    free(free_off);
    free(bytes);
    return -1;
}
