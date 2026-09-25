#!/usr/bin/env python3
"""
Extração completa das texturas de cabelo e cabeça do Link Adulto (e Criança)
(The Legend of Zelda: Ocarina of Time 3D - TriAevum).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent / "oot3d" / "oot3d_asset_tool" / "src"))

from oot3d_asset_tool.zar import ZarArchive
from oot3d_asset_tool.cmb import CmbModel
sys.path.insert(0, str(Path(__file__).parent))
from extract_adult_menu_item_textures import (
    RomFsReader,
    decode_to_rgba8888,
    get_native_format_number,
    compute_azahar_cityhash,
)


def main():
    root_dir = Path(__file__).resolve().parents[1]
    rom_path = Path("/media/windroid/SSD KING/Legend of Zelda, The - Ocarina of Time 3D (USA) (En,Fr,Es).3ds")
    if not rom_path.is_file():
        print(f"Erro: ROM não encontrada em {rom_path}")
        sys.exit(1)

    out_base = root_dir / "texturas_cabelo_link"
    gameplay_dir = out_base / "cabelo_gameplay_link_adulto"
    menu_dir = out_base / "cabelo_menu_pausa_link_adulto"
    abertura_dir = out_base / "cabelo_cutscene_abertura_link_adulto"
    crianca_dir = out_base / "cabelo_link_crianca"

    for d in [gameplay_dir, menu_dir, abertura_dir, crianca_dir]:
        d.mkdir(parents=True, exist_ok=True)

    print("== Extraindo texturas de cabelo do Link Adulto ==")
    reader = RomFsReader(rom_path)
    catalog = []
    pack_map = {}

    # 1. Cabelo de Gameplay do Link Adulto (zelda_link_boy_new.zar -> link_v2.cmb)
    print("\n[+] Extraindo cabelo principal de gameplay do Link Adulto (link_v2.cmb)...")
    zar_boy_data = reader.read_file_by_path("/actor/zelda_link_boy_new.zar")
    if zar_boy_data:
        zar_boy = ZarArchive.parse(zar_boy_data, "zelda_link_boy_new.zar")
        for f in zar_boy.files:
            if f.name == "boy/model/link_v2.cmb":
                cmb_data = zar_boy.read_file(f)
                cmb = CmbModel.parse(cmb_data, f.name)
                hair_map = {
                    "link_f01": ("cabelo_franja_mechas_adulto", "Franja, costeletas e mechas principais de cabelo do Link Adulto"),
                    "link_f00": ("raiz_cabelo_cabeca_orelhas_adulto", "Couro cabeludo, raízes do cabelo e orelhas do Link Adulto"),
                }
                for t in cmb.textures:
                    if t.name in hair_map:
                        dest_key, desc = hair_map[t.name]
                        cityhash = compute_azahar_cityhash(t.data)
                        fmt_num = get_native_format_number(t.texture_format, t.data_type)
                        azahar_name = f"tex1_{t.width}x{t.height}_{cityhash}_{fmt_num}_mip0.png"

                        rgba = decode_to_rgba8888(t.data, t.width, t.height, t.texture_format, t.data_type)
                        im = Image.frombytes("RGBA", (t.width, t.height), rgba)
                        out_png = gameplay_dir / f"{dest_key}.png"
                        im.save(out_png)

                        catalog.append({
                            "key": dest_key,
                            "original_name": t.name,
                            "category": "cabelo_gameplay_adulto",
                            "source_file": "actor/zelda_link_boy_new.zar:boy/model/link_v2.cmb",
                            "description": desc,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_map[cityhash] = [f"cabelo_gameplay_link_adulto/{dest_key}.png"]
                        print(f"  -> {dest_key}.png ({t.width}x{t.height}) -> Hash: {cityhash} -> {azahar_name}")

    # 2. Cabelo do Link no Menu de Pausa (misc/menu_link.zar -> menu_link_omote.cmb)
    print("\n[+] Extraindo cabelo do Link no Menu de Pausa (menu_link_omote.cmb)...")
    menu_link_data = reader.read_file_by_path("/misc/menu_link.zar")
    if menu_link_data:
        zar_menu = ZarArchive.parse(menu_link_data, "menu_link.zar")
        for f in zar_menu.files:
            if f.name == "menu_link_omote.cmb":
                cmb_data = zar_menu.read_file(f)
                cmb = CmbModel.parse(cmb_data, f.name)
                menu_hair = {
                    "link_f01": ("menu_cabelo_mechas_adulto", "Mechas e franja de cabelo do Link no Menu de Status"),
                    "n_link_f01": ("menu_cabelo_normal_map_adulto", "Normal map (relevo dos fios) do cabelo no Menu"),
                    "link_f00": ("menu_cabeca_raiz_cabelo_adulto", "Textura HD da cabeça e raiz do cabelo no Menu"),
                    "n_link_f00": ("menu_cabeca_normal_map_adulto", "Normal map da cabeça e orelhas no Menu"),
                }
                for t in cmb.textures:
                    if t.name in menu_hair:
                        dest_key, desc = menu_hair[t.name]
                        cityhash = compute_azahar_cityhash(t.data)
                        fmt_num = get_native_format_number(t.texture_format, t.data_type)
                        azahar_name = f"tex1_{t.width}x{t.height}_{cityhash}_{fmt_num}_mip0.png"

                        rgba = decode_to_rgba8888(t.data, t.width, t.height, t.texture_format, t.data_type)
                        im = Image.frombytes("RGBA", (t.width, t.height), rgba)
                        out_png = menu_dir / f"{dest_key}.png"
                        im.save(out_png)

                        catalog.append({
                            "key": dest_key,
                            "original_name": t.name,
                            "category": "cabelo_menu_adulto",
                            "source_file": "misc/menu_link.zar:menu_link_omote.cmb",
                            "description": desc,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_map[cityhash] = [f"cabelo_menu_pausa_link_adulto/{dest_key}.png"]
                        print(f"  -> {dest_key}.png ({t.width}x{t.height}) -> Hash: {cityhash} -> {azahar_name}")

    # 3. Cabelo na Cena de Abertura (zelda_link_opening.zar -> link_opening.cmb)
    print("\n[+] Extraindo cabelo da cutscene de abertura (link_opening.cmb)...")
    open_data = reader.read_file_by_path("/actor/zelda_link_opening.zar")
    if open_data:
        zar_open = ZarArchive.parse(open_data, "zelda_link_opening.zar")
        for f in zar_open.files:
            if f.name == "boy/model/link_opening.cmb":
                cmb_data = zar_open.read_file(f)
                cmb = CmbModel.parse(cmb_data, f.name)
                open_hair = {
                    "link_f01": ("abertura_cabelo_mechas_adulto", "Cabelo do Link cavalgando Epona na introdução"),
                    "link_f00": ("abertura_raiz_cabelo_cabeca_adulto", "Cabeça e raiz do cabelo na introdução"),
                }
                for t in cmb.textures:
                    if t.name in open_hair:
                        dest_key, desc = open_hair[t.name]
                        cityhash = compute_azahar_cityhash(t.data)
                        fmt_num = get_native_format_number(t.texture_format, t.data_type)
                        azahar_name = f"tex1_{t.width}x{t.height}_{cityhash}_{fmt_num}_mip0.png"

                        rgba = decode_to_rgba8888(t.data, t.width, t.height, t.texture_format, t.data_type)
                        im = Image.frombytes("RGBA", (t.width, t.height), rgba)
                        out_png = abertura_dir / f"{dest_key}.png"
                        im.save(out_png)

                        catalog.append({
                            "key": dest_key,
                            "original_name": t.name,
                            "category": "cabelo_abertura_adulto",
                            "source_file": "actor/zelda_link_opening.zar:boy/model/link_opening.cmb",
                            "description": desc,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_map[cityhash] = [f"cabelo_cutscene_abertura_link_adulto/{dest_key}.png"]
                        print(f"  -> {dest_key}.png ({t.width}x{t.height}) -> Hash: {cityhash} -> {azahar_name}")

    # 4. Cabelo do Link Criança (zelda_link_child_new.zar -> childlink_v2.cmb)
    print("\n[+] Extraindo cabelo do Link Criança para referência (childlink_v2.cmb)...")
    child_data = reader.read_file_by_path("/actor/zelda_link_child_new.zar")
    if child_data:
        zar_child = ZarArchive.parse(child_data, "zelda_link_child_new.zar")
        for f in zar_child.files:
            if f.name == "child/model/childlink_v2.cmb":
                cmb_data = zar_child.read_file(f)
                cmb = CmbModel.parse(cmb_data, f.name)
                child_hair = {
                    "childlink_f01": ("cabelo_mechas_crianca", "Franja e cabelo do Link Criança"),
                    "childlink_f00": ("raiz_cabelo_cabeca_crianca", "Cabeça e raízes do cabelo do Link Criança"),
                }
                for t in cmb.textures:
                    if t.name in child_hair:
                        dest_key, desc = child_hair[t.name]
                        cityhash = compute_azahar_cityhash(t.data)
                        fmt_num = get_native_format_number(t.texture_format, t.data_type)
                        azahar_name = f"tex1_{t.width}x{t.height}_{cityhash}_{fmt_num}_mip0.png"

                        rgba = decode_to_rgba8888(t.data, t.width, t.height, t.texture_format, t.data_type)
                        im = Image.frombytes("RGBA", (t.width, t.height), rgba)
                        out_png = crianca_dir / f"{dest_key}.png"
                        im.save(out_png)

                        catalog.append({
                            "key": dest_key,
                            "original_name": t.name,
                            "category": "cabelo_crianca",
                            "source_file": "actor/zelda_link_child_new.zar:child/model/childlink_v2.cmb",
                            "description": desc,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_map[cityhash] = [f"cabelo_link_crianca/{dest_key}.png"]
                        print(f"  -> {dest_key}.png ({t.width}x{t.height}) -> Hash: {cityhash} -> {azahar_name}")

    reader.close()

    # Salva o Catálogo JSON
    cat_path = out_base / "tabela_hashes_cabelo_link.json"
    cat_path.write_text(json.dumps(catalog, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n[OK] Tabela de hashes salva em {cat_path} com {len(catalog)} texturas catalogadas.")

    # Gera pack.json para modding direto de cabelo
    pack_manifest = {
        "options": {
            "skip_mipmap": True,
            "flip_png_files": True,
            "use_new_hash": True
        },
        "textures": pack_map
    }
    (out_base / "pack.json").write_text(json.dumps(pack_manifest, indent=2), encoding="utf-8")

    # README
    readme_text = f"""# Texturas do Cabelo do Link (The Legend of Zelda: Ocarina of Time 3D)

