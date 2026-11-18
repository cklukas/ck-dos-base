/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "DOSSHELL.H"

#ifdef __CKCL__
#include <direct.h>
#include <dos.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

static void bind_panel_buffers(void)
{
    unsigned char __far *base;
    if (g_app.data_seg == 0) {
        g_app.pan1.dirs = 0;
        g_app.pan1.files = 0;
        g_app.pan2.dirs = 0;
        g_app.pan2.files = 0;
        return;
    }
#ifdef __CKCL__
    base = (unsigned char __far *)MK_FP(g_app.data_seg, 0);
#else
    base = 0;
#endif
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
}

static void add_item(unsigned char group_id, unsigned char is_group,
                     unsigned char target_group, unsigned char pause_after,
                     const char *title, const char *cmd, const char *sdir)
{
    unsigned i;
    for (i = 0; i < DSH_MAX_PROG_ITEMS; ++i) {
        if (!g_app.items[i].used) {
            memset(&g_app.items[i], 0, sizeof(struct dsh_prog_item));
            g_app.items[i].used = 1;
            g_app.items[i].is_group = is_group;
            g_app.items[i].group_id = group_id;
            g_app.items[i].target_group = target_group;
            g_app.items[i].pause_after = pause_after;
            strncpy(g_app.items[i].title, title, 35);
            strncpy(g_app.items[i].command, cmd, 65);
            strncpy(g_app.items[i].startup_dir, sdir, 65);
            return;
        }
    }
}

void dsh_prog_defaults(void)
{
    memset(g_app.group_titles, 0, sizeof(g_app.group_titles));
    memset(g_app.items, 0, sizeof(g_app.items));
    memset(g_app.assocs, 0, sizeof(g_app.assocs));

    strcpy(g_app.group_titles[0], "Main");
    strcpy(g_app.group_titles[1], "Disk Utilities");

    /* Group 0: Main */
    add_item(0, 0, 0, 0, "Command Prompt", "COMMAND.COM", "");
    add_item(0, 0, 0, 0, "Editor", "EDIT.COM", "");
    add_item(0, 0, 0, 0, "CK-DOS QBasic", "QBASIC.EXE", "");
    add_item(0, 1, 1, 0, "Disk Utilities", "", "");

    /* Group 1: Disk Utilities (first entry returns to Main) */
    add_item(1, 1, 0, 0, "Main", "", "");
    add_item(1, 0, 0, 1, "Disk Copy", "DISKCOPY A: B:", "");
    add_item(1, 0, 0, 1, "Backup Fixed Disk", "BACKUP C:\\*.* A: /S", "");
    add_item(1, 0, 0, 1, "Restore Fixed Disk", "RESTORE A: C:\\*.* /S", "");
    add_item(1, 0, 0, 1, "Format", "FORMAT A:", "");

    /* Default file associations */
    strcpy(g_app.assocs[0].ext, "TXT");
    strcpy(g_app.assocs[0].command, "EDIT.COM");
    strcpy(g_app.assocs[1].ext, "DOC");
    strcpy(g_app.assocs[1].command, "EDIT.COM");
    strcpy(g_app.assocs[2].ext, "BAS");
    strcpy(g_app.assocs[2].command, "QBASIC.EXE /RUN");
    g_app.assoc_count = 3;
}

unsigned dsh_prog_visible_count(void)
{
    unsigned i, c = 0;
    for (i = 0; i < DSH_MAX_PROG_ITEMS; ++i) {
        if (g_app.items[i].used && g_app.items[i].group_id == g_app.active_group) {
            ++c;
        }
    }
    return c;
}

int dsh_prog_visible_slot(unsigned vis_idx)
{
    unsigned i, c = 0;
    for (i = 0; i < DSH_MAX_PROG_ITEMS; ++i) {
        if (g_app.items[i].used && g_app.items[i].group_id == g_app.active_group) {
            if (c == vis_idx) return (int)i;
            ++c;
        }
    }
    return -1;
}

