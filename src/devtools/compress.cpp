// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/compress.hpp"

#include "dosbase/devtools/binary.hpp"

namespace dosbase::devtools {
namespace {

void emit_escaped_byte(std::vector<std::uint8_t>& out, std::uint8_t value) {
    if (value == 0xFF) {
        out.push_back(0xFF);
        out.push_back(0xFF);
    } else if (value == 0x0D) {
        out.push_back(0xFF);
        out.push_back(0x01);
    } else if (value == 0x1A) {
        out.push_back(0xFF);
        out.push_back(0x02);
    } else {
        out.push_back(value);
    }
}

}  // namespace

std::vector<std::uint8_t> compress_rle(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> out;
    out.reserve(input.size());

    std::size_t index = 0;
    while (index < input.size()) {
        const std::uint8_t value = input[index];
        std::size_t run = 1;
        while (index + run < input.size() && input[index + run] == value && run < 255) {
            ++run;
        }

        // Never emit run lengths 2, 13 (0x0D), 26 (0x1A), or 255 (0xFF).
        if (run == 2 || run == 0x0D || run == 0x1A || run == 0xFF) {
            --run;
        }

        if (run == 1) {
            emit_escaped_byte(out, value);
        } else {
            out.push_back(0xFF);
            out.push_back(static_cast<std::uint8_t>(run));
            emit_escaped_byte(out, value);
        }
        index += run;
    }

    return out;
}

std::vector<std::uint8_t> decompress_rle(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> out;
    std::size_t index = 0;

    auto read_escaped = [&](std::size_t at) -> std::pair<std::uint8_t, std::size_t> {
        if (at >= input.size()) {
            throw FormatError(at, "truncated RLE stream");
        }
        if (input[at] != 0xFF) {
            return {input[at], 1};
        }
        if (at + 1 >= input.size()) {
            throw FormatError(at, "truncated RLE escape sequence");
        }
        const std::uint8_t code = input[at + 1];
        if (code == 0xFF) return {0xFF, 2};
        if (code == 0x01) return {0x0D, 2};
        if (code == 0x02) return {0x1A, 2};
        throw FormatError(at, "invalid RLE escaped byte code");
    };

    while (index < input.size()) {
        const std::uint8_t byte = input[index];
        if (byte != 0xFF) {
            out.push_back(byte);
            ++index;
            continue;
        }
        if (index + 1 >= input.size()) {
            throw FormatError(index, "truncated RLE escape sequence");
        }
        const std::uint8_t tag = input[index + 1];
        if (tag == 0xFF) {
            out.push_back(0xFF);
            index += 2;
        } else if (tag == 0x01) {
            out.push_back(0x0D);
            index += 2;
        } else if (tag == 0x02) {
            out.push_back(0x1A);
            index += 2;
        } else if (tag >= 3 && tag != 0x0D && tag != 0x1A) {
            const auto [value, consumed] = read_escaped(index + 2);
            out.insert(out.end(), tag, value);
            index += 2 + consumed;
        } else {
            throw FormatError(index, "invalid RLE tag");
        }
    }

    return out;
}

}  // namespace dosbase::devtools
