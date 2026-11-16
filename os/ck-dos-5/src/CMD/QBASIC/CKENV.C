/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT */
#include "CKENV.H"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if defined(__WATCOMC__) || defined(_MSDOS)
#include <dos.h>
#else
#define __far
#endif

static unsigned char __far *block;
static unsigned capacity;
static unsigned entry_capacity;
static int writable;
#if !defined(__WATCOMC__) && !defined(_MSDOS)
/* Host runs must never read or mutate the host process environment. */
static unsigned char host_block[256] = {0, 0, 1, 0, 0};
#endif

/* Find the second terminating zero and then validate the DOS 3+ trailer:
 * a word count followed by that many ASCIIZ strings.  Do not scan past MCB. */
static int bounds(unsigned *end, unsigned *used)
{
    unsigned p = 0, count, i;
    if (!block || capacity < 4) return 0;
    while (p < capacity && block[p]) {
        while (p < capacity && block[p]) ++p;
        if (p == capacity) return 0;
        ++p;
    }
    if (p == capacity) return 0;
    /* Empty blocks contain two NUL bytes; after nonempty entries the preceding
       entry terminator is the first NUL. */
    if (p == 0) {
        if (capacity < 2 || block[1]) return 0;
        p = 1;
    }
    *end = p;
    if (p >= capacity - 2) return 0;
    ++p;
    count = block[p] | ((unsigned)block[p + 1] << 8);
    p += 2;
    for (i = 0; i < count; ++i) {
        while (p < capacity && block[p]) ++p;
        if (p == capacity) return 0;
        ++p;
    }
    *used = p;
    return 1;
}

void ckenv_init(void)
{
    unsigned end, used;
    block = NULL;
    capacity = entry_capacity = 0;
    writable = 0;
#if defined(__WATCOMC__) || defined(_MSDOS)
    {
        unsigned seg = *(const unsigned __far *)MK_FP(_psp, 0x2c);
        unsigned paragraphs, owner;
        unsigned char kind;
        if (!seg) return;
        kind = *(const unsigned char __far *)MK_FP(seg - 1U, 0);
        owner = *(const unsigned __far *)MK_FP(seg - 1U, 1);
        paragraphs = *(const unsigned __far *)MK_FP(seg - 1U, 3);
        if ((kind != 'M' && kind != 'Z') || !paragraphs) return;
        capacity = paragraphs > 0xfffU ? 0xffffU : paragraphs * 16U;
        block = MK_FP(seg, 0);
        writable = owner == _psp;
    }
#else
    block = host_block; capacity = sizeof(host_block); writable = 1;
#endif
    /* QBasic reuses freed entry bytes within the initial string extent.
     * DOS paragraph padding is available to the block, not to ENVIRON. */
    if (bounds(&end, &used) && block[0]) entry_capacity = end;
}

static unsigned fold(unsigned c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

static int find_name(const char *name, unsigned length, unsigned *at, unsigned *after)
{
    unsigned p = 0;
    while (p < capacity && block[p]) {
        unsigned start = p, i = 0;
        while (p < capacity && block[p] && block[p] != '=') ++p;
        if (p == capacity) return 0;
        if (block[p] == '=' && p - start == length) {
            while (i < length && fold(block[start + i]) == fold((unsigned char)name[i])) ++i;
            if (i == length) {
                *at = start;
                while (p < capacity && block[p]) ++p;
                if (p == capacity) return 0;
                *after = p + 1;
                return 1;
            }
        }
        while (p < capacity && block[p]) ++p;
        if (p == capacity) return 0;
        ++p;
    }
    return 0;
}

static int copy_far(unsigned begin, unsigned length, char *out, unsigned outcap)
{
    unsigned i;
    if (length > INT_MAX) return -7;
    if (!out) return (int)length;
    if (length >= outcap) return -7;
    for (i = 0; i < length; ++i) out[i] = (char)block[begin + i];
    out[length] = 0;
    return (int)length;
}

int ckenv_get(const char *name, char *out, unsigned outcap)
{
    unsigned end, used, at, after, n;
    if (!name || !bounds(&end, &used)) return -5;
    n = strlen(name);
    if (!n || strchr(name, '=')) return -5;
    if (!find_name(name, n, &at, &after)) return copy_far(0, 0, out, outcap);
    return copy_far(at + n + 1, after - at - n - 2, out, outcap);
}

int ckenv_at(long index, char *out, unsigned outcap)
{
    unsigned end, used, p = 0, start;
    if (index <= 0 || !bounds(&end, &used)) return -5;
    while (p < end && block[p]) {
        start = p;
        while (p < end && block[p]) ++p;
        if (--index == 0) return copy_far(start, p - start, out, outcap);
        ++p;
    }
    return copy_far(0, 0, out, outcap);
}

int ckenv_program_path(char *out, unsigned outcap)
{
    unsigned end, used, start, p;
    if (!bounds(&end, &used)) return -5;
    start = end + 3;
    if (start == used) return copy_far(0, 0, out, outcap);
    p = start;
    while (block[p]) ++p;
    return copy_far(start, p - start, out, outcap);
}

int ckenv_set(const char *assignment)
{
    unsigned end, used, at, after, old = 0, add, empty, i, n;
    const char *separator;
    if (!assignment || !(separator = strpbrk(assignment, "= ")) || separator == assignment ||
        !bounds(&end, &used)) return 5;
    n = (unsigned)(separator - assignment);
    if (find_name(assignment, n, &at, &after)) old = after - at;
    add = !separator[1] || (separator[1] == ';' && !separator[2]) ?
          0 : (unsigned)strlen(assignment) + 1U;
    if (!old && !add) return 0;
    if (!writable) return 7;
    /* QBasic removes the old entry before checking replacement capacity.
     * Keep the DOS trailer and a valid double-NUL even if the add fails. */
    if (old) {
        for (i = after; i < used; ++i) block[i - old] = block[i];
        end -= old;
        used -= old;
        if (!end) {
            i = used;
            while (i) { --i; block[i + 1U] = block[i]; }
            block[0] = 0;
            ++end;
            ++used;
        }
    }
    if (!add) return 0;
    empty = !block[0];
    if (end - empty + (unsigned long)add > entry_capacity ||
        add > capacity - (used - empty)) return 7;
    i = used;
    while (i > end) { --i; block[i + add - empty] = block[i]; }
    end -= empty;
    for (i = 0; i < add; ++i) block[end + i] = (unsigned char)assignment[i];
    block[end + n] = '=';
    return 0;
}

void ckenv_free_child_env(char **env)
{
    unsigned i;
    if (!env) return;
    for (i = 0; env[i]; ++i) free(env[i]);
    free(env);
}

char **ckenv_child_env(void)
{
    unsigned end, used, p = 0, count = 0, i = 0;
    char **env;
    if (!bounds(&end, &used)) return NULL;
    while (p < end && block[p]) { while (p < end && block[p]) ++p; ++p; ++count; }
    if (count >= UINT_MAX / sizeof(char *)) return NULL;
    env = calloc(count + 1U, sizeof(char *));
    if (!env) return NULL;
    p = 0;
    while (i < count) {
        unsigned start = p, j, size;
        while (block[p]) ++p;
        size = p - start + 1U;
        env[i] = malloc(size);
        if (!env[i]) { ckenv_free_child_env(env); return NULL; }
        for (j = 0; j < size; ++j) env[i][j] = (char)block[start + j];
        ++i; ++p;
    }
    return env;
}
