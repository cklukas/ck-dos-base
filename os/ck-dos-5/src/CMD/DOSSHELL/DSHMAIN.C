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

struct dsh_app g_app;

static void write_stdout(const char *s)
{
#ifdef __CKCL__
    unsigned int nw = 0;
    _dos_write(1, s, (unsigned int)strlen(s), &nw);
#else
    fputs(s, stdout);
#endif
}

static void print_usage(void)
{
    static const char msg[] =
        "\r\n"
        "CK-DOS Version 5.0  CK-DOS Shell\r\n"
        "Copyright (c) 2026 C. Klukas. All rights reserved.\r\n"
        "\r\n"
        "Starts CK-DOS Shell.\r\n"
        "\r\n"
        "DOSSHELL [/T[:res[n]]] [/B]\r\n"
        "DOSSHELL [/G[:res[n]]] [/B]\r\n"
        "\r\n"
        "  /T        Starts CK-DOS Shell in text mode.\r\n"
        "  :res[n]   A letter (L, M, H) and number indicating screen resolution.\r\n"
        "  /B        Starts CK-DOS Shell using black-and-white color scheme.\r\n"
        "  /G        Starts CK-DOS Shell in graphics mode.\r\n";
    write_stdout(msg);
}

static int parse_cli_tail(int *show_help_only)
{
#ifdef __CKCL__
    char tail[130];
    const unsigned char __far *psp = (const unsigned char __far *)MK_FP(_psp, 0x80U);
    unsigned len = psp[0];
    unsigned i = 0;

    *show_help_only = 0;
    if (len > 126U) len = 126U;
    for (i = 0; i < len; ++i) {
        char c = (char)psp[1U + i];
        if (c == '\r' || c == '\0') break;
        tail[i] = c;
    }
    tail[i] = '\0';

    i = 0;
    while (tail[i] != '\0') {
        while (tail[i] == ' ' || tail[i] == '\t') ++i;
        if (tail[i] == '\0') break;
        if (tail[i] == '/' || tail[i] == '-') {
            char sw;
            ++i;
            sw = tail[i];
            if (sw >= 'a' && sw <= 'z') sw = (char)(sw - 32);
            if (sw == '?') {
                *show_help_only = 1;
                return 0;
            }
            if (sw == 'B') {
                g_app.bw_mode = 1;
                g_app.color_scheme = 1;
                ++i;
            } else if (sw == 'T' || sw == 'G') {
                ++i;
                if (tail[i] == ':') {
                    char res;
                    ++i;
                    res = tail[i];
                    if (res >= 'a' && res <= 'z') res = (char)(res - 32);
                    if (res == 'H' || res == 'M' || res == '2') {
                        g_app.req_rows = 50U;
                        ++i;
                    } else if (res == 'L' || res == '1') {
                        g_app.req_rows = 25U;
                        ++i;
                    }
                    while (tail[i] >= '0' && tail[i] <= '9') ++i;
                }
            } else {
                write_stdout("Invalid switch\r\n");
                return -1;
            }
        } else {
            while (tail[i] != '\0' && tail[i] != ' ' && tail[i] != '\t' && tail[i] != '/') {
                ++i;
            }
        }
    }
#else
    *show_help_only = 0;
#endif
    return 0;
}

static void resolve_ini_path(void)
{
#ifdef __CKCL__
    const char *env = getenv("DOSSHELL");
    unsigned int attr = 0;
    if (env && env[0] != '\0') {
        unsigned l = (unsigned)strlen(env);
        if (l < 110U) {
            strcpy(g_app.ini_path, env);
            if (g_app.ini_path[l - 1U] != '\\' && g_app.ini_path[l - 1U] != '/' &&
                g_app.ini_path[l - 1U] != ':') {
                strcat(g_app.ini_path, "\\");
            }
            strcat(g_app.ini_path, "DOSSHELL.INI");
            return;
        }
    }
    if (dsh_fs_get_attr("C:\\DOS", &attr) == 0 && (attr & 0x10U) != 0) {
        strcpy(g_app.ini_path, "C:\\DOS\\DOSSHELL.INI");
        return;
    }
#endif
    strcpy(g_app.ini_path, "DOSSHELL.INI");
}

