// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// The structured OMF module reader cklink is built on. Fixtures
// are crafted directly rather than pulled from a real object, matching
// test_omf.cpp's approach for the record-level parser this layer sits on;
// each record's bytes are checksummed the way MASM writes them.
#include "test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/omf_module.hpp"
#include "dosbase/devtools/omf_record.hpp"

namespace {
using dosbase::devtools::DataChunk;
using dosbase::devtools::FixupRefKind;
using dosbase::devtools::FormatError;
using dosbase::devtools::Module;
using dosbase::devtools::OmfRecord;
using dosbase::devtools::parse_module;
using dosbase::devtools::parse_omf_records;

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
    for (const std::vector<std::uint8_t>& part : parts) result.insert(result.end(), part.begin(), part.end());
    return result;
}

Module parse(const std::vector<std::uint8_t>& bytes) {
    return parse_module(parse_omf_records(bytes, 0, false));
}
}  // namespace

// A single-segment, single-group module shaped like MSBOOT.ASM's real
// structure: an absolute SEGBIOS segment referenced only by a fixup frame,
// a CODE segment holding the actual data, a self-relative fixup, and a
// segment-relative one -- the two location kinds MSBOOT.OBJ's own FIXUPP
// record uses.
DOSBASE_TEST(parse_module_reads_segments_groups_and_absolute_frames) {
    ObjectBuilder builder;
    builder.record(0x80, ObjectBuilder::name("msboot.asm"));
    builder.record(0x96, concat({ObjectBuilder::name(""), ObjectBuilder::name("SEGBIOS"),
                                 ObjectBuilder::name("CODE")}));
    // SEGBIOS: acbp=0x00 (absolute, private, use16), frame 0x0070, offset 0, length 0.
    builder.record(0x98, {0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x01});
    // CODE: acbp=0x60 (paragraph align, private, use16), length 0x7E00.
    builder.record(0x98, {0x60, 0x00, 0x7E, 0x03, 0x02, 0x01});
    const Module module = parse(builder.bytes());

    DOSBASE_TEST_EXPECT_EQ(module.name, std::string{"msboot.asm"});
    DOSBASE_TEST_EXPECT_EQ(module.segments.size(), std::size_t{2});
    const auto& segbios = module.segments[0];
    DOSBASE_TEST_EXPECT_EQ(segbios.name, std::string{"SEGBIOS"});
    DOSBASE_TEST_EXPECT_EQ(segbios.alignment, std::uint8_t{0});
    DOSBASE_TEST_EXPECT_EQ(segbios.absolute_frame, std::uint16_t{0x0070});
    DOSBASE_TEST_EXPECT_EQ(segbios.length, std::uint32_t{0});
    const auto& code = module.segments[1];
    DOSBASE_TEST_EXPECT_EQ(code.name, std::string{"CODE"});
    DOSBASE_TEST_EXPECT_EQ(code.alignment, std::uint8_t{3});  // paragraph
    DOSBASE_TEST_EXPECT_EQ(code.combine, std::uint8_t{0});    // private
    DOSBASE_TEST_EXPECT_EQ(code.length, std::uint32_t{0x7E00});
}

