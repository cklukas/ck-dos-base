/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#ifndef CKBSER_H
#define CKBSER_H

typedef struct CkbSerial CkbSerial;

extern unsigned ckb_serial_open_count;

int ckb_serial_name(const char *name);
const char *ckb_serial_open(const char *name, CkbSerial **channel);
const char *ckb_serial_close(CkbSerial *channel);
void ckb_serial_poll(void);
unsigned ckb_serial_count(CkbSerial *channel);
unsigned ckb_serial_transmit_capacity(const CkbSerial *channel);
const char *ckb_serial_newline(const CkbSerial *channel);
const char *ckb_serial_read(CkbSerial *channel, char *bytes, unsigned length);
const char *ckb_serial_write(CkbSerial *channel, const char *bytes, unsigned length);

#endif
