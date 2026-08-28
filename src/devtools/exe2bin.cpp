// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/exe2bin.hpp"

#include "dosbase/devtools/binary.hpp"

namespace dosbase::devtools {
namespace {

constexpr std::uint16_t kComOrigin = 0x0100;

}  // namespace

Exe2BinResult exe2bin(const MzImage& image, std::optional<std::uint16_t> base_segment) {
    const MzFileHeader& header = image.header();
    if (header.relocation_count != 0 && !base_segment.has_value()) {
        throw FormatError(6, "the image needs fix-ups but no base segment was given");
    }
    if (header.relocation_count == 0 && base_segment.has_value()) {
        throw FormatError(6, "a base segment was given for an image with no fix-ups");
    }
    if (header.initial_ss != 0 || header.initial_sp != 0) {
        throw FormatError(14, "file cannot be converted: the image declares a stack");
    }
    if (header.initial_cs != 0) {
        throw FormatError(22, "file cannot be converted: the entry segment is not zero");
    }
    if (image.trailing_bytes() != 0) {
        throw FormatError(2, "the file carries data past its load image; the tree never does");
    }
    Exe2BinResult result;
    std::vector<std::uint8_t> load(image.image().begin(), image.image().end());
    if (base_segment.has_value()) {
        for (const MzRelocation& relocation : image.relocations()) {
            const std::size_t at =
                static_cast<std::size_t>(relocation.segment) * 16U + relocation.offset;
            if (at + 1 >= load.size()) {
                throw FormatError(at, "a fix-up points past the load image");
            }
            const auto word = static_cast<std::uint16_t>(load[at] | (load[at + 1] << 8));
            const auto fixed = static_cast<std::uint16_t>(word + *base_segment);
            load[at] = static_cast<std::uint8_t>(fixed & 0xFF);
            load[at + 1] = static_cast<std::uint8_t>(fixed >> 8);
        }
    }
    if (header.initial_ip == 0) {
        result.kind = Exe2BinKind::pure_binary;
        result.bytes.assign(load.begin(), load.end());
        return result;
    }
    if (header.initial_ip == kComOrigin) {
        if (load.size() < kComOrigin) {
            throw FormatError(2, "the load image is shorter than the 0x100 byte origin");
        }
        result.kind = Exe2BinKind::com_image;
        result.bytes.assign(load.begin() + kComOrigin, load.end());
        return result;
    }
    throw FormatError(20, "entry offset is neither 0000 nor 0100; the tree exercises no other");
}

}  // namespace dosbase::devtools
