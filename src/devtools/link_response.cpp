// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/link_response.hpp"

#include <cctype>

namespace dosbase::devtools {
namespace {

std::string_view rtrim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    return rtrim(text);
}

bool is_separator(char character) {
    return std::isspace(static_cast<unsigned char>(character)) || character == '+';
}

std::vector<std::string> tokenize_plus_or_space(std::string_view field) {
    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < field.size()) {
        while (index < field.size() && is_separator(field[index])) ++index;
        const std::size_t start = index;
        while (index < field.size() && !is_separator(field[index]) && field[index] != ',') ++index;
        if (index > start) tokens.push_back(std::string(trim(field.substr(start, index - start))));
    }
    return tokens;
}

// Every `/` in a .LNK file begins a switch token (DOS paths never contain
// one), running until the next separator. Blanking it to spaces in place
// removes it from the stream without disturbing later field splitting --
// the blanked span just reads as ordinary whitespace.
std::vector<std::string> extract_switches(std::string& text) {
    std::vector<std::string> switches;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '/') continue;
        std::size_t end = index + 1;
        while (end < text.size() && text[end] != ',' && text[end] != ';' && text[end] != '+' &&
               !std::isspace(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        switches.push_back(text.substr(index, end - index));
        for (std::size_t blank = index; blank < end; ++blank) text[blank] = ' ';
        index = end - 1;
    }
    return switches;
}

std::vector<std::string> split_physical_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line = end == std::string_view::npos ? text.substr(start)
                                                                : text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(std::string(line));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return lines;
}

// Merges a physical line ending in `+` with the next one -- LINK's own
// cross-line continuation for the object and library fields -- leaving the
// `+` itself in place as the item separator it already is.
std::vector<std::string> merge_continued_lines(const std::vector<std::string>& lines) {
    std::vector<std::string> logical;
    std::string pending;
    for (const std::string& line : lines) {
        pending += line;
        if (rtrim(pending).size() > 0 && rtrim(pending).back() == '+') continue;
        if (!trim(pending).empty()) logical.push_back(pending);
        pending.clear();
    }
    if (!trim(pending).empty()) logical.push_back(pending);
    return logical;
}

}  // namespace

LinkResponse parse_link_response(std::string_view input) {
    std::string text(input);
    const std::size_t control_z = text.find('\x1a');
    if (control_z != std::string::npos) text.resize(control_z);

    LinkResponse response;
    response.switches = extract_switches(text);

    const std::vector<std::string> logical_lines = merge_continued_lines(split_physical_lines(text));
    std::string flat;
    for (const std::string& line : logical_lines) {
        const std::string_view trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (!flat.empty()) {
            const char last = rtrim(flat).back();
            if (last != ',' && last != ';') flat += ',';
        }
        flat += trimmed;
    }

    const std::size_t terminator = flat.find(';');
    const std::string_view body = terminator == std::string::npos ? std::string_view(flat)
                                                                    : std::string_view(flat).substr(0, terminator);

    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = body.find(',', start);
        fields.push_back(comma == std::string_view::npos ? body.substr(start) : body.substr(start, comma - start));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }

    if (fields.size() > 0) response.objects = tokenize_plus_or_space(fields[0]);
    if (fields.size() > 1) response.exe_name = std::string(trim(fields[1]));
    if (fields.size() > 2) response.map_name = std::string(trim(fields[2]));
    if (fields.size() > 3) response.libraries = tokenize_plus_or_space(fields[3]);
    return response;
}

std::string default_exe_name(const LinkResponse& response) {
    if (!response.exe_name.empty() || response.objects.empty()) return response.exe_name;
    std::string base = response.objects.front();
    const std::size_t dot = base.rfind('.');
    const std::size_t sep = base.find_last_of("\\/");
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
        base.resize(dot);
    }
    return base + ".EXE";
}

}  // namespace dosbase::devtools
