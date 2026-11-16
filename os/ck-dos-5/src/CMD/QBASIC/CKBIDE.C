/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#include "CKBIDE.H"
#include "CKBINI.H"
#include "CKBHELP.H"
#include "CKBFIND.H"
#include "CKBPROC.H"
#include "CKBRUN.H"

#include <ctype.h>
#include <direct.h>
#include <dos.h>
#ifdef __WATCOMC__
#include <i86.h>
#else
#include <dos.h>
#endif
#include <malloc.h>
#include <stdio.h>
#include <string.h>

/* DOSUI owns keyboard/mouse acquisition, video save/restore and drawing. */
extern void __far ui_init(unsigned);
extern void __far ui_done(void);
extern void __far ui_fill(unsigned, unsigned, unsigned, unsigned);
extern void __far ui_write(unsigned, unsigned, const char *);
extern void __far ui_menu(const unsigned char *);
extern void __far ui_cursor(unsigned);
extern unsigned long __far ui_event(void);
#ifdef __WATCOMC__
#pragma aux ui_init "CKBIDE_INIT" parm [ax] modify exact []
#pragma aux ui_done "CKBIDE_DONE" modify exact []
#pragma aux ui_fill "CKBIDE_FILL" parm [dx] [ax] [bx] [cx] modify exact []
#pragma aux ui_write "CKBIDE_WRITE" parm [dx] [bx] [si] modify exact []
#pragma aux ui_menu "CKBIDE_MENU" parm [si] modify exact []
#pragma aux ui_cursor "CKBIDE_CURSOR" parm [dx] modify exact []
#pragma aux ui_event "CKBIDE_EVENT" value [dx ax] modify exact [dx ax]
#endif

#define SOURCE_LIMIT 60000U
#define PATH_SIZE 128
#define SCREEN_SAVE_PARAGRAPHS 600 /* DOSUI_SAVE_PARAGRAPHS */
#define VIEW_HEIGHT 18
#define SPLIT_HEIGHT 8
#define VIEW_WIDTH 78
#define TITLE 0x71
#define GRAY 0x70
#define SELECTED 0x07
#define STATUS 0x3f
#define GUTTER 0x30
#define VERTICAL 0xb3
#define HORIZONTAL 0xc4
#define POS(row, col) (((unsigned)(row) << 8) | (unsigned)(col))
#define MOUSE_EVENT 0x10000UL
#define SHIFT_HELD 0x03000000UL
#define NO_MARK 0xffffU

#define KEY_UP 0x4800
#define KEY_DOWN 0x5000
#define KEY_LEFT 0x4b00
#define KEY_RIGHT 0x4d00
#define KEY_HOME 0x4700
#define KEY_END 0x4f00
#define KEY_PREV 0x4900
#define KEY_NEXT 0x5100
#define KEY_INSERT 0x5200
#define KEY_DELETE 0x5300
#define KEY_CTRL_INSERT 0x9200
#define KEY_ALT_D 0x2000
#define KEY_ALT_E 0x1200
#define KEY_ALT_F 0x2100
#define KEY_ALT_O 0x1800
#define KEY_ALT_R 0x1300
#define KEY_ALT_S 0x1f00
#define KEY_ALT_V 0x2f00
#define KEY_F2 0x3c00
#define KEY_F3 0x3d00
#define KEY_F4 0x3e00
#define KEY_F5 0x3f00
#define KEY_F6 0x4000
#define KEY_F8 0x4200
#define KEY_F9 0x4300
#define KEY_F10 0x4400
#define KEY_SHIFT_F5 0x5800
#define KEY_SHIFT_F6 0x5900
#define KEY_ALT 0x3800
#define FIND_LIMIT 31
#define PROCEDURE_LIMIT 256
#define PROCEDURE_NAME_LIMIT 40
#define BREAKPOINT_LIMIT 128

enum run_mode { RUN_NORMAL, RUN_STEP, RUN_PROCEDURE_STEP };
enum debug_command { DEBUG_STEP, DEBUG_PROCEDURE_STEP, DEBUG_TRACE,
                     DEBUG_TOGGLE, DEBUG_CLEAR, DEBUG_NEXT, DEBUG_CANCEL };

enum view_focus { TOP_VIEW, BOTTOM_VIEW, IMMEDIATE_VIEW };

struct source_view {
    unsigned caret, top, line, anchor;
    unsigned long left, column, preferred;
    int procedure;
};

struct workspace {
    char __far *text;
    char __far *clipboard;
    char immediate[2][CKB_IMMEDIATE_LIMIT + 1];
    unsigned immediate_row, immediate_col, immediate_line;
    unsigned __far *output;
    unsigned output_cursor;
    unsigned break_at, break_width;
    unsigned breakpoints[BREAKPOINT_LIMIT], breakpoint_count;
    unsigned statement_depth, step_depth;
    enum run_mode run_mode;
    unsigned error_at, error_width, error_scroll_line;
    int exit_requested, restart_requested;
    unsigned length, caret, top, line, anchor, clip_length;
    CkbProcedure procedures[PROCEDURE_LIMIT];
    unsigned procedure_count, module_resume;
    unsigned resume[PROCEDURE_LIMIT], resume_count;
    int procedure;
    struct ckb_ini_display display;
    struct source_view other_view;
    unsigned long left, column, preferred;
    int dirty, display_dirty, split;
    enum view_focus focus, source_focus;
    char path[PATH_SIZE];
    char error[64];
    char find_pattern[FIND_LIMIT + 1], change_text[FIND_LIMIT + 1];
    unsigned find_flags;
};

static void fill(unsigned row, unsigned col, unsigned ch, unsigned attr, unsigned count)
{
    ui_fill(POS(row, col), ch, attr, count);
}

static void put(unsigned row, unsigned col, unsigned attr, const char *text)
{
    ui_write(POS(row, col), attr, text);
}

static unsigned start_of(const struct workspace *w, unsigned at)
{
    while (at && w->text[at - 1] != '\n') --at;
    return at;
}

static unsigned end_of(const struct workspace *w, unsigned at)
{
    while (at < w->length && w->text[at] != '\n') ++at;
    return at;
}

static unsigned breakpoint_index(const struct workspace *w, unsigned first)
{
    unsigned i;
    for (i = 0; i < w->breakpoint_count; ++i)
        if (w->breakpoints[i] == first) break;
    return i;
}

static void toggle_breakpoint(struct workspace *w)
{
    unsigned first = start_of(w, w->caret);
    unsigned i = breakpoint_index(w, first);
    if (i < w->breakpoint_count) w->breakpoints[i] = w->breakpoints[--w->breakpoint_count];
    else if (first < w->length && w->breakpoint_count < BREAKPOINT_LIMIT)
        w->breakpoints[w->breakpoint_count++] = first;
}

static int procedure_at(const struct workspace *w, unsigned at)
{
    return ckb_proc_at(w->procedures, w->procedure_count, at);
}

static unsigned module_start(const struct workspace *w, unsigned at)
{
    int procedure;
    while ((procedure = procedure_at(w, at)) >= 0)
        at = w->procedures[procedure].end;
    return at;
}

static unsigned view_start(const struct workspace *w)
{
    if (w->procedure >= 0) return w->procedures[w->procedure].start;
    return module_start(w, 0);
}

static unsigned view_next(const struct workspace *w, unsigned at)
{
    unsigned end = end_of(w, at);
    if (end == w->length) return NO_MARK;
    if (w->procedure < 0) return module_start(w, end + 1);
    return end + 1 < w->procedures[w->procedure].end ? end + 1 : NO_MARK;
}

static unsigned view_previous(const struct workspace *w, unsigned at)
{
    if (at == view_start(w)) return NO_MARK;
    while (at) {
        int procedure;
        at = start_of(w, at - 1);
        if (w->procedure >= 0 || (procedure = procedure_at(w, at)) < 0) return at;
        at = w->procedures[procedure].start;
    }
    return NO_MARK;
}

static unsigned view_last(const struct workspace *w)
{
    unsigned end = w->procedure < 0 ? w->length : w->procedures[w->procedure].end;
    if (w->procedure >= 0 && end && w->text[end - 1] == '\n') --end;
    return end;
}

static void select_view(struct workspace *w, int procedure)
{
    w->procedure = procedure;
    w->caret = w->top = view_start(w);
    w->left = w->column = w->line = w->preferred = 0;
    w->anchor = NO_MARK;
}

static unsigned view_height(const struct workspace *w)
{
    return w->split ? SPLIT_HEIGHT : VIEW_HEIGHT;
}

static struct source_view snapshot_view(const struct workspace *w)
{
    struct source_view view;
    view.caret = w->caret;
    view.top = w->top;
    view.line = w->line;
    view.anchor = w->anchor;
    view.left = w->left;
    view.column = w->column;
    view.preferred = w->preferred;
    view.procedure = w->procedure;
    return view;
}

static void restore_view(struct workspace *w, const struct source_view *view)
{
    w->caret = view->caret;
    w->top = view->top;
    w->line = view->line;
    w->anchor = view->anchor;
    w->left = view->left;
    w->column = view->column;
    w->preferred = view->preferred;
    w->procedure = view->procedure;
}

static unsigned resume_slot(const struct workspace *w, int procedure)
{
    unsigned i;
    for (i = 0; i < w->resume_count; ++i)
        if (w->resume[i] != NO_MARK && procedure_at(w, w->resume[i]) == procedure)
            return i;
    return w->resume_count;
}

static void procedure_name(const struct workspace *w, unsigned index, char *name)
{
    const CkbProcedure *procedure = &w->procedures[index];
    unsigned len = procedure->name_length;
    if (len > PROCEDURE_NAME_LIMIT) len = PROCEDURE_NAME_LIMIT;
    _fmemcpy(name, w->text + procedure->name, len);
    name[len] = '\0';
}

static unsigned at_column(const struct workspace *w, unsigned start, unsigned long target)
{
    unsigned width = end_of(w, start) - start;
    return start + (target < width ? (unsigned)target : width);
}

static void locate(struct workspace *w)
{
    unsigned at, line_start, last;
    int procedure = procedure_at(w, w->caret);
    if (w->procedure >= 0 && w->caret == view_last(w))
        procedure = w->procedure;
    if (procedure != w->procedure) {
        w->procedure = procedure;
        w->top = view_start(w);
        w->left = 0;
    }
    w->line = 0;
    last = view_start(w);
    line_start = start_of(w, w->caret);
    if (w->caret == last && line_start < last) line_start = last;
    w->column = w->caret - line_start;
    if (w->top < last || w->top > line_start ||
        (w->procedure < 0 && procedure_at(w, w->top) >= 0)) w->top = line_start;
    for (at = last; at < line_start; at = view_next(w, at)) {
        ++w->line;
        if (w->line >= view_height(w)) last = view_next(w, last);
    }
    if (w->top < last) w->top = last;
    if (w->column < w->left) w->left = w->column;
    if (w->column - w->left >= VIEW_WIDTH) w->left = w->column - VIEW_WIDTH + 1;
}

static unsigned caret_row(const struct workspace *w)
{
    unsigned at = w->top, row = w->source_focus == BOTTOM_VIEW ? 12 : 2;
    unsigned line = start_of(w, w->caret);
    while (at < line) {
        at = view_next(w, at);
        ++row;
    }
    return row;
}

static void position(const struct workspace *w)
{
    if (w->focus == IMMEDIATE_VIEW)
        ui_cursor(POS(22 + w->immediate_row, 1 + w->immediate_col % VIEW_WIDTH));
    else ui_cursor(POS(caret_row(w), 1 + w->column - w->left));
}

static void focus_view(struct workspace *w, enum view_focus focus)
{
    if (focus != IMMEDIATE_VIEW && focus != w->source_focus) {
        struct source_view old = snapshot_view(w);
        restore_view(w, &w->other_view);
        w->other_view = old;
        w->source_focus = focus;
        locate(w);
    }
    w->focus = focus;
}

static void cycle_view(struct workspace *w, int backwards)
{
    enum view_focus next;
    if (!w->split) next = w->focus == IMMEDIATE_VIEW ? TOP_VIEW : IMMEDIATE_VIEW;
    else next = (w->focus + (backwards ? 2 : 1)) % 3;
    focus_view(w, next);
}

static void toggle_split(struct workspace *w)
{
    if (w->split) {
        w->split = 0;
        w->source_focus = TOP_VIEW;
        if (w->focus != IMMEDIATE_VIEW) w->focus = TOP_VIEW;
    } else {
        struct source_view old = snapshot_view(w);
        w->split = 1;
        select_view(w, w->procedure);
        w->other_view = snapshot_view(w);
        restore_view(w, &old);
    }
    locate(w);
}

static void recenter(struct workspace *w)
{
    w->breakpoint_count = 0;
    w->procedure_count = ckb_proc_index(w->text, w->length, w->procedures,
                                         PROCEDURE_LIMIT);
    select_view(w, -1);
    w->other_view = snapshot_view(w);
    w->module_resume = w->caret;
    w->resume_count = 0;
    w->dirty = 0;
    w->error[0] = '\0';
}

static const char *file_title(const struct workspace *w)
{
    const char *p, *name = w->path;
    if (!*name) return "Untitled";
    for (p = name; *p; ++p) if (*p == '\\' || *p == '/' || *p == ':') name = p + 1;
    return name;
}

static unsigned selection(const struct workspace *w, unsigned *first)
{
    unsigned end, i;
    *first = w->caret;
    if (w->anchor == NO_MARK) return 0;
    end = w->anchor > w->caret ? w->anchor : w->caret;
    if (w->anchor < w->caret) *first = w->anchor;
    for (i = 0; w->procedure < 0 && i < w->procedure_count; ++i) {
        const CkbProcedure *p = &w->procedures[i];
        if (*first < p->end && end > p->start) {
            *first = w->caret;
            return 0;
        }
    }
    return end - *first;
}

