# Renderização nativa de jogos de 3DS em GPUs Adreno

**Guia técnico passo a passo, sem emular a PICA200 em tempo de execução** Data: 04/10/2026 · Itens marcados com ⚠ vêm de conhecimento geral ou de fontes secundárias e precisam ser validados na prática.

---

## 0. Regras obrigatórias para o agente (ler ANTES de qualquer ação)

> Estas regras têm prioridade sobre o resto do documento, sobre pedidos de pressa e sobre qualquer "atalho". Se uma tarefa exigir violar uma regra, o agente **para e pergunta**. Cada regra tem um ID (R1, R2…) para citar em commits e relatórios.

### 0.0 Princípio norteador: natividade máxima

**O agente SEMPRE visa a natividade: rodar o máximo possível diretamente no hardware do Android** (CPU ARM64 com código compilado AOT, GPU Adreno via Vulkan), com o mínimo de interpretação, emulação e tradução em tempo de execução. Entre duas soluções corretas, escolhe-se a mais nativa. As regras de natividade estão em **0.8 (R23–R32)**.

**Ordem de precedência quando houver conflito:**

1. Segurança do código e dos dados (R1–R6)
2. Correção verificada (R7–R11)
3. **Natividade e aproveitamento do hardware (R23–R32)**
4. Conveniência e rapidez de implementação

Natividade nunca justifica pular teste, relaxar tolerância ou arriscar dados. Em contrapartida, "é mais fácil de implementar" nunca justifica ficar menos nativo.

### 0.1 Segurança do código e dos dados

- **R1.** Trabalhar sempre em branch própria (`feature/native-render-*`). Nunca commitar direto na branch principal.
- **R2.** Commits pequenos, um por subpasso, com a fase e o teste que passou na mensagem.
- **R3.** Nunca apagar, sobrescrever ou reescrever em massa arquivos fora do escopo da fase atual. Antes de qualquer mudança que apague arquivos ou toque mais de \~10, listar o que vai mudar e **esperar confirmação**.
- **R4.** Nunca executar comandos destrutivos sem confirmação explícita: `rm -rf`, `git reset --hard`, `git push --force`, `git clean -fdx`, limpar pastas de dados ou o cache do usuário.
- **R5.** Nunca tocar em saves, ROMs, chaves ou segredos. Nunca incluir ROMs, BIOS, chaves ou assets protegidos no repositório, em artefatos ou em logs. Nunca baixar nem fornecer material protegido.
- **R6.** Não adicionar dependências nem baixar binários ou drivers (ex.: Turnip) sem informar origem, licença, versão e hash e **pedir confirmação**. Não copiar código de outros projetos sem checar a compatibilidade da licença (ex.: GPL).

### 0.2 Honestidade e verificação

- **R7.** Nunca declarar "funciona", "corrigido" ou "concluído" sem rodar o teste de validação da fase e anexar a evidência (saída do teste, diff de frame, métricas). Sem evidência, o status é **"não verificado"**.
- **R8.** Itens marcados com ⚠ são hipóteses. Verificar contra captura real, Azahar ou 3dbrew antes de implementar como verdade, e registrar o resultado em `DECISIONS.md`.
- **R9.** Nunca inventar nomes de registradores, endereços, formatos, funções de API ou extensões Vulkan. Se não souber, consultar a fonte; se não achar, escrever "desconhecido" e abrir uma tarefa de investigação.
- **R10.** Nunca enfraquecer, desativar, pular ou editar testes e tolerâncias só para passar. Mudar tolerância exige justificativa escrita e confirmação.
- **R11.** Nunca esconder falhas. Stubs e atalhos devem ser explícitos no código (`PICA_UNIMPLEMENTED("...")`) e listados no relatório. Proibido retornar valores falsos em silêncio.

### 0.3 Segurança do renderer em runtime

