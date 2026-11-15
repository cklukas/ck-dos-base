// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// COMPRESS.COM's replacement for the MS-DOS 4 SELECT build.
// With no arguments, compresses SEL-PAN.DAT -> SELECT.DAT.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/compress.hpp"
#include "dosbase/devtools/dos_file.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.size() > 2) {
        std::cerr << "usage: ckcompress [INPUT [OUTPUT]]\n";
        return 2;
    }

    const std::string input_name = arguments.size() >= 1 ? arguments[0] : "SEL-PAN.DAT";
    const std::string output_name = arguments.size() >= 2 ? arguments[1] : "SELECT.DAT";

    try {
        const std::filesystem::path input = resolve_dos_input(input_name);
        const std::vector<std::uint8_t> raw = read_file(input.string());
        const std::vector<std::uint8_t> compressed = compress_rle(raw);

        const std::filesystem::path output = resolve_dos_output(output_name);
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::cerr << "ckcompress: cannot create " << output.string() << "\n";
            return 1;
        }
        stream.write(reinterpret_cast<const char*>(compressed.data()),
                     static_cast<std::streamsize>(compressed.size()));
        if (!stream) {
            std::cerr << "ckcompress: write error on " << output.string() << "\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "ckcompress: " << input_name << ": " << error.what() << "\n";
        return 1;
    }
    return 0;
}