static void bind_workspace(void)
{
#ifdef __CKCL__
    unsigned char __far *base = (unsigned char __far *)MK_FP(g_app.data_seg, 0);
    g_app.pan1.dirs = (struct dsh_dir_node __far *)base;
    g_app.pan1.max_dirs = DSH_MAX_DIRS_MAIN;
    base += (unsigned)(DSH_MAX_DIRS_MAIN * sizeof(struct dsh_dir_node));

    g_app.pan1.files = (struct dsh_file_entry __far *)base;
    g_app.pan1.max_files = DSH_MAX_FILES_MAIN;
    base += (unsigned)(DSH_MAX_FILES_MAIN * sizeof(struct dsh_file_entry));

    g_app.pan2.dirs = (struct dsh_dir_node __far *)base;
    g_app.pan2.max_dirs = DSH_MAX_DIRS_SEC;
    base += (unsigned)(DSH_MAX_DIRS_SEC * sizeof(struct dsh_dir_node));

    g_app.pan2.files = (struct dsh_file_entry __far *)base;
    g_app.pan2.max_files = DSH_MAX_FILES_SEC;
#endif
}

static void cycle_focus(int backward)
{
    static const unsigned char ring_pf[4] = {FOCUS_DRIVE, FOCUS_TREE, FOCUS_FILES, FOCUS_PROGS};
    static const unsigned char ring_sg[3] = {FOCUS_DRIVE, FOCUS_TREE, FOCUS_FILES};
    static const unsigned char ring_dl[6] = {
        FOCUS_DRIVE, FOCUS_TREE, FOCUS_FILES, FOCUS_DRIVE2, FOCUS_TREE2, FOCUS_FILES2
    };
    static const unsigned char ring_al[2] = {FOCUS_DRIVE, FOCUS_FILES};

    const unsigned char *ring = ring_pf;
    int rlen = 4;
    int i, pos = 0;

    if (g_app.view_mode == VIEW_PROG) {
        g_app.focus = FOCUS_PROGS;
        return;
    }
    if (g_app.view_mode == VIEW_SINGLE) { ring = ring_sg; rlen = 3; }
    else if (g_app.view_mode == VIEW_DUAL) { ring = ring_dl; rlen = 6; }
    else if (g_app.view_mode == VIEW_ALL) { ring = ring_al; rlen = 2; }

    for (i = 0; i < rlen; ++i) {
        if (ring[i] == g_app.focus) { pos = i; break; }
    }
    if (backward) pos = (pos + rlen - 1) % rlen;
    else pos = (pos + 1) % rlen;
    g_app.focus = ring[pos];
}

static unsigned tree_visible_rows(int second_panel)
{
    unsigned H = g_app.screen_rows;
    if (g_app.view_mode == VIEW_SINGLE) return H - 8U;
    if (g_app.view_mode == VIEW_DUAL) {
        unsigned sb = (H >= 40U) ? 24U : 12U;
        return second_panel ? (H - 2U - (sb + 5U)) : (sb - 6U);
    }
    /* VIEW_PROG_FILE */
    return ((H >= 40U) ? 26U : 14U) - 6U;
}

static unsigned file_visible_rows(int second_panel)
{
    if (g_app.view_mode == VIEW_ALL) return g_app.screen_rows - 8U;
    return tree_visible_rows(second_panel);
}

static unsigned prog_visible_rows(void)
{
    unsigned H = g_app.screen_rows;
    if (g_app.view_mode == VIEW_PROG) return H - 5U;
    return (H - 2U) - (((H >= 40U) ? 26U : 14U) + 2U);
}

