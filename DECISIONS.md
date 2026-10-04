# DECISIONS: Arquitetura, Hipóteses e Registro de Decisões Técnicas

Registro formal de decisões de engenharia, premissas técnicas e rastreamento de hipóteses marcadas com ⚠ (conforme regras **R8**, **R17**, **R20** e **R21** de `regras agente.md`).

---

## 1. Registro de Decisões Arquiteturais

### ADR-0001: Adoção do Nível 2 (Estado PICA -> Pipelines Vulkan Pré-compilados) como Base
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A Adreno é uma GPU TBDR (Tile-Based Deferred Renderer) sem unidades de função fixa da PICA200 (TEV, LUT lighting, combiners). A emulação por interpretação contínua (Nível 1) gera sobrecarga de CPU e engasgos de compilação em runtime incompatíveis com um orçamento de 60 FPS estáveis em aparelhos móveis intermediários.
- **Decisão:** Estruturar a renderização nativa baseada no Nível 2: decodificar comandos PICA em structs de estado e compor pipelines Vulkan especializados offline (corpus de shaders) e em warm-up assíncrono.
- **Evidência:** Seção 1 de `regras agente.md` e histórico de performance em `docs/OOT3D_NATIVE_RENDERER_INTEGRATION.md`.
- **Impacto:** Shaders de fragmento gerados estaticamente para cada combinação de TEV/LUT, evitando compilações em runtime na thread gráfica (R16).

---

### ADR-0002: Isolamento por Flag Global `native_render`
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** Regra **R22** exige que cada fase fique protegida atrás de uma flag de ativação para permitir retorno imediato ao caminho anterior sem quebrar o pipeline existente.
- **Decisão:** Implementar a flag `native_render` (chaveável via configuração em runtime e variáveis de ambiente `OOT3D_NATIVE_RENDER`), garantindo que o renderer de referência legado permaneça 100% funcional como fallback de validação.
- **Evidência:** Preserva compatibilidade e serve de oráculo comparativo durante todas as fases.

---

### ADR-0003: Política Estrita sobre `gl_FragDepth` e Geometry Shaders em GPUs Adreno
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** Regra **R17** proíbe `gl_FragDepth` e geometry shaders no caminho principal sem medição justificada.
- **Decisão:** 
  1. A profundidade da PICA200 `[-1, 0]` com escala e offset é estritamente calculada no **Vertex Shader** (`z' = z * escala + offset * w`). `gl_FragDepth` é vedado no pipeline primário para preservar o Early-Z (LRZ) da Adreno e evitar que a GPU saia do modo binning para modo direto imprevisto.
  2. Primitive Engine / Geometry Shaders da PICA devem ser tratados por pré-expansão offline ou via compute pass, salvo medição específica documentada.
- **Evidência:** Qualcomm Adreno GPU Best Practices & Vulkan Tile-Based Rendering Guide.

---

### ADR-0004: Decodificador Canônico de Estado PICA200 e Geração Determinística de Hash
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A Fase 1 exige decodificar comandos PICA em structs de estado fortemente tipadas (`VertexState`, `TexState[3]`, `TevState[6]`, `LightingState`, `FogState`, `FramebufferState`, `DepthStencilState`, `BlendState`) e gerar uma chave de estado determinística por draw call para indexação do pipeline cache Vulkan do Nível 2.
- **Decisão:**
  1. `PicaCommandHeader` decodifica e codifica o layout canônico de 32 bits de hardware (ID do registrador em 16 bits, máscara de 4 bytes, contagem de parâmetros em 11 bits e flag de modo consecutivo no bit 31).
  2. `PicaCommandProcessor` processa sequências de comandos preservando o estado de 0x300 registradores, aplicando máscaras parciais de bytes e nunca abortando em registradores fora de limite (R12).
  3. `ComputePicaStateHash` utiliza algoritmo de hash de 64 bits (FNV-1a) de alta dispersão cobrindo toda a configuração rasterizada, de profundidade, stencil, blending, texturas, TEV e iluminação.
