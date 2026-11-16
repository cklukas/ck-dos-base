// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include "dosbase/devtools/cl_compiler.hpp"
#include "dosbase/devtools/cl_preproc.hpp"
#include "dosbase/devtools/link_image.hpp"
#include "dosbase/devtools/omf_module.hpp"
#include "dosbase/devtools/omf_record.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using dosbase::devtools::CompileOptions;
using dosbase::devtools::MemoryModel;
using dosbase::devtools::OmfNameTable;
using dosbase::devtools::OmfRecord;
using dosbase::devtools::OmfType;
using dosbase::devtools::PreprocOptions;
using dosbase::devtools::compile_c_to_omf;
using dosbase::devtools::dump_omf_record;
using dosbase::devtools::parse_omf_records;
using dosbase::devtools::preprocess_c_source;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("dosbase-cl-" + std::to_string(stamp) + "-" +
                 std::to_string(sequence.fetch_add(1)));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void write_text_file(const fs::path& path, const std::string& content) {
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string read_text_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string dump_all_omf(const std::vector<OmfRecord>& records) {
    OmfNameTable table;
    std::string out;
    for (const auto& rec : records) {
        dump_omf_record(rec, table, "", false, out);
    }
    return out;
}

bool has_record_type(const std::vector<OmfRecord>& records, OmfType type) {
    const auto raw = static_cast<std::uint8_t>(type);
    return std::any_of(records.begin(), records.end(),
                       [raw](const OmfRecord& r) { return r.type == raw; });
}

}  // namespace

DOSBASE_TEST(cl_preproc_handles_includes_macros_and_conditionals) {
    TemporaryDirectory temp;
    write_text_file(temp.path() / "INC" / "DEFS.H",
                    "#define ADD(a, b) ((a) + (b))\n"
                    "#define STR(x) #x\n"
                    "#define CAT(a, b) a##b\n");

    const fs::path src_path = temp.path() / "TEST.C";
    const std::string src =
        "#include \"INC/DEFS.H\"\n"
        "#define FLAG 2\n"
        "#if defined(FLAG) && (FLAG == 2)\n"
        "int CAT(my_, var) = ADD(10, 20);\n"
        "char *msg = STR(hello);\n"
        "#elif FLAG == 1\n"
        "int bad_var = 1;\n"
        "#else\n"
        "int bad_var = 2;\n"
        "#endif\n";

    PreprocOptions opts;
    opts.include_dirs.push_back(temp.path());
    const std::string pp = preprocess_c_source(src, src_path, opts);

    DOSBASE_TEST_EXPECT(pp.find("int my_var = ((10) + (20));") != std::string::npos);
    DOSBASE_TEST_EXPECT(pp.find("char *msg = \"hello\";") != std::string::npos);
    DOSBASE_TEST_EXPECT(pp.find("bad_var") == std::string::npos);
}

DOSBASE_TEST(cl_compiler_emits_valid_omf_records_and_segments) {
    const std::string src =
        "extern int ext_fn(int x);\n"
        "int g_init = 42;\n"
        "int g_bss;\n"
        "const char g_str[] = \"DOS\";\n"
        "int add_one(int a) {\n"
        "    return ext_fn(a) + g_init;\n"
        "}\n";

    CompileOptions opts;
    opts.module_name = "SAMPLE";
    const auto obj = compile_c_to_omf(src, opts);
    const auto records = parse_omf_records(obj, 0, true);

    DOSBASE_TEST_EXPECT(!records.empty());
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::theadr));
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::lnames));
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::segdef));
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::grpdef));
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::extdef));
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::pubdef));
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::ledata));
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::fixupp));
    DOSBASE_TEST_EXPECT(has_record_type(records, OmfType::modend));

    const std::string dump = dump_all_omf(records);
    DOSBASE_TEST_EXPECT(dump.find("_TEXT") != std::string::npos);
    DOSBASE_TEST_EXPECT(dump.find("_DATA") != std::string::npos);
    DOSBASE_TEST_EXPECT(dump.find("DGROUP") != std::string::npos);
    DOSBASE_TEST_EXPECT(dump.find("_add_one") != std::string::npos);
    DOSBASE_TEST_EXPECT(dump.find("_ext_fn") != std::string::npos);
}

