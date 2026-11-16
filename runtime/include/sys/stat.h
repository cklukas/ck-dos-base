#ifndef _CKCL_SYS_STAT_H
#define _CKCL_SYS_STAT_H

#include <sys/types.h>

#define S_IFMT   0xF000
#define S_IFDIR  0x4000
#define S_IFCHR  0x2000
#define S_IFREG  0x8000

#define S_IREAD  0x0100
#define S_IWRITE 0x0080
#define S_IEXEC  0x0040

#define S_IRUSR  S_IREAD
#define S_IWUSR  S_IWRITE
#define S_IXUSR  S_IEXEC

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    unsigned short st_mode;
    short st_nlink;
    short st_uid;
    short st_gid;
    dev_t st_rdev;
    off_t st_size;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);

#endif
