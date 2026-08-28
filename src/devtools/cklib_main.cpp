// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// LIB.EXE's replacement for the reference build. The pinned tree
// issues exactly one invocation, `lib @mapper.lbr`, which creates a library
// from `+file.obj` operations and writes a listing, so that is what this
// implements; anything else is refused by name rather than guessed at.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/lib_writer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

// `+CHDIR` and `+SET_TOD.obj` both appear in the tree's own response file, so
// a member named without an extension is an object file.
std::string with_default_extension(const std::string& file) {
    return std::filesystem::path(file).has_extension() ? file : file + ".OBJ";
}

// LIB uses the response file's own spelling: the tree's one lowercase entry,
// `+setint24.obj`, becomes the module `setint24` and the dictionary entry
// `setint24!`, while its uppercase neighbours stay uppercase.
std::string module_name_of(const std::string& file) {
    return std::filesystem::path(file).stem().string();
}

bool write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.size() != 1 || arguments[0].empty() || arguments[0][0] != '@') {
        std::cerr << "usage: cklib @RESPONSE\n"
                  << "  the pinned tree only ever drives LIB from a response file\n";
        return 2;
    }
    try {
        const std::filesystem::path response = resolve_dos_input(arguments[0].substr(1));
        const std::vector<std::uint8_t> text = read_file(response.string());
        const LibraryRequest request = parse_library_response(
            std::string_view(reinterpret_cast<const char*>(text.data()), text.size()));
        if (request.library.empty() || request.additions.empty()) {
            std::cerr << "cklib: the response file names no library or no members\n";
            return 1;
        }
        std::vector<LibraryInput> members;
        members.reserve(request.additions.size());
        for (const std::string& addition : request.additions) {
            const std::filesystem::path object =
                resolve_dos_input(with_default_extension(addition));
            members.push_back({module_name_of(addition), read_file(object.string())});
        }
        const LibraryBuild build = build_library(members);
        if (!write_file(resolve_dos_output(request.library), build.bytes)) {
            std::cerr << "cklib: cannot write " << request.library << "\n";
            return 1;
        }
        if (!request.listing.empty()) {
            const std::string listing = build_listing(members, build);
            const std::vector<std::uint8_t> bytes(listing.begin(), listing.end());
            if (!write_file(resolve_dos_output(request.listing), bytes)) {
                std::cerr << "cklib: cannot write " << request.listing << "\n";
                return 1;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "cklib: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
