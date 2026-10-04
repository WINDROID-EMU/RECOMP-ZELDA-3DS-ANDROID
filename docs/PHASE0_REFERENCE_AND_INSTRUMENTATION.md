# Fase 0 — Referência, Oráculo e Instrumentação

Documento formal de encerramento dos requisitos da **Fase 0** do guia técnico [regras agente.md](file:///media/windroid/SSD%20KING/RECOMP-ZELDA-3DS-ANDROID/regras%20agente.md).

---

## 1. Oráculo de Correção e Versão do Azahar

### 1.1 Especificação do Oráculo Azahar
- **Base / Fork de Referência:** Azahar CTR / Citra fork instrumentado para conformance PICA200.
- **Commit / Versão Anotada:**
  - Decomp / ABI CTR de referência: `e0b6c5f7d19488cf8a9333288901d8bd22ce5a83`
  - Ferramental de cobertura e extração Azahar: `8d93699`
  - Build de referência Windows / x64: `azahar-oot3d-coverage-build` (Vulkan backend)
  - Módulo de decompilação PICA shader de Azahar/nihstro isolado em submodule GPL/BSD com atribuição.
- **Papel do Oráculo:**
  - Servir exclusivamente como ground-truth para:
    1. Semântica exata dos registradores de hardware da PICA200 (`0x000` - `0x2FF`).
    2. Comportamento aritmético da pipeline fixa (combiners TEV, LUTs de fragment lighting, depth clipping).
    3. Geração de *golden frames* e dumps de stream de comandos PICA para comparação pixel a pixel e byte a byte.
  - Conforme regra **R5**, ROMs, chaves e assets comerciais protegidos permanecem fora do versionamento do git.

---

## 2. Capturas e Cenas de Referência

O repositório e o ferramental de diagnóstico contam com os seguintes catálogos e suites de referência já indexados:

| Categoria de Cena | Identificador / Catálogo | frames / checkpoints | Propósito na Validação |
| :--- | :--- | :---: | :--- |
| **Menu / Boot** | `boot_title` (`whole_aot_product_golden.json`) | 901 frames (checkpoints a cada 100f) | Validação de inicialização, 2D ortho, alpha test e blend em UI |
| **Cutscene** | `kokiri_cutscene` (`whole_aot_product_golden.json`) | 481 frames (checkpoints a cada 60f) | Transformações de câmera, skinning rígido/suave, iluminação dinâmica |
| **Campo Aberto** | `kokiri_gameplay` / `scene_spot04` | Checkpoints de estado de 75 MB | Renderização de terreno, folhagem, alpha test de grama e fog |
| **Interior** | `scene_ydan` (Inside Deku Tree) / `oot3d_azahar_coverage_scenarios.json` | Scenarios 0010 - 0035 | Oclusão, iluminação de pontos/direcional, profundidade complexa |
| **Efeitos / Partículas** | `Oot3dNativeEffectSs` & `kankyo` | Dumps de comandos dedicados | Renderização de overlays, quads 28C, texturas animadas |
| **Cross-Title PICA** | SM3DL `azahar-boot-smoke` / `azahar-idle-30s` | 67 command lists, 169 draws | Prova de conformidade de primitivas e registradores independente de OoT3D |

### 2.1 Formato de Dump da Lista de Comandos PICA
O stream de captura PICA (`.jsonl` de conformidade estruturado) é processado por `Fast::Renderer3ds::Diagnostics::AnalyzePicaCaptureStream`:
- `capture_begin` / `capture_end`
- `command_list_begin` / `command_list_end`
- `register_write` (registrador `id`, valor `value`)
- `draw_begin` (modo `arrays`/`elements`, topologia de primitivas, texturas ativas, hashes de identidade de shader `vp`/`vs`/`fp`)
- `frame_boundary`

---

## 3. Instrumentação e Profilers (Adreno / Snapdragon)

### 3.1 Snapdragon Profiler & Snapdragon Developer Tools
- **Alvo:** Samsung Galaxy S25 (SM-S931B), Android 16, Qualcomm Adreno 830.
- **Métricas Chave de Tile:**
  - `% GMEM Loads / Stores`: Deve tender a 0 em passes intermediários de profundidade (`LOAD_OP_CLEAR/DONT_CARE` e `STORE_OP_DONT_CARE`).
  - `Clocks / Vertex` e `Clocks / Fragment`: Avaliar impacto da eliminação de `gl_FragDepth` e early-Z (LRZ).
  - `ALU / Instruction Issue`: Garantir que combiners TEV traduzidos mantenham baixa ocupação de registradores vetoriais.

### 3.2 RenderDoc
- Captura de frames via Vulkan Layer configurada na inicialização do runtime.
- Inspeção de pipelines gerados, descritores de uniforms (UBO 96 vec4) e samplers de LUT.

---

## 4. Portão de Segurança e Governança

- **Script de Validação:** [`tools/gate_check.sh`](file:///media/windroid/SSD%20KING/RECOMP-ZELDA-3DS-ANDROID/tools/gate_check.sh)
- **Hooks Automatizados:**
  - `.git/hooks/pre-commit` -> Executa checagem de branch (R1), ausência de ROMs/keys (R5), ausência de `gl_FragDepth` não documentado (R17) e presença de `STATUS.md` e `DECISIONS.md` (R20).
  - `.git/hooks/pre-push` -> Bloqueia `push --force` na branch principal (R4).
- **Controle de Chaveamento:** [`NativeRenderControl`](file:///media/windroid/SSD%20KING/RECOMP-ZELDA-3DS-ANDROID/runtime/three_ds_recomp/include/three_ds_recomp/native_render_control.h) permitindo ativar/desativar `native_render` via código e variável de ambiente (`OOT3D_NATIVE_RENDER=0/1`) conforme regra **R22**.
