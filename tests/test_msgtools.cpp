// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// The DOS 4 message toolchain's replacements. Every expectation
// below is measured against the seed BUILDIDX/NOSRVBLD/BUILDMSG through the
// six invocations the pinned tree issues, captured in
// tools/bootstrap/transcript.json; the E1 gate replays those invocations
// byte-for-byte, and these fixtures pin each measured rule in isolation.
#include "test_harness.hpp"

#include <string>

#include "dosbase/devtools/msgtools.hpp"

namespace {
using dosbase::devtools::BuildIdxResult;
using dosbase::devtools::BuildMsgResult;
using dosbase::devtools::MsgDatabase;
using dosbase::devtools::buildidx;
using dosbase::devtools::buildmsg;
using dosbase::devtools::nosrvbld;
using dosbase::devtools::parse_msg;

// A miniature database in the real file's shape: hex level, section headers
// with hex offset and DECIMAL count, a tab continuation, a message whose
// text ends without CR,LF (the EXTEND/PARSE style), and one with it.
const std::string kMsg =
    "0009\n"
    "COMMON   0006 0002\n"
    "0001 U 0000 \"With newline\",CR,LF\n"
    "0002 U 0000 \"Split\",CR,LF\n"
    "\t\"over two lines\",13,10,0\n"
    "EXTEND   0050 0001\n"
    "0007 U 0000 \"Bare text\"\n"
    "DEMO     0070 0001\n"
    "0004 U 0000 \"From the database\",CR,LF\n";

MsgDatabase database() { return parse_msg(kMsg); }
}  // namespace

DOSBASE_TEST(msg_parser_reads_sections_numbers_and_continuations) {
    const MsgDatabase msg = database();
    DOSBASE_TEST_EXPECT_EQ(msg.level, std::string{"0009"});
    DOSBASE_TEST_EXPECT_EQ(msg.sections.size(), std::size_t{3});
    const auto* split = msg.find("common", 2);
    DOSBASE_TEST_EXPECT(split != nullptr);
    DOSBASE_TEST_EXPECT_EQ(split->lines.size(), std::size_t{2});
    // The continuation keeps its leading tab: both consumers copy it into
    // their output verbatim (COMMAND 464 is the reference's witness).
    DOSBASE_TEST_EXPECT_EQ(split->lines[1], std::string{"\t\"over two lines\",13,10,0"});
}

DOSBASE_TEST(buildidx_bumps_the_level_in_hex_and_rewrites_headers) {
    const BuildIdxResult result = buildidx(kMsg);
    // 0009 + 1 carries into hex 000a; every line gains CRLF; each header is
    // restamped with its offset in the REWRITTEN file (lowercase hex) and
    // its message count in zero-padded decimal.
    DOSBASE_TEST_EXPECT_EQ(result.message_file.substr(0, 6), std::string{"000a\r\n"});
    DOSBASE_TEST_EXPECT(result.message_file.find("COMMON   0006 0002\r\n") !=
                           std::string::npos);
    DOSBASE_TEST_EXPECT(result.message_file.find("EXTEND   0072 0001\r\n") !=
                           std::string::npos);
    // The index is the level plus the header lines, nothing else.
    DOSBASE_TEST_EXPECT_EQ(result.index_file,
                              std::string{"000a\r\n"
                                          "COMMON   0006 0002\r\n"
                                          "EXTEND   0072 0001\r\n"
                                          "DEMO     009f 0001\r\n"});
}

DOSBASE_TEST(buildidx_counts_in_decimal_not_hex) {
    std::string msg = "0001\nBULK     0006 0000\n";
    for (int i = 1; i <= 12; ++i) {
        char line[64];
        std::snprintf(line, sizeof line, "%04d U 0000 \"x\"\n", i);
        msg += line;
    }
    const BuildIdxResult result = buildidx(msg);
    // Twelve messages stamp 0012, not 000c: four decimal digits, measured
    // off COMMON's 38 messages stamped "0038" where hex would say 26.
    DOSBASE_TEST_EXPECT(result.index_file.find("BULK     0006 0012\r\n") !=
                           std::string::npos);
}

