/* Clean-room 16-bit MS-DOS CRT coalescing heap allocator & mem ops (WP-5 / WP-15) */

extern unsigned int _heap_ptr;
extern unsigned int _heap_limit;

typedef struct FreeBlk {
    unsigned int size_flags; /* total block bytes incl. 2-byte header; bit 0 = used */
    struct FreeBlk *next;    /* valid when free (stored in first 2 payload bytes) */
} FreeBlk;

static unsigned char crt_heap[1024];
static FreeBlk *free_head = (FreeBlk *)0;
static int heap_inited = 0;

void free(void *ptr);

static void add_arena(unsigned int start, unsigned int end) {
    unsigned int aligned_start = (start + 1u) & ~1u;
    unsigned int aligned_end = end & ~1u;
    FreeBlk *blk;
    if (aligned_end <= aligned_start || aligned_end - aligned_start < 8u) {
        return;
    }
    blk = (FreeBlk *)aligned_start;
    blk->size_flags = (aligned_end - aligned_start) | 1u;
    free((void *)((unsigned char *)blk + 2));
}

static void heap_init(void) {
    heap_inited = 1;
    add_arena((unsigned int)&crt_heap[0], (unsigned int)&crt_heap[0] + sizeof(crt_heap));
    if (_heap_ptr != 0u && _heap_limit > _heap_ptr) {
        unsigned int hp = _heap_ptr;
        unsigned int hl = _heap_limit;
        _heap_ptr = _heap_limit;
        add_arena(hp, hl);
    }
}

void *malloc(unsigned int size) {
    unsigned int aligned;
    unsigned int need;
    FreeBlk *prev;
    FreeBlk *cur;
    if (!heap_inited) {
        heap_init();
    }
    aligned = (size <= 2u) ? 2u : ((size + 1u) & ~1u);
    if (aligned > 0xFFF8u) {
        return (void *)0;
    }
    need = aligned + 2u;

    prev = (FreeBlk *)0;
    cur = free_head;
    while (cur != (FreeBlk *)0) {
        unsigned int bsize = cur->size_flags & ~1u;
        if (bsize >= need) {
            if (bsize - need >= 4u) {
                FreeBlk *rem = (FreeBlk *)((unsigned char *)cur + need);
                rem->size_flags = bsize - need;
                rem->next = cur->next;
                if (prev != (FreeBlk *)0) {
                    prev->next = rem;
                } else {
                    free_head = rem;
                }
                cur->size_flags = need | 1u;
            } else {
                if (prev != (FreeBlk *)0) {
                    prev->next = cur->next;
                } else {
                    free_head = cur->next;
                }
                cur->size_flags = bsize | 1u;
            }
            return (void *)((unsigned char *)cur + 2);
        }
        prev = cur;
        cur = cur->next;
    }
    return (void *)0;
}

void free(void *ptr) {
    FreeBlk *blk;
    FreeBlk *prev;
    FreeBlk *cur;
    unsigned int bsize;
    if (ptr == (void *)0) {
        return;
    }
    blk = (FreeBlk *)((unsigned char *)ptr - 2);
    if ((blk->size_flags & 1u) == 0u) {
        return;
    }
    bsize = blk->size_flags & ~1u;
    blk->size_flags = bsize;

    prev = (FreeBlk *)0;
    cur = free_head;
    while (cur != (FreeBlk *)0 && (unsigned int)cur < (unsigned int)blk) {
        prev = cur;
        cur = cur->next;
    }

    if (cur != (FreeBlk *)0 &&
        (unsigned int)((unsigned char *)blk + bsize) == (unsigned int)cur) {
        bsize += (cur->size_flags & ~1u);
        blk->size_flags = bsize;
        blk->next = cur->next;
    } else {
        blk->next = cur;
    }

    if (prev != (FreeBlk *)0 &&
        (unsigned int)((unsigned char *)prev + (prev->size_flags & ~1u)) == (unsigned int)blk) {
        prev->size_flags = (prev->size_flags & ~1u) + bsize;
        prev->next = blk->next;
    } else if (prev != (FreeBlk *)0) {
        prev->next = blk;
    } else {
        free_head = blk;
    }
}

