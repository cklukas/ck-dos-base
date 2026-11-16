/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_NMAKE_CORE_H
#define DOSBASE_DEVTOOLS_CORE_NMAKE_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if file exists (setting *out_mtime to packed DOS date/time or epoch),
   0 if not found. */
typedef int (*ck_nmake_stat_fn)(void *ctx, const char *path, ck_u32 *out_mtime);

/* Executes a command line. Returns 0 on success, non-zero on failure. */
typedef int (*ck_nmake_exec_fn)(void *ctx, const char *cmd, int silent);

typedef struct ck_nmake_opts {
    const char *makefile_text;
    ck_u32 makefile_size;
    const char *const *targets;
    ck_usize target_count;
    const char *const *macro_defs; /* "NAME=VALUE" strings */
    ck_usize macro_def_count;
    int force_all; /* -a */
    int dry_run;   /* -n */
    ck_nmake_stat_fn stat_fn;
    ck_nmake_exec_fn exec_fn;
    void *cb_ctx;
} ck_nmake_opts;

int ck_nmake_run(const ck_nmake_opts *opts, ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_NMAKE_CORE_H */
