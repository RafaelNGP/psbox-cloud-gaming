# Diretrizes de Desenvolvimento e Deploy no PS5 (xCloud-PS5)

Este documento estabelece regras operacionais estritas para qualquer agente de IA trabalhando no repositório **xCloud-PS5** e no ambiente do console PlayStation 5.

---

## 1. Regra Fundamental: Zero Blind Deploys

- **NUNCA solicite ao usuário que teste no console alterações de UI ou comportamento sem validação prévia.**
- **NUNCA assuma que o layout de tela está correto apenas pelo código C++.** Cálculos de clipping, safe-area, matrizes de projeção e renderização Vulkan no PS5 podem divergir do esperado na tela.
- **Toda e qualquer alteração de UI deve ser verificada visualmente pelo agente** através de screenshots capturadas pelo console antes de considerar a tarefa concluída ou entregar para o usuário.

---

## 2. Fluxo Obrigatório de Validação Autônoma de UI

Para qualquer alteração em arquivos de interface (`src/app_ui.cpp`, `src/app/autoplay.cpp`, shaders, renderizadores 2D):

1. **Parar a aplicação em execução**:
   ```bash
   PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
   ```
2. **Compilar e Empacotar**:
   ```bash
   ninja -C build-ps5 xcloud_app
   bash tools/ps5/link.sh build-ps5
   XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5
   ```
3. **Enviar a nova build ao console**:
   ```bash
   PS5_HOST=192.168.15.17 ./tools/ps5/deploy.sh build-ps5
   ```
4. **Configurar teste autônomo (`autoplay.txt`)**:
   - Enviar `/data/homebrew/PPSA99810/autoplay.txt` via FTP.
   - **Regra obrigatória para testes de UI**: Sempre utilize o prefixo `AUTOTEST` (ex: `AUTOTEST 30 gamesettingstest`, `AUTOTEST 30 librarytest`, etc.) para testes de telas/modais sem streaming. NUNCA use nomes arbitrários no primeiro argumento que possam ser interpretados como IDs de jogos pelo loop de streaming (`worker.cpp`).
   - Remover capturas `.ppm` antigas antes de iniciar.
5. **Iniciar a aplicação remotamente**:
   ```bash
   PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
   ```
6. **Coletar e Inspecionar os Resultados Visuais**:
   - Baixar os arquivos `.ppm` (`ui.ppm`, `grid1.ppm`, `grid2.ppm`, `launch.ppm`, etc.) via FTP para um diretório local temporário.
   - Converter `.ppm` para `.png` utilizando Python (`PIL.Image`).
   - Usar a ferramenta `view_file` para analisar visualmente a imagem gerada.
   - Verificar conformidade: clipping, centralização, padding, corte de texto, contraste e consistência de grade.
7. **Limpeza e Restauração de Modo Interativo**:
   - Excluir `/data/homebrew/PPSA99810/autoplay.txt` via FTP para que a aplicação não inicialize em modo autoplay caso o usuário queira usá-la interativamente.
   - Matar e reiniciar o app se for entregar o controle ao usuário:
     ```bash
     PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
     PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
     ```

### 2.3 Proibição de Testes Não-Relacionados e Invasão de Menus Arbitrários (Zero Unrelated Autotests)
- **Foco Cirúrgico no Alvo da Tarefa**: O teste autônomo via `autoplay.txt` deve acionar estritamente a tela, modal ou subsistema modificado na tarefa atual.
- **Proibição Estrita de Navegação em "Teclas do teclado" (`keystest`)**:
  - É terminantemente proibido acionar `keystest`, abrir o modal de "Teclas do teclado" ou executar passos de autoplay que naveguem pela lista de configurações até a opção de teclas, a menos que a tarefa solicitada pelo usuário seja explicitamente sobre o mapeamento de teclado.
  - Agentes NUNCA devem rodar testes genéricos de configurações por comodidade. Se a tarefa é sobre giroscópio, troféus, áudio, rede, streaming ou botões de controle, configure o alvo exato correspondente (ex: `streammenutest`, `padmaptest`, `trophysynctest`, etc.).
  - Esta prática evita perturbações visuais indesejadas na TV/monitor do usuário, perda de tempo e alterações acidentais de keymaps locais.

