# STATUS: Renderização Nativa PICA200 -> Adreno (Vulkan)

Última atualização: 2026-10-04  
Branch ativa: `feature/native-render-phase4-fragment-shaders`  
Fase atual: **Fase 7 — Pipelines, cache e stutter** (Concluída) -> Próxima: **Fase 8 — Rumo ao nativo de verdade (HLE)**

---

## 1. Portões de Qualidade (Quality Gates)

| Fase | Título | Critério de Aprovação | Status | Evidência / Notas |
| :---: | :--- | :--- | :---: | :--- |
| **0** | **Referência e instrumentação** | Capturas e frames de referência salvos; versão/commit do Azahar anotada; scripts de portão criados | **CONCLUÍDO** | `tools/gate_check.sh` operacional e integrado aos hooks; frames e oráculo documentados em `docs/PHASE0_REFERENCE_AND_INSTRUMENTATION.md`; `NativeRenderControl` testado e validado (R22). |
| **1** | **Decodificador de estado PICA** | Dump de estado igual à referência em todas as capturas | **CONCLUÍDO** | `PicaCommandProcessor` e `DecodePicaDrawState` implementados; structs canônicas (`VertexState`, `TexState[3]`, `TevState[6]`, etc.) e hash determinístico (`ComputePicaStateHash`) validados em `pica_state_decoder_tests`. |
| **2** | **Dados: vértices, texturas e superfícies** | Texturas idênticas byte a byte; sem vazamento no cache de superfícies | **CONCLUÍDO** | `PicaVertexTranslator`, `PicaTextureCache` (com deswizzle Morton 8x8 e passthrough ETC1->ETC2) e `PicaSurfaceCache` (display transfer e reinterpretação D24S8) validados em `pica_data_pipeline_tests`. |
| **3** | **Shaders de vértice (e geometria)** | Posições de vértice dentro da tolerância registrada; 0 erros no `spirv-val` | **CONCLUÍDO** | `PicaShaderTranslator` (decodificador de bytecode, reestruturação de fluxo JMP/CALL/LOOP, sanitize_mul, std140 UBO 1616B e mapeamento de profundidade em vertex stage sem `gl_FragDepth`) e `PicaShaderCorpus` validados com 100% de sucesso e 0 erros no `spirv-val`. |
| **4** | **Gerador de fragment shader** | Diff por pixel dentro da tolerância (< 1/255); cada função TEV e cada config de lighting testada; 0 erros no `spirv-val` | **CONCLUÍDO** | `PicaFragmentGenerator`: 10 operações TEV em 6 estágios, combiner buffer com delay de 2 estágios, iluminação LUT configs 0–7, fog blending, alpha test com discard, precisão `mediump`/`RelaxedPrecision` (R27) e 0 erros no `spirv-val`. |
| **5** | **Rasterização, profundidade e blending** | Cenas de z/alpha/stencil sem regressão | **CONCLUÍDO** | `PicaRasterPipeline`: Mapeamento de Viewport (Y-flip Adreno), Scissor, 8 Compare Ops, 8 Stencil Ops, 5 Blend Equations, 15 Blend Factors, 16 Logic Ops, Polygon Offset para Decals (elimina Z-fighting), e oráculo CPU validado em `pica_raster_pipeline_tests`. |
| **6** | **Render passes amigáveis à Adreno** | GMEM loads/stores medidos e registrados; sem leitura de RT externo no meio do pass | **CONCLUÍDO** | `PicaRenderPassScheduler`: Agrupamento de draws por alvo, política `DepthStoreOp = DONT_CARE` para evitar despejo de GMEM na DRAM, subpasses com input attachment para feedback de cor local (`VK_DEPENDENCY_BY_REGION_BIT`), 0 violações de mid-pass reads, validado em `pica_render_pass_scheduler_tests`. |
| **7** | **Pipelines, cache e stutter** | Sem hitch acima do limite em teste de 5 min; cache corrompido não causa crash | **CONCLUÍDO** | `PicaPipelineManager`: Cache em disco com cabeçalho de driver e checksum FNV-1a, tolerância a arquivos truncados/corrompidos (R15), teto de 64 MB (R14), warm-up do corpus de pipelines, compilação assíncrona com ubershader fallback (< 1 ms, R16) e 0 hitches > 33.3 ms validado em `pica_pipeline_manager_tests`. |
| **8** | Rumo ao nativo de verdade (HLE) | Frame idêntico entre o caminho HLE e o nível 2 | **PRÓXIMA** | Mapeamento direto de chamadas SDK de gráficos para draws nativas sem passar pela decodificação intermediária de registradores. |
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

