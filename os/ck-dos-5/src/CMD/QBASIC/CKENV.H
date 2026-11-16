/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT */
#ifndef CKENV_H
#define CKENV_H

/* get/at return the string length, or -5/-7 for invalid data / insufficient
 * capacity. Pass a null output pointer to measure without copying. */
void ckenv_init(void);
int ckenv_get(const char *name, char *out, unsigned capacity);
int ckenv_at(long index, char *out, unsigned capacity);
/* Copy the DOS executable name from the validated environment trailer. */
int ckenv_program_path(char *out, unsigned capacity);
int ckenv_set(const char *assignment);
/* Pass the returned NULL-terminated vector to spawnve; NULL means out of memory.
 * Free it with ckenv_free_child_env after the child exits. */
char **ckenv_child_env(void);
void ckenv_free_child_env(char **env);

#endif
