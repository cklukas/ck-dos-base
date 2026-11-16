/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dos_cli.h"
#include "dosbase/devtools/core/link_core.h"

int sprintf(char *dst, const char *fmt, ...);

static char *dup_n(const char *s, ck_usize n) {
    char *p = (char *)malloc((unsigned int)n + 1u);
    if (!p) return NULL;
    if (n > 0) memcpy(p, s, (unsigned int)n);
    p[n] = '\0';
    return p;
}

static char *dup_s(const char *s) {
    return dup_n(s, strlen(s));
}

static int has_ext(const char *path) {
    const char *p = path;
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    if (s1 && (!s2 || s1 > s2)) p = s1 + 1;
    else if (s2) p = s2 + 1;
    return strrchr(p, '.') != NULL;
}

static void replace_ext(const char *path, const char *ext, char *out, ck_usize cap) {
    const char *dot = strrchr(path, '.');
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    const char *slash = s1 > s2 ? s1 : s2;
    ck_usize base_len = (dot && (!slash || dot > slash)) ? (ck_usize)(dot - path) : strlen(path);
    if (base_len + strlen(ext) + 1u > cap) base_len = cap - strlen(ext) - 1u;
    memcpy(out, path, (unsigned int)base_len);
    strcpy(out + base_len, ext);
}

typedef struct str_list {
    char **items;
    ck_usize count;
    ck_usize cap;
} str_list;

static int sl_push(str_list *sl, const char *s) {
    char *d = dup_s(s);
    if (!d) return -1;
    if (sl->count == sl->cap) {
        ck_usize nc = sl->cap ? sl->cap * 2u : 8u;
        char **np = (char **)realloc(sl->items, (unsigned int)(nc * sizeof(char *)));
        if (!np) { free(d); return -1; }
        sl->items = np;
        sl->cap = nc;
    }
    sl->items[sl->count++] = d;
    return 0;
}

static void sl_free(str_list *sl) {
    ck_usize i;
    for (i = 0; i < sl->count; ++i) free(sl->items[i]);
    free(sl->items);
    memset(sl, 0, sizeof(*sl));
}

static int sl_contains_ci(const str_list *sl, const char *s) {
    ck_usize i;
    for (i = 0; i < sl->count; ++i) {
        if (strcmpi(sl->items[i], s) == 0) return 1;
    }
    return 0;
}

static void tok_plus_space(const char *field, ck_usize flen, str_list *out) {
    ck_usize i = 0;
    while (i < flen) {
        ck_usize st;
        while (i < flen && (field[i] == ' ' || field[i] == '\t' || field[i] == '+' ||
                            field[i] == '\r' || field[i] == '\n')) ++i;
        st = i;
        while (i < flen && field[i] != ' ' && field[i] != '\t' && field[i] != '+' &&
               field[i] != ',' && field[i] != '\r' && field[i] != '\n') ++i;
        if (i > st) {
            char *t = dup_n(field + st, i - st);
            if (t) { sl_push(out, t); free(t); }
        }
    }
}

typedef struct lib_sym {
    char *name;
    ck_u16 page;
} lib_sym;

typedef struct loaded_lib {
    ck_u8 *data;
    ck_u32 size;
    ck_u16 page_size;
    lib_sym *syms;
    ck_usize sym_count;
    ck_u16 *pulled_pages;
    ck_usize pulled_count;
} loaded_lib;

