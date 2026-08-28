// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/omf_library.hpp"

#include "dosbase/devtools/binary.hpp"

#include <algorithm>
#include <string_view>

namespace dosbase::devtools {
namespace {

constexpr std::uint8_t kLibraryHeader = 0xF0;
constexpr std::uint8_t kLibraryEnd = 0xF1;
constexpr std::uint8_t kExtendedDictionary = 0xF2;
constexpr std::size_t kDictionaryBlockBytes = 512;
constexpr std::size_t kDictionaryBuckets = 37;

bool all_zero(std::span<const std::uint8_t> bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t value) { return value == 0; });
}

std::vector<LibrarySymbol> parse_dictionary(std::span<const std::uint8_t> dictionary) {
    std::vector<LibrarySymbol> symbols;
    const std::size_t blocks = dictionary.size() / kDictionaryBlockBytes;
    for (std::size_t block = 0; block < blocks; ++block) {
        const auto page = dictionary.subspan(block * kDictionaryBlockBytes, kDictionaryBlockBytes);
        for (std::size_t bucket = 0; bucket < kDictionaryBuckets; ++bucket) {
            const std::size_t slot = page[bucket] * 2U;
            if (slot == 0) continue;
            if (slot >= kDictionaryBlockBytes) continue;
            const std::size_t length = page[slot];
            if (length == 0 || slot + 1U + length + 2U > kDictionaryBlockBytes) continue;
            LibrarySymbol symbol;
            symbol.name.assign(reinterpret_cast<const char*>(page.data() + slot + 1U), length);
            symbol.page = static_cast<std::uint16_t>(page[slot + 1U + length] |
                                                     (page[slot + 2U + length] << 8));
            symbol.block = static_cast<std::uint16_t>(block);
            symbol.bucket = static_cast<std::uint8_t>(bucket);
            symbols.push_back(std::move(symbol));
        }
    }
    return symbols;
}

}  // namespace

OmfLibrary OmfLibrary::parse(std::span<const std::uint8_t> bytes) {
    ByteCursor cursor(bytes);
    OmfLibrary library;
    if (cursor.byte() != kLibraryHeader) {
        throw FormatError(0, "not an OMF object library (no 0xF0 header record)");
    }
    const std::uint16_t header_length = cursor.word();
    library.page_size_ = static_cast<std::size_t>(header_length) + 3U;
    if (library.page_size_ < 16 || (library.page_size_ & (library.page_size_ - 1U)) != 0) {
        throw FormatError(1, "library page size is not a power of two of at least 16");
    }
    library.dictionary_offset_ = cursor.dword();
    library.dictionary_blocks_ = cursor.word();
    library.flags_ = cursor.byte();
    if (library.dictionary_offset_ > bytes.size()) {
        throw FormatError(3, "dictionary offset runs past the end of the file");
    }
    if (library.dictionary_offset_ % library.page_size_ != 0) {
        throw FormatError(3, "dictionary offset is not page-aligned");
    }
    const std::size_t dictionary_bytes =
        static_cast<std::size_t>(library.dictionary_blocks_) * kDictionaryBlockBytes;
    if (dictionary_bytes > bytes.size() - library.dictionary_offset_) {
        throw FormatError(7, "dictionary runs past the end of the file");
    }
    cursor.seek(0);
    const auto prologue = cursor.take(library.page_size_);
    library.prologue_.assign(prologue.begin(), prologue.end());

    while (cursor.position() < library.dictionary_offset_) {
        if (bytes[cursor.position()] == kLibraryEnd) break;
        LibraryMember member;
        member.offset = cursor.position();
        const auto rest = bytes.subspan(cursor.position(), library.dictionary_offset_ - cursor.position());
        std::size_t consumed = 0;
        member.records = parse_omf_records(rest, cursor.position(), true, &consumed);
        if (member.records.empty()) break;
        const OmfRecord& last = member.records.back();
        if ((last.type & 0xFEU) != static_cast<std::uint8_t>(OmfType::modend)) {
            throw FormatError(member.offset, "library member does not end with MODEND");
        }
        const OmfRecord& first = member.records.front();
        if ((first.type & 0xFEU) == static_cast<std::uint8_t>(OmfType::theadr)) {
            ByteCursor name_cursor(first.body, first.offset + 3U);
            member.name = name_cursor.name();
        }
        cursor.seek(cursor.position() + consumed);
        const std::size_t next = ((cursor.position() + library.page_size_ - 1U) / library.page_size_) *
                                 library.page_size_;
        const std::size_t padding = std::min(next, static_cast<std::size_t>(library.dictionary_offset_)) -
                                    cursor.position();
        const auto slack = cursor.take(padding);
        member.padding.assign(slack.begin(), slack.end());
        library.members_.push_back(std::move(member));
    }
    const auto end_region = cursor.take(library.dictionary_offset_ - cursor.position());
    library.end_record_.assign(end_region.begin(), end_region.end());
    library.has_end_record_ = !library.end_record_.empty() && library.end_record_.front() == kLibraryEnd;

    const auto dictionary = cursor.take(dictionary_bytes);
    library.dictionary_.assign(dictionary.begin(), dictionary.end());
    library.symbols_ = parse_dictionary(library.dictionary_);
    const auto trailing = cursor.take(cursor.remaining());
    library.trailing_.assign(trailing.begin(), trailing.end());
    library.extended_dictionary_ =
        !library.trailing_.empty() && library.trailing_.front() == kExtendedDictionary;
    return library;
}