DOSBASE_TEST(cl_compiler_supports_struct_packing_and_kr_and_ms_extensions) {
    const std::string src =
        "struct S {\n"
        "    char a;\n"
        "    int b;\n"
        "};\n"
        "int sz = sizeof(struct S);\n"
        "int pascal PASCAL_FN(int x, int y) { return x + y; }\n"
        "void interrupt INT_FN(void) {}\n"
        "int kr_fn(a, b)\n"
        "int a;\n"
        "int b;\n"
        "{\n"
        "    return a - b;\n"
        "}\n"
        "char far *fp;\n";

    CompileOptions packed_opts;
    packed_opts.pack_structs = true;
    const auto packed_obj = compile_c_to_omf(src, packed_opts);
    const auto packed_recs = parse_omf_records(packed_obj, 0, true);
    const std::string packed_dump = dump_all_omf(packed_recs);
    DOSBASE_TEST_EXPECT(packed_dump.find("PASCAL_FN") != std::string::npos);
    DOSBASE_TEST_EXPECT(packed_dump.find("_kr_fn") != std::string::npos);

    CompileOptions large_opts;
    large_opts.model = MemoryModel::large_model;
    large_opts.module_name = "LARGE_MOD";
    const auto large_obj = compile_c_to_omf(src, large_opts);
    const auto large_recs = parse_omf_records(large_obj, 0, true);
    const std::string large_dump = dump_all_omf(large_recs);
    DOSBASE_TEST_EXPECT(large_dump.find("LARGE_MOD_TEXT") != std::string::npos);
}

DOSBASE_TEST(cl_compiles_real_msdos4_c_sources) {
    const fs::path dos_root = fs::path(DOSBASE_SOURCE_DIR) / "third_party" / "ms-dos-4" / "src";
    if (!fs::exists(dos_root)) {
        return;
    }

    const fs::path flush13 = dos_root / "DEV" / "SMARTDRV" / "FLUSH13.C";
    if (fs::exists(flush13)) {
        PreprocOptions pp_opts;
        pp_opts.include_dirs.push_back(dos_root / "DEV" / "SMARTDRV");
        pp_opts.include_dirs.push_back(dos_root / "INC");
        pp_opts.include_dirs.push_back(dos_root / "TOOLS" / "BLD" / "INC");
        pp_opts.defines["MSDOS"] = "1";
        pp_opts.defines["M_I86SM"] = "1";
        const std::string pp = preprocess_c_source(read_text_file(flush13), flush13, pp_opts);
        CompileOptions cc_opts;
        cc_opts.model = MemoryModel::small_model;
        cc_opts.pack_structs = true;
        cc_opts.module_name = "FLUSH13";
        const auto obj = compile_c_to_omf(pp, cc_opts);
        const auto recs = parse_omf_records(obj, 0, true);
        DOSBASE_TEST_EXPECT(has_record_type(recs, OmfType::theadr));
        DOSBASE_TEST_EXPECT(has_record_type(recs, OmfType::modend));
    }

    const fs::path ntoi = dos_root / "CMD" / "FC" / "NTOI.C";
    if (fs::exists(ntoi)) {
        PreprocOptions pp_opts;
        pp_opts.include_dirs.push_back(dos_root / "CMD" / "FC");
        pp_opts.include_dirs.push_back(dos_root / "INC");
        pp_opts.include_dirs.push_back(dos_root / "TOOLS" / "BLD" / "INC");
        pp_opts.defines["MSDOS"] = "1";
        pp_opts.defines["M_I86SM"] = "1";
        const std::string pp = preprocess_c_source(read_text_file(ntoi), ntoi, pp_opts);
        CompileOptions cc_opts;
        cc_opts.model = MemoryModel::small_model;
        cc_opts.module_name = "NTOI";
        const auto obj = compile_c_to_omf(pp, cc_opts);
        const auto recs = parse_omf_records(obj, 0, true);
        DOSBASE_TEST_EXPECT(has_record_type(recs, OmfType::theadr));
        DOSBASE_TEST_EXPECT(has_record_type(recs, OmfType::modend));
    }
}

#ifdef DOSBASE_TOOL_DIR
DOSBASE_TEST(cl_and_link_end_to_end_c_program) {
    TemporaryDirectory temp;
    const fs::path c_file = temp.path() / "PROG.C";
    const fs::path obj_file = temp.path() / "PROG.OBJ";
    const fs::path exe_file = temp.path() / "PROG.EXE";

    write_text_file(
        c_file,
        "int helper(int x) { return x + 1; }\n"
        "void main(void) { int y = helper(41); }\n");

    const fs::path tool_dir(DOSBASE_TOOL_DIR);
    DOSBASE_TEST_EXPECT_EQ(
        dosbase::bootstrap_test::run_tool(
            tool_dir, "ckcl", {"-c", "-AS", "-Zl", "-Fo" + obj_file.string(), c_file.string()}, temp.path()),
        0);
    DOSBASE_TEST_EXPECT(fs::is_regular_file(obj_file));

    DOSBASE_TEST_EXPECT_EQ(
        dosbase::bootstrap_test::run_tool(tool_dir, "cklink", {"PROG.OBJ,PROG.EXE;"}, temp.path()),
        0);
    DOSBASE_TEST_EXPECT(fs::is_regular_file(exe_file));
}
#endif

