# STATUS: Renderização Nativa PICA200 -> Adreno (Vulkan)

Última atualização: 2026-10-04  
Branch ativa: `feature/native-render-phase2-data-pipeline`  
Fase atual: **Fase 2 — Dados: vértices, texturas e superfícies** (Concluída) -> Próxima: **Fase 3 — Shaders de vértice (e geometria)**

---

## 1. Portões de Qualidade (Quality Gates)

| Fase | Título | Critério de Aprovação | Status | Evidência / Notas |
| :---: | :--- | :--- | :---: | :--- |
| **0** | **Referência e instrumentação** | Capturas e frames de referência salvos; versão/commit do Azahar anotada; scripts de portão criados | **CONCLUÍDO** | `tools/gate_check.sh` operacional e integrado aos hooks; frames e oráculo documentados em `docs/PHASE0_REFERENCE_AND_INSTRUMENTATION.md`; `NativeRenderControl` testado e validado (R22). |
| **1** | **Decodificador de estado PICA** | Dump de estado igual à referência em todas as capturas | **CONCLUÍDO** | `PicaCommandProcessor` e `DecodePicaDrawState` implementados; structs canônicas (`VertexState`, `TexState[3]`, `TevState[6]`, etc.) e hash determinístico (`ComputePicaStateHash`) validados em `pica_state_decoder_tests`. |
| **2** | **Dados: vértices, texturas e superfícies** | Texturas idênticas byte a byte; sem vazamento no cache de superfícies | **CONCLUÍDO** | `PicaVertexTranslator`, `PicaTextureCache` (com deswizzle Morton 8x8 e passthrough ETC1->ETC2) e `PicaSurfaceCache` (display transfer e reinterpretação D24S8) validados em `pica_data_pipeline_tests`. |
| **3** | Shaders de vértice (e geometria) | Posições de vértice dentro da tolerância registrada; 0 erros no `spirv-val` | **PRÓXIMA** | Estruturação de fluxo de shaders PICA (JMP/CALL/LOOP), emulação de inf*0=0, UBO 96 vec4 e geração de SPIR-V. |
| **4** | Gerador de fragment shader | Diff por pixel dentro da tolerância; cada função TEV e cada config de lighting testada | PENDENTE | Depende da Fase 3 |
| **5** | Rasterização, profundidade e blending | Cenas de z/alpha/stencil sem regressão | PENDENTE | Depende da Fase 4 |
| **6** | Render passes amigáveis à Adreno | GMEM loads/stores medidos e registrados; sem leitura de RT externo no meio do pass | PENDENTE | Depende da Fase 5 |
| **7** | Pipelines, cache e stutter | Sem hitch acima do limite em teste de 5 min; cache corrompido não causa crash | PENDENTE | Depende da Fase 6 |
| **8** | Rumo ao nativo de verdade (HLE) | Frame idêntico entre o caminho HLE e o nível 2 | PENDENTE | Depende da Fase 7 |
| **9** | Validação contínua | Matriz de aparelhos/drivers executada | PENDENTE | Depende da Fase 8 |
| **10** | Otimização Adreno | Ganho medido contra o baseline, sem regressão de correção | PENDENTE | Depende da Fase 9 |

---

## 2. Acompanhamento da Fase 2

- [x] **2.1** `PicaVertexTranslator`: Mapeamento de formatos de atributos (byte/short/float) para formatos Vulkan (`R32G32B32_Sfloat`, `R16G16_Snorm`, etc.), geração de descrições de binding e suporte a índices u8/u16.
- [x] **2.2** `PicaTextureCache`: Algoritmo de deswizzle Morton 8x8 exato byte-a-byte, decodificador de formatos PICA para RGBA8, passthrough direto de blocos ETC1 para samplers de hardware ETC2 (Adreno) e teto de memória configurável com política de despejo automática (R14).
- [x] **2.3** `PicaSurfaceCache`: Cache de superfícies com teto de memória, display transfer (detiling Morton e rotação 90/180/270 para apresentação) e reinterpretação segura de D24S8 para RGBA8 com fallback seguro contra buffer ausente (R12).
- [x] **2.4** Suíte de testes automatizados [`pica_data_pipeline_tests.cpp`](runtime/three_ds_recomp/tests/pica_data_pipeline_tests.cpp) aprovada com 100% de sucesso.
- [x] **2.5** Registro de decisões técnicas em [`DECISIONS.md`](DECISIONS.md) (ADR-0005, resolução de WARN-03, WARN-04 e WARN-05).

---

## 3. Registro de Fallbacks e Alertas (R26)

- `NATIVE_FALLBACK`: 0 ativos no hot path.
- Orçamento de memória: 128 MB para texturas, 64 MB para superfícies de renderização (R14).
- Proteção contra buffers inexistentes implementada sem abortos (R12).
