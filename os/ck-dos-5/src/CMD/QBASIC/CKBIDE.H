/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBIDE_H
#define CKBIDE_H

/* A null or empty path opens an untitled source buffer. */
int ckb_ide(const char *path);
int ckb_ide_run(const char *path, int mbf, int slash_run, unsigned display_flags);

#endif
