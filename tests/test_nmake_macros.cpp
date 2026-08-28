// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// NMAKE 1.00.05's macro and inference-rule grammar. The fixtures
// below are excerpts of the real TOOLS.INI and the tree's own makefiles;
// the parser is exercised against the shapes it actually has to read, not
// invented ones.
#include "test_harness.hpp"

#include <string>
#include <vector>

#include "dosbase/devtools/nmake_macros.hpp"

namespace {
using dosbase::devtools::expand_macros;
using dosbase::devtools::join_continuations;
using dosbase::devtools::MacroFile;
using dosbase::devtools::parse_macro_file;
}  // namespace

DOSBASE_TEST(join_continuations_merges_backslash_lines_and_drops_comments) {
    const std::string text =
        "# a comment line, dropped entirely\r\n"
        "msg\t=..\\..\\messages\r\n"
        "tree.ctl: tree.skl \\\r\n"
        "    $(msg)\\$(COUNTRY).msg \\\r\n"
        "    makefile\r\n"
        "\r\n"
        "tree.obj: tree.asm\r\n";
    const auto lines = join_continuations(text);
    DOSBASE_TEST_EXPECT_EQ(lines.size(), std::size_t{5});
    DOSBASE_TEST_EXPECT_EQ(lines[0], std::string{"msg\t=..\\..\\messages"});
    DOSBASE_TEST_EXPECT(lines[1].find("tree.skl") != std::string::npos);
    DOSBASE_TEST_EXPECT(lines[1].find("$(msg)\\$(COUNTRY).msg") != std::string::npos);
    DOSBASE_TEST_EXPECT(lines[1].find("makefile") != std::string::npos);
    DOSBASE_TEST_EXPECT_EQ(lines[2], std::string{""});
    DOSBASE_TEST_EXPECT_EQ(lines[3], std::string{"tree.obj: tree.asm"});
}

DOSBASE_TEST(parse_macro_file_reads_tools_ini_macros_suffixes_and_rules) {
    // A faithful excerpt of third_party/ms-dos-4/src/TOOLS/TOOLS.INI.
    const std::string text =
        "[nmake]\r\n"
        "\r\n"
        "asm\t=masm\r\n"
        "aflags\t=-Mx -t $(extasw)\r\n"
        "ainc\t=-I. -I$(inc) -I$(dos)\r\n"
        "\r\n"
        ".SUFFIXES:\r\n"
        ".SUFFIXES: .c .obj .asm .skl .cl1 .ctl .msg .idx\r\n"
        "\r\n"
        ".asm.obj:\r\n"
        "\t$(asm) $(aflags) $(ainc) $*.asm,$*.obj;\r\n"
        "\r\n"
        ".msg.idx:\r\n"
        "        attrib -R $*.msg\r\n"
        "\tbuildidx $*.msg\r\n"
        "        attrib +R $*.msg\r\n";
    const auto lines = join_continuations(text);
    const MacroFile file = parse_macro_file(lines);

    DOSBASE_TEST_EXPECT_EQ(file.macros.size(), std::size_t{3});
    DOSBASE_TEST_EXPECT_EQ(file.macros.at("ASM"), std::string{"masm"});
    DOSBASE_TEST_EXPECT_EQ(file.macros.at("AFLAGS"), std::string{"-Mx -t $(extasw)"});

    DOSBASE_TEST_EXPECT_EQ(file.suffixes.size(), std::size_t{8});
    DOSBASE_TEST_EXPECT_EQ(file.suffixes[0], std::string{".c"});

    DOSBASE_TEST_EXPECT_EQ(file.rules.size(), std::size_t{2});
    DOSBASE_TEST_EXPECT_EQ(file.rules[0].from_suffix, std::string{".asm"});
    DOSBASE_TEST_EXPECT_EQ(file.rules[0].to_suffix, std::string{".obj"});
    DOSBASE_TEST_EXPECT_EQ(file.rules[0].recipe.size(), std::size_t{1});
    // .msg.idx has three recipe lines, the shape that first exposed the
    // need to keep collecting lines until a blank one ends the rule.
    DOSBASE_TEST_EXPECT_EQ(file.rules[1].from_suffix, std::string{".msg"});
    DOSBASE_TEST_EXPECT_EQ(file.rules[1].recipe.size(), std::size_t{3});
    DOSBASE_TEST_EXPECT_EQ(file.rules[1].recipe[1], std::string{"buildidx $*.msg"});
}

DOSBASE_TEST(parse_macro_file_reads_a_tree_makefiles_local_macros) {
    // third_party/ms-dos-4/src/CMD/TREE/MAKEFILE's own header macros.
    const std::string text =
        "msg\t=..\\..\\messages\r\n"
        "dos\t=..\\..\\dos\r\n"
        "inc\t=..\\..\\inc\r\n"
        "hinc\t=..\\..\\h\r\n"
        "\r\n"
        "all: tree.com\r\n"
        "\r\n"
        "tree.ctl: tree.skl \\\r\n"
        "    $(msg)\\$(COUNTRY).msg \\\r\n"
        "    makefile\r\n";
    const MacroFile file = parse_macro_file(join_continuations(text));
    DOSBASE_TEST_EXPECT_EQ(file.macros.size(), std::size_t{4});
    DOSBASE_TEST_EXPECT_EQ(file.macros.at("MSG"), std::string{"..\\..\\messages"});
    // "all: tree.com" and "tree.ctl: ..." are explicit target rules, not
    // macros or inference rules -- correctly left unparsed by this layer.
    DOSBASE_TEST_EXPECT_EQ(file.rules.size(), std::size_t{0});
}

DOSBASE_TEST(expand_macros_substitutes_recursively_and_leaves_undefined_ones_empty) {
    MacroFile file;
    file.macros["ASM"] = "masm";
    file.macros["EXTASW"] = "";  // present but empty, as most makefiles leave it
    file.macros["AFLAGS"] = "-Mx -t $(extasw)";
    // extasw is genuinely undefined in most of the tree's makefiles, and
    // NMAKE expands an undefined macro to nothing rather than erroring.
    DOSBASE_TEST_EXPECT_EQ(expand_macros("$(asm) $(aflags)", file),
                              std::string{"masm -Mx -t "});
    DOSBASE_TEST_EXPECT_EQ(expand_macros("$(unknown)", file), std::string{""});
}

DOSBASE_TEST(expand_macros_expands_every_reference_on_one_line) {
    MacroFile file;
    file.macros["MSG"] = "..\\..\\messages";
    file.macros["COUNTRY"] = "usa-ms";
    DOSBASE_TEST_EXPECT_EQ(expand_macros("$(msg)\\$(country).msg", file),
                              std::string{"..\\..\\messages\\usa-ms.msg"});
}

DOSBASE_TEST(expand_macros_leaves_the_automatic_stem_macro_alone) {
    // $* is NMAKE's inference-rule "matched stem" variable, a different
    // piece of grammar the dependency-graph evaluator resolves per target;
    // this layer only expands `$(NAME)` macros.
    MacroFile file;
    file.macros["ASM"] = "masm";
    DOSBASE_TEST_EXPECT_EQ(expand_macros("$(asm) $*.asm,$*.obj;", file),
                              std::string{"masm $*.asm,$*.obj;"});
}
