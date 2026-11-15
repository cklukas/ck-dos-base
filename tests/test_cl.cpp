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
