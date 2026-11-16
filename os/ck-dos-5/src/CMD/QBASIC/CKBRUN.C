/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "CKBRUN.H"
#include "CKBSTR.H"
#include "CKBCON.H"
#include "CKBFMT.H"
#include "CKBNUM.H"
#include "CKENV.H"
#include "CKBSER.H"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__WATCOMC__) || defined(_MSDOS)
#include <direct.h>
#include <dos.h>
#include <io.h>
#ifdef __WATCOMC__
#include <i86.h>
#else
#include <dos.h>
#endif
#include <malloc.h>
#include <share.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#define __far
#define _fmemcpy memcpy
#define _fmemchr memchr
#endif

/* Source fits a single far segment. Parser tables and runtime state use
 * near memory. Keep offsets small enough for the 16-bit DOS guest. */
#define SOURCE_MAX 16384
#define LINE_MAX 512
#define VALUE_MAX 88
#define BASIC_NAME_MAX 40
#define STRING_MAX CKB_STRING_MAX
#define TEXT_INPUT_MAX CKB_INLINE_STRING
#define BLOCK_MAX 128
#define STACK_MAX 24
#define DEPTH_MAX 16
#define FILE_MAX 16
#define PROC_MAX 16
#define PARAM_MAX 8
#define DIMENSION_MAX 60
#define ARRAY_CELL_MAX 4096
#define RECORD_MAX 16
#if defined(__WATCOMC__) || defined(_MSDOS)
#define IMMEDIATE_START (SOURCE_MAX + 1)
#define SOURCE_CAPACITY (IMMEDIATE_START + CKB_IMMEDIATE_LIMIT + 1)
#define LINE_CAPACITY (LINE_MAX + 1)
#else
#define SOURCE_CAPACITY (SOURCE_MAX + 1)
#define LINE_CAPACITY LINE_MAX
#endif

enum { T_NUMBER = 256, T_STRING, T_NAME, T_LE, T_GE, T_NE };
enum { B_FOR, B_NEXT, B_DO, B_LOOP, B_WHILE, B_WEND, B_IF, B_ELSE, B_ENDIF, B_SELECT, B_CASE, B_ENDSELECT };
enum { C_ALWAYS, C_WHILE, C_UNTIL };
enum { ARRAY_IMPLICIT, ARRAY_STATIC, ARRAY_DYNAMIC };

typedef CkbValue Value;
typedef struct Variable Variable;

typedef struct {
    Variable *target;
    unsigned cell, offset, width;
    char kind;
} Reference;

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
    long lower, upper;
} Bounds;

struct Variable {
    char name[BASIC_NAME_MAX + 1];
    double number, indexed_number;
    char *text;
    char kind;
    int scope, constant, indexed_constant, shared, array, typed;
    unsigned shared_scopes;
    unsigned common;
    unsigned fixed_width;
    unsigned cells, dimensions;
    int dynamic;
    Bounds *bounds;
    void *elements;
    unsigned frame_slot;
    int persistent;
    Reference reference;
};

typedef struct {
    unsigned start, body, end;
    const char __far *label;
    unsigned label_length;
} Line;

typedef struct {
    unsigned line, offset;
} Position;

typedef struct {
    Position at, after;
    unsigned peer, owner, finish;
    int kind;
} Block;

typedef struct {
    unsigned line, pos, start, length;
    int token;
    double number;
    char number_kind;
} Parser;

typedef struct {
    int variable;
    unsigned block;
    double last, step;
} ForFrame;

static char __far source[SOURCE_CAPACITY];
static Line lines[LINE_CAPACITY];
static Variable variables[VALUE_MAX];
static RecordType records[RECORD_MAX];
static unsigned record_count;
static Block blocks[BLOCK_MAX];
static unsigned block_stack[STACK_MAX];
static ForFrame for_stack[STACK_MAX];
static Position calls[STACK_MAX];
static unsigned line_count, variable_count, block_count, block_depth, for_depth, call_depth;
enum { FILE_INPUT, FILE_OUTPUT, FILE_APPEND, FILE_BINARY, FILE_RANDOM };
enum { ACCESS_READ = 1, ACCESS_WRITE = 2 };
enum { SHARE_DEFAULT, SHARE_ALLOW, SHARE_DENY_READ, SHARE_DENY_WRITE, SHARE_DENY_BOTH };
enum { FILE_SEEK, FILE_LOC, FILE_LOF };
typedef struct FieldBinding FieldBinding;

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

static unsigned screen_column, printer_column;
static Channel channels[FILE_MAX];
static Position data_statements[BLOCK_MAX];
static unsigned data_count, data_index;
static Position next_position, first_error;
static unsigned data_offset;
static const char *error_message;
static int stopped, checking, jumped, indexing_constant;
static CkbRunPause pause_execution;
static CkbRunBreak break_execution;
static void *pause_context;
static int aborted;
static char restart_path[TEXT_INPUT_MAX + 1];
static enum { RESTART_NONE, RESTART_RUN, RESTART_CHAIN } restart_kind;
static unsigned common_ids[VALUE_MAX], common_count;
static char default_kind[26];
static long option_base;
static int dynamic_arrays;
static unsigned long random_state;
static int mbf_mode;
#if defined(__WATCOMC__) || defined(_MSDOS)
static int immediate_output, immediate_idle, immediate_active;
#endif

typedef struct {
    char kind;
    unsigned width;
    int array;
} ParameterType;

typedef struct {
    char name[BASIC_NAME_MAX + 1];
    char result_kind;
    unsigned line, end, count, locals;
    int function, params[PARAM_MAX], result, persistent;
    unsigned declared_count;
    ParameterType declared_params[PARAM_MAX];
    int declared;
} Procedure;

typedef struct {
    Variable *locals;
    unsigned for_base, call_base;
} Activation;

static Activation *activation;
static Procedure procedures[PROC_MAX];
static unsigned procedure_count, procedure_depth;
static int current_scope, returning;
static unsigned error_handler[PROC_MAX + 1];
static Position error_resume, error_resume_next;
static int handling_error, resume_scope;
static unsigned trapped_error_code, trapped_error_line;
static int line_scope[LINE_MAX];

static const struct { unsigned code; const char *text; } runtime_errors[] = {
        {3, "RETURN without GOSUB"}, {4, "Out of DATA"},
        {5, "Illegal function call"}, {6, "Overflow"}, {7, "Out of memory"},
        {9, "Subscript out of range"}, {10, "Duplicate definition"}, {11, "Division by zero"},
        {13, "Type mismatch"}, {14, "String too long"},
        {20, "RESUME without error"}, {28, "Out of stack space"}, {52, "Bad file number"},
        {53, "File not found"}, {54, "Bad file mode"}, {55, "File already open"},
        {57, "Read error"}, {57, "Write error"}, {58, "File already exists"},
        {62, "Input past end of file"}, {63, "Bad record number"},
        {50, "FIELD overflow"}, {64, "Bad file name"}, {67, "Too many files"},
        {68, "Device unavailable"}, {69, "Communications buffer overflow"},
        {24, "Device timeout"}, {70, "Permission denied"}, {75, "Path/File access error"}, {76, "Path not found"},
};

static unsigned runtime_error_code(const char *message)
{
    unsigned i;
    for (i = 0; i < sizeof(runtime_errors) / sizeof(runtime_errors[0]); ++i)
        if (strcmp(message, runtime_errors[i].text) == 0) return runtime_errors[i].code;
    return 0;
}

static const char *runtime_error_text(unsigned code)
{
    unsigned i;
    for (i = 0; i < sizeof(runtime_errors) / sizeof(runtime_errors[0]); ++i)
        if (runtime_errors[i].code == code) return runtime_errors[i].text;
    return "Unprintable error";
}

static unsigned error_line_number(unsigned index)
{
    int scope;
    if (index >= line_count) return 0;
    scope = line_scope[index];
    do {
        const Line *line = &lines[index];
        if (line_scope[index] != scope) break;
        if (line->label && isdigit((unsigned char)*line->label)) {
            unsigned long number = 0;
            unsigned k;
            for (k = 0; k < line->label_length && number <= 65535UL; ++k)
                number = number * 10UL + (unsigned)(line->label[k] - '0');
            if (number <= 65535UL) return (unsigned)number;
        }
    } while (index--);
    return 0;
}

static int same(const char __far *a, unsigned n, const char *b)
{
    while (n && *b && toupper((unsigned char)*a) == (unsigned char)*b) {
        ++a;
        ++b;
        --n;
    }
    return !n && !*b;
}

