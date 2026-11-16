/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_MSGTOOLS_CORE_H
#define DOSBASE_DEVTOOLS_CORE_MSGTOOLS_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ck_msg_lines {
    char **lines;
    ck_usize count;
} ck_msg_lines;

/* Callback to look up message (component, number).
   Returns 0 if found (populating out_lines; caller frees via ck_msg_lines_free),
   1 if not found, -1 on error. */
typedef int (*ck_msg_lookup_fn)(void *ctx,
                                const char *component,
                                int number,
                                ck_msg_lines *out_lines);

/* Callback to emit an output file (name/extension suffix such as "CL1", "CTL", "1").
   Returns 0 on success, -1 on error. */
typedef int (*ck_msg_emit_fn)(void *ctx,
                              const char *tag,
                              const char *data,
                              ck_u32 size);

void ck_msg_lines_free(ck_msg_lines *ml);

int ck_nosrvbld_core(const char *skl_raw,
                     ck_u32 skl_size,
                     const char *base_name,
                     ck_msg_lookup_fn lookup,
                     void *lookup_ctx,
                     ck_msg_emit_fn emit,
                     void *emit_ctx,
                     ck_error *err);

int ck_buildmsg_core(const char *skl_raw,
                     ck_u32 skl_size,
                     ck_msg_lookup_fn lookup,
                     void *lookup_ctx,
                     ck_msg_emit_fn emit,
                     void *emit_ctx,
                     char *out_util,
                     ck_usize out_util_cap,
                     ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_MSGTOOLS_CORE_H */
