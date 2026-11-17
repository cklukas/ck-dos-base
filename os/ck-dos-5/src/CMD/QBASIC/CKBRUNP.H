/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBRUNP_H
#define CKBRUNP_H

#include "CKBRUN.H"
#include "CKBSTR.H"
#include "CKBCON.H"
#include "CKBFMT.H"
#include "CKBNUM.H"
#include "CKENV.H"
#include "CKBSER.H"

#include <stdio.h>
#include <stddef.h>

#if !defined(__WATCOMC__) && !defined(_MSDOS)
#ifndef __far
#define __far
#define _fmemcpy memcpy
#define _fmemmove memmove
#define _fmemchr memchr
#define _fmalloc malloc
#define _ffree free
#endif
#endif

#define SOURCE_MAX 60000U
#define LINE_MAX 2500U
#define VALUE_MAX 320U
#define BASIC_NAME_MAX 40
#define STRING_MAX CKB_STRING_MAX
#define TEXT_INPUT_MAX CKB_INLINE_STRING
#define BLOCK_MAX 1024U
#define DATA_MAX 512U
#define STACK_MAX 24
#define DEPTH_MAX 16
#define FILE_MAX 16
#define PROC_MAX 64
#define DEF_FN_MAX 16
#define PARAM_MAX 8
#define DIMENSION_MAX 60
#define ARRAY_CELL_MAX 4096
#define RECORD_MAX 16

enum { T_NUMBER = 256, T_STRING, T_NAME, T_LE, T_GE, T_NE };
enum { ARRAY_IMPLICIT, ARRAY_STATIC, ARRAY_DYNAMIC };
enum { FILE_INPUT, FILE_OUTPUT, FILE_APPEND, FILE_BINARY, FILE_RANDOM };
enum { ACCESS_READ = 1, ACCESS_WRITE = 2 };
enum { SHARE_DEFAULT, SHARE_ALLOW, SHARE_DENY_READ, SHARE_DENY_WRITE, SHARE_DENY_BOTH };
enum { FILE_SEEK, FILE_LOC, FILE_LOF };
enum { CREATE_DIRECTORY, CHANGE_DIRECTORY, REMOVE_DIRECTORY, DELETE_FILE };

enum { CF_CINT = -1, CF_CLNG = -2, CF_CSNG = -3, CF_CDBL = -4,
       CF_ABS = -5, CF_SGN = -6, CF_INT = -7, CF_FIX = -8,
       CF_SQR = -9, CF_SIN = -10, CF_COS = -11, CF_TAN = -12,
       CF_ATN = -13, CF_EXP = -14, CF_LOG = -15,
       CF_MKS = -16, CF_MKD = -17, CF_MKSMBF = -18, CF_MKDMBF = -19,
       CF_CVS = -20, CF_CVD = -21, CF_CVSMBF = -22, CF_CVDMBF = -23,
       CF_MKI = -24, CF_MKL = -25, CF_CVI = -26, CF_CVL = -27,
       CF_HEX = -28, CF_OCT = -29 };

typedef CkbValue Value;
typedef struct Variable Variable;
typedef struct FieldBinding FieldBinding;

typedef struct {
    Variable *target;
    unsigned cell, offset, width;
    char kind;
} Reference;

typedef struct {
    long lower, upper;
} Bounds;

typedef struct {
    unsigned line, offset;
} Position;

typedef struct RecordField RecordField;

struct RecordField {
    RecordField *next;
    unsigned offset, width, start, length;
    char kind;
};

typedef struct {
    char name[BASIC_NAME_MAX + 1];
    unsigned line, end, size;
    RecordField *fields;
} RecordType;

typedef struct {
    double number;
    char *text;
    void *elements;
    Bounds *bounds;
    unsigned width, cells, dimensions;
    int array;
    char kind;
} CommonValue;

typedef struct {
    RecordField *fields;
    unsigned size;
} CommonType;

struct Variable {
    unsigned name_slot;
    double number;
    char *text;
    char kind;
    signed char scope;
    unsigned char first_upper;
    unsigned char constant, indexed_constant, shared, array, typed, dynamic, persistent;
    unsigned common;
    unsigned fixed_width;
    unsigned cells, dimensions;
    Bounds *bounds;
    void *elements;
    unsigned frame_slot;
    Reference reference;
};