static int name_start(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int name_char(int c)
{
    return isalnum((unsigned char)c) || c == '.' || c == '_';
}

static Position position(unsigned line, unsigned offset)
{
    Position result;
    result.line = line;
    result.offset = offset;
    return result;
}

static void fail(const Parser *p, const char *message)
{
    if (error_message) return;
    first_error = position(p->line, p->start);
    error_message = message;
}

static int stack_room(Parser *p, unsigned reserve)
{
#if defined(__WATCOMC__) || defined(_MSDOS)
    if (stackavail() < reserve) {
        fail(p, "Out of stack space");
        return 0;
    }
#else
    (void)p;
    (void)reserve;
#endif
    return 1;
}

static void scan_radix(Parser *p, unsigned end)
{
    unsigned base = 8, digit, digits;
    unsigned long bits = 0;
    if (p->pos < end) {
        int marker = toupper((unsigned char)source[p->pos]);
        if (marker == 'H') base = 16;
        if (marker == 'H' || marker == 'O') ++p->pos;
    }
    digits = p->pos;
    while (p->pos < end) {
        int c = toupper((unsigned char)source[p->pos]);
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else break;
        if (digit >= base || bits > (0xffffffffUL - digit) / base) {
            fail(p, "Invalid number");
            return;
        }
        bits = bits * base + digit;
        ++p->pos;
    }
    if (p->pos == digits) {
        fail(p, "Invalid number");
        return;
    }
    p->number_kind = bits > 0xffffUL ? '&' : '%';
    if (p->pos < end && source[p->pos] == '&') {
        ++p->pos;
        p->number_kind = '&';
    }
    p->number = (double)bits;
    if (p->number_kind == '%' && bits >= 0x8000UL) p->number -= 65536.0;
    if (p->number_kind == '&' && bits >= 0x80000000UL) p->number -= 4294967296.0;
    p->token = T_NUMBER;
}

static unsigned mantissa_digits(const char *number)
{
    unsigned digits = 0;
    for (; *number && *number != 'D' && *number != 'd' &&
           *number != 'E' && *number != 'e'; ++number) {
        if (*number >= '0' && *number <= '9') ++digits;
    }
    return digits;
}

static void scan(Parser *p)
{
    unsigned end = lines[p->line].end;
    int c;
    const char __far *suffix;
    char *tail;
    while (p->pos < end && (source[p->pos] == ' ' || source[p->pos] == '\t')) ++p->pos;
    p->start = p->pos;
    p->length = 0;
    p->token = 0;
    if (p->pos >= end || source[p->pos] == '\'') return;
    c = (unsigned char)source[p->pos++];
    p->token = c;
    if (c == '"') {
        p->token = T_STRING;
        ++p->start;
        while (p->pos < end && source[p->pos] != '"') ++p->pos;
        p->length = p->pos - p->start;
        if (p->pos == end) fail(p, "Unterminated string");
        else ++p->pos;
        if (p->length > TEXT_INPUT_MAX) fail(p, "String too long");
        return;
    }
    if (name_start(c)) {
        p->token = T_NAME;
        while (p->pos < end && name_char(source[p->pos])) ++p->pos;
        if (p->pos < end && strchr("$%&!#", source[p->pos])) ++p->pos;
    } else if (c == '&') {
        scan_radix(p, end);
    } else if (isdigit(c) || (c == '.' && p->pos < end && isdigit((unsigned char)source[p->pos]))) {
        /* strtod also accepts ISO C hexadecimal literals on host compilers.
         * Find the BASIC decimal token ourselves before calling it. */
        char number[50];
        unsigned fraction = c == '.';
        unsigned digits;
        int floating = fraction, double_literal = 0;
        while (p->pos < end && isdigit((unsigned char)source[p->pos])) ++p->pos;
        if (!fraction && p->pos < end && source[p->pos] == '.') {
            floating = 1;
            ++p->pos;
            while (p->pos < end && isdigit((unsigned char)source[p->pos])) ++p->pos;
        }
        if (p->pos < end && strchr("EeDd", source[p->pos])) {
            floating = 1;
            double_literal = source[p->pos] == 'D' || source[p->pos] == 'd';
            ++p->pos;
            if (p->pos < end && strchr("+-", source[p->pos])) ++p->pos;
            while (p->pos < end && isdigit((unsigned char)source[p->pos])) ++p->pos;
        }
        p->length = p->pos - p->start;
        p->token = T_NUMBER;
        p->number = 0;
        if (p->length >= sizeof(number)) {
            fail(p, "Invalid number");
            return;
        }
        _fmemcpy(number, source + p->start, p->length);
        number[p->length] = 0;
        digits = mantissa_digits(number);
        tail = strchr(number, 'D');
        if (!tail) tail = strchr(number, 'd');
        if (tail) *tail = 'E';
        p->number = strtod(number, &tail);
        if (*tail) fail(p, "Invalid number");
        suffix = source + p->pos;
        if (p->pos < end && strchr("%&!#", *suffix)) {
            p->number_kind = *suffix;
            ++p->pos;
        } else if (!floating && p->number <= 32767.0) p->number_kind = '%';
        else if (!floating && p->number <= 2147483647.0) p->number_kind = '&';
        else p->number_kind = double_literal || digits >= 8 ? '#' : '!';
        return;
    } else if (p->pos < end) {
        int d = source[p->pos];
        if ((c == '<' && d == '=') || (c == '=' && d == '<')) p->token = T_LE;
        else if ((c == '>' && d == '=') || (c == '=' && d == '>')) p->token = T_GE;
        else if ((c == '<' && d == '>') || (c == '>' && d == '<')) p->token = T_NE;
        if (p->token != c) ++p->pos;
    }
    p->length = p->pos - p->start;
}

static int word(const Parser *p, const char *text)
{
    return p->token == T_NAME && same(source + p->start, p->length, text);
}

static int take(Parser *p, int token)
{
    if (p->token != token) return 0;
    scan(p);
    return 1;
}

static int take_word(Parser *p, const char *text)
{
    if (!word(p, text)) return 0;
    scan(p);
    return 1;
}

static int expect(Parser *p, int token, const char *message)
{
    if (take(p, token)) return 1;
    fail(p, message);
    return 0;
}

static int expect_word(Parser *p, const char *text, const char *message)
{
    if (take_word(p, text)) return 1;
    fail(p, message);
    return 0;
}

static int end_statement(const Parser *p)
{
    return !p->token || p->token == ':' || word(p, "ELSE");
}

static int error_value(Parser *p, Value *value, int run)
{
    int line = word(p, "ERL");
    if (!line && !word(p, "ERR")) return 0;
    scan(p);
    value->string = 0;
    value->kind = line ? '&' : '%';
    value->number = run ? (line ? trapped_error_line : trapped_error_code) : 0;
    return 1;
}

static void initialize_parser(Parser *p, Position at)
{
    p->line = at.line;
    p->pos = at.offset;
    scan(p);
}

static unsigned identifier(Parser *p, char *name, char *kind)
{
    unsigned i, length;
    if (p->token != T_NAME || p->length > BASIC_NAME_MAX) {
        fail(p, "Expected: variable");
        return 0;
    }
    length = p->length;
    *kind = source[p->start + length - 1];
    if (strchr("$%&!#", *kind)) --length;
    else *kind = default_kind[toupper((unsigned char)source[p->start]) - 'A'];
    for (i = 0; i < length; ++i)
        name[i] = (char)toupper((unsigned char)source[p->start + i]);
    name[length] = 0;
    scan(p);
    return length;
}

static RecordType *record_type(char kind)
{
    unsigned id = (unsigned char)kind;
    return id && id <= record_count ? &records[id - 1] : NULL;
}

static unsigned record_size(char kind)
{
    const RecordType *type = record_type(kind);
    return type ? type->size : 0;
}

static unsigned member_size(char kind, unsigned width)
{
    switch (kind) {
    case '$': return width;
    case '%': return 2;
    case '&': case '!': return 4;
    case '#': return 8;
    default: return record_size(kind);
    }
}

static unsigned array_element_size(const Variable *v)
{
    return v->kind == '$' ? sizeof(char *) : member_size(v->kind, 0);
}

static int visible_shared(const Variable *v)
{
    return v->shared ||
           (current_scope >= 0 && (v->shared_scopes & (1U << current_scope)));
}

static int variable_matches(const Variable *v, const char *name, char kind,
                            int array, int unsuffixed)
{
    return v->array == array && !strcmp(v->name, name) &&
           (v->kind == kind || (unsuffixed && v->typed));
}

/* Resolve without creating an implicit dotted name. A declared record in
 * the current scope, or a shared record, claims its own field syntax. */
static int record_variable(const char __far *name, unsigned length, int array)
{
    unsigned i;
    int shared = -1;
    for (i = 0; i < variable_count; ++i) {
        const Variable *v = &variables[i];
        if (v->array == array && v->typed && same(name, length, v->name)) {
            if (v->scope == current_scope) return record_size(v->kind) ? (int)i : -1;
            if (visible_shared(v) && record_size(v->kind)) shared = (int)i;
        }
    }
    return shared;
}

static int named_variable(Parser *p, const char *name, char kind, int array,
                          int defining, int unsuffixed, int typed)
{
    unsigned i;
    int local = -1, shared = -1;
    for (i = 0; i < variable_count; ++i) {
        Variable *v = &variables[i];
        if (variable_matches(v, name, kind, array, unsuffixed)) {
            if (v->scope == current_scope) {
                if (typed) {
                    if (v->typed && v->kind != kind) {
                        fail(p, "Duplicate definition");
                        return -1;
                    }
                    v->kind = kind;
                    v->typed = 1;
                }
                if (!unsuffixed || v->typed) return (int)i;
                local = (int)i;
            }
            if (defining && v->scope < 0 && current_scope >= 0 &&
                (v->shared_scopes & (1U << current_scope))) {
                fail(p, "Duplicate definition");
                return -1;
            }
            if (!defining && visible_shared(v) && (shared < 0 || v->typed))
                shared = (int)i;
        }
    }
    if (local >= 0) return local;
    if (shared >= 0) return shared;
    if (variable_count == VALUE_MAX) {
        fail(p, "Too many variables");
        return -1;
    }
    i = variable_count++;
    memset(&variables[i], 0, sizeof(variables[i]));
    strcpy(variables[i].name, name);
    variables[i].kind = kind;
    variables[i].scope = current_scope;
    variables[i].array = array;
    variables[i].typed = typed;
    return (int)i;
}

static int unsuffixed_name(Parser *p)
{
    return p->token == T_NAME && !strchr("$%&!#", source[p->start + p->length - 1]);
}

static int variable_mode(Parser *p, int defining)
{
    char kind, name[BASIC_NAME_MAX + 1];
    int unsuffixed = unsuffixed_name(p);
    if (!identifier(p, name, &kind)) return -1;
    return named_variable(p, name, kind, p->token == '(', defining, unsuffixed, 0);
}

static int variable(Parser *p)
{
    return variable_mode(p, 0);
}

/* Look up an existing constant without creating an implicit variable. */
static int known_constant(Parser *p)
{
    Parser copy = *p;
    char name[BASIC_NAME_MAX + 1], kind;
    int unsuffixed = unsuffixed_name(&copy), shared = -1;
    unsigned i;
    if (copy.token != T_NAME || !identifier(&copy, name, &kind)) return -1;
    for (i = 0; i < variable_count; ++i) {
        Variable *v = &variables[i];
        if (!variable_matches(v, name, kind, 0, unsuffixed)) continue;
        if (v->scope == current_scope) return v->indexed_constant ? (int)i : -1;
        if (v->indexed_constant && visible_shared(v)) shared = (int)i;
    }
    return shared;
}

static int type_letter(Parser *p)
{
    int c = toupper((unsigned char)source[p->start]);
    if (p->token != T_NAME || p->length != 1 || c < 'A' || c > 'Z') {
        fail(p, "Expected: letter");
        return 0;
    }
    scan(p);
    return c;
}

static void default_type_statement(Parser *p, char kind)
{
    int first, last;
    do {
        first = type_letter(p);
        last = take(p, '-') ? type_letter(p) : first;
        if (last < first) fail(p, "Invalid letter range");
        if (!error_message)
            for (; first <= last; ++first) default_kind[first - 'A'] = kind;
    } while (!error_message && take(p, ','));
}

static double round_even(double n)
{
    double low = floor(n);
    double fraction = n - low;
    return fraction > .5 || (fraction == .5 && fmod(low, 2) != 0) ? low + 1 : low;
}

static int finite_number(double n)
{
    return !ckb_num_is_special(n);
}

static void convert_number(Parser *p, Value *v, char kind, int run)
{
    double n = v->number;
    v->kind = kind;
    if (!run) return;
    if (kind == '%' || kind == '&') n = round_even(n);
    if (!finite_number(n) ||
        (kind == '%' && (n < -32768.0 || n > 32767.0)) ||
        (kind == '&' && (n < -2147483648.0 || n > 2147483647.0)) ||
        (kind == '!' && (n < -3.4028234663852886e38 || n > 3.4028234663852886e38))) {
        fail(p, "Overflow");
        return;
    }
    if (kind == '!') n = ckb_single_precision(n);
    v->number = n;
}

static Variable *variable_storage(int id)
{
    unsigned slot = variables[id].frame_slot;
    return slot ? &activation->locals[slot - 1] : &variables[id];
}

static Variable *array_storage(int id)
{
    Variable *v = variable_storage(id);
    return v->reference.target ? v->reference.target : v;
}

/* Store the ultimate owner, since a recursive call can bind the same symbol
 * in another activation. Whole-array aliases keep the cell from this access. */
static Reference reference_at(int id, unsigned cell)
{
    Variable *v = variable_storage(id);
    Reference ref;
    if (v->reference.target) {
        ref = v->reference;
        if (v->array) ref.cell = cell;
        return ref;
    }
    ref.target = v;
    ref.cell = cell;
    ref.offset = 0;
    ref.width = v->fixed_width;
    ref.kind = v->kind;
    return ref;
}

static int valid_cell(Parser *p, const Variable *v, unsigned cell)
{
    if (v->array && (!v->elements || cell >= v->cells)) {
        fail(p, "Subscript out of range");
        return 0;
    }
    return 1;
}

/* Bind resolved storage, so aliases to scalar FIELD variables see the
 * same record bytes and recursion can keep independent local bindings. */
static FieldBinding **find_field(Channel *file, const Variable *variable)
{
    FieldBinding **binding = &file->fields;
    while (*binding && (*binding)->variable != variable) binding = &(*binding)->next;
    return binding;
}

static char *field_data(const Variable *variable, unsigned *width)
{
    unsigned i;
    if (variable->kind != '$' || variable->array || variable->fixed_width) return NULL;
    for (i = 0; i < FILE_MAX; ++i) {
        Channel *file = &channels[i];
        const FieldBinding *binding;
        if (!file->fields) continue;
        binding = *find_field(file, variable);
        if (binding) {
            *width = binding->width;
            return file->record + binding->offset;
        }
    }
    return NULL;
}

static void unbind_field(const Variable *variable)
{
    unsigned i;
    if (variable->kind != '$' || variable->array || variable->fixed_width) return;
    for (i = 0; i < FILE_MAX; ++i) {
        FieldBinding **link = find_field(&channels[i], variable);
        FieldBinding *binding = *link;
        if (binding) {
            *link = binding->next;
            free(binding);
            return;
        }
    }
}

static void fixed_string_store(char *item, unsigned width, const Value *value);
static void fixed_string_fetch(Parser *p, Value *value, const char *item, unsigned width);

static void variable_string_store(Parser *p, Variable *v, unsigned cell, const Value *value)
{
    char **slot = v->array ? (char **)v->elements + cell : &v->text;
    char *copy = NULL;
    unsigned length = v->fixed_width ? v->fixed_width : value->length;
    unsigned header = v->fixed_width ? 0 : 2;
    if (length) {
        copy = malloc(length + header);
        if (!copy) {
            fail(p, "Out of memory");
            return;
        }
        if (v->fixed_width) fixed_string_store(copy, length, value);
        else {
            copy[0] = (char)length;
            copy[1] = (char)(length >> 8);
            memcpy(copy + header, ckb_string_bytes(value), length);
        }
    }
    unbind_field(v);
    free(*slot);
    *slot = copy;
}

static void variable_string_fetch(Parser *p, const Variable *v, unsigned cell, Value *value)
{
    const char *stored = v->array ? ((char *const *)v->elements)[cell] : v->text;
    unsigned width;
    const char *field = field_data(v, &width);
    if (field) fixed_string_fetch(p, value, field, width);
    else if (v->fixed_width) fixed_string_fetch(p, value, stored, v->fixed_width);
    else {
        width = stored ? (unsigned char)stored[0] | ((unsigned)(unsigned char)stored[1] << 8) : 0;
        fixed_string_fetch(p, value, stored ? stored + 2 : NULL, width);
    }
}

/* Fixed strings store exactly their declared width, with space padding. */
static void fixed_string_store(char *item, unsigned width, const Value *value)
{
    unsigned n = value->length;
    if (n > width) n = width;
    memcpy(item, ckb_string_bytes(value), n);
    memset(item + n, ' ', width - n);
}

static void fixed_string_fetch(Parser *p, Value *value, const char *item, unsigned width)
{
    char *bytes = ckb_string_buffer(value, width);
    if (!bytes) {
        fail(p, "Out of memory");
        return;
    }
    if (item) memcpy(bytes, item, width);
    else memset(bytes, 0, width);
    bytes[width] = 0;
}

static char *record_data(Parser *p, Reference ref)
{
    Variable *v = ref.target;
    unsigned size = record_size(v->kind);
    if (!v->elements) {
        v->elements = calloc(1, size);
        if (!v->elements) {
            fail(p, "Out of memory");
            return NULL;
        }
    }
    return (char *)v->elements + ref.cell * size + ref.offset;
}

static void record_float(char *bytes, void *value, unsigned size, int store)
{
    unsigned i;
    unsigned one = 1;
    unsigned char *native = value;
    for (i = 0; i < size; ++i) {
        unsigned at = *(unsigned char *)&one ? i : size - i - 1;
        if (store) bytes[i] = (char)native[at];
        else native[at] = (unsigned char)bytes[i];
    }
}

/* Pack numeric members and array elements in little-endian order. */
static void packed_number(char *item, char kind, double *number, int store)
{
    if (kind == '!' || kind == '#') {
        if (store) ckb_num_encode(item, *number, kind == '#', 0);
        else ckb_num_decode(number, item, kind == '#' ? 8 : 4, kind == '#', 0);
    } else {
        unsigned i, size = member_size(kind, 0);
        unsigned long n = store ? (unsigned long)(long)*number : 0;
        for (i = 0; i < size; ++i) {
            if (store) item[i] = (char)(n >> (8 * i));
            else n |= (unsigned long)(unsigned char)item[i] << (8 * i);
        }
        if (!store) {
            *number = (double)n;
            if ((unsigned char)item[size - 1] & 128)
                *number -= size == 2 ? 65536.0 : 4294967296.0;
        }
    }
}

static void assign_reference(Parser *p, Reference ref, Value *value, int run)
{
    Variable *v = ref.target;
    unsigned size = record_size(ref.kind);
    char *item;
    if (!v) return;
    if ((ref.kind == '$') != value->string ||
        ((size || record_size(value->kind)) && ref.kind != value->kind)) {
        fail(p, "Type mismatch");
        return;
    }
    if (v->constant) {
        fail(p, "Cannot assign to a constant");
        return;
    }
    if (!run || !valid_cell(p, v, ref.cell)) return;
    if (!value->string && !size &&
        !(ref.kind == value->kind && ckb_num_is_special(value->number))) {
        convert_number(p, value, ref.kind, run);
        if (error_message) return;
    }
    if (record_size(v->kind)) {
        item = record_data(p, ref);
        if (!item) return;
        if (size) memmove(item, value->record, size);
        else if (value->string) fixed_string_store(item, ref.width, value);
        else packed_number(item, ref.kind, &value->number, 1);
    } else if (value->string) variable_string_store(p, v, ref.cell, value);
    else if (v->array) {
        item = (char *)v->elements + ref.cell * array_element_size(v);
        packed_number(item, ref.kind, &value->number, 1);
    }
    else v->number = value->number;
}

static void fetch_reference(Parser *p, Reference ref, int run, Value *value)
{
    const Variable *v = ref.target;
    char *item;
    memset(value, 0, sizeof(*value));
    if (!v) return;
    value->string = ref.kind == '$';
    value->kind = ref.kind;
    if (!run || !valid_cell(p, v, ref.cell)) return;
    if (record_size(v->kind)) {
        item = record_data(p, ref);
        if (!item) return;
        if (record_size(ref.kind)) value->record = item;
        else if (value->string) fixed_string_fetch(p, value, item, ref.width);
        else packed_number(item, ref.kind, &value->number, 0);
    } else if (value->string) variable_string_fetch(p, v, ref.cell, value);
    else if (v->array) {
        item = (char *)v->elements + ref.cell * array_element_size(v);
        packed_number(item, ref.kind, &value->number, 0);
    } else value->number = v->number;
}

static void fetch(Parser *p, int id, unsigned cell, int run, Value *value)
{
    fetch_reference(p, reference_at(id, cell), run, value);
}

static void assign(Parser *p, int id, Value *value, int run)
{
    if (id >= 0) assign_reference(p, reference_at(id, 0), value, run);
}

static void string_value(Parser *p, Value *v)
{
    unsigned n = p->length > TEXT_INPUT_MAX ? TEXT_INPUT_MAX : p->length;
    v->string = 1;
    v->length = n;
    _fmemcpy(v->text, source + p->start, n);
    v->text[n] = 0;
    scan(p);
}

/* Left-associative powers, lower unary sign precedence, and BASIC's
 * comparison-before-NOT ordering. */
static int precedence(const Parser *p)
{
    if (p->token == '^') return 10;
    if (p->token == '*' || p->token == '/') return 8;
    if (p->token == '\\') return 7;
    if (word(p, "MOD")) return 6;
    if (p->token == '+' || p->token == '-') return 5;
    if (p->token == '=' || p->token == '<' || p->token == '>' ||
        p->token == T_LE || p->token == T_GE || p->token == T_NE) return 4;
    if (word(p, "AND")) return 2;
    if (word(p, "OR")) return 1;
    return 0;
}

static long integer_operand(Parser *p, double value, int run)
{
    double n;
    if (!run) return 0;
    n = round_even(value);
    if (!(n >= -2147483648.0 && n <= 2147483647.0)) {
        fail(p, "Overflow");
        return 0;
    }
    return (long)n;
}

static int numeric_rank(char kind)
{
    if (kind == '#') return 4;
    if (kind == '!' || !kind) return 3;
    if (kind == '&') return 2;
    return 1;
}

static void operate(Parser *p, Value *a, const Value *b, int op, int rank, int run)
{
    int comparison, result_rank;
    char result_kind;
    double n = a->number, m = b->number;
    long x, y;
    unsigned length;
    if (record_size(a->kind) || record_size(b->kind) || a->string != b->string ||
        (a->string && op != '+' && rank != 4)) {
        fail(p, "Type mismatch");
        return;
    }
    if (op == '+' && a->string) {
        const char *left = ckb_string_bytes(a);
        char *text;
        if (!run) return;
        length = a->length + b->length;
        if (length > STRING_MAX) {
            fail(p, "String too long");
            return;
        }
        text = ckb_string_buffer(a, length);
        if (!text) {
            fail(p, "Out of memory");
            return;
        }
        if (text != left) memcpy(text, left, length - b->length);
        memcpy(text + length - b->length, ckb_string_bytes(b), b->length + 1);
        return;
    }
    if (rank == 4) {
        comparison = a->string ? ckb_string_compare(a, b) : (n > m) - (n < m);
        switch (op) {
        case '=': n = comparison == 0; break;
        case '<': n = comparison < 0; break;
        case '>': n = comparison > 0; break;
        case T_LE: n = comparison <= 0; break;
        case T_GE: n = comparison >= 0; break;
        default: n = comparison != 0; break;
        }
        a->string = 0;
        a->number = n ? -1 : 0;
        a->kind = '%';
        return;
    }
    result_rank = numeric_rank(a->kind);
    if (numeric_rank(b->kind) > result_rank) result_rank = numeric_rank(b->kind);
    if ((op == '/' || op == '^') && result_rank < 3) result_rank = 3;
    if (rank <= 2 || op == '\\' || rank == 6)
        result_rank = result_rank == 1 ? 1 : 2;
    result_kind = result_rank == 4 ? '#' : result_rank == 3 ? '!' :
                  result_rank == 2 ? '&' : '%';
    a->kind = result_kind;
    if (!run) return;
    switch (op) {
    case '+': n += m; break;
    case '-': n -= m; break;
    case '*': n *= m; break;
    case '/':
        if (m == 0) fail(p, "Division by zero");
        else n /= m;
        break;
    case '^':
        if (n == 0 && m < 0) fail(p, "Division by zero");
        else if (n < 0 && m != floor(m)) fail(p, "Illegal function call");
        else n = pow(n, m);
        break;
    default:
        x = integer_operand(p, n, run);
        y = integer_operand(p, m, run);
        if (rank == 1) n = (double)(x | y);
        else if (rank == 2) n = (double)(x & y);
        else if (!y) fail(p, "Division by zero");
        else if (x == (-2147483647L - 1) && y == -1) fail(p, "Overflow");
        else if (op == '\\') n = (double)(x / y);
        else n = (double)(x % y);
        break;
    }
    a->number = n;
    if (!error_message) {
        /* Stored SINGLE values are rounded on assignment and conversion.
         * Keep expression intermediates at the working precision. */
        if (result_kind == '!') {
            if (!finite_number(n)) fail(p, "Overflow");
        } else convert_number(p, a, result_kind, run);
    }
}

static Channel *file_number(Parser *p, int run);
static double numeric(Parser *p, int run);
static unsigned array_cell(Parser *p, int id, int run);
static Reference lvalue(Parser *p, int run);
static void array_bound(Parser *p, Value *value, int run);
static int expression_call(Parser *p, Value *v, unsigned depth, int run);
static int file_eof(Parser *p, Channel *file);
static long file_position(Parser *p, Channel *file, int which);

static void expression(Parser *p, Value *v, int minimum, unsigned depth, int run);

/* The QBasic sequence advances in 24-bit state space. Seed folding uses
 * the numeric representation, independent of the host's byte order. */
static void seed_random(double seed)
{
    unsigned char bytes[8];
    unsigned i, mixed = 0;
    record_float((char *)bytes, &seed, 8, 1);
    for (i = 4; i < 8; i += 2)
        mixed ^= (unsigned)bytes[i] | ((unsigned)bytes[i + 1] << 8);
    random_state = (random_state & 255UL) | ((unsigned long)mixed << 8);
}

static void random_value(Parser *p, Value *v, unsigned depth, int run)
{
    double argument = 1;
    scan(p);
    if (take(p, '(')) {
        Value parameter;
        expression(p, &parameter, 1, depth + 1, run);
        expect(p, ')', "Expected: )");
        if (parameter.string || record_size(parameter.kind)) fail(p, "Type mismatch");
        else {
            convert_number(p, &parameter, '!', run);
            argument = parameter.number;
        }
    }
    v->kind = '!';
    if (!run || error_message) return;
    if (argument < 0) {
        unsigned char bytes[4];
        float single = (float)argument;
        record_float((char *)bytes, &single, 4, 1);
        random_state = ((unsigned long)bytes[0] + ((unsigned long)bytes[1] << 8) +
                        ((unsigned long)bytes[2] << 16) + bytes[3]) & 0xffffffUL;
    }
    if (argument != 0)
        random_state = (random_state * 16598013UL + 12820163UL) & 0xffffffUL;
    v->number = (double)random_state / 16777216.0;
}

/* Only an infix operator needs a right operand buffer. Atom evaluation
 * can call a FUNCTION before any such buffer is needed. */
static void expression_rhs(Parser *p, Value *v, int op, int rank, unsigned depth, int run)
{
    Value right;
    expression(p, &right, rank + 1, depth + 1, run);
    if (!error_message) operate(p, v, &right, op, rank, run);
}

static void expression_reference(Parser *p, Value *v, int minimum, unsigned depth, int run,
                                 Reference *reference)
{
    int rank, op;
    if (reference) reference->target = NULL;
    memset(v, 0, sizeof(*v));
    if (depth >= DEPTH_MAX) {
        fail(p, "Expression too complex");
        return;
    }
    /* Array subscripts can enter numeric parsing at a fresh expression depth. */
    if (!stack_room(p, 2048)) return;
    if (p->token == '+' || p->token == '-' || word(p, "NOT")) {
        op = p->token;
        scan(p);
        expression(p, v, op == T_NAME ? 3 : 9, depth + 1, run);
        if (v->string || record_size(v->kind)) fail(p, "Type mismatch");
        else if (op == '-') {
            v->number = -v->number;
            convert_number(p, v, v->kind ? v->kind : '!', run);
        }
        else if (op == T_NAME && run) v->number = (double)~integer_operand(p, v->number, run);
    } else if (take(p, '(')) {
        expression(p, v, 1, depth + 1, run);
        expect(p, ')', "Expected: )");
    } else if (p->token == T_NUMBER) {
        v->number = p->number;
        convert_number(p, v, p->number_kind, run);
        scan(p);
    } else if (p->token == T_STRING) {
        string_value(p, v);
    } else if (indexing_constant && p->token == T_NAME && !precedence(p)) {
        int id = known_constant(p);
        if (id < 0) fail(p, "Expected: constant");
        else {
            fetch(p, id, 0, run, v);
            scan(p);
        }
    } else if (take_word(p, "EOF")) {
        Channel *file;
        expect(p, '(', "Expected: (");
        file = file_number(p, run);
        expect(p, ')', "Expected: )");
        if (run && !error_message) v->number = file_eof(p, file) ? -1 : 0;
    } else if (word(p, "SEEK") || word(p, "LOC") || word(p, "LOF")) {
        int which = word(p, "SEEK") ? FILE_SEEK : word(p, "LOC") ? FILE_LOC : FILE_LOF;
        Channel *file;
        scan(p);
        expect(p, '(', "Expected: (");
        file = file_number(p, run);
        expect(p, ')', "Expected: )");
        v->kind = '&';
        if (run && !error_message) v->number = (double)file_position(p, file, which);
    } else if (word(p, "LBOUND") || word(p, "UBOUND")) {
        array_bound(p, v, run);
    } else if (p->token == T_NAME) {
        if (!expression_call(p, v, depth, run)) {
            Reference ref = lvalue(p, run);
            if (!error_message) {
                fetch_reference(p, ref, run, v);
                if (reference && ref.target && !ref.target->constant)
                    *reference = ref;
            }
        }
    } else {
        fail(p, "Expected: expression");
    }
    while (!error_message && (rank = precedence(p)) >= minimum) {
        if (reference) reference->target = NULL;
        op = p->token;
        scan(p);
        expression_rhs(p, v, op, rank, depth, run);
    }
}

static void expression(Parser *p, Value *v, int minimum, unsigned depth, int run)
{
    expression_reference(p, v, minimum, depth, run, NULL);
}

static double numeric(Parser *p, int run)
{
    Value v;
    expression(p, &v, 1, 0, run);
    if (v.string || record_size(v.kind)) fail(p, "Type mismatch");
    return v.number;
}

static void dimension_array(Parser *p, Variable *v, const Bounds *bounds, unsigned dimensions,
                            unsigned cells);

static void implicit_array(Parser *p, Variable *v, unsigned dimensions)
{
    Bounds bounds[DIMENSION_MAX];
    unsigned i, cells = 1, extent = (unsigned)(11 - option_base);
    if (dimensions > DIMENSION_MAX) {
        fail(p, "Subscript out of range");
        return;
    }
    for (i = 0; i < dimensions; ++i) {
        if (extent > ARRAY_CELL_MAX / cells) {
            fail(p, "Out of memory");
            return;
        }
        cells *= extent;
        bounds[i].lower = option_base;
        bounds[i].upper = 10;
    }
    dimension_array(p, v, bounds, dimensions, cells);
}

static unsigned array_cell(Parser *p, int id, int run)
{
    long indexes[DIMENSION_MAX];
    unsigned cell = 0, dimensions = 0, i;
    Variable *v;
    if (!take(p, '(')) return 0;
    do {
        if (dimensions == DIMENSION_MAX) {
            fail(p, "Subscript out of range");
            return 0;
        }
        indexes[dimensions++] = integer_operand(p, numeric(p, run), run);
    } while (!error_message && take(p, ','));
    expect(p, ')', "Expected: )");
    if (!run || id < 0 || error_message) return 0;
    v = array_storage(id);
    if (!v->elements && v->dynamic != ARRAY_DYNAMIC) implicit_array(p, v, dimensions);
    if (!v->elements || dimensions != v->dimensions) {
        fail(p, "Subscript out of range");
        return 0;
    }
    for (i = 0; i < dimensions; ++i) {
        const Bounds *bound = &v->bounds[i];
        if (indexes[i] < bound->lower || indexes[i] > bound->upper) {
            fail(p, "Subscript out of range");
            return 0;
        }
        cell = cell * (unsigned)(bound->upper - bound->lower + 1) +
               (unsigned)(indexes[i] - bound->lower);
    }
    return cell;
}

static int same_slice(const char __far *a, const char __far *b, unsigned length)
{
    while (length--) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) return 0;
    }
    return 1;
}

