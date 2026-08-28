// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include "dosbase/devtools/omf_record.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dosbase::devtools {

struct LibraryMember {
    std::size_t offset{};            // page-aligned file offset of the first record
    std::vector<OmfRecord> records;  // THEADR .. MODEND
    std::vector<std::uint8_t> padding;  // bytes to the next page boundary
    std::string name;                // THEADR name, or empty when the member has none
};

struct LibrarySymbol {
    std::string name;
    std::uint16_t page{};     // member page number
    std::uint16_t block{};    // dictionary block that holds the entry
    std::uint8_t bucket{};    // slot within the block
};

// A whole-file model of a Microsoft/Intel OMF object library, keeping the
// page padding and the dictionary bytes verbatim so serialize(parse(bytes))
// == bytes for any library that parses.
class OmfLibrary {
public:
    static OmfLibrary parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;

    [[nodiscard]] std::size_t page_size() const noexcept { return page_size_; }
    [[nodiscard]] std::uint32_t dictionary_offset() const noexcept { return dictionary_offset_; }
    [[nodiscard]] std::uint16_t dictionary_blocks() const noexcept { return dictionary_blocks_; }
    [[nodiscard]] std::uint8_t flags() const noexcept { return flags_; }
    [[nodiscard]] const std::vector<LibraryMember>& members() const noexcept { return members_; }
    [[nodiscard]] const std::vector<LibrarySymbol>& symbols() const noexcept { return symbols_; }
    [[nodiscard]] std::span<const std::uint8_t> dictionary() const noexcept { return dictionary_; }
    [[nodiscard]] std::span<const std::uint8_t> trailing() const noexcept { return trailing_; }
    [[nodiscard]] bool has_extended_dictionary() const noexcept { return extended_dictionary_; }
    // LIB.EXE closes the member area with an 0xF1 record that runs up to the
    // dictionary page. Its length is how the writer chose to pad, so it is
    // kept as read rather than recomputed.
    [[nodiscard]] std::span<const std::uint8_t> end_record() const noexcept { return end_record_; }
    [[nodiscard]] bool has_end_record() const noexcept { return has_end_record_; }

private:
    std::size_t page_size_{};
    std::uint32_t dictionary_offset_{};
    std::uint16_t dictionary_blocks_{};
    std::uint8_t flags_{};
    std::vector<std::uint8_t> prologue_;  // header record and its page padding
    std::vector<LibraryMember> members_;
    std::vector<std::uint8_t> end_record_;  // 0xF1 record and slack before the dictionary
    std::vector<std::uint8_t> dictionary_;
    std::vector<std::uint8_t> trailing_;
    std::vector<LibrarySymbol> symbols_;
    bool extended_dictionary_{};
    bool has_end_record_{};
};


// LIB 3.10's dictionary placement, derived by measurement against the tool
// itself () and verified byte-for-byte against
// the reference build's own MAPPER.LIB. Four hashes drive it, all over the
// name with bit 5 set on every byte — case folding that also moves `_` and
// other punctuation, which is what makes it *not* `tolower`.
struct DictionaryHash {
    std::uint16_t bucket_index{};  // 0, name backwards,  h = ror16(h,2) xor fold(c)
    std::uint16_t bucket_step{};   // len|0x20, name minus its last character, ror16
    std::uint16_t block_index{};   // len|0x20, name minus its last character, rol16
    std::uint16_t block_step{};    // 0, name backwards,  rol16
};

[[nodiscard]] DictionaryHash dictionary_hash(std::string_view name);

struct DictionarySymbol {
    std::string name;
    std::uint16_t page{};
    friend bool operator==(const DictionarySymbol&, const DictionarySymbol&) = default;
};

struct DictionaryPlacement {
    std::size_t block{};
    std::uint8_t bucket{};
    friend bool operator==(const DictionaryPlacement&, const DictionaryPlacement&) = default;
};

// Places symbols the way LIB does: in case-insensitive ascending order,
// starting at (block_index, bucket_index), advancing the bucket by
// bucket_step while a bucket is taken, and advancing the block by block_step
// when a free bucket is found in a block that has no room left for the entry
// — *keeping* the bucket it had reached, which is the detail that separates
// this from a fresh probe per block. Returns nothing when the symbols do not
// fit in `blocks` blocks.
[[nodiscard]] std::optional<std::vector<DictionaryPlacement>> place_dictionary(
    const std::vector<DictionarySymbol>& symbols, std::size_t blocks);

// The dictionary blocks exactly as LIB writes them: 37 bucket bytes, a
// free-space byte, then each entry (length, name, page) at an even offset in
// insertion order.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> build_dictionary(
    const std::vector<DictionarySymbol>& symbols, std::size_t blocks);

// The smallest block count that holds these symbols, which is the count LIB
// settles on.
[[nodiscard]] std::size_t dictionary_blocks_for(const std::vector<DictionarySymbol>& symbols);

// Deterministic line-oriented dump. `records` expands every member's OMF
// records, `symbols` expands the dictionary, `data` includes payload bytes.
void dump_library(const OmfLibrary& library, const std::string& label, bool records, bool symbols,
                  bool data, std::string& out);

}  // namespace dosbase::devtools
