/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#include "dosbase/devtools/core/nmake_core.h"

#if defined(M_I86) && !defined(_MSC_VER)
int sprintf(char *dst, const char *fmt, ...);
int printf(const char *fmt, ...);
#else
#include <stdio.h>
#endif

typedef struct nm_macro {
    char *name;
    char *value;
} nm_macro;

typedef struct nm_rule {
    char *target;
    char **deps;
    ck_usize dep_count;
    char **cmds;
    ck_usize cmd_count;
    int is_inference;
} nm_rule;

typedef struct nm_ctx {
    nm_macro *macros;
    ck_usize macro_count;
    ck_usize macro_cap;
    nm_rule *rules;
    ck_usize rule_count;
    ck_usize rule_cap;
    char **built;
    ck_usize built_count;
    ck_usize built_cap;
    const ck_nmake_opts *opts;
} nm_ctx;

static char *nm_strdup_n(const char *s, ck_usize n) {
    char *p = (char *)malloc((unsigned int)n + 1u);
    if (!p) return NULL;
    if (n > 0) memcpy(p, s, (unsigned int)n);
    p[n] = '\0';
    return p;
}

static char *nm_strdup(const char *s) {
    return nm_strdup_n(s ? s : "", s ? strlen(s) : 0u);
}

static int nm_set_macro(nm_ctx *ctx, const char *name, const char *val) {
    ck_usize i;
    for (i = 0; i < ctx->macro_count; ++i) {
        if (strcmpi(ctx->macros[i].name, name) == 0) {
            char *nv = nm_strdup(val);
            if (!nv) return -1;
            free(ctx->macros[i].value);
            ctx->macros[i].value = nv;
            return 0;
        }
    }
    if (ctx->macro_count == ctx->macro_cap) {
        ck_usize nc = ctx->macro_cap ? ctx->macro_cap * 2u : 16u;
        nm_macro *np = (nm_macro *)realloc(ctx->macros, (unsigned int)(nc * sizeof(nm_macro)));
        if (!np) return -1;
        ctx->macros = np;
        ctx->macro_cap = nc;
    }
    ctx->macros[ctx->macro_count].name = nm_strdup(name);
    ctx->macros[ctx->macro_count].value = nm_strdup(val);
    if (!ctx->macros[ctx->macro_count].name || !ctx->macros[ctx->macro_count].value) return -1;
    ctx->macro_count++;
    return 0;
}

static const char *nm_get_macro(const nm_ctx *ctx, const char *name) {
    ck_usize i;
    for (i = 0; i < ctx->macro_count; ++i) {
        if (strcmpi(ctx->macros[i].name, name) == 0) return ctx->macros[i].value;
    }
    return "";
}

