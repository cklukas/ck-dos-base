// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/cl_preproc.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"

#include <cctype>
#include <charconv>
#include <set>
#include <sstream>
#include <unordered_set>

namespace dosbase::devtools {
namespace fs = std::filesystem;
namespace {

struct MacroDef {
    bool is_function = false;
    std::vector<std::string> params;
    std::string body;
};

using MacroTable = std::unordered_map<std::string, MacroDef>;

bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string strip_comments_and_splice(std::string_view input) {
    // 1. Normalize CRLF -> LF and splice backslash-newline continuations.
    std::string spliced;
    spliced.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') ++i;
            spliced.push_back('\n');
        } else if (input[i] == '\\' && i + 1 < input.size() &&
                   (input[i + 1] == '\n' ||
                    (input[i + 1] == '\r' && i + 2 < input.size() && input[i + 2] == '\n'))) {
            if (input[i + 1] == '\r') i += 2;
            else ++i;
        } else {
            spliced.push_back(input[i]);
        }
    }

    // 2. Strip /* ... */ and // ... comments while respecting string/char literals.
    std::string out;
    out.reserve(spliced.size());
    std::size_t i = 0;
    while (i < spliced.size()) {
        const char c = spliced[i];
        if (c == '"' || c == '\'') {
            const char quote = c;
            out.push_back(c);
            ++i;
            while (i < spliced.size()) {
                if (spliced[i] == '\\' && i + 1 < spliced.size()) {
                    out.push_back(spliced[i++]);
                    out.push_back(spliced[i++]);
                } else if (spliced[i] == quote) {
                    out.push_back(spliced[i++]);
                    break;
                } else {
                    out.push_back(spliced[i++]);
                }
            }
        } else if (c == '/' && i + 1 < spliced.size() && spliced[i + 1] == '*') {
            out.push_back(' ');
            i += 2;
            while (i + 1 < spliced.size() && !(spliced[i] == '*' && spliced[i + 1] == '/')) {
                if (spliced[i] == '\n') out.push_back('\n');
                ++i;
            }
            if (i + 1 < spliced.size()) i += 2;
            else i = spliced.size();
        } else if (c == '/' && i + 1 < spliced.size() && spliced[i + 1] == '/') {
            i += 2;
            while (i < spliced.size() && spliced[i] != '\n') ++i;
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

std::string_view trim_ws(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    return sv;
}

std::string expand_macros(std::string_view text,
                          const MacroTable& macros,
                          std::unordered_set<std::string>& active,
                          const std::string& current_file,
                          std::size_t current_line);

std::string substitute_params(std::string_view body,
                              const std::vector<std::string>& params,
                              const std::vector<std::string>& args) {
    std::string out;
    std::size_t i = 0;
    while (i < body.size()) {
        if (body[i] == '"' || body[i] == '\'') {
            const char q = body[i];
            out.push_back(body[i++]);
            while (i < body.size()) {
                if (body[i] == '\\' && i + 1 < body.size()) {
                    out.push_back(body[i++]);
                    out.push_back(body[i++]);
                } else if (body[i] == q) {
                    out.push_back(body[i++]);
                    break;
                } else {
                    out.push_back(body[i++]);
                }
            }
            continue;
        }
        if (body[i] == '#' && i + 1 < body.size() && body[i + 1] == '#') {
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
            i += 2;
            while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
            continue;
        }
        if (body[i] == '#') {
            std::size_t j = i + 1;
            while (j < body.size() && (body[j] == ' ' || body[j] == '\t')) ++j;
            if (j < body.size() && is_ident_start(body[j])) {
                std::size_t k = j + 1;
                while (k < body.size() && is_ident_char(body[k])) ++k;
                std::string_view pname = body.substr(j, k - j);
                auto it = std::find(params.begin(), params.end(), pname);
                if (it != params.end()) {
                    const std::string& arg = args[static_cast<std::size_t>(it - params.begin())];
                    out.push_back('"');
                    for (char c : arg) {
                        if (c == '"' || c == '\\') out.push_back('\\');
                        out.push_back(c);
                    }
                    out.push_back('"');
                    i = k;
                    continue;
                }
            }
        }
        if (is_ident_start(body[i])) {
            std::size_t j = i + 1;
            while (j < body.size() && is_ident_char(body[j])) ++j;
            std::string_view ident = body.substr(i, j - i);
            auto it = std::find(params.begin(), params.end(), ident);
            if (it != params.end()) {
                out += args[static_cast<std::size_t>(it - params.begin())];
            } else {
                out += ident;
            }
            i = j;
            continue;
        }
        out.push_back(body[i++]);
    }
    return out;
}

std::string expand_macros(std::string_view text,
                          const MacroTable& macros,
                          std::unordered_set<std::string>& active,
                          const std::string& current_file,
                          std::size_t current_line) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '"' || text[i] == '\'') {
            const char q = text[i];
            out.push_back(text[i++]);
            while (i < text.size()) {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    out.push_back(text[i++]);
                    out.push_back(text[i++]);
                } else if (text[i] == q) {
                    out.push_back(text[i++]);
                    break;
                } else {
                    out.push_back(text[i++]);
                }
            }
            continue;
        }
        if (is_ident_start(text[i])) {
            std::size_t j = i + 1;
            while (j < text.size() && is_ident_char(text[j])) ++j;
            std::string ident(text.substr(i, j - i));
            if (ident == "__LINE__") {
                out += std::to_string(current_line);
                i = j;
                continue;
            }
            if (ident == "__FILE__") {
                out.push_back('"');
                for (char c : current_file) {
                    if (c == '\\' || c == '"') out.push_back('\\');
                    out.push_back(c);
                }
                out.push_back('"');
                i = j;
                continue;
            }
            auto it = macros.find(ident);
            if (it != macros.end() && active.find(ident) == active.end()) {
                const MacroDef& def = it->second;
                if (!def.is_function) {
                    active.insert(ident);
                    out += expand_macros(def.body, macros, active, current_file, current_line);
                    active.erase(ident);
                    i = j;
                    continue;
                }
                // Function-like macro: must be followed by '('
                std::size_t k = j;
                while (k < text.size() && (text[k] == ' ' || text[k] == '\t' || text[k] == '\n')) {
                    ++k;
                }
                if (k < text.size() && text[k] == '(') {
                    ++k;
                    std::vector<std::string> args;
                    int depth = 1;
                    std::size_t arg_start = k;
                    while (k < text.size() && depth > 0) {
                        if (text[k] == '"' || text[k] == '\'') {
                            const char q = text[k++];
                            while (k < text.size()) {
                                if (text[k] == '\\' && k + 1 < text.size()) k += 2;
                                else if (text[k] == q) {
                                    ++k;
                                    break;
                                } else {
                                    ++k;
                                }
                            }
                            continue;
                        }
                        if (text[k] == '(') ++depth;
                        else if (text[k] == ')') {
                            --depth;
                            if (depth == 0) {
                                std::string_view arg = trim_ws(text.substr(arg_start, k - arg_start));
                                if (!arg.empty() || !args.empty() || !def.params.empty()) {
                                    args.push_back(std::string(arg));
                                }
                                ++k;
                                break;
                            }
                        } else if (text[k] == ',' && depth == 1) {
                            args.push_back(std::string(trim_ws(text.substr(arg_start, k - arg_start))));
                            arg_start = k + 1;
                        }
                        ++k;
                    }
                    if (args.size() < def.params.size()) {
                        args.resize(def.params.size());
                    }
                    for (std::string& a : args) {
                        a = expand_macros(a, macros, active, current_file, current_line);
                    }
                    const std::string substituted = substitute_params(def.body, def.params, args);
                    active.insert(ident);
                    out += expand_macros(substituted, macros, active, current_file, current_line);
                    active.erase(ident);
                    i = k;
                    continue;
                }
            }
            out += ident;
            i = j;
            continue;
        }
        out.push_back(text[i++]);
    }
    return out;
}

