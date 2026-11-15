// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace dosbase::devtools {

// CONVERT.EXE refuses images whose converted .COM output would reach 0xFFFE
// bytes (64 KiB minus 2 bytes).
inline constexpr std::size_t kConvertMaxComBytes = 0xFFFE;

// Wraps an MZ .EXE image in a self-relocating .COM loader stub matching
// MS-DOS 4's CONVERT.EXE format:
//   [16-byte header: E9 <rel16> "Converted\0\0\0\0"]
//   [original .EXE bytes verbatim]
//   [123-byte 8086 relocation & jump-to-entry stub]
// Throws FormatError if the input is not a valid MZ executable or if the
// resulting .COM image would be >= kConvertMaxComBytes.
[[nodiscard]] std::vector<std::uint8_t> convert_exe_to_com(
    std::span<const std::uint8_t> exe_bytes);

}  // namespace dosbase::devtools
