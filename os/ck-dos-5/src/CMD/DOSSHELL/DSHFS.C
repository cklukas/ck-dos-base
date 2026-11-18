/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "DOSSHELL.H"

#ifdef __CKCL__
#include <direct.h>
#include <dos.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#else
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#endif

static char up_ch(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}

static int str_icmp_n(const char *a, const char *b)
{
    unsigned char ca, cb;
    for (;;) {
        ca = (unsigned char)up_ch(*a++);
        cb = (unsigned char)up_ch(*b++);
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
}

static int fstr_cmp_far(const char __far *a, const char __far *b)
{
    unsigned char ca, cb;
    for (;;) {
        ca = (unsigned char)up_ch(*a++);
        cb = (unsigned char)up_ch(*b++);
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
}

static void copy_far_to_near(char *dst, const char __far *src, unsigned max_len)
{
    unsigned i = 0;
    while (i + 1U < max_len && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void copy_near_to_far(char __far *dst, const char *src, unsigned max_len)
{
    unsigned i = 0;
    while (i + 1U < max_len && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

void dsh_fs_enum_drives(void)
{
#ifdef __CKCL__
    unsigned char fcb[40];
    char probe[6];
    union REGS regs;
    struct SREGS sregs;
    unsigned int cur_drv = 3;
    unsigned int d;

    g_app.drive_count = 0;
    _dos_getdrive(&cur_drv);
    if (cur_drv < 1U || cur_drv > 26U) cur_drv = 3U;

    for (d = 0; d < 26U; ++d) {
        memset(fcb, 0, sizeof(fcb));
        probe[0] = (char)('A' + d);
        probe[1] = ':';
        probe[2] = 'X';
        probe[3] = '\0';
        segread(&sregs);
        sregs.es = sregs.ds;
        regs.x.ax = 0x2900U;
        regs.x.si = (unsigned int)probe;
        regs.x.di = (unsigned int)fcb;
        intdosx(&regs, &regs, &sregs);
        if (regs.h.al != 0xFFU && g_app.drive_count < DSH_MAX_DRIVES) {
            g_app.drives[g_app.drive_count++] = (unsigned char)d;
        }
    }
    if (g_app.drive_count == 0) {
        g_app.drives[0] = (unsigned char)(cur_drv - 1U);
        g_app.drive_count = 1;
    }
    g_app.pan1.drive = (unsigned char)(cur_drv - 1U);
    g_app.pan2.drive = (unsigned char)(cur_drv - 1U);
    g_app.drive_cursor = 0;
    for (d = 0; d < g_app.drive_count; ++d) {
        if (g_app.drives[d] == g_app.pan1.drive) {
            g_app.drive_cursor = (unsigned char)d;
            break;
        }
    }
    g_app.drive2_cursor = g_app.drive_cursor;
#endif
}

void dsh_fs_build_dir_path(const struct dsh_panel *pan, unsigned short dir_idx, char *out_path)
{
    unsigned short chain[16];
    unsigned count = 0;
    short cur;
    char seg_name[14];
    unsigned pos;

    out_path[0] = (char)('A' + pan->drive);
    out_path[1] = ':';
    out_path[2] = '\\';
    out_path[3] = '\0';

    if (pan->dirs == 0 || dir_idx == 0 || dir_idx >= pan->dir_count) {
        return;
    }
    cur = (short)dir_idx;
    while (cur > 0 && count < 16U) {
        chain[count++] = (unsigned short)cur;
        cur = pan->dirs[cur].parent;
    }
    pos = 3;
    while (count > 0) {
        --count;
        copy_far_to_near(seg_name, pan->dirs[chain[count]].name, sizeof(seg_name));
        if (pos > 3U && pos + 1U < 126U) {
            out_path[pos++] = '\\';
        }
        {
            unsigned k = 0;
            while (seg_name[k] != '\0' && pos + 1U < 126U) {
                out_path[pos++] = seg_name[k++];
            }
        }
        out_path[pos] = '\0';
    }
}

void dsh_fs_rebuild_vis(struct dsh_panel *pan)
{
    unsigned short stack_node[16];
    unsigned short sp = 0;
    unsigned short vcount = 0;
    unsigned short cur_dir_idx = 0;

    if (pan->vis_count > 0 && pan->tree_sel < pan->vis_count) {
        cur_dir_idx = pan->vis_dirs[pan->tree_sel];
    }
    if (pan->dir_count == 0 || pan->dirs == 0) {
        pan->vis_count = 0;
        pan->tree_sel = 0;
        pan->tree_top = 0;
        return;
    }

    stack_node[sp++] = 0;
    while (sp > 0 && vcount < DSH_MAX_DIRS_MAIN) {
        unsigned short node = stack_node[--sp];
        pan->vis_dirs[vcount++] = node;
        if (pan->dirs[node].expanded && pan->dirs[node].has_children) {
            /* Push direct children in reverse order so first child pops first */
            unsigned short j = pan->dir_count;
            while (j > node + 1U) {
                --j;
                if (pan->dirs[j].parent == (short)node && sp < 16U) {
                    stack_node[sp++] = j;
                }
            }
        }
    }
    pan->vis_count = vcount;
    pan->tree_sel = 0;
    {
        unsigned short i;
        for (i = 0; i < vcount; ++i) {
            if (pan->vis_dirs[i] == cur_dir_idx) {
                pan->tree_sel = i;
                break;
            }
        }
    }
    if (pan->tree_top > pan->tree_sel) {
        pan->tree_top = pan->tree_sel;
    }
}

static void query_disk_stats(struct dsh_panel *pan)
{
#ifdef __CKCL__
    union REGS regs;
    struct find_t ft;
    char root_spec[8];

    pan->disk_total = 0;
    pan->disk_avail = 0;
    pan->vol_label[0] = '\0';

    regs.h.ah = 0x36U;
    regs.h.dl = (unsigned char)(pan->drive + 1U);
    intdos(&regs, &regs);
    if (regs.x.ax != 0xFFFFU) {
        unsigned long bpc = (unsigned long)regs.x.ax * (unsigned long)regs.x.cx;
        pan->disk_avail = bpc * (unsigned long)regs.x.bx;
        pan->disk_total = bpc * (unsigned long)regs.x.dx;
    }

    root_spec[0] = (char)('A' + pan->drive);
    root_spec[1] = ':';
    root_spec[2] = '\\';
    root_spec[3] = '*';
    root_spec[4] = '.';
    root_spec[5] = '*';
    root_spec[6] = '\0';
    if (_dos_findfirst(root_spec, _A_VOLID, &ft) == 0) {
        unsigned i = 0, o = 0;
        while (ft.name[i] != '\0' && o < 12U) {
            if (ft.name[i] != '.') {
                pan->vol_label[o++] = ft.name[i];
            }
            ++i;
        }
        pan->vol_label[o] = '\0';
    }
#endif
}

static void count_dir_files_quick(struct dsh_panel *pan)
{
#ifdef __CKCL__
    struct find_t ft;
    char spec[136];
    unsigned short idx;
    unsigned int attr_mask = _A_RDONLY | _A_ARCH | _A_HIDDEN | _A_SYSTEM;
    unsigned short total_files = 0;

    for (idx = 0; idx < pan->dir_count; ++idx) {
        dsh_fs_build_dir_path(pan, idx, spec);
        if (strlen(spec) > 3U) strcat(spec, "\\*.*");
        else strcat(spec, "*.*");
        if (_dos_findfirst(spec, attr_mask, &ft) == 0) {
            do {
                if ((ft.attrib & (_A_SUBDIR | _A_VOLID)) == 0) {
                    ++total_files;
                }
            } while (_dos_findnext(&ft) == 0);
        }
    }
    pan->total_disk_files = total_files;
    pan->total_disk_dirs = (pan->dir_count > 0) ? (unsigned short)(pan->dir_count - 1U) : 0;
#endif
}

void dsh_fs_scan_drive(struct dsh_panel *pan, const char *prefer_dir)
{
#ifdef __CKCL__
    struct find_t ft;
    char spec[136];
    char saved_exp[8][68];
    unsigned saved_exp_cnt = 0;
    char root_name[4];
    unsigned short idx;
    unsigned int search_attr = _A_SUBDIR | _A_RDONLY | _A_ARCH;
    if (g_app.show_hidden) search_attr |= (_A_HIDDEN | _A_SYSTEM);

    if (pan->dirs == 0 || pan->max_dirs == 0) return;

    root_name[0] = (char)('A' + pan->drive);
    root_name[1] = ':';
    root_name[2] = '\\';
    root_name[3] = '\0';

    if (pan->dir_count > 1U && up_ch(pan->dirs[0].name[0]) == root_name[0]) {
        for (idx = 1U; idx < pan->dir_count && saved_exp_cnt < 8U; ++idx) {
            if (pan->dirs[idx].expanded) {
                dsh_fs_build_dir_path(pan, idx, spec);
                if (strlen(spec) < 68U) {
                    strcpy(saved_exp[saved_exp_cnt++], spec);
                }
            }
        }
    }

    _fmemset(&pan->dirs[0], 0, sizeof(struct dsh_dir_node));
    copy_near_to_far(pan->dirs[0].name, root_name, 13);
    pan->dirs[0].parent = -1;
    pan->dirs[0].depth = 0;
    pan->dirs[0].expanded = 1;
    pan->dirs[0].last_sibling = 1;
    pan->dirs[0].branch_mask = 0;
    pan->dir_count = 1;

    for (idx = 0; idx < pan->dir_count; ++idx) {
        unsigned short start_child = pan->dir_count;
        unsigned char child_depth = (unsigned char)(pan->dirs[idx].depth + 1U);
        if (child_depth > 7U) continue;

        dsh_fs_build_dir_path(pan, idx, spec);
        if (strlen(spec) > 3U) strcat(spec, "\\*.*");
        else strcat(spec, "*.*");

        if (_dos_findfirst(spec, search_attr, &ft) == 0) {
            do {
                if ((ft.attrib & _A_SUBDIR) == 0) continue;
                if (ft.name[0] == '.') continue;
                if (pan->dir_count >= pan->max_dirs) break;
                {
                    unsigned short ci = pan->dir_count++;
                    unsigned char bmask = pan->dirs[idx].branch_mask;
                    if (idx > 0 && !pan->dirs[idx].last_sibling && pan->dirs[idx].depth >= 1U) {
                        bmask |= (unsigned char)(1U << (pan->dirs[idx].depth - 1U));
                    }
                    _fmemset(&pan->dirs[ci], 0, sizeof(struct dsh_dir_node));
                    copy_near_to_far(pan->dirs[ci].name, ft.name, 13);
                    pan->dirs[ci].parent = (short)idx;
                    pan->dirs[ci].depth = child_depth;
                    pan->dirs[ci].expanded = 0;
                    pan->dirs[ci].last_sibling = 0;
                    pan->dirs[ci].branch_mask = bmask;
                }
            } while (_dos_findnext(&ft) == 0);
        }

        if (pan->dir_count > start_child) {
            unsigned short a, b;
            pan->dirs[idx].has_children = 1;
            /* Sort direct children alphabetically */
            for (a = start_child; a + 1U < pan->dir_count; ++a) {
                for (b = (unsigned short)(a + 1U); b < pan->dir_count; ++b) {
                    if (fstr_cmp_far(pan->dirs[a].name, pan->dirs[b].name) > 0) {
                        struct dsh_dir_node tmp = pan->dirs[a];
                        pan->dirs[a] = pan->dirs[b];
                        pan->dirs[b] = tmp;
                    }
                }
            }
            pan->dirs[pan->dir_count - 1U].last_sibling = 1;
        }
    }

    if (saved_exp_cnt > 0U) {
        for (idx = 1U; idx < pan->dir_count; ++idx) {
            if (pan->dirs[idx].has_children) {
                unsigned k;
                dsh_fs_build_dir_path(pan, idx, spec);
                for (k = 0; k < saved_exp_cnt; ++k) {
                    if (str_icmp_n(spec, saved_exp[k]) == 0) {
                        pan->dirs[idx].expanded = 1;
                        break;
                    }
                }
            }
        }
    }

    pan->vis_count = 0;
    pan->tree_sel = 0;
    pan->tree_top = 0;

    if (prefer_dir && prefer_dir[0] != '\0') {
        unsigned short match_idx = 0;
        for (idx = 0; idx < pan->dir_count; ++idx) {
            dsh_fs_build_dir_path(pan, idx, spec);
            if (str_icmp_n(spec, prefer_dir) == 0) {
                match_idx = idx;
                break;
            }
        }
        if (match_idx > 0) {
            short p = pan->dirs[match_idx].parent;
            while (p >= 0) {
                pan->dirs[p].expanded = 1;
                p = pan->dirs[p].parent;
            }
        }
        dsh_fs_rebuild_vis(pan);
        for (idx = 0; idx < pan->vis_count; ++idx) {
            if (pan->vis_dirs[idx] == match_idx) {
                pan->tree_sel = idx;
                break;
            }
        }
    } else {
        dsh_fs_rebuild_vis(pan);
    }

    query_disk_stats(pan);
    count_dir_files_quick(pan);
    if (g_app.view_mode == VIEW_ALL) {
        dsh_fs_scan_all_files(pan, g_app.filter_pat);
    } else {
        dsh_fs_scan_files(pan);
    }
#else
    (void)pan;
    (void)prefer_dir;
#endif
}

static int match_part(const char *s, const char *p)
{
    while (*p) {
        if (*p == '*') return 1;
        if (*p == '?') {
            if (*s != '\0') ++s;
            ++p;
        } else {
            if (up_ch(*s) != up_ch(*p)) return 0;
            ++s;
            ++p;
        }
    }
    return *s == '\0';
}

int dsh_fs_match_wildcard(const char *name, const char *pattern)
{
    char nb[10], ne[6], pb[10], pe[6];
    const char *dot;
    unsigned len;

    if (!pattern || pattern[0] == '\0' || strcmp(pattern, "*.*") == 0 || strcmp(pattern, "*") == 0) {
        return 1;
    }
    dot = strchr(name, '.');
    if (dot) {
        len = (unsigned)(dot - name);
        if (len > 8U) len = 8U;
        memcpy(nb, name, len);
        nb[len] = '\0';
        strncpy(ne, dot + 1, 4);
        ne[4] = '\0';
    } else {
        strncpy(nb, name, 8);
        nb[8] = '\0';
        ne[0] = '\0';
    }

    dot = strchr(pattern, '.');
    if (dot) {
        len = (unsigned)(dot - pattern);
        if (len > 8U) len = 8U;
        memcpy(pb, pattern, len);
        pb[len] = '\0';
        strncpy(pe, dot + 1, 4);
        pe[4] = '\0';
    } else {
        strncpy(pb, pattern, 8);
        pb[8] = '\0';
        strcpy(pe, "*");
    }
    return match_part(nb, pb) && match_part(ne, pe);
}

static void split_ext_far(const char __far *name, char *stem_out, char *ext_out)
{
    unsigned i = 0, o = 0;
    while (name[i] != '\0' && name[i] != '.' && o < 8U) {
        stem_out[o++] = up_ch(name[i++]);
    }
    stem_out[o] = '\0';
    while (name[i] != '\0' && name[i] != '.') ++i;
    o = 0;
    if (name[i] == '.') {
        ++i;
        while (name[i] != '\0' && o < 3U) {
            ext_out[o++] = up_ch(name[i++]);
        }
    }
    ext_out[o] = '\0';
}

static int compare_file_entries(const struct dsh_file_entry __far *a,
                                const struct dsh_file_entry __far *b)
{
    char sa[9], ea[4], sb[9], eb[4];
    int cmp = 0;

    if (g_app.sort_mode == SORT_DISK) {
        if (a->disk_order < b->disk_order) cmp = -1;
        else if (a->disk_order > b->disk_order) cmp = 1;
    } else if (g_app.sort_mode == SORT_SIZE) {
        if (a->size < b->size) cmp = -1;
        else if (a->size > b->size) cmp = 1;
    } else if (g_app.sort_mode == SORT_DATE) {
        if (a->wr_date != b->wr_date) cmp = (a->wr_date < b->wr_date) ? -1 : 1;
        else if (a->wr_time != b->wr_time) cmp = (a->wr_time < b->wr_time) ? -1 : 1;
    } else if (g_app.sort_mode == SORT_EXT) {
        split_ext_far(a->name, sa, ea);
        split_ext_far(b->name, sb, eb);
        cmp = strcmp(ea, eb);
        if (cmp == 0) cmp = strcmp(sa, sb);
    }
    if (cmp == 0 && g_app.sort_mode != SORT_DISK) {
        split_ext_far(a->name, sa, ea);
        split_ext_far(b->name, sb, eb);
        cmp = strcmp(sa, sb);
        if (cmp == 0) cmp = strcmp(ea, eb);
    }
    return g_app.sort_desc ? -cmp : cmp;
}

void dsh_fs_sort_files(struct dsh_panel *pan)
{
    unsigned short gap, i, j;
    unsigned short n = pan->file_count;
    if (n < 2U || pan->files == 0) return;

    for (gap = (unsigned short)(n / 2U); gap > 0U; gap = (unsigned short)(gap / 2U)) {
        for (i = gap; i < n; ++i) {
            struct dsh_file_entry temp = pan->files[i];
            j = i;
            while (j >= gap && compare_file_entries(&pan->files[j - gap], &temp) > 0) {
                pan->files[j] = pan->files[j - gap];
                j = (unsigned short)(j - gap);
            }
            pan->files[j] = temp;
        }
    }
}

void dsh_fs_scan_files(struct dsh_panel *pan)
{
#ifdef __CKCL__
    struct find_t ft;
    char spec[136];
    unsigned short dir_idx = 0;
    unsigned int attr_mask = _A_RDONLY | _A_ARCH;
    unsigned char order = 0;

    if (g_app.show_hidden) attr_mask |= (_A_HIDDEN | _A_SYSTEM);
    if (pan->files == 0 || pan->max_files == 0) return;

    if (pan->vis_count > 0 && pan->tree_sel < pan->vis_count) {
        dir_idx = pan->vis_dirs[pan->tree_sel];
    }
    dsh_fs_build_dir_path(pan, dir_idx, spec);
    if (strlen(spec) > 3U) strcat(spec, "\\*.*");
    else strcat(spec, "*.*");

    pan->file_count = 0;
    pan->file_sel = 0;
    pan->file_top = 0;

    if (_dos_findfirst(spec, attr_mask, &ft) == 0) {
        do {
            if ((ft.attrib & (_A_SUBDIR | _A_VOLID)) != 0) continue;
            if (!dsh_fs_match_wildcard(ft.name, g_app.filter_pat)) continue;
            if (pan->file_count >= pan->max_files) break;
            {
                unsigned short fi = pan->file_count++;
                _fmemset(&pan->files[fi], 0, sizeof(struct dsh_file_entry));
                copy_near_to_far(pan->files[fi].name, ft.name, 13);
                pan->files[fi].attrib = ft.attrib;
                pan->files[fi].wr_time = ft.wr_time;
                pan->files[fi].wr_date = ft.wr_date;
                pan->files[fi].size = ft.size;
                pan->files[fi].dir_idx = dir_idx;
                pan->files[fi].selected = 0;
                pan->files[fi].disk_order = order++;
            }
        } while (_dos_findnext(&ft) == 0);
    }

    dsh_fs_sort_files(pan);
    if (pan->file_count > 0) {
        pan->files[0].selected = 1;
    }
#else
    (void)pan;
#endif
}

void dsh_fs_scan_all_files(struct dsh_panel *pan, const char *pattern)
{
#ifdef __CKCL__
    struct find_t ft;
    char spec[136];
    unsigned short dir_idx;
    unsigned int attr_mask = _A_RDONLY | _A_ARCH;
    unsigned char order = 0;

    if (g_app.show_hidden) attr_mask |= (_A_HIDDEN | _A_SYSTEM);
    if (pan->files == 0 || pan->max_files == 0) return;

    pan->file_count = 0;
    pan->file_sel = 0;
    pan->file_top = 0;

    for (dir_idx = 0; dir_idx < pan->dir_count; ++dir_idx) {
        dsh_fs_build_dir_path(pan, dir_idx, spec);
        if (strlen(spec) > 3U) strcat(spec, "\\*.*");
        else strcat(spec, "*.*");

        if (_dos_findfirst(spec, attr_mask, &ft) == 0) {
            do {
                if ((ft.attrib & (_A_SUBDIR | _A_VOLID)) != 0) continue;
                if (!dsh_fs_match_wildcard(ft.name, pattern)) continue;
                if (pan->file_count >= pan->max_files) break;
                {
                    unsigned short fi = pan->file_count++;
                    _fmemset(&pan->files[fi], 0, sizeof(struct dsh_file_entry));
                    copy_near_to_far(pan->files[fi].name, ft.name, 13);
                    pan->files[fi].attrib = ft.attrib;
                    pan->files[fi].wr_time = ft.wr_time;
                    pan->files[fi].wr_date = ft.wr_date;
                    pan->files[fi].size = ft.size;
                    pan->files[fi].dir_idx = dir_idx;
                    pan->files[fi].selected = 0;
                    pan->files[fi].disk_order = order++;
                }
            } while (_dos_findnext(&ft) == 0);
        }
    }

    dsh_fs_sort_files(pan);
    if (pan->file_count > 0) {
        pan->files[0].selected = 1;
    }
#else
    (void)pan;
    (void)pattern;
#endif
}

void dsh_fs_tree_expand_one(struct dsh_panel *pan)
{
    unsigned short node;
    if (pan->vis_count == 0) return;
    node = pan->vis_dirs[pan->tree_sel];
    if (pan->dirs[node].has_children && !pan->dirs[node].expanded) {
        pan->dirs[node].expanded = 1;
        dsh_fs_rebuild_vis(pan);
    }
}

static int is_descendant(const struct dsh_panel *pan, unsigned short cand, unsigned short anc)
{
    short p = pan->dirs[cand].parent;
    while (p >= 0) {
        if ((unsigned short)p == anc) return 1;
        p = pan->dirs[p].parent;
    }
    return 0;
}

void dsh_fs_tree_expand_branch(struct dsh_panel *pan)
{
    unsigned short node, i;
    if (pan->vis_count == 0) return;
    node = pan->vis_dirs[pan->tree_sel];
    for (i = node; i < pan->dir_count; ++i) {
        if (i == node || is_descendant(pan, i, node)) {
            if (pan->dirs[i].has_children) pan->dirs[i].expanded = 1;
        }
    }
    dsh_fs_rebuild_vis(pan);
}

void dsh_fs_tree_expand_all(struct dsh_panel *pan)
{
    unsigned short i;
    for (i = 0; i < pan->dir_count; ++i) {
        if (pan->dirs[i].has_children) pan->dirs[i].expanded = 1;
    }
    dsh_fs_rebuild_vis(pan);
}

void dsh_fs_tree_collapse(struct dsh_panel *pan)
{
    unsigned short node, i;
    if (pan->vis_count == 0) return;
    node = pan->vis_dirs[pan->tree_sel];
    if (pan->dirs[node].expanded) {
        for (i = node; i < pan->dir_count; ++i) {
            if (i == node || is_descendant(pan, i, node)) {
                pan->dirs[i].expanded = 0;
            }
        }
        dsh_fs_rebuild_vis(pan);
    }
}

int dsh_fs_get_attr(const char *path, unsigned int *attr_out)
{
#ifdef __CKCL__
    union REGS regs;
    regs.x.ax = 0x4300U;
    regs.x.dx = (unsigned int)path;
    intdos(&regs, &regs);
    if (regs.x.cflag) return -1;
    *attr_out = regs.x.cx;
    return 0;
#else
    (void)path;
    *attr_out = 0;
    return -1;
#endif
}

int dsh_fs_set_attr(const char *path, unsigned int attr)
{
#ifdef __CKCL__
    union REGS regs;
    regs.x.ax = 0x4301U;
    regs.x.cx = attr & 0x27U;
    regs.x.dx = (unsigned int)path;
    intdos(&regs, &regs);
    return regs.x.cflag ? -1 : 0;
#else
    (void)path;
    (void)attr;
    return -1;
#endif
}

int dsh_fs_copy_file(const char *src, const char *dst)
{
#ifdef __CKCL__
    static unsigned char io_buf[2048];
    int hsrc = -1, hdst = -1;
    unsigned int nread, nwritten;
    unsigned int ftime = 0, fdate = 0;
    union REGS regs;
    int ok = 0;

    if (str_icmp_n(src, dst) == 0) return -1;
    if (_dos_open(src, 0, &hsrc) != 0) return -1;

    regs.x.ax = 0x5700U;
    regs.x.bx = (unsigned int)hsrc;
    intdos(&regs, &regs);
    if (!regs.x.cflag) {
        ftime = regs.x.cx;
        fdate = regs.x.dx;
    }

    /* Clear destination read-only if replacing */
    dsh_fs_set_attr(dst, 0x20U);
    if (_dos_creat(dst, 0, &hdst) == 0) {
        ok = 1;
        while (_dos_read(hsrc, io_buf, sizeof(io_buf), &nread) == 0 && nread > 0) {
            if (_dos_write(hdst, io_buf, nread, &nwritten) != 0 || nwritten != nread) {
                ok = 0;
                break;
            }
        }
        if (ok && (ftime || fdate)) {
            regs.x.ax = 0x5701U;
            regs.x.bx = (unsigned int)hdst;
            regs.x.cx = ftime;
            regs.x.dx = fdate;
            intdos(&regs, &regs);
        }
        if (_dos_close(hdst) != 0) ok = 0;
        if (!ok) unlink(dst);
    }
    _dos_close(hsrc);
    return ok ? 0 : -1;
#else
    (void)src;
    (void)dst;
    return -1;
#endif
}

int dsh_fs_rename_path(const char *old_path, const char *new_path)
{
#ifdef __CKCL__
    union REGS regs;
    struct SREGS sregs;
    segread(&sregs);
    sregs.es = sregs.ds;
    regs.h.ah = 0x56U;
    regs.x.dx = (unsigned int)old_path;
    regs.x.di = (unsigned int)new_path;
    intdosx(&regs, &regs, &sregs);
    return regs.x.cflag ? -1 : 0;
#else
    (void)old_path;
    (void)new_path;
    return -1;
#endif
}

int dsh_fs_move_file(const char *src, const char *dst)
{
#ifdef __CKCL__
    if (str_icmp_n(src, dst) == 0) return -1;
    if (dsh_fs_rename_path(src, dst) == 0) return 0;
    if (dsh_fs_copy_file(src, dst) == 0) {
        unsigned int attr = 0;
        if (dsh_fs_get_attr(src, &attr) == 0 && (attr & _A_RDONLY)) {
            dsh_fs_set_attr(src, 0);
        }
        if (unlink(src) == 0) return 0;
    }
    return -1;
#else
    (void)src;
    (void)dst;
    return -1;
#endif
}

int dsh_fs_delete_file(const char *path, int force_readonly)
{
#ifdef __CKCL__
    if (force_readonly) {
        dsh_fs_set_attr(path, 0);
    }
    return unlink(path);
#else
    (void)path;
    (void)force_readonly;
    return -1;
#endif
}

int dsh_fs_mkdir_path(const char *path)
{
#ifdef __CKCL__
    return mkdir(path);
#else
    (void)path;
    return -1;
#endif
}

void dsh_fs_sync_dos_cwd(const struct dsh_panel *pan)
{
#ifdef __CKCL__
    unsigned int total_drvs = 0;
    char dir_path[132];
    unsigned short dir_idx = 0;

    _dos_setdrive((unsigned int)(pan->drive + 1U), &total_drvs);
    if (pan->vis_count > 0 && pan->tree_sel < pan->vis_count) {
        dir_idx = pan->vis_dirs[pan->tree_sel];
    }
    dsh_fs_build_dir_path(pan, dir_idx, dir_path);
    chdir(dir_path);
#else
    (void)pan;
#endif
}