void dsh_prog_activate_selected(void)
{
    int slot;
    char cwd[136];

    if (g_app.reorder_active) {
        int src_slot = dsh_prog_visible_slot(g_app.reorder_src);
        int dst_slot = dsh_prog_visible_slot(g_app.prog_sel);
        g_app.reorder_active = 0;
        if (src_slot >= 0 && dst_slot >= 0 && src_slot != dst_slot) {
            struct dsh_prog_item tmp = g_app.items[src_slot];
            g_app.items[src_slot] = g_app.items[dst_slot];
            g_app.items[dst_slot] = tmp;
            g_app.ini_dirty = 1;
        }
        return;
    }

    slot = dsh_prog_visible_slot(g_app.prog_sel);
    if (slot < 0) return;

    if (g_app.items[slot].is_group) {
        g_app.active_group = g_app.items[slot].target_group;
        g_app.prog_sel = 0;
        g_app.prog_top = 0;
        return;
    }

    if (strcmp(g_app.items[slot].command, "COMMAND.COM") == 0) {
        dsh_fs_build_dir_path(&g_app.pan1,
            (g_app.pan1.vis_count > 0) ? g_app.pan1.vis_dirs[g_app.pan1.tree_sel] : 0, cwd);
        dsh_exec_command("", cwd, 0, 1);
        return;
    }

    if (g_app.items[slot].startup_dir[0] != '\0') {
        strncpy(cwd, g_app.items[slot].startup_dir, sizeof(cwd) - 1U);
        cwd[sizeof(cwd) - 1U] = '\0';
    } else {
        dsh_fs_build_dir_path(&g_app.pan1,
            (g_app.pan1.vis_count > 0) ? g_app.pan1.vis_dirs[g_app.pan1.tree_sel] : 0, cwd);
    }
    dsh_exec_command(g_app.items[slot].command, cwd, g_app.items[slot].pause_after, 0);
}

void dsh_prog_new_dialog(void)
{
    int is_grp = 0; /* default: (•) Program Item matching D5.7 dlg-prog-new */
    int foc = 0;    /* 0=radio, 1=OK, 2=Cancel */

    for (;;) {
        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        dsh_draw_box(5, 9, 13, 53, g_theme.dlg_box, "New Program Object", 1);
        dsh_draw_box(7, 12, 11, 32, g_theme.dlg_box, "New", 0);

        dsh_ui_write(8, 14, (foc == 0 && is_grp) ? g_theme.dlg_btn_sel : g_theme.dlg_box, "( ) Program Group");
        dsh_ui_write(10, 14, (foc == 0 && !is_grp) ? g_theme.dlg_btn_sel : g_theme.dlg_box, "( ) Program Item");
        dsh_ui_put_cell(is_grp ? 8U : 10U, 15, CH_BULLET,
                        (foc == 0) ? g_theme.dlg_btn_sel : g_theme.dlg_box);

        dsh_ui_write(8, 40, (foc == 1) ? g_theme.dlg_btn_sel : g_theme.dlg_box, "  OK  ");
        dsh_ui_write(10, 39, (foc == 2) ? g_theme.dlg_btn_sel : g_theme.dlg_box, " Cancel ");
        if (foc == 0) dsh_ui_cursor(is_grp ? 8U : 10U, 15);
        else dsh_ui_cursor((foc == 1) ? 8U : 10U, 41);
        dsh_ui_end_frame();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char scan = (unsigned char)((lo >> 8) & 0xFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);

            if (hi == 1U) continue;
            if (ascii == 27) return;
            if (ascii == 9) { foc = (foc + 1) % 3; continue; }
            if (scan == 0x0FU) { foc = (foc + 2) % 3; continue; }
            if (foc == 0 && (scan == 0x48U || scan == 0x50U)) { is_grp = !is_grp; continue; }
            if (ascii == 13) {
                if (foc == 2) return;
                break;
            }
        }
    }

    if (is_grp) {
        char gtitle[28];
        unsigned g;
        gtitle[0] = '\0';
        if (!dsh_dlg_input_one("Add Group", "Enter title for new program group:", "Title:", gtitle, sizeof(gtitle))) return;
        if (gtitle[0] == '\0') return;
        for (g = 2; g < DSH_MAX_GROUPS; ++g) {
            if (g_app.group_titles[g][0] == '\0') {
                strncpy(g_app.group_titles[g], gtitle, 27);
                add_item(g_app.active_group, 1, (unsigned char)g, 0, gtitle, "", "");
                add_item((unsigned char)g, 1, g_app.active_group, 0, g_app.group_titles[g_app.active_group], "", "");
                g_app.ini_dirty = 1;
                return;
            }
        }
    } else {
        char ititle[36], icmd[66];
        ititle[0] = '\0';
        icmd[0] = '\0';
        if (!dsh_dlg_input_one("Add Program", "Step 1 of 2 - Program Title:", "Program Title:", ititle, sizeof(ititle))) return;
        if (ititle[0] == '\0') return;
        if (!dsh_dlg_input_one("Add Program", "Step 2 of 2 - Commands:", "Commands:", icmd, sizeof(icmd))) return;
        if (icmd[0] == '\0') return;
        add_item(g_app.active_group, 0, 0, 1, ititle, icmd, "");
        g_app.ini_dirty = 1;
    }
}

