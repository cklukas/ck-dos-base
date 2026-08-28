// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include "dosbase/devtools/nmake_macros.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dosbase::devtools {

// NMAKE 1.00.05's other half of the grammar: explicit target rules and the
// dependency-graph evaluation they feed. nmake_macros.hpp reads
// macros and inference-rule *templates*; this layer reads the dependency
// lists NMAKE actually decides staleness from, and applies both an
// explicit recipe and, when a target rule has none of its own, a single
// inference-rule hop -- the shape every target in the tree's own
// SRC/CMD/TREE/MAKEFILE uses (`tree.obj: tree.asm ...` names its
// dependencies and leaves `.asm.obj:` to supply the recipe).

struct TargetRule {
    std::string target;                     // as declared, e.g. "tree.obj"
    std::vector<std::string> dependencies;   // macro-expanded, whitespace-split
    std::vector<std::string> recipe;         // command lines, macros not yet expanded
};

// One makefile's explicit target rules, plus the macro/inference-rule
// context (typically TOOLS.INI merged with the makefile's own local
// macros) they were read against.
struct Makefile {
    MacroFile macros;
    std::vector<TargetRule> targets;
};

// Parses explicit target rules out of already-joined lines (see
// join_continuations()). Macro definitions and `.suffix.suffix:` inference
// rules are recognized and skipped -- parse_macro_file() reads those --
// leaving `name: dep dep ...` headers and their tab-indented recipe lines.
// Dependency lists are macro-expanded against `macros` as they are parsed,
// matching a makefile whose own local macros (already folded into
// `macros`) are in scope by the time its target rules appear.
[[nodiscard]] std::vector<TargetRule> parse_target_rules(
    const std::vector<std::string>& lines, const MacroFile& macros);

struct PlannedCommand {
    std::string target;
    std::string command;  // fully macro- and $*-expanded
};

// Returns a path's modification stamp (opaque, ordered only), or nullopt if
// the path does not exist on the tree state being planned against --
// matching what a FAT listing diff gives the real comparison harness.
using MTimeOracle = std::function<std::optional<std::int64_t>(std::string_view)>;

// Plans one target the way `NMAKE /N` decides it from a given tree state:
// out of date if the target does not exist, if any dependency is newer, or
// if a dependency itself had to be rebuilt to satisfy this call. Recurses
// into any dependency that is itself a declared target; a dependency with
// no target rule is a leaf (a source file, expected to already exist).
//
// What this deliberately does not implement -- real gaps, not
// simplifications passed off as complete: inference-rule chains longer
// than one hop; searching all declared .SUFFIXES for a match rather than
// only the dependencies already listed; double-colon rules; response
// files; and the driver-level concerns run_module() already owns in
// tools/bootstrap/capture_reference.py -- nested `NMAKE` recipe lines and
// re-planning convergence for dependency-less recipe targets.
[[nodiscard]] std::vector<PlannedCommand> plan_target(
    const std::string& target, const Makefile& makefile, const MTimeOracle& mtime);

}  // namespace dosbase::devtools
