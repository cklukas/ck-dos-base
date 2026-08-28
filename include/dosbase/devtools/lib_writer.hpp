// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include "dosbase/devtools/omf_library.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dosbase::devtools {

// One `+FILE.OBJ` operation of a LIB response file.
struct LibraryInput {
    // The file's stem exactly as the response file spells it: LIB carries
    // that spelling into both the LIBMOD comment and the dictionary entry.
    std::string module_name;
    std::vector<std::uint8_t> object;  // the object file, as read
};

struct LibraryBuild {
    std::vector<std::uint8_t> bytes;
    std::vector<DictionarySymbol> symbols;  // dictionary contents, module entries included
    std::vector<std::uint16_t> pages;       // the page each member starts on
    std::vector<std::uint16_t> sizes;       // each member's code and data size, for the listing
    std::vector<std::vector<std::string>> publics;  // each member's public names, in record order
};

// Writes a library the way LIB 3.10 does (all of it measured against
// the tool): the 0xF0 header, members on page boundaries each carrying an
// inserted LIBMOD comment, an 0xF1 record padding to the dictionary's
// 512-byte boundary, the hashed dictionary, and the 0xF2 extended dictionary
// naming each member's dependencies.
[[nodiscard]] LibraryBuild build_library(const std::vector<LibraryInput>& members,
                                         std::size_t page_size = 16);

// The listing LIB writes when the response file names one: a cross-reference
// of every public symbol to its module, then a section per module with its
// offset, its code and data size, and its symbols. Columns are eighteen wide,
// four to a line, and a name longer than sixteen takes two of them.
[[nodiscard]] std::string build_listing(const std::vector<LibraryInput>& members,
                                        const LibraryBuild& build, std::size_t page_size = 16);

// A LIB response file: the library name, an optional `y` answering the
// "Library does not exist. Create?" prompt, `+name` operations continued with
// `&`, and an optional listing file before the closing `;`.
struct LibraryRequest {
    std::string library;
    std::string listing;
    std::vector<std::string> additions;
};

[[nodiscard]] LibraryRequest parse_library_response(std::string_view text);

}  // namespace dosbase::devtools
