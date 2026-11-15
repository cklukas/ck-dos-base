// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
//
// MASM 5.10 compatibility implementation. Encoding and record choices are
// measured against reference-produced objects; behavior outside the verified
// source corpus is refused by name rather than guessed.
#include "dosbase/devtools/masm.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/dos_file.hpp"
#include "dosbase/devtools/omf_record.hpp"

#include <algorithm>
#include <map>
#include <functional>
#include <set>
#include <optional>

namespace dosbase::devtools {
namespace {

// ------------------------------------------------------------------ text
std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

bool ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$' ||
           c == '?' || c == '@';
}
bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }

// ------------------------------------------------------------------ tokens
struct Token {
    enum Kind { ident, number, str, punct, end } kind = end;
    std::string text;   // spelling (idents keep case)
    long value = 0;     // numbers and single-character strings
};

class Lexer {
  public:
    static std::vector<Token> line(std::string_view text, std::string* error) {
        std::vector<Token> tokens;
        std::size_t i = 0;
        int angle_depth = 0;
        char angle_quote = '\0';
        while (i < text.size()) {
            const char c = text[i];
            if (angle_depth > 0 && angle_quote != '\0') {
                if (c == angle_quote) angle_quote = '\0';
            } else if (angle_depth > 0 && (c == '\'' || c == '"')) {
                angle_quote = c;
            }
            if (angle_quote == '\0') {
                if (c == '<') ++angle_depth;
                if (c == '>' && angle_depth > 0) --angle_depth;
            }
            if (c == ' ' || c == '\t' || c == '\f') {  // form feed = page eject
                ++i;
                continue;
            }
            if (c == ';') break;  // comment to end of line
            if ((c == '\'' || c == '"') && angle_depth > 0) {
                // Inside <...> a quote is a plain character (LABEL's
                // `HEADER <... FCB'S ...>`).
                tokens.push_back({Token::punct, std::string(1, c), 0});
                ++i;
                continue;
            }
            if (c == '\'' || c == '"') {
                const char quote = c;
                std::string body;
                ++i;
                // A doubled quote inside the string is that quote
                // (PSDATA.INC's `$P_DOUBLE_QUOTE EQU """"` is one 0x22).
                while (i < text.size()) {
                    if (text[i] == quote) {
                        if (i + 1 < text.size() && text[i + 1] == quote) {
                            body += quote;
                            i += 2;
                            continue;
                        }
                        break;
                    }
                    body += text[i++];
                }
                if (i >= text.size()) {
                    if (error != nullptr) *error = "unterminated string";
                    break;
                }
                ++i;
                Token token{Token::str, body, 0};
                if (body.size() == 1) token.value = static_cast<unsigned char>(body[0]);
                tokens.push_back(std::move(token));
                continue;
            }
            if (ident_start(c)) {
                std::string name;
                while (i < text.size() && ident_char(text[i])) name += text[i++];
                // MASM keeps 31 significant characters -- DISPLAY's
                // msgIncompatibleParametersForHardDisk truncates, merging
                // any spellings that agree on the first 31.
                if (name.size() > 31) name.resize(31);
                tokens.push_back({Token::ident, name, 0});
                continue;
            }
            if (c >= '0' && c <= '9') {
                std::string digits;
                while (i < text.size() && (ident_char(text[i]))) digits += text[i++];
                long value = 0;
                int radix = 10;
                std::string body = upper(digits);
                if (!body.empty() && body.back() == 'H') {
                    radix = 16;
                    body.pop_back();
                } else if (!body.empty() && body.back() == 'B') {
                    radix = 2;
                    body.pop_back();
                } else if (!body.empty() && (body.back() == 'O' || body.back() == 'Q')) {
                    radix = 8;
                    body.pop_back();
                } else if (!body.empty() && body.back() == 'D') {
                    radix = 10;
                    body.pop_back();
                }
                bool malformed = false;
                for (char digit : body) {
                    int d = 0;
                    if (digit >= '0' && digit <= '9') {
                        d = digit - '0';
                    } else if (digit >= 'A' && digit <= 'F') {
                        d = digit - 'A' + 10;
                    } else {
                        malformed = true;
                        break;
                    }
                    if (d >= radix) {
                        malformed = true;
                        break;
                    }
                    value = value * radix + d;
                }
                if (malformed) {
                    // A digit-led token that is no number stays text --
                    // MSBIO2's `IRP AA,<...,0E,...>` only pastes the
                    // items into names (Int19OLD0E). Evaluating one is
                    // still an error, at use.
                    tokens.push_back({Token::ident, digits, 0});
                } else {
                    tokens.push_back({Token::number, digits, value});
                }
                continue;
            }
            tokens.push_back({Token::punct, std::string(1, c), 0});
            ++i;
        }
        tokens.push_back({Token::end, "", 0});
        return tokens;
    }
};

// -------------------------------------------------------------- statements
struct Macro {
    std::vector<std::string> parameters;  // uppercased
    std::vector<std::string> locals;      // uppercased LOCAL names
    std::vector<std::string> body;
};

// One logical source line queue: the main file, INCLUDEs, and macro
// expansions all play through here, re-wound for each pass.
struct Frame {
    std::vector<std::string> lines;
    std::size_t next = 0;
    bool macro_frame = false;          // a macro expansion (MEXIT target)
    std::size_t conditional_base = 0;  // conditionals_ depth at entry
};

// ------------------------------------------------------------------- model
struct Segment {
    std::string name;   // uppercased
    std::string cls;    // uppercased class name
    int align = 3;      // 1 byte, 2 word, 3 para
    int combine = 0;    // private unless the definition says otherwise
    bool absolute = false;      // `SEGMENT AT frame`
    std::uint16_t frame = 0;    // the AT expression's value
    int use_attr = -1;          // -1 default, 0 USE16, 1 USE32 (explicit keyword)
    bool decl_386 = false;      // .386/.386P was active when the segment opened
    // ORG and uninitialized data move the location, so contents are items:
    // literal runs (LEDATA) and iterated blocks (LIDATA -- `dup(value)`
    // emits repeat x { 1 x literal } exactly as OPEN.OBJ shows).
    // Measured off GETCNTRY.OBJ: locat M=1, loc base/offset16, explicit
    // F_SEG frame and T_SEG target with the addend carried in the data.
    struct Fixup {
        std::uint16_t location = 0;  // where the patched word sits
        int loc = 1;                 // 1 offset16, 2 base, 3 pointer
        bool self_relative = false;
        int target_segment = 0;      // 1-based SEGDEF index, or
        int target_extern = 0;       // 1-based EXTDEF index when nonzero, or
        int target_group = 0;        // 1-based GRPDEF index when nonzero
        int frame_segment = 0;       // externs: the declaring segment
        int frame_group = 0;         // group-framed offsets (OFFSET GROUP:x)
        // Code immediates carry the addend in the data (P=1); data words
        // like `dw label` zero the data and carry it here (P=0, measured
        // off SIGHAND.OBJ).
        long explicit_disp = -1;
        // ORG can replace bytes at an existing segment offset. Associate a
        // relocation with the particular data-record run that emitted it so
        // the original and replacement records do not borrow each other's
        // same-offset fixups (INDEDMA's overlaid jump table).
        std::uint32_t sequence = 0;
    };
    struct Item {
        std::uint16_t start = 0;
        std::uint32_t repeat = 0;  // 0: literal run; else single-block LIDATA
        std::vector<std::uint8_t> bytes;
        std::vector<std::uint8_t> lidata;  // pre-encoded block sequence
        // MASM streams data records as it assembles, so records from
        // different segments interleave in source order (_MSGRET's _DATA
        // table precedes every _TEXT record).
        std::uint32_t sequence = 0;
        // Atomic emission units (run-relative [start,end)): the 1024-byte
        // record cap never splits an instruction or a data item -- the
        // whole unit slides into the next record (SORT cuts at 1019 on an
        // instruction boundary; REPLACE at a `dw` fixup word) -- while
        // string bytes split anywhere (DEBUASM's full 1020).
        std::vector<std::pair<std::uint32_t, std::uint32_t>> atoms;
        // A relocatable struc-instance override (COMPSM's MSGNUM_BAD
        // <6,,SUBLIST_6,...>) rides a FIXUPP paired with the LIDATA whose
        // data-record offset counts into the record's block image.
        std::vector<Fixup> lidata_fixups;
    };
    std::vector<Item> runs;           // pass 2
    std::vector<Fixup> fixups;
    std::uint16_t location = 0;       // both passes
    std::uint16_t extent = 0;         // SEGDEF length
};

struct Struc {
    std::uint16_t size = 0;
    std::map<std::string, std::uint16_t> fields;  // uppercased -> offset
    // Instances emit per field: initialized fields produce bytes,
    // uninitialized (`?`, `dup(?)`) only occupy space -- F_FIRST.OBJ's
    // data segment holds 18 emitted bytes out of hundreds reserved.
    struct Piece {
        std::uint32_t repeat = 1;             // dup count; 1 for scalars
        std::vector<std::uint8_t> once;       // one iteration, `?` as zeros
        // A multi-item field (`db 0DH,0AH`) nests one block per item.
        std::vector<std::vector<std::uint8_t>> items;
    };
    std::vector<Piece> pieces;
    // Indices into `pieces` whose scalar value is `TYPE selfname`/`SIZE
    // selfname` -- a self-describing length header field (CPSFONT3's
    // `FTB_LENGTH DW TYPE FTBLK` inside `FTBLK STRUC`). The struc's own
    // size isn't known until ENDS, so these are backpatched there.
    std::vector<std::size_t> self_size_patches;
};

struct Symbol {
    enum Kind { label, constant, struc_type } kind = constant;
    std::string spelling;  // as defined (for -Mx emission)
    int segment = -1;      // labels: segment index
    long value = 0;        // offset or constant
    bool far_label = false;
    bool defined = false;
    int size = 0;          // data labels: 1/2/4 from the defining directive
    int extern_index = 0;  // 1-based EXTDEF index for EXTRN symbols
    int pub_group = 0;
    // Declaration-time CS/DS-ASSUMEd SEGMENT (0 none, else 1-based):
    // frames an extern declared outside any segment (FORINIT's
    // Main_Routine under CS:code vs FORPROC's Display_Interface with no
    // ASSUME at all).
    int pub_segment = 0;
    // `EXTRN name:ABS`: an absolute external -- uses are immediates
    // carrying an F_EXT fixup (DEBASM's `CMP CX,MAXOP`).
    bool abs_extern = false;     // 1-based GRPDEF index for the PUBDEF record
    // LENGTH * TYPE for a data label with a leading DUP: TREE's
    // `SIZE FIX_DTA_FILN` (a `DB 13 DUP(?)`) is 13, not its offset.
    long size_total = 0;
    // A STRUC member: its value is an offset within the type, so a bare use
    // is that offset as an immediate and `field.field` sums the two offsets
    // (INDEINI's `MOV [BX].ETSS_LDT,SCRUBBER.VM_LDTR`). Distinguishes it from
    // an EQU/extern-alias constant, whose `.field` is a memory reference.
    bool struc_field = false;
    // This label aliases an EXTRN that was still undeclared at the EQU. MASM
    // sizes a byte store through it as the pass-1 word form, then emits the
    // pass-2 byte form plus a NOP (KEYBI48's `SD EQU SHARED_DATA`).
    bool late_extern_alias = false;
    // An EQU may retain an OFFSET relocation instead of becoming absolute:
    // VMTRAP's `VMT_STACK EQU OFFSET STACK:kstack_top - 40` carries both the
    // external target and STACK frame into each immediate use.
    int constant_fixup_extern = 0;
    int constant_frame_segment = 0;
    int constant_frame_group = 0;
    // This constant, or a constant it depends on, was unresolved when its
    // EQU ran in pass 1. MASM preserves the pass-1 operand reservation even
    // after the chain has a concrete value in pass 2 (RESCODE's
    // adjustment_in_paragraphs).
    bool forward_derived_constant = false;
};

const std::map<std::string, int>& reg16() {
    static const std::map<std::string, int> table{{"AX", 0}, {"CX", 1}, {"DX", 2}, {"BX", 3},
                                                  {"SP", 4}, {"BP", 5}, {"SI", 6}, {"DI", 7}};
    return table;
}
const std::map<std::string, int>& reg8() {
    static const std::map<std::string, int> table{{"AL", 0}, {"CL", 1}, {"DL", 2}, {"BL", 3},
                                                  {"AH", 4}, {"CH", 5}, {"DH", 6}, {"BH", 7}};
    return table;
}
const std::map<std::string, int>& reg32() {
    // 386 dword registers; encoded like reg16 with a 66 operand-size prefix in
    // the 16-bit segments the tree targets (MEMM's `PUSH EAX`).
    static const std::map<std::string, int> table{{"EAX", 0}, {"ECX", 1}, {"EDX", 2}, {"EBX", 3},
                                                  {"ESP", 4}, {"EBP", 5}, {"ESI", 6}, {"EDI", 7}};
    return table;
}
const std::map<std::string, int>& creg() {
    // 386 control/debug/test registers, reached only by MOV (0F 20-23)
    // (MEMM's `mov eax,cr3`).
    static const std::map<std::string, int> table{
        {"CR0", 0}, {"CR2", 2}, {"CR3", 3}, {"CR4", 4}};
    return table;
}
const std::map<std::string, int>& sreg() {
    static const std::map<std::string, int> table{
        {"ES", 0}, {"CS", 1}, {"SS", 2}, {"DS", 3}, {"FS", 4}, {"GS", 5}};
    return table;
}

struct Operand {
    enum Kind { none, r16, r8, seg, imm, mem, lab } kind = none;
    int reg = 0;          // register number / segment register
    long value = 0;       // immediate / displacement / label offset
    bool value_known = true;
    // memory addressing
    bool has_base_bx = false, has_base_bp = false, has_si = false, has_di = false;
    int base32 = -1;      // 0..7 for 32-bit base register [EAX..EDI]
    int size = 0;         // 0 unknown, 1 byte, 2 word (from ptr or register)
    // A 386 dword operand in a 16-bit segment: the encoding is the ordinary
    // 16-bit form behind a 66 operand-size prefix, with EAX..EDI riding the
    // same reg numbers as AX..DI and immediates widened to four bytes.
    bool wide = false;
    // A direct reference whose home segment has USE32 addressing carries a
    // 67 prefix and disp32 even while the containing code segment is USE16
    // (EM286LL's references into the absolute L286BUFF segment).
    bool address_wide = false;
    int segment_override = -1;
    // A struc-field displacement keeps the 16-bit form even when tiny
    // (DEBCOM3's `DEC WORD PTR [BP].OLDIP` is FF 8E 02 00).
    bool disp_symbolic = false;
    // Pass 2 resolved a constant that pass 1 sized as forward memory:
    // the emitter pads with NOP (EDLIN's `MOV CX,NUMCOM` = B9 0F 00 90).
    bool forward_const = false;
    // The immediate came through the OFFSET operator, so MASM sizes it as a
    // word even when it folds to a small absolute constant (a struc field's
    // offset, or `offset A - offset B`): the 83/6A byte forms are out
    // (GRAPHICS' `add bx,offset SELECT_ESC` = 81, EM386LL's label diffs).
    bool from_offset = false;
    bool defined_at_use = false;  // lab: definition precedes the use
    int fixup_loc = 0;            // 0 none, 1 offset16, 2 base, 3 pointer
    int fixup_group = 0;          // 1-based GRPDEF index (group immediates)
    int frame_group = 0;          // OFFSET GROUP:label frames through the group
    int frame_segment = 0;        // OFFSET SEGNAME:label frames the named
                                  // segment (FORLABEL's `offset data:MsgCRLF`
                                  // on an extern target)
    bool forward_marked = false;  // label relocated by a forward JMP/CALL
    bool far_override = false;    // FAR PTR prefix on the operand
    bool size_explicit = false;   // size came from an explicit BYTE/WORD PTR
    int home_segment = 0;         // the symbol's (or extern's declaring) segment
    int prefix_group = 0;         // `RESGROUP:label` prefix on the operand
                                  // (COMMAND1's `JMP RESGROUP:CONPROC`)
    bool bracket_field = false;   // a `.field` selector rode inside the
                                  // brackets (RUCODE's [$M_RT.$M_RETURN_ADDR]
                                  // stays memory even over a constant base)
    bool lazy_base = false;       // the base symbol was an unknown-probe
                                  // constant (pass 1 of `$M_RT.field`)
    bool forward_ref = false;     // the label, or an EQU's external target, was
                                  // unknown at its pass-1 definition; a `MOV
                                  // mem,imm` store reserved the word form and
                                  // pass 2 pads a byte store with a NOP
    int fixup_segment = 0;        // target SEGDEF index (1-based)
    int fixup_extern = 0;         // target EXTDEF index (1-based)
    std::string label_name;       // lab: symbol reference
};

// -------------------------------------------------------------- assembler
class Assembler {
  public:
    Assembler(std::string spelling, MasmOptions options)
        : source_spelling_(std::move(spelling)), options_(std::move(options)) {}

    MasmResult run() {
        const std::filesystem::path source = resolve_dos_input(source_spelling_);
        std::vector<std::uint8_t> raw;
        try {
            raw = read_file(source.string());
        } catch (const std::exception& error) {
            return {{}, {error.what()}, {}};
        }
        root_lines_ = split_source(raw);
        for (pass_ = 1; pass_ <= 2 && errors_.empty(); ++pass_) {
            begin_pass();
            if (pass_ == 2 && !externs_.empty()) {
                // EXTDEF emits in name-table order, and FIXUPP indices
                // follow the emitted order.
                table_order(externs_);
                for (std::size_t i = 0; i < externs_.size(); ++i) {
                    const auto symbol = symbols_.find(upper(externs_[i]));
                    if (symbol != symbols_.end()) {
                        symbol->second.extern_index = static_cast<int>(i) + 1;
                    }
                }
                extern_sequences_.assign(externs_.size(), 0);
            }
            if (pass_ == 2 && groups_.size() > 1) {
                // GRPDEF records emit in name-table order, not declaration
                // order (_REPLACE declares PGROUP first but DGROUP, the
                // lower-hashing name, is group 1). Sorting groups_ here
                // keeps the fixup group indices computed in pass 2 aligned
                // with the emitted order.
                std::vector<std::string> names;
                for (const Group& group : groups_) names.push_back(group.name);
                table_order(names);
                std::stable_sort(
                    groups_.begin(), groups_.end(),
                    [&names](const Group& a, const Group& b) {
                        const auto rank = [&names](const std::string& n) {
                            return std::find(names.begin(), names.end(), n) -
                                   names.begin();
                        };
                        return rank(a.name) < rank(b.name);
                    });
            }
            frames_.push_back({root_lines_, 0});
            while (!frames_.empty() && !ended_ && errors_.empty()) {
                if (frames_.back().next >= frames_.back().lines.size()) {
                    frames_.pop_back();
                    continue;
                }
                const std::string line = frames_.back().lines[frames_.back().next++];
                process(line);
            }
        }
        if (std::getenv("DOSBASE_MASM_DEBUG") != nullptr) {
            std::fprintf(stderr, "pass-end locals=%u forward=%zu\n",
                         local_counter_, forward_labels_.size());
        }
        MasmResult result;
        result.errors = errors_;
        result.console = console_;
        if (errors_.empty()) result.object = emit();
        return result;
    }

  private:
    // ------------------------------------------------------------ sources
    static std::vector<std::string> split_source(const std::vector<std::uint8_t>& raw) {
        std::vector<std::string> lines;
        std::string current;
        for (std::uint8_t byte : raw) {
            if (byte == '\n') {
                lines.push_back(current);
                current.clear();
            } else if (byte != '\r' && byte != 0x1A) {
                current += static_cast<char>(byte);
            }
        }
        if (!current.empty()) lines.push_back(current);
        return lines;
    }

    void begin_pass() {
        ended_ = false;
        frames_.clear();
        if (pass_ == 1) {
            segments_order_.clear();
            segments_.clear();
            name_insertions_.clear();
            groups_.clear();
            forward_labels_.clear();
        } else {
            // Segments and groups persist so forward references
            // (psdata_seg:label before the segment opens) resolve; only
            // the per-pass content state resets.
            for (Segment& segment : segments_) {
                segment.location = 0;
                segment.extent = 0;
                segment.runs.clear();
                segment.fixups.clear();
            }
            emit_sequence_ = 0;
        }
        // note_fixup is a no-op in pass 1 and leaves the hint set, so a
        // stale frame must not leak into pass 2's first fixup (TBATCH's
        // first TRANCODE fixup is the module's first CALL).
        pending_frame_group_ = 0;
        pending_frame_segment_ = 0;
        strucs_.clear();
        records_.clear();
        record_fields_.clear();
        // Macros persist across passes: DCOPYP invokes HEADER four lines
        // before defining it, and pass 2 resolves the early call through
        // the pass-1 definition. Redefinition simply overwrites.
        conditionals_.clear();
        defined_this_pass_.clear();
        // publics_ persists into pass 2 so an IF1-guarded PUBLIC (declared
        // in pass 1 only, like CHKPRMT's pathlabl $$A markers) survives;
        // the PUBLIC directive dedups, so the normal both-pass case is fine.
        if (pass_ == 1) publics_.clear();
        open_struc_.clear();
        current_segment_ = -1;
        segment_stack_.clear();
        proc_far_ = false;
        proc_far_stack_.clear();
        assumes_[0] = assumes_[1] = assumes_[2] = assumes_[3] = -1;
        assume_group_[0] = assume_group_[1] = assume_group_[2] = assume_group_[3] = 0;
        sticky_assumes_[0] = sticky_assumes_[1] = sticky_assumes_[2] =
            sticky_assumes_[3] = -1;
        collecting_macro_ = false;
        collecting_block_ = false;
        macro_depth_ = 0;
        block_depth_ = 0;
        comment_delimiter_ = '\0';
        if (pass_ == 1) {
            symbols_.clear();
            for (const auto& [def_name, def_val] : options_.defines) {
                Symbol sym;
                sym.kind = Symbol::constant;
                sym.spelling = def_name;
                sym.value = static_cast<long>(def_val);
                sym.defined = true;
                symbols_[upper(def_name)] = std::move(sym);
            }
            externs_.clear();
            extern_sequences_.clear();
            text_equates_.clear();
        }
        deferred_alias_.clear();
        if (pass_ == 2 && std::getenv("DOSBASE_MASM_DEBUG") != nullptr) {
            std::fprintf(stderr, "pass1-end locals=%u\n", local_counter_);
        }
        local_counter_ = 0;
        // pass 2 keeps the reordered externs_
        for (auto& [name, symbol] : symbols_) symbol.defined = false;
    }

    void fail(const std::string& message) {
        errors_.push_back(current_line_.empty() ? message
                                                : message + " | " + current_line_);
    }

    // --------------------------------------------------------- statements
    void process(const std::string& text) {
        if (comment_delimiter_ != '\0') {
            if (text.find(comment_delimiter_) != std::string::npos) comment_delimiter_ = '\0';
            return;
        }
        current_line_ = text;
        // Directives whose argument is free text (%OUT, TITLE, SUBTTL,
        // PAGE, NAME) take the rest of the line literally, apostrophes and
        // all -- lexing them as operands would choke on `Don't` (MSBOOT's
        // %OUT). When alive, consume them before the operand lexer runs.
        if (conditional_active() && !collecting_macro_ && !collecting_block_) {
            std::size_t s = 0;
            while (s < text.size() && (text[s] == ' ' || text[s] == '\t')) ++s;
            std::size_t e = s;
            while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) ||
                                       text[e] == '%' || text[e] == '_')) {
                ++e;
            }
            std::string head = upper(text.substr(s, e - s));
            if (head == "%OUT") {
                std::string message = trim(text.substr(e));
                console_.push_back(message);
                return;
            }
            if (head == "TITLE" || head == "SUBTTL" || head == "PAGE" ||
                head == "NAME") {
                return;  // listing-only, no object effect
            }
        }
        std::string lex_error;
        std::vector<Token> tokens = Lexer::line(text, &lex_error);
        if (!lex_error.empty()) {
            // A line that only exists inside a dead conditional need not
            // lex (MSBOOT's `%OUT Don't destroy...` under a false IF); the
            // partial token list still carries the IF/ENDIF keywords the
            // suppressed path scans for.
            if (!conditional_active() || collecting_macro_ || collecting_block_) {
                lex_error.clear();
            } else {
                return fail(lex_error + ": " + text);
            }
        }
        // The subject of an EQU/= definition is not text-expanded -- on a
        // later pass the name already aliases its own body, and expanding
        // it would re-parse `buf_offset EQU DWORD PTR ...` as a DWORD line.
        std::size_t protect_from = 0;
        if (tokens.size() >= 2 && tokens[0].kind == Token::ident &&
            ((tokens[1].kind == Token::ident && upper(tokens[1].text) == "EQU") ||
             (tokens[1].kind == Token::punct && tokens[1].text == "="))) {
            protect_from = 1;
        }
        // A macro definition's parameter list keeps its formal names, and
        // a body being collected stays raw -- pass-1 parm equates like
        // ITOUPPER's `c` must not splice into pass 2's re-collection of
        // cmacros' `?pg macro n,p,c,a`.
        const bool macro_definition =
            tokens.size() >= 2 && tokens[0].kind == Token::ident &&
            tokens[1].kind == Token::ident && upper(tokens[1].text) == "MACRO";
        if (collecting_macro_ || collecting_block_ || macro_definition) {
            protect_from = tokens.size();
        }
        for (int round = 0; round < 4; ++round) {
            bool changed = false;
            for (std::size_t k = protect_from; k < tokens.size(); ++k) {
                if (tokens[k].kind != Token::ident) continue;
                const auto equate = text_equates_.find(upper(tokens[k].text));
                if (equate == text_equates_.end()) continue;
                std::string splice_error;
                std::vector<Token> spliced = Lexer::line(equate->second, &splice_error);
                if (!spliced.empty()) spliced.pop_back();  // drop end marker
                // MASM's bracketed register EQU is shadowed when the
                // immediate bracket suffix contains TYPE: `parm_list EQU
                // [BX]` followed by `parm_list[DI+TYPE entry]` addresses only
                // DI. Without TYPE the alias combines, as do literal adjacent
                // brackets (`[BX][SI]`), so preserve the distinction here
                // while the equate's origin is visible (BRALIAS.OBJ;
                // MODEPARS's parm_list tables).
                bool suffix_has_type = false;
                if (k + 1 < tokens.size() &&
                    tokens[k + 1].kind == Token::punct &&
                    tokens[k + 1].text == "[") {
                    for (std::size_t look = k + 2;
                         look < tokens.size() &&
                         !(tokens[look].kind == Token::punct &&
                           tokens[look].text == "]") &&
                         tokens[look].kind != Token::end;
                         ++look) {
                        if (tokens[look].kind == Token::ident &&
                            upper(tokens[look].text) == "TYPE") {
                            suffix_has_type = true;
                        }
                    }
                }
                const bool shadowed_bracket_alias =
                    k + 1 < tokens.size() &&
                    tokens[k + 1].kind == Token::punct &&
                    tokens[k + 1].text == "[" && suffix_has_type &&
                    spliced.size() == 3 &&
                    spliced[0].kind == Token::punct &&
                    spliced[0].text == "[" &&
                    spliced[1].kind == Token::ident &&
                    reg16().contains(upper(spliced[1].text)) &&
                    spliced[2].kind == Token::punct &&
                    spliced[2].text == "]";
                // A struc NAME inside the spliced equate body is its base,
                // zero -- IFSSYM chains `LENGTH_CHDIR EQU IFSR_NAME@-IFSRH+4`
                // through a DWORD-PTR text equate, so the bare IFSRH only
                // reaches a use site here, never the EQU-time zeroing. Guard
                // the leading token with the operator that precedes the splice
                // so `SIZE equate` still measures the type.
                if (!(k > 0 && tokens[k - 1].kind == Token::ident &&
                      [&] {
                          const std::string p = upper(tokens[k - 1].text);
                          return p == "SIZE" || p == "TYPE" || p == "LENGTH" ||
                                 p == "WIDTH" || p == "MASK" || p == "OFFSET";
                      }())) {
                    zero_bare_struc_names(spliced);
                }
                tokens.erase(tokens.begin() + static_cast<long>(k));
                if (!shadowed_bracket_alias) {
                    tokens.insert(tokens.begin() + static_cast<long>(k),
                                  spliced.begin(), spliced.end());
                }
                changed = true;
                break;
            }
            if (!changed) break;
        }
        std::size_t at = 0;
        if (tokens[at].kind == Token::end) return;
        // `.sall` and `%out` lex as punctuation + name; fold each into one
        // directive token.
        if (tokens[at].kind == Token::punct &&
            (tokens[at].text == "." || tokens[at].text == "%") &&
            tokens[at + 1].kind == Token::ident) {
            tokens[at + 1].text = tokens[at].text + tokens[at + 1].text;
            tokens.erase(tokens.begin() + static_cast<long>(at));
        } else if (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                   tokens[at + 1].kind == Token::number) {
            // A CPU/FPU select directive lexes as `.` + number (.8087,
            // .286): fold it into one directive token.
            tokens[at + 1].kind = Token::ident;
            tokens[at + 1].text = "." + tokens[at + 1].text;
            tokens.erase(tokens.begin() + static_cast<long>(at));
        }

        // Conditional handling first: only IF/ELSE/ENDIF are alive when
        // suppressed.
        const std::string first = upper(tokens[at].text);
        if (!conditional_active()) {
            if (first == "IF1" || first == "IF2" || first == "IFDEF" || first == "IFNDEF" ||
                first == "IF" || first == "IFE" || first == "IFB" || first == "IFNB" ||
                first == "IFIDN" || first == "IFDIF") {
                conditionals_.push_back(false);
            } else if (first == "ELSE" && !conditionals_.empty()) {
                conditionals_.back() = !conditionals_.back();
            } else if (first == "ENDIF" && !conditionals_.empty()) {
                conditionals_.pop_back();
            }
            return;
        }

        // A block keyword counts only in directive position -- the first
        // token (ENDM/REPT/IRP/IRPC) or the second after a name (MACRO) --
        // never inside operand text (GETMSG passes the word 'Macro' inside
        // a <...> message argument).
        const auto block_keyword = [&tokens]() -> std::string {
            if (tokens[0].kind == Token::ident) {
                const std::string folded = upper(tokens[0].text);
                if (folded == "ENDM" || folded == "REPT" || folded == "IRP" ||
                    folded == "IRPC" || folded == "MACRO") {
                    return folded;
                }
                if (tokens[1].kind == Token::ident && upper(tokens[1].text) == "MACRO") {
                    return "MACRO";
                }
            }
            return "";
        };

        // REPT/IRP/IRPC block collection: the body plays back expanded.
        if (collecting_block_) {
            const std::string word = block_keyword();
            if (word == "ENDM" && block_depth_ == 0) {
                collecting_block_ = false;
                expand_block();
            } else {
                if (word == "MACRO" || word == "REPT" || word == "IRP" || word == "IRPC") {
                    ++block_depth_;
                } else if (word == "ENDM") {
                    --block_depth_;
                }
                block_body_.push_back(text);
            }
            return;
        }

        // Macro definition body collection happens verbatim; nested
        // MACRO/REPT/IRP blocks keep their ENDMs.
        if (collecting_macro_) {
            const std::string word = block_keyword();
            if (word == "ENDM" && macro_depth_ == 0) {
                macros_[pending_macro_name_] = pending_macro_;
                collecting_macro_ = false;
            } else {
                bool is_local = false;
                if (macro_depth_ == 0 && pending_macro_.body.empty()) {
                    // leading LOCAL lines declare generated labels
                    for (const Token& token : tokens) {
                        if (token.kind != Token::ident) break;
                        if (upper(token.text) == "LOCAL") {
                            is_local = true;
                        }
                        break;
                    }
                }
                if (is_local) {
                    for (const Token& token : tokens) {
                        if (token.kind == Token::ident && upper(token.text) != "LOCAL") {
                            pending_macro_.locals.push_back(upper(token.text));
                        }
                    }
                    return;
                }
                if (word == "MACRO" || word == "REPT" || word == "IRP" || word == "IRPC") {
                    ++macro_depth_;
                } else if (word == "ENDM") {
                    --macro_depth_;
                }
                pending_macro_.body.push_back(text);
            }
            return;
        }

        // Label / name field.
        std::string name;
        std::string name_spelling;
        if (tokens[at].kind == Token::punct && tokens[at].text == ":") {
            // an empty label -- a macro expanded `&label&:` with no argument
            ++at;
            if (tokens[at].kind == Token::end) return;
        }
        if (tokens[at].kind == Token::ident && tokens[at + 1].kind == Token::punct &&
            tokens[at + 1].text == ":") {
            define_label(tokens[at].text, false);
            at += 2;
            if (tokens[at].kind == Token::end) return;
        } else if (tokens[at].kind == Token::ident && tokens[at + 1].kind == Token::ident) {
            const std::string second = upper(tokens[at + 1].text);
            static const std::map<std::string, int> naming{
                {"SEGMENT", 0}, {"ENDS", 0}, {"PROC", 0},  {"ENDP", 0}, {"STRUC", 0},
                {"MACRO", 0},   {"EQU", 0},  {"LABEL", 0}, {"DB", 0},   {"DW", 0},
                {"DD", 0},      {"DQ", 0},   {"DT", 0},    {"GROUP", 0},
                {"RECORD", 0}};
            // `IFNDEF A_BPB` is a conditional, not a label + struc-type
            // instance (IOCTL.INC probes the struc's existence by name).
            static const std::set<std::string> never_labels{
                "IF", "IF1", "IF2", "IFE", "IFDEF", "IFNDEF", "IFB",
                "IFNB", "IFIDN", "IFDIF", "ELSE", "ENDIF", "PURGE",
                "PUBLIC", "EXTRN", "INCLUDE"};
            if ((naming.contains(second) ||
                 strucs_.contains(second)) &&
                // `INCLUDE STRUC.INC` is an include of STRUC.INC, never
                // a STRUC definition named INCLUDE (MORE's structured
                // macros vanished behind exactly that misparse).
                !never_labels.contains(upper(tokens[at].text))) {
                name_spelling = tokens[at].text;
                name = upper(name_spelling);
                ++at;
            }
        } else if (tokens[at].kind == Token::ident && tokens[at + 1].kind == Token::punct &&
                   tokens[at + 1].text == "=") {
            // `symbol = expression` -- the redefinable equate.
            name_spelling = tokens[at].text;
            name = upper(name_spelling);
            ++at;
        }

        // A dotted directive after a label -- `CUP: .IF <cond>` (ANSI's
        // structured code) -- folds the same way one at the statement start
        // does. The leading fold ran before the label was consumed, so the
        // `.` + name pair still needs joining here or `.IF` dispatches as the
        // bare `.` punct and its macro never fires, leaving the structure open.
        if (tokens[at].kind == Token::punct && tokens[at].text == "." &&
            tokens[at + 1].kind == Token::ident) {
            tokens[at + 1].text = "." + tokens[at + 1].text;
            tokens.erase(tokens.begin() + static_cast<long>(at));
        }
        if (tokens[at].kind == Token::end) return;
        const std::string op = tokens[at].kind == Token::punct && tokens[at].text == "="
                                   ? "="
                                   : upper(tokens[at].text);
        std::vector<Token> rest(tokens.begin() + static_cast<long>(at) + 1, tokens.end());

        if (directive(name, name_spelling, op, rest, text)) return;

        // Macro invocation?
        const auto macro = macros_.find(op);
        if (macro != macros_.end()) return invoke_macro(macro->second, op, text);

        // STRUC instance allocation: `[label] strucname <...>`.
        if (const auto type = strucs_.find(op); type != strucs_.end()) {
            if (!name_spelling.empty()) define_data_label(name_spelling, 0);
            if (current_segment_ < 0) return fail("struc instance outside any segment");
            // Positional overrides: `X strucname <a,,b>` replaces the
            // matching fields' single iteration.
            std::vector<std::string> overrides;
            {
                const std::size_t open = text.find('<');
                if (open != std::string::npos) {
                    const std::size_t close = text.rfind('>');
                    const std::string inside =
                        close != std::string::npos && close > open
                            ? text.substr(open + 1, close - open - 1)
                            : "";
                    if (!trim(inside).empty()) {
                        overrides = split_struc_overrides(inside);
                    }
                }
            }
            // `N dup(<...>)` repeats the whole instance as an outer
            // LIDATA block (MSBIO2's `BDSMs BDSM_type
            // Max_mini_dsk_num dup (<>)` -- 23 x 32 nested blocks).
            std::uint32_t instances = 1;
            {
                std::size_t probe = 0;
                if (long repeat = 0; dup_count(rest, probe, &repeat) &&
                                     repeat > 0) {
                    instances = static_cast<std::uint32_t>(repeat);
                }
            }
            std::uint32_t top_blocks = 0;
            std::vector<std::uint8_t> blob;
            std::vector<Segment::Fixup> item_fixups;
            std::uint32_t space = 0;
            const auto word16 = [&blob](std::uint32_t v) {
                blob.push_back(static_cast<std::uint8_t>(v & 0xFF));
                blob.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
            };
            std::size_t field_index = 0;
            for (const Struc::Piece& original : type->second.pieces) {
                Struc::Piece piece = original;
                bool reloc_field = false;
                if (field_index < overrides.size() && !overrides[field_index].empty()) {
                    std::string override_error;
                    std::vector<Token> override_tokens =
                        Lexer::line(overrides[field_index], &override_error);
                    std::vector<std::uint8_t> replaced;
                    if (override_tokens.size() >= 2 &&
                        override_tokens[0].kind == Token::str &&
                        override_tokens[1].kind == Token::end) {
                        // A string override fills character-wise from the
                        // left; the field default's tail stays (FIND's
                        // qea_name <"CP"> keeps six of its eight blanks).
                        const std::string& chars = override_tokens[0].text;
                        replaced.assign(chars.begin(), chars.end());
                    } else {
                        // A lone label override (COMPMS's `MSG_DESC
                        // <6,,SUBLIST_6,ONE_SUBS>`), or an explicit OFFSET
                        // label (GRPATTRN's `TAB_ENTRY <OFFSET PAT_4X2,...>`),
                        // zeros the word and
                        // rides a FIXUPP on the LIDATA record, its offset
                        // counted into the record's block image (COMPSM's
                        // 9C at 0013 -> CSEG:006f).
                        std::size_t label_at = 0;
                        const bool label_override =
                            override_tokens.size() >= 2 &&
                            override_tokens[0].kind == Token::ident &&
                            override_tokens[1].kind == Token::end;
                        const bool offset_label_override =
                            override_tokens.size() >= 3 &&
                            override_tokens[0].kind == Token::ident &&
                            upper(override_tokens[0].text) == "OFFSET" &&
                            override_tokens[1].kind == Token::ident &&
                            override_tokens[2].kind == Token::end;
                        if (offset_label_override) label_at = 1;
                        if (piece.repeat == 1 && piece.items.size() <= 1 &&
                            !piece.once.empty() &&
                            (label_override || offset_label_override)) {
                            const auto symbol =
                                symbols_.find(upper(override_tokens[label_at].text));
                            if (symbol != symbols_.end() &&
                                symbol->second.kind == Symbol::label &&
                                (symbol->second.extern_index > 0 ||
                                 symbol->second.segment >= 0)) {
                                // A dword field takes a pointer32
                                // (SYSINIT1's IFSR_PARMS@ overrides).
                                const int loc = piece.once.size() == 4 ? 3 : 1;
                                if (symbol->second.extern_index > 0) {
                                    note_fixup(loc, 0, symbol->second.extern_index,
                                               false, -1);
                                } else {
                                    note_fixup(loc, symbol->second.segment + 1, 0,
                                               false, symbol->second.value);
                                }
                                reloc_field = pass_ == 2;
                                replaced.assign(piece.once.size(), 0);
                            }
                        }
                        if (replaced.empty()) {
                            replaced = data_bytes(
                                static_cast<int>(piece.once.size()), override_tokens);
                        }
                    }
                    for (std::size_t b = replaced.size(); b < piece.once.size();
                         ++b) {
                        replaced.push_back(piece.once[b]);
                    }
                    replaced.resize(piece.once.size());
                    piece.once = std::move(replaced);
                }
                ++field_index;
                ++top_blocks;
                if (piece.repeat > 1) {
                    word16(piece.repeat);
                    word16(1);
                    word16(1);
                    word16(0);
                    blob.push_back(static_cast<std::uint8_t>(piece.once.size()));
                    blob.insert(blob.end(), piece.once.begin(), piece.once.end());
                } else if (piece.items.size() > 1 &&
                           (field_index - 1 >= overrides.size() ||
                            overrides[field_index - 1].empty())) {
                    // One nested block per comma item ($M_CRLF's 0DH,0AH).
                    word16(1);
                    word16(static_cast<std::uint32_t>(piece.items.size()));
                    for (const auto& item : piece.items) {
                        word16(1);
                        word16(0);
                        blob.push_back(static_cast<std::uint8_t>(item.size()));
                        blob.insert(blob.end(), item.begin(), item.end());
                    }
                } else {
                    word16(1);
                    word16(0);
                    blob.push_back(static_cast<std::uint8_t>(piece.once.size()));
                    if (reloc_field && !segment().fixups.empty()) {
                        Segment::Fixup fixup = segment().fixups.back();
                        segment().fixups.pop_back();
                        fixup.location = static_cast<std::uint16_t>(blob.size());
                        item_fixups.push_back(std::move(fixup));
                    }
                    blob.insert(blob.end(), piece.once.begin(), piece.once.end());
                }
                space += piece.repeat * static_cast<std::uint32_t>(piece.once.size());
            }
            if (instances > 1) {
                std::vector<std::uint8_t> outer;
                outer.push_back(static_cast<std::uint8_t>(instances & 0xFF));
                outer.push_back(static_cast<std::uint8_t>((instances >> 8) & 0xFF));
                outer.push_back(static_cast<std::uint8_t>(top_blocks & 0xFF));
                outer.push_back(static_cast<std::uint8_t>((top_blocks >> 8) & 0xFF));
                outer.insert(outer.end(), blob.begin(), blob.end());
                blob = std::move(outer);
                for (Segment::Fixup& fixup : item_fixups) fixup.location += 4;
                space *= instances;
            }
            Segment& home = segment();
            if (pass_ == 2) {
                Segment::Item item;
                item.start = home.location;
                item.lidata = std::move(blob);
                item.sequence = ++emit_sequence_;
                item.lidata_fixups = std::move(item_fixups);
                home.runs.push_back(std::move(item));
            }
            home.location = static_cast<std::uint16_t>(home.location + space);
            home.extent = std::max(home.extent, home.location);
            return;
        }

        encode_instruction(op, rest);
    }

    bool conditional_active() const {
        return std::all_of(conditionals_.begin(), conditionals_.end(), [](bool b) { return b; });
    }

    // --------------------------------------------------------- directives
    bool directive(const std::string& name, const std::string& name_spelling,
                   const std::string& op, std::vector<Token>& rest, const std::string& text) {
        if (op == "IF1") return conditionals_.push_back(pass_ == 1), true;
        if (op == "IF2") return conditionals_.push_back(pass_ == 2), true;
        if (op == "IFDEF")
            return conditionals_.push_back(rest[0].kind == Token::ident &&
                                           symbols_.contains(upper(rest[0].text))),
                   true;
        if (op == "IFNDEF")
            return conditionals_.push_back(!(rest[0].kind == Token::ident &&
                                             symbols_.contains(upper(rest[0].text)))),
                   true;
        if (op == "IF") return conditionals_.push_back(expression(rest).value != 0), true;
        if (op == "IFE") return conditionals_.push_back(expression(rest).value == 0), true;
        if (op == "IFB" || op == "IFNB" || op == "IFIDN" || op == "IFDIF") {
            std::size_t where = text.find_first_not_of(" \t");
            where = text.find_first_of(" \t", where);
            const std::string operands =
                where == std::string::npos ? "" : text.substr(where);
            std::vector<std::string> parts = split_macro_arguments(operands);
            const auto strip = [](std::string s) {
                // `<...>` delimits the argument; `<>` IS blank (ATTRIBA's
                // CENTER with an empty NAMELIST).
                if (s.size() >= 2 && s.front() == '<' && s.back() == '>') {
                    s = s.substr(1, s.size() - 2);
                }
                return s;
            };
            const std::string a = strip(parts.size() > 0 ? parts[0] : "");
            const std::string b = strip(parts.size() > 1 ? parts[1] : "");
            bool truth = false;
            if (op == "IFB") truth = a.empty();
            else if (op == "IFNB") truth = !a.empty();
            else if (op == "IFIDN") truth = a == b;
            else truth = a != b;
            conditionals_.push_back(truth);
            return true;
        }
        if (op == "ELSE") {
            if (conditionals_.empty()) fail("ELSE without IF");
            else conditionals_.back() = !conditionals_.back();
            return true;
        }
        if (op == "ENDIF") {
            if (conditionals_.empty()) fail("ENDIF without IF");
            else conditionals_.pop_back();
            return true;
        }
        if (op == "PAGE" || op == "TITLE" || op == "SUBTTL" || op == "NAME" || op == ".SALL" ||
            op == ".XALL" || op == ".LALL" || op == ".LIST" || op == ".XLIST" ||
            op == ".CREF" || op == ".XCREF" || op == "EVEN" ||
            // CPU/FPU selection and listing controls: no effect on the
            // emitted object (the pinned tree targets the 8086/8087).
            op == ".8086" || op == ".8087" || op == ".186" || op == ".286" ||
            op == ".286P" || op == ".286C" || op == ".287" || op == ".386" ||
            // (the 186+ selects below also unlock imm-count shifts)
            op == ".386P" || op == ".387" || op == ".SEQ" || op == ".DOSSEG" ||
            op == ".ALPHA" || op == ".SFCOND" || op == ".LFCOND" ||
            op == ".TFCOND") {
            if (op == ".186" || op == ".286" || op == ".286P" || op == ".286C" ||
                op == ".386" || op == ".386P") {
                cpu_186_ = true;  // imm-count shifts exist (MSHARD's SHR BX,4)
            }
            if (op == ".386" || op == ".386P") {
                cpu_386_ = true;  // near conditional jumps (0F 8x) exist
            }
            if (op == ".ALPHA") {
                // Alphabetical segment ordering: SEGDEF records (and every
                // segment index referencing them) emit sorted by segment
                // name -- SELECT's S_DISPLY declares DATA before CODE_FAR
                // yet its OBJ lists CODE_FAR first, while INTVEC (no
                // .ALPHA, same declaration shape) keeps declaration order.
                alpha_order_ = true;
            }
            if (op == ".SEQ") alpha_order_ = false;
            if (op == "EVEN") {
                // EVEN pads with NOP when the CS ASSUME covers the
                // segment -- directly or through its group (MSDATA's DATA
                // under CS:DOSGROUP, LABEL's classless CSEG) -- and zero
                // where it does not (GETMSG's messages segment).
                if (current_segment_ >= 0 && (segment().location & 1) != 0) {
                    bool cs_covers = assumes_[1] == current_segment_;
                    if (!cs_covers && assume_group_[1] > 0) {
                        const auto& members =
                            groups_[static_cast<std::size_t>(assume_group_[1]) - 1]
                                .members;
                        cs_covers = std::find(members.begin(), members.end(),
                                              segment().name) != members.end();
                    }
                    const std::string cls = upper(segment().cls);
                    const std::string seg_name = upper(segment().name);
                    const auto codeish = [](const std::string& text) {
                        return text.find("CODE") != std::string::npos ||
                               text == "DATA";
                    };
                    emit_byte(cs_covers || codeish(cls) || codeish(seg_name)
                                  ? 0x90
                                  : 0x00);
                }
            }
            return true;
        }
        if (op == "%OUT") {
            const std::size_t where = text.find('%');
            std::string message = where == std::string::npos ? "" : text.substr(where + 4);
            while (!message.empty() && (message.front() == ' ' || message.front() == '\t')) {
                message.erase(message.begin());
            }
            console_.push_back(message);
            return true;
        }
        if (op == "INCLUDE") {
            include_file(text);
            return true;
        }
        if (op == "MACRO") {
            pending_macro_ = Macro{};
            pending_macro_name_ = name;
            for (const Token& token : rest) {
                if (token.kind == Token::ident) pending_macro_.parameters.push_back(upper(token.text));
            }
            collecting_macro_ = true;
            return true;
        }
        if (op == "SEGMENT") {
            open_segment(name, rest);
            return true;
        }
        if (op == "ENDS") {
            if (!open_struc_.empty()) {
                close_struc(name);
            } else {
                // Segments nest (DOSMAC.INC opens Table inside code);
                // ENDS resumes the enclosing one.
                current_segment_ = segment_stack_.empty() ? -1 : segment_stack_.back();
                if (!segment_stack_.empty()) segment_stack_.pop_back();
            }
            return true;
        }
        if (op == "RECORD") {
            // `name RECORD f1:w1[=d1][,f2:w2...]` -- fields become
            // shift-count constants, packed from the high bit down
            // (COMMAND's parseflags; MSGHAN's SF_BITS). The record packs
            // into one byte when its widths fit, else a word.
            Record record;
            for (std::size_t i = 0; i + 2 < rest.size();) {
                if (rest[i].kind == Token::ident && rest[i + 1].kind == Token::punct &&
                    rest[i + 1].text == ":" && rest[i + 2].kind == Token::number) {
                    Record::Field field;
                    field.name = upper(rest[i].text);
                    field.width = static_cast<int>(rest[i + 2].value);
                    i += 3;
                    if (rest[i].kind == Token::punct && rest[i].text == "=" &&
                        rest[i + 1].kind == Token::number) {
                        field.preset = static_cast<long>(rest[i + 1].value);
                        i += 2;
                    }
                    record.total += field.width;
                    record.fields.push_back(field);
                } else {
                    ++i;
                }
            }
            record.unit = record.total <= 8 ? 1 : 2;
            int shift = record.total;
            for (Record::Field& field : record.fields) {
                shift -= field.width;
                field.shift = shift;
                Symbol symbol;
                symbol.kind = Symbol::constant;
                symbol.spelling = field.name;
                symbol.value = shift;
                symbol.defined = true;
                symbols_[field.name] = symbol;
                defined_this_pass_.insert(field.name);
                record_fields_[field.name] = {field.width, field.shift};
            }
            records_[name] = record;
            Symbol type;
            type.kind = Symbol::constant;
            type.spelling = name_spelling;
            type.value = record.total;
            type.defined = true;
            symbols_[name] = type;
            defined_this_pass_.insert(name);
            return true;
        }
        if (op == "STRUC") {
            open_struc_ = name;
            strucs_[name] = Struc{};
            struc_offset_ = 0;
            return true;
        }
        if (op == "ASSUME") {
            // `assume cs:dosxxx,ds:nothing,...` -- drives synthesized
            // segment prefixes on label references (CHARIN.OBJ's 2E A1).
            for (std::size_t i = 0; i + 2 < rest.size(); ++i) {
                if (rest[i].kind != Token::ident || rest[i + 1].kind != Token::punct ||
                    rest[i + 1].text != ":") {
                    continue;
                }
                const std::string reg = upper(rest[i].text);
                if (!sreg().contains(reg)) continue;
                const std::string target = upper(rest[i + 2].text);
                const std::size_t slot = static_cast<std::size_t>(sreg().at(reg));
                // A register can be assumed to a group (DEBERR's
                // `ASSUME DS:DG`); the group then frames references reached
                // through that register.
                int group = 0;
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    if (groups_[g].name == target) { group = static_cast<int>(g) + 1;
                                                     break; }
                }
                assume_group_[slot] = group;
                assumes_[slot] =
                    target == "NOTHING" ? -1 : segment_index_for(target) - 1;
                // The frame association survives ASSUME NOTHING: fixups
                // through the register still frame the LAST segment it
                // was assumed to (MSLOAD's `ds:Org_Rom_DiskTable` under
                // ds:nothing frames BOOTSEG).
                if (assumes_[slot] >= 0) sticky_assumes_[slot] = assumes_[slot];
                i += 2;
            }
            return true;
        }
        if (op == "PURGE") {
            // Deletes macro definitions (GETL's `purge cBegin`); unknown
            // names are ignored.
            for (const Token& token : rest) {
                if (token.kind != Token::ident) continue;
                macros_.erase(upper(token.text));
            }
            return true;
        }
        if (op == "PUBLIC") {
            for (const Token& token : rest) {
                if (token.kind != Token::ident) continue;
                const std::string key = upper(token.text);
                note_name(key);
                bool seen = false;
                for (const std::string& existing : publics_) {
                    if (upper(existing) == key) { seen = true; break; }
                }
                if (!seen) publics_.push_back(token.text);
            }
            return true;
        }
        if (op == "PROC") {
            proc_far_stack_.push_back(proc_far_);
            proc_far_ = !rest.empty() && rest[0].kind == Token::ident &&
                        upper(rest[0].text) == "FAR";
            define_label(name_spelling, proc_far_);
            return true;
        }
        if (op == "ENDP") {
            // MASM permits nested PROC blocks. Restore the enclosing
            // procedure's distance after ENDP; at top level a bare RET is
            // near again (PRINTF's helper after its far proc ends).
            if (proc_far_stack_.empty()) {
                proc_far_ = false;
            } else {
                proc_far_ = proc_far_stack_.back();
                proc_far_stack_.pop_back();
            }
            return true;
        }
        if (op == "EQU" || op == "=") {
            note_name(name);
            // `X EQU BYTE PTR $` is `X EQU THIS BYTE` (SYS1's EOS): the
            // location snapshots at the definition.
            if (rest.size() >= 4 && rest[0].kind == Token::ident &&
                rest[1].kind == Token::ident && upper(rest[1].text) == "PTR" &&
                rest[2].kind == Token::ident && rest[2].text == "$" &&
                rest[3].kind == Token::end) {
                const std::string type = upper(rest[0].text);
                const int size = type == "BYTE"    ? 1
                                 : type == "WORD"  ? 2
                                 : type == "DWORD" ? 4
                                                   : 0;
                define_label(name_spelling, false, size);
                return true;
            }
            // `X EQU WORD PTR $ - 2` freezes a typed label at a fixed offset
            // from the definition point (RAMDRIVE's ABOVE_PID names the word
            // two bytes back). It must snapshot `$` here, not splice as a text
            // equate that re-reads `$` at every use -- otherwise `[ABOVE_PID]`
            // relocates to the wrong place and its size flips across passes.
            if (rest.size() >= 6 && rest[0].kind == Token::ident &&
                rest[1].kind == Token::ident && upper(rest[1].text) == "PTR" &&
                rest[2].kind == Token::ident && rest[2].text == "$" &&
                rest[3].kind == Token::punct &&
                (rest[3].text == "+" || rest[3].text == "-") &&
                rest[4].kind == Token::number && rest[5].kind == Token::end) {
                const std::string type = upper(rest[0].text);
                const int size = type == "BYTE"    ? 1
                                 : type == "WORD"  ? 2
                                 : type == "DWORD" ? 4
                                                   : 0;
                const long addend =
                    (rest[3].text == "-" ? -1 : 1) * rest[4].value;
                define_label(name_spelling, false, size, addend);
                return true;
            }
            // `X EQU $` is a label at the current location, not a
            // constant -- TREESYSM's LAST_BYTE rides the CSEG PUBDEF
            // run, never the absolute form. The redefinable `X = $` form is
            // instead a plain absolute snapshot of the location counter
            // (STRUC.INC's `$LastLabelOrg = $`, reassigned at every label);
            // it must fall through to the redefinable-constant path so a new
            // snapshot is not a phase error against the old one.
            if (op == "EQU" && rest[0].kind == Token::ident &&
                rest[0].text == "$" && rest[1].kind == Token::end) {
                define_label(name_spelling, false, 0);
                return true;
            }
            // `X EQU THIS WORD` defines a label at the current location
            // with the named width (MS_TABLE's MSVERS over MSMAJOR).
            if (rest.size() >= 3 && rest[0].kind == Token::ident &&
                upper(rest[0].text) == "THIS" && rest[1].kind == Token::ident &&
                rest[2].kind == Token::end) {
                const std::string type = upper(rest[1].text);
                const int size = type == "BYTE"    ? 1
                                 : type == "WORD"  ? 2
                                 : type == "DWORD" ? 4
                                                   : 0;
                define_label(name_spelling, type == "FAR", size);
                return true;
            }
            // `Sublist = dg:sublist_msg_parserr` binds the LABEL, the
            // group qualifier dropped: RECDISP's later `dw Sublist` rows
            // relocate F_SEG DATA with the label's own offset, not
            // through DG (and the binding must not linger as a text
            // equate poisoning every other row's `dw Sublist`).
            if (rest.size() >= 4 && rest[0].kind == Token::ident &&
                rest[1].kind == Token::punct && rest[1].text == ":" &&
                rest[2].kind == Token::ident && rest[3].kind == Token::end) {
                const std::string qualifier = upper(rest[0].text);
                // A segment-register qualifier drops the same way
                // (E2BTABLE's `Sublist = ds:sublist_msg_parserr`).
                bool named = segment_index_for(qualifier) > 0 ||
                             sreg().contains(qualifier);
                for (const Group& group : groups_) {
                    if (group.name == qualifier) named = true;
                }
                const auto source = symbols_.find(upper(rest[2].text));
                if (named && source != symbols_.end() &&
                    source->second.kind == Symbol::label) {
                    Symbol alias = source->second;
                    alias.spelling = name_spelling;
                    symbols_[name] = alias;
                    defined_this_pass_.insert(name);
                    return true;
                }
            }
            // `LOAD EQU ZLOAD` aliases a SEGMENT name: uses behave like
            // the segment itself (E2BINIT's `MOV BP,SEG LOAD` frames
            // ZLOAD), so it splices as text rather than probing to the
            // pass-2 numeric fallback.
            if (rest[0].kind == Token::ident && rest[1].kind == Token::end &&
                segment_index_for(upper(rest[0].text)) > 0 &&
                !symbols_.contains(upper(rest[0].text))) {
                text_equates_[name] = rest[0].text;
                return true;
            }
            // `PREV_OWN EQU DWORD PTR JMPREV+1` -- a PTR cast over a LABEL
            // (with an optional numeric addend) is a typed ADDRESS alias, a
            // real symbol: GRTABHAN's `PUBLIC PREV_OWN` rides the PUBDEF at
            // JMPREV+1, which the text-equate splice below cannot provide.
            // An unknown base still falls through to the text equate
            // (PSDATA's `$P_Flags1 equ byte ptr $P_Flags` chains).
            if (rest.size() >= 3 && rest[0].kind == Token::ident &&
                (upper(rest[0].text) == "BYTE" || upper(rest[0].text) == "WORD" ||
                 upper(rest[0].text) == "DWORD") &&
                rest[1].kind == Token::ident && upper(rest[1].text) == "PTR" &&
                rest[2].kind == Token::ident) {
                long addend = 0;
                bool shaped = rest[3].kind == Token::end;
                if (!shaped && rest[3].kind == Token::punct &&
                    (rest[3].text == "+" || rest[3].text == "-") &&
                    rest[4].kind == Token::number && rest[5].kind == Token::end) {
                    addend = (rest[3].text == "-" ? -1 : 1) * rest[4].value;
                    shaped = true;
                }
                if (shaped) {
                    const auto source = symbols_.find(upper(rest[2].text));
                    if (source != symbols_.end() &&
                        source->second.kind == Symbol::label &&
                        source->second.segment >= 0 &&
                        source->second.extern_index == 0) {
                        const std::string type = upper(rest[0].text);
                        Symbol alias = source->second;
                        alias.spelling = name_spelling;
                        alias.value += addend;
                        alias.size = type == "BYTE" ? 1 : type == "WORD" ? 2 : 4;
                        symbols_[name] = alias;
                        defined_this_pass_.insert(name);
                        return true;
                    }
                }
            }
            // A numeric operand makes a constant; anything else -- DWORD
            // PTR aliases, bracketed text -- becomes a text equate spliced
            // back in at every use (COMMAND's buf_offset).
            std::size_t used = 0;
            std::vector<Token> probe = rest;
            // A bare struc NAME in the probed body is its base, zero
            // (IFSSYM's `LENGTH_INIT EQU IFSR_RESSIZE-IFSRH+2` = 28,
            // its IFSR_RESSIZE resolving through the text-equate chain).
            zero_bare_struc_names(probe);
            const std::size_t previous_errors = errors_.size();
            const Value value = expression(probe, &used);
            // A register alias is text, never a value (PARSE.ASM's
            // `psdata_seg EQU CS` names whichever register reaches the
            // parser's data).
            const bool register_body =
                rest[0].kind == Token::ident &&
                (sreg().contains(upper(rest[0].text)) ||
                 reg16().contains(upper(rest[0].text)) ||
                 reg8().contains(upper(rest[0].text)) ||
                 // A PTR-cast body is an alias, never a value (PSDATA's
                 // `$P_Flags1 equ byte ptr $P_Flags`).
                 ((upper(rest[0].text) == "BYTE" ||
                   upper(rest[0].text) == "WORD" ||
                   upper(rest[0].text) == "DWORD") &&
                  rest[1].kind == Token::ident &&
                  upper(rest[1].text) == "PTR"));
            const bool numeric =
                !register_body &&
                probe[used].kind == Token::end && errors_.size() == previous_errors;
            if (!numeric) {
                errors_.resize(previous_errors);
                std::size_t where = 0;
                std::string upper_text = upper(text);
                where = upper_text.find(op == "=" ? "=" : "EQU");
                std::string body =
                    where == std::string::npos
                        ? ""
                        : trim(text.substr(where + (op == "=" ? 1 : 3)));
                // The comment never joins the equate (PARSE.ASM's
                // `psdata_seg EQU CS ;AN022;` splices in as bare CS).
                char in_quote = '\0';
                for (std::size_t k = 0; k < body.size(); ++k) {
                    const char ch = body[k];
                    if (in_quote != '\0') {
                        if (ch == in_quote) in_quote = '\0';
                        continue;
                    }
                    if (ch == '\'' || ch == '"') in_quote = ch;
                    if (ch == ';') {
                        body = trim(body.substr(0, k));
                        break;
                    }
                }
                if (body.size() >= 2 && body.front() == '<' && body.back() == '>') {
                    body = body.substr(1, body.size() - 2);
                }
                text_equates_[name] = body;
                return true;
            }
            // `Sublist = Sublist_msgCurrentTrack` aliases a LABEL: uses
            // keep relocating to it (DISPLAY's message tables). A struc
            // FIELD aliases with its width intact -- DISKCOMP's
            // `VOL_SERIAL EQU EXT_BOOT_SERIAL` keeps TYPE = 4.
            if (rest[0].kind == Token::ident && rest[1].kind == Token::end) {
                const auto source = symbols_.find(upper(rest[0].text));
                if (source != symbols_.end() &&
                    (source->second.kind == Symbol::label ||
                     (source->second.kind == Symbol::constant &&
                      source->second.size > 0))) {
                    Symbol alias = source->second;
                    alias.spelling = name_spelling;
                    alias.late_extern_alias =
                        source->second.late_extern_alias ||
                        (source->second.extern_index > 0 &&
                         !defined_this_pass_.contains(upper(rest[0].text)));
                    symbols_[name] = alias;
                    defined_this_pass_.insert(name);
                    return true;
                }
                // The RHS symbol is not declared yet. Freeze the constant
                // fallback below, but remember the alias so an EXTRN of that
                // name later in this pass can re-bind it to the external --
                // otherwise `MOV SD.field,0` sizes its store as a WORD in pass
                // 1 (unknown base) and a BYTE in pass 2 (extern alias) and
                // phase-errors.
                if (source == symbols_.end()) {
                    deferred_alias_[upper(rest[0].text)].push_back(
                        {name, name_spelling});
                }
            }
            // `VEC EQU (VECTOR14 + 1)` -- a LABEL plus a constant addend is
            // an address alias, not a number: RESCODE's `CALL DWORD PTR
            // CS:VEC` relocates through it (data 0000, the offset in the
            // fixup), where folding to a constant wrote the resolved offset
            // raw. Parens optional; only a label base aliases -- a constant
            // base keeps the numeric fold below.
            {
                std::size_t b = 0;
                std::size_t tail = 0;
                if (rest[0].kind == Token::punct && rest[0].text == "(" &&
                    rest[1].kind == Token::ident && rest[2].kind == Token::punct &&
                    (rest[2].text == "+" || rest[2].text == "-") &&
                    rest[3].kind == Token::number &&
                    rest[4].kind == Token::punct && rest[4].text == ")" &&
                    rest[5].kind == Token::end) {
                    b = 1;
                    tail = 2;
                } else if (rest[0].kind == Token::ident &&
                           rest[1].kind == Token::punct &&
                           (rest[1].text == "+" || rest[1].text == "-") &&
                           rest[2].kind == Token::number &&
                           rest[3].kind == Token::end) {
                    b = 0;
                    tail = 1;
                } else {
                    tail = 0;
                }
                if (tail != 0) {
                    const auto source = symbols_.find(upper(rest[b].text));
                    if (source != symbols_.end() &&
                        source->second.kind == Symbol::label &&
                        source->second.segment >= 0 &&
                        source->second.extern_index == 0) {
                        const long addend =
                            (rest[tail].text == "-" ? -1 : 1) *
                            rest[tail + 1].value;
                        Symbol alias = source->second;
                        alias.spelling = name_spelling;
                        alias.value += addend;
                        symbols_[name] = alias;
                        defined_this_pass_.insert(name);
                        return true;
                    }
                }
            }
            Symbol symbol;
            symbol.kind = Symbol::constant;
            symbol.spelling = name_spelling;
            symbol.value = value.value;
            symbol.defined = true;
            // A numeric EQU with one OFFSET external base is still
            // relocatable. The expression evaluator supplies its constant
            // addend; retain the target and any segment/group qualifier for
            // the eventual immediate fixup (VMTRAP's VMT_STACK/VMTERR_STACK).
            int offset_externs = 0;
            for (std::size_t i = 0; rest[i].kind != Token::end; ++i) {
                if (rest[i].kind != Token::ident ||
                    upper(rest[i].text) != "OFFSET") {
                    continue;
                }
                std::size_t target = i + 1;
                int frame_segment = 0;
                int frame_group = 0;
                if (rest[target].kind == Token::ident &&
                    rest[target + 1].kind == Token::punct &&
                    rest[target + 1].text == ":") {
                    const std::string qualifier = upper(rest[target].text);
                    frame_segment = segment_index_for(qualifier);
                    if (frame_segment == 0) {
                        for (std::size_t g = 0; g < groups_.size(); ++g) {
                            if (groups_[g].name == qualifier) {
                                frame_group = static_cast<int>(g) + 1;
                                break;
                            }
                        }
                    }
                    target += 2;
                }
                if (rest[target].kind != Token::ident) continue;
                const auto base = symbols_.find(upper(rest[target].text));
                if (base == symbols_.end() || base->second.extern_index <= 0) {
                    continue;
                }
                ++offset_externs;
                symbol.constant_fixup_extern = base->second.extern_index;
                symbol.constant_frame_segment = frame_segment;
                symbol.constant_frame_group = frame_group;
            }
            if (offset_externs != 1) {
                symbol.constant_fixup_extern = 0;
                symbol.constant_frame_segment = 0;
                symbol.constant_frame_group = 0;
            }
            const auto previous = symbols_.find(name);
            symbol.forward_derived_constant =
                op == "EQU" &&
                (!value.known ||
                 (previous != symbols_.end() &&
                  previous->second.forward_derived_constant));
            symbols_[name] = symbol;
            defined_this_pass_.insert(name);
            return true;
        }
        if (op == "DB" || op == "DW" || op == "DD" || op == "DQ" || op == "DT") {
            const int unit = op == "DB" ? 1 : op == "DW" ? 2 : op == "DD" ? 4 : op == "DQ" ? 8 : 10;
            if (!open_struc_.empty()) {
                struc_field(name, unit, rest);
            } else {
                if (!name.empty()) {
                    define_data_label(name_spelling, unit);
                    // LENGTH is the leading DUP's count; SIZE is
                    // LENGTH * TYPE (TREE's `SIZE FIX_DTA_FILN` = 13).
                    std::size_t probe = 0;
                    if (long repeat = 0;
                        dup_count(rest, probe, &repeat) && repeat > 0) {
                        symbols_[name].size_total = repeat * unit;
                    } else {
                        symbols_[name].size_total = unit;
                    }
                }
                data_directive(unit, rest);
            }
            return true;
        }
        if (op == "EXTRN" || op == "EXTERN") {
            // `extrn name:type[,name:type...]` -- EXTDEF order is the
            // reference order; the type byte in the record is always 0.
            for (std::size_t i = 0; i < rest.size() && rest[i].kind != Token::end;) {
                if (rest[i].kind != Token::ident) {
                    ++i;
                    continue;
                }
                const std::string spelling = rest[i].text;
                ++i;
                int declared_size = 0;
                bool declared_far = false;
                bool declared_abs = false;
                if (rest[i].kind == Token::punct && rest[i].text == ":") {
                    ++i;
                    if (rest[i].kind == Token::ident) {
                        const std::string type = upper(rest[i].text);
                        declared_size = type == "BYTE" ? 1 : type == "WORD" ? 2
                                        : type == "DWORD" ? 4 : 0;
                        declared_far = type == "FAR";
                        declared_abs = type == "ABS";
                        ++i;
                    }
                }
                if (rest[i].kind == Token::punct && rest[i].text == ",") ++i;
                const std::string key = upper(spelling);
                if (!symbols_.contains(key)) {
                    externs_.push_back(spelling);
                    // A pass-2-only declaration (invoke's IF2/IFNDEF path)
                    // streams its EXTDEF at this point and breaks the
                    // active LEDATA run (BUF.OBJ's interleaved EXTDEFs).
                    extern_sequences_.push_back(pass_ == 2 ? ++emit_sequence_
                                                           : 0);
                    Symbol symbol;
                    symbol.kind = Symbol::label;
                    symbol.spelling = spelling;
                    symbol.extern_index = static_cast<int>(externs_.size());
                    symbol.defined = true;
                    symbol.value = 0;
                    symbol.size = declared_size;
                    symbol.far_label = declared_far;
                    // The declaration-time group: whichever register is
                    // ASSUMEd to a group when the EXTRN executes (CHKDSK2
                    // after `assume cs:dg`; CTRLC's I_need externs under
                    // SS:DOSGROUP,CS:DOSGROUP with DS unassumed).
                    symbol.pub_group = declaration_group(declared_size);
                    {
                        const int reg = declared_size > 0 ? 3 : 1;
                        symbol.pub_segment =
                            assumes_[reg] >= 0 ? assumes_[reg] + 1 : 0;
                    }
                    // The declaring segment frames this external's fixups
                    // (fixdata 06, measured off F_CLOSE.OBJ); an ABS
                    // external has none and stays F_EXT.
                    symbol.segment = declared_abs ? -1 : current_segment_;
                    symbol.abs_extern = declared_abs;
                    if (declared_abs) {
                        symbol.pub_group = 0;
                        symbol.pub_segment = 0;
                    }
                    symbols_[key] = symbol;
                    defined_this_pass_.insert(key);
                    // Re-bind any earlier `X EQU <this name>` that had frozen
                    // as a constant because the name was still undeclared:
                    // X now aliases the external, so a `MOV X.field,imm`
                    // sizes identically in both passes.
                    if (const auto pend = deferred_alias_.find(key);
                        pend != deferred_alias_.end()) {
                        for (const auto& [alias_key, alias_spelling] :
                             pend->second) {
                            Symbol alias = symbol;
                            alias.spelling = alias_spelling;
                            alias.late_extern_alias = true;
                            symbols_[alias_key] = alias;
                            defined_this_pass_.insert(alias_key);
                        }
                    }
                } else if (const auto known = symbols_.find(key); known != symbols_.end()) {
                    known->second.defined = true;
                    defined_this_pass_.insert(key);
                    // Group indices re-sort between passes, so refresh the
                    // declaration-time group under pass-2 numbering
                    // (_REPLACE's `extrn _restore:near` under CS:PGROUP).
                    if (pass_ == 2 && known->second.extern_index > 0) {
                        known->second.pub_group = declaration_group(declared_size);
                        const int reg = declared_size > 0 ? 3 : 1;
                        known->second.pub_segment =
                            assumes_[reg] >= 0 ? assumes_[reg] + 1 : 0;
                    }
                }
            }
            return true;
        }
        if (op == "ORG") {
            if (current_segment_ < 0) {
                fail("ORG outside any segment");
                return true;
            }
            segment().location = static_cast<std::uint16_t>(expression(rest).value);
            segment().extent = std::max(segment().extent, segment().location);
            return true;
        }
        if (op == "REPT" || op == "IRP" || op == "IRPC") {
            begin_block(op, text);
            return true;
        }
        if (op == "GROUP") {
            if (pass_ == 1) {
                Group group;
                group.name = name;
                for (const Token& token : rest) {
                    if (token.kind == Token::ident) {
                        group.members.push_back(upper(token.text));
                    }
                }
                name_insertions_.push_back(name);
                groups_.push_back(std::move(group));
            }
            return true;
        }
        if (op == "LABEL") {
            const std::string type =
                !rest.empty() && rest[0].kind == Token::ident ? upper(rest[0].text) : "";
            const int size = type == "BYTE" ? 1 : type == "WORD" ? 2 : type == "DWORD" ? 4 : 0;
            define_label(name_spelling, type == "FAR", size);
            return true;
        }
        if (op == "COMMENT") {
            std::size_t where = text.find_first_not_of(" \t");
            where = text.find_first_of(" \t", where);        // past "comment"
            where = text.find_first_not_of(" \t", where);    // the delimiter
            if (where == std::string::npos) return true;
            const char delimiter = text[where];
            if (text.find(delimiter, where + 1) == std::string::npos) {
                comment_delimiter_ = delimiter;
            }
            return true;
        }
        if ((op == "MEXIT" || op == "EXITM") && !macros_.contains(op)) {
            // Exit the innermost macro expansion: drop frames down to and
            // including the macro frame, and close any IF opened inside it
            // (GETMSG's `if ... MsgError / mexit / endif` guards). A USER
            // macro of the same name shadows the directive -- MAPPER's
            // MACROS.INC defines `Mexit` as its own epilogue macro.
            while (!frames_.empty()) {
                const bool was_macro = frames_.back().macro_frame;
                const std::size_t base = frames_.back().conditional_base;
                frames_.pop_back();
                if (was_macro) {
                    if (conditionals_.size() > base) conditionals_.resize(base);
                    break;
                }
            }
            return true;
        }
        if (op == "END") {
            ended_ = true;
            // `END label` names the program entry point; the MODEND record
            // then carries a start address and the main-module flag.
            if (!rest.empty() && rest[0].kind == Token::ident) {
                end_start_label_ = upper(rest[0].text);
            }
            return true;
        }
        return false;
    }

    void begin_block(const std::string& kind, const std::string& text) {
        collecting_block_ = true;
        block_depth_ = 0;
        block_kind_ = kind;
        block_body_.clear();
        block_items_.clear();
        block_parameter_.clear();
        // Operands from the raw text: `irp p,<a,b,c>` / `irpc p,chars` /
        // `rept expr`.
        std::size_t i = text.find_first_not_of(" \t");
        i = text.find_first_of(" \t", i);
        i = text.find_first_not_of(" \t", i);
        std::string operand = i == std::string::npos ? "" : text.substr(i);
        // Strip a trailing comment (outside <> and strings): ATTRIBA's
        // `IRP ANAME,<NAMELIST>  ; FOR EACH ...`.
        {
            int angle = 0;
            char quote = '\0';
            for (std::size_t k = 0; k < operand.size(); ++k) {
                const char ch = operand[k];
                if (quote != '\0') {
                    if (ch == quote) quote = '\0';
                } else if (ch == '\'' || ch == '"') {
                    quote = ch;
                } else if (ch == '<') {
                    ++angle;
                } else if (ch == '>') {
                    --angle;
                } else if (ch == ';' && angle == 0) {
                    operand.resize(k);
                    break;
                }
            }
        }
        while (!operand.empty() && (operand.back() == ' ' || operand.back() == '\t')) {
            operand.pop_back();
        }
        if (kind == "REPT") {
            std::string error;
            std::vector<Token> tokens = Lexer::line(operand, &error);
            const long count = expression(tokens).value;
            for (long n = 0; n < count; ++n) block_items_.push_back("");
            return;
        }
        const std::size_t comma = operand.find(',');
        block_parameter_ = upper(operand.substr(0, comma));
        while (!block_parameter_.empty() && block_parameter_.back() == ' ') {
            block_parameter_.pop_back();
        }
        std::string list = comma == std::string::npos ? "" : operand.substr(comma + 1);
        while (!list.empty() && (list.front() == ' ' || list.front() == '\t')) {
            list.erase(list.begin());
        }
        if (kind == "IRPC") {
            if (!list.empty() && list.front() == '<' && list.back() == '>') {
                list = list.substr(1, list.size() - 2);
            }
            for (char c : list) block_items_.push_back(std::string(1, c));
            return;
        }
        if (!list.empty() && list.front() == '<' && list.back() == '>') {
            list = list.substr(1, list.size() - 2);
        }
        if (list.empty()) return;
        // Each item sheds one angle level, exactly like a macro argument:
        // STRUC.INC's `irp x,<<jeq,je>,...>` hands `$BuildJump x` the two
        // names as separate arguments.
        const auto push_item = [this](std::string item) {
            while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) {
                item.erase(item.begin());
            }
            while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) {
                item.pop_back();
            }
            if (item.size() >= 2 && item.front() == '<' && item.back() == '>') {
                item = item.substr(1, item.size() - 2);
            }
            block_items_.push_back(item);
        };
        std::string item;
        int angle = 0;
        for (char c : list) {
            if (c == '<') ++angle;
            if (c == '>') --angle;
            if (c == ',' && angle == 0) {
                push_item(item);
                item.clear();
                continue;
            }
            item += c;
        }
        push_item(item);
    }

    void expand_block() {
        Frame frame;
        // A REPT/IRP/IRPC expansion is an EXITM target just like a macro.
        frame.macro_frame = true;
        frame.conditional_base = conditionals_.size();
        for (const std::string& item : block_items_) {
            for (const std::string& line : block_body_) {
                if (block_parameter_.empty()) frame.lines.push_back(line);
                else frame.lines.push_back(substitute(line, {block_parameter_}, {item}));
            }
        }
        frames_.push_back(std::move(frame));
    }

    void include_file(const std::string& text) {
        // Take the operand as raw text after the directive word: paths carry
        // backslashes and dots that the lexer splits.
        std::string spec;
        std::size_t i = 0;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        i += 7;  // "include"
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != ';') {
            spec += text[i++];
        }
        if (const auto equate = text_equates_.find(upper(spec));
            equate != text_equates_.end()) {
            spec = equate->second;
        }
        std::vector<std::string> candidates{spec};
        for (const std::string& dir : options_.include_paths) {
            candidates.push_back(dir + "\\" + spec);
        }
        for (const std::string& candidate : candidates) {
            const std::filesystem::path path = resolve_dos_input(candidate);
            std::error_code missing;
            if (!std::filesystem::is_regular_file(path, missing)) continue;
            frames_.push_back({split_source(read_file(path.string())), 0});
            return;
        }
        fail("include file not found: " + spec);
    }

    // Arguments come from the raw line: split at top-level commas, honor
    // `<verbatim>` grouping, evaluate `%expression` args to decimal text.
    void invoke_macro(const Macro& macro, const std::string& op, const std::string& text) {
        // find the operand text after the macro's name token
        std::size_t at = 0;
        std::string upper_text = upper(text);
        const std::string needle = upper(op);
        while (true) {
            at = upper_text.find(needle, at);
            if (at == std::string::npos) {
                at = text.size();
                break;
            }
            const bool left_ok = at == 0 || !ident_char(text[at - 1]);
            const bool right_ok =
                at + needle.size() >= text.size() || !ident_char(text[at + needle.size()]);
            if (left_ok && right_ok) {
                at += needle.size();
                break;
            }
            ++at;
        }
        std::vector<std::string> arguments = split_macro_arguments(text.substr(at));
        for (std::string& argument : arguments) {
            if (!argument.empty() && argument.front() == '%') {
                std::string error;
                std::vector<Token> tokens = Lexer::line(argument.substr(1), &error);
                argument = std::to_string(expression(tokens).value);
            }
        }
        Frame frame;
        frame.macro_frame = true;
        frame.conditional_base = conditionals_.size();
        std::vector<std::string> parameters = macro.parameters;
        std::vector<std::string> values = arguments;
        // Missing arguments substitute as empty -- pad before appending
        // LOCAL values or the locals shift into the parameter slots
        // (DOSMAC's `return` with its unused x parameter).
        values.resize(parameters.size(), "");
        // LOCAL names substitute as ??0000-style generated labels.
        for (const std::string& local : macro.locals) {
            parameters.push_back(local);
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "??%04X", local_counter_++);
            values.push_back(buffer);
        }
        for (const std::string& line : macro.body) {
            frame.lines.push_back(substitute(line, parameters, values));
        }
        frames_.push_back(std::move(frame));
    }

    // Inside a struc instance's <...> only commas separate the positional
    // overrides -- MSGHAN's `MSG_DESC <,STDERR,,,CLASS_1 SHL 8>` passes the
    // whole expression as the fifth field (COMPSM.OBJ holds 0100, the
    // shifted class). Blank-splitting is a macro-invocation rule only.
    static std::vector<std::string> split_struc_overrides(const std::string& inside) {
        std::vector<std::string> out;
        std::string current;
        int angle = 0;
        char quote = '\0';
        for (const char c : inside) {
            if (quote != '\0') {
                current += c;
                if (c == quote) quote = '\0';
                continue;
            }
            if (c == '\'' || c == '"') {
                quote = c;
                current += c;
                continue;
            }
            if (c == '<') ++angle;
            if (c == '>') --angle;
            if (c == ',' && angle == 0) {
                out.push_back(trim(current));
                current.clear();
                continue;
            }
            current += c;
        }
        out.push_back(trim(current));
        return out;
    }

    static std::vector<std::string> split_macro_arguments(const std::string& operands) {
        std::vector<std::string> out;
        std::string current;
        int angle = 0;
        char quote = '\0';
        bool any = false;
        bool stripped_open = false;
        for (std::size_t i = 0; i < operands.size(); ++i) {
            const char c = operands[i];
            // A comment ends the argument list -- `CENTER  ;AN000;` has no
            // arguments -- unless the `;` sits inside <> or a string.
            if (c == ';' && angle == 0 && quote == '\0') break;
            if (quote != '\0') {
                current += c;
                if (c == quote) quote = '\0';
                continue;
            }
            if (c == ';' && angle == 0) break;
            if (c == '\'' || c == '"') {
                quote = c;
                current += c;
                continue;
            }
            if (c == '<') {
                // Only an argument-LEADING bracket pair is stripped; a
                // record instance mid-argument keeps its brackets
                // (SUBLIST's `SF_BITS<SF_LEFT,...>` field).
                if (angle++ == 0) {
                    if (trim(current).empty()) {
                        any = true;
                        stripped_open = true;
                        continue;
                    }
                    stripped_open = false;
                }
            }
            if (c == '>') {
                if (--angle == 0 && stripped_open) continue;
            }
            if (c == ',' && angle == 0) {
                out.push_back(trim(current));
                current.clear();
                any = true;
                continue;
            }
            // Blanks separate arguments too (E2BINIT's `procedure
            // Main_Init near` passes two, and the one-parameter macro
            // drops the excess); a comma right after eats the blank run.
            if ((c == ' ' || c == '\t') && angle == 0 &&
                !trim(current).empty()) {
                std::size_t look = i;
                while (look + 1 < operands.size() &&
                       (operands[look + 1] == ' ' || operands[look + 1] == '\t')) {
                    ++look;
                }
                const char next =
                    look + 1 < operands.size() ? operands[look + 1] : '\0';
                if (next == ',' || next == ';' || next == '\0') {
                    i = look;
                    continue;
                }
                out.push_back(trim(current));
                current.clear();
                any = true;
                i = look;
                continue;
            }
            current += c;
            if (c != ' ' && c != '\t') any = true;
        }
        if (any || !trim(current).empty()) out.push_back(trim(current));
        return out;
    }

    static std::string trim(const std::string& text) {
        std::size_t begin = 0;
        std::size_t end = text.size();
        while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
        while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
        return text.substr(begin, end - begin);
    }

    static std::string substitute(const std::string& line,
                                  const std::vector<std::string>& parameters,
                                  const std::vector<std::string>& arguments) {
        const auto value_of = [&](const std::string& folded, std::string* out) {
            for (std::size_t p = 0; p < parameters.size(); ++p) {
                if (parameters[p] == folded) {
                    *out = p < arguments.size() ? arguments[p] : "";
                    return true;
                }
            }
            return false;
        };
        std::string out;
        std::size_t i = 0;
        bool splice_pending = false;  // an `&` was just consumed
        while (i < line.size()) {
            if (line[i] == '&') {
                // MASM strips exactly one ampersand per expansion level. A run
                // of k>=2 emits k-1 and defers substitution to a deeper level
                // (CMACROS' mpush leaves `?&x` for its inner IRP; STRUC.INC's
                // `$&&&x` must survive macro -> IRP -> IRP down to `$or`); a
                // lone `&` splices a parameter now (GETMSG's Message&Number)
                // or, next to no parameter, survives for the next level.
                std::size_t run = 0;
                while (i + run < line.size() && line[i + run] == '&') ++run;
                if (run >= 2) {
                    out.append(run - 1, '&');
                    i += run;
                    continue;
                }
                std::string word;
                std::size_t j = i + 1;
                while (j < line.size() && ident_char(line[j])) word += line[j++];
                std::string value;
                if (!word.empty() && value_of(upper(word), &value)) {
                    out += value;
                    i = j;
                    if (i < line.size() && line[i] == '&') ++i;  // trailing marker
                    continue;
                }
                splice_pending = true;
                ++i;
                continue;
            }
            if (ident_start(line[i])) {
                std::string word;
                while (i < line.size() && ident_char(line[i])) word += line[i++];
                std::string value;
                if (value_of(upper(word), &value)) {
                    out += value;
                    if (i < line.size() && line[i] == '&') ++i;  // NAME& form
                } else {
                    out += word;
                }
                splice_pending = false;
                continue;
            }
            (void)splice_pending;
            out += line[i++];
        }
        return out;
    }

    // ----------------------------------------------------------- segments
    Segment& segment() { return segments_[static_cast<std::size_t>(current_segment_)]; }

    static bool segment_uses_32(const Segment& segment) {
        return segment.use_attr == 1 ||
               (segment.absolute && segment.use_attr == -1 && segment.decl_386);
    }

    bool segment_uses_32(int one_based) const {
        return one_based > 0 &&
               one_based <= static_cast<int>(segments_.size()) &&
               segment_uses_32(segments_[static_cast<std::size_t>(one_based) - 1]);
    }

    void open_segment(const std::string& name, const std::vector<Token>& rest) {
        segment_stack_.push_back(current_segment_);
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            if (segments_[i].name == name) {
                current_segment_ = static_cast<int>(i);
                // A reopen with explicit attributes updates them -- DEBUG
                // declares `CONST SEGMENT PUBLIC` then reopens
                // `SEGMENT PUBLIC BYTE`, and the SEGDEF shows BYTE; VMTRAP
                // opens a bare `_TEXT segment` for its EXTRNs before
                // VDMseg.inc's full `_TEXT SEGMENT BYTE USE16 PUBLIC
                // 'CODE'`, and the SEGDEF carries the later combine and
                // class. Attributes absent from the reopen keep their
                // stored values.
                for (const Token& token : rest) {
                    if (token.kind == Token::str) {
                        segments_[i].cls = upper(token.text);
                        name_insertions_.push_back(segments_[i].cls);
                        continue;
                    }
                    if (token.kind != Token::ident) continue;
                    const std::string word = upper(token.text);
                    if (word == "BYTE") segments_[i].align = 1;
                    else if (word == "WORD") segments_[i].align = 2;
                    else if (word == "PARA") segments_[i].align = 3;
                    else if (word == "PAGE") segments_[i].align = 4;
                    else if (word == "USE16") segments_[i].use_attr = 0;
                    else if (word == "USE32") segments_[i].use_attr = 1;
                    else if (word == "PUBLIC") segments_[i].combine = 2;
                    else if (word == "COMMON") segments_[i].combine = 6;
                    else if (word == "STACK") segments_[i].combine = 5;
                }
                return;
            }
        }
        if (pass_ == 2) {
            fail("segment appeared only in pass 2: " + name);
            return;
        }
        Segment fresh;
        fresh.name = name;
        fresh.decl_386 = cpu_386_;
        for (std::size_t i = 0; i < rest.size(); ++i) {
            const Token& token = rest[i];
            if (token.kind == Token::ident) {
                const std::string word = upper(token.text);
                if (word == "BYTE") fresh.align = 1;
                else if (word == "WORD") fresh.align = 2;
                else if (word == "PARA") fresh.align = 3;
                else if (word == "PAGE") fresh.align = 4;
                else if (word == "USE16") fresh.use_attr = 0;
                else if (word == "USE32") fresh.use_attr = 1;
                else if (word == "PUBLIC") fresh.combine = 2;
                else if (word == "COMMON") fresh.combine = 6;
                else if (word == "STACK") fresh.combine = 5;
                else if (word == "AT") {
                    // `SEGMENT AT frame`: an absolute segment -- align 0,
                    // the frame in the SEGDEF (ATTRIBA's `psp segment at 0`).
                    fresh.combine = 0;
                    fresh.absolute = true;
                    std::vector<Token> expr(rest.begin() + static_cast<long>(i) + 1,
                                            rest.end());
                    fresh.frame = static_cast<std::uint16_t>(expression(expr).value);
                    break;
                }
            } else if (token.kind == Token::str) {
                fresh.cls = upper(token.text);
            }
        }
        if (!fresh.name.empty()) name_insertions_.push_back(fresh.name);
        if (!fresh.cls.empty()) name_insertions_.push_back(fresh.cls);
        segments_.push_back(std::move(fresh));
        current_segment_ = static_cast<int>(segments_.size() - 1);
        segments_order_.push_back(current_segment_);
    }

    // ------------------------------------------------------------- strucs
    void struc_field(const std::string& name, int unit, std::vector<Token>& rest) {
        if (!name.empty()) {
            strucs_[open_struc_].fields[name] = struc_offset_;
            Symbol symbol;
            symbol.kind = Symbol::constant;
            symbol.spelling = name;
            symbol.value = struc_offset_;
            symbol.size = unit;
            symbol.defined = true;
            symbol.struc_field = true;
            // SIZE/LENGTH of the field read its element count: a leading DUP
            // gives LENGTH, else one element (GRAFTABL's `TABSIZE EQU SIZE
            // LANCHAR + ...` over a `DB 1024 DUP(?)` field wants 1024, not the
            // field's offset).
            std::size_t probe = 0;
            if (long repeat = 0; dup_count(rest, probe, &repeat) && repeat > 0) {
                symbol.size_total = repeat * unit;
            } else {
                symbol.size_total = unit;
            }
            symbols_[name] = symbol;
            defined_this_pass_.insert(name);
        }
        // Each field directive becomes one instance piece: a DUP nests as
        // repeat x { 1 x literal }, a scalar list is a flat literal, and
        // `?` zeroes -- the exact block shapes F_FIRST.OBJ's LIDATA holds.
        Struc& owner = strucs_[open_struc_];
        std::size_t i = 0;
        long repeat = 0;
        if (dup_count(rest, i, &repeat)) {
            if (rest[i].kind == Token::punct && rest[i].text == "(") ++i;
            std::vector<Token> inner;
            int depth = 1;
            for (; rest[i].kind != Token::end; ++i) {
                if (rest[i].kind == Token::punct && rest[i].text == "(") ++depth;
                if (rest[i].kind == Token::punct && rest[i].text == ")") {
                    if (--depth == 0) break;
                }
                inner.push_back(rest[i]);
            }
            inner.push_back({Token::end, "", 0});
            Struc::Piece piece;
            piece.repeat = static_cast<std::uint32_t>(repeat);
            piece.once = data_bytes(unit, inner);
            struc_offset_ = static_cast<std::uint16_t>(struc_offset_ +
                                                       piece.repeat * piece.once.size());
            owner.pieces.push_back(std::move(piece));
            return;
        }
        Struc::Piece piece;
        // `TYPE selfname`/`SIZE selfname` naming the struc currently being
        // defined can't resolve here -- the struc isn't closed, so
        // strucs_[open_struc_].size still reads 0. Evaluate it now (giving
        // the wrong placeholder) but flag the piece for close_struc to
        // overwrite once the real total is known (oracle-verified: MASM's
        // FTBLK probe writes the struc's final byte length here, not 0).
        const bool self_size_ref =
            rest.size() == 3 && rest[0].kind == Token::ident &&
            (upper(rest[0].text) == "TYPE" || upper(rest[0].text) == "SIZE") &&
            rest[1].kind == Token::ident &&
            upper(rest[1].text) == upper(open_struc_) &&
            rest[2].kind == Token::end;
        piece.once = data_bytes(unit, rest);
        if (self_size_ref) owner.self_size_patches.push_back(owner.pieces.size());
        // Record the per-item split: MASM's instance LIDATA nests one
        // block per comma item ($M_CRLF's `db 0DH,0AH`).
        {
            std::size_t k = 0;
            while (rest[k].kind != Token::end) {
                std::vector<Token> item;
                int depth = 0;
                for (; rest[k].kind != Token::end; ++k) {
                    if (rest[k].kind == Token::punct && rest[k].text == "(") ++depth;
                    if (rest[k].kind == Token::punct && rest[k].text == ")") --depth;
                    if (depth == 0 && rest[k].kind == Token::punct &&
                        rest[k].text == ",") {
                        ++k;
                        break;
                    }
                    item.push_back(rest[k]);
                }
                if (item.empty()) continue;
                item.push_back({Token::end, "", 0});
                piece.items.push_back(data_bytes(unit, item));
            }
        }
        struc_offset_ = static_cast<std::uint16_t>(struc_offset_ + piece.once.size());
        owner.pieces.push_back(std::move(piece));
    }

    // Parses `count DUP` where count is any constant expression; on
    // success advances `i` past DUP and stores the count.
    bool dup_count(std::vector<Token>& tokens, std::size_t& i, long* repeat) {
        // Prescan: only parse a count if DUP appears before the element's
        // end -- the expression parser reports errors and must not run on
        // plain values like `?`.
        bool has_dup = false;
        int depth = 0;
        for (std::size_t look = i; tokens[look].kind != Token::end; ++look) {
            if (tokens[look].kind == Token::punct) {
                if (tokens[look].text == "(") { ++depth; continue; }
                if (tokens[look].text == ")") { --depth; continue; }
                // A comma at the top level ends this element -- a
                // parenthesized count like `(SIZE SRHEAD) DUP(?)` keeps the
                // DUP visible past its own parentheses.
                if (tokens[look].text == "," && depth == 0) break;
            }
            if (depth == 0 && tokens[look].kind == Token::ident &&
                upper(tokens[look].text) == "DUP") {
                has_dup = true;
                break;
            }
        }
        if (!has_dup) return false;
        std::vector<Token> rest(tokens.begin() + static_cast<long>(i), tokens.end());
        std::size_t at = 0;
        const Value value = parse_additive(rest, at);
        if (at == 0) return false;
        if (rest[at].kind != Token::ident || upper(rest[at].text) != "DUP") return false;
        *repeat = value.value;
        i += at + 1;
        return true;
    }

    // `RecName <v1,v2,...>` packs record-field values (blank keeps the
    // preset) into the record's byte or word (MSGHAN's SF_BITS).
    bool record_instance_value(std::vector<Token>& tokens, std::size_t& i,
                               long* value, int* unit) {
        if (tokens[i].kind != Token::ident) return false;
        const auto record = records_.find(upper(tokens[i].text));
        if (record == records_.end()) return false;
        if (!(tokens[i + 1].kind == Token::punct && tokens[i + 1].text == "<")) {
            return false;
        }
        std::size_t k = i + 2;
        std::vector<std::vector<Token>> items(1);
        for (; tokens[k].kind != Token::end; ++k) {
            if (tokens[k].kind == Token::punct && tokens[k].text == ">") break;
            if (tokens[k].kind == Token::punct && tokens[k].text == ",") {
                items.emplace_back();
                continue;
            }
            items.back().push_back(tokens[k]);
        }
        if (tokens[k].kind != Token::end) ++k;  // '>'
        long packed = 0;
        const auto& fields = record->second.fields;
        for (std::size_t f = 0; f < fields.size(); ++f) {
            long field_value = fields[f].preset;
            if (f < items.size() && !items[f].empty()) {
                std::vector<Token> expr = items[f];
                expr.push_back({Token::end, "", 0});
                std::size_t used = 0;
                field_value = expression(expr, &used).value;
            }
            packed |= (field_value & ((1L << fields[f].width) - 1))
                      << fields[f].shift;
        }
        *value = packed;
        *unit = record->second.unit;
        i = k;
        return true;
    }

    // The bytes a data operand list denotes, `?` counting as zero.
    std::vector<std::uint8_t> data_bytes(int unit, std::vector<Token>& tokens) {
        {
            // A record instance packs to its own width regardless of the
            // directive's unit.
            std::size_t probe = 0;
            long packed = 0;
            int record_unit = 0;
            if (record_instance_value(tokens, probe, &packed, &record_unit) &&
                tokens[probe].kind == Token::end) {
                std::vector<std::uint8_t> out;
                for (int b = 0; b < record_unit; ++b) {
                    out.push_back(static_cast<std::uint8_t>(packed >> (8 * b)));
                }
                return out;
            }
        }
        // `(?)` is `?` here too (the argv struc's `argpointer DW (?)`).
        for (std::size_t k = 0; tokens[k].kind != Token::end; ++k) {
            if (tokens[k].kind == Token::punct && tokens[k].text == "(" &&
                tokens[k + 1].text == "?" && tokens[k + 1].kind != Token::str &&
                tokens[k + 2].kind == Token::punct && tokens[k + 2].text == ")" &&
                !(k > 0 && tokens[k - 1].kind == Token::ident &&
                  upper(tokens[k - 1].text) == "DUP")) {
                tokens.erase(tokens.begin() + static_cast<long>(k + 2));
                tokens.erase(tokens.begin() + static_cast<long>(k));
            }
        }
        std::vector<std::uint8_t> out;
        std::size_t i = 0;
        while (tokens[i].kind != Token::end) {
            if (tokens[i].kind == Token::punct && tokens[i].text == ",") {
                ++i;
                continue;
            }
            if (long repeat = 0; dup_count(tokens, i, &repeat)) {
                if (tokens[i].kind == Token::punct && tokens[i].text == "(") ++i;
                std::vector<Token> inner;
                int depth = 1;
                for (; tokens[i].kind != Token::end; ++i) {
                    if (tokens[i].kind == Token::punct && tokens[i].text == "(") ++depth;
                    if (tokens[i].kind == Token::punct && tokens[i].text == ")") {
                        if (--depth == 0) {
                            ++i;
                            break;
                        }
                    }
                    inner.push_back(tokens[i]);
                }
                inner.push_back({Token::end, "", 0});
                const std::vector<std::uint8_t> once = data_bytes(unit, inner);
                for (long n = 0; n < repeat; ++n) out.insert(out.end(), once.begin(), once.end());
                continue;
            }
            if (tokens[i].kind == Token::str && unit == 1 && tokens[i].text.size() > 1) {
                out.insert(out.end(), tokens[i].text.begin(), tokens[i].text.end());
                ++i;
                continue;
            }
            if (tokens[i].text == "?" &&
                (tokens[i].kind == Token::punct || tokens[i].kind == Token::ident)) {
                out.insert(out.end(), static_cast<std::size_t>(unit), 0);
                ++i;
                continue;
            }
            std::vector<Token> expr(tokens.begin() + static_cast<long>(i), tokens.end());
            std::size_t used = 0;
            const long value = expression(expr, &used).value;
            for (int b = 0; b < unit; ++b) {
                out.push_back(static_cast<std::uint8_t>(value >> (8 * b)));
            }
            i += used;
            if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
            else break;
        }
        return out;
    }

    void close_struc(const std::string& name) {
        Struc& owner = strucs_[open_struc_];
        owner.size = struc_offset_;
        for (std::size_t idx : owner.self_size_patches) {
            Struc::Piece& piece = owner.pieces[idx];
            for (std::size_t b = 0; b < piece.once.size(); ++b) {
                piece.once[b] = static_cast<std::uint8_t>(struc_offset_ >> (8 * b));
            }
        }
        Symbol symbol;
        symbol.kind = Symbol::struc_type;
        symbol.spelling = name;
        symbol.value = struc_offset_;
        symbol.defined = true;
        symbols_[open_struc_] = symbol;
        open_struc_.clear();
    }

    // How many bytes a data directive's operand list occupies.
    int data_length(int unit, std::vector<Token>& tokens) {
        int total = 0;
        std::size_t i = 0;
        while (tokens[i].kind != Token::end) {
            if (tokens[i].kind == Token::punct && tokens[i].text == ",") {
                ++i;
                continue;
            }
            // `n dup(...)`
            if (long repeat = 0; dup_count(tokens, i, &repeat)) {
                int depth = 0;
                int inner_units = 0;
                for (; tokens[i].kind != Token::end; ++i) {
                    if (tokens[i].kind == Token::punct && tokens[i].text == "(") ++depth;
                    else if (tokens[i].kind == Token::punct && tokens[i].text == ")") {
                        if (--depth == 0) {
                            ++i;
                            break;
                        }
                    } else if (depth > 0 && !(tokens[i].kind == Token::punct &&
                                              tokens[i].text == ",")) {
                        ++inner_units;
                    }
                }
                total += static_cast<int>(repeat) * unit * std::max(inner_units, 1);
                continue;
            }
            if (tokens[i].kind == Token::str && unit == 1 && tokens[i].text.size() > 1) {
                total += static_cast<int>(tokens[i].text.size());
                ++i;
                continue;
            }
            total += unit;
            ++i;
        }
        return total;
    }

    // True when a data item's relocatable terms all cancel, so the item is an
    // absolute constant rather than a relocation. A same-segment address
    // difference -- KDF keyboard tables' `DW LOGIC_END-$`, or a plain
    // `DW END-START` -- is such a case: the two labels share a frame, the
    // linker contributes nothing, and MASM emits the folded distance with no
    // fixup. Conservative: only a pure additive chain of labels / `$` /
    // constants is judged; any multiplicative or type/selector operator (which
    // this evaluator cannot net out cheaply) falls back to the fixup path.
    bool data_expression_absolute(const std::vector<Token>& tokens) {
        static const std::set<std::string> non_additive = {
            "MOD",  "SHL",    "SHR",  "OFFSET", "SEG",  "AND",   "OR",
            "XOR",  "NOT",    "EQ",   "NE",     "LT",   "LE",    "GT",
            "GE",   "PTR",    "DUP",  "SIZE",   "LENGTH", "TYPE", "WIDTH",
            "MASK", "THIS"};
        std::map<int, int> net;  // effective segment -> signed relocatable count
        int sign = 1;
        bool any_reloc = false;
        for (std::size_t k = 0; tokens[k].kind != Token::end; ++k) {
            const Token& tok = tokens[k];
            if (tok.kind == Token::punct) {
                if (tok.text == "+") { sign = 1; continue; }
                if (tok.text == "-") { sign = -1; continue; }
                return false;  // ( ) [ ] . : * / -- not a plain additive chain
            }
            if (tok.kind == Token::number) continue;  // constant term
            if (tok.kind == Token::str) return false;
            const std::string upper_text = upper(tok.text);
            if (non_additive.contains(upper_text)) return false;
            int seg = -2;
            if (tok.text == "$") {
                seg = current_segment_;
            } else {
                const auto symbol = symbols_.find(upper_text);
                if (symbol != symbols_.end() &&
                    symbol->second.kind == Symbol::label &&
                    symbol->second.segment >= 0 &&
                    symbol->second.extern_index == 0) {
                    seg = symbol->second.segment;
                }
                // any other ident (an EQU/constant or a plain decimal token) is
                // a constant term and contributes no relocation
            }
            if (seg != -2) {
                net[seg] += sign;
                any_reloc = true;
            }
        }
        if (!any_reloc) return false;
        for (const auto& [seg, count] : net) {
            if (count != 0) return false;
        }
        return true;
    }

    // Instruction operands may perform further arithmetic on an OFFSET
    // difference after its relocations have cancelled. RAMDRIVE uses both
    // `OFFSET end-OFFSET start` and `((OFFSET end-OFFSET start)+15)/16`.
    // They are absolute values even though a single OFFSET term would need a
    // fixup. Keep this deliberately narrow: only numeric arithmetic and
    // OFFSET-decorated labels/constants are admitted.
    bool instruction_expression_absolute(const std::vector<Token>& tokens) {
        std::map<int, int> net;
        int sign = 1;
        bool any_reloc = false;
        for (std::size_t k = 0; tokens[k].kind != Token::end; ++k) {
            const Token& tok = tokens[k];
            if (tok.kind == Token::number) continue;
            if (tok.kind == Token::str) return false;
            if (tok.kind == Token::punct) {
                if (tok.text == "+") { sign = 1; continue; }
                if (tok.text == "-") { sign = -1; continue; }
                if (tok.text == "(" || tok.text == ")" ||
                    tok.text == "*" || tok.text == "/") {
                    continue;
                }
                return false;
            }
            const std::string word = upper(tok.text);
            if (word == "OFFSET") continue;
            if (word == "$" && current_segment_ >= 0) {
                net[current_segment_] += sign;
                any_reloc = true;
                continue;
            }
            const auto symbol = symbols_.find(word);
            if (symbol != symbols_.end() &&
                symbol->second.kind == Symbol::label &&
                symbol->second.segment >= 0 &&
                symbol->second.extern_index == 0) {
                net[symbol->second.segment] += sign;
                any_reloc = true;
                continue;
            }
            if (symbol != symbols_.end() &&
                symbol->second.kind == Symbol::constant) {
                continue;
            }
            return false;
        }
        if (!any_reloc) return false;
        for (const auto& [segment, count] : net) {
            (void)segment;
            if (count != 0) return false;
        }
        return true;
    }

    void data_directive(int unit, std::vector<Token>& tokens) {
        data_use_ = true;
        data_directive_inner(unit, tokens);
        data_use_ = false;
    }

    void data_directive_inner(int unit, std::vector<Token>& tokens) {
        // Angle brackets around an item are decoration (NIBDOS's
        // `DW <OFFSET DOSGroup:LeaveDOS>`).
        for (std::size_t k = 0; tokens[k].kind != Token::end; ++k) {
            if (tokens[k].kind == Token::punct && tokens[k].text == "<") {
                int depth = 1;
                std::size_t close = k + 1;
                for (; tokens[close].kind != Token::end; ++close) {
                    if (tokens[close].kind != Token::punct) continue;
                    if (tokens[close].text == "<") ++depth;
                    if (tokens[close].text == ">" && --depth == 0) break;
                }
                if (tokens[close].kind == Token::end) break;
                const bool ends_item =
                    tokens[close + 1].kind == Token::end ||
                    (tokens[close + 1].kind == Token::punct &&
                     tokens[close + 1].text == ",");
                if (!ends_item) continue;
                tokens.erase(tokens.begin() + static_cast<long>(close));
                tokens.erase(tokens.begin() + static_cast<long>(k));
                --k;
            }
        }
        // `(?)` is `?` (COMMAND's `argpointer DW (?)`).
        for (std::size_t k = 0; tokens[k].kind != Token::end; ++k) {
            if (tokens[k].kind == Token::punct && tokens[k].text == "(" &&
                tokens[k + 1].text == "?" && tokens[k + 1].kind != Token::str &&
                tokens[k + 2].kind == Token::punct && tokens[k + 2].text == ")" &&
                !(k > 0 && tokens[k - 1].kind == Token::ident &&
                  upper(tokens[k - 1].text) == "DUP")) {
                tokens.erase(tokens.begin() + static_cast<long>(k + 2));
                tokens.erase(tokens.begin() + static_cast<long>(k));
            }
        }
        std::size_t i = 0;
        while (tokens[i].kind != Token::end) {
            if (tokens[i].kind == Token::punct && tokens[i].text == ",") {
                ++i;
                continue;
            }
            if (long repeat = 0; dup_count(tokens, i, &repeat)) {
                if (tokens[i].kind == Token::punct && tokens[i].text == "(") ++i;
                std::vector<Token> inner;
                int depth = 1;
                for (; tokens[i].kind != Token::end; ++i) {
                    if (tokens[i].kind == Token::punct && tokens[i].text == "(") ++depth;
                    if (tokens[i].kind == Token::punct && tokens[i].text == ")") {
                        if (--depth == 0) {
                            ++i;
                            break;
                        }
                    }
                    inner.push_back(tokens[i]);
                }
                inner.push_back({Token::end, "", 0});
                const bool uninitialized = std::all_of(
                    inner.begin(), inner.end(), [](const Token& token) {
                        return token.kind == Token::end ||
                               (token.kind == Token::punct && token.text == ",") ||
                               (token.text == "?" && token.kind != Token::str);
                    });
                if (uninitialized && current_segment_ >= 0) {
                    // DUP(?) occupies space but emits no LEDATA
                    // (QCURDIR.OBJ's data segment has none); each comma
                    // item counts (FIND's stack `dup(?,?)` is two words).
                    long items = 1;
                    for (const Token& token : inner) {
                        if (token.kind == Token::punct && token.text == ",") {
                            ++items;
                        }
                    }
                    Segment& home = segment();
                    home.location = static_cast<std::uint16_t>(
                        home.location + repeat * unit * items);
                    home.extent = std::max(home.extent, home.location);
                    continue;
                }
                // A nested DUP nests LIDATA blocks (CONST2's
                // `DB 5 DUP((SIZE sf_entry) DUP(0))` is repeat 5 { repeat
                // 59 { 1x0 content 00 } }).
                {
                    bool has_nested = false;
                    {
                        std::size_t probe = 0;
                        long nested_repeat = 0;
                        std::vector<Token> copy = inner;
                        if (dup_count(copy, probe, &nested_repeat)) {
                            has_nested = true;
                        }
                    }
                    if (has_nested) {
                        std::vector<std::uint8_t> block;
                        std::uint32_t span = 1;
                        const std::function<void(std::uint32_t,
                                                 std::vector<Token>&)>
                            encode = [&](std::uint32_t times,
                                         std::vector<Token>& body) {
                                block.push_back(static_cast<std::uint8_t>(times & 0xFF));
                                block.push_back(static_cast<std::uint8_t>(times >> 8));
                                span *= times;
                                std::size_t k = 0;
                                long inner_repeat = 0;
                                if (dup_count(body, k, &inner_repeat)) {
                                    if (body[k].kind == Token::punct &&
                                        body[k].text == "(") {
                                        ++k;
                                    }
                                    std::vector<Token> deeper;
                                    int nest = 1;
                                    for (; body[k].kind != Token::end; ++k) {
                                        if (body[k].kind == Token::punct &&
                                            body[k].text == "(") ++nest;
                                        if (body[k].kind == Token::punct &&
                                            body[k].text == ")") {
                                            if (--nest == 0) break;
                                        }
                                        deeper.push_back(body[k]);
                                    }
                                    deeper.push_back({Token::end, "", 0});
                                    block.push_back(0x01);  // one sub-block
                                    block.push_back(0x00);
                                    encode(static_cast<std::uint32_t>(inner_repeat),
                                           deeper);
                                    return;
                                }
                                std::vector<std::uint8_t> bytes =
                                    data_bytes(unit, body);
                                span *= static_cast<std::uint32_t>(bytes.size());
                                block.push_back(0x01);  // one literal block:
                                block.push_back(0x00);
                                block.push_back(0x01);  //   1 x literal
                                block.push_back(0x00);
                                block.push_back(0x00);
                                block.push_back(0x00);
                                block.push_back(
                                    static_cast<std::uint8_t>(bytes.size()));
                                block.insert(block.end(), bytes.begin(),
                                             bytes.end());
                            };
                        encode(static_cast<std::uint32_t>(repeat), inner);
                        if (current_segment_ >= 0) {
                            Segment& home = segment();
                            if (pass_ == 2) {
                                Segment::Item item;
                                item.start = home.location;
                                item.lidata = std::move(block);
                                item.sequence = ++emit_sequence_;
                                home.runs.push_back(std::move(item));
                            }
                            home.location = static_cast<std::uint16_t>(
                                home.location + span);
                            home.extent =
                                std::max(home.extent, home.location);
                        }
                        continue;
                    }
                }
                // Initialized DUP compresses into one LIDATA block
                // (OPEN.OBJ's dw 100 dup(0)). A lone relocatable label stays
                // zero in that block and gets one FIXUPP at the repeated
                // literal's byte position; the linker applies it to every
                // iteration (DUPLABEL.OBJ; MODEPARS's Sw_control tables).
                {
                    std::vector<Token> copy = inner;
                    std::vector<Segment::Fixup> item_fixups;
                    std::size_t label_at = 0;
                    const bool label_value =
                        copy.size() >= 2 && copy[0].kind == Token::ident &&
                        copy[1].kind == Token::end;
                    const bool offset_label_value =
                        copy.size() >= 3 && copy[0].kind == Token::ident &&
                        upper(copy[0].text) == "OFFSET" &&
                        copy[1].kind == Token::ident &&
                        copy[2].kind == Token::end;
                    if (offset_label_value) label_at = 1;
                    const auto target =
                        (label_value || offset_label_value)
                            ? symbols_.find(upper(copy[label_at].text))
                            : symbols_.end();
                    const bool relocatable =
                        (unit == 2 || unit == 4) && target != symbols_.end() &&
                        target->second.kind == Symbol::label &&
                        (target->second.extern_index > 0 ||
                         target->second.segment >= 0);
                    std::vector<std::uint8_t> once;
                    if (relocatable) {
                        const int loc = unit == 4 ? 3 : 1;
                        if (target->second.extern_index > 0) {
                            note_fixup(loc, 0, target->second.extern_index,
                                       false, 0);
                        } else {
                            note_fixup(loc, target->second.segment + 1, 0,
                                       false, target->second.value);
                        }
                        if (pass_ == 2 && !segment().fixups.empty()) {
                            Segment::Fixup fixup = segment().fixups.back();
                            segment().fixups.pop_back();
                            // Relative to the LIDATA block image: two repeat
                            // counts, two block counts, then the byte length.
                            fixup.location = segment_uses_32(segment()) ? 13 : 9;
                            item_fixups.push_back(std::move(fixup));
                        }
                        once.assign(static_cast<std::size_t>(unit), 0);
                    } else {
                        once = data_bytes(unit, copy);
                    }
                    emit_iterated(static_cast<std::uint32_t>(repeat), once);
                    if (pass_ == 2 && !item_fixups.empty()) {
                        segment().runs.back().lidata_fixups =
                            std::move(item_fixups);
                    }
                }
                continue;
            }
            // `dw offset [segname:]label` -- GETMSG's message index table
            // (`dw offset messages:Message0`): the qualifier only names the
            // frame, and the label's offset rides the fixup displacement.
            // Parentheses around the whole OFFSET item, or just its target,
            // are decoration too. INDEINI's JUMPFAR macro emits `DW (OFFSET
            // NEXT)`; CPSPI uses `DW OFFSET (HARD_SL1)`. Real MASM keeps the
            // relocation in both forms rather than folding the known offset
            // into the data word.
            if (unit == 2) {
                const auto matching_close = [&](std::size_t open) {
                    int depth = 1;
                    std::size_t close = open + 1;
                    for (; tokens[close].kind != Token::end; ++close) {
                        if (tokens[close].kind != Token::punct) continue;
                        if (tokens[close].text == "(") ++depth;
                        if (tokens[close].text == ")" && --depth == 0) break;
                    }
                    return close;
                };
                const auto ends_item = [&](std::size_t close) {
                    return tokens[close].kind != Token::end &&
                           (tokens[close + 1].kind == Token::end ||
                            (tokens[close + 1].kind == Token::punct &&
                             tokens[close + 1].text == ","));
                };
                if (tokens[i].kind == Token::punct && tokens[i].text == "(" &&
                    tokens[i + 1].kind == Token::ident &&
                    upper(tokens[i + 1].text) == "OFFSET") {
                    const std::size_t close = matching_close(i);
                    if (ends_item(close)) {
                        tokens.erase(tokens.begin() + static_cast<long>(close));
                        tokens.erase(tokens.begin() + static_cast<long>(i));
                    }
                }
                if (tokens[i].kind == Token::ident &&
                    upper(tokens[i].text) == "OFFSET" &&
                    tokens[i + 1].kind == Token::punct &&
                    tokens[i + 1].text == "(") {
                    const std::size_t close = matching_close(i + 1);
                    if (ends_item(close)) {
                        tokens.erase(tokens.begin() + static_cast<long>(close));
                        tokens.erase(tokens.begin() + static_cast<long>(i + 1));
                    }
                }
            }
            // A parenthesized OFFSET can be one relocatable term in a larger
            // data expression. INDEINS's long-condition macro emits
            // `DW (OFFSET DISPLAY)-(TEMP)`: the external stays the target,
            // the subtraction becomes the fixup displacement, and the data
            // word itself is zero (OFFDIFF.OBJ; INDEDMA's LJNE/LJA).
            if (unit == 2 && i + 3 < tokens.size() &&
                tokens[i].kind == Token::punct &&
                tokens[i].text == "(" && tokens[i + 1].kind == Token::ident &&
                upper(tokens[i + 1].text) == "OFFSET" &&
                tokens[i + 2].kind == Token::ident &&
                tokens[i + 3].kind == Token::punct &&
                tokens[i + 3].text == ")") {
                const auto target = symbols_.find(upper(tokens[i + 2].text));
                if (target != symbols_.end() &&
                    target->second.kind == Symbol::label &&
                    target->second.extern_index > 0) {
                    std::vector<Token> expr(
                        tokens.begin() + static_cast<long>(i), tokens.end());
                    std::size_t used = 0;
                    const long value = expression(expr, &used).value;
                    note_fixup(1, 0, target->second.extern_index, false,
                               value != 0 ? (value & 0xFFFF) : -1,
                               /*force_external_frame=*/true);
                    emit_word(0);
                    i += used;
                    if (tokens[i].kind == Token::punct && tokens[i].text == ",") {
                        ++i;
                    }
                    continue;
                }
            }
            if (unit == 2 && tokens[i].kind == Token::ident &&
                upper(tokens[i].text) == "OFFSET") {
                std::size_t j = i + 1;
                // A PTR cast adds nothing to the offset (CHKDISP's
                // `dw offset word ptr sernum+2`).
                if (tokens[j].kind == Token::ident && tokens[j + 1].kind == Token::ident &&
                    upper(tokens[j + 1].text) == "PTR") {
                    j += 2;
                }
                int qualifier_segment = 0;
                int qualifier_group = 0;
                if (tokens[j].kind == Token::ident &&
                    tokens[j + 1].kind == Token::punct && tokens[j + 1].text == ":" &&
                    (segment_index_for(upper(tokens[j].text)) > 0 ||
                     [&]{ for (const Group& g : groups_) if (g.name == upper(tokens[j].text)) return true; return false; }())) {
                    // The explicit qualifier names the frame: F_SEG for a
                    // segment (UINIT's `DW OFFSET ENVIRONMENT:ECOMSPEC`),
                    // F_GRP for a group.
                    qualifier_segment = segment_index_for(upper(tokens[j].text));
                    if (qualifier_segment == 0) {
                        for (std::size_t g = 0; g < groups_.size(); ++g) {
                            if (groups_[g].name == upper(tokens[j].text)) {
                                qualifier_group = static_cast<int>(g) + 1;
                            }
                        }
                    }
                    j += 2;  // skip `segname:`/`group:`
                }
                if (tokens[j].kind == Token::ident) {
                    const auto target = symbols_.find(upper(tokens[j].text));
                    if (target != symbols_.end() &&
                        target->second.kind == Symbol::label) {
                        std::vector<Token> expr(tokens.begin() + static_cast<long>(j),
                                                tokens.end());
                        std::size_t used = 0;
                        const long value = expression(expr, &used).value;
                        if (qualifier_group > 0) pending_frame_group_ = qualifier_group;
                        if (qualifier_segment > 0) {
                            pending_frame_segment_ = qualifier_segment;
                        }
                        if (target->second.extern_index > 0) {
                            note_fixup(1, 0, target->second.extern_index, false,
                                       value != 0 ? (value & 0xFFFF) : -1);
                        } else if (target->second.segment >= 0) {
                            note_fixup(1, target->second.segment + 1, 0, false, value);
                        }
                        emit_word(0);
                        i = j + used;
                        if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
                        continue;
                    }
                }
            }
            // `dw group:label` / `dd group:label`: the group frames an
            // offset16 or pointer32 fixup, offset riding as displacement
            // and data zeroed (EDLIN.OBJ's `dd dg:Start` -- pointer32,
            // frame F_GRP DG, target DATA, displacement 0476).
            if ((unit == 2 || unit == 4) && tokens[i].kind == Token::ident &&
                tokens[i + 1].kind == Token::punct && tokens[i + 1].text == ":" &&
                tokens[i + 2].kind == Token::ident) {
                int group_index = 0;
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    if (groups_[g].name == upper(tokens[i].text)) {
                        group_index = static_cast<int>(g) + 1;
                        break;
                    }
                }
                const auto target = symbols_.find(upper(tokens[i + 2].text));
                if (group_index > 0 && target != symbols_.end() &&
                    target->second.kind == Symbol::label) {
                    std::vector<Token> expr(tokens.begin() + static_cast<long>(i) + 2,
                                            tokens.end());
                    std::size_t used = 0;
                    const long value = expression(expr, &used).value;
                    pending_frame_group_ = group_index;
                    if (target->second.extern_index > 0) {
                        note_fixup(unit == 4 ? 3 : 1, 0,
                                   target->second.extern_index, false,
                                   value != 0 ? (value & 0xFFFF) : -1);
                    } else {
                        note_fixup(unit == 4 ? 3 : 1,
                                   target->second.segment + 1, 0, false, value);
                    }
                    emit_word(0);
                    if (unit == 4) emit_word(0);
                    i += 2 + used;
                    if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
                    continue;
                }
            }
            // `dw SEG segname/groupname`: the segment/group base, exactly like
            // the bare `dw segname` below -- a base (paragraph) fixup, data
            // zeroed (PRTINT2F's `DW SEG CSEG`, ASM2C's `dw seg dgroup`).
            if (unit == 2 && tokens[i].kind == Token::ident &&
                upper(tokens[i].text) == "SEG" && tokens[i + 1].kind == Token::ident &&
                (tokens[i + 2].kind == Token::end ||
                 (tokens[i + 2].kind == Token::punct && tokens[i + 2].text == ","))) {
                const std::string target = upper(tokens[i + 1].text);
                int grp = 0;
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    if (groups_[g].name == target) grp = static_cast<int>(g) + 1;
                }
                if (grp > 0) {
                    note_group_fixup(2, grp);
                } else {
                    note_fixup(2, segment_index_for(target), 0, false);
                }
                emit_word(0);
                i += 2;
                if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
                continue;
            }
            // `dw groupname`: a base fixup targeting the group (CHKDISP's
            // `dw dg` -- "segment of value").
            if (unit == 2 && tokens[i].kind == Token::ident &&
                (tokens[i + 1].kind == Token::end ||
                 (tokens[i + 1].kind == Token::punct && tokens[i + 1].text == ","))) {
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    if (groups_[g].name == upper(tokens[i].text)) {
                        note_group_fixup(2, static_cast<int>(g) + 1);
                        emit_word(0);
                        ++i;
                        break;
                    }
                }
                if (tokens[i].kind == Token::punct && tokens[i].text == ",") {
                    ++i;
                    continue;
                }
                if (tokens[i].kind == Token::end) continue;
            }
            // `dw label` zeroes the word and carries the target offset as
            // the fixup's explicit displacement; `dw segname` is a base
            // fixup with no displacement (SIGHAND.OBJ).
            if (unit == 2 && tokens[i].kind == Token::ident &&
                symbols_.contains(upper(tokens[i].text)) &&
                symbols_.at(upper(tokens[i].text)).kind == Symbol::label &&
                !(tokens[i + 1].kind == Token::end ||
                  (tokens[i + 1].kind == Token::punct && tokens[i + 1].text == ","))) {
                // `dw label+expr`: the whole element evaluates to an offset
                // that rides the fixup, data zeroed (F_FIRST's DTA table) --
                // unless the relocatable terms cancel (`DW LOGIC_END-$`), in
                // which case it is an absolute constant and no fixup is emitted.
                const Symbol& base = symbols_.at(upper(tokens[i].text));
                std::vector<Token> expr(tokens.begin() + static_cast<long>(i), tokens.end());
                std::size_t used = 0;
                const long value = expression(expr, &used).value;
                std::vector<Token> item(expr.begin(),
                                        expr.begin() + static_cast<long>(used));
                item.push_back({Token::end, "", 0});
                if (data_expression_absolute(item)) {
                    emit_word(static_cast<std::uint16_t>(value));
                    i += used;
                    if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
                    continue;
                }
                if (base.extern_index > 0) {
                    note_fixup(1, 0, base.extern_index, false, value != 0 ? (value & 0xFFFF) : -1);
                } else if (base.segment >= 0) {
                    note_fixup(1, base.segment + 1, 0, false, value);
                }
                emit_word(0);
                i += used;
                if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
                continue;
            }
            if (unit == 2 && tokens[i].kind == Token::ident &&
                (tokens[i + 1].kind == Token::end ||
                 (tokens[i + 1].kind == Token::punct && tokens[i + 1].text == ","))) {
                const std::string word = upper(tokens[i].text);
                bool is_segment = false;
                for (const Segment& candidate : segments_) {
                    if (candidate.name == word) is_segment = true;
                }
                const auto symbol = symbols_.find(word);
                if (is_segment) {
                    note_fixup(2, segment_index_for(word), 0, false);
                    emit_word(0);
                    ++i;
                    continue;
                }
                if (symbol != symbols_.end() && symbol->second.kind == Symbol::label) {
                    if (symbol->second.extern_index > 0) {
                        note_fixup(1, 0, symbol->second.extern_index, false, 0);
                    } else if (symbol->second.segment >= 0) {
                        note_fixup(1, symbol->second.segment + 1, 0, false,
                                   symbol->second.value);
                    }
                    emit_word(0);
                    ++i;
                    continue;
                }
                if (symbol == symbols_.end() && pass_ == 1) {
                    // forward label reference: sized now, resolved in pass 2
                    emit_word(0);
                    ++i;
                    continue;
                }
            }
            if (unit == 4 && tokens[i].kind == Token::ident &&
                (tokens[i + 1].kind == Token::end ||
                 (tokens[i + 1].kind == Token::punct && tokens[i + 1].text == ","))) {
                // `dd label`: a pointer32 fixup, data zeroed, the offset
                // riding as the displacement (MSBIO1's device-header
                // chain `DD AUXDEV2` -> CODE:0192).
                const auto symbol = symbols_.find(upper(tokens[i].text));
                if (symbol != symbols_.end() &&
                    symbol->second.kind == Symbol::label) {
                    if (symbol->second.extern_index > 0) {
                        note_fixup(3, 0, symbol->second.extern_index, false, 0);
                    } else if (symbol->second.segment >= 0) {
                        note_fixup(3, symbol->second.segment + 1, 0, false,
                                   symbol->second.value);
                    }
                    emit_word(0);
                    emit_word(0);
                    ++i;
                    if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
                    continue;
                }
                if (symbol == symbols_.end() && pass_ == 1) {
                    emit_word(0);
                    emit_word(0);
                    ++i;
                    if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
                    continue;
                }
            }
            if (tokens[i].kind == Token::str && unit == 1 && tokens[i].text.size() > 1) {
                for (char c : tokens[i].text) emit_byte(static_cast<std::uint8_t>(c));
                ++i;
                continue;
            }
            {
                long packed = 0;
                int record_unit = 0;
                if (record_instance_value(tokens, i, &packed, &record_unit)) {
                    for (int b = 0; b < record_unit; ++b) {
                        emit_byte(static_cast<std::uint8_t>(packed >> (8 * b)));
                    }
                    if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
                    continue;
                }
            }
            if (tokens[i].text == "?" &&
                (tokens[i].kind == Token::punct || tokens[i].kind == Token::ident)) {
                // A scalar `?` emits zeros (QFSINFO's data segment holds
                // eight of them); only DUP(?) blocks skip LEDATA.
                for (int b = 0; b < unit; ++b) emit_byte(0);
                ++i;
                continue;
            }
            // A byte initialized from an external remains zero in the LEDATA
            // and carries a low-byte relocation framed by the external itself
            // (ABSBYTE.OBJ; GEN_COMS's ABS E_CR/E_LF strings).
            if (unit == 1 && tokens[i].kind == Token::ident &&
                (tokens[i + 1].kind == Token::end ||
                 (tokens[i + 1].kind == Token::punct &&
                  tokens[i + 1].text == ","))) {
                const auto symbol = symbols_.find(upper(tokens[i].text));
                if (symbol != symbols_.end() &&
                    symbol->second.kind == Symbol::label &&
                    symbol->second.extern_index > 0) {
                    note_fixup(0, 0, symbol->second.extern_index, false, 0);
                    emit_byte(0);
                    ++i;
                    if (tokens[i].kind == Token::punct &&
                        tokens[i].text == ",") {
                        ++i;
                    }
                    continue;
                }
            }
            std::vector<Token> expr(tokens.begin() + static_cast<long>(i), tokens.end());
            std::size_t used = 0;
            const long value = expression(expr, &used).value;
            for (int b = 0; b < unit; ++b) emit_byte(static_cast<std::uint8_t>(value >> (8 * b)));
            i += used;
            if (tokens[i].kind == Token::punct && tokens[i].text == ",") ++i;
            else break;
        }
    }

    // ------------------------------------------------------------ symbols
    // MASM front-inserts each name into its hash chain when the name FIRST
    // enters the symbol table -- at its definition when that precedes the
    // PUBLIC statement (RECOVER defines sec_count before data_start_high
    // and PUBLICs them in the opposite order; the PUBDEF follows the
    // definitions).
    void note_name(const std::string& key) {
        if (pass_ == 1 && !name_first_seen_.contains(key)) {
            name_first_seen_[key] = ++name_seen_counter_;
        }
    }

    void define_label(const std::string& spelling, bool far_label, int size = 0,
                      long addend = 0) {
        const std::string key = upper(spelling);
        note_name(key);
        Symbol symbol;
        symbol.kind = Symbol::label;
        symbol.spelling = spelling;
        symbol.segment = current_segment_;
        symbol.value = current_segment_ >= 0 ? segment().location + addend : 0;
        symbol.far_label = far_label;
        symbol.size = size;
        symbol.defined = true;
        // The PUBDEF group is decided at definition time: the group CS is
        // ASSUMEd to when it contains this segment (ATTRIBA defines
        // `vector` under DGROUP and `xcexit` under PGROUP, both in _TEXT),
        // else the first group containing the segment.
        if (current_segment_ >= 0) {
            const std::string& seg_name =
                segments_[static_cast<std::size_t>(current_segment_)].name;
            const auto in_group = [&](int group_index) {
                if (group_index <= 0) return false;
                const auto& members =
                    groups_[static_cast<std::size_t>(group_index) - 1].members;
                return std::find(members.begin(), members.end(), seg_name) !=
                       members.end();
            };
            // Data labels (a nonzero unit: dd/dw/db) reach through DS,
            // code labels through CS -- ATTRIBA's `vector dd 0` takes
            // DGROUP while XCEXIT in the same segment takes PGROUP.
            // The reaching register's ASSUMEd group -- DS for data
            // labels, CS for code -- applies even when the segment is
            // not a member of THAT group (ATTRIBA's `vector dd 0` in
            // _TEXT, a PGROUP member, takes DGROUP) -- but only when the
            // defining segment belongs to SOME group: XCOPY's
            // SAV_INT24_OFF in ungrouped CSEG under DS:DGROUP carries
            // group 0. So does a segment-ASSUMEd register (ATTRIBA's
            // _DATA publics under `assume ds:psp`).
            bool member_of_any = false;
            for (std::size_t g = 0; g < groups_.size(); ++g) {
                if (in_group(static_cast<int>(g) + 1)) member_of_any = true;
            }
            symbol.pub_group =
                member_of_any
                    ? (size > 0 ? assume_group_[3] : assume_group_[1])
                    : 0;
        }
        if (pass_ == 2) {
            const auto known = symbols_.find(key);
            if (known != symbols_.end() && known->second.kind == Symbol::label &&
                known->second.value != symbol.value && known->second.segment == symbol.segment) {
                fail("phase error on " + spelling);
            }
        }
        defined_this_pass_.insert(key);
        symbols_[key] = symbol;
    }

    void define_data_label(const std::string& spelling, int unit) {
        define_label(spelling, false, unit);
    }

    // -------------------------------------------------------- expressions
    struct Value {
        long value = 0;
        bool known = true;
    };

    Value expression(std::vector<Token>& tokens) {
        std::size_t at = 0;
        return parse_additive(tokens, at);
    }
    Value expression(std::vector<Token>& tokens, std::size_t* used) {
        std::size_t at = 0;
        const Value value = parse_additive(tokens, at);
        *used = at;
        return value;
    }

    Value parse_additive(std::vector<Token>& tokens, std::size_t& at) {
        Value left = parse_not(tokens, at);
        while (tokens[at].kind == Token::ident &&
               (upper(tokens[at].text) == "AND" || upper(tokens[at].text) == "OR" ||
                upper(tokens[at].text) == "XOR")) {
            const std::string what = upper(tokens[at].text);
            ++at;
            const Value right = parse_not(tokens, at);
            if (what == "AND") left.value &= right.value;
            else if (what == "OR") left.value |= right.value;
            else left.value ^= right.value;
            left.known = left.known && right.known;
        }
        return left;
    }

    // MASM binds NOT looser than the arithmetic and relational operators but
    // tighter than AND/OR/XOR (oracle: `NOT 80H + 40H` is FF3F = NOT(80+40),
    // while `NOT 80H AND 40H` is 0040 = (NOT 80) AND 40). So COMMSUBS's
    // `AND ES:KB_FLAG_3,NOT RD_ID+LC_AB` masks with NOT(80h+40h)=3F, not
    // (NOT 80h)+40h=BF. A NOT that trails a tighter operator (`a + NOT b`,
    // `LOW NOT x`) is still handled at the primary level.
    Value parse_not(std::vector<Token>& tokens, std::size_t& at) {
        if (tokens[at].kind == Token::ident && upper(tokens[at].text) == "NOT") {
            ++at;
            Value inner = parse_not(tokens, at);
            inner.value = ~inner.value & 0xFFFF;
            return inner;
        }
        return parse_relational(tokens, at);
    }

    // MASM's relational operators yield -1 (true) or 0 -- GETMSG's
    // `if MacroState NE 0` message state machine.
    Value parse_relational(std::vector<Token>& tokens, std::size_t& at) {
        Value left = parse_sum(tokens, at);
        while (tokens[at].kind == Token::ident &&
               (upper(tokens[at].text) == "EQ" || upper(tokens[at].text) == "NE" ||
                upper(tokens[at].text) == "LT" || upper(tokens[at].text) == "LE" ||
                upper(tokens[at].text) == "GT" || upper(tokens[at].text) == "GE")) {
            const std::string what = upper(tokens[at].text);
            ++at;
            const Value right = parse_sum(tokens, at);
            bool truth = false;
            if (what == "EQ") truth = left.value == right.value;
            else if (what == "NE") truth = left.value != right.value;
            else if (what == "LT") truth = left.value < right.value;
            else if (what == "LE") truth = left.value <= right.value;
            else if (what == "GT") truth = left.value > right.value;
            else truth = left.value >= right.value;
            left.value = truth ? -1 : 0;
            left.known = left.known && right.known;
        }
        return left;
    }

    Value parse_sum(std::vector<Token>& tokens, std::size_t& at) {
        Value left = parse_multiplicative(tokens, at);
        while (tokens[at].kind == Token::punct &&
               (tokens[at].text == "+" || tokens[at].text == "-")) {
            const bool add = tokens[at].text == "+";
            ++at;
            const Value right = parse_multiplicative(tokens, at);
            left.value = add ? left.value + right.value : left.value - right.value;
            left.known = left.known && right.known;
        }
        return left;
    }

    Value parse_multiplicative(std::vector<Token>& tokens, std::size_t& at) {
        Value left = parse_primary(tokens, at);
        while ((tokens[at].kind == Token::punct &&
                (tokens[at].text == "*" || tokens[at].text == "/")) ||
               (tokens[at].kind == Token::ident &&
                (upper(tokens[at].text) == "MOD" || upper(tokens[at].text) == "SHL" ||
                 upper(tokens[at].text) == "SHR"))) {
            const std::string what = upper(tokens[at].text);
            ++at;
            const Value right = parse_primary(tokens, at);
            if (what == "*") left.value *= right.value;
            else if (what == "/") left.value = right.value == 0 ? 0 : left.value / right.value;
            else if (what == "MOD") left.value = right.value == 0 ? 0 : left.value % right.value;
            else if (what == "SHL") left.value <<= right.value;
            else if (what == "SHR") left.value >>= right.value;
            left.known = left.known && right.known;
        }
        return left;
    }

    // A bare struc NAME in an expression is its base, zero -- but the
    // operand of SIZE/TYPE/LENGTH stays the name (IOCTL_REQ's
    // `(SIZE SRHEAD) DUP(?)`).
    void zero_bare_struc_names(std::vector<Token>& toks) const {
        for (std::size_t k = 0; k < toks.size(); ++k) {
            if (toks[k].kind != Token::ident) continue;
            if (k > 0 && toks[k - 1].kind == Token::ident) {
                const std::string previous = upper(toks[k - 1].text);
                if (previous == "SIZE" || previous == "TYPE" ||
                    previous == "LENGTH" || previous == "WIDTH" ||
                    previous == "MASK" || previous == "OFFSET") {
                    continue;
                }
            }
            const auto known = symbols_.find(upper(toks[k].text));
            if (known != symbols_.end() &&
                known->second.kind == Symbol::struc_type) {
                toks[k] = {Token::number, "0", 0};
            }
        }
    }

    // Numeric resolution of a text equate's body, used only from
    // expression context (never for the register/PTR aliases whose
    // splice-as-text semantics matter elsewhere): strips a leading
    // `type PTR` cast and reads struc NAMES as zero.
    bool evaluate_text_equate(const std::string& word, long* out) {
        const auto body = text_equates_.find(word);
        if (body == text_equates_.end()) return false;
        std::string lex_error;
        std::vector<Token> toks = Lexer::line(body->second, &lex_error);
        if (!lex_error.empty()) return false;
        while (toks.size() >= 2 && toks[0].kind == Token::ident &&
               (upper(toks[0].text) == "BYTE" || upper(toks[0].text) == "WORD" ||
                upper(toks[0].text) == "DWORD") &&
               toks[1].kind == Token::ident && upper(toks[1].text) == "PTR") {
            toks.erase(toks.begin(), toks.begin() + 2);
        }
        if (toks.empty() || toks[0].kind == Token::end) return false;
        zero_bare_struc_names(toks);
        const std::size_t previous_errors = errors_.size();
        std::size_t used = 0;
        const Value value = expression(toks, &used);
        if (!value.known || toks[used].kind != Token::end ||
            errors_.size() != previous_errors) {
            errors_.resize(previous_errors);
            return false;
        }
        *out = value.value;
        return true;
    }

    Value parse_primary(std::vector<Token>& tokens, std::size_t& at) {
        const Token& token = tokens[at];
        if (token.kind == Token::ident && token.text == "$") {
            // `$` is the location counter (_MSGRET's `JMP $+3`).
            ++at;
            return {current_segment_ >= 0 ? static_cast<long>(segment().location)
                                          : 0,
                    true};
        }
        if (token.kind == Token::ident && upper(token.text) == "THIS" &&
            at + 1 < tokens.size() && tokens[at + 1].kind == Token::ident) {
            // `THIS BYTE`/`THIS WORD` inside an expression is the location
            // counter carrying a type; numerically it is `$` (MEMM's
            // `SIG_LENGTH equ (this byte - memmsig)`, `(THIS BYTE-CMD_TABLE)/2`).
            const std::string type = upper(tokens[at + 1].text);
            if (type == "BYTE" || type == "WORD" || type == "DWORD" ||
                type == "FWORD" || type == "QWORD" || type == "TBYTE") {
                at += 2;
                return {current_segment_ >= 0 ? static_cast<long>(segment().location)
                                              : 0,
                        true};
            }
        }
        if (token.kind == Token::number || (token.kind == Token::str && token.text.size() <= 2)) {
            long value = token.value;
            if (token.kind == Token::str && token.text.size() == 2) {
                value = (static_cast<unsigned char>(token.text[0]) << 8) |
                        static_cast<unsigned char>(token.text[1]);
            }
            ++at;
            return {value, true};
        }
        if (token.kind == Token::punct && token.text == "(") {
            ++at;
            const Value inner = parse_additive(tokens, at);
            if (tokens[at].kind == Token::punct && tokens[at].text == ")") ++at;
            return inner;
        }
        if (token.kind == Token::punct && token.text == "-") {
            ++at;
            Value inner = parse_primary(tokens, at);
            inner.value = -inner.value;
            return inner;
        }
        if (token.kind == Token::punct && token.text == "+") {
            // Unary plus (MSAUX's `add sp,+2`).
            ++at;
            return parse_primary(tokens, at);
        }
        if (token.kind == Token::ident && upper(token.text) == "NOT") {
            ++at;
            Value inner = parse_primary(tokens, at);
            inner.value = ~inner.value & 0xFFFF;
            return inner;
        }
        if (token.kind == Token::ident &&
            (upper(token.text) == "LOW" || upper(token.text) == "HIGH")) {
            // LOW/HIGH byte selectors (FCB's `db LOW (NOT (fChk))`).
            const bool low = upper(token.text) == "LOW";
            ++at;
            Value inner = parse_primary(tokens, at);
            inner.value = low ? (inner.value & 0xFF) : ((inner.value >> 8) & 0xFF);
            return inner;
        }

        if (token.kind == Token::ident) {
            const std::string word = upper(token.text);
            if (word == "MASK" || word == "WIDTH") {
                ++at;
                const auto field = record_fields_.find(upper(tokens[at].text));
                ++at;
                if (field == record_fields_.end()) return {0, false};
                const auto [width, shift] = field->second;
                if (word == "WIDTH") return {width, true};
                return {((1L << width) - 1) << shift, true};
            }
            if (word == "SIZE" || word == "TYPE" || word == "LENGTH") {
                ++at;
                const std::string target = upper(tokens[at].text);
                ++at;
                // SIZE/TYPE of a STRUC name is its byte length -- IOCTL_REQ's
                // `DB (SIZE SRHEAD) DUP(?)` reserves the 13-byte request
                // header (DEVSYM.INC), putting MAJORFUNCTION at offset 0xd.
                if (word != "LENGTH") {
                    const auto structure = strucs_.find(target);
                    if (structure != strucs_.end()) {
                        return {static_cast<long>(structure->second.size), true};
                    }
                }
                const auto known = symbols_.find(target);
                // SIZE of a data label is LENGTH * TYPE (TREE's `SIZE
                // FIX_DTA_FILN` = 13), LENGTH its leading DUP count. A STRUC
                // field carries the same size_total, so SIZE/LENGTH of it read
                // its own width, not its offset (GRAFTABL's `SIZE LANCHAR`).
                if (known != symbols_.end() &&
                    (known->second.kind == Symbol::label ||
                     known->second.struc_field)) {
                    if (known->second.size_total > 0) {
                        if (word == "SIZE") {
                            return {known->second.size_total, true};
                        }
                        if (word == "LENGTH") {
                            const long unit =
                                known->second.size > 0 ? known->second.size : 1;
                            return {known->second.size_total / unit, true};
                        }
                    }
                    // TYPE of any data label is its unit (DISKCOMP's
                    // `MOV CX,TYPE VOL_SERIAL` through the DWORD alias).
                    if (word == "TYPE" && known->second.size > 0) {
                        return {known->second.size, true};
                    }
                }
                // TYPE of a struc FIELD is the field's width too.
                if (known != symbols_.end() && word == "TYPE" &&
                    known->second.kind == Symbol::constant &&
                    known->second.size > 0) {
                    return {known->second.size, true};
                }
                if (known != symbols_.end()) return {known->second.value, true};
                return {0, pass_ == 2 ? false : true};
            }
            if (word == "OFFSET") {
                ++at;
                // A group/segment qualifier adds nothing to the VALUE
                // (MSDOSME's `[BP+OFFSET DOSGROUP:ESCFUNC]`).
                if (tokens[at].kind == Token::ident &&
                    tokens[at + 1].kind == Token::punct &&
                    tokens[at + 1].text == ":" &&
                    (segment_index_for(upper(tokens[at].text)) > 0 ||
                     [&] {
                         for (const Group& g : groups_) {
                             if (g.name == upper(tokens[at].text)) return true;
                         }
                         return false;
                     }())) {
                    at += 2;
                }
                // OFFSET of a struc TYPE selector is zero-based -- the
                // type's symbol carries its size, which is not an offset.
                Value inner;
                if (tokens[at].kind == Token::ident &&
                    symbols_.contains(upper(tokens[at].text)) &&
                    symbols_.at(upper(tokens[at].text)).kind ==
                        Symbol::struc_type) {
                    ++at;
                    inner = {0, true};
                } else {
                    inner = parse_primary(tokens, at);
                }
                // `.field` chains add their offsets (PATH2 subtracts
                // `OFFSET arg_unit.argbuf` to cancel a field addend).
                while (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                       tokens[at + 1].kind == Token::ident) {
                    const auto field = symbols_.find(upper(tokens[at + 1].text));
                    if (field == symbols_.end()) break;
                    inner.value += field->second.value;
                    at += 2;
                }
                return inner;
            }
            // A bare type name is its size in bytes: DEBUG's
            // `[DISADD+WORD]` and PARSE.ASM's `[si+byte]` step by 2 and 1.
            if ((word == "BYTE" || word == "WORD" || word == "DWORD") &&
                tokens[at + 1].kind == Token::ident &&
                upper(tokens[at + 1].text) == "PTR") {
                // A PTR cast passes the value through in numeric context
                // (MSBOOT's `if $ le BIOS$_L` after the equate splice).
                at += 2;
                return parse_primary(tokens, at);
            }
            if (word == "BYTE" || word == "WORD" || word == "DWORD" ||
                word == "QWORD" || word == "TBYTE") {
                if (!(tokens[at + 1].kind == Token::ident &&
                      upper(tokens[at + 1].text) == "PTR") &&
                    !symbols_.contains(word)) {
                    ++at;
                    const long size = word == "BYTE"    ? 1
                                      : word == "WORD"  ? 2
                                      : word == "DWORD" ? 4
                                      : word == "QWORD" ? 8
                                                        : 10;
                    return {size, true};
                }
            }
            ++at;
            const auto known = symbols_.find(word);
            if (known != symbols_.end() && known->second.defined) {
                return {known->second.value,
                        pass_ == 2 ||
                            !known->second.forward_derived_constant};
            }
            // A text equate that evaluates numerically is a value here:
            // IFSSYM's `LENGTH_INIT EQU IFSR_RESSIZE-IFSRH+2` chains
            // through `IFSR_RESSIZE = WORD PTR IFSR_FCN_PARMS+4` -- the
            // leading cast strips, and a struc NAME is its base, zero.
            if (long spliced = 0; evaluate_text_equate(word, &spliced)) {
                return {spliced, true};
            }

            if (pass_ == 2) {
                const auto later = symbols_.find(word);
                if (later != symbols_.end()) return {later->second.value, true};
                // A segment name evaluates as its base, offset zero
                // (COMP2's `if ($-cseg) mod 16`).
                if (segment_index_for(word) > 0) return {0, true};
                fail("undefined symbol " + token.text);
            }
            return {0, false};
        }
        if (pass_ == 2) fail("unparsable expression at " + token.text);
        ++at;
        return {0, false};
    }

    // ----------------------------------------------------------- operands
    std::vector<Operand> parse_operands(std::vector<Token>& tokens) {
        std::vector<Operand> operands;
        std::size_t at = 0;
        while (tokens[at].kind != Token::end) {
            operands.push_back(parse_operand(tokens, at));
            if (tokens[at].kind == Token::punct && tokens[at].text == ",") ++at;
            else break;
        }
        return operands;
    }

    Operand parse_operand(std::vector<Token>& tokens, std::size_t& at) {
        Operand operand;
        // `$`-based expressions resolve to the location counter
        // (SYSMSG.INC's `JMP $+3`).
        if (tokens[at].kind == Token::ident && tokens[at].text == "$") {
            std::vector<Token> expr(tokens.begin() + static_cast<long>(at),
                                    tokens.end());
            std::size_t used = 0;
            const Value value = expression(expr, &used);
            at += used;
            operand.kind = Operand::imm;
            operand.value = value.value;
            operand.value_known = value.known;
            return operand;
        }
        // size override (BYTE/WORD/... PTR), SHORT, and a segment override
        // may appear in either order before the operand proper. Parentheses
        // wrapping the whole remainder are decoration (MSDOS's
        // `MOV DS:(BYTE PTR 4),AL`).
        while (tokens[at].kind == Token::ident ||
               (tokens[at].kind == Token::punct &&
                (tokens[at].text == "(" || tokens[at].text == "<"))) {
            if (tokens[at].kind == Token::punct) {
                const std::string open = tokens[at].text;
                const std::string shut = open == "(" ? ")" : ">";
                int depth = 0;
                std::size_t close = at;
                for (std::size_t look = at; tokens[look].kind != Token::end; ++look) {
                    if (tokens[look].kind != Token::punct) continue;
                    if (tokens[look].text == open) ++depth;
                    if (tokens[look].text == shut && --depth == 0) {
                        close = look;
                        break;
                    }
                }
                const bool wraps =
                    close > at &&
                    (tokens[close + 1].kind == Token::end ||
                     (tokens[close + 1].kind == Token::punct &&
                      tokens[close + 1].text == ","));
                if (!wraps) break;
                tokens.erase(tokens.begin() + static_cast<long>(close));
                tokens.erase(tokens.begin() + static_cast<long>(at));
                continue;
            }
            const std::string word = upper(tokens[at].text);
            if ((word == "BYTE" || word == "WORD" || word == "DWORD" || word == "QWORD" ||
                 word == "FWORD" || word == "TBYTE") &&
                tokens[at + 1].kind == Token::ident && upper(tokens[at + 1].text) == "PTR") {
                operand.size = word == "BYTE" ? 1 : word == "WORD" ? 2 : 4;
                operand.size_explicit = true;
                at += 2;
                continue;
            }
            if ((word == "FAR" || word == "NEAR") &&
                tokens[at + 1].kind == Token::ident && upper(tokens[at + 1].text) == "PTR") {
                // `CALL FAR PTR label` (SYSMSG's class-1 call) forces the
                // far form regardless of the label's own distance.
                operand.far_override = word == "FAR";
                at += 2;
                continue;
            }
            if (word == "SHORT") {  // jump distance hint
                ++at;
                operand.size = -1;
                continue;
            }
            if (sreg().contains(word) && tokens[at + 1].kind == Token::punct &&
                tokens[at + 1].text == ":") {
                operand.segment_override = sreg().at(word);
                at += 2;
                continue;
            }
            if (tokens[at + 1].kind == Token::punct && tokens[at + 1].text == ":" &&
                segment_index_for(word) > 0 && !symbols_.contains(word)) {
                // `segname:label` reaches the label through that segment's
                // ASSUME (COMMAND's psdata_seg:$P_Flags).
                const int target = segment_index_for(word) - 1;
                if (assumes_[3] == target) operand.segment_override = -1;
                else if (assumes_[2] == target) operand.segment_override = 2;
                else if (assumes_[0] == target) operand.segment_override = 0;
                else if (assumes_[1] == target) operand.segment_override = 1;
                at += 2;
                continue;
            }
            if (tokens[at + 1].kind == Token::punct && tokens[at + 1].text == ":" &&
                !symbols_.contains(word)) {
                // `groupname:label` (XCOPY's dg:XM_HANDLE_RET) reaches the
                // label through whichever register ASSUMEs the group.
                int group_index = 0;
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    if (groups_[g].name == word) {
                        group_index = static_cast<int>(g) + 1;
                        break;
                    }
                }
                if (group_index > 0) {
                    if (assume_group_[3] == group_index) {
                        operand.segment_override = -1;
                    } else if (assume_group_[2] == group_index) {
                        operand.segment_override = 2;
                    } else if (assume_group_[0] == group_index) {
                        operand.segment_override = 0;
                    } else if (assume_group_[1] == group_index) {
                        operand.segment_override = 1;
                    }
                    operand.prefix_group = group_index;
                    at += 2;
                    continue;
                }
            }
            break;
        }
        // Resolve a complete same-segment OFFSET difference before the
        // ordinary OFFSET path attaches a fixup to its first term. This must
        // precede the parenthesis normalization below so nested arithmetic
        // remains intact for numeric evaluation.
        {
            std::size_t end = at;
            int depth = 0;
            while (tokens[end].kind != Token::end) {
                if (tokens[end].kind == Token::punct) {
                    if (tokens[end].text == "(" || tokens[end].text == "[") {
                        ++depth;
                    } else if (tokens[end].text == ")" ||
                               tokens[end].text == "]") {
                        --depth;
                    } else if (tokens[end].text == "," && depth == 0) {
                        break;
                    }
                }
                ++end;
            }
            std::vector<Token> expr(tokens.begin() + static_cast<long>(at),
                                    tokens.begin() + static_cast<long>(end));
            expr.push_back({Token::end, "", 0});
            if (instruction_expression_absolute(expr)) {
                std::size_t used = 0;
                const Value value = expression(expr, &used);
                operand.kind = Operand::imm;
                operand.value = value.value;
                operand.value_known = value.known;
                operand.from_offset = std::any_of(
                    expr.begin(), expr.end(), [](const Token& token) {
                        return token.kind == Token::ident &&
                               upper(token.text) == "OFFSET";
                    });
                if (pass_ == 2 && !operand.from_offset) {
                    for (const Token& token : expr) {
                        if (token.kind != Token::ident) continue;
                        const std::string w = upper(token.text);
                        if (symbols_.contains(w) &&
                            (symbols_.at(w).kind == Symbol::label ||
                             symbols_.at(w).kind == Symbol::constant) &&
                            !defined_this_pass_.contains(w)) {
                            operand.forward_const = true;
                            break;
                        }
                    }
                }
                at = end;
                return operand;
            }
        }
        // Parens around an OFFSET expression are decoration -- SYS1's
        // `sub cx,FAT_BUF + (OFFSET BUF)` reads as FAT_BUF + OFFSET BUF.
        for (std::size_t k = at; tokens[k].kind != Token::end; ++k) {
            if (tokens[k].kind == Token::punct && tokens[k].text == "(" &&
                tokens[k + 1].kind == Token::ident &&
                upper(tokens[k + 1].text) == "OFFSET") {
                int depth = 1;
                std::size_t close = k + 1;
                for (; tokens[close].kind != Token::end; ++close) {
                    if (tokens[close].kind != Token::punct) continue;
                    if (tokens[close].text == "(") ++depth;
                    if (tokens[close].text == ")" && --depth == 0) break;
                }
                if (tokens[close].kind == Token::end) break;
                tokens.erase(tokens.begin() + static_cast<long>(close));
                tokens.erase(tokens.begin() + static_cast<long>(k));
            }
        }
        // A leading addend commutes over OFFSET -- `254+offset dg:arg_buf`
        // rides 0x00fe as the fixup displacement (EDLCMD2.OBJ), exactly as
        // the trailing-addend form does; rotate it behind the OFFSET part.
        if (tokens[at].kind == Token::number ||
            (tokens[at].kind == Token::ident &&
             symbols_.contains(upper(tokens[at].text)) &&
             symbols_.at(upper(tokens[at].text)).kind == Symbol::constant)) {
            std::size_t plus = at;
            while (tokens[plus].kind != Token::end &&
                   !(tokens[plus].kind == Token::punct &&
                     (tokens[plus].text == "," || tokens[plus].text == "[")) &&
                   !(tokens[plus].kind == Token::punct && tokens[plus].text == "+" &&
                     tokens[plus + 1].kind == Token::ident &&
                     upper(tokens[plus + 1].text) == "OFFSET")) {
                ++plus;
            }
            if (tokens[plus].kind == Token::punct && tokens[plus].text == "+" &&
                tokens[plus + 1].kind == Token::ident &&
                upper(tokens[plus + 1].text) == "OFFSET") {
                std::size_t stop = plus + 1;
                while (tokens[stop].kind != Token::end &&
                       !(tokens[stop].kind == Token::punct && tokens[stop].text == ",")) {
                    ++stop;
                }
                std::vector<Token> prefix(tokens.begin() + static_cast<long>(at),
                                          tokens.begin() + static_cast<long>(plus));
                tokens.erase(tokens.begin() + static_cast<long>(at),
                             tokens.begin() + static_cast<long>(plus + 1));
                const std::size_t insert_at = stop - (plus + 1 - at);
                std::vector<Token> tail;
                tail.push_back({Token::punct, "+", 0});
                tail.insert(tail.end(), prefix.begin(), prefix.end());
                tokens.insert(tokens.begin() + static_cast<long>(insert_at),
                              tail.begin(), tail.end());
            }
        }
        // `seg X` and `offset [segname:]X` produce fixed-up immediates.
        if (tokens[at].kind == Token::ident) {
            const std::string word = upper(tokens[at].text);
            if (word == "SEG" && tokens[at + 1].kind == Token::ident) {
                ++at;
                const std::string target = upper(tokens[at].text);
                ++at;
                operand.kind = Operand::imm;
                operand.value = 0;
                operand.fixup_loc = 2;
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    if (groups_[g].name == target) {
                        operand.fixup_group = static_cast<int>(g) + 1;
                        return operand;
                    }
                }
                operand.fixup_segment = segment_index_for(target);
                // `SEG label` rides P=1 with no displacement word
                // (E2BINIT's `mov ax,seg command_line_table`); only the
                // bare segment-name immediate (`mov ax,buffer`) carries
                // the target segment's location counter as displacement
                // (QFSINFO's 0008, QCURDSK's 001d).
                return operand;
            }
            if (word == "OFFSET" && tokens[at + 1].kind == Token::ident) {
                ++at;
                operand.from_offset = true;  // word-sized even if it folds small
                // A PTR cast adds nothing (MSINIT's
                // `OFFSET WORD PTR OUTCHR`).
                if ((upper(tokens[at].text) == "BYTE" ||
                     upper(tokens[at].text) == "WORD" ||
                     upper(tokens[at].text) == "DWORD") &&
                    tokens[at + 1].kind == Token::ident &&
                    upper(tokens[at + 1].text) == "PTR" &&
                    tokens[at + 2].kind == Token::ident) {
                    at += 2;
                }
                std::string target = upper(tokens[at].text);
                ++at;
                bool qualified = false;
                // `offset es:label` frames through the register's ASSUME
                // (EDLPARSE's `cmp es:X,offset es:sw_b_switch` under
                // ES:DG frames DG; FORINIT's `offset
                // es:Sublist_MsgParse_Error` under ES:data frames F_SEG
                // DATA on the extern target).
                if (sreg().contains(target) &&
                    tokens[at].kind == Token::punct && tokens[at].text == ":" &&
                    (tokens[at + 1].kind == Token::ident ||
                     (tokens[at + 1].kind == Token::punct && tokens[at + 1].text == "[" &&
                      tokens[at + 2].kind == Token::ident))) {
                    const int slot = sreg().at(target);
                    if (assume_group_[slot] > 0) {
                        operand.frame_group = assume_group_[slot];
                    } else if (assumes_[slot] >= 0) {
                        operand.frame_segment = assumes_[slot] + 1;
                    }
                    ++at;  // the ':'
                    // The label may be bracketed decoration (IFSUTIL's
                    // `OFFSET CS:[PHONEY_NAME]`).
                    if (tokens[at].kind == Token::punct && tokens[at].text == "[") {
                        target = upper(tokens[at + 1].text);
                        at += 2;
                        if (tokens[at].kind == Token::punct && tokens[at].text == "]") ++at;
                    } else {
                        target = upper(tokens[at].text);
                        ++at;
                    }
                }
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    if (groups_[g].name == target &&
                        tokens[at].kind == Token::punct && tokens[at].text == ":" &&
                        (tokens[at + 1].kind == Token::ident ||
                         (tokens[at + 1].kind == Token::punct &&
                          tokens[at + 1].text == "("))) {
                        // `offset DOSGROUP:label` (or a parenthesized
                        // expression): group-framed offset
                        operand.frame_group = static_cast<int>(g) + 1;
                        break;
                    }
                }
                if (tokens[at].kind == Token::punct && tokens[at].text == ":" &&
                    tokens[at + 1].kind == Token::ident) {
                    // `offset segname:label` -- the frame is the named
                    // segment, and the addend stays in the data (P=1,
                    // GETCNTRY.OBJ); a plain `offset label` zeroes the data
                    // and carries the offset as the fixup's displacement
                    // (SETINT24.OBJ).
                    if (operand.frame_group == 0) {
                        const int index = segment_index_for(target);
                        if (index > 0) operand.frame_segment = index;
                    }
                    target = upper(tokens[at + 1].text);
                    at += 2;
                    qualified = true;
                }
                // `OFFSET DG:(BYTEBUF-10)`: a parenthesized expression
                // after the qualifier -- the base label leads it and the
                // whole value rides the fixup (DEBASM).
                if (tokens[at].kind == Token::punct && tokens[at].text == ":" &&
                    tokens[at + 1].kind == Token::punct && tokens[at + 1].text == "(") {
                    ++at;  // the qualifier's colon
                }
                if (tokens[at].kind == Token::punct && tokens[at].text == "(" &&
                    tokens[at + 1].kind == Token::ident) {
                    target = upper(tokens[at + 1].text);
                    operand.kind = Operand::imm;
                    const auto base = symbols_.find(target);
                    std::vector<Token> expr(tokens.begin() + static_cast<long>(at),
                                            tokens.end());
                    std::size_t used = 0;
                    const Value value = expression(expr, &used);
                    at += used;
                    operand.value = value.value;
                    if (base != symbols_.end() && base->second.kind == Symbol::label) {
                        if (base->second.extern_index > 0) {
                            operand.fixup_loc = 1;
                            operand.fixup_extern = base->second.extern_index;
                        } else if (base->second.segment >= 0) {
                            operand.fixup_loc = 1;
                            operand.fixup_segment = base->second.segment + 1;
                        }
                    } else if (pass_ == 2) {
                        fail("OFFSET of unknown symbol " + target);
                    }
                    return operand;
                }
                operand.kind = Operand::imm;
                (void)qualified;
                if (target == "$") {
                    // `OFFSET $` (SYS1's `EOS EQU BYTE PTR $` splice) is
                    // the location counter, relocated like a label there.
                    operand.value =
                        current_segment_ >= 0 ? segment().location : 0;
                    if (current_segment_ >= 0) {
                        operand.fixup_loc = 1;
                        operand.fixup_segment = current_segment_ + 1;
                    }
                    return operand;
                }
                const auto symbol = symbols_.find(target);
                if (symbol != symbols_.end() && symbol->second.kind == Symbol::label) {
                    operand.value = symbol->second.value;
                    if (symbol->second.extern_index > 0) {
                        operand.fixup_loc = 1;
                        operand.fixup_extern = symbol->second.extern_index;
                    } else if (symbol->second.segment >= 0) {
                        operand.fixup_loc = 1;
                        operand.fixup_segment = symbol->second.segment + 1;
                    }
                } else if (symbol != symbols_.end() &&
                           symbol->second.kind == Symbol::constant) {
                    // OFFSET of a struc FIELD is its plain offset, no
                    // relocation (XCOPY's `MOV DI,OFFSET
                    // ES:FILENAME_FOUND`).
                    operand.value = symbol->second.value;
                } else if (const int index = segment_index_for(target); index > 0) {
                    operand.value = 0;
                    operand.fixup_loc = 1;
                    operand.fixup_segment = index;
                } else if (pass_ == 2) {
                    fail("OFFSET of unknown symbol " + target);
                } else {
                    // Pass 1, forward symbol: reserve the relocatable
                    // 16-bit form (EDLIN's `SUB DI,1+OFFSET DG:COMTAB`
                    // before COMTAB's definition).
                    operand.fixup_loc = 1;
                }
                // A trailing `+n`/`-n` addend rides in the relocation
                // (CHKPRMT's `OFFSET DG:CONBUF+2`), and so does a struc
                // field's offset (GETSET's
                // `OFFSET DOSGROUP:COUNTRY_CDPG.ccDFormat` -- disp 004c).
                while (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                       tokens[at + 1].kind == Token::ident) {
                    const auto field = symbols_.find(upper(tokens[at + 1].text));
                    if (field == symbols_.end()) break;
                    operand.value += field->second.value;
                    at += 2;
                }
                while (tokens[at].kind == Token::punct &&
                       (tokens[at].text == "+" || tokens[at].text == "-")) {
                    const bool minus = tokens[at].text == "-";
                    ++at;
                    std::vector<Token> rest(tokens.begin() + static_cast<long>(at),
                                            tokens.end());
                    std::size_t used = 0;
                    const Value addend = expression(rest, &used);
                    operand.value += minus ? -addend.value : addend.value;
                    at += used;
                }
                return operand;
            }
        }
        // segment override "es:" before brackets
        if (tokens[at].kind == Token::ident && sreg().contains(upper(tokens[at].text)) &&
            tokens[at + 1].kind == Token::punct && tokens[at + 1].text == ":") {
            operand.segment_override = sreg().at(upper(tokens[at].text));
            at += 2;
        }
        if (tokens[at].kind == Token::ident) {
            const std::string word = upper(tokens[at].text);
            if (reg16().contains(word) && !(tokens[at + 1].kind == Token::punct &&
                                            tokens[at + 1].text == ":")) {
                operand.kind = Operand::r16;
                operand.reg = reg16().at(word);
                operand.size = 2;
                ++at;
                return finish_memory_suffix(operand, tokens, at);
            }
            if (reg32().contains(word) && !(tokens[at + 1].kind == Token::punct &&
                                            tokens[at + 1].text == ":")) {
                // A 386 dword register rides the r16 path behind a 66 prefix.
                operand.kind = Operand::r16;
                operand.reg = reg32().at(word);
                operand.size = 4;
                operand.wide = true;
                ++at;
                return finish_memory_suffix(operand, tokens, at);
            }
            if (reg8().contains(word)) {
                operand.kind = Operand::r8;
                operand.reg = reg8().at(word);
                operand.size = 1;
                ++at;
                return operand;
            }
            if (sreg().contains(word)) {
                operand.kind = Operand::seg;
                operand.reg = sreg().at(word);
                operand.size = 2;
                ++at;
                return operand;
            }
        }
        if (tokens[at].kind == Token::punct && tokens[at].text == "[") {
            memory_brackets(operand, tokens, at);
            Operand finished = finish_memory_suffix(operand, tokens, at);
            // Brackets around a pure constant are decoration -- MASM
            // reads `MOV AL,[DRVCHAR]` with `DRVCHAR EQU ":"` as the
            // immediate 3A; only a register, a label, or an explicit
            // override makes it memory. A forward label (`MOV [LBX],DI`
            // before LBX's definition, RAMDRIVE) carries disp_symbolic and
            // stays memory -- it has no fixup yet only because pass 1 has
            // not reached the definition.
            if (finished.kind == Operand::mem && !finished.has_base_bx &&
                !finished.has_base_bp && !finished.has_si && !finished.has_di &&
                finished.base32 < 0 &&
                finished.fixup_segment == 0 && finished.fixup_extern == 0 &&
                finished.segment_override < 0 && finished.home_segment == 0 &&
                !finished.bracket_field && !finished.disp_symbolic) {
                finished.kind = Operand::imm;
            }
            // `[OFFSET label]` is the OFFSET immediate; the brackets and any
            // segment override are decoration -- RAMDRIVE's `mov word ptr
            // [RealLoc1],cs:[offset ReturnBack]` stores the return offset, and
            // the oracle (CSO.OBJ) shows `mov ax,cs:[offset ReturnBack]` is
            // B8 <off> and the memory-store form is byte-identical to the
            // plain `offset` immediate. The offset's own relocation rides the
            // immediate's fixup; a base/index register would make it a real
            // memory displacement instead.
            else if (finished.kind == Operand::mem && finished.from_offset &&
                     !finished.has_base_bx && !finished.has_base_bp &&
                     !finished.has_si && !finished.has_di && finished.base32 < 0) {
                finished.kind = Operand::imm;
                finished.segment_override = -1;
            }
            return finished;
        }
        // `ds:018h` / `cs:prevadrs` / `es:model_byte`: an override followed
        // by a number, constant, or label is a direct memory reference.
        if (operand.segment_override >= 0 &&
            (tokens[at].kind == Token::number ||
             (tokens[at].kind == Token::ident &&
              symbols_.contains(upper(tokens[at].text)) &&
              symbols_.at(upper(tokens[at].text)).kind == Symbol::constant))) {
            // Bound the displacement at a top-level `[` so an index register
            // survives for finish_memory_suffix (`ES:PRINTR-2+[SI]`, PRINTR an
            // absolute-segment constant -- MODEECHO); the expression must not
            // swallow the bracket group.
            std::size_t stop = at;
            for (int depth = 0; tokens[stop].kind != Token::end; ++stop) {
                const Token& t = tokens[stop];
                if (t.kind == Token::punct && t.text == "(") ++depth;
                else if (t.kind == Token::punct && t.text == ")") --depth;
                else if (depth == 0 && t.kind == Token::punct &&
                         (t.text == "," || t.text == "[")) break;
            }
            // Leave a `+` that joins the index (`...+[si]`) for the bracket
            // handler, not a dangling operator in the displacement expression.
            if (stop > at && tokens[stop - 1].kind == Token::punct &&
                tokens[stop - 1].text == "+") {
                --stop;
            }
            std::vector<Token> rest(tokens.begin() + static_cast<long>(at),
                                    tokens.begin() + static_cast<long>(stop));
            rest.push_back({Token::end, "", 0});
            std::size_t used = 0;
            const Value value = expression(rest, &used);
            // A struc field anywhere in the address expression stamps
            // the reference width (MSINIT's `MOV DS:(SEC9 +
            // DISK_SPECIFY_1),0DFH` stores a byte through the field).
            if (operand.size == 0) {
                for (std::size_t k = at; k < at + used; ++k) {
                    if (tokens[k].kind != Token::ident) continue;
                    const auto field = symbols_.find(upper(tokens[k].text));
                    if (field != symbols_.end() &&
                        field->second.kind == Symbol::constant &&
                        field->second.size > 0) {
                        operand.size = field->second.size;
                        break;
                    }
                }
            }
            at = stop;
            operand.kind = Operand::mem;
            operand.value = value.value;
            return finish_memory_suffix(operand, tokens, at);
        }
        // label or expression
        if (tokens[at].kind == Token::ident) {
            const std::string word = upper(tokens[at].text);
            if (const auto reloc = symbols_.find(word);
                reloc != symbols_.end() &&
                reloc->second.kind == Symbol::constant &&
                reloc->second.constant_fixup_extern > 0 &&
                tokens[at + 1].kind == Token::end) {
                Operand imm;
                imm.kind = Operand::imm;
                imm.value = reloc->second.value;
                imm.value_known = true;
                imm.from_offset = true;
                imm.fixup_loc = 1;
                imm.fixup_extern = reloc->second.constant_fixup_extern;
                imm.frame_segment = reloc->second.constant_frame_segment;
                imm.frame_group = reloc->second.constant_frame_group;
                ++at;
                return imm;
            }
            if (const auto fwd = symbols_.find(word);
                fwd != symbols_.end() && fwd->second.kind == Symbol::constant &&
                ((pass_ == 2 && !defined_this_pass_.contains(word)) ||
                 fwd->second.forward_derived_constant) &&
                tokens[at + 1].kind == Token::end) {
                Operand imm;
                imm.kind = Operand::imm;
                imm.value = fwd->second.value;
                imm.value_known = true;
                imm.forward_const = true;
                ++at;
                return imm;
            }
            if (const auto known = symbols_.find(word);
                known != symbols_.end() &&
                known->second.kind == Symbol::constant &&
                known->second.struc_field &&
                tokens[at + 1].kind == Token::punct &&
                tokens[at + 1].text == "." &&
                tokens[at + 2].kind == Token::ident &&
                symbols_.count(upper(tokens[at + 2].text)) &&
                symbols_.at(upper(tokens[at + 2].text)).struc_field) {
                // `field.field` between two STRUC members is the SUM of their
                // offsets as an immediate, not a memory reference: INDEINI's
                // `MOV [BX].ETSS_LDT,SCRUBBER.VM_LDTR` is C7 47 xx <sum>
                // (oracle FF.OBJ: `MOV AX,F1B.F2B` = B8 04 00, 1+3). A bare
                // struc field is already its offset; chain any further members.
                long sum = known->second.value;
                ++at;
                while (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                       tokens[at + 1].kind == Token::ident &&
                       symbols_.count(upper(tokens[at + 1].text)) &&
                       symbols_.at(upper(tokens[at + 1].text)).struc_field) {
                    sum += symbols_.at(upper(tokens[at + 1].text)).value;
                    at += 2;
                }
                Operand imm;
                imm.kind = Operand::imm;
                imm.value = sum;
                imm.value_known = true;
                return imm;
            }
            if (const auto known = symbols_.find(word);
                known != symbols_.end() &&
                known->second.kind == Symbol::constant &&
                tokens[at + 1].kind == Token::punct &&
                tokens[at + 1].text == "." &&
                tokens[at + 2].kind == Token::ident) {
                // `constant.field` is a direct memory reference at the
                // constant's value -- RUCODE's `$M_RT.$M_TEMP_BUF` in
                // pass 1, where `$M_RT EQU $M_RT2` probed to a constant
                // before the lazy EXTRN resolves it (pass 2 re-binds
                // the alias to the extern and relocates, so the
                // displacement reserves its 16-bit form).
                operand.kind = Operand::mem;
                operand.value = known->second.value;
                operand.disp_symbolic = true;
                ++at;
                Operand finished = finish_memory_suffix(operand, tokens, at);
                finished.lazy_base = true;
                return finished;
            }
            static const std::map<std::string, int> operators{
                {"NOT", 0}, {"AND", 0}, {"OR", 0},   {"XOR", 0},  {"MOD", 0},
                {"SHL", 0}, {"SHR", 0}, {"SIZE", 0}, {"TYPE", 0}, {"LENGTH", 0},
                {"OFFSET", 0}, {"SEG", 0}, {"LOW", 0}, {"HIGH", 0}, {"PTR", 0},
                {"MASK", 0}, {"WIDTH", 0},
                // Bare type names are size constants, never forward labels
                // (LABEL.ASM's `mov s1_maxwidth,DWORD` stores 4).
                {"BYTE", 0}, {"WORD", 0}, {"DWORD", 0}, {"QWORD", 0},
                {"TBYTE", 0}};
            for (const Segment& candidate : segments_) {
                if (candidate.name == word) {
                    Operand base;
                    base.kind = Operand::imm;
                    // A bare segment name (without SEG) carries the
                    // segment's extent as the fixup displacement --
                    // QFSINFO's mov ax,buffer has disp 0008 while
                    // GETCNTRY's mov dx,seg buffer has none.
                    base.value = candidate.extent;
                    base.fixup_loc = 2;
                    base.fixup_segment = segment_index_for(word);
                    ++at;
                    return base;
                }
            }
            for (std::size_t g = 0; g < groups_.size(); ++g) {
                if (groups_[g].name == word) {
                    Operand base;
                    base.kind = Operand::imm;
                    base.value = 0;
                    base.fixup_loc = 2;
                    base.fixup_group = static_cast<int>(g) + 1;
                    ++at;
                    return base;
                }
            }
            const auto known = symbols_.find(word);
            if (known != symbols_.end() && known->second.abs_extern) {
                Operand imm;
                imm.kind = Operand::imm;
                imm.value = 0;
                imm.fixup_loc = 1;
                imm.fixup_extern = known->second.extern_index;
                // Carry a segment override / explicit PTR size gathered by the
                // prefix loop, so an addressed use stays a memory reference.
                imm.segment_override = operand.segment_override;
                imm.size = operand.size;
                imm.size_explicit = operand.size_explicit;
                ++at;
                // A trailing +n/-n rides the fixup as its target displacement
                // (SELECT2's `M_KEYBOARD+2`, M_KEYBOARD:ABS -- disp 0002), not
                // the zeroed data word.
                while (tokens[at].kind == Token::punct &&
                       (tokens[at].text == "+" || tokens[at].text == "-")) {
                    const bool minus = tokens[at].text == "-";
                    ++at;
                    std::vector<Token> rest(tokens.begin() + static_cast<long>(at),
                                            tokens.end());
                    std::size_t used = 0;
                    const Value addend = expression(rest, &used);
                    imm.value += minus ? -addend.value : addend.value;
                    at += used;
                }
                // A segment override, explicit PTR size, index, or field
                // selector makes it a memory reference based at the ABS
                // external (SELECT ROUTINES' `PRINTER_TABLES[SI]
                // .PRINTER_DATA_VALID`; MODE's `MOV BYTE PTR
                // ES:res_com_retry_type,DL`, res_com_retry_type:ABS): the
                // extern rides the displacement relocation.
                if (imm.segment_override >= 0 || imm.size_explicit ||
                    (tokens[at].kind == Token::punct &&
                     (tokens[at].text == "[" || tokens[at].text == "."))) {
                    imm.kind = Operand::mem;
                    return finish_memory_suffix(imm, tokens, at);
                }
                return imm;
            }
            if (!operators.contains(word) && word != "$" &&
                ((known != symbols_.end() && known->second.kind == Symbol::label) ||
                 (known == symbols_.end() && pass_ == 1))) {
                operand.kind = Operand::lab;
                operand.label_name = word;
                if (known != symbols_.end() && operand.size == 0) {
                    operand.size = known->second.size;
                }
                if (known != symbols_.end()) {
                    if (known->second.extern_index > 0) {
                        operand.fixup_extern = known->second.extern_index;
                    } else if (known->second.segment >= 0) {
                        operand.fixup_segment = known->second.segment + 1;
                    }
                    if (known->second.segment >= 0) {
                        operand.home_segment = known->second.segment + 1;
                        operand.address_wide =
                            segment_uses_32(operand.home_segment);
                    }
                }
                // A label pulled into MASM's relocation set by a forward
                // near JMP/CALL keeps relocating at every such use (MSLPT's
                // PrnOp); conditional-jump and data uses do not mark it
                // (GETMSG's loop head resolves its backward `jmp`). The
                // marking happens in the JMP/CALL encoders.
                operand.forward_marked = forward_labels_.contains(word);
                if (known != symbols_.end() && known->second.defined &&
                    known->second.extern_index == 0 &&
                    defined_this_pass_.contains(word)) {
                    // Resolve a backward reference only when the label was
                    // actually defined earlier in THIS pass; a label defined
                    // solely under IF1 (CHKDSK2's $$A_START) never is in
                    // pass 2 and so relocates, like a forward or external.
                    operand.value = known->second.value;
                    operand.defined_at_use = true;
                } else if (known != symbols_.end()) {
                    operand.value = known->second.value;
                    operand.value_known = pass_ == 2;
                } else {
                    operand.value_known = false;
                }
                // A label defined later in the module (unknown in pass 1;
                // known from pass 1 but not yet defined this pass in pass 2)
                // is a forward reference. A `MOV mem,imm` store through it
                // reserved the word form in pass 1; pass 2 keeps that size
                // with a byte store plus a NOP. Ordinary externals run through
                // their own lazy path; a late-bound EQU alias carries the same
                // uncertainty even when its EXTRN precedes the use.
                operand.forward_ref =
                    known == symbols_.end() ||
                    known->second.late_extern_alias ||
                    (known->second.extern_index == 0 &&
                     !defined_this_pass_.contains(word));
                ++at;
                // `label[bx]`, `label+2`, `label.field` are memory forms;
                // suffixes fold in and flip the operand to mem. An explicit
                // segment override does too (`cs:prevadrs`).
                if ((tokens[at].kind == Token::punct &&
                     (tokens[at].text == "[" || tokens[at].text == "+" ||
                      tokens[at].text == "-" || tokens[at].text == ".")) ||
                    operand.segment_override >= 0) {
                    Operand mem = label_as_memory(operand);
                    mem.size = operand.size;
                    // A forward BASE under a FIELD selector keeps the
                    // 16-bit displacement even when the field is known
                    // (SYSSR's `$M_RT.$M_TEMP_BUF[BX]` before MSGDCL's
                    // lazy EXTRN resolves $M_RT), and an immediate
                    // store through it sizes as a WORD in pass 1.
                    if (known == symbols_.end() &&
                        tokens[at].kind == Token::punct &&
                        (tokens[at].text == "." || tokens[at].text == "[")) {
                        // A forward label used as a memory displacement --
                        // `JMP jump_table1[BX]` before the table's LABEL
                        // directive, MODE's CASE[SI] -- reserves the 16-bit
                        // form pass 2 relocates, so the instruction keeps its
                        // size across passes (else the table label phase-errors).
                        mem.disp_symbolic = true;
                        if (tokens[at].text == ".") mem.lazy_base = true;
                    }
                    return finish_memory_suffix(mem, tokens, at);
                }
                return operand;
            }
        }
        std::vector<Token> rest(tokens.begin() + static_cast<long>(at), tokens.end());
        std::size_t used = 0;
        const Value value = expression(rest, &used);
        at += used;
        operand.value = value.value;
        operand.value_known = value.known;
        if (tokens[at].kind == Token::punct && tokens[at].text == "[") {
            // `constant[si]` indexes memory at the constant displacement.
            operand.kind = Operand::mem;
            return finish_memory_suffix(operand, tokens, at);
        }
        operand.kind = Operand::imm;
        return operand;
    }

    int segment_index_for(const std::string& upper_name) {
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            if (segments_[i].name == upper_name) return static_cast<int>(i) + 1;
        }
        const auto symbol = symbols_.find(upper_name);
        if (symbol != symbols_.end() && symbol->second.kind == Symbol::label &&
            symbol->second.segment >= 0) {
            return symbol->second.segment + 1;
        }
        return 0;
    }

    void memory_brackets(Operand& operand, std::vector<Token>& tokens, std::size_t& at) {
        operand.kind = Operand::mem;
        ++at;  // '['
        while (!(tokens[at].kind == Token::punct && tokens[at].text == "]") &&
               tokens[at].kind != Token::end) {
            if (tokens[at].kind == Token::ident) {
                const std::string word = upper(tokens[at].text);
                if (word == "BX") { operand.has_base_bx = true; ++at; continue; }
                if (word == "BP") { operand.has_base_bp = true; ++at; continue; }
                if (word == "SI") { operand.has_si = true; ++at; continue; }
                if (word == "DI") { operand.has_di = true; ++at; continue; }
                if (reg32().contains(word)) {
                    operand.base32 = reg32().at(word);
                    operand.address_wide = true;
                    ++at;
                    continue;
                }
                // `[BP+OFFSET DOSGROUP:ESCFUNC]`: the OFFSET of a label
                // inside brackets keeps its relocation -- MSDOSME's JMP
                // rides FF A6 0000 with a fixup to TABLE:02ff.
                if (word == "OFFSET") {
                    std::size_t look = at + 1;
                    if (tokens[look].kind == Token::ident &&
                        tokens[look + 1].kind == Token::punct &&
                        tokens[look + 1].text == ":") {
                        const std::string qualifier = upper(tokens[look].text);
                        bool named = segment_index_for(qualifier) > 0;
                        for (const Group& group : groups_) {
                            if (group.name == qualifier) named = true;
                        }
                        if (named) look += 2;
                    }
                    if (tokens[look].kind == Token::ident) {
                        const auto target =
                            symbols_.find(upper(tokens[look].text));
                        if (target != symbols_.end() &&
                            target->second.kind == Symbol::label &&
                            target->second.extern_index == 0 &&
                            target->second.segment >= 0) {
                            operand.value += target->second.value;
                            operand.fixup_loc = 1;
                            operand.fixup_segment = target->second.segment + 1;
                            // Record that the displacement came through OFFSET
                            // so a register-free `[OFFSET label]` collapses to
                            // the immediate (RAMDRIVE's `cs:[offset ReturnBack]`);
                            // with a base/index it stays a memory displacement.
                            operand.from_offset = true;
                            at = look + 1;
                            continue;
                        }
                        // A forward label (defined later in the module):
                        // reserve the relocatable 16-bit form in pass 1 as the
                        // top-level OFFSET does, so `[offset ReturnBack]` folds
                        // to a word immediate in both passes (RAMDRIVE stores
                        // its own return address before ReturnBack is defined).
                        if (target == symbols_.end() && pass_ == 1) {
                            operand.fixup_loc = 1;
                            operand.from_offset = true;
                            at = look + 1;
                            continue;
                        }
                    }
                }
                // A spliced cast inside the brackets (MSBOOT's
                // `[DIR$_L]` with DIR$_L EQU WORD PTR UDATA+18).
                if ((word == "BYTE" || word == "WORD" || word == "DWORD") &&
                    tokens[at + 1].kind == Token::ident &&
                    upper(tokens[at + 1].text) == "PTR") {
                    if (!operand.size_explicit) {
                        operand.size = word == "BYTE" ? 1 : word == "WORD" ? 2 : 4;
                        operand.size_explicit = true;
                    }
                    at += 2;
                    continue;
                }
            }
            if (tokens[at].kind == Token::punct && tokens[at].text == "+") { ++at; continue; }
            if (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                tokens[at + 1].kind == Token::number) {
                // `[bx.6]` adds a plain displacement (SYSINIT1's
                // `cmp word ptr ds:[bx.6],424Bh`).
                operand.value += tokens[at + 1].value;
                at += 2;
                continue;
            }
            if (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                tokens[at + 1].kind == Token::ident &&
                (upper(tokens[at + 1].text) == "BYTE" ||
                 upper(tokens[at + 1].text) == "WORD" ||
                 upper(tokens[at + 1].text) == "DWORD") &&
                tokens[at + 2].kind == Token::ident &&
                upper(tokens[at + 2].text) == "PTR") {
                // A spliced `.DWORD PTR name` selector casts then selects
                // (SYSCONF's `[ifs_rh.IFSR_PARMS@]` through the
                // IFSR_PARMS@ text equate).
                if (!operand.size_explicit) {
                    const std::string cast = upper(tokens[at + 1].text);
                    operand.size = cast == "BYTE" ? 1 : cast == "WORD" ? 2 : 4;
                    operand.size_explicit = true;
                }
                at += 3;
                continue;
            }
            if (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                tokens[at + 1].kind == Token::ident) {
                // `[DI.BX]` is [DI+BX] -- a dot before a register adds it
                // (FCBIO's `MOV WORD PTR ES:[DI.BX],AX`).
                const std::string reg = upper(tokens[at + 1].text);
                if (reg == "BX" || reg == "BP" || reg == "SI" || reg == "DI") {
                    if (reg == "BX") operand.has_base_bx = true;
                    if (reg == "BP") operand.has_base_bp = true;
                    if (reg == "SI") operand.has_si = true;
                    if (reg == "DI") operand.has_di = true;
                    at += 2;
                    continue;
                }
                // `.field` inside brackets adds the struct field offset
                // (_MSGRET's `POP [$M_RT.$M_RETURN_ADDR]`).
                operand.bracket_field = true;
                const auto field = symbols_.find(upper(tokens[at + 1].text));
                if (field == symbols_.end() && pass_ == 1) {
                    // Forward `.name`: pass 1 reserves the 16-bit form.
                    operand.disp_symbolic = true;
                }
                if (field != symbols_.end()) {
                    if (field->second.kind != Symbol::struc_type) {
                        operand.value += field->second.value;
                        if (field->second.abs_extern) {
                            operand.fixup_loc = 1;
                            operand.fixup_extern =
                                field->second.extern_index;
                            operand.disp_symbolic = true;
                        }
                        // A forward selector keeps the 16-bit
                        // displacement pass 1 reserved (DEBCOM3 defines
                        // OLDIP eight lines after `[BP].OLDIP`).
                        if (!defined_this_pass_.contains(
                                upper(tokens[at + 1].text))) {
                            operand.disp_symbolic = true;
                        }
                    }
                    if (!operand.size_explicit && field->second.size > 0) {
                        operand.size = field->second.size;
                    }
                }
                at += 2;
                continue;
            }
            // A symbol inside the brackets carries a relocation the plain
            // expression value would drop -- `[PTRSAV]` (an external DWORD)
            // reached by MSLPT's `LDS BX,CS:[PTRSAV]` needs its fixup.
            if (tokens[at].kind == Token::ident) {
                const auto known = symbols_.find(upper(tokens[at].text));
                if (const std::string bare = upper(tokens[at].text);
                    known == symbols_.end() && pass_ == 1 &&
                    !reg16().contains(bare) && !sreg().contains(bare) &&
                    bare != "BYTE" && bare != "WORD" && bare != "DWORD" &&
                    bare != "QWORD" && bare != "TBYTE" &&
                    // Expression operators are never forward names --
                    // SYS2's `[si + size DIR_ENTRY]` keeps its disp8.
                    bare != "SIZE" && bare != "TYPE" && bare != "LENGTH" &&
                    bare != "WIDTH" && bare != "MASK" && bare != "OFFSET" &&
                    bare != "SEG" && bare != "LOW" && bare != "HIGH" &&
                    bare != "NOT" && bare != "AND" && bare != "OR" &&
                    bare != "XOR" && bare != "MOD" && bare != "SHL" &&
                    bare != "SHR" && bare != "PTR" && bare != "DUP") {
                    // Forward name: pass 1 reserves the 16-bit
                    // displacement a pass-2 fixup will need (EDLIN's
                    // `CALL [BX+TABLE]` before TABLE's definition).
                    operand.disp_symbolic = true;
                }
                if (known != symbols_.end() && known->second.kind == Symbol::label) {
                    if (known->second.extern_index > 0) {
                        operand.fixup_extern = known->second.extern_index;
                        if (known->second.segment >= 0) {
                            // The declaring segment still decides the
                            // synthesized prefix (UTIL reaches CurrentPDB,
                            // an extern in DATA, through CS:DOSGROUP).
                            operand.home_segment = known->second.segment + 1;
                            operand.address_wide =
                                segment_uses_32(operand.home_segment);
                        }
                    } else if (known->second.segment >= 0) {
                        operand.fixup_segment = known->second.segment + 1;
                        operand.home_segment = known->second.segment + 1;
                        operand.address_wide =
                            segment_uses_32(operand.home_segment);
                    }
                    // The variable's declared width sizes the reference when
                    // no explicit PTR override is present (CHKDSK2's
                    // `CMP [NOISY],0` on a byte -> a byte compare).
                    if (operand.size == 0 && known->second.size > 0) {
                        operand.size = known->second.size;
                    }
                } else if (known != symbols_.end() &&
                           known->second.kind == Symbol::constant &&
                           operand.size == 0 && known->second.size > 0) {
                    // A bare struc field is an offset with the field's
                    // width (TBATCH's `MOV ES:[BatType],BatchType` stores
                    // a byte).
                    operand.size = known->second.size;
                }
            }
            // The expression stops at a register: `[90h+bx]` adds the
            // base after the constant (MSDISK's drive-type table).
            std::size_t stop = at;
            int paren = 0;
            while (tokens[stop].kind != Token::end) {
                if (tokens[stop].kind == Token::punct) {
                    if (tokens[stop].text == "(") ++paren;
                    if (tokens[stop].text == ")") --paren;
                    if ((tokens[stop].text == "]" || tokens[stop].text == ".") &&
                        paren == 0) {
                        break;
                    }
                }
                if (paren == 0 && tokens[stop].kind == Token::ident) {
                    const std::string word = upper(tokens[stop].text);
                    if (word == "BX" || word == "BP" || word == "SI" ||
                        word == "DI") {
                        break;
                    }
                }
                ++stop;
            }
            std::vector<Token> rest(tokens.begin() + static_cast<long>(at),
                                    tokens.begin() + static_cast<long>(stop));
            // strip a trailing `+` so `90h+` evaluates cleanly
            while (!rest.empty() && rest.back().kind == Token::punct &&
                   (rest.back().text == "+")) {
                rest.pop_back();
            }
            rest.push_back({Token::end, "", 0});
            std::size_t used = 0;
            const Value value = expression(rest, &used);
            operand.value += value.value;
            operand.value_known = operand.value_known && value.known;
            if (stop == at) ++stop;  // always progress (stray punctuation)
            at = stop;
        }
        if (tokens[at].kind != Token::end) ++at;  // ']'
        synthesize_override(operand);
    }

    // Reaching a symbol's home segment through the ASSUMEs synthesizes a
    // prefix when DS does not already cover it (SYSMSG's stack-frame
    // struct reached as `POP [$M_RT.$M_RETURN_ADDR]` takes 36).
    void synthesize_override(Operand& mem) {
        const int reach = mem.home_segment > 0 ? mem.home_segment : mem.fixup_segment;
        if (segment_uses_32(reach)) mem.address_wide = true;
        if (mem.segment_override >= 0 || reach <= 0) return;
        const int target = reach - 1;
        const auto covers = [&](int slot) {
            if (assumes_[slot] == target) return true;
            const int group = assume_group_[slot];
            if (group > 0 && target >= 0 &&
                target < static_cast<int>(segments_.size())) {
                const std::string& seg_name = segments_[target].name;
                const auto& members = groups_[group - 1].members;
                return std::find(members.begin(), members.end(), seg_name) !=
                       members.end();
            }
            return false;
        };
        if (covers(3)) {
            // DS already covers it
        } else if (covers(2)) {
            mem.segment_override = 2;  // SS
        } else if (covers(0)) {
            mem.segment_override = 0;  // ES
        } else if (covers(1)) {
            mem.segment_override = 1;  // CS
        }
    }

    Operand finish_memory_suffix(Operand operand, std::vector<Token>& tokens, std::size_t& at) {
        // `.field` and `+expr` postfixes; also further bracket groups.
        while (true) {
            if (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                tokens[at + 1].kind == Token::number) {
                // `.6` is a plain displacement (SYSINIT1's
                // `cmp word ptr ds:[bx.6],424Bh`).
                operand.value += tokens[at + 1].value;
                at += 2;
                continue;
            }
            if (tokens[at].kind == Token::punct && tokens[at].text == "." &&
                tokens[at + 1].kind == Token::ident) {
                if (operand.kind == Operand::r16 && operand.reg == 5) {
                    // bare `bp.field` never occurs; `[bp].field` arrives as mem
                }
                // A spliced `.BYTE PTR name` (FCBIO2's fcb_sfn text
                // equate) casts the size, then selects the name.
                {
                    const std::string cast = upper(tokens[at + 1].text);
                    if ((cast == "BYTE" || cast == "WORD" || cast == "DWORD") &&
                        tokens[at + 2].kind == Token::ident &&
                        upper(tokens[at + 2].text) == "PTR") {
                        operand.size = cast == "BYTE" ? 1 : cast == "WORD" ? 2 : 4;
                        operand.size_explicit = true;
                        if (tokens[at + 3].kind == Token::ident) {
                            at += 2;  // fall through to the field selector
                        } else {
                            at += 3;  // `.word ptr (expr)` -- BOOTFORM's
                            continue;  // BOOT_SIGNATURE equate
                        }
                    }
                }
                const std::string field_name = upper(tokens[at + 1].text);
                const auto symbol = symbols_.find(field_name);
                if (symbol != symbols_.end()) {
                    // A STRUC type name as the selector means offset 0 --
                    // MSGSERV's `[DI].$M_CLASS_ID` addresses the struct
                    // base, not its size.
                    if (symbol->second.kind != Symbol::struc_type) {
                        operand.value += symbol->second.value;
                        if (symbol->second.abs_extern) {
                            operand.fixup_loc = 1;
                            operand.fixup_extern =
                                symbol->second.extern_index;
                            operand.disp_symbolic = true;
                        }
                        // A forward selector keeps the 16-bit
                        // displacement pass 1 reserved (DEBCOM3's
                        // `[BP].OLDIP` before OLDIP's definition).
                        if (!defined_this_pass_.contains(field_name)) {
                            operand.disp_symbolic = true;
                        }
                    }
                    // The field's declared width wins over the base
                    // symbol's (MSGSERV divides by a word field of a
                    // byte-based frame), but never over an explicit PTR.
                    if (!operand.size_explicit && symbol->second.size > 0) {
                        operand.size = symbol->second.size;
                    }
                    operand.bracket_field = true;
                } else if (pass_ == 2) {
                    fail("unknown struct field " + tokens[at + 1].text);
                } else {
                    operand.value_known = false;
                    operand.disp_symbolic = true;  // pass 1 reserves disp16
                }
                at += 2;
                continue;
            }
            if (operand.kind == Operand::mem && tokens[at].kind == Token::punct &&
                tokens[at].text == "(") {
                // A parenthesized addend (spliced casts leave one behind).
                std::vector<Token> rest(tokens.begin() + static_cast<long>(at),
                                        tokens.end());
                std::size_t used = 0;
                const Value value = expression(rest, &used);
                operand.value += value.value;
                at += used;
                continue;
            }
            if (operand.kind == Operand::mem && tokens[at].kind == Token::punct &&
                (tokens[at].text == "+" || tokens[at].text == "-")) {
                // `+[si]` adds an index register, not a displacement expression
                // (MODEECHO's `ES:PRINTR-2+[SI]`); drop the `+` and let the
                // bracket handler below take the register.
                if (tokens[at].text == "+" && tokens[at + 1].kind == Token::punct &&
                    tokens[at + 1].text == "[") {
                    ++at;
                    continue;
                }
                const bool add = tokens[at].text == "+";
                ++at;
                // Bound the addend expression at a top-level `[` so an index
                // register (`PRINTR-2+[SI]`, PRINTR an absolute-segment label)
                // is left for the bracket handler, not swallowed as a group.
                std::size_t stop = at;
                for (int depth = 0; tokens[stop].kind != Token::end; ++stop) {
                    const Token& t = tokens[stop];
                    if (t.kind == Token::punct && t.text == "(") ++depth;
                    else if (t.kind == Token::punct && t.text == ")") --depth;
                    else if (depth == 0 && t.kind == Token::punct &&
                             (t.text == "," || t.text == "[")) break;
                }
                if (stop > at && tokens[stop - 1].kind == Token::punct &&
                    tokens[stop - 1].text == "+") {
                    --stop;
                }
                std::vector<Token> rest(tokens.begin() + static_cast<long>(at),
                                        tokens.begin() + static_cast<long>(stop));
                rest.push_back({Token::end, "", 0});
                // A typed structure field used as a postfix displacement
                // still supplies the memory operand's width. CASERVIC's
                // `ES:[DI]+SCB_OPT1` is word-sized because SCB_OPT1 is DW,
                // allowing `AND ...,NOT SCB_UKS` to use 83 /4 ib.
                if (!operand.size_explicit && operand.size == 0) {
                    for (std::size_t look = at; look < stop; ++look) {
                        if (tokens[look].kind != Token::ident) continue;
                        const auto field = symbols_.find(upper(tokens[look].text));
                        if (field != symbols_.end() &&
                            field->second.struc_field &&
                            field->second.size > 0) {
                            operand.size = field->second.size;
                            break;
                        }
                    }
                }
                std::size_t used = 0;
                const Value value = expression(rest, &used);
                operand.value += add ? value.value : -value.value;
                at = stop;
                continue;
            }
            if (operand.kind == Operand::mem && tokens[at].kind == Token::punct &&
                tokens[at].text == "." && tokens[at + 1].kind == Token::punct &&
                tokens[at + 1].text == "[") {
                ++at;
                memory_brackets(operand, tokens, at);
                continue;
            }
            if (operand.kind == Operand::mem && tokens[at].kind == Token::punct &&
                tokens[at].text == "[") {
                memory_brackets(operand, tokens, at);
                continue;
            }
            break;
        }
        return operand;
    }

    // A mem,imm operation through a LAZY extern base (its EXTRN line
    // still ahead this pass) sizes by the extern's own width -- pass 1
    // reserved the word form, so a shrunken byte form pads a NOP
    // (MSGSERV's `MOV $M_RT.$M_SIZE,$M_NULL` = C6 .. 90; its
    // `CMP $M_RT.$M_CLASS,UTILITY_MSG_CLASS` = 80 .. 90). An up-front
    // extern keeps the field's width (MSINIT's `MOV [DEVCALL.REQSTAT],0`
    // stays C7).
    int lazy_extern_store_size(const Operand& dst, int size, bool* pad) {
        *pad = false;
        if (dst.size_explicit || dst.fixup_extern <= 0) return size;
        const std::size_t index = static_cast<std::size_t>(dst.fixup_extern) - 1;
        if (index >= externs_.size() ||
            defined_this_pass_.contains(upper(externs_[index]))) {
            return size;
        }
        // The operand's resolved width (field's, or the extern's own)
        // stands; only the BYTE forms shrank from pass 1's word
        // reservation and pad ($M_MSG_NUM's word CMP stays 81 with no
        // pad even through the lazy base).
        *pad = size == 1;
        return size;
    }

    // A 16-bit immediate takes the sign-extended 83 form when its low byte
    // reproduces it: 0FFF0h is -16 (measured off ALLOCSEG.OBJ's and bx).
    static bool fits_signed_byte(long value) {
        const std::uint16_t word = static_cast<std::uint16_t>(value);
        return word < 0x80 || word >= 0xFF80;
    }
    // The 32-bit form of the same test (66 83 /r ib, sign-extended).
    static bool fits_signed_byte_wide(long value) {
        const std::uint32_t dword = static_cast<std::uint32_t>(value);
        return dword < 0x80 || dword >= 0xFFFFFF80u;
    }

    // A bare data label used as an operand is a direct memory reference
    // whose displacement is the label's segment offset, fixed up to its
    // segment (or external).
    Operand label_as_memory(const Operand& lab) {
        Operand mem;
        mem.kind = Operand::mem;
        mem.value = lab.value;
        mem.fixup_loc = 1;
        mem.fixup_segment = lab.fixup_segment;
        mem.fixup_extern = lab.fixup_extern;
        mem.size = lab.size;
        mem.size_explicit = lab.size_explicit;
        mem.forward_ref = lab.forward_ref;
        mem.address_wide = lab.address_wide ||
                           segment_uses_32(lab.home_segment > 0
                                               ? lab.home_segment
                                               : lab.fixup_segment);
        mem.segment_override = lab.segment_override;
        mem.prefix_group = lab.prefix_group;
        // No explicit override: reach the label's segment -- an extern's
        // declaring segment (CHARIN.OBJ's 2E A1) -- through the ASSUMEs:
        // DS free of charge, else a synthesized prefix.
        const int reach = lab.home_segment > 0 ? lab.home_segment : lab.fixup_segment;
        if (mem.segment_override < 0 && reach > 0) {
            const int target = reach - 1;
            // A register covers the target either by being ASSUMEd to that
            // segment directly, or to a group that contains it (_REPLACE's
            // code data reached through CS:PGROUP needs the 2E prefix).
            const auto covers = [&](int slot) {
                if (assumes_[slot] == target) return true;
                const int group = assume_group_[slot];
                if (group > 0 && target >= 0 &&
                    target < static_cast<int>(segments_.size())) {
                    const std::string& seg_name = segments_[target].name;
                    const auto& members = groups_[group - 1].members;
                    return std::find(members.begin(), members.end(), seg_name) !=
                           members.end();
                }
                return false;
            };
            if (covers(3)) {
                // DS already covers it
            } else if (covers(2)) {
                mem.segment_override = 2;  // SS
            } else if (covers(0)) {
                mem.segment_override = 0;  // ES
            } else if (covers(1)) {
                mem.segment_override = 1;  // CS
            }
        }
        return mem;
    }

    // ------------------------------------------------------------ emitter
    // Atomic units: instructions and scalar data items never straddle the
    // record cap. begin_atom/end_atom bracket one unit's emission.
    void begin_atom() {
        // Only an instruction's multi-byte field is atomic against the LEDATA
        // record cap; MASM splits DATA (a `dw`/`dd` item) across the boundary
        // at the byte level (KDFCF's word at 0x3fb straddles the 1020 cap --
        // MASM keeps its low byte in the first record, ckmasm slid the whole
        // word and cut a byte early). Data emissions therefore open no atom.
        atom_open_ = current_segment_ >= 0 && !data_use_;
        if (!atom_open_) return;
        atom_segment_ = current_segment_;
        atom_location_ = segment().location;
    }
    void end_atom(bool keep = true) {
        if (!atom_open_) return;
        atom_open_ = false;
        if (!keep || pass_ != 2 || current_segment_ != atom_segment_) return;
        Segment& target = segment();
        if (target.runs.empty()) return;
        Segment::Item& run = target.runs.back();
        if (run.repeat != 0 || !run.lidata.empty()) return;
        if (run.sequence != emit_sequence_) return;
        if (run.start > atom_location_) return;
        if (run.start + run.bytes.size() != target.location) return;
        const std::uint32_t begin =
            static_cast<std::uint32_t>(atom_location_ - run.start);
        const std::uint32_t end =
            static_cast<std::uint32_t>(target.location - run.start);
        if (end > begin) run.atoms.push_back({begin, end});
    }

    void emit_byte(std::uint8_t byte) {
        if (current_segment_ < 0) return fail("code outside any segment");
        Segment& target = segment();
        if (pass_ == 2) {
            if (target.runs.empty() || target.runs.back().repeat != 0 ||
                target.runs.back().sequence != emit_sequence_ ||
                target.runs.back().start + target.runs.back().bytes.size() != target.location) {
                target.runs.push_back({target.location, 0, {}, {}, ++emit_sequence_, {}, {}});
            }
            target.runs.back().bytes.push_back(byte);
        }
        target.location = static_cast<std::uint16_t>(target.location + 1);
        target.extent = std::max(target.extent, target.location);
    }

    void emit_iterated(std::uint32_t repeat, const std::vector<std::uint8_t>& bytes) {
        if (current_segment_ < 0) return fail("data outside any segment");
        Segment& target = segment();
        if (pass_ == 2) {
            target.runs.push_back({target.location, repeat, bytes, {}, ++emit_sequence_, {}, {}});
        }
        target.location =
            static_cast<std::uint16_t>(target.location + repeat * bytes.size());
        target.extent = std::max(target.extent, target.location);
    }
    void emit_word(std::uint16_t word) {
        // A 16-bit field is atomic against the record cap: the flush
        // slides it whole into the next LEDATA (SORT cuts mid-CMP right
        // before the disp16; DEBUASM's byte-wise rel8 splits at 1020).
        begin_atom();
        emit_byte(static_cast<std::uint8_t>(word & 0xFF));
        emit_byte(static_cast<std::uint8_t>(word >> 8));
        end_atom();
    }

    void emit_dword(std::uint32_t dword) {
        begin_atom();
        emit_byte(static_cast<std::uint8_t>(dword & 0xFF));
        emit_byte(static_cast<std::uint8_t>((dword >> 8) & 0xFF));
        emit_byte(static_cast<std::uint8_t>((dword >> 16) & 0xFF));
        emit_byte(static_cast<std::uint8_t>((dword >> 24) & 0xFF));
        end_atom();
    }

    // A 386 dword operation in a 16-bit segment carries the 66 operand-size
    // prefix, emitted after any segment override and before the opcode.
    void emit_opsize(bool wide) {
        if (wide) emit_byte(0x66);
    }

    std::uint32_t fixup_sequence() const {
        const Segment& target =
            segments_[static_cast<std::size_t>(current_segment_)];
        if (!target.runs.empty()) {
            const Segment::Item& run = target.runs.back();
            if (run.repeat == 0 && run.lidata.empty() &&
                run.sequence == emit_sequence_ &&
                run.start + run.bytes.size() == target.location) {
                return run.sequence;
            }
        }
        // The following emit_byte starts a fresh run at this location.
        return emit_sequence_ + 1;
    }

    void note_fixup(int loc, int target_segment, int target_extern,
                    bool self_relative, long explicit_disp = -1,
                    bool force_external_frame = false) {
        if (pass_ != 2 || current_segment_ < 0) return;
        // A negative addend is a real displacement word (DELETE's
        // `OFFSET DOSGROUP:AUXSTACK - (SIZE sf_entry)` rides 0xffc5);
        // only the -1 sentinel means none.
        if (explicit_disp != -1) explicit_disp &= 0xFFFF;
        // A base fixup naming the CURRENT segment rides P=1 with no
        // displacement (MSINIT's `ADD AX,CODE` inside CODE); naming
        // another segment keeps its location counter as displacement
        // (QFSINFO's `mov ax,buffer` from DOSXXX rides 0008).
        if (loc == 2 && target_segment == current_segment_ + 1) {
            explicit_disp = -1;
        }
        int frame_segment = pending_frame_segment_;
        int frame_group = pending_frame_group_;
        pending_frame_segment_ = 0;
        if (frame_segment > 0) {
            segment().fixups.push_back({segment().location, loc, self_relative,
                                        target_segment, target_extern, 0,
                                        frame_segment, 0,
                                        explicit_disp != -1
                                            ? (explicit_disp & 0xFFFF)
                                            : explicit_disp,
                                        fixup_sequence()});
            pending_frame_group_ = 0;
            return;
        }
        // A self-relative internal reference (CALL/JMP) is framed by CS:
        // if CS is ASSUMEd to a group, that group frames it (_REPLACE's
        // PGROUP code).
        if (frame_group == 0 && self_relative && target_extern == 0 &&
            assume_group_[1] > 0) {
            frame_group = assume_group_[1];
        }
        // A non-self internal reference frames through the ASSUMEd group
        // containing the target segment -- DS first, then CS (_MSGRET's
        // `mov dx,offset ...` under DS:DGROUP frames DGROUP).
        if (frame_group == 0 && !self_relative && target_extern == 0 &&
            loc != 2 &&  // a base (segment-load) fixup frames its target
                         // segment itself (EDLCMD2's `MOV AX,cstack`)
            target_segment > 0 &&
            target_segment <= static_cast<int>(segments_.size())) {
            const std::string& seg_name =
                segments_[static_cast<std::size_t>(target_segment) - 1].name;
            const auto contains = [&](int group_index) {
                if (group_index <= 0) return false;
                const auto& members =
                    groups_[static_cast<std::size_t>(group_index) - 1].members;
                return std::find(members.begin(), members.end(), seg_name) !=
                       members.end();
            };
            // When the DS or CS ASSUME covers the target -- naming its
            // segment directly or a group containing it -- the frame is
            // the segment itself (CHKINIT's C7 store frames F_SEG DATA
            // under DS:DG; SORT's tables under CS:DG). Only an UNCOVERED
            // target falls back to the containing group of a
            // PUBLIC-combine segment (_MSGRET's `mov dx,offset ...` under
            // DS:_TEXT frames DGROUP; RDATA's DATARES words frame
            // RESGROUP with no ASSUME at all).
            const int target_zero = target_segment - 1;
            const auto covers = [&](int slot) {
                if (assumes_[slot] == target_zero) return true;
                return contains(assume_group_[slot]);
            };
            if (data_use_ && target_zero == current_segment_) {
                // A data word pointing within its own segment stays F_SEG,
                // even when that segment belongs to a group and no ASSUME is
                // active (SAMEFIX.OBJ; EM286LL's ELOff -> ELbuff).
            } else if (covers(3)) {
                // Covered by DS: an offset16 reference keeps F_SEG
                // (CHKINIT's C7 store under DS:DG), but a pointer32 takes
                // the covering group (MEM _MSGRET's far pointers under
                // DS:DGROUP).
                if (loc == 3 && contains(assume_group_[3])) {
                    frame_group = assume_group_[3];
                }
            } else if (data_use_ || assumes_[3] >= 0 || assume_group_[3] > 0) {
                // The fallback needs DS ASSUMEd to SOMETHING (or a data
                // word, which falls back with no ASSUME at all -- RDATA):
                // XCOPY's `MOV DX,OFFSET SAV_S_DRV` under DS:NOTHING
                // keeps F_SEG DSEG.
                const Segment& target_seg =
                    segments_[static_cast<std::size_t>(target_zero)];
                if (target_seg.combine != 0) {
                    for (std::size_t g = 0; g < groups_.size(); ++g) {
                        if (contains(static_cast<int>(g) + 1)) {
                            frame_group = static_cast<int>(g) + 1;
                            break;
                        }
                    }
                }

            }
        }
        if (target_extern > 0) {
            // An external's frame is fixed at DECLARATION time, not use
            // time. Self-relative references take the group ASSUMEd to CS
            // when the EXTRN executed (BUF's EcritDisk under CS:DOSGROUP),
            // else the declaring segment itself (COPY declares CERROR in
            // TRANCODE before any ASSUME -- F_SEG TRANCODE), else F_EXT
            // (ATTRIBA's `call _inmain`). Data references take the group
            // CONTAINING the declaring segment (RDATA's `dw COMMAND` in
            // TRANCODE frames TRANGROUP although CS/DS say RESGROUP),
            // else the declaration-time group (CHKDSK2's
            // Display_Interface), else the declaring segment.
            const std::string& spelling = externs_[static_cast<std::size_t>(target_extern) - 1];
            const auto symbol = symbols_.find(upper(spelling));
            const int declaring =
                symbol != symbols_.end() ? symbol->second.segment : -1;
            const int declared_group =
                symbol != symbols_.end() ? symbol->second.pub_group : 0;
            const int declared_segment =
                symbol != symbols_.end() ? symbol->second.pub_segment : 0;
            int containing = 0;
            if (declaring >= 0) {
                const std::string& seg_name =
                    segments_[static_cast<std::size_t>(declaring)].name;
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    const auto& members = groups_[g].members;
                    if (std::find(members.begin(), members.end(), seg_name) !=
                        members.end()) {
                        containing = static_cast<int>(g) + 1;
                        break;
                    }
                }
            }
            if (frame_group == 0) {
                if (self_relative) {
                    // Declaration-time state throughout: CS group, else
                    // the CS-ASSUMEd segment at declaration (FORINIT's
                    // Main_Routine under CS:code; MSFOR declares
                    // AddToSystemSize:near INSIDE `data` yet its CALLs
                    // frame CODE), else the declaring segment (COPY's
                    // CERROR in TRANCODE before any ASSUME), else F_EXT
                    // (FORPROC's Display_Interface declared with no
                    // ASSUME; ATTRIBA's _inmain).
                    if (declared_group > 0) frame_group = declared_group;
                    else if (declared_segment > 0) {
                        frame_segment = declared_segment;
                    } else if (declaring >= 0) {
                        frame_segment = declaring + 1;
                    }
                } else if (data_use_) {
                    // Only a data directive's reference (TDATA's `dw
                    // copy_Num`) falls back to the containing group; an
                    // instruction operand (CHKDISK's `mov
                    // Read_Write_Relative...` under DS:DG) does not. A
                    // LAZY extern's data use frames its declaring segment
                    // (EDLIN's TABLE words to invoke-declared nocom).
                    // The containing-group fallback needs an ASSUME in
                    // force: DEBCONST's tables reference NEAR externs
                    // declared in CODE under no ASSUME at all, and frame
                    // F_SEG CODE, not the containing DG.
                    const bool any_assume =
                        assumes_[0] >= 0 || assumes_[1] >= 0 ||
                        assumes_[2] >= 0 || assumes_[3] >= 0 ||
                        assume_group_[0] > 0 || assume_group_[1] > 0 ||
                        assume_group_[2] > 0 || assume_group_[3] > 0;
                    if (declaring >= 0 && declaring == current_segment_) {
                        // Same segment as the data: F_SEG (EDLIN's TABLE
                        // words to nocom, both in CODE).
                        frame_segment = declaring + 1;
                    } else if (containing > 0 && any_assume) {
                        frame_group = containing;
                    } else if (declared_group > 0) {
                        frame_group = declared_group;
                    } else if (declaring >= 0) {
                        frame_segment = declaring + 1;
                    }
                } else {
                    if (declared_group > 0) frame_group = declared_group;
                    else if (declaring >= 0) frame_segment = declaring + 1;
                }
            }
            if (frame_group > 0) frame_segment = 0;
        }
        if (force_external_frame && target_extern > 0) {
            frame_segment = 0;
            frame_group = 0;
        }
        segment().fixups.push_back({segment().location,
                                    loc,
                                    self_relative,
                                    target_segment,
                                    target_extern,
                                    /*target_group=*/0,
                                    frame_segment,
                                    frame_group,
                                    explicit_disp,
                                    fixup_sequence()});
        pending_frame_group_ = 0;
    }

    void note_group_fixup(int loc, int group_index) {
        if (pass_ != 2 || current_segment_ < 0) return;
        segment().fixups.push_back({segment().location, loc, false, 0, 0, group_index,
                                    0, 0, -1, fixup_sequence()});
    }

    // The declaration-time group of an EXTRN. A code-typed (NEAR)
    // external follows CS alone: CS ASSUMEd to a group takes it
    // (TBATCH2's lazies under CS:TRANGROUP,DS:RESGROUP frame TRANGROUP),
    // CS on a plain segment leaves none (XCOPYPAR declares GET_PARMS
    // under CS:CSEG,DS:DGROUP and frames F_SEG CSEG). A data-typed one
    // takes whichever register holds a group (CTRLC's I_need externs
    // under SS:DOSGROUP,CS:DOSGROUP with DS unassumed).
    int declaration_group(int declared_size) const {
        if (declared_size == 0) return assume_group_[1];
        // A data-typed external never records one: its uses frame through
        // the reaching register (memory operands), the containing group
        // (data directives), or its declaring segment (CHKDISK's plain
        // `mov bx,offset Read_Write_Relative` is F_SEG DATA even under
        // an all-DG ASSUME).
        return 0;
    }

    void operand_fixup(const Operand& operand, bool self_relative = false) {
        if (operand.fixup_loc == 0) return;
        if (operand.fixup_group > 0) {
            note_group_fixup(operand.fixup_loc, operand.fixup_group);
            return;
        }
        // An explicit `offset GROUP:x` qualifier frames the fixup even
        // through the generic path (CTRLC's `SUB AX,OFFSET
        // DOSGroup:ErrMap24`).
        if (operand.frame_group > 0) pending_frame_group_ = operand.frame_group;
        if (operand.frame_segment > 0) pending_frame_segment_ = operand.frame_segment;
        // The operand's resolved offset rides as the displacement, data
        // zeroed (EDLPARSE's `cmp [X],label-imm` carries disp 0025).
        note_fixup(operand.fixup_loc, operand.fixup_segment, operand.fixup_extern,
                   self_relative,
                   operand.value != 0 ? (operand.value & 0xFFFF) : -1);
    }

    static bool is_direct(const Operand& mem) {
        return mem.kind == Operand::mem && !mem.has_base_bx && !mem.has_base_bp &&
               !mem.has_si && !mem.has_di && mem.base32 < 0;
    }

    // The group that frames a memory reference: the group ASSUMEd to the
    // register that reaches it (the segment override, or DS by default).
    int reaching_group(const Operand& mem) const {
        const int slot = mem.segment_override >= 0 ? mem.segment_override : 3;
        return assume_group_[slot];
    }
    // The reaching register's sticky SEGMENT frame: only meaningful when
    // the register is CURRENTLY assumed to NOTHING -- the frame keeps the
    // last segment it held (MSLOAD's ds:Org_Rom_DiskTable after
    // `assume ds:nothing`); an active segment ASSUME frames the target.
    int reaching_segment(const Operand& mem) const {
        // Only an explicit DS: override frames the register's ASSUMEd
        // segment (MSLOAD's `ds:Org_Rom_DiskTable` under ds:Bootseg
        // frames BOOTSEG on a CSEG target); CS:/ES:/SS: overrides and
        // default reaches frame the target itself (SETINT24's cs: word).
        if (mem.segment_override != 3) return 0;
        if (assumes_[3] >= 0) return assumes_[3] + 1;
        return sticky_assumes_[3] >= 0 ? sticky_assumes_[3] + 1 : 0;
    }

    // An external reached through an explicit segment-register override
    // frames the register's ASSUMEd segment, not the extern itself -- any
    // register, any extern (ABS or a regular field-typed one): MSDISK's
    // ABS `MOV AL,CS:[DI + NUMERR - 1]` under CS:CODE frames F_SEG CODE,
    // and KEYB's non-ABS `CMP WORD PTR CS:SD.OLD_INT_2F,0` (SD an alias for
    // `EXTRN SHARED_DATA:BYTE`) frames F_SEG CODE the same way, while the
    // plain `SD.field` uses with no override stay framed by the extern
    // itself (F_EXT) -- both confirmed in KEYBI2F/KEYBI48's reference OBJs.
    int extern_override_frame(const Operand& mem) const {
        if (mem.fixup_extern <= 0 || mem.segment_override < 0) return 0;
        const int slot = mem.segment_override;
        return assumes_[slot] >= 0 ? assumes_[slot] + 1 : 0;
    }

    // The displacement word of a direct reference, with its fixup.
    void emit_direct_disp(const Operand& mem) {
        const bool wide_address = mem.address_wide && !mem.has_base_bx &&
                                  !mem.has_base_bp && !mem.has_si && !mem.has_di;
        if (mem.fixup_segment != 0) {
            pending_frame_group_ = reaching_group(mem);
            if (pending_frame_group_ == 0 && mem.segment_override >= 0) {
                pending_frame_segment_ = reaching_segment(mem);
            }
            note_fixup(wide_address ? 9 : 1, mem.fixup_segment, 0, false,
                       mem.value);
            if (wide_address) emit_dword(0);
            else emit_word(0);
            return;
        }
        if (mem.fixup_extern != 0) {
            pending_frame_group_ = reaching_group(mem);
            if (pending_frame_group_ == 0 && mem.segment_override >= 0) {
                pending_frame_segment_ = reaching_segment(mem);
                if (pending_frame_segment_ == 0) {
                    pending_frame_segment_ = extern_override_frame(mem);
                }
            }
            note_fixup(wide_address ? 9 : 1, 0, mem.fixup_extern, false,
                       mem.value != 0 ? (mem.value & 0xFFFF) : -1);
            if (wide_address) emit_dword(0);
            else emit_word(0);
            return;
        }
        if (wide_address) emit_dword(static_cast<std::uint32_t>(mem.value));
        else emit_word(static_cast<std::uint16_t>(mem.value));
    }

    // modrm for the slice's addressing subset
    void emit_modrm_mem(int reg, const Operand& mem) {
        if (mem.base32 >= 0) {
            const int rm = mem.base32;
            const long disp = mem.value;
            if (mem.fixup_segment != 0 || mem.fixup_extern != 0) {
                emit_byte(static_cast<std::uint8_t>(0x80 | (reg << 3) | rm));
                if (rm == 4) emit_byte(0x24);
                pending_frame_group_ = reaching_group(mem);
                note_fixup(9, mem.fixup_segment, mem.fixup_extern, false, disp);
                emit_dword(0);
                return;
            }
            if (disp == 0 && rm != 5 && !mem.disp_symbolic) {
                emit_byte(static_cast<std::uint8_t>(0x00 | (reg << 3) | rm));
                if (rm == 4) emit_byte(0x24);
            } else if (disp >= -128 && disp <= 127 && !mem.disp_symbolic) {
                emit_byte(static_cast<std::uint8_t>(0x40 | (reg << 3) | rm));
                if (rm == 4) emit_byte(0x24);
                emit_byte(static_cast<std::uint8_t>(disp));
            } else {
                emit_byte(static_cast<std::uint8_t>(0x80 | (reg << 3) | rm));
                if (rm == 4) emit_byte(0x24);
                emit_dword(static_cast<std::uint32_t>(disp));
            }
            return;
        }
        int rm = -1;
        if (mem.has_base_bx && mem.has_si) rm = 0;
        else if (mem.has_base_bx && mem.has_di) rm = 1;
        else if (mem.has_base_bp && mem.has_si) rm = 2;
        else if (mem.has_base_bp && mem.has_di) rm = 3;
        else if (mem.has_si) rm = 4;
        else if (mem.has_di) rm = 5;
        else if (mem.has_base_bp) rm = 6;
        else if (mem.has_base_bx) rm = 7;
        const long disp = mem.value;
        if (rm < 0) {  // direct address
            const bool wide_address = mem.address_wide;
            emit_byte(static_cast<std::uint8_t>((wide_address ? 0x05 : 0x06) |
                                                (reg << 3)));
            if (mem.fixup_segment != 0) {
                // Internal label: zeroed data, the offset travels as the
                // fixup's explicit displacement (SETINT24.OBJ).
                pending_frame_group_ = reaching_group(mem);
            if (pending_frame_group_ == 0 && mem.segment_override >= 0) {
                pending_frame_segment_ = reaching_segment(mem);
            }
                note_fixup(wide_address ? 9 : 1, mem.fixup_segment, 0, false,
                           disp);
                if (wide_address) emit_dword(0);
                else emit_word(0);
                return;
            }
            if (mem.fixup_extern != 0) {
                pending_frame_group_ = reaching_group(mem);
            if (pending_frame_group_ == 0 && mem.segment_override >= 0) {
                pending_frame_segment_ = reaching_segment(mem);
                if (pending_frame_segment_ == 0) {
                    pending_frame_segment_ = extern_override_frame(mem);
                }
            }
                note_fixup(wide_address ? 9 : 1, 0, mem.fixup_extern, false,
                           disp != 0 ? (disp & 0xFFFF) : -1);
                if (wide_address) emit_dword(0);
                else emit_word(0);
                return;
            }
            if (wide_address) emit_dword(static_cast<std::uint32_t>(disp));
            else emit_word(static_cast<std::uint16_t>(disp));
            return;
        }
        if (mem.fixup_segment != 0 || mem.fixup_extern != 0) {
            // A label-based displacement always takes the 16-bit form with
            // zeroed data and the offset in the fixup (QCURDSK's d1 9d 00 00).
            emit_byte(static_cast<std::uint8_t>(0x80 | (reg << 3) | rm));
            // A based reference frames through the use-time reaching
            // register, like the direct form (TBATCH2's
            // `MOV AL,BatBuf[BX]` under DS:TranGroup; SORT's
            // `MOV BYTE PTR $M_RT.$M_TEMP_BUF[BX],AL` through CS:DG).
            pending_frame_group_ = reaching_group(mem);
            if (pending_frame_group_ == 0) {
                if (mem.segment_override >= 0) {
                    pending_frame_segment_ = reaching_segment(mem);
                    if (pending_frame_segment_ == 0 && mem.fixup_extern != 0) {
                        pending_frame_segment_ = extern_override_frame(mem);
                    }
                } else if (mem.fixup_extern != 0 && assumes_[3] >= 0) {
                    // A based external is reached through its effective
                    // segment register. With no explicit override that is
                    // DS, so MASM frames `EXT[BX]` through DS's active
                    // segment even when a direct `EXT` remains F_EXT
                    // (DSFRAME.OBJ; SELECT's PRINTER_TABLES/SELECT_STACK).
                    pending_frame_segment_ = assumes_[3] + 1;
                }
            }
            note_fixup(1, mem.fixup_segment, mem.fixup_extern, false,
                       disp != 0 ? (disp & 0xFFFF) : -1);
            emit_word(0);
            return;
        }
        if (disp == 0 && !(rm == 6) && !mem.disp_symbolic) {
            emit_byte(static_cast<std::uint8_t>(0x00 | (reg << 3) | rm));
        } else if (disp >= -128 && disp <= 127 && !mem.disp_symbolic) {
            emit_byte(static_cast<std::uint8_t>(0x40 | (reg << 3) | rm));
            emit_byte(static_cast<std::uint8_t>(disp));
        } else {
            emit_byte(static_cast<std::uint8_t>(0x80 | (reg << 3) | rm));
            emit_word(static_cast<std::uint16_t>(disp));
        }
    }
    void emit_modrm_reg(int reg, int rm) {
        emit_byte(static_cast<std::uint8_t>(0xC0 | (reg << 3) | rm));
    }
    void emit_override(const Operand& mem) {
        if (mem.segment_override >= 0) {
            // MASM drops a prefix that names the mode's own default segment:
            // ds:[si] emits bare 8B 04 (measured off GETCNTRY.OBJ), es:[di] a
            // real 26. [bp]-based modes default to SS.
            const bool bp_based = mem.has_base_bp || mem.base32 == 5;
            const int default_segment = bp_based ? 2 : 3;
            if (mem.segment_override != default_segment ||
                mem.kind != Operand::mem) {
                emit_byte(static_cast<std::uint8_t>(
                    0x26 | (mem.segment_override << 3)));
            }
        }
        if ((mem.address_wide && !mem.has_base_bx && !mem.has_base_bp &&
             !mem.has_si && !mem.has_di) ||
            (mem.base32 >= 0 &&
             !(current_segment_ >= 0 && segment_uses_32(segments_[current_segment_])))) {
            emit_byte(0x67);
        }
    }

    // ------------------------------------------------------- instructions
    void encode_instruction(const std::string& op, std::vector<Token>& rest) {
        static const std::map<std::string, std::uint8_t> zero_operand{
            {"NOP", 0x90},  {"CBW", 0x98},  {"CWD", 0x99},   {"WAIT", 0x9B}, {"PUSHF", 0x9C},
            {"POPF", 0x9D}, {"SAHF", 0x9E}, {"LAHF", 0x9F},  {"MOVSB", 0xA4},
            {"MOVSW", 0xA5}, {"CMPSB", 0xA6}, {"CMPSW", 0xA7}, {"STOSB", 0xAA},
            {"STOSW", 0xAB}, {"LODSB", 0xAC}, {"LODSW", 0xAD}, {"SCASB", 0xAE},
            {"SCASW", 0xAF}, {"RET", 0x00},  {"RETN", 0xC3},  {"RETF", 0xCB},
            {"INT3", 0xCC}, {"IRET", 0xCF}, {"XLAT", 0xD7},  {"HLT", 0xF4},  {"CMC", 0xF5},
            {"CLC", 0xF8},  {"STC", 0xF9},  {"CLI", 0xFA},   {"STI", 0xFB},  {"CLD", 0xFC},
            {"STD", 0xFD},
            {"DAA", 0x27},  {"DAS", 0x2F},  {"AAA", 0x37},   {"AAS", 0x3F},
            {"INTO", 0xCE}, {"XLATB", 0xD7},
            // 186+ string I/O (MSHARD's REPZ INSW under .286c)
            {"INSB", 0x6C}, {"INSW", 0x6D}, {"OUTSB", 0x6E}, {"OUTSW", 0x6F},
            // 186+ all-register push/pop (MEMM/XMAEM protected-mode entry glue)
            {"PUSHA", 0x60}, {"POPA", 0x61}};
        static const std::map<std::string, std::uint8_t> jcc{
            {"JO", 0x70},  {"JNO", 0x71}, {"JB", 0x72},  {"JC", 0x72},   {"JNAE", 0x72},
            {"JNB", 0x73}, {"JNC", 0x73}, {"JAE", 0x73}, {"JE", 0x74},   {"JZ", 0x74},
            {"JNE", 0x75}, {"JNZ", 0x75}, {"JBE", 0x76}, {"JNA", 0x76},  {"JA", 0x77},
            {"JNBE", 0x77}, {"JS", 0x78}, {"JNS", 0x79}, {"JP", 0x7A},   {"JPE", 0x7A},
            {"JNP", 0x7B}, {"JPO", 0x7B}, {"JL", 0x7C},  {"JNGE", 0x7C}, {"JGE", 0x7D},
            {"JNL", 0x7D}, {"JLE", 0x7E}, {"JNG", 0x7E}, {"JG", 0x7F},   {"JNLE", 0x7F},
            {"LOOPNE", 0xE0}, {"LOOPNZ", 0xE0}, {"LOOPE", 0xE1}, {"LOOPZ", 0xE1},
            {"LOOP", 0xE2}, {"JCXZ", 0xE3}, {"JECXZ", 0xE3}};
        // reg,rm opcodes for the ALU family (MASM picks this direction for
        // reg,reg -- 33 C0 for xor ax,ax, 8B EC for mov bp,sp)
        static const std::map<std::string, std::uint8_t> alu_reg_rm{
            {"ADD", 0x03}, {"OR", 0x0B},  {"ADC", 0x13}, {"SBB", 0x1B},
            {"AND", 0x23}, {"SUB", 0x2B}, {"XOR", 0x33}, {"CMP", 0x3B}};

        if (op == "LOCK") {
            emit_byte(0xF0);
            if (!rest.empty() && rest[0].kind == Token::ident) {
                std::vector<Token> tail(rest.begin() + 1, rest.end());
                encode_instruction(upper(rest[0].text), tail);
            }
            return;
        }
        if (op == "REP" || op == "REPE" || op == "REPZ" || op == "REPNE" || op == "REPNZ") {
            emit_byte(op == "REPNE" || op == "REPNZ" ? 0xF2 : 0xF3);
            if (!rest.empty() && rest[0].kind == Token::ident) {
                std::vector<Token> tail(rest.begin() + 1, rest.end());
                encode_instruction(upper(rest[0].text), tail);
            }
            return;
        }

        // 386 dword register push/pop, caught before operand parsing (which
        // would reject EAX as an undefined symbol): the 66 operand-size prefix
        // ahead of the ordinary 50+r / 58+r (MEMM's `PUSH EAX` in a 16-bit
        // segment).
        if ((op == "PUSH" || op == "POP") && !rest.empty() &&
            rest[0].kind == Token::ident &&
            (rest.size() < 2 || rest[1].kind == Token::end)) {
            if (const auto r = reg32().find(upper(rest[0].text)); r != reg32().end()) {
                emit_byte(0x66);
                emit_byte(static_cast<std::uint8_t>((op == "PUSH" ? 0x50 : 0x58) + r->second));
                return;
            }
        }
        // MOV to/from a control register (0F 20 r32,CR / 0F 22 CR,r32), caught
        // before operand parsing rejects the CR name (MEMM's `mov eax,cr3`).
        if (op == "MOV" && rest.size() >= 3 && rest[0].kind == Token::ident &&
            rest[1].kind == Token::punct && rest[1].text == "," &&
            rest[2].kind == Token::ident) {
            const auto dst_cr = creg().find(upper(rest[0].text));
            const auto src_cr = creg().find(upper(rest[2].text));
            const auto dst_r = reg32().find(upper(rest[0].text));
            const auto src_r = reg32().find(upper(rest[2].text));
            if (dst_r != reg32().end() && src_cr != creg().end()) {
                emit_byte(0x0F);
                emit_byte(0x20);
                emit_modrm_reg(src_cr->second, dst_r->second);
                return;
            }
            if (dst_cr != creg().end() && src_r != reg32().end()) {
                emit_byte(0x0F);
                emit_byte(0x22);
                emit_modrm_reg(dst_cr->second, src_r->second);
                return;
            }
        }

        std::vector<Operand> operands = parse_operands(rest);

        if (op == "RET") {
            if (operands.empty()) {
                emit_byte(proc_far_ ? 0xCB : 0xC3);
            } else {
                emit_byte(proc_far_ ? 0xCA : 0xC2);
                emit_word(static_cast<std::uint16_t>(operands[0].value));
            }
            return;
        }
        if (const auto simple = zero_operand.find(op);
            simple != zero_operand.end() && op != "RET" &&
            !(op == "XLAT" && !operands.empty())) {
            emit_byte(simple->second);
            return;
        }
        // 386 dword string ops: the 66 prefix ahead of the word-string opcode
        // (MEMM's `movsd`, `stosd`, `repe cmpsd`).
        {
            static const std::map<std::string, std::uint8_t> string32{
                {"MOVSD", 0xA5}, {"CMPSD", 0xA7}, {"STOSD", 0xAB}, {"LODSD", 0xAD},
                {"SCASD", 0xAF}, {"INSD", 0x6D},  {"OUTSD", 0x6F}, {"PUSHFD", 0x9C},
                {"POPFD", 0x9D}, {"PUSHAD", 0x60},{"POPAD", 0x61}, {"IRETD", 0xCF},
                {"CWDE", 0x98},  {"CDQ", 0x99}};
            if (const auto s = string32.find(op); s != string32.end()) {
                emit_byte(0x66);
                emit_byte(s->second);
                return;
            }
        }
        if (op == "PUSH" || op == "POP") {
            const bool push = op == "PUSH";
            if (operands.size() == 1 && operands[0].kind == Operand::lab &&
                operands[0].size != 0) {
                operands[0] = label_as_memory(operands[0]);
            }
            if (operands.size() == 1 && operands[0].kind == Operand::r16) {
                emit_byte(static_cast<std::uint8_t>((push ? 0x50 : 0x58) + operands[0].reg));
                return;
            }
            if (operands.size() == 1 && operands[0].kind == Operand::seg) {
                if (operands[0].reg == 4 || operands[0].reg == 5) {
                    emit_byte(0x0F);
                    emit_byte(static_cast<std::uint8_t>((operands[0].reg == 4 ? 0xA0 : 0xA8) + (push ? 0 : 1)));
                    return;
                }
                static const std::uint8_t pushes[] = {0x06, 0x0E, 0x16, 0x1E};
                static const std::uint8_t pops[] = {0x07, 0x0F, 0x17, 0x1F};
                emit_byte(push ? pushes[operands[0].reg] : pops[operands[0].reg]);
                return;
            }
            if (operands.size() == 1 && operands[0].kind == Operand::mem) {
                emit_override(operands[0]);
                emit_byte(push ? 0xFF : 0x8F);
                emit_modrm_mem(push ? 6 : 0, operands[0]);
                return;
            }
            if (push && cpu_186_ && operands.size() == 1 &&
                operands[0].kind == Operand::imm) {
                // 186+ push of a plain constant that fits a signed byte is the
                // sign-extended `6A ib` short form (MEMM's `PUSH 50h`); a
                // relocatable or wider immediate is the full `68 iw` form
                // (MSHARD's `PUSH OFFSET ROMFRET`).
                if (operands[0].fixup_loc == 0 && !operands[0].from_offset &&
                    !(operands[0].size_explicit && operands[0].size == 2) &&
                    operands[0].value >= -128 && operands[0].value <= 127) {
                    emit_byte(0x6A);
                    emit_byte(static_cast<std::uint8_t>(operands[0].value & 0xFF));
                    return;
                }
                emit_byte(0x68);
                if (operands[0].fixup_group != 0) {
                    // `PUSH DGROUP`: the group's base paragraph (MEMM's MOVEB),
                    // a group base fixup like MOV's.
                    note_group_fixup(operands[0].fixup_loc, operands[0].fixup_group);
                    emit_word(0);
                    return;
                }
                if (operands[0].fixup_loc != 0) {
                    pending_frame_group_ = operands[0].frame_group;
                    if (operands[0].frame_segment > 0) {
                        pending_frame_segment_ = operands[0].frame_segment;
                    }
                    note_fixup(operands[0].fixup_loc, operands[0].fixup_segment,
                               operands[0].fixup_extern, false,
                               operands[0].value != 0
                                   ? (operands[0].value & 0xFFFF)
                                   : -1);
                    emit_word(0);
                    return;
                }
                emit_word(static_cast<std::uint16_t>(operands[0].value));
                return;
            }
            return fail("unsupported PUSH/POP form");
        }
        if (op == "XLAT" && !operands.empty()) {
            // `xlat es:[bx]`: the operand only contributes a prefix
            // (PARSE.ASM's case-map table is 26 D7).
            Operand& probe = operands[0];
            if (probe.kind == Operand::lab) probe = label_as_memory(probe);
            emit_override(probe);
            emit_byte(0xD7);
            return;
        }
        {
            // Explicit-operand string forms: the operand only contributes
            // its size and any segment prefix (DEBCOM2's
            // `LODS CS:WORD PTR [SI]` is 2E AD).
            static const std::map<std::string, std::uint8_t> string_ops{
                {"MOVS", 0xA4}, {"CMPS", 0xA6}, {"STOS", 0xAA},
                {"LODS", 0xAC}, {"SCAS", 0xAE}};
            if (const auto entry = string_ops.find(op); entry != string_ops.end()) {
                if (operands.empty()) return fail(op + " needs an operand");
                for (Operand& o : operands) {
                    if (o.kind == Operand::lab) o = label_as_memory(o);
                }
                // Only the DS:SI side's override can emit; the ES:DI side
                // is hardwired, so MASM drops a prefix there. MOVS' source
                // is its SECOND operand (KEYBCPSD's `REP MOVS ES:BYTE PTR
                // [DI],DS:[SI]` is the bare F3 A4), LODS' and CMPS' is the
                // first (DEBCOM2's `LODS CS:WORD PTR [SI]` is 2E AD);
                // STOS/SCAS take none.
                if (op == "MOVS") {
                    if (operands.size() > 1) emit_override(operands[1]);
                } else if (op == "LODS" || op == "CMPS") {
                    emit_override(operands[0]);
                }
                const bool word =
                    operands[0].size == 2 ||
                    (operands[0].size == 0 && operands.size() > 1 &&
                     operands[1].size == 2);
                emit_byte(static_cast<std::uint8_t>(entry->second + (word ? 1 : 0)));
                return;
            }
        }
        if (op == "INT") {
            if (operands.size() == 1) {
                emit_byte(0xCD);
                emit_byte(static_cast<std::uint8_t>(operands[0].value));
                return;
            }
            return fail("unsupported INT form");
        }
        if (const auto conditional = jcc.find(op); conditional != jcc.end()) {
            if (operands.size() == 1 &&
                (operands[0].kind == Operand::lab || operands[0].kind == Operand::imm)) {
                const long target = operands[0].value;
                const bool backward =
                    operands[0].kind == Operand::lab && operands[0].defined_at_use;
                // Under .386, a forward conditional jump reserves the near
                // form (0F 8x + rel16) in pass 1 and keeps it, like the near
                // unconditional forward JMP -- MEMM's I286 keeps `0F 85 0000`
                // with a self-relative fixup even where the target lands in
                // short range. LOOP/JCXZ (no near form) stay short.
                const bool backward_near =
                    cpu_386_ && backward && operands[0].kind == Operand::lab &&
                    (target - (segment().location + 2) < -128 ||
                     target - (segment().location + 2) > 127);
                if (cpu_386_ && (!backward || backward_near) && op != "LOOP" &&
                    op != "LOOPE" && op != "LOOPNE" && op != "LOOPZ" && op != "LOOPNZ" &&
                    op != "JCXZ" && op != "JECXZ" && operands[0].kind == Operand::lab) {
                    emit_byte(0x0F);
                    emit_byte(static_cast<std::uint8_t>(conditional->second + 0x10));
                    note_fixup(1, operands[0].fixup_segment, operands[0].fixup_extern,
                               true, target);
                    emit_word(0);
                    return;
                }
                if (op == "JECXZ" && segment().use_attr != 1) emit_byte(0x67);
                emit_byte(conditional->second);
                const long rel = target - (segment().location + 1);
                if (pass_ == 2 && (rel < -128 || rel > 127)) fail("relative jump out of range");
                emit_byte(static_cast<std::uint8_t>(rel));
                return;
            }
            return fail("unsupported conditional jump form");
        }
        if (op == "JMP") {
            if (operands.size() == 1 && operands[0].kind == Operand::imm &&
                operands[0].fixup_loc == 0) {
                // A computed target (`jmp $+3`) resolves like a defined
                // backward label: short when it fits.
                const long rel = operands[0].value - (segment().location + 2);
                if (rel >= -128 && rel <= 127) {
                    emit_byte(0xEB);
                    emit_byte(static_cast<std::uint8_t>(rel));
                } else {
                    emit_byte(0xE9);
                    emit_word(static_cast<std::uint16_t>(
                        operands[0].value - (segment().location + 2)));
                }
                return;
            }
            if (operands.size() == 1 && operands[0].kind == Operand::lab &&
                (operands[0].size == 2 || operands[0].size == 4)) {
                // A jump whose target is a word/dword DATA variable is an
                // indirect memory jump (DISP's `JMP restore_tmp` through
                // the I_need'd word is 2E FF 26).
                operands[0] = label_as_memory(operands[0]);
            }
            if (operands.size() == 1 && operands[0].kind == Operand::lab &&
                (operands[0].far_override ||
                 (symbols_.contains(operands[0].label_name) &&
                  symbols_.at(operands[0].label_name).far_label &&
                  // A FAR extern is far at every use (MSINIT's `JMP
                  // SYSINIT` is EA with a pointer32 fixup).
                  (defined_this_pass_.contains(operands[0].label_name) ||
                   symbols_.at(operands[0].label_name).extern_index > 0)))) {
                // A backward far label still optimizes to the short form
                // when in range (CHKPROC's EB B6 to NULLDIRERR); an
                // explicit `far ptr` or an out-of-range target is the
                // direct far jump: EA, four zeroed bytes, a pointer fixup
                // carrying the target offset (`JMP far ptr ROOTDIR`).
                const long rel_far =
                    operands[0].value - (segment().location + 2);
                // The short-form optimization of a backward far label only
                // applies when the target lives in the CURRENT segment; a far
                // label in another segment (REBOOT's `JMP POR` into ROMBIOS)
                // can never be reached by a same-segment EB/E9 and stays the
                // EA direct far jump with a pointer32 fixup.
                const bool same_segment =
                    symbols_.contains(operands[0].label_name) &&
                    symbols_.at(operands[0].label_name).segment ==
                        current_segment_;
                if (!operands[0].far_override && same_segment &&
                    operands[0].defined_at_use && rel_far >= -128 &&
                    rel_far <= 127) {
                    emit_byte(0xEB);
                    emit_byte(static_cast<std::uint8_t>(
                        operands[0].value - (segment().location + 1)));
                    return;
                }
                emit_byte(0xEA);
                note_fixup(3, operands[0].fixup_segment, operands[0].fixup_extern,
                           false, operands[0].value);
                emit_word(0);
                emit_word(0);
                return;
            }
            if (operands.size() == 1 && operands[0].kind == Operand::lab) {
                const bool wants_short = operands[0].size == -1;
                const long target = operands[0].value;
                const bool backward = operands[0].defined_at_use;
                if (operands[0].fixup_extern > 0) {
                    // An external near jump has no in-module offset to
                    // measure: always the E9 near form with a self-relative
                    // fixup (DEBERR's `jmp RESTART`); an explicit group
                    // qualifier frames it (COMMAND1's `JMP
                    // RESGROUP:CONPROC`).
                    emit_byte(0xE9);
                    if (operands[0].frame_group > 0) {
                        pending_frame_group_ = operands[0].frame_group;
                    } else if (operands[0].prefix_group > 0) {
                        pending_frame_group_ = operands[0].prefix_group;
                    }
                    if (operands[0].frame_segment > 0) {
                        pending_frame_segment_ = operands[0].frame_segment;
                    }
                    note_fixup(1, 0, operands[0].fixup_extern, true, target);
                    emit_word(0);
                    return;
                }
                if (wants_short ||
                    (backward && target - (segment().location + 2) >= -128)) {
                    emit_byte(0xEB);
                    emit_byte(static_cast<std::uint8_t>(target - (segment().location + 1)));
                    return;
                }
                if (backward) {
                    // A backward jump beyond short range is near -- and MASM
                    // still routes it through a self-relative fixup with a
                    // zeroed word (F_NEXT.OBJ's jmp DoFindNext).
                    emit_byte(0xE9);
                    note_fixup(1, operands[0].fixup_segment, operands[0].fixup_extern,
                               true, target);
                    emit_word(0);
                    return;
                }
                // A forward JMP without SHORT reserves the near form in pass
                // 1; when pass 2 finds the target in short range, MASM keeps
                // the layout and pads with a NOP: EB xx 90 (measured off
                // BEEP.OBJ).
                const long rel_short = target - (segment().location + 2);
                if (pass_ == 2 && rel_short >= -128 && rel_short <= 127) {
                    emit_byte(0xEB);
                    emit_byte(static_cast<std::uint8_t>(rel_short));
                    emit_byte(0x90);
                } else {
                    // A forward near jump that stays E9 keeps its word
                    // zeroed and takes a SELF-RELATIVE fixup with the
                    // target offset (F_NEXT.OBJ).
                    emit_byte(0xE9);
                    note_fixup(1, operands[0].fixup_segment, operands[0].fixup_extern,
                               true, target);
                    emit_word(0);
                }
                return;
            }
            if (operands.size() == 1 && operands[0].kind == Operand::r16) {
                // `JMP BX`: register-indirect near jump.
                emit_byte(0xFF);
                emit_modrm_reg(4, operands[0].reg);
                return;
            }
            if (operands.size() == 1 && operands[0].kind == Operand::mem &&
                !operands[0].has_base_bx && !operands[0].has_base_bp &&
                !operands[0].has_si && !operands[0].has_di &&
                operands[0].fixup_extern > 0 && operands[0].size == 0) {
                // Brackets around a NEAR-code external are decoration:
                // RECOVER's `jmp [exitpgm]` with `extrn exitpgm:near` is
                // the direct E9 with a self-relative fixup, not FF /4 --
                // indirection needs a word-sized variable. An explicit
                // group qualifier frames it (COMMAND1's `JMP
                // RESGROUP:CONPROC`).
                emit_byte(0xE9);
                if (operands[0].frame_group > 0) {
                    pending_frame_group_ = operands[0].frame_group;
                } else if (operands[0].prefix_group > 0) {
                    pending_frame_group_ = operands[0].prefix_group;
                }
                note_fixup(1, 0, operands[0].fixup_extern, true,
                           operands[0].value);
                emit_word(0);
                return;
            }
            if (operands.size() == 1 && operands[0].kind == Operand::mem) {
                // `JMP [INT_2E_RET]` / `JMP WORD PTR [BX+1]`: memory-
                // indirect, /4 near or /5 far by operand size.
                Operand& target = operands[0];
                emit_override(target);
                emit_byte(0xFF);
                emit_modrm_mem(target.size == 4 ? 5 : 4, target);
                return;
            }
            return fail("unsupported JMP form");
        }
        if (op == "CALL") {
            if (operands.size() == 1 && operands[0].kind == Operand::lab) {
                const auto symbol = symbols_.find(operands[0].label_name);
                const bool far_proc =
                    operands[0].far_override ||
                    (symbol != symbols_.end() && symbol->second.far_label);
                const bool dword_variable = operands[0].size == 4;
                // A call whose target is a word/dword DATA variable (a
                // function pointer, size 2/4) is an indirect memory call,
                // not a direct one -- MSCLOCK calls through its relocatable
                // BinToBCD/TimeToTicks word pointers. Code labels are size 0.
                const bool word_variable = operands[0].size == 2;
                if (far_proc && !dword_variable) {
                    // Direct far call: 9A, four zeroed bytes, a pointer
                    // fixup carrying the target offset (QCURDSK.OBJ).
                    emit_byte(0x9A);
                    note_fixup(3, operands[0].fixup_segment, operands[0].fixup_extern,
                               false, operands[0].value);
                    emit_word(0);
                    emit_word(0);
                    return;
                }
                if (!dword_variable && !word_variable &&
                    operands[0].segment_override < 0) {
                    if (pass_ == 1 && !operands[0].defined_at_use &&
                        operands[0].fixup_extern == 0) {
                        forward_labels_.insert(operands[0].label_name);
                    }
                    emit_byte(0xE8);
                    // A near call to a label ALWAYS relocates -- MASM
                    // never resolves the E8 displacement itself
                    // (_MSGRET's backward call to its own local).
                    if (true) {
                        note_fixup(1, operands[0].fixup_segment, operands[0].fixup_extern,
                                   true, operands[0].value);
                        emit_word(0);
                        return;
                    }
                    emit_word(static_cast<std::uint16_t>(operands[0].value -
                                                         (segment().location + 2)));
                    return;
                }
            }
            if (operands.size() == 1 &&
                (operands[0].kind == Operand::mem || operands[0].kind == Operand::lab)) {
                Operand target = operands[0].kind == Operand::lab
                                     ? label_as_memory(operands[0])
                                     : operands[0];
                emit_override(target);
                emit_byte(0xFF);
                emit_modrm_mem(target.size == 4 ? 3 : 2, target);
                return;
            }
            if (operands.size() == 1 && operands[0].kind == Operand::r16) {
                emit_byte(0xFF);
                emit_modrm_reg(2, operands[0].reg);
                return;
            }
            return fail("unsupported CALL form");
        }
        if (op == "MOV") {
            if (operands.size() != 2) return fail("MOV needs two operands");
            Operand& dst = operands[0];
            Operand& src = operands[1];
            if (dst.kind == Operand::lab) dst = label_as_memory(dst);
            if (pass_ == 1 && dst.kind == Operand::mem && src.kind == Operand::lab && src.forward_ref) {
                src.kind = Operand::imm;
                src.value = 0x100;
                src.forward_const = true;
            }
            if (src.kind == Operand::lab) src = label_as_memory(src);
            if (dst.kind == Operand::r16 && src.kind == Operand::r16) {
                emit_opsize(dst.wide || src.wide);
                emit_byte(0x8B);
                emit_modrm_reg(dst.reg, src.reg);
                return;
            }
            if (dst.kind == Operand::r8 && src.kind == Operand::r8) {
                emit_byte(0x8A);
                emit_modrm_reg(dst.reg, src.reg);
                return;
            }
            if (dst.kind == Operand::r16 && src.kind == Operand::mem) {
                emit_override(src);
                emit_opsize(dst.wide);
                if (dst.reg == 0 && is_direct(src)) {
                    emit_byte(0xA1);
                    emit_direct_disp(src);
                    return;
                }
                emit_byte(0x8B);
                emit_modrm_mem(dst.reg, src);
                return;
            }
            if (dst.kind == Operand::mem && src.kind == Operand::r16) {
                emit_override(dst);
                emit_opsize(src.wide);
                if (src.reg == 0 && is_direct(dst)) {
                    emit_byte(0xA3);
                    emit_direct_disp(dst);
                    return;
                }
                emit_byte(0x89);
                emit_modrm_mem(src.reg, dst);
                return;
            }
            if (dst.kind == Operand::r8 && src.kind == Operand::mem) {
                emit_override(src);
                if (dst.reg == 0 && is_direct(src)) {
                    emit_byte(0xA0);
                    emit_direct_disp(src);
                    return;
                }
                emit_byte(0x8A);
                emit_modrm_mem(dst.reg, src);
                return;
            }
            if (dst.kind == Operand::mem && src.kind == Operand::r8) {
                emit_override(dst);
                if (src.reg == 0 && is_direct(dst)) {
                    emit_byte(0xA2);
                    emit_direct_disp(dst);
                    return;
                }
                emit_byte(0x88);
                emit_modrm_mem(src.reg, dst);
                return;
            }
            if (dst.kind == Operand::r16 && src.kind == Operand::imm) {
                emit_opsize(dst.wide);
                if (dst.wide && src.fixup_loc == 0) {
                    // 66 B8+r imm32 (MEMM's `mov eax,4000h`).
                    emit_byte(static_cast<std::uint8_t>(0xB8 + dst.reg));
                    emit_dword(static_cast<std::uint32_t>(src.value));
                    return;
                }
                emit_byte(static_cast<std::uint8_t>(0xB8 + dst.reg));
                if (src.fixup_group != 0) {
                    note_group_fixup(src.fixup_loc, src.fixup_group);
                    emit_word(0);
                    return;
                }
                if (src.fixup_loc != 0 && src.fixup_segment != 0) {
                    pending_frame_group_ = src.frame_group;
                    if (src.frame_segment > 0) {
                        pending_frame_segment_ = src.frame_segment;
                    }
                    note_fixup(src.fixup_loc, src.fixup_segment, 0, false, src.value);
                    emit_word(0);
                    return;
                }
                if (src.fixup_loc != 0 && src.fixup_extern != 0) {
                    // OFFSET of an external, with any `+n` addend riding in
                    // the relocation (CHKPRMT's `OFFSET DG:CONBUF+2`);
                    // a segment qualifier frames the named segment
                    // (FORLABEL's `offset data:MsgCRLF`).
                    pending_frame_group_ = src.frame_group;
                    if (src.frame_segment > 0) {
                        pending_frame_segment_ = src.frame_segment;
                    }
                    note_fixup(src.fixup_loc, 0, src.fixup_extern, false,
                               src.value != 0 ? (src.value & 0xFFFF) : -1);
                    emit_word(0);
                    return;
                }
                operand_fixup(src);
                emit_word(src.fixup_loc != 0 ? 0
                                             : static_cast<std::uint16_t>(src.value));
                // Pass 1 sized the forward constant as memory: 8B /r
                // disp16 = 4 for a non-accumulator (EDLIN's `MOV
                // CX,NUMCOM` = B9 0F 00 90), but AX's load is A1 disp16
                // = 3 -- the same length as B8 imm16, no pad (INIT's
                // `MOV AX,DOS_GET_EXT_PARSE_ADD`).
                if (src.forward_const && dst.reg != 0) emit_byte(0x90);
                return;
            }
            if (dst.kind == Operand::r8 && src.kind == Operand::imm) {
                emit_byte(static_cast<std::uint8_t>(0xB0 + dst.reg));
                if (src.fixup_extern != 0) {
                    // A byte immediate from an ABS external rides a LOBYTE
                    // fixup, exactly as the memory store does (MODEPRIN's
                    // `MOV DH,utility_msg_class`, utility_msg_class:ABS).
                    pending_frame_group_ = src.frame_group;
                    note_fixup(0, 0, src.fixup_extern, false,
                               src.value != 0 ? (src.value & 0xFFFF) : -1);
                    emit_byte(0);
                    return;
                }
                emit_byte(static_cast<std::uint8_t>(src.value));
                // Pass 1 sized a forward constant as memory: 8A /r
                // disp16 = 4 for a non-accumulator, so the byte-register
                // form pads TWO NOPs (INIT's `MOV DL,DOS_SET_ADDR` =
                // B2 09 90 90); AL's load is A0 disp16 = 3, one pad.
                if (src.forward_const) {
                    emit_byte(0x90);
                    if (dst.reg != 0) emit_byte(0x90);
                }
                return;
            }
            if (dst.kind == Operand::seg && (src.kind == Operand::r16 || src.kind == Operand::mem)) {
                if (src.kind == Operand::mem) {
                    emit_override(src);
                    emit_byte(0x8E);
                    emit_modrm_mem(dst.reg, src);
                } else {
                    emit_byte(0x8E);
                    emit_modrm_reg(dst.reg, src.reg);
                }
                return;
            }
            if ((dst.kind == Operand::r16 || dst.kind == Operand::mem) && src.kind == Operand::seg) {
                if (dst.kind == Operand::mem) {
                    emit_override(dst);
                    emit_byte(0x8C);
                    emit_modrm_mem(src.reg, dst);
                } else {
                    emit_byte(0x8C);
                    emit_modrm_reg(src.reg, dst.reg);
                }
                return;
            }
            if (dst.kind == Operand::mem && src.kind == Operand::imm) {
                emit_override(dst);
                // An explicit type on the immediate can size an otherwise
                // untyped destination (MODEPARS's `MOV ES:[DI],BYTE PTR
                // blank` is C6, not the default C7 word store).
                int size = dst.size != 0
                               ? dst.size
                               : (src.size_explicit && src.size != 0 ? src.size
                                                                    : 2);
                // An immediate store through an extern base takes the
                // EXTERN's declared width over a field selector's
                // (RUCODE's `MOV $M_RT.$M_MSG_NUM,0` through the BYTE
                // $M_RT2 is C6); when the extern was declared lazily
                // AFTER the use, pass 1 reserved the word form and the
                // shrunken store pads a NOP.
                bool lazy_shrink = false;
                size = lazy_extern_store_size(dst, size, &lazy_shrink);
                // Pass 1 sizes a store through an unknown base as a
                // WORD even when the field is byte-wide (MSGSERV's
                // `MOV $M_RT.$M_SIZE,$M_NULL` reserves C7; pass 2's C6
                // pads the NOP). ALU compares stay field-sized -- their
                // byte and word imm8 forms match in length.
                if (dst.lazy_base && size == 1 && !dst.size_explicit) size = 2;
                // A forward local data reference reserved the word form in
                // pass 1 (its size unknown); pass 2 finds it byte-wide and
                // keeps the layout with a byte store plus a NOP, exactly as a
                // lazy extern does -- measured off FWD.OBJ, where real MASM
                // emits `MOV fwd_byte,0` as C6 06 <disp> 00 90. An explicit
                // BYTE PTR resolves the size in pass 1 and opts out.
                if (dst.forward_ref && !dst.size_explicit && size == 1) {
                    lazy_shrink = true;
                }
                emit_byte(size == 1 ? 0xC6 : 0xC7);
                emit_modrm_mem(0, dst);
                if (size == 1 && src.fixup_extern != 0) {
                    // A byte immediate from an ABS external takes a
                    // LOBYTE fixup (DCOMPPAR's `MOV EXITFL,EXPAR`).
                    pending_frame_group_ = src.frame_group;
                    note_fixup(0, 0, src.fixup_extern, false,
                               src.value != 0 ? (src.value & 0xFFFF) : -1);
                    emit_byte(0);
                    return;
                }
                if (size != 1 && src.fixup_loc != 0) {
                    // A relocated immediate stored to memory keeps its
                    // fixup (DEBUASM's `mov [OPCODE],offset dg:ESCMN`).
                    pending_frame_group_ = src.frame_group;
                    if (src.frame_segment > 0) {
                        pending_frame_segment_ = src.frame_segment;
                    }
                    if (src.fixup_segment != 0) {
                        // Keep the source operator's relocation kind: SEG is
                        // a base fixup while OFFSET is offset16. INDEINI stores
                        // `SEG PROG` directly into four TSS fields; real MASM
                        // emits loc-base for each immediate word.
                        note_fixup(src.fixup_loc, src.fixup_segment, 0, false,
                                   src.value);
                    } else {
                        note_fixup(1, 0, src.fixup_extern, false,
                                   src.value != 0 ? (src.value & 0xFFFF) : -1);
                    }
                    emit_word(0);
                    return;
                }
                if (size == 1) emit_byte(static_cast<std::uint8_t>(src.value));
                else emit_word(static_cast<std::uint16_t>(src.value));
                if (lazy_shrink) emit_byte(0x90);
                return;
            }
            return fail("unsupported MOV form");
        }
        if (const auto alu = alu_reg_rm.find(op); alu != alu_reg_rm.end()) {
            if (operands.size() != 2) return fail(op + " needs two operands");
            Operand& dst = operands[0];
            Operand& src = operands[1];
            if (dst.kind == Operand::lab) dst = label_as_memory(dst);
            if (pass_ == 1 && dst.kind == Operand::mem && src.kind == Operand::lab && src.forward_ref) {
                src.kind = Operand::imm;
                src.value = 0x100;
                src.forward_const = true;
            }
            if (src.kind == Operand::lab) src = label_as_memory(src);
            const std::uint8_t base = static_cast<std::uint8_t>(alu->second & ~0x02);
            if (dst.kind == Operand::r16 && src.kind == Operand::r16) {
                emit_opsize(dst.wide || src.wide);
                emit_byte(alu->second);
                emit_modrm_reg(dst.reg, src.reg);
                return;
            }
            if (dst.kind == Operand::r8 && src.kind == Operand::r8) {
                emit_byte(static_cast<std::uint8_t>(alu->second - 1));
                emit_modrm_reg(dst.reg, src.reg);
                return;
            }
            if (dst.kind == Operand::r16 && src.kind == Operand::mem) {
                emit_override(src);
                emit_opsize(dst.wide);
                emit_byte(alu->second);
                emit_modrm_mem(dst.reg, src);
                return;
            }
            if (dst.kind == Operand::mem && src.kind == Operand::r16) {
                // `base` already clears the direction bit, so it is the
                // r/m16,r16 opcode (SUB [mem],cx = 29, MSLPT's COUNT store).
                emit_override(dst);
                emit_opsize(src.wide);
                emit_byte(base);
                emit_modrm_mem(src.reg, dst);
                return;
            }
            const int group_index = (alu->second >> 3) & 7;
            if (dst.kind == Operand::r16 && src.kind == Operand::imm) {
                // A 32-bit immediate that fits a signed byte takes the short
                // 66 83 /r ib form even for EAX -- the accumulator imm32 form
                // (66 25/05/..) would be two bytes longer, so MASM only picks
                // it when the value needs the full dword. (16-bit ties, so it
                // keeps the accumulator form there -- handled below.)
                if (dst.wide && src.fixup_loc == 0 && !src.forward_const &&
                    !src.from_offset && fits_signed_byte_wide(src.value)) {
                    emit_opsize(true);
                    emit_byte(0x83);
                    emit_modrm_reg(group_index, dst.reg);
                    emit_byte(static_cast<std::uint8_t>(src.value));
                    return;
                }
                // Measured off EXIT.OBJ's `cmp ax,1` = 3D 01 00: the
                // accumulator-immediate form beats 83 even for tiny values.
                if (dst.reg == 0) {
                    emit_opsize(dst.wide);
                    emit_byte(static_cast<std::uint8_t>((group_index << 3) | 0x05));
                    operand_fixup(src);
                    if (dst.wide && src.fixup_loc == 0) {
                        emit_dword(static_cast<std::uint32_t>(src.value));
                        return;
                    }
                    emit_word(static_cast<std::uint16_t>(src.fixup_loc != 0 ? 0
                                                                            : src.value));
                    // Pass 1 sized a forward constant as memory (4 bytes);
                    // the shrunken accumulator form pads with NOP
                    // (MSLOAD's `sub ax,LoadBio_Size`).
                    if (src.forward_const) emit_byte(0x90);
                    return;
                }
                if (src.fixup_loc != 0) {
                    // A relocated immediate patches sixteen bits: the 81
                    // word form, data zeroed (DEBUASM's `add bx,offset ...`).
                    // (also reached for forward constants, size-stable)
                    emit_byte(0x81);
                    emit_modrm_reg(group_index, dst.reg);
                    if (src.fixup_segment != 0) {
                        pending_frame_group_ = src.frame_group;
                        if (src.frame_segment > 0) {
                            pending_frame_segment_ = src.frame_segment;
                        }
                        // A bare segment name is its base paragraph (loc 2 --
                        // MEMM's `sub bx,_TEXT`); OFFSET keeps loc 1.
                        note_fixup(src.fixup_loc, src.fixup_segment, 0, false, src.value);
                    } else if (src.fixup_extern != 0) {
                        pending_frame_group_ = src.frame_group;
                        if (src.frame_segment > 0) {
                            pending_frame_segment_ = src.frame_segment;
                        }
                        note_fixup(1, 0, src.fixup_extern, false,
                                   src.value != 0 ? (src.value & 0xFFFF) : -1);
                    }
                    emit_word(0);
                    return;
                }
                const bool byte_fits = dst.wide ? fits_signed_byte_wide(src.value)
                                                : fits_signed_byte(src.value);
                if (byte_fits && !src.forward_const && !src.from_offset) {
                    emit_opsize(dst.wide);
                    emit_byte(0x83);
                    emit_modrm_reg(group_index, dst.reg);
                    emit_byte(static_cast<std::uint8_t>(src.value));
                } else {
                    emit_opsize(dst.wide);
                    emit_byte(0x81);
                    emit_modrm_reg(group_index, dst.reg);
                    if (dst.wide) emit_dword(static_cast<std::uint32_t>(src.value));
                    else emit_word(static_cast<std::uint16_t>(src.value));
                }
                return;
            }
            if (dst.kind == Operand::r8 && src.kind == Operand::imm) {
                if (dst.reg == 0) {
                    emit_byte(static_cast<std::uint8_t>((group_index << 3) | 0x04));
                    if (src.fixup_extern != 0) {
                        pending_frame_group_ = src.frame_group;
                        note_fixup(0, 0, src.fixup_extern, false,
                                   src.value != 0 ? (src.value & 0xFFFF) : -1);
                        emit_byte(0);
                        return;
                    }
                    emit_byte(static_cast<std::uint8_t>(src.value));
                    // Pass 1 sized a forward constant as memory (4
                    // bytes); the byte accumulator form pads TWO NOPs
                    // (SYSINIT2's `cmp al,SWTCHR` = 3C 2F 90 90).
                    if (src.forward_const) {
                        emit_byte(0x90);
                        emit_byte(0x90);
                    }
                    return;
                }
                emit_byte(0x80);
                emit_modrm_reg(group_index, dst.reg);
                if (src.fixup_extern != 0) {
                    // A byte immediate from an ABS external rides a LOBYTE
                    // fixup here too (SCN_PARM's structured `.IF < DL EQ
                    // E_CR >` compares against the E_CR:ABS extern).
                    pending_frame_group_ = src.frame_group;
                    note_fixup(0, 0, src.fixup_extern, false,
                               src.value != 0 ? (src.value & 0xFFFF) : -1);
                    emit_byte(0);
                    return;
                }
                emit_byte(static_cast<std::uint8_t>(src.value));
                return;
            }
            if (dst.kind == Operand::mem && src.kind == Operand::imm) {
                emit_override(dst);
                const int size = dst.size != 0 ? dst.size : 2;
                if (size == 1) {
                    emit_byte(0x80);
                    emit_modrm_mem(group_index, dst);
                    if (src.fixup_extern != 0) {
                        // A byte immediate from an ABS external rides a
                        // LOBYTE fixup (TREEPAR's `OR FLAGS,F_SWITCH`).
                        pending_frame_group_ = src.frame_group;
                        note_fixup(0, 0, src.fixup_extern, false,
                                   src.value != 0 ? (src.value & 0xFFFF) : -1);
                        emit_byte(0);
                        return;
                    }
                    emit_byte(static_cast<std::uint8_t>(src.value));
                    return;
                }
                // A relocated immediate patches sixteen bits, so the
                // sign-extended 83 form is out (TCMD1A's `cmp [X],offset y`
                // is 81 3E .. 0000 with the fixup on the word). A DWORD PTR
                // target rides the 66 prefix and a dword immediate (MEMM's
                // `xor dword ptr [di],100000h` = 66 81 35 00001000). And the
                const bool wide = size == 4;
                // An otherwise untyped memory operand inherits its width
                // from a word-valued literal. MASM keeps that literal in the
                // 81 form even when its value could be sign-extended from a
                // byte (OEMPROC's `XOR ES:[BX],0FFFFh`). Small literals do
                // not establish a word width and retain the ordinary rule.
                const bool immediate_establishes_word =
                    dst.size == 0 && src.value > 0xFF;
                const bool fits =
                    !immediate_establishes_word && !src.forward_const && src.fixup_loc == 0 &&
                    (wide ? fits_signed_byte_wide(src.value) : fits_signed_byte(src.value));
                emit_opsize(wide);
                emit_byte(fits ? 0x83 : 0x81);
                emit_modrm_mem(group_index, dst);
                if (fits) {
                    emit_byte(static_cast<std::uint8_t>(src.value));
                } else if (wide && src.fixup_loc == 0) {
                    emit_dword(static_cast<std::uint32_t>(src.value));
                } else {
                    operand_fixup(src);
                    emit_word(src.fixup_loc != 0
                                  ? 0
                                  : static_cast<std::uint16_t>(src.value));
                }
                return;
            }
            if (dst.kind == Operand::mem && src.kind == Operand::r8) {
                // r/m8,r8 is base-1 (CMP es:[di],dh = 26 38 35 in _MSGRET).
                emit_override(dst);
                emit_byte(static_cast<std::uint8_t>(base - 1));
                emit_modrm_mem(src.reg, dst);
                return;
            }
            if (dst.kind == Operand::r8 && src.kind == Operand::mem) {
                emit_override(src);
                emit_byte(static_cast<std::uint8_t>(alu->second - 1));
                emit_modrm_mem(dst.reg, src);
                return;
            }
            return fail("unsupported " + op + " form");
        }
        if (op == "MOVZX" || op == "MOVSX") {
            // 386 zero/sign-extend into a wider register (MEMM's
            // `movzx eax,bl`, `movzx edx,[si.field]`). The second opcode byte
            // picks the SOURCE width: B6/BE from a byte, B7/BF from a word.
            if (operands.size() != 2) return fail(op + " needs two operands");
            Operand& dst = operands[0];
            Operand& src = operands[1];
            if (src.kind == Operand::lab) src = label_as_memory(src);
            const int srcsize = src.kind == Operand::r8    ? 1
                                : src.kind == Operand::r16 ? 2
                                : src.size != 0            ? src.size
                                                           : 1;
            const std::uint8_t op2 = static_cast<std::uint8_t>(
                (op == "MOVZX" ? 0xB6 : 0xBE) + (srcsize == 1 ? 0 : 1));
            if (src.kind == Operand::mem) emit_override(src);
            emit_opsize(dst.wide);
            emit_byte(0x0F);
            emit_byte(op2);
            if (src.kind == Operand::mem) {
                emit_modrm_mem(dst.reg, src);
            } else {
                emit_modrm_reg(dst.reg, src.reg);
            }
            return;
        }
        if (op == "INC" || op == "DEC") {
            if (operands.size() != 1) return fail(op + " needs one operand");
            Operand& target = operands[0];
            if (target.kind == Operand::lab) target = label_as_memory(target);
            const int slot = op == "INC" ? 0 : 1;
            if (target.kind == Operand::r16) {
                emit_opsize(target.wide);
                emit_byte(static_cast<std::uint8_t>((op == "INC" ? 0x40 : 0x48) + target.reg));
                return;
            }
            if (target.kind == Operand::r8) {
                emit_byte(0xFE);
                emit_modrm_reg(slot, target.reg);
                return;
            }
            if (target.kind == Operand::mem) {
                emit_override(target);
                emit_byte(target.size == 1 ? 0xFE : 0xFF);
                emit_modrm_mem(slot, target);
                return;
            }
            return fail("unsupported INC/DEC form");
        }
        if (op == "LEA") {
            if (operands.size() == 2 && operands[1].kind == Operand::lab) {
                operands[1] = label_as_memory(operands[1]);
            }
            if (operands.size() == 2 && operands[0].kind == Operand::r16 &&
                operands[1].kind == Operand::mem) {
                emit_byte(0x8D);
                emit_modrm_mem(operands[0].reg, operands[1]);
                return;
            }
            return fail("unsupported LEA form");
        }
        if (op == "LDS" || op == "LES") {
            if (operands.size() == 2 && operands[1].kind == Operand::lab) {
                operands[1] = label_as_memory(operands[1]);
            }
            if (operands.size() == 2 && operands[0].kind == Operand::r16 &&
                operands[1].kind == Operand::mem) {
                emit_override(operands[1]);
                emit_byte(op == "LDS" ? 0xC5 : 0xC4);
                emit_modrm_mem(operands[0].reg, operands[1]);
                return;
            }
            return fail("unsupported " + op + " form");
        }
        if (op == "TEST") {
            if (operands.size() != 2) return fail("TEST needs two operands");
            Operand& a = operands[0];
            Operand& b = operands[1];
            // A forward EQU as the source of a MEMORY test is an
            // immediate, not a second memory operand (SYSINIT2's `test
            // word ptr Switches,flagec35` with flagec35 EQU'd later); a
            // register destination keeps the 4-byte 85 /r reservation
            // pass 2's padded accumulator form matches.
            if (b.kind == Operand::lab && b.fixup_loc == 0 &&
                b.fixup_segment == 0 && b.fixup_extern == 0 &&
                (a.kind == Operand::lab || a.kind == Operand::mem)) {
                b.kind = Operand::imm;
            }
            if (a.kind == Operand::lab) a = label_as_memory(a);
            if (b.kind == Operand::lab) b = label_as_memory(b);
            const bool word = a.size == 2 || b.size == 2;
            if ((a.kind == Operand::r16 || a.kind == Operand::r8) &&
                (b.kind == Operand::r16 || b.kind == Operand::r8)) {
                emit_byte(word ? 0x85 : 0x84);
                // Register-register TEST puts the FIRST operand in the
                // reg field (DEBCOM2's `TEST AX,BX` is 85 C3).
                emit_modrm_reg(a.reg, b.reg);
                return;
            }
            if (a.kind == Operand::mem &&
                (b.kind == Operand::r16 || b.kind == Operand::r8)) {
                emit_override(a);
                emit_byte(word ? 0x85 : 0x84);
                emit_modrm_mem(b.reg, a);
                return;
            }
            if (b.kind == Operand::mem &&
                (a.kind == Operand::r16 || a.kind == Operand::r8)) {
                emit_override(b);
                emit_byte(word ? 0x85 : 0x84);
                emit_modrm_mem(a.reg, b);
                return;
            }
            if (a.kind == Operand::imm && b.kind == Operand::imm &&
                a.size_explicit) {
                // A forward `word ptr Switches` resolves as a bare value
                // in pass 1; the explicit cast marks it memory.
                a.kind = Operand::mem;
            }
            if (a.kind == Operand::mem && b.kind == Operand::imm) {
                // `test word ptr Switches,flagec35` -- F6/F7 /0 with the
                // immediate trailing (SYSINIT2).
                emit_override(a);
                emit_byte(a.size == 1 ? 0xF6 : 0xF7);
                emit_modrm_mem(0, a);
                if (a.size == 1) {
                    emit_byte(static_cast<std::uint8_t>(b.value));
                } else {
                    emit_word(static_cast<std::uint16_t>(b.value));
                }
                // A late external alias was untyped in pass 1, so MASM
                // reserved the word TEST form. When pass 2 learns that the
                // field is a byte it keeps the original length with a NOP
                // (KEYBI9's `TEST CS:SD.TABLE_OK,1`).
                if (a.forward_ref && !a.size_explicit && a.size == 1) {
                    emit_byte(0x90);
                }
                return;
            }
            if ((a.kind == Operand::r16 || a.kind == Operand::r8) && b.kind == Operand::imm) {
                if (a.reg == 0) {
                    emit_byte(a.kind == Operand::r16 ? 0xA9 : 0xA8);
                } else {
                    emit_byte(a.kind == Operand::r16 ? 0xF7 : 0xF6);
                    emit_modrm_reg(0, a.reg);
                }
                if (a.kind == Operand::r16) emit_word(static_cast<std::uint16_t>(b.value));
                else emit_byte(static_cast<std::uint8_t>(b.value));
                // Pass 1 sized a forward constant as memory (4 bytes);
                // the shrunken accumulator form pads with NOP(s)
                // (SYSINIT2's `test ax,FWD` = A9 xx xx 90).
                if (b.forward_const && a.reg == 0) {
                    emit_byte(0x90);
                    if (a.kind == Operand::r8) emit_byte(0x90);
                }
                return;
            }
            if (a.kind == Operand::mem && b.kind == Operand::imm) {
                emit_override(a);
                const int size = a.size != 0 ? a.size : 2;
                emit_byte(size == 1 ? 0xF6 : 0xF7);
                emit_modrm_mem(0, a);
                if (size == 1) emit_byte(static_cast<std::uint8_t>(b.value));
                else emit_word(static_cast<std::uint16_t>(b.value));
                return;
            }
            return fail("unsupported TEST form");
        }
        {
            static const std::map<std::string, int> group_f7{{"NOT", 2}, {"NEG", 3}, {"MUL", 4},
                                                             {"IMUL", 5}, {"DIV", 6}, {"IDIV", 7}};
            if (const auto entry = group_f7.find(op); entry != group_f7.end()) {
                if (op == "IMUL" && operands.size() == 3 &&
                    operands[0].kind == Operand::r16 && operands[2].kind == Operand::imm) {
                    Operand& dst = operands[0];
                    Operand& src = operands[1];
                    Operand& imm = operands[2];
                    if (src.kind == Operand::lab) src = label_as_memory(src);
                    const bool wide = dst.wide;
                    const bool fits = wide ? fits_signed_byte_wide(imm.value)
                                           : fits_signed_byte(imm.value);
                    if (src.kind == Operand::mem) emit_override(src);
                    emit_opsize(wide);
                    emit_byte(fits ? 0x6B : 0x69);
                    if (src.kind == Operand::mem) emit_modrm_mem(dst.reg, src);
                    else emit_modrm_reg(dst.reg, src.reg);
                    if (fits) emit_byte(static_cast<std::uint8_t>(imm.value));
                    else if (wide) emit_dword(static_cast<std::uint32_t>(imm.value));
                    else emit_word(static_cast<std::uint16_t>(imm.value));
                    return;
                }
                if (op == "IMUL" && operands.size() == 2 && operands[0].kind == Operand::r16) {
                    Operand& dst = operands[0];
                    Operand& src = operands[1];
                    if (src.kind == Operand::lab) src = label_as_memory(src);
                    if (src.kind == Operand::mem) emit_override(src);
                    emit_opsize(dst.wide);
                    emit_byte(0x0F);
                    emit_byte(0xAF);
                    if (src.kind == Operand::mem) emit_modrm_mem(dst.reg, src);
                    else emit_modrm_reg(dst.reg, src.reg);
                    return;
                }
                if (operands.size() != 1) return fail(op + " needs one operand");
                Operand& target = operands[0];
                if (target.kind == Operand::lab) target = label_as_memory(target);
                if (target.kind == Operand::r16) {
                    emit_byte(0xF7);
                    emit_modrm_reg(entry->second, target.reg);
                    return;
                }
                if (target.kind == Operand::r8) {
                    emit_byte(0xF6);
                    emit_modrm_reg(entry->second, target.reg);
                    return;
                }
                if (target.kind == Operand::mem) {
                    emit_override(target);
                    emit_byte(target.size == 1 ? 0xF6 : 0xF7);
                    emit_modrm_mem(entry->second, target);
                    return;
                }
                return fail("unsupported " + op + " form");
            }
        }
        if (op == "XCHG") {
            if (operands.size() != 2) return fail("XCHG needs two operands");
            Operand& a = operands[0];
            Operand& b = operands[1];
            if (a.kind == Operand::lab) a = label_as_memory(a);
            if (b.kind == Operand::lab) b = label_as_memory(b);
            if (a.kind == Operand::r16 && b.kind == Operand::r16 && (a.reg == 0 || b.reg == 0)) {
                emit_byte(static_cast<std::uint8_t>(0x90 + (a.reg == 0 ? b.reg : a.reg)));
                return;
            }
            if ((a.kind == Operand::r16 || a.kind == Operand::r8) && b.kind == a.kind) {
                emit_byte(a.kind == Operand::r16 ? 0x87 : 0x86);
                emit_modrm_reg(a.reg, b.reg);
                return;
            }
            const Operand& reg = a.kind == Operand::mem ? b : a;
            const Operand& mem = a.kind == Operand::mem ? a : b;
            if (mem.kind == Operand::mem &&
                (reg.kind == Operand::r16 || reg.kind == Operand::r8)) {
                emit_override(mem);
                emit_byte(reg.kind == Operand::r16 ? 0x87 : 0x86);
                emit_modrm_mem(reg.reg, mem);
                return;
            }
            return fail("unsupported XCHG form");
        }
        {
            static const std::map<std::string, int> shifts{{"ROL", 0}, {"ROR", 1}, {"RCL", 2},
                                                           {"RCR", 3}, {"SHL", 4}, {"SAL", 4},
                                                           {"SHR", 5}, {"SAR", 7}};
            if (const auto entry = shifts.find(op); entry != shifts.end()) {
                if (operands.size() != 2) return fail(op + " needs two operands");
                Operand& target = operands[0];
                Operand& count = operands[1];
                if (target.kind == Operand::lab) target = label_as_memory(target);
                const bool by_cl = count.kind == Operand::r8 && count.reg == 1;
                if (!by_cl && count.kind == Operand::imm && count.value != 1 &&
                    cpu_186_) {
                    // 186+ shift by immediate (MSHARD's SHR BX,4 = C1 EB 04).
                    const bool wide = target.size != 1;
                    if (target.kind == Operand::r16 || target.kind == Operand::r8) {
                        emit_opsize(target.wide);
                        emit_byte(target.kind == Operand::r16 ? 0xC1 : 0xC0);
                        emit_modrm_reg(entry->second, target.reg);
                    } else {
                        emit_override(target);
                        emit_opsize(target.wide);
                        emit_byte(wide ? 0xC1 : 0xC0);
                        emit_modrm_mem(entry->second, target);
                    }
                    emit_byte(static_cast<std::uint8_t>(count.value));
                    return;
                }
                if (!by_cl && !(count.kind == Operand::imm && count.value == 1)) {
                    return fail("unsupported " + op + " count");
                }
                const bool word = target.size != 1;
                const std::uint8_t opcode =
                    by_cl ? (word ? 0xD3 : 0xD2) : (word ? 0xD1 : 0xD0);
                if (target.kind == Operand::r16 || target.kind == Operand::r8) {
                    emit_opsize(target.wide);
                    emit_byte(by_cl ? (target.kind == Operand::r16 ? 0xD3 : 0xD2)
                                    : (target.kind == Operand::r16 ? 0xD1 : 0xD0));
                    emit_modrm_reg(entry->second, target.reg);
                    return;
                }
                if (target.kind == Operand::mem) {
                    emit_override(target);
                    emit_opsize(target.wide);
                    emit_byte(opcode);
                    emit_modrm_mem(entry->second, target);
                    return;
                }
                return fail("unsupported " + op + " form");
            }
        }
        if (op == "IN" || op == "OUT") {
            if (operands.size() != 2) return fail(op + " needs two operands");
            const Operand& accumulator = op == "IN" ? operands[0] : operands[1];
            const Operand& port = op == "IN" ? operands[1] : operands[0];
            const bool word = accumulator.kind == Operand::r16;
            if (port.kind == Operand::imm) {
                emit_byte(static_cast<std::uint8_t>((op == "IN" ? 0xE4 : 0xE6) | (word ? 1 : 0)));
                emit_byte(static_cast<std::uint8_t>(port.value));
                return;
            }
            if (port.kind == Operand::r16 && port.reg == 2) {  // DX
                emit_byte(static_cast<std::uint8_t>((op == "IN" ? 0xEC : 0xEE) | (word ? 1 : 0)));
                return;
            }
            return fail("unsupported " + op + " form");
        }
        // 286/386 protected-mode system instructions (MEMM/XMAEM). All but
        // ARPL ride the 0F escape; the single-operand forms take a /digit in
        // the reg field over an r/m16 (register-direct or memory).
        if (op == "CLTS") {  // 0F 06, no operand
            emit_byte(0x0F);
            emit_byte(0x06);
            return;
        }
        {
            static const std::map<std::string, std::pair<std::uint8_t, int>> sys_rm{
                {"SLDT", {0x00, 0}}, {"STR", {0x00, 1}},  {"LLDT", {0x00, 2}},
                {"LTR", {0x00, 3}},  {"VERR", {0x00, 4}}, {"VERW", {0x00, 5}},
                {"SGDT", {0x01, 0}}, {"SIDT", {0x01, 1}}, {"LGDT", {0x01, 2}},
                {"LIDT", {0x01, 3}}, {"SMSW", {0x01, 4}}, {"LMSW", {0x01, 6}}};
            if (const auto it = sys_rm.find(op);
                it != sys_rm.end() && operands.size() == 1) {
                const std::uint8_t opcode2 = it->second.first;
                const int digit = it->second.second;
                Operand& target = operands[0];
                // A bare label is a memory operand here too. Converting it
                // before emitting the prefix lets the ASSUMEs select CS when
                // DS is NOTHING (SYSLABEL.OBJ; RAMDRIVE's EMM_GDT).
                if (target.kind == Operand::lab) target = label_as_memory(target);
                if (target.kind == Operand::mem) emit_override(target);
                emit_byte(0x0F);
                emit_byte(opcode2);
                if (target.kind == Operand::r16) {
                    emit_modrm_reg(digit, target.reg);
                } else {
                    emit_modrm_mem(digit, target);
                }
                return;
            }
        }
        if ((op == "LAR" || op == "LSL") && operands.size() == 2 &&
            operands[0].kind == Operand::r16) {
            // reg16, r/m16: the destination rides the reg field (0F 02 / 0F 03).
            if (operands[1].kind == Operand::mem) emit_override(operands[1]);
            emit_byte(0x0F);
            emit_byte(op == "LAR" ? 0x02 : 0x03);
            if (operands[1].kind == Operand::r16) {
                emit_modrm_reg(operands[0].reg, operands[1].reg);
            } else {
                emit_modrm_mem(operands[0].reg, operands[1]);
            }
            return;
        }
        if (op == "ARPL" && operands.size() == 2 && operands[1].kind == Operand::r16) {
            // r/m16, reg16: the source rides the reg field (63 /r, no 0F).
            if (operands[0].kind == Operand::mem) emit_override(operands[0]);
            emit_byte(0x63);
            if (operands[0].kind == Operand::r16) {
                emit_modrm_reg(operands[1].reg, operands[0].reg);
            } else {
                emit_modrm_mem(operands[1].reg, operands[0]);
            }
            return;
        }
        // Pass 1 tolerates an opcode that a not-yet-collected macro will
        // supply (DCOPYP invokes HEADER four lines before defining it);
        // pass 2 resolves it through the persisted macro table or fails.
        if (pass_ == 1) return;
        fail("unsupported instruction " + op);
    }

    // --------------------------------------------------------------- OMF
    // MASM 5.10 emits LNAMES, EXTDEF, and PUBDEF names in its name table's
    // iteration order: buckets ascending by sum of the uppercased bytes mod
    // 451, chains front-inserted so later definitions come first within a
    // bucket. Fitted against all 155 reference objects (146 exact-order
    // cases; the 118 EXTDEF records leave 451 as the only modulus).
    static unsigned name_bucket(const std::string& name) {
        unsigned sum = 0;
        for (char c : upper(name)) sum += static_cast<unsigned char>(c);
        return sum % 451;
    }

    static void table_order(std::vector<std::string>& names) {
        std::vector<std::pair<std::string, std::size_t>> entries;
        for (std::size_t i = 0; i < names.size(); ++i) entries.push_back({names[i], i});
        std::stable_sort(entries.begin(), entries.end(),
                         [](const auto& a, const auto& b) {
                             const unsigned ba = name_bucket(a.first);
                             const unsigned bb = name_bucket(b.first);
                             if (ba != bb) return ba < bb;
                             return a.second > b.second;  // front-inserted chains
                         });
        names.clear();
        for (auto& [name, index] : entries) names.push_back(name);
    }

    static void push_index(std::vector<std::uint8_t>& body, std::size_t index) {
        // OMF index encoding: one byte below 0x80, else 0x80|hi then lo
        // (MSTABLE's extern 130 rides as 80 82).
        if (index < 0x80) {
            body.push_back(static_cast<std::uint8_t>(index));
        } else {
            body.push_back(static_cast<std::uint8_t>(0x80 | (index >> 8)));
            body.push_back(static_cast<std::uint8_t>(index & 0xFF));
        }
    }

    OmfRecord make(std::uint8_t type, std::vector<std::uint8_t> body) {
        OmfRecord record;
        record.type = type;
        record.body = std::move(body);
        record.declared_length = static_cast<std::uint16_t>(record.body.size() + 1);
        unsigned sum = record.type;
        sum += record.declared_length & 0xFF;
        sum += record.declared_length >> 8;
        for (std::uint8_t byte : record.body) sum += byte;
        record.checksum = static_cast<std::uint8_t>((256 - (sum & 0xFF)) & 0xFF);
        return record;
    }

    std::vector<std::uint8_t> emit() {
        std::vector<OmfRecord> records;
        // THEADR: the command line's spelling of the source
        {
            std::vector<std::uint8_t> body;
            body.push_back(static_cast<std::uint8_t>(source_spelling_.size()));
            body.insert(body.end(), source_spelling_.begin(), source_spelling_.end());
            records.push_back(make(0x80, std::move(body)));
        }
        // LNAMES: "" first, then every segment and class name in the name
        // table's iteration order.
        std::vector<std::string> named;
        for (const std::string& name : name_insertions_) {
            if (!name.empty() &&
                std::find(named.begin(), named.end(), name) == named.end()) {
                named.push_back(name);
            }
        }
        table_order(named);
        std::vector<std::string> lnames{""};
        lnames.insert(lnames.end(), named.begin(), named.end());
        const auto lname_index = [&lnames](const std::string& name) -> std::size_t {
            for (std::size_t i = 0; i < lnames.size(); ++i) {
                if (lnames[i] == name) return i + 1;
            }
            return 0;
        };
        struct SegmentNames { std::size_t name, cls; };
        std::vector<SegmentNames> segment_names;
        for (const Segment& segment : segments_) {
            segment_names.push_back({lname_index(segment.name), lname_index(segment.cls)});
        }
        // .ALPHA: SEGDEFs emit sorted by segment name, and every segment
        // index in the module (LEDATA/LIDATA headers, FIXUPP frames and
        // targets, GRPDEF members, PUBDEF runs, MODEND) renumbers to match
        // (S_DISPLY declares DATA before CODE_FAR, its OBJ lists CODE_FAR
        // first). The data-record STREAM keeps emission order -- DATA's
        // bytes still precede CODE_FAR's, only the indices swap -- and
        // LNAMES keeps the name-table hash order either way.
        std::vector<std::size_t> seg_order(segments_.size());
        for (std::size_t i = 0; i < seg_order.size(); ++i) seg_order[i] = i;
        if (alpha_order_) {
            std::stable_sort(seg_order.begin(), seg_order.end(),
                             [&](std::size_t a, std::size_t b) {
                                 return segments_[a].name < segments_[b].name;
                             });
        }
        std::vector<std::size_t> seg_renumber(segments_.size() + 1, 0);
        for (std::size_t pos = 0; pos < seg_order.size(); ++pos) {
            seg_renumber[seg_order[pos] + 1] = pos + 1;
        }
        const auto mapseg = [&seg_renumber](std::size_t index) {
            return index == 0 ? std::size_t{0} : seg_renumber[index];
        };
        {
            std::vector<std::uint8_t> body;
            for (const std::string& name : lnames) {
                body.push_back(static_cast<std::uint8_t>(name.size()));
                body.insert(body.end(), name.begin(), name.end());
            }
            records.push_back(make(0x96, std::move(body)));
        }
        // SEGDEF per segment, in emission (declaration or .ALPHA) order
        for (std::size_t pos = 0; pos < seg_order.size(); ++pos) {
            const std::size_t i = seg_order[pos];
            const Segment& segment = segments_[i];
            std::vector<std::uint8_t> body;
            // An absolute segment defaults to USE32 when .386 was active at its
            // declaration (MEMM's `ABS0 segment at 0000h` -- acbp 01), but an
            // explicit USE16 keeps it 16-bit (OEMPROC's `DIAGSEG segment use16
            // at 0`), as does a 16-bit assembly (ATTRIBA's `psp segment at 0`).
            const bool use32 = segment_uses_32(segment);
            const std::uint8_t acbp =
                segment.absolute
                    ? static_cast<std::uint8_t>(use32 ? 0x01 : 0x00)
                    : static_cast<std::uint8_t>((segment.align << 5) |
                                                (segment.combine << 2));
            body.push_back(acbp);
            if (segment.absolute) {
                // Align 0: a frame number and an offset byte precede the
                // length (ATTRIBA's PSP at frame 0).
                body.push_back(static_cast<std::uint8_t>(segment.frame & 0xFF));
                body.push_back(static_cast<std::uint8_t>(segment.frame >> 8));
                body.push_back(0x00);
            }
            body.push_back(static_cast<std::uint8_t>(segment.extent & 0xFF));
            body.push_back(static_cast<std::uint8_t>(segment.extent >> 8));
            push_index(body, segment_names[i].name);
            push_index(body, segment_names[i].cls);
            push_index(body, 1);  // overlay ""
            records.push_back(make(0x98, std::move(body)));
        }
        // GRPDEF per group: name index, then FF + SEGDEF index per member,
        // in statement order (measured off SIGHAND.OBJ)
        for (const Group& group : groups_) {
            std::vector<std::uint8_t> body;
            push_index(body, lname_index(group.name));
            for (const std::string& member : group.members) {
                int segment_index = 0;
                for (std::size_t s = 0; s < segments_.size(); ++s) {
                    if (segments_[s].name == member) {
                        segment_index = static_cast<int>(s) + 1;
                        break;
                    }
                }
                if (segment_index == 0) {
                    fail("GROUP member is not a segment: " + member);
                    continue;
                }
                body.push_back(0xFF);
                push_index(body, mapseg(static_cast<std::size_t>(segment_index)));
            }
            records.push_back(make(0x9A, std::move(body)));
        }
        // EXTDEF, in EXTRN declaration order (type byte always 0). Only
        // pass-1 declarations join the up-front block; pass-2 lazies
        // stream between data records at their declaration points.
        {
            std::vector<std::uint8_t> body;
            for (std::size_t e = 0; e < externs_.size(); ++e) {
                if (e < extern_sequences_.size() && extern_sequences_[e] != 0) {
                    continue;
                }
                const std::string& name = externs_[e];
                // Record-capped: MASM fills an EXTDEF body up to 1016 bytes
                // (measured max across the corpus; SELECT/GEN_COMS flushes a
                // 1005-byte body because its next 10-char extern would reach
                // 1017) before spilling to a new record.
                if (body.size() + name.size() + 2 > 1016) {
                    records.push_back(make(0x8C, std::move(body)));
                    body = {};
                }
                body.push_back(static_cast<std::uint8_t>(name.size()));
                body.insert(body.end(), name.begin(), name.end());
                push_index(body, 0);
            }
            if (!body.empty()) records.push_back(make(0x8C, std::move(body)));
        }
        // PUBDEF: names in name-table order, one record per consecutive
        // run of publics sharing a segment (KBDGSTAT.OBJ carries two).
        std::vector<std::string> ordered_publics = publics_;
        // Within a bucket the chain ranks by the name's FIRST sight
        // (definition or PUBLIC statement, whichever the assembler met
        // first), front-inserted so later-seen names lead.
        {
            std::vector<std::pair<std::string, std::uint64_t>> entries;
            for (std::size_t i = 0; i < ordered_publics.size(); ++i) {
                const auto seen = name_first_seen_.find(upper(ordered_publics[i]));
                const std::uint64_t rank =
                    seen != name_first_seen_.end()
                        ? seen->second
                        : (1ull << 32) + i;
                entries.push_back({ordered_publics[i], rank});
            }
            std::stable_sort(entries.begin(), entries.end(),
                             [](const auto& a, const auto& b) {
                                 const unsigned ba = name_bucket(a.first);
                                 const unsigned bb = name_bucket(b.first);
                                 if (ba != bb) return ba < bb;
                                 return a.second > b.second;
                             });
            ordered_publics.clear();
            for (auto& [name, rank] : entries) ordered_publics.push_back(name);
        }
        std::size_t at = 0;
        while (at < ordered_publics.size()) {
            const auto first = symbols_.find(upper(ordered_publics[at]));
            if (first == symbols_.end()) {
                fail("undefined PUBLIC " + ordered_publics[at]);
                ++at;
                continue;
            }
            const int run_segment = first->second.segment;
            // The record's group index was fixed when each label was
            // defined (the CS-ASSUMEd group); a run breaks when either
            // the segment or the group changes (ATTRIBA's vector/xcexit).
            const int run_group = first->second.pub_group;
            // A public CONSTANT (DISKCOMP's `FINE EQU -1`) takes the
            // absolute form: segment index 0 with an explicit frame word,
            // the value riding as the offset.
            const bool run_absolute = first->second.kind == Symbol::constant;
            std::vector<std::uint8_t> body;
            push_index(body, static_cast<std::size_t>(run_group));
            if (run_absolute) {
                push_index(body, 0);
                body.push_back(0x00);
                body.push_back(0x00);
            } else {
                push_index(body,
                           run_segment >= 0
                               ? mapseg(static_cast<std::size_t>(run_segment) + 1)
                               : 1);
            }
            while (at < ordered_publics.size()) {
                const std::string& name = ordered_publics[at];
                const auto symbol = symbols_.find(upper(name));
                if (symbol == symbols_.end() || symbol->second.segment != run_segment ||
                    symbol->second.pub_group != run_group ||
                    (symbol->second.kind == Symbol::constant) != run_absolute) break;
                const std::string spelling =
                    options_.preserve_public_case ? name : upper(name);
                // PUBDEF is record-capped like the rest, but tighter: a
                // public that would push the BODY past 1015 starts a
                // fresh record with the same group/segment header.
                // Bounds measured: DEBCONST breaks at body 1007 + 9
                // (so cap <= 1015); TSPC's second record packs to body
                // 1013 (so cap >= 1013).
                if (body.size() + spelling.size() + 4 > 1015) break;
                body.push_back(static_cast<std::uint8_t>(spelling.size()));
                body.insert(body.end(), spelling.begin(), spelling.end());
                body.push_back(static_cast<std::uint8_t>(symbol->second.value & 0xFF));
                body.push_back(static_cast<std::uint8_t>((symbol->second.value >> 8) & 0xFF));
                push_index(body, 0);  // type
                ++at;
            }
            records.push_back(make(0x90, std::move(body)));
        }
        // Link-pass separator (COMENT class A2). Data 00 when the module
        // names an entry point OR streams pass-2 EXTDEFs after the
        // separator (all 107 zero-byte corpus OBJs are exactly those two
        // sets); 01 otherwise -- LINK may finish its pass 1 early only
        // when no symbol records follow.
        const bool late_extdefs = std::any_of(
            extern_sequences_.begin(), extern_sequences_.end(),
            [](std::uint32_t sequence) { return sequence != 0; });
        records.push_back(
            make(0x88, {0x00, 0xA2,
                        static_cast<std::uint8_t>(
                            end_start_label_.empty() && !late_extdefs ? 0x01
                                                                      : 0x00)}));
        // LEDATA per literal run, LIDATA per iterated block, streamed in
        // emission (source) order across segments as MASM writes them.
        // A lazy EXTDEF (sequence-stamped) interleaves into the same
        // stream between the runs it separates.
        struct StreamEntry {
            std::uint32_t sequence;
            std::size_t segment;              // for runs
            const Segment::Item* run;         // null for an EXTDEF entry
            std::size_t extern_index;         // for EXTDEFs
        };
        std::vector<StreamEntry> stream;
        for (std::size_t i = 0; i < segments_.size(); ++i) {
            for (const Segment::Item& run : segments_[i].runs) {
                stream.push_back({run.sequence, i, &run, 0});
            }
        }
        for (std::size_t e = 0; e < extern_sequences_.size(); ++e) {
            if (extern_sequences_[e] != 0) {
                stream.push_back({extern_sequences_[e], 0, nullptr, e});
            }
        }
        std::stable_sort(stream.begin(), stream.end(),
                         [](const StreamEntry& a, const StreamEntry& b) {
                             return a.sequence < b.sequence;
                         });
        const auto append_fixup = [&](const Segment::Fixup& fixup,
                                      std::vector<std::uint8_t>& fixupp,
                                      long data_offset) {
            const std::uint8_t locat0 = static_cast<std::uint8_t>(
                0x80 | (fixup.self_relative ? 0 : 0x40) | (fixup.loc << 2) |
                ((data_offset >> 8) & 3));
            fixupp.push_back(locat0);
            fixupp.push_back(static_cast<std::uint8_t>(data_offset & 0xFF));
            // Internal fixups zero the data word; the target
            // offset rides as an explicit displacement unless it
            // is zero, where the P=1 short form saves two bytes
            // (FLUSHBUF vs SETINT24).
            const bool p0 = fixup.explicit_disp > 0;
            if (fixup.target_group > 0) {
                // frame F_GRP, target T_GRP (SIGHAND's DGROUP)
                fixupp.push_back(0x15);
                push_index(fixupp, static_cast<std::size_t>(fixup.target_group));
                push_index(fixupp, static_cast<std::size_t>(fixup.target_group));
                return;
            }
            if (fixup.frame_group > 0 && fixup.target_extern > 0) {
                // frame F_GRP, target T_EXT: an external reached
                // through a group-ASSUMEd register (DEBERR's
                // DG-framed DRVLET/RESTART).
                fixupp.push_back(p0 ? 0x12 : 0x16);
                push_index(fixupp, static_cast<std::size_t>(fixup.frame_group));
                push_index(fixupp, static_cast<std::size_t>(fixup.target_extern));
                if (p0) {
                    fixupp.push_back(static_cast<std::uint8_t>(fixup.explicit_disp & 0xFF));
                    fixupp.push_back(static_cast<std::uint8_t>((fixup.explicit_disp >> 8) & 0xFF));
                }
                return;
            }
            if (fixup.frame_group > 0) {
                // frame F_GRP, target T_SEG (OFFSET DOSGROUP:x)
                fixupp.push_back(p0 ? 0x10 : 0x14);
                push_index(fixupp, static_cast<std::size_t>(fixup.frame_group));
                push_index(fixupp, mapseg(static_cast<std::size_t>(fixup.target_segment)));
                if (p0) {
                    fixupp.push_back(static_cast<std::uint8_t>(fixup.explicit_disp & 0xFF));
                    fixupp.push_back(static_cast<std::uint8_t>((fixup.explicit_disp >> 8) & 0xFF));
                }
                return;
            }
            if (fixup.target_extern > 0) {
                if (fixup.frame_segment > 0) {
                    // frame F_SEG of the declaring segment,
                    // target T_EXT (F_CLOSE.OBJ)
                    fixupp.push_back(p0 ? 0x02 : 0x06);
                    push_index(fixupp, mapseg(static_cast<std::size_t>(fixup.frame_segment)));
                } else {
                    // no declaring segment: frame F_EXT of the
                    // external itself (QCURDSK's far call)
                    fixupp.push_back(p0 ? 0x22 : 0x26);
                    push_index(fixupp, static_cast<std::size_t>(fixup.target_extern));
                }
                push_index(fixupp, static_cast<std::size_t>(fixup.target_extern));
            } else {
                // frame F_SEG + index (a pending frame can
                // name another segment: MSLOAD's BOOTSEG
                // frame on a CSEG target), target T_SEG
                fixupp.push_back(p0 ? 0x00 : 0x04);
                push_index(fixupp,
                           mapseg(static_cast<std::size_t>(
                               fixup.frame_segment > 0
                                   ? fixup.frame_segment
                                   : fixup.target_segment)));
                push_index(fixupp, mapseg(static_cast<std::size_t>(fixup.target_segment)));
            }
            if (p0) {
                fixupp.push_back(static_cast<std::uint8_t>(fixup.explicit_disp & 0xFF));
                fixupp.push_back(static_cast<std::uint8_t>((fixup.explicit_disp >> 8) & 0xFF));
            }
        };
        const auto fixup_index_extensions = [&](const Segment::Fixup& fixup) {
            std::size_t extra = 0;
            const auto count = [&](std::size_t index) {
                if (index >= 0x80) ++extra;
            };
            if (fixup.target_group > 0) {
                count(static_cast<std::size_t>(fixup.target_group));
                count(static_cast<std::size_t>(fixup.target_group));
            } else if (fixup.frame_group > 0 && fixup.target_extern > 0) {
                count(static_cast<std::size_t>(fixup.frame_group));
                count(static_cast<std::size_t>(fixup.target_extern));
            } else if (fixup.frame_group > 0) {
                count(static_cast<std::size_t>(fixup.frame_group));
                count(mapseg(static_cast<std::size_t>(fixup.target_segment)));
            } else if (fixup.target_extern > 0) {
                if (fixup.frame_segment > 0) {
                    count(mapseg(static_cast<std::size_t>(fixup.frame_segment)));
                } else {
                    count(static_cast<std::size_t>(fixup.target_extern));
                }
                count(static_cast<std::size_t>(fixup.target_extern));
            } else {
                count(mapseg(static_cast<std::size_t>(
                    fixup.frame_segment > 0 ? fixup.frame_segment
                                            : fixup.target_segment)));
                count(mapseg(static_cast<std::size_t>(fixup.target_segment)));
            }
            return extra;
        };
        for (const StreamEntry& entry : stream) {
            const std::size_t i = entry.segment;
            if (entry.run == nullptr) {
                const std::string& name = externs_[entry.extern_index];
                std::vector<std::uint8_t> body;
                body.push_back(static_cast<std::uint8_t>(name.size()));
                body.insert(body.end(), name.begin(), name.end());
                push_index(body, 0);
                records.push_back(make(0x8C, std::move(body)));
                continue;
            }
            {
                const Segment::Item& run = *entry.run;
                const bool use32 = segment_uses_32(segments_[i]);
                if (!run.lidata.empty()) {
                    std::vector<std::uint8_t> body;
                    push_index(body, mapseg(i + 1));
                    body.push_back(static_cast<std::uint8_t>(run.start & 0xFF));
                    body.push_back(static_cast<std::uint8_t>(run.start >> 8));
                    if (use32) {
                        body.push_back(0x00);
                        body.push_back(0x00);
                        std::size_t source = 0;
                        const std::function<void()> widen_block = [&] {
                            const std::uint16_t repeat =
                                static_cast<std::uint16_t>(run.lidata[source]) |
                                (static_cast<std::uint16_t>(run.lidata[source + 1])
                                 << 8);
                            source += 2;
                            body.push_back(static_cast<std::uint8_t>(repeat & 0xFF));
                            body.push_back(static_cast<std::uint8_t>(repeat >> 8));
                            body.push_back(0x00);
                            body.push_back(0x00);
                            const std::uint16_t blocks =
                                static_cast<std::uint16_t>(run.lidata[source]) |
                                (static_cast<std::uint16_t>(run.lidata[source + 1])
                                 << 8);
                            source += 2;
                            body.push_back(static_cast<std::uint8_t>(blocks & 0xFF));
                            body.push_back(static_cast<std::uint8_t>(blocks >> 8));
                            if (blocks == 0) {
                                const std::size_t length = run.lidata[source++];
                                body.push_back(static_cast<std::uint8_t>(length));
                                body.insert(body.end(),
                                            run.lidata.begin() +
                                                static_cast<long>(source),
                                            run.lidata.begin() +
                                                static_cast<long>(source + length));
                                source += length;
                                return;
                            }
                            for (std::uint16_t block = 0; block < blocks; ++block) {
                                widen_block();
                            }
                        };
                        while (source < run.lidata.size()) widen_block();
                    } else {
                        body.insert(body.end(), run.lidata.begin(),
                                    run.lidata.end());
                    }
                    records.push_back(make(use32 ? 0xA3 : 0xA2,
                                           std::move(body)));
                    if (!run.lidata_fixups.empty()) {
                        // The paired FIXUPP's data-record offset counts
                        // into the LIDATA's block image (COMPSM's 0013).
                        std::vector<std::uint8_t> fixupp;
                        for (const Segment::Fixup& fixup : run.lidata_fixups) {
                            append_fixup(fixup, fixupp, fixup.location);
                        }
                        records.push_back(make(0x9C, std::move(fixupp)));
                    }
                    continue;
                }
                if (run.repeat != 0) {
                    std::vector<std::uint8_t> body;
                    push_index(body, mapseg(i + 1));
                    body.push_back(static_cast<std::uint8_t>(run.start & 0xFF));
                    body.push_back(static_cast<std::uint8_t>(run.start >> 8));
                    if (use32) {
                        body.push_back(0x00);
                        body.push_back(0x00);
                    }
                    body.push_back(static_cast<std::uint8_t>(run.repeat & 0xFF));
                    body.push_back(static_cast<std::uint8_t>((run.repeat >> 8) & 0xFF));
                    if (use32) {
                        body.push_back(static_cast<std::uint8_t>(
                            (run.repeat >> 16) & 0xFF));
                        body.push_back(static_cast<std::uint8_t>(
                            (run.repeat >> 24) & 0xFF));
                    }
                    body.push_back(0x01);  // one block:
                    body.push_back(0x00);
                    body.push_back(0x01);  //   1 x literal
                    body.push_back(0x00);
                    if (use32) {
                        body.push_back(0x00);
                        body.push_back(0x00);
                    }
                    body.push_back(0x00);
                    body.push_back(0x00);
                    body.push_back(static_cast<std::uint8_t>(run.bytes.size()));
                    body.insert(body.end(), run.bytes.begin(), run.bytes.end());
                    records.push_back(make(use32 ? 0xA3 : 0xA2,
                                           std::move(body)));
                    if (!run.lidata_fixups.empty()) {
                        std::vector<std::uint8_t> fixupp;
                        for (const Segment::Fixup& fixup : run.lidata_fixups) {
                            append_fixup(fixup, fixupp, fixup.location);
                        }
                        records.push_back(make(0x9C, std::move(fixupp)));
                    }
                    continue;
                }
                std::size_t offset = 0;
                while (offset < run.bytes.size()) {
                    // MASM caps the whole LEDATA record at 1024 bytes, so the
                // data payload tops out at 1020 (GETMSG's 0x3fc split). A
                // FIXUPP patches within one LEDATA, so a patched word that
                // would straddle the cap slides the flush earlier (REPLACE
                // _MSGRET's 1019-byte first record).
                std::size_t chunk = std::min<std::size_t>(1020, run.bytes.size() - offset);
                if (offset + chunk < run.bytes.size()) {
                    const std::size_t edge = offset + chunk;
                    for (const auto& atom : run.atoms) {
                        if (atom.first < edge && atom.second > edge &&
                            atom.first > offset) {
                            chunk = atom.first - offset;
                            break;
                        }
                        if (atom.first >= edge) break;
                    }
                }

                    // The paired FIXUPP is capped like every record;
                    // when it would burst 1024 bytes the LEDATA flushes
                    // early (EDLIN's 871-byte second CODE record pairs a
                    // 1018-byte FIXUPP).
                    const auto build_fixupp = [&](std::size_t at_offset,
                                                  std::size_t at_chunk,
                                                  long* overflow_at) {
                        std::vector<std::uint8_t> fixupp;
                        *overflow_at = -1;
                        for (const Segment::Fixup& fixup : segments_[i].fixups) {
                            if (fixup.sequence != run.sequence) continue;
                            const long data_offset =
                                fixup.location - (run.start + at_offset);
                            if (data_offset < 0 ||
                                data_offset >= static_cast<long>(at_chunk)) {
                                continue;
                            }
                            const std::size_t before = fixupp.size();
                            append_fixup(fixup, fixupp, data_offset);
                            // Cap the FIXUPP body at 1020 so the whole
                            // record normally fits MASM's buffer. Its preflight
                            // counts every OMF index as one byte, then the
                            // encoder may extend an index >= 128 and produce a
                            // 1021-byte body (FIXCAPX; SELECT3's first FIXUPP).
                            const std::size_t encoded = fixupp.size() - before;
                            const std::size_t estimated =
                                encoded - fixup_index_extensions(fixup);
                            if (before + estimated > 1020) {
                                fixupp.resize(before);
                                *overflow_at = fixup.location;
                                return fixupp;
                            }
                        }
                        return fixupp;
                    };
                    std::vector<std::uint8_t> fixupp;
                    for (;;) {
                        long overflow_at = -1;
                        fixupp = build_fixupp(offset, chunk, &overflow_at);
                        if (overflow_at < 0) break;
                        chunk = static_cast<std::size_t>(overflow_at) -
                                (run.start + offset);
                    }
                    std::vector<std::uint8_t> body;
                    push_index(body, mapseg(i + 1));
                    const std::size_t where = run.start + offset;
                    body.push_back(static_cast<std::uint8_t>(where & 0xFF));
                    body.push_back(static_cast<std::uint8_t>(where >> 8));
                    body.insert(body.end(), run.bytes.begin() + static_cast<long>(offset),
                                run.bytes.begin() + static_cast<long>(offset + chunk));
                    records.push_back(make(0xA0, std::move(body)));
                    if (!fixupp.empty()) records.push_back(make(0x9C, std::move(fixupp)));
                    offset += chunk;
                }
            }
        }
        // MODEND: `module_type` 0xC1 (main + start address) when END named
        // an entry label, else 0x00. The start address is a segment-framed
        // self-relative reference: End Data 0x00 (frame method F_SEG,
        // target method T_SEG), the segment index for frame and target,
        // then the label's offset as the displacement (COMP1's START).
        std::vector<std::uint8_t> modend{0x00};
        // Under .386 a start address rides the 32-bit MODEND (8B) with a 4-byte
        // target offset; a 16-bit module (or one with no start) keeps 8A
        // (MEMMONF's `end` with no entry stays 8A).
        std::uint8_t modend_type = 0x8A;
        if (!end_start_label_.empty()) {
            const auto symbol = symbols_.find(end_start_label_);
            if (symbol != symbols_.end() && symbol->second.kind == Symbol::label &&
                symbol->second.segment >= 0) {
                if (cpu_386_) modend_type = 0x8B;
                const std::size_t seg =
                    static_cast<std::size_t>(symbol->second.segment) + 1;
                // The start frame is the GROUP holding the entry segment
                // when one does (DEBUASM's END under DG), else the segment.
                int group = 0;
                const std::string& seg_name =
                    segments_[static_cast<std::size_t>(symbol->second.segment)].name;
                for (std::size_t g = 0; g < groups_.size(); ++g) {
                    if (std::find(groups_[g].members.begin(),
                                  groups_[g].members.end(),
                                  seg_name) != groups_[g].members.end()) {
                        group = static_cast<int>(g) + 1;
                        break;
                    }
                }
                if (group > 0) {
                    modend = {0xC1, 0x10};  // F_GRP
                    push_index(modend, static_cast<std::size_t>(group));
                } else {
                    modend = {0xC1, 0x00};  // F_SEG
                    push_index(modend, mapseg(seg));
                }
                push_index(modend, mapseg(seg));   // target: T_SEG index
                const std::uint32_t offset =
                    static_cast<std::uint32_t>(symbol->second.value);
                modend.push_back(static_cast<std::uint8_t>(offset & 0xFF));
                modend.push_back(static_cast<std::uint8_t>((offset >> 8) & 0xFF));
                if (modend_type == 0x8B) {
                    modend.push_back(static_cast<std::uint8_t>((offset >> 16) & 0xFF));
                    modend.push_back(static_cast<std::uint8_t>((offset >> 24) & 0xFF));
                }
            }
        }
        records.push_back(make(modend_type, std::move(modend)));
        return serialize_omf_records(records);
    }

    // ------------------------------------------------------------- state
    std::string source_spelling_;
    MasmOptions options_;
    std::vector<std::string> root_lines_;
    std::vector<Frame> frames_;
    int pass_ = 1;
    // Inside a data directive: its extern references frame differently
    // from instruction operands (TDATA vs CHKDISK).
    bool data_use_ = false;
    bool cpu_186_ = false;
    bool cpu_386_ = false;
    bool alpha_order_ = false;  // .ALPHA: SEGDEFs sort by name at emission
    bool atom_open_ = false;
    int atom_segment_ = -1;
    std::uint16_t atom_location_ = 0;

    // Global record-emission clock: orders data records across segments the
    // way MASM streams them (source order), and marks a run stale once a
    // different segment emitted after it.
    std::uint32_t emit_sequence_ = 0;
    bool ended_ = false;
    std::string end_start_label_;  // entry label from `END label`, if any
    std::set<std::string> forward_labels_;  // relocated by a forward JMP/CALL
    std::set<std::string> defined_this_pass_;  // labels defined in this pass flow
    std::vector<std::string> errors_;
    std::vector<std::string> console_;

    std::map<std::string, Symbol> symbols_;
    std::map<std::string, Struc> strucs_;
    std::map<std::string, Macro> macros_;
    std::vector<Segment> segments_;
    std::vector<int> segments_order_;
    std::vector<std::string> publics_;
    // First-sight sequence per (uppercased) name: definitions, EQUates,
    // and PUBLIC statements stamp on their pass-1 encounter; the PUBDEF
    // chain order ranks by it (see note_name).
    std::map<std::string, std::uint32_t> name_first_seen_;
    std::uint32_t name_seen_counter_ = 0;
    std::vector<std::string> externs_;
    // Emission stamp per extern: 0 = declared by pass 1 (the up-front
    // EXTDEF block); nonzero = declared lazily during pass 2 (DOSMAC's
    // `invoke` IF2 path), streaming an EXTDEF at that point.
    std::vector<std::uint32_t> extern_sequences_;
    std::vector<std::string> name_insertions_;  // LNAMES candidates, in order
    struct Group {
        std::string name;                  // uppercased
        std::vector<std::string> members;  // uppercased segment names
    };
    std::vector<Group> groups_;
    std::vector<bool> conditionals_;
    bool collecting_macro_ = false;
    int macro_depth_ = 0;
    bool collecting_block_ = false;
    int block_depth_ = 0;
    std::string block_kind_;
    std::string block_parameter_;
    std::vector<std::string> block_items_;
    std::vector<std::string> block_body_;
    Macro pending_macro_;
    std::string pending_macro_name_;
    std::string open_struc_;
    std::uint16_t struc_offset_ = 0;
    int current_segment_ = -1;
    std::vector<int> segment_stack_;
    bool proc_far_ = false;
    std::vector<bool> proc_far_stack_;
    int assumes_[4] = {-1, -1, -1, -1};  // ES CS SS DS -> segment index
    int assume_group_[4] = {0, 0, 0, 0};  // ES CS SS DS -> 1-based GRPDEF
    int sticky_assumes_[4] = {-1, -1, -1, -1};  // last non-NOTHING segment
    char comment_delimiter_ = '\0';
    int pending_frame_group_ = 0;
    int pending_frame_segment_ = 0;
    unsigned local_counter_ = 0;
    std::map<std::string, std::string> text_equates_;
    // `X EQU <name>` where name is not yet declared freezes X as a constant;
    // if `name` is later EXTRN'd, X must re-bind to that external (KEYB's
    // `SD EQU SHARED_DATA` precedes `EXTRN SHARED_DATA:BYTE`). Keyed by the
    // undeclared RHS name; each entry is (alias key, alias spelling).
    std::map<std::string, std::vector<std::pair<std::string, std::string>>>
        deferred_alias_;
    struct Record {
        int unit = 1;
        int total = 0;
        struct Field {
            std::string name;
            int width = 0;
            int shift = 0;
            long preset = 0;
        };
        std::vector<Field> fields;
    };
    std::map<std::string, Record> records_;
    std::map<std::string, std::pair<int, int>> record_fields_;  // width, shift
    std::string current_line_;
};

}  // namespace

MasmResult masm_assemble(const std::string& source_spelling, const MasmOptions& options) {
    return Assembler(source_spelling, options).run();
}

}  // namespace dosbase::devtools
