/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Pre-compiled 16-bit bytecode compiler for CK Basic (QBASIC.EXE).
 */

#include "CKBVMP.H"

VmModule __far vm;

void vm_free_module(void)
{
    if (vm.code) { _ffree(vm.code); vm.code = NULL; }
    if (vm.fconsts) { _ffree(vm.fconsts); vm.fconsts = NULL; }
    if (vm.line_pc) { _ffree(vm.line_pc); vm.line_pc = NULL; }
    if (vm.stmts) { _ffree(vm.stmts); vm.stmts = NULL; }
    if (vm.fnframes) { _ffree(vm.fnframes); vm.fnframes = NULL; }
    if (vm.sel_str_buf) { _ffree(vm.sel_str_buf); vm.sel_str_buf = NULL; }
    vm.code_len = vm.fconst_len = vm.stmt_len = 0;
}
/* =========================================================================
 * Compiler Helpers
 * ========================================================================= */

typedef struct {
    VmFixup __far *fixups;
    unsigned fixup_cap, fixup_len;
    unsigned select_next_slot;
    int overflowed;
} VmCompiler;

static void emit(VmCompiler *c, unsigned w)
{
    if (vm.code_len >= vm.code_cap) {
        c->overflowed = 1;
        return;
    }
    vm.code[vm.code_len++] = (unsigned short)w;
}

static void emit2(VmCompiler *c, unsigned w1, unsigned w2)
{
    emit(c, w1);
    emit(c, w2);
}

static void emit3(VmCompiler *c, unsigned w1, unsigned w2, unsigned w3)
{
    emit(c, w1);
    emit(c, w2);
    emit(c, w3);
}

static unsigned add_fconst(VmCompiler *c, double val, int is_single)
{
    unsigned i;
    if (is_single) val = ckb_single_precision(val);
    for (i = 0; i < vm.fconst_len; ++i) {
        if (memcmp(&vm.fconsts[i], &val, sizeof(double)) == 0)
            return i | (is_single ? 0x8000U : 0U);
    }
    if (vm.fconst_len >= VM_MAX_FCONSTS) {
        c->overflowed = 1;
        return 0;
    }
    vm.fconsts[vm.fconst_len] = val;
    return (vm.fconst_len++) | (is_single ? 0x8000U : 0U);
}

static void emit_fixup(VmCompiler *c, Position pos, int plus_two)
{
    unsigned patch_at = vm.code_len;
    emit(c, 0);
    if (c->overflowed) return;
    if (c->fixup_len >= c->fixup_cap) {
        c->overflowed = 1;
        return;
    }
    c->fixups[c->fixup_len].patch_pc = patch_at;
    c->fixups[c->fixup_len].line = pos.line;
    c->fixups[c->fixup_len].offset = pos.offset;
    c->fixups[c->fixup_len].plus_two = (unsigned char)(plus_two ? 1 : 0);
    ++c->fixup_len;
}

static void init_parser_at(Parser *p, unsigned line, unsigned offset)
{
    p->line = line;
    p->pos = offset;
    p->line_end = lines[line].end;
    scan(p);
}

static int numeric_rank_vm(char kind)
{
    if (kind == '#') return 4;
    if (kind == '!' || !kind) return 3;
    if (kind == '&') return 2;
    return 1;
}

static int op_precedence_vm(const Parser *p)
{
    int t = p->token;
    if (!t) return 0;
    if (t == '+' || t == '-') return 8;
    if (t == '*' || t == '/') return 11;
    if (t == '=' || t == '<' || t == '>' ||
        t == T_LE || t == T_GE || t == T_NE) return 7;
    if (t == '\\') return 10;
    if (t == '^') return 13;
    if (t == T_NAME) {
        if (p->first_upper == 'M' && word(p, "MOD")) return 9;
        if (p->first_upper == 'A' && word(p, "AND")) return 5;
        if (p->first_upper == 'O' && word(p, "OR")) return 4;
        if (p->first_upper == 'X' && word(p, "XOR")) return 3;
        if (p->first_upper == 'E' && word(p, "EQV")) return 2;
        if (p->first_upper == 'I' && word(p, "IMP")) return 1;
    }
    return 0;
}

static int same_slice_vm(const char __far *a, const char __far *b, unsigned n)
{
    while (n--) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++))
            return 0;
    }
    return 1;
}

static int lookup_record_var_vm(const char __far *name, unsigned length, int array)
{
    unsigned i;
    int shared = -1;
    for (i = 0; i < variable_count; ++i) {
        const Variable *v = &variables[i];
        if (v->array == array && v->typed && record_size(v->kind)) {
            /* Match against source or variable table via named lookup */
            Parser tmp;
            char vname[BASIC_NAME_MAX + 1];
            unsigned k;
            if (length > BASIC_NAME_MAX) continue;
            for (k = 0; k < length; ++k) {
                unsigned char ch = (unsigned char)name[k];
                vname[k] = (char)((ch >= 'a' && ch <= 'z') ? (ch - 32U) : ch);
            }
            vname[length] = '\0';
            (void)tmp;
            if (v->first_upper == (unsigned char)vname[0]) {
                int cand;
                int saved_sc = current_scope;
                Parser dp;
                memset(&dp, 0, sizeof(dp));
                cand = named_variable(&dp, vname, v->kind, array, 0, 1, 0);
                current_scope = saved_sc;
                if (cand == (int)i) return cand;
            }
        }
    }
    return shared;
}

static int resolve_field_vm(char rec_kind, const char __far *path, unsigned length,
                            unsigned *out_off, unsigned *out_width, char *out_kind)
{
    unsigned off = 0;
    char k = rec_kind;
    unsigned w = 0;
    for (;;) {
        const RecordType *rt = record_type(k);
        const RecordField *f;
        unsigned comp = 0;
        if (!rt) return 0;
        while (comp < length && path[comp] != '.') ++comp;
        for (f = rt->fields; f; f = f->next) {
            if (f->length == comp && same_slice_vm(path, source + f->start, comp))
                break;
        }
        if (!f) return 0;
        off += f->offset;
        k = f->kind;
        w = f->width;
        if (comp == length) {
            if (record_size(k)) return 0; /* nested whole-record value: fallback */
            *out_off = off;
            *out_width = w;
            *out_kind = k;
            return 1;
        }
        path += comp + 1;
        length -= comp + 1;
    }
}

static int compile_expr(VmCompiler *c, Parser *p, int min_prec, char *out_kind);

static int compile_int_expr(VmCompiler *c, Parser *p)
{
    char k = 0;
    if (!compile_expr(c, p, 1, &k)) return 0;
    if (k == '$') return 0;
    if (k == '!' || k == '#') emit(c, OP_CONV_I32);
    return 1;
}

static int compile_f64_expr(VmCompiler *c, Parser *p)
{
    char k = 0;
    if (!compile_expr(c, p, 1, &k)) return 0;
    if (k == '$') return 0;
    if (k == '%' || k == '&') emit(c, OP_I2F);
    return 1;
}

static void emit_coerce_to(VmCompiler *c, char from_k, char to_k)
{
    if (from_k == to_k) return;
    if (from_k == '%' && to_k == '&') return;
    if ((from_k == '%' || from_k == '&') && (to_k == '!' || to_k == '#')) {
        emit(c, OP_I2F);
        if (to_k == '!') emit(c, OP_CONV_F32);
        return;
    }
    if (to_k == '%') emit(c, OP_CONV_I16);
    else if (to_k == '&') emit(c, OP_CONV_I32);
    else if (to_k == '!') emit(c, OP_CONV_F32);
    else if (to_k == '#') emit(c, OP_CONV_F64);
}

