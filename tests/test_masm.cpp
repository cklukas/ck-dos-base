// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// MASM 5.10 compatibility fixtures. Each expectation pins a rule measured
// from a reference-produced object so a regression names itself.
#include "test_harness.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dosbase/devtools/masm.hpp"
#include "dosbase/devtools/omf_record.hpp"

namespace {
using dosbase::devtools::MasmOptions;
using dosbase::devtools::MasmResult;
using dosbase::devtools::masm_assemble;
using dosbase::devtools::parse_omf_records;

std::filesystem::path fixture_dir() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dosbase-masm-fixtures";
    std::filesystem::create_directories(dir);
    return dir;
}

// Writes `source`, assembles it as `name`, and returns the object bytes.
std::vector<std::uint8_t> assemble(const std::string& name, const std::string& source) {
    const std::filesystem::path dir = fixture_dir();
    std::ofstream stream(dir / name, std::ios::binary | std::ios::trunc);
    stream << source;
    stream.close();
    MasmOptions options;
    options.preserve_public_case = true;
    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(dir);
    const MasmResult result = masm_assemble(name, options);
    std::filesystem::current_path(previous);
    for (const std::string& error : result.errors) {
        throw std::runtime_error("ckmasm: " + error);
    }
    return result.object;
}

std::string hex(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    char buffer[4];
    for (std::uint8_t byte : bytes) {
        std::snprintf(buffer, sizeof buffer, "%02x", byte);
        out += buffer;
    }
    return out;
}

// The LEDATA payload of the first segment carrying data.
std::vector<std::uint8_t> code_bytes(const std::vector<std::uint8_t>& object) {
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0xA0) {
            return {record.body.begin() + 3, record.body.end()};
        }
    }
    return {};
}

const char* kPrologue =
    "code segment byte public 'code'\n"
    "        assume cs:code\n"
    "start proc far\n";
const char* kEpilogue =
    "start endp\n"
    "code ends\n"
    "        end\n";
}  // namespace

DOSBASE_TEST(masm_prefers_the_accumulator_immediate_form) {
    // EXIT.OBJ: `cmp ax,1` is 3D 01 00, never the shorter 83 F8 01 -- the
    // accumulator form wins for AX regardless of the immediate's size.
    const auto object = assemble("acc.asm", std::string(kPrologue) +
                                                "        cmp ax,1\n"
                                                "        cmp bx,1\n" +
                                                kEpilogue);
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)), std::string{"3d010083fb01"});
}

DOSBASE_TEST(masm_pads_a_relaxed_forward_jmp_with_a_nop) {
    // BEEP.OBJ: a forward JMP without SHORT reserves the near form in pass
    // 1; pass 2 finds the target in range and emits EB xx 90, keeping every
    // later offset stable. A backward JMP is short outright.
    const auto object = assemble("jmp.asm", std::string(kPrologue) +
                                                "back:   nop\n"
                                                "        jmp fwd\n"
                                                "        nop\n"
                                                "fwd:    jmp back\n" +
                                                kEpilogue);
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)), std::string{"90eb029090ebf9"});
}

DOSBASE_TEST(masm_elides_a_default_segment_override) {
    // GETCNTRY.OBJ: ds:[si] emits bare 8B 04 -- DS is [si]'s default -- while
    // es:[di+4] really carries the 26 prefix.
    const auto object = assemble("ovr.asm", std::string(kPrologue) +
                                                "        mov ax,ds:[si]\n"
                                                "        mov es:[di+4],ax\n" +
                                                kEpilogue);
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)), std::string{"8b0426894504"});
}

DOSBASE_TEST(masm_sizes_struc_fields_and_far_returns) {
    // CHDIR.OBJ: the str STRUC's fields land at 0/2/6/10, `size str` is 14,
    // and RET in a FAR proc encodes CA with the operand.
    const auto object = assemble("struc.asm",
                                 "str struc\n"
                                 "old_bp dw ?\n"
                                 "return dd ?\n"
                                 "dtrm   dd 0h\n"
                                 "dnam   dd ?\n"
                                 "str ends\n" +
                                     std::string(kPrologue) +
                                     "        mov dx,word ptr [bp].dnam\n"
                                     "        ret size str - 6\n" +
                                     kEpilogue);
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)), std::string{"8b560aca0800"});
}

