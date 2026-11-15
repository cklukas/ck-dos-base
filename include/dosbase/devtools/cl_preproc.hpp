// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dosbase::devtools {

struct PreprocOptions {
    std::vector<std::filesystem::path> include_dirs;
    std::unordered_map<std::string, std::string> defines;
};

// Preprocesses a C source buffer according to C89 + MS-DOS 4 CL rules:
//   - Line splicing (\ at EOL) and /* ... */ / // comment stripping
//   - #include "..." (relative to including file, then include_dirs) and <...>
//     resolved case-insensitively via resolve_dos_input()
//   - #define (object-like and function-like macros), #undef
//   - #ifdef, #ifndef, #if, #elif, #else, #endif (with defined() and integer exprs)
//   - #line, #error, #pragma (ignored)
// Throws FormatError on syntax errors or missing includes.
[[nodiscard]] std::string preprocess_c_source(
    std::string_view source,
    const std::filesystem::path& source_path,
    const PreprocOptions& options = {});

}  // namespace dosbase::devtools
