// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include "dosbase/devtools/mz_image.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace dosbase::devtools {

// EXE2BIN's two conversions, as the pinned repair tree exercises them
//. The rules were read off the reference build's own artifacts,
// not off a manual: `MSBOOT.EXE` enters with CS:IP 0000:0000 and leaves as
// the whole 32256-byte load image, and `SYS.EXE` enters with CS:IP 0000:0100
// and leaves as the load image past its first 0x100 bytes.
enum class Exe2BinKind {
    pure_binary,  // CS:IP 0000:0000 — the load image, unchanged
    com_image,    // CS:IP 0000:0100 — the load image past the PSP-relative origin
};

// EXE2BIN's prompt when the image carries fix-ups, verbatim from the
// reference build's own console log for `exe2bin msbio.exe msbio.bin`.
inline constexpr char kBaseSegmentPrompt[] = "Fix-ups needed - base segment (hex): ";

struct Exe2BinResult {
    Exe2BinKind kind{};
    std::vector<std::uint8_t> bytes;
};

// `base_segment` relocates an image that carries fix-ups: every relocation
// word gains that paragraph, which is what `exe2bin msbio.exe msbio.bin
// <locscr` does with the 0x70 its input file supplies. Throws FormatError
// for the shapes the tree does not exercise — a declared stack, a nonzero
// entry segment, appended overlay data, fix-ups with no base segment —
// rather than inventing behavior for them.
[[nodiscard]] Exe2BinResult exe2bin(const MzImage& image,
                                    std::optional<std::uint16_t> base_segment = std::nullopt);

}  // namespace dosbase::devtools
