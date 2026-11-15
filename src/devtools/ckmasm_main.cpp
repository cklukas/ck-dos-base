// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// MASM.EXE compatibility driver. Accepts the reference tool's command form:
// `masm -Mx -t -I<dir>... source,object;` --
// the trailing semicolon accepts the listing/crossref defaults (none).
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/masm.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    MasmOptions options;
    bool terse = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument.size() >= 2 && (argument[0] == '-' || argument[0] == '/')) {
            const char flag = argument[1];
            if (flag == 'I' || flag == 'i') {
                options.include_paths.push_back(argument.substr(2));
            } else if (flag == 'M' || flag == 'm') {
                if (argument.size() > 2 && (argument[2] == 'x' || argument[2] == 'X')) {
                    options.preserve_public_case = true;
                }
            } else if (flag == 't' || flag == 'T') {
                terse = true;
            } else if (flag == 'd' || flag == 'D') {
                std::string def = argument.substr(2);
                const std::size_t eq = def.find('=');
                if (eq == std::string::npos) {
                    options.defines.emplace_back(def, 1);
                } else {
                    const std::string name = def.substr(0, eq);
                    std::int64_t val = 1;
                    try {
                        val = std::stoll(def.substr(eq + 1), nullptr, 0);
                    } catch (...) {
                    }
                    options.defines.emplace_back(name, val);
                }
            } else {
                std::cerr << "ckmasm: unsupported option " << argument << "\n";
                return 2;
            }
            continue;
        }
        files.push_back(argument);
    }
    if (files.size() != 1) {
        std::cerr << "usage: ckmasm [-Mx] [-t] [-Ipath]... source,object;\n";
        return 2;
    }
    std::string spec = files[0];
    while (!spec.empty() && spec.back() == ';') spec.pop_back();
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = spec.find(',', start);
        parts.push_back(spec.substr(start, comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    std::string source = parts[0];
    if (source.find('.') == std::string::npos ||
        (source.find_last_of("\\/") != std::string::npos &&
         source.rfind('.') < source.find_last_of("\\/"))) {
        source += ".asm";
    }
    std::string object;
    if (parts.size() > 1 && !parts[1].empty()) {
        object = parts[1];
        if (object.find('.') == std::string::npos ||
            (object.find_last_of("\\/") != std::string::npos &&
             object.rfind('.') < object.find_last_of("\\/"))) {
            object += ".obj";
        }
    } else {
        object = std::filesystem::path(source).stem().string() + ".obj";
    }

    const MasmResult result = masm_assemble(source, options);
    for (const std::string& line : result.console) std::cout << line << "\n";
    if (!result.errors.empty()) {
        for (const std::string& error : result.errors) {
            std::cerr << "ckmasm: " << source << ": " << error << "\n";
        }
        return 1;
    }
    const std::filesystem::path path = resolve_dos_output(object);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(result.object.data()),
                 static_cast<std::streamsize>(result.object.size()));
    if (!stream) {
        std::cerr << "ckmasm: cannot write " << path.string() << "\n";
        return 1;
    }
    if (!terse) std::cout << "  0 Warning Errors\n  0 Severe  Errors\n";
    return 0;
}
