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
#include <conio.h>
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

Channel *file_number(Parser *p, int run)
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

int file_open(const Channel *file)
{
    return file->stream || file->serial;
}

int file_eof(Parser *p, Channel *file)
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
        INIT_VALUE(&item);
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

void print_statement(Parser *p, int run, int printer)
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
                unsigned width = output.stream == stdout && ckb_con_direct_active() ? ckb_con_width() : 80U;
                unsigned i, spaces = 14 - *output.column % 14;
                if (output.stream == stdout && *output.column + spaces >= width) {
                    print_newline(&output);
                } else {
                    for (i = 0; i < spaces; ++i) print_text(&output, " ");
                }
            }
        } else if (take_word(p, "TAB") || word(p, "SPC")) {
            int is_tab = p->token != T_NAME || !word(p, "SPC");
            long count;
            if (!is_tab) scan(p);
            expect(p, '(', "Expected: (");
            count = integer_operand(p, numeric(p, run), run);
            expect(p, ')', "Expected: )");
            trailing = 1;
            if (run && !error_message) {
                unsigned width = output.stream == stdout && ckb_con_direct_active() ? ckb_con_width() : 80U;
                if (is_tab) {
                    unsigned target_col;
                    if (count < 1) count = 1;
                    target_col = (unsigned)((count - 1) % (long)width);
                    if (*output.column > target_col) print_newline(&output);
                    while (*output.column < target_col && !error_message)
                        print_text(&output, " ");
                } else if (count > 0) {
                    long i;
                    if (count > 32767) count = 32767;
                    for (i = 0; i < count && !error_message; ++i)
                        print_text(&output, " ");
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

void write_statement(Parser *p, int run)
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

long file_position(Parser *p, Channel *file, int which)
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

void seek_file(Parser *p, Channel *file, long position)
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

void open_statement(Parser *p, int run)
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

void file_transfer(Parser *p, int run, int writing)
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
    clearerr(file->stream);
    if (positioned) seek_file(p, file, position);
    else if (fseek(file->stream, 0, SEEK_CUR) != 0) file_io_error(p, "Read error");
    if (error_message) return;
    if (file->mode == FILE_RANDOM && !operand) {
        if (writing) {
            if (fwrite(file->record, 1, file->record_length, file->stream) != file->record_length)
                file_io_error(p, "Write error");
        } else random_record_read(p, file);
        return;
    }
    if (!ref.target) return;
    {
        long rec_start = file->mode == FILE_RANDOM ? ftell(file->stream) : 0L;
        if (file->mode == FILE_RANDOM && ref.kind == '$' && !ref.width) {
            unsigned char len2[2];
            if (writing) {
                fetch_reference(p, ref, 1, &value);
                if (error_message) return;
                if (2U + value.length > file->record_length) {
                    fail(p, "Bad record length");
                    return;
                }
                len2[0] = (unsigned char)(value.length & 0xffU);
                len2[1] = (unsigned char)(value.length >> 8);
                if (fwrite(len2, 1, 2, file->stream) != 2 ||
                    (value.length && fwrite(ckb_string_bytes(&value), 1, value.length, file->stream) != value.length)) {
                    file_io_error(p, "Write error");
                    return;
                }
            } else {
                unsigned slen = 0;
                INIT_VALUE(&value);
                value.string = 1;
                value.kind = '$';
                if (fread(len2, 1, 2, file->stream) == 2)
                    slen = (unsigned)len2[0] | ((unsigned)len2[1] << 8);
                else if (ferror(file->stream)) { file_io_error(p, "Read error"); return; }
                if (2U + slen > file->record_length) {
                    fail(p, "Bad record length");
                    return;
                }
                bytes = ckb_string_buffer(&value, slen);
                if (!bytes) { fail(p, "Out of memory"); return; }
                if (slen) {
                    size_t got = fread(bytes, 1, slen, file->stream);
                    if (ferror(file->stream)) { file_io_error(p, "Read error"); return; }
                    if (got < slen) memset(bytes + got, 0, slen - got);
                }
                assign_reference(p, ref, &value, 1);
            }
            if (!error_message)
                fseek(file->stream, rec_start + (long)file->record_length, SEEK_SET);
            return;
        }
        fetch_reference(p, ref, 1, &value);
        if (error_message) return;
        size = value.string ? value.length : member_size(ref.kind, ref.width);
        if (file->mode == FILE_RANDOM && size > file->record_length) {
            fail(p, "Bad record length");
            return;
        }
        if (record_size(ref.kind)) {
            char __far *frec = (char __far *)value.record;
            unsigned char rbuf[256];
            unsigned off = 0;
            while (off < size) {
                unsigned n = (size - off) > sizeof(rbuf) ? sizeof(rbuf) : (size - off);
                if (!writing) {
                    size_t got = fread(rbuf, 1, n, file->stream);
                    if (ferror(file->stream)) { file_io_error(p, "Read error"); return; }
                    if (got < n) memset(rbuf + got, 0, n - got);
                    _fmemcpy(frec + off, rbuf, n);
                } else {
                    _fmemcpy(rbuf, frec + off, n);
                    if (fwrite(rbuf, 1, n, file->stream) != n) { file_io_error(p, "Write error"); return; }
                }
                off += n;
            }
        } else {
            if (value.string) bytes = ckb_string_buffer_at(&value);
            else { bytes = value.text; if (writing) packed_number(bytes, ref.kind, &value.number, 1); }
            if (!writing) {
                size_t count = fread(bytes, 1, size, file->stream);
                if (ferror(file->stream)) { file_io_error(p, "Read error"); return; }
                memset(bytes + count, 0, size - count);
                if (!value.string) packed_number(bytes, ref.kind, &value.number, 0);
                assign_reference(p, ref, &value, 1);
            } else if (fwrite(bytes, 1, size, file->stream) != size) file_io_error(p, "Write error");
        }
        if (file->mode == FILE_RANDOM && !error_message)
            fseek(file->stream, rec_start + (long)file->record_length, SEEK_SET);
    }
}

void file_lock_statement(Parser *p, int run, int unlocking)
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

void field_statement(Parser *p, int run)
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
        any_field_bound = 1;
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

const char *release_channel(Channel *file)
{
    const char *error = NULL;
    if (file->serial) error = ckb_serial_close(file->serial);
    if (file->stream && fclose(file->stream) != 0) error = "Write error";
    file->serial = NULL;
    file->stream = NULL;
    release_file_record(file);
    return error;
}

void close_file(Parser *p, Channel *file)
{
    const char *error = release_channel(file);
    if (error) fail(p, error);
}

void rename_statement(Parser *p, int run)
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


void path_statement(Parser *p, int run, int operation)
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

void environment_statement(Parser *p, int run)
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

void shell_statement(Parser *p, int run)
{
    Value command;
    const char *text;
    INIT_VALUE(&command);
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

void line_input_statement(Parser *p, int run)
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
    INIT_VALUE(&v);
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
    INIT_VALUE(&value);
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

void input_file_statement(Parser *p, int run)
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

void environment_function(Parser *p, Value *value, unsigned depth, int run)
{
    Value arg;
    const char *name = NULL;
    char *bytes;
    long index = 0;
    int length;
    INIT_VALUE(&arg);
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

#define BUILTIN_POOL_MAX 3U
static Value builtin_arg_pool[BUILTIN_POOL_MAX];
static unsigned builtin_pool_top;

int builtin(Parser *p, Value *v, unsigned depth, int run, int id, int length)
{
    Value *args;
    unsigned n = 0, i;
    int pooled = 0;
    const char *err;
    if (builtin_pool_top + 3U <= BUILTIN_POOL_MAX) {
        args = &builtin_arg_pool[builtin_pool_top];
        builtin_pool_top += 3U;
        pooled = 1;
    } else {
        args = malloc(3 * sizeof(*args));
        if (!args) {
            fail(p, "Out of memory");
            return 1;
        }
    }
    scan(p);
    expect(p, '(', "Expected: (");
    if (p->token != ')') do {
        if (n == 3) {
            fail(p, "Too many arguments");
            if (pooled) builtin_pool_top -= 3U;
            else free(args);
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
                COPY_VALUE(v, &args[0]);
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
    if (pooled) builtin_pool_top -= 3U;
    else free(args);
    return 1;
}

void input_string(Parser *p, Value *v, unsigned depth, int run)
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

void swap_statement(Parser *p, int run)
{
    Reference left = lvalue(p, run), right;
    Value a, b;
    unsigned size;
    char __far *x, *y;
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

void justify_assign(Parser *p, int run, int right)
{
    Reference ref = lvalue(p, run);
    Value value, replacement;
    unsigned width, count, offset;
    char *field, *destination;
    expect(p, '=', "Expected: =");
    expression(p, &replacement, 1, 0, run);
    if (!ref.target || error_message) return;
    if (!right && record_size(ref.kind) && record_size(replacement.kind)) {
        char __far *rfield;
        if (!run || !valid_cell(p, ref.target, ref.cell)) return;
        width = record_size(ref.kind);
        count = record_size(replacement.kind);
        rfield = record_data(p, ref);
        if (rfield) _fmemmove(rfield, replacement.record, count < width ? count : width);
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

void mid_assign(Parser *p, int run)
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

int parse_gfx_coord(Parser *p, int run, int *x_out, int *y_out)
{
    int step = take_word(p, "STEP");
    double wx, wy;
    if (!expect(p, '(', "Expected: (")) return 0;
    if (ckb_window_active()) {
        wx = numeric(p, run);
        if (!expect(p, ',', "Expected: ,")) return 0;
        wy = numeric(p, run);
    } else {
        wx = (double)integer_operand(p, numeric(p, run), run);
        if (!expect(p, ',', "Expected: ,")) return 0;
        wy = (double)integer_operand(p, numeric(p, run), run);
    }
    if (!expect(p, ')', "Expected: )")) return 0;
    if (run && !error_message) {
        ckb_gfx_map_coord(wx, wy, step, x_out, y_out);
    } else {
        *x_out = *y_out = 0;
    }
    return !error_message;
}

static unsigned char __far *resolve_sprite_buffer(Parser *p, int run, unsigned *capacity_out)
{
    char name[BASIC_NAME_MAX + 1], kind;
    int id, unsuffixed = unsuffixed_name(p);
    unsigned cell = 0, esize;
    Variable *v;
    *capacity_out = 0;
    if (!identifier(p, name, &kind)) return NULL;
    id = named_variable(p, name, kind, 1, 0, unsuffixed, 0);
    if (id >= 0 && variables[id].kind == '$') fail(p, "Type mismatch");
    if (p->token == '(') cell = array_cell(p, id, run);
    else if (run && id >= 0 && !error_message) {
        v = array_storage(id);
        if (!v->elements && v->dynamic != ARRAY_DYNAMIC) implicit_array(p, v, 1);
    }
    if (!run || error_message || id < 0) return NULL;
    v = array_storage(id);
    if (!v->elements || cell >= v->cells) {
        fail(p, "Subscript out of range");
        return NULL;
    }
    esize = array_element_size(v);
    *capacity_out = (v->cells - cell) * esize;
    return (unsigned char __far *)v->elements + cell * esize;
}

void gfx_line_statement(Parser *p, int run)
{
    int has_first = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, color = -1, box = 0;
    if (p->token != '-') {
        has_first = 1;
        if (!parse_gfx_coord(p, run, &x1, &y1)) return;
    }
    if (!expect(p, '-', "Expected: -")) return;
    if (!parse_gfx_coord(p, run, &x2, &y2)) return;
    if (take(p, ',')) {
        if (p->token != ',' && !end_statement(p))
            color = (int)integer_operand(p, numeric(p, run), run);
        if (take(p, ',')) {
            if (take_word(p, "BF")) box = 2;
            else if (take_word(p, "B")) box = 1;
            else if (!end_statement(p)) fail(p, "Expected: B or BF");
        }
    }
    if (run && !error_message) {
        const char *err = ckb_line(has_first, x1, y1, x2, y2, color, box);
        if (err) fail(p, err);
    }
}

void gfx_circle_statement(Parser *p, int run)
{
    int x = 0, y = 0, color = -1, hs = 0, he = 0, ha = 0;
    double radius, sa = 0.0, ea = 0.0, asp = 1.0;
    if (!parse_gfx_coord(p, run, &x, &y)) return;
    if (!expect(p, ',', "Expected: ,")) return;
    radius = numeric(p, run);
    if (take(p, ',')) {
        if (p->token != ',' && !end_statement(p))
            color = (int)integer_operand(p, numeric(p, run), run);
        if (take(p, ',')) {
            if (p->token != ',' && !end_statement(p)) { hs = 1; sa = numeric(p, run); }
            if (take(p, ',')) {
                if (p->token != ',' && !end_statement(p)) { he = 1; ea = numeric(p, run); }
                if (take(p, ',') && !end_statement(p)) { ha = 1; asp = numeric(p, run); }
            }
        }
    }
    if (run && !error_message) {
        const char *err = ckb_circle(x, y, radius, color, hs, sa, he, ea, ha, asp);
        if (err) fail(p, err);
    }
}

void gfx_paint_statement(Parser *p, int run)
{
    int x = 0, y = 0, fill_c = -1, border_c = -1;
    if (!parse_gfx_coord(p, run, &x, &y)) return;
    if (take(p, ',')) {
        if (p->token != ',' && !end_statement(p))
            fill_c = (int)integer_operand(p, numeric(p, run), run);
        if (take(p, ',')) {
            if (p->token != ',' && !end_statement(p))
                border_c = (int)integer_operand(p, numeric(p, run), run);
            if (take(p, ',') && !end_statement(p)) {
                Value tile;
                expression(p, &tile, 1, 0, run);
            }
        }
    }
    if (run && !error_message) {
        const char *err = ckb_paint(x, y, fill_c, border_c);
        if (err) fail(p, err);
    }
}

void gfx_get_statement(Parser *p, int run)
{
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    unsigned cap = 0;
    unsigned char __far *buf;
    if (!parse_gfx_coord(p, run, &x1, &y1)) return;
    if (!expect(p, '-', "Expected: -")) return;
    if (!parse_gfx_coord(p, run, &x2, &y2)) return;
    if (!expect(p, ',', "Expected: ,")) return;
    buf = resolve_sprite_buffer(p, run, &cap);
    if (run && !error_message && buf) {
        const char *err = ckb_gfx_get(x1, y1, x2, y2, buf, cap);
        if (err) fail(p, err);
    }
}

void gfx_put_statement(Parser *p, int run)
{
    int x = 0, y = 0, action = CKB_PUT_XOR;
    unsigned cap = 0;
    unsigned char __far *buf;
    if (!parse_gfx_coord(p, run, &x, &y)) return;
    if (!expect(p, ',', "Expected: ,")) return;
    buf = resolve_sprite_buffer(p, run, &cap);
    if (take(p, ',')) {
        if (take_word(p, "PSET")) action = CKB_PUT_PSET;
        else if (take_word(p, "PRESET")) action = CKB_PUT_PRESET;
        else if (take_word(p, "AND")) action = CKB_PUT_AND;
        else if (take_word(p, "OR")) action = CKB_PUT_OR;
        else if (take_word(p, "XOR")) action = CKB_PUT_XOR;
        else fail(p, "Expected: action verb");
    }
    if (run && !error_message && buf) {
        const char *err = ckb_gfx_put(x, y, buf, cap, action);
        if (err) fail(p, err);
    }
}


int close_channels(void)
{
    unsigned i;
    int failed = 0;
    for (i = 0; i < FILE_MAX; ++i)
        if (release_channel(&channels[i])) failed = 1;
    memset(channels, 0, sizeof(channels));
    return !failed;
}

void array_bound(Parser *p, Value *value, int run)
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

void clear_array(Variable *v)
{
    unsigned i;
    if (v->kind == '$') {
        char __far * __far *items = (char __far * __far *)v->elements;
        for (i = 0; i < v->cells; ++i) {
            ckb_sfree(items[i]);
            items[i] = NULL;
        }
    } else if (v->elements) {
        _fmemset(v->elements, 0, v->cells * array_element_size(v));
    }
}

void release_array(Variable *v)
{
    if (v->kind == '$') clear_array(v);
    if (v->elements) _ffree(v->elements);
    free(v->bounds);
    v->elements = NULL;
    v->bounds = NULL;
    v->cells = v->dimensions = 0;
}

void dimension_array(Parser *p, Variable *v, const Bounds *bounds, unsigned dimensions,
                     unsigned cells)
{
    Bounds *shape;
    void __far *elements;
    unsigned size = array_element_size(v);
    unsigned long total = (unsigned long)cells * (unsigned long)size;
    unsigned bytes;
    if (total > 65520UL) {
        fail(p, "Out of memory");
        return;
    }
    shape = malloc(dimensions * sizeof(*shape));
    bytes = (unsigned)(total ? total : 1UL);
    elements = _fmalloc(bytes);
    if (elements) _fmemset(elements, 0, bytes);
    if (!shape || !elements) {
        free(shape);
        if (elements) _ffree(elements);
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

void erase_statement(Parser *p, int run)
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

static void write_output(Parser *p, FILE *stream, unsigned *column, const char *s)
{
    write_bytes(p, stream, column, s, strlen(s));
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

static int editor_input_ex(Parser *p, char *input, int emit_newline)
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
    ckb_con_sync_cursor_hw();
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
            if (emit_newline) write_output(p, stdout, &screen_column, "\n");
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
    ckb_con_restore_cursor();
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

static int console_read_line(Parser *p, Position at, const char *prompt,
                             int question, int emit_newline, char *input,
                             unsigned input_cap)
{
    for (;;) {
        if (prompt && *prompt) write_output(p, stdout, &screen_column, prompt);
        if (question) write_output(p, stdout, &screen_column, "? ");
        if (fflush(stdout) == EOF) fail(p, "Write error");
        if (error_message) return 0;
#if defined(__WATCOMC__) || defined(_MSDOS)
        if (isatty(0) && isatty(1)) {
            if (editor_input_ex(p, input, emit_newline)) break;
            if (!input_interrupted(p, at)) return 0;
            if (input_column()) write_output(p, stdout, &screen_column, "\n");
            continue;
        }
#else
        (void)at;
        (void)emit_newline;
#endif
        if (fgets(input, (int)input_cap, stdin)) break;
        fail(p, "Input past end of file");
        return 0;
    }
    if (emit_newline) screen_column = 0;
    input[strcspn(input, "\r\n")] = 0;
    return 1;
}

void console_line_input(Parser *p, int run, Position at)
{
    Value prompt, v;
    int same_line = take(p, ';');
    Reference ref;
    char input[TEXT_INPUT_MAX + 3];
    unsigned len;
    prompt.length = 0;
    prompt.string = 1;
    prompt.text[0] = 0;
    if (p->token == T_STRING) {
        string_value(p, &prompt);
        if (!take(p, ';')) expect(p, ',', "Expected: ; or ,");
    }
    ref = lvalue(p, run);
    if (ref.target && ref.kind != '$') fail(p, "Type mismatch");
    if (!run || error_message) return;
    if (!console_read_line(p, at, ckb_string_bytes(&prompt), 0, !same_line,
                           input, sizeof(input)))
        return;
    len = (unsigned)strlen(input);
    if (len > TEXT_INPUT_MAX) {
        fail(p, "Input too long");
        return;
    }
    INIT_VALUE(&v);
    v.string = 1;
    v.length = len;
    memcpy(v.text, input, len + 1U);
    assign_reference(p, ref, &v, 1);
}

void input_statement(Parser *p, int run, Position at)
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
    if (!console_read_line(p, at, ckb_string_bytes(&prompt), question, 1,
                           input, sizeof(input)))
        return;
    INIT_VALUE(&v);
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

void free_common(CommonValue *saved, unsigned count, CommonType *types)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        CommonValue *v = &saved[i];
        if (v->array && v->kind == '$') {
            unsigned cell;
            for (cell = 0; cell < v->cells; ++cell)
                ckb_sfree(((char __far * __far *)v->elements)[cell]);
        }
        if (v->elements) _ffree(v->elements);
        free(v->bounds);
        ckb_sfree(v->text);
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

void save_common(CommonValue *saved, CommonType *types)
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

void restore_common(Parser *p, CommonValue *saved, const CommonType *types,
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

void carry_fields(int restore)
{
    unsigned i;
    if (restore) any_field_bound = 1;
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

void cls_statement(Parser *p, int run)
{
    int mode = -1;
    if (!end_statement(p)) mode = (int)integer_operand(p, numeric(p, run), run);
    if (run && !error_message) {
        ckb_cls_mode(mode);
        screen_column = 0;
    }
}

void color_statement(Parser *p, int run)
{
    int fg = -1, bg = -1;
    if (!end_statement(p) && p->token != ',') fg = (int)integer_operand(p, numeric(p, run), run);
    if (take(p, ',')) {
        if (p->token != ',' && !end_statement(p))
            bg = (int)integer_operand(p, numeric(p, run), run);
        if (take(p, ',') && !end_statement(p)) (void)numeric(p, run);
    }
    if (run && !error_message) ckb_color(fg, bg);
}

#if defined(__WATCOMC__) || defined(_MSDOS)
extern void __far ckb_call_absolute(unsigned target_seg, unsigned target_off,
                                    unsigned argc, const unsigned *arg_offsets);
#ifdef __WATCOMC__
#pragma aux ckb_call_absolute "CKB_CALL_ABSOLUTE" parm [ax] [dx] [cx] [bx] modify exact []
#endif
#endif

static unsigned dgroup_seg(void)
{
#if defined(__WATCOMC__) || defined(_MSDOS)
    unsigned char local_b = 0;
    return FP_SEG((void __far *)&local_b);
#else
    return 0;
#endif
}

static int parse_u16_addr(Parser *p, int run, unsigned *out)
{
    double v = numeric(p, run), r;
    *out = 0;
    if (!run || error_message) return !error_message;
    if (!finite_number(v)) { fail(p, "Overflow"); return 0; }
    r = round_even(v);
    if (r < -65536.0 || r > 65535.0) { fail(p, "Overflow"); return 0; }
    *out = (unsigned)((unsigned long)(long)r & 0xffffUL);
    return 1;
}

typedef struct {
    Variable *var;
    unsigned char num_bytes[8];
    unsigned short str_desc[2];
    char *str_near;
    unsigned str_cap;
} VarShadow;

#define VAR_SHADOW_MAX 4
static VarShadow var_shadows[VAR_SHADOW_MAX];
static unsigned var_shadow_next = 0;
static char varptr_resolve_buf[260];

void reset_varptr_shadows(void)
{
    unsigned i;
    for (i = 0; i < VAR_SHADOW_MAX; ++i) {
        if (var_shadows[i].str_near) free(var_shadows[i].str_near);
        memset(&var_shadows[i], 0, sizeof(var_shadows[i]));
    }
    var_shadow_next = 0;
}

static VarShadow *bind_scalar_shadow(Variable *v)
{
    unsigned i, slot_idx;
    VarShadow *s = NULL;
    for (i = 0; i < VAR_SHADOW_MAX; ++i) {
        if (var_shadows[i].var == v) { s = &var_shadows[i]; break; }
    }
    if (!s) {
        for (i = 0; i < VAR_SHADOW_MAX; ++i) {
            if (!var_shadows[i].var) { s = &var_shadows[i]; break; }
        }
        if (!s) {
            slot_idx = var_shadow_next++ % VAR_SHADOW_MAX;
            s = &var_shadows[slot_idx];
        }
        s->var = v;
    }
    if (v->kind == '$') {
        unsigned fwidth = 0;
        char *fdata = field_data(v, &fwidth);
        unsigned len = fdata ? fwidth :
                       v->fixed_width ? v->fixed_width :
                       (v->text ? *(const unsigned __far *)v->text : 0U);
        if (len > s->str_cap || !s->str_near) {
            char *nb = malloc(len + 2U);
            if (nb) {
                free(s->str_near);
                s->str_near = nb;
                s->str_cap = len + 1U;
            } else {
                len = s->str_cap;
            }
        }
        if (s->str_near && len) {
            if (fdata) memcpy(s->str_near, fdata, len);
            else if (v->text) _fmemcpy(s->str_near, v->fixed_width ? v->text : v->text + 4, len);
            else memset(s->str_near, ' ', len);
        }
        s->str_desc[0] = (unsigned short)len;
        s->str_desc[1] = (unsigned short)(size_t)s->str_near;
    } else {
        double n = v->number;
        packed_number((char *)s->num_bytes, v->kind, &n, 1);
    }
    return s;
}

static void sync_shadows_to_vars(void)
{
    unsigned i;
    for (i = 0; i < VAR_SHADOW_MAX; ++i) {
        VarShadow *s = &var_shadows[i];
        Variable *v = s->var;
        if (!v) continue;
        if (v->kind != '$') {
            double n = 0.0;
            packed_number((char *)s->num_bytes, v->kind, &n, 0);
            v->number = n;
        }
    }
}

static const char *resolve_varptr_str(const unsigned char *vp3, unsigned *len_out)
{
    unsigned code = (unsigned)vp3[1] | ((unsigned)vp3[2] << 8);
    *len_out = 0;
    if (vp3[0] != 3U) return NULL;
    if (code & 0x8000U) {
        unsigned vid = code & 0x7fffU;
        Variable *v;
        unsigned len;
        if (vid >= variable_count) return NULL;
        v = variable_storage((int)vid);
        if (v->kind != '$') return NULL;
        if (v->fixed_width) {
            len = v->fixed_width < 255U ? v->fixed_width : 255U;
            if (v->text) _fmemcpy(varptr_resolve_buf, v->text, len);
            else memset(varptr_resolve_buf, ' ', len);
        } else {
            len = v->text ? *(const unsigned __far *)v->text : 0U;
            if (len > 255U) len = 255U;
            if (len) _fmemcpy(varptr_resolve_buf, v->text + 4, len);
        }
        varptr_resolve_buf[len] = '\0';
        *len_out = len;
        return varptr_resolve_buf;
    }
    return NULL;
}

static int resolve_lvalue_far_addr(Parser *p, int run, unsigned *seg_out,
                                   unsigned *off_out, Reference *ref_out)
{
    Reference ref = lvalue(p, run);
    if (ref_out) *ref_out = ref;
    *seg_out = 0;
    *off_out = 0;
    if (!run || error_message || !ref.target) return !error_message;
    if (record_size(ref.target->kind)) {
        char __far *rdata = record_data(p, ref);
        if (!rdata) return 0;
#if defined(__WATCOMC__) || defined(_MSDOS)
        *seg_out = FP_SEG(rdata);
        *off_out = FP_OFF(rdata);
#endif
        return 1;
    }
    if (ref.target->array) {
        unsigned esize = array_element_size(ref.target);
        unsigned char __far *elem;
        if (!valid_cell(p, ref.target, ref.cell)) return 0;
        elem = (unsigned char __far *)ref.target->elements + ref.cell * esize;
        if (ref.target->kind == '$') {
            VarShadow *s = bind_scalar_shadow(ref.target);
            char __far *st = ((char __far * __far *)ref.target->elements)[ref.cell];
            unsigned len = ref.target->fixed_width ? ref.target->fixed_width :
                           (st ? *(const unsigned __far *)st : 0U);
            if (len > 255U) len = 255U;
            if (!s->str_near || s->str_cap < len + 1U) {
                char *nb = malloc(len + 2U);
                if (nb) { free(s->str_near); s->str_near = nb; s->str_cap = len + 1U; }
            }
            if (s->str_near && len && st)
                _fmemcpy(s->str_near, ref.target->fixed_width ? elem : (unsigned char __far *)(st + 4), len);
            s->var = NULL;
            s->str_desc[0] = (unsigned short)len;
            s->str_desc[1] = (unsigned short)(size_t)s->str_near;
            *seg_out = dgroup_seg();
            *off_out = (unsigned)(size_t)s->str_desc;
            return 1;
        }
#if defined(__WATCOMC__) || defined(_MSDOS)
        *seg_out = FP_SEG(elem);
        *off_out = FP_OFF(elem);
#endif
        return 1;
    }
    {
        VarShadow *s = bind_scalar_shadow(ref.target);
        *seg_out = dgroup_seg();
        *off_out = (ref.target->kind == '$')
            ? (unsigned)(size_t)s->str_desc
            : (unsigned)(size_t)s->num_bytes;
    }
    return 1;
}

void varseg_function(Parser *p, Value *v, int run)
{
    unsigned seg = 0, off = 0;
    expect(p, '(', "Expected: (");
    resolve_lvalue_far_addr(p, run, &seg, &off, NULL);
    expect(p, ')', "Expected: )");
    v->kind = '%';
    if (run && !error_message) v->number = (double)(short)seg;
}

void varptr_function(Parser *p, Value *v, int run)
{
    unsigned seg = 0, off = 0;
    expect(p, '(', "Expected: (");
    resolve_lvalue_far_addr(p, run, &seg, &off, NULL);
    expect(p, ')', "Expected: )");
    v->kind = '%';
    if (run && !error_message) v->number = (double)(short)off;
}

void varptr_str_function(Parser *p, Value *v, int run)
{
    Reference ref;
    unsigned char tag = 4U;
    unsigned code = 0U;
    expect(p, '(', "Expected: (");
    ref = lvalue(p, run);
    expect(p, ')', "Expected: )");
    v->kind = '$';
    v->string = 1;
    if (!run || error_message || !ref.target) {
        v->length = 0;
        v->text[0] = '\0';
        return;
    }
    switch (ref.kind) {
    case '%': tag = 2U; break;
    case '$': tag = 3U; break;
    case '&': tag = 20U; break;
    case '!': tag = 4U; break;
    case '#': tag = 8U; break;
    default: tag = 4U; break;
    }
    if (!ref.target->array)
        code = 0x8000U | (unsigned)(ref.target - variables);
    v->length = 3U;
    v->text[0] = (char)tag;
    v->text[1] = (char)(code & 0xffU);
    v->text[2] = (char)((code >> 8) & 0xffU);
    v->text[3] = '\0';
}

void sadd_function(Parser *p, Value *v, int run, unsigned depth)
{
    Parser save_p;
    expect(p, '(', "Expected: (");
    save_p = *p;
    v->kind = '%';
    if (p->token == T_NAME) {
        Reference ref = lvalue(p, run);
        if (!error_message && p->token == ')' && ref.target && ref.kind == '$' && !ref.target->array) {
            expect(p, ')', "Expected: )");
            if (run) {
                VarShadow *s = bind_scalar_shadow(ref.target);
                v->number = (double)(short)s->str_desc[1];
            }
            return;
        }
    }
    error_message = NULL;
    *p = save_p;
    {
        Value sv;
        expression(p, &sv, 1, depth + 1U, run);
        expect(p, ')', "Expected: )");
        if (!error_message && !sv.string) fail(p, "Type mismatch");
        else if (run && !error_message) {
            VarShadow *s = &var_shadows[var_shadow_next++ % VAR_SHADOW_MAX];
            unsigned len = sv.length;
            s->var = NULL;
            if (!s->str_near || s->str_cap < len + 1U) {
                char *nb = malloc(len + 2U);
                if (nb) { free(s->str_near); s->str_near = nb; s->str_cap = len + 1U; }
            }
            if (s->str_near && len) memcpy(s->str_near, ckb_string_bytes(&sv), len);
            v->number = (double)(short)(unsigned)(size_t)s->str_near;
        }
    }
}

void screen_statement(Parser *p, int run)
{
    int mode = -1, cs = -1, ap = -1, vp = -1;
    if (p->token != ',' && !end_statement(p))
        mode = (int)integer_operand(p, numeric(p, run), run);
    if (take(p, ',')) {
        if (p->token != ',' && !end_statement(p))
            cs = (int)integer_operand(p, numeric(p, run), run);
        if (take(p, ',')) {
            if (p->token != ',' && !end_statement(p))
                ap = (int)integer_operand(p, numeric(p, run), run);
            if (take(p, ',') && !end_statement(p))
                vp = (int)integer_operand(p, numeric(p, run), run);
        }
    }
    if (mode == -1 && cs == -1 && ap == -1 && vp == -1) {
        fail(p, "Expected: expression");
        return;
    }
    if (run && !error_message) {
        const char *err = ckb_screen_ex(mode, cs, ap, vp);
        if (err) fail(p, err);
        else screen_column = 0;
    }
}

void pcopy_statement(Parser *p, int run)
{
    int src = (int)integer_operand(p, numeric(p, run), run), dst;
    expect(p, ',', "Expected: ,");
    dst = (int)integer_operand(p, numeric(p, run), run);
    if (run && !error_message) {
        const char *err = ckb_pcopy(src, dst);
        if (err) fail(p, err);
    }
}

void width_statement(Parser *p, int run)
{
    int cols = -1, rows = -1;
    if (p->token != ',' && !end_statement(p))
        cols = (int)integer_operand(p, numeric(p, run), run);
    if (take(p, ',') && !end_statement(p))
        rows = (int)integer_operand(p, numeric(p, run), run);
    if (run && !error_message) {
        const char *err = ckb_width_screen(cols, rows);
        if (err) fail(p, err);
        else screen_column = 0;
    }
}

void palette_statement(Parser *p, int run)
{
    if (!end_statement(p)) {
        int attr = (int)integer_operand(p, numeric(p, run), run);
        long color;
        expect(p, ',', "Expected: ,");
        color = integer_operand(p, numeric(p, run), run);
        if (run && !error_message) {
            const char *err = ckb_palette(attr, color);
            if (err) fail(p, err);
        }
    }
}

void locate_statement(Parser *p, int run)
{
    int row = -1, col = -1, cursor = -1, extra;
    if (p->token != ',' && !end_statement(p))
        row = (int)integer_operand(p, numeric(p, run), run);
    if (take(p, ',')) {
        if (p->token != ',' && !end_statement(p))
            col = (int)integer_operand(p, numeric(p, run), run);
        if (take(p, ',')) {
            if (p->token != ',' && !end_statement(p))
                cursor = (int)integer_operand(p, numeric(p, run), run);
            for (extra = 0; extra < 2 && take(p, ','); ++extra)
                if (p->token != ',' && !end_statement(p)) (void)numeric(p, run);
        }
    }
    if (run && !error_message) {
        const char *err = ckb_locate(row, col, cursor);
        if (err) fail(p, err);
        else screen_column = ckb_con_column();
    }
}

void view_statement(Parser *p, int run)
{
    if (take_word(p, "PRINT")) {
        int top = -1, bottom = -1;
        if (!end_statement(p)) {
            top = (int)integer_operand(p, numeric(p, run), run);
            expect_word(p, "TO", "Expected: TO");
            bottom = (int)integer_operand(p, numeric(p, run), run);
        }
        if (run && !error_message) {
            const char *err = ckb_view_print(top, bottom);
            if (err) fail(p, err);
            else screen_column = 0;
        }
    } else if (end_statement(p)) {
        if (run) {
            const char *err = ckb_view_gfx(1, 0, 0, 0, 0, 0, -1, -1);
            if (err) fail(p, err);
        }
    } else {
        int is_scr = take_word(p, "SCREEN");
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0, fc = -1, bc = -1;
        if (!expect(p, '(', "Expected: (")) return;
        x1 = (int)integer_operand(p, numeric(p, run), run);
        if (!expect(p, ',', "Expected: ,")) return;
        y1 = (int)integer_operand(p, numeric(p, run), run);
        if (!expect(p, ')', "Expected: )") || !expect(p, '-', "Expected: -") ||
            !expect(p, '(', "Expected: (")) return;
        x2 = (int)integer_operand(p, numeric(p, run), run);
        if (!expect(p, ',', "Expected: ,")) return;
        y2 = (int)integer_operand(p, numeric(p, run), run);
        if (!expect(p, ')', "Expected: )")) return;
        if (take(p, ',')) {
            if (p->token != ',' && !end_statement(p))
                fc = (int)integer_operand(p, numeric(p, run), run);
            if (take(p, ',') && !end_statement(p))
                bc = (int)integer_operand(p, numeric(p, run), run);
        }
        if (run && !error_message) {
            const char *err = ckb_view_gfx(0, is_scr, x1, y1, x2, y2, fc, bc);
            if (err) fail(p, err);
        }
    }
}

void window_statement(Parser *p, int run)
{
    if (end_statement(p)) {
        if (run) {
            const char *err = ckb_window(1, 0, 0.0, 0.0, 0.0, 0.0);
            if (err) fail(p, err);
        }
    } else {
        int is_scr = take_word(p, "SCREEN");
        double x1, y1, x2, y2;
        if (!expect(p, '(', "Expected: (")) return;
        x1 = numeric(p, run);
        if (!expect(p, ',', "Expected: ,")) return;
        y1 = numeric(p, run);
        if (!expect(p, ')', "Expected: )") || !expect(p, '-', "Expected: -") ||
            !expect(p, '(', "Expected: (")) return;
        x2 = numeric(p, run);
        if (!expect(p, ',', "Expected: ,")) return;
        y2 = numeric(p, run);
        if (!expect(p, ')', "Expected: )")) return;
        if (run && !error_message) {
            const char *err = ckb_window(0, is_scr, x1, y1, x2, y2);
            if (err) fail(p, err);
        }
    }
}

void draw_statement(Parser *p, int run)
{
    Value mml;
    expression(p, &mml, 1, 0, run);
    if (!mml.string) fail(p, "Type mismatch");
    else if (run && !error_message) {
        const char *err = ckb_draw(ckb_string_bytes(&mml), mml.length, resolve_varptr_str);
        if (err) fail(p, err);
    }
}

void poke_statement(Parser *p, int run, unsigned seg)
{
    unsigned addr = 0;
    long val;
    if (!parse_u16_addr(p, run, &addr)) return;
    expect(p, ',', "Expected: ,");
    val = integer_operand(p, numeric(p, run), run);
    if (run && !error_message) {
        if (val < 0 || val > 255L)
            fail(p, "Illegal function call");
#if defined(__WATCOMC__) || defined(_MSDOS)
        else {
            *(unsigned char __far *)MK_FP(seg, addr) = (unsigned char)val;
            if (seg == dgroup_seg()) sync_shadows_to_vars();
        }
#else
        else (void)seg;
#endif
    }
}

void out_statement(Parser *p, int run)
{
    unsigned port = 0;
    long val;
    if (!parse_u16_addr(p, run, &port)) return;
    expect(p, ',', "Expected: ,");
    val = integer_operand(p, numeric(p, run), run);
    if (run && !error_message) {
        if (val < -128L || val > 255L) fail(p, "Illegal function call");
#if defined(__WATCOMC__) || defined(_MSDOS)
        else outp(port, (int)((unsigned long)val & 0xffUL));
#endif
    }
}

void wait_statement(Parser *p, int run)
{
    unsigned port = 0;
    long and_mask, xor_mask = 0;
    if (!parse_u16_addr(p, run, &port)) return;
    expect(p, ',', "Expected: ,");
    and_mask = integer_operand(p, numeric(p, run), run);
    if (take(p, ',') && !end_statement(p))
        xor_mask = integer_operand(p, numeric(p, run), run);
    if (run && !error_message) {
        if (and_mask < 0 || and_mask > 255L || xor_mask < 0 || xor_mask > 255L) {
            fail(p, "Illegal function call");
            return;
        }
#if defined(__WATCOMC__) || defined(_MSDOS)
        while ((((unsigned)inp(port) ^ (unsigned)xor_mask) & (unsigned)and_mask) == 0U) {
        }
#endif
    }
}

void inp_function(Parser *p, Value *v, int run)
{
    unsigned port = 0;
    expect(p, '(', "Expected: (");
    parse_u16_addr(p, run, &port);
    expect(p, ')', "Expected: )");
    v->kind = '%';
    if (run && !error_message) {
#if defined(__WATCOMC__) || defined(_MSDOS)
        v->number = (double)((unsigned)inp(port) & 0xffU);
#else
        v->number = 0;
#endif
    }
}

void bsave_statement(Parser *p, int run)
{
    Value path;
    unsigned off = 0;
    long len;
    expression(p, &path, 1, 0, run);
    if (!path.string) { fail(p, "Type mismatch"); return; }
    if (!expect(p, ',', "Expected: ,") || !parse_u16_addr(p, run, &off) ||
        !expect(p, ',', "Expected: ,")) return;
    len = integer_operand(p, numeric(p, run), run);
    if (!run || error_message) return;
    if (len < 0 || len > 65535L) { fail(p, "Illegal function call"); return; }
    if (!path.length || path.length > TEXT_INPUT_MAX) { fail(p, "Bad file name"); return; }
    {
        FILE *fp = fopen(ckb_string_bytes(&path), "wb");
        unsigned seg = current_def_seg();
        unsigned ulen = (unsigned)len;
        unsigned char hdr[7];
        unsigned char chunk[256];
        unsigned pos = 0;
        if (!fp) { fail(p, "Path/File access error"); return; }
        hdr[0] = 0xfdU;
        hdr[1] = (unsigned char)(seg & 0xffU);
        hdr[2] = (unsigned char)(seg >> 8);
        hdr[3] = (unsigned char)(off & 0xffU);
        hdr[4] = (unsigned char)(off >> 8);
        hdr[5] = (unsigned char)(ulen & 0xffU);
        hdr[6] = (unsigned char)(ulen >> 8);
        if (fwrite(hdr, 1, 7, fp) != 7) { fclose(fp); fail(p, "Write error"); return; }
#if defined(__WATCOMC__) || defined(_MSDOS)
        while (pos < ulen) {
            unsigned n = (ulen - pos) > sizeof(chunk) ? sizeof(chunk) : (ulen - pos);
            _fmemcpy(chunk, MK_FP(seg, off + pos), n);
            if (fwrite(chunk, 1, n, fp) != n) { fclose(fp); fail(p, "Write error"); return; }
            pos += n;
        }
#else
        memset(chunk, 0, sizeof(chunk));
        while (pos < ulen) {
            unsigned n = (ulen - pos) > sizeof(chunk) ? sizeof(chunk) : (ulen - pos);
            if (fwrite(chunk, 1, n, fp) != n) { fclose(fp); fail(p, "Write error"); return; }
            pos += n;
        }
#endif
        if (fclose(fp) != 0) fail(p, "Write error");
    }
}

void bload_statement(Parser *p, int run)
{
    Value path;
    int has_off = 0;
    unsigned off = 0;
    expression(p, &path, 1, 0, run);
    if (!path.string) { fail(p, "Type mismatch"); return; }
    if (take(p, ',') && !end_statement(p)) {
        has_off = 1;
        if (!parse_u16_addr(p, run, &off)) return;
    }
    if (!run || error_message) return;
    if (!path.length || path.length > TEXT_INPUT_MAX) { fail(p, "Bad file name"); return; }
    {
        FILE *fp = fopen(ckb_string_bytes(&path), "rb");
        unsigned char hdr[7];
        unsigned char chunk[256];
        unsigned seg, dst_off, ulen, pos = 0;
        if (!fp) { fail(p, "File not found"); return; }
        if (fread(hdr, 1, 7, fp) != 7) {
            fclose(fp);
            fail(p, "Bad file mode");
            return;
        }
        seg = has_off ? current_def_seg() : ((unsigned)hdr[1] | ((unsigned)hdr[2] << 8));
        dst_off = has_off ? off : ((unsigned)hdr[3] | ((unsigned)hdr[4] << 8));
        ulen = (unsigned)hdr[5] | ((unsigned)hdr[6] << 8);
        while (pos < ulen) {
            unsigned n = (ulen - pos) > sizeof(chunk) ? sizeof(chunk) : (ulen - pos);
            if (fread(chunk, 1, n, fp) != n) {
                fclose(fp);
                fail(p, "Bad file mode");
                return;
            }
#if defined(__WATCOMC__) || defined(_MSDOS)
            _fmemcpy(MK_FP(seg, dst_off + pos), chunk, n);
#else
            (void)seg; (void)dst_off;
#endif
            pos += n;
        }
        fclose(fp);
#if defined(__WATCOMC__) || defined(_MSDOS)
        if (seg == dgroup_seg()) sync_shadows_to_vars();
#endif
    }
}

typedef struct {
    Reference ref;
    int is_ref;
    char kind;
    unsigned char num_slot[8];
    unsigned short str_desc[2];
    char *str_buf;
    unsigned str_len;
} AbsArg;

void call_absolute_statement(Parser *p, int run)
{
    AbsArg args[PARAM_MAX];
    unsigned arg_offs[PARAM_MAX];
    unsigned argc = 0, i, target_off = 0;
    memset(args, 0, sizeof(args));
    if (!expect(p, '(', "Expected: (")) return;
    for (;;) {
        Parser scan_p = *p;
        int depth = 0;
        while (scan_p.token && !(depth == 0 && (scan_p.token == ',' || scan_p.token == ')'))) {
            if (scan_p.token == '(') ++depth;
            else if (scan_p.token == ')') --depth;
            scan(&scan_p);
        }
        if (scan_p.token == ',') {
            Parser item_start = *p;
            AbsArg *a;
            if (argc >= PARAM_MAX) { fail(p, "Too many arguments"); goto cleanup; }
            a = &args[argc];
            if (p->token == T_NAME) {
                Reference r = lvalue(p, run);
                if (!error_message && p->token == ',') {
                    a->ref = r;
                    a->is_ref = 1;
                    a->kind = r.kind;
                } else {
                    error_message = NULL;
                    *p = item_start;
                }
            }
            if (!a->is_ref) {
                Value val;
                expression(p, &val, 1, 0, run);
                if (error_message) goto cleanup;
                a->kind = val.string ? '$' : (val.kind ? val.kind : '!');
                if (run) {
                    if (val.string) {
                        a->str_len = val.length;
                        a->str_buf = malloc(a->str_len + 2U);
                        if (!a->str_buf) { fail(p, "Out of memory"); goto cleanup; }
                        if (a->str_len) memcpy(a->str_buf, ckb_string_bytes(&val), a->str_len);
                        a->str_desc[0] = (unsigned short)a->str_len;
                        a->str_desc[1] = (unsigned short)(size_t)a->str_buf;
                    } else {
                        double d = val.number;
                        packed_number((char *)a->num_slot, a->kind, &d, 1);
                    }
                }
            } else if (run && a->ref.target) {
                Value cur;
                unsigned rsize = record_size(a->kind);
                if (rsize) {
                    char __far *rd = record_data(p, a->ref);
                    if (!rd) goto cleanup;
                    a->str_len = rsize;
                    a->str_buf = malloc(rsize);
                    if (!a->str_buf) { fail(p, "Out of memory"); goto cleanup; }
                    _fmemcpy(a->str_buf, rd, rsize);
                    arg_offs[argc++] = (unsigned)(size_t)a->str_buf;
                    expect(p, ',', "Expected: ,");
                    continue;
                }
                fetch_reference(p, a->ref, 1, &cur);
                if (error_message) goto cleanup;
                if (a->kind == '$') {
                    a->str_len = cur.length;
                    a->str_buf = malloc(a->str_len + 2U);
                    if (!a->str_buf) { fail(p, "Out of memory"); goto cleanup; }
                    if (a->str_len) memcpy(a->str_buf, ckb_string_bytes(&cur), a->str_len);
                    a->str_desc[0] = (unsigned short)a->str_len;
                    a->str_desc[1] = (unsigned short)(size_t)a->str_buf;
                } else {
                    double d = cur.number;
                    packed_number((char *)a->num_slot, a->kind, &d, 1);
                }
            }
            arg_offs[argc] = (a->kind == '$')
                ? (unsigned)(size_t)a->str_desc
                : (unsigned)(size_t)a->num_slot;
            ++argc;
            expect(p, ',', "Expected: ,");
        } else {
            parse_u16_addr(p, run, &target_off);
            expect(p, ')', "Expected: )");
            break;
        }
    }
    if (run && !error_message) {
#if defined(__WATCOMC__) || defined(_MSDOS)
        ckb_call_absolute(current_def_seg(), target_off, argc, arg_offs);
        sync_shadows_to_vars();
#endif
        for (i = 0; i < argc && !error_message; ++i) {
            AbsArg *a = &args[i];
            unsigned rsize = record_size(a->kind);
            if (!a->is_ref || !a->ref.target) continue;
            if (rsize) {
                char __far *rd = record_data(p, a->ref);
                if (rd && a->str_buf) _fmemcpy(rd, a->str_buf, rsize);
                continue;
            }
            if (a->kind == '$') {
                Value nv;
                unsigned nlen = a->str_desc[0] <= a->str_len ? a->str_desc[0] : a->str_len;
                char *dst;
                INIT_VALUE(&nv);
                nv.string = 1;
                nv.kind = '$';
                dst = ckb_string_buffer(&nv, nlen);
                if (dst && nlen && a->str_buf) memcpy(dst, a->str_buf, nlen);
                if (dst) dst[nlen] = '\0';
                assign_reference(p, a->ref, &nv, 1);
            } else {
                Value nv;
                double d = 0.0;
                packed_number((char *)a->num_slot, a->kind, &d, 0);
                INIT_VALUE(&nv);
                nv.kind = a->kind;
                nv.number = d;
                assign_reference(p, a->ref, &nv, 1);
            }
        }
    }
cleanup:
    for (i = 0; i < argc; ++i) free(args[i].str_buf);
}

void sound_statement(Parser *p, int run)
{
    double freq = numeric(p, run), dur;
    expect(p, ',', "Expected: ,");
    dur = numeric(p, run);
    if (run && !error_message) {
        const char *err = ckb_sound(freq, dur, NULL);
        if (err) fail(p, err);
    }
}

void play_statement(Parser *p, int run)
{
    Value mml;
    expression(p, &mml, 1, 0, run);
    if (!mml.string) fail(p, "Type mismatch");
    else if (run && !error_message) {
        const char *err = ckb_play(ckb_string_bytes(&mml), mml.length, NULL, resolve_varptr_str);
        if (err) fail(p, err);
    }
}

void sleep_statement(Parser *p, int run)
{
    long secs = 0;
    if (!end_statement(p)) secs = integer_operand(p, numeric(p, run), run);
    if (run && !error_message) ckb_sleep(secs, NULL);
}

void pset_statement(Parser *p, int run)
{
    int is_preset = p->token == T_NAME && word(p, "PRESET");
    int x = 0, y = 0, color = -1;
    if (is_preset) scan(p);
    if (parse_gfx_coord(p, run, &x, &y)) {
        if (take(p, ',') && !end_statement(p))
            color = (int)integer_operand(p, numeric(p, run), run);
        if (run && !error_message) {
            const char *err = ckb_pset(x, y, color, is_preset);
            if (err) fail(p, err);
        }
    }
}

void close_statement(Parser *p, int run)
{
    unsigned index;
    if (end_statement(p)) {
        if (run) for (index = 0; index < FILE_MAX; ++index) close_file(p, &channels[index]);
    } else do {
        Channel *file = file_number(p, run);
        if (run && !error_message) close_file(p, file);
    } while (!error_message && take(p, ','));
}

void seek_statement(Parser *p, int run)
{
    Channel *file = file_number(p, run);
    long position;
    expect(p, ',', "Expected: ,");
    position = integer_operand(p, numeric(p, run), run);
    if (run && !error_message) seek_file(p, file, position);
}

void peek_function(Parser *p, Value *v, int run, unsigned seg)
{
    unsigned addr = 0;
    expect(p, '(', "Expected: (");
    parse_u16_addr(p, run, &addr);
    expect(p, ')', "Expected: )");
    v->kind = '%';
    if (run && !error_message) {
#if defined(__WATCOMC__) || defined(_MSDOS)
        v->number = *(const unsigned char __far *)MK_FP(seg, addr);
#else
        (void)seg;
        v->number = 0;
#endif
    }
}

void pmap_function(Parser *p, Value *v, int run)
{
    double coord;
    long func;
    expect(p, '(', "Expected: (");
    coord = numeric(p, run);
    expect(p, ',', "Expected: ,");
    func = integer_operand(p, numeric(p, run), run);
    expect(p, ')', "Expected: )");
    v->kind = '!';
    if (run && !error_message) {
        const char *err = NULL;
        v->number = ckb_pmap(coord, (int)func, &err);
        if (err) fail(p, err);
    }
}

void screen_function(Parser *p, Value *v, int run)
{
    long row, col, flag = 0;
    expect(p, '(', "Expected: (");
    row = integer_operand(p, numeric(p, run), run);
    expect(p, ',', "Expected: ,");
    col = integer_operand(p, numeric(p, run), run);
    if (take(p, ','))
        flag = integer_operand(p, numeric(p, run), run);
    expect(p, ')', "Expected: )");
    v->kind = '%';
    if (run && !error_message) {
        const char *err = NULL;
        v->number = (double)ckb_screen_cell((int)row, (int)col, (int)flag, &err);
        if (err) fail(p, err);
    }
}

void point_function(Parser *p, Value *v, int run)
{
    double first;
    expect(p, '(', "Expected: (");
    first = numeric(p, run);
    if (take(p, ',')) {
        double second = numeric(p, run);
        expect(p, ')', "Expected: )");
        v->kind = '%';
        if (run && !error_message) {
            int px = 0, py = 0;
            if (!ckb_window_active()) {
                long ix = integer_operand(p, first, 1);
                long iy = integer_operand(p, second, 1);
                if (error_message) return;
                ckb_gfx_map_coord((double)ix, (double)iy, 0, &px, &py);
            } else {
                ckb_gfx_map_coord(first, second, 0, &px, &py);
            }
            v->number = ckb_point(px, py);
        }
    } else {
        long fn = integer_operand(p, first, run);
        expect(p, ')', "Expected: )");
        v->kind = (fn >= 2 && ckb_window_active()) ? '!' : '%';
        if (run && !error_message) {
            const char *err = NULL;
            v->number = ckb_point_info((int)fn, &err);
            if (err) fail(p, err);
        }
    }
}
