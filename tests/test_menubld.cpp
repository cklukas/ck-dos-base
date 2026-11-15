// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/menubld.hpp"

namespace {
using dosbase::devtools::FormatError;
using dosbase::devtools::build_menu_source;
using dosbase::devtools::find_idx_section_offset;

std::vector<std::uint8_t> as_bytes(std::string_view sv) {
    return {sv.begin(), sv.end()};
}

template <typename Fn>
bool throws_format_error(Fn&& fn) {
    try {
        fn();
        return false;
    } catch (const FormatError&) {
        return true;
    }
}
}  // namespace

DOSBASE_TEST(menubld_substitutes_placeholders_from_matching_section) {
    const std::string country =
        "OTHER    0000 0001\r\n"
        "1001 U 0000 \"Wrong Section\"\r\n"
        "FDISK    b1ba 0122\r\n"
        "1001 U 0000 \"MS-DOS Version 4.00\"\r\n"
        "1002 U 0000 \"Fixed Disk Setup Program\"\r\n"
        "FORMAT   0000 0001\r\n"
        "1001 U 0000 \"Another Section\"\r\n";

    std::string tmpl = "char far *menu_1 = \"^0030^<R>";
    tmpl.push_back(static_cast<char>(0xE8));
    tmpl += "1001\\n";
    tmpl.push_back(static_cast<char>(0xE8));
    tmpl += "1002\";\r\n";

    const std::vector<std::uint8_t> out =
        build_menu_source(as_bytes(tmpl), as_bytes(country), "fdisk");
    const std::string result(out.begin(), out.end());
    DOSBASE_TEST_EXPECT_EQ(
        result,
        std::string("char far *menu_1 = \"^0030^<R>MS-DOS Version 4.00\\nFixed Disk Setup Program\";\r\n"));
}

DOSBASE_TEST(menubld_parses_idx_offset_case_insensitively) {
    const std::string idx = "COMMAND 0010\r\nFDISK   01A0\r\n";
    const auto offset = find_idx_section_offset(idx, "fdisk");
    DOSBASE_TEST_EXPECT(offset.has_value());
    DOSBASE_TEST_EXPECT_EQ(*offset, std::size_t{0x01A0});
    DOSBASE_TEST_EXPECT(!find_idx_section_offset(idx, "missing").has_value());
}

DOSBASE_TEST(menubld_rejects_missing_message_id) {
    const std::string country = "FDISK 0000 0001\r\n1001 U 0000 \"Hello\"\r\n";
    std::string tmpl;
    tmpl.push_back(static_cast<char>(0xE8));
    tmpl += "9999";

    DOSBASE_TEST_EXPECT(throws_format_error([&] {
        (void)build_menu_source(as_bytes(tmpl), as_bytes(country), "FDISK");
    }));
}
