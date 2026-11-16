/* Copyright (c) 2026 C. Klukas */
/* SPDX-License-Identifier: MIT */
#ifndef DOSBASE_DEVTOOLS_CORE_LINK_CORE_H
#define DOSBASE_DEVTOOLS_CORE_LINK_CORE_H

#include "dosbase/devtools/core/omf_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CK_SIZE_NONE ((ck_usize)-1)

enum {
    CK_FIX_NONE = 0,
    CK_FIX_SEGMENT = 1,
    CK_FIX_GROUP = 2,
    CK_FIX_EXTERNAL = 3,
    CK_FIX_FRAME_NUM = 4,
    CK_FIX_LOCATION = 5,
    CK_FIX_TARGET = 6,
    CK_FIX_THREADED = 7
};

typedef struct ck_link_segment {
    char *name;
    char *class_name;
    char *overlay_name;
    ck_u8 alignment;
    ck_u8 combine;
    int big;
    int use32;
    ck_u32 length;
    ck_u16 absolute_frame;
    ck_u8 absolute_offset;
} ck_link_segment;

typedef struct ck_link_group {
    char *name;
    ck_usize *segments;
    ck_usize seg_count;
} ck_link_group;

typedef struct ck_link_public {
    ck_usize segment;
    ck_usize group;
    char *name;
    ck_u32 offset;
    ck_u16 frame;
    int local;
    ck_u32 mention;
} ck_link_public;

typedef struct ck_link_external {
    char *name;
    int communal;
    int local;
    ck_u32 communal_length;
    ck_u32 mention;
} ck_link_external;

typedef struct ck_link_data {
    ck_usize segment;
    ck_u32 offset;
    ck_u8 *bytes;
    ck_u32 len;
} ck_link_data;

typedef struct ck_link_fixref {
    int kind;
    ck_usize index;
    ck_u16 frame_number;
    ck_u8 thread;
} ck_link_fixref;

typedef struct ck_link_fixup {
    ck_usize segment;
    ck_u32 offset;
    int segment_relative;
    ck_u8 location;
    ck_link_fixref frame;
    ck_link_fixref target;
    ck_u16 displacement;
    int has_displacement;
} ck_link_fixup;

typedef struct ck_link_end {
    int is_main;
    int has_start;
    ck_link_fixref start_frame;
    ck_link_fixref start_target;
    ck_u16 start_displacement;
    int start_has_displacement;
} ck_link_end;

typedef struct ck_link_module {
    char *name;
    char **lnames;
    ck_usize lname_count;
    ck_link_segment *segments;
    ck_usize seg_count;
    ck_link_group *groups;
    ck_usize group_count;
    ck_link_public *publics;
    ck_usize pub_count;
    ck_link_external *externals;
    ck_usize ext_count;
    ck_link_data *data;
    ck_usize data_count;
    ck_link_fixup *fixups;
    ck_usize fixup_count;
    ck_link_end end;
    int dosseg;
} ck_link_module;

int ck_omf_parse_module(const ck_omf_record *recs,
                        ck_usize rec_count,
                        ck_link_module *out_mod,
                        ck_error *err);

void ck_link_module_free(ck_link_module *mod);

int ck_link_modules(const ck_link_module *modules,
                    ck_usize mod_count,
                    int map_publics,
                    ck_u32 stack_override,
                    int exepack,
                    const ck_usize *placement_order,
                    ck_usize placement_count,
                    ck_u8 **out_exe,
                    ck_u32 *out_exe_size,
                    char **out_map,
                    ck_u32 *out_map_size,
                    ck_error *err);

#ifdef __cplusplus
}
#endif

#endif /* DOSBASE_DEVTOOLS_CORE_LINK_CORE_H */
