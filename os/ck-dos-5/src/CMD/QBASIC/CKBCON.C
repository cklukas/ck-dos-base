/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include "CKBCON.H"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__WATCOMC__) || defined(_MSDOS)
#include <conio.h>
#include <dos.h>
#include <io.h>
#include <malloc.h>
#ifdef __WATCOMC__
#include <i86.h>
#endif

extern unsigned __far input_key(void);
#ifdef __WATCOMC__
#pragma aux input_key "_input_key" value [ax] modify exact [ax]
#endif

static int screen_mode = 0;
static int screen_cols = 80;
static int screen_rows = 25;
static int gfx_width = 0;
static int gfx_height = 0;
static int max_color = 15;
static int fg_color = 7;
static int bg_color = 0;
static unsigned char text_attr = 0x07;
static int cur_row = 1;
static int cur_col = 1;
static int cursor_synced = 0;
static int cursor_visible = 1;
static int cached_tty = -1;
static int pending_wrap = 0;
static int view_top = 1;
static int view_bottom = 25;
static int direct_console = 0;
static int last_x = 0;
static int last_y = 0;

/* Persistent MML PLAY state (reset at start of each program run). */
static int play_octave = 4;
static int play_length = 4;
static int play_tempo = 120;
static int play_style = 0; /* 0 = MN (7/8), 1 = ML (8/8), 2 = MS (3/4) */
static int play_bg = 0;

/* Timer pacing state so tight TIMER loops calibrate consistently across CPUs. */
static int timer_has_prev = 0;
static unsigned long timer_prev_total = 0;

#define TWO_PI 6.28318530717958647692

static int tty_console(void)
{
    if (cached_tty < 0) cached_tty = isatty(1) != 0;
    return cached_tty;
}

static void sync_bios_cursor_from_bda(void)
{
    union REGS regs = {0};
    regs.h.ah = 0x03;
    regs.h.bh = 0;
    int86(0x10, &regs, &regs);
    cur_row = (int)regs.h.dh + 1;
    cur_col = (int)regs.h.dl + 1;
    if (cur_row < 1) cur_row = 1;
    if (cur_row > screen_rows) cur_row = screen_rows;
    if (cur_col < 1) cur_col = 1;
    if (cur_col > screen_cols) cur_col = screen_cols;
    cursor_synced = 1;
    pending_wrap = 0;
}

static void set_bios_cursor_hw(int row, int col)
{
    union REGS regs = {0};
    if (row < 1) row = 1;
    if (row > screen_rows) row = screen_rows;
    if (col < 1) col = 1;
    if (col > screen_cols) col = screen_cols;
    regs.h.ah = 0x02;
    regs.h.bh = 0;
    regs.h.dh = (unsigned char)(row - 1);
    regs.h.dl = (unsigned char)(col - 1);
    int86(0x10, &regs, &regs);
}

static void set_bios_cursor(int row, int col)
{
    if (row < 1) row = 1;
    if (row > screen_rows) row = screen_rows;
    if (col < 1) col = 1;
    if (col > screen_cols) col = screen_cols;
    if (screen_mode == 0 && !cursor_visible) {
        *(volatile unsigned __far *)MK_FP(0x40, 0x50) =
            (unsigned)(((unsigned)(row - 1) << 8) | (unsigned)(col - 1));
        return;
    }
    set_bios_cursor_hw(row, col);
}

void ckb_con_sync_cursor_hw(void)
{
    if (tty_console() && cursor_synced) set_bios_cursor_hw(cur_row, cur_col);
}

void ckb_con_restore_cursor(void)
{
    if (tty_console() && screen_mode == 0) {
        union REGS regs = {0};
        regs.h.ah = 0x01;
        regs.x.cx = cursor_visible ? 0x0607 : 0x2607;
        int86(0x10, &regs, &regs);
    }
}

static void reset_planar_gc(void)
{
    outp(0x3ce, 0x05);
    outp(0x3cf, 0x00);
    outp(0x3ce, 0x08);
    outp(0x3cf, 0xff);
    outp(0x3ce, 0x03);
    outp(0x3cf, 0x00);
    outp(0x3ce, 0x04);
    outp(0x3cf, 0x00);
}

void ckb_speaker_off(void)
{
    unsigned v = (unsigned)inp(0x61);
    outp(0x61, (int)(v & ~3U));
}

void ckb_con_reset_run(void)
{
    ckb_speaker_off();
    screen_mode = 0;
    screen_cols = 80;
    screen_rows = 25;
    gfx_width = 0;
    gfx_height = 0;
    max_color = 15;
    fg_color = 7;
    bg_color = 0;
    text_attr = 0x07;
    cur_row = 1;
    cur_col = 1;
    cursor_synced = 0;
    cursor_visible = 1;
    cached_tty = -1;
    pending_wrap = 0;
    view_top = 1;
    view_bottom = 25;
    direct_console = 0;
    last_x = 0;
    last_y = 0;
    play_octave = 4;
    play_length = 4;
    play_tempo = 120;
    play_style = 0;
    play_bg = 0;
    timer_has_prev = 0;
}

void ckb_con_reset_text(void)
{
    union REGS regs = {0};
    ckb_speaker_off();
    if ((*(const unsigned char __far *)MK_FP(0x40, 0x49) & 0x7f) != 3) {
        regs.x.ax = 0x0003;
        int86(0x10, &regs, &regs);
    } else if (!cursor_visible && tty_console()) {
        regs.h.ah = 0x01;
        regs.x.cx = 0x0607;
        int86(0x10, &regs, &regs);
    }
    ckb_con_reset_run();
}

void ckb_con_acknowledge_graphics(void)
{
    const char *msg = "Press any key to continue";
    ckb_speaker_off();
    view_top = 1;
    view_bottom = screen_rows;
    ckb_locate(screen_rows, 1, -1);
    ckb_con_write(msg, (unsigned)strlen(msg));
    while (input_key() == 0xffffU) {
    }
}