namespace {
std::vector<std::uint8_t> extract_code_bytes(const std::vector<OmfRecord>& recs) {
    std::vector<std::uint8_t> code;
    for (const OmfRecord& r : recs) {
        if ((r.type & 0xFEU) == static_cast<std::uint8_t>(OmfType::ledata) &&
            r.body.size() >= 3 && r.body[0] == 1) {
            code.insert(code.end(), r.body.begin() + 3, r.body.end());
        }
    }
    return code;
}

bool contains_seq(const std::vector<std::uint8_t>& hay, const std::vector<std::uint8_t>& needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}
}  // namespace

DOSBASE_TEST(cl_wp10_const_folding_and_strength_reduction) {
    // 1. Constant folding reduces complex constant expressions to `mov ax, 50` (B8 32 00)
    {
        const char* src =
            "int folded(void) {\n"
            "    return ((1 << 4) + (200 / 5) - (0 ? 99 : 6));\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xB8, 0x32, 0x00}));
        DOSBASE_TEST_EXPECT(code.size() <= 8U);
    }

    // 2. Array indexing & strength reduction emit shifts/masks with zero MUL/DIV opcodes
    {
        const char* src =
            "int idx_and_math(int *arr, int i, unsigned int u, unsigned long ul) {\n"
            "    int a = arr[0] + arr[3] + arr[i] + (2 + arr)[1];\n"
            "    int b = (i * 8) + (i * 10) + (int)(u / 16u) + (int)(u % 512u);\n"
            "    unsigned long c = (ul * 16UL) + (ul / 512UL) + (ul % 512UL);\n"
            "    return a + b + (int)c;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // shl bx, 1 (D1 E3) or shl cx, 1 (D1 E1) is used for int* indexing
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xD1, 0xE3}) || contains_seq(code, {0xD1, 0xE1}));
        // Zero mul cx (F7 E1), imul cx (F7 E9), div cx (F7 F1), idiv cx (F7 F9), or mul bx (F7 E3)
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xF7, 0xE1}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xF7, 0xE9}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xF7, 0xF1}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xF7, 0xF9}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xF7, 0xE3}));
        // Zero 32-iteration software division loop (`mov cx, 32` = B9 20 00)
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xB9, 0x20, 0x00}));
    }
}

DOSBASE_TEST(cl_wp11_inplace_memory_ops_and_discarded_values) {
    // 1. 16-bit local/param ++, --, += emit direct memory instructions with zero push/pop/lea
    {
        const char* src =
            "void step_16(volatile int x, int *p) {\n"
            "    x++;\n"
            "    --x;\n"
            "    x += 5;\n"
            "    p++;\n"
            "    (void)x;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // inc word ptr [bp+4] (FF 46 04), dec word ptr [bp+4] (FF 4E 04), add word ptr [bp+4], 5 (83 46 04 05)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xFF, 0x46, 0x04}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xFF, 0x4E, 0x04}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x83, 0x46, 0x04, 0x05}));
        // p++ on int* adds 2 in place: add word ptr [bp+6], 2 (83 46 06 02)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x83, 0x46, 0x06, 0x02}));
        // Zero push ax (0x50), pop ax (0x58), or lea bx (0x8D)
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x58}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x8D}));
    }

    // 2. 32-bit long ++ and += in statement context emit add/adc [bp+d] in place
    {
        const char* src =
            "void step_32(unsigned long n) {\n"
            "    n++;\n"
            "    n += 16UL;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // add word ptr [bp+4], 1 (83 46 04 01) ; adc word ptr [bp+6], 0 (83 56 06 00)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x83, 0x46, 0x04, 0x01, 0x83, 0x56, 0x06, 0x00}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x52}));
    }

    // 3. Global 16-bit += and 32-bit ++ emit direct memory [sym] / [sym+2] operations
    {
        const char* src =
            "int g16 = 1;\n"
            "unsigned long g32 = 2UL;\n"
            "void step_globals(void) {\n"
            "    g16++;\n"
            "    g16 += 7;\n"
            "    g32++;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // inc word ptr [0] (FF 06 00 00), add word ptr [0], 7 (83 06 00 00 07)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xFF, 0x06, 0x00, 0x00}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x83, 0x06, 0x00, 0x00, 0x07}));
        // g32 at _DATA+2: add word ptr [2], 1 (83 06 02 00 01); adc word ptr [4], 0 (83 16 04 00 00)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x83, 0x06, 0x02, 0x00, 0x01, 0x83, 0x16, 0x04, 0x00, 0x00}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xBB}));
    }
}

