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
        } else if (op == Tok::plus && r->type &&
                   (r->type->kind == TypeKind::t_ptr || r->type->kind == TypeKind::t_array)) {
            TypePtr base = r->type->base;
            e->type = r->type->kind == TypeKind::t_array
                          ? make_ptr(base, default_far_data())
                          : r->type;
        } else if (op == Tok::shl || op == Tok::shr) {
            e->type = common_arith_type(l->type, make_prim(TypeKind::t_short));
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
    using LabelId = std::size_t;
    struct BranchSite {
        std::size_t raw_pos = 0;
        std::uint8_t orig_len = 0;
        std::uint8_t cur_len = 0;
        bool is_cond = false;
        std::uint8_t jcc_opcode = 0;
        LabelId target = 0;
    };

    std::size_t fn_code_start_ = 0;
    std::vector<std::size_t> label_raw_pos_;
    std::vector<BranchSite> branches_;
    LabelId epilogue_label_ = 0;
    std::vector<std::unordered_map<std::string, std::pair<TypePtr, int>>> local_vars_;
    std::vector<LabelId> break_stack_;
    std::vector<LabelId> continue_stack_;
    std::unordered_map<std::string, std::pair<LabelId, bool>> user_labels_;
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

    LabelId new_label() {
        const LabelId id = label_raw_pos_.size();
        label_raw_pos_.push_back(0);
        return id;
    }

    void bind_label(LabelId id) {
        label_raw_pos_[id] = omf_.code.data.size() - fn_code_start_;
    }

    LabelId get_or_create_user_label(const std::string& name) {
        auto it = user_labels_.find(name);
        if (it != user_labels_.end()) return it->second.first;
        const LabelId id = new_label();
        user_labels_.emplace(name, std::make_pair(id, false));
        return id;
    }

    void emit_jmp(LabelId target) {
        const std::size_t rpos = omf_.code.data.size() - fn_code_start_;
        branches_.push_back({rpos, 3, 3, false, 0, target});
        emit_u8(0xE9);
        emit_u16(0);
    }

    void emit_jcc(std::uint8_t jcc_opcode, LabelId target) {
        const std::size_t rpos = omf_.code.data.size() - fn_code_start_;
        branches_.push_back({rpos, 5, 5, true, jcc_opcode, target});
        emit_u8(static_cast<std::uint8_t>(jcc_opcode ^ 0x01));
        emit_u8(0x03);
        emit_u8(0xE9);
        emit_u16(0);
    }

    void relax_and_finalize_function(std::size_t pf_start, std::size_t cf_start) {
        for (const auto& [name, entry] : user_labels_) {
            if (!entry.second) {
                throw FormatError(0, "undefined label: " + name);
            }
        }
        if (branches_.empty()) return;

        const std::size_t raw_len = omf_.code.data.size() - fn_code_start_;
        std::vector<std::uint8_t> raw_bytes(
            omf_.code.data.begin() + static_cast<std::ptrdiff_t>(fn_code_start_),
            omf_.code.data.end());

        std::vector<std::size_t> raw_to_relaxed(raw_len + 1, 0);
        auto build_map = [&]() {
            std::size_t b_idx = 0;
            std::size_t out_off = 0;
            std::size_t r = 0;
            while (r <= raw_len) {
                raw_to_relaxed[r] = out_off;
                if (b_idx < branches_.size() && r == branches_[b_idx].raw_pos) {
                    out_off += branches_[b_idx].cur_len;
                    r += branches_[b_idx].orig_len;
                    ++b_idx;
                } else {
                    if (r == raw_len) break;
                    ++out_off;
                    ++r;
                }
            }
            return out_off;
        };

        bool changed = true;
        while (changed) {
            changed = false;
            build_map();
            for (BranchSite& b : branches_) {
                if (b.cur_len == 0) continue;
                const std::size_t b_start = raw_to_relaxed[b.raw_pos];
                const std::size_t b_end = raw_to_relaxed[b.raw_pos + b.orig_len];
                const std::size_t tgt_raw = label_raw_pos_[b.target];
                const std::size_t tgt_pos = raw_to_relaxed[tgt_raw];

                if (tgt_raw >= b.raw_pos + b.orig_len && tgt_pos == b_end) {
                    b.cur_len = 0;
                    changed = true;
                    continue;
                }
                if (b.cur_len > 2) {
                    const std::size_t ref_end =
                        (tgt_raw <= b.raw_pos) ? (b_start + 2) : b_end;
                    const std::ptrdiff_t disp_short =
                        static_cast<std::ptrdiff_t>(tgt_pos) -
                        static_cast<std::ptrdiff_t>(ref_end);
                    if (disp_short >= -128 && disp_short <= 127) {
                        b.cur_len = 2;
                        changed = true;
                    }
                }
            }
        }

        const std::size_t final_len = build_map();
        std::vector<std::uint8_t> out_code;
        out_code.reserve(final_len);

        std::size_t b_idx = 0;
        std::size_t r = 0;
        while (r < raw_len) {
            if (b_idx < branches_.size() && r == branches_[b_idx].raw_pos) {
                const BranchSite& b = branches_[b_idx];
                const std::size_t b_end = out_code.size() + b.cur_len;
                const std::ptrdiff_t disp =
                    static_cast<std::ptrdiff_t>(raw_to_relaxed[label_raw_pos_[b.target]]) -
                    static_cast<std::ptrdiff_t>(b_end);
                if (b.cur_len == 2) {
                    out_code.push_back(b.is_cond ? b.jcc_opcode : 0xEB);
                    out_code.push_back(static_cast<std::uint8_t>(disp & 0xFF));
                } else if (b.cur_len == 3) {
                    out_code.push_back(0xE9);
                    out_code.push_back(static_cast<std::uint8_t>(disp & 0xFF));
                    out_code.push_back(static_cast<std::uint8_t>((disp >> 8) & 0xFF));
                } else if (b.cur_len == 5) {
                    out_code.push_back(static_cast<std::uint8_t>(b.jcc_opcode ^ 0x01));
                    out_code.push_back(0x03);
                    out_code.push_back(0xE9);
                    out_code.push_back(static_cast<std::uint8_t>(disp & 0xFF));
                    out_code.push_back(static_cast<std::uint8_t>((disp >> 8) & 0xFF));
                }
                r += b.orig_len;
                ++b_idx;
            } else {
                out_code.push_back(raw_bytes[r]);
                ++r;
            }
        }

        for (std::size_t i = pf_start; i < pending_fixups_.size(); ++i) {
            pending_fixups_[i].code_offset =
                fn_code_start_ + raw_to_relaxed[pending_fixups_[i].code_offset - fn_code_start_];
        }
        for (std::size_t i = cf_start; i < omf_.code.fixups.size(); ++i) {
            omf_.code.fixups[i].offset =
                fn_code_start_ + raw_to_relaxed[omf_.code.fixups[i].offset - fn_code_start_];
        }

        omf_.code.data.resize(fn_code_start_);
        omf_.code.data.insert(omf_.code.data.end(), out_code.begin(), out_code.end());
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

    static int exact_log2_u32(std::uint32_t v) {
        if (v == 0 || (v & (v - 1U)) != 0) return -1;
        int k = 0;
        while ((v >> k) > 1U) ++k;
        return k;
    }

    static std::int64_t truncate_const_to_type(std::int64_t val, const TypePtr& t) {
        if (!t) return static_cast<std::int16_t>(val & 0xFFFF);
        switch (t->kind) {
            case TypeKind::t_char:
                return static_cast<std::int8_t>(val & 0xFF);
            case TypeKind::t_uchar:
                return static_cast<std::uint8_t>(val & 0xFF);
            case TypeKind::t_short:
                return static_cast<std::int16_t>(val & 0xFFFF);
            case TypeKind::t_ushort:
                return static_cast<std::uint16_t>(val & 0xFFFF);
            case TypeKind::t_long:
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(val));
            case TypeKind::t_ulong:
                return static_cast<std::int64_t>(static_cast<std::uint32_t>(val));
            case TypeKind::t_ptr:
                if (t->is_far) return static_cast<std::int64_t>(static_cast<std::uint32_t>(val));
                return static_cast<std::uint16_t>(val & 0xFFFF);
            default:
                return val;
        }
    }

    static bool is_foldable_scalar_type(const TypePtr& t) {
        if (!t) return false;
        return t->kind == TypeKind::t_char || t->kind == TypeKind::t_uchar ||
               t->kind == TypeKind::t_short || t->kind == TypeKind::t_ushort ||
               t->kind == TypeKind::t_long ||
               t->kind == TypeKind::t_ulong || t->kind == TypeKind::t_ptr;
    }

    ExprPtr make_folded_int(std::int64_t val, const TypePtr& t) {
        auto lit = std::make_shared<Expr>();
        lit->kind = ExprKind::int_lit;
        lit->type = t;
        lit->int_val = truncate_const_to_type(val, t);
        return lit;
    }

    ExprPtr fold_expr(ExprPtr e) {
        if (!e) return nullptr;
        if (e->lhs) e->lhs = fold_expr(e->lhs);
        if (e->rhs) e->rhs = fold_expr(e->rhs);
        if (e->third) e->third = fold_expr(e->third);
        for (ExprPtr& a : e->args) a = fold_expr(a);

        if (e->kind == ExprKind::cast && e->lhs && e->lhs->kind == ExprKind::int_lit &&
            is_foldable_scalar_type(e->type)) {
            const bool near_to_far =
                e->type && e->type->kind == TypeKind::t_ptr && e->type->is_far &&
                e->lhs->type &&
                (e->lhs->type->kind == TypeKind::t_ptr || e->lhs->type->kind == TypeKind::t_array) &&
                !e->lhs->type->is_far;
            if (!near_to_far) {
                return make_folded_int(e->lhs->int_val, e->type);
            }
        }

        if (e->kind == ExprKind::unary && e->lhs && e->lhs->kind == ExprKind::int_lit &&
            is_foldable_scalar_type(e->type)) {
            const std::int64_t v = e->lhs->int_val;
            if (e->op == Tok::plus) return make_folded_int(v, e->type);
            if (e->op == Tok::minus) return make_folded_int(-static_cast<std::uint64_t>(v), e->type);
            if (e->op == Tok::tilde) return make_folded_int(~static_cast<std::uint64_t>(v), e->type);
            if (e->op == Tok::bang) return make_folded_int(v == 0 ? 1 : 0, e->type);
        }

        if (e->kind == ExprKind::ternary && e->lhs && e->lhs->kind == ExprKind::int_lit &&
            e->rhs && e->third) {
            ExprPtr chosen = (e->lhs->int_val != 0) ? e->rhs : e->third;
            if (chosen->type != e->type && is_foldable_scalar_type(e->type)) {
                auto c = std::make_shared<Expr>();
                c->kind = ExprKind::cast;
                c->type = e->type;
                c->lhs = chosen;
                return fold_expr(c);
            }
            return chosen;
        }

        if (e->kind == ExprKind::binary && e->lhs && e->rhs) {
            const bool lhs_ptr = e->lhs->type && (e->lhs->type->kind == TypeKind::t_ptr ||
                                                  e->lhs->type->kind == TypeKind::t_array);
            const bool rhs_ptr = e->rhs->type && (e->rhs->type->kind == TypeKind::t_ptr ||
                                                  e->rhs->type->kind == TypeKind::t_array);

            if (!lhs_ptr && !rhs_ptr &&
                e->lhs->kind == ExprKind::int_lit && e->rhs->kind == ExprKind::int_lit &&
                is_foldable_scalar_type(e->type)) {
                if (e->op == Tok::land) {
                    return make_folded_int((e->lhs->int_val != 0 && e->rhs->int_val != 0) ? 1 : 0, e->type);
                }
                if (e->op == Tok::lor) {
                    return make_folded_int((e->lhs->int_val != 0 || e->rhs->int_val != 0) ? 1 : 0, e->type);
                }
                const bool op32 = is_32bit_scalar(e->lhs->type) || is_32bit_scalar(e->rhs->type);
                if (e->op == Tok::shl || e->op == Tok::shr) {
                    const std::int64_t cnt = e->rhs->int_val;
                    const int width = op32 ? 32 : 16;
                    if (cnt >= 0 && cnt < width) {
                        if (is_32bit_scalar(e->type)) {
                            if (e->op == Tok::shl) {
                                return make_folded_int(
                                    static_cast<std::uint32_t>(e->lhs->int_val) << cnt, e->type);
                            }
                            if (is_signed_type(e->lhs->type)) {
                                return make_folded_int(
                                    static_cast<std::int32_t>(e->lhs->int_val) >> cnt, e->type);
                            }
                            return make_folded_int(
                                static_cast<std::uint32_t>(e->lhs->int_val) >> cnt, e->type);
                        } else {
                            if (e->op == Tok::shl) {
                                return make_folded_int(
                                    static_cast<std::uint16_t>(e->lhs->int_val) << cnt, e->type);
                            }
                            if (is_signed_type(e->lhs->type)) {
                                return make_folded_int(
                                    static_cast<std::int16_t>(e->lhs->int_val) >> cnt, e->type);
                            }
                            return make_folded_int(
                                static_cast<std::uint16_t>(e->lhs->int_val) >> cnt, e->type);
                        }
                    }
                } else {
                    const bool is_signed = is_signed_type(e->lhs->type) && is_signed_type(e->rhs->type);
                    if (op32) {
                        const std::uint32_t ul = static_cast<std::uint32_t>(e->lhs->int_val);
                        const std::uint32_t ur = static_cast<std::uint32_t>(e->rhs->int_val);
                        const std::int32_t sl = static_cast<std::int32_t>(ul);
                        const std::int32_t sr = static_cast<std::int32_t>(ur);
                        if ((e->op == Tok::slash || e->op == Tok::percent) &&
                            (ur == 0 || (is_signed && sl == (-2147483647 - 1) && sr == -1))) {
                            return e;
                        }
                        std::int64_t r = 0;
                        switch (e->op) {
                            case Tok::plus: r = ul + ur; break;
                            case Tok::minus: r = ul - ur; break;
                            case Tok::star: r = ul * ur; break;
                            case Tok::slash: r = is_signed ? static_cast<std::int64_t>(sl / sr) : static_cast<std::int64_t>(ul / ur); break;
                            case Tok::percent: r = is_signed ? static_cast<std::int64_t>(sl % sr) : static_cast<std::int64_t>(ul % ur); break;
                            case Tok::amp: r = ul & ur; break;
                            case Tok::pipe: r = ul | ur; break;
                            case Tok::caret: r = ul ^ ur; break;
                            case Tok::eq: r = (ul == ur) ? 1 : 0; break;
                            case Tok::ne: r = (ul != ur) ? 1 : 0; break;
                            case Tok::lt: r = (is_signed ? (sl < sr) : (ul < ur)) ? 1 : 0; break;
                            case Tok::le: r = (is_signed ? (sl <= sr) : (ul <= ur)) ? 1 : 0; break;
                            case Tok::gt: r = (is_signed ? (sl > sr) : (ul > ur)) ? 1 : 0; break;
                            case Tok::ge: r = (is_signed ? (sl >= sr) : (ul >= ur)) ? 1 : 0; break;
                            default: return e;
                        }
                        return make_folded_int(r, e->type);
                    } else {
                        const std::uint16_t ul = static_cast<std::uint16_t>(e->lhs->int_val);
                        const std::uint16_t ur = static_cast<std::uint16_t>(e->rhs->int_val);
                        const std::int16_t sl = static_cast<std::int16_t>(ul);
                        const std::int16_t sr = static_cast<std::int16_t>(ur);
                        if ((e->op == Tok::slash || e->op == Tok::percent) &&
                            (ur == 0 || (is_signed && sl == -32768 && sr == -1))) {
                            return e;
                        }
                        std::int64_t r = 0;
                        switch (e->op) {
                            case Tok::plus: r = static_cast<std::uint16_t>(ul + ur); break;
                            case Tok::minus: r = static_cast<std::uint16_t>(ul - ur); break;
                            case Tok::star: r = static_cast<std::uint16_t>(ul * ur); break;
                            case Tok::slash: r = is_signed ? (sl / sr) : (ul / ur); break;
                            case Tok::percent: r = is_signed ? (sl % sr) : (ul % ur); break;
                            case Tok::amp: r = static_cast<std::uint16_t>(ul & ur); break;
                            case Tok::pipe: r = static_cast<std::uint16_t>(ul | ur); break;
                            case Tok::caret: r = static_cast<std::uint16_t>(ul ^ ur); break;
                            case Tok::eq: r = (ul == ur) ? 1 : 0; break;
                            case Tok::ne: r = (ul != ur) ? 1 : 0; break;
                            case Tok::lt: r = (is_signed ? (sl < sr) : (ul < ur)) ? 1 : 0; break;
                            case Tok::le: r = (is_signed ? (sl <= sr) : (ul <= ur)) ? 1 : 0; break;
                            case Tok::gt: r = (is_signed ? (sl > sr) : (ul > ur)) ? 1 : 0; break;
                            case Tok::ge: r = (is_signed ? (sl >= sr) : (ul >= ur)) ? 1 : 0; break;
                            default: return e;
                        }
                        return make_folded_int(r, e->type);
                    }
                }
            }

            // Normalize constant literal to RHS for commutative / relational ops
            if (e->lhs->kind == ExprKind::int_lit && e->rhs->kind != ExprKind::int_lit) {
                if (e->op == Tok::plus || e->op == Tok::star || e->op == Tok::amp ||
                    e->op == Tok::pipe || e->op == Tok::caret || e->op == Tok::eq ||
                    e->op == Tok::ne) {
                    std::swap(e->lhs, e->rhs);
                } else if (e->op == Tok::lt || e->op == Tok::le ||
                           e->op == Tok::gt || e->op == Tok::ge) {
                    std::swap(e->lhs, e->rhs);
                    if (e->op == Tok::lt) e->op = Tok::gt;
                    else if (e->op == Tok::le) e->op = Tok::ge;
                    else if (e->op == Tok::gt) e->op = Tok::lt;
                    else if (e->op == Tok::ge) e->op = Tok::le;
                }
            }
        }
        return e;
    }

    void fold_stmt(const StmtPtr& s) {
        if (!s) return;
        if (s->expr) s->expr = fold_expr(s->expr);
        if (s->cond) s->cond = fold_expr(s->cond);
        if (s->post) s->post = fold_expr(s->post);
        for (LocalVar& lv : s->locals) {
            if (lv.init) lv.init = fold_expr(lv.init);
        }
        for (StmtPtr& sub : s->stmts) fold_stmt(sub);
        if (s->body) fold_stmt(s->body);
        if (s->else_body) fold_stmt(s->else_body);
    }

    void emit_shl16_ax_const(unsigned k) {
        if (k == 0) return;
        if (k >= 16) {
            emit_u8(0x33); emit_u8(0xC0);  // xor ax, ax
            return;
        }
        if (k <= 3) {
            for (unsigned i = 0; i < k; ++i) { emit_u8(0xD1); emit_u8(0xE0); }  // shl ax, 1
            return;
        }
        if (k == 8) {
            emit_u8(0x8A); emit_u8(0xE0);  // mov ah, al
            emit_u8(0x32); emit_u8(0xC0);  // xor al, al
            return;
        }
        emit_u8(0xB1); emit_u8(static_cast<std::uint8_t>(k));  // mov cl, k
        emit_u8(0xD3); emit_u8(0xE0);                          // shl ax, cl
    }

    void emit_shr16_ax_const(unsigned k, bool is_signed) {
        if (k == 0) return;
        if (k >= 16) {
            if (is_signed) {
                emit_u8(0x99);                 // cwd
                emit_u8(0x8B); emit_u8(0xC2);  // mov ax, dx
            } else {
                emit_u8(0x33); emit_u8(0xC0);  // xor ax, ax
            }
            return;
        }
        if (k <= 3) {
            for (unsigned i = 0; i < k; ++i) {
                emit_u8(0xD1);
                emit_u8(is_signed ? 0xF8 : 0xE8);  // sar/shr ax, 1
            }
            return;
        }
        if (k == 8) {
            emit_u8(0x8A); emit_u8(0xC4);  // mov al, ah
            if (is_signed) {
                emit_u8(0x98);             // cbw
            } else {
                emit_u8(0x32); emit_u8(0xE4);  // xor ah, ah
            }
            return;
        }
        emit_u8(0xB1); emit_u8(static_cast<std::uint8_t>(k));  // mov cl, k
        emit_u8(0xD3); emit_u8(is_signed ? 0xF8 : 0xE8);      // sar/shr ax, cl
    }

    void emit_mul16_ax_const(std::uint16_t imm, bool is_signed) {
        if (imm == 0) { emit_u8(0x33); emit_u8(0xC0); return; }  // xor ax, ax
        if (imm == 1) return;
        if (imm == 0xFFFF) { emit_u8(0xF7); emit_u8(0xD8); return; }  // neg ax
        const int lg = exact_log2_u32(imm);
        if (lg >= 1) {
            emit_shl16_ax_const(static_cast<unsigned>(lg));
            return;
        }
        if (imm == 3) {
            emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x03); emit_u8(0xC1);  // add ax, cx
            return;
        }
        if (imm == 5) {
            emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x03); emit_u8(0xC1);  // add ax, cx
            return;
        }
        if (imm == 6) {
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x03); emit_u8(0xC1);  // add ax, cx
            return;
        }
        if (imm == 9) {
            emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x03); emit_u8(0xC1);  // add ax, cx
            return;
        }
        if (imm == 10) {
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x03); emit_u8(0xC1);  // add ax, cx
            return;
        }
        if (imm == 12) {
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
            emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
            emit_u8(0x03); emit_u8(0xC1);  // add ax, cx
            return;
        }
        emit_u8(0xB9); emit_u16(imm);                  // mov cx, imm
        emit_u8(0xF7); emit_u8(is_signed ? 0xE9 : 0xE1);  // imul/mul cx
    }

    // Scale CX by elem_sz while preserving AX and DX
    void emit_scale16_cx(std::uint16_t elem_sz) {
        if (elem_sz <= 1) return;
        const int lg = exact_log2_u32(elem_sz);
        if (lg >= 1) {
            if (lg <= 4) {
                for (int i = 0; i < lg; ++i) { emit_u8(0xD1); emit_u8(0xE1); }  // shl cx, 1
            } else if (lg == 8) {
                emit_u8(0x8A); emit_u8(0xE9);  // mov ch, cl
                emit_u8(0x32); emit_u8(0xC9);  // xor cl, cl
            } else {
                emit_u8(0x8B); emit_u8(0xD9);  // mov bx, cx
                emit_u8(0xB1); emit_u8(static_cast<std::uint8_t>(lg));  // mov cl, lg
                emit_u8(0xD3); emit_u8(0xE3);  // shl bx, cl
                emit_u8(0x8B); emit_u8(0xCB);  // mov cx, bx
            }
            return;
        }
        if (elem_sz == 3) {
            emit_u8(0x8B); emit_u8(0xD9);  // mov bx, cx
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0x03); emit_u8(0xCB);  // add cx, bx
            return;
        }
        if (elem_sz == 5) {
            emit_u8(0x8B); emit_u8(0xD9);  // mov bx, cx
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0x03); emit_u8(0xCB);  // add cx, bx
            return;
        }
        if (elem_sz == 6) {
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0x8B); emit_u8(0xD9);  // mov bx, cx
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0x03); emit_u8(0xCB);  // add cx, bx
            return;
        }
        if (elem_sz == 10) {
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0x8B); emit_u8(0xD9);  // mov bx, cx
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0x03); emit_u8(0xCB);  // add cx, bx
            return;
        }
        if (elem_sz == 12) {
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0x8B); emit_u8(0xD9);  // mov bx, cx
            emit_u8(0xD1); emit_u8(0xE1);  // shl cx, 1
            emit_u8(0x03); emit_u8(0xCB);  // add cx, bx
            return;
        }
        emit_u8(0x50);                 // push ax
        emit_u8(0x52);                 // push dx
        emit_u8(0xB8); emit_u16(elem_sz);
        emit_u8(0xF7); emit_u8(0xE1);  // mul cx
        emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
        emit_u8(0x5A);                 // pop dx
        emit_u8(0x58);                 // pop ax
    }

    void emit_shl32_dxax_const(unsigned k) {
        if (k == 0) return;
        if (k >= 32) {
            emit_u8(0x33); emit_u8(0xC0);  // xor ax, ax
            emit_u8(0x33); emit_u8(0xD2);  // xor dx, dx
            return;
        }
        if (k >= 16) {
            emit_u8(0x8B); emit_u8(0xD0);  // mov dx, ax
            emit_u8(0x33); emit_u8(0xC0);  // xor ax, ax
            const unsigned rem = k - 16;
            if (rem == 0) return;
            if (rem <= 3) {
                for (unsigned i = 0; i < rem; ++i) { emit_u8(0xD1); emit_u8(0xE2); }  // shl dx, 1
            } else if (rem == 8) {
                emit_u8(0x8A); emit_u8(0xF2);  // mov dh, dl
                emit_u8(0x32); emit_u8(0xD2);  // xor dl, dl
            } else {
                emit_u8(0xB1); emit_u8(static_cast<std::uint8_t>(rem));
                emit_u8(0xD3); emit_u8(0xE2);  // shl dx, cl
            }
            return;
        }
        if (k == 8) {
            emit_u8(0x8A); emit_u8(0xF2);  // mov dh, dl
            emit_u8(0x8A); emit_u8(0xD4);  // mov dl, ah
            emit_u8(0x8A); emit_u8(0xE0);  // mov ah, al
            emit_u8(0x32); emit_u8(0xC0);  // xor al, al
            return;
        }
        if (k <= 3) {
            for (unsigned i = 0; i < k; ++i) {
                emit_u8(0xD1); emit_u8(0xE0);  // shl ax, 1
                emit_u8(0xD1); emit_u8(0xD2);  // rcl dx, 1
            }
            return;
        }
        emit_u8(0xB9); emit_u16(static_cast<std::uint16_t>(k));  // mov cx, k
        emit_u8(0xD1); emit_u8(0xE0);                            // shl ax, 1
        emit_u8(0xD1); emit_u8(0xD2);                            // rcl dx, 1
        emit_u8(0xE2); emit_u8(0xFA);                            // loop -6
    }

    void emit_shr32_dxax_const(unsigned k, bool is_signed) {
        if (k == 0) return;
        if (k >= 32) {
            if (is_signed) {
                emit_u8(0x8B); emit_u8(0xC2);  // mov ax, dx
                emit_u8(0x99);                 // cwd
                emit_u8(0x8B); emit_u8(0xC2);  // mov ax, dx
            } else {
                emit_u8(0x33); emit_u8(0xC0);
                emit_u8(0x33); emit_u8(0xD2);
            }
            return;
        }
        if (k >= 16) {
            emit_u8(0x8B); emit_u8(0xC2);  // mov ax, dx
            if (is_signed) {
                emit_u8(0x99);  // cwd
            } else {
                emit_u8(0x33); emit_u8(0xD2);  // xor dx, dx
            }
            emit_shr16_ax_const(k - 16, is_signed);
            return;
        }
        if (k == 8 && !is_signed) {
            emit_u8(0x8A); emit_u8(0xC4);  // mov al, ah
            emit_u8(0x8A); emit_u8(0xE2);  // mov ah, dl
            emit_u8(0x8A); emit_u8(0xD6);  // mov dl, dh
            emit_u8(0x32); emit_u8(0xF6);  // xor dh, dh
            return;
        }
        if (k <= 3) {
            for (unsigned i = 0; i < k; ++i) {
                emit_u8(0xD1); emit_u8(is_signed ? 0xFA : 0xEA);  // sar/shr dx, 1
                emit_u8(0xD1); emit_u8(0xD8);                     // rcr ax, 1
            }
            return;
        }
        emit_u8(0xB9); emit_u16(static_cast<std::uint16_t>(k));   // mov cx, k
        emit_u8(0xD1); emit_u8(is_signed ? 0xFA : 0xEA);          // sar/shr dx, 1
        emit_u8(0xD1); emit_u8(0xD8);                             // rcr ax, 1
        emit_u8(0xE2); emit_u8(0xFA);                             // loop -6
    }

    void emit_function(const FuncDef& fn) {
        fold_stmt(fn.body);
        cur_func_ = &fn;
        fn_code_start_ = omf_.code.data.size();
        const std::size_t pf_start = pending_fixups_.size();
        const std::size_t cf_start = omf_.code.fixups.size();
        label_raw_pos_.clear();
        branches_.clear();
        epilogue_label_ = new_label();
        break_stack_.clear();
        continue_stack_.clear();
        user_labels_.clear();
        local_vars_.clear();
        local_vars_.emplace_back();

        std::uint16_t fn_off = static_cast<std::uint16_t>(fn_code_start_);
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
        bind_label(epilogue_label_);

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

        relax_and_finalize_function(pf_start, cf_start);

        local_vars_.clear();
        cur_func_ = nullptr;
    }

    void emit_branch(const ExprPtr& cond, LabelId target, bool jump_on_true) {
        if (!cond) return;
        if (cond->kind == ExprKind::int_lit) {
            const bool truthy = is_32bit_scalar(cond->type)
                                    ? (static_cast<std::uint32_t>(cond->int_val) != 0U)
                                    : (static_cast<std::uint16_t>(cond->int_val) != 0U);
            if (truthy == jump_on_true) emit_jmp(target);
            return;
        }
        if (cond->kind == ExprKind::unary && cond->op == Tok::bang) {
            emit_branch(cond->lhs, target, !jump_on_true);
            return;
        }
        if (cond->kind == ExprKind::comma) {
            emit_expr(cond->lhs, false);
            emit_branch(cond->rhs, target, jump_on_true);
            return;
        }
        if (cond->kind == ExprKind::binary && cond->op == Tok::land) {
            if (!jump_on_true) {
                emit_branch(cond->lhs, target, false);
                emit_branch(cond->rhs, target, false);
            } else {
                const LabelId skip = new_label();
                emit_branch(cond->lhs, skip, false);
                emit_branch(cond->rhs, target, true);
                bind_label(skip);
            }
            return;
        }
        if (cond->kind == ExprKind::binary && cond->op == Tok::lor) {
            if (jump_on_true) {
                emit_branch(cond->lhs, target, true);
                emit_branch(cond->rhs, target, true);
            } else {
                const LabelId skip = new_label();
                emit_branch(cond->lhs, skip, true);
                emit_branch(cond->rhs, target, false);
                bind_label(skip);
            }
            return;
        }
        if (cond->kind == ExprKind::binary &&
            (cond->op == Tok::eq || cond->op == Tok::ne || cond->op == Tok::lt ||
             cond->op == Tok::le || cond->op == Tok::gt || cond->op == Tok::ge)) {
            const bool lhs_ptr = cond->lhs->type && (cond->lhs->type->kind == TypeKind::t_ptr ||
                                                     cond->lhs->type->kind == TypeKind::t_array);
            const bool rhs_ptr = cond->rhs->type && (cond->rhs->type->kind == TypeKind::t_ptr ||
                                                     cond->rhs->type->kind == TypeKind::t_array);
            if (!is_32bit_scalar(cond->lhs->type) && !is_32bit_scalar(cond->rhs->type)) {
                if (cond->rhs->kind == ExprKind::int_lit) {
                    const std::uint16_t imm = static_cast<std::uint16_t>(cond->rhs->int_val);
                    emit_expr(cond->lhs);
                    if (imm == 0) {
                        emit_u8(0x85); emit_u8(0xC0);  // test ax, ax
                    } else {
                        emit_u8(0x3D); emit_u16(imm);   // cmp ax, imm16
                    }
                } else if (DirectMem dm; try_resolve_word_mem(cond->rhs, dm)) {
                    emit_expr(cond->lhs);
                    emit_op_ax_direct_mem(0x3B, dm);  // cmp ax, [bp+d] / [sym+d]
                } else {
                    emit_expr(cond->lhs);
                    emit_u8(0x50);  // push ax
                    emit_expr(cond->rhs);
                    emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax
                    emit_u8(0x58);                 // pop ax
                    emit_u8(0x3B); emit_u8(0xC1);  // cmp ax, cx
                }
                const bool is_signed =
                    is_signed_type(cond->lhs->type) && is_signed_type(cond->rhs->type);
                std::uint8_t jcc = 0x74;
                if (cond->op == Tok::eq) jcc = 0x74;
                else if (cond->op == Tok::ne) jcc = 0x75;
                else if (cond->op == Tok::lt) jcc = is_signed ? 0x7C : 0x72;
                else if (cond->op == Tok::le) jcc = is_signed ? 0x7E : 0x76;
                else if (cond->op == Tok::gt) jcc = is_signed ? 0x7F : 0x77;
                else if (cond->op == Tok::ge) jcc = is_signed ? 0x7D : 0x73;
                emit_jcc(jump_on_true ? jcc : static_cast<std::uint8_t>(jcc ^ 0x01), target);
                return;
            }
            if (!lhs_ptr && !rhs_ptr &&
                (is_32bit_scalar(cond->lhs->type) || is_32bit_scalar(cond->rhs->type))) {
                const bool is_signed =
                    is_signed_type(cond->lhs->type) && is_signed_type(cond->rhs->type);
                Tok eff_op = cond->op;
                if (!jump_on_true) {
                    if (eff_op == Tok::eq) eff_op = Tok::ne;
                    else if (eff_op == Tok::ne) eff_op = Tok::eq;
                    else if (eff_op == Tok::lt) eff_op = Tok::ge;
                    else if (eff_op == Tok::ge) eff_op = Tok::lt;
                    else if (eff_op == Tok::le) eff_op = Tok::gt;
                    else if (eff_op == Tok::gt) eff_op = Tok::le;
                }
                if ((eff_op == Tok::eq || eff_op == Tok::ne) &&
                    cond->rhs->kind == ExprKind::int_lit &&
                    static_cast<std::uint32_t>(cond->rhs->int_val) == 0U) {
                    emit_expr(cond->lhs);
                    emit_widen_dxax(cond->lhs->type);
                    emit_u8(0x0B); emit_u8(0xC2);  // or ax, dx
                    emit_jcc(eff_op == Tok::ne ? 0x75 : 0x74, target);
                    return;
                }
                emit_expr(cond->lhs);
                emit_widen_dxax(cond->lhs->type);
                emit_u8(0x52);  // push dx
                emit_u8(0x50);  // push ax
                emit_expr(cond->rhs);
                emit_widen_dxax(cond->rhs->type);
                emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax (rhs lo)
                emit_u8(0x8B); emit_u8(0xCA);  // mov cx, dx (rhs hi)
                emit_u8(0x58);                 // pop ax (lhs lo)
                emit_u8(0x5A);                 // pop dx (lhs hi)
                if (eff_op == Tok::eq) {
                    const LabelId skip = new_label();
                    emit_u8(0x3B); emit_u8(0xD1);  // cmp dx, cx
                    emit_jcc(0x75, skip);
                    emit_u8(0x3B); emit_u8(0xC3);  // cmp ax, bx
                    emit_jcc(0x74, target);
                    bind_label(skip);
                } else if (eff_op == Tok::ne) {
                    emit_u8(0x3B); emit_u8(0xD1);  // cmp dx, cx
                    emit_jcc(0x75, target);
                    emit_u8(0x3B); emit_u8(0xC3);  // cmp ax, bx
                    emit_jcc(0x75, target);
                } else {
                    const LabelId skip = new_label();
                    const std::uint8_t hi_jcc =
                        (eff_op == Tok::lt || eff_op == Tok::le)
                            ? (is_signed ? 0x7C : 0x72)
                            : (is_signed ? 0x7F : 0x77);
                    const std::uint8_t lo_jcc = eff_op == Tok::lt   ? 0x72
                                                : eff_op == Tok::le ? 0x76
                                                : eff_op == Tok::gt ? 0x77
                                                                    : 0x73;
                    emit_u8(0x3B); emit_u8(0xD1);  // cmp dx, cx
                    emit_jcc(hi_jcc, target);
                    emit_jcc(0x75, skip);
                    emit_u8(0x3B); emit_u8(0xC3);  // cmp ax, bx
                    emit_jcc(lo_jcc, target);
                    bind_label(skip);
                }
                return;
            }
        }

        emit_expr(cond);
        if (is_32bit_scalar(cond->type)) {
            emit_u8(0x0B); emit_u8(0xC2);  // or ax, dx
        } else {
            emit_u8(0x85); emit_u8(0xC0);  // test ax, ax
        }
        emit_jcc(jump_on_true ? 0x75 : 0x74, target);
    }

    void emit_stmt(const StmtPtr& s) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::empty:
                break;
            case StmtKind::expr:
                emit_expr(s->expr, false);
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
                const LabelId l_false = new_label();
                emit_branch(s->cond, l_false, false);
                emit_stmt(s->body);
                if (s->else_body) {
                    const LabelId l_end = new_label();
                    emit_jmp(l_end);
                    bind_label(l_false);
                    emit_stmt(s->else_body);
                    bind_label(l_end);
                } else {
                    bind_label(l_false);
                }
                break;
            }
            case StmtKind::while_stmt: {
                const LabelId l_top = new_label();
                const LabelId l_after = new_label();
                bind_label(l_top);
                break_stack_.push_back(l_after);
                continue_stack_.push_back(l_top);
                emit_branch(s->cond, l_after, false);
                emit_stmt(s->body);
                emit_jmp(l_top);
                bind_label(l_after);
                break_stack_.pop_back();
                continue_stack_.pop_back();
                break;
            }
            case StmtKind::do_while_stmt: {
                const LabelId l_top = new_label();
                const LabelId l_cond = new_label();
                const LabelId l_after = new_label();
                bind_label(l_top);
                break_stack_.push_back(l_after);
                continue_stack_.push_back(l_cond);
                emit_stmt(s->body);
                bind_label(l_cond);
                emit_branch(s->cond, l_top, true);
                bind_label(l_after);
                break_stack_.pop_back();
                continue_stack_.pop_back();
                break;
            }
            case StmtKind::for_stmt: {
                const LabelId l_cond = new_label();
                const LabelId l_post = new_label();
                const LabelId l_after = new_label();
                if (s->expr) emit_expr(s->expr, false);
                bind_label(l_cond);
                if (s->cond) {
                    emit_branch(s->cond, l_after, false);
                }
                break_stack_.push_back(l_after);
                continue_stack_.push_back(l_post);
                emit_stmt(s->body);
                bind_label(l_post);
                if (s->post) emit_expr(s->post, false);
                emit_jmp(l_cond);
                bind_label(l_after);
                break_stack_.pop_back();
                continue_stack_.pop_back();
                break;
            }
            case StmtKind::switch_stmt: {
                emit_expr(s->cond);
                emit_u8(0x50);  // push ax
                const LabelId l_dispatch = new_label();
                const LabelId l_after = new_label();
                emit_jmp(l_dispatch);
                break_stack_.push_back(l_after);
                std::vector<std::pair<std::int64_t, LabelId>> cases;
                std::optional<LabelId> def_lbl;
                emit_switch_body(s->body, cases, def_lbl);
                emit_jmp(l_after);

                bind_label(l_dispatch);
                emit_u8(0x58);  // pop ax
                for (const auto& [val, target_lbl] : cases) {
                    const std::uint16_t uval = static_cast<std::uint16_t>(val);
                    if (uval == 0) {
                        emit_u8(0x85); emit_u8(0xC0);  // test ax, ax
                    } else {
                        emit_u8(0x3D); emit_u16(uval);  // cmp ax, imm16
                    }
                    emit_jcc(0x74, target_lbl);
                }
                if (def_lbl.has_value()) {
                    emit_jmp(*def_lbl);
                }
                bind_label(l_after);
                break_stack_.pop_back();
                break;
            }
            case StmtKind::case_stmt:
            case StmtKind::default_stmt:
                emit_stmt(s->body);
                break;
            case StmtKind::break_stmt:
                if (!break_stack_.empty()) emit_jmp(break_stack_.back());
                break;
            case StmtKind::continue_stmt:
                if (!continue_stack_.empty()) emit_jmp(continue_stack_.back());
                break;
            case StmtKind::return_stmt:
                if (s->expr) emit_expr(s->expr);
                emit_jmp(epilogue_label_);
                break;
            case StmtKind::goto_stmt:
                emit_jmp(get_or_create_user_label(s->label));
                break;
            case StmtKind::label_stmt: {
                const LabelId lbl = get_or_create_user_label(s->label);
                user_labels_[s->label].second = true;
                bind_label(lbl);
                emit_stmt(s->body);
                break;
            }
        }
    }

    void emit_switch_body(const StmtPtr& s,
                          std::vector<std::pair<std::int64_t, LabelId>>& cases,
                          std::optional<LabelId>& def_lbl) {
        if (!s) return;
        if (s->kind == StmtKind::block) {
            local_vars_.emplace_back();
            for (const LocalVar& lv : s->locals) {
                local_vars_.back()[lv.name] = {lv.type, lv.bp_offset};
                if (lv.init) {
                    emit_expr(lv.init);
                    if (is_32bit_scalar(lv.type)) emit_widen_dxax(lv.init->type);
                    store_local(lv.type, lv.bp_offset);
                }
            }
            for (const StmtPtr& sub : s->stmts) emit_switch_body(sub, cases, def_lbl);
            local_vars_.pop_back();
        } else if (s->kind == StmtKind::case_stmt) {
            const LabelId lbl = new_label();
            bind_label(lbl);
            cases.emplace_back(s->case_val, lbl);
            emit_switch_body(s->body, cases, def_lbl);
        } else if (s->kind == StmtKind::default_stmt) {
            const LabelId lbl = new_label();
            bind_label(lbl);
            def_lbl = lbl;
            emit_switch_body(s->body, cases, def_lbl);
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

    struct DirectMem {
        enum Kind { bp_rel, dgroup_sym } kind = bp_rel;
        int disp = 0;
        std::string sym;
    };

    static std::size_t lookup_field_offset(const TypePtr& st, const std::string& name) {
        if (st) {
            for (const StructField& f : st->fields) {
                if (f.name == name) return f.offset;
            }
        }
        return 0;
    }

    bool try_resolve_direct_mem(const ExprPtr& e, DirectMem& out) const {
        if (!e) return false;
        if (e->kind == ExprKind::var) {
            if (e->type && e->type->kind == TypeKind::t_func) return false;
            if (auto loc = find_local(e->name)) {
                out = {DirectMem::bp_rel, loc->second, ""};
                return true;
            }
            auto gf = parser_.global_syms.find(e->name);
            std::string sym = !e->resolved_asm_name.empty()
                                  ? e->resolved_asm_name
                                  : (gf != parser_.global_syms.end() ? gf->second.asm_name
                                                                     : ("_" + e->name));
            out = {DirectMem::dgroup_sym, 0, std::move(sym)};
            return true;
        }
        if (e->kind == ExprKind::member && !e->is_arrow) {
            if (!try_resolve_direct_mem(e->lhs, out)) return false;
            const long long foff = static_cast<long long>(lookup_field_offset(e->lhs->type, e->name));
            const long long nd = static_cast<long long>(out.disp) + foff;
            if (out.kind == DirectMem::dgroup_sym) {
                if (nd < 0 || nd > 65500) return false;
            } else {
                if (nd < -32700 || nd > 32700) return false;
            }
            out.disp = static_cast<int>(nd);
            return true;
        }
        if (e->kind == ExprKind::unary && e->op == Tok::star && e->lhs) {
            if (e->lhs->type && e->lhs->type->kind == TypeKind::t_array) {
                return try_resolve_direct_mem(e->lhs, out);
            }
            if (e->lhs->kind == ExprKind::binary &&
                (e->lhs->op == Tok::plus || e->lhs->op == Tok::minus) &&
                e->lhs->lhs && e->lhs->lhs->type &&
                e->lhs->lhs->type->kind == TypeKind::t_array &&
                e->lhs->rhs && e->lhs->rhs->kind == ExprKind::int_lit) {
                if (!try_resolve_direct_mem(e->lhs->lhs, out)) return false;
                const std::size_t elem_sz =
                    std::max<std::size_t>(1, type_size(e->lhs->lhs->type->base));
                long long delta = static_cast<long long>(static_cast<std::int16_t>(e->lhs->rhs->int_val)) *
                                  static_cast<long long>(elem_sz);
                if (e->lhs->op == Tok::minus) delta = -delta;
                const long long nd = static_cast<long long>(out.disp) + delta;
                if (out.kind == DirectMem::dgroup_sym) {
                    if (nd < 0 || nd > 65500) return false;
                } else {
                    if (nd < -32700 || nd > 32700) return false;
                }
                out.disp = static_cast<int>(nd);
                return true;
            }
        }
        return false;
    }

    bool try_resolve_word_mem(const ExprPtr& e, DirectMem& out) const {
        if (!e || !e->type) return false;
        if (is_byte_scalar(e->type) || is_32bit_scalar(e->type)) return false;
        if (e->type->kind == TypeKind::t_array || e->type->kind == TypeKind::t_func ||
            e->type->kind == TypeKind::t_struct || e->type->kind == TypeKind::t_void) {
            return false;
        }
        if (type_size(e->type) != 2) return false;
        return try_resolve_direct_mem(e, out);
    }

    void emit_dgroup_disp(const std::string& sym, int disp) {
        pending_fixups_.push_back({omf_.code.data.size(), FixupKind::near_offset_dgroup, sym});
        emit_u16(static_cast<std::uint16_t>(disp));
    }

    void emit_op_ax_direct_mem(std::uint8_t opc, const DirectMem& dm) {
        emit_u8(opc);
        if (dm.kind == DirectMem::bp_rel) {
            emit_bp_rm(0, dm.disp);
        } else {
            emit_u8(0x06);
            emit_dgroup_disp(dm.sym, dm.disp);
        }
    }

    void load_direct_mem(const TypePtr& t, const DirectMem& dm) {
        if (t && t->kind == TypeKind::t_array) {
            if (dm.kind == DirectMem::bp_rel) {
                emit_u8(0x8D); emit_bp_rm(0, dm.disp);  // lea ax, [bp+d]
            } else {
                emit_u8(0xB8);  // mov ax, offset sym+d
                emit_dgroup_disp(dm.sym, dm.disp);
            }
            if (t->is_far) { emit_u8(0x8C); emit_u8(0xDA); }
            return;
        }
        if (is_byte_scalar(t)) {
            if (dm.kind == DirectMem::bp_rel) {
                emit_u8(0x8A); emit_bp_rm(0, dm.disp);  // mov al, [bp+d]
            } else {
                emit_u8(0xA0);  // mov al, [sym+d]
                emit_dgroup_disp(dm.sym, dm.disp);
            }
            if (is_signed_type(t)) emit_u8(0x98);
            else { emit_u8(0x32); emit_u8(0xE4); }
        } else if (is_32bit_scalar(t)) {
            if (dm.kind == DirectMem::bp_rel) {
                emit_u8(0x8B); emit_bp_rm(0, dm.disp);      // mov ax, [bp+d]
                emit_u8(0x8B); emit_bp_rm(2, dm.disp + 2);  // mov dx, [bp+d+2]
            } else {
                emit_u8(0xA1);  // mov ax, [sym+d]
                emit_dgroup_disp(dm.sym, dm.disp);
                emit_u8(0x8B); emit_u8(0x16);  // mov dx, [sym+d+2]
                emit_dgroup_disp(dm.sym, dm.disp + 2);
            }
        } else {
            if (dm.kind == DirectMem::bp_rel) {
                emit_u8(0x8B); emit_bp_rm(0, dm.disp);  // mov ax, [bp+d]
            } else {
                emit_u8(0xA1);  // mov ax, [sym+d]
                emit_dgroup_disp(dm.sym, dm.disp);
            }
        }
    }

    void store_direct_mem(const TypePtr& t, const DirectMem& dm) {
        if (is_byte_scalar(t)) {
            if (dm.kind == DirectMem::bp_rel) {
                emit_u8(0x88); emit_bp_rm(0, dm.disp);  // mov [bp+d], al
            } else {
                emit_u8(0xA2);  // mov [sym+d], al
                emit_dgroup_disp(dm.sym, dm.disp);
            }
        } else if (is_32bit_scalar(t)) {
            if (dm.kind == DirectMem::bp_rel) {
                emit_u8(0x89); emit_bp_rm(0, dm.disp);      // mov [bp+d], ax
                emit_u8(0x89); emit_bp_rm(2, dm.disp + 2);  // mov [bp+d+2], dx
            } else {
                emit_u8(0xA3);  // mov [sym+d], ax
                emit_dgroup_disp(dm.sym, dm.disp);
                emit_u8(0x89); emit_u8(0x16);  // mov [sym+d+2], dx
                emit_dgroup_disp(dm.sym, dm.disp + 2);
            }
        } else {
            if (dm.kind == DirectMem::bp_rel) {
                emit_u8(0x89); emit_bp_rm(0, dm.disp);  // mov [bp+d], ax
            } else {
                emit_u8(0xA3);  // mov [sym+d], ax
                emit_dgroup_disp(dm.sym, dm.disp);
            }
        }
    }

    void emit_bx_rm(std::uint8_t reg_or_op, int disp) {
        if (disp == 0) {
            emit_u8(static_cast<std::uint8_t>(0x07 | ((reg_or_op & 7) << 3)));
        } else if (disp >= -128 && disp <= 127) {
            emit_u8(static_cast<std::uint8_t>(0x47 | ((reg_or_op & 7) << 3)));
            emit_u8(static_cast<std::uint8_t>(disp & 0xFF));
        } else {
            emit_u8(static_cast<std::uint8_t>(0x87 | ((reg_or_op & 7) << 3)));
            emit_u16(static_cast<std::uint16_t>(disp));
        }
    }

    bool can_emit_ptr_to_bx_preserving_ax_dx(const ExprPtr& ptr_expr) const {
        if (!ptr_expr || !ptr_expr->type) return false;
        if (ptr_expr->kind == ExprKind::binary &&
            (ptr_expr->op == Tok::plus || ptr_expr->op == Tok::minus) &&
            ptr_expr->lhs && ptr_expr->lhs->type &&
            (ptr_expr->lhs->type->kind == TypeKind::t_ptr ||
             ptr_expr->lhs->type->kind == TypeKind::t_array) &&
            ptr_expr->rhs && ptr_expr->rhs->kind == ExprKind::int_lit) {
            return can_emit_ptr_to_bx_preserving_ax_dx(ptr_expr->lhs);
        }
        if (ptr_expr->type->kind == TypeKind::t_array && ptr_expr->kind == ExprKind::member) {
            return can_emit_lvalue_bx_preserving_ax_dx(ptr_expr);
        }
        if (ptr_expr->type->kind == TypeKind::t_ptr && !ptr_expr->type->is_far) {
            DirectMem dm;
            return try_resolve_direct_mem(ptr_expr, dm);
        }
        return false;
    }

    bool can_emit_lvalue_bx_preserving_ax_dx(const ExprPtr& e) const {
        if (!e) return false;
        if (e->kind == ExprKind::cast) return can_emit_lvalue_bx_preserving_ax_dx(e->lhs);
        if (e->kind == ExprKind::member) {
            if (e->is_arrow) return can_emit_ptr_to_bx_preserving_ax_dx(e->lhs);
            return can_emit_lvalue_bx_preserving_ax_dx(e->lhs);
        }
        if (e->kind == ExprKind::unary && e->op == Tok::star) {
            return can_emit_ptr_to_bx_preserving_ax_dx(e->lhs);
        }
        return false;
    }

    void emit_ptr_to_bx(const ExprPtr& ptr_expr, bool& out_far, int& inout_disp) {
        if (ptr_expr && ptr_expr->kind == ExprKind::binary &&
            (ptr_expr->op == Tok::plus || ptr_expr->op == Tok::minus) &&
            ptr_expr->lhs && ptr_expr->lhs->type &&
            (ptr_expr->lhs->type->kind == TypeKind::t_ptr ||
             ptr_expr->lhs->type->kind == TypeKind::t_array) &&
            ptr_expr->rhs && ptr_expr->rhs->kind == ExprKind::int_lit) {
            const std::size_t elem_sz =
                std::max<std::size_t>(1, type_size(ptr_expr->lhs->type->base));
            const int delta = static_cast<int>(
                static_cast<std::int16_t>(ptr_expr->rhs->int_val) * static_cast<int>(elem_sz));
            emit_ptr_to_bx(ptr_expr->lhs, out_far, inout_disp);
            inout_disp += (ptr_expr->op == Tok::plus ? delta : -delta);
            return;
        }
        if (ptr_expr && ptr_expr->type && ptr_expr->type->kind == TypeKind::t_array &&
            ptr_expr->kind == ExprKind::member) {
            int sub_disp = 0;
            emit_lvalue_bx_disp(ptr_expr, out_far, sub_disp);
            inout_disp += sub_disp;
            return;
        }
        if (ptr_expr && ptr_expr->type && ptr_expr->type->kind == TypeKind::t_ptr &&
            !ptr_expr->type->is_far) {
            DirectMem dm;
            if (try_resolve_direct_mem(ptr_expr, dm)) {
                out_far = false;
                if (dm.kind == DirectMem::bp_rel) {
                    emit_u8(0x8B); emit_bp_rm(3, dm.disp);  // mov bx, [bp+d]
                } else {
                    emit_u8(0x8B); emit_u8(0x1E);           // mov bx, [sym+d]
                    emit_dgroup_disp(dm.sym, dm.disp);
                }
                return;
            }
        }
        emit_expr(ptr_expr);
        if (ptr_expr && ptr_expr->type && ptr_expr->type->is_far) {
            emit_u8(0x8E); emit_u8(0xC2);  // mov es, dx
            out_far = true;
        } else {
            out_far = false;
        }
        emit_u8(0x8B); emit_u8(0xD8);  // mov bx, ax
    }

    void emit_lvalue_bx_disp(const ExprPtr& e, bool& out_far, int& out_disp) {
        out_far = false;
        out_disp = 0;
        if (!e) throw FormatError(0, "expression is not assignable");
        if (DirectMem dm; try_resolve_direct_mem(e, dm)) {
            if (dm.kind == DirectMem::bp_rel) {
                emit_u8(0x8D); emit_bp_rm(3, dm.disp);  // lea bx, [bp+d]
            } else {
                emit_u8(0xBB);  // mov bx, offset sym+d
                emit_dgroup_disp(dm.sym, dm.disp);
            }
            return;
        }
        if (e->kind == ExprKind::cast) {
            emit_lvalue_bx_disp(e->lhs, out_far, out_disp);
            return;
        }
        if (e->kind == ExprKind::member) {
            TypePtr st = e->is_arrow && e->lhs->type ? e->lhs->type->base : e->lhs->type;
            const int foff = static_cast<int>(lookup_field_offset(st, e->name));
            if (e->is_arrow) {
                emit_ptr_to_bx(e->lhs, out_far, out_disp);
            } else {
                emit_lvalue_bx_disp(e->lhs, out_far, out_disp);
            }
            out_disp += foff;
            return;
        }
        if (e->kind == ExprKind::unary && e->op == Tok::star) {
            emit_ptr_to_bx(e->lhs, out_far, out_disp);
            return;
        }
        throw FormatError(0, "expression is not assignable");
    }

    // Loads address of an lvalue into BX (or ES:BX if far)
    void emit_lvalue_addr(const ExprPtr& e, bool& out_far) {
        int disp = 0;
        emit_lvalue_bx_disp(e, out_far, disp);
        if (disp != 0) {
            emit_u8(0x8D); emit_bx_rm(3, disp);  // lea bx, [bx + disp]
        }
    }

    void load_from_bx(const TypePtr& t, bool is_far, int disp = 0) {
        if (is_far) emit_u8(0x26);  // es:
        if (is_byte_scalar(t)) {
            emit_u8(0x8A); emit_bx_rm(0, disp);  // mov al, [bx+disp]
            if (is_signed_type(t)) emit_u8(0x98);  // cbw
            else { emit_u8(0x32); emit_u8(0xE4); }  // xor ah, ah
        } else if (is_32bit_scalar(t)) {
            emit_u8(0x8B); emit_bx_rm(0, disp);      // mov ax, [bx+disp]
            if (is_far) emit_u8(0x26);
            emit_u8(0x8B); emit_bx_rm(2, disp + 2);  // mov dx, [bx+disp+2]
        } else {
            emit_u8(0x8B); emit_bx_rm(0, disp);  // mov ax, [bx+disp]
        }
    }

    void store_to_bx(const TypePtr& t, bool is_far, int disp = 0) {
        if (is_far) emit_u8(0x26);  // es:
        if (is_byte_scalar(t)) {
            emit_u8(0x88); emit_bx_rm(0, disp);  // mov [bx+disp], al
        } else if (is_32bit_scalar(t)) {
            emit_u8(0x89); emit_bx_rm(0, disp);      // mov [bx+disp], ax
            if (is_far) emit_u8(0x26);
            emit_u8(0x89); emit_bx_rm(2, disp + 2);  // mov [bx+disp+2], dx
        } else {
            emit_u8(0x89); emit_bx_rm(0, disp);  // mov [bx+disp], ax
        }
    }

    void emit_grp1_bp_imm16(std::uint8_t subop, int bp_disp, std::uint16_t imm) {
        const std::int16_t simm = static_cast<std::int16_t>(imm);
        if (simm >= -128 && simm <= 127) {
            emit_u8(0x83);
            emit_bp_rm(subop, bp_disp);
            emit_u8(static_cast<std::uint8_t>(simm & 0xFF));
        } else {
            emit_u8(0x81);
            emit_bp_rm(subop, bp_disp);
            emit_u16(imm);
        }
    }

    void emit_expr(const ExprPtr& e, bool need_value = true) {
        if (!e) return;
        if (!need_value && (e->kind == ExprKind::int_lit || e->kind == ExprKind::str_lit ||
                            e->kind == ExprKind::var || e->kind == ExprKind::member)) {
            if (e->kind == ExprKind::member && e->lhs) emit_expr(e->lhs, false);
            return;
        }
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
                if (e->type && e->type->kind == TypeKind::t_func) {
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
                    break;
                }
                if (!(e->type && e->type->kind == TypeKind::t_struct)) {
                    DirectMem dm;
                    if (try_resolve_direct_mem(e, dm)) {
                        load_direct_mem(e->type, dm);
                        break;
                    }
                }
                bool is_far = false;
                int disp = 0;
                emit_lvalue_bx_disp(e, is_far, disp);
                load_from_bx(e->type, is_far, disp);
                break;
            }

            case ExprKind::member: {
                if (!(e->type && e->type->kind == TypeKind::t_struct)) {
                    DirectMem dm;
                    if (try_resolve_direct_mem(e, dm)) {
                        load_direct_mem(e->type, dm);
                        break;
                    }
                }
                if (e->type && e->type->kind == TypeKind::t_array) {
                    bool is_far = false;
                    emit_lvalue_addr(e, is_far);
                    emit_u8(0x8B); emit_u8(0xC3);  // mov ax, bx
                    if (is_far) { emit_u8(0x8C); emit_u8(0xC2); }  // mov dx, es
                } else {
                    bool is_far = false;
                    int disp = 0;
                    emit_lvalue_bx_disp(e, is_far, disp);
                    load_from_bx(e->type, is_far, disp);
                }
                break;
            }

            case ExprKind::cast:
                if (!need_value) {
                    emit_expr(e->lhs, false);
                    break;
                }
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
                emit_expr(e->lhs, false);
                emit_expr(e->rhs, need_value);
                break;

            case ExprKind::ternary: {
                const LabelId l_false = new_label();
                const LabelId l_end = new_label();
                emit_branch(e->lhs, l_false, false);
                emit_expr(e->rhs, need_value);
                if (need_value && is_32bit_scalar(e->type)) emit_widen_dxax(e->rhs->type);
                emit_jmp(l_end);
                bind_label(l_false);
                emit_expr(e->third, need_value);
                if (need_value && is_32bit_scalar(e->type)) emit_widen_dxax(e->third->type);
                bind_label(l_end);
                break;
            }

            case ExprKind::assign: {
                emit_expr(e->rhs);
                if (is_32bit_scalar(e->type)) emit_widen_dxax(e->rhs->type);
                if (!(e->type && e->type->kind == TypeKind::t_struct)) {
                    DirectMem dm;
                    if (try_resolve_direct_mem(e->lhs, dm)) {
                        store_direct_mem(e->type, dm);
                        break;
                    }
                    if (can_emit_lvalue_bx_preserving_ax_dx(e->lhs)) {
                        bool is_far = false;
                        int disp = 0;
                        emit_lvalue_bx_disp(e->lhs, is_far, disp);
                        store_to_bx(e->type, is_far, disp);
                        break;
                    }
                }
                emit_u8(0x50);  // push ax
                if (is_32bit_scalar(e->type)) emit_u8(0x52);  // push dx
                bool is_far = false;
                int disp = 0;
                emit_lvalue_bx_disp(e->lhs, is_far, disp);
                if (is_32bit_scalar(e->type)) emit_u8(0x5A);  // pop dx
                emit_u8(0x58);  // pop ax
                store_to_bx(e->type, is_far, disp);
                break;
            }

            case ExprKind::compound_assign: {
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

                const bool lhs_is_ptr = e->type && e->type->kind == TypeKind::t_ptr;
                const std::uint16_t ptr_scale =
                    (lhs_is_ptr && (bin_op == Tok::plus || bin_op == Tok::minus) && e->type->base)
                        ? static_cast<std::uint16_t>(std::max<std::size_t>(1, type_size(e->type->base)))
                        : 1U;

                // Fast in-place compound assignment on DirectMem ([bp+d] or [sym+d])
                if (DirectMem dm; try_resolve_direct_mem(e->lhs, dm)) {
                    const bool is_grp1 = (bin_op == Tok::plus || bin_op == Tok::minus ||
                                          bin_op == Tok::amp || bin_op == Tok::pipe ||
                                          bin_op == Tok::caret);
                    const std::uint8_t grp1_sub = bin_op == Tok::plus    ? 0
                                                  : bin_op == Tok::pipe  ? 1
                                                  : bin_op == Tok::amp   ? 4
                                                  : bin_op == Tok::minus ? 5
                                                                         : 6;
                    if (dm.kind == DirectMem::bp_rel) {
                        const int disp = dm.disp;
                        if (!is_32bit_scalar(e->type) && is_grp1 && e->rhs->kind == ExprKind::int_lit) {
                            const std::uint16_t imm = static_cast<std::uint16_t>(
                                static_cast<std::uint16_t>(e->rhs->int_val) * ptr_scale);
                            if (is_byte_scalar(e->type)) {
                                if (imm != 0 || bin_op == Tok::amp) {
                                    emit_u8(0x80);
                                    emit_bp_rm(grp1_sub, disp);
                                    emit_u8(static_cast<std::uint8_t>(imm & 0xFF));
                                }
                            } else {
                                if (imm == 1 && (bin_op == Tok::plus || bin_op == Tok::minus)) {
                                    emit_u8(0xFF);
                                    emit_bp_rm(bin_op == Tok::plus ? 0 : 1, disp);  // inc/dec word ptr [bp+d]
                                } else if (imm != 0 || bin_op == Tok::amp) {
                                    emit_grp1_bp_imm16(grp1_sub, disp, imm);
                                }
                            }
                            if (need_value) load_direct_mem(e->type, dm);
                            break;
                        }
                        if (!need_value && !is_32bit_scalar(e->type) && !is_byte_scalar(e->type) && is_grp1) {
                            emit_expr(e->rhs, true);
                            if (ptr_scale > 1) emit_mul16_ax_const(ptr_scale, false);
                            const std::uint8_t mr_opc = bin_op == Tok::plus    ? 0x01
                                                        : bin_op == Tok::pipe  ? 0x09
                                                        : bin_op == Tok::amp   ? 0x21
                                                        : bin_op == Tok::minus ? 0x29
                                                                               : 0x31;
                            emit_u8(mr_opc);
                            emit_bp_rm(0, disp);  // op [bp+d], ax
                            break;
                        }
                        const bool is32_int = e->type && (e->type->kind == TypeKind::t_long ||
                                                          e->type->kind == TypeKind::t_ulong);
                        if (!need_value && is32_int && (bin_op == Tok::plus || bin_op == Tok::minus)) {
                            if (e->rhs->kind == ExprKind::int_lit) {
                                std::uint32_t uval = 0;
                                if (is_32bit_scalar(e->rhs->type)) {
                                    uval = static_cast<std::uint32_t>(e->rhs->int_val);
                                } else if (is_signed_type(e->rhs->type)) {
                                    uval = static_cast<std::uint32_t>(
                                        static_cast<std::int32_t>(static_cast<std::int16_t>(e->rhs->int_val)));
                                } else {
                                    uval = static_cast<std::uint16_t>(e->rhs->int_val);
                                }
                                if (uval != 0) {
                                    const std::uint16_t lo = static_cast<std::uint16_t>(uval & 0xFFFFU);
                                    const std::uint16_t hi = static_cast<std::uint16_t>((uval >> 16) & 0xFFFFU);
                                    if (lo == 0) {
                                        emit_grp1_bp_imm16(bin_op == Tok::plus ? 0 : 5, disp + 2, hi);
                                    } else {
                                        emit_grp1_bp_imm16(bin_op == Tok::plus ? 0 : 5, disp, lo);
                                        emit_grp1_bp_imm16(bin_op == Tok::plus ? 2 : 3, disp + 2, hi);  // adc/sbb
                                    }
                                }
                            } else {
                                emit_expr(e->rhs, true);
                                emit_widen_dxax(e->rhs->type);
                                emit_u8(bin_op == Tok::plus ? 0x01 : 0x29);
                                emit_bp_rm(0, disp);      // add/sub [bp+d], ax
                                emit_u8(bin_op == Tok::plus ? 0x11 : 0x19);
                                emit_bp_rm(2, disp + 2);  // adc/sbb [bp+d+2], dx
                            }
                            break;
                        }
                    } else {
                        if (!is_32bit_scalar(e->type) && is_grp1 && e->rhs->kind == ExprKind::int_lit) {
                            const std::uint16_t imm = static_cast<std::uint16_t>(
                                static_cast<std::uint16_t>(e->rhs->int_val) * ptr_scale);
                            if (is_byte_scalar(e->type)) {
                                if (imm != 0 || bin_op == Tok::amp) {
                                    emit_u8(0x80);
                                    emit_u8(static_cast<std::uint8_t>((grp1_sub << 3) | 0x06));
                                    emit_dgroup_disp(dm.sym, dm.disp);
                                    emit_u8(static_cast<std::uint8_t>(imm & 0xFF));
                                }
                            } else if (imm == 1 && (bin_op == Tok::plus || bin_op == Tok::minus)) {
                                emit_u8(0xFF);
                                emit_u8(bin_op == Tok::plus ? 0x06 : 0x0E);
                                emit_dgroup_disp(dm.sym, dm.disp);
                            } else if (imm != 0 || bin_op == Tok::amp) {
                                const std::int16_t simm = static_cast<std::int16_t>(imm);
                                if (simm >= -128 && simm <= 127) {
                                    emit_u8(0x83);
                                    emit_u8(static_cast<std::uint8_t>((grp1_sub << 3) | 0x06));
                                    emit_dgroup_disp(dm.sym, dm.disp);
                                    emit_u8(static_cast<std::uint8_t>(simm & 0xFF));
                                } else {
                                    emit_u8(0x81);
                                    emit_u8(static_cast<std::uint8_t>((grp1_sub << 3) | 0x06));
                                    emit_dgroup_disp(dm.sym, dm.disp);
                                    emit_u16(imm);
                                }
                            }
                            if (need_value) load_direct_mem(e->type, dm);
                            break;
                        }
                        if (!need_value && !is_32bit_scalar(e->type) && !is_byte_scalar(e->type) && is_grp1) {
                            emit_expr(e->rhs, true);
                            if (ptr_scale > 1) emit_mul16_ax_const(ptr_scale, false);
                            const std::uint8_t mr_opc = bin_op == Tok::plus    ? 0x01
                                                        : bin_op == Tok::pipe  ? 0x09
                                                        : bin_op == Tok::amp   ? 0x21
                                                        : bin_op == Tok::minus ? 0x29
                                                                               : 0x31;
                            emit_u8(mr_opc);
                            emit_u8(0x06);
                            emit_dgroup_disp(dm.sym, dm.disp);
                            break;
                        }
                    }
                }

                bool is_far = false;
                int bx_disp = 0;
                emit_lvalue_bx_disp(e->lhs, is_far, bx_disp);
                if (is_far) emit_u8(0x06);  // push es
                emit_u8(0x53);              // push bx
                load_from_bx(e->type, is_far, bx_disp);
                if (is_32bit_scalar(e->type) && !lhs_is_ptr) {
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
                    if (lhs_is_ptr && e->type->is_far) emit_u8(0x52);  // preserve far seg
                    emit_u8(0x50);  // push ax (lhs value)
                    emit_expr(e->rhs);
                    emit_u8(0x8B); emit_u8(0xC8);  // mov cx, ax (rhs)
                    if (ptr_scale > 1) emit_scale16_cx(ptr_scale);
                    emit_u8(0x58);                 // pop ax (lhs)
                    if (lhs_is_ptr && e->type->is_far) emit_u8(0x5A);
                    emit_alu_ax_cx(bin_op, e->type);
                }
                emit_u8(0x5B);  // pop bx
                if (is_far) emit_u8(0x07);  // pop es
                store_to_bx(e->type, is_far, bx_disp);
                break;
            }

            case ExprKind::unary: {
                if (e->op == Tok::amp) {
                    if (!need_value) { emit_expr(e->lhs, false); break; }
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
                    if (!need_value) { emit_expr(e->lhs, false); break; }
                    if (e->type && (e->type->kind == TypeKind::t_array ||
                                    e->type->kind == TypeKind::t_func)) {
                        emit_expr(e->lhs);
                        break;
                    }
                    if (!(e->type && e->type->kind == TypeKind::t_struct)) {
                        DirectMem dm;
                        if (try_resolve_direct_mem(e, dm)) {
                            load_direct_mem(e->type, dm);
                            break;
                        }
                    }
                    bool is_far = false;
                    int disp = 0;
                    emit_ptr_to_bx(e->lhs, is_far, disp);
                    load_from_bx(e->type, is_far, disp);
                    break;
                }
                if (e->op == Tok::inc || e->op == Tok::dec) {
                    const bool is_inc = (e->op == Tok::inc);
                    std::uint16_t step = 1;
                    if (e->type && e->type->kind == TypeKind::t_ptr && e->type->base) {
                        step = static_cast<std::uint16_t>(std::max<std::size_t>(1, type_size(e->type->base)));
                    }
                    const bool is32 = is_32bit_scalar(e->type);
                    const bool is32_int = e->type && (e->type->kind == TypeKind::t_long ||
                                                      e->type->kind == TypeKind::t_ulong);

                    // Fast in-place ++ / -- on DirectMem ([bp+d] or [sym+d])
                    if (DirectMem dm; !is32 && try_resolve_direct_mem(e->lhs, dm)) {
                        if (dm.kind == DirectMem::bp_rel) {
                            const int disp = dm.disp;
                            auto emit_mod = [&]() {
                                if (is_byte_scalar(e->type)) {
                                    emit_u8(0xFE);
                                    emit_bp_rm(is_inc ? 0 : 1, disp);
                                } else if (step == 1) {
                                    emit_u8(0xFF);
                                    emit_bp_rm(is_inc ? 0 : 1, disp);  // inc/dec word ptr [bp+d]
                                } else {
                                    emit_grp1_bp_imm16(is_inc ? 0 : 5, disp, step);
                                }
                            };
                            if (!need_value) {
                                emit_mod();
                            } else if (e->is_postfix) {
                                load_direct_mem(e->type, dm);
                                emit_mod();
                            } else {
                                emit_mod();
                                load_direct_mem(e->type, dm);
                            }
                            break;
                        }
                        auto emit_gmod = [&]() {
                            if (is_byte_scalar(e->type)) {
                                emit_u8(0xFE);
                                emit_u8(is_inc ? 0x06 : 0x0E);
                                emit_dgroup_disp(dm.sym, dm.disp);
                            } else if (step == 1) {
                                emit_u8(0xFF);
                                emit_u8(is_inc ? 0x06 : 0x0E);
                                emit_dgroup_disp(dm.sym, dm.disp);
                            } else if (step <= 127) {
                                emit_u8(0x83);
                                emit_u8(is_inc ? 0x06 : 0x2E);
                                emit_dgroup_disp(dm.sym, dm.disp);
                                emit_u8(static_cast<std::uint8_t>(step));
                            } else {
                                emit_u8(0x81);
                                emit_u8(is_inc ? 0x06 : 0x2E);
                                emit_dgroup_disp(dm.sym, dm.disp);
                                emit_u16(step);
                            }
                        };
                        if (!need_value) {
                            emit_gmod();
                        } else if (e->is_postfix) {
                            load_direct_mem(e->type, dm);
                            emit_gmod();
                        } else {
                            emit_gmod();
                            load_direct_mem(e->type, dm);
                        }
                        break;
                    }

                    if (DirectMem dm; !need_value && is32_int && try_resolve_direct_mem(e->lhs, dm)) {
                        if (dm.kind == DirectMem::bp_rel) {
                            emit_grp1_bp_imm16(is_inc ? 0 : 5, dm.disp, step);
                            emit_grp1_bp_imm16(is_inc ? 2 : 3, dm.disp + 2, 0);
                        } else {
                            emit_u8(0x83);
                            emit_u8(is_inc ? 0x06 : 0x2E);  // add/sub word ptr [sym+d], step
                            emit_dgroup_disp(dm.sym, dm.disp);
                            emit_u8(static_cast<std::uint8_t>(step));
                            emit_u8(0x83);
                            emit_u8(is_inc ? 0x16 : 0x1E);  // adc/sbb word ptr [sym+d+2], 0
                            emit_dgroup_disp(dm.sym, dm.disp + 2);
                            emit_u8(0);
                        }
                        break;
                    }

                    // General lvalue via [es:][bx+disp] — avoid push/pop when 8/16-bit
                    bool is_far = false;
                    int bx_disp = 0;
                    emit_lvalue_bx_disp(e->lhs, is_far, bx_disp);
                    if (!is32) {
                        auto emit_bx_mod = [&]() {
                            if (is_far) emit_u8(0x26);
                            if (is_byte_scalar(e->type)) {
                                emit_u8(0xFE); emit_bx_rm(is_inc ? 0 : 1, bx_disp);
                            } else if (step == 1) {
                                emit_u8(0xFF); emit_bx_rm(is_inc ? 0 : 1, bx_disp);
                            } else if (step <= 127) {
                                emit_u8(0x83); emit_bx_rm(is_inc ? 0 : 5, bx_disp);
                                emit_u8(static_cast<std::uint8_t>(step));
                            } else {
                                emit_u8(0x81); emit_bx_rm(is_inc ? 0 : 5, bx_disp);
                                emit_u16(step);
                            }
                        };
                        if (!need_value) {
                            emit_bx_mod();
                        } else if (e->is_postfix) {
                            load_from_bx(e->type, is_far, bx_disp);
                            emit_bx_mod();
                        } else {
                            emit_bx_mod();
                            load_from_bx(e->type, is_far, bx_disp);
                        }
                        break;
                    }

                    load_from_bx(e->type, is_far, bx_disp);
                    if (need_value && e->is_postfix) {
                        if (is32) emit_u8(0x52);  // push dx
                        emit_u8(0x50);            // push ax
                    }
                    if (is_inc) {
                        emit_u8(0x05); emit_u16(step);  // add ax, step
                        if (is32_int) { emit_u8(0x83); emit_u8(0xD2); emit_u8(0x00); }  // adc dx, 0
                    } else {
                        emit_u8(0x2D); emit_u16(step);  // sub ax, step
                        if (is32_int) { emit_u8(0x83); emit_u8(0xDA); emit_u8(0x00); }  // sbb dx, 0
                    }
                    store_to_bx(e->type, is_far, bx_disp);
                    if (need_value && e->is_postfix) {
                        emit_u8(0x58);            // pop ax
                        if (is32) emit_u8(0x5A);  // pop dx
                    }
                    break;
                }
                if (!need_value) {
                    emit_expr(e->lhs, false);
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
                    if (!need_value) {
                        const LabelId l_done = new_label();
                        emit_branch(e->lhs, l_done, false);
                        emit_expr(e->rhs, false);
                        bind_label(l_done);
                        break;
                    }
                    const LabelId l_false = new_label();
                    const LabelId l_end = new_label();
                    emit_branch(e, l_false, false);
                    emit_u8(0xB8); emit_u16(1);     // mov ax, 1
                    emit_jmp(l_end);
                    bind_label(l_false);
                    emit_u8(0x33); emit_u8(0xC0);  // xor ax, ax
                    bind_label(l_end);
                    break;
                }
                if (e->op == Tok::lor) {
                    if (!need_value) {
                        const LabelId l_done = new_label();
                        emit_branch(e->lhs, l_done, true);
                        emit_expr(e->rhs, false);
                        bind_label(l_done);
                        break;
                    }
                    const LabelId l_true = new_label();
                    const LabelId l_end = new_label();
                    emit_branch(e, l_true, true);
                    emit_u8(0x33); emit_u8(0xC0);  // xor ax, ax
                    emit_jmp(l_end);
                    bind_label(l_true);
                    emit_u8(0xB8); emit_u16(1);     // mov ax, 1
                    bind_label(l_end);
                    break;
                }
                if (!need_value) {
                    emit_expr(e->lhs, false);
                    emit_expr(e->rhs, false);
                    break;
                }

                const bool lhs_ptr = e->lhs->type && (e->lhs->type->kind == TypeKind::t_ptr ||
                                                      e->lhs->type->kind == TypeKind::t_array);
                const bool rhs_ptr = e->rhs->type && (e->rhs->type->kind == TypeKind::t_ptr ||
                                                      e->rhs->type->kind == TypeKind::t_array);

                // Pointer +/- constant index fast path (e.g. arr[0], arr[k], ptr + k)
                if ((e->op == Tok::plus || e->op == Tok::minus) && lhs_ptr && !rhs_ptr &&
                    e->rhs->kind == ExprKind::int_lit) {
                    const std::size_t elem_sz = std::max<std::size_t>(1, type_size(e->lhs->type->base));
                    const std::uint16_t byte_off = static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(e->rhs->int_val) * static_cast<std::uint16_t>(elem_sz));
                    emit_expr(e->lhs);
                    if (byte_off == 0) break;
                    if (e->op == Tok::plus) {
                        if (byte_off == 1) emit_u8(0x40);
                        else if (byte_off == 2) { emit_u8(0x40); emit_u8(0x40); }
                        else { emit_u8(0x05); emit_u16(byte_off); }
                    } else {
                        if (byte_off == 1) emit_u8(0x48);
                        else if (byte_off == 2) { emit_u8(0x48); emit_u8(0x48); }
                        else { emit_u8(0x2D); emit_u16(byte_off); }
                    }
                    break;
                }

                if (e->rhs->kind == ExprKind::int_lit && !is_32bit_scalar(e->type) &&
                    !is_32bit_scalar(e->lhs->type) && !lhs_ptr && !rhs_ptr) {
                    const std::uint16_t imm = static_cast<std::uint16_t>(e->rhs->int_val);
                    const bool is_signed = is_signed_type(e->lhs->type) && is_signed_type(e->rhs->type);
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
                    if (e->op == Tok::shl) {
                        emit_expr(e->lhs);
                        emit_shl16_ax_const(imm);
                        break;
                    }
                    if (e->op == Tok::shr) {
                        emit_expr(e->lhs);
                        emit_shr16_ax_const(imm, is_signed_type(e->lhs->type));
                        break;
                    }
                    if (e->op == Tok::star) {
                        emit_expr(e->lhs);
                        emit_mul16_ax_const(imm, is_signed_type(e->type));
                        break;
                    }
                    if ((e->op == Tok::slash || e->op == Tok::percent) && imm != 0) {
                        emit_expr(e->lhs);
                        if (!is_signed) {
                            if (imm == 1) {
                                if (e->op == Tok::percent) { emit_u8(0x33); emit_u8(0xC0); }
                                break;
                            }
                            const int lg = exact_log2_u32(imm);
                            if (lg >= 1) {
                                if (e->op == Tok::slash) {
                                    emit_shr16_ax_const(static_cast<unsigned>(lg), false);
                                } else {
                                    emit_u8(0x25); emit_u16(static_cast<std::uint16_t>(imm - 1U));  // and ax, mask
                                }
                                break;
                            }
                        } else if (imm == 1) {
                            if (e->op == Tok::percent) { emit_u8(0x33); emit_u8(0xC0); }
                            break;
                        }
                        emit_u8(0xB9); emit_u16(imm);  // mov cx, imm
                        emit_alu_ax_cx(e->op, e->type, is_signed);
                        break;
                    }
                    if (e->op == Tok::plus && imm == 2) {
                        emit_expr(e->lhs);
                        emit_u8(0x40); emit_u8(0x40);  // inc ax; inc ax
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
                        if (imm == 0) {
                            if (e->op == Tok::amp) { emit_u8(0x33); emit_u8(0xC0); }
                            break;
                        }
                        if (e->op == Tok::amp && imm == 0xFFFF) break;
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
                        emit_cmp_setcc(e->op, is_signed);
                        break;
                    }
                }

                // Direct 16-bit memory RHS fast path ([bp+d] or [sym+d])
                if (!is_32bit_scalar(e->type) && !is_32bit_scalar(e->lhs->type) &&
                    !is_32bit_scalar(e->rhs->type)) {
                    DirectMem dm;
                    if (try_resolve_word_mem(e->rhs, dm)) {
                        const bool byte_ptr_add =
                            (e->op == Tok::plus || e->op == Tok::minus) && lhs_ptr && !rhs_ptr &&
                            std::max<std::size_t>(1, type_size(e->lhs->type->base)) == 1;
                        if (((!lhs_ptr && !rhs_ptr) || byte_ptr_add) &&
                            (e->op == Tok::plus || e->op == Tok::minus || e->op == Tok::amp ||
                             e->op == Tok::pipe || e->op == Tok::caret)) {
                            emit_expr(e->lhs);
                            const std::uint8_t opc = e->op == Tok::plus    ? 0x03
                                                     : e->op == Tok::minus ? 0x2B
                                                     : e->op == Tok::amp   ? 0x23
                                                     : e->op == Tok::pipe  ? 0x0B
                                                                           : 0x33;
                            emit_op_ax_direct_mem(opc, dm);
                            break;
                        }
                        if (e->op == Tok::eq || e->op == Tok::ne || e->op == Tok::lt ||
                            e->op == Tok::le || e->op == Tok::gt || e->op == Tok::ge) {
                            emit_expr(e->lhs);
                            emit_op_ax_direct_mem(0x3B, dm);
                            emit_cmp_setcc(e->op, is_signed_type(e->lhs->type) &&
                                                      is_signed_type(e->rhs->type));
                            break;
                        }
                    }
                }

                if (!lhs_ptr && !rhs_ptr &&
                    (is_32bit_scalar(e->lhs->type) || is_32bit_scalar(e->rhs->type))) {
                    emit_binary_32bit(e);
                    break;
                }

                emit_expr(e->lhs);
                const bool lhs_far_ptr = lhs_ptr && e->lhs->type->is_far;
                if (lhs_far_ptr) emit_u8(0x52);  // push dx (preserve far segment)
                emit_u8(0x50);                   // push ax
                emit_expr(e->rhs);
                const bool rhs_far_ptr = rhs_ptr && e->rhs->type->is_far;
                emit_u8(0x8B); emit_u8(0xC8);    // mov cx, ax (rhs)
                emit_u8(0x58);                   // pop ax (lhs)
                if (lhs_far_ptr) emit_u8(0x5A);  // pop dx

                // Scale pointer arithmetic
                if ((e->op == Tok::plus || e->op == Tok::minus) && lhs_ptr && !rhs_ptr) {
                    std::size_t elem_sz = std::max<std::size_t>(1, type_size(e->lhs->type->base));
                    if (elem_sz > 1) {
                        emit_scale16_cx(static_cast<std::uint16_t>(elem_sz));
                    }
                    emit_alu_ax_cx(e->op, e->type, false);
                    break;
                }
                if (e->op == Tok::plus && !lhs_ptr && rhs_ptr) {
                    // idx (in AX) + ptr (in DX:CX or CX): swap AX and CX, scale CX, add
                    emit_u8(0x91);  // xchg ax, cx
                    std::size_t elem_sz = std::max<std::size_t>(1, type_size(e->rhs->type->base));
                    if (elem_sz > 1) {
                        emit_scale16_cx(static_cast<std::uint16_t>(elem_sz));
                    }
                    (void)rhs_far_ptr;
                    emit_u8(0x03); emit_u8(0xC1);  // add ax, cx
                    break;
                }
                if (e->op == Tok::minus && lhs_ptr && rhs_ptr) {
                    emit_u8(0x2B); emit_u8(0xC1);  // sub ax, cx
                    std::size_t elem_sz = std::max<std::size_t>(1, type_size(e->lhs->type->base));
                    if (elem_sz > 1) {
                        const int lg = exact_log2_u32(static_cast<std::uint32_t>(elem_sz));
                        if (lg >= 1) {
                            emit_shr16_ax_const(static_cast<unsigned>(lg), true);
                        } else {
                            emit_u8(0xB9); emit_u16(static_cast<std::uint16_t>(elem_sz));
                            emit_u8(0x99);                 // cwd
                            emit_u8(0xF7); emit_u8(0xF9);  // idiv cx
                        }
                    }
                    break;
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
                auto push_arg = [&](std::size_t idx, const ExprPtr& arg) {
                    emit_expr(arg);
                    bool want32 = is_32bit_scalar(arg->type);
                    if (fn_type && idx < fn_type->params.size() && fn_type->params[idx].type) {
                        want32 = is_32bit_scalar(fn_type->params[idx].type);
                        if (want32) emit_widen_dxax(arg->type);
                    }
                    if (want32) {
                        emit_u8(0x52);  // push dx (segment/high word first)
                        emit_u8(0x50);  // push ax (offset/low word)
                        arg_bytes += 4;
                    } else {
                        emit_u8(0x50);  // push ax
                        arg_bytes += 2;
                    }
                };
                if (is_pascal) {
                    for (std::size_t i = 0; i < e->args.size(); ++i) push_arg(i, e->args[i]);
                } else {
                    for (std::size_t i = e->args.size(); i > 0; --i) push_arg(i - 1, e->args[i - 1]);
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
        const bool is_signed = is_signed_type(e->lhs->type) && is_signed_type(e->rhs->type);
        if (e->rhs->kind == ExprKind::int_lit) {
            std::uint32_t uval = 0;
            if (is_32bit_scalar(e->rhs->type)) {
                uval = static_cast<std::uint32_t>(e->rhs->int_val);
            } else if (is_signed_type(e->rhs->type)) {
                uval = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(e->rhs->int_val)));
            } else {
                uval = static_cast<std::uint16_t>(e->rhs->int_val);
            }
            const std::uint16_t lo = static_cast<std::uint16_t>(uval & 0xFFFFU);
            const std::uint16_t hi = static_cast<std::uint16_t>((uval >> 16) & 0xFFFFU);

            if (e->op == Tok::shl) {
                emit_expr(e->lhs);
                emit_widen_dxax(e->lhs->type);
                emit_shl32_dxax_const(uval & 0xFFU);
                return;
            }
            if (e->op == Tok::shr) {
                emit_expr(e->lhs);
                emit_widen_dxax(e->lhs->type);
                emit_shr32_dxax_const(uval & 0xFFU, is_signed_type(e->lhs->type));
                return;
            }
            if (e->op == Tok::plus) {
                emit_expr(e->lhs);
                emit_widen_dxax(e->lhs->type);
                if (uval == 0) return;
                if (lo == 0) {
                    emit_u8(0x81); emit_u8(0xC2); emit_u16(hi);  // add dx, hi
                } else {
                    emit_u8(0x05); emit_u16(lo);                 // add ax, lo
                    emit_u8(0x81); emit_u8(0xD2); emit_u16(hi);  // adc dx, hi
                }
                return;
            }
            if (e->op == Tok::minus) {
                emit_expr(e->lhs);
                emit_widen_dxax(e->lhs->type);
                if (uval == 0) return;
                if (lo == 0) {
                    emit_u8(0x81); emit_u8(0xEA); emit_u16(hi);  // sub dx, hi
                } else {
                    emit_u8(0x2D); emit_u16(lo);                 // sub ax, lo
                    emit_u8(0x81); emit_u8(0xDA); emit_u16(hi);  // sbb dx, hi
                }
                return;
            }
            if (e->op == Tok::amp) {
                emit_expr(e->lhs);
                emit_widen_dxax(e->lhs->type);
                if (lo == 0) { emit_u8(0x33); emit_u8(0xC0); }
                else if (lo != 0xFFFFU) { emit_u8(0x25); emit_u16(lo); }
                if (hi == 0) { emit_u8(0x33); emit_u8(0xD2); }
                else if (hi != 0xFFFFU) { emit_u8(0x81); emit_u8(0xE2); emit_u16(hi); }
                return;
            }
            if (e->op == Tok::pipe) {
                emit_expr(e->lhs);
                emit_widen_dxax(e->lhs->type);
                if (lo != 0) { emit_u8(0x0D); emit_u16(lo); }
                if (hi != 0) { emit_u8(0x81); emit_u8(0xCA); emit_u16(hi); }
                return;
            }
            if (e->op == Tok::caret) {
                emit_expr(e->lhs);
                emit_widen_dxax(e->lhs->type);
                if (lo != 0) { emit_u8(0x35); emit_u16(lo); }
                if (hi != 0) { emit_u8(0x81); emit_u8(0xF2); emit_u16(hi); }
                return;
            }
            if (e->op == Tok::star) {
                if (uval == 0) {
                    emit_expr(e->lhs);
                    emit_u8(0x33); emit_u8(0xC0);
                    emit_u8(0x33); emit_u8(0xD2);
                    return;
                }
                if (uval == 1) {
                    emit_expr(e->lhs);
                    emit_widen_dxax(e->lhs->type);
                    return;
                }
                const int lg = exact_log2_u32(uval);
                if (lg >= 1) {
                    emit_expr(e->lhs);
                    emit_widen_dxax(e->lhs->type);
                    emit_shl32_dxax_const(static_cast<unsigned>(lg));
                    return;
                }
            }
            if ((e->op == Tok::slash || e->op == Tok::percent) && !is_signed && uval > 0) {
                if (uval == 1) {
                    emit_expr(e->lhs);
                    if (e->op == Tok::slash) {
                        emit_widen_dxax(e->lhs->type);
                    } else {
                        emit_u8(0x33); emit_u8(0xC0);
                        emit_u8(0x33); emit_u8(0xD2);
                    }
                    return;
                }
                const int lg = exact_log2_u32(uval);
                if (lg >= 1) {
                    emit_expr(e->lhs);
                    emit_widen_dxax(e->lhs->type);
                    if (e->op == Tok::slash) {
                        emit_shr32_dxax_const(static_cast<unsigned>(lg), false);
                    } else {
                        const std::uint32_t mask = uval - 1U;
                        const std::uint16_t mlo = static_cast<std::uint16_t>(mask & 0xFFFFU);
                        const std::uint16_t mhi = static_cast<std::uint16_t>((mask >> 16) & 0xFFFFU);
                        if (mlo == 0) { emit_u8(0x33); emit_u8(0xC0); }
                        else if (mlo != 0xFFFFU) { emit_u8(0x25); emit_u16(mlo); }
                        if (mhi == 0) { emit_u8(0x33); emit_u8(0xD2); }
                        else if (mhi != 0xFFFFU) { emit_u8(0x81); emit_u8(0xE2); emit_u16(mhi); }
                    }
                    return;
                }
            }
        }

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
                emit_u8(0x75); emit_u8(0x0B);  // jne +11
                emit_u8(0x3B); emit_u8(0xC3);  // cmp ax, bx
                emit_cmp_setcc(op, false);
                emit_u8(0xEB); emit_u8(0x07);  // jmp +7
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
                        const std::uint16_t cur = static_cast<std::uint16_t>(
                            omf_.code.data[pf.code_offset] |
                            (static_cast<std::uint16_t>(omf_.code.data[pf.code_offset + 1]) << 8));
                        write_u16(omf_.code.data, pf.code_offset,
                                  static_cast<std::uint16_t>(cur + it->second.offset));
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
