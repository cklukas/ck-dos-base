// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/binary.hpp"

#include <array>
#include <fstream>
#include <ios>

namespace dosbase::devtools {
namespace {

constexpr char kDigits[] = "0123456789abcdef";

std::string hex_value(std::uint64_t value, int digits) {
    std::string text(static_cast<std::size_t>(digits), '0');
    for (int index = digits - 1; index >= 0; --index) {
        text[static_cast<std::size_t>(index)] = kDigits[value & 0xF];
        value >>= 4;
    }
    return text;
}

}  // namespace

std::string FormatError::hex(std::size_t value) {
    std::string text;
    do {
        text.insert(text.begin(), kDigits[value & 0xF]);
        value >>= 4;
    } while (value != 0);
    return text;
}

void ByteCursor::require(std::size_t count) const {
    if (count > remaining()) {
        throw FormatError(absolute(), "record runs past the end of the input");
    }
}

std::uint8_t ByteCursor::byte() {
    require(1);
    return bytes_[position_++];
}

std::uint16_t ByteCursor::word() {
    require(2);
    const auto low = static_cast<std::uint16_t>(bytes_[position_]);
    const auto high = static_cast<std::uint16_t>(bytes_[position_ + 1]);
    position_ += 2;
    return static_cast<std::uint16_t>(low | (high << 8));
}

std::uint32_t ByteCursor::dword() {
    const std::uint32_t low = word();
    const std::uint32_t high = word();
    return low | (high << 16);
}

std::span<const std::uint8_t> ByteCursor::take(std::size_t count) {
    require(count);
    const auto slice = bytes_.subspan(position_, count);
    position_ += count;
    return slice;
}

std::string ByteCursor::name() {
    const std::size_t length = byte();
    const auto slice = take(length);
    return std::string(reinterpret_cast<const char*>(slice.data()), slice.size());
}

std::uint16_t ByteCursor::index() {
    const std::uint16_t first = byte();
    if ((first & 0x80U) == 0) return first;
    const std::uint16_t second = byte();
    return static_cast<std::uint16_t>(((first & 0x7FU) << 8) | second);
}

void ByteCursor::seek(std::size_t position) {
    if (position > bytes_.size()) {
        throw FormatError(base_ + position, "seek past the end of the input");
    }
    position_ = position;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw FormatError(0, "cannot open " + path);
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 0) throw FormatError(0, "cannot size " + path);
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!stream) throw FormatError(0, "cannot read " + path);
    }
    return bytes;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t index = 0; index < 256; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1U) != 0 ? 0xEDB88320U ^ (value >> 1) : value >> 1;
            }
            values[index] = value;
        }
        return values;
    }();
    std::uint32_t value = 0xFFFFFFFFU;
    for (const std::uint8_t item : bytes) {
        value = table[(value ^ item) & 0xFFU] ^ (value >> 8);
    }
    return value ^ 0xFFFFFFFFU;
}

std::string hex8(std::uint8_t value) { return hex_value(value, 2); }
std::string hex16(std::uint16_t value) { return hex_value(value, 4); }
std::string hex32(std::uint32_t value) { return hex_value(value, 8); }

std::string hex_bytes(std::span<const std::uint8_t> bytes) {
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::uint8_t item : bytes) {
        text.push_back(kDigits[item >> 4]);
        text.push_back(kDigits[item & 0xF]);
    }
    return text;
}

std::string quoted(const std::string& text) {
    std::string result = "\"";
    for (const char character : text) {
        const auto value = static_cast<std::uint8_t>(character);
        if (character == '"' || character == '\\') {
            result.push_back('\\');
            result.push_back(character);
        } else if (value < 0x20 || value >= 0x7F) {
            result += "\\x" + hex8(value);
        } else {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

}  // namespace dosbase::devtools
