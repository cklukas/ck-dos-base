// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace dosbase::devtools {

// DBOF's transformation, measured against the reference build's own
// artifacts. `dbof INPUT OUTPUT OFFSET LENGTH` reads LENGTH bytes
// of INPUT starting at OFFSET and writes them as MASM `db` directives: eight
// bytes per line, each formatted as `0XXH` — a leading zero always present,
// even when the hex digits would not otherwise start with a letter, so the
// token can never be read as an identifier — comma-separated with no
// trailing comma, CRLF line endings, nothing else in the file. The pinned
// tree's one invocation, `dbof msboot.bin boot.inc 7c00 200`, slices the
// padded boot sector out of MSBOOT.BIN so SYS can carry it as assembled data.
[[nodiscard]] std::string format_db_directive(std::span<const std::uint8_t> bytes);

// Slices `file` to [offset, offset + length) and formats it; throws
// FormatError if the slice runs past the end of the input.
[[nodiscard]] std::string dbof(std::span<const std::uint8_t> file, std::size_t offset,
                               std::size_t length);

}  // namespace dosbase::devtools
