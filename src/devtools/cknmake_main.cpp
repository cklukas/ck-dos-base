// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// NMAKE 1.00.05's replacement, the /N driver. Reads TOOLS.INI
// from the directory the INIT environment variable names -- exactly where
// the captured build's environment points it -- merges the makefile's own
// macros and rules over it, plans the requested targets, and prints the
// command lines one per row, the listing the comparison harness's
// parse_plan() consumes. Only the plan mode exists: the reference chain
// never executes through NMAKE itself in the transcript, it executes the
// planned commands one at a time.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/nmake_plan.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::vector<std::string> read_joined(const std::filesystem::path& path) {
    using namespace dosbase::devtools;
    const std::vector<std::uint8_t> raw = read_file(path.string());
    return join_continuations(std::string_view(
        reinterpret_cast<const char*>(raw.data()), raw.size()));
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    bool plan_only = false;
    std::vector<std::string> targets;
    std::string makefile_name = "makefile";
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument.size() >= 2 && (argument[0] == '/' || argument[0] == '-')) {
            const char flag = static_cast<char>(std::toupper(argument[1]));
            if (flag == 'N') plan_only = true;
            else if (flag == 'I') continue;  // ignore-errors: no effect on a plan
            else if (flag == 'F' && i + 1 < argc) makefile_name = argv[++i];
            else {
                std::cerr << "cknmake: unsupported option " << argument << "\n";
                return 2;
            }
            continue;
        }
        targets.push_back(argument);
    }
    if (!plan_only) {
        std::cerr << "cknmake: only the /N plan mode is implemented\n";
        return 2;
    }
    try {
        Makefile makefile;
        // Environment macros first; TOOLS.INI and the makefile override.
        for (const char* name : {"COUNTRY", "INIT", "INCLUDE", "LIB", "BAKROOT",
                                 "CONFIG", "PATH", "COMSPEC"}) {
            const char* value = std::getenv(name);
            if (value != nullptr) makefile.macros.macros[name] = value;
        }
        if (const char* init = std::getenv("INIT"); init != nullptr) {
            // A guest-absolute INIT (c:\src\tools) resolves against the
            // DOS filesystem root the harness exports.
            std::string init_dir = init;
            if (init_dir.size() >= 2 && init_dir[1] == ':') {
                const char* root = std::getenv("DOSBASE_DOS_ROOT");
                init_dir = (root != nullptr ? std::string(root) : std::string(".")) +
                           init_dir.substr(2);
            }
            const std::filesystem::path tools_ini =
                resolve_dos_input(init_dir + "\\TOOLS.INI");
            std::error_code missing;
            if (std::filesystem::is_regular_file(tools_ini, missing)) {
                const std::vector<std::string> lines = read_joined(tools_ini);
                const MacroFile tools = parse_macro_file(lines);
                for (const auto& [key, value] : tools.macros) {
                    makefile.macros.macros[key] = value;
                }
                makefile.macros.suffixes = tools.suffixes;
                makefile.macros.rules = tools.rules;
            }
        }
        const std::filesystem::path makefile_path = resolve_dos_input(makefile_name);
        const std::vector<std::string> lines = read_joined(makefile_path);
        const MacroFile local = parse_macro_file(lines);
        for (const auto& [key, value] : local.macros) {
            makefile.macros.macros[key] = value;
        }
        for (const std::string& suffix : local.suffixes) {
            makefile.macros.suffixes.push_back(suffix);
        }
        for (const InferenceRule& rule : local.rules) {
            makefile.macros.rules.push_back(rule);
        }
        makefile.targets = parse_target_rules(lines, makefile.macros);
        if (targets.empty() && !makefile.targets.empty()) {
            targets.push_back(makefile.targets.front().target);
        }
        const MTimeOracle mtime = [](std::string_view name)
            -> std::optional<std::int64_t> {
            const std::filesystem::path path = resolve_dos_input(name);
            std::error_code missing;
            const auto stamp = std::filesystem::last_write_time(path, missing);
            if (missing) return std::nullopt;
            return std::chrono::duration_cast<std::chrono::seconds>(
                       stamp.time_since_epoch())
                .count();
        };
        for (std::string target : targets) {
            // Targets given as macros expand first: the bios module plans
            // `$(dest).sys`.
            target = expand_macros(target, makefile.macros);
            for (const PlannedCommand& command : plan_target(target, makefile, mtime)) {
                std::cout << "\t" << command.command << "\n";
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "cknmake: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
