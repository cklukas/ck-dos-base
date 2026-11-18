/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "DOSSHELL.H"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strupr_in_place(char *s)
{
    while (*s != '\0') {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - 32);
        }
        ++s;
    }
}

static void draw_input_field(unsigned row, unsigned col, unsigned width,
                             const char *text, int focused)
{
    unsigned len = (unsigned)strlen(text);
    unsigned i;
    dsh_ui_put_cell(row, col, '[', g_theme.dlg_box);
    for (i = 0; i < width; ++i) {
        unsigned char ch = (i < len) ? (unsigned char)text[i] : (unsigned char)CH_DOT_FILL;
        dsh_ui_put_cell(row, col + 1U + i, ch, g_theme.dlg_input);
    }
    dsh_ui_put_cell(row, col + 1U + width, ']', g_theme.dlg_box);
    if (focused) {
        unsigned cpos = (len < width) ? len : (width - 1U);
        dsh_ui_cursor(row, col + 1U + cpos);
    }
}

static int edit_field_key(char *buf, unsigned max_len, int *untouched,
                          unsigned char scan, unsigned char ascii)
{
    unsigned len = (unsigned)strlen(buf);
    if (ascii == 8) { /* Backspace */
        if (*untouched) {
            buf[0] = '\0';
            *untouched = 0;
        } else if (len > 0U) {
            buf[len - 1U] = '\0';
        }
        return 1;
    }
    if (scan == 0x53U) { /* Delete */
        buf[0] = '\0';
        *untouched = 0;
        return 1;
    }
    if (scan == 0x4BU || scan == 0x4DU || scan == 0x47U || scan == 0x4FU) {
        *untouched = 0;
        return 1;
    }
    if (ascii >= 32 && ascii < 127) {
        if (*untouched) {
            buf[0] = (char)ascii;
            buf[1] = '\0';
            *untouched = 0;
        } else if (len + 1U < max_len) {
            buf[len] = (char)ascii;
            buf[len + 1U] = '\0';
        }
        return 1;
    }
    return 0;
}

void dsh_dlg_message(const char *title, const char *line1, const char *line2)
{
    for (;;) {
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(7, 15, 16, 64, g_theme.dlg_box, title, 1);
        if (line1) dsh_ui_write(10, 18, g_theme.dlg_box, line1);
        if (line2) dsh_ui_write(11, 18, g_theme.dlg_box, line2);
        dsh_ui_write(14, 36, g_theme.dlg_btn_sel, "  OK  ");
        dsh_ui_cursor(14, 38);
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);
            if (hi == 1U) return;
            if (ascii == 13 || ascii == 27 || ascii == ' ') return;
        }
    }
}

