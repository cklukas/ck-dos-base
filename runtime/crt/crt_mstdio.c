/*
 * Clean-room Medium-Model C89 <stdio.h> + IEEE-754 printf/strtod runtime
 * for ckcl 16-bit 8086 targets (MLIBCE.LIB).
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <io.h>
#include <math.h>
#include <share.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _F_READ  0x0001U
#define _F_WRIT  0x0002U
#define _F_UNBUF 0x0004U
#define _F_MYBUF 0x0008U
#define _F_EOF   0x0010U
#define _F_ERR   0x0020U
#define _F_LBUF  0x0040U
#define _F_RDWR  0x0080U
#define _F_BIN   0x0100U
#define _F_IN    0x0200U
#define _F_OUT   0x0400U
#define _F_OPEN  0x0800U

FILE _iob[FOPEN_MAX] = {
    { NULL, 0, NULL, _F_OPEN | _F_READ,           0, 0, -1 },
    { NULL, 0, NULL, _F_OPEN | _F_WRIT,           1, 0, -1 },
    { NULL, 0, NULL, _F_OPEN | _F_WRIT | _F_UNBUF, 2, 0, -1 }
};

static void ensure_buffer(FILE *f)
{
    if ((f->_flag & _F_UNBUF) || f->_base != NULL) return;
    if ((f->_file == 0 || f->_file == 1) && isatty(f->_file)) {
        f->_flag |= _F_UNBUF;
        return;
    }
    f->_base = (char *)malloc(BUFSIZ);
    if (!f->_base) {
        f->_flag |= _F_UNBUF;
        return;
    }
    f->_bufsiz = BUFSIZ;
    f->_flag |= _F_MYBUF;
    f->_ptr = f->_base;
    f->_cnt = 0;
}

static int flush_write_buf(FILE *f)
{
    if (!f || !(f->_flag & _F_OPEN)) return EOF;
    if ((f->_flag & _F_OUT) && f->_base != NULL) {
        int pending = (int)(f->_ptr - f->_base);
        f->_ptr = f->_base;
        f->_cnt = (int)f->_bufsiz;
        f->_flag &= ~_F_OUT;
        if (pending > 0) {
            int off = 0;
            while (off < pending) {
                int wr = write(f->_file, f->_base + off, (unsigned int)(pending - off));
                if (wr <= 0) {
                    f->_flag |= _F_ERR;
                    return EOF;
                }
                off += wr;
            }
        }
    }
    return 0;
}

int fflush(FILE *stream)
{
    int i, rc = 0;
    if (stream == NULL) {
        for (i = 0; i < FOPEN_MAX; ++i) {
            if ((_iob[i]._flag & _F_OPEN) && (_iob[i]._flag & _F_OUT)) {
                if (flush_write_buf(&_iob[i]) == EOF) rc = EOF;
            }
        }
        return rc;
    }
    if (!(stream->_flag & _F_OPEN)) return EOF;
    if (stream->_flag & _F_OUT) {
        return flush_write_buf(stream);
    }
    return 0;
}

void __stdio_exit_flush(void)
{
    fflush(NULL);
}

static int parse_open_mode(const char *mode, int *out_oflag, unsigned int *out_fflag)
{
    int oflag = 0;
    unsigned int fflag = _F_OPEN;
    int plus = 0, bin = 0;
    const char *p;

    if (!mode || !*mode) return -1;
    for (p = mode + 1; *p != '\0'; ++p) {
        if (*p == '+') plus = 1;
        else if (*p == 'b') bin = 1;
        else if (*p == 't') bin = 0;
    }

    switch (mode[0]) {
        case 'r':
            oflag = plus ? O_RDWR : O_RDONLY;
            fflag |= plus ? _F_RDWR : _F_READ;
            break;
        case 'w':
            oflag = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
            fflag |= plus ? _F_RDWR : _F_WRIT;
            break;
        case 'a':
            oflag = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
            fflag |= plus ? _F_RDWR : _F_WRIT;
            break;
        default:
            return -1;
    }

    if (bin) {
        oflag |= O_BINARY;
        fflag |= _F_BIN;
    } else {
        oflag |= O_TEXT;
    }

    *out_oflag = oflag;
    *out_fflag = fflag;
    return 0;
}

static FILE *alloc_stream_slot(void)
{
    int i;
    for (i = 3; i < FOPEN_MAX; ++i) {
        if (!(_iob[i]._flag & _F_OPEN)) {
            return &_iob[i];
        }
    }
    errno = EMFILE;
    return NULL;
}

FILE *_fsopen(const char *path, const char *mode, int shflag)
{
    int oflag = 0, fd;
    unsigned int fflag = 0U;
    FILE *f;

    if (parse_open_mode(mode, &oflag, &fflag) != 0) {
        errno = EINVAL;
        return NULL;
    }
    f = alloc_stream_slot();
    if (!f) return NULL;

    fd = sopen(path, oflag, shflag, 0x0180);
    if (fd < 0) return NULL;

    f->_ptr = NULL;
    f->_cnt = 0;
    f->_base = NULL;
    f->_flag = fflag;
    f->_file = fd;
    f->_bufsiz = 0;
    f->_ungot = -1;
    return f;
}

FILE *fopen(const char *path, const char *mode)
{
    return _fsopen(path, mode, 0);
}

FILE *fdopen(int fd, const char *mode)
{
    int oflag = 0;
    unsigned int fflag = 0U;
    FILE *f;

    if (fd < 0 || parse_open_mode(mode, &oflag, &fflag) != 0) {
        errno = EINVAL;
        return NULL;
    }
    f = alloc_stream_slot();
    if (!f) return NULL;

    f->_ptr = NULL;
    f->_cnt = 0;
    f->_base = NULL;
    f->_flag = fflag;
    f->_file = fd;
    f->_bufsiz = 0;
    f->_ungot = -1;
    return f;
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    int oflag = 0, fd;
    unsigned int fflag = 0U;
    if (!stream) return NULL;
    fclose(stream);
    if (parse_open_mode(mode, &oflag, &fflag) != 0) return NULL;
    fd = sopen(path, oflag, 0, 0x0180);
    if (fd < 0) return NULL;
    stream->_ptr = NULL;
    stream->_cnt = 0;
    stream->_base = NULL;
    stream->_flag = fflag;
    stream->_file = fd;
    stream->_bufsiz = 0;
    stream->_ungot = -1;
    return stream;
}

int fclose(FILE *stream)
{
    int rc = 0;
    if (!stream || !(stream->_flag & _F_OPEN)) return EOF;
    if (flush_write_buf(stream) == EOF) rc = EOF;
    if ((stream->_flag & _F_MYBUF) && stream->_base != NULL) {
        free(stream->_base);
    }
    if (stream->_file > 2) {
        if (close(stream->_file) != 0) rc = EOF;
    }
    stream->_ptr = NULL;
    stream->_cnt = 0;
    stream->_base = NULL;
    stream->_flag = 0U;
    stream->_file = -1;
    stream->_bufsiz = 0;
    stream->_ungot = -1;
    return rc;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    if (!stream || !(stream->_flag & _F_OPEN)) return -1;
    flush_write_buf(stream);
    if ((stream->_flag & _F_MYBUF) && stream->_base != NULL) {
        free(stream->_base);
    }
    stream->_base = NULL;
    stream->_ptr = NULL;
    stream->_cnt = 0;
    stream->_bufsiz = 0;
    stream->_flag &= ~(_F_UNBUF | _F_MYBUF | _F_LBUF | _F_IN | _F_OUT);

    if (mode == _IONBF) {
        stream->_flag |= _F_UNBUF;
        return 0;
    }
    if (size == 0U) size = BUFSIZ;
    if (buf == NULL) {
        buf = (char *)malloc(size);
        if (!buf) return -1;
        stream->_flag |= _F_MYBUF;
    }
    stream->_base = buf;
    stream->_ptr = buf;
    stream->_bufsiz = (unsigned int)size;
    if (mode == _IOLBF) stream->_flag |= _F_LBUF;
    return 0;
}

void setbuf(FILE *stream, char *buf)
{
    setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

static int raw_getc(FILE *f)
{
    unsigned char ch;
    if (!f || !(f->_flag & _F_OPEN)) return EOF;
    if (!(f->_flag & (_F_READ | _F_RDWR))) {
        f->_flag |= _F_ERR;
        return EOF;
    }
    if (f->_flag & _F_OUT) {
        if (flush_write_buf(f) == EOF) return EOF;
    }
    ensure_buffer(f);
    if (f->_flag & _F_UNBUF) {
        int n = read(f->_file, &ch, 1U);
        if (n == 0) { f->_flag |= _F_EOF; return EOF; }
        if (n < 0)  { f->_flag |= _F_ERR; return EOF; }
        return (int)ch;
    }
    if (f->_cnt <= 0) {
        int n = read(f->_file, f->_base, f->_bufsiz);
        if (n == 0) {
            f->_flag &= ~_F_IN;
            f->_flag |= _F_EOF;
            return EOF;
        }
        if (n < 0) {
            f->_flag &= ~_F_IN;
            f->_flag |= _F_ERR;
            return EOF;
        }
        f->_ptr = f->_base;
        f->_cnt = n;
        f->_flag |= _F_IN;
    }
    --f->_cnt;
    return (int)(unsigned char)*f->_ptr++;
}

int fgetc(FILE *stream)
{
    int c;
    if (!stream) return EOF;
    if (stream->_ungot >= 0) {
        c = stream->_ungot;
        stream->_ungot = -1;
        stream->_flag &= ~_F_EOF;
        return c;
    }
    if (stream->_flag & _F_EOF) return EOF;
    for (;;) {
        c = raw_getc(stream);
        if (c == EOF) return EOF;
        if (stream->_flag & _F_BIN) return c;
        if (c == 0x1A) {
            stream->_flag |= _F_EOF;
            return EOF;
        }
        if (c == '\r') {
            int next = raw_getc(stream);
            if (next == '\n') return '\n';
            if (next != EOF) stream->_ungot = next;
            return '\r';
        }
        return c;
    }
}

int getc(FILE *stream) { return fgetc(stream); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *stream)
{
    if (c == EOF || !stream || !(stream->_flag & _F_OPEN)) return EOF;
    stream->_ungot = (int)(unsigned char)c;
    stream->_flag &= ~_F_EOF;
    return (int)(unsigned char)c;
}

static int raw_putc(int c, FILE *f)
{
    unsigned char ch = (unsigned char)c;
    if (!f || !(f->_flag & _F_OPEN)) return EOF;
    if (!(f->_flag & (_F_WRIT | _F_RDWR))) {
        f->_flag |= _F_ERR;
        return EOF;
    }
    if (f->_flag & _F_IN) {
        /* Switch from read to write requires prior fseek/rewind per C89 */
        f->_cnt = 0;
        f->_ptr = f->_base;
        f->_flag &= ~_F_IN;
    }
    ensure_buffer(f);
    if (f->_flag & _F_UNBUF) {
        if (write(f->_file, &ch, 1U) != 1) {
            f->_flag |= _F_ERR;
            return EOF;
        }
        return (int)ch;
    }
    if (!(f->_flag & _F_OUT)) {
        f->_ptr = f->_base;
        f->_cnt = (int)f->_bufsiz;
        f->_flag |= _F_OUT;
    }
    *f->_ptr++ = (char)ch;
    --f->_cnt;
    if (f->_cnt <= 0 || ((f->_flag & _F_LBUF) && ch == '\n')) {
        if (flush_write_buf(f) == EOF) return EOF;
    }
    return (int)ch;
}

