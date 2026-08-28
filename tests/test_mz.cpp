// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/mz_image.hpp"

namespace {
using dosbase::devtools::FormatError;
using dosbase::devtools::MzImage;

void put_word(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

struct MzBuilder {
    std::vector<std::uint8_t> header = std::vector<std::uint8_t>(32, 0);
    std::vector<std::uint8_t> image;
    std::vector<std::uint8_t> trailing;

    MzBuilder() {
        header[0] = 'M';
        header[1] = 'Z';
        put_word(header, 8, 2);   // header_paragraphs: 32 bytes
        put_word(header, 24, 28);  // relocation table directly after the fixed header
    }

    void set(std::size_t at, std::uint16_t value) { put_word(header, at, value); }

    [[nodiscard]] std::vector<std::uint8_t> build() {
        const std::size_t total = header.size() + image.size();
        put_word(header, 4, static_cast<std::uint16_t>((total + 511) / 512));
        put_word(header, 2, static_cast<std::uint16_t>(total % 512));
        std::vector<std::uint8_t> bytes = header;
        bytes.insert(bytes.end(), image.begin(), image.end());
        bytes.insert(bytes.end(), trailing.begin(), trailing.end());
        return bytes;
    }
};
}  // namespace

DOSBASE_TEST(mz_round_trips_a_minimal_image) {
    MzBuilder builder;
    builder.image = {0xB4, 0x4C, 0xCD, 0x21};
    const std::vector<std::uint8_t> bytes = builder.build();
    const MzImage image = MzImage::parse(bytes);
    DOSBASE_TEST_EXPECT_EQ(image.header_bytes(), std::size_t{32});
    DOSBASE_TEST_EXPECT_EQ(image.image_bytes(), std::size_t{4});
    DOSBASE_TEST_EXPECT_EQ(image.trailing_bytes(), std::size_t{0});
    DOSBASE_TEST_EXPECT(image.serialize() == bytes);
}

DOSBASE_TEST(mz_keeps_relocations_gap_and_trailing_bytes) {
    MzBuilder builder;
    builder.header.resize(48, 0);
    builder.set(8, 3);   // three paragraphs of header
    builder.set(6, 2);   // two relocations
    builder.set(24, 30);  // table starts two bytes past the fixed header
    builder.header[28] = 0xCC;  // gap byte the format does not describe
    builder.header[29] = 0xDD;
    put_word(builder.header, 30, 0x0102);
    put_word(builder.header, 32, 0x0304);
    put_word(builder.header, 34, 0x0506);
    put_word(builder.header, 36, 0x0708);
    builder.header[40] = 0xEE;  // slack after the table
    builder.image = {1, 2, 3, 4, 5, 6, 7, 8};
    builder.trailing = {0xAA, 0xBB, 0xCC};
    const std::vector<std::uint8_t> bytes = builder.build();
    const MzImage image = MzImage::parse(bytes);
    DOSBASE_TEST_EXPECT_EQ(image.relocations().size(), std::size_t{2});
    DOSBASE_TEST_EXPECT_EQ(image.relocations()[0].offset, std::uint16_t{0x0102});
    DOSBASE_TEST_EXPECT_EQ(image.relocations()[1].segment, std::uint16_t{0x0708});
    DOSBASE_TEST_EXPECT_EQ(image.relocation_gap().size(), std::size_t{2});
    DOSBASE_TEST_EXPECT_EQ(image.header_slack().size(), std::size_t{10});
    DOSBASE_TEST_EXPECT_EQ(image.trailing_bytes(), std::size_t{3});
    DOSBASE_TEST_EXPECT(image.serialize() == bytes);

    std::string text;
    dump_mz(image, "fixture", false, text);
    DOSBASE_TEST_EXPECT(text.find("relocation_gap 2 nonzero") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("header_slack 10 nonzero") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("relocation 1 0708:0506") != std::string::npos);
}

DOSBASE_TEST(mz_detects_an_exepack_stub) {
    MzBuilder builder;
    builder.image = std::vector<std::uint8_t>(64, 0);
    const std::vector<std::uint16_t> stub = {0x0012, 0x1208, 0x0000, 0x062F,
                                             0x0298, 0x1D27, 0x1B71, 0x4252};
    for (std::size_t index = 0; index < stub.size(); ++index) {
        builder.image[index * 2] = static_cast<std::uint8_t>(stub[index] & 0xFF);
        builder.image[index * 2 + 1] = static_cast<std::uint8_t>(stub[index] >> 8);
    }
    builder.set(20, 16);  // initial_ip: the stub header is sixteen bytes
    builder.set(22, 0);   // initial_cs
    const std::vector<std::uint8_t> bytes = builder.build();
    const MzImage image = MzImage::parse(bytes);
    DOSBASE_TEST_EXPECT(image.exepack().has_value());
    DOSBASE_TEST_EXPECT_EQ(image.exepack()->real_cs, std::uint16_t{0x1208});
    DOSBASE_TEST_EXPECT_EQ(image.exepack()->destination_length, std::uint16_t{0x1B71});
    DOSBASE_TEST_EXPECT(!image.exepack()->skip_length.has_value());
    DOSBASE_TEST_EXPECT(image.serialize() == bytes);
}

DOSBASE_TEST(mz_rejects_malformed_files) {
    const auto rejects = [](std::vector<std::uint8_t> bytes) {
        try {
            MzImage::parse(bytes);
        } catch (const FormatError&) {
            return true;
        }
        return false;
    };
    MzBuilder builder;
    builder.image = {1, 2, 3, 4};
    std::vector<std::uint8_t> bytes = builder.build();
    bytes[0] = 'P';
    DOSBASE_TEST_EXPECT(rejects(bytes));

    bytes = builder.build();
    put_word(bytes, 8, 1);  // header_paragraphs too small for the fixed header
    DOSBASE_TEST_EXPECT(rejects(bytes));

    bytes = builder.build();
    put_word(bytes, 4, 400);  // declared pages run past the end of the file
    DOSBASE_TEST_EXPECT(rejects(bytes));

    bytes = builder.build();
    put_word(bytes, 6, 4);   // four relocations
    put_word(bytes, 24, 28);  // but the header has no room for them
    DOSBASE_TEST_EXPECT(rejects(bytes));

    DOSBASE_TEST_EXPECT(rejects({}));
}