static int compile_proc_call_args(VmCompiler *c, Parser *p, int proc_id, int parenthesized, int is_func)
{
    Procedure __far *proc = &procedures[proc_id];
    unsigned n_params = proc->count, i = 0;
    int arglist = parenthesized && take(p, '(');
    if ((arglist && p->token != ')') || (!arglist && !end_statement(p) && !is_func)) {
        do {
            int param_id;
            const Variable *param;
            if (i >= n_params) return 0;
            param_id = proc->params[i];
            param = &variables[param_id];
            if (param->array) {
                char aname[BASIC_NAME_MAX + 1], akind;
                int unsuf = unsuffixed_name(p), aid;
                if (!identifier(p, aname, &akind)) return 0;
                if (!take(p, '(') || !take(p, ')')) return 0;
                aid = named_variable(p, aname, akind, 1, 0, unsuf, 0);
                if (aid < 0 || error_message) return 0;
                emit2(c, OP_PUSH_REF_ARR, (unsigned)aid);
            } else if (record_size(param->kind)) {
                return 0; /* whole-record parameter: let fallback handle */
            } else {
                /* Check if argument is a simple BYREF variable or 1D array element */
                Parser save_p = *p;
                int did_ref = 0;
                if (p->token == T_NAME && !_fmemchr(source + p->start, '.', p->length)) {
                    int ffn = lookup_deffn_token(p);
                    int fpr = lookup_proc_token(p);
                    unsigned look = p->pos;
                    while (look < p->line_end && (source[look] == ' ' || source[look] == '\t')) ++look;
                    if (ffn < 0 && (fpr < 0 || !procedures[fpr].function) &&
                        !(look < p->line_end && source[look] == '(' &&
                          ckb_string_function("X") == -999)) {
                        /* Try parsing simple var or 1D array element followed by ',' or ')' or EOS */
                        char vname[BASIC_NAME_MAX + 1], vkind;
                        int unsuf = unsuffixed_name(p);
                        Parser try_p = *p;
                        if (try_p.length <= BASIC_NAME_MAX && identifier(&try_p, vname, &vkind)) {
                            /* Make sure vname is not a builtin keyword */
                            if (strcmp(vname, "NOT") != 0 && strcmp(vname, "TIMER") != 0 &&
                                strcmp(vname, "RND") != 0 && strcmp(vname, "INKEY$") != 0 &&
                                strcmp(vname, "ERR") != 0 && strcmp(vname, "ERL") != 0 &&
                                strcmp(vname, "FREEFILE") != 0 && !ckb_string_function(vname)) {
                                if (try_p.token == ',' || try_p.token == ')' || end_statement(&try_p)) {
                                    int vid = named_variable(&try_p, vname, vkind, 0, 0, unsuf, 0);
                                    if (vid >= 0 && !error_message && !variables[vid].constant) {
                                        if (variables[vid].kind != param->kind ||
                                            variables[vid].fixed_width ||
                                            param->fixed_width || any_field_bound) {
                                            return 0;
                                        }
                                        *p = try_p;
                                        emit2(c, OP_PUSH_REF_VAR, (unsigned)vid);
                                        did_ref = 1;
                                    }
                                } else if (try_p.token == '(') {
                                    int vid = named_variable(&try_p, vname, vkind, 1, 0, unsuf, 0);
                                    if (vid >= 0 && !error_message) {
                                        if (variables[vid].kind != param->kind ||
                                            variables[vid].kind == '$' ||
                                            variables[vid].fixed_width || param->fixed_width ||
                                            record_size(variables[vid].kind)) {
                                            return 0;
                                        }
                                        take(&try_p, '(');
                                        /* Check if single subscript */
                                        *p = save_p;
                                        identifier(p, vname, &vkind);
                                        take(p, '(');
                                        if (compile_int_expr(c, p) && take(p, ')') &&
                                            (p->token == ',' || p->token == ')' || end_statement(p))) {
                                            emit2(c, OP_PUSH_REF_ARR1, (unsigned)vid);
                                            did_ref = 1;
                                        } else {
                                            return 0;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (!did_ref) {
                    char ak = 0;
                    *p = save_p;
                    /* Wait: if expression is a complex BYREF lvalue (like rec.field or 2D array),
                     * check if it's followed by operator; if it's a dotted/2D lvalue, fallback to be 100% BYREF-exact! */
                    if (p->token == T_NAME && _fmemchr(source + p->start, '.', p->length)) {
                        /* Could be BYREF record field if alone; check if parameter is ever mutated? Safest: allow if value expr, else fallback if alone! */
                        Parser look_p = *p;
                        scan(&look_p);
                        if (look_p.token == ',' || look_p.token == ')' || end_statement(&look_p))
                            return 0;
                    }
                    if (!compile_expr(c, p, 1, &ak)) return 0;
                    if ((ak == '$') != (param->kind == '$')) return 0;
                    if (ak != '$') emit_coerce_to(c, ak, param->kind);
                }
            }
            ++i;
        } while (take(p, ','));
    }
    if (arglist && !take(p, ')')) return 0;
    if (i != n_params) return 0;
    emit3(c, OP_CALL_PROC, (unsigned)proc_id, (unsigned)(is_func ? 1 : 0));
    return 1;
}

static int compile_builtin_call(VmCompiler *c, Parser *p, char *out_kind)
{
    static const char *const num_fns[] = {
        "CINT", "CLNG", "CSNG", "CDBL", "ABS", "SGN", "INT", "FIX",
        "SQR", "SIN", "COS", "TAN", "ATN", "EXP", "LOG"
    };
    char name[BASIC_NAME_MAX + 1];
    unsigned i, look;
    int sfn, nfn = 0;

    switch (p->first_upper) {
    case 'E':
        if (word(p, "ERR") || word(p, "ERL")) {
            int is_erl = word(p, "ERL");
            scan(p);
            emit2(c, OP_ERR_VAL, (unsigned)is_erl);
            *out_kind = is_erl ? '&' : '%';
            return 1;
        }
        if (word(p, "EOF") || word(p, "ENVIRON$")) return -1; /* fallback */
        break;
    case 'I':
        if (word(p, "INPUT$")) return -1;
        if (take_word(p, "INKEY$")) {
            emit(c, OP_INKEY);
            *out_kind = '$';
            return 1;
        }
        break;
    case 'F':
        if (word(p, "FREEFILE")) return -1;
        if (def_fn_count) {
            int fn_id = lookup_deffn_token(p);
            if (fn_id >= 0) {
                DefFn __far *fn = &def_fns[fn_id];
                unsigned pc = fn->param_count;
                scan(p);
                if (pc) {
                    if (!take(p, '(')) return -1;
                    for (i = 0; i < pc; ++i) {
                        char ak = 0;
                        if (i > 0 && !take(p, ',')) return -1;
                        if (!compile_expr(c, p, 1, &ak)) return -1;
                        if ((ak == '$') != (fn->param_kinds[i] == '$')) return -1;
                        if (ak != '$') emit_coerce_to(c, ak, fn->param_kinds[i]);
                    }
                    if (!take(p, ')')) return -1;
                } else if (take(p, '(')) {
                    if (!take(p, ')')) return -1;
                }
                emit2(c, OP_CALL_DEFFN, (unsigned)fn_id);
                *out_kind = fn->result_kind;
                return 1;
            }
        }
        break;
    case 'T':
        if (take_word(p, "TIMER")) {
            emit(c, OP_TIMER);
            *out_kind = '!';
            return 1;
        }
        break;
    case 'P':
        if (take_word(p, "PEEK")) {
            if (!take(p, '(') || !compile_int_expr(c, p) || !take(p, ')')) return -1;
            emit(c, OP_PEEK);
            *out_kind = '%';
            return 1;
        }
        if (take_word(p, "POINT")) {
            if (!take(p, '(') || !compile_int_expr(c, p)) return -1;
            if (take(p, ',')) {
                if (!compile_int_expr(c, p) || !take(p, ')')) return -1;
                emit(c, OP_POINT2);
            } else {
                if (!take(p, ')')) return -1;
                emit(c, OP_POINT1);
            }
            *out_kind = '%';
            return 1;
        }
        break;
    case 'R':
        if (take_word(p, "RND")) {
            if (take(p, '(')) {
                char ak = 0;
                if (!compile_expr(c, p, 1, &ak) || ak == '$' || !take(p, ')')) return -1;
                if (ak == '%' || ak == '&') emit(c, OP_I2F);
                emit(c, OP_RND1);
            } else {
                emit(c, OP_RND0);
            }
            *out_kind = '!';
            return 1;
        }
        break;
    case 'S':
        if (word(p, "SEEK")) return -1;
        break;
    case 'L':
        if (word(p, "LOC") || word(p, "LOF")) return -1;
        if (word(p, "LBOUND")) {
            char aname[BASIC_NAME_MAX + 1], akind;
            int unsuf, aid, has_d = 0;
            scan(p);
            if (!take(p, '(')) return -1;
            unsuf = unsuffixed_name(p);
            if (!identifier(p, aname, &akind)) return -1;
            aid = named_variable(p, aname, akind, 1, 0, unsuf, 0);
            if (aid < 0 || error_message) return -1;
            if (take(p, ',')) {
                if (!compile_int_expr(c, p)) return -1;
                has_d = 1;
            }
            if (!take(p, ')')) return -1;
            emit3(c, OP_BOUND, (unsigned)aid, (unsigned)(has_d ? 0x100U : 0U));
            *out_kind = '&';
            return 1;
        }
        break;
    case 'U':
        if (word(p, "UBOUND")) {
            char aname[BASIC_NAME_MAX + 1], akind;
            int unsuf, aid, has_d = 0;
            scan(p);
            if (!take(p, '(')) return -1;
            unsuf = unsuffixed_name(p);
            if (!identifier(p, aname, &akind)) return -1;
            aid = named_variable(p, aname, akind, 1, 0, unsuf, 0);
            if (aid < 0 || error_message) return -1;
            if (take(p, ',')) {
                if (!compile_int_expr(c, p)) return -1;
                has_d = 1;
            }
            if (!take(p, ')')) return -1;
            emit3(c, OP_BOUND, (unsigned)aid, (unsigned)(1U | (has_d ? 0x100U : 0U)));
            *out_kind = '&';
            return 1;
        }
        break;
    default:
        break;
    }

    if (p->length <= BASIC_NAME_MAX) {
        look = p->pos;
        while (look < p->line_end && (source[look] == ' ' || source[look] == '\t')) ++look;
        if (look < p->line_end && source[look] == '(') {
            for (i = 0; i < p->length; ++i) {
                unsigned char ch = (unsigned char)source[p->start + i];
                name[i] = (char)((ch >= 'a' && ch <= 'z') ? (ch - 32U) : ch);
            }
            name[i] = '\0';
            sfn = ckb_string_function(name);
            if (sfn) {
                char ak[3];
                unsigned argc = 0;
                if (sfn == SFN_REPEAT) return -1; /* STRING$(n, x) rare: fallback */
                scan(p);
                take(p, '(');
                if (p->token != ')') {
                    do {
                        if (argc >= 3) return -1;
                        if (!compile_expr(c, p, 1, &ak[argc])) return -1;
                        /* LEN(record_var) -> fallback */
                        if (sfn == SFN_LEN && record_size(ak[argc])) return -1;
                        ++argc;
                    } while (take(p, ','));
                }
                if (!take(p, ')')) return -1;
                if (argc == 1) {
                    if (sfn == SFN_CHR || sfn == SFN_SPACE) {
                        if (ak[0] == '$') return -1;
                        if (ak[0] == '!' || ak[0] == '#') emit(c, OP_CONV_I32);
                    } else if (sfn == SFN_STR) {
                        if (ak[0] == '$') return -1;
                    } else if (ak[0] != '$') return -1;
                    emit2(c, OP_STR_FN1, (unsigned)sfn);
                } else if (argc == 2) {
                    if (sfn == SFN_LEFT || sfn == SFN_RIGHT || sfn == SFN_MID) {
                        if (ak[0] != '$' || ak[1] == '$') return -1;
                        if (ak[1] == '!' || ak[1] == '#') emit(c, OP_CONV_I32);
                    } else if (sfn == SFN_INSTR) {
                        if (ak[0] != '$' || ak[1] != '$') return -1;
                    } else return -1;
                    emit2(c, OP_STR_FN2, (unsigned)sfn);
                } else if (argc == 3) {
                    if (sfn == SFN_MID) {
                        if (ak[0] != '$' || ak[1] == '$' || ak[2] == '$') return -1;
                        if (ak[2] == '!' || ak[2] == '#') emit(c, OP_CONV_I32);
                        /* Note: if ak[1] was float, convert via fallback to keep stack simple */
                        if (ak[1] == '!' || ak[1] == '#') return -1;
                    } else if (sfn == SFN_INSTR) {
                        if (ak[0] == '$' || ak[1] != '$' || ak[2] != '$') return -1;
                        if (ak[0] == '!' || ak[0] == '#') return -1;
                    } else return -1;
                    emit2(c, OP_STR_FN3, (unsigned)sfn);
                } else return -1;

                if (sfn == SFN_ASC || sfn == SFN_LEN || sfn == SFN_INSTR) *out_kind = '%';
                else if (sfn == SFN_VAL) *out_kind = '#';
                else *out_kind = '$';
                return 1;
            }

            for (i = 0; i < sizeof(num_fns) / sizeof(num_fns[0]); ++i) {
                if (strcmp(name, num_fns[i]) == 0) {
                    nfn = CF_CINT - (int)i;
                    break;
                }
            }
            /* Check MKS$/CVS/HEX$/OCT$ -> fallback */
            if (!nfn && (name[0] == 'M' || name[0] == 'C' || name[0] == 'H' || name[0] == 'O')) {
                if (strcmp(name, "MKS$") == 0 || strcmp(name, "MKD$") == 0 ||
                    strcmp(name, "MKSMBF$") == 0 || strcmp(name, "MKDMBF$") == 0 ||
                    strcmp(name, "MKI$") == 0 || strcmp(name, "MKL$") == 0 ||
                    strcmp(name, "CVS") == 0 || strcmp(name, "CVD") == 0 ||
                    strcmp(name, "CVSMBF") == 0 || strcmp(name, "CVDMBF") == 0 ||
                    strcmp(name, "CVI") == 0 || strcmp(name, "CVL") == 0 ||
                    strcmp(name, "HEX$") == 0 || strcmp(name, "OCT$") == 0)
                    return -1;
            }
            if (nfn) {
                char ak = 0;
                scan(p);
                if (!take(p, '(') || !compile_expr(c, p, 1, &ak) || ak == '$' || !take(p, ')'))
                    return -1;
                if (nfn >= CF_CDBL) {
                    char tk = nfn == CF_CINT ? '%' : nfn == CF_CLNG ? '&' :
                              nfn == CF_CSNG ? '!' : '#';
                    emit_coerce_to(c, ak, tk);
                    *out_kind = tk;
                    return 1;
                }
                if ((nfn == CF_INT || nfn == CF_FIX) && (ak == '%' || ak == '&')) {
                    *out_kind = ak; /* INT/FIX on integer is identity! */
                    return 1;
                }
                {
                    char rk = ak ? ak : '!';
                    if (nfn == CF_SGN) rk = '%';
                    else if (nfn <= CF_SQR && rk != '#') rk = '!';
                    emit3(c, OP_MATH_FN, (unsigned)(unsigned short)nfn, (unsigned)(unsigned char)rk);
                    *out_kind = rk;
                    return 1;
                }
            }
        }
    }

    if (procedure_count) {
        int pid = lookup_proc_token(p);
        if (pid >= 0 && procedures[pid].function) {
            if (current_scope == pid) {
                Parser nxt = *p;
                scan(&nxt);
                if (nxt.token != '(') return 0; /* reads return variable */
            }
            scan(p);
            if (!compile_proc_call_args(c, p, pid, 1, 1)) return -1;
            *out_kind = procedures[pid].result_kind;
            return 1;
        }
    }
    return 0;
}

static int compile_var_or_lvalue_load(VmCompiler *c, Parser *p, char *out_kind)
{
    const char __far *name = source + p->start;
    const char __far *dot = _fmemchr(name, '.', p->length);
    unsigned length = p->length;
    int id;

    if (dot) {
        unsigned foff = 0, fw = 0;
        char fk = 0;
        id = lookup_record_var_vm(name, (unsigned)(dot - name), 0);
        if (id >= 0) {
            if (!resolve_field_vm(variables[id].kind, dot + 1,
                                  length - (unsigned)(dot - name) - 1, &foff, &fw, &fk))
                return 0;
            if (fk == '$') return 0; /* fixed string in record: fallback */
            scan(p);
            emit(c, OP_LOAD_REC);
            emit3(c, (unsigned)id, foff, (fw << 8) | (unsigned char)fk);
            *out_kind = fk;
            return 1;
        }
    }

    {
        char vname[BASIC_NAME_MAX + 1], vkind;
        int unsuf = unsuffixed_name(p);
        int is_arr;
        if (!identifier(p, vname, &vkind) || error_message) return 0;
        is_arr = (p->token == '(');
        id = named_variable(p, vname, vkind, is_arr, 0, unsuf, 0);
        if (id < 0 || error_message) return 0;

        if (is_arr) {
            unsigned dims = 0;
            take(p, '(');
            do {
                if (dims >= 2) return 0; /* >2D array: fallback */
                if (!compile_int_expr(c, p)) return 0;
                ++dims;
            } while (take(p, ','));
            if (!take(p, ')')) return 0;

            if (record_size(variables[id].kind)) {
                unsigned foff = 0, fw = 0;
                char fk = 0;
                if (dims != 1 || !take(p, '.') || p->token != T_NAME) return 0;
                if (!resolve_field_vm(variables[id].kind, source + p->start, p->length,
                                      &foff, &fw, &fk) || fk == '$')
                    return 0;
                scan(p);
                emit(c, OP_LOAD_RECARR1);
                emit3(c, (unsigned)id, foff, (fw << 8) | (unsigned char)fk);
                *out_kind = fk;
                return 1;
            }
            if (p->token == '.') return 0;
            emit2(c, dims == 1 ? OP_LOAD_ARR1 : OP_LOAD_ARR2, (unsigned)id);
            *out_kind = variables[id].kind;
            return 1;
        }

        if (record_size(variables[id].kind)) return 0;
        *out_kind = variables[id].kind;
        if (variables[id].frame_slot) {
            if (*out_kind == '%') emit2(c, OP_LOAD_LINT, (unsigned)id);
            else if (*out_kind == '&') emit2(c, OP_LOAD_LINT, (unsigned)id);
            else if (*out_kind == '$') emit2(c, OP_LOAD_LSTR, (unsigned)id);
            else emit2(c, OP_LOAD_LFLT, (unsigned)id);
        } else {
            unsigned garg = VM_GVAR_ARG(id);
            if (*out_kind == '%') emit2(c, OP_LOAD_GINT_I16, garg);
            else if (*out_kind == '&') emit2(c, OP_LOAD_GINT_I32, garg);
            else if (*out_kind == '$') emit2(c, OP_LOAD_GSTR, garg);
            else if (*out_kind == '!') emit2(c, OP_LOAD_GFLT_SNG, garg);
            else emit2(c, OP_LOAD_GFLT_DBL, garg);
        }
        return 1;
    }
}

static int compile_expr(VmCompiler *c, Parser *p, int min_prec, char *out_kind)
{
    char lk = 0;
    int rank;

    if (p->token == '+' || p->token == '-' ||
        (p->token == T_NAME && p->first_upper == 'N' && word(p, "NOT"))) {
        int op = p->token;
        scan(p);
        if (!compile_expr(c, p, op == T_NAME ? 6 : 12, &lk)) return 0;
        if (lk == '$' || record_size(lk)) return 0;
        if (op == '-') {
            if (lk == '%') emit(c, OP_NEG_I16);
            else if (lk == '&') emit(c, OP_NEG_I32);
            else emit2(c, OP_NEG_F, (unsigned)(unsigned char)(lk ? lk : '!'));
        } else if (op == T_NAME) {
            if (lk == '!' || lk == '#') {
                emit(c, OP_CONV_I32);
                lk = '&';
            }
            emit(c, lk == '%' ? OP_NOT_I16 : OP_NOT_I32);
        }
    } else if (take(p, '(')) {
        if (!compile_expr(c, p, 1, &lk)) return 0;
        if (!take(p, ')')) return 0;
    } else if (p->token == T_NUMBER) {
        char nk = p->number_kind;
        double nv = p->number;
        scan(p);
        if (nk == '%') {
            long lv = (long)round_even(nv);
            if (lv < -32768L || lv > 32767L) return 0;
            emit2(c, OP_LIT_I16, (unsigned)(unsigned short)(short)lv);
            lk = '%';
        } else if (nk == '&') {
            long lv = (long)round_even(nv);
            unsigned long uv = (unsigned long)lv;
            emit3(c, OP_LIT_I32, (unsigned)(uv & 0xffffUL), (unsigned)(uv >> 16));
            lk = '&';
        } else {
            unsigned idx = add_fconst(c, nv, nk == '!');
            emit2(c, OP_LIT_F64, idx);
            lk = nk == '#' ? '#' : '!';
        }
    } else if (p->token == T_STRING) {
        unsigned soff = p->start;
        unsigned slen = p->length > TEXT_INPUT_MAX ? TEXT_INPUT_MAX : p->length;
        scan(p);
        emit3(c, OP_LIT_STR, soff, slen);
        lk = '$';
    } else if (p->token == T_NAME) {
        int bres = compile_builtin_call(c, p, &lk);
        if (bres < 0) return 0;
        if (bres == 0) {
            if (!compile_var_or_lvalue_load(c, p, &lk)) return 0;
        }
    } else {
        return 0;
    }

    while (!c->overflowed && !error_message && (rank = op_precedence_vm(p)) >= min_prec) {
        int op = p->token;
        char rk = 0;
        scan(p);
        if (!compile_expr(c, p, rank + 1, &rk)) return 0;
        if (record_size(lk) || record_size(rk) || ((lk == '$') != (rk == '$')))
            return 0;

        if (lk == '$') {
            if (op == '+') {
                emit(c, OP_CONCAT_STR);
                lk = '$';
            } else if (rank == 7) {
                emit2(c, OP_CMP_STR, (unsigned)op);
                lk = '%';
            } else return 0;
            continue;
        }

        if (rank == 7) {
            /* Relational comparison -> result is '%' */
            if ((lk == '%' || lk == '&') && (rk == '%' || rk == '&')) {
                emit2(c, OP_CMP_I, (unsigned)op);
            } else {
                if (lk == '%' || lk == '&') emit(c, OP_I2F_UNDER);
                if (rk == '%' || rk == '&') emit(c, OP_I2F);
                emit2(c, OP_CMP_F, (unsigned)op);
            }
            lk = '%';
            continue;
        }

        {
            int lrank = numeric_rank_vm(lk);
            int rrank = numeric_rank_vm(rk);
            int res_rank = lrank > rrank ? lrank : rrank;
            char res_kind;
            if ((op == '/' || op == '^') && res_rank < 3) res_rank = 3;
            if (rank <= 5 || op == '\\' || rank == 9)
                res_rank = (lrank == 1 && rrank == 1) ? 1 : 2;
            res_kind = res_rank == 4 ? '#' : res_rank == 3 ? '!' :
                       res_rank == 2 ? '&' : '%';

            if (rank <= 5 || op == '\\' || rank == 9) {
                /* Integer / bitwise operator */
                if (lrank >= 3) emit(c, OP_CONV_I32_UNDER);
                if (rrank >= 3) emit(c, res_rank == 1 ? OP_CONV_I16 : OP_CONV_I32);
                if (res_rank == 1) {
                    if (op == '\\') emit(c, OP_IDIV_I16);
                    else if (rank == 9) emit(c, OP_MOD_I16);
                    else if (rank == 5) emit(c, OP_AND_I16);
                    else if (rank == 4) emit(c, OP_OR_I16);
                    else if (rank == 3) emit(c, OP_XOR_I16);
                    else if (rank == 2) emit(c, OP_EQV_I16);
                    else emit(c, OP_IMP_I16);
                } else {
                    if (op == '\\') emit(c, OP_IDIV_I32);
                    else if (rank == 9) emit(c, OP_MOD_I32);
                    else if (rank == 5) emit(c, OP_AND_I32);
                    else if (rank == 4) emit(c, OP_OR_I32);
                    else if (rank == 3) emit(c, OP_XOR_I32);
                    else if (rank == 2) emit(c, OP_EQV_I32);
                    else emit(c, OP_IMP_I32);
                }
                lk = res_kind;
                continue;
            }

            if (res_rank == 1) {
                if (op == '+') emit(c, OP_ADD_I16);
                else if (op == '-') emit(c, OP_SUB_I16);
                else emit(c, OP_MUL_I16);
            } else if (res_rank == 2) {
                if (op == '+') emit(c, OP_ADD_I32);
                else if (op == '-') emit(c, OP_SUB_I32);
                else emit(c, OP_MUL_I32);
            } else {
                if (lrank <= 2) emit(c, OP_I2F_UNDER);
                if (rrank <= 2) emit(c, OP_I2F);
                if (op == '+') emit2(c, OP_ADD_F, (unsigned)(unsigned char)res_kind);
                else if (op == '-') emit2(c, OP_SUB_F, (unsigned)(unsigned char)res_kind);
                else if (op == '*') emit2(c, OP_MUL_F, (unsigned)(unsigned char)res_kind);
                else if (op == '/') emit2(c, OP_DIV_F, (unsigned)(unsigned char)res_kind);
                else emit2(c, OP_POW_F, (unsigned)(unsigned char)res_kind);
            }
            lk = res_kind;
        }
    }

    *out_kind = lk;
    return !c->overflowed && !error_message;
}

/* =========================================================================
 * Statement Compiler
 * ========================================================================= */

static int compile_gfx_coord_vm(VmCompiler *c, Parser *p, int *out_step)
{
    *out_step = (p->token == T_NAME && p->first_upper == 'S') ? take_word(p, "STEP") : 0;
    if (!take(p, '(')) return 0;
    if (!compile_int_expr(c, p)) return 0;
    if (!take(p, ',')) return 0;
    if (!compile_int_expr(c, p)) return 0;
    if (!take(p, ')')) return 0;
    return 1;
}

static int is_reserved_stmt_vm(const char *s)
{
    static const char *const kw[] = {
        "MID$", "ENVIRON", "OUT", "POKE", "PLAY", "SOUND", "SLEEP", "WIDTH",
        "OPEN", "CLOSE", "INPUT", "WRITE", "READ", "RESTORE", "RANDOMIZE",
        "SWAP", "LSET", "RSET", "ERASE", "REDIM", "CLEAR", "COMMON", "OPTION",
        "CHAIN", "RUN", "SHELL", "FIELD", "SEEK", "LOCK", "UNLOCK", "KILL",
        "NAME", "MKDIR", "RMDIR", "CHDIR", "LPRINT", "ERROR", "ON", "RESUME",
        "CONST"
    };
    unsigned i;
    for (i = 0; i < sizeof(kw) / sizeof(kw[0]); ++i)
        if (kw[i][0] == s[0] && strcmp(kw[i], s) == 0) return 1;
    return 0;
}

static int compile_assignment_vm(VmCompiler *c, Parser *p)
{
    const char __far *name;
    const char __far *dot;
    unsigned length;
    int id;
    char lk = 0, rk = 0;

    take_word(p, "LET");
    if (p->token != T_NAME) return 0;
    name = source + p->start;
    dot = _fmemchr(name, '.', p->length);
    length = p->length;

    if (dot) {
        unsigned foff = 0, fw = 0;
        id = lookup_record_var_vm(name, (unsigned)(dot - name), 0);
        if (id >= 0) {
            if (variables[id].constant) return 0;
            if (!resolve_field_vm(variables[id].kind, dot + 1,
                                  length - (unsigned)(dot - name) - 1, &foff, &fw, &lk))
                return 0;
            if (lk == '$') return 0;
            scan(p);
            if (!take(p, '=')) return 0;
            if (!compile_expr(c, p, 1, &rk) || rk == '$') return 0;
            emit_coerce_to(c, rk, lk);
            emit(c, OP_STORE_REC);
            emit3(c, (unsigned)id, foff, (fw << 8) | (unsigned char)lk);
            return end_statement(p);
        }
    }

    {
        char vname[BASIC_NAME_MAX + 1], vkind;
        int unsuf = unsuffixed_name(p);
        int is_arr;
        if (!identifier(p, vname, &vkind) || error_message) return 0;
        if (is_reserved_stmt_vm(vname)) return 0;
        is_arr = (p->token == '(');
        if (!is_arr && p->token != '=') return 0;
        id = named_variable(p, vname, vkind, is_arr, 0, unsuf, 0);
        if (id < 0 || error_message || variables[id].constant) return 0;

        if (is_arr) {
            unsigned dims = 0;
            take(p, '(');
            do {
                if (dims >= 2) return 0;
                if (!compile_int_expr(c, p)) return 0;
                ++dims;
            } while (take(p, ','));
            if (!take(p, ')')) return 0;

            if (record_size(variables[id].kind)) {
                unsigned foff = 0, fw = 0;
                if (dims != 1 || !take(p, '.') || p->token != T_NAME) return 0;
                if (!resolve_field_vm(variables[id].kind, source + p->start, p->length,
                                      &foff, &fw, &lk) || lk == '$')
                    return 0;
                scan(p);
                if (!take(p, '=')) return 0;
                if (!compile_expr(c, p, 1, &rk) || rk == '$') return 0;
                emit_coerce_to(c, rk, lk);
                emit(c, OP_STORE_RECARR1);
                emit3(c, (unsigned)id, foff, (fw << 8) | (unsigned char)lk);
                return end_statement(p);
            }
            if (p->token == '.') return 0;
            lk = variables[id].kind;
            if (!take(p, '=')) return 0;
            if (!compile_expr(c, p, 1, &rk)) return 0;
            if ((lk == '$') != (rk == '$') || record_size(rk)) return 0;
            if (lk != '$') emit_coerce_to(c, rk, lk);
            emit2(c, dims == 1 ? OP_STORE_ARR1 : OP_STORE_ARR2, (unsigned)id);
            return end_statement(p);
        }

        if (record_size(variables[id].kind)) return 0;
        lk = variables[id].kind;
        if (!take(p, '=')) return 0;
        if (!compile_expr(c, p, 1, &rk)) return 0;
        if ((lk == '$') != (rk == '$') || record_size(rk)) return 0;
        if (lk != '$') emit_coerce_to(c, rk, lk);
        if (variables[id].frame_slot) {
            if (lk == '%' || lk == '&') emit2(c, OP_STORE_LINT, (unsigned)id);
            else if (lk == '$') emit2(c, OP_STORE_LSTR, (unsigned)id);
            else emit2(c, OP_STORE_LFLT, (unsigned)id);
        } else {
            unsigned garg = VM_GVAR_ARG(id);
            if (lk == '%') emit2(c, OP_STORE_GINT_I16, garg);
            else if (lk == '&') emit2(c, OP_STORE_GINT_I32, garg);
            else if (lk == '$') emit2(c, OP_STORE_GSTR, garg);
            else if (lk == '!') emit2(c, OP_STORE_GFLT_SNG, garg);
            else emit2(c, OP_STORE_GFLT_DBL, garg);
        }
        return end_statement(p);
    }
}

static int compile_stmt(VmCompiler *c, Parser *p, Position at, unsigned inline_depth,
                        unsigned *sel_blocks, unsigned *sel_slots, unsigned *sel_top);

static int compile_inline_list(VmCompiler *c, Parser *p, unsigned inline_depth,
                               unsigned *sel_blocks, unsigned *sel_slots, unsigned *sel_top)
{
    if (p->token == T_NUMBER) {
        unsigned tgt = target_label(p);
        if (error_message || tgt >= line_count) return 0;
        emit(c, OP_JMP);
        {
            Position tp;
            tp.line = tgt;
            tp.offset = lines[tgt].body;
            emit_fixup(c, tp, 0);
        }
        return end_statement(p);
    }
    if (end_statement(p)) return 0;
    do {
        Position sub_at;
        sub_at.line = p->line;
        sub_at.offset = p->start;
        if (!compile_stmt(c, p, sub_at, inline_depth, sel_blocks, sel_slots, sel_top))
            return 0;
    } while (take(p, ':') && p->token && !word(p, "ELSE"));
    return 1;
}

static int compile_cond_bool(VmCompiler *c, Parser *p)
{
    char k = 0;
    if (!compile_expr(c, p, 1, &k)) return 0;
    if (k == '$' || record_size(k)) return 0;
    return 1;
}

static int compile_stmt(VmCompiler *c, Parser *p, Position at, unsigned inline_depth,
                        unsigned *sel_blocks, unsigned *sel_slots, unsigned *sel_top)
{
    unsigned bidx;
    if (!p->token) return 1;
    if (p->token == T_NAME && (p->first_upper == 'R' || p->first_upper == 'D') &&
        (word(p, "REM") || word(p, "DECLARE"))) {
        p->token = 0;
        p->pos = lines[p->line].end;
        return 1;
    }

    switch (p->token == '?' ? '?' : (p->token == T_NAME ? (int)p->first_upper : 0)) {
    case '?':
        take(p, '?');
        goto do_console_print;

    case 'B':
        if (take_word(p, "BEEP")) {
            emit(c, OP_CON_BEEP);
            return end_statement(p);
        }
        break;

    case 'C':
        if (take_word(p, "CLS")) {
            if (end_statement(p)) {
                emit2(c, OP_CON_CLS, 0);
                return 1;
            }
            if (!compile_int_expr(c, p)) return 0;
            emit2(c, OP_CON_CLS, 1);
            return end_statement(p);
        }
        if (take_word(p, "COLOR")) {
            unsigned mask = 0;
            if (p->token != ',' && !end_statement(p)) {
                if (!compile_int_expr(c, p)) return 0;
                mask |= 1U;
            }
            if (take(p, ',')) {
                if (p->token != ',' && !end_statement(p)) {
                    if (!compile_int_expr(c, p)) return 0;
                    mask |= 2U;
                }
                if (take(p, ',')) {
                    /* border argument ignored on EGA/VGA, still evaluate if present */
                    if (!end_statement(p)) return 0;
                }
            }
            emit2(c, OP_CON_COLOR, mask);
            return end_statement(p);
        }
        if (take_word(p, "CIRCLE")) {
            int step = 0;
            unsigned flags = 0;
            if (!compile_gfx_coord_vm(c, p, &step)) return 0;
            if (step) flags |= 1U;
            if (!take(p, ',') || !compile_f64_expr(c, p)) return 0;
            if (take(p, ',')) {
                if (p->token != ',' && !end_statement(p)) {
                    if (!compile_int_expr(c, p)) return 0;
                    flags |= 2U;
                }
                if (take(p, ',')) {
                    if (p->token != ',' && !end_statement(p)) {
                        if (!compile_f64_expr(c, p)) return 0;
                        flags |= 4U;
                    }
                    if (take(p, ',')) {
                        if (p->token != ',' && !end_statement(p)) {
                            if (!compile_f64_expr(c, p)) return 0;
                            flags |= 8U;
                        }
                        if (take(p, ',') && !end_statement(p)) {
                            if (!compile_f64_expr(c, p)) return 0;
                            flags |= 16U;
                        }
                    }
                }
            }
            emit2(c, OP_GFX_CIRCLE, flags);
            return end_statement(p);
        }
        if (take_word(p, "CALL")) {
            int pid = lookup_proc_token(p);
            if (pid < 0 || procedures[pid].function) return 0;
            scan(p);
            if (procedures[pid].count && p->token != '(') return 0;
            if (!compile_proc_call_args(c, p, pid, 1, 0)) return 0;
            return end_statement(p);
        }
        if (take_word(p, "CASE")) {
            unsigned slot = 0, k, finish_idx, peer_idx;
            Position body_pos;
            if (inline_depth) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX) return 0;
            finish_idx = blocks[blocks[bidx].owner].finish;
            peer_idx = blocks[bidx].peer;
            body_pos = blocks[bidx].after;
            for (k = 0; k < *sel_top; ++k) {
                if (sel_blocks[k] == blocks[bidx].owner) {
                    slot = sel_slots[k];
                    break;
                }
            }
            if (k == *sel_top) return 0;
            /* Word 0..1: fallthrough from previous CASE body jumps past END SELECT */
            emit(c, OP_JMP);
            emit_fixup(c, blocks[finish_idx].after, 0);
            /* Word 2..: CASE test */
            if (take_word(p, "ELSE")) {
                return end_statement(p);
            }
            do {
                if (take_word(p, "IS")) {
                    int cop = p->token;
                    char ck = 0;
                    if (cop != '=' && cop != '<' && cop != '>' &&
                        cop != T_LE && cop != T_GE && cop != T_NE) return 0;
                    scan(p);
                    if (!compile_expr(c, p, 8, &ck) || record_size(ck)) return 0;
                    emit3(c, OP_SEL_CASE_IS, slot, (unsigned)cop);
                    emit_fixup(c, body_pos, 0);
                } else {
                    char ck1 = 0;
                    if (!compile_expr(c, p, 1, &ck1) || record_size(ck1)) return 0;
                    if (take_word(p, "TO")) {
                        char ck2 = 0;
                        if (!compile_expr(c, p, 1, &ck2) || record_size(ck2)) return 0;
                        emit2(c, OP_SEL_CASE_RNG, slot);
                        emit_fixup(c, body_pos, 0);
                    } else {
                        emit2(c, OP_SEL_CASE_VAL, slot);
                        emit_fixup(c, body_pos, 0);
                    }
                }
            } while (take(p, ','));
            /* None matched in this CASE -> jump to next CASE (+2) or END SELECT */
            emit(c, OP_JMP);
            if (blocks[peer_idx].kind == B_CASE)
                emit_fixup(c, blocks[peer_idx].at, 1);
            else
                emit_fixup(c, blocks[finish_idx].after, 0);
            return end_statement(p);
        }
        break;

    case 'D':
        if (take_word(p, "DO")) {
            int lkind = C_ALWAYS;
            if (inline_depth) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX) return 0;
            if (take_word(p, "WHILE")) lkind = C_WHILE;
            else if (take_word(p, "UNTIL")) lkind = C_UNTIL;
            if (lkind != C_ALWAYS) {
                if (!compile_cond_bool(c, p)) return 0;
                emit(c, lkind == C_WHILE ? OP_JFALSE : OP_JTRUE);
                emit_fixup(c, blocks[blocks[bidx].peer].after, 0);
            }
            return end_statement(p);
        }
        if (take_word(p, "DIM") || take_word(p, "STATIC")) {
            /* Scalar DIM/STATIC declarations are compile-time only (runtime NOP)!
             * Array DIM/STATIC falls back to allocate array storage once. */
            if (word(p, "SHARED")) scan(p);
            do {
                char dname[BASIC_NAME_MAX + 1], dkind;
                if (!identifier(p, dname, &dkind)) return 0;
                if (p->token == '(') return 0; /* array DIM -> OP_FALLBACK */
                if (take_word(p, "AS")) {
                    /* skip type name */
                    if (take_word(p, "STRING")) {
                        if (take(p, '*')) {
                            if (p->token == T_NUMBER || p->token == T_NAME) scan(p);
                            else return 0;
                        }
                    } else if (p->token == T_NAME) {
                        scan(p);
                    } else return 0;
                }
            } while (take(p, ','));
            return end_statement(p);
        }
        if (take_word(p, "DATA")) {
            /* DATA lines are non-executable at runtime */
            p->token = 0;
            p->pos = lines[p->line].end;
            return 1;
        }
        if (word(p, "DEFINT") || word(p, "DEFLNG") || word(p, "DEFSNG") ||
            word(p, "DEFDBL") || word(p, "DEFSTR")) {
            return 0; /* run fallback once to keep default_kind synced */
        }
        if (take_word(p, "DEF")) {
            if (word(p, "SEG")) return 0; /* DEF SEG -> fallback */
            /* DEF FN declaration is skipped at runtime */
            p->token = 0;
            p->pos = lines[p->line].end;
            return 1;
        }
        break;

    case 'E':
        if (take_word(p, "END")) {
            if (take_word(p, "IF")) {
                if (inline_depth) return 0;
                return end_statement(p);
            }
            if (take_word(p, "SELECT")) {
                if (inline_depth) return 0;
                if (*sel_top > 0) --(*sel_top);
                return end_statement(p);
            }
            if (take_word(p, "SUB") || take_word(p, "FUNCTION")) {
                emit(c, OP_RET_PROC);
                return end_statement(p);
            }
            emit(c, OP_HALT);
            return end_statement(p);
        }
        if (take_word(p, "ELSEIF")) {
            unsigned finish_idx, peer_idx;
            if (inline_depth) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX) return 0;
            finish_idx = blocks[blocks[bidx].owner].finish;
            peer_idx = blocks[bidx].peer;
            /* Word 0..1: fallthrough from prior branch jumps past END IF */
            emit(c, OP_JMP);
            emit_fixup(c, blocks[finish_idx].after, 0);
            /* Word 2..: condition check */
            if (!compile_cond_bool(c, p) || !take_word(p, "THEN")) return 0;
            emit(c, OP_JFALSE);
            if (blocks[peer_idx].kind == B_ELSEIF)
                emit_fixup(c, blocks[peer_idx].at, 1);
            else
                emit_fixup(c, blocks[peer_idx].after, 0);
            return end_statement(p);
        }
        if (take_word(p, "ELSE")) {
            unsigned finish_idx;
            if (inline_depth) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX) return 0;
            finish_idx = blocks[blocks[bidx].owner].finish;
            emit(c, OP_JMP);
            emit_fixup(c, blocks[finish_idx].after, 0);
            return end_statement(p);
        }
        if (take_word(p, "EXIT")) {
            if (!(take_word(p, "SUB") || take_word(p, "FUNCTION")) || current_scope < 0)
                return 0;
            emit(c, OP_RET_PROC);
            return end_statement(p);
        }
        break;

    case 'F':
        if (take_word(p, "FOR")) {
            char vname[BASIC_NAME_MAX + 1], vkind, k1, k2, k3;
            int unsuf = unsuffixed_name(p), vid, has_step = 0;
            if (inline_depth) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX) return 0;
            if (!identifier(p, vname, &vkind)) return 0;
            vid = named_variable(p, vname, vkind, 0, 0, unsuf, 0);
            if (vid < 0 || error_message || variables[vid].array ||
                variables[vid].kind == '$' || record_size(variables[vid].kind) ||
                variables[vid].constant)
                return 0;
            if (!take(p, '=')) return 0;
            if (variables[vid].kind == '%') {
                Parser sp_save = *p;
                unsigned pc_save = vm.code_len, fc_save = vm.fconst_len, fx_save = c->fixup_len;
                int all_int = 1;
                if (!compile_expr(c, p, 1, &k1) || (k1 != '%' && k1 != '&')) all_int = 0;
                if (all_int && !take_word(p, "TO")) return 0;
                if (all_int && (!compile_expr(c, p, 1, &k2) || (k2 != '%' && k2 != '&'))) all_int = 0;
                if (all_int && take_word(p, "STEP")) {
                    if (!compile_expr(c, p, 1, &k3) || (k3 != '%' && k3 != '&')) all_int = 0;
                    has_step = 1;
                }
                if (all_int && !error_message) {
                    emit(c, OP_FOR_INIT_I16);
                    emit3(c, (unsigned)vid, bidx, (unsigned)has_step);
                    emit_fixup(c, blocks[blocks[bidx].peer].after, 0);
                    return end_statement(p);
                }
                error_message = NULL;
                *p = sp_save;
                vm.code_len = pc_save;
                vm.fconst_len = fc_save;
                c->fixup_len = fx_save;
                has_step = 0;
            }
            if (!compile_expr(c, p, 1, &k1) || k1 == '$') return 0;
            if (k1 == '%' || k1 == '&') emit(c, OP_I2F);
            if (!take_word(p, "TO")) return 0;
            if (!compile_expr(c, p, 1, &k2) || k2 == '$') return 0;
            if (k2 == '%' || k2 == '&') emit(c, OP_I2F);
            if (take_word(p, "STEP")) {
                if (!compile_expr(c, p, 1, &k3) || k3 == '$') return 0;
                if (k3 == '%' || k3 == '&') emit(c, OP_I2F);
                has_step = 1;
            }
            emit(c, OP_FOR_INIT);
            emit3(c, (unsigned)vid, bidx, (unsigned)has_step);
            emit_fixup(c, blocks[blocks[bidx].peer].after, 0);
            return end_statement(p);
        }
        break;

    case 'G':
        if (take_word(p, "GOTO")) {
            unsigned tgt = target_label(p);
            Position tp;
            if (error_message || tgt >= line_count) return 0;
            tp.line = tgt;
            tp.offset = lines[tgt].body;
            emit(c, OP_JMP);
            emit_fixup(c, tp, 0);
            return end_statement(p);
        }
        if (take_word(p, "GOSUB")) {
            unsigned tgt = target_label(p);
            Position tp;
            if (error_message || tgt >= line_count || !end_statement(p)) return 0;
            tp.line = tgt;
            tp.offset = lines[tgt].body;
            emit(c, OP_GOSUB);
            emit_fixup(c, tp, 0);
            emit(c, vm.code_len + 1U); /* return PC right after OP_GOSUB */
            return 1;
        }
        if (take_word(p, "GET")) {
            int s1 = 0, s2 = 0, has_idx = 0, unsuf, aid;
            char aname[BASIC_NAME_MAX + 1], akind;
            if (p->token != '(' && !word(p, "STEP")) return 0; /* file GET -> fallback */
            if (!compile_gfx_coord_vm(c, p, &s1)) return 0;
            if (!take(p, '-')) return 0;
            if (!compile_gfx_coord_vm(c, p, &s2)) return 0;
            if (!take(p, ',')) return 0;
            unsuf = unsuffixed_name(p);
            if (!identifier(p, aname, &akind)) return 0;
            aid = named_variable(p, aname, akind, 1, 0, unsuf, 0);
            if (aid < 0 || error_message || variables[aid].kind == '$') return 0;
            if (take(p, '(')) {
                if (!compile_int_expr(c, p) || !take(p, ')')) return 0;
                has_idx = 1;
            }
            emit3(c, OP_GFX_GET, (unsigned)aid,
                  (unsigned)((s1 ? 1U : 0U) | (s2 ? 2U : 0U) | (has_idx ? 4U : 0U)));
            return end_statement(p);
        }
        break;

    case 'I':
        if (take_word(p, "IF")) {
            if (!compile_cond_bool(c, p)) return 0;
            if (take_word(p, "GOTO")) {
                unsigned tgt = target_label(p);
                unsigned patch_skip;
                Position tp;
                if (error_message || tgt >= line_count) return 0;
                emit(c, OP_JFALSE);
                patch_skip = vm.code_len;
                emit(c, 0);
                tp.line = tgt;
                tp.offset = lines[tgt].body;
                emit(c, OP_JMP);
                emit_fixup(c, tp, 0);
                if (!c->overflowed) vm.code[patch_skip] = (unsigned short)vm.code_len;
                return end_statement(p);
            }
            if (!take_word(p, "THEN")) return 0;
            if (!p->token) {
                /* Multi-line block IF */
                unsigned peer_idx;
                if (inline_depth) return 0;
                bidx = block_at_pos(at);
                if (bidx == BLOCK_MAX) return 0;
                peer_idx = blocks[bidx].peer;
                emit(c, OP_JFALSE);
                if (blocks[peer_idx].kind == B_ELSEIF)
                    emit_fixup(c, blocks[peer_idx].at, 1);
                else
                    emit_fixup(c, blocks[peer_idx].after, 0);
                return 1;
            }
            if (inline_depth >= 3) return 0;
            {
                unsigned patch_false, patch_end;
                emit(c, OP_JFALSE);
                patch_false = vm.code_len;
                emit(c, 0);
                if (!compile_inline_list(c, p, inline_depth + 1U, sel_blocks, sel_slots, sel_top))
                    return 0;
                if (take_word(p, "ELSE")) {
                    emit(c, OP_JMP);
                    patch_end = vm.code_len;
                    emit(c, 0);
                    if (!c->overflowed) vm.code[patch_false] = (unsigned short)vm.code_len;
                    if (!compile_inline_list(c, p, inline_depth + 1U, sel_blocks, sel_slots, sel_top))
                        return 0;
                    if (!c->overflowed) vm.code[patch_end] = (unsigned short)vm.code_len;
                } else {
                    if (!c->overflowed) vm.code[patch_false] = (unsigned short)vm.code_len;
                }
                return end_statement(p);
            }
        }
        break;

    case 'L':
        if (take_word(p, "LOOP")) {
            int lkind = C_ALWAYS;
            if (inline_depth) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX) return 0;
            if (take_word(p, "WHILE")) lkind = C_WHILE;
            else if (take_word(p, "UNTIL")) lkind = C_UNTIL;
            if (lkind == C_ALWAYS) {
                emit(c, OP_JMP);
                emit_fixup(c, blocks[blocks[bidx].peer].at, 0);
            } else {
                if (!compile_cond_bool(c, p)) return 0;
                emit(c, lkind == C_WHILE ? OP_JTRUE : OP_JFALSE);
                emit_fixup(c, blocks[blocks[bidx].peer].at, 0);
            }
            return end_statement(p);
        }
        if (take_word(p, "LINE")) {
            int s1 = 0, s2 = 0;
            unsigned flags = 0;
            if (word(p, "INPUT")) return 0; /* LINE INPUT -> fallback */
            if (p->token != '-') {
                flags |= 1U;
                if (!compile_gfx_coord_vm(c, p, &s1)) return 0;
                if (s1) flags |= 2U;
            }
            if (!take(p, '-')) return 0;
            if (!compile_gfx_coord_vm(c, p, &s2)) return 0;
            if (s2) flags |= 4U;
            if (take(p, ',')) {
                if (p->token != ',' && !end_statement(p)) {
                    if (!compile_int_expr(c, p)) return 0;
                    flags |= 8U;
                }
                if (take(p, ',')) {
                    if (take_word(p, "BF")) flags |= (2U << 4);
                    else if (take_word(p, "B")) flags |= (1U << 4);
                    else if (!end_statement(p)) return 0;
                }
            }
            emit2(c, OP_GFX_LINE, flags);
            return end_statement(p);
        }
        if (take_word(p, "LOCATE")) {
            unsigned mask = 0;
            if (p->token != ',' && !end_statement(p)) {
                if (!compile_int_expr(c, p)) return 0;
                mask |= 1U;
            }
            if (take(p, ',')) {
                if (p->token != ',' && !end_statement(p)) {
                    if (!compile_int_expr(c, p)) return 0;
                    mask |= 2U;
                }
                if (take(p, ',')) {
                    if (p->token != ',' && !end_statement(p)) {
                        if (!compile_int_expr(c, p)) return 0;
                        mask |= 4U;
                    }
                    /* Extra cursor start/stop args -> fallback */
                    if (p->token == ',') return 0;
                }
            }
            emit2(c, OP_CON_LOCATE, mask);
            return end_statement(p);
        }
        break;

    case 'N':
        if (take_word(p, "NEXT")) {
            int vid = -1;
            if (inline_depth) return 0;
            if (!end_statement(p)) {
                char vname[BASIC_NAME_MAX + 1], vkind;
                int unsuf = unsuffixed_name(p);
                if (!identifier(p, vname, &vkind)) return 0;
                vid = named_variable(p, vname, vkind, 0, 0, unsuf, 0);
                if (vid < 0 || error_message) return 0;
                if (p->token == ',') return 0; /* NEXT j, i -> fallback */
            }
            emit2(c, OP_FOR_NEXT, vid < 0 ? 0xffffU : (unsigned)vid);
            return end_statement(p);
        }
        break;

    case 'P':
        if (take_word(p, "PUT")) {
            int step = 0, has_idx = 0, unsuf, aid, action = CKB_PUT_XOR;
            char aname[BASIC_NAME_MAX + 1], akind;
            if (p->token != '(' && !word(p, "STEP")) return 0; /* file PUT -> fallback */
            if (!compile_gfx_coord_vm(c, p, &step)) return 0;
            if (!take(p, ',')) return 0;
            unsuf = unsuffixed_name(p);
            if (!identifier(p, aname, &akind)) return 0;
            aid = named_variable(p, aname, akind, 1, 0, unsuf, 0);
            if (aid < 0 || error_message || variables[aid].kind == '$') return 0;
            if (take(p, '(')) {
                if (!compile_int_expr(c, p) || !take(p, ')')) return 0;
                has_idx = 1;
            }
            if (take(p, ',')) {
                if (take_word(p, "PSET")) action = CKB_PUT_PSET;
                else if (take_word(p, "PRESET")) action = CKB_PUT_PRESET;
                else if (take_word(p, "AND")) action = CKB_PUT_AND;
                else if (take_word(p, "OR")) action = CKB_PUT_OR;
                else if (take_word(p, "XOR")) action = CKB_PUT_XOR;
                else return 0;
            }
            emit3(c, OP_GFX_PUT, (unsigned)aid,
                  (unsigned)((step ? 1U : 0U) | (has_idx ? 2U : 0U) | ((unsigned)action << 4)));
            return end_statement(p);
        }
        if (take_word(p, "PRINT")) {
        do_console_print:
            if (p->token == '#' || word(p, "USING")) return 0; /* file / USING -> fallback */
            {
                int trailing = 0;
                while (!end_statement(p)) {
                    if (take(p, ';')) {
                        trailing = 1;
                    } else if (take(p, ',')) {
                        emit2(c, OP_PRINT_ITEM, 2U);
                        trailing = 1;
                    } else if (word(p, "TAB") || word(p, "SPC")) {
                        return 0; /* TAB/SPC -> fallback */
                    } else {
                        char pk = 0;
                        trailing = 0;
                        if (!compile_expr(c, p, 1, &pk) || record_size(pk)) return 0;
                        emit2(c, OP_PRINT_ITEM, 0U);
                        if (!end_statement(p) && p->token != ';' && p->token != ',')
                            return 0;
                    }
                }
                emit2(c, OP_PRINT_END, (unsigned)(trailing ? 0U : 1U));
                return 1;
            }
        }
        if (word(p, "PSET") || word(p, "PRESET")) {
            int is_pre = word(p, "PRESET"), step = 0;
            unsigned flags = is_pre ? 4U : 0U;
            scan(p);
            if (!compile_gfx_coord_vm(c, p, &step)) return 0;
            if (step) flags |= 1U;
            if (take(p, ',') && !end_statement(p)) {
                if (!compile_int_expr(c, p)) return 0;
                flags |= 2U;
            }
            emit2(c, OP_GFX_PSET, flags);
            return end_statement(p);
        }
        if (take_word(p, "PAINT")) {
            int step = 0;
            unsigned flags = 0;
            if (!compile_gfx_coord_vm(c, p, &step)) return 0;
            if (step) flags |= 1U;
            if (take(p, ',')) {
                if (p->token != ',' && !end_statement(p)) {
                    if (!compile_int_expr(c, p)) return 0;
                    flags |= 2U;
                }
                if (take(p, ',')) {
                    if (p->token != ',' && !end_statement(p)) {
                        if (!compile_int_expr(c, p)) return 0;
                        flags |= 4U;
                    }
                    if (p->token == ',') return 0;
                }
            }
            emit2(c, OP_GFX_PAINT, flags);
            return end_statement(p);
        }
        if (take_word(p, "PALETTE")) {
            if (end_statement(p)) {
                emit2(c, OP_CON_PALETTE, 0U);
                return 1;
            }
            if (!compile_int_expr(c, p) || !take(p, ',') || !compile_int_expr(c, p))
                return 0;
            emit2(c, OP_CON_PALETTE, 1U);
            return end_statement(p);
        }
        break;

    case 'R':
        if (take_word(p, "RETURN")) {
            if (end_statement(p)) {
                emit2(c, OP_RETURN, 0xffffU);
                return 1;
            }
            {
                unsigned tgt = target_label(p);
                Position tp;
                if (error_message || tgt >= line_count) return 0;
                tp.line = tgt;
                tp.offset = lines[tgt].body;
                emit(c, OP_RETURN);
                emit_fixup(c, tp, 0);
                return end_statement(p);
            }
        }
        break;

    case 'S':
        if (take_word(p, "SELECT")) {
            char sk = 0;
            unsigned slot, peer_idx, finish_idx;
            if (inline_depth || !take_word(p, "CASE")) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX || *sel_top >= VM_SEL_MAX) return 0;
            if (!compile_expr(c, p, 1, &sk) || record_size(sk)) return 0;
            slot = *sel_top;
            sel_blocks[*sel_top] = bidx;
            sel_slots[*sel_top] = slot;
            ++(*sel_top);
            peer_idx = blocks[bidx].peer;
            finish_idx = blocks[bidx].finish;
            emit2(c, OP_SEL_PUSH, slot);
            if (blocks[peer_idx].kind == B_CASE)
                emit_fixup(c, blocks[peer_idx].at, 1);
            else
                emit_fixup(c, blocks[finish_idx].after, 0);
            return end_statement(p);
        }
        if (take_word(p, "SCREEN")) {
            if (p->token == ',' || end_statement(p)) return 0;
            if (!compile_int_expr(c, p)) return 0;
            if (p->token == ',') return 0; /* extra page args -> fallback */
            emit(c, OP_CON_SCREEN);
            return end_statement(p);
        }
        if (take_word(p, "SHARED")) {
            p->token = 0;
            p->pos = lines[p->line].end;
            return 1;
        }
        if (take_word(p, "SYSTEM")) {
            emit(c, OP_HALT);
            return end_statement(p);
        }
        if (take_word(p, "STOP")) {
            emit(c, OP_STOP_STMT);
            return end_statement(p);
        }
        break;

    case 'V':
        if (take_word(p, "VIEW")) {
            if (!take_word(p, "PRINT")) return 0;
            if (end_statement(p)) {
                emit2(c, OP_CON_VIEW_PRINT, 0U);
                return 1;
            }
            if (!compile_int_expr(c, p) || !take_word(p, "TO") || !compile_int_expr(c, p))
                return 0;
            emit2(c, OP_CON_VIEW_PRINT, 1U);
            return end_statement(p);
        }
        break;

    case 'W':
        if (take_word(p, "WHILE")) {
            if (inline_depth) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX) return 0;
            if (!compile_cond_bool(c, p)) return 0;
            emit(c, OP_JFALSE);
            emit_fixup(c, blocks[blocks[bidx].peer].after, 0);
            return end_statement(p);
        }
        if (take_word(p, "WEND")) {
            if (inline_depth) return 0;
            bidx = block_at_pos(at);
            if (bidx == BLOCK_MAX) return 0;
            emit(c, OP_JMP);
            emit_fixup(c, blocks[blocks[bidx].peer].at, 0);
            return end_statement(p);
        }
        break;

    default:
        break;
    }

    /* Check bare SUB call vs assignment */
    if (procedure_count && p->token == T_NAME) {
        unsigned look = p->pos;
        while (look < p->line_end && (source[look] == ' ' || source[look] == '\t')) ++look;
        if (!(look < p->line_end && source[look] == '=')) {
            int pid = lookup_proc_token(p);
            if (pid >= 0 && !procedures[pid].function) {
                scan(p);
                if (!compile_proc_call_args(c, p, pid, 0, 0)) return 0;
                return end_statement(p);
            }
        }
    }

    return compile_assignment_vm(c, p);
}

