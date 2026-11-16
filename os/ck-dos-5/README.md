# CK-DOS 5.0 (`os/ck-dos-5`)

`os/ck-dos-5` is an independent, MIT-licensed 16-bit DOS operating system tree
evolved from the open-source MS-DOS 4.00 codebase together with clean-room,
first-party CK-DOS 5.0 kernel enhancements and full-screen utilities.

- **Zero proprietary binary tools or prebuilt objects:** Every `.EXE`, `.COM`,
  `.SYS`, `.BIN`, `.OBJ`, and `.LIB` artifact is compiled and linked from source
  using the native CK-DOS Base toolchain (`ckcl`, `ckmasm`, `cklink`, `cklib`,
  `cknmake`, `ckexe2bin`, `ckmkimg`, `ckdist`) and the clean-room `runtime/`
  libraries.
- **First-party full-screen DOSUI applications:**
  - `src/CMD/DOSUI/` — `DOSUI-0` 8086 text-mode UI, menu, modal dialog, and
    `INT 33h` mouse framework.
  - `src/CMD/EDIT/` — `EDIT.COM` (also installed as `CKEDIT.COM`), a clean-room
    full-screen ASCII text editor with pull-down menus, mouse navigation,
    clipboard, search/replace, and embedded context help (`EDITHLP.ASM`).
  - `src/CMD/HELP/` — `HELP.COM`, a clean-room full-screen command reference
    browser built from the authored topic corpus in `src/CMD/HELP/topics.json`
    (`HELPDATA.INC`).
- **Zero Microsoft MS-DOS 5.0 source or binary code:** All DOS 5.0-compatible
  features and full-screen utilities in `os/ck-dos-5` are original clean-room
  implementations authored from public specifications, built atop the MIT
  MS-DOS 4.00 open-source ancestor.
- **Frozen MS-DOS 4.00 reference preserved separately:** The unmodified,
  654-hash-verified MS-DOS 4.00 source tree remains untouched in
  `third_party/ms-dos-4`.
