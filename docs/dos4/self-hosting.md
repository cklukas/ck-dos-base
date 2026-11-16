---
title: Self-Hosting 16-Bit DOS Tools
description: Running the 16-bit real-mode DOS developer toolchain (CKLINK.EXE, CKLIB.EXE, CKNMAKE.EXE, CKMKIMG.EXE) inside booted CK-DOS 4.0 and 5.0.
---
# Self-Hosting 16-Bit DOS Tools (`C:\DEVTOOLS`)

Every `ckdist release` build cross-compiles the portable C11 tool cores (`src/devtools/core/*.c` + `src/devtools/dos/*.c`) with `ckcl -AS -Os` + `SLIBC.LIB` into **10 real-mode 16-bit DOS `.EXE` binaries** and installs them into `C:\DEVTOOLS` on both `ckdos-4.00-hdd-32m.vhd` and `ckdos-5.00-hdd-32m.vhd` (as well as `dos-bin/` in the cross-SDK `.zip`):

| 16-Bit Executable | Purpose Inside Real-Mode DOS |
|---|---|
| `CKLINK.EXE` | 16-bit OMF linker supporting `.OBJ`, `.LIB`, `/MAP`, `/STACK`, `/EXEPACK`, and `@response` files |
| `CKLIB.EXE` | 16-bit OMF library archiver and dictionary builder |
| `CKNMAKE.EXE` | 16-bit `MAKEFILE` macro & dependency executor |
| `CKEXE2BN.EXE` | 16-bit `MZ`-to-`.COM`/`.SYS` flat binary converter |
| `CKMKIMG.EXE` | 16-bit FAT12 floppy, MBR VHD, and ZIP image builder running directly inside DOS |
| `CKEXEFIX.EXE` | `MZ` header minimum-allocation and stack adjuster |
| `CKCONV.EXE` | DOS resource conversion utility |
| `CKCMPRSS.EXE` | Message/resource compression tool |
| `CKBLDMSG.EXE` | Indexed DOS message catalog compiler |
| `CKNOSRV.EXE` | Standalone message module builder |

Alongside `C:\DEVTOOLS\*.EXE`, the hard-disk and ZIP distributions stage `C:\DEVTOOLS\LIB\SLIBC.LIB`, `MLIBCE.LIB` (on CK-DOS 5.0), `COMSUBS.LIB`, and `CASSFAR.LIB` so programs can be linked directly inside a running 16-bit DOS guest.

To build the 16-bit self-hosting tools standalone on a host machine:

```sh
./build/ckdist build-dos-tools --repo-root . --tool-dir build --out-dir dos-tools
```
