// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/mz_image.hpp"

#include "dosbase/devtools/binary.hpp"

#include <algorithm>

namespace dosbase::devtools {
namespace {

constexpr std::uint16_t kSignatureMz = 0x5A4D;  // "MZ"
constexpr std::uint16_t kSignatureZm = 0x4D5A;  // "ZM"
constexpr std::size_t kFixedHeaderBytes = 28;
constexpr std::uint16_t kExepackSignature = 0x4252;  // "RB"

void append_word(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

bool all_zero(std::span<const std::uint8_t> bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t value) { return value == 0; });
}

std::optional<ExepackHeader> detect_exepack(const MzFileHeader& header,
                                            std::span<const std::uint8_t> image,
                                            std::size_t header_bytes) {
    const std::size_t stub = static_cast<std::size_t>(header.initial_cs) * 16;
    const std::size_t length = header.initial_ip;
    if (length < 4 || (length % 2) != 0) return std::nullopt;
    if (stub > image.size() || length > image.size() - stub) return std::nullopt;
    const auto words = [&](std::size_t index) {
        const std::size_t at = stub + index * 2;
        return static_cast<std::uint16_t>(image[at] | (image[at + 1] << 8));
    };
    const std::size_t count = length / 2;
    if (words(count - 1) != kExepackSignature) return std::nullopt;
    if (count < 8) return std::nullopt;
    ExepackHeader stub_header;
    stub_header.image_offset = static_cast<std::uint32_t>(header_bytes + stub);
    stub_header.real_ip = words(0);
    stub_header.real_cs = words(1);
    stub_header.memory_start = words(2);
    stub_header.exepack_size = words(3);
    stub_header.real_sp = words(4);
    stub_header.real_ss = words(5);
    stub_header.destination_length = words(6);
    if (count >= 10) stub_header.skip_length = words(7);
    stub_header.header_bytes = length;
    return stub_header;
}

}  // namespace

MzImage MzImage::parse(std::span<const std::uint8_t> bytes) {
    ByteCursor cursor(bytes);
    MzImage result;
    MzFileHeader& header = result.header_;
    header.signature = cursor.word();
    if (header.signature != kSignatureMz && header.signature != kSignatureZm) {
        throw FormatError(0, "not an MZ executable");
    }
    header.bytes_in_last_page = cursor.word();
    header.pages_in_file = cursor.word();
    header.relocation_count = cursor.word();
    header.header_paragraphs = cursor.word();
    header.minimum_extra_paragraphs = cursor.word();
    header.maximum_extra_paragraphs = cursor.word();
    header.initial_ss = cursor.word();
    header.initial_sp = cursor.word();
    header.checksum = cursor.word();
    header.initial_ip = cursor.word();
    header.initial_cs = cursor.word();
    header.relocation_table_offset = cursor.word();
    header.overlay_number = cursor.word();

    result.header_bytes_ = static_cast<std::size_t>(header.header_paragraphs) * 16;
    if (result.header_bytes_ < kFixedHeaderBytes) {
        throw FormatError(8, "header_paragraphs does not cover the fixed header");
    }
    if (result.header_bytes_ > bytes.size()) {
        throw FormatError(8, "header runs past the end of the file");
    }

    const std::size_t table = header.relocation_table_offset;
    const std::size_t table_bytes = static_cast<std::size_t>(header.relocation_count) * 4;
    if (header.relocation_count != 0) {
        if (table < kFixedHeaderBytes) {
            throw FormatError(24, "relocation table overlaps the fixed header");
        }
        if (table > result.header_bytes_ || table_bytes > result.header_bytes_ - table) {
            throw FormatError(24, "relocation table runs past the header");
        }
        const auto gap = cursor.take(table - kFixedHeaderBytes);
        result.relocation_gap_.assign(gap.begin(), gap.end());
        result.relocations_.reserve(header.relocation_count);
        for (std::uint16_t index = 0; index < header.relocation_count; ++index) {
            MzRelocation relocation;
            relocation.offset = cursor.word();
            relocation.segment = cursor.word();
            result.relocations_.push_back(relocation);
        }
    }
    const auto slack = cursor.take(result.header_bytes_ - cursor.position());
    result.header_slack_.assign(slack.begin(), slack.end());

    std::size_t declared = 0;
    if (header.pages_in_file != 0) {
        declared = (static_cast<std::size_t>(header.pages_in_file) - 1) * 512;
        declared += header.bytes_in_last_page != 0 ? header.bytes_in_last_page : 512;
    }
    if (declared < result.header_bytes_) {
        throw FormatError(2, "declared file size is smaller than the header");
    }
    std::size_t image_length = declared - result.header_bytes_;
    if (image_length > bytes.size() - result.header_bytes_) {
        throw FormatError(2, "declared file size runs past the end of the file");
    }
    const auto image = cursor.take(image_length);
    result.image_.assign(image.begin(), image.end());
    const auto trailing = cursor.take(cursor.remaining());
    result.trailing_.assign(trailing.begin(), trailing.end());
    result.exepack_ = detect_exepack(header, result.image_, result.header_bytes_);
    return result;
}

