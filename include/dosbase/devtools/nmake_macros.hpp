// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dosbase::devtools {

// NMAKE 1.00.05's macro and inference-rule grammar, as the tree's
// own `TOOLS.INI` and every makefile use it: `NAME = value` definitions,
// `\`-continued lines, `$(NAME)` expansion (case-insensitive, matching DOS
// convention), and `.suffix1.suffix2:` inference rules whose recipe fires
// whenever a target of the second suffix is built from a same-named file of
// the first and no explicit rule says otherwise -- the mechanism behind
// `.skl.ctl: buildmsg $(msg)\$(COUNTRY) $*.skl`, which is why a makefile can
// declare `tree.ctl:`'s dependencies with no recipe of its own.

struct InferenceRule {
    std::string from_suffix;  // ".asm"
    std::string to_suffix;    // ".obj"
    std::vector<std::string> recipe;  // command lines, macros not yet expanded
};

// A makefile or TOOLS.INI section reduced to its macros and inference rules.
// Explicit target rules (dependency lists with recipes) are NMAKE's other
// half and belong to the dependency-graph evaluator this layer feeds, not
// to this parser.
struct MacroFile {
    std::map<std::string, std::string, std::less<>> macros;  // uppercase keys
    std::vector<std::string> suffixes;   // declared by `.SUFFIXES: .c .obj ...`
    std::vector<InferenceRule> rules;
};

// Joins `\`-continued physical lines into logical ones and strips comment
// lines (`#...`), matching how the tree's makefiles and TOOLS.INI are
// written. Blank lines are preserved as empty strings so callers can still
// use them as section breaks.
[[nodiscard]] std::vector<std::string> join_continuations(std::string_view text);

// Parses macro definitions and inference rules out of already-joined lines.
// Explicit target rules (anything with a `:` that isn't a `.suffix.suffix:`
// or a macro `=`) are skipped, not rejected: this parser reads the half of
// the grammar it implements and leaves the rest for the dependency-graph
// evaluator to read from the same lines.
[[nodiscard]] MacroFile parse_macro_file(const std::vector<std::string>& lines);

// Expands every `$(NAME)` reference in `text` against `macros`, recursively
// (a macro's value can itself reference other macros, as `aflags = -Mx -t
// $(extasw)` does), matching case-insensitively. An undefined macro expands
// to nothing, which is NMAKE's own behavior, not an error: `extasw` is
// deliberately absent from most makefiles and expands empty on purpose.
[[nodiscard]] std::string expand_macros(std::string_view text, const MacroFile& file);

}  // namespace dosbase::devtools