DOSBASE_TEST(masm_restores_outer_proc_distance_after_nested_endp) {
    // NESTPROC.OBJ, assembled with MASM 5.10: the inner near procedure's RET
    // is C3, then its ENDP restores the enclosing far procedure so the outer
    // RET is CB.
    const auto object = assemble("nestproc.asm",
                                 "code segment byte public 'code'\n"
                                 "        assume cs:code\n"
                                 "outer proc far\n"
                                 "inner proc near\n"
                                 "        ret\n"
                                 "inner endp\n"
                                 "        ret\n"
                                 "outer endp\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)), std::string{"c3cb"});
}

DOSBASE_TEST(masm_encodes_movsx_from_byte_register) {
    // MOVSX is supported for the MEMM/XMAEM 386 sources; keep the behavior
    // explicit rather than listing the instruction in the refusal fixture.
    const auto object = assemble("movsx.asm", std::string(kPrologue) +
                                                  "        movsx ax,bl\n" +
                                                  kEpilogue);
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)), std::string{"0fbec3"});
}

DOSBASE_TEST(masm_emits_extdef_and_extern_fixups) {
    // SFILEINF.OBJ: EXTDEF sits between SEGDEF and PUBDEF with type byte 0;
    // a data reference to the external gets a FIXUPP whose fixdata 0x56 is
    // frame F_TARG, target T_EXT.
    const auto object = assemble("ext.asm", std::string(kPrologue) +
                                                "        extrn Outside:word\n"
                                                "        mov bx,Outside\n" +
                                                kEpilogue);
    std::vector<std::uint8_t> extdef;
    std::vector<std::uint8_t> fixupp;
    bool extdef_before_pubdef = false;
    bool pubdef_seen = false;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x8C) {
            extdef = record.body;
            extdef_before_pubdef = !pubdef_seen;
        }
        if (record.type == 0x90) pubdef_seen = true;
        if (record.type == 0x9C) fixupp = record.body;
        DOSBASE_TEST_EXPECT(record.checksum_valid());
    }
    DOSBASE_TEST_EXPECT_EQ(hex(extdef), std::string{"074f75747369646500"});
    DOSBASE_TEST_EXPECT(extdef_before_pubdef);
    // The extern was declared inside 'code' under `assume cs:code`, so the
    // reference synthesizes a 2E prefix (CHARIN.OBJ) -- the zeroed word sits
    // at data offset 3. locat 0xC403; fixdata 0x06 = frame F_SEG of the
    // declaring segment, target T_EXT (F_CLOSE.OBJ); frame 1, extern 1.
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp), std::string{"c403060101"});
}

DOSBASE_TEST(masm_frames_es_overridden_accumulator_store_at_use) {
    // Real MASM 5.10 EXTFRAME.OBJ: even though VECTOR is declared inside
    // OTHER, the explicit ES: store under ASSUME ES:MAIN frames its offset
    // relocation through MAIN. AX selects the compact A3 encoding.
    const auto object = assemble("extframe.asm",
                                 "other segment para public 'code'\n"
                                 "        extrn vector:dword\n"
                                 "other ends\n"
                                 "main segment para public 'code'\n"
                                 "        assume es:main\n"
                                 "        mov word ptr es:vector + word,ax\n"
                                 "main ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"26a30000"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c4020202010200"});
}

DOSBASE_TEST(masm_frames_based_externals_through_default_ds) {
    // Real MASM 5.10 DSFRAME.OBJ: indexed EXTWORD/EXTABS references frame
    // through the DATA segment currently ASSUMEd to DS. Direct references
    // to the same externals remain framed by their external targets.
    const auto object = assemble("dsframe.asm",
                                 "data segment word public 'data'\n"
                                 "data ends\n"
                                 "extrn extword:word\n"
                                 "extrn extabs:abs\n"
                                 "code segment word public 'code'\n"
                                 "        assume cs:code,ds:data\n"
                                 "        mov ax,extword[bx]\n"
                                 "        mov ax,extabs[bx]\n"
                                 "        mov ax,extword\n"
                                 "        mov ax,extabs\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"8b8700008b870000a10000b80000"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(
        hex(fixupp),
        std::string{"c402060102c406060101c409260202c40c260101"});
}

