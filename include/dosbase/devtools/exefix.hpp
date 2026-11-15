// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dosbase::devtools {

struct ExefixOptions {
    std::optional<std::uint16_t> max_alloc;
    std::optional<std::uint16_t> min_alloc;
};

struct ExefixResult {
    std::vector<std::uint8_t> bytes;
    bool min_clamped_to_stack = false;
    bool max_clamped_to_min = false;
};

// Patches an MZ executable's header to match MS-DOS 4's EXEFIX.EXE:
//   - Normalizes the MZ signature to "MZ" (0x5A4D)
//   - Updates maximum/minimum extra paragraphs if provided
//   - Recomputes pages_in_file and bytes_in_last_page from the file size
//   - Clamps minimum_extra_paragraphs up if the declared stack extends past
//     the end of the loaded image
//   - Clamps maximum_extra_paragraphs up to minimum_extra_paragraphs if min > max
// Throws FormatError if the input is not a valid MZ executable.
[[nodiscard]] ExefixResult exefix(std::span<const std::uint8_t> exe_bytes,
                                  const ExefixOptions& options = {});

}  // namespace dosbase::devtools