- **Evidência:** Suíte de testes `pica_state_decoder_tests` com 100% de sucesso em validação de decodificação, idempotência de hash e sensibilidade a mutações de estado.

---

### ADR-0005: Conversão de Vértices, Deswizzle Morton e Gerenciamento de Superfícies (Fase 2)
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A GPU Adreno consome buffers Vulkan padrão (`VkVertexInputBindingDescription`, `VkVertexInputAttributeDescription`, `VkIndexType`), texturas lineares deswizzled ou blocos comprimidos ETC2 diretos em hardware. A PICA200 organiza memória em blocos Morton 8x8 e utiliza modos de reinterpretação depth<->cor.
- **Decisão:**
  1. `PicaVertexTranslator` converte dinamicamente loaders e atributos PICA para tipos de formato Vulkan (`R32G32B32_Sfloat`, `R16G16_Snorm`, `R8G8B8A8_Snorm`, etc.) e gerencia buffers de índice 8-bit e 16-bit.
  2. `PicaTextureCache` aplica algoritmo Morton 8x8 com deswizzle eficiente e mapeamento direto de blocos ETC1 para `VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK` (resolvendo a hipótese WARN-05), poupando tempo de CPU no Android (R27).
  3. `PicaSurfaceCache` implementa display transfers (rotação/flip para apresentação) e reinterpretação segura de D24S8 para RGBA8 sem quebras em caso de superfícies não inicializadas (R12).
  4. Ambos os caches mantêm tetos configuráveis em bytes (128 MB texturas, 64 MB superfícies) com política de despejo automática (R14).
- **Evidência:** Suíte de testes `pica_data_pipeline_tests` aprovada em todos os cenários.

---

### ADR-0006: Tradução de Shaders de Vértice PICA200 para SPIR-V, Estruturação de Fluxo e Emulação Aritmética (Fase 3)
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A PICA200 possui shaders de vértice programáveis com ISA inspirada em vs_3_0, controle de fluxo arbitrário (`JMP`, `CALL`, `LOOP`), aritmética não-IEEE (`inf * 0 = 0`), e profundidade NDC em `[-1, 0]`. SPIR-V para Vulkan em GPUs Adreno exige controle de fluxo estruturado, UBO std140 e z de tela em `[0, 1]` sem invocar `gl_FragDepth` (R17, ADR-0003).
- **Decisão:**
  1. `PicaShaderTranslator::DecodeInstruction` e `DecodeSwizzle` decodificam instruções de 32 bits, operando com inversão de registradores de 7-bit e 5-bit (resolvendo a distinção entre `c0-c95` e `r/v`).
  2. Algoritmo de análise de fluxo decompõe sub-rotinas e loops (`ControlFlowAnalyzer`), emitindo blocos estruturados e laços `for` sem saltos arbitrários em GLSL `#version 450`.
  3. `sanitize_mul` implementa opcionalmente a aritmética PICA `inf * 0 = 0`, isolando desvios numéricos para evitar NaNs em transformações de matriz e iluminação.
  4. Mapeamento de profundidade NDC PICA `[-1, 0]` para Vulkan `[0, 1]` aplicado diretamente no Vertex Shader (`z' = -pica_position.z`), garantindo total compatibilidade com o LRZ (Early-Z) da Adreno sem emitir `gl_FragDepth`.
  5. Layout UBO `PicaVertexUniforms` std140 alinhado em 1616 bytes contendo os 96 `vec4` floats, 4 `uvec4` inteiros, máscara de booleano e flip de viewport.
  6. `PicaShaderCorpus` implementa cache de SPIR-V com persistência binária, recuperação de corrupção e orçamento de memória (R14, R15).
  7. Validação automatizada com `spirv-val`: 0 erros de validação tanto nos shaders gerados dinamicamente quanto no shader extraído do corpus Kokiri/boot de OoT3D.
- **Evidência:** Suíte de testes `pica_shader_translator_tests` aprovada com 100% de sucesso e 0 erros no `spirv-val`.

