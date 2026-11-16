// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace dosbase::bootstrap_test {
struct TestCase { const char* name; void (*function)(); };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> tests; return tests; }
struct Registrar {
    Registrar(const char* name, void (*function)()) { registry().push_back({name, function}); }
};

inline int run_tool(const std::filesystem::path& tool_dir,
                    const std::string& tool_name,
                    const std::vector<std::string>& args,
                    const std::filesystem::path& cwd) {
#if defined(_WIN32)
    std::filesystem::path exe = tool_dir / (tool_name + ".exe");
    auto quote_win = [](const std::string& s) -> std::string {
        if (!s.empty() && s.find_first_of(" \t\"") == std::string::npos) return s;
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else out += c;
        }
        out += "\"";
        return out;
    };
    std::string cmdline = quote_win(exe.string());
    for (const std::string& a : args) {
        cmdline += ' ';
        cmdline += quote_win(a);
    }
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> mut(cmdline.begin(), cmdline.end());
    mut.push_back('\0');
    const std::string cwd_str = cwd.string();
    if (!CreateProcessA(exe.string().c_str(), mut.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, cwd_str.c_str(), &si, &pi)) {
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    std::filesystem::path exe = tool_dir / tool_name;
    const pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(127);
        std::vector<std::string> copy;
        copy.push_back(exe.string());
        copy.insert(copy.end(), args.begin(), args.end());
        std::vector<char*> argv_ptrs;
        for (auto& s : copy) argv_ptrs.push_back(s.data());
        argv_ptrs.push_back(nullptr);
        execv(copy[0].c_str(), argv_ptrs.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

template <typename Actual, typename Expected>
void expect_equal(const Actual& actual, const Expected& expected, const char* expression,
                  const char* file, int line) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << file << ':' << line << ": expected " << expression;
        throw std::runtime_error(message.str());
    }
}
}  // namespace dosbase::bootstrap_test

#define DOSBASE_TEST_JOIN2(a, b) a##b
#define DOSBASE_TEST_JOIN(a, b) DOSBASE_TEST_JOIN2(a, b)
#define DOSBASE_TEST(name) \
    static void name(); \
    static ::dosbase::bootstrap_test::Registrar DOSBASE_TEST_JOIN(registrar_, __LINE__)(#name, &name); \
    static void name()
#define DOSBASE_TEST_EXPECT(expression) do { if (!(expression)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": expected " #expression); } while (false)
#define DOSBASE_TEST_EXPECT_EQ(actual, expected) ::dosbase::bootstrap_test::expect_equal((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