std::vector<std::uint8_t> OmfLibrary::serialize() const {
    std::vector<std::uint8_t> out = prologue_;
    for (const LibraryMember& member : members_) {
        const std::vector<std::uint8_t> records = serialize_omf_records(member.records);
        out.insert(out.end(), records.begin(), records.end());
        out.insert(out.end(), member.padding.begin(), member.padding.end());
    }
    out.insert(out.end(), end_record_.begin(), end_record_.end());
    out.insert(out.end(), dictionary_.begin(), dictionary_.end());
    out.insert(out.end(), trailing_.begin(), trailing_.end());
    return out;
}


namespace {

constexpr std::size_t kBuckets = 37;
constexpr std::size_t kBlockBytes = 512;
constexpr std::size_t kBlockHeader = kBuckets + 1;  // bucket table plus the free-space byte

std::uint16_t rotate_right(std::uint16_t value, int amount) {
    return static_cast<std::uint16_t>((value >> amount) | (value << (16 - amount)));
}

std::uint16_t rotate_left(std::uint16_t value, int amount) {
    return static_cast<std::uint16_t>((value << amount) | (value >> (16 - amount)));
}

// LIB folds case by setting bit 5 on every byte, not by `tolower`.
std::uint8_t fold(char character) {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(character) | 0x20U);
}

std::size_t entry_bytes(std::string_view name) {
    const std::size_t size = 1 + name.size() + 2;
    return size + (size % 2);  // entries start at even offsets
}

}  // namespace

DictionaryHash dictionary_hash(std::string_view name) {
    DictionaryHash hash;
    for (auto character = name.rbegin(); character != name.rend(); ++character) {
        hash.bucket_index = static_cast<std::uint16_t>(rotate_right(hash.bucket_index, 2) ^
                                                       fold(*character));
        hash.block_step = static_cast<std::uint16_t>(rotate_left(hash.block_step, 2) ^
                                                     fold(*character));
    }
    const auto seed = static_cast<std::uint16_t>(name.size() | 0x20U);
    hash.bucket_step = seed;
    hash.block_index = seed;
    if (!name.empty()) {
        for (std::size_t index = 0; index + 1 < name.size(); ++index) {
            hash.bucket_step = static_cast<std::uint16_t>(rotate_right(hash.bucket_step, 2) ^
                                                          fold(name[index]));
            hash.block_index = static_cast<std::uint16_t>(rotate_left(hash.block_index, 2) ^
                                                          fold(name[index]));
        }
    }
    return hash;
}

