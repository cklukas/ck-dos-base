/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/compress_core.h"

static int buf_reserve(ck_byte_buf *b, ck_u32 extra) {
    ck_u32 needed = b->size + extra;
    ck_u32 new_cap;
    ck_u8 *np;
    if (needed <= b->cap) return 0;
    new_cap = b->cap ? b->cap : 64u;
    while (new_cap < needed) {
        if (new_cap > 0x7FFFFFFFu / 2u) {
            new_cap = needed;
            break;
        }
        new_cap *= 2u;
    }
    np = (ck_u8 *)realloc(b->data, (unsigned int)new_cap);
    if (!np) return -1;
    b->data = np;
    b->cap = new_cap;
    return 0;
}

static int buf_push(ck_byte_buf *b, ck_u8 v) {
    if (buf_reserve(b, 1u) != 0) return -1;
    b->data[b->size++] = v;
    return 0;
}

static int emit_escaped_byte(ck_byte_buf *out, ck_u8 value) {
    if (value == 0xFF) {
        if (buf_push(out, 0xFF) != 0) return -1;
        return buf_push(out, 0xFF);
    } else if (value == 0x0D) {
        if (buf_push(out, 0xFF) != 0) return -1;
        return buf_push(out, 0x01);
    } else if (value == 0x1A) {
        if (buf_push(out, 0xFF) != 0) return -1;
        return buf_push(out, 0x02);
    }
    return buf_push(out, value);
}

int ck_compress_rle(const ck_u8 *input,
                    ck_u32 input_size,
                    ck_u8 **out_bytes,
                    ck_u32 *out_size,
                    ck_error *err) {
    ck_byte_buf out;
    ck_u32 index = 0;
    ck_u8 value;
    ck_u32 run;

    if (!out_bytes || !out_size) {
        ck_set_error(err, 0, "null output");
        return -1;
    }
    *out_bytes = NULL;
    *out_size = 0;

    out.data = NULL;
    out.size = 0;
    out.cap = 0;
    if (input_size > 0 && buf_reserve(&out, input_size) != 0) {
        ck_set_error(err, input_size, "out of memory");
        return -1;
    }

    while (index < input_size) {
        value = input[index];
        run = 1;
        while (index + run < input_size && input[index + run] == value && run < 255u) {
            ++run;
        }
        if (run == 2u || run == 0x0Du || run == 0x1Au || run == 0xFFu) {
            --run;
        }
        if (run == 1u) {
            if (emit_escaped_byte(&out, value) != 0) goto oom;
        } else {
            if (buf_push(&out, 0xFF) != 0) goto oom;
            if (buf_push(&out, (ck_u8)run) != 0) goto oom;
            if (emit_escaped_byte(&out, value) != 0) goto oom;
        }
        index += run;
    }

    if (!out.data) {
        out.data = (ck_u8 *)malloc(1u);
        if (!out.data) goto oom;
    }
    *out_bytes = out.data;
    *out_size = out.size;
    return 0;

oom:
    free(out.data);
    ck_set_error(err, index, "out of memory");
    return -1;
}

static int read_escaped(const ck_u8 *input,
                        ck_u32 input_size,
                        ck_u32 at,
                        ck_u8 *out_val,
                        ck_u32 *out_consumed,
                        ck_error *err) {
    ck_u8 code;
    if (at >= input_size) {
        ck_set_error(err, at, "truncated RLE stream");
        return -1;
    }
    if (input[at] != 0xFF) {
        *out_val = input[at];
        *out_consumed = 1u;
        return 0;
    }
    if (at + 1u >= input_size) {
        ck_set_error(err, at, "truncated RLE escape sequence");
        return -1;
    }
    code = input[at + 1u];
    if (code == 0xFF) { *out_val = 0xFF; *out_consumed = 2u; return 0; }
    if (code == 0x01) { *out_val = 0x0D; *out_consumed = 2u; return 0; }
    if (code == 0x02) { *out_val = 0x1A; *out_consumed = 2u; return 0; }
    ck_set_error(err, at, "invalid RLE escaped byte code");
    return -1;
}

int ck_decompress_rle(const ck_u8 *input,
                      ck_u32 input_size,
                      ck_u8 **out_bytes,
                      ck_u32 *out_size,
                      ck_error *err) {
    ck_byte_buf out;
    ck_u32 index = 0;
    ck_u8 byte;
    ck_u8 tag;
    ck_u8 val;
    ck_u32 consumed;
    ck_u32 i;

    if (!out_bytes || !out_size) {
        ck_set_error(err, 0, "null output");
        return -1;
    }
    *out_bytes = NULL;
    *out_size = 0;

    out.data = NULL;
    out.size = 0;
    out.cap = 0;

    while (index < input_size) {
        byte = input[index];
        if (byte != 0xFF) {
            if (buf_push(&out, byte) != 0) goto oom;
            ++index;
            continue;
        }
        if (index + 1u >= input_size) {
            free(out.data);
            ck_set_error(err, index, "truncated RLE escape sequence");
            return -1;
        }
        tag = input[index + 1u];
        if (tag == 0xFF) {
            if (buf_push(&out, 0xFF) != 0) goto oom;
            index += 2u;
        } else if (tag == 0x01) {
            if (buf_push(&out, 0x0D) != 0) goto oom;
            index += 2u;
        } else if (tag == 0x02) {
            if (buf_push(&out, 0x1A) != 0) goto oom;
            index += 2u;
        } else if (tag >= 3 && tag != 0x0D && tag != 0x1A) {
            if (read_escaped(input, input_size, index + 2u, &val, &consumed, err) != 0) {
                free(out.data);
                return -1;
            }
            if (buf_reserve(&out, (ck_u32)tag) != 0) goto oom;
            for (i = 0; i < (ck_u32)tag; ++i) {
                out.data[out.size++] = val;
            }
            index += 2u + consumed;
        } else {
            free(out.data);
            ck_set_error(err, index, "invalid RLE tag");
            return -1;
        }
    }

    if (!out.data) {
        out.data = (ck_u8 *)malloc(1u);
        if (!out.data) goto oom;
    }
    *out_bytes = out.data;
    *out_size = out.size;
    return 0;

oom:
    free(out.data);
    ck_set_error(err, index, "out of memory");
    return -1;
}
