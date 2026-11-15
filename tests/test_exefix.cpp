// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <cstdint>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/exefix.hpp"
#include "dosbase/devtools/mz_image.hpp"

namespace {
using dosbase::devtools::ExefixOptions;
using dosbase::devtools::FormatError;
using dosbase::devtools::MzImage;
using dosbase::devtools::exefix;

void put_word(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::vector<std::uint8_t> make_mz(std::size_t image_bytes, std::uint16_t min_alloc,
                                  std::uint16_t max_alloc, std::uint16_t ss = 0,
                                  std::uint16_t sp = 0) {
    std::vector<std::uint8_t> bytes(512 + image_bytes, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    const std::size_t total = bytes.size();
    put_word(bytes, 2, static_cast<std::uint16_t>(total % 512));
    put_word(bytes, 4, static_cast<std::uint16_t>((total + 511) / 512));
    put_word(bytes, 8, 32);  // 512-byte header
    put_word(bytes, 10, min_alloc);
    put_word(bytes, 12, max_alloc);
    put_word(bytes, 14, ss);
    put_word(bytes, 16, sp);
    put_word(bytes, 24, 28);
    return bytes;
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

DOSBASE_TEST(exefix_patches_max_and_min_alloc) {
    const std::vector<std::uint8_t> input = make_mz(1024, 0x0010, 0xFFFF);
    const auto result = exefix(input, ExefixOptions{0x0001, 0x0001});
    DOSBASE_TEST_EXPECT(!result.min_clamped_to_stack);
    DOSBASE_TEST_EXPECT(!result.max_clamped_to_min);

    const MzImage parsed = MzImage::parse(result.bytes);
    DOSBASE_TEST_EXPECT_EQ(parsed.header().minimum_extra_paragraphs, std::uint16_t{1});
    DOSBASE_TEST_EXPECT_EQ(parsed.header().maximum_extra_paragraphs, std::uint16_t{1});
    DOSBASE_TEST_EXPECT_EQ(parsed.header().pages_in_file, std::uint16_t{3});
    DOSBASE_TEST_EXPECT_EQ(parsed.header().bytes_in_last_page, std::uint16_t{0});
}

DOSBASE_TEST(exefix_clamps_min_to_stack_and_max_to_min) {
    // Load image is 512 bytes (0x200), stack top is 0x0040:0x0000 = 0x400 (0x200 past image -> 0x20 paras)
    const std::vector<std::uint8_t> input = make_mz(512, 0x0001, 0x0001, 0x0040, 0x0000);
    const auto result = exefix(input, ExefixOptions{0x0001, 0x0001});
    DOSBASE_TEST_EXPECT(result.min_clamped_to_stack);
    DOSBASE_TEST_EXPECT(result.max_clamped_to_min);

    const MzImage parsed = MzImage::parse(result.bytes);
    DOSBASE_TEST_EXPECT_EQ(parsed.header().minimum_extra_paragraphs, std::uint16_t{0x0020});
    DOSBASE_TEST_EXPECT_EQ(parsed.header().maximum_extra_paragraphs, std::uint16_t{0x0020});
}

DOSBASE_TEST(exefix_normalizes_zm_signature_and_recomputes_size) {
    std::vector<std::uint8_t> input = make_mz(100, 0x0000, 0xFFFF);
    input[0] = 'Z';
    input[1] = 'M';
    // Corrupt page fields to verify exefix recomputes them from actual file size (612 bytes)
    put_word(input, 2, 0);
    put_word(input, 4, 1);

    const auto result = exefix(input);
    const MzImage parsed = MzImage::parse(result.bytes);
    DOSBASE_TEST_EXPECT_EQ(parsed.header().signature, std::uint16_t{0x5A4D});
    DOSBASE_TEST_EXPECT_EQ(parsed.header().bytes_in_last_page, std::uint16_t{100});
    DOSBASE_TEST_EXPECT_EQ(parsed.header().pages_in_file, std::uint16_t{2});
}

DOSBASE_TEST(exefix_rejects_non_mz_input) {
    const std::vector<std::uint8_t> garbage = {0x00, 0x01, 0x02, 0x03};
    DOSBASE_TEST_EXPECT(throws_format_error([&] { (void)exefix(garbage); }));
}
