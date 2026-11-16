/* Clean-room 16-bit MS-DOS CRT string and ctype functions (WP-5) */

unsigned char _ctype_[257] = { 0 };
unsigned char *_ctype = _ctype_;

int isalpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isxdigit(int c) {
    return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}

unsigned int strlen(const char *s) {
    unsigned int n = 0;
    if (s == (const char *)0) {
        return 0;
    }
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

char *strncpy(char *dst, const char *src, unsigned int n) {
    unsigned int i = 0;
    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    while (i < n) {
        dst[i] = '\0';
        ++i;
    }
    return dst;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    if (src == (const char *)0) {
        return dst;
    }
    while (*d != '\0') {
        ++d;
    }
    while ((*d++ = *src++) != '\0') {
    }
    return dst;
}

char *strncat(char *dst, const char *src, unsigned int n) {
    char *d = dst;
    unsigned int i = 0;
    while (*d != '\0') {
        ++d;
    }
    while (i < n && src[i] != '\0') {
        *d++ = src[i++];
    }
    *d = '\0';
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned int n) {
    unsigned int i = 0;
    while (i < n) {
        if (a[i] != b[i] || a[i] == '\0') {
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        }
        ++i;
    }
    return 0;
}

int strcmpi(const char *a, const char *b) {
    int ca;
    int cb;
    for (;;) {
        ca = tolower((unsigned char)*a);
        cb = tolower((unsigned char)*b);
        if (ca != cb || ca == 0) {
            return ca - cb;
        }
        ++a;
        ++b;
    }
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s != '\0') {
        if (*s == ch) {
            return (char *)s;
        }
        ++s;
    }
    if (ch == '\0') {
        return (char *)s;
    }
    return (char *)0;
}

char *strpbrk(const char *s, const char *accept) {
    const char *a;
    while (*s != '\0') {
        for (a = accept; *a != '\0'; ++a) {
            if (*s == *a) {
                return (char *)s;
            }
        }
        ++s;
    }
    return (char *)0;
}

char *strupr(char *s) {
    char *p = s;
    while (*p != '\0') {
        *p = (char)toupper((unsigned char)*p);
        ++p;
    }
    return s;
}

char *strrchr(const char *s, int c) {
    const char *last = (const char *)0;
    char ch = (char)c;
    while (*s != '\0') {
        if (*s == ch) {
            last = s;
        }
        ++s;
    }
    if (ch == '\0') {
        return (char *)s;
    }
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    unsigned int nlen;
    if (*needle == '\0') {
        return (char *)haystack;
    }
    nlen = strlen(needle);
    while (*haystack != '\0') {
        if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0) {
            return (char *)haystack;
        }
        ++haystack;
    }
    return (char *)0;
}

long atol(const char *s) {
    long val = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        ++s;
    }
    if (*s == '-') {
        neg = 1;
        ++s;
    } else if (*s == '+') {
        ++s;
    }
    while (isdigit((unsigned char)*s)) {
        val = val * 10L + (long)(*s - '0');
        ++s;
    }
    return neg ? -val : val;
}

int atoi(const char *s) {
    return (int)atol(s);
}