DOSBASE_TEST(masm_keeps_parenthesized_offset_data_relocations) {
    // Real MASM 5.10 OFFPAREN.OBJ: parentheses around the whole OFFSET or
    // around its target are decoration. All three words are zero in LEDATA
    // and carry identical offset16 relocations to TARGET at offset 6.
    const auto object = assemble("offparen.asm",
                                 "code segment byte public 'code'\n"
                                 "        assume cs:code\n"
                                 "        dw (offset target)\n"
                                 "        dw offset target\n"
                                 "        dw offset (target)\n"
                                 "target: nop\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"00000000000090"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(
        hex(fixupp),
        std::string{"c4000001010600c4020001010600c4040001010600"});
}

DOSBASE_TEST(masm_keeps_seg_relocation_kind_in_memory_immediates) {
    // Real MASM 5.10 OFFPAREN.OBJ: storing SEG directly to memory carries a
    // base relocation at data offset 2; storing OFFSET carries offset16 at 7.
    const auto object = assemble("memseg.asm",
                                 "code segment byte public 'code'\n"
                                 "        assume cs:code\n"
                                 "        mov word ptr [bx],seg code\n"
                                 "        mov word ptr [bx+2],offset target\n"
                                 "target: nop\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"c7070000c74702000090"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c802040101c4070001010900"});
}

DOSBASE_TEST(masm_pads_a_byte_store_through_a_late_extern_alias) {
    // Real MASM 5.10 LATALIAS.OBJ: when the EQU precedes its EXTRN, pass 1
    // reserves a word store and pass 2 emits C6 plus a NOP. An alias declared
    // after its EXTRN is typed in pass 1 and needs no pad.
    const auto object = assemble("latalias.asm",
                                 "fields struc\n"
                                 "pad db ?\n"
                                 "flag db ?\n"
                                 "fields ends\n"
                                 "late equ late_ext\n"
                                 "extrn late_ext:byte\n"
                                 "extrn early_ext:byte\n"
                                 "early equ early_ext\n"
                                 "code segment byte public 'code'\n"
                                 "        assume cs:code\n"
                                 "        mov late.flag,0\n"
                                 "        mov early.flag,0\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"c60600000090c606000000"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c4022201010100c4082202020100"});
}

DOSBASE_TEST(masm_pads_test_through_a_late_extern_alias) {
    // Real MASM 5.10 LATETEST.OBJ: pass 1 reserves an untyped alias as a
    // word TEST; after EXTRN supplies the byte type, pass 2 emits F6 plus a
    // trailing NOP to preserve the reserved length.
    const auto object = assemble("latetest.asm",
                                 "fields struc\n"
                                 "pad db ?\n"
                                 "flag db ?\n"
                                 "fields ends\n"
                                 "late equ late_ext\n"
                                 "extrn late_ext:byte\n"
                                 "code segment byte public 'code'\n"
                                 "        assume cs:code\n"
                                 "        test late.flag,1\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"f60600000190"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c4022201010100"});
}

DOSBASE_TEST(masm_keeps_word_immediate_that_sizes_untyped_memory) {
    // Real MASM 5.10 ALUSIZE.OBJ: when the word-valued immediate is what
    // establishes an otherwise untyped memory operand's width, MASM keeps
    // 81 /digit iw. An explicit WORD PTR makes 83 /digit ib available.
    const auto object = assemble("alusize.asm",
                                 ".386p\n"
                                 "fields struc\n"
                                 "pad db ?\n"
                                 "opt dw ?\n"
                                 "fields ends\n"
                                 "code segment use16 word public 'code'\n"
                                 "        assume cs:code\n"
                                 "        xor [bx],0ffffh\n"
                                 "        xor word ptr [bx],0ffffh\n"
                                 "        and [bx],0fff0h\n"
                                 "        and word ptr [bx],0fff0h\n"
                                 "        and [bx]+opt,0fff0h\n"
                                 "        cmp word ptr [bx],1\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(
        hex(code_bytes(object)),
        std::string{"8137ffff8337ff8127f0ff8327f0836701f0833f01"});
}

