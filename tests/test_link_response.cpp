// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// LINK's response-file grammar, checked against real `.LNK`
// files the tree's own makefiles pass as `link @foo.lnk` -- the exact
// invocations a real capture recorded as `[tree] 64 LINK link @tree.lnk`,
// `[bios] 83 LINK link @msbio.lnk`, and so on through MSDOS/COMMAND/SYS.
// Every fixture here is real file content (some trimmed to an excerpt,
// never invented), because the grammar's flexibility -- `,` and bare
// newlines both end a field, `+` and whitespace both join list items, a
// switch can sit with no space before it -- only shows up by reading what
// the files actually do, not by guessing at the spec.
#include "test_harness.hpp"

#include <string>
#include <vector>

#include "dosbase/devtools/link_response.hpp"

namespace {
using dosbase::devtools::default_exe_name;
using dosbase::devtools::LinkResponse;
using dosbase::devtools::parse_link_response;
}  // namespace

DOSBASE_TEST(parses_tree_lnk_with_no_exe_field_and_a_trailing_semicolon) {
    // The complete real file: four `+`-joined object names, no exe/map
    // fields at all, terminated by `;`.
    const LinkResponse response = parse_link_response("TREE+\r\nTREEPAR+\r\nTREESYSP+\r\nTREESYSM;\r\n");
    const std::vector<std::string> expected = {"TREE", "TREEPAR", "TREESYSP", "TREESYSM"};
    DOSBASE_TEST_EXPECT_EQ(response.objects, expected);
    DOSBASE_TEST_EXPECT_EQ(response.exe_name, std::string{""});
    DOSBASE_TEST_EXPECT_EQ(response.map_name, std::string{""});
    DOSBASE_TEST_EXPECT(response.switches.empty());
    // LINK's own default when the field is empty: first object + ".EXE" --
    // the real capture of `link @tree.lnk` produced TREE.EXE.
    DOSBASE_TEST_EXPECT_EQ(default_exe_name(response), std::string{"TREE.EXE"});
}

DOSBASE_TEST(parses_sys_lnk_with_three_fields_and_a_switch_on_one_line) {
    // The complete real file: everything on one line, three plus-joined
    // objects, exe and map fields, and a `/m` switch with no space before
    // the terminating `;`.
    const LinkResponse response = parse_link_response("SYS1+SYS2+SYSSR,sys,sys.map /m;\r\n");
    const std::vector<std::string> expected = {"SYS1", "SYS2", "SYSSR"};
    DOSBASE_TEST_EXPECT_EQ(response.objects, expected);
    DOSBASE_TEST_EXPECT_EQ(response.exe_name, std::string{"sys"});
    DOSBASE_TEST_EXPECT_EQ(response.map_name, std::string{"sys.map"});
    const std::vector<std::string> switches = {"/m"};
    DOSBASE_TEST_EXPECT_EQ(response.switches, switches);
}

DOSBASE_TEST(parses_msbio_lnk_with_a_switch_directly_against_the_map_name) {
    // The complete real file: twelve `+`-continued lines (one object per
    // line, no trailing comma), then a final line with all three trailing
    // fields packed together -- `sysimes,msbio,msBIO/M;` -- where the `/M`
    // switch has no space before it at all.
    const LinkResponse response = parse_link_response(
        "msbio1+\r\nmsCON+\r\nmsAUX+\r\nmsLPT+\r\nmsCLOCK+\r\nmsDISK+\r\nmsBIO2+\r\nmshard+\r\n"
        "msinit+\r\nsysinit1+\r\nsysconf+\r\nsysinit2+\r\nsysimes,msbio,msBIO/M;\r\n");
    const std::vector<std::string> expected = {"msbio1", "msCON",  "msAUX",    "msLPT",    "msCLOCK",
                                                "msDISK", "msBIO2", "mshard",   "msinit",   "sysinit1",
                                                "sysconf", "sysinit2", "sysimes"};
    DOSBASE_TEST_EXPECT_EQ(response.objects, expected);
    DOSBASE_TEST_EXPECT_EQ(response.exe_name, std::string{"msbio"});
    DOSBASE_TEST_EXPECT_EQ(response.map_name, std::string{"msBIO"});
    const std::vector<std::string> switches = {"/M"};
    DOSBASE_TEST_EXPECT_EQ(response.switches, switches);
}

