#!/usr/bin/env python3
"""Headless QEMU MS-DOS 4 boot & utility smoke test (WP-8)."""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import verify_full_build as vfb


# Clean-room 34-byte MBR bootstrap: relocate 0000:7C00 -> 0000:0600,
# load partition 0 VBR (CHS 0/1/1, drive 80h) to 0000:7C00, and jump to it.
MBR_BOOTSTRAP = bytes([
    0xFA,                          # cli
    0x33, 0xC0,                    # xor ax, ax
    0x8E, 0xD0,                    # mov ss, ax
    0xBC, 0x00, 0x7C,              # mov sp, 7C00h
    0xFB,                          # sti
    0x8E, 0xD8,                    # mov ds, ax
    0x8E, 0xC0,                    # mov es, ax
    0xBE, 0x00, 0x7C,              # mov si, 7C00h
    0xBF, 0x00, 0x06,              # mov di, 0600h
    0xB9, 0x00, 0x01,              # mov cx, 0100h
    0xFC,                          # cld
    0xF3, 0xA5,                    # rep movsw
    0xEA, 0x1E, 0x06, 0x00, 0x00,  # jmp 0000:061Eh
    0xBE, 0xBE, 0x07,              # mov si, 07BEh
    0xB8, 0x01, 0x02,              # mov ax, 0201h
    0xB9, 0x01, 0x00,              # mov cx, 0001h
    0xBA, 0x80, 0x01,              # mov dx, 0180h
    0xBB, 0x00, 0x7C,              # mov bx, 7C00h
    0xCD, 0x13,                    # int 13h
    0xEA, 0x00, 0x7C, 0x00, 0x00,  # jmp 0000:7C00h
])


def find_ci(directory: pathlib.Path, name: str) -> pathlib.Path:
    target = name.lower()
    for p in directory.iterdir():
        if p.name.lower() == target:
            return p
    return directory / name


