// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/link_image.hpp"
#include "dosbase/devtools/core/link_core.h"

#include <cstring>
#include <vector>

namespace dosbase::devtools {
namespace {

ck_link_fixref to_c_fixref(const FixupRef& ref) {
    ck_link_fixref out{};
    switch (ref.kind) {
        case FixupRefKind::none: out.kind = CK_FIX_NONE; break;
        case FixupRefKind::segment: out.kind = CK_FIX_SEGMENT; break;
        case FixupRefKind::group: out.kind = CK_FIX_GROUP; break;
        case FixupRefKind::external: out.kind = CK_FIX_EXTERNAL; break;
        case FixupRefKind::frame_number: out.kind = CK_FIX_FRAME_NUM; break;
        case FixupRefKind::location: out.kind = CK_FIX_LOCATION; break;
        case FixupRefKind::target: out.kind = CK_FIX_TARGET; break;
        case FixupRefKind::threaded: out.kind = CK_FIX_THREADED; break;
    }
    out.index = (ref.index == SIZE_MAX) ? CK_SIZE_NONE : ref.index;
    out.frame_number = ref.frame_number;
    out.thread = ref.thread;
    return out;
}

}  // namespace

LinkResult link_modules(const std::vector<Module>& modules,
                        bool map_publics,
                        std::uint32_t stack_override,
                        bool exepack,
                        const std::vector<std::size_t>& placement_order) {
    LinkResult result;
    std::vector<ck_link_module> c_mods(modules.size());
    std::vector<std::vector<ck_link_segment>> c_segs(modules.size());
    std::vector<std::vector<ck_link_group>> c_grps(modules.size());
    std::vector<std::vector<ck_link_public>> c_pubs(modules.size());
    std::vector<std::vector<ck_link_external>> c_exts(modules.size());
    std::vector<std::vector<ck_link_data>> c_data(modules.size());
    std::vector<std::vector<ck_link_fixup>> c_fixs(modules.size());

    for (std::size_t m = 0; m < modules.size(); ++m) {
        const Module& mod = modules[m];
        c_mods[m].name = const_cast<char*>(mod.name.c_str());
        c_mods[m].dosseg = mod.dosseg ? 1 : 0;

        c_segs[m].resize(mod.segments.size());
        for (std::size_t i = 0; i < mod.segments.size(); ++i) {
            const ModuleSegment& s = mod.segments[i];
            c_segs[m][i].name = const_cast<char*>(s.name.c_str());
            c_segs[m][i].class_name = const_cast<char*>(s.class_name.c_str());
            c_segs[m][i].overlay_name = const_cast<char*>(s.overlay_name.c_str());
            c_segs[m][i].alignment = s.alignment;
            c_segs[m][i].combine = s.combine;
            c_segs[m][i].big = s.big ? 1 : 0;
            c_segs[m][i].use32 = s.use32 ? 1 : 0;
            c_segs[m][i].length = s.length;
            c_segs[m][i].absolute_frame = s.absolute_frame;
            c_segs[m][i].absolute_offset = s.absolute_offset;
        }
        c_mods[m].segments = c_segs[m].data();
        c_mods[m].seg_count = c_segs[m].size();

        c_grps[m].resize(mod.groups.size());
        for (std::size_t i = 0; i < mod.groups.size(); ++i) {
            c_grps[m][i].name = const_cast<char*>(mod.groups[i].name.c_str());
            c_grps[m][i].segments = const_cast<std::size_t*>(mod.groups[i].segments.data());
            c_grps[m][i].seg_count = mod.groups[i].segments.size();
        }
        c_mods[m].groups = c_grps[m].data();
        c_mods[m].group_count = c_grps[m].size();

        c_pubs[m].resize(mod.publics.size());
        for (std::size_t i = 0; i < mod.publics.size(); ++i) {
            const ModulePublic& p = mod.publics[i];
            c_pubs[m][i].segment = (p.segment == SIZE_MAX) ? CK_SIZE_NONE : p.segment;
            c_pubs[m][i].group = (p.group == SIZE_MAX) ? CK_SIZE_NONE : p.group;
            c_pubs[m][i].name = const_cast<char*>(p.name.c_str());
            c_pubs[m][i].offset = p.offset;
            c_pubs[m][i].frame = p.frame;
            c_pubs[m][i].local = p.local ? 1 : 0;
            c_pubs[m][i].mention = p.mention;
        }
        c_mods[m].publics = c_pubs[m].data();
        c_mods[m].pub_count = c_pubs[m].size();

        c_exts[m].resize(mod.externals.size());
        for (std::size_t i = 0; i < mod.externals.size(); ++i) {
            const ModuleExternal& e = mod.externals[i];
            c_exts[m][i].name = const_cast<char*>(e.name.c_str());
            c_exts[m][i].communal = e.communal ? 1 : 0;
            c_exts[m][i].local = e.local ? 1 : 0;
            c_exts[m][i].communal_length = e.communal_length;
            c_exts[m][i].mention = e.mention;
        }
        c_mods[m].externals = c_exts[m].data();
        c_mods[m].ext_count = c_exts[m].size();

        c_data[m].resize(mod.data.size());
        for (std::size_t i = 0; i < mod.data.size(); ++i) {
            const DataChunk& d = mod.data[i];
            c_data[m][i].segment = d.segment;
            c_data[m][i].offset = d.offset;
            c_data[m][i].bytes = const_cast<ck_u8*>(d.bytes.data());
            c_data[m][i].len = static_cast<ck_u32>(d.bytes.size());
        }
        c_mods[m].data = c_data[m].data();
        c_mods[m].data_count = c_data[m].size();

        c_fixs[m].resize(mod.fixups.size());
        for (std::size_t i = 0; i < mod.fixups.size(); ++i) {
            const Fixup& f = mod.fixups[i];
            c_fixs[m][i].segment = f.segment;
            c_fixs[m][i].offset = f.offset;
            c_fixs[m][i].segment_relative = f.segment_relative ? 1 : 0;
            c_fixs[m][i].location = f.location;
            c_fixs[m][i].frame = to_c_fixref(f.frame);
            c_fixs[m][i].target = to_c_fixref(f.target);
            c_fixs[m][i].displacement = f.displacement;
            c_fixs[m][i].has_displacement = f.has_displacement ? 1 : 0;
        }
        c_mods[m].fixups = c_fixs[m].data();
        c_mods[m].fixup_count = c_fixs[m].size();

        c_mods[m].end.is_main = mod.end.is_main ? 1 : 0;
        c_mods[m].end.has_start = mod.end.has_start ? 1 : 0;
        c_mods[m].end.start_frame = to_c_fixref(mod.end.start_frame);
        c_mods[m].end.start_target = to_c_fixref(mod.end.start_target);
        c_mods[m].end.start_displacement = mod.end.start_displacement;
        c_mods[m].end.start_has_displacement = mod.end.start_has_displacement ? 1 : 0;
    }

    ck_u8* exe_buf = nullptr;
    ck_u32 exe_sz = 0;
    char* map_buf = nullptr;
    ck_u32 map_sz = 0;
    ck_error err{};
    if (ck_link_modules(c_mods.data(), c_mods.size(), map_publics ? 1 : 0,
                        stack_override, exepack ? 1 : 0,
                        placement_order.empty() ? nullptr : placement_order.data(),
                        placement_order.size(),
                        &exe_buf, &exe_sz, &map_buf, &map_sz, &err) != 0) {
        result.errors.emplace_back(err.message);
        return result;
    }
    if (exe_buf && exe_sz > 0) result.exe.assign(exe_buf, exe_buf + exe_sz);
    if (map_buf && map_sz > 0) result.map.assign(map_buf, map_buf + map_sz);
    std::free(exe_buf);
    std::free(map_buf);
    return result;
}

}  // namespace dosbase::devtools
