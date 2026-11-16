/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/diskimg_core.h"

#define CK_DEFAULT_DOS_DATE ((ck_u16)(((1988u - 1980u) << 9) | (6u << 5) | 17u))
#define CK_DEFAULT_DOS_TIME ((ck_u16)((12u << 11) | (0u << 5) | 0u))
#define CK_DEFAULT_SERIAL   0x19880400UL

static void ck_write_u16_be(ck_u8 *p, ck_u16 v) {
    p[0] = (ck_u8)((v >> 8) & 0xFFu);
    p[1] = (ck_u8)(v & 0xFFu);
}

static void ck_write_u32_be(ck_u8 *p, ck_u32 v) {
    p[0] = (ck_u8)((v >> 24) & 0xFFu);
    p[1] = (ck_u8)((v >> 16) & 0xFFu);
    p[2] = (ck_u8)((v >> 8) & 0xFFu);
    p[3] = (ck_u8)(v & 0xFFu);
}

static void ck_write_both16(ck_u8 *p, ck_u16 v) {
    ck_write_u16_le(p, v);
    ck_write_u16_be(p + 2, v);
}

static void ck_write_both32(ck_u8 *p, ck_u32 v) {
    ck_write_u32_le(p, v);
    ck_write_u32_be(p + 4, v);
}