static void display_line(const struct workspace *w, unsigned row, unsigned first, unsigned last)
{
    char text[VIEW_WIDTH + 1];
    unsigned n = 0, select_first, select_last;
    unsigned paint_first = VIEW_WIDTH, paint_last = 0, start = first;
    unsigned attr = w->display.text_attribute;
    int breakpoint = breakpoint_index(w, start) < w->breakpoint_count;
    unsigned selected = (attr >> 4) | ((attr & 15) << 4);
    unsigned long col = 0;
    if (breakpoint) {
        attr = w->display.breakpoint_attribute;
        fill(row, 1, ' ', attr, VIEW_WIDTH);
    }
    select_last = selection(w, &select_first);
    select_last += select_first;
    while (first < last && n < VIEW_WIDTH) {
        int marked = first >= select_first && first < select_last;
        unsigned char ch = w->text[first++];
        if (ch < 32 || ch == 127) ch = ' ';
        if (col >= w->left) {
            if (marked) {
                if (paint_first == VIEW_WIDTH) paint_first = n;
                paint_last = n + 1;
            }
            text[n++] = ch;
        }
        ++col;
    }
    text[n] = '\0';
    put(row, 1, attr, text);
    if (paint_last) {
        text[paint_last] = '\0';
        put(row, 1 + paint_first, selected, text + paint_first);
    }
    if (last < w->length && last >= select_first && last < select_last &&
        n < VIEW_WIDTH)
        fill(row, 1 + n, ' ', selected, VIEW_WIDTH - n);
    if (w->break_width && w->break_at >= start && w->break_at <= last) {
        unsigned left = w->break_at - start, right = left + w->break_width;
        unsigned current = w->display.current_attribute;
        if (right > w->left && left < w->left + VIEW_WIDTH) {
            left = left < w->left ? 0 : left - (unsigned)w->left;
            right -= (unsigned)w->left;
            if (right > VIEW_WIDTH) right = VIEW_WIDTH;
            if (right > n) {
                memset(text + n, ' ', right - n);
                n = right;
            }
            text[n] = '\0';
            text[right] = '\0';
            put(row, 1 + left, breakpoint ? (attr & 0xf0) | (current & 15) : current,
                text + left);
        }
    }
    if (w->error_width && w->error_at >= start + w->left &&
        w->error_at - start - w->left < n) {
        unsigned left = w->error_at - start - (unsigned)w->left;
        unsigned right = left + w->error_width;
        if (right > n) right = n;
        text[right] = '\0';
        put(row, 1 + left, TITLE, text + left);
    }
}

static void center(unsigned row, unsigned left, unsigned width, unsigned attr, const char *text)
{
    unsigned len = strlen(text);
    unsigned col = left + (width - len) / 2;
    put(row, col, attr, text);
}

static void workspace_status(const struct workspace *w, const char *menu)
{
    char coordinates[12];
    fill(24, 0, ' ', STATUS, 62);
    if (menu) put(24, 1, STATUS, menu);
    else if (w->error[0]) put(24, 1, STATUS, w->error);
    else if (w->focus == IMMEDIATE_VIEW)
        put(24, 1, STATUS, "<Shift+F1=Help> <F6=Window> <Enter=Execute Line>");
    else if (w->break_width)
        put(24, 1, STATUS, "<Shift+F1=Help> <F5=Continue> <F9=Toggle Bkpt> <F8=Step>");
    else put(24, 1, STATUS, "<Shift+F1=Help> <F6=Window> <F2=Subs> <F5=Run> <F8=Step>");
    fill(24, 62, ' ', GUTTER, 18);
    fill(24, 62, VERTICAL, GUTTER, 1);
    if (w->focus == IMMEDIATE_VIEW)
        sprintf(coordinates, "%05u:%03u", w->immediate_line + 1, w->immediate_col + 1);
    else sprintf(coordinates, "%05u:%03u", w->line + 1, (unsigned)((w->column + 1) % 1000));
    put(24, 70, GUTTER, coordinates);
}

static void pane_gutter(unsigned row, unsigned attr)
{
    fill(row, 0, ' ', attr, 80);
    fill(row, 0, VERTICAL, attr, 1);
    fill(row, 79, VERTICAL, attr, 1);
}

static void draw_source(const struct workspace *w, unsigned header,
                        unsigned height, int active)
{
    unsigned row, i, bottom = header + height, last = 0, at = w->top;
    unsigned attr = w->display.text_attribute;
    int bars = active && w->display.scroll_bars;
    char title[76], name[PROCEDURE_NAME_LIMIT + 1];
    fill(header, 0, HORIZONTAL, attr, 80);
    fill(header, 0, header == 1 ? 0xda : 0xc3, attr, 1);
    fill(header, 79, header == 1 ? 0xbf : 0xb4, attr, 1);
    if (w->procedure >= 0) {
        procedure_name(w, w->procedure, name);
        sprintf(title, " %.20s:%.40s ", file_title(w), name);
    } else sprintf(title, " %.40s ", file_title(w));
    center(header, 0, 79, active ? TITLE : attr, title);
    put(header, 75, attr, "\xb4\x18\xc3");
    fill(header, 76, 0x18, TITLE, 1);
    for (row = header + 1; row <= bottom; ++row) {
        fill(row, 0, ' ', attr, 80);
        fill(row, 0, VERTICAL, attr, 1);
        if (at != NO_MARK) {
            display_line(w, row, at, end_of(w, at));
            at = view_next(w, at);
        }
        if (bars)
            fill(row, 79, row == header + 1 ? 0x18 : row == bottom ? 0x19 : 0xb0, GRAY, 1);
        else fill(row, 79, VERTICAL, attr, 1);
    }
    if (bars) {
        for (i = view_start(w); i != NO_MARK; i = view_next(w, i)) ++last;
        if (w->procedure < 0 && last) --last;
        row = header + 2;
        if (last) row += (unsigned)((height - 3UL) *
                                    (w->error_width ? w->error_scroll_line : w->line) / last);
        fill(row, 79, ' ', SELECTED, 1);
        pane_gutter(bottom + 1, attr);
        fill(bottom + 1, 1, 0x1b, GRAY, 1);
        fill(bottom + 1, 2, 0xb0, GRAY, 76);
        fill(bottom + 1, 2, ' ', SELECTED, 1);
        fill(bottom + 1, 78, 0x1a, GRAY, 1);
    } else {
        pane_gutter(bottom + 1, attr);
        if (w->split && at != NO_MARK)
            display_line(w, bottom + 1, at, end_of(w, at));
    }
}

static void draw_workspace(struct workspace *w)
{
    unsigned attr = w->display.text_attribute;
    unsigned i;
    fill(0, 0, ' ', GRAY, 80);
    put(0, 2, GRAY, "File  Edit  View  Search  Run  Debug  Options");
    put(0, 74, GRAY, "Help");
    fill(0, 2, 'F', 0x7f, 1);
    if (w->split) {
        struct source_view active = snapshot_view(w);
        draw_source(w, w->source_focus == TOP_VIEW ? 1 : 11, SPLIT_HEIGHT,
                    w->focus == w->source_focus);
        restore_view(w, &w->other_view);
        draw_source(w, w->source_focus == TOP_VIEW ? 11 : 1, SPLIT_HEIGHT, 0);
        restore_view(w, &active);
    } else draw_source(w, 1, VIEW_HEIGHT, w->focus == TOP_VIEW);
    fill(21, 0, HORIZONTAL, attr, 80);
    fill(21, 0, 0xc3, attr, 1);
    fill(21, 79, 0xb4, attr, 1);
    center(21, 0, 79, w->focus == IMMEDIATE_VIEW ? TITLE : attr, " Immediate ");
    for (i = 0; i < 2; ++i) {
        char line[VIEW_WIDTH + 1];
        unsigned left = w->immediate_col / VIEW_WIDTH * VIEW_WIDTH;
        unsigned length = strlen(w->immediate[i]);
        if (i != w->immediate_row || w->focus != IMMEDIATE_VIEW) left = 0;
        if (left > length) left = length;
        pane_gutter(22 + i, attr);
        sprintf(line, "%.78s", w->immediate[i] + left);
        put(22 + i, 1, attr, line);
    }
    workspace_status(w, NULL);
}

static void set_error(struct workspace *w, const char *message)
{
    strcpy(w->error, message);
}

/* Read into a second segment so a failed Open leaves the current source intact. */
static int load_source(struct workspace *w, const char *path)
{
    FILE *file;
    char __far *text;
    unsigned length = 0;
    int ch, cr = 0;
    if (strlen(path) >= PATH_SIZE) {
        set_error(w, "File name too long");
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        set_error(w, "Cannot open source file");
        return 0;
    }
    text = _fmalloc(SOURCE_LIMIT + 1U);
    if (!text) {
        fclose(file);
        set_error(w, "Not enough memory to open source");
        return 0;
    }
    while ((ch = fgetc(file)) != EOF && ch != 26) {
        if (ch == '\n' && cr) {
            cr = 0;
            continue;
        }
        if (!ch || length == SOURCE_LIMIT) break;
        cr = ch == '\r';
        text[length++] = cr ? '\n' : ch == '\t' ? ' ' : ch;
    }
    if (ferror(file) || (ch != EOF && ch != 26)) {
        fclose(file);
        _ffree(text);
        set_error(w, "Source is unreadable, binary or too large");
        return 0;
    }
    fclose(file);
    if (w->text) _ffree(w->text);
    w->text = text;
    w->length = length;
    strcpy(w->path, path);
    recenter(w);
    return 1;
}

static int write_source_range(const struct workspace *w, FILE *file,
                              unsigned at, unsigned end)
{
    int leading = 1;
    for (; at < end; ++at) {
        unsigned char ch = w->text[at];
        if (leading && ch == ' ') {
            unsigned count = 1;
            while (count < w->display.tab_width && at + count < end &&
                   w->text[at + count] == ' ') ++count;
            if (count == w->display.tab_width) {
                if (fputc('\t', file) == EOF) break;
                at += count - 1;
                continue;
            }
        }
        if (ch == '\n' && fputc('\r', file) == EOF) break;
        if (fputc(ch, file) == EOF) break;
        leading = ch == '\n';
    }
    return at == end;
}

static int write_program(struct workspace *w, FILE *file)
{
    unsigned i, at, end, last = NO_MARK;
    int active = w->procedure, ok = 1;
    if (!w->procedure_count) {
        if (!write_source_range(w, file, 0, w->length)) return 0;
        if (w->length && w->text[w->length - 1] != '\n' &&
            fputs("\r\n", file) == EOF) return 0;
        return (w->length >= 2 && w->text[w->length - 2] == '\n' &&
                w->text[w->length - 1] == '\n') || fputs("\r\n", file) != EOF;
    }
    w->procedure = -1;
    for (at = view_start(w); at != NO_MARK; at = view_next(w, at))
        if (end_of(w, at) != at) last = at;
    if (last != NO_MARK) {
        for (at = view_start(w); ok && at <= last; at = view_next(w, at)) {
            end = end_of(w, at);
            ok = write_source_range(w, file, at, end) && fputs("\r\n", file) != EOF;
        }
        if (ok) ok = fputs("\r\n", file) != EOF;
    }
    w->procedure = active;
    for (i = 0; ok && i < w->procedure_count; ++i) {
        const CkbProcedure *p = &w->procedures[i];
        end = p->end;
        while (end > p->start && w->text[end - 1] == '\n') --end;
        ok = write_source_range(w, file, p->start, end) &&
             fputs("\r\n\r\n", file) != EOF;
    }
    return ok;
}

static int save_source(struct workspace *w, const char *path)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (!file) {
        set_error(w, "Cannot create source file");
        return 0;
    }
    ok = write_program(w, file);
    if (fclose(file) || !ok) {
        set_error(w, "Error writing source file");
        return 0;
    }
    if (w->path != path) strcpy(w->path, path);
    w->dirty = 0;
    return 1;
}

enum file_command { NEW, OPEN, SAVE, SAVE_AS, EXIT, NO_COMMAND };
static const char *const commands[] = {"New", "Open...", "Save", "Save As...", "Exit"};
static const unsigned command_rows[] = {2, 3, 4, 5, 9};
static const unsigned accelerators[] = {0, 0, 0, 5, 1};

static enum file_command file_menu(struct workspace *w)
{
    static const unsigned char box[] = {1, 1, 10, 18, GRAY, GRAY};
    unsigned selected = 0, i, attr;
    unsigned long event;
    for (;;) {
        draw_workspace(w);
        put(0, 2, SELECTED, " File ");
        fill(0, 3, 'F', 0x0f, 1);
        ui_menu(box);
        for (i = 0; i < NO_COMMAND; ++i) {
            attr = selected == i ? SELECTED : GRAY;
            fill(command_rows[i], 2, ' ', attr, 16);
            put(command_rows[i], 3, attr, commands[i]);
            fill(command_rows[i], 3 + accelerators[i], commands[i][accelerators[i]],
                 selected == i ? 0x0f : 0x7f, 1);
        }
        workspace_status(w, "F1=Help \xb3 Removes currently loaded file from memory");
        ui_cursor(0xffff);
        event = ui_event();
        if (event & MOUSE_EVENT) {
            unsigned col = event & 255, row = (event >> 8) & 255;
            if (col > 1 && col < 18)
                for (i = 0; i < NO_COMMAND; ++i)
                    if (row == command_rows[i]) return (enum file_command)i;
            return NO_COMMAND;
        }
        switch ((unsigned)event) {
        case KEY_UP: selected = (selected + NO_COMMAND - 1) % NO_COMMAND; break;
        case KEY_DOWN: selected = (selected + 1) % NO_COMMAND; break;
        case KEY_END: selected = EXIT; break;
        case KEY_HOME: selected = NEW; break;
        case KEY_ALT: case KEY_ALT_F: return NO_COMMAND;
        default:
            switch (tolower((unsigned char)event)) {
            case '\r': return (enum file_command)selected;
            case 'n': return NEW;
            case 'o': return OPEN;
            case 's': return SAVE;
            case 'a': return SAVE_AS;
            case 'x': return EXIT;
            case 27: return NO_COMMAND;
            }
        }
    }
}

static void draw_dialog(const unsigned char *box, const char *title)
{
    ui_menu(box);
    center(box[0], box[1] + 1, box[3] - box[1] - 1, GRAY, title);
}