namespace {

// The order LIB inserts in: ascending by the folded name.
std::vector<std::size_t> insertion_order(const std::vector<DictionarySymbol>& symbols) {
    std::vector<std::size_t> order(symbols.size());
    for (std::size_t index = 0; index < symbols.size(); ++index) order[index] = index;
    std::stable_sort(order.begin(), order.end(), [&symbols](std::size_t left, std::size_t right) {
        const std::string& one = symbols[left].name;
        const std::string& other = symbols[right].name;
        const std::size_t shared = std::min(one.size(), other.size());
        for (std::size_t index = 0; index < shared; ++index) {
            if (fold(one[index]) != fold(other[index])) return fold(one[index]) < fold(other[index]);
        }
        return one.size() < other.size();
    });
    return order;
}

}  // namespace

namespace {

// A block that has once refused an entry for lack of room is marked full by
// writing 0xFF into its free-space byte, which is why a real dictionary can
// show a free pointer past its last entry.
struct DictionaryState {
    std::vector<std::vector<int>> occupant;
    std::vector<std::size_t> used;
    std::vector<bool> declined;
    std::vector<DictionaryPlacement> placement;
};

std::optional<DictionaryState> lay_out(const std::vector<DictionarySymbol>& symbols,
                                       std::size_t blocks);

}  // namespace

std::optional<std::vector<DictionaryPlacement>> place_dictionary(
    const std::vector<DictionarySymbol>& symbols, std::size_t blocks) {
    const auto state = lay_out(symbols, blocks);
    if (!state) return std::nullopt;
    return state->placement;
}

