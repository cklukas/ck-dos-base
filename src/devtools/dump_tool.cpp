// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/dump_tool.hpp"

#include "dosbase/devtools/binary.hpp"

#include <algorithm>
#include <iostream>

namespace dosbase::devtools {
namespace {

void usage(const std::string& tool) {
    std::cerr << "usage: " << tool
              << " [--data] [--records] [--symbols] [--round-trip] [--verify-dictionary]"
              << " [--quiet] FILE...\n";
}

// Reports where two byte streams first differ, which is the question a
// bootstrap diff always asks next.
std::string first_difference(std::span<const std::uint8_t> left,
                             std::span<const std::uint8_t> right) {
    const std::size_t shared = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < shared; ++index) {
        if (left[index] != right[index]) {
            return "byte " + hex32(static_cast<std::uint32_t>(index)) + ": input " +
                   hex8(left[index]) + ", re-serialized " + hex8(right[index]);
        }
    }
    return "length: input " + std::to_string(left.size()) + ", re-serialized " +
           std::to_string(right.size());
}

}  // namespace

int run_dump_tool(int argc, char** argv, const std::string& tool, const DumpFunction& dump) {
    DumpOptions options;
    std::vector<std::string> files;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--data") {
            options.data = true;
        } else if (argument == "--records") {
            options.records = true;
        } else if (argument == "--symbols") {
            options.symbols = true;
        } else if (argument == "--round-trip") {
            options.round_trip = true;
        } else if (argument == "--verify-dictionary") {
            options.verify_dictionary = true;
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--help" || argument == "-h") {
            usage(tool);
            return 0;
        } else if (argument.rfind("--", 0) == 0) {
            std::cerr << tool << ": unknown option " << argument << "\n";
            usage(tool);
            return 2;
        } else {
            files.push_back(argument);
        }
    }
    if (files.empty()) {
        usage(tool);
        return 2;
    }

    int failures = 0;
    for (const std::string& file : files) {
        std::string text;
        try {
            const std::vector<std::uint8_t> bytes = read_file(file);
            const std::vector<std::uint8_t> again = dump(bytes, file, options, text);
            if (options.round_trip) {
                if (again.size() == bytes.size() &&
                    std::equal(again.begin(), again.end(), bytes.begin())) {
                    text += "  round_trip ok " + std::to_string(bytes.size()) + " bytes\n";
                } else {
                    ++failures;
                    text += "  round_trip MISMATCH " + first_difference(bytes, again) + "\n";
                    std::cout << text << std::flush;
                    continue;
                }
            }
            if (!options.quiet) std::cout << text << std::flush;
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << tool << ": " << file << ": " << error.what() << "\n";
        }
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace dosbase::devtools
