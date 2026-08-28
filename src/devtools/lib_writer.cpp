// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/lib_writer.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/omf_record.hpp"

#include <algorithm>
#include <map>

namespace dosbase::devtools {
namespace {

constexpr std::uint8_t kLibraryHeader = 0xF0;
constexpr std::uint8_t kLibraryEnd = 0xF1;
constexpr std::uint8_t kExtendedDictionary = 0xF2;
constexpr std::uint8_t kComment = 0x88;
constexpr std::uint8_t kLibmodClass = 0xA3;
constexpr std::size_t kDictionaryBlockBytes = 512;

void append_word(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

std::uint8_t fold_byte(char character) {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(character) | 0x20U);
}

std::uint8_t checksum_for(std::span<const std::uint8_t> record) {
    std::uint32_t sum = 0;
    for (const std::uint8_t value : record) sum += value;
    return static_cast<std::uint8_t>((0x100U - (sum & 0xFFU)) & 0xFFU);
}

// The comment LIB inserts after a member's THEADR to record its module name.
OmfRecord libmod_record(const std::string& module_name) {
    OmfRecord record;
    record.type = kComment;
    record.body.push_back(0x00);          // comment flags
    record.body.push_back(kLibmodClass);  // LIBMOD
    record.body.push_back(static_cast<std::uint8_t>(module_name.size()));
    for (const char character : module_name) {
        record.body.push_back(static_cast<std::uint8_t>(character));
    }
    record.declared_length = static_cast<std::uint16_t>(record.body.size() + 1);
    std::vector<std::uint8_t> prefix{record.type,
                                     static_cast<std::uint8_t>(record.declared_length & 0xFF),
                                     static_cast<std::uint8_t>(record.declared_length >> 8)};
    prefix.insert(prefix.end(), record.body.begin(), record.body.end());
    record.checksum = checksum_for(prefix);
    return record;
}

std::size_t round_up(std::size_t value, std::size_t multiple) {
    return ((value + multiple - 1) / multiple) * multiple;
}

}  // namespace

LibraryBuild build_library(const std::vector<LibraryInput>& members, std::size_t page_size) {
    LibraryBuild build;
    std::vector<std::vector<std::uint8_t>> bodies;
    std::vector<std::vector<std::string>> externals;
    bodies.reserve(members.size());

    std::size_t offset = page_size;  // the header record occupies page zero
    for (const LibraryInput& member : members) {
        std::vector<OmfRecord> records = parse_omf_records(member.object, 0, false);
        if (records.empty()) throw FormatError(0, "member " + member.module_name + " has no records");
        records.insert(records.begin() + 1, libmod_record(member.module_name));
        std::vector<std::uint8_t> body = serialize_omf_records(records);

        const auto page = static_cast<std::uint16_t>(offset / page_size);
        build.pages.push_back(page);
        build.symbols.push_back({member.module_name + "!", page});
        std::vector<std::string> names;
        std::vector<std::string> referenced;
        std::uint16_t size = 0;
        OmfNameTable table;
        for (const OmfRecord& record : records) {
            ByteCursor cursor(record.body, 0);
            const std::uint8_t base = record.type & 0xFEU;
            if (base == static_cast<std::uint8_t>(OmfType::pubdef)) {
                (void)cursor.index();
                const std::uint16_t segment = cursor.index();
                if (segment == 0) (void)cursor.word();
                while (!cursor.empty()) {
                    names.push_back(cursor.name());
                    (void)cursor.word();
                    (void)cursor.index();
                }
            } else if (base == static_cast<std::uint8_t>(OmfType::extdef)) {
                while (!cursor.empty()) {
                    referenced.push_back(cursor.name());
                    (void)cursor.index();
                }
            } else if (base == static_cast<std::uint8_t>(OmfType::segdef)) {
                const std::uint8_t acbp = cursor.byte();
                if ((acbp >> 5) == 0) {
                    (void)cursor.word();
                    (void)cursor.byte();
                }
                size = static_cast<std::uint16_t>(size + cursor.word());
            }
        }
        for (const std::string& name : names) build.symbols.push_back({name, page});
        build.publics.push_back(names);
        externals.push_back(referenced);
        build.sizes.push_back(size);

        const std::size_t padded = round_up(body.size(), page_size);
        body.resize(padded, 0);
        offset += padded;
        bodies.push_back(std::move(body));
    }

    const std::size_t blocks = dictionary_blocks_for(build.symbols);
    const auto dictionary = build_dictionary(build.symbols, blocks);
    if (!dictionary) throw FormatError(0, "the dictionary does not fit the symbols");

    // The dictionary starts on a 512-byte boundary; an 0xF1 record fills the gap.
    std::size_t dictionary_offset = round_up(offset + 3, kDictionaryBlockBytes);
    const std::size_t end_record_bytes = dictionary_offset - offset;

    build.bytes.reserve(dictionary_offset + dictionary->size() + 512);
    build.bytes.push_back(kLibraryHeader);
    append_word(build.bytes, static_cast<std::uint16_t>(page_size - 3));
    append_word(build.bytes, static_cast<std::uint16_t>(dictionary_offset & 0xFFFF));
    append_word(build.bytes, static_cast<std::uint16_t>(dictionary_offset >> 16));
    append_word(build.bytes, static_cast<std::uint16_t>(blocks));
    build.bytes.push_back(0x00);  // flags
    build.bytes.resize(page_size, 0);
    for (const std::vector<std::uint8_t>& body : bodies) {
        build.bytes.insert(build.bytes.end(), body.begin(), body.end());
    }
    build.bytes.push_back(kLibraryEnd);
    append_word(build.bytes, static_cast<std::uint16_t>(end_record_bytes - 3));
    build.bytes.resize(dictionary_offset, 0);
    build.bytes.insert(build.bytes.end(), dictionary->begin(), dictionary->end());

    // The extended dictionary: per member, the members that define the names
    // it references, as zero-based indices.
    std::map<std::string, std::size_t> definer;
    for (std::size_t index = 0; index < members.size(); ++index) {
        for (const std::string& name : build.publics[index]) {
            definer.emplace(dos_upper(name), index);
        }
    }
    std::vector<std::uint8_t> pairs;
    std::vector<std::uint8_t> payload;
    std::size_t entry_offset = 4 * (members.size() + 1);
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::vector<std::uint16_t> uses;
        for (const std::string& name : externals[index]) {
            const auto found = definer.find(dos_upper(name));
            if (found == definer.end()) continue;
            const auto value = static_cast<std::uint16_t>(found->second);
            if (std::find(uses.begin(), uses.end(), value) == uses.end()) uses.push_back(value);
        }
        std::sort(uses.begin(), uses.end());
        append_word(pairs, build.pages[index]);
        append_word(pairs, static_cast<std::uint16_t>(entry_offset));
        append_word(payload, static_cast<std::uint16_t>(uses.size()));
        for (const std::uint16_t value : uses) append_word(payload, value);
        entry_offset += 2 + 2 * uses.size();
    }
    append_word(pairs, 0);
    append_word(pairs, 0);
    std::vector<std::uint8_t> extended;
    append_word(extended, static_cast<std::uint16_t>(members.size()));
    extended.insert(extended.end(), pairs.begin(), pairs.end());
    extended.insert(extended.end(), payload.begin(), payload.end());
    build.bytes.push_back(kExtendedDictionary);
    append_word(build.bytes, static_cast<std::uint16_t>(extended.size()));
    build.bytes.insert(build.bytes.end(), extended.begin(), extended.end());
    return build;
}


