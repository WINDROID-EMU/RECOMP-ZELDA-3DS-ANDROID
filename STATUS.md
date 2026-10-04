# STATUS: Renderização Nativa PICA200 -> Adreno (Vulkan)

Última atualização: 2026-10-04  
Branch ativa: `feature/native-render-phase0-instrumentation`  
Fase atual: **Fase 0 — Referência e instrumentação** (Concluída) -> Próxima: **Fase 1 — Decodificador de estado PICA**

---

## 1. Portões de Qualidade (Quality Gates)

| Fase | Título | Critério de Aprovação | Status | Evidência / Notas |
| :---: | :--- | :--- | :---: | :--- |
| **0** | **Referência e instrumentação** | Capturas e frames de referência salvos; versão/commit do Azahar anotada; scripts de portão criados | **CONCLUÍDO** | `tools/gate_check.sh` operacional e integrado aos hooks; frames e oráculo documentados em `docs/PHASE0_REFERENCE_AND_INSTRUMENTATION.md`; `NativeRenderControl` testado e validado (R22). |
| **1** | Decodificador de estado PICA | Dump de estado igual à referência em todas as capturas | **PRÓXIMA** | Estruturação de `VertexState`, `TexState[3]`, `TevState[6]`, `LightingState`, etc. e hash de estado por draw. |
| **2** | Dados: vértices, texturas e superfícies | Texturas idênticas byte a byte; sem vazamento no cache de superfícies | PENDENTE | Depende da Fase 1 |
| **3** | Shaders de vértice (e geometria) | Posições de vértice dentro da tolerância registrada; 0 erros no `spirv-val` | PENDENTE | Depende da Fase 2 |
| **4** | Gerador de fragment shader | Diff por pixel dentro da tolerância; cada função TEV e cada config de lighting testada | PENDENTE | Depende da Fase 3 |
| **5** | Rasterização, profundidade e blending | Cenas de z/alpha/stencil sem regressão | PENDENTE | Depende da Fase 4 |
| **6** | Render passes amigáveis à Adreno | GMEM loads/stores medidos e registrados; sem leitura de RT externo no meio do pass | PENDENTE | Depende da Fase 5 |
| **7** | Pipelines, cache e stutter | Sem hitch acima do limite em teste de 5 min; cache corrompido não causa crash | PENDENTE | Depende da Fase 6 |
| **8** | Rumo ao nativo de verdade (HLE) | Frame idêntico entre o caminho HLE e o nível 2 | PENDENTE | Depende da Fase 7 |
| **9** | Validação contínua | Matriz de aparelhos/drivers executada | PENDENTE | Depende da Fase 8 |
| **10** | Otimização Adreno | Ganho medido contra o baseline, sem regressão de correção | PENDENTE | Depende da Fase 9 |

---

## 2. Acompanhamento da Fase 0

- [x] **0.1** Branch isolada `feature/native-render-phase0-instrumentation` criada (R1).
- [x] **0.2** Script de portão de qualidade [`tools/gate_check.sh`](tools/gate_check.sh) criado e executável (R0.7).
- [x] **0.3** Hooks locais instalados (`.git/hooks/pre-commit`, `.git/hooks/pre-push`) para validação automática (R0.7).
- [x] **0.4** Arquivos de governança [`STATUS.md`](STATUS.md) e [`DECISIONS.md`](DECISIONS.md) inicializados (R20).
- [x] **0.5** Definição da flag global e configuração de chaveamento `native_render` via [`NativeRenderControl`](runtime/three_ds_recomp/include/three_ds_recomp/native_render_control.h) com suíte de testes aprovada (R22).
- [x] **0.6** Definição da suíte de oráculo de referência (Azahar build/commit anotados, datasets de golden frames e dumps de comandos PICA) em [`docs/PHASE0_REFERENCE_AND_INSTRUMENTATION.md`](docs/PHASE0_REFERENCE_AND_INSTRUMENTATION.md).
- [x] **0.7** Validação e fechamento formal do portão da Fase 0.

---

## 3. Registro de Falhas e Alertas

- Nenhum bloqueio ativo. Todos os portões da Fase 0 estão verdes.
