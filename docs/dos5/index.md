---
layout: home
title: CK-DOS 5.0
description: High-memory 16-bit operating system with 615+ KB free conventional RAM, CK Basic (QBASIC.EXE), CK Edit (EDIT.COM), and CK Help (HELP.COM).
hero:
  name: CK-DOS 5.0
  text: High-Memory 16-Bit DOS & Full-Screen Suite
  tagline: Clean-room DOS 5.0 kernel extensions (HIMEM.SYS, DOS=HIGH,UMB, 615.7 KB free RAM) paired with CK Basic, CK Edit, and CK Help — compiled 100% from C and assembly source under the MIT License.
  image:
    src: ../images/hero-dos5.jpg
    alt: CK-DOS 5.0 workstation running CK Basic IDE with DOS=HIGH and 615 KB FREE conventional memory
  actions:
    - text: Getting Started
      link: getting-started.md
      theme: brand
    - text: CK Basic (QBASIC.EXE)
      link: ck-basic.md
      theme: alt
    - text: Command Reference (63 Topics)
      link: command-reference.md
      theme: alt
features:
  - title: 615.7 KB Free Conventional RAM
    details: Relocates the kernel and disk buffers into the High Memory Area (DOS=HIGH) and loads TSRs and drivers into Upper Memory Blocks (DOS=UMB, DEVICEHIGH=, LH).
    link: memory-management.md
  - title: CK Basic (QBASIC.EXE)
    details: Clean-room full-screen BASIC IDE and structured QuickBASIC-compatible interpreter with SUB/FUNCTION, IEEE-754 floating-point, debugger, and online help.
    link: ck-basic.md
  - title: CK Edit & CK Help
    details: Mouse- and keyboard-driven full-screen text editor (EDIT.COM) and 63-topic hyperlinked command reference browser (HELP.COM) built on DOSUI.
    link: ck-edit-and-help.md
  - title: >32 MB FAT16 Without SHARE
    details: Moves 16-bit FCB safety directly into the kernel so large hard-disk volumes boot cleanly without spending ~13.5 KB of RAM on mandatory SHARE.EXE.
    link: memory-management.md
  - title: Modern DOS 5 CLI & Universal /?
    details: Ships dual-mode DOSKEY.COM (command history & macros), SETVER.EXE, LOADFIX.COM, EXPAND.EXE (SZDD/KWAJ), and universal /? help across every command.
    link: command-reference.md
  - title: 100% Built From Source
    details: Zero prebuilt binaries and zero proprietary MS-DOS 5.0 or QBasic source code — built end-to-end by ckcl, ckmasm, cklink, cklib, and ckmkimg.
    link: provenance.md
---

## Architecture at a Glance

`CK-DOS 5.0` (`os/ck-dos-5/`) combines an slimmed, DOS 5.0-ABI-aligned evolution of the MIT-licensed MS-DOS 4.00 kernel with clean-room, first-party 16-bit drivers and full-screen applications (`Copyright (c) 2026 C. Klukas`). For seamless compatibility with classic DOS batch scripts, books, and tools, executables use standard DOS 5 filenames (`QBASIC.EXE`, `EDIT.COM`, `HELP.COM`, `HIMEM.SYS`, `DOSKEY.COM`, `SETVER.EXE`, `LOADFIX.COM`, `EXPAND.EXE`).

```mermaid
flowchart LR
    subgraph LowRAM["Conventional RAM (0–640 KB)"]
        IVT["IVT + BIOS + DOSDATA Stub (~18 KB)"]
        FREE["615.7 KB (630,480 B) Free Application Arena"]
    end
    subgraph UMB["Upper Memory (D000h–DFFFh)"]
        ANSI["DEVICEHIGH=ANSI.SYS"]
        DK["LH DOSKEY.COM"]
    end
    subgraph HMA["High Memory Area (FFFF:0010h)"]
        KRN["Relocated DOSGROUP Kernel + BUFFERS=20"]
        HMAFREE["~19 KB Free HMA (INT 2Fh 4A01h/4A02h)"]
    end
    HIMEM["HIMEM.SYS (XMS 3.0 + A20 + UMB)"] --> HMA
    HIMEM --> UMB
    IVT --> FREE
```

## Quick Boot in QEMU

```sh
# Boot the 32 MB pre-installed CK-DOS 5.0 hard disk (includes C:\DOS + C:\DEVTOOLS):
qemu-system-i386 -m 4M -drive file=dist/ckdos-5.00-hdd-32m.vhd,format=vpc -boot c

# Or boot from the 1.44 MB install diskette or El-Torito CD-ROM ISO:
qemu-system-i386 -m 4M -fda dist/ckdos-5.00-floppy-1440k.img -boot a
qemu-system-i386 -m 4M -cdrom dist/ckdos-5.00-boot.iso -boot d
```

Looking for the frozen 654-hash MS-DOS 4.00 reference build or the cross-compiler toolchain manual? Switch to **[CK-DOS 4.0 & SDK](../dos4/index.md)** from the site selector in the header.
