#!/usr/bin/env python3
import hashlib
import json
import struct
import sys
import time
from pathlib import Path

import argparse

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ROM_PATH = Path("/media/windroid/SSD KING/Legend of Zelda, The - Ocarina of Time 3D (USA) (En,Fr,Es).3ds")
ADAPTER_PATH = REPO_ROOT / "ports/android/app/src/main/assets/adapters/oot3d_usa_code_copies.bin"
DEFAULT_WORK_DIR = Path("/home/windroid/triaevum-android-build/work-ir")
DEFAULT_OUT_DIR = Path("/home/windroid/triaevum-android-build/title-sources")

sys.path.insert(0, str(REPO_ROOT / "tools/triaevum_release"))
sys.path.insert(0, str(REPO_ROOT / "tools/oot3d/native_a32_runtime"))

import ctr_rom
from whole_aot_cpp import generate
from whole_aot_program import DEFAULT_BASE, main as extract_program
from generate_aot import UPSTREAM, SUPPLEMENTAL_ENTRIES, add_local_entry_intervals

def main():
    parser = argparse.ArgumentParser(description="Extrai e gera os fontes Whole-AOT C++ a partir da ROM 3DS")
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM_PATH, help="Caminho para a ROM 3DS / CCI")
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK_DIR, help="Diretório de trabalho intermediário")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT_DIR, help="Diretório de saída para os fontes C++")
    args = parser.parse_args()

    work_dir = args.work_dir
    out_dir = args.output
    rom_path = args.rom

    work_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"1. Extraindo e adaptando code.bin da ROM ({rom_path.name})...")
    t0 = time.time()
    with rom_path.open("rb") as f:
        layout = ctr_rom._find_title_layout(f, rom_path.stat().st_size)
        raw_code = ctr_rom._read_exact(f, layout.code_offset, layout.code_size, "code")
        if layout.compressed_code:
            raw_code = ctr_rom.decompress_exefs_code(raw_code)

    with ADAPTER_PATH.open("rb") as f:
        count = struct.unpack("<I", f.read(4))[0]
        code = bytearray(4567040)
        out_pos = 0
        for _ in range(count):
            origin, length = struct.unpack("<II", f.read(8))
            code[out_pos:out_pos + length] = raw_code[origin:origin + length]
            out_pos += length

    code_digest = hashlib.sha256(code).hexdigest()
    print(f"   code.bin adaptado SHA256: {code_digest}")
    assert code_digest == "16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220", "Hash do code.bin não corresponde ao esperado!"

    adapted_code_path = work_dir / "code.bin"
    adapted_code_path.write_bytes(code)

    print("2. Gerando inventário de entradas locais...")
    inventory_path = work_dir / "inventory.csv"
    add_local_entry_intervals(
        UPSTREAM / "analysis/codebin_function_inventory.csv",
        SUPPLEMENTAL_ENTRIES,
        inventory_path,
        DEFAULT_BASE,
    )

    print("3. Extraindo aot_program.json...")
    program_path = work_dir / "aot_program.json"
    if not program_path.exists():
        extract_args = [
            "--code", str(adapted_code_path),
            "--inventory", str(inventory_path),
            "--boundary-audit", str(UPSTREAM / "analysis/codebin_callable_boundary_residue_audit_166.csv"),
            "--base", hex(DEFAULT_BASE),
            "--executable-size", hex(3973120),
            "--output", str(program_path),
        ]
        ret = extract_program(extract_args)
        if ret != 0:
            print("Erro ao extrair aot_program:", ret)
            return ret
    print(f"   aot_program.json pronto ({program_path.stat().st_size} bytes)")

    print("4. Gerando shards C++ (whole_aot_cpp)...")
    selection_path = REPO_ROOT / "tools/oot3d/native_a32_runtime/whole_aot_functions.json"
    manifest = generate(
        program_path,
        selection_path,
        adapted_code_path,
        out_dir,
        shard_count=256,
        shard_strategy="affinity",
    )
    print(f"   Gerados {len(manifest['functions'])} funções em {manifest['shard_count']} shards!")

    print("5. Criando TITLE_SOURCE_MANIFEST.json...")
    manifest_entries = {}
    for p in sorted(out_dir.iterdir()):
        if p.is_file() and p.name.endswith((".cpp", ".h")):
            manifest_entries[p.name] = hashlib.sha256(p.read_bytes()).hexdigest()

    title_manifest_path = out_dir / "TITLE_SOURCE_MANIFEST.json"
    title_manifest_path.write_text(
        json.dumps({
            "format": "triaevum_translated_title_source_v1",
            "files": manifest_entries
        }, indent=2),
        encoding="utf-8"
    )
    return 0

if __name__ == "__main__":
    sys.exit(main())

