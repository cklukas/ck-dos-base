# Work Packages: Complete Open-Source Build Toolchain

This document breaks down the remaining work required to build the preserved MS-DOS 4 source tree (`third_party/ms-dos-4/src`) natively on macOS, Linux, and Windows using only open-source tools—with zero reliance on historical 16-bit DOS executables or precompiled `.LIB`/`.OBJ` binaries.

**Ordering principle:** Small, self-contained tools first, followed by build orchestration, then the C compiler and runtime libraries.

## Priority & Ordering Overview

```text
WP-1: ckconvert & ckexefix (P0)
  └──► WP-2: ckmenubld, ckasc2hlp, ckcompress (P0)
         └──► WP-3: cknmake execution (P1)
                └──► WP-4: ckcl C compiler (P1)
                       └──► WP-5: Open-source CRT & libs (P2)
                              └──► WP-6: Full build & CI gate (P2)
```

| ID | Work Package | Priority | Depends On | Milestone Unlocked |
| --- | --- | --- | --- | --- |
| **WP-1** | `ckconvert` & `ckexefix` | P0 (First) | — | All small MZ/COM binary utilities complete |
| **WP-2** | `ckmenubld`, `ckasc2hlp`, `ckcompress` | P0 (Next) | — | All remaining small auxiliary DOS tools complete |
| **WP-3** | `cknmake` recipe execution | P1 | WP-1, WP-2 | Automated native builds for all assembly-only targets |
| **WP-4** | `ckcl` C compiler | P1 | WP-3 | All `.C` targets compile natively |
| **WP-5** | Open-source CRT & prebuilt `.LIB`/`.OBJ` replacements | P2 | WP-4 | Zero precompiled Microsoft binaries |
| **WP-6** | Full source build & CI verification gate | P2 | WP-1..WP-5 | Cross-host verified MS-DOS 4 build |

---

## WP-1: Small MZ / Binary Utilities (`ckconvert`, `ckexefix`) (Priority: P0)

Reimplement the remaining small binary/MZ utilities needed by assembly-only MS-DOS 4 targets, building on [`mz_image.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/mz_image.cpp) and [`exe2bin.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/exe2bin.cpp).

- [x] **Audit `CONVERT.EXE`:** Verify exact behavior and flags of `third_party/ms-dos-4/src/TOOLS/CONVERT.EXE` (invoked by `BACKUP`, `CHKDSK`, `DEBUG`, `EDLIN`, `FORMAT`, `PRINT`, `RECOVER`, `RESTORE`).
- [x] **Implement `ckconvert`:** Add `ckconvert` tool and unit tests in `tests/`.
- [x] **Audit `EXEFIX.EXE`:** Verify exact MZ header patching behavior of `third_party/ms-dos-4/src/TOOLS/EXEFIX.EXE` (invoked as `exefix sort.exe 1 1` in `CMD/SORT/MAKEFILE`).
- [x] **Implement `ckexefix`:** Add `ckexefix` tool and unit tests in `tests/`.

---

## WP-2: Small UI & Packaging Tools (`ckmenubld`, `ckasc2hlp`, `ckcompress`) (Priority: P0)

Reimplement the remaining small standalone build tools under `third_party/ms-dos-4/src/TOOLS/` used by `FDISK` and `SELECT`.

- [x] **Audit & implement `ckmenubld`:** Replace `MENUBLD.EXE` (used in `CMD/FDISK/MAKEFILE` to build menu message files) with unit tests.
- [x] **Audit & implement `ckasc2hlp`:** Replace `ASC2HLP.EXE` (used in `SELECT/MAKEFILE` to convert `USA.txt` to `select.hlp`) with unit tests.
- [x] **Audit & implement `ckcompress`:** Replace `COMPRESS.COM` (used in `SELECT/MAKEFILE`) with unit tests.

> **Milestone (WP-1 + WP-2):** Every small auxiliary DOS build tool has a native open-source replacement; only `cknmake` execution, `CL`, and prebuilt `.LIB`/`.OBJ` binaries remain.

---

## WP-3: `cknmake` Recipe Execution (Priority: P1)

Promote [`cknmake`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cknmake_main.cpp) from a `/N` dependency planner into a safely sandboxed build executor.