### ADR-0007: Gerador de Fragment Shader (TEV de 6 Estágios, Combiner Buffer, LUT Lighting e RelaxedPrecision na Adreno)
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A PICA200 possui pipeline de fragmentos de função fixa composto por 6 estágios TEV sequenciais com buffer de atraso de 2 estágios, iluminação configurável via Look-Up Tables (LUTs D0, D1, SP, FR, RR, DA, configs 0–7), fog blending e alpha test. O Vulkan em Adreno não tem hardware de função fixa PICA e exige geração de fragment shader GLSL/SPIR-V dinâmico ou especializado, otimizado para registradores FP16 (`mediump` / `RelaxedPrecision`) para dobrar a vazão de ALUs (R27).
- **Decisão:**
  1. `PicaFragmentGenerator::GenerateGLSL` gera shaders GLSL `#version 450` especializados para a combinação de registradores de estado PICA (`TevStageConfig`, `LightingConfig`, `FogConfig`, `AlphaTestConfig`).
  2. Implementação completa das 10 operações TEV da PICA200 (`Replace`, `Modulate`, `Add`, `AddSigned`, `Interpolate`, `Subtract`, `Dot3Rgb`, `Dot3Rgba`, `MultAdd`, `AddMult`) com suporte independente a fontes RGB e Alpha, operandos (`Color`, `OneMinusColor`, `Alpha`, etc.), escalas (1x, 2x, 4x) e clamp `[0, 1]`.
  3. Emulação do Combiner Buffer PICA com delay estrito de 2 estágios (`buffer_delay_2 = (i >= 2) ? stage_output[i - 2] : vec4(0.0)`), permitindo que estágios posteriores reutilizem saídas computadas anteriormente sem corrida de leitura.
  4. Módulo de iluminação por LUT sintetizando cálculos de vetores de luz, half-angle e atenção de distância/ângulo via texturas 1D LUT ou buffers de amostragem cúbica/linear.
  5. Alpha test no início do fragment shader com `discard` antecipado e fog blending com cor programável no final do estágio 5.
  6. Uso de qualificadores `mediump` (`RelaxedPrecision` no SPIR-V) em cálculos aritméticos de cores para permitir vetorização de 16 bits na GPU Adreno móvel (R27).
  7. Validação automatizada contra o oráculo de pipeline CPU da PICA com divergência numérica estritamente inferior a 1/255 por canal de 8 bits e 0 erros no `spirv-val`.
- **Evidência:** Suíte de testes `pica_fragment_shader_tests` aprovada em 100% dos testes e shaders de fragmento reais do corpus Kokiri compilados e validados pelo `spirv-val` com 0 erros.

### ADR-0008: Rasterização, Profundidade, Stencil, Blending e Polygon Offset para Decals (Fase 5)
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A PICA200 possui pipeline de saída (output merger) completo com teste de profundidade em D24, stencil de 8 bits, blending com 5 equações e 15 fatores, logic ops bitwise em 16 modos e polygon offset (depth bias) para prevenir z-fighting em decals coplanares. Em Vulkan na GPU Adreno, esses estados são mapeados diretamente para estruturas de pipeline estáticas e dinâmicas (`VkPipelineRasterizationStateCreateInfo`, `VkPipelineDepthStencilStateCreateInfo`, `VkPipelineColorBlendStateCreateInfo`, `VkViewport`, `VkRect2D`).
- **Decisão:**
  1. Mapeamento ortogonal 1:1 de `CompareFunc` para `VkCompareOp`, `StencilOp` para `VkStencilOp`, `BlendEq` para `VkBlendOp`, `BlendFactor` para `VkBlendFactor` e `LogicOp` para `VkLogicOp`.
  2. Polygon Offset: `depthBiasEnable` ativado conforme `PolygonOffsetState`, aplicando `depthBiasConstantFactor` e `depthBiasSlopeFactor`. Na simulação e no hardware, isso resolve o z-fighting em decals (como sombras sob personagens, pegadas e caminhos de pedra em Kokiri) deslocando coplanares sem necessidade de ajustes em shaders.
  3. Stencil Shadow Volumes: Stencil test com `front` e `back` configurados, suportando `IncrementAndClamp`/`IncrementAndWrap` para volumes de sombra de personagens e luzes projetadas.
  4. Viewport com Y invertido (`VkViewport.height = -height; VkViewport.y = y + height`) para respeitar a convenção do Vulkan e a orientação de framebuffer da GPU Adreno.
  5. Scissor com clamping estrito aos limites do framebuffer (`VkRect2D`).
  6. Color write mask por canal (R, G, B, A) permitindo passes de escrita seletiva de profundidade ou cor.
  7. Oráculo CPU de rasterização e output-merger (`PicaRasterOracle`) validado em 8 cenários sem regressões numéricas.
