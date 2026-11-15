// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/cl_compiler.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/omf_record.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dosbase::devtools {
namespace {

// ---------------------------------------------------------------------------
// 1. Lexer
// ---------------------------------------------------------------------------

enum class Tok {
    eof,
    ident,
    int_lit,
    str_lit,
    // Keywords
    kw_void, kw_char, kw_short, kw_int, kw_long, kw_float, kw_double,
    kw_signed, kw_unsigned, kw_const, kw_volatile,
    kw_struct, kw_union, kw_enum, kw_typedef,
    kw_static, kw_extern, kw_auto, kw_register,
    kw_if, kw_else, kw_while, kw_do, kw_for,
    kw_switch, kw_case, kw_default, kw_break, kw_continue, kw_return, kw_goto,
    kw_sizeof,
    kw_near, kw_far, kw_huge, kw_cdecl, kw_pascal, kw_interrupt,
    // Punctuation & operators
    lparen, rparen, lbrace, rbrace, lbracket, rbracket,
    semi, comma, dot, arrow, colon, question, ellipsis,
    plus, minus, star, slash, percent,
    amp, pipe, caret, tilde, bang,
    assign, plus_eq, minus_eq, star_eq, slash_eq, percent_eq,
    amp_eq, pipe_eq, caret_eq, shl_eq, shr_eq,
    inc, dec, shl, shr,
    eq, ne, lt, le, gt, ge,
    land, lor,
};

struct Token {
    Tok kind = Tok::eof;
    std::string text;
    std::uint64_t int_val = 0;
    bool is_unsigned = false;
    bool is_long = false;
    std::size_t line = 1;
};

bool is_id_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_id_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

char decode_escape(std::string_view src, std::size_t& i) {
    if (i >= src.size()) return 0;
    char c = src[i++];
    switch (c) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'a': return '\a';
        case 'v': return '\v';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case '?': return '?';
        case 'x':
        case 'X': {
            unsigned int val = 0;
            int digits = 0;
            while (i < src.size() && digits < 2 &&
                   std::isxdigit(static_cast<unsigned char>(src[i]))) {
                char d = src[i++];
                val *= 16;
                if (d >= '0' && d <= '9') val += static_cast<unsigned int>(d - '0');
                else if (d >= 'a' && d <= 'f') val += static_cast<unsigned int>(d - 'a' + 10);
                else val += static_cast<unsigned int>(d - 'A' + 10);
                ++digits;
            }
            return static_cast<char>(val & 0xFF);
        }
        default:
            if (c >= '0' && c <= '7') {
                unsigned int val = static_cast<unsigned int>(c - '0');
                for (int k = 0; k < 2 && i < src.size() && src[i] >= '0' && src[i] <= '7'; ++k) {
                    val = val * 8 + static_cast<unsigned int>(src[i++] - '0');
                }
                return static_cast<char>(val & 0xFF);
            }
            return c;
    }
}

Tok lookup_keyword(std::string_view s) {
    static const std::unordered_map<std::string_view, Tok> kw = {
        {"void", Tok::kw_void}, {"char", Tok::kw_char}, {"short", Tok::kw_short},
        {"int", Tok::kw_int}, {"long", Tok::kw_long}, {"float", Tok::kw_float},
        {"double", Tok::kw_double}, {"signed", Tok::kw_signed}, {"unsigned", Tok::kw_unsigned},
        {"const", Tok::kw_const}, {"volatile", Tok::kw_volatile},
        {"struct", Tok::kw_struct}, {"union", Tok::kw_union}, {"enum", Tok::kw_enum},
        {"typedef", Tok::kw_typedef}, {"static", Tok::kw_static}, {"extern", Tok::kw_extern},
        {"auto", Tok::kw_auto}, {"register", Tok::kw_register},
        {"if", Tok::kw_if}, {"else", Tok::kw_else}, {"while", Tok::kw_while},
        {"do", Tok::kw_do}, {"for", Tok::kw_for}, {"switch", Tok::kw_switch},
        {"case", Tok::kw_case}, {"default", Tok::kw_default}, {"break", Tok::kw_break},
        {"continue", Tok::kw_continue}, {"return", Tok::kw_return}, {"goto", Tok::kw_goto},
        {"sizeof", Tok::kw_sizeof},
        {"near", Tok::kw_near}, {"_near", Tok::kw_near}, {"__near", Tok::kw_near},
        {"far", Tok::kw_far}, {"_far", Tok::kw_far}, {"__far", Tok::kw_far},
        {"huge", Tok::kw_huge}, {"_huge", Tok::kw_huge}, {"__huge", Tok::kw_huge},
        {"cdecl", Tok::kw_cdecl}, {"_cdecl", Tok::kw_cdecl}, {"__cdecl", Tok::kw_cdecl},
        {"pascal", Tok::kw_pascal}, {"_pascal", Tok::kw_pascal}, {"__pascal", Tok::kw_pascal},
        {"fortran", Tok::kw_pascal}, {"_fortran", Tok::kw_pascal},
        {"interrupt", Tok::kw_interrupt}, {"_interrupt", Tok::kw_interrupt},
        {"__interrupt", Tok::kw_interrupt},
    };
    auto it = kw.find(s);
    return it != kw.end() ? it->second : Tok::ident;
}

std::vector<Token> tokenize(std::string_view src) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    std::size_t line = 1;

    while (i < src.size()) {
        const char c = src[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) || c == '\x1a') {
            ++i;
            continue;
        }

        if (c == '"') {
            std::string str;
            ++i;
            while (i < src.size()) {
                if (src[i] == '"') {
                    ++i;
                    // Adjacent string literal concatenation
                    std::size_t look = i;
                    while (look < src.size() &&
                           std::isspace(static_cast<unsigned char>(src[look]))) {
                        if (src[look] == '\n') ++line;
                        ++look;
                    }
                    if (look < src.size() && src[look] == '"') {
                        i = look + 1;
                        continue;
                    }
                    break;
                }
                if (src[i] == '\\') {
                    ++i;
                    str.push_back(decode_escape(src, i));
                } else {
                    if (src[i] == '\n') ++line;
                    str.push_back(src[i++]);
                }
            }
            tokens.push_back({Tok::str_lit, std::move(str), 0, false, false, line});
            continue;
        }

        if (c == '\'') {
            ++i;
            std::uint64_t val = 0;
            while (i < src.size() && src[i] != '\'') {
                char ch = (src[i] == '\\') ? (++i, decode_escape(src, i)) : src[i++];
                val = (val << 8) | static_cast<unsigned char>(ch);
            }
            if (i < src.size() && src[i] == '\'') ++i;
            tokens.push_back({Tok::int_lit, {}, val, false, false, line});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t start = i;
            int base = 10;
            if (c == '0' && i + 1 < src.size() && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                base = 16;
                i += 2;
            } else if (c == '0') {
                base = 8;
            }
            std::uint64_t val = 0;
            const auto res = std::from_chars(src.data() + i, src.data() + src.size(), val, base);
            i = static_cast<std::size_t>(res.ptr - src.data());
            // Skip optional fractional/exponent part for float constants
            if (base == 10 && i < src.size() && (src[i] == '.' || src[i] == 'e' || src[i] == 'E')) {
                if (src[i] == '.') {
                    ++i;
                    while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
                }
                if (i < src.size() && (src[i] == 'e' || src[i] == 'E')) {
                    ++i;
                    if (i < src.size() && (src[i] == '+' || src[i] == '-')) ++i;
                    while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
                }
            }
            bool is_u = false;
            bool is_l = false;
            while (i < src.size() &&
                   (src[i] == 'u' || src[i] == 'U' || src[i] == 'l' || src[i] == 'L' ||
                    src[i] == 'f' || src[i] == 'F')) {
                if (src[i] == 'u' || src[i] == 'U') is_u = true;
                if (src[i] == 'l' || src[i] == 'L') is_l = true;
                ++i;
            }
            tokens.push_back({Tok::int_lit, std::string(src.substr(start, i - start)),
                              val, is_u, is_l, line});
            continue;
        }

        if (is_id_start(c)) {
            std::size_t start = i++;
            while (i < src.size() && is_id_char(src[i])) ++i;
            std::string id(src.substr(start, i - start));
            Tok k = lookup_keyword(id);
            tokens.push_back({k, std::move(id), 0, false, false, line});
            continue;
        }

        auto match2 = [&](char b) -> bool {
            return i + 1 < src.size() && src[i + 1] == b;
        };
        auto match3 = [&](char b, char d) -> bool {
            return i + 2 < src.size() && src[i + 1] == b && src[i + 2] == d;
        };

        Tok punct = Tok::eof;
        std::size_t advance = 1;
        switch (c) {
            case '(': punct = Tok::lparen; break;
            case ')': punct = Tok::rparen; break;
            case '{': punct = Tok::lbrace; break;
            case '}': punct = Tok::rbrace; break;
            case '[': punct = Tok::lbracket; break;
            case ']': punct = Tok::rbracket; break;
            case ';': punct = Tok::semi; break;
            case ',': punct = Tok::comma; break;
            case ':': punct = Tok::colon; break;
            case '?': punct = Tok::question; break;
            case '~': punct = Tok::tilde; break;
            case '.':
                if (match3('.', '.')) { punct = Tok::ellipsis; advance = 3; }
                else punct = Tok::dot;
                break;
            case '+':
                if (match2('+')) { punct = Tok::inc; advance = 2; }
                else if (match2('=')) { punct = Tok::plus_eq; advance = 2; }
                else punct = Tok::plus;
                break;
            case '-':
                if (match2('-')) { punct = Tok::dec; advance = 2; }
                else if (match2('=')) { punct = Tok::minus_eq; advance = 2; }
                else if (match2('>')) { punct = Tok::arrow; advance = 2; }
                else punct = Tok::minus;
                break;
            case '*':
                if (match2('=')) { punct = Tok::star_eq; advance = 2; }
                else punct = Tok::star;
                break;
            case '/':
                if (match2('=')) { punct = Tok::slash_eq; advance = 2; }
                else punct = Tok::slash;
                break;
            case '%':
                if (match2('=')) { punct = Tok::percent_eq; advance = 2; }
                else punct = Tok::percent;
                break;
            case '&':
                if (match2('&')) { punct = Tok::land; advance = 2; }
                else if (match2('=')) { punct = Tok::amp_eq; advance = 2; }
                else punct = Tok::amp;
                break;
            case '|':
                if (match2('|')) { punct = Tok::lor; advance = 2; }
                else if (match2('=')) { punct = Tok::pipe_eq; advance = 2; }
                else punct = Tok::pipe;
                break;
            case '^':
                if (match2('=')) { punct = Tok::caret_eq; advance = 2; }
                else punct = Tok::caret;
                break;
            case '!':
                if (match2('=')) { punct = Tok::ne; advance = 2; }
                else punct = Tok::bang;
                break;
            case '=':
                if (match2('=')) { punct = Tok::eq; advance = 2; }
                else punct = Tok::assign;
                break;
            case '<':
                if (match3('<', '=')) { punct = Tok::shl_eq; advance = 3; }
                else if (match2('<')) { punct = Tok::shl; advance = 2; }
                else if (match2('=')) { punct = Tok::le; advance = 2; }
                else punct = Tok::lt;
                break;
            case '>':
                if (match3('>', '=')) { punct = Tok::shr_eq; advance = 3; }
                else if (match2('>')) { punct = Tok::shr; advance = 2; }
                else if (match2('=')) { punct = Tok::ge; advance = 2; }
                else punct = Tok::gt;
                break;
            default:
                throw FormatError(line, std::string("unexpected character: ") + c);
        }
        tokens.push_back({punct, {}, 0, false, false, line});
        i += advance;
    }

    tokens.push_back({Tok::eof, {}, 0, false, false, line});
    return tokens;
}

// ---------------------------------------------------------------------------
// 2. Type System
// ---------------------------------------------------------------------------

enum class TypeKind {
    t_void,
    t_char,
    t_uchar,
    t_short,
    t_ushort,
    t_long,
    t_ulong,
    t_float,
    t_double,
    t_ptr,
    t_array,
    t_struct,
    t_union,
    t_func,
};

struct CType;
using TypePtr = std::shared_ptr<CType>;

struct StructField {
    std::string name;
    TypePtr type;
    std::size_t offset = 0;
    int bit_offset = -1;
    int bit_width = 0;
};

struct ParamDecl {
    std::string name;
    TypePtr type;
};

struct CType {
    TypeKind kind = TypeKind::t_short;
    TypePtr base;  // pointee, array element, or return type
    std::size_t array_len = 0;
    bool is_far = false;
    bool is_pascal = false;
    bool is_interrupt = false;
    bool is_vararg = false;
    std::vector<ParamDecl> params;
    std::vector<StructField> fields;
    std::size_t cached_size = 0;
    std::size_t cached_align = 1;
    bool complete = true;
};

TypePtr make_prim(TypeKind k) {
    auto t = std::make_shared<CType>();
    t->kind = k;
    switch (k) {
        case TypeKind::t_void: t->cached_size = 0; t->cached_align = 1; break;
        case TypeKind::t_char:
        case TypeKind::t_uchar: t->cached_size = 1; t->cached_align = 1; break;
        case TypeKind::t_short:
        case TypeKind::t_ushort: t->cached_size = 2; t->cached_align = 2; break;
        case TypeKind::t_long:
        case TypeKind::t_ulong:
        case TypeKind::t_float: t->cached_size = 4; t->cached_align = 2; break;
        case TypeKind::t_double: t->cached_size = 8; t->cached_align = 2; break;
        default: break;
    }
    return t;
}

TypePtr make_ptr(TypePtr pointee, bool is_far) {
    auto t = std::make_shared<CType>();
    t->kind = TypeKind::t_ptr;
    t->base = std::move(pointee);
    t->is_far = is_far;
    t->cached_size = is_far ? 4 : 2;
    t->cached_align = 2;
    return t;
}

TypePtr make_array(TypePtr elem, std::size_t len) {
    auto t = std::make_shared<CType>();
    t->kind = TypeKind::t_array;
    t->base = std::move(elem);
    t->array_len = len;
    t->cached_align = t->base->cached_align;
    t->cached_size = t->base->cached_size * len;
    return t;
}

std::size_t type_size(const TypePtr& t) {
    return t ? t->cached_size : 2;
}

std::size_t type_align(const TypePtr& t, bool packed) {
    if (!t || packed) return 1;
    return std::min<std::size_t>(2, std::max<std::size_t>(1, t->cached_align));
}

bool is_signed_type(const TypePtr& t) {
    return t && (t->kind == TypeKind::t_char || t->kind == TypeKind::t_short ||
                 t->kind == TypeKind::t_long);
}

bool is_32bit_scalar(const TypePtr& t) {
    return t && (t->kind == TypeKind::t_long || t->kind == TypeKind::t_ulong ||
                 t->kind == TypeKind::t_float ||
                 (t->kind == TypeKind::t_ptr && t->is_far));
}

bool is_byte_scalar(const TypePtr& t) {
    return t && (t->kind == TypeKind::t_char || t->kind == TypeKind::t_uchar);
}

// ---------------------------------------------------------------------------
// 3. AST
// ---------------------------------------------------------------------------

enum class ExprKind {
    int_lit,
    str_lit,
    var,
    unary,
    binary,
    assign,
    compound_assign,
    ternary,
    call,
    member,
    cast,
    comma,
};

struct Expr {
    ExprKind kind = ExprKind::int_lit;
    TypePtr type;
    std::int64_t int_val = 0;
    std::string name;
    std::string resolved_asm_name;
    Tok op = Tok::eof;
    bool is_postfix = false;
    bool is_arrow = false;
    std::shared_ptr<Expr> lhs;
    std::shared_ptr<Expr> rhs;
    std::shared_ptr<Expr> third;
    std::vector<std::shared_ptr<Expr>> args;
};

using ExprPtr = std::shared_ptr<Expr>;

enum class StmtKind {
    empty,
    expr,
    block,
    if_stmt,
    while_stmt,
    do_while_stmt,
    for_stmt,
    switch_stmt,
    case_stmt,
    default_stmt,
    break_stmt,
    continue_stmt,
    return_stmt,
    goto_stmt,
    label_stmt,
};

struct LocalVar {
    std::string name;
    TypePtr type;
    int bp_offset = 0;  // negative offset from BP
    ExprPtr init;
};