static void select_single_file(struct dsh_panel *pan, unsigned short new_sel, int extend_shift)
{
    unsigned short i;
    if (pan->file_count == 0) return;
    if (new_sel >= pan->file_count) new_sel = (unsigned short)(pan->file_count - 1U);
    if (!extend_shift && !g_app.select_across) {
        for (i = 0; i < pan->file_count; ++i) {
            pan->files[i].selected = 0;
        }
    }
    pan->file_sel = new_sel;
    pan->files[new_sel].selected = 1;
}

static void handle_mouse_click(unsigned mrow, unsigned mcol)
{
    unsigned H = g_app.screen_rows;
    if (mrow == 1U) {
        dsh_run_menu_bar(0);
        return;
    }
    if (g_app.view_mode != VIEW_PROG && mrow == 3U) {
        unsigned idx = (mcol >= 1U) ? ((mcol - 1U) / 6U) : 0U;
        if (idx < g_app.drive_count) {
            g_app.focus = FOCUS_DRIVE;
            g_app.drive_cursor = (unsigned char)idx;
            if (g_app.pan1.drive != g_app.drives[idx]) {
                g_app.pan1.drive = g_app.drives[idx];
                dsh_fs_scan_drive(&g_app.pan1, 0);
            }
        }
        return;
    }
    if (g_app.view_mode == VIEW_PROG_FILE) {
        unsigned sb = (H >= 40U) ? 26U : 14U;
        if (mrow >= 6U && mrow < sb) {
            if (mcol < 36U) {
                unsigned idx = g_app.pan1.tree_top + (mrow - 6U);
                g_app.focus = FOCUS_TREE;
                if (idx < g_app.pan1.vis_count) {
                    g_app.pan1.tree_sel = (unsigned short)idx;
                    dsh_fs_scan_files(&g_app.pan1);
                }
            } else {
                unsigned idx = g_app.pan1.file_top + (mrow - 6U);
                g_app.focus = FOCUS_FILES;
                if (idx < g_app.pan1.file_count) {
                    select_single_file(&g_app.pan1, (unsigned short)idx, 0);
                }
            }
        } else if (mrow >= sb + 2U && mrow < H - 2U) {
            unsigned idx = g_app.prog_top + (mrow - (sb + 2U));
            g_app.focus = FOCUS_PROGS;
            if (idx < dsh_prog_visible_count()) {
                g_app.prog_sel = (unsigned char)idx;
            }
        }
    }
}

