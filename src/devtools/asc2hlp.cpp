// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/asc2hlp.hpp"

#include "dosbase/devtools/binary.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace dosbase::devtools {
namespace {

constexpr std::uint16_t kCasMagic = 0x1234;
constexpr std::uint16_t kCasObjectDirectoryCount = 20;
constexpr std::uint16_t kCasHelpObjectId = 0x00FE;
constexpr std::size_t kCasHeaderBytes = 4 + kCasObjectDirectoryCount * 8;  // 164 bytes

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

std::uint16_t parse_u16_after(std::string_view text, std::size_t pos, std::string_view label) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data() + pos, text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || value > 0xFFFF) {
        throw FormatError(pos, "invalid integer after " + std::string(label));
    }
    return static_cast<std::uint16_t>(value);
}

std::size_t find_required(std::string_view text, std::size_t from, std::string_view needle) {
    const std::size_t found = text.find(needle, from);
    if (found == std::string_view::npos) {
        throw FormatError(from, "Missing \"" + std::string(needle) + "\" Reserve Word");
    }
    return found;
}

}  // namespace

HelpDocument parse_help_source(std::span<const std::uint8_t> source) {
    const std::string_view text(reinterpret_cast<const char*>(source.data()), source.size());
    HelpDocument doc;

    std::size_t pos = find_required(text, 0, ".TOPICLEN.");
    doc.topic_len = parse_u16_after(text, pos + 10, ".TOPICLEN.");

    pos = find_required(text, pos + 10, ".MAXNUMHLP.");
    doc.max_num_help = parse_u16_after(text, pos + 11, ".MAXNUMHLP.");

    pos += 11;
    while (true) {
        const std::size_t id_pos = text.find(".HELPID.", pos);
        if (id_pos == std::string_view::npos) {
            break;
        }
        if (doc.entries.size() >= doc.max_num_help) {
            throw FormatError(id_pos, "help entry count exceeds .MAXNUMHLP.");
        }

        HelpEntry entry;
        entry.id = parse_u16_after(text, id_pos + 8, ".HELPID.");

        const std::size_t topic_pos = find_required(text, id_pos + 8, ".TOPICTEXT.");
        const std::size_t topic_start = topic_pos + 11;
        std::size_t topic_end = text.find_first_of("\r\n", topic_start);
        if (topic_end == std::string_view::npos) topic_end = text.size();
        entry.topic = std::string(text.substr(topic_start, topic_end - topic_start));

        const std::size_t help_pos = find_required(text, topic_end, ".HELPTEXT.");
        std::size_t help_start = help_pos + 10;
        if (help_start < text.size() && text[help_start] == ' ') {
            ++help_start;
        }
        const std::size_t help_end = find_required(text, help_start, ".HELPTEND.");
        entry.text = std::string(text.substr(help_start, help_end - help_start));

        doc.entries.push_back(std::move(entry));
        pos = help_end + 10;
    }

    if (doc.entries.empty()) {
        throw FormatError(0, "Missing \".HELPID.\" Reserve Word");
    }

    return doc;
}

std::vector<std::uint8_t> build_cas_help(const HelpDocument& doc) {
    if (doc.entries.size() > doc.max_num_help) {
        throw FormatError(0, "help entry count exceeds max_num_help");
    }

    const std::size_t index_table_bytes = static_cast<std::size_t>(doc.max_num_help) * 8;
    const std::size_t topics_bytes =
        static_cast<std::size_t>(doc.max_num_help) * doc.topic_len;
    const std::size_t texts_base = kCasHeaderBytes + 4 + index_table_bytes + topics_bytes;

    std::size_t total_text_bytes = 0;
    for (const auto& entry : doc.entries) {
        if (entry.text.size() > 0xFFFF) {
            throw FormatError(0, "help text exceeds 64 KiB");
        }
        total_text_bytes += entry.text.size();
    }

    std::vector<std::uint8_t> out;
    out.reserve(texts_base + total_text_bytes);

    // 1. CAS file header (164 bytes)
    append_u16(out, kCasMagic);
    append_u16(out, kCasObjectDirectoryCount);
    append_u16(out, kCasHelpObjectId);
    append_u32(out, static_cast<std::uint32_t>(kCasHeaderBytes));
    append_u16(out, doc.max_num_help);
    out.resize(kCasHeaderBytes, 0);

    // 2. Help object header (4 bytes)
    append_u16(out, static_cast<std::uint16_t>(doc.entries.size()));
    append_u16(out, doc.topic_len);

    // 3. Help index table (max_num_help * 8 bytes)
    std::uint32_t current_text_offset = static_cast<std::uint32_t>(texts_base);
    for (const auto& entry : doc.entries) {
        append_u16(out, entry.id);
        append_u32(out, current_text_offset);
        append_u16(out, static_cast<std::uint16_t>(entry.text.size()));
        current_text_offset += static_cast<std::uint32_t>(entry.text.size());
    }
    out.resize(kCasHeaderBytes + 4 + index_table_bytes, 0);

    // 4. Space-padded topic strings (max_num_help * topic_len bytes)
    for (std::size_t i = 0; i < doc.max_num_help; ++i) {
        const std::size_t start = out.size();
        out.resize(start + doc.topic_len, static_cast<std::uint8_t>(' '));
        if (i < doc.entries.size()) {
            const std::size_t copy_len =
                std::min<std::size_t>(doc.entries[i].topic.size(), doc.topic_len);
            std::copy_n(doc.entries[i].topic.begin(), copy_len, out.begin() + start);
        }
    }

    // 5. Concatenated help text payloads
    for (const auto& entry : doc.entries) {
        out.insert(out.end(), entry.text.begin(), entry.text.end());
    }

    return out;
}

std::vector<std::uint8_t> asc2hlp(std::span<const std::uint8_t> source) {
    return build_cas_help(parse_help_source(source));
}

}  // namespace dosbase::devtools
