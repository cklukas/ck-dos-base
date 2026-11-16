/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_msg_db.h"

int sprintf(char *dst, const char *fmt, ...);

typedef struct emit_ctx {
    char base[64];
} emit_ctx;

static int emit_nosrv_file(void *ctx, const char *id, const char *data, ck_u32 size) {
    emit_ctx *ec = (emit_ctx *)ctx;
    char path[128];
    sprintf(path, "%s.CL%s", ec->base, id);
    if (dos_write_all(path, (const ck_u8 *)data, size) != 0) {
        return -1;
    }
    printf("Writing to %s.CL%s\n", ec->base, id);
    return 0;
}

static dos_msg_db db;

int main(int argc, char **argv) {
    ck_u8 *skl_data = NULL;
    ck_u32 skl_size = 0;
    emit_ctx ec;
    ck_error err;

    if (argc != 3) {
        printf("usage: cknosrv SKELETON MESSAGE-FILE\n");
        return 2;
    }
    if (dos_read_all(argv[1], &skl_data, &skl_size) != 0) {
        printf("cknosrv: cannot read %s\n", argv[1]);
        return 1;
    }
    if (dos_msg_db_open(&db, argv[2]) != 0) {
        free(skl_data);
        printf("cknosrv: cannot open %s\n", argv[2]);
        return 1;
    }
    extract_base_name(argv[1], ec.base, sizeof(ec.base));
    if (ck_nosrvbld_core((const char *)skl_data, skl_size, ec.base, dos_msg_lookup, &db,
                         emit_nosrv_file, &ec, &err) != 0) {
        free(skl_data);
        dos_msg_db_close(&db);
        printf("cknosrv: %s: %s\n", argv[1], err.message);
        return 1;
    }
    free(skl_data);
    dos_msg_db_close(&db);
    return 0;
}
