// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/omf_record.hpp"

namespace {
using dosbase::devtools::FormatError;
using dosbase::devtools::OmfNameTable;
using dosbase::devtools::OmfRecord;
using dosbase::devtools::parse_omf_records;
using dosbase::devtools::serialize_omf_records;

// Emits records the way MASM does, including the modulo-256 checksum, so the
// fixtures exercise the same acceptance path as a real object file.
class ObjectBuilder {
public:
    void record(std::uint8_t type, const std::vector<std::uint8_t>& body) {
        const auto length = static_cast<std::uint16_t>(body.size() + 1U);
        bytes_.push_back(type);
        bytes_.push_back(static_cast<std::uint8_t>(length & 0xFFU));
        bytes_.push_back(static_cast<std::uint8_t>(length >> 8));
        bytes_.insert(bytes_.end(), body.begin(), body.end());
        std::uint32_t sum = type;
        sum += length & 0xFFU;
        sum += length >> 8;
        for (const std::uint8_t value : body) sum += value;
        bytes_.push_back(static_cast<std::uint8_t>((0x100U - (sum & 0xFFU)) & 0xFFU));
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

    static std::vector<std::uint8_t> name(const std::string& text) {
        std::vector<std::uint8_t> body{static_cast<std::uint8_t>(text.size())};
        body.insert(body.end(), text.begin(), text.end());
        return body;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

std::vector<std::uint8_t> concat(std::initializer_list<std::vector<std::uint8_t>> parts) {
    std::vector<std::uint8_t> result;
    for (const std::vector<std::uint8_t>& part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

// THEADR, LNAMES, SEGDEF, LEDATA, FIXUPP, MODEND: the record shapes every
// .ASM of the pinned tree produces.
std::vector<std::uint8_t> sample_object() {
    ObjectBuilder builder;
    builder.record(0x80, ObjectBuilder::name("TCMD1A.ASM"));
    builder.record(0x96, concat({ObjectBuilder::name(""), ObjectBuilder::name("CODE"),
                                 ObjectBuilder::name("CODE")}));
    builder.record(0x98, {0x48, 0x10, 0x00, 0x02, 0x03, 0x01});
    builder.record(0x8C, concat({ObjectBuilder::name("PRINTF"), {0x00}}));
    builder.record(0xA0, {0x01, 0x00, 0x00, 0xB4, 0x4C, 0xCD, 0x21});
    // Segment-relative offset16 fixup at 0003, frame from the target,
    // target external 1, no displacement.
    builder.record(0x9C, {0xC4, 0x03, 0x56, 0x01});
    builder.record(0x8A, {0x00});
    return builder.bytes();
}
}  // namespace

DOSBASE_TEST(omf_round_trips_a_record_stream) {
    const std::vector<std::uint8_t> bytes = sample_object();
    const std::vector<OmfRecord> records = parse_omf_records(bytes, 0, false);
    DOSBASE_TEST_EXPECT_EQ(records.size(), std::size_t{7});
    DOSBASE_TEST_EXPECT_EQ(records.front().type, std::uint8_t{0x80});
    DOSBASE_TEST_EXPECT_EQ(records.back().type, std::uint8_t{0x8A});
    for (const OmfRecord& record : records) {
        DOSBASE_TEST_EXPECT(record.checksum_valid());
    }
    DOSBASE_TEST_EXPECT(serialize_omf_records(records) == bytes);
}

DOSBASE_TEST(omf_dump_names_records_and_resolves_indices) {
    const std::vector<OmfRecord> records = parse_omf_records(sample_object(), 0, false);
    OmfNameTable table;
    std::string text;
    for (const OmfRecord& record : records) {
        dosbase::devtools::dump_omf_record(record, table, "  ", false, text);
    }
    DOSBASE_TEST_EXPECT(text.find("80 THEADR") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("name \"TCMD1A.ASM\"") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("align word combine public use16") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("name 2:\"CODE\" class 3:\"CODE\"") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("a0 LEDATA") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("segment 1:\"CODE\" offset 0000 bytes 4") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("external 1 \"PRINTF\"") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("fixup segment location offset16") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("target external:1:\"PRINTF\"") != std::string::npos);
    // The dump carries no host state, so two runs of the same input agree.
    OmfNameTable again_table;
    std::string again;
    for (const OmfRecord& record : records) {
        dosbase::devtools::dump_omf_record(record, again_table, "  ", false, again);
    }
    DOSBASE_TEST_EXPECT_EQ(again, text);
}

DOSBASE_TEST(omf_dump_includes_payload_bytes_on_request) {
    const std::vector<OmfRecord> records = parse_omf_records(sample_object(), 0, false);
    OmfNameTable table;
    std::string text;
    for (const OmfRecord& record : records) {
        dosbase::devtools::dump_omf_record(record, table, "  ", true, text);
    }
    DOSBASE_TEST_EXPECT(text.find("data b44ccd21") != std::string::npos);
}

DOSBASE_TEST(omf_reports_a_bad_checksum_without_failing_the_parse) {
    std::vector<std::uint8_t> bytes = sample_object();
    bytes.back() = static_cast<std::uint8_t>(bytes.back() ^ 0xFFU);
    const std::vector<OmfRecord> records = parse_omf_records(bytes, 0, false);
    DOSBASE_TEST_EXPECT(!records.back().checksum_valid());
    OmfNameTable table;
    std::string text;
    dosbase::devtools::dump_omf_record(records.back(), table, "  ", false, text);
    DOSBASE_TEST_EXPECT(text.find("checksum bad") != std::string::npos);
    DOSBASE_TEST_EXPECT(serialize_omf_records(records) == bytes);
}

DOSBASE_TEST(omf_stops_after_modend_for_library_members) {
    std::vector<std::uint8_t> bytes = sample_object();
    const std::size_t module_bytes = bytes.size();
    const std::vector<std::uint8_t> second = sample_object();
    bytes.insert(bytes.end(), second.begin(), second.end());
    std::size_t consumed = 0;
    const std::vector<OmfRecord> records = parse_omf_records(bytes, 0, true, &consumed);
    DOSBASE_TEST_EXPECT_EQ(consumed, module_bytes);
    DOSBASE_TEST_EXPECT_EQ(records.size(), std::size_t{7});
}

DOSBASE_TEST(omf_rejects_truncated_and_impossible_records) {
    const auto rejects = [](const std::vector<std::uint8_t>& bytes) {
        try {
            (void)parse_omf_records(bytes, 0, false);
        } catch (const FormatError&) {
            return true;
        }
        return false;
    };
    std::vector<std::uint8_t> bytes = sample_object();
    bytes.pop_back();
    DOSBASE_TEST_EXPECT(rejects(bytes));
    DOSBASE_TEST_EXPECT(rejects({0x80, 0x00, 0x00}));
    DOSBASE_TEST_EXPECT(rejects({0x00, 0x01, 0x00, 0x00}));
    DOSBASE_TEST_EXPECT(rejects({0x80}));
}

DOSBASE_TEST(omf_decodes_iterated_data_blocks) {
    ObjectBuilder builder;
    builder.record(0x80, ObjectBuilder::name("FILL.ASM"));
    // LIDATA: segment 1, offset 0, repeat 4 of a nested block that repeats
    // two bytes twice — the shape MASM emits for DUP initializers.
    builder.record(0xA2, {0x01, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00,
                          0x02, 0x90, 0x90});
    builder.record(0x8A, {0x00});
    const std::vector<OmfRecord> records = parse_omf_records(builder.bytes(), 0, false);
    OmfNameTable table;
    std::string text;
    for (const OmfRecord& record : records) {
        dosbase::devtools::dump_omf_record(record, table, "  ", true, text);
    }
    DOSBASE_TEST_EXPECT(text.find("a2 LIDATA") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("block repeat 4 blocks 1") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("block repeat 2 blocks 0 content 2") != std::string::npos);
    DOSBASE_TEST_EXPECT(text.find("data 9090") != std::string::npos);
    DOSBASE_TEST_EXPECT(serialize_omf_records(records) == builder.bytes());
}
