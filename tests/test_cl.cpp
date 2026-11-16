// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include "dosbase/devtools/cl_compiler.hpp"
#include "dosbase/devtools/cl_preproc.hpp"
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

    const fs::path ckcl = fs::path(DOSBASE_TOOL_DIR) / "ckcl";
    const fs::path cklink = fs::path(DOSBASE_TOOL_DIR) / "cklink";

    const std::string cl_cmd =
        "\"" + ckcl.string() + "\" -c -AS -Zl -Fo\"" + obj_file.string() + "\" \"" + c_file.string() + "\"";
    DOSBASE_TEST_EXPECT_EQ(std::system(cl_cmd.c_str()), 0);
    DOSBASE_TEST_EXPECT(fs::is_regular_file(obj_file));

    const std::string link_cmd =
        "cd \"" + temp.path().string() + "\" && \"" + cklink.string() + "\" PROG.OBJ,PROG.EXE;";
    DOSBASE_TEST_EXPECT_EQ(std::system(link_cmd.c_str()), 0);
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
        // shl cx, 1 (D1 E1) is used for int* indexing
        DOSBASE_TEST_EXPECT(contains_seq(code, {0xD1, 0xE1}));
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
