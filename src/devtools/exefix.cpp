// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/exefix.hpp"

#include "dosbase/devtools/mz_image.hpp"

#include <algorithm>
#include <cstdint>

namespace dosbase::devtools {
namespace {

constexpr std::uint16_t kSignatureMz = 0x5A4D;

void write_word(std::vector<std::uint8_t>& out, std::size_t at, std::uint16_t value) {
    out[at] = static_cast<std::uint8_t>(value & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

}  // namespace

ExefixResult exefix(std::span<const std::uint8_t> exe_bytes, const ExefixOptions& options) {
    const MzImage image = MzImage::parse(exe_bytes);
    MzFileHeader header = image.header();

    header.signature = kSignatureMz;
    if (options.max_alloc.has_value()) {
        header.maximum_extra_paragraphs = *options.max_alloc;
    }
    if (options.min_alloc.has_value()) {
        header.minimum_extra_paragraphs = *options.min_alloc;
    }

    const std::size_t total_size = exe_bytes.size();
    header.bytes_in_last_page = static_cast<std::uint16_t>(total_size % 512);
    header.pages_in_file = static_cast<std::uint16_t>((total_size + 511) / 512);

    const std::int64_t header_size = static_cast<std::int64_t>(header.header_paragraphs) * 16;
    const std::int64_t prog_size = static_cast<std::int64_t>(total_size) - header_size;
    const std::int64_t stack_top =
        (static_cast<std::int64_t>(header.initial_ss) << 4) +
        static_cast<std::int64_t>(header.initial_sp);
    const std::int64_t diff = stack_top - prog_size;

    ExefixResult result;
    if (diff > 0) {
        const std::uint16_t min_needed =
            static_cast<std::uint16_t>(std::min<std::int64_t>((diff + 15) >> 4, 0xFFFF));
        if (header.minimum_extra_paragraphs < min_needed) {
            header.minimum_extra_paragraphs = min_needed;
            result.min_clamped_to_stack = true;
        }
    }

    if (header.minimum_extra_paragraphs > header.maximum_extra_paragraphs) {
        header.maximum_extra_paragraphs = header.minimum_extra_paragraphs;
        result.max_clamped_to_min = true;
    }

    result.bytes.assign(exe_bytes.begin(), exe_bytes.end());
    write_word(result.bytes, 0, header.signature);
    write_word(result.bytes, 2, header.bytes_in_last_page);
    write_word(result.bytes, 4, header.pages_in_file);
    write_word(result.bytes, 10, header.minimum_extra_paragraphs);
    write_word(result.bytes, 12, header.maximum_extra_paragraphs);
    return result;
}

}  // namespace dosbase::devtools