// Constant integer expression evaluator for #if / #elif
class IfExprParser {
public:
    IfExprParser(std::string_view expr, const MacroTable& macros)
        : expr_(expr), macros_(macros) {}

    std::int64_t eval() {
        // First handle `defined(X)` and `defined X`, then macro-expand remaining tokens.
        std::string pre;
        std::size_t i = 0;
        while (i < expr_.size()) {
            if (is_ident_start(expr_[i])) {
                std::size_t j = i + 1;
                while (j < expr_.size() && is_ident_char(expr_[j])) ++j;
                std::string_view id = expr_.substr(i, j - i);
                if (id == "defined") {
                    std::size_t k = j;
                    while (k < expr_.size() && (expr_[k] == ' ' || expr_[k] == '\t')) ++k;
                    bool has_paren = false;
                    if (k < expr_.size() && expr_[k] == '(') {
                        has_paren = true;
                        ++k;
                        while (k < expr_.size() && (expr_[k] == ' ' || expr_[k] == '\t')) ++k;
                    }
                    std::size_t name_start = k;
                    while (k < expr_.size() && is_ident_char(expr_[k])) ++k;
                    std::string name(expr_.substr(name_start, k - name_start));
                    if (has_paren) {
                        while (k < expr_.size() && (expr_[k] == ' ' || expr_[k] == '\t')) ++k;
                        if (k < expr_.size() && expr_[k] == ')') ++k;
                    }
                    pre += macros_.count(name) ? "1" : "0";
                    i = k;
                    continue;
                }
                pre += id;
                i = j;
            } else {
                pre.push_back(expr_[i++]);
            }
        }
        std::unordered_set<std::string> active;
        expanded_ = expand_macros(pre, macros_, active, "", 0);
        pos_ = 0;
        return parse_ternary();
    }

private:
    void skip_ws() {
        while (pos_ < expanded_.size() &&
               (expanded_[pos_] == ' ' || expanded_[pos_] == '\t')) {
            ++pos_;
        }
    }

