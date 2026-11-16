/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBHELP_H
#define CKBHELP_H

/* The caller owns this state and the screen. Topic IDs remain stable within
 * this interface; zero means no matching context. */
enum ckb_help_topic {
    CKB_HELP_NONE, CKB_HELP_USING, CKB_HELP_CONTENTS, CKB_HELP_INDEX,
    CKB_HELP_PRINT, CKB_HELP_INPUT, CKB_HELP_FLOW, CKB_HELP_FILES,
    CKB_HELP_EDITING, CKB_HELP_TOPIC_COUNT
};
#define CKB_HELP_HISTORY 16
struct ckb_help {
    unsigned char topic, selection, top, depth;
    unsigned char history[CKB_HELP_HISTORY];
};

/* Open replaces the current topic and remembers it when requested. Back is
 * a no-op at the start of the history. Context lookup is ASCII case-folded. */
void ckb_help_init(struct ckb_help *help, unsigned topic);
void ckb_help_open(struct ckb_help *help, unsigned topic);
void ckb_help_back(struct ckb_help *help);
unsigned ckb_help_context(const char *word);
const char *ckb_help_title(unsigned topic);
unsigned ckb_help_line_count(unsigned topic);
const char *ckb_help_line(unsigned topic, unsigned line);
/* Every body line may have one link. Zero means plain text. */
unsigned ckb_help_link(unsigned topic, unsigned line);
void ckb_help_move(struct ckb_help *help, int direction);
void ckb_help_follow(struct ckb_help *help);
void ckb_help_scroll(struct ckb_help *help, int amount, unsigned visible);

#endif