## 3. Acompanhamento da Fase 4

- [x] **4.1** `PicaFragmentGenerator`: Geração de fragment shaders Vulkan GLSL `#version 450` a partir do estado fixo da PICA200.
- [x] **4.2** TEV de 6 estágios: Implementação das 10 operações ortogonais em RGB e Alpha (`Replace`, `Modulate`, `Add`, `AddSigned`, `Interpolate`, `Subtract`, `Dot3Rgb`, `Dot3Rgba`, `MultAdd`, `AddMult`).
- [x] **4.3** Modificadores e escalonamento TEV: Fontes de operandos (`Color`, `OneMinusColor`, `Alpha`, `OneMinusAlpha`), multiplicadores de escala (1x, 2x, 4x) e clamping estrito `[0, 1]`.
- [x] **4.4** Combiner Buffer: Emulação do atraso de hardware de 2 estágios (`buffer_delay_2`), garantindo consistência com o pipeline original da PICA200 sem corridas de leitura de estágio anterior.
- [x] **4.5** Iluminação LUT PICA: Suporte às tabelas de consulta D0, D1, SP, FR, RR, DA com modos de configuração 0 a 7, sintetizando luzes direcionais e pontuais com diffuse e specular.
- [x] **4.6** Fog Blending e Alpha Test: Teste de transparência com `discard` antecipado e mistura de névoa (fog linear/LUT) com cor e fatores configuráveis.
- [x] **4.7** Otimização Adreno FP16: Variáveis e operações de cor emuladas com qualificadores `mediump` (`RelaxedPrecision` no SPIR-V) para execução em registradores de 16 bits (R27).
- [x] **4.8** Validação SPIR-V: Compilação via `glslangValidator` e validação com `spirv-val` com **0 erros de validação** em pipelines sintéticos e shaders reais do corpus de fragmento Kokiri/boot de OoT3D.
- [x] **4.9** Paridade de pixel: Comparação contra o oráculo CPU de pipeline da PICA com erro absoluto inferior a `1/255` por canal de 8 bits em todos os testes.
- [x] **4.10** Suíte de testes automatizados [`pica_fragment_shader_tests.cpp`](runtime/three_ds_recomp/tests/pica_fragment_shader_tests.cpp) aprovada com 100% de sucesso.
- [x] **4.11** Decisões técnicas registradas em [`DECISIONS.md`](DECISIONS.md) (ADR-0007).

---

## 4. Acompanhamento da Fase 5

- [x] **5.1** `PicaRasterPipeline`: Mapeamento de estado de rasterização, profundidade, stencil e blending para Vulkan (`VkPipelineRasterizationStateCreateInfo`, `VkPipelineDepthStencilStateCreateInfo`, `VkPipelineColorBlendStateCreateInfo`).
- [x] **5.2** Viewport e Scissor: Mapeamento de Viewport com inversão Y (`VkViewport.height = -height; VkViewport.y = y + height`) para conformidade Vulkan/Adreno e Scissor com clamping estrito aos limites do framebuffer.
- [x] **5.3** Teste de profundidade: Suporte a todos os 8 modos de comparação (`Never`, `Always`, `Equal`, `NotEqual`, `Less`, `LessOrEqual`, `Greater`, `GreaterOrEqual`) e controle de escrita seletiva (`DepthWrite`).
- [x] **5.4** Polygon Offset (Depth Bias): Emulação e mapeamento de constantes e fatores de inclinação da PICA200 para eliminação comprovada de Z-fighting em decalques coplanares (pegadas, sombras de chão).
- [x] **5.5** Teste e operações de Stencil: Mapeamento dos 8 operadores de stencil (`Keep`, `Zero`, `Replace`, `Increment`, `Decrement`, `Invert`, `IncrementWrap`, `DecrementWrap`), máscaras de leitura/escrita e suporte a volumes de sombra projetada.
- [x] **5.6** Mistura de cores (Color Blending): Mapeamento das 5 equações de blend e 15 fatores de escala para canais RGB e Alpha de forma independente, além de Constant Color.
- [x] **5.7** Operações Lógicas (Logic Ops): Suporte aos 16 operadores lógicos bitwise quando o blending está desativado.
- [x] **5.8** Máscara de escrita de cor: Controle independente de escrita por canal (R, G, B, A).
- [x] **5.9** Oráculo CPU de simulação (`PicaRasterOracle`): Validação em software do pipeline de fragmentos completo em buffer 2D (RGBA8 + D24S8).
- [x] **5.10** Suíte de testes automatizados [`pica_raster_pipeline_tests.cpp`](runtime/three_ds_recomp/tests/pica_raster_pipeline_tests.cpp) aprovada com 100% de sucesso.
- [x] **5.11** Decisões técnicas registradas em [`DECISIONS.md`](DECISIONS.md) (ADR-0008).

