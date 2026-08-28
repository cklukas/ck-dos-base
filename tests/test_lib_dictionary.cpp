// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// LIB 3.10's dictionary placement. Every expectation here is a
// measurement taken from the tool itself through
// tools/bootstrap/lib_dictionary_probe.py, not a reading of a manual.
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "dosbase/devtools/lib_writer.hpp"
#include "dosbase/devtools/omf_library.hpp"

namespace {
using dosbase::devtools::DictionaryPlacement;
using dosbase::devtools::DictionarySymbol;
using dosbase::devtools::build_dictionary;
using dosbase::devtools::dictionary_blocks_for;
using dosbase::devtools::dictionary_hash;
using dosbase::devtools::place_dictionary;

std::uint16_t bucket_of(const std::string& name) {
    return static_cast<std::uint16_t>(dictionary_hash(name).bucket_index % 37);
}

std::uint16_t step_of(const std::string& name) {
    const auto value = static_cast<std::uint16_t>(dictionary_hash(name).bucket_step % 37);
    return value != 0 ? value : 1;
}
}  // namespace

DOSBASE_TEST(dictionary_bucket_is_the_measured_hash) {
    // A one-symbol library cannot collide, so LIB's recorded bucket is the
    // hash itself: the bucket of a one-character name is its folded code.
    DOSBASE_TEST_EXPECT_EQ(bucket_of("A"), std::uint16_t{23});   // 'a' = 0x61, 97 % 37
    DOSBASE_TEST_EXPECT_EQ(bucket_of("B"), std::uint16_t{24});
    DOSBASE_TEST_EXPECT_EQ(bucket_of("O"), std::uint16_t{0});
    DOSBASE_TEST_EXPECT_EQ(bucket_of("Z"), std::uint16_t{11});
    DOSBASE_TEST_EXPECT_EQ(bucket_of("T000!"), std::uint16_t{3});
}

DOSBASE_TEST(dictionary_folds_case_by_setting_bit_five) {
    // Not tolower: '_' (0x5F) folds to 0x7F, which is why names carrying an
    // underscore missed a tolower model completely.
    DOSBASE_TEST_EXPECT_EQ(bucket_of("a"), bucket_of("A"));
    DOSBASE_TEST_EXPECT_EQ(bucket_of("DosChDir"), bucket_of("DOSCHDIR"));
    DOSBASE_TEST_EXPECT_EQ(dictionary_hash("_").bucket_index, std::uint16_t{0x7F});
}

DOSBASE_TEST(dictionary_step_is_never_zero) {
    // A zero stride would probe the same bucket forever; LIB uses one.
    // "EAAAAAY" and "AAAHAAY" are measured cases whose step hash is 0 mod 37.
    DOSBASE_TEST_EXPECT_EQ(dictionary_hash("EAAAAAY").bucket_step % 37, 0);
    DOSBASE_TEST_EXPECT_EQ(step_of("EAAAAAY"), std::uint16_t{1});
    DOSBASE_TEST_EXPECT_EQ(dictionary_hash("AAAHAAY").bucket_step % 37, 0);
    DOSBASE_TEST_EXPECT_EQ(step_of("AAAHAAY"), std::uint16_t{1});
}

DOSBASE_TEST(dictionary_places_a_collision_at_its_measured_step) {
    // Both names hash to bucket 5 — a real collision from MAPPER.LIB.
    // "CLOSE!" sorts first and keeps the bucket; "LSEEK!" advances by its own
    // step of 23, to bucket 28.
    DOSBASE_TEST_EXPECT_EQ(bucket_of("CLOSE!"), std::uint16_t{5});
    DOSBASE_TEST_EXPECT_EQ(bucket_of("LSEEK!"), std::uint16_t{5});
    DOSBASE_TEST_EXPECT_EQ(step_of("LSEEK!"), std::uint16_t{23});
    const std::vector<DictionarySymbol> symbols{{"CLOSE!", 353}, {"LSEEK!", 330}};
    const auto placement = place_dictionary(symbols, 1);
    DOSBASE_TEST_EXPECT(placement.has_value());
    DOSBASE_TEST_EXPECT_EQ((*placement)[0], (DictionaryPlacement{0, 5}));
    DOSBASE_TEST_EXPECT_EQ((*placement)[1], (DictionaryPlacement{0, 28}));
}

DOSBASE_TEST(dictionary_insertion_order_is_case_insensitive_ascending) {
    // "doschgfileptr" beats "SEL_DISK!" to their shared bucket because the
    // order folds case, which lowercase-first ASCII ordering would not do.
    const std::vector<DictionarySymbol> symbols{{"SEL_DISK!", 382}, {"doschgfileptr", 330}};
    const auto placement = place_dictionary(symbols, 1);
    DOSBASE_TEST_EXPECT(placement.has_value());
    DOSBASE_TEST_EXPECT_EQ((*placement)[1].bucket,
                              static_cast<std::uint8_t>(bucket_of("doschgfileptr")));
}

