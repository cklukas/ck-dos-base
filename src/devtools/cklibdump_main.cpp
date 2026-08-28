// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dump_tool.hpp"
#include "dosbase/devtools/omf_library.hpp"

#include <algorithm>
#include <vector>

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    return run_dump_tool(argc, argv, "cklibdump",
                         [](std::span<const std::uint8_t> bytes, const std::string& label,
                            const DumpOptions& options, std::string& out) {
                             const OmfLibrary library = OmfLibrary::parse(bytes);
                             dump_library(library, label, options.records, options.symbols,
                                          options.data, out);
                             if (options.verify_dictionary) {
                                 std::vector<DictionarySymbol> entries;
                                 entries.reserve(library.symbols().size());
                                 for (const LibrarySymbol& symbol : library.symbols()) {
                                     entries.push_back({symbol.name, symbol.page});
                                 }
                                 const auto rebuilt =
                                     build_dictionary(entries, library.dictionary_blocks());
                                 const auto stored = library.dictionary();
                                 if (!rebuilt) {
                                     out += "  dictionary REBUILD FAILED: symbols do not fit\n";
                                 } else if (rebuilt->size() == stored.size() &&
                                            std::equal(rebuilt->begin(), rebuilt->end(),
                                                       stored.begin())) {
                                     out += "  dictionary rebuilt byte-identically (" +
                                            std::to_string(entries.size()) + " symbols, " +
                                            std::to_string(library.dictionary_blocks()) +
                                            " blocks)\n";
                                 } else {
                                     std::size_t at = 0;
                                     while (at < stored.size() && (*rebuilt)[at] == stored[at]) ++at;
                                     out += "  dictionary MISMATCH at byte " +
                                            hex32(static_cast<std::uint32_t>(at)) + ": stored " +
                                            hex8(stored[at]) + ", rebuilt " + hex8((*rebuilt)[at]) +
                                            "\n";
                                 }
                                 const std::size_t chosen = dictionary_blocks_for(entries);
                                 out += "  dictionary block count " +
                                        std::to_string(library.dictionary_blocks()) +
                                        ", smallest that fits " + std::to_string(chosen) + "\n";
                             }
                             return library.serialize();
                         });
}