    bool match(std::string_view tok) {
        skip_ws();
        if (expanded_.compare(pos_, tok.size(), tok) == 0) {
            if ((tok == "<" || tok == ">") && pos_ + 1 < expanded_.size() &&
                (expanded_[pos_ + 1] == tok[0] || expanded_[pos_ + 1] == '=')) {
                return false;
            }
            if ((tok == "&" || tok == "|") && pos_ + 1 < expanded_.size() &&
                expanded_[pos_ + 1] == tok[0]) {
                return false;
            }
            if (tok == "!" && pos_ + 1 < expanded_.size() && expanded_[pos_ + 1] == '=') {
                return false;
            }
            pos_ += tok.size();
            return true;
        }
        return false;
    }

    std::int64_t parse_ternary() {
        std::int64_t cond = parse_or();
        if (match("?")) {
            std::int64_t t = parse_ternary();
            (void)match(":");
            std::int64_t f = parse_ternary();
            return cond ? t : f;
        }
        return cond;
    }

    std::int64_t parse_or() {
        std::int64_t v = parse_and();
        while (match("||")) v = (v != 0) || (parse_and() != 0);
        return v;
    }

    std::int64_t parse_and() {
        std::int64_t v = parse_bitor();
        while (match("&&")) v = (v != 0) && (parse_bitor() != 0);
        return v;
    }

    std::int64_t parse_bitor() {
        std::int64_t v = parse_bitxor();
        while (match("|")) v |= parse_bitxor();
        return v;
    }

    std::int64_t parse_bitxor() {
        std::int64_t v = parse_bitand();
        while (match("^")) v ^= parse_bitand();
        return v;
    }

