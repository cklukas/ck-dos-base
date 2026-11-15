// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// MENUBLD.EXE's replacement for the MS-DOS 4 FDISK build.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/menubld.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::filesystem::path default_menu_output(const std::filesystem::path& template_path) {
    std::filesystem::path out = template_path;
    const std::string stem = template_path.stem().string();
    out.replace_filename(stem + "m.c");
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.size() < 2 || arguments.size() > 3) {
        std::cerr << "usage: ckmenubld TEMPLATE.MSG COUNTRY.MSG [OUTPUT.C]\n";
        return 2;
    }

    try {
        const std::filesystem::path template_path = resolve_dos_input(arguments[0]);
        const std::filesystem::path country_path = resolve_dos_input(arguments[1]);
        const std::filesystem::path output_path =
            arguments.size() == 3
                ? resolve_dos_output(arguments[2])
                : resolve_dos_output(default_menu_output(template_path).string());

        const std::vector<std::uint8_t> template_bytes = read_file(template_path.string());
        const std::vector<std::uint8_t> country_bytes = read_file(country_path.string());
        const std::string section_name = template_path.stem().string();

        std::optional<std::size_t> idx_offset;
        std::filesystem::path idx_path = country_path;
        idx_path.replace_extension(".idx");
        std::error_code ec;
        if (!std::filesystem::exists(idx_path, ec)) {
            idx_path.replace_extension(".IDX");
        }
        if (std::filesystem::exists(idx_path, ec)) {
            const std::vector<std::uint8_t> idx_bytes = read_file(idx_path.string());
            idx_offset = find_idx_section_offset(
                std::string_view(reinterpret_cast<const char*>(idx_bytes.data()), idx_bytes.size()),
                section_name);
        }

        const std::vector<std::uint8_t> generated =
            build_menu_source(template_bytes, country_bytes, section_name, idx_offset);

        std::ofstream stream(output_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::cerr << "ckmenubld: cannot create " << output_path.string() << "\n";
            return 1;
        }
        stream.write(reinterpret_cast<const char*>(generated.data()),
                     static_cast<std::streamsize>(generated.size()));
        if (!stream) {
            std::cerr << "ckmenubld: write error on " << output_path.string() << "\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "ckmenubld: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