void dsh_prog_properties_dialog(void)
{
    int slot = dsh_prog_visible_slot(g_app.prog_sel);
    char buf_t[36], buf_c[66];
    if (slot < 0) return;

    strncpy(buf_t, g_app.items[slot].title, 35);
    buf_t[35] = '\0';
    if (!dsh_dlg_input_one("Program Item Properties", "Edit program title:", "Program Title:", buf_t, sizeof(buf_t))) return;
    if (buf_t[0] != '\0') {
        strncpy(g_app.items[slot].title, buf_t, 35);
        if (g_app.items[slot].is_group) {
            unsigned char tg = g_app.items[slot].target_group;
            if (tg < DSH_MAX_GROUPS) strncpy(g_app.group_titles[tg], buf_t, 27);
        }
    }
    if (!g_app.items[slot].is_group) {
        strncpy(buf_c, g_app.items[slot].command, 65);
        buf_c[65] = '\0';
        if (dsh_dlg_input_one("Program Item Properties", "Edit command line:", "Commands:", buf_c, sizeof(buf_c))) {
            if (buf_c[0] != '\0') strncpy(g_app.items[slot].command, buf_c, 65);
        }
    }
    g_app.ini_dirty = 1;
}

void dsh_prog_delete_selected(void)
{
    int slot = dsh_prog_visible_slot(g_app.prog_sel);
    char prompt[64];
    if (slot < 0) return;
    if (g_app.items[slot].is_group && g_app.items[slot].target_group == 0) return;
    sprintf(prompt, "Delete item %.24s?", g_app.items[slot].title);
    if (dsh_dlg_confirm_yes_no_cancel("Delete Item", "", prompt) == 1) {
        unsigned cnt;
        g_app.items[slot].used = 0;
        cnt = dsh_prog_visible_count();
        if (g_app.prog_sel >= cnt && cnt > 0) g_app.prog_sel = (unsigned char)(cnt - 1U);
        g_app.ini_dirty = 1;
    }
}

void dsh_prog_copy_selected(void)
{
    int slot = dsh_prog_visible_slot(g_app.prog_sel);
    if (slot < 0 || g_app.items[slot].is_group) return;
    add_item(g_app.active_group, 0, 0, g_app.items[slot].pause_after,
             g_app.items[slot].title, g_app.items[slot].command, g_app.items[slot].startup_dir);
    g_app.ini_dirty = 1;
}

/* -------------------------------------------------------------------------
 * Child Process Execution & Shift+F9 Shell Spawn
 * ------------------------------------------------------------------------- */

