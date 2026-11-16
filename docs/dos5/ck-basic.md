---
title: CK Basic (QBASIC.EXE)
description: Complete guide to CK Basic — the clean-room full-screen BASIC IDE and structured QuickBASIC-compatible interpreter shipped as QBASIC.EXE in CK-DOS 5.0.
---
# CK Basic (`QBASIC.EXE`)

**CK Basic** (`os/ck-dos-5/src/CMD/QBASIC/`, shipped under the classic DOS 5 executable filename **`QBASIC.EXE`**) is `CK-DOS 5.0`'s full-screen BASIC programming environment and structured QuickBASIC-compatible interpreter.

> [!NOTE]
> **Authorship & Clean-Room Provenance:** **CK Basic** (`QBASIC.EXE`) is `Copyright (c) 2026 C. Klukas`, licensed under the MIT License. It was developed 100% clean-room from public QuickBASIC 4.5 / QBasic 1.1 language specifications and black-box behavioral tests, containing zero Microsoft QBasic or MS-DOS 5.0 source code, binary code, or help text. It compiles natively from C89 and 8086 assembly via `ckcl -AM` + `MLIBCE.LIB` + `ckmasm` + `cklink`.

## Launching CK Basic

```text
QBASIC [/B] [/EDITOR] [/G] [/H] [/MBF] [/NOHI] [[/RUN] [drive:][path]filename]
QBASIC /?
```

| Switch | Behavior |
|---|---|
| `filename` | Opens the full-screen **CK Basic** IDE with `filename` loaded in the module editor (defaults to `.BAS`). |
| `/RUN filename` | Loads and executes `filename` immediately, returning to the DOS prompt when the program finishes (`END` or `SYSTEM`). |
| `/MBF` | Selects Microsoft Binary Format (`CVDMBF`/`MKSMBF`) floating-point conversion semantics for `/RUN` compatibility with legacy data files. |
| `/EDITOR [file]` | Delegates directly to **CK Edit** (`EDIT.COM`), matching DOS 5's `EDIT` ↔ `QBASIC /EDITOR` workflow. |
| `/B` | Renders the DOSUI interface using monochrome attributes on color adapters. |
| `/G` | Enables fast direct video RAM updates on CGA adapters. |
| `/H` | Selects the highest character-row text resolution supported by EGA/VGA (`80×43` / `80×50`). |
| `/NOHI` | Adapts colors for displays that do not support high-intensity background attributes. |
| `/?` | Prints the **CK Basic** version, copyright, clean-room notice, and switch summary. |

## Full-Screen IDE & Debugger

Built on the shared 16-bit `DOSUI` text windowing engine, the **CK Basic** IDE provides:

- **Split Editor & Immediate Window:** Edit `.BAS` source modules in the upper pane and press `F6` (`Shift+F6`) to evaluate live BASIC expressions and statements in the lower `Immediate` pane.
- **Procedure View (`F2`):** Browse, inspect, and switch between `MAIN` and individual `SUB` or `FUNCTION` procedures in isolation, while saving a single unified `.BAS` file on disk.
- **Interactive Stepping & Breakpoints:**
  - `F5` — Run / Continue execution (`Shift+F5` restarts from the top).
  - `F8` — Step one statement (into `SUB`/`FUNCTION` calls).
  - `F10` — Procedure step (step over calls).
  - `F9` — Toggle breakpoint on the current line.
  - `F4` — Toggle between the IDE and the user program's output screen.
  - `Ctrl+Break` — Interrupt a running program and highlight the active line in the editor.
- **Search, Replace & Persistent Preferences:** Full `Find`, `Repeat Last Find (`F3`)`, and `Change` dialogs, plus color/display preferences persisted in `QBASIC.INI`.
- **Built-In Help & About Dialog:** Press `F1` on any BASIC keyword or menu item for instant offline syntax help, or open `Help → About...` for version and clean-room copyright details.

