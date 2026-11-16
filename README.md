# CK-DOS Base

CK-DOS Base provides a complete, 100% clean-room native C/C++20
cross-compilation and packaging toolchain alongside two side-by-side 16-bit DOS
operating-system distributions built completely from source without proprietary
compilers, assemblers, or Python build scripts:

- **CK-DOS 4.0 (`third_party/ms-dos-4`)** — Frozen, reproducible MS-DOS
  4.00/4.01 reference tree verified byte-for-byte against all 654 SHA-256 hashes
  in `docs/build-manifest.json` (`ckdos-4.00-*` release media).
- **CK-DOS 5.0 (`os/ck-dos-5`)** — Independent MIT-licensed evolution of the
  open-source MS-DOS 4.00 codebase combined with clean-room, first-party
  CK-DOS 5.0 high-memory extensions (`HIMEM.SYS`, `DOS=HIGH,UMB` with **615.7 KB**
  free conventional RAM, `DEVICEHIGH=`, `LOADHIGH`/`LH`), full-screen `DOSUI`
  applications (**CK Basic** shipped as `QBASIC.EXE`, **CK Edit** shipped as
  `EDIT.COM`, and **CK Help** shipped as `HELP.COM`), and CLI utilities
  (`DOSKEY.COM`, `SETVER.EXE`, `LOADFIX.COM`, `EXPAND.EXE`, universal `/?`
  switches), containing zero historical binary tools and zero Microsoft
  MS-DOS 5.0 or QBasic code (`ckdos-5.00-*` release media).

The project has no dependency on any private or downstream product. Downstream
products may consume CK-DOS Base, while this repository remains completely
standalone.

## What is included

The native C/C++20 tools run directly on macOS, Linux, and Windows; portable
C89 cores (`src/devtools/core/*_core.c`) also compile with `ckcl` into native
16-bit DOS `.EXE` binaries (`C:\DEVTOOLS`).

| Tool | Role |
| --- | --- |
| `ckcl` | 16-bit 8086 C89 compiler (`-AS` small & `-AM` medium model + clean-room IEEE-754 float) |
| `ckmasm` | MASM-compatible 16-bit x86 assembler |
| `cklink` | Microsoft-compatible OMF linker (`DOSSEG`, communals, `/EXEPACK`) |
| `cklib` | OMF object library builder |
| `cknmake` | NMAKE-compatible build planner and recipe executor |
| `ckexe2bin`, `ckconvert`, `ckexefix` | MZ executable to `.BIN`/`.COM` converters and header fixer |
| `ckcompress`, `ckmenubld`, `ckasc2hlp` | `SELECT` installer RLE compressor, menu builder, and help compiler |
| `ckbuildidx`, `cknosrvbld`, `ckbuildmsg`, `ckdbof` | DOS message & boot-sector build utilities |
| `ckmkimg` (`CKMKIMG.EXE` on DOS) | Portable C89 FAT12/FAT16 floppy, MBR/VHD hard-disk, El-Torito ISO, and ZIP image builder |
| `ckdist` | Zero-Python dual-OS (`CK-DOS 4.0` + `CK-DOS 5.0`) builder, SHA-256 manifest verifier, and release media packager |
| `ckmzdump`, `ckomfdump`, `cklibdump` | MZ, OMF, and library binary inspectors |

Provenance and licensing for both `third_party/ms-dos-4` and `os/ck-dos-5` are
documented in [docs/PROVENANCE.md](docs/PROVENANCE.md).

## Build, verify, and package release media

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure

# Build both CK-DOS 4.0 (verifying all 654 SHA-256 manifest hashes) and
# CK-DOS 5.0, build 16-bit self-hosting DOS tools, and emit all release media
# into dist/ (--os defaults to all; pass --os dos4 or --os dos5 to select one):
./build/ckdist release \
  --os all \
  --repo-root . \
  --tool-dir build \
  --verify-manifest docs/build-manifest.json \
  --output dist
