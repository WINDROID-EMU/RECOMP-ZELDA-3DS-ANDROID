#!/usr/bin/env python3
"""
Extração completa das texturas da roupa/túnica e vestimentas do Link Adulto
(The Legend of Zelda: Ocarina of Time 3D - TriAevum).
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent / "oot3d" / "oot3d_asset_tool" / "src"))

from oot3d_asset_tool.zar import ZarArchive
from oot3d_asset_tool.cmb import CmbModel
from oot3d_asset_tool.cmab_audit import (
    cmab_txpt_texture_records,
    resolved_txpt_offset,
    tag_offsets,
    CMAB_TXPT_MAGIC,
    CMAB_STRT_MAGIC,
    cmab_string_table_names,
)
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

    out_base = root_dir / "texturas_roupa_link_adulto"
    tunicas_dir = out_base / "tunicas_gameplay"
    vestimentas_dir = out_base / "vestimentas_e_acessorios"
    menu_dir = out_base / "roupas_menu_pausa"
    abertura_dir = out_base / "roupas_cutscene_abertura"

    for d in [tunicas_dir, vestimentas_dir, menu_dir, abertura_dir]:
        d.mkdir(parents=True, exist_ok=True)

    print("== Extraindo texturas das roupas e túnicas do Link Adulto ==")
    reader = RomFsReader(rom_path)
    catalog = []
    pack_map = {}

    # 1. Túnicas Coloridas de Gameplay (zelda_link_boy_new.zar -> link_body.cmab)
    print("\n[+] Extraindo túnicas (Kokiri, Goron, Zora) de link_body.cmab...")
    zar_boy_data = reader.read_file_by_path("/actor/zelda_link_boy_new.zar")
    if zar_boy_data:
        zar_boy = ZarArchive.parse(zar_boy_data, "zelda_link_boy_new.zar")
        for f in zar_boy.files:
            if f.name == "boy/misc/link_body.cmab":
                cmab_data = zar_boy.read_file(f)
                txpt_offsets = tag_offsets(cmab_data, CMAB_TXPT_MAGIC)
                strt_offsets = tag_offsets(cmab_data, CMAB_STRT_MAGIC)
                names = cmab_string_table_names(cmab_data, strt_offsets[0] if strt_offsets else None)
                cand_tex_data = int.from_bytes(cmab_data[0x1C:0x20], "little") if len(cmab_data) >= 0x20 else None
                cand_txpt = int.from_bytes(cmab_data[0x30:0x34], "little") if len(cmab_data) >= 0x34 else None
                txpt_off, _ = resolved_txpt_offset(cmab_data, cand_txpt, txpt_offsets)
                records = cmab_txpt_texture_records(cmab_data, txpt_off, cand_tex_data, names)

                tunic_names = {
                    "link_01g": ("tunica_kokiri_verde", "Túnica Kokiri Verde Oficial (Link Adulto)"),
                    "link_01r": ("tunica_goron_vermelha", "Túnica Goron Vermelha Oficial (Link Adulto)"),
                    "link_01b": ("tunica_zora_azul", "Túnica Zora Azul Oficial (Link Adulto)"),
                }

                if records:
                    for r in records:
                        orig_name = r.get("name")
                        dest_info = tunic_names.get(orig_name)
                        dest_key = dest_info[0] if dest_info else orig_name
                        desc = dest_info[1] if dest_info else "Túnica"

                        d_start = cand_tex_data + r["data_offset"]
                        t_data = cmab_data[d_start : d_start + r["data_size"]]
                        cityhash = compute_azahar_cityhash(t_data)
                        fmt_num = get_native_format_number(r["texture_format"], r["data_type"])
                        azahar_name = f"tex1_{r['width']}x{r['height']}_{cityhash}_{fmt_num}_mip0.png"

                        rgba = decode_to_rgba8888(t_data, r["width"], r["height"], r["texture_format"], r["data_type"])
                        im = Image.frombytes("RGBA", (r["width"], r["height"]), rgba)
                        out_png = tunicas_dir / f"{dest_key}.png"
                        im.save(out_png)

                        catalog.append({
                            "key": dest_key,
                            "original_name": orig_name,
                            "category": "tunica_gameplay",
                            "source_file": "boy/misc/link_body.cmab",
                            "description": desc,
                            "width": r["width"],
                            "height": r["height"],
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_map[cityhash] = [f"tunicas_gameplay/{dest_key}.png"]
                        print(f"  -> {dest_key}.png ({r['width']}x{r['height']}) -> Hash: {cityhash} -> {azahar_name}")

            # 2. Roupa branca e detalhes de vestimenta em link_v2.cmb
            elif f.name == "boy/model/link_v2.cmb":
                print("\n[+] Extraindo roupas de baixo e acessórios de vestuário de link_v2.cmb...")
                cmb_data = zar_boy.read_file(f)
                cmb = CmbModel.parse(cmb_data, f.name)
                
                clothing_map = {
                    "link_00": ("roupa_branca_calcas_camisa", "Roupa branca (calças brancas, mangas internas e gola)"),
                    "link_01g": ("tunica_verde_base_modelo", "Textura base da túnica verde embutida no modelo 3D"),
                    "p_tex01": ("luvas_bracadeiras_couro", "Luvas e protetores de pulso de couro marrom"),
                    "p_tex03": ("cinto_bolsa_couro", "Cinto principal e bolsa de utilidades de couro"),
                    "p_tex05": ("botas_calcados_couro", "Botas de couro marrom cano alto"),
                    "p_tex08": ("detalhes_mangas_tecido", "Dobras das mangas e acabamentos de tecido"),
                    "p_tex09": ("fivelas_luvas_detalhes", "Fivelas metálicas das luvas"),
                    "p_tex12": ("fivelas_cinto_detalhes", "Fivela e passadores metálicos do cinto"),
                    "p_tex16": ("costuras_bainha_roupa", "Costuras e acabamento de borda da túnica"),
                }

                for t in cmb.textures:
                    if t.name in clothing_map:
                        dest_key, desc = clothing_map[t.name]
                        cityhash = compute_azahar_cityhash(t.data)
                        fmt_num = get_native_format_number(t.texture_format, t.data_type)
                        azahar_name = f"tex1_{t.width}x{t.height}_{cityhash}_{fmt_num}_mip0.png"

                        rgba = decode_to_rgba8888(t.data, t.width, t.height, t.texture_format, t.data_type)
                        im = Image.frombytes("RGBA", (t.width, t.height), rgba)
                        out_png = vestimentas_dir / f"{dest_key}.png"
                        im.save(out_png)

                        catalog.append({
                            "key": dest_key,
                            "original_name": t.name,
                            "category": "vestimenta_acessorio",
                            "source_file": "boy/model/link_v2.cmb",
                            "description": desc,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_map[cityhash] = [f"vestimentas_e_acessorios/{dest_key}.png"]
                        print(f"  -> {dest_key}.png ({t.width}x{t.height}) -> Hash: {cityhash} -> {azahar_name}")

    # 3. Roupas do Link exibido no Menu de Pausa (misc/menu_link.zar)
    print("\n[+] Extraindo roupas do Link no Menu de Pausa (misc/menu_link.zar)...")
    menu_link_data = reader.read_file_by_path("/misc/menu_link.zar")
    if menu_link_data:
        zar_menu = ZarArchive.parse(menu_link_data, "menu_link.zar")
        for f in zar_menu.files:
            if f.name == "menu_link_omote.cmb":
                cmb_data = zar_menu.read_file(f)
                cmb = CmbModel.parse(cmb_data, f.name)
                menu_cloth = {
                    "link_01g": ("menu_tunica_kokiri_verde", "Túnica Kokiri do modelo de Link no Menu"),
                    "n_link_01g": ("menu_tunica_normal_map", "Normal map (relevo) da túnica no Menu"),
                    "link_00": ("menu_roupa_branca", "Roupa branca (calças e mangas) no Menu"),
                    "n_link_00": ("menu_roupa_branca_normal_map", "Normal map da roupa branca no Menu"),
                    "p_tex01": ("menu_luvas_couro", "Luvas de couro no Menu"),
                    "n_p_tex01": ("menu_luvas_couro_normal_map", "Normal map das luvas no Menu"),
                }
                for t in cmb.textures:
                    if t.name in menu_cloth:
                        dest_key, desc = menu_cloth[t.name]
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
                            "category": "roupa_menu_link",
                            "source_file": "misc/menu_link.zar:menu_link_omote.cmb",
                            "description": desc,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_map[cityhash] = [f"roupas_menu_pausa/{dest_key}.png"]
                        print(f"  -> {dest_key}.png ({t.width}x{t.height}) -> Hash: {cityhash} -> {azahar_name}")

    # 4. Roupas da Cutscene de Abertura (actor/zelda_link_opening.zar)
    print("\n[+] Extraindo roupas da cena de abertura (actor/zelda_link_opening.zar)...")
    open_data = reader.read_file_by_path("/actor/zelda_link_opening.zar")
    if open_data:
        zar_open = ZarArchive.parse(open_data, "zelda_link_opening.zar")
        for f in zar_open.files:
            if f.name == "boy/model/link_opening.cmb":
                cmb_data = zar_open.read_file(f)
                cmb = CmbModel.parse(cmb_data, f.name)
                open_cloth = {
                    "link_01g": ("abertura_tunica_verde", "Túnica verde de Link cavalgando Epona na abertura"),
                    "link_00": ("abertura_roupa_branca", "Calça e camisa branca de Link na abertura"),
                    "p_tex01": ("abertura_luvas_couro", "Luvas de couro de Link na abertura"),
                }
                for t in cmb.textures:
                    if t.name in open_cloth:
                        dest_key, desc = open_cloth[t.name]
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
                            "category": "roupa_abertura",
                            "source_file": "actor/zelda_link_opening.zar:boy/model/link_opening.cmb",
                            "description": desc,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_map[cityhash] = [f"roupas_cutscene_abertura/{dest_key}.png"]
                        print(f"  -> {dest_key}.png ({t.width}x{t.height}) -> Hash: {cityhash} -> {azahar_name}")

    reader.close()

    # Salva o Catálogo JSON
    cat_path = out_base / "tabela_hashes_roupa_link.json"
    cat_path.write_text(json.dumps(catalog, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n[OK] Tabela de hashes salva em {cat_path} com {len(catalog)} texturas catalogadas.")

    # Gera pack.json para modding direto
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
    readme_text = f"""# Texturas das Roupas e Túnicas do Link Adulto