static int field_path(Parser *p, Reference *ref, const char __far *path, unsigned length)
{
    const RecordField *field;
    for (;;) {
        const RecordType *type = record_type(ref->kind);
        unsigned component = 0;
        while (component < length && path[component] != '.') ++component;
        field = type ? type->fields : NULL;
        while (field && (field->length != component ||
                         !same_slice(path, source + field->start, component)))
            field = field->next;
        if (!field) {
            fail(p, "Element not defined");
            return 0;
        }
        ref->offset += field->offset;
        ref->kind = field->kind;
        ref->width = field->width;
        if (component == length) return 1;
        path += component + 1;
        length -= component + 1;
    }
}

static Reference lvalue(Parser *p, int run)
{
    Reference ref;
    const char __far *name = source + p->start;
    const char __far *dot = p->token == T_NAME ? _fmemchr(name, '.', p->length) : NULL;
    unsigned length = p->length, cell;
    int id = dot ? record_variable(name, (unsigned)(dot - name), 0) : -1;
    memset(&ref, 0, sizeof(ref));
    if (id >= 0) {
        ref = reference_at(id, 0);
        if (field_path(p, &ref, dot + 1, length - (unsigned)(dot - name) - 1)) scan(p);
        return ref;
    }
    id = variable(p);
    cell = array_cell(p, id, run);
    if (id < 0 || error_message) return ref;
    ref = reference_at(id, cell);
    if (record_size(ref.kind) && take(p, '.')) {
        /* After a subscript the scanner sees a standalone separator. */
        if (p->token != T_NAME) fail(p, "Element not defined");
        else if (field_path(p, &ref, source + p->start, p->length)) scan(p);
    }
    return ref;
}

static void array_bound(Parser *p, Value *value, int run)
{
    int id, upper = take_word(p, "UBOUND");
    long dimension = 1;
    char name[BASIC_NAME_MAX + 1], kind;
    int unsuffixed;
    if (!upper) scan(p);
    expect(p, '(', "Expected: (");
    unsuffixed = unsuffixed_name(p);
    if (!identifier(p, name, &kind)) return;
    id = named_variable(p, name, kind, 1, 0, unsuffixed, 0);
    if (take(p, ',')) dimension = integer_operand(p, numeric(p, run), run);
    expect(p, ')', "Expected: )");
    value->kind = '&';
    if (run && id >= 0 && !error_message) {
        Variable *v = array_storage(id);
        if (!v->elements && v->dynamic != ARRAY_DYNAMIC) implicit_array(p, v, 1);
        if (!v->elements || dimension < 1 || dimension > v->dimensions)
            fail(p, "Subscript out of range");
        else
            value->number = upper ? v->bounds[dimension - 1].upper :
                                    v->bounds[dimension - 1].lower;
    }
}

/* A zero width denotes an ordinary, variable-length string. */
static unsigned fixed_string_width(Parser *p)
{
    unsigned width;
    if (!take(p, '*')) return 0;
    {
        double length;
        int id = known_constant(p);
        if (p->token == T_NUMBER) length = p->number;
        else if (id >= 0 && variables[id].kind != '$') length = variables[id].indexed_number;
        else {
            fail(p, "Invalid string length");
            return 0;
        }
        if (length < 1 || length > STRING_MAX || floor(length) != length) {
            fail(p, "Invalid string length");
            return 0;
        }
        width = (unsigned)length;
        scan(p);
    }
    return width;
}

static char declared_kind(Parser *p, unsigned *width)
{
    unsigned i;
    *width = 0;
    if (take_word(p, "INTEGER")) return '%';
    if (take_word(p, "LONG")) return '&';
    if (take_word(p, "SINGLE")) return '!';
    if (take_word(p, "DOUBLE")) return '#';
    if (take_word(p, "STRING")) {
        *width = fixed_string_width(p);
        return '$';
    }
    for (i = 0; i < record_count; ++i) {
        if (take_word(p, records[i].name)) return (char)(i + 1);
    }
    fail(p, "Expected: type");
    return 0;
}

static int member_name(Parser *p)
{
    if (unsuffixed_name(p) && p->length <= BASIC_NAME_MAX &&
        !_fmemchr(source + p->start, '.', p->length)) return 1;
    fail(p, "Expected: identifier");
    return 0;
}

static unsigned index_record(Parser *p, unsigned line)
{
    RecordType *type;
    RecordField *field;
    unsigned i;
    char unused;
    if (!member_name(p)) return line;
    if (record_count == RECORD_MAX) {
        fail(p, "Too many types");
        return line;
    }
    for (i = 0; i < record_count; ++i) {
        if (word(p, records[i].name)) {
            fail(p, "Duplicate definition");
            return line;
        }
    }
    type = &records[record_count];
    memset(type, 0, sizeof(*type));
    type->line = line;
    identifier(p, type->name, &unused);
    if (p->token) fail(p, "Expected: end of statement");
    while (!error_message && ++line < line_count) {
        unsigned start, length, width, size;
        char kind;
        initialize_parser(p, position(line, lines[line].body));
        if (!p->token || word(p, "REM")) continue;
        if (take_word(p, "END")) {
            expect_word(p, "TYPE", "Expected: TYPE");
            if (p->token) fail(p, "Expected: end of statement");
            if (!type->size) fail(p, "Expected: element");
            type->end = line;
            ++record_count;
            return line;
        }
        if (!member_name(p)) break;
        start = p->start;
        length = p->length;
        for (field = type->fields; field; field = field->next) {
            if (field->length == length && same_slice(source + start, source + field->start, length))
                fail(p, "Duplicate definition");
        }
        scan(p);
        if (!expect_word(p, "AS", "Expected: AS")) break;
        kind = declared_kind(p, &width);
        if (p->token) fail(p, "Expected: end of statement");
        size = member_size(kind, width);
        if (!size) fail(p, "Expected: fixed-length string");
        if (size > 32767U - type->size) fail(p, "Out of memory");
        if (error_message) break;
        field = malloc(sizeof(*field));
        if (!field) {
            fail(p, "Out of memory");
            break;
        }
        field->next = type->fields;
        field->start = start;
        field->length = length;
        field->offset = type->size;
        field->width = width;
        field->kind = kind;
        type->fields = field;
        type->size += size;
    }
    if (!error_message) fail(p, "Expected: END TYPE");
    /* Include the partial type in normal cleanup. */
    ++record_count;
    return line;
}

static void release_records(void)
{
    unsigned i;
    for (i = 0; i < record_count; ++i) {
        RecordField *field = records[i].fields;
        while (field) {
            RecordField *next = field->next;
            free(field);
            field = next;
        }
    }
    record_count = 0;
}

static void clear_array(Variable *v)
{
    unsigned i;
    if (v->kind == '$') {
        char **items = v->elements;
        for (i = 0; i < v->cells; ++i) {
            free(items[i]);
            items[i] = NULL;
        }
    } else if (v->elements) {
        memset(v->elements, 0, v->cells * array_element_size(v));
    }
}

static void release_array(Variable *v)
{
    if (v->kind == '$') clear_array(v);
    free(v->elements);
    free(v->bounds);
    v->elements = NULL;
    v->bounds = NULL;
    v->cells = v->dimensions = 0;
}

static void dimension_array(Parser *p, Variable *v, const Bounds *bounds, unsigned dimensions,
                            unsigned cells)
{
    Bounds *shape;
    void *elements;
    unsigned size = array_element_size(v);
    if (record_size(v->kind) && cells > 32767U / size) {
        fail(p, "Out of memory");
        return;
    }
    shape = malloc(dimensions * sizeof(*shape));
    elements = calloc(cells, size);
    if (!shape || !elements) {
        free(shape);
        free(elements);
        fail(p, "Out of memory");
        return;
    }
    memcpy(shape, bounds, dimensions * sizeof(*shape));
    release_array(v);
    v->bounds = shape;
    v->dimensions = dimensions;
    v->elements = elements;
    v->cells = cells;
}

static void dim_statement(Parser *p, int run, int resize, int persistent)
{
    int shared = take_word(p, "SHARED");
    Bounds bounds[DIMENSION_MAX];
    if (shared && (current_scope >= 0 || persistent)) {
        fail(p, "SHARED attribute must be in module-level code");
        return;
    }
    do {
        int id, typed, array, dynamic = resize || dynamic_arrays;
        int unsuffixed = unsuffixed_name(p);
        char name[BASIC_NAME_MAX + 1], kind;
        unsigned dimensions = 0, cells = 1, fixed_width = 0;
        if (!identifier(p, name, &kind)) return;
        array = take(p, '(');
        if (array) {
            do {
                long lower = option_base;
                Parser start = *p;
                long upper = integer_operand(p, numeric(p, run), run);
                double extent;
                if (dimensions == DIMENSION_MAX) {
                    fail(p, "Subscript out of range");
                    return;
                }
                if (take_word(p, "TO")) {
                    lower = upper;
                    upper = integer_operand(p, numeric(p, run), run);
                }
                if (checking) {
                    while (!dynamic && start.start < p->start && start.token) {
                        if (start.token == T_NAME && !precedence(&start) &&
                            !word(&start, "NOT") && !word(&start, "TO"))
                            dynamic = 1;
                        scan(&start);
                    }
                }
                bounds[dimensions].lower = lower;
                bounds[dimensions].upper = upper;
                ++dimensions;
                extent = (double)upper - (double)lower + 1;
                if (run && !error_message) {
                    if (extent < 1) fail(p, "Subscript out of range");
                    else if (extent > ARRAY_CELL_MAX / cells) fail(p, "Out of memory");
                    else cells *= (unsigned)extent;
                }
            } while (!error_message && take(p, ','));
            expect(p, ')', "Expected: )");
        } else if (resize) fail(p, "Expected: (");
        typed = take_word(p, "AS");
        if (typed) kind = declared_kind(p, &fixed_width);
        if (error_message) return;
        id = named_variable(p, name, kind, array, !resize || shared, unsuffixed, typed);
        if (id >= 0 && !error_message) {
            Variable *symbol = &variables[id];
            if (checking) {
                if (typed) symbol->fixed_width = fixed_width;
                if (shared) symbol->shared = 1;
                if (persistent) symbol->persistent = 1;
                if (resize && symbol->dynamic == ARRAY_STATIC) {
                    fail(p, "Duplicate definition");
                    return;
                }
                if (array && !(symbol->common && symbol->dynamic == ARRAY_DYNAMIC))
                    symbol->dynamic = dynamic ? ARRAY_DYNAMIC : ARRAY_STATIC;
            }
            if (run && array) {
                Variable *v = array_storage(id);
                if (resize && v->dynamic == ARRAY_STATIC)
                    fail(p, "Duplicate definition");
                else if (!resize && v->dynamic == ARRAY_DYNAMIC && v->elements)
                    fail(p, "Duplicate definition");
                else if (resize || !v->elements) {
                    dimension_array(p, v, bounds, dimensions, cells);
                    if (!error_message && resize) v->dynamic = ARRAY_DYNAMIC;
                }
            }
        }
    } while (!error_message && take(p, ','));
}

/* SHARED imports module storage into one procedure. COMMON declares that
 * same module storage and optionally makes it visible in every procedure. */
static void storage_statement(Parser *p, int common)
{
    int scope = current_scope;
    int global = common && take_word(p, "SHARED");
    if (common ? scope >= 0 : scope < 0) {
        fail(p, common ? "COMMON must be in module-level code" :
                        "SHARED must be in a procedure");
        return;
    }
    if (common && take(p, '/')) {
        char name[BASIC_NAME_MAX + 1], kind;
        if (!identifier(p, name, &kind) || !expect(p, '/', "Expected: /")) return;
    }
    do {
        char name[BASIC_NAME_MAX + 1], kind;
        int id, array, typed, unsuffixed = unsuffixed_name(p);
        unsigned width = 0, i;
        if (!identifier(p, name, &kind)) return;
        array = take(p, '(');
        if (array && !expect(p, ')', "Expected: )")) return;
        typed = take_word(p, "AS");
        if (typed) kind = declared_kind(p, &width);
        if (error_message || !checking) continue;
        if (!common)
            for (i = 0; i < variable_count; ++i) {
                const Variable *v = &variables[i];
                if (v->scope == scope && variable_matches(v, name, kind, array, unsuffixed)) {
                    fail(p, "Duplicate definition");
                    return;
                }
            }
        current_scope = -1;
        id = named_variable(p, name, kind, array, 1, unsuffixed, typed);
        current_scope = scope;
        if (id >= 0) {
            Variable *v = &variables[id];
            if (typed) v->fixed_width = width;
            if (common) {
                if (v->common) fail(p, "Duplicate definition");
                else {
                    common_ids[common_count++] = (unsigned)id;
                    v->common = common_count;
                }
                if (global) v->shared = 1;
                if (array && v->dynamic == ARRAY_IMPLICIT) v->dynamic = ARRAY_DYNAMIC;
            } else v->shared_scopes |= 1U << scope;
        }
    } while (!error_message && take(p, ','));
}

static void erase_statement(Parser *p, int run)
{
    do {
        char name[BASIC_NAME_MAX + 1], kind;
        int id, unsuffixed = unsuffixed_name(p);
        if (!identifier(p, name, &kind)) return;
        id = named_variable(p, name, kind, 1, 0, unsuffixed, 0);
        if (run && id >= 0 && !error_message) {
            Variable *v = array_storage(id);
            if (v->dynamic == ARRAY_DYNAMIC) release_array(v);
            else clear_array(v);
        }
    } while (!error_message && take(p, ','));
}

static void array_directive(Parser *p)
{
    unsigned at;
    unsigned end = lines[p->line].end;
    if (!checking) return;
    if (word(p, "REM")) at = p->pos;
    else if (!p->token && p->start < end && source[p->start] == '\'') at = p->start + 1;
    else return;
    while (at < end && (source[at] == ' ' || source[at] == '\t')) ++at;
    if (at == end || source[at++] != '$') return;
    end = at;
    while (end < lines[p->line].end && name_char(source[end])) ++end;
    if (same(source + at, end - at, "DYNAMIC")) dynamic_arrays = 1;
    else if (same(source + at, end - at, "STATIC")) dynamic_arrays = 0;
}

static void const_statement(Parser *p, int run)
{
    int id;
    Value v;
    do {
        id = variable_mode(p, 1);
        if (id >= 0 && variables[id].array) fail(p, "Expected: scalar variable");
        expect(p, '=', "Expected: =");
        expression(p, &v, 1, 0, run);
        if (id >= 0 && !error_message) {
            Variable *storage = variable_storage(id);
            storage->constant = 0;
            assign(p, id, &v, run);
            storage->constant = 1;
            if (checking) variables[id].shared = current_scope < 0;
        }
    } while (!error_message && take(p, ','));
}

static int label_equal(const char __far *a, unsigned an, const char __far *b, unsigned bn)
{
    if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
        while (an > 1 && *a == '0') { ++a; --an; }
        while (bn > 1 && *b == '0') { ++b; --bn; }
    }
    if (an != bn) return 0;
    while (an--) {
        if (toupper((unsigned char)*a++) != toupper((unsigned char)*b++)) return 0;
    }
    return 1;
}

static unsigned target(Parser *p)
{
    unsigned i;
    if (p->token == T_NUMBER || p->token == T_NAME) {
        for (i = 0; i < line_count; ++i) {
            if (lines[i].label && label_equal(source + p->start, p->length,
                                              lines[i].label, lines[i].label_length)) {
                scan(p);
                return i;
            }
        }
        fail(p, "Label not defined");
    } else fail(p, "Expected: label or line number");
    return line_count;
}

static Position next_line(unsigned line)
{
    ++line;
    return position(line, line < line_count ? lines[line].body : 0);
}

static Position after_statement(const Parser *p)
{
    if (p->token == ':') return position(p->line, p->pos);
    return next_line(p->line);
}

static int pause_at(Parser *p, Position at, const char *message)
{
    unsigned end = lines[at.line].end;
    if (!stack_room(p, 3100)) return 0;
    if (p->line == at.line && (p->token == ':' || word(p, "ELSE")))
        end = p->start;
    while (end > at.offset && isspace((unsigned char)source[end - 1])) --end;
    if (pause_execution(pause_context, at.line + 1, at.offset - lines[at.line].start + 1,
                        end - at.offset + 1, message)) return 1;
    aborted = stopped = 1;
    error_message = "Execution cancelled";
    return 0;
}

static int before_statement(Parser *p, Position at)
{
    Parser end = *p;
    unsigned before = at.offset;
    while (end.token && end.token != ':' && !word(&end, "ELSE"))
        scan(&end);
    while (before > lines[at.line].body && isspace((unsigned char)source[before - 1]))
        --before;
    if (before > lines[at.line].body && source[before - 1] == ':')
        at.offset = before - 1;
    return pause_at(&end, at, NULL);
}

static void go(Position p)
{
    next_position = p;
    jumped = 1;
}

static void go_line(unsigned line)
{
    if (line < line_count) go(position(line, lines[line].body));
}

static unsigned block(Parser *p, Position at, int kind, unsigned inline_depth)
{
    unsigned i, previous;
    int expected;
    if (inline_depth) {
        fail(p, "Block statement in single-line IF");
        return 0;
    }
    if (!checking) {
        for (i = 0; i < block_count; ++i) {
            if (blocks[i].at.line == at.line && blocks[i].at.offset == at.offset) return i;
        }
        fail(p, "Invalid block");
        return 0;
    }
    if (block_count == BLOCK_MAX) {
        fail(p, "Too many block statements");
        return 0;
    }
    i = block_count++;
    blocks[i].kind = kind;
    blocks[i].at = at;
    blocks[i].after = after_statement(p);
    blocks[i].peer = blocks[i].owner = blocks[i].finish = BLOCK_MAX;
    if (kind == B_FOR || kind == B_DO || kind == B_WHILE || kind == B_IF || kind == B_SELECT) {
        if (block_depth == STACK_MAX) fail(p, "Blocks nested too deeply");
        else block_stack[block_depth++] = i;
        return i;
    }
    if (!block_depth) {
        fail(p, "Block statement without matching opener");
        return i;
    }
    previous = block_stack[block_depth - 1];
    if (kind == B_CASE || kind == B_ENDSELECT) {
        unsigned owner;
        if (blocks[previous].kind != B_SELECT && blocks[previous].kind != B_CASE) {
            fail(p, "CASE without SELECT");
            return i;
        }
        owner = blocks[previous].kind == B_SELECT ? previous : blocks[previous].owner;
        blocks[i].owner = owner;
        blocks[previous].peer = i;
        if (kind == B_CASE) block_stack[block_depth - 1] = i;
        else {
            blocks[owner].finish = i;
            --block_depth;
        }
        return i;
    }
    expected = kind == B_NEXT ? B_FOR : kind == B_LOOP ? B_DO : kind == B_WEND ? B_WHILE : B_IF;
    if (blocks[previous].kind != expected &&
        !(kind == B_ENDIF && blocks[previous].kind == B_ELSE)) {
        fail(p, "Mismatched block statement");
        return i;
    }
    blocks[previous].peer = i;
    blocks[i].peer = previous;
    if (kind == B_ELSE) block_stack[block_depth - 1] = i;
    else --block_depth;
    return i;
}

