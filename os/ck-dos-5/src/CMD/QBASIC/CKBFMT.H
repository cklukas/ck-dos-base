/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBFMT_H
#define CKBFMT_H

#include "CKBSTR.H"

typedef enum {
    CKB_FORMAT_OK, CKB_FORMAT_INVALID, CKB_FORMAT_TYPE
} CkbFormatResult;

typedef void (*CkbFormatEmit)(void *context, const char *bytes, unsigned length);

typedef struct {
    const char *text;
    unsigned length, pos;
} CkbFormat;

/* The caller keeps the format bytes alive until finish. */
void ckb_format_init(CkbFormat *format, const char *text, unsigned length);
CkbFormatResult ckb_format_value(CkbFormat *format, const CkbValue *value,
                                 CkbFormatEmit emit, void *context);
void ckb_format_finish(CkbFormat *format, CkbFormatEmit emit, void *context);

#endif