/* Returns: 1 = Yes, 0 = No, -1 = Cancel */
int dsh_dlg_confirm_yes_no_cancel(const char *title, const char *warn_line, const char *prompt_line)
{
    int btn = 0; /* 0=Yes, 1=No, 2=Cancel */
    static const unsigned bcols[3] = {23, 35, 45};

    for (;;) {
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(7, 18, 16, 55, g_theme.dlg_box, title, 1);
        if (warn_line && warn_line[0] != '\0') {
            dsh_ui_write(9, 21, g_theme.dlg_box, warn_line);
        }
        if (prompt_line) {
            dsh_ui_write(11, 21, g_theme.dlg_box, prompt_line);
        }
        dsh_ui_write(14, 22, (btn == 0) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Yes ");
        dsh_ui_write(14, 34, (btn == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " No ");
        dsh_ui_write(14, 44, (btn == 2) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Cancel ");
        dsh_ui_cursor(14, bcols[btn]);
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) {
                unsigned mr = (lo >> 8) & 0xFFU;
                unsigned mc = lo & 0xFFU;
                if (mr == 14U) {
                    if (mc >= 22U && mc <= 27U) return 1;
                    if (mc >= 34U && mc <= 38U) return 0;
                    if (mc >= 44U && mc <= 52U) return -1;
                }
                continue;
            }
            if (ascii == 27) return -1;
            if (ascii == 'y' || ascii == 'Y') return 1;
            if (ascii == 'n' || ascii == 'N') return 0;
            if (ascii == 'c' || ascii == 'C') return -1;
            if (ascii == 9 || scan == 0x4DU) btn = (btn + 1) % 3;
            else if (scan == 0x0FU || scan == 0x4BU) btn = (btn + 2) % 3;
            else if (ascii == 13) {
                if (btn == 0) return 1;
                if (btn == 1) return 0;
                return -1;
            }
        }
    }
}

int dsh_dlg_input_one(const char *title, const char *info_line, const char *label,
                      char *buf, unsigned max_len)
{
    int foc = 0; /* 0=input, 1=OK, 2=Cancel, 3=Help */
    int untouched = 1;
    unsigned lbl_len = (unsigned)strlen(label);
    unsigned inp_col = 18U + lbl_len + 2U;
    unsigned inp_w = (inp_col + 24U < 62U) ? (62U - inp_col) : 13U;
    if (inp_w > max_len - 1U) inp_w = max_len - 1U;

    for (;;) {
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(7, 14, 20, 65, g_theme.dlg_box, title, 1);
        if (info_line) dsh_ui_write(10, 17, g_theme.dlg_box, info_line);
        dsh_ui_write(12, 17, g_theme.dlg_box, label);
        draw_input_field(12, inp_col, inp_w, buf, foc == 0);

        dsh_ui_write(19, 23, (foc == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " OK ");
        dsh_ui_write(19, 36, (foc == 2) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Cancel ");
        dsh_ui_write(19, 52, (foc == 3) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Help ");
        if (foc > 0) {
            unsigned bc = (foc == 1) ? 24U : ((foc == 2) ? 37U : 53U);
            dsh_ui_cursor(19, bc);
        }
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) {
                unsigned mr = (lo >> 8) & 0xFFU;
                unsigned mc = lo & 0xFFU;
                if (mr == 19U) {
                    if (mc >= 23U && mc <= 27U) return 1;
                    if (mc >= 36U && mc <= 44U) return 0;
                    if (mc >= 52U && mc <= 58U) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                }
                continue;
            }
            if (ascii == 27) return 0;
            if (scan == 0x3BU) { dsh_show_help_topic(HELP_COMMANDS); continue; }
            if (ascii == 9) { foc = (foc + 1) % 4; continue; }
            if (scan == 0x0FU) { foc = (foc + 3) % 4; continue; }
            if (ascii == 13) {
                if (foc == 2) return 0;
                if (foc == 3) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                return 1;
            }
            if (foc == 0) {
                edit_field_key(buf, max_len, &untouched, scan, ascii);
            }
        }
    }
}

int dsh_dlg_copy_or_move(int is_move, const char *from_desc, char *to_buf, unsigned max_len)
{
    int foc = 1; /* 0=From (read-only display), 1=To, 2=OK, 3=Cancel, 4=Help */
    int untouched = 1;
    char from_buf[48];
    strncpy(from_buf, from_desc, 47);
    from_buf[47] = '\0';

    for (;;) {
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(7, 15, 20, 66, g_theme.dlg_box, is_move ? "Move File" : "Copy File", 1);
        dsh_ui_write(11, 22, g_theme.dlg_box, "From:");
        draw_input_field(11, 30, 30, from_buf, foc == 0);
        dsh_ui_write(13, 22, g_theme.dlg_box, "To:");
        draw_input_field(13, 30, 30, to_buf, foc == 1);

        dsh_ui_write(19, 24, (foc == 2) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " OK ");
        dsh_ui_write(19, 37, (foc == 3) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Cancel ");
        dsh_ui_write(19, 53, (foc == 4) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Help ");
        if (foc >= 2) {
            unsigned bc = (foc == 2) ? 25U : ((foc == 3) ? 38U : 54U);
            dsh_ui_cursor(19, bc);
        }
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) {
                unsigned mr = (lo >> 8) & 0xFFU;
                unsigned mc = lo & 0xFFU;
                if (mr == 19U) {
                    if (mc >= 24U && mc <= 28U) return 1;
                    if (mc >= 37U && mc <= 45U) return 0;
                    if (mc >= 53U && mc <= 59U) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                }
                continue;
            }
            if (ascii == 27) return 0;
            if (scan == 0x3BU) { dsh_show_help_topic(HELP_COMMANDS); continue; }
            if (ascii == 9) { foc = (foc == 4) ? 1 : (foc + 1); continue; }
            if (scan == 0x0FU) { foc = (foc <= 1) ? 4 : (foc - 1); continue; }
            if (ascii == 13) {
                if (foc == 3) return 0;
                if (foc == 4) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                return 1;
            }
            if (foc == 1) {
                edit_field_key(to_buf, max_len, &untouched, scan, ascii);
            }
        }
    }
}

static void build_file_full_path(const struct dsh_panel *pan, unsigned short fi,
                                 char *out_full, char *out_leaf)
{
    char dir_p[136];
    unsigned k = 0;
    while (k < 12U && pan->files[fi].name[k] != '\0') {
        out_leaf[k] = pan->files[fi].name[k];
        ++k;
    }
    out_leaf[k] = '\0';
    dsh_fs_build_dir_path(pan, pan->files[fi].dir_idx, dir_p);
    if (strlen(dir_p) > 3U) sprintf(out_full, "%s\\%s", dir_p, out_leaf);
    else sprintf(out_full, "%s%s", dir_p, out_leaf);
}

void dsh_dlg_do_copy_or_move(struct dsh_panel *pan, int is_move)
{
    char from_desc[48];
    char to_buf[128];
    char cur_dir[136];
    unsigned short i, sel_cnt = 0, first_sel = 0;

    if (pan->file_count == 0) return;
    for (i = 0; i < pan->file_count; ++i) {
        if (pan->files[i].selected) {
            if (sel_cnt == 0) first_sel = i;
            ++sel_cnt;
        }
    }
    if (sel_cnt == 0) {
        first_sel = pan->file_sel;
        pan->files[first_sel].selected = 1;
        sel_cnt = 1;
    }

    if (sel_cnt == 1U) {
        unsigned k = 0;
        while (k < 12U && pan->files[first_sel].name[k] != '\0') {
            from_desc[k] = pan->files[first_sel].name[k];
            ++k;
        }
        from_desc[k] = '\0';
    } else {
        sprintf(from_desc, "%u selected files", (unsigned)sel_cnt);
    }

    dsh_fs_build_dir_path(pan, (pan->vis_count > 0 && pan->tree_sel < pan->vis_count)
                               ? pan->vis_dirs[pan->tree_sel] : 0, cur_dir);
    strncpy(to_buf, cur_dir, sizeof(to_buf) - 1U);
    to_buf[sizeof(to_buf) - 1U] = '\0';

    if (!dsh_dlg_copy_or_move(is_move, from_desc, to_buf, sizeof(to_buf))) return;
    if (to_buf[0] == '\0') return;

    {
        unsigned int dst_attr = 0;
        int dst_is_dir = 0;
        unsigned tlen = (unsigned)strlen(to_buf);
        if ((tlen == 2U && to_buf[1] == ':') ||
            (tlen == 3U && to_buf[1] == ':' && (to_buf[2] == '\\' || to_buf[2] == '/'))) {
            dst_is_dir = 1;
        } else if (dsh_fs_get_attr(to_buf, &dst_attr) == 0 && (dst_attr & 0x10U) != 0) {
            dst_is_dir = 1;
        }

        for (i = 0; i < pan->file_count; ++i) {
            char src_full[152], leaf[14], dst_full[152];
            unsigned int ex_attr = 0;
            int rc;
            if (!pan->files[i].selected) continue;
            build_file_full_path(pan, i, src_full, leaf);

            if (dst_is_dir || sel_cnt > 1U) {
                unsigned dl = (unsigned)strlen(to_buf);
                if (dl > 0 && (to_buf[dl - 1U] == '\\' || to_buf[dl - 1U] == '/' || to_buf[dl - 1U] == ':')) {
                    sprintf(dst_full, "%s%s", to_buf, leaf);
                } else {
                    sprintf(dst_full, "%s\\%s", to_buf, leaf);
                }
            } else {
                strncpy(dst_full, to_buf, sizeof(dst_full) - 1U);
                dst_full[sizeof(dst_full) - 1U] = '\0';
            }

            if (g_app.confirm_replace && dsh_fs_get_attr(dst_full, &ex_attr) == 0 && (ex_attr & 0x10U) == 0) {
                char prompt[64];
                int ans;
                sprintf(prompt, "Replace %.26s?", dst_full);
                ans = dsh_dlg_confirm_yes_no_cancel("Replace File Confirmation", "", prompt);
                if (ans < 0) break;
                if (ans == 0) continue;
            }

            rc = is_move ? dsh_fs_move_file(src_full, dst_full) : dsh_fs_copy_file(src_full, dst_full);
            if (rc != 0) {
                dsh_dlg_message(is_move ? "Move Error" : "Copy Error",
                                "Cannot complete file operation:", leaf);
                break;
            }
        }
    }

    dsh_fs_scan_drive(&g_app.pan1, cur_dir);
    if (g_app.view_mode == VIEW_DUAL) {
        char d2[136];
        dsh_fs_build_dir_path(&g_app.pan2, (g_app.pan2.vis_count > 0) ? g_app.pan2.vis_dirs[g_app.pan2.tree_sel] : 0, d2);
        dsh_fs_scan_drive(&g_app.pan2, d2);
    }
}

void dsh_dlg_do_delete(struct dsh_panel *pan)
{
    unsigned short i;
    char cur_dir[136];
    if (pan->file_count == 0) return;

    dsh_fs_build_dir_path(pan, (pan->vis_count > 0 && pan->tree_sel < pan->vis_count)
                               ? pan->vis_dirs[pan->tree_sel] : 0, cur_dir);

    for (i = 0; i < pan->file_count; ++i) {
        char src_full[152], leaf[14], prompt[64];
        int is_ro;
        if (!pan->files[i].selected) continue;
        build_file_full_path(pan, i, src_full, leaf);
        is_ro = (pan->files[i].attrib & 0x01U) != 0;

        if (g_app.confirm_delete || is_ro) {
            int ans;
            sprintf(prompt, "Delete %.26s?", src_full);
            ans = dsh_dlg_confirm_yes_no_cancel(
                "Delete File Confirmation",
                is_ro ? "Warning! File is Read Only!" : "",
                prompt
            );
            if (ans < 0) return;
            if (ans == 0) continue;
        }
        if (dsh_fs_delete_file(src_full, is_ro) != 0) {
            dsh_dlg_message("Delete Error", "Access denied or disk protected:", leaf);
            break;
        }
    }
    dsh_fs_scan_drive(pan, cur_dir);
}

void dsh_dlg_do_rename(struct dsh_panel *pan)
{
    char src_full[152], leaf[14], dir_p[136], new_name[32], dst_full[152], info[56];
    unsigned short idx;

    if (pan->file_count == 0) return;
    idx = pan->file_sel;
    build_file_full_path(pan, idx, src_full, leaf);
    dsh_fs_build_dir_path(pan, pan->files[idx].dir_idx, dir_p);
    sprintf(info, "Current name:  %-14s     1 of     1", leaf);
    new_name[0] = '\0';

    if (!dsh_dlg_input_one("Rename File", info, "New name. .", new_name, sizeof(new_name))) return;
    if (new_name[0] == '\0') return;

    if (strchr(new_name, '\\') || strchr(new_name, ':')) {
        strncpy(dst_full, new_name, sizeof(dst_full) - 1U);
        dst_full[sizeof(dst_full) - 1U] = '\0';
    } else if (strlen(dir_p) > 3U) {
        sprintf(dst_full, "%s\\%s", dir_p, new_name);
    } else {
        sprintf(dst_full, "%s%s", dir_p, new_name);
    }

    if (dsh_fs_rename_path(src_full, dst_full) != 0) {
        dsh_dlg_message("Rename Error", "Cannot rename file:", leaf);
        return;
    }
    dsh_fs_scan_drive(pan, dir_p);
}

void dsh_dlg_do_mkdir(struct dsh_panel *pan)
{
    char parent_p[136], info[60], new_dir[32], full_p[160];
    unsigned short dir_idx = (pan->vis_count > 0 && pan->tree_sel < pan->vis_count)
                             ? pan->vis_dirs[pan->tree_sel] : 0;

    dsh_fs_build_dir_path(pan, dir_idx, parent_p);
    sprintf(info, "Parent name: %.40s", parent_p);
    new_dir[0] = '\0';

    if (!dsh_dlg_input_one("Create Directory", info, "New directory name. .", new_dir, sizeof(new_dir))) return;
    if (new_dir[0] == '\0') return;

    if (strlen(parent_p) > 3U) sprintf(full_p, "%s\\%s", parent_p, new_dir);
    else sprintf(full_p, "%s%s", parent_p, new_dir);

    if (dsh_fs_mkdir_path(full_p) != 0) {
        dsh_dlg_message("Create Directory Error", "Unable to create directory:", new_dir);
        return;
    }

    dsh_fs_scan_drive(pan, parent_p);
    /* Expand parent so the newly created subdirectory is immediately visible */
    if (pan->vis_count > 0 && pan->tree_sel < pan->vis_count) {
        unsigned short pnode = pan->vis_dirs[pan->tree_sel];
        pan->dirs[pnode].expanded = 1;
        dsh_fs_rebuild_vis(pan);
    }
}

void dsh_dlg_do_change_attr(struct dsh_panel *pan)
{
    static const char * const attr_names[4] = {"Hidden", "System", "Archive", "Read only"};
    static const unsigned int attr_bits[4] = {0x02U, 0x04U, 0x20U, 0x01U};
    char src_full[152], leaf[14], dir_p[136], info[56];
    unsigned int cur_attr = 0;
    int row_cur = 0;
    int btn_foc = 0; /* 0=list, 1=OK, 2=Cancel, 3=Help */

    if (pan->file_count == 0) return;
    build_file_full_path(pan, pan->file_sel, src_full, leaf);
    dsh_fs_build_dir_path(pan, pan->files[pan->file_sel].dir_idx, dir_p);
    cur_attr = pan->files[pan->file_sel].attrib;
    sprintf(info, "File:  %-16s           1 of     1", leaf);

    for (;;) {
        int i;
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(7, 14, 21, 65, g_theme.dlg_box, "Change Attributes", 1);
        dsh_ui_write(10, 18, g_theme.dlg_box, info);
        dsh_ui_write(12, 18, g_theme.dlg_box, "To change attribute, select item and press");
        dsh_ui_write(13, 18, g_theme.dlg_box, "the SPACEBAR. Press ENTER when complete.");

        for (i = 0; i < 4; ++i) {
            unsigned r = (unsigned)(15 + i);
            unsigned char a = (btn_foc == 0 && row_cur == i) ? g_theme.dlg_btn_sel : g_theme.dlg_box;
            if (cur_attr & attr_bits[i]) {
                dsh_ui_put_cell(r, 17, CH_TRI_RIGHT, g_theme.dlg_box);
            }
            dsh_ui_write(r, 19, a, attr_names[i]);
        }

        dsh_ui_write(20, 23, (btn_foc == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " OK ");
        dsh_ui_write(20, 36, (btn_foc == 2) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Cancel ");
        dsh_ui_write(20, 52, (btn_foc == 3) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Help ");
        dsh_ui_cursor(20, (btn_foc == 2) ? 37U : ((btn_foc == 3) ? 53U : 24U));
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) continue;
            if (ascii == 27) return;
            if (scan == 0x3BU) { dsh_show_help_topic(HELP_COMMANDS); continue; }
            if (ascii == 9) { btn_foc = (btn_foc + 1) % 4; continue; }
            if (scan == 0x0FU) { btn_foc = (btn_foc + 3) % 4; continue; }
            if (btn_foc == 0) {
                if (scan == 0x50U && row_cur < 3) { ++row_cur; continue; }
                if (scan == 0x48U && row_cur > 0) { --row_cur; continue; }
                if (ascii == ' ') {
                    cur_attr ^= attr_bits[row_cur];
                    continue;
                }
            }
            if (ascii == 13) {
                if (btn_foc == 2) return;
                if (btn_foc == 3) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                if (dsh_fs_set_attr(src_full, cur_attr) != 0) {
                    dsh_dlg_message("Attribute Error", "Cannot change file attributes:", leaf);
                    return;
                }
                dsh_fs_scan_drive(pan, dir_p);
                return;
            }
        }
    }
}

void dsh_dlg_do_search(struct dsh_panel *pan)
{
    char pat[16];
    char cur_dir[136], info[60];
    int entire_disk = 1;
    int foc = 0; /* 0=input, 1=checkbox, 2=OK, 3=Cancel, 4=Help */
    int untouched = 1;

    strcpy(pat, "*.*");
    dsh_fs_build_dir_path(pan, (pan->vis_count > 0 && pan->tree_sel < pan->vis_count)
                               ? pan->vis_dirs[pan->tree_sel] : 0, cur_dir);
    sprintf(info, "Current Directory is %.34s", cur_dir);

    for (;;) {
        char chk[32];
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(6, 12, 17, 60, g_theme.dlg_box, "Search File", 1);
        dsh_ui_write(8, 14, g_theme.dlg_box, info);
        dsh_ui_write(10, 14, g_theme.dlg_box, "Search for. .");
        draw_input_field(10, 28, 13, pat, foc == 0);

        sprintf(chk, "[%c] Search entire disk", entire_disk ? 'X' : ' ');
        dsh_ui_write(12, 21, (foc == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, chk);
        if (foc == 1) dsh_ui_cursor(12, 22);

        dsh_ui_write(16, 19, (foc == 2) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " OK ");
        dsh_ui_write(16, 32, (foc == 3) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Cancel ");
        dsh_ui_write(16, 48, (foc == 4) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Help ");
        if (foc >= 2) {
            dsh_ui_cursor(16, (foc == 2) ? 20U : ((foc == 3) ? 33U : 49U));
        }
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) continue;
            if (ascii == 27) return;
            if (scan == 0x3BU) { dsh_show_help_topic(HELP_COMMANDS); continue; }
            if (ascii == 9) { foc = (foc + 1) % 5; continue; }
            if (scan == 0x0FU) { foc = (foc + 4) % 5; continue; }
            if (foc == 1 && ascii == ' ') { entire_disk = !entire_disk; continue; }
            if (ascii == 13) {
                if (foc == 3) return;
                if (foc == 4) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                if (pat[0] == '\0') strcpy(pat, "*.*");
                strupr_in_place(pat);
                dsh_run_search_results(pat, entire_disk);
                return;
            }
            if (foc == 0) {
                edit_field_key(pat, sizeof(pat), &untouched, scan, ascii);
            }
        }
    }
}

void dsh_dlg_file_display_options(void)
{
    static const char * const sort_labels[5] = {"Name", "Extension", "Date", "Size", "DiskOrder"};
    char pat[14];
    int show_hid = g_app.show_hidden;
    int desc = g_app.sort_desc;
    int smode = g_app.sort_mode;
    int foc = 0; /* 0=Name, 1=Hidden, 2=Descending, 3=SortBy, 4=OK, 5=Cancel, 6=Help */
    int untouched = 1;

    strncpy(pat, g_app.filter_pat, 13);
    pat[13] = '\0';

    for (;;) {
        char cb1[36], cb2[32];
        int i;
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(7, 16, 21, 68, g_theme.dlg_box, "File Display Options", 1);
        dsh_ui_write(9, 18, g_theme.dlg_box, "Name:");
        draw_input_field(9, 27, 13, pat, foc == 0);

        dsh_ui_write(11, 52, g_theme.dlg_box, "Sort by:");

        sprintf(cb1, "[%c] Display hidden/system files", show_hid ? 'X' : ' ');
        dsh_ui_write(13, 18, (foc == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, cb1);
        if (foc == 1) dsh_ui_cursor(13, 19);

        sprintf(cb2, "[%c] Descending order", desc ? 'X' : ' ');
        dsh_ui_write(16, 18, (foc == 2) ? g_theme.dlg_btn_sel : g_theme.dlg_box, cb2);
        if (foc == 2) dsh_ui_cursor(16, 19);

        for (i = 0; i < 5; ++i) {
            char rb[20];
            unsigned r = (unsigned)(13 + i);
            sprintf(rb, "( ) %s", sort_labels[i]);
            dsh_ui_write(r, 52, (foc == 3 && smode == i) ? g_theme.dlg_btn_sel : g_theme.dlg_box, rb);
            if (smode == i) {
                dsh_ui_put_cell(r, 53, CH_BULLET, (foc == 3) ? g_theme.dlg_btn_sel : g_theme.dlg_box);
            }
        }
        if (foc == 3) dsh_ui_cursor((unsigned)(13 + smode), 53);

        dsh_ui_write(20, 25, (foc == 4) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " OK ");
        dsh_ui_write(20, 38, (foc == 5) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Cancel ");
        dsh_ui_write(20, 54, (foc == 6) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Help ");
        if (foc >= 4) {
            dsh_ui_cursor(20, (foc == 4) ? 26U : ((foc == 5) ? 39U : 55U));
        }
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) continue;
            if (ascii == 27) return;
            if (scan == 0x3BU) { dsh_show_help_topic(HELP_COMMANDS); continue; }
            if (ascii == 9) { foc = (foc + 1) % 7; continue; }
            if (scan == 0x0FU) { foc = (foc + 6) % 7; continue; }
            if (foc == 1 && ascii == ' ') { show_hid = !show_hid; continue; }
            if (foc == 2 && ascii == ' ') { desc = !desc; continue; }
            if (foc == 3) {
                if (scan == 0x50U && smode < 4) { ++smode; continue; }
                if (scan == 0x48U && smode > 0) { --smode; continue; }
            }
            if (ascii == 13) {
                char cur_p[136];
                if (foc == 5) return;
                if (foc == 6) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                if (pat[0] == '\0') strcpy(pat, "*.*");
                strupr_in_place(pat);
                strncpy(g_app.filter_pat, pat, 13);
                g_app.filter_pat[13] = '\0';
                g_app.show_hidden = (unsigned char)show_hid;
                g_app.sort_desc = (unsigned char)desc;
                g_app.sort_mode = (unsigned char)smode;
                g_app.ini_dirty = 1;
                dsh_fs_build_dir_path(&g_app.pan1,
                    (g_app.pan1.vis_count > 0) ? g_app.pan1.vis_dirs[g_app.pan1.tree_sel] : 0, cur_p);
                dsh_fs_scan_drive(&g_app.pan1, cur_p);
                if (g_app.view_mode == VIEW_DUAL) dsh_fs_scan_files(&g_app.pan2);
                return;
            }
            if (foc == 0) {
                edit_field_key(pat, sizeof(pat), &untouched, scan, ascii);
            }
        }
    }
}

void dsh_dlg_confirmation(void)
{
    int c_del = g_app.confirm_delete;
    int c_rep = g_app.confirm_replace;
    int c_mse = g_app.confirm_mouse;
    int foc = 3; /* 0..2 checkboxes, 3=OK, 4=Cancel, 5=Help */

    for (;;) {
        char s0[34], s1[34], s2[34];
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(5, 4, 15, 43, g_theme.dlg_box, "Confirmation", 1);
        sprintf(s0, "[%c] Confirm on Delete", c_del ? 'X' : ' ');
        sprintf(s1, "[%c] Confirm on Replace", c_rep ? 'X' : ' ');
        sprintf(s2, "[%c] Confirm on Mouse Operation", c_mse ? 'X' : ' ');
        dsh_ui_write(8, 8, (foc == 0) ? g_theme.dlg_btn_sel : g_theme.dlg_box, s0);
        dsh_ui_write(9, 8, (foc == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, s1);
        dsh_ui_write(10, 8, (foc == 2) ? g_theme.dlg_btn_sel : g_theme.dlg_box, s2);

        dsh_ui_write(14, 10, (foc == 3) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " OK ");
        dsh_ui_write(14, 20, (foc == 4) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Cancel ");
        dsh_ui_write(14, 33, (foc == 5) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Help ");
        if (foc < 3) dsh_ui_cursor((unsigned)(8 + foc), 9);
        else dsh_ui_cursor(14, (foc == 3) ? 11U : ((foc == 4) ? 21U : 34U));
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) continue;
            if (ascii == 27) return;
            if (scan == 0x3BU) { dsh_show_help_topic(HELP_COMMANDS); continue; }
            if (ascii == 9) { foc = (foc + 1) % 6; continue; }
            if (scan == 0x0FU) { foc = (foc + 5) % 6; continue; }
            if (ascii == ' ') {
                if (foc == 0) c_del = !c_del;
                else if (foc == 1) c_rep = !c_rep;
                else if (foc == 2) c_mse = !c_mse;
                continue;
            }
            if (ascii == 13) {
                if (foc == 4) return;
                if (foc == 5) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                g_app.confirm_delete = (unsigned char)c_del;
                g_app.confirm_replace = (unsigned char)c_rep;
                g_app.confirm_mouse = (unsigned char)c_mse;
                g_app.ini_dirty = 1;
                return;
            }
        }
    }
}

void dsh_dlg_show_information(const struct dsh_panel *pan)
{
    char fname[14], attr_s[6], dname[14], num_s[16], line[32];
    unsigned short sel_cnt = 0, dir_files = 0, dir_idx = 0, i;
    unsigned long sel_bytes = 0, dir_bytes = 0;
    int btn = 0;

    fname[0] = '\0';
    strcpy(attr_s, "....");
    if (pan->vis_count > 0 && pan->tree_sel < pan->vis_count) {
        dir_idx = pan->vis_dirs[pan->tree_sel];
    }
    if (pan->file_count > 0 && pan->file_sel < pan->file_count) {
        unsigned char a = pan->files[pan->file_sel].attrib;
        unsigned k = 0;
        while (k < 12U && pan->files[pan->file_sel].name[k] != '\0') {
            fname[k] = pan->files[pan->file_sel].name[k];
            ++k;
        }
        fname[k] = '\0';
        dir_idx = pan->files[pan->file_sel].dir_idx;
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
        if (pan->files[i].dir_idx == dir_idx) {
            ++dir_files;
            dir_bytes += pan->files[i].size;
        }
    }
    dname[0] = '\0';
    if (pan->dirs != 0 && dir_idx < pan->dir_count) {
        unsigned k = 0;
        while (k < 12U && pan->dirs[dir_idx].name[k] != '\0') {
            dname[k] = pan->dirs[dir_idx].name[k];
            ++k;
        }
        dname[k] = '\0';
    }

    for (;;) {
        unsigned r = 4;
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(2, 6, 22, 35, g_theme.dlg_box, "Show Information", 1);

        dsh_ui_write(r++, 8, g_theme.dlg_box, "File");
        sprintf(line, "  Name  : %-12s", fname);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        sprintf(line, "  Attr  : %s", attr_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);

        sprintf(line, "Selected             %c", (char)('A' + pan->drive));
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        dsh_format_size_commas(sel_cnt, num_s, 11);
        sprintf(line, "  Number: %s", num_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        dsh_format_size_commas(sel_bytes, num_s, 11);
        sprintf(line, "  Size  : %s", num_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);

        dsh_ui_write(r++, 8, g_theme.dlg_box, "Directory");
        sprintf(line, "  Name  : %-12s", dname);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        dsh_format_size_commas(dir_bytes, num_s, 11);
        sprintf(line, "  Size  : %s", num_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        dsh_format_size_commas(dir_files, num_s, 11);
        sprintf(line, "  Files : %s", num_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);

        dsh_ui_write(r++, 8, g_theme.dlg_box, "Disk");
        sprintf(line, "  Name  : %-12s", pan->vol_label[0] ? pan->vol_label : "NONE");
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        dsh_format_size_commas(pan->disk_total, num_s, 11);
        sprintf(line, "  Size  : %s", num_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        dsh_format_size_commas(pan->disk_avail, num_s, 11);
        sprintf(line, "  Avail : %s", num_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        dsh_format_size_commas(pan->total_disk_files, num_s, 11);
        sprintf(line, "  Files : %s", num_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);
        dsh_format_size_commas(pan->total_disk_dirs, num_s, 11);
        sprintf(line, "  Dirs  : %s", num_s);
        dsh_ui_write(r++, 8, g_theme.dlg_box, line);

        dsh_ui_write(21, 10, (btn == 0) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Close ");
        dsh_ui_write(21, 23, (btn == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Help ");
        dsh_ui_cursor(21, (btn == 0) ? 11U : 24U);
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) return;
            if (ascii == 27) return;
            if (scan == 0x3BU) { dsh_show_help_topic(HELP_COMMANDS); continue; }
            if (ascii == 9 || scan == 0x4DU || scan == 0x4BU) { btn = 1 - btn; continue; }
            if (ascii == 13) {
                if (btn == 1) { dsh_show_help_topic(HELP_COMMANDS); continue; }
                return;
            }
        }
    }
}

void dsh_dlg_display_mode(void)
{
    int sel = (g_app.screen_rows > 25U) ? 1 : 0;
    for (;;) {
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(7, 16, 17, 63, g_theme.dlg_box, "Screen Display Mode", 1);
        dsh_ui_write(10, 22, (sel == 0) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Text  25 lines  Low Resolution  ");
        dsh_ui_write(11, 22, (sel == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Text  50 lines  High Resolution ");
        dsh_ui_write(15, 26, g_theme.dlg_btn_sel, " OK ");
        dsh_ui_write(15, 42, g_theme.dlg_box, " Cancel ");
        dsh_ui_cursor(15, 27);
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);
            if (hi == 1U) continue;
            if (ascii == 27) return;
            if (scan == 0x48U || scan == 0x50U) { sel = 1 - sel; continue; }
            if (ascii == 13) {
                g_app.req_rows = sel ? 50U : 25U;
                dsh_ui_done();
                dsh_ui_init(g_app.save_seg, g_app.bw_mode, g_app.req_rows);
                g_app.screen_rows = dsh_ui_rows();
                g_app.ini_dirty = 1;
                return;
            }
        }
    }
}

void dsh_dlg_colors(void)
{
    static const char * const names[5] = {
        "Basic Blue", "Monochrome-2 Colors", "Ocean", "Emerald", "Reverse"
    };
    int sel = g_app.color_scheme;
    for (;;) {
        int i;
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(6, 18, 18, 61, g_theme.dlg_box, "Color Scheme", 1);
        dsh_ui_write(8, 22, g_theme.dlg_box, "Current Color: ");
        dsh_ui_write(8, 37, g_theme.dlg_box, names[sel]);
        for (i = 0; i < 5; ++i) {
            dsh_ui_write((unsigned)(10 + i), 24,
                         (sel == i) ? g_theme.dlg_btn_sel : g_theme.dlg_box, names[i]);
        }
        dsh_ui_write(16, 26, g_theme.dlg_btn_sel, " OK ");
        dsh_ui_write(16, 42, g_theme.dlg_box, " Cancel ");
        dsh_ui_cursor(16, 27);
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);
            if (hi == 1U) continue;
            if (ascii == 27) return;
            if (scan == 0x48U && sel > 0) { --sel; continue; }
            if (scan == 0x50U && sel < 4) { ++sel; continue; }
            if (ascii == 13) {
                g_app.color_scheme = (unsigned char)sel;
                g_app.bw_mode = (unsigned char)(sel == 1 ? 1 : 0);
                dsh_apply_theme();
                dsh_ui_done();
                dsh_ui_init(g_app.save_seg, g_app.bw_mode, g_app.req_rows);
                g_app.screen_rows = dsh_ui_rows();
                g_app.ini_dirty = 1;
                return;
            }
        }
    }
}

void dsh_dlg_do_run(void)
{
    char cmd[66];
    char cwd[136];
    cmd[0] = '\0';
    if (!dsh_dlg_input_one("Run", "Enter command line to execute:", "Command Line:", cmd, sizeof(cmd))) {
        return;
    }
    if (cmd[0] == '\0') return;
    dsh_fs_build_dir_path(&g_app.pan1,
        (g_app.pan1.vis_count > 0) ? g_app.pan1.vis_dirs[g_app.pan1.tree_sel] : 0, cwd);
    dsh_exec_command(cmd, cwd, 1, 0);
}

void dsh_dlg_do_associate(struct dsh_panel *pan)
{
    char leaf[14], src_full[152], ext[4], info[56], prog[48];
    const char *dot;
    unsigned i;

    if (pan->file_count == 0) return;
    build_file_full_path(pan, pan->file_sel, src_full, leaf);
    dot = strchr(leaf, '.');
    if (dot && dot[1] != '\0') {
        strncpy(ext, dot + 1, 3);
        ext[3] = '\0';
    } else {
        strcpy(ext, "TXT");
    }
    prog[0] = '\0';
    for (i = 0; i < g_app.assoc_count; ++i) {
        if (strcmp(g_app.assocs[i].ext, ext) == 0) {
            strcpy(prog, g_app.assocs[i].command);
            break;
        }
    }
    sprintf(info, "Associate extension .%s with program:", ext);
    if (!dsh_dlg_input_one("Associate File", info, "Programs:", prog, sizeof(prog))) return;

    for (i = 0; i < g_app.assoc_count; ++i) {
        if (strcmp(g_app.assocs[i].ext, ext) == 0) {
            strncpy(g_app.assocs[i].command, prog, 47);
            g_app.assocs[i].command[47] = '\0';
            g_app.ini_dirty = 1;
            return;
        }
    }
    if (prog[0] != '\0' && g_app.assoc_count < DSH_MAX_ASSOC) {
        strcpy(g_app.assocs[g_app.assoc_count].ext, ext);
        strncpy(g_app.assocs[g_app.assoc_count].command, prog, 47);
        g_app.assocs[g_app.assoc_count].command[47] = '\0';
        ++g_app.assoc_count;
        g_app.ini_dirty = 1;
    }
}

void dsh_dlg_about(void)
{
    for (;;) {
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(6, 17, 14, 63, g_theme.dlg_box, "About Shell", 1);
        dsh_ui_write(8, 34, g_theme.dlg_box, "CK-DOS Shell");
        dsh_ui_write(9, 31, g_theme.dlg_box, "CK-DOS Version 5.00");
        dsh_ui_write(11, 19, g_theme.dlg_box, "Copyright (c) 2026 C. Klukas. All rights");
        dsh_ui_write(12, 36, g_theme.dlg_box, "reserved.");
        dsh_ui_write(13, 37, g_theme.dlg_btn_sel, " Close ");
        dsh_ui_cursor(13, 38);
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);
            if (hi == 1U) return;
            if (ascii == 13 || ascii == 27 || ascii == ' ' || ascii == 'c' || ascii == 'C') return;
        }
    }
}