struct Stmt {
    StmtKind kind = StmtKind::empty;
    ExprPtr expr;
    ExprPtr cond;
    ExprPtr post;
    std::shared_ptr<Stmt> body;
    std::shared_ptr<Stmt> else_body;
    std::vector<LocalVar> locals;
    std::vector<std::shared_ptr<Stmt>> stmts;
    std::string label;
    std::int64_t case_val = 0;
};

using StmtPtr = std::shared_ptr<Stmt>;

struct InitItem {
    std::size_t offset = 0;
    TypePtr type;
    std::int64_t int_val = 0;
    std::string sym_ref;  // non-empty if address of global/string
    std::int64_t sym_addend = 0;
    bool is_string_bytes = false;
    std::string str_bytes;
};

struct GlobalVar {
    std::string name;
    TypePtr type;
    bool is_static = false;
    bool is_extern = false;
    bool has_init = false;
    std::vector<InitItem> inits;
};

struct FuncDef {
    std::string name;
    TypePtr type;
    bool is_static = false;
    std::vector<ParamDecl> params;
    StmtPtr body;
    std::size_t frame_size = 0;
};

// ---------------------------------------------------------------------------
// 4. OMF Emitter
// ---------------------------------------------------------------------------

enum class FixupKind {
    near_offset_dgroup,  // 16-bit offset in DGROUP (seg/ext)
    near_offset_code,    // 16-bit offset in target code segment (seg/ext)
    near_call_rel,       // 16-bit self-relative displacement in _TEXT
    far_ptr,             // 32-bit offset:segment
    seg_only,            // 16-bit segment
};

struct Fixup {
    std::size_t offset = 0;
    FixupKind kind = FixupKind::near_offset_dgroup;
    int target_seg = 0;  // 1=_TEXT, 2=_DATA, 3=CONST, 4=_BSS, 0=external
    int target_ext = 0;  // 1-based EXTDEF index if target_seg == 0
};

struct SegmentBuf {
    std::vector<std::uint8_t> data;
    std::vector<Fixup> fixups;
};

struct PubSym {
    std::string name;
    int seg = 1;
    std::uint16_t offset = 0;
};

class OmfBuilder {
public:
    explicit OmfBuilder(const CompileOptions& opts) : opts_(opts) {}

    int intern_ext(const std::string& sym) {
        auto it = ext_map_.find(sym);
        if (it != ext_map_.end()) return it->second;
        int idx = static_cast<int>(externals_.size()) + 1;
        externals_.push_back(sym);
        ext_map_[sym] = idx;
        return idx;
    }

    void add_pub(const std::string& sym, int seg, std::uint16_t offset) {
        pubs_.push_back({sym, seg, offset});
    }

    SegmentBuf code;   // seg 1: _TEXT
    SegmentBuf data;   // seg 2: _DATA
    SegmentBuf cnst;   // seg 3: CONST
    std::size_t bss_size = 0;  // seg 4: _BSS

