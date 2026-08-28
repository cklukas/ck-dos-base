// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/omf_module.hpp"

#include <array>
#include <optional>

#include "dosbase/devtools/binary.hpp"

namespace dosbase::devtools {
namespace {

// Expanded positions of one raw LIDATA content offset, one per repetition
// of the literal block that holds it; returns the block's expanded length.
std::uint32_t lidata_hits(const std::vector<std::uint8_t>& raw, std::size_t& pos,
                          std::uint16_t target, std::uint32_t expanded_base,
                          std::vector<std::uint32_t>& hits) {
    const std::uint16_t repeat =
        static_cast<std::uint16_t>(raw[pos] | (raw[pos + 1] << 8));
    const std::uint16_t blocks =
        static_cast<std::uint16_t>(raw[pos + 2] | (raw[pos + 3] << 8));
    pos += 4;
    if (blocks == 0) {
        const std::uint8_t count = raw[pos];
        const std::size_t data_start = pos + 1;
        pos += 1U + count;
        if (target >= data_start && target < data_start + count) {
            for (std::uint16_t k = 0; k < repeat; ++k) {
                hits.push_back(expanded_base + k * count +
                               static_cast<std::uint32_t>(target - data_start));
            }
        }
        return static_cast<std::uint32_t>(repeat) * count;
    }
    std::vector<std::uint32_t> inner_hits;
    std::uint32_t once = 0;
    for (std::uint16_t b = 0; b < blocks; ++b) {
        once += lidata_hits(raw, pos, target, expanded_base + once, inner_hits);
    }
    for (std::uint32_t hit : inner_hits) {
        for (std::uint16_t k = 0; k < repeat; ++k) hits.push_back(hit + k * once);
    }
    return static_cast<std::uint32_t>(repeat) * once;
}

// A variable-length OMF number, used by COMDEF/LCOMDEF communal lengths.
std::uint32_t variable_length(ByteCursor& cursor) {
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

// Resolves a FIXUPP/MODEND frame reference. `method <= 2` consumes an index
// from the cursor (segment/group/external); the rest are self-describing.
FixupRef read_frame(ByteCursor& cursor, std::uint8_t method, bool threaded, std::uint8_t thread) {
    FixupRef ref;
    ref.thread = thread;
    if (threaded) {
        ref.kind = FixupRefKind::threaded;
        return ref;
    }
    switch (method) {
        case 0: ref.kind = FixupRefKind::segment; ref.index = cursor.index() - 1U; break;
        case 1: ref.kind = FixupRefKind::group; ref.index = cursor.index() - 1U; break;
        case 2: ref.kind = FixupRefKind::external; ref.index = cursor.index() - 1U; break;
        case 3: ref.kind = FixupRefKind::frame_number; ref.frame_number = cursor.word(); break;
        case 4: ref.kind = FixupRefKind::location; break;
        case 5: ref.kind = FixupRefKind::target; break;
        case 6: ref.kind = FixupRefKind::none; break;
        default: throw FormatError(cursor.absolute(), "unknown FIXUPP frame method");
    }
    return ref;
}

// Resolves a FIXUPP/MODEND target reference. Method's low two bits select
// segment/group/external/frame-number the same way `read_frame` does.
FixupRef read_target(ByteCursor& cursor, std::uint8_t method, bool threaded, std::uint8_t thread) {
    FixupRef ref;
    ref.thread = thread;
    if (threaded) {
        ref.kind = FixupRefKind::threaded;
        return ref;
    }
    switch (method & 3U) {
        case 0: ref.kind = FixupRefKind::segment; ref.index = cursor.index() - 1U; break;
        case 1: ref.kind = FixupRefKind::group; ref.index = cursor.index() - 1U; break;
        case 2: ref.kind = FixupRefKind::external; ref.index = cursor.index() - 1U; break;
        case 3: ref.kind = FixupRefKind::frame_number; ref.frame_number = cursor.word(); break;
        default: throw FormatError(cursor.absolute(), "unknown FIXUPP target method");
    }
    return ref;
}

void expand_iterated(ByteCursor& cursor, std::vector<std::uint8_t>& out, std::size_t depth) {
    if (depth > 16) throw FormatError(cursor.absolute(), "iterated data nested too deeply");
    const std::uint16_t repeat = cursor.word();
    const std::uint16_t blocks = cursor.word();
    if (blocks != 0) {
        const std::size_t mark = out.size();
        for (std::uint16_t index = 0; index < blocks; ++index) expand_iterated(cursor, out, depth + 1);
        const std::vector<std::uint8_t> block(out.begin() + static_cast<std::ptrdiff_t>(mark), out.end());
        for (std::uint16_t index = 1; index < repeat; ++index) {
            out.insert(out.end(), block.begin(), block.end());
        }
        return;
    }
    const std::size_t length = cursor.byte();
    const auto content = cursor.take(length);
    for (std::uint16_t index = 0; index < repeat; ++index) out.insert(out.end(), content.begin(), content.end());
}

}  // namespace

Module parse_module(const std::vector<OmfRecord>& records) {
    Module module;
    std::size_t last_data_segment = SIZE_MAX;
    std::vector<std::uint32_t> typdef_communal;  // per TYPDEF: communal bytes
    // FIXUPP THREAD state: four frame and four target slots, persisting
    // across FIXUPP records (and into MODEND) for the whole module.
    std::array<std::optional<FixupRef>, 4> thread_frames;
    std::array<std::optional<FixupRef>, 4> thread_targets;
    const auto frame_thread = [&](std::uint8_t number, std::size_t at) {
        if (!thread_frames[number]) {
            throw FormatError(at, "FIXUPP uses an undefined frame thread");
        }
        return *thread_frames[number];
    };
    const auto target_thread = [&](std::uint8_t number, std::size_t at) {
        if (!thread_targets[number]) {
            throw FormatError(at, "FIXUPP uses an undefined target thread");
        }
        return *thread_targets[number];
    };
    std::uint32_t last_data_offset = 0;
    bool last_was_lidata = false;
    std::vector<std::uint8_t> last_lidata_raw;
    // Record-stream order of each PUBDEF/EXTDEF/COMDEF name, so link_image can
    // recover LINK's symbol-table insertion order for the /M by-value tie-break.
    std::uint32_t mention_seq = 0;

    for (const OmfRecord& record : records) {
        ByteCursor cursor(record.body, record.offset + 3U);
        const std::uint8_t base = record.type & 0xFEU;
        switch (static_cast<OmfType>(base)) {
            case OmfType::theadr:
            case OmfType::lheadr: {
                module.name = cursor.name();
                break;
            }
            case OmfType::lnames: {
                while (!cursor.empty()) module.lnames.push_back(cursor.name());
                break;
            }
            case OmfType::segdef: {
                ModuleSegment segment;
                const std::uint8_t acbp = cursor.byte();
                segment.alignment = static_cast<std::uint8_t>(acbp >> 5);
                segment.combine = static_cast<std::uint8_t>((acbp >> 2) & 7U);
                segment.big = (acbp & 0x02U) != 0;
                segment.use32 = (acbp & 0x01U) != 0;
                if (segment.alignment == 0) {
                    segment.absolute_frame = cursor.word();
                    segment.absolute_offset = cursor.byte();
                }
                segment.length = cursor.word();
                if (segment.big && segment.length == 0) segment.length = 0x10000;
                const std::uint16_t name_index = cursor.index();
                const std::uint16_t class_index = cursor.index();
                const std::uint16_t overlay_index = cursor.index();
                segment.name = name_index != 0 && name_index <= module.lnames.size()
                                  ? module.lnames[name_index - 1U] : std::string();
                segment.class_name = class_index != 0 && class_index <= module.lnames.size()
                                        ? module.lnames[class_index - 1U] : std::string();
                segment.overlay_name = overlay_index != 0 && overlay_index <= module.lnames.size()
                                          ? module.lnames[overlay_index - 1U] : std::string();
                module.segments.push_back(std::move(segment));
                break;
            }
            case OmfType::grpdef: {
                ModuleGroup group;
                const std::uint16_t name_index = cursor.index();
                group.name = name_index != 0 && name_index <= module.lnames.size()
                                ? module.lnames[name_index - 1U] : std::string();
                while (!cursor.empty()) {
                    cursor.byte();  // component kind (0xFF = segment index; only kind the tree uses)
                    const std::uint16_t segment_index = cursor.index();
                    group.segments.push_back(segment_index - 1U);
                }
                module.groups.push_back(std::move(group));
                break;
            }
            case OmfType::pubdef:
            case OmfType::lpubdef: {
                const bool is_local = record.type ==
                                      static_cast<std::uint8_t>(OmfType::lpubdef);
                const std::uint16_t group_index = cursor.index();
                const std::uint16_t segment_index = cursor.index();
                std::uint16_t frame_number = 0;
                if (segment_index == 0) frame_number = cursor.word();  // absolute
                while (!cursor.empty()) {
                    ModulePublic pub;
                    pub.name = cursor.name();
                    pub.offset = cursor.word();
                    cursor.index();  // type index, unused by the linker
                    pub.segment = segment_index == 0 ? SIZE_MAX : segment_index - 1U;
                    pub.group = group_index == 0 ? SIZE_MAX : group_index - 1U;
                    pub.frame = frame_number;
                    pub.local = is_local;
                    pub.mention = mention_seq++;
                    module.publics.push_back(std::move(pub));
                }
                break;
            }
            case OmfType::typdef: {
                // Only the old-style communal TYPDEF shape matters to the
                // linker: an empty name, `en` zero, leaf 62 (near), a
                // variable leaf, and the length in bits (COMSUBS's
                // cmdbcs declares _dbcsvec this way).
                std::uint32_t communal_bytes = 0;
                if (cursor.remaining() >= 4) {
                    const std::string typdef_name = cursor.name();
                    cursor.byte();  // en
                    const std::uint8_t leaf = cursor.byte();
                    if (typdef_name.empty() && leaf == 0x62 &&
                        cursor.remaining() >= 2) {
                        cursor.byte();  // variable leaf (scalar/array/struct)
                        std::uint32_t bits = 0;
                        const std::uint8_t number = cursor.byte();
                        if (number <= 0x80) {
                            bits = number;
                        } else if (number == 0x81 && cursor.remaining() >= 2) {
                            bits = cursor.word();
                        } else if (number == 0x88 && cursor.remaining() >= 4) {
                            bits = cursor.word();
                            bits |= static_cast<std::uint32_t>(cursor.word()) << 16;
                        }
                        communal_bytes = (bits + 7) / 8;
                    }
                }
                typdef_communal.push_back(communal_bytes);
                break;
            }
            case OmfType::extdef:
            case OmfType::lextdef: {
                const bool local_scope = record.type ==
                                         static_cast<std::uint8_t>(OmfType::lextdef);
                while (!cursor.empty()) {
                    ModuleExternal external;
                    external.name = cursor.name();
                    external.local = local_scope;
                    const std::uint16_t type_index = cursor.index();
                    if (type_index > 0 &&
                        type_index <= typdef_communal.size() &&
                        typdef_communal[type_index - 1] > 0) {
                        external.communal = true;
                        external.communal_length =
                            typdef_communal[type_index - 1];
                    }
                    external.mention = mention_seq++;
                    module.externals.push_back(std::move(external));
                }
                break;
            }
            case OmfType::comdef:
            case OmfType::lcomdef: {
                while (!cursor.empty()) {
                    ModuleExternal external;
                    external.name = cursor.name();
                    cursor.index();  // type index
                    external.communal = true;
                    const std::uint8_t data_type = cursor.byte();
                    if (data_type == 0x61) {
                        const std::uint32_t count = variable_length(cursor);
                        const std::uint32_t element = variable_length(cursor);
                        external.communal_length = count * element;
                    } else {
                        external.communal_length = variable_length(cursor);
                    }
                    external.mention = mention_seq++;
                    module.externals.push_back(std::move(external));
                }
                break;
            }
            case OmfType::ledata: {
                DataChunk chunk;
                const std::uint16_t segment_index = cursor.index();
                chunk.segment = segment_index - 1U;
                chunk.offset = cursor.word();
                const auto payload = cursor.take(cursor.remaining());
                chunk.bytes.assign(payload.begin(), payload.end());
                last_data_segment = chunk.segment;
                last_data_offset = chunk.offset;
                last_was_lidata = false;
                module.data.push_back(std::move(chunk));
                break;
            }
            case OmfType::lidata: {
                DataChunk chunk;
                const std::uint16_t segment_index = cursor.index();
                chunk.segment = segment_index - 1U;
                chunk.offset = cursor.word();
                {
                    const auto raw = cursor.take(cursor.remaining());
                    last_lidata_raw.assign(raw.begin(), raw.end());
                    ByteCursor replay(raw, cursor.absolute() - raw.size());
                    while (!replay.empty()) expand_iterated(replay, chunk.bytes, 0);
                }
                last_data_segment = chunk.segment;
                last_data_offset = chunk.offset;
                last_was_lidata = true;
                module.data.push_back(std::move(chunk));
                break;
            }
            case OmfType::fixupp: {
                while (!cursor.empty()) {
                    const std::uint8_t lead = cursor.byte();
                    if ((lead & 0x80U) == 0) {
                        // THREAD subrecord: caches a frame/target method under
                        // a thread number for later FIXUP subrecords to reuse
                        // (the DOS utility libraries' members define them).
                        const bool is_frame = (lead & 0x40U) != 0;
                        const auto method = static_cast<std::uint8_t>((lead >> 2) & 7U);
                        const auto number = static_cast<std::uint8_t>(lead & 3U);
                        if (is_frame) {
                            thread_frames[number] =
                                read_frame(cursor, method, false, number);
                        } else {
                            thread_targets[number] =
                                read_target(cursor, method, false, number);
                        }
                        continue;
                    }
                    Fixup fixup;
                    const std::uint8_t second = cursor.byte();
                    fixup.segment_relative = (lead & 0x40U) != 0;
                    fixup.location = static_cast<std::uint8_t>((lead >> 2) & 0x0FU);
                    const auto data_offset = static_cast<std::uint16_t>(((lead & 3U) << 8) | second);
                    if (last_data_segment == SIZE_MAX) {
                        throw FormatError(cursor.absolute(), "FIXUPP with no preceding LEDATA/LIDATA");
                    }
                    fixup.segment = last_data_segment;
                    fixup.offset = last_data_offset + data_offset;
                    const std::uint8_t fix_data = cursor.byte();
                    const bool frame_threaded = (fix_data & 0x80U) != 0;
                    const auto frame_method = static_cast<std::uint8_t>((fix_data >> 4) & 7U);
                    const bool target_threaded = (fix_data & 0x08U) != 0;
                    const bool no_displacement = (fix_data & 0x04U) != 0;
                    const auto target_method = static_cast<std::uint8_t>(fix_data & 3U);
                    fixup.frame =
                        frame_threaded
                            ? frame_thread(static_cast<std::uint8_t>((fix_data >> 4) & 3U),
                                           cursor.absolute())
                            : read_frame(cursor, frame_method, false, 0);
                    fixup.target =
                        target_threaded
                            ? target_thread(static_cast<std::uint8_t>(fix_data & 3U),
                                            cursor.absolute())
                            : read_target(cursor, target_method, false, 0);
                    fixup.has_displacement = !no_displacement;
                    if (!no_displacement) fixup.displacement = cursor.word();
                    if (last_was_lidata) {
                        // LINK applies an LIDATA fixup to EVERY repetition
                        // of the block holding it (MSBIO's SYSINIT far
                        // pointer tables).
                        std::vector<std::uint32_t> hits;
                        std::size_t raw_pos = 0;
                        std::uint32_t expanded = 0;
                        while (raw_pos < last_lidata_raw.size()) {
                            expanded += lidata_hits(last_lidata_raw, raw_pos, data_offset,
                                                    expanded, hits);
                        }
                        for (std::uint32_t hit : hits) {
                            Fixup replicated = fixup;
                            replicated.offset = last_data_offset + hit;
                            module.fixups.push_back(replicated);
                        }
                    } else {
                        module.fixups.push_back(fixup);
                    }
                }
                break;
            }
            case OmfType::modend: {
                const std::uint8_t module_type = cursor.byte();
                module.end.is_main = (module_type & 0x80U) != 0;
                module.end.has_start = (module_type & 0x40U) != 0;
                if (module.end.has_start) {
                    const std::uint8_t end_data = cursor.byte();
                    const bool frame_threaded = (end_data & 0x80U) != 0;
                    const auto frame_method = static_cast<std::uint8_t>((end_data >> 4) & 7U);
                    const bool target_threaded = (end_data & 0x08U) != 0;
                    const bool no_displacement = (end_data & 0x04U) != 0;
                    const auto target_method = static_cast<std::uint8_t>(end_data & 3U);
                    module.end.start_frame =
                        frame_threaded
                            ? frame_thread(static_cast<std::uint8_t>((end_data >> 4) & 3U),
                                           cursor.absolute())
                            : read_frame(cursor, frame_method, false, 0);
                    module.end.start_target =
                        target_threaded
                            ? target_thread(static_cast<std::uint8_t>(end_data & 3U),
                                            cursor.absolute())
                            : read_target(cursor, target_method, false, 0);
                    module.end.start_has_displacement = !no_displacement;
                    if (!no_displacement) module.end.start_displacement = cursor.word();
                }
                break;
            }
            case OmfType::coment: {
                if (cursor.remaining() >= 2) {
                    cursor.byte();  // comment flags
                    if (cursor.byte() == 0x9E) module.dosseg = true;
                }
                break;
            }
            default:
                break;  // other advisory records carry nothing cklink needs
        }
    }
    return module;
}

}  // namespace dosbase::devtools
