# Source provenance

## Native tools and clean-room runtime

The code under `include/dosbase`, `src/devtools`, `runtime`, and `tests` is an
independent clean-room implementation. Compatibility behavior was established by
comparing small, purpose-built inputs and outputs with legitimately obtained
historical tools and public specifications. Historical executable files are not
linked into, embedded in, or needed at runtime by the native tools.

## Dual-OS architecture (`CK-DOS 4.0` and `CK-DOS 5.0`)

CK-DOS Base builds and packages two side-by-side 16-bit DOS distributions on
`main`:

1. **`CK-DOS 4.0` (`third_party/ms-dos-4`)** — Frozen, reproducible MS-DOS
   4.00/4.01 reference tree gated byte-for-byte against all 654 SHA-256 artifact
   hashes in `docs/build-manifest.json`, emitting `ckdos-4.00-*` release media.
2. **`CK-DOS 5.0` (`os/ck-dos-5`)** — Independent MIT-licensed evolution of the
   open-source MS-DOS 4.00 codebase combined with clean-room, first-party
   CK-DOS 5.0 kernel enhancements and utilities, emitting `ckdos-5.00-*`
   release media.

## MS-DOS 4 source snapshot (`third_party/ms-dos-4`)

Microsoft published the MS-DOS 4.0 source under the MIT License at:

- <https://github.com/microsoft/MS-DOS>

Some files in the published archive contain Unicode replacement characters
where the original byte stream was damaged. This repository preserves the
repair release maintained at:

- Repository: <https://github.com/chasonr/MS-DOS.git>
- Tag: `release-1`
- Commit: `95deabd0d309ac8157f0c738a22910508de64050`
- Tree: `a74039742f9f419a138d3eb613a285ef7ef43ccb`

That repair restores damaged bytes, original line endings, and build scripts;
it is not presented as a new operating-system implementation. The vendored
snapshot's MIT license is retained at `third_party/ms-dos-4/LICENSE`.

The snapshot includes historical build executables because they are part of
the licensed upstream tree. CK-DOS Base CI does not redistribute them in native
tool packages, and the native tools do not invoke them.

## CK-DOS 5.0 source tree (`os/ck-dos-5`)

`os/ck-dos-5` was seeded from the MIT-licensed `third_party/ms-dos-4/src`
sources with all historical binary tools (`.EXE`, `.COM`) and prebuilt object
or library binaries (`.OBJ`, `.LIB`, `.SYS`) stripped prior to import. All
runtime libraries (`SLIBCE.LIB`, `COMSUBS.LIB`, `CASSFAR.LIB`) are built from
the first-party clean-room sources in `runtime/`.

Every DOS 5.0-compatible kernel extension, memory tool, and full-screen utility
added under `os/ck-dos-5` — including **CK Edit** (`EDIT.COM`), **CK Basic**
(`QBASIC.EXE`), **CK Help** (`HELP.COM`), and the CK-DOS XMS/memory utilities
(`HIMEM.SYS`, `DOSKEY.COM`, `SETVER.EXE`, `LOADFIX.COM`, `EXPAND.EXE`), all
`Copyright (c) 2026 C. Klukas` — is an original clean-room implementation
authored from public MS-DOS 5.0 / QBasic / IBM PC BIOS / LIM EMS / XMS
documentation and black-box behavioral specifications, shipped under classic
compatible executable filenames for DOS batch and CLI interoperability. Zero
Microsoft MS-DOS 5.0 or QBasic source code, disassembly, help text, or binary
code is used. Licensing is recorded in `os/ck-dos-5/LICENSE`.

## Clean public boundary

This repository is self-contained. Its source files, tests, workflows, and
documentation must not depend on private downstream repositories. A downstream
product may import a released CK-DOS Base version; dependencies never point in
the reverse direction.