    [[nodiscard]] std::vector<std::uint8_t> build() const {
        std::vector<std::uint8_t> out;

        auto emit_rec = [&](std::uint8_t type, const std::vector<std::uint8_t>& body) {
            const std::uint16_t len = static_cast<std::uint16_t>(body.size() + 1);
            out.push_back(type);
            out.push_back(static_cast<std::uint8_t>(len & 0xFF));
            out.push_back(static_cast<std::uint8_t>(len >> 8));
            out.insert(out.end(), body.begin(), body.end());
            std::uint32_t sum = type + (len & 0xFF) + (len >> 8);
            for (std::uint8_t b : body) sum += b;
            out.push_back(static_cast<std::uint8_t>((256 - (sum & 0xFF)) & 0xFF));
        };

        auto put_str = [](std::vector<std::uint8_t>& b, std::string_view s) {
            std::size_t n = std::min<std::size_t>(s.size(), 255);
            b.push_back(static_cast<std::uint8_t>(n));
            b.insert(b.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
        };

        auto put_u16 = [](std::vector<std::uint8_t>& b, std::uint16_t v) {
            b.push_back(static_cast<std::uint8_t>(v & 0xFF));
            b.push_back(static_cast<std::uint8_t>(v >> 8));
        };

        auto put_idx = [](std::vector<std::uint8_t>& b, int idx) {
            if (idx < 128) {
                b.push_back(static_cast<std::uint8_t>(idx));
            } else {
                b.push_back(static_cast<std::uint8_t>(0x80 | (idx >> 8)));
                b.push_back(static_cast<std::uint8_t>(idx & 0xFF));
            }
        };

        // 1. THEADR
        {
            std::vector<std::uint8_t> b;
            put_str(b, opts_.module_name);
            emit_rec(0x80, b);
        }

        // 2. COMENT: translator + optional default library
        {
            std::vector<std::uint8_t> b = {0x00, 0x00, 'M', 'S', ' ', 'C'};
            emit_rec(0x88, b);
        }
        if (!opts_.omit_default_lib) {
            std::string lib = "SLIBC";
            if (opts_.model == MemoryModel::medium_model) lib = "MLIBC";
            else if (opts_.model == MemoryModel::compact_model) lib = "CLIBC";
            else if (opts_.model == MemoryModel::large_model) lib = "LLIBC";
            std::vector<std::uint8_t> b = {0x00, 0x9F};
            b.insert(b.end(), lib.begin(), lib.end());
            emit_rec(0x88, b);
        }

        // 3. LNAMES
        std::string code_name = "_TEXT";
        if (opts_.model == MemoryModel::medium_model || opts_.model == MemoryModel::large_model) {
            std::string mod_up = opts_.module_name;
            for (char& c : mod_up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            code_name = mod_up + "_TEXT";
        }
        {
            std::vector<std::uint8_t> b;
            for (const std::string& name : std::vector<std::string>{
                     "", "DGROUP", code_name, "CODE", "_DATA", "DATA", "CONST", "_BSS", "BSS"}) {
                put_str(b, name);
            }
            emit_rec(0x96, b);
        }

        // 4. SEGDEF (1: _TEXT, 2: _DATA, 3: CONST, 4: _BSS)
        auto emit_segdef = [&](std::uint8_t acbp, std::size_t size, int name_idx, int class_idx) {
            std::vector<std::uint8_t> b;
            b.push_back(acbp);
            put_u16(b, static_cast<std::uint16_t>(size));
            put_idx(b, name_idx);
            put_idx(b, class_idx);
            put_idx(b, 1);  // overlay ""
            emit_rec(0x98, b);
        };
        emit_segdef(0x28, code.data.size(), 3, 4);
        emit_segdef(0x48, data.data.size(), 5, 6);
        emit_segdef(0x48, cnst.data.size(), 7, 7);
        emit_segdef(0x48, bss_size, 8, 9);

        // 5. GRPDEF: DGROUP = { CONST (3), _BSS (4), _DATA (2) }
        {
            std::vector<std::uint8_t> b;
            put_idx(b, 2);
            for (int seg : {3, 4, 2}) {
                b.push_back(0xFF);
                put_idx(b, seg);
            }
            emit_rec(0x9A, b);
        }

        // 6. EXTDEF (batched)
        if (!externals_.empty()) {
            std::vector<std::uint8_t> b;
            for (const std::string& ext : externals_) {
                if (b.size() + ext.size() + 3 > 900) {
                    emit_rec(0x8C, b);
                    b.clear();
                }
                put_str(b, ext);
                put_idx(b, 0);
            }
            if (!b.empty()) emit_rec(0x8C, b);
        }

        // 7. PUBDEF (grouped by segment)
        for (int seg = 1; seg <= 4; ++seg) {
            std::vector<std::uint8_t> b;
            int grp = (seg == 1) ? 0 : 1;
            for (const PubSym& p : pubs_) {
                if (p.seg != seg) continue;
                if (b.empty()) {
                    put_idx(b, grp);
                    put_idx(b, seg);
                }
                if (b.size() + p.name.size() + 6 > 900) {
                    emit_rec(0x90, b);
                    b.clear();
                    put_idx(b, grp);
                    put_idx(b, seg);
                }
                put_str(b, p.name);
                put_u16(b, p.offset);
                put_idx(b, 0);
            }
            if (!b.empty()) emit_rec(0x90, b);
        }

        // 8. LEDATA + FIXUPP
        auto emit_segment_data = [&](int seg_idx, const SegmentBuf& seg) {
            constexpr std::size_t kMaxChunk = 900;
            std::size_t pos = 0;
            while (pos < seg.data.size()) {
                std::size_t end = std::min(pos + kMaxChunk, seg.data.size());
                // Don't split a 2- or 4-byte fixup across chunks
                for (const Fixup& f : seg.fixups) {
                    std::size_t fsize = (f.kind == FixupKind::far_ptr) ? 4 : 2;
                    if (f.offset < end && f.offset + fsize > end) {
                        end = f.offset;
                    }
                }
                std::vector<std::uint8_t> le;
                put_idx(le, seg_idx);
                put_u16(le, static_cast<std::uint16_t>(pos));
                le.insert(le.end(), seg.data.begin() + static_cast<std::ptrdiff_t>(pos),
                          seg.data.begin() + static_cast<std::ptrdiff_t>(end));
                emit_rec(0xA0, le);

                std::vector<std::uint8_t> fx;
                for (const Fixup& f : seg.fixups) {
                    if (f.offset < pos || f.offset >= end) continue;
                    std::uint16_t rec_off = static_cast<std::uint16_t>(f.offset - pos);
                    std::uint8_t mode = 1;  // segment-relative
                    std::uint8_t loc = 1;   // 16-bit offset
                    if (f.kind == FixupKind::near_call_rel) {
                        mode = 0;  // self-relative
                        loc = 1;
                    } else if (f.kind == FixupKind::far_ptr) {
                        mode = 1;
                        loc = 3;  // 32-bit pointer
                    } else if (f.kind == FixupKind::seg_only) {
                        mode = 1;
                        loc = 2;  // 16-bit segment
                    }
                    std::uint16_t locat = static_cast<std::uint16_t>(
                        0x8000 | (mode << 14) | (loc << 10) | (rec_off & 0x03FF));
                    // High byte first in OMF FIXUPP locat
                    fx.push_back(static_cast<std::uint8_t>(locat >> 8));
                    fx.push_back(static_cast<std::uint8_t>(locat & 0xFF));

                    // FixData: frame method + target method
                    // Frame: if near_offset_dgroup or target is DGROUP seg (2,3,4), frame=1 (DGROUP)
                    // Otherwise frame=0 (seg 1) or frame=2 (ext)
                    std::uint8_t frame_method = 0;
                    int frame_idx = 1;
                    if (f.kind == FixupKind::near_offset_dgroup) {
                        frame_method = 1;  // group DGROUP
                        frame_idx = 1;
                    } else if (f.target_seg == 0) {
                        frame_method = 2;  // external
                        frame_idx = f.target_ext;
                    } else if (f.target_seg == 1) {
                        frame_method = 0;  // seg _TEXT
                        frame_idx = 1;
                    } else {
                        frame_method = 1;  // group DGROUP
                        frame_idx = 1;
                    }

                    std::uint8_t target_method = (f.target_seg == 0) ? 2 : 0;  // 2=ext, 0=seg
                    int target_idx = (f.target_seg == 0) ? f.target_ext : f.target_seg;
                    // P=1 (no displacement) -> bit 2 = 1
                    std::uint8_t fixdata = static_cast<std::uint8_t>(
                        (frame_method << 4) | 0x04 | target_method);
                    fx.push_back(fixdata);
                    put_idx(fx, frame_idx);
                    put_idx(fx, target_idx);
                }
                if (!fx.empty()) emit_rec(0x9C, fx);
                pos = end;
            }
        };

        emit_segment_data(1, code);
        emit_segment_data(2, data);
        emit_segment_data(3, cnst);

        // 9. MODEND
        emit_rec(0x8A, {0x00});
        return out;
    }

private:
    CompileOptions opts_;
    std::vector<std::string> externals_;
    std::unordered_map<std::string, int> ext_map_;
    std::vector<PubSym> pubs_;
};

// ---------------------------------------------------------------------------
// 5. Parser & Semantic State
// ---------------------------------------------------------------------------

struct VarSymbol {
    std::string name;
    TypePtr type;
    bool is_global = false;
    bool is_static = false;
    bool is_extern = false;
    int bp_offset = 0;
    std::string asm_name;  // mangled name (for globals/functions)
};

class Parser {
public:
    Parser(std::vector<Token> tokens, const CompileOptions& opts)
        : tokens_(std::move(tokens)), opts_(opts) {}

    void parse_translation_unit() {
        while (!at(Tok::eof)) {
            parse_external_decl();
        }
    }

    std::vector<GlobalVar> globals;
    std::vector<FuncDef> funcs;
    std::unordered_map<std::string, VarSymbol> global_syms;

private:
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    CompileOptions opts_;
    int anon_id_ = 0;

    std::unordered_map<std::string, TypePtr> typedefs_;
    std::unordered_map<std::string, TypePtr> tag_types_;
    std::unordered_map<std::string, std::int64_t> enum_consts_;

    // Local scopes during function parsing
    std::vector<std::unordered_map<std::string, VarSymbol>> local_scopes_;
    int next_local_offset_ = 0;

    const Token& peek(std::size_t ahead = 0) const {
        if (pos_ + ahead >= tokens_.size()) return tokens_.back();
        return tokens_[pos_ + ahead];
    }
    bool at(Tok k) const { return peek().kind == k; }
    bool match(Tok k) {
        if (at(k)) {
            ++pos_;
            return true;
        }
        return false;
    }
    Token consume(Tok k, const char* msg) {
        if (!at(k)) {
            throw FormatError(peek().line, msg);
        }
        return tokens_[pos_++];
    }

    bool default_far_code() const {
        return opts_.model == MemoryModel::medium_model ||
               opts_.model == MemoryModel::large_model;
    }
    bool default_far_data() const {
        return opts_.model == MemoryModel::compact_model ||
               opts_.model == MemoryModel::large_model;
    }

    std::string mangle_name(const std::string& name, bool is_pascal, bool is_static) {
        if (is_static) {
            return "__L" + std::to_string(++anon_id_) + "_" + name;
        }
        if (is_pascal) {
            std::string up = name;
            for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return up;
        }
        return "_" + name;
    }

    bool is_type_start(std::size_t ahead = 0) const {
        const Token& t = peek(ahead);
        switch (t.kind) {
            case Tok::kw_void: case Tok::kw_char: case Tok::kw_short: case Tok::kw_int:
            case Tok::kw_long: case Tok::kw_float: case Tok::kw_double:
            case Tok::kw_signed: case Tok::kw_unsigned:
            case Tok::kw_const: case Tok::kw_volatile:
            case Tok::kw_struct: case Tok::kw_union: case Tok::kw_enum:
            case Tok::kw_typedef: case Tok::kw_static: case Tok::kw_extern:
            case Tok::kw_auto: case Tok::kw_register:
            case Tok::kw_near: case Tok::kw_far: case Tok::kw_huge:
            case Tok::kw_cdecl: case Tok::kw_pascal: case Tok::kw_interrupt:
                return true;
            case Tok::ident:
                return typedefs_.count(t.text) > 0;
            default:
                return false;
        }
    }

    struct DeclSpecs {
        TypePtr base_type;
        bool is_typedef = false;
        bool is_static = false;
        bool is_extern = false;
        bool has_far = false;
        bool has_near = false;
        bool is_pascal = false;
        bool is_cdecl = false;
        bool is_interrupt = false;
    };

    DeclSpecs parse_decl_specs() {
        DeclSpecs ds;
        bool seen_type = false;
        bool is_unsigned = false;
        bool is_signed = false;
        bool is_long = false;
        bool is_short = false;
        bool is_char = false;
        bool is_void = false;
        bool is_float = false;
        bool is_double = false;
        bool is_int = false;
        TypePtr custom;

        while (true) {
            const Tok k = peek().kind;
            if (k == Tok::kw_typedef) { ds.is_typedef = true; ++pos_; }
            else if (k == Tok::kw_static) { ds.is_static = true; ++pos_; }
            else if (k == Tok::kw_extern) { ds.is_extern = true; ++pos_; }
            else if (k == Tok::kw_auto || k == Tok::kw_register ||
                     k == Tok::kw_const || k == Tok::kw_volatile) { ++pos_; }
            else if (k == Tok::kw_far || k == Tok::kw_huge) { ds.has_far = true; ++pos_; }
            else if (k == Tok::kw_near) { ds.has_near = true; ++pos_; }
            else if (k == Tok::kw_pascal) { ds.is_pascal = true; ++pos_; }
            else if (k == Tok::kw_cdecl) { ds.is_cdecl = true; ++pos_; }
            else if (k == Tok::kw_interrupt) { ds.is_interrupt = true; ++pos_; }
            else if (k == Tok::kw_void) { is_void = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_char) { is_char = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_short) { is_short = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_int) { is_int = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_long) { is_long = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_float) { is_float = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_double) { is_double = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_signed) { is_signed = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_unsigned) { is_unsigned = true; seen_type = true; ++pos_; }
            else if (k == Tok::kw_struct || k == Tok::kw_union) {
                custom = parse_struct_or_union();
                seen_type = true;
            } else if (k == Tok::kw_enum) {
                custom = parse_enum();
                seen_type = true;
            } else if (k == Tok::ident && !seen_type && typedefs_.count(peek().text)) {
                custom = typedefs_[peek().text];
                seen_type = true;
                ++pos_;
            } else {
                break;
            }
        }
        (void)is_signed;
        (void)is_short;
        (void)is_int;

        if (custom) ds.base_type = custom;
        else if (is_void) ds.base_type = make_prim(TypeKind::t_void);
        else if (is_char) ds.base_type = make_prim(is_unsigned ? TypeKind::t_uchar : TypeKind::t_char);
        else if (is_float) ds.base_type = make_prim(TypeKind::t_float);
        else if (is_double) ds.base_type = make_prim(TypeKind::t_double);
        else if (is_long) ds.base_type = make_prim(is_unsigned ? TypeKind::t_ulong : TypeKind::t_long);
        else if (is_unsigned) ds.base_type = make_prim(TypeKind::t_ushort);
        else ds.base_type = make_prim(TypeKind::t_short);

        return ds;
    }

    TypePtr parse_struct_or_union() {
        const bool is_union = match(Tok::kw_union);
        if (!is_union) consume(Tok::kw_struct, "expected struct");

        std::string tag;
        if (at(Tok::ident)) {
            tag = consume(Tok::ident, "").text;
        }

        TypePtr st;
        if (!tag.empty() && tag_types_.count(tag)) {
            st = tag_types_[tag];
        } else {
            st = std::make_shared<CType>();
            st->kind = is_union ? TypeKind::t_union : TypeKind::t_struct;
            st->complete = false;
            if (!tag.empty()) tag_types_[tag] = st;
        }

        if (!match(Tok::lbrace)) {
            return st;
        }

        st->fields.clear();
        std::size_t offset = 0;
        std::size_t max_size = 0;
        std::size_t max_align = 1;
        int cur_bit_off = 0;
        std::size_t cur_bit_unit_bytes = 0;

        while (!at(Tok::rbrace) && !at(Tok::eof)) {
            DeclSpecs fds = parse_decl_specs();
            if (match(Tok::semi)) continue;
            do {
                std::string fname;
                TypePtr ftype = parse_declarator(fds.base_type, fds, &fname);
                if (match(Tok::colon)) {
                    int width = static_cast<int>(eval_const_expr(parse_expr_cond()));
                    std::size_t unit = std::max<std::size_t>(1, type_size(ftype));
                    if (is_union) {
                        st->fields.push_back({fname, ftype, 0, 0, width});
                        max_size = std::max(max_size, unit);
                    } else {
                        if (width == 0 || cur_bit_unit_bytes != unit ||
                            cur_bit_off + width > static_cast<int>(unit * 8)) {
                            if (cur_bit_unit_bytes != 0) {
                                offset += cur_bit_unit_bytes;
                                cur_bit_off = 0;
                                cur_bit_unit_bytes = 0;
                            }
                            if (width > 0) {
                                std::size_t a = type_align(ftype, opts_.pack_structs);
                                offset = (offset + a - 1) & ~(a - 1);
                                cur_bit_unit_bytes = unit;
                                st->fields.push_back({fname, ftype, offset, 0, width});
                                cur_bit_off = width;
                            }
                        } else {
                            st->fields.push_back({fname, ftype, offset, cur_bit_off, width});
                            cur_bit_off += width;
                        }
                    }
                } else {
                    if (cur_bit_unit_bytes != 0) {
                        offset += cur_bit_unit_bytes;
                        cur_bit_off = 0;
                        cur_bit_unit_bytes = 0;
                    }
                    std::size_t a = type_align(ftype, opts_.pack_structs);
                    std::size_t sz = type_size(ftype);
                    max_align = std::max(max_align, a);
                    if (is_union) {
                        st->fields.push_back({fname, ftype, 0, -1, 0});
                        max_size = std::max(max_size, sz);
                    } else {
                        offset = (offset + a - 1) & ~(a - 1);
                        st->fields.push_back({fname, ftype, offset, -1, 0});
                        offset += sz;
                    }
                }
            } while (match(Tok::comma));
            consume(Tok::semi, "expected ';' after struct member");
        }
        consume(Tok::rbrace, "expected '}'");

        if (cur_bit_unit_bytes != 0) offset += cur_bit_unit_bytes;
        std::size_t final_size = is_union ? max_size : offset;
        if (!opts_.pack_structs && max_align > 1) {
            final_size = (final_size + max_align - 1) & ~(max_align - 1);
        }
        st->cached_size = final_size;
        st->cached_align = max_align;
        st->complete = true;
        return st;
    }

    TypePtr parse_enum() {
        consume(Tok::kw_enum, "expected enum");
        if (at(Tok::ident)) ++pos_;
        if (match(Tok::lbrace)) {
            std::int64_t next_val = 0;
            while (!at(Tok::rbrace) && !at(Tok::eof)) {
                std::string name = consume(Tok::ident, "expected enumerator").text;
                if (match(Tok::assign)) {
                    next_val = eval_const_expr(parse_expr_cond());
                }
                enum_consts_[name] = next_val++;
                if (!match(Tok::comma)) break;
            }
            consume(Tok::rbrace, "expected '}'");
        }
        return make_prim(TypeKind::t_short);
    }

    // Declarator parsing (handles pointers, near/far, parens, arrays, functions)
    struct DeclWrap {
        enum class Kind { ptr, array, func };
        Kind kind;
        bool is_far = false;
        bool is_pascal = false;
        bool is_interrupt = false;
        bool is_vararg = false;
        std::size_t array_len = 0;
        std::vector<ParamDecl> params;
    };

    TypePtr parse_declarator(TypePtr base, const DeclSpecs& ds, std::string* out_name) {
        std::vector<DeclWrap> wraps;
        parse_decl_inner(wraps, out_name, ds);
        // Build type outside-in from base: wrappers at the end wrap `base`?
        // Wait: `int *f()` -> ptr wraps `int`, func wraps `ptr`? No: in C `int *f()` means
        // `f` is a function returning `int*`! So `func` is applied LAST to `base`,
        // wait: `(*p)()` means `p` is pointer to function!
        // In our recursive descent, inner wraps are pushed BEFORE outer pointer stars:
        //   1. `*` pushes ptr to `prefix_ptrs`
        //   2. direct_declarator recurses into `(...)` and then parses trailing `[]` / `()`
        //   3. `prefix_ptrs` are appended AFTER trailing `[]`/`()`, so applying `wraps` in
        //      reverse order (outermost first) builds the exact C type!
        TypePtr cur = std::move(base);
        for (auto it = wraps.rbegin(); it != wraps.rend(); ++it) {
            if (it->kind == DeclWrap::Kind::ptr) {
                cur = make_ptr(std::move(cur), it->is_far);
            } else if (it->kind == DeclWrap::Kind::array) {
                cur = make_array(std::move(cur), it->array_len);
            } else {
                auto fn = std::make_shared<CType>();
                fn->kind = TypeKind::t_func;
                fn->base = std::move(cur);
                fn->params = it->params;
                fn->is_vararg = it->is_vararg;
                fn->is_far = it->is_far;
                fn->is_pascal = it->is_pascal;
                fn->is_interrupt = it->is_interrupt;
                fn->cached_size = fn->is_far ? 4 : 2;
                fn->cached_align = 2;
                cur = std::move(fn);
            }
        }
        return cur;
    }

    void parse_decl_inner(std::vector<DeclWrap>& wraps,
                          std::string* out_name,
                          const DeclSpecs& ds) {
        std::vector<DeclWrap> ptrs;
        bool decl_far = ds.has_far;
        bool decl_near = ds.has_near;
        bool decl_pascal = ds.is_pascal;
        bool decl_cdecl = ds.is_cdecl;
        bool decl_interrupt = ds.is_interrupt;
        while (true) {
            bool mod_far = ds.has_far;
            bool mod_near = ds.has_near;
            while (at(Tok::kw_const) || at(Tok::kw_volatile) ||
                   at(Tok::kw_near) || at(Tok::kw_far) || at(Tok::kw_huge) ||
                   at(Tok::kw_cdecl) || at(Tok::kw_pascal) || at(Tok::kw_interrupt)) {
                if (at(Tok::kw_far) || at(Tok::kw_huge)) {
                    mod_far = true; mod_near = false;
                    decl_far = true; decl_near = false;
                } else if (at(Tok::kw_near)) {
                    mod_near = true; mod_far = false;
                    decl_near = true; decl_far = false;
                } else if (at(Tok::kw_cdecl)) {
                    decl_cdecl = true; decl_pascal = false;
                } else if (at(Tok::kw_pascal)) {
                    decl_pascal = true; decl_cdecl = false;
                } else if (at(Tok::kw_interrupt)) {
                    decl_interrupt = true;
                }
                ++pos_;
            }
            if (!match(Tok::star)) break;
            bool is_far = mod_far ? true : (mod_near ? false : default_far_data());
            ptrs.push_back({DeclWrap::Kind::ptr, is_far, false, false, false, 0, {}});
        }

        std::vector<DeclWrap> inner_after;
        if (at(Tok::lparen) &&
            (peek(1).kind == Tok::star || peek(1).kind == Tok::lparen ||
             peek(1).kind == Tok::kw_near || peek(1).kind == Tok::kw_far ||
             peek(1).kind == Tok::kw_cdecl || peek(1).kind == Tok::kw_pascal)) {
            consume(Tok::lparen, "");
            parse_decl_inner(inner_after, out_name, ds);
            consume(Tok::rparen, "expected ')' in declarator");
        } else if (at(Tok::ident) && out_name != nullptr) {
            *out_name = consume(Tok::ident, "").text;
        }

        // Trailing [N] and (...)
        std::vector<DeclWrap> suffixes;
        while (true) {
            if (match(Tok::lbracket)) {
                std::size_t len = 0;
                if (!at(Tok::rbracket)) {
                    len = static_cast<std::size_t>(eval_const_expr(parse_expr_cond()));
                }
                consume(Tok::rbracket, "expected ']'");
                suffixes.push_back({DeclWrap::Kind::array, false, false, false, false, len, {}});
            } else if (match(Tok::lparen)) {
                std::vector<ParamDecl> params;
                bool is_vararg = false;
                if (!at(Tok::rparen)) {
                    if (at(Tok::kw_void) && peek(1).kind == Tok::rparen) {
                        ++pos_;
                    } else if (is_type_start()) {
                        while (!at(Tok::rparen) && !at(Tok::eof)) {
                            if (match(Tok::ellipsis)) {
                                is_vararg = true;
                                break;
                            }
                            DeclSpecs pds = parse_decl_specs();
                            std::string pname;
                            TypePtr ptype = parse_declarator(pds.base_type, pds, &pname);
                            if (ptype->kind == TypeKind::t_array) {
                                ptype = make_ptr(ptype->base, default_far_data());
                            } else if (ptype->kind == TypeKind::t_func) {
                                ptype = make_ptr(ptype, ptype->is_far);
                            }
                            params.push_back({pname, ptype});
                            if (!match(Tok::comma)) break;
                        }
                    } else {
                        // K&R identifier list: f(a, b)
                        while (at(Tok::ident)) {
                            params.push_back({consume(Tok::ident, "").text, make_prim(TypeKind::t_short)});
                            if (!match(Tok::comma)) break;
                        }
                    }
                }
                consume(Tok::rparen, "expected ')'");
                bool is_far = decl_far ? true : (decl_near ? false : default_far_code());
                bool is_pas = decl_pascal && !decl_cdecl;
                suffixes.push_back(
                    {DeclWrap::Kind::func, is_far, is_pas, decl_interrupt, is_vararg, 0, params});
            } else {
                break;
            }
        }

        for (auto& w : inner_after) wraps.push_back(std::move(w));
        for (auto& w : suffixes) wraps.push_back(std::move(w));
        for (auto& w : ptrs) wraps.push_back(std::move(w));
    }

    TypePtr parse_type_name() {
        DeclSpecs ds = parse_decl_specs();
        return parse_declarator(ds.base_type, ds, nullptr);
    }

    void parse_external_decl() {
        if (match(Tok::semi)) return;
        DeclSpecs ds = parse_decl_specs();
        if (match(Tok::semi)) return;

        while (true) {
            std::string name;
            TypePtr type = parse_declarator(ds.base_type, ds, &name);
            if (name.empty()) {
                consume(Tok::semi, "expected ';' or declarator");
                return;
            }

            if (ds.is_typedef) {
                typedefs_[name] = type;
                if (match(Tok::comma)) continue;
                consume(Tok::semi, "expected ';'");
                return;
            }

            if (type->kind == TypeKind::t_func &&
                (at(Tok::lbrace) || is_type_start())) {
                // Function definition (ANSI or K&R parameter declarations before '{')
                if (is_type_start()) {
                    std::unordered_map<std::string, TypePtr> kr_types;
                    while (is_type_start() && !at(Tok::lbrace) && !at(Tok::eof)) {
                        DeclSpecs pds = parse_decl_specs();
                        do {
                            std::string pname;
                            TypePtr ptype = parse_declarator(pds.base_type, pds, &pname);
                            if (ptype->kind == TypeKind::t_array) {
                                ptype = make_ptr(ptype->base, default_far_data());
                            }
                            if (!pname.empty()) kr_types[pname] = ptype;
                        } while (match(Tok::comma));
                        consume(Tok::semi, "expected ';' in K&R parameter declaration");
                    }
                    for (ParamDecl& p : type->params) {
                        auto it = kr_types.find(p.name);
                        if (it != kr_types.end()) p.type = it->second;
                    }
                }

                std::string asm_name = mangle_name(name, type->is_pascal, ds.is_static);
                global_syms[name] = {name, type, true, ds.is_static, false, 0, asm_name};

                FuncDef fn;
                fn.name = asm_name;
                fn.type = type;
                fn.is_static = ds.is_static;
                fn.params = type->params;

                local_scopes_.clear();
                local_scopes_.emplace_back();
                next_local_offset_ = 0;

                // Bind parameters at positive BP offsets
                int arg_off = type->is_far ? 6 : 4;
                if (type->is_interrupt) arg_off = 4;
                if (type->is_pascal) {
                    // Pascal pushes left-to-right, so last param is at lowest offset
                    for (auto it = fn.params.rbegin(); it != fn.params.rend(); ++it) {
                        std::size_t sz = std::max<std::size_t>(2, type_size(it->type));
                        if (sz % 2 != 0) ++sz;
                        if (!it->name.empty()) {
                            local_scopes_.back()[it->name] = {
                                it->name, it->type, false, false, false, arg_off, {}};
                        }
                        arg_off += static_cast<int>(sz);
                    }
                } else {
                    for (const ParamDecl& p : fn.params) {
                        std::size_t sz = std::max<std::size_t>(2, type_size(p.type));
                        if (sz % 2 != 0) ++sz;
                        if (!p.name.empty()) {
                            local_scopes_.back()[p.name] = {
                                p.name, p.type, false, false, false, arg_off, {}};
                        }
                        arg_off += static_cast<int>(sz);
                    }
                }

                fn.body = parse_compound_stmt();
                fn.frame_size = static_cast<std::size_t>(-next_local_offset_);
                local_scopes_.clear();
                funcs.push_back(std::move(fn));
                return;
            }

            // Global variable or function prototype
            const bool is_pascal = (type->kind == TypeKind::t_func) ? type->is_pascal : ds.is_pascal;
            std::string asm_name = mangle_name(name, is_pascal, ds.is_static);
            if (!global_syms.count(name) || !ds.is_extern) {
                global_syms[name] = {name, type, true, ds.is_static, ds.is_extern, 0, asm_name};
            }

            if (type->kind != TypeKind::t_func && !ds.is_extern) {
                GlobalVar gv;
                gv.name = asm_name;
                gv.type = type;
                gv.is_static = ds.is_static;
                if (match(Tok::assign)) {
                    gv.has_init = true;
                    parse_and_size_initializer(type, gv.inits);
                }
                globals.push_back(std::move(gv));
            } else if (match(Tok::assign)) {
                throw FormatError(peek().line, "cannot initialize extern declaration");
            }

            if (!match(Tok::comma)) break;
        }
        consume(Tok::semi, "expected ';' after declaration");
    }

    void parse_and_size_initializer(const TypePtr& type, std::vector<InitItem>& out) {
        parse_global_initializer(type, 0, out);
        if (type->kind == TypeKind::t_array && type->array_len == 0 && !out.empty()) {
            const std::size_t elem_sz = type_size(type->base);
            if (out.back().is_string_bytes) {
                type->array_len = out.back().str_bytes.size();
            } else if (elem_sz > 0) {
                type->array_len = (out.back().offset / elem_sz) + 1;
            }
            type->cached_size = elem_sz * type->array_len;
        }
    }

    void parse_global_initializer(const TypePtr& type,
                                  std::size_t base_off,
                                  std::vector<InitItem>& out) {
        if (type->kind == TypeKind::t_array && is_byte_scalar(type->base) && at(Tok::str_lit)) {
            std::string s = consume(Tok::str_lit, "").text;
            s.push_back('\0');
            if (type->array_len > 0 && s.size() > type->array_len) {
                s.resize(type->array_len);
            }
            InitItem item;
            item.offset = base_off;
            item.type = type;
            item.is_string_bytes = true;
            item.str_bytes = std::move(s);
            out.push_back(std::move(item));
            return;
        }
        if (match(Tok::lbrace)) {
            if (type->kind == TypeKind::t_array) {
                std::size_t idx = 0;
                const std::size_t elem_sz = type_size(type->base);
                while (!at(Tok::rbrace) && !at(Tok::eof)) {
                    parse_global_initializer(type->base, base_off + idx * elem_sz, out);
                    ++idx;
                    if (!match(Tok::comma)) break;
                }
            } else if (type->kind == TypeKind::t_struct || type->kind == TypeKind::t_union) {
                std::size_t fi = 0;
                while (!at(Tok::rbrace) && !at(Tok::eof)) {
                    if (fi < type->fields.size()) {
                        parse_global_initializer(type->fields[fi].type,
                                                 base_off + type->fields[fi].offset, out);
                        ++fi;
                    } else {
                        (void)parse_expr_assign();
                    }
                    if (!match(Tok::comma)) break;
                }
            } else {
                parse_global_initializer(type, base_off, out);
            }
            consume(Tok::rbrace, "expected '}' in initializer");
            return;
        }

        ExprPtr e = parse_expr_assign();
        InitItem item;
        item.offset = base_off;
        item.type = type;
        if (e->kind == ExprKind::str_lit) {
            item.sym_ref = "__STR:" + e->name;
        } else if (e->kind == ExprKind::unary && e->op == Tok::amp && e->lhs &&
                   e->lhs->kind == ExprKind::var) {
            auto it = global_syms.find(e->lhs->name);
            if (it != global_syms.end()) item.sym_ref = it->second.asm_name;
        } else if (e->kind == ExprKind::var && e->type &&
                   (e->type->kind == TypeKind::t_array || e->type->kind == TypeKind::t_func)) {
            auto it = global_syms.find(e->name);
            if (it != global_syms.end()) item.sym_ref = it->second.asm_name;
        } else {
            item.int_val = eval_const_expr(e);
        }
        out.push_back(std::move(item));
    }

    StmtPtr parse_compound_stmt() {
        consume(Tok::lbrace, "expected '{'");
        auto blk = std::make_shared<Stmt>();
        blk->kind = StmtKind::block;
        local_scopes_.emplace_back();

        while (!at(Tok::rbrace) && !at(Tok::eof)) {
            if (is_type_start()) {
                DeclSpecs ds = parse_decl_specs();
                if (match(Tok::semi)) continue;
                do {
                    std::string name;
                    TypePtr type = parse_declarator(ds.base_type, ds, &name);
                    if (name.empty()) break;
                    if (ds.is_typedef) {
                        typedefs_[name] = type;
                        continue;
                    }
                    if (ds.is_extern) {
                        std::string asm_name = mangle_name(name, ds.is_pascal, false);
                        global_syms[name] = {name, type, true, false, true, 0, asm_name};
                        continue;
                    }
                    if (ds.is_static) {
                        std::string asm_name = mangle_name(name, false, true);
                        local_scopes_.back()[name] = {name, type, true, true, false, 0, asm_name};
                        GlobalVar gv;
                        gv.name = asm_name;
                        gv.type = type;
                        gv.is_static = true;
                        if (match(Tok::assign)) {
                            gv.has_init = true;
                            parse_and_size_initializer(type, gv.inits);
                        }
                        globals.push_back(std::move(gv));
                        continue;
                    }
                    std::size_t sz = std::max<std::size_t>(2, type_size(type));
                    if (sz % 2 != 0) ++sz;
                    next_local_offset_ -= static_cast<int>(sz);
                    local_scopes_.back()[name] = {
                        name, type, false, false, false, next_local_offset_, {}};
                    ExprPtr init;
                    if (match(Tok::assign)) {
                        if (at(Tok::lbrace)) {
                            // Local aggregate brace initializer: consume for now
                            int depth = 1;
                            ++pos_;
                            while (!at(Tok::eof) && depth > 0) {
                                if (at(Tok::lbrace)) ++depth;
                                else if (at(Tok::rbrace)) --depth;
                                ++pos_;
                            }
                        } else {
                            init = parse_expr_assign();
                        }
                    }
                    blk->locals.push_back({name, type, next_local_offset_, init});
                } while (match(Tok::comma));
                consume(Tok::semi, "expected ';'");
            } else {
                blk->stmts.push_back(parse_stmt());
            }
        }
        consume(Tok::rbrace, "expected '}'");
        local_scopes_.pop_back();
        return blk;
    }

    StmtPtr parse_stmt() {
        if (at(Tok::lbrace)) return parse_compound_stmt();
        if (match(Tok::semi)) return std::make_shared<Stmt>();

        if (at(Tok::ident) && peek(1).kind == Tok::colon) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::label_stmt;
            s->label = consume(Tok::ident, "").text;
            consume(Tok::colon, "");
            s->body = parse_stmt();
            return s;
        }
        if (match(Tok::kw_if)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::if_stmt;
            consume(Tok::lparen, "expected '('");
            s->cond = parse_expr();
            consume(Tok::rparen, "expected ')'");
            s->body = parse_stmt();
            if (match(Tok::kw_else)) s->else_body = parse_stmt();
            return s;
        }
        if (match(Tok::kw_while)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::while_stmt;
            consume(Tok::lparen, "expected '('");
            s->cond = parse_expr();
            consume(Tok::rparen, "expected ')'");
            s->body = parse_stmt();
            return s;
        }
        if (match(Tok::kw_do)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::do_while_stmt;
            s->body = parse_stmt();
            consume(Tok::kw_while, "expected 'while'");
            consume(Tok::lparen, "expected '('");
            s->cond = parse_expr();
            consume(Tok::rparen, "expected ')'");
            consume(Tok::semi, "expected ';'");
            return s;
        }
        if (match(Tok::kw_for)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::for_stmt;
            consume(Tok::lparen, "expected '('");
            if (!at(Tok::semi)) s->expr = parse_expr();
            consume(Tok::semi, "expected ';'");
            if (!at(Tok::semi)) s->cond = parse_expr();
            consume(Tok::semi, "expected ';'");
            if (!at(Tok::rparen)) s->post = parse_expr();
            consume(Tok::rparen, "expected ')'");
            s->body = parse_stmt();
            return s;
        }
        if (match(Tok::kw_switch)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::switch_stmt;
            consume(Tok::lparen, "expected '('");
            s->cond = parse_expr();
            consume(Tok::rparen, "expected ')'");
            s->body = parse_stmt();
            return s;
        }
        if (match(Tok::kw_case)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::case_stmt;
            s->case_val = eval_const_expr(parse_expr_cond());
            consume(Tok::colon, "expected ':'");
            s->body = parse_stmt();
            return s;
        }
        if (match(Tok::kw_default)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::default_stmt;
            consume(Tok::colon, "expected ':'");
            s->body = parse_stmt();
            return s;
        }
        if (match(Tok::kw_break)) {
            consume(Tok::semi, "expected ';'");
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::break_stmt;
            return s;
        }
        if (match(Tok::kw_continue)) {
            consume(Tok::semi, "expected ';'");
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::continue_stmt;
            return s;
        }
        if (match(Tok::kw_goto)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::goto_stmt;
            s->label = consume(Tok::ident, "expected label").text;
            consume(Tok::semi, "expected ';'");
            return s;
        }
        if (match(Tok::kw_return)) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::return_stmt;
            if (!at(Tok::semi)) s->expr = parse_expr();
            consume(Tok::semi, "expected ';'");
            return s;
        }
        auto s = std::make_shared<Stmt>();
        s->kind = StmtKind::expr;
        s->expr = parse_expr();
        consume(Tok::semi, "expected ';'");
        return s;
    }