unsigned resolve_pos_pc(Position pos)
{
    unsigned lo, hi;
    if (pos.line >= line_count) return vm.line_pc[line_count];
    if (pos.offset <= lines[pos.line].body) return vm.line_pc[pos.line];
    lo = 0;
    hi = vm.stmt_len;
    while (lo < hi) {
        unsigned mid = lo + ((hi - lo) >> 1);
        if (vm.stmts[mid].line < pos.line ||
            (vm.stmts[mid].line == pos.line && vm.stmts[mid].offset < pos.offset))
            lo = mid + 1U;
        else
            hi = mid;
    }
    if (lo < vm.stmt_len && vm.stmts[lo].line == pos.line && vm.stmts[lo].offset == pos.offset)
        return vm.stmts[lo].start_pc;
    return vm.line_pc[pos.line];
}

static void record_stmt_span(VmCompiler *c, unsigned pc, unsigned line, unsigned offset)
{
    if (vm.stmt_len >= vm.stmt_cap) {
        c->overflowed = 1;
        return;
    }
    vm.stmts[vm.stmt_len].start_pc = pc;
    vm.stmts[vm.stmt_len].line = line;
    vm.stmts[vm.stmt_len].offset = offset;
    ++vm.stmt_len;
}

static void advance_default_type_compile(Parser *p, char kind)
{
    int first, last;
    do {
        int ch = toupper((unsigned char)source[p->start]);
        if (p->token != T_NAME || p->length != 1 || ch < 'A' || ch > 'Z') return;
        first = ch;
        scan(p);
        if (take(p, '-')) {
            ch = toupper((unsigned char)source[p->start]);
            if (p->token != T_NAME || p->length != 1 || ch < 'A' || ch > 'Z') return;
            last = ch;
            scan(p);
        } else {
            last = first;
        }
        for (; first <= last; ++first) default_kind[first - 'A'] = kind;
    } while (take(p, ','));
}

