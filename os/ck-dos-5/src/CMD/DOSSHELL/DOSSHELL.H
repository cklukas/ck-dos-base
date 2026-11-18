/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#ifndef CK_DOSSHELL_H
#define CK_DOSSHELL_H

#ifndef __CKCL__
#ifndef __WATCOMC__
#define __far
#define __interrupt
#endif
#endif

#define DSH_SAVE_PARAS      1100U
#define DSH_DATA_PARAS      2304U   /* 36 KB far workspace */

#define DSH_MAX_DRIVES      26
#define DSH_MAX_DIRS_MAIN   256
#define DSH_MAX_FILES_MAIN  512
#define DSH_MAX_DIRS_SEC    128
#define DSH_MAX_FILES_SEC   256

#define DSH_MAX_GROUPS      8
#define DSH_MAX_PROG_ITEMS  32
#define DSH_MAX_ASSOC       12

/* View modes matching MS-DOS 5 View menu order */
#define VIEW_SINGLE         0
#define VIEW_DUAL           1
#define VIEW_ALL            2
#define VIEW_PROG_FILE      3
#define VIEW_PROG           4

/* Focus ring targets */
#define FOCUS_DRIVE         0
#define FOCUS_TREE          1
#define FOCUS_FILES         2
#define FOCUS_DRIVE2        3
#define FOCUS_TREE2         4
#define FOCUS_FILES2        5
#define FOCUS_PROGS         6

/* Sort orders */
#define SORT_NAME           0
#define SORT_EXT            1
#define SORT_DATE           2
#define SORT_SIZE           3
#define SORT_DISK           4

/* CP437 glyphs */
#define CH_ARROW_RIGHT      0x1AU   /* → focus indicator */
#define CH_TRI_RIGHT        0x10U   /* ► selected indicator */
#define CH_SCROLL_UP        0x18U   /* ↑ */
#define CH_SCROLL_DOWN      0x19U   /* ↓ */
#define CH_BULLET           0x07U   /* • radio dot */
#define CH_DOT_FILL         0xFAU   /* · input field fill */
#define CH_BOX_H            0xC4U   /* ─ */
#define CH_BOX_V            0xB3U   /* │ */
#define CH_BOX_TL           0xDAU   /* ┌ */
#define CH_BOX_TR           0xBFU   /* ┐ */
#define CH_BOX_BL           0xC0U   /* └ */
#define CH_BOX_BR           0xD9U   /* ┘ */
#define CH_BOX_LT           0xC3U   /* ├ */
#define CH_BOX_RT           0xB4U   /* ┤ */
#define CH_TREE_TEE         0xC3U   /* ├ */
#define CH_TREE_ELBOW       0xC0U   /* └ */

/* Directory tree node (28 bytes, no bitfields) */
struct dsh_dir_node {
    char name[13];              /* "C:\" for root, else 8.3 dir name */
    short parent;               /* -1 for root */
    unsigned char depth;        /* 0 for root */
    unsigned char has_children; /* 0 or 1 */
    unsigned char expanded;     /* 0 or 1 */
    unsigned char last_sibling; /* 0 or 1: draws └─ instead of ├─ */
    unsigned char branch_mask;  /* bit d=1 if ancestor at depth d has more siblings */
    unsigned char pad;
    unsigned short first_file;  /* used in All Files / Search mode */
    unsigned short file_count;
    unsigned short pad2;
};

/* File entry (26 bytes, no bitfields) */
struct dsh_file_entry {
    char name[13];              /* "ALPHA.TXT" */
    unsigned char attrib;
    unsigned short wr_time;
    unsigned short wr_date;
    unsigned long size;
    unsigned short dir_idx;     /* owning directory index in tree */
    unsigned char selected;     /* 0 or 1 */
    unsigned char disk_order;
};

/* File panel state (primary or secondary in Dual File Lists) */
struct dsh_panel {
    unsigned char drive;            /* 0='A', 2='C', ... */
    unsigned char pad;
    struct dsh_dir_node __far *dirs;
    unsigned short max_dirs;
    unsigned short dir_count;
    unsigned short vis_dirs[DSH_MAX_DIRS_MAIN];
    unsigned short vis_count;
    unsigned short tree_sel;        /* index into vis_dirs[] */
    unsigned short tree_top;        /* scroll top into vis_dirs[] */

    struct dsh_file_entry __far *files;
    unsigned short max_files;
    unsigned short file_count;
    unsigned short file_sel;        /* index into files[] */
    unsigned short file_top;        /* scroll top into files[] */

