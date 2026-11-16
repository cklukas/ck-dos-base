/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "CKBFMT.H"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { LITERAL, FIRST, STRING, FIXED_STRING, NUMBER } FormatField;

typedef struct {
    FormatField kind;
    unsigned width;
    int whole, fraction, point, group, exponent;
    int stars, dollar;
    char leading_sign, trailing_sign;
} Field;

static char at(const CkbFormat *format, unsigned pos)
{
    return pos < format->length ? format->text[pos] : 0;
}

static Field field_at(const CkbFormat *format)
{
    Field field;
    unsigned pos = format->pos;
    unsigned start = pos;
    char first = at(format, pos);

    memset(&field, 0, sizeof(field));
    field.width = 1;
    if (first == '!' || first == '&') {
        field.kind = first == '!' ? FIRST : STRING;
        return field;
    }
    if (first == '\\') {
        ++pos;
        while (at(format, pos) == ' ') ++pos;
        if (at(format, pos) == '\\') {
            field.kind = FIXED_STRING;
            field.width = pos - start + 1;
        }
        return field;
    }

    if (first == '+') {
        field.leading_sign = '+';
        ++pos;
    }
    if (at(format, pos) == '*' && at(format, pos + 1) == '*') {
        field.stars = 1;
        field.whole = 2;
        pos += 2;
        if (at(format, pos) == '$') {
            field.dollar = 1;
            ++pos;
        }
    } else if (at(format, pos) == '$' && at(format, pos + 1) == '$') {
        field.dollar = 1;
        field.whole = 1;
        pos += 2;
    }
    while (at(format, pos) == '#' || (field.whole && at(format, pos) == ',')) {
        if (at(format, pos++) == '#') ++field.whole;
        else ++field.group;
    }
    if (at(format, pos) == '.' && (field.whole || at(format, pos + 1) == '#')) {
        field.point = 1;
        ++pos;
        while (at(format, pos) == '#') {
            ++field.fraction;
            ++pos;
        }
    }
    if (!(field.whole || field.fraction)) return field;

    if (at(format, pos) == '^' && at(format, pos + 1) == '^' &&
        at(format, pos + 2) == '^' && at(format, pos + 3) == '^') {
        field.exponent = at(format, pos + 4) == '^' ? 5 : 4;
        pos += field.exponent;
    }
    if (!field.leading_sign && (at(format, pos) == '+' || at(format, pos) == '-'))
        field.trailing_sign = at(format, pos++);
    field.kind = NUMBER;
    field.width = pos - start;
    return field;
}

static Field next_field(CkbFormat *format, CkbFormatEmit emit, void *context)
{
    Field field;
    while (format->pos < format->length) {
        field = field_at(format);
        if (field.kind != LITERAL) return field;
        if (at(format, format->pos) == '_' && format->pos + 1 < format->length)
            ++format->pos;
        emit(context, format->text + format->pos, 1);
        ++format->pos;
    }
    memset(&field, 0, sizeof(field));
    return field;
}

static void padding(CkbFormatEmit emit, void *context, char byte, unsigned count)
{
    char block[16];
    memset(block, byte, sizeof(block));
    while (count) {
        unsigned chunk = count < sizeof(block) ? count : sizeof(block);
        emit(context, block, chunk);
        count -= chunk;
    }
}

/* Convert once at BASIC's effective precision, then round at the selected
 * decimal position. This also gives harmless zeroes beyond that precision. */
#define DECIMAL_DIGITS 16

typedef struct {
    char digits[DECIMAL_DIGITS];
    int power;
} Decimal;

static void decimal_from_value(Decimal *decimal, const CkbValue *value)
{
    char text[32];
    int i, precision;
    double magnitude = value->number < 0 ? -value->number : value->number;

    precision = value->kind == '%' || value->kind == '&' ? 10 : DECIMAL_DIGITS;
    sprintf(text, "%.*E", precision - 1, magnitude);
    decimal->power = atoi(text + precision + 2);
    decimal->digits[0] = text[0];
    for (i = 1; i < precision; ++i) decimal->digits[i] = text[i + 1];
    for (; i < DECIMAL_DIGITS; ++i) decimal->digits[i] = '0';
}

