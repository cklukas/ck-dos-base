---
title: Getting Started with CK-DOS 5.0
description: Build CK-DOS 5.0 from source on Linux, macOS, or Windows, boot the release images in QEMU or real hardware, and install to C:.
---
# Getting Started with CK-DOS 5.0

## Building From Source

`CK-DOS 5.0` requires only a C11 and C++20 host compiler plus CMake (3.21+). No external assemblers, Watcom/MSVC compilers, DOS emulators, or Python scripts are needed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure

# Build CK-DOS 5.0 (or --os all for both 4.0 and 5.0) into dist/:
./build/ckdist release \
  --os dos5 \
  --repo-root . \
  --tool-dir build \
  --output dist
```

## Release Media (`dist/ckdos-5.00-*`)

Every `ckdist release` build emits five ready-to-use `CK-DOS 5.0` media formats:

| Artifact | Format | Contents |
|---|---|---|
| `ckdos-5.00-floppy-1440k.img` | 3.5" 1.44 MB FAT12 floppy | Bootable all-in-one setup & rescue disk (`IO.SYS`, `MSDOS.SYS`, `COMMAND.COM`, `HIMEM.SYS`, `EDIT.COM`, `HELP.COM`, `QBASIC.EXE`, `INSTALL.BAT`, and all `DOS/` + `DEV/` utilities) |
| `ckdos-5.00-floppy-720k-disk1..3.img` | 3×720 KB FAT12 floppy set | Vintage XT/PS2 3-diskette distribution: **Disk 1** (Boot + Setup + DOSUI), **Disk 2** (Utilities & Drivers), **Disk 3** (`QBASIC.EXE` — CK Basic / Supplemental) |
| `ckdos-5.00-hdd-32m.vhd` | 32 MB Fixed VHD hard disk | Pre-installed bootable `C:` drive with `DOS=HIGH,UMB`, `C:\DOS` (all 62 utilities including **CK Basic**, **CK Edit**, **CK Help**), and `C:\DEVTOOLS` (16-bit self-hosting SDK + `MLIBCE.LIB`) |
| `ckdos-5.00-boot.iso` | El-Torito ISO-9660 CD-ROM | Bootable CD-ROM image carrying the 1.44 MB boot image plus the full `DOS/`, `DEV/`, and `DEVTOOLS/` trees |
| `ckdos-5.00-files.zip` | Standard ZIP archive | Complete loose file hierarchy for CompactFlash / XT-IDE cards, SD adapters, or host directory mounts |

## Default Startup Configuration

Out of the box on 286/386+ machines, `CK-DOS 5.0` uses a lean high-memory configuration that leaves **615.7 KB (`630,480` bytes)** of conventional RAM free for applications:

```ini
; C:\CONFIG.SYS
FILES=30
BUFFERS=20
DEVICE=C:\DOS\HIMEM.SYS
DOS=HIGH,UMB
DEVICEHIGH=C:\DEV\ANSI.SYS
```

```bat
@ECHO OFF
PATH C:\DOS;C:\DEVTOOLS
PROMPT $P$G
LH C:\DOS\DOSKEY >NUL
VER
```

## Installing onto a Blank Virtual or Physical Hard Disk

1. Boot `ckdos-5.00-floppy-1440k.img` in drive `A:` alongside a target hard disk.
2. If the hard disk has no active FAT partition yet, run `FDISK`, create a Primary DOS Partition, mark it active, and reboot onto `A:`.
3. Run `INSTALL` at the `A:\>` prompt — `INSTALL.BAT` formats `C:` if needed, transfers `IO.SYS`, `MSDOS.SYS`, and `COMMAND.COM`, copies `C:\DOS` and `C:\DEV`, and writes `CONFIG.SYS` and `AUTOEXEC.BAT`.
4. Eject floppy `A:` and boot directly from `C:`.

## Staging Bare Boot Payloads for Virtual Machine Bundles

Downstream emulators and VM frontends can stage bare `boot/dos4/` and `boot/dos5/` directories directly without building `.vhd` or `.iso` wrappers:

```sh
./build/ckdist stage-vm --os all --repo-root . --tool-dir build --output vm-stage
```