DOSBASE_TEST(cl_wp12_direct_mem_and_folded_addressing) {
    // 1. Direct memory RHS in 16-bit binary ALU and comparisons (zero push/pop)
    {
        const char* src =
            "int direct_rhs(int a, int b, int c) {\n"
            "    if (a < b) {\n"
            "        return ((a + b) - c) ^ b;\n"
            "    }\n"
            "    return a & c;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // cmp ax, [bp+6] (3B 46 06), add ax, [bp+6] (03 46 06), sub ax, [bp+8] (2B 46 08), and ax, [bp+8] (23 46 08)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x3B, 0x46, 0x06}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x03, 0x46, 0x06}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x2B, 0x46, 0x08}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x23, 0x46, 0x08}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));
    }

    // 2. Folded [bp+disp] on local struct fields (zero lea bx / add bx)
    {
        const char* src =
            "struct Pair { int a; int b; };\n"
            "int local_struct(int x, int y) {\n"
            "    struct Pair s;\n"
            "    s.a = x;\n"
            "    s.b = y;\n"
            "    s.b++;\n"
            "    return s.a + s.b;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x8D}));         // no lea bx
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x81, 0xC3}));   // no add bx, imm16
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));         // no push ax
    }

    // 3. [bx+disp8] addressing for p->field and p[const_idx] (zero add bx / push ax)
    {
        const char* src =
            "struct Pair { int a; int b; };\n"
            "int ptr_disp(volatile struct Pair *p, int *arr, int v) {\n"
            "    p->b = v;\n"
            "    return p->b + arr[3];\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // mov bx, [bp+4] (8B 5E 04) ; mov [bx+2], ax (89 47 02)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8B, 0x5E, 0x04, 0x89, 0x47, 0x02}));
        // mov bx, [bp+6] (8B 5E 06) ; mov ax, [bx+6] (8B 47 06)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8B, 0x5E, 0x06, 0x8B, 0x47, 0x06}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x81, 0xC3}));
    }

    // 4. Fast 6-byte [bp+d] load/store for 32-bit long and far pointer locals
    {
        const char* src =
            "long copy_long(long x) {\n"
            "    long tmp;\n"
            "    tmp = x;\n"
            "    return tmp;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // mov ax, [bp+4] (8B 46 04) ; mov dx, [bp+6] (8B 56 06) ; mov [bp-4], ax (89 46 FC) ; mov [bp-2], dx (89 56 FE)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8B, 0x46, 0x04, 0x8B, 0x56, 0x06, 0x89, 0x46, 0xFC, 0x89, 0x56, 0xFE}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x8D}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));
    }
}

DOSBASE_TEST(cl_wp13_short_circuit_branches_and_jump_relaxation) {
    // 1. Short-circuit && / || / ! and rel8 relaxation: zero mov ax,1 and zero E9 long jumps
    {
        const char* src =
            "int short_branches(int a, int b, int c) {\n"
            "    if (!(a < b && c != 0)) {\n"
            "        a += 3;\n"
            "    } else {\n"
            "        a -= 3;\n"
            "    }\n"
            "    return a;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // Zero boolean materialization (mov ax, 1 = B8 01 00) and zero 16-bit jmp (E9)
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xB8, 0x01, 0x00}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0xE9}));
        // Uses 2-byte jmp rel8 (0xEB) across else branch, and final return jmp +0 is eliminated
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xEB}));
        // Epilogue ends with pop bp (0x5D), ret (0xC3) immediately after mov ax, [bp+4] (8B 46 04)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8B, 0x46, 0x04, 0x5D, 0xC3}));
    }

    // 2. Long >128-byte branch preserves rel16 jump while adjusting subsequent OMF fixups accurately
    {
        std::string src =
            "int g_sink = 0;\n"
            "int g_val = 42;\n"
            "int callee(int v) { return v + 1; }\n"
            "int mixed_jumps(int x) {\n"
            "    if (x == 0) {\n";
        for (int i = 0; i < 32; ++i) {
            src += "        g_sink += 5;\n";
        }
        src +=
            "    }\n"
            "    if (x == 1) {\n"
            "        goto done;\n"
            "    }\n"
            "    switch (x) {\n"
            "        case 2: g_sink = g_val; break;\n"
            "        default: g_sink = callee(x); break;\n"
            "    }\n"
            "done:\n"
            "    return g_sink;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto recs = parse_omf_records(obj, 0, true);
        const auto code = extract_code_bytes(recs);
        // Long branch over >128B loop of g_sink += 5 (if x == 0 -> jump if != 0 = 0x75, inverted = 0x74)
        // keeps 5-byte je +3; jmp rel16 (74 03 E9 ...)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x74, 0x03, 0xE9}));
        // Self-relative near call to callee(0) at offset 0 of _TEXT must point back to offset 0!
        // Find 0xE8 (call rel16) and verify target == 0
        bool found_valid_call = false;
        for (std::size_t i = 0; i + 2 < code.size(); ++i) {
            if (code[i] == 0xE8) {
                const auto rel = static_cast<std::int16_t>(
                    code[i + 1] | (static_cast<std::uint16_t>(code[i + 2]) << 8));
                const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(i + 3) + rel;
                if (target == 0) {
                    found_valid_call = true;
                    break;
                }
            }
        }
        DOSBASE_TEST_EXPECT(found_valid_call);
    }
}