Este diretório contém a extração completa e precisa de todas as texturas de cabelo, franja, costeletas e couro cabeludo do Link Adulto (e Criança) decodificadas em PNG 32-bit de alta fidelidade e catalogadas com hashes canônicos CityHash64 para texturas personalizadas.

---

## 💇 Categorias de Cabelo Extraídas

### 1. `cabelo_gameplay_link_adulto/` (Cabelo do Modelo 3D Principal)
* **`cabelo_franja_mechas_adulto.png`** (32x128): Mechas principais, franja e costeletas douradas do Link Adulto.
* **`raiz_cabelo_cabeca_orelhas_adulto.png`** (64x128): Couro cabeludo, base do cabelo e orelhas hylianas.

### 2. `cabelo_menu_pausa_link_adulto/` (Link na Tela de Status/Equipamentos)
* **`menu_cabelo_mechas_adulto.png`** (32x128): Textura difusa do cabelo no menu.
* **`menu_cabelo_normal_map_adulto.png`** (32x128): Mapa de relevo e reflexo dos fios de cabelo no menu.
* **`menu_cabeca_raiz_cabelo_adulto.png`** (128x128): Textura de alta definição da cabeça e raiz do cabelo.
* **`menu_cabeca_normal_map_adulto.png`** (128x128): Normal map da cabeça.

