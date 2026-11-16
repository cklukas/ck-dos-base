/*
 * Clean-room Medium-Model C89 & MS-DOS runtime functions (MLIBCE.LIB).
 */

#include <ctype.h>
#include <direct.h>
#include <dos.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <malloc.h>
#include <process.h>
#include <share.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int _doserrno = 0;

extern unsigned int _dos_raw_find(const char *path, unsigned int attr, void *dta, int first);
extern int _dos_raw_exec(const char *path, const void *tail128, unsigned int env_seg);
extern void __stdio_exit_flush(void);

static int map_dos_err(unsigned int derr)
{
    _doserrno = (int)derr;
    switch (derr) {
        case 1:  errno = EINVAL; break;
        case 2:
        case 3:
        case 15:
        case 18: errno = ENOENT; break;
        case 4:  errno = EMFILE; break;
        case 5:
        case 16:
        case 19:
        case 32:
        case 33: errno = EACCES; break;
        case 6:  errno = EBADF;  break;
        case 7:
        case 8:  errno = ENOMEM; break;
        case 17: errno = EXDEV;  break;
        case 80: errno = EEXIST; break;
        default: errno = EINVAL; break;
    }
    return -1;
}

/* --- <ctype.h> additions (complementing crt_str.c) --- */

int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
int isprint(int c) { return c >= 0x20 && c <= 0x7E; }
int isgraph(int c) { return c >= 0x21 && c <= 0x7E; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isascii(int c) { return (unsigned int)c <= 0x7FU; }
int toascii(int c) { return c & 0x7F; }

/* --- <string.h> additions (complementing crt_mem.c & crt_str.c) --- */

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char target = (unsigned char)c;
    size_t i;
    for (i = 0; i < n; ++i) {
        if (p[i] == target) return (void *)(p + i);
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    const char *a;
    while (s[n] != '\0') {
        int found = 0;
        for (a = accept; *a != '\0'; ++a) {
            if (s[n] == *a) { found = 1; break; }
        }
        if (!found) break;
        ++n;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    const char *r;
    while (s[n] != '\0') {
        for (r = reject; *r != '\0'; ++r) {
            if (s[n] == *r) return n;
        }
        ++n;
    }
    return n;
}

char *strtok(char *s, const char *delim)
{
    static char *save_ptr = NULL;
    char *tok;
    if (s == NULL) s = save_ptr;
    if (s == NULL) return NULL;
    s += strspn(s, delim);
    if (*s == '\0') {
        save_ptr = NULL;
        return NULL;
    }
    tok = s;
    s += strcspn(s, delim);
    if (*s != '\0') {
        *s = '\0';
        save_ptr = s + 1;
    } else {
        save_ptr = NULL;
    }
    return tok;
}

int stricmp(const char *a, const char *b)
{
    int ca, cb;
    for (;;) {
        ca = tolower((unsigned char)*a);
        cb = tolower((unsigned char)*b);
        if (ca != cb || ca == 0) return ca - cb;
        ++a;
        ++b;
    }
}

int _stricmp(const char *a, const char *b) { return stricmp(a, b); }

int strnicmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb || ca == 0) return ca - cb;
    }
    return 0;
}

int _strnicmp(const char *a, const char *b, size_t n) { return strnicmp(a, b, n); }

char *strdup(const char *s)
{
    size_t len;
    char *p;
    if (!s) return NULL;
    len = strlen(s) + 1U;
    p = (char *)malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

char *strerror(int errnum)
{
    switch (errnum) {
        case 0:      return "No error";
        case ENOENT: return "No such file or directory";
        case EACCES: return "Permission denied";
        case EBADF:  return "Bad file descriptor";
        case ENOMEM: return "Not enough memory";
        case EEXIST: return "File exists";
        case EINVAL: return "Invalid argument";
        case EMFILE: return "Too many open files";
        case ENOSPC: return "No space left on device";
        case EDOM:   return "Domain error";
        case ERANGE: return "Result too large";
        default:     return "Unknown error";
    }
}

/* --- <stdlib.h> integer, conversion, search/sort, and exit helpers --- */

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0L ? -x : x; }

div_t div(int numer, int denom)
{
    div_t r;
    r.quot = numer / denom;
    r.rem  = numer % denom;
    return r;
}

ldiv_t ldiv(long numer, long denom)
{
    ldiv_t r;
    r.quot = numer / denom;
    r.rem  = numer % denom;
    return r;
}

static unsigned long rand_seed = 1UL;

void srand(unsigned int seed) { rand_seed = (unsigned long)seed; }

int rand(void)
{
    rand_seed = rand_seed * 1103515245UL + 12345UL;
    return (int)((rand_seed >> 16) & 0x7FFFUL);
}

