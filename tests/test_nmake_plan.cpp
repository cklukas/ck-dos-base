// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// NMAKE 1.00.05's dependency-graph evaluator, checked against
// real fixtures: an excerpt of TOOLS.INI's built-in rules (the same one
// test_nmake_macros.cpp uses) merged with SRC/CMD/TREE/MAKEFILE's own
// local macros and target rules, and the exact command lines a real
// capture recorded for TREE.COM's build (the recorded compatibility corpus) --
// `[tree-pre] 59 BUILDMSG buildmsg ..\..\messages\usa-ms tree.skl`,
// `[tree] 60 MASM masm -Mx -t  -I. -I..\..\inc -I..\..\dos
// tree.asm,tree.obj;`, and so on through the final LINK/EXE2BIN/DEL.
#include "test_harness.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "dosbase/devtools/nmake_macros.hpp"
#include "dosbase/devtools/nmake_plan.hpp"

namespace {
using dosbase::devtools::join_continuations;
using dosbase::devtools::Makefile;
using dosbase::devtools::MacroFile;
using dosbase::devtools::parse_macro_file;
using dosbase::devtools::parse_target_rules;
using dosbase::devtools::plan_target;
using dosbase::devtools::PlannedCommand;

const std::string kToolsIni =
    "[nmake]\r\n"
    "\r\n"
    "asm\t=masm\r\n"
    "aflags\t=-Mx -t $(extasw)\r\n"
    "ainc\t=-I. -I$(inc) -I$(dos)\r\n"
    "\r\n"
    "cc\t=cl\r\n"
    "cflags\t=-AS -Os -Zp $(extcsw)\r\n"
    "cinc\t=-I. -I$(hinc)\r\n"
    "\r\n"
    "link\t=link\r\n"
    "\r\n"
    ".SUFFIXES:\r\n"
    ".SUFFIXES: .c .obj .lst .exe .com .cod .inc .skl .cl1 .ctl .asm .idx .msg\r\n"
    "\r\n"
    ".asm.obj:\r\n"
    "\t$(asm) $(aflags) $(ainc) $*.asm,$*.obj;\r\n"
    "\r\n"
    ".skl.ctl:\r\n"
    "\tbuildmsg $(msg)\\$(COUNTRY) $*.skl\r\n";

// A faithful excerpt of third_party/ms-dos-4/src/CMD/TREE/MAKEFILE.
const std::string kTreeMakefile =
    "msg\t=..\\..\\messages\r\n"
    "dos\t=..\\..\\dos\r\n"
    "inc\t=..\\..\\inc\r\n"
    "hinc\t=..\\..\\h\r\n"
    "\r\n"
    "all: tree.com\r\n"
    "\r\n"
    "tree.ctl: tree.skl \\\r\n"
    "    $(msg)\\$(COUNTRY).msg \\\r\n"
    "    makefile\r\n"
    "\r\n"
    "tree.obj: tree.asm \\\r\n"
    "    makefile \\\r\n"
    "    treequ.inc\r\n"
    "\r\n"
    "treesysp.obj: treesysp.asm \\\r\n"
    "    makefile\r\n"
    "\r\n"
    "treesysm.obj: treesysm.asm \\\r\n"
    "    makefile \\\r\n"
    "    tree.ctl \\\r\n"
    "    tree.cl1 \\\r\n"
    "    tree.cl2 \\\r\n"
    "    tree.cla\r\n"
    "\r\n"
    "treepar.obj: treepar.asm \\\r\n"
    "    makefile\r\n"
    "\r\n"
    "tree.com: tree.obj \\\r\n"
    "    makefile \\\r\n"
    "    tree.lnk \\\r\n"
    "    treesysp.obj \\\r\n"
    "    treesysm.obj \\\r\n"
    "    treepar.obj\r\n"
    "\tlink @tree.lnk\r\n"
    "\texe2bin tree.exe tree.com\r\n"
    "\tdel tree.exe\r\n";

// Builds the merged Makefile (TOOLS.INI's rules + TREE's local macros and
// target rules) real NMAKE would see when run inside SRC\CMD\TREE, with
// COUNTRY folded in from the guest environment the way a makefile macro
// reference falls back to it.
Makefile tree_makefile() {
    MacroFile macros = parse_macro_file(join_continuations(kToolsIni));
    const MacroFile local = parse_macro_file(join_continuations(kTreeMakefile));
    for (const auto& [name, value] : local.macros) macros.macros[name] = value;
    macros.macros["COUNTRY"] = "usa-ms";
    Makefile makefile;
    makefile.targets = parse_target_rules(join_continuations(kTreeMakefile), macros);
    makefile.macros = std::move(macros);
    return makefile;
}

// Every fixture path "exists" except the ones under test, matching a tree
// state where everything upstream of the target in question has already
// been built -- the same granularity the real capture recorded across its
// split [tree-pre]/[tree] phases.
struct FakeTree {
    std::set<std::string> missing;  // upper-cased
    std::optional<std::int64_t> operator()(std::string_view path) const {
        std::string upper(path);
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (missing.count(upper)) return std::nullopt;
        return std::int64_t{1000};
    }
};

}  // namespace

