// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// CONVERT.EXE's replacement for the MS-DOS 4 build tree.
// Supports both single-argument (`convert debug.exe`) and explicit-output
// (`convert backup.exe backup.com`) forms used by the tree's makefiles.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/convert.hpp"
#include "dosbase/devtools/dos_file.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::string lowercase_ascii(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool has_extension_ci(const std::filesystem::path& path, const std::string& ext) {
    return lowercase_ascii(path.extension().string()) == ext;
}

std::filesystem::path default_com_output(const std::filesystem::path& input) {
    std::filesystem::path out = input;
    out.replace_extension(".com");
    return out;
}

int convert_one(const std::string& input_arg,
                const std::optional<std::string>& output_arg) {
    using namespace dosbase::devtools;
    const std::filesystem::path input = resolve_dos_input(input_arg);
    if (has_extension_ci(input, ".com")) {
        std::cerr << "ckconvert: " << input_arg << ": .COM to .EXE conversion is not implemented\n";
        return 1;
    }
    if (!has_extension_ci(input, ".exe")) {
        std::cerr << "ckconvert: " << input_arg << ": .EXE required\n";
        return 1;
    }

    const std::vector<std::uint8_t> exe_bytes = read_file(input.string());
    const std::vector<std::uint8_t> com_bytes = convert_exe_to_com(exe_bytes);

    const std::filesystem::path output =
        output_arg.has_value()
            ? resolve_dos_output(*output_arg)
            : resolve_dos_output(default_com_output(input).string());

    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) {
        std::cerr << "ckconvert: cannot create " << output.string() << "\n";
        return 1;
    }
    stream.write(reinterpret_cast<const char*>(com_bytes.data()),
                 static_cast<std::streamsize>(com_bytes.size()));
    if (!stream) {
        std::cerr << "ckconvert: write error on " << output.string() << "\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.empty()) {
        std::cerr << "usage: ckconvert INPUT.EXE [OUTPUT.COM]\n";
        return 2;
    }

    try {
        if (arguments.size() == 2 &&
            has_extension_ci(std::filesystem::path(arguments[1]), ".com")) {
            return convert_one(arguments[0], arguments[1]);
        }
        for (const std::string& arg : arguments) {
            if (int rc = convert_one(arg, std::nullopt); rc != 0) {
                return rc;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "ckconvert: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
