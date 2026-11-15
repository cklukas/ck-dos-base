// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dosbase::devtools {

// Substitutes 0xE8<id> message placeholders in a menu template file (such as
// FDISK.MSG) using the matching section (e.g. "FDISK") of a country message
// database (such as USA-MS.MSG), producing the generated C source (fdiskm.c).
// If `section_offset` is provided (from an .IDX file), lookup starts there;
// otherwise the section header is located directly in `country_messages`.
// Throws FormatError if the section or a referenced message ID is missing.
[[nodiscard]] std::vector<std::uint8_t> build_menu_source(
    std::span<const std::uint8_t> template_bytes,
    std::span<const std::uint8_t> country_messages,
    std::string_view section_name,
    std::optional<std::size_t> section_offset = std::nullopt);

// Parses a .IDX file (lines of `<SECTION> <HEX_OFFSET>`) and returns the
// byte offset for `section_name` (case-insensitive), if present.
[[nodiscard]] std::optional<std::size_t> find_idx_section_offset(
    std::string_view idx_text, std::string_view section_name);

}  // namespace dosbase::devtools
