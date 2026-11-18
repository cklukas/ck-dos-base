/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Shared bytecode opcodes and module structures between CKBCMP.C and CKBVM.C.
 */
#ifndef CKBVMP_H
#define CKBVMP_H

#include "CKBRUNP.H"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(__WATCOMC__) || defined(_MSDOS)
#include <dos.h>
#include <malloc.h>
#endif

#if !defined(__WATCOMC__) && !defined(_MSDOS)
#ifndef _fmemset
#define _fmemset memset
#endif
#endif

/* Opcodes */
enum {
    OP_NOP = 0,
    OP_HALT,            /* SYSTEM / END */
    OP_STOP_STMT,       /* STOP */
    OP_JMP,             /* word1: target_pc */
    OP_JFALSE,          /* word1: target_pc (pops int/float truthiness) */
    OP_JTRUE,           /* word1: target_pc */
    OP_GOSUB,           /* word1: target_pc, word2: ret_pc */
    OP_RETURN,          /* word1: 0xffff or target_pc */
    OP_RET_PROC,        /* return from SUB/FUNCTION */
    OP_FALLBACK,        /* w1: line, w2: offset */

    /* Literals */
    OP_LIT_I16,         /* w1: signed 16-bit */
    OP_LIT_I32,         /* w1: lo16, w2: hi16 */
    OP_LIT_F64,         /* w1: fconst index | (is_single ? 0x8000 : 0) */
    OP_LIT_STR,         /* w1: source offset, w2: length */

    /* Scalar Loads */
    OP_LOAD_GINT_I16,   /* w1: near ptr &variables[id] (%) */
    OP_LOAD_GINT_I32,   /* w1: near ptr &variables[id] (&) */
    OP_LOAD_LINT_I16,   /* w1: byte offset in activation->locals (%) */
    OP_LOAD_LINT_I32,   /* w1: byte offset in activation->locals (&) */
    OP_LOAD_GFLT_SNG,   /* w1: near ptr &variables[id] (!) */
    OP_LOAD_GFLT_DBL,   /* w1: near ptr &variables[id] (#) */
    OP_LOAD_LFLT_SNG,   /* w1: byte offset in activation->locals (!) */
    OP_LOAD_LFLT_DBL,   /* w1: byte offset in activation->locals (#) */
    OP_LOAD_GSTR,       /* w1: near ptr &variables[id] ($) */
    OP_LOAD_LSTR,       /* w1: var_id */

    /* Scalar Stores */
    OP_STORE_GINT_I16,  /* w1: near ptr &variables[id] (%) */
    OP_STORE_GINT_I32,  /* w1: near ptr &variables[id] (&) */
    OP_STORE_LINT_I16,  /* w1: byte offset in activation->locals (%) */
    OP_STORE_LINT_I32,  /* w1: byte offset in activation->locals (&) */
    OP_STORE_GFLT_SNG,  /* w1: near ptr &variables[id] (!) */
    OP_STORE_GFLT_DBL,  /* w1: near ptr &variables[id] (#) */
    OP_STORE_LFLT_SNG,  /* w1: byte offset in activation->locals (!) */
    OP_STORE_LFLT_DBL,  /* w1: byte offset in activation->locals (#) */
    OP_STORE_GSTR,      /* w1: near ptr &variables[id] ($) */
    OP_STORE_LSTR,      /* w1: var_id */

    /* 1D / 2D Array & Record Loads/Stores */
    OP_LOAD_GARR1,      /* w1: near ptr &variables[id] (global 1D) */
    OP_LOAD_ARR1,       /* w1: var_id */
    OP_LOAD_ARR2,       /* w1: var_id */
    OP_STORE_GARR1,     /* w1: near ptr &variables[id] (global 1D) */
    OP_STORE_ARR1,      /* w1: var_id */
    OP_STORE_ARR2,      /* w1: var_id */
    OP_LOAD_REC,        /* w1: var_id, w2: field_offset, w3: (width<<8)|kind */
    OP_STORE_REC,       /* w1: var_id, w2: field_offset, w3: (width<<8)|kind */
    OP_LOAD_RECARR1,    /* w1: var_id, w2: field_offset, w3: (width<<8)|kind */
    OP_STORE_RECARR1,   /* w1: var_id, w2: field_offset, w3: (width<<8)|kind */
    OP_LOAD_RECARR2,    /* w1: var_id, w2: field_offset, w3: (width<<8)|kind */
    OP_STORE_RECARR2,   /* w1: var_id, w2: field_offset, w3: (width<<8)|kind */

    /* Type Conversions */
    OP_I2F,             /* convert top-of-stack int -> double */
    OP_I2F_UNDER,       /* convert sp[-2] int -> double */
    OP_CONV_I16,        /* CINT / coerce to % (round_even if float) */
    OP_CONV_I32,        /* CLNG / coerce to & (round_even if float) */
    OP_CONV_I32_UNDER,  /* convert sp[-2] float -> & (round_even) */
    OP_CONV_F32,        /* CSNG */
    OP_CONV_F64,        /* CDBL */