DOSBASE_TEST(nosrvbld_renders_use_and_def_through_the_database) {
    const std::string skl =
        ":class 1\r\n"
        ":use 001 COMMON FIRST\r\n"
        ":class 3\r\n"
        ";just a comment\r\n"
        ":def 04 LOCAL\tDB\t\"documentation only, not what is emitted\"\r\n"
        ":end\r\n";
    const auto files = nosrvbld(skl, "demo", database());
    DOSBASE_TEST_EXPECT_EQ(files.size(), std::size_t{2});
    DOSBASE_TEST_EXPECT_EQ(files[0].first, std::string{"1"});
    DOSBASE_TEST_EXPECT_EQ(files[0].second,
                              std::string{"; demo.cl1 \r\n"
                                          "\r\n"
                                          "\r\n"
                                          ";_______________________\r\n"
                                          "\r\n"
                                          "FIRST DB    \"With newline\",CR,LF\r\n"});
    // The :def resolves DEMO 4 from the database; its inline text is
    // documentation (measured: MSBIO 12/16/21 differ between skeleton and
    // database, and the reference outputs carry the database's words).
    DOSBASE_TEST_EXPECT_EQ(files[1].second,
                              std::string{"; demo.cl3 \r\n"
                                          "\r\n"
                                          "\r\n"
                                          ";_______________________\r\n"
                                          "\r\n"
                                          "LOCAL DB    \"From the database\",CR,LF\r\n"});
}

DOSBASE_TEST(nosrvbld_keeps_the_continuation_tab_after_its_db) {
    const std::string skl = ":class 1\r\n:use 002 COMMON PAIR\r\n:end\r\n";
    const auto files = nosrvbld(skl, "demo", database());
    // `\tDB   ` plus the continuation verbatim, whose own tab supplies the
    // separator -- BOOT.CL1's second line in the reference build.
    DOSBASE_TEST_EXPECT(files[0].second.find(
                               "PAIR DB    \"Split\",CR,LF\r\n"
                               "\tDB   \t\"over two lines\",13,10,0\r\n") != std::string::npos);
}

namespace {
const std::string kServerSkl =
    ":util DEMO\r\n"
    ":class 1\r\n"
    ":use EXTEND7\t\t; two-argument form: id = source number\r\n"
    ":class 3\r\n"
    ":use -1 EXTEND999\t; resident sentinel, lookup fails -> builtin\r\n"
    ":class A\r\n"
    ":use 9 EXTEND7\t\t; three-argument form appends CR,LF\r\n"
    ":def 4 \"doc only\"\t; resolves DEMO 4\r\n"
    ":end\r\n";
}  // namespace

DOSBASE_TEST(buildmsg_transient_and_resident_classes_differ_throughout) {
    const BuildMsgResult result = buildmsg(kServerSkl, database());
    DOSBASE_TEST_EXPECT_EQ(result.util, std::string{"DEMO"});
    // The transient pair exists in every output (measured on MORE.OBJ):
    // the undeclared class 2 is prepended, so the order is CL2, CL1, CL3,
    // CLA, CTL.
    DOSBASE_TEST_EXPECT_EQ(result.files[0].first, std::string{"CL2"});
    const std::string& cl1 = result.files[1].second;
    const std::string& cl3 = result.files[2].second;
    const std::string& cla = result.files[3].second;
    // Class 1 is transient: $M_MSGSERV_1, class byte 001H, lowercase Endp.
    DOSBASE_TEST_EXPECT(cl1.find("        PUBLIC  $M_MSGSERV_1\r\n") != std::string::npos);
    DOSBASE_TEST_EXPECT(cl1.find("<001H,EXPECTED_VERSION,") != std::string::npos);
    DOSBASE_TEST_EXPECT(cl1.find("$M_MSGSERV_1 Endp\r\n") != std::string::npos);
    // Classes 3 and A are resident: $M_CLS_<i> numbered across the resident
    // classes only, class byte 0FFH even for the digit class (measured off
    // COMMAND.CL3/CL4), uppercase ENDP.
    DOSBASE_TEST_EXPECT(cl3.find("        PUBLIC  $M_CLS_1\r\n") != std::string::npos);
    DOSBASE_TEST_EXPECT(cl3.find("<0FFH,EXPECTED_VERSION,") != std::string::npos);
    DOSBASE_TEST_EXPECT(cl3.find("$M_CLS_1 ENDP\r\n") != std::string::npos);
    DOSBASE_TEST_EXPECT(cla.find("        PUBLIC  $M_CLS_2\r\n") != std::string::npos);
}

