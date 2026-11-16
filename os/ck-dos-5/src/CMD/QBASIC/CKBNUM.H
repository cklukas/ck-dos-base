/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT */
#ifndef CKBNUM_H
#define CKBNUM_H

typedef enum { CKB_NUM_OK, CKB_NUM_INVALID, CKB_NUM_OVERFLOW } CkbNumResult;

/* Round a finite value to the nearest SINGLE, with ties to even. */
double ckb_single_precision(double value);
int ckb_num_is_special(double value);
/* out must hold 4 bytes (single) or 8 bytes (double). */
CkbNumResult ckb_num_encode(char *out, double value, int is_double, int mbf);
/* Extra input bytes are ignored. */
CkbNumResult ckb_num_decode(double *out, const char *bytes, unsigned length,
                   int is_double, int mbf);

#endif
