/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_cli.h"
#include "dosbase/devtools/core/nmake_core.h"

int sprintf(char *dst, const char *fmt, ...);
int _dos_exec(const char *prog, const char *tail);

struct WORDREGS {
    unsigned int ax, bx, cx, dx, si, di, cflag;
};
union REGS {
    struct WORDREGS x;
};
int intdos(union REGS *inregs, union REGS *outregs);

static int dos_stat_fn(void *ctx, const char *path, ck_u32 *out_mtime) {
    int fd = open(path, 0);
    union REGS inr, outr;
    (void)ctx;
    if (fd < 0) return 0;
    inr.x.ax = 0x5700u;
    inr.x.bx = (unsigned int)fd;
    intdos(&inr, &outr);
    close(fd);
    if (outr.x.cflag) {
        *out_mtime = 1u;
    } else {
        *out_mtime = ((ck_u32)outr.x.dx << 16) | (ck_u32)outr.x.cx;
        if (*out_mtime == 0) *out_mtime = 1u;
    }
    return 1;
}

static int dos_exec_fn(void *ctx, const char *cmd, int silent) {
    char tail[140];
    (void)ctx;
    (void)silent;
    if (strlen(cmd) + 4u >= sizeof(tail)) {
        printf("cknmake: command too long\n");
        return 1;
    }
    sprintf(tail, "/C %s", cmd);
    return _dos_exec("C:\\COMMAND.COM", tail);
}

int main(int argc, char **argv) {
    const char *makefile = NULL;
    const char *targets[16];
    const char *macros[16];
    ck_nmake_opts opts;
    ck_u8 *mf_data = NULL;
    ck_u32 mf_size = 0;
    ck_error err;
    int i;
    int rc = 1;

    memset(&opts, 0, sizeof(opts));
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if ((a[0] == '-' || a[0] == '/') && (a[1] == 'f' || a[1] == 'F')) {
            if (a[2] != '\0') makefile = a + 2;
            else if (i + 1 < argc) makefile = argv[++i];
        } else if ((a[0] == '-' || a[0] == '/') && (a[1] == 'a' || a[1] == 'A') && a[2] == '\0') {
            opts.force_all = 1;
        } else if ((a[0] == '-' || a[0] == '/') && (a[1] == 'n' || a[1] == 'N') && a[2] == '\0') {
            opts.dry_run = 1;
        } else if ((a[0] == '-' || a[0] == '/') && (a[1] == 's' || a[1] == 'S') && a[2] == '\0') {
            /* silent flag accepted */
        } else if (strchr(a, '=') != NULL) {
            if (opts.macro_def_count < 16u) macros[opts.macro_def_count++] = a;
        } else {
            if (opts.target_count < 16u) targets[opts.target_count++] = a;
        }
    }

    if (!makefile) makefile = "MAKEFILE";
    if (dos_read_all(makefile, &mf_data, &mf_size) != 0) {
        printf("cknmake: cannot read %s\n", makefile);
        return 1;
    }

    opts.makefile_text = (const char *)mf_data;
    opts.makefile_size = mf_size;
    opts.targets = targets;
    opts.macro_defs = macros;
    opts.stat_fn = dos_stat_fn;
    opts.exec_fn = dos_exec_fn;

    if (ck_nmake_run(&opts, &err) != 0) {
        printf("cknmake: %s\n", err.message);
        goto done;
    }
    rc = 0;

done:
    free(mf_data);
    return rc;
}
