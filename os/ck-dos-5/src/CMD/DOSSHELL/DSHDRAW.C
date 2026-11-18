/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "DOSSHELL.H"

#ifdef __CKCL__
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

struct dsh_theme g_theme;

void dsh_apply_theme(void)
{
    if (g_app.bw_mode || g_app.color_scheme == 1) {
        g_theme.title        = 0x07U;
        g_theme.menubar      = 0x70U;
        g_theme.statusbar    = 0x07U;
        g_theme.menu_accel   = 0x70U;
        g_theme.menu_dim     = 0x70U;
        g_theme.surface      = 0x70U;
        g_theme.inactive_hdr = 0x07U;
        g_theme.sel_active   = 0x07U;
        g_theme.sel_inactive = 0x07U;
        g_theme.dlg_box      = 0x70U;
        g_theme.dlg_input    = 0x07U;
        g_theme.dlg_btn_sel  = 0x07U;
        g_theme.shadow       = 0x07U;
        return;
    }
    if (g_app.color_scheme == 4) {
        /* Reverse monochrome */
        g_theme.title        = 0x70U;
        g_theme.menubar      = 0x07U;
        g_theme.statusbar    = 0x07U;
        g_theme.menu_accel   = 0x0FU;
        g_theme.menu_dim     = 0x07U;
        g_theme.surface      = 0x07U;
        g_theme.inactive_hdr = 0x70U;
        g_theme.sel_active   = 0x70U;
        g_theme.sel_inactive = 0x0FU;
        g_theme.dlg_box      = 0x70U;
        g_theme.dlg_input    = 0x07U;
        g_theme.dlg_btn_sel  = 0x0FU;
        g_theme.shadow       = 0x08U;
        return;
    }
    /* Default Basic Blue (0), Ocean (2), Emerald (3) */
    g_theme.title        = (g_app.color_scheme == 3) ? 0x2FU : 0x1FU;
    g_theme.menubar      = 0x30U;
    g_theme.statusbar    = 0x30U;
    g_theme.menu_accel   = 0x3FU;
    g_theme.menu_dim     = 0x37U;
    g_theme.surface      = (g_app.color_scheme == 2) ? 0xB0U : 0xF0U;
    g_theme.inactive_hdr = 0x70U;
    g_theme.sel_active   = (g_app.color_scheme == 3) ? 0x2FU : 0x1FU;
    g_theme.sel_inactive = (g_app.color_scheme == 2) ? 0xB0U : 0xF0U;
    g_theme.dlg_box      = 0x70U;
    g_theme.dlg_input    = 0x70U;
    g_theme.dlg_btn_sel  = 0x1FU;
    g_theme.shadow       = 0x78U;
}

void dsh_format_size_commas(unsigned long val, char *out, int width)
{
    char rev[20];
    int rlen = 0;
    int digits = 0;
    int i, pad;

    if (val == 0UL) {
        rev[rlen++] = '0';
    } else {
        while (val > 0UL) {
            if (digits > 0 && (digits % 3) == 0) {
                rev[rlen++] = ',';
            }
            rev[rlen++] = (char)('0' + (val % 10UL));
            val /= 10UL;
            ++digits;
        }
    }
    pad = width - rlen;
    i = 0;
    while (pad > 0) {
        out[i++] = ' ';
        --pad;
    }
    while (rlen > 0) {
        out[i++] = rev[--rlen];
    }
    out[i] = '\0';
}

void dsh_format_date(unsigned short wr_date, char *out)
{
    unsigned m = (wr_date >> 5) & 0x0FU;
    unsigned d = wr_date & 0x1FU;
    unsigned y = ((wr_date >> 9) & 0x7FU) + 1980U;
    unsigned yy = y % 100U;
    out[0] = (char)('0' + (m / 10U));
    out[1] = (char)('0' + (m % 10U));
    out[2] = '-';
    out[3] = (char)('0' + (d / 10U));
    out[4] = (char)('0' + (d % 10U));
    out[5] = '-';
    out[6] = (char)('0' + (yy / 10U));
    out[7] = (char)('0' + (yy % 10U));
    out[8] = '\0';
}

void dsh_format_time(unsigned short wr_time, char *out)
{
    unsigned h = (wr_time >> 11) & 0x1FU;
    unsigned m = (wr_time >> 5) & 0x3FU;
    char ap = (h >= 12U) ? 'p' : 'a';
    unsigned h12 = h % 12U;
    if (h12 == 0U) h12 = 12U;
    out[0] = (h12 >= 10U) ? (char)('0' + (h12 / 10U)) : ' ';
    out[1] = (char)('0' + (h12 % 10U));
    out[2] = ':';
    out[3] = (char)('0' + (m / 10U));
    out[4] = (char)('0' + (m % 10U));
    out[5] = ap;
    out[6] = '\0';
}

static void get_dos_clock_str(char *out)
{
#ifdef __CKCL__
    union REGS regs;
    unsigned short packed;
    regs.h.ah = 0x2CU;
    intdos(&regs, &regs);
    packed = (unsigned short)(((unsigned short)regs.h.ch << 11) |
                              ((unsigned short)regs.h.cl << 5));
    dsh_format_time(packed, out);
#else
    strcpy(out, " 5:00a");
#endif
}

static void write_centered(unsigned row, unsigned left, unsigned width,
                           unsigned attr, const char *text)
{
    unsigned len = (unsigned)strlen(text);
    unsigned col = left;
    dsh_ui_fill(row, left, ' ', attr, width);
    if (len < width) {
        col = left + (width - len) / 2U;
    }
    dsh_ui_write(row, col, attr, text);
}

void dsh_draw_box(unsigned top, unsigned left, unsigned bottom, unsigned right,
                  unsigned attr, const char *title, int shadow)
{
    unsigned r;
    unsigned inner_w;
    if (right <= left || bottom <= top) return;
    inner_w = right - left - 1U;

    dsh_ui_put_cell(top, left, CH_BOX_TL, attr);
    dsh_ui_fill(top, left + 1U, CH_BOX_H, attr, inner_w);
    dsh_ui_put_cell(top, right, CH_BOX_TR, attr);

    if (title && title[0] != '\0') {
        char tbuf[64];
        unsigned tlen = (unsigned)strlen(title);
        if (tlen + 2U < inner_w && tlen < 58U) {
            unsigned tcol;
            tbuf[0] = ' ';
            memcpy(tbuf + 1, title, tlen);
            tbuf[tlen + 1] = ' ';
            tbuf[tlen + 2] = '\0';
            tcol = left + 1U + (inner_w - (tlen + 2U)) / 2U;
            dsh_ui_write(top, tcol, attr, tbuf);
        }
    }

    for (r = top + 1U; r < bottom; ++r) {
        dsh_ui_put_cell(r, left, CH_BOX_V, attr);
        dsh_ui_fill(r, left + 1U, ' ', attr, inner_w);
        dsh_ui_put_cell(r, right, CH_BOX_V, attr);
    }

    dsh_ui_put_cell(bottom, left, CH_BOX_BL, attr);
    dsh_ui_fill(bottom, left + 1U, CH_BOX_H, attr, inner_w);
    dsh_ui_put_cell(bottom, right, CH_BOX_BR, attr);

    if (shadow && !g_app.bw_mode && g_app.color_scheme != 1) {
        dsh_ui_shadow_rect(top + 1U, right + 1U, bottom + 1U, right + 2U, g_theme.shadow);
        dsh_ui_shadow_rect(bottom + 1U, left + 2U, bottom + 1U, right + 2U, g_theme.shadow);
    }
}

