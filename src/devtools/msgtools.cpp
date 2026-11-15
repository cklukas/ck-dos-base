// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/msgtools.hpp"

#include "dosbase/devtools/binary.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace dosbase::devtools {
namespace {

constexpr std::string_view kCrlf = "\r\n";
constexpr std::string_view kSeparator =
    "; ----------------------------------------------------------";

std::vector<std::string_view> split_lines(std::string_view raw) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= raw.size()) {
        std::size_t end = raw.find('\n', start);
        if (end == std::string_view::npos) {
            if (start < raw.size()) lines.push_back(raw.substr(start));
            break;
        }
        std::size_t stop = end;
        if (stop > start && raw[stop - 1] == '\r') --stop;
        lines.push_back(raw.substr(start, stop - start));
        start = end + 1;
    }
    return lines;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_lower_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f'); }

// `NAME     oooo cccc` -- name of A-Z0-9 starting with a letter padded into
// eight columns, offset in lowercase hex, count in decimal.
bool parse_section_header(std::string_view line, std::string* name) {
    std::size_t i = 0;
    if (i >= line.size() || !is_upper(line[i])) return false;
    while (i < line.size() && (is_upper(line[i]) || is_digit(line[i]))) ++i;
    const std::size_t name_end = i;
    if (i == 0 || i >= line.size() || line[i] != ' ') return false;
    while (i < line.size() && line[i] == ' ') ++i;
    if (line.size() - i != 9) return false;
    for (std::size_t k = 0; k < 4; ++k) {
        if (!is_lower_hex(line[i + k])) return false;
    }
    if (line[i + 4] != ' ') return false;
    for (std::size_t k = 5; k < 9; ++k) {
        if (!is_digit(line[i + k])) return false;
    }
    if (name != nullptr) *name = std::string(line.substr(0, name_end));
    return true;
}

// `nnnn U ffff <expression>`
bool parse_message_line(std::string_view line, MsgMessage* message) {
    if (line.size() < 11) return false;
    for (std::size_t k = 0; k < 4; ++k) {
        if (!is_digit(line[k])) return false;
    }
    if (line[4] != ' ' || line[6] != ' ') return false;
    for (std::size_t k = 7; k < 11; ++k) {
        if (!is_digit(line[k])) return false;
    }
    if (line.size() > 11 && line[11] != ' ') return false;
    if (message != nullptr) {
        message->number = 0;
        std::from_chars(line.data(), line.data() + 4, message->number);
        message->usage = line[5];
        message->flags = std::string(line.substr(7, 4));
        message->lines = {std::string(line.size() > 12 ? line.substr(12) : std::string_view{})};
    }
    return true;
}

std::string hex_upper(unsigned value, int width) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%0*X", width, value);
    return buffer;
}

std::vector<std::string_view> split_fields(std::string_view line, std::size_t max_fields) {
    std::vector<std::string_view> fields;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) break;
        if (fields.size() + 1 == max_fields) {
            fields.push_back(line.substr(i));
            break;
        }
        std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        fields.push_back(line.substr(start, i - start));
    }
    return fields;
}

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

std::string upper(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return result;
}

std::string join_crlf(const std::vector<std::string>& lines) {
    std::string out;
    std::size_t size = 0;
    for (const std::string& line : lines) size += line.size() + 2;
    out.reserve(size);
    for (const std::string& line : lines) {
        out += line;
        out += kCrlf;
    }
    return out;
}

// A ';' outside quotes starts a comment; both quote characters exist in the
// tree's skeletons. The expression keeps its spelling otherwise.
std::string_view strip_comment(std::string_view line) {
    char quote = '\0';
    std::size_t end = line.size();
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote != '\0') {
            if (c == quote) quote = '\0';
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == ';') {
            end = i;
            break;
        }
    }
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) --end;
    return line.substr(0, end);
}

}  // namespace

