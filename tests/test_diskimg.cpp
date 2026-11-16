// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "test_harness.hpp"

#include "dosbase/devtools/cl_compiler.hpp"
#include "dosbase/devtools/cl_preproc.hpp"
#include "dosbase/devtools/core/diskimg_core.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int vec_write_cb(void* ctx, const ck_u8* buf, ck_u32 len) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(ctx);
    out->insert(out->end(), buf, buf + len);
    return 0;
}

std::string read_file_str(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

DOSBASE_TEST(diskimg_floppy_fat12_orders_sys_files_and_subdirs) {
    ck_fat_geom geom{};
    ck_diskimg_floppy_geom(CK_FLOPPY_1440K, "CKDOS 4.00", &geom);

    const std::string cmd_data = "COMMANDPAYLOAD";
    const std::string io_data(600, 'I');
    const std::string msdos_data(700, 'M');
    const std::string fmt_data = "FORMAT_UTILITY";

    ck_img_file files[4]{};
    files[0].dos_path = "COMMAND.COM";
    files[0].attr = 0x20u;
    files[0].size = static_cast<ck_u32>(cmd_data.size());
    files[0].mem_data = reinterpret_cast<const ck_u8*>(cmd_data.data());

    files[1].dos_path = "DOS/FORMAT.COM";
    files[1].attr = 0x20u;
    files[1].size = static_cast<ck_u32>(fmt_data.size());
    files[1].mem_data = reinterpret_cast<const ck_u8*>(fmt_data.data());

    files[2].dos_path = "MSDOS.SYS";
    files[2].attr = 0x07u;
    files[2].size = static_cast<ck_u32>(msdos_data.size());
    files[2].mem_data = reinterpret_cast<const ck_u8*>(msdos_data.data());

    files[3].dos_path = "IO.SYS";
    files[3].attr = 0x07u;
    files[3].size = static_cast<ck_u32>(io_data.size());
    files[3].mem_data = reinterpret_cast<const ck_u8*>(io_data.data());

    std::vector<std::uint8_t> img;
    ck_error err{};
    DOSBASE_TEST_EXPECT_EQ(
        ck_diskimg_write_fat_volume(&geom, nullptr, files, 4, vec_write_cb, &img, &err), 0);
    DOSBASE_TEST_EXPECT_EQ(img.size(), std::size_t{1474560});

    // Boot signature & FAT12 BPB tag
    DOSBASE_TEST_EXPECT_EQ(img[0x1FE], std::uint8_t{0x55});
    DOSBASE_TEST_EXPECT_EQ(img[0x1FF], std::uint8_t{0xAA});
    DOSBASE_TEST_EXPECT(std::memcmp(img.data() + 0x36, "FAT12   ", 8) == 0);

    // Root directory starts at sector 1 + 2*9 = 19 (offset 9728)
    const std::size_t root_off = 19u * 512u;
    // Entry #0 MUST be IO.SYS starting at cluster 2
    DOSBASE_TEST_EXPECT(std::memcmp(img.data() + root_off + 0, "IO      SYS", 11) == 0);
    DOSBASE_TEST_EXPECT_EQ(img[root_off + 11], std::uint8_t{0x07});
    DOSBASE_TEST_EXPECT_EQ(ck_read_u16_le(img.data() + root_off + 26), std::uint16_t{2});

    // Entry #1 MUST be MSDOS.SYS starting immediately after IO.SYS (2 sectors -> cluster 4)
    DOSBASE_TEST_EXPECT(std::memcmp(img.data() + root_off + 32, "MSDOS   SYS", 11) == 0);
    DOSBASE_TEST_EXPECT_EQ(ck_read_u16_le(img.data() + root_off + 32 + 26), std::uint16_t{4});

    // Entry #2 is volume label
    DOSBASE_TEST_EXPECT_EQ(img[root_off + 64 + 11], std::uint8_t{0x08});

    // Data area starts at sector 19 + 14 = 33; cluster 2 is IO.SYS
    const std::size_t data_off = 33u * 512u;
    DOSBASE_TEST_EXPECT_EQ(img[data_off], static_cast<std::uint8_t>('I'));
    DOSBASE_TEST_EXPECT_EQ(img[data_off + 599], static_cast<std::uint8_t>('I'));
}

DOSBASE_TEST(diskimg_hdd_fat16_and_fixed_vhd_footer) {
    const std::string hi = "HELLO DOS HDD";
    ck_img_file file{};
    file.dos_path = "HELLO.TXT";
    file.attr = 0x20u;
    file.size = static_cast<ck_u32>(hi.size());
    file.mem_data = reinterpret_cast<const ck_u8*>(hi.data());

    std::vector<std::uint8_t> vhd;
    ck_error err{};
    // 16 cyl * 16 heads * 63 spt = 16,128 sectors (>4085 clusters with spc=2 -> FAT16)
    DOSBASE_TEST_EXPECT_EQ(
        ck_diskimg_write_hdd_image(16, 16, 63, "CKDOSHDD", nullptr, &file, 1, 1, vec_write_cb, &vhd, &err),
        0);

    const std::size_t disk_bytes = 16u * 16u * 63u * 512u;
    DOSBASE_TEST_EXPECT_EQ(vhd.size(), disk_bytes + 512u);

    // MBR active flag + signature
    DOSBASE_TEST_EXPECT_EQ(vhd[0x1BE], std::uint8_t{0x80});
    DOSBASE_TEST_EXPECT_EQ(vhd[0x1FE], std::uint8_t{0x55});
    DOSBASE_TEST_EXPECT_EQ(vhd[0x1FF], std::uint8_t{0xAA});

    // Partition VBR at sector 63 has FAT16 signature
    const std::size_t vbr_off = 63u * 512u;
    DOSBASE_TEST_EXPECT(std::memcmp(vhd.data() + vbr_off + 0x36, "FAT16   ", 8) == 0);

    // VHD Footer cookie & valid one's-complement checksum
    const std::uint8_t* footer = vhd.data() + disk_bytes;
    DOSBASE_TEST_EXPECT(std::memcmp(footer, "conectix", 8) == 0);
    std::uint32_t sum = 0;
    for (int i = 0; i < 512; ++i) {
        if (i >= 0x40 && i < 0x44) continue;
        sum += footer[i];
    }
    const std::uint32_t expected_csum = ~sum;
    const std::uint32_t actual_csum =
        (static_cast<std::uint32_t>(footer[0x40]) << 24) |
        (static_cast<std::uint32_t>(footer[0x41]) << 16) |
        (static_cast<std::uint32_t>(footer[0x42]) << 8) |
        static_cast<std::uint32_t>(footer[0x43]);
    DOSBASE_TEST_EXPECT_EQ(actual_csum, expected_csum);
}

DOSBASE_TEST(diskimg_eltorito_iso9660_boot_catalog_and_extents) {
    std::vector<std::uint8_t> boot1440(1474560, 0);
    boot1440[0] = 0xEB;
    boot1440[510] = 0x55;
    boot1440[511] = 0xAA;

    ck_img_file boot_f{};
    boot_f.dos_path = "BOOT1440.IMG";
    boot_f.attr = 0x20u;
    boot_f.size = static_cast<ck_u32>(boot1440.size());
    boot_f.mem_data = boot1440.data();

    const std::string readme = "ISO README\r\n";
    ck_img_file extra{};
    extra.dos_path = "DOS/README.TXT";
    extra.attr = 0x20u;
    extra.size = static_cast<ck_u32>(readme.size());
    extra.mem_data = reinterpret_cast<const ck_u8*>(readme.data());

    std::vector<std::uint8_t> iso;
    ck_error err{};
    DOSBASE_TEST_EXPECT_EQ(
        ck_diskimg_write_eltorito_iso("CKDOS_400", &boot_f, &extra, 1, vec_write_cb, &iso, &err),
        0);
    DOSBASE_TEST_EXPECT(iso.size() > 1474560u);
    DOSBASE_TEST_EXPECT_EQ(iso.size() % 2048u, std::size_t{0});

    // Primary Volume Descriptor at block 16, El-Torito Boot Record at block 17, Catalog at block 19
    DOSBASE_TEST_EXPECT(std::memcmp(iso.data() + 16u * 2048u + 1u, "CD001", 5) == 0);
    DOSBASE_TEST_EXPECT(std::memcmp(iso.data() + 17u * 2048u + 7u, "EL TORITO SPECIFICATION", 23) == 0);
    DOSBASE_TEST_EXPECT_EQ(ck_read_u32_le(iso.data() + 17u * 2048u + 0x47u), std::uint32_t{19});

    const std::uint8_t* cat = iso.data() + 19u * 2048u;
    std::uint16_t wsum = 0;
    for (std::size_t i = 0; i < 32; i += 2) {
        wsum = static_cast<std::uint16_t>(wsum + ck_read_u16_le(cat + i));
    }
    DOSBASE_TEST_EXPECT_EQ(wsum, std::uint16_t{0});
    DOSBASE_TEST_EXPECT_EQ(cat[0x20], std::uint8_t{0x88});
    DOSBASE_TEST_EXPECT_EQ(cat[0x21], std::uint8_t{0x02});
    const std::uint32_t boot_rba = ck_read_u32_le(cat + 0x28);
    DOSBASE_TEST_EXPECT_EQ(iso[boot_rba * 2048u], std::uint8_t{0xEB});
}

DOSBASE_TEST(diskimg_crc32_and_zip_archive_format) {
    const char* check_str = "123456789";
    DOSBASE_TEST_EXPECT_EQ(
        ck_diskimg_crc32(0, reinterpret_cast<const ck_u8*>(check_str), 9),
        std::uint32_t{0xCBF43926u});

    ck_zip_item z{};
    z.archive_path = "bin/ckmkimg";
    z.unix_mode = 0100755u;
    z.size = 9;
    z.mem_data = reinterpret_cast<const ck_u8*>(check_str);

    std::vector<std::uint8_t> zip_bytes;
    ck_error err{};
    DOSBASE_TEST_EXPECT_EQ(ck_diskimg_write_zip(&z, 1, vec_write_cb, &zip_bytes, &err), 0);
    DOSBASE_TEST_EXPECT_EQ(ck_read_u32_le(zip_bytes.data()), std::uint32_t{0x04034B50u});
    DOSBASE_TEST_EXPECT_EQ(
        ck_read_u32_le(zip_bytes.data() + zip_bytes.size() - 22u),
        std::uint32_t{0x06054B50u});
}

DOSBASE_TEST(diskimg_compiles_for_16bit_dos_with_ckcl) {
    using namespace dosbase::devtools;
    const std::filesystem::path root(DOSBASE_SOURCE_DIR);
    for (const auto& rel : {"src/devtools/core/diskimg_core.c", "src/devtools/dos/ckmkimg.c"}) {
        const auto src_path = root / rel;
        PreprocOptions popts;
        popts.include_dirs = {root / "include", root / "src/devtools/dos"};
        popts.defines = {{"M_I86", "1"}, {"M_I86SM", "1"}, {"MSDOS", "1"}, {"CK_DOS_LEAN", "1"}};
        const std::string pp = preprocess_c_source(read_file_str(src_path), src_path, popts);

        CompileOptions copts;
        copts.module_name = src_path.stem().string();
        copts.model = MemoryModel::small_model;
        copts.pack_structs = true;
        const auto obj = compile_c_to_omf(pp, copts);
        DOSBASE_TEST_EXPECT(obj.size() > 64u);
        DOSBASE_TEST_EXPECT_EQ(obj[0], std::uint8_t{0x80});
    }
}