DOSBASE_TEST(cl_wp14_si_di_register_allocation) {
    // 1. Hot pointer + loop counter allocate to SI/DI, using [si]/[si+disp], inc si/di, push/pop si/di
    {
        const char* src =
            "struct Item { int tag; int val; };\n"
            "int sum_items(struct Item *p, int n) {\n"
            "    int sum = 0;\n"
            "    while (n > 0) {\n"
            "        sum += p->val;\n"
            "        p++;\n"
            "        n--;\n"
            "    }\n"
            "    return sum;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // Saves/restores SI (0x56 / 0x5E) and DI (0x57 / 0x5F)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x56, 0x57}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x5F, 0x5E}));
        // Uses [si+2] (8B 44 02) or [di+2] (8B 45 02) directly without copying pointer to BX
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8B, 0x44, 0x02}) ||
                            contains_seq(code, {0x8B, 0x45, 0x02}));
        // Zero BX load `mov bx, [bp+4]` (8B 5E 04)
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x8B, 0x5E, 0x04}));
    }

    // 2. Address-taken variable (&x) is never allocated to SI/DI, while register keyword var is
    {
        const char* src =
            "void sink_ptr(int *p);\n"
            "int addr_safety(void) {\n"
            "    int taken = 1;\n"
            "    register int fast = 0;\n"
            "    sink_ptr(&taken);\n"
            "    fast++;\n"
            "    fast += taken;\n"
            "    return fast;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // `fast` is in SI: xor si, si (33 F6) and inc si (0x46)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x33, 0xF6}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x46}));
        // `taken` stays on stack: lea ax, [bp-2] (8D 46 FE)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8D, 0x46, 0xFE}));
    }

    // 3. MS C cast-as-lvalue on register-allocated pointer variable (((int)sptr) = ..., ((char*)sptr)++)
    {
        const char* src =
            "int cast_lval_reg(int start, int limit) {\n"
            "    char *sptr;\n"
            "    int n = 0;\n"
            "    for (((int)sptr) = start; ((int)sptr) < limit && *sptr == ' '; ((char *)sptr)++) {\n"
            "        n++;\n"
            "    }\n"
            "    return n;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        DOSBASE_TEST_EXPECT(!code.empty());
    }
}

DOSBASE_TEST(cl_wp15_struct_value_abi) {
    // 1. Odd-sized packed struct assignment (7 bytes = 3 words + 1 byte) with live SI/DI register vars
    {
        const char* src =
            "struct Odd7 { int a; int b; int c; char d; };\n"
            "int copy_odd7(struct Odd7 *dst, struct Odd7 *src) {\n"
            "    register int acc = 0;\n"
            "    register int cnt = 4;\n"
            "    while (cnt > 0) {\n"
            "        acc += cnt;\n"
            "        cnt--;\n"
            "    }\n"
            "    *dst = *src;\n"
            "    return acc;\n"
            "}\n";
        CompileOptions opts;
        opts.pack_structs = true;
        const auto obj = compile_c_to_omf(src, opts);
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // Preserves SI/DI around block copy: push si; push di (56 57) and pop di; pop si (5F 5E)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x56, 0x57}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x5F, 0x5E}));
        // cld (FC), mov cx, 3 (B9 03 00), rep movsw (F3 A5), movsb (A4)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xFC, 0xB9, 0x03, 0x00, 0xF3, 0xA5, 0xA4}));
    }

    // 2. Struct pass-by-value & return-by-value (<= 4B in DX:AX and > 4B via hidden [bp+4] sret pointer)
    {
        const char* src =
            "struct Pair { int x; int y; };\n"
            "struct Big  { int a; int b; int c; int d; };\n"
            "struct Pair make_pair(int x, int y) {\n"
            "    struct Pair p;\n"
            "    p.x = x; p.y = y;\n"
            "    return p;\n"
            "}\n"
            "struct Big make_big(int base) {\n"
            "    struct Big b;\n"
            "    b.a = base; b.b = base + 1; b.c = base + 2; b.d = base + 3;\n"
            "    return b;\n"
            "}\n"
            "int sum_big(struct Big v, int extra) {\n"
            "    return v.a + v.b + v.c + v.d + extra;\n"
            "}\n"
            "void main(void) {\n"
            "    struct Pair p = make_pair(3, 4);\n"
            "    struct Big b = make_big(10);\n"
            "    int total = sum_big(b, p.x + p.y);\n"
            "    (void)total;\n"
            "}\n";
        CompileOptions opts;
        opts.omit_default_lib = true;
        const auto obj = compile_c_to_omf(src, opts);
        const auto records = parse_omf_records(obj, 0, true);
        const auto code = extract_code_bytes(records);
        // Passing 8-byte struct Big by value reserves 8 stack bytes: sub sp, 8 (83 EC 08)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x83, 0xEC, 0x08}));
        // Returning >4B struct Big loads hidden sret pointer from [bp+4]: mov ax, [bp+4] (8B 46 04)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8B, 0x46, 0x04}));

        const auto mod = dosbase::devtools::parse_module(records);
        const auto linked = dosbase::devtools::link_modules({mod});
        DOSBASE_TEST_EXPECT(linked.errors.empty());
        DOSBASE_TEST_EXPECT(!linked.exe.empty());
    }
}

