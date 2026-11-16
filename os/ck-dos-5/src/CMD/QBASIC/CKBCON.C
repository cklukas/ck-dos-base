/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include "CKBCON.H"

#if defined(__WATCOMC__) || defined(_MSDOS)
#include <dos.h>
#ifdef __WATCOMC__
#include <i86.h>
#else
#include <dos.h>
#endif
#include <io.h>
#endif

/* COLOR is retained as interpreter state. PRINT currently uses DOS handles,
 * so it does not yet apply this attribute to characters written to a console. */
static unsigned foreground = 7;
static unsigned background = 0;

void ckb_color(int fg, int bg)
{
    if (fg >= 0) foreground = (unsigned)fg;
    if (bg >= 0) background = (unsigned)bg;
}

void ckb_cls(void)
{
#if defined(__WATCOMC__) || defined(_MSDOS)
    union REGS regs = {0};
    unsigned page, columns, rows;
    if (!isatty(1)) return;
    regs.h.ah = 0x0f;
    int86(0x10, &regs, &regs);
    page = regs.h.bh;
    columns = regs.h.ah;
    rows = *(const unsigned char __far *)MK_FP(0x40, 0x84) + 1U;
    if (!columns || rows < 2 || rows > 60) rows = 25;
    if (!columns) columns = 80;
    regs.x.ax = 0x0600;
    regs.h.bh = (unsigned char)((background << 4) | (foreground & 15U) |
                                  ((foreground & 16U) ? 0x80U : 0));
    regs.x.cx = 0;
    regs.h.dh = (unsigned char)(rows - 1);
    regs.h.dl = (unsigned char)(columns - 1);
    int86(0x10, &regs, &regs);
    regs.h.ah = 2;
    regs.h.bh = (unsigned char)page;
    regs.x.dx = 0;
    int86(0x10, &regs, &regs);
#else
    (void)foreground;
    (void)background;
#endif
}