enum { B_FOR, B_NEXT, B_DO, B_LOOP, B_WHILE, B_WEND, B_IF, B_ELSEIF, B_ELSE, B_ENDIF, B_SELECT, B_CASE, B_ENDSELECT };
enum { C_ALWAYS, C_WHILE, C_UNTIL };

typedef struct {
    unsigned start, body, end, first_block;
    const char __far *label;
    unsigned label_length;
} Line;

typedef struct {
    Position at, after;
    unsigned peer, owner, finish;
    int kind;
} Block;

typedef struct {
    int variable;
    unsigned block;
    double last, step;
    long ilast, istep;
    Variable *fast_var;
    unsigned body_pc;
    unsigned char fast_i16;
} ForFrame;

typedef struct {
    char name[BASIC_NAME_MAX + 1];
    char result_kind;
    unsigned param_count;
    char param_names[PARAM_MAX][16];
    char param_kinds[PARAM_MAX];
    int param_var_ids[PARAM_MAX];
    Position expr_at;
} DefFn;

typedef struct {
    char kind;
    unsigned width;
    int array;
} ParameterType;

typedef struct {
    char name[BASIC_NAME_MAX + 1];
    char result_kind;
    unsigned line, end, count, locals, first_var, last_var;
    int function, params[PARAM_MAX], result, persistent;
    unsigned declared_count;
    ParameterType declared_params[PARAM_MAX];
    int declared;
} Procedure;

typedef struct Activation {
    Variable *locals;
    struct Activation *prev;
    unsigned for_base, call_base;
} Activation;

typedef struct {
    unsigned line, pos, start, length, line_end;
    int token;
    unsigned char first_upper;
    char number_kind;
    double number;
} Parser;

#define INIT_VALUE(vp) do { \
    Value *iv_ = (vp); \
    iv_->number = 0.0; \
    iv_->record = NULL; \
    iv_->wide = NULL; \
    iv_->length = 0; \
    iv_->string = 0; \
    iv_->kind = 0; \
    iv_->text[0] = '\0'; \
} while (0)

#define COPY_VALUE(dst, src) do { \
    const Value *sv_ = (src); \
    Value *dv_ = (dst); \
    dv_->number = sv_->number; \
    dv_->record = sv_->record; \
    dv_->wide = sv_->wide; \
    dv_->length = sv_->length; \
    dv_->string = sv_->string; \
    dv_->kind = sv_->kind; \
    if (sv_->string && sv_->length <= CKB_INLINE_STRING) \
        memcpy(dv_->text, sv_->text, sv_->length + 1U); \
    else \
        dv_->text[0] = '\0'; \
} while (0)

struct FieldBinding {
    FieldBinding *next;
    Variable *variable;
    unsigned offset, width, common;
};

typedef struct {
    FILE *stream;
    CkbSerial *serial;
    unsigned column;
    int mode, access;
    unsigned record_length;
    char *record;
    FieldBinding *fields;
} Channel;

/* Prefix cross-module runtime symbols with ckbr_ to keep OMF linkage clean. */
#define variables ckbr_variables
#define channels ckbr_channels
#define screen_column ckbr_screen_column
#define printer_column ckbr_printer_column
#define error_message ckbr_error_message
#define mbf_mode ckbr_mbf_mode
#define records ckbr_records
#define record_count ckbr_record_count
#define common_ids ckbr_common_ids
#define common_count ckbr_common_count