    std::int64_t parse_bitand() {
        std::int64_t v = parse_eq();
        while (match("&")) v &= parse_eq();
        return v;
    }

    std::int64_t parse_eq() {
        std::int64_t v = parse_rel();
        while (true) {
            if (match("==")) v = (v == parse_rel()) ? 1 : 0;
            else if (match("!=")) v = (v != parse_rel()) ? 1 : 0;
            else break;
        }
        return v;
    }

    std::int64_t parse_rel() {
        std::int64_t v = parse_shift();
        while (true) {
            if (match("<=")) v = (v <= parse_shift()) ? 1 : 0;
            else if (match(">=")) v = (v >= parse_shift()) ? 1 : 0;
            else if (match("<")) v = (v < parse_shift()) ? 1 : 0;
            else if (match(">")) v = (v > parse_shift()) ? 1 : 0;
            else break;
        }
        return v;
    }

    std::int64_t parse_shift() {
        std::int64_t v = parse_add();
        while (true) {
            if (match("<<")) v <<= parse_add();
            else if (match(">>")) v >>= parse_add();
            else break;
        }
        return v;
    }

    std::int64_t parse_add() {
        std::int64_t v = parse_mul();
        while (true) {
            if (match("+")) v += parse_mul();
            else if (match("-")) v -= parse_mul();
            else break;
        }
        return v;
    }

    std::int64_t parse_mul() {
        std::int64_t v = parse_unary();
        while (true) {
            if (match("*")) v *= parse_unary();
            else if (match("/")) {
                std::int64_t d = parse_unary();
                v = d != 0 ? v / d : 0;
            } else if (match("%")) {
                std::int64_t d = parse_unary();
                v = d != 0 ? v % d : 0;
            } else break;
        }
        return v;
    }

    std::int64_t parse_unary() {
        if (match("!")) return parse_unary() == 0 ? 1 : 0;
        if (match("~")) return ~parse_unary();
        if (match("-")) return -parse_unary();
        if (match("+")) return parse_unary();
        return parse_primary();
    }

    std::int64_t parse_primary() {
        skip_ws();
        if (match("(")) {
            std::int64_t v = parse_ternary();
            (void)match(")");
            return v;
        }
        if (pos_ < expanded_.size() && expanded_[pos_] == '\'') {
            ++pos_;
            std::int64_t ch = 0;
            if (pos_ < expanded_.size()) {
                if (expanded_[pos_] == '\\' && pos_ + 1 < expanded_.size()) {
                    ++pos_;
                    char e = expanded_[pos_++];
                    if (e == 'n') ch = '\n';
                    else if (e == 'r') ch = '\r';
                    else if (e == 't') ch = '\t';
                    else if (e == '0') ch = 0;
                    else ch = static_cast<unsigned char>(e);
                } else {
                    ch = static_cast<unsigned char>(expanded_[pos_++]);
                }
            }
            if (pos_ < expanded_.size() && expanded_[pos_] == '\'') ++pos_;
            return ch;
        }
        if (pos_ < expanded_.size() && std::isdigit(static_cast<unsigned char>(expanded_[pos_]))) {
            int base = 10;
            if (expanded_[pos_] == '0' && pos_ + 1 < expanded_.size() &&
                (expanded_[pos_ + 1] == 'x' || expanded_[pos_ + 1] == 'X')) {
                base = 16;
                pos_ += 2;
            } else if (expanded_[pos_] == '0') {
                base = 8;
            }
            std::uint64_t val = 0;
            const auto res = std::from_chars(expanded_.data() + pos_,
                                             expanded_.data() + expanded_.size(), val, base);
            pos_ = static_cast<std::size_t>(res.ptr - expanded_.data());
            while (pos_ < expanded_.size() &&
                   (expanded_[pos_] == 'u' || expanded_[pos_] == 'U' ||
                    expanded_[pos_] == 'l' || expanded_[pos_] == 'L')) {
                ++pos_;
            }
            return static_cast<std::int64_t>(val);
        }
        if (pos_ < expanded_.size() && is_ident_start(expanded_[pos_])) {
            while (pos_ < expanded_.size() && is_ident_char(expanded_[pos_])) ++pos_;
            return 0;  // Undefined identifier in #if evaluates to 0 per C89.
        }
        return 0;
    }

