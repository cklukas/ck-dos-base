// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/nmake_plan.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <deque>
#include <map>
#include <set>

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

bool looks_like_macro_definition(std::string_view line) {
    // Same test parse_macro_file() uses: the first `=` precedes any `:`,
    // `$`, `(`, `)`, or whitespace in the candidate name.
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) return false;
    const std::string_view candidate = trim(line.substr(0, equals));
    if (candidate.empty()) return false;
    for (const char character : candidate) {
        if (std::isspace(static_cast<unsigned char>(character))) return false;
        if (character == ':' || character == '$' || character == '(' || character == ')') return false;
    }
    return true;
}

bool looks_like_suffix_rule_header(std::string_view line) {
    if (line.empty() || line.front() != '.') return false;
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) return false;
    const std::string_view head = line.substr(0, colon);
    if (head.substr(0, 9) == ".SUFFIXES") return true;
    const std::size_t second_dot = head.find('.', 1);
    return second_dot != std::string_view::npos && second_dot != 0;
}

std::vector<std::string> split_whitespace(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index]))) ++index;
        const std::size_t start = index;
        while (index < text.size() && !std::isspace(static_cast<unsigned char>(text[index]))) ++index;
        if (index > start) parts.push_back(std::string(text.substr(start, index - start)));
    }
    return parts;
}

// The path's extension (including the dot), or an empty string if it has
// none -- `stem` is set to everything before it.
std::string_view suffix_of(std::string_view path, std::string_view& stem) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos) {
        stem = path;
        return {};
    }
    stem = path.substr(0, dot);
    return path.substr(dot);
}

std::string substitute_star(std::string_view line, std::string_view stem) {
    std::string result;
    result.reserve(line.size());
    for (std::size_t index = 0; index < line.size();) {
        if (line[index] == '$' && index + 1 < line.size() && line[index + 1] == '*') {
            result += stem;
            index += 2;
        } else {
            result += line[index];
            ++index;
        }
    }
    return result;
}

const TargetRule* find_rule(const Makefile& makefile, std::string_view name) {
    const std::string wanted = upper(name);
    for (const TargetRule& rule : makefile.targets) {
        if (upper(rule.target) == wanted) return &rule;
    }
    return nullptr;
}

// Finds an inference rule whose `to_suffix` matches the target and whose
// `from_suffix` matches one of the rule's own listed dependencies sharing
// the target's stem -- the shape every recipe-less target in the tree
// actually uses (tree.obj's deps list tree.asm; tree.ctl's list tree.skl).
// Returns the matched dependency's stem and the rule's recipe, or nullopt.
std::optional<std::pair<std::string, const std::vector<std::string>*>> find_inference_recipe(
    const TargetRule& rule, const MacroFile& macros) {
    std::string_view target_stem;
    const std::string_view target_suffix = suffix_of(rule.target, target_stem);
    for (const std::string& dependency : rule.dependencies) {
        std::string_view dependency_stem;
        const std::string_view dependency_suffix = suffix_of(dependency, dependency_stem);
        if (dependency_stem != target_stem) continue;
        for (const InferenceRule& candidate : macros.rules) {
            if (candidate.from_suffix == dependency_suffix && candidate.to_suffix == target_suffix) {
                return std::make_pair(std::string(target_stem), &candidate.recipe);
            }
        }
    }
    return std::nullopt;
}

class Planner {
  public:
    Planner(const Makefile& makefile, const MTimeOracle& mtime)
        : makefile_(makefile), mtime_(mtime) {}

    std::vector<PlannedCommand> plan(const std::string& target) {
        resolve(target);
        return std::move(out_);
    }

  private:
    // Returns the effective timestamp to use for `name` in a staleness
    // comparison: its real mtime if unchanged this run, or a sentinel
    // newer than anything on the tree if this call just rebuilt it.
    std::int64_t resolve(std::string_view name) {
        const std::string key = upper(name);
        const auto cached = resolved_.find(key);
        if (cached != resolved_.end()) return cached->second;
        // A target rule with no recipe still names a dependency list that
        // must be resolved (tree.obj lists treequ.inc; a leaf with no rule
        // just falls through to the mtime oracle below), so cycle
        // detection guards recursion depth, not "is this a target rule".
        if (!visiting_.insert(key).second) {
            resolved_[key] = existing_or_epoch(name);
            return resolved_[key];
        }
        const TargetRule* rule = find_rule(makefile_, name);
        std::int64_t result;
        if (rule == nullptr) {
            // The .SUFFIXES search: an undeclared target whose source
            // exists builds through an inference rule -- SORT's
            // sortmes.obj has no rule of its own and comes from
            // sortmes.asm via `.asm.obj:`.
            const TargetRule* synthesized = synthesize(name);
            result = synthesized != nullptr ? build_if_stale(*synthesized)
                                            : existing_or_epoch(name);
        } else {
            result = build_if_stale(*rule);
        }
        visiting_.erase(key);
        resolved_[key] = result;
        return result;
    }

