#ifndef _CKCL_STRING_H
#define _CKCL_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int stricmp(const char *a, const char *b);
int strnicmp(const char *a, const char *b, size_t n);
int _stricmp(const char *a, const char *b);
int _strnicmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *s, const char *delim);
size_t strlen(const char *s);
char *strerror(int errnum);
char *strdup(const char *s);

void __far *_fmemcpy(void __far *dst, const void __far *src, size_t n);
void __far *_fmemmove(void __far *dst, const void __far *src, size_t n);
void __far *_fmemset(void __far *dst, int c, size_t n);
void __far *_fmemchr(const void __far *s, int c, size_t n);
int _fmemcmp(const void __far *a, const void __far *b, size_t n);
size_t _fstrlen(const char __far *s);
char __far *_fstrcpy(char __far *dst, const char __far *src);
int _fstrcmp(const char __far *a, const char __far *b);

#endif