def make_hdd_image(img_path: pathlib.Path, stage: pathlib.Path) -> None:
    cyls, heads, spt = 305, 4, 17
    total_sectors = cyls * heads * spt
    part_start = spt  # LBA 17 = CHS (0, 1, 1)
    part_sectors = total_sectors - part_start
    part_offset = part_start * 512

    img_path.write_bytes(b"\x00" * (total_sectors * 512))

    # Format FAT16 partition with mformat
    subprocess.run(
        [
            "mformat",
            "-i", f"{img_path}@@{part_offset}",
            "-T", str(part_sectors),
            "-h", str(heads),
            "-s", str(spt),
            "-H", str(part_start),
            "-M", "512",
            "::",
        ],
        check=True,
    )

    # Read formatted VBR BPB, splice MSBOOT.BIN preserving BPB (0x0B..0x3E)
    msboot = find_ci(stage / "BOOT", "MSBOOT.BIN").read_bytes()[0x7C00:0x7E00]
    with img_path.open("r+b") as f:
        # Write MBR + partition 0 entry
        mbr = bytearray(512)
        mbr[:len(MBR_BOOTSTRAP)] = MBR_BOOTSTRAP
        end_cyl = cyls - 1
        part_entry = bytes([
            0x80,                                  # bootable
            1, 1, 0,                               # start CHS: head 1, sec 1, cyl 0
            0x04,                                  # FAT16 < 32MB
            heads - 1,                             # end head
            spt | (((end_cyl >> 8) & 3) << 6),     # end sec + cyl hi
            end_cyl & 0xFF,                        # end cyl lo
        ]) + struct.pack("<II", part_start, part_sectors)
        mbr[0x1BE:0x1CE] = part_entry
        mbr[0x1FE:0x200] = b"\x55\xAA"
        f.seek(0)
        f.write(mbr)

        # Patch VBR with MSBOOT.BIN keeping BPB
        f.seek(part_offset)
        orig_vbr = f.read(512)
        vbr = bytearray(msboot)
        vbr[0x0B:0x3E] = orig_vbr[0x0B:0x3E]
        vbr[0x24] = 0x80  # Phydrv = 80h (HDD)
        vbr[0x1FE:0x200] = b"\x55\xAA"
        f.seek(part_offset)
        f.write(vbr)

    spec = f"{img_path}@@{part_offset}"
    # Copy IO.SYS, MSDOS.SYS first as system/hidden/readonly, then COMMAND.COM
    subprocess.run(["mcopy", "-o", "-i", spec, str(find_ci(stage / "BIOS", "IO.SYS")), "::IO.SYS"], check=True)
    subprocess.run(["mcopy", "-o", "-i", spec, str(find_ci(stage / "DOS", "MSDOS.SYS")), "::MSDOS.SYS"], check=True)
    subprocess.run(["mattrib", "-i", spec, "+s", "+h", "+r", "::IO.SYS", "::MSDOS.SYS"], check=True)
    subprocess.run(["mcopy", "-o", "-i", spec, str(find_ci(stage / "CMD" / "COMMAND", "command.com")), "::COMMAND.COM"], check=True)

    # Stage validated utilities
    utilities = [
        (find_ci(stage / "CMD" / "MEM", "mem.exe"), "::MEM.EXE"),
        (find_ci(stage / "CMD" / "FC", "fc.exe"), "::FC.EXE"),
        (find_ci(stage / "CMD" / "BACKUP", "backup.com"), "::BACKUP.COM"),
        (find_ci(stage / "CMD" / "JOIN", "join.exe"), "::JOIN.EXE"),
        (find_ci(stage / "CMD" / "SUBST", "subst.exe"), "::SUBST.EXE"),
        (find_ci(stage / "DEV" / "SMARTDRV", "flush13.exe"), "::FLUSH13.EXE"),
    ]
    for src, dst in utilities:
        subprocess.run(["mcopy", "-o", "-i", spec, str(src), dst], check=True)

    # Create test files & AUTOEXEC.BAT
    tmp_dir = img_path.parent
    (tmp_dir / "A.TXT").write_bytes(b"hello dos 4\r\nline two\r\n")
    (tmp_dir / "B.TXT").write_bytes(b"hello dos 4\r\nline 2\r\n")
    (tmp_dir / "AUTOEXEC.BAT").write_bytes(
        b"@ECHO OFF\r\n"
        b"MEM\r\n"
        b"FC A.TXT B.TXT\r\n"
        b"FC /B A.TXT B.TXT\r\n"
        b"BACKUP\r\n"
        b"JOIN\r\n"
        b"FLUSH13\r\n"
        b"VER\r\n"
        b"ECHO ==QEMU_SMOKE_DONE==\r\n"
    )
    subprocess.run(["mcopy", "-o", "-i", spec, str(tmp_dir / "A.TXT"), "::A.TXT"], check=True)
    subprocess.run(["mcopy", "-o", "-i", spec, str(tmp_dir / "B.TXT"), "::B.TXT"], check=True)
    subprocess.run(["mcopy", "-o", "-i", spec, str(tmp_dir / "AUTOEXEC.BAT"), "::AUTOEXEC.BAT"], check=True)