DOSBASE_TEST(dictionary_bytes_match_the_format_lib_writes) {
    const std::vector<DictionarySymbol> symbols{{"ALLOC", 3}, {"FREE", 5}};
    const auto bytes = build_dictionary(symbols, 1);
    DOSBASE_TEST_EXPECT(bytes.has_value());
    DOSBASE_TEST_EXPECT_EQ(bytes->size(), std::size_t{512});
    // Entries follow the 37 bucket bytes and the free-space byte, in
    // insertion order, at even offsets.
    const std::size_t first = 38;
    DOSBASE_TEST_EXPECT_EQ((*bytes)[first], std::uint8_t{5});          // length of "ALLOC"
    DOSBASE_TEST_EXPECT_EQ((*bytes)[first + 1], std::uint8_t{'A'});
    DOSBASE_TEST_EXPECT_EQ((*bytes)[first + 6], std::uint8_t{3});      // page, low byte
    DOSBASE_TEST_EXPECT_EQ((*bytes)[bucket_of("ALLOC")], std::uint8_t{first / 2});
    const std::size_t second = first + 8;                                 // 1 + 5 + 2, already even
    DOSBASE_TEST_EXPECT_EQ((*bytes)[second], std::uint8_t{4});         // length of "FREE"
    DOSBASE_TEST_EXPECT_EQ((*bytes)[37], std::uint8_t{(second + 8) / 2});  // free-space pointer
}

DOSBASE_TEST(dictionary_sizes_itself_at_a_prime_number_of_blocks) {
    // Measured directly against LIB with libraries built to need exactly
    // three, four and six blocks: it took 3, then 5, then 7, skipping the
    // composite sizes. Twelve-character names cost sixteen bytes each and a
    // block holds 474, so the symbol counts below reproduce those cases.
    const auto blocks_for_count = [](int count) {
        std::vector<DictionarySymbol> symbols;
        for (int index = 0; index < count; ++index) {
            std::string name = "V" + std::to_string(index);
            name.resize(12, 'X');
            symbols.push_back({name, static_cast<std::uint16_t>(index + 1)});
        }
        return dictionary_blocks_for(symbols);
    };
    DOSBASE_TEST_EXPECT_EQ(blocks_for_count(70), std::size_t{3});   // needs 3, takes 3
    DOSBASE_TEST_EXPECT_EQ(blocks_for_count(100), std::size_t{5});  // needs 4, takes 5
    DOSBASE_TEST_EXPECT_EQ(blocks_for_count(160), std::size_t{7});  // needs 6, takes 7
}

DOSBASE_TEST(dictionary_reports_when_symbols_do_not_fit) {
    std::vector<DictionarySymbol> symbols;
    for (int index = 0; index < 40; ++index) {
        symbols.push_back({"SYMBOL" + std::to_string(index), static_cast<std::uint16_t>(index + 1)});
    }
    // Thirty-eight buckets are not enough for forty names, whatever the space.
    DOSBASE_TEST_EXPECT(!place_dictionary(symbols, 1).has_value());
    DOSBASE_TEST_EXPECT(dictionary_blocks_for(symbols) >= 2);
    DOSBASE_TEST_EXPECT(place_dictionary(symbols, dictionary_blocks_for(symbols)).has_value());
}

DOSBASE_TEST(library_response_files_parse_as_lib_reads_them) {
    // MAPPER.LBR's shape: the library, the answer to the create prompt,
    // `+name` operations continued with `&` — some without an extension —
    // and a listing file before the closing semicolon.
    const auto request = dosbase::devtools::parse_library_response(
        "mapper.lib\r\ny\r\n+CHDIR&\r\n+SET_TOD.obj&\r\n+setint24.obj\r\nmapper.lst;\r\n\x1A");
    DOSBASE_TEST_EXPECT_EQ(request.library, std::string{"mapper.lib"});
    DOSBASE_TEST_EXPECT_EQ(request.listing, std::string{"mapper.lst"});
    DOSBASE_TEST_EXPECT_EQ(request.additions.size(), std::size_t{3});
    DOSBASE_TEST_EXPECT_EQ(request.additions[0], std::string{"CHDIR"});
    DOSBASE_TEST_EXPECT_EQ(request.additions[2], std::string{"setint24.obj"});
}

DOSBASE_TEST(library_listing_lays_symbols_out_in_eighteen_column_fields) {
    dosbase::devtools::LibraryBuild build;
    build.pages = {1, 10};
    build.sizes = {0x2C, 0x29};
    build.publics = {{"DosChDir"}, {"dosgetmachinemode"}};
    const std::vector<dosbase::devtools::LibraryInput> members{{"CHDIR", {}}, {"GETVER", {}}};
    const std::string listing = dosbase::devtools::build_listing(members, build);
    // Cross-reference: the name is dotted out to eighteen columns.
    DOSBASE_TEST_EXPECT(listing.find("DosChDir..........CHDIR") == 0);
    // A module heading carries the offset and size in lowercase hex.
    DOSBASE_TEST_EXPECT(listing.find("CHDIR             Offset: 00000010H  Code and data size: 2cH")
                           != std::string::npos);
    // A name of eight characters is padded to its sixteen-column field...
    DOSBASE_TEST_EXPECT(listing.find("\r\n  DosChDir        \r\n") != std::string::npos);
    // ...while one of seventeen takes a second column.
    DOSBASE_TEST_EXPECT(listing.find("\r\n  dosgetmachinemode                 ") != std::string::npos);
}