static void resolve_comspec(char *out, char *dir_arg_out)
{
    dir_arg_out[0] = '\0';
#ifdef __CKCL__
    {
        const char *env = getenv("COMSPEC");
        unsigned int attr = 0;
        if (env && env[0] != '\0' && dsh_fs_get_attr(env, &attr) == 0) {
            strncpy(out, env, 120);
            out[120] = '\0';
            return;
        }
        if (dsh_fs_get_attr("C:\\DOS\\COMMAND.COM", &attr) == 0) {
            strcpy(out, "C:\\DOS\\COMMAND.COM");
            strcpy(dir_arg_out, "C:\\DOS");
            return;
        }
        if (dsh_fs_get_attr("C:\\COMMAND.COM", &attr) == 0) {
            strcpy(out, "C:\\COMMAND.COM");
            strcpy(dir_arg_out, "C:\\");
            return;
        }
    }
#endif
    strcpy(out, "COMMAND.COM");
}

int dsh_exec_command(const char *cmd_line, const char *work_dir, int pause_after, int is_shell_prompt)
{
#ifdef __CKCL__
    char saved_dir1[136], saved_dir2[136], comspec[124], comspec_dir[16];
    const char *argv[5];
    int rc = 0;

    dsh_fs_build_dir_path(&g_app.pan1,
        (g_app.pan1.vis_count > 0) ? g_app.pan1.vis_dirs[g_app.pan1.tree_sel] : 0, saved_dir1);
    dsh_fs_build_dir_path(&g_app.pan2,
        (g_app.pan2.vis_count > 0) ? g_app.pan2.vis_dirs[g_app.pan2.tree_sel] : 0, saved_dir2);

    /* Shut down UI and release all far memory so child has maximum conventional RAM */
    dsh_ui_done();
    if (g_app.data_seg != 0) {
        _dos_freemem(g_app.data_seg);
        g_app.data_seg = 0;
    }
    if (g_app.save_seg != 0) {
        _dos_freemem(g_app.save_seg);
        g_app.save_seg = 0;
    }
    bind_panel_buffers();

    if (work_dir && work_dir[0] != '\0') {
        if (work_dir[1] == ':') {
            unsigned int td = 0;
            char dl = work_dir[0];
            if (dl >= 'a' && dl <= 'z') dl = (char)(dl - 32);
            if (dl >= 'A' && dl <= 'Z') _dos_setdrive((unsigned int)(dl - 'A' + 1), &td);
        }
        chdir(work_dir);
    }

    /* Clear screen to clean 80x25 mode 3 for child */
    {
        union REGS regs;
        regs.x.ax = 0x0003U;
        int86(0x10, &regs, &regs);
    }

    resolve_comspec(comspec, comspec_dir);
    if (is_shell_prompt) {
        argv[0] = comspec;
        if (comspec_dir[0] != '\0') {
            argv[1] = comspec_dir;
            argv[2] = 0;
        } else {
            argv[1] = 0;
        }
        rc = spawnv(P_WAIT, comspec, argv);
        if (rc == -1) rc = spawnvp(P_WAIT, "COMMAND.COM", argv);
    } else if (cmd_line && cmd_line[0] != '\0') {
        /* If simple unredirected .COM/.EXE without spaces, try direct spawnv first */
        int has_meta = 0;
        const char *p = cmd_line;
        while (*p) {
            if (*p == ' ' || *p == '\t' || *p == '>' || *p == '<' || *p == '|') {
                has_meta = 1;
                break;
            }
            ++p;
        }
        if (!has_meta && (strstr(cmd_line, ".COM") || strstr(cmd_line, ".com") ||
                          strstr(cmd_line, ".EXE") || strstr(cmd_line, ".exe"))) {
            argv[0] = cmd_line;
            argv[1] = 0;
            rc = spawnv(P_WAIT, cmd_line, argv);
            if (rc == -1) rc = spawnvp(P_WAIT, cmd_line, argv);
        } else {
            rc = -1;
        }
        if (rc == -1) {
            int ai = 0;
            argv[ai++] = comspec;
            if (comspec_dir[0] != '\0') argv[ai++] = comspec_dir;
            argv[ai++] = "/C";
            argv[ai++] = cmd_line;
            argv[ai] = 0;
            rc = spawnv(P_WAIT, comspec, argv);
            if (rc == -1) rc = spawnvp(P_WAIT, "COMMAND.COM", argv);
        }
    }

    if (pause_after) {
        static const char msg[] = "\r\nPress any key to return to CK-DOS Shell....";
        unsigned int nw = 0;
        union REGS regs;
        _dos_write(1, msg, sizeof(msg) - 1U, &nw);
        regs.h.ah = 0x10U;
        int86(0x16, &regs, &regs);
    }

    /* Reallocate far buffers and restore UI */
    if (_dos_allocmem(DSH_SAVE_PARAS, &g_app.save_seg) != 0) g_app.save_seg = 0;
    if (_dos_allocmem(DSH_DATA_PARAS, &g_app.data_seg) != 0) g_app.data_seg = 0;
    bind_panel_buffers();

    dsh_ui_init(g_app.save_seg, g_app.bw_mode, g_app.req_rows);
    g_app.screen_rows = dsh_ui_rows();

    dsh_fs_scan_drive(&g_app.pan1, saved_dir1);
    if (g_app.view_mode == VIEW_DUAL) {
        dsh_fs_scan_drive(&g_app.pan2, saved_dir2);
    }
    return rc;
#else
    (void)cmd_line;
    (void)work_dir;
    (void)pause_after;
    (void)is_shell_prompt;
    return 0;
#endif
}

