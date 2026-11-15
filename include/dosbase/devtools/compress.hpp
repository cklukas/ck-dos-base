// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace dosbase::devtools {

// Compresses a panel data stream using MS-DOS 4 COMPRESS.COM's RLE format:
//   - 0xFF 0xFF        -> literal 0xFF
//   - 0xFF 0x01        -> literal 0x0D
//   - 0xFF 0x02        -> literal 0x1A
//   - 0xFF <cnt> <enc> -> <cnt> copies of byte (<cnt> in 3..254, never 13 or 26;
//                         <enc> is itself escaped if 0xFF, 0x0D, or 0x1A)
//   - any other byte   -> literal byte
[[nodiscard]] std::vector<std::uint8_t> compress_rle(std::span<const std::uint8_t> input);

// Decompresses a COMPRESS.COM RLE stream back to its original bytes.
// Throws FormatError on truncated or malformed escape sequences.
[[nodiscard]] std::vector<std::uint8_t> decompress_rle(std::span<const std::uint8_t> input);

}  // namespace dosbase::devtools
