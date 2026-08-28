// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/nmake_macros.hpp"

#include "dosbase/devtools/binary.hpp"

#include <algorithm>
#include <cctype>

namespace dosbase::devtools {
namespace {

std::string upper(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

bool is_macro_definition(std::string_view line, std::string_view& name, std::string_view& value) {
    // A macro definition is `NAME = value` or `NAME=value`; NMAKE requires
    // the name to precede the FIRST `=` on the line, and a target rule's `:`
    // (if any) would appear after it, so a bare equals-scan is sufficient --
    // target rules never contain `=` before their own `:`.
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) return false;
    const std::string_view candidate = trim(line.substr(0, equals));
    if (candidate.empty()) return false;
    for (const char character : candidate) {
        if (std::isspace(static_cast<unsigned char>(character))) return false;
        if (character == ':' || character == '$' || character == '(' || character == ')') return false;
    }
    name = candidate;
    value = trim(line.substr(equals + 1));
    return true;
}

bool is_suffix_rule_header(std::string_view line, std::string& from, std::string& to) {
    // `.asm.obj:` or the bare declaration `.SUFFIXES:` / `.SUFFIXES: .c .obj`.
    if (line.empty() || line.front() != '.') return false;
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) return false;
    const std::string_view head = line.substr(0, colon);
    // Two dot-led suffixes back to back, e.g. ".asm.obj" -> from=".asm" to=".obj".
    const std::size_t second_dot = head.find('.', 1);
    if (second_dot == std::string_view::npos || second_dot == 0) return false;
    from = std::string(head.substr(0, second_dot));
    to = std::string(head.substr(second_dot));
    return !from.empty() && !to.empty() && from != ".SUFFIXES" &&
           to.find(' ') == std::string::npos;
}

}  // namespace

std::vector<std::string> join_continuations(std::string_view text) {
    std::vector<std::string> lines;
    std::string pending;
    bool continuing = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view raw = end == std::string_view::npos ? text.substr(start)
                                                              : text.substr(start, end - start);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        if (!continuing && !raw.empty() && raw.front() == '#') {
            // A comment line, dropped entirely (never continued: TOOLS.INI
            // and the tree's makefiles never break a comment across lines).
        } else {
            const bool continues = !raw.empty() && raw.back() == '\\';
            pending += continues ? std::string(raw.substr(0, raw.size() - 1)) : std::string(raw);
            if (continues) {
                pending.push_back(' ');
                continuing = true;
            } else {
                lines.push_back(pending);
                pending.clear();
                continuing = false;
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    if (!pending.empty()) lines.push_back(pending);
    return lines;
}

MacroFile parse_macro_file(const std::vector<std::string>& lines) {
    MacroFile file;
    InferenceRule* active_rule = nullptr;
    for (const std::string& raw : lines) {
        const std::string_view line = raw;
        const std::string_view trimmed = trim(line);
        if (trimmed.empty()) {
            active_rule = nullptr;
            continue;
        }
        // A recipe line for the most recently opened inference rule: NMAKE
        // recognizes it by leading whitespace (a tab, conventionally) on the
        // ORIGINAL (untrimmed) line, the same rule explicit target recipes
        // follow.
        if (active_rule != nullptr && !line.empty() &&
            (line.front() == '\t' || line.front() == ' ')) {
            active_rule->recipe.push_back(std::string(trimmed));
            continue;
        }
        active_rule = nullptr;
        std::string from;
        std::string to;
        if (trimmed.substr(0, 9) == ".SUFFIXES") {
            const std::size_t colon = trimmed.find(':');
            if (colon != std::string_view::npos) {
                std::string_view rest = trim(trimmed.substr(colon + 1));
                while (!rest.empty()) {
                    const std::size_t space = rest.find(' ');
                    const std::string_view suffix = space == std::string_view::npos ? rest
                                                                                    : rest.substr(0, space);
                    if (!suffix.empty()) file.suffixes.push_back(std::string(suffix));
                    if (space == std::string_view::npos) break;
                    rest = trim(rest.substr(space));
                }
            }
            continue;
        }
        if (is_suffix_rule_header(trimmed, from, to)) {
            file.rules.push_back({from, to, {}});
            active_rule = &file.rules.back();
            continue;
        }
        std::string_view name;
        std::string_view value;
        if (is_macro_definition(trimmed, name, value)) {
            file.macros[upper(name)] = std::string(value);
        }
        // Anything else (an explicit target rule, a `[section]` header, a
        // `!include`/`!if` directive) belongs to a different part of NMAKE's
        // grammar and is intentionally left unparsed here.
    }
    return file;
}

std::string expand_macros(std::string_view text, const MacroFile& file) {
    std::string result(text);
    for (int pass = 0; pass < 16; ++pass) {
        const std::size_t open = result.find("$(");
        if (open == std::string::npos) break;
        const std::size_t close = result.find(')', open);
        if (close == std::string::npos) break;
        const std::string name = upper(std::string_view(result).substr(open + 2, close - open - 2));
        const auto found = file.macros.find(name);
        const std::string replacement = found != file.macros.end() ? found->second : std::string();
        result = result.substr(0, open) + replacement + result.substr(close + 1);
    }
    return result;
}

}  // namespace dosbase::devtools