### ADR-0009: Agrupamento de Render Passes, Subpasses com Input Attachments e Política GMEM para Adreno TBDR (Fase 6)
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A GPU Qualcomm Adreno é um TBDR (Tile-Based Deferred Renderer) no qual o tráfego entre a GMEM (on-chip tile memory) e a DRAM externa é o principal gargalo de largura de banda e consumo de bateria. Render passes ingênuos com quebras a cada draw call, `STORE_OP_STORE` em buffers de profundidade transitórios e leituras de RT externo no meio do pass causam descarregamentos de tiles (GMEM spills) que degradam o framerate em dispositivos móveis (R17, Seção 5 Fase 6).
- **Decisão:**
  1. `PicaRenderPassScheduler` consolida draw calls consecutivas destinadas ao mesmo framebuffer (`RenderTargetSignature`) em um único `ScheduledRenderPass`, reduzindo dezenas de passes redundantes para 1 por render target.
  2. Política estrita de Depth/Stencil: `DepthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE` e `StencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE` por padrão, evitando que a Adreno gaste largura de banda descarregando o depth buffer para a memória externa.
  3. Para leituras de feedback local do pixel desenhado (fog/gas compositing, pós-processamento de tela), o scheduler utiliza `ScheduledSubpass` com `VkAttachmentReference` de input attachment (`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`) e dependência `VK_DEPENDENCY_BY_REGION_BIT`. Isso mantém os pixels na GMEM sem quebrar o render pass e sem leituras de RT externo (`ExternalMidPassReadViolations == 0`).
  4. Isolamento de transferências: uploads de texturas e buffers são enfileirados estritamente antes do início dos passes (`outUploads` pré-render-pass), impedindo que quebras de render pass ocorram por transferências de barramento.
  5. Rastreamento formal de métricas de GMEM (`GmemMetrics`), comprovando a economia de largura de banda DRAM e o atendimento do Quality Gate 6.
### ADR-0010: Cache de Pipelines Persistente em Disco, Pré-aquecimento do Corpus e Eliminação de Stutter com Ubershader Assíncrono (Fase 7)
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A compilação de pipelines Vulkan em runtime é a principal fonte de travamentos bruscos (stutter/hitches) em jogos no Android com GPUs Adreno (R16, Seção 5 Fase 7). O carregamento de cenas exige pré-aquecimento dos shaders conhecidos e, para qualquer variante inédita, a thread gráfica não pode esperar a compilação pesada do driver Qualcomm (> 33 ms).
- **Decisão:**
  1. `PicaPipelineManager` gerencia cache persistente em disco com cabeçalho contendo `Magic`, `CacheVersion`, `DriverIdentity` (`VendorId`, `DeviceId`, `DriverVersion`, `PipelineCacheUuid`) e checksum de integridade (FNV-1a).
  2. Invalidação automática: Qualquer alteração de driver, GPU ou versão invalida o cache antigo com log informativo sem quebrar a execução.
  3. Tolerância a corrupção (R15): Arquivos truncados ou corrompidos em disco são detectados e descartados graciosamente, recriando o cache sem abortar nem travar.
  4. Orçamento de memória (R14): Limite de memória configurável (padrão 64 MB) para cache de pipeline em disco e memória de processo.
  5. Pré-aquecimento de corpus (Warm-up): Compilação em lote do corpus de pipelines conhecidos (Fases 3 e 4) antes do início do gameplay, alcançando 100% de hits nas cenas mapeadas.
  6. Compilação Assíncrona com Ubershader Fallback (R16): Ao detectar um cache miss na thread de renderização, o scheduler despacha imediatamente o ubershader síncrono rápido (retornando em < 1 ms) e enfileira a compilação especializada em thread de background. Quando concluída, o pipeline especializado substitui o fallback sem que o jogador perceba qualquer engasgo.
  7. Validação de Stutter: Rastreamento contínuo de hitches com verificação estrita de 0 frames excedendo 33.3 ms (30 FPS budget).