The Legend of Zelda: Ocarina of Time 3D (TriAevum Recomp)

Este diretório contém a extração completa e precisa de todas as texturas de roupa, túnicas e vestimentas do Link Adulto decodificadas em PNG 32-bit com canais RGBA preservados e catalogadas com hashes canônicos CityHash64 para texturas personalizadas.

---

## 👕 Categorias de Roupas Extraídas

### 1. `tunicas_gameplay/` (Túnicas Oficiais de Jogo)
* **`tunica_kokiri_verde.png`** (128x128): Túnica Kokiri Verde usada por padrão pelo Link Adulto.
* **`tunica_goron_vermelha.png`** (128x128): Túnica Goron Vermelha resistente a calor/lava.
* **`tunica_zora_azul.png`** (128x128): Túnica Zora Azul para respiração subaquática.

### 2. `vestimentas_e_acessorios/` (Roupas e Acessórios do Modelo 3D Principal)
* **`roupa_branca_calcas_camisa.png`** (128x128): Calça branca, camisa e meias do Link Adulto.
* **`tunica_verde_base_modelo.png`** (128x128): Textura base da túnica no modelo 3D.
* **`luvas_bracadeiras_couro.png`** (64x32): Luvas e braçadeiras de couro marrom.
* **`cinto_bolsa_couro.png`** (64x64): Cinto e bolsa utilitária de couro.
* **`botas_calcados_couro.png`** (32x64): Botas de couro de cano alto.
* **`detalhes_mangas_tecido.png`** (32x64): Mangas e acabamento de tecido.
* **`fivelas_cinto_detalhes.png`** (32x64): Fivelas e fechos metálicos do cinto.
* **`costuras_bainha_roupa.png`** (32x64): Costuras e bainhas.

