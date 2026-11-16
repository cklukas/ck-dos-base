/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/diskimg_core.h"
#include "dos_cli.h"

typedef struct dos_src_entry {
    const char *host_path;
} dos_src_entry;

static int dos_stream_read(void *ctx, ck_u32 offset, ck_u8 *buf, ck_u32 len) {
    const dos_src_entry *e = (const dos_src_entry *)ctx;
    int fd;
    ck_u32 got = 0UL;

    fd = open(e->host_path, 0);
    if (fd < 0) return -1;
    if (lseek(fd, (long)offset, 0) < 0L) {
        close(fd);
        return -1;
    }
    while (got < len) {
        unsigned int chunk = (len - got > 4096UL) ? 4096u : (unsigned int)(len - got);
        int n = read(fd, buf + got, chunk);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        got += (ck_u32)n;
    }
    close(fd);
    return 0;
}

static int dos_stream_write(void *ctx, const ck_u8 *buf, ck_u32 len) {
    int fd = *(int *)ctx;
    ck_u32 off = 0UL;
    while (off < len) {
        unsigned int chunk = (len - off > 16384UL) ? 16384u : (unsigned int)(len - off);
        int n = write(fd, buf + off, chunk);
        if (n <= 0) return -1;
        off += (ck_u32)n;
    }
    return 0;
}

static int dos_load_boot512(const char *path, ck_u8 out512[512]) {
    int fd = open(path, 0);
    long flen;
    long off = 0L;
    int n;

    if (fd < 0) return -1;
    flen = filelength(fd);
    if (flen >= 0x7E00L) {
        off = 0x7C00L;
    } else if (flen < 512L) {
        close(fd);
        return -1;
    }
    lseek(fd, off, 0);
    n = read(fd, out512, 512u);
    close(fd);
    return (n == 512) ? 0 : -1;
}

static long dos_probe_size(const char *path) {
    int fd = open(path, 0);
    long flen;
    if (fd < 0) return -1L;
    flen = filelength(fd);
    close(fd);
    return flen;
}

static int is_sys_root_file(const char *dos_path) {
    while (*dos_path == '/' || *dos_path == '\\') ++dos_path;
    if (strcmpi(dos_path, "IO.SYS") == 0 || strcmpi(dos_path, "MSDOS.SYS") == 0) {
        return 1;
    }
    return 0;
}

static int add_file_spec(const char *a,
                         dos_src_entry *srcs,
                         ck_img_file *files,
                         ck_zip_item *zitems,
                         char **owned_strs,
                         ck_usize *owned_cnt,
                         ck_usize *count) {
    const char *eq = strchr(a, '=');
    long sz;
    char *hpath;
    char *dpath_copy;
    const char *dpath;

    if (eq) {
        unsigned int hlen = (unsigned int)(eq - a);
        unsigned int dlen = (unsigned int)strlen(eq + 1);
        hpath = (char *)malloc(hlen + 1u);
        dpath_copy = (char *)malloc(dlen + 1u);
        if (!hpath || !dpath_copy) { free(hpath); free(dpath_copy); return -1; }
        memcpy(hpath, a, hlen);
        hpath[hlen] = '\0';
        memcpy(dpath_copy, eq + 1, dlen + 1u);
        dpath = dpath_copy;
        owned_strs[(*owned_cnt)++] = hpath;
        owned_strs[(*owned_cnt)++] = dpath_copy;
    } else {
        const char *slash = strrchr(a, '\\');
        const char *fslash = strrchr(a, '/');
        if (fslash && (!slash || fslash > slash)) slash = fslash;
        hpath = (char *)malloc((unsigned int)strlen(a) + 1u);
        if (!hpath) return -1;
        strcpy(hpath, a);
        owned_strs[(*owned_cnt)++] = hpath;
        dpath = slash ? (slash + 1) : hpath;
    }

    sz = dos_probe_size(hpath);
    if (sz < 0L) {
        printf("CKMKIMG: cannot open %s\n", hpath);
        return -1;
    }
    srcs[*count].host_path = hpath;
    files[*count].dos_path = dpath;
    files[*count].attr = is_sys_root_file(dpath) ? 0x07u : 0x20u;
    files[*count].size = (ck_u32)sz;
    files[*count].read_ctx = &srcs[*count];
    files[*count].read_fn = dos_stream_read;

    zitems[*count].archive_path = dpath;
    zitems[*count].size = (ck_u32)sz;
    zitems[*count].read_ctx = &srcs[*count];
    zitems[*count].read_fn = dos_stream_read;
    ++(*count);
    return 0;
}