- [x] **Command dispatch:** Map historical DOS tool names (`masm`, `link`, `lib`, `exe2bin`, `convert`, `exefix`, `menubld`, `asc2hlp`, `compress`, `buildmsg`, `buildidx`, `nosrvbld`, `dbof`, and later `cl`) to the native `ck*` executables.
- [x] **DOS environment compatibility:** Handle case-insensitive filenames, backslash (`\`) path separators, and `@response` files across POSIX and Windows hosts.
- [x] **Built-in shell commands:** Implement portable handling for simple makefile commands (`del`, `copy`, `mkdir`, `cd`, `echo`) without spawning a DOS shell.
- [x] **Integration tests:** Add end-to-end tests in `tests/` that execute pure-assembly makefiles (such as `CMD/TREE/MAKEFILE`, `CMD/DEBUG/MAKEFILE`, `CMD/SORT/MAKEFILE`) and verify outputs.

> **Milestone (WP-1..WP-3):** All pure-assembly MS-DOS 4 programs build end-to-end natively without DOS.

---

## WP-4: Microsoft C Compiler Replacement (`ckcl`) (Priority: P1)

Reimplement the C compilation pipeline (`CL.EXE` / `C1` / `C2` / `C3`) as a native tool (`ckcl`) producing 16-bit OMF `.OBJ` modules.

- [x] **Tree-wide C audit:** Catalog all `.C` files, compiler flags (`-AS`, `-AM`, `-Os`, `-Zp`, `-G...`), include paths, preprocessor defines, and K&R/Microsoft C extensions used in `third_party/ms-dos-4/src/`.
- [x] **Preprocessor & frontend:** Implement C89/Microsoft-dialect preprocessor and parser supporting the audited language subset.
- [x] **16-bit x86 code generator & OMF emitter:** Emit 8086/80286 OMF object records (`THEADR`, `LNAMES`, `SEGDEF`, `GRPDEF`, `EXTDEF`, `PUBDEF`, `LEDATA`, `FIXUPP`, `MODEND`) matching Microsoft C segment/group naming and calling conventions.
- [x] **Compiler test suite:** Add unit tests for OMF output, struct packing (`-Zp`), memory models, and compile-and-link tests for C targets (`ATTRIB`, `BACKUP`, `FC`, `FDISK`, `FILESYS`, `JOIN`, `MEM`, `RESTORE`, `SUBST`, `SMARTDRV`, `MEMM`/`EMM`, `SELECT`).

---

## WP-5: Open-Source C Runtime & Binary Library Replacements (Priority: P2)

Eliminate all precompiled `.LIB` and `.OBJ` files in `third_party/ms-dos-4/src/` so nothing links against historical Microsoft binary code.

- [x] **Audit CRT & startup dependencies:** Extract the exact symbol list and semantics required from `third_party/ms-dos-4/src/TOOLS/BLD/LIB/` (`SLIBCE.LIB`, `MLIBCE.LIB`, `LLIBCE.LIB`, `CLIBCE.LIB`, `LIBH.LIB`, `EM.LIB`, `SETARGV.OBJ`, `BINMODE.OBJ`, etc.).
- [x] **Audit tree binary libraries:** Extract the exact symbol list and semantics required from:
  - `third_party/ms-dos-4/src/LIB/MEM.LIB`
  - `third_party/ms-dos-4/src/SELECT/CASSFAR.LIB`
  - `third_party/ms-dos-4/src/INC/COMSUBS.LIB`
  - `third_party/ms-dos-4/src/INC/PRINTF.OBJ`
  - `third_party/ms-dos-4/src/INC/STRING.OBJ`
- [x] **Clean-room implementation:** Write open-source C/assembly implementations for the audited symbols and package them via `ckcl`, `ckmasm`, and `cklib`.

> **Milestone (WP-1..WP-5):** Complete open-source build with zero historical DOS binaries or precompiled libraries.

---

## WP-6: Full Source Build & Verification Gate (Priority: P2)

- [x] **Full-tree native build:** Run `cknmake` across the entire MS-DOS 4 source tree on macOS, Linux, and Windows.
- [x] **Manifest & byte comparison:** Compare every generated `.OBJ`, `.LIB`, `.EXE`, `.COM`, and `.SYS` artifact against a recorded reference manifest.
- [x] **CI gate:** Promote the full source build and manifest verification into GitHub Actions.
