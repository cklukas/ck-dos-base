/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT */
#ifndef CKBINI_H
#define CKBINI_H

struct ckb_ini_display {
    unsigned text_attribute;
    unsigned current_attribute;
    unsigned breakpoint_attribute;
    unsigned tab_width;
    unsigned scroll_bars;
};

void ckb_ini_load_display(struct ckb_ini_display *display);
int ckb_ini_save_display(const struct ckb_ini_display *display);

#endif