### 3. `roupas_menu_pausa/` (Roupas do Link no Menu de Status)
* **`menu_tunica_kokiri_verde.png`** e **`menu_tunica_normal_map.png`**: Textura difusa e mapa de relevo normal da túnica no menu.
* **`menu_roupa_branca.png`** e **`menu_roupa_branca_normal_map.png`**: Textura difusa e normal map da calça e camisa branca.
* **`menu_luvas_couro.png`** e **`menu_luvas_couro_normal_map.png`**: Textura difusa e normal map das luvas.

### 4. `roupas_cutscene_abertura/` (Link na Sequência de Introdução)
* **`abertura_tunica_verde.png`**: Túnica verde do Link cavalgando a Epona.
* **`abertura_roupa_branca.png`**: Calça e camisa branca na introdução.
* **`abertura_luvas_couro.png`**: Luvas de couro na introdução.

---

## 🎨 Como Criar Textura Personalizada da Roupa

1. Abra qualquer uma das texturas (ex.: `tunica_kokiri_verde.png` ou `roupa_branca_calcas_camisa.png`) no seu editor de imagem preferido (Photoshop, GIMP, Krita, Aseprite, etc.).
2. Pinte ou altere o padrão de cores (ex.: túnica preta de Dark Link, túnica dourada, roupas camufladas ou texturas em alta resolução 512x512 ou 1024x1024).
3. Salve a imagem com o mesmo nome ou copie usando o nome canônico do Azahar listado em `tabela_hashes_roupa_link.json`.
4. Coloque a imagem modificada na pasta de texturas do TriAevum no Android (`/Android/data/org.triaevum.android/files/textures/`) ou aponte a pasta no seletor de texturas do app.
"""
    (out_base / "README.md").write_text(readme_text, encoding="utf-8")
    print(f"\n[OK] Concluído com sucesso! Pasta gerada em:\n  {out_base}")


if __name__ == "__main__":
    main()