static void draw_drive_bar(unsigned row, const struct dsh_panel *pan,
                           unsigned char cursor_idx, int has_focus)
{
    unsigned i;
    dsh_ui_fill(row, 0, ' ', g_theme.surface, 80);
    for (i = 0; i < g_app.drive_count && (1U + i * 6U + 4U) < 79U; ++i) {
        char dbuf[6];
        unsigned col = 1U + i * 6U;
        unsigned char drv = g_app.drives[i];
        unsigned char attr = g_theme.surface;
        dbuf[0] = '[';
        dbuf[1] = (char)('A' + drv);
        dbuf[2] = ':';
        dbuf[3] = ']';
        dbuf[4] = '\0';
        if ((has_focus && i == cursor_idx) || (!has_focus && drv == pan->drive)) {
            if (has_focus || (!g_app.bw_mode && g_app.color_scheme != 1)) {
                attr = g_theme.sel_active;
            }
        }
        dsh_ui_write(row, col, attr, dbuf);
    }
}

static void draw_tree_pane(const struct dsh_panel *pan, unsigned hdr_row,
                           unsigned top_row, unsigned bot_row, int has_focus)
{
    unsigned list_rows = bot_row - top_row;
    unsigned r;
    unsigned char hdr_attr = has_focus ? g_theme.title : g_theme.inactive_hdr;

    write_centered(hdr_row, 0, 36, hdr_attr, "Directory Tree");

    for (r = 0; r < list_rows; ++r) {
        unsigned scr_row = top_row + r;
        unsigned v_idx = pan->tree_top + r;
        dsh_ui_put_cell(scr_row, 0, CH_BOX_V, g_theme.surface);
        dsh_ui_fill(scr_row, 1, ' ', g_theme.surface, 33);
        if (r == 0) {
            dsh_ui_put_cell(scr_row, 34, CH_SCROLL_UP, g_theme.surface);
        } else if (r + 1U == list_rows) {
            dsh_ui_put_cell(scr_row, 34, CH_SCROLL_DOWN, g_theme.surface);
        } else {
            dsh_ui_put_cell(scr_row, 34, 0xB0U, g_theme.surface);
        }
        dsh_ui_put_cell(scr_row, 35, CH_BOX_V, g_theme.surface);

        if (v_idx < pan->vis_count && pan->dirs != 0) {
            unsigned short d_idx = pan->vis_dirs[v_idx];
            unsigned char depth = pan->dirs[d_idx].depth;
            unsigned char bmask = pan->dirs[d_idx].branch_mask;
            char nbuf[14];
            unsigned k = 0;
            unsigned char sign_ch = ' ';
            unsigned char name_attr = g_theme.surface;

            while (k < 12U && pan->dirs[d_idx].name[k] != '\0') {
                nbuf[k] = pan->dirs[d_idx].name[k];
                ++k;
            }
            nbuf[k] = '\0';

            if (pan->dirs[d_idx].has_children) {
                sign_ch = pan->dirs[d_idx].expanded ? '-' : '+';
            }
            if (v_idx == pan->tree_sel) {
                name_attr = has_focus ? g_theme.sel_active :
                            ((g_app.bw_mode || g_app.color_scheme == 1) ? 0x07U : g_theme.surface);
            }

            if (depth == 0) {
                char box[4];
                box[0] = '[';
                box[1] = sign_ch;
                box[2] = ']';
                box[3] = '\0';
                if (has_focus && v_idx == pan->tree_sel) {
                    dsh_ui_put_cell(scr_row, 1, CH_ARROW_RIGHT, g_theme.surface);
                }
                dsh_ui_write(scr_row, 2, (v_idx == pan->tree_sel && (g_app.bw_mode || g_app.color_scheme == 1)) ? 0x07U : g_theme.surface, box);
                dsh_ui_write(scr_row, 6, name_attr, nbuf);
            } else {
                unsigned d;
                unsigned ecol = 3U * (unsigned)depth;
                for (d = 1U; d < (unsigned)depth; ++d) {
                    if ((bmask & (1U << (d - 1U))) != 0 && (3U * d) < 33U) {
                        dsh_ui_put_cell(scr_row, 3U * d, CH_BOX_V, g_theme.surface);
                    }
                }
                if (has_focus && v_idx == pan->tree_sel) {
                    dsh_ui_put_cell(scr_row, 2, CH_ARROW_RIGHT, g_theme.surface);
                }
                if (ecol + 6U < 34U) {
                    char box[4];
                    box[0] = '[';
                    box[1] = sign_ch;
                    box[2] = ']';
                    box[3] = '\0';
                    dsh_ui_put_cell(scr_row, ecol, pan->dirs[d_idx].last_sibling ? CH_TREE_ELBOW : CH_TREE_TEE, g_theme.surface);
                    dsh_ui_put_cell(scr_row, ecol + 1U, CH_BOX_H, g_theme.surface);
                    dsh_ui_write(scr_row, ecol + 2U, g_theme.surface, box);
                    dsh_ui_write(scr_row, ecol + 6U, name_attr, nbuf);
                }
            }
        }
    }

    dsh_ui_put_cell(bot_row, 0, CH_BOX_BL, g_theme.surface);
    dsh_ui_fill(bot_row, 1, CH_BOX_H, g_theme.surface, 34);
    dsh_ui_put_cell(bot_row, 35, CH_BOX_BR, g_theme.surface);
}

static void format_file_row(const struct dsh_file_entry __far *fe, char *out, int with_time)
{
    char stem[9], ext[4], sz[12], dt[10], tm[8];
    unsigned i = 0, o = 0;

    memset(stem, ' ', 8);
    stem[8] = '\0';
    memset(ext, ' ', 3);
    ext[3] = '\0';

    while (fe->name[i] != '\0' && fe->name[i] != '.' && o < 8U) {
        stem[o++] = fe->name[i++];
    }
    while (fe->name[i] != '\0' && fe->name[i] != '.') ++i;
    if (fe->name[i] == '.') {
        ++i;
        o = 0;
        while (fe->name[i] != '\0' && o < 3U) {
            ext[o++] = fe->name[i++];
        }
    }
    dsh_format_size_commas(fe->size, sz, 9);
    dsh_format_date(fe->wr_date, dt);
    if (with_time) {
        dsh_format_time(fe->wr_time, tm);
        sprintf(out, "%s.%s  %s  %s  %s", stem, ext, sz, dt, tm);
    } else {
        sprintf(out, "%s.%s    %s  %s", stem, ext, sz, dt);
    }
}