static void write_bytes(Parser *p, FILE *stream, unsigned *column, const char *s, size_t n)
{
#if defined(__WATCOMC__) || defined(_MSDOS)
    if (immediate_active && stream == stdout && n) immediate_output = 1;
#endif
    if (fwrite(s, 1, n, stream) != n) fail(p, "Write error");
    if (n && s[n - 1] == '\n') *column = 0;
    else *column += n;
}

static void write_output(Parser *p, FILE *stream, unsigned *column, const char *s)
{
    write_bytes(p, stream, column, s, strlen(s));
}

static Channel *file_number(Parser *p, int run)
{
    long n;
    take(p, '#');
    n = integer_operand(p, numeric(p, run), run);
    if (!run || error_message) return &channels[0];
    if (n < 1 || n > FILE_MAX) {
        fail(p, "Bad file number");
        return &channels[0];
    }
    return &channels[n - 1];
}

static int file_open(const Channel *file)
{
    return file->stream || file->serial;
}

static int file_eof(Parser *p, Channel *file)
{
    int c;
    if (file->serial) return ckb_serial_count(file->serial) == 0;
    if (!file->stream || file->mode == FILE_OUTPUT || file->mode == FILE_APPEND ||
        ((file->mode == FILE_BINARY || file->mode == FILE_RANDOM) && !(file->access & ACCESS_READ))) {
        fail(p, "Bad file mode");
        return 1;
    }
    if (file->mode != FILE_INPUT && fseek(file->stream, 0, SEEK_CUR) != 0) {
        fail(p, "Read error");
        return 1;
    }
    c = fgetc(file->stream);
    if (c == EOF && ferror(file->stream)) fail(p, "Read error");
    if (c == EOF) return 1;
    ungetc(c, file->stream);
    return file->mode == FILE_INPUT && c == 26;
}

static int text_output(const Channel *file)
{
    if (file->serial) return (file->access & ACCESS_WRITE) != 0;
    return file->stream && (file->mode == FILE_OUTPUT || file->mode == FILE_APPEND);
}

typedef struct {
    Parser *parser;
    FILE *stream;
    CkbSerial *serial;
    unsigned *column;
} FormatOutput;

static void write_format(void *context, const char *bytes, unsigned length)
{
    FormatOutput *output = context;
    if (error_message) return;
    if (output->serial) {
        const char *error = ckb_serial_write(output->serial, bytes, length);
        if (error) fail(output->parser, error);
        if (length && (bytes[length - 1] == '\n' || bytes[length - 1] == '\r')) *output->column = 0;
        else *output->column += length;
    } else write_bytes(output->parser, output->stream, output->column, bytes, length);
}

static void format_item(CkbFormat *format, const Value *value, FormatOutput *output)
{
    CkbFormatResult result = ckb_format_value(format, value, write_format, output);
    if (result == CKB_FORMAT_TYPE) fail(output->parser, "Type mismatch");
    else if (result == CKB_FORMAT_INVALID) fail(output->parser, "Illegal function call");
}

static void print_text(FormatOutput *output, const char *text)
{
    write_format(output, text, strlen(text));
}

static void print_newline(FormatOutput *output)
{
    print_text(output, output->serial ? ckb_serial_newline(output->serial) : "\n");
}

static void print_using(Parser *p, int run, FormatOutput *output)
{
    Value format, item;
    CkbFormat state;
    int trailing = 0;
    expression(p, &format, 1, 0, run);
    if (!format.string) fail(p, "Type mismatch");
    expect(p, ';', "Expected: ;");
    ckb_format_init(&state, ckb_string_bytes(&format), format.length);
    if (run && !error_message && end_statement(p)) {
        memset(&item, 0, sizeof(item));
        item.string = 1;
        format_item(&state, &item, output);
    }
    while (!error_message && !end_statement(p)) {
        expression(p, &item, 1, 0, run);
        if (record_size(item.kind)) fail(p, "Type mismatch");
        if (run && !error_message) format_item(&state, &item, output);
        trailing = take(p, ';') || take(p, ',');
        if (!trailing && !end_statement(p)) fail(p, "Expected: PRINT separator");
    }
    if (run && !error_message) {
        ckb_format_finish(&state, write_format, output);
        if (!trailing && !error_message) print_newline(output);
    }
}

static void print_statement(Parser *p, int run, int printer)
{
    Value v;
    FormatOutput output;
    int trailing = 0;
    output.parser = p;
    output.stream = stdout;
    output.serial = NULL;
    output.column = &screen_column;
    if (printer && run) {
#if defined(__WATCOMC__) || defined(_MSDOS)
        /* QBasic checks the BIOS LPT1 base; route output through DOS PRN. */
        if (*(unsigned __far *)MK_FP(0x40, 0x08) == 0) {
            fail(p, "Device unavailable");
            return;
        }
#else
        fail(p, "Device unavailable");
        return;
#endif
        output.stream = fopen("PRN", "w");
        if (!output.stream) { fail(p, "Device unavailable"); return; }
        setvbuf(output.stream, NULL, _IONBF, 0);
        output.column = &printer_column;
    }
    if (!printer && take(p, '#')) {
        Channel *file = file_number(p, run);
        expect(p, ',', "Expected: ,");
        if (run && !file_open(file)) fail(p, "Bad file number");
        else if (run && !text_output(file)) fail(p, "Bad file mode");
        output.stream = file->stream;
        output.serial = file->serial;
        output.column = &file->column;
    }
    if (take_word(p, "USING")) {
        print_using(p, run, &output);
        if (printer && run) fclose(output.stream);
        return;
    }
    while (!error_message && !end_statement(p)) {
        if (take(p, ';')) trailing = 1;
        else if (take(p, ',')) {
            trailing = 1;
            if (run) {
                unsigned i, spaces = 14 - *output.column % 14;
                if (output.stream == stdout && *output.column + spaces >= 80) {
                    print_newline(&output);
                } else {
                    for (i = 0; i < spaces; ++i) print_text(&output, " ");
                }
            }
        } else {
            trailing = 0;
            expression(p, &v, 1, 0, run);
            if (record_size(v.kind)) fail(p, "Type mismatch");
            if (run && !error_message) {
                char number[48];
                if (v.string) write_format(&output, ckb_string_bytes(&v), v.length);
                else {
                    ckb_number_text(number, &v);
                    strcat(number, " ");
                    print_text(&output, number);
                }
            }
            if (!end_statement(p) && p->token != ';' && p->token != ',')
                fail(p, "Expected: PRINT separator");
        }
    }
    if (run && !error_message && !trailing) print_newline(&output);
    if (printer && run) fclose(output.stream);
}

static void write_statement(Parser *p, int run)
{
    Value value;
    FormatOutput output;
    output.parser = p;
    output.stream = stdout;
    output.serial = NULL;
    output.column = &screen_column;
    if (take(p, '#')) {
        Channel *file = file_number(p, run);
        expect(p, ',', "Expected: ,");
        if (run && !text_output(file)) fail(p, "Bad file mode");
        output.stream = file->stream;
        output.serial = file->serial;
        output.column = &file->column;
    }
    if (!end_statement(p)) do {
        expression(p, &value, 1, 0, run);
        if (record_size(value.kind)) fail(p, "Type mismatch");
        if (run && !error_message) {
            if (value.string) {
                print_text(&output, "\"");
                write_format(&output, ckb_string_bytes(&value), value.length);
                print_text(&output, "\"");
            } else {
                char number[48];
                ckb_number_text(number, &value);
                print_text(&output, number + (number[0] == ' '));
            }
        }
        if (error_message || !take(p, ',')) break;
        if (run) print_text(&output, ",");
    } while (!error_message);
    if (run && !error_message) print_newline(&output);
}

static long file_position(Parser *p, Channel *file, int which)
{
    long here, end;
    if (file->serial) {
        if (which == FILE_LOC) return ckb_serial_count(file->serial);
        if (which == FILE_LOF) return ckb_serial_transmit_capacity(file->serial);
        return 0;
    }
    if (!file->stream || (file->mode != FILE_BINARY && file->mode != FILE_RANDOM)) {
        fail(p, "Bad file mode");
        return 0;
    }
    here = ftell(file->stream);
    if (here < 0) { fail(p, "Read error"); return 0; }
    if (which != FILE_LOF && file->mode == FILE_RANDOM) here /= file->record_length;
    if (which == FILE_SEEK) {
        if (here >= 2147483647L) { fail(p, "Overflow"); return 0; }
        return here + 1;
    }
    if (which == FILE_LOC) return here;
    if (fseek(file->stream, 0, SEEK_END) != 0) { fail(p, "Read error"); return 0; }
    end = ftell(file->stream);
    if (fseek(file->stream, here, SEEK_SET) != 0 || end < 0) {
        fail(p, "Read error");
        return 0;
    }
    return end;
}

static void seek_file(Parser *p, Channel *file, long position)
{
    unsigned width = file->mode == FILE_RANDOM ? file->record_length : 1;
    if (!file->stream || (file->mode != FILE_BINARY && file->mode != FILE_RANDOM))
        fail(p, "Bad file mode");
    else if (position < 1 || position - 1 > 2147483647L / width)
        fail(p, "Bad record number");
    else if (fseek(file->stream, (position - 1) * width, SEEK_SET) != 0)
        fail(p, "Read error");
}

static FILE *open_file(const char *name, const char *mode, int sharing)
{
#if defined(__WATCOMC__) || defined(_MSDOS)
    static const int share_modes[] = {0, SH_DENYNO, SH_DENYRD, SH_DENYWR, SH_DENYRW};
    if (sharing != SHARE_DEFAULT) {
        _doserrno = 0;
        return _fsopen(name, mode, share_modes[sharing]);
    }
#else
    (void)sharing;
#endif
    return fopen(name, mode);
}

static int sharing_error(void)
{
#if defined(__WATCOMC__) || defined(_MSDOS)
    union REGS registers;
    if (_doserrno == 32 || _doserrno == 33) return 75;
    memset(&registers, 0, sizeof(registers));
    registers.w.ax = 0x5900;
    int86(0x21, &registers, &registers);
    if (registers.w.ax == 32 || registers.w.ax == 33) return 70;
#endif
    return 0;
}

static void file_io_error(Parser *p, const char *message)
{
    fail(p, sharing_error() ? "Permission denied" : message);
}

static void open_statement(Parser *p, int run)
{
    Value v;
    Channel *file;
    int mode = FILE_RANDOM, access = ACCESS_READ | ACCESS_WRITE;
    long width = 128;
    int sharing = SHARE_DEFAULT, share_error;
    expression(p, &v, 1, 0, run);
    if (!v.string) fail(p, "Type mismatch");
    if (take_word(p, "FOR")) {
        if (take_word(p, "INPUT")) mode = FILE_INPUT;
        else if (take_word(p, "OUTPUT")) mode = FILE_OUTPUT;
        else if (take_word(p, "APPEND")) mode = FILE_APPEND;
        else if (take_word(p, "BINARY")) mode = FILE_BINARY;
        else if (!take_word(p, "RANDOM")) fail(p, "Expected: file mode");
    }
    if ((mode == FILE_BINARY || mode == FILE_RANDOM) && take_word(p, "ACCESS")) {
        if (take_word(p, "READ")) {
            access = ACCESS_READ;
            if (take_word(p, "WRITE")) access |= ACCESS_WRITE;
        } else if (take_word(p, "WRITE")) access = ACCESS_WRITE;
        else fail(p, "Expected: READ or WRITE");
    }
    if (take_word(p, "SHARED")) sharing = SHARE_ALLOW;
    else if (take_word(p, "LOCK")) {
        if (take_word(p, "READ"))
            sharing = take_word(p, "WRITE") ? SHARE_DENY_BOTH : SHARE_DENY_READ;
        else if (take_word(p, "WRITE")) sharing = SHARE_DENY_WRITE;
        else fail(p, "Expected: READ or WRITE");
    }
    expect_word(p, "AS", "Expected: AS");
    file = file_number(p, run);
    if (mode == FILE_RANDOM && take_word(p, "LEN")) {
        expect(p, '=', "Expected: =");
        width = integer_operand(p, numeric(p, run), run);
    }
    if (run && !error_message) {
        if (file_open(file)) fail(p, "File already open");
        else if (memchr(ckb_string_bytes(&v), 0, v.length)) fail(p, "Bad file name");
        else if (ckb_serial_name(ckb_string_bytes(&v))) {
            const char *error;
            if (mode == FILE_BINARY) error = "Bad file mode";
            else if (mode == FILE_APPEND || access != (ACCESS_READ | ACCESS_WRITE))
                error = "Device unavailable";
            else error = ckb_serial_open(ckb_string_bytes(&v), &file->serial);
            if (error) fail(p, error);
            else {
                file->mode = mode;
                file->access = mode == FILE_INPUT ? ACCESS_READ :
                    mode == FILE_OUTPUT || mode == FILE_APPEND ? ACCESS_WRITE : access;
                file->column = 0;
            }
        }
        else if (mode == FILE_RANDOM && (width < 1 || width > 32767))
            fail(p, "Illegal function call");
        else {
            if (mode == FILE_RANDOM) {
                file->record = calloc(1, (unsigned)width);
                if (!file->record) {
                    fail(p, "Out of memory");
                    return;
                }
                file->record_length = (unsigned)width;
            }
            if (mode == FILE_BINARY || mode == FILE_RANDOM) {
                file->stream = open_file(ckb_string_bytes(&v), access & ACCESS_WRITE ? "r+b" : "rb", sharing);
                if (!file->stream && errno == ENOENT && (access & ACCESS_WRITE))
                    file->stream = open_file(ckb_string_bytes(&v), "w+b", sharing);
            } else file->stream = open_file(ckb_string_bytes(&v), mode == FILE_INPUT ? "rb" :
                                                mode == FILE_APPEND ? "a" : "w", sharing);
            if (!file->stream) {
                free(file->record);
                file->record = NULL;
                if (errno == EMFILE) fail(p, "Too many files");
                else if (sharing != SHARE_DEFAULT && (share_error = sharing_error()) != 0)
                    fail(p, share_error == 75 ? "Path/File access error" : "Permission denied");
                else fail(p, mode == FILE_INPUT || !(access & ACCESS_WRITE) ?
                             "File not found" : "Cannot open output file");
                return;
            }
            if (sharing != SHARE_DEFAULT) setvbuf(file->stream, NULL, _IONBF, 0);
            file->column = 0;
            file->mode = mode;
            file->access = access;
        }
    }
}

static void random_record_read(Parser *p, Channel *file)
{
    long here = ftell(file->stream), end;
    unsigned count = 0;
    if (here < 0 || fseek(file->stream, 0, SEEK_END) != 0) {
        file_io_error(p, "Read error");
        return;
    }
    end = ftell(file->stream);
    if (end < 0 || here > 2147483647L - file->record_length) {
        fail(p, end < 0 ? "Read error" : "Bad record number");
        return;
    }
    if (here < end) {
        unsigned available = end - here < file->record_length ?
                             (unsigned)(end - here) : file->record_length;
        if (fseek(file->stream, here, SEEK_SET) != 0) {
            file_io_error(p, "Read error");
            return;
        }
        count = (unsigned)fread(file->record, 1, available, file->stream);
        if (ferror(file->stream)) {
            file_io_error(p, "Read error");
            return;
        }
    }
    memset(file->record + count, 0, file->record_length - count);
    /* RANDOM consumes one logical record even when its bytes are absent. */
    if (fseek(file->stream, here + file->record_length, SEEK_SET) != 0)
        file_io_error(p, "Read error");
}

static void file_transfer(Parser *p, int run, int writing)
{
    Channel *file = file_number(p, run);
    Reference ref;
    Value value;
    long position = 0;
    int positioned = 0, operand = 0;
    unsigned size;
    char *bytes;
    if (take(p, ',')) {
        if (p->token != ',' && !end_statement(p)) {
            positioned = 1;
            position = integer_operand(p, numeric(p, run), run);
        }
        if (take(p, ',')) {
            operand = 1;
            ref = lvalue(p, run);
        }
    }
    if (!run || error_message) return;
    if (!file->stream || (file->mode != FILE_BINARY && file->mode != FILE_RANDOM) ||
        !(file->access & (writing ? ACCESS_WRITE : ACCESS_READ))) {
        fail(p, "Bad file mode"); return;
    }
    if (!operand && file->mode == FILE_BINARY) {
        fail(p, "Expected: ,");
        return;
    }
    if (operand && file->mode == FILE_RANDOM) {
        fail(p, "Bad file mode");
        return;
    }
    clearerr(file->stream);
    if (positioned) seek_file(p, file, position);
    else if (fseek(file->stream, 0, SEEK_CUR) != 0) file_io_error(p, "Read error");
    if (error_message) return;
    if (file->mode == FILE_RANDOM) {
        if (writing) {
            if (fwrite(file->record, 1, file->record_length, file->stream) != file->record_length)
                file_io_error(p, "Write error");
        } else random_record_read(p, file);
        return;
    }
    if (!ref.target) return;
    fetch_reference(p, ref, 1, &value);
    if (error_message) return;
    size = value.string ? value.length : member_size(ref.kind, ref.width);
    if (record_size(ref.kind)) bytes = (char *)value.record;
    else if (value.string) bytes = ckb_string_buffer_at(&value);
    else { bytes = value.text; if (writing) packed_number(bytes, ref.kind, &value.number, 1); }
    if (!writing) {
        size_t count = fread(bytes, 1, size, file->stream);
        if (ferror(file->stream)) { file_io_error(p, "Read error"); return; }
        memset(bytes + count, 0, size - count);
        if (!record_size(ref.kind)) {
            if (!value.string) packed_number(bytes, ref.kind, &value.number, 0);
            assign_reference(p, ref, &value, 1);
        }
    } else if (fwrite(bytes, 1, size, file->stream) != size) file_io_error(p, "Write error");
}

static void file_lock_statement(Parser *p, int run, int unlocking)
{
    Channel *file = file_number(p, run);
    long first = 1, last = 0;
    unsigned long start, count, width;
    int range = take(p, ',');
    if (range) {
        first = last = integer_operand(p, numeric(p, run), run);
        if (take_word(p, "TO")) last = integer_operand(p, numeric(p, run), run);
    }
    if (!run || error_message) return;
    if (!file->stream || (range && file->mode != FILE_BINARY && file->mode != FILE_RANDOM)) {
        fail(p, "Bad file mode");
        return;
    }
    width = file->mode == FILE_RANDOM ? file->record_length : 1;
    if (first < 1 || (range && (last < first || (unsigned long)last > 0xffffffffUL / width))) {
        fail(p, "Bad record number");
        return;
    }
    start = (unsigned long)(first - 1) * width;
    count = range ? (unsigned long)(last - first + 1) * width : 0xffffffffUL;
#if defined(__WATCOMC__) || defined(_MSDOS)
    if (fflush(file->stream) != 0) {
        file_io_error(p, "Write error");
        return;
    }
    if ((unlocking ? unlock(fileno(file->stream), start, count) :
                     lock(fileno(file->stream), start, count)) != 0)
        fail(p, "Permission denied");
#else
    (void)start;
    (void)count;
    (void)unlocking;
    fail(p, "Permission denied");
#endif
}

static void field_statement(Parser *p, int run)
{
    Channel *file = file_number(p, run);
    unsigned offset = 0;
    if (run && !error_message && (!file->stream || file->mode != FILE_RANDOM))
        fail(p, "Bad file mode");
    do {
        long width;
        Reference ref;
        FieldBinding *binding;
        expect(p, ',', "Expected: ,");
        width = integer_operand(p, numeric(p, run), run);
        expect_word(p, "AS", "Expected: AS");
        ref = lvalue(p, run);
        if (error_message || !ref.target) return;
        if (ref.kind != '$' || ref.width || ref.target->array || ref.target->constant) {
            fail(p, "Type mismatch");
            return;
        }
        if (!run) continue;
        if (width < 0 || width > TEXT_INPUT_MAX || width > file->record_length - offset) {
            fail(p, "FIELD overflow");
            return;
        }
        binding = malloc(sizeof(*binding));
        if (!binding) {
            fail(p, "Out of memory");
            return;
        }
        unbind_field(ref.target);
        free(ref.target->text);
        ref.target->text = NULL;
        binding->variable = ref.target;
        binding->common = 0;
        binding->width = (unsigned)width;
        binding->offset = offset;
        binding->next = file->fields;
        file->fields = binding;
        offset += (unsigned)width;
    } while (p->token == ',');
}

