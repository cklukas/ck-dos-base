<!-- Copyright (c) 2026 C. Klukas. All rights reserved. -->
<!-- SPDX-License-Identifier: MIT -->

# CK-DOS QBasic (`QBASIC.EXE`)

`QBASIC.EXE` is a clean-room
16-bit DOS QBasic-compatible interpreter and full-screen `DOSUI` IDE compiled
natively with `ckcl -AM -FPa`, `ckmasm`, and `cklink`.

## Usage

- `QBASIC` — open the interactive full-screen editor and Immediate window
- `QBASIC PROGRAM.BAS` — open `PROGRAM.BAS` in the IDE
- `QBASIC /RUN PROGRAM.BAS` — execute `PROGRAM.BAS` directly and return to DOS
- `QBASIC /MBF /RUN PROGRAM.BAS` — use Microsoft Binary Format for `MKS$`/`MKD$`/`CVS`/`CVD`
- `QBASIC /EDITOR [FILE]` — launch standalone `EDIT.COM`

## Language & IDE Capabilities

- Full-screen `DOSUI` editor, menu bar, mouse support, Immediate window (`F6`),
  single-stepping (`F8` / `F10`), breakpoints (`F9`), output screen toggle (`F4`),
  and `F5` run/continue.
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