static void draw_file_pane(const struct dsh_panel *pan, unsigned left_col,
                           unsigned hdr_row, unsigned top_row, unsigned bot_row,
                           int has_focus, int all_files_mode)
{
    unsigned width = 80U - left_col;
    unsigned list_rows = bot_row - top_row;
    unsigned r;
    unsigned char hdr_attr = has_focus ? g_theme.title : g_theme.inactive_hdr;
    char hdr_buf[140];

    if (all_files_mode) {
        strcpy(hdr_buf, g_app.filter_pat);
    } else {
        unsigned short dir_idx = (pan->vis_count > 0 && pan->tree_sel < pan->vis_count)
                                 ? pan->vis_dirs[pan->tree_sel] : 0;
        dsh_fs_build_dir_path(pan, dir_idx, hdr_buf);
        if (strlen(hdr_buf) > 3U) strcat(hdr_buf, "\\");
        strcat(hdr_buf, g_app.filter_pat);
    }
    write_centered(hdr_row, left_col, width, hdr_attr, hdr_buf);

    for (r = 0; r < list_rows; ++r) {
        unsigned scr_row = top_row + r;
        unsigned f_idx = pan->file_top + r;
        dsh_ui_put_cell(scr_row, left_col, CH_BOX_V, g_theme.surface);
        dsh_ui_fill(scr_row, left_col + 1U, ' ', g_theme.surface, width - 2U);
        if (r == 0) {
            dsh_ui_put_cell(scr_row, 78, CH_SCROLL_UP, g_theme.surface);
        } else if (r + 1U == list_rows) {
            dsh_ui_put_cell(scr_row, 78, CH_SCROLL_DOWN, g_theme.surface);
        } else {
            dsh_ui_put_cell(scr_row, 78, 0xB0U, g_theme.surface);
        }
        dsh_ui_put_cell(scr_row, 79, CH_BOX_V, g_theme.surface);

        if (pan->file_count == 0) {
            if (r == 1U) {
                dsh_ui_write(scr_row, left_col + 6U, g_theme.surface, "No files in selected directory.");
            }
        } else if (f_idx < pan->file_count && pan->files != 0) {
            char line[64];
            unsigned char row_attr = g_theme.surface;
            if (pan->files[f_idx].selected) {
                row_attr = has_focus ? g_theme.sel_active :
                           ((g_app.bw_mode || g_app.color_scheme == 1) ? 0x07U : g_theme.surface);
                dsh_ui_put_cell(scr_row, left_col + 4U, CH_TRI_RIGHT, row_attr);
            }
            if (has_focus && f_idx == pan->file_sel) {
                dsh_ui_put_cell(scr_row, left_col + 2U, CH_ARROW_RIGHT, g_theme.surface);
            }
            format_file_row(&pan->files[f_idx], line, all_files_mode);
            dsh_ui_write(scr_row, left_col + 6U, row_attr, line);
        }
    }

    dsh_ui_put_cell(bot_row, left_col, CH_BOX_BL, g_theme.surface);
    dsh_ui_fill(bot_row, left_col + 1U, CH_BOX_H, g_theme.surface, width - 2U);
    dsh_ui_put_cell(bot_row, 79, CH_BOX_BR, g_theme.surface);
}

static void draw_all_files_summary(const struct dsh_panel *pan, unsigned top_row, unsigned bot_row)
{
    unsigned r;
    char fname[14], attr_s[6], dir_name[14], num_s[16], line[32];
    unsigned short sel_cnt = 0;
    unsigned long sel_bytes = 0;
    unsigned short dir_files = 0;
    unsigned long dir_bytes = 0;
    unsigned short cur_dir_idx = 0;
    unsigned short i;

    for (r = top_row - 1U; r <= bot_row; ++r) {
        dsh_ui_fill(r, 0, ' ', g_theme.surface, 28);
    }

    fname[0] = '\0';
    strcpy(attr_s, "....");
    if (pan->file_count > 0 && pan->file_sel < pan->file_count) {
        unsigned char a = pan->files[pan->file_sel].attrib;
        unsigned k = 0;
        while (k < 12U && pan->files[pan->file_sel].name[k] != '\0') {
            fname[k] = pan->files[pan->file_sel].name[k];
            ++k;
        }
        fname[k] = '\0';
        cur_dir_idx = pan->files[pan->file_sel].dir_idx;
        attr_s[0] = (a & 0x01U) ? 'r' : '.';
        attr_s[1] = (a & 0x02U) ? 'h' : '.';
        attr_s[2] = (a & 0x04U) ? 's' : '.';
        attr_s[3] = (a & 0x20U) ? 'a' : '.';
        attr_s[4] = '\0';
    }
    for (i = 0; i < pan->file_count; ++i) {
        if (pan->files[i].selected) {
            ++sel_cnt;
            sel_bytes += pan->files[i].size;
        }
        if (pan->files[i].dir_idx == cur_dir_idx) {
            ++dir_files;
            dir_bytes += pan->files[i].size;
        }
    }
    dir_name[0] = '\0';
    if (pan->dirs != 0 && cur_dir_idx < pan->dir_count) {
        unsigned k = 0;
        while (k < 12U && pan->dirs[cur_dir_idx].name[k] != '\0') {
            dir_name[k] = pan->dirs[cur_dir_idx].name[k];
            ++k;
        }
        dir_name[k] = '\0';
    }

    r = top_row + 1U;
    dsh_ui_write(r++, 2, g_theme.surface, "File");
    sprintf(line, "  Name  : %-12s", fname);
    dsh_ui_write(r++, 2, g_theme.surface, line);
    sprintf(line, "  Attr  : %s", attr_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);

    sprintf(line, "Selected             %c", (char)('A' + pan->drive));
    dsh_ui_write(r++, 2, g_theme.surface, line);
    dsh_format_size_commas(sel_cnt, num_s, 11);
    sprintf(line, "  Number: %s", num_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);
    dsh_format_size_commas(sel_bytes, num_s, 11);
    sprintf(line, "  Size  : %s", num_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);

    dsh_ui_write(r++, 2, g_theme.surface, "Directory");
    sprintf(line, "  Name  : %-12s", dir_name);
    dsh_ui_write(r++, 2, g_theme.surface, line);
    dsh_format_size_commas(dir_bytes, num_s, 11);
    sprintf(line, "  Size  : %s", num_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);
    dsh_format_size_commas(dir_files, num_s, 11);
    sprintf(line, "  Files : %s", num_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);

    dsh_ui_write(r++, 2, g_theme.surface, "Disk");
    sprintf(line, "  Name  : %-12s", pan->vol_label[0] ? pan->vol_label : "NONE");
    dsh_ui_write(r++, 2, g_theme.surface, line);
    dsh_format_size_commas(pan->disk_total, num_s, 11);
    sprintf(line, "  Size  : %s", num_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);
    dsh_format_size_commas(pan->disk_avail, num_s, 11);
    sprintf(line, "  Avail : %s", num_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);
    dsh_format_size_commas(pan->total_disk_files, num_s, 11);
    sprintf(line, "  Files : %s", num_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);
    dsh_format_size_commas(pan->total_disk_dirs, num_s, 11);
    sprintf(line, "  Dirs  : %s", num_s);
    dsh_ui_write(r++, 2, g_theme.surface, line);
}

static void draw_program_pane(unsigned hdr_row, unsigned top_row, unsigned bot_row, int has_focus)
{
    unsigned list_rows = bot_row - top_row;
    unsigned vcount = dsh_prog_visible_count();
    unsigned r;
    unsigned char hdr_attr = has_focus ? g_theme.title : g_theme.inactive_hdr;
    const char *gtitle = g_app.group_titles[g_app.active_group];

    write_centered(hdr_row, 0, 80, hdr_attr, gtitle);

    for (r = 0; r < list_rows; ++r) {
        unsigned scr_row = top_row + r;
        unsigned v_idx = g_app.prog_top + r;
        dsh_ui_put_cell(scr_row, 0, CH_BOX_V, g_theme.surface);
        dsh_ui_fill(scr_row, 1, ' ', g_theme.surface, 77);
        if (r == 0) {
            dsh_ui_put_cell(scr_row, 78, CH_SCROLL_UP, g_theme.surface);
        } else if (r + 1U == list_rows) {
            dsh_ui_put_cell(scr_row, 78, CH_SCROLL_DOWN, g_theme.surface);
        } else {
            dsh_ui_put_cell(scr_row, 78, 0xB0U, g_theme.surface);
        }
        dsh_ui_put_cell(scr_row, 79, CH_BOX_V, g_theme.surface);

        if (v_idx < vcount) {
            int slot = dsh_prog_visible_slot(v_idx);
            if (slot >= 0) {
                char label[44];
                unsigned char iattr = g_theme.surface;
                if (g_app.items[slot].is_group) {
                    sprintf(label, "[%s]", g_app.items[slot].title);
                } else {
                    strncpy(label, g_app.items[slot].title, 40);
                    label[40] = '\0';
                }
                if (has_focus && v_idx == g_app.prog_sel) {
                    iattr = g_theme.sel_active;
                    dsh_ui_put_cell(scr_row, 1, CH_ARROW_RIGHT, g_theme.surface);
                }
                dsh_ui_write(scr_row, 5, iattr, label);
            }
        }
    }

    dsh_ui_put_cell(bot_row, 0, CH_BOX_BL, g_theme.surface);
    dsh_ui_fill(bot_row, 1, CH_BOX_H, g_theme.surface, 78);
    dsh_ui_put_cell(bot_row, 79, CH_BOX_BR, g_theme.surface);
}