std::vector<std::uint8_t> MzImage::serialize() const {
    std::vector<std::uint8_t> out;
    out.reserve(header_bytes_ + image_.size() + trailing_.size());
    append_word(out, header_.signature);
    append_word(out, header_.bytes_in_last_page);
    append_word(out, header_.pages_in_file);
    append_word(out, header_.relocation_count);
    append_word(out, header_.header_paragraphs);
    append_word(out, header_.minimum_extra_paragraphs);
    append_word(out, header_.maximum_extra_paragraphs);
    append_word(out, header_.initial_ss);
    append_word(out, header_.initial_sp);
    append_word(out, header_.checksum);
    append_word(out, header_.initial_ip);
    append_word(out, header_.initial_cs);
    append_word(out, header_.relocation_table_offset);
    append_word(out, header_.overlay_number);
    out.insert(out.end(), relocation_gap_.begin(), relocation_gap_.end());
    for (const MzRelocation& relocation : relocations_) {
        append_word(out, relocation.offset);
        append_word(out, relocation.segment);
    }
    out.insert(out.end(), header_slack_.begin(), header_slack_.end());
    out.insert(out.end(), image_.begin(), image_.end());
    out.insert(out.end(), trailing_.begin(), trailing_.end());
    return out;
}

void dump_mz(const MzImage& image, const std::string& label, bool data, std::string& out) {
    const MzFileHeader& header = image.header();
    out += "mz " + label + "\n";
    out += "  signature " + hex16(header.signature) + "\n";
    out += "  bytes_in_last_page " + std::to_string(header.bytes_in_last_page) + "\n";
    out += "  pages_in_file " + std::to_string(header.pages_in_file) + "\n";
    out += "  relocation_count " + std::to_string(header.relocation_count) + "\n";
    out += "  header_paragraphs " + std::to_string(header.header_paragraphs) + "\n";
    out += "  minimum_extra_paragraphs " + hex16(header.minimum_extra_paragraphs) + "\n";
    out += "  maximum_extra_paragraphs " + hex16(header.maximum_extra_paragraphs) + "\n";
    out += "  initial_ss_sp " + hex16(header.initial_ss) + ":" + hex16(header.initial_sp) + "\n";
    out += "  checksum " + hex16(header.checksum) + "\n";
    out += "  initial_cs_ip " + hex16(header.initial_cs) + ":" + hex16(header.initial_ip) + "\n";
    out += "  relocation_table_offset " + hex16(header.relocation_table_offset) + "\n";
    out += "  overlay_number " + std::to_string(header.overlay_number) + "\n";
    out += "  header_bytes " + std::to_string(image.header_bytes()) + "\n";
    out += "  relocation_gap " + std::to_string(image.relocation_gap().size()) +
           (all_zero(image.relocation_gap()) ? " zero\n" : " nonzero\n");
    out += "  header_slack " + std::to_string(image.header_slack().size()) +
           (all_zero(image.header_slack()) ? " zero\n" : " nonzero\n");
    out += "  image_bytes " + std::to_string(image.image_bytes()) + " crc32 " +
           hex32(crc32(image.image())) + "\n";
    out += "  trailing_bytes " + std::to_string(image.trailing_bytes());
    if (image.trailing_bytes() != 0) out += " crc32 " + hex32(crc32(image.trailing()));
    out += "\n";
    if (image.exepack()) {
        const ExepackHeader& stub = *image.exepack();
        out += "  exepack header_bytes " + std::to_string(stub.header_bytes) + " file_offset " +
               hex32(stub.image_offset) + "\n";
        out += "    real_cs_ip " + hex16(stub.real_cs) + ":" + hex16(stub.real_ip) + "\n";
        out += "    real_ss_sp " + hex16(stub.real_ss) + ":" + hex16(stub.real_sp) + "\n";
        out += "    memory_start " + hex16(stub.memory_start) + "\n";
        out += "    exepack_size " + hex16(stub.exepack_size) + "\n";
        out += "    destination_length " + hex16(stub.destination_length) + "\n";
        if (stub.skip_length) out += "    skip_length " + hex16(*stub.skip_length) + "\n";
    }
    std::size_t index = 0;
    for (const MzRelocation& relocation : image.relocations()) {
        out += "  relocation " + std::to_string(index) + " " + hex16(relocation.segment) + ":" +
               hex16(relocation.offset) + "\n";
        ++index;
    }
    if (data) {
        out += "  image_data " + hex_bytes(image.image()) + "\n";
        if (image.trailing_bytes() != 0) {
            out += "  trailing_data " + hex_bytes(image.trailing()) + "\n";
        }
    }
}

}  // namespace dosbase::devtools