static void display_frame(unsigned top, unsigned left, unsigned bottom, unsigned right)
{
    unsigned row;
    fill(top, left, HORIZONTAL, GRAY, right - left + 1);
    fill(bottom, left, HORIZONTAL, GRAY, right - left + 1);
    fill(top, left, 0xda, GRAY, 1);
    fill(top, right, 0xbf, GRAY, 1);
    fill(bottom, left, 0xc0, GRAY, 1);
    fill(bottom, right, 0xd9, GRAY, 1);
    for (row = top + 1; row < bottom; ++row) {
        fill(row, left, VERTICAL, GRAY, 1);
        fill(row, right, VERTICAL, GRAY, 1);
    }
}

enum display_focus { DISPLAY_PREVIEW, DISPLAY_FG, DISPLAY_BG, DISPLAY_SCROLL,
                     DISPLAY_WIDTH, DISPLAY_OK, DISPLAY_CANCEL, DISPLAY_HELP,
                     DISPLAY_FOCUS_COUNT };

struct display_dialog {
    struct ckb_ini_display values;
    unsigned group, focus, width, digits, foreground_top;
    int replace_width;
};

static unsigned *display_attr(struct display_dialog *d)
{
    switch (d->group) {
    case 1: return &d->values.current_attribute;
    case 2: return &d->values.breakpoint_attribute;
    default: return &d->values.text_attribute;
    }
}

static unsigned display_colour(unsigned attr, unsigned focus)
{
    return focus == DISPLAY_FG ? attr & 15 : (attr >> 4) & 7;
}

static void reveal_foreground(struct display_dialog *d, unsigned colour)
{
    if (colour < d->foreground_top) d->foreground_top = colour;
    if (colour >= d->foreground_top + 8) d->foreground_top = colour - 7;
}

static void choose_display_group(struct display_dialog *d, unsigned group)
{
    d->group = group;
    reveal_foreground(d, display_colour(*display_attr(d), DISPLAY_FG));
}

static void display_list(unsigned column, unsigned colour, unsigned first)
{
    static const char *const names[] = {
        "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "White",
        "Gray", "BrBlue", "BrGreen", "BrCyan", "BrRed", "Pink", "Yellow", "BrWhite"
    };
    unsigned row, thumb = 1 + first * 5 / 8;
    display_frame(4, column - 1, 13, column + 9);
    for (row = 0; row < 8; ++row) {
        unsigned attr = colour == first + row ? SELECTED : GRAY;
        fill(5 + row, column, ' ', attr, 9);
        put(5 + row, column + 1, attr, names[first + row]);
        fill(5 + row, column + 9,
             row == 0 ? 0x18 : row == 7 ? 0x19 : row == thumb ? ' ' : 0xb0,
             row == thumb ? SELECTED : GRAY, 1);
    }
}

static void draw_display(struct workspace *w, struct display_dialog *d)
{
    static const unsigned char box[] = {1, 10, 22, 69, GRAY, GRAY};
    static const char *const groups[] = {"1.  Normal Text", "2.  Current Statement",
                                         "3.  Breakpoint Lines"};
    static const char *const buttons[] = {"< OK >", "< Cancel >", "< Help >"};
    static const unsigned cols[] = {20, 34, 52};
    unsigned active = *display_attr(d), i, width_attr;
    char width[3];
    draw_workspace(w);
    put(0, 39, SELECTED, " Options ");
    draw_dialog(box, " Display ");
    display_frame(2, 12, 15, 67);
    center(2, 13, 54, GRAY, " Colors ");
    put(3, 42, GRAY, "Foreground   Background");
    for (i = 0; i < 3; ++i) {
        unsigned attr = i == 0 ? d->values.text_attribute :
            i == 1 ? d->values.current_attribute : d->values.breakpoint_attribute;
        put(5 + 2 * i, 14, GRAY, d->group == i ? "(\x07)" : "( )");
        put(5 + 2 * i, 18, GRAY, groups[i]);
        fill(5 + 2 * i, 21, ' ', attr, 19);
        put(5 + 2 * i, 21, attr, groups[i] + 3);
    }
    display_list(42, display_colour(active, DISPLAY_FG), d->foreground_top);
    display_list(55, display_colour(active, DISPLAY_BG), 0);
    display_frame(17, 12, 19, 67);
    center(17, 13, 54, GRAY, " Display Options ");
    put(18, 16, GRAY, d->values.scroll_bars ? "[X] Scroll Bars" : "[ ] Scroll Bars");
    put(18, 49, GRAY, "Tab Stops:");
    width[0] = 0;
    if (d->digits) sprintf(width, d->digits == 2 ? "%02u" : "%u", d->width % 100);
    width_attr = d->focus == DISPLAY_WIDTH && d->replace_width ? SELECTED : GRAY;
    put(18, 60, width_attr, width);
    fill(20, 10, 0xc3, GRAY, 1);
    fill(20, 11, HORIZONTAL, GRAY, 58);
    fill(20, 69, 0xb4, GRAY, 1);
    for (i = 0; i < 3; ++i) {
        put(21, cols[i], GRAY, buttons[i]);
        if (d->focus == DISPLAY_OK + i || (i == 0 && d->focus < DISPLAY_OK)) {
            fill(21, cols[i], '<', 0x7f, 1);
            fill(21, cols[i] + strlen(buttons[i]) - 1, '>', 0x7f, 1);
        }
    }
    fill(24, 0, ' ', STATUS, 80);
    put(24, 1, STATUS, "F1=Help   Enter=Execute   Esc=Cancel   Tab=Next Field   Arrow=Next Item");
    switch (d->focus) {
    case DISPLAY_PREVIEW: ui_cursor(POS(5 + 2 * d->group, 15)); break;
    case DISPLAY_FG: case DISPLAY_BG:
        i = display_colour(active, d->focus);
        ui_cursor(POS(5 + i - (d->focus == DISPLAY_FG ? d->foreground_top : 0),
                      d->focus == DISPLAY_FG ? 43 : 56));
        break;
    case DISPLAY_SCROLL: ui_cursor(POS(18, 17)); break;
    case DISPLAY_WIDTH: ui_cursor(POS(18, 60 + (d->replace_width ? 0 : d->digits))); break;
    default: ui_cursor(0xffff);
    }
}

static void display_select_colour(struct display_dialog *d, unsigned colour)
{
    unsigned *attr = display_attr(d);
    if (d->focus == DISPLAY_FG) {
        *attr = (*attr & 0xf0) | colour;
        reveal_foreground(d, colour);
    }
    else *attr = (*attr & 15) | (colour << 4);
}

static void show_help(struct workspace *w, unsigned topic, int menu_pending);

static void options_display(struct workspace *w)
{
    struct display_dialog d;
    unsigned long event;
    unsigned key, row, col, colour;
    char ch;
    memset(&d, 0, sizeof(d));
    d.values = w->display;
    choose_display_group(&d, 0);
    d.width = d.values.tab_width;
    d.digits = d.width >= 10 ? 2 : 1;
    for (;;) {
        int accept = 0;
        draw_display(w, &d);
        event = ui_event();
        key = (unsigned)event;
        ch = (unsigned char)key;
        if (event & MOUSE_EVENT) {
            row = key >> 8;
            col = key & 255;
            if (row >= 5 && row <= 9 && row % 2 && col >= 14 && col < 40) {
                choose_display_group(&d, (row - 5) / 2);
                d.focus = DISPLAY_PREVIEW;
            } else if (row >= 5 && row <= 12 && ((col >= 42 && col <= 50) || (col >= 55 && col <= 64))) {
                d.focus = col < 51 ? DISPLAY_FG : DISPLAY_BG;
                display_select_colour(&d, (d.focus == DISPLAY_FG ? d.foreground_top : 0) + row - 5);
            } else if (row == 18 && col >= 16 && col < 32) {
                d.focus = DISPLAY_SCROLL;
                d.values.scroll_bars = !d.values.scroll_bars;
            } else if (row == 18 && col >= 49 && col <= 62) {
                d.focus = DISPLAY_WIDTH;
                d.replace_width = 1;
            } else if (row == 21 && col >= 20 && col <= 25) accept = 1;
            else if (row == 21 && col >= 34 && col <= 43) return;
            else if (row == 21 && col >= 52 && col <= 59) show_help(w, CKB_HELP_USING, 0);
        } else if (ch == 27) return;
        else if (ch == '\r') {
            if (d.focus == DISPLAY_CANCEL) return;
            if (d.focus == DISPLAY_HELP) show_help(w, CKB_HELP_USING, 0);
            else accept = 1;
        } else if (key == 0x3b00) show_help(w, CKB_HELP_USING, 0);
        else if (ch == '\t' || key == 0x0f00) {
            d.focus = (d.focus + (key == 0x0f00 ? DISPLAY_FOCUS_COUNT - 1 : 1)) % DISPLAY_FOCUS_COUNT;
            d.replace_width = d.focus == DISPLAY_WIDTH;
        } else if (d.focus == DISPLAY_PREVIEW && (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT)) {
            choose_display_group(&d, (d.group + (key == KEY_UP || key == KEY_LEFT ? 2 : 1)) % 3);
        } else if (d.focus == DISPLAY_PREVIEW && ch >= '1' && ch <= '3') choose_display_group(&d, ch - '1');
        else if (d.focus == DISPLAY_FG || d.focus == DISPLAY_BG) {
            unsigned max = d.focus == DISPLAY_FG ? 15 : 7;
            colour = display_colour(*display_attr(&d), d.focus);
            if (key == KEY_HOME) colour = 0;
            else if (key == KEY_END) colour = max;
            else if (key == KEY_UP && colour) --colour;
            else if (key == KEY_DOWN && colour < max) ++colour;
            else if (key == KEY_PREV) colour = colour < 8 ? 0 : colour - 8;
            else if (key == KEY_NEXT) colour = colour + 8 > max ? max : colour + 8;
            display_select_colour(&d, colour);
        } else if (d.focus == DISPLAY_SCROLL && ch == ' ') d.values.scroll_bars = !d.values.scroll_bars;
        else if (d.focus == DISPLAY_WIDTH && (ch == '\b' || key == KEY_DELETE || (ch >= '0' && ch <= '9'))) {
            if (d.replace_width || key == KEY_DELETE) d.width = d.digits = 0;
            else if (ch == '\b' && d.digits) { d.width /= 10; --d.digits; }
            if (ch >= '0' && ch <= '9' && d.digits < 2) {
                d.width = 10 * d.width + ch - '0';
                ++d.digits;
            }
            d.replace_width = 0;
        }
        if (accept) {
            if (d.width) d.values.tab_width = d.width;
            w->display = d.values;
            w->display_dirty = 1;
            locate(w);
            return;
        }
    }
}

static void options_menu(struct workspace *w)
{
    static const unsigned char box[] = {1, 39, 5, 57, GRAY, GRAY};
    unsigned long event;
    unsigned key, i, selected = 0;
    static const char *const items[] = {"Display...", "Help Path...", "\x07Syntax Checking"};
    static const char *const status[] = {
        "F1=Help \xb3 Changes display attributes",
        "F1=Help \xb3 Changes the help path",
        "F1=Help \xb3 Checks BASIC syntax during editing"
    };
    static const unsigned accelerator[] = {0, 5, 2};
    for (;;) {
        draw_workspace(w);
        put(0, 39, SELECTED, " Options ");
        ui_menu(box);
        for (i = 0; i < 3; ++i) {
            unsigned attr = selected == i ? SELECTED : GRAY;
            fill(2 + i, 40, ' ', attr, 17);
            put(2 + i, i == 2 ? 40 : 41, attr, items[i]);
            fill(2 + i, (i == 2 ? 40 : 41) + accelerator[i],
                 items[i][accelerator[i]], selected == i ? 0x0f : 0x7f, 1);
        }
        workspace_status(w, status[selected]);
        ui_cursor(0xffff);
        event = ui_event();
        key = (unsigned)event;
        if (event & MOUSE_EVENT) {
            if ((key >> 8) == 2 && (key & 255) > 39 && (key & 255) < 57)
                options_display(w);
            return;
        }
        if (key == KEY_UP) selected = (selected + 2) % 3;
        else if (key == KEY_DOWN) selected = (selected + 1) % 3;
        else if ((unsigned char)key == 'd' || (unsigned char)key == 'D' || ((unsigned char)key == '\r' && !selected)) {
            options_display(w);
            return;
        } else if ((unsigned char)key == 27 || key == KEY_ALT || key == KEY_ALT_O) return;
    }
}

enum find_focus { FIND_FIELD, FIND_CASE, FIND_WORD, FIND_OK, FIND_CANCEL, FIND_HELP,
                  FIND_FOCUS_COUNT };

struct find_dialog {
    char text[FIND_LIMIT + 1];
    unsigned caret, flags, focus;
    int marked;
};

static void search_header(void)
{
    put(0, 20, SELECTED, " Search ");
}

static void search_message(struct workspace *w, int changed, unsigned scroll_row)
{
    static const unsigned char box[] = {9, 27, 15, 51, GRAY, GRAY};
    unsigned help = 0, key;
    unsigned long event;
    do {
        draw_workspace(w);
        if (w->display.scroll_bars && scroll_row) {
            unsigned row;
            for (row = 3; row <= 18; ++row)
                fill(row, 79, row == scroll_row ? ' ' : 0xb0,
                     row == scroll_row ? SELECTED : GRAY, 1);
        }
        ui_menu(box);
        center(11, 28, 23, GRAY, changed ? "Change complete" : "Match not found");
        fill(13, 27, 0xc3, GRAY, 1);
        fill(13, 28, HORIZONTAL, GRAY, 23);
        fill(13, 51, 0xb4, GRAY, 1);
        put(14, 30, GRAY, "<  OK  >   < Help >");
        if (changed) fill(14, 43, 'H', 0x7f, 1);
        fill(14, help ? 41 : 30, '<', 0x7f, 1);
        fill(14, help ? 48 : 37, '>', 0x7f, 1);
        fill(24, 0, ' ', STATUS, 80);
        put(24, 1, STATUS, "F1=Help   Enter=Execute   Esc=Cancel   Tab=Next Field   Arrow=Next Item");
        ui_cursor(POS(14, help ? 43 : 33));
        event = ui_event();
        key = (unsigned)event;
        if (event & MOUSE_EVENT) {
            if ((key >> 8) == 14 && (key & 255) >= 30 && (key & 255) <= 37) return;
            if ((key >> 8) == 14 && (key & 255) >= 41 && (key & 255) <= 48)
                show_help(w, CKB_HELP_USING, 0);
        } else if ((unsigned char)key == 27) return;
        else if ((unsigned char)key == '\t' || key == 0x0f00 || key == KEY_LEFT || key == KEY_RIGHT)
            help = !help;
        else if (key == 0x3b00 || ((unsigned char)key == '\r' && help))
            show_help(w, CKB_HELP_USING, 0);
        else if ((unsigned char)key == '\r') return;
    } while (1);
}