const char *ckb_screen(int mode)
{
    union REGS regs = {0};
    int bios_mode = 0x03;
    switch (mode) {
    case 0:
        bios_mode = screen_cols == 40 ? 0x01 : 0x03;
        screen_rows = 25;
        gfx_width = 0;
        gfx_height = 0;
        max_color = 15;
        fg_color = 7;
        bg_color = 0;
        break;
    case 1:
        bios_mode = 0x04;
        screen_cols = 40;
        screen_rows = 25;
        gfx_width = 320;
        gfx_height = 200;
        max_color = 3;
        fg_color = 3;
        bg_color = 0;
        break;
    case 2:
        bios_mode = 0x06;
        screen_cols = 80;
        screen_rows = 25;
        gfx_width = 640;
        gfx_height = 200;
        max_color = 1;
        fg_color = 1;
        bg_color = 0;
        break;
    case 7:
        bios_mode = 0x0d;
        screen_cols = 40;
        screen_rows = 25;
        gfx_width = 320;
        gfx_height = 200;
        max_color = 15;
        fg_color = 15;
        bg_color = 0;
        break;
    case 8:
        bios_mode = 0x0e;
        screen_cols = 80;
        screen_rows = 25;
        gfx_width = 640;
        gfx_height = 200;
        max_color = 15;
        fg_color = 15;
        bg_color = 0;
        break;
    case 9:
        bios_mode = 0x10;
        screen_cols = 80;
        screen_rows = 25;
        gfx_width = 640;
        gfx_height = 350;
        max_color = 15;
        fg_color = 15;
        bg_color = 0;
        break;
    case 12:
        bios_mode = 0x12;
        screen_cols = 80;
        screen_rows = 30;
        gfx_width = 640;
        gfx_height = 480;
        max_color = 15;
        fg_color = 15;
        bg_color = 0;
        break;
    case 13:
        bios_mode = 0x13;
        screen_cols = 40;
        screen_rows = 25;
        gfx_width = 320;
        gfx_height = 200;
        max_color = 255;
        fg_color = 15;
        bg_color = 0;
        break;
    default:
        return "Illegal function call";
    }
    if (!direct_console) fflush(stdout);
    screen_mode = mode;
    text_attr = (unsigned char)(((bg_color & 7) << 4) | (fg_color & 15));
    view_top = 1;
    view_bottom = screen_rows;
    cur_row = 1;
    cur_col = 1;
    cursor_synced = 1;
    cursor_visible = 0;
    pending_wrap = 0;
    last_x = gfx_width / 2;
    last_y = gfx_height / 2;
    if (mode != 0 || tty_console()) {
        direct_console = 1;
        regs.x.ax = (unsigned)bios_mode;
        int86(0x10, &regs, &regs);
        if (mode == 0) {
            regs.h.ah = 0x01;
            regs.x.cx = 0x2607;
            int86(0x10, &regs, &regs);
        }
    }
    return NULL;
}

const char *ckb_width_screen(int cols, int rows)
{
    if (cols != -1 && cols != 40 && cols != 80) return "Illegal function call";
    if (rows != -1 && rows != 25 && rows != 30 && rows != 43 && rows != 50)
        return "Illegal function call";
    if (cols != -1) screen_cols = cols;
    if (rows != -1 && screen_mode == 0) screen_rows = 25;
    if (screen_mode == 0 && tty_console()) {
        union REGS regs = {0};
        direct_console = 1;
        regs.x.ax = screen_cols == 40 ? 0x0001 : 0x0003;
        int86(0x10, &regs, &regs);
        if (!cursor_visible) {
            regs.h.ah = 0x01;
            regs.x.cx = 0x2607;
            int86(0x10, &regs, &regs);
        }
        cur_row = 1;
        cur_col = 1;
        cursor_synced = 1;
        pending_wrap = 0;
        view_top = 1;
        view_bottom = screen_rows;
    }
    return NULL;
}

const char *ckb_palette(int attr, long color)
{
    union REGS regs = {0};
    if (screen_mode == 0 || screen_mode == 1 || screen_mode == 2)
        return "Illegal function call";
    if (attr < 0 || attr > max_color || color < -1)
        return "Illegal function call";
    if (color == -1 || !tty_console()) return NULL;
    if (screen_mode == 13) {
        outp(0x3c8, attr);
        outp(0x3c9, (int)(color & 0x3f));
        outp(0x3c9, (int)((color >> 8) & 0x3f));
        outp(0x3c9, (int)((color >> 16) & 0x3f));
        return NULL;
    }
    regs.x.ax = 0x1000;
    regs.h.bl = (unsigned char)attr;
    regs.h.bh = (unsigned char)(color & 0x3f);
    int86(0x10, &regs, &regs);
    return NULL;
}

void ckb_color(int foreground, int background)
{
    if (screen_mode == 1) {
        /* In SCREEN 1: COLOR background, palette */
        if (foreground >= 0) {
            union REGS regs = {0};
            bg_color = foreground & 15;
            if (tty_console()) {
                regs.h.ah = 0x0b;
                regs.h.bh = 0x00;
                regs.h.bl = (unsigned char)bg_color;
                int86(0x10, &regs, &regs);
            }
        }
        if (background >= 0 && tty_console()) {
            union REGS regs = {0};
            regs.h.ah = 0x0b;
            regs.h.bh = 0x01;
            regs.h.bl = (unsigned char)(background & 1);
            int86(0x10, &regs, &regs);
        }
        return;
    }
    if (screen_mode != 0) {
        if (foreground >= 0) fg_color = foreground & max_color;
        if (background >= 0) bg_color = background & 15;
        return;
    }
    if (foreground >= 0) {
        fg_color = foreground & 31;
        text_attr = (unsigned char)((text_attr & 0x70) | (foreground & 15) |
                                    (foreground >= 16 ? 0x80 : 0));
    }
    if (background >= 0) {
        bg_color = background & 7;
        text_attr = (unsigned char)((text_attr & 0x8f) | ((background & 7) << 4));
    }
    if (tty_console()) {
        if (!cursor_synced) sync_bios_cursor_from_bda();
        direct_console = 1;
    }
}

static void clear_text_rows(int top_row, int bottom_row)
{
    unsigned __far *vram = (unsigned __far *)MK_FP(0xb800, 0);
    unsigned cell = ((unsigned)text_attr << 8) | ' ';
    int r, c;
    for (r = top_row - 1; r < bottom_row; ++r) {
        unsigned base = (unsigned)r * (unsigned)screen_cols;
        for (c = 0; c < screen_cols; ++c)
            vram[base + (unsigned)c] = cell;
    }
}

void ckb_cls_mode(int mode)
{
    union REGS regs = {0};
    if (!direct_console) fflush(stdout);
    if (!tty_console()) return;
    direct_console = 1;
    if (screen_mode == 0) {
        int top = mode == 2 ? view_top : 1;
        int bottom = mode == 2 ? view_bottom : screen_rows;
        clear_text_rows(top, bottom);
        cur_row = top;
        cur_col = 1;
        cursor_synced = 1;
        pending_wrap = 0;
        set_bios_cursor(cur_row, cur_col);
        return;
    }
    if (mode == 2) {
        /* Clear text viewport rows only */
        regs.h.ah = 0x06;
        regs.h.al = 0;
        regs.h.bh = 0;
        regs.h.ch = (unsigned char)(view_top - 1);
        regs.h.cl = 0;
        regs.h.dh = (unsigned char)(view_bottom - 1);
        regs.h.dl = (unsigned char)(screen_cols - 1);
        int86(0x10, &regs, &regs);
        cur_row = view_top;
        cur_col = 1;
        pending_wrap = 0;
        set_bios_cursor(cur_row, cur_col);
        return;
    }
    /* Clear full graphics screen */
    (void)ckb_line(1, 0, 0, gfx_width - 1, gfx_height - 1, 0, 2);
    last_x = gfx_width / 2;
    last_y = gfx_height / 2;
    if (mode != 1) {
        cur_row = view_top;
        cur_col = 1;
        pending_wrap = 0;
        set_bios_cursor(cur_row, cur_col);
    }
}