### 2.4 Armadilhas de Autoplay e Persistência de Configurações
- **Proteção Automática de Configurações (`guardSettingsForAutoplay`)**: Qualquer teste rodando com `autoplay.txt` faz backup das configurações (`settings.autoplay-backup.json`). Ao reiniciar sem `autoplay.txt` (modo interativo), as configurações originais do usuário são restauradas e o backup é apagado. Não assuma que alterações gravadas durante um autotest persistirão após a remoção do `autoplay.txt`.
- **Seleção de Tiles em Testes de Detalhes**: Nunca assuma que o primeiro card da tela é um jogo jogável com botão 'Jogar'. A biblioteca pode posicionar linhas de jogos gratuitos para adquirir no topo (*Faça streaming de jogos gratuitos*). Em testes de modais de jogo/detalhes, especifique o título alvo (`locktitle=` ou targeting explícito de título adquirido) para evitar abrir telas de aquisição da loja acidentalmente.

### 2.2 Ciclo Obrigatório de Homologação Humana (Human-in-the-Loop Validation)
- **Princípio Operacional: Os Agentes Testam, o Usuário Homologa**:
  - A execução e os testes autônomos por screenshots no console são etapas de validação técnica interna dos agentes, mas **a tarefa NUNCA é considerada concluída ou encerrada unilateralmente pelo agente**.
  - **Aprovação Mandatória do Usuário**: Nenhum subagente ou tarefa de desenvolvimento deve ser finalizado até que o **usuário forneça o "OK" explícito** de homologação após a verificação em tela real (TV / monitor).
  - **Manutenção de Contexto Ativo (`Warm Context`)**:
    - Enquanto o usuário não der o "OK", os subagentes responsáveis permanecem vivos com seu contexto ativo.
    - Ao receber feedback humano real (ex: "ficou muito alto", "afastar botões", "ajustar contraste"), o agente pai encaminha as correções diretamente ao mesmo subagente via mensagens (`send_message`), permitindo iterações ágeis e incrementais sem reinício do zero.
    - O agente principal permanece livre para orquestrar novos requisitos, criar novos agentes e gerenciar a esteira de desenvolvimento.

---

## 3. Higienização Mandatória do Console

- O arquivo `autoplay.txt` nunca deve permanecer no console após a finalização de testes automatizados.
- Se a aplicação estiver em execução e você precisar enviar novos arquivos, **SEMPRE mate o processo primeiro** (`kill PPSA99810`) antes de enviar o binário sobre FTP para evitar arquivos corrompidos ou locks em `eboot.bin`.

---

## 4. Preservação de Código e Prevenção de Regressões

- **NUNCA execute `git restore .` ou `git checkout -- .` de forma indiscriminada.**
- Antes de qualquer descarte ou reversão de alterações:
  - Inspecione `git diff` detalhadamente.
  - Se houver trabalho em andamento que precise ser isolado, utilize `git stash` ou crie uma branch temporária de segurança.
- Mantenha commits atômicos, focados e com mensagens claras.

### 4.1 Protocolo de Reversão de Emergência / Contexto Esgotado
- Se for solicitada a reversão para uma versão anterior ou release estável:
  1. **Isolamento Seguro**: Comite as alterações em branch temporária/feature: `git commit -am "wip: ..."` (NUNCA descarte com `git checkout -- .`).
  2. **Restauração Limpa**: Alterne para a `main` e garanta que o working tree esteja sincronizado e limpo na tag oficial (`git checkout main`).
  3. **Rebuild e Re-deploy no PS5**: Recompile, empacote e envie o binário da release oficial ao console via FTP.
  4. **Higienização Completa**: Remova `autoplay.txt` e relance o aplicativo em modo interativo foreground para o usuário.

### 4.2 Congelamento de Fluxos Estabelecidos e Modificações Cirúrgicas
- **Regra de Não-Interferência**: Qualquer alteração futura deve respeitar integralmente o fluxo já existente e consolidado. NUNCA altere comportamentos, layouts ou lógicas de módulos estáveis a menos que o usuário solicite explicitamente aquela mudança pontual.
- **Fluxos Blindados e Intocáveis (Preservação Mandatória)**:
  1. **Organização e Distribuição de Jogos**: Estantes oficiais SIGL (Novidades, Mais Populares, Free to Play, Saindo em Breve), catálogo geral e deduplicação Series X|S sobre Xbox One.
  2. **Aquisição e Compra na Loja**: Motor universal de aquisição de jogos gratuitos em 1-clique, transição imediata para "Jogar", e inserção imediata no topo da lista sem exigir <kbd>L3</kbd>.
  3. **Splash Screen e Retenção de Inputs**: Prender o usuário na tela de inicialização com inputs completamente travados até o login e sincronização inicial estarem concluídos.
  4. **Ordenação de Recentes**: Aba "Meus jogos" ordenada por recentes logo no primeiro frame do boot pelo cache persistente.
  5. **Telas e Menus de Configurações**: Ordem definida dos 9 itens principais, alinhamento visual de textos/chevrons, botão de reset protegido (5s) e sincronização bidirecional com a WebUI.
