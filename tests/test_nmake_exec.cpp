// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/nmake_exec.hpp"

namespace {
namespace fs = std::filesystem;
using dosbase::devtools::NmakeExecOptions;
using dosbase::devtools::ParsedCommand;
using dosbase::devtools::execute_nmake_command;
using dosbase::devtools::map_dos_tool_name;
using dosbase::devtools::parse_nmake_command;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("dosbase-nmake-exec-" + std::to_string(stamp) + "-" +
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

class WorkingDirGuard {
public:
    explicit WorkingDirGuard(const fs::path& target) : saved_(fs::current_path()) {
        fs::current_path(target);
    }
    ~WorkingDirGuard() {
        std::error_code ec;
        fs::current_path(saved_, ec);
    }

private:
    fs::path saved_;
};

void write_text_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) throw std::runtime_error("failed to write " + path.string());
}

// throws_runtime_error removed; cl is now wired to ckcl
}  // namespace

DOSBASE_TEST(nmake_exec_parses_prefixes_args_and_redirections) {
    const ParsedCommand c1 = parse_nmake_command(
        "  @-masm -Mx -t -I. -I..\\..\\inc tree.asm,tree.obj;");
    DOSBASE_TEST_EXPECT(c1.quiet);
    DOSBASE_TEST_EXPECT(c1.ignore_error);
    DOSBASE_TEST_EXPECT_EQ(c1.program, std::string("masm"));
    DOSBASE_TEST_EXPECT_EQ(c1.args.size(), std::size_t{5});
    DOSBASE_TEST_EXPECT_EQ(c1.args[4], std::string("tree.asm,tree.obj;"));

    const ParsedCommand c2 = parse_nmake_command("exe2bin msbio.exe msbio.bin <locscr >nul");
    DOSBASE_TEST_EXPECT(!c2.quiet);
    DOSBASE_TEST_EXPECT(!c2.ignore_error);
    DOSBASE_TEST_EXPECT_EQ(c2.program, std::string("exe2bin"));
    DOSBASE_TEST_EXPECT_EQ(c2.args.size(), std::size_t{2});
    DOSBASE_TEST_EXPECT(c2.stdin_file.has_value());
    DOSBASE_TEST_EXPECT_EQ(*c2.stdin_file, std::string("locscr"));
    DOSBASE_TEST_EXPECT(c2.stdout_file.has_value());
    DOSBASE_TEST_EXPECT_EQ(*c2.stdout_file, std::string("nul"));
}

DOSBASE_TEST(nmake_exec_maps_dos_tools_to_native_executables) {
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("masm"), std::string("ckmasm"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("LINK.EXE"), std::string("cklink"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("c:\\dos\\exe2bin"), std::string("ckexe2bin"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("convert"), std::string("ckconvert"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("exefix"), std::string("ckexefix"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("menubld"), std::string("ckmenubld"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("asc2hlp"), std::string("ckasc2hlp"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("compress.com"), std::string("ckcompress"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("buildmsg"), std::string("ckbuildmsg"));
    DOSBASE_TEST_EXPECT_EQ(*map_dos_tool_name("cl"), std::string("ckcl"));
}

DOSBASE_TEST(nmake_exec_runs_builtin_shell_commands) {
    TemporaryDirectory temp;
    WorkingDirGuard guard(temp.path());
    const NmakeExecOptions options{};

    DOSBASE_TEST_EXPECT_EQ(
        execute_nmake_command(parse_nmake_command("mkdir sub"), options), 0);
    DOSBASE_TEST_EXPECT(fs::is_directory(temp.path() / "SUB"));

    DOSBASE_TEST_EXPECT_EQ(
        execute_nmake_command(parse_nmake_command("echo hello > sub\\msg.txt"), options), 0);
    DOSBASE_TEST_EXPECT(fs::is_regular_file(temp.path() / "SUB" / "MSG.TXT"));

    DOSBASE_TEST_EXPECT_EQ(
        execute_nmake_command(parse_nmake_command("copy sub\\msg.txt sub\\copy.txt"), options), 0);
    DOSBASE_TEST_EXPECT(fs::is_regular_file(temp.path() / "SUB" / "COPY.TXT"));

    DOSBASE_TEST_EXPECT_EQ(
        execute_nmake_command(parse_nmake_command("del sub\\*.txt"), options), 0);
    DOSBASE_TEST_EXPECT(!fs::exists(temp.path() / "SUB" / "MSG.TXT"));
    DOSBASE_TEST_EXPECT(!fs::exists(temp.path() / "SUB" / "COPY.TXT"));

    DOSBASE_TEST_EXPECT_EQ(
        execute_nmake_command(parse_nmake_command("attrib -R foo.txt"), options), 0);
}

#ifdef DOSBASE_TOOL_DIR
DOSBASE_TEST(nmake_exec_builds_assembly_makefile_end_to_end) {
    TemporaryDirectory temp;
    WorkingDirGuard guard(temp.path());

    write_text_file(
        temp.path() / "HELLO.ASM",
        "code segment\r\n"
        "assume cs:code\r\n"
        "org 100h\r\n"
        "start:\r\n"
        "    mov ax, 4c00h\r\n"
        "    int 21h\r\n"
        "code ends\r\n"
        "end start\r\n");

    write_text_file(
        temp.path() / "MAKEFILE",
        "all: hello.com\r\n"
        "hello.obj: hello.asm\r\n"
        "\tmasm hello.asm,hello.obj;\r\n"
        "hello.exe: hello.obj\r\n"
        "\tlink hello.obj,hello.exe;\r\n"
        "hello.com: hello.exe\r\n"
        "\texe2bin hello.exe hello.com\r\n"
        "\tdel hello.exe\r\n");

    ParsedCommand cmd;
    cmd.program = "nmake";
    cmd.args = {"/S"};
    const NmakeExecOptions options{fs::path(DOSBASE_TOOL_DIR), false, true};
    DOSBASE_TEST_EXPECT_EQ(execute_nmake_command(cmd, options), 0);

    DOSBASE_TEST_EXPECT(fs::is_regular_file(temp.path() / "HELLO.OBJ"));
    DOSBASE_TEST_EXPECT(fs::is_regular_file(temp.path() / "HELLO.COM"));
    DOSBASE_TEST_EXPECT(!fs::exists(temp.path() / "HELLO.EXE"));
    DOSBASE_TEST_EXPECT_EQ(fs::file_size(temp.path() / "HELLO.COM"), std::uintmax_t{5});
}
#endif