static int parse_lib(const char *path, loaded_lib *lib) {
    ck_u32 dict_off;
    ck_u16 blocks, b;
    ck_usize sym_cap = 0;

    memset(lib, 0, sizeof(*lib));
    if (dos_read_all(path, &lib->data, &lib->size) != 0) return -1;
    if (lib->size < 10u || lib->data[0] != 0xF0u) {
        free(lib->data);
        return -1;
    }
    lib->page_size = (ck_u16)(ck_read_u16_le(lib->data + 1u) + 3u);
    dict_off = ck_read_u32_le(lib->data + 3u);
    blocks = ck_read_u16_le(lib->data + 7u);
    if (dict_off + (ck_u32)blocks * 512u > lib->size) {
        free(lib->data);
        return -1;
    }
    for (b = 0; b < blocks; ++b) {
        const ck_u8 *blk = lib->data + dict_off + (ck_u32)b * 512u;
        ck_u16 k;
        for (k = 0; k < 37u; ++k) {
            ck_u8 woff = blk[k];
            ck_u16 off;
            ck_u8 slen;
            if (woff == 0) continue;
            off = (ck_u16)woff * 2u;
            if ((ck_u32)off + 1u >= 512u) continue;
            slen = blk[off];
            if ((ck_u32)off + 1u + slen + 2u > 512u) continue;
            if (slen > 0 && blk[off + slen] == '!') continue; /* skip module! entries */
            if (lib->sym_count == sym_cap) {
                ck_usize nc = sym_cap ? sym_cap * 2u : 32u;
                lib_sym *np = (lib_sym *)realloc(lib->syms, (unsigned int)(nc * sizeof(lib_sym)));
                if (!np) return -1;
                lib->syms = np;
                sym_cap = nc;
            }
            lib->syms[lib->sym_count].name = dup_n((const char *)(blk + off + 1u), slen);
            lib->syms[lib->sym_count].page = ck_read_u16_le(blk + off + 1u + slen);
            lib->sym_count++;
        }
    }
    return 0;
}

static void free_lib(loaded_lib *lib) {
    ck_usize i;
    free(lib->data);
    for (i = 0; i < lib->sym_count; ++i) free(lib->syms[i].name);
    free(lib->syms);
    free(lib->pulled_pages);
    memset(lib, 0, sizeof(*lib));
}

static int lib_is_pulled(const loaded_lib *lib, ck_u16 page) {
    ck_usize i;
    for (i = 0; i < lib->pulled_count; ++i) {
        if (lib->pulled_pages[i] == page) return 1;
    }
    return 0;
}

static int lib_mark_pulled(loaded_lib *lib, ck_u16 page) {
    ck_u16 *np = (ck_u16 *)realloc(lib->pulled_pages, (unsigned int)((lib->pulled_count + 1u) * sizeof(ck_u16)));
    if (!np) return -1;
    lib->pulled_pages = np;
    lib->pulled_pages[lib->pulled_count++] = page;
    return 0;
}

