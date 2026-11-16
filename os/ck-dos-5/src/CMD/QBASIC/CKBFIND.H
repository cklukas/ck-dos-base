/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBFIND_H
#define CKBFIND_H

#if defined(__WATCOMC__) || defined(_MSDOS)
#define CKB_FIND_FAR __far
#else
#define CKB_FIND_FAR
#endif

#define CKB_FIND_MATCH_CASE 1u
#define CKB_FIND_WHOLE_WORD 2u

/* Search candidate start offsets in [start, end), without wrapping.  Both
 * bounds are clamped to length; an empty or reversed range cannot match.
 * A match may extend beyond end, and whole-word checks use the full source.
 * This makes end suitable for tracking an origin across source mutations.
 * Pattern, source and found have the same contract as ckb_find. */
int ckb_find_range(const char CKB_FIND_FAR *source, unsigned length,
                   unsigned start, unsigned end, const char *pattern,
                   unsigned flags, unsigned *found);

/* Search candidate positions from start through the end, then from zero to
 * start.  start is inclusive and may equal length.  The pattern is a nonempty
 * NUL-terminated near string; source is length bytes and need not be
 * terminated.  On success, write the byte offset to found and return 1.
 * Empty patterns and patterns longer than source never match. */
int ckb_find(const char CKB_FIND_FAR *source, unsigned length, unsigned start,
             const char *pattern, unsigned flags, unsigned *found);

#endif