static void release_file_record(Channel *file)
{
    while (file->fields) {
        FieldBinding *binding = file->fields;
        file->fields = binding->next;
        free(binding);
    }
    free(file->record);
    file->record = NULL;
}

static const char *release_channel(Channel *file)
{
    const char *error = NULL;
    if (file->serial) error = ckb_serial_close(file->serial);
    if (file->stream && fclose(file->stream) != 0) error = "Write error";
    file->serial = NULL;
    file->stream = NULL;
    release_file_record(file);
    return error;
}

static void close_file(Parser *p, Channel *file)
{
    const char *error = release_channel(file);
    if (error) fail(p, error);
}

static void rename_statement(Parser *p, int run)
{
    Value from, to;
    expression(p, &from, 1, 0, run);
    expect_word(p, "AS", "Expected: AS");
    expression(p, &to, 1, 0, run);
    if (!from.string || !to.string) fail(p, "Type mismatch");
    if (run && !error_message) {
        if (memchr(ckb_string_bytes(&from), 0, from.length) || memchr(ckb_string_bytes(&to), 0, to.length))
            fail(p, "Bad file name");
        else if (rename(ckb_string_bytes(&from), ckb_string_bytes(&to)) != 0) fail(p, "File not found");
    }
}

enum { CREATE_DIRECTORY, CHANGE_DIRECTORY, REMOVE_DIRECTORY, DELETE_FILE };

static void path_statement(Parser *p, int run, int operation)
{
    Value path;
    const char *name;
    int result;
    expression(p, &path, 1, 0, run);
    if (!path.string) fail(p, "Type mismatch");
    if (!run || error_message) return;
    name = ckb_string_bytes(&path);
    if (!path.length || memchr(name, 0, path.length)) {
        fail(p, "Bad file name");
        return;
    }
    switch (operation) {
    case CREATE_DIRECTORY:
#if defined(__WATCOMC__) || defined(_MSDOS)
        result = mkdir(name);
#else
        result = mkdir(name, 0777);
#endif
        break;
    case CHANGE_DIRECTORY: result = chdir(name); break;
    case REMOVE_DIRECTORY: result = rmdir(name); break;
    default: result = unlink(name); break;
    }
    if (result == 0) return;
    if (errno == ENOENT) fail(p, operation == DELETE_FILE ? "File not found" : "Path not found");
    else fail(p, "Path/File access error");
}

static void environment_statement(Parser *p, int run)
{
    Value assignment;
    const char *text;
    int result;
    expression(p, &assignment, 1, 0, run);
    if (!assignment.string) fail(p, "Type mismatch");
    if (!run || error_message) return;
    text = ckb_string_bytes(&assignment);
    if (memchr(text, 0, assignment.length)) {
        fail(p, "Illegal function call");
        return;
    }
    result = ckenv_set(text);
    if (result) fail(p, result == 7 ? "Out of memory" : "Illegal function call");
}

static void shell_statement(Parser *p, int run)
{
    Value command;
    const char *text;
    memset(&command, 0, sizeof(command));
    command.string = 1;
    if (!end_statement(p)) expression(p, &command, 1, 0, run);
    if (!command.string) fail(p, "Type mismatch");
    if (!run || error_message) return;
    text = ckb_string_bytes(&command);
    if (command.length > 122 || memchr(text, 0, command.length) ||
        memchr(text, '\r', command.length) || memchr(text, '\n', command.length)) {
        fail(p, "Illegal function call");
        return;
    }
#if defined(__WATCOMC__) || defined(_MSDOS)
    {
        char shell[CKB_INLINE_STRING + 1];
        const char *arguments[4];
        char **environment;
        int status;
        if (ckenv_get("COMSPEC", shell, sizeof(shell)) < 0) {
            fail(p, "Illegal function call");
            return;
        }
        arguments[0] = *shell ? shell : "COMMAND.COM";
        arguments[1] = command.length ? "/C" : NULL;
        arguments[2] = command.length ? text : NULL;
        arguments[3] = NULL;
        if (fflush(NULL) == EOF) {
            fail(p, "Write error");
            return;
        }
        environment = ckenv_child_env();
        if (!environment) {
            fail(p, "Out of memory");
            return;
        }
        status = spawnve(P_WAIT, arguments[0], arguments, (const char * const *)environment);
        ckenv_free_child_env(environment);
        if (status == -1) fail(p, "Illegal function call");
    }
#else
    fail(p, "Illegal function call");
#endif
}

static void line_input_statement(Parser *p, int run)
{
    Channel *file = file_number(p, run);
    Value v;
    unsigned n = 0;
    int c;
    Reference ref;
    expect(p, ',', "Expected: ,");
    ref = lvalue(p, run);
    if (ref.target && ref.kind != '$') fail(p, "Type mismatch");
    if (!run || error_message) return;
    if (!file->stream || file->mode != FILE_INPUT) {
        fail(p, "Bad file mode");
        return;
    }
    if (file_eof(p, file)) {
        fail(p, "Input past end of file");
        return;
    }
    memset(&v, 0, sizeof(v));
    v.string = 1;
    while ((c = fgetc(file->stream)) != EOF && c != 26 && c != '\n' && c != '\r') {
        if (n == TEXT_INPUT_MAX) {
            fail(p, "String too long");
            return;
        }
        v.text[n++] = (char)c;
    }
    if (c == '\r') {
        c = fgetc(file->stream);
        if (c != '\n' && c != EOF) ungetc(c, file->stream);
    } else if (c == 26) ungetc(c, file->stream);
    if (ferror(file->stream)) fail(p, "Read error");
    v.length = n;
    if (!error_message) assign_reference(p, ref, &v, 1);
}

static int sequential_char(FILE *stream)
{
    int c = fgetc(stream);
    if (c == 26) {
        ungetc(c, stream);
        return EOF;
    }
    return c;
}

static void input_file_field(Parser *p, FILE *stream, Reference ref)
{
    Value value;
    int c, quoted;
    unsigned length = 0;
    memset(&value, 0, sizeof(value));
    value.string = ref.kind == '$';
    do {
        c = sequential_char(stream);
    } while (c != EOF && isspace(c));
    if (c == EOF) {
        fail(p, ferror(stream) ? "Read error" : "Input past end of file");
        return;
    }
    quoted = value.string && c == '"';
    if (quoted) c = sequential_char(stream);
    while (c != EOF &&
           (quoted ? c != '"' : c != ',' && c != '\r' && c != '\n' &&
                                  (value.string || !isspace(c)))) {
        if (length == TEXT_INPUT_MAX) {
            fail(p, "String too long");
            return;
        }
        value.text[length++] = (char)c;
        c = sequential_char(stream);
    }
    if (quoted && c == '"') c = sequential_char(stream);
    if (c == '\r') {
        c = sequential_char(stream);
        if (c != EOF && c != '\n') ungetc(c, stream);
    } else if (c != EOF && c != ',' && c != '\n' && (quoted || !isspace(c))) {
        ungetc(c, stream);
    }
    if (ferror(stream)) { fail(p, "Read error"); return; }
    value.text[length] = 0;
    value.length = length;
    if (!value.string) value.number = ckb_numeric_prefix(value.text);
    assign_reference(p, ref, &value, 1);
}

static void input_file_statement(Parser *p, int run)
{
    Channel *file = file_number(p, run);
    expect(p, ',', "Expected: ,");
    if (run && !error_message && (!file->stream || file->mode != FILE_INPUT))
        fail(p, "Input past end of file");
    while (!error_message) {
        Reference ref = lvalue(p, run);
        if (record_size(ref.kind)) fail(p, "Type mismatch");
        if (run && ref.target && !error_message) input_file_field(p, file->stream, ref);
        if (error_message || !take(p, ',')) break;
    }
}

/* DATA is literal, including unquoted text. Keep its source positions so
 * RESTORE does not depend on whether execution passed through a DATA line. */
static int end_data(const Parser *p)
{
    return !p->token || p->token == ':';
}

static int data_field(Parser *p, Value *v)
{
    unsigned start, end;
    Parser item;
    int negative = 0;
    if (end_data(p)) return 0;
    start = p->start;
    if (p->token == T_STRING) {
        string_value(p, v);
        if (!end_data(p) && p->token != ',') fail(p, "Expected: DATA separator");
        return 1;
    }
    item = *p;
    if (p->token == '+' || p->token == '-') {
        negative = p->token == '-';
        scan(&item);
    }
    if (item.token == T_NUMBER) {
        scan(&item);
        if (item.token == ',' || end_data(&item)) {
            initialize_parser(&item, position(p->line, start));
            if (item.token == '+' || item.token == '-') scan(&item);
            v->number = negative ? -item.number : item.number;
            v->string = 0;
            scan(&item);
            *p = item;
            return 1;
        }
    }
    end = start;
    while (end < lines[p->line].end && !strchr(",:\'", source[end])) ++end;
    p->pos = end;
    scan(p);
    while (end > start && (source[end - 1] == ' ' || source[end - 1] == '\t')) --end;
    if (end - start > TEXT_INPUT_MAX) fail(p, "DATA item too long");
    else {
        v->string = 1;
        v->length = end - start;
        _fmemcpy(v->text, source + start, v->length);
        v->text[v->length] = 0;
    }
    return 1;
}

static void data_statement(Parser *p)
{
    Value datum;
    if (checking && !end_data(p)) {
        if (data_count == BLOCK_MAX) fail(p, "Too many DATA statements");
        else data_statements[data_count++] = position(p->line, p->start - (p->token == T_STRING));
    }
    while (!error_message && data_field(p, &datum) && take(p, ',')) {}
}

static void read_value(Parser *p, Reference ref)
{
    Parser field;
    Value datum;
    memset(&datum, 0, sizeof(datum));
    while (data_index < data_count) {
        initialize_parser(&field, position(data_statements[data_index].line,
                                            data_offset ? data_offset : data_statements[data_index].offset));
        if (data_field(&field, &datum)) {
            if (take(&field, ',') && !end_data(&field)) data_offset = field.start - (field.token == T_STRING);
            else {
                ++data_index;
                data_offset = 0;
            }
            assign_reference(p, ref, &datum, 1);
            return;
        }
        ++data_index;
        data_offset = 0;
    }
    fail(p, "Out of DATA");
}

#if defined(__WATCOMC__) || defined(_MSDOS)
static void input_cursor(unsigned start)
{
    union REGS regs = {0};
    regs.x.ax = 0x0100;
    regs.x.cx = (start << 8) | 7;
    int86(0x10, &regs, &regs);
}

static volatile unsigned char input_break_requested;
typedef void (__interrupt __far *BiosInterrupt)(void);
extern void __interrupt __far input_scan(void);
extern void __far input_scan_init(BiosInterrupt);
extern int __far input_scan_pending(void);
#ifdef __WATCOMC__
#pragma aux input_scan "CKB_INPUT_SCAN"
#pragma aux input_scan_init "CKB_INPUT_SCAN_INIT" parm [dx ax] modify exact []
#pragma aux input_scan_pending "CKB_INPUT_SCAN_PENDING" value [ax] modify exact [ax]
#endif

static void __interrupt __far input_break_interrupt(void)
{
    input_break_requested = 1;
}

/* INT 1Bh is the BIOS Break callback. Read keys without blocking so the
 * debugger can pause even on BIOSes that discard the dummy break key. */
#ifdef __WATCOMC__
static unsigned input_key(void);
#pragma aux input_key = \
    "mov ah, 01h" "int 16h" "jz empty" \
    "xor ah, ah" "int 16h" "jmp short done" \
    "empty: mov ax, 0ffffh" \
    "done:" value [ax] modify exact [ax]

static void input_idle(void);
#pragma aux input_idle = "sti" "hlt" modify exact []
#else
extern unsigned __far input_key(void);
extern void __far input_idle(void);
#endif

static int editor_input(Parser *p, char *input)
{
    volatile unsigned char __far *break_flag = MK_FP(0x40, 0x71);
    BiosInterrupt previous_break = _dos_getvect(0x1b);
    BiosInterrupt previous_scan = _dos_getvect(0x15);
    unsigned length = 0;
    int interrupted = 0;
    *break_flag &= 0x7f;
    input_break_requested = 0;
    input_scan_init(previous_scan);
    _dos_setvect(0x15, input_scan);
    _dos_setvect(0x1b, input_break_interrupt);
    input_cursor(7);
    while (!error_message) {
        unsigned key = input_key();
        unsigned char ch;
        if (input_break_requested || input_scan_pending() || (*break_flag & 0x80)) {
            *break_flag &= 0x7f;
            interrupted = 1;
            break;
        }
        if (key == 0xffff) {
            input_idle();
            continue;
        }
        ch = (unsigned char)key;
        if (ch == '\r') {
            write_output(p, stdout, &screen_column, "\n");
            break;
        }
        if (ch == '\b') {
            if (length) {
                write_output(p, stdout, &screen_column, "\b \b");
                --length;
            }
        } else if (ch >= 32 && ch != 127 && length < TEXT_INPUT_MAX) {
            input[length++] = ch;
            write_bytes(p, stdout, &screen_column, (char *)&ch, 1);
        }
        if (fflush(stdout) == EOF) fail(p, "Write error");
    }
    input[length] = 0;
    input_cursor(6);
    _dos_setvect(0x1b, previous_break);
    _dos_setvect(0x15, previous_scan);
    return !interrupted;
}

static unsigned input_column(void)
{
    union REGS regs = {0};
    regs.x.ax = 0x0300;
    int86(0x10, &regs, &regs);
    return regs.h.dl;
}
#endif

static void input_statement(Parser *p, int run, Position at)
{
    Value prompt, v;
    int question = 1;
    Reference ref;
    char input[TEXT_INPUT_MAX + 3], *end, *first;
    prompt.length = 0;
    prompt.string = 1;
    prompt.text[0] = 0;
    if (p->token == T_STRING) {
        string_value(p, &prompt);
        if (take(p, ',')) question = 0;
        else expect(p, ';', "Expected: ; or ,");
    }
    ref = lvalue(p, run);
    if (record_size(ref.kind)) fail(p, "Type mismatch");
    if (!run || error_message) return;
    for (;;) {
        write_output(p, stdout, &screen_column, ckb_string_bytes(&prompt));
        if (question) write_output(p, stdout, &screen_column, "? ");
        if (fflush(stdout) == EOF) fail(p, "Write error");
        if (error_message) return;
#if defined(__WATCOMC__) || defined(_MSDOS)
        if (pause_execution && isatty(0) && isatty(1)) {
            if (editor_input(p, input)) break;
            if (!pause_at(p, at, NULL)) return;
            if (input_column()) write_output(p, stdout, &screen_column, "\n");
            continue;
        }
#else
        (void)at;
#endif
        if (fgets(input, sizeof(input), stdin)) break;
        fail(p, "Input past end of file");
        return;
    }
    screen_column = 0;
    input[strcspn(input, "\r\n")] = 0;
    memset(&v, 0, sizeof(v));
    v.string = ref.kind == '$';
    if (v.string) {
        first = input;
        end = input + strlen(input);
        if (end > first + 1 && *first == '"' && end[-1] == '"') {
            *--end = 0;
            ++first;
        }
        if (end - first > TEXT_INPUT_MAX) fail(p, "Input too long");
        else {
            v.length = (unsigned)(end - first);
            memcpy(v.text, first, v.length + 1);
        }
    } else {
        v.number = strtod(input, &end);
        if (end == input) fail(p, "Expected: number");
        while (*end == ' ' || *end == '\t') ++end;
        if (*end) fail(p, "Expected: number");
        if (!finite_number(v.number)) fail(p, "Overflow");
    }
    if (!error_message) assign_reference(p, ref, &v, 1);
}

static int loop_passes(Parser *p, int run, int *kind)
{
    double condition;
    *kind = C_ALWAYS;
    if (take_word(p, "WHILE")) *kind = C_WHILE;
    else if (take_word(p, "UNTIL")) *kind = C_UNTIL;
    if (*kind == C_ALWAYS) return 1;
    condition = numeric(p, run);
    return *kind == C_WHILE ? condition != 0 : condition == 0;
}

static void walk_at(Position at, int run);

static int procedure_id(const char *name)
{
    unsigned i;
    for (i = 0; i < procedure_count; ++i)
        if (!strcmp(name, procedures[i].name)) return (int)i;
    return -1;
}

static int lookup_procedure(const Parser *p)
{
    Parser name_parser = *p;
    char name[BASIC_NAME_MAX + 1], kind;
    if (p->token != T_NAME || p->length > BASIC_NAME_MAX) return -1;
    identifier(&name_parser, name, &kind);
    return procedure_id(name);
}

static int whole_array_argument(Parser *p, int run)
{
    char name[BASIC_NAME_MAX + 1], kind;
    int id, unsuffixed = unsuffixed_name(p);
    if (!identifier(p, name, &kind)) return -1;
    if (!expect(p, '(', "Expected: array argument") ||
        !expect(p, ')', "Expected: )")) return -1;
    id = named_variable(p, name, kind, 1, 0, unsuffixed, 0);
    if (run && id >= 0 && !error_message) {
        Variable *v = array_storage(id);
        if (!v->elements && v->dynamic != ARRAY_DYNAMIC) implicit_array(p, v, 1);
    }
    return id;
}

static void procedure_arguments(Parser *p, const Procedure *proc, unsigned depth, int run,
                                int parenthesized, int function, Reference *references,
                                Value **copies)
{
    Value arg;
    unsigned i = 0;
    int arglist = parenthesized && take(p, '(');
    if ((arglist && p->token != ')') || (!arglist && !end_statement(p) && !function)) do {
        const Variable *param;
        if (i == proc->count) {
            fail(p, "Too many arguments");
            break;
        }
        param = &variables[proc->params[i]];
        if (param->array) {
            int id = whole_array_argument(p, run);
            if (id >= 0 && variables[id].kind != param->kind)
                fail(p, "Parameter type mismatch");
            if (id >= 0 && !error_message)
                references[i] = reference_at(id, 0);
        } else {
            expression_reference(p, &arg, 1, depth + 1, run, &references[i]);
            if (!error_message &&
                (arg.string != (param->kind == '$') ||
                 ((record_size(arg.kind) || record_size(param->kind)) &&
                  (arg.kind != param->kind || !references[i].target)) ||
                 (references[i].target && !arg.string &&
                  (arg.kind ? arg.kind : '!') != param->kind)))
                fail(p, "Parameter type mismatch");
            if (run && !error_message &&
                (!references[i].target ||
                 (arg.string && references[i].width && !param->fixed_width))) {
                copies[i] = malloc(sizeof(arg));
                if (!copies[i]) fail(p, "Out of memory");
                else *copies[i] = arg;
            }
        }
        ++i;
    } while (!error_message && take(p, ','));
    if (arglist) expect(p, ')', "Expected: )");
    if (i != proc->count) fail(p, "Wrong number of arguments");
}