void ckb_cls(void)
{
    ckb_cls_mode(-1);
}

const char *ckb_locate(int row, int col, int cursor)
{
    if (!direct_console) fflush(stdout);
    if (row != -1 && (row < 1 || row > screen_rows))
        return "Illegal function call";
    if (col != -1 && (col < 1 || col > screen_cols))
        return "Illegal function call";
    if (tty_console() && !cursor_synced) sync_bios_cursor_from_bda();
    if (row != -1) cur_row = row;
    if (col != -1) cur_col = col;
    pending_wrap = 0;
    if (tty_console()) {
        direct_console = 1;
        cursor_synced = 1;
        if (cursor != -1 && screen_mode == 0) {
            union REGS regs = {0};
            cursor_visible = cursor != 0;
            if (cursor_visible) set_bios_cursor_hw(cur_row, cur_col);
            regs.h.ah = 0x01;
            regs.x.cx = cursor_visible ? 0x0607 : 0x2607;
            int86(0x10, &regs, &regs);
        }
        set_bios_cursor(cur_row, cur_col);
    }
    return NULL;
}

const char *ckb_view_print(int top, int bottom)
{
    if (top == -1 && bottom == -1) {
        view_top = 1;
        view_bottom = screen_rows;
    } else {
        if (top < 1 || bottom > screen_rows || top > bottom)
            return "Illegal function call";
        view_top = top;
        view_bottom = bottom;
    }
    cur_row = view_top;
    cur_col = 1;
    pending_wrap = 0;
    if (tty_console()) {
        direct_console = 1;
        cursor_synced = 1;
        set_bios_cursor(cur_row, cur_col);
    }
    return NULL;
}

int ckb_con_direct_active(void)
{
    return (screen_mode != 0 || direct_console) && tty_console();
}

unsigned ckb_con_column(void)
{
    if (tty_console() && !cursor_synced) sync_bios_cursor_from_bda();
    return pending_wrap ? 0U : (unsigned)(cur_col - 1);
}

unsigned ckb_con_width(void)
{
    return (unsigned)screen_cols;
}

static void scroll_viewport_up(void)
{
    if (screen_mode == 0) {
        unsigned __far *vram = (unsigned __far *)MK_FP(0xb800, 0);
        unsigned blank = ((unsigned)text_attr << 8) | ' ';
        int r, c;
        for (r = view_top - 1; r < view_bottom - 1; ++r) {
            unsigned dst = (unsigned)r * (unsigned)screen_cols;
            unsigned src = (unsigned)(r + 1) * (unsigned)screen_cols;
            for (c = 0; c < screen_cols; ++c) vram[dst + (unsigned)c] = vram[src + (unsigned)c];
        }
        {
            unsigned dst = (unsigned)(view_bottom - 1) * (unsigned)screen_cols;
            for (c = 0; c < screen_cols; ++c) vram[dst + (unsigned)c] = blank;
        }
    } else {
        union REGS regs = {0};
        regs.h.ah = 0x06;
        regs.h.al = 1;
        regs.h.bh = 0;
        regs.h.ch = (unsigned char)(view_top - 1);
        regs.h.cl = 0;
        regs.h.dh = (unsigned char)(view_bottom - 1);
        regs.h.dl = (unsigned char)(screen_cols - 1);
        int86(0x10, &regs, &regs);
    }
}

static void advance_line(void)
{
    cur_col = 1;
    if (cur_row == view_bottom) scroll_viewport_up();
    else if (cur_row < screen_rows) ++cur_row;
}

static void put_console_char(unsigned char ch)
{
    if (ch == '\r') {
        cur_col = 1;
        pending_wrap = 0;
        return;
    }
    if (ch == '\n') {
        pending_wrap = 0;
        advance_line();
        return;
    }
    if (ch == '\b') {
        pending_wrap = 0;
        if (cur_col > 1) --cur_col;
        return;
    }
    if (ch == 7) {
        ckb_beep();
        return;
    }
    if (pending_wrap) {
        pending_wrap = 0;
        advance_line();
    }
    if (screen_mode == 0) {
        unsigned __far *vram = (unsigned __far *)MK_FP(0xb800, 0);
        unsigned idx = (unsigned)(cur_row - 1) * (unsigned)screen_cols + (unsigned)(cur_col - 1);
        vram[idx] = ((unsigned)text_attr << 8) | ch;
    } else {
        union REGS regs = {0};
        set_bios_cursor(cur_row, cur_col);
        regs.h.ah = 0x09;
        regs.h.al = ch;
        regs.h.bh = 0;
        regs.h.bl = (unsigned char)(fg_color & max_color);
        regs.x.cx = 1;
        int86(0x10, &regs, &regs);
    }
    if (cur_col >= screen_cols) pending_wrap = 1;
    else ++cur_col;
}

void ckb_con_write(const char *text, unsigned length)
{
    unsigned i;
    if (!cursor_synced) sync_bios_cursor_from_bda();
    for (i = 0; i < length; ++i)
        put_console_char((unsigned char)text[i]);
    set_bios_cursor(cur_row, cur_col);
}

void ckb_con_newline(void)
{
    if (!cursor_synced) sync_bios_cursor_from_bda();
    pending_wrap = 0;
    advance_line();
    set_bios_cursor(cur_row, cur_col);
}

void ckb_gfx_last(int *x, int *y)
{
    *x = last_x;
    *y = last_y;
}

int ckb_gfx_default_color(int is_preset)
{
    return is_preset ? 0 : fg_color;
}

