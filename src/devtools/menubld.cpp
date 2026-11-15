// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/menubld.hpp"

#include "dosbase/devtools/binary.hpp"

#include <cctype>
#include <charconv>
#include <unordered_map>

namespace dosbase::devtools {
namespace {

bool equals_ci(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view trim_left(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
    return sv;
}

std::size_t find_section_start(std::string_view text, std::string_view section_name) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        if (!line.empty() && std::isalpha(static_cast<unsigned char>(line.front()))) {
            std::size_t ws = line.find_first_of(" \t");
            std::string_view name = ws == std::string_view::npos ? line : line.substr(0, ws);
            if (equals_ci(name, section_name)) {
                return eol < text.size() ? eol + 1 : text.size();
            }
        }
        pos = eol + 1;
    }
    throw FormatError(0, "section '" + std::string(section_name) + "' not found in message file");
}

std::unordered_map<std::uint32_t, std::string> parse_section_messages(
    std::string_view text, std::size_t start) {
    std::unordered_map<std::uint32_t, std::string> messages;
    std::size_t pos = start;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pos = eol + 1;

        std::string_view trimmed = trim_left(line);
        if (trimmed.empty()) continue;

        // Stop if we hit the next utility section header.
        if (std::isalpha(static_cast<unsigned char>(trimmed.front()))) {
            break;
        }
        if (!std::isdigit(static_cast<unsigned char>(trimmed.front()))) {
            continue;
        }

        std::uint32_t id = 0;
        const auto parsed = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), id);
        if (parsed.ec != std::errc{}) continue;

        const std::size_t open_quote = trimmed.find('"');
        if (open_quote == std::string_view::npos) continue;
        const std::size_t close_quote = trimmed.find('"', open_quote + 1);
        if (close_quote == std::string_view::npos) continue;

        messages.emplace(id, std::string(trimmed.substr(open_quote + 1, close_quote - open_quote - 1)));
    }
    return messages;
}

}  // namespace

std::optional<std::size_t> find_idx_section_offset(
    std::string_view idx_text, std::string_view section_name) {
    std::size_t pos = 0;
    while (pos < idx_text.size()) {
        std::size_t eol = idx_text.find('\n', pos);
        if (eol == std::string_view::npos) eol = idx_text.size();
        std::string_view line = idx_text.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pos = eol + 1;

        line = trim_left(line);
        if (line.empty()) continue;
        const std::size_t ws = line.find_first_of(" \t");
        if (ws == std::string_view::npos) continue;
        const std::string_view name = line.substr(0, ws);
        if (!equals_ci(name, section_name)) continue;

        const std::string_view rest = trim_left(line.substr(ws));
        std::size_t offset = 0;
        const auto parsed = std::from_chars(rest.data(), rest.data() + rest.size(), offset, 16);
        if (parsed.ec == std::errc{}) {
            return offset;
        }
    }
    return std::nullopt;
}

std::vector<std::uint8_t> build_menu_source(
    std::span<const std::uint8_t> template_bytes,
    std::span<const std::uint8_t> country_messages,
    std::string_view section_name,
    std::optional<std::size_t> section_offset) {
    const std::string_view msg_text(
        reinterpret_cast<const char*>(country_messages.data()), country_messages.size());
    std::size_t start = section_offset.has_value()
                            ? *section_offset
                            : find_section_start(msg_text, section_name);
    if (start > msg_text.size()) {
        throw FormatError(start, "section offset past end of message file");
    }
    // IDX offsets point to the section header line itself (`FDISK <off> <cnt>`);
    // advance past it so parse_section_messages doesn't stop on the header.
    if (section_offset.has_value() && start < msg_text.size() &&
        std::isalpha(static_cast<unsigned char>(msg_text[start]))) {
        const std::size_t eol = msg_text.find('\n', start);
        start = eol == std::string_view::npos ? msg_text.size() : eol + 1;
    }

    const auto messages = parse_section_messages(msg_text, start);

    std::vector<std::uint8_t> out;
    out.reserve(template_bytes.size());

    std::size_t i = 0;
    while (i < template_bytes.size()) {
        const std::uint8_t byte = template_bytes[i];
        if (byte != 0xE8) {
            out.push_back(byte);
            ++i;
            continue;
        }

        std::size_t j = i + 1;
        while (j < template_bytes.size() &&
               std::isdigit(static_cast<unsigned char>(template_bytes[j]))) {
            ++j;
        }
        if (j == i + 1) {
            throw FormatError(i, "expected message ID digits after 0xE8 marker");
        }

        std::uint32_t id = 0;
        const char* begin = reinterpret_cast<const char*>(template_bytes.data() + i + 1);
        const char* end = reinterpret_cast<const char*>(template_bytes.data() + j);
        const auto parsed = std::from_chars(begin, end, id);
        if (parsed.ec != std::errc{}) {
            throw FormatError(i, "invalid message ID");
        }

        const auto it = messages.find(id);
        if (it == messages.end()) {
            throw FormatError(i, "message ID " + std::to_string(id) + " not found in section '" +
                                     std::string(section_name) + "'");
        }
        out.insert(out.end(), it->second.begin(), it->second.end());
        i = j;
    }

    return out;
}

}  // namespace dosbase::devtools