- **R12.** Estado PICA desconhecido nunca derruba o app: sem `abort`/`throw` fatal. Registrar (com limite de taxa), usar fallback seguro (pular o draw ou usar pipeline simples) e continuar.
- **R13.** Toda chamada Vulkan checa `VkResult`. Validation layers ligadas em build debug; uma fase só conclui com **zero erros de validação**.
- **R14.** Todo cache e pool (texturas, superfícies, pipelines) tem teto configurável. Definir e registrar o orçamento de memória do aparelho-alvo no início da Fase 6.
- **R15.** Cache de shaders/pipelines é validado por versão, driver e hash. Cache corrompido é descartado e reconstruído, nunca causa crash. Só apagar caches dentro da pasta do próprio app.
- **R16.** Proibido compilar pipeline na thread de render sem fallback (ubershader ou pipeline simplificado), exceto na tela de preparação.
- **R17.** Proibido `gl_FragDepth` e geometry shader no caminho principal sem medição que justifique, registrada em `DECISIONS.md`.

### 0.4 Processo

- **R18.** Seguir as fases na ordem (0 → 10). Não iniciar uma fase sem o portão de qualidade da anterior (tabela 0.5).
- **R19.** **Parar e perguntar** quando: (a) a mudança sair do escopo; (b) este documento conflitar com o código; (c) o mesmo teste falhar 3 vezes pela mesma causa; (d) a ação for irreversível; (e) algo exigir rede, downloads ou credenciais.
- **R20.** Manter `DECISIONS.md` (decisão, motivo, evidência, ⚠ resolvido ou não) e `STATUS.md` (fase atual, o que passou, o que falhou).
- **R21.** Não reescrever a arquitetura "por limpeza". Mudança de design só com justificativa em `DECISIONS.md`.
- **R22.** Cada fase fica atrás de uma flag (`native_render=on/off`) que volta ao caminho anterior. O caminho antigo só pode ser removido após a validação final e confirmação do usuário.

### 0.5 Portões de qualidade (critério para encerrar cada fase)

| Fase | Critério de aprovação |
| --- | --- |
| 0 | Capturas e frames de referência salvos; versão/commit do Azahar anotada; scripts de portão criados |
| 1 | Dump de estado igual à referência em todas as capturas |
| 2 | Texturas idênticas byte a byte; sem vazamento no cache de superfícies |
| 3 | Posições de vértice dentro da tolerância registrada; 0 erros no `spirv-val` |
| 4 | Diff por pixel dentro da tolerância; cada função TEV e cada config de lighting testada |
| 5 | Cenas de z/alpha/stencil sem regressão |
| 6 | GMEM loads/stores medidos e registrados; sem leitura de RT externo no meio do pass |
| 7 | Sem hitch acima do limite em teste de 5 min; cache corrompido não causa crash |
| 8 | Frame idêntico entre o caminho HLE e o nível 2 |
| 9 | Matriz de aparelhos/drivers executada |
| 10 | Ganho medido contra o baseline, sem regressão de correção |

### 0.6 Relatório obrigatório ao fim de cada fase

```
Fase: <n>            Regras relevantes: <IDs>
Arquivos alterados: <lista>
Testes executados + evidência: <saída/diffs/métricas>
⚠ resolvidos / ainda abertos: <lista>
Stubs e pendências: <lista>
Índice de natividade (R32): <% de draws no caminho nativo; NATIVE_FALLBACK ativos;
  ms/frame CPU e GPU; recursos de hardware em uso; o que falta para 100%>
Riscos: <lista>
Próximo passo: <1 linha>
```

### 0.7 Como as regras são aplicadas (não basta "prometer")

Regras escritas orientam o agente, mas só scripts as tornam obrigatórias. **Na Fase 0 o agente DEVE criar e passar a executar antes de cada commit** (`tools/gate_check.sh` + hook de pre-commit/CI) com, no mínimo:

1. Bloqueio de arquivos de jogo/segredo no git (`.3ds`, `.cci`, `.cia`, `.cxi`, chaves, `.env`) e de arquivos acima de um tamanho limite.
2. Bloqueio de `push --force` na branch principal.
3. Falha se o número de testes diminuir ou se algum teste for marcado como *skip/ignored* sem justificativa.
4. Falha se `gl_FragDepth` ou geometry shader aparecerem no caminho principal sem registro em `DECISIONS.md`.
5. Execução da suíte de validação com validation layers e **zero erros**.
6. Verificação de que `STATUS.md` e `DECISIONS.md` foram atualizados no mesmo commit da fase.
7. Falha se existir no código algum `NATIVE_FALLBACK` que não esteja listado em `STATUS.md` com motivo e plano de remoção (R26).

