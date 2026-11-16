/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include <dos.h>
#if defined(__WATCOMC__) || defined(_MSDOS)
#ifdef __WATCOMC__
#include <i86.h>
#else
#include <dos.h>
#endif
#endif
#include <process.h>
#include <stdlib.h>
#include <string.h>

#include "CKBIDE.H"
#include "CKBRUN.H"

#define DOS_SAVE_PARAGRAPHS 600U
#define DOS_NO_MEMORY 8U
#define DOS_STDOUT 1
#define DOS_STDERR 2
#define PSP_TAIL_MAX 126U

extern unsigned __far dosui_smoke(unsigned segment, const char *text);
#ifdef __WATCOMC__
#pragma aux dosui_smoke "CKBASIC_DOSUI_SMOKE" parm [ax] [si] value [ax] modify exact [ax]
#endif

static char tail[PSP_TAIL_MAX + 1U];
static unsigned char file_buffer[512];

static int is_space(char byte)
{
    return byte == ' ' || byte == '\t';
}

static char *argument(char **cursor)
{
    char *start;
    while (is_space(**cursor)) ++*cursor;
    start = *cursor;
    while (**cursor && !is_space(**cursor)) ++*cursor;
    if (**cursor) *(*cursor)++ = '\0';
    return start;
}

/* A quoted source path may contain spaces.  Keep the legacy argument parser
 * for /PROBE and /EXIT so their established command tails do not change. */
static char *run_argument(char **cursor)
{
    char *start;
    while (is_space(**cursor)) ++*cursor;
    if (**cursor != '"') return argument(cursor);
    start = ++*cursor;
    while (**cursor && **cursor != '"') ++*cursor;
    if (!**cursor) return NULL;
    *(*cursor)++ = '\0';
    if (**cursor && !is_space(**cursor)) return NULL;
    return start;
}

static int read_tail(void)
{
    const unsigned char __far *psp_tail = MK_FP(_psp, 0x80);
    unsigned i;
    unsigned length = psp_tail[0];
    if (length > PSP_TAIL_MAX || psp_tail[length + 1U] != '\r') return 0;
    for (i = 0; i < length; ++i) {
        if (psp_tail[i + 1U] == '\0') return 0;
        tail[i] = (char)psp_tail[i + 1U];
    }
    tail[length] = '\0';
    return 1;
}

/* Read only environment strings; the DOS 3+ trailer is not an entry.
 * The preceding MCB bounds the scan for the normal DOS-owned block. */
static int check_environment(void)
{
    static const char expected[] = "CKBASIC_WP02=ready";
    unsigned segment = *(const unsigned __far *)MK_FP(_psp, 0x2c);
    const unsigned char __far *entry;
    unsigned paragraphs;
    unsigned limit;
    unsigned offset = 0;
    unsigned match = 0;
    if (segment == 0) return 0;
    paragraphs = *(const unsigned __far *)MK_FP(segment - 1U, 3);
    limit = paragraphs > 0xfffU ? 0xffffU : paragraphs * 16U;
    entry = MK_FP(segment, 0);
    while (offset < limit && entry[offset]) {
        unsigned start = offset;
        unsigned length;
        while (offset < limit && entry[offset]) ++offset;
        if (offset == limit) return 0;
        length = offset - start;
        if (length == sizeof(expected) - 1U) {
            unsigned i;
            for (i = 0; i < length && entry[start + i] == expected[i]; ++i) {}
            if (i == length) match = 1;
        }
        ++offset;
    }
    return offset < limit && match;
}

static int write_bytes(int handle, const void *data, unsigned size)
{
    unsigned written;
    return _dos_write(handle, data, size, &written) == 0 && written == size;
}

static int write_text(int handle, const char *text)
{
    return write_bytes(handle, text, strlen(text));
}

/* DOS 3+ create-new (INT 21h/5Bh); never truncate an existing destination. */
static int copy_file(const char *input, const char *output)
{
    int from, to;
    unsigned size;
    int ok = 0;
    if (_dos_open(input, 0, &from) != 0) return 0;
    if (_dos_creatnew(output, 0, &to) == 0) {
        while (_dos_read(from, file_buffer, sizeof(file_buffer), &size) == 0) {
            if (!size) {
                ok = 1;
                break;
            }
            if (!write_bytes(to, file_buffer, size)) break;
        }
        if (_dos_close(to) != 0) ok = 0;
    }
    if (_dos_close(from) != 0) ok = 0;
    return ok;
}

static int largest_free_block(unsigned *paragraphs)
{
    union REGS regs = {0};
    regs.h.ah = 0x48;
    regs.x.bx = 0xffffU;
    intdos(&regs, &regs);
    if (!regs.x.cflag) {
        _dos_freemem(regs.x.ax);
        return 0;
    }
    if (regs.x.ax != DOS_NO_MEMORY) return 0;
    *paragraphs = regs.x.bx;
    return 1;
}

struct screen {
    unsigned mode_and_width;
    unsigned page;
    unsigned cursor;
    unsigned cursor_shape;
    unsigned first_cell;
};

