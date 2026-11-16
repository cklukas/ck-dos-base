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
- `QBASIC PROGRAM.BAS` — open `PROGRAM.BAS` in the IDE
- `QBASIC /RUN PROGRAM.BAS` — execute `PROGRAM.BAS` directly and return to DOS
- `QBASIC /MBF /RUN PROGRAM.BAS` — use Microsoft Binary Format for `MKS$`/`MKD$`/`CVS`/`CVD`
- `QBASIC /EDITOR [FILE]` — launch standalone CK Edit (`EDIT.COM`)
- `QBASIC /?` — print command-line usage and copyright notice

## Language & IDE Capabilities

- Full-screen `DOSUI` editor, menu bar, `Help -> About...` dialog, mouse
  support, Immediate window (`F6`), single-stepping (`F8` / `F10`), breakpoints
  (`F9`), output screen toggle (`F4`), and `F5` run/continue.
- `INTEGER`, `LONG`, `SINGLE`, `DOUBLE`, variable-length `STRING`, fixed-length
  `STRING * n`, packed `TYPE ... END TYPE` user-defined records, dynamic `REDIM`
  and multidimensional `DIM` arrays, `SUB` and `FUNCTION` procedures with
  recursion and `STATIC` locals.
- Control flow (`IF`/`ELSEIF`/`ELSE`/`END IF`, `SELECT CASE`, `FOR`/`NEXT`,
  `DO`/`LOOP`, `WHILE`/`WEND`, `GOTO`, `GOSUB`/`RETURN`, `ON ERROR GOTO`/`RESUME`),
  `DATA`/`READ`/`RESTORE`, sequential/random/binary file I/O (`OPEN`, `FIELD`,
  `GET`, `PUT`, `LOCK`/`UNLOCK`, `LINE INPUT #`, `PRINT #`, `WRITE #`), serial
  `COM1:`..`COM4:` I/O, `ENVIRON`/`ENVIRON$`, and `SHELL`.
- Runs classic QBasic programs such as `REMLINE.BAS` unchanged on an 8086 without
  requiring an x87 math coprocessor.
