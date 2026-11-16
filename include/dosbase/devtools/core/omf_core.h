/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_OMF_CORE_H
#define DOSBASE_DEVTOOLS_CORE_OMF_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_omf_record {
    ck_u8 type;
    ck_u16 declared_length;
    ck_u8 *body;
    ck_u16 body_len;
    ck_u8 checksum;
} ck_omf_record;

typedef struct ck_dict_sym {
    char *name;
    ck_u16 page;
} ck_dict_sym;

typedef struct ck_dict_hash {
    ck_u16 bucket_index;
    ck_u16 bucket_step;
    ck_u16 block_index;
    ck_u16 block_step;
} ck_dict_hash;

ck_u8 ck_omf_checksum(const ck_u8 *buf, ck_u32 len);

int ck_omf_parse_records(const ck_u8 *data,
                         ck_u32 len,
                         ck_u32 offset,
                         int stop_on_modend,
                         ck_omf_record **out_recs,
                         ck_usize *out_count,
                         ck_error *err);

void ck_omf_free_records(ck_omf_record *recs, ck_usize count);

int ck_omf_serialize_records(const ck_omf_record *recs,
                             ck_usize count,
                             ck_u8 **out_buf,
                             ck_u32 *out_size);

int ck_omf_read_index(const ck_u8 *buf, ck_u16 len, ck_u16 *pos, ck_u16 *out_val);
int ck_omf_read_name(const ck_u8 *buf, ck_u16 len, ck_u16 *pos, char *out_name, ck_usize cap);

void ck_omf_dict_hash(const char *name, ck_dict_hash *out_hash);
int ck_omf_fold_cmp(const char *a, const char *b);

int ck_omf_build_dictionary(const ck_dict_sym *syms,
                            ck_usize sym_count,
                            ck_u16 *out_blocks,
                            ck_u8 **out_dict,
                            ck_u32 *out_dict_size,
                            ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_OMF_CORE_H */