static unsigned find_word_at(const struct workspace *w, char *text)
{
    unsigned start = w->caret, length = 0;
    unsigned char ch = start < w->length ? w->text[start] : 0;
    if (!isalnum(ch) && ch != '.' && ch != '/') {
        *text = 0;
        return 0;
    }
    while (start) {
        ch = w->text[start - 1];
        if (!isalnum(ch) && ch != '.' && ch != '/') break;
        --start;
    }
    while (start + length < w->length && length < FIND_LIMIT) {
        ch = w->text[start + length];
        if (!isalnum(ch) && ch != '.' && ch != '/') break;
        text[length++] = ch;
    }
    text[length] = 0;
    return length;
}

static void find_next(struct workspace *w)
{
    unsigned found, start = w->caret;
    if (!*w->find_pattern && !find_word_at(w, w->find_pattern)) return;
    if (start < w->length) ++start;
    if (!ckb_find(w->text, w->length, start, w->find_pattern, w->find_flags, &found)) {
        search_message(w, 0, 0);
        return;
    }
    w->caret = found;
    w->anchor = found + strlen(w->find_pattern);
    locate(w);
    w->preferred = w->column;
}

static void draw_find(struct workspace *w, const struct find_dialog *d)
{
    static const unsigned char box[] = {7, 10, 17, 68, GRAY, GRAY};
    static const char *const buttons[] = {"< OK >", "< Cancel >", "< Help >"};
    static const unsigned cols[] = {19, 34, 52};
    unsigned i;
    draw_workspace(w);
    search_header();
    draw_dialog(box, " Find ");
    display_frame(8, 23, 10, 66);
    put(9, 12, GRAY, "Find What:");
    put(9, 24, d->marked ? SELECTED : GRAY, d->text);
    put(13, 15, GRAY, d->flags & CKB_FIND_MATCH_CASE ?
        "[X] Match Upper/Lowercase" : "[ ] Match Upper/Lowercase");
    put(13, 48, GRAY, d->flags & CKB_FIND_WHOLE_WORD ? "[X] Whole Word" : "[ ] Whole Word");
    fill(15, 10, 0xc3, GRAY, 1);
    fill(15, 11, HORIZONTAL, GRAY, 57);
    fill(15, 68, 0xb4, GRAY, 1);
    for (i = 0; i < 3; ++i) {
        put(16, cols[i], GRAY, buttons[i]);
        if (d->focus == FIND_OK + i || (i == 0 && d->focus < FIND_OK)) {
            fill(16, cols[i], '<', 0x7f, 1);
            fill(16, cols[i] + strlen(buttons[i]) - 1, '>', 0x7f, 1);
        }
    }
    fill(24, 0, ' ', STATUS, 80);
    put(24, 1, STATUS, "F1=Help   Enter=Execute   Esc=Cancel   Tab=Next Field   Arrow=Next Item");
    switch (d->focus) {
    case FIND_FIELD: ui_cursor(POS(9, 24 + d->caret)); break;
    case FIND_CASE: ui_cursor(POS(13, 16)); break;
    case FIND_WORD: ui_cursor(POS(13, 49)); break;
    default: ui_cursor(0xffff);
    }
}

static void edit_search_field(char *text, unsigned *caret, int *marked, unsigned key)
{
    unsigned length = strlen(text), at = *caret;
    unsigned char ch = (unsigned char)key;
    switch (key) {
    case KEY_LEFT: if (at) --at; break;
    case KEY_RIGHT: if (at < length) ++at; break;
    case KEY_HOME: at = 0; break;
    case KEY_END: at = length; break;
    default:
        if (ch != '\b' && key != KEY_DELETE && (ch < 32 || ch == 127)) return;
        if (*marked) text[at = length = 0] = 0;
        if (ch == '\b' && at) {
            --at;
            memmove(text + at, text + at + 1, length - at);
        } else if (key == KEY_DELETE && at < length) {
            memmove(text + at, text + at + 1, length - at);
        } else if (ch >= 32 && ch != 127 && length < FIND_LIMIT) {
            memmove(text + at + 1, text + at, length - at + 1);
            text[at++] = ch;
        }
    }
    *caret = at;
    *marked = 0;
}

static void find_prefill(const struct workspace *w, struct find_dialog *d)
{
    unsigned start, length = selection(w, &start);
    if (length) {
        if (length > FIND_LIMIT) length = FIND_LIMIT;
        _fmemcpy(d->text, w->text + start, length);
        d->text[length] = 0;
    } else if (!find_word_at(w, d->text)) strcpy(d->text, w->find_pattern);
    d->caret = strlen(d->text);
    d->marked = d->caret != 0;
}

static void find_dialog(struct workspace *w)
{
    struct find_dialog d;
    memset(&d, 0, sizeof(d));
    d.flags = w->find_flags;
    find_prefill(w, &d);
    for (;;) {
        unsigned long event;
        unsigned key, ch;
        draw_find(w, &d);
        event = ui_event();
        key = (unsigned)event;
        ch = (unsigned char)key;
        if (event & MOUSE_EVENT) {
            unsigned row = key >> 8, col = key & 255;
            if (row == 9 && col >= 24 && col < 66) {
                unsigned length = strlen(d.text);
                d.focus = FIND_FIELD;
                d.caret = col - 24 < length ? col - 24 : length;
                d.marked = 0;
            } else if (row == 13 && col >= 15 && col < 40) {
                d.focus = FIND_CASE;
                d.flags ^= CKB_FIND_MATCH_CASE;
            } else if (row == 13 && col >= 48 && col < 62) {
                d.focus = FIND_WORD;
                d.flags ^= CKB_FIND_WHOLE_WORD;
            } else if (row == 16 && col >= 19 && col < 25) {
                d.focus = FIND_OK;
                ch = '\r';
            } else if (row == 16 && col >= 34 && col < 44) return;
            else if (row == 16 && col >= 52 && col < 60) show_help(w, CKB_HELP_USING, 0);
            if (ch != '\r') continue;
        }
        if (ch == 27 || (ch == '\r' && d.focus == FIND_CANCEL)) return;
        if (key == 0x3b00 || (ch == '\r' && d.focus == FIND_HELP))
            show_help(w, CKB_HELP_USING, 0);
        else if (ch == '\r') {
            if (*d.text) {
                strcpy(w->find_pattern, d.text);
                w->find_flags = d.flags;
                find_next(w);
            }
            return;
        } else if (ch == '\t' || key == 0x0f00) {
            d.focus = (d.focus + (key == 0x0f00 ? FIND_FOCUS_COUNT - 1 : 1)) % FIND_FOCUS_COUNT;
        } else if (ch == ' ' && d.focus == FIND_CASE) d.flags ^= CKB_FIND_MATCH_CASE;
        else if (ch == ' ' && d.focus == FIND_WORD) d.flags ^= CKB_FIND_WHOLE_WORD;
        else if (d.focus == FIND_FIELD) edit_search_field(d.text, &d.caret, &d.marked, key);
    }
}

static void change_dialog(struct workspace *w);

static void search_menu(struct workspace *w)
{
    static const unsigned char box[] = {1, 19, 5, 46, GRAY, GRAY};
    static const char *const items[] = {"Find...", "Repeat Last Find      F3", "Change..."};
    static const char *const status[] = {
        "F1=Help \xb3 Finds specified text",
        "F1=Help \xb3 Repeats the last Find",
        "F1=Help \xb3 Searches for and changes text"
    };
    unsigned selected = 0;
    for (;;) {
        unsigned long event;
        unsigned i, key, ch;
        draw_workspace(w);
        search_header();
        ui_menu(box);
        for (i = 0; i < 3; ++i) {
            unsigned attr = i == selected ? SELECTED : GRAY;
            fill(2 + i, 20, ' ', attr, 26);
            put(2 + i, 21, attr, items[i]);
            fill(2 + i, 21, items[i][0], i == selected ? 0x0f : 0x7f, 1);
        }
        workspace_status(w, status[selected]);
        ui_cursor(0xffff);
        event = ui_event();
        key = (unsigned)event;
        ch = tolower((unsigned char)key);
        if (event & MOUSE_EVENT) {
            unsigned row = key >> 8, col = key & 255;
            if (row < 2 || row > 4 || col <= 19 || col >= 46) return;
            selected = row - 2;
            ch = '\r';
        }
        if (key == KEY_UP) selected = (selected + 2) % 3;
        else if (key == KEY_DOWN) selected = (selected + 1) % 3;
        else if (ch == 'f' || (ch == '\r' && !selected)) { find_dialog(w); return; }
        else if (ch == 'r' || key == KEY_F3 || (ch == '\r' && selected == 1)) { find_next(w); return; }
        else if (ch == 'c' || (ch == '\r' && selected == 2)) { change_dialog(w); return; }
        else if (ch == 27 || key == KEY_ALT || key == KEY_ALT_S) return;
    }
}

static void directory_list(unsigned row, unsigned col, unsigned width,
                           unsigned rows, const char *pattern, int dirs)
{
    struct find_t found;
    unsigned result, used = 0;
    result = _dos_findfirst(pattern, _A_SUBDIR | _A_RDONLY | _A_ARCH, &found);
    while (!result && used < rows) {
        if (!!(found.attrib & _A_SUBDIR) == dirs) {
            if (!dirs || strcmp(found.name, ".") != 0) {
                if (strlen(found.name) <= width) {
                    put(row + used, col, GRAY, found.name);
                    ++used;
                }
            }
        }
        if (used < rows) result = _dos_findnext(&found);
    }
}

static void path_dialog(struct workspace *w, int saving, const char *name,
                        unsigned caret, int marked, unsigned focus)
{
    static const unsigned char save_box[] = {3, 19, 20, 60, GRAY, GRAY};
    static const unsigned char open_box[] = {2, 6, 20, 72, GRAY, GRAY};
    const unsigned char *box = saving ? save_box : open_box;
    unsigned width = saving ? 25 : 50;
    unsigned row = box[0] + 2, left = box[1] + 14;
    unsigned first = caret >= width ? caret - width + 1 : 0;
    unsigned len = strlen(name + first);
    char clipped[51], cwd[PATH_SIZE];
    unsigned i, buttons[3];
    const char *const labels[3] = {"< OK >", "< Cancel >", "< Help >"};
    if (len > width) len = width;
    memcpy(clipped, name + first, len);
    clipped[len] = '\0';
    draw_workspace(w);
    draw_dialog(box, saving ? " Save As " : " Open ");
    fill(row, left - 1, VERTICAL, GRAY, 1);
    fill(row, left + width, VERTICAL, GRAY, 1);
    put(row, box[1] + 2, GRAY, "File Name:");
    put(row, left, marked ? SELECTED : GRAY, clipped);
    if (getcwd(cwd, sizeof(cwd))) put(row + 2, box[1] + 2, GRAY, cwd);
    if (saving) {
        put(8, 34, GRAY, "Dirs/Drives");
        directory_list(10, 24, 20, 8, "*.*", 1);
    } else {
        put(7, 28, GRAY, "Files");
        put(7, 57, GRAY, "Dirs/Drives");
        directory_list(9, 12, 12, 8, "*.BAS", 0);
        directory_list(9, 55, 12, 8, "*.*", 1);
    }
    buttons[0] = saving ? 26 : 17;
    buttons[1] = saving ? 36 : 34;
    buttons[2] = saving ? 50 : 55;
    for (i = 0; i < 3; ++i) {
        put(19, buttons[i], GRAY, labels[i]);
        if ((!focus && !i) || focus == i + 1) {
            fill(19, buttons[i], '<', 0x7f, 1);
            fill(19, buttons[i] + strlen(labels[i]) - 1, '>', 0x7f, 1);
        }
    }
    fill(24, 0, ' ', STATUS, 80);
    put(24, 1, STATUS, "F1=Help   Enter=Execute   Esc=Cancel   Tab=Next Field   Arrow=Next Item");
    if (w->error[0]) put(17, box[1] + 2, GRAY, w->error);
    ui_cursor(focus ? 0xffff : POS(row, left + caret - first));
}

static int source_extension(char *name)
{
    const char *leaf = name, *p;
    for (p = name; *p; ++p)
        if (*p == ':' || *p == '/' || *p == '\\') leaf = p + 1;
    if (!*leaf || strchr(leaf, '*') || strchr(leaf, '?')) return 0;
    if (!strchr(leaf, '.')) {
        if (strlen(name) > PATH_SIZE - 5) return 0;
        strcat(name, ".BAS");
    }
    return 1;
}

static int ask(struct workspace *w, const char *question, int cancel)
{
    static const unsigned char box[] = {8, 12, 14, 67, GRAY, GRAY};
    unsigned selected = 0;
    unsigned long event;
    for (;;) {
        draw_workspace(w);
        draw_dialog(box, " CK Basic ");
        center(10, 13, 54, GRAY, question);
        put(12, 22, selected == 0 ? SELECTED : GRAY, "< Yes >");
        put(12, 36, selected == 1 ? SELECTED : GRAY, "< No >");
        if (cancel) put(12, 49, selected == 2 ? SELECTED : GRAY, "< Cancel >");
        ui_cursor(0xffff);
        event = ui_event();
        if (event & MOUSE_EVENT) {
            unsigned col = event & 255;
            if (((event >> 8) & 255) == 12) {
                if (col >= 22 && col < 29) return 0;
                if (col >= 36 && col < 42) return 1;
                if (cancel && col >= 49 && col < 59) return 2;
            }
            continue;
        }
        if ((unsigned)event == KEY_LEFT)
            selected = (selected + 1 + !!cancel) % (2 + !!cancel);
        if ((unsigned)event == KEY_RIGHT || (unsigned char)event == '\t')
            selected = (selected + 1) % (2 + !!cancel);
        switch (tolower((unsigned char)event)) {
        case '\r': return selected;
        case 'y': return 0;
        case 'n': return 1;
        case 27: return 2;
        }
    }
}

