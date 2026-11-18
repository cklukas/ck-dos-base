/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "CKBSTR.H"
#include "CKBRUNP.H"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__WATCOMC__) || defined(_MSDOS)
#include <dos.h>
#include <malloc.h>
#endif

enum { ASC = 1, CHR, INSTR, LEFT, LEN, MID, SPACE, UCASE, VAL,
       RIGHT, LTRIM, RTRIM, REPEAT, STR, LCASE };

struct CkbStringTemp {
    CkbStringTemp *previous;
};

#define temporaries ckb_temporaries
CkbStringTemp *temporaries;
static char temp_arena[256];
static unsigned temp_arena_used;

CkbStringTemp *ckb_string_mark(void)
{
    return temporaries;
}

void ckb_string_release(CkbStringTemp *mark)
{
    while (temporaries != mark) {
        CkbStringTemp *item = temporaries;
        char *raw = (char *)item;
        temporaries = item->previous;
        if (raw >= temp_arena && raw < temp_arena + sizeof(temp_arena))
            temp_arena_used = (unsigned)(raw - temp_arena);
        else
            free(item);
    }
}

static char *string_temp(unsigned length)
{
    CkbStringTemp *item;
    unsigned need = (unsigned)((sizeof(*item) + length + 2U) & ~1U);
    if (need <= sizeof(temp_arena) - temp_arena_used) {
        item = (CkbStringTemp *)(temp_arena + temp_arena_used);
        temp_arena_used += need;
    } else {
        item = malloc(sizeof(*item) + length + 1U);
        if (!item) return NULL;
    }
    item->previous = temporaries;
    temporaries = item;
    return (char *)(item + 1);
}

char *ckb_string_temp_alloc(unsigned length)
{
    return string_temp(length);
}

char *ckb_string_buffer(CkbValue *value, unsigned length)
{
    if (length > CKB_INLINE_STRING) {
        char *bytes = string_temp(length);
        if (!bytes) return NULL;
        value->wide = bytes;
    }
    value->length = length;
    return ckb_string_buffer_at(value);
}

char *ckb_string_buffer_at(CkbValue *value)
{
    return value->length > CKB_INLINE_STRING ? value->wide : value->text;
}

const char *ckb_string_bytes(const CkbValue *value)
{
    return value->length > CKB_INLINE_STRING ? value->wide : value->text;
}

int ckb_string_compare(const CkbValue *a, const CkbValue *b)
{
    unsigned common = a->length < b->length ? a->length : b->length;
    int result = memcmp(ckb_string_bytes(a), ckb_string_bytes(b), common);
    if (result) return result;
    return (a->length > b->length) - (a->length < b->length);
}

/* Prefer a leading decimal point when the rounded fractional digits fit. */
static void small_number_text(char *text, char *exponent, unsigned precision)
{
    int zeros = atoi(exponent + 1) * -1 - 1;
    char digits[16];
    unsigned length = 0;
    char *p;
    if (zeros < 0 || (unsigned)zeros >= precision) return;
    for (p = text + 1; p < exponent; ++p)
        if (*p != '.') digits[length++] = *p;
    if (length + (unsigned)zeros > precision) return;
    text[1] = '.';
    memset(text + 2, '0', (size_t)zeros);
    memcpy(text + 2 + zeros, digits, length);
    text[2 + zeros + length] = 0;
}

void ckb_number_text(char *text, const CkbValue *value)
{
    double number = value->number;
    char kind = value->kind;
    if (kind == '%' || kind == '&') {
        long lv = (long)number;
        unsigned long uv;
        char rev[16];
        unsigned n = 0;
        if (lv < 0) {
            *text++ = '-';
            uv = 0UL - (unsigned long)lv;
        } else {
            *text++ = ' ';
            uv = (unsigned long)lv;
        }
        do {
            rev[n++] = (char)('0' + (unsigned)(uv % 10UL));
            uv /= 10UL;
        } while (uv != 0UL);
        while (n--) *text++ = rev[n];
        *text = 0;
    } else {
        char *exponent;
        if (number == 0) number = 0;
        sprintf(text, kind == '#' ? "% .16G" : "% .7G", number);
        exponent = strchr(text, 'E');
        if (exponent) {
            if (kind == '#') *exponent = 'D';
            small_number_text(text, exponent, kind == '#' ? 16 : 7);
        }
        if (text[1] == '0' && text[2] == '.')
            memmove(text + 1, text + 2, strlen(text + 2) + 1);
    }
}

