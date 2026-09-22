#!/usr/bin/env python3
"""
Extrai e gera os fontes Whole-AOT C++ (256 shards) a partir de uma ROM (.3ds/.cci)
do The Legend of Zelda: Ocarina of Time 3D (USA ou EUR).
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
import zipfile

REPO_ROOT = Path(__file__).resolve().parents[2]
ADAPTER_BIN_PATH = REPO_ROOT / "ports/android/app/src/main/assets/adapters/oot3d_usa_code_copies.bin"
ADAPTER_JSON_PATH = REPO_ROOT / "tools/triaevum_release/input_coverage/recipes/adapters/oot3d-usa-ef210566-copies.json"
DEFAULT_WORK_DIR = Path("/home/windroid/triaevum-build/work-ir")
DEFAULT_OUT_DIR = Path("/home/windroid/triaevum-build/translated-sources")

sys.path.insert(0, str(REPO_ROOT / "tools/triaevum_release"))
sys.path.insert(0, str(REPO_ROOT / "tools/oot3d/native_a32_runtime"))

import ctr_rom
from generate_aot import SUPPLEMENTAL_ENTRIES, UPSTREAM, add_local_entry_intervals
from whole_aot_cpp import generate
from whole_aot_program import DEFAULT_BASE, main as extract_program

CANONICAL_EUR_SHA256 = "16a6b0aa4c4784680220a6f780f7f8a73cfb205557aa9f9f0e705179e0613220"
USA_REV1_SHA256 = "ef210566e1d9d16879a746dfb063fcbad232f0171d860de906531ecc526cc020"


def find_rom_candidate(specified: Path | None) -> Path:
    if specified and specified.is_file():
        return specified

    search_dirs = [REPO_ROOT, REPO_ROOT.parent, Path.cwd()]
    for directory in search_dirs:
        if not directory.is_dir():
            continue
        for ext in ("*.3ds", "*.cci"):
            candidates = list(directory.glob(ext))
            if candidates:
                # Prefer files mentioning Zelda or OoT
                for c in candidates:
                    if "zelda" in c.name.lower() or "oot" in c.name.lower():
                        return c
                return candidates[0]

    raise FileNotFoundError(
        "Nenhuma ROM 3DS (.3ds / .cci) encontrada. Especifique com --rom <caminho>."
    )


def adapt_usa_code(raw_code: bytes) -> bytes:
    if ADAPTER_BIN_PATH.is_file():
        with ADAPTER_BIN_PATH.open("rb") as f:
            count = struct.unpack("<I", f.read(4))[0]
            code = bytearray(4567040)
            out_pos = 0
            for _ in range(count):
                origin, length = struct.unpack("<II", f.read(8))
                code[out_pos : out_pos + length] = raw_code[origin : origin + length]
                out_pos += length
        return bytes(code)
    elif ADAPTER_JSON_PATH.is_file():
        import input_copy_adapter

        program = json.loads(ADAPTER_JSON_PATH.read_text(encoding="utf-8"))
        return input_copy_adapter.apply_program(raw_code, program)
    else:
        raise FileNotFoundError("Adaptador USA->EUR não encontrado no repositório.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Extrai e gera os fontes Whole-AOT C++ a partir da ROM 3DS"
    )
    parser.add_argument(
        "--rom",
        type=Path,
        default=None,
        help="Caminho para a ROM 3DS / CCI (auto-detectado se omitido)",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=DEFAULT_WORK_DIR,
        help="Diretório de trabalho para arquivos intermediários",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUT_DIR,
        help="Diretório de saída para os fontes C++ (shards)",
    )
    parser.add_argument(
        "--zip",
        type=Path,
        default=None,
        help="Criar arquivo zip compactado com os fontes gerados (ex: source.zip)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Regerar mesmo se já existir",
    )
    args = parser.parse_args(argv)

    work_dir: Path = args.work_dir
    out_dir: Path = args.output
    work_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)

    rom_path = find_rom_candidate(args.rom)
    print(f"1. ROM localizada: {rom_path} ({rom_path.stat().st_size:,} bytes)")

    # 1. Extração do code.bin e exheader.bin
    with rom_path.open("rb") as f:
        layout = ctr_rom._find_title_layout(f, rom_path.stat().st_size)
        raw_code = ctr_rom._read_exact(f, layout.code_offset, layout.code_size, "code")
        if layout.compressed_code:
            print("   Descomprimindo code.bin (LZSS)...")
            raw_code = ctr_rom.decompress_exefs_code(raw_code)

        f.seek(layout.exheader_offset)
        exheader = f.read(layout.exheader_size)

    raw_sha = hashlib.sha256(raw_code).hexdigest()
    print(f"   code.bin extraído SHA256: {raw_sha}")

    if raw_sha == CANONICAL_EUR_SHA256:
        print("   ROM identificada como EUR canônica. Adaptação não necessária.")
        code = raw_code
    elif raw_sha == USA_REV1_SHA256:
        print("   ROM identificada como USA Rev 1. Aplicando adaptador de cópia oficial...")
        code = adapt_usa_code(raw_code)
        code_digest = hashlib.sha256(code).hexdigest()
        print(f"   code.bin adaptado SHA256: {code_digest}")
        if code_digest != CANONICAL_EUR_SHA256:
            raise ValueError(f"Hash adaptado inválido: {code_digest} != {CANONICAL_EUR_SHA256}")
    else:
        raise ValueError(
            f"ROM não suportada. Hash do code.bin: {raw_sha}. Esperado EUR ({CANONICAL_EUR_SHA256}) ou USA ({USA_REV1_SHA256})."
        )

    adapted_code_path = work_dir / "code.bin"
    adapted_code_path.write_bytes(code)

    exheader_path = work_dir / "exheader.bin"
    exheader_path.write_bytes(exheader)

    executable_size = 3973120
    if len(exheader) >= 0x18:
        parsed_size = int.from_bytes(exheader[0x14:0x18], "little") * 0x1000
        if parsed_size > 0:
            executable_size = parsed_size
    print(f"   Tamanho executável do segmento de texto: {executable_size} bytes (0x{executable_size:X})")

    # 2. Inventário de funções
    print("2. Gerando inventário de funções com entradas de runtime...")
    inventory_path = work_dir / "inventory_with_process_entry.csv"
    add_local_entry_intervals(
        UPSTREAM / "analysis/codebin_function_inventory.csv",
        SUPPLEMENTAL_ENTRIES,
        inventory_path,
        DEFAULT_BASE,
    )

    # 3. Geração do IR AOT
    print("3. Gerando representação intermediária whole_aot_program.json...")
    program_path = work_dir / "whole_aot_program.json"
    if args.force or not program_path.exists():
        extract_args = [
            "--code", str(adapted_code_path),
            "--inventory", str(inventory_path),
            "--boundary-audit", str(UPSTREAM / "analysis/codebin_callable_boundary_residue_audit_166.csv"),
            "--base", hex(DEFAULT_BASE),
            "--executable-size", str(executable_size),
            "--output", str(program_path),
            "--force",
        ]
        ret = extract_program(extract_args)
        if ret != 0:
            print("Erro ao extrair whole_aot_program:", ret)
            return ret
    print(f"   whole_aot_program.json pronto ({program_path.stat().st_size:,} bytes)")

    # 4. Geração dos 256 shards C++
    print("4. Traduzindo funções para C++ (256 shards com estratégia por afinidade)...")
    selection_path = REPO_ROOT / "tools/oot3d/native_a32_runtime/whole_aot_functions.json"
    manifest = generate(
        program_path,
        selection_path,
        adapted_code_path,
        out_dir,
        shard_count=256,
        shard_strategy="affinity",
    )
    print(f"   Sucesso: {len(manifest['functions'])} funções geradas em {manifest['shard_count']} shards.")

    # 5. Emissão do manifesto TITLE_SOURCE_MANIFEST.json
    print("5. Gerando TITLE_SOURCE_MANIFEST.json...")
    title_manifest = {
        "format": "triaevum_translated_title_source_v1",
        "recipe": "oot3d-canonical-eur",
        "build_source_commit": "manual",
        "build": {},
        "files": manifest["files"],
    }
    manifest_path = out_dir / "TITLE_SOURCE_MANIFEST.json"
    manifest_path.write_text(json.dumps(title_manifest, indent=2), encoding="utf-8")
    print(f"   Manifesto salvo com {len(manifest['files'])} arquivos mapeados.")

    # 6. Opcional: Criar source.zip
    if args.zip:
        zip_path = args.zip.resolve()
        zip_path.parent.mkdir(parents=True, exist_ok=True)
        print(f"6. Compactando fontes para {zip_path}...")
        with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for p in sorted(out_dir.iterdir()):
                if p.is_file():
                    zf.write(p, p.name)
        print(f"   Arquivo compactado com sucesso ({zip_path.stat().st_size:,} bytes).")

    print("\n✅ Concluído! Todos os fontes C++ estão prontos para compilação com o NDK.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
