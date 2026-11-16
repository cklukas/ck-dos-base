# Work Packages: Complete Open-Source Build Toolchain

This document breaks down the remaining work required to build the preserved MS-DOS 4 source tree (`third_party/ms-dos-4/src`) natively on macOS, Linux, and Windows using only open-source tools—with zero reliance on historical 16-bit DOS executables or precompiled `.LIB`/`.OBJ` binaries.

**Ordering principle:** Small, self-contained tools first, followed by build orchestration, then the C compiler and runtime libraries.

## Priority & Ordering Overview

```text
WP-1: ckconvert & ckexefix (P0)
  └──► WP-2: ckmenubld, ckasc2hlp, ckcompress (P0)
         └──► WP-3: cknmake execution (P1)
                └──► WP-4: ckcl C compiler (P1)
                       └──► WP-5: Open-source CRT & libs (P2)
                              └──► WP-6: Full build & CI gate (P2)
```

| ID | Work Package | Priority | Depends On | Milestone Unlocked |
| --- | --- | --- | --- | --- |
| **WP-1** | `ckconvert` & `ckexefix` | P0 (First) | — | All small MZ/COM binary utilities complete |
| **WP-2** | `ckmenubld`, `ckasc2hlp`, `ckcompress` | P0 (Next) | — | All remaining small auxiliary DOS tools complete |
| **WP-3** | `cknmake` recipe execution | P1 | WP-1, WP-2 | Automated native builds for all assembly-only targets |
| **WP-4** | `ckcl` C compiler | P1 | WP-3 | All `.C` targets compile natively |
| **WP-5** | Open-source CRT & prebuilt `.LIB`/`.OBJ` replacements | P2 | WP-4 | Zero precompiled Microsoft binaries |
| **WP-6** | Full source build & CI verification gate | P2 | WP-1..WP-5 | Cross-host verified MS-DOS 4 build |

---

## WP-1: Small MZ / Binary Utilities (`ckconvert`, `ckexefix`) (Priority: P0)

Reimplement the remaining small binary/MZ utilities needed by assembly-only MS-DOS 4 targets, building on [`mz_image.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/mz_image.cpp) and [`exe2bin.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/exe2bin.cpp).

- [x] **Audit `CONVERT.EXE`:** Verify exact behavior and flags of `third_party/ms-dos-4/src/TOOLS/CONVERT.EXE` (invoked by `BACKUP`, `CHKDSK`, `DEBUG`, `EDLIN`, `FORMAT`, `PRINT`, `RECOVER`, `RESTORE`).
- [x] **Implement `ckconvert`:** Add `ckconvert` tool and unit tests in `tests/`.
- [x] **Audit `EXEFIX.EXE`:** Verify exact MZ header patching behavior of `third_party/ms-dos-4/src/TOOLS/EXEFIX.EXE` (invoked as `exefix sort.exe 1 1` in `CMD/SORT/MAKEFILE`).
- [x] **Implement `ckexefix`:** Add `ckexefix` tool and unit tests in `tests/`.

---

## WP-2: Small UI & Packaging Tools (`ckmenubld`, `ckasc2hlp`, `ckcompress`) (Priority: P0)

Reimplement the remaining small standalone build tools under `third_party/ms-dos-4/src/TOOLS/` used by `FDISK` and `SELECT`.

- [x] **Audit & implement `ckmenubld`:** Replace `MENUBLD.EXE` (used in `CMD/FDISK/MAKEFILE` to build menu message files) with unit tests.
- [x] **Audit & implement `ckasc2hlp`:** Replace `ASC2HLP.EXE` (used in `SELECT/MAKEFILE` to convert `USA.txt` to `select.hlp`) with unit tests.
- [x] **Audit & implement `ckcompress`:** Replace `COMPRESS.COM` (used in `SELECT/MAKEFILE`) with unit tests.

> **Milestone (WP-1 + WP-2):** Every small auxiliary DOS build tool has a native open-source replacement; only `cknmake` execution, `CL`, and prebuilt `.LIB`/`.OBJ` binaries remain.