char *ultoa(unsigned long val, char *buf, int radix)
{
    char tmp[36];
    int n = 0, i;
    if (radix < 2 || radix > 36) {
        buf[0] = '\0';
        return buf;
    }
    if (val == 0UL) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    while (val != 0UL) {
        unsigned int d = (unsigned int)(val % (unsigned long)radix);
        tmp[n++] = (char)(d < 10U ? ('0' + d) : ('a' + (d - 10U)));
        val /= (unsigned long)radix;
    }
    for (i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return buf;
}

char *ltoa(long val, char *buf, int radix)
{
    if (radix == 10 && val < 0L) {
        buf[0] = '-';
        ultoa(0UL - (unsigned long)val, buf + 1, 10);
        return buf;
    }
    return ultoa((unsigned long)val, buf, radix);
}

char *itoa(int val, char *buf, int radix)
{
    if (radix == 10 && val < 0) {
        return ltoa((long)val, buf, 10);
    }
    return ultoa((unsigned long)(unsigned int)val, buf, radix);
}

static int digit_val(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    return -1;
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
    const char *p = s;
    unsigned long acc = 0UL, cutoff, cutlim;
    int neg = 0, any = 0, overflow = 0, d;

    while (isspace((unsigned char)*p)) ++p;
    if (*p == '-') { neg = 1; ++p; }
    else if (*p == '+') { ++p; }

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        if (digit_val((unsigned char)p[2]) >= 0 && digit_val((unsigned char)p[2]) < 16) {
            p += 2;
            base = 16;
        } else if (base == 0) {
            base = 8;
        }
    } else if (base == 0) {
        base = (*p == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char *)s;
        return 0UL;
    }

    cutoff = 0xFFFFFFFFUL / (unsigned long)base;
    cutlim = 0xFFFFFFFFUL % (unsigned long)base;

    for (; *p != '\0'; ++p) {
        d = digit_val((unsigned char)*p);
        if (d < 0 || d >= base) break;
        any = 1;
        if (overflow || acc > cutoff || (acc == cutoff && (unsigned long)d > cutlim)) {
            overflow = 1;
        } else {
            acc = acc * (unsigned long)base + (unsigned long)d;
        }
    }

    if (!any) {
        if (endptr) *endptr = (char *)s;
        return 0UL;
    }
    if (endptr) *endptr = (char *)p;
    if (overflow) {
        errno = ERANGE;
        return 0xFFFFFFFFUL;
    }
    return neg ? (0UL - acc) : acc;
}

long strtol(const char *s, char **endptr, int base)
{
    const char *p = s;
    unsigned long acc = 0UL, lim, cutoff, cutlim;
    int neg = 0, any = 0, overflow = 0, d;

    while (isspace((unsigned char)*p)) ++p;
    if (*p == '-') { neg = 1; ++p; }
    else if (*p == '+') { ++p; }

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        if (digit_val((unsigned char)p[2]) >= 0 && digit_val((unsigned char)p[2]) < 16) {
            p += 2;
            base = 16;
        } else if (base == 0) {
            base = 8;
        }
    } else if (base == 0) {
        base = (*p == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char *)s;
        return 0L;
    }

    lim = neg ? 0x80000000UL : 0x7FFFFFFFUL;
    cutoff = lim / (unsigned long)base;
    cutlim = lim % (unsigned long)base;

    for (; *p != '\0'; ++p) {
        d = digit_val((unsigned char)*p);
        if (d < 0 || d >= base) break;
        any = 1;
        if (overflow || acc > cutoff || (acc == cutoff && (unsigned long)d > cutlim)) {
            overflow = 1;
        } else {
            acc = acc * (unsigned long)base + (unsigned long)d;
        }
    }

    if (!any) {
        if (endptr) *endptr = (char *)s;
        return 0L;
    }
    if (endptr) *endptr = (char *)p;
    if (overflow) {
        errno = ERANGE;
        return neg ? (-2147483647L - 1L) : 2147483647L;
    }
    return neg ? (long)(0UL - acc) : (long)acc;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
    size_t lo = 0, hi = nmemb;
    const unsigned char *b = (const unsigned char *)base;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        const void *elem = (const void *)(b + mid * size);
        int c = compar(key, elem);
        if (c == 0) return (void *)elem;
        if (c < 0) hi = mid;
        else lo = mid + 1U;
    }
    return NULL;
}

static void swap_bytes(unsigned char *a, unsigned char *b, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i) {
        unsigned char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    unsigned char *b = (unsigned char *)base;
    size_t gap, i, j;
    if (nmemb < 2U || size == 0U) return;
    /* Iterative Knuth-gap Shell sort: O(N^1.25), zero recursion stack risk on DOS! */
    gap = 1U;
    while (gap < nmemb / 3U) gap = gap * 3U + 1U;
    while (gap > 0U) {
        for (i = gap; i < nmemb; ++i) {
            j = i;
            while (j >= gap &&
                   compar((const void *)(b + (j - gap) * size),
                          (const void *)(b + j * size)) > 0) {
                swap_bytes(b + (j - gap) * size, b + j * size, size);
                j -= gap;
            }
        }
        gap /= 3U;
    }
}