static int choose_path(struct workspace *w, int saving)
{
    char name[PATH_SIZE], path[PATH_SIZE];
    unsigned length, caret, focus = 0, ch;
    unsigned long event;
    int marked = 1;
    strcpy(name, saving ? w->path : "*.BAS");
    length = caret = strlen(name);
    w->error[0] = '\0';
    for (;;) {
        path_dialog(w, saving, name, caret, marked, focus);
        event = ui_event();
        if (event & MOUSE_EVENT) {
            unsigned row = (event >> 8) & 255, col = event & 255;
            unsigned ok = saving ? 26 : 17, cancel = saving ? 36 : 34;
            if (row == (saving ? 5U : 4U)) focus = 0;
            if (row != 19) continue;
            if (col >= cancel && col < cancel + 10) break;
            if (col < ok || col >= ok + 6) continue;
            focus = 1;
            event = '\r';
        }
        ch = (unsigned char)event;
        if (ch == 27 || (ch == '\r' && focus == 2)) break;
        if (ch == '\t') {
            focus = (focus + 1) % 4;
            continue;
        }
        if (ch == '\r' && focus != 3) {
            strcpy(path, name);
            if (!source_extension(path)) set_error(w, "Enter a file name");
            else if (saving) {
                FILE *existing = fopen(path, "rb");
                if (existing) {
                    fclose(existing);
                    if (ask(w, "Replace the existing file?", 0) != 0) continue;
                }
                if (save_source(w, path)) return 1;
            } else if (load_source(w, path)) return 1;
            continue;
        }
        if (focus) continue;
        switch ((unsigned)event) {
        case KEY_LEFT: if (caret) --caret; marked = 0; break;
        case KEY_RIGHT: if (caret < length) ++caret; marked = 0; break;
        case KEY_HOME: caret = 0; marked = 0; break;
        case KEY_END: caret = length; marked = 0; break;
        default:
            if (ch == '\b' || (unsigned)event == KEY_DELETE || ch >= 32) {
                if (marked) {
                    name[0] = '\0';
                    length = caret = 0;
                    marked = 0;
                }
                if (ch == '\b' && caret) {
                    --caret;
                    --length;
                    memmove(name + caret, name + caret + 1, length - caret + 1);
                } else if ((unsigned)event == KEY_DELETE && caret < length) {
                    --length;
                    memmove(name + caret, name + caret + 1, length - caret + 1);
                } else if (ch >= 32 && length < PATH_SIZE - 1) {
                    memmove(name + caret + 1, name + caret, length - caret + 1);
                    name[caret++] = toupper(ch);
                    ++length;
                }
                w->error[0] = '\0';
            }
        }
    }
    w->error[0] = '\0';
    return 0;
}

static int confirm_discard(struct workspace *w)
{
    if (!w->dirty) return 1;
    switch (ask(w, "Save changes to the current source?", 1)) {
    case 0: return w->path[0] ? save_source(w, w->path) : choose_path(w, 1);
    case 1: return 1;
    default: return 0;
    }
}

static unsigned edited_offset(unsigned at, unsigned first, unsigned removed,
                              unsigned length)
{
    if (at == NO_MARK || at < first) return at;
    return at >= first + removed ? at - removed + length : first;
}

static int replace(struct workspace *w, unsigned first, unsigned removed,
                   const char __far *text, unsigned length)
{
    if (length > SOURCE_LIMIT - (w->length - removed)) {
        set_error(w, "Source buffer is full");
        return 0;
    }
    if (removed || length) {
        unsigned i, kept = 0;
        w->module_resume = edited_offset(w->module_resume, first, removed, length);
        for (i = 0; i < w->resume_count; ++i)
            w->resume[i] = edited_offset(w->resume[i], first, removed, length);
        if (w->top > first) {
            if (w->top >= first + removed) w->top = w->top - removed + length;
            else w->top = start_of(w, first);
        }
        _fmemmove(w->text + first + length, w->text + first + removed,
                  w->length - first - removed);
        if (length) _fmemcpy(w->text + first, text, length);
        w->length = w->length - removed + length;
        w->dirty = 1;
        for (i = 0; i < w->breakpoint_count; ++i) {
            unsigned at = w->breakpoints[i];
            if (at >= first && at < first + removed &&
                (first != at || !length)) continue;
            at = start_of(w, edited_offset(at, first, removed, length));
            w->breakpoints[kept++] = at;
        }
        w->breakpoint_count = kept;
        w->procedure_count = ckb_proc_index(w->text, w->length, w->procedures,
                                             PROCEDURE_LIMIT);
        w->procedure = procedure_at(w, first);
        if (w->split) {
            struct source_view active = snapshot_view(w);
            restore_view(w, &w->other_view);
            w->caret = edited_offset(w->caret, first, removed, length);
            w->top = start_of(w, edited_offset(w->top, first, removed, length));
            w->anchor = edited_offset(w->anchor, first, removed, length);
            w->procedure = procedure_at(w, w->caret);
            locate(w);
            w->other_view = snapshot_view(w);
            restore_view(w, &active);
        }
    }
    w->caret = first;
    w->anchor = NO_MARK;
    return 1;
}

enum change_focus { CHANGE_FIND, CHANGE_TEXT, CHANGE_CASE, CHANGE_WORD,
                    CHANGE_VERIFY, CHANGE_ALL, CHANGE_CANCEL, CHANGE_HELP,
                    CHANGE_FOCUS_COUNT };

struct change_dialog {
    struct find_dialog find;
    char replacement[FIND_LIMIT + 1];
    unsigned caret, focus;
    int marked;
};

static void search_status(void)
{
    fill(24, 0, ' ', STATUS, 80);
    put(24, 1, STATUS, "F1=Help   Enter=Execute   Esc=Cancel   Tab=Next Field   Arrow=Next Item");
}

static void dim_verify_status(void)
{
    static const char text[] = " F1=Help   Enter=Execute   Esc=Cancel   Tab=Next Field   Arrow=Next Item";
    unsigned i;
    for (i = 15; i <= 67; ++i) fill(24, i, text[i], 0x08, 1);
}

static void search_separator(unsigned row, unsigned left, unsigned right)
{
    fill(row, left, 0xc3, GRAY, 1);
    fill(row, left + 1, HORIZONTAL, GRAY, right - left - 1);
    fill(row, right, 0xb4, GRAY, 1);
}

static void search_button(unsigned row, unsigned col, const char *label, int active)
{
    put(row, col, GRAY, label);
    if (active) {
        fill(row, col, '<', 0x7f, 1);
        fill(row, col + strlen(label) - 1, '>', 0x7f, 1);
    }
}

#define SUBS_ROWS 9

static void open_source_view(struct workspace *w, int procedure)
{
    unsigned at = start_of(w, w->caret), slot, restore;
    if (w->procedure < 0) w->module_resume = at;
    else {
        slot = resume_slot(w, w->procedure);
        if (slot == w->resume_count && slot < PROCEDURE_LIMIT) ++w->resume_count;
        if (slot < w->resume_count) w->resume[slot] = at;
    }
    select_view(w, procedure);
    if (procedure < 0) {
        restore = start_of(w, w->module_resume);
        if (procedure_at(w, restore) < 0) w->caret = restore;
    } else {
        slot = resume_slot(w, procedure);
        if (slot < w->resume_count) w->caret = start_of(w, w->resume[slot]);
    }
    locate(w);
}

static void draw_subs(struct workspace *w, unsigned selected,
                      unsigned top, unsigned focus)
{
    static const unsigned char box[] = {2, 3, 19, 76, GRAY, GRAY};
    char name[PROCEDURE_NAME_LIMIT + 1], description[72];
    unsigned row, index, attr;
    draw_workspace(w);
    draw_dialog(box, " SUBs ");
    put(4, 5, GRAY, "Choose program item to edit");
    display_frame(5, 5, 15, 74);
    for (row = 6; row <= 14; ++row) {
        index = top + row - 6;
        if (index > w->procedure_count) break;
        attr = index == selected ? SELECTED : GRAY;
        if (index == selected) fill(row, 6, ' ', attr, 23);
        if (index) procedure_name(w, index - 1, name);
        else sprintf(name, "%.40s", file_title(w));
        put(row, index ? 9 : 7, attr, name);
    }
    fill(15, 6, 0x1b, GRAY, 1);
    fill(15, 7, 0xb0, GRAY, 66);
    fill(15, 7, ' ', SELECTED, 1);
    fill(15, 73, 0x1a, GRAY, 1);
    if (selected) {
        procedure_name(w, selected - 1, name);
        sprintf(description, "%.32s is a %s in %.20s", name,
                w->procedures[selected - 1].kind == CKB_PROC_FUNCTION
                    ? "FUNCTION" : "SUB", file_title(w));
    } else sprintf(description, "%.40s is the Main Module", file_title(w));
    put(16, 5, GRAY, description);
    search_separator(17, 3, 76);
    search_button(18, 9, "< Edit in Active >", focus <= 1);
    search_button(18, 32, "< Delete >", focus == 2);
    search_button(18, 48, "< Cancel >", focus == 3);
    search_button(18, 63, "< Help >", focus == 4);
    search_status();
    ui_cursor(POS(focus ? 18 : 6 + selected - top,
                  focus == 1 ? 11 : focus == 2 ? 34 : focus == 3 ? 50 :
                  focus == 4 ? 65 : 7));
}

static void subs_dialog(struct workspace *w)
{
    unsigned selected = 0, top = 0, focus = 0;
    for (;;) {
        unsigned long event;
        unsigned key, ch;
        draw_subs(w, selected, top, focus);
        event = ui_event();
        key = (unsigned)event;
        ch = (unsigned char)key;
        if (event & MOUSE_EVENT) {
            unsigned row = key >> 8, col = key & 255;
            if (row >= 6 && row <= 14 && col >= 6 && col < 74 &&
                top + row - 6 <= w->procedure_count) {
                selected = top + row - 6;
                focus = 0;
            } else if (row == 18) {
                if (col >= 9 && col <= 26) focus = 1;
                else if (col >= 32 && col <= 41) focus = 2;
                else if (col >= 48 && col <= 57) focus = 3;
                else if (col >= 63 && col <= 70) focus = 4;
                else continue;
                ch = '\r';
            } else continue;
        }
        if (ch == 27 || (ch == '\r' && focus == 3)) return;
        if (key == 0x3b00 || (ch == '\r' && focus == 4))
            show_help(w, CKB_HELP_USING, 0);
        else if (ch == '\r' && focus <= 1) {
            w->focus = w->source_focus;
            open_source_view(w, (int)selected - 1);
            return;
        } else if (ch == '\r' && focus == 2 && selected) {
            const CkbProcedure *p = &w->procedures[selected - 1];
            if (!ask(w, "Delete selected procedure?", 1)) {
                replace(w, p->start, p->end - p->start, NULL, 0);
                select_view(w, -1);
                return;
            }
        } else if (ch == '\t' || key == 0x0f00)
            focus = (focus + (key == 0x0f00 ? 4 : 1)) % 5;
        else if (!focus) {
            switch (key) {
            case KEY_UP: if (selected) --selected; break;
            case KEY_DOWN: if (selected < w->procedure_count) ++selected; break;
            case KEY_HOME: selected = 0; break;
            case KEY_END: selected = w->procedure_count; break;
            case KEY_PREV: selected = selected >= SUBS_ROWS ? selected - SUBS_ROWS : 0; break;
            case KEY_NEXT:
                selected += SUBS_ROWS;
                if (selected > w->procedure_count) selected = w->procedure_count;
                break;
            }
        }
        if (selected < top) top = selected;
        if (selected >= top + SUBS_ROWS) top = selected - SUBS_ROWS + 1;
    }
}

/* DOSUI keeps the IDE in mode 3 on page zero. The output page holds program
 * cells while the editor is visible; the DOS console writes to the live page. */
static void restore_output(const struct workspace *w)
{
    unsigned row;
    if (w->output)
        _fmemcpy(MK_FP(0xb800, 0), w->output, 4000);
    else for (row = 0; row < 25; ++row) fill(row, 0, ' ', SELECTED, 80);
    ui_cursor(w->output_cursor);
}

static void hide_output_cursor(unsigned position)
{
    union REGS regs = {0};
    ui_cursor(position);
    regs.x.ax = 0x0100;
    regs.x.cx = 0x2707;
    int86(0x10, &regs, &regs);
}

static void output_screen(const struct workspace *w)
{
    restore_output(w);
    if (w->output) hide_output_cursor(w->output_cursor);
    ui_event();
}

static void capture_output(struct workspace *w)
{
    union REGS regs = {0};
    regs.x.ax = 0x0300;
    int86(0x10, &regs, &regs);
    w->output_cursor = regs.x.dx;
    _fmemcpy(w->output, MK_FP(0xb800, 0), 4000);
}

static int prepare_output(struct workspace *w)
{
    unsigned i;
    if (w->output) return 1;
    w->output = _fmalloc(4000);
    if (!w->output) {
        set_error(w, "Not enough memory for program output");
        return 0;
    }
    for (i = 0; i < 2000; ++i) w->output[i] = 0x0720;
    return 1;
}

static void acknowledge_output(void)
{
    fill(24, 0, ' ', SELECTED, 80);
    put(24, 0, SELECTED, "Press any key to continue");
    hide_output_cursor(POS(24, 25));
    ui_event();
}

static unsigned run_source_position(const struct workspace *w, unsigned line, unsigned column)
{
    unsigned at = 0, row = 1;
    while (at < w->length && row < line)
        if (w->text[at++] == '\n') ++row;
    return at_column(w, at, column ? column - 1 : 0);
}

