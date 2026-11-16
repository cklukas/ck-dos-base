---
title: Provenance & Clean-Room Licensing
description: MIT licensing, clean-room engineering rules, C. Klukas authorship of CK Basic / CK Edit / CK Help, and classic filename compatibility.
---
# Provenance & Clean-Room Licensing

`CK-DOS 5.0` is 100% open-source software distributed under the **MIT License**.

## Two Clearly Separated Provenance Layers

1. **MIT-Licensed MS-DOS 4.00 Base (`third_party/ms-dos-4/src` → `os/ck-dos-5/src`):**
   - `os/ck-dos-5/src` was seeded from the MIT-licensed MS-DOS 4.00 source release published by Microsoft and IBM in 2024 (with encoding repairs from `chasonr/MS-DOS` `release-1`).
   - Prior to seeding `os/ck-dos-5/src`, **all 55 historical prebuilt binary executables, object modules, and OMF libraries** (`.EXE`, `.COM`, `.OBJ`, `.LIB`, `.SYS`) were stripped. An automated test (`dos5_tree_contains_zero_prebuilt_binaries`) enforces that zero binary blobs ever enter `os/ck-dos-5/src`.

2. **First-Party Clean-Room CK-DOS 5.0 Components (`Copyright (c) 2026 C. Klukas`):**
   - **Full-Screen Applications:** **CK Basic** (`os/ck-dos-5/src/CMD/QBASIC/` → `QBASIC.EXE`), **CK Edit** (`os/ck-dos-5/src/CMD/EDIT/` → `EDIT.COM`), **CK Help** (`os/ck-dos-5/src/CMD/HELP/` → `HELP.COM`), and the **`DOSUI`** framework (`os/ck-dos-5/src/CMD/DOSUI/`).
   - **High-Memory & DOS 5 Utilities:** `HIMEM.SYS` (`DEV/HIMEM/`), `DOS=HIGH` HMA relocation, `DOS=UMB` / `DEVICEHIGH=` / `LH`, `DOSKEY.COM` (`CMD/DOSKEY/`), `SETVER.EXE` (`CMD/SETVER/`), `LOADFIX.COM` (`CMD/LOADFIX/`), and `EXPAND.EXE` (`CMD/EXPAND/`).
   - **Runtime Libraries & Cross-Toolchain:** `SLIBCE.LIB`, `MLIBCE.LIB` (including clean-room software IEEE-754 `float`/`double` and `<math.h>`), `COMSUBS.LIB`, `CASSFAR.LIB`, and the native/16-bit toolchain (`ckcl`, `ckmasm`, `cklink`, `cklib`, `cknmake`, `ckexe2bin`, `ckmkimg`, `ckdist`).

> [!IMPORTANT]
> **Zero Proprietary MS-DOS 5.0 or QBasic Code:** Every DOS 5.0 extension and application in `CK-DOS 5.0` was written from scratch using public IBM PC BIOS, LIM EMS, XMS 3.0, MS-DOS Programmer's Reference, and QuickBASIC language specifications. Zero Microsoft MS-DOS 5.0 or QBasic source code, disassembly, `DOSHELP.HLP` text, or `QBASIC.HLP` data was used.

## Classic Executable Filenames vs. Application Branding

For 100% drop-in compatibility with existing DOS batch files, `QBASIC /EDITOR` handoffs, `CONFIG.SYS` templates, and third-party installers, `CK-DOS 5.0` names its compiled binaries using standard DOS 5 filenames (`QBASIC.EXE`, `EDIT.COM`, `HELP.COM`, `HIMEM.SYS`) without redundant duplicate files, while clearly identifying the first-party clean-room applications (**CK Basic**, **CK Edit**, **CK Help**) and `Copyright (c) 2026 C. Klukas` in `/?` banners, `Help → About...` dialogs, built-in `HELP` descriptions, and this documentation.