int compile_module(void)
{
    VmCompiler comp;
    unsigned line, i;
    unsigned proc_skip_patch[PROC_MAX];
    unsigned sel_blocks[VM_SEL_MAX], sel_slots[VM_SEL_MAX], sel_top = 0;
    int saved_scope = current_scope;
    char saved_defaults[26];

    memset(&comp, 0, sizeof(comp));
    vm_free_module();

    vm.code_cap = line_count * 18U + 1024U;
    if (vm.code_cap > VM_MAX_CODE) vm.code_cap = VM_MAX_CODE;
    vm.stmt_cap = line_count * 2U + 128U;
    if (vm.stmt_cap > VM_MAX_STMTS) vm.stmt_cap = VM_MAX_STMTS;
    comp.fixup_cap = vm.stmt_cap + 256U;
    if (comp.fixup_cap > VM_MAX_FIXUPS) comp.fixup_cap = VM_MAX_FIXUPS;

    vm.code = (unsigned short __far *)_fmalloc(vm.code_cap * sizeof(unsigned short));
    vm.fconsts = (double __far *)_fmalloc(VM_MAX_FCONSTS * sizeof(double));
    vm.line_pc = (unsigned short __far *)_fmalloc((line_count + 1U) * sizeof(unsigned short));
    vm.stmts = (StmtSpan __far *)_fmalloc(vm.stmt_cap * sizeof(StmtSpan));
    vm.fnframes = def_fn_count ? (VmDefFnFrame __far *)_fmalloc(DEPTH_MAX * sizeof(VmDefFnFrame)) : NULL;
    vm.sel_str_buf = (char __far *)_fmalloc(4U * 256U);
    comp.fixups = (VmFixup __far *)_fmalloc(comp.fixup_cap * sizeof(VmFixup));

    if (!vm.code || !vm.fconsts || !vm.line_pc || !vm.stmts || !vm.sel_str_buf ||
        (def_fn_count && !vm.fnframes) || !comp.fixups) {
        if (comp.fixups) _ffree(comp.fixups);
        vm_free_module();
        return 0;
    }

    memcpy(saved_defaults, default_kind, sizeof(saved_defaults));
    memset(default_kind, '!', sizeof(default_kind));
    for (i = 0; i < PROC_MAX; ++i) proc_skip_patch[i] = 0xffffU;

    for (line = 0; line < line_count && !comp.overflowed && !error_message; ++line) {
        int scope = line_scope[line];
        unsigned rtype;
        Parser p;

        vm.line_pc[line] = (unsigned short)vm.code_len;

        /* Skip TYPE ... END TYPE lines */
        for (rtype = 0; rtype < record_count; ++rtype) {
            if (line >= records[rtype].line && line <= records[rtype].end)
                break;
        }
        if (rtype < record_count) continue;

        /* Procedure header: emit jump around procedure body for module fallthrough */
        if (scope >= 0 && line == procedures[scope].line) {
            emit(&comp, OP_JMP);
            proc_skip_patch[scope] = vm.code_len;
            emit(&comp, 0);
            vm.proc_entry_pc[scope] = vm.code_len;
            sel_top = 0;
            continue;
        }

        current_scope = scope;
        init_parser_at(&p, line, lines[line].body);

        while (p.token && !comp.overflowed && !error_message) {
            Position at;
            unsigned stmt_pc, saved_fconst_len, saved_fixup_len;
            if (p.token == ':') {
                scan(&p);
                continue;
            }
            at.line = line;
            at.offset = p.start;
            stmt_pc = vm.code_len;
            saved_fconst_len = vm.fconst_len;
            saved_fixup_len = comp.fixup_len;
            record_stmt_span(&comp, stmt_pc, line, at.offset);

            /* Track DEFINT..DEFSTR during compile so subsequent lines get right kinds */
            if (p.token == T_NAME && p.first_upper == 'D') {
                Parser dp = p;
                if (take_word(&dp, "DEFINT")) advance_default_type_compile(&dp, '%');
                else if (take_word(&dp, "DEFLNG")) advance_default_type_compile(&dp, '&');
                else if (take_word(&dp, "DEFSNG")) advance_default_type_compile(&dp, '!');
                else if (take_word(&dp, "DEFDBL")) advance_default_type_compile(&dp, '#');
                else if (take_word(&dp, "DEFSTR")) advance_default_type_compile(&dp, '$');
            }

            if (!compile_stmt(&comp, &p, at, 0, sel_blocks, sel_slots, &sel_top) || error_message) {
                /* Roll back partial instructions and emit clean OP_FALLBACK */
                error_message = NULL;
                vm.code_len = stmt_pc;
                vm.fconst_len = saved_fconst_len;
                comp.fixup_len = saved_fixup_len;
                emit3(&comp, OP_FALLBACK, line, at.offset);
                init_parser_at(&p, line, at.offset);
                /* Advance past this statement to ':' or EOL */
                if (word(&p, "IF")) {
                    /* Single-line IF has colons/ELSE extending to EOL */
                    p.token = 0;
                    p.pos = lines[line].end;
                } else {
                    while (p.token && p.token != ':') scan(&p);
                }
            }
            if (p.token == ':') scan(&p);
        }

        if (scope >= 0 && line == procedures[scope].end) {
            emit(&comp, OP_RET_PROC);
            if (proc_skip_patch[scope] != 0xffffU && !comp.overflowed)
                vm.code[proc_skip_patch[scope]] = (unsigned short)vm.code_len;
            sel_top = 0;
        }
    }

    vm.line_pc[line_count] = (unsigned short)vm.code_len;
    emit(&comp, OP_HALT);

    /* Compile DEF FN bodies at end of bytecode */
    current_scope = -1;
    for (i = 0; i < def_fn_count && !comp.overflowed && !error_message; ++i) {
        DefFn __far *fn = &def_fns[i];
        unsigned pi;
        Parser fp;
        char fk = 0;
        for (pi = 0; pi < fn->param_count; ++pi) {
            if (fn->param_var_ids[pi] < 0) {
                char pname[16];
                Parser dp;
                memset(&dp, 0, sizeof(dp));
                _fmemcpy(pname, fn->param_names[pi], 16);
                fn->param_var_ids[pi] = named_variable(&dp, pname, fn->param_kinds[pi], 0, 0, 0, 0);
            }
        }
        vm.deffn_entry_pc[i] = vm.code_len;
        init_parser_at(&fp, fn->expr_at.line, fn->expr_at.offset);
        if (!compile_expr(&comp, &fp, 1, &fk) || error_message ||
            ((fk == '$') != (fn->result_kind == '$'))) {
            comp.overflowed = 1;
            break;
        }
        if (fk != '$') emit_coerce_to(&comp, fk, fn->result_kind);
        emit(&comp, OP_RET_DEFFN);
    }

    current_scope = saved_scope;
    memcpy(default_kind, saved_defaults, sizeof(default_kind));

    if (comp.overflowed || error_message) {
        error_message = NULL;
        _ffree(comp.fixups);
        vm_free_module();
        return 0;
    }

    /* Resolve all Position -> PC fixups */
    for (i = 0; i < comp.fixup_len; ++i) {
        Position tp;
        unsigned target_pc;
        tp.line = comp.fixups[i].line;
        tp.offset = comp.fixups[i].offset;
        target_pc = resolve_pos_pc(tp);
        if (comp.fixups[i].plus_two) target_pc += 2U;
        vm.code[comp.fixups[i].patch_pc] = (unsigned short)target_pc;
    }

    _ffree(comp.fixups);
    return 1;
}

