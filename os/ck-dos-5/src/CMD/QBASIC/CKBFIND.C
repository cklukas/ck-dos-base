/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include "CKBFIND.H"

static unsigned char fold(unsigned char c)
{
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a' + 'A');
    return c;
}

static int word_char(unsigned char c)
{
    c = fold(c);
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '/';
}

static int matches(const char CKB_FIND_FAR *source, unsigned length,
                   unsigned pos, const char *pattern, unsigned size,
                   unsigned flags)
{
    unsigned i;
    if ((flags & CKB_FIND_WHOLE_WORD) &&
        ((pos && word_char((unsigned char)source[pos - 1])) ||
         (pos + size < length && word_char((unsigned char)source[pos + size]))))
        return 0;
    for (i = 0; i < size; ++i) {
        unsigned char a = (unsigned char)source[pos + i];
        unsigned char b = (unsigned char)pattern[i];
        if (!(flags & CKB_FIND_MATCH_CASE)) {
            a = fold(a);
            b = fold(b);
        }
        if (a != b) return 0;
    }
    return 1;
}

static unsigned pattern_size(const char *pattern, unsigned length)
{
    unsigned size = 0;
    /* Stop at length before incrementing, including on 16-bit unsigned. */
    while (pattern[size]) {
        if (size == length) return 0;
        ++size;
    }
    return size;
}

static int find_range(const char CKB_FIND_FAR *source, unsigned length,
                      unsigned start, unsigned end, const char *pattern,
                      unsigned size, unsigned flags, unsigned *found)
{
    unsigned pos, last;
    if (!size) return 0;
    if (start > length) start = length;
    if (end > length) end = length;
    last = length - size;
    for (pos = start; pos < end && pos <= last; ++pos) {
        if (matches(source, length, pos, pattern, size, flags)) {
            *found = pos;
            return 1;
        }
    }
    return 0;
}

int ckb_find_range(const char CKB_FIND_FAR *source, unsigned length,
                   unsigned start, unsigned end, const char *pattern,
                   unsigned flags, unsigned *found)
{
    unsigned size = pattern_size(pattern, length);
    return find_range(source, length, start, end, pattern, size, flags, found);
}

int ckb_find(const char CKB_FIND_FAR *source, unsigned length, unsigned start,
             const char *pattern, unsigned flags, unsigned *found)
{
    unsigned size = pattern_size(pattern, length);
    if (start > length) start = length;
    return find_range(source, length, start, length, pattern, size, flags, found) ||
           find_range(source, length, 0, start, pattern, size, flags, found);
}