- **Escopo Estritamente Delimitado**: Desenvolva apenas o que foi solicitado na tarefa atual, sem refatorações periféricas ou "melhorias espontâneas" não autorizadas.

---

## 5. Gestão de Contexto e Uso de Subagentes

Para evitar degradação de desempenho, latência alta e manter a interação ágil e leve:

- **Delegação Mandatória de Tarefas Pesadas**:
  - **Pesquisa e Varredura de Código**: Delegar para subagentes (`research` ou `self`) tarefas que envolvam ler múltiplos arquivos, buscar padrões no codebase ou analisar grandes trechos de código/documentação.
  - **Análise de Logs Extensos**: Usar subagentes para processar ou depurar saídas longas (builds, WebRTC, traces), retornando apenas o diagnóstico e resumo executivo ao contexto principal.
  - **Inspeção de Imagens e Testes Intermediários**: Quando houver rotinas com múltiplas validações de telas ou passos intermediários de build, priorizar subagentes para evitar inflar o histórico principal com tokens de imagens e logs.
- **Saídas Enxutas no Contexto Principal**:
  - Evitar despejar comandos com saídas verbosas desnecessárias (usar filtros, flags resumidas ou subagentes).
  - O agente principal deve atuar na orquestração, decisão e interação com o usuário, preservando o contexto limpo e rápido.

---

## 6. Protocolo Obrigatório de Publicação de Releases (Padrão Tri-Plataforma Triplo)

- **Regra Fundamental: Zero Releases Incompletas e Zero Duplicatas**:
  TODA release oficial (`vX.Y.Z`) no GitHub DEVE conter OBRIGATORIAMENTE os binários funcionais para as **3 plataformas suportadas**: **PS5**, **PS4** e **Linux (AppImage)**, totalizando EXATAMENTE os **10 assets mandatórios** abaixo. É terminantemente proibido publicar uma release sem contemplar qualquer um dos 3 alvos ou deixar binários duplicados/temporários do CI (`PSBox-*-linux-x86_64.*` NUNCA devem permanecer na release final):
  1. **PlayStation 5 (Standalone Oficial)**:
     - `PSBoxCloudGaming[PS5]-vX.Y.Z.zip` (pacote ZIP oficial para instalação manual ou via gerenciadores homebrew).
     - `PSBoxCloudGaming[PS5]-vX.Y.Z.zip.sig` (assinatura digital ECDSA para verificação de integridade e autenticidade).
     - `PSBoxCloudGaming[PS5]-vX.Y.Z.zip.sha256` (checksum SHA256 do pacote de PS5).
  2. **PlayStation 5 (In-App Updater Canônico)**:
     - `PPSA99810.zip` (pacote canônico de PS5 obrigatório para o atualizador automático in-app).
     - `PPSA99810.zip.sig` (assinatura canônica obrigatória para validação do in-app updater).
     - `PPSA99810.zip.sha256` (checksum SHA256 do pacote canônico de PS5).
  3. **PlayStation 4 (FPKG)**:
     - `PSBoxCloudGaming[PS4]-vX.Y.Z.pkg` (pacote FPKG instalável para PlayStation 4, compatível com GoldHEN Package Installer).
     - `PSBoxCloudGaming[PS4]-vX.Y.Z.pkg.sha256` (checksum SHA256 do pacote de PS4).
  4. **Linux Desktop / Steam Deck (AppImage)**:
     - `PSBoxCloudGaming[Linux]-vX.Y.Z.AppImage` (pacote executável universal para distribuições Linux e Steam Deck em Desktop Mode e Game Mode).
     - `PSBoxCloudGaming[Linux]-vX.Y.Z.AppImage.sha256` (checksum SHA256 do pacote AppImage Linux).
- **Invariante dos Colchetes e Rótulos no GitHub (`#label`)**:
  - O GitHub sanitiza nomes de arquivos de release substituindo `[` e `]` por pontos `.`. Para que a interface web do GitHub exiba o nome oficial com colchetes `[]`, envie SEMPRE os arquivos utilizando a sintaxe `<arquivo>#<label>` no GitHub CLI (ex: `"PSBoxCloudGaming[[]PS5]-$tag.zip#PSBoxCloudGaming[PS5]-$tag.zip"`) ou ajuste via PATCH na API de assets (`{"label": "PSBoxCloudGaming[...]"}`).
  - Ao especificar o caminho local de arquivos com colchetes, escape o colchete de abertura no comando do `gh` CLI (`[[]PS5]`, `[[]PS4]`, `[[]Linux]`) para evitar que a biblioteca em Go interprete o nome como expressão de busca (glob).
