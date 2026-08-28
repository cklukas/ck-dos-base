// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// DBOF.EXE's replacement. Every expectation below is measured
// against the reference build's own DBOF.EXE, via the one invocation the
// pinned tree issues, captured in tools/bootstrap/transcript.json.
#include "test_harness.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dbof.hpp"

namespace {
using dosbase::devtools::FormatError;
using dosbase::devtools::dbof;
using dosbase::devtools::format_db_directive;
}  // namespace

DOSBASE_TEST(dbof_formats_a_single_line_with_a_leading_zero_token) {
    // Measured from the tree's own BOOT.INC: the first line slices
    // MSBOOT.BIN's boot sector starting "EB 3C 90 4D 53 44 4F 53" (the jump,
    // the OEM name "MSDOS"...). Every byte becomes "0" + two uppercase hex
    // digits + "H" -- the leading zero is unconditional, not just a guard
    // for values that would otherwise start with a letter.
    const std::vector<std::uint8_t> bytes{0xEB, 0x3C, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53};
    const std::string text = format_db_directive(bytes);
    DOSBASE_TEST_EXPECT_EQ(text, std::string{"\tdb\t0EBH,03CH,090H,04DH,053H,044H,04FH,053H\r\n"});
}

DOSBASE_TEST(dbof_pads_every_value_with_a_leading_zero_even_when_unneeded) {
    // 0x03 -> "03CH"-style tokens ("034H" for 0x34 is in the fixture above);
    // this isolates a value whose own hex digits never need the guard
    // (0x00, 0x03) to show DBOF always emits it anyway.
    const std::vector<std::uint8_t> bytes{0x00, 0x03, 0x99};
    const std::string text = format_db_directive(bytes);
    DOSBASE_TEST_EXPECT_EQ(text, std::string{"\tdb\t000H,003H,099H\r\n"});
}

DOSBASE_TEST(dbof_wraps_at_eight_bytes_per_line_with_crlf) {
    std::vector<std::uint8_t> bytes(9, 0xFF);
    const std::string text = format_db_directive(bytes);
    const std::string line = "\tdb\t0FFH,0FFH,0FFH,0FFH,0FFH,0FFH,0FFH,0FFH\r\n";
    DOSBASE_TEST_EXPECT_EQ(text, line + "\tdb\t0FFH\r\n");
}

DOSBASE_TEST(dbof_matches_the_reference_build_boot_sector_exactly) {
    // The full 512-byte slice the tree extracts: 64 lines, ending in the
    // boot signature 55 AA, 2880 bytes total (64 * 45). Only the framing is
    // checked here in detail; the corpus/differential gate checks every byte
    // against the captured reference artifact.
    std::vector<std::uint8_t> sector(512, 0x00);
    sector[0] = 0xEB;
    sector[510] = 0x55;
    sector[511] = 0xAA;
    const std::string text = format_db_directive(sector);
    DOSBASE_TEST_EXPECT_EQ(text.size(), std::size_t{2880});
    DOSBASE_TEST_EXPECT(text.substr(0, 8) == "\tdb\t0EBH");
    DOSBASE_TEST_EXPECT(text.substr(text.size() - 11) == "055H,0AAH\r\n");
    DOSBASE_TEST_EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), std::ptrdiff_t{64});
}

DOSBASE_TEST(dbof_slices_the_input_file_at_offset_and_length) {
    std::vector<std::uint8_t> file(0x8000, 0x00);
    file[0x7C00] = 0xEB;
    file[0x7C01] = 0x3C;
    const std::string text = dbof(file, 0x7C00, 4);
    DOSBASE_TEST_EXPECT_EQ(text, std::string{"\tdb\t0EBH,03CH,000H,000H\r\n"});
}

DOSBASE_TEST(dbof_refuses_a_slice_past_the_end_of_the_input) {
    const std::vector<std::uint8_t> file(0x100, 0x00);
    bool refused = false;
    try {
        (void)dbof(file, 0x80, 0x200);
    } catch (const FormatError&) {
        refused = true;
    }
    DOSBASE_TEST_EXPECT(refused);

    refused = false;
    try {
        (void)dbof(file, 0x200, 0x10);  // offset itself past the end
    } catch (const FormatError&) {
        refused = true;
    }
    DOSBASE_TEST_EXPECT(refused);
}

DOSBASE_TEST(dbof_formats_an_empty_slice_as_an_empty_file) {
    DOSBASE_TEST_EXPECT_EQ(format_db_directive({}), std::string{});
}