DOSBASE_TEST(parses_format_lnk_where_bare_newlines_end_fields_with_no_comma) {
    // The complete real file: `+`-continued objects across six lines, then
    // TWO fields (exe, map) on a single final line -- no comma anywhere
    // separates the object list from "FORMAT.exe", only the newline does.
    const LinkResponse response = parse_link_response(
        "DISPLAY.OBJ+\r\nFORINIT.OBJ+\r\nFORLABEL.OBJ+\r\nFORMAT.OBJ+\r\nFORPROC.OBJ+\r\nMSFOR.OBJ+\r\n"
        "FOREXEC.OBJ\r\nFORMAT.exe,FORMAT.MAP;\r\n");
    const std::vector<std::string> expected = {"DISPLAY.OBJ", "FORINIT.OBJ", "FORLABEL.OBJ",
                                                "FORMAT.OBJ",  "FORPROC.OBJ", "MSFOR.OBJ", "FOREXEC.OBJ"};
    DOSBASE_TEST_EXPECT_EQ(response.objects, expected);
    DOSBASE_TEST_EXPECT_EQ(response.exe_name, std::string{"FORMAT.exe"});
    DOSBASE_TEST_EXPECT_EQ(response.map_name, std::string{"FORMAT.MAP"});
}

DOSBASE_TEST(parses_msdos_lnk_where_every_field_is_its_own_bare_line) {
    // A real excerpt: the object list ends with no comma or `+` (a bare
    // newline ends the field), then the exe name, then the map name with
    // its `/M` switch, each entirely alone on its own line.
    const LinkResponse response = parse_link_response(
        "..\\inc\\nibdos.obj    +\r\n..\\inc\\const2.obj   +\r\n misdata.obj\r\nMSDOS.EXE\r\nMSDOS.MAP /M\r\n;\r\n");
    const std::vector<std::string> expected = {"..\\inc\\nibdos.obj", "..\\inc\\const2.obj", "misdata.obj"};
    DOSBASE_TEST_EXPECT_EQ(response.objects, expected);
    DOSBASE_TEST_EXPECT_EQ(response.exe_name, std::string{"MSDOS.EXE"});
    DOSBASE_TEST_EXPECT_EQ(response.map_name, std::string{"MSDOS.MAP"});
    const std::vector<std::string> switches = {"/M"};
    DOSBASE_TEST_EXPECT_EQ(response.switches, switches);
}

DOSBASE_TEST(parses_command_lnk_where_objects_are_space_separated_with_a_leading_switch) {
    // A real excerpt (COMMAND.LNK's first two continuation lines plus its
    // final line, verbatim): a `/map` switch before any field content,
    // object names separated by spaces rather than `+`, `+`-continuation
    // across the line break, and two empty trailing fields (`,,`).
    const LinkResponse response = parse_link_response(
        "/map COMMAND1.OBJ COMMAND2.OBJ RUCODE.OBJ RDATA.OBJ INIT.OBJ IPARSE.OBJ +\r\n"
        "UINIT.OBJ TCODE.OBJ TBATCH.OBJ,COMMAND.EXE,,;\r\n");
    const std::vector<std::string> expected = {"COMMAND1.OBJ", "COMMAND2.OBJ", "RUCODE.OBJ", "RDATA.OBJ",
                                                "INIT.OBJ",     "IPARSE.OBJ",   "UINIT.OBJ",  "TCODE.OBJ",
                                                "TBATCH.OBJ"};
    DOSBASE_TEST_EXPECT_EQ(response.objects, expected);
    DOSBASE_TEST_EXPECT_EQ(response.exe_name, std::string{"COMMAND.EXE"});
    DOSBASE_TEST_EXPECT_EQ(response.map_name, std::string{""});
    DOSBASE_TEST_EXPECT(response.libraries.empty());
    const std::vector<std::string> switches = {"/map"};
    DOSBASE_TEST_EXPECT_EQ(response.switches, switches);
    // The field was explicitly given, so the default rule must not apply.
    DOSBASE_TEST_EXPECT_EQ(default_exe_name(response), std::string{"COMMAND.EXE"});
}
