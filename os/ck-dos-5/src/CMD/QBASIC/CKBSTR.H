/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBSTR_H
#define CKBSTR_H

#define CKB_INLINE_STRING 255
#define CKB_STRING_MAX 32767

typedef struct CkbStringTemp CkbStringTemp;

typedef struct {
    double number;
    const char *record;
    char text[CKB_INLINE_STRING + 1];
    char *wide;
    unsigned length;
    int string;
    char kind; /* Numeric BASIC type: %, &, !, or #. */
} CkbValue;

CkbStringTemp *ckb_string_mark(void);
void ckb_string_release(CkbStringTemp *mark);
char *ckb_string_buffer(CkbValue *value, unsigned length);
char *ckb_string_buffer_at(CkbValue *value);
const char *ckb_string_bytes(const CkbValue *value);
int ckb_string_compare(const CkbValue *a, const CkbValue *b);
void ckb_number_text(char *text, const CkbValue *value);
double ckb_numeric_prefix(const char *text);

/* Look up an uppercase BASIC intrinsic name. Zero means unknown. */
int ckb_string_function(const char *upper_name);

/* Check argument types on both passes. Evaluate arguments only while running.
 * Return a BASIC diagnostic on error, or NULL on success. */
const char *ckb_string_apply(int id, CkbValue *out, const CkbValue *args,
                             unsigned count, int run);

#endif
