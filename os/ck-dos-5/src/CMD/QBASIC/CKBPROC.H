/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBPROC_H
#define CKBPROC_H

#if defined(__WATCOMC__) || defined(_MSDOS)
#define CKB_PROC_FAR __far
#else
#define CKB_PROC_FAR
#endif

#define CKB_PROC_SUB 1
#define CKB_PROC_FUNCTION 2

typedef struct {
    unsigned start, end;
    unsigned name, name_length;
    unsigned char kind;
} CkbProcedure;

/* Index the first capacity SUB and FUNCTION definitions in source order,
 * then sort them by name (ASCII case-insensitive). All offsets refer to the
 * original source bytes. The procedure range [start, end) includes the
 * closing line and its line break, when present. Rebuild after source edits.
 * Source need not be NUL-terminated and must fit in one segment. */
unsigned ckb_proc_index(const char CKB_PROC_FAR *source, unsigned length,
                        CkbProcedure *entries, unsigned capacity);

/* Return the sorted index for offset, or -1 for module code. */
int ckb_proc_at(const CkbProcedure *entries, unsigned count, unsigned offset);

#endif