Se qualquer verificação falhar, o commit não acontece. O agente não pode alterar o próprio script de portão sem confirmação do usuário (R10, R19).

### 0.8 Natividade máxima e aproveitamento do hardware (obrigatório)

- **R23.** Natividade é o objetivo permanente. Toda decisão de design escolhe, entre alternativas corretas, a que executa mais código nativo na CPU ARM64 (AOT) e na GPU Adreno (Vulkan), e menos interpretação, emulação e tradução em runtime. O nível 2 (seção 1) é a base; o **nível 3 (HLE da camada gráfica) é a meta**.
- **R24.** Precedência: R1–R6 > R7–R11 > natividade > conveniência (ver 0.0).
- **R25.** Proibido no caminho principal (*hot path*): interpretador de comandos ou de shaders, shader JIT na CPU, emulação de registrador por draw quando o estado puder virar um pipeline pré-compilado, e qualquer trabalho por vértice ou por pixel na CPU que a GPU possa fazer. Shaders de vértice PICA rodam como SPIR-V na GPU.
- **R26.** Caminhos não-nativos (interpretador, CPU shader, rota lenta) só existem como **andaime temporário**. Cada um é marcado no código com `NATIVE_FALLBACK(motivo, id)`, listado em `STATUS.md` com o que falta para removê-lo, e tem o custo medido (ms/frame e % dos draws). Nenhum fallback vira permanente sem decisão registrada em `DECISIONS.md`.
- **R27.** Trabalho de dados vai para onde for mais rápido, **medido**: deswizzle de textura, conversão de formatos, geração de LUT/procedural, expansão de geometria e cópias/reinterpretação de superfícies devem rodar em compute/transfer na GPU. Se a CPU for mais rápida ou a GPU estiver saturada, usar NEON na CPU. Nunca C escalar ingênuo no hot path.
- **R28.** Usar os recursos do hardware **quando o aparelho os expõe**, consultando as features em runtime (nunca assumir): ETC2 em hardware, FP16/`mediump`, subpasses ou local read com GMEM, compute, pipeline cache e pipeline library, subgroups. Cada uso tem fallback seguro (R12) e teste.
- **R29.** CPU: código AOT para ARM64 com NEON onde o compilador não vetoriza; threads separadas para lógica do jogo, render, compilação de pipelines e I/O, com prioridade e afinidade pensadas para núcleos big/LITTLE; sem locks no hot path; sem alocação dinâmica por frame (usar pools).
- **R30.** Integração com o Android: frame pacing (ex.: Swappy), hints de desempenho e térmicos do sistema (ADPF) e zero-copy via `AHardwareBuffer` onde fizer sentido. ⚠ Validar a disponibilidade de cada um por versão do Android e do driver. Reduzir carga **antes** do throttling térmico, não depois.
- **R31.** Medir, não supor. Toda afirmação de "mais nativo" ou "mais rápido" exige medição no aparelho-alvo (tempo de CPU/GPU por frame, banda, temperatura). Sem medição, não conta como ganho (R7).
- **R32.** Todo relatório de fase inclui o **índice de natividade** (ver 0.6): % de draws no caminho nativo, `NATIVE_FALLBACK` ativos, ms/frame de CPU e de GPU, recursos de hardware em uso e o que falta para chegar a 100%. Meta: 100% dos draws no caminho nativo, com fallback só para estados raros e registrados.

---

## 1. Premissa: o que "nativo" realmente significa

A Adreno não tem as unidades fixas da PICA200 (TEV, iluminação por LUT, sombras, gás, textura procedural). Não dá para "copiar a GPU" em hardware. O que dá é **traduzir cada função da PICA200 para shaders e estados Vulkan** que a Adreno executa. O que muda entre as abordagens é *onde* e *quando* essa tradução acontece:

| Nível | O que faz | Custo em runtime | Uso |
| --- | --- | --- | --- |
| 1. Emulação de registradores (Citra/Azahar) | Interpreta a lista de comandos PICA a cada frame e gera shaders sob demanda | Alto (decodificação + compilação durante o jogo) | Referência de correção |
| 2. Estado → pipelines pré-compilados | Decodifica o estado, mas shaders e pipelines vêm de um corpus gerado offline | Médio/baixo | **Base recomendada** |
| 3. HLE da camada gráfica do jogo | Intercepta as funções do jogo que *geram* os comandos e desenha direto em Vulkan | Mínimo | Só por jogo (recomp AOT) |

