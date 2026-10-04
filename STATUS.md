# STATUS: Renderização Nativa PICA200 -> Adreno (Vulkan)

Última atualização: 2026-10-04  
Branch ativa: `feature/native-render-phase3-vertex-shaders`  
Fase atual: **Fase 3 — Shaders de vértice (e geometria)** (Concluída) -> Próxima: **Fase 4 — Gerador de fragment shader**

---

## 1. Portões de Qualidade (Quality Gates)

| Fase | Título | Critério de Aprovação | Status | Evidência / Notas |
| :---: | :--- | :--- | :---: | :--- |
| **0** | **Referência e instrumentação** | Capturas e frames de referência salvos; versão/commit do Azahar anotada; scripts de portão criados | **CONCLUÍDO** | `tools/gate_check.sh` operacional e integrado aos hooks; frames e oráculo documentados em `docs/PHASE0_REFERENCE_AND_INSTRUMENTATION.md`; `NativeRenderControl` testado e validado (R22). |
| **1** | **Decodificador de estado PICA** | Dump de estado igual à referência em todas as capturas | **CONCLUÍDO** | `PicaCommandProcessor` e `DecodePicaDrawState` implementados; structs canônicas (`VertexState`, `TexState[3]`, `TevState[6]`, etc.) e hash determinístico (`ComputePicaStateHash`) validados em `pica_state_decoder_tests`. |
| **2** | **Dados: vértices, texturas e superfícies** | Texturas idênticas byte a byte; sem vazamento no cache de superfícies | **CONCLUÍDO** | `PicaVertexTranslator`, `PicaTextureCache` (com deswizzle Morton 8x8 e passthrough ETC1->ETC2) e `PicaSurfaceCache` (display transfer e reinterpretação D24S8) validados em `pica_data_pipeline_tests`. |
| **3** | **Shaders de vértice (e geometria)** | Posições de vértice dentro da tolerância registrada; 0 erros no `spirv-val` | **CONCLUÍDO** | `PicaShaderTranslator` (decodificador de bytecode, reestruturação de fluxo JMP/CALL/LOOP, sanitize_mul, std140 UBO 1616B e mapeamento de profundidade em vertex stage sem `gl_FragDepth`) e `PicaShaderCorpus` validados com 100% de sucesso e 0 erros no `spirv-val`. |
| **4** | Gerador de fragment shader | Diff por pixel dentro da tolerância; cada função TEV e cada config de lighting testada | **PRÓXIMA** | Geração a partir do estado fixo: TEV de 6 estágios, combiner buffer, LUTs de iluminação, fog, alpha test e specialization constants. |
| **5** | Rasterização, profundidade e blending | Cenas de z/alpha/stencil sem regressão | PENDENTE | Depende da Fase 4 |
| **6** | Render passes amigáveis à Adreno | GMEM loads/stores medidos e registrados; sem leitura de RT externo no meio do pass | PENDENTE | Depende da Fase 5 |
| **7** | Pipelines, cache e stutter | Sem hitch acima do limite em teste de 5 min; cache corrompido não causa crash | PENDENTE | Depende da Fase 6 |
| **8** | Rumo ao nativo de verdade (HLE) | Frame idêntico entre o caminho HLE e o nível 2 | PENDENTE | Depende da Fase 7 |
| **9** | Validação contínua | Matriz de aparelhos/drivers executada | PENDENTE | Depende da Fase 8 |
| **10** | Otimização Adreno | Ganho medido contra o baseline, sem regressão de correção | PENDENTE | Depende da Fase 9 |

---

## 2. Acompanhamento da Fase 3

- [x] **3.1** `PicaShaderTranslator`: Decodificação completa de instruções 32-bit PICA (Formatos 0-5: ADD, MUL, DP3, DP4, MAD, CALL, IF, LOOP, END, MOVA) com suporte à inversão de operandos (7-bit vs 5-bit).
- [x] **3.2** Estruturação de fluxo de controle: Sub-rotinas e laços transformados em código estruturado sem saltos arbitrários em GLSL `#version 450`.
- [x] **3.3** Emulação aritmética: `sanitize_mul` reproduz o comportamento não-IEEE da PICA200 (`inf * 0 = 0`), prevenindo NaNs espúrios em transformações de vértice.
- [x] **3.4** UBO std140 alinhado em 1616 bytes (`PicaVertexUniformState`) com 96 `vec4` floats, 4 `uvec4` inteiros, máscara de booleano e flip de viewport.
- [x] **3.5** Mapeamento de profundidade NDC `[-1, 0]` para Vulkan `[0, 1]` aplicado diretamente no Vertex Shader (`z' = -pica_position.z`), garantindo total compatibilidade com o LRZ da Adreno sem emitir `gl_FragDepth` (R17, ADR-0003).
- [x] **3.6** Validação de bytecode SPIR-V: Compilação via `glslangValidator` e validação com `spirv-val` com **0 erros de validação** (inclusive no shader extraído do pipeline Kokiri/boot de OoT3D).
- [x] **3.7** Paridade numérica: Transformações de vértice avaliadas contra o oráculo CPU de referência com desvio absoluto inferior a `1e-4` (resolvendo hipótese WARN-01).
- [x] **3.8** `PicaShaderCorpus`: Cache de shaders offline persistente em disco com validação de cabeçalho, tolerância a corrupção (R15) e teto de memória configurável (R14).
- [x] **3.9** Suíte de testes automatizados [`pica_shader_translator_tests.cpp`](runtime/three_ds_recomp/tests/pica_shader_translator_tests.cpp) aprovada com 100% de sucesso.
- [x] **3.10** Decisões técnicas e hipóteses registradas em [`DECISIONS.md`](DECISIONS.md) (ADR-0006, resolução de WARN-01 e WARN-06).

---

## 3. Registro de Fallbacks e Alertas (R26)

- `NATIVE_FALLBACK`: 0 ativos no hot path.
- Orçamento de memória: 128 MB para texturas, 64 MB para superfícies de renderização, 32 MB para corpus de shaders (R14).
- Proteção contra buffers inexistentes e arquivos de cache corrompidos implementada sem abortos (R12, R15).
