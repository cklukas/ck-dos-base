// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include "dosbase/devtools/mz_image.hpp"
#include "dosbase/devtools/omf_library.hpp"

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
using dosbase::devtools::MzImage;
using dosbase::devtools::OmfLibrary;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("dosbase-rt-" + std::to_string(stamp) + "-" +
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
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::vector<std::uint8_t> read_binary_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

bool has_symbol(const OmfLibrary& lib, const std::string& name) {
    for (const auto& entry : lib.symbols()) {
        if (entry.name == name) {
            return true;
        }
    }
    return false;
}

}  // namespace

#ifdef DOSBASE_TOOL_DIR
DOSBASE_TEST(runtime_clean_room_libs_build_package_and_link) {
    const fs::path rt_root = fs::path(DOSBASE_SOURCE_DIR) / "runtime";
    const fs::path tool_dir(DOSBASE_TOOL_DIR);

    TemporaryDirectory temp;
    const fs::path d = temp.path();

    auto run_asm = [&](const fs::path& src, const std::string& obj) {
        const fs::path local_asm = d / src.filename();
        fs::copy_file(src, local_asm, fs::copy_options::overwrite_existing);
        DOSBASE_TEST_EXPECT_EQ(
            dosbase::bootstrap_test::run_tool(
                tool_dir, "ckmasm", {"-Mx", src.filename().string() + "," + obj + ";"}, d),
            0);
    };

    auto run_cc = [&](const fs::path& src, const std::string& obj) {
        DOSBASE_TEST_EXPECT_EQ(
            dosbase::bootstrap_test::run_tool(
                tool_dir, "ckcl", {"-c", "-AS", "-Zl", "-Fo" + obj, src.string()}, d),
            0);
    };

    auto run_lib = [&](const std::string& libname, const std::vector<std::string>& objs) {
        std::ostringstream rsp;
        rsp << libname << "\nY\n";
        for (std::size_t i = 0; i < objs.size(); ++i) {
            rsp << "+" << objs[i] << (i + 1 < objs.size() ? "&\n" : ";\n");
        }
        const std::string rsp_name = libname + ".RSP";
        write_text_file(d / rsp_name, rsp.str());
        DOSBASE_TEST_EXPECT_EQ(
            dosbase::bootstrap_test::run_tool(tool_dir, "cklib", {"@" + rsp_name}, d),
            0);
    };

    run_asm(rt_root / "crt" / "crt0.asm", "CRT0.OBJ");
    run_asm(rt_root / "crt" / "dos_io.asm", "DOS_IO.OBJ");
    run_cc(rt_root / "crt" / "crt_mem.c", "CRT_MEM.OBJ");
    run_cc(rt_root / "crt" / "crt_str.c", "CRT_STR.OBJ");
    run_cc(rt_root / "crt" / "crt_stdio.c", "CRT_STDIO.OBJ");
    run_cc(rt_root / "crt" / "setargv.c", "SETARGV.OBJ");
    run_cc(rt_root / "crt" / "binmode.c", "BINMODE.OBJ");
    run_cc(rt_root / "comsubs" / "comsubs.c", "COMSUBS.OBJ");
    run_asm(rt_root / "comsubs" / "realopen.asm", "REALOPEN.OBJ");
    run_cc(rt_root / "string" / "string_ext.c", "STRING.OBJ");
    run_asm(rt_root / "cassfar" / "cassfar.asm", "CASSFAR.OBJ");

    run_lib("SLIBCE.LIB", {"CRT0.OBJ", "DOS_IO.OBJ", "CRT_MEM.OBJ", "CRT_STR.OBJ", "CRT_STDIO.OBJ"});
    run_lib("COMSUBS.LIB", {"COMSUBS.OBJ", "REALOPEN.OBJ"});
    run_lib("CASSFAR.LIB", {"CASSFAR.OBJ"});

    const OmfLibrary slib = OmfLibrary::parse(read_binary_file(d / "SLIBCE.LIB"));
    DOSBASE_TEST_EXPECT(has_symbol(slib, "__acrtused"));
    DOSBASE_TEST_EXPECT(has_symbol(slib, "__chkstk"));
    DOSBASE_TEST_EXPECT(has_symbol(slib, "_printf"));
    DOSBASE_TEST_EXPECT(has_symbol(slib, "_malloc"));
    DOSBASE_TEST_EXPECT(has_symbol(slib, "_strlen"));
    DOSBASE_TEST_EXPECT(has_symbol(slib, "_intdos"));
    DOSBASE_TEST_EXPECT(has_symbol(slib, "_int86"));

    const OmfLibrary comsubs = OmfLibrary::parse(read_binary_file(d / "COMSUBS.LIB"));
    DOSBASE_TEST_EXPECT(has_symbol(comsubs, "_comgetarg"));
    DOSBASE_TEST_EXPECT(has_symbol(comsubs, "_computmsg"));
    DOSBASE_TEST_EXPECT(has_symbol(comsubs, "_com_toupper"));
    DOSBASE_TEST_EXPECT(has_symbol(comsubs, "REALOPEN"));

    const OmfLibrary cassfar = OmfLibrary::parse(read_binary_file(d / "CASSFAR.LIB"));
    DOSBASE_TEST_EXPECT(has_symbol(cassfar, "BOXDR"));
    DOSBASE_TEST_EXPECT(has_symbol(cassfar, "COLOR"));
    DOSBASE_TEST_EXPECT(has_symbol(cassfar, "CORNER"));
    DOSBASE_TEST_EXPECT(has_symbol(cassfar, "SLCTP"));
    DOSBASE_TEST_EXPECT(has_symbol(cassfar, "WWRAP"));

    write_text_file(
        d / "APP.C",
        "extern int printf(const char *, ...);\n"
        "extern unsigned int strlen(const char *);\n"
        "extern void *malloc(unsigned int);\n"
        "extern int com_toupper(unsigned char);\n"
        "static char big_bss[8192];\n"
        "int comm_arr[2048];\n"
        "int main(void) {\n"
        "    char *p = (char *)malloc(16);\n"
        "    big_bss[8191] = 1;\n"
        "    comm_arr[2047] = 2;\n"
        "    if (p) printf(\"%u %c %d\\n\", strlen(\"DOS\"), com_toupper('a'), big_bss[0] + comm_arr[0]);\n"
        "    return 0;\n"
        "}\n");
    run_cc(d / "APP.C", "APP.OBJ");

    DOSBASE_TEST_EXPECT_EQ(
        dosbase::bootstrap_test::run_tool(
            tool_dir, "cklink", {"APP.OBJ,APP.EXE,,SLIBCE.LIB+COMSUBS.LIB;"}, d),
        0);
    DOSBASE_TEST_EXPECT(fs::is_regular_file(d / "APP.EXE"));

    const auto exe_bytes = read_binary_file(d / "APP.EXE");
    DOSBASE_TEST_EXPECT(exe_bytes.size() < 10000u);
    const MzImage mz = MzImage::parse(exe_bytes);
    DOSBASE_TEST_EXPECT_EQ(mz.header().signature, std::uint16_t{0x5A4D});
    DOSBASE_TEST_EXPECT(mz.header().minimum_extra_paragraphs >= 768u);
}
#endif
