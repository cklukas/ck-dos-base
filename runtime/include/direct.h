#ifndef _CKCL_DIRECT_H
#define _CKCL_DIRECT_H

#include <stddef.h>

char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int mkdir(const char *path);
int rmdir(const char *path);

#endif