int fputc(int c, FILE *stream)
{
    unsigned char ch = (unsigned char)c;
    if (!stream) return EOF;
    if (!(stream->_flag & _F_BIN) && ch == '\n') {
        if (raw_putc('\r', stream) == EOF) return EOF;
    }
    return raw_putc((int)ch, stream);
}

int putc(int c, FILE *stream) { return fputc(c, stream); }
int putchar(int c) { return fputc(c, stdout); }

char *fgets(char *s, int n, FILE *stream)
{
    int i = 0, c;
    if (!s || n <= 0 || !stream) return NULL;
    while (i + 1 < n) {
        c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

char *gets(char *s)
{
    int i = 0, c;
    if (!s) return NULL;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        s[i++] = (char)c;
    }
    if (c == EOF && i == 0) return NULL;
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream)
{
    if (!s || !stream) return EOF;
    while (*s != '\0') {
        if (fputc((unsigned char)*s++, stream) == EOF) return EOF;
    }
    return 0;
}

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    unsigned char *dst = (unsigned char *)ptr;
    size_t total, done = 0U;

    if (!ptr || !stream || size == 0U || nmemb == 0U) return 0U;
    if (nmemb > (size_t)(0xFFFFU / size)) total = 0xFFFFU;
    else total = size * nmemb;

    if ((stream->_flag & _F_BIN) && stream->_ungot < 0 && (stream->_flag & _F_UNBUF)) {
        while (done < total) {
            int n = read(stream->_file, dst + done, (unsigned int)(total - done));
            if (n == 0) { stream->_flag |= _F_EOF; break; }
            if (n < 0)  { stream->_flag |= _F_ERR; break; }
            done += (size_t)n;
        }
        return done / size;
    }

    while (done < total) {
        if ((stream->_flag & _F_BIN) && stream->_ungot < 0 &&
            (stream->_flag & _F_IN) && stream->_cnt > 0) {
            size_t avail = (size_t)stream->_cnt;
            size_t chunk = (total - done) < avail ? (total - done) : avail;
            memcpy(dst + done, stream->_ptr, chunk);
            stream->_ptr += chunk;
            stream->_cnt -= (int)chunk;
            done += chunk;
            continue;
        }
        {
            int c = fgetc(stream);
            if (c == EOF) break;
            dst[done++] = (unsigned char)c;
        }
    }
    return done / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    const unsigned char *src = (const unsigned char *)ptr;
    size_t total, done = 0U;

    if (!ptr || !stream || size == 0U || nmemb == 0U) return 0U;
    if (nmemb > (size_t)(0xFFFFU / size)) total = 0xFFFFU;
    else total = size * nmemb;

    if ((stream->_flag & _F_BIN) && (stream->_flag & _F_UNBUF)) {
        while (done < total) {
            int n = write(stream->_file, src + done, (unsigned int)(total - done));
            if (n <= 0) { stream->_flag |= _F_ERR; break; }
            done += (size_t)n;
        }
        return done / size;
    }

    while (done < total) {
        if (fputc(src[done], stream) == EOF) break;
        ++done;
    }
    return done / size;
}

