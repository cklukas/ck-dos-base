// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include "dosbase/devtools/omf_record.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dosbase::devtools {

// A structured, linkable view of one OMF module, built on top of
// the record-level parser in omf_record.hpp rather than re-deriving OMF's
// binary layout: this layer exists because cklink needs segments, groups,
// fixups, and data as typed values to place and patch, not as dump text.

struct ModuleSegment {
    std::string name;
    std::string class_name;
    std::string overlay_name;
    std::uint8_t alignment{};  // SEGDEF acbp >> 5: 0 absolute, 1 byte, 2 word, 3 para, 4 page, 5 dword
    std::uint8_t combine{};    // (acbp >> 2) & 7: 0 private, 2/4/7 public, 5 stack, 6 common
    bool big{};                // length is 0x10000 (acbp bit 0x02)
    bool use32{};
    std::uint32_t length{};
    std::uint16_t absolute_frame{};  // meaningful only when alignment == 0
    std::uint8_t absolute_offset{};
};

struct ModuleGroup {
    std::string name;
    std::vector<std::size_t> segments;  // 0-based indices into Module::segments
};

struct ModulePublic {
    std::size_t segment{};  // index into Module::segments; SIZE_MAX when the
                            // record is absolute (base segment 0) -- MSBIO's
                            // NUMERR -- with `frame` carrying its address
    std::size_t group{};    // index into Module::groups; SIZE_MAX if absent
    std::string name;
    std::uint32_t offset{};
    std::uint16_t frame{};  // absolute publics: the explicit frame number
    bool local{};           // LPUBDEF: invisible to the linker and the map
    std::uint32_t mention{};  // per-module record-stream order of first appearance,
                              // shared with EXTDEF/COMDEF: LINK's symbol-table
                              // insertion order (the /M by-value tie key)
};

struct ModuleExternal {
    std::string name;
    bool communal{};
    bool local{};  // LEXTDEF: binds to the module's own LPUBDEF scope
    std::uint32_t communal_length{};  // near: byte length; far: count * element size
    std::uint32_t mention{};  // per-module record-stream order (see ModulePublic)
};

// One LEDATA/LIDATA payload, already expanded to flat bytes: cklink's
// segment layout needs contiguous data, not LIDATA's repeat encoding.
struct DataChunk {
    std::size_t segment{};
    std::uint32_t offset{};
    std::vector<std::uint8_t> bytes;
};

enum class FixupRefKind { none, segment, group, external, frame_number, location, target, threaded };

// A resolved FIXUPP frame or target reference. `index` means different
// things depending on `kind`: an index into Module::segments, ::groups, or
// ::externals; `frame_number` is used only for the `frame_number` kind.
struct FixupRef {
    FixupRefKind kind{FixupRefKind::none};
    std::size_t index{};
    std::uint16_t frame_number{};
    std::uint8_t thread{};
};

struct Fixup {
    std::size_t segment{};    // the segment the patched bytes live in
    std::uint32_t offset{};   // absolute offset within that segment
    bool segment_relative{};  // OMF's "self-relative" bit, inverted
    std::uint8_t location{};  // 0 lobyte, 1 offset16, 2 base, 3 pointer32, 9 offset32, 11 pointer48
    FixupRef frame;
    FixupRef target;
    std::uint16_t displacement{};
    bool has_displacement{true};
};

struct ModuleEnd {
    bool is_main{};
    bool has_start{};
    FixupRef start_frame;
    FixupRef start_target;
    std::uint16_t start_displacement{};
    bool start_has_displacement{true};
};

struct Module {
    std::string name;  // THEADR
    std::vector<std::string> lnames;
    std::vector<ModuleSegment> segments;
    std::vector<ModuleGroup> groups;
    std::vector<ModulePublic> publics;
    std::vector<ModuleExternal> externals;
    std::vector<DataChunk> data;
    std::vector<Fixup> fixups;
    ModuleEnd end;
    bool dosseg{};  // COMENT class 9E: DOSSEG segment ordering requested
};

// Parses one module's records (THEADR .. MODEND) into a Module. Throws
// FormatError on a record shape the preserved source tree does not exercise,
// rather than guessing at behavior for it.
[[nodiscard]] Module parse_module(const std::vector<OmfRecord>& records);

}  // namespace dosbase::devtools
