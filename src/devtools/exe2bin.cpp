// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/exe2bin.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/core/exe2bin_core.h"

#include <cstdlib>

namespace dosbase::devtools {

Exe2BinResult exe2bin(const MzImage& image, std::optional<std::uint16_t> base_segment) {
    const std::vector<std::uint8_t> raw = image.serialize();
    ck_exe2bin_out core_out{};
    ck_error err{};
    if (ck_exe2bin(raw.data(),
                   static_cast<ck_u32>(raw.size()),
                   base_segment.has_value() ? 1 : 0,
                   base_segment.value_or(0),
                   &core_out,
                   &err) != 0) {
        throw FormatError(err.offset, err.message);
    }
    Exe2BinResult result;
    result.kind = (core_out.kind == CK_EXE2BIN_PURE_BINARY) ? Exe2BinKind::pure_binary
                                                            : Exe2BinKind::com_image;
    if (core_out.size > 0 && core_out.bytes) {
        result.bytes.assign(core_out.bytes, core_out.bytes + core_out.size);
    }
    std::free(core_out.bytes);
    return result;
}

}  // namespace dosbase::devtools