    const VarSymbol* lookup_var(const std::string& name) const {
        for (auto it = local_scopes_.rbegin(); it != local_scopes_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        auto gf = global_syms.find(name);
        if (gf != global_syms.end()) return &gf->second;
        return nullptr;
    }

    TypePtr common_arith_type(const TypePtr& a, const TypePtr& b) {
        if (!a) return b;
        if (!b) return a;
        if (a->kind == TypeKind::t_double || b->kind == TypeKind::t_double) return make_prim(TypeKind::t_double);
        if (a->kind == TypeKind::t_float || b->kind == TypeKind::t_float) return make_prim(TypeKind::t_float);
        if (a->kind == TypeKind::t_ulong || b->kind == TypeKind::t_ulong) return make_prim(TypeKind::t_ulong);
        if (a->kind == TypeKind::t_long || b->kind == TypeKind::t_long) return make_prim(TypeKind::t_long);
        if (a->kind == TypeKind::t_ushort || b->kind == TypeKind::t_ushort) return make_prim(TypeKind::t_ushort);
        return make_prim(TypeKind::t_short);
    }

    ExprPtr make_bin(Tok op, ExprPtr l, ExprPtr r) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::binary;
        e->op = op;
        if ((op == Tok::plus || op == Tok::minus) && l->type &&
            (l->type->kind == TypeKind::t_ptr || l->type->kind == TypeKind::t_array)) {
            TypePtr base = l->type->base;
            if (r->type && (r->type->kind == TypeKind::t_ptr || r->type->kind == TypeKind::t_array)) {
                e->type = make_prim(TypeKind::t_short);
            } else {
                e->type = l->type->kind == TypeKind::t_array
                              ? make_ptr(base, default_far_data())
                              : l->type;
            }
        } else if (op == Tok::eq || op == Tok::ne || op == Tok::lt || op == Tok::le ||
                   op == Tok::gt || op == Tok::ge || op == Tok::land || op == Tok::lor) {
            e->type = make_prim(TypeKind::t_short);
        } else {
            e->type = common_arith_type(l->type, r->type);
        }
        e->lhs = std::move(l);
        e->rhs = std::move(r);
        return e;
    }

    ExprPtr parse_expr() {
        ExprPtr e = parse_expr_assign();
        while (match(Tok::comma)) {
            ExprPtr r = parse_expr_assign();
            auto c = std::make_shared<Expr>();
            c->kind = ExprKind::comma;
            c->type = r->type;
            c->lhs = std::move(e);
            c->rhs = std::move(r);
            e = std::move(c);
        }
        return e;
    }

    ExprPtr parse_expr_assign() {
        ExprPtr l = parse_expr_cond();
        const Tok k = peek().kind;
        if (k == Tok::assign) {
            ++pos_;
            ExprPtr r = parse_expr_assign();
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::assign;
            e->type = l->type;
            e->lhs = std::move(l);
            e->rhs = std::move(r);
            return e;
        }
        if (k == Tok::plus_eq || k == Tok::minus_eq || k == Tok::star_eq ||
            k == Tok::slash_eq || k == Tok::percent_eq || k == Tok::amp_eq ||
            k == Tok::pipe_eq || k == Tok::caret_eq || k == Tok::shl_eq || k == Tok::shr_eq) {
            ++pos_;
            ExprPtr r = parse_expr_assign();
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::compound_assign;
            e->op = k;
            e->type = l->type;
            e->lhs = std::move(l);
            e->rhs = std::move(r);
            return e;
        }
        return l;
    }

    ExprPtr parse_expr_cond() {
        ExprPtr c = parse_expr_lor();
        if (match(Tok::question)) {
            ExprPtr t = parse_expr();
            consume(Tok::colon, "expected ':'");
            ExprPtr f = parse_expr_cond();
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::ternary;
            e->type = t->type;
            e->lhs = std::move(c);
            e->rhs = std::move(t);
            e->third = std::move(f);
            return e;
        }
        return c;
    }

    ExprPtr parse_expr_lor() {
        ExprPtr e = parse_expr_land();
        while (match(Tok::lor)) e = make_bin(Tok::lor, std::move(e), parse_expr_land());
        return e;
    }
    ExprPtr parse_expr_land() {
        ExprPtr e = parse_expr_bitor();
        while (match(Tok::land)) e = make_bin(Tok::land, std::move(e), parse_expr_bitor());
        return e;
    }
    ExprPtr parse_expr_bitor() {
        ExprPtr e = parse_expr_bitxor();
        while (match(Tok::pipe)) e = make_bin(Tok::pipe, std::move(e), parse_expr_bitxor());
        return e;
    }
    ExprPtr parse_expr_bitxor() {
        ExprPtr e = parse_expr_bitand();
        while (match(Tok::caret)) e = make_bin(Tok::caret, std::move(e), parse_expr_bitand());
        return e;
    }
    ExprPtr parse_expr_bitand() {
        ExprPtr e = parse_expr_eq();
        while (match(Tok::amp)) e = make_bin(Tok::amp, std::move(e), parse_expr_eq());
        return e;
    }
    ExprPtr parse_expr_eq() {
        ExprPtr e = parse_expr_rel();
        while (at(Tok::eq) || at(Tok::ne)) {
            Tok op = peek().kind;
            ++pos_;
            e = make_bin(op, std::move(e), parse_expr_rel());
        }
        return e;
    }
    ExprPtr parse_expr_rel() {
        ExprPtr e = parse_expr_shift();
        while (at(Tok::lt) || at(Tok::le) || at(Tok::gt) || at(Tok::ge)) {
            Tok op = peek().kind;
            ++pos_;
            e = make_bin(op, std::move(e), parse_expr_shift());
        }
        return e;
    }
    ExprPtr parse_expr_shift() {
        ExprPtr e = parse_expr_add();
        while (at(Tok::shl) || at(Tok::shr)) {
            Tok op = peek().kind;
            ++pos_;
            e = make_bin(op, std::move(e), parse_expr_add());
        }
        return e;
    }
    ExprPtr parse_expr_add() {
        ExprPtr e = parse_expr_mul();
        while (at(Tok::plus) || at(Tok::minus)) {
            Tok op = peek().kind;
            ++pos_;
            e = make_bin(op, std::move(e), parse_expr_mul());
        }
        return e;
    }
    ExprPtr parse_expr_mul() {
        ExprPtr e = parse_expr_unary();
        while (at(Tok::star) || at(Tok::slash) || at(Tok::percent)) {
            Tok op = peek().kind;
            ++pos_;
            e = make_bin(op, std::move(e), parse_expr_unary());
        }
        return e;
    }

