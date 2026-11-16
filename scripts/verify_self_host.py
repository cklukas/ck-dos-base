#!/usr/bin/env python3
"""Verify 16-bit DOS self-hosting ck* tools inside MS-DOS 4 under QEMU
and assert byte-for-byte identical output against host ck* tools (WP-9)."""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import build_dos_tools as bdt
import verify_qemu_boot as vqb


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify 16-bit DOS self-hosting ck* tools in QEMU")
    parser.add_argument("--repo-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent)
    parser.add_argument("--tool-dir", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent / "build")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    tool_dir = args.tool_dir.resolve()

    for tool in ["qemu-system-i386", "mformat", "mcopy", "mattrib", "mmd"]:
        if shutil.which(tool) is None:
            print(f"ERROR: required host tool not found in PATH: {tool}", file=sys.stderr)
            return 1

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = pathlib.Path(tmp)
        stage = tmp_path / "stage"
        dos_bin = tmp_path / "dos_bin"
        img_path = tmp_path / "dos4_selfhost.img"
        vga_path = tmp_path / "vga.bin"
        host_out = tmp_path / "host_out"
        dos_out = tmp_path / "dos_out"
        host_out.mkdir()
        dos_out.mkdir()

        # 1. Build MS-DOS 4 OS + utilities on host
        vqb.build_boot_and_utilities(repo_root, tool_dir, stage)

        # 2. Build 16-bit DOS ck* tools using ckcl/ckmasm/cklib/cklink
        dos_exes = bdt.build_all_dos_tools(repo_root, tool_dir, dos_bin)

        # 3. Prepare reference inputs & host reference outputs
        usa_msg = vqb.find_ci(stage / "MESSAGES", "USA-MS.MSG")
        mem_skl = vqb.find_ci(stage / "CMD" / "MEM", "MEM.SKL")
        nosrv_skl = vqb.find_ci(stage / "DEV" / "XMAEM", "XMAEM.SKL")
        sample_exe = vqb.find_ci(stage / "CMD" / "MEM", "mem.exe")
        # Use a tiny MZ image for exe2bin / convert tests: FDBOOT or COMMAND
        # Build a small .BIN from CMD/COMMAND/command.com's .exe if available, or test sample_exe with ckexefix / ckconv / ckcmprss
        raw_sample = tmp_path / "SAMPLE.DAT"
        raw_sample.write_bytes(b"AAAAABBBBBCCCCC\r\n\x1a\xff" * 40)

        # Host ckbuildmsg
        subprocess.run(
            [str(tool_dir / "ckbuildmsg"), str(usa_msg.with_suffix("")), str(mem_skl)],
            cwd=host_out,
            check=True,
        )
        # Host cknosrvbld
        subprocess.run(
            [str(tool_dir / "cknosrvbld"), str(nosrv_skl), str(usa_msg)],
            cwd=host_out,
            check=True,
        )
        # Host ckcompress
        subprocess.run(
            [str(tool_dir / "ckcompress"), str(raw_sample), str(host_out / "COMP.RLE")],
            check=True,
        )
        shutil.copy2(raw_sample, host_out / "DECOMP.DAT")
        # Host ckexefix
        shutil.copy2(sample_exe, host_out / "FIXTEST.EXE")
        subprocess.run(
            [str(tool_dir / "ckexefix"), str(host_out / "FIXTEST.EXE"), "4000", "200"],
            check=True,
        )
        # Host ckconvert on a <30KB MZ .EXE (CKCONV.EXE itself, 18KB)
        tiny_exe = dos_bin / "CKCONV.EXE"
        subprocess.run(
            [str(tool_dir / "ckconvert"), str(tiny_exe), str(host_out / "CONV.COM")],
            check=True,
        )
        # Find an exe2bin-compatible exe (initial_cs=0, ss=0, sp=0) e.g. CMD/BACKUP/backup.exc or BOOT
        exe2bin_src = None
        for d, n in [("CMD/BACKUP", "backup.exc"), ("CMD/COMMAND", "command.exc"), ("BIOS", "msbio.bin")]:
            p = vqb.find_ci(stage / d, n)
            if p.exists():
                exe2bin_src = p
                break
        if exe2bin_src is None:
            # Build a minimal valid MZ .EXE (initial_cs=0, ss=0, sp=0, ip=0)
            hdr = bytearray(32)
            struct.pack_into("<14H", hdr, 0,
                0x5A4D, 48, 1, 0, 2, 0, 0xFFFF, 0, 0, 0, 0, 0, 32, 0)
            payload = b"\xb8\x00\x4c\xcd\x21" + b"\x90" * 11
            exe2bin_src = tmp_path / "E2B_TEST.EXE"
            exe2bin_src.write_bytes(bytes(hdr) + payload)
        subprocess.run(
            [str(tool_dir / "ckexe2bin"), str(exe2bin_src), str(host_out / "E2B.BIN")],
            check=True,
        )
        # Host cklib on two .OBJ files from dos_bin/obj
        obj1 = vqb.find_ci(dos_bin / "obj", "CRT0.OBJ")
        obj2 = vqb.find_ci(dos_bin / "obj", "CRT_MEM.OBJ")
        subprocess.run(
            [str(tool_dir / "cklib"), str(host_out / "TEST.LIB"), f"+{obj1}", f"+{obj2}"],
            check=True,
        )
        # Host ckmasm + cklink on a tiny .ASM referencing _memcpy from TEST.LIB
        hi_asm = tmp_path / "HI.ASM"
        hi_asm.write_text(
            "PUBLIC _main\n"
            "EXTRN _memcpy:NEAR\n"
            "_TEXT SEGMENT BYTE PUBLIC 'CODE'\n"
            "ASSUME CS:_TEXT\n"
            "_main PROC NEAR\n"
            "    mov ax, OFFSET _memcpy\n"
            "    xor ax, ax\n"
            "    ret\n"
            "_main ENDP\n"
            "_TEXT ENDS\n"
            "END\n",
            encoding="ascii",
        )
        hi_obj = host_out / "HI.OBJ"
        subprocess.run([str(tool_dir / "ckmasm"), "../HI.ASM,HI.OBJ;"], cwd=host_out, check=True)
        subprocess.run(
            [str(tool_dir / "cklink"), "HI.OBJ,LINKED.EXE,nul,TEST.LIB;"],
            cwd=host_out,
            check=True,
        )
        hi_obj = tmp_path / "HI.OBJ"
        shutil.move(str(host_out / "HI.OBJ"), str(hi_obj))
        shutil.copy2(host_out / "TEST.LIB", host_out / "MK.LIB")
        shutil.copy2(host_out / "LINKED.EXE", host_out / "MK.EXE")

        makefile_txt = (
            "# Self-hosting NMAKE test\r\n"
            "OBJS = C:\\IN\\HI.OBJ\r\n"
            "LIBS = C:\\OUT\\MK.LIB\r\n"
            "\r\n"
            "all: C:\\OUT\\MK.EXE\r\n"
            "\r\n"
            "C:\\OUT\\MK.LIB: C:\\IN\\CRT0.OBJ C:\\IN\\CRT_MEM.OBJ\r\n"
            "\tCKLIB $@ +C:\\IN\\CRT0.OBJ +C:\\IN\\CRT_MEM.OBJ\r\n"
            "\r\n"
            "C:\\OUT\\MK.EXE: $(OBJS) $(LIBS)\r\n"
            "\tCKLINK $(OBJS),$@,NUL,$(LIBS);\r\n"
        )
        makefile_path = tmp_path / "MAKEFILE"
        makefile_path.write_bytes(makefile_txt.encode("ascii"))

        # 4. Build bootable HDD image with DOS tools + inputs + AUTOEXEC.BAT
        vqb.make_hdd_image(img_path, stage)
        spec = f"{img_path}@@{17 * 512}"
        subprocess.run(["mmd", "-i", spec, "::BIN"], check=True)
        subprocess.run(["mmd", "-i", spec, "::IN"], check=True)
        subprocess.run(["mmd", "-i", spec, "::OUT"], check=True)
        for exe in dos_exes:
            subprocess.run(["mcopy", "-o", "-i", spec, str(exe), f"::BIN/{exe.name}"], check=True)

        subprocess.run(["mcopy", "-o", "-i", spec, str(usa_msg), "::IN/USA-MS.MSG"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(mem_skl), "::IN/MEM.SKL"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(nosrv_skl), "::IN/XMAEM.SKL"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(raw_sample), "::IN/SAMPLE.DAT"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(sample_exe), "::IN/FIXTEST.EXE"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(tiny_exe), "::IN/CONV.EXE"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(exe2bin_src), "::IN/E2B.EXE"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(obj1), "::IN/CRT0.OBJ"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(obj2), "::IN/CRT_MEM.OBJ"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(hi_obj), "::IN/HI.OBJ"], check=True)
        subprocess.run(["mcopy", "-o", "-i", spec, str(makefile_path), "::IN/MAKEFILE"], check=True)

        bat = (
            "@ECHO OFF\r\n"
            "PATH C:\\BIN;C:\\\r\n"
            "CD C:\\OUT\r\n"
            "CKBLDMSG C:\\IN\\USA-MS C:\\IN\\MEM.SKL\r\n"
            "CKNOSRV C:\\IN\\XMAEM.SKL C:\\IN\\USA-MS.MSG\r\n"
            "CKCMPRSS C:\\IN\\SAMPLE.DAT C:\\OUT\\COMP.RLE\r\n"
            "CKCMPRSS -d C:\\OUT\\COMP.RLE C:\\OUT\\DECOMP.DAT\r\n"
            "COPY C:\\IN\\FIXTEST.EXE C:\\OUT\\FIXTEST.EXE > NUL\r\n"
            "CKEXEFIX C:\\OUT\\FIXTEST.EXE 4000 200\r\n"
            "CKCONV C:\\IN\\CONV.EXE C:\\OUT\\CONV.COM\r\n"
            "CKEXE2BN C:\\IN\\E2B.EXE C:\\OUT\\E2B.BIN\r\n"
            "CKLIB C:\\OUT\\TEST.LIB +C:\\IN\\CRT0.OBJ +C:\\IN\\CRT_MEM.OBJ\r\n"
            "CKLINK C:\\IN\\HI.OBJ,C:\\OUT\\LINKED.EXE,NUL,C:\\OUT\\TEST.LIB;\r\n"
            "CKNMAKE -f C:\\IN\\MAKEFILE\r\n"
            "ECHO ==QEMU_SMOKE_DONE==\r\n"
        )

        bat_file = tmp_path / "AUTOEXEC.BAT"
        bat_file.write_bytes(bat.encode("ascii"))
        subprocess.run(["mcopy", "-o", "-i", spec, str(bat_file), "::AUTOEXEC.BAT"], check=True)

        # 5. Boot QEMU and run AUTOEXEC.BAT
        screen = vqb.run_qemu_smoke(img_path, vga_path, timeout_s=30.0)
        print("=== QEMU Self-Host VGA Screen ===")
        print(screen)
        if "==QEMU_SMOKE_DONE==" not in screen:
            print("ERROR: QEMU self-host batch did not finish", file=sys.stderr)
            return 1

        # 6. Extract C:\OUT\* and compare byte-for-byte against host_out!
        subprocess.run(["mcopy", "-s", "-o", "-i", spec, "::OUT/*", str(dos_out)], check=True)
        expected_files = [f.name for f in host_out.iterdir() if f.is_file()]
        for fname in sorted(expected_files):
            host_bytes = (host_out / fname).read_bytes()
            dos_file = vqb.find_ci(dos_out, fname)
            if not dos_file.exists():
                print(f"ERROR: DOS tool did not produce {fname}", file=sys.stderr)
                return 1
            dos_bytes = dos_file.read_bytes()
            if dos_bytes != host_bytes:
                print(
                    f"ERROR: byte mismatch for {fname}: host={len(host_bytes)}B vs dos={len(dos_bytes)}B",
                    file=sys.stderr,
                )
                import difflib
                h_lines = host_bytes.decode("latin1", errors="replace").splitlines()
                d_lines = dos_bytes.decode("latin1", errors="replace").splitlines()
                diff = list(difflib.unified_diff(h_lines, d_lines, lineterm=""))
                for line in [l for l in diff if not l.startswith("-")][:30]:
                    print(line, file=sys.stderr)
                return 1
            print(f"MATCH {fname}: {len(dos_bytes)} bytes identical")

    print("WP-9 self-hosting QEMU verification PASSED (byte-for-byte identical)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