static void call_procedure(Parser *p, int id, unsigned depth, int run, Value *result,
                           int parenthesized)
{
    Procedure *proc = &procedures[id];
    Value *copies[PARAM_MAX];
    Reference references[PARAM_MAX];
    Activation frame, *caller = activation;
    unsigned i;
    int old_scope = current_scope, old_jumped = jumped, old_returning = returning;
    Position old_next = next_position;
    if (result) {
        memset(result, 0, sizeof(*result));
        result->string = proc->result_kind == '$';
        result->kind = proc->result_kind;
    }
    if (proc->line == LINE_MAX) {
        fail(p, proc->function ? "Function not defined" : "Subprogram not defined");
        return;
    }
    if (procedure_depth == STACK_MAX) {
        fail(p, "Out of stack space");
        return;
    }
    if (!stack_room(p, 4096)) return;
    memset(copies, 0, sizeof(copies));
    procedure_arguments(p, proc, depth, run, parenthesized, result != NULL, references, copies);
    if (!run) return;
    frame.locals = NULL;
    frame.for_base = for_depth;
    frame.call_base = call_depth;
    if (!error_message && proc->locals) {
        frame.locals = malloc(proc->locals * sizeof(*frame.locals));
        if (!frame.locals) fail(p, "Out of memory");
    }
    if (!error_message) {
        for (i = 0; i < variable_count; ++i) {
            const Variable *v = &variables[i];
            if (v->scope == id && v->frame_slot)
                frame.locals[v->frame_slot - 1] = *v;
        }
        activation = &frame;
        current_scope = id;
        for (i = 0; i < proc->count && !error_message; ++i) {
            int param = proc->params[i];
            if (copies[i]) {
                assign(p, param, copies[i], 1);
                /* A by-value temporary is no longer needed once assigned.
                 * Keep fixed-string actuals for copy-back on return. */
                if (!references[i].target) {
                    free(copies[i]);
                    copies[i] = NULL;
                }
            } else variable_storage(param)->reference = references[i];
        }
    }
    /* The next statement needs stack space before reaching expression checks. */
    if (!error_message && stack_room(p, 4096)) {
        ++procedure_depth;
        walk_at(next_line(proc->line), 1);
        --procedure_depth;
        if (result && !error_message) fetch(p, proc->result, 0, 1, result);
    }
    /* Fixed actuals passed to variable-length formals use independent
     * descriptors; QBasic copies them back in reverse argument order. */
    if (frame.locals && !error_message) {
        for (i = proc->count; i > 0; --i) {
            unsigned n = i - 1;
            if (copies[n] && references[n].target && references[n].width) {
                fetch(p, proc->params[n], 0, 1, copies[n]);
                if (!error_message) assign_reference(p, references[n], copies[n], 1);
            }
        }
    }
    for (i = 0; i < proc->count; ++i) free(copies[i]);
    if (frame.locals) {
        for (i = 0; i < proc->locals; ++i) {
            unbind_field(&frame.locals[i]);
            release_array(&frame.locals[i]);
            free(frame.locals[i].text);
        }
        free(frame.locals);
    }
    activation = caller;
    current_scope = old_scope;
    returning = old_returning;
    jumped = old_jumped;
    next_position = old_next;
    for_depth = frame.for_base;
    call_depth = frame.call_base;
}

enum { CF_CINT = -1, CF_CLNG = -2, CF_CSNG = -3, CF_CDBL = -4,
       CF_ABS = -5, CF_SGN = -6, CF_INT = -7, CF_FIX = -8,
       CF_SQR = -9, CF_SIN = -10, CF_COS = -11, CF_TAN = -12,
       CF_ATN = -13, CF_EXP = -14, CF_LOG = -15,
       CF_MKS = -16, CF_MKD = -17, CF_MKSMBF = -18, CF_MKDMBF = -19,
       CF_CVS = -20, CF_CVD = -21, CF_CVSMBF = -22, CF_CVDMBF = -23,
       CF_MKI = -24, CF_MKL = -25, CF_CVI = -26, CF_CVL = -27,
       CF_HEX = -28, CF_OCT = -29 };

/* Numeric intrinsics share the argument parser with the string functions.
 * Integral functions retain their operand type; transcendental functions
 * return DOUBLE only for a DOUBLE operand, and SINGLE otherwise. */
static void math_builtin(Parser *p, Value *v, const Value *arg, int id, int run)
{
    double n = arg->number;
    char kind = arg->kind ? arg->kind : '!';
    if (id == CF_SGN) kind = '%';
    if (id <= CF_SQR && kind != '#') kind = '!';
    *v = *arg;
    v->kind = kind;
    if (!run) return;
    switch (id) {
    case CF_ABS: n = fabs(n); break;
    case CF_SGN: n = (n > 0) - (n < 0); break;
    case CF_INT: n = floor(n); break;
    case CF_FIX: n = n < 0 ? -floor(-n) : floor(n); break;
    case CF_SQR:
        if (n < 0) { fail(p, "Illegal function call"); return; }
        n = sqrt(n); break;
    case CF_SIN: n = sin(n); break;
    case CF_COS: n = cos(n); break;
    case CF_TAN: n = tan(n); break;
    case CF_ATN: n = atan(n); break;
    case CF_EXP:
        /* Avoid invoking the C library beyond its representable range. */
        if (n > 709.782712893384) { fail(p, "Overflow"); return; }
        n = n < -745.0 ? 0 : exp(n); break;
    case CF_LOG:
        if (n <= 0) { fail(p, "Illegal function call"); return; }
        n = log(n); break;
    }
    v->number = n;
    convert_number(p, v, kind, run);
}

static void encoding_builtin(Parser *p, Value *v, Value *arg, int id, int run)
{
    int integral = id <= CF_MKI;
    int encode = id >= CF_MKDMBF || id == CF_MKI || id == CF_MKL;
    char kind = integral ? (id == CF_MKL || id == CF_CVL ? '&' : '%') :
                (id == CF_MKD || id == CF_MKDMBF || id == CF_CVD || id == CF_CVDMBF ? '#' : '!');
    unsigned width = member_size(kind, 0);
    int mbf = mbf_mode || id == CF_MKSMBF || id == CF_MKDMBF ||
                         id == CF_CVSMBF || id == CF_CVDMBF;
    CkbNumResult result;
    if (record_size(arg->kind) || arg->string == encode) {
        fail(p, "Type mismatch");
        return;
    }
    v->kind = encode ? '$' : kind;
    v->string = encode;
    v->length = encode ? width : 0;
    if (!run) return;
    if (integral) {
        if (encode) {
            v->number = arg->number;
            convert_number(p, v, kind, 1);
            if (!error_message) packed_number(v->text, kind, &v->number, 1);
            v->text[width] = 0;
            v->kind = '$';
        } else if (arg->length < width) {
            fail(p, "Illegal function call");
        } else {
            packed_number((char *)ckb_string_bytes(arg), kind, &v->number, 0);
        }
        return;
    }
    if (encode) {
        result = ckb_num_encode(v->text, arg->number, kind == '#', mbf);
        v->text[v->length] = 0;
    } else {
        result = ckb_num_decode(&v->number, ckb_string_bytes(arg), arg->length, kind == '#', mbf);
    }
    if (result == CKB_NUM_INVALID) fail(p, "Illegal function call");
    if (result == CKB_NUM_OVERFLOW) fail(p, "Overflow");
}

static void radix_string_builtin(Parser *p, Value *v, const Value *arg, int id, int run)
{
    static const char digits[] = "0123456789ABCDEF";
    char reversed[11];
    unsigned base = id == CF_HEX ? 16 : 8;
    unsigned long bits;
    unsigned n = 0, i;
    if (arg->string || record_size(arg->kind)) {
        fail(p, "Type mismatch");
        return;
    }
    v->string = 1;
    v->kind = '$';
    if (!run) return;
    bits = (unsigned long)integer_operand(p, arg->number, 1);
    if (error_message) return;
    bits &= arg->kind == '%' ? 0xffffUL : 0xffffffffUL;
    do {
        reversed[n++] = digits[bits % base];
        bits /= base;
    } while (bits);
    for (i = 0; i < n; ++i) v->text[i] = reversed[n - i - 1];
    v->text[n] = 0;
    v->length = n;
}

static void environment_function(Parser *p, Value *value, unsigned depth, int run)
{
    Value arg;
    const char *name = NULL;
    char *bytes;
    long index = 0;
    int length;
    memset(&arg, 0, sizeof(arg));
    expect(p, '(', "Expected: (");
    expression(p, &arg, 1, depth + 1, run);
    expect(p, ')', "Expected: )");
    value->kind = '$';
    value->string = 1;
    if (record_size(arg.kind)) fail(p, "Type mismatch");
    if (!run || error_message) return;
    if (arg.string) {
        name = ckb_string_bytes(&arg);
        if (memchr(name, 0, arg.length)) {
            fail(p, "Illegal function call");
            return;
        }
    } else {
        index = integer_operand(p, arg.number, 1);
        if (error_message) return;
    }
    length = name ? ckenv_get(name, NULL, 0) : ckenv_at(index, NULL, 0);
    if (length < 0) {
        fail(p, length == -7 ? "String too long" : "Illegal function call");
        return;
    }
    bytes = ckb_string_buffer(value, (unsigned)length);
    if (!bytes) {
        fail(p, "Out of memory");
        return;
    }
    if (name) ckenv_get(name, bytes, (unsigned)length + 1);
    else ckenv_at(index, bytes, (unsigned)length + 1);
}

static int builtin(Parser *p, Value *v, unsigned depth, int run, int id, int length)
{
    Value *args;
    unsigned n = 0, i;
    const char *err;
    args = malloc(3 * sizeof(*args));
    if (!args) {
        fail(p, "Out of memory");
        return 1;
    }
    scan(p);
    expect(p, '(', "Expected: (");
    if (p->token != ')') do {
        if (n == 3) {
            fail(p, "Too many arguments");
            free(args);
            return 1;
        }
        expression(p, &args[n++], 1, depth + 1, run);
    } while (!error_message && take(p, ','));
    expect(p, ')', "Expected: )");
    if (!error_message) {
        if (length && n == 1 && record_size(args[0].kind)) {
            v->number = record_size(args[0].kind);
        } else if (id < 0) {
            if (n != 1) fail(p, "Incorrect number of arguments");
            else if (id <= CF_HEX) radix_string_builtin(p, v, &args[0], id, run);
            else if (id <= CF_MKS) encoding_builtin(p, v, &args[0], id, run);
            else if (args[0].string || record_size(args[0].kind)) fail(p, "Type mismatch");
            else if (id <= CF_ABS) math_builtin(p, v, &args[0], id, run);
            else {
                *v = args[0];
                convert_number(p, v, id == CF_CINT ? '%' : id == CF_CLNG ? '&' :
                               id == CF_CSNG ? '!' : '#', run);
            }
        } else {
            for (i = 0; i < n; ++i)
                if (record_size(args[i].kind)) fail(p, "Type mismatch");
            if (!error_message) {
                err = ckb_string_apply(id, v, args, n, run);
                if (err) fail(p, err);
                v->kind = '!';
            }
        }
    }
    free(args);
    return 1;
}

static void input_string(Parser *p, Value *v, unsigned depth, int run)
{
    Value length;
    Channel *file;
    long count;
    char *bytes;
    expect(p, '(', "Expected: (");
    expression(p, &length, 1, depth + 1, run);
    if (length.string || record_size(length.kind)) fail(p, "Type mismatch");
    expect(p, ',', "Expected: ,");
    file = file_number(p, run);
    expect(p, ')', "Expected: )");
    v->string = 1;
    if (!run || error_message) return;
    count = integer_operand(p, length.number, run);
    if (error_message) return;
    if (count < 0 || count > STRING_MAX) {
        fail(p, "Illegal function call");
        return;
    }
    if (!file_open(file)) {
        fail(p, "Bad file number");
        return;
    }
    if (file->mode == FILE_OUTPUT || file->mode == FILE_APPEND || !(file->access & ACCESS_READ)) {
        fail(p, "Input past end of file");
        return;
    }
    bytes = ckb_string_buffer(v, (unsigned)count);
    if (!bytes) fail(p, "Out of memory");
    else if (file->serial) {
        const char *error = ckb_serial_read(file->serial, bytes, (unsigned)count);
        if (error) fail(p, error);
    } else if (fread(bytes, 1, (unsigned)count, file->stream) != (unsigned)count) {
        fail(p, ferror(file->stream) ? "Read error" : "Input past end of file");
    }
    if (bytes) bytes[count] = 0;
}

static int expression_call(Parser *p, Value *v, unsigned depth, int run)
{
    static const char *const numeric_functions[] = {
        "CINT", "CLNG", "CSNG", "CDBL", "ABS", "SGN", "INT", "FIX",
        "SQR", "SIN", "COS", "TAN", "ATN", "EXP", "LOG",
        "MKS$", "MKD$", "MKSMBF$", "MKDMBF$", "CVS", "CVD", "CVSMBF", "CVDMBF",
        "MKI$", "MKL$", "CVI", "CVL", "HEX$", "OCT$"
    };
    unsigned i;
    char name[BASIC_NAME_MAX + 1];
    int id;
    if (error_value(p, v, run)) return 1;
    if (take_word(p, "INPUT$")) {
        input_string(p, v, depth, run);
        return 1;
    }
    if (take_word(p, "ENVIRON$")) {
        environment_function(p, v, depth, run);
        return 1;
    }
    if (take_word(p, "FREEFILE")) {
        v->kind = '%';
        if (run) {
            for (i = 0; i < FILE_MAX && file_open(&channels[i]); ++i) {}
            if (i == FILE_MAX) fail(p, "Too many files");
            else v->number = i + 1;
        }
        return 1;
    }
    if (word(p, "RND")) {
        random_value(p, v, depth, run);
        return 1;
    }
    if (p->length <= BASIC_NAME_MAX) {
        for (i = 0; i < p->length; ++i)
            name[i] = (char)toupper((unsigned char)source[p->start + i]);
        name[i] = 0;
        id = ckb_string_function(name);
        for (i = 0; !id && i < sizeof(numeric_functions) / sizeof(numeric_functions[0]); ++i)
            if (strcmp(name, numeric_functions[i]) == 0) id = CF_CINT - (int)i;
        if (id) {
            int result = builtin(p, v, depth, run, id, !strcmp(name, "LEN"));
            if (!strcmp(name, "ASC") || !strcmp(name, "INSTR") || !strcmp(name, "LEN"))
                v->kind = '%';
            return result;
        }
    }
    id = lookup_procedure(p);
    if (id < 0 || !procedures[id].function) return 0;
    if (current_scope == id) {
        Parser next = *p;
        scan(&next);
        if (next.token != '(') return 0;
    }
    scan(p);
    call_procedure(p, id, depth, run, v, 1);
    return 1;
}

static void check_signature(Parser *p, const ParameterType *a, unsigned a_count,
                            const ParameterType *b, unsigned b_count)
{
    unsigned i;
    if (a_count != b_count) {
        fail(p, "Argument-count mismatch");
        return;
    }
    for (i = 0; i < a_count; ++i) {
        if (a[i].kind != b[i].kind || a[i].array != b[i].array || a[i].width != b[i].width) {
            fail(p, "Parameter type mismatch");
            return;
        }
    }
}

static void procedure_header(Parser *p, Procedure *proc, const char *name, int declaration)
{
    ParameterType signature[PARAM_MAX];
    unsigned count = 0, j;
    int i;
    if (take(p, '(')) {
        if (p->token != ')') do {
            char param[BASIC_NAME_MAX + 1], kind;
            int typed, array, unsuffixed = unsuffixed_name(p);
            unsigned width = 0;
            if (count == PARAM_MAX) {
                fail(p, "Too many parameters");
                return;
            }
            if (!identifier(p, param, &kind)) return;
            array = take(p, '(');
            if (array) expect(p, ')', "Expected: )");
            typed = take_word(p, "AS");
            if (typed) kind = declared_kind(p, &width);
            if (error_message) return;
            signature[count].kind = kind;
            signature[count].array = array;
            signature[count].width = width;
            if (!declaration) {
                i = named_variable(p, param, kind, array, 1, unsuffixed, typed);
                if (i < 0) return;
                if (typed) variables[i].fixed_width = width;
                for (j = 0; j < count; ++j) {
                    if (proc->params[j] == i) {
                        fail(p, "Duplicate definition");
                        return;
                    }
                }
                proc->params[count] = i;
            }
            ++count;
        } while (!error_message && take(p, ','));
        expect(p, ')', "Expected: )");
    }
    if (proc->declared) {
        check_signature(p, proc->declared_params, proc->declared_count, signature, count);
    } else if (declaration) {
        if (proc->line != LINE_MAX) {
            ParameterType body[PARAM_MAX];
            for (j = 0; j < proc->count; ++j) {
                const Variable *param = &variables[proc->params[j]];
                body[j].kind = param->kind;
                body[j].array = param->array;
                body[j].width = param->fixed_width;
            }
            check_signature(p, body, proc->count, signature, count);
        }
        proc->declared = 1;
        proc->declared_count = count;
        memcpy(proc->declared_params, signature, count * sizeof(*signature));
    }
    if (declaration || error_message) return;
    proc->count = count;
    proc->persistent = take_word(p, "STATIC");
    if (proc->function) {
        for (i = 0; i < (int)variable_count; ++i)
            if (variables[i].scope == current_scope && !strcmp(variables[i].name, name) &&
                variables[i].kind == proc->result_kind) break;
        if (i == (int)variable_count) {
            if (variable_count == VALUE_MAX) {
                fail(p, "Too many variables");
                return;
            }
            ++variable_count;
            memset(&variables[i], 0, sizeof(variables[i]));
            strcpy(variables[i].name, name);
            variables[i].scope = current_scope;
            variables[i].kind = proc->result_kind;
        }
        proc->result = i;
    }
}

/* Pre-evaluate only side-effect-free CONST expressions. The ordinary pass
 * remains responsible for checking declarations we cannot index here. */
static void index_constants(Parser *p)
{
    do {
        Parser value_start, look;
        int id = variable_mode(p, 1);
        Value value;
        if (id < 0 || !expect(p, '=', "Expected: =")) return;
        value_start = look = *p;
        while (look.token && look.token != ',' && look.token != ':') {
            if (look.token == T_NAME && !word(&look, "NOT") && !precedence(&look)) {
                if (known_constant(&look) < 0) return;
            } else if (look.token != T_NAME && look.token != T_NUMBER &&
                       look.token != T_STRING && look.token != '(' && look.token != ')' &&
                       look.token != '+' && look.token != '-' && !precedence(&look)) return;
            scan(&look);
        }
        *p = value_start;
        indexing_constant = 1;
        expression(p, &value, 1, 0, 1);
        indexing_constant = 0;
        if (error_message || (p->token && p->token != ',' && p->token != ':')) return;
        variables[id].constant = 0;
        assign(p, id, &value, 1);
        variables[id].constant = 1;
        variables[id].indexed_constant = 1;
        variables[id].indexed_number = variables[id].number;
        variables[id].shared = current_scope < 0;
    } while (!error_message && take(p, ','));
}

static void index_procedures(void)
{
    unsigned line;
    int scope = -1, id, declaration, function;
    Parser p;
    char name[BASIC_NAME_MAX + 1], kind;
    for (line = 0; line < line_count && !error_message; ++line) {
        initialize_parser(&p, position(line, lines[line].body));
        line_scope[line] = scope;
        current_scope = scope;
        if (take_word(&p, "CONST")) {
            CkbStringTemp *mark = ckb_string_mark();
            index_constants(&p);
            ckb_string_release(mark);
        } else if (take_word(&p, "TYPE")) {
            if (scope >= 0) fail(&p, "TYPE must be in module-level code");
            else line = index_record(&p, line);
        } else if (take_word(&p, "DEFINT")) default_type_statement(&p, '%');
        else if (take_word(&p, "DEFLNG")) default_type_statement(&p, '&');
        else if (take_word(&p, "DEFSNG")) default_type_statement(&p, '!');
        else if (take_word(&p, "DEFDBL")) default_type_statement(&p, '#');
        else if (take_word(&p, "DEFSTR")) default_type_statement(&p, '$');
        else if (take_word(&p, "END") && (word(&p, "SUB") || word(&p, "FUNCTION"))) {
            if (scope < 0 || word(&p, "FUNCTION") != procedures[scope].function)
                fail(&p, "Mismatched procedure end");
            else procedures[scope].end = line;
            scope = -1;
        } else {
            declaration = take_word(&p, "DECLARE");
            function = take_word(&p, "FUNCTION");
            if (!function && !take_word(&p, "SUB")) continue;
            if (scope >= 0) {
                fail(&p, "Nested procedure definition");
                continue;
            }
            if (!identifier(&p, name, &kind)) continue;
            id = procedure_id(name);
            if (id < 0) {
                if (procedure_count == PROC_MAX) {
                    fail(&p, "Too many procedures");
                    continue;
                }
                id = (int)procedure_count++;
                memset(&procedures[id], 0, sizeof(procedures[id]));
                strcpy(procedures[id].name, name);
                procedures[id].result_kind = kind;
                procedures[id].function = function;
                procedures[id].line = procedures[id].end = LINE_MAX;
            }
            if (procedures[id].function != function) fail(&p, "Procedure type mismatch");
            if (function && procedures[id].result_kind != kind) fail(&p, "Duplicate definition");
            if (error_message) continue;
            if (!declaration) {
                if (procedures[id].line != LINE_MAX) {
                    fail(&p, "Duplicate procedure");
                    continue;
                }
                procedures[id].line = line;
                scope = id;
                line_scope[line] = id;
                current_scope = id;
            }
            procedure_header(&p, &procedures[id], name, declaration);
            current_scope = -1;
            if (!end_statement(&p)) fail(&p, "Expected: end of statement");
        }
    }
    for (id = 0; id < (int)procedure_count; ++id)
        if (procedures[id].line != LINE_MAX && procedures[id].end == LINE_MAX) {
            initialize_parser(&p, position(0, lines[0].body));
            fail(&p, "Missing procedure body");
        }
}

