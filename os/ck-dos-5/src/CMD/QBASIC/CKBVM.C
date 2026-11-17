/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Native-typed 16-bit bytecode virtual machine for CK Basic (QBASIC.EXE).
 */

#include "CKBVMP.H"

static int vm_ints_live;

static void vm_sync_var_to_double(Variable *v)
{
    if ((v->kind == '%' || v->kind == '&') && !v->array && !v->reference.target) {
        long lv = VAR_IVAL(v);
        v->number = (double)lv;
    }
}

static void vm_sync_var_to_int(Variable *v)
{
    if ((v->kind == '%' || v->kind == '&') && !v->array && !v->reference.target) {
        double d = v->number;
        long lv = try_exact_int21(d, &lv) ? lv : (long)d;
        VAR_IVAL(v) = lv;
    }
}

static void vm_sync_active_frames(int to_double)
{
    unsigned i, p_idx;
    Activation *act;
    if (to_double) {
        if (!vm_ints_live) return;
        vm_ints_live = 0;
    } else {
        if (vm_ints_live) return;
        vm_ints_live = 1;
    }
    for (i = 0; i < variable_count; ++i) {
        if (variables[i].frame_slot) continue;
        if (to_double) vm_sync_var_to_double(&variables[i]);
        else vm_sync_var_to_int(&variables[i]);
    }
    for (act = activation; act; act = act->prev) {
        if (!act->locals) continue;
        for (p_idx = 0; p_idx < procedure_count; ++p_idx) {
            unsigned n_loc = procedures[p_idx].locals;
            unsigned fv = procedures[p_idx].first_var, lv = procedures[p_idx].last_var;
            if (!n_loc) continue;
            for (i = fv; i < lv; ++i) {
                unsigned sl = variables[i].frame_slot;
                if (variables[i].scope == (int)p_idx && sl && sl <= n_loc) {
                    Variable *loc = &act->locals[sl - 1];
                    if (loc->name_slot == variables[i].name_slot) {
                        if (to_double) vm_sync_var_to_double(loc);
                        else vm_sync_var_to_int(loc);
                    }
                }
            }
        }
    }
}

/* =========================================================================
 * Virtual Machine Execution Engine
 * ========================================================================= */

static unsigned find_stmt_for_pc(unsigned pc)
{
    unsigned lo = 0, hi = vm.stmt_len;
    if (!hi) return 0;
    while (lo + 1U < hi) {
        unsigned mid = lo + ((hi - lo) >> 1);
        if (vm.stmts[mid].start_pc <= pc) lo = mid;
        else hi = mid;
    }
    return lo;
}

static long vm_read_ref_int(Reference ref)
{
    Variable *v = ref.target;
    if (record_size(v->kind)) {
        Parser dp;
        double d = 0;
        char *item;
        memset(&dp, 0, sizeof(dp));
        item = record_data(&dp, ref);
        if (!item) return 0;
        packed_number(item, ref.kind, &d, 0);
        return (long)d;
    }
    if (v->array) {
        char *item = (char *)v->elements + ref.cell * array_element_size(v);
        if (ref.kind == '%') {
            unsigned u = (unsigned char)item[0] | ((unsigned)(unsigned char)item[1] << 8);
            return (u & 0x8000U) ? ((long)u - 65536L) : (long)u;
        } else {
            unsigned long u = (unsigned long)(unsigned char)item[0] |
                              ((unsigned long)(unsigned char)item[1] << 8) |
                              ((unsigned long)(unsigned char)item[2] << 16) |
                              ((unsigned long)(unsigned char)item[3] << 24);
            return (u & 0x80000000UL) ? (-(long)(~u) - 1L) : (long)u;
        }
    }
    return VAR_IVAL(v);
}

static int vm_write_ref_int(Reference ref, long val)
{
    Variable *v = ref.target;
    if (ref.kind == '%' && (val < -32768L || val > 32767L)) {
        error_message = "Overflow";
        return 0;
    }
    if (record_size(v->kind)) {
        Parser dp;
        double d = (double)val;
        char *item;
        memset(&dp, 0, sizeof(dp));
        item = record_data(&dp, ref);
        if (!item) return 0;
        packed_number(item, ref.kind, &d, 1);
        return 1;
    }
    if (v->array) {
        char *item = (char *)v->elements + ref.cell * array_element_size(v);
        unsigned long u = (unsigned long)val;
        item[0] = (char)u;
        item[1] = (char)(u >> 8);
        if (ref.kind == '&') {
            item[2] = (char)(u >> 16);
            item[3] = (char)(u >> 24);
        }
        return 1;
    }
    VAR_IVAL(v) = val;
    return 1;
}

static double vm_read_ref_flt(Reference ref)
{
    Variable *v = ref.target;
    double d = 0;
    if (record_size(v->kind)) {
        Parser dp;
        char *item;
        memset(&dp, 0, sizeof(dp));
        item = record_data(&dp, ref);
        if (!item) return 0.0;
        packed_number(item, ref.kind, &d, 0);
        return d;
    }
    if (v->array) {
        char *item = (char *)v->elements + ref.cell * array_element_size(v);
        packed_number(item, ref.kind, &d, 0);
        return d;
    }
    return v->number;
}

static int vm_write_ref_flt(Reference ref, double val)
{
    Variable *v = ref.target;
    if (!finite_number(val) ||
        (ref.kind == '!' && (val < -3.4028234663852886e38 || val > 3.4028234663852886e38))) {
        error_message = "Overflow";
        return 0;
    }
    if (ref.kind == '!') val = ckb_single_precision(val);
    if (record_size(v->kind)) {
        Parser dp;
        char *item;
        memset(&dp, 0, sizeof(dp));
        item = record_data(&dp, ref);
        if (!item) return 0;
        packed_number(item, ref.kind, &val, 1);
        return 1;
    }
    if (v->array) {
        char *item = (char *)v->elements + ref.cell * array_element_size(v);
        packed_number(item, ref.kind, &val, 1);
        return 1;
    }
    v->number = val;
    return 1;
}

static void vm_read_var_str(const Variable *v, unsigned cell, VmVal *out)
{
    const char *stored = v->array ? ((char *const *)v->elements)[cell] : v->text;
    unsigned w;
    const char *fld = any_field_bound ? field_data(v, &w) : NULL;
    out->kind = '$';
    out->is_ref = 0;
    if (fld) {
        out->u.fs = (const char __far *)fld;
        out->length = w;
    } else if (v->fixed_width) {
        if (stored) {
            out->u.fs = (const char __far *)stored;
            out->length = v->fixed_width;
        } else {
            CkbValue tmp;
            char *b;
            INIT_VALUE(&tmp);
            b = ckb_string_buffer(&tmp, v->fixed_width);
            if (b) {
                memset(b, 0, v->fixed_width);
                out->u.fs = (const char __far *)b;
                out->length = v->fixed_width;
            } else {
                error_message = "Out of memory";
                out->u.fs = (const char __far *)"";
                out->length = 0;
            }
        }
    } else if (stored) {
        out->length = (unsigned char)stored[0] | ((unsigned)(unsigned char)stored[1] << 8);
        out->u.fs = (const char __far *)(stored + 4);
    } else {
        out->u.fs = (const char __far *)"";
        out->length = 0;
    }
}

static void vm_store_var_str(Variable *v, unsigned cell, const char __far *fs, unsigned len)
{
    Parser dp;
    memset(&dp, 0, sizeof(dp));
    variable_string_store_raw(&dp, v, cell, len ? fs : (const char __far *)"", len);
}

