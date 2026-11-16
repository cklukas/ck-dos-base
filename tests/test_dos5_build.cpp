// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

fs::path select_temp_root() {
    if (const char* env_tmp = std::getenv("TMPDIR"); env_tmp && env_tmp[0] != '\0') {
        fs::path p(env_tmp);
#if defined(__APPLE__)
        const std::string ps = p.string();
        if (ps.rfind("/tmp", 0) == 0 || ps.rfind("/private/tmp", 0) == 0) {
            if (fs::is_directory("/Volumes/S8T")) {
                fs::path s8t("/Volumes/S8T/tmp");
                std::error_code ec;
                fs::create_directories(s8t, ec);
                return s8t;
            }
            if (fs::is_directory("/Volumes/PRO-BLADE")) {
                fs::path pb("/Volumes/PRO-BLADE/tmp");
                std::error_code ec;
                fs::create_directories(pb, ec);
                return pb;
            }
        }
#endif
        return p;
    }
#if defined(__APPLE__)
    if (fs::is_directory("/Volumes/S8T")) {
        fs::path s8t("/Volumes/S8T/tmp");
        std::error_code ec;
        fs::create_directories(s8t, ec);
        return s8t;
    }
#endif
    return fs::temp_directory_path();
}

class ScratchDir {
public:
    ScratchDir() {
        static std::atomic<std::uint64_t> seq{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = select_temp_root() /
                ("dosbase-dos5-test-" + std::to_string(stamp) + "-" + std::to_string(seq.fetch_add(1)));
        fs::create_directories(path_);
    }
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

std::string upper_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

DOSBASE_TEST(dos5_tree_contains_zero_prebuilt_binaries) {
    const fs::path dos5_src = fs::path(DOSBASE_SOURCE_DIR) / "os" / "ck-dos-5" / "src";
    DOSBASE_TEST_EXPECT(fs::is_directory(dos5_src));
    DOSBASE_TEST_EXPECT(fs::is_regular_file(dos5_src / "CMD" / "DOSUI" / "DOSUI.ASM"));
    DOSBASE_TEST_EXPECT(fs::is_regular_file(dos5_src / "CMD" / "EDIT" / "EDIT.ASM"));
    DOSBASE_TEST_EXPECT(fs::is_regular_file(dos5_src / "CMD" / "EDIT" / "EDITHLP.ASM"));
    DOSBASE_TEST_EXPECT(fs::is_regular_file(dos5_src / "CMD" / "HELP" / "HELP.ASM"));
    DOSBASE_TEST_EXPECT(fs::is_regular_file(dos5_src / "CMD" / "HELP" / "HELPDATA.INC"));

    const std::set<std::string> forbidden = {".EXE", ".COM", ".OBJ", ".SYS", ".BIN"};
    for (const auto& entry : fs::recursive_directory_iterator(dos5_src)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = upper_ascii(entry.path().extension().string());
        DOSBASE_TEST_EXPECT(forbidden.count(ext) == 0);
        if (ext == ".LIB") {
            std::ifstream in(entry.path(), std::ios::binary);
            unsigned char magic = 0;
            if (in.read(reinterpret_cast<char*>(&magic), 1)) {
                DOSBASE_TEST_EXPECT(magic != 0xF0u);
            }
        }
    }
}

#ifdef DOSBASE_TOOL_DIR
DOSBASE_TEST(dos5_dosui_edit_and_help_assemble_link_and_convert) {
    const fs::path cmd_root = fs::path(DOSBASE_SOURCE_DIR) / "os" / "ck-dos-5" / "src" / "CMD";
    const fs::path tool_dir(DOSBASE_TOOL_DIR);
    ScratchDir temp;
    const fs::path d = temp.path();

    for (const fs::path& src : {
             cmd_root / "DOSUI" / "DOSUI.INC",
             cmd_root / "DOSUI" / "DOSUI.ASM",
             cmd_root / "EDIT" / "EDIT.ASM",
             cmd_root / "EDIT" / "EDITHLP.ASM",
             cmd_root / "HELP" / "HELP.ASM",
             cmd_root / "HELP" / "HELPDATA.INC"}) {
        fs::copy_file(src, d / src.filename(), fs::copy_options::overwrite_existing);
    }

    for (const char* mod : {"DOSUI", "EDIT", "EDITHLP", "HELP"}) {
        const std::string spec = std::string(mod) + ".ASM," + mod + ".OBJ,NUL,NUL;";
        DOSBASE_TEST_EXPECT_EQ(
            dosbase::bootstrap_test::run_tool(tool_dir, "ckmasm", {"-I.", spec}, d), 0);
    }

    DOSBASE_TEST_EXPECT_EQ(
        dosbase::bootstrap_test::run_tool(
            tool_dir, "cklink", {"EDIT.OBJ+DOSUI.OBJ+EDITHLP.OBJ,EDIT.EXE,NUL,;"}, d),
        0);
    DOSBASE_TEST_EXPECT_EQ(
        dosbase::bootstrap_test::run_tool(tool_dir, "ckexe2bin", {"EDIT.EXE", "EDIT.COM"}, d), 0);

    DOSBASE_TEST_EXPECT_EQ(
        dosbase::bootstrap_test::run_tool(
            tool_dir, "cklink", {"HELP.OBJ+DOSUI.OBJ,HELP.EXE,NUL,;"}, d),
        0);
    DOSBASE_TEST_EXPECT_EQ(
        dosbase::bootstrap_test::run_tool(tool_dir, "ckexe2bin", {"HELP.EXE", "HELP.COM"}, d), 0);

    for (const char* com_name : {"EDIT.COM", "HELP.COM"}) {
        const fs::path p = d / com_name;
        DOSBASE_TEST_EXPECT(fs::is_regular_file(p));
        const auto sz = fs::file_size(p);
        DOSBASE_TEST_EXPECT(sz > 1024u && sz < 0xFF00u);
        std::ifstream in(p, std::ios::binary);
        char hdr[2]{0, 0};
        in.read(hdr, 2);
        DOSBASE_TEST_EXPECT(!(hdr[0] == 'M' && hdr[1] == 'Z'));
    }
}
#endif
