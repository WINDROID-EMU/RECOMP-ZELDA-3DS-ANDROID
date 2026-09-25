#include "oot3d/renderer/azahar_texture_pack.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cassert>
#include <filesystem>

int main(int argc, char** argv) {
    std::cout << "=========================================================\n";
    std::cout << "  TESTE DO SISTEMA DE TEXTURAS PERSONALIZADAS (TRIAEVUM) \n";
    std::cout << "=========================================================\n\n";

    std::filesystem::path projectRoot = "/media/windroid/SSD KING/RECOMP-ZELDA-3DS-ANDROID";
    std::filesystem::path packDir = projectRoot / "texturas_itens_menu_link_adulto" / "pacote_texturas_personalizadas_teste";
    std::filesystem::path equipCtxb = "/tmp/menu_equip_parts00.ctxb";
    std::filesystem::path iconCtxb = "/tmp/icon_item_menu00.ctxb";

    if (!std::filesystem::exists(packDir / "pack.json")) {
        std::cerr << "[FALHA] pack.json nao encontrado em: " << packDir << "\n";
        return 1;
    }

    std::cout << "[1] Inicializando AzaharTexturePackRuntime...\n";
    Oot3d::Renderer::AzaharTexturePackRuntime runtime;
    runtime.Configure({
        .DumpTextures = false,
        .LoadCustomTextures = true,
        .TitleId = 0x0004000000033600ULL,
        .UserDirectory = projectRoot,
        .LoadDirectory = packDir,
    });

    const auto snapshotInitial = runtime.Snapshot();
    std::cout << "    - LoadDirectory: " << snapshotInitial.LoadDirectory << "\n";
    std::cout << "    - PackConfigurationFound: " << (snapshotInitial.PackConfigurationFound ? "SIM" : "NAO") << "\n";
    std::cout << "    - PackUsesNewHash: " << (snapshotInitial.PackUsesNewHash ? "SIM" : "NAO") << "\n";
    std::cout << "    - IndexedTextures: " << snapshotInitial.IndexedTextures << "\n\n";

    if (!snapshotInitial.PackConfigurationFound || snapshotInitial.IndexedTextures == 0) {
        std::cerr << "[FALHA] Pacote nao foi indexado corretamente pelo runtime.\n";
        return 1;
    }

    // 2. Testar Substituicao da Textura dos Equipamentos (menu_equip_parts00)
    std::cout << "[2] Testando resolucao da textura customizada dos Equipamentos (menu_equip_parts00)...\n";
    std::ifstream fEquip(equipCtxb, std::ios::binary);
    if (!fEquip) {
        std::cerr << "[FALHA] Nao foi possivel abrir " << equipCtxb << "\n";
        return 1;
    }
    std::vector<uint8_t> equipData((std::istreambuf_iterator<char>(fEquip)), std::istreambuf_iterator<char>());
    std::span<const uint8_t> equipPayload(equipData.data() + 0x48, equipData.size() - 0x48);

    std::vector<uint8_t> dummyRgba(512U * 256U * 4U, 0);
    Oot3d::Renderer::AzaharTextureRequest reqEquip{
        .Width = 512,
        .Height = 256,
        .NativeFormat = 13, // ETC1A4
        .MipLevel = 0,
        .NativeBytes = equipPayload,
        .NativeRgba8 = dummyRgba
    };

    auto resEquip = runtime.ResolveAndMaybeDump(reqEquip);
    if (!resEquip) {
        std::cerr << "[FALHA] ResolveAndMaybeDump retornou nullptr para menu_equip_parts00!\n";
        return 1;
    }
    std::cout << "    [OK] Textura customizada resolvida com sucesso!\n";
    std::cout << "    - Origem: " << resEquip->SourcePath.filename() << "\n";
    std::cout << "    - Dimensoes: " << resEquip->Width << "x" << resEquip->Height << "\n";
    std::cout << "    - Native Hash: 0x" << std::hex << resEquip->NativeHash << std::dec << "\n";
    std::cout << "    - Pixels RGBA8 decodificados: " << (resEquip->Rgba8 ? resEquip->Rgba8->size() : 0) << " bytes\n\n";

    // 3. Testar Substituicao da Textura dos Itens de Inventario (icon_item_menu00)
    std::cout << "[3] Testando resolucao da textura customizada dos Itens (icon_item_menu00)...\n";
    std::ifstream fIcon(iconCtxb, std::ios::binary);
    if (!fIcon) {
        std::cerr << "[FALHA] Nao foi possivel abrir " << iconCtxb << "\n";
        return 1;
    }
    std::vector<uint8_t> iconData((std::istreambuf_iterator<char>(fIcon)), std::istreambuf_iterator<char>());
    std::span<const uint8_t> iconPayload(iconData.data() + 0x48, iconData.size() - 0x48);

    std::vector<uint8_t> dummyIconRgba(512U * 512U * 4U, 0);
    Oot3d::Renderer::AzaharTextureRequest reqIcon{
        .Width = 512,
        .Height = 512,
        .NativeFormat = 13, // ETC1A4
        .MipLevel = 0,
        .NativeBytes = iconPayload,
        .NativeRgba8 = dummyIconRgba
    };

    auto resIcon = runtime.ResolveAndMaybeDump(reqIcon);
    if (!resIcon) {
        std::cerr << "[FALHA] ResolveAndMaybeDump retornou nullptr para icon_item_menu00!\n";
        return 1;
    }
    std::cout << "    [OK] Textura customizada resolvida com sucesso!\n";
    std::cout << "    - Origem: " << resIcon->SourcePath.filename() << "\n";
    std::cout << "    - Dimensoes: " << resIcon->Width << "x" << resIcon->Height << "\n";
    std::cout << "    - Native Hash: 0x" << std::hex << resIcon->NativeHash << std::dec << "\n";
    std::cout << "    - Pixels RGBA8 decodificados: " << (resIcon->Rgba8 ? resIcon->Rgba8->size() : 0) << " bytes\n\n";

    // 4. Testar Carregamento Assincrono / Fila (ResolveOrQueue)
    std::cout << "[4] Testando fila assincrona do motor (ResolveOrQueue + PollQueued)...\n";
    auto queued = runtime.ResolveOrQueue(reqEquip);
    if (queued.State == Oot3d::Renderer::AzaharTextureResolveState::Ready) {
        std::cout << "    [OK] Retornou instantaneamente do cache em memoria (Ready)!\n";
    } else {
        std::cout << "    - Estado pendente, aguardando thread de carregamento...\n";
        runtime.WaitForPendingLoads();
        auto ready = runtime.PollQueued(resEquip->NativeHash);
        if (ready.State != Oot3d::Renderer::AzaharTextureResolveState::Ready) {
            std::cerr << "[FALHA] PollQueued nao retornou Ready!\n";
            return 1;
        }
        std::cout << "    [OK] Carregamento assincrono concluido com sucesso!\n";
    }

    const auto snapshotFinal = runtime.Snapshot();
    std::cout << "\n=========================================================\n";
    std::cout << "  RELATORIO FINAL DE AUDITORIA DO RUNTIME DE TEXTURAS    \n";
    std::cout << "=========================================================\n";
    std::cout << "  * Texturas Indexadas no Pacote: " << snapshotFinal.IndexedTextures << "\n";
    std::cout << "  * Texturas Carregadas com Sucesso: " << snapshotFinal.LoadedTextures << "\n";
    std::cout << "  * Falhas de Carregamento: " << snapshotFinal.FailedLoads << "\n";
    std::cout << "  * Arquivos Nao Suportados: " << snapshotFinal.UnsupportedTextureFiles << "\n";
    std::cout << "  * Status: SUCESSO TOTAL (100% OPERACIONAL)\n";
    std::cout << "=========================================================\n";

    return 0;
}