DOSBASE_TEST(buildmsg_sentinels_implicit_style_and_explicit_style) {
    const BuildMsgResult result = buildmsg(kServerSkl, database());
    const std::string& cl1 = result.files[1].second;
    const std::string& cl3 = result.files[2].second;
    // The transient class gains the implicit sentinel: $M_1_FF labels, a
    // single space after $M_ID, the builtin text without CR,LF, and the
    // sentinel joins the count.
    DOSBASE_TEST_EXPECT(cl1.find("        $M_ID <0FFFFH,$M_1_FF_MSG-$M_1_FF_STRUC>") !=
                           std::string::npos);
    DOSBASE_TEST_EXPECT(cl1.find("        DB      \"Extended Error %1\"\r\n") !=
                           std::string::npos);
    DOSBASE_TEST_EXPECT(cl1.find("Class_1_MessageCount EQU     2\r\n") != std::string::npos);
    // The resident class renders `:use -1` as an ordinary entry with id
    // 0FFFFH -- three spaces after $M_ID -- whose failed lookup falls back
    // to the builtin and gains ,CR,LF through the ordinary append rule.
    DOSBASE_TEST_EXPECT(cl3.find("        $M_ID   <0FFFFH,$M_3_0FFFFH_MSG-"
                                    "$M_3_0FFFFH_STRUC>") != std::string::npos);
    DOSBASE_TEST_EXPECT(cl3.find("        DB      \"Extended Error %1\",CR,LF\r\n") !=
                           std::string::npos);
    DOSBASE_TEST_EXPECT(cl3.find("Class_3_MessageCount EQU     1\r\n") != std::string::npos);
    // No implicit sentinel in a resident class.
    DOSBASE_TEST_EXPECT(cl3.find("$M_3_FF_STRUC") == std::string::npos);
}

DOSBASE_TEST(buildmsg_three_argument_use_appends_crlf_only_when_missing) {
    const BuildMsgResult result = buildmsg(kServerSkl, database());
    const std::string& cl1 = result.files[1].second;
    const std::string& cla = result.files[3].second;
    // Two-argument :use keeps the bare text (COMMAND.CLD's EXTEND uses).
    DOSBASE_TEST_EXPECT(cl1.find("        DB      \"Bare text\"\r\n") != std::string::npos);
    // Three-argument :use appends ,CR,LF to the same message (COMMAND.CLA's
    // `:use 230 EXTEND5`); a :def whose database text already ends in CR,LF
    // is left alone.
    DOSBASE_TEST_EXPECT(cla.find("        DB      \"Bare text\",CR,LF\r\n") !=
                           std::string::npos);
    DOSBASE_TEST_EXPECT(cla.find("        DB      \"From the database\",CR,LF\r\n") !=
                           std::string::npos);
    DOSBASE_TEST_EXPECT(cla.find("CR,LF,CR,LF") == std::string::npos);
}

DOSBASE_TEST(buildmsg_ctl_counts_the_resident_classes) {
    const BuildMsgResult result = buildmsg(kServerSkl, database());
    // SYS: six classes, four resident, `$M_NUM_CLS  EQU 4`; COMMAND: ten
    // and eight. Here: classes 1, 3, A -> two resident.
    DOSBASE_TEST_EXPECT_EQ(result.files.back().first, std::string{"CTL"});
    DOSBASE_TEST_EXPECT_EQ(result.files.back().second,
                              std::string{"$M_NUM_CLS  EQU 2\r\n"});
}

DOSBASE_TEST(buildmsg_comments_are_stripped_outside_quotes_only) {
    // A ';' inside either quote kind stays; the trailing comment goes. The
    // tree's witnesses: COMMAND.SKL's `;AC000;` tails and MSBIO's quoted
    // dollar strings.
    const std::string skl =
        ":util DEMO\r\n"
        ":class A\r\n"
        ":use 9 EXTEND7 ;AC000; trailing comment\r\n"
        ":end\r\n";
    const BuildMsgResult result = buildmsg(skl, database());
    // files: CL2, CL1 (implicit transient pair), then the declared CLA.
    DOSBASE_TEST_EXPECT(result.files[2].second.find("\"Bare text\",CR,LF") !=
                           std::string::npos);
}
