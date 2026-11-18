/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBCON_H
#define CKBCON_H

#if defined(__WATCOMC__) || defined(_MSDOS)
#define CKB_FAR __far
#else
#define CKB_FAR
#endif

enum {
    CKB_PUT_XOR = 0,
    CKB_PUT_PSET = 1,
    CKB_PUT_PRESET = 2,
    CKB_PUT_AND = 3,
    CKB_PUT_OR = 4
};

enum {
    CKB_DISP_MONO = 1,
    CKB_DISP_CGA_FAST = 2,
    CKB_DISP_HIGH_ROWS = 4,
    CKB_DISP_NOHI = 8
};

typedef const char *(*CkbVarptrStrResolver)(const unsigned char *vp3, unsigned *len_out);

/* Omitted arguments use -1 where applicable. */
void ckb_set_display_flags(unsigned flags);
unsigned ckb_get_display_flags(void);
const char *ckb_screen(int mode);
const char *ckb_screen_ex(int mode, int colorswitch, int apage, int vpage);
const char *ckb_pcopy(int src_page, int dst_page);
const char *ckb_width_screen(int cols, int rows);
const char *ckb_palette(int attr, long color);
void ckb_color(int foreground, int background);
void ckb_cls_mode(int mode);
void ckb_cls(void);
const char *ckb_locate(int row, int col, int cursor);
const char *ckb_view_print(int top, int bottom);
const char *ckb_view_gfx(int reset, int is_screen, int x1, int y1, int x2, int y2,
                         int fill_color, int border_color);
const char *ckb_window(int reset, int is_screen, double x1, double y1,
                       double x2, double y2);
double ckb_pmap(double coord, int func, const char **err_out);
double ckb_point_info(int func, const char **err_out);
void ckb_gfx_map_coord(double wx, double wy, int is_step, int *px_out, int *py_out);
int ckb_window_active(void);

int ckb_csrlin(void);
int ckb_pos_col(void);
int ckb_screen_cell(int row, int col, int color_flag, const char **err_out);

int ckb_con_direct_active(void);
unsigned ckb_con_column(void);
unsigned ckb_con_width(void);
void ckb_con_write(const char *text, unsigned length);
void ckb_con_newline(void);
void ckb_con_reset_run(void);
void ckb_con_end_run(void);
void ckb_con_reset_text(void);
void ckb_con_acknowledge_graphics(void);
void ckb_con_sync_cursor_hw(void);
void ckb_con_restore_cursor(void);

void ckb_gfx_last(int *x, int *y);
int ckb_gfx_default_color(int is_preset);
const char *ckb_pset(int x, int y, int color, int is_preset);
int ckb_point(int x, int y);
const char *ckb_line(int has_first, int x1, int y1, int x2, int y2,
                     int color, int box_mode);
const char *ckb_circle(int x, int y, double radius, int color,
                       int has_start, double start_angle,
                       int has_end, double end_angle,
                       int has_aspect, double aspect);
const char *ckb_paint(int x, int y, int fill_color, int border_color);
const char *ckb_draw(const char *mml, unsigned length, CkbVarptrStrResolver resolver);
const char *ckb_gfx_get(int x1, int y1, int x2, int y2,
                        unsigned char CKB_FAR *buffer, unsigned capacity);
const char *ckb_gfx_put(int x, int y, const unsigned char CKB_FAR *buffer,
                        unsigned capacity, int action);

double ckb_timer(void);
int ckb_inkey(char *out_two_bytes);
void ckb_sleep(long seconds, volatile int *break_flag);
void ckb_beep(void);
const char *ckb_sound(double freq, double ticks, volatile int *break_flag);
const char *ckb_play(const char *mml, unsigned length, volatile int *break_flag,
                     CkbVarptrStrResolver resolver);
int ckb_play_queued(void);
void ckb_speaker_off(void);

#endif