**Estratégia:** construir o nível 2 como alicerce e migrar para o nível 3 nos pontos em que o jogo é bem entendido. O nível 3 é a **meta permanente** (R23); o nível 1 nunca é o destino. Cada fase abaixo diz o que entregar e como validar.

---

## 2. Como a PICA200 funciona

### 2.1 Visão geral

- Projetada pela DMP, 268 MHz, arquitetura MAESTRO-2G, compatível com OpenGL ES 1.1 mais extensões proprietárias.
- **Estágio de fragmento 100% fixo** (não existe fragment shader). Vértice e geometria são programáveis.
- 3 processadores de vértice programáveis + **Primitive Engine** (shader de geometria com o mesmo ISA; desligado, vira o 4º processador de vértice).
- Extensões em hardware: iluminação por fragmento (LUT), sombras duras/suaves, subdivisão de polígonos (via GS), bump mapping e textura procedural, gás/partículas.

### 2.2 Pipeline

```
Lista de comandos (escritas em registradores)
  → Vertex shader (ISA PICA)
  → [Geometry shader / Primitive Engine]
  → Clip + Raster
  → Texturas (3 unidades + procedural)
  → Fragment lighting (LUTs)
  → TEV (6 estágios + combiner buffer)
  → Fog / Gás → Alpha test → Stencil/Depth → Blend / Logic op
  → Framebuffer
```

### 2.3 Shaders de vértice e geometria

- ISA inspirada no vs_3_0 do Direct3D. Registradores: entradas `v0–v15`, saídas `o0–o15`, temporários `r0–r15`, constantes `c0–c95` (vec4), 8 booleanos, 4 inteiros vec4, registradores de endereço e de loop.
- Controle de fluxo: `CALL`, `IF`, `LOOP` e **`JMP` arbitrário** (não existe em GLSL/SPIR-V estruturado).
- **Aritmética não-IEEE**: `inf * 0 = 0` (em GPUs normais dá NaN), inclusive em MUL, MAD, DP4.
- ⚠ Ponto flutuante de 24 bits (1-7-16) nos shaders. ⚠ Há errata de hardware (ex.: duas `mova` seguidas travam a GPU), irrelevante para tradução, mas explica código estranho nos binários.

### 2.4 Estágio de fragmento (fixo)

- **TEV:** 6 estágios (fontes secundárias citam 5; bibliotecas homebrew e a documentação da comunidade usam 6). Cada estágio combina até 3 fontes com funções *Replace, Modulate, Add, Add signed, Interpolate, Subtract, Dot3 RGB, Dot3 RGBA, Multiply-add, Add-multiply*, com escala e cor constante.
- **Combiner buffer:** saída guardada por um estágio e lida dois estágios depois; só os primeiros estágios escrevem nele.
- **Fragment lighting:** o vertex shader emite um quaternion (espaço da superfície); interpolado, ele gera produtos escalares (N·L, N·H, etc.) que **indexam LUTs de 256 entradas**. Uma "configuração" (0–7) define quais LUTs existem (D0, D1, SP, FR, RB/RG, RR, DA). Até 8 luzes; permite Blinn-Phong, Cook-Torrance, Ward; cel shading = degraus na LUT.
- **Fog, gás, sombra e textura procedural** (a 4ª unidade de textura é só procedural).
- **Saída:** D24S8; cores RGBA8/RGB8/RGB565/RGB5A1/RGBA4 ⚠ (conferir lista exata). Z de tela em **−1…0**, com plano de clip fixo `z ≤ 0`, e `depth = (z/w)·escala + offset` por draw.
- ⚠ Texturas e framebuffers em tiles 8×8 com ordem Morton; compressão ETC1/ETC1A4 (conferir no 3dbrew).

### 2.5 Lições de quem já traduziu isso (Citra/Azahar)

- Mover **toda** a emulação para a GPU hospedeira (inclusive shaders de vértice) deu \~2× de velocidade.
- Traduzir os shaders exigiu reestruturar `JMP` em funções GLSL por análise de fluxo; um interpretador com `switch` gigante ficou mais lento que o renderizador por software.
- A multiplicação "exata" (`inf*0=0`) custa caro em GPUs fracas e virou opção.
- Jogos reinterpretam depth/stencil como textura RGBA (contornos de Pokémon) — exige cópia/conversão.