const MsgMessage* MsgDatabase::find(std::string_view component, int number) const {
    const std::string wanted = upper(component);
    for (const MsgSection& section : sections) {
        if (section.name != wanted) continue;
        for (const MsgMessage& message : section.messages) {
            if (message.number == number) return &message;
        }
        return nullptr;
    }
    return nullptr;
}

MsgDatabase parse_msg(std::string_view raw) {
    const std::vector<std::string_view> lines = split_lines(raw);
    if (lines.empty()) throw FormatError(0, "empty message file");
    MsgDatabase database;
    database.level = std::string(lines[0]);
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (line.empty()) continue;
        std::string name;
        if (parse_section_header(line, &name)) {
            database.sections.push_back({name, {}});
            continue;
        }
        MsgMessage message;
        if (parse_message_line(line, &message)) {
            if (database.sections.empty()) {
                throw FormatError(i, "message line before the first section header");
            }
            database.sections.back().messages.push_back(std::move(message));
            continue;
        }
        if (!line.empty() && line[0] == '\t') {
            if (database.sections.empty() || database.sections.back().messages.empty()) {
                throw FormatError(i, "continuation line with no message to continue");
            }
            database.sections.back().messages.back().lines.emplace_back(line);
            continue;
        }
        throw FormatError(i, "unrecognized message-file line");
    }
    return database;
}

BuildIdxResult buildidx(std::string_view msg_raw) {
    const MsgDatabase database = parse_msg(msg_raw);
    unsigned level = 0;
    std::from_chars(database.level.data(), database.level.data() + database.level.size(),
                    level, 16);
    char level_text[8];
    std::snprintf(level_text, sizeof level_text, "%04x", level + 1);

    std::vector<std::string_view> lines = split_lines(msg_raw);
    std::vector<std::string> rewritten;
    std::vector<std::string> index_lines;
    rewritten.reserve(lines.size());
    index_lines.push_back(level_text);
    std::size_t position = 0;
    std::size_t next_section = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line;
        std::string name;
        if (i == 0) {
            line = level_text;
        } else if (parse_section_header(lines[i], &name)) {
            const MsgSection& section = database.sections.at(next_section++);
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%-8s %04zx %04zu", section.name.c_str(),
                          position, section.messages.size());
            line = buffer;
            index_lines.push_back(line);
        } else {
            line = std::string(lines[i]);
        }
        position += line.size() + 2;
        rewritten.push_back(std::move(line));
    }
    return {join_crlf(rewritten), join_crlf(index_lines)};
}

std::vector<std::pair<std::string, std::string>> nosrvbld(std::string_view skl_raw,
                                                          std::string_view base_name,
                                                          const MsgDatabase& msg) {
    const std::string base_lower = lower(base_name);
    const std::string base_upper = upper(base_name);
    std::vector<std::pair<std::string, std::vector<std::string>>> files;
    for (std::string_view raw_line : split_lines(skl_raw)) {
        std::string_view line = raw_line;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }
        if (line.empty() || line[0] == ';') continue;
        if (line.rfind(":class", 0) == 0) {
            const std::vector<std::string_view> fields = split_fields(line, 2);
            if (fields.size() < 2) throw FormatError(0, ":class without a class id");
            const std::string id(fields[1]);
            files.push_back({id, {"; " + base_lower + ".cl" + lower(id) + " ", ""}});
            continue;
        }
        const bool is_use = line.rfind(":use", 0) == 0;
        const bool is_def = line.rfind(":def", 0) == 0;
        if (is_use || is_def) {
            if (files.empty()) throw FormatError(0, "directive before the first :class");
            const std::vector<std::string_view> fields = split_fields(line, 4);
            if (fields.size() < 3) throw FormatError(0, "directive with too few fields");
            int number = 0;
            std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), number);
            const std::string component =
                is_use ? std::string(fields[2]) : base_upper;
            const std::string_view label = is_use ? fields[3] : fields[2];
            const MsgMessage* message = msg.find(component, number);
            if (message == nullptr) {
                throw FormatError(0, "no message " + component + " " + std::to_string(number));
            }
            std::vector<std::string>& body = files.back().second;
            body.emplace_back();
            body.emplace_back(";_______________________");
            body.emplace_back();
            body.push_back(std::string(label) + " DB    " + message->lines.front());
            for (std::size_t k = 1; k < message->lines.size(); ++k) {
                body.push_back("\tDB   " + message->lines[k]);
            }
            continue;
        }
        if (line.rfind(":end", 0) == 0) break;
    }
    std::vector<std::pair<std::string, std::string>> rendered;
    rendered.reserve(files.size());
    for (auto& [id, body] : files) {
        rendered.push_back({id, join_crlf(body)});
    }
    return rendered;
}