static void run_error_dialog(struct workspace *w, const char *message)
{
    unsigned char box[] = {9, 27, 15, 51, GRAY, GRAY};
    char label[59];
    unsigned long event;
    unsigned length = strlen(message);
    int help = 0;
    if (length > 16) {
        unsigned width = length > 53 ? 59 : length + 6;
        if (width < 25) width = 25;
        box[1] = (80 - width) / 2;
        box[3] = box[1] + width - 1;
    }
    sprintf(label, "%.53s", message);
    for (;;) {
        unsigned key, ch;
        draw_workspace(w);
        ui_menu(box);
        center(11, box[1] + 1, box[3] - box[1] - 1, GRAY, label);
        search_separator(13, box[1], box[3]);
        search_button(14, 30, "<  OK  >", !help);
        search_button(14, 41, "< Help >", help);
        search_status();
        ui_cursor(POS(14, help ? 43 : 33));
        event = ui_event();
        key = (unsigned)event;
        ch = (unsigned char)key;
        if (event & MOUSE_EVENT) {
            if ((key >> 8) != 14) continue;
            if ((key & 255) >= 30 && (key & 255) <= 37) return;
            if ((key & 255) < 41 || (key & 255) > 48) continue;
            show_help(w, CKB_HELP_USING, 0);
        } else if (ch == 27 || (ch == '\r' && !help)) return;
        else if (ch == '\t' || key == 0x0f00 || key == KEY_LEFT || key == KEY_RIGHT)
            help = !help;
        else if (key == 0x3b00 || ch == '\r') show_help(w, CKB_HELP_USING, 0);
    }
}

static void next_immediate_line(struct workspace *w)
{
    if (w->immediate_row) {
        strcpy(w->immediate[0], w->immediate[1]);
        w->immediate[1][0] = '\0';
    } else w->immediate_row = 1;
    ++w->immediate_line;
    w->immediate_col = 0;
}

static void execute_immediate(struct workspace *w)
{
    CkbImmediateResult result;
    char *command = w->immediate[w->immediate_row];
    if (!*command) {
        next_immediate_line(w);
        return;
    }
    if (!prepare_output(w)) return;
    restore_output(w);
    ckb_immediate(command, w->output_cursor & 255, &result);
    fflush(stdout);
    capture_output(w);
    if (result.error) {
        if (result.runtime_error) w->immediate_col = 0;
        run_error_dialog(w, result.error);
        if (!result.runtime_error) next_immediate_line(w);
    } else {
        if (result.wrote_output) acknowledge_output();
        next_immediate_line(w);
    }
}

static void edit_immediate(struct workspace *w, unsigned key)
{
    char *command = w->immediate[w->immediate_row];
    unsigned length = strlen(command), col = w->immediate_col;
    unsigned char ch = key;
    switch (key) {
    case KEY_LEFT: if (col) --col; break;
    case KEY_RIGHT: if (col < length) ++col; break;
    case KEY_HOME: col = 0; break;
    case KEY_END: col = length; break;
    case KEY_DELETE:
        if (col < length) memmove(command + col, command + col + 1, length - col);
        break;
    default:
        if (ch == '\r') {
            execute_immediate(w);
            return;
        }
        if (ch == '\b' && col) {
            memmove(command + col - 1, command + col, length - col + 1);
            --col;
        } else if (ch >= 32 && ch != 127 && length < CKB_IMMEDIATE_LIMIT) {
            memmove(command + col + 1, command + col, length - col + 1);
            command[col++] = ch;
        }
    }
    w->immediate_col = col;
}

static enum debug_command debug_menu(struct workspace *w)
{
    static const unsigned char box[] = {1, 32, 10, 62, GRAY, GRAY};
    static const char *const labels[] = {
        " Step                     F8 ", " Procedure Step          F10 ",
        " Trace On                    ", " Toggle Breakpoint        F9 ",
        " Clear All Breakpoints       ", " Set Next Statement          "
    };
    static const char *const status[] = {
        "F1=Help \xb3 Executes next program statement",
        "F1=Help \xb3 Executes next statement, stepping over procedures",
        "F1=Help \xb3 Displays statements as they execute",
        "F1=Help \xb3 Sets or removes a breakpoint on current line",
        "F1=Help \xb3 Removes all breakpoints",
        "F1=Help \xb3 Sets the next statement to execute"
    };
    static const unsigned rows[] = {2, 3, 5, 7, 8, 9};
    static const unsigned mnemonic[] = {1, 1, 1, 8, 1, 1};
    unsigned selected = 0, i;
    for (;;) {
        unsigned long event;
        unsigned key;
        draw_workspace(w);
        put(0, 33, SELECTED, " Debug ");
        ui_menu(box);
        search_separator(4, 32, 62);
        search_separator(6, 32, 62);
        for (i = 0; i < DEBUG_CANCEL; ++i) {
            unsigned attr = i == selected ? SELECTED : GRAY;
            unsigned col = mnemonic[i];
            if (i == DEBUG_NEXT) put(rows[i], 34, 0x78, "Set Next Statement");
            else {
                put(rows[i], 33, attr, labels[i]);
                fill(rows[i], 33 + col, labels[i][col], i == selected ? 0x0f : 0x7f, 1);
            }
        }
        workspace_status(w, status[selected]);
        ui_cursor(0xffff);
        event = ui_event();
        key = (unsigned)event;
        if (event & MOUSE_EVENT) {
            unsigned row = key >> 8, col = key & 255;
            if (col > 32 && col < 62)
                for (i = 0; i < DEBUG_NEXT; ++i)
                    if (row == rows[i]) return (enum debug_command)i;
            return DEBUG_CANCEL;
        }
        if (key == KEY_UP) selected = (selected + DEBUG_NEXT - 1) % DEBUG_NEXT;
        else if (key == KEY_DOWN) selected = (selected + 1) % DEBUG_NEXT;
        else if (key == KEY_HOME) selected = DEBUG_STEP;
        else if (key == KEY_END) selected = DEBUG_CLEAR;
        else if (key == KEY_ALT || key == KEY_ALT_D || (unsigned char)key == 27) return DEBUG_CANCEL;
        else if (key == KEY_F8) return DEBUG_STEP;
        else if (key == KEY_F10) return DEBUG_PROCEDURE_STEP;
        else if (key == KEY_F9) return DEBUG_TOGGLE;
        else {
            switch (tolower((unsigned char)key)) {
            case '\r': return (enum debug_command)selected;
            case 's': return DEBUG_STEP;
            case 'p': return DEBUG_PROCEDURE_STEP;
            case 't': return DEBUG_TRACE;
            case 'b': return DEBUG_TOGGLE;
            case 'c': return DEBUG_CLEAR;
            }
        }
    }
}

static int pause_workspace(void *context, unsigned line, unsigned column,
                           unsigned width, const char *message)
{
    struct workspace *w = context;
    unsigned original_line = w->line;
    int original_procedure = w->procedure;
    if (fflush(stdout) == EOF) return 0;
    capture_output(w);
    w->caret = run_source_position(w, line, column);
    w->anchor = NO_MARK;
    w->focus = w->source_focus;
    locate(w);
    w->preferred = w->column;
    if (message) {
        unsigned end = w->caret;
        while (end < w->length && !isspace((unsigned char)w->text[end]) &&
               w->text[end] != ':') ++end;
        w->error_at = w->caret;
        w->error_width = end - w->caret;
        w->error_scroll_line = original_procedure == w->procedure ? original_line : w->line;
        run_error_dialog(w, message);
        w->error_width = 0;
    }
    w->break_at = w->caret;
    w->break_width = width;
    for (;;) {
        unsigned long event;
        unsigned key;
        draw_workspace(w);
        position(w);
        event = ui_event();
        key = (unsigned)event;
        if (key == KEY_ALT_D || ((event & MOUSE_EVENT) && (key >> 8) == 0 &&
                                (key & 255) >= 33 && (key & 255) <= 38)) {
            enum debug_command command = debug_menu(w);
            if (command == DEBUG_STEP) key = KEY_F8;
            else if (command == DEBUG_PROCEDURE_STEP) key = KEY_F10;
            else if (command == DEBUG_TOGGLE) key = KEY_F9;
            else {
                if (command == DEBUG_CLEAR) w->breakpoint_count = 0;
                continue;
            }
        } else if (event & MOUSE_EVENT) continue;
        if (key == KEY_F4) output_screen(w);
        else if (key == KEY_F6 || key == KEY_SHIFT_F6) cycle_view(w, key == KEY_SHIFT_F6);
        else if (w->focus == IMMEDIATE_VIEW && key != KEY_F5 && key != KEY_SHIFT_F5 &&
                 key != KEY_F8 && key != KEY_F10 && key != KEY_ALT_F)
            edit_immediate(w, key);
        else if (key == KEY_F9) toggle_breakpoint(w);
        else if (key == KEY_F5 || key == KEY_F8 || key == KEY_F10) {
            w->run_mode = key == KEY_F8 ? RUN_STEP :
                          key == KEY_F10 ? RUN_PROCEDURE_STEP : RUN_NORMAL;
            w->step_depth = w->statement_depth;
            restore_output(w);
            break;
        } else if (key == KEY_SHIFT_F5) {
            w->restart_requested = 1;
            break;
        } else if (key == KEY_ALT_F && file_menu(w) == EXIT && confirm_discard(w)) {
            w->exit_requested = 1;
            break;
        }
    }
    w->break_width = 0;
    return !w->restart_requested && !w->exit_requested;
}

static int should_break(void *context, unsigned line_start, int first_statement,
                        unsigned depth)
{
    struct workspace *w = context;
    w->statement_depth = depth;
    if (w->run_mode == RUN_STEP ||
        (w->run_mode == RUN_PROCEDURE_STEP && depth <= w->step_depth)) return 1;
    return first_statement && breakpoint_index(w, line_start) < w->breakpoint_count;
}

static void run_workspace(struct workspace *w, enum run_mode mode)
{
    CkbRunResult result;
    if (!prepare_output(w)) return;
    do {
        w->restart_requested = 0;
        w->run_mode = mode;
        w->step_depth = w->statement_depth = 0;
        restore_output(w);
        ckb_run_source(w->path[0] ? w->path : "Untitled", w->text, w->length,
                       &result, pause_workspace, should_break, w);
        mode = RUN_NORMAL;
    } while (w->restart_requested);
    if (result.aborted) return;
    capture_output(w);
    if (result.error) {
        if (result.editor_source && result.line) {
            w->caret = run_source_position(w, result.line, result.column);
            w->anchor = NO_MARK;
            locate(w);
            w->preferred = w->column;
        }
        run_error_dialog(w, result.error);
        return;
    }
    acknowledge_output();
}

static void dispatch_debug(struct workspace *w)
{
    switch (debug_menu(w)) {
    case DEBUG_STEP: run_workspace(w, RUN_STEP); break;
    case DEBUG_PROCEDURE_STEP: run_workspace(w, RUN_PROCEDURE_STEP); break;
    case DEBUG_TOGGLE: toggle_breakpoint(w); break;
    case DEBUG_CLEAR: w->breakpoint_count = 0; break;
    default: break;
    }
}

static void run_menu(struct workspace *w)
{
    static const unsigned char box[] = {1, 27, 5, 49, GRAY, GRAY};
    static const char *const labels[] = {
        " Start      Shift+F5 ", " Restart             ", " Continue         F5 "
    };
    static const char *const status[] = {
        "F1=Help \xb3 Runs current program",
        "F1=Help \xb3 Restarts current program",
        "F1=Help \xb3 Continues running current program"
    };
    unsigned selected = 0, i;
    for (;;) {
        unsigned long event;
        unsigned key;
        draw_workspace(w);
        put(0, 28, SELECTED, " Run ");
        ui_menu(box);
        for (i = 0; i < 3; ++i) {
            unsigned attr = i == selected ? SELECTED : GRAY;
            put(2 + i, 28, attr, labels[i]);
            fill(2 + i, 29, labels[i][1],
                 i == selected ? 0x0f : 0x7f, 1);
        }
        workspace_status(w, status[selected]);
        ui_cursor(0xffff);
        event = ui_event();
        key = (unsigned)event;
        if (event & MOUSE_EVENT) {
            unsigned row = key >> 8, col = key & 255;
            if (row < 2 || row > 4 || col < 28 || col > 48) return;
            key = '\r';
        }
        if (key == KEY_UP) selected = (selected + 2) % 3;
        else if (key == KEY_DOWN) selected = (selected + 1) % 3;
        else if (key == KEY_HOME) selected = 0;
        else if (key == KEY_END) selected = 2;
        else if (key == KEY_ALT || key == KEY_ALT_R || (unsigned char)key == 27) return;
        else if (key == KEY_F5 || key == KEY_SHIFT_F5 || (unsigned char)key == '\r' ||
                 tolower((unsigned char)key) == 's' ||
                 tolower((unsigned char)key) == 'r' || tolower((unsigned char)key) == 'c') {
            run_workspace(w, RUN_NORMAL);
            return;
        }
    }
}

static void view_menu(struct workspace *w)
{
    static const unsigned char box[] = {1, 13, 5, 37, GRAY, GRAY};
    static const char *const labels[] = {
        " SUBs...            F2 ", " Split                 ",
        " Output Screen      F4 "
    };
    static const char *const status[] = {
        "F1=Help \xb3 Displays a loaded SUB or FUNCTION",
        "F1=Help \xb3 Divides screen into two View windows",
        "F1=Help \xb3 Displays output screen"
    };
    static const unsigned accelerators[] = {1, 2, 1};
    unsigned selected = 0, i;
    for (;;) {
        unsigned long event;
        unsigned key;
        draw_workspace(w);
        put(0, 14, SELECTED, " View ");
        ui_menu(box);
        for (i = 0; i < 3; ++i) {
            unsigned attr = i == selected ? SELECTED : GRAY;
            put(2 + i, 14, attr, labels[i]);
            fill(2 + i, 14 + accelerators[i], labels[i][accelerators[i]],
                 i == selected ? 0x0f : 0x7f, 1);
        }
        workspace_status(w, status[selected]);
        ui_cursor(0xffff);
        event = ui_event();
        key = (unsigned)event;
        if (event & MOUSE_EVENT) {
            unsigned row = key >> 8, col = key & 255;
            if (row < 2 || row > 4 || col < 14 || col > 36) return;
            selected = row - 2;
            key = '\r';
        }
        if (key == KEY_UP) selected = (selected + 2) % 3;
        else if (key == KEY_DOWN) selected = (selected + 1) % 3;
        else if (key == KEY_HOME) selected = 0;
        else if (key == KEY_END) selected = 2;
        else if (key == KEY_ALT || key == KEY_ALT_V || (unsigned char)key == 27) return;
        else if (key == KEY_F2 || tolower((unsigned char)key) == 's' ||
                 ((unsigned char)key == '\r' && selected == 0)) {
            subs_dialog(w);
            return;
        } else if (key == KEY_F4 || tolower((unsigned char)key) == 'o' ||
                   ((unsigned char)key == '\r' && selected == 2)) {
            output_screen(w);
            return;
        } else if ((unsigned char)key == '\r' || tolower((unsigned char)key) == 'p') {
            toggle_split(w);
            return;
        }
    }
}