    unsigned long disk_total;
    unsigned long disk_avail;
    unsigned short total_disk_files;
    unsigned short total_disk_dirs;
    char vol_label[13];
};

/* Program Manager item / group */
struct dsh_prog_item {
    unsigned char used;
    unsigned char is_group;         /* 1 = sub-group, 0 = command item */
    unsigned char group_id;         /* parent group (0 = Main) */
    unsigned char target_group;     /* if is_group==1, group index */
    unsigned char pause_after;      /* 1 = "Press any key to return..." */
    unsigned char pad;
    char title[36];
    char command[66];
    char startup_dir[66];
};

struct dsh_assoc {
    char ext[4];                    /* e.g. "TXT", "BAS" */
    char command[48];               /* e.g. "EDIT.COM" */
};

/* Palette attributes */
struct dsh_theme {
    unsigned char title;            /* row 0 & active header */
    unsigned char menubar;          /* row 1 menu bar & drop-downs */
    unsigned char statusbar;        /* bottom status bar */
    unsigned char menu_accel;       /* highlighted letter in menu */
    unsigned char menu_dim;         /* disabled menu item */
    unsigned char surface;          /* window interiors & borders */
    unsigned char inactive_hdr;     /* unfocused pane header */
    unsigned char sel_active;       /* selected row in focused pane */
    unsigned char sel_inactive;     /* selected row in unfocused pane */
    unsigned char dlg_box;          /* modal dialog frame/fill */
    unsigned char dlg_input;        /* input field inside dialog */
    unsigned char dlg_btn_sel;      /* active button */
    unsigned char shadow;           /* drop-shadow attribute */
};

/* Global application state */
struct dsh_app {
    unsigned int save_seg;
    unsigned int data_seg;
    unsigned int screen_rows;       /* 25 or 43/50 */
    unsigned int req_rows;          /* 25, 43, or 50 */
    unsigned char bw_mode;          /* 1 if /B */
    unsigned char color_scheme;     /* 0..4 */
    unsigned char view_mode;        /* VIEW_* */
    unsigned char focus;            /* FOCUS_* */
    unsigned char running;

    /* File display & confirmation options */
    char filter_pat[14];            /* default "*.*" */
    unsigned char sort_mode;        /* SORT_* */
    unsigned char sort_desc;        /* 0 or 1 */
    unsigned char show_hidden;      /* 0 or 1 */
    unsigned char select_across;    /* 0 or 1 */
    unsigned char confirm_delete;   /* default 1 */
    unsigned char confirm_replace;  /* default 1 */
    unsigned char confirm_mouse;    /* default 1 */

    /* Drive bar */
    unsigned char drives[DSH_MAX_DRIVES];
    unsigned char drive_count;
    unsigned char drive_cursor;     /* cursor index in drives[] for primary */
    unsigned char drive2_cursor;    /* cursor index in drives[] for secondary */

    /* Panels */
    struct dsh_panel pan1;
    struct dsh_panel pan2;

    /* Program Manager */
    char group_titles[DSH_MAX_GROUPS][28];
    unsigned char active_group;     /* 0 = Main */
    unsigned char prog_sel;
    unsigned char prog_top;
    unsigned char reorder_active;
    unsigned char reorder_src;
    struct dsh_prog_item items[DSH_MAX_PROG_ITEMS];

    /* Extension associations */
    struct dsh_assoc assocs[DSH_MAX_ASSOC];
    unsigned char assoc_count;

    /* Persistence path */
    char ini_path[128];
    unsigned char ini_dirty;
};

extern struct dsh_app g_app;
extern struct dsh_theme g_theme;

/* ASM UI bridge (DSHUI.ASM) */
extern void __far dsh_ui_init(unsigned save_seg, unsigned bw_mode, unsigned rows_req);
extern void __far dsh_ui_done(void);
extern unsigned __far dsh_ui_rows(void);
extern void __far dsh_ui_begin_frame(void);
extern void __far dsh_ui_end_frame(void);
extern void __far dsh_ui_suspend_bb(void);
extern void __far dsh_ui_resume_bb(unsigned save_seg);
extern void __far dsh_ui_fill(unsigned row, unsigned col, unsigned ch, unsigned attr, unsigned count);
extern void __far dsh_ui_write(unsigned row, unsigned col, unsigned attr, const char *str);
extern void __far dsh_ui_put_cell(unsigned row, unsigned col, unsigned ch, unsigned attr);
extern void __far dsh_ui_shadow_rect(unsigned top, unsigned left, unsigned bottom, unsigned right, unsigned attr);
extern void __far dsh_ui_cursor(unsigned row_or_ffff, unsigned col);
extern unsigned long __far dsh_ui_event(void);
extern unsigned __far dsh_ui_poll_key(void);
extern void __far dsh_compact_dgroup(unsigned heap_bytes, unsigned stack_bytes);

