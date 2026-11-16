#ifndef _CKCL_I86_H
#define _CKCL_I86_H

struct WORDREGS {
    unsigned int ax;
    unsigned int bx;
    unsigned int cx;
    unsigned int dx;
    unsigned int si;
    unsigned int di;
    unsigned int cflag;
};

struct BYTEREGS {
    unsigned char al, ah;
    unsigned char bl, bh;
    unsigned char cl, ch;
    unsigned char dl, dh;
};

union REGS {
    struct WORDREGS x;
    struct WORDREGS w;
    struct BYTEREGS h;
};

struct SREGS {
    unsigned int es;
    unsigned int cs;
    unsigned int ss;
    unsigned int ds;
};

#ifndef MK_FP
#define MK_FP(seg, off) ((void __far *)(((unsigned long)(unsigned int)(seg) << 16) | (unsigned int)(off)))
#endif
#ifndef FP_SEG
#define FP_SEG(fp) ((unsigned int)((unsigned long)(void __far *)(fp) >> 16))
#endif
#ifndef FP_OFF
#define FP_OFF(fp) ((unsigned int)(unsigned long)(void __far *)(fp))
#endif

int int86(int intno, const union REGS *inregs, union REGS *outregs);
int int86x(int intno, const union REGS *inregs, union REGS *outregs, struct SREGS *sregs);
int intdos(const union REGS *inregs, union REGS *outregs);
int intdosx(const union REGS *inregs, union REGS *outregs, struct SREGS *sregs);
void segread(struct SREGS *sregs);
void _enable(void);
void _disable(void);

#endif
