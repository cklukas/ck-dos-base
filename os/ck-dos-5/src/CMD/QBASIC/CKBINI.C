/* Copyright (c) 2026 C. Klukas. All rights reserved.
 * SPDX-License-Identifier: MIT */
#include "CKBINI.H"
#include "CKENV.H"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef S_IREAD
#define S_IREAD S_IRUSR
#define S_IWRITE S_IWUSR
#endif

#define DOS_PATH_SIZE 128
#define SETTINGS_SIZE 32

static const char ini_name[] = "QBASIC.INI";
static const unsigned char defaults[] = {
    'Q','B','P',4, 1,0,7,0,0,0,0,0, 1,0,7,0,1,0,0,0, 4,0,7,0,0,0,0,0,
    8,0, 1,0, 1,0,1,1,1,0,0,1,0,0,1,0,0, 1,0, 0
};
static char program_ini[DOS_PATH_SIZE], current_ini[DOS_PATH_SIZE];
static char source_ini[DOS_PATH_SIZE];
static struct ckb_ini_display loaded;

static int separator(char ch)
{
    return ch == '\\' || ch == '/' || ch == ':';
}

/* Resolve relative directories before a BASIC program can change them. */
static int absolute_path(char *output, const char *path)
{
#ifdef __DOS__
    return _fullpath(output, path, DOS_PATH_SIZE) != NULL;
#else
    unsigned n;
    if (path[0] == '/') {
        if (strlen(path) >= DOS_PATH_SIZE) return 0;
        strcpy(output, path);
        return 1;
    }
    if (!getcwd(output, DOS_PATH_SIZE)) return 0;
    n = strlen(output);
    if (n + 1 + strlen(path) >= DOS_PATH_SIZE) return 0;
    output[n++] = '/';
    strcpy(output + n, path);
    return 1;
#endif
}

static unsigned word(const unsigned char *data)
{
    return data[0] | ((unsigned)data[1] << 8);
}

static unsigned attribute(const unsigned char *data)
{
    unsigned bg = data[0] | (word(data + 6) ? 8 : 0);
    unsigned fg = data[2] | (word(data + 4) ? 8 : 0);
    return ((bg & 15) << 4) | (fg & 15);
}

static void encode_attribute(unsigned char *data, unsigned attr)
{
    memset(data, 0, 8);
    data[0] = (attr >> 4) & 7;
    data[2] = attr & 7;
    data[4] = (attr >> 3) & 1;
    data[6] = (attr >> 7) & 1;
}

/* The first file that opens wins, even if it does not have a QBP v4 header. */
static int read_display(const char *path, struct ckb_ini_display *display)
{
    unsigned char data[SETTINGS_SIZE];
    unsigned n, tab;
    FILE *file;
    if (!*path) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    strcpy(source_ini, path);
    n = fread(data, 1, sizeof(data), file);
    fclose(file);
    if (n < 4 || memcmp(data, defaults, 4)) return 1;
    memcpy(data + n, defaults + n, sizeof(data) - n);
    display->text_attribute = attribute(data + 4);
    display->current_attribute = attribute(data + 12);
    display->breakpoint_attribute = attribute(data + 20);
    tab = word(data + 28);
    if (n >= 30 && tab && tab <= 99) display->tab_width = tab;
    if (n >= 32) display->scroll_bars = word(data + 30) != 0;
    return 1;
}

void ckb_ini_load_display(struct ckb_ini_display *display)
{
    char path[DOS_PATH_SIZE], absolute[DOS_PATH_SIZE], *last = NULL, *p;
    char *paths, *part;
    unsigned n;
    int len;

    program_ini[0] = current_ini[0] = source_ini[0] = '\0';
    display->text_attribute = attribute(defaults + 4);
    display->current_attribute = attribute(defaults + 12);
    display->breakpoint_attribute = attribute(defaults + 20);
    display->tab_width = defaults[28];
    display->scroll_bars = defaults[30];
    ckenv_init();
    if (!absolute_path(current_ini, ini_name)) current_ini[0] = '\0';
    if (ckenv_program_path(path, sizeof(path)) > 0) {
        for (p = path; *p; ++p) if (separator(*p)) last = p;
        if (last && last + 1 - path + sizeof(ini_name) <= sizeof(path)) {
            strcpy(last + 1, ini_name);
            if (!absolute_path(program_ini, path)) program_ini[0] = '\0';
        }
    }
    if (read_display(program_ini, display) || read_display(current_ini, display))
        goto done;

    len = ckenv_get("PATH", NULL, 0);
    if (len <= 0) goto done;
    paths = malloc((unsigned)len + 1U);
    if (!paths) goto done;
    if (ckenv_get("PATH", paths, (unsigned)len + 1U) >= 0) {
        /* Do not truncate an overlong entry: its prefix might name another
         * directory. Skip it and keep looking through the rest of PATH. */
        for (part = paths; *part; part = p + 1) {
            p = strchr(part, ';');
            n = p ? (unsigned)(p - part) : strlen(part);
            if (n && n < sizeof(path)) {
                unsigned slash = !separator(part[n - 1]);
                if (n + slash + sizeof(ini_name) <= sizeof(path)) {
                    memcpy(path, part, n);
                    if (slash) {
#ifdef __DOS__
                        path[n++] = '\\';
#else
                        path[n++] = '/';
#endif
                    }
                    strcpy(path + n, ini_name);
                    if (absolute_path(absolute, path) && read_display(absolute, display))
                        break;
                }
            }
            if (!p) break;
        }
    }
    free(paths);
done:
    loaded = *display;
}

