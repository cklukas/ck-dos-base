// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// NOSRVBLD.EXE's replacement. `nosrvbld foo.skl ..\messages\usa-ms.msg`
// renders one flat `LABEL DB    <text>` include per `:class` of the skeleton,
// named `FOO.CL<class>` beside the skeleton. Text resolves exclusively
// through the message database -- a `:def`'s inline text is documentation.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/msgtools.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string_view as_text(const std::vector<std::uint8_t>& raw) {
    return {reinterpret_cast<const char*>(raw.data()), raw.size()};
}

}  // namespace

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    if (argc != 3) {
        std::cerr << "usage: cknosrvbld SKELETON MESSAGE-FILE\n";
        return 2;
    }
    const std::string skeleton_name = argv[1];
    try {
        const std::vector<std::uint8_t> skeleton =
            read_file(resolve_dos_input(skeleton_name).string());
        const std::vector<std::uint8_t> messages =
            read_file(resolve_dos_input(argv[2]).string());
        const MsgDatabase database = parse_msg(as_text(messages));
        std::string base = skeleton_name;
        const std::size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) base = base.substr(slash + 1);
        const std::size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        for (const auto& [class_id, bytes] : nosrvbld(as_text(skeleton), base, database)) {
            const std::filesystem::path path = resolve_dos_output(base + ".cl" + class_id);
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!stream) {
                std::cerr << "cknosrvbld: cannot write " << path.string() << "\n";
                return 1;
            }
            std::cout << "Writing to " << base << ".cl" << class_id << "\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "cknosrvbld: " << skeleton_name << ": " << error.what() << "\n";
        return 1;
    }
    return 0;
}