void dsh_open_selected_file(struct dsh_panel *pan)
{
    char dir_p[136], leaf[14], full_p[152], ext[4], cmd[180];
    const char *dot;
    unsigned k = 0, i;

    if (pan->file_count == 0 || pan->file_sel >= pan->file_count) return;
    while (k < 12U && pan->files[pan->file_sel].name[k] != '\0') {
        leaf[k] = pan->files[pan->file_sel].name[k];
        ++k;
    }
    leaf[k] = '\0';
    dsh_fs_build_dir_path(pan, pan->files[pan->file_sel].dir_idx, dir_p);
    if (strlen(dir_p) > 3U) sprintf(full_p, "%s\\%s", dir_p, leaf);
    else sprintf(full_p, "%s%s", dir_p, leaf);

    dot = strchr(leaf, '.');
    ext[0] = '\0';
    if (dot && dot[1] != '\0') {
        strncpy(ext, dot + 1, 3);
        ext[3] = '\0';
    }

    if (strcmp(ext, "COM") == 0 || strcmp(ext, "EXE") == 0 || strcmp(ext, "BAT") == 0) {
        dsh_exec_command(full_p, dir_p, 1, 0);
        return;
    }

    for (i = 0; i < g_app.assoc_count; ++i) {
        if (strcmp(g_app.assocs[i].ext, ext) == 0 && g_app.assocs[i].command[0] != '\0') {
            sprintf(cmd, "%s %s", g_app.assocs[i].command, full_p);
            dsh_exec_command(cmd, dir_p, 0, 0);
            return;
        }
    }

    dsh_dlg_message("Open File", "No program is associated with this file:", leaf);
}

/* -------------------------------------------------------------------------
 * Clean-Room DOSSHELL.INI Persistence
 * ------------------------------------------------------------------------- */

