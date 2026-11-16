/* Clean-room 16-bit MS-DOS CRT memory allocation and manipulation (WP-5) */

extern unsigned int _heap_ptr;
extern unsigned int _heap_limit;

static unsigned char crt_heap[1024];
static unsigned int crt_heap_top = 0;

void *malloc(unsigned int size) {
    unsigned int aligned;
    void *ptr;
    if (size == 0) {
        size = 1;
    }
    aligned = (size + 1) & ~1u;
    if (_heap_ptr != 0 && _heap_ptr + aligned <= _heap_limit &&
        _heap_ptr + aligned >= _heap_ptr) {
        ptr = (void *)_heap_ptr;
        _heap_ptr += aligned;
        return ptr;
    }
    if (crt_heap_top + aligned > sizeof(crt_heap)) {
        return (void *)0;
    }
    ptr = (void *)&crt_heap[crt_heap_top];
    crt_heap_top += aligned;
    return ptr;
}

void free(void *ptr) {
    (void)ptr;
}

void *memset(void *dst, int val, unsigned int count) {
    unsigned char *p = (unsigned char *)dst;
    unsigned int i;
    for (i = 0; i < count; ++i) {
        p[i] = (unsigned char)val;
    }
    return dst;
}

void *calloc(unsigned int count, unsigned int size) {
    unsigned int total = count * size;
    void *p = malloc(total);
    if (p != (void *)0) {
        memset(p, 0, total);
    }
    return p;
}

void *memcpy(void *dst, const void *src, unsigned int count) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    unsigned int i;
    for (i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, unsigned int count) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    unsigned int i;
    if (d < s) {
        for (i = 0; i < count; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (i = count; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

void *realloc(void *ptr, unsigned int new_size) {
    void *np;
    if (ptr == (void *)0) {
        return malloc(new_size);
    }
    if (new_size == 0) {
        return (void *)0;
    }
    np = malloc(new_size);
    if (np != (void *)0) {
        memcpy(np, ptr, new_size);
    }
    return np;
}

int memcmp(const void *a, const void *b, unsigned int count) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    unsigned int i;
    for (i = 0; i < count; ++i) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}
