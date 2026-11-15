// Copyright (c) 2026 C. Klukas
// SPDX-License-Identifier: MIT
#include "dosbase/devtools/convert.hpp"

#include "dosbase/devtools/binary.hpp"
#include "dosbase/devtools/mz_image.hpp"

#include <array>

namespace dosbase::devtools {
namespace {

constexpr std::size_t kPrefixSize = 16;

// 123-byte (0x7B) self-relocating .COM loader stub emitted by CONVERT.EXE:
//   +0x00..0x07: 8-byte scratch area for relocated entry CS:IP and SS:SP
//   +0x08..0x7A: 8086 code that reads the embedded MZ header at CS:0110,
//                applies segment relocations, copies the load image down to
//                CS:0100, switches SS:SP, and far-jumps to CS:IP.
constexpr std::array<std::uint8_t, 123> kConvertStub = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // scratch: IP, CS, SP, SS
    0xE8, 0x00, 0x00,                                // call $+3
    0x5B,                                            // pop bx
    0x50,                                            // push ax
    0x8C, 0xC0,                                      // mov ax, cs
    0x05, 0x10, 0x00,                                // add ax, 0x0010
    0x8B, 0x0E, 0x1E, 0x01,                          // mov cx, [0x011e] (initial_ss)
    0x03, 0xC8,                                      // add cx, ax
    0x89, 0x4F, 0xFB,                                // mov [bx-5], cx
    0x8B, 0x0E, 0x26, 0x01,                          // mov cx, [0x0126] (initial_cs)
    0x03, 0xC8,                                      // add cx, ax
    0x89, 0x4F, 0xF7,                                // mov [bx-9], cx
    0x8B, 0x0E, 0x20, 0x01,                          // mov cx, [0x0120] (initial_sp)
    0x89, 0x4F, 0xF9,                                // mov [bx-7], cx
    0x8B, 0x0E, 0x24, 0x01,                          // mov cx, [0x0124] (initial_ip)
    0x89, 0x4F, 0xF5,                                // mov [bx-11], cx
    0x8B, 0x3E, 0x28, 0x01,                          // mov di, [0x0128] (reloc_table_offset)
    0x8B, 0x16, 0x18, 0x01,                          // mov dx, [0x0118] (header_paragraphs)
    0xB1, 0x04,                                      // mov cl, 4
    0xD3, 0xE2,                                      // shl dx, cl
    0x8B, 0x0E, 0x16, 0x01,                          // mov cx, [0x0116] (relocation_count)
    0xE3, 0x1A,                                      // jcxz copy_image
    // reloc_loop:
    0x26, 0xC5, 0xB5, 0x10, 0x01,                    // lds si, es:[di+0x0110]
    0x83, 0xC7, 0x04,                                // add di, 4
    0x8C, 0xDD,                                      // mov bp, ds
    0x26, 0x03, 0x2E, 0x18, 0x01,                    // add bp, es:[0x0118]
    0x83, 0xC5, 0x01,                                // add bp, 1
    0x03, 0xE8,                                      // add bp, ax
    0x8E, 0xDD,                                      // mov ds, bp
    0x01, 0x04,                                      // add [si], ax
    0xE2, 0xE6,                                      // loop reloc_loop
    // copy_image:
    0x0E,                                            // push cs
    0x1F,                                            // pop ds
    0xBF, 0x00, 0x01,                                // mov di, 0x0100
    0x8B, 0xF2,                                      // mov si, dx
    0x81, 0xC6, 0x10, 0x01,                          // add si, 0x0110
    0x8B, 0xCB,                                      // mov cx, bx
    0x2B, 0xCE,                                      // sub cx, si
    0xF3, 0xA4,                                      // rep movsb
    0x58,                                            // pop ax
    0xFA,                                            // cli
    0x8E, 0x57, 0xFB,                                // mov ss, [bx-5]
    0x8B, 0x67, 0xF9,                                // mov sp, [bx-7]
    0xFB,                                            // sti
    0xFF, 0x6F, 0xF5,                                // jmp far ptr [bx-11]
};

}  // namespace

std::vector<std::uint8_t> convert_exe_to_com(std::span<const std::uint8_t> exe_bytes) {
    // Validate that the input is a well-formed MZ executable.
    (void)MzImage::parse(exe_bytes);

    const std::size_t total_size = kPrefixSize + exe_bytes.size() + kConvertStub.size();
    if (total_size >= kConvertMaxComBytes) {
        throw FormatError(exe_bytes.size(), "converted .COM image exceeds 64 KiB limit");
    }

    // Jump displacement from end of the 3-byte JMP instruction (offset 3)
    // to the CALL $+3 instruction at offset 8 inside kConvertStub:
    //   target = kPrefixSize + exe_bytes.size() + 8
    //   rel16  = target - 3 = kPrefixSize + exe_bytes.size() + 5
    const std::uint16_t rel16 =
        static_cast<std::uint16_t>(kPrefixSize + exe_bytes.size() + 5);

    std::vector<std::uint8_t> out;
    out.reserve(total_size);

    // 16-byte prefix: JMP rel16 + "Converted\0\0\0\0"
    out.push_back(0xE9);
    out.push_back(static_cast<std::uint8_t>(rel16 & 0xFF));
    out.push_back(static_cast<std::uint8_t>(rel16 >> 8));
    const std::array<std::uint8_t, 13> tag = {'C', 'o', 'n', 'v', 'e', 'r', 't',
                                              'e', 'd', 0,   0,   0,   0};
    out.insert(out.end(), tag.begin(), tag.end());

    // Original MZ .EXE verbatim
    out.insert(out.end(), exe_bytes.begin(), exe_bytes.end());

    // 123-byte self-relocating loader stub
    out.insert(out.end(), kConvertStub.begin(), kConvertStub.end());

    return out;
}

}  // namespace dosbase::devtools