    std::int64_t existing_or_epoch(std::string_view name) const {
        const auto found = mtime_(name);
        return found.has_value() ? *found : kMissing;
    }

    std::int64_t build_if_stale(const TargetRule& rule) {
        std::int64_t newest_dependency = kMissing;
        bool any_rebuilt = false;
        for (const std::string& dependency : rule.dependencies) {
            const std::int64_t before = existing_or_epoch(dependency);
            const std::int64_t after = resolve(dependency);
            if (after != before) any_rebuilt = true;
            newest_dependency = std::max(newest_dependency, after);
        }
        const std::int64_t target_time = existing_or_epoch(rule.target);
        const bool stale = target_time == kMissing || any_rebuilt || newest_dependency > target_time;
        if (!stale) return target_time;

        std::string_view stem_view;
        suffix_of(rule.target, stem_view);
        std::string stem(stem_view);
        const std::vector<std::string>* recipe = &rule.recipe;
        if (recipe->empty()) {
            const auto inferred = find_inference_recipe(rule, makefile_.macros);
            if (inferred.has_value()) {
                stem = inferred->first;
                recipe = inferred->second;
            } else {
                // The dependency list need not name the source: SORT's
                // sortmes.obj lists sort.asm yet builds from sortmes.asm.
                // The suffix search uses the target's own stem.
                for (const InferenceRule& candidate : makefile_.macros.rules) {
                    if (candidate.to_suffix !=
                        suffix_of(rule.target, stem_view)) {
                        continue;
                    }
                    const std::string source =
                        std::string(stem_view) + std::string(candidate.from_suffix);
                    if (!mtime_(source).has_value()) continue;
                    stem = std::string(stem_view);
                    recipe = &candidate.recipe;
                    break;
                }
            }
        }
        for (const std::string& line : *recipe) {
            out_.push_back({rule.target, expand_macros(substitute_star(line, stem), makefile_.macros)});
        }
        return ++clock_;
    }

    const TargetRule* synthesize(std::string_view name) {
        std::string_view stem;
        const std::string_view target_suffix = suffix_of(name, stem);
        if (target_suffix.empty()) return nullptr;
        if (target_suffix.rfind(".cl", 0) == 0) {
            if (const TargetRule* ctl = find_rule(makefile_, std::string(stem) + ".ctl");
                ctl != nullptr) {
                resolve(ctl->target);
                return nullptr;
            }
        }
        for (const InferenceRule& candidate : makefile_.macros.rules) {
            if (candidate.to_suffix != target_suffix) continue;
            const std::string source =
                std::string(stem) + std::string(candidate.from_suffix);
            if (!mtime_(source).has_value()) continue;
            synthesized_.push_back({std::string(name), {source}, {}});
            return &synthesized_.back();
        }
        return nullptr;
    }

    static constexpr std::int64_t kMissing = std::numeric_limits<std::int64_t>::min();

    const Makefile& makefile_;
    const MTimeOracle& mtime_;
    std::map<std::string, std::int64_t> resolved_;
    std::deque<TargetRule> synthesized_;
    std::set<std::string> visiting_;
    std::vector<PlannedCommand> out_;
    // Freshly built targets get a timestamp newer than anything the mtime
    // oracle can report, so a dependent evaluated later in the same plan()
    // call correctly sees them as just-rebuilt; the counter only needs to
    // be monotonic within one call, not meaningful across calls.
    std::int64_t clock_ = std::numeric_limits<std::int64_t>::max() - 1'000'000;
};

}  // namespace

std::vector<TargetRule> parse_target_rules(const std::vector<std::string>& lines, const MacroFile& macros) {
    std::vector<TargetRule> rules;
    std::size_t active_count = 0;
    for (const std::string& raw : lines) {
        const std::string_view line = raw;
        const std::string_view trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (active_count > 0 && !line.empty() && (line.front() == '\t' || line.front() == ' ')) {
            for (std::size_t k = rules.size() - active_count; k < rules.size(); ++k) {
                rules[k].recipe.push_back(std::string(trimmed));
            }
            continue;
        }
        active_count = 0;
        if (looks_like_macro_definition(trimmed) || looks_like_suffix_rule_header(trimmed)) continue;
        const std::size_t colon = trimmed.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string name =
            expand_macros(trim(trimmed.substr(0, colon)), macros);
        if (name.empty()) continue;
        const std::string expanded_deps = expand_macros(trimmed.substr(colon + 1), macros);
        const std::vector<std::string> targets = split_whitespace(name);
        const std::vector<std::string> deps = split_whitespace(expanded_deps);
        for (const std::string& t : targets) {
            rules.push_back({t, deps, {}});
        }
        active_count = targets.size();
    }
    return rules;
}

std::vector<PlannedCommand> plan_target(const std::string& target, const Makefile& makefile,
                                         const MTimeOracle& mtime) {
    Planner planner(makefile, mtime);
    return planner.plan(target);
}

}  // namespace dosbase::devtools