    /* Unary Operators */
    OP_NEG_I16,
    OP_NEG_I32,
    OP_NEG_F,           /* w1: kind ('!' or '#') */
    OP_NOT_I16,
    OP_NOT_I32,

    /* Integer 16-bit (%) Binary Operators */
    OP_ADD_I16,
    OP_SUB_I16,
    OP_MUL_I16,
    OP_IDIV_I16,
    OP_MOD_I16,
    OP_AND_I16,
    OP_OR_I16,
    OP_XOR_I16,
    OP_EQV_I16,
    OP_IMP_I16,
    OP_ADD_I16_IMM,     /* w1: signed 16-bit imm */
    OP_MUL_I16_IMM,     /* w1: signed 16-bit imm */
    OP_AND_I16_IMM,     /* w1: signed 16-bit imm */
    OP_OR_I16_IMM,      /* w1: signed 16-bit imm */
    OP_CMP_EQ_I,
    OP_CMP_NE_I,
    OP_CMP_LT_I,
    OP_CMP_LE_I,
    OP_CMP_GT_I,
    OP_CMP_GE_I,
    OP_CMP_EQ_I_IMM,    /* w1: signed 16-bit imm */
    OP_CMP_NE_I_IMM,    /* w1: signed 16-bit imm */
    OP_CMP_LT_I_IMM,    /* w1: signed 16-bit imm */
    OP_CMP_LE_I_IMM,    /* w1: signed 16-bit imm */
    OP_CMP_GT_I_IMM,    /* w1: signed 16-bit imm */
    OP_CMP_GE_I_IMM,    /* w1: signed 16-bit imm */
    OP_JEQ_I,           /* w1: target_pc */
    OP_JNE_I,           /* w1: target_pc */
    OP_JLT_I,           /* w1: target_pc */
    OP_JLE_I,           /* w1: target_pc */
    OP_JGT_I,           /* w1: target_pc */
    OP_JGE_I,           /* w1: target_pc */
    OP_JEQ_I_IMM,       /* w1: signed 16-bit imm, w2: target_pc */
    OP_JNE_I_IMM,       /* w1: signed 16-bit imm, w2: target_pc */
    OP_JLT_I_IMM,       /* w1: signed 16-bit imm, w2: target_pc */
    OP_JLE_I_IMM,       /* w1: signed 16-bit imm, w2: target_pc */
    OP_JGT_I_IMM,       /* w1: signed 16-bit imm, w2: target_pc */
    OP_JGE_I_IMM,       /* w1: signed 16-bit imm, w2: target_pc */

    /* Long 32-bit (&) Binary Operators */
    OP_ADD_I32,
    OP_SUB_I32,
    OP_MUL_I32,
    OP_IDIV_I32,
    OP_MOD_I32,
    OP_AND_I32,
    OP_OR_I32,
    OP_XOR_I32,
    OP_EQV_I32,
    OP_IMP_I32,

    /* Floating-Point (! / #) Binary Operators */
    OP_ADD_F,           /* w1: result_kind */
    OP_SUB_F,           /* w1: result_kind */
    OP_MUL_F,           /* w1: result_kind */
    OP_DIV_F,           /* w1: result_kind */
    OP_POW_F,           /* w1: result_kind */
    OP_CMP_F,           /* w1: op */

    /* String Operators & Intrinsics */
    OP_CONCAT_STR,
    OP_CMP_STR,         /* w1: op */
    OP_STR_FN1,         /* w1: fn_id (LEN, ASC, CHR$, SPACE$, STR$, VAL, UCASE$, LCASE$, LTRIM$, RTRIM$) */
    OP_STR_FN2,         /* w1: fn_id (LEFT$, RIGHT$, INSTR2, MID$2, STRING$) */
    OP_STR_FN3,         /* w1: fn_id (MID$3, INSTR3) */

    /* Numeric Intrinsics */
    OP_MATH_FN,         /* w1: CF_* id, w2: out_kind */
    OP_TIMER,
    OP_RND0,            /* RND with no parens */
    OP_RND1,            /* RND(expr) */
    OP_INKEY,
    OP_POINT2,          /* POINT(x, y) */
    OP_POINT1,          /* POINT(n) */
    OP_PEEK,
    OP_ERR_VAL,         /* w1: is_erl */
    OP_BOUND,           /* w1: var_id, w2: is_ubound | (has_dim ? 0x100 : 0) */

    /* Structured Loops & SELECT CASE */
    OP_FOR_INIT,        /* w1: var_id, w2: block_idx, w3: has_step, w4: skip_pc */
    OP_FOR_INIT_I16,    /* w1: var_id, w2: block_idx, w3: has_step, w4: skip_pc */
    OP_FOR_NEXT,        /* w1: var_id_or_ffff */
    OP_SEL_PUSH,        /* w1: sel_slot, w2: first_case_pc */
    OP_SEL_CASE_VAL,    /* w1: sel_slot, w2: next_try_pc (matched jumps to body_pc: w3) */
    OP_SEL_CASE_RNG,    /* w1: sel_slot, w2: body_pc */
    OP_SEL_CASE_IS,     /* w1: sel_slot, w2: op, w3: body_pc */