```

### Release artifacts (`dist/`)

- `ckdos-5.00-floppy-1440k.img` & `ckdos-4.00-floppy-1440k.img` — 3.5" 1.44 MB bootable & installable FAT12 floppies (`INSTALL.BAT` installs DOS onto `C:`)
- `ckdos-5.00-floppy-720k-disk1..3.img` (`3×720 KB` set, including CK Basic Disk 3) & `ckdos-4.00-floppy-720k-disk1..2.img` (`2×720 KB` set) — 720 KB floppy sets for vintage PC/XT and early PS/2 systems
- `ckdos-5.00-hdd-32m.vhd` & `ckdos-4.00-hdd-32m.vhd` — Ready-to-boot 32 MB Fixed VHD hard disks with `C:\DOS` and 16-bit self-hosting `C:\DEVTOOLS`
- `ckdos-5.00-boot.iso` & `ckdos-4.00-boot.iso` — El-Torito bootable CD-ROM ISO-9660 images
- `ckdos-5.00-files.zip` & `ckdos-4.00-files.zip` — Complete DOS + `DEVTOOLS` directory archives for DOSBox-X, XT-IDE CF, and SD cards
- `ck-dos-sdk-<version>-<os>-<arch>.zip` — Host cross-compiler binaries, clean-room `runtime/`, and 16-bit `.LIB` archives
- `SHA256SUMS` — Deterministic SHA-256 checksums

`ckmkimg` (and its 16-bit DOS counterpart `CKMKIMG.EXE`) can also build custom
floppy, VHD, ISO, or ZIP images directly on host or from inside DOS:

```sh
./build/ckmkimg floppy -s 1440k -l CKDOS500 -b MSBOOT.BIN -o boot.img IO.SYS MSDOS.SYS COMMAND.COM
```

### Use as a CMake subproject

```cmake
set(DOSBASE_BUILD_TESTS OFF)
set(DOSBASE_INSTALL OFF)
add_subdirectory(external/ck-dos-base)
target_link_libraries(my_target PRIVATE dosbase::devtools)
```

## Documentation Portal (`ckdocs`) & Auto-Generated Reference

Documentation is built with [`ckdocs`](https://github.com/cklukas/ck-git-hosting) as a **two-site portal** (`ckdocs.yml`) — one site for **CK-DOS 5.0** (`docs/dos5/`) and one site for **CK-DOS 4.0 & SDK** (`docs/dos4/`) — and published automatically on both `ckgit` Pages and GitHub Pages:

```sh
# Regenerate (or verify with --check) the 63-command CK-DOS 5.0 reference
# and 654-hash CK-DOS 4.0 manifest summary from topics.json & build-manifest.json:
./build/ckdist gen-docs --repo-root .

# Validate and preview the two-site documentation portal locally:
ckdocs check --root .
ckdocs serve --root .
```

## Continuous integration and releases

Every push runs native builds, unit tests, `ckdist gen-docs --check`, `ckdocs --strict` portal validation, full 654-hash `CK-DOS 4.0` manifest verification, full `CK-DOS 5.0` clean-room source compilation (including **CK Basic** `QBASIC.EXE`, **CK Edit** `EDIT.COM`, **CK Help** `HELP.COM`, and `HIMEM.SYS`), dual-OS release media packaging, headless QEMU boot checks for both OS lines, and automatic `ckdocs` Pages deployment on both `ckgit@rpi4` (`.ckgit/ci.yml`) and GitHub Actions (`.github/workflows/build.yml` & `.github/workflows/pages.yml`). Pushing an annotated version tag (`v*` matching `VERSION`) publishes durable multi-platform SDK and dual-OS media releases on both channels.

## Independence and trademarks

CK-DOS Base is independently developed and maintained by C. Klukas. It is not
affiliated with, endorsed by, or sponsored by Microsoft. Microsoft, MS-DOS, and
related names may be trademarks of Microsoft Corporation. The names “CK-DOS
Base”, “CK-DOS 4.0”, and “CK-DOS 5.0” identify this independent project; they
do not identify a Microsoft product.

See [LICENSE](LICENSE) and [NOTICE](NOTICE).