#define fail ckbr_fail
#define scan ckbr_scan
#define word ckbr_word
#define take ckbr_take
#define take_word ckbr_take_word
#define expect ckbr_expect
#define expect_word ckbr_expect_word
#define end_statement ckbr_end_statement
#define identifier ckbr_identifier
#define unsuffixed_name ckbr_unsuffixed_name
#define named_variable ckbr_named_variable
#define record_type ckbr_record_type
#define record_size ckbr_record_size
#define finite_number ckbr_finite_number
#define input_interrupted ckbr_input_interrupted
#define member_size ckbr_member_size
#define array_element_size ckbr_array_element_size
#define convert_number ckbr_convert_number
#define array_storage ckbr_array_storage
#define valid_cell ckbr_valid_cell
#define field_data ckbr_field_data
#define unbind_field ckbr_unbind_field
#define record_data ckbr_record_data
#define packed_number ckbr_packed_number
#define assign_reference ckbr_assign_reference
#define fetch_reference ckbr_fetch_reference
#define string_value ckbr_string_value
#define integer_operand ckbr_integer_operand
#define expression ckbr_expression
#define numeric ckbr_numeric
#define implicit_array ckbr_implicit_array
#define array_cell ckbr_array_cell
#define lvalue ckbr_lvalue
#define write_bytes ckbr_write_bytes

#define file_number ckbr_file_number
#define file_open ckbr_file_open
#define file_eof ckbr_file_eof
#define file_position ckbr_file_position
#define seek_file ckbr_seek_file
#define print_statement ckbr_print_statement
#define write_statement ckbr_write_statement
#define open_statement ckbr_open_statement
#define file_transfer ckbr_file_transfer
#define file_lock_statement ckbr_file_lock_statement
#define field_statement ckbr_field_statement
#define release_channel ckbr_release_channel
#define close_file ckbr_close_file
#define close_channels ckbr_close_channels
#define rename_statement ckbr_rename_statement
#define path_statement ckbr_path_statement
#define environment_statement ckbr_environment_statement
#define shell_statement ckbr_shell_statement
#define line_input_statement ckbr_line_input_statement
#define input_file_statement ckbr_input_file_statement
#define input_string ckbr_input_string
#define environment_function ckbr_environment_function
#define builtin ckbr_builtin
#define swap_statement ckbr_swap_statement
#define justify_assign ckbr_justify_assign
#define mid_assign ckbr_mid_assign
#define parse_gfx_coord ckbr_parse_gfx_coord
#define gfx_line_statement ckbr_gfx_line_statement
#define gfx_circle_statement ckbr_gfx_circle_statement
#define gfx_paint_statement ckbr_gfx_paint_statement
#define gfx_get_statement ckbr_gfx_get_statement
#define gfx_put_statement ckbr_gfx_put_statement
#define array_bound ckbr_array_bound
#define clear_array ckbr_clear_array
#define release_array ckbr_release_array
#define dimension_array ckbr_dimension_array
#define erase_statement ckbr_erase_statement
#define console_line_input ckbr_console_line_input
#define input_statement ckbr_input_statement
#define free_common ckbr_free_common
#define save_common ckbr_save_common
#define restore_common ckbr_restore_common
#define carry_fields ckbr_carry_fields
#define cls_statement ckbr_cls_statement
#define color_statement ckbr_color_statement
#define screen_statement ckbr_screen_statement
#define width_statement ckbr_width_statement
#define palette_statement ckbr_palette_statement
#define locate_statement ckbr_locate_statement
#define view_statement ckbr_view_statement
#define poke_statement ckbr_poke_statement
#define sound_statement ckbr_sound_statement
#define play_statement ckbr_play_statement
#define sleep_statement ckbr_sleep_statement
#define pset_statement ckbr_pset_statement
#define close_statement ckbr_close_statement
#define seek_statement ckbr_seek_statement
#define peek_function ckbr_peek_function
#define point_function ckbr_point_function

#define source ckbr_source
#define lines ckbr_lines
#define line_scope ckbr_line_scope
#define blocks ckbr_blocks
#define procedures ckbr_procedures
#define def_fns ckbr_def_fns
#define for_stack ckbr_for_stack
#define calls ckbr_calls
#define error_handler ckbr_error_handler
#define line_count ckbr_line_count
#define variable_count ckbr_variable_count
#define block_count ckbr_block_count
#define for_depth ckbr_for_depth
#define call_depth ckbr_call_depth
#define procedure_count ckbr_procedure_count
#define procedure_depth ckbr_procedure_depth
#define def_fn_count ckbr_def_fn_count
#define current_scope ckbr_current_scope
#define activation ckbr_activation
#define returning ckbr_returning
#define stopped ckbr_stopped
#define jumped ckbr_jumped
#define entering_elseif ckbr_entering_elseif
#define next_position ckbr_next_position
#define first_error ckbr_first_error
#define handling_error ckbr_handling_error
#define resume_scope ckbr_resume_scope
#define error_resume ckbr_error_resume
#define error_resume_next ckbr_error_resume_next
#define trapped_error_code ckbr_trapped_error_code
#define trapped_error_line ckbr_trapped_error_line
#define random_state ckbr_random_state
#define option_base ckbr_option_base
#define default_kind ckbr_default_kind
#define any_field_bound ckbr_any_field_bound
#define pause_execution ckbr_pause_execution