void dsh_draw_main_screen(void)
{
    unsigned H = g_app.screen_rows;
    char path_buf[136];
    char clock_s[8];

    dsh_ui_begin_frame();
    dsh_ui_cursor(0xFFFFU, 0);

    /* Row 0: Title bar */
    write_centered(0, 0, 80, g_theme.title, "CK-DOS Shell");

    /* Row 1: Menu bar */
    dsh_ui_fill(1, 0, ' ', g_theme.menubar, 80);
    if (g_app.focus == FOCUS_PROGS || g_app.view_mode == VIEW_PROG) {
        dsh_ui_write(1, 2, g_theme.menubar, "File  Options  View  Help");
    } else {
        dsh_ui_write(1, 2, g_theme.menubar, "File  Options  View  Tree  Help");
    }

    if (g_app.view_mode == VIEW_PROG) {
        draw_program_pane(2, 3, H - 2U, 1);
    } else {
        unsigned short dir_idx = (g_app.pan1.vis_count > 0 && g_app.pan1.tree_sel < g_app.pan1.vis_count)
                                 ? g_app.pan1.vis_dirs[g_app.pan1.tree_sel] : 0;
        dsh_fs_build_dir_path(&g_app.pan1, dir_idx, path_buf);
        dsh_ui_fill(2, 0, ' ', g_theme.surface, 80);
        dsh_ui_write(2, 2, g_theme.surface, path_buf);
        draw_drive_bar(3, &g_app.pan1, g_app.drive_cursor, g_app.focus == FOCUS_DRIVE);
        dsh_ui_fill(4, 0, ' ', g_theme.surface, 80);

        if (g_app.view_mode == VIEW_SINGLE) {
            draw_tree_pane(&g_app.pan1, 5, 6, H - 2U, g_app.focus == FOCUS_TREE);
            draw_file_pane(&g_app.pan1, 36, 5, 6, H - 2U, g_app.focus == FOCUS_FILES, 0);
        } else if (g_app.view_mode == VIEW_DUAL) {
            unsigned split1_bot = (H >= 40U) ? 24U : 12U;
            draw_tree_pane(&g_app.pan1, 5, 6, split1_bot, g_app.focus == FOCUS_TREE);
            draw_file_pane(&g_app.pan1, 36, 5, 6, split1_bot, g_app.focus == FOCUS_FILES, 0);

            dsh_ui_fill(split1_bot + 1U, 0, ' ', g_theme.surface, 80);
            draw_drive_bar(split1_bot + 2U, &g_app.pan2, g_app.drive2_cursor, g_app.focus == FOCUS_DRIVE2);
            dsh_ui_fill(split1_bot + 3U, 0, ' ', g_theme.surface, 80);
            draw_tree_pane(&g_app.pan2, split1_bot + 4U, split1_bot + 5U, H - 2U, g_app.focus == FOCUS_TREE2);
            draw_file_pane(&g_app.pan2, 36, split1_bot + 4U, split1_bot + 5U, H - 2U, g_app.focus == FOCUS_FILES2, 0);
        } else if (g_app.view_mode == VIEW_ALL) {
            draw_all_files_summary(&g_app.pan1, 6, H - 2U);
            draw_file_pane(&g_app.pan1, 28, 5, 6, H - 2U, g_app.focus == FOCUS_FILES, 1);
        } else {
            /* Default VIEW_PROG_FILE */
            unsigned split_bot = (H >= 40U) ? 26U : 14U;
            draw_tree_pane(&g_app.pan1, 5, 6, split_bot, g_app.focus == FOCUS_TREE);
            draw_file_pane(&g_app.pan1, 36, 5, 6, split_bot, g_app.focus == FOCUS_FILES, 0);
            draw_program_pane(split_bot + 1U, split_bot + 2U, H - 2U, g_app.focus == FOCUS_PROGS);
        }
    }

    /* Status bar */
    dsh_ui_fill(H - 1U, 0, ' ', g_theme.statusbar, 80);
    if (g_app.reorder_active) {
        dsh_ui_write(H - 1U, 2, g_theme.statusbar, "Select location and press ENTER. ESC to cancel.");
    } else if (g_app.view_mode == VIEW_PROG) {
        dsh_ui_write(H - 1U, 2, g_theme.statusbar, "F10=Actions            Shift+F9=Command Prompt");
    } else {
        dsh_ui_write(H - 1U, 2, g_theme.statusbar, "F10=Actions  Shift+F9=Command Prompt");
    }
    get_dos_clock_str(clock_s);
    dsh_ui_write(H - 1U, 72, g_theme.statusbar, clock_s);

    dsh_ui_end_frame();
}

/* -------------------------------------------------------------------------
 * Drop-down Menus
 * ------------------------------------------------------------------------- */

struct menu_entry {
    const char *text;       /* NULL for separator */
    char accel;             /* uppercase accelerator char, 0 if none */
    unsigned char accel_pos;
    unsigned char cmd_id;
};

#define CMD_OPEN            1
#define CMD_RUN             2
#define CMD_PRINT           3
#define CMD_ASSOCIATE       4
#define CMD_SEARCH          5
#define CMD_VIEW_FILE       6
#define CMD_MOVE            7
#define CMD_COPY            8
#define CMD_DELETE          9
#define CMD_RENAME          10
#define CMD_CHATTR          11
#define CMD_MKDIR           12
#define CMD_SEL_ALL         13
#define CMD_DESEL_ALL       14
#define CMD_EXIT            15

#define CMD_PROG_NEW        20
#define CMD_PROG_OPEN       21
#define CMD_PROG_COPY       22
#define CMD_PROG_DEL        23
#define CMD_PROG_PROP       24
#define CMD_PROG_REORDER    25

#define CMD_OPT_CONFIRM     30
#define CMD_OPT_FDO         31
#define CMD_OPT_ACROSS      32
#define CMD_OPT_SHOWINFO    33
#define CMD_OPT_SWAPPER     34
#define CMD_OPT_DISPLAY     35
#define CMD_OPT_COLORS      36

#define CMD_VIEW_SINGLE     40
#define CMD_VIEW_DUAL       41
#define CMD_VIEW_ALL        42
#define CMD_VIEW_PROGFILE   43
#define CMD_VIEW_PROG       44
#define CMD_VIEW_REPAINT    45
#define CMD_VIEW_REFRESH    46

#define CMD_TREE_EXP1       50
#define CMD_TREE_EXPB       51
#define CMD_TREE_EXPA       52
#define CMD_TREE_COLL       53

#define CMD_HELP_INDEX      60
#define CMD_HELP_KEYS       61
#define CMD_HELP_BASICS     62
#define CMD_HELP_CMDS       63
#define CMD_HELP_PROCS      64
#define CMD_HELP_USING      65
#define CMD_HELP_ABOUT      66

static const struct menu_entry s_file_fm[] = {
    {"Open                   ", 'O', 0, CMD_OPEN},
    {"Run...                 ", 'R', 0, CMD_RUN},
    {"Print                  ", 'P', 0, CMD_PRINT},
    {"Associate...           ", 'A', 0, CMD_ASSOCIATE},
    {"Search...              ", 'H', 5, CMD_SEARCH},
    {"View File Contents  F9 ", 'V', 0, CMD_VIEW_FILE},
    {0, 0, 0, 0},
    {"Move...             F7 ", 'M', 0, CMD_MOVE},
    {"Copy...             F8 ", 'C', 0, CMD_COPY},
    {"Delete...          Del ", 'D', 0, CMD_DELETE},
    {"Rename...              ", 'N', 2, CMD_RENAME},
    {"Change Attributes...   ", 'G', 4, CMD_CHATTR},
    {0, 0, 0, 0},
    {"Create Directory...    ", 'E', 2, CMD_MKDIR},
    {0, 0, 0, 0},
    {"Select All             ", 'S', 0, CMD_SEL_ALL},
    {"Deselect All           ", 'L', 4, CMD_DESEL_ALL},
    {0, 0, 0, 0},
    {"Exit            Alt+F4 ", 'X', 1, CMD_EXIT}
};