/* Copy the full record of the file that supplied the settings. Replace only
 * Display's bytes and keep other options, the help path, and any trailing
 * bytes. Also complete a short file with the defaults for absent fields. */
static int write_display(FILE *out, const struct ckb_ini_display *display)
{
    unsigned char data[sizeof(defaults)], block[512];
    FILE *source = NULL;
    unsigned n, count;
    int ok;

    memcpy(data, defaults, sizeof(data));
    if (*source_ini) {
        source = fopen(source_ini, "rb");
        if (!source) return 0;
        n = fread(block, 1, sizeof(data), source);
        if (ferror(source)) {
            fclose(source);
            return 0;
        }
        if (n >= 4 && !memcmp(block, defaults, 4))
            memcpy(data, block, n);
        else {
            fclose(source);
            source = NULL;
        }
    }
    encode_attribute(data + 4, display->text_attribute);
    if (display->current_attribute != loaded.current_attribute)
        encode_attribute(data + 12, display->current_attribute);
    if (display->breakpoint_attribute != loaded.breakpoint_attribute)
        encode_attribute(data + 20, display->breakpoint_attribute);
    data[28] = display->tab_width;
    data[29] = display->tab_width >> 8;
    data[30] = display->scroll_bars != 0;
    data[31] = 0;
    ok = fwrite(data, 1, sizeof(data), out) == sizeof(data);
    if (source) {
        while (ok && (count = fread(block, 1, sizeof(block), source)) != 0)
            ok = fwrite(block, 1, count, out) == count;
        if (ferror(source)) ok = 0;
        if (fclose(source)) ok = 0;
    }
    return ok;
}

/* -1 lets the caller fall back when this directory cannot take the file.
 * Once writing starts, do not retry write or installation failures elsewhere. */
static int save_at(const char *path, const struct ckb_ini_display *display)
{
    char temporary[DOS_PATH_SIZE], backup[DOS_PATH_SIZE];
    struct stat attributes;
    int descriptor, exists, ok;
    FILE *file;
    if (!*path) return -1;
    exists = stat(path, &attributes) == 0;
    if (exists ? !(attributes.st_mode & S_IWRITE) || !S_ISREG(attributes.st_mode)
               : errno != ENOENT) return -1;
    strcpy(temporary, path);
    strcpy(backup, path);
    strcpy(strrchr(temporary, '.'), ".CKN");
    strcpy(strrchr(backup, '.'), ".CKB");
    if (exists && (stat(backup, &attributes) == 0 || errno != ENOENT))
        return -1;
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_BINARY,
                      S_IREAD | S_IWRITE);
    if (descriptor < 0) return -1;
    file = fdopen(descriptor, "wb");
    if (!file) {
        close(descriptor);
        remove(temporary);
        return 0;
    }
    ok = write_display(file, display);
    if (fclose(file)) ok = 0;
    if (!ok || (exists && rename(path, backup))) {
        remove(temporary);
        return 0;
    }
    if (rename(temporary, path)) {
        if (!exists || !rename(backup, path)) remove(temporary);
        return 0;
    }
    if (exists) remove(backup);
    return 1;
}

int ckb_ini_save_display(const struct ckb_ini_display *display)
{
    int result = save_at(program_ini, display);
    if (result >= 0 || !strcmp(program_ini, current_ini)) return result > 0;
    return save_at(current_ini, display) > 0;
}
