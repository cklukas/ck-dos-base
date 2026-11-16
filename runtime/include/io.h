#ifndef _CKCL_IO_H
#define _CKCL_IO_H

int open(const char *path, int oflag, ...);
int sopen(const char *path, int oflag, int shflag, ...);
int creat(const char *path, int mode);
int close(int fd);
int read(int fd, void *buf, unsigned int count);
int write(int fd, const void *buf, unsigned int count);
long lseek(int fd, long offset, int origin);
long filelength(int fd);
int isatty(int fd);
int access(const char *path, int mode);
int unlink(const char *path);
int lock(int fd, unsigned long offset, unsigned long length);
int unlock(int fd, unsigned long offset, unsigned long length);

#endif
