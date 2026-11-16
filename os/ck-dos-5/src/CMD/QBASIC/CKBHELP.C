/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 * Independently authored CK Basic help; no retail help data is used.
 */
#include "CKBHELP.H"
#include <string.h>

struct line { const char *text; unsigned char link; };
struct topic { const char *title; const struct line *lines; unsigned char count; };
#define LINE(s) {s, CKB_HELP_NONE}
#define LINK(s, t) {s, t}
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const struct line using_lines[] = {
    LINE("CK Basic - Copyright (c) 2026 C. Klukas (clean-room MIT implementation)."),
    LINE("Choose a link with Tab and press Enter to open it."),
    LINE("Alt+F1 returns to the previous topic; Esc closes Help."),
    LINE("Use the arrow and page keys to scroll a longer topic."),
    LINK("Contents", CKB_HELP_CONTENTS), LINK("Index", CKB_HELP_INDEX)
};
static const struct line contents_lines[] = {
    LINE("CK Basic (QBASIC.EXE) - Copyright (c) 2026 C. Klukas."),
    LINE("Browse CK Basic topics:"),
    LINK("Using Help", CKB_HELP_USING), LINK("Editing a program", CKB_HELP_EDITING),
    LINK("PRINT", CKB_HELP_PRINT), LINK("INPUT", CKB_HELP_INPUT),
    LINK("Program flow", CKB_HELP_FLOW), LINK("Sequential files", CKB_HELP_FILES)
};
static const struct line index_lines[] = {
    LINK("Editing", CKB_HELP_EDITING), LINK("Files", CKB_HELP_FILES),
    LINK("INPUT", CKB_HELP_INPUT), LINK("PRINT", CKB_HELP_PRINT),
    LINK("Program flow", CKB_HELP_FLOW), LINK("Using Help", CKB_HELP_USING)
};
static const struct line print_lines[] = {
    LINE("PRINT writes values to the output."),
    LINE("Example: PRINT \"Total: \"; amount"),
    LINE("A semicolon joins output; a comma advances to the next zone."),
    LINE("A trailing semicolon or comma keeps the next output on the line."),
    LINE("PRINT # writes to an open sequential output file."),
    LINK("Sequential files", CKB_HELP_FILES)
};
static const struct line input_lines[] = {
    LINE("INPUT reads a value into a variable."),
    LINE("Example: INPUT \"Name\"; name$"),
    LINE("LINE INPUT # reads a line from an open sequential file."),
    LINK("Sequential files", CKB_HELP_FILES)
};
static const struct line flow_lines[] = {
    LINE("IF chooses statements according to a condition."),
    LINE("FOR/NEXT, DO/LOOP and WHILE/WEND repeat statements."),
    LINE("GOTO transfers control; GOSUB returns with RETURN.")
};
static const struct line files_lines[] = {
    LINE("OPEN selects a sequential file for input or output."),
    LINE("Use PRINT # for output and LINE INPUT # to read a line."),
    LINE("EOF tests for the end of an input file; CLOSE releases it.")
};
static const struct line editing_lines[] = {
    LINE("Type in the source window to change your program."),
    LINE("Use File to open or save a program."),
    LINE("F1 requests help on the word at the cursor."),
    LINK("Using Help", CKB_HELP_USING)
};
#define TOPIC(title, lines) { title, lines, COUNT(lines) }
static const struct topic topics[] = {
    {"", 0, 0}, TOPIC("Using Help", using_lines),
    TOPIC("Table of Contents", contents_lines), TOPIC("Index", index_lines),
    TOPIC("PRINT", print_lines), TOPIC("INPUT", input_lines),
    TOPIC("Program flow", flow_lines), TOPIC("Sequential files", files_lines),
    TOPIC("Editing a program", editing_lines)
};
struct mapping { const char *word; unsigned char topic; };
static const struct mapping mappings[] = {
    {"PRINT", CKB_HELP_PRINT}, {"INPUT", CKB_HELP_INPUT},
    {"IF", CKB_HELP_FLOW}, {"FOR", CKB_HELP_FLOW}, {"NEXT", CKB_HELP_FLOW},
    {"DO", CKB_HELP_FLOW}, {"LOOP", CKB_HELP_FLOW}, {"WHILE", CKB_HELP_FLOW},
    {"WEND", CKB_HELP_FLOW}, {"GOTO", CKB_HELP_FLOW}, {"GOSUB", CKB_HELP_FLOW},
    {"RETURN", CKB_HELP_FLOW}, {"OPEN", CKB_HELP_FILES}, {"CLOSE", CKB_HELP_FILES},
    {"EOF", CKB_HELP_FILES}
};
static int equal_folded(const char *a, const char *b)
{
    unsigned char c;
    while (*a && *b) {
        c = (unsigned char)*a++;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c != (unsigned char)*b++) return 0;
    }
    return !*a && !*b;
}
unsigned ckb_help_context(const char *word)
{
    unsigned i;
    if (!word) return CKB_HELP_NONE;
    for (i = 0; i < COUNT(mappings); ++i)
        if (equal_folded(word, mappings[i].word)) return mappings[i].topic;
    return CKB_HELP_NONE;
}
static unsigned valid(unsigned topic) { return topic > 0 && topic < CKB_HELP_TOPIC_COUNT; }
const char *ckb_help_title(unsigned topic) { return valid(topic) ? topics[topic].title : ""; }
unsigned ckb_help_line_count(unsigned topic) { return valid(topic) ? topics[topic].count : 0; }
const char *ckb_help_line(unsigned topic, unsigned line)
{
    return line < ckb_help_line_count(topic) ? topics[topic].lines[line].text : "";
}
unsigned ckb_help_link(unsigned topic, unsigned line)
{
    return line < ckb_help_line_count(topic) ? topics[topic].lines[line].link : CKB_HELP_NONE;
}
static void select_topic(struct ckb_help *h, unsigned topic)
{
    unsigned i;
    h->topic = (unsigned char)topic;
    h->top = 0;
    h->selection = 255;
    for (i = 0; i < ckb_help_line_count(topic); ++i)
        if (ckb_help_link(topic, i)) { h->selection = (unsigned char)i; break; }
}
void ckb_help_init(struct ckb_help *h, unsigned topic)
{
    memset(h, 0, sizeof(*h));
    select_topic(h, valid(topic) ? topic : CKB_HELP_USING);
}
void ckb_help_open(struct ckb_help *h, unsigned topic)
{
    if (!valid(topic)) return;
    if (h->depth == CKB_HELP_HISTORY) {
        memmove(h->history, h->history + 1, CKB_HELP_HISTORY - 1);
        --h->depth;
    }
    h->history[h->depth++] = h->topic;
    select_topic(h, topic);
}
void ckb_help_back(struct ckb_help *h)
{
    if (h->depth) select_topic(h, h->history[--h->depth]);
}
void ckb_help_move(struct ckb_help *h, int direction)
{
    unsigned count = ckb_help_line_count(h->topic), i, pos;
    if (!count || !direction) return;
    pos = h->selection < count ? h->selection : (direction > 0 ? count - 1 : 0);
    for (i = 0; i < count; ++i) {
        pos = direction > 0 ? (pos + 1) % count : (pos + count - 1) % count;
        if (ckb_help_link(h->topic, pos)) { h->selection = (unsigned char)pos; return; }
    }
}
void ckb_help_follow(struct ckb_help *h)
{
    unsigned topic = ckb_help_link(h->topic, h->selection);
    if (topic) ckb_help_open(h, topic);
}
void ckb_help_scroll(struct ckb_help *h, int amount, unsigned visible)
{
    unsigned count = ckb_help_line_count(h->topic), max = count > visible ? count - visible : 0;
    long top = (long)h->top + amount;
    if (top < 0) top = 0;
    if ((unsigned long)top > max) top = max;
    h->top = (unsigned char)top;
}