static int nm_expand(const nm_ctx *ctx,
                     const char *src,
                     const char *at_target,
                     const char *first_dep,
                     const char *stem,
                     const char *changed_deps,
                     char **out,
                     int depth) {
    char *buf = NULL;
    ck_usize len = 0, cap = 0;
    ck_usize i = 0;
    ck_usize slen = strlen(src);

    if (depth > 16) {
        *out = nm_strdup(src);
        return *out ? 0 : -1;
    }

    cap = slen + 32u;
    buf = (char *)malloc((unsigned int)cap);
    if (!buf) return -1;

    while (i < slen) {
        if (src[i] == '$' && i + 1u < slen) {
            const char *rep = NULL;
            char *dyn_rep = NULL;
            char next = src[i + 1u];
            if (next == '$') {
                rep = "$";
                i += 2u;
            } else if (next == '@') {
                rep = at_target ? at_target : "";
                i += 2u;
            } else if (next == '<') {
                rep = first_dep ? first_dep : "";
                i += 2u;
            } else if (next == '*') {
                rep = stem ? stem : "";
                i += 2u;
            } else if (next == '?') {
                rep = changed_deps ? changed_deps : "";
                i += 2u;
            } else if (next == '(' || next == '{') {
                char close = (next == '(') ? ')' : '}';
                ck_usize st = i + 2u;
                ck_usize en = st;
                while (en < slen && src[en] != close) ++en;
                if (en < slen) {
                    char *mname = nm_strdup_n(src + st, en - st);
                    if (!mname) { free(buf); return -1; }
                    if (strcmp(mname, "@") == 0) rep = at_target ? at_target : "";
                    else if (strcmp(mname, "<") == 0) rep = first_dep ? first_dep : "";
                    else if (strcmp(mname, "*") == 0) rep = stem ? stem : "";
                    else if (strcmp(mname, "?") == 0) rep = changed_deps ? changed_deps : "";
                    else {
                        const char *raw = nm_get_macro(ctx, mname);
                        if (nm_expand(ctx, raw, at_target, first_dep, stem, changed_deps, &dyn_rep, depth + 1) != 0) {
                            free(mname); free(buf); return -1;
                        }
                        rep = dyn_rep;
                    }
                    free(mname);
                    i = en + 1u;
                } else {
                    rep = "$";
                    i += 1u;
                }
            } else {
                char mname[2];
                const char *raw;
                mname[0] = next; mname[1] = '\0';
                raw = nm_get_macro(ctx, mname);
                if (nm_expand(ctx, raw, at_target, first_dep, stem, changed_deps, &dyn_rep, depth + 1) != 0) {
                    free(buf); return -1;
                }
                rep = dyn_rep;
                i += 2u;
            }
            {
                ck_usize rlen = strlen(rep);
                if (len + rlen + 1u > cap) {
                    ck_usize nc = cap * 2u;
                    char *np;
                    while (nc < len + rlen + 1u) nc *= 2u;
                    np = (char *)realloc(buf, (unsigned int)nc);
                    if (!np) { free(dyn_rep); free(buf); return -1; }
                    buf = np; cap = nc;
                }
                if (rlen > 0) memcpy(buf + len, rep, (unsigned int)rlen);
                len += rlen;
                free(dyn_rep);
            }
            continue;
        }
        if (len + 2u > cap) {
            ck_usize nc = cap * 2u;
            char *np = (char *)realloc(buf, (unsigned int)nc);
            if (!np) { free(buf); return -1; }
            buf = np; cap = nc;
        }
        buf[len++] = src[i++];
    }
    buf[len] = '\0';
    *out = buf;
    return 0;
}

static char *trim_ws(char *s) {
    ck_usize n;
    while (*s == ' ' || *s == '\t') ++s;
    n = strlen(s);
    while (n > 0 && (s[n - 1u] == ' ' || s[n - 1u] == '\t' || s[n - 1u] == '\r')) s[--n] = '\0';
    return s;
}

static int split_ws(const char *s, char ***out_items, ck_usize *out_count) {
    ck_usize i = 0, len = strlen(s), cap = 0;
    *out_items = NULL;
    *out_count = 0;
    while (i < len) {
        ck_usize st;
        while (i < len && (s[i] == ' ' || s[i] == '\t')) ++i;
        st = i;
        while (i < len && s[i] != ' ' && s[i] != '\t') ++i;
        if (i > st) {
            char *tok = nm_strdup_n(s + st, i - st);
            if (!tok) return -1;
            if (*out_count == cap) {
                ck_usize nc = cap ? cap * 2u : 8u;
                char **np = (char **)realloc(*out_items, (unsigned int)(nc * sizeof(char *)));
                if (!np) { free(tok); return -1; }
                *out_items = np;
                cap = nc;
            }
            (*out_items)[(*out_count)++] = tok;
        }
    }
    return 0;
}

static int is_inference_name(const char *t) {
    const char *d1, *d2;
    if (t[0] != '.') return 0;
    d1 = strchr(t + 1, '.');
    if (!d1) return 0;
    d2 = strchr(d1 + 1, '.');
    return d2 == NULL;
}

static nm_rule *find_rule(nm_ctx *ctx, const char *target) {
    ck_usize i;
    for (i = 0; i < ctx->rule_count; ++i) {
        if (strcmpi(ctx->rules[i].target, target) == 0) return &ctx->rules[i];
    }
    return NULL;
}