long ftell(FILE *stream)
{
    long pos;
    if (!stream || !(stream->_flag & _F_OPEN)) {
        errno = EBADF;
        return -1L;
    }
    if (stream->_flag & _F_OUT) {
        if (flush_write_buf(stream) == EOF) return -1L;
    }
    pos = lseek(stream->_file, 0L, SEEK_CUR);
    if (pos < 0L) return -1L;
    if ((stream->_flag & _F_IN) && stream->_cnt > 0) {
        pos -= (long)stream->_cnt;
    }
    if (stream->_ungot >= 0) {
        --pos;
    }
    if (pos < 0L) pos = 0L;
    return pos;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream || !(stream->_flag & _F_OPEN)) {
        errno = EBADF;
        return -1;
    }
    if (stream->_flag & _F_OUT) {
        if (flush_write_buf(stream) == EOF) return -1;
    }
    if (whence == SEEK_CUR) {
        if ((stream->_flag & _F_IN) && stream->_cnt > 0) {
            offset -= (long)stream->_cnt;
        }
        if (stream->_ungot >= 0) {
            --offset;
        }
    }
    stream->_cnt = 0;
    stream->_ptr = stream->_base;
    stream->_ungot = -1;
    stream->_flag &= ~(_F_EOF | _F_IN);
    return (lseek(stream->_file, offset, whence) < 0L) ? -1 : 0;
}

void rewind(FILE *stream)
{
    if (stream) {
        fseek(stream, 0L, SEEK_SET);
        stream->_flag &= ~_F_ERR;
    }
}

int fgetpos(FILE *stream, fpos_t *pos)
{
    long p = ftell(stream);
    if (p < 0L || !pos) return -1;
    *pos = p;
    return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos)
{
    if (!pos) return -1;
    return fseek(stream, *pos, SEEK_SET);
}

void clearerr(FILE *stream)
{
    if (stream) stream->_flag &= ~(_F_EOF | _F_ERR);
}

int feof(FILE *stream)
{
    return (stream && (stream->_flag & _F_EOF) && stream->_ungot < 0) ? 1 : 0;
}