void dsh_ini_load(void)
{
    FILE *fp;
    char line[160];
    int in_ck_section = 0;
    int reset_items = 0;

    if (g_app.ini_path[0] == '\0') return;
    fp = fopen(g_app.ini_path, "r");
    if (!fp) return;

    while (fgets(line, sizeof(line), fp) != 0) {
        unsigned l = (unsigned)strlen(line);
        while (l > 0U && (line[l - 1U] == '\r' || line[l - 1U] == '\n' || line[l - 1U] == ' ')) {
            line[--l] = '\0';
        }
        if (line[0] == ';' || line[0] == '\0') continue;
        if (strcmp(line, "[CKDOSSHELL]") == 0) {
            in_ck_section = 1;
            continue;
        }
        if (line[0] == '[') {
            in_ck_section = 0;
            continue;
        }
        if (!in_ck_section) continue;

        if (strncmp(line, "view=", 5) == 0) {
            int v = atoi(line + 5);
            if (v >= 0 && v <= 4) g_app.view_mode = (unsigned char)v;
        } else if (strncmp(line, "color=", 6) == 0) {
            int c = atoi(line + 6);
            if (c >= 0 && c <= 4) g_app.color_scheme = (unsigned char)c;
        } else if (strncmp(line, "sort=", 5) == 0) {
            int s = atoi(line + 5);
            if (s >= 0 && s <= 4) g_app.sort_mode = (unsigned char)s;
        } else if (strncmp(line, "desc=", 5) == 0) {
            g_app.sort_desc = (unsigned char)(atoi(line + 5) ? 1 : 0);
        } else if (strncmp(line, "hidden=", 7) == 0) {
            g_app.show_hidden = (unsigned char)(atoi(line + 7) ? 1 : 0);
        } else if (strncmp(line, "across=", 7) == 0) {
            g_app.select_across = (unsigned char)(atoi(line + 7) ? 1 : 0);
        } else if (strncmp(line, "confirm=", 8) == 0) {
            int m = atoi(line + 8);
            g_app.confirm_delete = (unsigned char)((m & 1) ? 1 : 0);
            g_app.confirm_replace = (unsigned char)((m & 2) ? 1 : 0);
            g_app.confirm_mouse = (unsigned char)((m & 4) ? 1 : 0);
        } else if (strncmp(line, "filter=", 7) == 0 && line[7] != '\0') {
            strncpy(g_app.filter_pat, line + 7, 13);
            g_app.filter_pat[13] = '\0';
        } else if (strncmp(line, "group=", 6) == 0) {
            int gid = atoi(line + 6);
            char *sep = strchr(line + 6, '|');
            if (gid >= 0 && gid < DSH_MAX_GROUPS && sep && sep[1] != '\0') {
                strncpy(g_app.group_titles[gid], sep + 1, 27);
                g_app.group_titles[gid][27] = '\0';
            }
        } else if (strncmp(line, "item=", 5) == 0) {
            /* item=gid|is_grp|tgt|pause|title|cmd */
            char *p1 = line + 5;
            char *p2 = strchr(p1, '|');
            if (p2) {
                char *p3, *p4, *p5, *p6;
                *p2++ = '\0';
                p3 = strchr(p2, '|');
                if (p3) {
                    *p3++ = '\0';
                    p4 = strchr(p3, '|');
                    if (p4) {
                        *p4++ = '\0';
                        p5 = strchr(p4, '|');
                        if (p5) {
                            *p5++ = '\0';
                            p6 = strchr(p5, '|');
                            if (p6) {
                                *p6++ = '\0';
                                if (!reset_items) {
                                    memset(g_app.items, 0, sizeof(g_app.items));
                                    reset_items = 1;
                                }
                                add_item((unsigned char)atoi(p1),
                                         (unsigned char)atoi(p2),
                                         (unsigned char)atoi(p3),
                                         (unsigned char)atoi(p4),
                                         p5, p6, "");
                            }
                        }
                    }
                }
            }
        }
    }
    fclose(fp);
}

void dsh_ini_save(void)
{
    FILE *fp;
    unsigned i;
    int cmask;

    if (!g_app.ini_dirty || g_app.ini_path[0] == '\0') return;
    fp = fopen(g_app.ini_path, "w");
    if (!fp) return;

    cmask = (g_app.confirm_delete ? 1 : 0) |
            (g_app.confirm_replace ? 2 : 0) |
            (g_app.confirm_mouse ? 4 : 0);

    fprintf(fp, "; CK-DOS Shell Configuration\n[CKDOSSHELL]\n");
    fprintf(fp, "view=%u\n", (unsigned)g_app.view_mode);
    fprintf(fp, "color=%u\n", (unsigned)g_app.color_scheme);
    fprintf(fp, "sort=%u\n", (unsigned)g_app.sort_mode);
    fprintf(fp, "desc=%u\n", (unsigned)g_app.sort_desc);
    fprintf(fp, "hidden=%u\n", (unsigned)g_app.show_hidden);
    fprintf(fp, "across=%u\n", (unsigned)g_app.select_across);
    fprintf(fp, "confirm=%d\n", cmask);
    fprintf(fp, "filter=%s\n", g_app.filter_pat);

    for (i = 0; i < DSH_MAX_GROUPS; ++i) {
        if (g_app.group_titles[i][0] != '\0') {
            fprintf(fp, "group=%u|%s\n", i, g_app.group_titles[i]);
        }
    }
    for (i = 0; i < DSH_MAX_PROG_ITEMS; ++i) {
        if (g_app.items[i].used) {
            fprintf(fp, "item=%u|%u|%u|%u|%s|%s\n",
                    (unsigned)g_app.items[i].group_id,
                    (unsigned)g_app.items[i].is_group,
                    (unsigned)g_app.items[i].target_group,
                    (unsigned)g_app.items[i].pause_after,
                    g_app.items[i].title,
                    g_app.items[i].command);
        }
    }
    fclose(fp);
    g_app.ini_dirty = 0;
}

