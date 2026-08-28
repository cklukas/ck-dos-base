// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/dbof.hpp"

#include "dosbase/devtools/binary.hpp"

#include <algorithm>

namespace dosbase::devtools {
namespace {

constexpr std::size_t kBytesPerLine = 8;
constexpr char kDigits[] = "0123456789ABCDEF";

std::string token_for(std::uint8_t value) {
    std::string text = "0";
    text.push_back(kDigits[value >> 4]);
    text.push_back(kDigits[value & 0x0F]);
    text.push_back('H');
    return text;
}

}  // namespace

std::string format_db_directive(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() / kBytesPerLine + 1) * 45);
    for (std::size_t offset = 0; offset < bytes.size(); offset += kBytesPerLine) {
        const std::size_t count = std::min(kBytesPerLine, bytes.size() - offset);
        out += "\tdb\t";
        for (std::size_t index = 0; index < count; ++index) {
            if (index != 0) out += ',';
            out += token_for(bytes[offset + index]);
        }
        out += "\r\n";
    }
    return out;
}

std::string dbof(std::span<const std::uint8_t> file, std::size_t offset, std::size_t length) {
    if (offset > file.size() || length > file.size() - offset) {
        throw FormatError(offset, "the requested slice runs past the end of the input");
    }
    return format_db_directive(file.subspan(offset, length));
}

}  // namespace dosbase::devtools
