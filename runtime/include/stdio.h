#ifndef _CKCL_STDIO_H
#define _CKCL_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef long fpos_t;

typedef struct _iobuf {
    char *_ptr;
    int _cnt;
    char *_base;
    unsigned int _flag;
    int _file;
    unsigned int _bufsiz;
    int _ungot;
} FILE;

#define EOF          (-1)
#define BUFSIZ       512
#define FILENAME_MAX 144
#define FOPEN_MAX    20
#define L_tmpnam     16

#define _IOFBF       0x0000
#define _IOLBF       0x0040
#define _IONBF       0x0004

#define SEEK_SET     0
#define SEEK_CUR     1
#define SEEK_END     2

extern FILE _iob[20];

#define stdin  (&_iob[0])
#define stdout (&_iob[1])
#define stderr (&_iob[2])

FILE *fopen(const char *path, const char *mode);
FILE *_fsopen(const char *path, const char *mode, int shflag);
FILE *freopen(const char *path, const char *mode, FILE *stream);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
void setbuf(FILE *stream, char *buf);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fgetc(FILE *stream);
char *fgets(char *s, int n, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int getc(FILE *stream);
int getchar(void);
char *gets(char *s);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);
int ungetc(int c, FILE *stream);

int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void perror(const char *s);
int fileno(FILE *stream);
int _fileno(FILE *stream);

int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
FILE *tmpfile(void);
char *tmpnam(char *s);

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);
int fscanf(FILE *stream, const char *fmt, ...);
int scanf(const char *fmt, ...);

#endif