static void raw_put_pixel(int x, int y, int color)
{
    unsigned char c;
    if (x < 0 || x >= gfx_width || y < 0 || y >= gfx_height) return;
    c = (unsigned char)(color & max_color);
    switch (screen_mode) {
    case 1: {
        unsigned char __far *vram = (unsigned char __far *)MK_FP(0xb800, (y & 1) ? 0x2000 : 0);
        unsigned off = (unsigned)(y >> 1) * 80U + (unsigned)(x >> 2);
        unsigned shift = (unsigned)(6 - ((x & 3) << 1));
        vram[off] = (unsigned char)((vram[off] & ~(3U << shift)) | ((unsigned)(c & 3) << shift));
        break;
    }
    case 2: {
        unsigned char __far *vram = (unsigned char __far *)MK_FP(0xb800, (y & 1) ? 0x2000 : 0);
        unsigned off = (unsigned)(y >> 1) * 80U + (unsigned)(x >> 3);
        unsigned bit = (unsigned)(0x80U >> (x & 7));
        if (c & 1) vram[off] |= (unsigned char)bit;
        else vram[off] &= (unsigned char)~bit;
        break;
    }
    case 7:
    case 8:
    case 9:
    case 12: {
        volatile unsigned char __far *vram = (volatile unsigned char __far *)MK_FP(0xa000, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned off = (unsigned)y * stride + (unsigned)(x >> 3);
        unsigned char mask = (unsigned char)(0x80U >> (x & 7));
        volatile unsigned char latch;
        outp(0x3ce, 0x05);
        outp(0x3cf, 0x02);
        outp(0x3ce, 0x08);
        outp(0x3cf, mask);
        latch = vram[off];
        (void)latch;
        vram[off] = c;
        reset_planar_gc();
        break;
    }
    case 13: {
        unsigned char __far *vram = (unsigned char __far *)MK_FP(0xa000, 0);
        vram[(unsigned)y * 320U + (unsigned)x] = c;
        break;
    }
    }
}

static int raw_get_pixel(int x, int y)
{
    if (x < 0 || x >= gfx_width || y < 0 || y >= gfx_height) return -1;
    switch (screen_mode) {
    case 1: {
        const unsigned char __far *vram = (const unsigned char __far *)MK_FP(0xb800, (y & 1) ? 0x2000 : 0);
        unsigned off = (unsigned)(y >> 1) * 80U + (unsigned)(x >> 2);
        unsigned shift = (unsigned)(6 - ((x & 3) << 1));
        return (vram[off] >> shift) & 3;
    }
    case 2: {
        const unsigned char __far *vram = (const unsigned char __far *)MK_FP(0xb800, (y & 1) ? 0x2000 : 0);
        unsigned off = (unsigned)(y >> 1) * 80U + (unsigned)(x >> 3);
        return (vram[off] >> (7 - (x & 7))) & 1;
    }
    case 7:
    case 8:
    case 9:
    case 12: {
        const unsigned char __far *vram = (const unsigned char __far *)MK_FP(0xa000, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned off = (unsigned)y * stride + (unsigned)(x >> 3);
        unsigned bit = (unsigned)(7 - (x & 7));
        int p, val = 0;
        for (p = 0; p < 4; ++p) {
            outp(0x3ce, 0x04);
            outp(0x3cf, p);
            val |= ((vram[off] >> bit) & 1) << p;
        }
        return val;
    }
    case 13: {
        const unsigned char __far *vram = (const unsigned char __far *)MK_FP(0xa000, 0);
        return vram[(unsigned)y * 320U + (unsigned)x];
    }
    }
    return 0;
}

static void raw_hline(int x1, int x2, int y, int color)
{
    int x;
    if (y < 0 || y >= gfx_height) return;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (x2 < 0 || x1 >= gfx_width) return;
    if (x1 < 0) x1 = 0;
    if (x2 >= gfx_width) x2 = gfx_width - 1;
    if (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12) {
        volatile unsigned char __far *vram = (volatile unsigned char __far *)MK_FP(0xa000, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned row_off = (unsigned)y * stride;
        unsigned b1 = (unsigned)(x1 >> 3), b2 = (unsigned)(x2 >> 3);
        unsigned char left_mask = (unsigned char)(0xffU >> (x1 & 7));
        unsigned char right_mask = (unsigned char)(0xffU << (7 - (x2 & 7)));
        unsigned char c = (unsigned char)(color & 15);
        volatile unsigned char latch;
        outp(0x3ce, 0x05);
        outp(0x3cf, 0x02);
        if (b1 == b2) {
            outp(0x3ce, 0x08);
            outp(0x3cf, left_mask & right_mask);
            latch = vram[row_off + b1];
            (void)latch;
            vram[row_off + b1] = c;
        } else {
            unsigned b;
            outp(0x3ce, 0x08);
            outp(0x3cf, left_mask);
            latch = vram[row_off + b1];
            (void)latch;
            vram[row_off + b1] = c;
            if (b1 + 1 < b2) {
                outp(0x3ce, 0x08);
                outp(0x3cf, 0xff);
                for (b = b1 + 1; b < b2; ++b)
                    vram[row_off + b] = c;
            }
            outp(0x3ce, 0x08);
            outp(0x3cf, right_mask);
            latch = vram[row_off + b2];
            (void)latch;
            vram[row_off + b2] = c;
        }
        reset_planar_gc();
        return;
    }
    if (screen_mode == 13) {
        unsigned char __far *vram = (unsigned char __far *)MK_FP(0xa000, 0);
        unsigned off = (unsigned)y * 320U + (unsigned)x1;
        unsigned count = (unsigned)(x2 - x1 + 1);
        unsigned char c = (unsigned char)(color & 255);
        while (count--) vram[off++] = c;
        return;
    }
    for (x = x1; x <= x2; ++x) raw_put_pixel(x, y, color);
}

static void raw_line_segment(int x0, int y0, int x1, int y1, int color)
{
    long dx, dy, sx, sy, err, e2;
    if (y0 == y1) {
        raw_hline(x0, x1, y0, color);
        return;
    }
    dx = x1 >= x0 ? (long)x1 - (long)x0 : (long)x0 - (long)x1;
    dy = y1 >= y0 ? (long)y0 - (long)y1 : (long)y1 - (long)y0;
    sx = x0 < x1 ? 1L : -1L;
    sy = y0 < y1 ? 1L : -1L;
    err = dx + dy;
    for (;;) {
        raw_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2L;
        if (e2 >= dy) {
            err += dy;
            x0 = (int)((long)x0 + sx);
        }
        if (e2 <= dx) {
            err += dx;
            y0 = (int)((long)y0 + sy);
        }
    }
}

const char *ckb_pset(int x, int y, int color, int is_preset)
{
    if (screen_mode == 0) return "Illegal function call";
    if (color < 0) color = is_preset ? 0 : fg_color;
    raw_put_pixel(x, y, color);
    last_x = x;
    last_y = y;
    return NULL;
}

int ckb_point(int x, int y)
{
    if (screen_mode == 0) return -1;
    return raw_get_pixel(x, y);
}

const char *ckb_line(int has_first, int x1, int y1, int x2, int y2,
                     int color, int box_mode)
{
    if (screen_mode == 0) return "Illegal function call";
    if (!has_first) {
        x1 = last_x;
        y1 = last_y;
    }
    if (color < 0) color = fg_color;
    if (box_mode == 2) { /* BF */
        int top = y1 < y2 ? y1 : y2;
        int bottom = y1 < y2 ? y2 : y1;
        int y;
        for (y = top; y <= bottom; ++y)
            raw_hline(x1, x2, y, color);
    } else if (box_mode == 1) { /* B */
        raw_hline(x1, x2, y1, color);
        raw_hline(x1, x2, y2, color);
        raw_line_segment(x1, y1, x1, y2, color);
        raw_line_segment(x2, y1, x2, y2, color);
    } else {
        raw_line_segment(x1, y1, x2, y2, color);
    }
    last_x = x2;
    last_y = y2;
    return NULL;
}

const char *ckb_circle(int x, int y, double radius, int color,
                       int has_start, double start_angle,
                       int has_end, double end_angle,
                       int has_aspect, double aspect)
{
    double asp, xr, yr, a0 = 0.0, a1 = TWO_PI;
    int ray_start = 0, ray_end = 0, full_circle = (!has_start && !has_end);
    int steps, i, prev_px = 0, prev_py = 0, first_px = 0, first_py = 0;

    if (screen_mode == 0 || radius < 0.0) return "Illegal function call";
    if (color < 0) color = fg_color;
    if (has_aspect) asp = fabs(aspect);
    else asp = 4.0 * ((double)gfx_height / (double)gfx_width) / 3.0;

    if (asp > 1.0) {
        xr = radius / asp;
        yr = radius;
    } else {
        xr = radius;
        yr = radius * asp;
    }

    last_x = x;
    last_y = y;

    if (radius < 0.5) {
        raw_put_pixel(x, y, color);
        return NULL;
    }

    if (!full_circle) {
        double raw0 = has_start ? start_angle : 0.0;
        double raw1 = has_end ? end_angle : TWO_PI;
        ray_start = has_start && raw0 < 0.0;
        ray_end = has_end && raw1 < 0.0;
        a0 = fmod(fabs(raw0), TWO_PI);
        a1 = fmod(fabs(raw1), TWO_PI);
        if (a0 < 0.0) a0 += TWO_PI;
        if (a1 < 0.0) a1 += TWO_PI;
        if (a1 < a0 || (fabs(a1 - a0) < 1e-9 && fabs(raw1 - raw0) > 1e-9))
            a1 += TWO_PI;
    }

    {
        double max_r = xr > yr ? xr : yr;
        double span = a1 - a0;
        double est = max_r * span * 1.5 + 8.0;
        steps = est > 720.0 ? 720 : (int)est;
        if (steps < 8) steps = 8;
    }

    {
        double step_ang = (a1 - a0) / (double)steps;
        double cs = cos(step_ang), sn = sin(step_ang);
        double ux = cos(a0), uy = sin(a0);
        for (i = 0; i <= steps; ++i) {
            double dx = xr * ux;
            double dy = yr * uy;
            int px = x + (int)floor(dx + 0.5);
            int py = y - (int)floor(dy + 0.5);
            double nux = ux * cs - uy * sn;
            uy = ux * sn + uy * cs;
            ux = nux;
            if (i == 0) {
                first_px = prev_px = px;
                first_py = prev_py = py;
                raw_put_pixel(px, py, color);
            } else if (px != prev_px || py != prev_py) {
                raw_line_segment(prev_px, prev_py, px, py, color);
                prev_px = px;
                prev_py = py;
            }
        }
    }
    if (full_circle && (prev_px != first_px || prev_py != first_py))
        raw_line_segment(prev_px, prev_py, first_px, first_py, color);
    if (ray_start) raw_line_segment(x, y, first_px, first_py, color);
    if (ray_end) raw_line_segment(x, y, prev_px, prev_py, color);
    return NULL;
}

typedef struct {
    short x1, x2, y, dy;
} PaintSpan;

#define PAINT_MAX_SPANS 768

static int paint_fillable(int x, int y, int fill_c, int border_c)
{
    int c = raw_get_pixel(x, y);
    return c >= 0 && c != border_c && c != fill_c;
}

const char *ckb_paint(int x, int y, int fill_color, int border_color)
{
    PaintSpan __far *stack;
    unsigned sp = 0;

    if (screen_mode == 0) return "Illegal function call";
    if (fill_color < 0) fill_color = fg_color;
    fill_color &= max_color;
    if (border_color < 0) border_color = fill_color;
    else border_color &= max_color;

    last_x = x;
    last_y = y;

    if (x < 0 || x >= gfx_width || y < 0 || y >= gfx_height) return NULL;
    if (!paint_fillable(x, y, fill_color, border_color)) return NULL;

    stack = (PaintSpan __far *)_fmalloc(PAINT_MAX_SPANS * sizeof(PaintSpan));
    if (!stack) return "Out of memory";

    #define PUSH_SPAN(XL, XR, YV, DYV) do { \
        int ny_ = (YV) + (DYV); \
        if (sp < PAINT_MAX_SPANS && ny_ >= 0 && ny_ < gfx_height) { \
            stack[sp].x1 = (short)(XL); \
            stack[sp].x2 = (short)(XR); \
            stack[sp].y = (short)(YV); \
            stack[sp].dy = (short)(DYV); \
            ++sp; \
        } \
    } while (0)

    PUSH_SPAN(x, x, y, 1);
    PUSH_SPAN(x, x, y + 1, -1);

    while (sp > 0) {
        int x1, x2, dy, l;
        --sp;
        x1 = stack[sp].x1;
        x2 = stack[sp].x2;
        dy = stack[sp].dy;
        y = stack[sp].y + dy;
        x = x1;
        if (paint_fillable(x, y, fill_color, border_color)) {
            while (x > 0 && paint_fillable(x - 1, y, fill_color, border_color))
                --x;
            if (x < x1) PUSH_SPAN(x, x1 - 1, y, -dy);
        }
        while (x1 <= x2) {
            l = x;
            while (x < gfx_width && paint_fillable(x, y, fill_color, border_color))
                ++x;
            if (x > l) {
                raw_hline(l, x - 1, y, fill_color);
                PUSH_SPAN(l, x - 1, y, dy);
                if (x > x2 + 1) PUSH_SPAN(x2 + 1, x - 1, y, -dy);
            }
            ++x;
            while (x <= x2 && !paint_fillable(x, y, fill_color, border_color))
                ++x;
            x1 = x;
        }
    }

    #undef PUSH_SPAN
    _ffree(stack);
    return NULL;
}

static unsigned sprite_row_bytes(unsigned width)
{
    switch (screen_mode) {
    case 1: return (width * 2U + 7U) >> 3;
    case 2: return (width + 7U) >> 3;
    case 13: return width;
    default: return ((width + 7U) >> 3) * 4U;
    }
}

static unsigned sprite_header_width(unsigned width)
{
    if (screen_mode == 1) return width * 2U;
    if (screen_mode == 13) return width * 8U;
    return width;
}

static int sprite_decode_width(unsigned header_w, unsigned *width_out)
{
    if (screen_mode == 1) {
        if ((header_w & 1U) != 0) return 0;
        *width_out = header_w >> 1;
        return 1;
    }
    if (screen_mode == 13) {
        if ((header_w & 7U) != 0) return 0;
        *width_out = header_w >> 3;
        return 1;
    }
    *width_out = header_w;
    return 1;
}

const char *ckb_gfx_get(int x1, int y1, int x2, int y2,
                        unsigned char *buffer, unsigned capacity)
{
    int left, right, top, bottom;
    unsigned width, height, rbytes, total, ry, rx;
    unsigned hdr_w;

    if (screen_mode == 0) return "Illegal function call";
    left = x1 < x2 ? x1 : x2;
    right = x1 < x2 ? x2 : x1;
    top = y1 < y2 ? y1 : y2;
    bottom = y1 < y2 ? y2 : y1;
    if (left < 0 || right >= gfx_width || top < 0 || bottom >= gfx_height)
        return "Illegal function call";

    width = (unsigned)(right - left + 1);
    height = (unsigned)(bottom - top + 1);
    rbytes = sprite_row_bytes(width);
    if (height && rbytes > (65531U / height)) return "Illegal function call";
    total = 4U + rbytes * height;
    if (total > capacity) return "Illegal function call";

    hdr_w = sprite_header_width(width);
    buffer[0] = (unsigned char)(hdr_w & 0xffU);
    buffer[1] = (unsigned char)(hdr_w >> 8);
    buffer[2] = (unsigned char)(height & 0xffU);
    buffer[3] = (unsigned char)(height >> 8);
    memset(buffer + 4, 0, total - 4U);

    if (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12) {
        unsigned pbytes = (width + 7U) >> 3;
        const unsigned char __far *vram = (const unsigned char __far *)MK_FP(0xa000, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        int p;
        for (p = 0; p < 4; ++p) {
            outp(0x3ce, 0x04);
            outp(0x3cf, p);
            for (ry = 0; ry < height; ++ry) {
                unsigned char *pdst = buffer + 4U + ry * rbytes + (unsigned)p * pbytes;
                unsigned row_off = (unsigned)(top + (int)ry) * stride;
                for (rx = 0; rx < width; ++rx) {
                    int sx = left + (int)rx;
                    if ((vram[row_off + (unsigned)(sx >> 3)] >> (7 - (sx & 7))) & 1U)
                        pdst[rx >> 3] |= (unsigned char)(0x80U >> (rx & 7));
                }
            }
        }
    } else {
        for (ry = 0; ry < height; ++ry) {
            unsigned char *row_dst = buffer + 4U + ry * rbytes;
            int sy = top + (int)ry;
            for (rx = 0; rx < width; ++rx) {
                int c = raw_get_pixel(left + (int)rx, sy);
                if (screen_mode == 1)
                    row_dst[rx >> 2] |= (unsigned char)((unsigned)(c & 3) << (6 - ((rx & 3) << 1)));
                else if (screen_mode == 2) {
                    if (c & 1) row_dst[rx >> 3] |= (unsigned char)(0x80U >> (rx & 7));
                } else if (screen_mode == 13)
                    row_dst[rx] = (unsigned char)c;
            }
        }
    }
    return NULL;
}

const char *ckb_gfx_put(int x, int y, const unsigned char *buffer,
                        unsigned capacity, int action)
{
    unsigned hdr_w, width, height, rbytes, total, ry, rx;

    if (screen_mode == 0 || capacity < 4U) return "Illegal function call";
    hdr_w = (unsigned)buffer[0] | ((unsigned)buffer[1] << 8);
    height = (unsigned)buffer[2] | ((unsigned)buffer[3] << 8);
    if (!hdr_w || !height || !sprite_decode_width(hdr_w, &width) || !width)
        return "Illegal function call";
    if (x < 0 || y < 0 || x + (long)width > (long)gfx_width ||
        y + (long)height > (long)gfx_height)
        return "Illegal function call";

    rbytes = sprite_row_bytes(width);
    if (rbytes > (65531U / height)) return "Illegal function call";
    total = 4U + rbytes * height;
    if (total > capacity) return "Illegal function call";

    if (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12) {
        volatile unsigned char __far *vram = (volatile unsigned char __far *)MK_FP(0xa000, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned pbytes = (width + 7U) >> 3;
        int alu = action == CKB_PUT_AND ? 0x08 :
                  action == CKB_PUT_OR ? 0x10 :
                  (action == CKB_PUT_PSET || action == CKB_PUT_PRESET) ? 0x00 : 0x18;
        outp(0x3ce, 0x05);
        outp(0x3cf, 0x02);
        outp(0x3ce, 0x03);
        outp(0x3cf, alu);
        outp(0x3ce, 0x08);
        for (ry = 0; ry < height; ++ry) {
            const unsigned char *row_src = buffer + 4U + ry * rbytes;
            unsigned row_off = (unsigned)(y + (int)ry) * stride;
            for (rx = 0; rx < width; ++rx) {
                int dx = x + (int)rx;
                unsigned b_idx = rx >> 3, b_shift = 7U - (rx & 7U);
                unsigned src = (((row_src[b_idx] >> b_shift) & 1U)) |
                               (((row_src[pbytes + b_idx] >> b_shift) & 1U) << 1) |
                               (((row_src[2U * pbytes + b_idx] >> b_shift) & 1U) << 2) |
                               (((row_src[3U * pbytes + b_idx] >> b_shift) & 1U) << 3);
                volatile unsigned char latch;
                if ((action == CKB_PUT_XOR || action == CKB_PUT_OR) && src == 0U)
                    continue;
                if (action == CKB_PUT_PRESET) src = (~src) & 15U;
                outp(0x3cf, (int)(0x80U >> (dx & 7)));
                latch = vram[row_off + (unsigned)(dx >> 3)];
                (void)latch;
                vram[row_off + (unsigned)(dx >> 3)] = (unsigned char)src;
            }
        }
        reset_planar_gc();
        return NULL;
    }

    for (ry = 0; ry < height; ++ry) {
        const unsigned char *row_src = buffer + 4U + ry * rbytes;
        int dy = y + (int)ry;
        unsigned pbytes = (width + 7U) >> 3;
        for (rx = 0; rx < width; ++rx) {
            int dx = x + (int)rx;
            int src = 0, out_c = 0;
            if (screen_mode == 1)
                src = (row_src[rx >> 2] >> (6 - ((rx & 3) << 1))) & 3;
            else if (screen_mode == 2)
                src = (row_src[rx >> 3] >> (7 - (rx & 7))) & 1;
            else if (screen_mode == 13)
                src = row_src[rx];
            else {
                unsigned b_idx = rx >> 3, b_shift = 7U - (rx & 7U);
                src = (((row_src[b_idx] >> b_shift) & 1U)) |
                      (((row_src[pbytes + b_idx] >> b_shift) & 1U) << 1) |
                      (((row_src[2U * pbytes + b_idx] >> b_shift) & 1U) << 2) |
                      (((row_src[3U * pbytes + b_idx] >> b_shift) & 1U) << 3);
            }
            switch (action) {
            case CKB_PUT_PSET: out_c = src; break;
            case CKB_PUT_PRESET: out_c = (~src) & max_color; break;
            case CKB_PUT_AND: out_c = raw_get_pixel(dx, dy) & src; break;
            case CKB_PUT_OR: out_c = raw_get_pixel(dx, dy) | src; break;
            default: /* XOR */ out_c = raw_get_pixel(dx, dy) ^ src; break;
            }
            raw_put_pixel(dx, dy, out_c);
        }
    }
    return NULL;
}

static unsigned long read_pit_sample(unsigned long *bda_out, unsigned *sub_out)
{
    unsigned long bda1, bda2;
    unsigned lo, hi, count, sub;
    for (;;) {
        bda1 = *(const volatile unsigned long __far *)MK_FP(0x40, 0x6c);
        outp(0x43, 0x00);
        lo = (unsigned)inp(0x40);
        hi = (unsigned)inp(0x40);
        bda2 = *(const volatile unsigned long __far *)MK_FP(0x40, 0x6c);
        count = (hi << 8) | lo;
        if (bda1 == bda2) break;
    }
    sub = 65535U - count;
    if (bda_out) *bda_out = bda1;
    if (sub_out) *sub_out = sub;
    return (bda1 << 16) + (unsigned long)sub;
}

static unsigned long read_pit_total_ticks(void)
{
    return read_pit_sample(NULL, NULL);
}

static unsigned long pit_elapsed(unsigned long start, unsigned long now)
{
    unsigned long diff = now - start;
    /* Midnight rollover (~0x1800B0 * 65536) produces a jump > 2^31 */
    if (diff > 0x80000000UL) return 1193UL;
    return diff;
}

double ckb_timer(void)
{
    unsigned long bda = 0;
    unsigned sub = 0;
    unsigned long total = read_pit_sample(&bda, &sub);
    timer_has_prev = 1;
    timer_prev_total = total;
    return ((double)bda * 65536.0 + (double)sub) * 8.380953445225638e-7;
}

int ckb_inkey(char *out_two_bytes)
{
    unsigned k = input_key();
    if (k == 0xffffU) return 0;
    if ((k & 0xffU) == 0) {
        out_two_bytes[0] = 0;
        out_two_bytes[1] = (char)(k >> 8);
        return 2;
    }
    out_two_bytes[0] = (char)(k & 0xffU);
    return 1;
}

static int bios_key_waiting(void)
{
    unsigned head = *(const volatile unsigned __far *)MK_FP(0x40, 0x1a);
    unsigned tail = *(const volatile unsigned __far *)MK_FP(0x40, 0x1c);
    return head != tail;
}

static void delay_pit_ticks(unsigned long pit_ticks, volatile int *break_flag,
                            int stop_on_key)
{
    unsigned long start = read_pit_total_ticks();
    while (pit_elapsed(start, read_pit_total_ticks()) < pit_ticks) {
        if (break_flag && *break_flag) break;
        if (stop_on_key && bios_key_waiting()) break;
    }
}

void ckb_sleep(long seconds, volatile int *break_flag)
{
    unsigned long start = read_pit_total_ticks();
    unsigned long limit = seconds > 0 ? (unsigned long)seconds * 1193182UL : 0UL;
    for (;;) {
        if (break_flag && *break_flag) break;
        if (bios_key_waiting()) {
            (void)input_key();
            break;
        }
        if (seconds > 0 && pit_elapsed(start, read_pit_total_ticks()) >= limit)
            break;
    }
}

static void speaker_tone_divisor(unsigned divisor)
{
    unsigned port61;
    if (divisor < 18U) divisor = 18U;
    outp(0x43, 0xb6);
    outp(0x42, (int)(divisor & 0xffU));
    outp(0x42, (int)(divisor >> 8));
    port61 = (unsigned)inp(0x61);
    if ((port61 & 3U) != 3U) outp(0x61, (int)(port61 | 3U));
}

void ckb_beep(void)
{
    if (!tty_console()) return;
    speaker_tone_divisor(1491U); /* ~800 Hz */
    delay_pit_ticks(260000UL, NULL, 0);
    ckb_speaker_off();
}

const char *ckb_sound(double freq, double ticks, volatile int *break_flag)
{
    unsigned long pit_wait;
    if (ticks < 0.0 || ticks > 65535.0) return "Illegal function call";
    if (ticks == 0.0) {
        ckb_speaker_off();
        return NULL;
    }
    if (freq < 37.0 || freq > 32767.0) return "Illegal function call";
    if (!tty_console()) return NULL;
    speaker_tone_divisor((unsigned)(1193182.0 / freq + 0.5));
    pit_wait = (unsigned long)(ticks * 65536.0);
    delay_pit_ticks(pit_wait, break_flag, 0);
    ckb_speaker_off();
    return NULL;
}

static unsigned parse_mml_uint(const char **pp, unsigned def_val)
{
    const char *p = *pp;
    unsigned v = 0;
    while (*p == ' ') ++p;
    if (!isdigit((unsigned char)*p)) return def_val;
    while (isdigit((unsigned char)*p)) {
        v = v * 10U + (unsigned)(*p - '0');
        ++p;
    }
    *pp = p;
    return v;
}

const char *ckb_play(const char *mml, volatile int *break_flag)
{
    /* PIT divisors for O0: C, C#, D, D#, E, F, F#, G, G#, A, A#, B */
    static const unsigned base_div[12] = {
        18242U, 17218U, 16252U, 15340U, 14479U, 13666U,
        12899U, 12175U, 11492U, 10847U, 10238U, 9664U
    };
    static const int letter_semi[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
    const char *p = mml;

    while (*p) {
        int ch = toupper((unsigned char)*p++);
        if (ch == ' ' || ch == ';') continue;
        if (break_flag && *break_flag) break;
        if (play_bg && bios_key_waiting()) break;

        if (ch == 'O') {
            unsigned oct = parse_mml_uint(&p, 99U);
            if (oct > 6U) { ckb_speaker_off(); return "Illegal function call"; }
            play_octave = (int)oct;
        } else if (ch == '<') {
            if (play_octave > 0) --play_octave;
        } else if (ch == '>') {
            if (play_octave < 6) ++play_octave;
        } else if (ch == 'L') {
            unsigned len = parse_mml_uint(&p, 0U);
            if (len < 1U || len > 64U) { ckb_speaker_off(); return "Illegal function call"; }
            play_length = (int)len;
        } else if (ch == 'T') {
            unsigned t = parse_mml_uint(&p, 0U);
            if (t < 32U || t > 255U) { ckb_speaker_off(); return "Illegal function call"; }
            play_tempo = (int)t;
        } else if (ch == 'M') {
            int sub;
            while (*p == ' ') ++p;
            sub = toupper((unsigned char)*p);
            if (!sub) { ckb_speaker_off(); return "Illegal function call"; }
            ++p;
            if (sub == 'N') play_style = 0;
            else if (sub == 'L') play_style = 1;
            else if (sub == 'S') play_style = 2;
            else if (sub == 'F') play_bg = 0;
            else if (sub == 'B') play_bg = 1;
            else { ckb_speaker_off(); return "Illegal function call"; }
        } else if (ch == 'P') {
            unsigned len = parse_mml_uint(&p, (unsigned)play_length);
            unsigned long whole_ticks, dur_ticks, add_ticks;
            if (len < 1U || len > 64U) { ckb_speaker_off(); return "Illegal function call"; }
            whole_ticks = (4UL * 60UL * 1193182UL) / (unsigned long)play_tempo;
            dur_ticks = whole_ticks / (unsigned long)len;
            add_ticks = dur_ticks >> 1;
            while (*p == '.') {
                dur_ticks += add_ticks;
                add_ticks >>= 1;
                ++p;
            }
            ckb_speaker_off();
            if (tty_console()) delay_pit_ticks(dur_ticks, break_flag, play_bg);
        } else if (ch == 'N') {
            unsigned note_num = parse_mml_uint(&p, 999U);
            unsigned long whole_ticks, dur_ticks, add_ticks;
            if (note_num > 84U) { ckb_speaker_off(); return "Illegal function call"; }
            whole_ticks = (4UL * 60UL * 1193182UL) / (unsigned long)play_tempo;
            dur_ticks = whole_ticks / (unsigned long)play_length;
            add_ticks = dur_ticks >> 1;
            while (*p == '.') {
                dur_ticks += add_ticks;
                add_ticks >>= 1;
                ++p;
            }
            if (note_num == 0U) {
                ckb_speaker_off();
                if (tty_console()) delay_pit_ticks(dur_ticks, break_flag, play_bg);
            } else if (tty_console()) {
                int oct = (int)((note_num - 1U) / 12U);
                int semi = (int)((note_num - 1U) % 12U);
                unsigned div = base_div[semi] >> oct;
                unsigned long on_ticks = play_style == 1 ? dur_ticks :
                                         play_style == 2 ? (dur_ticks * 3UL) >> 2 :
                                                           (dur_ticks * 7UL) >> 3;
                speaker_tone_divisor(div);
                delay_pit_ticks(on_ticks, break_flag, play_bg);
                ckb_speaker_off();
                if (dur_ticks > on_ticks)
                    delay_pit_ticks(dur_ticks - on_ticks, break_flag, play_bg);
            }
        } else if (ch >= 'A' && ch <= 'G') {
            int semi = letter_semi[ch - 'A'];
            int oct = play_octave;
            unsigned len;
            unsigned long whole_ticks, dur_ticks, add_ticks;
            while (*p == ' ') ++p;
            if (*p == '#' || *p == '+') { ++semi; ++p; }
            else if (*p == '-') { --semi; ++p; }
            if (semi < 0) { semi += 12; if (oct > 0) --oct; }
            else if (semi >= 12) { semi -= 12; if (oct < 6) ++oct; }
            len = parse_mml_uint(&p, (unsigned)play_length);
            if (len < 1U || len > 64U) { ckb_speaker_off(); return "Illegal function call"; }
            whole_ticks = (4UL * 60UL * 1193182UL) / (unsigned long)play_tempo;
            dur_ticks = whole_ticks / (unsigned long)len;
            add_ticks = dur_ticks >> 1;
            while (*p == '.') {
                dur_ticks += add_ticks;
                add_ticks >>= 1;
                ++p;
            }
            if (tty_console()) {
                unsigned div = base_div[semi] >> oct;
                unsigned long on_ticks = play_style == 1 ? dur_ticks :
                                         play_style == 2 ? (dur_ticks * 3UL) >> 2 :
                                                           (dur_ticks * 7UL) >> 3;
                speaker_tone_divisor(div);
                delay_pit_ticks(on_ticks, break_flag, play_bg);
                ckb_speaker_off();
                if (dur_ticks > on_ticks)
                    delay_pit_ticks(dur_ticks - on_ticks, break_flag, play_bg);
            }
        } else if (ch == 'X') {
            while (*p && *p != ';') ++p;
            if (*p == ';') ++p;
        } else {
            ckb_speaker_off();
            return "Illegal function call";
        }
    }
    ckb_speaker_off();
    return NULL;
}

#else

/* Host stub implementation when compiled outside DOS. */
const char *ckb_screen(int mode) { return mode == 0 ? NULL : "Illegal function call"; }
const char *ckb_width_screen(int cols, int rows) { (void)cols; (void)rows; return NULL; }
const char *ckb_palette(int attr, long color) { (void)attr; (void)color; return NULL; }
void ckb_color(int foreground, int background) { (void)foreground; (void)background; }
void ckb_cls_mode(int mode) { (void)mode; }
void ckb_cls(void) {}
const char *ckb_locate(int row, int col, int cursor) { (void)row; (void)col; (void)cursor; return NULL; }
const char *ckb_view_print(int top, int bottom) { (void)top; (void)bottom; return NULL; }
int ckb_con_direct_active(void) { return 0; }
unsigned ckb_con_column(void) { return 0; }
unsigned ckb_con_width(void) { return 80; }
void ckb_con_write(const char *text, unsigned length) { fwrite(text, 1, length, stdout); }
void ckb_con_newline(void) { fputc('\n', stdout); }
void ckb_con_reset_run(void) {}
void ckb_con_reset_text(void) {}
void ckb_con_acknowledge_graphics(void) {}
void ckb_gfx_last(int *x, int *y) { *x = 0; *y = 0; }
int ckb_gfx_default_color(int is_preset) { (void)is_preset; return 7; }
const char *ckb_pset(int x, int y, int color, int is_preset) {
    (void)x; (void)y; (void)color; (void)is_preset; return "Illegal function call";
}
int ckb_point(int x, int y) { (void)x; (void)y; return -1; }
const char *ckb_line(int hf, int x1, int y1, int x2, int y2, int c, int bm) {
    (void)hf; (void)x1; (void)y1; (void)x2; (void)y2; (void)c; (void)bm;
    return "Illegal function call";
}
const char *ckb_circle(int x, int y, double r, int c, int hs, double sa,
                       int he, double ea, int ha, double asp) {
    (void)x; (void)y; (void)r; (void)c; (void)hs; (void)sa; (void)he; (void)ea; (void)ha; (void)asp;
    return "Illegal function call";
}
const char *ckb_paint(int x, int y, int fc, int bc) {
    (void)x; (void)y; (void)fc; (void)bc; return "Illegal function call";
}
const char *ckb_gfx_get(int x1, int y1, int x2, int y2, unsigned char *b, unsigned cap) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)b; (void)cap; return "Illegal function call";
}
const char *ckb_gfx_put(int x, int y, const unsigned char *b, unsigned cap, int act) {
    (void)x; (void)y; (void)b; (void)cap; (void)act; return "Illegal function call";
}
double ckb_timer(void) { return 0.0; }
int ckb_inkey(char *out_two_bytes) { (void)out_two_bytes; return 0; }
void ckb_sleep(long seconds, volatile int *break_flag) { (void)seconds; (void)break_flag; }
void ckb_beep(void) {}
const char *ckb_sound(double freq, double ticks, volatile int *break_flag) {
    (void)freq; (void)ticks; (void)break_flag; return NULL;
}
const char *ckb_play(const char *mml, volatile int *break_flag) {
    (void)mml; (void)break_flag; return NULL;
}
void ckb_speaker_off(void) {}

#endif