namespace {

std::optional<DictionaryState> lay_out(const std::vector<DictionarySymbol>& symbols,
                                       std::size_t blocks) {
    if (blocks == 0) return std::nullopt;
    DictionaryState state;
    state.occupant.assign(blocks, std::vector<int>(kBuckets, -1));
    state.used.assign(blocks, kBlockHeader);
    state.declined.assign(blocks, false);
    state.placement.resize(symbols.size());
    auto& occupant = state.occupant;
    auto& used = state.used;
    auto& placement = state.placement;
    std::vector<bool> placed(symbols.size(), false);
    for (const std::size_t index : insertion_order(symbols)) {
        const DictionarySymbol& symbol = symbols[index];
        const DictionaryHash hash = dictionary_hash(symbol.name);
        const std::size_t need = entry_bytes(symbol.name);
        std::size_t bucket = hash.bucket_index % kBuckets;
        const std::size_t bucket_step = (hash.bucket_step % kBuckets) != 0
                                            ? hash.bucket_step % kBuckets
                                            : 1;
        std::size_t block = hash.block_index % blocks;
        const std::size_t block_step = (hash.block_step % blocks) != 0
                                           ? hash.block_step % blocks
                                           : 1;
        for (std::size_t attempt = 0; attempt < blocks && !placed[index]; ++attempt) {
            for (std::size_t probe = 0; probe < kBuckets; ++probe) {
                if (occupant[block][bucket] >= 0) {
                    bucket = (bucket + bucket_step) % kBuckets;
                    continue;
                }
                if (used[block] + need > kBlockBytes) {
                    state.declined[block] = true;  // the block is out of room from here on
                    break;                         // keep this bucket and try the next block
                }
                occupant[block][bucket] = static_cast<int>(index);
                used[block] += need;
                placement[index] = {block, static_cast<std::uint8_t>(bucket)};
                placed[index] = true;
                break;
            }
            block = (block + block_step) % blocks;
        }
        if (!placed[index]) return std::nullopt;
    }
    return state;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> build_dictionary(
    const std::vector<DictionarySymbol>& symbols, std::size_t blocks) {
    const auto state = lay_out(symbols, blocks);
    if (!state) return std::nullopt;
    const std::vector<DictionaryPlacement>* placement = &state->placement;
    std::vector<std::uint8_t> bytes(blocks * kBlockBytes, 0);
    std::vector<std::size_t> free_offset(blocks, kBlockHeader);
    for (const std::size_t index : insertion_order(symbols)) {
        const DictionarySymbol& symbol = symbols[index];
        const DictionaryPlacement& spot = (*placement)[index];
        const std::size_t base = spot.block * kBlockBytes;
        const std::size_t at = free_offset[spot.block];
        bytes[base + spot.bucket] = static_cast<std::uint8_t>(at / 2);
        bytes[base + at] = static_cast<std::uint8_t>(symbol.name.size());
        for (std::size_t character = 0; character < symbol.name.size(); ++character) {
            bytes[base + at + 1 + character] = static_cast<std::uint8_t>(symbol.name[character]);
        }
        bytes[base + at + 1 + symbol.name.size()] = static_cast<std::uint8_t>(symbol.page & 0xFF);
        bytes[base + at + 2 + symbol.name.size()] = static_cast<std::uint8_t>(symbol.page >> 8);
        free_offset[spot.block] += entry_bytes(symbol.name);
    }
    for (std::size_t block = 0; block < blocks; ++block) {
        bytes[block * kBlockBytes + kBuckets] =
            state->declined[block] ? 0xFF
                                   : static_cast<std::uint8_t>(free_offset[block] / 2);
    }
    return bytes;
}

std::size_t dictionary_blocks_for(const std::vector<DictionarySymbol>& symbols) {
    // LIB only ever sizes a dictionary at a prime number of blocks — the
    // libraries of the tree run 2, 5, 7, 11, 17, 23, 31, 37 — which is what a
    // modulo-based block index wants. It takes the first size its symbols fit
    // in, so a library can hold five blocks where four would have done.
    for (std::size_t blocks = 1; blocks <= 8192; ++blocks) {
        if (blocks > 2) {
            bool prime = blocks % 2 != 0;
            for (std::size_t factor = 3; prime && factor * factor <= blocks; factor += 2) {
                if (blocks % factor == 0) prime = false;
            }
            if (!prime) continue;
        }
        if (place_dictionary(symbols, blocks)) return blocks;
    }
    return 0;
}

void dump_library(const OmfLibrary& library, const std::string& label, bool records, bool symbols,
                  bool data, std::string& out) {
    out += "library " + label + "\n";
    out += "  page_size " + std::to_string(library.page_size()) + "\n";
    out += "  flags " + hex8(library.flags()) + "\n";
    out += "  members " + std::to_string(library.members().size()) + "\n";
    out += "  dictionary_offset " + hex32(library.dictionary_offset()) + " blocks " +
           std::to_string(library.dictionary_blocks()) + " symbols " +
           std::to_string(library.symbols().size()) + "\n";
    out += "  end_record " + std::to_string(library.end_record().size()) +
           (library.has_end_record() ? " f1\n" : " none\n");
    out += "  trailing_bytes " + std::to_string(library.trailing().size()) +
           (library.has_extended_dictionary() ? " extended_dictionary" : "") + "\n";
    std::size_t index = 0;
    for (const LibraryMember& member : library.members()) {
        out += "  member " + std::to_string(index) + " page " +
               std::to_string(member.offset / library.page_size()) + " offset " +
               hex32(static_cast<std::uint32_t>(member.offset)) + " records " +
               std::to_string(member.records.size()) + " name " + quoted(member.name) +
               " padding " + std::to_string(member.padding.size()) +
               (all_zero(member.padding) ? " zero" : " nonzero") + "\n";
        if (records) {
            OmfNameTable table;
            for (const OmfRecord& record : member.records) {
                dump_omf_record(record, table, "    ", data, out);
            }
        }
        ++index;
    }
    if (symbols) {
        for (const LibrarySymbol& symbol : library.symbols()) {
            out += "  symbol " + quoted(symbol.name) + " page " + std::to_string(symbol.page) +
                   " block " + std::to_string(symbol.block) + " bucket " +
                   std::to_string(symbol.bucket) + "\n";
        }
    }
}

}  // namespace dosbase::devtools