- **Formato Bilíngue Obrigatório nas Release Notes (`notes.md`)**:
  - O arquivo `notes.md` e o corpo da release no GitHub devem SEMPRE apresentar as tabelas completas de manifesto em Inglês (`### 📦 Attached Files (English)`) e em Português (`### 📦 Arquivos Anexados (Português)`), listando os 10 arquivos e suas descrições exatas, seguidas pelas instruções detalhadas de instalação para as 3 plataformas (`### 🚀 How to Install` e `### 🚀 Como Instalar`).
- **Validação Automatizada Mandatória (`tools/release-verify.sh`)**:
  - Nenhuma release pode ser considerada pronta ou entregue sem a execução e aprovação com código 0 de:
    ```bash
    tools/release-verify.sh vX.Y.Z --verify-sigs
    ```
  - Este script audita automaticamente a presença dos 10 assets, os tamanhos mínimos, os checksums sha256, as assinaturas criptográficas e a presença das tabelas de manifesto bilíngues na release.
- **Pipeline Unificado de Publicação**:
  - Para orquestrar toda a publicação de forma à prova de falhas:
    ```bash
    tools/publish-release.sh <X.Y.Z>
    ```
  - O script constrói localmente os pacotes de PS5 e PS4, sobe a tag, cria a release no GitHub, aguarda a compilação do AppImage de Linux pela GitHub Action, assina criptograficamente o AppImage baixado, remove binários intermediários do CI e executa a auditoria final dos 10 assets.

---

## 7. Protocolo Obrigatório de Otimização de Performance (Benchmarking A/B no Console)

Para qualquer proposta de melhoria de desempenho, redução de latência ou otimização de pipeline (vídeo, áudio, GPU, rede ou entrada):

- **Proibição de Otimizações Teóricas**: Nenhuma alteração de performance deve ser aceita apenas com base em estimativas ou testes sintéticos de desktop.
- **Benchmarking Comparativo Mandatório (Original vs Modificado)**:
  1. **Linha de Base (Original / Baseline)**:
     - Executar uma sessão de teste no console PS5 sob condições fixadas (ex: 60 ou 120 segundos de gameplay ativa em jogo padrão como Fortnite).
     - Coletar as métricas de referência do log oficial: `decodeMs` (média/pico), `drawMs` (GPU), `onScreenMs` (latência de tela), quadros descartados (`skipped`/`late`), recuperação de pacotes (`lost`/`recovered`), estabilidade da fila de áudio (`queued ms`, `empty grains`, `ms cut`) e taxa de relatórios de entrada.
  2. **Medição da Versão Modificada**:
     - Compilar, empacotar e enviar a build modificada para o console PS5.
     - Executar o teste sob **exatamente as mesmas condições** (mesmo jogo, mesma cena/mapa, mesma duração de amostragem, mesma taxa de bits configurada).
  3. **Relatório Comparativo Formal**:
     - Apresentar tabela lado a lado com os números: **Original vs Modificado**.
     - Apenas aceitar a modificação se os dados comprovarem ganho real mensurável sem regressões visuais, de áudio ou de estabilidade.

---

## 8. Diretrizes de Sincronização de Conquistas e Troféus no PS5

Para qualquer funcionalidade que manipule registros, sincronização ou desbloqueio de troféus no PlayStation 5:

1. **Abordagem Focada e Incremental**:
   - NUNCA execute testes de sincronização em lote sobre o catálogo completo de centenas de jogos.
   - Sempre valide o fluxo completo em **UM único título alvo** (ex: Fortnite `PPSA01922` / `NPWR20561_00`) até comprovar a persistência no banco local do console.
2. **Pureza Visual e Sonora**:
   - É estritamente proibido emitir bipes/chimes no alto-falante do controle (`playPadSpeakerAudio`), vibrações (`input::setRumble`) ou caixas de diálogo com emojis/toasts de debug em troféus.
   - A sincronização manual deve refletir apenas a barra de progresso padrão do aplicativo.
3. **Parâmetros de Contexto do SceShellCore**:
   - Em `sceNpTrophy2CreateContext`, NUNCA passe `serviceLabel = 0`. Use sempre `1` ou `-1` (`0xffffffff`), caso contrário o kernel rejeita o registro com erro `0x80553914`.