static void draw_change(struct workspace *w, const struct change_dialog *d)
{
    static const unsigned char box[] = {5, 11, 18, 68, GRAY, GRAY};
    static const char *const buttons[] = {"< Find and Verify >", "< Change All >", "< Cancel >", "< Help >"};
    static const unsigned cols[] = {13, 33, 48, 59};
    unsigned i;
    draw_workspace(w);
    search_header();
    draw_dialog(box, " Change ");
    display_frame(6, 24, 8, 66);
    display_frame(9, 24, 11, 66);
    put(7, 13, GRAY, "Find What:");
    put(10, 13, GRAY, "Change To:");
    put(7, 25, d->find.marked ? SELECTED : GRAY, d->find.text);
    put(10, 25, d->marked ? SELECTED : GRAY, d->replacement);
    put(14, 16, GRAY, d->find.flags & CKB_FIND_MATCH_CASE ?
        "[X] Match Upper/Lowercase" : "[ ] Match Upper/Lowercase");
    put(14, 48, GRAY, d->find.flags & CKB_FIND_WHOLE_WORD ? "[X] Whole Word" : "[ ] Whole Word");
    search_separator(16, 11, 68);
    for (i = 0; i < 4; ++i)
        search_button(17, cols[i], buttons[i],
                      d->focus == CHANGE_VERIFY + i || (!i && d->focus < CHANGE_VERIFY));
    search_status();
    switch (d->focus) {
    case CHANGE_FIND: ui_cursor(POS(7, 25 + d->find.caret)); break;
    case CHANGE_TEXT: ui_cursor(POS(10, 25 + d->caret)); break;
    case CHANGE_CASE: ui_cursor(POS(14, 17)); break;
    case CHANGE_WORD: ui_cursor(POS(14, 49)); break;
    default: ui_cursor(POS(17, cols[d->focus - CHANGE_VERIFY] + 2));
    }
}

static int verify_change(struct workspace *w, int *accelerators)
{
    static const unsigned char box[] = {19, 13, 23, 65, GRAY, GRAY};
    static const char *const buttons[] = {"< Change >", "< Skip >", "< Cancel >", "< Help >"};
    static const unsigned cols[] = {17, 29, 41, 54};
    unsigned focus = 0;
    for (;;) {
        unsigned long event;
        unsigned i, key, ch;
        draw_workspace(w);
        draw_dialog(box, " Change ");
        search_separator(21, 13, 65);
        for (i = 0; i < 4; ++i) search_button(22, cols[i], buttons[i], focus == i);
        search_status();
        dim_verify_status();
        if (*accelerators) {
            fill(22, 19, 'C', 0x7f, 1);
            fill(22, 31, 'S', 0x7f, 1);
            fill(22, 56, 'H', 0x7f, 1);
        }
        ui_cursor(POS(22, cols[focus] + 2));
        event = ui_event();
        key = (unsigned)event;
        ch = tolower((unsigned char)key);
        if (event & MOUSE_EVENT) {
            if ((key >> 8) != 22) continue;
            for (i = 0; i < 4; ++i)
                if ((key & 255) >= cols[i] && (key & 255) < cols[i] + strlen(buttons[i])) break;
            if (i == 4) continue;
            focus = i;
            ch = '\r';
        }
        if (ch == 27) return -1;
        if (key == 0x2e00 || key == KEY_ALT_S) *accelerators = 1;
        if (ch == 'c' || ch == 'y' || key == 0x2e00) return 1;
        if (ch == 's' || ch == 'n' || key == KEY_ALT_S) return 0;
        if (ch == '\r' && focus < 3) return 1 - (int)focus;
        if (ch == '\t' || key == KEY_RIGHT) focus = (focus + 1) % 4;
        else if (key == 0x0f00 || key == KEY_LEFT) focus = (focus + 3) % 4;
        else if (key == 0x3b00 || (ch == '\r' && focus == 3)) show_help(w, CKB_HELP_USING, 0);
    }
}

static void finish_change(struct workspace *w, unsigned caret)
{
    w->caret = caret > w->length ? w->length : caret;
    w->anchor = NO_MARK;
    locate(w);
    w->preferred = w->column;
}

static void change_matches(struct workspace *w, int all)
{
    unsigned origin = w->caret, start = origin, found, boundary;
    unsigned origin_column = origin - start_of(w, origin);
    unsigned scroll_row = 0;
    unsigned from = strlen(w->find_pattern), to = strlen(w->change_text);
    unsigned last = origin, count = 0;
    int wrapped = 0, accelerators = 0;
    if (!from) return;
    if (start < w->length) ++start;
    boundary = start;
    for (;;) {
        unsigned end = wrapped ? boundary : w->length;
        int answer = 1;
        if (!ckb_find_range(w->text, w->length, start, end, w->find_pattern,
                            w->find_flags, &found)) {
            if (wrapped) break;
            wrapped = 1;
            start = 0;
            continue;
        }
        if (!all) {
            w->caret = found;
            w->anchor = found + from;
            locate(w);
            answer = verify_change(w, &accelerators);
            if (answer < 0) {
                finish_change(w, origin);
                return;
            }
        }
        start = found + from;
        if (!answer) continue;
        if (!replace(w, found, from, w->change_text, to)) {
            finish_change(w, origin);
            return;
        }
        if (wrapped) {
            boundary = boundary - (from > boundary - found ? boundary - found : from) + to;
            if (found < origin)
                origin = origin - (from > origin - found ? origin - found : from) + to;
        }
        start = found + to;
        last = found + from;
        ++count;
    }
    finish_change(w, count ? (all ? last - from : last) : origin);
    if (all) {
        unsigned lines = 0, i;
        for (i = 0; i < w->length; ++i)
            if (w->text[i] == '\n') ++lines;
        if (w->length && w->text[w->length - 1] != '\n') ++lines;
        /* The completion box leaves the thumb at the last replaced row. */
        if (count && lines) scroll_row = 3 + (unsigned)(15UL * w->line / lines);
        finish_change(w, at_column(w, start_of(w, origin), origin_column));
    }
    search_message(w, count != 0, scroll_row);
}

static void change_dialog(struct workspace *w)
{
    struct change_dialog d;
    memset(&d, 0, sizeof(d));
    strcpy(d.find.text, w->find_pattern);
    d.find.caret = strlen(d.find.text);
    d.find.marked = d.find.caret != 0;
    d.find.flags = w->find_flags;
    strcpy(d.replacement, w->change_text);
    d.caret = strlen(d.replacement);
    d.marked = d.caret != 0;
    for (;;) {
        unsigned long event;
        unsigned key, ch;
        draw_change(w, &d);
        event = ui_event();
        key = (unsigned)event;
        ch = (unsigned char)key;
        if (event & MOUSE_EVENT) {
            unsigned row = key >> 8, col = key & 255;
            if ((row == 7 || row == 10) && col >= 25 && col < 66) {
                unsigned limit = strlen(row == 7 ? d.find.text : d.replacement);
                unsigned at = col - 25 < limit ? col - 25 : limit;
                d.focus = row == 7 ? CHANGE_FIND : CHANGE_TEXT;
                if (row == 7) { d.find.caret = at; d.find.marked = 0; }
                else { d.caret = at; d.marked = 0; }
            } else if (row == 14 && col >= 16 && col < 41) {
                d.focus = CHANGE_CASE;
                d.find.flags ^= CKB_FIND_MATCH_CASE;
            } else if (row == 14 && col >= 48 && col < 62) {
                d.focus = CHANGE_WORD;
                d.find.flags ^= CKB_FIND_WHOLE_WORD;
            } else if (row == 17 && col >= 13 && col < 67) {
                d.focus = col < 32 ? CHANGE_VERIFY : col < 47 ? CHANGE_ALL :
                          col < 58 ? CHANGE_CANCEL : CHANGE_HELP;
                ch = '\r';
            }
            if (ch != '\r') continue;
        }
        if (ch == 27 || (ch == '\r' && d.focus == CHANGE_CANCEL)) return;
        if (key == 0x3b00 || (ch == '\r' && d.focus == CHANGE_HELP))
            show_help(w, CKB_HELP_USING, 0);
        else if (ch == '\r' || key == 0x2e00) { /* Alt+C */
            if (*d.find.text) {
                strcpy(w->find_pattern, d.find.text);
                strcpy(w->change_text, d.replacement);
                w->find_flags = d.find.flags;
                change_matches(w, d.focus == CHANGE_ALL || key == 0x2e00);
            }
            return;
        } else if (ch == '\t' || key == 0x0f00) {
            d.focus = (d.focus + (key == 0x0f00 ? CHANGE_FOCUS_COUNT - 1 : 1)) % CHANGE_FOCUS_COUNT;
        } else if (ch == ' ' && d.focus == CHANGE_CASE) d.find.flags ^= CKB_FIND_MATCH_CASE;
        else if (ch == ' ' && d.focus == CHANGE_WORD) d.find.flags ^= CKB_FIND_WHOLE_WORD;
        else if (d.focus == CHANGE_FIND) edit_search_field(d.find.text, &d.find.caret, &d.find.marked, key);
        else if (d.focus == CHANGE_TEXT) edit_search_field(d.replacement, &d.caret, &d.marked, key);
    }
}

enum edit_command { CUT, COPY_TEXT, PASTE, CLEAR };

static int edit_enabled(const struct workspace *w, enum edit_command command)
{
    unsigned first;
    return command == PASTE ? w->clip_length != 0 : selection(w, &first) != 0;
}

static void clipboard_command(struct workspace *w, enum edit_command command)
{
    unsigned first, length;
    char __far *clipboard;
    if (!edit_enabled(w, command)) return;
    length = selection(w, &first);
    if (command == COPY_TEXT || command == CUT) {
        clipboard = _fmalloc(length);
        if (!clipboard) {
            set_error(w, "Not enough memory to copy selection");
            return;
        }
        _fmemcpy(clipboard, w->text + first, length);
        if (w->clipboard) _ffree(w->clipboard);
        w->clipboard = clipboard;
        w->clip_length = length;
        if (command == COPY_TEXT) return;
    }
    if (command == PASTE)
        replace(w, first, length, w->clipboard, w->clip_length);
    else
        replace(w, first, length, NULL, 0);
    locate(w);
    w->preferred = w->column;
}

static void edit_menu(struct workspace *w)
{
    static const unsigned char box[] = {1, 7, 9, 30, GRAY, GRAY};
    static const char *const items[] = {
        "Cut        Shift+Del", "Copy        Ctrl+Ins",
        "Paste      Shift+Ins", "Clear            Del"
    };
    static const char *const status[] = {
        "F1=Help \xb3 Deletes selected text and copies it to buffer",
        "F1=Help \xb3 Copies selected text to buffer",
        "F1=Help \xb3 Inserts text from buffer",
        "F1=Help \xb3 Deletes selected text"
    };
    static const unsigned accelerator[] = {2, 0, 0, 2};
    unsigned selected = 0, i, attr, key;
    unsigned long event;
    for (;;) {
        draw_workspace(w);
        put(0, 8, SELECTED, " Edit ");
        ui_menu(box);
        for (i = 0; i <= CLEAR; ++i) {
            int enabled = edit_enabled(w, (enum edit_command)i);
            attr = selected == i ? SELECTED : GRAY;
            fill(2 + i, 8, ' ', attr, 22);
            put(2 + i, 9, attr, items[i]);
            if (!enabled && selected != i) {
                unsigned column;
                for (column = 0; items[i][column]; ++column)
                    if (items[i][column] != ' ')
                        fill(2 + i, 9 + column, items[i][column], 0x78, 1);
            }
            if (enabled)
                fill(2 + i, 9 + accelerator[i], items[i][accelerator[i]],
                     selected == i ? 0x0f : 0x7f, 1);
        }
        fill(6, 7, 0xc3, GRAY, 1);
        fill(6, 8, HORIZONTAL, GRAY, 22);
        fill(6, 30, 0xb4, GRAY, 1);
        put(7, 9, GRAY, "New SUB...");
        fill(7, 13, 'S', 0x7f, 1);
        put(8, 9, GRAY, "New FUNCTION...");
        fill(8, 13, 'F', 0x7f, 1);
        workspace_status(w, status[selected]);
        ui_cursor(0xffff);
        event = ui_event();
        key = (unsigned)event;
        if (event & MOUSE_EVENT) {
            unsigned row = key >> 8, col = key & 255;
            if (row >= 2 && row <= 5 && col >= 8 && col < 30)
                clipboard_command(w, (enum edit_command)(row - 2));
            return;
        }
        switch (key) {
        case KEY_UP: selected = (selected + CLEAR) % (CLEAR + 1); break;
        case KEY_DOWN: selected = (selected + 1) % (CLEAR + 1); break;
        case KEY_HOME: selected = CUT; break;
        case KEY_END: selected = CLEAR; break;
        case KEY_ALT: case KEY_ALT_E: return;
        default:
            switch (tolower((unsigned char)event)) {
            case 't': selected = CUT; break;
            case 'c': selected = COPY_TEXT; break;
            case 'p': selected = PASTE; break;
            case 'e': selected = CLEAR; break;
            case '\r': break;
            case 27: return;
            default: continue;
            }
            clipboard_command(w, (enum edit_command)selected);
            return;
        }
    }
}