int ckb_string_function(const char *name)
{
    static const char *const names[] = {
        "ASC", "CHR$", "INSTR", "LEFT$", "LEN", "MID$", "SPACE$", "UCASE$", "VAL",
        "RIGHT$", "LTRIM$", "RTRIM$", "STRING$", "STR$", "LCASE$"
    };
    unsigned i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (names[i][0] == name[0] && strcmp(names[i], name) == 0) return (int)i + 1;
    return 0;
}

static unsigned argument_types(int id, unsigned count)
{
    /* Bit i marks a string argument. Eight is an invalid signature. */
    switch (id) {
    case ASC: case LEN: case UCASE: case LCASE: case VAL: case LTRIM: case RTRIM:
        return count == 1 ? 1 : 8;
    case CHR: case SPACE: case STR: return count == 1 ? 0 : 8;
    case LEFT: case RIGHT: return count == 2 ? 1 : 8;
    case REPEAT: return count == 2 ? 0 : 8;
    case MID: return count == 2 || count == 3 ? 1 : 8;
    case INSTR: return count == 2 ? 3 : count == 3 ? 6 : 8;
    default: return 8;
    }
}

static int integer_argument(double n, int minimum, int maximum, int *out)
{
    double result, fraction;
#if defined(_MSDOS) || defined(__WATCOMC__) || defined(__LITTLE_ENDIAN__) || defined(__x86_64__) || defined(__aarch64__)
    {
        const unsigned short *w = (const unsigned short *)&n;
        unsigned int w3 = w[3];
        if (w[0] == 0 && w[1] == 0) {
            if ((w3 & 0x7FFFU) == 0 && w[2] == 0) {
                if (0 < minimum || 0 > maximum) return 0;
                *out = 0;
                return 1;
            }
            {
                unsigned int bexp = (w3 >> 4) & 0x7FFU;
                if (bexp >= 1023U && bexp <= 1043U) {
                    unsigned int shift = 1043U - bexp;
                    unsigned long hi21 = (((unsigned long)(w3 & 0x000FU) << 16) | (unsigned long)w[2]) | 0x00100000UL;
                    unsigned long mag = hi21 >> shift;
                    if ((mag << shift) == hi21) {
                        long lv = (w3 & 0x8000U) ? -(long)mag : (long)mag;
                        if (lv < (long)minimum || lv > (long)maximum) return 0;
                        *out = (int)lv;
                        return 1;
                    }
                }
            }
        }
    }
#endif
    if (!(n >= (double)minimum - .5 && n <= (double)maximum + .5)) return 0;
    result = floor(n);
    fraction = n - result;
    if (fraction > .5 || (fraction == .5 && ((long)result % 2) != 0)) ++result;
    if (result < minimum || result > maximum) return 0;
    *out = (int)result;
    return 1;
}

static double numeric_prefix(const char *text, char *number)
{
    char *end;
    unsigned used = 0;
    int base = 8, sign = 1;
    unsigned long value = 0;
    const char *p;
    for (; *text; ++text) {
        if (*text != ' ' && *text != '\t' && *text != '\n' && *text != '\r')
            number[used++] = *text;
    }
    number[used] = 0;
    p = number;
    if (*p == '+' || *p == '-') {
        if (*p == '-') sign = -1;
        ++p;
    }
    if (*p == '&') {
        ++p;
        if (*p == 'H' || *p == 'h') {
            base = 16;
            ++p;
        } else if (*p == 'O' || *p == 'o') {
            ++p;
        }
        for (; *p; ++p) {
            unsigned digit;
            if (*p >= '0' && *p <= '9') digit = (unsigned)(*p - '0');
            else if (*p >= 'A' && *p <= 'F') digit = (unsigned)(*p - 'A' + 10);
            else if (*p >= 'a' && *p <= 'f') digit = (unsigned)(*p - 'a' + 10);
            else break;
            if (digit >= (unsigned)base) break;
            value = (value * (unsigned)base + digit) & 0xffffffffUL;
        }
        if (value > 0x7fffffffUL) return sign * ((double)value - 4294967296.0);
        if (value >= 0x8000UL && value <= 0xffffUL && *p != '&')
            return sign * ((double)value - 65536.0);
        return sign * (double)value;
    }
    if (!((*p >= '0' && *p <= '9') || *p == '.')) return 0;
    if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) return 0;
    end = strpbrk(number, "Dd");
    if (end) *end = 'E';
    return strtod(number, &end);
}

