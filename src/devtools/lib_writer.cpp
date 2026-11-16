// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/lib_writer.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/core/lib_core.h"

#include <algorithm>

namespace dosbase::devtools {
namespace {

std::uint8_t fold_byte(char character) {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(character) | 0x20U);
}

}  // namespace

LibraryBuild build_library(const std::vector<LibraryInput>& members, std::size_t page_size) {
    std::vector<ck_lib_member_in> c_members(members.size());
    for (std::size_t i = 0; i < members.size(); ++i) {
        c_members[i].module_name = members[i].module_name.c_str();
        c_members[i].object = members[i].object.data();
        c_members[i].object_size = static_cast<ck_u32>(members[i].object.size());
    }
    ck_lib_build_out c_out{};
    ck_error err{};
    if (ck_lib_build(c_members.data(), c_members.size(), page_size, &c_out, &err) != 0) {
        throw FormatError(err.offset, err.message);
    }
    LibraryBuild build;
    build.bytes.assign(c_out.bytes, c_out.bytes + c_out.size);
    build.pages.assign(c_out.pages, c_out.pages + c_out.member_count);
    build.sizes.assign(c_out.sizes, c_out.sizes + c_out.member_count);
    build.symbols.reserve(c_out.symbol_count);
    for (std::size_t i = 0; i < c_out.symbol_count; ++i) {
        build.symbols.push_back({c_out.symbols[i].name, c_out.symbols[i].page});
    }
    build.publics.resize(c_out.member_count);
    for (std::size_t i = 0; i < c_out.member_count; ++i) {
        for (std::size_t j = 0; j < c_out.public_counts[i]; ++j) {
            build.publics[i].push_back(c_out.publics[i][j]);
        }
    }
    ck_lib_build_out_free(&c_out);
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
    ck_lib_request c_req{};
    ck_error err{};
    if (ck_lib_parse_response(text.data(), static_cast<ck_u32>(text.size()), &c_req, &err) != 0) {
        throw FormatError(err.offset, err.message);
    }
    LibraryRequest request;
    if (c_req.library) request.library = c_req.library;
    if (c_req.listing) request.listing = c_req.listing;
    for (std::size_t i = 0; i < c_req.addition_count; ++i) {
        request.additions.emplace_back(c_req.additions[i]);
    }
    ck_lib_request_free(&c_req);
    return request;
}

}  // namespace dosbase::devtools