    ExprPtr parse_expr_unary() {
        if (at(Tok::lparen) && is_type_start(1)) {
            consume(Tok::lparen, "");
            TypePtr target = parse_type_name();
            consume(Tok::rparen, "expected ')'");
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::cast;
            e->type = target;
            e->lhs = parse_expr_unary();
            return e;
        }
        if (match(Tok::kw_sizeof)) {
            std::size_t sz = 2;
            if (at(Tok::lparen) && is_type_start(1)) {
                consume(Tok::lparen, "");
                sz = type_size(parse_type_name());
                consume(Tok::rparen, "expected ')'");
            } else {
                ExprPtr sub = parse_expr_unary();
                sz = type_size(sub->type);
            }
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::int_lit;
            e->type = make_prim(TypeKind::t_ushort);
            e->int_val = static_cast<std::int64_t>(sz);
            return e;
        }
        const Tok k = peek().kind;
        if (k == Tok::inc || k == Tok::dec || k == Tok::plus || k == Tok::minus ||
            k == Tok::tilde || k == Tok::bang || k == Tok::star || k == Tok::amp) {
            ++pos_;
            ExprPtr sub = parse_expr_unary();
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::unary;
            e->op = k;
            e->lhs = sub;
            if (k == Tok::bang) e->type = make_prim(TypeKind::t_short);
            else if (k == Tok::star) {
                e->type = (sub->type && sub->type->base) ? sub->type->base
                                                         : make_prim(TypeKind::t_short);
            } else if (k == Tok::amp) {
                e->type = make_ptr(sub->type, default_far_data());
            } else {
                e->type = sub->type;
            }
            return e;
        }
        return parse_expr_postfix();
    }

    ExprPtr parse_expr_postfix() {
        ExprPtr e = parse_expr_primary();
        while (true) {
            if (match(Tok::lbracket)) {
                ExprPtr idx = parse_expr();
                consume(Tok::rbracket, "expected ']'");
                ExprPtr added = make_bin(Tok::plus, std::move(e), std::move(idx));
                auto deref = std::make_shared<Expr>();
                deref->kind = ExprKind::unary;
                deref->op = Tok::star;
                deref->lhs = added;
                deref->type = (added->type && added->type->base)
                                  ? added->type->base
                                  : make_prim(TypeKind::t_short);
                e = std::move(deref);
            } else if (match(Tok::lparen)) {
                auto call = std::make_shared<Expr>();
                call->kind = ExprKind::call;
                call->lhs = std::move(e);
                TypePtr fn_type = call->lhs->type;
                if (fn_type && fn_type->kind == TypeKind::t_ptr && fn_type->base) {
                    fn_type = fn_type->base;
                }
                call->type = (fn_type && fn_type->kind == TypeKind::t_func && fn_type->base)
                                 ? fn_type->base
                                 : make_prim(TypeKind::t_short);
                while (!at(Tok::rparen) && !at(Tok::eof)) {
                    call->args.push_back(parse_expr_assign());
                    if (!match(Tok::comma)) break;
                }
                consume(Tok::rparen, "expected ')'");
                e = std::move(call);
            } else if (at(Tok::dot) || at(Tok::arrow)) {
                const bool arrow = match(Tok::arrow);
                if (!arrow) consume(Tok::dot, "");
                std::string fname = consume(Tok::ident, "expected field name").text;
                TypePtr st = arrow && e->type ? e->type->base : e->type;
                TypePtr ftype = make_prim(TypeKind::t_short);
                if (st) {
                    for (const StructField& f : st->fields) {
                        if (f.name == fname) {
                            ftype = f.type;
                            break;
                        }
                    }
                }
                auto mem = std::make_shared<Expr>();
                mem->kind = ExprKind::member;
                mem->is_arrow = arrow;
                mem->name = std::move(fname);
                mem->lhs = std::move(e);
                mem->type = std::move(ftype);
                e = std::move(mem);
            } else if (at(Tok::inc) || at(Tok::dec)) {
                Tok op = peek().kind;
                ++pos_;
                auto post = std::make_shared<Expr>();
                post->kind = ExprKind::unary;
                post->op = op;
                post->is_postfix = true;
                post->type = e->type;
                post->lhs = std::move(e);
                e = std::move(post);
            } else {
                break;
            }
        }
        return e;
    }

    ExprPtr parse_expr_primary() {
        if (match(Tok::lparen)) {
            ExprPtr e = parse_expr();
            consume(Tok::rparen, "expected ')'");
            return e;
        }
        if (at(Tok::int_lit)) {
            Token t = consume(Tok::int_lit, "");
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::int_lit;
            e->int_val = static_cast<std::int64_t>(t.int_val);
            if (t.is_long || t.int_val > 0xFFFF) {
                e->type = make_prim(t.is_unsigned ? TypeKind::t_ulong : TypeKind::t_long);
            } else if (t.is_unsigned || t.int_val > 0x7FFF) {
                e->type = make_prim(TypeKind::t_ushort);
            } else {
                e->type = make_prim(TypeKind::t_short);
            }
            return e;
        }
        if (at(Tok::str_lit)) {
            Token t = consume(Tok::str_lit, "");
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::str_lit;
            e->name = std::move(t.text);
            e->type = make_ptr(make_prim(TypeKind::t_char), default_far_data());
            return e;
        }
        if (at(Tok::ident)) {
            Token t = consume(Tok::ident, "");
            auto ec = enum_consts_.find(t.text);
            if (ec != enum_consts_.end()) {
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::int_lit;
                e->int_val = ec->second;
                e->type = make_prim(TypeKind::t_short);
                return e;
            }
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::var;
            e->name = t.text;
            const VarSymbol* sym = lookup_var(t.text);
            if (sym) {
                e->type = sym->type;
                if (sym->is_global) e->resolved_asm_name = sym->asm_name;
            } else if (at(Tok::lparen)) {
                // Implicitly declared external function returning int
                auto fn = std::make_shared<CType>();
                fn->kind = TypeKind::t_func;
                fn->base = make_prim(TypeKind::t_short);
                fn->is_far = default_far_code();
                fn->cached_size = fn->is_far ? 4 : 2;
                global_syms[t.text] = {t.text, fn, true, false, true, 0, "_" + t.text};
                e->type = fn;
            } else {
                e->type = make_prim(TypeKind::t_short);
            }
            return e;
        }
        throw FormatError(peek().line, "expected expression");
    }

    std::int64_t eval_const_expr(const ExprPtr& e) const {
        if (!e) return 0;
        switch (e->kind) {
            case ExprKind::int_lit: return e->int_val;
            case ExprKind::cast: return eval_const_expr(e->lhs);
            case ExprKind::unary: {
                std::int64_t v = eval_const_expr(e->lhs);
                if (e->op == Tok::minus) return -v;
                if (e->op == Tok::plus) return v;
                if (e->op == Tok::tilde) return ~v;
                if (e->op == Tok::bang) return v == 0 ? 1 : 0;
                return v;
            }
            case ExprKind::binary: {
                std::int64_t l = eval_const_expr(e->lhs);
                std::int64_t r = eval_const_expr(e->rhs);
                switch (e->op) {
                    case Tok::plus: return l + r;
                    case Tok::minus: return l - r;
                    case Tok::star: return l * r;
                    case Tok::slash: return r != 0 ? l / r : 0;
                    case Tok::percent: return r != 0 ? l % r : 0;
                    case Tok::amp: return l & r;
                    case Tok::pipe: return l | r;
                    case Tok::caret: return l ^ r;
                    case Tok::shl: return l << r;
                    case Tok::shr: return l >> r;
                    case Tok::eq: return l == r;
                    case Tok::ne: return l != r;
                    case Tok::lt: return l < r;
                    case Tok::le: return l <= r;
                    case Tok::gt: return l > r;
                    case Tok::ge: return l >= r;
                    case Tok::land: return (l != 0) && (r != 0);
                    case Tok::lor: return (l != 0) || (r != 0);
                    default: return 0;
                }
            }
            case ExprKind::ternary:
                return eval_const_expr(e->lhs) ? eval_const_expr(e->rhs)
                                              : eval_const_expr(e->third);
            default:
                return 0;
        }
    }
};

// ---------------------------------------------------------------------------
// 6. 16-bit x86 Code Generator
// ---------------------------------------------------------------------------

class Codegen {
public:
    Codegen(OmfBuilder& omf, const Parser& parser, const CompileOptions& opts)
        : omf_(omf), parser_(parser), opts_(opts) {}

    void emit_all() {
        // 1. Emit globals into _DATA / _BSS / CONST
        for (const GlobalVar& gv : parser_.globals) {
            std::size_t sz = std::max<std::size_t>(1, type_size(gv.type));
            if (!gv.has_init) {
                std::size_t off = omf_.bss_size;
                omf_.bss_size += sz;
                global_locs_[gv.name] = {4, static_cast<std::uint16_t>(off)};
                if (!gv.is_static) omf_.add_pub(gv.name, 4, static_cast<std::uint16_t>(off));
                continue;
            }
            for (const InitItem& item : gv.inits) {
                const std::size_t item_end =
                    item.offset + (item.is_string_bytes ? item.str_bytes.size()
                                                        : type_size(item.type));
                if (item_end > sz) sz = item_end;
            }
            std::size_t base = omf_.data.data.size();
            omf_.data.data.resize(base + sz, 0);
            global_locs_[gv.name] = {2, static_cast<std::uint16_t>(base)};
            if (!gv.is_static) omf_.add_pub(gv.name, 2, static_cast<std::uint16_t>(base));

            for (const InitItem& item : gv.inits) {
                if (item.is_string_bytes) {
                    std::size_t copy_n = std::min(item.str_bytes.size(), sz - item.offset);
                    std::copy_n(item.str_bytes.begin(), copy_n,
                                omf_.data.data.begin() + static_cast<std::ptrdiff_t>(base + item.offset));
                    continue;
                }
                std::size_t at = base + item.offset;
                if (!item.sym_ref.empty()) {
                    if (item.sym_ref.rfind("__STR:", 0) == 0) {
                        std::uint16_t str_off = intern_string(item.sym_ref.substr(6));
                        if (item.type && item.type->is_far) {
                            omf_.data.fixups.push_back({at, FixupKind::far_ptr, 3, 0});
                        } else {
                            omf_.data.fixups.push_back({at, FixupKind::near_offset_dgroup, 3, 0});
                        }
                        write_u16(omf_.data.data, at, str_off);
                    } else {
                        int ext = omf_.intern_ext(item.sym_ref);
                        omf_.data.fixups.push_back(
                            {at, (item.type && item.type->is_far)
                                     ? FixupKind::far_ptr
                                     : FixupKind::near_offset_dgroup,
                             0, ext});
                    }
                    continue;
                }
                std::size_t isz = type_size(item.type);
                if (isz == 1) omf_.data.data[at] = static_cast<std::uint8_t>(item.int_val & 0xFF);
                else if (isz == 2) write_u16(omf_.data.data, at, static_cast<std::uint16_t>(item.int_val));
                else if (isz >= 4) {
                    write_u16(omf_.data.data, at, static_cast<std::uint16_t>(item.int_val & 0xFFFF));
                    write_u16(omf_.data.data, at + 2, static_cast<std::uint16_t>((item.int_val >> 16) & 0xFFFF));
                }
            }
        }

        // 2. Emit functions into _TEXT
        for (const FuncDef& fn : parser_.funcs) {
            emit_function(fn);
        }

        // Patch any intra-module references to globals/functions defined later in the file
        resolve_local_fixups();
    }

private:
    OmfBuilder& omf_;
    const Parser& parser_;
    CompileOptions opts_;

    struct Loc {
        int seg = 0;
        std::uint16_t offset = 0;
    };
    std::unordered_map<std::string, Loc> global_locs_;
    std::unordered_map<std::string, std::uint16_t> string_pool_;

    // Deferred fixups for symbols that might be defined later in the same module
    struct PendingCodeFixup {
        std::size_t code_offset = 0;
        FixupKind kind = FixupKind::near_call_rel;
        std::string sym_name;
    };
    std::vector<PendingCodeFixup> pending_fixups_;

    // Per-function state
    std::vector<std::unordered_map<std::string, std::pair<TypePtr, int>>> local_vars_;
    std::vector<std::size_t> return_jumps_;
    std::vector<std::vector<std::size_t>> break_stack_;
    std::vector<std::vector<std::size_t>> continue_stack_;
    std::unordered_map<std::string, std::size_t> user_labels_;
    std::vector<std::pair<std::size_t, std::string>> goto_patches_;
    const FuncDef* cur_func_ = nullptr;

    static void write_u16(std::vector<std::uint8_t>& buf, std::size_t at, std::uint16_t v) {
        buf[at] = static_cast<std::uint8_t>(v & 0xFF);
        buf[at + 1] = static_cast<std::uint8_t>(v >> 8);
    }

    void emit_u8(std::uint8_t b) { omf_.code.data.push_back(b); }
    void emit_u16(std::uint16_t w) {
        emit_u8(static_cast<std::uint8_t>(w & 0xFF));
        emit_u8(static_cast<std::uint8_t>(w >> 8));
    }

    std::size_t emit_jmp_rel16() {
        emit_u8(0xE9);
        std::size_t patch = omf_.code.data.size();
        emit_u16(0);
        return patch;
    }

    std::size_t emit_jcc_rel16(std::uint8_t jcc_opcode) {
        // 8086 compatible: j<!cc> +3; jmp rel16
        emit_u8(jcc_opcode ^ 0x01);
        emit_u8(0x03);
        return emit_jmp_rel16();
    }

    void patch_rel16(std::size_t patch_at, std::size_t target) {
        std::int32_t disp = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(patch_at + 2);
        write_u16(omf_.code.data, patch_at, static_cast<std::uint16_t>(disp));
    }

    std::uint16_t intern_string(const std::string& s) {
        auto it = string_pool_.find(s);
        if (it != string_pool_.end()) return it->second;
        std::uint16_t off = static_cast<std::uint16_t>(omf_.cnst.data.size());
        omf_.cnst.data.insert(omf_.cnst.data.end(), s.begin(), s.end());
        omf_.cnst.data.push_back(0);
        string_pool_[s] = off;
        return off;
    }

    // Emit mod/rm + disp for [bp + disp]
    void emit_bp_rm(std::uint8_t reg_field, int disp) {
        if (disp >= -128 && disp <= 127) {
            emit_u8(static_cast<std::uint8_t>(0x46 | (reg_field << 3)));
            emit_u8(static_cast<std::uint8_t>(disp & 0xFF));
        } else {
            emit_u8(static_cast<std::uint8_t>(0x86 | (reg_field << 3)));
            emit_u16(static_cast<std::uint16_t>(disp));
        }
    }

    void emit_function(const FuncDef& fn) {
        cur_func_ = &fn;
        return_jumps_.clear();
        break_stack_.clear();
        continue_stack_.clear();
        user_labels_.clear();
        goto_patches_.clear();
        local_vars_.clear();
        local_vars_.emplace_back();

        std::uint16_t fn_off = static_cast<std::uint16_t>(omf_.code.data.size());
        global_locs_[fn.name] = {1, fn_off};
        if (!fn.is_static) omf_.add_pub(fn.name, 1, fn_off);
        if (fn.name == "_main" && fn.params.size() != 1 && !opts_.omit_default_lib) {
            omf_.intern_ext("__acrtused");
        }

        // Bind params
        int arg_off = fn.type->is_far ? 6 : 4;
        if (fn.type->is_interrupt) arg_off = 4;
        auto bind_param = [&](const ParamDecl& p) {
            std::size_t sz = std::max<std::size_t>(2, type_size(p.type));
            if (sz % 2 != 0) ++sz;
            if (!p.name.empty()) local_vars_.back()[p.name] = {p.type, arg_off};
            arg_off += static_cast<int>(sz);
        };
        if (fn.type->is_pascal) {
            for (auto it = fn.params.rbegin(); it != fn.params.rend(); ++it) bind_param(*it);
        } else {
            for (const ParamDecl& p : fn.params) bind_param(p);
        }

        // Prologue
        const bool need_bp = fn.frame_size > 0 || !fn.params.empty() || fn.type->is_interrupt;
        if (fn.type->is_interrupt) {
            // push ax, bx, cx, dx, es, ds, si, di, bp
            for (std::uint8_t op : {0x50, 0x53, 0x51, 0x52, 0x06, 0x1E, 0x56, 0x57, 0x55}) {
                emit_u8(op);
            }
            emit_u8(0x8B); emit_u8(0xEC);  // mov bp, sp
        } else if (need_bp) {
            emit_u8(0x55);                 // push bp
            emit_u8(0x8B); emit_u8(0xEC);  // mov bp, sp
        }
        if (fn.frame_size > 0) {
            if (fn.frame_size <= 127) {
                emit_u8(0x83); emit_u8(0xEC);
                emit_u8(static_cast<std::uint8_t>(fn.frame_size));
            } else {
                emit_u8(0x81); emit_u8(0xEC);
                emit_u16(static_cast<std::uint16_t>(fn.frame_size));
            }
        }

        emit_stmt(fn.body);

        // Epilogue
        std::size_t epilogue = omf_.code.data.size();
        for (std::size_t j : return_jumps_) patch_rel16(j, epilogue);
        for (const auto& [j, lbl] : goto_patches_) {
            auto it = user_labels_.find(lbl);
            if (it == user_labels_.end()) {
                throw FormatError(0, "undefined label: " + lbl);
            }
            patch_rel16(j, it->second);
        }

        if (fn.frame_size > 0 || fn.type->is_interrupt) {
            emit_u8(0x8B); emit_u8(0xE5);  // mov sp, bp
        }
        if (fn.type->is_interrupt) {
            for (std::uint8_t op : {0x5D, 0x5F, 0x5E, 0x1F, 0x07, 0x5A, 0x59, 0x5B, 0x58, 0xCF}) {
                emit_u8(op);
            }
        } else {
            if (need_bp) emit_u8(0x5D);  // pop bp
            if (fn.type->is_pascal) {
                std::size_t param_bytes = 0;
                for (const ParamDecl& p : fn.params) {
                    std::size_t sz = std::max<std::size_t>(2, type_size(p.type));
                    if (sz % 2 != 0) ++sz;
                    param_bytes += sz;
                }
                if (param_bytes == 0) emit_u8(fn.type->is_far ? 0xCB : 0xC3);
                else {
                    emit_u8(fn.type->is_far ? 0xCA : 0xC2);
                    emit_u16(static_cast<std::uint16_t>(param_bytes));
                }
            } else {
                emit_u8(fn.type->is_far ? 0xCB : 0xC3);
            }
        }

        local_vars_.clear();
        cur_func_ = nullptr;
    }

