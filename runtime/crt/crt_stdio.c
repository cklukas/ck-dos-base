/* Clean-room 16-bit MS-DOS CRT stdio, environment, and process stubs (WP-5) */

extern int open(const char *path, int oflag);
extern int creat(const char *path, int mode);
extern int close(int fd);
extern int read(int fd, void *buf, unsigned int count);
extern int write(int fd, const void *buf, unsigned int count);
extern long lseek(int fd, long offset, int origin);
extern unsigned int strlen(const char *s);

struct _iobuf {
    char *_ptr;
    int _cnt;
    char *_base;
    char _flag;
    char _file;
};

struct _iobuf _iob[20] = {
    { (char *)0, 0, (char *)0, 0x01, 0 },
    { (char *)0, 0, (char *)0, 0x02, 1 },
    { (char *)0, 0, (char *)0, 0x02, 2 },
    { (char *)0, 0, (char *)0, 0x03, 3 },
    { (char *)0, 0, (char *)0, 0x02, 4 }
};

int _filbuf(struct _iobuf *f) {
    unsigned char ch;
    int n = read((int)f->_file, &ch, 1);
    if (n <= 0) {
        f->_flag |= 0x10;
        return -1;
    }
    return (int)ch;
}

int _flsbuf(int c, struct _iobuf *f) {
    unsigned char ch = (unsigned char)c;
    if (write((int)f->_file, &ch, 1) != 1) {
        f->_flag |= 0x20;
        return -1;
    }
    return (int)ch;
}

struct _iobuf *fopen(const char *path, const char *mode) {
    int i;
    int fd;
    if (mode[0] == 'w') {
        fd = creat(path, 0);
    } else if (mode[0] == 'r' && mode[1] == '+') {
        fd = open(path, 2);
    } else if (mode[0] == 'a') {
        fd = open(path, 2);
        if (fd < 0) {
            fd = creat(path, 0);
        } else {
            lseek(fd, 0L, 2);
        }
    } else {
        fd = open(path, 0);
    }
    if (fd < 0) {
        return (struct _iobuf *)0;
    }
    for (i = 5; i < 20; ++i) {
        if (_iob[i]._flag == 0) {
            _iob[i]._ptr = (char *)0;
            _iob[i]._cnt = 0;
            _iob[i]._base = (char *)0;
            _iob[i]._flag = (mode[0] == 'w' || mode[0] == 'a') ? 0x02 : 0x01;
            _iob[i]._file = (char)fd;
            return &_iob[i];
        }
    }
    close(fd);
    return (struct _iobuf *)0;
}

int fflush(struct _iobuf *f) {
    (void)f;
    return 0;
}

unsigned int fread(void *buf, unsigned int size, unsigned int count, struct _iobuf *f) {
    unsigned int total = size * count;
    unsigned int got = 0;
    char *p = (char *)buf;
    int n;
    if (total == 0 || size == 0 || f == (struct _iobuf *)0) {
        return 0;
    }
    while (got < total) {
        n = read((int)f->_file, p + got, total - got);
        if (n <= 0) {
            break;
        }
        got += (unsigned int)n;
    }
    return got / size;
}

int fseek(struct _iobuf *f, long offset, int origin) {
    if (f == (struct _iobuf *)0) {
        return -1;
    }
    return (lseek((int)f->_file, offset, origin) < 0L) ? -1 : 0;
}

long ftell(struct _iobuf *f) {
    if (f == (struct _iobuf *)0) {
        return -1L;
    }
    return lseek((int)f->_file, 0L, 1);
}

int fgetc(struct _iobuf *f) {
    unsigned char ch;
    if (f == (struct _iobuf *)0) {
        return -1;
    }
    if (read((int)f->_file, &ch, 1) != 1) {
        return -1;
    }
    return (int)ch;
}

int fputc(int c, struct _iobuf *f) {
    unsigned char ch = (unsigned char)c;
    if (f == (struct _iobuf *)0) {
        return -1;
    }
    if (write((int)f->_file, &ch, 1) != 1) {
        return -1;
    }
    return (int)ch;
}

int fclose(struct _iobuf *f) {
    int rc;
    if (f == (struct _iobuf *)0) {
        return -1;
    }
    rc = close((int)f->_file);
    f->_flag = 0;
    return rc;
}

unsigned int fwrite(const void *buf, unsigned int size, unsigned int count, struct _iobuf *f) {
    unsigned int total = size * count;
    int written;
    if (total == 0) {
        return 0;
    }
    written = write((int)f->_file, buf, total);
    if (written <= 0) {
        return 0;
    }
    return (unsigned int)written / size;
}

