// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/omf_record.hpp"

#include "dosbase/devtools/binary.hpp"

#include <algorithm>

namespace dosbase::devtools {
namespace {

constexpr std::size_t kMaxIteratedDepth = 16;

const char* location_name(std::uint8_t location) {
    switch (location) {
        case 0: return "lobyte";
        case 1: return "offset16";
        case 2: return "base";
        case 3: return "pointer32";
        case 4: return "hibyte";
        case 5: return "offset16_loader";
        case 9: return "offset32";
        case 11: return "pointer48";
        case 13: return "offset32_loader";
        default: return "location?";
    }
}

const char* alignment_name(std::uint8_t alignment) {
    switch (alignment) {
        case 0: return "absolute";
        case 1: return "byte";
        case 2: return "word";
        case 3: return "paragraph";
        case 4: return "page";
        case 5: return "dword";
        default: return "align?";
    }
}

const char* combine_name(std::uint8_t combine) {
    switch (combine) {
        case 0: return "private";
        case 2: return "public";
        case 4: return "public4";
        case 5: return "stack";
        case 6: return "common";
        case 7: return "public7";
        default: return "combine?";
    }
}

const char* comment_class_name(std::uint8_t comment_class) {
    switch (comment_class) {
        case 0x00: return "translator";
        case 0x01: return "intel_copyright";
        case 0x81: return "default_library";
        case 0x9C: return "ms_dos_version";
        case 0x9D: return "memory_model";
        case 0x9E: return "dosseg";
        case 0x9F: return "library_specifier";
        case 0xA0: return "omf_extension";
        case 0xA1: return "new_omf";
        case 0xA2: return "link_pass_separator";
        case 0xA3: return "libmod";
        case 0xE9: return "dependency_file";
        case 0xFF: return "command_line";
        default: return "class?";
    }
}

std::string frame_reference(std::uint8_t method, bool threaded, std::uint16_t datum,
                            const OmfNameTable& table) {
    if (threaded) return "thread" + std::to_string(method);
    switch (method) {
        case 0: return "segment:" + table.segment(datum);
        case 1: return "group:" + table.group(datum);
        case 2: return "external:" + table.external(datum);
        case 3: return "frame:" + hex16(datum);
        case 4: return "location";
        case 5: return "target";
        case 6: return "none";
        default: return "method?" + std::to_string(method);
    }
}

std::string target_reference(std::uint8_t method, bool threaded, std::uint16_t datum,
                             const OmfNameTable& table) {
    if (threaded) return "thread" + std::to_string(method);
    switch (method & 3U) {
        case 0: return "segment:" + table.segment(datum);
        case 1: return "group:" + table.group(datum);
        case 2: return "external:" + table.external(datum);
        default: return "frame:" + hex16(datum);
    }
}

// COMDEF communal lengths use OMF's variable-length number encoding.
std::uint32_t communal_length(ByteCursor& cursor) {
    const std::uint8_t lead = cursor.byte();
    if (lead < 0x80) return lead;
    switch (lead) {
        case 0x81: return cursor.word();
        case 0x84: {
            const std::uint32_t low = cursor.word();
            return low | (static_cast<std::uint32_t>(cursor.byte()) << 16);
        }
        case 0x88: return cursor.dword();
        default: throw FormatError(cursor.absolute(), "unknown communal length prefix");
    }
}

void dump_iterated_block(ByteCursor& cursor, const std::string& indent, std::size_t depth,
                         bool data, std::string& out) {
    if (depth > kMaxIteratedDepth) {
        throw FormatError(cursor.absolute(), "iterated data nested too deeply");
    }
    const std::uint16_t repeat = cursor.word();
    const std::uint16_t blocks = cursor.word();
    out += indent + "block repeat " + std::to_string(repeat) + " blocks " + std::to_string(blocks);
    if (blocks != 0) {
        out += "\n";
        for (std::uint16_t index = 0; index < blocks; ++index) {
            dump_iterated_block(cursor, indent + "  ", depth + 1, data, out);
        }
        return;
    }
    const std::size_t length = cursor.byte();
    const auto content = cursor.take(length);
    out += " content " + std::to_string(length) + " crc32 " + hex32(crc32(content));
    if (data) out += " data " + hex_bytes(content);
    out += "\n";
}

std::string checksum_state(const OmfRecord& record) {
    if (record.checksum == 0) return "zero";
    return record.checksum_valid() ? "ok" : "bad";
}

}  // namespace

std::string omf_type_name(std::uint8_t type) {
    switch (static_cast<OmfType>(type & 0xFEU)) {
        case OmfType::theadr: return "THEADR";
        case OmfType::lheadr: return "LHEADR";
        case OmfType::coment: return "COMENT";
        case OmfType::modend: return "MODEND";
        case OmfType::extdef: return "EXTDEF";
        case OmfType::typdef: return "TYPDEF";
        case OmfType::pubdef: return "PUBDEF";
        case OmfType::linnum: return "LINNUM";
        case OmfType::lnames: return "LNAMES";
        case OmfType::segdef: return "SEGDEF";
        case OmfType::grpdef: return "GRPDEF";
        case OmfType::fixupp: return "FIXUPP";
        case OmfType::ledata: return "LEDATA";
        case OmfType::lidata: return "LIDATA";
        case OmfType::comdef: return "COMDEF";
        case OmfType::bakpat: return "BAKPAT";
        case OmfType::lextdef: return "LEXTDEF";
        case OmfType::lpubdef: return "LPUBDEF";
        case OmfType::lcomdef: return "LCOMDEF";
        default: return "UNKNOWN";
    }
}

bool OmfRecord::checksum_valid() const noexcept {
    std::uint32_t sum = type;
    sum += declared_length & 0xFFU;
    sum += declared_length >> 8;
    for (const std::uint8_t value : body) sum += value;
    sum += checksum;
    return (sum & 0xFFU) == 0;
}

void OmfNameTable::clear() {
    names.clear();
    segments.clear();
    groups.clear();
    externals.clear();
}

namespace {
std::string lookup(const std::vector<std::string>& values, std::uint16_t index, const char* kind) {
    if (index == 0 || index > values.size()) {
        return std::string(kind) + "#" + std::to_string(index);
    }
    return std::to_string(index) + ":" + quoted(values[index - 1U]);
}
}  // namespace

std::string OmfNameTable::name(std::uint16_t index) const { return lookup(names, index, "name"); }
std::string OmfNameTable::segment(std::uint16_t index) const { return lookup(segments, index, "seg"); }
std::string OmfNameTable::group(std::uint16_t index) const { return lookup(groups, index, "grp"); }
std::string OmfNameTable::external(std::uint16_t index) const {
    return lookup(externals, index, "ext");
}

std::vector<OmfRecord> parse_omf_records(std::span<const std::uint8_t> bytes,
                                         std::size_t base_offset, bool stop_after_modend,
                                         std::size_t* consumed) {
    ByteCursor cursor(bytes, base_offset);
    std::vector<OmfRecord> records;
    while (!cursor.empty()) {
        OmfRecord record;
        record.offset = cursor.absolute();
        record.type = cursor.byte();
        if (record.type == 0) {
            throw FormatError(record.offset, "record type 0x00 is not an OMF record");
        }
        record.declared_length = cursor.word();
        if (record.declared_length == 0) {
            throw FormatError(record.offset, "record length 0 leaves no checksum byte");
        }
        const auto body = cursor.take(record.declared_length - 1U);
        record.body.assign(body.begin(), body.end());
        record.checksum = cursor.byte();
        const bool modend = (record.type & 0xFEU) == static_cast<std::uint8_t>(OmfType::modend);
        records.push_back(std::move(record));
        if (stop_after_modend && modend) break;
    }
    if (consumed != nullptr) *consumed = cursor.position();
    return records;
}

std::vector<std::uint8_t> serialize_omf_records(const std::vector<OmfRecord>& records) {
    std::vector<std::uint8_t> out;
    std::size_t total = 0;
    for (const OmfRecord& record : records) total += record.total_bytes();
    out.reserve(total);
    for (const OmfRecord& record : records) {
        out.push_back(record.type);
        out.push_back(static_cast<std::uint8_t>(record.declared_length & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(record.declared_length >> 8));
        out.insert(out.end(), record.body.begin(), record.body.end());
        out.push_back(record.checksum);
    }
    return out;
}

void dump_omf_record(const OmfRecord& record, OmfNameTable& table, const std::string& indent,
                     bool data, std::string& out) {
    ByteCursor cursor(record.body, record.offset + 3U);
    const std::uint8_t base_type = record.type & 0xFEU;
    out += indent + hex8(record.type) + " " + omf_type_name(record.type) + " offset " +
           hex32(static_cast<std::uint32_t>(record.offset)) + " length " +
           std::to_string(record.declared_length) + " checksum " + checksum_state(record) + "\n";
    const std::string detail = indent + "  ";
    switch (static_cast<OmfType>(base_type)) {
        case OmfType::theadr:
        case OmfType::lheadr: {
            out += detail + "name " + quoted(cursor.name()) + "\n";
            break;
        }
        case OmfType::coment: {
            const std::uint8_t flags = cursor.byte();
            const std::uint8_t comment_class = cursor.byte();
            const auto rest = cursor.take(cursor.remaining());
            out += detail + "flags " + hex8(flags) + " class " + hex8(comment_class) + " " +
                   comment_class_name(comment_class) + " bytes " + std::to_string(rest.size()) +
                   " data " + hex_bytes(rest) + "\n";
            break;
        }
        case OmfType::modend: {
            const std::uint8_t module_type = cursor.byte();
            out += detail + "module_type " + hex8(module_type) +
                   ((module_type & 0x80U) != 0 ? " main" : "") +
                   ((module_type & 0x40U) != 0 ? " start" : "") + "\n";
            if ((module_type & 0x40U) != 0) {
                const std::uint8_t end_data = cursor.byte();
                const bool frame_threaded = (end_data & 0x80U) != 0;
                const auto frame_method = static_cast<std::uint8_t>((end_data >> 4) & 7U);
                const bool target_threaded = (end_data & 0x08U) != 0;
                const bool no_displacement = (end_data & 0x04U) != 0;
                const auto target_method = static_cast<std::uint8_t>(end_data & 3U);
                std::uint16_t frame_datum = 0;
                std::uint16_t target_datum = 0;
                if (!frame_threaded && frame_method <= 2) frame_datum = cursor.index();
                if (!target_threaded) target_datum = cursor.index();
                out += detail + "start frame " +
                       frame_reference(frame_method, frame_threaded, frame_datum, table) +
                       " target " +
                       target_reference(target_method, target_threaded, target_datum, table);
                if (!no_displacement) out += " displacement " + hex16(cursor.word());
                out += "\n";
            }
            break;
        }
        case OmfType::extdef:
        case OmfType::lextdef: {
            while (!cursor.empty()) {
                const std::string name = cursor.name();
                const std::uint16_t type_index = cursor.index();
                table.externals.push_back(name);
                out += detail + "external " + std::to_string(table.externals.size()) + " " +
                       quoted(name) + " type " + std::to_string(type_index) + "\n";
            }
            break;
        }
        case OmfType::pubdef:
        case OmfType::lpubdef: {
            const std::uint16_t group_index = cursor.index();
            const std::uint16_t segment_index = cursor.index();
            std::string base = "group " + table.group(group_index) + " segment " +
                               table.segment(segment_index);
            if (segment_index == 0) base += " frame " + hex16(cursor.word());
            out += detail + base + "\n";
            while (!cursor.empty()) {
                const std::string name = cursor.name();
                const std::uint16_t offset = cursor.word();
                const std::uint16_t type_index = cursor.index();
                out += detail + "public " + quoted(name) + " offset " + hex16(offset) + " type " +
                       std::to_string(type_index) + "\n";
            }
            break;
        }
        case OmfType::lnames: {
            while (!cursor.empty()) {
                table.names.push_back(cursor.name());
                out += detail + "name " + std::to_string(table.names.size()) + " " +
                       quoted(table.names.back()) + "\n";
            }
            break;
        }
        case OmfType::segdef: {
            const std::uint8_t acbp = cursor.byte();
            const auto alignment = static_cast<std::uint8_t>(acbp >> 5);
            const auto combine = static_cast<std::uint8_t>((acbp >> 2) & 7U);
            const bool big = (acbp & 0x02U) != 0;
            const bool use32 = (acbp & 0x01U) != 0;
            std::string absolute;
            if (alignment == 0) {
                const std::uint16_t frame = cursor.word();
                const std::uint8_t offset = cursor.byte();
                absolute = " frame " + hex16(frame) + " offset " + hex8(offset);
            }
            const std::uint16_t length = cursor.word();
            const std::uint16_t name_index = cursor.index();
            const std::uint16_t class_index = cursor.index();
            const std::uint16_t overlay_index = cursor.index();
            table.segments.push_back(name_index != 0 && name_index <= table.names.size()
                                         ? table.names[name_index - 1U]
                                         : std::string("?"));
            out += detail + "segment " + std::to_string(table.segments.size()) + " acbp " +
                   hex8(acbp) + " align " + alignment_name(alignment) + " combine " +
                   combine_name(combine) + (big ? " big" : "") + (use32 ? " use32" : " use16") +
                   absolute + " length " + hex16(length) + " name " + table.name(name_index) +
                   " class " + table.name(class_index) + " overlay " + table.name(overlay_index) +
                   "\n";
            break;
        }
        case OmfType::grpdef: {
            const std::uint16_t name_index = cursor.index();
            table.groups.push_back(name_index != 0 && name_index <= table.names.size()
                                       ? table.names[name_index - 1U]
                                       : std::string("?"));
            out += detail + "group " + std::to_string(table.groups.size()) + " name " +
                   table.name(name_index) + "\n";
            while (!cursor.empty()) {
                const std::uint8_t kind = cursor.byte();
                const std::uint16_t member = cursor.index();
                out += detail + "member kind " + hex8(kind) + " " + table.segment(member) + "\n";
            }
            break;
        }
        case OmfType::fixupp: {
            while (!cursor.empty()) {
                const std::uint8_t lead = cursor.byte();
                if ((lead & 0x80U) == 0) {
                    const bool frame_thread = (lead & 0x40U) != 0;
                    const auto method = static_cast<std::uint8_t>((lead >> 2) & 7U);
                    const auto thread = static_cast<std::uint8_t>(lead & 3U);
                    std::string datum;
                    if (method <= 2) {
                        const std::uint16_t value = cursor.index();
                        datum = frame_thread ? frame_reference(method, false, value, table)
                                             : target_reference(method, false, value, table);
                    } else {
                        datum = frame_thread ? frame_reference(method, false, 0, table) : "frame";
                    }
                    out += detail + "thread " + std::to_string(thread) + " kind " +
                           (frame_thread ? "frame" : "target") + " method " +
                           std::to_string(method) + " " + datum + "\n";
                    continue;
                }
                const std::uint8_t second = cursor.byte();
                const bool segment_relative = (lead & 0x40U) != 0;
                const auto location = static_cast<std::uint8_t>((lead >> 2) & 0x0FU);
                const auto data_offset =
                    static_cast<std::uint16_t>(((lead & 3U) << 8) | second);
                const std::uint8_t fix_data = cursor.byte();
                const bool frame_threaded = (fix_data & 0x80U) != 0;
                const auto frame_method = static_cast<std::uint8_t>((fix_data >> 4) & 7U);
                const bool target_threaded = (fix_data & 0x08U) != 0;
                const bool no_displacement = (fix_data & 0x04U) != 0;
                const auto target_method = static_cast<std::uint8_t>(fix_data & 3U);
                std::uint16_t frame_datum = 0;
                std::uint16_t target_datum = 0;
                if (!frame_threaded && frame_method <= 2) frame_datum = cursor.index();
                if (!target_threaded) target_datum = cursor.index();
                out += detail + "fixup " + (segment_relative ? "segment" : "self") + " location " +
                       location_name(location) + " offset " + hex16(data_offset) + " frame " +
                       frame_reference(frame_method, frame_threaded, frame_datum, table) +
                       " target " +
                       target_reference(target_method, target_threaded, target_datum, table);
                if (!no_displacement) out += " displacement " + hex16(cursor.word());
                out += "\n";
            }
            break;
        }
        case OmfType::ledata: {
            const std::uint16_t segment_index = cursor.index();
            const std::uint16_t offset = cursor.word();
            const auto payload = cursor.take(cursor.remaining());
            out += detail + "segment " + table.segment(segment_index) + " offset " + hex16(offset) +
                   " bytes " + std::to_string(payload.size()) + " crc32 " + hex32(crc32(payload));
            if (data) out += " data " + hex_bytes(payload);
            out += "\n";
            break;
        }
        case OmfType::lidata: {
            const std::uint16_t segment_index = cursor.index();
            const std::uint16_t offset = cursor.word();
            out += detail + "segment " + table.segment(segment_index) + " offset " + hex16(offset) +
                   "\n";
            while (!cursor.empty()) {
                dump_iterated_block(cursor, detail + "  ", 0, data, out);
            }
            break;
        }
        case OmfType::comdef:
        case OmfType::lcomdef: {
            while (!cursor.empty()) {
                const std::string name = cursor.name();
                const std::uint16_t type_index = cursor.index();
                const std::uint8_t data_type = cursor.byte();
                table.externals.push_back(name);
                std::string sizes;
                if (data_type == 0x61) {
                    const std::uint32_t count = communal_length(cursor);
                    const std::uint32_t element = communal_length(cursor);
                    sizes = " far count " + hex32(count) + " element " + hex32(element);
                } else {
                    sizes = " near length " + hex32(communal_length(cursor));
                }
                out += detail + "communal " + std::to_string(table.externals.size()) + " " +
                       quoted(name) + " type " + std::to_string(type_index) + " data_type " +
                       hex8(data_type) + sizes + "\n";
            }
            break;
        }
        case OmfType::linnum: {
            const std::uint16_t group_index = cursor.index();
            const std::uint16_t segment_index = cursor.index();
            out += detail + "group " + table.group(group_index) + " segment " +
                   table.segment(segment_index) + "\n";
            while (!cursor.empty()) {
                const std::uint16_t line = cursor.word();
                const std::uint16_t offset = cursor.word();
                out += detail + "line " + std::to_string(line) + " offset " + hex16(offset) + "\n";
            }
            break;
        }
        case OmfType::bakpat: {
            const std::uint16_t segment_index = cursor.index();
            const std::uint8_t location = cursor.byte();
            out += detail + "segment " + table.segment(segment_index) + " location " +
                   hex8(location) + "\n";
            while (!cursor.empty()) {
                const std::uint16_t offset = cursor.word();
                const std::uint16_t value = cursor.word();
                out += detail + "patch offset " + hex16(offset) + " value " + hex16(value) + "\n";
            }
            break;
        }
        default: {
            const auto payload = cursor.take(cursor.remaining());
            out += detail + "bytes " + std::to_string(payload.size()) + " data " +
                   hex_bytes(payload) + "\n";
            break;
        }
    }
}

}  // namespace dosbase::devtools
