/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_EXE2BIN_CORE_H
#define DOSBASE_DEVTOOLS_CORE_EXE2BIN_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ck_exe2bin_kind {
    CK_EXE2BIN_PURE_BINARY = 0,
    CK_EXE2BIN_COM_IMAGE = 1
} ck_exe2bin_kind;

typedef struct ck_exe2bin_out {
    ck_exe2bin_kind kind;
    ck_u8 *bytes;
    ck_u32 size;
} ck_exe2bin_out;

/* Converts MZ exe_bytes into a flat binary or .COM image.
   Caller must free(out->bytes) on success (return 0). */
int ck_exe2bin(const ck_u8 *exe_bytes,
               ck_u32 exe_size,
               int has_base_seg,
               ck_u16 base_seg,
               ck_exe2bin_out *out,
               ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_EXE2BIN_CORE_H */