---

## WP-3: `cknmake` Recipe Execution (Priority: P1)

Promote [`cknmake`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cknmake_main.cpp) from a `/N` dependency planner into a safely sandboxed build executor.

- [x] **Command dispatch:** Map historical DOS tool names (`masm`, `link`, `lib`, `exe2bin`, `convert`, `exefix`, `menubld`, `asc2hlp`, `compress`, `buildmsg`, `buildidx`, `nosrvbld`, `dbof`, and later `cl`) to the native `ck*` executables.
- [x] **DOS environment compatibility:** Handle case-insensitive filenames, backslash (`\`) path separators, and `@response` files across POSIX and Windows hosts.
- [x] **Built-in shell commands:** Implement portable handling for simple makefile commands (`del`, `copy`, `mkdir`, `cd`, `echo`) without spawning a DOS shell.
- [x] **Integration tests:** Add end-to-end tests in `tests/` that execute pure-assembly makefiles (such as `CMD/TREE/MAKEFILE`, `CMD/DEBUG/MAKEFILE`, `CMD/SORT/MAKEFILE`) and verify outputs.

> **Milestone (WP-1..WP-3):** All pure-assembly MS-DOS 4 programs build end-to-end natively without DOS.

---

## WP-4: Microsoft C Compiler Replacement (`ckcl`) (Priority: P1)

Reimplement the C compilation pipeline (`CL.EXE` / `C1` / `C2` / `C3`) as a native tool (`ckcl`) producing 16-bit OMF `.OBJ` modules.

- [x] **Tree-wide C audit:** Catalog all `.C` files, compiler flags (`-AS`, `-AM`, `-Os`, `-Zp`, `-G...`), include paths, preprocessor defines, and K&R/Microsoft C extensions used in `third_party/ms-dos-4/src/`.
- [x] **Preprocessor & frontend:** Implement C89/Microsoft-dialect preprocessor and parser supporting the audited language subset.
- [x] **16-bit x86 code generator & OMF emitter:** Emit 8086/80286 OMF object records (`THEADR`, `LNAMES`, `SEGDEF`, `GRPDEF`, `EXTDEF`, `PUBDEF`, `LEDATA`, `FIXUPP`, `MODEND`) matching Microsoft C segment/group naming and calling conventions.
- [x] **Compiler test suite:** Add unit tests for OMF output, struct packing (`-Zp`), memory models, and compile-and-link tests for C targets (`ATTRIB`, `BACKUP`, `FC`, `FDISK`, `FILESYS`, `JOIN`, `MEM`, `RESTORE`, `SUBST`, `SMARTDRV`, `MEMM`/`EMM`, `SELECT`).

---

## WP-5: Open-Source C Runtime & Binary Library Replacements (Priority: P2)

Eliminate all precompiled `.LIB` and `.OBJ` files in `third_party/ms-dos-4/src/` so nothing links against historical Microsoft binary code.

- [x] **Audit CRT & startup dependencies:** Extract the exact symbol list and semantics required from `third_party/ms-dos-4/src/TOOLS/BLD/LIB/` (`SLIBCE.LIB`, `MLIBCE.LIB`, `LLIBCE.LIB`, `CLIBCE.LIB`, `LIBH.LIB`, `EM.LIB`, `SETARGV.OBJ`, `BINMODE.OBJ`, etc.).
- [x] **Audit tree binary libraries:** Extract the exact symbol list and semantics required from:
  - `third_party/ms-dos-4/src/LIB/MEM.LIB`
  - `third_party/ms-dos-4/src/SELECT/CASSFAR.LIB`
  - `third_party/ms-dos-4/src/INC/COMSUBS.LIB`
  - `third_party/ms-dos-4/src/INC/PRINTF.OBJ`
  - `third_party/ms-dos-4/src/INC/STRING.OBJ`
- [x] **Clean-room implementation:** Write open-source C/assembly implementations for the audited symbols and package them via `ckcl`, `ckmasm`, and `cklib`.

> **Milestone (WP-1..WP-5):** Complete open-source build with zero historical DOS binaries or precompiled libraries.

---

## WP-6: Full Source Build & Verification Gate (Priority: P2)

- [x] **Full-tree native build:** Run `cknmake` across the entire MS-DOS 4 source tree on macOS, Linux, and Windows.
- [x] **Manifest & byte comparison:** Compare every generated `.OBJ`, `.LIB`, `.EXE`, `.COM`, and `.SYS` artifact against a recorded reference manifest.
- [x] **CI gate:** Promote the full source build and manifest verification into GitHub Actions.

---

## WP-7: Complete MS-DOS 4 Tree Build (Remaining 46 Subdirectories) (Priority: P1)

Close the remaining tool-compatibility gaps so all 60 `MAKEFILE` directories in `third_party/ms-dos-4/src/` build cleanly (60/60 pass).

- [x] **Message & `.skl` inference (`cknmake` / `cknosrvbld` / `ckbuildmsg`):** Fix `COMMON1 1` / `.skl` rule handling blocking ~25 `CMD/*` targets and `DEV/VDISK`.
- [x] **`LIB` search path in `cklink`:** Search the `LIB` environment variable for `SLIBC.LIB` / `SLIBCE.LIB` / `MEM.LIB` so `CMD/FC`, `CMD/MEM`, `CMD/PRINT`, and `DEV/SMARTDRV` link cleanly.
- [x] **`ckexe2bin` & `cknmake` `copy` edge cases:** Support single-argument `exe2bin fdboot` (`CMD/FDISK`) and `copy` argument forms used in `CMD/FORMAT`.
- [x] **`ckmasm` remaining directives & flags:** Support `/DI386`, `/i..\memm`, and remaining assembly constructs in `CMD/SHARE`, `DEV/COUNTRY`, `DEV/DISPLAY`, `DEV/DRIVER`, `DEV/RAMDRIVE`, `DEV/XMA2EMS`, `MEMM/EMM`, `MEMM/MEMM`, and `SELECT`.
- [x] **60/60 verification gate:** Update `scripts/verify_full_build.py` and `docs/build-manifest.json` to enforce 60/60 directory builds in CI.

---

## WP-8: Runtime Verification in DOS Emulator (Priority: P1)

Verify that the natively built OS and utilities actually boot and run correctly under real 16-bit x86 execution (QEMU / DOSBox-X / 86Box).

- [x] **Bootable disk image & OS boot test:** Assemble a bootable disk image from `MSBOOT.BIN`, `IO.SYS`, `MSDOS.SYS`, and `COMMAND.COM` and verify headless boot to a `C:\>` prompt.
- [x] **C utility & CRT runtime validation:** Run `ckcl`-compiled utilities (`ATTRIB`, `FC`, `MEM`, `BACKUP`, `RESTORE`, `FDISK`, `JOIN`, `SUBST`, `FLUSH13`) inside the emulator and fix any `ckcl` 8086 codegen or `runtime/` library bugs uncovered.
- [x] **Automated emulator smoke test:** Add a reproducible headless emulator test script to verify OS boot and core command execution.

---

## WP-9: Self-Hosting 16-Bit DOS Toolchain (Priority: P3)

- [x] **16-bit DOS port of core `ck*` tools:** Build or cross-compile a 16-bit DOS subset of the toolchain so MS-DOS 4 can rebuild itself from inside MS-DOS. (Shared C89 core in `include/dosbase/devtools/core/` + `src/devtools/core/` compiled into both host `dosbase_devtools` and 16-bit DOS `CKEXE2BN.EXE`, `CKEXEFIX.EXE`, `CKCONV.EXE`, `CKCMPRSS.EXE`, `CKBLDMSG.EXE`, `CKNOSRV.EXE`, `CKLIB.EXE`, `CKLINK.EXE`, `CKNMAKE.EXE`; verified byte-for-byte in MS-DOS 4 under QEMU via `scripts/verify_self_host.py`.)

---

# Phase 2: `ckcl` Code Quality, 8086 Performance & MS C 5.1 Parity (`WP-10`..`WP-15`)

With the full MS-DOS 4 tree and self-hosting 16-bit DOS tools building and verified in QEMU, Phase 2 upgrades [`ckcl`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cl_compiler.cpp) and [`runtime/crt/`](file:///Volumes/PRO-BLADE/git/ck-dos-base/runtime/crt/crt_mem.c) from a straightforward stack-machine compiler + bump allocator into a compact, fast 8086 code generator approaching Microsoft C 5.1 `-Os` output quality.

## Phase 2 Ordering & Dependency Overview

```text
WP-10: Constant folding & strength reduction (eliminate MUL/DIV) (P1)
  └──► WP-11: Statement-context (need_value) & in-place ++/--/+= (P1)
         └──► WP-12: Direct 8086 memory operands & [bp+d]/[bx+d] addressing (P1)
                └──► WP-13: Short-circuit branches & rel8 jump relaxation (P2)
                       └──► WP-14: SI/DI register variable allocation (P2)
                              └──► WP-15: Struct ABI (rep movsw) & coalescing CRT heap (P2)
```

| ID | Work Package | Priority | Depends On | Primary Impact on DOS Executables |
| --- | --- | --- | --- | --- |
| **WP-10** | Constant folding & strength reduction | P1 | WP-9 | Eliminates ~125-cycle `MUL CX` on array/pointer indexing (`shl` = 2 cycles) |
| **WP-11** | `need_value` context & in-place `++`/`--`/`+=` | P1 | WP-10 | Shrinks loop steps from 12+ bytes to 3–4 bytes (`inc word ptr [bp-d]`) |
| **WP-12** | Direct memory operands & folded addressing | P1 | WP-11 | Eliminates stack `push`/`pop` in binary ops, `s.field`, `p->field`, and 32-bit `long` locals |
| **WP-13** | Short-circuit branches & `rel8` jump relaxation | P2 | WP-12 | Replaces 5-byte `j!cc +3; jmp rel16` with 2-byte `jcc rel8`; zero `mov ax, 1/0` in `if (a && b)` |
| **WP-14** | `SI` / `DI` register variable allocation | P2 | WP-13 | Keeps hot loop counters & pointers in `SI`/`DI` (20–30% smaller code, 2–4x faster loops) |
| **WP-15** | Struct copy (`rep movsw`) & coalescing CRT heap | P2 | WP-14 | Full C89 struct value semantics + reusable 64 KB `DGROUP` heap (`free`/`realloc`) |

> **Verification rule for every Phase 2 WP (`WP-10`..`WP-15`):**
> Every work package must pass:
> 1. `cmake --build build --config Release && ctest --test-dir build --build-config Release --output-on-failure` (with new unit tests covering the emitted 8086 machine code).
> 2. Regenerate & verify `docs/build-manifest.json` via `scripts/verify_full_build.py` (60/60 MS-DOS 4 directories).
> 3. `scripts/verify_qemu_boot.py` (MS-DOS 4 boot + C utilities smoke test in QEMU).
> 4. `scripts/verify_self_host.py` (all 9 DOS self-hosting tools produce byte-for-byte identical outputs to host tools in QEMU).

---

## WP-10: Constant Folding & Strength Reduction in `ckcl` (Priority: P1)

Eliminate expensive 8086 `MUL`/`IMUL`/`DIV`/`IDIV` instructions (118–165 cycles on 8086) for compile-time constants and power-of-2 pointer/integer scaling in [`src/devtools/cl_compiler.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cl_compiler.cpp).

- [x] **AST constant-expression folding:** Fold constant `ExprKind::unary`, `ExprKind::binary`, `ExprKind::ternary`, and scalar `ExprKind::cast` subtrees prior to code emission so expressions like `sizeof(hdr) + 16`, `1 << 4`, or `0x100 - 1` emit a single immediate (`mov ax, imm16`).
- [x] **Pointer & subscript scaling strength reduction:** In [`cl_compiler.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cl_compiler.cpp#L3038-L3050), replace `mov ax, elem_sz; mul cx` with:
  - `elem_sz == 2`: `shl cx, 1` (2 bytes, 2 cycles vs. 9 bytes, ~130 cycles).
  - `elem_sz == 4`: two `shl cx, 1` instructions (4 bytes, 4 cycles; pure 8086 compatible).
  - `elem_sz == 8`: three `shl cx, 1` instructions (6 bytes) or `mov cl, k; shl reg, cl` when `CX` is free.
  - Common small struct/table element sizes (`3, 5, 6, 10, 12`): inline shift-and-add sequences before falling back to `mul cx`.
- [x] **Integer `*`, `/`, `%`, `<<`, `>>` by constants:**
  - Constant shift counts `x << k` / `x >> k` (for 16-bit and 32-bit `DX:AX`): emit inline `shl`/`shr`/`sar` for `k <= 3`, or `mov cl, k; shl/shr/sar ax, cl` for `16`-bit values instead of full general evaluation.
  - Multiply by power-of-2 constant `x * (1 << k)` -> left shift.
  - Unsigned divide `u / (1 << k)` -> `shr`, and unsigned modulo `u % (1 << k)` -> `and ax, (1 << k) - 1`.
- [x] **Unit & QEMU verification:** Add OMF disassembly/opcode assertions in `tests/` confirming zero `mul` opcodes on `int[]` / pointer indexing and constant arithmetic, update `docs/build-manifest.json`, and run `verify_qemu_boot.py` + `verify_self_host.py`.

---

## WP-11: Statement-Context (`need_value`) & In-Place Memory Operations (Priority: P1)

Stop computing and preserving unused expression results in `AX`/`DX:AX`, and emit direct memory-modifying 8086 instructions for `++`, `--`, and compound assignments (`+=`, `-=`, `&=`, `|=`, `^=`).

- [x] **Thread `bool need_value` through `emit_expr`:** Pass `need_value = false` from `StmtKind::expr`, `for`-loop init and post expressions (`for (init; cond; post)`), and the left-hand side of `ExprKind::comma` in [`cl_compiler.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cl_compiler.cpp#L2354-L2455).
- [x] **In-place `++` / `--` for 8-bit & 16-bit lvalues:**
  - When `need_value == false`, treat postfix `x++` / `x--` identically to prefix `++x` / `--x` (eliminating `push ax` / `pop ax`).
  - For scalar locals (`[bp+d]`), globals (`[_sym]`), and simple dereferences (`[bx]`): emit `inc`/`dec byte|word ptr [mem]` (`FE`/`FF /0` or `/1`, 3–4 bytes instead of 12+ bytes) when `step == 1`, or `add`/`sub word ptr [mem], step` when `step > 1` (pointer increments).
- [x] **In-place 32-bit `long` `++` / `--` (`need_value == false`):** For 32-bit locals/globals, emit `add word ptr [mem], step; adc word ptr [mem+2], 0` (or `sub`/`sbb`) without loading `DX:AX`.
- [x] **In-place compound assignments (`+=`, `-=`, `&=`, `|=`, `^=`):** When `need_value == false` and the RHS is an immediate constant on an 8/16-bit local or global, emit `add`/`sub`/`and`/`or`/`xor [bp+d], imm` directly.
- [x] **Unit & QEMU verification:** Verify code-size reduction across `build_dos_tools.py` outputs and pass full manifest + QEMU gates.

---

## WP-12: Direct 8086 Memory Operands & Folded Addressing Modes (Priority: P1)

Eliminate redundant stack `push`/`pop` pairs and `lea bx` + `add bx` address calculations by exploiting 8086 `[bp+disp]`, `[bx+disp]`, and `[_sym]` addressing modes in [`src/devtools/cl_compiler.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cl_compiler.cpp).

- [x] **Direct memory RHS in 16-bit binary ALU & comparisons:** When `e->rhs` in `+`, `-`, `&`, `|`, `^`, or relational comparisons (`==`, `!=`, `<`, `<=`, `>`, `>=`) is a 16-bit local/parameter (`[bp+d]`) or near global (`[_sym]`) matching signedness/width:
  - Emit `emit_expr(lhs)` followed directly by `add/sub/and/or/xor/cmp ax, [bp+d]` or `[_sym]` (saving `push ax`, `mov ax, ...`, `mov cx, ax`, `pop ax` — cutting 12 bytes down to 5–6 bytes).
- [x] **Folded `[bp + disp]` addressing for local structs & arrays:** When `emit_lvalue_addr` / `ExprKind::member` accesses `s.field` (or nested `s.a.b`) on a local stack struct, fold the constant field offset directly into `[bp + local_disp + foff]` so scalar loads/stores use a single `mov ax, [bp + disp]` without `lea bx` + `add bx`.
- [x] **Displacement addressing `[bx + disp8/16]` for `p->field` and `p[const_idx]`:** Extend `load_from_bx` and `store_to_bx` ([`cl_compiler.cpp`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cl_compiler.cpp#L2604-L2630)) to accept an integer `disp` parameter (`[bx]`, `[bx+disp8]`, `[bx+disp16]`), removing `add bx, foff` (`81 C3 xx xx`, 4 bytes) from every `p->field` access.
- [x] **Fast `[bp+d]` load/store for 32-bit `long` & `far` pointer locals:** Add 32-bit local/parameter fast paths in `ExprKind::var` ([`L2690`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cl_compiler.cpp#L2690)) and `ExprKind::assign` ([`L2783`](file:///Volumes/PRO-BLADE/git/ck-dos-base/src/devtools/cl_compiler.cpp#L2783)): emit `mov ax, [bp+d]; mov dx, [bp+d+2]` (6 bytes) and `mov [bp+d], ax; mov [bp+d+2], dx` (6 bytes vs. current 13-byte `push/lea/pop/mov` sequence).
- [x] **Unit & QEMU verification:** Add unit tests for local struct, `p->field`, 32-bit local, and direct-RHS codegen; verify all host, manifest, and QEMU tests pass.

---

## WP-13: Short-Circuit Control-Flow Lowering & Jump Relaxation (Priority: P2)

Eliminate 5-byte long conditional jump thunks on short branches and boolean `0`/`1` materialization inside control-flow conditions.

- [x] **Direct short-circuit branch lowering (`emit_branch`):** Implement `emit_branch(cond, LabelId target, bool jump_on_true)` used by `if`, `while`, `do-while`, `for`, and `?:`:
  - `!x` flips `jump_on_true` with zero instructions emitted.
  - `a && b` and `a || b` emit direct short-circuit conditional jumps to `false_lbl` / `true_lbl` without `mov ax, 1 / jmp / mov ax, 0 / test ax, ax`.
  - Relational operators (`==`, `!=`, `<`, `<=`, `>`, `>=`, both 16-bit and 32-bit) jump directly on the condition flags.
- [x] **Symbolic labels & `rel8` jump relaxation pass:**
  - Record basic-block/branch instructions and OMF fixup offsets relative to logical labels in `emit_function`.
  - Run an iterative relaxation pass before serializing the function's `LEDATA`:
    - Shrink `jmp rel16` (`E9 xx xx`, 3 bytes) -> `jmp rel8` (`EB xx`, 2 bytes) when displacement fits in `[-128, +127]`, and delete `jmp +0` fall-throughs (e.g., `return` at the very end of a function).
    - Shrink `j<!cc> +3; jmp rel16` (5 bytes) -> `j<cc> rel8` (2 bytes) when displacement fits in `[-128, +127]`.
  - Adjust local function offsets, `PUBDEF` offsets, and `FIXUPP` offsets accurately after relaxation.
- [x] **Unit & QEMU verification:** Add branch-relaxation tests (covering both `< 127`-byte short jumps and `> 128`-byte long jumps + `goto` + `switch` + OMF fixups inside relaxed functions) and verify all 60 MS-DOS 4 directories and QEMU tests pass.

---

## WP-14: `SI` / `DI` Register Variable Allocation (Priority: P2)

Implement Microsoft C 5.1's signature 16-bit optimization: keeping up to two hot scalar/pointer variables per function in callee-saved registers `SI` and `DI`.

- [ ] **Liveness / safety & loop-weighted scoring pass:** Walk each function's AST before `emit_function` to identify candidate locals and parameters:
  - **Eligibility:** 16-bit integer or near pointer (`sizeof == 2`, not `volatile`, address `&v` never taken, not modified across `setjmp`).
  - **Scoring:** Explicit `register` keyword gets priority bonus; each read/write adds `1 + 10 * loop_depth`. Select the top 1–2 candidates for `SI` and `DI` when total savings exceed prologue/epilogue save/restore cost (`push si`/`pop si` = 2 bytes + optional 3-byte param load `mov si, [bp+off]`).
- [ ] **Prologue / epilogue & parameter initialization:** Save/restore `SI` and/or `DI` around the function body (after `mov bp, sp; sub sp, frame_size`), and copy promoted parameters from `[bp+off]` into `SI`/`DI` on entry.
- [ ] **Register-aware codegen across `emit_expr`:**
  - Reads/writes: `mov ax, si`, `mov si, ax`, `inc si` (1 byte!), `dec si` (1 byte!), `add si, imm`, `cmp si, rhs`.
  - Pointer dereference (`*p`, `p->field`, `p[const]`): when `p` lives in `SI` or `DI`, address memory directly via `[si]`, `[si+disp8]`, `[si+disp16]` (or `[di+...]`) without copying into `BX`.
- [ ] **Unit & QEMU verification:** Add unit tests verifying `SI`/`DI` allocation, nested calls preserving `SI`/`DI`, and pointer dereferencing via `[si+disp]`; measure `.EXE` size reductions and run full QEMU verification.

---

## WP-15: Struct Value ABI (`rep movsw`) & Coalescing CRT Heap Allocator (Priority: P2)

Close the remaining C89 struct-value limitation in `ckcl` and upgrade the 16-bit DOS CRT heap in [`runtime/crt/crt_mem.c`](file:///Volumes/PRO-BLADE/git/ck-dos-base/runtime/crt/crt_mem.c) so DOS tools can run arbitrarily large workloads within 64 KB `DGROUP`.

- [ ] **Struct assignment (`a = b`) in `ckcl`:** When assigning structs/unions (`TypeKind::t_struct`), evaluate RHS address into `SI` (preserving `SI`/`DI` if live in register variables!), LHS address into `ES:DI` (`push ds; pop es` for near data), and emit `cld; mov cx, sz/2; rep movsw` (+ `movsb` if `sz & 1`), leaving LHS address in `AX` if `need_value` is true.
- [ ] **Struct pass-by-value & return-by-value:**
  - Push struct arguments onto the stack (even-aligned byte count via `sub sp, aligned_sz` + `rep movsw`) in `ExprKind::call`.
  - Support functions returning structs > 4 bytes via a hidden caller-allocated buffer pointer matching MS C small/medium model conventions.
- [ ] **Coalescing free-list `malloc` / `free` / `realloc` in `runtime/crt/crt_mem.c`:**
  - Replace the bump-only allocator (`free()` no-op) with a 4-byte block header (`size_and_flags` + free-list links or boundary tags) over `_heap_ptr.._heap_limit` (with the static `crt_heap` fallback when `_heap_ptr == 0`).
  - Split oversized free blocks on `malloc`, coalesce adjacent free blocks on `free()`, and shrink/expand blocks in-place in `realloc()` before falling back to allocate-copy-free.
- [ ] **End-to-end size/speed audit & small-model check:**
  - Measure before/after binary sizes across MS-DOS 4 C utilities (`ATTRIB.EXE`, `MEM.EXE`, `FC.EXE`, `FDISK.EXE`, `BACKUP.COM`, `RESTORE.COM`) and `build_dos_tools.py` outputs (check if `CKLINK.EXE` now fits in `-AS` small model!).
  - Add a DOS heap churn stress test to `scripts/verify_self_host.py` (allocating and freeing > 200 KB total across many iterations inside a 64 KB `DGROUP` process), update `docs/build-manifest.json`, and run all verification gates.

