// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dosbase::devtools {

// A MASM 5.10-compatible assembler developed through byte-level comparisons
// with reference-produced objects. The current compatibility corpus reproduces
// all 311 standalone assembly objects in the preserved MS-DOS 4 source tree.
// Unsupported syntax is rejected with a named diagnostic rather than guessed.

struct MasmOptions {
    // -I paths in command order; the source's directory is searched first.
    std::vector<std::string> include_paths;
    // -Mx: spell PUBDEF/EXTDEF names as the source wrote them.
    bool preserve_public_case = false;
};

struct MasmResult {
    std::vector<std::uint8_t> object;
    std::vector<std::string> errors;   // empty on success
    std::vector<std::string> console;  // %OUT lines, in pass order
};

// Assembles `source_spelling` (the path exactly as the command line wrote
// it -- THEADR keeps that spelling) resolved against the host filesystem.
[[nodiscard]] MasmResult masm_assemble(const std::string& source_spelling,
                                       const MasmOptions& options);

}  // namespace dosbase::devtools