void *memset(void *dst, int val, unsigned int count) {
    unsigned char *p = (unsigned char *)dst;
    unsigned char b = (unsigned char)val;
    if (count >= 4u) {
        unsigned int w = (unsigned int)b | ((unsigned int)b << 8);
        unsigned int words;
        if ((unsigned int)p & 1u) {
            *p++ = b;
            --count;
        }
        words = count >> 1;
        while (words--) {
            *(unsigned int *)p = w;
            p += 2;
        }
        if (count & 1u) {
            *p = b;
        }
        return dst;
    }
    while (count--) {
        *p++ = b;
    }
    return dst;
}

void *calloc(unsigned int count, unsigned int size) {
    unsigned int total;
    void *p;
    if (count != 0u && size > (unsigned int)(0xFFFFu / count)) {
        return (void *)0;
    }
    total = count * size;
    p = malloc(total);
    if (p != (void *)0) {
        memset(p, 0, total);
    }
    return p;
}

void *memcpy(void *dst, const void *src, unsigned int count) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    unsigned int words = count >> 1;
    while (words--) {
        *(unsigned int *)d = *(const unsigned int *)s;
        d += 2;
        s += 2;
    }
    if (count & 1u) {
        *d = *s;
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
    FreeBlk *blk;
    unsigned int old_bsize;
    unsigned int old_payload;
    unsigned int aligned;
    unsigned int need;
    void *np;
    if (ptr == (void *)0) {
        return malloc(new_size);
    }
    if (new_size == 0u) {
        free(ptr);
        return (void *)0;
    }
    aligned = (new_size <= 2u) ? 2u : ((new_size + 1u) & ~1u);
    if (aligned > 0xFFF8u) {
        return (void *)0;
    }
    need = aligned + 2u;

    blk = (FreeBlk *)((unsigned char *)ptr - 2);
    old_bsize = blk->size_flags & ~1u;
    old_payload = old_bsize - 2u;

    /* 1. In-place shrink */
    if (need <= old_bsize) {
        if (old_bsize - need >= 4u) {
            FreeBlk *rem = (FreeBlk *)((unsigned char *)blk + need);
            rem->size_flags = (old_bsize - need) | 1u;
            blk->size_flags = need | 1u;
            free((void *)((unsigned char *)rem + 2));
        }
        return ptr;
    }

    /* 2. In-place forward expansion into adjacent free block */
    {
        unsigned int right_addr = (unsigned int)((unsigned char *)blk + old_bsize);
        FreeBlk *prev = (FreeBlk *)0;
        FreeBlk *cur = free_head;
        while (cur != (FreeBlk *)0 && (unsigned int)cur < right_addr) {
            prev = cur;
            cur = cur->next;
        }
        if (cur != (FreeBlk *)0 && (unsigned int)cur == right_addr) {
            unsigned int combined = old_bsize + (cur->size_flags & ~1u);
            if (combined >= need) {
                if (combined - need >= 4u) {
                    FreeBlk *rem = (FreeBlk *)((unsigned char *)blk + need);
                    rem->size_flags = combined - need;
                    rem->next = cur->next;
                    if (prev != (FreeBlk *)0) {
                        prev->next = rem;
                    } else {
                        free_head = rem;
                    }
                    blk->size_flags = need | 1u;
                } else {
                    if (prev != (FreeBlk *)0) {
                        prev->next = cur->next;
                    } else {
                        free_head = cur->next;
                    }
                    blk->size_flags = combined | 1u;
                }
                return ptr;
            }
        }
    }

    /* 3. Allocate, copy, and free old block */
    np = malloc(new_size);
    if (np != (void *)0) {
        memcpy(np, ptr, old_payload);
        free(ptr);
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
