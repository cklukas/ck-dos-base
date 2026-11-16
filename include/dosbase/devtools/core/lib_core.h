/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_LIB_CORE_H
#define DOSBASE_DEVTOOLS_CORE_LIB_CORE_H

#include "dosbase/devtools/core/omf_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_lib_member_in {
    const char *module_name;
    const ck_u8 *object;
    ck_u32 object_size;
} ck_lib_member_in;

typedef struct ck_lib_build_out {
    ck_u8 *bytes;
    ck_u32 size;
    ck_u16 *pages;
    ck_u16 *sizes;
    ck_dict_sym *symbols;
    ck_usize symbol_count;
    char ***publics;
    ck_usize *public_counts;
    ck_usize member_count;
} ck_lib_build_out;

typedef struct ck_lib_request {
    char *library;
    char *listing;
    char **additions;
    ck_usize addition_count;
} ck_lib_request;

void ck_lib_build_out_free(ck_lib_build_out *out);
void ck_lib_request_free(ck_lib_request *req);

int ck_lib_parse_response(const char *text,
                          ck_u32 size,
                          ck_lib_request *out,
                          ck_error *err);

int ck_lib_build(const ck_lib_member_in *members,
                 ck_usize member_count,
                 ck_usize page_size,
                 ck_lib_build_out *out,
                 ck_error *err);

int ck_lib_build_listing(const ck_lib_member_in *members,
                         const ck_lib_build_out *build,
                         ck_usize page_size,
                         char **out_text,
                         ck_u32 *out_len);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_LIB_CORE_H */