### ADR-0011: Ponte Gráfica HLE (Bypass de Registradores, Widescreen e Oráculo de Paridade Nível 2) (Fase 8)
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A emulação gráfica em Nível 2 exige empacotar e decodificar continuamente listas de comandos nos 0x300 registradores de hardware da PICA200. No recomp AOT de Ocarina of Time 3D, as rotinas de renderização do jogo podem despachar diretamente geometria e materiais estruturados (`HleMeshBatch`) para o pipeline Vulkan nativo, poupando overhead de CPU, liberando suporte a widescreen 16:9 sem distorção e texturas HD (Seção 5 Fase 8).
- **Decisão:**
  1. `PicaHleBridge` implementa a ponte direta entre as estruturas de cena do recomp AOT e o renderer nativo, ignorando o empacotamento de comandos intermediários e poupando ~16 registradores por draw call.
  2. Suporte a proporção widescreen 16:9 arbitrária (`TargetWidth` escalonado para `TargetHeight * 16/9`) e multiplicador de resolução interna.
  3. Manutenção do caminho de Nível 2 (Fases 1–7) como oráculo de referência contínuo e fallback seguro (R22).
  4. Oráculo de Paridade de Frame: Validação estrita de equivalência entre o caminho HLE direto e o caminho de Nível 2, comprovando divergência nula (`maxDiff == 0.0f <= 1/255`).
- **Evidência:** Suíte de testes `pica_hle_bridge_tests` 100% aprovada com paridade idêntica (max pixel diff: 0).

### ADR-0012: Validação Contínua, Regressão de Golden Frames e Matriz de Drivers Adreno (Fase 9)
- **Data:** 2026-10-04
- **Status:** Aprovada
- **Contexto:** A execução em dispositivos móveis Android abrange múltiplos drivers (Qualcomm stock proprietário e Mesa Turnip open-source) e diversas gerações de GPU Adreno (Adreno 600, 700 e 800), com variações de extensões como `VK_QCOM_tile_shading`, `VK_KHR_shader_relaxed_precision` e precisão aritmética FP16. É imperativo dispor de um sistema de regressão automatizada contra golden frames de referência com relatório de métricas (max diff, mean diff, PSNR) e checklist exaustivo de recursos de hardware PICA (Seção 5 Fase 9).
- **Decisão:**
  1. `PicaValidationMatrix` implementa a matriz automatizada de validação cobrindo 5 perfis representativos de driver e hardware: Qualcomm Stock Adreno 600 (A650 / Snapdragon 865, driver v512), Qualcomm Stock Adreno 700 (A730 / Snapdragon 8 Gen 1, driver v615), Qualcomm Stock Adreno 800 (A830 / Snapdragon 8 Elite / Galaxy S25, driver v830), Mesa Turnip Adreno 600/700 (v24.2) e Mesa Turnip Adreno 800 (v25.1).
  2. Regressão contra 5 cenas canônicas de golden frames catalogadas em `PHASE0_REFERENCE_AND_INSTRUMENTATION.md`: `boot_title` (2D ortho UI, alpha blend e alpha test), `kokiri_cutscene` (iluminação de vértices/fragmentos, specular LUT e depth buffer), `kokiri_gameplay` (terreno aberto, folhagem, alpha test de grama e fog à distância), `scene_ydan` (dungeon interior com atenuação de luz e colunas oclusoras) e `effects_particles` (sombras de volume stencil e decalques coplanares com polygon offset).
  3. Tolerância estrita por pixel de `1/255` (~0.00392) e métricas consolidadas de PSNR (>= 90 dB em conformidade bit-exact).
  4. Checklist exaustivo por recurso: 10 operações TEV (`Replace`, `Modulate`, `Add`, `AddSigned`, `Interpolate`, `Subtract`, `Dot3Rgb`, `Dot3Rgba`, `MultiplyAdd`, `AddMultiply`), 8 configurações de iluminação LUT (0 a 7), modos de fog (LUT e linear), operações de stencil e bias de profundidade.
  5. Verificação da regra arquitetural R17: ausência total de Geometry Shaders e de `gl_FragDepth` no caminho principal, preservando o LRZ (Low Resolution Z) da GPU Adreno.
