/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_COMPRESS_CORE_H
#define DOSBASE_DEVTOOLS_CORE_COMPRESS_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compresses `input` using MS-DOS 4 RLE encoding.
   Caller must free(*out_bytes) on success (return 0). */
int ck_compress_rle(const ck_u8 *input,
                    ck_u32 input_size,
                    ck_u8 **out_bytes,
                    ck_u32 *out_size,
                    ck_error *err);

/* Decompresses `input` using MS-DOS 4 RLE encoding.
   Caller must free(*out_bytes) on success (return 0). */
int ck_decompress_rle(const ck_u8 *input,
                      ck_u32 input_size,
                      ck_u8 **out_bytes,
                      ck_u32 *out_size,
                      ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_COMPRESS_CORE_H */
