/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/convert_core.h"
#include "dosbase/devtools/core/mz_core.h"

#define CK_PREFIX_SIZE 16u
#define CK_STUB_SIZE 123u

static const ck_u8 k_convert_stub[CK_STUB_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE8, 0x00, 0x00,
    0x5B,
    0x50,
    0x8C, 0xC0,
    0x05, 0x10, 0x00,
    0x8B, 0x0E, 0x1E, 0x01,
    0x03, 0xC8,
    0x89, 0x4F, 0xFB,
    0x8B, 0x0E, 0x26, 0x01,
    0x03, 0xC8,
    0x89, 0x4F, 0xF7,
    0x8B, 0x0E, 0x20, 0x01,
    0x89, 0x4F, 0xF9,
    0x8B, 0x0E, 0x24, 0x01,
    0x89, 0x4F, 0xF5,
    0x8B, 0x3E, 0x28, 0x01,
    0x8B, 0x16, 0x18, 0x01,
    0xB1, 0x04,
    0xD3, 0xE2,
    0x8B, 0x0E, 0x16, 0x01,
    0xE3, 0x1A,
    0x26, 0xC5, 0xB5, 0x10, 0x01,
    0x83, 0xC7, 0x04,
    0x8C, 0xDD,
    0x26, 0x03, 0x2E, 0x18, 0x01,
    0x83, 0xC5, 0x01,
    0x03, 0xE8,
    0x8E, 0xDD,
    0x01, 0x04,
    0xE2, 0xE6,
    0x0E,
    0x1F,
    0xBF, 0x00, 0x01,
    0x8B, 0xF2,
    0x81, 0xC6, 0x10, 0x01,
    0x8B, 0xCB,
    0x2B, 0xCE,
    0xF3, 0xA4,
    0x58,
    0xFA,
    0x8E, 0x57, 0xFB,
    0x8B, 0x67, 0xF9,
    0xFB,
    0xFF, 0x6F, 0xF5
};

static const ck_u8 k_convert_tag[13] = {
    'C', 'o', 'n', 'v', 'e', 'r', 't', 'e', 'd', 0, 0, 0, 0
};

int ck_convert_exe_to_com(const ck_u8 *exe_bytes,
                          ck_u32 exe_size,
                          ck_u8 **out_bytes,
                          ck_u32 *out_size,
                          ck_error *err) {
    ck_mz_view view;
    ck_u32 total_size;
    ck_u16 rel16;
    ck_u8 *buf;

    if (!out_bytes || !out_size) {
        ck_set_error(err, 0, "null output");
        return -1;
    }
    *out_bytes = NULL;
    *out_size = 0;

    if (ck_mz_parse_view(exe_bytes, exe_size, &view, err) != 0) {
        return -1;
    }

    total_size = (ck_u32)CK_PREFIX_SIZE + exe_size + (ck_u32)CK_STUB_SIZE;
    if (total_size >= (ck_u32)CK_CONVERT_MAX_COM_BYTES) {
        ck_set_error(err, exe_size, "converted .COM image exceeds 64 KiB limit");
        return -1;
    }

    buf = (ck_u8 *)malloc((unsigned int)total_size);
    if (!buf) {
        ck_set_error(err, total_size, "out of memory");
        return -1;
    }

    rel16 = (ck_u16)(CK_PREFIX_SIZE + exe_size + 5u);
    buf[0] = 0xE9;
    buf[1] = (ck_u8)(rel16 & 0xFF);
    buf[2] = (ck_u8)((rel16 >> 8) & 0xFF);
    memcpy(buf + 3, k_convert_tag, 13);
    memcpy(buf + CK_PREFIX_SIZE, exe_bytes, (unsigned int)exe_size);
    memcpy(buf + CK_PREFIX_SIZE + exe_size, k_convert_stub, CK_STUB_SIZE);

    *out_bytes = buf;
    *out_size = total_size;
    return 0;
}
