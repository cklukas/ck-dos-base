/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_EXEFIX_CORE_H
#define DOSBASE_DEVTOOLS_CORE_EXEFIX_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_exefix_opts {
    int has_min_alloc;
    ck_u16 min_alloc;
    int has_max_alloc;
    ck_u16 max_alloc;
} ck_exefix_opts;

typedef struct ck_exefix_info {
    int min_clamped_to_stack;
    int max_clamped_to_min;
} ck_exefix_info;

/* Validates and patches the first 28 bytes of an MZ executable in-place.
   `bytes` must contain at least `total_size` bytes on host, or at least
   `header_paragraphs * 16` bytes when `total_size` is the full file size.
   Returns 0 on success, -1 on error. */
int ck_exefix_patch(ck_u8 *bytes,
                    ck_u32 available_bytes,
                    ck_u32 total_size,
                    const ck_exefix_opts *opts,
                    ck_exefix_info *out_info,
                    ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_EXEFIX_CORE_H */