DOSBASE_TEST(masm_preserves_size_of_forward_derived_equate) {
    // Real MASM 5.10 FWDEQU.OBJ: DELTA and PARAS are defined before their
    // source labels. Although PARAS is 10h by its pass-2 use, SUB retains
    // the pass-1 memory-sized reservation by padding the accumulator form.
    const auto object = assemble("fwdequ.asm",
                                 "delta equ (target - base)\n"
                                 "paras equ (delta / 10h)\n"
                                 "code segment word public 'code'\n"
                                 "        assume cs:code\n"
                                 "base label byte\n"
                                 "        org $+160\n"
                                 "target:\n"
                                 "        sub ax,paras\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"2d0a0090"});
}

DOSBASE_TEST(masm_preserves_offset_relocation_through_equate) {
    // Real MASM 5.10 EQUOFF.OBJ: folding an OFFSET-external expression into
    // an EQU preserves its target, STACK frame, and -40 addend. The compare
    // therefore uses the full relocated immediate form, not 83 FC D8.
    const auto object = assemble("equoff.asm",
                                 "stack segment para stack 'stack'\n"
                                 "extrn kstack_top:byte\n"
                                 "stack ends\n"
                                 "vmt_stack equ offset stack:kstack_top - 40\n"
                                 "code segment word public 'code'\n"
                                 "        assume cs:code,ss:nothing\n"
                                 "        cmp sp,vmt_stack\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"81fc0000"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c402020101d8ff"});
}

DOSBASE_TEST(masm_distinguishes_this_label_from_this_difference) {
    // Real MASM 5.10 THISEQU.OBJ: `HERE EQU THIS BYTE` is a DATA-relative
    // label, but adding `- START` makes LEN an absolute constant. Their
    // PUBDEF records therefore have different headers despite equal values.
    const auto object = assemble("thisequ.asm",
                                 "data segment word public 'data'\n"
                                 "START label byte\n"
                                 "        db 5 dup (0)\n"
                                 "        public LEN,HERE\n"
                                 "LEN equ this byte - START\n"
                                 "HERE equ this byte\n"
                                 "data ends\n"
                                 "        end\n");
    std::vector<std::vector<std::uint8_t>> pubdefs;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x90) pubdefs.push_back(record.body);
    }
    DOSBASE_TEST_EXPECT_EQ(pubdefs.size(), std::size_t{2});
    DOSBASE_TEST_EXPECT_EQ(hex(pubdefs[0]),
                              std::string{"00000000034c454e050000"});
    DOSBASE_TEST_EXPECT_EQ(hex(pubdefs[1]),
                              std::string{"00010448455245050000"});
}

DOSBASE_TEST(masm_uses_32bit_records_and_addresses_for_use32_absolute_segment) {
    // Real MASM 5.10 ABS32.OBJ: an absolute segment declared under .386P
    // defaults to USE32. Its initialized DUP is LIDATA32 (A3), and direct
    // references from USE16 code carry 67 + disp32 with offset32 fixups.
    const auto object = assemble("abs32.asm",
                                 ".386p\n"
                                 "abs32 segment at 80h\n"
                                 "X db 6 dup (0)\n"
                                 "abs32 ends\n"
                                 "code segment use16 word public 'code'\n"
                                 "        assume cs:code,ds:abs32\n"
                                 "        mov cx,[X]\n"
                                 "        mov ax,[X+2]\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"678b0d0000000067a100000000"});
    std::vector<std::uint8_t> lidata32;
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0xA3) lidata32 = record.body;
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(
        hex(lidata32),
        std::string{"01000000000600000001000100000000000100"});
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"e403040101e4090001010200"});
}

DOSBASE_TEST(masm_frames_same_segment_data_offset_by_segment) {
    // Real MASM 5.10 SAMEFIX.OBJ: a data word pointing at a label in its own
    // segment uses F_SEG DATA, even though DATA belongs to DG and no ASSUME
    // is active at the directive.
    const auto object = assemble("samefix.asm",
                                 "dg group data\n"
                                 "data segment word public 'data'\n"
                                 "TARGET db 0\n"
                                 "POINTER dw offset TARGET\n"
                                 "data ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"000000"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c401040101"});
}

