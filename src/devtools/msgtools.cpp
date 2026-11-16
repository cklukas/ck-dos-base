// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/msgtools.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/core/msgtools_core.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dosbase::devtools {
namespace {

constexpr std::string_view kCrlf = "\r\n";

std::vector<std::string_view> split_lines(std::string_view raw) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= raw.size()) {
        std::size_t end = raw.find('\n', start);
        if (end == std::string_view::npos) {
            if (start < raw.size()) lines.push_back(raw.substr(start));
            break;
        }
        std::size_t stop = end;
        if (stop > start && raw[stop - 1] == '\r') --stop;
        lines.push_back(raw.substr(start, stop - start));
        start = end + 1;
    }
    return lines;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_lower_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f'); }

// `NAME     oooo cccc` -- name of A-Z0-9 starting with a letter padded into
// eight columns, offset in lowercase hex, count in decimal.
bool parse_section_header(std::string_view line, std::string* name) {
    std::size_t i = 0;
    if (i >= line.size() || !is_upper(line[i])) return false;
    while (i < line.size() && (is_upper(line[i]) || is_digit(line[i]))) ++i;
    const std::size_t name_end = i;
    if (i == 0 || i >= line.size() || line[i] != ' ') return false;
    while (i < line.size() && line[i] == ' ') ++i;
    if (line.size() - i != 9) return false;
    for (std::size_t k = 0; k < 4; ++k) {
        if (!is_lower_hex(line[i + k])) return false;
    }
    if (line[i + 4] != ' ') return false;
    for (std::size_t k = 5; k < 9; ++k) {
        if (!is_digit(line[i + k])) return false;
    }
    if (name != nullptr) *name = std::string(line.substr(0, name_end));
    return true;
}

// `nnnn U ffff <expression>`
bool parse_message_line(std::string_view line, MsgMessage* message) {
    if (line.size() < 11) return false;
    for (std::size_t k = 0; k < 4; ++k) {
        if (!is_digit(line[k])) return false;
    }
    if (line[4] != ' ' || line[6] != ' ') return false;
    for (std::size_t k = 7; k < 11; ++k) {
        if (!is_digit(line[k])) return false;
    }
    if (line.size() > 11 && line[11] != ' ') return false;
    if (message != nullptr) {
        message->number = 0;
        std::from_chars(line.data(), line.data() + 4, message->number);
        message->usage = line[5];
        message->flags = std::string(line.substr(7, 4));
        message->lines = {std::string(line.size() > 12 ? line.substr(12) : std::string_view{})};
    }
    return true;
}

std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return result;
}

std::string join_crlf(const std::vector<std::string>& lines) {
    std::string out;
    std::size_t size = 0;
    for (const std::string& line : lines) size += line.size() + 2;
    out.reserve(size);
    for (const std::string& line : lines) {
        out += line;
        out += kCrlf;
    }
    return out;
}

}  // namespace

const MsgMessage* MsgDatabase::find(std::string_view component, int number) const {
    const std::string wanted = upper(component);
    for (const MsgSection& section : sections) {
        if (section.name != wanted) continue;
        for (const MsgMessage& message : section.messages) {
            if (message.number == number) return &message;
        }
        return nullptr;
    }
    return nullptr;
}

MsgDatabase parse_msg(std::string_view raw) {
    const std::vector<std::string_view> lines = split_lines(raw);
    if (lines.empty()) throw FormatError(0, "empty message file");
    MsgDatabase database;
    database.level = std::string(lines[0]);
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (line.empty()) continue;
        std::string name;
        if (parse_section_header(line, &name)) {
            database.sections.push_back({name, {}});
            continue;
        }
        MsgMessage message;
        if (parse_message_line(line, &message)) {
            if (database.sections.empty()) {
                throw FormatError(i, "message line before the first section header");
            }
            database.sections.back().messages.push_back(std::move(message));
            continue;
        }
        if (!line.empty() && line[0] == '\t') {
            if (database.sections.empty() || database.sections.back().messages.empty()) {
                throw FormatError(i, "continuation line with no message to continue");
            }
            database.sections.back().messages.back().lines.emplace_back(line);
            continue;
        }
        throw FormatError(i, "unrecognized message-file line");
    }
    return database;
}