---

## 5. Acompanhamento da Fase 6

- [x] **6.1** `PicaRenderPassScheduler`: Agrupamento e consolidação de draw calls consecutivas que visam o mesmo framebuffer (`RenderTargetSignature`), minimizando a troca de render passes no hardware móvel.
- [x] **6.2** Política de Depth/Stencil `DONT_CARE`: Configuração padrão de `DepthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE` e `StencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE`, eliminando o custo de despejo do depth buffer da GMEM para a DRAM externa.
- [x] **6.3** Política de Load de Framebuffer: Aplicação de `VK_ATTACHMENT_LOAD_OP_CLEAR` no primeiro pass em que o alvo é acessado no frame, e `VK_ATTACHMENT_LOAD_OP_LOAD` em passes subsequentes, mantendo a consistência visual.
- [x] **6.4** Subpasses locais com Input Attachments: Detecção automática de leitura de textura apontando para o próprio framebuffer ativo e divisão em `ScheduledSubpass` com `UsesInputAttachment = true` e dependência `VK_DEPENDENCY_BY_REGION_BIT`, eliminando quebras de render pass e vazamentos para a DRAM.
- [x] **6.5** Isolamento de transferências de textura: Enfileiramento de cópias e uploads de textura estritamente antes do início de qualquer render pass (`outUploads` pré-pass), garantindo conformidade com a Seção 5 item 4 das regras.
- [x] **6.6** Medição e rastreamento de métricas GMEM (`GmemMetrics`): Comprovação de 0 violações de mid-pass reads (`ExternalMidPassReadViolations == 0`), dezenas de draw calls agrupadas e gigabytes de largura de banda DRAM economizados por segundo.
- [x] **6.7** Suíte de testes automatizados [`pica_render_pass_scheduler_tests.cpp`](runtime/three_ds_recomp/tests/pica_render_pass_scheduler_tests.cpp) aprovada com 100% de sucesso.
- [x] **6.8** Decisões técnicas registradas em [`DECISIONS.md`](DECISIONS.md) (ADR-0009).

---

## 6. Acompanhamento da Fase 7

- [x] **7.1** `PicaPipelineManager`: Cache de pipelines persistente em disco com cabeçalho contendo `Magic`, `CacheVersion` e `DriverIdentity` (`VendorId`, `DeviceId`, `DriverVersion`, `PipelineCacheUuid`).
- [x] **7.2** Invalidação automática de driver: Detecção de atualizações de driver ou GPU para descarte transparente de pipelines obsoletos.
- [x] **7.3** Tolerância a corrupção e arquivos truncados (R15): Checksum de integridade FNV-1a com verificação de tamanho de payload, descartando arquivos inválidos sem abortos ou crashes.
- [x] **7.4** Limite de orçamento de memória (R14): Teto de 64 MB configurável para cache de pipelines.
- [x] **7.5** Pré-aquecimento de corpus (Warm-up): Pré-compilação em lote do corpus de shaders conhecidos (Fases 3 e 4) antes do gameplay, garantindo 100% de cache hits nas cenas indexadas.
- [x] **7.6** Compilação assíncrona com Ubershader Fallback (R16): Despacho imediato de ubershader rápido (< 1 ms) na thread de renderização enquanto variantes especializadas compilam em thread de background, eliminando travamentos de compilação.
- [x] **7.7** Medição e rastreamento de Stutter: Rastreamento contínuo de hitches com verificação estrita de **0 hitches acima do limite de 33.3 ms** ao longo da execução.
- [x] **7.8** Suíte de testes automatizados [`pica_pipeline_manager_tests.cpp`](runtime/three_ds_recomp/tests/pica_pipeline_manager_tests.cpp) aprovada com 100% de sucesso.
- [x] **7.9** Decisões técnicas registradas em [`DECISIONS.md`](DECISIONS.md) (ADR-0010).

---

## 7. Registro de Fallbacks e Alertas (R26)

- `NATIVE_FALLBACK`: 0 ativos no hot path.
- Orçamento de memória: 128 MB para texturas, 64 MB para superfícies de renderização, 32 MB para corpus de shaders (R14).
- Proteção contra buffers inexistentes e arquivos de cache corrompidos implementada sem abortos (R12, R15).
