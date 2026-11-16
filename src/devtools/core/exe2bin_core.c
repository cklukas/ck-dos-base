/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/exe2bin_core.h"
#include "dosbase/devtools/core/mz_core.h"

#define CK_COM_ORIGIN 0x0100u

int ck_exe2bin(const ck_u8 *exe_bytes,
               ck_u32 exe_size,
               int has_base_seg,
               ck_u16 base_seg,
               ck_exe2bin_out *out,
               ck_error *err) {
    ck_mz_view view;
    ck_u8 *load;
    ck_u16 idx;
    ck_u32 at;
    ck_u16 word;
    ck_u16 fixed;

    if (!out) {
        ck_set_error(err, 0, "null output");
        return -1;
    }
    out->bytes = NULL;
    out->size = 0;

    if (ck_mz_parse_view(exe_bytes, exe_size, &view, err) != 0) {
        return -1;
    }
    if (view.header.relocation_count != 0 && !has_base_seg) {
        ck_set_error(err, 6, "the image needs fix-ups but no base segment was given");
        return -1;
    }
    if (view.header.relocation_count == 0 && has_base_seg) {
        ck_set_error(err, 6, "a base segment was given for an image with no fix-ups");
        return -1;
    }
    if (view.header.initial_ss != 0 || view.header.initial_sp != 0) {
        ck_set_error(err, 14, "file cannot be converted: the image declares a stack");
        return -1;
    }
    if (view.header.initial_cs != 0) {
        ck_set_error(err, 22, "file cannot be converted: the entry segment is not zero");
        return -1;
    }
    if (view.trailing_bytes != 0) {
        ck_set_error(err, 2, "the file carries data past its load image; the tree never does");
        return -1;
    }
    if (view.header.initial_ip != 0 && view.header.initial_ip != CK_COM_ORIGIN) {
        ck_set_error(err, 20, "entry offset is neither 0000 nor 0100; the tree exercises no other");
        return -1;
    }
    if (view.header.initial_ip == CK_COM_ORIGIN && view.image_size < CK_COM_ORIGIN) {
        ck_set_error(err, 2, "the load image is shorter than the 0x100 byte origin");
        return -1;
    }

    load = (ck_u8 *)malloc(view.image_size > 0 ? (unsigned int)view.image_size : 1u);
    if (!load) {
        ck_set_error(err, view.image_size, "out of memory");
        return -1;
    }
    if (view.image_size > 0) {
        memcpy(load, exe_bytes + view.image_offset, (unsigned int)view.image_size);
    }

    if (has_base_seg) {
        for (idx = 0; idx < view.header.relocation_count; ++idx) {
            ck_mz_reloc rel = ck_mz_get_reloc(exe_bytes, &view, idx);
            at = (ck_u32)rel.segment * 16u + (ck_u32)rel.offset;
            if (at + 1u >= view.image_size) {
                free(load);
                ck_set_error(err, at, "a fix-up points past the load image");
                return -1;
            }
            word = ck_read_u16_le(load + at);
            fixed = (ck_u16)(word + base_seg);
            ck_write_u16_le(load + at, fixed);
        }
    }

    if (view.header.initial_ip == 0) {
        out->kind = CK_EXE2BIN_PURE_BINARY;
        out->bytes = load;
        out->size = view.image_size;
        return 0;
    }

    out->kind = CK_EXE2BIN_COM_IMAGE;
    out->size = view.image_size - CK_COM_ORIGIN;
    if (out->size > 0) {
        memmove(load, load + CK_COM_ORIGIN, (unsigned int)out->size);
    }
    out->bytes = load;
    return 0;
}