    std::size_t emit_jump_if_false(const ExprPtr& cond) {
        if (cond && cond->kind == ExprKind::binary &&
            (cond->op == Tok::eq || cond->op == Tok::ne || cond->op == Tok::lt ||
             cond->op == Tok::le || cond->op == Tok::gt || cond->op == Tok::ge) &&
            !is_32bit_scalar(cond->lhs->type) && !is_32bit_scalar(cond->rhs->type)) {
            if (cond->rhs->kind == ExprKind::int_lit) {
                const std::uint16_t imm = static_cast<std::uint16_t>(cond->rhs->int_val);
                emit_expr(cond->lhs);
                if (imm == 0) {
                    emit_u8(0x85); emit_u8(0xC0);  // test ax, ax
                } else {
                    emit_u8(0x3D); emit_u16(imm);   // cmp ax, imm16
                }
            } else {
                emit_expr(cond->lhs);
                emit_u8(0x50);  // push ax
                emit_expr(cond->rhs);
                emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
                emit_u8(0x58);                 // pop ax
                emit_u8(0x3B); emit_u8(0xC1);  // cmp ax, cx
            }
            const bool is_signed = is_signed_type(cond->lhs->type) && is_signed_type(cond->rhs->type);
            std::uint8_t jcc = 0x74;
            if (cond->op == Tok::eq) jcc = 0x74;
            else if (cond->op == Tok::ne) jcc = 0x75;
            else if (cond->op == Tok::lt) jcc = is_signed ? 0x7C : 0x72;
            else if (cond->op == Tok::le) jcc = is_signed ? 0x7E : 0x76;
            else if (cond->op == Tok::gt) jcc = is_signed ? 0x7F : 0x77;
            else if (cond->op == Tok::ge) jcc = is_signed ? 0x7D : 0x73;
            return emit_jcc_rel16(static_cast<std::uint8_t>(jcc ^ 0x01));
        }
        emit_expr(cond);
        test_ax_or_dxax(cond->type);
        return emit_jcc_rel16(0x74);  // jz
    }

