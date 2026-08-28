// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// BUILDIDX.EXE's replacement. `buildidx usa-ms.msg` rewrites the
// message database in place -- CRLF line endings, the level incremented,
// every section header restamped with its offset in the rewritten file --
// and writes the sibling `.IDX`: the level plus the section header lines.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/msgtools.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool write_whole(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    if (argc != 2) {
        std::cerr << "usage: ckbuildidx MESSAGE-FILE\n";
        return 2;
    }
    const std::string argument = argv[1];
    try {
        const std::filesystem::path message_path = resolve_dos_input(argument);
        const std::vector<std::uint8_t> raw = read_file(message_path.string());
        const BuildIdxResult result =
            buildidx(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
        std::string index_name = argument;
        const std::size_t dot = index_name.rfind('.');
        index_name = (dot == std::string::npos ? index_name : index_name.substr(0, dot)) + ".idx";
        const std::filesystem::path index_path = resolve_dos_output(index_name);
        if (!write_whole(message_path, result.message_file) ||
            !write_whole(index_path, result.index_file)) {
            std::cerr << "ckbuildidx: cannot write the updated files\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "ckbuildidx: " << argument << ": " << error.what() << "\n";
        return 1;
    }
    std::cout << "Message file and index file will be updated.\n";
    return 0;
}
