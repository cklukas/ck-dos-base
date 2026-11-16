/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_MZ_CORE_H
#define DOSBASE_DEVTOOLS_CORE_MZ_CORE_H

#include "dosbase/devtools/core/c_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CK_MZ_SIG_MZ 0x5A4Du
#define CK_MZ_SIG_ZM 0x4D5Au
#define CK_MZ_FIXED_HEADER_BYTES 28u

typedef struct ck_mz_header {
    ck_u16 signature;
    ck_u16 bytes_in_last_page;
    ck_u16 pages_in_file;
    ck_u16 relocation_count;
    ck_u16 header_paragraphs;
    ck_u16 minimum_extra_paragraphs;
    ck_u16 maximum_extra_paragraphs;
    ck_u16 initial_ss;
    ck_u16 initial_sp;
    ck_u16 checksum;
    ck_u16 initial_ip;
    ck_u16 initial_cs;
    ck_u16 relocation_table_offset;
    ck_u16 overlay_number;
} ck_mz_header;

typedef struct ck_mz_reloc {
    ck_u16 offset;
    ck_u16 segment;
} ck_mz_reloc;

typedef struct ck_mz_view {
    ck_mz_header header;
    ck_u32 header_bytes;
    ck_u32 image_offset;
    ck_u32 image_size;
    ck_u32 trailing_bytes;
} ck_mz_view;

/* Parses MZ header and validates table/image bounds against total_size.
   Returns 0 on success, -1 on error (populating err). */
int ck_mz_parse_view(const ck_u8 *bytes,
                     ck_u32 total_size,
                     ck_mz_view *out_view,
                     ck_error *err);

/* Reads relocation entry `index` from `bytes` (already validated by ck_mz_parse_view). */
ck_mz_reloc ck_mz_get_reloc(const ck_u8 *bytes,
                            const ck_mz_view *view,
                            ck_u16 index);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_MZ_CORE_H */