static int vm_cmp_far_str(const char __far *a, unsigned alen,
                          const char __far *b, unsigned blen)
{
    unsigned n = alen < blen ? alen : blen, i;
    for (i = 0; i < n; ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return (alen > blen) - (alen < blen);
}

static int vm_eval_rel(int cmp, int op)
{
    switch (op) {
    case '=': return cmp == 0;
    case '<': return cmp < 0;
    case '>': return cmp > 0;
    case T_LE: return cmp <= 0;
    case T_GE: return cmp >= 0;
    default: return cmp != 0;
    }
}

static int vm_float_to_long_even(double v, long *out)
{
    double r;
    if (try_exact_int21(v, out)) return 1;
    if (v >= -2147483648.0 && v <= 2147483647.0) {
        long lv = (long)v;
        if ((double)lv == v) {
            *out = lv;
            return 1;
        }
    }
    r = round_even(v);
    if (!(r >= -2147483648.0 && r <= 2147483647.0)) {
        error_message = "Overflow";
        return 0;
    }
    *out = (long)r;
    return 1;
}

static Variable *vm_ready_array(int id, unsigned dims)
{
    Variable *v = array_storage(id);
    if (!v->elements && v->dynamic != ARRAY_DYNAMIC) {
        Parser dp;
        memset(&dp, 0, sizeof(dp));
        implicit_array(&dp, v, dims);
    }
    if (!v->elements || v->dimensions != dims) {
        error_message = "Subscript out of range";
        return NULL;
    }
    return v;
}

static void vm_load_elem(Variable *v, unsigned cell, VmVal *dst)
{
    char k = v->kind;
    char *item;
    dst->kind = k;
    dst->is_ref = 0;
    if (k == '$') {
        vm_read_var_str(v, cell, dst);
        return;
    }
    item = (char *)v->elements + cell * array_element_size(v);
    if (k == '%') {
        unsigned u = (unsigned char)item[0] | ((unsigned)(unsigned char)item[1] << 8);
        dst->u.i = (u & 0x8000U) ? ((long)u - 65536L) : (long)u;
    } else if (k == '&') {
        unsigned long u = (unsigned long)(unsigned char)item[0] |
                          ((unsigned long)(unsigned char)item[1] << 8) |
                          ((unsigned long)(unsigned char)item[2] << 16) |
                          ((unsigned long)(unsigned char)item[3] << 24);
        dst->u.i = (u & 0x80000000UL) ? (-(long)(~u) - 1L) : (long)u;
    } else {
        packed_number(item, k, &dst->u.f, 0);
    }
}

static void vm_store_elem(Variable *v, unsigned cell, const VmVal *src)
{
    char k = v->kind;
    char *item;
    if (k == '$') {
        vm_store_var_str(v, cell, src->u.fs, src->length);
        return;
    }
    item = (char *)v->elements + cell * array_element_size(v);
    if (k == '%') {
        unsigned u = (unsigned)src->u.i;
        item[0] = (char)u;
        item[1] = (char)(u >> 8);
    } else if (k == '&') {
        unsigned long u = (unsigned long)src->u.i;
        item[0] = (char)u;
        item[1] = (char)(u >> 8);
        item[2] = (char)(u >> 16);
        item[3] = (char)(u >> 24);
    } else {
        double d = src->u.f;
        packed_number(item, k, &d, 1);
    }
}

int ckb_vm_execute(void)
{
    VmVal vstack[VM_STACK_MAX];
    VmVal sel_vals[VM_SEL_MAX];
    VmProcFrame pframes[STACK_MAX];
    unsigned sp = 0, sp_base = 0, fn_depth = 0;
    unsigned pc = 0, cur_op_pc = 0;
    unsigned short __far *code;
    CkbStringTemp *str_base;

    if (!compile_module()) return 0;

    vm_ints_live = 0;
    vm_sync_active_frames(0);
    code = vm.code;
    str_base = ckb_string_mark();

    while (!stopped) {
        unsigned op;

        if (error_message) {
            unsigned sidx = find_stmt_for_pc(cur_op_pc);
            Position err_at, after_at;
            err_at.line = vm.stmts[sidx].line;
            err_at.offset = vm.stmts[sidx].offset;
            if (sidx + 1U < vm.stmt_len) {
                after_at.line = vm.stmts[sidx + 1U].line;
                after_at.offset = vm.stmts[sidx + 1U].offset;
            } else {
                after_at.line = line_count;
                after_at.offset = 0;
            }
            first_error = err_at;
            sp = sp_base;
            if (ckb_string_mark() != str_base) ckb_string_release(str_base);

            if (!handling_error && error_handler[current_scope + 1] < line_count) {
                unsigned handler = error_handler[current_scope + 1];
                error_resume = err_at;
                error_resume_next = after_at;
                handling_error = 1;
                resume_scope = current_scope;
                trapped_error_code = runtime_error_code(error_message);
                trapped_error_line = error_line_number(first_error.line);
                error_message = NULL;
                if (line_scope[handler] < 0) {
                    while (procedure_depth > 0) {
                        VmProcFrame *pf = &pframes[--procedure_depth];
                        free_proc_locals(pf->frame.locals, pf->n_locals, pf->pooled_locals);
                        activation = pf->frame.prev;
                        for_depth = pf->frame.for_base;
                        call_depth = pf->frame.call_base;
                    }
                    current_scope = -1;
                }
                pc = vm.line_pc[handler];
                continue;
            }
            if (pause_execution) {
                int cont;
                vm_sync_active_frames(1);
                cont = pause_at_pos(err_at, error_message);
                vm_sync_active_frames(0);
                if (cont) {
                    error_message = NULL;
                    pc = vm.stmts[sidx].start_pc;
                    continue;
                }
            }
            break;
        }

        if (sp == sp_base && ckb_string_mark() != str_base)
            ckb_string_release(str_base);

        cur_op_pc = pc;
        op = code[pc++];

        switch (op) {
        case OP_NOP:
            break;

        case OP_HALT:
            stopped = 1;
            break;

        case OP_STOP_STMT: {
            unsigned sidx = find_stmt_for_pc(cur_op_pc);
            Position sat;
            sat.line = vm.stmts[sidx].line;
            sat.offset = vm.stmts[sidx].offset;
            if (pause_execution) {
                vm_sync_active_frames(1);
                pause_at_pos(sat, NULL);
                vm_sync_active_frames(0);
            } else {
                stopped = 1;
                first_error = sat;
                error_message = "Program stopped";
            }
            break;
        }

        case OP_JMP:
            pc = code[pc];
            break;

        case OP_JFALSE: {
            unsigned tgt = code[pc++];
            VmVal *v = &vstack[--sp];
            int truth = (v->kind == '%' || v->kind == '&') ? (v->u.i != 0L) : (v->u.f != 0.0);
            if (!truth) pc = tgt;
            break;
        }

        case OP_JTRUE: {
            unsigned tgt = code[pc++];
            VmVal *v = &vstack[--sp];
            int truth = (v->kind == '%' || v->kind == '&') ? (v->u.i != 0L) : (v->u.f != 0.0);
            if (truth) pc = tgt;
            break;
        }

        case OP_GOSUB: {
            unsigned tgt = code[pc++];
            unsigned ret_pc = code[pc++];
            if (call_depth >= STACK_MAX) {
                error_message = "Too many nested GOSUBs";
                break;
            }
            /* Encode VM return PC cleanly with line = 0xffffU marker */
            calls[call_depth].line = 0xffffU;
            calls[call_depth].offset = ret_pc;
            ++call_depth;
            pc = tgt;
            break;
        }

        case OP_RETURN: {
            unsigned tgt = code[pc++];
            unsigned base = activation ? activation->call_base : 0;
            if (call_depth <= base) {
                error_message = "RETURN without GOSUB";
                break;
            }
            --call_depth;
            if (tgt != 0xffffU) pc = tgt;
            else if (calls[call_depth].line == 0xffffU) pc = calls[call_depth].offset;
            else pc = resolve_pos_pc(calls[call_depth]);
            break;
        }

        case OP_FALLBACK: {
            Position fat, after_pos;
            fat.line = code[pc++];
            fat.offset = code[pc++];
            vm_sync_active_frames(1);
            exec_fallback_stmt(fat, &after_pos);
            vm_sync_active_frames(0);
            if (!error_message && !stopped) {
                if (returning) {
                    returning = 0;
                    goto do_ret_proc;
                }
                if (jumped) {
                    /* Handle RESUME into a procedure vs module, or GOTO/GOSUB */
                    pc = resolve_pos_pc(after_pos);
                } else {
                    /* Continue at next bytecode instruction unless jumped */
                }
            }
            break;
        }

        case OP_LIT_I16: {
            VmVal *v = &vstack[sp++];
            v->kind = '%';
            v->is_ref = 0;
            v->u.i = (long)(short)code[pc++];
            break;
        }

        case OP_LIT_I32: {
            unsigned lo = code[pc++];
            unsigned hi = code[pc++];
            unsigned long u = (unsigned long)lo | ((unsigned long)hi << 16);
            VmVal *v = &vstack[sp++];
            v->kind = '&';
            v->is_ref = 0;
            v->u.i = (u & 0x80000000UL) ? (-(long)(~u) - 1L) : (long)u;
            break;
        }

        case OP_LIT_F64: {
            unsigned raw = code[pc++];
            VmVal *v = &vstack[sp++];
            v->kind = (raw & 0x8000U) ? '!' : '#';
            v->is_ref = 0;
            v->u.f = vm.fconsts[raw & 0x7fffU];
            break;
        }

        case OP_LIT_STR: {
            unsigned soff = code[pc++];
            unsigned slen = code[pc++];
            VmVal *v = &vstack[sp++];
            v->kind = '$';
            v->is_ref = 0;
            v->u.fs = source + soff;
            v->length = slen;
            break;
        }

        case OP_LOAD_GINT: {
            unsigned id = code[pc++];
            VmVal *v = &vstack[sp++];
            v->kind = variables[id].kind;
            v->is_ref = 0;
            v->u.i = VAR_IVAL(&variables[id]);
            break;
        }

        case OP_LOAD_LINT: {
            unsigned id = code[pc++];
            Variable *sv = &activation->locals[variables[id].frame_slot - 1];
            VmVal *v = &vstack[sp++];
            v->kind = variables[id].kind;
            v->is_ref = 0;
            v->u.i = sv->reference.target ? vm_read_ref_int(sv->reference) : VAR_IVAL(sv);
            break;
        }

        case OP_LOAD_GFLT: {
            unsigned id = code[pc++];
            VmVal *v = &vstack[sp++];
            v->kind = variables[id].kind;
            v->is_ref = 0;
            v->u.f = variables[id].number;
            break;
        }

        case OP_LOAD_LFLT: {
            unsigned id = code[pc++];
            Variable *sv = &activation->locals[variables[id].frame_slot - 1];
            VmVal *v = &vstack[sp++];
            v->kind = variables[id].kind;
            v->is_ref = 0;
            v->u.f = sv->reference.target ? vm_read_ref_flt(sv->reference) : sv->number;
            break;
        }

        case OP_LOAD_GSTR: {
            unsigned id = code[pc++];
            vm_read_var_str(&variables[id], 0, &vstack[sp++]);
            break;
        }

        case OP_LOAD_LSTR: {
            unsigned id = code[pc++];
            Variable *sv = &activation->locals[variables[id].frame_slot - 1];
            if (sv->reference.target)
                vm_read_var_str(sv->reference.target, sv->reference.cell, &vstack[sp++]);
            else
                vm_read_var_str(sv, 0, &vstack[sp++]);
            break;
        }

        case OP_STORE_GINT: {
            unsigned id = code[pc++];
            long val = vstack[--sp].u.i;
            if (variables[id].kind == '%' && (val < -32768L || val > 32767L))
                error_message = "Overflow";
            else
                VAR_IVAL(&variables[id]) = val;
            break;
        }

        case OP_STORE_LINT: {
            unsigned id = code[pc++];
            long val = vstack[--sp].u.i;
            Variable *sv = &activation->locals[variables[id].frame_slot - 1];
            if (sv->reference.target) {
                vm_write_ref_int(sv->reference, val);
            } else if (variables[id].kind == '%' && (val < -32768L || val > 32767L)) {
                error_message = "Overflow";
            } else {
                VAR_IVAL(sv) = val;
            }
            break;
        }

        case OP_STORE_GFLT: {
            unsigned id = code[pc++];
            double val = vstack[--sp].u.f;
            char k = variables[id].kind;
            if (!finite_number(val) ||
                (k == '!' && (val < -3.4028234663852886e38 || val > 3.4028234663852886e38)))
                error_message = "Overflow";
            else
                variables[id].number = (k == '!') ? ckb_single_precision(val) : val;
            break;
        }

        case OP_STORE_LFLT: {
            unsigned id = code[pc++];
            double val = vstack[--sp].u.f;
            Variable *sv = &activation->locals[variables[id].frame_slot - 1];
            if (sv->reference.target) {
                vm_write_ref_flt(sv->reference, val);
            } else {
                char k = variables[id].kind;
                if (!finite_number(val) ||
                    (k == '!' && (val < -3.4028234663852886e38 || val > 3.4028234663852886e38)))
                    error_message = "Overflow";
                else
                    sv->number = (k == '!') ? ckb_single_precision(val) : val;
            }
            break;
        }

        case OP_STORE_GSTR: {
            unsigned id = code[pc++];
            VmVal *sv = &vstack[--sp];
            vm_store_var_str(&variables[id], 0, sv->u.fs, sv->length);
            break;
        }

        case OP_STORE_LSTR: {
            unsigned id = code[pc++];
            VmVal *val = &vstack[--sp];
            Variable *sv = &activation->locals[variables[id].frame_slot - 1];
            if (sv->reference.target)
                vm_store_var_str(sv->reference.target, sv->reference.cell, val->u.fs, val->length);
            else
                vm_store_var_str(sv, 0, val->u.fs, val->length);
            break;
        }

        case OP_LOAD_ARR1: {
            unsigned id = code[pc++];
            long idx = vstack[--sp].u.i;
            Variable *av = vm_ready_array((int)id, 1);
            if (av) {
                if (idx < av->bounds[0].lower || idx > av->bounds[0].upper)
                    error_message = "Subscript out of range";
                else
                    vm_load_elem(av, (unsigned)(idx - av->bounds[0].lower), &vstack[sp++]);
            }
            break;
        }

        case OP_LOAD_ARR2: {
            unsigned id = code[pc++];
            long idx2 = vstack[--sp].u.i;
            long idx1 = vstack[--sp].u.i;
            Variable *av = vm_ready_array((int)id, 2);
            if (av) {
                if (idx1 < av->bounds[0].lower || idx1 > av->bounds[0].upper ||
                    idx2 < av->bounds[1].lower || idx2 > av->bounds[1].upper) {
                    error_message = "Subscript out of range";
                } else {
                    unsigned c1 = (unsigned)(idx1 - av->bounds[0].lower);
                    unsigned ext2 = (unsigned)(av->bounds[1].upper - av->bounds[1].lower + 1L);
                    unsigned cell = c1 * ext2 + (unsigned)(idx2 - av->bounds[1].lower);
                    vm_load_elem(av, cell, &vstack[sp++]);
                }
            }
            break;
        }

        case OP_STORE_ARR1: {
            unsigned id = code[pc++];
            VmVal val = vstack[--sp];
            long idx = vstack[--sp].u.i;
            Variable *av = vm_ready_array((int)id, 1);
            if (av) {
                if (idx < av->bounds[0].lower || idx > av->bounds[0].upper)
                    error_message = "Subscript out of range";
                else
                    vm_store_elem(av, (unsigned)(idx - av->bounds[0].lower), &val);
            }
            break;
        }

        case OP_STORE_ARR2: {
            unsigned id = code[pc++];
            VmVal val = vstack[--sp];
            long idx2 = vstack[--sp].u.i;
            long idx1 = vstack[--sp].u.i;
            Variable *av = vm_ready_array((int)id, 2);
            if (av) {
                if (idx1 < av->bounds[0].lower || idx1 > av->bounds[0].upper ||
                    idx2 < av->bounds[1].lower || idx2 > av->bounds[1].upper) {
                    error_message = "Subscript out of range";
                } else {
                    unsigned c1 = (unsigned)(idx1 - av->bounds[0].lower);
                    unsigned ext2 = (unsigned)(av->bounds[1].upper - av->bounds[1].lower + 1L);
                    unsigned cell = c1 * ext2 + (unsigned)(idx2 - av->bounds[1].lower);
                    vm_store_elem(av, cell, &val);
                }
            }
            break;
        }

        case OP_LOAD_REC: {
            unsigned id = code[pc++];
            unsigned foff = code[pc++];
            unsigned kw = code[pc++];
            Reference ref = reference_at((int)id, 0);
            VmVal *dst = &vstack[sp++];
            ref.offset += foff;
            ref.kind = (char)(kw & 0xffU);
            ref.width = kw >> 8;
            dst->kind = ref.kind;
            dst->is_ref = 0;
            if (ref.kind == '%' || ref.kind == '&')
                dst->u.i = vm_read_ref_int(ref);
            else
                dst->u.f = vm_read_ref_flt(ref);
            break;
        }

        case OP_STORE_REC: {
            unsigned id = code[pc++];
            unsigned foff = code[pc++];
            unsigned kw = code[pc++];
            VmVal *val = &vstack[--sp];
            Reference ref = reference_at((int)id, 0);
            ref.offset += foff;
            ref.kind = (char)(kw & 0xffU);
            ref.width = kw >> 8;
            if (ref.kind == '%' || ref.kind == '&')
                vm_write_ref_int(ref, val->u.i);
            else
                vm_write_ref_flt(ref, val->u.f);
            break;
        }

        case OP_LOAD_RECARR1: {
            unsigned id = code[pc++];
            unsigned foff = code[pc++];
            unsigned kw = code[pc++];
            long idx = vstack[--sp].u.i;
            Variable *av = vm_ready_array((int)id, 1);
            if (av) {
                if (idx < av->bounds[0].lower || idx > av->bounds[0].upper) {
                    error_message = "Subscript out of range";
                } else {
                    Reference ref;
                    VmVal *dst = &vstack[sp++];
                    ref.target = av;
                    ref.cell = (unsigned)(idx - av->bounds[0].lower);
                    ref.offset = foff;
                    ref.kind = (char)(kw & 0xffU);
                    ref.width = kw >> 8;
                    dst->kind = ref.kind;
                    dst->is_ref = 0;
                    if (ref.kind == '%' || ref.kind == '&')
                        dst->u.i = vm_read_ref_int(ref);
                    else
                        dst->u.f = vm_read_ref_flt(ref);
                }
            }
            break;
        }

        case OP_STORE_RECARR1: {
            unsigned id = code[pc++];
            unsigned foff = code[pc++];
            unsigned kw = code[pc++];
            VmVal val = vstack[--sp];
            long idx = vstack[--sp].u.i;
            Variable *av = vm_ready_array((int)id, 1);
            if (av) {
                if (idx < av->bounds[0].lower || idx > av->bounds[0].upper) {
                    error_message = "Subscript out of range";
                } else {
                    Reference ref;
                    ref.target = av;
                    ref.cell = (unsigned)(idx - av->bounds[0].lower);
                    ref.offset = foff;
                    ref.kind = (char)(kw & 0xffU);
                    ref.width = kw >> 8;
                    if (ref.kind == '%' || ref.kind == '&')
                        vm_write_ref_int(ref, val.u.i);
                    else
                        vm_write_ref_flt(ref, val.u.f);
                }
            }
            break;
        }

        case OP_I2F:
            vstack[sp - 1].u.f = (double)vstack[sp - 1].u.i;
            vstack[sp - 1].kind = '#';
            break;

        case OP_I2F_UNDER:
            vstack[sp - 2].u.f = (double)vstack[sp - 2].u.i;
            vstack[sp - 2].kind = '#';
            break;

        case OP_CONV_I16: {
            VmVal *v = &vstack[sp - 1];
            long lv;
            if (v->kind == '%' || v->kind == '&') lv = v->u.i;
            else if (!vm_float_to_long_even(v->u.f, &lv)) break;
            if (lv < -32768L || lv > 32767L) error_message = "Overflow";
            else { v->u.i = lv; v->kind = '%'; }
            break;
        }

        case OP_CONV_I32: {
            VmVal *v = &vstack[sp - 1];
            long lv;
            if (v->kind != '%' && v->kind != '&') {
                if (!vm_float_to_long_even(v->u.f, &lv)) break;
                v->u.i = lv;
            }
            v->kind = '&';
            break;
        }

        case OP_CONV_I32_UNDER: {
            VmVal *v = &vstack[sp - 2];
            long lv;
            if (v->kind != '%' && v->kind != '&') {
                if (!vm_float_to_long_even(v->u.f, &lv)) break;
                v->u.i = lv;
            }
            v->kind = '&';
            break;
        }

        case OP_CONV_F32: {
            VmVal *v = &vstack[sp - 1];
            double d = (v->kind == '%' || v->kind == '&') ? (double)v->u.i : v->u.f;
            if (!finite_number(d) || d < -3.4028234663852886e38 || d > 3.4028234663852886e38)
                error_message = "Overflow";
            else { v->u.f = ckb_single_precision(d); v->kind = '!'; }
            break;
        }

        case OP_CONV_F64: {
            VmVal *v = &vstack[sp - 1];
            double d = (v->kind == '%' || v->kind == '&') ? (double)v->u.i : v->u.f;
            if (!finite_number(d)) error_message = "Overflow";
            else { v->u.f = d; v->kind = '#'; }
            break;
        }

        case OP_NEG_I16: {
            long r = -vstack[sp - 1].u.i;
            if (r > 32767L) error_message = "Overflow"; /* -(-32768) overflows % */
            else vstack[sp - 1].u.i = r;
            break;
        }

        case OP_NEG_I32: {
            long x = vstack[sp - 1].u.i;
            if (x == (-2147483647L - 1L)) error_message = "Overflow";
            else vstack[sp - 1].u.i = -x;
            break;
        }

        case OP_NEG_F: {
            char rk = (char)code[pc++];
            vstack[sp - 1].u.f = -vstack[sp - 1].u.f;
            vstack[sp - 1].kind = rk;
            break;
        }

        case OP_NOT_I16:
            vstack[sp - 1].u.i = (long)(short)~(unsigned short)(int)vstack[sp - 1].u.i;
            vstack[sp - 1].kind = '%';
            break;

        case OP_NOT_I32:
            vstack[sp - 1].u.i = ~vstack[sp - 1].u.i;
            vstack[sp - 1].kind = '&';
            break;

        case OP_ADD_I16: {
            long b = vstack[--sp].u.i;
            long r = vstack[sp - 1].u.i + b;
            if (r < -32768L || r > 32767L) error_message = "Overflow";
            else { vstack[sp - 1].u.i = r; vstack[sp - 1].kind = '%'; }
            break;
        }

        case OP_SUB_I16: {
            long b = vstack[--sp].u.i;
            long r = vstack[sp - 1].u.i - b;
            if (r < -32768L || r > 32767L) error_message = "Overflow";
            else { vstack[sp - 1].u.i = r; vstack[sp - 1].kind = '%'; }
            break;
        }

        case OP_MUL_I16: {
            long b = vstack[--sp].u.i;
            long r = vstack[sp - 1].u.i * b;
            if (r < -32768L || r > 32767L) error_message = "Overflow";
            else { vstack[sp - 1].u.i = r; vstack[sp - 1].kind = '%'; }
            break;
        }

        case OP_IDIV_I16: {
            long b = vstack[--sp].u.i;
            long a = vstack[sp - 1].u.i;
            if (!b) error_message = "Division by zero";
            else if (a == -32768L && b == -1L) error_message = "Overflow";
            else { vstack[sp - 1].u.i = a / b; vstack[sp - 1].kind = '%'; }
            break;
        }

        case OP_MOD_I16: {
            long b = vstack[--sp].u.i;
            long a = vstack[sp - 1].u.i;
            if (!b) error_message = "Division by zero";
            else if (a == -32768L && b == -1L) error_message = "Overflow";
            else { vstack[sp - 1].u.i = a % b; vstack[sp - 1].kind = '%'; }
            break;
        }

        case OP_AND_I16: {
            long b = vstack[--sp].u.i;
            vstack[sp - 1].u.i &= b;
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_OR_I16: {
            long b = vstack[--sp].u.i;
            vstack[sp - 1].u.i |= b;
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_XOR_I16: {
            long b = vstack[--sp].u.i;
            vstack[sp - 1].u.i ^= b;
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_EQV_I16: {
            long b = vstack[--sp].u.i;
            vstack[sp - 1].u.i = ~(vstack[sp - 1].u.i ^ b);
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_IMP_I16: {
            long b = vstack[--sp].u.i;
            vstack[sp - 1].u.i = (~vstack[sp - 1].u.i) | b;
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_CMP_I: {
            int cop = (int)code[pc++];
            long b = vstack[--sp].u.i;
            long a = vstack[sp - 1].u.i;
            int cmp = (a > b) - (a < b);
            vstack[sp - 1].u.i = vm_eval_rel(cmp, cop) ? -1L : 0L;
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_ADD_I32: {
            long b = vstack[--sp].u.i;
            long a = vstack[sp - 1].u.i;
            if ((b > 0 && a > 2147483647L - b) || (b < 0 && a < (-2147483647L - 1L) - b))
                error_message = "Overflow";
            else { vstack[sp - 1].u.i = a + b; vstack[sp - 1].kind = '&'; }
            break;
        }

        case OP_SUB_I32: {
            long b = vstack[--sp].u.i;
            long a = vstack[sp - 1].u.i;
            if ((b < 0 && a > 2147483647L + b) || (b > 0 && a < (-2147483647L - 1L) + b))
                error_message = "Overflow";
            else { vstack[sp - 1].u.i = a - b; vstack[sp - 1].kind = '&'; }
            break;
        }

        case OP_MUL_I32: {
            long b = vstack[--sp].u.i;
            long a = vstack[sp - 1].u.i;
            if (a >= -32768L && a <= 32767L && b >= -32768L && b <= 32767L) {
                vstack[sp - 1].u.i = a * b;
                vstack[sp - 1].kind = '&';
            } else {
                double pval = (double)a * (double)b;
                if (pval < -2147483648.0 || pval > 2147483647.0) error_message = "Overflow";
                else { vstack[sp - 1].u.i = (long)pval; vstack[sp - 1].kind = '&'; }
            }
            break;
        }

        case OP_IDIV_I32: {
            long b = vstack[--sp].u.i;
            long a = vstack[sp - 1].u.i;
            if (!b) error_message = "Division by zero";
            else if (a == (-2147483647L - 1L) && b == -1L) error_message = "Overflow";
            else { vstack[sp - 1].u.i = a / b; vstack[sp - 1].kind = '&'; }
            break;
        }

        case OP_MOD_I32: {
            long b = vstack[--sp].u.i;
            long a = vstack[sp - 1].u.i;
            if (!b) error_message = "Division by zero";
            else if (a == (-2147483647L - 1L) && b == -1L) error_message = "Overflow";
            else { vstack[sp - 1].u.i = a % b; vstack[sp - 1].kind = '&'; }
            break;
        }

        case OP_AND_I32:
            vstack[sp - 2].u.i &= vstack[sp - 1].u.i;
            --sp;
            vstack[sp - 1].kind = '&';
            break;
        case OP_OR_I32:
            vstack[sp - 2].u.i |= vstack[sp - 1].u.i;
            --sp;
            vstack[sp - 1].kind = '&';
            break;
        case OP_XOR_I32:
            vstack[sp - 2].u.i ^= vstack[sp - 1].u.i;
            --sp;
            vstack[sp - 1].kind = '&';
            break;
        case OP_EQV_I32:
            vstack[sp - 2].u.i = ~(vstack[sp - 2].u.i ^ vstack[sp - 1].u.i);
            --sp;
            vstack[sp - 1].kind = '&';
            break;
        case OP_IMP_I32:
            vstack[sp - 2].u.i = (~vstack[sp - 2].u.i) | vstack[sp - 1].u.i;
            --sp;
            vstack[sp - 1].kind = '&';
            break;

        case OP_ADD_F: {
            char rk = (char)code[pc++];
            double b = vstack[--sp].u.f;
            double a = vstack[sp - 1].u.f;
            long la, lb;
            double r;
            if (try_exact_int21(a, &la) && try_exact_int21(b, &lb))
                r = (double)(la + lb);
            else {
                r = a + b;
                if (!finite_number(r)) { error_message = "Overflow"; break; }
            }
            vstack[sp - 1].u.f = r;
            vstack[sp - 1].kind = rk;
            break;
        }

        case OP_SUB_F: {
            char rk = (char)code[pc++];
            double b = vstack[--sp].u.f;
            double a = vstack[sp - 1].u.f;
            long la, lb;
            double r;
            if (try_exact_int21(a, &la) && try_exact_int21(b, &lb))
                r = (double)(la - lb);
            else {
                r = a - b;
                if (!finite_number(r)) { error_message = "Overflow"; break; }
            }
            vstack[sp - 1].u.f = r;
            vstack[sp - 1].kind = rk;
            break;
        }

        case OP_MUL_F: {
            char rk = (char)code[pc++];
            double b = vstack[--sp].u.f;
            double a = vstack[sp - 1].u.f;
            double r = a * b;
            if (!finite_number(r)) error_message = "Overflow";
            else { vstack[sp - 1].u.f = r; vstack[sp - 1].kind = rk; }
            break;
        }

        case OP_DIV_F: {
            char rk = (char)code[pc++];
            double b = vstack[--sp].u.f;
            double a = vstack[sp - 1].u.f;
            if (b == 0.0) error_message = "Division by zero";
            else {
                double r = a / b;
                if (!finite_number(r)) error_message = "Overflow";
                else { vstack[sp - 1].u.f = r; vstack[sp - 1].kind = rk; }
            }
            break;
        }

        case OP_POW_F: {
            char rk = (char)code[pc++];
            double b = vstack[--sp].u.f;
            double a = vstack[sp - 1].u.f;
            double r;
            if (a == 0.0 && b < 0.0) { error_message = "Division by zero"; break; }
            if (a < 0.0 && b != floor(b)) { error_message = "Illegal function call"; break; }
            if (b == 2.0) r = a * a;
            else if (b == 3.0) r = a * a * a;
            else if (b == 1.0) r = a;
            else if (b == 0.0) r = 1.0;
            else r = pow(a, b);
            if (!finite_number(r)) error_message = "Overflow";
            else { vstack[sp - 1].u.f = r; vstack[sp - 1].kind = rk; }
            break;
        }

        case OP_CMP_F: {
            int cop = (int)code[pc++];
            double b = vstack[--sp].u.f;
            double a = vstack[sp - 1].u.f;
            long la, lb;
            int cmp;
            if (try_exact_int21(a, &la) && try_exact_int21(b, &lb))
                cmp = (la > lb) - (la < lb);
            else
                cmp = (a > b) - (a < b);
            vstack[sp - 1].u.i = vm_eval_rel(cmp, cop) ? -1L : 0L;
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_CONCAT_STR: {
            VmVal *b = &vstack[--sp];
            VmVal *a = &vstack[sp - 1];
            unsigned total = a->length + b->length;
            CkbValue tmp;
            char *dst;
            if (total > STRING_MAX) {
                error_message = "String too long";
                break;
            }
            INIT_VALUE(&tmp);
            /* Always allocate in temp_arena (> CKB_INLINE_STRING or force wide via temp_arena) */
            dst = ckb_string_buffer(&tmp, total < CKB_INLINE_STRING + 1U ? CKB_INLINE_STRING + 1U : total);
            if (!dst) {
                error_message = "Out of memory";
                break;
            }
            if (a->length) _fmemcpy(dst, a->u.fs, a->length);
            if (b->length) _fmemcpy(dst + a->length, b->u.fs, b->length);
            dst[total] = '\0';
            a->u.fs = (const char __far *)dst;
            a->length = total;
            a->kind = '$';
            break;
        }

        case OP_CMP_STR: {
            int cop = (int)code[pc++];
            VmVal *b = &vstack[--sp];
            VmVal *a = &vstack[sp - 1];
            int cmp = vm_cmp_far_str(a->u.fs, a->length, b->u.fs, b->length);
            a->u.i = vm_eval_rel(cmp, cop) ? -1L : 0L;
            a->kind = '%';
            break;
        }

        case OP_STR_FN1: {
            unsigned sfn = code[pc++];
            VmVal *a = &vstack[sp - 1];
            switch (sfn) {
            case SFN_LEN:
                a->u.i = (long)a->length;
                a->kind = '%';
                break;
            case SFN_ASC:
                if (!a->length) error_message = "Illegal function call";
                else { a->u.i = (long)(unsigned char)a->u.fs[0]; a->kind = '%'; }
                break;
            case SFN_LTRIM:
                while (a->length && a->u.fs[0] == ' ') { ++a->u.fs; --a->length; }
                break;
            case SFN_RTRIM:
                while (a->length && a->u.fs[a->length - 1] == ' ') --a->length;
                break;
            case SFN_STR: {
                CkbValue nv, tmp;
                char num_buf[48];
                unsigned nlen;
                char *dst;
                INIT_VALUE(&nv);
                nv.kind = a->kind;
                nv.number = (a->kind == '%' || a->kind == '&') ? (double)a->u.i : a->u.f;
                ckb_number_text(num_buf, &nv);
                nlen = (unsigned)strlen(num_buf);
                INIT_VALUE(&tmp);
                dst = ckb_string_buffer(&tmp, CKB_INLINE_STRING + 1U + nlen);
                if (!dst) { error_message = "Out of memory"; break; }
                memcpy(dst, num_buf, nlen + 1U);
                a->u.fs = (const char __far *)dst;
                a->length = nlen;
                a->kind = '$';
                break;
            }
            case SFN_CHR: {
                CkbValue tmp;
                char *dst;
                if (a->u.i < 0 || a->u.i > 255L) { error_message = "Illegal function call"; break; }
                INIT_VALUE(&tmp);
                dst = ckb_string_buffer(&tmp, CKB_INLINE_STRING + 2U);
                if (!dst) { error_message = "Out of memory"; break; }
                dst[0] = (char)a->u.i;
                dst[1] = '\0';
                a->u.fs = (const char __far *)dst;
                a->length = 1U;
                a->kind = '$';
                break;
            }
            case SFN_SPACE: {
                CkbValue tmp;
                char *dst;
                unsigned n;
                if (a->u.i < 0 || a->u.i > (long)STRING_MAX) { error_message = "Illegal function call"; break; }
                n = (unsigned)a->u.i;
                if (n == 0) {
                    a->u.fs = (const char __far *)"";
                    a->length = 0;
                    a->kind = '$';
                    break;
                }
                INIT_VALUE(&tmp);
                dst = ckb_string_buffer(&tmp, n < CKB_INLINE_STRING + 1U ? CKB_INLINE_STRING + 1U : n);
                if (!dst) { error_message = "Out of memory"; break; }
                memset(dst, ' ', n);
                dst[n] = '\0';
                a->u.fs = (const char __far *)dst;
                a->length = n;
                a->kind = '$';
                break;
            }
            case SFN_UCASE:
            case SFN_LCASE: {
                CkbValue tmp;
                char *dst;
                unsigned i, n = a->length;
                if (!n) break;
                INIT_VALUE(&tmp);
                dst = ckb_string_buffer(&tmp, n < CKB_INLINE_STRING + 1U ? CKB_INLINE_STRING + 1U : n);
                if (!dst) { error_message = "Out of memory"; break; }
                for (i = 0; i < n; ++i) {
                    unsigned char ch = (unsigned char)a->u.fs[i];
                    dst[i] = (char)(sfn == SFN_UCASE ? toupper(ch) : tolower(ch));
                }
                dst[n] = '\0';
                a->u.fs = (const char __far *)dst;
                break;
            }
            case SFN_VAL: {
                CkbValue in_v, out_v;
                char *buf;
                const char *err;
                INIT_VALUE(&in_v);
                INIT_VALUE(&out_v);
                in_v.string = 1;
                buf = ckb_string_buffer(&in_v, a->length);
                if (!buf) { error_message = "Out of memory"; break; }
                if (a->length) _fmemcpy(buf, a->u.fs, a->length);
                buf[a->length] = '\0';
                err = ckb_string_apply(SFN_VAL, &out_v, &in_v, 1, 1);
                if (err) error_message = err;
                else { a->u.f = out_v.number; a->kind = '#'; }
                break;
            }
            }
            break;
        }

        case OP_STR_FN2: {
            unsigned sfn = code[pc++];
            VmVal *b = &vstack[--sp];
            VmVal *a = &vstack[sp - 1];
            if (sfn == SFN_LEFT) {
                if (b->u.i < 0 || b->u.i > 32767L) error_message = "Illegal function call";
                else if ((unsigned)b->u.i < a->length) a->length = (unsigned)b->u.i;
            } else if (sfn == SFN_RIGHT) {
                if (b->u.i < 0 || b->u.i > 32767L) error_message = "Illegal function call";
                else if ((unsigned)b->u.i < a->length) {
                    unsigned n = (unsigned)b->u.i;
                    a->u.fs += a->length - n;
                    a->length = n;
                }
            } else if (sfn == SFN_MID) {
                if (b->u.i < 1 || b->u.i > 32767L) error_message = "Illegal function call";
                else {
                    unsigned st = (unsigned)(b->u.i - 1L);
                    if (st >= a->length) a->length = 0;
                    else { a->u.fs += st; a->length -= st; }
                }
            } else if (sfn == SFN_INSTR) {
                unsigned hlen = a->length, nlen = b->length, pos = 0;
                if (nlen == 0) {
                    pos = hlen ? 1U : 0U; /* Wait: check ckb_string_apply: if nlen <= hlen && 1 <= hlen -> pos = 1, if hlen==0 && nlen==0 -> 1 <= 0 false -> 0! */
                    if (hlen == 0) pos = 0U;
                } else if (nlen <= hlen && hlen > 0) {
                    unsigned off, lim = hlen - nlen;
                    char c0 = b->u.fs[0];
                    for (off = 0; off <= lim; ++off) {
                        if (a->u.fs[off] == c0 &&
                            vm_cmp_far_str(a->u.fs + off, nlen, b->u.fs, nlen) == 0) {
                            pos = off + 1U;
                            break;
                        }
                    }
                }
                a->u.i = (long)pos;
                a->kind = '%';
            }
            break;
        }

        case OP_STR_FN3: {
            unsigned sfn = code[pc++];
            VmVal *arg3 = &vstack[--sp];
            VmVal *arg2 = &vstack[--sp];
            VmVal *arg1 = &vstack[sp - 1];
            if (sfn == SFN_MID) {
                if (arg2->u.i < 1 || arg2->u.i > 32767L || arg3->u.i < 0 || arg3->u.i > 32767L) {
                    error_message = "Illegal function call";
                } else {
                    unsigned st = (unsigned)(arg2->u.i - 1L);
                    unsigned sz = (unsigned)arg3->u.i;
                    if (st >= arg1->length || !sz) arg1->length = 0;
                    else {
                        arg1->u.fs += st;
                        arg1->length -= st;
                        if (sz < arg1->length) arg1->length = sz;
                    }
                }
            } else if (sfn == SFN_INSTR) {
                if (arg1->u.i < 1 || arg1->u.i > 32767L) {
                    error_message = "Illegal function call";
                } else {
                    unsigned st = (unsigned)arg1->u.i;
                    unsigned hlen = arg2->length, nlen = arg3->length, pos = 0;
                    if (nlen <= hlen && st <= hlen) {
                        unsigned off, lim = hlen - nlen;
                        for (off = st - 1U; off <= lim; ++off) {
                            if (nlen == 0 ||
                                (arg2->u.fs[off] == arg3->u.fs[0] &&
                                 vm_cmp_far_str(arg2->u.fs + off, nlen, arg3->u.fs, nlen) == 0)) {
                                pos = off + 1U;
                                break;
                            }
                        }
                    }
                    arg1->u.i = (long)pos;
                    arg1->kind = '%';
                }
            }
            break;
        }

        case OP_MATH_FN: {
            int mfn = (short)code[pc++];
            char rk = (char)code[pc++];
            VmVal *a = &vstack[sp - 1];
            if (mfn == CF_ABS && (a->kind == '%' || a->kind == '&')) {
                if (a->kind == '%' && a->u.i == -32768L) error_message = "Overflow";
                else if (a->kind == '&' && a->u.i == (-2147483647L - 1L)) error_message = "Overflow";
                else if (a->u.i < 0) a->u.i = -a->u.i;
                break;
            }
            if (mfn == CF_SGN && (a->kind == '%' || a->kind == '&')) {
                a->u.i = (a->u.i > 0) - (a->u.i < 0);
                a->kind = '%';
                break;
            }
            {
                double n = (a->kind == '%' || a->kind == '&') ? (double)a->u.i : a->u.f;
                switch (mfn) {
                case CF_ABS: n = fabs(n); break;
                case CF_SGN:
                    a->u.i = (n > 0.0) - (n < 0.0);
                    a->kind = '%';
                    goto done_math;
                case CF_INT: n = floor(n); break;
                case CF_FIX: n = n < 0.0 ? -floor(-n) : floor(n); break;
                case CF_SQR:
                    if (n < 0.0) { error_message = "Illegal function call"; goto done_math; }
                    n = sqrt(n);
                    break;
                case CF_SIN: n = sin(n); break;
                case CF_COS: n = cos(n); break;
                case CF_TAN: n = tan(n); break;
                case CF_ATN: n = atan(n); break;
                case CF_EXP:
                    if (n > 709.782712893384) { error_message = "Overflow"; goto done_math; }
                    n = n < -745.0 ? 0.0 : exp(n);
                    break;
                case CF_LOG:
                    if (n <= 0.0) { error_message = "Illegal function call"; goto done_math; }
                    n = log(n);
                    break;
                }
                if (!finite_number(n) ||
                    (rk == '!' && (n < -3.4028234663852886e38 || n > 3.4028234663852886e38))) {
                    error_message = "Overflow";
                } else {
                    a->u.f = rk == '!' ? ckb_single_precision(n) : n;
                    a->kind = rk;
                }
            done_math:;
            }
            break;
        }

        case OP_TIMER: {
            VmVal *v = &vstack[sp++];
            v->kind = '!';
            v->is_ref = 0;
            v->u.f = ckb_timer();
            break;
        }

        case OP_RND0: {
            VmVal *v = &vstack[sp++];
            random_state = (random_state * 16598013UL + 12820163UL) & 0xffffffUL;
            v->kind = '!';
            v->is_ref = 0;
#if defined(_MSDOS) || defined(__WATCOMC__) || defined(__LITTLE_ENDIAN__) || defined(__x86_64__) || defined(__aarch64__)
            if (random_state == 0) v->u.f = 0.0;
            else {
                double rn = (double)random_state;
                ((unsigned short *)&rn)[3] -= (unsigned short)(24U << 4);
                v->u.f = rn;
            }
#else
            v->u.f = (double)random_state / 16777216.0;
#endif
            break;
        }

        case OP_RND1: {
            VmVal *a = &vstack[sp - 1];
            double arg = a->u.f;
            long arg_l;
            int ak = 1;
            if (try_exact_int21(arg, &arg_l) && arg_l >= 0) {
                ak = arg_l > 0 ? 1 : 0;
            } else {
                arg = ckb_single_precision(arg);
                ak = arg < 0.0 ? -1 : (arg != 0.0 ? 1 : 0);
            }
            if (ak < 0) {
                float sng = (float)arg;
                const unsigned char *b = (const unsigned char *)&sng;
                random_state = ((unsigned long)b[0] + ((unsigned long)b[1] << 8) +
                                ((unsigned long)b[2] << 16) + b[3]) & 0xffffffUL;
            }
            if (ak != 0)
                random_state = (random_state * 16598013UL + 12820163UL) & 0xffffffUL;
            a->kind = '!';
#if defined(_MSDOS) || defined(__WATCOMC__) || defined(__LITTLE_ENDIAN__) || defined(__x86_64__) || defined(__aarch64__)
            if (random_state == 0) a->u.f = 0.0;
            else {
                double rn = (double)random_state;
                ((unsigned short *)&rn)[3] -= (unsigned short)(24U << 4);
                a->u.f = rn;
            }
#else
            a->u.f = (double)random_state / 16777216.0;
#endif
            break;
        }

        case OP_INKEY: {
            CkbValue tmp;
            char kbuf[4];
            unsigned klen = (unsigned)ckb_inkey(kbuf);
            VmVal *v = &vstack[sp++];
            v->kind = '$';
            v->is_ref = 0;
            if (!klen) {
                v->u.fs = (const char __far *)"";
                v->length = 0;
            } else {
                char *dst;
                INIT_VALUE(&tmp);
                dst = ckb_string_buffer(&tmp, CKB_INLINE_STRING + 4U);
                if (!dst) { error_message = "Out of memory"; break; }
                memcpy(dst, kbuf, klen);
                dst[klen] = '\0';
                v->u.fs = (const char __far *)dst;
                v->length = klen;
            }
            break;
        }

        case OP_POINT2: {
            long y = vstack[--sp].u.i;
            long x = vstack[sp - 1].u.i;
            vstack[sp - 1].u.i = (long)ckb_point((int)x, (int)y);
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_POINT1: {
            long x = vstack[sp - 1].u.i;
            int lx, ly;
            ckb_gfx_last(&lx, &ly);
            if (x == 0 || x == 2) vstack[sp - 1].u.i = lx;
            else if (x == 1 || x == 3) vstack[sp - 1].u.i = ly;
            else error_message = "Illegal function call";
            vstack[sp - 1].kind = '%';
            break;
        }

        case OP_PEEK: {
            long addr = vstack[sp - 1].u.i;
            if (addr < 0 || addr > 65535L) error_message = "Illegal function call";
            else {
#if defined(__WATCOMC__) || defined(_MSDOS)
                vstack[sp - 1].u.i = *(const unsigned char __far *)MK_FP(current_def_seg(), (unsigned)addr);
#else
                vstack[sp - 1].u.i = 0;
#endif
                vstack[sp - 1].kind = '%';
            }
            break;
        }

        case OP_ERR_VAL: {
            unsigned is_erl = code[pc++];
            VmVal *v = &vstack[sp++];
            v->kind = is_erl ? '&' : '%';
            v->is_ref = 0;
            v->u.i = is_erl ? (long)trapped_error_line : (long)trapped_error_code;
            break;
        }

        case OP_BOUND: {
            unsigned aid = code[pc++];
            unsigned flags = code[pc++];
            long dim = (flags & 0x100U) ? vstack[--sp].u.i : 1L;
            Variable *av = array_storage((int)aid);
            VmVal *v;
            if (!av->elements && av->dynamic != ARRAY_DYNAMIC) {
                Parser dp;
                memset(&dp, 0, sizeof(dp));
                implicit_array(&dp, av, 1);
            }
            if (!av->elements || dim < 1 || (unsigned)dim > av->dimensions) {
                error_message = "Subscript out of range";
                break;
            }
            v = &vstack[sp++];
            v->kind = '&';
            v->is_ref = 0;
            v->u.i = (flags & 1U) ? av->bounds[dim - 1].upper : av->bounds[dim - 1].lower;
            break;
        }

        case OP_FOR_INIT: {
            int vid = (int)code[pc++];
            unsigned bidx = code[pc++];
            unsigned has_step = code[pc++];
            unsigned skip_pc = code[pc++];
            double stp = has_step ? vstack[--sp].u.f : 1.0;
            double lst = vstack[--sp].u.f;
            double beg = vstack[--sp].u.f;
            char vk = variables[vid].kind;
            Reference ref = reference_at(vid, 0);
            double cur_val;
            long ibeg, ilst, istp;

            if (vk == '%' || vk == '&') {
                if (!vm_float_to_long_even(beg, &ibeg)) break;
                if (!vm_write_ref_int(ref, ibeg)) break;
                cur_val = (double)ibeg;
            } else {
                if (!vm_write_ref_flt(ref, beg)) break;
                cur_val = vk == '!' ? ckb_single_precision(beg) : beg;
            }
            while (for_depth > (activation ? activation->for_base : 0) &&
                   for_stack[for_depth - 1].block == bidx)
                --for_depth;
            if (stp >= 0.0 ? cur_val > lst : cur_val < lst) {
                pc = skip_pc;
            } else if (for_depth >= STACK_MAX) {
                error_message = "Too many active FOR loops";
            } else {
                ForFrame *ff = &for_stack[for_depth++];
                ff->variable = vid;
                ff->block = bidx;
                ff->last = lst;
                ff->step = stp;
                ff->body_pc = pc;
                ff->fast_i16 = 0;
                if (vk == '%' && !variable_storage(vid)->reference.target &&
                    try_exact_int21(lst, &ilst) && try_exact_int21(stp, &istp)) {
                    ff->ilast = ilst;
                    ff->istep = istp;
                    ff->fast_i16 = 1;
                }
            }
            break;
        }

        case OP_FOR_NEXT: {
            unsigned opt_vid = code[pc++];
            unsigned base = activation ? activation->for_base : 0;
            ForFrame *ff;
            if (for_depth <= base) {
                error_message = "NEXT without matching FOR";
                break;
            }
            ff = &for_stack[for_depth - 1];
            if (opt_vid != 0xffffU && (int)opt_vid != ff->variable) {
                error_message = "NEXT without matching FOR";
                break;
            }
            if (ff->fast_i16) {
                Variable *fv = variable_storage(ff->variable);
                long nxt = VAR_IVAL(fv) + ff->istep;
                if (nxt < -32768L || nxt > 32767L) {
                    error_message = "Overflow";
                    break;
                }
                VAR_IVAL(fv) = nxt;
                if (ff->istep >= 0 ? (nxt <= ff->ilast) : (nxt >= ff->ilast))
                    pc = ff->body_pc;
                else
                    --for_depth;
            } else {
                Reference ref = reference_at(ff->variable, 0);
                double cur_d = (ref.kind == '%' || ref.kind == '&') ? (double)vm_read_ref_int(ref) : vm_read_ref_flt(ref);
                double nxt_d = cur_d + ff->step;
                if (!finite_number(nxt_d)) { error_message = "Overflow"; break; }
                if (ref.kind == '%' || ref.kind == '&') {
                    long nl;
                    if (!vm_float_to_long_even(nxt_d, &nl)) break;
                    if (!vm_write_ref_int(ref, nl)) break;
                    nxt_d = (double)nl;
                } else {
                    if (!vm_write_ref_flt(ref, nxt_d)) break;
                    if (ref.kind == '!') nxt_d = ckb_single_precision(nxt_d);
                }
                if (ff->step >= 0.0 ? (nxt_d <= ff->last) : (nxt_d >= ff->last))
                    pc = ff->body_pc;
                else
                    --for_depth;
            }
            break;
        }

        case OP_SEL_PUSH: {
            unsigned slot = code[pc++];
            unsigned tgt = code[pc++];
            VmVal *sv = &vstack[--sp];
            if (sv->kind == '$' && slot < 4U && sv->length <= 255U && vm.sel_str_buf) {
                char __far *sbuf = vm.sel_str_buf + slot * 256U;
                if (sv->length) _fmemcpy(sbuf, sv->u.fs, sv->length);
                sbuf[sv->length] = '\0';
                sv->u.fs = sbuf;
            }
            sel_vals[slot] = *sv;
            pc = tgt;
            break;
        }

        case OP_SEL_CASE_VAL: {
            unsigned slot = code[pc++];
            unsigned body_pc = code[pc++];
            VmVal *cv = &vstack[--sp];
            VmVal *sv = &sel_vals[slot];
            int match = 0;
            if ((sv->kind == '$') != (cv->kind == '$')) {
                error_message = "Type mismatch";
                break;
            }
            if (sv->kind == '$') {
                match = vm_cmp_far_str(sv->u.fs, sv->length, cv->u.fs, cv->length) == 0;
            } else if ((sv->kind == '%' || sv->kind == '&') && (cv->kind == '%' || cv->kind == '&')) {
                match = sv->u.i == cv->u.i;
            } else {
                double sd = (sv->kind == '%' || sv->kind == '&') ? (double)sv->u.i : sv->u.f;
                double cd = (cv->kind == '%' || cv->kind == '&') ? (double)cv->u.i : cv->u.f;
                match = sd == cd;
            }
            if (match) pc = body_pc;
            break;
        }

        case OP_SEL_CASE_RNG: {
            unsigned slot = code[pc++];
            unsigned body_pc = code[pc++];
            VmVal *hi = &vstack[--sp];
            VmVal *lo = &vstack[--sp];
            VmVal *sv = &sel_vals[slot];
            int match = 0;
            if ((sv->kind == '$') != (lo->kind == '$') || (lo->kind == '$') != (hi->kind == '$')) {
                error_message = "Type mismatch";
                break;
            }
            if (sv->kind == '$') {
                match = vm_cmp_far_str(sv->u.fs, sv->length, lo->u.fs, lo->length) >= 0 &&
                        vm_cmp_far_str(sv->u.fs, sv->length, hi->u.fs, hi->length) <= 0;
            } else if ((sv->kind == '%' || sv->kind == '&') &&
                       (lo->kind == '%' || lo->kind == '&') &&
                       (hi->kind == '%' || hi->kind == '&')) {
                match = sv->u.i >= lo->u.i && sv->u.i <= hi->u.i;
            } else {
                double sd = (sv->kind == '%' || sv->kind == '&') ? (double)sv->u.i : sv->u.f;
                double ld = (lo->kind == '%' || lo->kind == '&') ? (double)lo->u.i : lo->u.f;
                double hd = (hi->kind == '%' || hi->kind == '&') ? (double)hi->u.i : hi->u.f;
                match = sd >= ld && sd <= hd;
            }
            if (match) pc = body_pc;
            break;
        }

        case OP_SEL_CASE_IS: {
            unsigned slot = code[pc++];
            int cop = (int)code[pc++];
            unsigned body_pc = code[pc++];
            VmVal *cv = &vstack[--sp];
            VmVal *sv = &sel_vals[slot];
            int cmp;
            if ((sv->kind == '$') != (cv->kind == '$')) {
                error_message = "Type mismatch";
                break;
            }
            if (sv->kind == '$') {
                cmp = vm_cmp_far_str(sv->u.fs, sv->length, cv->u.fs, cv->length);
            } else if ((sv->kind == '%' || sv->kind == '&') && (cv->kind == '%' || cv->kind == '&')) {
                cmp = (sv->u.i > cv->u.i) - (sv->u.i < cv->u.i);
            } else {
                double sd = (sv->kind == '%' || sv->kind == '&') ? (double)sv->u.i : sv->u.f;
                double cd = (cv->kind == '%' || cv->kind == '&') ? (double)cv->u.i : cv->u.f;
                cmp = (sd > cd) - (sd < cd);
            }
            if (vm_eval_rel(cmp, cop)) pc = body_pc;
            break;
        }

        case OP_PUSH_REF_VAR: {
            unsigned vid = code[pc++];
            VmVal *v = &vstack[sp++];
            v->is_ref = 1;
            v->kind = variables[vid].kind;
            v->u.ref = reference_at((int)vid, 0);
            break;
        }

        case OP_PUSH_REF_ARR: {
            unsigned vid = code[pc++];
            Variable *av = array_storage((int)vid);
            VmVal *v;
            if (!av->elements && av->dynamic != ARRAY_DYNAMIC) {
                Parser dp;
                memset(&dp, 0, sizeof(dp));
                implicit_array(&dp, av, 1);
            }
            v = &vstack[sp++];
            v->is_ref = 1;
            v->kind = variables[vid].kind;
            v->u.ref = reference_at((int)vid, 0);
            break;
        }

        case OP_PUSH_REF_ARR1: {
            unsigned vid = code[pc++];
            long idx = vstack[--sp].u.i;
            Variable *av = vm_ready_array((int)vid, 1);
            if (av) {
                if (idx < av->bounds[0].lower || idx > av->bounds[0].upper) {
                    error_message = "Subscript out of range";
                } else {
                    VmVal *v = &vstack[sp++];
                    v->is_ref = 1;
                    v->kind = av->kind;
                    v->u.ref.target = av;
                    v->u.ref.cell = (unsigned)(idx - av->bounds[0].lower);
                    v->u.ref.offset = 0;
                    v->u.ref.width = av->fixed_width;
                    v->u.ref.kind = av->kind;
                }
            }
            break;
        }

        case OP_CALL_PROC: {
            int pid = (int)code[pc++];
            unsigned is_func = code[pc++];
            Procedure __far *proc = &procedures[pid];
            unsigned n_params = proc->count;
            unsigned n_locals = proc->locals;
            VmProcFrame *pf;
            Variable *locs = NULL;
            int pooled = 0;
            unsigned i;
            Parser dp;

            if (proc->line == LINE_MAX) {
                error_message = is_func ? "Function not defined" : "Subprogram not defined";
                break;
            }
            if (procedure_depth >= STACK_MAX) {
                error_message = "Out of stack space";
                break;
            }
            memset(&dp, 0, sizeof(dp));
            if (!alloc_proc_locals(&dp, pid, &locs, &pooled)) break;

            pf = &pframes[procedure_depth++];
            pf->frame.locals = locs;
            pf->frame.prev = activation;
            pf->frame.for_base = for_depth;
            pf->frame.call_base = call_depth;
            pf->caller_scope = current_scope;
            pf->return_pc = pc;
            pf->n_locals = n_locals;
            pf->pooled_locals = (unsigned char)pooled;
            pf->is_func = (unsigned char)is_func;
            pf->result_var = proc->result;
            pf->result_kind = proc->result_kind;

            /* Pop arguments in reverse order into parameter slots */
            for (i = n_params; i > 0; --i) {
                int param_id = proc->params[i - 1U];
                Variable *pvar = variables[param_id].frame_slot
                                     ? &locs[variables[param_id].frame_slot - 1U]
                                     : &variables[param_id];
                VmVal *arg = &vstack[--sp];
                if (arg->is_ref) {
                    pvar->reference = arg->u.ref;
                } else {
                    char pk = variables[param_id].kind;
                    pvar->reference.target = NULL;
                    if (pk == '%' || pk == '&') VAR_IVAL(pvar) = arg->u.i;
                    else if (pk == '$') vm_store_var_str(pvar, 0, arg->u.fs, arg->length);
                    else pvar->number = arg->u.f;
                }
            }

            activation = &pf->frame;
            current_scope = pid;
            pc = vm.proc_entry_pc[pid];
            break;
        }

        case OP_RET_PROC:
        do_ret_proc: {
            VmProcFrame *pf;
            VmVal ret_val;
            int has_ret = 0;
            if (procedure_depth == 0) {
                stopped = 1;
                break;
            }
            pf = &pframes[--procedure_depth];
            if (pf->is_func && !error_message) {
                Variable *rv = variable_storage(pf->result_var);
                has_ret = 1;
                ret_val.kind = pf->result_kind;
                ret_val.is_ref = 0;
                if (pf->result_kind == '%' || pf->result_kind == '&') {
                    ret_val.u.i = VAR_IVAL(rv);
                } else if (pf->result_kind == '$') {
                    /* Copy return string into temp_arena BEFORE freeing local frame! */
                    VmVal tmp_s;
                    CkbValue cv;
                    char *dst;
                    vm_read_var_str(rv, 0, &tmp_s);
                    if (tmp_s.length == 0) {
                        ret_val.u.fs = (const char __far *)"";
                        ret_val.length = 0;
                    } else {
                        INIT_VALUE(&cv);
                        dst = ckb_string_buffer(&cv, tmp_s.length < CKB_INLINE_STRING + 1U ? CKB_INLINE_STRING + 1U : tmp_s.length);
                        if (!dst) { error_message = "Out of memory"; has_ret = 0; }
                        else {
                            _fmemcpy(dst, tmp_s.u.fs, tmp_s.length);
                            dst[tmp_s.length] = '\0';
                            ret_val.u.fs = (const char __far *)dst;
                            ret_val.length = tmp_s.length;
                        }
                    }
                } else {
                    ret_val.u.f = rv->number;
                }
            }
            free_proc_locals(pf->frame.locals, pf->n_locals, pf->pooled_locals);
            activation = pf->frame.prev;
            current_scope = pf->caller_scope;
            for_depth = pf->frame.for_base;
            call_depth = pf->frame.call_base;
            pc = pf->return_pc;
            if (has_ret) vstack[sp++] = ret_val;
            break;
        }

        case OP_CALL_DEFFN: {
            int fn_id = (int)code[pc++];
            DefFn __far *fn = &def_fns[fn_id];
            unsigned pcount = fn->param_count, i;
            VmDefFnFrame __far *df;
            if (fn_depth >= DEPTH_MAX || !vm.fnframes) {
                error_message = "Expression too complex";
                break;
            }
            df = &vm.fnframes[fn_depth++];
            df->fn_id = fn_id;
            for (i = pcount; i > 0; --i) {
                unsigned idx = i - 1U;
                int pid = fn->param_var_ids[idx];
                VmVal *arg = &vstack[--sp];
                _fmemcpy(df->saved_raw[idx], &variables[pid].number, sizeof(double));
                df->saved_texts[idx] = variables[pid].text;
                variables[pid].text = NULL;
                if (variables[pid].kind == '%' || variables[pid].kind == '&')
                    VAR_IVAL(&variables[pid]) = arg->u.i;
                else if (variables[pid].kind == '$')
                    vm_store_var_str(&variables[pid], 0, arg->u.fs, arg->length);
                else
                    variables[pid].number = arg->u.f;
            }
            df->return_pc = pc;
            pc = vm.deffn_entry_pc[fn_id];
            break;
        }

        case OP_RET_DEFFN: {
            VmDefFnFrame __far *df = &vm.fnframes[--fn_depth];
            DefFn __far *fn = &def_fns[df->fn_id];
            unsigned pcount = fn->param_count, i;
            /* If return value is string referencing param var, copy to temp_arena first */
            if (fn->result_kind == '$' && sp > 0 && vstack[sp - 1].length > 0) {
                VmVal *rv = &vstack[sp - 1];
                CkbValue cv;
                char *dst;
                INIT_VALUE(&cv);
                dst = ckb_string_buffer(&cv, rv->length < CKB_INLINE_STRING + 1U ? CKB_INLINE_STRING + 1U : rv->length);
                if (dst) {
                    _fmemcpy(dst, rv->u.fs, rv->length);
                    dst[rv->length] = '\0';
                    rv->u.fs = (const char __far *)dst;
                }
            }
            for (i = 0; i < pcount; ++i) {
                int pid = fn->param_var_ids[i];
                free(variables[pid].text);
                variables[pid].text = df->saved_texts[i];
                _fmemcpy(&variables[pid].number, df->saved_raw[i], sizeof(double));
            }
            pc = df->return_pc;
            break;
        }

        case OP_GFX_LINE: {
            unsigned flags = code[pc++];
            int color = (flags & 8U) ? (int)vstack[--sp].u.i : -1;
            int y2 = (int)vstack[--sp].u.i;
            int x2 = (int)vstack[--sp].u.i;
            int x1 = 0, y1 = 0, lx, ly;
            const char *err;
            if (flags & 1U) {
                y1 = (int)vstack[--sp].u.i;
                x1 = (int)vstack[--sp].u.i;
                if (flags & 2U) {
                    ckb_gfx_last(&lx, &ly);
                    x1 += lx;
                    y1 += ly;
                }
            }
            if (flags & 4U) {
                /* Note: in LINE (x1,y1)-STEP(dx,dy), STEP is relative to (x1,y1) if has_first,
                 * Wait: check parse_gfx_coord in CKBIO.C: parse_gfx_coord uses ckb_gfx_last! */
                ckb_gfx_last(&lx, &ly);
                x2 += lx;
                y2 += ly;
            }
            err = ckb_line((flags & 1U) ? 1 : 0, x1, y1, x2, y2, color, (int)((flags >> 4) & 3U));
            if (err) error_message = err;
            break;
        }

        case OP_GFX_CIRCLE: {
            unsigned flags = code[pc++];
            double asp = (flags & 16U) ? vstack[--sp].u.f : 1.0;
            double ea = (flags & 8U) ? vstack[--sp].u.f : 0.0;
            double sa = (flags & 4U) ? vstack[--sp].u.f : 0.0;
            int color = (flags & 2U) ? (int)vstack[--sp].u.i : -1;
            double rad = vstack[--sp].u.f;
            int y = (int)vstack[--sp].u.i;
            int x = (int)vstack[--sp].u.i;
            const char *err;
            if (flags & 1U) {
                int lx, ly;
                ckb_gfx_last(&lx, &ly);
                x += lx;
                y += ly;
            }
            err = ckb_circle(x, y, rad, color,
                             (flags & 4U) != 0, sa,
                             (flags & 8U) != 0, ea,
                             (flags & 16U) != 0, asp);
            if (err) error_message = err;
            break;
        }

        case OP_GFX_PAINT: {
            unsigned flags = code[pc++];
            int bc = (flags & 4U) ? (int)vstack[--sp].u.i : -1;
            int fc = (flags & 2U) ? (int)vstack[--sp].u.i : -1;
            int y = (int)vstack[--sp].u.i;
            int x = (int)vstack[--sp].u.i;
            const char *err;
            if (flags & 1U) {
                int lx, ly;
                ckb_gfx_last(&lx, &ly);
                x += lx;
                y += ly;
            }
            err = ckb_paint(x, y, fc, bc);
            if (err) error_message = err;
            break;
        }

        case OP_GFX_PSET: {
            unsigned flags = code[pc++];
            int color = (flags & 2U) ? (int)vstack[--sp].u.i : -1;
            int y = (int)vstack[--sp].u.i;
            int x = (int)vstack[--sp].u.i;
            const char *err;
            if (flags & 1U) {
                int lx, ly;
                ckb_gfx_last(&lx, &ly);
                x += lx;
                y += ly;
            }
            err = ckb_pset(x, y, color, (flags & 4U) != 0);
            if (err) error_message = err;
            break;
        }

        case OP_GFX_GET: {
            unsigned aid = code[pc++];
            unsigned flags = code[pc++];
            long idx = (flags & 4U) ? vstack[--sp].u.i : 0L;
            int y2 = (int)vstack[--sp].u.i;
            int x2 = (int)vstack[--sp].u.i;
            int y1 = (int)vstack[--sp].u.i;
            int x1 = (int)vstack[--sp].u.i;
            Variable *av = array_storage((int)aid);
            unsigned cell = 0, esize, cap;
            const char *err;
            if (flags & 1U) { int lx, ly; ckb_gfx_last(&lx, &ly); x1 += lx; y1 += ly; }
            if (flags & 2U) { int lx, ly; ckb_gfx_last(&lx, &ly); x2 += lx; y2 += ly; }
            if (!av->elements && av->dynamic != ARRAY_DYNAMIC) {
                Parser dp;
                memset(&dp, 0, sizeof(dp));
                implicit_array(&dp, av, 1);
            }
            if (!av->elements) { error_message = "Subscript out of range"; break; }
            if (flags & 4U) {
                if (av->dimensions != 1 || idx < av->bounds[0].lower || idx > av->bounds[0].upper) {
                    error_message = "Subscript out of range";
                    break;
                }
                cell = (unsigned)(idx - av->bounds[0].lower);
            }
            if (cell >= av->cells) { error_message = "Subscript out of range"; break; }
            esize = array_element_size(av);
            cap = (av->cells - cell) * esize;
            err = ckb_gfx_get(x1, y1, x2, y2, (unsigned char *)av->elements + cell * esize, cap);
            if (err) error_message = err;
            break;
        }

        case OP_GFX_PUT: {
            unsigned aid = code[pc++];
            unsigned flags = code[pc++];
            long idx = (flags & 2U) ? vstack[--sp].u.i : 0L;
            int y = (int)vstack[--sp].u.i;
            int x = (int)vstack[--sp].u.i;
            Variable *av = array_storage((int)aid);
            unsigned cell = 0, esize, cap;
            const char *err;
            if (flags & 1U) { int lx, ly; ckb_gfx_last(&lx, &ly); x += lx; y += ly; }
            if (!av->elements && av->dynamic != ARRAY_DYNAMIC) {
                Parser dp;
                memset(&dp, 0, sizeof(dp));
                implicit_array(&dp, av, 1);
            }
            if (!av->elements) { error_message = "Subscript out of range"; break; }
            if (flags & 2U) {
                if (av->dimensions != 1 || idx < av->bounds[0].lower || idx > av->bounds[0].upper) {
                    error_message = "Subscript out of range";
                    break;
                }
                cell = (unsigned)(idx - av->bounds[0].lower);
            }
            if (cell >= av->cells) { error_message = "Subscript out of range"; break; }
            esize = array_element_size(av);
            cap = (av->cells - cell) * esize;
            err = ckb_gfx_put(x, y, (unsigned char *)av->elements + cell * esize, cap,
                              (int)((flags >> 4) & 0x0fU));
            if (err) error_message = err;
            break;
        }

        case OP_CON_CLS: {
            unsigned has_m = code[pc++];
            int m = has_m ? (int)vstack[--sp].u.i : -1;
            if (m < -1 || m > 2) error_message = "Illegal function call";
            else {
                ckb_cls_mode(m);
                if (m != 1) screen_column = 0;
            }
            break;
        }

        case OP_CON_LOCATE: {
            unsigned mask = code[pc++];
            int vis = (mask & 4U) ? (int)vstack[--sp].u.i : -1;
            int col = (mask & 2U) ? (int)vstack[--sp].u.i : -1;
            int row = (mask & 1U) ? (int)vstack[--sp].u.i : -1;
            const char *err = ckb_locate(row, col, vis);
            if (err) error_message = err;
            else screen_column = ckb_con_column();
            break;
        }

        case OP_CON_COLOR: {
            unsigned mask = code[pc++];
            int bg = (mask & 2U) ? (int)vstack[--sp].u.i : -1;
            int fg = (mask & 1U) ? (int)vstack[--sp].u.i : -1;
            ckb_color(fg, bg);
            break;
        }

        case OP_CON_SCREEN: {
            int sm = (int)vstack[--sp].u.i;
            const char *err = ckb_screen(sm);
            if (err) error_message = err;
            else screen_column = 0;
            break;
        }

        case OP_CON_PALETTE: {
            unsigned has_args = code[pc++];
            if (has_args) {
                long col = vstack[--sp].u.i;
                int attr = (int)vstack[--sp].u.i;
                const char *err = ckb_palette(attr, col);
                if (err) error_message = err;
            }
            break;
        }

        case OP_CON_VIEW_PRINT: {
            unsigned has_args = code[pc++];
            int top = -1, bot = -1;
            const char *err;
            if (has_args) {
                bot = (int)vstack[--sp].u.i;
                top = (int)vstack[--sp].u.i;
            }
            err = ckb_view_print(top, bot);
            if (err) error_message = err;
            else screen_column = 0;
            break;
        }

        case OP_CON_BEEP:
            ckb_beep();
            break;

        case OP_PRINT_ITEM: {
            unsigned kind = code[pc++];
            Parser dp;
            memset(&dp, 0, sizeof(dp));
            if (kind == 2U) {
                unsigned width = ckb_con_direct_active() ? ckb_con_width() : 80U;
                unsigned spaces = 14U - screen_column % 14U;
                static const char sp14[] = "              ";
                if (screen_column + spaces >= width) {
                    if (ckb_con_direct_active()) { ckb_con_newline(); screen_column = 0; }
                    else write_bytes(&dp, stdout, &screen_column, "\n", 1);
                } else {
                    write_bytes(&dp, stdout, &screen_column, sp14, spaces);
                }
            } else if (kind == 0U) {
                VmVal *pv = &vstack[--sp];
                if (pv->kind == '$') {
                    char sbuf[256];
                    unsigned rem = pv->length, off = 0;
                    while (rem > 0 && !error_message) {
                        unsigned chunk = rem > sizeof(sbuf) ? (unsigned)sizeof(sbuf) : rem;
                        _fmemcpy(sbuf, pv->u.fs + off, chunk);
                        /* Note: write_format wraps on direct/80 col? Simple console text matches write_bytes unless > width */
                        write_bytes(&dp, stdout, &screen_column, sbuf, chunk);
                        off += chunk;
                        rem -= chunk;
                    }
                } else {
                    CkbValue nv;
                    char nbuf[48];
                    unsigned nlen;
                    INIT_VALUE(&nv);
                    nv.kind = pv->kind;
                    nv.number = (pv->kind == '%' || pv->kind == '&') ? (double)pv->u.i : pv->u.f;
                    ckb_number_text(nbuf, &nv);
                    nlen = (unsigned)strlen(nbuf);
                    nbuf[nlen++] = ' ';
                    nbuf[nlen] = '\0';
                    if ((ckb_con_direct_active() ? ckb_con_width() : 80U) - screen_column < nlen &&
                        screen_column > 0) {
                        if (ckb_con_direct_active()) { ckb_con_newline(); screen_column = 0; }
                        else write_bytes(&dp, stdout, &screen_column, "\n", 1);
                    }
                    write_bytes(&dp, stdout, &screen_column, nbuf, nlen);
                }
            }
            break;
        }

        case OP_PRINT_END: {
            unsigned nl = code[pc++];
            if (nl) {
                if (ckb_con_direct_active()) {
                    ckb_con_newline();
                    screen_column = 0;
                } else {
                    Parser dp;
                    memset(&dp, 0, sizeof(dp));
                    write_bytes(&dp, stdout, &screen_column, "\n", 1);
                }
            }
            break;
        }

        default:
            error_message = "Internal VM error";
            break;
        }
    }

    /* Clean up any remaining VM procedure frames on error/halt */
    while (procedure_depth > 0) {
        VmProcFrame *pf = &pframes[--procedure_depth];
        free_proc_locals(pf->frame.locals, pf->n_locals, pf->pooled_locals);
        activation = pf->frame.prev;
        current_scope = pf->caller_scope;
    }
    while (fn_depth > 0) {
        VmDefFnFrame __far *df = &vm.fnframes[--fn_depth];
        DefFn __far *fn = &def_fns[df->fn_id];
        unsigned i;
        for (i = 0; i < fn->param_count; ++i) {
            int pid = fn->param_var_ids[i];
            free(variables[pid].text);
            variables[pid].text = df->saved_texts[i];
            _fmemcpy(&variables[pid].number, df->saved_raw[i], sizeof(double));
        }
    }
    if (ckb_string_mark() != str_base)
        ckb_string_release(str_base);

    vm_sync_active_frames(1);
    vm_free_module();
    return 1;
}