namespace {

struct SklEntry {
    int local = 0;         // -1 becomes 0FFFFH
    std::string component;  // empty: the utility's own section
    int number = 0;
    bool ensure_crlf = false;  // the three-argument :use form
};

struct SklClass {
    std::string id;
    std::vector<SklEntry> entries;
};

// `EXTEND15` -> (EXTEND, 15)
bool split_use_reference(std::string_view text, std::string* component, int* number) {
    std::size_t split = text.size();
    while (split > 0 && is_digit(text[split - 1])) --split;
    if (split == 0 || split == text.size()) return false;
    *component = std::string(text.substr(0, split));
    *number = 0;
    std::from_chars(text.data() + split, text.data() + text.size(), *number);
    return true;
}

const std::string_view builtin_text(const std::string& class_id) {
    // Classes 1/3 answer extended errors, 2/4 parse errors; the resident
    // pair gains ",CR,LF" through the ordinary three-argument append rule.
    if (class_id == "2" || class_id == "4") return "\"Parse Error %1\"";
    return "\"Extended Error %1\"";
}

}  // namespace

BuildMsgResult buildmsg(std::string_view skl_raw, const MsgDatabase& msg) {
    std::string util;
    std::vector<SklClass> classes;
    for (std::string_view raw_line : split_lines(skl_raw)) {
        if (!raw_line.empty() && (raw_line[0] == '\t' || raw_line[0] == ' ')) {
            continue;  // continuation of a :def -- documentation only
        }
        const std::string_view line = strip_comment(raw_line);
        if (line.empty()) continue;
        // Directives match case-insensitively: EXE2BIN.SKL writes ":CLASS B".
        std::string head(line.substr(0, std::min<std::size_t>(line.size(), 6)));
        for (char& c : head) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (head.rfind(":util", 0) == 0) {
            const std::vector<std::string_view> fields = split_fields(line, 2);
            if (fields.size() < 2) throw FormatError(0, ":util without a name");
            util = std::string(fields[1]);
            continue;
        }
        if (head.rfind(":class", 0) == 0) {
            const std::vector<std::string_view> fields = split_fields(line, 2);
            if (fields.size() < 2) throw FormatError(0, ":class without a class id");
            classes.push_back({std::string(fields[1]), {}});
            continue;
        }
        if (head.rfind(":end", 0) == 0) break;
        const bool is_use = head.rfind(":use", 0) == 0;
        const bool is_def = head.rfind(":def", 0) == 0;
        if (!is_use && !is_def) continue;
        if (classes.empty()) throw FormatError(0, "directive before the first :class");
        const std::vector<std::string_view> fields = split_fields(line, 4);
        if (util.empty() && is_use && fields.size() == 4) {
            std::string dummy_comp;
            int dummy_num = 0;
            if (!split_use_reference(fields[2], &dummy_comp, &dummy_num)) {
                BuildMsgResult result;
                for (auto& [id, body] : nosrvbld(skl_raw, "", msg)) {
                    result.files.push_back({"CL" + id, std::move(body)});
                }
                result.files.push_back({"CTL", ""});
                return result;
            }
        }
        SklEntry entry;
        if (is_def) {
            if (fields.size() < 2) throw FormatError(0, ":def without a message id");
            std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), entry.local);
            entry.number = entry.local;
        } else if (fields.size() == 2) {
            if (!split_use_reference(fields[1], &entry.component, &entry.number)) {
                throw FormatError(0, "unparsable :use reference");
            }
            entry.local = entry.number;
        } else if (fields.size() >= 4 &&
                   !split_use_reference(fields[2], &entry.component, &entry.number)) {
            std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), entry.local);
            entry.number = entry.local;
            entry.component = upper(fields[2]);
            entry.ensure_crlf = true;
        } else {
            std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), entry.local);
            if (!split_use_reference(fields[2], &entry.component, &entry.number)) {
                throw FormatError(0, "unparsable :use reference");
            }
            entry.ensure_crlf = true;
        }
        classes.back().entries.push_back(std::move(entry));
    }

    // Util-section messages the skeleton never referenced are appended to
    // the last class with their own numbers as ids -- COMP.CLA carries
    // "Files compare OK" (COMP 23) with no :def anywhere in COMP.SKL.
    if (!classes.empty() && !util.empty()) {
        int last_id = 0;
        for (const SklEntry& entry : classes.back().entries) {
            last_id = std::max(last_id, entry.local);
        }
        for (const MsgSection& section : msg.sections) {
            if (section.name != upper(util)) continue;
            // ... but only while the numbers continue the last id without a
            // gap: COMP 23 follows 22 and lands, ATTRIB's 199 does not
            // follow 15 and stays out.
            bool grew = true;
            while (grew) {
                grew = false;
                for (const MsgMessage& message : section.messages) {
                    if (message.number == last_id + 1) {
                        SklEntry entry;
                        entry.local = message.number;
                        entry.number = message.number;
                        classes.back().entries.push_back(entry);
                        ++last_id;
                        grew = true;
                    }
                }
            }
        }
    }

    // Classes 1 and 2 -- the transient pair -- exist in every output,
    // sentinel-only when the skeleton does not declare them: MORE.SKL
    // declares only class A yet the reference build writes CL1 and CL2.
    for (const char* transient : {"2", "1"}) {
        const bool declared = std::any_of(
            classes.begin(), classes.end(),
            [transient](const SklClass& entry) { return entry.id == transient; });
        if (!declared) classes.insert(classes.begin(), {transient, {}});
    }
    BuildMsgResult result;
    result.util = util;
    int resident_sequence = 0;
    int resident_count = 0;
    for (const SklClass& skl_class : classes) {
        if (skl_class.id != "1" && skl_class.id != "2") ++resident_count;
    }
    for (const SklClass& skl_class : classes) {
        const std::string& cid = skl_class.id;
        const bool transient = cid == "1" || cid == "2";
        if (!transient) ++resident_sequence;
        const std::string public_name =
            transient ? "$M_MSGSERV_" + cid : "$M_CLS_" + std::to_string(resident_sequence);

        struct Resolved {
            unsigned local;
            std::vector<std::string> lines;
        };
        std::vector<Resolved> resolved;
        for (const SklEntry& entry : skl_class.entries) {
            const unsigned local =
                entry.local == -1 ? 0xFFFFu : static_cast<unsigned>(entry.local);
            const MsgMessage* message =
                msg.find(entry.component.empty() ? util : entry.component, entry.number);
            std::vector<std::string> lines;
            if (message != nullptr) {
                lines = message->lines;
            } else if (entry.local == -1) {
                // only the resident sentinel falls back to the builtin; an
                // ordinary failed lookup renders an empty message
                // (RESTORE 31).
                lines = {std::string(builtin_text(cid))};
            }
            const std::string upper_component = upper(entry.component);
            const bool crlf_component =
                upper_component == "EXTEND" || upper_component == "PARSE";
            const std::string_view crlf_tail = "CR,LF";
            if (entry.ensure_crlf && crlf_component && !lines.empty() &&
                (lines.back().size() < crlf_tail.size() ||
                 lines.back().compare(lines.back().size() - crlf_tail.size(),
                                      crlf_tail.size(), crlf_tail) != 0)) {
                lines.back() += ",CR,LF";
            }
            (void)crlf_tail;
            resolved.push_back({local, std::move(lines)});
        }

        std::vector<std::string> out;
        const auto sep = [&out] {
            out.emplace_back();
            out.emplace_back(kSeparator);
            out.emplace_back();
        };
        const auto mtag = [&cid](unsigned local) {
            return "$M_" + cid + "_" + hex_upper(local, 5) + "H";
        };
        sep();
        out.push_back("        PUBLIC  " + public_name);
        out.emplace_back("        IF1");
        out.push_back("        %out    ... Including message Class " + cid);
        out.emplace_back("        ENDIF");
        sep();
        const std::string class_byte =
            transient ? "00" + cid + "H" : std::string("0FFH");
        out.push_back("$M_CLASS_" + cid + "_STRUC LABEL BYTE");
        out.push_back("        $M_CLASS_ID <" + class_byte + ",EXPECTED_VERSION,Class_" + cid +
                      "_MessageCount>");
        sep();
        out.emplace_back();
        for (const Resolved& entry : resolved) {
            out.push_back(mtag(entry.local) + "_STRUC LABEL BYTE");
            out.push_back("        $M_ID   <" + hex_upper(entry.local, 5) + "H," +
                          mtag(entry.local) + "_MSG-" + mtag(entry.local) + "_STRUC>");
            out.emplace_back();
        }
        out.pop_back();
        if (transient) {
            sep();
            out.push_back("$M_" + cid + "_FF_STRUC LABEL BYTE");
            out.push_back("        $M_ID <0FFFFH,$M_" + cid + "_FF_MSG-$M_" + cid +
                          "_FF_STRUC>");
        }
        sep();
        out.emplace_back();
        for (const Resolved& entry : resolved) {
            out.push_back(mtag(entry.local) + "_MSG LABEL BYTE");
            out.push_back("        DB      " + mtag(entry.local) + "_END-" + mtag(entry.local) +
                          "_MSG-1");
            for (const std::string& text : entry.lines) {
                out.push_back("        DB      " + text);
            }
            out.push_back(mtag(entry.local) + "_END LABEL BYTE");
            out.emplace_back("  ");
        }
        if (transient) {
            out.emplace_back(kSeparator);
            out.emplace_back();
            out.push_back("$M_" + cid + "_FF_MSG LABEL BYTE");
            out.push_back("        DB      $M_" + cid + "_FF_END-$M_" + cid + "_FF_MSG-1");
            out.push_back("        DB      " + std::string(builtin_text(cid)));
            out.push_back("$M_" + cid + "_FF_END LABEL BYTE");
            out.emplace_back("  ");
        }
        out.emplace_back(kSeparator);
        out.emplace_back();
        const std::size_t count = resolved.size() + (transient ? 1 : 0);
        out.push_back("Class_" + cid + "_MessageCount EQU     " + std::to_string(count));
        sep();
        out.emplace_back("        IF      FARmsg");
        out.push_back(public_name + " PROC FAR");
        out.emplace_back("        ELSE");
        out.push_back(public_name + " PROC NEAR");
        out.emplace_back("        ENDIF");
        out.emplace_back();
        out.emplace_back("        PUSH    CS");
        out.emplace_back("        POP     ES");
        out.push_back("        LEA     DI,$M_CLASS_" + cid + "_STRUC");
        out.push_back("        ADD     CX,$-$M_CLASS_" + cid + "_STRUC");
        out.emplace_back("        RET");
        out.emplace_back();
        out.push_back(public_name + (transient ? " Endp" : " ENDP"));
        sep();
        result.files.push_back({"CL" + cid, join_crlf(out)});
    }
    result.files.push_back(
        {"CTL", "$M_NUM_CLS  EQU " + std::to_string(resident_count) + "\r\n"});
    return result;
}

}  // namespace dosbase::devtools
