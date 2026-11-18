/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "CKBRUNP.H"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#if defined(__WATCOMC__) || defined(_MSDOS)
#include <direct.h>
#include <dos.h>
#include <io.h>
#ifdef __WATCOMC__
#include <i86.h>
#endif
#include <malloc.h>
#include <share.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__WATCOMC__) || defined(_MSDOS)
#define IMMEDIATE_START (SOURCE_MAX + 1)
#define IMM_EXTRA_BYTES (CKB_IMMEDIATE_LIMIT + 4U)
#else
#define IMMEDIATE_START (SOURCE_MAX + 1)
#define IMM_EXTRA_BYTES 260U
#endif

char __far *source;
static char __far *editor_source;
static unsigned editor_source_len;
static char __far *owned_source;
static unsigned lines_cap, blocks_cap, data_cap;
static unsigned imm_line_idx, imm_offset;
Line __far *lines;
static char __far *var_names;
static unsigned long __far *var_shared_scopes;
static unsigned var_cap;
Variable variables[VALUE_MAX];
RecordType records[RECORD_MAX];
unsigned record_count;
static unsigned max_record_end;
static const int *debug_active_flag;
Block __far *blocks;
static unsigned block_stack[STACK_MAX];
ForFrame for_stack[STACK_MAX];
Position calls[STACK_MAX];
unsigned line_count, variable_count, block_count, for_depth, call_depth;
static unsigned block_depth;

unsigned screen_column, printer_column;
Channel channels[FILE_MAX];
static Position __far *data_statements;
static unsigned data_count, data_index;
Position next_position, first_error;
static unsigned data_offset;
const char *error_message;
int stopped, jumped, entering_elseif;
static int checking, indexing_constant;
CkbRunPause pause_execution;
static CkbRunBreak break_execution;
static void *pause_context;
static int aborted;
static char restart_path[TEXT_INPUT_MAX + 1];
static enum { RESTART_NONE, RESTART_RUN, RESTART_CHAIN } restart_kind;
unsigned __far *common_ids;
unsigned common_count;
char default_kind[26];
long option_base;
static int dynamic_arrays;
unsigned long random_state;
int mbf_mode;
int any_field_bound;
static unsigned def_seg_value;
static int has_def_seg;
static int stopped_by_system;
#if defined(__WATCOMC__) || defined(_MSDOS)
static int immediate_output, immediate_idle, immediate_active;
#endif

DefFn __far *def_fns;
unsigned def_fn_count;

Activation *activation;
Procedure __far *procedures;
static unsigned char proc_first_upper[PROC_MAX];
unsigned procedure_count, procedure_depth;
int current_scope, returning;
unsigned error_handler[PROC_MAX + 1];
Position error_resume, error_resume_next;
int handling_error, resume_scope;
unsigned trapped_error_code, trapped_error_line;
int __far *line_scope;

static const struct { unsigned code; const char *text; } runtime_errors[] = {
        {3, "RETURN without GOSUB"}, {4, "Out of DATA"},
        {5, "Illegal function call"}, {6, "Overflow"}, {7, "Out of memory"},
        {9, "Subscript out of range"}, {10, "Duplicate definition"}, {11, "Division by zero"},
        {13, "Type mismatch"}, {14, "String too long"},
        {20, "RESUME without error"}, {28, "Out of stack space"}, {52, "Bad file number"},
        {53, "File not found"}, {54, "Bad file mode"}, {55, "File already open"},
        {57, "Read error"}, {57, "Write error"}, {58, "File already exists"},
        {59, "Bad record length"},
        {62, "Input past end of file"}, {63, "Bad record number"},
        {50, "FIELD overflow"}, {64, "Bad file name"}, {67, "Too many files"},
        {68, "Device unavailable"}, {69, "Communications buffer overflow"},
        {24, "Device timeout"}, {70, "Permission denied"}, {75, "Path/File access error"}, {76, "Path not found"},
};

unsigned runtime_error_code(const char *message)
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