typedef void (*atexit_fn)(void);
static atexit_fn atexit_list[16];
static int atexit_count = 0;

int atexit(void (*func)(void))
{
    if (!func || atexit_count >= 16) return -1;
    atexit_list[atexit_count++] = func;
    return 0;
}

void _exit(int status)
{
    union REGS r;
    r.h.ah = 0x4C;
    r.h.al = (unsigned char)status;
    intdos(&r, &r);
    for (;;) {}
}

void exit(int status)
{
    while (atexit_count > 0) {
        atexit_fn fn = atexit_list[--atexit_count];
        if (fn) fn();
    }
    __stdio_exit_flush();
    _exit(status);
}

void abort(void)
{
    _exit(3);
}

int getpid(void)
{
    return (int)_psp;
}

/* --- DOS <dos.h>, <io.h>, <direct.h>, <sys/stat.h> --- */

unsigned int _dos_open(const char *path, unsigned int mode, int *handle)
{
    union REGS r;
    r.h.ah = 0x3D;
    r.h.al = (unsigned char)mode;
    r.x.dx = (unsigned int)path;
    intdos(&r, &r);
    if (r.x.cflag) {
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    *handle = (int)r.x.ax;
    return 0U;
}

unsigned int _dos_creat(const char *path, unsigned int attr, int *handle)
{
    union REGS r;
    r.h.ah = 0x3C;
    r.x.cx = attr;
    r.x.dx = (unsigned int)path;
    intdos(&r, &r);
    if (r.x.cflag) {
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    *handle = (int)r.x.ax;
    return 0U;
}

unsigned int _dos_creatnew(const char *path, unsigned int attr, int *handle)
{
    union REGS r;
    r.h.ah = 0x5B;
    r.x.cx = attr;
    r.x.dx = (unsigned int)path;
    intdos(&r, &r);
    if (r.x.cflag) {
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    *handle = (int)r.x.ax;
    return 0U;
}

unsigned int _dos_close(int handle)
{
    union REGS r;
    r.h.ah = 0x3E;
    r.x.bx = (unsigned int)handle;
    intdos(&r, &r);
    if (r.x.cflag) {
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    return 0U;
}

unsigned int _dos_read(int handle, void __far *buf, unsigned int count, unsigned int *bytes_read)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.ds = FP_SEG(buf);
    r.h.ah = 0x3F;
    r.x.bx = (unsigned int)handle;
    r.x.cx = count;
    r.x.dx = FP_OFF(buf);
    intdosx(&r, &r, &s);
    if (r.x.cflag) {
        if (bytes_read) *bytes_read = 0U;
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    if (bytes_read) *bytes_read = r.x.ax;
    return 0U;
}

unsigned int _dos_write(int handle, const void __far *buf, unsigned int count, unsigned int *bytes_written)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.ds = FP_SEG(buf);
    r.h.ah = 0x40;
    r.x.bx = (unsigned int)handle;
    r.x.cx = count;
    r.x.dx = FP_OFF(buf);
    intdosx(&r, &r, &s);
    if (r.x.cflag) {
        if (bytes_written) *bytes_written = 0U;
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    if (bytes_written) *bytes_written = r.x.ax;
    return 0U;
}

unsigned int _dos_allocmem(unsigned int size_paras, unsigned int *segment)
{
    union REGS r;
    r.h.ah = 0x48;
    r.x.bx = size_paras;
    intdos(&r, &r);
    if (r.x.cflag) {
        if (segment) *segment = r.x.bx;
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    if (segment) *segment = r.x.ax;
    return 0U;
}

unsigned int _dos_freemem(unsigned int segment)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.es = segment;
    r.h.ah = 0x49;
    intdosx(&r, &r, &s);
    if (r.x.cflag) {
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    return 0U;
}

unsigned int _dos_setblock(unsigned int size_paras, unsigned int segment, unsigned int *max_paras)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.es = segment;
    r.h.ah = 0x4A;
    r.x.bx = size_paras;
    intdosx(&r, &r, &s);
    if (r.x.cflag) {
        if (max_paras) *max_paras = r.x.bx;
        map_dos_err(r.x.ax);
        return r.x.ax;
    }
    return 0U;
}

unsigned int _dos_findfirst(const char *path, unsigned int attr, struct find_t *fileinfo)
{
    unsigned int rc = _dos_raw_find(path, attr, fileinfo, 1);
    if (rc != 0U) map_dos_err(rc);
    return rc;
}

unsigned int _dos_findnext(struct find_t *fileinfo)
{
    unsigned int rc = _dos_raw_find((const char *)0, 0U, fileinfo, 0);
    if (rc != 0U) map_dos_err(rc);
    return rc;
}

void _dos_getdrive(unsigned int *drive)
{
    union REGS r;
    r.h.ah = 0x19;
    intdos(&r, &r);
    if (drive) *drive = (unsigned int)r.h.al + 1U;
}

void _dos_setdrive(unsigned int drive, unsigned int *total)
{
    union REGS r;
    r.h.ah = 0x0E;
    r.h.dl = (unsigned char)(drive > 0U ? (drive - 1U) : 0U);
    intdos(&r, &r);
    if (total) *total = (unsigned int)r.h.al;
}

int sopen(const char *path, int oflag, int shflag, ...)
{
    int handle = -1;
    unsigned int dos_mode = (unsigned int)(oflag & 3) | (unsigned int)(shflag & 0x70);
    unsigned int create_attr = 0U;

    if (oflag & O_CREAT) {
        va_list ap;
        int pmode;
        va_start(ap, shflag);
        pmode = va_arg(ap, int);
        va_end(ap);
        if ((pmode & S_IWRITE) == 0) create_attr = _A_RDONLY;
    }

    if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
        if (_dos_creatnew(path, create_attr, &handle) != 0U) return -1;
        if (shflag != 0 || (oflag & 3) == O_RDONLY) {
            _dos_close(handle);
            if (_dos_open(path, dos_mode, &handle) != 0U) return -1;
        }
    } else if ((oflag & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC)) {
        if (shflag != 0) {
            if (_dos_open(path, (unsigned int)O_WRONLY | (unsigned int)(shflag & 0x70), &handle) == 0U) {
                unsigned int wr = 0U;
                _dos_write(handle, (const void __far *)"", 0U, &wr);
                if ((oflag & 3) != O_WRONLY) {
                    _dos_close(handle);
                    if (_dos_open(path, dos_mode, &handle) != 0U) return -1;
                }
            } else if (_doserrno == 2) {
                if (_dos_creat(path, create_attr, &handle) != 0U) return -1;
                _dos_close(handle);
                if (_dos_open(path, dos_mode, &handle) != 0U) return -1;
            } else {
                return -1;
            }
        } else {
            if (_dos_creat(path, create_attr, &handle) != 0U) return -1;
            if ((oflag & 3) == O_RDONLY) {
                _dos_close(handle);
                if (_dos_open(path, dos_mode, &handle) != 0U) return -1;
            }
        }
    } else if (oflag & O_CREAT) {
        if (_dos_open(path, dos_mode, &handle) != 0U) {
            if (_doserrno != 2) return -1;
            if (_dos_creatnew(path, create_attr, &handle) != 0U) return -1;
            if (shflag != 0 || (oflag & 3) == O_RDONLY) {
                _dos_close(handle);
                if (_dos_open(path, dos_mode, &handle) != 0U) return -1;
            }
        }
    } else {
        if (_dos_open(path, dos_mode, &handle) != 0U) return -1;
        if (oflag & O_TRUNC) {
            unsigned int wr = 0U;
            _dos_write(handle, (const void __far *)"", 0U, &wr);
        }
    }

    if (oflag & O_APPEND) {
        lseek(handle, 0L, SEEK_END);
    }
    return handle;
}

int open(const char *path, int oflag, ...)
{
    int pmode = S_IREAD | S_IWRITE;
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    return sopen(path, oflag, 0, pmode);
}

int creat(const char *path, int mode)
{
    return sopen(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0, mode);
}

int close(int fd)
{
    return (_dos_close(fd) == 0U) ? 0 : -1;
}

int read(int fd, void *buf, unsigned int count)
{
    unsigned int got = 0U;
    if (_dos_read(fd, buf, count, &got) != 0U) return -1;
    return (int)got;
}

int write(int fd, const void *buf, unsigned int count)
{
    unsigned int put = 0U;
    if (_dos_write(fd, buf, count, &put) != 0U) return -1;
    return (int)put;
}

long lseek(int fd, long offset, int origin)
{
    union REGS r;
    r.h.ah = 0x42;
    r.h.al = (unsigned char)origin;
    r.x.bx = (unsigned int)fd;
    r.x.cx = (unsigned int)((unsigned long)offset >> 16);
    r.x.dx = (unsigned int)((unsigned long)offset & 0xFFFFUL);
    intdos(&r, &r);
    if (r.x.cflag) {
        map_dos_err(r.x.ax);
        return -1L;
    }
    return (long)(((unsigned long)r.x.dx << 16) | (unsigned long)r.x.ax);
}

long filelength(int fd)
{
    long cur = lseek(fd, 0L, SEEK_CUR);
    long end;
    if (cur < 0L) return -1L;
    end = lseek(fd, 0L, SEEK_END);
    lseek(fd, cur, SEEK_SET);
    return end;
}

int isatty(int fd)
{
    union REGS r;
    r.x.ax = 0x4400;
    r.x.bx = (unsigned int)fd;
    intdos(&r, &r);
    if (r.x.cflag) return 0;
    return (r.x.dx & 0x80U) ? 1 : 0;
}

int access(const char *path, int mode)
{
    union REGS r;
    r.x.ax = 0x4300;
    r.x.dx = (unsigned int)path;
    intdos(&r, &r);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    if ((mode & 2) && (r.x.cx & _A_RDONLY)) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

int unlink(const char *path)
{
    union REGS r;
    r.h.ah = 0x41;
    r.x.dx = (unsigned int)path;
    intdos(&r, &r);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    return 0;
}

int remove(const char *path) { return unlink(path); }

int rename(const char *oldpath, const char *newpath)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.es = s.ds;
    r.h.ah = 0x56;
    r.x.dx = (unsigned int)oldpath;
    r.x.di = (unsigned int)newpath;
    intdosx(&r, &r, &s);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    return 0;
}

int lock(int fd, unsigned long offset, unsigned long length)
{
    union REGS r;
    r.x.ax = 0x5C00;
    r.x.bx = (unsigned int)fd;
    r.x.cx = (unsigned int)(offset >> 16);
    r.x.dx = (unsigned int)(offset & 0xFFFFUL);
    r.x.si = (unsigned int)(length >> 16);
    r.x.di = (unsigned int)(length & 0xFFFFUL);
    intdos(&r, &r);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    return 0;
}

int unlock(int fd, unsigned long offset, unsigned long length)
{
    union REGS r;
    r.x.ax = 0x5C01;
    r.x.bx = (unsigned int)fd;
    r.x.cx = (unsigned int)(offset >> 16);
    r.x.dx = (unsigned int)(offset & 0xFFFFUL);
    r.x.si = (unsigned int)(length >> 16);
    r.x.di = (unsigned int)(length & 0xFFFFUL);
    intdos(&r, &r);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    return 0;
}

int chdir(const char *path)
{
    union REGS r;
    r.h.ah = 0x3B;
    r.x.dx = (unsigned int)path;
    intdos(&r, &r);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    return 0;
}

int mkdir(const char *path)
{
    union REGS r;
    r.h.ah = 0x39;
    r.x.dx = (unsigned int)path;
    intdos(&r, &r);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    return 0;
}

int rmdir(const char *path)
{
    union REGS r;
    r.h.ah = 0x3A;
    r.x.dx = (unsigned int)path;
    intdos(&r, &r);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    return 0;
}

static int query_drive_cwd(unsigned int drv1, char *dir64)
{
    union REGS r;
    dir64[0] = '\0';
    r.h.ah = 0x47;
    r.h.dl = (unsigned char)drv1;
    r.x.si = (unsigned int)dir64;
    intdos(&r, &r);
    if (r.x.cflag) return map_dos_err(r.x.ax);
    return 0;
}

char *getcwd(char *buf, size_t size)
{
    unsigned int drv = 0U;
    char dir[68];
    char full[80];
    size_t len;

    _dos_getdrive(&drv);
    if (query_drive_cwd(0U, dir) != 0) return NULL;

    full[0] = (char)('A' + (drv - 1U));
    full[1] = ':';
    full[2] = '\\';
    strcpy(full + 3, dir);
    len = strlen(full) + 1U;

    if (buf == NULL) {
        size_t alloc_sz = size > len ? size : len;
        buf = (char *)malloc(alloc_sz);
        if (!buf) { errno = ENOMEM; return NULL; }
    } else if (size < len) {
        errno = ERANGE;
        return NULL;
    }
    memcpy(buf, full, len);
    return buf;
}

char *_fullpath(char *buffer, const char *path, size_t maxlen)
{
    unsigned int drv = 0U;
    const char *p = path;
    char work[260];
    char out[260];
    size_t wlen = 0U, olen, i;

    if (!path || !*path) {
        return getcwd(buffer, buffer ? maxlen : _MAX_PATH);
    }

    if (isalpha((unsigned char)p[0]) && p[1] == ':') {
        drv = (unsigned int)(toupper((unsigned char)p[0]) - 'A' + 1);
        p += 2;
    } else {
        _dos_getdrive(&drv);
    }

    work[0] = (char)('A' + (drv - 1U));
    work[1] = ':';
    work[2] = '\\';
    wlen = 3U;

    if (*p == '\\' || *p == '/') {
        while (*p == '\\' || *p == '/') ++p;
    } else {
        char dir[68];
        if (query_drive_cwd(drv, dir) != 0) return NULL;
        if (dir[0] != '\0') {
            size_t dlen = strlen(dir);
            memcpy(work + wlen, dir, dlen);
            wlen += dlen;
            work[wlen++] = '\\';
        }
    }

    while (*p != '\0' && wlen + 2U < sizeof(work)) {
        char ch = (*p == '/') ? '\\' : *p;
        work[wlen++] = ch;
        ++p;
    }
    work[wlen] = '\0';

    /* Normalize '.' and '..' components */
    out[0] = work[0];
    out[1] = ':';
    out[2] = '\\';
    olen = 3U;
    i = 3U;
    while (work[i] != '\0') {
        size_t start;
        size_t clen;
        while (work[i] == '\\') ++i;
        if (work[i] == '\0') break;
        start = i;
        while (work[i] != '\0' && work[i] != '\\') ++i;
        clen = i - start;
        if (clen == 1U && work[start] == '.') {
            continue;
        }
        if (clen == 2U && work[start] == '.' && work[start + 1U] == '.') {
            if (olen > 3U) {
                --olen;
                while (olen > 3U && out[olen - 1U] != '\\') --olen;
            }
            continue;
        }
        if (olen > 3U && out[olen - 1U] != '\\') {
            if (olen + 1U >= sizeof(out)) return NULL;
            out[olen++] = '\\';
        }
        if (olen + clen >= sizeof(out)) return NULL;
        memcpy(out + olen, work + start, clen);
        olen += clen;
    }
    out[olen] = '\0';

    if (buffer == NULL) {
        size_t alloc_sz = maxlen > (olen + 1U) ? maxlen : (olen + 1U);
        buffer = (char *)malloc(alloc_sz);
        if (!buffer) { errno = ENOMEM; return NULL; }
    } else if (maxlen <= olen) {
        errno = ERANGE;
        return NULL;
    }
    memcpy(buffer, out, olen + 1U);
    return buffer;
}

int stat(const char *path, struct stat *buf)
{
    char full[_MAX_PATH + 4];
    struct find_t ft;
    size_t len;

    if (!path || !*path || !buf) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(path, '*') || strchr(path, '?')) {
        errno = ENOENT;
        return -1;
    }
    memset(buf, 0, sizeof(*buf));

    if (!_fullpath(full, path, sizeof(full))) {
        return -1;
    }
    len = strlen(full);

    /* Root directory e.g. "C:\" */
    if (len == 3U && full[1] == ':' && full[2] == '\\') {
        char dir[68];
        unsigned int drv = (unsigned int)(toupper((unsigned char)full[0]) - 'A' + 1);
        if (query_drive_cwd(drv, dir) != 0) return -1;
        buf->st_dev = (dev_t)(drv - 1U);
        buf->st_rdev = buf->st_dev;
        buf->st_mode = S_IFDIR | S_IREAD | S_IWRITE | S_IEXEC;
        buf->st_nlink = 1;
        return 0;
    }

    if (len > 3U && full[len - 1U] == '\\') {
        full[len - 1U] = '\0';
    }

    if (_dos_findfirst(full, _A_RDONLY | _A_HIDDEN | _A_SYSTEM | _A_SUBDIR | _A_ARCH, &ft) != 0U) {
        errno = ENOENT;
        return -1;
    }

    buf->st_dev = (dev_t)(toupper((unsigned char)full[0]) - 'A');
    buf->st_rdev = buf->st_dev;
    buf->st_nlink = 1;
    buf->st_size = (off_t)ft.size;
    if (ft.attrib & _A_SUBDIR) {
        buf->st_mode = S_IFDIR | S_IREAD | S_IWRITE | S_IEXEC;
    } else {
        buf->st_mode = S_IFREG | S_IREAD | ((ft.attrib & _A_RDONLY) ? 0U : S_IWRITE);
    }
    return 0;
}

int fstat(int fd, struct stat *buf)
{
    if (!buf || fd < 0) {
        errno = EBADF;
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    buf->st_nlink = 1;
    if (isatty(fd)) {
        buf->st_mode = S_IFCHR | S_IREAD | S_IWRITE;
    } else {
        long sz = filelength(fd);
        if (sz < 0L) return -1;
        buf->st_mode = S_IFREG | S_IREAD | S_IWRITE;
        buf->st_size = (off_t)sz;
    }
    return 0;
}

/* --- Environment & Process Spawning --- */

char *getenv(const char *name)
{
    static char env_val[260];
    unsigned int env_seg;
    const unsigned char __far *env;
    size_t nlen;
    unsigned int p = 0U;

    if (!name || !*name || !_psp) return NULL;
    env_seg = *(const unsigned int __far *)MK_FP(_psp, 0x2CU);
    if (!env_seg) return NULL;
    env = (const unsigned char __far *)MK_FP(env_seg, 0U);
    nlen = strlen(name);

    while (env[p] != 0U) {
        size_t i = 0U;
        while (i < nlen && env[p + i] != 0U &&
               tolower(env[p + i]) == tolower((unsigned char)name[i])) {
            ++i;
        }
        if (i == nlen && env[p + i] == '=') {
            unsigned int vstart = p + (unsigned int)nlen + 1U;
            size_t j = 0U;
            while (env[vstart + j] != 0U && j + 1U < sizeof(env_val)) {
                env_val[j] = (char)env[vstart + j];
                ++j;
            }
            env_val[j] = '\0';
            return env_val;
        }
        while (env[p] != 0U) ++p;
        ++p;
    }
    return NULL;
}

int putenv(const char *entry)
{
    (void)entry;
    return -1;
}

static int build_dos_tail(unsigned char *tail128, const char *const *argv)
{
    unsigned int pos = 0U;
    int i;
    if (argv && argv[0]) {
        for (i = 1; argv[i] != NULL; ++i) {
            const char *a = argv[i];
            size_t alen = strlen(a);
            if (pos + 1U + alen > 126U) {
                errno = E2BIG;
                return -1;
            }
            tail128[1U + pos++] = ' ';
            memcpy(tail128 + 1U + pos, a, alen);
            pos += (unsigned int)alen;
        }
    }
    tail128[0] = (unsigned char)pos;
    tail128[1U + pos] = 0x0DU;
    return 0;
}

int spawnve(int mode, const char *path, const char *const *argv, const char *const *envp)
{
    unsigned char tail128[130];
    unsigned int env_seg = 0U;
    int rc;

    if (mode != P_WAIT && mode != P_OVERLAY) {
        errno = EINVAL;
        return -1;
    }
    if (!path || !*path) {
        errno = ENOENT;
        return -1;
    }
    if (build_dos_tail(tail128, argv) != 0) return -1;

    if (envp != NULL) {
        unsigned long total = 2UL;
        int i;
        unsigned char __far *dst;
        unsigned int off = 0U;
        for (i = 0; envp[i] != NULL; ++i) {
            total += (unsigned long)strlen(envp[i]) + 1UL;
        }
        if (total > 32768UL) {
            errno = E2BIG;
            return -1;
        }
        if (_dos_allocmem((unsigned int)((total + 15UL) >> 4), &env_seg) != 0U) {
            errno = ENOMEM;
            return -1;
        }
        dst = (unsigned char __far *)MK_FP(env_seg, 0U);
        for (i = 0; envp[i] != NULL; ++i) {
            size_t elen = strlen(envp[i]) + 1U;
            _fmemcpy(dst + off, envp[i], elen);
            off += (unsigned int)elen;
        }
        if (off == 0U) dst[off++] = 0U;
        dst[off] = 0U;
    }

    fflush(NULL);
    rc = _dos_raw_exec(path, tail128, env_seg);
    if (env_seg != 0U) {
        _dos_freemem(env_seg);
    }

    if (rc < 0) {
        return map_dos_err((unsigned int)(-rc));
    }
    if (mode == P_OVERLAY) {
        exit(rc);
    }
    return rc;
}

int spawnv(int mode, const char *path, const char *const *argv)
{
    return spawnve(mode, path, argv, NULL);
}

static int try_exec_exts(int mode, const char *base, int has_ext,
                         const char *const *argv, const char *const *envp)
{
    static const char *const exts[3] = { ".COM", ".EXE", ".BAT" };
    char candidate[_MAX_PATH + 8];
    int i, rc;

    if (has_ext) {
        return spawnve(mode, base, argv, envp);
    }
    for (i = 0; i < 3; ++i) {
        if (strlen(base) + 4U >= sizeof(candidate)) continue;
        strcpy(candidate, base);
        strcat(candidate, exts[i]);
        if (access(candidate, 0) == 0) {
            rc = spawnve(mode, candidate, argv, envp);
            if (rc != -1 || errno != ENOENT) return rc;
        }
    }
    errno = ENOENT;
    return -1;
}

int spawnvp(int mode, const char *file, const char *const *argv)
{
    const char *slash, *dot, *path_env;
    int has_ext, rc;

    if (!file || !*file) {
        errno = ENOENT;
        return -1;
    }
    slash = strrchr(file, '\\');
    if (!slash) slash = strrchr(file, '/');
    dot = strrchr(slash ? (slash + 1) : file, '.');
    has_ext = (dot != NULL);

    if (slash || (isalpha((unsigned char)file[0]) && file[1] == ':')) {
        return try_exec_exts(mode, file, has_ext, argv, NULL);
    }

    rc = try_exec_exts(mode, file, has_ext, argv, NULL);
    if (rc != -1 || errno != ENOENT) return rc;

    path_env = getenv("PATH");
    if (!path_env) {
        errno = ENOENT;
        return -1;
    }
    while (*path_env != '\0') {
        char dir[_MAX_PATH + 8];
        size_t dlen = 0U;
        while (*path_env != '\0' && *path_env != ';') {
            if (dlen + 2U < sizeof(dir)) dir[dlen++] = *path_env;
            ++path_env;
        }
        if (*path_env == ';') ++path_env;
        if (dlen == 0U) continue;
        if (dir[dlen - 1U] != '\\' && dir[dlen - 1U] != '/') dir[dlen++] = '\\';
        dir[dlen] = '\0';
        if (dlen + strlen(file) + 5U >= sizeof(dir)) continue;
        strcat(dir, file);
        rc = try_exec_exts(mode, dir, has_ext, argv, NULL);
        if (rc != -1 || errno != ENOENT) return rc;
    }
    errno = ENOENT;
    return -1;
}

int spawnl(int mode, const char *path, const char *arg0, ...)
{
    const char *argv[34];
    int n = 0;
    va_list ap;
    argv[n++] = arg0;
    if (arg0 != NULL) {
        va_start(ap, arg0);
        while (n < 33) {
            const char *a = va_arg(ap, const char *);
            argv[n++] = a;
            if (a == NULL) break;
        }
        va_end(ap);
    }
    argv[33] = NULL;
    return spawnv(mode, path, argv);
}

int spawnlp(int mode, const char *file, const char *arg0, ...)
{
    const char *argv[34];
    int n = 0;
    va_list ap;
    argv[n++] = arg0;
    if (arg0 != NULL) {
        va_start(ap, arg0);
        while (n < 33) {
            const char *a = va_arg(ap, const char *);
            argv[n++] = a;
            if (a == NULL) break;
        }
        va_end(ap);
    }
    argv[33] = NULL;
    return spawnvp(mode, file, argv);
}

int spawnle(int mode, const char *path, const char *arg0, ...)
{
    const char *argv[34];
    const char *const *envp = NULL;
    int n = 0;
    va_list ap;
    argv[n++] = arg0;
    va_start(ap, arg0);
    if (arg0 != NULL) {
        while (n < 33) {
            const char *a = va_arg(ap, const char *);
            argv[n++] = a;
            if (a == NULL) break;
        }
    }
    envp = va_arg(ap, const char *const *);
    va_end(ap);
    argv[33] = NULL;
    return spawnve(mode, path, argv, envp);
}

int system(const char *command)
{
    const char *comspec = getenv("COMSPEC");
    const char *argv[4];
    if (!comspec || !*comspec) comspec = "C:\\COMMAND.COM";
    if (command == NULL) {
        return (access(comspec, 0) == 0) ? 1 : 0;
    }
    argv[0] = comspec;
    argv[1] = "/C";
    argv[2] = command;
    argv[3] = NULL;
    return spawnvp(P_WAIT, comspec, argv);
}

/* --- <time.h> --- */

clock_t clock(void)
{
    union REGS r;
    unsigned long secs;
    r.h.ah = 0x2C;
    intdos(&r, &r);
    secs = (unsigned long)r.h.ch * 3600UL +
           (unsigned long)r.h.cl * 60UL +
           (unsigned long)r.h.dh;
    return (clock_t)(secs * 100UL + (unsigned long)r.h.dl);
}

static int is_leap(int yr)
{
    return (yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0);
}

static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

time_t mktime(struct tm *t)
{
    long days = 0L;
    int yr, m;
    if (!t) return (time_t)-1L;
    yr = t->tm_year + 1900;
    for (m = 1970; m < yr; ++m) days += is_leap(m) ? 366L : 365L;
    for (m = 0; m < t->tm_mon && m < 12; ++m) {
        days += mdays[m];
        if (m == 1 && is_leap(yr)) ++days;
    }
    days += (long)(t->tm_mday - 1);
    return (time_t)(days * 86400L + (long)t->tm_hour * 3600L +
                    (long)t->tm_min * 60L + (long)t->tm_sec);
}

time_t time(time_t *tloc)
{
    union REGS rd, rt;
    struct tm tm_now;
    time_t res;
    rd.h.ah = 0x2A;
    intdos(&rd, &rd);
    rt.h.ah = 0x2C;
    intdos(&rt, &rt);
    memset(&tm_now, 0, sizeof(tm_now));
    tm_now.tm_year = (int)rd.x.cx - 1900;
    tm_now.tm_mon  = (int)rd.h.dh - 1;
    tm_now.tm_mday = (int)rd.h.dl;
    tm_now.tm_hour = (int)rt.h.ch;
    tm_now.tm_min  = (int)rt.h.cl;
    tm_now.tm_sec  = (int)rt.h.dh;
    res = mktime(&tm_now);
    if (tloc) *tloc = res;
    return res;
}

double difftime(time_t time1, time_t time0)
{
    return (double)(time1 - time0);
}

struct tm *gmtime(const time_t *timer)
{
    static struct tm tm_buf;
    long sec = timer ? (long)*timer : 0L;
    long days;
    int yr = 1970, m;
    if (sec < 0L) sec = 0L;
    days = sec / 86400L;
    sec %= 86400L;
    tm_buf.tm_hour = (int)(sec / 3600L);
    sec %= 3600L;
    tm_buf.tm_min = (int)(sec / 60L);
    tm_buf.tm_sec = (int)(sec % 60L);
    tm_buf.tm_wday = (int)((days + 4L) % 7L);
    for (;;) {
        int dy = is_leap(yr) ? 366 : 365;
        if (days < (long)dy) break;
        days -= (long)dy;
        ++yr;
    }
    tm_buf.tm_year = yr - 1900;
    tm_buf.tm_yday = (int)days;
    for (m = 0; m < 12; ++m) {
        int dm = mdays[m] + ((m == 1 && is_leap(yr)) ? 1 : 0);
        if (days < (long)dm) break;
        days -= (long)dm;
    }
    tm_buf.tm_mon = m;
    tm_buf.tm_mday = (int)days + 1;
    tm_buf.tm_isdst = 0;
    return &tm_buf;
}

struct tm *localtime(const time_t *timer)
{
    return gmtime(timer);
}
