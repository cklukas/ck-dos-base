// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// EXE2BIN.EXE's replacement for the reference build. The pinned
// tree always names the output file, so this refuses to derive one instead of
// guessing at unverified behavior.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/exe2bin.hpp"
#include "dosbase/devtools/mz_image.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

// The image carries fix-ups, so the base paragraph comes from the console —
// in the reference build, redirected from a file (`<locscr`).
std::optional<std::uint16_t> read_base_segment() {
    std::cout << dosbase::devtools::kBaseSegmentPrompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return std::nullopt;
    std::size_t first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) return std::nullopt;
    std::size_t last = line.find_last_not_of(" \t\r");
    const char* begin = line.data() + first;
    const char* end = line.data() + last + 1;
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(begin, end, value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value > 0xFFFF) return std::nullopt;
    return static_cast<std::uint16_t>(value);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.size() != 1 && arguments.size() != 2) {
        std::cerr << "usage: ckexe2bin INPUT.EXE [OUTPUT]\n";
        return 2;
    }
    try {
        std::string in_arg = arguments[0];
        if (in_arg.find('.') == std::string::npos) {
            in_arg += ".EXE";
        }
        const std::filesystem::path input = resolve_dos_input(in_arg);
        const std::string out_arg =
            arguments.size() == 2 ? arguments[1] : (input.stem().string() + ".BIN");
        const MzImage image = MzImage::parse(read_file(input.string()));
        std::optional<std::uint16_t> base_segment;
        if (image.header().relocation_count != 0) {
            base_segment = read_base_segment();
            if (!base_segment.has_value()) {
                std::cerr << "\nckexe2bin: no base segment for an image that needs fix-ups\n";
                return 1;
            }
            std::cout << "\n";
        }
        const Exe2BinResult result = exe2bin(image, base_segment);
        const std::filesystem::path output = resolve_dos_output(out_arg);
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::cerr << "ckexe2bin: cannot write " << output.string() << "\n";
            return 1;
        }
        stream.write(reinterpret_cast<const char*>(result.bytes.data()),
                     static_cast<std::streamsize>(result.bytes.size()));
        if (!stream) {
            std::cerr << "ckexe2bin: cannot write " << output.string() << "\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "ckexe2bin: " << arguments[0] << ": " << error.what() << "\n";
        return 1;
    }
    return 0;
}
