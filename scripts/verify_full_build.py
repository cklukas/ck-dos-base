#!/usr/bin/env python3
"""Full-tree MS-DOS 4 native build & manifest verification gate (WP-6)."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def build_clean_room_runtime(repo_root: Path, stage: Path, tool_dir: Path) -> None:
    ckcl = str(tool_dir / "ckcl")
    ckmasm = str(tool_dir / "ckmasm")
    cklib = str(tool_dir / "cklib")
    rt_root = repo_root / "runtime"

    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)

        def asm(src: Path, obj: str) -> None:
            shutil.copy2(src, d / src.name)
            subprocess.run([ckmasm, "-Mx", f"{src.name},{obj};"], cwd=d, check=True)

        def cc(src: Path, obj: str) -> None:
            subprocess.run([ckcl, "-c", "-AS", "-Zl", f"-Fo{obj}", str(src)], cwd=d, check=True)

        def mklib(libname: str, objs: list[str]) -> None:
            rsp = d / f"{libname}.RSP"
            lines = [libname, "Y"]
            for i, obj in enumerate(objs):
                lines.append(f"+{obj}" + ("&" if i + 1 < len(objs) else ";"))
            rsp.write_text("\n".join(lines) + "\n", encoding="utf-8")
            subprocess.run([cklib, f"@{rsp.name}"], cwd=d, check=True)

        asm(rt_root / "crt" / "crt0.asm", "CRT0.OBJ")
        asm(rt_root / "crt" / "dos_io.asm", "DOS_IO.OBJ")
        cc(rt_root / "crt" / "crt_mem.c", "CRT_MEM.OBJ")
        cc(rt_root / "crt" / "crt_str.c", "CRT_STR.OBJ")
        cc(rt_root / "crt" / "crt_stdio.c", "CRT_STDIO.OBJ")
        cc(rt_root / "crt" / "setargv.c", "SETARGV.OBJ")
        cc(rt_root / "crt" / "binmode.c", "BINMODE.OBJ")
        cc(rt_root / "comsubs" / "comsubs.c", "COMSUBS.OBJ")
        asm(rt_root / "comsubs" / "realopen.asm", "REALOPEN.OBJ")
        cc(rt_root / "string" / "string_ext.c", "STRING.OBJ")
        asm(rt_root / "cassfar" / "cassfar.asm", "CASSFAR.OBJ")

        mklib("SLIBCE.LIB", ["CRT0.OBJ", "DOS_IO.OBJ", "CRT_MEM.OBJ", "CRT_STR.OBJ", "CRT_STDIO.OBJ"])
        mklib("COMSUBS.LIB", ["COMSUBS.OBJ", "REALOPEN.OBJ"])
        mklib("CASSFAR.LIB", ["CASSFAR.OBJ"])

        bld_lib = stage / "TOOLS" / "BLD" / "LIB"
        bld_lib.mkdir(parents=True, exist_ok=True)
        for name in ("SLIBCE.LIB", "SLIBC.LIB", "MLIBCE.LIB", "CLIBCE.LIB", "LLIBCE.LIB"):
            shutil.copy2(d / "SLIBCE.LIB", bld_lib / name)
        shutil.copy2(d / "SETARGV.OBJ", bld_lib / "SETARGV.OBJ")
        shutil.copy2(d / "BINMODE.OBJ", bld_lib / "BINMODE.OBJ")

        (stage / "LIB").mkdir(parents=True, exist_ok=True)
        shutil.copy2(d / "SLIBCE.LIB", stage / "LIB" / "MEM.LIB")
        shutil.copy2(d / "COMSUBS.LIB", stage / "INC" / "COMSUBS.LIB")
        shutil.copy2(d / "STRING.OBJ", stage / "INC" / "STRING.OBJ")
        shutil.copy2(d / "CASSFAR.LIB", stage / "SELECT" / "CASSFAR.LIB")


def collect_manifest(stage: Path) -> dict[str, dict[str, object]]:
    exts = {".OBJ", ".LIB", ".EXE", ".COM", ".SYS", ".BIN"}
    manifest: dict[str, dict[str, object]] = {}
    for path in sorted(stage.rglob("*")):
        if path.is_file() and path.suffix.upper() in exts:
            rel = path.relative_to(stage).as_posix()
            manifest[rel] = {
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
    return manifest


def run_build(repo_root: Path, tool_dir: Path, write_manifest: Path | None, verify_manifest: Path | None) -> int:
    src_tree = repo_root / "third_party" / "ms-dos-4" / "src"
    cknmake = str(tool_dir / "cknmake")

    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / "src"
        shutil.copytree(src_tree, stage)
        build_clean_room_runtime(repo_root, stage, tool_dir)

        env = os.environ.copy()
        env["PATH"] = str(tool_dir) + os.pathsep + env.get("PATH", "")
        env["INIT"] = str(stage / "TOOLS")
        env["COUNTRY"] = "USA-MS"
        env["INCLUDE"] = f"{stage / 'INC'};{stage / 'TOOLS' / 'BLD' / 'INC'}"
        env["LIB"] = f"{stage / 'TOOLS' / 'BLD' / 'LIB'};{stage / 'LIB'}"

        core_targets = ["MESSAGES", "MAPPER", "BOOT", "BIOS", "DOS"]
        failures: list[tuple[str, int, str]] = []
        built_dirs: list[str] = []

        for sub in core_targets:
            sub_dir = stage / sub
            if not (sub_dir / "MAKEFILE").exists() and not (sub_dir / "makefile").exists():
                continue
            r = subprocess.run([cknmake], cwd=sub_dir, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
            if r.returncode == 0:
                built_dirs.append(sub)
            else:
                failures.append((sub, r.returncode, r.stderr.strip()))

        # Also exercise CMD/, DEV/, MEMM/, SELECT/ subdirectories
        for group in ("CMD", "DEV", "MEMM", "SELECT"):
            gdir = stage / group
            if not gdir.exists():
                continue
            candidates = [gdir] + sorted(p for p in gdir.iterdir() if p.is_dir())
            for sub_dir in candidates:
                if not (sub_dir / "MAKEFILE").exists() and not (sub_dir / "makefile").exists():
                    continue
                rel = sub_dir.relative_to(stage).as_posix()
                r = subprocess.run([cknmake], cwd=sub_dir, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
                if r.returncode == 0:
                    built_dirs.append(rel)
                else:
                    failures.append((rel, r.returncode, r.stderr.strip()))

        required_outputs = [
            stage / "BOOT" / "MSBOOT.BIN",
            stage / "BIOS" / "IO.SYS",
            stage / "DOS" / "MSDOS.SYS",
        ]
        for req in required_outputs:
            if not req.is_file() or req.stat().st_size == 0:
                print(f"ERROR: required core artifact missing or empty: {req.relative_to(stage)}", file=sys.stderr)
                return 1

        manifest = collect_manifest(stage)
        print(f"Built {len(built_dirs)} directories cleanly; core targets: {', '.join(core_targets)}")
        print(f"Manifest artifacts: {len(manifest)}")
        for req in required_outputs:
            rel = req.relative_to(stage).as_posix()
            print(f"  {rel}: {manifest[rel]['size']} bytes sha256={manifest[rel]['sha256']}")

        if failures:
            for sub, rc, err in failures:
                print(f"FAIL {sub} (rc={rc}): {err}", file=sys.stderr)
            return 1

        if write_manifest is not None:
            write_manifest.parent.mkdir(parents=True, exist_ok=True)
            write_manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"Wrote manifest to {write_manifest}")

        if verify_manifest is not None:
            expected = json.loads(verify_manifest.read_text(encoding="utf-8"))
            mismatches = []
            for key, exp_val in expected.items():
                if key not in manifest:
                    mismatches.append(f"missing {key}")
                elif manifest[key] != exp_val:
                    mismatches.append(f"mismatch {key}: got {manifest[key]}, expected {exp_val}")
            if mismatches:
                for m in mismatches[:20]:
                    print(f"MANIFEST ERROR: {m}", file=sys.stderr)
                return 1
            print(f"Manifest verification passed ({len(expected)} artifacts matched byte-for-byte)")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify full MS-DOS 4 native build and manifest")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--tool-dir", type=Path, default=Path(__file__).resolve().parent.parent / "build")
    parser.add_argument("--write-manifest", type=Path, default=None)
    parser.add_argument("--verify-manifest", type=Path, default=None)
    args = parser.parse_args()
    return run_build(args.repo_root, args.tool_dir, args.write_manifest, args.verify_manifest)


if __name__ == "__main__":
    raise SystemExit(main())
