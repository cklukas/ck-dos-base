// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/core/diskimg_core.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct HostSrcFile {
    fs::path host_path;
    std::string target_path;
    std::uint32_t size = 0;
    std::uint32_t unix_mode = 0;
};

int host_stream_read(void* ctx, ck_u32 offset, ck_u8* buf, ck_u32 len) {
    const auto* entry = static_cast<const HostSrcFile*>(ctx);
    std::ifstream in(entry->host_path, std::ios::binary);
    if (!in) return -1;
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in) return -1;
    in.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
    return in.gcount() == static_cast<std::streamsize>(len) ? 0 : -1;
}

int host_stream_write(void* ctx, const ck_u8* buf, ck_u32 len) {
    auto* out = static_cast<std::ofstream*>(ctx);
    out->write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(len));
    return out->good() ? 0 : -1;
}

bool load_boot_sector_512(const fs::path& path, ck_u8 out512[512]) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const auto sz = in.tellg();
    if (sz < 512) return false;
    const std::streamoff off = (sz >= 0x7E00) ? 0x7C00 : 0;
    in.seekg(off, std::ios::beg);
    in.read(reinterpret_cast<char*>(out512), 512);
    return in.gcount() == 512;
}

bool is_root_sys_file(std::string_view p) {
    while (!p.empty() && (p.front() == '/' || p.front() == '\\')) p.remove_prefix(1);
    std::string up(p);
    for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return up == "IO.SYS" || up == "MSDOS.SYS";
}

