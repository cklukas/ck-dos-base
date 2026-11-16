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

enum class FloatMode {
    default_mode,  // off in -AS (preserving MS-DOS 4.00 manifest), IEEE-754 soft-float in -AM/-AC/-AL
    soft_float,    // -FPa / -FPi / -FPc: IEEE-754 software floating point
    none_mode,     // disable software floating point
};

struct CompileOptions {
    MemoryModel model = MemoryModel::small_model;
    FloatMode float_mode = FloatMode::default_mode;
    bool pack_structs = false;      // -Zp
    bool omit_default_lib = false;  // -Zl
    std::string module_name = "module";
};

[[nodiscard]] inline bool use_soft_float(const CompileOptions& opts) {
    return opts.float_mode == FloatMode::soft_float ||
           (opts.float_mode == FloatMode::default_mode &&
            opts.model != MemoryModel::small_model);
}

// Compiles preprocessed C source into a 16-bit OMF .OBJ byte vector
// compatible with cklink and MS-DOS 4's OMF conventions.
// Throws FormatError on syntax, type, or semantic errors.
[[nodiscard]] std::vector<std::uint8_t> compile_c_to_omf(
    std::string_view preprocessed_source,
    const CompileOptions& options = {});

}  // namespace dosbase::devtools