    std::string_view expr_;
    const MacroTable& macros_;
    std::string expanded_;
    std::size_t pos_{0};
};

struct CondState {
    bool parent_active = true;
    bool current_active = true;
    bool any_taken = false;
};

fs::path find_include_file(std::string_view header,
                           bool quoted,
                           const fs::path& current_file,
                           const std::vector<fs::path>& include_dirs) {
    auto try_dir = [&](const fs::path& dir) -> std::optional<fs::path> {
        std::string combined = (dir / "").string() + std::string(header);
        const fs::path candidate = resolve_dos_input(combined);
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return candidate;
        return std::nullopt;
    };

    if (quoted && current_file.has_parent_path()) {
        if (auto p = try_dir(current_file.parent_path())) return *p;
    }
    for (const fs::path& dir : include_dirs) {
        if (auto p = try_dir(dir)) return *p;
    }
    throw FormatError(0, "cannot open include file: '" + std::string(header) + "'");
}

void process_file(std::string_view raw_source,
                  const fs::path& file_path,
                  const PreprocOptions& options,
                  MacroTable& macros,
                  std::string& out,
                  int depth) {
    if (depth > 64) {
        throw FormatError(0, "include nesting depth exceeded");
    }

    const std::string cleaned = strip_comments_and_splice(raw_source);
    std::vector<CondState> cond_stack;
    auto is_active = [&]() {
        return cond_stack.empty() || cond_stack.back().current_active;
    };

    std::size_t pos = 0;
    std::size_t line_no = 0;
    while (pos < cleaned.size()) {
        ++line_no;
        std::size_t eol = cleaned.find('\n', pos);
        if (eol == std::string::npos) eol = cleaned.size();
        const std::string_view line(cleaned.data() + pos, eol - pos);
        pos = eol < cleaned.size() ? eol + 1 : cleaned.size();

        const std::string_view trimmed = trim_ws(line);
        if (!trimmed.empty() && trimmed.front() == '#') {
            std::string_view rest = trim_ws(trimmed.substr(1));
            std::size_t kw_end = 0;
            while (kw_end < rest.size() && is_ident_char(rest[kw_end])) ++kw_end;
            const std::string_view kw = rest.substr(0, kw_end);
            rest = trim_ws(rest.substr(kw_end));

            if (kw == "ifdef" || kw == "ifndef") {
                std::size_t n_end = 0;
                while (n_end < rest.size() && is_ident_char(rest[n_end])) ++n_end;
                std::string name(rest.substr(0, n_end));
                bool cond = macros.count(name) > 0;
                if (kw == "ifndef") cond = !cond;
                const bool parent = is_active();
                cond_stack.push_back({parent, parent && cond, cond});
            } else if (kw == "if") {
                const bool parent = is_active();
                bool cond = false;
                if (parent) {
                    IfExprParser parser(rest, macros);
                    cond = parser.eval() != 0;
                }
                cond_stack.push_back({parent, parent && cond, cond});
            } else if (kw == "elif") {
                if (cond_stack.empty()) throw FormatError(0, "unmatched #elif");
                CondState& top = cond_stack.back();
                if (!top.parent_active || top.any_taken) {
                    top.current_active = false;
                } else {
                    IfExprParser parser(rest, macros);
                    const bool cond = parser.eval() != 0;
                    top.current_active = cond;
                    if (cond) top.any_taken = true;
                }
            } else if (kw == "else") {
                if (cond_stack.empty()) throw FormatError(0, "unmatched #else");
                CondState& top = cond_stack.back();
                top.current_active = top.parent_active && !top.any_taken;
                top.any_taken = true;
            } else if (kw == "endif") {
                if (cond_stack.empty()) throw FormatError(0, "unmatched #endif");
                cond_stack.pop_back();
            } else if (!is_active()) {
                // Ignore inactive directives.
            } else if (kw == "define") {
                std::size_t n_end = 0;
                while (n_end < rest.size() && is_ident_char(rest[n_end])) ++n_end;
                std::string name(rest.substr(0, n_end));
                MacroDef def;
                std::string_view after = rest.substr(n_end);
                if (!after.empty() && after.front() == '(') {
                    def.is_function = true;
                    std::size_t close = after.find(')');
                    if (close == std::string_view::npos) {
                        throw FormatError(0, "unterminated macro parameter list");
                    }
                    std::string_view plist = after.substr(1, close - 1);
                    std::size_t p = 0;
                    while (p < plist.size()) {
                        std::size_t comma = plist.find(',', p);
                        if (comma == std::string_view::npos) comma = plist.size();
                        std::string_view param = trim_ws(plist.substr(p, comma - p));
                        if (!param.empty()) def.params.push_back(std::string(param));
                        p = comma + 1;
                    }
                    def.body = std::string(trim_ws(after.substr(close + 1)));
                } else {
                    def.body = std::string(trim_ws(after));
                }
                macros[std::move(name)] = std::move(def);
            } else if (kw == "undef") {
                std::size_t n_end = 0;
                while (n_end < rest.size() && is_ident_char(rest[n_end])) ++n_end;
                macros.erase(std::string(rest.substr(0, n_end)));
            } else if (kw == "include") {
                std::unordered_set<std::string> active;
                std::string inc_arg = expand_macros(rest, macros, active, file_path.string(), line_no);
                std::string_view sv = trim_ws(inc_arg);
                bool quoted = false;
                std::string header;
                if (sv.size() >= 2 && sv.front() == '"') {
                    quoted = true;
                    std::size_t endq = sv.find('"', 1);
                    header = std::string(sv.substr(1, endq - 1));
                } else if (sv.size() >= 2 && sv.front() == '<') {
                    std::size_t endq = sv.find('>', 1);
                    header = std::string(sv.substr(1, endq - 1));
                } else {
                    throw FormatError(0, "malformed #include directive");
                }
                const fs::path inc_path =
                    find_include_file(header, quoted, file_path, options.include_dirs);
                const std::vector<std::uint8_t> inc_bytes = read_file(inc_path.string());
                process_file(
                    std::string_view(reinterpret_cast<const char*>(inc_bytes.data()), inc_bytes.size()),
                    inc_path, options, macros, out, depth + 1);
            } else if (kw == "error") {
                throw FormatError(0, "#error: " + std::string(rest));
            }
            out.push_back('\n');
            continue;
        }

        if (!is_active()) {
            out.push_back('\n');
            continue;
        }

        std::unordered_set<std::string> active;
        out += expand_macros(line, macros, active, file_path.string(), line_no);
        out.push_back('\n');
    }

    if (!cond_stack.empty()) {
        throw FormatError(0, "unterminated #if / #ifdef");
    }
}

}  // namespace

std::string preprocess_c_source(std::string_view source,
                                  const fs::path& source_path,
                                  const PreprocOptions& options) {
    MacroTable macros;
    macros["MSDOS"] = MacroDef{false, {}, "1"};
    macros["M_I86"] = MacroDef{false, {}, "1"};
    for (const auto& [k, v] : options.defines) {
        macros[k] = MacroDef{false, {}, v};
    }

    std::string out;
    out.reserve(source.size());
    process_file(source, source_path, options, macros, out, 0);
    return out;
}

}  // namespace dosbase::devtools
