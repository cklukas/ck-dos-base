// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// LINK's replacement. Every header field, layout rule, and map
// column is measured off reference-corpus executables -- MSBOOT.EXE for the
// single-module shape, MSLOAD.MAP for the map skeleton, MSBIO.EXE/.MAP for
// multi-module combination, relocations, and the /M publics sections.
// Anything the preserved source tree does not exercise is refused by name.
#include "dosbase/devtools/link_image.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace dosbase::devtools {
namespace {

std::uint32_t alignment_of(std::uint8_t alignment) {
    switch (alignment) {
        case 1: return 1;
        case 2: return 2;
        case 3: return 16;
        case 4: return 256;
        case 5: return 4;
        default: return 1;
    }
}

// LINK 3.65's symbol-table bucket hash (measured off the seed LINK.EXE's
// enter-symbol routine): h starts at the name length, then folds each
// byte from last to first as tolower(byte) << (index & 3), and the bucket
// is the low byte. Symbols chain by front insertion, so a bucket lists
// its members newest-first; allocation walks buckets 0..255.
std::uint8_t link_bucket(std::string_view name) {
    std::uint16_t hash = static_cast<std::uint16_t>(name.size());
    for (std::size_t i = name.size(); i >= 1; --i) {
        const auto byte = static_cast<std::uint8_t>(name[i - 1]) | 0x20u;
        hash = static_cast<std::uint16_t>(
            hash + ((byte << (i & 3u)) & 0xFFFFu));
    }
    return static_cast<std::uint8_t>(hash & 0xFF);
}

std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return result;
}

// One module-segment's placement inside a combined output segment.
struct Placement {
    bool absolute = false;
    std::uint16_t absolute_frame = 0;
    std::size_t combined = 0;   // index into combined segments
    std::uint32_t offset = 0;   // of the module segment within the combined
};

struct Combined {
    std::string name;
    std::string class_name;
    std::uint8_t alignment = 1;  // the widest member's
    std::uint32_t length = 0;
    std::uint32_t base = 0;   // in the load image
    std::uint16_t frame = 0;  // base / 16, rounded down
};

struct ResolvedPublic {
    std::string name;         // as defined
    bool absolute = false;
    std::uint16_t frame = 0;  // display segment
    std::uint16_t offset = 0;
    std::uint32_t linear = 0;
};

// LINK's own qsort, reproduced from the unpacked seed LINK.EXE (image offset
// 0x1097e, recursion 0x109ee). It is what the `/M` "Publics by Value" section
// runs over the symbol-table walk order, and it is UNSTABLE: pivot is the first
// element, `di` scans up while <= pivot, `si` scans down while >= pivot, the two
// are swapped while si > di, then the pivot swaps into si, and it recurses the
// smaller side first while iterating the larger. Equal-address publics therefore
// come out in an order that depends on their positions, not a stable key -- which
// is exactly why every stable-sort model of this section was slightly wrong.
void link_qsort(std::vector<const ResolvedPublic*>& a, std::ptrdiff_t lo,
                std::ptrdiff_t hi) {
    while (lo < hi) {
        const std::uint32_t pivot = a[static_cast<std::size_t>(lo)]->linear;
        std::ptrdiff_t di = lo;
        std::ptrdiff_t si = hi + 1;
        for (;;) {
            do {
                ++di;
            } while (di < hi && a[static_cast<std::size_t>(di)]->linear <= pivot);
            do {
                --si;
            } while (si > lo && a[static_cast<std::size_t>(si)]->linear >= pivot);
            if (si > di) {
                std::swap(a[static_cast<std::size_t>(di)],
                          a[static_cast<std::size_t>(si)]);
            } else {
                break;
            }
        }
        std::swap(a[static_cast<std::size_t>(lo)], a[static_cast<std::size_t>(si)]);
        if (si - lo < hi - si) {
            link_qsort(a, lo, si - 1);
            lo = si + 1;
        } else {
            link_qsort(a, si + 1, hi);
            hi = si - 1;
        }
    }
}

}  // namespace

namespace {
// LINK 3.65's /EXEPACK unpacker stub, byte-for-byte as EXE2BIN.EXE and
// FIND.EXE carry it (identical in both), ending in its error message.
constexpr std::uint8_t kExepackStub[] = {
    0x8C, 0xC0, 0x05, 0x10, 0x00, 0x0E, 0x1F, 0xA3, 0x04, 0x00, 0x03, 0x06,
    0x0C, 0x00, 0x8E, 0xC0, 0x8B, 0x0E, 0x06, 0x00, 0x8B, 0xF9, 0x4F, 0x8B,
    0xF7, 0xFD, 0xF3, 0xA4, 0x50, 0xB8, 0x32, 0x00, 0x50, 0xCB, 0x8C, 0xC3,
    0x8C, 0xD8, 0x48, 0x8E, 0xD8, 0x8E, 0xC0, 0xBF, 0x0F, 0x00, 0xB9, 0x10,
    0x00, 0xB0, 0xFF, 0xF3, 0xAE, 0x47, 0x8B, 0xF7, 0x8B, 0xC3, 0x48, 0x8E,
    0xC0, 0xBF, 0x0F, 0x00, 0xB1, 0x04, 0x8B, 0xC6, 0xF7, 0xD0, 0xD3, 0xE8,
    0x74, 0x09, 0x8C, 0xDA, 0x2B, 0xD0, 0x8E, 0xDA, 0x83, 0xCE, 0xF0, 0x8B,
    0xC7, 0xF7, 0xD0, 0xD3, 0xE8, 0x74, 0x09, 0x8C, 0xC2, 0x2B, 0xD0, 0x8E,
    0xC2, 0x83, 0xCF, 0xF0, 0xAC, 0x8A, 0xD0, 0x4E, 0xAD, 0x8B, 0xC8, 0x46,
    0x8A, 0xC2, 0x24, 0xFE, 0x3C, 0xB0, 0x75, 0x06, 0xAC, 0xF3, 0xAA, 0xEB,
    0x07, 0x90, 0x3C, 0xB2, 0x75, 0x6B, 0xF3, 0xA4, 0x8A, 0xC2, 0xA8, 0x01,
    0x74, 0xBA, 0xBE, 0x25, 0x01, 0x0E, 0x1F, 0x8B, 0x1E, 0x04, 0x00, 0xFC,
    0x33, 0xD2, 0xAD, 0x8B, 0xC8, 0xE3, 0x13, 0x8B, 0xC2, 0x03, 0xC3, 0x8E,
    0xC0, 0xAD, 0x8B, 0xF8, 0x83, 0xFF, 0xFF, 0x74, 0x11, 0x26, 0x01, 0x1D,
    0xE2, 0xF3, 0x81, 0xFA, 0x00, 0xF0, 0x74, 0x16, 0x81, 0xC2, 0x00, 0x10,
    0xEB, 0xDC, 0x8C, 0xC0, 0x40, 0x8E, 0xC0, 0x83, 0xEF, 0x10, 0x26, 0x01,
    0x1D, 0x48, 0x8E, 0xC0, 0xEB, 0xE2, 0x8B, 0xC3, 0x8B, 0x3E, 0x08, 0x00,
    0x8B, 0x36, 0x0A, 0x00, 0x03, 0xF0, 0x01, 0x06, 0x02, 0x00, 0x2D, 0x10,
    0x00, 0x8E, 0xD8, 0x8E, 0xC0, 0xBB, 0x00, 0x00, 0xFA, 0x8E, 0xD6, 0x8B,
    0xE7, 0xFB, 0x2E, 0xFF, 0x2F, 0xB4, 0x40, 0xBB, 0x02, 0x00, 0xB9, 0x16,
    0x00, 0x8C, 0xCA, 0x8E, 0xDA, 0xBA, 0x0F, 0x01, 0xCD, 0x21, 0xB8, 0xFF,
    0x4C, 0xCD, 0x21, 0x50, 0x61, 0x63, 0x6B, 0x65, 0x64, 0x20, 0x66, 0x69,
    0x6C, 0x65, 0x20, 0x69, 0x73, 0x20, 0x63, 0x6F, 0x72, 0x72, 0x75, 0x70,
    0x74,
};
}  // namespace

