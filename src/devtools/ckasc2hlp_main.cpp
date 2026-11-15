// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// ASC2HLP.EXE's replacement for the MS-DOS 4 SELECT build.
#include "dosbase/devtools/asc2hlp.hpp"
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.size() != 2) {
        std::cerr << "usage: ckasc2hlp INPUT.TXT OUTPUT.HLP\n";
        return 2;
    }

    try {
        const std::filesystem::path input = resolve_dos_input(arguments[0]);
        const std::vector<std::uint8_t> source = read_file(input.string());
        const HelpDocument doc = parse_help_source(source);
        const std::vector<std::uint8_t> hlp = build_cas_help(doc);

        const std::filesystem::path output = resolve_dos_output(arguments[1]);
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::cerr << "ckasc2hlp: cannot create " << output.string() << "\n";
            return 1;
        }
        stream.write(reinterpret_cast<const char*>(hlp.data()),
                     static_cast<std::streamsize>(hlp.size()));
        if (!stream) {
            std::cerr << "ckasc2hlp: write error on " << output.string() << "\n";
            return 1;
        }
        std::cout << "The number topic(s) and text string(s) written = " << doc.entries.size()
                  << "\n";
    } catch (const std::exception& error) {
        std::cerr << "ckasc2hlp: " << arguments[0] << ": " << error.what() << "\n";
        return 1;
    }
    return 0;
}
