// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <cstdint>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/compress.hpp"

namespace {
using dosbase::devtools::FormatError;
using dosbase::devtools::compress_rle;
using dosbase::devtools::decompress_rle;

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

DOSBASE_TEST(compress_escapes_special_bytes_and_skips_length_2_runs) {
    const std::vector<std::uint8_t> input = {0x0D, 0x1A, 0xFF, 0x41, 0x41};
    const std::vector<std::uint8_t> compressed = compress_rle(input);
    const std::vector<std::uint8_t> expected = {
        0xFF, 0x01,  // 0x0D
        0xFF, 0x02,  // 0x1A
        0xFF, 0xFF,  // 0xFF
        0x41, 0x41,  // run of 2 stays literal
    };
    DOSBASE_TEST_EXPECT_EQ(compressed, expected);
    DOSBASE_TEST_EXPECT_EQ(decompress_rle(compressed), input);
}

DOSBASE_TEST(compress_avoids_reserved_run_counts_13_26_255) {
    for (std::size_t len : {std::size_t{3}, std::size_t{13}, std::size_t{26},
                            std::size_t{254}, std::size_t{255}, std::size_t{300}}) {
        for (std::uint8_t byte : {std::uint8_t{0x00}, std::uint8_t{0x0D},
                                  std::uint8_t{0x1A}, std::uint8_t{0xFF}}) {
            const std::vector<std::uint8_t> input(len, byte);
            const std::vector<std::uint8_t> compressed = compress_rle(input);
            DOSBASE_TEST_EXPECT_EQ(decompress_rle(compressed), input);
        }
    }
}

DOSBASE_TEST(compress_round_trips_mixed_stream) {
    std::vector<std::uint8_t> input;
    for (int i = 0; i < 64; ++i) input.push_back(static_cast<std::uint8_t>(i));
    input.insert(input.end(), 13, 0x20);
    input.insert(input.end(), 26, 0x0D);
    input.insert(input.end(), 260, 0xFF);
    input.push_back(0x1A);

    const std::vector<std::uint8_t> compressed = compress_rle(input);
    DOSBASE_TEST_EXPECT(compressed.size() < input.size());
    DOSBASE_TEST_EXPECT_EQ(decompress_rle(compressed), input);
}

DOSBASE_TEST(decompress_rejects_truncated_escape) {
    const std::vector<std::uint8_t> bad = {0x41, 0xFF};
    DOSBASE_TEST_EXPECT(throws_format_error([&] { (void)decompress_rle(bad); }));
}