/* -------------------------------------------------------------------------
 * Independently Authored Built-In Help System (F1 & Help Menu)
 * ------------------------------------------------------------------------- */

struct help_page {
    const char *title;
    const char *lines[13];
};

static const struct help_page s_help_pages[7] = {
    {
        "CK-DOS Shell Context Help",
        {
            "CK-DOS Shell combines a visual Directory Tree, File List, and",
            "Program Manager in standard 80x25 or 80x50 text mode.",
            "",
            "  Tab / Shift+Tab   Move focus between Drive Bar, Directory",
            "                    Tree, File List, and Program List.",
            "  Alt or F10        Activate the menu bar.",
            "  Enter             Open selected directory, program, or file.",
            "  F9                View selected file in ASCII or Hex mode.",
            "  Shift+F9          Start an interactive DOS Command Prompt.",
            "  F3 or Alt+F4      Exit CK-DOS Shell and return to DOS.",
            "",
            "Press I for Help Index, K for Keyboard keys, or Esc to close.",
            0
        }
    },
    {
        "CK-DOS Shell Help Index",
        {
            "Available CK-DOS Shell Help Topics:",
            "",
            "  [K] Keyboard Navigation & Shortcut Keys",
            "  [S] Shell Basics (Drives, Trees, Views, Selection)",
            "  [C] Menu Commands Reference (File, Options, View, Tree)",
            "  [P] Common File & Program Procedures",
            "  [U] Using CK-DOS Shell Help",
            "",
            "Press the bracketed letter (K, S, C, P, U) to open a topic,",
            "or press Esc to return to CK-DOS Shell.",
            "",
            "Copyright (c) 2026 C. Klukas. All rights reserved.",
            0
        }
    },
    {
        "Keyboard Reference",
        {
            "Global & File Manager Keys:",
            "  F1            Display Help        F3 / Alt+F4   Exit Shell",
            "  F5            Refresh disk        Shift+F5      Repaint screen",
            "  F7            Move file(s)        F8            Copy file(s)",
            "  F9            View file (ASCII/Hex)  Shift+F9   Command Prompt",
            "  F10 / Alt     Menu bar            Del           Delete item",
            "  + / -         Expand / collapse directory",
            "  * / Ctrl+*    Expand branch / expand entire drive tree",
            "  Space         Toggle file selection in File List",
            "  Ctrl+A..Z     Switch active drive",
            "",
            "Press I for Index or Esc to close Help.",
            0
        }
    },
    {
        "Shell Basics",
        {
            "Screen Areas:",
            "  Drive Bar       Selects the active disk drive (A:, B:, C:, ...).",
            "  Directory Tree  Displays folders; [+] indicates subfolders.",
            "  File List       Shows files matching the active filter (*.*).",
            "  Program List    Organizes launchable commands and sub-groups.",
            "",
            "Use the View menu to switch between Single File List, Dual",
            "File Lists, All Files (with disk summary), Program/File Lists,",
            "and full-screen Program List.",
            "",
            "Press I for Index or Esc to close Help.",
            "",
            0
        }
    },
    {
        "Menu Commands",
        {
            "File Menu (File Manager): Open, Run, Associate, Search, View",
            "  File Contents (F9), Move (F7), Copy (F8), Delete (Del),",
            "  Rename, Change Attributes, Create Directory (when Tree has",
            "  focus), Select All, Deselect All, and Exit.",
            "",
            "File Menu (Program List): New (Group or Item), Open, Copy,",
            "  Delete, Properties, Reorder, Run, and Exit.",
            "",
            "Options Menu: Confirmation, File Display Options (wildcard &",
            "  sort order), Select Across Directories, Show Information,",
            "  Display (25/50 lines), and Colors.",
            "Press I for Index or Esc to close Help.",
            0
        }
    },
    {
        "Procedures",
        {
            "Copying or Moving Files:",
            "  1. Highlight a file in the File List (or toggle multiple",
            "     files with Space).",
            "  2. Press F8 (Copy) or F7 (Move), enter target path, press Enter.",
            "",
            "Adding a Program Item:",
            "  1. Press Tab until the Program List (Main) header is active.",
            "  2. Choose File -> New -> Program Item, then enter its title",
            "     and command line.",
            "",
            "Press I for Index or Esc to close Help.",
            "",
            0
        }
    },
    {
        "Using Help",
        {
            "Press F1 at any time in CK-DOS Shell or inside a dialog box to",
            "open built-in Help without external .HLP dependencies.",
            "",
            "Inside any Help window:",
            "  I       Jump to the Help Index",
            "  K       Open Keyboard Reference",
            "  S       Open Shell Basics",
            "  C       Open Menu Commands",
            "  P       Open Procedures",
            "  Esc     Close Help and restore your exact workspace",
            "",
            "",
            0
        }
    }
};

