/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_C_COMPAT_H
#define DOSBASE_DEVTOOLS_CORE_C_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(M_I86) && !defined(_MSC_VER)
/* 16-bit DOS target compiled with ckcl */
typedef unsigned char ck_u8;
typedef signed char ck_s8;
typedef unsigned short ck_u16;
typedef short ck_s16;
typedef unsigned long ck_u32;
typedef long ck_s32;
typedef unsigned int ck_usize;

#ifndef NULL
#define NULL ((void *)0)
#endif

void *malloc(unsigned int size);
void *calloc(unsigned int count, unsigned int size);
void *realloc(void *ptr, unsigned int new_size);
void free(void *ptr);
void *memcpy(void *dst, const void *src, unsigned int count);
void *memmove(void *dst, const void *src, unsigned int count);
void *memset(void *dst, int val, unsigned int count);
int memcmp(const void *a, const void *b, unsigned int count);
unsigned int strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, unsigned int n);
char *strcat(char *dst, const char *src);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, unsigned int n);
int strcmpi(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
int toupper(int c);
int tolower(int c);
int isalpha(int c);
int isdigit(int c);
int isxdigit(int c);
int atoi(const char *s);
long atol(const char *s);

#else
/* Modern host C99/C++20 target */
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t ck_u8;
typedef int8_t ck_s8;
typedef uint16_t ck_u16;
typedef int16_t ck_s16;
typedef uint32_t ck_u32;
typedef int32_t ck_s32;
typedef size_t ck_usize;

#if defined(__GNUC__) || defined(__clang__)
#define CK_INLINE static inline __attribute__((unused))
#else
#define CK_INLINE static
#endif

#if !defined(_WIN32)
CK_INLINE int ck_strcmpi(const char *a, const char *b) {
    unsigned char ca, cb;
    while (*a && *b) {
        ca = (unsigned char)tolower((unsigned char)*a);
        cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb) return (int)ca - (int)cb;
        ++a;
        ++b;
    }
    return (int)(unsigned char)tolower((unsigned char)*a) -
           (int)(unsigned char)tolower((unsigned char)*b);
}
#define strcmpi ck_strcmpi
#else
#define strcmpi _stricmp
#endif

#endif /* M_I86 */

#ifndef CK_INLINE
#define CK_INLINE static
#endif

typedef struct ck_error {
    ck_u32 offset;
    char message[160];
} ck_error;

typedef struct ck_byte_buf {
    ck_u8 *data;
    ck_u32 size;
    ck_u32 cap;
} ck_byte_buf;

CK_INLINE ck_u16 ck_read_u16_le(const ck_u8 *p) {
    return (ck_u16)((ck_u16)p[0] | ((ck_u16)p[1] << 8));
}

CK_INLINE ck_u32 ck_read_u32_le(const ck_u8 *p) {
    return (ck_u32)p[0] |
           ((ck_u32)p[1] << 8) |
           ((ck_u32)p[2] << 16) |
           ((ck_u32)p[3] << 24);
}

CK_INLINE void ck_write_u16_le(ck_u8 *p, ck_u16 v) {
    p[0] = (ck_u8)(v & 0xFF);
    p[1] = (ck_u8)((v >> 8) & 0xFF);
}

CK_INLINE void ck_write_u32_le(ck_u8 *p, ck_u32 v) {
    p[0] = (ck_u8)(v & 0xFF);
    p[1] = (ck_u8)((v >> 8) & 0xFF);
    p[2] = (ck_u8)((v >> 16) & 0xFF);
    p[3] = (ck_u8)((v >> 24) & 0xFF);
}

CK_INLINE void ck_set_error(ck_error *err, ck_u32 offset, const char *msg) {
    ck_usize len;
    if (!err) return;
    err->offset = offset;
    if (!msg) {
        err->message[0] = '\0';
        return;
    }
    len = strlen(msg);
    if (len >= sizeof(err->message)) {
        len = sizeof(err->message) - 1;
    }
    memcpy(err->message, msg, (unsigned int)len);
    err->message[len] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_C_COMPAT_H */