4. **Tríade de Reconhecimento do Sistema**:
   - O PS5 OS não indexa troféus de títulos inexistentes na partição de aplicativos.
   - Para títulos puramente de nuvem, o provisionamento exige a coexistência de:
     * Metadados em `/system_data/priv/appmeta/<TitleID>/` (`param.json`, `nptitle.dat`, `trophy2/npbind.dat`).
     * Pacote e dados em `/user/trophy2/nobackup/conf/<NPWR>/TROPHY.UCP` e `/user/home/<uid>/trophy2/nobackup/data/<NPWR>/TRPTITLE.DAT`.
     * Registro do título em `/system_data/priv/mms/app.db`.
5. **Automação e Isolamento do Payload de Troféus**:
   - O payload de unjail e patch de troféus deve ser empacotado exclusivamente como `assets/PSBox_Trophies.elf` e copiado em runtime para `/data/psbox/PSBox_Trophies.elf`.
   - É terminantemente proibido referenciar, instalar ou deixar payloads na pasta de terceiros `/data/pldmgr/payloads/`, garantindo que o usuário não o apague acidentalmente.
6. **Invariante Estritamente Aditiva para Jogos Locais**:
   - NUNCA sobrescrever metadados (`appmeta`), bancos do sistema ou pacotes `TROPHY.UCP` de títulos com instalação de varejo no console (`/user/app/<PPSA>/eboot.bin`).
   - A sincronização deve apenas somar novas conquistas obtidas em nuvem ao `TRPTITLE.DAT` existente, preservando 100% dos troféus e timestamps originais.
   - Rotinas de limpeza de troféus (`removeAllTrophies`) devem ignorar e proteger títulos instalados localmente.
7. **Estabilidade de Transações SQLite no SceShellCore**:
   - Conexões de escrita com `/system_data/priv/mms/app.db` e `appinfo.db` devem SEMPRE configurar:
     * `PRAGMA journal_mode = MEMORY;` (evita falhas de criação de arquivo `.db-journal` em partições restritas);
     * `PRAGMA synchronous = OFF;`
     * `sqlite3_busy_timeout(db, 5000)` para tratar contenções com o daemon `SceShellCore`.
8. **Limite Arquitetural de 128 Troféus no PS5**:
   - O formato Sony Trophy 2 (`TRPTITLE.DAT` / `TROPHY.UCP`) limita cada conjunto a no máximo 128 troféus (`0` Platina + `1..127` base).
   - Para jogos com mais de 127 conquistas, a ordem dos troféus `1..127` deve permanecer rigorosamente estável para evitar falsos desbloqueios e desalinhamento de ícones.

---

## 9. Invariantes de Unjail, Sandbox e Ciclo de Vida do In-App Updater no PS5

Para o sistema de atualização automática in-app e manipulação de binários após unjail no PlayStation 5:

1. **Dissolução do Ponto `/app0` sob Namespace Desjaulado**:
   - Ao realizar o unjail (`jailbreak`) para sincronização nativa de troféus ou chamadas privilegiadas do kernel, o `chroot` do processo é alterado para a raiz do sistema real (`/`).
   - O ponto de montagem virtual `/app0` deixa de estar acessível no nível superior da raiz. Qualquer função de resolução de executável, validação de integridade ou reinicialização (`isInstalledCopy`, `restartApp`, `appDir`) NUNCA deve assumir que `/app0/eboot.bin` existe.
   - A resolução deve SEMPRE escanear dinamicamente `/mnt/sandbox/` para instâncias ativas do título (`/mnt/sandbox/<TITLE_ID>*/app0/eboot.bin`), comparando inodes (`st_ino`), com fallback seguro para o diretório canônico homebrew (`/data/homebrew/<TITLE_ID>/`).
2. **Protocolo de Hotfix e Substituição de Releases Publicadas**:
   - Caso um bug crítico exija a substituição emergencial de binários de uma release oficial já lançada:
     * Utilizar a flag `--force` em `tools/release.sh <VERSION> [out-dir] --force` para ignorar travas de tag pré-existente e reconstruir os pacotes de PS5 e PS4 com assinatura ECDSA e checksums válidos.
     * Atualizar a tag Git localmente e forçar envio ao repositório remoto (`git tag -fa vX.Y.Z -m "..." && git push -f origin vX.Y.Z`).
     * Enviar os binários corrigidos ao GitHub via `gh release upload vX.Y.Z ... --clobber` preservando rigorosamente a sintaxe de rótulos `#label` para evitar duplicatas e perda de colchetes.
     * Executar e obter aprovação com código 0 em `tools/release-verify.sh vX.Y.Z --verify-sigs`.