---

## 3. A Adreno como alvo

- **Tile-based com FlexRender:** o driver alterna entre modo TBDR (binning + GMEM) e modo direto conforme a carga. Passes bem estruturados mantêm dados na GMEM e evitam tráfego de memória.
- **Fragment shaders completos:** TEV, iluminação, fog e alpha test viram código de shader.
- **Subpasses / input attachments** (ou `VK_KHR_dynamic_rendering_local_read`) permitem ler o próprio pixel sem sair da GMEM.
- **Invalidação de attachments:** `LOAD_OP_CLEAR/DONT_CARE` e `STORE_OP_DONT_CARE` evitam carga/descarga da GMEM (depth quase nunca precisa ser salvo).
- **Drivers:** o driver stock da Qualcomm varia muito por aparelho/Android; muitos usuários de emuladores recorrem a drivers Turnip (Mesa). Há relatos de falha de compilação de shader no driver stock em alguns casos. Planeje testar nos dois.
- **Seu aparelho é intermediário (6 GB):** orçamento apertado de banda e memória; prefira resolução interna moderada, `mediump` onde seguro e cache de pipelines agressivo.

---

## 4. Mapa de tradução PICA200 → Vulkan/Adreno

| Função PICA200 | Equivalente | Observação |
| --- | --- | --- |
| Vertex shader PICA | Vertex shader SPIR-V gerado do bytecode | Estruturar `JMP`; emular `inf*0`; UBO com 96 vec4 + bools + ints |
| Geometry shader / Primitive Engine | Evitar GS; pré-expansão em compute ou baking por jogo | GS existe no Vulkan, mas costuma ser ruim em tilers ⚠ medir |
| Atributos de vértice | Vertex input / buffers | Conversão de formatos (byte/short/float) e índices |
| 3 texturas + procedural | Samplers + textura gerada | Procedural: pré-computar ou compute |
| TEV (6 estágios) | Código gerado no fragment shader | Gerar por estado; specialization constants |
| Combiner buffer | Variável local com atraso de 2 estágios | Reproduzir a regra exata |
| Fragment lighting (LUT) | Texturas/buffers de 256 entradas + código | Uma LUT por tabela; configuração define as ativas |
| Fog / gás | Fog por LUT no shader | Gás só se o jogo usar |
| Alpha test | `discard` ou comparação no shader | Preferir em specialization constant |
| Depth `[-1,0]` + escala/offset | Aplicar em **z do vertex shader** | Evita `gl_FragDepth` (que desliga early-Z) ⚠ validar |
| Plano de clip `z ≤ 0` | `gl_ClipDistance` | Requer feature `shaderClipDistance` |
| Stencil, blend, logic op | Estado de pipeline Vulkan | Logic op: checar `logicOp`; senão framebuffer fetch |
| Render targets em memória do 3DS | Cache de superfícies por endereço/tamanho/formato | Reinterpretação depth↔cor exige conversão |
| Texturas Morton 8×8 | Deswizzle (CPU/compute) | ETC1 ⊂ ETC2: ⚠ verificar `textureCompressionETC2` |

---

## 5. Passo a passo

### Fase 0 — Referência e instrumentação

1. Tenha um **oráculo de correção**: rodar o mesmo trecho no Azahar e capturar frames de referência.
2. Capture **listas de comandos** (ou, num recomp, logue as escritas de registrador) de cenas representativas: menu, campo aberto, interior, cutscene, efeitos.
3. Configure RenderDoc e Snapdragon Profiler para o aparelho.
4. **Obrigatório (regras 0.7):** criar `tools/gate_check.sh`, o hook de pre-commit/CI, `STATUS.md` e `DECISIONS.md`, e a flag `native_render`. Nenhuma outra fase começa antes disso. **Entregável:** capturas + frames de referência + scripts de portão funcionando.

### Fase 1 — Decodificador de estado PICA