static void handle_key_event(unsigned char shift_flags, unsigned char scan, unsigned char ascii)
{
    int prog_mode = (g_app.focus == FOCUS_PROGS || g_app.view_mode == VIEW_PROG);
    int is_sec = (g_app.focus == FOCUS_DRIVE2 || g_app.focus == FOCUS_TREE2 || g_app.focus == FOCUS_FILES2);
    struct dsh_panel *pan = is_sec ? &g_app.pan2 : &g_app.pan1;

    /* Global function & Alt shortcuts */
    if (scan == 0x3BU) { /* F1 */
        dsh_show_help_topic(HELP_CONTEXT);
        return;
    }
    if (scan == 0x3DU || scan == 0x6BU) { /* F3 or Alt+F4 */
        g_app.running = 0;
        return;
    }
    if (scan == 0x3FU) { /* F5 Refresh */
        if (g_app.view_mode != VIEW_PROG) {
            char cur_p[136];
            dsh_fs_build_dir_path(pan, (pan->vis_count > 0) ? pan->vis_dirs[pan->tree_sel] : 0, cur_p);
            dsh_fs_scan_drive(pan, cur_p);
        }
        return;
    }
    if (scan == 0x58U) { /* Shift+F5 Repaint */
        return;
    }
    if (scan == 0x5CU) { /* Shift+F9 Command Prompt */
        char cwd[136];
        dsh_fs_build_dir_path(&g_app.pan1,
            (g_app.pan1.vis_count > 0) ? g_app.pan1.vis_dirs[g_app.pan1.tree_sel] : 0, cwd);
        dsh_exec_command("", cwd, 0, 1);
        return;
    }
    if (scan == 0x41U && (g_app.focus == FOCUS_FILES || g_app.focus == FOCUS_FILES2)) { /* F7 Move */
        dsh_dlg_do_copy_or_move(pan, 1);
        return;
    }
    if (scan == 0x42U && (g_app.focus == FOCUS_FILES || g_app.focus == FOCUS_FILES2)) { /* F8 Copy */
        dsh_dlg_do_copy_or_move(pan, 0);
        return;
    }
    if (scan == 0x43U && (g_app.focus == FOCUS_FILES || g_app.focus == FOCUS_FILES2)) { /* F9 View */
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
        return;
    }

    /* Menu bar activations */
    if (scan == 0x44U || (scan == 0x38U && ascii == 0)) { dsh_run_menu_bar(-1); return; }
    if (scan == 0x21U && ascii == 0) { dsh_run_menu_bar(0); return; } /* Alt+F */
    if (scan == 0x18U && ascii == 0) { dsh_run_menu_bar(1); return; } /* Alt+O */
    if (scan == 0x2FU && ascii == 0) { dsh_run_menu_bar(2); return; } /* Alt+V */
    if (scan == 0x14U && ascii == 0 && !prog_mode) { dsh_run_menu_bar(3); return; } /* Alt+T */
    if (scan == 0x23U && ascii == 0) { dsh_run_menu_bar(prog_mode ? 3 : 4); return; } /* Alt+H */

    /* Tab / Shift+Tab focus ring */
    if (ascii == 9 && scan == 0x0FU) {
        cycle_focus(0);
        return;
    }
    if (ascii == 0 && scan == 0x0FU) {
        cycle_focus(1);
        return;
    }

    /* Ctrl+* Expand All */
    if (scan == 0x96U || ((shift_flags & 0x04U) != 0 && (scan == 0x37U || ascii == '*'))) {
        if (g_app.view_mode != VIEW_PROG && g_app.view_mode != VIEW_ALL) {
            dsh_fs_tree_expand_all(pan);
        }
        return;
    }

    /* Ctrl+A..Z Drive switch (only when Ctrl shift flag is held!) */
    if ((shift_flags & 0x04U) != 0 && ascii >= 1 && ascii <= 26 && g_app.view_mode != VIEW_PROG) {
        unsigned char want = (unsigned char)(ascii - 1);
        unsigned i;
        for (i = 0; i < g_app.drive_count; ++i) {
            if (g_app.drives[i] == want) {
                if (is_sec) g_app.drive2_cursor = (unsigned char)i;
                else g_app.drive_cursor = (unsigned char)i;
                pan->drive = want;
                dsh_fs_scan_drive(pan, 0);
                break;
            }
        }
        return;
    }

    /* Focus-specific keys */
    if (g_app.focus == FOCUS_DRIVE || g_app.focus == FOCUS_DRIVE2) {
        unsigned char *cur = is_sec ? &g_app.drive2_cursor : &g_app.drive_cursor;
        if (scan == 0x4BU && *cur > 0) { --(*cur); }
        else if (scan == 0x4DU && (unsigned)(*cur) + 1U < g_app.drive_count) { ++(*cur); }
        else if (ascii == 13 || ascii == ' ') {
            pan->drive = g_app.drives[*cur];
            dsh_fs_scan_drive(pan, 0);
        } else {
            char up = (ascii >= 'a' && ascii <= 'z') ? (char)(ascii - 32) : (char)ascii;
            if (up >= 'A' && up <= 'Z') {
                unsigned i;
                unsigned char want = (unsigned char)(up - 'A');
                for (i = 0; i < g_app.drive_count; ++i) {
                    if (g_app.drives[i] == want) {
                        *cur = (unsigned char)i;
                        pan->drive = want;
                        dsh_fs_scan_drive(pan, 0);
                        break;
                    }
                }
            }
        }
        return;
    }

    if (g_app.focus == FOCUS_TREE || g_app.focus == FOCUS_TREE2) {
        unsigned vrows = tree_visible_rows(is_sec);
        unsigned short old_sel = pan->tree_sel;

        if (ascii == '+') { dsh_fs_tree_expand_one(pan); return; }
        if (ascii == '-') { dsh_fs_tree_collapse(pan); return; }
        if (ascii == '*') { dsh_fs_tree_expand_branch(pan); return; }
        if (ascii == 13 && pan->vis_count > 0) {
            unsigned short n = pan->vis_dirs[pan->tree_sel];
            if (pan->dirs[n].has_children) {
                if (pan->dirs[n].expanded) dsh_fs_tree_collapse(pan);
                else dsh_fs_tree_expand_one(pan);
            }
            return;
        }
        if (scan == 0x50U && pan->tree_sel + 1U < pan->vis_count) ++pan->tree_sel;
        else if (scan == 0x48U && pan->tree_sel > 0U) --pan->tree_sel;
        else if (scan == 0x47U) pan->tree_sel = 0;
        else if (scan == 0x4FU && pan->vis_count > 0U) pan->tree_sel = (unsigned short)(pan->vis_count - 1U);
        else if (scan == 0x51U && pan->vis_count > 0U) {
            unsigned n = pan->tree_sel + vrows;
            pan->tree_sel = (n < pan->vis_count) ? (unsigned short)n : (unsigned short)(pan->vis_count - 1U);
        } else if (scan == 0x49U) {
            pan->tree_sel = (pan->tree_sel > vrows) ? (unsigned short)(pan->tree_sel - vrows) : 0U;
        }

        if (pan->tree_sel < pan->tree_top) pan->tree_top = pan->tree_sel;
        else if (pan->tree_sel >= pan->tree_top + vrows) {
            pan->tree_top = (unsigned short)(pan->tree_sel - vrows + 1U);
        }
        if (pan->tree_sel != old_sel) {
            dsh_fs_scan_files(pan);
        }
        return;
    }

    if (g_app.focus == FOCUS_FILES || g_app.focus == FOCUS_FILES2) {
        unsigned frows = file_visible_rows(is_sec);
        int sh = (shift_flags & 0x03U) != 0;

        if (pan->file_count == 0) return;
        if (ascii == ' ') {
            pan->files[pan->file_sel].selected = (unsigned char)!pan->files[pan->file_sel].selected;
            return;
        }
        if (scan == 0x53U) { /* Delete */
            dsh_dlg_do_delete(pan);
            return;
        }
        if (ascii == 13) { /* Enter -> Open */
            dsh_open_selected_file(pan);
            return;
        }
        if (scan == 0x50U && pan->file_sel + 1U < pan->file_count) {
            select_single_file(pan, (unsigned short)(pan->file_sel + 1U), sh);
        } else if (scan == 0x48U && pan->file_sel > 0U) {
            select_single_file(pan, (unsigned short)(pan->file_sel - 1U), sh);
        } else if (scan == 0x47U) {
            select_single_file(pan, 0, sh);
        } else if (scan == 0x4FU) {
            select_single_file(pan, (unsigned short)(pan->file_count - 1U), sh);
        } else if (scan == 0x51U) {
            unsigned n = pan->file_sel + frows;
            if (n >= pan->file_count) n = pan->file_count - 1U;
            select_single_file(pan, (unsigned short)n, sh);
        } else if (scan == 0x49U) {
            unsigned n = (pan->file_sel > frows) ? (pan->file_sel - frows) : 0U;
            select_single_file(pan, (unsigned short)n, sh);
        }

        if (pan->file_sel < pan->file_top) pan->file_top = pan->file_sel;
        else if (pan->file_sel >= pan->file_top + frows) {
            pan->file_top = (unsigned short)(pan->file_sel - frows + 1U);
        }
        return;
    }

    if (g_app.focus == FOCUS_PROGS) {
        unsigned prows = prog_visible_rows();
        unsigned cnt = dsh_prog_visible_count();
        if (ascii == 27) {
            if (g_app.reorder_active) g_app.reorder_active = 0;
            else if (g_app.active_group != 0) {
                g_app.active_group = 0;
                g_app.prog_sel = 0;
                g_app.prog_top = 0;
            }
            return;
        }
        if (cnt == 0) return;
        if (ascii == 13) {
            dsh_prog_activate_selected();
            return;
        }
        if (scan == 0x53U) {
            dsh_prog_delete_selected();
            return;
        }
        if (scan == 0x50U && (unsigned)g_app.prog_sel + 1U < cnt) ++g_app.prog_sel;
        else if (scan == 0x48U && g_app.prog_sel > 0U) --g_app.prog_sel;
        else if (scan == 0x47U) g_app.prog_sel = 0;
        else if (scan == 0x4FU) g_app.prog_sel = (unsigned char)(cnt - 1U);

        if (g_app.prog_sel < g_app.prog_top) g_app.prog_top = g_app.prog_sel;
        else if (g_app.prog_sel >= g_app.prog_top + prows) {
            g_app.prog_top = (unsigned char)(g_app.prog_sel - prows + 1U);
        }
    }
}

