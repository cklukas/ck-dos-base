// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dosbase::devtools {

struct HelpEntry {
    std::uint16_t id{};
    std::string topic;
    std::string text;
};

struct HelpDocument {
    std::uint16_t topic_len{};
    std::uint16_t max_num_help{};
    std::vector<HelpEntry> entries;
};

// Parses an ASCII help source file (such as SELECT/USA.TXT) using
// .TOPICLEN., .MAXNUMHLP., .HELPID., .TOPICTEXT., .HELPTEXT., and .HELPTEND.
// Throws FormatError if required directives are missing or malformed.
[[nodiscard]] HelpDocument parse_help_source(std::span<const std::uint8_t> source);

// Serializes a HelpDocument into the IBM CAS .HLP binary format produced by
// MS-DOS 4's ASC2HLP.EXE (0x1234 CAS header + 0x00FE help object).
[[nodiscard]] std::vector<std::uint8_t> build_cas_help(const HelpDocument& doc);

// Convenience wrapper: parses `source` and serializes to CAS .HLP bytes.
[[nodiscard]] std::vector<std::uint8_t> asc2hlp(std::span<const std::uint8_t> source);

}  // namespace dosbase::devtools
