#ifndef _CKCL_MALLOC_H
#define _CKCL_MALLOC_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

void __far *_fmalloc(size_t size);
void _ffree(void __far *ptr);

size_t stackavail(void);

#endif
