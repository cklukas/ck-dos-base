// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/omf_library.hpp"

namespace {
using dosbase::devtools::FormatError;
using dosbase::devtools::OmfLibrary;

constexpr std::size_t kPageSize = 16;
constexpr std::size_t kBlockBytes = 512;

void append_record(std::vector<std::uint8_t>& bytes, std::uint8_t type,
                   const std::vector<std::uint8_t>& body) {
    const auto length = static_cast<std::uint16_t>(body.size() + 1U);
    bytes.push_back(type);
    bytes.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>(length >> 8));
    bytes.insert(bytes.end(), body.begin(), body.end());
    std::uint32_t sum = type;
    sum += length & 0xFFU;
    sum += length >> 8;
    for (const std::uint8_t value : body) sum += value;
    bytes.push_back(static_cast<std::uint8_t>((0x100U - (sum & 0xFFU)) & 0xFFU));
}

std::vector<std::uint8_t> name_body(const std::string& text) {
    std::vector<std::uint8_t> body{static_cast<std::uint8_t>(text.size())};
    body.insert(body.end(), text.begin(), text.end());
    return body;
}

void pad_to_page(std::vector<std::uint8_t>& bytes) {
    while (bytes.size() % kPageSize != 0) bytes.push_back(0);
}

// A two-member library with a one-block dictionary, laid out the way LIB.EXE
// writes one: page-aligned members, dictionary at a page boundary.
std::vector<std::uint8_t> sample_library(const std::vector<std::string>& modules) {
    std::vector<std::uint8_t> members;
    std::vector<std::uint16_t> pages;
    for (const std::string& module : modules) {
        pad_to_page(members);
        pages.push_back(static_cast<std::uint16_t>((members.size() / kPageSize) + 1U));
        append_record(members, 0x80, name_body(module));
        append_record(members, 0x8A, {0x00});
        pad_to_page(members);
    }
    const std::size_t dictionary_offset = kPageSize + members.size();

    std::vector<std::uint8_t> block(kBlockBytes, 0);
    std::size_t write = 38;  // first free byte after the bucket table and free pointer
    for (std::size_t index = 0; index < modules.size(); ++index) {
        const std::string symbol = "_" + modules[index];
        block[index] = static_cast<std::uint8_t>(write / 2U);
        block[write] = static_cast<std::uint8_t>(symbol.size());
        for (std::size_t at = 0; at < symbol.size(); ++at) {
            block[write + 1U + at] = static_cast<std::uint8_t>(symbol[at]);
        }
        block[write + 1U + symbol.size()] = static_cast<std::uint8_t>(pages[index] & 0xFFU);
        block[write + 2U + symbol.size()] = static_cast<std::uint8_t>(pages[index] >> 8);
        write += ((symbol.size() + 3U + 1U) / 2U) * 2U;
    }
    block[37] = static_cast<std::uint8_t>(write / 2U);

    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> header_body;
    header_body.push_back(static_cast<std::uint8_t>(dictionary_offset & 0xFFU));
    header_body.push_back(static_cast<std::uint8_t>((dictionary_offset >> 8) & 0xFFU));
    header_body.push_back(static_cast<std::uint8_t>((dictionary_offset >> 16) & 0xFFU));
    header_body.push_back(static_cast<std::uint8_t>((dictionary_offset >> 24) & 0xFFU));
    header_body.push_back(0x01);  // one dictionary block
    header_body.push_back(0x00);
    header_body.push_back(0x00);  // flags
    header_body.resize(kPageSize - 4U, 0);  // record header plus checksum fill the page
    append_record(bytes, 0xF0, header_body);
    bytes.resize(kPageSize, 0);
    bytes.insert(bytes.end(), members.begin(), members.end());
    bytes.insert(bytes.end(), block.begin(), block.end());
    return bytes;
}
}  // namespace

DOSBASE_TEST(library_round_trips_members_and_dictionary) {
    const std::vector<std::uint8_t> bytes = sample_library({"ALLOC.ASM", "FREE.ASM"});
    const OmfLibrary library = OmfLibrary::parse(bytes);
    DOSBASE_TEST_EXPECT_EQ(library.page_size(), std::size_t{16});
    DOSBASE_TEST_EXPECT_EQ(library.members().size(), std::size_t{2});
    DOSBASE_TEST_EXPECT_EQ(library.members()[0].name, std::string{"ALLOC.ASM"});
    DOSBASE_TEST_EXPECT_EQ(library.members()[1].name, std::string{"FREE.ASM"});
    DOSBASE_TEST_EXPECT_EQ(library.dictionary_blocks(), std::uint16_t{1});
    DOSBASE_TEST_EXPECT_EQ(library.symbols().size(), std::size_t{2});
    DOSBASE_TEST_EXPECT_EQ(library.symbols()[0].name, std::string{"_ALLOC.ASM"});
    DOSBASE_TEST_EXPECT_EQ(library.symbols()[0].page, std::uint16_t{1});
    DOSBASE_TEST_EXPECT(library.trailing().empty());
    DOSBASE_TEST_EXPECT(library.serialize() == bytes);
}

DOSBASE_TEST(library_dump_lists_members_records_and_symbols) {
    const OmfLibrary library = OmfLibrary::parse(sample_library({"ALLOC.ASM", "FREE.ASM"}));
    std::string text;
    dump_library(library, "fixture", true, true, false, text);
    DOSBASE_TEST_EXPECT(text.find("page_size 16") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("member 0 page 1") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("80 THEADR") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("symbol \"_FREE.ASM\" page 3") != std::string::npos);
    std::string again;
    dump_library(library, "fixture", true, true, false, again);
    DOSBASE_TEST_EXPECT_EQ(again, text);
}

DOSBASE_TEST(library_keeps_an_extended_dictionary_verbatim) {
    std::vector<std::uint8_t> bytes = sample_library({"ALLOC.ASM"});
    const std::vector<std::uint8_t> extended{0xF2, 0x03, 0x00, 0x01, 0x02, 0x03};
    bytes.insert(bytes.end(), extended.begin(), extended.end());
    const OmfLibrary library = OmfLibrary::parse(bytes);
    DOSBASE_TEST_EXPECT(library.has_extended_dictionary());
    DOSBASE_TEST_EXPECT_EQ(library.trailing().size(), std::size_t{6});
    DOSBASE_TEST_EXPECT(library.serialize() == bytes);
}

DOSBASE_TEST(library_rejects_files_that_are_not_object_libraries) {
    const auto rejects = [](const std::vector<std::uint8_t>& bytes) {
        try {
            OmfLibrary::parse(bytes);
        } catch (const FormatError&) {
            return true;
        }
        return false;
    };
    // A MASM macro include file named .LIB, as the tree carries in CMD/APPEND.
    const std::string text = "; SYSTEM MACROS\n";
    DOSBASE_TEST_EXPECT(rejects(std::vector<std::uint8_t>(text.begin(), text.end())));

    std::vector<std::uint8_t> bytes = sample_library({"ALLOC.ASM"});
    bytes[3] = 0xFF;  // dictionary offset past the end of the file
    bytes[4] = 0xFF;
    DOSBASE_TEST_EXPECT(rejects(bytes));

    bytes = sample_library({"ALLOC.ASM"});
    bytes[1] = 0x11;  // page size 20 is not a power of two
    DOSBASE_TEST_EXPECT(rejects(bytes));
}
