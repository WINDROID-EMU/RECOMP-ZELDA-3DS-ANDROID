#!/usr/bin/env python3
"""
Extração e geração de texturas dos itens do menu do Link Adulto e modelos 3D do OoT 3D.
Gera também catálogo completo de hashes Azahar (CityHash64) e pacote de teste customizado.
"""

from __future__ import annotations

import json
import math
import os
import struct
import subprocess
import sys
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent / "oot3d" / "oot3d_asset_tool" / "src"))

from oot3d_asset_tool.zar import ZarArchive
from oot3d_asset_tool.cmb import (
    CmbModel,
    PICA_TEXTURE_RGBA,
    PICA_TEXTURE_RGB,
    PICA_TEXTURE_ALPHA,
    PICA_TEXTURE_LUMINANCE,
    PICA_TEXTURE_LUMINANCE_ALPHA,
    PICA_TEXTURE_ETC1,
    PICA_TEXTURE_ETC1A4,
    PICA_U8,
    PICA_UNSIGNED_BYTE_4_4,
    PICA_UNSIGNED_4BITS,
    PICA_UNSIGNED_SHORT_4444,
    PICA_UNSIGNED_SHORT_5551,
    PICA_UNSIGNED_SHORT_565,
    detile_ctr_texture,
    detile_ctr_texture_4bpp,
    sample_etc1_subtile,
    expand_4_to_8,
    expand_5_to_8,
)
from oot3d_asset_tool.ctxb import parse_ctxb

HASHER_BIN = Path(__file__).resolve().parent / "azahar_hasher"


def compute_azahar_cityhash(data: bytes) -> str:
    """Calcula o CityHash64 canônico usando a ferramenta C++ azahar_hasher."""
    tmp_path = Path("/tmp/azahar_hash_input.bin")
    tmp_path.write_bytes(data)
    res = subprocess.run([str(HASHER_BIN), str(tmp_path), "0", str(len(data))],
                         capture_output=True, text=True, check=True)
    return res.stdout.strip()


def get_native_format_number(tex_fmt: int, data_type: int) -> int:
    """Mapeia os códigos de formato PICA para o número de formato nativo do Azahar."""
    if tex_fmt == PICA_TEXTURE_ETC1:
        return 12
    if tex_fmt == PICA_TEXTURE_ETC1A4:
        return 13
    if tex_fmt == PICA_TEXTURE_RGBA and data_type == PICA_U8:
        return 0
    if tex_fmt == PICA_TEXTURE_RGB and data_type == PICA_U8:
        return 1
    if tex_fmt == PICA_TEXTURE_RGBA and data_type == PICA_UNSIGNED_SHORT_5551:
        return 2
    if tex_fmt == PICA_TEXTURE_RGB and data_type == PICA_UNSIGNED_SHORT_565:
        return 3
    if tex_fmt == PICA_TEXTURE_RGBA and data_type == PICA_UNSIGNED_SHORT_4444:
        return 4
    if tex_fmt == PICA_TEXTURE_LUMINANCE_ALPHA and data_type == PICA_U8:
        return 5
    if tex_fmt == PICA_TEXTURE_LUMINANCE and data_type == PICA_U8:
        return 7
    if tex_fmt == PICA_TEXTURE_ALPHA and data_type == PICA_U8:
        return 8
    if tex_fmt == PICA_TEXTURE_LUMINANCE_ALPHA and data_type == PICA_UNSIGNED_BYTE_4_4:
        return 9
    if tex_fmt == PICA_TEXTURE_LUMINANCE and data_type == PICA_UNSIGNED_4BITS:
        return 10
    return 0


