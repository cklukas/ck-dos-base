// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dosbase::devtools {

enum class MemoryModel {
    small_model,    // -AS: near code, near data
    medium_model,   // -AM: far code, near data
    compact_model,  // -AC: near code, far data
    large_model,    // -AL: far code, far data
};

struct CompileOptions {
    MemoryModel model = MemoryModel::small_model;
    bool pack_structs = false;      // -Zp
    bool omit_default_lib = false;  // -Zl
    std::string module_name = "module";
};

// Compiles preprocessed C source into a 16-bit OMF .OBJ byte vector
// compatible with cklink and MS-DOS 4's OMF conventions.
// Throws FormatError on syntax, type, or semantic errors.
[[nodiscard]] std::vector<std::uint8_t> compile_c_to_omf(
    std::string_view preprocessed_source,
    const CompileOptions& options = {});

}  // namespace dosbase::devtools
