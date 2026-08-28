// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// BUILDMSG.EXE's replacement. `buildmsg ..\..\messages\usa-ms foo.skl`
// takes the message database path WITHOUT its extension, renders the
// message-server include for every class of the skeleton (`FOO.CL<class>`),
// and the `$M_NUM_CLS` control file `FOO.CTL`.
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
        std::cerr << "usage: ckbuildmsg MESSAGE-BASE SKELETON\n";
        return 2;
    }
    const std::string skeleton_name = argv[2];
    try {
        const std::vector<std::uint8_t> messages =
            read_file(resolve_dos_input(std::string(argv[1]) + ".msg").string());
        const std::vector<std::uint8_t> skeleton =
            read_file(resolve_dos_input(skeleton_name).string());
        const MsgDatabase database = parse_msg(as_text(messages));
        const BuildMsgResult result = buildmsg(as_text(skeleton), database);
        std::string base = skeleton_name;
        const std::size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) base = base.substr(slash + 1);
        const std::size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        for (const auto& [suffix, bytes] : result.files) {
            const std::filesystem::path path = resolve_dos_output(base + "." + suffix);
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!stream) {
                std::cerr << "ckbuildmsg: cannot write " << path.string() << "\n";
                return 1;
            }
            std::cout << " Created include file [" << base << "." << suffix << "]\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "ckbuildmsg: " << skeleton_name << ": " << error.what() << "\n";
        return 1;
    }
    return 0;
}
