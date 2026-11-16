/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include "CKBSER.H"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#if defined(__WATCOMC__) || defined(_MSDOS)
#include <conio.h>
#ifdef __WATCOMC__
#include <i86.h>
#else
#include <dos.h>
#endif
#endif

int ckb_serial_name(const char *name)
{
    return toupper((unsigned char)name[0]) == 'C' &&
           toupper((unsigned char)name[1]) == 'O' &&
           toupper((unsigned char)name[2]) == 'M' &&
           name[3] >= '1' && name[3] <= '4' && name[4] == ':';
}

#if defined(__WATCOMC__) || defined(_MSDOS)

enum { DATA, INTERRUPTS, FIFO_CONTROL, LINE_CONTROL, MODEM_CONTROL, LINE_STATUS, MODEM_STATUS };
enum { DATA_READY = 0x01, OVERRUN = 0x02, RX_ERROR = 0x1c, TX_READY = 0x20, TX_EMPTY = 0x40 };
enum { DTR = 0x01, RTS = 0x02, CTS = 0x10, DSR = 0x20, DCD = 0x80 };
enum { SERIAL_PORTS = 4, FIFO_BYTES = 16, BUFFER_BYTES = 512, TX_TIMEOUT_TICKS = 18 };

typedef struct {
    unsigned baud, line, modem, receive_size, transmit_size;
    int lf;
    unsigned signal_ms[3];
} SerialSettings;

struct CkbSerial {
    unsigned base, head, count, status;
    unsigned signal_ms[3];
    unsigned capacity, transmit_size;
    int lf;
    unsigned char buffer[1];
};

static CkbSerial *ports[SERIAL_PORTS];
unsigned ckb_serial_open_count;
static const unsigned signal_mask[] = {CTS, DSR, DCD};

static int number(const char *text, unsigned length, unsigned limit, unsigned *value)
{
    unsigned i, digit, n = 0;
    if (!length) return 0;
    for (i = 0; i < length; ++i) {
        digit = (unsigned char)text[i] - '0';
        if (digit > 9 || digit > limit || n > (limit - digit) / 10) return 0;
        n = n * 10 + digit;
    }
    *value = n;
    return 1;
}

static int settings(const char *text, SerialSettings *out)
{
    static const unsigned baud_rates[] = {75, 110, 150, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200};
    static const char parity[] = "NOEMS";
    static const unsigned parity_bits[] = {0, 8, 24, 40, 56};
    static const char *const signals[] = {"CS", "DS", "CD"};
    unsigned baud = 300, data = 7, stop = 1, line_parity = 24;
    unsigned field = 0, i;
    int explicit_cts = 0;
    out->modem = DTR | RTS;
    out->receive_size = out->transmit_size = BUFFER_BYTES;
    out->lf = 0;
    out->signal_ms[0] = out->signal_ms[1] = 1000;
    out->signal_ms[2] = 0;
    while (*text) {
        const char *end = strchr(text, ',');
        unsigned length = end ? end - text : strlen(text);
        if (length && field == 0) {
            if (!number(text, length, 19200, &baud))
                return 0;
        } else if (length && field == 1) {
            const char *mode = strchr(parity, toupper((unsigned char)*text));
            if (length != 1 || !mode) return 0;
            line_parity = parity_bits[mode - parity];
        } else if (length && field == 2) {
            if (!number(text, length, 8, &data) || data < 5) return 0;
        } else if (length && field == 3) {
            if (!number(text, length, 2, &stop) || !stop) return 0;
        } else if (length && field >= 4) {
            int first = toupper((unsigned char)text[0]);
            int second = length >= 2 ? toupper((unsigned char)text[1]) : 0;
            if (length == 2 && first == 'R' && second == 'S') {
                out->modem &= ~RTS;
            } else if (length == 2 && first == 'L' && second == 'F') {
                out->lf = 1;
            } else if (second == 'B' && (first == 'R' || first == 'T')) {
                unsigned size;
                if (!number(text + 2, length - 2, 32767, &size)) return 0;
                if (first == 'R') out->receive_size = size ? size : BUFFER_BYTES;
                else out->transmit_size = size ? size : BUFFER_BYTES;
            } else {
                for (i = 0; i < 3; ++i)
                    if (first == signals[i][0] && second == signals[i][1]) break;
                if (i == 3) return 0;
                if (i == 0) explicit_cts = 1;
                out->signal_ms[i] = 1000;
                if (length > 2 && !number(text + 2, length - 2, 65535, &out->signal_ms[i]))
                    return 0;
            }
        }
        if (!end) break;
        text = end + 1;
        ++field;
    }
    for (i = 0; i < sizeof(baud_rates) / sizeof(baud_rates[0]); ++i)
        if (baud_rates[i] == baud) break;
    if (i == sizeof(baud_rates) / sizeof(baud_rates[0]) || (data == 8 && line_parity)) return 0;
    if (!(out->modem & RTS) && !explicit_cts) out->signal_ms[0] = 0;
    out->baud = baud;
    out->line = (data - 5) | (stop == 2 ? 4 : 0) | line_parity;
    return 1;
}

static unsigned receive(CkbSerial *channel)
{
    unsigned i, status = 0;
    for (i = 0; i < FIFO_BYTES; ++i) {
        unsigned byte;
        status = inp(channel->base + LINE_STATUS);
        channel->status |= status & (OVERRUN | RX_ERROR);
        if (!(status & DATA_READY)) break;
        byte = inp(channel->base + DATA);
        if (channel->count == channel->capacity) channel->status |= OVERRUN;
        else {
            channel->buffer[(channel->head + channel->count) % channel->capacity] = byte;
            ++channel->count;
        }
    }
    return status;
}