static const struct menu_entry s_file_pm[] = {
    {"New...             ", 'N', 0, CMD_PROG_NEW},
    {"Open         Enter ", 'O', 0, CMD_PROG_OPEN},
    {"Copy               ", 'C', 0, CMD_PROG_COPY},
    {"Delete...      Del ", 'D', 0, CMD_PROG_DEL},
    {"Properties...      ", 'P', 0, CMD_PROG_PROP},
    {"Reorder            ", 'E', 1, CMD_PROG_REORDER},
    {0, 0, 0, 0},
    {"Run...             ", 'R', 0, CMD_RUN},
    {0, 0, 0, 0},
    {"Exit        Alt+F4 ", 'X', 1, CMD_EXIT}
};

static const struct menu_entry s_options[] = {
    {"Confirmation...          ", 'C', 0, CMD_OPT_CONFIRM},
    {"File Display Options...  ", 'F', 0, CMD_OPT_FDO},
    {"Select Across Directories", 'A', 7, CMD_OPT_ACROSS},
    {"Show Information...      ", 'S', 0, CMD_OPT_SHOWINFO},
    {"Enable Task Swapper      ", 'E', 0, CMD_OPT_SWAPPER},
    {"Display...               ", 'D', 0, CMD_OPT_DISPLAY},
    {"Colors...                ", 'O', 1, CMD_OPT_COLORS}
};

static const struct menu_entry s_view[] = {
    {"Single File List       ", 'S', 0, CMD_VIEW_SINGLE},
    {"Dual File Lists        ", 'D', 0, CMD_VIEW_DUAL},
    {"All Files              ", 'A', 0, CMD_VIEW_ALL},
    {"Program/File Lists     ", 'F', 8, CMD_VIEW_PROGFILE},
    {"Program List           ", 'P', 0, CMD_VIEW_PROG},
    {0, 0, 0, 0},
    {"Repaint Screen Shift+F5", 'E', 1, CMD_VIEW_REPAINT},
    {"Refresh        F5      ", 'R', 0, CMD_VIEW_REFRESH}
};

static const struct menu_entry s_tree[] = {
    {"Expand One Level +     ", 'E', 0, CMD_TREE_EXP1},
    {"Expand Branch    *     ", 'B', 7, CMD_TREE_EXPB},
    {"Expand All       Ctrl+*", 'A', 7, CMD_TREE_EXPA},
    {"Collapse Branch  -     ", 'C', 0, CMD_TREE_COLL}
};

static const struct menu_entry s_help[] = {
    {"Index       ", 'I', 0, CMD_HELP_INDEX},
    {"Keyboard    ", 'K', 0, CMD_HELP_KEYS},
    {"Shell Basics", 'S', 0, CMD_HELP_BASICS},
    {"Commands    ", 'C', 0, CMD_HELP_CMDS},
    {"Procedures  ", 'P', 0, CMD_HELP_PROCS},
    {"Using Help  ", 'U', 0, CMD_HELP_USING},
    {0, 0, 0, 0},
    {"About Shell ", 'A', 0, CMD_HELP_ABOUT}
};

static struct dsh_panel *active_panel(void)
{
    if (g_app.view_mode == VIEW_DUAL &&
        (g_app.focus == FOCUS_DRIVE2 || g_app.focus == FOCUS_TREE2 || g_app.focus == FOCUS_FILES2)) {
        return &g_app.pan2;
    }
    return &g_app.pan1;
}

static int is_cmd_enabled(unsigned char cmd)
{
    struct dsh_panel *pan = active_panel();
    int in_files = (g_app.focus == FOCUS_FILES || g_app.focus == FOCUS_FILES2);
    int has_sel = 0;

    if (pan->file_count > 0) {
        unsigned short i;
        for (i = 0; i < pan->file_count; ++i) {
            if (pan->files[i].selected) {
                has_sel = 1;
                break;
            }
        }
    }

    switch (cmd) {
    case CMD_PRINT:
    case CMD_OPT_SWAPPER:
        return 0; /* Print & DOSSWAP task swapper intentionally disabled in D5.8 */
    case CMD_OPEN:
        return in_files ? has_sel : 1;
    case CMD_ASSOCIATE:
    case CMD_VIEW_FILE:
    case CMD_MOVE:
    case CMD_COPY:
    case CMD_DELETE:
    case CMD_RENAME:
    case CMD_CHATTR:
        return in_files && has_sel;
    case CMD_SEL_ALL:
    case CMD_DESEL_ALL:
        return in_files && pan->file_count > 0;
    case CMD_MKDIR:
        return !in_files && g_app.view_mode != VIEW_ALL && g_app.view_mode != VIEW_PROG;
    case CMD_VIEW_REFRESH:
        return g_app.view_mode != VIEW_PROG;
    case CMD_PROG_COPY:
    case CMD_PROG_DEL:
    case CMD_PROG_PROP:
    case CMD_PROG_REORDER:
        return dsh_prog_visible_count() > 0;
    default:
        return 1;
    }
}