static void park_hidden_help_cursor(void)
{
#ifdef __CKCL__
    union REGS regs;
    regs.h.ah = 0x02U;
    regs.h.bh = 0;
    regs.h.dh = 6;
    regs.h.dl = 3;
    int86(0x10, &regs, &regs);
    regs.h.ah = 0x01U;
    regs.x.cx = 0x2000U;
    int86(0x10, &regs, &regs);
#endif
}

void dsh_show_help_topic(int topic_id)
{
    int cur = (topic_id >= 0 && topic_id < 7) ? topic_id : 0;

    for (;;) {
        const struct help_page *pg = &s_help_pages[cur];
        unsigned i;

        dsh_draw_main_screen();
        dsh_ui_begin_frame();
        /* 68x20 modal (1,360 cells inside rows 0..23) */
        dsh_draw_box(3, 6, 22, 73, g_theme.dlg_box, pg->title, 1);
        for (i = 0; i < 13U && pg->lines[i] != 0; ++i) {
            dsh_ui_write(5U + i, 9, g_theme.dlg_box, pg->lines[i]);
        }
        dsh_ui_write(20, 18, g_theme.dlg_btn_sel, " Close ");
        dsh_ui_write(20, 34, g_theme.dlg_box, " Index ");
        dsh_ui_write(20, 50, g_theme.dlg_box, " Keys ");
        dsh_ui_end_frame();
        park_hidden_help_cursor();

        {
            unsigned long ev = dsh_ui_event();
            unsigned int hi = (unsigned int)(ev >> 16);
            unsigned int lo = (unsigned int)(ev & 0xFFFFU);
            unsigned char ascii = (unsigned char)(lo & 0xFFU);
            char up = (ascii >= 'a' && ascii <= 'z') ? (char)(ascii - 32) : (char)ascii;

            if (hi == 1U) return;
            if (ascii == 27 || ascii == 13 || ascii == ' ') return;
            if (up == 'I') cur = HELP_INDEX;
            else if (up == 'K') cur = HELP_KEYBOARD;
            else if (up == 'S') cur = HELP_BASICS;
            else if (up == 'C') cur = HELP_COMMANDS;
            else if (up == 'P') cur = HELP_PROCEDURES;
            else if (up == 'U') cur = HELP_USING_HELP;
        }
    }
}
