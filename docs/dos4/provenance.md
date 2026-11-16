---
title: MS-DOS 4.00 Reference Provenance
description: Provenance, MIT licensing, encoding-repair lineage, and strict isolation of historical tools in third_party/ms-dos-4.
---
# MS-DOS 4.00 Reference Provenance

`CK-DOS 4.0` preserves the open-source 1988 MS-DOS 4.00 source release as an immutable, reproducible baseline on `main`.

## Upstream MIT Snapshot (`third_party/ms-dos-4`)

- **Original Release:** Published under the MIT License by Microsoft and IBM at `https://github.com/microsoft/MS-DOS`.
- **Encoding-Repaired Snapshot:** Vendored from `https://github.com/chasonr/MS-DOS.git` (`release-1`, commit `95deabd0d309ac8157f0c738a22910508de64050`, tree `a74039742f9f419a138d3eb613a285ef7ef43ccb`), restoring original CP437 byte sequences damaged by UTF-8 conversion in the initial archive.
- **License Preservation:** Retained verbatim at `third_party/ms-dos-4/LICENSE` and `NOTICE`.

## Zero Historical Binary Execution

Although the preserved `third_party/ms-dos-4` snapshot contains 1988 build tools as part of its historical archive, `ckdist` **never executes them and never ships them in native SDK packages**:

1. During `ckdist verify-build` and `ckdist release`, only `third_party/ms-dos-4/src` is staged into a clean temporary workspace.
2. Freshly compiled clean-room runtime archives (`SLIBCE.LIB`, `COMSUBS.LIB`, `CASSFAR.LIB`) are installed from `runtime/`.
3. Host-native `ckcl`, `ckmasm`, `cklink`, `cklib`, `cknmake`, `ckexe2bin`, and message builders compile all **60** source directories from scratch.
4. Every one of the **654** resulting `.OBJ`, `.LIB`, `.BIN`, `.SYS`, `.COM`, and `.EXE` outputs is verified byte-for-byte against [the pinned SHA-256 manifest](manifest-summary.md).
