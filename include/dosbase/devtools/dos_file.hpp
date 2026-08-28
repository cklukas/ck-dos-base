// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace dosbase::devtools {

// Path rules a reimplemented DOS tool must follow when it runs on a host
// filesystem (the host-path compatibility policy). A makefile writes `sys.exe` while
// FAT stores `SYS.EXE`: on macOS and Windows the difference is invisible, on
// Linux it decides whether the build works at all. Inputs therefore resolve
// case-insensitively, and outputs are created with the name FAT would hold —
// uppercase — so an artifact has the same path identity on every host.
[[nodiscard]] std::filesystem::path resolve_dos_input(std::string_view name);
[[nodiscard]] std::filesystem::path resolve_dos_output(std::string_view name);
[[nodiscard]] std::string dos_upper(std::string_view text);

}  // namespace dosbase::devtools