namespace {

bool before_folded(const std::string& left, const std::string& right) {
    const std::size_t shared = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < shared; ++index) {
        const std::uint8_t one = fold_byte(left[index]);
        const std::uint8_t other = fold_byte(right[index]);
        if (one != other) return one < other;
    }
    return left.size() < right.size();
}

void pad_to(std::string& line, std::size_t width) {
    if (line.size() < width) line.append(width - line.size(), ' ');
}

std::string lower_hex(std::uint32_t value, int digits) {
    std::string text;
    do {
        text.insert(text.begin(), "0123456789abcdef"[value & 0xF]);
        value >>= 4;
    } while (value != 0);
    while (static_cast<int>(text.size()) < digits) text.insert(text.begin(), '0');
    return text;
}

}  // namespace

std::string build_listing(const std::vector<LibraryInput>& members, const LibraryBuild& build,
                          std::size_t page_size) {
    std::vector<std::pair<std::string, std::string>> cross;
    for (std::size_t index = 0; index < members.size(); ++index) {
        for (const std::string& name : build.publics[index]) {
            cross.emplace_back(name, members[index].module_name);
        }
    }
    std::sort(cross.begin(), cross.end(), [](const auto& left, const auto& right) {
        return before_folded(left.first, right.first);
    });

    std::string out;
    const auto line_end = [&out](const std::string& line) { out += line + "\r\n"; };
    for (std::size_t index = 0; index < cross.size(); index += 2) {
        std::string line;
        for (std::size_t column = 0; column < 2 && index + column < cross.size(); ++column) {
            if (column == 1) pad_to(line, 36);
            const auto& [name, module] = cross[index + column];
            line += name;
            line.append(std::max<std::size_t>(2, 18 - std::min<std::size_t>(18, name.size())), '.');
            line += module;
        }
        line_end(line);
    }
    line_end("");
    line_end("");
    for (std::size_t index = 0; index < members.size(); ++index) {
        std::string head = members[index].module_name;
        pad_to(head, 18);
        head += "Offset: " + lower_hex(static_cast<std::uint32_t>(build.pages[index] * page_size), 8) +
                "H  Code and data size: " + lower_hex(build.sizes[index], 1) + "H";
        line_end(head);
        std::vector<std::string> names = build.publics[index];
        std::sort(names.begin(), names.end(), before_folded);
        std::string line;
        std::size_t column = 0;
        for (const std::string& name : names) {
            if (column >= 4) {
                line_end(line);
                line.clear();
                column = 0;
            }
            pad_to(line, 2 + 18 * column);
            line += name;
            column += 1 + (name.size() > 16 ? 1 : 0);  // a long name takes two columns
        }
        if (!line.empty()) {
            if (column < 4) pad_to(line, 18 * column);
            line_end(line);
        }
        line_end("");
    }
    if (out.size() >= 2) out.resize(out.size() - 2);  // the file ends without a final break
    return out;
}