### 3. `cabelo_cutscene_abertura_link_adulto/` (Link na Cena de Introdução com a Epona)
* **`abertura_cabelo_mechas_adulto.png`** (32x128): Cabelo do Link na introdução.
* **`abertura_raiz_cabelo_cabeca_adulto.png`** (64x128): Cabeça e raiz do cabelo na introdução.

### 4. `cabelo_link_crianca/` (Para Referência e Customização do Link Jovem)
* **`cabelo_mechas_crianca.png`** (32x128): Franja e mechas do Link Criança.
* **`raiz_cabelo_cabeca_crianca.png`** (64x128): Cabeça e raiz do cabelo do Link Criança.

---

## 🎨 Como Criar Textura Personalizada do Cabelo

1. Abra `cabelo_franja_mechas_adulto.png` ou `menu_cabelo_mechas_adulto.png` no seu editor gráfico (Photoshop, GIMP, Krita, Aseprite).
2. Modifique a cor do cabelo (ex.: Cabelo Castanho / Twilight Princess, Cabelo Preto de Dark Link, Cabelo Branco/Prateado estilo Fierce Deity, ou Loiro Platina / Anime).
3. Salve a textura modificada e aplique no TriAevum via pasta de texturas ou pelo seletor de arquivos de texturas no Android.
"""
    (out_base / "README.md").write_text(readme_text, encoding="utf-8")
    print(f"\n[OK] Concluído com sucesso! Pasta gerada em:\n  {out_base}")


if __name__ == "__main__":
    main()
