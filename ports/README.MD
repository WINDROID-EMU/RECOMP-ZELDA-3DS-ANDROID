Com base no que está documentado no próprio repo (`README.md`, `INTRO.md`, `controls/README.md` e `SHADER_PREPARATION.md` dentro de `ports/android/`), esse é o estado real das pendências — não é uma opinião minha, é o que os próprios logs de dev registram:

## 1. Controles (input) — nada conectado ainda
**Falta:** o overlay de toque do Azahar foi importado (`controls/`) mas está isolado, sem ligação com o app SDL real. Também falta suporte a controle USB/Bluetooth.
**Como fazer:**
- Implementar `Native3dsInputTarget` contra o input owner nativo existente, mesclando overlay + controle físico ali (nunca rodando comandos de jogo na UI thread).
- Instanciar `OverlayHost` com um `AzaharInputAdapter`, montar o `InputOverlay` por cima da surface SDL do jogo.
- Aplicar a transformação inversa de apresentação/TopScreen (letterbox, aspect ratio) antes de converter toque em coordenada de tela do 3DS — nunca usar `x/width, y/height` cru.
- Garantir release de input em pause/perda de foco/mudança de layout/teardown, e rebind ao resumir.
- Ajustar a flag `exported`/`not-exported` do receiver de HID do SDL (exigência do target SDK) antes de validar controle USB.

## 2. Importação de ROM / instalador
**Falta:** hoje a ROM é preparada manualmente no PC (via Forge + `prepare_intro_data.py`) e empurrada por ADB com `run-as`. Não existe fluxo de usuário final.
**Como fazer:**
- Trocar o script Python por um serviço nativo de import que use o **document picker** do Android (Storage Access Framework) em vez de caminho de arquivo — URIs de documento não são paths de filesystem, então isso exige uma camada própria de leitura.
- Portar a etapa de preparação de dados (o que hoje o Forge/`prepare_intro_data.py` faz) para dentro do app ou de um instalador Android dedicado, com progresso e cancelamento.
- Empacotar título ARM64 pré-compilado + runtime junto do instalador (hoje isso é feito manualmente pelo script `stage_native_app.py`).

## 3. Ciclo de vida do processo/app
**Falta:** reiniciar o app depois que a Activity é destruída não funciona — os globais do runtime (herdados do desktop) são "process-lifetime", então um segundo boot na mesma sessão de processo quebra.
**Como fazer:** hoje o workaround é forçar `force-stop` do pacote antes de cada novo lançamento. A correção real é revisar os globais estáticos do runtime (herança do código desktop) para suportar init/destroy repetido dentro do mesmo processo Android — isso é trabalho de C++ no runtime compartilhado, não só na camada Android.

## 4. Perda/recriação de surface Vulkan
**Falta:** `VK_ERROR_SURFACE_LOST_KHR` já foi observado quando o app é colocado em background durante o startup; o comportamento nesse caso não está validado.
**Como fazer:** testar sistematicamente os eventos de lifecycle (`surfaceDestroyed`/`surfaceCreated`, tela bloqueando, app indo pra background) e garantir que o swapchain é recriado corretamente sem crash, reusando a lógica de invalidação "seletiva" já implementada (que hoje só recria quando extent/format/transform realmente mudam).

## 5. Áudio
**Falta:** playback de áudio nativo no Android ainda não foi validado — está listado explicitamente como pendente junto com input e surface loss.
**Como fazer:** validar o binding do dispositivo de áudio do host Android (isso é "responsabilidade do host", separado da lógica de jogo) e testar em cenários de pause/resume/perda de foco de áudio.

## 6. Cache de shader/pipeline — falta virar produto
**Falta:** hoje o warm-up de shader (o que dá os ~29 fps estáveis) é feito por uma ferramenta de linha de comando isolada (`triaevum_nri_pipeline_prepare`) rodada manualmente via ADB, não pelo próprio app.
**Como fazer:**
1. Ligar a instalação Android ao mesmo job de preparação compartilhado (in-process), com progresso/cancelamento — hoje é só uma prova via ADB, não uma UI de instalador real.
2. Pré-compilar os "pass shaders" comuns dentro do próprio módulo do APK antes do gameplay começar (hoje o primeiro uso compila 20 pass shaders na hora, e qualquer rebuild do módulo invalida esse cache porque o fingerprint do compilador muda).
3. Medir separadamente criação de pipeline residual vs. espera de frame (zero chamada ao shaderc não significa zero criação de pipeline pelo driver).

## 7. Performance / gameplay em si
**Falta:** só a intro/tela de título foi qualificada (~29-30 fps nativos no Adreno 830, com stalls ocasionais). Gameplay real ainda não foi testado.
**Como fazer:** depois que input estiver conectado (item 1), rodar sessões de jogo reais medindo `NativeFramePhaseTiming` (host/GUI, input, frame start do renderer) em vez de só a intro, separando espera de aquisição/fence/present do trabalho de CPU por draw — é a mesma metodologia (simpleperf + JSON de timing) já usada nos testes de intro.

## 8. Release
**Falta:** o catálogo de releases deliberadamente ainda não anuncia Android — não é esquecimento, é decisão consciente até os itens acima fecharem.
**Como fazer:** só depois que 1–7 estiverem qualificados em device real é que faz sentido gerar um `release-manifest.json`/pacote Android seguindo o mesmo modelo de "corresponding source" que já usam pro Windows.

Se quiser, posso detalhar o código de algum desses pontos (por exemplo o `Native3dsInputTarget` que falta implementar, ou a lógica de globais estáticos do item 3) olhando os arquivos correspondentes no repo.
