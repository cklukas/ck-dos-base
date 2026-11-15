/* Clean-room INC/STRING.OBJ replacement (WP-5) */

char haveinttab = 0;

int toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}

char *strupr(char *s) {
    char *p = s;
    while (*p != '\0') {
        *p = (char)toupper((unsigned char)*p);
        ++p;
    }
    return s;
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