DOSBASE_TEST(cl_block_scope_frame_compaction_and_zero_init_reuse) {
    const char* src =
        "int sibling_scopes(void) {\n"
        "    volatile int z1 = 0;\n"
        "    volatile int z2 = 0;\n"
        "    int acc = 0;\n"
        "    { volatile int s1 = 10; acc += s1; }\n"
        "    { volatile int s2 = 20; acc += s2; }\n"
        "    { volatile int s3 = 30; acc += s3; }\n"
        "    return acc + z1 + z2;\n"
        "}\n";
    const auto obj = compile_c_to_omf(src, CompileOptions{});
    const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
    // z1 (-2), z2 (-4), acc (promoted to SI -> -4), and s1/s2/s3 share the same 2-byte slot (-8):
    // frame is <= 8 bytes (83 EC 06 or 83 EC 08), never 12 bytes (83 EC 0C)!
    DOSBASE_TEST_EXPECT(!contains_seq(code, {0x83, 0xEC, 0x0C}));
    // z1 = 0 and z2 = 0 share a single xor ax, ax followed by two mov [bp-d], ax stores:
    // 33 C0 89 46 FE 89 46 FC
    DOSBASE_TEST_EXPECT(contains_seq(code, {0x33, 0xC0, 0x89, 0x46, 0xFE, 0x89, 0x46, 0xFC}));
}

DOSBASE_TEST(cl_base_index_addressing_and_imm_stores) {
    // 1. Local byte array indexed by SI emits 4-byte `mov byte ptr [bp+si-128], 0x20` (C6 42 80 20)
    {
        const char* src =
            "void fill_dbcs(void) {\n"
            "    char darray[128];\n"
            "    int i;\n"
            "    for (i = 0; i < 128; i++) {\n"
            "        darray[i] = ' ';\n"
            "    }\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xC6, 0x42, 0x80, 0x20}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));  // zero push ax!
    }

    // 2. Word pointer array store `placements[m] = val` scales BX without push/pop AX
    {
        const char* src =
            "void set_slot(int **placements, volatile int m, int *val) {\n"
            "    placements[m] = val;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // shl bx, 1 (D1 E3); add bx, [bp+4] (03 5E 04)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xD1, 0xE3, 0x03, 0x5E, 0x04}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));
    }
}

DOSBASE_TEST(cl_far_ptr_les_and_inplace_arith) {
    const char* src =
        "int scan_dbcs(unsigned char far *ptr) {\n"
        "    int count = 0;\n"
        "    for (; *(unsigned int far *)ptr != 0; ptr += 2) {\n"
        "        count += ptr[0] + ptr[1];\n"
        "    }\n"
        "    return count;\n"
        "}\n";
    const auto obj = compile_c_to_omf(src, CompileOptions{});
    const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
    // Uses 3-byte `les bx, [bp+4]` (C4 5E 04) instead of 10-byte mov ax/dx + mov es, dx
    DOSBASE_TEST_EXPECT(contains_seq(code, {0xC4, 0x5E, 0x04}));
    DOSBASE_TEST_EXPECT(!contains_seq(code, {0x8E, 0xC2}));  // zero `mov es, dx`
    // Uses 4-byte `add word ptr [bp+4], 2` (83 46 04 02) for `ptr += 2`
    DOSBASE_TEST_EXPECT(contains_seq(code, {0x83, 0x46, 0x04, 0x02}));
}

