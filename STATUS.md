# STATUS: Renderização Nativa PICA200 -> Adreno (Vulkan)

Última atualização: 2026-10-04  
Branch ativa: `feature/native-render-phase1-state-decoder`  
Fase atual: **Fase 1 — Decodificador de estado PICA** (Concluída) -> Próxima: **Fase 2 — Dados: vértices, texturas e superfícies**

---

## 1. Portões de Qualidade (Quality Gates)

| Fase | Título | Critério de Aprovação | Status | Evidência / Notas |
| :---: | :--- | :--- | :---: | :--- |
| **0** | **Referência e instrumentação** | Capturas e frames de referência salvos; versão/commit do Azahar anotada; scripts de portão criados | **CONCLUÍDO** | `tools/gate_check.sh` operacional e integrado aos hooks; frames e oráculo documentados em `docs/PHASE0_REFERENCE_AND_INSTRUMENTATION.md`; `NativeRenderControl` testado e validado (R22). |
| **1** | **Decodificador de estado PICA** | Dump de estado igual à referência em todas as capturas | **CONCLUÍDO** | `PicaCommandProcessor` e `DecodePicaDrawState` implementados; structs canônicas (`VertexState`, `TexState[3]`, `TevState[6]`, `LightingState`, etc.) e hash determinístico de estado (`ComputePicaStateHash`) validados em `pica_state_decoder_tests`. |
| **2** | Dados: vértices, texturas e superfícies | Texturas idênticas byte a byte; sem vazamento no cache de superfícies | **PRÓXIMA** | Deswizzle Morton 8x8, decodificação de texturas, ETC1->ETC2 e cache de superfícies de render. |
| **3** | Shaders de vértice (e geometria) | Posições de vértice dentro da tolerância registrada; 0 erros no `spirv-val` | PENDENTE | Depende da Fase 2 |
| **4** | Gerador de fragment shader | Diff por pixel dentro da tolerância; cada função TEV e cada config de lighting testada | PENDENTE | Depende da Fase 3 |
| **5** | Rasterização, profundidade e blending | Cenas de z/alpha/stencil sem regressão | PENDENTE | Depende da Fase 4 |
| **6** | Render passes amigáveis à Adreno | GMEM loads/stores medidos e registrados; sem leitura de RT externo no meio do pass | PENDENTE | Depende da Fase 5 |
| **7** | Pipelines, cache e stutter | Sem hitch acima do limite em teste de 5 min; cache corrompido não causa crash | PENDENTE | Depende da Fase 6 |
| **8** | Rumo ao nativo de verdade (HLE) | Frame idêntico entre o caminho HLE e o nível 2 | PENDENTE | Depende da Fase 7 |
| **9** | Validação contínua | Matriz de aparelhos/drivers executada | PENDENTE | Depende da Fase 8 |
| **10** | Otimização Adreno | Ganho medido contra o baseline, sem regressão de correção | PENDENTE | Depende da Fase 9 |

---

## 2. Acompanhamento da Fase 1

- [x] **1.1** Implementação do parser da lista de comandos PICA ([`PicaCommandHeader`](runtime/three_ds_recomp/include/three_ds_recomp/pica/pica_command_processor.h), [`PicaCommandProcessor`](runtime/three_ds_recomp/src/pica/pica_command_processor.cpp)) com suporte a máscaras de bytes parciais, escrita consecutiva e não-consecutiva (FIFO).
- [x] **1.2** Modelagem das structs canônicas de estado em [`pica_state.h`](runtime/three_ds_recomp/include/three_ds_recomp/pica/pica_state.h): `VertexState`, `TexState[3]`, `TevState[6]`, `LightingState`, `FogState`, `FramebufferState`, `DepthStencilState`, `BlendState`.
- [x] **1.3** Implementação do decodificador de estado ([`DecodePicaDrawState`](runtime/three_ds_recomp/src/pica/pica_state_decoder.cpp)), gerador de hash determinístico ([`ComputePicaStateHash`](runtime/three_ds_recomp/src/pica/pica_state_decoder.cpp)) e formatador legível de dump ([`DumpReadablePicaState`](runtime/three_ds_recomp/src/pica/pica_state_decoder.cpp)).
- [x] **1.4** Suíte de testes automatizados [`pica_state_decoder_tests.cpp`](runtime/three_ds_recomp/tests/pica_state_decoder_tests.cpp) aprovada com 100% de sucesso.
- [x] **1.5** Registro de decisões técnicas em [`DECISIONS.md`](DECISIONS.md) (ADR-0004).

---

## 3. Registro de Fallbacks e Alertas (R26)

- `NATIVE_FALLBACK`: 0 ativos no hot path.
- Tratamento de estados e registradores desconhecidos via fallback sem crash ([R12](regras%20agente.md#L43)).