    /* Procedures & DEF FN */
    OP_PUSH_REF_VAR,    /* w1: var_id */
    OP_PUSH_REF_ARR,    /* w1: var_id (whole array `arr()`) */
    OP_PUSH_REF_ARR1,   /* w1: var_id (1D element `arr(idx)`) */
    OP_CALL_PROC,       /* w1: proc_id, w2: is_func */
    OP_CALL_DEFFN,      /* w1: fn_id */
    OP_RET_DEFFN,

    /* Graphics & Console Statements */
    OP_GFX_LINE,        /* w1: flags (bit0:has_first, bit1:step1, bit2:step2, bit3:has_color, bits4..5:box) */
    OP_GFX_CIRCLE,      /* w1: flags (bit0:step, bit1:has_c, bit2:hs, bit3:he, bit4:ha) */
    OP_GFX_PAINT,       /* w1: flags (bit0:step, bit1:has_fc, bit2:has_bc) */
    OP_GFX_PSET,        /* w1: flags (bit0:step, bit1:has_c, bit2:is_preset) */
    OP_GFX_GET,         /* w1: arr_var_id, w2: flags (bit0:step1, bit1:step2, bit2:has_idx) */
    OP_GFX_PUT,         /* w1: arr_var_id, w2: flags (bit0:step, bit1:has_idx, bits4..7:action) */
    OP_CON_CLS,         /* w1: has_mode */
    OP_CON_LOCATE,      /* w1: mask (bit0:row, bit1:col, bit2:vis) */
    OP_CON_COLOR,       /* w1: mask (bit0:fg, bit1:bg) */
    OP_CON_SCREEN,
    OP_CON_PALETTE,     /* w1: has_args */
    OP_CON_VIEW_PRINT,  /* w1: has_args */
    OP_CON_BEEP,
    OP_PRINT_ITEM,      /* w1: 0=expr, 1=';', 2=',' */
    OP_PRINT_END        /* w1: newline_if_1 */
};

enum {
    SFN_ASC = 1, SFN_CHR, SFN_INSTR, SFN_LEFT, SFN_LEN, SFN_MID,
    SFN_SPACE, SFN_UCASE, SFN_VAL, SFN_RIGHT, SFN_LTRIM, SFN_RTRIM,
    SFN_REPEAT, SFN_STR, SFN_LCASE
};

#define VM_MAX_CODE     14336U
#define VM_MAX_FCONSTS  128U
#define VM_MAX_STMTS    1600U
#define VM_MAX_FIXUPS   1600U
#define VM_STACK_MAX    32U
#define VM_SEL_MAX      8U

typedef struct {
    unsigned start_pc;
    unsigned line;
    unsigned offset;
} StmtSpan;

typedef struct {
    unsigned patch_pc;
    unsigned line;
    unsigned offset;
    unsigned char plus_two;
} VmFixup;

#if defined(__WATCOMC__) || defined(_MSDOS)
#define VM_GVAR_ARG(id) ((unsigned)&variables[id])
#define VM_GVAR_PTR(w)  ((Variable *)(w))
#else
#define VM_GVAR_ARG(id) ((unsigned)(id))
#define VM_GVAR_PTR(w)  (&variables[w])
#endif
#define VM_LVAR_ARG(id) ((unsigned)((variables[id].frame_slot - 1U) * sizeof(Variable)))
#define VM_LVAR_PTR(w)  ((Variable *)((char *)activation->locals + (w)))

typedef struct {
    char kind;
    unsigned char is_ref;
    unsigned length;
    unsigned pad0, pad1;
    union {
        long i;
        double f;
        const char __far *fs;
        struct {
            Variable *target;
            unsigned cell;
        } sref;
    } u;
} VmVal;

typedef struct {
    Activation frame;
    int caller_scope;
    unsigned return_pc;
    unsigned n_locals;
    unsigned char pooled_locals;
    unsigned char is_func;
    int result_var;
    char result_kind;
} VmProcFrame;

#define VAR_IVAL(v) (*(long *)&((v)->number))

typedef struct {
    int fn_id;
    unsigned return_pc;
    char saved_raw[PARAM_MAX][sizeof(double)];
    char __far *saved_texts[PARAM_MAX];
} VmDefFnFrame;

typedef struct {
    unsigned short __far *code;
    double __far *fconsts;
    unsigned short __far *line_pc;
    StmtSpan __far *stmts;
    VmDefFnFrame __far *fnframes;
    char __far *sel_str_buf;
    unsigned code_cap, code_len;
    unsigned fconst_len;
    unsigned stmt_cap, stmt_len;
    unsigned proc_entry_pc[PROC_MAX];
    unsigned deffn_entry_pc[DEF_FN_MAX];
} VmModule;

#define vm ckb_vm_mod
#define vm_free_module ckb_vm_free_module
#define resolve_pos_pc ckb_vm_resolve_pos_pc
#define compile_module ckb_vm_compile_module

extern VmModule __far vm;
void vm_free_module(void);
unsigned resolve_pos_pc(Position pos);
int compile_module(void);

#endif