LibraryRequest parse_library_response(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char character : text) {
        if (character == '\r') continue;
        if (character == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        if (character == '\x1A') break;  // DOS end of file
        current.push_back(character);
    }
    if (!current.empty()) lines.push_back(current);

    LibraryRequest request;
    std::size_t index = 0;
    while (index < lines.size() && lines[index].empty()) ++index;
    if (index < lines.size()) request.library = lines[index++];
    if (index < lines.size() && (lines[index] == "y" || lines[index] == "Y")) ++index;
    std::string operations;
    for (; index < lines.size(); ++index) {
        std::string line = lines[index];
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty()) continue;
        const bool continued = line.back() == '&';
        if (continued) line.pop_back();
        operations += line;
        if (!continued) {
            ++index;
            break;
        }
    }
    for (; index < lines.size(); ++index) {
        std::string line = lines[index];
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty()) continue;
        if (!line.empty() && line.back() == ';') line.pop_back();
        request.listing = line;
        break;
    }
    std::string token;
    for (const char character : operations) {
        if (character == '+' || character == ' ' || character == '\t' || character == ';') {
            if (!token.empty()) request.additions.push_back(token);
            token.clear();
            continue;
        }
        token.push_back(character);
    }
    if (!token.empty()) request.additions.push_back(token);
    return request;
}

}  // namespace dosbase::devtools
