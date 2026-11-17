/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "CKBSTR.H"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
