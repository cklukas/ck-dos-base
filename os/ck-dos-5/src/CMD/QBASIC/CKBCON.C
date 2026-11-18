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
extern int __far ckb_snd_push(unsigned divisor, unsigned ticks);
extern unsigned __far ckb_snd_queued(void);
extern int __far ckb_snd_active(void);
extern void __far ckb_snd_stop(void);
#ifdef __WATCOMC__
#pragma aux input_key "_input_key" value [ax] modify exact [ax]
#pragma aux ckb_snd_push "CKB_SND_PUSH" parm [ax] [dx] value [ax] modify exact [ax]
#pragma aux ckb_snd_queued "CKB_SND_QUEUED" value [ax] modify exact [ax]
#pragma aux ckb_snd_active "CKB_SND_ACTIVE" value [ax] modify exact [ax]
#pragma aux ckb_snd_stop "CKB_SND_STOP" modify exact []
#endif

static unsigned display_flags = 0;
static int screen_mode = 0;
static int screen_cols = 80;
static int screen_rows = 25;
static int active_page = 0;
static int visual_page = 0;
static unsigned vram_page_seg_off = 0;
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

/* Graphics VIEW and WINDOW state */
static int vp_active = 0;
static int vp_screen = 0;
static int vp_x1 = 0, vp_y1 = 0, vp_x2 = 0, vp_y2 = 0;
static int win_active = 0;
static int win_screen = 0;
static double win_x1 = 0.0, win_y1 = 0.0, win_x2 = 0.0, win_y2 = 0.0;

/* Persistent DRAW state (reset on run / SCREEN change) */
static int draw_scale = 4;
static int draw_angle = 0;

/* Persistent MML PLAY state (reset at start of each program run). */
static int play_octave = 4;
static int play_length = 4;
static int play_tempo = 120;
static int play_style = 0; /* 0 = MN (7/8), 1 = ML (8/8), 2 = MS (3/4) */
static int play_bg = 0;
static unsigned long play_frac_rem = 0;

/* Timer pacing state so tight TIMER loops calibrate consistently across CPUs. */
static int timer_has_prev = 0;
static unsigned long timer_prev_total = 0;

void ckb_set_display_flags(unsigned flags) { display_flags = flags; }
unsigned ckb_get_display_flags(void) { return display_flags; }

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

static unsigned text_page_seg(int page)
{
    unsigned paras = (screen_cols == 40 && screen_rows == 25) ? 128U : 256U;
    if (screen_rows > 25) paras = 512U;
    return 0xb800U + (unsigned)page * paras;
}

static unsigned gfx_page_seg(int mode, int page)
{
    if (mode == 7) return 0xa000U + (unsigned)page * 0x0200U;
    if (mode == 8) return 0xa000U + (unsigned)page * 0x0400U;
    if (mode == 9 || mode == 10) return 0xa000U + (unsigned)page * 0x0800U;
    return 0xa000U;
}

static int max_page_for_mode(int mode)
{
    switch (mode) {
    case 0: return screen_rows > 25 ? 1 : 7;
    case 7: return 7;
    case 8: return 3;
    case 9:
    case 10: return 1;
    default: return 0;
    }
}

void ckb_speaker_off(void)
{
    ckb_snd_stop();
}

void ckb_con_reset_run(void)
{
    ckb_snd_stop();
    screen_mode = 0;
    screen_cols = 80;
    screen_rows = 25;
    active_page = 0;
    visual_page = 0;
    vram_page_seg_off = 0;
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
    vp_active = 0;
    vp_screen = 0;
    vp_x1 = vp_y1 = vp_x2 = vp_y2 = 0;
    win_active = 0;
    win_screen = 0;
    draw_scale = 4;
    draw_angle = 0;
    play_octave = 4;
    play_length = 4;
    play_tempo = 120;
    play_style = 0;
    play_bg = 0;
    play_frac_rem = 0;
    timer_has_prev = 0;
}

