// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/nmake_exec.hpp"

#include "dosbase/devtools/dos_file.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dosbase::devtools {
namespace fs = std::filesystem;
namespace {

std::string lowercase_ascii(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string program_stem_lower(std::string_view program) {
    std::size_t slash = program.find_last_of("/\\");
    std::string_view base = slash == std::string_view::npos ? program : program.substr(slash + 1);
    std::string lower = lowercase_ascii(base);
    for (const std::string_view ext : {".exe", ".com", ".bat"}) {
        if (lower.size() > ext.size() &&
            lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0) {
            lower.resize(lower.size() - ext.size());
            break;
        }
    }
    return lower;
}

bool wildcard_match_ci(std::string_view pattern, std::string_view text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star_p = std::string_view::npos;
    std::size_t star_t = 0;
    while (t < text.size()) {
        if (p < pattern.size() &&
            (pattern[p] == '?' ||
             std::toupper(static_cast<unsigned char>(pattern[p])) ==
                 std::toupper(static_cast<unsigned char>(text[t])))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_t = t;
        } else if (star_p != std::string_view::npos) {
            p = star_p + 1;
            t = ++star_t;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

int run_del(const ParsedCommand& cmd) {
    int status = 0;
    for (const std::string& arg : cmd.args) {
        if (arg.find('*') != std::string::npos || arg.find('?') != std::string::npos) {
            std::string normalized(arg);
            for (char& ch : normalized) {
                if (ch == '\\') ch = '/';
            }
            const fs::path p(normalized);
            const fs::path dir = p.has_parent_path() ? resolve_dos_input(p.parent_path().string())
                                                     : fs::path(".");
            const std::string pat = p.filename().string();
            std::error_code ec;
            for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
                if (wildcard_match_ci(pat, entry.path().filename().string())) {
                    fs::remove(entry.path(), ec);
                }
            }
            continue;
        }
        const fs::path target = resolve_dos_input(arg);
        std::error_code ec;
        if (!fs::remove(target, ec) && ec) {
            status = 1;
        }
    }
    return status;
}

int run_copy(const ParsedCommand& cmd) {
    std::vector<std::string> positional;
    for (const std::string& arg : cmd.args) {
        if (arg.size() >= 2 && arg[0] == '/') continue;  // /B, /A, /V, /Y
        positional.push_back(arg);
    }
    if (positional.size() != 2) {
        throw std::runtime_error("copy requires source and destination");
    }
    std::vector<std::string> sources;
    std::size_t start = 0;
    while (start <= positional[0].size()) {
        const std::size_t plus = positional[0].find('+', start);
        const std::string part = (plus == std::string::npos)
                                     ? positional[0].substr(start)
                                     : positional[0].substr(start, plus - start);
        if (!part.empty()) sources.push_back(part);
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    if (sources.empty()) {
        throw std::runtime_error("copy requires source and destination");
    }
    if (sources.size() == 1) {
        const fs::path src = resolve_dos_input(sources[0]);
        fs::path dst = resolve_dos_output(positional[1]);
        std::error_code ec;
        if (fs::is_directory(dst, ec)) {
            dst /= dos_upper(src.filename().string());
        }
        if (!fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec)) {
            throw std::runtime_error("copy failed: " + ec.message());
        }
        return 0;
    }
    const fs::path dst = resolve_dos_output(positional[1]);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("copy failed opening destination: " + dst.string());
    }
    for (const std::string& s : sources) {
        const fs::path src = resolve_dos_input(s);
        std::ifstream in(src, std::ios::binary);
        if (!in) {
            throw std::runtime_error("copy failed opening source: " + src.string());
        }
        out << in.rdbuf();
    }
    return 0;
}

int run_mkdir(const ParsedCommand& cmd) {
    for (const std::string& arg : cmd.args) {
        std::error_code ec;
        fs::create_directories(resolve_dos_output(arg), ec);
        if (ec) {
            throw std::runtime_error("mkdir failed: " + ec.message());
        }
    }
    return 0;
}

int run_cd(const ParsedCommand& cmd) {
    if (cmd.args.empty()) return 0;
    const fs::path dir = resolve_dos_input(cmd.args[0]);
    std::error_code ec;
    fs::current_path(dir, ec);
    if (ec) {
        throw std::runtime_error("cd " + cmd.args[0] + " failed: " + ec.message());
    }
    return 0;
}

int run_echo(const ParsedCommand& cmd) {
    std::string line;
    for (std::size_t i = 0; i < cmd.args.size(); ++i) {
        if (i > 0) line.push_back(' ');
        line += cmd.args[i];
    }
    if (cmd.stdout_file.has_value()) {
        if (lowercase_ascii(*cmd.stdout_file) == "nul") return 0;
        std::ofstream out(resolve_dos_output(*cmd.stdout_file), std::ios::trunc);
        out << line << "\n";
        return out ? 0 : 1;
    }
    std::cout << line << "\n";
    return 0;
}

int spawn_process(const std::string& executable,
                  const std::vector<std::string>& args,
                  const std::optional<std::string>& stdin_file,
                  const std::optional<std::string>& stdout_file,
                  bool use_path) {
#if defined(_WIN32)
    auto quote_arg = [](const std::string& a) {
        if (a.find_first_of(" \t\"") == std::string::npos) return a;
        std::string q = "\"";
        for (char c : a) {
            if (c == '"') q += "\\\"";
            else q.push_back(c);
        }
        q.push_back('"');
        return q;
    };
    std::string cmdline = quote_arg(executable);
    for (const std::string& a : args) {
        cmdline.push_back(' ');
        cmdline += quote_arg(a);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    HANDLE h_in = INVALID_HANDLE_VALUE;
    HANDLE h_out = INVALID_HANDLE_VALUE;
    if (stdin_file.has_value() || stdout_file.has_value()) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        if (stdin_file.has_value()) {
            const fs::path in_path = resolve_dos_input(*stdin_file);
            h_in = CreateFileA(in_path.string().c_str(), GENERIC_READ, FILE_SHARE_READ,
                               &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h_in == INVALID_HANDLE_VALUE) {
                throw std::runtime_error("cannot open stdin file: " + in_path.string());
            }
            si.hStdInput = h_in;
        }
        if (stdout_file.has_value()) {
            const std::string target = lowercase_ascii(*stdout_file) == "nul"
                                           ? "NUL"
                                           : resolve_dos_output(*stdout_file).string();
            h_out = CreateFileA(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h_out == INVALID_HANDLE_VALUE) {
                if (h_in != INVALID_HANDLE_VALUE) CloseHandle(h_in);
                throw std::runtime_error("cannot open stdout file: " + target);
            }
            si.hStdOutput = h_out;
        }
    }

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessA(
        use_path ? nullptr : executable.c_str(), cmdline.data(), nullptr, nullptr,
        (stdin_file.has_value() || stdout_file.has_value()) ? TRUE : FALSE,
        0, nullptr, nullptr, &si, &pi);
    if (h_in != INVALID_HANDLE_VALUE) CloseHandle(h_in);
    if (h_out != INVALID_HANDLE_VALUE) CloseHandle(h_out);
    if (!ok) {
        throw std::runtime_error("failed to launch " + executable);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(exit_code);
#else
    std::vector<std::string> argv_storage;
    argv_storage.reserve(1 + args.size());
    argv_storage.push_back(executable);
    for (const std::string& a : args) argv_storage.push_back(a);

    std::vector<char*> argv_ptrs;
    argv_ptrs.reserve(argv_storage.size() + 1);
    for (std::string& s : argv_storage) argv_ptrs.push_back(s.data());
    argv_ptrs.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (stdin_file.has_value()) {
        const fs::path in_path = resolve_dos_input(*stdin_file);
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, in_path.c_str(), O_RDONLY, 0);
    }
    if (stdout_file.has_value()) {
        const std::string out_target =
            lowercase_ascii(*stdout_file) == "nul"
                ? "/dev/null"
                : resolve_dos_output(*stdout_file).string();
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, out_target.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }

    pid_t pid = 0;
    const int spawn_rc =
        use_path ? posix_spawnp(&pid, executable.c_str(), &actions, nullptr, argv_ptrs.data(), environ)
                 : posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv_ptrs.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_rc != 0) {
        throw std::runtime_error("failed to launch " + executable + ": " +
                                 std::generic_category().message(spawn_rc));
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        throw std::runtime_error("waitpid failed for " + executable);
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
#endif
}

}  // namespace

ParsedCommand parse_nmake_command(std::string_view command_line) {
    ParsedCommand result;
    std::size_t i = 0;
    while (i < command_line.size() &&
           std::isspace(static_cast<unsigned char>(command_line[i]))) {
        ++i;
    }

    // Consume leading '@' and '-' recipe prefixes.
    while (i < command_line.size() && (command_line[i] == '@' || command_line[i] == '-')) {
        if (command_line[i] == '@') result.quiet = true;
        else result.ignore_error = true;
        ++i;
        while (i < command_line.size() &&
               std::isspace(static_cast<unsigned char>(command_line[i]))) {
            ++i;
        }
    }

    std::vector<std::string> tokens;
    while (i < command_line.size()) {
        while (i < command_line.size() &&
               std::isspace(static_cast<unsigned char>(command_line[i]))) {
            ++i;
        }
        if (i >= command_line.size()) break;

        if (command_line[i] == '<' || command_line[i] == '>') {
            const char dir = command_line[i++];
            if (dir == '>' && i < command_line.size() && command_line[i] == '>') {
                ++i;
            }
            while (i < command_line.size() &&
                   std::isspace(static_cast<unsigned char>(command_line[i]))) {
                ++i;
            }
            const std::size_t start = i;
            while (i < command_line.size() &&
                   !std::isspace(static_cast<unsigned char>(command_line[i]))) {
                ++i;
            }
            if (i > start) {
                std::string file(command_line.substr(start, i - start));
                if (dir == '<') result.stdin_file = std::move(file);
                else result.stdout_file = std::move(file);
            }
            continue;
        }

        std::string token;
        while (i < command_line.size() &&
               !std::isspace(static_cast<unsigned char>(command_line[i]))) {
            if (command_line[i] == '"') {
                ++i;
                while (i < command_line.size() && command_line[i] != '"') {
                    token.push_back(command_line[i++]);
                }
                if (i < command_line.size() && command_line[i] == '"') ++i;
            } else if (command_line[i] == '<' || command_line[i] == '>') {
                break;
            } else {
                token.push_back(command_line[i++]);
            }
        }
        if (!token.empty()) tokens.push_back(std::move(token));
    }

    if (!tokens.empty()) {
        result.program = std::move(tokens.front());
        result.args.assign(std::make_move_iterator(tokens.begin() + 1),
                           std::make_move_iterator(tokens.end()));
    }
    return result;
}

std::optional<std::string> map_dos_tool_name(std::string_view program) {
    static const std::unordered_map<std::string, std::string> kToolMap = {
        {"masm", "ckmasm"},
        {"cl", "ckcl"},
        {"link", "cklink"},
        {"lib", "cklib"},
        {"exe2bin", "ckexe2bin"},
        {"convert", "ckconvert"},
        {"exefix", "ckexefix"},
        {"menubld", "ckmenubld"},
        {"asc2hlp", "ckasc2hlp"},
        {"compress", "ckcompress"},
        {"buildmsg", "ckbuildmsg"},
        {"buildidx", "ckbuildidx"},
        {"nosrvbld", "cknosrvbld"},
        {"dbof", "ckdbof"},
        {"nmake", "cknmake"},
        {"make", "cknmake"},
    };
    const std::string stem = program_stem_lower(program);
    const auto it = kToolMap.find(stem);
    if (it == kToolMap.end()) return std::nullopt;
    return it->second;
}

int execute_nmake_command(const ParsedCommand& cmd, const NmakeExecOptions& options) {
    if (cmd.program.empty()) return 0;

    const std::string stem = program_stem_lower(cmd.program);
    if (stem == "del" || stem == "erase") return run_del(cmd);
    if (stem == "copy") return run_copy(cmd);
    if (stem == "mkdir" || stem == "md") return run_mkdir(cmd);
    if (stem == "cd" || stem == "chdir") return run_cd(cmd);
    if (stem == "echo") return run_echo(cmd);
    if (stem == "rem" || stem == "attrib") return 0;

    const std::optional<std::string> mapped = map_dos_tool_name(cmd.program);
    if (!mapped.has_value()) {
        throw std::runtime_error("unsupported command or tool: " + cmd.program);
    }

    std::string executable = *mapped;
    bool use_path = true;
    if (!options.tool_dir.empty()) {
        fs::path candidate = options.tool_dir / executable;
#if defined(_WIN32)
        if (!fs::exists(candidate)) {
            candidate = options.tool_dir / (executable + ".exe");
        }
#endif
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) {
            executable = candidate.string();
            use_path = false;
        }
    }

    return spawn_process(executable, cmd.args, cmd.stdin_file, cmd.stdout_file, use_path);
}

}  // namespace dosbase::devtools