static int already_built(const nm_ctx *ctx, const char *target) {
    ck_usize i;
    for (i = 0; i < ctx->built_count; ++i) {
        if (strcmpi(ctx->built[i], target) == 0) return 1;
    }
    return 0;
}

static int mark_built(nm_ctx *ctx, const char *target) {
    char *dup = nm_strdup(target);
    if (!dup) return -1;
    if (ctx->built_count == ctx->built_cap) {
        ck_usize nc = ctx->built_cap ? ctx->built_cap * 2u : 16u;
        char **np = (char **)realloc(ctx->built, (unsigned int)(nc * sizeof(char *)));
        if (!np) { free(dup); return -1; }
        ctx->built = np;
        ctx->built_cap = nc;
    }
    ctx->built[ctx->built_count++] = dup;
    return 0;
}

static void target_stem(const char *target, char *out, ck_usize cap) {
    const char *dot = strrchr(target, '.');
    const char *s1 = strrchr(target, '\\');
    const char *s2 = strrchr(target, '/');
    const char *sl = s1 > s2 ? s1 : s2;
    ck_usize n = (dot && (!sl || dot > sl)) ? (ck_usize)(dot - target) : strlen(target);
    if (n >= cap) n = cap - 1u;
    if (n > 0) memcpy(out, target, (unsigned int)n);
    out[n] = '\0';
}

