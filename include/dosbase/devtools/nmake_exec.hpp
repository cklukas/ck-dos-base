// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dosbase::devtools {

struct ParsedCommand {
    bool quiet = false;         // leading '@'
    bool ignore_error = false;  // leading '-'
    std::string program;
    std::vector<std::string> args;
    std::optional<std::string> stdin_file;
    std::optional<std::string> stdout_file;
};

struct NmakeExecOptions {
    std::filesystem::path tool_dir;  // directory containing ck* binaries
    bool ignore_errors = false;      // global /I flag
    bool quiet = false;              // suppress echoing commands
};

// Parses a single NMAKE recipe line into program, arguments, '@'/'-' prefixes,
// and '<' / '>' file redirections.
[[nodiscard]] ParsedCommand parse_nmake_command(std::string_view command_line);

// Maps a historical DOS tool name (e.g. "masm", "LINK.EXE", "c:\\dos\\exe2bin")
// to its native ck* executable name (e.g. "ckmasm"), or nullopt if unknown.
[[nodiscard]] std::optional<std::string> map_dos_tool_name(std::string_view program);

// Executes a parsed NMAKE command (either a built-in DOS shell command such as
// del/erase/copy/cd/mkdir/echo/attrib or a mapped ck* native tool).
// Returns the command exit code (0 on success). Throws std::runtime_error if
// the command cannot be launched or is not yet supported (such as `cl`).
[[nodiscard]] int execute_nmake_command(const ParsedCommand& cmd,
                                        const NmakeExecOptions& options);

}  // namespace dosbase::devtools