#define variable_storage ckbr_variable_storage
#define reference_at ckbr_reference_at
#define try_exact_int21 ckbr_try_exact_int21
#define round_even ckbr_round_even
#define current_def_seg ckbr_current_def_seg
#define target_label ckbr_target_label
#define lookup_proc_token ckbr_lookup_proc_token
#define lookup_deffn_token ckbr_lookup_deffn_token
#define block_at_pos ckbr_block_at_pos
#define exec_fallback_stmt ckbr_exec_fallback_stmt
#define alloc_proc_locals ckbr_alloc_proc_locals
#define free_proc_locals ckbr_free_proc_locals
#define runtime_error_code ckbr_runtime_error_code
#define error_line_number ckbr_error_line_number
#define pause_at_pos ckbr_pause_at_pos
#define variable_string_store_raw ckbr_variable_string_store_raw
#define ckb_vm_execute ckbr_vm_execute

extern Variable variables[VALUE_MAX];
extern Channel channels[FILE_MAX];
extern unsigned screen_column, printer_column;
extern const char *error_message;
extern int mbf_mode;
extern RecordType records[RECORD_MAX];
extern unsigned record_count;
extern unsigned __far *common_ids;
extern unsigned common_count;

extern char __far source[];
extern Line __far *lines;
extern int __far *line_scope;
extern Block __far *blocks;
extern Procedure __far *procedures;
extern DefFn __far *def_fns;
extern ForFrame for_stack[STACK_MAX];
extern Position calls[STACK_MAX];
extern unsigned error_handler[PROC_MAX + 1];
extern unsigned line_count, variable_count, block_count, for_depth, call_depth;
extern unsigned procedure_count, procedure_depth, def_fn_count;
extern int current_scope, returning, stopped, jumped, entering_elseif;
extern Activation *activation;
extern Position next_position, first_error;
extern int handling_error, resume_scope;
extern Position error_resume, error_resume_next;
extern unsigned trapped_error_code, trapped_error_line;
extern unsigned long random_state;
extern long option_base;
extern char default_kind[26];
extern int any_field_bound;
extern CkbRunPause pause_execution;

/* Exported from CKBRUN.C */
void fail(const Parser *p, const char *message);
void scan(Parser *p);
int word(const Parser *p, const char *text);
int take(Parser *p, int token);
int take_word(Parser *p, const char *text);
int expect(Parser *p, int token, const char *message);
int expect_word(Parser *p, const char *text, const char *message);
int end_statement(const Parser *p);
unsigned identifier(Parser *p, char *name, char *kind);
int unsuffixed_name(Parser *p);
int named_variable(Parser *p, const char *name, char kind, int array,
                   int defining, int unsuffixed, int typed);
