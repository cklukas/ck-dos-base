// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dosbase::devtools {

struct MzRelocation {
    std::uint16_t offset{};
    std::uint16_t segment{};
    friend bool operator==(const MzRelocation&, const MzRelocation&) = default;
};

struct MzFileHeader {
    std::uint16_t signature{};
    std::uint16_t bytes_in_last_page{};
    std::uint16_t pages_in_file{};
    std::uint16_t relocation_count{};
    std::uint16_t header_paragraphs{};
    std::uint16_t minimum_extra_paragraphs{};
    std::uint16_t maximum_extra_paragraphs{};
    std::uint16_t initial_ss{};
    std::uint16_t initial_sp{};
    std::uint16_t checksum{};
    std::uint16_t initial_ip{};
    std::uint16_t initial_cs{};
    std::uint16_t relocation_table_offset{};
    std::uint16_t overlay_number{};
    friend bool operator==(const MzFileHeader&, const MzFileHeader&) = default;
};

// The EXEPACK stub header that sits at the packed entry segment. The 1988
// tools ship in both the 16-byte and the 18/20-byte shape, so the words are
// kept as read and only named when present.
struct ExepackHeader {
    std::uint32_t image_offset{};  // file offset of the stub header
    std::uint16_t real_ip{};
    std::uint16_t real_cs{};
    std::uint16_t memory_start{};
    std::uint16_t exepack_size{};
    std::uint16_t real_sp{};
    std::uint16_t real_ss{};
    std::uint16_t destination_length{};
    std::optional<std::uint16_t> skip_length{};
    std::size_t header_bytes{};
};

// A whole-file model of an MZ executable: every byte belongs to exactly one
// member, so serialize(parse(bytes)) == bytes for any file that parses.
class MzImage {
public:
    static MzImage parse(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;

    [[nodiscard]] const MzFileHeader& header() const noexcept { return header_; }
    [[nodiscard]] const std::vector<MzRelocation>& relocations() const noexcept { return relocations_; }
    [[nodiscard]] std::size_t header_bytes() const noexcept { return header_bytes_; }
    [[nodiscard]] std::size_t image_bytes() const noexcept { return image_.size(); }
    [[nodiscard]] std::size_t trailing_bytes() const noexcept { return trailing_.size(); }
    [[nodiscard]] std::span<const std::uint8_t> image() const noexcept { return image_; }
    [[nodiscard]] std::span<const std::uint8_t> trailing() const noexcept { return trailing_; }
    [[nodiscard]] const std::optional<ExepackHeader>& exepack() const noexcept { return exepack_; }
    // Bytes of the header the format does not account for: the gap before the
    // relocation table and the slack after it. LINK writes zeros; a
    // reimplementation that writes something else diverges here first.
    [[nodiscard]] std::span<const std::uint8_t> relocation_gap() const noexcept { return relocation_gap_; }
    [[nodiscard]] std::span<const std::uint8_t> header_slack() const noexcept { return header_slack_; }

private:
    MzFileHeader header_{};
    std::vector<MzRelocation> relocations_;
    std::vector<std::uint8_t> relocation_gap_;
    std::vector<std::uint8_t> header_slack_;
    std::vector<std::uint8_t> image_;
    std::vector<std::uint8_t> trailing_;
    std::optional<ExepackHeader> exepack_;
    std::size_t header_bytes_{};
};

// Deterministic line-oriented dump. `data` includes the load image bytes as
// hex; without it the image is summarized by length and CRC-32.
void dump_mz(const MzImage& image, const std::string& label, bool data, std::string& out);

}  // namespace dosbase::devtools