static int build_target(nm_ctx *ctx, const char *target, ck_error *err) {
    nm_rule *r;
    ck_usize i;
    ck_u32 t_mtime = 0;
    int t_exists = 0;
    int need = ctx->opts->force_all ? 1 : 0;
    const char *first_dep = NULL;
    char *changed = NULL;
    ck_usize ch_len = 0, ch_cap = 0;
    char stem[128];
    nm_rule *inf_rule = NULL;
    char inf_dep[128];

    if (already_built(ctx, target)) return 0;

    r = find_rule(ctx, target);
    t_exists = ctx->opts->stat_fn(ctx->opts->cb_ctx, target, &t_mtime);
    if (!t_exists) need = 1;

    if (r) {
        if (r->dep_count > 0) first_dep = r->deps[0];
        for (i = 0; i < r->dep_count; ++i) {
            ck_u32 d_mtime = 0;
            int d_exists;
            if (build_target(ctx, r->deps[i], err) != 0) return -1;
            d_exists = ctx->opts->stat_fn(ctx->opts->cb_ctx, r->deps[i], &d_mtime);
            if (!t_exists || !d_exists || d_mtime > t_mtime) {
                ck_usize dl = strlen(r->deps[i]);
                need = 1;
                if (ch_len + dl + 2u > ch_cap) {
                    ck_usize nc = ch_cap ? ch_cap * 2u : 128u;
                    char *np;
                    while (nc < ch_len + dl + 2u) nc *= 2u;
                    np = (char *)realloc(changed, (unsigned int)nc);
                    if (!np) { free(changed); ck_set_error(err, 0, "out of memory"); return -1; }
                    changed = np; ch_cap = nc;
                }
                if (ch_len > 0) changed[ch_len++] = ' ';
                memcpy(changed + ch_len, r->deps[i], (unsigned int)dl);
                ch_len += dl;
                changed[ch_len] = '\0';
            }
        }
        if (r->cmd_count == 0 && r->dep_count == 0) need = 0;
    }

    target_stem(target, stem, sizeof(stem));
    if ((!r || r->cmd_count == 0)) {
        const char *dot = strrchr(target, '.');
        if (dot) {
            for (i = 0; i < ctx->rule_count; ++i) {
                nm_rule *ir = &ctx->rules[i];
                const char *d2;
                if (!ir->is_inference) continue;
                d2 = strchr(ir->target + 1, '.');
                if (d2 && strcmpi(d2, dot) == 0) {
                    ck_usize ext1_len = (ck_usize)(d2 - ir->target);
                    char cand[128];
                    ck_u32 d_mtime = 0;
                    if (strlen(stem) + ext1_len + 1u < sizeof(cand)) {
                        strcpy(cand, stem);
                        memcpy(cand + strlen(stem), ir->target, (unsigned int)ext1_len);
                        cand[strlen(stem) + ext1_len] = '\0';
                        if (ctx->opts->stat_fn(ctx->opts->cb_ctx, cand, &d_mtime)) {
                            inf_rule = ir;
                            strcpy(inf_dep, cand);
                            first_dep = inf_dep;
                            if (!t_exists || d_mtime > t_mtime) need = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    {
        nm_rule *exec_r = (r && r->cmd_count > 0) ? r : inf_rule;
        if (need && exec_r) {
            for (i = 0; i < exec_r->cmd_count; ++i) {
                const char *raw_cmd = exec_r->cmds[i];
                int silent = 0, ignore_err = 0;
                char *exp = NULL;
                int rc;
                while (*raw_cmd == '@' || *raw_cmd == '-') {
                    if (*raw_cmd == '@') silent = 1;
                    else ignore_err = 1;
                    ++raw_cmd;
                }
                while (*raw_cmd == ' ' || *raw_cmd == '\t') ++raw_cmd;
                if (nm_expand(ctx, raw_cmd, target, first_dep, stem, changed ? changed : "", &exp, 0) != 0) {
                    free(changed);
                    ck_set_error(err, 0, "out of memory");
                    return -1;
                }
                if (!silent) printf("\t%s\n", exp);
                if (!ctx->opts->dry_run) {
                    rc = ctx->opts->exec_fn(ctx->opts->cb_ctx, exp, silent);
                    if (rc != 0 && !ignore_err) {
                        char msg[160];
                        sprintf(msg, "command failed (%d): %.100s", rc, exp);
                        free(exp);
                        free(changed);
                        ck_set_error(err, 0, msg);
                        return -1;
                    }
                }
                free(exp);
            }
        }
    }

    free(changed);
    return mark_built(ctx, target);
}

int ck_nmake_run(const ck_nmake_opts *opts, ck_error *err) {
    nm_ctx ctx;
    char **lines = NULL;
    ck_usize line_count = 0, line_cap = 0;
    ck_u32 pos = 0;
    char *cur = NULL;
    ck_usize cur_len = 0, cur_cap = 0;
    ck_usize i, j;
    nm_rule *cur_rule = NULL;
    int cond_stack[16];
    int cond_top = 0;
    const char *first_target = NULL;
    int rc = 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.opts = opts;
    for (i = 0; i < opts->macro_def_count; ++i) {
        const char *eq = strchr(opts->macro_defs[i], '=');
        if (eq) {
            char *nm = nm_strdup_n(opts->macro_defs[i], (ck_usize)(eq - opts->macro_defs[i]));
            if (!nm || nm_set_macro(&ctx, trim_ws(nm), eq + 1) != 0) {
                free(nm);
                goto oom;
            }
            free(nm);
        }
    }

    /* Split logical lines (handling '\' continuation) */
    while (pos < opts->makefile_size) {
        ck_u32 start = pos;
        ck_u32 end;
        ck_usize llen;
        int cont = 0;
        while (pos < opts->makefile_size && opts->makefile_text[pos] != '\n') ++pos;
        end = pos;
        if (pos < opts->makefile_size && opts->makefile_text[pos] == '\n') ++pos;
        if (end > start && opts->makefile_text[end - 1u] == '\r') --end;
        llen = (ck_usize)(end - start);
        if (llen > 0 && opts->makefile_text[end - 1u] == '\\') {
            cont = 1;
            --llen;
        }
        if (cur_len + llen + 2u > cur_cap) {
            ck_usize nc = cur_cap ? cur_cap * 2u : 128u;
            char *np;
            while (nc < cur_len + llen + 2u) nc *= 2u;
            np = (char *)realloc(cur, (unsigned int)nc);
            if (!np) goto oom;
            cur = np; cur_cap = nc;
        }
        if (llen > 0) memcpy(cur + cur_len, opts->makefile_text + start, (unsigned int)llen);
        cur_len += llen;
        if (cont) {
            cur[cur_len++] = ' ';
            continue;
        }
        cur[cur_len] = '\0';
        {
            char *dup = nm_strdup(cur);
            if (!dup) goto oom;
            if (line_count == line_cap) {
                ck_usize nc = line_cap ? line_cap * 2u : 32u;
                char **np = (char **)realloc(lines, (unsigned int)(nc * sizeof(char *)));
                if (!np) { free(dup); goto oom; }
                lines = np; line_cap = nc;
            }
            lines[line_count++] = dup;
        }
        cur_len = 0;
    }
    if (cur_len > 0) {
        cur[cur_len] = '\0';
        {
            char *dup = nm_strdup(cur);
            if (!dup) goto oom;
            if (line_count == line_cap) {
                ck_usize nc = line_cap ? line_cap * 2u : 32u;
                char **np = (char **)realloc(lines, (unsigned int)(nc * sizeof(char *)));
                if (!np) { free(dup); goto oom; }
                lines = np; line_cap = nc;
            }
            lines[line_count++] = dup;
        }
    }
    free(cur);
    cur = NULL;

    for (i = 0; i < line_count; ++i) {
        char *ln = lines[i];
        int is_indented = (ln[0] == '\t' || ln[0] == ' ');
        int active = 1;
        char *t;
        for (j = 0; j < (ck_usize)cond_top; ++j) {
            if (!cond_stack[j]) { active = 0; break; }
        }
        t = trim_ws(ln);
        if (t[0] == '\0' || t[0] == '#') continue;
        if (t[0] == '!') {
            if (strncmp(t + 1, "endif", 5) == 0 || strncmp(t + 1, "ENDIF", 5) == 0) {
                if (cond_top > 0) --cond_top;
            } else if (strncmp(t + 1, "else", 4) == 0 || strncmp(t + 1, "ELSE", 4) == 0) {
                if (cond_top > 0) cond_stack[cond_top - 1] = !cond_stack[cond_top - 1];
            } else if (strncmp(t + 1, "ifdef", 5) == 0 || strncmp(t + 1, "IFDEF", 5) == 0) {
                const char *arg = trim_ws(t + 6);
                if (cond_top < 16) cond_stack[cond_top++] = (nm_get_macro(&ctx, arg)[0] != '\0');
            } else if (strncmp(t + 1, "ifndef", 6) == 0 || strncmp(t + 1, "IFNDEF", 6) == 0) {
                const char *arg = trim_ws(t + 7);
                if (cond_top < 16) cond_stack[cond_top++] = (nm_get_macro(&ctx, arg)[0] == '\0');
            }
            continue;
        }
        if (!active) continue;

        if (is_indented && cur_rule) {
            char *cmd = nm_strdup(t);
            char **np;
            if (!cmd) goto oom;
            np = (char **)realloc(cur_rule->cmds, (unsigned int)((cur_rule->cmd_count + 1u) * sizeof(char *)));
            if (!np) { free(cmd); goto oom; }
            cur_rule->cmds = np;
            cur_rule->cmds[cur_rule->cmd_count++] = cmd;
            continue;
        }
        cur_rule = NULL;

        {
            char *eq = strchr(t, '=');
            char *col = NULL;
            {
                char *p = t;
                while ((p = strchr(p, ':')) != NULL) {
                    int is_drive = (p > t &&
                                    ((p[-1] >= 'A' && p[-1] <= 'Z') || (p[-1] >= 'a' && p[-1] <= 'z')) &&
                                    (p == t + 1 || p[-2] == ' ' || p[-2] == '\t') &&
                                    (p[1] == '\\' || p[1] == '/'));
                    if (!is_drive) { col = p; break; }
                    ++p;
                }
            }
            if (eq && (!col || eq < col)) {
                char *mname = nm_strdup_n(t, (ck_usize)(eq - t));
                char *mval = NULL;
                if (!mname) goto oom;
                if (nm_expand(&ctx, trim_ws(eq + 1), NULL, NULL, NULL, NULL, &mval, 0) != 0) {
                    free(mname); goto oom;
                }
                if (nm_set_macro(&ctx, trim_ws(mname), mval) != 0) {
                    free(mname); free(mval); goto oom;
                }
                free(mname); free(mval);
                continue;
            }
            if (col) {
                char *lhs_raw = nm_strdup_n(t, (ck_usize)(col - t));
                char *lhs_exp = NULL, *rhs_exp = NULL;
                char **targs = NULL, **deps = NULL;
                ck_usize tcnt = 0, dcnt = 0;
                if (!lhs_raw) goto oom;
                if (nm_expand(&ctx, lhs_raw, NULL, NULL, NULL, NULL, &lhs_exp, 0) != 0 ||
                    nm_expand(&ctx, col + 1, NULL, NULL, NULL, NULL, &rhs_exp, 0) != 0) {
                    free(lhs_raw); free(lhs_exp); free(rhs_exp); goto oom;
                }
                free(lhs_raw);
                if (split_ws(lhs_exp, &targs, &tcnt) != 0 || split_ws(rhs_exp, &deps, &dcnt) != 0) {
                    free(lhs_exp); free(rhs_exp); goto oom;
                }
                free(lhs_exp); free(rhs_exp);
                for (j = 0; j < tcnt; ++j) {
                    nm_rule *nr;
                    ck_usize k;
                    if (ctx.rule_count == ctx.rule_cap) {
                        ck_usize nc = ctx.rule_cap ? ctx.rule_cap * 2u : 16u;
                        nm_rule *np = (nm_rule *)realloc(ctx.rules, (unsigned int)(nc * sizeof(nm_rule)));
                        if (!np) goto oom;
                        ctx.rules = np; ctx.rule_cap = nc;
                    }
                    nr = &ctx.rules[ctx.rule_count++];
                    memset(nr, 0, sizeof(*nr));
                    nr->target = nm_strdup(targs[j]);
                    nr->is_inference = is_inference_name(targs[j]);
                    if (!nr->is_inference && strcmpi(targs[j], ".SUFFIXES") != 0 && !first_target) {
                        first_target = nr->target;
                    }
                    if (dcnt > 0) {
                        nr->deps = (char **)malloc((unsigned int)(dcnt * sizeof(char *)));
                        if (!nr->deps) goto oom;
                        for (k = 0; k < dcnt; ++k) nr->deps[k] = nm_strdup(deps[k]);
                        nr->dep_count = dcnt;
                    }
                    cur_rule = nr;
                }
                for (j = 0; j < tcnt; ++j) free(targs[j]);
                for (j = 0; j < dcnt; ++j) free(deps[j]);
                free(targs); free(deps);
            }
        }
    }

    if (opts->target_count > 0) {
        for (i = 0; i < opts->target_count; ++i) {
            if (build_target(&ctx, opts->targets[i], err) != 0) { rc = -1; break; }
        }
    } else if (first_target) {
        if (build_target(&ctx, first_target, err) != 0) rc = -1;
    }

    goto cleanup;

oom:
    ck_set_error(err, 0, "out of memory");
    rc = -1;

cleanup:
    free(cur);
    for (i = 0; i < line_count; ++i) free(lines[i]);
    free(lines);
    for (i = 0; i < ctx.macro_count; ++i) { free(ctx.macros[i].name); free(ctx.macros[i].value); }
    free(ctx.macros);
    for (i = 0; i < ctx.rule_count; ++i) {
        free(ctx.rules[i].target);
        for (j = 0; j < ctx.rules[i].dep_count; ++j) free(ctx.rules[i].deps[j]);
        free(ctx.rules[i].deps);
        for (j = 0; j < ctx.rules[i].cmd_count; ++j) free(ctx.rules[i].cmds[j]);
        free(ctx.rules[i].cmds);
    }
    free(ctx.rules);
    for (i = 0; i < ctx.built_count; ++i) free(ctx.built[i]);
    free(ctx.built);
    return rc;
}