static void vertical(struct workspace *w, int direction, unsigned lines)
{
    unsigned at = start_of(w, w->caret), next;
    while (lines--) {
        next = direction < 0 ? view_previous(w, at) : view_next(w, at);
        if (next == NO_MARK) break;
        at = next;
    }
    w->caret = at_column(w, at, w->preferred);
    locate(w);
}

static void move_mouse(struct workspace *w, unsigned row, unsigned col)
{
    unsigned first = w->source_focus == BOTTOM_VIEW ? 12 : 2;
    unsigned at = w->top;
    while (row-- > first) {
        at = view_next(w, at);
        if (at == NO_MARK) return;
    }
    w->anchor = NO_MARK;
    w->caret = at_column(w, at, w->left + col - 1);
    locate(w);
    w->preferred = w->column;
}

static void edit_key(struct workspace *w, unsigned long event)
{
    unsigned key = (unsigned)event, first, length, line, next;
    char ch = (unsigned char)key;
    int shifted = (event & SHIFT_HELD) != 0;
    line = start_of(w, w->caret);
    switch (key) {
    case KEY_CTRL_INSERT: clipboard_command(w, COPY_TEXT); return;
    case KEY_INSERT:
        if (shifted) clipboard_command(w, PASTE);
        return;
    case KEY_DELETE:
        if (shifted) {
            clipboard_command(w, CUT);
            return;
        }
        break;
    case KEY_UP: case KEY_DOWN: case KEY_PREV: case KEY_NEXT:
    case KEY_LEFT: case KEY_RIGHT: case KEY_HOME: case KEY_END:
    case 0x7700: case 0x7500: /* Ctrl+Home and Ctrl+End */
        if (!shifted) w->anchor = NO_MARK;
        else if (w->anchor == NO_MARK) w->anchor = w->caret;
        break;
    }

    switch (key) {
    case KEY_UP: vertical(w, -1, 1); return;
    case KEY_DOWN: vertical(w, 1, 1); return;
    case KEY_PREV: vertical(w, -1, view_height(w)); return;
    case KEY_NEXT: vertical(w, 1, view_height(w)); return;
    case KEY_LEFT:
        if (w->caret > line) --w->caret;
        else if ((next = view_previous(w, line)) != NO_MARK) w->caret = end_of(w, next);
        break;
    case KEY_RIGHT:
        if (w->caret < end_of(w, line)) ++w->caret;
        else if ((next = view_next(w, line)) != NO_MARK) w->caret = next;
        break;
    case KEY_HOME: w->caret = line; break;
    case KEY_END: w->caret = end_of(w, w->caret); break;
    case 0x7700: w->caret = view_start(w); break;
    case 0x7500: w->caret = view_last(w); break;
    default:
        length = selection(w, &first);
        if (ch == '\b' || key == KEY_DELETE) {
            if (!length && ch == '\b') {
                next = view_previous(w, line);
                if (first > line || (next != NO_MARK && end_of(w, next) + 1 == first)) {
                    --first;
                    length = 1;
                }
            } else if (!length && key == KEY_DELETE) {
                if (first < end_of(w, line) || view_next(w, line) == first + 1)
                    length = 1;
            }
            replace(w, first, length, NULL, 0);
        } else if (ch == '\t') {
            char spaces[99];
            unsigned col = first - start_of(w, first);
            unsigned count = w->display.tab_width - col % w->display.tab_width;
            memset(spaces, ' ', count);
            if (replace(w, first, length, spaces, count)) w->caret += count;
        } else if (ch == '\r' || ((unsigned char)ch >= 32 && ch != 127)) {
            if (ch == '\r') ch = '\n';
            if (replace(w, first, length, &ch, 1)) ++w->caret;
        }
    }
    locate(w);
    w->preferred = w->column;
}

static int dispatch(struct workspace *w, enum file_command command)
{
    switch (command) {
    case NEW:
        if (confirm_discard(w)) {
            w->path[0] = '\0';
            w->length = 0;
            recenter(w);
        }
        break;
    case OPEN:
        if (confirm_discard(w)) choose_path(w, 0);
        break;
    case SAVE:
        if (w->path[0]) save_source(w, w->path);
        else choose_path(w, 1);
        break;
    case SAVE_AS:
        choose_path(w, 1);
        break;
    case EXIT:
        return confirm_discard(w);
    default:
        break;
    }
    return 0;
}

/* Help uses the same event and drawing primitives as the source workspace.
 * Its topic data is independently authored in CKBHELP.C. */
static unsigned help_word(const struct workspace *w)
{
    char word[32];
    unsigned first = w->caret, end, n = 0;
    while (first && (isalnum((unsigned char)w->text[first - 1]) || w->text[first - 1] == '_')) --first;
    end = first;
    while (end < w->length && (isalnum((unsigned char)w->text[end]) || w->text[end] == '_')) {
        if (n < sizeof(word) - 1) word[n++] = w->text[end];
        ++end;
    }
    word[n] = 0;
    return ckb_help_context(word);
}

static void draw_help(struct workspace *w, const struct ckb_help *h)
{
    unsigned row, line, end;
    char title[80];
    draw_workspace(w);
    fill(1, 0, HORIZONTAL, SELECTED, 80);
    fill(1, 0, 0xda, SELECTED, 1);
    fill(1, 79, 0xbf, SELECTED, 1);
    sprintf(title, " HELP: %.65s ", ckb_help_title(h->topic));
    center(1, 0, 79, TITLE, title);
    for (row = 2; row <= 19; ++row) {
        fill(row, 0, ' ', SELECTED, 80);
        fill(row, 0, VERTICAL, SELECTED, 1);
        fill(row, 79, row == 2 ? 0x18 : row == 19 ? 0x19 : 0xb0, GRAY, 1);
    }
    put(2, 3, SELECTED, "\x11" "Contents" "\x10   \x11" "Index" "\x10   \x11" "Back" "\x10");
    fill(3, 1, HORIZONTAL, SELECTED, 78);
    for (row = 4; row <= 19; ++row) {
        line = h->top + row - 4;
        if (line >= ckb_help_line_count(h->topic)) break;
        put(row, 3, line == h->selection ? TITLE : SELECTED, ckb_help_line(h->topic, line));
    }
    fill(20, 0, HORIZONTAL, w->display.text_attribute, 80);
    fill(20, 0, 0xc3, w->display.text_attribute, 1);
    fill(20, 79, 0xb4, w->display.text_attribute, 1);
    sprintf(title, " %.40s ", file_title(w));
    center(20, 0, 79, w->display.text_attribute, title);
    fill(21, 0, ' ', w->display.text_attribute, 80);
    fill(21, 0, VERTICAL, w->display.text_attribute, 1);
    fill(21, 79, VERTICAL, w->display.text_attribute, 1);
    end = end_of(w, w->top);
    display_line(w, 21, w->top, end);
    fill(22, 0, HORIZONTAL, w->display.text_attribute, 80);
    center(22, 0, 79, w->display.text_attribute, " Immediate ");
    fill(23, 0, ' ', w->display.text_attribute, 80);
    fill(23, 0, VERTICAL, w->display.text_attribute, 1);
    fill(23, 79, VERTICAL, w->display.text_attribute, 1);
    fill(24, 0, ' ', STATUS, 80);
    put(24, 1, STATUS, "<Shift+F1=Help> <Esc=Cancel> <Alt+F1=Back>");
    if (h->selection >= h->top && h->selection < h->top + 16)
        ui_cursor(POS(4 + h->selection - h->top, 3));
    else ui_cursor(0xffff);
}

static void show_help(struct workspace *w, unsigned topic, int menu_pending)
{
    struct ckb_help h;
    unsigned long event;
    unsigned key;
    ckb_help_init(&h, topic);
    for (;;) {
        draw_help(w, &h);
        if (menu_pending) { event = 0x2300; menu_pending = 0; }
        else event = ui_event();
        if (event & MOUSE_EVENT) continue;
        key = (unsigned)event;
        if ((unsigned char)key == 27) return;
        if (key == 0x6800) ckb_help_back(&h); /* Alt+F1 */
        else if (key == 0x2300) { /* Alt+H */
            static const unsigned char menu[] = {1, 50, 6, 78, GRAY, GRAY};
            ui_menu(menu);
            put(2, 51, GRAY, "Index");
            put(3, 51, GRAY, "Contents");
            put(4, 51, GRAY, "Back");
            event = ui_event(); key = (unsigned)event;
            if ((unsigned char)key == 'i' || (unsigned char)key == 'I') ckb_help_open(&h, CKB_HELP_INDEX);
            else if ((unsigned char)key == 'c' || (unsigned char)key == 'C') ckb_help_open(&h, CKB_HELP_CONTENTS);
            else if ((unsigned char)key == 'b' || (unsigned char)key == 'B') ckb_help_back(&h);
        } else if ((unsigned char)key == '\t') ckb_help_move(&h, 1);
        else if (key == 0x0f00) ckb_help_move(&h, -1);
        else if ((unsigned char)key == '\r') ckb_help_follow(&h);
        else if (key == KEY_UP) ckb_help_scroll(&h, -1, 16);
        else if (key == KEY_DOWN) ckb_help_scroll(&h, 1, 16);
        else if (key == KEY_PREV) ckb_help_scroll(&h, -16, 16);
        else if (key == KEY_NEXT) ckb_help_scroll(&h, 16, 16);
    }
}

int ckb_ide(const char *path)
{
    struct workspace w;
    unsigned segment, row, col;
    unsigned long event;
    int done = 0;
    memset(&w, 0, sizeof(w));
    ckb_ini_load_display(&w.display);
    w.anchor = NO_MARK;
    w.procedure = -1;
    if (path && *path) {
        if (!load_source(&w, path)) {
            fprintf(stderr, "%s: %s\n", path, w.error);
            return 1;
        }
    } else {
        w.text = _fmalloc(SOURCE_LIMIT + 1U);
        if (!w.text) {
            fputs("Not enough memory for workspace\n", stderr);
            return 1;
        }
    }
    if (_dos_allocmem(SCREEN_SAVE_PARAGRAPHS, &segment)) {
        fputs("Not enough memory to save the DOS screen\n", stderr);
        _ffree(w.text);
        return 1;
    }
    ui_init(segment);
    while (!done && !w.exit_requested) {
        draw_workspace(&w);
        position(&w);
        event = ui_event();
        w.error[0] = '\0';
        if (event & MOUSE_EVENT) {
            row = (event >> 8) & 255;
            col = event & 255;
            if (!row && col < 8) done = dispatch(&w, file_menu(&w));
            else if (!row && col < 14) edit_menu(&w);
            else if (!row && col >= 14 && col <= 18) view_menu(&w);
            else if (!row && col >= 19 && col <= 26) search_menu(&w);
            else if (!row && col >= 28 && col <= 32) run_menu(&w);
            else if (!row && col >= 33 && col <= 38) dispatch_debug(&w);
            else if (!row && col >= 39 && col <= 47) options_menu(&w);
            else if (col > 0 && col < 79) {
                if (row == 22) focus_view(&w, IMMEDIATE_VIEW);
                else if (row >= 2 && row <= 19 && (!w.split || row <= 9 || row >= 12)) {
                    focus_view(&w, w.split && row >= 12 ? BOTTOM_VIEW : TOP_VIEW);
                    move_mouse(&w, row, col);
                }
            }
        } else if ((unsigned)event == 0x3b00) {
            unsigned topic = help_word(&w);
            show_help(&w, topic ? topic : CKB_HELP_USING, 0);
        } else if ((unsigned)event == 0x5400) {
            show_help(&w, CKB_HELP_USING, 0);
        } else if ((unsigned)event == 0x2300) {
            show_help(&w, CKB_HELP_USING, 1);
        } else if ((unsigned)event == KEY_ALT_F || (unsigned)event == KEY_ALT) {
            done = dispatch(&w, file_menu(&w));
        } else if ((unsigned)event == KEY_ALT_E) {
            edit_menu(&w);
        } else if ((unsigned)event == KEY_ALT_O) {
            options_menu(&w);
        } else if ((unsigned)event == KEY_ALT_S) {
            search_menu(&w);
        } else if ((unsigned)event == KEY_ALT_V) {
            view_menu(&w);
        } else if ((unsigned)event == KEY_ALT_R) {
            run_menu(&w);
        } else if ((unsigned)event == KEY_ALT_D) {
            dispatch_debug(&w);
        } else if ((unsigned)event == KEY_F2) {
            subs_dialog(&w);
        } else if ((unsigned)event == KEY_F4) {
            output_screen(&w);
        } else if ((unsigned)event == KEY_F5 || (unsigned)event == KEY_SHIFT_F5) {
            run_workspace(&w, RUN_NORMAL);
        } else if ((unsigned)event == KEY_F8 || (unsigned)event == KEY_F10) {
            run_workspace(&w, (unsigned)event == KEY_F8 ? RUN_STEP : RUN_PROCEDURE_STEP);
        } else if ((unsigned)event == KEY_F9 && w.focus != IMMEDIATE_VIEW) {
            toggle_breakpoint(&w);
        } else if ((unsigned)event == KEY_F6 || (unsigned)event == KEY_SHIFT_F6) {
            cycle_view(&w, (unsigned)event == KEY_SHIFT_F6);
        } else if ((unsigned)event == KEY_F3) {
            find_next(&w);
        } else if ((unsigned char)event == 19) { /* Ctrl+S */
            done = dispatch(&w, SAVE);
        } else if (w.focus == IMMEDIATE_VIEW) edit_immediate(&w, event);
        else edit_key(&w, event);
    }
    ui_done();
    if (w.display_dirty && !ckb_ini_save_display(&w.display))
        fputs("QBASIC: Could not save QBASIC.INI\n", stderr);
    _dos_freemem(segment);
    if (w.clipboard) _ffree(w.clipboard);
    if (w.output) _ffree(w.output);
    ckb_immediate_clear();
    _ffree(w.text);
    return 0;
}