static char ck_ascii_up(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static void ck_pad_label(char dst[11], const char *label) {
    int i = 0;
    if (label) {
        while (label[i] != '\0' && i < 11) {
            dst[i] = ck_ascii_up(label[i]);
            ++i;
        }
    }
    while (i < 11) {
        dst[i++] = ' ';
    }
}

void ck_diskimg_floppy_geom(ck_floppy_kind kind, const char *label, ck_fat_geom *out) {
    memset(out, 0, sizeof(*out));
    out->bytes_per_sector = 512u;
    out->reserved_sectors = 1u;
    out->num_fats = 2u;
    out->num_heads = 2u;
    out->hidden_sectors = 0UL;
    out->drive_num = 0x00u;
    out->volume_serial = CK_DEFAULT_SERIAL;
    ck_pad_label(out->volume_label, label ? label : "CKDOS 4.00");

    switch (kind) {
        case CK_FLOPPY_360K:
            out->sectors_per_cluster = 2u;
            out->root_entries = 112u;
            out->total_sectors = 720UL;
            out->media_byte = 0xFDu;
            out->sectors_per_fat = 2u;
            out->sectors_per_track = 9u;
            break;
        case CK_FLOPPY_720K:
            out->sectors_per_cluster = 2u;
            out->root_entries = 112u;
            out->total_sectors = 1440UL;
            out->media_byte = 0xF9u;
            out->sectors_per_fat = 3u;
            out->sectors_per_track = 9u;
            break;
        case CK_FLOPPY_1200K:
            out->sectors_per_cluster = 1u;
            out->root_entries = 224u;
            out->total_sectors = 2400UL;
            out->media_byte = 0xF9u;
            out->sectors_per_fat = 7u;
            out->sectors_per_track = 15u;
            break;
        case CK_FLOPPY_1440K:
        default:
            out->sectors_per_cluster = 1u;
            out->root_entries = 224u;
            out->total_sectors = 2880UL;
            out->media_byte = 0xF0u;
            out->sectors_per_fat = 9u;
            out->sectors_per_track = 18u;
            break;
    }
}

void ck_diskimg_hdd_geom(ck_u16 cylinders,
                         ck_u16 heads,
                         ck_u16 sectors_per_track,
                         const char *label,
                         ck_fat_geom *out) {
    ck_u32 disk_sectors = (ck_u32)cylinders * (ck_u32)heads * (ck_u32)sectors_per_track;
    ck_u32 part_start = (ck_u32)sectors_per_track;
    ck_u32 part_sectors = (disk_sectors > part_start) ? (disk_sectors - part_start) : 0UL;
    ck_u8 spc = 4u;
    ck_u32 root_secs = 32UL;
    ck_u32 est_clusters;
    ck_u16 spf;

    if (part_sectors < 18000UL) spc = 2u;
    if (part_sectors > 130000UL) spc = 8u;
    est_clusters = (part_sectors - 1UL - root_secs) / (ck_u32)spc + 2UL;
    spf = (ck_u16)((est_clusters * 2UL + 511UL) / 512UL);

    memset(out, 0, sizeof(*out));
    out->bytes_per_sector = 512u;
    out->sectors_per_cluster = spc;
    out->reserved_sectors = 1u;
    out->num_fats = 2u;
    out->root_entries = 512u;
    out->total_sectors = part_sectors;
    out->media_byte = 0xF8u;
    out->sectors_per_fat = spf;
    out->sectors_per_track = sectors_per_track;
    out->num_heads = heads;
    out->hidden_sectors = part_start;
    out->drive_num = 0x80u;
    out->volume_serial = CK_DEFAULT_SERIAL + 1UL;
    ck_pad_label(out->volume_label, label ? label : "CKDOS 4.00");
}

static int ck_format_83(const char *seg, ck_usize len, char out11[11]) {
    ck_usize i;
    ck_usize dot = len;
    int pos = 0;

    if (len == 0u) return -1;
    memset(out11, ' ', 11);
    for (i = 0u; i < len; ++i) {
        if (seg[i] == '.') {
            dot = i;
            break;
        }
    }
    if (dot == 0u || dot > 8u) return -1;
    if (dot < len && (len - dot - 1u) > 3u) return -1;

    for (i = 0u; i < dot && pos < 8; ++i) {
        out11[pos++] = ck_ascii_up(seg[i]);
    }
    if (dot < len) {
        pos = 8;
        for (i = dot + 1u; i < len && pos < 11; ++i) {
            out11[pos++] = ck_ascii_up(seg[i]);
        }
    }
    return 0;
}

typedef struct ck_fat_node {
    int parent;          /* -1 for root */
    int is_dir;
    char name11[11];
    ck_u8 attr;
    ck_u16 dos_time;
    ck_u16 dos_date;
    ck_u16 first_cluster;
    ck_u16 cluster_count;
    ck_u32 size;
    ck_usize file_idx;
} ck_fat_node;

static void ck_fat_set_entry(ck_u8 *fat, int is_fat16, ck_u16 cluster, ck_u16 value) {
    if (is_fat16) {
        ck_u32 off = (ck_u32)cluster * 2UL;
        ck_write_u16_le(fat + off, value);
    } else {
        ck_u32 off = (ck_u32)cluster + ((ck_u32)cluster >> 1);
        if ((cluster & 1u) == 0u) {
            fat[off] = (ck_u8)(value & 0xFFu);
            fat[off + 1u] = (ck_u8)((fat[off + 1u] & 0xF0u) | ((value >> 8) & 0x0Fu));
        } else {
            fat[off] = (ck_u8)((fat[off] & 0x0Fu) | ((value & 0x0Fu) << 4));
            fat[off + 1u] = (ck_u8)((value >> 4) & 0xFFu);
        }
    }
}

static void ck_write_dir_slot(ck_u8 slot[32],
                              const char name11[11],
                              ck_u8 attr,
                              ck_u16 dos_time,
                              ck_u16 dos_date,
                              ck_u16 first_cluster,
                              ck_u32 size) {
    memset(slot, 0, 32);
    memcpy(slot, name11, 11);
    slot[11] = attr;
    ck_write_u16_le(slot + 22, dos_time ? dos_time : CK_DEFAULT_DOS_TIME);
    ck_write_u16_le(slot + 24, dos_date ? dos_date : CK_DEFAULT_DOS_DATE);
    ck_write_u16_le(slot + 26, first_cluster);
    ck_write_u32_le(slot + 28, size);
}

static int ck_read_file_slice(const ck_img_file *f, ck_u32 off, ck_u8 *dst, ck_u32 len) {
    if (len == 0UL) return 0;
    if (f->mem_data) {
        memcpy(dst, f->mem_data + off, (unsigned int)len);
        return 0;
    }
    if (f->read_fn) {
        return f->read_fn(f->read_ctx, off, dst, len);
    }
    return -1;
}

int ck_diskimg_write_fat_volume(const ck_fat_geom *geom,
                                const ck_u8 *boot_sector_512,
                                const ck_img_file *files,
                                ck_usize file_count,
                                ck_img_write_fn write_fn,
                                void *write_ctx,
                                ck_error *err) {
    ck_u32 root_secs;
    ck_u32 first_data_sec;
    ck_u32 data_secs;
    ck_u32 total_clusters;
    int is_fat16;
    ck_u16 eoc_mark;
    ck_u32 clus_bytes;
    ck_u32 fat_bytes;
    ck_u32 root_bytes;
    ck_u8 *fat = NULL;
    ck_u8 *root_buf = NULL;
    ck_fat_node *nodes = NULL;
    ck_usize node_cap = 0u;
    ck_usize node_cnt = 0u;
    ck_usize fi, ni;
    int io_node = -1;
    int msdos_node = -1;
    ck_u16 next_cluster = 2u;
    ck_u16 root_used = 0u;
    ck_u8 sec[512];
    int rc = -1;

    if (!geom || !write_fn || geom->bytes_per_sector != 512u || geom->sectors_per_cluster == 0u) {
        ck_set_error(err, 0UL, "invalid FAT geometry");
        return -1;
    }

    root_secs = ((ck_u32)geom->root_entries * 32UL + 511UL) / 512UL;
    first_data_sec = (ck_u32)geom->reserved_sectors +
                     (ck_u32)geom->num_fats * (ck_u32)geom->sectors_per_fat + root_secs;
    if (geom->total_sectors <= first_data_sec) {
        ck_set_error(err, 0UL, "FAT volume too small for metadata");
        return -1;
    }
    data_secs = geom->total_sectors - first_data_sec;
    total_clusters = data_secs / (ck_u32)geom->sectors_per_cluster;
    is_fat16 = (total_clusters >= 4085UL) ? 1 : 0;
    eoc_mark = is_fat16 ? (ck_u16)0xFFFFu : (ck_u16)0x0FFFu;
    clus_bytes = (ck_u32)geom->sectors_per_cluster * 512UL;

    fat_bytes = (ck_u32)geom->sectors_per_fat * 512UL;
    root_bytes = root_secs * 512UL;
    node_cap = file_count * 3u + 8u;

    fat = (ck_u8 *)calloc(1u, (unsigned int)fat_bytes);
    root_buf = (ck_u8 *)calloc(1u, (unsigned int)root_bytes);
    nodes = (ck_fat_node *)calloc((unsigned int)node_cap, sizeof(ck_fat_node));
    if (!fat || !root_buf || !nodes) {
        ck_set_error(err, 0UL, "out of memory building FAT layout");
        goto cleanup;
    }

    fat[0] = geom->media_byte;
    fat[1] = 0xFFu;
    fat[2] = 0xFFu;
    if (is_fat16) fat[3] = 0xFFu;

    /* 1. Build directory and file node tree */
    for (fi = 0u; fi < file_count; ++fi) {
        const char *p = files[fi].dos_path;
        int parent = -1;
        if (!p || p[0] == '\0') {
            ck_set_error(err, (ck_u32)fi, "empty DOS path");
            goto cleanup;
        }
        while (*p == '/' || *p == '\\') ++p;
        while (*p != '\0') {
            const char *start = p;
            ck_usize seg_len;
            int is_last;
            char name11[11];

            while (*p != '\0' && *p != '/' && *p != '\\') ++p;
            seg_len = (ck_usize)(p - start);
            while (*p == '/' || *p == '\\') ++p;
            is_last = (*p == '\0') ? 1 : 0;

            if (ck_format_83(start, seg_len, name11) != 0) {
                ck_set_error(err, (ck_u32)fi, "invalid 8.3 path component");
                goto cleanup;
            }

            if (!is_last) {
                int found = -1;
                for (ni = 0u; ni < node_cnt; ++ni) {
                    if (nodes[ni].parent == parent && nodes[ni].is_dir &&
                        memcmp(nodes[ni].name11, name11, 11) == 0) {
                        found = (int)ni;
                        break;
                    }
                }
                if (found < 0) {
                    if (node_cnt >= node_cap) {
                        ck_usize ncap = node_cap * 2u;
                        ck_fat_node * grown = (ck_fat_node *)realloc(
                            nodes, (unsigned int)(ncap * sizeof(ck_fat_node)));
                        if (!grown) {
                            ck_set_error(err, 0UL, "out of memory growing FAT nodes");
                            goto cleanup;
                        }
                        nodes = grown;
                        node_cap = ncap;
                    }
                    memset(&nodes[node_cnt], 0, sizeof(ck_fat_node));
                    nodes[node_cnt].parent = parent;
                    nodes[node_cnt].is_dir = 1;
                    memcpy(nodes[node_cnt].name11, name11, 11);
                    nodes[node_cnt].attr = 0x10u;
                    nodes[node_cnt].dos_time = CK_DEFAULT_DOS_TIME;
                    nodes[node_cnt].dos_date = CK_DEFAULT_DOS_DATE;
                    found = (int)node_cnt++;
                }
                parent = found;
            } else {
                if (node_cnt >= node_cap) {
                    ck_usize ncap = node_cap * 2u;
                    ck_fat_node *grown = (ck_fat_node *)realloc(
                        nodes, (unsigned int)(ncap * sizeof(ck_fat_node)));
                    if (!grown) {
                        ck_set_error(err, 0UL, "out of memory growing FAT nodes");
                        goto cleanup;
                    }
                    nodes = grown;
                    node_cap = ncap;
                }
                memset(&nodes[node_cnt], 0, sizeof(ck_fat_node));
                nodes[node_cnt].parent = parent;
                nodes[node_cnt].is_dir = 0;
                memcpy(nodes[node_cnt].name11, name11, 11);
                nodes[node_cnt].attr = files[fi].attr ? files[fi].attr : 0x20u;
                nodes[node_cnt].dos_time = files[fi].dos_time;
                nodes[node_cnt].dos_date = files[fi].dos_date;
                nodes[node_cnt].size = files[fi].size;
                nodes[node_cnt].file_idx = fi;
                if (parent == -1 && memcmp(name11, "IO      SYS", 11) == 0) {
                    io_node = (int)node_cnt;
                } else if (parent == -1 && memcmp(name11, "MSDOS   SYS", 11) == 0) {
                    msdos_node = (int)node_cnt;
                }
                ++node_cnt;
            }
        }
    }

    /* 2. Allocate clusters: IO.SYS first (cluster 2), MSDOS.SYS second, then dirs, then files */
    {
        int pass;
        for (pass = 0; pass < 4; ++pass) {
            for (ni = 0u; ni < node_cnt; ++ni) {
                ck_u32 need_bytes = 0UL;
                ck_u16 n_clus;
                ck_u16 c;

                if (pass == 0 && (int)ni != io_node) continue;
                if (pass == 1 && (int)ni != msdos_node) continue;
                if (pass == 2 && !nodes[ni].is_dir) continue;
                if (pass == 3 && (nodes[ni].is_dir || (int)ni == io_node || (int)ni == msdos_node)) continue;

                if (nodes[ni].is_dir) {
                    ck_u32 children = 2UL; /* . and .. */
                    ck_usize k;
                    for (k = 0u; k < node_cnt; ++k) {
                        if (nodes[k].parent == (int)ni) ++children;
                    }
                    need_bytes = children * 32UL;
                } else {
                    need_bytes = nodes[ni].size;
                }

                if (need_bytes == 0UL) {
                    nodes[ni].first_cluster = 0u;
                    nodes[ni].cluster_count = 0u;
                    continue;
                }

                n_clus = (ck_u16)((need_bytes + clus_bytes - 1UL) / clus_bytes);
                if ((ck_u32)next_cluster - 2UL + (ck_u32)n_clus > total_clusters) {
                    ck_set_error(err, need_bytes, "FAT volume capacity exceeded");
                    goto cleanup;
                }
                nodes[ni].first_cluster = next_cluster;
                nodes[ni].cluster_count = n_clus;
                for (c = 0u; c < n_clus; ++c) {
                    ck_u16 cur = (ck_u16)(next_cluster + c);
                    ck_u16 nxt = (c + 1u < n_clus) ? (ck_u16)(cur + 1u) : eoc_mark;
                    ck_fat_set_entry(fat, is_fat16, cur, nxt);
                }
                next_cluster = (ck_u16)(next_cluster + n_clus);
            }
        }
    }

    /* 3. Populate root directory (IO.SYS #0, MSDOS.SYS #1, volume label, remaining root entries) */
    if (io_node >= 0) {
        if (root_used >= geom->root_entries) { ck_set_error(err, 0UL, "root directory full"); goto cleanup; }
        ck_write_dir_slot(root_buf + (ck_u32)root_used * 32UL,
                          nodes[io_node].name11, nodes[io_node].attr,
                          nodes[io_node].dos_time, nodes[io_node].dos_date,
                          nodes[io_node].first_cluster, nodes[io_node].size);
        ++root_used;
    }
    if (msdos_node >= 0) {
        if (root_used >= geom->root_entries) { ck_set_error(err, 0UL, "root directory full"); goto cleanup; }
        ck_write_dir_slot(root_buf + (ck_u32)root_used * 32UL,
                          nodes[msdos_node].name11, nodes[msdos_node].attr,
                          nodes[msdos_node].dos_time, nodes[msdos_node].dos_date,
                          nodes[msdos_node].first_cluster, nodes[msdos_node].size);
        ++root_used;
    }
    if (geom->volume_label[0] != ' ' && geom->volume_label[0] != '\0') {
        if (root_used >= geom->root_entries) { ck_set_error(err, 0UL, "root directory full"); goto cleanup; }
        ck_write_dir_slot(root_buf + (ck_u32)root_used * 32UL,
                          geom->volume_label, 0x08u,
                          CK_DEFAULT_DOS_TIME, CK_DEFAULT_DOS_DATE, 0u, 0UL);
        ++root_used;
    }
    for (ni = 0u; ni < node_cnt; ++ni) {
        if (nodes[ni].parent != -1) continue;
        if ((int)ni == io_node || (int)ni == msdos_node) continue;
        if (root_used >= geom->root_entries) {
            ck_set_error(err, 0UL, "root directory full");
            goto cleanup;
        }
        ck_write_dir_slot(root_buf + (ck_u32)root_used * 32UL,
                          nodes[ni].name11, nodes[ni].attr,
                          nodes[ni].dos_time, nodes[ni].dos_date,
                          nodes[ni].first_cluster,
                          nodes[ni].is_dir ? 0UL : nodes[ni].size);
        ++root_used;
    }

    /* 4. Emit Boot Sector (VBR) + optional extra reserved sectors */
    memset(sec, 0, sizeof(sec));
    if (boot_sector_512) {
        memcpy(sec, boot_sector_512, 512);
    } else {
        sec[0] = 0xEBu;
        sec[1] = 0x3Cu;
        sec[2] = 0x90u;
        memcpy(sec + 3, "MSDOS4.0", 8);
        sec[0x3E] = 0xCDu;
        sec[0x3F] = 0x19u;
    }
    ck_write_u16_le(sec + 0x0B, geom->bytes_per_sector);
    sec[0x0D] = geom->sectors_per_cluster;
    ck_write_u16_le(sec + 0x0E, geom->reserved_sectors);
    sec[0x10] = geom->num_fats;
    ck_write_u16_le(sec + 0x11, geom->root_entries);
    if (geom->total_sectors <= 65535UL) {
        ck_write_u16_le(sec + 0x13, (ck_u16)geom->total_sectors);
        ck_write_u32_le(sec + 0x20, 0UL);
    } else {
        ck_write_u16_le(sec + 0x13, 0u);
        ck_write_u32_le(sec + 0x20, geom->total_sectors);
    }
    sec[0x15] = geom->media_byte;
    ck_write_u16_le(sec + 0x16, geom->sectors_per_fat);
    ck_write_u16_le(sec + 0x18, geom->sectors_per_track);
    ck_write_u16_le(sec + 0x1A, geom->num_heads);
    ck_write_u32_le(sec + 0x1C, geom->hidden_sectors);
    sec[0x24] = geom->drive_num;
    sec[0x25] = 0x00u;
    sec[0x26] = 0x29u;
    ck_write_u32_le(sec + 0x27, geom->volume_serial);
    memcpy(sec + 0x2B, geom->volume_label, 11);
    memcpy(sec + 0x36, is_fat16 ? "FAT16   " : "FAT12   ", 8);
    sec[0x1FE] = 0x55u;
    sec[0x1FF] = 0xAAu;

    if (write_fn(write_ctx, sec, 512UL) != 0) {
        ck_set_error(err, 0UL, "write failure on boot sector");
        goto cleanup;
    }
    memset(sec, 0, sizeof(sec));
    {
        ck_u16 r;
        for (r = 1u; r < geom->reserved_sectors; ++r) {
            if (write_fn(write_ctx, sec, 512UL) != 0) goto cleanup;
        }
    }

    /* 5. Emit FAT copies and Root Directory */
    {
        ck_u8 fcopy;
        for (fcopy = 0u; fcopy < geom->num_fats; ++fcopy) {
            if (write_fn(write_ctx, fat, fat_bytes) != 0) {
                ck_set_error(err, 0UL, "write failure on FAT");
                goto cleanup;
            }
        }
    }
    if (write_fn(write_ctx, root_buf, root_bytes) != 0) {
        ck_set_error(err, 0UL, "write failure on root directory");
        goto cleanup;
    }

    /* 6. Stream allocated clusters 2..next_cluster-1 in ascending order */
    {
        ck_u16 cur_clus = 2u;
        while (cur_clus < next_cluster) {
            int owner = -1;
            for (ni = 0u; ni < node_cnt; ++ni) {
                if (nodes[ni].cluster_count > 0u && nodes[ni].first_cluster == cur_clus) {
                    owner = (int)ni;
                    break;
                }
            }
            if (owner < 0) {
                ck_set_error(err, (ck_u32)cur_clus, "internal cluster map gap");
                goto cleanup;
            }

            if (nodes[owner].is_dir) {
                ck_u32 total_sec_cnt = (ck_u32)nodes[owner].cluster_count * (ck_u32)geom->sectors_per_cluster;
                ck_u32 emitted_secs = 0UL;
                int slot_in_sec = 0;
                char dot11[11];
                char dotdot11[11];
                ck_u16 parent_clus = 0u;
                ck_usize k;

                memset(sec, 0, sizeof(sec));
                memset(dot11, ' ', 11);
                dot11[0] = '.';
                memset(dotdot11, ' ', 11);
                dotdot11[0] = '.';
                dotdot11[1] = '.';
                if (nodes[owner].parent >= 0) {
                    parent_clus = nodes[nodes[owner].parent].first_cluster;
                }

                ck_write_dir_slot(sec + 0, dot11, 0x10u,
                                  nodes[owner].dos_time, nodes[owner].dos_date,
                                  nodes[owner].first_cluster, 0UL);
                ck_write_dir_slot(sec + 32, dotdot11, 0x10u,
                                  nodes[owner].dos_time, nodes[owner].dos_date,
                                  parent_clus, 0UL);
                slot_in_sec = 2;

                for (k = 0u; k < node_cnt; ++k) {
                    if (nodes[k].parent != owner) continue;
                    if (slot_in_sec == 16) {
                        if (write_fn(write_ctx, sec, 512UL) != 0) goto cleanup;
                        ++emitted_secs;
                        memset(sec, 0, sizeof(sec));
                        slot_in_sec = 0;
                    }
                    ck_write_dir_slot(sec + slot_in_sec * 32,
                                      nodes[k].name11, nodes[k].attr,
                                      nodes[k].dos_time, nodes[k].dos_date,
                                      nodes[k].first_cluster,
                                      nodes[k].is_dir ? 0UL : nodes[k].size);
                    ++slot_in_sec;
                }
                if (write_fn(write_ctx, sec, 512UL) != 0) goto cleanup;
                ++emitted_secs;
                memset(sec, 0, sizeof(sec));
                while (emitted_secs < total_sec_cnt) {
                    if (write_fn(write_ctx, sec, 512UL) != 0) goto cleanup;
                    ++emitted_secs;
                }
            } else {
                const ck_img_file *f = &files[nodes[owner].file_idx];
                ck_u32 alloc_bytes = (ck_u32)nodes[owner].cluster_count * clus_bytes;
                ck_u32 off = 0UL;
                while (off < alloc_bytes) {
                    memset(sec, 0, sizeof(sec));
                    if (off < f->size) {
                        ck_u32 chunk = (f->size - off > 512UL) ? 512UL : (f->size - off);
                        if (ck_read_file_slice(f, off, sec, chunk) != 0) {
                            ck_set_error(err, off, "source file read failed");
                            goto cleanup;
                        }
                    }
                    if (write_fn(write_ctx, sec, 512UL) != 0) {
                        ck_set_error(err, off, "destination write failed");
                        goto cleanup;
                    }
                    off += 512UL;
                }
            }
            cur_clus = (ck_u16)(cur_clus + nodes[owner].cluster_count);
        }
    }

    /* 7. Zero-pad unallocated tail sectors to exact total_sectors */
    {
        ck_u32 used_data_secs = ((ck_u32)(next_cluster - 2u)) * (ck_u32)geom->sectors_per_cluster;
        ck_u32 rem_secs = geom->total_sectors - first_data_sec - used_data_secs;
        memset(sec, 0, sizeof(sec));
        while (rem_secs > 0UL) {
            if (write_fn(write_ctx, sec, 512UL) != 0) {
                ck_set_error(err, 0UL, "write failure zero-padding volume");
                goto cleanup;
            }
            --rem_secs;
        }
    }

    rc = 0;

cleanup:
    free(nodes);
    free(root_buf);
    free(fat);
    return rc;
}

/* Clean-room active-partition MBR chainloader (relocates 7C00h -> 0600h, boots active entry) */
static void ck_fill_mbr_bootstrap(ck_u8 mbr[512]) {
    int p = 0;
    mbr[p++] = 0xFAu;                                       /* cli */
    mbr[p++] = 0x33u; mbr[p++] = 0xC0u;                     /* xor ax, ax */
    mbr[p++] = 0x8Eu; mbr[p++] = 0xD0u;                     /* mov ss, ax */
    mbr[p++] = 0xBCu; mbr[p++] = 0x00u; mbr[p++] = 0x7Cu;   /* mov sp, 7C00h */
    mbr[p++] = 0xFBu;                                       /* sti */
    mbr[p++] = 0x8Eu; mbr[p++] = 0xD8u;                     /* mov ds, ax */
    mbr[p++] = 0x8Eu; mbr[p++] = 0xC0u;                     /* mov es, ax */
    mbr[p++] = 0xBEu; mbr[p++] = 0x00u; mbr[p++] = 0x7Cu;   /* mov si, 7C00h */
    mbr[p++] = 0xBFu; mbr[p++] = 0x00u; mbr[p++] = 0x06u;   /* mov di, 0600h */
    mbr[p++] = 0xB9u; mbr[p++] = 0x00u; mbr[p++] = 0x01u;   /* mov cx, 0100h */
    mbr[p++] = 0xFCu;                                       /* cld */
    mbr[p++] = 0xF3u; mbr[p++] = 0xA5u;                     /* rep movsw */
    mbr[p++] = 0xEAu; mbr[p++] = 0x1Eu; mbr[p++] = 0x06u;   /* jmp 0000:061Eh */
    mbr[p++] = 0x00u; mbr[p++] = 0x00u;
    /* 061Eh: scan 4 partition entries at 07BEh for boot indicator 80h */
    mbr[p++] = 0xBEu; mbr[p++] = 0xBEu; mbr[p++] = 0x07u;   /* mov si, 07BEh */
    mbr[p++] = 0xB9u; mbr[p++] = 0x04u; mbr[p++] = 0x00u;   /* mov cx, 4 */
    /* next_pt: */
    mbr[p++] = 0x80u; mbr[p++] = 0x3Cu; mbr[p++] = 0x80u;   /* cmp byte ptr [si], 80h */
    mbr[p++] = 0x74u; mbr[p++] = 0x07u;                     /* je found_pt */
    mbr[p++] = 0x83u; mbr[p++] = 0xC6u; mbr[p++] = 0x10u;   /* add si, 16 */
    mbr[p++] = 0xE2u; mbr[p++] = 0xF6u;                     /* loop next_pt */
    mbr[p++] = 0xCDu; mbr[p++] = 0x18u;                     /* int 18h */
    /* found_pt: */
    mbr[p++] = 0x8Au; mbr[p++] = 0x14u;                     /* mov dl, [si] */
    mbr[p++] = 0x8Au; mbr[p++] = 0x74u; mbr[p++] = 0x01u;   /* mov dh, [si+1] */
    mbr[p++] = 0x8Bu; mbr[p++] = 0x4Cu; mbr[p++] = 0x02u;   /* mov cx, [si+2] */
    mbr[p++] = 0xBBu; mbr[p++] = 0x00u; mbr[p++] = 0x7Cu;   /* mov bx, 7C00h */
    mbr[p++] = 0xB8u; mbr[p++] = 0x01u; mbr[p++] = 0x02u;   /* mov ax, 0201h */
    mbr[p++] = 0xCDu; mbr[p++] = 0x13u;                     /* int 13h */
    mbr[p++] = 0xEAu; mbr[p++] = 0x00u; mbr[p++] = 0x7Cu;   /* jmp 0000:7C00h */
    mbr[p++] = 0x00u; mbr[p++] = 0x00u;
}

int ck_diskimg_write_hdd_image(ck_u16 cylinders,
                               ck_u16 heads,
                               ck_u16 sectors_per_track,
                               const char *label,
                               const ck_u8 *boot_sector_512,
                               const ck_img_file *files,
                               ck_usize file_count,
                               int append_vhd_footer,
                               ck_img_write_fn write_fn,
                               void *write_ctx,
                               ck_error *err) {
    ck_fat_geom part_geom;
    ck_u8 sec[512];
    ck_u16 end_cyl;
    ck_u8 part_type;
    ck_u32 s;

    if (cylinders < 2u || heads < 1u || heads > 255u ||
        sectors_per_track < 1u || sectors_per_track > 63u) {
        ck_set_error(err, 0UL, "invalid CHS hard disk geometry");
        return -1;
    }

    ck_diskimg_hdd_geom(cylinders, heads, sectors_per_track, label, &part_geom);
    part_type = (part_geom.total_sectors < 65536UL) ? 0x04u : 0x06u;
    end_cyl = (ck_u16)((cylinders - 1u > 1023u) ? 1023u : (cylinders - 1u));

    /* 1. Sector 0: MBR + Partition Table */
    memset(sec, 0, sizeof(sec));
    ck_fill_mbr_bootstrap(sec);
    sec[0x1BE + 0] = 0x80u;
    sec[0x1BE + 1] = (heads > 1u) ? 1u : 0u;
    sec[0x1BE + 2] = 1u;
    sec[0x1BE + 3] = 0u;
    sec[0x1BE + 4] = part_type;
    sec[0x1BE + 5] = (ck_u8)(heads - 1u);
    sec[0x1BE + 6] = (ck_u8)((sectors_per_track & 0x3Fu) | (((end_cyl >> 8) & 0x03u) << 6));
    sec[0x1BE + 7] = (ck_u8)(end_cyl & 0xFFu);
    ck_write_u32_le(sec + 0x1BE + 8, part_geom.hidden_sectors);
    ck_write_u32_le(sec + 0x1BE + 12, part_geom.total_sectors);
    sec[0x1FE] = 0x55u;
    sec[0x1FF] = 0xAAu;

    if (write_fn(write_ctx, sec, 512UL) != 0) {
        ck_set_error(err, 0UL, "failed writing MBR");
        return -1;
    }

    /* 2. Sectors 1 .. part_start-1: Track 0 alignment padding */
    memset(sec, 0, sizeof(sec));
    for (s = 1UL; s < part_geom.hidden_sectors; ++s) {
        if (write_fn(write_ctx, sec, 512UL) != 0) {
            ck_set_error(err, s * 512UL, "failed writing track 0 padding");
            return -1;
        }
    }

    /* 3. Partition 0 FAT16 volume */
    if (ck_diskimg_write_fat_volume(&part_geom, boot_sector_512, files, file_count,
                                    write_fn, write_ctx, err) != 0) {
        return -1;
    }

    /* 4. Optional Microsoft Fixed VHD 512-byte Footer */
    if (append_vhd_footer) {
        ck_u32 disk_secs = (ck_u32)cylinders * (ck_u32)heads * (ck_u32)sectors_per_track;
        ck_u32 size_lo = (disk_secs << 9);
        ck_u32 size_hi = (disk_secs >> 23);
        ck_u32 sum = 0UL;
        int i;

        memset(sec, 0, sizeof(sec));
        memcpy(sec + 0x00, "conectix", 8);
        ck_write_u32_be(sec + 0x08, 0x00000002UL);
        ck_write_u32_be(sec + 0x0C, 0x00010000UL);
        ck_write_u32_be(sec + 0x10, 0xFFFFFFFFUL);
        ck_write_u32_be(sec + 0x14, 0xFFFFFFFFUL);
        ck_write_u32_be(sec + 0x18, 0UL);
        memcpy(sec + 0x1C, "ckds", 4);
        ck_write_u32_be(sec + 0x20, 0x00000100UL);
        memcpy(sec + 0x24, "Wi2k", 4);
        ck_write_u32_be(sec + 0x28, size_hi);
        ck_write_u32_be(sec + 0x2C, size_lo);
        ck_write_u32_be(sec + 0x30, size_hi);
        ck_write_u32_be(sec + 0x34, size_lo);
        ck_write_u16_be(sec + 0x38, cylinders);
        sec[0x3A] = (ck_u8)heads;
        sec[0x3B] = (ck_u8)sectors_per_track;
        ck_write_u32_be(sec + 0x3C, 0x00000002UL);
        /* Deterministic 16-byte UUID */
        ck_write_u32_be(sec + 0x44, 0x434B4453UL);
        ck_write_u32_be(sec + 0x48, 0x04001988UL);
        ck_write_u32_be(sec + 0x4C, disk_secs);
        ck_write_u32_be(sec + 0x50, part_geom.volume_serial);

        for (i = 0; i < 512; ++i) sum += (ck_u32)sec[i];
        ck_write_u32_be(sec + 0x40, ~sum);

        if (write_fn(write_ctx, sec, 512UL) != 0) {
            ck_set_error(err, 0UL, "failed writing VHD footer");
            return -1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------- */
/* ISO-9660 + El-Torito Bootable CD-ROM Builder                              */
/* ------------------------------------------------------------------------- */

typedef struct ck_iso_node {
    int parent;         /* -1 for root, else index in dirs[] */
    int is_dir;
    int dir_index;      /* 0..dir_cnt-1 if is_dir */
    char ident[16];     /* e.g. "DOS" or "FORMAT.COM;1" */
    ck_u8 ident_len;
    ck_u32 extent_lba;
    ck_u32 data_len;
    const ck_img_file *src;
} ck_iso_node;

static int ck_format_iso_ident(const char *seg, ck_usize len, int is_dir, char out_id[16], ck_u8 *out_len) {
    ck_usize i;
    int pos = 0;
    if (len == 0u || len > 12u) return -1;
    for (i = 0u; i < len && pos < 12; ++i) {
        char c = ck_ascii_up(seg[i]);
        if (c == '-') c = '_';
        out_id[pos++] = c;
    }
    if (!is_dir) {
        out_id[pos++] = ';';
        out_id[pos++] = '1';
    }
    out_id[pos] = '\0';
    *out_len = (ck_u8)pos;
    return 0;
}

static ck_u8 ck_iso_dir_rec_len(ck_u8 name_len) {
    ck_u8 n = (ck_u8)(33u + name_len);
    if ((n & 1u) != 0u) ++n;
    return n;
}

static void ck_iso_write_dir_rec(ck_u8 *dst,
                                 ck_u32 lba,
                                 ck_u32 data_len,
                                 int is_dir,
                                 const char *name,
                                 ck_u8 name_len) {
    ck_u8 rec_len = ck_iso_dir_rec_len(name_len);
    memset(dst, 0, rec_len);
    dst[0] = rec_len;
    ck_write_both32(dst + 2, lba);
    ck_write_both32(dst + 10, data_len);
    dst[18] = 88u; /* 1988 */
    dst[19] = 6u;
    dst[20] = 17u;
    dst[21] = 12u;
    dst[22] = 0u;
    dst[23] = 0u;
    dst[24] = 0u;
    dst[25] = is_dir ? 0x02u : 0x00u;
    ck_write_both16(dst + 28, 1u);
    dst[32] = name_len;
    memcpy(dst + 33, name, name_len);
}

int ck_diskimg_write_eltorito_iso(const char *volume_id,
                                  const ck_img_file *boot_floppy_1440k,
                                  const ck_img_file *files,
                                  ck_usize file_count,
                                  ck_img_write_fn write_fn,
                                  void *write_ctx,
                                  ck_error *err) {
    ck_iso_node *nodes = NULL;
    ck_usize node_cap = file_count * 3u + 8u;
    ck_usize node_cnt = 0u;
    int *dir_nodes = NULL;
    int dir_cnt = 0;
    ck_u8 *blk = NULL;
    ck_u8 *pt_le = NULL;
    ck_u8 *pt_be = NULL;
    ck_u32 pt_bytes = 0UL;
    ck_u32 pt_blocks = 0UL;
    ck_u32 lba_pt_l;
    ck_u32 lba_pt_m;
    ck_u32 cur_lba;
    ck_u32 boot_img_lba = 0UL;
    ck_u32 total_blocks;
    ck_usize fi, i, j;
    int d;
    int rc = -1;

    if (!boot_floppy_1440k || boot_floppy_1440k->size != 1474560UL || !write_fn) {
        ck_set_error(err, 0UL, "El-Torito ISO requires a 1,474,560-byte 1.44M boot floppy");
        return -1;
    }

    nodes = (ck_iso_node *)calloc((unsigned int)node_cap, sizeof(ck_iso_node));
    blk = (ck_u8 *)calloc(1u, 2048u);
    if (!nodes || !blk) {
        ck_set_error(err, 0UL, "out of memory building ISO");
        goto cleanup;
    }

    /* Node 0 is root directory */
    nodes[0].parent = -1;
    nodes[0].is_dir = 1;
    nodes[0].dir_index = 0;
    nodes[0].ident[0] = '\0';
    nodes[0].ident_len = 1u;
    node_cnt = 1u;

    /* Node 1 is BOOT1440.IMG in root */
    nodes[1].parent = 0;
    nodes[1].is_dir = 0;
    strcpy(nodes[1].ident, "BOOT1440.IMG;1");
    nodes[1].ident_len = (ck_u8)strlen(nodes[1].ident);
    nodes[1].data_len = boot_floppy_1440k->size;
    nodes[1].src = boot_floppy_1440k;
    node_cnt = 2u;

    for (fi = 0u; fi < file_count; ++fi) {
        const char *p = files[fi].dos_path;
        int parent = 0;
        if (!p || !p[0]) continue;
        while (*p == '/' || *p == '\\') ++p;
        while (*p != '\0') {
            const char *start = p;
            ck_usize slen;
            int is_last;
            char id[16];
            ck_u8 id_len = 0u;

            while (*p != '\0' && *p != '/' && *p != '\\') ++p;
            slen = (ck_usize)(p - start);
            while (*p == '/' || *p == '\\') ++p;
            is_last = (*p == '\0') ? 1 : 0;

            if (ck_format_iso_ident(start, slen, !is_last, id, &id_len) != 0) {
                ck_set_error(err, (ck_u32)fi, "invalid ISO-9660 identifier");
                goto cleanup;
            }
            if (!is_last) {
                int found = -1;
                for (i = 1u; i < node_cnt; ++i) {
                    if (nodes[i].parent == parent && nodes[i].is_dir &&
                        strcmp(nodes[i].ident, id) == 0) {
                        found = (int)i;
                        break;
                    }
                }
                if (found < 0) {
                    if (node_cnt >= node_cap) {
                        ck_usize ncap = node_cap * 2u;
                        ck_iso_node *g = (ck_iso_node *)realloc(
                            nodes, (unsigned int)(ncap * sizeof(ck_iso_node)));
                        if (!g) goto cleanup;
                        nodes = g;
                        node_cap = ncap;
                    }
                    memset(&nodes[node_cnt], 0, sizeof(ck_iso_node));
                    nodes[node_cnt].parent = parent;
                    nodes[node_cnt].is_dir = 1;
                    strcpy(nodes[node_cnt].ident, id);
                    nodes[node_cnt].ident_len = id_len;
                    found = (int)node_cnt++;
                }
                parent = found;
            } else {
                if (parent == 0 && strcmp(id, "BOOT1440.IMG;1") == 0) continue;
                if (node_cnt >= node_cap) {
                    ck_usize ncap = node_cap * 2u;
                    ck_iso_node *g = (ck_iso_node *)realloc(
                        nodes, (unsigned int)(ncap * sizeof(ck_iso_node)));
                    if (!g) goto cleanup;
                    nodes = g;
                    node_cap = ncap;
                }
                memset(&nodes[node_cnt], 0, sizeof(ck_iso_node));
                nodes[node_cnt].parent = parent;
                nodes[node_cnt].is_dir = 0;
                strcpy(nodes[node_cnt].ident, id);
                nodes[node_cnt].ident_len = id_len;
                nodes[node_cnt].data_len = files[fi].size;
                nodes[node_cnt].src = &files[fi];
                ++node_cnt;
            }
        }
    }

    /* Sort non-root nodes lexicographically by (parent, ident) per ISO-9660 */
    for (i = 1u; i < node_cnt; ++i) {
        for (j = i + 1u; j < node_cnt; ++j) {
            int swap = 0;
            if (nodes[j].parent < nodes[i].parent) {
                swap = 1;
            } else if (nodes[j].parent == nodes[i].parent &&
                       strcmp(nodes[j].ident, nodes[i].ident) < 0) {
                swap = 1;
            }
            if (swap) {
                ck_iso_node tmp = nodes[i];
                int old_i = (int)i;
                int old_j = (int)j;
                ck_usize k;
                nodes[i] = nodes[j];
                nodes[j] = tmp;
                for (k = 1u; k < node_cnt; ++k) {
                    if (nodes[k].parent == old_i) nodes[k].parent = old_j;
                    else if (nodes[k].parent == old_j) nodes[k].parent = old_i;
                }
            }
        }
    }

    dir_nodes = (int *)calloc((unsigned int)node_cnt, sizeof(int));
    if (!dir_nodes) goto cleanup;
    for (i = 0u; i < node_cnt; ++i) {
        if (nodes[i].is_dir) {
            nodes[i].dir_index = dir_cnt;
            dir_nodes[dir_cnt++] = (int)i;
        }
    }

    /* Compute Path Table size */
    for (d = 0; d < dir_cnt; ++d) {
        int dn = dir_nodes[d];
        ck_u8 nlen = (dn == 0) ? 1u : nodes[dn].ident_len;
        pt_bytes += 8UL + (ck_u32)nlen + ((nlen & 1u) ? 1UL : 0UL);
    }
    pt_blocks = (pt_bytes + 2047UL) / 2048UL;
    lba_pt_l = 20UL;
    lba_pt_m = lba_pt_l + pt_blocks;
    cur_lba = lba_pt_m + pt_blocks;

    /* Assign directory extent sizes and LBAs */
    for (d = 0; d < dir_cnt; ++d) {
        int dn = dir_nodes[d];
        ck_u32 secs = 1UL;
        ck_u32 used = 68UL; /* . and .. */
        for (i = 1u; i < node_cnt; ++i) {
            if (nodes[i].parent == dn) {
                ck_u8 rl = ck_iso_dir_rec_len(nodes[i].ident_len);
                if (used + (ck_u32)rl > 2048UL) {
                    ++secs;
                    used = 0UL;
                }
                used += (ck_u32)rl;
            }
        }
        nodes[dn].extent_lba = cur_lba;
        nodes[dn].data_len = secs * 2048UL;
        cur_lba += secs;
    }

    /* Assign file LBAs */
    for (i = 1u; i < node_cnt; ++i) {
        if (nodes[i].is_dir) continue;
        nodes[i].extent_lba = cur_lba;
        if (nodes[i].src == boot_floppy_1440k) {
            boot_img_lba = cur_lba;
        }
        cur_lba += (nodes[i].data_len + 2047UL) / 2048UL;
    }
    total_blocks = cur_lba;

    /* Build L and M Path Tables */
    pt_le = (ck_u8 *)calloc(1u, (unsigned int)(pt_blocks * 2048UL));
    pt_be = (ck_u8 *)calloc(1u, (unsigned int)(pt_blocks * 2048UL));
    if (!pt_le || !pt_be) goto cleanup;
    {
        ck_u32 pos = 0UL;
        for (d = 0; d < dir_cnt; ++d) {
            int dn = dir_nodes[d];
            ck_u8 nlen = (dn == 0) ? 1u : nodes[dn].ident_len;
            ck_u16 pnum = (dn == 0) ? 1u : (ck_u16)(nodes[nodes[dn].parent].dir_index + 1);
            pt_le[pos] = nlen;
            pt_be[pos] = nlen;
            ck_write_u32_le(pt_le + pos + 2, nodes[dn].extent_lba);
            ck_write_u32_be(pt_be + pos + 2, nodes[dn].extent_lba);
            ck_write_u16_le(pt_le + pos + 6, pnum);
            ck_write_u16_be(pt_be + pos + 6, pnum);
            if (dn == 0) {
                pt_le[pos + 8] = 0u;
                pt_be[pos + 8] = 0u;
            } else {
                memcpy(pt_le + pos + 8, nodes[dn].ident, nlen);
                memcpy(pt_be + pos + 8, nodes[dn].ident, nlen);
            }
            pos += 8UL + (ck_u32)nlen + ((nlen & 1u) ? 1UL : 0UL);
        }
    }

    /* Blocks 0..15: System Area */
    memset(blk, 0, 2048);
    for (i = 0u; i < 16u; ++i) {
        if (write_fn(write_ctx, blk, 2048UL) != 0) goto cleanup;
    }

    /* Block 16: Primary Volume Descriptor */
    memset(blk, 0, 2048);
    blk[0] = 1u;
    memcpy(blk + 1, "CD001", 5);
    blk[6] = 1u;
    memset(blk + 8, ' ', 32);
    memcpy(blk + 8, "MS-DOS 4.00", 11);
    memset(blk + 40, ' ', 32);
    if (volume_id) {
        ck_usize vl = strlen(volume_id);
        if (vl > 32u) vl = 32u;
        for (i = 0u; i < vl; ++i) blk[40 + i] = (ck_u8)ck_ascii_up(volume_id[i]);
    } else {
        memcpy(blk + 40, "CKDOS_400", 9);
    }
    ck_write_both32(blk + 80, total_blocks);
    ck_write_both16(blk + 120, 1u);
    ck_write_both16(blk + 124, 1u);
    ck_write_both16(blk + 128, 2048u);
    ck_write_both32(blk + 132, pt_bytes);
    ck_write_u32_le(blk + 140, lba_pt_l);
    ck_write_u32_be(blk + 148, lba_pt_m);
    {
        char zero_id = '\0';
        ck_iso_write_dir_rec(blk + 156, nodes[0].extent_lba, nodes[0].data_len, 1, &zero_id, 1u);
    }
    memset(blk + 190, ' ', 623);
    memcpy(blk + 813, "1988061712000000\0", 17);
    memcpy(blk + 830, "1988061712000000\0", 17);
    memcpy(blk + 847, "0000000000000000\0", 17);
    memcpy(blk + 864, "1988061712000000\0", 17);
    blk[881] = 1u;
    if (write_fn(write_ctx, blk, 2048UL) != 0) goto cleanup;

    /* Block 17: El-Torito Boot Record Volume Descriptor */
    memset(blk, 0, 2048);
    blk[0] = 0u;
    memcpy(blk + 1, "CD001", 5);
    blk[6] = 1u;
    memcpy(blk + 7, "EL TORITO SPECIFICATION", 23);
    ck_write_u32_le(blk + 0x47, 19UL); /* Boot Catalog at block 19 */
    if (write_fn(write_ctx, blk, 2048UL) != 0) goto cleanup;

    /* Block 18: Volume Descriptor Set Terminator */
    memset(blk, 0, 2048);
    blk[0] = 0xFFu;
    memcpy(blk + 1, "CD001", 5);
    blk[6] = 1u;
    if (write_fn(write_ctx, blk, 2048UL) != 0) goto cleanup;

    /* Block 19: El-Torito Boot Catalog */
    memset(blk, 0, 2048);
    blk[0x00] = 0x01u; /* Validation Entry */
    blk[0x01] = 0x00u; /* 80x86 */
    memcpy(blk + 0x04, "CK-DOS 4.00", 11);
    blk[0x1E] = 0x55u;
    blk[0x1F] = 0xAAu;
    {
        ck_u16 wsum = 0u;
        for (i = 0u; i < 32u; i += 2u) {
            wsum = (ck_u16)(wsum + ck_read_u16_le(blk + i));
        }
        ck_write_u16_le(blk + 0x1C, (ck_u16)(0u - wsum));
    }
    blk[0x20] = 0x88u; /* Bootable Initial/Default Entry */
    blk[0x21] = 0x02u; /* 1.44 MB diskette emulation */
    ck_write_u16_le(blk + 0x22, 0u);
    blk[0x24] = 0x00u;
    ck_write_u16_le(blk + 0x26, 1u);
    ck_write_u32_le(blk + 0x28, boot_img_lba);
    if (write_fn(write_ctx, blk, 2048UL) != 0) goto cleanup;

    /* Blocks 20..: Path Tables */
    if (write_fn(write_ctx, pt_le, pt_blocks * 2048UL) != 0) goto cleanup;
    if (write_fn(write_ctx, pt_be, pt_blocks * 2048UL) != 0) goto cleanup;

    /* Directory Extents */
    for (d = 0; d < dir_cnt; ++d) {
        int dn = dir_nodes[d];
        int pnode = (dn == 0) ? 0 : nodes[dn].parent;
        char dot_c = '\0';
        char dotdot_c = '\x01';
        ck_u32 pos = 0UL;

        memset(blk, 0, 2048);
        ck_iso_write_dir_rec(blk + 0, nodes[dn].extent_lba, nodes[dn].data_len, 1, &dot_c, 1u);
        ck_iso_write_dir_rec(blk + 34, nodes[pnode].extent_lba, nodes[pnode].data_len, 1, &dotdot_c, 1u);
        pos = 68UL;

        for (i = 1u; i < node_cnt; ++i) {
            ck_u8 rl;
            if (nodes[i].parent != dn) continue;
            rl = ck_iso_dir_rec_len(nodes[i].ident_len);
            if (pos + (ck_u32)rl > 2048UL) {
                if (write_fn(write_ctx, blk, 2048UL) != 0) goto cleanup;
                memset(blk, 0, 2048);
                pos = 0UL;
            }
            ck_iso_write_dir_rec(blk + pos, nodes[i].extent_lba, nodes[i].data_len,
                                 nodes[i].is_dir, nodes[i].ident, nodes[i].ident_len);
            pos += (ck_u32)rl;
        }
        if (write_fn(write_ctx, blk, 2048UL) != 0) goto cleanup;
    }

    /* File Extents */
    for (i = 1u; i < node_cnt; ++i) {
        ck_u32 padded;
        ck_u32 off = 0UL;
        if (nodes[i].is_dir) continue;
        padded = ((nodes[i].data_len + 2047UL) / 2048UL) * 2048UL;
        while (off < padded) {
            memset(blk, 0, 2048);
            if (off < nodes[i].data_len) {
                ck_u32 chunk = (nodes[i].data_len - off > 2048UL) ? 2048UL : (nodes[i].data_len - off);
                if (ck_read_file_slice(nodes[i].src, off, blk, chunk) != 0) goto cleanup;
            }
            if (write_fn(write_ctx, blk, 2048UL) != 0) goto cleanup;
            off += 2048UL;
        }
    }

    rc = 0;

cleanup:
    free(pt_be);
    free(pt_le);
    free(dir_nodes);
    free(blk);
    free(nodes);
    return rc;
}

/* ------------------------------------------------------------------------- */
/* Streaming CRC-32 & PKZIP 2.0 Archive Builder                              */
/* ------------------------------------------------------------------------- */

ck_u32 ck_diskimg_crc32(ck_u32 init_crc, const ck_u8 *buf, ck_u32 len) {
    ck_u32 crc = ~init_crc;
    ck_u32 i;
    for (i = 0UL; i < len; ++i) {
        int b;
        crc ^= (ck_u32)buf[i];
        for (b = 0; b < 8; ++b) {
            if ((crc & 1UL) != 0UL) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

typedef struct ck_zip_meta {
    ck_u32 crc;
    ck_u32 local_offset;
    ck_u16 name_len;
} ck_zip_meta;

int ck_diskimg_write_zip(const ck_zip_item *items,
                         ck_usize item_count,
                         ck_img_write_fn write_fn,
                         void *write_ctx,
                         ck_error *err) {
    ck_zip_meta *meta = NULL;
    ck_u8 *chunk_buf = NULL;
    ck_u8 hdr[46];
    ck_u32 cur_off = 0UL;
    ck_u32 cd_start;
    ck_u32 cd_size = 0UL;
    ck_usize i;
    int rc = -1;

    if (!write_fn || item_count > 65535u) {
        ck_set_error(err, 0UL, "invalid ZIP parameters");
        return -1;
    }

    meta = (ck_zip_meta *)calloc(item_count ? (unsigned int)item_count : 1u, sizeof(ck_zip_meta));
    chunk_buf = (ck_u8 *)malloc(4096u);
    if (!meta || !chunk_buf) {
        ck_set_error(err, 0UL, "out of memory building ZIP");
        goto cleanup;
    }

    /* Pre-pass: compute CRC-32 of each entry so Local File Header carries exact CRC */
    for (i = 0u; i < item_count; ++i) {
        ck_img_file tmp_f;
        ck_u32 off = 0UL;
        ck_u32 crc = 0UL;
        if (!items[i].archive_path || items[i].archive_path[0] == '\0') {
            ck_set_error(err, (ck_u32)i, "empty ZIP entry path");
            goto cleanup;
        }
        meta[i].name_len = (ck_u16)strlen(items[i].archive_path);
        tmp_f.mem_data = items[i].mem_data;
        tmp_f.read_ctx = items[i].read_ctx;
        tmp_f.read_fn = items[i].read_fn;
        while (off < items[i].size) {
            ck_u32 n = (items[i].size - off > 4096UL) ? 4096UL : (items[i].size - off);
            if (ck_read_file_slice(&tmp_f, off, chunk_buf, n) != 0) {
                ck_set_error(err, off, "failed reading ZIP input entry");
                goto cleanup;
            }
            crc = ck_diskimg_crc32(crc, chunk_buf, n);
            off += n;
        }
        meta[i].crc = crc;
    }

    /* Local File Headers + File Payloads */
    for (i = 0u; i < item_count; ++i) {
        ck_img_file tmp_f;
        ck_u32 off = 0UL;
        ck_u16 dtime = items[i].dos_time ? items[i].dos_time : CK_DEFAULT_DOS_TIME;
        ck_u16 ddate = items[i].dos_date ? items[i].dos_date : CK_DEFAULT_DOS_DATE;

        meta[i].local_offset = cur_off;
        memset(hdr, 0, 30);
        ck_write_u32_le(hdr + 0, 0x04034B50UL);
        ck_write_u16_le(hdr + 4, 20u);
        ck_write_u16_le(hdr + 6, 0u);
        ck_write_u16_le(hdr + 8, 0u); /* Stored */
        ck_write_u16_le(hdr + 10, dtime);
        ck_write_u16_le(hdr + 12, ddate);
        ck_write_u32_le(hdr + 14, meta[i].crc);
        ck_write_u32_le(hdr + 18, items[i].size);
        ck_write_u32_le(hdr + 22, items[i].size);
        ck_write_u16_le(hdr + 26, meta[i].name_len);
        ck_write_u16_le(hdr + 28, 0u);

        if (write_fn(write_ctx, hdr, 30UL) != 0) goto cleanup;
        if (write_fn(write_ctx, (const ck_u8 *)items[i].archive_path, (ck_u32)meta[i].name_len) != 0) goto cleanup;
        cur_off += 30UL + (ck_u32)meta[i].name_len;

        tmp_f.mem_data = items[i].mem_data;
        tmp_f.read_ctx = items[i].read_ctx;
        tmp_f.read_fn = items[i].read_fn;
        while (off < items[i].size) {
            ck_u32 n = (items[i].size - off > 4096UL) ? 4096UL : (items[i].size - off);
            if (ck_read_file_slice(&tmp_f, off, chunk_buf, n) != 0) goto cleanup;
            if (write_fn(write_ctx, chunk_buf, n) != 0) goto cleanup;
            off += n;
        }
        cur_off += items[i].size;
    }

    /* Central Directory */
    cd_start = cur_off;
    for (i = 0u; i < item_count; ++i) {
        ck_u16 dtime = items[i].dos_time ? items[i].dos_time : CK_DEFAULT_DOS_TIME;
        ck_u16 ddate = items[i].dos_date ? items[i].dos_date : CK_DEFAULT_DOS_DATE;
        ck_u16 ver_made = items[i].unix_mode ? (ck_u16)((3u << 8) | 20u) : (ck_u16)20u;
        ck_u32 ext_attr = items[i].unix_mode ? (items[i].unix_mode << 16) : 0x20UL;

        memset(hdr, 0, 46);
        ck_write_u32_le(hdr + 0, 0x02014B50UL);
        ck_write_u16_le(hdr + 4, ver_made);
        ck_write_u16_le(hdr + 6, 20u);
        ck_write_u16_le(hdr + 8, 0u);
        ck_write_u16_le(hdr + 10, 0u);
        ck_write_u16_le(hdr + 12, dtime);
        ck_write_u16_le(hdr + 14, ddate);
        ck_write_u32_le(hdr + 16, meta[i].crc);
        ck_write_u32_le(hdr + 20, items[i].size);
        ck_write_u32_le(hdr + 24, items[i].size);
        ck_write_u16_le(hdr + 28, meta[i].name_len);
        ck_write_u32_le(hdr + 38, ext_attr);
        ck_write_u32_le(hdr + 42, meta[i].local_offset);

        if (write_fn(write_ctx, hdr, 46UL) != 0) goto cleanup;
        if (write_fn(write_ctx, (const ck_u8 *)items[i].archive_path, (ck_u32)meta[i].name_len) != 0) goto cleanup;
        cd_size += 46UL + (ck_u32)meta[i].name_len;
    }

    /* End of Central Directory */
    memset(hdr, 0, 22);
    ck_write_u32_le(hdr + 0, 0x06054B50UL);
    ck_write_u16_le(hdr + 8, (ck_u16)item_count);
    ck_write_u16_le(hdr + 10, (ck_u16)item_count);
    ck_write_u32_le(hdr + 12, cd_size);
    ck_write_u32_le(hdr + 16, cd_start);
    if (write_fn(write_ctx, hdr, 22UL) != 0) goto cleanup;

    rc = 0;

cleanup:
    free(chunk_buf);
    free(meta);
    return rc;
}