    void emit_stmt(const StmtPtr& s) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::empty:
                break;
            case StmtKind::expr:
                emit_expr(s->expr);
                break;
            case StmtKind::block:
                local_vars_.emplace_back();
                for (const LocalVar& lv : s->locals) {
                    local_vars_.back()[lv.name] = {lv.type, lv.bp_offset};
                    if (lv.init) {
                        emit_expr(lv.init);
                        if (is_32bit_scalar(lv.type)) emit_widen_dxax(lv.init->type);
                        store_local(lv.type, lv.bp_offset);
                    }
                }
                for (const StmtPtr& sub : s->stmts) emit_stmt(sub);
                local_vars_.pop_back();
                break;
            case StmtKind::if_stmt: {
                std::size_t j_false = emit_jump_if_false(s->cond);
                emit_stmt(s->body);
                if (s->else_body) {
                    std::size_t j_end = emit_jmp_rel16();
                    patch_rel16(j_false, omf_.code.data.size());
                    emit_stmt(s->else_body);
                    patch_rel16(j_end, omf_.code.data.size());
                } else {
                    patch_rel16(j_false, omf_.code.data.size());
                }
                break;
            }
            case StmtKind::while_stmt: {
                std::size_t top = omf_.code.data.size();
                break_stack_.emplace_back();
                continue_stack_.emplace_back();
                std::size_t j_exit = emit_jump_if_false(s->cond);
                emit_stmt(s->body);
                std::size_t cont_target = top;
                for (std::size_t c : continue_stack_.back()) patch_rel16(c, cont_target);
                std::size_t j_back = emit_jmp_rel16();
                patch_rel16(j_back, top);
                std::size_t after = omf_.code.data.size();
                patch_rel16(j_exit, after);
                for (std::size_t b : break_stack_.back()) patch_rel16(b, after);
                break_stack_.pop_back();
                continue_stack_.pop_back();
                break;
            }
            case StmtKind::do_while_stmt: {
                std::size_t top = omf_.code.data.size();
                break_stack_.emplace_back();
                continue_stack_.emplace_back();
                emit_stmt(s->body);
                std::size_t cond_pos = omf_.code.data.size();
                for (std::size_t c : continue_stack_.back()) patch_rel16(c, cond_pos);
                emit_expr(s->cond);
                test_ax_or_dxax(s->cond->type);
                std::size_t j_top = emit_jcc_rel16(0x75);  // jnz
                patch_rel16(j_top, top);
                std::size_t after = omf_.code.data.size();
                for (std::size_t b : break_stack_.back()) patch_rel16(b, after);
                break_stack_.pop_back();
                continue_stack_.pop_back();
                break;
            }
            case StmtKind::for_stmt: {
                if (s->expr) emit_expr(s->expr);
                std::size_t cond_pos = omf_.code.data.size();
                std::optional<std::size_t> j_exit;
                if (s->cond) {
                    j_exit = emit_jump_if_false(s->cond);
                }
                break_stack_.emplace_back();
                continue_stack_.emplace_back();
                emit_stmt(s->body);
                std::size_t post_pos = omf_.code.data.size();
                for (std::size_t c : continue_stack_.back()) patch_rel16(c, post_pos);
                if (s->post) emit_expr(s->post);
                std::size_t j_back = emit_jmp_rel16();
                patch_rel16(j_back, cond_pos);
                std::size_t after = omf_.code.data.size();
                if (j_exit.has_value()) patch_rel16(*j_exit, after);
                for (std::size_t b : break_stack_.back()) patch_rel16(b, after);
                break_stack_.pop_back();
                continue_stack_.pop_back();
                break;
            }
            case StmtKind::switch_stmt: {
                emit_expr(s->cond);
                emit_u8(0x50);  // push ax
                std::size_t j_dispatch = emit_jmp_rel16();
                break_stack_.emplace_back();
                std::vector<std::pair<std::int64_t, std::size_t>> cases;
                std::optional<std::size_t> def_pos;
                emit_switch_body(s->body, cases, def_pos);
                std::size_t j_end = emit_jmp_rel16();

                patch_rel16(j_dispatch, omf_.code.data.size());
                emit_u8(0x58);  // pop ax
                for (const auto& [val, target] : cases) {
                    emit_u8(0x3D); emit_u16(static_cast<std::uint16_t>(val));  // cmp ax, imm16
                    std::size_t jc = emit_jcc_rel16(0x74);
                    patch_rel16(jc, target);
                }
                if (def_pos.has_value()) {
                    std::size_t jd = emit_jmp_rel16();
                    patch_rel16(jd, *def_pos);
                }
                std::size_t after = omf_.code.data.size();
                patch_rel16(j_end, after);
                for (std::size_t b : break_stack_.back()) patch_rel16(b, after);
                break_stack_.pop_back();
                break;
            }
            case StmtKind::case_stmt:
            case StmtKind::default_stmt:
                emit_stmt(s->body);
                break;
            case StmtKind::break_stmt:
                if (!break_stack_.empty()) break_stack_.back().push_back(emit_jmp_rel16());
                break;
            case StmtKind::continue_stmt:
                if (!continue_stack_.empty()) continue_stack_.back().push_back(emit_jmp_rel16());
                break;
            case StmtKind::return_stmt:
                if (s->expr) emit_expr(s->expr);
                return_jumps_.push_back(emit_jmp_rel16());
                break;
            case StmtKind::goto_stmt:
                goto_patches_.emplace_back(emit_jmp_rel16(), s->label);
                break;
            case StmtKind::label_stmt:
                user_labels_[s->label] = omf_.code.data.size();
                emit_stmt(s->body);
                break;
        }
    }

    void emit_switch_body(const StmtPtr& s,
                          std::vector<std::pair<std::int64_t, std::size_t>>& cases,
                          std::optional<std::size_t>& def_pos) {
        if (!s) return;
        if (s->kind == StmtKind::block) {
            local_vars_.emplace_back();
            for (const StmtPtr& sub : s->stmts) emit_switch_body(sub, cases, def_pos);
            local_vars_.pop_back();
        } else if (s->kind == StmtKind::case_stmt) {
            cases.emplace_back(s->case_val, omf_.code.data.size());
            emit_switch_body(s->body, cases, def_pos);
        } else if (s->kind == StmtKind::default_stmt) {
            def_pos = omf_.code.data.size();
            emit_switch_body(s->body, cases, def_pos);
        } else {
            emit_stmt(s);
        }
    }

    void test_ax_or_dxax(const TypePtr& t) {
        if (is_32bit_scalar(t)) {
            emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax
            emit_u8(0x0B); emit_u8(0xDA);  // or bx, dx
        } else {
            emit_u8(0x0B); emit_u8(0xC0);  // or ax, ax
        }
    }

    std::optional<std::pair<TypePtr, int>> find_local(const std::string& name) const {
        for (auto it = local_vars_.rbegin(); it != local_vars_.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        return std::nullopt;
    }

    void store_local(const TypePtr& t, int bp_off) {
        if (is_byte_scalar(t)) {
            emit_u8(0x88); emit_bp_rm(0, bp_off);  // mov [bp+d], al
        } else if (is_32bit_scalar(t)) {
            emit_u8(0x89); emit_bp_rm(0, bp_off);      // mov [bp+d], ax
            emit_u8(0x89); emit_bp_rm(2, bp_off + 2);  // mov [bp+d+2], dx
        } else {
            emit_u8(0x89); emit_bp_rm(0, bp_off);  // mov [bp+d], ax
        }
    }

    // Loads address of an lvalue into BX (or ES:BX if far)
    void emit_lvalue_addr(const ExprPtr& e, bool& out_far) {
        out_far = false;
        if (e->kind == ExprKind::var) {
            if (auto loc = find_local(e->name)) {
                emit_u8(0x8D); emit_bp_rm(3, loc->second);  // lea bx, [bp+d]
                return;
            }
            auto gf = parser_.global_syms.find(e->name);
            std::string sym = !e->resolved_asm_name.empty()
                                  ? e->resolved_asm_name
                                  : (gf != parser_.global_syms.end() ? gf->second.asm_name
                                                                     : ("_" + e->name));
            emit_u8(0xBB);  // mov bx, imm16
            pending_fixups_.push_back({omf_.code.data.size(), FixupKind::near_offset_dgroup, sym});
            emit_u16(0);
            return;
        }
        if (e->kind == ExprKind::unary && e->op == Tok::star) {
            emit_expr(e->lhs);
            if (e->lhs->type && e->lhs->type->is_far) {
                emit_u8(0x8E); emit_u8(0xC2);  // mov es, dx
                emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax
                out_far = true;
            } else {
                emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax
            }
            return;
        }
        if (e->kind == ExprKind::member) {
            TypePtr st = e->is_arrow && e->lhs->type ? e->lhs->type->base : e->lhs->type;
            std::size_t foff = 0;
            if (st) {
                for (const StructField& f : st->fields) {
                    if (f.name == e->name) {
                        foff = f.offset;
                        break;
                    }
                }
            }
            if (e->is_arrow) {
                emit_expr(e->lhs);
                if (e->lhs->type && e->lhs->type->is_far) {
                    emit_u8(0x8E); emit_u8(0xC2);  // mov es, dx
                    out_far = true;
                }
                emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax
            } else {
                emit_lvalue_addr(e->lhs, out_far);
            }
            if (foff > 0) {
                emit_u8(0x81); emit_u8(0xC3); emit_u16(static_cast<std::uint16_t>(foff));  // add bx, foff
            }
            return;
        }
        if (e->kind == ExprKind::cast) {
            emit_lvalue_addr(e->lhs, out_far);
            return;
        }
        throw FormatError(0, "expression is not assignable");
    }

    void load_from_bx(const TypePtr& t, bool is_far) {
        if (is_far) emit_u8(0x26);  // es:
        if (is_byte_scalar(t)) {
            emit_u8(0x8A); emit_u8(0x07);  // mov al, [bx]
            if (is_signed_type(t)) emit_u8(0x98);  // cbw
            else { emit_u8(0x32); emit_u8(0xE4); }  // xor ah, ah
        } else if (is_32bit_scalar(t)) {
            emit_u8(0x8B); emit_u8(0x07);  // mov ax, [bx]
            if (is_far) emit_u8(0x26);
            emit_u8(0x8B); emit_u8(0x57); emit_u8(0x02);  // mov dx, [bx+2]
        } else {
            emit_u8(0x8B); emit_u8(0x07);  // mov ax, [bx]
        }
    }

    void store_to_bx(const TypePtr& t, bool is_far) {
        if (is_far) emit_u8(0x26);  // es:
        if (is_byte_scalar(t)) {
            emit_u8(0x88); emit_u8(0x07);  // mov [bx], al
        } else if (is_32bit_scalar(t)) {
            emit_u8(0x89); emit_u8(0x07);  // mov [bx], ax
            if (is_far) emit_u8(0x26);
            emit_u8(0x89); emit_u8(0x57); emit_u8(0x02);  // mov [bx+2], dx
        } else {
            emit_u8(0x89); emit_u8(0x07);  // mov [bx], ax
        }
    }

    void emit_expr(const ExprPtr& e) {
        if (!e) return;
        switch (e->kind) {
            case ExprKind::int_lit: {
                const std::uint16_t lo = static_cast<std::uint16_t>(e->int_val & 0xFFFF);
                if (lo == 0) {
                    emit_u8(0x33); emit_u8(0xC0);  // xor ax, ax
                } else {
                    emit_u8(0xB8); emit_u16(lo);   // mov ax, imm16
                }
                if (is_32bit_scalar(e->type)) {
                    const std::uint16_t hi = static_cast<std::uint16_t>((e->int_val >> 16) & 0xFFFF);
                    if (hi == 0) {
                        emit_u8(0x33); emit_u8(0xD2);  // xor dx, dx
                    } else {
                        emit_u8(0xBA); emit_u16(hi);   // mov dx, imm16
                    }
                }
                break;
            }

            case ExprKind::str_lit: {
                std::uint16_t off = intern_string(e->name);
                emit_u8(0xB8);  // mov ax, offset CONST:str
                omf_.code.fixups.push_back(
                    {omf_.code.data.size(), FixupKind::near_offset_dgroup, 3, 0});
                emit_u16(off);
                if (e->type && e->type->is_far) {
                    emit_u8(0x8C); emit_u8(0xDA);  // mov dx, ds
                }
                break;
            }

            case ExprKind::var: {
                if (e->type && (e->type->kind == TypeKind::t_array || e->type->kind == TypeKind::t_func)) {
                    bool is_far = false;
                    if (e->type->kind == TypeKind::t_func) {
                        auto gf = parser_.global_syms.find(e->name);
                        std::string sym = !e->resolved_asm_name.empty()
                                              ? e->resolved_asm_name
                                              : (gf != parser_.global_syms.end()
                                                     ? gf->second.asm_name
                                                     : ("_" + e->name));
                        emit_u8(0xB8);  // mov ax, offset sym
                        pending_fixups_.push_back({omf_.code.data.size(), FixupKind::near_offset_code, sym});
                        emit_u16(0);
                        if (e->type->is_far) {
                            emit_u8(0xBA);
                            pending_fixups_.push_back({omf_.code.data.size(), FixupKind::seg_only, sym});
                            emit_u16(0);
                        }
                    } else {
                        emit_lvalue_addr(e, is_far);
                        emit_u8(0x8B); emit_u8(0xC3);  // mov ax, bx
                        if (e->type->is_far) { emit_u8(0x8C); emit_u8(0xDA); }
                    }
                    break;
                }
                if (!is_32bit_scalar(e->type) &&
                    !(e->type && e->type->kind == TypeKind::t_struct)) {
                    if (auto loc = find_local(e->name)) {
                        if (is_byte_scalar(e->type)) {
                            emit_u8(0x8A); emit_bp_rm(0, loc->second);  // mov al, [bp+d]
                            if (is_signed_type(e->type)) emit_u8(0x98);
                            else { emit_u8(0x32); emit_u8(0xE4); }
                        } else {
                            emit_u8(0x8B); emit_bp_rm(0, loc->second);  // mov ax, [bp+d]
                        }
                        break;
                    }
                    auto gf = parser_.global_syms.find(e->name);
                    std::string sym = !e->resolved_asm_name.empty()
                                          ? e->resolved_asm_name
                                          : (gf != parser_.global_syms.end()
                                                 ? gf->second.asm_name
                                                 : ("_" + e->name));
                    if (is_byte_scalar(e->type)) {
                        emit_u8(0xA0);  // mov al, [sym]
                        pending_fixups_.push_back({omf_.code.data.size(), FixupKind::near_offset_dgroup, sym});
                        emit_u16(0);
                        if (is_signed_type(e->type)) emit_u8(0x98);
                        else { emit_u8(0x32); emit_u8(0xE4); }
                    } else {
                        emit_u8(0xA1);  // mov ax, [sym]
                        pending_fixups_.push_back({omf_.code.data.size(), FixupKind::near_offset_dgroup, sym});
                        emit_u16(0);
                    }
                    break;
                }
                bool is_far = false;
                emit_lvalue_addr(e, is_far);
                load_from_bx(e->type, is_far);
                break;
            }

            case ExprKind::member: {
                bool is_far = false;
                emit_lvalue_addr(e, is_far);
                if (e->type && e->type->kind == TypeKind::t_array) {
                    emit_u8(0x8B); emit_u8(0xC3);  // mov ax, bx
                    if (is_far) { emit_u8(0x8C); emit_u8(0xC2); }  // mov dx, es
                } else {
                    load_from_bx(e->type, is_far);
                }
                break;
            }

            case ExprKind::cast:
                emit_expr(e->lhs);
                if (is_byte_scalar(e->type)) {
                    if (is_signed_type(e->type)) emit_u8(0x98);
                    else { emit_u8(0x32); emit_u8(0xE4); }
                } else if (is_32bit_scalar(e->type) && !is_32bit_scalar(e->lhs->type)) {
                    const bool near_to_far =
                        e->type && e->type->kind == TypeKind::t_ptr && e->type->is_far &&
                        e->lhs->type &&
                        (e->lhs->type->kind == TypeKind::t_ptr ||
                         e->lhs->type->kind == TypeKind::t_array) &&
                        !e->lhs->type->is_far;
                    if (near_to_far) {
                        emit_u8(0x8C); emit_u8(0xDA);  // mov dx, ds
                    } else if (is_signed_type(e->lhs->type)) {
                        emit_u8(0x99);  // cwd
                    } else {
                        emit_u8(0x33); emit_u8(0xD2);  // xor dx, dx
                    }
                }
                break;

            case ExprKind::comma:
                emit_expr(e->lhs);
                emit_expr(e->rhs);
                break;

            case ExprKind::ternary: {
                emit_expr(e->lhs);
                test_ax_or_dxax(e->lhs->type);
                std::size_t j_false = emit_jcc_rel16(0x74);
                emit_expr(e->rhs);
                std::size_t j_end = emit_jmp_rel16();
                patch_rel16(j_false, omf_.code.data.size());
                emit_expr(e->third);
                patch_rel16(j_end, omf_.code.data.size());
                break;
            }

            case ExprKind::assign: {
                emit_expr(e->rhs);
                if (is_32bit_scalar(e->type)) emit_widen_dxax(e->rhs->type);
                if (e->lhs->kind == ExprKind::var && !is_32bit_scalar(e->type) &&
                    !(e->type && e->type->kind == TypeKind::t_struct)) {
                    if (auto loc = find_local(e->lhs->name)) {
                        if (is_byte_scalar(e->type)) {
                            emit_u8(0x88); emit_bp_rm(0, loc->second);  // mov [bp+d], al
                        } else {
                            emit_u8(0x89); emit_bp_rm(0, loc->second);  // mov [bp+d], ax
                        }
                        break;
                    }
                    auto gf = parser_.global_syms.find(e->lhs->name);
                    std::string sym = !e->lhs->resolved_asm_name.empty()
                                          ? e->lhs->resolved_asm_name
                                          : (gf != parser_.global_syms.end()
                                                 ? gf->second.asm_name
                                                 : ("_" + e->lhs->name));
                    emit_u8(is_byte_scalar(e->type) ? 0xA2 : 0xA3);  // mov [sym], al/ax
                    pending_fixups_.push_back({omf_.code.data.size(), FixupKind::near_offset_dgroup, sym});
                    emit_u16(0);
                    break;
                }
                emit_u8(0x50);  // push ax
                if (is_32bit_scalar(e->type)) emit_u8(0x52);  // push dx
                bool is_far = false;
                emit_lvalue_addr(e->lhs, is_far);
                if (is_32bit_scalar(e->type)) emit_u8(0x5A);  // pop dx
                emit_u8(0x58);  // pop ax
                store_to_bx(e->type, is_far);
                break;
            }

            case ExprKind::compound_assign: {
                bool is_far = false;
                emit_lvalue_addr(e->lhs, is_far);
                if (is_far) emit_u8(0x06);  // push es
                emit_u8(0x53);              // push bx
                load_from_bx(e->type, is_far);
                Tok bin_op = Tok::plus;
                if (e->op == Tok::minus_eq) bin_op = Tok::minus;
                else if (e->op == Tok::star_eq) bin_op = Tok::star;
                else if (e->op == Tok::slash_eq) bin_op = Tok::slash;
                else if (e->op == Tok::percent_eq) bin_op = Tok::percent;
                else if (e->op == Tok::amp_eq) bin_op = Tok::amp;
                else if (e->op == Tok::pipe_eq) bin_op = Tok::pipe;
                else if (e->op == Tok::caret_eq) bin_op = Tok::caret;
                else if (e->op == Tok::shl_eq) bin_op = Tok::shl;
                else if (e->op == Tok::shr_eq) bin_op = Tok::shr;
                if (is_32bit_scalar(e->type)) {
                    emit_u8(0x52);  // push dx (lhs hi)
                    emit_u8(0x50);  // push ax (lhs lo)
                    emit_expr(e->rhs);
                    emit_widen_dxax(e->rhs->type);
                    emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax (rhs lo)
                    emit_u8(0x8B); emit_u8(0xCA);  // mov cx, dx (rhs hi)
                    emit_u8(0x58);                 // pop ax (lhs lo)
                    emit_u8(0x5A);                 // pop dx (lhs hi)
                    emit_alu_32bit(bin_op, is_signed_type(e->type));
                } else {
                    emit_u8(0x50);  // push ax (lhs value)
                    emit_expr(e->rhs);
                    emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax (rhs)
                    emit_u8(0x58);                 // pop ax (lhs)
                    emit_alu_ax_cx(bin_op, e->type);
                }
                emit_u8(0x5B);  // pop bx
                if (is_far) emit_u8(0x07);  // pop es
                store_to_bx(e->type, is_far);
                break;
            }

            case ExprKind::unary: {
                if (e->op == Tok::amp) {
                    bool is_far = false;
                    emit_lvalue_addr(e->lhs, is_far);
                    emit_u8(0x8B); emit_u8(0xC3);  // mov ax, bx
                    if (e->type && e->type->is_far) {
                        if (is_far) { emit_u8(0x8C); emit_u8(0xC2); }
                        else { emit_u8(0x8C); emit_u8(0xDA); }
                    }
                    break;
                }
                if (e->op == Tok::star) {
                    emit_expr(e->lhs);
                    if (e->type && (e->type->kind == TypeKind::t_array ||
                                    e->type->kind == TypeKind::t_func)) {
                        break;
                    }
                    bool is_far = e->lhs->type && e->lhs->type->is_far;
                    if (is_far) { emit_u8(0x8E); emit_u8(0xC2); }  // mov es, dx
                    emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax
                    load_from_bx(e->type, is_far);
                    break;
                }
                if (e->op == Tok::inc || e->op == Tok::dec) {
                    bool is_far = false;
                    emit_lvalue_addr(e->lhs, is_far);
                    load_from_bx(e->type, is_far);
                    std::uint16_t step = 1;
                    if (e->type && e->type->kind == TypeKind::t_ptr && e->type->base) {
                        step = static_cast<std::uint16_t>(std::max<std::size_t>(1, type_size(e->type->base)));
                    }
                    const bool is32 = is_32bit_scalar(e->type);
                    if (e->is_postfix) {
                        if (is32) emit_u8(0x52);  // push dx
                        emit_u8(0x50);            // push ax
                    }
                    if (e->op == Tok::inc) {
                        emit_u8(0x05); emit_u16(step);  // add ax, step
                        if (is32) { emit_u8(0x83); emit_u8(0xD2); emit_u8(0x00); }  // adc dx, 0
                    } else {
                        emit_u8(0x2D); emit_u16(step);  // sub ax, step
                        if (is32) { emit_u8(0x83); emit_u8(0xDA); emit_u8(0x00); }  // sbb dx, 0
                    }
                    store_to_bx(e->type, is_far);
                    if (e->is_postfix) {
                        emit_u8(0x58);            // pop ax
                        if (is32) emit_u8(0x5A);  // pop dx
                    }
                    break;
                }
                emit_expr(e->lhs);
                if (e->op == Tok::minus) {
                    emit_u8(0xF7); emit_u8(0xD8);  // neg ax
                } else if (e->op == Tok::tilde) {
                    emit_u8(0xF7); emit_u8(0xD0);  // not ax
                } else if (e->op == Tok::bang) {
                    test_ax_or_dxax(e->lhs->type);
                    emit_u8(0xB8); emit_u16(1);  // mov ax, 1
                    emit_u8(0x74); emit_u8(2);   // jz +2
                    emit_u8(0x33); emit_u8(0xC0);  // xor ax, ax
                }
                break;
            }

            case ExprKind::binary: {
                if (e->op == Tok::land) {
                    emit_expr(e->lhs);
                    test_ax_or_dxax(e->lhs->type);
                    std::size_t j_false1 = emit_jcc_rel16(0x74);
                    emit_expr(e->rhs);
                    test_ax_or_dxax(e->rhs->type);
                    std::size_t j_false2 = emit_jcc_rel16(0x74);
                    emit_u8(0xB8); emit_u16(1);
                    std::size_t j_end = emit_jmp_rel16();
                    std::size_t f_pos = omf_.code.data.size();
                    patch_rel16(j_false1, f_pos);
                    patch_rel16(j_false2, f_pos);
                    emit_u8(0xB8); emit_u16(0);
                    patch_rel16(j_end, omf_.code.data.size());
                    break;
                }
                if (e->op == Tok::lor) {
                    emit_expr(e->lhs);
                    test_ax_or_dxax(e->lhs->type);
                    std::size_t j_true1 = emit_jcc_rel16(0x75);
                    emit_expr(e->rhs);
                    test_ax_or_dxax(e->rhs->type);
                    std::size_t j_true2 = emit_jcc_rel16(0x75);
                    emit_u8(0xB8); emit_u16(0);
                    std::size_t j_end = emit_jmp_rel16();
                    std::size_t t_pos = omf_.code.data.size();
                    patch_rel16(j_true1, t_pos);
                    patch_rel16(j_true2, t_pos);
                    emit_u8(0xB8); emit_u16(1);
                    patch_rel16(j_end, omf_.code.data.size());
                    break;
                }

                if (e->rhs->kind == ExprKind::int_lit && !is_32bit_scalar(e->type) &&
                    !is_32bit_scalar(e->lhs->type) &&
                    !(e->lhs->type && (e->lhs->type->kind == TypeKind::t_ptr ||
                                       e->lhs->type->kind == TypeKind::t_array))) {
                    const std::uint16_t imm = static_cast<std::uint16_t>(e->rhs->int_val);
                    if (e->op == Tok::plus && imm == 1) {
                        emit_expr(e->lhs);
                        emit_u8(0x40);  // inc ax
                        break;
                    }
                    if (e->op == Tok::minus && imm == 1) {
                        emit_expr(e->lhs);
                        emit_u8(0x48);  // dec ax
                        break;
                    }
                    if (e->op == Tok::shl && imm == 1) {
                        emit_expr(e->lhs);
                        emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
                        break;
                    }
                    if (e->op == Tok::shr && imm == 1) {
                        emit_expr(e->lhs);
                        emit_u8(0xD1);
                        emit_u8(is_signed_type(e->lhs->type) ? 0xF8 : 0xE8);  // sar/shr ax, 1
                        break;
                    }
                    if (e->op == Tok::plus && imm == 2) {
                        emit_expr(e->lhs);
                        emit_u8(0x40); emit_u8(0x40);  // inc ax; inc ax
                        break;
                    }
                    if (e->op == Tok::minus && imm == 1) {
                        emit_expr(e->lhs);
                        emit_u8(0x48);  // dec ax
                        break;
                    }
                    if (e->op == Tok::minus && imm == 2) {
                        emit_expr(e->lhs);
                        emit_u8(0x48); emit_u8(0x48);  // dec ax; dec ax
                        break;
                    }
                    if (e->op == Tok::plus || e->op == Tok::minus ||
                        e->op == Tok::amp || e->op == Tok::pipe || e->op == Tok::caret) {
                        emit_expr(e->lhs);
                        if (imm == 0 && e->op != Tok::amp) break;
                        const std::uint8_t opc = e->op == Tok::plus    ? 0x05
                                                 : e->op == Tok::minus ? 0x2D
                                                 : e->op == Tok::amp   ? 0x25
                                                 : e->op == Tok::pipe  ? 0x0D
                                                                       : 0x35;
                        emit_u8(opc);
                        emit_u16(imm);
                        break;
                    }
                    if (e->op == Tok::eq || e->op == Tok::ne || e->op == Tok::lt ||
                        e->op == Tok::le || e->op == Tok::gt || e->op == Tok::ge) {
                        emit_expr(e->lhs);
                        if (imm == 0) {
                            emit_u8(0x85); emit_u8(0xC0);  // test ax, ax
                        } else {
                            emit_u8(0x3D);  // cmp ax, imm16
                            emit_u16(imm);
                        }
                        emit_cmp_setcc(e->op, is_signed_type(e->lhs->type) && is_signed_type(e->rhs->type));
                        break;
                    }
                }

                {
                    const bool lhs_ptr = e->lhs->type && (e->lhs->type->kind == TypeKind::t_ptr ||
                                                          e->lhs->type->kind == TypeKind::t_array);
                    const bool rhs_ptr = e->rhs->type && (e->rhs->type->kind == TypeKind::t_ptr ||
                                                          e->rhs->type->kind == TypeKind::t_array);
                    if (!lhs_ptr && !rhs_ptr &&
                        (is_32bit_scalar(e->lhs->type) || is_32bit_scalar(e->rhs->type))) {
                        emit_binary_32bit(e);
                        break;
                    }
                }

                emit_expr(e->lhs);
                emit_u8(0x50);  // push ax
                emit_expr(e->rhs);
                emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax (rhs)
                emit_u8(0x58);                 // pop ax (lhs)

                // Scale pointer arithmetic
                if ((e->op == Tok::plus || e->op == Tok::minus) && e->lhs->type &&
                    (e->lhs->type->kind == TypeKind::t_ptr || e->lhs->type->kind == TypeKind::t_array)) {
                    std::size_t elem_sz = std::max<std::size_t>(1, type_size(e->lhs->type->base));
                    if (elem_sz > 1 && !(e->rhs->type &&
                                         (e->rhs->type->kind == TypeKind::t_ptr ||
                                          e->rhs->type->kind == TypeKind::t_array))) {
                        emit_u8(0x50);  // push ax
                        emit_u8(0xB8); emit_u16(static_cast<std::uint16_t>(elem_sz));
                        emit_u8(0xF7); emit_u8(0xE1);  // mul cx
                        emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
                        emit_u8(0x58);                 // pop ax
                    }
                }
                emit_alu_ax_cx(e->op, e->type,
                               is_signed_type(e->lhs->type) && is_signed_type(e->rhs->type));
                break;
            }

            case ExprKind::call: {
                TypePtr fn_type = e->lhs->type;
                if (fn_type && fn_type->kind == TypeKind::t_ptr && fn_type->base) {
                    fn_type = fn_type->base;
                }
                const bool is_far = fn_type && fn_type->is_far;
                const bool is_pascal = fn_type && fn_type->is_pascal;

                std::size_t arg_bytes = 0;
                auto push_arg = [&](const ExprPtr& arg) {
                    emit_expr(arg);
                    if (is_32bit_scalar(arg->type)) {
                        emit_u8(0x52);  // push dx (segment/high word first)
                        emit_u8(0x50);  // push ax (offset/low word)
                        arg_bytes += 4;
                    } else {
                        emit_u8(0x50);  // push ax
                        arg_bytes += 2;
                    }
                };
                if (is_pascal) {
                    for (const ExprPtr& arg : e->args) push_arg(arg);
                } else {
                    for (auto it = e->args.rbegin(); it != e->args.rend(); ++it) push_arg(*it);
                }

                if (e->lhs->kind == ExprKind::var && !find_local(e->lhs->name).has_value()) {
                    auto gf = parser_.global_syms.find(e->lhs->name);
                    std::string sym = !e->lhs->resolved_asm_name.empty()
                                          ? e->lhs->resolved_asm_name
                                          : (gf != parser_.global_syms.end()
                                                 ? gf->second.asm_name
                                                 : ("_" + e->lhs->name));
                    if (is_far) {
                        emit_u8(0x9A);  // call far seg:off
                        pending_fixups_.push_back({omf_.code.data.size(), FixupKind::far_ptr, sym});
                        emit_u16(0);
                        emit_u16(0);
                    } else {
                        emit_u8(0xE8);  // call near rel16
                        pending_fixups_.push_back({omf_.code.data.size(), FixupKind::near_call_rel, sym});
                        emit_u16(0);
                    }
                } else {
                    emit_expr(e->lhs);
                    if (is_far) {
                        // Store DX:AX in temporary [bp-...] or call via stack:
                        // push dx; push ax; mov bx, sp; call far ptr ss:[bx]; add sp, 4
                        emit_u8(0x52);
                        emit_u8(0x50);
                        emit_u8(0x8B); emit_u8(0xDC);  // mov bx, sp
                        emit_u8(0x36); emit_u8(0xFF); emit_u8(0x1F);  // call far ptr ss:[bx]
                        emit_u8(0x83); emit_u8(0xC4); emit_u8(0x04);  // add sp, 4
                    } else {
                        emit_u8(0xFF); emit_u8(0xD0);  // call ax
                    }
                }

                if (!is_pascal && arg_bytes > 0) {
                    if (arg_bytes == 2) {
                        emit_u8(0x59);  // pop cx
                    } else if (arg_bytes == 4) {
                        emit_u8(0x59); emit_u8(0x59);  // pop cx; pop cx
                    } else if (arg_bytes <= 127) {
                        emit_u8(0x83); emit_u8(0xC4); emit_u8(static_cast<std::uint8_t>(arg_bytes));
                    } else {
                        emit_u8(0x81); emit_u8(0xC4); emit_u16(static_cast<std::uint16_t>(arg_bytes));
                    }
                }
                break;
            }
        }
    }

    void emit_cmp_setcc(Tok op, bool is_signed) {
        std::uint8_t jcc = 0x74;
        if (op == Tok::eq) jcc = 0x74;
        else if (op == Tok::ne) jcc = 0x75;
        else if (op == Tok::lt) jcc = is_signed ? 0x7C : 0x72;
        else if (op == Tok::le) jcc = is_signed ? 0x7E : 0x76;
        else if (op == Tok::gt) jcc = is_signed ? 0x7F : 0x77;
        else if (op == Tok::ge) jcc = is_signed ? 0x7D : 0x73;
        emit_u8(0xB8); emit_u16(1);   // mov ax, 1
        emit_u8(jcc);  emit_u8(2);    // jcc +2
        emit_u8(0x33); emit_u8(0xC0); // xor ax, ax
    }

    void emit_widen_dxax(const TypePtr& t) {
        if (is_32bit_scalar(t)) return;
        if (is_signed_type(t)) {
            emit_u8(0x99);  // cwd
        } else {
            emit_u8(0x33); emit_u8(0xD2);  // xor dx, dx
        }
    }

    void emit_binary_32bit(const ExprPtr& e) {
        emit_expr(e->lhs);
        emit_widen_dxax(e->lhs->type);
        emit_u8(0x52);  // push dx
        emit_u8(0x50);  // push ax
        emit_expr(e->rhs);
        emit_widen_dxax(e->rhs->type);
        emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax (rhs lo)
        emit_u8(0x8B); emit_u8(0xCA);  // mov cx, dx (rhs hi)
        emit_u8(0x58);                 // pop ax (lhs lo)
        emit_u8(0x5A);                 // pop dx (lhs hi)
        const bool is_signed = is_signed_type(e->lhs->type) && is_signed_type(e->rhs->type);
        emit_alu_32bit(e->op, is_signed);
    }

    void emit_alu_32bit(Tok op, bool is_signed) {
        switch (op) {
            case Tok::plus:
                emit_u8(0x03); emit_u8(0xC3);  // add ax, bx
                emit_u8(0x13); emit_u8(0xD1);  // adc dx, cx
                break;
            case Tok::minus:
                emit_u8(0x2B); emit_u8(0xC3);  // sub ax, bx
                emit_u8(0x1B); emit_u8(0xD1);  // sbb dx, cx
                break;
            case Tok::amp:
                emit_u8(0x23); emit_u8(0xC3);  // and ax, bx
                emit_u8(0x23); emit_u8(0xD1);  // and dx, cx
                break;
            case Tok::pipe:
                emit_u8(0x0B); emit_u8(0xC3);  // or ax, bx
                emit_u8(0x0B); emit_u8(0xD1);  // or dx, cx
                break;
            case Tok::caret:
                emit_u8(0x33); emit_u8(0xC3);  // xor ax, bx
                emit_u8(0x33); emit_u8(0xD1);  // xor dx, cx
                break;
            case Tok::star:
                emit_u8(0x56);                 // push si
                emit_u8(0x50);                 // push ax
                emit_u8(0x52);                 // push dx
                emit_u8(0xF7); emit_u8(0xE1);  // mul cx
                emit_u8(0x8B); emit_u8(0xF0);  // mov si, ax
                emit_u8(0x58);                 // pop ax (lhs hi)
                emit_u8(0xF7); emit_u8(0xE3);  // mul bx
                emit_u8(0x03); emit_u8(0xF0);  // add si, ax
                emit_u8(0x58);                 // pop ax (lhs lo)
                emit_u8(0xF7); emit_u8(0xE3);  // mul bx
                emit_u8(0x03); emit_u8(0xD6);  // add dx, si
                emit_u8(0x5E);                 // pop si
                break;
            case Tok::shl:
            case Tok::shr:
                emit_u8(0x8B); emit_u8(0xCB);  // mov cx, bx
                emit_u8(0xE3); emit_u8(0x06);  // jcxz +6
                if (op == Tok::shl) {
                    emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
                    emit_u8(0xD1); emit_u8(0xD2);  // rcl dx, 1
                } else {
                    emit_u8(0xD1); emit_u8(is_signed ? 0xFA : 0xEA);  // sar/shr dx, 1
                    emit_u8(0xD1); emit_u8(0xD8);  // rcr ax, 1
                }
                emit_u8(0xE2); emit_u8(0xFA);  // loop -6
                break;
            case Tok::eq:
            case Tok::ne:
                emit_u8(0x3B); emit_u8(0xD1);  // cmp dx, cx
                emit_u8(0x75); emit_u8(0x02);  // jne +2
                emit_u8(0x3B); emit_u8(0xC3);  // cmp ax, bx
                emit_cmp_setcc(op, false);
                break;
            case Tok::lt:
            case Tok::le:
            case Tok::gt:
            case Tok::ge:
                emit_u8(0x3B); emit_u8(0xD1);  // cmp dx, cx
                emit_u8(0x75); emit_u8(0x0A);  // jne +10
                emit_u8(0x3B); emit_u8(0xC3);  // cmp ax, bx
                emit_cmp_setcc(op, false);
                emit_u8(0xEB); emit_u8(0x06);  // jmp +6
                emit_cmp_setcc(op, is_signed);
                break;
            case Tok::slash:
            case Tok::percent: {
                emit_u8(0x56);  // push si
                emit_u8(0x57);  // push di
                emit_u8(0x55);  // push bp
                if (is_signed) {
                    emit_u8(0x8B); emit_u8(0xF2);  // mov si, dx
                    if (op == Tok::slash) {
                        emit_u8(0x33); emit_u8(0xF1);  // xor si, cx
                    }
                    emit_u8(0x56);  // push si
                    emit_u8(0x85); emit_u8(0xD2);  // test dx, dx
                    emit_u8(0x79); emit_u8(0x07);  // jns +7
                    emit_u8(0xF7); emit_u8(0xDA);  // neg dx
                    emit_u8(0xF7); emit_u8(0xD8);  // neg ax
                    emit_u8(0x83); emit_u8(0xDA); emit_u8(0x00);  // sbb dx, 0
                    emit_u8(0x85); emit_u8(0xC9);  // test cx, cx
                    emit_u8(0x79); emit_u8(0x07);  // jns +7
                    emit_u8(0xF7); emit_u8(0xD9);  // neg cx
                    emit_u8(0xF7); emit_u8(0xDB);  // neg bx
                    emit_u8(0x83); emit_u8(0xD9); emit_u8(0x00);  // sbb cx, 0
                }
                emit_u8(0x51);                 // push cx ([bp+2] = divisor_hi)
                emit_u8(0x53);                 // push bx ([bp+0] = divisor_lo)
                emit_u8(0x8B); emit_u8(0xEC);  // mov bp, sp
                emit_u8(0x33); emit_u8(0xF6);  // xor si, si
                emit_u8(0x33); emit_u8(0xFF);  // xor di, di
                emit_u8(0xB9); emit_u16(32);   // mov cx, 32
                const std::size_t div_loop = omf_.code.data.size();
                emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
                emit_u8(0xD1); emit_u8(0xD2);  // rcl dx, 1
                emit_u8(0xD1); emit_u8(0xD7);  // rcl di, 1
                emit_u8(0xD1); emit_u8(0xD6);  // rcl si, 1
                emit_u8(0x3B); emit_u8(0x76); emit_u8(0x02);  // cmp si, [bp+2]
                emit_u8(0x72); emit_u8(0x0E);  // jb no_sub (+14)
                emit_u8(0x77); emit_u8(0x05);  // ja do_sub (+5)
                emit_u8(0x3B); emit_u8(0x7E); emit_u8(0x00);  // cmp di, [bp+0]
                emit_u8(0x72); emit_u8(0x07);  // jb no_sub (+7)
                // do_sub:
                emit_u8(0x2B); emit_u8(0x7E); emit_u8(0x00);  // sub di, [bp+0]
                emit_u8(0x1B); emit_u8(0x76); emit_u8(0x02);  // sbb si, [bp+2]
                emit_u8(0x40);                 // inc ax
                // no_sub:
                const std::int8_t rel = static_cast<std::int8_t>(
                    static_cast<std::ptrdiff_t>(div_loop) -
                    static_cast<std::ptrdiff_t>(omf_.code.data.size() + 2));
                emit_u8(0xE2); emit_u8(static_cast<std::uint8_t>(rel));  // loop div_loop
                emit_u8(0x83); emit_u8(0xC4); emit_u8(0x04);  // add sp, 4
                if (op == Tok::percent) {
                    emit_u8(0x8B); emit_u8(0xC7);  // mov ax, di
                    emit_u8(0x8B); emit_u8(0xD6);  // mov dx, si
                }
                if (is_signed) {
                    emit_u8(0x5E);                 // pop si
                    emit_u8(0x85); emit_u8(0xF6);  // test si, si
                    emit_u8(0x79); emit_u8(0x07);  // jns +7
                    emit_u8(0xF7); emit_u8(0xDA);  // neg dx
                    emit_u8(0xF7); emit_u8(0xD8);  // neg ax
                    emit_u8(0x83); emit_u8(0xDA); emit_u8(0x00);  // sbb dx, 0
                }
                emit_u8(0x5D);  // pop bp
                emit_u8(0x5F);  // pop di
                emit_u8(0x5E);  // pop si
                break;
            }
            default:
                break;
        }
    }

    void emit_alu_ax_cx(Tok op, const TypePtr& type, bool cmp_signed = true) {
        const bool is_signed = is_signed_type(type);
        switch (op) {
            case Tok::plus:  emit_u8(0x03); emit_u8(0xC1); break;  // add ax, cx
            case Tok::minus: emit_u8(0x2B); emit_u8(0xC1); break;  // sub ax, cx
            case Tok::amp:   emit_u8(0x23); emit_u8(0xC1); break;  // and ax, cx
            case Tok::pipe:  emit_u8(0x0B); emit_u8(0xC1); break;  // or ax, cx
            case Tok::caret: emit_u8(0x33); emit_u8(0xC1); break;  // xor ax, cx
            case Tok::shl:   emit_u8(0xD3); emit_u8(0xE0); break;  // shl ax, cl
            case Tok::shr:
                emit_u8(0xD3);
                emit_u8(is_signed ? 0xF8 : 0xE8);  // sar/shr ax, cl
                break;
            case Tok::star:
                emit_u8(0xF7);
                emit_u8(is_signed ? 0xE9 : 0xE1);  // imul/mul cx
                break;
            case Tok::slash:
            case Tok::percent:
                if (is_signed) {
                    emit_u8(0x99);  // cwd
                    emit_u8(0xF7); emit_u8(0xF9);  // idiv cx
                } else {
                    emit_u8(0x33); emit_u8(0xD2);  // xor dx, dx
                    emit_u8(0xF7); emit_u8(0xF1);  // div cx
                }
                if (op == Tok::percent) {
                    emit_u8(0x8B); emit_u8(0xC2);  // mov ax, dx
                }
                break;
            case Tok::eq: case Tok::ne: case Tok::lt: case Tok::le: case Tok::gt: case Tok::ge: {
                emit_u8(0x3B); emit_u8(0xC1);  // cmp ax, cx
                emit_cmp_setcc(op, cmp_signed);
                break;
            }
            default:
                break;
        }
    }

    void resolve_local_fixups() {
        for (const PendingCodeFixup& pf : pending_fixups_) {
            auto it = global_locs_.find(pf.sym_name);
            if (it != global_locs_.end()) {
                if (pf.kind == FixupKind::near_call_rel && it->second.seg == 1) {
                    // Self-relative call to function in the same _TEXT segment
                    std::int32_t disp = static_cast<std::int32_t>(it->second.offset) -
                                        static_cast<std::int32_t>(pf.code_offset + 2);
                    write_u16(omf_.code.data, pf.code_offset, static_cast<std::uint16_t>(disp));
                } else {
                    if (pf.kind == FixupKind::near_offset_dgroup ||
                        pf.kind == FixupKind::near_offset_code ||
                        pf.kind == FixupKind::far_ptr) {
                        write_u16(omf_.code.data, pf.code_offset, it->second.offset);
                    }
                    omf_.code.fixups.push_back({pf.code_offset, pf.kind, it->second.seg, 0});
                }
            } else {
                int ext = omf_.intern_ext(pf.sym_name);
                omf_.code.fixups.push_back({pf.code_offset, pf.kind, 0, ext});
            }
        }
    }
};

}  // namespace

std::vector<std::uint8_t> compile_c_to_omf(std::string_view preprocessed_source,
                                           const CompileOptions& options) {
    std::vector<Token> tokens = tokenize(preprocessed_source);
    Parser parser(std::move(tokens), options);
    parser.parse_translation_unit();

    OmfBuilder omf(options);
    Codegen cg(omf, parser, options);
    cg.emit_all();
    return omf.build();
}

}  // namespace dosbase::devtools