static int dosshell_run(void)
{
    int show_help_only = 0;

    memset(&g_app, 0, sizeof(g_app));
    g_app.req_rows = 25U;
    g_app.screen_rows = 25U;
    g_app.view_mode = VIEW_PROG_FILE;
    g_app.focus = FOCUS_DRIVE;
    g_app.running = 1;
    strcpy(g_app.filter_pat, "*.*");
    g_app.sort_mode = SORT_NAME;
    g_app.confirm_delete = 1;
    g_app.confirm_replace = 1;
    g_app.confirm_mouse = 1;

    dsh_prog_defaults();
    resolve_ini_path();
    dsh_ini_load();

    if (parse_cli_tail(&show_help_only) != 0) {
        return 1;
    }
    if (show_help_only) {
        print_usage();
        return 0;
    }

#ifdef __CKCL__
    if (_dos_allocmem(DSH_SAVE_PARAS, &g_app.save_seg) != 0) {
        g_app.save_seg = 0;
    }
    if (_dos_allocmem(DSH_DATA_PARAS, &g_app.data_seg) != 0) {
        if (g_app.save_seg != 0) _dos_freemem(g_app.save_seg);
        write_stdout("Insufficient memory to run CK-DOS Shell.\r\n");
        return 1;
    }
    bind_workspace();
#endif

    dsh_apply_theme();
    dsh_fs_enum_drives();
    dsh_ui_init(g_app.save_seg, g_app.bw_mode, g_app.req_rows);
    g_app.screen_rows = dsh_ui_rows();

    dsh_fs_scan_drive(&g_app.pan1, 0);
    if (g_app.view_mode == VIEW_DUAL) {
        dsh_fs_scan_drive(&g_app.pan2, 0);
    }

    while (g_app.running) {
        unsigned long ev;
        unsigned int hi, lo;
        dsh_draw_main_screen();
        ev = dsh_ui_event();
        hi = (unsigned int)(ev >> 16);
        lo = (unsigned int)(ev & 0xFFFFU);
        if (hi == 1U) {
            handle_mouse_click((lo >> 8) & 0xFFU, lo & 0xFFU);
        } else {
            handle_key_event((unsigned char)((hi >> 8) & 0xFFU),
                             (unsigned char)((lo >> 8) & 0xFFU),
                             (unsigned char)(lo & 0xFFU));
        }
    }

    dsh_ini_save();
    dsh_ui_done();

#ifdef __CKCL__
    if (g_app.data_seg != 0) {
        _dos_freemem(g_app.data_seg);
        g_app.data_seg = 0;
    }
    if (g_app.save_seg != 0) {
        _dos_freemem(g_app.save_seg);
        g_app.save_seg = 0;
    }
#endif
    return 0;
}

int main(void)
{
#ifdef __CKCL__
    dsh_compact_dgroup(8192U, 8192U);
#endif
    return dosshell_run();
}
