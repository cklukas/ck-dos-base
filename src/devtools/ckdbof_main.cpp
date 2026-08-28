// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// DBOF.EXE's replacement for the reference build. The pinned tree
// exists in the vendored source only as a binary — no source anywhere in
// the tree builds it — so this is measured from its one captured invocation
// and its input/output pair in the reference corpus, not from a manual.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dbof.hpp"
#include "dosbase/devtools/dos_file.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool parse_hex(const std::string& text, std::size_t& value) {
    if (text.empty()) return false;
    unsigned long long parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    value = static_cast<std::size_t>(parsed);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.size() != 4) {
        std::cerr << "usage: ckdbof INPUT OUTPUT OFFSET LENGTH  (OFFSET, LENGTH in hex)\n";
        return 2;
    }
    std::size_t offset = 0;
    std::size_t length = 0;
    if (!parse_hex(arguments[2], offset) || !parse_hex(arguments[3], length)) {
        std::cerr << "ckdbof: OFFSET and LENGTH must be plain hexadecimal numbers\n";
        return 2;
    }
    try {
        const std::filesystem::path input = resolve_dos_input(arguments[0]);
        const std::vector<std::uint8_t> file = read_file(input.string());
        const std::string text = dbof(file, offset, length);
        const std::filesystem::path output = resolve_dos_output(arguments[1]);
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::cerr << "ckdbof: cannot write " << output.string() << "\n";
            return 1;
        }
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream) {
            std::cerr << "ckdbof: cannot write " << output.string() << "\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "ckdbof: " << arguments[0] << ": " << error.what() << "\n";
        return 1;
    }
    return 0;
}
