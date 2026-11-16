// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/exefix.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/core/exefix_core.h"

namespace dosbase::devtools {

ExefixResult exefix(std::span<const std::uint8_t> exe_bytes, const ExefixOptions& options) {
    ExefixResult result;
    result.bytes.assign(exe_bytes.begin(), exe_bytes.end());
    ck_exefix_opts opts{};
    opts.has_min_alloc = options.min_alloc.has_value() ? 1 : 0;
    opts.min_alloc = options.min_alloc.value_or(0);
    opts.has_max_alloc = options.max_alloc.has_value() ? 1 : 0;
    opts.max_alloc = options.max_alloc.value_or(0);
    ck_exefix_info info{};
    ck_error err{};
    if (ck_exefix_patch(result.bytes.data(),
                        static_cast<ck_u32>(result.bytes.size()),
                        static_cast<ck_u32>(result.bytes.size()),
                        &opts,
                        &info,
                        &err) != 0) {
        throw FormatError(err.offset, err.message);
    }
    result.min_clamped_to_stack = info.min_clamped_to_stack != 0;
    result.max_clamped_to_min = info.max_clamped_to_min != 0;
    return result;
}

}  // namespace dosbase::devtools