void print_usage() {
    std::cerr << "Usage:\n"
              << "  ckmkimg floppy [-s 360k|720k|1200k|1440k] [-l LABEL] [-b BOOT] -o OUT.IMG [SRC=DST ...]\n"
              << "  ckmkimg hdd [-c CYLS] [-h HEADS] [-t SPT] [--vhd] [-l LABEL] [-b BOOT] -o OUT.VHD [SRC=DST ...]\n"
              << "  ckmkimg iso [-l VOLID] -b BOOT1440.IMG -o OUT.ISO [SRC=DST ...]\n"
              << "  ckmkimg zip -o OUT.ZIP [SRC=DST ...]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    const std::string mode = argv[1];
    fs::path out_path;
    fs::path boot_path;
    std::string label = (mode == "iso") ? "CKDOS_400" : "CKDOS 4.00";
    ck_floppy_kind fkind = CK_FLOPPY_1440K;
    ck_u16 cyls = 64;
    ck_u16 heads = 16;
    ck_u16 spt = 63;
    bool vhd = false;
    std::vector<HostSrcFile> sources;

    auto add_spec = [&](const std::string& spec) -> bool {
        HostSrcFile item;
        const auto eq = spec.find('=');
        if (eq != std::string::npos) {
            item.host_path = spec.substr(0, eq);
            item.target_path = spec.substr(eq + 1);
        } else {
            item.host_path = spec;
            item.target_path = fs::path(spec).filename().string();
        }
        std::error_code ec;
        const auto fsz = fs::file_size(item.host_path, ec);
        if (ec) {
            std::cerr << "ckmkimg: cannot stat input file: " << item.host_path << "\n";
            return false;
        }
        const auto st = fs::status(item.host_path, ec);
        if (!ec && (st.permissions() & fs::perms::owner_exec) != fs::perms::none) {
            item.unix_mode = 0100755u;
        } else {
            item.unix_mode = 0100644u;
        }
        item.size = static_cast<std::uint32_t>(fsz);
        sources.push_back(std::move(item));
        return true;
    };

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-o" || a == "--output") && i + 1 < argc) {
            out_path = argv[++i];
        } else if ((a == "-b" || a == "--boot" || a == "--eltorito-floppy") && i + 1 < argc) {
            boot_path = argv[++i];
        } else if ((a == "-l" || a == "--label") && i + 1 < argc) {
            label = argv[++i];
        } else if ((a == "-s" || a == "--size") && i + 1 < argc) {
            std::string sv = argv[++i];
            for (char& c : sv) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (sv == "360k") fkind = CK_FLOPPY_360K;
            else if (sv == "720k") fkind = CK_FLOPPY_720K;
            else if (sv == "1200k" || sv == "1.2m") fkind = CK_FLOPPY_1200K;
            else if (sv == "1440k" || sv == "1.44m") fkind = CK_FLOPPY_1440K;
            else {
                std::cerr << "ckmkimg: unsupported floppy size: " << sv << "\n";
                return 1;
            }
        } else if (a == "--vhd") {
            vhd = true;
        } else if (a == "-c" && i + 1 < argc) {
            cyls = static_cast<ck_u16>(std::stoi(argv[++i]));
        } else if (a == "-h" && i + 1 < argc) {
            heads = static_cast<ck_u16>(std::stoi(argv[++i]));
        } else if (a == "-t" && i + 1 < argc) {
            spt = static_cast<ck_u16>(std::stoi(argv[++i]));
        } else if (a == "--size-mb" && i + 1 < argc) {
            int mb = std::stoi(argv[++i]);
            if (mb < 4) mb = 4;
            cyls = static_cast<ck_u16>((mb * 2048) / (static_cast<int>(heads) * static_cast<int>(spt)));
            if (cyls < 8) cyls = 8;
        } else if (!a.empty() && a.front() == '@') {
            std::ifstream rsp(a.substr(1));
            if (!rsp) {
                std::cerr << "ckmkimg: cannot open response file: " << a.substr(1) << "\n";
                return 1;
            }
            std::string line;
            while (std::getline(rsp, line)) {
                while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
                std::size_t st = 0;
                while (st < line.size() && std::isspace(static_cast<unsigned char>(line[st]))) ++st;
                if (st < line.size() && line[st] != '#') {
                    if (!add_spec(line.substr(st))) return 1;
                }
            }
        } else {
            if (!add_spec(a)) return 1;
        }
    }

    if (out_path.empty()) {
        std::cerr << "ckmkimg: missing -o <output>\n";
        return 1;
    }

    std::vector<ck_img_file> img_files(sources.size());
    std::vector<ck_zip_item> zip_items(sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i) {
        std::memset(&img_files[i], 0, sizeof(ck_img_file));
        img_files[i].dos_path = sources[i].target_path.c_str();
        img_files[i].attr = is_root_sys_file(sources[i].target_path) ? 0x07u : 0x20u;
        img_files[i].size = sources[i].size;
        img_files[i].read_ctx = &sources[i];
        img_files[i].read_fn = host_stream_read;

        std::memset(&zip_items[i], 0, sizeof(ck_zip_item));
        zip_items[i].archive_path = sources[i].target_path.c_str();
        zip_items[i].unix_mode = sources[i].unix_mode;
        zip_items[i].size = sources[i].size;
        zip_items[i].read_ctx = &sources[i];
        zip_items[i].read_fn = host_stream_read;
    }

    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
    }
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "ckmkimg: cannot open output file: " << out_path << "\n";
        return 1;
    }

    ck_error err{};
    if (mode == "floppy") {
        ck_u8 boot512[512];
        const ck_u8* boot_ptr = nullptr;
        if (!boot_path.empty()) {
            if (!load_boot_sector_512(boot_path, boot512)) {
                std::cerr << "ckmkimg: invalid boot sector file: " << boot_path << "\n";
                return 1;
            }
            boot_ptr = boot512;
        }
        ck_fat_geom geom{};
        ck_diskimg_floppy_geom(fkind, label.c_str(), &geom);
        if (ck_diskimg_write_fat_volume(&geom, boot_ptr, img_files.data(), img_files.size(),
                                        host_stream_write, &out, &err) != 0) {
            std::cerr << "ckmkimg: " << err.message << "\n";
            return 1;
        }
    } else if (mode == "hdd") {
        ck_u8 boot512[512];
        const ck_u8* boot_ptr = nullptr;
        if (!boot_path.empty()) {
            if (!load_boot_sector_512(boot_path, boot512)) {
                std::cerr << "ckmkimg: invalid boot sector file: " << boot_path << "\n";
                return 1;
            }
            boot_ptr = boot512;
        }
        if (ck_diskimg_write_hdd_image(cyls, heads, spt, label.c_str(), boot_ptr,
                                       img_files.data(), img_files.size(), vhd ? 1 : 0,
                                       host_stream_write, &out, &err) != 0) {
            std::cerr << "ckmkimg: " << err.message << "\n";
            return 1;
        }
    } else if (mode == "iso") {
        if (boot_path.empty()) {
            std::cerr << "ckmkimg: iso mode requires -b <1440k-boot-floppy.img>\n";
            return 1;
        }
        HostSrcFile boot_src{boot_path, "BOOT1440.IMG", 0, 0100644u};
        std::error_code ec;
        boot_src.size = static_cast<std::uint32_t>(fs::file_size(boot_path, ec));
        if (ec || boot_src.size != 1474560u) {
            std::cerr << "ckmkimg: boot floppy must be 1,474,560 bytes: " << boot_path << "\n";
            return 1;
        }
        ck_img_file boot_f{};
        boot_f.dos_path = "BOOT1440.IMG";
        boot_f.attr = 0x20u;
        boot_f.size = boot_src.size;
        boot_f.read_ctx = &boot_src;
        boot_f.read_fn = host_stream_read;
        if (ck_diskimg_write_eltorito_iso(label.c_str(), &boot_f, img_files.data(), img_files.size(),
                                          host_stream_write, &out, &err) != 0) {
            std::cerr << "ckmkimg: " << err.message << "\n";
            return 1;
        }
    } else if (mode == "zip") {
        if (ck_diskimg_write_zip(zip_items.data(), zip_items.size(),
                                 host_stream_write, &out, &err) != 0) {
            std::cerr << "ckmkimg: " << err.message << "\n";
            return 1;
        }
    } else {
        print_usage();
        return 1;
    }

    out.close();
    if (!out) {
        std::cerr << "ckmkimg: failed flushing output file: " << out_path << "\n";
        return 1;
    }
    return 0;
}
