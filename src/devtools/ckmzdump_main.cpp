// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/dump_tool.hpp"
#include "dosbase/devtools/mz_image.hpp"

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    return run_dump_tool(argc, argv, "ckmzdump",
                         [](std::span<const std::uint8_t> bytes, const std::string& label,
                            const DumpOptions& options, std::string& out) {
                             const MzImage image = MzImage::parse(bytes);
                             dump_mz(image, label, options.data, out);
                             return image.serialize();
                         });
}
