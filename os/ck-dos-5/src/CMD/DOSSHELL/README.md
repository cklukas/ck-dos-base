<!-- Copyright (c) 2026 C. Klukas. All rights reserved. -->
<!-- SPDX-License-Identifier: MIT -->

# CK-DOS Shell (`DOSSHELL.COM` + `DOSSHELL.EXE`)

Clean-room 16-bit DOS file and program manager for CK-DOS 5.0 (`D5.8`), built
from first-party C (`ckcl -AM -Os`) and 8086 assembly (`ckmasm`) over `DOSUI`:

- `DOSSHELL.ASM` -> `DOSSHELL.COM`: compact `.COM` entry launcher (~600 bytes
  resident MCB) resolving and invoking `DOSSHELL.EXE` via `INT 21h/4B00h`.
- `DSHUI.ASM` + `DSHMAIN.C` + `DSHFS.C` + `DSHDRAW.C` + `DSHDLG.C` +
  `DSHPROG.C` -> `DOSSHELL.EXE`: full-screen `80x25` and `80x50` (`/T:H`) visual
  shell supporting `/T`, `/B` monochrome, `/?` usage, all five `View` layouts,
  collapsible `Directory Tree`, wildcard filtering and sorting, ASCII/Hex file
  viewer (`F9`), Copy (`F8`), Move (`F7`), Delete (`Del` with Read-Only
  protection), Rename, Create Directory, Change Attributes, Search, Program
  Manager (`Main` & groups) with `DOSSHELL.INI` persistence, low-overhead child
  `EXEC` & `Shift+F9` Command Prompt, `INT 33h` mouse input, and built-in Help.