static int read_screen(struct screen *screen)
{
    union REGS regs = {0};
    unsigned segment;
    unsigned offset;
    regs.h.ah = 0x0f;
    int86(0x10, &regs, &regs);
    screen->mode_and_width = regs.x.ax;
    screen->page = regs.h.bh;
    switch (regs.h.al & 0x7f) {
    case 2:
    case 3:
        segment = 0xb800U;
        break;
    case 7:
        segment = 0xb000U;
        break;
    default:
        return 0;
    }
    offset = *(const unsigned __far *)MK_FP(0x40, 0x4e);
    screen->first_cell = *(const volatile unsigned __far *)MK_FP(segment, offset);
    regs.h.ah = 3;
    int86(0x10, &regs, &regs);
    screen->cursor = regs.x.dx;
    screen->cursor_shape = regs.x.cx;
    return 1;
}

static int probe_ui(unsigned *available)
{
    struct screen before, after;
    unsigned segment;
    unsigned after_free;
    int ok;
    if (!read_screen(&before) ||
        !largest_free_block(available) ||
        _dos_allocmem(DOS_SAVE_PARAGRAPHS, &segment) != 0) return 0;
    ok = dosui_smoke(segment, "CK Basic");
    if (_dos_freemem(segment) != 0) return 0;
    return ok && read_screen(&after) &&
           before.mode_and_width == after.mode_and_width &&
           before.page == after.page && before.cursor == after.cursor &&
           before.cursor_shape == after.cursor_shape &&
           before.first_cell == after.first_cell &&
           largest_free_block(&after_free) && after_free == *available;
}

static int error_message(const char *message, int status)
{
    write_text(DOS_STDERR, message);
    return status;
}

static int exit_status(const char *value)
{
    unsigned result = 0;
    if (!*value) return -1;
    while (*value >= '0' && *value <= '9') {
        result = result * 10U + (unsigned)(*value++ - '0');
        if (result > 255U) return -1;
    }
    return *value ? -1 : (int)result;
}

/* /EDITOR is serviced by the standalone DOS editor. Prefer the installed
 * copy so the child can locate its own settings and help beside it. */
static int editor_command(char *cursor)
{
    static const char installed[] = "C:\\DOS\\EDIT.COM";
    static const char local[] = "EDIT.COM";
    const char *argv[PSP_TAIL_MAX / 2 + 2];
    unsigned count = 1;
    int status;
    for (;;) {
        const char *token = run_argument(&cursor);
        if (!token || count >= sizeof(argv) / sizeof(*argv))
            return error_message("Usage: QBASIC /EDITOR [file] [/B] [/G] [/H] [/NOHI]\r\n", 1);
        if (!*token) break;
        argv[count++] = token;
    }
    argv[count] = NULL;
    argv[0] = installed;
    status = spawnv(P_WAIT, installed, argv);
    if (status == -1) {
        argv[0] = local;
        status = spawnvp(P_WAIT, local, argv);
    }
    return status == -1 ? error_message("QBASIC: Cannot execute EDIT.COM\r\n", 1) : status;
}

int main(void)
{
    char *cursor = tail;
    char *mode, *first, *second;
    int status, mbf;
    unsigned available, i;
    static char memory[] = "FREE PARAGRAPHS: 0000\r\n";
    if (read_tail()) {
        while (is_space(*cursor)) ++cursor;
        if (*cursor != '/') {
            first = run_argument(&cursor);
            if (first && !*argument(&cursor)) return ckb_ide(first);
            return error_message("Usage: QBASIC [source.bas]\r\n", 1);
        }
        mode = argument(&cursor);
        if (strcmp(mode, "/?") == 0) {
            write_text(DOS_STDOUT,
                "Starts CK Basic (QBASIC.EXE), the CK-DOS BASIC programming environment.\r\n"
                "Copyright (c) 2026 C. Klukas. All rights reserved. Clean-room MIT implementation.\r\n\r\n"
                "QBASIC [/B] [/EDITOR] [/G] [/H] [/MBF] [/NOHI] [[/RUN] [drive:][path]filename]\r\n");
            return 0;
        }
        mbf = stricmp(mode, "/MBF") == 0;
        if (mbf) mode = argument(&cursor);
        if (stricmp(mode, "/RUN") == 0) {
            first = run_argument(&cursor);
            if (first && *first && !*argument(&cursor))
                return ckb_run(first, mbf);
            return error_message("Usage: QBASIC /RUN source.bas\r\n", 1);
        }
        if (mbf)
            return error_message("Usage: QBASIC /MBF /RUN source.bas\r\n", 1);
        if (stricmp(mode, "/EDITOR") == 0)
            return editor_command(cursor);
        first = argument(&cursor);
        second = argument(&cursor);
        if (stricmp(mode, "/EXIT") == 0 && !*second) {
            status = exit_status(first);
            if (status >= 0) return status;
        }
        if (stricmp(mode, "/PROBE") == 0 && *first && *second &&
            !*argument(&cursor)) {
            if (!check_environment())
                return error_message("CKBASIC: expected CKBASIC_WP02=ready\r\n", 2);
            if (!copy_file(first, second))
                return error_message("CKBASIC: file I/O failed\r\n", 3);
            if (!probe_ui(&available))
                return error_message("CKBASIC: DOSUI or memory check failed\r\n", 4);
            for (i = 0; i < 4U; ++i) {
                memory[20U - i] = "0123456789ABCDEF"[available & 0xfU];
                available >>= 4;
            }
            if (!write_text(DOS_STDOUT, "CKBASIC WP02 OK\r\n") ||
                !write_text(DOS_STDOUT, memory)) return 5;
            return 0;
        }
    }
    return error_message("Usage: QBASIC [source.bas] | /RUN source.bas | /EDITOR [file]\r\n", 1);
}