static unsigned long clock_ticks(void)
{
    union REGS regs;
    regs.h.ah = 0;
    int86(0x1a, &regs, &regs);
    return ((unsigned long)regs.x.cx << 16) | regs.x.dx;
}

static unsigned long elapsed_ticks(unsigned long start)
{
    unsigned long now = clock_ticks();
    return now >= start ? now - start : (0x1800B0UL - start) + now;
}

static const char *wait_transmitter(CkbSerial *channel, unsigned line_bit, int check_modem)
{
    unsigned long start = clock_ticks();
    for (;;) {
        unsigned modem = check_modem ? inp(channel->base + MODEM_STATUS) : 0;
        unsigned i, waiting = 0;
        unsigned long elapsed;
        unsigned line_status = receive(channel);
        if (check_modem) for (i = 0; i < 3; ++i) {
            if (channel->signal_ms[i] && !(modem & signal_mask[i])) waiting |= 1 << i;
        }
        if (!waiting && (line_status & line_bit)) return NULL;
        elapsed = elapsed_ticks(start);
        for (i = 0; i < 3; ++i)
            if ((waiting & (1 << i)) && elapsed > (channel->signal_ms[i] * 182UL) / 10000)
                return "Device timeout";
        if (!waiting && elapsed >= TX_TIMEOUT_TICKS) return "Device timeout";
    }
}

const char *ckb_serial_open(const char *name, CkbSerial **channel)
{
    SerialSettings config;
    CkbSerial *port;
    unsigned index = name[3] - '1';
    unsigned base = *(const unsigned __far *)MK_FP(0x40, index * 2);
    unsigned divisor;
    if (!base) return "Device unavailable";
    if (!settings(name + 5, &config)) return "Bad file name";
    if (ports[index]) return "File already open";
    port = calloc(1, sizeof(*port) - 1 + config.receive_size);
    if (!port) return "Out of memory";
    port->base = base;
    port->capacity = config.receive_size;
    port->transmit_size = config.transmit_size;
    port->lf = config.lf;
    memcpy(port->signal_ms, config.signal_ms, sizeof(port->signal_ms));
    divisor = (unsigned)((115200UL + config.baud / 2) / config.baud);
    outp(base + LINE_CONTROL, config.line | 0x80); /* DLAB */
    outp(base + DATA, divisor & 0xff);
    outp(base + INTERRUPTS, divisor >> 8);
    outp(base + LINE_CONTROL, config.line);
    outp(base + INTERRUPTS, 0); /* Receive is serviced by polling. */
    outp(base + FIFO_CONTROL, 0x07); /* Enable and reset both FIFOs if present. */
    outp(base + MODEM_CONTROL, config.modem);
    *channel = ports[index] = port;
    ++ckb_serial_open_count;
    return NULL;
}

const char *ckb_serial_close(CkbSerial *channel)
{
    const char *error = wait_transmitter(channel, TX_EMPTY, 0);
    unsigned i;
    for (i = 0; i < SERIAL_PORTS; ++i)
        if (ports[i] == channel) ports[i] = NULL;
    outp(channel->base + MODEM_CONTROL, 0);
    outp(channel->base + FIFO_CONTROL, 0);
    free(channel);
    --ckb_serial_open_count;
    return error;
}

void ckb_serial_poll(void)
{
    unsigned i;
    for (i = 0; i < SERIAL_PORTS; ++i)
        if (ports[i]) receive(ports[i]);
}

unsigned ckb_serial_count(CkbSerial *channel)
{
    receive(channel);
    return channel->count;
}

unsigned ckb_serial_transmit_capacity(const CkbSerial *channel)
{
    return channel->transmit_size;
}

const char *ckb_serial_newline(const CkbSerial *channel)
{
    return channel->lf ? "\r\n" : "\r";
}

const char *ckb_serial_read(CkbSerial *channel, char *bytes, unsigned length)
{
    while (length--) {
        do {
            receive(channel);
            if (channel->status) {
                unsigned status = channel->status;
                channel->status = 0;
                return status & OVERRUN ? "Communications buffer overflow" : "Read error";
            }
        } while (!channel->count);
        *bytes++ = (char)channel->buffer[channel->head];
        channel->head = (channel->head + 1) % channel->capacity;
        --channel->count;
    }
    return NULL;
}

const char *ckb_serial_write(CkbSerial *channel, const char *bytes, unsigned length)
{
    while (length--) {
        const char *error = wait_transmitter(channel, TX_READY, 1);
        if (error) return error;
        outp(channel->base + DATA, (unsigned char)*bytes++);
    }
    return NULL;
}

#else

unsigned ckb_serial_open_count;

const char *ckb_serial_open(const char *name, CkbSerial **channel)
{
    (void)name;
    (void)channel;
    return "Device unavailable";
}

const char *ckb_serial_close(CkbSerial *channel)
{
    (void)channel;
    return "Device unavailable";
}

void ckb_serial_poll(void) {}

unsigned ckb_serial_count(CkbSerial *channel)
{
    (void)channel;
    return 0;
}

unsigned ckb_serial_transmit_capacity(const CkbSerial *channel)
{
    (void)channel;
    return 0;
}

const char *ckb_serial_newline(const CkbSerial *channel)
{
    (void)channel;
    return "\r";
}

const char *ckb_serial_read(CkbSerial *channel, char *bytes, unsigned length)
{
    (void)channel;
    (void)bytes;
    (void)length;
    return "Device unavailable";
}

const char *ckb_serial_write(CkbSerial *channel, const char *bytes, unsigned length)
{
    (void)channel;
    (void)bytes;
    (void)length;
    return "Device unavailable";
}

#endif