int ferror(FILE *stream)
{
    return (stream && (stream->_flag & _F_ERR)) ? 1 : 0;
}

int fileno(FILE *stream)
{
    return stream ? stream->_file : -1;
}

int _fileno(FILE *stream)
{
    return fileno(stream);
}

void perror(const char *s)
{
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

char *tmpnam(char *s)
{
    static char internal[L_tmpnam];
    static unsigned int seq = 0U;
    char *out = s ? s : internal;
    ++seq;
    sprintf(out, "TMP%05u.$$$", seq);
    return out;
}

FILE *tmpfile(void)
{
    char name[L_tmpnam];
    return fopen(tmpnam(name), "w+b");
}

/* --- IEEE-754 pow10 helper for strtod & printf --- */

static const double p10_small[23] = {
    1.0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

static const double p10_big[5] = {
    1e16, 1e32, 1e64, 1e128, 1e256
};

static double scale_pow10(double val, int exp10)
{
    int i, e;
    if (val == 0.0 || exp10 == 0) return val;
    if (exp10 > 0) {
        if (exp10 <= 22) return val * p10_small[exp10];
        if (exp10 > 309) return val * 1e308 * 1e10;
        e = exp10;
        val *= p10_small[e & 15];
        e >>= 4;
        for (i = 0; e > 0 && i < 5; ++i, e >>= 1) {
            if (e & 1) val *= p10_big[i];
        }
        return val;
    }
    e = -exp10;
    if (e <= 22) return val / p10_small[e];
    if (e > 340) return 0.0 * val;
    while (e > 308) {
        val /= 1e16;
        e -= 16;
    }
    val /= p10_small[e & 15];
    e >>= 4;
    for (i = 0; e > 0 && i < 5; ++i, e >>= 1) {
        if (e & 1) val /= p10_big[i];
    }
    return val;
}

double strtod(const char *s, char **endptr)
{
    const char *p = s;
    int neg = 0;
    unsigned long hi9 = 0UL, lo9 = 0UL;
    int sig_digits = 0, total_digits = 0, frac_digits = 0;
    int exp10 = 0;
    double val;

    while (isspace((unsigned char)*p)) ++p;
    if (*p == '-') { neg = 1; ++p; }
    else if (*p == '+') { ++p; }

    /* INF / INFINITY / NAN */
    if (strnicmp(p, "inf", 3) == 0) {
        p += 3;
        if (strnicmp(p, "inity", 5) == 0) p += 5;
        if (endptr) *endptr = (char *)p;
        val = HUGE_VAL * 2.0;
        return neg ? -val : val;
    }
    if (strnicmp(p, "nan", 3) == 0) {
        p += 3;
        if (endptr) *endptr = (char *)p;
        val = 0.0 / 0.0;
        return val;
    }

    while (isdigit((unsigned char)*p)) {
        int d = *p - '0';
        ++total_digits;
        if (sig_digits > 0 || d != 0) {
            if (sig_digits < 9) {
                hi9 = hi9 * 10UL + (unsigned long)d;
                ++sig_digits;
            } else if (sig_digits < 18) {
                lo9 = lo9 * 10UL + (unsigned long)d;
                ++sig_digits;
            } else {
                ++exp10;
            }
        }
        ++p;
    }

    if (*p == '.') {
        ++p;
        while (isdigit((unsigned char)*p)) {
            int d = *p - '0';
            ++total_digits;
            if (sig_digits == 0 && d == 0) {
                --exp10;
            } else if (sig_digits < 9) {
                hi9 = hi9 * 10UL + (unsigned long)d;
                ++sig_digits;
                ++frac_digits;
            } else if (sig_digits < 18) {
                lo9 = lo9 * 10UL + (unsigned long)d;
                ++sig_digits;
                ++frac_digits;
            }
            ++p;
        }
    }

    if (total_digits == 0) {
        if (endptr) *endptr = (char *)s;
        return 0.0;
    }

    exp10 -= frac_digits;

    if (*p == 'e' || *p == 'E' || *p == 'd' || *p == 'D') {
        const char *ep = p + 1;
        int eneg = 0;
        long eval = 0L;
        if (*ep == '-') { eneg = 1; ++ep; }
        else if (*ep == '+') { ++ep; }
        if (isdigit((unsigned char)*ep)) {
            while (isdigit((unsigned char)*ep)) {
                if (eval < 10000L) eval = eval * 10L + (long)(*ep - '0');
                ++ep;
            }
            p = ep;
            exp10 += eneg ? -(int)eval : (int)eval;
        }
    }

    if (endptr) *endptr = (char *)p;

    if (sig_digits == 0) {
        return neg ? -0.0 : 0.0;
    }

    if (sig_digits <= 9) {
        val = (double)hi9;
    } else {
        val = (double)hi9 * p10_small[sig_digits - 9] + (double)lo9;
    }

    val = scale_pow10(val, exp10);
    if (val > DBL_MAX) errno = ERANGE;
    else if (val == 0.0) errno = ERANGE;
    return neg ? -val : val;
}

double atof(const char *s)
{
    return strtod(s, NULL);
}

/* --- IEEE-754 printf formatting --- */

typedef struct {
    char *buf;
    size_t cap;
    size_t pos;
    int has_cap;
    FILE *stream;
    int err;
} fmt_sink;

static void sink_putc(fmt_sink *sk, char c)
{
    if (sk->stream != NULL) {
        if (!sk->err && fputc((unsigned char)c, sk->stream) == EOF) {
            sk->err = 1;
        }
        ++sk->pos;
        return;
    }
    if (!sk->has_cap || sk->pos + 1U < sk->cap) {
        if (sk->buf) sk->buf[sk->pos] = c;
    }
    ++sk->pos;
}

static void sink_pad(fmt_sink *sk, char ch, int count)
{
    while (count-- > 0) sink_putc(sk, ch);
}

static void sink_write(fmt_sink *sk, const char *s, int len)
{
    int i;
    for (i = 0; i < len; ++i) sink_putc(sk, s[i]);
}

static int fp_bits_sign(double v)
{
    const unsigned short *w = (const unsigned short *)&v;
    return (w[3] & 0x8000U) ? 1 : 0;
}

static int fp_bits_special(double v)
{
    const unsigned short *w = (const unsigned short *)&v;
    unsigned short e = w[3] & 0x7FF0U;
    if (e != 0x7FF0U) return 0;
    if ((w[3] & 0x000FU) || w[2] || w[1] || w[0]) return 2; /* NaN */
    return 1; /* Inf */
}

/* Produces 17 significant decimal digits in dig[0..16] ('0'..'9') and *out_exp10 */
static void fp_to_dec17(double mag, char *dig, int *out_exp10)
{
    int bexp = 0, k, i;
    double scaled, scaled8, rem;
    unsigned long hi9, lo8;

    if (mag == 0.0) {
        for (i = 0; i < 17; ++i) dig[i] = '0';
        dig[17] = '\0';
        *out_exp10 = 0;
        return;
    }

    frexp(mag, &bexp);
    k = (int)floor((double)(bexp - 1) * 0.30102999566398120);
    scaled = scale_pow10(mag, -k);
    while (scaled >= 10.0) {
        scaled /= 10.0;
        ++k;
    }
    while (scaled > 0.0 && scaled < 1.0) {
        scaled *= 10.0;
        --k;
    }

    scaled8 = scaled * 100000000.0;
    hi9 = (unsigned long)scaled8;
    if (hi9 < 100000000UL) hi9 = 100000000UL;
    if (hi9 >= 1000000000UL) {
        hi9 = 100000000UL;
        ++k;
        lo8 = 0UL;
    } else {
        rem = (scaled8 - (double)hi9) * 100000000.0;
        lo8 = (unsigned long)(rem + 0.5);
        if (lo8 >= 100000000UL) {
            lo8 -= 100000000UL;
            ++hi9;
            if (hi9 >= 1000000000UL) {
                hi9 = 100000000UL;
                ++k;
            }
        }
    }

    for (i = 8; i >= 0; --i) {
        dig[i] = (char)('0' + (hi9 % 10UL));
        hi9 /= 10UL;
    }
    for (i = 16; i >= 9; --i) {
        dig[i] = (char)('0' + (lo8 % 10UL));
        lo8 /= 10UL;
    }
    dig[17] = '\0';
    *out_exp10 = k;
}

/* Round dig[0..16] at `keep` digits (0 <= keep <= 17); adjusts *exp10 on carry */
static void round_dec17(char *dig, int keep, int *exp10, int is_zero)
{
    int i;
    if (is_zero || keep >= 17) return;
    if (keep < 0) {
        for (i = 0; i < 17; ++i) dig[i] = '0';
        return;
    }
    if (dig[keep] >= '5') {
        int round_up = 1;
        if (dig[keep] == '5') {
            int tail_nz = 0;
            for (i = keep + 1; i < 17; ++i) {
                if (dig[i] != '0') { tail_nz = 1; break; }
            }
            if (!tail_nz && keep > 0 && ((dig[keep - 1] - '0') % 2 == 0)) {
                round_up = 0;
            }
        }
        if (round_up) {
            if (keep == 0) {
                dig[0] = '1';
                for (i = 1; i < 17; ++i) dig[i] = '0';
                ++(*exp10);
                return;
            }
            i = keep - 1;
            while (i >= 0 && dig[i] == '9') {
                dig[i] = '0';
                --i;
            }
            if (i >= 0) {
                ++dig[i];
            } else {
                dig[0] = '1';
                ++(*exp10);
            }
        }
    }
    for (i = keep; i < 17; ++i) dig[i] = '0';
}

static void format_float_item(fmt_sink *sk, double val, char spec,
                              int fl_left, int fl_plus, int fl_space,
                              int fl_alt, int fl_zero, int width, int prec)
{
    char tmp[360];
    int tlen = 0;
    int neg = fp_bits_sign(val);
    int sp = fp_bits_special(val);
    int upper = (spec == 'E' || spec == 'F' || spec == 'G');
    char sign_ch = 0;
    char dig[20];
    int exp10 = 0;
    int is_zero;
    char eff_mode;
    int eff_prec;

    if (neg) sign_ch = '-';
    else if (fl_plus) sign_ch = '+';
    else if (fl_space) sign_ch = ' ';

    if (sp != 0) {
        const char *word = (sp == 2) ? (upper ? "NAN" : "nan")
                                     : (upper ? "INF" : "inf");
        int wlen = 3 + ((sign_ch && sp == 1) ? 1 : 0);
        int pad = width - wlen;
        if (sp == 2) sign_ch = 0;
        if (!fl_left && pad > 0) sink_pad(sk, ' ', pad);
        if (sign_ch) sink_putc(sk, sign_ch);
        sink_write(sk, word, 3);
        if (fl_left && pad > 0) sink_pad(sk, ' ', pad);
        return;
    }

    if (prec < 0) prec = 6;
    val = fabs(val);
    is_zero = (val == 0.0);
    fp_to_dec17(val, dig, &exp10);

    if (spec == 'g' || spec == 'G') {
        int gprec = (prec == 0) ? 1 : prec;
        round_dec17(dig, gprec, &exp10, is_zero);
        if (exp10 < -4 || exp10 >= gprec) {
            eff_mode = 'e';
            eff_prec = gprec - 1;
        } else {
            eff_mode = 'f';
            eff_prec = gprec - 1 - exp10;
            if (eff_prec < 0) eff_prec = 0;
        }
    } else if (spec == 'e' || spec == 'E') {
        eff_mode = 'e';
        eff_prec = prec;
        round_dec17(dig, eff_prec + 1, &exp10, is_zero);
    } else {
        eff_mode = 'f';
        eff_prec = prec;
        round_dec17(dig, exp10 + eff_prec + 1, &exp10, is_zero);
    }

    if (eff_mode == 'e') {
        int i, aexp;
        tmp[tlen++] = dig[0];
        if (eff_prec > 0 || fl_alt) {
            tmp[tlen++] = '.';
            for (i = 0; i < eff_prec; ++i) {
                tmp[tlen++] = (i + 1 < 17) ? dig[i + 1] : '0';
            }
            if ((spec == 'g' || spec == 'G') && !fl_alt) {
                while (tlen > 0 && tmp[tlen - 1] == '0') --tlen;
                if (tlen > 0 && tmp[tlen - 1] == '.') --tlen;
            }
        }
        tmp[tlen++] = upper ? 'E' : 'e';
        tmp[tlen++] = (exp10 < 0) ? '-' : '+';
        aexp = exp10 < 0 ? -exp10 : exp10;
        if (aexp >= 100) {
            tmp[tlen++] = (char)('0' + (aexp / 100));
        }
        tmp[tlen++] = (char)('0' + ((aexp / 10) % 10));
        tmp[tlen++] = (char)('0' + (aexp % 10));
    } else {
        int i;
        if (is_zero || exp10 < 0) {
            tmp[tlen++] = '0';
        } else {
            for (i = 0; i <= exp10; ++i) {
                tmp[tlen++] = (i < 17) ? dig[i] : '0';
            }
        }
        if (eff_prec > 0 || fl_alt) {
            tmp[tlen++] = '.';
            for (i = 0; i < eff_prec; ++i) {
                int didx = exp10 + 1 + i;
                if (is_zero || didx < 0) tmp[tlen++] = '0';
                else if (didx < 17) tmp[tlen++] = dig[didx];
                else tmp[tlen++] = '0';
            }
            if ((spec == 'g' || spec == 'G') && !fl_alt) {
                while (tlen > 0 && tmp[tlen - 1] == '0') --tlen;
                if (tlen > 0 && tmp[tlen - 1] == '.') --tlen;
            }
        }
    }

    {
        int total = tlen + (sign_ch ? 1 : 0);
        int pad = width - total;
        if (!fl_left && !fl_zero && pad > 0) sink_pad(sk, ' ', pad);
        if (sign_ch) sink_putc(sk, sign_ch);
        if (!fl_left && fl_zero && pad > 0) sink_pad(sk, '0', pad);
        sink_write(sk, tmp, tlen);
        if (fl_left && pad > 0) sink_pad(sk, ' ', pad);
    }
}

static int format_core(fmt_sink *sk, const char *fmt, va_list ap)
{
    while (*fmt != '\0') {
        int fl_left = 0, fl_plus = 0, fl_space = 0, fl_alt = 0, fl_zero = 0;
        int width = 0, prec = -1;
        int mod_l = 0, mod_h = 0, mod_F = 0;
        char spec;

        if (*fmt != '%') {
            sink_putc(sk, *fmt++);
            continue;
        }
        ++fmt;
        if (*fmt == '%') {
            sink_putc(sk, *fmt++);
            continue;
        }

        /* Flags */
        for (;;) {
            if (*fmt == '-') fl_left = 1;
            else if (*fmt == '+') fl_plus = 1;
            else if (*fmt == ' ') fl_space = 1;
            else if (*fmt == '#') fl_alt = 1;
            else if (*fmt == '0') fl_zero = 1;
            else break;
            ++fmt;
        }

        /* Width */
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                fl_left = 1;
                width = -width;
            }
            ++fmt;
        } else {
            while (isdigit((unsigned char)*fmt)) {
                width = width * 10 + (*fmt - '0');
                ++fmt;
            }
        }

        /* Precision */
        if (*fmt == '.') {
            ++fmt;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                if (prec < 0) prec = -1;
                ++fmt;
            } else {
                prec = 0;
                while (isdigit((unsigned char)*fmt)) {
                    prec = prec * 10 + (*fmt - '0');
                    ++fmt;
                }
            }
        }

        /* Length modifiers */
        while (*fmt == 'l' || *fmt == 'L' || *fmt == 'h' || *fmt == 'F' || *fmt == 'N') {
            if (*fmt == 'l' || *fmt == 'L') mod_l = 1;
            else if (*fmt == 'h') mod_h = 1;
            else if (*fmt == 'F') mod_F = 1;
            ++fmt;
        }

        spec = *fmt;
        if (spec == '\0') break;
        ++fmt;

        if (spec == 'c') {
            char ch = (char)va_arg(ap, int);
            int pad = width - 1;
            if (!fl_left && pad > 0) sink_pad(sk, ' ', pad);
            sink_putc(sk, ch);
            if (fl_left && pad > 0) sink_pad(sk, ' ', pad);
            continue;
        }

        if (spec == 's') {
            if (mod_F) {
                const char __far *fs = va_arg(ap, const char __far *);
                int slen, i, pad;
                if (!fs) fs = "(null)";
                slen = (int)_fstrlen(fs);
                if (prec >= 0 && slen > prec) slen = prec;
                pad = width - slen;
                if (!fl_left && pad > 0) sink_pad(sk, ' ', pad);
                for (i = 0; i < slen; ++i) sink_putc(sk, fs[i]);
                if (fl_left && pad > 0) sink_pad(sk, ' ', pad);
            } else {
                const char *ns = va_arg(ap, const char *);
                int slen, pad;
                if (!ns) ns = "(null)";
                slen = (int)strlen(ns);
                if (prec >= 0 && slen > prec) slen = prec;
                pad = width - slen;
                if (!fl_left && pad > 0) sink_pad(sk, ' ', pad);
                sink_write(sk, ns, slen);
                if (fl_left && pad > 0) sink_pad(sk, ' ', pad);
            }
            continue;
        }

        if (spec == 'n') {
            if (mod_l) {
                long *lp = va_arg(ap, long *);
                if (lp) *lp = (long)sk->pos;
            } else {
                int *ip = va_arg(ap, int *);
                if (ip) *ip = (int)sk->pos;
            }
            continue;
        }

        if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' ||
            spec == 'g' || spec == 'G') {
            double dv = va_arg(ap, double);
            format_float_item(sk, dv, spec, fl_left, fl_plus, fl_space,
                              fl_alt, fl_zero, width, prec);
            continue;
        }

        if (spec == 'd' || spec == 'i' || spec == 'u' ||
            spec == 'o' || spec == 'x' || spec == 'X' || spec == 'p') {
            unsigned long uval;
            int neg = 0;
            unsigned int base = 10U;
            int upper = (spec == 'X');
            char dig[36];
            int ndig = 0, zeros = 0, pfx_len = 0, total, pad;
            char pfx[3];

            if (spec == 'p') {
                if (mod_F) {
                    void __far *fp = va_arg(ap, void __far *);
                    uval = (unsigned long)fp;
                } else {
                    uval = (unsigned long)(unsigned int)va_arg(ap, void *);
                }
                base = 16U;
                upper = 1;
            } else if (spec == 'd' || spec == 'i') {
                long sval;
                if (mod_l) sval = va_arg(ap, long);
                else if (mod_h) sval = (long)(short)va_arg(ap, int);
                else sval = (long)va_arg(ap, int);
                if (sval < 0L) {
                    neg = 1;
                    uval = 0UL - (unsigned long)sval;
                } else {
                    uval = (unsigned long)sval;
                }
            } else {
                if (mod_l) uval = va_arg(ap, unsigned long);
                else if (mod_h) uval = (unsigned long)(unsigned short)va_arg(ap, unsigned int);
                else uval = (unsigned long)va_arg(ap, unsigned int);
                if (spec == 'o') base = 8U;
                else if (spec == 'x' || spec == 'X') base = 16U;
            }

            if (spec == 'd' || spec == 'i') {
                if (neg) pfx[pfx_len++] = '-';
                else if (fl_plus) pfx[pfx_len++] = '+';
                else if (fl_space) pfx[pfx_len++] = ' ';
            } else if (fl_alt && uval != 0UL) {
                if (base == 8U) {
                    pfx[pfx_len++] = '0';
                } else if (base == 16U) {
                    pfx[pfx_len++] = '0';
                    pfx[pfx_len++] = upper ? 'X' : 'x';
                }
            }

            if (!(uval == 0UL && prec == 0)) {
                do {
                    unsigned int rem = (unsigned int)(uval % (unsigned long)base);
                    dig[ndig++] = (char)(rem < 10U ? ('0' + rem)
                                                   : ((upper ? 'A' : 'a') + (rem - 10U)));
                    uval /= (unsigned long)base;
                } while (uval != 0UL);
            }

            if (prec >= 0) {
                fl_zero = 0;
                if (prec > ndig) zeros = prec - ndig;
            }

            total = pfx_len + zeros + ndig;
            pad = width - total;
            if (!fl_left && !fl_zero && pad > 0) sink_pad(sk, ' ', pad);
            sink_write(sk, pfx, pfx_len);
            if (!fl_left && fl_zero && pad > 0) sink_pad(sk, '0', pad);
            sink_pad(sk, '0', zeros);
            while (ndig > 0) sink_putc(sk, dig[--ndig]);
            if (fl_left && pad > 0) sink_pad(sk, ' ', pad);
            continue;
        }

        sink_putc(sk, spec);
    }

    if (sk->stream == NULL && sk->buf != NULL) {
        if (!sk->has_cap) {
            sk->buf[sk->pos] = '\0';
        } else if (sk->cap > 0U) {
            size_t term = (sk->pos < sk->cap) ? sk->pos : (sk->cap - 1U);
            sk->buf[term] = '\0';
        }
    }
    return sk->err ? -1 : (int)sk->pos;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    fmt_sink sk;
    sk.buf = buf;
    sk.cap = 0U;
    sk.pos = 0U;
    sk.has_cap = 0;
    sk.stream = NULL;
    sk.err = 0;
    return format_core(&sk, fmt, ap);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    fmt_sink sk;
    sk.buf = buf;
    sk.cap = size;
    sk.pos = 0U;
    sk.has_cap = 1;
    sk.stream = NULL;
    sk.err = 0;
    return format_core(&sk, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
    int rc;
    va_list ap;
    va_start(ap, fmt);
    rc = vsprintf(buf, fmt, ap);
    va_end(ap);
    return rc;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    int rc;
    va_list ap;
    va_start(ap, fmt);
    rc = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return rc;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    fmt_sink sk;
    if (!stream) return -1;
    sk.buf = NULL;
    sk.cap = 0U;
    sk.pos = 0U;
    sk.has_cap = 0;
    sk.stream = stream;
    sk.err = 0;
    return format_core(&sk, fmt, ap);
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    int rc;
    va_list ap;
    va_start(ap, fmt);
    rc = vfprintf(stream, fmt, ap);
    va_end(ap);
    return rc;
}

