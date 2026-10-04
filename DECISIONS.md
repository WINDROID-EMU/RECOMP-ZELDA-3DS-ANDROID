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

## 2. Matriz de Hipóteses e Validação Técnica (Itens ⚠)

| ID | Item / Hipótese | Origem | Teste / Validação Exigida | Status | Resolução / Notas |
| :--- | :--- | :---: | :--- | :---: | :--- |
| **WARN-01** | Shaders PICA utilizam float de 24 bits (1-7-16) | 3dbrew / Seção 2.3 | Comparar aritmética com precisão `mediump` vs `highp` SPIR-V em vertex transforms | **ABERTO** | Investigar se desvios numéricos afetam z-buffer ou matriz de projeção |
| **WARN-02** | Errata de hardware PICA (duas `mova` seguidas travam a GPU) | Seção 2.3 | Inspecionar descompilação de shaders do jogo no corpus TriAevum | **ABERTO** | Informativo para o parser do compilador de shaders |
| **WARN-03** | Lista exata de formatos de framebuffer: RGBA8, RGB8, RGB565, RGB5A1, RGBA4 | Seção 2.4 | Verificar mapeamento em registradores PICA (0x00400000) e Azahar `framebuffer.cpp` | **ABERTO** | Será formalizado na Fase 1 / 2 |
| **WARN-04** | Texturas e framebuffers em ordem Morton (tiles 8x8); compressão ETC1/ETC1A4 | 3dbrew / Seção 2.4 | Validar decodificador Morton e deswizzle na Fase 2 contra dump de textura Azahar | **ABERTO** | Será testado com verificação byte-a-byte na Fase 2 |
| **WARN-05** | ETC1 nativamente suportado como subconjunto de `textureCompressionETC2` em Vulkan | Seção 4 | Checar `VkPhysicalDeviceFeatures::textureCompressionETC2` no driver Qualcomm stock e Turnip | **ABERTO** | Mapeamento direto de formatos `VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK` |
| **WARN-06** | Aplicação de profundidade no Vertex Shader dispensa `gl_FragDepth` sem artefatos de clipping | Seção 4 / Seção 5 | Renderizar cenas com decals e z-fighting e comparar diff de profundidade contra Azahar | **ABERTO** | Foco de validação na Fase 3 e 5 |
| **WARN-07** | Geometry Shader em Adreno causa perda excessiva de desempenho comparado a compute/instancing | Seção 4 / Seção 6 | Benchmark comparativo no Snapdragon Profiler (Fase 6) | **ABERTO** | GS desativado por padrão conforme R17 |

---

## 3. Diretrizes de Governança
- Qualquer desvio ou adição de dependência deve ser submetido à aprovação com hash, licença e justificativa técnica (**R6**).
- Mudanças de tolerância de teste exigem justificativa fundamentada neste documento (**R10**).