LinkResult link_modules(const std::vector<Module>& modules, bool map_publics,
                        std::uint32_t stack_override, bool exepack,
                        const std::vector<std::size_t>& placement_order) {
    LinkResult result;
    const auto fail = [&result](const std::string& message) {
        result.errors.push_back(message);
    };

    // ------------------------------------------------- combine and place
    std::vector<Combined> combined;
    std::vector<std::vector<Placement>> placements(modules.size());
    std::vector<std::size_t> combine_walk = placement_order;
    if (combine_walk.empty()) {
        for (std::size_t m = 0; m < modules.size(); ++m) {
            combine_walk.push_back(m);
        }
    }
    for (std::size_t m = 0; m < modules.size(); ++m) {
        placements[m].resize(modules[m].segments.size());
    }
    for (const std::size_t m : combine_walk) {
        for (std::size_t s = 0; s < modules[m].segments.size(); ++s) {
            const ModuleSegment& segment = modules[m].segments[s];
            Placement& place = placements[m][s];
            if (segment.alignment == 0) {
                place.absolute = true;
                place.absolute_frame = segment.absolute_frame;
                continue;
            }
            std::size_t index = combined.size();
            for (std::size_t c = 0; c < combined.size(); ++c) {
                if (combined[c].name == segment.name &&
                    combined[c].class_name == segment.class_name) {
                    index = c;
                    break;
                }
            }
            if (index == combined.size()) {
                combined.push_back({segment.name, segment.class_name, 1, 0, 0, 0});
            }
            Combined& home = combined[index];
            if (alignment_of(segment.alignment) >
                alignment_of(home.alignment)) {
                // The combined segment adopts its strictest piece's
                // alignment, wherever that piece sits: REPLACE's _DATA
                // starts on a paragraph because _replace.asm's empty
                // paragraph-aligned _DATA says so, while DEBUG's DATA
                // stays at the byte-aligned 0x470a because no stricter
                // piece joins it.
                home.alignment = segment.alignment;
            }
            if (segment.combine == 6) {
                // COMMON: instances overlay at the segment start and the
                // longest wins (the C runtime's two-byte PAD segments).
                place.combined = index;
                place.offset = 0;
                home.length = std::max(home.length, segment.length);
                continue;
            }
            if (segment.combine == 5 && home.length > 0) {
                // Stack pieces concatenate without alignment gaps -- the
                // sizes just sum (DEBUG's empty paragraph-aligned CSTACK
                // pieces leave the stack at 0x16a).
                place.combined = index;
                place.offset = home.length;
                home.length += segment.length;
                continue;
            }
            const std::uint32_t align = alignment_of(segment.alignment);
            home.length = (home.length + align - 1) & ~(align - 1);
            place.combined = index;
            place.offset = home.length;
            home.length += segment.length;
        }
    }
    // DOSSEG (COMENT 9E, emitted by the C runtime's crt0): reorder the
    // combined segments by class -- code first, then segments outside
    // DGROUP, then DGROUP as BEGDATA / data / BSS / c_common / STACK --
    // pad the first code segment with 16 leading null bytes, allocate
    // near communals into a synthesized c_common, and define _edata and
    // _end at the BSS boundaries (ATTRIB.EXE is the measuring witness).
    bool dosseg = false;
    for (const Module& module : modules) dosseg = dosseg || module.dosseg;
    std::vector<std::pair<std::string, std::uint32_t>> communal_layout;
    std::size_t c_common_index = SIZE_MAX;
    std::map<std::string, std::uint32_t> communal_sizes;
    std::vector<std::string> communal_order;
    {
        std::set<std::string> defined;
        for (const Module& module : modules) {
            for (const ModulePublic& item : module.publics) {
                if (!item.local) defined.insert(upper(item.name));
            }
        }
        std::vector<std::string> encounter;
        for (const Module& module : modules) {
            for (const ModuleExternal& item : module.externals) {
                if (item.communal_length == 0) continue;
                const std::string key = upper(item.name);
                if (defined.contains(key)) continue;
                if (!communal_sizes.contains(key)) encounter.push_back(key);
                communal_sizes[key] =
                    std::max(communal_sizes[key], item.communal_length);
            }
        }
        // LINK allocates communals in symbol-table walk order: buckets
        // 0..255, and within a bucket the front-inserted chain lists
        // symbols in reverse encounter order.
        std::array<std::vector<std::string>, 256> buckets;
        for (const std::string& key : encounter) {
            buckets[link_bucket(key)].insert(buckets[link_bucket(key)].begin(),
                                             key);
        }
        for (auto& bucket : buckets) {
            for (const std::string& key : bucket) communal_order.push_back(key);
        }
    }
    const auto make_common = [&]() {
        // Paragraph-aligned block; each item word-aligns except the
        // one-byte items, which pack (ATTRIB: _attr at the odd 0xb61).
        Combined common{"c_common", "BSS", 3, 0, 0, 0};
        for (const std::string& name : communal_order) {
            const std::uint32_t size = communal_sizes.at(name);
            // Even-size communals align to a word; odd-size ones pack
            // contiguously (ATTRIB's _file at even 0x94c after odd _mmask;
            // BACKUP's 0x15-byte _SW* buffers pack tight).
            if (size % 2 == 0) common.length = (common.length + 1U) & ~1U;
            communal_layout.emplace_back(name, common.length);
            common.length += size;
        }
        return common;
    };
    if (dosseg) {
        std::set<std::size_t> dgroup;
        for (std::size_t m = 0; m < modules.size(); ++m) {
            for (const ModuleGroup& group : modules[m].groups) {
                if (upper(group.name) != "DGROUP") continue;
                for (std::size_t member : group.segments) {
                    const Placement& place = placements[m][member];
                    if (!place.absolute) dgroup.insert(place.combined);
                }
            }
        }
        const auto bucket = [&](std::size_t index) {
            const std::string cls = upper(combined[index].class_name);
            if (cls.size() >= 4 && cls.substr(cls.size() - 4) == "CODE") return 0;
            if (!dgroup.contains(index)) return 1;
            if (cls == "BEGDATA") return 2;
            if (cls == "BSS") return 4;
            if (cls == "STACK") return 6;
            return 3;
        };
        // Within the ordinary DGROUP data (bucket 3), LINK groups segments
        // by class in the order each class first appears -- BACKUP's EXEC
        // (class DATA) joins the other DATA segments though its own segment
        // is defined last.
        std::map<std::string, std::size_t> class_rank;
        for (std::size_t i = 0; i < combined.size(); ++i) {
            if (bucket(i) != 3) continue;
            const std::string cls = upper(combined[i].class_name);
            if (!class_rank.contains(cls)) {
                class_rank.emplace(cls, class_rank.size());
            }
        }
        const auto sub_rank = [&](std::size_t index) {
            if (bucket(index) != 3) return static_cast<std::size_t>(0);
            return class_rank.at(upper(combined[index].class_name));
        };
        std::vector<std::size_t> order(combined.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) {
                             if (bucket(a) != bucket(b)) return bucket(a) < bucket(b);
                             return sub_rank(a) < sub_rank(b);
                         });
        std::vector<Combined> reordered;
        std::vector<std::size_t> remap(combined.size());
        for (std::size_t position = 0; position < order.size(); ++position) {
            if (!communal_order.empty() && c_common_index == SIZE_MAX &&
                bucket(order[position]) > 4) {
                c_common_index = reordered.size();
                reordered.push_back(make_common());
            }
            remap[order[position]] = reordered.size();
            reordered.push_back(combined[order[position]]);
        }
        if (!communal_order.empty() && c_common_index == SIZE_MAX) {
            c_common_index = reordered.size();
            reordered.push_back(make_common());
        }
        combined = std::move(reordered);
        for (std::vector<Placement>& module_places : placements) {
            for (Placement& place : module_places) {
                if (!place.absolute) place.combined = remap[place.combined];
            }
        }
        bool has_dgroup = false;
        for (const Module& module : modules) {
            for (const ModuleGroup& group : module.groups) {
                if (upper(group.name) == "DGROUP") has_dgroup = true;
            }
        }
        for (std::size_t index = 0;
             has_dgroup && index < combined.size(); ++index) {
            const std::string cls = upper(combined[index].class_name);
            if (cls.size() < 4 || cls.substr(cls.size() - 4) != "CODE") continue;
            combined[index].length += 16;
            for (std::vector<Placement>& module_places : placements) {
                for (Placement& place : module_places) {
                    if (!place.absolute && place.combined == index) {
                        place.offset += 16;
                    }
                }
            }
            break;
        }
    } else if (!communal_order.empty()) {
        c_common_index = combined.size();
        combined.push_back(make_common());
    }
    std::uint32_t image_size = 0;
    for (Combined& segment : combined) {
        // Each combined segment aligns by its own (widest) alignment --
        // MSDOS packs word-aligned CONSTANTS at 4, byte-aligned TABLE at
        // 0xD12 (the reference map's segment table).
        const std::uint32_t align = alignment_of(segment.alignment);
        image_size = (image_size + align - 1) & ~(align - 1);
        segment.base = image_size;
        segment.frame = static_cast<std::uint16_t>(image_size / 16);
        image_size += segment.length;
    }

    if (std::getenv("DOSBASE_LINK_DEBUG") != nullptr) {
        for (const Combined& segment : combined) {
            std::fprintf(stderr, "combined %-12s %-10s base %05x len %05x\n",
                         segment.name.c_str(), segment.class_name.c_str(),
                         segment.base, segment.length);
        }
        for (std::size_t m = 0; m < modules.size(); ++m) {
            for (std::size_t s = 0; s < placements[m].size(); ++s) {
                const Placement& place = placements[m][s];
                if (place.absolute ||
                    modules[m].segments[s].length == 0) {
                    continue;
                }
                std::fprintf(stderr, "piece %-14s %-10s at %05x len %05x\n",
                             modules[m].name.c_str(),
                             modules[m].segments[s].name.c_str(),
                             combined[place.combined].base + place.offset,
                             modules[m].segments[s].length);
            }
        }
    }

    // Groups merge by name across modules; a group's frame is its lowest
    // member's.
    struct LinkedGroup {
        std::string name;
        std::uint16_t frame = 0xFFFF;
    };
    std::vector<LinkedGroup> groups;
    std::vector<std::vector<std::size_t>> module_groups(modules.size());
    for (std::size_t m = 0; m < modules.size(); ++m) {
        for (const ModuleGroup& group : modules[m].groups) {
            std::size_t index = groups.size();
            for (std::size_t g = 0; g < groups.size(); ++g) {
                if (groups[g].name == group.name) {
                    index = g;
                    break;
                }
            }
            if (index == groups.size()) groups.push_back({group.name, 0xFFFF});
            module_groups[m].push_back(index);
            for (std::size_t member : group.segments) {
                const Placement& place = placements[m][member];
                if (!place.absolute) {
                    groups[index].frame = std::min(groups[index].frame,
                                                   combined[place.combined].frame);
                }
            }
        }
    }

    // ------------------------------------------------------------ publics
    // The displayed spelling is the FIRST encounter of the name in object
    // order, extern or public alike -- MSBIO's map says AUXDEV (msbio1's
    // EXTDEF) though the defining PUBDEF spells AuxDev.
    std::map<std::string, std::string> first_spelling;
    for (const Module& module : modules) {
        for (const ModuleExternal& item : module.externals) {
            first_spelling.emplace(upper(item.name), item.name);
        }
        for (const ModulePublic& item : module.publics) {
            if (!item.local) first_spelling.emplace(upper(item.name), item.name);
        }
    }
    std::map<std::string, ResolvedPublic> publics;  // keyed uppercased
    std::vector<std::string> public_order;          // encounter order (keys)
    for (std::size_t m = 0; m < modules.size(); ++m) {
        for (const ModulePublic& item : modules[m].publics) {
            if (item.local) continue;  // LPUBDEF stays out of the link
            ResolvedPublic entry;
            entry.name = first_spelling.at(upper(item.name));
            if (item.segment == SIZE_MAX) {
                // an absolute PUBDEF: the explicit frame is the address
                entry.absolute = true;
                entry.frame = item.frame;
                entry.offset = static_cast<std::uint16_t>(item.offset);
                entry.linear = item.frame * 16u + item.offset;
                if (!publics.contains(upper(item.name))) {
                    public_order.push_back(upper(item.name));
                }
                publics[upper(item.name)] = entry;
                continue;
            }
            const Placement& place = placements[m][item.segment];
            if (place.absolute) {
                entry.absolute = true;
                entry.frame = place.absolute_frame;
                entry.offset = static_cast<std::uint16_t>(item.offset);
                entry.linear = place.absolute_frame * 16u + item.offset;
            } else {
                const Combined& home = combined[place.combined];
                const std::uint32_t linear = home.base + place.offset + item.offset;
                entry.frame = home.frame;
                if (item.group != SIZE_MAX) {
                    entry.frame = groups[module_groups[m][item.group]].frame;
                }
                entry.offset = static_cast<std::uint16_t>(linear - entry.frame * 16u);
                entry.linear = linear;
            }
            if (!publics.contains(upper(item.name))) {
                    public_order.push_back(upper(item.name));
                }
                publics[upper(item.name)] = entry;
        }
    }
    // LPUBDEF names are a module-local scope: the module's own LEXTDEF
    // references bind to them (SLIBCE's output.c prints through its local
    // iprint/putbuf helpers).
    std::vector<std::map<std::string, ResolvedPublic>> local_publics(
        modules.size());
    for (std::size_t m = 0; m < modules.size(); ++m) {
        for (const ModulePublic& item : modules[m].publics) {
            if (!item.local || item.segment == SIZE_MAX) continue;
            const Placement& place = placements[m][item.segment];
            ResolvedPublic entry;
            entry.name = item.name;
            if (place.absolute) {
                entry.absolute = true;
                entry.frame = place.absolute_frame;
                entry.offset = static_cast<std::uint16_t>(item.offset);
                entry.linear = place.absolute_frame * 16u + item.offset;
            } else {
                const Combined& home = combined[place.combined];
                const std::uint32_t linear = home.base + place.offset + item.offset;
                entry.frame = home.frame;
                if (item.group != SIZE_MAX) {
                    entry.frame = groups[module_groups[m][item.group]].frame;
                }
                entry.offset = static_cast<std::uint16_t>(linear - entry.frame * 16u);
                entry.linear = linear;
            }
            local_publics[m][upper(item.name)] = entry;
        }
    }
    if (dosseg) {
        std::uint16_t dgroup_frame = 0;
        for (const LinkedGroup& group : groups) {
            if (upper(group.name) == "DGROUP") dgroup_frame = group.frame;
        }
        const auto define = [&](const std::string& name, std::uint32_t linear) {
            const std::string key = upper(name);
            if (publics.contains(key)) return;
            ResolvedPublic entry;
            entry.name = first_spelling.contains(key) ? first_spelling.at(key)
                                                      : name;
            entry.frame = dgroup_frame;
            entry.linear = linear;
            entry.offset = static_cast<std::uint16_t>(linear - dgroup_frame * 16u);
            public_order.push_back(key);
            publics[key] = entry;
        };
        std::uint32_t bss_start = 0;
        std::uint32_t bss_end = 0;
        bool seen_bss = false;
        for (const Combined& segment : combined) {
            if (upper(segment.class_name) != "BSS") continue;
            if (!seen_bss) bss_start = segment.base;
            seen_bss = true;
            bss_end = segment.base + segment.length;
        }
        if (seen_bss) {
            define("_edata", bss_start);
            // _end rounds the break up to a paragraph (ATTRIB: BSS ends
            // at 0xda4, _end reads 0xdb0).
            define("_end", (bss_end + 15u) & ~15u);
        } else {
            // No BSS anywhere (EXE2BIN under /DOSSEG): the pair still
            // exists, resolved to absolute zero, referenced or not.
            for (const char* name : {"_edata", "_end"}) {
                const std::string key = upper(name);
                if (publics.contains(key)) continue;
                ResolvedPublic entry;
                entry.name = first_spelling.contains(key)
                                 ? first_spelling.at(key)
                                 : std::string(name);
                entry.absolute = true;
                public_order.push_back(key);
                publics[key] = entry;
            }
        }
        for (const auto& [name, offset] : communal_layout) {
            define(name, combined[c_common_index].base + offset);
            if (std::getenv("DOSBASE_LINK_DEBUG") != nullptr) {
                std::fprintf(stderr, "communal %-20s at %05x dgrel %04x\n",
                             name.c_str(),
                             combined[c_common_index].base + offset,
                             combined[c_common_index].base + offset -
                                 dgroup_frame * 16u);
            }
        }
    } else {
        for (const auto& [name, offset] : communal_layout) {
            const std::string key = upper(name);
            if (publics.contains(key)) continue;
            ResolvedPublic entry;
            entry.name = first_spelling.contains(key) ? first_spelling.at(key)
                                                      : name;
            const std::uint32_t linear = combined[c_common_index].base + offset;
            entry.frame = combined[c_common_index].frame;
            entry.linear = linear;
            entry.offset = static_cast<std::uint16_t>(linear - entry.frame * 16u);
            public_order.push_back(key);
            publics[key] = entry;
        }
        // Without DOSSEG the same linker symbols resolve to absolute zero
        // (EXE2BIN.MAP lists them as `Abs` 0000:0000).
        for (const Module& module : modules) {
            for (const ModuleExternal& item : module.externals) {
                const std::string key = upper(item.name);
                if (key != "_EDATA" && key != "_END") continue;
                if (publics.contains(key)) continue;
                ResolvedPublic entry;
                entry.name = first_spelling.at(key);
                entry.absolute = true;
                entry.frame = 0;
                entry.offset = 0;
                entry.linear = 0;
                public_order.push_back(key);
                publics[key] = entry;
            }
        }
    }

    // -------------------------------------------------------------- image
    std::vector<std::uint8_t> image(image_size, 0);
    std::uint32_t initialized_end = 0;
    std::vector<bool> combined_has_data(combined.size(), false);
    for (std::size_t m = 0; m < modules.size(); ++m) {
        for (const DataChunk& chunk : modules[m].data) {
            const Placement& place = placements[m][chunk.segment];
            if (place.absolute) continue;
            combined_has_data[place.combined] = true;
            const Combined& home = combined[place.combined];
            const std::uint32_t at = home.base + place.offset + chunk.offset;
            std::copy(chunk.bytes.begin(), chunk.bytes.end(), image.begin() + at);
            initialized_end = std::max(
                initialized_end, at + static_cast<std::uint32_t>(chunk.bytes.size()));
        }
    }

    // ------------------------------------------------------------- fixups
    struct Reloc {
        std::uint32_t linear;
        std::uint16_t frame;  // of the combined segment holding the word
    };
    std::vector<Reloc> relocations;
    const auto read16 = [&image](std::uint32_t index) {
        return static_cast<std::uint16_t>(image[index] | (image[index + 1] << 8));
    };
    const auto write16 = [&image](std::uint32_t index, std::uint16_t value) {
        image[index] = static_cast<std::uint8_t>(value & 0xFF);
        image[index + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    for (std::size_t m = 0; m < modules.size(); ++m) {
        const Module& module = modules[m];
        for (const Fixup& fixup : module.fixups) {
            const Placement& home = placements[m][fixup.segment];
            if (home.absolute) continue;
            const Combined& container = combined[home.combined];
            const std::uint32_t at = container.base + home.offset + fixup.offset;
            std::uint32_t target_linear = 0;
            std::uint16_t target_frame = 0;
            bool target_relocatable = false;
            bool target_absolute = false;
            bool resolved = true;
            const std::uint32_t addend =
                fixup.has_displacement ? fixup.displacement : 0;
            switch (fixup.target.kind) {
                case FixupRefKind::group: {
                    const LinkedGroup& group =
                        groups[module_groups[m][fixup.target.index]];
                    target_frame = group.frame;
                    target_linear = group.frame * 16u + addend;
                    target_relocatable = true;
                    break;
                }
                case FixupRefKind::segment: {
                    const Placement& place = placements[m][fixup.target.index];
                    if (place.absolute) {
                        target_frame = place.absolute_frame;
                        target_linear = place.absolute_frame * 16u + addend;
                        target_absolute = true;
                    } else {
                        const Combined& target = combined[place.combined];
                        target_frame = target.frame;
                        target_linear = target.base + place.offset + addend;
                        target_relocatable = true;
                    }
                    break;
                }
                case FixupRefKind::external: {
                    const ModuleExternal& external =
                        module.externals[fixup.target.index];
                    const std::string key = upper(external.name);
                    const ResolvedPublic* hit = nullptr;
                    if (external.local) {
                        const auto local = local_publics[m].find(key);
                        if (local != local_publics[m].end()) hit = &local->second;
                    }
                    if (hit == nullptr) {
                        const auto found = publics.find(key);
                        if (found != publics.end()) hit = &found->second;
                    }
                    if (hit == nullptr) {
                        fail("unresolved external " + external.name);
                        resolved = false;
                        break;
                    }
                    target_frame = hit->frame;
                    target_linear = hit->linear + addend;
                    target_relocatable = !hit->absolute;
                    target_absolute = hit->absolute;
                    break;
                }
                default:
                    fail("fixup target kind beyond segment/external");
                    resolved = false;
            }
            if (!resolved) continue;
            // The FRAME reference overrides the target's own frame for
            // frame-relative offsets and base writes (F_GRP in the DOS
            // kernel's DOSGROUP references).
            if (target_absolute) {
                // An absolute target keeps its own frame: DEBUG's REGDIF
                // (Abs 0:016a) resolves to 0x16a although the fixup names
                // group DG as the frame.
            } else if (fixup.frame.kind == FixupRefKind::group) {
                target_frame = groups[module_groups[m][fixup.frame.index]].frame;
            } else if (fixup.frame.kind == FixupRefKind::segment) {
                const Placement& place = placements[m][fixup.frame.index];
                target_frame = place.absolute ? place.absolute_frame
                                              : combined[place.combined].frame;
            }
            switch (fixup.location) {
                case 5:    // loader-resolved offset16: same bytes in an EXE
                case 1: {  // offset16
                    if (fixup.segment_relative) {
                        write16(at,
                                static_cast<std::uint16_t>(read16(at) + target_linear -
                                                           target_frame * 16u));
                    } else {
                        write16(at, static_cast<std::uint16_t>(read16(at) +
                                                               target_linear - (at + 2)));
                    }
                    break;
                }
                case 9: {  // offset32
                    const std::uint32_t cur =
                        static_cast<std::uint32_t>(image[at]) |
                        (static_cast<std::uint32_t>(image[at + 1]) << 8) |
                        (static_cast<std::uint32_t>(image[at + 2]) << 16) |
                        (static_cast<std::uint32_t>(image[at + 3]) << 24);
                    const std::uint32_t val = fixup.segment_relative
                        ? (cur + target_linear - target_frame * 16u)
                        : (cur + target_linear - (at + 4));
                    image[at] = static_cast<std::uint8_t>(val & 0xFF);
                    image[at + 1] = static_cast<std::uint8_t>((val >> 8) & 0xFF);
                    image[at + 2] = static_cast<std::uint8_t>((val >> 16) & 0xFF);
                    image[at + 3] = static_cast<std::uint8_t>((val >> 24) & 0xFF);
                    break;
                }
                case 2: {  // base
                    write16(at, static_cast<std::uint16_t>(read16(at) + target_frame));
                    if (target_relocatable) {
                        relocations.push_back({at, container.frame});
                    }
                    break;
                }
                case 3: {  // pointer32
                    write16(at, static_cast<std::uint16_t>(read16(at) + target_linear -
                                                           target_frame * 16u));
                    write16(at + 2,
                            static_cast<std::uint16_t>(read16(at + 2) + target_frame));
                    if (target_relocatable) {
                        relocations.push_back({at + 2, container.frame});
                    }
                    break;
                }
                case 0: {  // lobyte of the frame-relative offset
                    image[at] = static_cast<std::uint8_t>(
                        image[at] + ((target_linear - target_frame * 16u) & 0xFF));
                    break;
                }
                case 4: {  // hibyte
                    image[at] = static_cast<std::uint8_t>(
                        image[at] + (((target_linear - target_frame * 16u) >> 8) & 0xFF));
                    break;
                }
                default:
                    fail("fixup location beyond lo/hi/offset16/base/pointer32");
            }
        }
    }
    if (!result.errors.empty()) return result;
    // Relocation entries stay in fixup-processing order (module by
    // module): EDLIN's table leads with module 0's high segment-base
    // write, which a by-address sort would put last.

    // -------------------------------------------------------------- start
    std::uint16_t start_cs = 0;
    std::uint16_t start_ip = 0;
    bool have_start = false;
    for (std::size_t m = 0; m < modules.size(); ++m) {
        const ModuleEnd& end = modules[m].end;
        if (!end.has_start || have_start) continue;
        have_start = true;
        if (end.start_target.kind != FixupRefKind::segment) {
            fail("start address beyond a segment target is not implemented");
            return result;
        }
        const Placement& place = placements[m][end.start_target.index];
        const std::uint32_t disp =
            end.start_has_displacement ? end.start_displacement : 0;
        if (place.absolute) {
            start_cs = place.absolute_frame;
            start_ip = static_cast<std::uint16_t>(disp);
        } else {
            const Combined& home = combined[place.combined];
            const std::uint32_t linear = home.base + place.offset + disp;
            std::uint16_t frame = home.frame;
            // The MODEND frame reference canonicalizes cs:ip -- CHKDSK's
            // entry prints as DG-relative 0007:2210.
            if (end.start_frame.kind == FixupRefKind::group) {
                frame = groups[module_groups[m][end.start_frame.index]].frame;
            } else if (end.start_frame.kind == FixupRefKind::segment) {
                const Placement& fplace =
                    placements[m][end.start_frame.index];
                frame = fplace.absolute ? fplace.absolute_frame
                                        : combined[fplace.combined].frame;
            }
            start_cs = frame;
            start_ip = static_cast<std::uint16_t>(linear - frame * 16u);
        }
    }

    // ------------------------------------------------------------- header
    // The file ends with the last non-empty segment; alignment padding
    // for trailing zero-length segments is never written and its
    // paragraphs move to min-alloc (COMMAND.EXE: TRANTAIL/FOR_SEGMENT's
    // twelve pad bytes become min_extra 1).
    // The file ends with the last data-bearing combined segment;
    // alignment padding for trailing zero-length or dataless segments is
    // never written and its paragraphs move to min-alloc.
    std::uint32_t kept = 0;
    for (std::size_t index = 0; index < combined.size(); ++index) {
        const Combined& segment = combined[index];
        if (segment.length > 0 && combined_has_data[index]) {
            kept = std::max(kept, segment.base + segment.length);
        }
    }
    if (kept == 0) kept = image_size;
    (void)initialized_end;
    const std::uint32_t reloc_bytes =
        static_cast<std::uint32_t>(relocations.size()) * 4;
    std::uint32_t header_size = 0x1E + reloc_bytes;
    header_size = (header_size + 511) & ~511u;
    const std::uint32_t file_size = header_size + kept;
    std::vector<std::uint8_t> exe(file_size, 0);
    const auto put16 = [&exe](std::size_t index, std::uint16_t value) {
        exe[index] = static_cast<std::uint8_t>(value & 0xFF);
        exe[index + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    put16(0x00, 0x5A4D);
    put16(0x02, static_cast<std::uint16_t>(file_size % 512));
    put16(0x04, static_cast<std::uint16_t>((file_size + 511) / 512));
    put16(0x06, static_cast<std::uint16_t>(relocations.size()));
    put16(0x08, static_cast<std::uint16_t>(header_size / 16));
    put16(0x0C, 0xFFFF);                           // max extra
    std::uint16_t stack_ss = 0;
    std::uint16_t stack_sp = 0;
    for (std::size_t m = 0; m < modules.size(); ++m) {
        for (std::size_t s = 0; s < modules[m].segments.size(); ++s) {
            if (modules[m].segments[s].combine != 5) continue;
            const Placement& place = placements[m][s];
            if (place.absolute) continue;
            const Combined& home = combined[place.combined];
            stack_ss = home.frame;
            stack_sp = static_cast<std::uint16_t>(home.base + home.length -
                                                  home.frame * 16u);
        }
    }
    std::uint32_t memory_end = image_size;
    if (stack_override != 0) {
        // /STACK:n replaces the stack length: sp moves to n (rounded
        // even) and the min-alloc grows to cover the taller stack.
        stack_sp = static_cast<std::uint16_t>((stack_override + 1U) & ~1U);
        memory_end = std::max(memory_end,
                              stack_ss * 16u + static_cast<std::uint32_t>(stack_sp));
    }
    put16(0x0A, static_cast<std::uint16_t>((memory_end - kept + 15) / 16));
    put16(0x0E, stack_ss);
    put16(0x10, stack_sp);
    put16(0x12, 0);                                // checksum, below
    put16(0x14, start_ip);
    put16(0x16, start_cs);
    put16(0x18, 0x001E);                           // relocation table
    put16(0x1A, 0);                                // overlay
    put16(0x1C, 0x0001);  // LINK's fingerprint word, relocations or not
    for (std::size_t r = 0; r < relocations.size(); ++r) {
        // Each entry is (offset within its combined segment's frame, that
        // frame), ordered by linear address -- measured off MSBIO's table.
        put16(0x1E + r * 4, static_cast<std::uint16_t>(relocations[r].linear -
                                                       relocations[r].frame * 16u));
        put16(0x20 + r * 4, relocations[r].frame);
    }
    std::copy(image.begin(), image.begin() + kept, exe.begin() + header_size);

    std::uint16_t sum = 0;
    for (std::size_t i = 0; i + 1 < exe.size(); i += 2) {
        sum = static_cast<std::uint16_t>(sum + (exe[i] | (exe[i + 1] << 8)));
    }
    if (exe.size() % 2 != 0) sum = static_cast<std::uint16_t>(sum + exe.back());
    put16(0x12, static_cast<std::uint16_t>(0xFFFF - sum));

    if (exepack) {
        // /E: the packed executable replaces the plain one. Measured off
        // EXE2BIN.EXE: the image keeps a verbatim prefix up to its first
        // eight-byte run, the rest becomes backward-read fill (B0/B1) and
        // copy (B2/B3) commands, 0xFF-padded to a paragraph; the stub
        // area holds the 16-byte packed header, the unpacker, and the
        // sixteen per-64K relocation sections.
        const std::uint32_t dest_bytes = (kept + 15U) & ~15U;
        std::vector<std::uint8_t> plain(image.begin(), image.begin() + kept);
        std::size_t prefix = plain.size();
        for (std::size_t at = 0; at + 8 <= plain.size(); ++at) {
            bool run = true;
            for (std::size_t j = 1; j < 8; ++j) {
                run = run && plain[at + j] == plain[at];
            }
            if (run) {
                prefix = at;
                break;
            }
        }
        std::vector<std::uint8_t> packed(plain.begin(),
                                         plain.begin() + prefix);
        std::size_t at = prefix;
        bool first_command = true;
        bool overlap = false;
        const auto emit = [&](std::uint8_t opcode, std::uint32_t length) {
            packed.push_back(static_cast<std::uint8_t>(length & 0xFF));
            packed.push_back(static_cast<std::uint8_t>(length >> 8));
            packed.push_back(static_cast<std::uint8_t>(
                opcode | (first_command ? 1 : 0)));
            first_command = false;
        };
        while (at < plain.size()) {
            std::size_t run_end = at + 1;
            while (run_end < plain.size() && plain[run_end] == plain[at] &&
                   run_end - at < 0x4000u) {
                ++run_end;
            }
            if (run_end - at >= 8) {
                packed.push_back(plain[at]);
                emit(0xB0, static_cast<std::uint32_t>(run_end - at));
                at = run_end;
                if (packed.size() > at) {
                    overlap = true;
                    break;
                }
                continue;
            }
            std::size_t copy_end = at;
            while (copy_end < plain.size() && copy_end - at < 0x4000u) {
                std::size_t probe = copy_end + 1;
                while (probe < plain.size() && plain[probe] == plain[copy_end]) {
                    ++probe;
                }
                if (probe - copy_end >= 8) break;
                copy_end = probe;
            }
            packed.insert(packed.end(), plain.begin() + at,
                          plain.begin() + copy_end);
            emit(0xB2, static_cast<std::uint32_t>(copy_end - at));
            at = copy_end;
            if (packed.size() > at) {
                overlap = true;
                break;
            }
        }
        if (!overlap) {
        if (dest_bytes > kept) {
            // The paragraph pad goes out as its own fill command
            // repeating the final content byte (FIND's closing 0x0d).
            packed.push_back(plain.empty() ? 0 : plain.back());
            emit(0xB0, dest_bytes - kept);
        }
        while (packed.size() % 16 != 0) packed.push_back(0xFF);
        const auto packed_paras = static_cast<std::uint16_t>(packed.size() / 16);
        std::vector<std::uint16_t> sections[16];
        for (const Reloc& reloc : relocations) {
            sections[reloc.linear >> 16].push_back(
                static_cast<std::uint16_t>(reloc.linear & 0xFFFF));
        }
        std::vector<std::uint8_t> stub;
        const auto stub16 = [&stub](std::uint16_t value) {
            stub.push_back(static_cast<std::uint8_t>(value & 0xFF));
            stub.push_back(static_cast<std::uint8_t>(value >> 8));
        };
        std::uint32_t exepack_size = 16 + sizeof kExepackStub;
        for (auto& section : sections) exepack_size += 2 + 2 * section.size();
        stub16(start_ip);
        stub16(start_cs);
        stub16(0);  // scratch for the unpacker's mem_start
        stub16(static_cast<std::uint16_t>(exepack_size));
        stub16(stack_sp);
        stub16(stack_ss);
        stub16(static_cast<std::uint16_t>(dest_bytes / 16));
        stub.push_back('R');
        stub.push_back('B');
        stub.insert(stub.end(), kExepackStub,
                    kExepackStub + sizeof kExepackStub);
        for (auto& section : sections) {
            std::sort(section.begin(), section.end());
            stub16(static_cast<std::uint16_t>(section.size()));
            for (std::uint16_t offset : section) stub16(offset);
        }
        const std::uint32_t packed_file =
            512 + static_cast<std::uint32_t>(packed.size() + stub.size());
        std::vector<std::uint8_t> packed_exe(packed_file, 0);
        const auto pput16 = [&packed_exe](std::size_t index,
                                          std::uint16_t value) {
            packed_exe[index] = static_cast<std::uint8_t>(value & 0xFF);
            packed_exe[index + 1] = static_cast<std::uint8_t>(value >> 8);
        };
        pput16(0x00, 0x5A4D);
        pput16(0x02, static_cast<std::uint16_t>(packed_file % 512));
        pput16(0x04, static_cast<std::uint16_t>((packed_file + 511) / 512));
        pput16(0x06, 0);
        pput16(0x08, 32);
        const std::uint32_t packed_ss =
            dest_bytes / 16 + (exepack_size + 15) / 16;
        // Min-alloc covers whichever stack tops out higher: the packed
        // loader's (FIND) or the unpacked program's own (EXE2BIN).
        const std::uint32_t packed_top =
            std::max(packed_ss * 16u + 0x80u,
                     stack_ss * 16u + static_cast<std::uint32_t>(stack_sp));
        pput16(0x0A, static_cast<std::uint16_t>(
                         (packed_top - (packed_file - 512)) / 16));
        pput16(0x0C, 0xFFFF);
        pput16(0x0E, static_cast<std::uint16_t>(packed_ss));
        pput16(0x10, 0x80);
        pput16(0x14, 0x10);
        pput16(0x16, packed_paras);
        pput16(0x18, 0x001E);
        pput16(0x1C, 0x0001);
        std::copy(packed.begin(), packed.end(), packed_exe.begin() + 512);
        std::copy(stub.begin(), stub.end(),
                  packed_exe.begin() + 512 + packed.size());
        exe = std::move(packed_exe);
        }
    }

    // ------------------------------------------------------------ the map
    std::string map;
    bool has_stack = false;
    for (const Module& module : modules) {
        for (const ModuleSegment& segment : module.segments) {
            if (segment.combine == 5) has_stack = true;
        }
    }
    if (stack_override != 0) {
        // RESTORE.MAP opens with LINK's acknowledgment of /STACK.
        map += "Stack Allocation = " + std::to_string(stack_override) +
               " bytes\r\n";
    }
    if (!has_stack) map += "LINK : warning L4021: no stack segment\r\n";
    map += "\r\n Start  Stop   Length Name                   Class\r\n";
    char line[160];
    for (const Combined& segment : combined) {
        std::uint32_t shown = segment.length;
        if (stack_override != 0 && segment.class_name == "STACK") {
            // RESTORE.MAP's STACK row shows the /STACK size, not the
            // declared segment's.
            shown = stack_override;
        }
        const std::uint32_t stop =
            shown == 0 ? segment.base : segment.base + shown - 1;
        std::string padded = segment.name;
        if (padded.size() < 23) padded.resize(23, ' ');
        std::snprintf(line, sizeof line, " %05XH %05XH %05XH %s%s\r\n", segment.base,
                      stop, shown, padded.c_str(),
                      segment.class_name.c_str());
        map += line;
    }
    if (!groups.empty()) {
        map += "\r\n Origin   Group\r\n";
        for (const LinkedGroup& group : groups) {
            std::snprintf(line, sizeof line, " %04X:%X   %s\r\n", group.frame, 0,
                          group.name.c_str());
            map += line;
        }
    }
    if (map_publics) {
        map += "\r\n  Address         Publics by Name\r\n\r\n";
        for (const auto& [key, entry] : publics) {
            std::snprintf(line, sizeof line, " %04X:%04X%s%s\r\n", entry.frame,
                          entry.offset, entry.absolute ? "  Abs  " : "       ",
                          entry.name.c_str());
            map += line;
        }
        // "Publics by Value": LINK sorts the symbol-table WALK order by linear
        // address with its own unstable qsort (link_qsort). The walk order is the
        // hash-bucket order (link_bucket, 0..255) with each bucket listed newest-
        // first -- the reverse of the order names were first entered into the
        // symbol table. That entry order is the record-stream order of a name's
        // first PUBDEF/EXTDEF/COMDEF mention, over modules in link order.
        std::map<std::string, std::uint32_t> first_mention;
        {
            std::uint32_t seq = 0;
            for (const Module& module : modules) {
                std::vector<std::pair<std::uint32_t, std::string>> items;
                for (const ModuleExternal& e : module.externals) {
                    items.emplace_back(e.mention, upper(e.name));
                }
                for (const ModulePublic& p : module.publics) {
                    if (!p.local) items.emplace_back(p.mention, upper(p.name));
                }
                std::sort(items.begin(), items.end());
                for (const auto& item : items) {
                    first_mention.emplace(item.second, seq);
                    ++seq;
                }
            }
        }
        std::vector<const ResolvedPublic*> by_value;
        {
            // A name never mentioned in an object (linker-generated: communals,
            // the DOSSEG _edata/_end pair) is entered last, so it sits newest in
            // its bucket; rank those after every real mention, by encounter.
            std::uint32_t after = 0;
            for (const auto& fm : first_mention) {
                after = std::max(after, fm.second);
            }
            ++after;
            struct Walk {
                std::uint8_t bucket;
                std::uint32_t rank;
                const ResolvedPublic* entry;
            };
            std::vector<Walk> order;
            std::uint32_t enc = 0;
            for (const std::string& key : public_order) {
                const auto it = first_mention.find(key);
                const std::uint32_t rank =
                    it != first_mention.end() ? it->second : after + enc;
                order.push_back({link_bucket(key), rank, &publics.at(key)});
                ++enc;
            }
            std::sort(order.begin(), order.end(),
                      [](const Walk& x, const Walk& y) {
                          if (x.bucket != y.bucket) return x.bucket < y.bucket;
                          return x.rank > y.rank;  // newest first (front insertion)
                      });
            for (const Walk& w : order) by_value.push_back(w.entry);
        }
        // LINK's qsort (0x1097e) first scans for any out-of-order adjacent pair
        // and, finding none, leaves an already-sorted array untouched. Mirror
        // that so an already-address-sorted walk keeps its order verbatim.
        const bool already_sorted =
            std::is_sorted(by_value.begin(), by_value.end(),
                           [](const ResolvedPublic* a, const ResolvedPublic* b) {
                               return a->linear < b->linear;
                           });
        if (!by_value.empty() && !already_sorted) {
            link_qsort(by_value, 0,
                       static_cast<std::ptrdiff_t>(by_value.size()) - 1);
        }
        map += "\r\n  Address         Publics by Value\r\n\r\n";
        for (const ResolvedPublic* entry : by_value) {
            std::snprintf(line, sizeof line, " %04X:%04X%s%s\r\n", entry->frame,
                          entry->offset, entry->absolute ? "  Abs  " : "       ",
                          entry->name.c_str());
            map += line;
        }
    }
    std::snprintf(line, sizeof line, "\r\nProgram entry point at %04X:%04X\r\n",
                  start_cs, start_ip);
    map += line;

    result.map = std::move(map);
    result.exe = std::move(exe);
    return result;
}

}  // namespace dosbase::devtools