1. Implemente o parser da lista de comandos (cabeçalho: id de registrador, máscara de bytes, parâmetros, modo consecutivo).
2. Converta os registradores em **structs de estado** (`VertexState`, `TexState[3]`, `TevState[6]`, `LightingState`, `FogState`, `FramebufferState`, `DepthStencilState`, `BlendState`).
3. Gere uma **chave de estado** (hash) para cada draw. **Validação:** dump legível do estado igual ao do Azahar.

### Fase 2 — Dados: vértices, texturas e superfícies

1. **Vértices:** descrever os loaders de atributo (formato, offset, stride) e converter para vertex input Vulkan; índices u8/u16.
2. **Texturas:** deswizzle Morton, decodificar formatos não suportados nativamente (L4/A4/HILO8 etc.), mapear ETC1 para ETC2 nativo, compor ETC1A4. Cache por (endereço, tamanho, formato, hash).
3. **Superfícies de render:** cache indexado por endereço/dimensões/formato; tratar *display transfers*, *texture copies* e reinterpretação depth↔cor. Garanta fallback para copy sem fonte coerente (nunca abortar).
4. **Validação:** texturas idênticas byte a byte ao dump do Azahar.

### Fase 3 — Shaders de vértice (e geometria)

1. Decodifique o bytecode PICA (instruções, descritores de operando/swizzle).
2. **Estruturar o fluxo:** transformar `CALL/IF/LOOP/JMP` em blocos estruturados; reservar um interpretador lento só como último recurso.
3. Emitir **SPIR-V** (ou GLSL → SPIR-V). Emular `inf*0=0` com `sanitize_mul` **apenas onde necessário**, com opção global de ligar/desligar.
4. Layout de uniforms: um UBO por shader com os 96 vec4, bools e ints.
5. **Corpus offline:** para um jogo conhecido, extraia **todos** os shaders de vértice/geometria do jogo em tempo de build (o TriAevum, por exemplo, já embarca mais de mil módulos PICA portáteis e um compilador de passes de render) e gere SPIR-V no build, não no jogo.
6. **Validação:** comparar posições de saída com o Azahar, vértice a vértice.

### Fase 4 — Gerador de fragment shader

1. Gerar o fragment shader **a partir do estado fixo**: TEV estágio a estágio, combiner buffer, lighting por LUT, fog, alpha test, sombra.
2. Estratégia de variantes: **specialization constants** para o que muda pouco; chave de estado para o resto; um **ubershader** como fallback enquanto a variante compila.
3. LUTs: uma textura/buffer de 256 entradas por tabela, atualizada por draw só quando muda.
4. Precisão: cores da PICA são de 8 bits; testar `RelaxedPrecision/mediump` para TEV (ganho real na Adreno) mantendo `highp` em iluminação e coordenadas.
5. **Validação:** diff por pixel vs. Azahar em cenas de iluminação, cel-shading, fog e transparência.

### Fase 5 — Rasterização, profundidade e blending

1. Z: aplicar `escala/offset` no **vertex shader** (`z' = z·escala + offset·w`) e manter o clip `z ≤ 0` com `gl_ClipDistance`. ⚠ Validar clamping contra a PICA.
2. Mapear depth/stencil, culling/winding, scissor, viewport, polygon offset.
3. Blend e logic op: checar suporte; se faltar, usar framebuffer fetch.
4. **Validação:** cenas com z-fighting, decals, sombras projetadas e alpha-test (folhas/grama).

### Fase 6 — Render passes amigáveis à Adreno

1. Cada troca de render target do jogo vira um render pass; **agrupe draws** do mesmo alvo.
2. `LOAD_OP_CLEAR/DONT_CARE` e `STORE_OP_DONT_CARE` sempre que o conteúdo não for reutilizado (depth quase sempre).
3. Efeitos que leem o pixel atual: **subpass/input attachment** ou `dynamic_rendering_local_read`; evite ler RT externo no meio do pass.
4. Evite quebrar o pass por upload de textura; faça uploads antes do `vkCmdBeginRenderPass`.
5. **Validação:** Snapdragon Profiler: contagem de GMEM loads/stores e tempo de GPU por frame.

### Fase 7 — Pipelines, cache e stutter