DOSBASE_TEST(cl_direct_byte_and_mem_comparisons) {
    const char* src =
        "int count_digits(const char *s, char target) {\n"
        "    int n = 0;\n"
        "    while (*s) {\n"
        "        if (*s >= '0' && *s <= '9') n++;\n"
        "        if (*s == target) n += 10;\n"
        "        s++;\n"
        "    }\n"
        "    return n;\n"
        "}\n";
    const auto obj = compile_c_to_omf(src, CompileOptions{});
    const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
    // `while (*s)` emits 3-byte `cmp byte ptr [si], 0` (80 3C 00) without loading AL or cbw!
    DOSBASE_TEST_EXPECT(contains_seq(code, {0x80, 0x3C, 0x00}));
    // `*s >= '0'` emits 3-byte `cmp byte ptr [si], 0x30` (80 3C 30)
    DOSBASE_TEST_EXPECT(contains_seq(code, {0x80, 0x3C, 0x30}));
    // `*s == target` emits 8-bit `cmp al, [bp+6]` (3A 46 06) or `cmp [si], al` (38 04) without push ax
    DOSBASE_TEST_EXPECT(contains_seq(code, {0x3A, 0x46, 0x06}) || contains_seq(code, {0x38, 0x04}));
    DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));  // zero push ax!
}

DOSBASE_TEST(cl_two_tier_frame_and_per_block_reg_reuse) {
    // 1. Two-tier layout places scalar `hot` at [bp-2] (disp8) even when declared after char bigbuf[256]
    {
        const char* src =
            "int tier_order(void) {\n"
            "    char bigbuf[256];\n"
            "    volatile int hot = 7;\n"
            "    bigbuf[0] = 1;\n"
            "    return hot + bigbuf[0];\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // `hot = 7` uses disp8 [bp-2]: mov word ptr [bp-2], 7 (C7 46 FE 07 00)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xC7, 0x46, 0xFE, 0x07, 0x00}));
    }

    // 2. Disjoint sibling blocks both promote their hot pointers to DI while outer `sum` uses SI
    {
        const char* src =
            "int two_loops(int *a, int na, int *b, int nb) {\n"
            "    int sum = 0;\n"
            "    {\n"
            "        int *p1 = a;\n"
            "        int *e1 = a + na;\n"
            "        while (p1 < e1) { sum += *p1; p1++; }\n"
            "    }\n"
            "    {\n"
            "        int *p2 = b;\n"
            "        int *e2 = b + nb;\n"
            "        while (p2 < e2) { sum += *p2; p2++; }\n"
            "    }\n"
            "    return sum;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // Count `inc si; inc si` (46 46) or `inc di; inc di` (47 47) — reused across BOTH loop bodies!
        int si_incs = 0;
        int di_incs = 0;
        for (std::size_t i = 0; i + 1 < code.size(); ++i) {
            if (code[i] == 0x46 && code[i + 1] == 0x46) ++si_incs;
            if (code[i] == 0x47 && code[i + 1] == 0x47) ++di_incs;
        }
        DOSBASE_TEST_EXPECT(si_incs >= 2 || di_incs >= 2);
    }
}

DOSBASE_TEST(cl_odd_struct_index_and_bx_reload_cache) {
    // 1. `&segs[i]` on 19-byte -Zp struct folds `&*` and emits `mov ax, 19; mul [bp+6]` with zero push/pop
    {
        const char* src =
            "struct Seg19 { char b[19]; };\n"
            "struct Seg19 *seg_addr(struct Seg19 * volatile segs, volatile int i) {\n"
            "    return &segs[i];\n"
            "}\n";
        CompileOptions opts;
        opts.pack_structs = true;
        const auto obj = compile_c_to_omf(src, opts);
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // mov ax, 19 (B8 13 00); mul word ptr [bp+6] (F7 66 06); add ax, [bp+4] (03 46 04)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xB8, 0x13, 0x00, 0xF7, 0x66, 0x06, 0x03, 0x46, 0x04}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x50}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x8B, 0xD8, 0x8B, 0xC3}));
    }

    // 2. Basic-block BX cache avoids reloading `mov bx, [bp+4]` across consecutive `out->field = imm`
    {
        const char* src =
            "struct Rec { int a; int b; int c; };\n"
            "int fill_rec(struct Rec *out) {\n"
            "    register int r1 = 10;\n"
            "    register int r2 = 20;\n"
            "    r1 += r2;\n"
            "    r2 += r1;\n"
            "    out->a = 1;\n"
            "    out->b = 2;\n"
            "    out->c = 3;\n"
            "    return r1 + r2;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        int bx_loads = 0;
        for (std::size_t i = 0; i + 2 < code.size(); ++i) {
            if (code[i] == 0x8B && code[i + 1] == 0x5E && code[i + 2] == 0x04) ++bx_loads;
        }
        DOSBASE_TEST_EXPECT_EQ(bx_loads, 1);
    }
}

