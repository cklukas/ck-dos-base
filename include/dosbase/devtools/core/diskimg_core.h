/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_DISKIMG_CORE_H
#define DOSBASE_DEVTOOLS_CORE_DISKIMG_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ck_floppy_kind {
    CK_FLOPPY_360K = 0,
    CK_FLOPPY_720K = 1,
    CK_FLOPPY_1200K = 2,
    CK_FLOPPY_1440K = 3
} ck_floppy_kind;

typedef struct ck_fat_geom {
    ck_u16 bytes_per_sector;
    ck_u8 sectors_per_cluster;
    ck_u16 reserved_sectors;
    ck_u8 num_fats;
    ck_u16 root_entries;
    ck_u32 total_sectors;
    ck_u8 media_byte;
    ck_u16 sectors_per_fat;
    ck_u16 sectors_per_track;
    ck_u16 num_heads;
    ck_u32 hidden_sectors;
    ck_u8 drive_num;
    ck_u32 volume_serial;
    char volume_label[11];
} ck_fat_geom;

typedef int (*ck_img_write_fn)(void *ctx, const ck_u8 *buf, ck_u32 len);
typedef int (*ck_img_read_fn)(void *ctx, ck_u32 offset, ck_u8 *buf, ck_u32 len);

typedef struct ck_img_file {
    const char *dos_path;
    ck_u8 attr;
    ck_u16 dos_time;
    ck_u16 dos_date;
    ck_u32 size;
    const ck_u8 *mem_data;
    void *read_ctx;
    ck_img_read_fn read_fn;
} ck_img_file;

void ck_diskimg_floppy_geom(ck_floppy_kind kind, const char *label, ck_fat_geom *out_geom);

void ck_diskimg_hdd_geom(ck_u16 cylinders,
                         ck_u16 heads,
                         ck_u16 sectors_per_track,
                         const char *label,
                         ck_fat_geom *out_part_geom);

int ck_diskimg_write_fat_volume(const ck_fat_geom *geom,
                                const ck_u8 *boot_sector_512,
                                const ck_img_file *files,
                                ck_usize file_count,
                                ck_img_write_fn write_fn,
                                void *write_ctx,
                                ck_error *err);

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
                               ck_error *err);

int ck_diskimg_write_eltorito_iso(const char *volume_id,
                                  const ck_img_file *boot_floppy_1440k,
                                  const ck_img_file *files,
                                  ck_usize file_count,
                                  ck_img_write_fn write_fn,
                                  void *write_ctx,
                                  ck_error *err);

typedef struct ck_zip_item {
    const char *archive_path;
    ck_u16 dos_time;
    ck_u16 dos_date;
    ck_u32 unix_mode;
    ck_u32 size;
    const ck_u8 *mem_data;
    void *read_ctx;
    ck_img_read_fn read_fn;
} ck_zip_item;

ck_u32 ck_diskimg_crc32(ck_u32 init_crc, const ck_u8 *buf, ck_u32 len);

int ck_diskimg_write_zip(const ck_zip_item *items,
                         ck_usize item_count,
                         ck_img_write_fn write_fn,
                         void *write_ctx,
                         ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_DISKIMG_CORE_H */