static void dispatch_menu_cmd(unsigned char cmd)
{
    struct dsh_panel *pan = active_panel();

    switch (cmd) {
    case CMD_OPEN:
        if (g_app.focus == FOCUS_FILES || g_app.focus == FOCUS_FILES2) {
            dsh_open_selected_file(pan);
        } else if (g_app.focus == FOCUS_TREE || g_app.focus == FOCUS_TREE2) {
            dsh_fs_tree_expand_one(pan);
        }
        break;
    case CMD_RUN:
        dsh_dlg_do_run();
        break;
    case CMD_ASSOCIATE:
        dsh_dlg_do_associate(pan);
        break;
    case CMD_SEARCH:
        dsh_dlg_do_search(pan);
        break;
    case CMD_VIEW_FILE:
        if (pan->file_count > 0 && pan->file_sel < pan->file_count) {
            char dir_p[136], full_p[152], disp[14];
            unsigned k = 0;
            while (k < 12U && pan->files[pan->file_sel].name[k] != '\0') {
                disp[k] = pan->files[pan->file_sel].name[k];
                ++k;
            }
            disp[k] = '\0';
            dsh_fs_build_dir_path(pan, pan->files[pan->file_sel].dir_idx, dir_p);
            if (strlen(dir_p) > 3U) sprintf(full_p, "%s\\%s", dir_p, disp);
            else sprintf(full_p, "%s%s", dir_p, disp);
            dsh_view_file(full_p, disp);
        }
        break;
    case CMD_MOVE:
        dsh_dlg_do_copy_or_move(pan, 1);
        break;
    case CMD_COPY:
        dsh_dlg_do_copy_or_move(pan, 0);
        break;
    case CMD_DELETE:
        dsh_dlg_do_delete(pan);
        break;
    case CMD_RENAME:
        dsh_dlg_do_rename(pan);
        break;
    case CMD_CHATTR:
        dsh_dlg_do_change_attr(pan);
        break;
    case CMD_MKDIR:
        dsh_dlg_do_mkdir(pan);
        break;
    case CMD_SEL_ALL:
        {
            unsigned short i;
            for (i = 0; i < pan->file_count; ++i) pan->files[i].selected = 1;
        }
        break;
    case CMD_DESEL_ALL:
        {
            unsigned short i;
            for (i = 0; i < pan->file_count; ++i) pan->files[i].selected = 0;
        }
        break;
    case CMD_EXIT:
        g_app.running = 0;
        break;

    case CMD_PROG_NEW:
        dsh_prog_new_dialog();
        break;
    case CMD_PROG_OPEN:
        dsh_prog_activate_selected();
        break;
    case CMD_PROG_COPY:
        dsh_prog_copy_selected();
        break;
    case CMD_PROG_DEL:
        dsh_prog_delete_selected();
        break;
    case CMD_PROG_PROP:
        dsh_prog_properties_dialog();
        break;
    case CMD_PROG_REORDER:
        if (dsh_prog_visible_count() > 0) {
            g_app.reorder_active = 1;
            g_app.reorder_src = g_app.prog_sel;
        }
        break;

    case CMD_OPT_CONFIRM:
        dsh_dlg_confirmation();
        break;
    case CMD_OPT_FDO:
        dsh_dlg_file_display_options();
        break;
    case CMD_OPT_ACROSS:
        g_app.select_across = (unsigned char)!g_app.select_across;
        g_app.ini_dirty = 1;
        break;
    case CMD_OPT_SHOWINFO:
        dsh_dlg_show_information(pan);
        break;
    case CMD_OPT_DISPLAY:
        dsh_dlg_display_mode();
        break;
    case CMD_OPT_COLORS:
        dsh_dlg_colors();
        break;

    case CMD_VIEW_SINGLE:
        g_app.view_mode = VIEW_SINGLE;
        g_app.focus = FOCUS_DRIVE;
        dsh_fs_scan_files(&g_app.pan1);
        g_app.ini_dirty = 1;
        break;
    case CMD_VIEW_DUAL:
        g_app.view_mode = VIEW_DUAL;
        g_app.focus = FOCUS_DRIVE;
        dsh_fs_scan_files(&g_app.pan1);
        dsh_fs_scan_drive(&g_app.pan2, 0);
        g_app.ini_dirty = 1;
        break;
    case CMD_VIEW_ALL:
        g_app.view_mode = VIEW_ALL;
        g_app.focus = FOCUS_DRIVE;
        dsh_fs_scan_all_files(&g_app.pan1, g_app.filter_pat);
        g_app.ini_dirty = 1;
        break;
    case CMD_VIEW_PROGFILE:
        g_app.view_mode = VIEW_PROG_FILE;
        g_app.focus = FOCUS_DRIVE;
        dsh_fs_scan_files(&g_app.pan1);
        g_app.ini_dirty = 1;
        break;
    case CMD_VIEW_PROG:
        g_app.view_mode = VIEW_PROG;
        g_app.focus = FOCUS_PROGS;
        g_app.ini_dirty = 1;
        break;
    case CMD_VIEW_REPAINT:
        break;
    case CMD_VIEW_REFRESH:
        {
            char cur_p[136];
            unsigned short di = (pan->vis_count > 0 && pan->tree_sel < pan->vis_count)
                                ? pan->vis_dirs[pan->tree_sel] : 0;
            dsh_fs_build_dir_path(pan, di, cur_p);
            dsh_fs_scan_drive(pan, cur_p);
        }
        break;

    case CMD_TREE_EXP1:
        dsh_fs_tree_expand_one(pan);
        break;
    case CMD_TREE_EXPB:
        dsh_fs_tree_expand_branch(pan);
        break;
    case CMD_TREE_EXPA:
        dsh_fs_tree_expand_all(pan);
        break;
    case CMD_TREE_COLL:
        dsh_fs_tree_collapse(pan);
        break;

    case CMD_HELP_INDEX:
        dsh_show_help_topic(HELP_INDEX);
        break;
    case CMD_HELP_KEYS:
        dsh_show_help_topic(HELP_KEYBOARD);
        break;
    case CMD_HELP_BASICS:
        dsh_show_help_topic(HELP_BASICS);
        break;
    case CMD_HELP_CMDS:
        dsh_show_help_topic(HELP_COMMANDS);
        break;
    case CMD_HELP_PROCS:
        dsh_show_help_topic(HELP_PROCEDURES);
        break;
    case CMD_HELP_USING:
        dsh_show_help_topic(HELP_USING_HELP);
        break;
    case CMD_HELP_ABOUT:
        dsh_dlg_about();
        break;
    }
}

