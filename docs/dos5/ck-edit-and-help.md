---
title: CK Edit & CK Help
description: Full-screen mouse- and keyboard-driven text editor (EDIT.COM) and hyperlinked 63-topic command reference browser (HELP.COM) in CK-DOS 5.0.
---
# CK Edit (`EDIT.COM`) & CK Help (`HELP.COM`)

Alongside [CK Basic](ck-basic.md), `CK-DOS 5.0` ships two compact, fast `.COM` full-screen utilities built in pure 8086 assembly atop the shared **`DOSUI`** text interface library (`os/ck-dos-5/src/CMD/DOSUI/`):

- **CK Edit** (`os/ck-dos-5/src/CMD/EDIT/` → **`EDIT.COM`**, `40,477` bytes)
- **CK Help** (`os/ck-dos-5/src/CMD/HELP/` → **`HELP.COM`**, `31,736` bytes)

Both are `Copyright (c) 2026 C. Klukas`, independently engineered clean-room under the MIT License without any Microsoft MS-DOS 5.0 source code or `DOSHELP.HLP` text, and assemble directly with `ckmasm` + `cklink` + `ckexe2bin`.

## CK Edit (`EDIT.COM`)

```text
EDIT [[drive:][path]filename] [/B] [/G] [/H] [/NOHI]
EDIT /?
```

**CK Edit** provides a classic drop-down menu, mouse-aware full-screen editor for `CONFIG.SYS`, `AUTOEXEC.BAT`, batch scripts, C, and assembly sources on plain 8086 real-mode machines:

- **Menus (`Alt+F`, `Alt+E`, `Alt+S`, `Alt+O`, `Alt+H`):** `New`, `Open...` (with interactive directory & drive browser), `Save`, `Save As...`, `Exit`; `Cut`, `Copy`, `Paste`, `Clear` clipboard operations; `Find...`, `Repeat Last Find` (`F3`), `Change...`; customizable foreground/background colors, scrollbars, and tab stops saved in `QBASIC.INI`.
- **Keyboard & Mouse Support:** Standard `Shift+Arrow` selection, `Ctrl+Ins` / `Shift+Ins` / `Shift+Del` clipboard shortcuts, classic WordStar cursor diamonds (`Ctrl+E/X/S/D`), clickable scroll rails, menu headers, dialog buttons, and status-bar `<F1=Help>` hotzones via `INT 33h`.
- **Embedded Help (`EDITHLP.ASM`):** Context-sensitive survival guide and keyboard reference linked directly into `EDIT.COM`, so `F1` always works even from a single floppy with no external `.HLP` file needed.

## CK Help (`HELP.COM`)

```text
HELP [topic]
```

**CK Help** is `CK-DOS 5.0`'s standalone full-screen command documentation browser:

- **Zero External Data File:** All **63** authored CK-DOS 5.0 command topics and **68** command aliases (`os/ck-dos-5/src/CMD/HELP/topics.json` → `HELPDATA.INC`) are compiled directly into `HELP.COM` (`31 KB`), guaranteeing that `HELP` works from any directory or boot diskette without hunting for `DOSHELP.HLP`.
- **Instant Incremental Filter & Hyperlinks:**
  - Launch `HELP` to browse the alphabetical topic list on the left and live documentation pane on the right, or `HELP XCOPY` / `HELP QBASIC` to jump straight to a command.
  - Press `Tab` to focus the `Search:` bar and narrow topics as you type.
  - Press `Tab` again (or click with an `INT 33h` mouse driver) to follow `See also:` cross-reference links between related commands.

For a single-page web view of every topic embedded in `HELP.COM`, see the auto-generated [Command Reference](command-reference.md).
