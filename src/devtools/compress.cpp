// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/compress.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/core/compress_core.h"

#include <cstdlib>

namespace dosbase::devtools {

std::vector<std::uint8_t> compress_rle(std::span<const std::uint8_t> input) {
    ck_u8* out_bytes = nullptr;
    ck_u32 out_size = 0;
    ck_error err{};
    if (ck_compress_rle(input.data(),
                        static_cast<ck_u32>(input.size()),
                        &out_bytes,
                        &out_size,
                        &err) != 0) {
        throw FormatError(err.offset, err.message);
    }
    std::vector<std::uint8_t> out(out_bytes, out_bytes + out_size);
    std::free(out_bytes);
    return out;
}

std::vector<std::uint8_t> decompress_rle(std::span<const std::uint8_t> input) {
    ck_u8* out_bytes = nullptr;
    ck_u32 out_size = 0;
    ck_error err{};
    if (ck_decompress_rle(input.data(),
                          static_cast<ck_u32>(input.size()),
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
