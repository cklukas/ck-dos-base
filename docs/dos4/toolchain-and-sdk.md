---
title: Cross-Toolchain & Clean-Room CRT
description: Reference manual for ckcl, ckmasm, cklink, cklib, cknmake, ckexe2bin, ckmkimg, ckdist, SLIBCE.LIB, and MLIBCE.LIB.
---
# Cross-Toolchain & Clean-Room CRT

`CK-DOS Base` implements a complete, dependency-free 16-bit x86 DOS compilation, assembly, linking, archiving, and disk-imaging suite in portable C11 and C++20.

## Native Host Executables

| Tool | Role | Key Options & Capabilities |
|---|---|---|
| `ckcl` | 16-bit C89 compiler | Small (`-AS`) and Medium (`-AM`, per-module `<MODULE>_TEXT` far code + `DGROUP` near data + `_FAR_BSS`) models, `-Os`, `-Zp`, `-Zl`, `__far`, `__near`, `__interrupt`, and software IEEE-754 `float`/`double` (`-FPa` default). |
| `ckmasm` | 8086/286/386 OMF assembler | MASM 5.10-compatible syntax, macros, `STRUC`/`RECORD`, `.DOSSEG`, `GROUP`, conditionals, and multi-pass phase-aligned OMF emission (`-Mx`, `-Ml`, `-I`, `-D`). |
| `cklink` | 16-bit OMF linker | Links OMF `.OBJ` modules and `.LIB` dictionary libraries into DOS `MZ` executables, with `.DOSSEG` ordering, `/MAP`, `/STACK:n`, `/EXEPACK`, and `@response` files. |
| `cklib` | OMF library manager | Builds and updates OMF `.LIB` archives with hashed 512-byte symbol lookup dictionaries. |
| `ckexe2bin` | Flat binary converter | Converts relocatable/ORG `100h` `MZ` images into flat `.COM` executables and `.SYS` device drivers. |
| `cknmake` | Build planner & executor | Evaluates MS-DOS `MAKEFILE` macros, inference rules, inline response blocks, and recipes (`/N` dry-run or live execution). |
| `ckmkimg` | Disk & archive packager | Writes bootable FAT12 floppies (`360k`, `720k`, `1200k`, `1440k`), MBR-partitioned Fixed `.VHD` hard disks, El-Torito bootable `.ISO` images, and `.ZIP` archives. |
| `ckdist` | Release & verification driver | Orchestrates `verify-build`, `release`, `stage-vm`, `build-dos-tools`, `verify-boot`, and `gen-docs` across `--os dos4|dos5|all`. |
| `ckbuildmsg`, `cknosrvbld`, `ckbuildidx`, `ckdbof`, `ckmenubld`, `ckasc2hlp`, `ckcompress`, `ckconvert`, `ckexefix` | DOS 4/5 message & resource builders | Clean-room native implementations of all specialized MS-DOS message, codepage, and `SELECT` resource compilers. |
| `ckmzdump`, `ckomfdump`, `cklibdump` | Binary inspectors | Deterministic JSON/text dumpers for `MZ` headers/relocations, OMF records, and `.LIB` symbol tables. |

## Clean-Room Runtime Libraries (`runtime/`)

`runtime/include/` provides **21** standard C89 and DOS headers (`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>`, `<float.h>`, `<dos.h>`, `<i86.h>`, `<conio.h>`, `<io.h>`, `<fcntl.h>`, `<process.h>`, `<malloc.h>`, `<direct.h>`, `<time.h>`, `<ctype.h>`, `<errno.h>`, `<limits.h>`, `<share.h>`, `<stdarg.h>`, `<stddef.h>`, `<sys/stat.h>`):

- **`SLIBCE.LIB`:** Small-model (`-AS`) C runtime startup (`crt0.asm`), DOS file/console I/O (`dos_io.asm`, `crt_stdio.c`), near heap (`crt_mem.c`), and string routines (`crt_str.c`).
- **`MLIBCE.LIB`:** Medium-model (`-AM`) C runtime startup (`crt0.asm -DFARCODE`, reserving 18 KB stack + shrinking MCB to `DGROUP + 64 KB` so `_fmalloc` and child `spawnv*` have conventional RAM), far heap & far memory ops (`crt_fmem.asm`), clean-room IEEE-754 single/double software floating-point (`crt_fp.c`, with automatic x87 hardware dispatch and 386+ 32-bit integer IEEE-754 fast paths in `crt_fp386.asm`) and transcendental `<math.h>` (`crt_math.c`), formatted `printf`/`sprintf`/`strtod` (`crt_mstdio.c`), and DOS `int86`/`intdos`/`_dos_*`/`spawnve` (`crt_dos.c`).
- **`COMSUBS.LIB` & `CASSFAR.LIB`:** Clean-room replacements for the historical MS-DOS utility helper libraries.

## Compiling a Medium-Model 16-Bit DOS Program

```sh
./build/ckcl -c -AM -Os -Iruntime/include -FoHELLO.OBJ HELLO.C
./build/cklink "HELLO.OBJ,HELLO.EXE,NUL,MLIBCE.LIB;"
```

## Using `ck-dos-base` as a CMake Subproject

```cmake
set(DOSBASE_BUILD_TESTS OFF)
set(DOSBASE_INSTALL OFF)
add_subdirectory(external/ck-dos-base)
target_link_libraries(my_target PRIVATE dosbase::devtools)
```
