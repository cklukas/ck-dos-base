// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/convert.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/core/convert_core.h"

#include <cstdlib>

namespace dosbase::devtools {

std::vector<std::uint8_t> convert_exe_to_com(std::span<const std::uint8_t> exe_bytes) {
    ck_u8* out_bytes = nullptr;
    ck_u32 out_size = 0;
    ck_error err{};
    if (ck_convert_exe_to_com(exe_bytes.data(),
                              static_cast<ck_u32>(exe_bytes.size()),
                              &out_bytes,
                              &out_size,
                              &err) != 0) {
        throw FormatError(err.offset, err.message);
    }
    std::vector<std::uint8_t> out(out_bytes, out_bytes + out_size);
    std::free(out_bytes);
    return out;
}

}  // namespace dosbase::devtools
