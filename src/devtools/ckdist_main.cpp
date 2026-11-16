// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/core/diskimg_core.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace {

enum class OsTarget {
    Dos4,
    Dos5,
    All
};

OsTarget parse_os_target(std::string_view value) {
    std::string low(value);
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (low == "dos4" || low == "4" || low == "4.0" || low == "4.00") return OsTarget::Dos4;
    if (low == "dos5" || low == "5" || low == "5.0" || low == "5.00") return OsTarget::Dos5;
    if (low == "all" || low == "both") return OsTarget::All;
    throw std::runtime_error("invalid --os value '" + std::string(value) + "' (expected dos4, dos5, or all)");
}

bool includes_dos4(OsTarget t) noexcept {
    return t == OsTarget::Dos4 || t == OsTarget::All;
}

bool includes_dos5(OsTarget t) noexcept {
    return t == OsTarget::Dos5 || t == OsTarget::All;
}

// ---------------------------------------------------------------------------
// First-principles FIPS 180-4 SHA-256 (constants derived from prime roots)
// ---------------------------------------------------------------------------

struct Sha256Tables {
    std::array<std::uint32_t, 8> h0{};
    std::array<std::uint32_t, 64> k{};

    Sha256Tables() {
        std::vector<int> primes;
        primes.reserve(64);
        for (int n = 2; primes.size() < 64; ++n) {
            bool is_p = true;
            for (int p : primes) {
                if (p * p > n) break;
                if (n % p == 0) {
                    is_p = false;
                    break;
                }
            }
            if (is_p) primes.push_back(n);
        }
        const double two32 = 4294967296.0;
        for (std::size_t i = 0; i < 8; ++i) {
            double ip = 0.0;
            double frac = std::modf(std::sqrt(static_cast<double>(primes[i])), &ip);
            h0[i] = static_cast<std::uint32_t>(frac * two32);
        }
        for (std::size_t i = 0; i < 64; ++i) {
            double ip = 0.0;
            double frac = std::modf(std::cbrt(static_cast<double>(primes[i])), &ip);
            k[i] = static_cast<std::uint32_t>(frac * two32);
        }
    }
};

const Sha256Tables& sha256_tables() {
    static const Sha256Tables tbl;
    return tbl;
}

inline std::uint32_t rotr32(std::uint32_t x, unsigned int n) {
    return (x >> n) | (x << (32u - n));
}

class Sha256 {
public:
    Sha256() { state_ = sha256_tables().h0; }

    void update(const std::uint8_t* data, std::size_t len) {
        total_bytes_ += len;
        std::size_t pos = 0;
        if (buf_len_ > 0) {
            const std::size_t take = std::min<std::size_t>(64u - buf_len_, len);
            std::memcpy(buf_.data() + buf_len_, data, take);
            buf_len_ += take;
            pos += take;
            if (buf_len_ == 64u) {
                compress(buf_.data());
                buf_len_ = 0;
            }
        }
        while (pos + 64u <= len) {
            compress(data + pos);
            pos += 64u;
        }
        if (pos < len) {
            buf_len_ = len - pos;
            std::memcpy(buf_.data(), data + pos, buf_len_);
        }
    }

    std::string final_hex() {
        const std::uint64_t bit_len = total_bytes_ * 8ULL;
        const std::uint8_t one = 0x80u;
        update(&one, 1);
        const std::uint8_t zero = 0x00u;
        while (buf_len_ != 56u) {
            update(&zero, 1);
        }
        std::uint8_t len_be[8];
        for (int i = 0; i < 8; ++i) {
            len_be[i] = static_cast<std::uint8_t>((bit_len >> ((7 - i) * 8)) & 0xFFu);
        }
        update(len_be, 8);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (std::uint32_t w : state_) {
            oss << std::setw(8) << w;
        }
        return oss.str();
    }

private:
    void compress(const std::uint8_t blk[64]) {
        const auto& k = sha256_tables().k;
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(blk[i * 4 + 0]) << 24) |
                   (static_cast<std::uint32_t>(blk[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(blk[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(blk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t t1 = h + s1 + ch + k[static_cast<std::size_t>(i)] + w[i];
            const std::uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buf_{};
    std::size_t buf_len_ = 0;
    std::uint64_t total_bytes_ = 0;
};

std::string sha256_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open file for sha256: " + path.string());
    Sha256 hasher;
    std::array<std::uint8_t, 65536> buf{};
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const auto got = in.gcount();
        if (got > 0) hasher.update(buf.data(), static_cast<std::size_t>(got));
    }
    return hasher.final_hex();
}

// ---------------------------------------------------------------------------
// Temporary Workspace Guard (enforces Mac /Volumes/S8T/tmp policy when present)
// ---------------------------------------------------------------------------

fs::path select_temp_base() {
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
    if (fs::is_directory("/Volumes/PRO-BLADE")) {
        fs::path pb("/Volumes/PRO-BLADE/tmp");
        std::error_code ec;
        fs::create_directories(pb, ec);
        return pb;
    }
#endif
    return fs::temp_directory_path();
}

class ScratchDir {
public:
    explicit ScratchDir(const std::string& prefix) {
        static std::atomic<std::uint64_t> seq{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = select_temp_base() /
                (prefix + "-" + std::to_string(stamp) + "-" + std::to_string(seq.fetch_add(1)));
        fs::create_directories(path_);
    }

    ~ScratchDir() {
        std::error_code ec;
        for (auto& entry : fs::recursive_directory_iterator(path_, ec)) {
            fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add, ec);
        }
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

// ---------------------------------------------------------------------------
// Cross-platform Child Process Runner
// ---------------------------------------------------------------------------

fs::path exe_path(const fs::path& tool_dir, const std::string& name) {
#if defined(_WIN32)
    fs::path p = tool_dir / (name + ".exe");
    if (fs::exists(p)) return p;
    fs::path rel = tool_dir / "Release" / (name + ".exe");
    if (fs::exists(rel)) return rel;
    return p;
#else
    fs::path p = tool_dir / name;
    if (fs::exists(p)) return p;
    fs::path rel = tool_dir / "Release" / name;
    if (fs::exists(rel)) return rel;
    return p;
#endif
}

int run_process(const std::vector<std::string>& args,
                const fs::path& cwd,
                const std::map<std::string, std::string>* extra_env = nullptr,
                bool quiet_stdout = true) {
    if (args.empty()) return 1;
#if defined(_WIN32)
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
    std::string cmdline;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmdline += ' ';
        cmdline += quote_win(args[i]);
    }

    struct WinEnvCiLess {
        bool operator()(const std::string& a, const std::string& b) const {
            const std::size_t n = std::min(a.size(), b.size());
            for (std::size_t i = 0; i < n; ++i) {
                const unsigned char ca = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(a[i])));
                const unsigned char cb = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(b[i])));
                if (ca != cb) return ca < cb;
            }
            return a.size() < b.size();
        }
    };
    std::map<std::string, std::string, WinEnvCiLess> merged_env;
    if (LPCH env_strings = GetEnvironmentStringsA()) {
        const char* p = env_strings;
        while (*p) {
            std::string entry(p);
            p += entry.size() + 1;
            const auto eq = entry.find('=');
            if (eq != std::string::npos && eq > 0) {
                merged_env[entry.substr(0, eq)] = entry.substr(eq + 1);
            }
        }
        FreeEnvironmentStringsA(env_strings);
    }
    if (extra_env) {
        for (const auto& [k, v] : *extra_env) {
            merged_env.erase(k);
            merged_env[k] = v;
        }
    }
    std::string env_block;
    for (const auto& [k, v] : merged_env) {
        env_block += k + "=" + v;
        env_block.push_back('\0');
    }
    env_block.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    HANDLE nul_handle = INVALID_HANDLE_VALUE;
    if (quiet_stdout) {
        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        nul_handle = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nul_handle != INVALID_HANDLE_VALUE) {
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            si.hStdOutput = nul_handle;
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        }
    }

