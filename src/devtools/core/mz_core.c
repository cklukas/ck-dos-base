/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/mz_core.h"

int ck_mz_parse_view(const ck_u8 *bytes,
                     ck_u32 total_size,
                     ck_mz_view *out_view,
                     ck_error *err) {
    ck_mz_header *h;
    ck_u32 header_bytes;
    ck_u32 table;
    ck_u32 table_bytes;
    ck_u32 declared;
    ck_u32 image_length;

    if (!bytes || !out_view) {
        ck_set_error(err, 0, "null MZ buffer");
        return -1;
    }
    if (total_size < CK_MZ_FIXED_HEADER_BYTES) {
        ck_set_error(err, total_size, "truncated MZ header");
        return -1;
    }

    h = &out_view->header;
    h->signature = ck_read_u16_le(bytes + 0);
    if (h->signature != CK_MZ_SIG_MZ && h->signature != CK_MZ_SIG_ZM) {
        ck_set_error(err, 0, "not an MZ executable");
        return -1;
    }
    h->bytes_in_last_page = ck_read_u16_le(bytes + 2);
    h->pages_in_file = ck_read_u16_le(bytes + 4);
    h->relocation_count = ck_read_u16_le(bytes + 6);
    h->header_paragraphs = ck_read_u16_le(bytes + 8);
    h->minimum_extra_paragraphs = ck_read_u16_le(bytes + 10);
    h->maximum_extra_paragraphs = ck_read_u16_le(bytes + 12);
    h->initial_ss = ck_read_u16_le(bytes + 14);
    h->initial_sp = ck_read_u16_le(bytes + 16);
    h->checksum = ck_read_u16_le(bytes + 18);
    h->initial_ip = ck_read_u16_le(bytes + 20);
    h->initial_cs = ck_read_u16_le(bytes + 22);
    h->relocation_table_offset = ck_read_u16_le(bytes + 24);
    h->overlay_number = ck_read_u16_le(bytes + 26);

    header_bytes = (ck_u32)h->header_paragraphs * 16u;
    if (header_bytes < CK_MZ_FIXED_HEADER_BYTES) {
        ck_set_error(err, 8, "header_paragraphs does not cover the fixed header");
        return -1;
    }
    if (header_bytes > total_size) {
        ck_set_error(err, 8, "header runs past the end of the file");
        return -1;
    }

    table = (ck_u32)h->relocation_table_offset;
    table_bytes = (ck_u32)h->relocation_count * 4u;
    if (h->relocation_count != 0) {
        if (table < CK_MZ_FIXED_HEADER_BYTES) {
            ck_set_error(err, 24, "relocation table overlaps the fixed header");
            return -1;
        }
        if (table > header_bytes || table_bytes > header_bytes - table) {
            ck_set_error(err, 24, "relocation table runs past the header");
            return -1;
        }
    }

    declared = 0;
    if (h->pages_in_file != 0) {
        declared = ((ck_u32)h->pages_in_file - 1u) * 512u;
        declared += (h->bytes_in_last_page != 0) ? (ck_u32)h->bytes_in_last_page : 512u;
    }
    if (declared < header_bytes) {
        ck_set_error(err, 2, "declared file size is smaller than the header");
        return -1;
    }
    image_length = declared - header_bytes;
    if (image_length > total_size - header_bytes) {
        ck_set_error(err, 2, "declared file size runs past the end of the file");
        return -1;
    }

    out_view->header_bytes = header_bytes;
    out_view->image_offset = header_bytes;
    out_view->image_size = image_length;
    out_view->trailing_bytes = total_size - header_bytes - image_length;
    return 0;
}

ck_mz_reloc ck_mz_get_reloc(const ck_u8 *bytes,
                            const ck_mz_view *view,
                            ck_u16 index) {
    ck_mz_reloc r;
    ck_u32 at = (ck_u32)view->header.relocation_table_offset + (ck_u32)index * 4u;
    r.offset = ck_read_u16_le(bytes + at);
    r.segment = ck_read_u16_le(bytes + at + 2u);
    return r;
}