void ckb_con_reset_text(void)
{
    union REGS regs = {0};
    ckb_snd_stop();
    if ((*(const unsigned char __far *)MK_FP(0x40, 0x49) & 0x7f) != 3 ||
        *(const unsigned char __far *)MK_FP(0x40, 0x62) != 0 ||
        *(const unsigned char __far *)MK_FP(0x40, 0x84) > 24) {
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
    ckb_snd_stop();
    view_top = 1;
    view_bottom = screen_rows;
    ckb_locate(screen_rows, 1, -1);
    ckb_con_write(msg, (unsigned)strlen(msg));
    while (input_key() == 0xffffU) {
    }
}

static int adapter_supports_mode(int target_mode)
{
    union REGS regs = {0};
    struct SREGS sregs = {0};
    unsigned char fbuf[64];
    int has_vga, has_ega, is_mda_equip;
    if (target_mode == 0) return 1;
    regs.x.ax = 0x1a00;
    int86(0x10, &regs, &regs);
    has_vga = (regs.h.al == 0x1a);
    if (has_vga) {
        if (target_mode >= 7) {
            memset(fbuf, 0, sizeof(fbuf));
            segread(&sregs);
            sregs.es = sregs.ss;
            regs.x.ax = 0x1b00;
            regs.x.bx = 0x0000;
            regs.x.di = (unsigned)(size_t)fbuf;
            int86x(0x10, &regs, &regs, &sregs);
            if (regs.h.al == 0x1b) {
                unsigned off = (unsigned)fbuf[0] | ((unsigned)fbuf[1] << 8);
                unsigned seg = (unsigned)fbuf[2] | ((unsigned)fbuf[3] << 8);
                if ((seg || off) &&
                    (((const unsigned char __far *)MK_FP(seg, off))[2] & 0x0fU) == 0U)
                    return 0;
            }
        }
        return 1;
    }
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x12;
    regs.h.bl = 0x10;
    int86(0x10, &regs, &regs);
    has_ega = (regs.h.bl <= 3);
    if (target_mode >= 11) return 0;
    if (target_mode >= 7) return has_ega;
    is_mda_equip = ((*(const unsigned __far *)MK_FP(0x40, 0x10)) & 0x30U) == 0x30U;
    return has_ega || !is_mda_equip;
}

const char *ckb_screen_ex(int mode, int colorswitch, int apage, int vpage)
{
    union REGS regs = {0};
    int target_mode = mode == -1 ? screen_mode : mode;
    int mode_changed = (mode != -1 && mode != screen_mode) ||
                       (mode == 0 && screen_mode == 0 && apage == -1 && vpage == -1);
    int bios_mode = 0x03;
    int max_pg;
    (void)colorswitch;

    if (tty_console() && !adapter_supports_mode(target_mode))
        return "Illegal function call";

    switch (target_mode) {
    case 0:
        bios_mode = screen_cols == 40 ? 0x01 : 0x03;
        if (mode_changed) {
            screen_rows = 25;
            gfx_width = 0;
            gfx_height = 0;
            max_color = 15;
            fg_color = 7;
            bg_color = 0;
        }
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
    case 10:
        bios_mode = 0x0f;
        screen_cols = 80;
        screen_rows = 25;
        gfx_width = 640;
        gfx_height = 350;
        max_color = 3;
        fg_color = 3;
        bg_color = 0;
        break;
    case 11:
        bios_mode = 0x11;
        screen_cols = 80;
        screen_rows = 30;
        gfx_width = 640;
        gfx_height = 480;
        max_color = 1;
        fg_color = 1;
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

    max_pg = max_page_for_mode(target_mode);
    if ((apage != -1 && (apage < 0 || apage > max_pg)) ||
        (vpage != -1 && (vpage < 0 || vpage > max_pg)))
        return "Illegal function call";

    if (!direct_console) fflush(stdout);
    screen_mode = target_mode;
    if (mode_changed) {
        active_page = 0;
        visual_page = 0;
        text_attr = (unsigned char)(((bg_color & 7) << 4) | (fg_color & 15));
        view_top = 1;
        view_bottom = screen_rows;
        cur_row = 1;
        cur_col = 1;
        cursor_synced = 1;
        cursor_visible = 0;
        pending_wrap = 0;
        vp_active = 0;
        vp_screen = 0;
        vp_x1 = 0;
        vp_y1 = 0;
        vp_x2 = gfx_width > 0 ? gfx_width - 1 : 0;
        vp_y2 = gfx_height > 0 ? gfx_height - 1 : 0;
        win_active = 0;
        win_screen = 0;
        draw_scale = 4;
        draw_angle = 0;
        last_x = gfx_width / 2;
        last_y = gfx_height / 2;
        if (target_mode != 0 || tty_console()) {
            direct_console = 1;
            regs.x.ax = (unsigned)bios_mode;
            int86(0x10, &regs, &regs);
            if (target_mode == 0) {
                regs.h.ah = 0x01;
                regs.x.cx = 0x2607;
                int86(0x10, &regs, &regs);
            }
        }
    }
    if (apage != -1) active_page = apage;
    if (vpage != -1) {
        visual_page = vpage;
        if (tty_console()) {
            regs.h.ah = 0x05;
            regs.h.al = (unsigned char)visual_page;
            int86(0x10, &regs, &regs);
        }
    }
    vram_page_seg_off = target_mode == 0
        ? (text_page_seg(active_page) - 0xb800U)
        : (gfx_page_seg(target_mode, active_page) - 0xa000U);
    return NULL;
}

const char *ckb_screen(int mode)
{
    return ckb_screen_ex(mode, -1, -1, -1);
}

const char *ckb_pcopy(int src_page, int dst_page)
{
    int max_pg = max_page_for_mode(screen_mode);
    if (src_page < 0 || src_page > max_pg || dst_page < 0 || dst_page > max_pg)
        return "Illegal function call";
    if (src_page == dst_page) return NULL;
    if (screen_mode == 0) {
        unsigned src_seg = text_page_seg(src_page);
        unsigned dst_seg = text_page_seg(dst_page);
        unsigned bytes = (unsigned)screen_cols * (unsigned)screen_rows * 2U;
        _fmemcpy(MK_FP(dst_seg, 0), MK_FP(src_seg, 0), bytes);
        return NULL;
    }
    if (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 10) {
        volatile unsigned char __far *src =
            (volatile unsigned char __far *)MK_FP(gfx_page_seg(screen_mode, src_page), 0);
        volatile unsigned char __far *dst =
            (volatile unsigned char __far *)MK_FP(gfx_page_seg(screen_mode, dst_page), 0);
        unsigned plane_bytes = (unsigned)(gfx_width >> 3) * (unsigned)gfx_height;
        unsigned i;
        outp(0x3c4, 0x02);
        outp(0x3c5, 0x0f);
        outp(0x3ce, 0x05);
        outp(0x3cf, 0x01);
        for (i = 0; i < plane_bytes; ++i) {
            unsigned char v = src[i];
            (void)v;
            dst[i] = 0;
        }
        reset_planar_gc();
        return NULL;
    }
    return "Illegal function call";
}

const char *ckb_width_screen(int cols, int rows)
{
    if (cols != -1 && cols != 40 && cols != 80) return "Illegal function call";
    if (rows != -1 && rows != 25 && rows != 30 && rows != 43 && rows != 50)
        return "Illegal function call";
    if (cols != -1) screen_cols = cols;
    if (rows != -1 && screen_mode == 0) screen_rows = rows;
    if (screen_mode == 0 && tty_console()) {
        union REGS regs = {0};
        direct_console = 1;
        regs.x.ax = screen_cols == 40 ? 0x0001 : 0x0003;
        int86(0x10, &regs, &regs);
        if (screen_rows >= 43) {
            regs.x.ax = 0x1112;
            regs.h.bl = 0;
            int86(0x10, &regs, &regs);
            screen_rows = (int)(*(const unsigned char __far *)MK_FP(0x40, 0x84)) + 1;
        }
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
        vram_page_seg_off = text_page_seg(active_page) - 0xb800U;
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
        if (background >= 0) {
            bg_color = background & 15;
            if ((screen_mode == 7 || screen_mode == 8 || screen_mode == 9) && tty_console()) {
                union REGS regs = {0};
                regs.x.ax = 0x1000;
                regs.h.bl = 0;
                regs.h.bh = (unsigned char)(background & 0x3f);
                int86(0x10, &regs, &regs);
            }
        }
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
    unsigned __far *vram = (unsigned __far *)MK_FP(0xb800U + vram_page_seg_off, 0);
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

int ckb_csrlin(void)
{
    if (tty_console() && !cursor_synced) sync_bios_cursor_from_bda();
    return cur_row;
}

int ckb_pos_col(void)
{
    if (tty_console() && !cursor_synced) sync_bios_cursor_from_bda();
    return pending_wrap ? screen_cols : cur_col;
}

int ckb_screen_cell(int row, int col, int color_flag, const char **err_out)
{
    *err_out = NULL;
    if (row < 1 || row > screen_rows || col < 1 || col > screen_cols) {
        *err_out = "Illegal function call";
        return 0;
    }
    if (color_flag && screen_mode != 0) {
        *err_out = "Illegal function call";
        return 0;
    }
    if (screen_mode == 0) {
        const unsigned __far *vram =
            (const unsigned __far *)MK_FP(0xb800U + vram_page_seg_off, 0);
        unsigned cell = vram[(unsigned)(row - 1) * (unsigned)screen_cols + (unsigned)(col - 1)];
        if (color_flag) return (int)((cell >> 8) & 0xffU);
        return (int)((cell & 0xffU) ? (cell & 0xffU) : 32U);
    }
    if (tty_console()) {
        union REGS regs = {0};
        int save_r = cur_row, save_c = cur_col;
        set_bios_cursor_hw(row, col);
        regs.h.ah = 0x08;
        regs.h.bh = (unsigned char)active_page;
        int86(0x10, &regs, &regs);
        set_bios_cursor(save_r, save_c);
        return regs.h.al ? (int)regs.h.al : 32;
    }
    return 32;
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
        unsigned __far *vram = (unsigned __far *)MK_FP(0xb800U + vram_page_seg_off, 0);
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
        unsigned __far *vram = (unsigned __far *)MK_FP(0xb800U + vram_page_seg_off, 0);
        unsigned idx = (unsigned)(cur_row - 1) * (unsigned)screen_cols + (unsigned)(cur_col - 1);
        vram[idx] = ((unsigned)text_attr << 8) | ch;
    } else {
        union REGS regs = {0};
        set_bios_cursor(cur_row, cur_col);
        regs.h.ah = 0x09;
        regs.h.al = ch;
        regs.h.bh = (unsigned char)active_page;
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

int ckb_window_active(void) { return win_active; }

static int clip_min_x(void) { return vp_active ? vp_x1 : 0; }
static int clip_max_x(void) { return vp_active ? vp_x2 : (gfx_width - 1); }
static int clip_min_y(void) { return vp_active ? vp_y1 : 0; }
static int clip_max_y(void) { return vp_active ? vp_y2 : (gfx_height - 1); }

static void raw_put_pixel(int x, int y, int color)
{
    unsigned char c;
    if (x < clip_min_x() || x > clip_max_x() || y < clip_min_y() || y > clip_max_y()) return;
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
    case 11: {
        unsigned char __far *vram = (unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned off = (unsigned)y * 80U + (unsigned)(x >> 3);
        unsigned bit = (unsigned)(0x80U >> (x & 7));
        if (c & 1) vram[off] |= (unsigned char)bit;
        else vram[off] &= (unsigned char)~bit;
        break;
    }
    case 7:
    case 8:
    case 9:
    case 10:
    case 12: {
        volatile unsigned char __far *vram =
            (volatile unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned off = (unsigned)y * stride + (unsigned)(x >> 3);
        unsigned char mask = (unsigned char)(0x80U >> (x & 7));
        unsigned char pc = screen_mode == 10 ? (unsigned char)(((c & 2) << 1) | (c & 1)) : c;
        volatile unsigned char latch;
        outp(0x3ce, 0x05);
        outp(0x3cf, 0x02);
        outp(0x3ce, 0x08);
        outp(0x3cf, mask);
        latch = vram[off];
        (void)latch;
        vram[off] = pc;
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
    case 11: {
        const unsigned char __far *vram =
            (const unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned off = (unsigned)y * 80U + (unsigned)(x >> 3);
        return (vram[off] >> (7 - (x & 7))) & 1;
    }
    case 7:
    case 8:
    case 9:
    case 10:
    case 12: {
        const unsigned char __far *vram =
            (const unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned off = (unsigned)y * stride + (unsigned)(x >> 3);
        unsigned bit = (unsigned)(7 - (x & 7));
        int val;
        outp(0x3ce, 0x04);
        outp(0x3cf, 0);
        val = (vram[off] >> bit) & 1;
        if (screen_mode == 10) {
            outp(0x3cf, 2);
            val |= ((vram[off] >> bit) & 1) << 1;
            outp(0x3cf, 0);
            return val;
        }
        outp(0x3cf, 1);
        val |= ((vram[off] >> bit) & 1) << 1;
        outp(0x3cf, 2);
        val |= ((vram[off] >> bit) & 1) << 2;
        outp(0x3cf, 3);
        val |= ((vram[off] >> bit) & 1) << 3;
        outp(0x3cf, 0);
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
    int x, minx = clip_min_x(), maxx = clip_max_x();
    if (y < clip_min_y() || y > clip_max_y()) return;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (x2 < minx || x1 > maxx) return;
    if (x1 < minx) x1 = minx;
    if (x2 > maxx) x2 = maxx;
    if (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12) {
        volatile unsigned char __far *vram =
            (volatile unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
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
            outp(0x3ce, 0x08);
            outp(0x3cf, left_mask);
            latch = vram[row_off + b1];
            (void)latch;
            vram[row_off + b1] = c;
            if (b1 + 1 < b2) {
                outp(0x3ce, 0x05);
                outp(0x3cf, 0x00);
                outp(0x3ce, 0x00);
                outp(0x3cf, c);
                outp(0x3ce, 0x01);
                outp(0x3cf, 0x0f);
                outp(0x3ce, 0x08);
                outp(0x3cf, 0xff);
                _fmemset((void __far *)(vram + row_off + b1 + 1U), 0xff, b2 - b1 - 1U);
                outp(0x3ce, 0x01);
                outp(0x3cf, 0x00);
                outp(0x3ce, 0x05);
                outp(0x3cf, 0x02);
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
        _fmemset(vram + (unsigned)y * 320U + (unsigned)x1, color & 255,
                 (unsigned)(x2 - x1 + 1));
        return;
    }
    for (x = x1; x <= x2; ++x) raw_put_pixel(x, y, color);
}

static void raw_fill_rect(int x1, int y1, int x2, int y2, int color)
{
    int y, minx = clip_min_x(), maxx = clip_max_x(), miny = clip_min_y(), maxy = clip_max_y();
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (x2 < minx || x1 > maxx || y2 < miny || y1 > maxy) return;
    if (x1 < minx) x1 = minx;
    if (x2 > maxx) x2 = maxx;
    if (y1 < miny) y1 = miny;
    if (y2 > maxy) y2 = maxy;

    if (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12) {
        volatile unsigned char __far *vram =
            (volatile unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned b1 = (unsigned)(x1 >> 3), b2 = (unsigned)(x2 >> 3);
        unsigned char left_mask = (unsigned char)(0xffU >> (x1 & 7));
        unsigned char right_mask = (unsigned char)(0xffU << (7 - (x2 & 7)));
        unsigned char c = (unsigned char)(color & 15);
        volatile unsigned char latch;
        unsigned off;

        outp(0x3ce, 0x05);
        outp(0x3cf, 0x02);
        if (b1 == b2) {
            outp(0x3ce, 0x08);
            outp(0x3cf, left_mask & right_mask);
            off = (unsigned)y1 * stride + b1;
            for (y = y1; y <= y2; ++y, off += stride) {
                latch = vram[off];
                (void)latch;
                vram[off] = c;
            }
        } else {
            unsigned fb1 = (left_mask == 0xffU) ? b1 : (b1 + 1U);
            unsigned fb2 = (right_mask == 0xffU) ? b2 : (b2 - 1U);
            if (left_mask != 0xffU) {
                outp(0x3ce, 0x08);
                outp(0x3cf, left_mask);
                off = (unsigned)y1 * stride + b1;
                for (y = y1; y <= y2; ++y, off += stride) {
                    latch = vram[off];
                    (void)latch;
                    vram[off] = c;
                }
            }
            if (fb1 <= fb2) {
                unsigned fcount = fb2 - fb1 + 1U;
                outp(0x3ce, 0x05);
                outp(0x3cf, 0x00);
                outp(0x3ce, 0x00);
                outp(0x3cf, c);
                outp(0x3ce, 0x01);
                outp(0x3cf, 0x0f);
                outp(0x3ce, 0x08);
                outp(0x3cf, 0xff);
                off = (unsigned)y1 * stride + fb1;
                for (y = y1; y <= y2; ++y, off += stride)
                    _fmemset((void __far *)(vram + off), 0xff, fcount);
                outp(0x3ce, 0x01);
                outp(0x3cf, 0x00);
                outp(0x3ce, 0x05);
                outp(0x3cf, 0x02);
            }
            if (right_mask != 0xffU) {
                outp(0x3ce, 0x08);
                outp(0x3cf, right_mask);
                off = (unsigned)y1 * stride + b2;
                for (y = y1; y <= y2; ++y, off += stride) {
                    latch = vram[off];
                    (void)latch;
                    vram[off] = c;
                }
            }
        }
        reset_planar_gc();
        return;
    }
    if (screen_mode == 13) {
        unsigned char __far *vram = (unsigned char __far *)MK_FP(0xa000, 0);
        unsigned count = (unsigned)(x2 - x1 + 1);
        for (y = y1; y <= y2; ++y)
            _fmemset(vram + (unsigned)y * 320U + (unsigned)x1, color & 255, count);
        return;
    }
    for (y = y1; y <= y2; ++y) raw_hline(x1, x2, y, color);
}

static void raw_vline(int x, int y1, int y2, int color)
{
    int y, miny = clip_min_y(), maxy = clip_max_y();
    if (x < clip_min_x() || x > clip_max_x()) return;
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (y2 < miny || y1 > maxy) return;
    if (y1 < miny) y1 = miny;
    if (y2 > maxy) y2 = maxy;
    if (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12) {
        volatile unsigned char __far *vram =
            (volatile unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned off = (unsigned)y1 * stride + (unsigned)(x >> 3);
        unsigned char mask = (unsigned char)(0x80U >> (x & 7));
        unsigned char c = (unsigned char)(color & 15);
        volatile unsigned char latch;
        outp(0x3ce, 0x05);
        outp(0x3cf, 0x02);
        outp(0x3ce, 0x08);
        outp(0x3cf, mask);
        for (y = y1; y <= y2; ++y, off += stride) {
            latch = vram[off];
            (void)latch;
            vram[off] = c;
        }
        reset_planar_gc();
        return;
    }
    for (y = y1; y <= y2; ++y) raw_put_pixel(x, y, color);
}

/* Assumes planar GC mode 2 and GC index 0x08 are ALREADY selected when planar_ready != 0. */
static void raw_line_segment_ex(int x0, int y0, int x1, int y1, int color, int planar_ready)
{
    int is_planar = (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12);
    if (!planar_ready) {
        if (y0 == y1) { raw_hline(x0, x1, y0, color); return; }
        if (x0 == x1) { raw_vline(x0, y0, y1, color); return; }
    }
    if (!vp_active && is_planar && x0 >= -4096 && x0 <= 4096 && x1 >= -4096 && x1 <= 4096 &&
        y0 >= -4096 && y0 <= 4096 && y1 >= -4096 && y1 <= 4096) {
        volatile unsigned char __far *vram =
            (volatile unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned char c = (unsigned char)(color & 15);
        int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
        int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;
        unsigned cur_off = 0xffffU;
        unsigned char acc_mask = 0, last_out_mask = 0;
        volatile unsigned char latch;

        if (!planar_ready) {
            outp(0x3ce, 0x05);
            outp(0x3cf, 0x02);
            outp(0x3ce, 0x08);
        }
        for (;;) {
            if ((unsigned)x0 < (unsigned)gfx_width && (unsigned)y0 < (unsigned)gfx_height) {
                unsigned off = (unsigned)y0 * stride + ((unsigned)x0 >> 3);
                unsigned char bit = (unsigned char)(0x80U >> (x0 & 7));
                if (off == cur_off) {
                    acc_mask |= bit;
                } else {
                    if (acc_mask) {
                        if (acc_mask != last_out_mask) {
                            outp(0x3cf, acc_mask);
                            last_out_mask = acc_mask;
                        }
                        latch = vram[cur_off];
                        (void)latch;
                        vram[cur_off] = c;
                    }
                    cur_off = off;
                    acc_mask = bit;
                }
            }
            if (x0 == x1 && y0 == y1) break;
            e2 = err << 1;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
        if (acc_mask) {
            if (acc_mask != last_out_mask) outp(0x3cf, acc_mask);
            latch = vram[cur_off];
            (void)latch;
            vram[cur_off] = c;
        }
        if (!planar_ready) reset_planar_gc();
        return;
    }
    {
        long dx = x1 >= x0 ? (long)x1 - (long)x0 : (long)x0 - (long)x1;
        long dy = y1 >= y0 ? (long)y0 - (long)y1 : (long)y1 - (long)y0;
        long sx = x0 < x1 ? 1L : -1L;
        long sy = y0 < y1 ? 1L : -1L;
        long err = dx + dy, e2;
        for (;;) {
            raw_put_pixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            e2 = err * 2L;
            if (e2 >= dy) { err += dy; x0 = (int)((long)x0 + sx); }
            if (e2 <= dx) { err += dx; y0 = (int)((long)y0 + sy); }
        }
    }
}

static void raw_line_segment(int x0, int y0, int x1, int y1, int color)
{
    raw_line_segment_ex(x0, y0, x1, y1, color, 0);
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
        raw_fill_rect(x1, y1, x2, y2, color);
    } else if (box_mode == 1) { /* B */
        raw_hline(x1, x2, y1, color);
        raw_hline(x1, x2, y2, color);
        raw_vline(x1, y1, y2, color);
        raw_vline(x2, y1, y2, color);
    } else {
        raw_line_segment(x1, y1, x2, y2, color);
    }
    last_x = x2;
    last_y = y2;
    return NULL;
}

static const short qsin14[65] = {
        0,   402,   804,  1205,  1606,  2006,  2404,  2801,
     3196,  3590,  3981,  4370,  4756,  5139,  5520,  5897,
     6270,  6639,  7005,  7366,  7723,  8076,  8423,  8765,
     9102,  9434,  9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395,
    13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986,
    16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
    16384
};

static int fast_sin14(int a4096)
{
    int a = a4096 & 4095;
    int q = (a >> 10) & 3;
    int off = a & 1023;
    int idx, frac, v;
    if (q & 1) off = 1024 - off;
    idx = off >> 4;
    frac = off & 15;
    v = (int)qsin14[idx] + ((((int)qsin14[idx + 1] - (int)qsin14[idx]) * frac + 8) >> 4);
    return (q & 2) ? -v : v;
}

static int round_fp18(long v)
{
    if (v >= -131072L) return (int)((unsigned long)(v + 131072L) >> 18);
    return -(int)(((unsigned long)(-v - 131073L) >> 18) + 1UL);
}

const char *ckb_circle(int x, int y, double radius, int color,
                       int has_start, double start_angle,
                       int has_end, double end_angle,
                       int has_aspect, double aspect)
{
    double asp, xr, yr, a0 = 0.0, a1 = TWO_PI;
    int ray_start = 0, ray_end = 0, full_circle = (!has_start && !has_end);
    int steps, i, prev_px = 0, prev_py = 0, first_px = 0, first_py = 0;
    int is_planar = (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12);
    long xr_fp4, yr_fp4;
    int a0_4096, a1_4096;

    if (screen_mode == 0 || radius < 0.0) return "Illegal function call";
    if (color < 0) color = fg_color;
    if (has_aspect) asp = fabs(aspect);
    else asp = (4.0 * (double)gfx_height) / (3.0 * (double)gfx_width);

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
        a0_4096 = (int)(a0 * (4096.0 / TWO_PI) + 0.5);
        a1_4096 = (int)(a1 * (4096.0 / TWO_PI) + 0.5);
    } else {
        a0_4096 = 0;
        a1_4096 = 4096;
    }

    xr_fp4 = (long)(xr * 16.0 + 0.5);
    yr_fp4 = (long)(yr * 16.0 + 0.5);
    {
        long max_r4 = xr_fp4 > yr_fp4 ? xr_fp4 : yr_fp4;
        long span_ang = (long)(a1_4096 - a0_4096);
        /* ~1.25 steps per pixel of arc length: max_r * span_rad = (max_r4/16)*(span_ang*2pi/4096) ~ max_r4*span_ang / 10430 */
        int est = (int)((max_r4 * span_ang) / 8192L) + 8;
        steps = est > 360 ? 360 : (est < 8 ? 8 : est);
    }

    if (is_planar) {
        outp(0x3ce, 0x05);
        outp(0x3cf, 0x02);
        outp(0x3ce, 0x08);
    }

    for (i = 0; i <= steps; ++i) {
        int ang = a0_4096 + (int)(((long)(a1_4096 - a0_4096) * (long)i) / (long)steps);
        int cs = fast_sin14(ang + 1024);
        int sn = fast_sin14(ang);
        int px = x + round_fp18(xr_fp4 * (long)cs);
        int py = y - round_fp18(yr_fp4 * (long)sn);
        if (i == 0) {
            first_px = prev_px = px;
            first_py = prev_py = py;
            if (is_planar) raw_line_segment_ex(px, py, px, py, color, 1);
            else raw_put_pixel(px, py, color);
        } else if (px != prev_px || py != prev_py) {
            raw_line_segment_ex(prev_px, prev_py, px, py, color, is_planar);
            prev_px = px;
            prev_py = py;
        }
    }
    if (full_circle && (prev_px != first_px || prev_py != first_py))
        raw_line_segment_ex(prev_px, prev_py, first_px, first_py, color, is_planar);
    if (ray_start) raw_line_segment_ex(x, y, first_px, first_py, color, is_planar);
    if (ray_end) raw_line_segment_ex(x, y, prev_px, prev_py, color, is_planar);

    if (is_planar) reset_planar_gc();
    return NULL;
}

typedef struct {
    short x1, x2, y, dy;
} PaintSpan;

#define PAINT_MAX_SPANS 768

static void unpack_planar_row(int y, unsigned char __far *row_buf)
{
    const unsigned char __far *vram =
        (const unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
    unsigned stride = screen_mode == 7 ? 40U : 80U;
    unsigned b_cnt = (unsigned)gfx_width >> 3;
    unsigned row_off = (unsigned)y * stride;
    unsigned b;
    int p;
    _fmemset(row_buf, 0, (unsigned)gfx_width);
    for (p = 0; p < 4; ++p) {
        unsigned char pbit = (unsigned char)(1U << p);
        outp(0x3ce, 0x04);
        outp(0x3cf, p);
        for (b = 0; b < b_cnt; ++b) {
            unsigned char vb = vram[row_off + b];
            unsigned char __far *dst;
            if (!vb) continue;
            dst = row_buf + (b << 3);
            if (vb & 0x80U) dst[0] |= pbit;
            if (vb & 0x40U) dst[1] |= pbit;
            if (vb & 0x20U) dst[2] |= pbit;
            if (vb & 0x10U) dst[3] |= pbit;
            if (vb & 0x08U) dst[4] |= pbit;
            if (vb & 0x04U) dst[5] |= pbit;
            if (vb & 0x02U) dst[6] |= pbit;
            if (vb & 0x01U) dst[7] |= pbit;
        }
    }
    outp(0x3ce, 0x04);
    outp(0x3cf, 0x00);
}

static int paint_fillable(int x, int y, int fill_c, int border_c)
{
    int c = raw_get_pixel(x, y);
    return c >= 0 && c != border_c && c != fill_c;
}

const char *ckb_paint(int x, int y, int fill_color, int border_color)
{
    PaintSpan __far *stack;
    unsigned char __far *row_buf = NULL;
    int is_planar = (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12);
    int cached_y = -1;
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

    stack = (PaintSpan __far *)_fmalloc(PAINT_MAX_SPANS * sizeof(PaintSpan) +
                                        (is_planar ? (unsigned)gfx_width : 0U));
    if (!stack) return "Out of memory";
    if (is_planar) row_buf = (unsigned char __far *)(stack + PAINT_MAX_SPANS);

    #define P_FILLABLE(PX, PY) (is_planar \
        ? ((int)row_buf[PX] != border_color && (int)row_buf[PX] != fill_color) \
        : paint_fillable((PX), (PY), fill_color, border_color))

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
        if (is_planar && y != cached_y) {
            unpack_planar_row(y, row_buf);
            cached_y = y;
        }
        x = x1;
        if (P_FILLABLE(x, y)) {
            while (x > 0 && P_FILLABLE(x - 1, y))
                --x;
            if (x < x1) PUSH_SPAN(x, x1 - 1, y, -dy);
        }
        while (x1 <= x2) {
            l = x;
            while (x < gfx_width && P_FILLABLE(x, y))
                ++x;
            if (x > l) {
                raw_hline(l, x - 1, y, fill_color);
                if (is_planar) _fmemset(row_buf + l, fill_color, (unsigned)(x - l));
                PUSH_SPAN(l, x - 1, y, dy);
                if (x > x2 + 1) PUSH_SPAN(x2 + 1, x - 1, y, -dy);
            }
            ++x;
            while (x <= x2 && !P_FILLABLE(x, y))
                ++x;
            x1 = x;
        }
    }

    #undef P_FILLABLE
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
                        unsigned char CKB_FAR *buffer, unsigned capacity)
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
    _fmemset(buffer + 4, 0, total - 4U);

    if (screen_mode == 7 || screen_mode == 8 || screen_mode == 9 || screen_mode == 12) {
        unsigned pbytes = (width + 7U) >> 3;
        const unsigned char __far *vram =
            (const unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned sh = (unsigned)left & 7U;
        unsigned b0 = (unsigned)left >> 3;
        unsigned char tail_mask = (unsigned char)(0xff00U >> ((width & 7U) ? (width & 7U) : 8U));
        unsigned b;
        int p;
        for (p = 0; p < 4; ++p) {
            outp(0x3ce, 0x04);
            outp(0x3cf, p);
            for (ry = 0; ry < height; ++ry) {
                unsigned char CKB_FAR *pdst = buffer + 4U + ry * rbytes + (unsigned)p * pbytes;
                const unsigned char __far *srow = vram + (unsigned)(top + (int)ry) * stride + b0;
                if (sh == 0U) {
                    for (b = 0; b < pbytes; ++b) pdst[b] = srow[b];
                } else {
                    for (b = 0; b < pbytes; ++b)
                        pdst[b] = (unsigned char)((srow[b] << sh) | (srow[b + 1U] >> (8U - sh)));
                }
                pdst[pbytes - 1U] &= tail_mask;
            }
        }
        outp(0x3ce, 0x04);
        outp(0x3cf, 0x00);
    } else {
        for (ry = 0; ry < height; ++ry) {
            unsigned char CKB_FAR *row_dst = buffer + 4U + ry * rbytes;
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

const char *ckb_gfx_put(int x, int y, const unsigned char CKB_FAR *buffer,
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
        volatile unsigned char __far *vram =
            (volatile unsigned char __far *)MK_FP(0xa000U + vram_page_seg_off, 0);
        unsigned stride = screen_mode == 7 ? 40U : 80U;
        unsigned pbytes = (width + 7U) >> 3;
        unsigned b_start = (unsigned)x >> 3;
        unsigned b_end = (unsigned)(x + (int)width - 1) >> 3;
        unsigned nb = b_end - b_start + 1U;
        unsigned sh = (unsigned)x & 7U;
        unsigned char lmask = (unsigned char)(0xffU >> sh);
        unsigned char rmask = (unsigned char)(0xff00U >> ((((unsigned)x + width - 1U) & 7U) + 1U));
        unsigned char tail_mask = (unsigned char)(0xff00U >> ((width & 7U) ? (width & 7U) : 8U));
        int alu = action == CKB_PUT_AND ? 0x08 :
                  action == CKB_PUT_OR ? 0x10 :
                  (action == CKB_PUT_PSET || action == CKB_PUT_PRESET) ? 0x00 : 0x18;
        unsigned char cur_bmask = 0;
        int p;

        outp(0x3ce, 0x05);
        outp(0x3cf, 0x00);
        outp(0x3ce, 0x01);
        outp(0x3cf, 0x00);
        outp(0x3ce, 0x03);
        outp(0x3cf, alu);
        outp(0x3ce, 0x08);
        for (p = 0; p < 4; ++p) {
            outp(0x3c4, 0x02);
            outp(0x3c5, 1 << p);
            for (ry = 0; ry < height; ++ry) {
                const unsigned char CKB_FAR *psrc = buffer + 4U + ry * rbytes + (unsigned)p * pbytes;
                unsigned row_off = (unsigned)(y + (int)ry) * stride + b_start;
                unsigned db;
                for (db = 0; db < nb; ++db) {
                    unsigned char s_cur = (db < pbytes)
                        ? (db == pbytes - 1U ? (unsigned char)(psrc[db] & tail_mask) : psrc[db])
                        : 0U;
                    unsigned char s_prev = (db > 0U && db - 1U < pbytes)
                        ? (db - 1U == pbytes - 1U ? (unsigned char)(psrc[db - 1U] & tail_mask) : psrc[db - 1U])
                        : 0U;
                    unsigned char val = (sh == 0U) ? s_cur
                        : (unsigned char)((s_prev << (8U - sh)) | (s_cur >> sh));
                    unsigned char bmask = (nb == 1U) ? (unsigned char)(lmask & rmask)
                        : (db == 0U ? lmask : (db == nb - 1U ? rmask : 0xffU));
                    volatile unsigned char latch;

                    if (action == CKB_PUT_PRESET) val = (unsigned char)(~val);
                    if ((action == CKB_PUT_XOR || action == CKB_PUT_OR) && (val & bmask) == 0U)
                        continue;
                    if (bmask != cur_bmask) {
                        outp(0x3cf, bmask);
                        cur_bmask = bmask;
                    }
                    latch = vram[row_off + db];
                    (void)latch;
                    vram[row_off + db] = val;
                }
            }
        }
        outp(0x3c4, 0x02);
        outp(0x3c5, 0x0f);
        reset_planar_gc();
        return NULL;
    }

    for (ry = 0; ry < height; ++ry) {
        const unsigned char CKB_FAR *row_src = buffer + 4U + ry * rbytes;
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
    double dbda;
    if (timer_has_prev) {
        while (pit_elapsed(timer_prev_total, total) < 600UL)
            total = read_pit_sample(&bda, &sub);
    }
    timer_has_prev = 1;
    timer_prev_total = total;
    if (bda == 0) return (double)sub * 8.380953445225638e-7;
    dbda = (double)bda;
    ((unsigned short *)&dbda)[3] = (unsigned short)(((unsigned short *)&dbda)[3] + (16U << 4));
    return (dbda + (double)sub) * 8.380953445225638e-7;
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

static int round_nearest(double v)
{
    return v >= 0.0 ? (int)(v + 0.5) : -(int)(-v + 0.5);
}

const char *ckb_view_gfx(int reset, int is_screen, int x1, int y1, int x2, int y2,
                         int fill_color, int border_color)
{
    int left, right, top, bottom;
    if (screen_mode == 0) return "Illegal function call";
    if (reset) {
        vp_active = 0;
        vp_screen = 0;
        vp_x1 = 0;
        vp_y1 = 0;
        vp_x2 = gfx_width - 1;
        vp_y2 = gfx_height - 1;
        last_x = gfx_width / 2;
        last_y = gfx_height / 2;
        return NULL;
    }
    left = x1 < x2 ? x1 : x2;
    right = x1 < x2 ? x2 : x1;
    top = y1 < y2 ? y1 : y2;
    bottom = y1 < y2 ? y2 : y1;
    if (left == right || top == bottom || left < 0 || top < 0 ||
        right >= gfx_width || bottom >= gfx_height)
        return "Illegal function call";

    vp_active = 0;
    if (fill_color >= 0) raw_fill_rect(left, top, right, bottom, fill_color);
    if (border_color >= 0) {
        raw_hline(left - 1, right + 1, top - 1, border_color);
        raw_hline(left - 1, right + 1, bottom + 1, border_color);
        raw_vline(left - 1, top - 1, bottom + 1, border_color);
        raw_vline(right + 1, top - 1, bottom + 1, border_color);
    }
    vp_x1 = left;
    vp_y1 = top;
    vp_x2 = right;
    vp_y2 = bottom;
    vp_screen = is_screen ? 1 : 0;
    vp_active = 1;
    last_x = (left + right) / 2;
    last_y = (top + bottom) / 2;
    return NULL;
}

const char *ckb_window(int reset, int is_screen, double x1, double y1,
                       double x2, double y2)
{
    if (screen_mode == 0) return "Illegal function call";
    if (reset) {
        win_active = 0;
        win_screen = 0;
        return NULL;
    }
    if (x1 == x2 || y1 == y2) return "Illegal function call";
    win_active = 1;
    win_screen = is_screen ? 1 : 0;
    if (x1 < x2) { win_x1 = x1; win_x2 = x2; }
    else { win_x1 = x2; win_x2 = x1; }
    if (y1 < y2) { win_y1 = y1; win_y2 = y2; }
    else { win_y1 = y2; win_y2 = y1; }
    return NULL;
}

double ckb_pmap(double coord, int func, const char **err_out)
{
    double vw = (double)((vp_active ? (vp_x2 - vp_x1) : (gfx_width - 1)));
    double vh = (double)((vp_active ? (vp_y2 - vp_y1) : (gfx_height - 1)));
    double base_x = (vp_active && vp_screen) ? (double)vp_x1 : 0.0;
    double base_y = (vp_active && vp_screen) ? (double)vp_y1 : 0.0;
    *err_out = NULL;
    if (screen_mode == 0 || func < 0 || func > 3) {
        *err_out = "Illegal function call";
        return 0.0;
    }
    if (!win_active) return (double)round_nearest(coord);
    switch (func) {
    case 0:
        return (double)round_nearest(base_x + ((coord - win_x1) * vw) / (win_x2 - win_x1));
    case 1:
        if (win_screen)
            return (double)round_nearest(base_y + ((coord - win_y1) * vh) / (win_y2 - win_y1));
        return (double)round_nearest(base_y + vh - ((coord - win_y1) * vh) / (win_y2 - win_y1));
    case 2:
        return win_x1 + ((coord - base_x) * (win_x2 - win_x1)) / vw;
    case 3:
        if (win_screen)
            return win_y1 + ((coord - base_y) * (win_y2 - win_y1)) / vh;
        return win_y1 + ((vh - (coord - base_y)) * (win_y2 - win_y1)) / vh;
    }
    return 0.0;
}

double ckb_point_info(int func, const char **err_out)
{
    int vx, vy;
    const char *e = NULL;
    *err_out = NULL;
    if (screen_mode == 0 || func < 0 || func > 3) {
        *err_out = "Illegal function call";
        return 0.0;
    }
    vx = last_x - ((vp_active && !vp_screen) ? vp_x1 : 0);
    vy = last_y - ((vp_active && !vp_screen) ? vp_y1 : 0);
    if (func == 0) return (double)vx;
    if (func == 1) return (double)vy;
    if (!win_active) return (double)(func == 2 ? vx : vy);
    return ckb_pmap((double)(func == 2 ? vx : vy), func, &e);
}

void ckb_gfx_map_coord(double wx, double wy, int is_step, int *px_out, int *py_out)
{
    int ox = (vp_active && !vp_screen) ? vp_x1 : 0;
    int oy = (vp_active && !vp_screen) ? vp_y1 : 0;
    if (!win_active) {
        int ix = round_nearest(wx);
        int iy = round_nearest(wy);
        if (is_step) {
            *px_out = last_x + ix;
            *py_out = last_y + iy;
        } else {
            *px_out = ox + ix;
            *py_out = oy + iy;
        }
        return;
    }
    {
        const char *e = NULL;
        if (is_step) {
            double cur_wx = ckb_point_info(2, &e);
            double cur_wy = ckb_point_info(3, &e);
            wx += cur_wx;
            wy += cur_wy;
        }
        *px_out = ox + (int)ckb_pmap(wx, 0, &e) - ((vp_active && vp_screen) ? vp_x1 : 0);
        *py_out = oy + (int)ckb_pmap(wy, 1, &e) - ((vp_active && vp_screen) ? vp_y1 : 0);
    }
}

static int parse_draw_int(const char **pp, const char *end, int def_val, int *has_val)
{
    const char *p = *pp;
    int sign = 1;
    long v = 0;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p < end && (*p == '+' || *p == '-')) {
        if (*p == '-') sign = -1;
        ++p;
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
    }
    if (p >= end || !isdigit((unsigned char)*p)) {
        if (has_val) *has_val = 0;
        return def_val;
    }
    while (p < end && isdigit((unsigned char)*p)) {
        v = v * 10L + (long)(*p - '0');
        ++p;
    }
    *pp = p;
    if (has_val) *has_val = 1;
    return (int)(v * sign);
}

static void rotate_draw_vec(long dx, long dy, int *rx_out, int *ry_out)
{
    long sx = (dx * (long)draw_scale + (dx >= 0 ? 2L : -2L)) / 4L;
    long sy = (dy * (long)draw_scale + (dy >= 0 ? 2L : -2L)) / 4L;
    int a = ((draw_angle % 360) + 360) % 360;
    if (a == 0) { *rx_out = (int)sx; *ry_out = (int)sy; return; }
    if (a == 90) { *rx_out = (int)sy; *ry_out = (int)(-sx); return; }
    if (a == 180) { *rx_out = (int)(-sx); *ry_out = (int)(-sy); return; }
    if (a == 270) { *rx_out = (int)(-sy); *ry_out = (int)sx; return; }
    {
        double rad = (double)a * (TWO_PI / 360.0);
        double cs = cos(rad), sn = sin(rad);
        /* Screen Y grows downward; CCW rotation maps (dx, dy) -> (dx*cos + dy*sin, -dx*sin + dy*cos) */
        *rx_out = round_nearest((double)sx * cs + (double)sy * sn);
        *ry_out = round_nearest(-(double)sx * sn + (double)sy * cs);
    }
}

static const char *draw_exec(const char *mml, unsigned length,
                             CkbVarptrStrResolver resolver, unsigned depth)
{
    const char *p = mml;
    const char *end = mml + length;
    int blind = 0, no_update = 0;

    if (screen_mode == 0 || depth > 8U) return "Illegal function call";

    while (p < end) {
        int ch = toupper((unsigned char)*p++);
        long dx = 0, dy = 0;
        int is_move = 0;

        if (ch == ' ' || ch == '\t' || ch == ';') continue;
        if (ch == 'B') { blind = 1; continue; }
        if (ch == 'N') { no_update = 1; continue; }

        switch (ch) {
        case 'U': dy = -1; is_move = 1; break;
        case 'D': dy = 1; is_move = 1; break;
        case 'L': dx = -1; is_move = 1; break;
        case 'R': dx = 1; is_move = 1; break;
        case 'E': dx = 1; dy = -1; is_move = 1; break;
        case 'F': dx = 1; dy = 1; is_move = 1; break;
        case 'G': dx = -1; dy = 1; is_move = 1; break;
        case 'H': dx = -1; dy = -1; is_move = 1; break;
        case 'M': {
            int rel = 0, mx, my, has_x = 0, has_y = 0, tx, ty, sx, sy;
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            if (p < end && (*p == '+' || *p == '-')) rel = 1;
            mx = parse_draw_int(&p, end, 0, &has_x);
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            if (!has_x || p >= end || *p != ',') return "Illegal function call";
            ++p;
            my = parse_draw_int(&p, end, 0, &has_y);
            if (!has_y) return "Illegal function call";
            sx = last_x;
            sy = last_y;
            if (rel) {
                int rdx, rdy;
                rotate_draw_vec(mx, my, &rdx, &rdy);
                tx = sx + rdx;
                ty = sy + rdy;
            } else {
                tx = ((vp_active && !vp_screen) ? vp_x1 : 0) + mx;
                ty = ((vp_active && !vp_screen) ? vp_y1 : 0) + my;
            }
            if (!blind) raw_line_segment(sx, sy, tx, ty, fg_color);
            if (!no_update) { last_x = tx; last_y = ty; }
            blind = no_update = 0;
            continue;
        }
        case 'C': {
            int has_c = 0, c = parse_draw_int(&p, end, 0, &has_c);
            if (!has_c || c < 0 || c > max_color) return "Illegal function call";
            fg_color = c;
            continue;
        }
        case 'S': {
            int has_s = 0, s = parse_draw_int(&p, end, 0, &has_s);
            if (!has_s || s < 1 || s > 255) return "Illegal function call";
            draw_scale = s;
            continue;
        }
        case 'A': {
            int has_a = 0, a = parse_draw_int(&p, end, 0, &has_a);
            if (!has_a || a < 0 || a > 3) return "Illegal function call";
            draw_angle = a * 90;
            continue;
        }
        case 'T': {
            int has_ta = 0, ta;
            if (p >= end || toupper((unsigned char)*p) != 'A') return "Illegal function call";
            ++p;
            ta = parse_draw_int(&p, end, 0, &has_ta);
            if (!has_ta || ta < -360 || ta > 360) return "Illegal function call";
            draw_angle = ta;
            continue;
        }
        case 'P': {
            int has_fc = 0, has_bc = 0, fc, bc;
            const char *err;
            fc = parse_draw_int(&p, end, 0, &has_fc);
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            if (!has_fc || p >= end || *p != ',') return "Illegal function call";
            ++p;
            bc = parse_draw_int(&p, end, 0, &has_bc);
            if (!has_bc || fc < 0 || fc > max_color || bc < 0 || bc > max_color)
                return "Illegal function call";
            err = ckb_paint(last_x, last_y, fc, bc);
            if (err) return err;
            continue;
        }
        case 'X': {
            unsigned sub_len = 0;
            const char *sub_str;
            const char *err;
            if (!resolver || (unsigned)(end - p) < 3U) return "Illegal function call";
            sub_str = resolver((const unsigned char *)p, &sub_len);
            p += 3;
            if (!sub_str) return "Illegal function call";
            err = draw_exec(sub_str, sub_len, resolver, depth + 1U);
            if (err) return err;
            continue;
        }
        default:
            return "Illegal function call";
        }

        if (is_move) {
            int dist = parse_draw_int(&p, end, 1, NULL);
            int rdx, rdy, sx = last_x, sy = last_y, tx, ty;
            rotate_draw_vec(dx * (long)dist, dy * (long)dist, &rdx, &rdy);
            tx = sx + rdx;
            ty = sy + rdy;
            if (!blind) raw_line_segment(sx, sy, tx, ty, fg_color);
            if (!no_update) { last_x = tx; last_y = ty; }
            blind = no_update = 0;
        }
    }
    return NULL;
}

const char *ckb_draw(const char *mml, unsigned length, CkbVarptrStrResolver resolver)
{
    return draw_exec(mml, length, resolver, 0U);
}

void ckb_beep(void)
{
    union REGS regs = {0};
    if (!tty_console()) return;
    /* Dispatch BIOS INT 10h teletype BEL (AX=0E07h) just as QBasic 1.0 does;
     * the active VGA BIOS owns BEL tone generation. */
    regs.x.ax = 0x0e07;
    regs.x.bx = 0x0000;
    int86(0x10, &regs, &regs);
}

static void enqueue_note(unsigned div, unsigned ticks, int fg_wait,
                         volatile int *break_flag)
{
    if (!ticks) return;
    if (fg_wait) {
        while (ckb_snd_active()) {
            if (break_flag && *break_flag) { ckb_snd_stop(); return; }
        }
    }
    while (!ckb_snd_push(div, ticks)) {
        if (break_flag && *break_flag) { ckb_snd_stop(); return; }
    }
}

const char *ckb_sound(double freq, double ticks, volatile int *break_flag)
{
    unsigned bticks, div;
    if (ticks < 0.0 || ticks > 65535.0) return "Illegal function call";
    if (ticks == 0.0) {
        ckb_snd_stop();
        return NULL;
    }
    if (freq < 37.0 || freq > 32767.0) return "Illegal function call";
    if (!tty_console()) return NULL;
    div = (unsigned)(1193182.0 / freq + 0.5);
    if (div < 18U) div = 18U;
    bticks = (unsigned)(ticks + 0.5);
    if (!bticks) bticks = 1U;
    enqueue_note(div, bticks, 1, break_flag);
    return NULL;
}

static unsigned parse_mml_uint_len(const char **pp, const char *end, unsigned def_val)
{
    const char *p = *pp;
    unsigned v = 0;
    while (p < end && *p == ' ') ++p;
    if (p >= end || !isdigit((unsigned char)*p)) return def_val;
    while (p < end && isdigit((unsigned char)*p)) {
        v = v * 10U + (unsigned)(*p - '0');
        ++p;
    }
    *pp = p;
    return v;
}

static unsigned alloc_mml_bios_ticks(unsigned long pit_ticks)
{
    unsigned long total = pit_ticks + play_frac_rem;
    unsigned bticks = (unsigned)(total >> 16);
    play_frac_rem = total & 0xffffUL;
    return bticks ? bticks : 1U;
}

static const char *play_exec(const char *mml, unsigned length,
                             volatile int *break_flag,
                             CkbVarptrStrResolver resolver, unsigned depth)
{
    /* 16x PIT divisors for O0: C, C#, D, D#, E, F, F#, G, G#, A, A#, B */
    static const unsigned long base_div16[12] = {
        291867UL, 275486UL, 260024UL, 245430UL, 231655UL, 218653UL,
        206381UL, 194798UL, 183865UL, 173545UL, 163805UL, 154611UL
    };
    static const int letter_semi[7] = {9, 11, 0, 2, 4, 5, 7}; /* A..G */
    const char *p = mml;
    const char *end = mml + length;

    if (depth > 8U) return "Illegal function call";

    while (p < end) {
        int ch = toupper((unsigned char)*p++);
        if (ch == ' ' || ch == ';') continue;
        if (break_flag && *break_flag) { ckb_snd_stop(); break; }

        if (ch == 'O') {
            unsigned oct = parse_mml_uint_len(&p, end, 99U);
            if (oct > 6U) { ckb_snd_stop(); return "Illegal function call"; }
            play_octave = (int)oct;
        } else if (ch == '<') {
            if (play_octave > 0) --play_octave;
        } else if (ch == '>') {
            if (play_octave < 6) ++play_octave;
        } else if (ch == 'L') {
            unsigned len = parse_mml_uint_len(&p, end, 0U);
            if (len < 1U || len > 64U) { ckb_snd_stop(); return "Illegal function call"; }
            play_length = (int)len;
        } else if (ch == 'T') {
            unsigned t = parse_mml_uint_len(&p, end, 0U);
            if (t < 32U || t > 255U) { ckb_snd_stop(); return "Illegal function call"; }
            play_tempo = (int)t;
        } else if (ch == 'M') {
            int sub;
            while (p < end && *p == ' ') ++p;
            if (p >= end) { ckb_snd_stop(); return "Illegal function call"; }
            sub = toupper((unsigned char)*p++);
            if (sub == 'N') play_style = 0;
            else if (sub == 'L') play_style = 1;
            else if (sub == 'S') play_style = 2;
            else if (sub == 'F') play_bg = 0;
            else if (sub == 'B') play_bg = 1;
            else { ckb_snd_stop(); return "Illegal function call"; }
        } else if (ch == 'P') {
            unsigned len = parse_mml_uint_len(&p, end, (unsigned)play_length);
            unsigned long whole_ticks, dur_ticks, add_ticks;
            if (len < 1U || len > 64U) { ckb_snd_stop(); return "Illegal function call"; }
            whole_ticks = (4UL * 60UL * 1193182UL) / (unsigned long)play_tempo;
            dur_ticks = whole_ticks / (unsigned long)len;
            add_ticks = dur_ticks >> 1;
            while (p < end && *p == '.') {
                dur_ticks += add_ticks;
                add_ticks >>= 1;
                ++p;
            }
            if (tty_console())
                enqueue_note(0U, alloc_mml_bios_ticks(dur_ticks), !play_bg, break_flag);
        } else if (ch == 'N') {
            unsigned note_num = parse_mml_uint_len(&p, end, 999U);
            unsigned long whole_ticks, dur_ticks, add_ticks;
            if (note_num > 84U) { ckb_snd_stop(); return "Illegal function call"; }
            whole_ticks = (4UL * 60UL * 1193182UL) / (unsigned long)play_tempo;
            dur_ticks = whole_ticks / (unsigned long)play_length;
            add_ticks = dur_ticks >> 1;
            while (p < end && *p == '.') {
                dur_ticks += add_ticks;
                add_ticks >>= 1;
                ++p;
            }
            if (tty_console()) {
                unsigned bticks = alloc_mml_bios_ticks(dur_ticks);
                if (note_num == 0U) {
                    enqueue_note(0U, bticks, !play_bg, break_flag);
                } else {
                    int oct = (int)((note_num - 1U) / 12U);
                    int semi = (int)((note_num - 1U) % 12U);
                    unsigned shift = (unsigned)(oct + 4);
                    unsigned div = (unsigned)((base_div16[semi] + (1UL << (shift - 1U))) >> shift);
                    unsigned on_bt = play_style == 1 ? bticks :
                                     play_style == 2 ? (bticks * 3U + 2U) >> 2 :
                                                       (bticks * 7U + 4U) >> 3;
                    if (!on_bt) on_bt = 1U;
                    enqueue_note(div, on_bt, !play_bg, break_flag);
                    if (bticks > on_bt)
                        enqueue_note(0U, bticks - on_bt, !play_bg, break_flag);
                }
            }
        } else if (ch >= 'A' && ch <= 'G') {
            int semi = letter_semi[ch - 'A'];
            int oct = play_octave;
            unsigned len;
            unsigned long whole_ticks, dur_ticks, add_ticks;
            while (p < end && *p == ' ') ++p;
            if (p < end && (*p == '#' || *p == '+')) { ++semi; ++p; }
            else if (p < end && *p == '-') { --semi; ++p; }
            if (semi < 0) { semi += 12; if (oct > 0) --oct; }
            else if (semi >= 12) { semi -= 12; if (oct < 6) ++oct; }
            len = parse_mml_uint_len(&p, end, (unsigned)play_length);
            if (len < 1U || len > 64U) { ckb_snd_stop(); return "Illegal function call"; }
            whole_ticks = (4UL * 60UL * 1193182UL) / (unsigned long)play_tempo;
            dur_ticks = whole_ticks / (unsigned long)len;
            add_ticks = dur_ticks >> 1;
            while (p < end && *p == '.') {
                dur_ticks += add_ticks;
                add_ticks >>= 1;
                ++p;
            }
            if (tty_console()) {
                unsigned bticks = alloc_mml_bios_ticks(dur_ticks);
                unsigned shift = (unsigned)(oct + 4);
                unsigned div = (unsigned)((base_div16[semi] + (1UL << (shift - 1U))) >> shift);
                unsigned on_bt = play_style == 1 ? bticks :
                                 play_style == 2 ? (bticks * 3U + 2U) >> 2 :
                                                   (bticks * 7U + 4U) >> 3;
                if (!on_bt) on_bt = 1U;
                enqueue_note(div, on_bt, !play_bg, break_flag);
                if (bticks > on_bt)
                    enqueue_note(0U, bticks - on_bt, !play_bg, break_flag);
            }
        } else if (ch == 'X') {
            unsigned sub_len = 0;
            const char *sub_str;
            const char *err;
            if (!resolver || (unsigned)(end - p) < 3U) {
                ckb_snd_stop();
                return "Illegal function call";
            }
            sub_str = resolver((const unsigned char *)p, &sub_len);
            p += 3;
            if (!sub_str) { ckb_snd_stop(); return "Illegal function call"; }
            err = play_exec(sub_str, sub_len, break_flag, resolver, depth + 1U);
            if (err) return err;
        } else {
            ckb_snd_stop();
            return "Illegal function call";
        }
    }
    return NULL;
}

const char *ckb_play(const char *mml, unsigned length, volatile int *break_flag,
                     CkbVarptrStrResolver resolver)
{
    return play_exec(mml, length, break_flag, resolver, 0U);
}

int ckb_play_queued(void)
{
    return (int)ckb_snd_queued();
}

#else

/* Host stub implementation when compiled outside DOS. */
void ckb_set_display_flags(unsigned flags) { (void)flags; }
unsigned ckb_get_display_flags(void) { return 0; }
const char *ckb_screen(int mode) { return mode == 0 ? NULL : "Illegal function call"; }
const char *ckb_screen_ex(int mode, int cs, int ap, int vp) {
    (void)cs; (void)ap; (void)vp;
    return mode <= 0 ? NULL : "Illegal function call";
}
const char *ckb_pcopy(int s, int d) { (void)s; (void)d; return "Illegal function call"; }
const char *ckb_width_screen(int cols, int rows) { (void)cols; (void)rows; return NULL; }
const char *ckb_palette(int attr, long color) { (void)attr; (void)color; return NULL; }
void ckb_color(int foreground, int background) { (void)foreground; (void)background; }
void ckb_cls_mode(int mode) { (void)mode; }
void ckb_cls(void) {}
const char *ckb_locate(int row, int col, int cursor) { (void)row; (void)col; (void)cursor; return NULL; }
const char *ckb_view_print(int top, int bottom) { (void)top; (void)bottom; return NULL; }
const char *ckb_view_gfx(int r, int s, int x1, int y1, int x2, int y2, int fc, int bc) {
    (void)r; (void)s; (void)x1; (void)y1; (void)x2; (void)y2; (void)fc; (void)bc;
    return "Illegal function call";
}
const char *ckb_window(int r, int s, double x1, double y1, double x2, double y2) {
    (void)r; (void)s; (void)x1; (void)y1; (void)x2; (void)y2;
    return "Illegal function call";
}
double ckb_pmap(double c, int f, const char **e) {
    (void)c; (void)f; *e = "Illegal function call"; return 0.0;
}
double ckb_point_info(int f, const char **e) {
    (void)f; *e = "Illegal function call"; return 0.0;
}
void ckb_gfx_map_coord(double wx, double wy, int is_step, int *px, int *py) {
    (void)is_step; *px = (int)wx; *py = (int)wy;
}
int ckb_window_active(void) { return 0; }
int ckb_csrlin(void) { return 1; }
int ckb_pos_col(void) { return 1; }
int ckb_screen_cell(int r, int c, int f, const char **e) {
    (void)r; (void)c; (void)f; *e = NULL; return 32;
}
int ckb_con_direct_active(void) { return 0; }
unsigned ckb_con_column(void) { return 0; }
unsigned ckb_con_width(void) { return 80; }
void ckb_con_write(const char *text, unsigned length) { fwrite(text, 1, length, stdout); }
void ckb_con_newline(void) { fputc('\n', stdout); }
void ckb_con_reset_run(void) {}
void ckb_con_reset_text(void) {}
void ckb_con_acknowledge_graphics(void) {}
void ckb_con_sync_cursor_hw(void) {}
void ckb_con_restore_cursor(void) {}
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
const char *ckb_draw(const char *m, unsigned l, CkbVarptrStrResolver r) {
    (void)m; (void)l; (void)r; return "Illegal function call";
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
const char *ckb_play(const char *mml, unsigned len, volatile int *break_flag,
                     CkbVarptrStrResolver r) {
    (void)mml; (void)len; (void)break_flag; (void)r; return NULL;
}
int ckb_play_queued(void) { return 0; }
void ckb_speaker_off(void) {}

#endif