1. `VkPipelineCache` persistente em disco; invalide por versão do driver/aparelho.
2. **Pré-aquecimento:** compilar o corpus conhecido na primeira execução (tela de "preparando shaders") e depois por carregamento de cena.
3. Compilação assíncrona com ubershader/pipeline simplificado enquanto a variante final não fica pronta; use `VK_EXT_graphics_pipeline_library` se o driver expuser.
4. **Validação:** nenhum hitch >33 ms em 5 minutos de jogo em cenas novas.

### Fase 8 — Rumo ao nativo de verdade (HLE da camada gráfica)

1. Num recomp AOT, localize as funções do jogo que **escrevem** a lista de comandos (a camada de gráficos do SDK).
2. Substitua essas funções por chamadas diretas ao seu renderer (uma "draw call" já estruturada, sem passar por registradores).
3. Ganhos: resolução e proporção livres (widescreen), texturas HD, interpolação de frames, menos decodificação. Risco: cada jogo é um caso; mantenha o caminho do nível 2 como fallback.
4. **Validação:** o mesmo frame sai igual nos dois caminhos.

### Fase 9 — Validação contínua

1. Regressão automática: *golden frames* vs. Azahar, com tolerância por pixel e relatório.
2. Matriz de aparelhos/drivers: Adreno stock e Turnip, 2–3 gerações.
3. Checklist por feature: TEV (cada função), lighting (cada config), fog, sombra, GS, procedural.

### Fase 10 — Otimização Adreno

1. Medir antes: banda, ALU, texturas, draw calls (a PICA200 em si é limitada por draw calls/estado, mas na Adreno o gargalo costuma ser banda e compilação).
2. Resolução interna escalável, `mediump` onde validado, mipmaps e compressão ETC2.
3. Desligar a multiplicação exata por padrão e ligar só em jogos/cenas que a exigem.

---

## 6. Armadilhas conhecidas

- **`JMP` arbitrário** e `inf*0=0` quebram traduções ingênuas.
- **Depth/stencil reinterpretados como cor** (contornos) e *texture copies* sobre alvos aliasados.
- **`gl_FragDepth`** desliga early-Z; tratar profundidade no vertex shader.
- **GS em tilers:** medir antes de depender.
- **Drivers Adreno:** shaders que compilam num driver e falham em outro; mantenha SPIR-V simples e valide com `spirv-val`.
- **Compilação durante o jogo** é a principal causa de stutter, por isso o corpus offline.

---

## 7. Ordem sugerida de entrega

1. Fases 0–2: estado + texturas corretos (imagem estática de um frame).
2. Fases 3–5: um frame completo igual ao Azahar.
3. Fase 6: desempenho estável no aparelho.
4. Fase 7: sem stutter.
5. Fase 8: HLE incremental, jogo a jogo.

---

## 8. Fontes consultadas

- 3dbrew: Hardware; GPU/Internal Registers; GPU/Fragment Lighting; GPU/Shader Instruction Set ([https://www.3dbrew.org](https://www.3dbrew.org))
- GBATEK 3DS GPU ([http://problemkaputt.de/gbatek-3ds-gpu-fragment-lighting.htm](http://problemkaputt.de/gbatek-3ds-gpu-fragment-lighting.htm))
- Citra/Azahar: "Hardware Renderer Updates" ([https://citra.azahar-emu.org/entry/improvements-to-hardware-renderer/](https://citra.azahar-emu.org/entry/improvements-to-hardware-renderer/)) e tese LIACS sobre decompilação de shaders PICA ([https://theses.liacs.nl/pdf/2018-2019-DelzenneMJ.pdf](https://theses.liacs.nl/pdf/2018-2019-DelzenneMJ.pdf))
- Wikipedia: PICA200; citro3d `texenv.h` ([https://github.com/devkitPro/citro3d](https://github.com/devkitPro/citro3d))
- Qualcomm: Adreno GPU on Mobile: Best Practices; VK_QCOM_tile_shading ([https://docs.vulkan.org/refpages/latest/refpages/source/VK_QCOM_tile_shading.html](https://docs.vulkan.org/refpages/latest/refpages/source/VK_QCOM_tile_shading.html))
- Vulkan Docs: Tile Based Rendering Best Practices
- TriAevum ([https://github.com/coccofresco/TriAevum](https://github.com/coccofresco/TriAevum)) e issues do Azahar sobre Vulkan/Adreno/Turnip ([https://github.com/azahar-emu/azahar](https://github.com/azahar-emu/azahar))