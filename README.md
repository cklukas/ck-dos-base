# CK-DOS Base

CK-DOS Base is an independent, open foundation for reproducible MS-DOS 4
build research. It combines a preserved MS-DOS 4 source snapshot with native,
clean-room implementations of the historical build tools needed to inspect
and reproduce its artifacts.

The project has no dependency on any private or downstream product. Downstream
products may consume CK-DOS Base, but this repository does not consume or refer
to their source, build system, assets, or services.

## What is included

The native C++20 tools run directly on macOS, Linux, and Windows; DOS or a DOS
virtual machine is not required to run them.

| Tool | Historical role |
| --- | --- |
| `ckmasm` | MASM-compatible 16-bit assembler |
| `cklink` | Microsoft-compatible OMF linker |
| `cklib` | OMF library builder |
| `ckexe2bin` | MZ executable to binary/COM converter |
| `cknmake` | NMAKE-compatible dependency planner (`/N` mode) |
| `ckdbof` | DBOF conversion tool |
| `ckbuildidx`, `cknosrvbld`, `ckbuildmsg` | MS-DOS message-build utilities |
| `ckmzdump`, `ckomfdump`, `cklibdump` | MZ, OMF, and library inspectors |

The MS-DOS 4 source snapshot is under `third_party/ms-dos-4`. Its provenance
and licensing are recorded in [docs/PROVENANCE.md](docs/PROVENANCE.md).

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

Install into a local directory with:

```sh
cmake --install build --config Release --prefix package
```

GitHub Actions builds and tests the tools on macOS, Linux, and Windows, then
publishes the installed tools as downloadable workflow artifacts.

## Current boundary

The native assembler reproduces the standalone assembly objects exercised by
the MS-DOS 4 tree, and the repository includes the supporting linker, librarian,
converter, message tools, inspectors, and build planner. The Microsoft C
compiler (`CL`) has not been reimplemented. `cknmake` currently plans commands
but does not execute a complete build by itself.

Consequently, CI produces tested native tool packages today. It does not claim
to produce a complete, byte-identical MS-DOS 4 distribution yet. Completing the
remaining C compiler and end-to-end orchestration is tracked in
[docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

## Independence and trademarks

CK-DOS Base is independently developed and maintained by C. Klukas. It is not
affiliated with, endorsed by, or sponsored by Microsoft. Microsoft, MS-DOS, and
related names may be trademarks of Microsoft Corporation. The name “CK-DOS
Base” identifies this independent project; it does not identify a Microsoft
product.

See [LICENSE](LICENSE) and [NOTICE](NOTICE).
