/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_CONVERT_CORE_H
#define DOSBASE_DEVTOOLS_CORE_CONVERT_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CK_CONVERT_MAX_COM_BYTES 0xFFFEu

/* Wraps an MZ executable in a self-relocating .COM loader.
   Caller must free(*out_bytes) on success (return 0). */
int ck_convert_exe_to_com(const ck_u8 *exe_bytes,
                          ck_u32 exe_size,
                          ck_u8 **out_bytes,
                          ck_u32 *out_size,
                          ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_CONVERT_CORE_H */