int main(int argc, char **argv) {
    str_list objs, libs, switches;
    char exe_name[128], map_name[128];
    char *flat = NULL;
    ck_usize flat_len = 0;
    int map_publics = 0, exepack = 0, dosseg_flag = 0;
    ck_u32 stack_override = 0;
    ck_link_module *mods = NULL;
    ck_usize mod_count = 0, mod_cap = 0;
    int *mod_lib = NULL;
    ck_u16 *mod_page = NULL;
    ck_usize *place_ord = NULL;
    loaded_lib *l_libs = NULL;
    ck_usize i;
    ck_error err;
    ck_u8 *exe_buf = NULL;
    ck_u32 exe_sz = 0;
    char *map_buf = NULL;
    ck_u32 map_sz = 0;
    int rc = 1;

    memset(&objs, 0, sizeof(objs));
    memset(&libs, 0, sizeof(libs));
    memset(&switches, 0, sizeof(switches));
    exe_name[0] = '\0';
    map_name[0] = '\0';

    if (argc < 2) {
        printf("usage: cklink @RESPONSE | cklink OBJ...,EXE,MAP,LIB...\n");
        return 2;
    }

    if (argc == 2 && argv[1][0] == '@') {
        ck_u8 *raw = NULL;
        ck_u32 rsz = 0;
        ck_u32 j;
        if (dos_read_all(argv[1] + 1, &raw, &rsz) != 0) {
            printf("cklink: cannot read %s\n", argv[1] + 1);
            return 1;
        }
        flat = (char *)malloc((unsigned int)rsz + 1u);
        if (!flat) { free(raw); return 1; }
        for (j = 0; j < rsz; ++j) {
            char c = (char)raw[j];
            if (c == '\x1A') break;
            if (c == '\r' || c == '\n') c = ' ';
            flat[flat_len++] = c;
        }
        flat[flat_len] = '\0';
        free(raw);
    } else {
        ck_usize pos = 0;
        flat = (char *)malloc(512u);
        if (!flat) return 1;
        flat[0] = '\0';
        for (i = 1; i < (ck_usize)argc; ++i) {
            ck_usize al = strlen(argv[i]);
            if (i > 1u && pos + 1u < 511u) flat[pos++] = ' ';
            if (pos + al < 511u) {
                memcpy(flat + pos, argv[i], (unsigned int)al);
                pos += al;
            }
        }
        flat[pos] = '\0';
        flat_len = pos;
    }

    /* Extract /SWITCH tokens */
    {
        ck_usize j;
        for (j = 0; j < flat_len; ++j) {
            if (flat[j] == '/') {
                ck_usize end = j + 1u;
                char *sw;
                while (end < flat_len && flat[end] != ',' && flat[end] != ';' &&
                       flat[end] != '+' && flat[end] != ' ' && flat[end] != '\t') ++end;
                sw = dup_n(flat + j, end - j);
                if (sw) { sl_push(&switches, sw); free(sw); }
                while (j < end) flat[j++] = ' ';
                if (j > 0) --j;
            }
        }
    }

    for (i = 0; i < switches.count; ++i) {
        const char *sw = switches.items[i];
        if (strcmpi(sw, "/M") == 0 || strcmpi(sw, "/MAP") == 0) map_publics = 1;
        else if (strcmpi(sw, "/E") == 0 || strcmpi(sw, "/EXEPACK") == 0) exepack = 1;
        else if (strcmpi(sw, "/DOSSEG") == 0) dosseg_flag = 1;
        else if (strlen(sw) > 7u && (sw[1] == 'S' || sw[1] == 's')) {
            const char *col = strchr(sw, ':');
            if (col) {
                if (col[1] == '0' && (col[2] == 'x' || col[2] == 'X')) {
                    const char *p = col + 3;
                    ck_u32 v = 0;
                    while (*p) {
                        char c = *p++;
                        v *= 16u;
                        if (c >= '0' && c <= '9') v += (ck_u32)(c - '0');
                        else if (c >= 'a' && c <= 'f') v += (ck_u32)(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') v += (ck_u32)(c - 'A' + 10);
                    }
                    stack_override = v;
                } else {
                    stack_override = (ck_u32)atol(col + 1);
                }
            }
        }
    }

    /* Split fields by ',' up to ';' */
    {
        char *semi = strchr(flat, ';');
        ck_usize end = semi ? (ck_usize)(semi - flat) : flat_len;
        const char *fstart[4];
        ck_usize flen[4];
        int fcnt = 0;
        ck_usize p = 0, st = 0;
        for (p = 0; p <= end && fcnt < 4; ++p) {
            if (p == end || flat[p] == ',') {
                fstart[fcnt] = flat + st;
                flen[fcnt] = (ck_usize)(p - st);
                fcnt++;
                st = p + 1u;
            }
        }
        if (fcnt >= 1) tok_plus_space(fstart[0], flen[0], &objs);
        if (fcnt >= 2) {
            str_list tmp;
            memset(&tmp, 0, sizeof(tmp));
            tok_plus_space(fstart[1], flen[1], &tmp);
            if (tmp.count > 0) {
                strcpy(exe_name, tmp.items[0]);
                if (!has_ext(exe_name)) strcat(exe_name, ".EXE");
            }
            sl_free(&tmp);
        }
        if (fcnt >= 3) {
            str_list tmp;
            memset(&tmp, 0, sizeof(tmp));
            tok_plus_space(fstart[2], flen[2], &tmp);
            if (tmp.count > 0 && strcmpi(tmp.items[0], "NUL") != 0) {
                strcpy(map_name, tmp.items[0]);
                if (!has_ext(map_name)) strcat(map_name, ".MAP");
            }
            sl_free(&tmp);
        }
        if (fcnt >= 4) tok_plus_space(fstart[3], flen[3], &libs);
    }
    free(flat);
    flat = NULL;

    if (objs.count == 0) {
        printf("cklink: no object files\n");
        goto done;
    }
    if (!exe_name[0]) replace_ext(objs.items[0], ".EXE", exe_name, sizeof(exe_name));

    for (i = 0; i < objs.count; ++i) {
        char path[128];
        ck_u8 *obuf = NULL;
        ck_u32 osz = 0;
        ck_omf_record *recs = NULL;
        ck_usize rcnt = 0;
        strcpy(path, objs.items[i]);
        if (!has_ext(path)) strcat(path, ".OBJ");
        if (dos_read_all(path, &obuf, &osz) != 0) {
            printf("cklink: cannot read %s\n", path);
            goto done;
        }
        if (ck_omf_parse_records(obuf, osz, 0, 1, &recs, &rcnt, &err) != 0) {
            free(obuf);
            printf("cklink: %s: %s\n", path, err.message);
            goto done;
        }
        free(obuf);
        if (mod_count == mod_cap) {
            ck_usize nc = mod_cap ? mod_cap * 2u : 8u;
            ck_link_module *np = (ck_link_module *)realloc(mods, (unsigned int)(nc * sizeof(ck_link_module)));
            int *nl = (int *)realloc(mod_lib, (unsigned int)(nc * sizeof(int)));
            ck_u16 *npg = (ck_u16 *)realloc(mod_page, (unsigned int)(nc * sizeof(ck_u16)));
            if (!np || !nl || !npg) { ck_omf_free_records(recs, rcnt); goto done; }
            mods = np; mod_lib = nl; mod_page = npg;
            mod_cap = nc;
        }
        if (ck_omf_parse_module(recs, rcnt, &mods[mod_count], &err) != 0) {
            ck_omf_free_records(recs, rcnt);
            printf("cklink: %s: %s\n", path, err.message);
            goto done;
        }
        ck_omf_free_records(recs, rcnt);
        if (dosseg_flag) mods[mod_count].dosseg = 1;
        mod_lib[mod_count] = -1;
        mod_page[mod_count] = (ck_u16)mod_count;
        mod_count++;
    }

    if (libs.count > 0) {
        str_list defined, unresolved;
        int progressed = 1;
        memset(&defined, 0, sizeof(defined));
        memset(&unresolved, 0, sizeof(unresolved));
        l_libs = (loaded_lib *)calloc((unsigned int)libs.count, sizeof(loaded_lib));
        if (!l_libs) goto done;
        for (i = 0; i < libs.count; ++i) {
            char path[128];
            strcpy(path, libs.items[i]);
            if (!has_ext(path)) strcat(path, ".LIB");
            if (parse_lib(path, &l_libs[i]) != 0) {
                printf("cklink: cannot read %s\n", path);
                sl_free(&defined); sl_free(&unresolved);
                goto done;
            }
        }
        for (i = 0; i < mod_count; ++i) {
            ck_usize k;
            for (k = 0; k < mods[i].pub_count; ++k) {
                if (!mods[i].publics[k].local && !sl_contains_ci(&defined, mods[i].publics[k].name)) {
                    sl_push(&defined, mods[i].publics[k].name);
                }
            }
            for (k = 0; k < mods[i].ext_count; ++k) {
                if (!mods[i].externals[k].local && !sl_contains_ci(&unresolved, mods[i].externals[k].name)) {
                    sl_push(&unresolved, mods[i].externals[k].name);
                }
            }
        }
        while (progressed) {
            ck_usize li;
            progressed = 0;
            for (li = 0; li < libs.count; ++li) {
                ck_usize si;
                for (si = 0; si < l_libs[li].sym_count; ++si) {
                    const char *sname = l_libs[li].syms[si].name;
                    ck_u16 page = l_libs[li].syms[si].page;
                    if (lib_is_pulled(&l_libs[li], page)) continue;
                    if (sl_contains_ci(&defined, sname)) continue;
                    if (!sl_contains_ci(&unresolved, sname)) continue;
                    {
                        ck_u32 off = (ck_u32)page * (ck_u32)l_libs[li].page_size;
                        ck_omf_record *recs = NULL;
                        ck_usize rcnt = 0, k;
                        lib_mark_pulled(&l_libs[li], page);
                        if (ck_omf_parse_records(l_libs[li].data, l_libs[li].size, off, 1, &recs, &rcnt, &err) != 0) {
                            sl_free(&defined); sl_free(&unresolved);
                            printf("cklink: lib member parse error: %s\n", err.message);
                            goto done;
                        }
                        if (mod_count == mod_cap) {
                            ck_usize nc = mod_cap ? mod_cap * 2u : 8u;
                            ck_link_module *np = (ck_link_module *)realloc(mods, (unsigned int)(nc * sizeof(ck_link_module)));
                            int *nl = (int *)realloc(mod_lib, (unsigned int)(nc * sizeof(int)));
                            ck_u16 *npg = (ck_u16 *)realloc(mod_page, (unsigned int)(nc * sizeof(ck_u16)));
                            if (!np || !nl || !npg) { ck_omf_free_records(recs, rcnt); sl_free(&defined); sl_free(&unresolved); goto done; }
                            mods = np; mod_lib = nl; mod_page = npg;
                            mod_cap = nc;
                        }
                        if (ck_omf_parse_module(recs, rcnt, &mods[mod_count], &err) != 0) {
                            ck_omf_free_records(recs, rcnt); sl_free(&defined); sl_free(&unresolved);
                            printf("cklink: lib member module error: %s\n", err.message);
                            goto done;
                        }
                        ck_omf_free_records(recs, rcnt);
                        for (k = 0; k < mods[mod_count].pub_count; ++k) {
                            if (!mods[mod_count].publics[k].local &&
                                !sl_contains_ci(&defined, mods[mod_count].publics[k].name)) {
                                sl_push(&defined, mods[mod_count].publics[k].name);
                            }
                        }
                        for (k = 0; k < mods[mod_count].ext_count; ++k) {
                            if (!mods[mod_count].externals[k].local &&
                                !sl_contains_ci(&unresolved, mods[mod_count].externals[k].name)) {
                                sl_push(&unresolved, mods[mod_count].externals[k].name);
                            }
                        }
                        if (dosseg_flag) mods[mod_count].dosseg = 1;
                        mod_lib[mod_count] = (int)li;
                        mod_page[mod_count] = page;
                        mod_count++;
                        progressed = 1;
                    }
                }
            }
        }
        sl_free(&defined);
        sl_free(&unresolved);
    }

    place_ord = (ck_usize *)malloc((unsigned int)(mod_count * sizeof(ck_usize)));
    if (!place_ord) goto done;
    for (i = 0; i < mod_count; ++i) place_ord[i] = i;
    for (i = 1; i < mod_count; ++i) {
        ck_usize k = place_ord[i];
        ck_usize j = i;
        while (j > 0) {
            ck_usize p = place_ord[j - 1u];
            if (mod_lib[p] < mod_lib[k]) break;
            if (mod_lib[p] == mod_lib[k] && mod_page[p] <= mod_page[k]) break;
            place_ord[j] = p;
            --j;
        }
        place_ord[j] = k;
    }

    if (ck_link_modules(mods, mod_count, map_publics, stack_override, exepack,
                        place_ord, mod_count, &exe_buf, &exe_sz,
                        map_name[0] ? &map_buf : NULL, map_name[0] ? &map_sz : NULL, &err) != 0) {
        printf("cklink: %s\n", err.message);
        goto done;
    }
    if (dos_write_all(exe_name, exe_buf, exe_sz) != 0) {
        printf("cklink: cannot write %s\n", exe_name);
        goto done;
    }
    if (map_name[0] && map_buf) {
        dos_write_all(map_name, (const ck_u8 *)map_buf, map_sz);
    }
    rc = 0;

done:
    free(exe_buf);
    free(map_buf);
    free(place_ord);
    free(mod_lib);
    free(mod_page);
    if (l_libs) {
        for (i = 0; i < libs.count; ++i) free_lib(&l_libs[i]);
        free(l_libs);
    }
    for (i = 0; i < mod_count; ++i) ck_link_module_free(&mods[i]);
    free(mods);
    sl_free(&objs);
    sl_free(&libs);
    sl_free(&switches);
    return rc;
}
