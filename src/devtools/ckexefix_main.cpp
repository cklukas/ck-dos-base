// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// EXEFIX.EXE's replacement for the MS-DOS 4 build tree.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/exefix.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::optional<std::uint16_t> parse_hex_u16(const std::string& text) {
    if (text.empty()) return std::nullopt;
    std::uint32_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto parsed = std::from_chars(begin, end, value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.empty() || arguments.size() > 3) {
        std::cerr << "usage: ckexefix <file> [<max-alloc> [<min-alloc>]]\n";
        return 2;
    }

    ExefixOptions options;
    if (arguments.size() >= 2) {
        options.max_alloc = parse_hex_u16(arguments[1]);
        if (!options.max_alloc.has_value()) {
            std::cerr << "ckexefix: invalid hex digit in " << arguments[1] << "\n";
            return 1;
        }
    }
    if (arguments.size() >= 3) {
        options.min_alloc = parse_hex_u16(arguments[2]);
        if (!options.min_alloc.has_value()) {
            std::cerr << "ckexefix: invalid hex digit in " << arguments[2] << "\n";
            return 1;
        }
    }

    try {
        const std::filesystem::path target = resolve_dos_input(arguments[0]);
        const std::vector<std::uint8_t> exe_bytes = read_file(target.string());
        const ExefixResult result = exefix(exe_bytes, options);
        if (result.min_clamped_to_stack) {
            std::cerr << "ckexefix: min < stack\n";
        }
        if (result.max_clamped_to_min) {
            std::cerr << "ckexefix: min > max\n";
        }

        const std::filesystem::path output = resolve_dos_output(target.string());
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::cerr << "ckexefix: cannot write " << output.string() << "\n";
            return 1;
        }
        stream.write(reinterpret_cast<const char*>(result.bytes.data()),
                     static_cast<std::streamsize>(result.bytes.size()));
        if (!stream) {
            std::cerr << "ckexefix: write error on " << output.string() << "\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "ckexefix: " << arguments[0] << ": " << error.what() << "\n";
        return 1;
    }
    return 0;
}
