// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dump_tool.hpp"
#include "dosbase/devtools/omf_record.hpp"

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    return run_dump_tool(argc, argv, "ckomfdump",
                         [](std::span<const std::uint8_t> bytes, const std::string& label,
                            const DumpOptions& options, std::string& out) {
                             const std::vector<OmfRecord> records =
                                 parse_omf_records(bytes, 0, false);
                             out += "object " + label + "\n";
                             out += "  bytes " + std::to_string(bytes.size()) + " records " +
                                    std::to_string(records.size()) + "\n";
                             OmfNameTable table;
                             for (const OmfRecord& record : records) {
                                 dump_omf_record(record, table, "  ", options.data, out);
                             }
                             return serialize_omf_records(records);
                         });
}
