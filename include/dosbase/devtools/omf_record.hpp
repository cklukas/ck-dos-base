// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dosbase::devtools {

// Intel OMF-86 record types the pinned repair tree exercises. Anything else
// dumps as raw bytes rather than being guessed at.
enum class OmfType : std::uint8_t {
    theadr = 0x80,
    lheadr = 0x82,
    coment = 0x88,
    modend = 0x8A,
    extdef = 0x8C,
    typdef = 0x8E,
    pubdef = 0x90,
    linnum = 0x94,
    lnames = 0x96,
    segdef = 0x98,
    grpdef = 0x9A,
    fixupp = 0x9C,
    ledata = 0xA0,
    lidata = 0xA2,
    comdef = 0xB0,
    bakpat = 0xB2,
    lextdef = 0xB4,
    lpubdef = 0xB6,
    lcomdef = 0xB8,
};

[[nodiscard]] std::string omf_type_name(std::uint8_t type);

struct OmfRecord {
    std::uint8_t type{};
    std::uint16_t declared_length{};  // as stored: body plus checksum byte
    std::vector<std::uint8_t> body;   // declared_length - 1 bytes
    std::uint8_t checksum{};
    std::size_t offset{};             // absolute offset of the record header

    [[nodiscard]] std::size_t total_bytes() const noexcept { return declared_length + 2U + 1U; }
    // MASM and LINK write either a real modulo-256 checksum or a zero byte
    // meaning "unchecked"; both are legal and the difference is diagnostic.
    [[nodiscard]] bool checksum_valid() const noexcept;
    friend bool operator==(const OmfRecord&, const OmfRecord&) = default;
};

// Names accumulated while walking a module, so a dump can print
// "segment 1 CODE" instead of an index the reader has to resolve by hand.
struct OmfNameTable {
    std::vector<std::string> names;    // LNAMES order, 1-based when referenced
    std::vector<std::string> segments; // SEGDEF order, 1-based when referenced
    std::vector<std::string> groups;   // GRPDEF order, 1-based when referenced
    std::vector<std::string> externals;// EXTDEF/COMDEF order, 1-based
    void clear();
    [[nodiscard]] std::string name(std::uint16_t index) const;
    [[nodiscard]] std::string segment(std::uint16_t index) const;
    [[nodiscard]] std::string group(std::uint16_t index) const;
    [[nodiscard]] std::string external(std::uint16_t index) const;
};

// Parses records until the input is exhausted or, when `stop_after_modend`,
// the module's MODEND record has been read. Library members end at MODEND.
[[nodiscard]] std::vector<OmfRecord> parse_omf_records(std::span<const std::uint8_t> bytes,
                                                       std::size_t base_offset,
                                                       bool stop_after_modend,
                                                       std::size_t* consumed = nullptr);
[[nodiscard]] std::vector<std::uint8_t> serialize_omf_records(const std::vector<OmfRecord>& records);

// Appends the canonical dump of one record, updating `table` with any names
// it defines. `indent` prefixes every emitted line; `data` includes LEDATA
// and LIDATA payload bytes as hex.
void dump_omf_record(const OmfRecord& record, OmfNameTable& table, const std::string& indent,
                     bool data, std::string& out);

}  // namespace dosbase::devtools