DOSBASE_TEST(cl_32bit_direct_mem_and_shared_helpers) {
    // 1. 32-bit direct memory ALU and comparisons emit zero `push dx; push ax` (52 50)
    {
        const char* src =
            "unsigned long u32_fast(unsigned long a, unsigned long b) {\n"
            "    if (a < b) return a + b;\n"
            "    return a - 100UL;\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        // add ax, [bp+8] (03 46 08); adc dx, [bp+10] (13 56 0A)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x03, 0x46, 0x08, 0x13, 0x56, 0x0A}));
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x52, 0x50}));
    }

    // 2. Multiple 32-bit divisions/modulos share a single `$L_uldiv` loop in `_TEXT`
    {
        const char* src =
            "long div_many(long a, long b, unsigned long ua, unsigned long ub) {\n"
            "    long q1 = a / b;\n"
            "    long r1 = a % b;\n"
            "    unsigned long q2 = ua / ub;\n"
            "    unsigned long r2 = ua % ub;\n"
            "    return q1 + r1 + (long)(q2 + r2);\n"
            "}\n";
        const auto obj = compile_c_to_omf(src, CompileOptions{});
        const auto code = extract_code_bytes(parse_omf_records(obj, 0, true));
        int loop_headers = 0;
        for (std::size_t i = 0; i + 2 < code.size(); ++i) {
            if (code[i] == 0xB9 && code[i + 1] == 0x20 && code[i + 2] == 0x00) ++loop_headers;
        }
        DOSBASE_TEST_EXPECT_EQ(loop_headers, 1);
    }
}

DOSBASE_TEST(cl_medium_model_far_interrupt_and_brace_init) {
    // 1. Medium model: <MOD>_TEXT segment, 4-byte function pointers, frame-based indirect far call,
    //    static far array in <MOD>_FAR_BSS, near-to-far DS widening vs NULL 0000:0000, 32-bit far ==
    {
        const char* src =
            "typedef int (*EmitFn)(void *ctx, const char __far *text, int len);\n"
            "typedef void (__interrupt __far *BiosIsr)(void);\n"
            "static volatile int g_break = 0;\n"
            "static char __far g_source[1024];\n"
            "static const char *const g_lines[] = { \"one\", \"two\" };\n"
            "struct Topic { const char *const *lines; int count; };\n"
            "const struct Topic g_topic = { g_lines, 2 };\n"
            "static void __interrupt __far break_isr(void) {\n"
            "    g_break = 1;\n"
            "}\n"
            "int invoke_emit(EmitFn fn, void *ctx, const char *near_str) {\n"
            "    unsigned char box[] = { 1, 2, 3, 4 };\n"
            "    const char __far *nul = (const char __far *)((void *)0);\n"
            "    BiosIsr isr = break_isr;\n"
            "    g_source[0] = box[2];\n"
            "    if (fn == 0 || isr == 0 || nul != 0) return -1;\n"
            "    return fn(ctx, near_str, (int)sizeof(box)) + g_source[0];\n"
            "}\n";
        CompileOptions opts;
        opts.model = MemoryModel::medium_model;
        opts.module_name = "ckbrun.c";
        const auto obj = compile_c_to_omf(src, opts);
        const auto recs = parse_omf_records(obj, 0, true);
        const auto code = extract_code_bytes(recs);

        // Verify LNAMES contains CKBRUN_TEXT and CKBRUN_FAR_BSS
        const std::string dump = dump_all_omf(recs);
        DOSBASE_TEST_EXPECT(dump.find("CKBRUN_TEXT") != std::string::npos);
        DOSBASE_TEST_EXPECT(dump.find("CKBRUN_FAR_BSS") != std::string::npos);
        DOSBASE_TEST_EXPECT(dump.find("FAR_BSS") != std::string::npos);
        // Static g_lines referenced from g_topic must resolve locally without EXTDEF!
        DOSBASE_TEST_EXPECT(!has_record_type(recs, OmfType::extdef));

        // Interrupt handler loads DS = DGROUP (B8 xx xx 8E D8 FC) and ends with IRET (CF)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8E, 0xD8, 0xFC}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xCF}));

        // Indirect far call uses call far ptr [bp+disp] (FF 5E xx or FF 9E xx xx), NOT ss:[bx] (36 FF 1F)
        DOSBASE_TEST_EXPECT(!contains_seq(code, {0x36, 0xFF, 0x1F}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xFF, 0x5E}) || contains_seq(code, {0xFF, 0x9E}));

        // Passing near_str to const char __far * widens segment via mov dx, ds (8C DA), and medium retf (CB)
        DOSBASE_TEST_EXPECT(contains_seq(code, {0x8C, 0xDA}));
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xCB}));
    }
}