def build_boot_and_utilities(repo_root: pathlib.Path, tool_dir: pathlib.Path, stage: pathlib.Path) -> None:
    shutil.copytree(repo_root / "third_party" / "ms-dos-4" / "src", stage, dirs_exist_ok=True)
    vfb.build_clean_room_runtime(repo_root, stage, tool_dir)

    env = os.environ.copy()
    env["PATH"] = f"{tool_dir}{os.pathsep}{env.get('PATH', '')}"
    env["INIT"] = str(stage / "TOOLS")
    env["COUNTRY"] = "USA-MS"
    env["LIB"] = f"{stage / 'TOOLS' / 'BLD' / 'LIB'};{stage / 'LIB'}"
    env["INCLUDE"] = f"{stage / 'INC'};{stage / 'TOOLS' / 'BLD' / 'INC'}"
    cknmake = str(tool_dir / "cknmake")

    dirs = [
        "MESSAGES",
        "MAPPER",
        "BOOT",
        "BIOS",
        "DOS",
        "CMD/COMMAND",
        "CMD/MEM",
        "CMD/FC",
        "CMD/BACKUP",
        "CMD/JOIN",
        "CMD/SUBST",
    ]
    for rel in dirs:
        r = subprocess.run([cknmake], cwd=stage / rel, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"cknmake failed in {rel}:\n{r.stdout}\n{r.stderr}")

    # Build FLUSH13.EXE in DEV/SMARTDRV
    sd = stage / "DEV" / "SMARTDRV"
    bld_inc = str(stage / "TOOLS" / "BLD" / "INC")
    for cmd in [
        [str(tool_dir / "ckcl"), "-c", "-AS", "-Os", "-Zp", "-I.", "-I../../h", f"-I{bld_inc}", "-Foflush13.obj", "FLUSH13.C"],
        [str(tool_dir / "ckmasm"), "-Mx", "-t", "-I.", "-I../../INC", "-I../../DOS", "FL13.ASM,fl13.obj;"],
        [str(tool_dir / "ckmasm"), "-Mx", "-t", "-I.", "-I../../INC", "-I../../DOS", "FLMES.ASM,flmes.obj;"],
        [str(tool_dir / "cklink"), "@FLUSH13.LNK"],
    ]:
        subprocess.run(cmd, cwd=sd, env=env, check=True, capture_output=True, text=True)


def read_vga_text(vga_path: pathlib.Path) -> str:
    raw = vga_path.read_bytes()
    lines = []
    for r in range(25):
        line = "".join(
            chr(raw[r * 160 + c * 2]) if 32 <= raw[r * 160 + c * 2] < 127 else " "
            for c in range(80)
        ).rstrip()
        if line:
            lines.append(line)
    return "\n".join(lines)


def run_qemu_smoke(img_path: pathlib.Path, vga_path: pathlib.Path, timeout_s: float = 15.0) -> str:
    proc = subprocess.Popen(
        [
            "qemu-system-i386",
            "-m", "4",
            "-drive", f"file={img_path},format=raw,if=ide",
            "-display", "none",
            "-monitor", "stdio",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    assert proc.stdin is not None
    deadline = time.monotonic() + timeout_s
    screen = ""
    try:
        time.sleep(2.5)
        while time.monotonic() < deadline:
            proc.stdin.write(f'pmemsave 0xb8000 4000 "{vga_path}"\n')
            proc.stdin.flush()
            time.sleep(0.4)
            if vga_path.exists():
                screen = read_vga_text(vga_path)
                if "==QEMU_SMOKE_DONE==" in screen:
                    break
        proc.stdin.write("quit\n")
        proc.stdin.flush()
        proc.wait(timeout=5)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)
    return screen


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify MS-DOS 4 boot and utilities in QEMU")
    parser.add_argument("--repo-root", type=Path if False else pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent)
    parser.add_argument("--tool-dir", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent / "build")
    args = parser.parse_args()

    for tool in ["qemu-system-i386", "mformat", "mcopy", "mattrib"]:
        if shutil.which(tool) is None:
            print(f"ERROR: required host tool not found in PATH: {tool}", file=sys.stderr)
            return 1

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = pathlib.Path(tmp)
        stage = tmp_path / "stage"
        img_path = tmp_path / "dos4_hdd.img"
        vga_path = tmp_path / "vga.bin"

        build_boot_and_utilities(args.repo_root.resolve(), args.tool_dir.resolve(), stage)
        make_hdd_image(img_path, stage)
        screen = run_qemu_smoke(img_path, vga_path)

    print("=== QEMU VGA Screen ===")
    print(screen)

    expected = [
        "MS-DOS Version 4.00",
        "655360 bytes total memory",
        "***** A.TXT",
        "hello dos 4",
        "fc: A.TXT longer than B.TXT",
        "No source drive specified",
        "SMARTDRV device not found",
        "==QEMU_SMOKE_DONE==",
    ]
    missing = [e for e in expected if e not in screen]
    if missing:
        print(f"ERROR: missing expected QEMU output: {missing}", file=sys.stderr)
        return 1
    print("QEMU MS-DOS 4 boot & utility smoke test PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
