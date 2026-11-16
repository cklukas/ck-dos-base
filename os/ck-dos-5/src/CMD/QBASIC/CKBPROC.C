/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include "CKBPROC.H"

static unsigned char upper(unsigned char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch;
}

static int name_start(unsigned char ch)
{
    ch = upper(ch);
    return ch >= 'A' && ch <= 'Z';
}

static int name_char(unsigned char ch)
{
    return name_start(ch) || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_';
}

static unsigned skip_space(const char CKB_PROC_FAR *source, unsigned at,
                           unsigned end)
{
    while (at < end && (source[at] == ' ' || source[at] == '\t')) ++at;
    return at;
}

static int match(const char CKB_PROC_FAR *source, unsigned *at, unsigned end,
                 const char *word)
{
    unsigned i = *at;
    while (*word && i < end && upper(source[i]) == (unsigned char)*word) {
        ++i;
        ++word;
    }
    if (*word || (i < end && name_char(source[i]))) return 0;
    *at = i;
    return 1;
}

static int proc_kind(const char CKB_PROC_FAR *source, unsigned *at, unsigned end)
{
    if (match(source, at, end, "SUB")) return CKB_PROC_SUB;
    if (match(source, at, end, "FUNCTION")) return CKB_PROC_FUNCTION;
    return 0;
}

static int is_proc_end(const char CKB_PROC_FAR *source, unsigned at,
                       unsigned end, int kind)
{
    if (proc_kind(source, &at, end) != kind) return 0;
    at = skip_space(source, at, end);
    return at == end || source[at] == '\'' || source[at] == ':';
}

static int name_less(const char CKB_PROC_FAR *source, const CkbProcedure *a,
                     const CkbProcedure *b)
{
    unsigned i;
    for (i = 0; i < a->name_length && i < b->name_length; ++i) {
        unsigned char x = upper(source[a->name + i]);
        unsigned char y = upper(source[b->name + i]);
        if (x != y) return x < y;
    }
    return a->name_length < b->name_length;
}

unsigned ckb_proc_index(const char CKB_PROC_FAR *source, unsigned length,
                        CkbProcedure *entries, unsigned capacity)
{
    unsigned start = 0, count = 0, i;
    int active = 0;
    while (start < length && (count < capacity || active)) {
        unsigned end = start, at, next, name;
        int kind;
        while (end < length && source[end] != '\n' && source[end] != '\r') ++end;
        next = end;
        if (next < length && source[next] == '\r') ++next;
        if (next < length && source[next] == '\n') ++next;
        at = skip_space(source, start, end);
        if (match(source, &at, end, "END")) {
            if (active && is_proc_end(source, skip_space(source, at, end), end, active)) {
                entries[count - 1].end = next;
                active = 0;
            }
        } else if ((kind = proc_kind(source, &at, end)) != 0) {
            name = skip_space(source, at, end);
            if (name > at && name < end && name_start(source[name])) {
                at = name + 1;
                while (at < end && name_char(source[at])) ++at;
                if (active) entries[count - 1].end = start;
                if (count == capacity) break;
                entries[count].start = start;
                entries[count].end = length;
                entries[count].name = name;
                entries[count].name_length = at - name;
                entries[count].kind = (unsigned char)kind;
                ++count;
                active = kind;
            }
        }
        start = next;
    }
    for (i = 1; i < count; ++i) {
        CkbProcedure entry = entries[i];
        unsigned j = i;
        while (j && name_less(source, &entry, &entries[j - 1])) {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = entry;
    }
    return count;
}

int ckb_proc_at(const CkbProcedure *entries, unsigned count, unsigned offset)
{
    unsigned i;
    for (i = 0; i < count; ++i)
        if (offset >= entries[i].start && offset < entries[i].end) return (int)i;
    return -1;
}
