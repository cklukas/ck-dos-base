/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/exefix_core.h"
#include "dosbase/devtools/core/mz_core.h"

int ck_exefix_patch(ck_u8 *bytes,
                    ck_u32 available_bytes,
                    ck_u32 total_size,
                    const ck_exefix_opts *opts,
                    ck_exefix_info *out_info,
                    ck_error *err) {
    ck_mz_view view;
    ck_mz_header header;
    ck_s32 header_size;
    ck_s32 prog_size;
    ck_s32 stack_top;
    ck_s32 diff;
    ck_u32 min_needed_u32;
    ck_u16 min_needed;

    if (available_bytes >= total_size) {
        if (ck_mz_parse_view(bytes, total_size, &view, err) != 0) {
            return -1;
        }
        header = view.header;
    } else {
        if (available_bytes < CK_MZ_FIXED_HEADER_BYTES || total_size < CK_MZ_FIXED_HEADER_BYTES) {
            ck_set_error(err, 0, "truncated MZ header");
            return -1;
        }
        if (ck_mz_parse_view(bytes, available_bytes, &view, err) != 0) {
            /* If only the declared size check failed because we only read the header, validate header fields */
            if (ck_read_u16_le(bytes) != CK_MZ_SIG_MZ && ck_read_u16_le(bytes) != CK_MZ_SIG_ZM) {
                return -1;
            }
        }
        header.signature = ck_read_u16_le(bytes + 0);
        header.bytes_in_last_page = ck_read_u16_le(bytes + 2);
        header.pages_in_file = ck_read_u16_le(bytes + 4);
        header.relocation_count = ck_read_u16_le(bytes + 6);
        header.header_paragraphs = ck_read_u16_le(bytes + 8);
        header.minimum_extra_paragraphs = ck_read_u16_le(bytes + 10);
        header.maximum_extra_paragraphs = ck_read_u16_le(bytes + 12);
        header.initial_ss = ck_read_u16_le(bytes + 14);
        header.initial_sp = ck_read_u16_le(bytes + 16);
        header.checksum = ck_read_u16_le(bytes + 18);
        header.initial_ip = ck_read_u16_le(bytes + 20);
        header.initial_cs = ck_read_u16_le(bytes + 22);
        header.relocation_table_offset = ck_read_u16_le(bytes + 24);
        header.overlay_number = ck_read_u16_le(bytes + 26);
        if ((ck_u32)header.header_paragraphs * 16u < CK_MZ_FIXED_HEADER_BYTES ||
            (ck_u32)header.header_paragraphs * 16u > total_size) {
            ck_set_error(err, 8, "invalid header_paragraphs");
            return -1;
        }
    }
    header.signature = CK_MZ_SIG_MZ;
    if (opts) {
        if (opts->has_max_alloc) {
            header.maximum_extra_paragraphs = opts->max_alloc;
        }
        if (opts->has_min_alloc) {
            header.minimum_extra_paragraphs = opts->min_alloc;
        }
    }

    header.bytes_in_last_page = (ck_u16)(total_size % 512u);
    header.pages_in_file = (ck_u16)((total_size + 511u) / 512u);

    header_size = (ck_s32)header.header_paragraphs * 16L;
    prog_size = (ck_s32)total_size - header_size;
    stack_top = ((ck_s32)header.initial_ss << 4) + (ck_s32)header.initial_sp;
    diff = stack_top - prog_size;

    if (out_info) {
        out_info->min_clamped_to_stack = 0;
        out_info->max_clamped_to_min = 0;
    }
    if (diff > 0) {
        min_needed_u32 = (ck_u32)(diff + 15L) >> 4;
        if (min_needed_u32 > 0xFFFFu) {
            min_needed_u32 = 0xFFFFu;
        }
        min_needed = (ck_u16)min_needed_u32;
        if (header.minimum_extra_paragraphs < min_needed) {
            header.minimum_extra_paragraphs = min_needed;
            if (out_info) out_info->min_clamped_to_stack = 1;
        }
    }
    if (header.minimum_extra_paragraphs > header.maximum_extra_paragraphs) {
        header.maximum_extra_paragraphs = header.minimum_extra_paragraphs;
        if (out_info) out_info->max_clamped_to_min = 1;
    }

    ck_write_u16_le(bytes + 0, header.signature);
    ck_write_u16_le(bytes + 2, header.bytes_in_last_page);
    ck_write_u16_le(bytes + 4, header.pages_in_file);
    ck_write_u16_le(bytes + 10, header.minimum_extra_paragraphs);
    ck_write_u16_le(bytes + 12, header.maximum_extra_paragraphs);
    return 0;
}
