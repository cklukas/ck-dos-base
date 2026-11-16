---
title: High Memory, UMBs & Kernel Architecture
description: How CK-DOS 5.0 achieves 615.7 KB of free conventional RAM using HIMEM.SYS, DOS=HIGH, DOS=UMB, DEVICEHIGH=, LOADHIGH, and kernel slimming.
---
# High Memory, UMBs & Kernel Architecture

The defining upgrade of `CK-DOS 5.0` over 1988-era DOS 4 is **conventional-memory liberation**: booting `CK-DOS 5.0` on a 286/386+ system with `HIMEM.SYS` and `DOS=HIGH,UMB` leaves **`630,480` bytes (`615.7 KB`)** for the largest executable program block while keeping `ANSI.SYS` and `DOSKEY.COM` resident in Upper Memory.

## 1. Kernel Slimming over MS-DOS 4.00

Before relocating a single byte to high memory, `os/ck-dos-5/` removes three major sources of conventional-memory bloat inherited from DOS 4.00:

1. **No Mandatory `SHARE.EXE` on >32 MB FAT16 Hard Disks (~13.5 KB saved):** MS-DOS 4.00 forced `SYSINIT1` to load `SHARE.EXE` whenever a FAT16 partition exceeded 32 MB so legacy 16-bit FCB calls could not corrupt 32-bit sector numbers. `CK-DOS 5.0` folds that >32 MB FCB safety check directly into `DOS/FCBIO.ASM` (`SHARE.EXE` remains available when explicit network/file locking is desired).
2. **Dead DOS 4 Subsystems Stripped (-14,295 lines):** Removes `DOS/IFS.ASM`, `DOS/EXTATTR.ASM`, `CMD/IFSFUNC/`, `CMD/FILESYS/`, and EMS `BUFFERS /X` (`SAVE_USER_MAP` on every `INT 21h` entry), while preserving the standard `INT 2Fh AX=11xxh` redirector ABI for `MSCDEX` and network drives.
3. **Lean Default Startup:** Eliminates default `FASTOPEN` and `APPEND` residency in `CONFIG.SYS` / `AUTOEXEC.BAT`.

## 2. Clean-Room `HIMEM.SYS` (`os/ck-dos-5/src/DEV/HIMEM/`)

`HIMEM.SYS` (`2,872` bytes) is a clean-room XMS 3.0 driver (`INT 2Fh AX=4300h/4310h`) providing:

- **A20 Gate Management (Functions `03h–07h`):** Supports PS/2 System Control Port A (`0x92`) fast A20 and AT 8042 keyboard controller fallback with local enable nesting.
- **High Memory Area Ownership (Functions `01h–02h`):** Arbitrates the 64 KB minus 16 bytes HMA region (`FFFF:0010h..FFFF:FFFFh`) for `DOS=HIGH`.
- **Extended Memory Blocks (Functions `08h–0Fh`, `88h–89h`):** Allocates, locks, queries, and copies extended memory above 1 MB via `INT 15h AH=87h` protected-mode block moves.
- **Upper Memory Block Provider (Functions `10h–11h`):** Scans `D000h–DFFFh` for ROM-free upper RAM (and enables shadow RAM on i440FX PAM registers when available) so `DOS=UMB` can link upper memory without needing a V86 monitor.

## 3. `DOS=HIGH` Kernel & Buffer Relocation into HMA

When `CONFIG.SYS` specifies `DEVICE=C:\DOS\HIMEM.SYS` and `DOS=HIGH`:

- `SYSCONF.ASM` initializes `HIMEM.SYS` in Pass 0 so XMS and A20 services are live before final kernel placement.
- `MSDOS.SYS` keeps only `DOSDATA` plus tiny `INT 20h–28h` A20-gating entry stubs in low conventional RAM, relocating `DOSGROUP` code and `BUFFERS=20` disk sector buffers to `FFFF:0010h`.
- `BIOS/MSDISK.ASM` transparently bounces disk transfers targeting `ES >= F000h` through a low sector buffer so ISA DMA/BIOS `INT 13h` never wraps across the 1 MB boundary, while allowing direct transfers into `D000h` UMBs.
- Unused HMA bytes (~`19 KB` after kernel + 20 buffers) are exposed via the standard DOS 5 **`INT 2Fh AX=4A01h / 4A02h`** suballocator.

## 4. `DOS=UMB`, `DEVICEHIGH=`, and `LOADHIGH` (`LH`)

When `DOS=UMB` (or `DOS=HIGH,UMB`) is active:

- `SYSINIT1` bridges the top of conventional RAM (`9FFFh`) into the UMB MCB chain (`D000h–DFFFh`) via a reserved system bridge arena, supporting `INT 21h AX=5802h/5803h` (*Get/Set UMB Link State*) and high-first allocation strategies (`0040h` / `0080h`).
- `DEVICEHIGH=` in `CONFIG.SYS` loads character/block drivers such as `ANSI.SYS` into upper memory, falling back cleanly to conventional RAM if no UMB is large enough.
- `LOADHIGH` (`LH`) in `COMMAND.COM` links UMBs and sets high-first allocation across child `EXEC` (`INT 21h AH=4Bh`) so TSRs like `DOSKEY.COM` reside in UMBs (`CS=D000h+`) with zero conventional-RAM footprint.

## 5. DOS 5.0 Kernel ABI & Resident `SETVER`

`CK-DOS 5.0` implements the documented DOS 5.0 kernel contract expected by memory managers, Windows 3.x, and DOS extenders:

| Interface | Behavior |
|---|---|
| `INT 21h AH=30h` | Returns reported version `5.00` by default, or the per-executable version configured in the resident `SETVER` table (`SETVER.EXE`). |
| `INT 21h AX=3306h` | *Get True DOS Version*: always returns `BL=5, BH=0`, `DL=0`, and `DH=10h` when DOS is resident in HMA. |
| `INT 21h AH=52h` (`SysVars`) | List of Lists aligned to exact DOS 5.0 field offsets (`+34h..+43h`: `SETVER` table pointer, `A20` fix routine offset, HMA buffer state, boot drive, 386 move flag, extended memory KB). |
| `INT 21h AX=5802h / 5803h` | Queries or updates UMB MCB chain link state (`0` = unlinked, `1` = linked). |
| `INT 21h AX=4B05h` | *Set Execution State* hook for DOS 5 version-aware loaders. |
