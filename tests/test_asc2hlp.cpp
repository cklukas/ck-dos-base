// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "dosbase/devtools/asc2hlp.hpp"
#include "dosbase/devtools/binary.hpp"

namespace {
using dosbase::devtools::FormatError;
using dosbase::devtools::HelpDocument;
using dosbase::devtools::asc2hlp;
using dosbase::devtools::parse_help_source;

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

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

DOSBASE_TEST(asc2hlp_builds_cas_help_header_index_topics_and_text) {
    const std::string src =
        ".TOPICLEN. 20\r\n"
        ".MAXNUMHLP. 3\r\n"
        ".HELPID. 1\r\n"
        ".TOPICTEXT.First Topic\r\n"
        ".HELPTEXT. Body one\r\n"
        ".HELPTEND.\r\n"
        ".HELPID. 42\r\n"
        ".TOPICTEXT.Second\r\n"
        ".HELPTEXT. Body two\r\n"
        ".HELPTEND.\r\n";

    const HelpDocument doc = parse_help_source(as_bytes(src));
    DOSBASE_TEST_EXPECT_EQ(doc.topic_len, std::uint16_t{20});
    DOSBASE_TEST_EXPECT_EQ(doc.max_num_help, std::uint16_t{3});
    DOSBASE_TEST_EXPECT_EQ(doc.entries.size(), std::size_t{2});

    const std::vector<std::uint8_t> hlp = asc2hlp(as_bytes(src));

    // CAS header
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 0), std::uint16_t{0x1234});
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 2), std::uint16_t{20});
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 4), std::uint16_t{0x00FE});
    DOSBASE_TEST_EXPECT_EQ(read_u32(hlp, 6), std::uint32_t{164});
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 10), std::uint16_t{3});

    // Help object header at 164
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 164), std::uint16_t{2});
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 166), std::uint16_t{20});

    // Index entries at 168 (3 * 8 = 24 bytes)
    const std::size_t topics_start = 168 + 24;
    const std::size_t texts_start = topics_start + 3 * 20;
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 168), std::uint16_t{1});
    DOSBASE_TEST_EXPECT_EQ(read_u32(hlp, 170), static_cast<std::uint32_t>(texts_start));
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 174), std::uint16_t{10});  // "Body one\r\n"

    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 176), std::uint16_t{42});
    DOSBASE_TEST_EXPECT_EQ(read_u32(hlp, 178), static_cast<std::uint32_t>(texts_start + 10));
    DOSBASE_TEST_EXPECT_EQ(read_u16(hlp, 182), std::uint16_t{10});  // "Body two\r\n"

    // Space-padded topic 0
    const std::string topic0(hlp.begin() + topics_start, hlp.begin() + topics_start + 20);
    DOSBASE_TEST_EXPECT_EQ(topic0, std::string("First Topic         "));

    // Help text payloads
    const std::string text0(hlp.begin() + texts_start, hlp.begin() + texts_start + 10);
    const std::string text1(hlp.begin() + texts_start + 10, hlp.begin() + texts_start + 20);
    DOSBASE_TEST_EXPECT_EQ(text0, std::string("Body one\r\n"));
    DOSBASE_TEST_EXPECT_EQ(text1, std::string("Body two\r\n"));
}

DOSBASE_TEST(asc2hlp_rejects_missing_directive) {
    const std::string bad = ".TOPICLEN. 20\r\n.HELPID. 1\r\n";
    DOSBASE_TEST_EXPECT(throws_format_error([&] { (void)asc2hlp(as_bytes(bad)); }));
}
