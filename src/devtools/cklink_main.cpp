// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// LINK.EXE compatibility driver. Accepts the reference tool's command forms:
// `link msboot;` and
// `link @response.lnk`, with the response grammar of link_response.hpp.
#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/link_image.hpp"
#include "dosbase/devtools/link_response.hpp"
#include "dosbase/devtools/omf_library.hpp"
#include "dosbase/devtools/omf_record.hpp"

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cctype>
#include <map>
#include <set>
#include <utility>
#include <algorithm>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace dosbase::devtools;
    std::string command;
    for (int i = 1; i < argc; ++i) {
        if (!command.empty()) command += ' ';
        command += argv[i];
    }
    try {
        std::string response_text = command;
        const std::size_t at_pos = command.find('@');
        if (at_pos != std::string::npos &&
            (at_pos == 0 || command[at_pos - 1] == ' ')) {
            std::string prefix_switches = command.substr(0, at_pos);
            std::string response_name = command.substr(at_pos + 1);
            while (!response_name.empty() &&
                   (response_name.back() == ';' || response_name.back() == ' ')) {
                response_name.pop_back();
            }
            const std::vector<std::uint8_t> raw =
                read_file(resolve_dos_input(response_name).string());
            response_text.assign(raw.begin(), raw.end());
            if (!prefix_switches.empty()) {
                response_text = prefix_switches + "\n" + response_text;
            }
        }
        const LinkResponse response = parse_link_response(response_text);
        std::vector<Module> modules;
        // COMENT class 9F names a default library (compiled C: SLIBCE);
        // LINK appends those after the response's own libraries and finds
        // them through the LIB environment path.
        std::vector<std::string> default_libraries;
        for (const std::string& object : response.objects) {
            std::string name = object;
            const std::size_t slash = name.find_last_of("\\/");
            const std::size_t dot = name.rfind('.');
            if (dot == std::string::npos ||
                (slash != std::string::npos && dot < slash)) {
                name += ".obj";
            }
            const std::vector<std::uint8_t> raw =
                read_file(resolve_dos_input(name).string());
            const std::vector<OmfRecord> records = parse_omf_records(raw, 0, true);
            for (const OmfRecord& record : records) {
                if (record.type != static_cast<std::uint8_t>(OmfType::coment)) continue;
                if (record.body.size() < 3 || record.body[1] != 0x9F) continue;
                default_libraries.emplace_back(record.body.begin() + 2,
                                               record.body.end());
            }
            modules.push_back(parse_module(records));
        }
        // Libraries resolve by pulling members for unresolved externals, in
        // encounter order, until a fixpoint. A bare name searches the
        // working directory and then each LIB directory, guest-absolute
        // entries resolving against the DOS filesystem root.
        const auto find_library = [](const std::string& spec) {
            std::string name = spec;
            if (name.find('.') == std::string::npos ||
                name.rfind('.') < name.find_last_of("\\/") + 1) {
                name += ".lib";
            }
            std::error_code missing;
            std::filesystem::path direct = resolve_dos_input(name);
            if (std::filesystem::is_regular_file(direct, missing)) return direct;
            if (name.find_first_of("\\/") == std::string::npos) {
                const char* lib_path = std::getenv("LIB");
                std::string remaining = lib_path != nullptr ? lib_path : "";
                while (!remaining.empty()) {
                    const std::size_t cut = remaining.find(';');
                    std::string directory = remaining.substr(0, cut);
                    remaining = cut == std::string::npos ? std::string()
                                                         : remaining.substr(cut + 1);
                    if (directory.empty()) continue;
                    if (directory.size() >= 2 && directory[1] == ':') {
                        if (const char* root = std::getenv("DOSBASE_DOS_ROOT"); root != nullptr) {
                            directory = std::string(root) + directory.substr(2);
                        }
#if !defined(_WIN32)
                        else {
                            directory = std::string(".") + directory.substr(2);
                        }
#endif
                    }
                    const std::filesystem::path candidate =
                        resolve_dos_input(directory + "\\" + name);
                    if (std::filesystem::is_regular_file(candidate, missing)) {
                        return candidate;
                    }
                }
            }
            return direct;  // read_file reports the miss
        };
        std::vector<OmfLibrary> libraries;
        std::set<std::string> library_names;
        // Segment placement can differ from the module (pull) order:
        // RESTORE's relocation table follows pull order while the
        // members place per-visit page-sorted, so the two walks split.
        std::vector<std::size_t> module_placement_order;
        for (const std::string& spec : response.libraries) {
            libraries.push_back(OmfLibrary::parse(
                read_file(find_library(spec).string())));
        }
        for (const std::string& spec : default_libraries) {
            std::string key = spec;
            for (char& ch : key) ch = static_cast<char>(std::toupper(ch));
            if (!library_names.insert(key).second) continue;
            libraries.push_back(OmfLibrary::parse(
                read_file(find_library(spec).string())));
        }
        const auto upper = [](std::string text) {
            for (char& ch : text) ch = static_cast<char>(std::toupper(ch));
            return text;
        };
        // /NOE turns the extended-dictionary machinery off for the whole
        // link (BACKUP's response passes it), so decide before resolving.
        bool use_extended_dictionary = true;
        for (const std::string& item : response.switches) {
            std::string folded = item;
            for (char& c : folded) c = static_cast<char>(std::toupper(c));
            if (folded == "/NOE") use_extended_dictionary = false;
        }
        // LIB's extended dictionary (record F2): a count-prefixed entry
        // table of (page, offset) pairs whose offsets index count-prefixed
        // lists of entry numbers -- each member's required members. LINK
        // pulls a member's listed closure outright, which is what makes an
        // extended-dictionary library's block page-sorted and pulls
        // data-only members a symbol-driven search never requests
        // (REPLACE's reference carries 16 zero bytes of such data).
        const auto extended_deps = [](std::span<const std::uint8_t> raw) {
            std::map<std::uint16_t, std::vector<std::uint16_t>> deps;
            if (raw.size() < 5 || raw[0] != 0xF2) return deps;
            const auto word = [&](std::size_t at) {
                return static_cast<std::uint16_t>(raw[at] | raw[at + 1] << 8);
            };
            const std::size_t body = 3;
            const std::uint16_t count = word(body);
            std::vector<std::pair<std::uint16_t, std::uint16_t>> entries;
            for (std::uint16_t k = 0; k < count; ++k) {
                entries.push_back({word(body + 2 + 4u * k),
                                   word(body + 4 + 4u * k)});
            }
            for (const auto& [page, offset] : entries) {
                const std::size_t list = body + 2 + offset;
                if (list + 2 > raw.size()) continue;
                const std::uint16_t wanted = word(list);
                std::vector<std::uint16_t> required;
                for (std::uint16_t k = 0; k < wanted; ++k) {
                    if (list + 4 + 2u * k > raw.size()) break;
                    const std::uint16_t entry = word(list + 2 + 2u * k);
                    if (entry < entries.size()) {
                        required.push_back(entries[entry].first);
                    }
                }
                deps[page] = std::move(required);
            }
            return deps;
        };
        if (!libraries.empty()) {
            // Library resolution, measured off the reference EXEs (MEM,
            // JOIN, SUBST, REPLACE, BACKUP, RESTORE). See the `cycle`
            // block below for the algorithm; the rules it encodes are
            // documented in the compatibility notes.
            std::vector<std::map<std::string, std::uint16_t>> dictionaries;
            std::vector<std::map<std::uint16_t, const LibraryMember*>> pages;
            std::vector<std::map<std::uint16_t, std::vector<std::string>>>
                page_symbols;
            for (const OmfLibrary& library : libraries) {
                std::map<std::string, std::uint16_t> dictionary;
                std::map<std::uint16_t, std::vector<std::string>> by_symbols;
                for (const LibrarySymbol& symbol : library.symbols()) {
                    if (!symbol.name.empty() && symbol.name.back() == '!') continue;
                    std::string key = symbol.name;
                    for (char& ch : key) ch = static_cast<char>(std::toupper(ch));
                    by_symbols[symbol.page].push_back(key);
                    dictionary.emplace(std::move(key), symbol.page);
                }
                dictionaries.push_back(std::move(dictionary));
                page_symbols.push_back(std::move(by_symbols));
                std::map<std::uint16_t, const LibraryMember*> by_page;
                for (const LibraryMember& member : library.members()) {
                    by_page.emplace(static_cast<std::uint16_t>(
                                        member.offset / library.page_size()),
                                    &member);
                }
                pages.push_back(std::move(by_page));
            }
            std::set<std::string> defined;
            std::deque<std::string> queue;   // fifo order
            std::set<std::string> queued;
            const auto enqueue_module = [&](const Module& module) {
                std::set<std::string> own_locals;
                for (const ModulePublic& item : module.publics) {
                    if (item.local) own_locals.insert(upper(item.name));
                }
                for (const ModuleExternal& item : module.externals) {
                    const std::string key = upper(item.name);
                    if (item.local && own_locals.contains(key)) continue;
                    if (queued.insert(key).second) queue.push_back(key);
                }
            };
            for (const Module& module : modules) {
                for (const ModulePublic& item : module.publics) {
                    if (!item.local) defined.insert(upper(item.name));
                }
            }
            for (const Module& module : modules) enqueue_module(module);
            std::set<std::pair<std::size_t, std::uint16_t>> pulled_members;
            std::vector<Module> pulled_modules;
            std::vector<std::pair<std::size_t, std::uint16_t>> pull_keys;
            std::vector<std::size_t> pull_visits;
            std::size_t current_visit = 0;
            const auto pull = [&](std::size_t l, std::uint16_t page) {
                pull_keys.push_back({l, page});
                pull_visits.push_back(current_visit);
                Module pulled_module = parse_module(pages[l].at(page)->records);
                for (const ModulePublic& item : pulled_module.publics) {
                    if (!item.local) defined.insert(upper(item.name));
                }
                enqueue_module(pulled_module);
                pulled_members.insert({l, page});
                pulled_modules.push_back(std::move(pulled_module));
            };
            std::vector<std::map<std::uint16_t, std::vector<std::uint16_t>>>
                library_deps(libraries.size());
            if (use_extended_dictionary) {
                for (std::size_t l = 0; l < libraries.size(); ++l) {
                    library_deps[l] = extended_deps(libraries[l].trailing());
                }
            }
            // Pull a member and, through the extended dictionary, the
            // members it is recorded to require, transitively.
            const auto pull_closure = [&](std::size_t l, std::uint16_t page) {
                std::vector<std::uint16_t> work{page};
                while (!work.empty()) {
                    const std::uint16_t next = work.back();
                    work.pop_back();
                    if (pulled_members.contains({l, next})) continue;
                    if (!pages[l].contains(next)) continue;
                    pull(l, next);
                    const auto slot = library_deps[l].find(next);
                    if (slot == library_deps[l].end()) continue;
                    for (auto it = slot->second.rbegin();
                         it != slot->second.rend(); ++it) {
                        work.push_back(*it);
                    }
                }
            };
            {
                // Measured off MEM/JOIN/REPLACE: cycles over the
                // libraries in command order (defaults appended). Each
                // cycle snapshots the undefined set; each library visit
                // runs page-ascending scans to closure, a member being
                // eligible when one of its publics is in the snapshot
                // or was added by THIS visit's own pulls -- so JOIN's
                // comsubs closes cmsubstr then cmdbcs in cycle 1 while
                // mapper's casemap/dbcs (queued by cmdbcs mid-cycle)
                // wait for cycle 2, landing after the SLIBCE block.
                // Placement groups each library's pulls into one block
                // at the library's first-pull position. A library with
                // an extended dictionary places its block page-sorted
                // (LINK learns the dependency closure up front and
                // reads the members sequentially -- ATTRIB's reference
                // puts dosret/lseek at their page slots even though
                // their triggers only surface from higher-paged pulls,
                // and MEM's custom crt0 lands last at the highest
                // page). Without one (COMSUBS.LIB is the only such
                // library in the corpus) the block keeps pull order:
                // REPLACE places cmrrchr/cmstrchr before the
                // lower-paged cmdbcs they drag in.
                bool progressed = true;
                while (progressed) {
                    progressed = false;
                    std::set<std::string> snapshot;
                    for (const std::string& name : queued) {
                        if (!defined.contains(name)) snapshot.insert(name);
                    }
                    for (std::size_t l = 0; l < libraries.size(); ++l) {
                        ++current_visit;
                        // Each SCAN is gated by a snapshot taken at its
                        // start -- BACKUP's /NOE layers place stdargv a
                        // scan after crt0 even though crt0 queues it at
                        // page 1, before the scan reaches page 251. A
                        // command library's scans grow only from the
                        // cycle snapshot plus this visit's own pulls
                        // (JOIN's mapper waits a cycle for cmdbcs's
                        // doscasemap); a default library's scans see
                        // everything queued before the scan starts
                        // (REPLACE's SLIBCE picks up cmdbcs's _malloc
                        // in the same cycle).
                        const bool live = l >= response.libraries.size();
                        std::set<std::string> visit_added;
                        bool grew = true;
                        while (grew) {
                            grew = false;
                            std::set<std::string> eligible;
                            if (live) {
                                for (const std::string& name : queued) {
                                    if (!defined.contains(name)) {
                                        eligible.insert(name);
                                    }
                                }
                            } else {
                                eligible = snapshot;
                                eligible.insert(visit_added.begin(),
                                                visit_added.end());
                            }
                            const std::size_t before = queue.size();
                            for (const auto& [page, member] : pages[l]) {
                                if (pulled_members.contains({l, page})) continue;
                                bool wanted = false;
                                const auto slot = page_symbols[l].find(page);
                                if (slot != page_symbols[l].end()) {
                                    for (const std::string& symbol :
                                         slot->second) {
                                        if (defined.contains(symbol)) continue;
                                        if (eligible.contains(symbol)) {
                                            wanted = true;
                                            break;
                                        }
                                    }
                                }
                                if (!wanted) continue;
                                pull_closure(l, page);
                                grew = true;
                                progressed = true;
                            }
                            for (std::size_t q = before; q < queue.size();
                                 ++q) {
                                visit_added.insert(queue[q]);
                            }
                        }
                    }
                }
                // Libraries order by their LAST visit that pulled
                // anything: RESTORE pulls mapper's DOS wrappers in
                // cycle 1, yet the whole mapper block sits after
                // SLIBCE's because mapper is the library that pulls
                // again in cycle 2. Within a library the visits stay
                // chronological, and inside one visit an
                // extended-dictionary library reads page-ascending
                // while COMSUBS.LIB (no extended dictionary) keeps
                // pull order.
                std::vector<std::size_t> last_visit(libraries.size(), 0);
                for (std::size_t at = 0; at < pull_keys.size(); ++at) {
                    last_visit[pull_keys[at].first] = pull_visits[at];
                }
                std::vector<std::size_t> placement(pulled_modules.size());
                for (std::size_t at = 0; at < placement.size(); ++at) {
                    placement[at] = at;
                }
                std::stable_sort(
                    placement.begin(), placement.end(),
                    [&](std::size_t left, std::size_t right) {
                        const std::size_t ll = pull_keys[left].first;
                        const std::size_t lr = pull_keys[right].first;
                        if (ll != lr) return last_visit[ll] < last_visit[lr];
                        if (pull_visits[left] != pull_visits[right]) {
                            return pull_visits[left] < pull_visits[right];
                        }
                        if (!use_extended_dictionary ||
                            !libraries[ll].has_extended_dictionary()) {
                            return left < right;
                        }
                        return pull_keys[left].second < pull_keys[right].second;
                    });
                const std::size_t roots = modules.size();
                for (std::size_t at = 0; at < roots; ++at) {
                    module_placement_order.push_back(at);
                }
                for (std::size_t at : placement) {
                    module_placement_order.push_back(roots + at);
                }
                for (Module& module : pulled_modules) {
                    modules.push_back(std::move(module));
                }
            }
        }
        bool map_publics = false;
        bool force_dosseg = false;
        bool exepack = false;
        std::uint32_t stack_override = 0;
        for (const std::string& item : response.switches) {
            std::string folded = item;
            for (char& c : folded) c = static_cast<char>(std::toupper(c));
            if (folded == "/M" || folded == "M" || folded == "/MAP") {
                map_publics = true;
            }
            if (folded == "/DOSSEG") force_dosseg = true;
            if (folded == "/E" || folded == "/EX" || folded == "/EXEPACK") {
                exepack = true;
            }
            if (folded == "/NOE") continue;  // extended dictionary: no effect
            if (folded.rfind("/STACK:", 0) == 0) {
                stack_override = static_cast<std::uint32_t>(
                    std::stoul(folded.substr(7)));
            }
        }
        if (force_dosseg && !modules.empty()) modules.front().dosseg = true;
        const LinkResult result =
            link_modules(modules, map_publics, stack_override, exepack,
                         module_placement_order);
        if (!result.errors.empty()) {
            for (const std::string& error : result.errors) {
                std::cerr << "cklink: " << error << "\n";
            }
            return 1;
        }
        std::string exe_name = default_exe_name(response);
        if (exe_name.find('.') == std::string::npos) exe_name += ".exe";
        const std::filesystem::path path = resolve_dos_output(exe_name);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(result.exe.data()),
                     static_cast<std::streamsize>(result.exe.size()));
        if (!stream) {
            std::cerr << "cklink: cannot write " << path.string() << "\n";
            return 1;
        }
        // A map is written when the field is present, even empty (the
        // `link msload.obj,msload,,;` form) -- a `;` before the field
        // suppresses it. The response struct erases that distinction, so
        // probe the raw text: two-plus top-level commas before the `;`.
        std::size_t commas = 0;
        for (char c : response_text) {
            if (c == ';') break;
            if (c == ',') ++commas;
        }
        const bool map_wanted = !response.map_name.empty() || commas >= 2;
        if (map_wanted) {
            std::string map_name = response.map_name;
            if (map_name.empty()) {
                map_name = exe_name.substr(0, exe_name.rfind('.')) + ".map";
            }
            if (map_name.find('.') == std::string::npos) map_name += ".map";
            std::string device = map_name.substr(0, map_name.rfind('.'));
            for (char& ch : device) ch = static_cast<char>(std::toupper(ch));
            if (device != "NUL") {  // `nul` maps go to the DOS null device
                const std::filesystem::path map_path = resolve_dos_output(map_name);
                std::ofstream map_stream(map_path,
                                         std::ios::binary | std::ios::trunc);
                map_stream.write(result.map.data(),
                                 static_cast<std::streamsize>(result.map.size()));
                if (!map_stream) {
                    std::cerr << "cklink: cannot write " << map_path.string()
                              << "\n";
                    return 1;
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "cklink: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
