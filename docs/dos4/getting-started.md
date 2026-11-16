---
title: Getting Started with CK-DOS 4.0
description: Build and byte-verify all 654 MS-DOS 4.00 artifacts natively, package the 5 bootable media formats, and boot in QEMU.
---
# Getting Started with CK-DOS 4.0

## Build & Verify All 654 Artifacts

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure

# Rebuild all 60 MS-DOS 4.00 directories from source and verify all 654 SHA-256 hashes:
./build/ckdist verify-build \
  --os dos4 \
  --repo-root . \
  --tool-dir build \
  --verify-manifest docs/build-manifest.json
```

To package all `CK-DOS 4.0` (and `CK-DOS 5.0`) release images plus the host SDK archive into `dist/`:

```sh
./build/ckdist release \
  --os all \
  --repo-root . \
  --tool-dir build \
  --verify-manifest docs/build-manifest.json \
  --output dist
```

## Release Media (`dist/ckdos-4.00-*`)

| Artifact | Format | Description |
|---|---|---|
| `ckdos-4.00-floppy-1440k.img` | 3.5" 1.44 MB FAT12 floppy | Bootable MS-DOS 4.00 diskette with `INSTALL.BAT` and all utilities/drivers |
| `ckdos-4.00-floppy-720k-disk1.img` / `disk2.img` | 2×720 KB FAT12 floppy set | Vintage IBM PC/XT & PS/2 Model 25/30 two-diskette set |
| `ckdos-4.00-hdd-32m.vhd` | 32 MB Fixed VHD hard disk | Ready-to-boot FAT16 hard drive with `C:\DOS`, `C:\DEV`, and 16-bit `C:\DEVTOOLS` |
| `ckdos-4.00-boot.iso` | El-Torito ISO-9660 CD-ROM | Bootable optical disc image with full DOS + SDK file tree |
| `ckdos-4.00-files.zip` | ZIP archive | Complete file tree for XT-IDE CompactFlash, SD adapters, or emulator directory mounts |

## Booting in QEMU

```sh
# 32 MB pre-installed hard disk:
qemu-system-i386 -drive file=dist/ckdos-4.00-hdd-32m.vhd,format=vpc -boot c

# 1.44 MB boot/install diskette:
qemu-system-i386 -fda dist/ckdos-4.00-floppy-1440k.img -boot a

# Automated headless verification of all 4 bootable formats:
./build/ckdist verify-boot --os dos4 --dist-dir dist
```