BuildIdxResult buildidx(std::string_view msg_raw) {
    const MsgDatabase database = parse_msg(msg_raw);
    unsigned level = 0;
    std::from_chars(database.level.data(), database.level.data() + database.level.size(),
                    level, 16);
    char level_text[8];
    std::snprintf(level_text, sizeof level_text, "%04x", level + 1);

    std::vector<std::string_view> lines = split_lines(msg_raw);
    std::vector<std::string> rewritten;
    std::vector<std::string> index_lines;
    rewritten.reserve(lines.size());
    index_lines.push_back(level_text);
    std::size_t position = 0;
    std::size_t next_section = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line;
        std::string name;
        if (i == 0) {
            line = level_text;
        } else if (parse_section_header(lines[i], &name)) {
            const MsgSection& section = database.sections.at(next_section++);
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%-8s %04zx %04zu", section.name.c_str(),
                          position, section.messages.size());
            line = buffer;
            index_lines.push_back(line);
        } else {
            line = std::string(lines[i]);
        }
        position += line.size() + 2;
        rewritten.push_back(std::move(line));
    }
    return {join_crlf(rewritten), join_crlf(index_lines)};
}

namespace {

int cpp_msg_lookup(void* ctx, const char* component, int number, ck_msg_lines* out_lines) {
    const auto* db = static_cast<const MsgDatabase*>(ctx);
    const MsgMessage* m = db->find(component ? component : "", number);
    if (!m) return 1;
    out_lines->count = m->lines.size();
    out_lines->lines = static_cast<char**>(std::calloc(m->lines.size(), sizeof(char*)));
    if (!out_lines->lines && m->lines.size() > 0) return -1;
    for (std::size_t i = 0; i < m->lines.size(); ++i) {
        const std::string& s = m->lines[i];
        out_lines->lines[i] = static_cast<char*>(std::malloc(s.size() + 1));
        if (!out_lines->lines[i]) {
            ck_msg_lines_free(out_lines);
            return -1;
        }
        std::memcpy(out_lines->lines[i], s.data(), s.size());
        out_lines->lines[i][s.size()] = '\0';
    }
    return 0;
}

int cpp_msg_emit(void* ctx, const char* tag, const char* data, ck_u32 size) {
    auto* vec = static_cast<std::vector<std::pair<std::string, std::string>>*>(ctx);
    vec->emplace_back(tag ? tag : "", std::string(data ? data : "", size));
    return 0;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> nosrvbld(std::string_view skl_raw,
                                                          std::string_view base_name,
                                                          const MsgDatabase& msg) {
    std::vector<std::pair<std::string, std::string>> rendered;
    const std::string base_str(base_name);
    ck_error err{};
    if (ck_nosrvbld_core(skl_raw.data(),
                         static_cast<ck_u32>(skl_raw.size()),
                         base_str.c_str(),
                         cpp_msg_lookup,
                         const_cast<MsgDatabase*>(&msg),
                         cpp_msg_emit,
                         &rendered,
                         &err) != 0) {
        throw FormatError(err.offset, err.message);
    }
    return rendered;
}

BuildMsgResult buildmsg(std::string_view skl_raw, const MsgDatabase& msg) {
    BuildMsgResult result;
    char util_buf[64]{};
    ck_error err{};
    if (ck_buildmsg_core(skl_raw.data(),
                         static_cast<ck_u32>(skl_raw.size()),
                         cpp_msg_lookup,
                         const_cast<MsgDatabase*>(&msg),
                         cpp_msg_emit,
                         &result.files,
                         util_buf,
                         sizeof(util_buf),
                         &err) != 0) {
        throw FormatError(err.offset, err.message);
    }
    result.util = util_buf;
    return result;
}

}  // namespace dosbase::devtools
