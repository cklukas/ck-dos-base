<!-- Copyright (c) 2026 C. Klukas. All rights reserved. -->
<!-- SPDX-License-Identifier: MIT -->

# CK Basic (`QBASIC.EXE`)

**CK Basic** (shipped under the classic DOS 5 executable filename `QBASIC.EXE`)
is an independent, clean-room 16-bit DOS BASIC interpreter and full-screen
`DOSUI` IDE authored by C. Klukas (`Copyright (c) 2026 C. Klukas. All rights
reserved.`) under the MIT License.

CK Basic was developed clean-room from public QBasic / QuickBASIC language
documentation and black-box behavioral specifications, with zero Microsoft
QBasic source code, disassembly, or retail help data used. It compiles 100%
from source using `ckcl -AM -FPa`, `ckmasm`, and `cklink`.

## Usage

- `QBASIC` — open the interactive full-screen CK Basic editor and Immediate window
- `QBASIC [/B] [/G] [/H] [/NOHI] [/MBF] [PROGRAM.BAS]` — open `PROGRAM.BAS` with optional monochrome (`/B`), CGA fast update (`/G`), 43/50-line high-density text (`/H`), no-high-intensity (`/NOHI`), or Microsoft Binary Format (`/MBF`) mode
- `QBASIC [/MBF] /RUN PROGRAM.BAS` — execute `PROGRAM.BAS` (returning directly to DOS on `SYSTEM` or when redirected, or entering the IDE after `END`/`STOP`/diagnostics on an interactive console)
- `QBASIC /EDITOR [/B] [/G] [/H] [/NOHI] [FILE]` — launch standalone CK Edit (`EDIT.COM`)
- `QBASIC /?` — print command-line usage and copyright notice

## Language & IDE Capabilities

- Full-screen `DOSUI` editor, `Alt` menu-bar focus, `Help -> About...` dialog,
  mouse support, split view, `SUBs` browser (`F2`), Immediate window (`F6`),
  single-stepping (`F8` / `F10`), breakpoints (`F9`), output screen toggle
  (`F4`), and `F5` / `Shift+F5` run/continue/start.
- `INTEGER`, `LONG`, `SINGLE`, `DOUBLE`, variable-length `STRING`, fixed-length
  `STRING * n`, packed `TYPE ... END TYPE` user-defined records, dynamic `REDIM`
  and multidimensional `DIM` arrays, `SUB` and `FUNCTION` procedures with
  recursion and `STATIC` locals.
- Control flow (`IF`/`ELSEIF`/`ELSE`/`END IF`, `SELECT CASE`, `FOR`/`NEXT`,
  `DO`/`LOOP`, `WHILE`/`WEND`, `GOTO`, `GOSUB`/`RETURN`, `ON expr GOTO/GOSUB`,
  `ON ERROR GOTO`/`RESUME`), event traps (`ON TIMER`, `ON PLAY`, `ON KEY`),
  `DATA`/`READ`/`RESTORE`, sequential/random/binary file I/O (`OPEN`, `FIELD`,
  `GET`, `PUT`, `LOCK`/`UNLOCK`, `LINE INPUT #`, `PRINT #`, `WRITE #`), serial
  `COM1:`..`COM4:` I/O, `ENVIRON`/`ENVIRON$`, `DATE$`/`TIME$`/`COMMAND$`, and `SHELL`.
- Graphics, sound, and bounded low-level hardware interfaces: `SCREEN` modes
  (`0`, `1`, `2`, `7`–`13`), pages and `PCOPY`, `PSET`/`PRESET`, `LINE`,
  `CIRCLE`, `PAINT`, `DRAW` MML, graphics `VIEW`/`WINDOW`/`PMAP`/`POINT`,
  `GET`/`PUT` sprites, `BEEP` (`INT 10h` `AX=0E07h`), `INT 1Ch` timer-queued
  `SOUND` and foreground/background `PLAY` (`PLAY(0)`), `DEF SEG`, `PEEK`/`POKE`,
  `INP`/`OUT`/`WAIT`, `VARSEG`/`VARPTR`/`VARPTR$`/`SADD`, `BSAVE`/`BLOAD`, and
  `CALL ABSOLUTE`.
- Runs classic QBasic programs (`MONEY.BAS`, `REMLINE.BAS`, `NIBBLES.BAS`, and
  `GORILLA.BAS`) unchanged on an 8086+ without requiring an x87 math coprocessor.