DOSBASE_TEST(masm_keeps_org_overlay_fixups_with_their_data_record) {
    // Real MASM 5.10 ORGFX.OBJ: the original three-entry table and the ORG
    // replacement each have their own LEDATA/FIXUPP pair. Relocations at
    // offset 4 in the segment therefore occur once in each record, not twice
    // in both records merely because the segment offsets overlap.
    const auto object = assemble("orgfx.asm",
                                 "code segment word public 'code'\n"
                                 "        assume cs:code\n"
                                 "table:\n"
                                 "        rept 3\n"
                                 "        jmp default\n"
                                 "        endm\n"
                                 "table_end:\n"
                                 "        org table+3\n"
                                 "        jmp special\n"
                                 "        org table_end\n"
                                 "        db 200 dup (0)\n"
                                 "default:\n"
                                 "        nop\n"
                                 "special:\n"
                                 "        nop\n"
                                 "code ends\n"
                                 "        end\n");
    std::vector<std::string> fixupps;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupps.push_back(hex(record.body));
    }
    DOSBASE_TEST_EXPECT_EQ(
        fixupps,
        (std::vector<std::string>{
            "8401000101d1008404000101d1008407000101d100",
            "8401000101d200"}));
}

DOSBASE_TEST(masm_relocates_parenthesized_external_offset_difference) {
    // Real MASM 5.10 OFFDIFF.OBJ: this is the data expression used by the
    // INDEINS long-condition macros. The word is zero and the external's
    // -4 addend rides in an F_EXT/T_EXT fixup.
    const auto object = assemble("offdiff.asm",
                                 "code segment word public 'code'\n"
                                 "        assume cs:code\n"
                                 "        extrn ext:near\n"
                                 "temp = $\n"
                                 "        db 0fh,85h\n"
                                 "        dw (offset ext)-(temp+4)\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"0f850000"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c402220101fcff"});
}

DOSBASE_TEST(masm_relocates_abs_externals_used_as_based_fields) {
    // Real MASM 5.10 ABSFIELD.OBJ: an ABS external selected after [BX]
    // occupies a disp16 field and relocates that field. A numeric suffix is
    // the fixup displacement; the instruction bytes remain zeroed.
    const auto object = assemble("absfield.asm",
                                 "code segment word public 'code'\n"
                                 "        assume cs:code\n"
                                 "        extrn field:abs\n"
                                 "start:\n"
                                 "        mov word ptr [bx].field,ax\n"
                                 "        mov word ptr [bx].field,offset start\n"
                                 "        mov [bx].field+2,cs\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(
        hex(code_bytes(object)),
        std::string{"89870000c787000000008c8f0000"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(
        hex(fixupp),
        std::string{
            "c402260101c406260101c408040101c40c2201010200"});
}

DOSBASE_TEST(masm_relocates_abs_externals_in_byte_data) {
    // Real MASM 5.10 ABSBYTE.OBJ: DB leaves each ABS external byte zero and
    // emits an F_EXT/T_EXT low-byte fixup at its LEDATA offset.
    const auto object = assemble("absbyte.asm",
                                 "        extrn e_cr:abs\n"
                                 "data segment word public 'data'\n"
                                 "        db 'X',e_cr,e_cr\n"
                                 "data ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"580000"});
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c001260101c002260101"});
}

DOSBASE_TEST(masm_fixupp_cap_preflights_extended_indices_as_one_byte) {
    // Real MASM 5.10 FIXCAPX.OBJ: the preflight accepts a 1019-byte logical
    // body, then index 128 extends twice and the emitted FIXUPP body reaches
    // 1021 bytes. The following word starts a new LEDATA/FIXUPP pair.
    std::string source = "data segment word public 'data'\n";
    char external[5];
    for (int index = 1; index <= 128; ++index) {
        if (index == 99) continue;
        std::snprintf(external, sizeof external, "X%03d", index);
        source += std::string{"        extrn "} + external + ":abs\n";
    }
    source += "        extrn X099:abs\n"
              "        rept 200\n"
              "        dw offset X001\n"
              "        endm\n"
              "        dw offset X001+1\n"
              "        dw offset X001+2\n"
              "        dw offset X099\n"
              "        dw offset X001\n"
              "data ends\n"
              "        end\n";
    const auto object = assemble("fixcapx.asm", source);
    std::vector<std::size_t> ledata_bodies;
    std::vector<std::size_t> fixupp_bodies;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0xA0) ledata_bodies.push_back(record.body.size());
        if (record.type == 0x9C) fixupp_bodies.push_back(record.body.size());
    }
    DOSBASE_TEST_EXPECT_EQ(
        ledata_bodies, (std::vector<std::size_t>{409, 5}));
    DOSBASE_TEST_EXPECT_EQ(
        fixupp_bodies, (std::vector<std::size_t>{1021, 5}));
}

DOSBASE_TEST(masm_relocates_offset_label_in_struc_override_lidata) {
    // Real MASM 5.10 STROFF.OBJ: OFFSET in a structure initializer leaves
    // the two-byte field zero and attaches its relocation to the LIDATA.
    const auto object = assemble("stroff.asm",
                                 "entry struc\n"
                                 "pointer dw 0\n"
                                 "value db 0\n"
                                 "entry ends\n"
                                 "code segment word public 'code'\n"
                                 "target db 0\n"
                                 "table entry <offset target,1>\n"
                                 "code ends\n"
                                 "        end\n");
    std::vector<std::uint8_t> lidata;
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0xA2) lidata = record.body;
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(
        hex(lidata),
        std::string{"01010001000000020000010000000101"});
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c405040101"});
}