DOSBASE_TEST(parse_module_expands_ledata_and_resolves_fixups_against_it) {
    ObjectBuilder builder;
    builder.record(0x80, ObjectBuilder::name("t.asm"));
    builder.record(0x96, concat({ObjectBuilder::name(""), ObjectBuilder::name("CODE")}));
    builder.record(0x98, {0x28, 0x00, 0x10, 0x02, 0x02, 0x01});  // CODE, word-aligned, length 0x10
    // LEDATA: segment 1, offset 0, four bytes b4 4c cd 21.
    builder.record(0xA0, {0x01, 0x00, 0x00, 0xB4, 0x4C, 0xCD, 0x21});
    // FIXUPP: segment-relative, location offset16, data offset 0x0001,
    // frame=segment(CODE), target=segment(CODE), displacement 0x0002 --
    // matches MSBOOT.OBJ's own "fixup segment location offset16 ...
    // frame segment:2 target segment:2 displacement ..." shape.
    builder.record(0x9C, {0xC4, 0x01, 0x00, 0x01, 0x01, 0x02, 0x00});
    const Module module = parse(builder.bytes());

    DOSBASE_TEST_EXPECT_EQ(module.data.size(), std::size_t{1});
    DOSBASE_TEST_EXPECT_EQ(module.data[0].segment, std::size_t{0});
    DOSBASE_TEST_EXPECT_EQ(module.data[0].offset, std::uint32_t{0});
    DOSBASE_TEST_EXPECT_EQ(module.data[0].bytes.size(), std::size_t{4});
    DOSBASE_TEST_EXPECT_EQ(module.data[0].bytes[0], std::uint8_t{0xB4});

    DOSBASE_TEST_EXPECT_EQ(module.fixups.size(), std::size_t{1});
    const auto& fix = module.fixups[0];
    DOSBASE_TEST_EXPECT(fix.segment_relative);
    DOSBASE_TEST_EXPECT_EQ(fix.location, std::uint8_t{1});  // offset16
    DOSBASE_TEST_EXPECT_EQ(fix.segment, std::size_t{0});
    DOSBASE_TEST_EXPECT_EQ(fix.offset, std::uint32_t{1});  // LEDATA offset 0 + data offset 1
    DOSBASE_TEST_EXPECT(fix.frame.kind == FixupRefKind::segment);
    DOSBASE_TEST_EXPECT_EQ(fix.frame.index, std::size_t{0});
    DOSBASE_TEST_EXPECT(fix.target.kind == FixupRefKind::segment);
    DOSBASE_TEST_EXPECT(fix.has_displacement);
    DOSBASE_TEST_EXPECT_EQ(fix.displacement, std::uint16_t{0x0002});
}

DOSBASE_TEST(parse_module_expands_lidata_repeats_into_flat_bytes) {
    ObjectBuilder builder;
    builder.record(0x80, ObjectBuilder::name("fill.asm"));
    builder.record(0x96, concat({ObjectBuilder::name(""), ObjectBuilder::name("CODE")}));
    builder.record(0x98, {0x28, 0x00, 0x08, 0x02, 0x02, 0x01});
    // LIDATA: segment 1, offset 0, repeat 3 of a two-byte block {0x90, 0x90}.
    builder.record(0xA2, {0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x90, 0x90});
    const Module module = parse(builder.bytes());

    DOSBASE_TEST_EXPECT_EQ(module.data.size(), std::size_t{1});
    const std::vector<std::uint8_t> expected(6, 0x90);
    DOSBASE_TEST_EXPECT(module.data[0].bytes == expected);
}

DOSBASE_TEST(parse_module_reads_groups_publics_and_externals) {
    ObjectBuilder builder;
    builder.record(0x80, ObjectBuilder::name("t.asm"));
    builder.record(0x96, concat({ObjectBuilder::name(""), ObjectBuilder::name("DGROUP"),
                                 ObjectBuilder::name("_DATA")}));
    builder.record(0x98, {0x28, 0x00, 0x10, 0x03, 0x03, 0x01});  // segment 1 "_DATA"
    builder.record(0x9A, {0x02, 0xFF, 0x01});                    // GRPDEF DGROUP { segment 1 }
    // PUBDEF: group 1 (DGROUP), segment 1 (_DATA), one public "_value" at offset 4.
    builder.record(0x90, concat({{0x01, 0x01}, ObjectBuilder::name("_value"), {0x04, 0x00, 0x00}}));
    builder.record(0x8C, concat({ObjectBuilder::name("PRINTF"), {0x00}}));  // EXTDEF
    const Module module = parse(builder.bytes());

    DOSBASE_TEST_EXPECT_EQ(module.groups.size(), std::size_t{1});
    DOSBASE_TEST_EXPECT_EQ(module.groups[0].name, std::string{"DGROUP"});
    DOSBASE_TEST_EXPECT_EQ(module.groups[0].segments, (std::vector<std::size_t>{0}));

    DOSBASE_TEST_EXPECT_EQ(module.publics.size(), std::size_t{1});
    DOSBASE_TEST_EXPECT_EQ(module.publics[0].name, std::string{"_value"});
    DOSBASE_TEST_EXPECT_EQ(module.publics[0].offset, std::uint32_t{4});
    DOSBASE_TEST_EXPECT_EQ(module.publics[0].segment, std::size_t{0});
    DOSBASE_TEST_EXPECT_EQ(module.publics[0].group, std::size_t{0});

    DOSBASE_TEST_EXPECT_EQ(module.externals.size(), std::size_t{1});
    DOSBASE_TEST_EXPECT_EQ(module.externals[0].name, std::string{"PRINTF"});
    DOSBASE_TEST_EXPECT(!module.externals[0].communal);
}

