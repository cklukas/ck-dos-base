/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBRUN_H
#define CKBRUN_H

/* Run a DOS text BASIC source file; return the DOS process status. */
int ckb_run(const char *path, int mbf);

typedef struct {
    const char *error;
    unsigned line, column;
    int editor_source;
    int aborted;
} CkbRunResult;

/* The hook runs on the live interpreter stack. Return zero to unwind
 * execution, or nonzero to continue. A null message denotes STOP. */
typedef int (*CkbRunPause)(void *context, unsigned line, unsigned column,
                          unsigned width, const char *message);

/* Called before an executable statement. line_start is an editor source
 * offset and depth counts active SUB/FUNCTION calls. */
typedef int (*CkbRunBreak)(void *context, unsigned line_start, int first_statement,
                           unsigned depth);

#if defined(__WATCOMC__) || defined(_MSDOS)
#define CKB_IMMEDIATE_LIMIT 255

typedef struct {
    const char *error;
    int wrote_output, runtime_error;
} CkbImmediateResult;

/* Execute the editor buffer without writing it to a DOS file. */
int ckb_run_source(const char *path, const char __far *text, unsigned length,
                   CkbRunResult *report, CkbRunPause pause, CkbRunBreak should_break,
                   void *context);
void ckb_set_debug_active(const int *flag);
void ckb_set_editor_source(char __far *buf, unsigned len);
void ckb_immediate(const char *command, unsigned output_column, CkbImmediateResult *result);
void ckb_immediate_clear(void);
#endif

#endif