## Language & Runtime Capabilities

**CK Basic** supports classic line-numbered GW-BASIC programs (including unmodified `REMLINE.BAS`) as well as modern line-number-free structured QuickBASIC programs:

- **Data Types:** `INTEGER` (`%`), `LONG` (`&`), IEEE-754 `SINGLE` (`!`) and `DOUBLE` (`#`) software floating-point (runs on any 8086/286/386 without requiring an 8087 FPU!), variable-length `STRING` (`$`), fixed-length `STRING * n`, and user-defined `TYPE ... END TYPE` records.
- **Control Flow:** Block `IF ... ELSEIF ... ELSE ... END IF`, `SELECT CASE` (`IS`, ranges, comma lists), `DO [WHILE|UNTIL] ... LOOP [WHILE|UNTIL]`, `FOR ... NEXT`, `WHILE ... WEND`, `GOSUB ... RETURN`, `ON expr GOTO/GOSUB`, and `ON ERROR GOTO` / `RESUME` (`RESUME`, `RESUME NEXT`, `RESUME label`) with `ERR` and `ERL`.
- **Procedures & Scoping:** True recursive `SUB` and `FUNCTION` procedures, `DECLARE`, `SHARED`, `DIM SHARED`, `COMMON SHARED`, `STATIC`, `REDIM`, `OPTION BASE 0|1`, `LBOUND`/`UBOUND`, and `ERASE`.
- **Math & Strings:** Full transcendental math (`SIN`, `COS`, `TAN`, `ATN`, `EXP`, `LOG`, `SQR`, `ABS`, `SGN`, `INT`, `FIX`, `CINT`, `CLNG`, `CSNG`, `CDBL`, `RND`/`RANDOMIZE`), formatted `PRINT USING`, `LSET`/`RSET`, `MID$` assignment, `INSTR`, `LTRIM$`/`RTRIM$`, `UCASE$`/`LCASE$`, `HEX$`/`OCT$`, `STR$`/`VAL`, and `MK*$`/`CV*` binary packers.
- **Files, Devices & System Integration:** `OPEN` for `INPUT`, `OUTPUT`, `APPEND`, `RANDOM` (`FIELD`, `GET`, `PUT`), and `BINARY` (`SEEK`, `LOC`, `LOF`, `EOF`, `FREEFILE`), serial `COM1:`/`COM2:` I/O, `LPRINT`, `SHELL`, `ENVIRON`/`ENVIRON$`, `COMMAND$`, `TIMER`, `SOUND`/`BEEP`, and `PEEK`/`POKE`/`INP`/`OUT`.

## Source Layout (`os/ck-dos-5/src/CMD/QBASIC/`)

| Module | Responsibility |
|---|---|
| `QBASIC.C` | CLI switch parser (`/?`, `/RUN`, `/MBF`, `/EDITOR`, `/B`, `/G`, `/H`, `/NOHI`) and DOS memory setup |
| `CKBIDE.C`, `CKBFIND.C`, `CKBPROC.C`, `CKBHELP.C`, `CKBINI.C` | Full-screen `DOSUI` IDE, `SUB`/`FUNCTION` procedure manager, search/replace, built-in keyword help, `About CK Basic`, and `QBASIC.INI` |
| `CKBRUN.C` | Tokenizer, AST/bytecode evaluator, recursive procedure frames, arrays, `TYPE` records, `ON ERROR`, and debugger hooks |
| `CKBNUM.C`, `CKBFMT.C`, `CKBSTR.C` | Exact numeric parsing, IEEE-754 / MBF conversions, `PRINT USING` formatter, and dynamic/fixed string engine |
| `CKBCON.C`, `CKBSER.C`, `CKENV.C`, `CKBUI.ASM` | BIOS/DOS console & screen modes, `INT 14h` / UART serial `COMn:` support, DOS environment, and 16-bit `cdecl` far assembly bridges to `DOSUI.OBJ` |
