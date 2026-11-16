#ifndef _CKCL_DOS_H
#define _CKCL_DOS_H

#include <i86.h>

extern unsigned int _psp;
extern unsigned char _osmajor;
extern unsigned char _osminor;
extern int _doserrno;

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_VOLID  0x08
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20

struct find_t {
    char reserved[21];
    unsigned char attrib;
    unsigned short wr_time;
    unsigned short wr_date;
    unsigned long size;
    char name[13];
};

#define _find_t find_t

unsigned int _dos_open(const char *path, unsigned int mode, int *handle);
unsigned int _dos_creat(const char *path, unsigned int attr, int *handle);
unsigned int _dos_creatnew(const char *path, unsigned int attr, int *handle);
unsigned int _dos_close(int handle);
unsigned int _dos_read(int handle, void __far *buf, unsigned int count, unsigned int *bytes_read);
unsigned int _dos_write(int handle, const void __far *buf, unsigned int count, unsigned int *bytes_written);
unsigned int _dos_allocmem(unsigned int size_paras, unsigned int *segment);
unsigned int _dos_freemem(unsigned int segment);
unsigned int _dos_setblock(unsigned int size_paras, unsigned int segment, unsigned int *max_paras);
unsigned int _dos_findfirst(const char *path, unsigned int attr, struct find_t *fileinfo);
unsigned int _dos_findnext(struct find_t *fileinfo);
void _dos_getdrive(unsigned int *drive);
void _dos_setdrive(unsigned int drive, unsigned int *total);
void (__interrupt __far *_dos_getvect(unsigned int intnum))();
void _dos_setvect(unsigned int intnum, void (__interrupt __far *handler)());

#endif
