/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT */
#include "CKBNUM.H"
#include <float.h>
#include <math.h>
#include <string.h>

/* Watcom's 8086 conversion to float rounds ties away from zero. */
double ckb_single_precision(double n)
{
    double magnitude, scaled, whole, fraction;
    int exponent, scale;
    if (n == 0) return n;
    magnitude = fabs(n);
    frexp(magnitude, &exponent);
    scale = exponent < -125 ? -149 : exponent - 24;
    scaled = ldexp(magnitude, -scale);
    whole = floor(scaled);
    fraction = scaled - whole;
    if (fraction > .5 || (fraction == .5 && fmod(whole, 2) != 0)) ++whole;
    magnitude = ldexp(whole, scale);
    return n < 0 ? -magnitude : magnitude;
}

/* IEEE values use little endian on disk regardless of the host byte order. */
static void ieee_bytes(char *out, void *value, unsigned size, int encode)
{
    unsigned i, one = 1;
    unsigned char *native = (unsigned char *)value;
    for (i = 0; i < size; ++i) {
        unsigned at = *(unsigned char *)&one ? i : size - i - 1;
        if (encode) out[i] = (char)native[at];
        else native[at] = (unsigned char)out[i];
    }
}

static int special_double(const char *bytes)
{
    return ((unsigned char)bytes[7] & 127) == 127 &&
           ((unsigned char)bytes[6] & 240) == 240;
}

int ckb_num_is_special(double value)
{
    char bytes[8];
    ieee_bytes(bytes, &value, 8, 1);
    return special_double(bytes);
}

/* Widen an IEEE single without a Watcom float cast, preserving denormals
 * and the bits of nonfinite values. */
static void decode_single(double *out, const char *bytes)
{
    unsigned char high = (unsigned char)bytes[3];
    unsigned char middle = (unsigned char)bytes[2];
    unsigned exponent = ((high & 127) << 1) | (middle >> 7);
    unsigned long fraction = ((unsigned long)(middle & 127) << 16) |
                             ((unsigned long)(unsigned char)bytes[1] << 8) |
                             (unsigned char)bytes[0];
    if (exponent == 255 || (exponent == 0 && fraction == 0)) {
        char wide[8] = {0};
        if (exponent) {
            wide[3] = (char)(fraction << 5);
            wide[4] = (char)(fraction >> 3);
            wide[5] = (char)(fraction >> 11);
            wide[6] = (char)(240 | (fraction >> 19));
            wide[7] = 127;
        }
        wide[7] = (char)(wide[7] | (high & 128));
        ieee_bytes(wide, out, 8, 0);
    } else {
        double value = exponent ? ldexp(8388608.0 + fraction, (int)exponent - 150)
                                : ldexp((double)fraction, -149);
        *out = high & 128 ? -value : value;
    }
}

static void encode_special_single(char *out, const char *wide)
{
    unsigned long fraction = ((unsigned long)((unsigned char)wide[6] & 15) << 19) |
                             ((unsigned long)(unsigned char)wide[5] << 11) |
                             ((unsigned long)(unsigned char)wide[4] << 3) |
                             ((unsigned char)wide[3] >> 5);
    out[0] = (char)fraction;
    out[1] = (char)(fraction >> 8);
    out[2] = (char)(128 | (fraction >> 16));
    out[3] = (char)(127 | ((unsigned char)wide[7] & 128));
}

CkbNumResult ckb_num_encode(char *out, double value, int is_double, int mbf)
{
    unsigned size = is_double ? 8 : 4;
    char wide[8];
    int exponent, i, part;
    double fraction;
    ieee_bytes(wide, &value, 8, 1);
    if (!mbf && is_double) {
        memcpy(out, wide, 8);
        return CKB_NUM_OK;
    }
    if (special_double(wide)) {
        if (mbf) return CKB_NUM_OVERFLOW;
        encode_special_single(out, wide);
        return CKB_NUM_OK;
    }
    if (!is_double) {
        value = ckb_single_precision(value);
        if (fabs(value) > FLT_MAX) return CKB_NUM_OVERFLOW;
    }
    if (!mbf) {
        if (fabs(value) < FLT_MIN) {
            unsigned long bits = (unsigned long)ldexp(fabs(value), 149);
            out[0] = (char)bits;
            out[1] = (char)(bits >> 8);
            out[2] = (char)(bits >> 16);
            out[3] = (char)((unsigned char)wide[7] & 128);
        } else {
            float single = (float)value;
            ieee_bytes(out, &single, size, 1);
        }
        return CKB_NUM_OK;
    }
    memset(out, 0, size);
    if (value == 0) return CKB_NUM_OK;
    fraction = frexp(fabs(value), &exponent);
    if (exponent + 128 > 255) return CKB_NUM_OVERFLOW;
    if (exponent + 128 <= 0) return CKB_NUM_OK;
    out[size - 1] = (char)(exponent + 128);
    fraction = fraction * 2 - 1;
    fraction *= 128;
    part = (int)fraction;
    out[size - 2] = (char)(part | (value < 0 ? 128 : 0));
    fraction -= part;
    for (i = (int)size - 3; i >= 0; --i) {
        fraction *= 256;
        part = (int)fraction;
        out[i] = (char)part;
        fraction -= part;
    }
    return CKB_NUM_OK;
}

CkbNumResult ckb_num_decode(double *out, const char *bytes, unsigned length,
                   int is_double, int mbf)
{
    unsigned size = is_double ? 8 : 4, i;
    double value;
    unsigned exponent;
    char buffer[8];
    if (length < size) return CKB_NUM_INVALID;
    if (!mbf) {
        memcpy(buffer, bytes, size);
        if (is_double) ieee_bytes(buffer, out, size, 0);
        else decode_single(out, buffer);
        return CKB_NUM_OK;
    }
    exponent = (unsigned char)bytes[size - 1];
    if (exponent < (is_double ? 1u : 3u)) value = 0;
    else {
        value = 0;
        for (i = 0; i < size - 2; ++i)
            value = (value + (unsigned char)bytes[i]) / 256;
        value = (value + ((unsigned char)bytes[size - 2] & 127)) / 128;
        value = ldexp((1 + value) / 2, (int)exponent - 128);
        if ((unsigned char)bytes[size - 2] & 128) value = -value;
        if (!is_double) {
            value = ckb_single_precision(value);
            if (fabs(value) > FLT_MAX) return CKB_NUM_OVERFLOW;
        }
    }
    *out = value;
    return CKB_NUM_OK;
}