static void assign_frame_slots(void)
{
    unsigned i;
    for (i = 0; i < variable_count; ++i) {
        Variable *v = &variables[i];
        Procedure *proc;
        unsigned param;
        if (v->scope < 0 || v->shared) continue;
        proc = &procedures[v->scope];
        for (param = 0; param < proc->count; ++param)
            if (proc->params[param] == (int)i) break;
        if (param < proc->count || (proc->function && proc->result == (int)i) ||
            (!proc->persistent && !v->persistent))
            v->frame_slot = ++proc->locals;
    }
}

static int case_matches(Parser *p, const Value *selected, int run, Value *value)
{
    int match = 0;
    if (take_word(p, "ELSE")) return 1;
    do {
        expression(p, value, 1, 0, run);
        if (record_size(value->kind)) fail(p, "Type mismatch");
        if (selected) {
            if (value->string != selected->string) fail(p, "Type mismatch");
            else if (value->string ? ckb_string_compare(value, selected) == 0 :
                     value->number == selected->number) match = 1;
        }
    } while (!error_message && take(p, ','));
    return match;
}

static void select_statement(Parser *p, Position at, int run, unsigned inline_depth)
{
    Value selected, candidate;
    unsigned index, peer;
    if (!expect_word(p, "CASE", "Expected: CASE")) return;
    expression(p, &selected, 1, 0, run);
    if (record_size(selected.kind)) fail(p, "Type mismatch");
    index = block(p, at, B_SELECT, inline_depth);
    if (!run || error_message) return;
    for (peer = blocks[index].peer; blocks[peer].kind == B_CASE && !error_message;
         peer = blocks[peer].peer) {
        Parser test;
        initialize_parser(&test, blocks[peer].at);
        scan(&test);
        if (case_matches(&test, &selected, run, &candidate)) {
            go(blocks[peer].after);
            return;
        }
    }
    if (!error_message) go(blocks[blocks[index].finish].after);
}

static void swap_statement(Parser *p, int run)
{
    Reference left = lvalue(p, run), right;
    Value a, b;
    unsigned size;
    char *x, *y;
    if (!expect(p, ',', "Expected: ,")) return;
    right = lvalue(p, run);
    if (error_message || !left.target || !right.target) return;
    if (left.kind != right.kind) {
        fail(p, "Type mismatch");
        return;
    }
    if (left.target->constant || right.target->constant) {
        fail(p, "Cannot assign to a constant");
        return;
    }
    if (!run || !valid_cell(p, left.target, left.cell) ||
        !valid_cell(p, right.target, right.cell)) return;
    if (left.target == right.target && left.cell == right.cell &&
        left.offset == right.offset) return;
    size = record_size(left.kind);
    if (size) {
        x = record_data(p, left);
        if (!x) return;
        y = record_data(p, right);
        if (!y) return;
        while (size--) {
            char byte = *x;
            *x++ = *y;
            *y++ = byte;
        }
    } else {
        fetch_reference(p, left, 1, &a);
        if (!error_message) fetch_reference(p, right, 1, &b);
        if (!error_message) assign_reference(p, left, &b, 1);
        if (!error_message) assign_reference(p, right, &a, 1);
    }
}

static void justify_assign(Parser *p, int run, int right)
{
    Reference ref = lvalue(p, run);
    Value value, replacement;
    unsigned width, count, offset;
    char *field, *destination;
    expect(p, '=', "Expected: =");
    expression(p, &replacement, 1, 0, run);
    if (!ref.target || error_message) return;
    if (!right && record_size(ref.kind) && record_size(replacement.kind)) {
        if (!run || !valid_cell(p, ref.target, ref.cell)) return;
        width = record_size(ref.kind);
        count = record_size(replacement.kind);
        field = record_data(p, ref);
        if (field) memmove(field, replacement.record, count < width ? count : width);
        return;
    }
    if (ref.kind != '$' || !replacement.string) {
        fail(p, "Type mismatch");
        return;
    }
    if (!run) return;
    field = field_data(ref.target, &width);
    if (!field) {
        fetch_reference(p, ref, 1, &value);
        if (error_message) return;
        width = value.length;
    }
    count = replacement.length;
    if (count > width) count = width;
    offset = right ? width - count : 0;
    if (field) {
        memset(field, ' ', width);
        memcpy(field + offset, ckb_string_bytes(&replacement), count);
        return;
    }
    destination = ckb_string_buffer_at(&value);
    memset(destination, ' ', width);
    memcpy(destination + offset, ckb_string_bytes(&replacement), count);
    assign_reference(p, ref, &value, 1);
}

static void mid_assign(Parser *p, int run)
{
    Reference ref;
    unsigned start, count, length, n;
    long from, requested = STRING_MAX;
    Value value, replacement;
    char *field;
    expect(p, '(', "Expected: (");
    ref = lvalue(p, run);
    expect(p, ',', "Expected: ,");
    from = integer_operand(p, numeric(p, run), run);
    if (take(p, ',')) requested = integer_operand(p, numeric(p, run), run);
    expect(p, ')', "Expected: )");
    expect(p, '=', "Expected: =");
    expression(p, &replacement, 1, 0, run);
    if (!ref.target) return;
    if (ref.kind != '$' || !replacement.string) fail(p, "Type mismatch");
    if (!run || error_message) return;
    if (from < 1 || from > 32767 || requested < 0 || requested > 32767) {
        fail(p, "Illegal function call");
        return;
    }
    field = field_data(ref.target, &length);
    if (!field) {
        fetch_reference(p, ref, 1, &value);
        if (error_message) return;
        length = value.length;
    }
    start = (unsigned)(from - 1);
    if (start >= length) return;
    count = (unsigned)requested;
    n = replacement.length;
    if (count > n) count = n;
    if (count > length - start) count = length - start;
    if (field) memcpy(field + start, ckb_string_bytes(&replacement), count);
    else {
        memcpy(ckb_string_buffer_at(&value) + start, ckb_string_bytes(&replacement), count);
        assign_reference(p, ref, &value, run);
    }
}

static void statement(Parser *p, int run, unsigned inline_depth);

static void inline_statement_list(Parser *p, int run, unsigned inline_depth)
{
    if (p->token == T_NUMBER) {
        unsigned line = target(p);
        if (run && !error_message) go_line(line);
        return;
    }
    if (end_statement(p)) {
        fail(p, "Expected: statement");
        return;
    }
    do {
        statement(p, run && !jumped && !stopped && !returning, inline_depth);
    } while (!error_message && take(p, ':') && p->token && !word(p, "ELSE"));
}

/* Keep statement-specific Value buffers out of the dispatcher, which
 * stays on the native stack when a statement invokes a procedure. */
static void case_statement(Parser *p, Position at, int run, unsigned inline_depth)
{
    Value v;
    unsigned index;
    case_matches(p, NULL, 0, &v);
    index = block(p, at, B_CASE, inline_depth);
    if (run && !error_message) go(blocks[blocks[blocks[index].owner].finish].after);
}

static void for_statement(Parser *p, Position at, int run, unsigned inline_depth)
{
    Value v;
    unsigned index;
    double begin, end, step;
    ForFrame *frame;
    int id = variable(p);
    if (id >= 0 && (variables[id].kind == '$' || record_size(variables[id].kind) ||
                    variables[id].array))
        fail(p, "Type mismatch");
    expect(p, '=', "Expected: =");
    begin = numeric(p, run);
    expect_word(p, "TO", "Expected: TO");
    end = numeric(p, run);
    step = take_word(p, "STEP") ? numeric(p, run) : 1;
    if (error_message) return;
    index = block(p, at, B_FOR, inline_depth);
    if (run && !error_message) {
        memset(&v, 0, sizeof(v));
        v.number = begin;
        assign(p, id, &v, 1);
        while (for_depth > (activation ? activation->for_base : 0) &&
               for_stack[for_depth - 1].block == index) --for_depth;
        if (step >= 0 ? v.number > end : v.number < end) go(blocks[blocks[index].peer].after);
        else if (for_depth == STACK_MAX) fail(p, "Too many active FOR loops");
        else {
            frame = &for_stack[for_depth++];
            frame->variable = id;
            frame->block = index;
            frame->last = end;
            frame->step = step;
        }
    }
}

static void next_statement(Parser *p, Position at, int run, unsigned inline_depth)
{
    Value v;
    unsigned index, peer;
    ForFrame *frame;
    int id = end_statement(p) ? -1 : variable(p);
    if (error_message) return;
    index = block(p, at, B_NEXT, inline_depth);
    if (run && !error_message) {
        peer = blocks[index].peer;
        if (for_depth == (activation ? activation->for_base : 0) ||
            for_stack[for_depth - 1].block != peer ||
            (id >= 0 && id != for_stack[for_depth - 1].variable)) {
            fail(p, "NEXT without matching FOR");
        } else {
            frame = &for_stack[for_depth - 1];
            fetch(p, frame->variable, 0, 1, &v);
            if (error_message) return;
            v.number += frame->step;
            if (!finite_number(v.number)) fail(p, "Overflow");
            else assign(p, frame->variable, &v, 1);
            if (frame->step >= 0 ? v.number <= frame->last : v.number >= frame->last)
                go(blocks[peer].after);
            else --for_depth;
        }
    }
}

static void assignment_statement(Parser *p, int run)
{
    Value v;
    int explicit_let = take_word(p, "LET");
    Reference ref = lvalue(p, run);
    if (p->token != '=' && !explicit_let) fail(p, "Expected: statement");
    else if (expect(p, '=', "Expected: =")) {
        expression(p, &v, 1, 0, run);
        if (!error_message) assign_reference(p, ref, &v, run);
    }
}

static int sub_call_statement(Parser *p, int run)
{
    int explicit_call = take_word(p, "CALL");
    int id = lookup_procedure(p);
    if (id < 0 || procedures[id].function) {
        if (explicit_call) fail(p, "Expected: SUB");
        return explicit_call;
    }
    scan(p);
    if (explicit_call && procedures[id].count && p->token != '(')
        fail(p, "Expected: (");
    else call_procedure(p, id, 0, run, NULL, explicit_call);
    return 1;
}

#if defined(__WATCOMC__) || defined(_MSDOS)
/* Immediate parsing has its own physical line so suspended Parser values,
 * label pointers, DATA positions and the program text stay valid. */
static void immediate_statement(Parser *p, int run)
{
    if (take_word(p, "PRINT") || take(p, '?')) print_statement(p, run, 0);
    else if (take_word(p, "WRITE")) write_statement(p, run);
    else if (!sub_call_statement(p, run)) {
        if (p->token == T_NAME) assignment_statement(p, run);
        else fail(p, "Expected: statement");
    }
}

static void immediate_line(int run)
{
    Parser p;
    initialize_parser(&p, position(LINE_MAX, IMMEDIATE_START));
    while (p.token && !error_message && !stopped) {
        CkbStringTemp *mark = ckb_string_mark();
        immediate_statement(&p, run);
        ckb_string_release(mark);
        if (!take(&p, ':')) {
            if (p.token) fail(&p, "Expected: end of statement");
            break;
        }
    }
}

void ckb_immediate_clear(void)
{
    unsigned i;
    if (!immediate_idle) return;
    for (i = 0; i < variable_count; ++i) {
        release_array(&variables[i]);
        free(variables[i].text);
        variables[i].text = NULL;
    }
    variable_count = 0;
    immediate_idle = 0;
}

void ckb_immediate(const char *command, unsigned output_column, CkbImmediateResult *result)
{
    Position saved_error = first_error;
    Position saved_next = next_position, saved_resume = error_resume;
    Position saved_resume_next = error_resume_next;
    const char *saved_message = error_message;
    CkbRunPause saved_pause = pause_execution;
    CkbRunBreak saved_break = break_execution;
    int saved_stopped = stopped, saved_returning = returning;
    int saved_jumped = jumped, saved_handling = handling_error;
    int saved_scope = current_scope, saved_resume_scope = resume_scope;
    unsigned saved_handlers[PROC_MAX + 1], i;
    unsigned saved_code = trapped_error_code, saved_line = trapped_error_line;
    unsigned saved_for = for_depth, saved_call = call_depth;
    unsigned saved_restart = restart_kind;
    char saved_path[sizeof(restart_path)];
    unsigned variables_before, length = strlen(command);
    memset(result, 0, sizeof(*result));
    if (length > CKB_IMMEDIATE_LIMIT) {
        result->error = "Line too long";
        return;
    }
    if (!saved_pause && !immediate_idle) {
        immediate_idle = 1;
        variable_count = procedure_count = line_count = 0;
        current_scope = -1;
        option_base = dynamic_arrays = mbf_mode = 0;
        random_state = 0x50000UL;
        trapped_error_code = trapped_error_line = 0;
        memset(default_kind, '!', sizeof(default_kind));
    }
    variables_before = variable_count;
    _fmemcpy(source + IMMEDIATE_START, command, length + 1);
    lines[LINE_MAX].start = lines[LINE_MAX].body = IMMEDIATE_START;
    lines[LINE_MAX].end = IMMEDIATE_START + length;
    screen_column = output_column;
    immediate_output = 0;
    immediate_active = 1;
    error_message = NULL;
    pause_execution = NULL;
    break_execution = NULL;
    stopped = returning = 0;
    memcpy(saved_handlers, error_handler, sizeof(saved_handlers));
    for (i = 0; i <= PROC_MAX; ++i) error_handler[i] = LINE_MAX;
    memcpy(saved_path, restart_path, sizeof(saved_path));
    restart_kind = RESTART_NONE;
    handling_error = jumped = 0;
    immediate_line(0);
    if (!error_message) {
        immediate_line(1);
        result->runtime_error = error_message != NULL;
    } else variable_count = variables_before;
    result->error = error_message;
    result->wrote_output = immediate_output;
    immediate_active = 0;
    error_message = saved_message;
    first_error = saved_error;
    pause_execution = saved_pause;
    break_execution = saved_break;
    stopped = saved_stopped;
    returning = saved_returning;
    jumped = saved_jumped;
    next_position = saved_next;
    current_scope = saved_scope;
    handling_error = saved_handling;
    resume_scope = saved_resume_scope;
    error_resume = saved_resume;
    error_resume_next = saved_resume_next;
    trapped_error_code = saved_code;
    trapped_error_line = saved_line;
    memcpy(error_handler, saved_handlers, sizeof(saved_handlers));
    for_depth = saved_for;
    call_depth = saved_call;
    restart_kind = saved_restart;
    memcpy(restart_path, saved_path, sizeof(saved_path));
}
#endif