/* DSHFS.C */
void dsh_fs_enum_drives(void);
void dsh_fs_build_dir_path(const struct dsh_panel *pan, unsigned short dir_idx, char *out_path);
void dsh_fs_rebuild_vis(struct dsh_panel *pan);
void dsh_fs_scan_drive(struct dsh_panel *pan, const char *prefer_dir);
void dsh_fs_scan_files(struct dsh_panel *pan);
void dsh_fs_scan_all_files(struct dsh_panel *pan, const char *pattern);
void dsh_fs_sort_files(struct dsh_panel *pan);
int dsh_fs_match_wildcard(const char *name, const char *pattern);
void dsh_fs_tree_expand_one(struct dsh_panel *pan);
void dsh_fs_tree_expand_branch(struct dsh_panel *pan);
void dsh_fs_tree_expand_all(struct dsh_panel *pan);
void dsh_fs_tree_collapse(struct dsh_panel *pan);
int dsh_fs_get_attr(const char *path, unsigned int *attr_out);
int dsh_fs_set_attr(const char *path, unsigned int attr);
int dsh_fs_copy_file(const char *src, const char *dst);
int dsh_fs_move_file(const char *src, const char *dst);
int dsh_fs_rename_path(const char *old_path, const char *new_path);
int dsh_fs_delete_file(const char *path, int force_readonly);
int dsh_fs_mkdir_path(const char *path);
void dsh_fs_sync_dos_cwd(const struct dsh_panel *pan);

/* DSHDRAW.C */
void dsh_apply_theme(void);
void dsh_format_size_commas(unsigned long val, char *out, int width);
void dsh_format_date(unsigned short wr_date, char *out);
void dsh_format_time(unsigned short wr_time, char *out);
void dsh_draw_box(unsigned top, unsigned left, unsigned bottom, unsigned right,
                  unsigned attr, const char *title, int shadow);
void dsh_draw_main_screen(void);
void dsh_run_menu_bar(int initial_menu);
void dsh_view_file(const char *full_path, const char *display_name);
void dsh_run_search_results(const char *pattern, int entire_disk);

/* DSHDLG.C */
void dsh_dlg_message(const char *title, const char *line1, const char *line2);
int dsh_dlg_confirm_yes_no_cancel(const char *title, const char *warn_line, const char *prompt_line);
int dsh_dlg_input_one(const char *title, const char *info_line, const char *label,
                      char *buf, unsigned max_len);
int dsh_dlg_copy_or_move(int is_move, const char *from_desc, char *to_buf, unsigned max_len);
void dsh_dlg_do_copy_or_move(struct dsh_panel *pan, int is_move);
void dsh_dlg_do_delete(struct dsh_panel *pan);
void dsh_dlg_do_rename(struct dsh_panel *pan);
void dsh_dlg_do_mkdir(struct dsh_panel *pan);
void dsh_dlg_do_change_attr(struct dsh_panel *pan);
void dsh_dlg_do_search(struct dsh_panel *pan);
void dsh_dlg_do_run(void);
void dsh_dlg_do_associate(struct dsh_panel *pan);
void dsh_dlg_file_display_options(void);
void dsh_dlg_confirmation(void);
void dsh_dlg_show_information(const struct dsh_panel *pan);
void dsh_dlg_display_mode(void);
void dsh_dlg_colors(void);
void dsh_dlg_about(void);

/* DSHPROG.C */
void dsh_prog_defaults(void);
void dsh_ini_load(void);
void dsh_ini_save(void);
unsigned dsh_prog_visible_count(void);
int dsh_prog_visible_slot(unsigned vis_idx);
void dsh_prog_activate_selected(void);
void dsh_prog_new_dialog(void);
void dsh_prog_properties_dialog(void);
void dsh_prog_delete_selected(void);
void dsh_prog_copy_selected(void);
int dsh_exec_command(const char *cmd_line, const char *work_dir, int pause_after, int is_shell_prompt);
void dsh_open_selected_file(struct dsh_panel *pan);
void dsh_show_help_topic(int topic_id);

/* Help topic IDs */
#define HELP_CONTEXT        0
#define HELP_INDEX          1
#define HELP_KEYBOARD       2
#define HELP_BASICS         3
#define HELP_COMMANDS       4
#define HELP_PROCEDURES     5
#define HELP_USING_HELP     6

#endif
