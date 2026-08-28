// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace dosbase::devtools {

struct DumpOptions {
    bool data = false;        // include payload bytes as hex
    bool records = false;     // expand library members into records
    bool symbols = false;     // expand the library dictionary
    bool round_trip = false;  // re-serialize and require byte identity
    bool verify_dictionary = false;  // rebuild a library dictionary and compare bytes
    bool quiet = false;       // report only failures
};

// Produces the dump text and returns the re-serialized artifact, which the
// driver compares with the input when --round-trip is given.
using DumpFunction = std::function<std::vector<std::uint8_t>(
    std::span<const std::uint8_t>, const std::string& label, const DumpOptions&, std::string& out)>;

// Shared entry point of the three inspectors: same flags, same exit codes,
// same first-divergence reporting.
int run_dump_tool(int argc, char** argv, const std::string& tool, const DumpFunction& dump);

}  // namespace dosbase::devtools