def decode_to_rgba8888(data: bytes, width: int, height: int, tex_fmt: int, data_type: int) -> bytes:
    out = bytearray(width * height * 4)
    if tex_fmt == PICA_TEXTURE_RGBA and data_type == PICA_U8:
        linear = detile_ctr_texture(data, width, height, 4)
        return bytes(linear)
    elif tex_fmt == PICA_TEXTURE_RGB and data_type == PICA_U8:
        linear = detile_ctr_texture(data, width, height, 3)
        for i in range(width * height):
            out[i * 4 : i * 4 + 3] = linear[i * 3 : i * 3 + 3]
            out[i * 4 + 3] = 255
        return bytes(out)
    elif tex_fmt == PICA_TEXTURE_RGB and data_type == PICA_UNSIGNED_SHORT_565:
        linear = detile_ctr_texture(data, width, height, 2)
        for i in range(width * height):
            val = int.from_bytes(linear[i * 2 : i * 2 + 2], "little")
            r = expand_5_to_8((val >> 11) & 0x1F)
            g = ((val >> 5) & 0x3F) * 255 // 63
            b = expand_5_to_8(val & 0x1F)
            out[i * 4 : i * 4 + 4] = bytes((r, g, b, 255))
        return bytes(out)
    elif tex_fmt == PICA_TEXTURE_RGBA and data_type == PICA_UNSIGNED_SHORT_5551:
        linear = detile_ctr_texture(data, width, height, 2)
        for i in range(width * height):
            val = int.from_bytes(linear[i * 2 : i * 2 + 2], "little")
            r = expand_5_to_8((val >> 11) & 0x1F)
            g = expand_5_to_8((val >> 6) & 0x1F)
            b = expand_5_to_8((val >> 1) & 0x1F)
            a = 255 if (val & 1) else 0
            out[i * 4 : i * 4 + 4] = bytes((r, g, b, a))
        return bytes(out)
    elif tex_fmt == PICA_TEXTURE_RGBA and data_type == PICA_UNSIGNED_SHORT_4444:
        linear = detile_ctr_texture(data, width, height, 2)
        for i in range(width * height):
            val = int.from_bytes(linear[i * 2 : i * 2 + 2], "little")
            r = expand_4_to_8((val >> 12) & 0xF)
            g = expand_4_to_8((val >> 8) & 0xF)
            b = expand_4_to_8((val >> 4) & 0xF)
            a = expand_4_to_8(val & 0xF)
            out[i * 4 : i * 4 + 4] = bytes((r, g, b, a))
        return bytes(out)
    elif tex_fmt == PICA_TEXTURE_ALPHA and data_type == PICA_U8:
        linear = detile_ctr_texture(data, width, height, 1)
        for i in range(width * height):
            out[i * 4 : i * 4 + 3] = b"\xff\xff\xff"
            out[i * 4 + 3] = linear[i]
        return bytes(out)
    elif tex_fmt == PICA_TEXTURE_LUMINANCE and data_type == PICA_U8:
        linear = detile_ctr_texture(data, width, height, 1)
        for i in range(width * height):
            v = linear[i]
            out[i * 4 : i * 4 + 4] = bytes((v, v, v, 255))
        return bytes(out)
    elif tex_fmt == PICA_TEXTURE_LUMINANCE and data_type == PICA_UNSIGNED_4BITS:
        linear = detile_ctr_texture_4bpp(data, width, height)
        for i in range(width * height):
            v = linear[i] * 17
            out[i * 4 : i * 4 + 4] = bytes((v, v, v, 255))
        return bytes(out)
    elif tex_fmt == PICA_TEXTURE_LUMINANCE_ALPHA and data_type == PICA_U8:
        linear = detile_ctr_texture(data, width, height, 2)
        for i in range(width * height):
            a, l = linear[i * 2], linear[i * 2 + 1]
            out[i * 4 : i * 4 + 4] = bytes((l, l, l, a))
        return bytes(out)
    elif tex_fmt == PICA_TEXTURE_LUMINANCE_ALPHA and data_type == PICA_UNSIGNED_BYTE_4_4:
        linear = detile_ctr_texture(data, width, height, 1)
        for i in range(width * height):
            val = linear[i]
            l = expand_4_to_8(val >> 4)
            a = expand_4_to_8(val & 0xF)
            out[i * 4 : i * 4 + 4] = bytes((l, l, l, a))
        return bytes(out)
    elif tex_fmt in (PICA_TEXTURE_ETC1, PICA_TEXTURE_ETC1A4):
        has_alpha = (tex_fmt == PICA_TEXTURE_ETC1A4)
        subtile_size = 16 if has_alpha else 8
        tile_size = subtile_size * 4
        tiles_x = max(1, math.ceil(width / 8))
        for y in range(height):
            for x in range(width):
                tile_x, tile_y = x // 8, y // 8
                fine_x, fine_y = x % 8, y % 8
                tile_base = (tile_y * tiles_x + tile_x) * tile_size
                subtile_index = (fine_x // 4) + 2 * (fine_y // 4)
                subtile_base = tile_base + subtile_index * subtile_size
                local_x, local_y = fine_x % 4, fine_y % 4
                alpha = 255
                etc_base = subtile_base
                if has_alpha:
                    packed_alpha = int.from_bytes(data[subtile_base : subtile_base + 8], "little")
                    alpha_nibble = (packed_alpha >> (4 * (local_x * 4 + local_y))) & 0xF
                    alpha = expand_4_to_8(alpha_nibble)
                    etc_base += 8
                raw = int.from_bytes(data[etc_base : etc_base + 8], "little")
                r, g, b = sample_etc1_subtile(raw, local_x, local_y)
                dst = (y * width + x) * 4
                out[dst : dst + 4] = bytes((r, g, b, alpha))
        return bytes(out)
    else:
        raise ValueError(f"Unsupported format {hex(tex_fmt)}, type {hex(data_type)}")


class RomFsReader:
    def __init__(self, rom_path: Path):
        self.stream = rom_path.open("rb")
        header = self._read_exact(0, 0x200)
        part_base = struct.unpack_from("<I", header, 0x120)[0] * 0x200
        ncch_header = self._read_exact(part_base, 0x200)
        romfs_units = struct.unpack_from("<I", ncch_header, 0x1B0)[0]
        self.romfs_base = part_base + romfs_units * 0x200
        self.stream.seek(self.romfs_base)
        prefix = self.stream.read(4)
        self.level3_base = self.romfs_base + (0x1000 if prefix == b"IVFC" else 0)
        self.stream.seek(self.level3_base)
        h = struct.unpack("<10I", self.stream.read(0x28))
        self.dir_meta_off, self.dir_meta_sz = h[3], h[4]
        self.file_meta_off, self.file_meta_sz = h[7], h[8]
        self.file_data_off = h[9]
        self.stream.seek(self.level3_base + self.dir_meta_off)
        self.dir_table = self.stream.read(self.dir_meta_sz)
        self.stream.seek(self.level3_base + self.file_meta_off)
        self.file_table = self.stream.read(self.file_meta_sz)

    def _read_exact(self, offset: int, size: int) -> bytes:
        self.stream.seek(offset)
        return self.stream.read(size)

    def _get_dir(self, offset: int):
        parent, sibling, child_dir, child_file, _, name_len = struct.unpack_from("<6I", self.dir_table, offset)
        name = self.dir_table[offset + 24 : offset + 24 + name_len].decode("utf-16le")
        return parent, sibling, child_dir, child_file, name

    def _get_file(self, offset: int):
        parent, sibling, data_off, data_sz, _, name_len = struct.unpack_from("<IIQQII", self.file_table, offset)
        name = self.file_table[offset + 32 : offset + 32 + name_len].decode("utf-16le")
        return parent, sibling, data_off, data_sz, name

    def read_file_by_path(self, target_path: str) -> bytes | None:
        parts = [p for p in target_path.split("/") if p]
        curr_dir = 0
        for i, part in enumerate(parts):
            parent, sibling, child_dir, child_file, name = self._get_dir(curr_dir)
            if i == len(parts) - 1:
                f = child_file
                while f != 0xFFFFFFFF:
                    _, f_sibling, f_data_off, f_data_sz, f_name = self._get_file(f)
                    if f_name == part:
                        self.stream.seek(self.level3_base + self.file_data_off + f_data_off)
                        return self.stream.read(f_data_sz)
                    f = f_sibling
                return None
            else:
                d = child_dir
                found = False
                while d != 0xFFFFFFFF:
                    _, d_sibling, _, _, d_name = self._get_dir(d)
                    if d_name == part:
                        curr_dir = d
                        found = True
                        break
                    d = d_sibling
                if not found:
                    return None

    def close(self):
        self.stream.close()


def main():
    root_dir = Path(__file__).resolve().parents[1]
    rom_path = Path("/media/windroid/SSD KING/Legend of Zelda, The - Ocarina of Time 3D (USA) (En,Fr,Es).3ds")
    if not rom_path.is_file():
        print(f"Erro: ROM não encontrada em {rom_path}")
        sys.exit(1)

    out_base = root_dir / "texturas_itens_menu_link_adulto"
    sheets_dir = out_base / "sheets_completas_2d"
    indiv_equip_dir = out_base / "itens_individuais_2d" / "equipamentos_adulto"
    indiv_items_dir = out_base / "itens_individuais_2d" / "itens_inventario_adulto"
    models_3d_dir = out_base / "modelos_equipamentos_3d"
    menu_link_dir = out_base / "menu_link_3d"
    test_pack_dir = out_base / "pacote_texturas_personalizadas_teste"

    for d in [sheets_dir, indiv_equip_dir, indiv_items_dir, models_3d_dir, menu_link_dir, test_pack_dir]:
        d.mkdir(parents=True, exist_ok=True)

    print(f"== Extraindo texturas dos itens do menu do Link Adulto ==")
    reader = RomFsReader(rom_path)

    hash_catalog = []
    pack_textures_map = {}

    # 1. Extração das Sheets 2D
    ctxb_files = [
        ("/menu/01_US_ENGLISH/icon_item_menu00.ctxb", "icon_item_menu00", "Grade completa de 64 ícones do inventário 2D"),
        ("/menu/01_US_ENGLISH/menu_equip_parts00.ctxb", "menu_equip_parts00", "Equipamentos equipáveis (Espadas, Escudos, Túnicas, Botas, Luvas)"),
        ("/menu/01_US_ENGLISH/menu_item_parts00.ctxb", "menu_item_parts00", "Molduras e slots do menu de itens"),
        ("/menu/01_US_ENGLISH/menu_item_parts01.ctxb", "menu_item_parts01", "Elementos do cursor e seletores do menu"),
        ("/menu/01_US_ENGLISH/hud_menu_title00.ctxb", "hud_menu_title00", "Títulos do menu"),
        ("/menu/01_US_ENGLISH/menu_cursor00.ctxb", "menu_cursor00", "Cursor indicador"),
    ]

    decoded_sheets = {}

    for romfs_p, name, desc in ctxb_files:
        raw_ctxb = reader.read_file_by_path(romfs_p)
        if not raw_ctxb:
            print(f"Aviso: {romfs_p} não encontrado!")
            continue
        ctxb = parse_ctxb(raw_ctxb, romfs_p)
        payload = raw_ctxb[ctxb.payload_offset : ctxb.payload_offset + ctxb.payload_size]
        fmt_num = get_native_format_number(ctxb.texture_format, ctxb.data_type)
        cityhash = compute_azahar_cityhash(payload)
        azahar_name = f"tex1_{ctxb.width}x{ctxb.height}_{cityhash}_{fmt_num}_mip0.png"

        rgba = decode_to_rgba8888(payload, ctxb.width, ctxb.height, ctxb.texture_format, ctxb.data_type)
        im = Image.frombytes("RGBA", (ctxb.width, ctxb.height), rgba)
        out_png = sheets_dir / f"{name}.png"
        im.save(out_png)
        decoded_sheets[name] = im

        record = {
            "name": name,
            "category": "menu_2d_sheet",
            "romfs_path": romfs_p,
            "description": desc,
            "width": ctxb.width,
            "height": ctxb.height,
            "native_format": fmt_num,
            "cityhash64": cityhash,
            "azahar_filename": azahar_name,
            "png_path": str(out_png.relative_to(out_base)),
        }
        hash_catalog.append(record)
        pack_textures_map[cityhash] = [f"sheets_completas_2d/{name}.png"]
        print(f"  [Sheet 2D] {name} ({ctxb.width}x{ctxb.height}) -> Hash: {cityhash} -> {azahar_name}")

    # 2. Recorte dos Ícones Individuais Transparentes (Link Adulto)
    im_equip = decoded_sheets.get("menu_equip_parts00")
    if im_equip:
        equip_adult_items = [
            ("master_sword", 1, 0, "Espada Master (Link Adulto)"),
            ("biggoron_sword", 2, 0, "Espada Biggoron (Link Adulto)"),
            ("broken_giants_knife", 3, 0, "Faca do Gigante Quebrada (Link Adulto)"),
            ("hylian_shield", 5, 0, "Escudo Hylian (Link Adulto)"),
            ("mirror_shield", 6, 0, "Escudo Espelho (Link Adulto)"),
            ("kokiri_tunic_adult", 0, 1, "Túnica Kokiri Verde"),
            ("goron_tunic_adult", 1, 1, "Túnica Goron Vermelha (Link Adulto)"),
            ("zora_tunic_adult", 2, 1, "Túnica Zora Azul (Link Adulto)"),
            ("kokiri_boots_adult", 3, 1, "Botas Kokiri"),
            ("iron_boots", 4, 1, "Botas de Ferro (Link Adulto)"),
            ("hover_boots", 5, 1, "Botas Flutuantes (Link Adulto)"),
            ("quiver_adult_30", 3, 2, "Aljava de Flechas 30 (Link Adulto)"),
            ("quiver_adult_40", 4, 2, "Aljava de Flechas 40 (Link Adulto)"),
            ("quiver_adult_50", 5, 2, "Aljava de Flechas 50 (Link Adulto)"),
            ("goron_bracelet", 0, 3, "Bracelete de Força Goron"),
            ("silver_gauntlets", 1, 3, "Luvas de Prata (Link Adulto)"),
            ("golden_gauntlets", 2, 3, "Luvas de Ouro (Link Adulto)"),
            ("golden_scale", 4, 3, "Escama de Ouro (Link Adulto)"),
            ("adult_wallet", 5, 3, "Carteira de Adulto (Link Adulto)"),
            ("giants_wallet", 6, 3, "Carteira de Gigante (Link Adulto)"),
        ]
        for item_key, col, row, desc in equip_adult_items:
            tile = im_equip.crop((col * 64, row * 64, (col + 1) * 64, (row + 1) * 64))
            p = indiv_equip_dir / f"{item_key}.png"
            tile.save(p)

    im_icon = decoded_sheets.get("icon_item_menu00")
    if im_icon:
        inv_adult_items = [
            ("fairy_bow", 3, 0, "Arco de Fada (Link Adulto)"),
            ("fire_arrow", 4, 0, "Flechas de Fogo (Link Adulto)"),
            ("dins_fire", 5, 0, "Fogo de Din"),
            ("ocarina_of_time", 0, 1, "Ocarina do Tempo"),
            ("hookshot", 2, 1, "Hookshot (Link Adulto)"),
            ("longshot", 3, 1, "Longshot (Link Adulto)"),
            ("ice_arrow", 4, 1, "Flechas de Gelo (Link Adulto)"),
            ("farores_wind", 5, 1, "Vento de Farore"),
            ("lens_of_truth", 7, 1, "Lente da Verdade"),
            ("megaton_hammer", 1, 2, "Martelo Megaton (Link Adulto)"),
            ("light_arrow", 2, 2, "Flechas de Luz (Link Adulto)"),
            ("nayrus_love", 3, 2, "Amor de Nayru"),
            ("bottle_empty", 4, 2, "Frasco Vazio"),
            ("bottle_red_potion", 5, 2, "Poção Vermelha"),
            ("bottle_green_potion", 6, 2, "Poção Verde"),
            ("bottle_blue_potion", 7, 2, "Poção Azul"),
            ("bottle_fairy", 0, 3, "Fada no Frasco"),
            ("bottle_fish", 1, 3, "Peixe no Frasco"),
            ("bottle_milk_full", 2, 3, "Leite Lon Lon Cheio"),
            ("bottle_blue_fire", 4, 3, "Fogo Azul no Frasco"),
            ("bottle_bug", 5, 3, "Insetos no Frasco"),
            ("bottle_big_poe", 6, 3, "Grande Poe no Frasco (Link Adulto)"),
            ("bottle_milk_half", 7, 3, "Leite Lon Lon Metade"),
            ("bottle_poe", 0, 4, "Poe no Frasco"),
            ("pocket_egg", 5, 5, "Ovo de Bolso (Troca Link Adulto)"),
            ("pocket_cucco", 6, 5, "Galinha de Bolso (Troca Link Adulto)"),
            ("cojiro", 7, 5, "Cojiro Galinha Azul (Troca Link Adulto)"),
            ("odd_mushroom", 0, 6, "Cogumelo Estranho (Troca Link Adulto)"),
            ("odd_potion", 1, 6, "Poção Estranha (Troca Link Adulto)"),
            ("poachers_saw", 2, 6, "Serra do Caçador (Troca Link Adulto)"),
            ("broken_goron_sword", 3, 6, "Espada Goron Quebrada (Troca Link Adulto)"),
            ("prescription", 4, 6, "Receita Médica (Troca Link Adulto)"),
            ("eyeball_frog", 5, 6, "Sapo Olho de Boi (Troca Link Adulto)"),
            ("eye_drops", 6, 6, "Colírio do Biggoron (Troca Link Adulto)"),
            ("claim_check", 7, 6, "Comprovante da Biggoron (Troca Link Adulto)"),
            ("bow_fire_arrow_combo", 0, 7, "Combo Arco + Flechas de Fogo"),
            ("bow_ice_arrow_combo", 1, 7, "Combo Arco + Flechas de Gelo"),
            ("bow_light_arrow_combo", 2, 7, "Combo Arco + Flechas de Luz"),
        ]
        for item_key, col, row, desc in inv_adult_items:
            tile = im_icon.crop((col * 64, row * 64, (col + 1) * 64, (row + 1) * 64))
            p = indiv_items_dir / f"{item_key}.png"
            tile.save(p)

    print(f"  [Ícones 2D] Extraídos {len(equip_adult_items)} equipamentos e {len(inv_adult_items)} itens de inventário em PNGs individuais.")

    # 3. Modelos 3D de Equipamentos de Link Adulto (zelda_link_boy_ultra.zar)
    print("\n[+] Extraindo modelos 3D de equipamentos do Link Adulto (zelda_link_boy_ultra.zar)...")
    ultra_data = reader.read_file_by_path("/actor/zelda_link_boy_ultra.zar")
    if ultra_data:
        zar_ultra = ZarArchive.parse(ultra_data, "zelda_link_boy_ultra.zar")
        for zf in zar_ultra.files:
            if zf.name.endswith(".cmb"):
                stem = Path(zf.name).stem
                cmb_data = zar_ultra.read_file(zf)
                try:
                    cmb = CmbModel.parse(cmb_data, zf.name)
                except Exception as e:
                    continue
                if not cmb.textures:
                    continue
                item_folder = models_3d_dir / stem
                item_folder.mkdir(parents=True, exist_ok=True)
                for t in cmb.textures:
                    try:
                        cityhash = compute_azahar_cityhash(t.data)
                        fmt_num = get_native_format_number(t.texture_format, t.data_type)
                        azahar_name = f"tex1_{t.width}x{t.height}_{cityhash}_{fmt_num}_mip0.png"
                        rgba = decode_to_rgba8888(t.data, t.width, t.height, t.texture_format, t.data_type)
                        im = Image.frombytes("RGBA", (t.width, t.height), rgba)
                        out_png = item_folder / f"{t.name}.png"
                        im.save(out_png)

                        hash_catalog.append({
                            "name": f"{stem}/{t.name}",
                            "category": "equipamento_3d_adulto",
                            "model": stem,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_textures_map[cityhash] = [f"modelos_equipamentos_3d/{stem}/{t.name}.png"]
                    except Exception as e:
                        print(f"    Erro ao exportar {stem} {t.name}: {e}")

    # 4. Modelo 3D de Link do Menu de Pausa (menu_link.zar)
    print("\n[+] Extraindo modelo 3D de Link do Menu (menu_link.zar)...")
    menu_link_data = reader.read_file_by_path("/misc/menu_link.zar")
    if menu_link_data:
        zar_menu = ZarArchive.parse(menu_link_data, "menu_link.zar")
        for zf in zar_menu.files:
            if zf.name.endswith(".cmb"):
                stem = Path(zf.name).stem
                cmb_data = zar_menu.read_file(zf)
                try:
                    cmb = CmbModel.parse(cmb_data, zf.name)
                except Exception:
                    continue
                model_folder = menu_link_dir / stem
                model_folder.mkdir(parents=True, exist_ok=True)
                for t in cmb.textures:
                    try:
                        cityhash = compute_azahar_cityhash(t.data)
                        fmt_num = get_native_format_number(t.texture_format, t.data_type)
                        azahar_name = f"tex1_{t.width}x{t.height}_{cityhash}_{fmt_num}_mip0.png"
                        rgba = decode_to_rgba8888(t.data, t.width, t.height, t.texture_format, t.data_type)
                        im = Image.frombytes("RGBA", (t.width, t.height), rgba)
                        out_png = model_folder / f"{t.name}.png"
                        im.save(out_png)

                        hash_catalog.append({
                            "name": f"{stem}/{t.name}",
                            "category": "menu_link_3d",
                            "model": stem,
                            "width": t.width,
                            "height": t.height,
                            "native_format": fmt_num,
                            "cityhash64": cityhash,
                            "azahar_filename": azahar_name,
                            "png_path": str(out_png.relative_to(out_base)),
                        })
                        pack_textures_map[cityhash] = [f"menu_link_3d/{stem}/{t.name}.png"]
                    except Exception as e:
                        print(f"    Erro ao exportar {stem} {t.name}: {e}")

    reader.close()

    # 5. Salva o Catálogo JSON Completo
    cat_file = out_base / "tabela_hashes_azahar.json"
    cat_file.write_text(json.dumps(hash_catalog, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n[OK] Tabela de hashes Azahar salva em {cat_file} com {len(hash_catalog)} texturas catalogadas.")

    # 6. Criação do Pacote de Texturas Personalizadas de Teste
    print("\n[+] Gerando pacote de texturas personalizadas para teste...")
    test_pack_textures = {}
    
    # Criamos uma versão HD / modificada especial do sheet de itens e equipamentos
    # Adicionando uma marca d'água de teste e saturação dourada nos itens da Master Sword e Escudo Hylian
    if im_equip:
        custom_equip = im_equip.copy()
        # Customização no ícone da Master Sword (col 1, row 0): dar um brilho dourado místico
        ms_tile = custom_equip.crop((64, 0, 128, 64)).convert("RGBA")
        pix = ms_tile.load()
        for y in range(64):
            for x in range(64):
                r, g, b, a = pix[x, y]
                if a > 50:
                    # Tonalidade dourada vibrante para teste visual evidente
                    pix[x, y] = (min(255, int(r * 1.5 + 40)), min(255, int(g * 1.3 + 30)), int(b * 0.4), a)
        custom_equip.paste(ms_tile, (64, 0))
        
        # Salva o arquivo de teste no pacote com nome descritivo e nome canônico Azahar
        custom_equip_path = test_pack_dir / "menu_equip_parts00_custom.png"
        custom_equip.save(custom_equip_path)
        
        equip_record = next(r for r in hash_catalog if r["name"] == "menu_equip_parts00")
        test_pack_textures[equip_record["cityhash64"]] = ["menu_equip_parts00_custom.png"]
        
        # Também gera o arquivo avulso no formato tex1_...
        loose_name = test_pack_dir / equip_record["azahar_filename"]
        custom_equip.save(loose_name)
        print(f"  [Teste] Criado {custom_equip_path.name} e {loose_name.name}")

    if im_icon:
        custom_icon = im_icon.copy()
        # Customização no ícone do Arco de Fada (col 3, row 0): brilho especial azul celestial
        bow_tile = custom_icon.crop((3 * 64, 0, 4 * 64, 64)).convert("RGBA")
        pix = bow_tile.load()
        for y in range(64):
            for x in range(64):
                r, g, b, a = pix[x, y]
                if a > 50:
                    pix[x, y] = (int(r * 0.3), min(255, int(g * 1.2 + 20)), min(255, int(b * 1.6 + 50)), a)
        custom_icon.paste(bow_tile, (3 * 64, 0))
        
        custom_icon_path = test_pack_dir / "icon_item_menu00_custom.png"
        custom_icon.save(custom_icon_path)
        
        icon_record = next(r for r in hash_catalog if r["name"] == "icon_item_menu00")
        test_pack_textures[icon_record["cityhash64"]] = ["icon_item_menu00_custom.png"]
        
        loose_icon = test_pack_dir / icon_record["azahar_filename"]
        custom_icon.save(loose_icon)
        print(f"  [Teste] Criado {custom_icon_path.name} e {loose_icon.name}")

    # Gera o pack.json
    pack_manifest = {
        "options": {
            "skip_mipmap": True,
            "flip_png_files": True,
            "use_new_hash": True
        },
        "textures": test_pack_textures
    }
    pack_json_file = test_pack_dir / "pack.json"
    pack_json_file.write_text(json.dumps(pack_manifest, indent=2), encoding="utf-8")
    print(f"  [Teste] pack.json gerado com sucesso em {pack_json_file}")

    # 7. README.md explicativo
    readme_content = f"""# Texturas dos Itens do Menu do Link Adulto e Teste de Texturas Personalizadas
The Legend of Zelda: Ocarina of Time 3D (TriAevum Recomp)

Este diretório contém todas as texturas extraídas dos itens de menu, inventário e modelos 3D de equipamentos do Link Adulto, decodificadas com precisão RGBA 32-bit e catalogadas com seus respectivos hashes Azahar CityHash64.

---

## 📁 Estrutura de Diretórios

1. **`sheets_completas_2d/`**:
   - `icon_item_menu00.png` (512x512): Atlas contendo os 64 itens do inventário de OoT3D.
   - `menu_equip_parts00.png` (512x256): Tela de equipamentos (espadas, escudos, túnicas, botas, aljavas, carteiras, luvas).
   - `menu_item_parts00.png` e `menu_item_parts01.png`: Molduras e seleção do menu.
   - `hud_menu_title00.png` e `menu_cursor00.png`: Títulos e cursores.

2. **`itens_individuais_2d/`**:
   - **`equipamentos_adulto/`**: Recortes individuais de 64x64 com fundo transparente de cada equipamento do Link Adulto (Espada Master, Espada Biggoron, Escudo Hylian, Escudo Espelho, Túnicas Goron e Zora, Botas de Ferro e Hover, Luvas de Ouro e Prata, Carteiras de Adulto e Gigante, Escama de Ouro).
   - **`itens_inventario_adulto/`**: Recortes individuais de 64x64 de todos os itens utilizáveis por Link Adulto (Arco, Flechas elementais, Hookshot, Longshot, Martelo Megaton, Lente da Verdade, Frascos, Poe, Grande Poe, Itens da Troca Adulta).

3. **`modelos_equipamentos_3d/`**:
   - Texturas originais dos modelos 3D de equipamentos de `zelda_link_boy_ultra.zar` (Espadas, Escudos, Bainha, Martelo, Arco, Botas de Ferro e Hover, Luvas).

4. **`menu_link_3d/`**:
   - Texturas do modelo 3D de Link exibido na tela de status e menu de pausa (`menu_link_omote` e `menu_link_ura`).

5. **`pacote_texturas_personalizadas_teste/`**:
   - Pacote pronto para teste do sistema de texturas personalizadas!
   - Inclui `pack.json` devidamente mapeado com opções `use_new_hash: true`.
   - Inclui versões customizadas de teste da Espada Master (dourada) e Arco de Fada (azul celestial) no `menu_equip_parts00` e `icon_item_menu00`.
   - Inclui arquivos nomeados no padrão avulso Azahar:
     - `{equip_record["azahar_filename"]}`
     - `{icon_record["azahar_filename"]}`

6. **`tabela_hashes_azahar.json`**:
   - Tabela JSON contendo as dimensões, formato nativo PICA e hash CityHash64 exato para cada textura.

---

## 🚀 Como Testar no TriAevum

### No Android:
1. Abra o TriAevum no aparelho.
2. Acesse **Configurações > Gráficos > Pacotes de Texturas (Azahar)**.
3. Clique em **"Escolher Pasta"** e aponte para a pasta `pacote_texturas_personalizadas_teste` (ou coloque os arquivos em `/Android/data/org.triaevum.android/files/textures/`).
4. Ative a opção **"Carregar Texturas Personalizadas"**.
5. Ao abrir o menu de pausa no jogo, a Espada Master e os equipamentos aparecerão instantaneamente com a textura modificada em alta qualidade!
"""
    (out_base / "README.md").write_text(readme_content, encoding="utf-8")
    print(f"\n[OK] Concluído com sucesso! Todo o material salvo em:\n  {out_base}")


if __name__ == "__main__":
    main()