unsigned error_line_number(unsigned index)
{
    int scope;
    if (index >= line_count) return 0;
    scope = line_scope[index];
    do {
        const Line __far *line = &lines[index];
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

void fail(const Parser *p, const char *message)
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

void scan(Parser *p)
{
    const unsigned char __far *s = (const unsigned char __far *)source;
    unsigned pos = p->pos;
    unsigned end = p->line_end;
    unsigned c;
    char *tail;
    while (pos < end && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
    p->start = pos;
    p->length = 0;
    p->token = 0;
    p->first_upper = 0;
    if (pos >= end || s[pos] == '\'') {
        p->pos = pos;
        return;
    }
    c = s[pos++];
    p->token = (int)c;
    if (c == '"') {
        p->token = T_STRING;
        ++p->start;
        while (pos < end && s[pos] != '"') ++pos;
        p->length = pos - p->start;
        if (pos == end) fail(p, "Unterminated string");
        else ++pos;
        p->pos = pos;
        if (p->length > TEXT_INPUT_MAX) fail(p, "String too long");
        return;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        p->token = T_NAME;
        p->first_upper = (unsigned char)(c >= 'a' ? (c - 32U) : c);
        while (pos < end) {
            unsigned d = s[pos];
            if ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') ||
                (d >= '0' && d <= '9') || d == '.' || d == '_') {
                ++pos;
            } else break;
        }
        if (pos < end) {
            unsigned suf = s[pos];
            if (suf == '$' || suf == '%' || suf == '&' || suf == '!' || suf == '#')
                ++pos;
        }
        p->pos = pos;
        p->length = pos - p->start;
        return;
    }
    p->pos = pos;
    if (c == '&') {
        scan_radix(p, end);
    } else if ((c >= '0' && c <= '9') || (c == '.' && pos < end && s[pos] >= '0' && s[pos] <= '9')) {
        char number[50];
        unsigned fraction = c == '.';
        unsigned digits;
        int floating = fraction, double_literal = 0;
        unsigned long int_acc = fraction ? 0UL : (unsigned long)(c - '0');
        while (pos < end && s[pos] >= '0' && s[pos] <= '9') {
            if (!fraction && pos - p->start < 9U)
                int_acc = int_acc * 10UL + (unsigned long)(s[pos] - '0');
            ++pos;
        }
        if (!fraction && pos < end && s[pos] == '.') {
            floating = 1;
            ++pos;
            while (pos < end && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos < end && (s[pos] == 'E' || s[pos] == 'e' || s[pos] == 'D' || s[pos] == 'd')) {
            floating = 1;
            double_literal = s[pos] == 'D' || s[pos] == 'd';
            ++pos;
            if (pos < end && (s[pos] == '+' || s[pos] == '-')) ++pos;
            while (pos < end && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        p->pos = pos;
        p->length = pos - p->start;
        p->token = T_NUMBER;
        /* Fast-path plain 1..9 digit integers without strtod or _fmemcpy. */
        if (!floating && p->length >= 1U && p->length <= 9U) {
            unsigned suf = pos < end ? s[pos] : 0;
            p->number = (double)int_acc;
            if (suf == '%' || suf == '&' || suf == '!' || suf == '#') {
                p->number_kind = (char)suf;
                ++p->pos;
            } else {
                p->number_kind = int_acc <= 32767UL ? '%' : '&';
            }
            return;
        }
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
        if (pos < end) {
            unsigned suf = s[pos];
            if (suf == '%' || suf == '&' || suf == '!' || suf == '#') {
                p->number_kind = (char)suf;
                ++p->pos;
                return;
            }
        }
        if (!floating && p->number <= 32767.0) p->number_kind = '%';
        else if (!floating && p->number <= 2147483647.0) p->number_kind = '&';
        else p->number_kind = double_literal || digits >= 8 ? '#' : '!';
        return;
    } else if (pos < end) {
        unsigned d = s[pos];
        if ((c == '<' && d == '=') || (c == '=' && d == '<')) p->token = T_LE;
        else if ((c == '>' && d == '=') || (c == '=' && d == '>')) p->token = T_GE;
        else if ((c == '<' && d == '>') || (c == '>' && d == '<')) p->token = T_NE;
        if (p->token != (int)c) ++p->pos;
    }
    p->length = p->pos - p->start;
}

int word(const Parser *p, const char *text)
{
    return p->token == T_NAME && p->first_upper == (unsigned char)text[0] &&
           same(source + p->start, p->length, text);
}

int take(Parser *p, int token)
{
    if (p->token != token) return 0;
    scan(p);
    return 1;
}

int take_word(Parser *p, const char *text)
{
    if (!word(p, text)) return 0;
    scan(p);
    return 1;
}

int expect(Parser *p, int token, const char *message)
{
    if (take(p, token)) return 1;
    fail(p, message);
    return 0;
}

int expect_word(Parser *p, const char *text, const char *message)
{
    if (take_word(p, text)) return 1;
    fail(p, message);
    return 0;
}

int end_statement(const Parser *p)
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
    p->line_end = lines[at.line].end;
    scan(p);
}

static int is_type_suffix(int ch)
{
    return ch == '$' || ch == '%' || ch == '&' || ch == '!' || ch == '#';
}

unsigned identifier(Parser *p, char *name, char *kind)
{
    const unsigned char __far *s = (const unsigned char __far *)source + p->start;
    unsigned i, length;
    if (p->token != T_NAME || p->length > BASIC_NAME_MAX) {
        fail(p, "Expected: variable");
        return 0;
    }
    length = p->length;
    *kind = (char)s[length - 1];
    if (is_type_suffix((unsigned char)*kind)) --length;
    else *kind = default_kind[p->first_upper - 'A'];
    for (i = 0; i < length; ++i) {
        unsigned char c = s[i];
        name[i] = (char)((c >= 'a' && c <= 'z') ? (c - 32U) : c);
    }
    name[length] = 0;
    scan(p);
    return length;
}

#define TOK_CACHE_MAX 64U

typedef struct {
    unsigned offset;
    short scope;
    short var_id;
} TokenVarCache;

static TokenVarCache __far *tok_cache;
static unsigned long func_first_mask;
static unsigned long func_len_mask;

static void tok_cache_clear(void)
{
    if (tok_cache) _fmemset(tok_cache, 0xFF, TOK_CACHE_MAX * sizeof(*tok_cache));
}

static void release_base_tables(void)
{
    if (var_names) { _ffree(var_names); var_names = NULL; }
    if (var_shared_scopes) { _ffree(var_shared_scopes); var_shared_scopes = NULL; }
    var_cap = 0;
}

static int ensure_base_tables(void)
{
    if (var_names && var_shared_scopes && var_cap >= VALUE_MAX) return 1;
    if (var_cap == 0) {
        if (!var_names) var_names = (char __far *)_fmalloc((unsigned)VALUE_MAX * (BASIC_NAME_MAX + 1U));
        if (!var_shared_scopes) var_shared_scopes = (unsigned long __far *)_fmalloc((unsigned)VALUE_MAX * 2U * sizeof(unsigned long));
        if (!var_names || !var_shared_scopes) return 0;
        var_cap = VALUE_MAX;
        return 1;
    }
    {
        char __far *nn = (char __far *)_fmalloc((unsigned)VALUE_MAX * (BASIC_NAME_MAX + 1U));
        unsigned long __far *ns = (unsigned long __far *)_fmalloc((unsigned)VALUE_MAX * 2U * sizeof(unsigned long));
        if (!nn || !ns) {
            if (nn) _ffree(nn);
            if (ns) _ffree(ns);
            return 0;
        }
        if (variable_count) {
            _fmemcpy(nn, var_names, variable_count * (BASIC_NAME_MAX + 1U));
            _fmemcpy(ns, var_shared_scopes, variable_count * 2U * sizeof(unsigned long));
        }
        _ffree(var_names);
        _ffree(var_shared_scopes);
        var_names = nn;
        var_shared_scopes = ns;
        var_cap = VALUE_MAX;
        return 1;
    }
}

static void release_exec_tables(void)
{
    if (lines) { _ffree(lines); lines = NULL; }
    if (line_scope) { _ffree(line_scope); line_scope = NULL; }
    if (blocks) { _ffree(blocks); blocks = NULL; }
    if (data_statements) { _ffree(data_statements); data_statements = NULL; }
    if (procedures) { _ffree(procedures); procedures = NULL; }
    if (def_fns) { _ffree(def_fns); def_fns = NULL; }
    if (common_ids) { _ffree(common_ids); common_ids = NULL; }
    if (tok_cache) { _ffree(tok_cache); tok_cache = NULL; }
    lines_cap = blocks_cap = data_cap = 0;
    if (owned_source) { _ffree(owned_source); owned_source = NULL; }
    source = editor_source;
    imm_offset = editor_source ? (editor_source_len + 2U) : 0U;
}

static int ensure_program_tables(unsigned src_len)
{
    unsigned k, nlines = 1U, req_lines, req_blocks, req_data;
    if (!ensure_base_tables()) return 0;
    for (k = 0; k < src_len; ++k) {
        if (source[k] == '\n') ++nlines;
    }
    if (nlines > LINE_MAX) nlines = LINE_MAX;
    req_lines = nlines + 2U;
    req_blocks = nlines * 2U + 32U;
    if (req_blocks > BLOCK_MAX) req_blocks = BLOCK_MAX;
    req_data = nlines + 16U;
    if (req_data > DATA_MAX) req_data = DATA_MAX;

    if (lines) { _ffree(lines); lines = NULL; }
    if (line_scope) { _ffree(line_scope); line_scope = NULL; }
    if (blocks) { _ffree(blocks); blocks = NULL; }
    if (data_statements) { _ffree(data_statements); data_statements = NULL; }
    if (procedures) { _ffree(procedures); procedures = NULL; }
    if (def_fns) { _ffree(def_fns); def_fns = NULL; }
    if (common_ids) { _ffree(common_ids); common_ids = NULL; }

    lines = (Line __far *)_fmalloc(req_lines * sizeof(*lines));
    line_scope = (int __far *)_fmalloc(req_lines * sizeof(*line_scope));
    if (!lines || !line_scope) return 0;
    lines_cap = req_lines;
    imm_line_idx = lines_cap - 1U;

    if (!tok_cache) {
        tok_cache = (TokenVarCache __far *)_fmalloc(TOK_CACHE_MAX * sizeof(*tok_cache));
        if (!tok_cache) return 0;
    }

    blocks = (Block __far *)_fmalloc(req_blocks * sizeof(*blocks));
    if (!blocks) return 0;
    blocks_cap = req_blocks;

    data_statements = (Position __far *)_fmalloc(req_data * sizeof(*data_statements));
    if (!data_statements) return 0;
    data_cap = req_data;
    return 1;
}

static void shrink_program_tables(void)
{
    if (var_names && var_shared_scopes) {
        unsigned keep_vars = variable_count + def_fn_count * PARAM_MAX + 16U;
        if (keep_vars > VALUE_MAX) keep_vars = VALUE_MAX;
        if (keep_vars < var_cap) {
            var_names = (char __far *)ckb_compact_block(var_names, keep_vars * (BASIC_NAME_MAX + 1U));
            var_shared_scopes = (unsigned long __far *)ckb_compact_block(
                var_shared_scopes, keep_vars * 2U * sizeof(unsigned long));
            var_cap = keep_vars;
        }
    }
    if (lines && line_scope) {
        unsigned keep_lines = line_count + 2U;
        if (keep_lines < lines_cap) {
            lines = (Line __far *)ckb_compact_block(lines, keep_lines * sizeof(*lines));
            line_scope = (int __far *)ckb_compact_block(line_scope, keep_lines * sizeof(*line_scope));
            lines_cap = keep_lines;
            imm_line_idx = lines_cap - 1U;
        }
    }
    if (tok_cache) {
        tok_cache = (TokenVarCache __far *)ckb_compact_block(tok_cache, TOK_CACHE_MAX * sizeof(*tok_cache));
    }
    if (blocks) {
        unsigned keep_blocks = block_count + 32U;
        if (keep_blocks < blocks_cap) {
            blocks = (Block __far *)ckb_compact_block(blocks, keep_blocks * sizeof(*blocks));
            blocks_cap = keep_blocks;
        }
    }
    data_statements = (Position __far *)ckb_compact_block(data_statements, data_count * sizeof(*data_statements));
    data_cap = data_count;
    procedures = (Procedure __far *)ckb_compact_block(procedures, procedure_count * sizeof(*procedures));
    def_fns = (DefFn __far *)ckb_compact_block(def_fns, def_fn_count * sizeof(*def_fns));
    common_ids = (unsigned __far *)ckb_compact_block(common_ids, common_count * sizeof(*common_ids));
}

static int ensure_immediate_tables(void)
{
    if ((!var_names || !var_shared_scopes) && !ensure_base_tables()) return 0;
    if (!source) {
        if (editor_source) {
            source = editor_source;
            imm_offset = editor_source_len + 2U;
        } else {
            owned_source = (char __far *)_fmalloc(IMM_EXTRA_BYTES);
            if (!owned_source) return 0;
            source = owned_source;
            imm_offset = 0U;
        }
    } else if (source == editor_source) {
        imm_offset = editor_source_len + 2U;
    }
    if (!lines) {
        lines = (Line __far *)_fmalloc(2U * sizeof(*lines));
        line_scope = (int __far *)_fmalloc(2U * sizeof(*line_scope));
        if (!lines || !line_scope) return 0;
        lines_cap = 2U;
    }
    imm_line_idx = lines_cap - 1U;
    if (!blocks) {
        blocks = (Block __far *)_fmalloc(32U * sizeof(*blocks));
        if (!blocks) return 0;
        blocks_cap = 32U;
    }
    if (!tok_cache) {
        tok_cache = (TokenVarCache __far *)_fmalloc(TOK_CACHE_MAX * sizeof(*tok_cache));
        if (!tok_cache) return 0;
    }
    return 1;
}

static void clear_far_bytes(void __far *dst, unsigned count)
{
    unsigned char __far *p = (unsigned char __far *)dst;
    while (count--) *p++ = 0;
}

#define VAR_HASH_BUCKETS 64U

static short __far var_hash_head[VAR_HASH_BUCKETS];
static short __far var_hash_tail[VAR_HASH_BUCKETS];
static short __far var_hash_next[VALUE_MAX];

static unsigned var_bucket(const char *name)
{
    return (((unsigned)(unsigned char)name[0] * 33U) ^ (unsigned)(unsigned char)name[1]) &
           (VAR_HASH_BUCKETS - 1U);
}

static void var_hash_reset(void)
{
    _fmemset(var_hash_head, 0xFF, sizeof(var_hash_head));
    _fmemset(var_hash_tail, 0xFF, sizeof(var_hash_tail));
}

static void var_hash_link(unsigned slot, unsigned b)
{
    var_hash_next[slot] = -1;
    if (var_hash_head[b] < 0) var_hash_head[b] = (short)slot;
    else var_hash_next[(unsigned)var_hash_tail[b]] = (short)slot;
    var_hash_tail[b] = (short)slot;
}

static void var_hash_rebuild(void)
{
    unsigned i;
    var_hash_reset();
    for (i = 0; i < variable_count; ++i) {
        const char __far *p = var_names + i * (BASIC_NAME_MAX + 1U);
        unsigned b = (((unsigned)(unsigned char)p[0] * 33U) ^ (unsigned)(unsigned char)p[1]) &
                     (VAR_HASH_BUCKETS - 1U);
        var_hash_link(i, b);
    }
}

static void var_name_store(unsigned slot, const char *name)
{
    variables[slot].first_upper = (unsigned char)name[0];
    _fmemcpy(var_names + slot * (BASIC_NAME_MAX + 1U), name, strlen(name) + 1U);
    var_hash_link(slot, var_bucket(name));
}

static int var_name_equals(unsigned slot, const char *name)
{
    const char __far *p = var_names + slot * (BASIC_NAME_MAX + 1U);
    while (*p && *p == *name) { ++p; ++name; }
    return *p == *name;
}

static int var_name_slice(unsigned slot, const char __far *slice, unsigned length)
{
    const char __far *p = var_names + slot * (BASIC_NAME_MAX + 1U);
    unsigned i;
    for (i = 0; i < length; ++i) {
        if (!p[i] || toupper((unsigned char)p[i]) != toupper((unsigned char)slice[i]))
            return 0;
    }
    return p[length] == '\0';
}

static int has_shared_scope(const Variable *v, int scope)
{
    if (scope < 0 || (unsigned)scope >= 64U) return 0;
    return (var_shared_scopes[v->name_slot * 2U + ((unsigned)scope >> 5)] &
            (1UL << ((unsigned)scope & 31U))) != 0UL;
}

static void add_shared_scope(Variable *v, int scope)
{
    if (scope >= 0 && (unsigned)scope < 64U)
        var_shared_scopes[v->name_slot * 2U + ((unsigned)scope >> 5)] |=
            (1UL << ((unsigned)scope & 31U));
}

static int visible_shared(const Variable *v)
{
    return v->shared || has_shared_scope(v, current_scope);
}

static int variable_matches(const Variable *v, const char *name, char kind,
                            int array, int unsuffixed)
{
    return v->first_upper == (unsigned char)name[0] && v->array == array &&
           (v->kind == kind || (unsuffixed && v->typed)) &&
           var_name_equals(v->name_slot, name);
}

/* Resolve without creating an implicit dotted name. A declared record in
 * the current scope, or a shared record, claims its own field syntax. */
static int record_variable(const char __far *name, unsigned length, int array)
{
    unsigned i;
    int shared = -1;
    for (i = 0; i < variable_count; ++i) {
        const Variable *v = &variables[i];
        if (v->array == array && v->typed && var_name_slice(v->name_slot, name, length)) {
            if (v->scope == current_scope) return record_size(v->kind) ? (int)i : -1;
            if (visible_shared(v) && record_size(v->kind)) shared = (int)i;
        }
    }
    return shared;
}

int named_variable(Parser *p, const char *name, char kind, int array,
                          int defining, int unsuffixed, int typed)
{
    unsigned i;
    int idx, local = -1, shared = -1;
    for (idx = var_hash_head[var_bucket(name)]; idx >= 0; idx = var_hash_next[(unsigned)idx]) {
        Variable *v = &variables[idx];
        i = (unsigned)idx;
        if (v->scope != current_scope && v->scope >= 0) continue;
        if (variable_matches(v, name, kind, array, unsuffixed)) {
            if (v->scope == current_scope) {
                if (typed) {
                    if (v->typed && v->kind != kind) {
                        fail(p, "Duplicate definition");
                        return -1;
                    }
                    v->kind = kind;
                    v->typed = (unsigned char)typed;
                }
                if (!unsuffixed || v->typed) return (int)i;
                local = (int)i;
            }
            if (defining && v->scope < 0 && has_shared_scope(v, current_scope)) {
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
    if (variable_count >= var_cap && !ensure_base_tables()) {
        fail(p, "Out of memory");
        return -1;
    }
    i = variable_count++;
    memset(&variables[i], 0, sizeof(variables[i]));
    variables[i].name_slot = i;
    var_shared_scopes[i * 2U] = 0UL;
    var_shared_scopes[i * 2U + 1U] = 0UL;
    var_name_store(i, name);
    variables[i].kind = kind;
    variables[i].scope = (signed char)current_scope;
    variables[i].array = (unsigned char)array;
    variables[i].typed = (unsigned char)typed;
    return (int)i;
}

int unsuffixed_name(Parser *p)
{
    return p->token == T_NAME &&
           !is_type_suffix((unsigned char)source[p->start + p->length - 1]);
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
    int idx, unsuffixed = unsuffixed_name(&copy), shared = -1;
    if (copy.token != T_NAME || !identifier(&copy, name, &kind)) return -1;
    for (idx = var_hash_head[var_bucket(name)]; idx >= 0; idx = var_hash_next[(unsigned)idx]) {
        Variable *v = &variables[idx];
        if (!variable_matches(v, name, kind, 0, unsuffixed)) continue;
        if (v->scope == current_scope) return v->indexed_constant ? idx : -1;
        if (v->indexed_constant && visible_shared(v)) shared = idx;
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
    if (!checking) tok_cache_clear();
}

/* Bind resolved storage, so aliases to scalar FIELD variables see the
 * same record bytes and recursion can keep independent local bindings. */
static FieldBinding **find_field(Channel *file, const Variable *variable)
{
    FieldBinding **binding = &file->fields;
    while (*binding && (*binding)->variable != variable) binding = &(*binding)->next;
    return binding;
}

char *field_data(const Variable *variable, unsigned *width)
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

void unbind_field(const Variable *variable)
{
    unsigned i;
    if (!any_field_bound || variable->kind != '$' || variable->array || variable->fixed_width) return;
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
static void fixed_string_fetch(Parser *p, Value *value, const char __far *item, unsigned width);

void variable_string_store_raw(Parser *p, Variable *v, unsigned cell, const char __far *src, unsigned src_len)
{
    char __far * __far *slot = v->array ? (char __far * __far *)v->elements + cell : &v->text;
    char __far *cur = *slot;
    unsigned length = v->fixed_width ? v->fixed_width : src_len;
    if (any_field_bound) unbind_field(v);
    if (v->fixed_width) {
        unsigned n = src_len > length ? length : src_len;
        if (!cur) {
            if (!length) return;
            cur = ckb_salloc(length);
            if (!cur) {
                fail(p, "Out of memory");
                return;
            }
            *slot = cur;
        }
        if (n) _fmemmove(cur, src, n);
        if (n < length) _fmemset(cur + n, ' ', length - n);
        return;
    }
    if (cur) {
        unsigned cap = (unsigned char)cur[2] | ((unsigned)(unsigned char)cur[3] << 8);
        if (!length && cap > 64U) {
            ckb_sfree(cur);
            *slot = NULL;
            return;
        }
        if (length <= cap && (cap <= 64U || length > 32U)) {
            if (length) _fmemmove(cur + 4, src, length);
            cur[0] = (char)length;
            cur[1] = (char)(length >> 8);
            return;
        }
    }
    if (!length) {
        ckb_sfree(cur);
        *slot = NULL;
        return;
    }
    {
        unsigned cap = length < 32U ? 32U : length;
        char __far *copy = ckb_salloc(cap + 4U);
        if (!copy) {
            fail(p, "Out of memory");
            return;
        }
        copy[0] = (char)length;
        copy[1] = (char)(length >> 8);
        copy[2] = (char)cap;
        copy[3] = (char)(cap >> 8);
        _fmemcpy(copy + 4U, src, length);
        ckb_sfree(cur);
        *slot = copy;
    }
}

static void variable_string_store(Parser *p, Variable *v, unsigned cell, const Value *value)
{
    variable_string_store_raw(p, v, cell, (const char __far *)ckb_string_bytes(value), value->length);
}

static void variable_string_fetch(Parser *p, const Variable *v, unsigned cell, Value *value)
{
    const char __far *stored = v->array ? ((char __far *const __far *)v->elements)[cell] : v->text;
    unsigned width;
    const char *field = any_field_bound ? field_data(v, &width) : NULL;
    if (field) fixed_string_fetch(p, value, (const char __far *)field, width);
    else if (v->fixed_width) fixed_string_fetch(p, value, stored, v->fixed_width);
    else {
        width = stored ? (unsigned char)stored[0] | ((unsigned)(unsigned char)stored[1] << 8) : 0;
        fixed_string_fetch(p, value, stored ? stored + 4 : NULL, width);
    }
}

/* Fixed strings store exactly their declared width, with space padding. */
static void fixed_string_store(char __far *item, unsigned width, const Value *value)
{
    unsigned n = value->length;
    if (n > width) n = width;
    if (n) _fmemcpy(item, (const char __far *)ckb_string_bytes(value), n);
    if (width > n) _fmemset(item + n, ' ', width - n);
}

static void fixed_string_fetch(Parser *p, Value *value, const char __far *item, unsigned width)
{
    char *bytes = ckb_string_buffer(value, width);
    if (!bytes) {
        fail(p, "Out of memory");
        return;
    }
    if (item) _fmemcpy(bytes, item, width);
    else memset(bytes, 0, width);
    bytes[width] = 0;
}

char __far *record_data(Parser *p, Reference ref)
{
    Variable *v = ref.target;
    unsigned size = record_size(v->kind);
    if (!v->elements) {
        v->elements = _fmalloc(size);
        if (!v->elements) {
            fail(p, "Out of memory");
            return NULL;
        }
        _fmemset(v->elements, 0, size);
    }
    return (char __far *)v->elements + ref.cell * size + ref.offset;
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
void packed_number(char *item, char kind, double *number, int store)
{
    if (kind == '!' || kind == '#') {
        if (store) ckb_num_encode(item, *number, kind == '#', 0);
        else ckb_num_decode(number, item, kind == '#' ? 8 : 4, kind == '#', 0);
    } else if (kind == '%') {
        if (store) {
            long lv;
            unsigned u = (unsigned)(try_exact_int21(*number, &lv) ? lv : (long)*number);
            item[0] = (char)u;
            item[1] = (char)(u >> 8);
        } else {
            unsigned u = (unsigned char)item[0] | ((unsigned)(unsigned char)item[1] << 8);
            long sv = (u & 0x8000U) ? ((long)u - 65536L) : (long)u;
            *number = (double)sv;
        }
    } else {
        if (store) {
            long lv;
            unsigned long u = (unsigned long)(try_exact_int21(*number, &lv) ? lv : (long)*number);
            item[0] = (char)u;
            item[1] = (char)(u >> 8);
            item[2] = (char)(u >> 16);
            item[3] = (char)(u >> 24);
        } else {
            unsigned long u = (unsigned long)(unsigned char)item[0] |
                              ((unsigned long)(unsigned char)item[1] << 8) |
                              ((unsigned long)(unsigned char)item[2] << 16) |
                              ((unsigned long)(unsigned char)item[3] << 24);
            long lv = (u & 0x80000000UL) ? (-(long)(~u) - 1L) : (long)u;
            *number = (double)lv;
        }
    }
}

void assign_reference(Parser *p, Reference ref, Value *value, int run)
{
    Variable *v = ref.target;
    unsigned size = record_size(ref.kind);
    char __far *item;
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
        if (size) _fmemmove(item, value->record, size);
        else if (value->string) fixed_string_store(item, ref.width, value);
        else packed_number_far(item, ref.kind, &value->number, 1);
    } else if (value->string) variable_string_store(p, v, ref.cell, value);
    else if (v->array) {
        packed_number_far((char __far *)v->elements + ref.cell * array_element_size(v),
                          ref.kind, &value->number, 1);
    }
    else v->number = value->number;
}

void fetch_reference(Parser *p, Reference ref, int run, Value *value)
{
    const Variable *v = ref.target;
    char __far *item;
    INIT_VALUE(value);
    if (!v) return;
    value->string = ref.kind == '$';
    value->kind = ref.kind;
    if (!run || !valid_cell(p, v, ref.cell)) return;
    if (record_size(v->kind)) {
        item = record_data(p, ref);
        if (!item) return;
        if (record_size(ref.kind)) value->record = item;
        else if (value->string) fixed_string_fetch(p, value, item, ref.width);
        else packed_number_far(item, ref.kind, &value->number, 0);
    } else if (value->string) variable_string_fetch(p, v, ref.cell, value);
    else if (v->array) {
        packed_number_far((char __far *)v->elements + ref.cell * array_element_size(v),
                          ref.kind, &value->number, 0);
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

void string_value(Parser *p, Value *v)
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

long integer_operand(Parser *p, double value, int run)
{
    double n;
    long lv;
    if (!run) return 0;
    if (try_exact_int21(value, &lv)) return lv;
    if (value >= -2147483648.0 && value <= 2147483647.0) {
        lv = (long)value;
        if ((double)lv == value) return lv;
    }
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
        (a->string && op != '+' && rank != 7)) {
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
    if (rank == 7) {
        if (a->string) comparison = ckb_string_compare(a, b);
        else if (try_exact_int21(n, &x) && try_exact_int21(m, &y)) comparison = (x > y) - (x < y);
        else comparison = (n > m) - (n < m);
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
    if (rank <= 5 || op == '\\' || rank == 9)
        result_rank = result_rank == 1 ? 1 : 2;
    result_kind = result_rank == 4 ? '#' : result_rank == 3 ? '!' :
                  result_rank == 2 ? '&' : '%';
    a->kind = result_kind;
    if (!run) return;
    if (result_rank <= 2 && (op == '+' || op == '-' || (op == '*' && result_rank == 1)) &&
        try_exact_int21(n, &x) && try_exact_int21(m, &y)) {
        long lr = op == '+' ? (x + y) : op == '-' ? (x - y) : (x * y);
        if (result_rank == 2 || (lr >= -32768L && lr <= 32767L)) {
            a->number = (double)lr;
            return;
        }
    }
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
        else if (m == 2.0) n = n * n;
        else if (m == 3.0) n = n * n * n;
        else if (m == 1.0) {}
        else if (m == 0.0) n = 1.0;
        else n = pow(n, m);
        break;
    default:
        x = integer_operand(p, n, run);
        y = integer_operand(p, m, run);
        if (rank == 5) n = (double)(x & y);
        else if (rank == 4) n = (double)(x | y);
        else if (rank == 3) n = (double)(x ^ y);
        else if (rank == 2) n = (double)~(x ^ y);
        else if (rank == 1) n = (double)((~x) | y);
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

static int expression_call(Parser *p, Value *v, unsigned depth, int run);


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
    int arg_kind = 1; /* 1: >0, 0: ==0, -1: <0 */
    scan(p);
    if (take(p, '(')) {
        Value parameter;
        expression(p, &parameter, 1, depth + 1, run);
        expect(p, ')', "Expected: )");
        if (parameter.string || record_size(parameter.kind)) fail(p, "Type mismatch");
        else if (run && !error_message) {
            long arg_l;
            if (try_exact_int21(parameter.number, &arg_l) && arg_l >= 0) {
                arg_kind = arg_l > 0 ? 1 : 0;
            } else {
                convert_number(p, &parameter, '!', run);
                argument = parameter.number;
                arg_kind = argument < 0 ? -1 : (argument != 0 ? 1 : 0);
            }
        }
    }
    v->kind = '!';
    if (!run || error_message) return;
    if (arg_kind < 0) {
        unsigned char bytes[4];
        float single = (float)argument;
        record_float((char *)bytes, &single, 4, 1);
        random_state = ((unsigned long)bytes[0] + ((unsigned long)bytes[1] << 8) +
                        ((unsigned long)bytes[2] << 16) + bytes[3]) & 0xffffffUL;
    }
    if (arg_kind != 0)
        random_state = (random_state * 16598013UL + 12820163UL) & 0xffffffUL;
#if defined(_MSDOS) || defined(__WATCOMC__) || defined(__LITTLE_ENDIAN__) || defined(__x86_64__) || defined(__aarch64__)
    if (random_state == 0) {
        v->number = 0.0;
    } else {
        double rn = (double)random_state;
        ((unsigned short *)&rn)[3] -= (unsigned short)(24U << 4);
        v->number = rn;
    }
#else
    v->number = (double)random_state / 16777216.0;
#endif
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
    INIT_VALUE(v);
    if (depth >= DEPTH_MAX) {
        fail(p, "Expression too complex");
        return;
    }
    /* Array subscripts can enter numeric parsing at a fresh expression depth. */
    if (depth > 0 && !stack_room(p, 2048)) return;
    if (run && !checking && !indexing_constant && p->token == T_NAME && p->line < line_count) {
        TokenVarCache __far *slot = &tok_cache[(p->start ^ (p->start >> 6)) & (TOK_CACHE_MAX - 1U)];
        if (slot->offset == p->start && slot->scope == (short)current_scope) {
            int cid = slot->var_id;
            Variable *sv = variable_storage(cid);
            scan(p);
            if (!sv->reference.target && variables[cid].kind != '$') {
                v->kind = variables[cid].kind;
                v->number = sv->number;
                if (reference && !sv->constant) {
                    reference->target = sv;
                    reference->cell = 0;
                    reference->offset = 0;
                    reference->width = 0;
                    reference->kind = v->kind;
                }
            } else {
                Reference cref = reference_at(cid, 0);
                fetch_reference(p, cref, 1, v);
                if (reference && cref.target && !cref.target->constant)
                    *reference = cref;
            }
            goto expr_infix;
        }
    }
    if (p->token == '+' || p->token == '-' ||
        (p->token == T_NAME && p->first_upper == 'N' && word(p, "NOT"))) {
        op = p->token;
        scan(p);
        expression(p, v, op == T_NAME ? 6 : 12, depth + 1, run);
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
    } else if (p->token == T_NAME && p->first_upper == 'E' && take_word(p, "EOF")) {
        Channel *file;
        expect(p, '(', "Expected: (");
        file = file_number(p, run);
        expect(p, ')', "Expected: )");
        if (run && !error_message) v->number = file_eof(p, file) ? -1 : 0;
    } else if (p->token == T_NAME &&
               ((p->first_upper == 'S' && word(p, "SEEK")) ||
                (p->first_upper == 'L' && (word(p, "LOC") || word(p, "LOF"))))) {
        int which = p->first_upper == 'S' ? FILE_SEEK : word(p, "LOC") ? FILE_LOC : FILE_LOF;
        Channel *file;
        scan(p);
        expect(p, '(', "Expected: (");
        file = file_number(p, run);
        expect(p, ')', "Expected: )");
        v->kind = '&';
        if (run && !error_message) v->number = (double)file_position(p, file, which);
    } else if (p->token == T_NAME &&
               ((p->first_upper == 'L' && word(p, "LBOUND")) ||
                (p->first_upper == 'U' && word(p, "UBOUND")))) {
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
expr_infix:
    while (!error_message && (rank = precedence(p)) >= minimum) {
        if (reference) reference->target = NULL;
        op = p->token;
        scan(p);
        expression_rhs(p, v, op, rank, depth, run);
    }
}

void expression(Parser *p, Value *v, int minimum, unsigned depth, int run)
{
    expression_reference(p, v, minimum, depth, run, NULL);
}

double numeric(Parser *p, int run)
{
    Value v;
    expression(p, &v, 1, 0, run);
    if (v.string || record_size(v.kind)) fail(p, "Type mismatch");
    return v.number;
}

void implicit_array(Parser *p, Variable *v, unsigned dimensions)
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

unsigned array_cell(Parser *p, int id, int run)
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

Reference lvalue(Parser *p, int run)
{
    Reference ref;
    const char __far *name;
    const char __far *dot;
    unsigned start_off, after_ident_pos, length, cell;
    int id;
    if (run && !checking && !indexing_constant && p->token == T_NAME && p->line < line_count) {
        TokenVarCache __far *slot = &tok_cache[(p->start ^ (p->start >> 6)) & (TOK_CACHE_MAX - 1U)];
        if (slot->offset == p->start && slot->scope == (short)current_scope) {
            id = slot->var_id;
            scan(p);
            return reference_at(id, 0);
        }
    }
    name = source + p->start;
    dot = p->token == T_NAME ? _fmemchr(name, '.', p->length) : NULL;
    length = p->length;
    id = dot ? record_variable(name, (unsigned)(dot - name), 0) : -1;
    memset(&ref, 0, sizeof(ref));
    if (id >= 0) {
        ref = reference_at(id, 0);
        if (field_path(p, &ref, dot + 1, length - (unsigned)(dot - name) - 1)) scan(p);
        return ref;
    }
    start_off = p->start;
    id = variable(p);
    after_ident_pos = p->pos;
    cell = array_cell(p, id, run);
    if (id < 0 || error_message) return ref;
    ref = reference_at(id, cell);
    if (record_size(ref.kind) && take(p, '.')) {
        /* After a subscript the scanner sees a standalone separator. */
        if (p->token != T_NAME) fail(p, "Element not defined");
        else if (field_path(p, &ref, source + p->start, p->length)) scan(p);
    }
    if (!dot && run && !checking && !indexing_constant && !error_message &&
        p->line < line_count && p->pos == after_ident_pos &&
        !variables[id].array && !record_size(variables[id].kind)) {
        TokenVarCache __far *slot = &tok_cache[(start_off ^ (start_off >> 6)) & (TOK_CACHE_MAX - 1U)];
        slot->offset = start_off;
        slot->scope = (short)current_scope;
        slot->var_id = (short)id;
    }
    return ref;
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
        else if (id >= 0 && variables[id].kind != '$') length = variables[id].number;
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

static char declared_kind_ex(Parser *p, unsigned *width, int allow_any)
{
    unsigned i;
    *width = 0;
    if (allow_any && take_word(p, "ANY")) return '?';
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

static char declared_kind(Parser *p, unsigned *width)
{
    return declared_kind_ex(p, width, 0);
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
            if (line > max_record_end) max_record_end = line;
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
                    if (!common_ids) {
                        common_ids = (unsigned __far *)_fmalloc((unsigned)VALUE_MAX * sizeof(unsigned));
                        if (!common_ids) { fail(p, "Out of memory"); return; }
                    }
                    common_ids[common_count++] = (unsigned)id;
                    v->common = common_count;
                }
                if (global) v->shared = 1;
                if (array && v->dynamic == ARRAY_IMPLICIT) v->dynamic = ARRAY_DYNAMIC;
            } else add_shared_scope(v, scope);
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

unsigned target_label(Parser *p)
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

#define target target_label

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

int pause_at_pos(Position at, const char *message)
{
    Parser p;
    initialize_parser(&p, at);
    while (p.token && p.token != ':' && !word(&p, "ELSE")) scan(&p);
    return pause_at(&p, at, message);
}

unsigned block_at_pos(Position at)
{
    unsigned i;
    for (i = lines[at.line].first_block;
         i < block_count && blocks[i].at.line == at.line; ++i) {
        if (blocks[i].at.offset == at.offset) return i;
    }
    return BLOCK_MAX;
}

int input_interrupted(Parser *p, Position at)
{
    if (!pause_execution) {
        stopped = 1;
        return 0;
    }
    return pause_at(p, at, NULL);
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
        for (i = lines[at.line].first_block;
             i < block_count && blocks[i].at.line == at.line; ++i) {
            if (blocks[i].at.offset == at.offset) return i;
        }
        fail(p, "Invalid block");
        return 0;
    }
    if (block_count >= blocks_cap || block_count == BLOCK_MAX) {
        fail(p, "Too many block statements");
        return 0;
    }
    i = block_count++;
    if (lines[at.line].first_block == BLOCK_MAX) lines[at.line].first_block = i;
    blocks[i].kind = kind;
    blocks[i].at = at;
    blocks[i].after = after_statement(p);
    blocks[i].peer = blocks[i].owner = blocks[i].finish = BLOCK_MAX;
    if (kind == B_FOR || kind == B_DO || kind == B_WHILE || kind == B_IF || kind == B_SELECT) {
        if (kind == B_IF) blocks[i].owner = i;
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
    if (kind == B_ELSEIF || kind == B_ELSE || kind == B_ENDIF) {
        unsigned owner;
        int prev_kind = blocks[previous].kind;
        int ok = (kind == B_ENDIF)
                     ? (prev_kind == B_IF || prev_kind == B_ELSEIF || prev_kind == B_ELSE)
                     : (prev_kind == B_IF || prev_kind == B_ELSEIF);
        if (!ok) {
            fail(p, "Mismatched block statement");
            return i;
        }
        owner = blocks[previous].owner;
        blocks[i].owner = owner;
        blocks[previous].peer = i;
        blocks[i].peer = previous;
        if (kind == B_ENDIF) {
            blocks[owner].finish = i;
            --block_depth;
        } else {
            block_stack[block_depth - 1] = i;
        }
        return i;
    }
    expected = kind == B_NEXT ? B_FOR : kind == B_LOOP ? B_DO : B_WHILE;
    if (blocks[previous].kind != expected) {
        fail(p, "Mismatched block statement");
        return i;
    }
    blocks[previous].peer = i;
    blocks[i].peer = previous;
    --block_depth;
    return i;
}

void write_bytes(Parser *p, FILE *stream, unsigned *column, const char *s, size_t n)
{
#if defined(__WATCOMC__) || defined(_MSDOS)
    if (immediate_active && stream == stdout && n) immediate_output = 1;
#endif
    if (stream == stdout && ckb_con_direct_active()) {
        ckb_con_write(s, (unsigned)n);
        *column = ckb_con_column();
        return;
    }
    if (fwrite(s, 1, n, stream) != n) fail(p, "Write error");
    if (n && s[n - 1] == '\n') *column = 0;
    else *column += n;
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
        if (data_count >= data_cap || data_count == DATA_MAX) fail(p, "Too many DATA statements");
        else data_statements[data_count++] = position(p->line, p->start - (p->token == T_STRING));
    }
    while (!error_message && data_field(p, &datum) && take(p, ',')) {}
}

static void read_value(Parser *p, Reference ref)
{
    Parser field;
    Value datum;
    INIT_VALUE(&datum);
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

static int proc_name_equals(unsigned idx, const char *name)
{
    const char __far *p = procedures[idx].name;
    while (*p && *p == *name) { ++p; ++name; }
    return *p == *name;
}

static void proc_name_store(unsigned idx, const char *name)
{
    proc_first_upper[idx] = (unsigned char)name[0];
    _fmemcpy(procedures[idx].name, name, strlen(name) + 1U);
}

static int procedure_id(const char *name)
{
    unsigned i;
    unsigned char ch0 = (unsigned char)name[0];
    for (i = 0; i < procedure_count; ++i)
        if (proc_first_upper[i] == ch0 && proc_name_equals(i, name)) return (int)i;
    return -1;
}

int lookup_proc_token(const Parser *p)
{
    Parser name_parser;
    char name[BASIC_NAME_MAX + 1], kind;
    unsigned i;
    unsigned char ch0;
    if (!procedure_count || p->token != T_NAME || p->length > BASIC_NAME_MAX) return -1;
    ch0 = p->first_upper;
    for (i = 0; i < procedure_count; ++i)
        if (proc_first_upper[i] == ch0) break;
    if (i == procedure_count) return -1;
    name_parser = *p;
    identifier(&name_parser, name, &kind);
    for (; i < procedure_count; ++i)
        if (proc_first_upper[i] == ch0 && proc_name_equals(i, name)) return (int)i;
    return -1;
}

#define lookup_procedure lookup_proc_token

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

#define LOCAL_VAR_POOL_MAX 8U
#define COPY_VAL_POOL_MAX 2U

static Variable local_var_pool[LOCAL_VAR_POOL_MAX];
static unsigned local_pool_top;

static void procedure_arguments(Parser *p, const Procedure __far *proc, unsigned depth, int run,
                                int parenthesized, int function, Reference *references,
                                Value *copy_slots, unsigned char *has_copy)
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
                COPY_VALUE(&copy_slots[i], &arg);
                has_copy[i] = 1;
            }
        }
        ++i;
    } while (!error_message && take(p, ','));
    if (arglist) expect(p, ')', "Expected: )");
    if (i != proc->count) fail(p, "Wrong number of arguments");
}

static Value copy_val_pool[COPY_VAL_POOL_MAX];
static unsigned copy_pool_top;

static void call_procedure(Parser *p, int id, unsigned depth, int run, Value *result,
                           int parenthesized)
{
    Procedure __far *proc = &procedures[id];
    Value *copy_slots = NULL;
    unsigned char has_copy[PARAM_MAX];
    Reference references[PARAM_MAX];
    Activation frame, *caller = activation;
    unsigned i, n_locals = proc->locals, n_params = proc->count;
    int pooled_locals = 0, pooled_copies = 0;
    int old_scope = current_scope, old_jumped = jumped, old_returning = returning;
    Position old_next = next_position;
    if (result) {
        INIT_VALUE(result);
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
    if (!stack_room(p, 2048)) return;
    memset(has_copy, 0, sizeof(has_copy));
    if (n_params) {
        if (copy_pool_top + n_params <= COPY_VAL_POOL_MAX) {
            copy_slots = &copy_val_pool[copy_pool_top];
            copy_pool_top += n_params;
            pooled_copies = 1;
        } else {
            copy_slots = malloc(n_params * sizeof(*copy_slots));
            if (!copy_slots) {
                fail(p, "Out of memory");
                return;
            }
        }
    }
    procedure_arguments(p, proc, depth, run, parenthesized, result != NULL, references,
                        copy_slots, has_copy);
    if (!run) {
        if (pooled_copies) copy_pool_top -= n_params;
        else free(copy_slots);
        return;
    }
    frame.locals = NULL;
    frame.for_base = for_depth;
    frame.call_base = call_depth;
    if (!error_message && n_locals) {
        if (local_pool_top + n_locals <= LOCAL_VAR_POOL_MAX) {
            frame.locals = &local_var_pool[local_pool_top];
            local_pool_top += n_locals;
            pooled_locals = 1;
        } else {
            frame.locals = malloc(n_locals * sizeof(*frame.locals));
            if (!frame.locals) fail(p, "Out of memory");
        }
    }
    if (!error_message) {
        unsigned fv = proc->first_var, lv = proc->last_var;
        for (i = fv; i < lv; ++i) {
            const Variable *v = &variables[i];
            if (v->scope == id && v->frame_slot)
                frame.locals[v->frame_slot - 1] = *v;
        }
        activation = &frame;
        frame.prev = caller;
        current_scope = id;
        for (i = 0; i < n_params && !error_message; ++i) {
            int param = proc->params[i];
            if (has_copy[i]) {
                assign(p, param, &copy_slots[i], 1);
                if (!references[i].target) has_copy[i] = 0;
            } else variable_storage(param)->reference = references[i];
        }
    }
    if (pooled_copies) copy_pool_top -= n_params;
    else free(copy_slots);
    /* The next statement needs stack space before reaching expression checks. */
    if (!error_message && stack_room(p, 2048)) {
        ++procedure_depth;
        walk_at(next_line(proc->line), 1);
        --procedure_depth;
        if (result && !error_message) fetch(p, proc->result, 0, 1, result);
    }
    /* Fixed actuals passed to variable-length formals use independent
     * descriptors; QBasic copies them back in reverse argument order. */
    if (frame.locals && !error_message) {
        for (i = n_params; i > 0; --i) {
            unsigned n = i - 1;
            if (has_copy[n] && references[n].target && references[n].width) {
                Value wb;
                fetch(p, proc->params[n], 0, 1, &wb);
                if (!error_message) assign_reference(p, references[n], &wb, 1);
            }
        }
    }
    if (frame.locals) {
        for (i = 0; i < n_locals; ++i) {
            unbind_field(&frame.locals[i]);
            release_array(&frame.locals[i]);
            ckb_sfree(frame.locals[i].text);
        }
        if (pooled_locals) local_pool_top -= n_locals;
        else free(frame.locals);
    }
    activation = caller;
    current_scope = old_scope;
    returning = old_returning;
    jumped = old_jumped;
    next_position = old_next;
    for_depth = frame.for_base;
    call_depth = frame.call_base;
}


int alloc_proc_locals(Parser *p, int proc_id, Variable **out_locals, int *out_pooled)
{
    Procedure __far *proc = &procedures[proc_id];
    unsigned n_locals = proc->locals, fv = proc->first_var, lv = proc->last_var, i;
    Variable *locs = NULL;
    *out_locals = NULL;
    *out_pooled = 0;
    if (n_locals) {
        if (local_pool_top + n_locals <= LOCAL_VAR_POOL_MAX) {
            locs = &local_var_pool[local_pool_top];
            local_pool_top += n_locals;
            *out_pooled = 1;
        } else {
            locs = malloc(n_locals * sizeof(*locs));
            if (!locs) {
                fail(p, "Out of memory");
                return 0;
            }
        }
        for (i = fv; i < lv; ++i) {
            const Variable *v = &variables[i];
            if (v->scope == proc_id && v->frame_slot)
                locs[v->frame_slot - 1] = *v;
        }
    }
    *out_locals = locs;
    return 1;
}

void free_proc_locals(Variable *locals, unsigned n_locals, int pooled)
{
    unsigned i;
    if (!locals) return;
    for (i = 0; i < n_locals; ++i) {
        if (any_field_bound) unbind_field(&locals[i]);
        if (locals[i].elements) release_array(&locals[i]);
        if (locals[i].text) ckb_sfree(locals[i].text);
    }
    if (pooled) local_pool_top -= n_locals;
    else free(locals);
}

unsigned current_def_seg(void)
{
#if defined(__WATCOMC__) || defined(_MSDOS)
    if (has_def_seg) return def_seg_value;
    {
        unsigned char near_dummy = 0;
        return FP_SEG((void __far *)&near_dummy);
    }
#else
    return def_seg_value;
#endif
}

static void call_def_fn(Parser *p, int fn_id, Value *v, unsigned depth, int run)
{
    DefFn __far *fn = &def_fns[fn_id];
    unsigned pcount = fn->param_count;
    char rkind = fn->result_kind;
    Value args[PARAM_MAX];
    double saved_nums[PARAM_MAX];
    char __far *saved_texts[PARAM_MAX];
    int param_ids[PARAM_MAX];
    unsigned i;
    int saved_scope = current_scope;
    scan(p);
    if (pcount) {
        if (!expect(p, '(', "Expected: (")) return;
        for (i = 0; i < pcount && !error_message; ++i) {
            if (i > 0 && !expect(p, ',', "Expected: ,")) return;
            expression(p, &args[i], 1, depth + 1, run);
            if (!error_message && args[i].string != (fn->param_kinds[i] == '$'))
                fail(p, "Type mismatch");
        }
        expect(p, ')', "Expected: )");
    } else if (take(p, '(')) {
        expect(p, ')', "Expected: )");
    }
    INIT_VALUE(v);
    v->kind = rkind;
    v->string = rkind == '$';
    if (error_message) return;
    current_scope = -1;
    for (i = 0; i < pcount && !error_message; ++i) {
        int pid = fn->param_var_ids[i];
        if (pid < 0) {
            char pname[16];
            _fmemcpy(pname, fn->param_names[i], 16);
            pid = named_variable(p, pname, fn->param_kinds[i], 0, 0, 0, 0);
            fn->param_var_ids[i] = pid;
        }
        param_ids[i] = pid;
        if (pid >= 0 && run) {
            saved_nums[i] = variables[pid].number;
            saved_texts[i] = variables[pid].text;
            variables[pid].text = NULL;
            assign(p, pid, &args[i], 1);
        }
    }
    if (!error_message) {
        Parser fn_p;
        initialize_parser(&fn_p, fn->expr_at);
        expression(&fn_p, v, 1, depth + 1, run);
        if (!error_message) {
            if (v->string != (rkind == '$')) fail(p, "Type mismatch");
            else if (!v->string) convert_number(p, v, rkind, run);
        }
    }
    if (run) {
        for (i = 0; i < pcount; ++i) {
            int pid = param_ids[i];
            if (pid >= 0) {
                ckb_sfree(variables[pid].text);
                variables[pid].text = saved_texts[i];
                variables[pid].number = saved_nums[i];
            }
        }
    }
    current_scope = saved_scope;
}

int lookup_deffn_token(const Parser *p)
{
    Parser np = *p;
    char name[BASIC_NAME_MAX + 1], kind;
    unsigned i;
    if (p->token != T_NAME || p->length < 2 || p->length > BASIC_NAME_MAX) return -1;
    if (toupper((unsigned char)source[p->start]) != 'F' ||
        toupper((unsigned char)source[p->start + 1]) != 'N')
        return -1;
    identifier(&np, name, &kind);
    for (i = 0; i < def_fn_count; ++i) {
        const char __far *fp = def_fns[i].name;
        const char *np2 = name;
        while (*fp && *fp == *np2) { ++fp; ++np2; }
        if (*fp == *np2) return (int)i;
    }
    return -1;
}

#define lookup_def_fn lookup_deffn_token

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
    switch (p->first_upper) {
    case 'C':
        if (take_word(p, "CSRLIN")) {
            v->kind = '%';
            if (run) v->number = ckb_csrlin();
            return 1;
        }
        if (take_word(p, "COMMAND$")) {
            v->kind = '$';
            v->string = 1;
            v->length = 0;
            v->text[0] = '\0';
            return 1;
        }
        break;
    case 'D':
        if (take_word(p, "DATE$")) {
            v->kind = '$';
            v->string = 1;
            if (run) {
#if defined(__WATCOMC__) || defined(_MSDOS)
                union REGS regs = {0};
                regs.h.ah = 0x2a;
                intdos(&regs, &regs);
                sprintf(v->text, "%02u-%02u-%04u",
                        (unsigned)regs.h.dh, (unsigned)regs.h.dl, (unsigned)regs.x.cx);
                v->length = 10U;
#else
                strcpy(v->text, "01-01-1991");
                v->length = 10U;
#endif
            }
            return 1;
        }
        break;
    case 'E':
        if (error_value(p, v, run)) return 1;
        if (take_word(p, "ENVIRON$")) {
            environment_function(p, v, depth, run);
            return 1;
        }
        break;
    case 'I':
        if (take_word(p, "INPUT$")) {
            input_string(p, v, depth, run);
            return 1;
        }
        if (take_word(p, "INKEY$")) {
            v->kind = '$';
            v->string = 1;
            if (run) {
                v->length = (unsigned)ckb_inkey(v->text);
                v->text[v->length] = 0;
            }
            return 1;
        }
        if (take_word(p, "INP")) {
            inp_function(p, v, run);
            return 1;
        }
        break;
    case 'F':
        if (take_word(p, "FREEFILE")) {
            v->kind = '%';
            if (run) {
                for (i = 0; i < FILE_MAX && file_open(&channels[i]); ++i) {}
                if (i == FILE_MAX) fail(p, "Too many files");
                else v->number = i + 1;
            }
            return 1;
        }
        if (take_word(p, "FRE")) {
            Value arg;
            v->kind = '&';
            if (expect(p, '(', "Expected: (")) {
                expression(p, &arg, 1, depth + 1, run);
                expect(p, ')', "Expected: )");
                if (run && !error_message) {
                    long code = arg.string ? 0L : integer_operand(p, arg.number, 1);
                    if (!error_message)
                        v->number = (double)ckb_fre(arg.string, code);
                }
            }
            return 1;
        }
        if (def_fn_count) {
            id = lookup_def_fn(p);
            if (id >= 0) {
                call_def_fn(p, id, v, depth, run);
                return 1;
            }
        }
        break;
    case 'T':
        if (take_word(p, "TIMER")) {
            v->kind = '!';
            if (run) v->number = ckb_timer();
            return 1;
        }
        if (take_word(p, "TIME$")) {
            v->kind = '$';
            v->string = 1;
            if (run) {
#if defined(__WATCOMC__) || defined(_MSDOS)
                union REGS regs = {0};
                regs.h.ah = 0x2c;
                intdos(&regs, &regs);
                sprintf(v->text, "%02u:%02u:%02u",
                        (unsigned)regs.h.ch, (unsigned)regs.h.cl, (unsigned)regs.h.dh);
                v->length = 8U;
#else
                strcpy(v->text, "00:00:00");
                v->length = 8U;
#endif
            }
            return 1;
        }
        break;
    case 'P':
        if (take_word(p, "PEEK")) {
            peek_function(p, v, run, current_def_seg());
            return 1;
        }
        if (take_word(p, "POINT")) {
            point_function(p, v, run);
            return 1;
        }
        if (take_word(p, "PMAP")) {
            pmap_function(p, v, run);
            return 1;
        }
        if (take_word(p, "POS")) {
            expect(p, '(', "Expected: (");
            (void)numeric(p, run);
            expect(p, ')', "Expected: )");
            v->kind = '%';
            if (run && !error_message) v->number = ckb_pos_col();
            return 1;
        }
        if (take_word(p, "PLAY")) {
            expect(p, '(', "Expected: (");
            (void)numeric(p, run);
            expect(p, ')', "Expected: )");
            v->kind = '%';
            if (run && !error_message) v->number = ckb_play_queued();
            return 1;
        }
        break;
    case 'R':
        if (word(p, "RND")) {
            random_value(p, v, depth, run);
            return 1;
        }
        break;
    case 'S':
        if (take_word(p, "SCREEN")) {
            screen_function(p, v, run);
            return 1;
        }
        if (take_word(p, "SADD")) {
            sadd_function(p, v, run, depth);
            return 1;
        }
        break;
    case 'V':
        if (take_word(p, "VARPTR$")) {
            varptr_str_function(p, v, run);
            return 1;
        }
        if (take_word(p, "VARPTR")) {
            varptr_function(p, v, run);
            return 1;
        }
        if (take_word(p, "VARSEG")) {
            varseg_function(p, v, run);
            return 1;
        }
        break;
    default:
        break;
    }
    if (p->length <= BASIC_NAME_MAX) {
        /* All 44 string and numeric intrinsics here require '(' after the name;
         * peeking ahead avoids scanning 44 function names on every scalar variable! */
        unsigned look = p->pos;
        while (look < p->line_end && (source[look] == ' ' || source[look] == '\t')) ++look;
        if (look < p->line_end && source[look] == '(') {
            unsigned char ch0 = p->first_upper;
            for (i = 0; i < p->length; ++i) {
                unsigned char c = (unsigned char)source[p->start + i];
                name[i] = (char)((c >= 'a' && c <= 'z') ? (c - 32U) : c);
            }
            name[i] = 0;
            id = ckb_string_function(name);
            for (i = 0; !id && i < sizeof(numeric_functions) / sizeof(numeric_functions[0]); ++i)
                if ((unsigned char)numeric_functions[i][0] == ch0 &&
                    strcmp(name, numeric_functions[i]) == 0)
                    id = CF_CINT - (int)i;
            if (id) {
                int result = builtin(p, v, depth, run, id, !strcmp(name, "LEN"));
                if (!strcmp(name, "ASC") || !strcmp(name, "INSTR") || !strcmp(name, "LEN"))
                    v->kind = '%';
                return result;
            }
        }
    }
    if (!func_first_mask || p->token != T_NAME || p->length > BASIC_NAME_MAX) return 0;
    if (!(func_first_mask & (1UL << (p->first_upper - 'A')))) return 0;
    if (!(func_len_mask & (1UL << (p->length < 31U ? p->length : 31U)))) return 0;
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

static void check_signature(Parser *p, const ParameterType __far *a, unsigned a_count,
                            const ParameterType __far *b, unsigned b_count)
{
    unsigned i;
    if (a_count != b_count) {
        fail(p, "Argument-count mismatch");
        return;
    }
    for (i = 0; i < a_count; ++i) {
        int any_type = (a[i].kind == '?' || b[i].kind == '?');
        if (a[i].array != b[i].array ||
            (!any_type && (a[i].kind != b[i].kind || a[i].width != b[i].width))) {
            fail(p, "Parameter type mismatch");
            return;
        }
    }
}

static void procedure_header(Parser *p, Procedure __far *proc, const char *name, int declaration)
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
            if (typed) kind = declared_kind_ex(p, &width, declaration);
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
        _fmemcpy(proc->declared_params, signature, count * sizeof(*signature));
    }
    if (declaration || error_message) return;
    proc->count = count;
    proc->persistent = take_word(p, "STATIC");
    if (proc->function) {
        for (i = 0; i < (int)variable_count; ++i)
            if (variables[i].scope == current_scope && var_name_equals(variables[i].name_slot, name) &&
                variables[i].kind == proc->result_kind) break;
        if (i == (int)variable_count) {
            if (variable_count == VALUE_MAX) {
                fail(p, "Too many variables");
                return;
            }
            ++variable_count;
            memset(&variables[i], 0, sizeof(variables[i]));
            variables[i].name_slot = (unsigned)i;
            var_shared_scopes[(unsigned)i * 2U] = 0UL;
            var_shared_scopes[(unsigned)i * 2U + 1U] = 0UL;
            var_name_store((unsigned)i, name);
            variables[i].scope = (signed char)current_scope;
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
        variables[id].shared = current_scope < 0;
    } while (!error_message && take(p, ','));
}

static void index_def_fn(Parser *p, unsigned line)
{
    DefFn fn_local;
    DefFn *fn = &fn_local;
    char name[BASIC_NAME_MAX + 1], kind;
    if (word(p, "SEG")) return;
    if (current_scope >= 0) {
        fail(p, "DEF FN not allowed in procedure");
        return;
    }
    if (!identifier(p, name, &kind)) return;
    if (toupper((unsigned char)name[0]) != 'F' || toupper((unsigned char)name[1]) != 'N') {
        fail(p, "Expected: FN name");
        return;
    }
    if (def_fn_count == DEF_FN_MAX) {
        fail(p, "Too many DEF FN");
        return;
    }
    memset(fn, 0, sizeof(*fn));
    {
        unsigned pi;
        for (pi = 0; pi < PARAM_MAX; ++pi) fn->param_var_ids[pi] = -1;
    }
    strcpy(fn->name, name);
    fn->result_kind = kind;
    if (take(p, '(')) {
        if (p->token != ')') do {
            char pname[BASIC_NAME_MAX + 1], pkind;
            if (fn->param_count == PARAM_MAX) {
                fail(p, "Too many parameters");
                return;
            }
            if (!identifier(p, pname, &pkind)) return;
            strncpy(fn->param_names[fn->param_count], pname, 15);
            fn->param_names[fn->param_count][15] = '\0';
            fn->param_kinds[fn->param_count] = pkind;
            ++fn->param_count;
        } while (!error_message && take(p, ','));
        expect(p, ')', "Expected: )");
    }
    if (!expect(p, '=', "Expected: =")) return;
    if (!def_fns) {
        def_fns = (DefFn __far *)_fmalloc(DEF_FN_MAX * sizeof(*def_fns));
        if (!def_fns) { fail(p, "Out of memory"); return; }
    }
    fn->expr_at = position(line, p->start);
    _fmemcpy(&def_fns[def_fn_count++], fn, sizeof(*fn));
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
        else if (take_word(&p, "DEF")) index_def_fn(&p, line);
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
                if (!procedures) {
                    procedures = (Procedure __far *)_fmalloc(PROC_MAX * sizeof(*procedures));
                    if (!procedures) {
                        fail(&p, "Out of memory");
                        continue;
                    }
                }
                id = (int)procedure_count++;
                clear_far_bytes(&procedures[id], sizeof(procedures[id]));
                proc_name_store((unsigned)id, name);
                procedures[id].result_kind = kind;
                procedures[id].function = function;
                procedures[id].line = procedures[id].end = LINE_MAX;
                if (function) {
                    unsigned nlen = (unsigned)strlen(name);
                    func_first_mask |= 1UL << ((unsigned char)name[0] - 'A');
                    func_len_mask |= 1UL << (nlen < 31U ? nlen : 31U);
                    func_len_mask |= 1UL << ((nlen + 1U) < 31U ? (nlen + 1U) : 31U);
                }
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
    for (i = 0; i < procedure_count; ++i) {
        procedures[i].first_var = variable_count;
        procedures[i].last_var = 0;
    }
    for (i = 0; i < variable_count; ++i) {
        Variable *v = &variables[i];
        Procedure __far *proc;
        unsigned param;
        if (v->scope < 0 || v->shared) continue;
        proc = &procedures[v->scope];
        if (i < proc->first_var) proc->first_var = i;
        if (i + 1U > proc->last_var) proc->last_var = i + 1U;
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
        if (take_word(p, "IS")) {
            int op = p->token;
            if (op != '=' && op != '<' && op != '>' && op != T_LE && op != T_GE && op != T_NE) {
                fail(p, "Expected: relational operator");
                return 0;
            }
            scan(p);
            expression(p, value, 8, 0, run);
            if (record_size(value->kind)) fail(p, "Type mismatch");
            if (selected && !error_message) {
                if (value->string != selected->string) fail(p, "Type mismatch");
                else {
                    int cmp = value->string
                                  ? ckb_string_compare(selected, value)
                                  : (selected->number > value->number) - (selected->number < value->number);
                    int ok = (op == '=') ? (cmp == 0) : (op == '<') ? (cmp < 0) :
                             (op == '>') ? (cmp > 0) : (op == T_LE) ? (cmp <= 0) :
                             (op == T_GE) ? (cmp >= 0) : (cmp != 0);
                    if (ok) match = 1;
                }
            }
        } else {
            expression(p, value, 1, 0, run);
            if (record_size(value->kind)) fail(p, "Type mismatch");
            if (take_word(p, "TO")) {
                Value high;
                expression(p, &high, 1, 0, run);
                if (record_size(high.kind) || high.string != value->string)
                    fail(p, "Type mismatch");
                if (selected && !error_message) {
                    if (value->string != selected->string) fail(p, "Type mismatch");
                    else if (value->string
                                 ? (ckb_string_compare(selected, value) >= 0 &&
                                    ckb_string_compare(selected, &high) <= 0)
                                 : (selected->number >= value->number &&
                                    selected->number <= high.number))
                        match = 1;
                }
            } else if (selected && !error_message) {
                if (value->string != selected->string) fail(p, "Type mismatch");
                else if (value->string ? ckb_string_compare(value, selected) == 0 :
                         value->number == selected->number) match = 1;
            }
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

static int is_legacy_cmd(Parser *p, const char *kw)
{
    unsigned pos;
    if (!word(p, kw)) return 0;
    pos = p->pos;
    while (pos < p->line_end && (source[pos] == ' ' || source[pos] == '\t')) ++pos;
    if (pos < p->line_end && (source[pos] == '=' || source[pos] == '(' || source[pos] == '.'))
        return 0;
    if (procedure_count && lookup_procedure(p) >= 0) return 0;
    fail(p, "Syntax error");
    return 1;
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
        INIT_VALUE(&v);
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
            if (variables[frame->variable].kind == '%') {
                Variable *fv = variable_storage(frame->variable);
                unsigned fw;
                long cur, stp, lst;
                if (!fv->reference.target && !field_data(fv, &fw) &&
                    try_exact_int21(fv->number, &cur) &&
                    try_exact_int21(frame->step, &stp)) {
                    long nxt = cur + stp;
                    if (nxt >= -32768L && nxt <= 32767L) {
                        int keep_going;
                        fv->number = (double)nxt;
                        if (try_exact_int21(frame->last, &lst))
                            keep_going = stp >= 0 ? (nxt <= lst) : (nxt >= lst);
                        else
                            keep_going = stp >= 0 ? (fv->number <= frame->last) : (fv->number >= frame->last);
                        if (keep_going) go(blocks[peer].after);
                        else --for_depth;
                        return;
                    }
                }
            }
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
    int explicit_call = (p->token == T_NAME && p->first_upper == 'C') ? take_word(p, "CALL") : 0;
    int id;
    if (explicit_call && take_word(p, "ABSOLUTE")) {
        call_absolute_statement(p, run);
        return 1;
    }
    if (!explicit_call) {
        unsigned pos;
        if (!procedure_count || p->token != T_NAME) return 0;
        pos = p->pos;
        while (pos < p->line_end && (source[pos] == ' ' || source[pos] == '\t')) ++pos;
        if (pos < p->line_end && source[pos] == '=') return 0;
    }
    id = lookup_procedure(p);
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
    initialize_parser(&p, position(imm_line_idx, imm_offset));
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
    if (immediate_idle) {
        for (i = 0; i < variable_count; ++i) {
            release_array(&variables[i]);
            ckb_sfree(variables[i].text);
            variables[i].text = NULL;
        }
        ckb_sreset();
        variable_count = 0;
        var_hash_reset();
        immediate_idle = 0;
    }
    release_base_tables();
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
    if (!ensure_immediate_tables()) {
        result->error = "Out of memory";
        return;
    }
    if (!saved_pause && !immediate_idle) {
        immediate_idle = 1;
        variable_count = procedure_count = line_count = 0;
        var_hash_reset();
        current_scope = -1;
        option_base = dynamic_arrays = mbf_mode = 0;
        random_state = 0x50000UL;
        trapped_error_code = trapped_error_line = 0;
        memset(default_kind, '!', sizeof(default_kind));
    }
    variables_before = variable_count;
    _fmemcpy(source + imm_offset, command, length + 1);
    lines[imm_line_idx].start = lines[imm_line_idx].body = imm_offset;
    lines[imm_line_idx].end = imm_offset + length;
    lines[imm_line_idx].first_block = BLOCK_MAX;
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
    } else {
        variable_count = variables_before;
        var_hash_rebuild();
    }
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

static void clear_statement(Parser *p, int run)
{
    unsigned extra, i;
    for (extra = 0; extra < 3 && take(p, ','); ++extra) {
        if (p->token != ',' && !end_statement(p))
            (void)integer_operand(p, numeric(p, run), run);
    }
    if (!run || error_message) return;
    close_channels();
    reset_varptr_shadows();
    ckb_speaker_off();
    for (i = 0; i < variable_count; ++i) {
        Variable *v = &variables[i];
        if (v->array) {
            if (v->dynamic == ARRAY_DYNAMIC) release_array(v);
            else clear_array(v);
        } else {
            v->number = 0.0;
            v->ival = 0L;
            if (v->text) {
                if (v->fixed_width) _fmemset(v->text + 2, ' ', v->fixed_width);
                else { ckb_sfree(v->text); v->text = NULL; }
            }
        }
    }
    data_index = data_offset = 0;
    for_depth = call_depth = 0;
    has_def_seg = 0;
    def_seg_value = 0;
}

static void run_or_chain_statement(Parser *p, int run, int chain)
{
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
}

static void statement(Parser *p, int run, unsigned inline_depth)
{
    Position at = position(p->line, p->start);
    unsigned index, line, body;
    int kind, condition, handled = 0;
    if (checking) array_directive(p);
    if (!p->token) return;
    if (p->token == T_NAME && (p->first_upper == 'R' || p->first_upper == 'D') &&
        (word(p, "REM") || word(p, "DECLARE"))) {
        p->token = 0;
        p->pos = lines[p->line].end;
        return;
    }
    if (run && !error_message && break_execution &&
        (!debug_active_flag || *debug_active_flag)) {
        body = lines[at.line].body;
        while (body < at.offset && isspace((unsigned char)source[body])) ++body;
        if (break_execution(pause_context, lines[at.line].start,
                            !inline_depth && body == at.offset, procedure_depth) &&
            !before_statement(p, at)) return;
    }
    switch (p->token == '?' ? '?' : (p->token == T_NAME ? (int)p->first_upper : 0)) {
    case '?':
        take(p, '?');
        print_statement(p, run, 0);
        handled = 1;
        break;
    case 'A':
        if (is_legacy_cmd(p, "AUTO")) handled = 1;
        break;
    case 'B':
        if (take_word(p, "BEEP")) {
            if (run) ckb_beep();
            handled = 1;
        } else if (take_word(p, "BSAVE")) {
            bsave_statement(p, run);
            handled = 1;
        } else if (take_word(p, "BLOAD")) {
            bload_statement(p, run);
            handled = 1;
        }
        break;
    case 'C':
        if (take_word(p, "CASE")) {
            case_statement(p, at, run, inline_depth);
            handled = 1;
        } else if (take_word(p, "CIRCLE")) {
            gfx_circle_statement(p, run);
            handled = 1;
        } else if (take_word(p, "CLS")) {
            cls_statement(p, run);
            handled = 1;
        } else if (take_word(p, "COLOR")) {
            color_statement(p, run);
            handled = 1;
        } else if (take_word(p, "CONST")) {
            const_statement(p, run);
            handled = 1;
        } else if (take_word(p, "COMMON")) {
            storage_statement(p, 1);
            handled = 1;
        } else if (take_word(p, "CLOSE")) {
            close_statement(p, run);
            handled = 1;
        } else if (take_word(p, "CHDIR")) {
            path_statement(p, run, CHANGE_DIRECTORY);
            handled = 1;
        } else if (word(p, "CHAIN")) {
            run_or_chain_statement(p, run, 1);
            handled = 1;
        } else if (take_word(p, "CLEAR")) {
            clear_statement(p, run);
            handled = 1;
        } else if (is_legacy_cmd(p, "CONT")) {
            handled = 1;
        }
        break;
    case 'D':
        if (take_word(p, "DO")) {
            condition = loop_passes(p, run, &kind);
            index = block(p, at, B_DO, inline_depth);
            if (run && !condition && !error_message) go(blocks[blocks[index].peer].after);
            handled = 1;
        } else if (take_word(p, "DIM")) {
            dim_statement(p, run, 0, 0);
            handled = 1;
        } else if (take_word(p, "DATA")) {
            data_statement(p);
            handled = 1;
        } else if (take_word(p, "DRAW")) {
            draw_statement(p, run);
            handled = 1;
        } else if (is_legacy_cmd(p, "DELETE")) {
            handled = 1;
        } else if (take_word(p, "DEFINT")) {
            default_type_statement(p, '%');
            handled = 1;
        } else if (take_word(p, "DEFLNG")) {
            default_type_statement(p, '&');
            handled = 1;
        } else if (take_word(p, "DEFSNG")) {
            default_type_statement(p, '!');
            handled = 1;
        } else if (take_word(p, "DEFDBL")) {
            default_type_statement(p, '#');
            handled = 1;
        } else if (take_word(p, "DEFSTR")) {
            default_type_statement(p, '$');
            handled = 1;
        } else if (take_word(p, "DEF")) {
            if (take_word(p, "SEG")) {
                if (take(p, '=')) {
                    long seg = integer_operand(p, numeric(p, run), run);
                    if (run && !error_message) {
                        def_seg_value = (unsigned)((unsigned long)seg & 0xffffUL);
                        has_def_seg = 1;
                    }
                } else if (run) has_def_seg = 0;
            } else if (p->token == T_NAME && p->length >= 3 &&
                       same(source + p->start, 3, "USR") &&
                       (p->length == 3 ||
                        (p->length == 4 && isdigit((unsigned char)source[p->start + 3])))) {
                fail(p, "Syntax error");
            } else {
                p->token = 0;
                p->pos = lines[p->line].end;
            }
            handled = 1;
        }
        break;
    case 'E':
        if (take_word(p, "END")) {
            if (take_word(p, "IF")) block(p, at, B_ENDIF, inline_depth);
            else if (take_word(p, "SELECT")) block(p, at, B_ENDSELECT, inline_depth);
            else if (take_word(p, "SUB") || take_word(p, "FUNCTION")) {
                if (run) returning = 1;
            } else if (run) stopped = 1;
            handled = 1;
        } else if (take_word(p, "ELSEIF")) {
            if (!run) {
                condition = numeric(p, 0) != 0;
                if (expect_word(p, "THEN", "Expected: THEN") && !error_message)
                    block(p, at, B_ELSEIF, inline_depth);
            } else if (entering_elseif) {
                entering_elseif = 0;
                condition = numeric(p, 1) != 0;
                if (expect_word(p, "THEN", "Expected: THEN") && !error_message) {
                    index = block(p, at, B_ELSEIF, inline_depth);
                    if (!condition && !error_message) {
                        unsigned peer = blocks[index].peer;
                        if (blocks[peer].kind == B_ELSEIF) {
                            entering_elseif = 1;
                            go(blocks[peer].at);
                        } else {
                            go(blocks[peer].after);
                        }
                    }
                }
            } else {
                p->token = 0;
                p->pos = lines[p->line].end;
                index = block(p, at, B_ELSEIF, inline_depth);
                if (!error_message) go(blocks[blocks[blocks[index].owner].finish].after);
            }
            handled = 1;
        } else if (take_word(p, "ELSE")) {
            index = block(p, at, B_ELSE, inline_depth);
            if (run && !error_message) go(blocks[blocks[blocks[index].owner].finish].after);
            handled = 1;
        } else if (take_word(p, "EXIT")) {
            if (!(take_word(p, "SUB") || take_word(p, "FUNCTION")) || current_scope < 0)
                fail(p, "Expected: EXIT SUB or EXIT FUNCTION");
            else if (run) returning = 1;
            handled = 1;
        } else if (take_word(p, "ERASE")) {
            erase_statement(p, run);
            handled = 1;
        } else if (take_word(p, "ERROR")) {
            long code = integer_operand(p, numeric(p, run), run);
            if (run && !error_message) {
                if (code <= 0 || code > 255) fail(p, "Illegal function call");
                else {
                    first_error = at;
                    error_message = runtime_error_text((unsigned)code);
                }
            }
            handled = 1;
        } else if (take_word(p, "ENVIRON")) {
            environment_statement(p, run);
            handled = 1;
        } else if (is_legacy_cmd(p, "EDIT")) {
            handled = 1;
        }
        break;
    case 'F':
        if (take_word(p, "FOR")) {
            for_statement(p, at, run, inline_depth);
            handled = 1;
        } else if (take_word(p, "FIELD")) {
            field_statement(p, run);
            handled = 1;
        }
        break;
    case 'G':
        if (take_word(p, "GOTO")) {
            line = target(p);
            if (run && !error_message) go_line(line);
            handled = 1;
        } else if (take_word(p, "GOSUB")) {
            line = target(p);
            if (run && !error_message) {
                if (call_depth == STACK_MAX) fail(p, "Too many nested GOSUBs");
                else {
                    calls[call_depth++] = after_statement(p);
                    go_line(line);
                }
            }
            handled = 1;
        } else if (take_word(p, "GET")) {
            if (p->token == '(' || word(p, "STEP")) gfx_get_statement(p, run);
            else file_transfer(p, run, 0);
            handled = 1;
        }
        break;
    case 'I':
        if (take_word(p, "IF")) {
            condition = numeric(p, run) != 0;
            if (take_word(p, "GOTO")) {
                line = target(p);
                if (run && condition && !error_message) go_line(line);
            } else if (expect_word(p, "THEN", "Expected: THEN") && !error_message) {
                if (!p->token) {
                    index = block(p, at, B_IF, inline_depth);
                    if (run && !condition && !error_message) {
                        unsigned peer = blocks[index].peer;
                        if (blocks[peer].kind == B_ELSEIF) {
                            entering_elseif = 1;
                            go(blocks[peer].at);
                        } else {
                            go(blocks[peer].after);
                        }
                    }
                } else if (inline_depth == 3) fail(p, "IF nested too deeply");
                else {
                    inline_statement_list(p, run && condition, inline_depth + 1);
                    if (take_word(p, "ELSE"))
                        inline_statement_list(p, run && !condition, inline_depth + 1);
                }
            }
            handled = 1;
        } else if (take_word(p, "INPUT")) {
            if (take(p, '#')) input_file_statement(p, run);
            else input_statement(p, run, at);
            handled = 1;
        }
        break;
    case 'K':
        if (take_word(p, "KILL")) {
            path_statement(p, run, DELETE_FILE);
            handled = 1;
        } else if (take_word(p, "KEY")) {
            ckbr_key_statement(p, run);
            handled = 1;
        }
        break;
    case 'L':
        if (take_word(p, "LOOP")) {
            condition = loop_passes(p, run, &kind);
            index = block(p, at, B_LOOP, inline_depth);
            if (run && condition && !error_message) go(blocks[blocks[index].peer].at);
            handled = 1;
        } else if (take_word(p, "LINE")) {
            if (take_word(p, "INPUT")) {
                if (take(p, '#')) line_input_statement(p, run);
                else console_line_input(p, run, at);
            } else {
                gfx_line_statement(p, run);
            }
            handled = 1;
        } else if (take_word(p, "LOCATE")) {
            locate_statement(p, run);
            handled = 1;
        } else if (take_word(p, "LPRINT")) {
            print_statement(p, run, 1);
            handled = 1;
        } else if (take_word(p, "LOCK")) {
            file_lock_statement(p, run, 0);
            handled = 1;
        } else if (take_word(p, "LSET")) {
            justify_assign(p, run, 0);
            handled = 1;
        } else if (word(p, "LIST")) {
            fail(p, "Syntax error");
            handled = 1;
        } else if (is_legacy_cmd(p, "LLIST") || is_legacy_cmd(p, "LOAD")) {
            handled = 1;
        }
        break;
    case 'M':
        if (take_word(p, "MID$")) {
            mid_assign(p, run);
            handled = 1;
        } else if (take_word(p, "MKDIR")) {
            path_statement(p, run, CREATE_DIRECTORY);
            handled = 1;
        } else if (is_legacy_cmd(p, "MERGE") || is_legacy_cmd(p, "MOTOR")) {
            handled = 1;
        }
        break;
    case 'N':
        if (take_word(p, "NEXT")) {
            next_statement(p, at, run, inline_depth);
            handled = 1;
        } else if (take_word(p, "NAME")) {
            rename_statement(p, run);
            handled = 1;
        } else if (is_legacy_cmd(p, "NEW")) {
            handled = 1;
        }
        break;
    case 'O':
        if (take_word(p, "ON")) {
            ckbr_on_statement(p, run);
            handled = 1;
        } else if (take_word(p, "OUT")) {
            out_statement(p, run);
            handled = 1;
        } else if (take_word(p, "OPEN")) {
            open_statement(p, run);
            handled = 1;
        } else if (take_word(p, "OPTION")) {
            expect_word(p, "BASE", "Expected: BASE");
            if (p->token != T_NUMBER || p->length != 1 ||
                (p->number != 0 && p->number != 1))
                fail(p, "Expected: 0 or 1");
            else {
                if (checking) option_base = (long)p->number;
                scan(p);
            }
            handled = 1;
        }
        break;
    case 'P':
        if (take_word(p, "PUT")) {
            if (p->token == '(' || word(p, "STEP")) gfx_put_statement(p, run);
            else file_transfer(p, run, 1);
            handled = 1;
        } else if (take_word(p, "PRINT")) {
            print_statement(p, run, 0);
            handled = 1;
        } else if (take_word(p, "PSET") || word(p, "PRESET")) {
            pset_statement(p, run);
            handled = 1;
        } else if (take_word(p, "PAINT")) {
            gfx_paint_statement(p, run);
            handled = 1;
        } else if (take_word(p, "PALETTE")) {
            palette_statement(p, run);
            handled = 1;
        } else if (take_word(p, "PCOPY")) {
            pcopy_statement(p, run);
            handled = 1;
        } else if (take_word(p, "PLAY")) {
            if (!ckbr_play_ctrl(p, run)) play_statement(p, run);
            handled = 1;
        } else if (take_word(p, "POKE")) {
            poke_statement(p, run, current_def_seg());
            handled = 1;
        }
        break;
    case 'R':
        if (take_word(p, "RETURN")) {
            line = end_statement(p) ? line_count : target(p);
            if (run && !error_message) {
                if (call_depth == (activation ? activation->call_base : 0))
                    fail(p, "RETURN without GOSUB");
                else {
                    --call_depth;
                    ckbr_on_return_pop();
                    if (line == line_count) go(calls[call_depth]);
                    else go_line(line);
                }
            }
            handled = 1;
        } else if (is_legacy_cmd(p, "RENUM")) {
            handled = 1;
        } else if (take_word(p, "READ")) {
            do {
                Reference ref = lvalue(p, run);
                if (record_size(ref.kind)) fail(p, "Type mismatch");
                if (run && !error_message) read_value(p, ref);
            } while (!error_message && take(p, ','));
            handled = 1;
        } else if (take_word(p, "RESTORE")) {
            line = end_statement(p) ? 0 : target(p);
            if (run && !error_message) {
                data_index = data_offset = 0;
                while (data_index < data_count && data_statements[data_index].line < line) ++data_index;
            }
            handled = 1;
        } else if (take_word(p, "RANDOMIZE")) {
            double seed = numeric(p, run);
            if (run && !error_message) seed_random(seed);
            handled = 1;
        } else if (take_word(p, "REDIM")) {
            dim_statement(p, run, 1, 0);
            handled = 1;
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
            handled = 1;
        } else if (word(p, "RUN")) {
            run_or_chain_statement(p, run, 0);
            handled = 1;
        } else if (take_word(p, "RMDIR")) {
            path_statement(p, run, REMOVE_DIRECTORY);
            handled = 1;
        } else if (take_word(p, "RSET")) {
            justify_assign(p, run, 1);
            handled = 1;
        }
        break;
    case 'S':
        if (take_word(p, "SELECT")) {
            select_statement(p, at, run, inline_depth);
            handled = 1;
        } else if (take_word(p, "SOUND")) {
            sound_statement(p, run);
            handled = 1;
        } else if (take_word(p, "SCREEN")) {
            screen_statement(p, run);
            handled = 1;
        } else if (take_word(p, "SHARED")) {
            storage_statement(p, 0);
            handled = 1;
        } else if (take_word(p, "STATIC")) {
            dim_statement(p, run, 0, 1);
            handled = 1;
        } else if (take_word(p, "SWAP")) {
            swap_statement(p, run);
            handled = 1;
        } else if (take_word(p, "SLEEP")) {
            sleep_statement(p, run);
            handled = 1;
        } else if (take_word(p, "SEEK")) {
            seek_statement(p, run);
            handled = 1;
        } else if (take_word(p, "SHELL")) {
            shell_statement(p, run);
            handled = 1;
        } else if (is_legacy_cmd(p, "SAVE")) {
            handled = 1;
        } else if (take_word(p, "SYSTEM")) {
            if (run) {
                stopped = 1;
                stopped_by_system = 1;
            }
            handled = 1;
        } else if (take_word(p, "STOP")) {
            if (run && pause_execution) pause_at(p, at, NULL);
            else if (run) {
                stopped = 1;
                fail(p, "Program stopped");
            }
            handled = 1;
        }
        break;
    case 'T':
        if (take_word(p, "TIMER")) {
            ckbr_timer_statement(p, run);
            handled = 1;
        } else if (take_word(p, "TRON") || take_word(p, "TROFF")) {
            handled = 1;
        }
        break;
    case 'U':
        if (take_word(p, "UNLOCK")) {
            file_lock_statement(p, run, 1);
            handled = 1;
        } else if (is_legacy_cmd(p, "USR")) {
            handled = 1;
        }
        break;
    case 'V':
        if (take_word(p, "VIEW")) {
            view_statement(p, run);
            handled = 1;
        }
        break;
    case 'W':
        if (take_word(p, "WHILE")) {
            condition = numeric(p, run) != 0;
            index = block(p, at, B_WHILE, inline_depth);
            if (run && !condition && !error_message) go(blocks[blocks[index].peer].after);
            handled = 1;
        } else if (take_word(p, "WEND")) {
            index = block(p, at, B_WEND, inline_depth);
            if (run && !error_message) go(blocks[blocks[index].peer].at);
            handled = 1;
        } else if (take_word(p, "WIDTH")) {
            width_statement(p, run);
            handled = 1;
        } else if (take_word(p, "WINDOW")) {
            window_statement(p, run);
            handled = 1;
        } else if (take_word(p, "WAIT")) {
            wait_statement(p, run);
            handled = 1;
        } else if (take_word(p, "WRITE")) {
            write_statement(p, run);
            handled = 1;
        }
        break;
    default:
        break;
    }
    if (!handled) {
        if (!sub_call_statement(p, run)) assignment_statement(p, run);
    }
}

static void read_lines(unsigned size)
{
    unsigned start = 0, end, body, label_end, i;
    Line __far *line;
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
        line->first_block = BLOCK_MAX;
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
                    "BEEP", "CLOSE", "CLS", "DATA", "DO", "ELSE", "END", "LOOP", "NEXT",
                    "PRINT", "REM", "RESTORE", "RETURN", "SLEEP", "STOP", "SYSTEM", "WEND"
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
    unsigned size = 0, cap = SOURCE_MAX + 2U;
    if (!in) {
        fprintf(stderr, "%s: File not found\n", path);
        return 0;
    }
#if defined(__WATCOMC__) || defined(_MSDOS)
    {
        long flen = filelength(fileno(in));
        if (flen >= 0L && flen <= (long)SOURCE_MAX)
            cap = (unsigned)flen + 2U;
    }
#endif
    if (owned_source) { _ffree(owned_source); owned_source = NULL; }
    owned_source = (char __far *)_fmalloc(cap + IMM_EXTRA_BYTES);
    if (!owned_source) {
        fprintf(stderr, "%s: Out of memory\n", path);
        fclose(in);
        return 0;
    }
    source = owned_source;
    while ((c = fgetc(in)) != EOF && c != 26) {
        if (size == 0 && (c == 0xff || c == 0xfe)) {
            fprintf(stderr, "%s: Bad file mode\n", path);
            fclose(in);
            return 0;
        }
        if (size == SOURCE_MAX || size + 1U >= cap || c == 0) {
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
    imm_offset = size + 2U;
    if (!ensure_program_tables(size)) {
        fprintf(stderr, "%s: Out of memory\n", path);
        return 0;
    }
    read_lines(size);
    return 1;
}

void exec_fallback_stmt(Position at, Position *out_after)
{
    Parser p;
    CkbStringTemp *mark;
    initialize_parser(&p, at);
    jumped = 0;
    mark = ckb_string_mark();
    if (ckb_serial_open_count) ckb_serial_poll();
    if (p.token != ':') statement(&p, 1, 0);
    ckb_string_release(mark);
    if (!jumped && !error_message && !end_statement(&p))
        fail(&p, "Expected: end of statement");
    if (!jumped && !error_message && word(&p, "ELSE"))
        fail(&p, "ELSE without IF");
    *out_after = jumped ? next_position : after_statement(&p);
}

static void walk_at(Position at, int run)
{
    Parser p;
    while (at.line < line_count && !error_message && !stopped && !returning) {
        int scope = line_scope[at.line];
        CkbStringTemp *mark;
        if (record_count && at.line <= max_record_end) {
            unsigned type;
            for (type = 0; type < record_count; ++type) {
                if (at.line >= records[type].line && at.line <= records[type].end) {
                    at = next_line(records[type].end);
                    break;
                }
            }
            if (type < record_count) continue;
        }
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
        if (checking && !p.token) array_directive(&p);
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
        if (run && ckbr_has_events && !error_message && !stopped && !returning)
            ckbr_check_events(&at);
    }
    if (!run && !error_message && block_depth) {
        first_error = blocks[block_stack[block_depth - 1]].at;
        error_message = "Block without matching terminator";
    }
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
#else
    release_base_tables();
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
        func_first_mask = func_len_mask = 0;
        def_fn_count = 0;
        option_base = 0;
        dynamic_arrays = 0;
        mbf_mode = mbf;
        any_field_bound = 0;
        has_def_seg = 0;
        def_seg_value = 0;
        stopped_by_system = 0;
        ckbr_reset_events();
        reset_varptr_shadows();
        entering_elseif = 0;
        random_state = 0x50000UL;
        returning = 0;
        line_count = variable_count = block_count = block_depth = for_depth = call_depth = 0;
        var_hash_reset();
        record_count = max_record_end = 0;
        local_pool_top = copy_pool_top = 0;
        common_count = 0;
        screen_column = printer_column = data_count = data_index = data_offset = 0;
        stopped = 0;
        ckb_con_reset_run();
        pause_execution = text ? pause : NULL;
        break_execution = pause_execution ? should_break : NULL;
        error_message = NULL;
        if (text) {
            if (length > SOURCE_MAX) {
                report_run_error(report, current_path, "Source too large", 1, 0, 0);
                result = 1;
                break;
            }
            if (editor_source && text == editor_source) {
                source = editor_source;
                editor_source_len = length;
                source[length] = 0;
                imm_offset = length + 2U;
            } else {
                if (owned_source) { _ffree(owned_source); owned_source = NULL; }
                owned_source = (char __far *)_fmalloc(length + 2U + IMM_EXTRA_BYTES);
                if (!owned_source) {
                    report_run_error(report, current_path, "Out of memory", 1, 0, 0);
                    result = 1;
                    break;
                }
                if (length) _fmemcpy(owned_source, text, length);
                owned_source[length] = 0;
                source = owned_source;
                imm_offset = length + 2U;
            }
            if (!ensure_program_tables(length)) {
                report_run_error(report, current_path, "Out of memory", 1, 0, 0);
                result = 1;
                break;
            }
            read_lines(length);
        } else if (!load_source(current_path)) { result = 1; break; }
        checking = 1;
        memset(default_kind, '!', sizeof(default_kind));
        if (length > 0 && text &&
            ((unsigned char)source[0] == 0xffU || (unsigned char)source[0] == 0xfeU)) {
            first_error = position(0, 0);
            error_message = "Bad file mode";
        }
        if (line_count && !error_message) index_procedures();
        if (line_count && !error_message) walk_at(position(0, lines[0].body), 0);
        assign_frame_slots();
        checking = 0;
        shrink_program_tables();
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
        tok_cache_clear();
        if (line_count && !error_message) {
            int dbg_on = break_execution && (!debug_active_flag || *debug_active_flag);
            if (dbg_on || ckbr_has_events || !ckb_vm_execute())
                walk_at(position(0, lines[0].body), 1);
        }
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
            ckb_sfree(v->text);
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
    ckb_sreset();
    release_exec_tables();
    if (!text) release_base_tables();
    ckb_speaker_off();
    if (!text) ckb_con_reset_text();
    pause_execution = NULL;
    break_execution = NULL;
    pause_context = NULL;
    if (report) {
        report->aborted = aborted;
        report->system_exit = stopped_by_system;
    }
    return result;
}

int ckb_run(const char *path, int mbf)
{
    return execute_program(path, mbf, NULL, 0, NULL, NULL, NULL, NULL);
}

#if defined(__WATCOMC__) || defined(_MSDOS)
void ckb_set_debug_active(const int *flag)
{
    debug_active_flag = flag;
}

void ckb_set_editor_source(char __far *buf, unsigned len)
{
    editor_source = buf;
    editor_source_len = len;
    if (!owned_source) {
        source = buf;
        imm_offset = buf ? (len + 2U) : 0U;
    }
}

int ckb_run_source_ex(const char *path, int mbf, const char __far *text, unsigned length,
                      CkbRunResult *report, CkbRunPause pause, CkbRunBreak should_break,
                      void *context)
{
    return execute_program(path, mbf, text, length, report, pause, should_break, context);
}

int ckb_run_source(const char *path, const char __far *text, unsigned length,
                   CkbRunResult *report, CkbRunPause pause, CkbRunBreak should_break,
                   void *context)
{
    return execute_program(path, 0, text, length, report, pause, should_break, context);
}
#endif