DOSBASE_TEST(masm_relocates_label_in_initialized_dup_lidata) {
    // Real MASM 5.10 DUPLABEL.OBJ: one relocation at block-image offset 9
    // applies TARGET's offset to every repetition of the zero word.
    const auto object = assemble("duplabel.asm",
                                 "code segment word public 'code'\n"
                                 "table dw 3 dup(target)\n"
                                 "target db 0\n"
                                 "code ends\n"
                                 "        end\n");
    std::vector<std::uint8_t> lidata;
    std::vector<std::uint8_t> fixupp;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type == 0xA2) lidata = record.body;
        if (record.type == 0x9C) fixupp = record.body;
    }
    DOSBASE_TEST_EXPECT_EQ(
        hex(lidata),
        std::string{"0100000300010001000000020000"});
    DOSBASE_TEST_EXPECT_EQ(hex(fixupp),
                              std::string{"c4090001010600"});
}

DOSBASE_TEST(masm_sizes_untyped_store_from_immediate_ptr_cast) {
    // MODEPARS.OBJ's `MOV ES:[DI],BYTE PTR blank`: the PTR cast belongs to
    // the immediate, but it still chooses the byte form for the memory store.
    const auto object = assemble("immptr.asm",
                                 "blank equ ' '\n"
                                 "code segment word public 'code'\n"
                                 "        mov es:[di],byte ptr blank\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"26c60520"});
}

DOSBASE_TEST(masm_shadows_bracketed_register_equ_with_type_suffix) {
    // Real MASM 5.10 BRALIAS.OBJ: TYPE in the bracket following PARM_LIST
    // shadows its `[BX]` EQU body; the no-TYPE and literal controls combine.
    const auto object = assemble("bralias.asm",
                                 "parm_list equ [bx]\n"
                                 "current_parm equ di\n"
                                 "entry struc\n"
                                 "value db 0\n"
                                 "item_tag db 0\n"
                                 "entry ends\n"
                                 "code segment word public 'code'\n"
                                 "mov parm_list[current_parm+type entry].item_tag,56h\n"
                                 "mov parm_list[current_parm].item_tag,55h\n"
                                 "mov byte ptr [bx][si],56h\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"c6450356c6410155c60056"});
}

DOSBASE_TEST(masm_reaches_system_instruction_label_through_cs) {
    // Real MASM 5.10 SYSLABEL.OBJ: with DS:NOTHING, a bare same-segment
    // LGDT label takes a CS override and retains its offset relocation.
    const auto object = assemble("syslabel.asm",
                                 ".286p\n"
                                 "code segment word public 'code'\n"
                                 "assume cs:code,ds:nothing\n"
                                 "table dw 0,0,0\n"
                                 "lgdt fword ptr table\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"0000000000002e0f01160000"});
}

DOSBASE_TEST(masm_folds_same_segment_label_difference_immediate) {
    // RAMDRIVE.OBJ's descriptor selectors are same-segment differences and
    // use MOV-immediate, without a relocation to the first label.
    const auto object = assemble("labdiff.asm",
                                 "code segment word public 'code'\n"
                                 "first db 0,0\n"
                                 "second db 0\n"
                                 "mov ax,second-first\n"
                                 "add di,offset second-offset first\n"
                                 "code ends\n"
                                 "        end\n");
    DOSBASE_TEST_EXPECT_EQ(hex(code_bytes(object)),
                              std::string{"000000b8020081c70200"});
}

DOSBASE_TEST(masm_orders_names_by_the_seed_tables_hash) {
    // LNAMES, EXTDEF, and PUBDEF walk MASM's name table: buckets ascending
    // by sum of the uppercased bytes mod 451, later insertions first within
    // a bucket. Fitted against all 155 reference objects; the 118 EXTDEF
    // records leave 451 as the only modulus (GETCNTRY.OBJ: BUFFER defined
    // first still lands after DOSXXX/DOS because sum("DOSXXX") = 494 wraps
    // to 43).
    const auto object = assemble("names.asm",
                                 "buffer segment word public 'buffer'\n"
                                 "buffer ends\n" +
                                     std::string(kPrologue) + kEpilogue);
    std::vector<std::string> lnames;
    for (const auto& record : parse_omf_records(object, 0, true)) {
        if (record.type != 0x96) continue;
        std::size_t at = 0;
        while (at < record.body.size()) {
            const std::size_t length = record.body[at];
            lnames.emplace_back(record.body.begin() + static_cast<long>(at) + 1,
                                record.body.begin() + static_cast<long>(at) + 1 +
                                    static_cast<long>(length));
            at += 1 + length;
        }
    }
    const std::vector<std::string> expected{"", "CODE", "BUFFER"};
    // sum("CODE") = 288, sum("BUFFER") = 442: CODE's bucket comes first
    // although BUFFER is defined first.
    DOSBASE_TEST_EXPECT_EQ(lnames.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        DOSBASE_TEST_EXPECT_EQ(lnames[i], expected[i]);
    }
}

DOSBASE_TEST(masm_reports_what_it_refuses_by_name) {
    // Unverified behavior is refused by name, never guessed at.
    const std::filesystem::path dir = fixture_dir();
    std::ofstream stream(dir / "bad.asm", std::ios::binary | std::ios::trunc);
    stream << kPrologue << "        bound ax,[bx]\n" << kEpilogue;
    stream.close();
    const std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::current_path(dir);
    const MasmResult result = masm_assemble("bad.asm", {});
    std::filesystem::current_path(previous);
    DOSBASE_TEST_EXPECT(!result.errors.empty());
    DOSBASE_TEST_EXPECT(result.errors.front().find("BOUND") != std::string::npos);
}

DOSBASE_TEST(masm_encodes_386_mul_div_bsr_shld_shrd) {
    const auto object = assemble("fp386.asm",
                                 std::string(kPrologue) +
                                     ".386\n"
                                     "        mul ebx\n"
                                     "        div DWORD PTR [bp-8]\n"
                                     "        test eax, edx\n"
                                     "        bsr edx, eax\n"
                                     "        shld edx, eax, 11\n"
                                     "        shld edx, eax, cl\n"
                                     "        shrd eax, edx, 7\n"
                                     "        shrd eax, edx, cl\n" +
                                     kEpilogue);
    DOSBASE_TEST_EXPECT_EQ(
        hex(code_bytes(object)),
        std::string{"66f7e3"       // mul ebx
                    "66f776f8"     // div dword ptr [bp-8]
                    "6685c2"       // test eax, edx
                    "660fbdd0"     // bsr edx, eax
                    "660fa4c20b"   // shld edx, eax, 11
                    "660fa5c2"     // shld edx, eax, cl
                    "660facd007"   // shrd eax, edx, 7
                    "660fadd0"});  // shrd eax, edx, cl
}
