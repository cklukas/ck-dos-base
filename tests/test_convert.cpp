// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/convert.hpp"
#include "dosbase/devtools/mz_image.hpp"

namespace {
using dosbase::devtools::FormatError;
using dosbase::devtools::convert_exe_to_com;
using dosbase::devtools::kConvertMaxComBytes;

void put_word(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::vector<std::uint8_t> make_mz_exe(std::size_t image_bytes, std::uint16_t relocations = 0) {
    std::vector<std::uint8_t> bytes(512 + image_bytes, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    const std::size_t total = bytes.size();
    put_word(bytes, 2, static_cast<std::uint16_t>(total % 512));
    put_word(bytes, 4, static_cast<std::uint16_t>((total + 511) / 512));
    put_word(bytes, 6, relocations);
    put_word(bytes, 8, 32);
    put_word(bytes, 24, 28);
    for (std::size_t index = 0; index < image_bytes; ++index) {
        bytes[512 + index] = static_cast<std::uint8_t>((index * 7 + 3) & 0xFF);
    }
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

DOSBASE_TEST(convert_wraps_exe_with_prefix_and_relocating_stub) {
    const std::vector<std::uint8_t> exe = make_mz_exe(256, 1);
    const std::vector<std::uint8_t> com = convert_exe_to_com(exe);

    // 16-byte prefix + exe + 123-byte stub
    DOSBASE_TEST_EXPECT_EQ(com.size(), std::size_t{16} + exe.size() + 123);

    // Prefix starts with JMP rel16 to the CALL $+3 at offset 8 of the stub
    DOSBASE_TEST_EXPECT_EQ(com[0], std::uint8_t{0xE9});
    const std::uint16_t rel16 = static_cast<std::uint16_t>(com[1] | (com[2] << 8));
    const std::size_t jump_target = 3 + rel16;
    DOSBASE_TEST_EXPECT_EQ(jump_target, std::size_t{16} + exe.size() + 8);
    DOSBASE_TEST_EXPECT_EQ(com[jump_target], std::uint8_t{0xE8});
    DOSBASE_TEST_EXPECT_EQ(com[jump_target + 1], std::uint8_t{0x00});
    DOSBASE_TEST_EXPECT_EQ(com[jump_target + 2], std::uint8_t{0x00});

    // Prefix tag "Converted\0\0\0\0"
    const std::string tag(reinterpret_cast<const char*>(&com[3]), 9);
    DOSBASE_TEST_EXPECT_EQ(tag, std::string("Converted"));

    // Embedded EXE matches input verbatim
    for (std::size_t i = 0; i < exe.size(); ++i) {
        DOSBASE_TEST_EXPECT_EQ(com[16 + i], exe[i]);
    }
}

DOSBASE_TEST(convert_rejects_oversized_exe) {
    const std::size_t max_exe_size = kConvertMaxComBytes - 1 - 16 - 123;
    const std::vector<std::uint8_t> ok_exe = make_mz_exe(max_exe_size - 512);
    DOSBASE_TEST_EXPECT_EQ(convert_exe_to_com(ok_exe).size(), kConvertMaxComBytes - 1);

    const std::vector<std::uint8_t> too_big_exe = make_mz_exe(max_exe_size - 512 + 1);
    DOSBASE_TEST_EXPECT(throws_format_error([&] { (void)convert_exe_to_com(too_big_exe); }));
}

DOSBASE_TEST(convert_rejects_non_mz_input) {
    const std::vector<std::uint8_t> garbage = {0x90, 0x90, 0xC3};
    DOSBASE_TEST_EXPECT(throws_format_error([&] { (void)convert_exe_to_com(garbage); }));
}