int printf(const char *fmt, ...)
{
    int rc;
    va_list ap;
    va_start(ap, fmt);
    rc = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return rc;
}

/* --- sscanf / fscanf / scanf --- */

static int vsscanf_str(const char *str, const char *fmt, va_list ap)
{
    const char *s = str;
    int assigned = 0;

    while (*fmt != '\0') {
        int suppress = 0, width = 0, mod_l = 0, mod_h = 0;
        char spec;

        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*fmt)) ++fmt;
            while (isspace((unsigned char)*s)) ++s;
            continue;
        }
        if (*fmt != '%') {
            if (*s != *fmt) break;
            ++s;
            ++fmt;
            continue;
        }
        ++fmt;
        if (*fmt == '%') {
            if (*s != '%') break;
            ++s;
            ++fmt;
            continue;
        }
        if (*fmt == '*') {
            suppress = 1;
            ++fmt;
        }
        while (isdigit((unsigned char)*fmt)) {
            width = width * 10 + (*fmt - '0');
            ++fmt;
        }
        if (*fmt == 'l' || *fmt == 'L') { mod_l = 1; ++fmt; }
        else if (*fmt == 'h') { mod_h = 1; ++fmt; }

        spec = *fmt;
        if (spec == '\0') break;
        ++fmt;

        if (spec != 'c' && spec != 'n') {
            while (isspace((unsigned char)*s)) ++s;
            if (*s == '\0') return assigned ? assigned : EOF;
        }

        if (spec == 'n') {
            if (!suppress) {
                if (mod_l) *va_arg(ap, long *) = (long)(s - str);
                else *va_arg(ap, int *) = (int)(s - str);
            }
            continue;
        }

        if (spec == 'c') {
            int n = (width > 0) ? width : 1;
            int i;
            char *dst = suppress ? NULL : va_arg(ap, char *);
            for (i = 0; i < n; ++i) {
                if (*s == '\0') return assigned ? assigned : EOF;
                if (dst) dst[i] = *s;
                ++s;
            }
            if (!suppress) ++assigned;
            continue;
        }

        if (spec == 's') {
            int limit = (width > 0) ? width : 32767;
            int i = 0;
            char *dst = suppress ? NULL : va_arg(ap, char *);
            while (*s != '\0' && !isspace((unsigned char)*s) && i < limit) {
                if (dst) dst[i] = *s;
                ++s;
                ++i;
            }
            if (i == 0) break;
            if (dst) dst[i] = '\0';
            if (!suppress) ++assigned;
            continue;
        }

        if (spec == 'd' || spec == 'i' || spec == 'u' ||
            spec == 'o' || spec == 'x' || spec == 'X') {
            char tmp[80];
            int limit = (width > 0 && width < 78) ? width : 78;
            int i = 0;
            char *endp = NULL;
            int base = (spec == 'd' || spec == 'u') ? 10
                     : (spec == 'o') ? 8
                     : (spec == 'x' || spec == 'X') ? 16 : 0;

            while (s[i] != '\0' && !isspace((unsigned char)s[i]) && i < limit) {
                tmp[i] = s[i];
                ++i;
            }
            tmp[i] = '\0';
            if (spec == 'd' || spec == 'i') {
                long v = strtol(tmp, &endp, base);
                if (endp == tmp) break;
                s += (endp - tmp);
                if (!suppress) {
                    if (mod_l) *va_arg(ap, long *) = v;
                    else if (mod_h) *va_arg(ap, short *) = (short)v;
                    else *va_arg(ap, int *) = (int)v;
                    ++assigned;
                }
            } else {
                unsigned long v = strtoul(tmp, &endp, base);
                if (endp == tmp) break;
                s += (endp - tmp);
                if (!suppress) {
                    if (mod_l) *va_arg(ap, unsigned long *) = v;
                    else if (mod_h) *va_arg(ap, unsigned short *) = (unsigned short)v;
                    else *va_arg(ap, unsigned int *) = (unsigned int)v;
                    ++assigned;
                }
            }
            continue;
        }

        if (spec == 'f' || spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G') {
            char tmp[128];
            int limit = (width > 0 && width < 126) ? width : 126;
            int i = 0;
            char *endp = NULL;
            double dv;
            while (s[i] != '\0' && !isspace((unsigned char)s[i]) && i < limit) {
                tmp[i] = s[i];
                ++i;
            }
            tmp[i] = '\0';
            dv = strtod(tmp, &endp);
            if (endp == tmp) break;
            s += (endp - tmp);
            if (!suppress) {
                if (mod_l) *va_arg(ap, double *) = dv;
                else *va_arg(ap, float *) = (float)dv;
                ++assigned;
            }
            continue;
        }
        break;
    }
    return assigned;
}

int sscanf(const char *str, const char *fmt, ...)
{
    int rc;
    va_list ap;
    va_start(ap, fmt);
    rc = vsscanf_str(str, fmt, ap);
    va_end(ap);
    return rc;
}

int fscanf(FILE *stream, const char *fmt, ...)
{
    char line[512];
    int rc;
    va_list ap;
    if (!fgets(line, sizeof(line), stream)) return EOF;
    va_start(ap, fmt);
    rc = vsscanf_str(line, fmt, ap);
    va_end(ap);
    return rc;
}

int scanf(const char *fmt, ...)
{
    char line[512];
    int rc;
    va_list ap;
    if (!fgets(line, sizeof(line), stdin)) return EOF;
    va_start(ap, fmt);
    rc = vsscanf_str(line, fmt, ap);
    va_end(ap);
    return rc;
}
