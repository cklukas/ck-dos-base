#!/usr/bin/env python3
"""Build 16-bit MS-DOS self-hosting ck* tools using ckcl, ckmasm, cklib, and cklink."""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys


def find_ci(directory: pathlib.Path, name: str) -> pathlib.Path:
    target = name.lower()
    for p in directory.iterdir():
        if p.name.lower() == target:
            return p
    return directory / name


def build_slibc(repo_root: pathlib.Path, tool_dir: pathlib.Path, out_dir: pathlib.Path, model: str = "S") -> pathlib.Path:
    crt_dir = repo_root / "runtime" / "crt"
    ckmasm = str(tool_dir / "ckmasm")
    ckcl = str(tool_dir / "ckcl")
    cklib = str(tool_dir / "cklib")

    out_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(crt_dir / "crt0.asm", out_dir / "crt0.asm")
    shutil.copy2(crt_dir / "dos_io.asm", out_dir / "dos_io.asm")
    masm_flags = ["-Mx", "-t"] + (["-DFARCODE"] if model == "M" else [])
    subprocess.run([ckmasm] + masm_flags + ["crt0.asm,crt0.obj;"], cwd=out_dir, check=True)
    subprocess.run([ckmasm] + masm_flags + ["dos_io.asm,dos_io.obj;"], cwd=out_dir, check=True)

    c_objs = []
    for name in ["crt_mem.c", "crt_str.c", "crt_stdio.c", "setargv.c", "binmode.c"]:
        obj = out_dir / (name[:-2] + ".obj")
        subprocess.run(
            [ckcl, "-c", f"-A{model}", "-Os", "-Zp", f"-Fo{obj}", str(crt_dir / name)],
            check=True,
        )
        c_objs.append(obj)

    lib_name = f"{model}LIBC.LIB"
    lib_path = out_dir / lib_name
    if lib_path.exists():
        lib_path.unlink()
    lib_cmds = ["+CRT0.OBJ", "+DOS_IO.OBJ"] + [f"+{o.name}" for o in [find_ci(out_dir, c.name) for c in c_objs]]
    subprocess.run([cklib, lib_name] + lib_cmds + [";"], cwd=out_dir, check=True)
    return find_ci(out_dir, lib_name)


def compile_and_link_exe(
    repo_root: pathlib.Path,
    tool_dir: pathlib.Path,
    build_dir: pathlib.Path,
    clib: pathlib.Path,
    exe_name: str,
    c_sources: list[pathlib.Path],
    model: str = "S",
) -> pathlib.Path:
    ckcl = str(tool_dir / "ckcl")
    cklink = str(tool_dir / "cklink")
    inc_dir = repo_root / "include"
    dos_dir = repo_root / "src" / "devtools" / "dos"

    objs = []
    for src in c_sources:
        obj_name = src.stem.upper() + ".OBJ"
        subprocess.run(
            [ckcl, "-c", f"-A{model}", "-Os", "-Zp", "-DCK_DOS_LEAN", f"-I{inc_dir}", f"-I{dos_dir}", f"-Fo{obj_name}", str(src)],
            cwd=build_dir,
            check=True,
        )
        objs.append(find_ci(build_dir, obj_name).name)

    obj_list = "+".join(objs)
    sw = "/DOSSEG" if model == "M" else ""
    subprocess.run([cklink, f"{obj_list},{exe_name},nul,{clib.name}{sw};"], cwd=build_dir, check=True)
    return find_ci(build_dir, exe_name)


def build_all_dos_tools(repo_root: pathlib.Path, tool_dir: pathlib.Path, out_dir: pathlib.Path) -> list[pathlib.Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    obj_dir = out_dir / "obj"
    obj_dir.mkdir(parents=True, exist_ok=True)
    med_dir = out_dir / "obj_m"
    slibc = build_slibc(repo_root, tool_dir, obj_dir, "S")
    mlibc = build_slibc(repo_root, tool_dir, med_dir, "M")

    core_dir = repo_root / "src" / "devtools" / "core"
    dos_dir = repo_root / "src" / "devtools" / "dos"

    tools = [
        ("CKEXE2BN.EXE", [core_dir / "mz_core.c", core_dir / "exe2bin_core.c", dos_dir / "ckexe2bn.c"], "S"),
        ("CKEXEFIX.EXE", [core_dir / "mz_core.c", core_dir / "exefix_core.c", dos_dir / "ckexefix.c"], "S"),
        ("CKCONV.EXE", [core_dir / "mz_core.c", core_dir / "convert_core.c", dos_dir / "ckconv.c"], "S"),
        ("CKCMPRSS.EXE", [core_dir / "compress_core.c", dos_dir / "ckcmprss.c"], "S"),
        ("CKBLDMSG.EXE", [core_dir / "msgtools_core.c", dos_dir / "ckbldmsg.c"], "S"),
        ("CKNOSRV.EXE", [core_dir / "msgtools_core.c", dos_dir / "cknosrv.c"], "S"),
        ("CKLIB.EXE", [core_dir / "omf_core.c", core_dir / "lib_core.c", dos_dir / "cklib.c"], "S"),
        ("CKLINK.EXE", [core_dir / "omf_core.c", core_dir / "link_core.c", dos_dir / "cklink.c"], "M"),
        ("CKNMAKE.EXE", [core_dir / "nmake_core.c", dos_dir / "cknmake.c"], "S"),
    ]

    built = []
    for exe_name, srcs, model in tools:
        bdir = med_dir if model == "M" else obj_dir
        clib = mlibc if model == "M" else slibc
        exe = compile_and_link_exe(repo_root, tool_dir, bdir, clib, exe_name, srcs, model)
        dst = out_dir / exe_name
        shutil.copy2(exe, dst)
        built.append(dst)
        print(f"Built {dst} ({dst.stat().st_size} bytes)")
    return built


def main() -> int:
    parser = argparse.ArgumentParser(description="Build 16-bit DOS self-hosting ck* tools")
    parser.add_argument("--repo-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent)
    parser.add_argument("--tool-dir", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent / "build")
    parser.add_argument("--out-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    build_all_dos_tools(args.repo_root.resolve(), args.tool_dir.resolve(), args.out_dir.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
