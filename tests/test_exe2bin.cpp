// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/exe2bin.hpp"
#include "dosbase/devtools/mz_image.hpp"

namespace {
namespace fs = std::filesystem;
using dosbase::devtools::Exe2BinKind;
using dosbase::devtools::FormatError;
using dosbase::devtools::MzImage;
using dosbase::devtools::exe2bin;

void put_word(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

// An MZ with a 512-byte header, as LINK writes for this tree.
std::vector<std::uint8_t> executable(std::uint16_t initial_ip, std::size_t image_bytes,
                                     std::uint16_t relocations = 0, std::uint16_t stack = 0) {
    std::vector<std::uint8_t> bytes(512 + image_bytes, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    const std::size_t total = bytes.size();
    put_word(bytes, 2, static_cast<std::uint16_t>(total % 512));
    put_word(bytes, 4, static_cast<std::uint16_t>((total + 511) / 512));
    put_word(bytes, 6, relocations);
    put_word(bytes, 8, 32);
    put_word(bytes, 14, stack);   // initial_ss
    put_word(bytes, 16, stack);   // initial_sp
    put_word(bytes, 20, initial_ip);
    put_word(bytes, 24, 28);
    for (std::size_t index = 0; index < image_bytes; ++index) {
        bytes[512 + index] = static_cast<std::uint8_t>(index & 0xFF);
    }
    return bytes;
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("dosbase-tools-" + std::to_string(stamp) + "-" +
                 std::to_string(sequence.fetch_add(1)));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};
}  // namespace

DOSBASE_TEST(exe2bin_converts_a_pure_binary_image) {
    // MSBOOT.EXE's shape: entry 0000:0000, whole load image.
    const MzImage image = MzImage::parse(executable(0x0000, 1024));
    const auto result = exe2bin(image);
    DOSBASE_TEST_EXPECT(result.kind == Exe2BinKind::pure_binary);
    DOSBASE_TEST_EXPECT_EQ(result.bytes.size(), std::size_t{1024});
    DOSBASE_TEST_EXPECT_EQ(result.bytes.front(), std::uint8_t{0});
}

DOSBASE_TEST(exe2bin_converts_a_com_image_past_its_origin) {
    // SYS.EXE's shape: entry 0000:0100, image past the first 0x100 bytes.
    const MzImage image = MzImage::parse(executable(0x0100, 1024));
    const auto result = exe2bin(image);
    DOSBASE_TEST_EXPECT(result.kind == Exe2BinKind::com_image);
    DOSBASE_TEST_EXPECT_EQ(result.bytes.size(), std::size_t{1024 - 0x100});
    DOSBASE_TEST_EXPECT_EQ(result.bytes.front(), std::uint8_t{0x00});
    DOSBASE_TEST_EXPECT_EQ(result.bytes[1], std::uint8_t{0x01});
}

DOSBASE_TEST(exe2bin_relocates_an_image_that_needs_fixups) {
    // MSBIO.EXE's shape: 41 fix-ups and a base segment of 0x70 supplied on
    // the console, which the makefile redirects from LOCSCR.
    std::vector<std::uint8_t> bytes = executable(0x0000, 512, 2);
    put_word(bytes, 24, 28);            // relocation table at the fixed header
    put_word(bytes, 28, 0x0100);        // first fix-up at image offset 0x100
    put_word(bytes, 30, 0x0000);
    put_word(bytes, 32, 0x0000);        // second fix-up at paragraph 1, offset 0
    put_word(bytes, 34, 0x0001);
    put_word(bytes, 512 + 0x100, 0x0123);
    put_word(bytes, 512 + 0x10, 0x8000);
    const MzImage image = MzImage::parse(bytes);
    const auto result = exe2bin(image, std::uint16_t{0x0070});
    DOSBASE_TEST_EXPECT(result.kind == Exe2BinKind::pure_binary);
    const auto word = [&result](std::size_t at) {
        return static_cast<std::uint16_t>(result.bytes[at] | (result.bytes[at + 1] << 8));
    };
    DOSBASE_TEST_EXPECT_EQ(word(0x100), std::uint16_t{0x0193});
    DOSBASE_TEST_EXPECT_EQ(word(0x10), std::uint16_t{0x8070});
    // Every other byte is untouched.
    DOSBASE_TEST_EXPECT_EQ(result.bytes[0x20], std::uint8_t{0x20});
}

DOSBASE_TEST(exe2bin_pairs_fixups_and_base_segment_or_refuses) {
    const std::vector<std::uint8_t> relocatable = executable(0x0000, 512, 1);
    bool refused = false;
    try {
        (void)exe2bin(MzImage::parse(relocatable));  // fix-ups, no base segment
    } catch (const FormatError&) {
        refused = true;
    }
    DOSBASE_TEST_EXPECT(refused);

    refused = false;
    try {
        (void)exe2bin(MzImage::parse(executable(0x0000, 512)), std::uint16_t{0x70});
    } catch (const FormatError&) {
        refused = true;  // a base segment for an image with no fix-ups
    }
    DOSBASE_TEST_EXPECT(refused);
}

DOSBASE_TEST(exe2bin_refuses_every_shape_the_tree_does_not_exercise) {
    const auto refuses = [](const std::vector<std::uint8_t>& bytes) {
        try {
            (void)exe2bin(MzImage::parse(bytes));
        } catch (const FormatError&) {
            return true;
        }
        return false;
    };
    DOSBASE_TEST_EXPECT(refuses(executable(0x0000, 512, 2)));       // fix-ups, no base
    DOSBASE_TEST_EXPECT(refuses(executable(0x0000, 512, 0, 0x40)));  // declares a stack
    DOSBASE_TEST_EXPECT(refuses(executable(0x0200, 512)));           // unexercised entry
    std::vector<std::uint8_t> with_overlay = executable(0x0000, 512);
    with_overlay.push_back(0xAA);
    DOSBASE_TEST_EXPECT(refuses(with_overlay));
    std::vector<std::uint8_t> short_image = executable(0x0100, 0x80);
    DOSBASE_TEST_EXPECT(refuses(short_image));
    std::vector<std::uint8_t> entry_segment = executable(0x0000, 512);
    put_word(entry_segment, 22, 0x0010);
    DOSBASE_TEST_EXPECT(refuses(entry_segment));
}

DOSBASE_TEST(dos_paths_resolve_case_insensitively_and_write_uppercase) {
    // A makefile says `sys.exe`; FAT holds `SYS.EXE`. On Linux the two are
    // different files, so a reimplemented tool must bridge them.
    TemporaryDirectory temporary;
    const fs::path directory = temporary.path();
    std::ofstream(directory / "SYS.EXE", std::ios::binary).put('x');
    fs::create_directory(directory / "INC");
    std::ofstream(directory / "INC" / "BOOT.INC", std::ios::binary).put('y');

    const fs::path found = dosbase::devtools::resolve_dos_input((directory / "sys.exe").string());
    DOSBASE_TEST_EXPECT(fs::exists(found));
    DOSBASE_TEST_EXPECT_EQ(found.filename().string(), std::string{"SYS.EXE"});

    const fs::path nested =
        dosbase::devtools::resolve_dos_input((directory.string() + "/inc\\boot.inc"));
    DOSBASE_TEST_EXPECT(fs::exists(nested));

    const fs::path output = dosbase::devtools::resolve_dos_output((directory / "sys.com").string());
    DOSBASE_TEST_EXPECT_EQ(output.filename().string(), std::string{"SYS.COM"});
    DOSBASE_TEST_EXPECT_EQ(output.parent_path(), directory);

    const fs::path missing = dosbase::devtools::resolve_dos_input((directory / "absent.obj").string());
    DOSBASE_TEST_EXPECT(!fs::exists(missing));
}