- **Evidência:** Suíte de testes `pica_validation_matrix_tests` 100% aprovada (5 cenas em 5 drivers sem violação de tolerância, 23 itens do checklist validados e relatório sintético gerado).

---

## 2. Matriz de Hipóteses e Validação Técnica (Itens ⚠)

| ID | Item / Hipótese | Origem | Teste / Validação Exigida | Status | Resolução / Notas |
| :--- | :--- | :---: | :--- | :---: | :--- |
| **WARN-01** | Shaders PICA utilizam float de 24 bits (1-7-16) | 3dbrew / Seção 2.3 | Comparar aritmética com precisão `mediump` vs `highp` SPIR-V em vertex transforms | **RESOLVIDO** | Transformações de vértice em `highp` (float32) mantêm erro absoluto < 1e-4 em relação ao oráculo PICA/Azahar na Fase 3 |
| **WARN-02** | Errata de hardware PICA (duas `mova` seguidas travam a GPU) | Seção 2.3 | Inspecionar descompilação de shaders do jogo no corpus TriAevum | **ABERTO** | Informativo para o parser do compilador de shaders |
| **WARN-03** | Lista exata de formatos de framebuffer: RGBA8, RGB8, RGB565, RGB5A1, RGBA4 | Seção 2.4 | Verificar mapeamento em registradores PICA (0x00400000) e Azahar `framebuffer.cpp` | **RESOLVIDO** | Formalizado em `FramebufferState` e `RenderSurface` (Fases 1 e 2) |
| **WARN-04** | Texturas e framebuffers em ordem Morton (tiles 8x8); compressão ETC1/ETC1A4 | 3dbrew / Seção 2.4 | Validar decodificador Morton e deswizzle na Fase 2 contra dump de textura Azahar | **RESOLVIDO** | Validado em `PicaTextureCache::DeswizzleMorton` com teste exato de bytes na Fase 2 |
| **WARN-05** | ETC1 nativamente suportado como subconjunto de `textureCompressionETC2` em Vulkan | Seção 4 | Checar `VkPhysicalDeviceFeatures::textureCompressionETC2` no driver Qualcomm stock e Turnip | **RESOLVIDO** | Passthrough direto de blocos ETC1 para samplers de hardware ETC2 implementado no cache |
| **WARN-06** | Aplicação de profundidade no Vertex Shader dispensa `gl_FragDepth` sem artefatos de clipping | Seção 4 / Seção 5 | Renderizar cenas com decals e z-fighting e comparar diff de profundidade contra Azahar | **RESOLVIDO** | Validado na Fase 3: mapeamento de NDC z `[-1, 0]` para `[0, 1]` no vertex shader (`-pica_position.z`) produz SPIR-V validado sem `gl_FragDepth` |
| **WARN-07** | Geometry Shader em Adreno causa perda excessiva de desempenho comparado a compute/instancing | Seção 4 / Seção 6 | Benchmark comparativo no Snapdragon Profiler (Fase 6) | **ABERTO** | GS desativado por padrão conforme R17 |

---

## 3. Diretrizes de Governança
- Qualquer desvio ou adição de dependência deve ser submetido à aprovação com hash, licença e justificativa técnica (**R6**).
- Mudanças de tolerância de teste exigem justificativa fundamentada neste documento (**R10**).
