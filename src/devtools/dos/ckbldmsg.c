/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_msg_db.h"

int sprintf(char *dst, const char *fmt, ...);

typedef struct emit_ctx {
    char base[64];
} emit_ctx;

static int emit_buildmsg_file(void *ctx, const char *tag, const char *data, ck_u32 size) {
    emit_ctx *ec = (emit_ctx *)ctx;
    char path[128];
    sprintf(path, "%s.%s", ec->base, tag);
    if (dos_write_all(path, (const ck_u8 *)data, size) != 0) {
        return -1;
    }
    printf(" Created include file [%s.%s]\n", ec->base, tag);
    return 0;
}

static dos_msg_db db;

int main(int argc, char **argv) {
    char msg_path[128];
    ck_u8 *skl_data = NULL;
    ck_u32 skl_size = 0;
    emit_ctx ec;
    ck_error err;
    char util_buf[64];

    if (argc != 3) {
        printf("usage: ckbldmsg MESSAGE-BASE SKELETON\n");
        return 2;
    }
    sprintf(msg_path, "%s.MSG", argv[1]);
    if (dos_msg_db_open(&db, msg_path) != 0) {
        printf("ckbldmsg: cannot open %s\n", msg_path);
        return 1;
    }
    if (dos_read_all(argv[2], &skl_data, &skl_size) != 0) {
        dos_msg_db_close(&db);
        printf("ckbldmsg: cannot read %s\n", argv[2]);
        return 1;
    }
    extract_base_name(argv[2], ec.base, sizeof(ec.base));
    if (ck_buildmsg_core((const char *)skl_data, skl_size, dos_msg_lookup, &db,
                         emit_buildmsg_file, &ec, util_buf, sizeof(util_buf), &err) != 0) {
        free(skl_data);
        dos_msg_db_close(&db);
        printf("ckbldmsg: %s: %s\n", argv[2], err.message);
        return 1;
    }
    free(skl_data);
    dos_msg_db_close(&db);
    return 0;
}