int main(int argc, char **argv) {
    const char *mode;
    const char *out_path = NULL;
    const char *boot_path = NULL;
    const char *label = "CKDOS 4.00";
    ck_floppy_kind fkind = CK_FLOPPY_1440K;
    ck_u16 cyls = 64u, heads = 16u, spt = 63u;
    int vhd = 0;
    dos_src_entry *srcs = NULL;
    ck_img_file *files = NULL;
    ck_zip_item *zitems = NULL;
    char **owned_strs = NULL;
    ck_usize owned_cnt = 0u;
    ck_usize count = 0u;
    int cap = 256;
    int i;
    int out_fd = -1;
    ck_u8 boot512[512];
    const ck_u8 *boot_ptr = NULL;
    ck_error err;
    int rc = 1;

    if (argc < 3) {
        printf("Usage: CKMKIMG <floppy|hdd|zip> [-s 360k|720k|1200k|1440k] [-b boot] [-l label] -o out [src=dst | @rsp ...]\n");
        return 1;
    }
    mode = argv[1];

    srcs = (dos_src_entry *)calloc((unsigned int)cap, sizeof(dos_src_entry));
    files = (ck_img_file *)calloc((unsigned int)cap, sizeof(ck_img_file));
    zitems = (ck_zip_item *)calloc((unsigned int)cap, sizeof(ck_zip_item));
    owned_strs = (char **)calloc((unsigned int)(cap * 2), sizeof(char *));
    if (!srcs || !files || !zitems || !owned_strs) return 1;

    for (i = 2; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmpi(a, "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmpi(a, "-b") == 0 && i + 1 < argc) {
            boot_path = argv[++i];
        } else if (strcmpi(a, "-l") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else if (strcmpi(a, "-s") == 0 && i + 1 < argc) {
            const char *sv = argv[++i];
            if (strcmpi(sv, "360k") == 0) fkind = CK_FLOPPY_360K;
            else if (strcmpi(sv, "720k") == 0) fkind = CK_FLOPPY_720K;
            else if (strcmpi(sv, "1200k") == 0) fkind = CK_FLOPPY_1200K;
            else fkind = CK_FLOPPY_1440K;
        } else if (strcmpi(a, "--vhd") == 0) {
            vhd = 1;
        } else if (strcmpi(a, "-c") == 0 && i + 1 < argc) {
            cyls = (ck_u16)atoi(argv[++i]);
        } else if (a[0] == '@') {
            ck_u8 *rbuf = NULL;
            ck_u32 rsz = 0UL;
            ck_u32 p = 0UL;
            if (dos_read_all(a + 1, &rbuf, &rsz) != 0) goto done;
            while (p < rsz) {
                ck_u32 st;
                char line[160];
                ck_u32 l;
                while (p < rsz && (rbuf[p] == ' ' || rbuf[p] == '\t' || rbuf[p] == '\r' || rbuf[p] == '\n')) ++p;
                if (p >= rsz) break;
                st = p;
                while (p < rsz && rbuf[p] != '\r' && rbuf[p] != '\n') ++p;
                l = p - st;
                while (l > 0UL && (rbuf[st + l - 1UL] == ' ' || rbuf[st + l - 1UL] == '\t')) --l;
                if (l > 0UL && l < sizeof(line) && rbuf[st] != '#') {
                    memcpy(line, rbuf + st, (unsigned int)l);
                    line[l] = '\0';
                    if ((int)count >= cap ||
                        add_file_spec(line, srcs, files, zitems, owned_strs, &owned_cnt, &count) != 0) {
                        free(rbuf);
                        goto done;
                    }
                }
            }
            free(rbuf);
        } else {
            if ((int)count >= cap ||
                add_file_spec(a, srcs, files, zitems, owned_strs, &owned_cnt, &count) != 0) {
                goto done;
            }
        }
    }

    if (!out_path) {
        printf("CKMKIMG: missing -o output\n");
        goto done;
    }

    if (boot_path) {
        if (dos_load_boot512(boot_path, boot512) != 0) {
            printf("CKMKIMG: invalid boot sector %s\n", boot_path);
            goto done;
        }
        boot_ptr = boot512;
    }

    out_fd = creat(out_path, 0);
    if (out_fd < 0) {
        printf("CKMKIMG: cannot create %s\n", out_path);
        goto done;
    }

    memset(&err, 0, sizeof(err));
    if (strcmpi(mode, "floppy") == 0) {
        ck_fat_geom g;
        ck_diskimg_floppy_geom(fkind, label, &g);
        if (ck_diskimg_write_fat_volume(&g, boot_ptr, files, count, dos_stream_write, &out_fd, &err) != 0) {
            printf("CKMKIMG error: %s\n", err.message);
            goto done;
        }
    } else if (strcmpi(mode, "hdd") == 0) {
        if (ck_diskimg_write_hdd_image(cyls, heads, spt, label, boot_ptr, files, count, vhd,
                                       dos_stream_write, &out_fd, &err) != 0) {
            printf("CKMKIMG error: %s\n", err.message);
            goto done;
        }
    } else if (strcmpi(mode, "zip") == 0) {
        if (ck_diskimg_write_zip(zitems, count, dos_stream_write, &out_fd, &err) != 0) {
            printf("CKMKIMG error: %s\n", err.message);
            goto done;
        }
    } else {
        printf("CKMKIMG: unknown mode %s\n", mode);
        goto done;
    }

    rc = 0;

done:
    if (out_fd >= 0) close(out_fd);
    for (i = 0; i < (int)owned_cnt; ++i) free(owned_strs[i]);
    free(owned_strs);
    free(zitems);
    free(files);
    free(srcs);
    return rc;
}