static void decimal_round(Decimal *decimal, int keep)
{
    int pos;
    if (keep >= DECIMAL_DIGITS) return;
    if (keep >= 0 && decimal->digits[keep] >= '5') {
        pos = keep;
        while (pos && decimal->digits[pos - 1] == '9') --pos;
        if (pos) {
            ++decimal->digits[pos - 1];
            memset(decimal->digits + pos, '0', DECIMAL_DIGITS - pos);
            return;
        }
        memset(decimal->digits, '0', DECIMAL_DIGITS);
        decimal->digits[0] = '1';
        ++decimal->power;
        return;
    }
    if (keep < 0) keep = 0;
    memset(decimal->digits + keep, '0', DECIMAL_DIGITS - keep);
}

static char decimal_digit(const Decimal *decimal, int power)
{
    int i = decimal->power - power;
    return i >= 0 && i < DECIMAL_DIGITS ? decimal->digits[i] : '0';
}

static void print_number(const Field *field, const CkbValue *value,
                         CkbFormatEmit emit, void *context)
{
    char number[DBL_MAX_10_EXP * 2 + 32];
    char *out = number;
    Decimal decimal;
    int exponent = 0, power, whole, digits, i;
    int negative = value->number < 0;

    decimal_from_value(&decimal, value);
    if (field->exponent) {
        whole = field->whole;
        if (whole && whole + field->fraction > 1 &&
            !field->leading_sign && !field->trailing_sign) --whole;
        digits = whole + field->fraction;
        decimal_round(&decimal, digits);
        if (decimal.digits[0] != '0') exponent = decimal.power - whole + 1;
        decimal.power = whole - 1;
        if (!whole && field->whole && !negative) whole = 1;
    } else {
        decimal_round(&decimal, decimal.power + field->fraction + 1);
        whole = decimal.digits[0] != '0' && decimal.power >= 0 ? decimal.power + 1 : 0;
        if (!whole && field->whole &&
            !(negative && field->whole + field->group == 1 &&
              !field->leading_sign && !field->trailing_sign && field->point))
            whole = 1;
    }

    if (field->leading_sign || (!field->trailing_sign && negative))
        *out++ = negative ? '-' : '+';
    else if (field->exponent && !field->trailing_sign && field->whole > whole)
        *out++ = ' ';
    if (field->dollar) *out++ = '$';

    for (power = whole - 1; power >= 0; --power) {
        *out++ = decimal_digit(&decimal, power);
        if (!field->exponent && field->group && power && power % 3 == 0)
            *out++ = ',';
    }
    if (field->point) *out++ = '.';
    for (i = 1; i <= field->fraction; ++i) *out++ = decimal_digit(&decimal, -i);
    if (field->exponent) {
        *out++ = value->kind == '#' ? 'D' : 'E';
        *out++ = exponent < 0 ? '-' : '+';
        if (exponent < 0) exponent = -exponent;
        if (field->exponent == 5 || exponent >= 100)
            *out++ = (char)('0' + exponent / 100);
        *out++ = (char)('0' + exponent / 10 % 10);
        *out++ = (char)('0' + exponent % 10);
    }
    if (field->trailing_sign)
        *out++ = negative ? '-' : (field->trailing_sign == '+' ? '+' : ' ');

    if ((unsigned)(out - number) > field->width) emit(context, "%", 1);
    else padding(emit, context, field->stars ? '*' : ' ',
                 field->width - (unsigned)(out - number));
    emit(context, number, (unsigned)(out - number));
}

void ckb_format_init(CkbFormat *format, const char *text, unsigned length)
{
    format->text = text;
    format->length = length;
    format->pos = 0;
}

CkbFormatResult ckb_format_value(CkbFormat *format, const CkbValue *value,
                                 CkbFormatEmit emit, void *context)
{
    unsigned start = format->pos;
    unsigned length;
    Field field = next_field(format, emit, context);
    if (field.kind == LITERAL && start) {
        format->pos = 0;
        field = next_field(format, emit, context);
    }
    if (field.kind == LITERAL) return CKB_FORMAT_INVALID;
    if ((field.kind == NUMBER) == !!value->string) return CKB_FORMAT_TYPE;
    if (field.kind == NUMBER) {
        if (field.whole + field.group + field.fraction + field.dollar > 24)
            return CKB_FORMAT_INVALID;
        print_number(&field, value, emit, context);
    } else {
        length = value->length;
        if (field.kind != STRING && length > field.width) length = field.width;
        if (length) emit(context, ckb_string_bytes(value), length);
        if (field.kind != STRING) padding(emit, context, ' ', field.width - length);
    }
    format->pos += field.width;
    return CKB_FORMAT_OK;
}

void ckb_format_finish(CkbFormat *format, CkbFormatEmit emit, void *context)
{
    next_field(format, emit, context);
}