double ckb_numeric_prefix(const char *text)
{
    char number[CKB_INLINE_STRING + 1];
    return numeric_prefix(text, number);
}

const char *ckb_string_apply(int id, CkbValue *out, const CkbValue *args,
                             unsigned count, int run)
{
    unsigned types = argument_types(id, count), i;
    size_t length;
    const char *text, *needle;
    char *result;
    int start, size, byte;
    if (types == 8) return "Incorrect number of arguments";
    if (id == REPEAT && args[1].string) types = 2;
    for (i = 0; i < count; ++i)
        if (!!args[i].string != (int)((types >> i) & 1U)) return "Type mismatch";
    out->number = 0;
    out->text[0] = 0;
    out->length = 0;
    out->string = id != ASC && id != INSTR && id != LEN && id != VAL;
    if (!run) return NULL;

    switch (id) {
    case ASC:
        if (!args[0].length) return "Illegal function call";
        out->number = (unsigned char)ckb_string_bytes(&args[0])[0];
        break;
    case CHR:
        if (!integer_argument(args[0].number, 0, 255, &size)) return "Illegal function call";
        out->text[0] = (char)size;
        out->text[1] = 0;
        out->length = 1;
        break;
    case INSTR:
        start = 1;
        i = count - 2;
        if (i && !integer_argument(args[0].number, 1, 32767, &start)) return "Illegal function call";
        text = ckb_string_bytes(&args[i]);
        needle = ckb_string_bytes(&args[i + 1]);
        length = args[i + 1].length;
        if (length <= args[i].length && (unsigned)start <= args[i].length) {
            unsigned offset;
            for (offset = (unsigned)(start - 1); offset <= args[i].length - length; ++offset)
                if (memcmp(text + offset, needle, length) == 0) {
                    out->number = offset + 1;
                    break;
                }
        }
        break;
    case LEFT: case RIGHT:
    case MID:
        start = 1;
        size = CKB_STRING_MAX;
        if (id != MID) {
            if (!integer_argument(args[1].number, 0, 32767, &size)) return "Illegal function call";
        } else {
            if (!integer_argument(args[1].number, 1, 32767, &start)) return "Illegal function call";
            if (count == 3 && !integer_argument(args[2].number, 0, 32767, &size))
                return "Illegal function call";
        }
        text = ckb_string_bytes(&args[0]);
        length = args[0].length;
        if (!size) break;
        if (id == RIGHT && (size_t)size < length) start = (int)(length - size) + 1;
        if ((size_t)start <= length) {
            length -= (size_t)start - 1;
            if ((size_t)size > length) size = (int)length;
            result = ckb_string_buffer(out, (unsigned)size);
            if (!result) return "Out of memory";
            memcpy(result, text + start - 1, size);
            result[size] = 0;
        }
        break;
    case LEN:
        out->number = args[0].length;
        break;
    case LTRIM: case RTRIM:
        text = ckb_string_bytes(&args[0]);
        length = args[0].length;
        if (id == LTRIM) {
            while (length && *text == ' ') { ++text; --length; }
        } else {
            while (length && text[length - 1] == ' ') --length;
        }
        result = ckb_string_buffer(out, (unsigned)length);
        if (!result) return "Out of memory";
        memcpy(result, text, length);
        result[length] = 0;
        break;
    case SPACE: case REPEAT:
        if (!integer_argument(args[0].number, 0, 32767, &size)) return "Illegal function call";
        byte = ' ';
        if (id == REPEAT) {
            if (args[1].string) {
                if (!args[1].length) return "Illegal function call";
                byte = (unsigned char)ckb_string_bytes(&args[1])[0];
            } else if (!integer_argument(args[1].number, 0, 255, &byte)) {
                return "Illegal function call";
            }
        }
        result = ckb_string_buffer(out, (unsigned)size);
        if (!result) return "Out of memory";
        memset(result, byte, size);
        result[size] = 0;
        break;
    case UCASE: case LCASE:
        text = ckb_string_bytes(&args[0]);
        result = ckb_string_buffer(out, args[0].length);
        if (!result) return "Out of memory";
        for (i = 0; i < args[0].length; ++i) {
            int byte = (unsigned char)text[i];
            if (id == UCASE && byte >= 'a' && byte <= 'z') byte -= 'a' - 'A';
            if (id == LCASE && byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
            result[i] = (char)byte;
        }
        result[i] = 0;
        break;
    case VAL:
        text = ckb_string_bytes(&args[0]);
        if (args[0].length <= CKB_INLINE_STRING) out->number = ckb_numeric_prefix(text);
        else {
            result = string_temp(args[0].length);
            if (!result) return "Out of memory";
            out->number = numeric_prefix(text, result);
        }
        break;
    case STR:
        ckb_number_text(out->text, &args[0]);
        out->length = (unsigned)strlen(out->text);
        break;
    }
    return NULL;
}

void __far *ckb_compact_block(void __far *ptr, unsigned active_bytes)
{
    void __far *np;
    if (!ptr) return NULL;
    if (!active_bytes) {
        _ffree(ptr);
        return NULL;
    }
#ifdef _CKCL_DOS_H
    np = _fmalloc(active_bytes);
    if (np) {
        if (FP_SEG(np) < FP_SEG(ptr)) {
            _fmemcpy(np, ptr, active_bytes);
            _ffree(ptr);
            return np;
        }
        _ffree(np);
    }
    _dos_setblock((active_bytes + 15U) >> 4, FP_SEG(ptr), NULL);
#endif
    return ptr;
}

void packed_number_far(char __far *item, char kind, double *number, int store)
{
    unsigned sz = kind == '#' ? 8U : (kind == '%' ? 2U : 4U);
    char buf[8];
    if (!store) _fmemcpy(buf, item, sz);
    packed_number(buf, kind, number, store);
    if (store) _fmemcpy(item, buf, sz);
}

#define CKB_STR_SLABS     4U
#define CKB_STR_SLAB_SIZE 16384U
#define CKB_STR_SLAB_CAP  (CKB_STR_SLAB_SIZE - 4U)

static unsigned char __far *str_slabs[CKB_STR_SLABS];
static unsigned long str_used_bytes;

char __far *ckb_salloc(unsigned n)
{
    unsigned s;
    unsigned char __far *hdr;
    n = (n + 1U) & ~1U;
    if (!n) n = 2U;
    if (n <= CKB_STR_SLAB_CAP) {
        for (s = 0; s < CKB_STR_SLABS; ++s) {
            unsigned char __far *slab = str_slabs[s];
            unsigned off = 0;
            if (!slab) continue;
            while (off < CKB_STR_SLAB_SIZE) {
                unsigned sz;
                hdr = slab + off;
                sz = (unsigned)hdr[0] | ((unsigned)hdr[1] << 8);
                if (hdr[2] == 0 && sz >= n) {
                    if (sz >= n + 8U) {
                        unsigned rem = sz - n - 4U;
                        unsigned char __far *nh = hdr + 4U + n;
                        nh[0] = (unsigned char)rem;
                        nh[1] = (unsigned char)(rem >> 8);
                        nh[2] = 0;
                        nh[3] = (unsigned char)s;
                        sz = n;
                    }
                    hdr[0] = (unsigned char)sz;
                    hdr[1] = (unsigned char)(sz >> 8);
                    hdr[2] = 1;
                    hdr[3] = (unsigned char)s;
                    str_used_bytes += (unsigned long)sz + 4UL;
                    return (char __far *)(hdr + 4U);
                }
                off += 4U + sz;
            }
        }
        for (s = 0; s < CKB_STR_SLABS; ++s) {
            unsigned char __far *slab;
            if (str_slabs[s]) continue;
            slab = (unsigned char __far *)_fmalloc(CKB_STR_SLAB_SIZE);
            if (!slab) break;
            str_slabs[s] = slab;
            if (CKB_STR_SLAB_CAP >= n + 8U) {
                unsigned rem = CKB_STR_SLAB_CAP - n - 4U;
                unsigned char __far *nh = slab + 4U + n;
                nh[0] = (unsigned char)rem;
                nh[1] = (unsigned char)(rem >> 8);
                nh[2] = 0;
                nh[3] = (unsigned char)s;
                slab[0] = (unsigned char)n;
                slab[1] = (unsigned char)(n >> 8);
                str_used_bytes += (unsigned long)n + 4UL;
            } else {
                slab[0] = (unsigned char)CKB_STR_SLAB_CAP;
                slab[1] = (unsigned char)(CKB_STR_SLAB_CAP >> 8);
                str_used_bytes += (unsigned long)CKB_STR_SLAB_CAP + 4UL;
            }
            slab[2] = 1;
            slab[3] = (unsigned char)s;
            return (char __far *)(slab + 4U);
        }
    }
    hdr = (unsigned char __far *)_fmalloc(n + 4U);
    if (!hdr) return NULL;
    hdr[0] = (unsigned char)n;
    hdr[1] = (unsigned char)(n >> 8);
    hdr[2] = 0xff;
    hdr[3] = 0;
    str_used_bytes += (unsigned long)n + 4UL;
    return (char __far *)(hdr + 4U);
}

void ckb_sfree(char __far *ptr)
{
    unsigned char __far *hdr, *slab;
    unsigned sz, s, off;
    if (!ptr) return;
    hdr = (unsigned char __far *)ptr - 4U;
    sz = (unsigned)hdr[0] | ((unsigned)hdr[1] << 8);
    if (str_used_bytes >= (unsigned long)sz + 4UL)
        str_used_bytes -= (unsigned long)sz + 4UL;
    else
        str_used_bytes = 0;
    if (hdr[2] == 0xff) {
        _ffree(hdr);
        return;
    }
    s = hdr[3];
    hdr[2] = 0;
    if (s >= CKB_STR_SLABS || !str_slabs[s]) return;
    slab = str_slabs[s];
    off = 0;
    while (off < CKB_STR_SLAB_SIZE) {
        unsigned char __far *h = slab + off;
        unsigned bsz = (unsigned)h[0] | ((unsigned)h[1] << 8);
        unsigned next = off + 4U + bsz;
        if (h[2] == 0 && next < CKB_STR_SLAB_SIZE && slab[next + 2U] == 0) {
            unsigned nsz = (unsigned)slab[next] | ((unsigned)slab[next + 1U] << 8);
            bsz += 4U + nsz;
            h[0] = (unsigned char)bsz;
            h[1] = (unsigned char)(bsz >> 8);
            continue;
        }
        off = next;
    }
    if (slab[2] == 0 &&
        (((unsigned)slab[0] | ((unsigned)slab[1] << 8)) == CKB_STR_SLAB_CAP)) {
        _ffree(slab);
        str_slabs[s] = NULL;
    }
}

void ckb_sreset(void)
{
    unsigned s;
    for (s = 0; s < CKB_STR_SLABS; ++s) {
        if (str_slabs[s]) {
            _ffree(str_slabs[s]);
            str_slabs[s] = NULL;
        }
    }
    str_used_bytes = 0;
}

static long dos_max_free_bytes(void)
{
#ifdef _CKCL_DOS_H
    unsigned max_paras = 0;
    _dos_allocmem(0xffffU, &max_paras);
    return (long)max_paras * 16L;
#else
    return 163840L;
#endif
}

long ckb_fre(int is_str, long arg)
{
    long conv;
    unsigned s;
    long slab_free = 0;
    if (!is_str && arg == -1L) return dos_max_free_bytes();
    if (!is_str && arg == -2L) {
#if defined(__WATCOMC__) || defined(_MSDOS)
        return (long)stackavail();
#else
        return 14336L;
#endif
    }
    for (s = 0; s < CKB_STR_SLABS; ++s) {
        unsigned char __far *slab = str_slabs[s];
        unsigned off = 0;
        if (!slab) continue;
        while (off < CKB_STR_SLAB_SIZE) {
            unsigned sz = (unsigned)slab[off] | ((unsigned)slab[off + 1U] << 8);
            if (slab[off + 2U] == 0) slab_free += (long)sz;
            off += 4U + sz;
        }
    }
    conv = dos_max_free_bytes();
    if (conv > 0) slab_free += conv;
    {
        long target = str_used_bytes < 32760UL ? (long)(32760UL - str_used_bytes) : 4096L;
        return slab_free < target ? slab_free : target;
    }
}

/* =========================================================================
 * Type, numeric conversion, storage helpers & event traps (shared segment)
 * ========================================================================= */

RecordType *record_type(char kind)
{
    unsigned id = (unsigned char)kind;
    return id && id <= record_count ? &records[id - 1] : NULL;
}

unsigned record_size(char kind)
{
    const RecordType *type = record_type(kind);
    return type ? type->size : 0;
}

unsigned member_size(char kind, unsigned width)
{
    switch (kind) {
    case '$': return width;
    case '%': return 2;
    case '&': case '!': return 4;
    case '#': return 8;
    default: return record_size(kind);
    }
}

unsigned array_element_size(const Variable *v)
{
    return v->kind == '$' ? sizeof(char __far *) : member_size(v->kind, 0);
}

double round_even(double n)
{
    double low = floor(n);
    double fraction = n - low;
    return fraction > .5 || (fraction == .5 && fmod(low, 2) != 0) ? low + 1 : low;
}

int finite_number(double n)
{
    return !ckb_num_is_special(n);
}

int try_exact_int21(double value, long *out)
{
    const unsigned short *w = (const unsigned short *)&value;
    unsigned int w3 = w[3];
    unsigned int bexp;
    if (w[0] != 0 || w[1] != 0) return 0;
    if ((w3 & 0x7FFFU) == 0 && w[2] == 0) {
        *out = 0;
        return 1;
    }
    bexp = (w3 >> 4) & 0x7FFU;
    if (bexp >= 1023U && bexp <= 1043U) {
        unsigned int shift = 1043U - bexp;
        unsigned long hi21 = (((unsigned long)(w3 & 0x000FU) << 16) | (unsigned long)w[2]) | 0x00100000UL;
        unsigned long mag = hi21 >> shift;
        if ((mag << shift) == hi21) {
            *out = (w3 & 0x8000U) ? -(long)mag : (long)mag;
            return 1;
        }
    }
    return 0;
}

void convert_number(Parser *p, Value *v, char kind, int run)
{
    double n = v->number;
    long lv;
    v->kind = kind;
    if (!run) return;
    if (kind == '%') {
        if (try_exact_int21(n, &lv)) {
            if (lv >= -32768L && lv <= 32767L) return;
            fail(p, "Overflow");
            return;
        }
        n = round_even(n);
    } else if (kind == '&') {
        if (try_exact_int21(n, &lv)) return;
        if (n >= -2147483648.0 && n <= 2147483647.0) {
            lv = (long)n;
            if ((double)lv == n) return;
        }
        n = round_even(n);
    }
    if (!finite_number(n) ||
        (kind == '%' && (n < -32768.0 || n > 32767.0)) ||
        (kind == '&' && (n < -2147483648.0 || n > 2147483647.0)) ||
        (kind == '!' && (n < -3.4028234663852886e38 || n > 3.4028234663852886e38))) {
        fail(p, "Overflow");
        return;
    }
    if (kind == '!') n = ckb_single_precision(n);
    v->number = n;
}

Variable *variable_storage(int id)
{
    unsigned slot = variables[id].frame_slot;
    return slot ? &activation->locals[slot - 1] : &variables[id];
}

Variable *array_storage(int id)
{
    Variable *v = variable_storage(id);
    return v->reference.target ? v->reference.target : v;
}

Reference reference_at(int id, unsigned cell)
{
    Variable *v = variable_storage(id);
    Reference ref;
    if (v->reference.target) {
        ref = v->reference;
        if (v->array) ref.cell = cell;
        return ref;
    }
    ref.target = v;
    ref.cell = cell;
    ref.offset = 0;
    ref.width = v->fixed_width;
    ref.kind = v->kind;
    return ref;
}

int valid_cell(Parser *p, const Variable *v, unsigned cell)
{
    if (v->array && (!v->elements || cell >= v->cells)) {
        fail(p, "Subscript out of range");
        return 0;
    }
    return 1;
}

int ckbr_has_events;
static unsigned timer_trap_line;
static unsigned char timer_trap_state;
static unsigned char timer_trap_pending;
static double timer_trap_period;
static double timer_trap_last;
static unsigned play_trap_line;
static unsigned char play_trap_state;
static unsigned char play_trap_thresh;
static unsigned key_trap_line[26];
static unsigned char key_trap_state[26];
static unsigned char in_event_trap;
static unsigned event_return_depth;

void ckbr_reset_events(void)
{
    unsigned i;
    ckbr_has_events = 0;
    timer_trap_line = LINE_MAX;
    timer_trap_state = 0;
    timer_trap_pending = 0;
    timer_trap_period = 0.0;
    timer_trap_last = 0.0;
    play_trap_line = LINE_MAX;
    play_trap_state = 0;
    play_trap_thresh = 1;
    for (i = 0; i < 26U; ++i) {
        key_trap_line[i] = LINE_MAX;
        key_trap_state[i] = 0;
    }
    in_event_trap = 0;
    event_return_depth = 0;
}

void ckbr_on_return_pop(void)
{
    if (in_event_trap && call_depth == event_return_depth)
        in_event_trap = 0;
}

void ckbr_timer_statement(Parser *p, int run)
{
    if (take_word(p, "ON")) {
        if (run) {
            timer_trap_state = 1;
            timer_trap_last = ckb_timer();
        }
    } else if (take_word(p, "OFF")) {
        if (run) { timer_trap_state = 0; timer_trap_pending = 0; }
    } else if (take_word(p, "STOP")) {
        if (run) timer_trap_state = 2;
    } else {
        fail(p, "Expected: ON, OFF, or STOP");
    }
}

int ckbr_play_ctrl(Parser *p, int run)
{
    if (take_word(p, "ON")) { if (run) play_trap_state = 1; return 1; }
    if (take_word(p, "OFF")) { if (run) play_trap_state = 0; return 1; }
    if (take_word(p, "STOP")) { if (run) play_trap_state = 2; return 1; }
    return 0;
}

void ckbr_key_statement(Parser *p, int run)
{
    if (take(p, '(')) {
        long n = integer_operand(p, numeric(p, run), run);
        int st = -1;
        expect(p, ')', "Expected: )");
        if (take_word(p, "ON")) st = 1;
        else if (take_word(p, "OFF")) st = 0;
        else if (take_word(p, "STOP")) st = 2;
        else { fail(p, "Expected: ON, OFF, or STOP"); return; }
        if (run && !error_message) {
            if (n < 1 || n > 25) fail(p, "Illegal function call");
            else key_trap_state[n] = (unsigned char)st;
        }
        return;
    }
    if (take_word(p, "OFF") || take_word(p, "ON") || take_word(p, "LIST"))
        return;
    {
        long n = integer_operand(p, numeric(p, run), run);
        Value def;
        expect(p, ',', "Expected: ,");
        expression(p, &def, 1, 0, run);
        if (!def.string) fail(p, "Type mismatch");
        else if (run && !error_message && (n < 1 || n > 25))
            fail(p, "Illegal function call");
    }
}

void ckbr_on_statement(Parser *p, int run)
{
    unsigned line;
    if (take_word(p, "ERROR")) {
        expect_word(p, "GOTO", "Expected: GOTO");
        if (p->token == T_NUMBER && p->number == 0) {
            scan(p);
            line = LINE_MAX;
        } else line = target_label(p);
        if (run && !error_message) error_handler[current_scope + 1] = line;
        return;
    }
    if (take_word(p, "TIMER")) {
        double secs;
        ckbr_has_events = 1;
        expect(p, '(', "Expected: (");
        secs = numeric(p, run);
        expect(p, ')', "Expected: )");
        expect_word(p, "GOSUB", "Expected: GOSUB");
        if (p->token == T_NUMBER && p->number == 0) {
            scan(p);
            line = LINE_MAX;
        } else line = target_label(p);
        if (run && !error_message) {
            if (secs <= 0.0 || secs > 86400.0) fail(p, "Illegal function call");
            else {
                timer_trap_period = secs;
                timer_trap_line = line;
                timer_trap_last = ckb_timer();
                timer_trap_pending = 0;
            }
        }
        return;
    }
    if (take_word(p, "PLAY")) {
        long qlim;
        ckbr_has_events = 1;
        expect(p, '(', "Expected: (");
        qlim = integer_operand(p, numeric(p, run), run);
        expect(p, ')', "Expected: )");
        expect_word(p, "GOSUB", "Expected: GOSUB");
        if (p->token == T_NUMBER && p->number == 0) {
            scan(p);
            line = LINE_MAX;
        } else line = target_label(p);
        if (run && !error_message) {
            if (qlim < 1 || qlim > 32) fail(p, "Illegal function call");
            else {
                play_trap_thresh = (unsigned char)qlim;
                play_trap_line = line;
            }
        }
        return;
    }
    if (take_word(p, "KEY")) {
        long kn;
        ckbr_has_events = 1;
        expect(p, '(', "Expected: (");
        kn = integer_operand(p, numeric(p, run), run);
        expect(p, ')', "Expected: )");
        expect_word(p, "GOSUB", "Expected: GOSUB");
        if (p->token == T_NUMBER && p->number == 0) {
            scan(p);
            line = LINE_MAX;
        } else line = target_label(p);
        if (run && !error_message) {
            if (kn < 1 || kn > 25) fail(p, "Illegal function call");
            else key_trap_line[kn] = line;
        }
        return;
    }
    {
        long choice = integer_operand(p, numeric(p, run), run);
        int is_gosub = 0;
        unsigned idx = 1, chosen = LINE_MAX;
        if (take_word(p, "GOSUB")) is_gosub = 1;
        else if (!expect_word(p, "GOTO", "Expected: GOTO or GOSUB")) return;
        do {
            unsigned tgt = target_label(p);
            if (idx == (unsigned)choice) chosen = tgt;
            ++idx;
        } while (!error_message && take(p, ','));
        if (run && !error_message) {
            if (choice < 0 || choice > 255) fail(p, "Illegal function call");
            else if (choice >= 1 && chosen < LINE_MAX) {
                if (is_gosub) {
                    if (call_depth == STACK_MAX) fail(p, "Too many nested GOSUBs");
                    else {
                        Position aft;
                        if (p->token == ':') { aft.line = p->line; aft.offset = p->pos; }
                        else {
                            aft.line = p->line + 1U;
                            aft.offset = aft.line < line_count ? lines[aft.line].body : 0U;
                        }
                        calls[call_depth++] = aft;
                        jumped = 1;
                        next_position.line = chosen;
                        next_position.offset = lines[chosen].body;
                    }
                } else {
                    jumped = 1;
                    next_position.line = chosen;
                    next_position.offset = lines[chosen].body;
                }
            }
        }
    }
}

void ckbr_check_events(Position *at_io)
{
    if (in_event_trap || handling_error || current_scope >= 0 || call_depth >= STACK_MAX)
        return;
    if (timer_trap_line < line_count && (timer_trap_state == 1 || timer_trap_state == 2)) {
        double now = ckb_timer();
        double dt = now - timer_trap_last;
        if (dt < 0.0) dt += 86400.0;
        if (dt >= timer_trap_period) {
            timer_trap_last = now;
            if (timer_trap_state == 2) timer_trap_pending = 1;
            else {
                timer_trap_pending = 0;
                in_event_trap = 1;
                event_return_depth = call_depth;
                calls[call_depth++] = *at_io;
                at_io->line = timer_trap_line;
                at_io->offset = lines[timer_trap_line].body;
                return;
            }
        } else if (timer_trap_state == 1 && timer_trap_pending) {
            timer_trap_pending = 0;
            in_event_trap = 1;
            event_return_depth = call_depth;
            calls[call_depth++] = *at_io;
            at_io->line = timer_trap_line;
            at_io->offset = lines[timer_trap_line].body;
            return;
        }
    }
    if (play_trap_line < line_count && play_trap_state == 1) {
        int q = ckb_play_queued();
        if (q < (int)play_trap_thresh) {
            play_trap_state = 0;
            in_event_trap = 1;
            event_return_depth = call_depth;
            calls[call_depth++] = *at_io;
            at_io->line = play_trap_line;
            at_io->offset = lines[play_trap_line].body;
            return;
        }
    }
}