int fputs(const char *s, struct _iobuf *f) {
    unsigned int len = strlen(s);
    if (len == 0) {
        return 0;
    }
    return (write((int)f->_file, s, len) == (int)len) ? 0 : -1;
}

static void fmt_uint(char **dst, unsigned long v, unsigned int base, int upper, int width, char pad) {
    char tmp[16];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v != 0) {
            tmp[i++] = digits[v % base];
            v /= base;
        }
    }
    while (i < width && i < 16) {
        tmp[i++] = pad;
    }
    while (i > 0) {
        *(*dst)++ = tmp[--i];
    }
}

static int vsprintf_mini(char *out, const char *fmt, char *ap) {
    char *start = out;
    while (*fmt != '\0') {
        if (*fmt != '%') {
            *out++ = *fmt++;
            continue;
        }
        ++fmt;
        if (*fmt == '%') {
            *out++ = *fmt++;
            continue;
        }
        {
            int is_long = 0;
            int width = 0;
            char pad = ' ';
            while (*fmt == '-' || *fmt == '+' || *fmt == '0') {
                if (*fmt == '0') pad = '0';
                ++fmt;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                ++fmt;
            }
            if (*fmt == 'l') {
                is_long = 1;
                ++fmt;
            }
            if (*fmt == 's') {
                char *s = *(char **)ap;
                ap += sizeof(char *);
                if (s == (char *)0) {
                    s = "(null)";
                }
                while (*s != '\0') {
                    *out++ = *s++;
                }
            } else if (*fmt == 'c') {
                int c = *(int *)ap;
                ap += sizeof(int);
                *out++ = (char)c;
            } else if (*fmt == 'd' || *fmt == 'i') {
                long v = is_long ? *(long *)ap : (long)(*(int *)ap);
                ap += is_long ? sizeof(long) : sizeof(int);
                if (v < 0) {
                    *out++ = '-';
                    v = -v;
                    if (width > 0) --width;
                }
                fmt_uint(&out, (unsigned long)v, 10, 0, width, pad);
            } else if (*fmt == 'u' || *fmt == 'x' || *fmt == 'X') {
                unsigned long v = is_long ? *(unsigned long *)ap : (unsigned long)(*(unsigned int *)ap);
                unsigned int base = (*fmt == 'u') ? 10 : 16;
                int upper = (*fmt == 'X');
                ap += is_long ? sizeof(long) : sizeof(int);
                fmt_uint(&out, v, base, upper, width, pad);
            } else if (*fmt != '\0') {
                *out++ = *fmt;
            }
            if (*fmt != '\0') {
                ++fmt;
            }
        }
    }
    *out = '\0';
    return (int)(out - start);
}

int sprintf(char *dst, const char *fmt, ...) {
    char *ap = (char *)&fmt + sizeof(fmt);
    return vsprintf_mini(dst, fmt, ap);
}

static void write_text(int fd, const char *buf, int n) {
    int i = 0;
    int start = 0;
    while (i < n) {
        if (buf[i] == '\n') {
            if (i > start) {
                write(fd, buf + start, (unsigned int)(i - start));
            }
            write(fd, "\r\n", 2);
            start = i + 1;
        }
        ++i;
    }
    if (i > start) {
        write(fd, buf + start, (unsigned int)(i - start));
    }
}

int fprintf(struct _iobuf *f, const char *fmt, ...) {
    char buf[512];
    char *ap = (char *)&fmt + sizeof(fmt);
    int n = vsprintf_mini(buf, fmt, ap);
    if (n > 0) {
        write_text((int)f->_file, buf, n);
    }
    return n;
}

int printf(const char *fmt, ...) {
    char buf[512];
    char *ap = (char *)&fmt + sizeof(fmt);
    int n = vsprintf_mini(buf, fmt, ap);
    if (n > 0) {
        write_text(1, buf, n);
    }
    return n;
}

int sys_nerr = 1;
char *sys_errlist[1] = { "DOS error" };

char *fgets(char *s, int n, struct _iobuf *f) {
    int i = 0;
    int c;
    if (s == (char *)0 || n <= 0) {
        return (char *)0;
    }
    while (i + 1 < n) {
        c = _filbuf(f);
        if (c < 0) {
            if (i == 0) {
                return (char *)0;
            }
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    s[i] = '\0';
    return s;
}

int sscanf(const char *src, const char *fmt, ...) {
    (void)src;
    (void)fmt;
    return 0;
}

char *getenv(const char *name) {
    (void)name;
    return (char *)0;
}

void (*signal(int sig, void (*func)(int)))(int) {
    (void)sig;
    return func;
}

int spawnlp(int mode, const char *cmd, const char *arg0, ...) {
    (void)mode;
    (void)cmd;
    (void)arg0;
    return -1;
}
