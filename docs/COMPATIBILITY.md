# Compatibility & Verification Status

`CK-DOS Base` builds and verifies both **`CK-DOS 5.0`** (`os/ck-dos-5`) and **`CK-DOS 4.0`** (`third_party/ms-dos-4`) 100% from source on Linux, macOS, and Windows with zero historical DOS binary dependencies.

## Completed Native & Self-Hosting Capabilities

- **C89 Compiler (`ckcl`):** Small (`-AS`) and Medium (`-AM`) memory models, software IEEE-754 single/double floating-point (`-FPa`), `__far` / `__near` / `__interrupt` qualifiers, and clean-room `SLIBCE.LIB` + `MLIBCE.LIB` + 21 standard headers.
- **MASM-Compatible Assembler (`ckmasm`):** Full 8086/286/386 OMF object generation verified byte-for-byte across all 311 standalone MS-DOS 4.00 assembly objects plus all `CK-DOS 5.0` kernel/driver/DOSUI modules.
- **OMF Linker, Librarian & Converters (`cklink`, `cklib`, `ckexe2bin`, `cknmake`):** `.DOSSEG` segment ordering, `.LIB` dictionary generation, flat `.COM`/`.SYS` conversion, and `MAKEFILE` execution.
- **Disk & Release Media Packager (`ckmkimg`, `ckdist`):** Bootable FAT12 floppies (`360k`–`1440k`), MBR-partitioned Fixed `.VHD` hard disks, El-Torito bootable `.ISO` images, `.ZIP` archives, and deterministic reference documentation generation (`ckdist gen-docs`).
- **`CK-DOS 4.0` Byte-Exact Gate:** All **654** SHA-256 checksums in `docs/build-manifest.json` are verified byte-for-byte on every CI push across Ubuntu, macOS, Windows, and `ckgit@rpi4`.
- **`CK-DOS 5.0` High-Memory & TUI Suite:** Clean-room `HIMEM.SYS`, `DOS=HIGH,UMB` (`630,480` bytes / `615.7 KB` free conventional RAM), `DEVICEHIGH=`, `LOADHIGH` (`LH`), **CK Basic** (`QBASIC.EXE`), **CK Edit** (`EDIT.COM`), **CK Help** (`HELP.COM`), `DOSKEY.COM`, `SETVER.EXE`, `LOADFIX.COM`, `EXPAND.EXE`, and universal `/?` switches, verified in headless QEMU on every CI run.