RecordType *record_type(char kind);
unsigned record_size(char kind);
int finite_number(double n);
int input_interrupted(Parser *p, Position at);
unsigned member_size(char kind, unsigned width);
unsigned array_element_size(const Variable *v);
void convert_number(Parser *p, Value *v, char kind, int run);
Variable *array_storage(int id);
int valid_cell(Parser *p, const Variable *v, unsigned cell);
char *field_data(const Variable *variable, unsigned *width);
void unbind_field(const Variable *variable);
char *record_data(Parser *p, Reference ref);
void packed_number(char *item, char kind, double *number, int store);
void assign_reference(Parser *p, Reference ref, Value *value, int run);
void fetch_reference(Parser *p, Reference ref, int run, Value *value);
void string_value(Parser *p, Value *v);
long integer_operand(Parser *p, double value, int run);
void expression(Parser *p, Value *v, int minimum, unsigned depth, int run);
double numeric(Parser *p, int run);
void implicit_array(Parser *p, Variable *v, unsigned dimensions);
unsigned array_cell(Parser *p, int id, int run);
Reference lvalue(Parser *p, int run);
void write_bytes(Parser *p, FILE *stream, unsigned *column, const char *s, size_t n);
Variable *variable_storage(int id);
Reference reference_at(int id, unsigned cell);
int try_exact_int21(double value, long *out);
double round_even(double n);
unsigned current_def_seg(void);
unsigned target_label(Parser *p);
int lookup_proc_token(const Parser *p);
int lookup_deffn_token(const Parser *p);
unsigned block_at_pos(Position at);
void exec_fallback_stmt(Position at, Position *out_after);
int alloc_proc_locals(Parser *p, int proc_id, Variable **out_locals, int *out_pooled);
void free_proc_locals(Variable *locals, unsigned n_locals, int pooled);
unsigned runtime_error_code(const char *message);
unsigned error_line_number(unsigned index);
int pause_at_pos(Position at, const char *message);
void variable_string_store_raw(Parser *p, Variable *v, unsigned cell, const char __far *src, unsigned src_len);

/* Exported from CKBVM.C */
int ckb_vm_execute(void);

/* Exported from CKBIO.C */
Channel *file_number(Parser *p, int run);
int file_open(const Channel *file);
int file_eof(Parser *p, Channel *file);
long file_position(Parser *p, Channel *file, int which);
void seek_file(Parser *p, Channel *file, long position);
void print_statement(Parser *p, int run, int printer);
void write_statement(Parser *p, int run);
void open_statement(Parser *p, int run);
void file_transfer(Parser *p, int run, int writing);
void file_lock_statement(Parser *p, int run, int unlocking);
void field_statement(Parser *p, int run);
const char *release_channel(Channel *file);
void close_file(Parser *p, Channel *file);
int close_channels(void);
void rename_statement(Parser *p, int run);
void path_statement(Parser *p, int run, int operation);
void environment_statement(Parser *p, int run);
void shell_statement(Parser *p, int run);
void line_input_statement(Parser *p, int run);
void input_file_statement(Parser *p, int run);
void input_string(Parser *p, Value *v, unsigned depth, int run);
void environment_function(Parser *p, Value *value, unsigned depth, int run);
int builtin(Parser *p, Value *v, unsigned depth, int run, int id, int length);
void swap_statement(Parser *p, int run);
void justify_assign(Parser *p, int run, int right);
void mid_assign(Parser *p, int run);
int parse_gfx_coord(Parser *p, int run, int *x_out, int *y_out);
void gfx_line_statement(Parser *p, int run);
void gfx_circle_statement(Parser *p, int run);
void gfx_paint_statement(Parser *p, int run);
void gfx_get_statement(Parser *p, int run);
void gfx_put_statement(Parser *p, int run);
void array_bound(Parser *p, Value *value, int run);
void clear_array(Variable *v);
void release_array(Variable *v);
void dimension_array(Parser *p, Variable *v, const Bounds *bounds, unsigned dimensions, unsigned cells);
void erase_statement(Parser *p, int run);
void console_line_input(Parser *p, int run, Position at);
void input_statement(Parser *p, int run, Position at);
void free_common(CommonValue *saved, unsigned count, CommonType *types);
void save_common(CommonValue *saved, CommonType *types);
void restore_common(Parser *p, CommonValue *saved, const CommonType *types, Variable *v);
void carry_fields(int restore);
void cls_statement(Parser *p, int run);
void color_statement(Parser *p, int run);
void screen_statement(Parser *p, int run);
void width_statement(Parser *p, int run);
void palette_statement(Parser *p, int run);
void locate_statement(Parser *p, int run);
void view_statement(Parser *p, int run);
void poke_statement(Parser *p, int run, unsigned seg);
void sound_statement(Parser *p, int run);
void play_statement(Parser *p, int run);
void sleep_statement(Parser *p, int run);
void pset_statement(Parser *p, int run);
void close_statement(Parser *p, int run);
void seek_statement(Parser *p, int run);
void peek_function(Parser *p, Value *v, int run, unsigned seg);
void point_function(Parser *p, Value *v, int run);

#endif