DOSBASE_TEST(parse_target_rules_reads_tree_makefile_dependencies_and_recipes) {
    const Makefile makefile = tree_makefile();
    DOSBASE_TEST_EXPECT_EQ(makefile.targets.size(), std::size_t{7});
    const auto& com = makefile.targets.back();
    DOSBASE_TEST_EXPECT_EQ(com.target, std::string{"tree.com"});
    DOSBASE_TEST_EXPECT_EQ(com.recipe.size(), std::size_t{3});
    DOSBASE_TEST_EXPECT_EQ(com.recipe[0], std::string{"link @tree.lnk"});
    DOSBASE_TEST_EXPECT_EQ(com.dependencies.size(), std::size_t{6});
    DOSBASE_TEST_EXPECT_EQ(com.dependencies[0], std::string{"tree.obj"});
}

DOSBASE_TEST(plan_target_synthesizes_buildmsg_from_the_skl_ctl_inference_rule) {
    // Real ground truth: `[tree-pre] 59 BUILDMSG buildmsg ..\..\messages\usa-ms tree.skl`.
    const Makefile makefile = tree_makefile();
    FakeTree tree;
    tree.missing = {"TREE.CTL"};
    const auto plan = plan_target("tree.ctl", makefile, tree);
    DOSBASE_TEST_EXPECT_EQ(plan.size(), std::size_t{1});
    DOSBASE_TEST_EXPECT_EQ(plan[0].command, std::string{"buildmsg ..\\..\\messages\\usa-ms tree.skl"});
}

DOSBASE_TEST(plan_target_synthesizes_masm_from_the_asm_obj_inference_rule) {
    // Real ground truth: `[tree] 60 MASM masm -Mx -t  -I. -I..\..\inc -I..\..\dos tree.asm,tree.obj;`.
    const Makefile makefile = tree_makefile();
    FakeTree tree;
    tree.missing = {"TREE.OBJ"};
    const auto plan = plan_target("tree.obj", makefile, tree);
    DOSBASE_TEST_EXPECT_EQ(plan.size(), std::size_t{1});
    DOSBASE_TEST_EXPECT_EQ(
        plan[0].command,
        std::string{"masm -Mx -t  -I. -I..\\..\\inc -I..\\..\\dos tree.asm,tree.obj;"});
}

DOSBASE_TEST(plan_target_uses_tree_coms_own_explicit_recipe_when_only_it_is_stale) {
    // Real ground truth: invocations 64-66, LINK then EXE2BIN then DEL, in
    // that order, once every object file and tree.ctl already exist.
    const Makefile makefile = tree_makefile();
    FakeTree tree;
    tree.missing = {"TREE.COM"};
    const auto plan = plan_target("tree.com", makefile, tree);
    DOSBASE_TEST_EXPECT_EQ(plan.size(), std::size_t{3});
    DOSBASE_TEST_EXPECT_EQ(plan[0].command, std::string{"link @tree.lnk"});
    DOSBASE_TEST_EXPECT_EQ(plan[1].command, std::string{"exe2bin tree.exe tree.com"});
    DOSBASE_TEST_EXPECT_EQ(plan[2].command, std::string{"del tree.exe"});
}

DOSBASE_TEST(plan_target_is_empty_once_everything_is_up_to_date) {
    const Makefile makefile = tree_makefile();
    FakeTree tree;  // nothing missing
    const auto plan = plan_target("tree.com", makefile, tree);
    DOSBASE_TEST_EXPECT(plan.empty());
}

DOSBASE_TEST(plan_target_rebuilds_dependents_of_a_rebuilt_dependency) {
    // tree.asm changed (newer than everything, including tree.com) but
    // nothing is literally "missing": tree.obj and then tree.com must both
    // be recognized as stale by propagation, not by their own absence.
    const Makefile makefile = tree_makefile();
    struct Newer {
        std::optional<std::int64_t> operator()(std::string_view path) const {
            std::string upper(path);
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (upper == "TREE.ASM") return std::int64_t{5000};
            return std::int64_t{1000};
        }
    };
    const auto plan = plan_target("tree.com", makefile, Newer{});
    DOSBASE_TEST_EXPECT_EQ(plan.size(), std::size_t{4});
    DOSBASE_TEST_EXPECT_EQ(plan[0].target, std::string{"tree.obj"});
    DOSBASE_TEST_EXPECT_EQ(
        plan[0].command,
        std::string{"masm -Mx -t  -I. -I..\\..\\inc -I..\\..\\dos tree.asm,tree.obj;"});
    DOSBASE_TEST_EXPECT_EQ(plan[1].target, std::string{"tree.com"});
    DOSBASE_TEST_EXPECT_EQ(plan[1].command, std::string{"link @tree.lnk"});
}
