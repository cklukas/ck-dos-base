#ifndef _CKCL_STDLIB_H
#define _CKCL_STDLIB_H

#include <stddef.h>

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX     32767

#define _MAX_PATH    144
#define _MAX_DRIVE   3
#define _MAX_DIR     130
#define _MAX_FNAME   9
#define _MAX_EXT     5

double atof(const char *s);
int atoi(const char *s);
long atol(const char *s);
double strtod(const char *s, char **endptr);
long strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);

int rand(void);
void srand(unsigned int seed);

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

void abort(void);
int atexit(void (*func)(void));
void exit(int status);
void _exit(int status);

char *getenv(const char *name);
int putenv(const char *entry);
int system(const char *command);

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));

int abs(int x);
long labs(long x);
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);

char *itoa(int val, char *buf, int radix);
char *ltoa(long val, char *buf, int radix);
char *ultoa(unsigned long val, char *buf, int radix);
char *_fullpath(char *buffer, const char *path, size_t maxlen);

#endif