DOSBASE_TEST(parse_module_reads_modend_with_and_without_a_start_address) {
    {
        ObjectBuilder builder;
        builder.record(0x80, ObjectBuilder::name("t.asm"));
        builder.record(0x8A, {0x00});  // plain MODEND: not main, no start
        const Module module = parse(builder.bytes());
        DOSBASE_TEST_EXPECT(!module.end.is_main);
        DOSBASE_TEST_EXPECT(!module.end.has_start);
    }
    {
        ObjectBuilder builder;
        builder.record(0x80, ObjectBuilder::name("t.asm"));
        builder.record(0x96, concat({ObjectBuilder::name(""), ObjectBuilder::name("CODE")}));
        builder.record(0x98, {0x28, 0x00, 0x10, 0x02, 0x02, 0x01});
        // MODEND: main + start, end_data selects frame=segment(1), target=segment(1),
        // with a displacement.
        builder.record(0x8A, {0xC0, 0x00, 0x01, 0x01, 0x00, 0x00});
        const Module module = parse(builder.bytes());
        DOSBASE_TEST_EXPECT(module.end.is_main);
        DOSBASE_TEST_EXPECT(module.end.has_start);
        DOSBASE_TEST_EXPECT(module.end.start_frame.kind == FixupRefKind::segment);
        DOSBASE_TEST_EXPECT(module.end.start_target.kind == FixupRefKind::segment);
        DOSBASE_TEST_EXPECT(module.end.start_has_displacement);
    }
}

DOSBASE_TEST(parse_module_refuses_a_fixupp_with_no_preceding_data_record) {
    ObjectBuilder builder;
    builder.record(0x80, ObjectBuilder::name("t.asm"));
    builder.record(0x96, concat({ObjectBuilder::name(""), ObjectBuilder::name("CODE")}));
    builder.record(0x98, {0x28, 0x00, 0x10, 0x02, 0x02, 0x01});
    builder.record(0x9C, {0xC4, 0x00, 0x00, 0x01, 0x00, 0x00});  // no LEDATA/LIDATA before this
    bool refused = false;
    try {
        (void)parse(builder.bytes());
    } catch (const FormatError&) {
        refused = true;
    }
    DOSBASE_TEST_EXPECT(refused);
}

DOSBASE_TEST(parse_module_resolves_thread_subrecords_in_later_fixups) {
    ObjectBuilder builder;
    builder.record(0x80, ObjectBuilder::name("t.asm"));
    builder.record(0x96, concat({ObjectBuilder::name(""), ObjectBuilder::name("CODE")}));
    builder.record(0x98, {0x28, 0x00, 0x10, 0x02, 0x02, 0x01});
    builder.record(0xA0, {0x01, 0x00, 0x00, 0x90});
    // Define target thread 0 as segment 1, then use it in an offset16 fixup.
    // fix_data 0Ch selects a direct segment frame, target thread 0, and no
    // displacement.
    builder.record(0x9C, {0x00, 0x01, 0xC4, 0x00, 0x0C, 0x01});
    const Module module = parse(builder.bytes());
    DOSBASE_TEST_EXPECT_EQ(module.fixups.size(), std::size_t{1});
    DOSBASE_TEST_EXPECT(module.fixups[0].frame.kind == FixupRefKind::segment);
    DOSBASE_TEST_EXPECT_EQ(module.fixups[0].frame.index, std::size_t{0});
    DOSBASE_TEST_EXPECT(module.fixups[0].target.kind == FixupRefKind::segment);
    DOSBASE_TEST_EXPECT_EQ(module.fixups[0].target.index, std::size_t{0});
}
