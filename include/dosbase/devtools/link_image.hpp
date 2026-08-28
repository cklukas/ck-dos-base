// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include "dosbase/devtools/omf_module.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dosbase::devtools {

// LINK-compatible image builder. The single-module, library-free path is
// exercised by the tree's `link msboot;`. Layout walks the
// module's segments in definition order -- an absolute segment occupies no
// image space and resolves through its frame number -- and the MZ header
// reproduces the seed's measured shape: a 512-byte header with the
// relocation table at 0x1E, LINK's 0x0001 word at 0x1C, max-alloc FFFF,
// and the negative word-sum checksum that makes the whole file sum to
// FFFF.
struct LinkResult {
    std::vector<std::uint8_t> exe;
    std::string map;                  // the .MAP text, CRLF line endings
    std::vector<std::string> errors;  // empty on success
};

// `map_publics` adds the /M switch's "Publics by Name"/"by Value"
// sections to the map. `placement_order` walks the modules in a
// different order for segment combination only -- LINK places library
// members by each visit's page order yet still runs pass 2 (and so
// emits relocations) in pull order; empty means natural order.
[[nodiscard]] LinkResult link_modules(
    const std::vector<Module>& modules, bool map_publics = false,
    std::uint32_t stack_override = 0, bool exepack = false,
    const std::vector<std::size_t>& placement_order = {});

}  // namespace dosbase::devtools