void dsh_run_menu_bar(int initial_menu)
{
    int prog_mode = (g_app.focus == FOCUS_PROGS || g_app.view_mode == VIEW_PROG);
    int menu_count = prog_mode ? 4 : 5;
    int m_idx = (initial_menu >= 0 && initial_menu < menu_count) ? initial_menu : 0;
    int open_drop = (initial_menu >= 0) ? 1 : 0;
    int sel_row = 0;

    for (;;) {
        const char *m_titles[5];
        unsigned m_cols[5];
        const struct menu_entry *entries = 0;
        unsigned e_count = 0;
        unsigned box_left = 0;
        unsigned inner_w = 24;
        unsigned i;

        m_titles[0] = " File ";    m_cols[0] = 1;
        m_titles[1] = " Options "; m_cols[1] = 7;
        m_titles[2] = " View ";    m_cols[2] = 16;
        if (prog_mode) {
            m_titles[3] = " Help "; m_cols[3] = 22;
        } else {
            m_titles[3] = " Tree "; m_cols[3] = 22;
            m_titles[4] = " Help "; m_cols[4] = 28;
        }

        if (m_idx == 0) {
            if (prog_mode) {
                entries = s_file_pm;
                e_count = sizeof(s_file_pm) / sizeof(s_file_pm[0]);
                inner_w = 20;
            } else {
                entries = s_file_fm;
                e_count = sizeof(s_file_fm) / sizeof(s_file_fm[0]);
                inner_w = 24;
            }
            box_left = 0;
        } else if (m_idx == 1) {
            entries = s_options;
            e_count = sizeof(s_options) / sizeof(s_options[0]);
            inner_w = 27;
            box_left = 6;
        } else if (m_idx == 2) {
            entries = s_view;
            e_count = sizeof(s_view) / sizeof(s_view[0]);
            inner_w = 25;
            box_left = 15;
        } else if (!prog_mode && m_idx == 3) {
            entries = s_tree;
            e_count = sizeof(s_tree) / sizeof(s_tree[0]);
            inner_w = 25;
            box_left = 21;
        } else {
            entries = s_help;
            e_count = sizeof(s_help) / sizeof(s_help[0]);
            inner_w = 14;
            box_left = prog_mode ? 20 : 27;
        }

        if (sel_row < 0 || (unsigned)sel_row >= e_count || entries[sel_row].text == 0) {
            sel_row = 0;
        }

        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_ui_write(1, m_cols[m_idx], g_theme.sel_active, m_titles[m_idx]);

        if (open_drop) {
            unsigned top = 2;
            unsigned bot = top + e_count + 1U;
            unsigned right = box_left + inner_w + 1U;
            dsh_draw_box(top, box_left, bot, right, g_theme.menubar, 0, 1);
            for (i = 0; i < e_count; ++i) {
                unsigned r = top + 1U + i;
                if (entries[i].text == 0) {
                    dsh_ui_put_cell(r, box_left, CH_BOX_LT, g_theme.menubar);
                    dsh_ui_fill(r, box_left + 1U, CH_BOX_H, g_theme.menubar, inner_w);
                    dsh_ui_put_cell(r, right, CH_BOX_RT, g_theme.menubar);
                } else {
                    int en = is_cmd_enabled(entries[i].cmd_id);
                    unsigned char base_a = ((int)i == sel_row) ? g_theme.sel_active
                                         : (en ? g_theme.menubar : g_theme.menu_dim);
                    dsh_ui_fill(r, box_left + 1U, ' ', base_a, inner_w);
                    if (entries[i].cmd_id == CMD_OPT_ACROSS && g_app.select_across) {
                        dsh_ui_put_cell(r, box_left + 1U, CH_BULLET, base_a);
                    }
                    dsh_ui_write(r, box_left + 2U, base_a, entries[i].text);
                    if (en && (int)i != sel_row && entries[i].accel != 0) {
                        unsigned apos = box_left + 2U + entries[i].accel_pos;
                        dsh_ui_put_cell(r, apos, (unsigned char)entries[i].text[entries[i].accel_pos], g_theme.menu_accel);
                    }
                }
            }
        }
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);

            if (hi == 1U) {
                /* Mouse click */
                unsigned mrow = (lo >> 8) & 0xFFU;
                unsigned mcol = lo & 0xFFU;
                if (mrow == 1U) {
                    int clicked = -1;
                    for (i = 0; i < (unsigned)menu_count; ++i) {
                        unsigned slen = (unsigned)strlen(m_titles[i]);
                        if (mcol >= m_cols[i] && mcol < m_cols[i] + slen) {
                            clicked = (int)i;
                            break;
                        }
                    }
                    if (clicked >= 0) {
                        m_idx = clicked;
                        open_drop = 1;
                        sel_row = 0;
                        continue;
                    }
                } else if (open_drop && mrow > 2U && mrow <= 2U + e_count &&
                           mcol > box_left && mcol <= box_left + inner_w) {
                    unsigned idx = mrow - 3U;
                    if (entries[idx].text != 0 && is_cmd_enabled(entries[idx].cmd_id)) {
                        unsigned char cmd = entries[idx].cmd_id;
                        dispatch_menu_cmd(cmd);
                        return;
                    }
                }
                return;
            } else {
                unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
                unsigned char ascii = (unsigned char)(lo & 0xFFU);
                char up = (ascii >= 'a' && ascii <= 'z') ? (char)(ascii - 32) : (char)ascii;

                if (ascii == 27 || scan == 0x44U || (lo == 0x3800U)) {
                    return; /* Esc, F10, or Alt closes menu */
                }
                if (scan == 0x3BU) {
                    dsh_show_help_topic(HELP_COMMANDS);
                    return;
                }
                if (scan == 0x4BU) { /* Left */
                    m_idx = (m_idx + menu_count - 1) % menu_count;
                    sel_row = 0;
                    continue;
                }
                if (scan == 0x4DU) { /* Right */
                    m_idx = (m_idx + 1) % menu_count;
                    sel_row = 0;
                    continue;
                }
                if (scan == 0x50U) { /* Down */
                    if (!open_drop) {
                        open_drop = 1;
                        sel_row = 0;
                    } else {
                        do {
                            sel_row = (sel_row + 1) % (int)e_count;
                        } while (entries[sel_row].text == 0);
                    }
                    continue;
                }
                if (scan == 0x48U && open_drop) { /* Up */
                    do {
                        sel_row = (sel_row + (int)e_count - 1) % (int)e_count;
                    } while (entries[sel_row].text == 0);
                    continue;
                }
                if (ascii == 13) { /* Enter */
                    if (!open_drop) {
                        open_drop = 1;
                        sel_row = 0;
                        continue;
                    }
                    if (entries[sel_row].text != 0 && is_cmd_enabled(entries[sel_row].cmd_id)) {
                        dispatch_menu_cmd(entries[sel_row].cmd_id);
                        return;
                    }
                    continue;
                }
                if (!open_drop) {
                    if (up == 'F') { m_idx = 0; open_drop = 1; sel_row = 0; continue; }
                    if (up == 'O') { m_idx = 1; open_drop = 1; sel_row = 0; continue; }
                    if (up == 'V') { m_idx = 2; open_drop = 1; sel_row = 0; continue; }
                    if (!prog_mode && up == 'T') { m_idx = 3; open_drop = 1; sel_row = 0; continue; }
                    if (up == 'H') { m_idx = prog_mode ? 3 : 4; open_drop = 1; sel_row = 0; continue; }
                } else if (up != 0) {
                    for (i = 0; i < e_count; ++i) {
                        if (entries[i].text != 0 && entries[i].accel == up) {
                            if (is_cmd_enabled(entries[i].cmd_id)) {
                                dispatch_menu_cmd(entries[i].cmd_id);
                                return;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Full-Screen ASCII / Hex File Viewer (F9)
 * ------------------------------------------------------------------------- */

void dsh_view_file(const char *full_path, const char *display_name)
{
#ifdef __CKCL__
    static unsigned char vbuf[8192];
    unsigned int vlen = 0;
    int fd = -1;
    int hex_mode = 0;
    unsigned int offset = 0;
    unsigned H = g_app.screen_rows;
    unsigned body_rows = H - 5U;

    if (_dos_open(full_path, 0, &fd) == 0) {
        _dos_read(fd, vbuf, sizeof(vbuf), &vlen);
        _dos_close(fd);
    }

    for (;;) {
        char title_s[64];
        char clock_s[8];
        unsigned r;

        dsh_ui_begin_frame();
        dsh_ui_cursor(0xFFFFU, 0);

        sprintf(title_s, "CK-DOS Shell - %s", display_name);
        write_centered(0, 0, 80, g_theme.title, title_s);

        dsh_ui_fill(1, 0, ' ', g_theme.menubar, 80);
        dsh_ui_write(1, 2, g_theme.menubar, "Display  View  Help");

        dsh_ui_fill(2, 0, ' ', g_theme.surface, 80);
        dsh_ui_put_cell(2, 0, CH_BOX_TL, g_theme.surface);
        dsh_ui_write(2, 4, g_theme.surface, "To view file's content use PgUp or PgDn or   or  .");
        dsh_ui_put_cell(2, 47, CH_SCROLL_UP, g_theme.surface);
        dsh_ui_put_cell(2, 52, CH_SCROLL_DOWN, g_theme.surface);
        dsh_ui_put_cell(2, 79, CH_BOX_TR, g_theme.surface);

        dsh_ui_put_cell(3, 0, CH_BOX_BL, g_theme.surface);
        dsh_ui_fill(3, 1, CH_BOX_H, g_theme.surface, 78);
        dsh_ui_put_cell(3, 79, CH_BOX_BR, g_theme.surface);

        if (!hex_mode) {
            unsigned pos = offset;
            for (r = 0; r < body_rows; ++r) {
                unsigned scr_r = 4U + r;
                unsigned col = 0;
                dsh_ui_fill(scr_r, 0, ' ', g_theme.surface, 80);
                while (pos < vlen) {
                    unsigned char ch = vbuf[pos++];
                    if (ch == '\r') {
                        if (pos < vlen && vbuf[pos] == '\n') ++pos;
                        break;
                    }
                    if (ch == '\n') break;
                    if (ch == '\t') {
                        unsigned next_tab = (col + 8U) & ~7U;
                        while (col < next_tab && col < 80U) ++col;
                    } else if (col < 80U) {
                        dsh_ui_put_cell(scr_r, col++, ch ? ch : ' ', g_theme.surface);
                    }
                }
            }
        } else {
            for (r = 0; r < body_rows; ++r) {
                unsigned scr_r = 4U + r;
                unsigned row_off = offset + r * 16U;
                dsh_ui_fill(scr_r, 0, ' ', g_theme.surface, 80);
                dsh_ui_put_cell(scr_r, 0, CH_BOX_V, g_theme.surface);
                dsh_ui_put_cell(scr_r, 11, CH_BOX_V, g_theme.surface);
                dsh_ui_put_cell(scr_r, 58, CH_BOX_V, g_theme.surface);
                dsh_ui_put_cell(scr_r, 79, CH_BOX_V, g_theme.surface);

                if (row_off < vlen || (vlen == 0 && r == 0)) {
                    char off_s[10];
                    unsigned b;
                    sprintf(off_s, "%06X", row_off);
                    dsh_ui_write(scr_r, 3, g_theme.surface, off_s);
                    for (b = 0; b < 16U && (row_off + b) < vlen; ++b) {
                        static const char hex_dig[] = "0123456789ABCDEF";
                        unsigned char val = vbuf[row_off + b];
                        unsigned hcol = 14U + (b / 4U) * 11U + (b % 4U) * 2U;
                        unsigned char ach = (val >= 32U && val < 127U) ? val : '.';
                        dsh_ui_put_cell(scr_r, hcol, (unsigned char)hex_dig[val >> 4], g_theme.surface);
                        dsh_ui_put_cell(scr_r, hcol + 1U, (unsigned char)hex_dig[val & 0x0FU], g_theme.surface);
                        dsh_ui_put_cell(scr_r, 61U + b, ach, g_theme.surface);
                    }
                }
            }
        }

        dsh_ui_fill(H - 1U, 0, ' ', g_theme.statusbar, 80);
        dsh_ui_write(H - 1U, 2, g_theme.statusbar, "<\xC4\xD9=PageDown  Esc=Cancel  F9=Hex/ASCII");
        get_dos_clock_str(clock_s);
        dsh_ui_write(H - 1U, 72, g_theme.statusbar, clock_s);

        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) continue;
            if (ascii == 27) return;
            if (scan == 0x43U) { /* F9 */
                hex_mode = !hex_mode;
                offset = 0;
            } else if (scan == 0x3BU) {
                dsh_show_help_topic(HELP_COMMANDS);
            } else if (scan == 0x51U || ascii == 13 || ascii == ' ') { /* PgDn / Enter */
                unsigned step = hex_mode ? (body_rows * 16U) : 512U;
                if (offset + step < vlen) offset += step;
            } else if (scan == 0x49U) { /* PgUp */
                unsigned step = hex_mode ? (body_rows * 16U) : 512U;
                offset = (offset > step) ? (offset - step) : 0U;
            } else if (scan == 0x50U) { /* Down */
                if (hex_mode) {
                    if (offset + 16U < vlen) offset += 16U;
                } else {
                    while (offset < vlen && vbuf[offset++] != '\n') {}
                }
            } else if (scan == 0x48U) { /* Up */
                if (hex_mode) {
                    offset = (offset >= 16U) ? (offset - 16U) : 0U;
                } else if (offset > 0U) {
                    --offset;
                    while (offset > 0U && vbuf[offset - 1U] != '\n') --offset;
                }
            } else if (scan == 0x47U) { /* Home */
                offset = 0;
            }
        }
    }
#else
    (void)full_path;
    (void)display_name;
#endif
}

/* -------------------------------------------------------------------------
 * Full-Screen Search Results View
 * ------------------------------------------------------------------------- */

void dsh_run_search_results(const char *pattern, int entire_disk)
{
    struct dsh_panel *pan = active_panel();
    unsigned short saved_dir_sel = pan->tree_sel;
    unsigned short sel = 0, top = 0;
    unsigned H = g_app.screen_rows;
    unsigned list_rows = H - 5U;

    if (entire_disk) {
        dsh_fs_scan_all_files(pan, pattern);
    } else {
        char old_pat[14];
        strcpy(old_pat, g_app.filter_pat);
        strncpy(g_app.filter_pat, pattern, 13);
        g_app.filter_pat[13] = '\0';
        dsh_fs_scan_files(pan);
        strcpy(g_app.filter_pat, old_pat);
    }

    for (;;) {
        char hdr[64], clock_s[8];
        unsigned r;

        dsh_ui_begin_frame();
        dsh_ui_cursor(0xFFFFU, 0);

        write_centered(0, 0, 80, g_theme.title, "CK-DOS Shell Search Results");
        dsh_ui_fill(1, 0, ' ', g_theme.menubar, 80);
        dsh_ui_write(1, 2, g_theme.menubar, "File  Options  View  Help");

        sprintf(hdr, "Search Results for: %s", pattern);
        write_centered(2, 0, 80, g_theme.title, hdr);

        for (r = 0; r < list_rows; ++r) {
            unsigned scr_r = 3U + r;
            unsigned idx = top + r;
            dsh_ui_put_cell(scr_r, 0, CH_BOX_V, g_theme.surface);
            dsh_ui_fill(scr_r, 1, ' ', g_theme.surface, 78);
            dsh_ui_put_cell(scr_r, 79, CH_BOX_V, g_theme.surface);

            if (pan->file_count == 0 && r == 1U) {
                dsh_ui_write(scr_r, 6, g_theme.surface, "No matching files found.");
            } else if (idx < pan->file_count) {
                char dir_p[136], fname[14], full_p[152], sz_s[12], dt_s[10], row_s[76];
                unsigned k = 0;
                unsigned char a = (idx == sel) ? g_theme.sel_active : g_theme.surface;
                while (k < 12U && pan->files[idx].name[k] != '\0') {
                    fname[k] = pan->files[idx].name[k];
                    ++k;
                }
                fname[k] = '\0';
                dsh_fs_build_dir_path(pan, pan->files[idx].dir_idx, dir_p);
                if (strlen(dir_p) > 3U) sprintf(full_p, "%s\\%s", dir_p, fname);
                else sprintf(full_p, "%s%s", dir_p, fname);
                dsh_format_size_commas(pan->files[idx].size, sz_s, 9);
                dsh_format_date(pan->files[idx].wr_date, dt_s);
                sprintf(row_s, "%-44.44s  %s  %s", full_p, sz_s, dt_s);
                if (idx == sel) {
                    dsh_ui_put_cell(scr_r, 2, CH_ARROW_RIGHT, g_theme.surface);
                }
                dsh_ui_write(scr_r, 4, a, row_s);
            }
        }
        dsh_ui_put_cell(H - 2U, 0, CH_BOX_BL, g_theme.surface);
        dsh_ui_fill(H - 2U, 1, CH_BOX_H, g_theme.surface, 78);
        dsh_ui_put_cell(H - 2U, 79, CH_BOX_BR, g_theme.surface);

        dsh_ui_fill(H - 1U, 0, ' ', g_theme.statusbar, 80);
        dsh_ui_write(H - 1U, 2, g_theme.statusbar, "Esc=Cancel  F9=View File Contents");
        get_dos_clock_str(clock_s);
        dsh_ui_write(H - 1U, 72, g_theme.statusbar, clock_s);

        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) continue;
            if (ascii == 27) break;
            if (scan == 0x50U && sel + 1U < pan->file_count) {
                ++sel;
                if (sel >= top + list_rows) top = (unsigned short)(sel - list_rows + 1U);
            } else if (scan == 0x48U && sel > 0U) {
                --sel;
                if (sel < top) top = sel;
            } else if (scan == 0x43U && pan->file_count > 0) {
                char dir_p[136], fname[14], full_p[152];
                unsigned k = 0;
                while (k < 12U && pan->files[sel].name[k] != '\0') {
                    fname[k] = pan->files[sel].name[k];
                    ++k;
                }
                fname[k] = '\0';
                dsh_fs_build_dir_path(pan, pan->files[sel].dir_idx, dir_p);
                if (strlen(dir_p) > 3U) sprintf(full_p, "%s\\%s", dir_p, fname);
                else sprintf(full_p, "%s%s", dir_p, fname);
                dsh_view_file(full_p, fname);
            }
        }
    }

    pan->tree_sel = saved_dir_sel;
    if (g_app.view_mode == VIEW_ALL) {
        dsh_fs_scan_all_files(pan, g_app.filter_pat);
    } else {
        dsh_fs_scan_files(pan);
    }
}