static void statement(Parser *p, int run, unsigned inline_depth)
{
    Position at = position(p->line, p->start);
    unsigned index, line, body;
    int kind, condition;
    array_directive(p);
    if (!p->token) return;
    if (word(p, "REM") || word(p, "DECLARE")) {
        p->token = 0;
        p->pos = lines[p->line].end;
        return;
    }
    if (run && !error_message && break_execution) {
        body = lines[at.line].body;
        while (body < at.offset && isspace((unsigned char)source[body])) ++body;
        if (break_execution(pause_context, lines[at.line].start,
                            !inline_depth && body == at.offset, procedure_depth) &&
            !before_statement(p, at)) return;
    }
    if (take_word(p, "PRINT") || take(p, '?')) {
        print_statement(p, run, 0);
    } else if (take_word(p, "LPRINT")) {
        print_statement(p, run, 1);
    } else if (take_word(p, "DEFINT")) {
        default_type_statement(p, '%');
    } else if (take_word(p, "DEFLNG")) {
        default_type_statement(p, '&');
    } else if (take_word(p, "DEFSNG")) {
        default_type_statement(p, '!');
    } else if (take_word(p, "DEFDBL")) {
        default_type_statement(p, '#');
    } else if (take_word(p, "DEFSTR")) {
        default_type_statement(p, '$');
    } else if (take_word(p, "CONST")) {
        const_statement(p, run);
    } else if (take_word(p, "RANDOMIZE")) {
        double seed = numeric(p, run);
        if (run && !error_message) seed_random(seed);
    } else if (take_word(p, "DIM")) {
        dim_statement(p, run, 0, 0);
    } else if (take_word(p, "REDIM")) {
        dim_statement(p, run, 1, 0);
    } else if (word(p, "RUN") || word(p, "CHAIN")) {
        int chain = word(p, "CHAIN");
        Value path;
        scan(p);
        expression(p, &path, 1, 0, run);
        if (!path.string) fail(p, "Type mismatch");
        else if (run && !error_message) {
            const char *bytes = ckb_string_bytes(&path);
            if (!path.length || path.length > TEXT_INPUT_MAX || memchr(bytes, 0, path.length))
                fail(p, "Bad file name");
            else {
                memcpy(restart_path, bytes, path.length);
                restart_path[path.length] = 0;
                restart_kind = chain ? RESTART_CHAIN : RESTART_RUN;
                stopped = 1;
            }
        }
    } else if (take_word(p, "COMMON")) {
        storage_statement(p, 1);
    } else if (take_word(p, "SHARED")) {
        storage_statement(p, 0);
    } else if (take_word(p, "ERASE")) {
        erase_statement(p, run);
    } else if (take_word(p, "STATIC")) {
        dim_statement(p, run, 0, 1);
    } else if (take_word(p, "OPTION")) {
        expect_word(p, "BASE", "Expected: BASE");
        if (p->token != T_NUMBER || p->length != 1 ||
            (p->number != 0 && p->number != 1))
            fail(p, "Expected: 0 or 1");
        else {
            if (checking) option_base = (long)p->number;
            scan(p);
        }
    } else if (take_word(p, "CLS")) {
        if (run) {
            ckb_cls();
            screen_column = 0;
        }
    } else if (take_word(p, "COLOR")) {
        int fg = -1, bg = -1;
        if (!end_statement(p) && p->token != ',') fg = (int)numeric(p, run);
        if (take(p, ',')) bg = (int)numeric(p, run);
        if (run && !error_message) ckb_color(fg, bg);
    } else if (take_word(p, "INPUT")) {
        if (take(p, '#')) input_file_statement(p, run);
        else input_statement(p, run, at);
    } else if (take_word(p, "WRITE")) {
        write_statement(p, run);
    } else if (take_word(p, "OPEN")) {
        open_statement(p, run);
    } else if (take_word(p, "LOCK")) {
        file_lock_statement(p, run, 0);
    } else if (take_word(p, "UNLOCK")) {
        file_lock_statement(p, run, 1);
    } else if (take_word(p, "FIELD")) {
        field_statement(p, run);
    } else if (take_word(p, "GET")) {
        file_transfer(p, run, 0);
    } else if (take_word(p, "PUT")) {
        file_transfer(p, run, 1);
    } else if (take_word(p, "SEEK")) {
        Channel *file = file_number(p, run);
        long position;
        expect(p, ',', "Expected: ,");
        position = integer_operand(p, numeric(p, run), run);
        if (run && !error_message) seek_file(p, file, position);
    } else if (take_word(p, "LINE")) {
        if (expect_word(p, "INPUT", "Expected: INPUT")) line_input_statement(p, run);
    } else if (take_word(p, "NAME")) {
        rename_statement(p, run);
    } else if (take_word(p, "MKDIR")) {
        path_statement(p, run, CREATE_DIRECTORY);
    } else if (take_word(p, "CHDIR")) {
        path_statement(p, run, CHANGE_DIRECTORY);
    } else if (take_word(p, "RMDIR")) {
        path_statement(p, run, REMOVE_DIRECTORY);
    } else if (take_word(p, "KILL")) {
        path_statement(p, run, DELETE_FILE);
    } else if (take_word(p, "ENVIRON")) {
        environment_statement(p, run);
    } else if (take_word(p, "SHELL")) {
        shell_statement(p, run);
    } else if (take_word(p, "CLOSE")) {
        if (end_statement(p)) {
            if (run) for (index = 0; index < FILE_MAX; ++index) close_file(p, &channels[index]);
        } else do {
            Channel *file = file_number(p, run);
            if (run && !error_message) close_file(p, file);
        } while (!error_message && take(p, ','));
    } else if (take_word(p, "SELECT")) {
        select_statement(p, at, run, inline_depth);
    } else if (take_word(p, "CASE")) {
        case_statement(p, at, run, inline_depth);
    } else if (take_word(p, "SWAP")) {
        swap_statement(p, run);
    } else if (take_word(p, "LSET")) {
        justify_assign(p, run, 0);
    } else if (take_word(p, "RSET")) {
        justify_assign(p, run, 1);
    } else if (take_word(p, "MID$")) {
        mid_assign(p, run);
    } else if (take_word(p, "ERROR")) {
        long code = integer_operand(p, numeric(p, run), run);
        if (run && !error_message) {
            if (code <= 0 || code > 255) fail(p, "Illegal function call");
            else {
                first_error = at;
                error_message = runtime_error_text((unsigned)code);
            }
        }
    } else if (take_word(p, "ON")) {
        expect_word(p, "ERROR", "Expected: ERROR");
        expect_word(p, "GOTO", "Expected: GOTO");
        if (p->token == T_NUMBER && p->number == 0) {
            scan(p);
            line = LINE_MAX;
        } else line = target(p);
        if (run && !error_message) error_handler[current_scope + 1] = line;
    } else if (take_word(p, "RESUME")) {
        int resume_next = take_word(p, "NEXT");
        line = end_statement(p) ? LINE_MAX : target(p);
        if (run && !error_message) {
            if (!handling_error) fail(p, "RESUME without error");
            else {
                handling_error = 0;
                trapped_error_code = trapped_error_line = 0;
                current_scope = resume_scope;
                if (line < LINE_MAX) go_line(line);
                else go(resume_next ? error_resume_next : error_resume);
            }
        }
    } else if (take_word(p, "DATA")) {
        data_statement(p);
    } else if (take_word(p, "READ")) {
        do {
            Reference ref = lvalue(p, run);
            if (record_size(ref.kind)) fail(p, "Type mismatch");
            if (run && !error_message) read_value(p, ref);
        } while (!error_message && take(p, ','));
    } else if (take_word(p, "RESTORE")) {
        line = end_statement(p) ? 0 : target(p);
        if (run && !error_message) {
            data_index = data_offset = 0;
            while (data_index < data_count && data_statements[data_index].line < line) ++data_index;
        }
    } else if (take_word(p, "GOTO")) {
        line = target(p);
        if (run && !error_message) go_line(line);
    } else if (take_word(p, "GOSUB")) {
        line = target(p);
        if (run && !error_message) {
            if (call_depth == STACK_MAX) fail(p, "Too many nested GOSUBs");
            else {
                calls[call_depth++] = after_statement(p);
                go_line(line);
            }
        }
    } else if (take_word(p, "RETURN")) {
        line = end_statement(p) ? line_count : target(p);
        if (run && !error_message) {
            if (call_depth == (activation ? activation->call_base : 0))
                fail(p, "RETURN without GOSUB");
            else {
                --call_depth;
                if (line == line_count) go(calls[call_depth]);
                else go_line(line);
            }
        }
    } else if (take_word(p, "IF")) {
        condition = numeric(p, run) != 0;
        if (take_word(p, "GOTO")) {
            line = target(p);
            if (run && condition && !error_message) go_line(line);
        } else if (expect_word(p, "THEN", "Expected: THEN") && !error_message) {
            if (!p->token) {
                index = block(p, at, B_IF, inline_depth);
                if (run && !condition && !error_message) go(blocks[blocks[index].peer].after);
            } else if (inline_depth == 3) fail(p, "IF nested too deeply");
            else {
                inline_statement_list(p, run && condition, inline_depth + 1);
                if (take_word(p, "ELSE"))
                    inline_statement_list(p, run && !condition, inline_depth + 1);
            }
        }
    } else if (take_word(p, "ELSE")) {
        index = block(p, at, B_ELSE, inline_depth);
        if (run && !error_message) go(blocks[blocks[index].peer].after);
    } else if (take_word(p, "END")) {
        if (take_word(p, "IF")) block(p, at, B_ENDIF, inline_depth);
        else if (take_word(p, "SELECT")) block(p, at, B_ENDSELECT, inline_depth);
        else if (take_word(p, "SUB") || take_word(p, "FUNCTION")) {
            if (run) returning = 1;
        } else if (run) stopped = 1;
    } else if (take_word(p, "EXIT")) {
        if (!(take_word(p, "SUB") || take_word(p, "FUNCTION")) || current_scope < 0)
            fail(p, "Expected: EXIT SUB or EXIT FUNCTION");
        else if (run) returning = 1;
    } else if (take_word(p, "SYSTEM")) {
        if (run) stopped = 1;
    } else if (take_word(p, "STOP")) {
        if (run && pause_execution) pause_at(p, at, NULL);
        else if (run) {
            stopped = 1;
            fail(p, "Program stopped");
        }
    } else if (take_word(p, "FOR")) {
        for_statement(p, at, run, inline_depth);
    } else if (take_word(p, "NEXT")) {
        next_statement(p, at, run, inline_depth);
    } else if (take_word(p, "DO")) {
        condition = loop_passes(p, run, &kind);
        index = block(p, at, B_DO, inline_depth);
        if (run && !condition && !error_message) go(blocks[blocks[index].peer].after);
    } else if (take_word(p, "LOOP")) {
        condition = loop_passes(p, run, &kind);
        index = block(p, at, B_LOOP, inline_depth);
        if (run && condition && !error_message) go(blocks[blocks[index].peer].at);
    } else if (take_word(p, "WHILE")) {
        condition = numeric(p, run) != 0;
        index = block(p, at, B_WHILE, inline_depth);
        if (run && !condition && !error_message) go(blocks[blocks[index].peer].after);
    } else if (take_word(p, "WEND")) {
        index = block(p, at, B_WEND, inline_depth);
        if (run && !error_message) go(blocks[blocks[index].peer].at);
    } else if (!sub_call_statement(p, run)) {
        assignment_statement(p, run);
    }
}

static void read_lines(unsigned size)
{
    unsigned start = 0, end, body, label_end, i;
    Line *line;
    while (start < size && !error_message) {
        if (line_count == LINE_MAX) {
            first_error = position(line_count, start);
            error_message = "Too many source lines";
            break;
        }
        end = start;
        while (end < size && source[end] != '\r' && source[end] != '\n') ++end;
        body = start;
        while (body < end && (source[body] == ' ' || source[body] == '\t')) ++body;
        line = &lines[line_count];
        line->start = start;
        line->body = body;
        line->end = end;
        line->label = NULL;
        label_end = body;
        if (body < end && isdigit((unsigned char)source[body])) {
            while (label_end < end && isdigit((unsigned char)source[label_end])) ++label_end;
            line->label = source + body;
            line->body = label_end;
        } else if (body < end && name_start(source[body])) {
            while (label_end < end && name_char(source[label_end])) ++label_end;
            if (label_end < end && source[label_end] == ':') {
                /* Statement keywords can also be followed by a colon. Only
                 * an identifier at the front of the line is a label. */
                static const char *const keywords[] = {
                    "CLOSE", "DATA", "DO", "ELSE", "END", "LOOP", "NEXT",
                    "PRINT", "REM", "RESTORE", "RETURN", "STOP", "SYSTEM", "WEND"
                };
                unsigned k;
                for (k = 0; k < sizeof(keywords) / sizeof(keywords[0]); ++k)
                    if (same(source + body, label_end - body, keywords[k])) break;
                if (k == sizeof(keywords) / sizeof(keywords[0])) {
                    line->label = source + body;
                    line->body = label_end + 1;
                }
            }
        }
        if (line->label) {
            line->label_length = label_end - body;
            for (i = 0; i < line_count; ++i) {
                if (lines[i].label && label_equal(line->label, line->label_length,
                                                  lines[i].label, lines[i].label_length)) {
                    first_error = position(line_count, body);
                    error_message = "Duplicate label";
                    break;
                }
            }
        }
        ++line_count;
        start = end;
        if (start < size && source[start] == '\r') ++start;
        if (start < size && source[start] == '\n') ++start;
    }
}

static int load_source(const char *path)
{
    FILE *in = fopen(path, "rb");
    int c;
    unsigned size = 0;
    if (!in) {
        fprintf(stderr, "%s: File not found\n", path);
        return 0;
    }
    while ((c = fgetc(in)) != EOF && c != 26) {
        if (size == SOURCE_MAX || c == 0) {
            fprintf(stderr, "%s: %s\n", path, c == 0 ? "Expected DOS text" : "Source too large");
            fclose(in);
            return 0;
        }
        source[size++] = (char)c;
    }
    if (ferror(in)) {
        fprintf(stderr, "%s: Read error\n", path);
        fclose(in);
        return 0;
    }
    fclose(in);
    source[size] = 0;
    read_lines(size);
    return 1;
}

static void walk_at(Position at, int run)
{
    Parser p;
    while (at.line < line_count && !error_message && !stopped && !returning) {
        unsigned type;
        int scope = line_scope[at.line];
        CkbStringTemp *mark;
        for (type = 0; type < record_count; ++type) {
            if (at.line >= records[type].line && at.line <= records[type].end) {
                at = next_line(records[type].end);
                break;
            }
        }
        if (type < record_count) continue;
        if (scope >= 0 && at.line == procedures[scope].line) {
            at = next_line(run && current_scope != scope ? procedures[scope].end : at.line);
            continue;
        }
        if (!run) current_scope = scope;
        initialize_parser(&p, at);
        jumped = 0;
        mark = ckb_string_mark();
        if (run && ckb_serial_open_count) ckb_serial_poll();
        if (p.token != ':') statement(&p, run, 0);
        ckb_string_release(mark);
        if (!p.token) array_directive(&p);
        if (run && error_message && !stopped && !handling_error && error_handler[current_scope + 1] < line_count) {
            unsigned handler = error_handler[current_scope + 1];
            Parser rest = p;
            while (rest.token && rest.token != ':') scan(&rest);
            error_resume = at;
            error_resume_next = after_statement(&rest);
            handling_error = 1;
            resume_scope = current_scope;
            trapped_error_code = runtime_error_code(error_message);
            trapped_error_line = error_line_number(first_error.line);
            error_message = NULL;
            if (line_scope[handler] < 0) current_scope = -1;
            go_line(handler);
        }
        if (!jumped && !end_statement(&p)) fail(&p, "Expected: end of statement");
        if (!jumped && word(&p, "ELSE"))
            fail(&p, "ELSE without IF");
        if (run && !stopped && error_message && pause_execution &&
            pause_at(&p, at, error_message)) {
            error_message = NULL;
            continue;
        }
        if (jumped) at = next_position;
        else at = after_statement(&p);
    }
    if (!run && !error_message && block_depth) {
        first_error = blocks[block_stack[block_depth - 1]].at;
        error_message = "Block without matching terminator";
    }
}

/* The next module owns its own declarations. Transfer storage by COMMON
 * position, with the old TYPE layouts kept separate from the new type IDs. */
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

static void free_common(CommonValue *saved, unsigned count, CommonType *types)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        CommonValue *v = &saved[i];
        if (v->array && v->kind == '$') {
            unsigned cell;
            for (cell = 0; cell < v->cells; ++cell) free(((char **)v->elements)[cell]);
        }
        free(v->elements);
        free(v->bounds);
        free(v->text);
    }
    free(saved);
    for (i = 0; i < RECORD_MAX; ++i) {
        RecordField *field = types[i].fields;
        while (field) {
            RecordField *next = field->next;
            free(field);
            field = next;
        }
        types[i].fields = NULL;
    }
}

static void save_common(CommonValue *saved, CommonType *types)
{
    unsigned i;
    for (i = 0; i < common_count; ++i) {
        Variable *v = &variables[common_ids[i]];
        CommonValue *item = &saved[i];
        item->kind = v->kind;
        item->width = v->fixed_width;
        item->array = v->array;
        item->number = v->number;
        item->text = v->text;
        item->elements = v->elements;
        item->bounds = v->bounds;
        item->cells = v->cells;
        item->dimensions = v->dimensions;
        v->text = NULL;
        v->elements = NULL;
        v->bounds = NULL;
        v->cells = v->dimensions = 0;
    }
    for (i = 0; i < record_count; ++i) {
        types[i].fields = records[i].fields;
        types[i].size = records[i].size;
        records[i].fields = NULL;
    }
}

static int common_kind_matches(char old_kind, char new_kind, const CommonType *types)
{
    const RecordType *new_type = record_type(new_kind);
    unsigned old_id = (unsigned char)old_kind;
    const RecordField *a, *b;
    if (!old_id || old_id > RECORD_MAX) return old_kind == new_kind;
    if (!new_type || types[old_id - 1].size != new_type->size) return 0;
    a = types[old_id - 1].fields;
    b = new_type->fields;
    for (; a && b; a = a->next, b = b->next) {
        if (a->offset != b->offset || a->width != b->width ||
            !common_kind_matches(a->kind, b->kind, types)) return 0;
    }
    return !a && !b;
}

static void restore_common(Parser *p, CommonValue *saved, const CommonType *types,
                           Variable *v)
{
    if (v->array != saved->array || v->fixed_width != saved->width ||
        !common_kind_matches(saved->kind, v->kind, types)) {
        fail(p, "Type mismatch");
        return;
    }
    v->number = saved->number;
    v->text = saved->text;
    v->bounds = saved->bounds;
    v->elements = saved->elements;
    v->cells = saved->cells;
    v->dimensions = saved->dimensions;
    saved->text = NULL;
    saved->bounds = NULL;
    saved->elements = NULL;
    saved->cells = 0;
}

/* Keep the operating system handle, cursor, and RANDOM buffer on CHAIN.
 * Detach FIELD links before recycling the module's variable table. */
static void carry_fields(int restore)
{
    unsigned i;
    for (i = 0; i < FILE_MAX; ++i) {
        FieldBinding **link = &channels[i].fields;
        while (*link) {
            FieldBinding *binding = *link;
            if (!restore) {
                binding->common = binding->variable->common;
                binding->variable = NULL;
            } else if (binding->common && binding->common <= common_count)
                binding->variable = &variables[common_ids[binding->common - 1]];
            if ((restore && !binding->variable) || (!restore && !binding->common)) {
                *link = binding->next;
                free(binding);
            } else link = &binding->next;
        }
    }
}

static int close_channels(void)
{
    unsigned i;
    int failed = 0;
    for (i = 0; i < FILE_MAX; ++i)
        if (release_channel(&channels[i])) failed = 1;
    memset(channels, 0, sizeof(channels));
    return !failed;
}

static void report_run_error(CkbRunResult *report, const char *path,
                             const char *message, int in_editor,
                             unsigned line, unsigned column)
{
    if (report) {
        report->error = message;
        report->editor_source = in_editor;
        report->line = line;
        report->column = column;
    } else if (line)
        fprintf(stderr, "%s:%u:%u: %s\n", path, line, column, message);
    else fprintf(stderr, "%s: %s\n", path, message);
}

static int execute_program(const char *path, int mbf, const char __far *text,
                           unsigned length, CkbRunResult *report, CkbRunPause pause,
                           CkbRunBreak should_break, void *context)
{
    unsigned column, i, saved_count = 0;
    CommonValue *saved = NULL;
    CommonType saved_types[RECORD_MAX];
    char current_path[TEXT_INPUT_MAX + 1];
    int result = 0;
    if (report) memset(report, 0, sizeof(*report));
    if (strlen(path) > TEXT_INPUT_MAX) return 1;
#if defined(__WATCOMC__) || defined(_MSDOS)
    ckb_immediate_clear();
#endif
    pause_context = context;
    aborted = 0;
    strcpy(current_path, path);
    ckenv_init();
    memset(saved_types, 0, sizeof(saved_types));
    restart_kind = RESTART_NONE;
    for (;;) {
        Parser restore_parser;
        current_scope = -1;
        activation = NULL;
        handling_error = 0;
        trapped_error_code = trapped_error_line = 0;
        for (i = 0; i <= PROC_MAX; ++i) error_handler[i] = LINE_MAX;
        procedure_count = procedure_depth = 0;
        option_base = 0;
        dynamic_arrays = 0;
        mbf_mode = mbf;
        random_state = 0x50000UL;
        returning = 0;
        line_count = variable_count = block_count = block_depth = for_depth = call_depth = 0;
        common_count = 0;
        screen_column = printer_column = data_count = data_index = data_offset = 0;
        stopped = 0;
        pause_execution = text ? pause : NULL;
        break_execution = pause_execution ? should_break : NULL;
        error_message = NULL;
        if (text) {
            if (length > SOURCE_MAX) {
                report_run_error(report, current_path, "Source too large", 1, 0, 0);
                result = 1;
                break;
            }
            _fmemcpy(source, text, length);
            source[length] = 0;
            read_lines(length);
        } else if (!load_source(current_path)) { result = 1; break; }
        checking = 1;
        memset(default_kind, '!', sizeof(default_kind));
        if (line_count && !error_message) index_procedures();
        if (line_count) walk_at(position(0, lines[0].body), 0);
        assign_frame_slots();
        checking = 0;
        current_scope = -1;
        if (!error_message && saved) {
            unsigned count = saved_count < common_count ? saved_count : common_count;
            initialize_parser(&restore_parser, position(0, lines[0].body));
            for (i = 0; i < count && !error_message; ++i)
                restore_common(&restore_parser, &saved[i], saved_types, &variables[common_ids[i]]);
        }
        if (!error_message && restart_kind == RESTART_CHAIN) carry_fields(1);
        free_common(saved, saved_count, saved_types);
        saved = NULL;
        saved_count = 0;
        restart_kind = RESTART_NONE;
        if (line_count && !error_message) walk_at(position(0, lines[0].body), 1);
        if (!error_message && restart_kind == RESTART_CHAIN && common_count) {
            Parser save_parser;
            initialize_parser(&save_parser, position(0, lines[0].body));
            saved = calloc(common_count, sizeof(*saved));
            if (!saved) fail(&save_parser, "Out of memory");
            else {
                saved_count = common_count;
                save_common(saved, saved_types);
            }
        }
        if (!error_message && restart_kind == RESTART_CHAIN) carry_fields(0);
        for (i = 0; i < variable_count; ++i) {
            Variable *v = &variables[i];
            release_array(v);
            free(v->text);
            v->text = NULL;
        }
        release_records();
        if (error_message || restart_kind != RESTART_CHAIN) {
            if (!close_channels() && !error_message) {
                report_run_error(report, current_path, "Write error", text != NULL, 0, 0);
                result = 1;
                break;
            }
        }
        if (fflush(stdout) == EOF && !error_message) {
            report_run_error(report, current_path, "Write error", text != NULL, 0, 0);
            result = 1;
            break;
        }
        if (error_message && !aborted) {
            column = first_error.line < line_count ? first_error.offset - lines[first_error.line].start : 0;
            report_run_error(report, current_path, error_message, text != NULL,
                             first_error.line + 1, column + 1);
            result = 1;
        }
        if (aborted || result || !restart_kind) break;
        text = NULL;
        strcpy(current_path, restart_path);
    }
    close_channels();
    free_common(saved, saved_count, saved_types);
    pause_execution = NULL;
    break_execution = NULL;
    pause_context = NULL;
    if (report) report->aborted = aborted;
    return result;
}

int ckb_run(const char *path, int mbf)
{
    return execute_program(path, mbf, NULL, 0, NULL, NULL, NULL, NULL);
}

#if defined(__WATCOMC__) || defined(_MSDOS)
int ckb_run_source(const char *path, const char __far *text, unsigned length,
                   CkbRunResult *report, CkbRunPause pause, CkbRunBreak should_break,
                   void *context)
{
    return execute_program(path, 0, text, length, report, pause, should_break, context);
}
#endif
