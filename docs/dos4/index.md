---
layout: home
title: CK-DOS 4.0 & SDK
description: 654-hash byte-exact MS-DOS 4.00 reference distribution and the clean-room native + self-hosting 16-bit C/MASM cross-toolchain.
hero:
  name: CK-DOS 4.0 & SDK
  text: 654-Hash Reference DOS & Clean-Room 16-Bit Toolchain
  tagline: Every single object, library, driver, kernel, and utility in the preserved 1988 MS-DOS 4.00 source tree — rebuilt 100% natively on Linux, macOS, and Windows and verified byte-for-byte against 654 SHA-256 checksums.
  image:
    src: ../images/hero-dos4.jpg
    alt: Classic 1988 PC-AT workstation displaying C:\>VER / CK-DOS 4.00 alongside striped CK-DOS 4.0 reference binders and floppy disks
  actions:
    - text: Getting Started
      link: getting-started.md
      theme: brand
    - text: Toolchain & SDK Manual
      link: toolchain-and-sdk.md
      theme: alt
    - text: 654-Hash Build Manifest
      link: manifest-summary.md
      theme: alt
features:
  - title: 654 / 654 Byte-Exact Gate
    details: Rebuilds all 60 MS-DOS 4.00 source directories without invoking a single historical DOS binary and verifies all 654 SHA-256 artifact hashes on every push.
    link: manifest-summary.md
  - title: Clean-Room C & MASM Compiler
    details: Native C/C++20 cross-toolchain — ckcl (-AS small & -AM medium model + IEEE-754 float), ckmasm, cklink, cklib, cknmake, ckexe2bin, and ckmkimg.
    link: toolchain-and-sdk.md
  - title: Self-Hosting Inside 16-Bit DOS
    details: Compiles 10 real-mode 16-bit DOS developer tools (CKLINK.EXE, CKLIB.EXE, CKNMAKE.EXE, CKMKIMG.EXE, ...) staged in C:\DEVTOOLS.
    link: self-hosting.md
  - title: Clean-Room Runtime Libraries
    details: Ships first-party MIT runtime archives (SLIBCE.LIB, MLIBCE.LIB, COMSUBS.LIB, CASSFAR.LIB) and 21 standard C89/DOS headers with zero proprietary blobs.
    link: toolchain-and-sdk.md
  - title: Five Bootable Media Formats
    details: Emits 1.44 MB boot/install floppy, 2x720 KB XT/PS2 diskettes, 32 MB Fixed VHD hard disk, El-Torito bootable ISO-9660 CD-ROM, and loose ZIP archive.
    link: getting-started.md
  - title: CMake Subproject Ready
    details: Embed dosbase::devtools and host cross-tools directly into emulators, test suites, or retro OS build pipelines via add_subdirectory().
    link: toolchain-and-sdk.md
---

## Reproducible Native Build Pipeline

`CK-DOS 4.0` keeps `third_party/ms-dos-4/` completely frozen as a reproducible historical reference while replacing every legacy 1988 DOS build executable (`MASM.EXE`, `CL.EXE`, `LINK.EXE`, `LIB.EXE`, `NMAKE.EXE`, `EXE2BIN.EXE`) with clean-room native host binaries:

```mermaid
flowchart LR
    SRC["third_party/ms-dos-4/src (60 dirs)"] --> TOOLS["ckcl + ckmasm + cklink + cklib + cknmake"]
    RT["Clean-Room runtime/ (SLIBCE, COMSUBS, CASSFAR)"] --> TOOLS
    TOOLS --> GATE["654 / 654 SHA-256 Manifest Gate"]
    GATE --> MEDIA["1.44M · 2x720K · 32M VHD · Boot ISO · ZIP"]
    MEDIA --> QEMU["Headless QEMU Boot Verification"]
```

Looking for high-memory `DOS=HIGH,UMB` (`615.7 KB` free RAM), **CK Basic (`QBASIC.EXE`)**, **CK Edit (`EDIT.COM`)**, and **CK Help (`HELP.COM`)**? Switch to **[CK-DOS 5.0](../dos5/index.md)** using the site selector in the header.