    PROCESS_INFORMATION pi{};
    std::vector<char> mut_cmd(cmdline.begin(), cmdline.end());
    mut_cmd.push_back('\0');
    const std::string cwd_str = cwd.string();
    const BOOL ok = CreateProcessA(
        args[0].c_str(), mut_cmd.data(), nullptr, nullptr, quiet_stdout ? TRUE : FALSE, 0,
        env_block.data(), cwd_str.c_str(), &si, &pi);
    if (nul_handle != INVALID_HANDLE_VALUE) CloseHandle(nul_handle);
    if (!ok) {
        std::cerr << "ckdist: failed launching " << args[0] << "\n";
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    std::map<std::string, std::string> merged_env;
    for (char** ep = environ; ep && *ep; ++ep) {
        std::string entry(*ep);
        const auto eq = entry.find('=');
        if (eq != std::string::npos) {
            merged_env[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
    }
    if (extra_env) {
        for (const auto& [k, v] : *extra_env) merged_env[k] = v;
    }

    const pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(127);
        if (quiet_stdout) {
            if (FILE* f = std::fopen("/dev/null", "wb")) {
                dup2(fileno(f), STDOUT_FILENO);
                std::fclose(f);
            }
        }
        std::vector<std::string> env_strs;
        env_strs.reserve(merged_env.size());
        std::vector<char*> envp;
        for (const auto& [k, v] : merged_env) {
            env_strs.push_back(k + "=" + v);
        }
        for (auto& s : env_strs) envp.push_back(s.data());
        envp.push_back(nullptr);

        std::vector<char*> argv_ptrs;
        std::vector<std::string> arg_copy = args;
        for (auto& s : arg_copy) argv_ptrs.push_back(s.data());
        argv_ptrs.push_back(nullptr);

        execve(arg_copy[0].c_str(), argv_ptrs.data(), envp.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
#endif
}

std::string upper_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

fs::path find_ci(const fs::path& dir, std::string_view name) {
    const std::string want = upper_ascii(name);
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (upper_ascii(entry.path().filename().string()) == want) {
            return entry.path();
        }
    }
    return dir / name;
}

void write_text_file(const fs::path& path, std::string_view content) {
    if (path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write file: " + path.string());
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string read_text_trimmed(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return "0.1.0";
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    return s.empty() ? "0.1.0" : s;
}

// ---------------------------------------------------------------------------
// Clean-Room Runtime & Dual-OS (CK-DOS 4.0 + CK-DOS 5.0) Tree Builders
// ---------------------------------------------------------------------------

struct ManifestEntry {
    std::string sha256;
    std::uint64_t size = 0;
};

fs::path compile_clean_room_runtime(const fs::path& repo_root,
                                    const fs::path& tool_dir,
                                    const fs::path& scratch_root) {
    const fs::path d = scratch_root / "rt_build";
    if (fs::is_regular_file(d / "SLIBCE.LIB")) {
        return d;
    }
    const fs::path ckcl = exe_path(tool_dir, "ckcl");
    const fs::path ckmasm = exe_path(tool_dir, "ckmasm");
    const fs::path cklib = exe_path(tool_dir, "cklib");
    const fs::path rt_root = repo_root / "runtime";
    fs::create_directories(d);

    auto run_asm = [&](const fs::path& src, const std::string& obj) {
        fs::copy_file(src, d / src.filename(), fs::copy_options::overwrite_existing);
        if (run_process({ckmasm.string(), "-Mx", src.filename().string() + "," + obj + ";"}, d) != 0) {
            throw std::runtime_error("ckmasm failed on " + src.string());
        }
    };

    auto run_cc = [&](const fs::path& src, const std::string& obj) {
        if (run_process({ckcl.string(), "-c", "-AS", "-Zl", "-Fo" + obj, src.string()}, d) != 0) {
            throw std::runtime_error("ckcl failed on " + src.string());
        }
    };

    auto run_mklib = [&](const std::string& libname, const std::vector<std::string>& objs) {
        std::ostringstream rsp;
        rsp << libname << "\nY\n";
        for (std::size_t i = 0; i < objs.size(); ++i) {
            rsp << "+" << objs[i] << (i + 1 < objs.size() ? "&\n" : ";\n");
        }
        const std::string rsp_name = libname + ".RSP";
        write_text_file(d / rsp_name, rsp.str());
        if (run_process({cklib.string(), "@" + rsp_name}, d) != 0) {
            throw std::runtime_error("cklib failed on " + libname);
        }
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

    run_mklib("SLIBCE.LIB", {"CRT0.OBJ", "DOS_IO.OBJ", "CRT_MEM.OBJ", "CRT_STR.OBJ", "CRT_STDIO.OBJ"});
    run_mklib("COMSUBS.LIB", {"COMSUBS.OBJ", "REALOPEN.OBJ"});
    run_mklib("CASSFAR.LIB", {"CASSFAR.OBJ"});
    return d;
}

void install_clean_room_runtime(const fs::path& rt_build_dir, const fs::path& stage) {
    const fs::path bld_lib = stage / "TOOLS" / "BLD" / "LIB";
    fs::create_directories(bld_lib);
    for (const char* name : {"SLIBCE.LIB", "SLIBC.LIB", "MLIBCE.LIB", "CLIBCE.LIB", "LLIBCE.LIB"}) {
        fs::copy_file(rt_build_dir / "SLIBCE.LIB", bld_lib / name, fs::copy_options::overwrite_existing);
    }
    fs::copy_file(rt_build_dir / "SETARGV.OBJ", bld_lib / "SETARGV.OBJ", fs::copy_options::overwrite_existing);
    fs::copy_file(rt_build_dir / "BINMODE.OBJ", bld_lib / "BINMODE.OBJ", fs::copy_options::overwrite_existing);

    fs::create_directories(stage / "LIB");
    fs::create_directories(stage / "INC");
    fs::create_directories(stage / "SELECT");
    fs::copy_file(rt_build_dir / "SLIBCE.LIB", stage / "LIB" / "MEM.LIB", fs::copy_options::overwrite_existing);
    fs::copy_file(rt_build_dir / "COMSUBS.LIB", stage / "INC" / "COMSUBS.LIB", fs::copy_options::overwrite_existing);
    fs::copy_file(rt_build_dir / "STRING.OBJ", stage / "INC" / "STRING.OBJ", fs::copy_options::overwrite_existing);
    fs::copy_file(rt_build_dir / "CASSFAR.LIB", stage / "SELECT" / "CASSFAR.LIB", fs::copy_options::overwrite_existing);
}

void verify_dos5_zero_prebuilt_binaries(const fs::path& dos5_src) {
    static const std::set<std::string> forbidden_exts = {".EXE", ".COM", ".OBJ", ".SYS", ".BIN"};
    for (const auto& entry : fs::recursive_directory_iterator(dos5_src)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = upper_ascii(entry.path().extension().string());
        if (forbidden_exts.count(ext) > 0) {
            throw std::runtime_error("forbidden prebuilt binary in os/ck-dos-5/src: " +
                                     fs::relative(entry.path(), dos5_src).generic_string());
        }
        if (ext == ".LIB") {
            std::ifstream in(entry.path(), std::ios::binary);
            unsigned char magic = 0;
            if (in.read(reinterpret_cast<char*>(&magic), 1) && magic == 0xF0u) {
                throw std::runtime_error("forbidden prebuilt OMF library in os/ck-dos-5/src: " +
                                         fs::relative(entry.path(), dos5_src).generic_string());
            }
        }
    }
}

std::map<std::string, ManifestEntry> collect_manifest(const fs::path& stage) {
    static const std::set<std::string> exts = {".OBJ", ".LIB", ".EXE", ".COM", ".SYS", ".BIN"};
    std::map<std::string, ManifestEntry> manifest;
    std::vector<fs::path> all_files;
    for (const auto& entry : fs::recursive_directory_iterator(stage)) {
        if (entry.is_regular_file()) {
            const std::string ext = upper_ascii(entry.path().extension().string());
            if (exts.count(ext) > 0) all_files.push_back(entry.path());
        }
    }
    std::sort(all_files.begin(), all_files.end());
    for (const fs::path& p : all_files) {
        const std::string rel = fs::relative(p, stage).generic_string();
        manifest[rel] = {sha256_file(p), fs::file_size(p)};
    }
    return manifest;
}

std::map<std::string, ManifestEntry> parse_manifest_json(const fs::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open manifest: " + path.string());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::map<std::string, ManifestEntry> out;

    auto next_quoted = [&](std::size_t& pos) -> std::string {
        pos = text.find('"', pos);
        if (pos == std::string::npos) return {};
        ++pos;
        std::string s;
        while (pos < text.size() && text[pos] != '"') {
            if (text[pos] == '\\' && pos + 1 < text.size()) ++pos;
            s.push_back(text[pos++]);
        }
        if (pos < text.size()) ++pos;
        return s;
    };

    std::size_t pos = 0;
    while (true) {
        const std::string key = next_quoted(pos);
        if (key.empty() || pos >= text.size()) break;
        const std::size_t brace = text.find('{', pos);
        const std::size_t end_brace = text.find('}', brace);
        if (brace == std::string::npos || end_brace == std::string::npos) break;

        ManifestEntry entry;
        std::size_t ipos = brace + 1;
        while (ipos < end_brace) {
            std::size_t q = text.find('"', ipos);
            if (q == std::string::npos || q >= end_brace) break;
            const std::string field = next_quoted(ipos);
            const std::size_t colon = text.find(':', ipos);
            if (colon == std::string::npos || colon >= end_brace) break;
            ipos = colon + 1;
            while (ipos < end_brace && std::isspace(static_cast<unsigned char>(text[ipos]))) ++ipos;
            if (field == "sha256") {
                entry.sha256 = next_quoted(ipos);
            } else if (field == "size") {
                std::size_t end_num = ipos;
                while (end_num < end_brace && std::isdigit(static_cast<unsigned char>(text[end_num]))) {
                    ++end_num;
                }
                entry.size = std::stoull(text.substr(ipos, end_num - ipos));
                ipos = end_num;
            }
        }
        out[key] = std::move(entry);
        pos = end_brace + 1;
    }
    return out;
}

void write_manifest_json(const fs::path& path, const std::map<std::string, ManifestEntry>& manifest) {
    std::ostringstream oss;
    oss << "{\n";
    std::size_t idx = 0;
    for (const auto& [rel, info] : manifest) {
        oss << "  \"" << rel << "\": {\n"
            << "    \"sha256\": \"" << info.sha256 << "\",\n"
            << "    \"size\": " << info.size << "\n"
            << "  }" << (idx + 1 < manifest.size() ? "," : "") << "\n";
        ++idx;
    }
    oss << "}\n";
    write_text_file(path, oss.str());
}

void validate_flat_com_image(const fs::path& com_path, const std::string& label) {
    if (!fs::is_regular_file(com_path)) {
        throw std::runtime_error(label + " missing after ckexe2bin: " + com_path.string());
    }
    const auto sz = fs::file_size(com_path);
    if (sz == 0 || sz >= 0xFF00u) {
        throw std::runtime_error(label + " invalid COM size (" + std::to_string(sz) + " bytes; must be 1..65279)");
    }
    std::ifstream in(com_path, std::ios::binary);
    char hdr[2]{0, 0};
    in.read(hdr, 2);
    if (in.gcount() == 2 && hdr[0] == 'M' && hdr[1] == 'Z') {
        throw std::runtime_error(label + " still has MZ header instead of flat COM image");
    }
}

void build_dos5_dosui_tools(const fs::path& tool_dir, const fs::path& out_stage) {
    const fs::path dosui_dir = out_stage / "CMD" / "DOSUI";
    const fs::path edit_dir = out_stage / "CMD" / "EDIT";
    const fs::path help_dir = out_stage / "CMD" / "HELP";
    if (!fs::is_directory(dosui_dir) || !fs::is_directory(edit_dir) || !fs::is_directory(help_dir)) {
        throw std::runtime_error("[CK-DOS 5.0] missing CMD/DOSUI, CMD/EDIT, or CMD/HELP source directories");
    }

    const fs::path ckmasm = exe_path(tool_dir, "ckmasm");
    const fs::path cklink = exe_path(tool_dir, "cklink");
    const fs::path ckexe2bin = exe_path(tool_dir, "ckexe2bin");

    if (run_process({ckmasm.string(), "-I.", "DOSUI.ASM,DOSUI.OBJ,NUL,NUL;"}, dosui_dir) != 0) {
        throw std::runtime_error("[CK-DOS 5.0] ckmasm failed on CMD/DOSUI/DOSUI.ASM");
    }
    const fs::path dosui_obj = find_ci(dosui_dir, "DOSUI.OBJ");

    for (const char* mod : {"EDIT", "EDITHLP"}) {
        const std::string spec = std::string(mod) + ".ASM," + mod + ".OBJ,NUL,NUL;";
        if (run_process({ckmasm.string(), "-I.", "-I" + dosui_dir.string(), spec}, edit_dir) != 0) {
            throw std::runtime_error(std::string("[CK-DOS 5.0] ckmasm failed on CMD/EDIT/") + mod + ".ASM");
        }
    }
    // EDITHLP.OBJ must link last past DOSUI.OBJ so EDIT reads its help topics in place.
    fs::copy_file(dosui_obj, edit_dir / "DOSUI.OBJ", fs::copy_options::overwrite_existing);
    if (run_process({cklink.string(), "EDIT.OBJ+DOSUI.OBJ+EDITHLP.OBJ,EDIT.EXE,NUL,;"}, edit_dir) != 0) {
        throw std::runtime_error("[CK-DOS 5.0] cklink failed on EDIT.EXE");
    }
    if (run_process({ckexe2bin.string(), "EDIT.EXE", "EDIT.COM"}, edit_dir) != 0) {
        throw std::runtime_error("[CK-DOS 5.0] ckexe2bin failed on EDIT.COM");
    }
    std::error_code ec;
    fs::remove(find_ci(edit_dir, "EDIT.EXE"), ec);
    fs::remove(edit_dir / "DOSUI.OBJ", ec);
    const fs::path edit_com = find_ci(edit_dir, "EDIT.COM");
    validate_flat_com_image(edit_com, "EDIT.COM");
    const fs::path ckedit_com = edit_dir / "CKEDIT.COM";
    fs::copy_file(edit_com, ckedit_com, fs::copy_options::overwrite_existing);
    validate_flat_com_image(ckedit_com, "CKEDIT.COM");

    if (run_process({ckmasm.string(), "-I.", "-I" + dosui_dir.string(), "HELP.ASM,HELP.OBJ,NUL,NUL;"}, help_dir) != 0) {
        throw std::runtime_error("[CK-DOS 5.0] ckmasm failed on CMD/HELP/HELP.ASM");
    }
    fs::copy_file(dosui_obj, help_dir / "DOSUI.OBJ", fs::copy_options::overwrite_existing);
    if (run_process({cklink.string(), "HELP.OBJ+DOSUI.OBJ,HELP.EXE,NUL,;"}, help_dir) != 0) {
        throw std::runtime_error("[CK-DOS 5.0] cklink failed on HELP.EXE");
    }
    if (run_process({ckexe2bin.string(), "HELP.EXE", "HELP.COM"}, help_dir) != 0) {
        throw std::runtime_error("[CK-DOS 5.0] ckexe2bin failed on HELP.COM");
    }
    fs::remove(find_ci(help_dir, "HELP.EXE"), ec);
    fs::remove(help_dir / "DOSUI.OBJ", ec);
    const fs::path help_com = find_ci(help_dir, "HELP.COM");
    validate_flat_com_image(help_com, "HELP.COM");

    std::cout << "[CK-DOS 5.0] Built first-party DOSUI tools: EDIT.COM & CKEDIT.COM ("
              << fs::file_size(edit_com) << " bytes), HELP.COM ("
              << fs::file_size(help_com) << " bytes)\n";
}

void build_dos_source_stage(const std::string& os_label,
                            const fs::path& src_tree,
                            const fs::path& repo_root,
                            const fs::path& tool_dir,
                            const fs::path& scratch_root,
                            const std::string& stage_subdir,
                            fs::path& out_stage,
                            const fs::path& write_manifest,
                            const fs::path& verify_manifest,
                            bool build_dos5_extras = false) {
    if (!fs::is_directory(src_tree)) {
        throw std::runtime_error("missing " + os_label + " source tree: " + src_tree.string());
    }
    const fs::path rt_build_dir = compile_clean_room_runtime(repo_root, tool_dir, scratch_root);
    out_stage = scratch_root / stage_subdir;
    fs::copy(src_tree, out_stage, fs::copy_options::recursive);
    install_clean_room_runtime(rt_build_dir, out_stage);

    const fs::path cknmake = exe_path(tool_dir, "cknmake");
    const fs::path actual_tool_dir = cknmake.parent_path();
#if defined(_WIN32)
    const char path_sep = ';';
#else
    const char path_sep = ':';
#endif
    const char* cur_path = std::getenv("PATH");
    std::map<std::string, std::string> env;
    env["PATH"] = actual_tool_dir.string() + (cur_path ? (std::string(1, path_sep) + cur_path) : "");
    env["INIT"] = (out_stage / "TOOLS").string();
    env["COUNTRY"] = "USA-MS";
    env["INCLUDE"] = (out_stage / "INC").string() + ";" + (out_stage / "TOOLS" / "BLD" / "INC").string();
    env["LIB"] = (out_stage / "TOOLS" / "BLD" / "LIB").string() + ";" + (out_stage / "LIB").string();

    auto has_makefile = [](const fs::path& d) {
        return fs::is_regular_file(d / "MAKEFILE") || fs::is_regular_file(d / "makefile");
    };

    std::size_t built_count = 0;
    for (const char* core : {"MESSAGES", "MAPPER", "BOOT", "BIOS", "DOS"}) {
        const fs::path sub = out_stage / core;
        if (!has_makefile(sub)) continue;
        if (run_process({cknmake.string()}, sub, &env) != 0) {
            throw std::runtime_error("[" + os_label + "] cknmake failed in core target: " + core);
        }
        ++built_count;
    }

    for (const char* group : {"CMD", "DEV", "MEMM", "SELECT"}) {
        const fs::path gdir = out_stage / group;
        if (!fs::is_directory(gdir)) continue;
        std::vector<fs::path> candidates = {gdir};
        std::vector<fs::path> children;
        for (const auto& e : fs::directory_iterator(gdir)) {
            if (e.is_directory()) children.push_back(e.path());
        }
        std::sort(children.begin(), children.end());
        candidates.insert(candidates.end(), children.begin(), children.end());

        for (const fs::path& sub : candidates) {
            if (!has_makefile(sub)) continue;
            if (run_process({cknmake.string()}, sub, &env) != 0) {
                throw std::runtime_error("[" + os_label + "] cknmake failed in " +
                                         fs::relative(sub, out_stage).generic_string());
            }
            ++built_count;
        }
    }

    for (const char* req : {"BOOT/MSBOOT.BIN", "BIOS/IO.SYS", "DOS/MSDOS.SYS", "CMD/COMMAND/COMMAND.COM"}) {
        const fs::path p = out_stage / req;
        if (!fs::is_regular_file(p) || fs::file_size(p) == 0) {
            throw std::runtime_error("[" + os_label + "] required core artifact missing: " + req);
        }
    }

    if (build_dos5_extras) {
        build_dos5_dosui_tools(tool_dir, out_stage);
    }

    const auto manifest = collect_manifest(out_stage);
    std::cout << "[" << os_label << "] Built " << built_count
              << " directories cleanly; binary artifacts: " << manifest.size() << "\n";

    if (!write_manifest.empty()) {
        write_manifest_json(write_manifest, manifest);
        std::cout << "[" << os_label << "] Wrote manifest to " << write_manifest.string() << "\n";
    }
    if (!verify_manifest.empty()) {
        const auto expected = parse_manifest_json(verify_manifest);
        std::size_t mismatches = 0;
        for (const auto& [k, v] : expected) {
            auto it = manifest.find(k);
            if (it == manifest.end() || it->second.sha256 != v.sha256 || it->second.size != v.size) {
                std::cerr << "  MISMATCH " << k << "\n";
                ++mismatches;
            }
        }
        for (const auto& [k, _] : manifest) {
            if (expected.find(k) == expected.end()) {
                std::cerr << "  EXTRA " << k << "\n";
                ++mismatches;
            }
        }
        if (mismatches > 0) {
            throw std::runtime_error("[" + os_label + "] manifest verification failed with " +
                                     std::to_string(mismatches) + " differences");
        }
        std::cout << "[" << os_label << "] Manifest verification passed ("
                  << manifest.size() << " artifacts matched byte-for-byte)\n";
    }
}

void build_and_verify_dos4_tree(const fs::path& repo_root,
                                const fs::path& tool_dir,
                                const fs::path& scratch_root,
                                fs::path& out_stage,
                                const fs::path& write_manifest,
                                const fs::path& verify_manifest) {
    const fs::path src_tree = repo_root / "third_party" / "ms-dos-4" / "src";
    build_dos_source_stage("CK-DOS 4.0", src_tree, repo_root, tool_dir, scratch_root,
                           "src_dos4", out_stage, write_manifest, verify_manifest);
}

void build_and_verify_dos5_tree(const fs::path& repo_root,
                                const fs::path& tool_dir,
                                const fs::path& scratch_root,
                                fs::path& out_stage,
                                const fs::path& write_manifest) {
    const fs::path src_tree = repo_root / "os" / "ck-dos-5" / "src";
    if (!fs::is_directory(src_tree)) {
        throw std::runtime_error("missing CK-DOS 5.0 source tree: " + src_tree.string());
    }
    verify_dos5_zero_prebuilt_binaries(src_tree);
    build_dos_source_stage("CK-DOS 5.0", src_tree, repo_root, tool_dir, scratch_root,
                           "src_dos5", out_stage, write_manifest, {}, true);
}

// ---------------------------------------------------------------------------
// 16-bit DOS Self-Hosting Tools Builder (including CKMKIMG.EXE)
// ---------------------------------------------------------------------------

std::vector<fs::path> build_dos_self_host_tools(const fs::path& repo_root,
                                                const fs::path& tool_dir,
                                                const fs::path& out_dir) {
    fs::create_directories(out_dir);
    const fs::path obj_dir = out_dir / "obj";
    const fs::path lib_out = out_dir / "lib";
    fs::create_directories(obj_dir);
    fs::create_directories(lib_out);

    const fs::path ckmasm = exe_path(tool_dir, "ckmasm");
    const fs::path ckcl = exe_path(tool_dir, "ckcl");
    const fs::path cklib = exe_path(tool_dir, "cklib");
    const fs::path cklink = exe_path(tool_dir, "cklink");
    const fs::path crt_dir = repo_root / "runtime" / "crt";

    fs::copy_file(crt_dir / "crt0.asm", obj_dir / "crt0.asm", fs::copy_options::overwrite_existing);
    fs::copy_file(crt_dir / "dos_io.asm", obj_dir / "dos_io.asm", fs::copy_options::overwrite_existing);
    if (run_process({ckmasm.string(), "-Mx", "-t", "crt0.asm,crt0.obj;"}, obj_dir) != 0 ||
        run_process({ckmasm.string(), "-Mx", "-t", "dos_io.asm,dos_io.obj;"}, obj_dir) != 0) {
        throw std::runtime_error("failed assembling 16-bit CRT startup");
    }

    std::vector<std::string> crt_objs = {"+CRT0.OBJ", "+DOS_IO.OBJ"};
    for (const char* cname : {"crt_mem.c", "crt_str.c", "crt_stdio.c", "setargv.c", "binmode.c"}) {
        const std::string stem = fs::path(cname).stem().string();
        const fs::path obj = obj_dir / (stem + ".obj");
        if (run_process({ckcl.string(), "-c", "-AS", "-Os", "-Zp", "-Fo" + obj.string(),
                         (crt_dir / cname).string()}, obj_dir) != 0) {
            throw std::runtime_error(std::string("failed compiling 16-bit CRT ") + cname);
        }
        crt_objs.push_back("+" + find_ci(obj_dir, stem + ".obj").filename().string());
    }

    const fs::path slibc = obj_dir / "SLIBC.LIB";
    std::error_code ec;
    fs::remove(slibc, ec);
    std::vector<std::string> lib_cmd = {cklib.string(), "SLIBC.LIB"};
    lib_cmd.insert(lib_cmd.end(), crt_objs.begin(), crt_objs.end());
    lib_cmd.push_back(";");
    if (run_process(lib_cmd, obj_dir) != 0) {
        throw std::runtime_error("failed building SLIBC.LIB");
    }
    const fs::path built_slibc = find_ci(obj_dir, "SLIBC.LIB");
    fs::copy_file(built_slibc, lib_out / "SLIBC.LIB", fs::copy_options::overwrite_existing);

    const fs::path core_dir = repo_root / "src" / "devtools" / "core";
    const fs::path dos_dir = repo_root / "src" / "devtools" / "dos";
    const fs::path inc_dir = repo_root / "include";

    struct DosToolSpec {
        const char* exe_name;
        std::vector<fs::path> sources;
    };
    const std::vector<DosToolSpec> specs = {
        {"CKEXE2BN.EXE", {core_dir / "mz_core.c", core_dir / "exe2bin_core.c", dos_dir / "ckexe2bn.c"}},
        {"CKEXEFIX.EXE", {core_dir / "mz_core.c", core_dir / "exefix_core.c", dos_dir / "ckexefix.c"}},
        {"CKCONV.EXE",   {core_dir / "mz_core.c", core_dir / "convert_core.c", dos_dir / "ckconv.c"}},
        {"CKCMPRSS.EXE", {core_dir / "compress_core.c", dos_dir / "ckcmprss.c"}},
        {"CKBLDMSG.EXE", {core_dir / "msgtools_core.c", dos_dir / "ckbldmsg.c"}},
        {"CKNOSRV.EXE",  {core_dir / "msgtools_core.c", dos_dir / "cknosrv.c"}},
        {"CKLIB.EXE",    {core_dir / "omf_core.c", core_dir / "lib_core.c", dos_dir / "cklib.c"}},
        {"CKLINK.EXE",   {core_dir / "omf_core.c", core_dir / "link_core.c", dos_dir / "cklink.c"}},
        {"CKNMAKE.EXE",  {core_dir / "nmake_core.c", dos_dir / "cknmake.c"}},
        {"CKMKIMG.EXE",  {core_dir / "diskimg_core.c", dos_dir / "ckmkimg.c"}},
    };

    std::vector<fs::path> built;
    for (const auto& sp : specs) {
        std::string obj_list;
        for (std::size_t i = 0; i < sp.sources.size(); ++i) {
            const std::string obj_name = upper_ascii(sp.sources[i].stem().string()) + ".OBJ";
            if (run_process({ckcl.string(), "-c", "-AS", "-Os", "-Zp", "-DCK_DOS_LEAN",
                             "-I" + inc_dir.string(), "-I" + dos_dir.string(),
                             "-Fo" + obj_name, sp.sources[i].string()}, obj_dir) != 0) {
                throw std::runtime_error(std::string("failed compiling 16-bit ") + sp.exe_name);
            }
            if (i > 0) obj_list += "+";
            obj_list += find_ci(obj_dir, obj_name).filename().string();
        }
        if (run_process({cklink.string(),
                         obj_list + "," + sp.exe_name + ",nul," + built_slibc.filename().string() + ";"},
                        obj_dir) != 0) {
            throw std::runtime_error(std::string("failed linking 16-bit ") + sp.exe_name);
        }
        const fs::path dst = out_dir / sp.exe_name;
        fs::copy_file(find_ci(obj_dir, sp.exe_name), dst, fs::copy_options::overwrite_existing);
        built.push_back(dst);
        std::cout << "Built 16-bit DOS tool " << sp.exe_name << " (" << fs::file_size(dst) << " bytes)\n";
    }
    return built;
}

// ---------------------------------------------------------------------------
// Release Media & Cross-Platform SDK Packaging (Option B — All 5 Formats)
// ---------------------------------------------------------------------------

struct MemOrFileItem {
    std::string target_path;
    fs::path host_path;
    std::string inline_bytes;
    bool use_inline = false;
    std::uint8_t attr = 0x20u;
    std::uint32_t unix_mode = 0100644u;
};

int item_stream_read(void* ctx, ck_u32 offset, ck_u8* buf, ck_u32 len) {
    const auto* it = static_cast<const MemOrFileItem*>(ctx);
    if (it->use_inline) {
        if (offset + len > it->inline_bytes.size()) return -1;
        std::memcpy(buf, it->inline_bytes.data() + offset, len);
        return 0;
    }
    std::ifstream in(it->host_path, std::ios::binary);
    if (!in) return -1;
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    in.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
    return in.gcount() == static_cast<std::streamsize>(len) ? 0 : -1;
}

int ofstream_write_cb(void* ctx, const ck_u8* buf, ck_u32 len) {
    auto* out = static_cast<std::ofstream*>(ctx);
    out->write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(len));
    return out->good() ? 0 : -1;
}

std::uint32_t item_size(const MemOrFileItem& it) {
    if (it.use_inline) return static_cast<std::uint32_t>(it.inline_bytes.size());
    return static_cast<std::uint32_t>(fs::file_size(it.host_path));
}

std::vector<ck_img_file> bind_img_files(const std::vector<MemOrFileItem>& items) {
    std::vector<ck_img_file> out(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::memset(&out[i], 0, sizeof(ck_img_file));
        out[i].dos_path = items[i].target_path.c_str();
        out[i].attr = items[i].attr;
        out[i].size = item_size(items[i]);
        if (items[i].use_inline) {
            out[i].mem_data = reinterpret_cast<const ck_u8*>(items[i].inline_bytes.data());
        } else {
            out[i].read_ctx = const_cast<MemOrFileItem*>(&items[i]);
            out[i].read_fn = item_stream_read;
        }
    }
    return out;
}

std::vector<ck_zip_item> bind_zip_items(const std::vector<MemOrFileItem>& items) {
    std::vector<ck_zip_item> out(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::memset(&out[i], 0, sizeof(ck_zip_item));
        out[i].archive_path = items[i].target_path.c_str();
        out[i].unix_mode = items[i].unix_mode;
        out[i].size = item_size(items[i]);
        if (items[i].use_inline) {
            out[i].mem_data = reinterpret_cast<const ck_u8*>(items[i].inline_bytes.data());
        } else {
            out[i].read_ctx = const_cast<MemOrFileItem*>(&items[i]);
            out[i].read_fn = item_stream_read;
        }
    }
    return out;
}

void collect_built_dos_payload(const fs::path& stage,
                               std::vector<std::pair<std::string, fs::path>>& cmd_files,
                               std::vector<std::pair<std::string, fs::path>>& dev_files) {
    const std::set<std::string> skip_dups = {"CHKDSK.EXE", "RECOVER.EXE", "MSBOOT.EXE", "MSLOAD.EXE"};
    for (const auto& e : fs::recursive_directory_iterator(stage / "CMD")) {
        if (!e.is_regular_file()) continue;
        const std::string fname = upper_ascii(e.path().filename().string());
        const std::string ext = upper_ascii(e.path().extension().string());
        if (fname == "COMMAND.COM" || skip_dups.count(fname) > 0) continue;
        if (ext == ".COM" || ext == ".EXE") {
            cmd_files.emplace_back(fname, e.path());
        }
    }
    for (const char* grp : {"DEV", "MEMM"}) {
        const fs::path g = stage / grp;
        if (!fs::is_directory(g)) continue;
        for (const auto& e : fs::recursive_directory_iterator(g)) {
            if (!e.is_regular_file()) continue;
            const std::string fname = upper_ascii(e.path().filename().string());
            const std::string ext = upper_ascii(e.path().extension().string());
            if (ext == ".SYS" || ext == ".CPI") {
                dev_files.emplace_back(fname, e.path());
            } else if (ext == ".EXE" || ext == ".COM") {
                cmd_files.emplace_back(fname, e.path());
            }
        }
    }

    auto dedup_sort = [](std::vector<std::pair<std::string, fs::path>>& vec) {
        std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        vec.erase(std::unique(vec.begin(), vec.end(),
                              [](const auto& a, const auto& b) { return a.first == b.first; }),
                  vec.end());
    };
    dedup_sort(cmd_files);
    dedup_sort(dev_files);
}

std::string default_platform_tag() {
#if defined(_WIN32)
    const char* os = "windows";
#elif defined(__APPLE__)
    const char* os = "macos";
#else
    const char* os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* arch = "arm64";
#else
    const char* arch = "x64";
#endif
    return std::string(os) + "-" + arch;
}

void build_sdk_zip(const fs::path& repo_root,
                   const fs::path& tool_dir,
                   const fs::path& stage,
                   const std::vector<fs::path>& dos_tools,
                   const fs::path& out_zip,
                   const std::string& version,
                   const std::string& platform) {
    const std::string prefix = "ck-dos-sdk-" + version + "-" + platform + "/";
    std::vector<MemOrFileItem> items;

    const std::vector<std::string> host_tools = {
        "ckmzdump", "ckomfdump", "cklibdump", "ckexe2bin", "ckconvert",
        "ckexefix", "ckmenubld", "ckasc2hlp", "ckcompress", "cklib",
        "ckdbof", "ckbuildidx", "cknosrvbld", "ckbuildmsg", "ckmasm",
        "ckcl", "cklink", "cknmake", "ckmkimg", "ckdist"};

    for (const std::string& t : host_tools) {
        const fs::path bin = exe_path(tool_dir, t);
        if (fs::is_regular_file(bin)) {
            items.push_back({prefix + "bin/" + bin.filename().string(), bin, {}, false, 0x20u, 0100755u});
        }
    }

    for (const fs::path& dt : dos_tools) {
        items.push_back({prefix + "dos-bin/" + upper_ascii(dt.filename().string()), dt, {}, false, 0x20u, 0100644u});
    }

    const std::vector<std::pair<std::string, fs::path>> dos_libs = {
        {"SLIBC.LIB", find_ci(stage / "TOOLS" / "BLD" / "LIB", "SLIBC.LIB")},
        {"SLIBCE.LIB", find_ci(stage / "TOOLS" / "BLD" / "LIB", "SLIBCE.LIB")},
        {"SETARGV.OBJ", find_ci(stage / "TOOLS" / "BLD" / "LIB", "SETARGV.OBJ")},
        {"BINMODE.OBJ", find_ci(stage / "TOOLS" / "BLD" / "LIB", "BINMODE.OBJ")},
        {"COMSUBS.LIB", find_ci(stage / "INC", "COMSUBS.LIB")},
        {"CASSFAR.LIB", find_ci(stage / "SELECT", "CASSFAR.LIB")},
    };
    for (const auto& [name, p] : dos_libs) {
        if (fs::is_regular_file(p)) {
            items.push_back({prefix + "dos-lib/" + name, p, {}, false, 0x20u, 0100644u});
        }
    }

    for (const char* sub : {"runtime", "include"}) {
        const fs::path root = repo_root / sub;
        if (!fs::is_directory(root)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::recursive_directory_iterator(root)) {
            if (e.is_regular_file()) files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) {
            items.push_back({prefix + fs::relative(f, repo_root).generic_string(), f, {}, false, 0x20u, 0100644u});
        }
    }

    for (const char* doc : {"LICENSE", "NOTICE", "README.md", "VERSION"}) {
        const fs::path p = repo_root / doc;
        if (fs::is_regular_file(p)) {
            items.push_back({prefix + doc, p, {}, false, 0x20u, 0100644u});
        }
    }

    const auto zitems = bind_zip_items(items);
    std::ofstream out(out_zip, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create SDK zip: " + out_zip.string());
    ck_error err{};
    if (ck_diskimg_write_zip(zitems.data(), zitems.size(), ofstream_write_cb, &out, &err) != 0) {
        throw std::runtime_error(std::string("failed writing SDK zip: ") + err.message);
    }
}

struct OsMediaSpec {
    std::string prefix;
    std::string floppy_1440_label;
    std::string floppy_720_d1_label;
    std::string floppy_720_d2_label;
    std::string hdd_label;
    std::string iso_volume_id;
    std::string format_vol_arg;
    std::string auto_floppy;
    std::string auto_hdd;
    std::string install_bat;
    std::string readme_txt;
    std::string disk2_readme;
    bool is_dos5 = false;
};

OsMediaSpec make_dos4_media_spec(const std::string& version) {
    OsMediaSpec s;
    s.prefix = "ckdos-4.00";
    s.floppy_1440_label = "CKDOS 4.00";
    s.floppy_720_d1_label = "CKDOS DISK1";
    s.floppy_720_d2_label = "CKDOS DISK2";
    s.hdd_label = "CKDOS 4.00";
    s.iso_volume_id = "CKDOS_400";
    s.format_vol_arg = "CKDOS400";
    s.auto_floppy =
        "@ECHO OFF\r\n"
        "PATH A:\\;A:\\DOS\r\n"
        "PROMPT $P$G\r\n"
        "ECHO.\r\n"
        "ECHO MS-DOS 4.00 (CK-DOS Clean-Room Release " + version + ")\r\n"
        "ECHO   Type INSTALL to install MS-DOS 4.00 onto hard drive C:\r\n"
        "ECHO   (Run FDISK first if hard drive C: is unpartitioned.)\r\n"
        "ECHO.\r\n";
    s.auto_hdd =
        "@ECHO OFF\r\n"
        "PATH C:\\DOS;C:\\DEVTOOLS\r\n"
        "PROMPT $P$G\r\n"
        "VER\r\n";
    s.install_bat =
        "@ECHO OFF\r\n"
        "A:\r\n"
        "CD \\\r\n"
        "ECHO Installing MS-DOS 4.00 to C:\\ ...\r\n"
        "A:\\DOS\\SYS C:\r\n"
        "IF ERRORLEVEL 1 GOTO NEEDFMT\r\n"
        "GOTO COPYFILES\r\n"
        ":NEEDFMT\r\n"
        "ECHO Formatting C: with system files ...\r\n"
        "A:\\DOS\\FORMAT C: /S /V:CKDOS400\r\n"
        "IF ERRORLEVEL 1 GOTO FAIL\r\n"
        ":COPYFILES\r\n"
        "COPY A:\\COMMAND.COM C:\\COMMAND.COM >NUL\r\n"
        "MD C:\\DOS\r\n"
        "MD C:\\DEV\r\n"
        "A:\\DOS\\XCOPY A:\\DOS\\*.* C:\\DOS\\ /E >NUL\r\n"
        "A:\\DOS\\XCOPY A:\\DEV\\*.* C:\\DEV\\ /E >NUL\r\n"
        "COPY A:\\HDCFG.SYS C:\\CONFIG.SYS >NUL\r\n"
        "COPY A:\\HDAUTO.BAT C:\\AUTOEXEC.BAT >NUL\r\n"
        "ECHO Installation complete! Remove floppy disk and reboot from C:.\r\n"
        "GOTO END\r\n"
        ":FAIL\r\n"
        "ECHO Installation stopped. Run FDISK first if C: has no active DOS partition.\r\n"
        ":END\r\n";
    s.readme_txt =
        "CK-DOS Base Release " + version + " (MS-DOS 4.00 Clean-Room Build)\r\n"
        "=============================================================\r\n\r\n"
        "Built 100% from source with clean-room native tools:\r\n"
        "  ckcl, ckmasm, cklink, cklib, cknmake, ckmkimg, ckdist\r\n\r\n"
        "Included Release Media:\r\n"
        "  - ckdos-4.00-floppy-1440k.img : 3.5\" 1.44M Boot + Install Floppy\r\n"
        "  - ckdos-4.00-floppy-720k-disk1.img / disk2.img : 2x720K XT/PS2 Set\r\n"
        "  - ckdos-4.00-hdd-32m.vhd      : Ready-to-Boot 32MB Hard Disk (+DEVTOOLS)\r\n"
        "  - ckdos-4.00-boot.iso         : El-Torito Bootable CD-ROM Image\r\n"
        "  - ckdos-4.00-files.zip        : File Archive for DOSBox-X / CF / SD\r\n";
    s.disk2_readme =
        "CK-DOS 4.00 Disk 2 (Utilities & Extended Drivers)\r\n"
        "Copy DOS\\*.* and DEV\\*.* to C:\\DOS and C:\\DEV.\r\n";
    s.is_dos5 = false;
    return s;
}

OsMediaSpec make_dos5_media_spec(const std::string& version) {
    OsMediaSpec s;
    s.prefix = "ckdos-5.00";
    s.floppy_1440_label = "CKDOS500";
    s.floppy_720_d1_label = "CKDOS5 DSK1";
    s.floppy_720_d2_label = "CKDOS5 DSK2";
    s.hdd_label = "CKDOS500";
    s.iso_volume_id = "CKDOS_500";
    s.format_vol_arg = "CKDOS500";
    s.auto_floppy =
        "@ECHO OFF\r\n"
        "PATH A:\\;A:\\DOS\r\n"
        "PROMPT $P$G\r\n"
        "ECHO.\r\n"
        "ECHO CK-DOS 5.00 (Clean-Room Release " + version + ")\r\n"
        "ECHO   Type INSTALL to install CK-DOS 5.00 onto hard drive C:\r\n"
        "ECHO   (Run FDISK first if hard drive C: is unpartitioned.)\r\n"
        "ECHO.\r\n";
    s.auto_hdd =
        "@ECHO OFF\r\n"
        "PATH C:\\DOS;C:\\DEVTOOLS\r\n"
        "PROMPT $P$G\r\n"
        "ECHO CK-DOS 5.00 (Clean-Room Release " + version + ")\r\n"
        "VER\r\n";
    s.install_bat =
        "@ECHO OFF\r\n"
        "A:\r\n"
        "CD \\\r\n"
        "ECHO Installing CK-DOS 5.00 to C:\\ ...\r\n"
        "A:\\DOS\\SYS C:\r\n"
        "IF ERRORLEVEL 1 GOTO NEEDFMT\r\n"
        "GOTO COPYFILES\r\n"
        ":NEEDFMT\r\n"
        "ECHO Formatting C: with system files ...\r\n"
        "A:\\DOS\\FORMAT C: /S /V:CKDOS500\r\n"
        "IF ERRORLEVEL 1 GOTO FAIL\r\n"
        ":COPYFILES\r\n"
        "COPY A:\\COMMAND.COM C:\\COMMAND.COM >NUL\r\n"
        "MD C:\\DOS\r\n"
        "MD C:\\DEV\r\n"
        "A:\\DOS\\XCOPY A:\\DOS\\*.* C:\\DOS\\ /E >NUL\r\n"
        "A:\\DOS\\XCOPY A:\\DEV\\*.* C:\\DEV\\ /E >NUL\r\n"
        "COPY A:\\HDCFG.SYS C:\\CONFIG.SYS >NUL\r\n"
        "COPY A:\\HDAUTO.BAT C:\\AUTOEXEC.BAT >NUL\r\n"
        "ECHO Installation complete! Remove floppy disk and reboot from C:.\r\n"
        "GOTO END\r\n"
        ":FAIL\r\n"
        "ECHO Installation stopped. Run FDISK first if C: has no active DOS partition.\r\n"
        ":END\r\n";
    s.readme_txt =
        "CK-DOS Base Release " + version + " (CK-DOS 5.00 Clean-Room Build)\r\n"
        "==============================================================\r\n\r\n"
        "Independent MIT-licensed CK-DOS 5.00 distribution built 100% from source\r\n"
        "with clean-room native tools (ckcl, ckmasm, cklink, cklib, cknmake, ckmkimg).\r\n"
        "Includes full-screen DOSUI utilities EDIT.COM (CKEDIT.COM) and HELP.COM.\r\n\r\n"
        "Included Release Media:\r\n"
        "  - ckdos-5.00-floppy-1440k.img : 3.5\" 1.44M Boot + Install Floppy (CKDOS500)\r\n"
        "  - ckdos-5.00-floppy-720k-disk1.img / disk2.img : 2x720K XT/PS2 Set\r\n"
        "  - ckdos-5.00-hdd-32m.vhd      : Ready-to-Boot 32MB Hard Disk (+DEVTOOLS)\r\n"
        "  - ckdos-5.00-boot.iso         : El-Torito Bootable CD-ROM Image\r\n"
        "  - ckdos-5.00-files.zip        : File Archive for DOSBox-X / CF / SD\r\n";
    s.disk2_readme =
        "CK-DOS 5.00 Disk 2 (Utilities & Extended Drivers)\r\n"
        "Copy DOS\\*.* and DEV\\*.* to C:\\DOS and C:\\DEV.\r\n";
    s.is_dos5 = true;
    return s;
}

void build_os_media_set(const fs::path& stage,
                        const std::vector<fs::path>& dos_tools,
                        const fs::path& dist_dir,
                        const OsMediaSpec& spec) {
    fs::create_directories(dist_dir);

    const fs::path msboot_path = find_ci(stage / "BOOT", "MSBOOT.BIN");
    ck_u8 boot512[512]{};
    {
        std::ifstream b_in(msboot_path, std::ios::binary);
        b_in.seekg(0x7C00, std::ios::beg);
        b_in.read(reinterpret_cast<char*>(boot512), 512);
        if (b_in.gcount() != 512) throw std::runtime_error("invalid MSBOOT.BIN");
    }

    const fs::path io_sys = find_ci(stage / "BIOS", "IO.SYS");
    const fs::path msdos_sys = find_ci(stage / "DOS", "MSDOS.SYS");
    const fs::path command_com = find_ci(stage / "CMD" / "COMMAND", "COMMAND.COM");

    std::vector<std::pair<std::string, fs::path>> cmd_files;
    std::vector<std::pair<std::string, fs::path>> dev_files;
    collect_built_dos_payload(stage, cmd_files, dev_files);

    const std::string cfg_floppy = "FILES=30\r\nBUFFERS=20\r\n";
    const std::string cfg_hdd = "FILES=40\r\nBUFFERS=25\r\nDEVICE=C:\\DEV\\ANSI.SYS\r\n";

    // 1. Build 1.44 MB All-in-One Boot + Install Floppy
    const fs::path img_1440 = dist_dir / (spec.prefix + "-floppy-1440k.img");
    {
        std::vector<MemOrFileItem> items = {
            {"IO.SYS", io_sys, {}, false, 0x07u, 0100644u},
            {"MSDOS.SYS", msdos_sys, {}, false, 0x07u, 0100644u},
            {"COMMAND.COM", command_com, {}, false, 0x20u, 0100644u},
            {"CONFIG.SYS", {}, cfg_floppy, true, 0x20u, 0100644u},
            {"AUTOEXEC.BAT", {}, spec.auto_floppy, true, 0x20u, 0100644u},
            {"INSTALL.BAT", {}, spec.install_bat, true, 0x20u, 0100644u},
            {"HDCFG.SYS", {}, cfg_hdd, true, 0x20u, 0100644u},
            {"HDAUTO.BAT", {}, spec.auto_hdd, true, 0x20u, 0100644u},
            {"README.TXT", {}, spec.readme_txt, true, 0x20u, 0100644u},
        };
        for (const auto& [name, p] : cmd_files) items.push_back({"DOS/" + name, p, {}, false, 0x20u, 0100644u});
        for (const auto& [name, p] : dev_files) items.push_back({"DEV/" + name, p, {}, false, 0x20u, 0100644u});

        ck_fat_geom g{};
        ck_diskimg_floppy_geom(CK_FLOPPY_1440K, spec.floppy_1440_label.c_str(), &g);
        const auto bound = bind_img_files(items);
        std::ofstream out(img_1440, std::ios::binary | std::ios::trunc);
        ck_error err{};
        if (ck_diskimg_write_fat_volume(&g, boot512, bound.data(), bound.size(), ofstream_write_cb, &out, &err) != 0) {
            throw std::runtime_error("failed building " + img_1440.filename().string() + ": " + err.message);
        }
        out.close();
        std::cout << "Built " << img_1440.filename().string() << " (" << fs::file_size(img_1440) << " bytes)\n";
    }

    // 2. Build 2x 720 KB Vintage XT/PS2 Floppy Set (Disk 1: Boot+Setup, Disk 2: Utilities+EMS)
    std::set<std::string> d1_cmds = {
        "FDISK.EXE", "FORMAT.COM", "SYS.COM", "XCOPY.EXE", "CHKDSK.COM",
        "DISKCOPY.COM", "DISKCOMP.COM", "ATTRIB.EXE", "MEM.EXE", "EDLIN.COM",
        "MORE.COM", "LABEL.COM", "MODE.COM", "KEYB.COM", "DEBUG.COM",
        "FC.EXE", "FIND.EXE", "SORT.EXE", "TREE.COM"};
    if (spec.is_dos5) {
        d1_cmds.insert("EDIT.COM");
        d1_cmds.insert("HELP.COM");
    }
    const std::set<std::string> d1_devs = {
        "ANSI.SYS", "COUNTRY.SYS", "DISPLAY.SYS", "DRIVER.SYS",
        "KEYBOARD.SYS", "RAMDRIVE.SYS", "VDISK.SYS"};

    const fs::path img_720_1 = dist_dir / (spec.prefix + "-floppy-720k-disk1.img");
    const fs::path img_720_2 = dist_dir / (spec.prefix + "-floppy-720k-disk2.img");
    {
        std::vector<MemOrFileItem> d1 = {
            {"IO.SYS", io_sys, {}, false, 0x07u, 0100644u},
            {"MSDOS.SYS", msdos_sys, {}, false, 0x07u, 0100644u},
            {"COMMAND.COM", command_com, {}, false, 0x20u, 0100644u},
            {"CONFIG.SYS", {}, cfg_floppy, true, 0x20u, 0100644u},
            {"AUTOEXEC.BAT", {}, spec.auto_floppy, true, 0x20u, 0100644u},
            {"INSTALL.BAT", {}, spec.install_bat, true, 0x20u, 0100644u},
            {"HDCFG.SYS", {}, cfg_hdd, true, 0x20u, 0100644u},
            {"HDAUTO.BAT", {}, spec.auto_hdd, true, 0x20u, 0100644u},
            {"README.TXT", {}, spec.readme_txt, true, 0x20u, 0100644u},
        };
        std::vector<MemOrFileItem> d2 = {
            {"README.TXT", {}, spec.disk2_readme, true, 0x20u, 0100644u},
        };
        for (const auto& [name, p] : cmd_files) {
            (d1_cmds.count(name) ? d1 : d2).push_back({"DOS/" + name, p, {}, false, 0x20u, 0100644u});
        }
        for (const auto& [name, p] : dev_files) {
            (d1_devs.count(name) ? d1 : d2).push_back({"DEV/" + name, p, {}, false, 0x20u, 0100644u});
        }

        ck_fat_geom g1{}, g2{};
        ck_diskimg_floppy_geom(CK_FLOPPY_720K, spec.floppy_720_d1_label.c_str(), &g1);
        ck_diskimg_floppy_geom(CK_FLOPPY_720K, spec.floppy_720_d2_label.c_str(), &g2);
        const auto b1 = bind_img_files(d1);
        const auto b2 = bind_img_files(d2);
        ck_error err{};
        {
            std::ofstream out1(img_720_1, std::ios::binary | std::ios::trunc);
            if (ck_diskimg_write_fat_volume(&g1, boot512, b1.data(), b1.size(), ofstream_write_cb, &out1, &err) != 0) {
                throw std::runtime_error("failed building " + img_720_1.filename().string() + ": " + err.message);
            }
        }
        {
            std::ofstream out2(img_720_2, std::ios::binary | std::ios::trunc);
            if (ck_diskimg_write_fat_volume(&g2, nullptr, b2.data(), b2.size(), ofstream_write_cb, &out2, &err) != 0) {
                throw std::runtime_error("failed building " + img_720_2.filename().string() + ": " + err.message);
            }
        }
        std::cout << "Built " << spec.prefix << " 2x720k floppy set ("
                  << fs::file_size(img_720_1) << " + " << fs::file_size(img_720_2) << " bytes)\n";
    }

    // Shared full HDD / ISO / ZIP item list (includes C:\DEVTOOLS self-hosting 16-bit SDK)
    std::vector<MemOrFileItem> full_hdd_items = {
        {"IO.SYS", io_sys, {}, false, 0x07u, 0100644u},
        {"MSDOS.SYS", msdos_sys, {}, false, 0x07u, 0100644u},
        {"COMMAND.COM", command_com, {}, false, 0x20u, 0100644u},
        {"CONFIG.SYS", {}, cfg_hdd, true, 0x20u, 0100644u},
        {"AUTOEXEC.BAT", {}, spec.auto_hdd, true, 0x20u, 0100644u},
        {"README.TXT", {}, spec.readme_txt, true, 0x20u, 0100644u},
    };
    for (const auto& [name, p] : cmd_files) full_hdd_items.push_back({"DOS/" + name, p, {}, false, 0x20u, 0100644u});
    for (const auto& [name, p] : dev_files) full_hdd_items.push_back({"DEV/" + name, p, {}, false, 0x20u, 0100644u});
    for (const fs::path& dt : dos_tools) {
        full_hdd_items.push_back({"DEVTOOLS/" + upper_ascii(dt.filename().string()), dt, {}, false, 0x20u, 0100644u});
    }
    for (const auto& [lname, lp] : std::vector<std::pair<std::string, fs::path>>{
             {"SLIBC.LIB", find_ci(stage / "TOOLS" / "BLD" / "LIB", "SLIBC.LIB")},
             {"COMSUBS.LIB", find_ci(stage / "INC", "COMSUBS.LIB")},
             {"CASSFAR.LIB", find_ci(stage / "SELECT", "CASSFAR.LIB")},
         }) {
        if (fs::is_regular_file(lp)) {
            full_hdd_items.push_back({"DEVTOOLS/LIB/" + lname, lp, {}, false, 0x20u, 0100644u});
        }
    }

    // 3. Build Pre-Installed 32 MB Fixed VHD Bootable Hard Disk (64 cyl * 16 heads * 63 spt)
    const fs::path hdd_vhd = dist_dir / (spec.prefix + "-hdd-32m.vhd");
    {
        const auto bound = bind_img_files(full_hdd_items);
        std::ofstream out(hdd_vhd, std::ios::binary | std::ios::trunc);
        ck_error err{};
        if (ck_diskimg_write_hdd_image(64u, 16u, 63u, spec.hdd_label.c_str(), boot512,
                                       bound.data(), bound.size(), 1,
                                       ofstream_write_cb, &out, &err) != 0) {
            throw std::runtime_error("failed building " + hdd_vhd.filename().string() + ": " + err.message);
        }
        out.close();
        std::cout << "Built " << hdd_vhd.filename().string() << " (" << fs::file_size(hdd_vhd) << " bytes)\n";
    }

    // 4. Build El-Torito Bootable CD-ROM ISO-9660
    const fs::path boot_iso = dist_dir / (spec.prefix + "-boot.iso");
    {
        std::vector<MemOrFileItem> boot_items = {{"BOOT1440.IMG", img_1440, {}, false, 0x20u, 0100644u}};
        const auto boot_vec = bind_img_files(boot_items);
        const auto bound = bind_img_files(full_hdd_items);
        std::ofstream out(boot_iso, std::ios::binary | std::ios::trunc);
        ck_error err{};
        if (ck_diskimg_write_eltorito_iso(spec.iso_volume_id.c_str(), &boot_vec[0], bound.data(), bound.size(),
                                          ofstream_write_cb, &out, &err) != 0) {
            throw std::runtime_error("failed building " + boot_iso.filename().string() + ": " + err.message);
        }
        out.close();
        std::cout << "Built " << boot_iso.filename().string() << " (" << fs::file_size(boot_iso) << " bytes)\n";
    }

    // 5. Build Loose Files Archive (<prefix>-files.zip) for DOSBox-X / XT-IDE CF / SD
    const fs::path files_zip = dist_dir / (spec.prefix + "-files.zip");
    {
        std::vector<MemOrFileItem> z_items = full_hdd_items;
        z_items.push_back({"BOOT/MSBOOT.BIN", msboot_path, {}, false, 0x20u, 0100644u});
        const auto bound_z = bind_zip_items(z_items);
        std::ofstream out(files_zip, std::ios::binary | std::ios::trunc);
        ck_error err{};
        if (ck_diskimg_write_zip(bound_z.data(), bound_z.size(), ofstream_write_cb, &out, &err) != 0) {
            throw std::runtime_error("failed building " + files_zip.filename().string() + ": " + err.message);
        }
        out.close();
        std::cout << "Built " << files_zip.filename().string() << " (" << fs::file_size(files_zip) << " bytes)\n";
    }
}

std::vector<std::pair<std::string, std::pair<std::string, std::uint64_t>>> write_sha256sums(const fs::path& dist_dir) {
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dist_dir)) {
        if (e.is_regular_file() && e.path().filename() != "SHA256SUMS") {
            files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end(),
              [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });

    std::ostringstream oss;
    std::vector<std::pair<std::string, std::pair<std::string, std::uint64_t>>> records;
    for (const fs::path& f : files) {
        const std::string h = sha256_file(f);
        const std::uint64_t sz = fs::file_size(f);
        const std::string name = f.filename().string();
        oss << h << "  " << name << "\n";
        records.push_back({name, {h, sz}});
    }
    write_text_file(dist_dir / "SHA256SUMS", oss.str());
    return records;
}

void write_pages_site(const fs::path& pages_dir,
                      const fs::path& dist_dir,
                      const std::string& version,
                      const std::vector<std::pair<std::string, std::pair<std::string, std::uint64_t>>>& sums) {
    fs::create_directories(pages_dir);
    for (const char* pub_asset : {
             "ckdos-5.00-floppy-1440k.img", "ckdos-5.00-files.zip",
             "ckdos-4.00-floppy-1440k.img", "ckdos-4.00-files.zip",
             "SHA256SUMS"}) {
        const fs::path src = dist_dir / pub_asset;
        if (fs::is_regular_file(src)) {
            fs::copy_file(src, pages_dir / pub_asset, fs::copy_options::overwrite_existing);
        }
    }

    const bool has_dos5 = fs::is_regular_file(dist_dir / "ckdos-5.00-floppy-1440k.img");
    const bool has_dos4 = fs::is_regular_file(dist_dir / "ckdos-4.00-floppy-1440k.img");

    std::ostringstream html;
    html << "<!doctype html>\n<html lang=\"en\">\n<head>\n"
         << "  <meta charset=\"utf-8\">\n"
         << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
         << "  <title>CK-DOS Base " << version << " — Clean-Room CK-DOS 5.0 &amp; 4.0 Builds &amp; SDK</title>\n"
         << "  <style>\n"
         << "    :root{color-scheme:light dark;--bg:#0e1116;--card:#161b22;--fg:#e6edf3;--muted:#8b949e;--line:#30363d;--acc:#58a6ff}\n"
         << "    @media (prefers-color-scheme:light){:root{--bg:#f6f8fa;--card:#ffffff;--fg:#1f2328;--muted:#59636e;--line:#d0d7de;--acc:#0969da}}\n"
         << "    body{font:15px/1.6 system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg);max-width:980px;margin:0 auto;padding:2rem 1.5rem}\n"
         << "    h1,h2{letter-spacing:-0.02em} code,pre{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.9em}\n"
         << "    .card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:1.25rem 1.5rem;margin:1.25rem 0}\n"
         << "    table{width:100%;border-collapse:collapse} th,td{text-align:left;padding:.55rem .6rem;border-bottom:1px solid var(--line)}\n"
         << "    th{font-size:.8rem;text-transform:uppercase;color:var(--muted)} a{color:var(--acc);text-decoration:none} a:hover{text-decoration:underline}\n"
         << "    .pill{display:inline-block;padding:.2rem .6rem;border:1px solid var(--line);border-radius:999px;font-size:.82rem;color:var(--muted)}\n"
         << "  </style>\n</head>\n<body>\n"
         << "  <h1>CK-DOS Base <span class=\"pill\">" << version << "</span></h1>\n"
         << "  <p>100% clean-room native C/C++20 cross-toolchain (<code>ckcl</code>, <code>ckmasm</code>, <code>cklink</code>, <code>cklib</code>, <code>cknmake</code>, <code>ckmkimg</code>) building both <strong>CK-DOS 5.0</strong> (<code>os/ck-dos-5</code>) and the frozen 654-hash <strong>CK-DOS 4.0</strong> reference (<code>third_party/ms-dos-4</code>), plus self-hosting 16-bit DOS tools.</p>\n"
         << "  <div class=\"card\">\n"
         << "    <h2>Direct Downloads (Latest Main Build)</h2>\n"
         << "    <ul>\n";
    if (has_dos5) {
        html << "      <li><a href=\"ckdos-5.00-floppy-1440k.img\"><code>ckdos-5.00-floppy-1440k.img</code></a> — CK-DOS 5.0 3.5&quot; 1.44 MB Boot &amp; Install Diskette (<code>CKDOS500</code>)</li>\n"
             << "      <li><a href=\"ckdos-5.00-files.zip\"><code>ckdos-5.00-files.zip</code></a> — CK-DOS 5.0 + 16-bit <code>DEVTOOLS</code> file archive (DOSBox-X / XT-IDE CF / SD)</li>\n";
    }
    if (has_dos4) {
        html << "      <li><a href=\"ckdos-4.00-floppy-1440k.img\"><code>ckdos-4.00-floppy-1440k.img</code></a> — CK-DOS 4.0 (MS-DOS 4.00 reference) 3.5&quot; 1.44 MB Boot &amp; Install Diskette</li>\n"
             << "      <li><a href=\"ckdos-4.00-files.zip\"><code>ckdos-4.00-files.zip</code></a> — CK-DOS 4.0 + 16-bit <code>DEVTOOLS</code> file archive</li>\n";
    }
    html << "      <li><a href=\"SHA256SUMS\"><code>SHA256SUMS</code></a> — Full release bundle SHA-256 checksums</li>\n"
         << "    </ul>\n"
         << "    <p>Full tagged releases (including 32 MB Fixed VHD hard disks, El-Torito bootable CD-ROM ISOs, 2&times;720 KB XT floppy sets, and Linux/macOS/Windows cross-SDK archives) are available under the project <strong>Releases</strong> tab.</p>\n"
         << "  </div>\n"
         << "  <div class=\"card\">\n"
         << "    <h2>Release Artifact Checksums</h2>\n"
         << "    <table>\n      <thead><tr><th>Artifact</th><th>Size (Bytes)</th><th>SHA-256</th></tr></thead>\n      <tbody>\n";
    for (const auto& [name, info] : sums) {
        html << "        <tr><td><code>" << name << "</code></td><td>" << info.second
             << "</td><td><code>" << info.first << "</code></td></tr>\n";
    }
    html << "      </tbody>\n    </table>\n  </div>\n"
         << "  <div class=\"card\">\n"
         << "    <h2>Quick Boot in QEMU</h2>\n"
         << "    <pre>qemu-system-i386 -fda ckdos-5.00-floppy-1440k.img -boot a\n"
         << "qemu-system-i386 -drive file=ckdos-5.00-hdd-32m.vhd,format=vpc -boot c\n"
         << "qemu-system-i386 -cdrom ckdos-5.00-boot.iso -boot d\n"
         << "qemu-system-i386 -fda ckdos-4.00-floppy-1440k.img -boot a</pre>\n"
         << "  </div>\n</body>\n</html>\n";

    write_text_file(pages_dir / "index.html", html.str());
}

void verify_boot_media_with_qemu(const fs::path& dist_dir,
                                 const std::string& qemu_bin,
                                 OsTarget os_target) {
#if defined(_WIN32)
    (void)dist_dir;
    (void)qemu_bin;
    (void)os_target;
    throw std::runtime_error("verify-boot is supported on POSIX hosts");
#else
    ScratchDir scratch("ckdist-qemu");

    auto check_one = [&](const std::string& label,
                         const std::vector<std::string>& media_args,
                         const std::vector<std::string>& expected_tokens) {
        const fs::path vga_file = scratch.path() / ("vga_" + label + ".bin");
        int in_pipe[2]{-1, -1};
        if (pipe(in_pipe) != 0) throw std::runtime_error("pipe() failed");

        std::vector<std::string> cmd = {
            qemu_bin, "-snapshot", "-m", "4M", "-display", "none", "-serial", "none",
            "-parallel", "none", "-monitor", "stdio"};
        cmd.insert(cmd.end(), media_args.begin(), media_args.end());

        const pid_t pid = fork();
        if (pid < 0) {
            close(in_pipe[0]);
            close(in_pipe[1]);
            throw std::runtime_error("fork() failed for qemu");
        }
        if (pid == 0) {
            dup2(in_pipe[0], STDIN_FILENO);
            close(in_pipe[0]);
            close(in_pipe[1]);
            if (FILE* f = std::fopen("/dev/null", "wb")) {
                dup2(fileno(f), STDOUT_FILENO);
                dup2(fileno(f), STDERR_FILENO);
                std::fclose(f);
            }
            std::vector<char*> argv_ptrs;
            for (auto& s : cmd) argv_ptrs.push_back(s.data());
            argv_ptrs.push_back(nullptr);
            execvp(cmd[0].c_str(), argv_ptrs.data());
            _exit(127);
        }
        close(in_pipe[0]);

        auto send_mon = [&](const std::string& line) {
            const ssize_t wr = write(in_pipe[1], line.data(), line.size());
            (void)wr;
        };

        bool matched = false;
        for (int attempt = 0; attempt < 80; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            std::error_code ec;
            fs::remove(vga_file, ec);
            send_mon("pmemsave 0xb8000 4000 \"" + vga_file.string() + "\"\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            if (fs::is_regular_file(vga_file, ec) && fs::file_size(vga_file, ec) == 4000u) {
                std::ifstream vin(vga_file, std::ios::binary);
                std::array<char, 4000> raw{};
                vin.read(raw.data(), 4000);
                std::string screen;
                screen.reserve(2000);
                for (std::size_t i = 0; i < 4000; i += 2) {
                    const unsigned char ch = static_cast<unsigned char>(raw[i]);
                    screen.push_back((ch >= 32 && ch < 127) ? static_cast<char>(ch) : ' ');
                }
                bool all_found = true;
                for (const std::string& tok : expected_tokens) {
                    if (screen.find(tok) == std::string::npos) {
                        all_found = false;
                        break;
                    }
                }
                if (all_found) {
                    matched = true;
                    break;
                }
            }
        }
        send_mon("quit\n");
        close(in_pipe[1]);
        int status = 0;
        waitpid(pid, &status, 0);
        if (!matched) {
            std::string desc;
            for (std::size_t i = 0; i < expected_tokens.size(); ++i) {
                if (i > 0) desc += " + ";
                desc += "'" + expected_tokens[i] + "'";
            }
            throw std::runtime_error("QEMU boot check timed out for " + label + " (expected " + desc + ")");
        }
        std::cout << "QEMU boot verified: " << label << " -> '" << expected_tokens.back() << "'\n";
    };

    if (includes_dos4(os_target)) {
        const fs::path f1440 = dist_dir / "ckdos-4.00-floppy-1440k.img";
        const fs::path f720 = dist_dir / "ckdos-4.00-floppy-720k-disk1.img";
        const fs::path vhd = dist_dir / "ckdos-4.00-hdd-32m.vhd";
        const fs::path iso = dist_dir / "ckdos-4.00-boot.iso";

        check_one("dos4-floppy-1440k", {"-fda", f1440.string(), "-boot", "a"}, {"A:\\>"});
        check_one("dos4-floppy-720k-disk1", {"-fda", f720.string(), "-boot", "a"}, {"A:\\>"});
        check_one("dos4-hdd-32m-vhd", {"-drive", "file=" + vhd.string() + ",format=vpc", "-boot", "c"}, {"MS-DOS Version 4.00", "C:\\>"});
        check_one("dos4-boot-iso", {"-cdrom", iso.string(), "-boot", "d"}, {"A:\\>"});
    }

    if (includes_dos5(os_target)) {
        const fs::path f1440 = dist_dir / "ckdos-5.00-floppy-1440k.img";
        const fs::path f720 = dist_dir / "ckdos-5.00-floppy-720k-disk1.img";
        const fs::path vhd = dist_dir / "ckdos-5.00-hdd-32m.vhd";
        const fs::path iso = dist_dir / "ckdos-5.00-boot.iso";

        check_one("dos5-floppy-1440k", {"-fda", f1440.string(), "-boot", "a"}, {"CK-DOS 5.00", "A:\\>"});
        check_one("dos5-floppy-720k-disk1", {"-fda", f720.string(), "-boot", "a"}, {"CK-DOS 5.00", "A:\\>"});
        check_one("dos5-hdd-32m-vhd", {"-drive", "file=" + vhd.string() + ",format=vpc", "-boot", "c"}, {"CK-DOS 5.00", "C:\\>"});
        check_one("dos5-boot-iso", {"-cdrom", iso.string(), "-boot", "d"}, {"CK-DOS 5.00", "A:\\>"});

        // Verify EDIT.COM, CKEDIT.COM, and full-screen DOSUI HELP.COM inside CK-DOS 5.0 VHD
        {
            const fs::path vga_file = scratch.path() / "vga_dos5_dosui.bin";
            int in_pipe[2]{-1, -1};
            if (pipe(in_pipe) != 0) throw std::runtime_error("pipe() failed");
            std::vector<std::string> cmd = {
                qemu_bin, "-snapshot", "-m", "4M", "-display", "none", "-serial", "none",
                "-parallel", "none", "-monitor", "stdio",
                "-drive", "file=" + vhd.string() + ",format=vpc", "-boot", "c"};
            const pid_t pid = fork();
            if (pid < 0) {
                close(in_pipe[0]);
                close(in_pipe[1]);
                throw std::runtime_error("fork() failed for qemu dosui check");
            }
            if (pid == 0) {
                dup2(in_pipe[0], STDIN_FILENO);
                close(in_pipe[0]);
                close(in_pipe[1]);
                if (FILE* f = std::fopen("/dev/null", "wb")) {
                    dup2(fileno(f), STDOUT_FILENO);
                    dup2(fileno(f), STDERR_FILENO);
                    std::fclose(f);
                }
                std::vector<char*> argv_ptrs;
                for (auto& s : cmd) argv_ptrs.push_back(s.data());
                argv_ptrs.push_back(nullptr);
                execvp(cmd[0].c_str(), argv_ptrs.data());
                _exit(127);
            }
            close(in_pipe[0]);

            auto send_mon = [&](const std::string& line) {
                const ssize_t wr = write(in_pipe[1], line.data(), line.size());
                (void)wr;
            };

            auto wait_for_tokens = [&](const std::vector<std::string>& tokens, const std::string& step_name) {
                for (int attempt = 0; attempt < 80; ++attempt) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                    std::error_code ec;
                    fs::remove(vga_file, ec);
                    send_mon("pmemsave 0xb8000 4000 \"" + vga_file.string() + "\"\n");
                    std::this_thread::sleep_for(std::chrono::milliseconds(60));
                    if (fs::is_regular_file(vga_file, ec) && fs::file_size(vga_file, ec) == 4000u) {
                        std::ifstream vin(vga_file, std::ios::binary);
                        std::array<char, 4000> raw{};
                        vin.read(raw.data(), 4000);
                        std::string screen;
                        screen.reserve(2000);
                        for (std::size_t i = 0; i < 4000; i += 2) {
                            const unsigned char ch = static_cast<unsigned char>(raw[i]);
                            screen.push_back((ch >= 32 && ch < 127) ? static_cast<char>(ch) : ' ');
                        }
                        bool ok = true;
                        for (const std::string& tok : tokens) {
                            if (screen.find(tok) == std::string::npos) {
                                ok = false;
                                break;
                            }
                        }
                        if (ok) return;
                    }
                }
                send_mon("quit\n");
                close(in_pipe[1]);
                int st = 0;
                waitpid(pid, &st, 0);
                throw std::runtime_error("QEMU DOSUI check timed out during " + step_name);
            };

            auto type_cmd = [&](std::string_view text) {
                for (char c : text) {
                    std::string key;
                    if (c >= 'a' && c <= 'z') key = std::string(1, c);
                    else if (c >= 'A' && c <= 'Z') key = std::string(1, static_cast<char>(c - 'A' + 'a'));
                    else if (c >= '0' && c <= '9') key = std::string(1, c);
                    else if (c == ' ') key = "spc";
                    else if (c == '/') key = "slash";
                    else if (c == '?') key = "shift-slash";
                    else if (c == '.') key = "dot";
                    else if (c == '\n') key = "ret";
                    if (!key.empty()) {
                        send_mon("sendkey " + key + "\n");
                        std::this_thread::sleep_for(std::chrono::milliseconds(35));
                    }
                }
            };

            wait_for_tokens({"CK-DOS 5.00", "C:\\>"}, "boot to C:\\>");

            type_cmd("cls\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            type_cmd("edit /?\n");
            wait_for_tokens({"Starts the CK-DOS Editor", "EDIT [[drive:][path]filename]"}, "EDIT.COM /?");
            std::cout << "QEMU utility verified: dos5 EDIT.COM /?\n";

            type_cmd("cls\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            type_cmd("ckedit /?\n");
            wait_for_tokens({"Starts the CK-DOS Editor", "/NOHI"}, "CKEDIT.COM /?");
            std::cout << "QEMU utility verified: dos5 CKEDIT.COM /?\n";

            type_cmd("help ver\n");
            wait_for_tokens({"File  Help", "CK-DOS 5 version", "SETVER"}, "HELP.COM VER");
            std::cout << "QEMU utility verified: dos5 HELP.COM VER (full-screen DOSUI)\n";

            send_mon("sendkey esc\n");
            wait_for_tokens({"C:\\>"}, "HELP.COM exit restore");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            type_cmd("edit readme.txt\n");
            wait_for_tokens({"CK-DOS Editor", "File  Edit  Search  Options", "CK-DOS 5.00 Clean-Room Build"},
                            "EDIT.COM README.TXT");
            std::cout << "QEMU utility verified: dos5 EDIT.COM README.TXT (full-screen DOSUI)\n";

            send_mon("quit\n");
            close(in_pipe[1]);
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }
#endif
}

void print_usage() {
    std::cerr << "Usage:\n"
              << "  ckdist verify-build [--os dos4|dos5|all] [--repo-root DIR] [--tool-dir DIR]\n"
              << "                      [--verify-manifest JSON] [--write-manifest JSON]\n"
              << "  ckdist build-dos-tools [--repo-root DIR] [--tool-dir DIR] --out-dir DIR\n"
              << "  ckdist release [--os dos4|dos5|all] [--repo-root DIR] [--tool-dir DIR] --output DIR\n"
              << "                 [--version VER] [--platform PLAT] [--verify-manifest JSON]\n"
              << "                 [--sdk-only] [--pages-out DIR]\n"
              << "  ckdist verify-boot [--os dos4|dos5|all] --dist-dir DIR [--qemu QEMU_BIN]\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 1;
        }
        const std::string subcmd = argv[1];
        fs::path repo_root = fs::current_path();
        fs::path tool_dir = fs::weakly_canonical(fs::path(argv[0])).parent_path();
        fs::path verify_manifest;
        fs::path write_manifest;
        fs::path output_dir;
        fs::path pages_dir;
        std::string version;
        std::string platform = default_platform_tag();
        std::string qemu_bin = "qemu-system-i386";
        OsTarget os_target = OsTarget::All;
        bool os_explicit = false;
        bool sdk_only = false;

        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--os" && i + 1 < argc) {
                os_target = parse_os_target(argv[++i]);
                os_explicit = true;
            } else if (a == "--repo-root" && i + 1 < argc) repo_root = argv[++i];
            else if (a == "--tool-dir" && i + 1 < argc) tool_dir = argv[++i];
            else if (a == "--verify-manifest" && i + 1 < argc) verify_manifest = argv[++i];
            else if (a == "--write-manifest" && i + 1 < argc) write_manifest = argv[++i];
            else if ((a == "--output" || a == "--out-dir" || a == "--dist-dir" || a == "-o") && i + 1 < argc) output_dir = argv[++i];
            else if (a == "--pages-out" && i + 1 < argc) pages_dir = argv[++i];
            else if (a == "--version" && i + 1 < argc) version = argv[++i];
            else if (a == "--platform" && i + 1 < argc) platform = argv[++i];
            else if (a == "--qemu" && i + 1 < argc) qemu_bin = argv[++i];
            else if (a == "--sdk-only") sdk_only = true;
            else {
                std::cerr << "ckdist: unrecognized option: " << a << "\n";
                print_usage();
                return 1;
            }
        }

        repo_root = fs::weakly_canonical(repo_root);
        tool_dir = fs::weakly_canonical(tool_dir);
        if (version.empty()) {
            version = read_text_trimmed(repo_root / "VERSION");
        }
        if (!os_explicit && !write_manifest.empty()) {
            os_target = OsTarget::Dos4;
        }

        if (subcmd == "verify-build") {
            ScratchDir scratch("ckdist-verify");
            if (includes_dos4(os_target)) {
                fs::path stage4;
                build_and_verify_dos4_tree(
                    repo_root, tool_dir, scratch.path(), stage4,
                    os_target == OsTarget::Dos4 ? write_manifest : fs::path{},
                    verify_manifest);
            }
            if (includes_dos5(os_target)) {
                fs::path stage5;
                build_and_verify_dos5_tree(
                    repo_root, tool_dir, scratch.path(), stage5,
                    os_target == OsTarget::Dos5 ? write_manifest : fs::path{});
            }
            return 0;
        }
        if (subcmd == "build-dos-tools") {
            if (output_dir.empty()) {
                std::cerr << "ckdist build-dos-tools requires --out-dir\n";
                return 1;
            }
            build_dos_self_host_tools(repo_root, tool_dir, output_dir);
            return 0;
        }
        if (subcmd == "release") {
            if (output_dir.empty()) {
                std::cerr << "ckdist release requires --output\n";
                return 1;
            }
            fs::create_directories(output_dir);
            ScratchDir scratch("ckdist-rel");
            fs::path stage4;
            fs::path stage5;
            if (includes_dos4(os_target)) {
                build_and_verify_dos4_tree(
                    repo_root, tool_dir, scratch.path(), stage4,
                    os_target == OsTarget::Dos4 ? write_manifest : fs::path{},
                    verify_manifest);
            }
            if (includes_dos5(os_target)) {
                build_and_verify_dos5_tree(
                    repo_root, tool_dir, scratch.path(), stage5,
                    os_target == OsTarget::Dos5 ? write_manifest : fs::path{});
            }

            const fs::path dos_tools_dir = scratch.path() / "dos_tools";
            const auto dos_tools = build_dos_self_host_tools(repo_root, tool_dir, dos_tools_dir);

            if (!sdk_only) {
                if (includes_dos4(os_target)) {
                    build_os_media_set(stage4, dos_tools, output_dir, make_dos4_media_spec(version));
                }
                if (includes_dos5(os_target)) {
                    build_os_media_set(stage5, dos_tools, output_dir, make_dos5_media_spec(version));
                }
            }

            const fs::path& sdk_stage = !stage5.empty() ? stage5 : stage4;
            const fs::path sdk_zip = output_dir / ("ck-dos-sdk-" + version + "-" + platform + ".zip");
            build_sdk_zip(repo_root, tool_dir, sdk_stage, dos_tools, sdk_zip, version, platform);
            std::cout << "Built " << sdk_zip.filename().string() << " (" << fs::file_size(sdk_zip) << " bytes)\n";

            if (!sdk_only) {
                const auto sums = write_sha256sums(output_dir);
                if (!pages_dir.empty()) {
                    write_pages_site(pages_dir, output_dir, version, sums);
                    std::cout << "Generated Pages site in " << pages_dir.string() << "\n";
                }
            }
            return 0;
        }

        if (subcmd == "verify-boot") {
            if (output_dir.empty()) {
                std::cerr << "ckdist verify-boot requires --dist-dir\n";
                return 1;
            }
            verify_boot_media_with_qemu(output_dir, qemu_bin, os_target);
            return 0;
        }
        print_usage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "ckdist ERROR: " << ex.what() << "\n";
        return 1;
    }
}
