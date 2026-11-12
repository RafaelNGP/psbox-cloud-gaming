# Roadmap — PSBox Cloud Gaming

Planejamento e visão de futuro do projeto **PSBox Cloud Gaming**, priorizado estritamente pelo que **ainda falta** e está **em andamento** para o ecossistema PlayStation (PS5 e PS4).

Status: **Em andamento (in progress / next)**, **Planejado (planned)**, **Futuro (later)**.

> **Última atualização:** 09/11/2026.

---

## 🚀 Em Andamento & Alta Prioridade (In Progress / Next)

### 1. Otimização de Performance da Decodificação HW no PS5 (VDEC2) — *in progress*
- **Sintoma & Meta**:
  - O decodificador HW (`libSceVideodec2`) está estável e com menor latência glass-to-glass (7.9 ms), mas o tempo por frame medido no overlay (`decodeMs`) fica em **3.3 ~ 3.8 ms** (QP 24), enquanto a CPU em software atinge **2.6 ~ 2.8 ms** (QP 25).
  - Meta: Reduzir o tempo de decodificação HW para **< 2.5 ms**, superando a CPU.
- **Diretrizes e Investigação**:
  - Documentação detalhada em [`HANDOFF_HW_DECODING.md`](./HANDOFF_HW_DECODING.md).
  - Investigação do custo síncrono de `sceVideodec2Flush`, double-buffering do `frameBuffer_`, atributos de coerência de memória direta (`directMemory`) e mitigação de overhead do parser de QP.

### 2. Expansão do Suporte a Controles USB no PS5 — *next*
- Suporte cabeado ao **DualShock 4 USB**.
- Suporte ao **Xbox 360 com receptor sem fio** e **Adaptador Sem Fio Xbox** (protocolos dedicados).
- Suporte a **8BitDo em D-Input**, fightsticks e controles HID genéricos via descriptor parsing.
- Vibração (rumble) nos controles USB cabeados no PS5.

---

## 📋 Planejado (Planned)

### 1. Sincronização de Troféus Nativos no PlayStation 4 — *planned*
- Construção de pacotes `TROPHY.TRP` (com `TROPCONF.SFM` e ícones) sem criptografia para ambiente GoldHEN/desbloqueado em `/user/home/<uid>/trophy/conf/<NPWR>/TROPHY.TRP` (arquitetura inspirada na *Atlas Trophy Tool*).
- Provisionamento aditivo direto no banco SQLite `trophy_local.db` (`tbl_trophy_title` e `tbl_trophy_flag`) preservando 100% dos troféus locais de jogos em disco/varejo.
- Metadados de catálogo no `/system_data/priv/mms/app.db` do PS4.

### 2. Pop-in Nativo do Sistema PlayStation Durante Gameplay no PS5 — *planned*
- Acionamento direto do banner oficial de notificação do sistema operacional do PS5 (`SceNotification` / canal `Trophies` com template `InteractiveToastTrophy`) durante a transmissão de jogos na nuvem, sem exigir toasts customizados do app.

### 3. Auto-Update para PlayStation 4 via GoldHEN Package Installer API — *planned*
- Download do pacote `PSBX00001.pkg` oficial diretamente pelo aplicativo para `/data/pkg/`.
- Envio de requisição HTTP local para o daemon do GoldHEN (`http://127.0.0.1:12800/api/install`) para instalação autônoma em background.

### 4. Presença Visual e Banners em Lojas Homebrew — *planned*
- Artes em alta definição (ícone 512x512, hero banners 1080p/4K e screenshots oficiais) para catálogos da **RecompStore** e **Prospero Store**.

### 5. Atalhos Diretos na Tela Inicial do PS5 (Home-Screen Forwarders) — *later*
- Criação de atalhos individuais na dashboard principal do PS5 para iniciar títulos específicos da nuvem diretamente, utilizando o formato ProsperoLight forwarder.

### 6. Jogos locais de PC Game Pass no PS5 — *exploração (research)*
- **Ideia**: listar, baixar e rodar no próprio console jogos de PC Game Pass que não estão no xCloud, apoiado em projetos de terceiros, em vez de só transmitir. Surgiu a partir da issue #405 (PUBG), mas **não resolve a #405**: PUBG é online com BattlEye, e nada neste caminho roda anti-cheat.
- **O que existe hoje (levantado em 2026-11-11, tudo alpha)**:
  - **XoDen** (`griches/xoden_ps5`, GPL-3.0, v0.1.0 de 2026-10-28): lista a biblioteca PC Game Pass, baixa dos servidores da Microsoft com a conta do usuário e cria um tile por jogo no PS5. UI/runtime derivados do Playden, downloader derivado do Xodus. Exige assinatura ativa e um PC para o login. Só LIMBO com gameplay confirmado.
  - **Xodus** (`xodus-gaming/xodus`, Rust, GPL-3.0): login, download MSIXVC e licenças no Linux; ainda sem decriptação sob demanda de executáveis, MSIXVC2 e `xgameruntime.dll` completos.
  - **Playden** (`leolobato/playden-ps5`, GPL-3.0, v0.6.0): launcher que roda a biblioteca Steam no PS5 via `prospero-win` (port do Wine), só FW 13.60, offline.
  - **MihawkOS** (`mihawk-99/PS5_MihawkOS`, GPL-3.0): "desktop" com Wine/Proton + DXVK/VKD3D no PS5 (FW 13.40/13.60).
- **Questões em aberto**: maturidade do `xgameruntime`; firmwares suportados; dezenas de GB por jogo; se vale embutir ou apenas abrir/integrar o XoDen; compatibilidade de licença (todos GPL-3.0, como o PSBox).
- **Riscos**: os pacotes são criptografados, e rodá-los fora do Windows pode configurar contorno de DRM; viola os termos da Microsoft, com risco de banimento da conta não quantificado; expectativa errada de online/anti-cheat; depender de uma pilha alpha de terceiros.
- **Primeiros passos**: acompanhar XoDen e Xodus; testar o XoDen com conta secundária (nunca a principal); só então decidir entre integrar, abrir por atalho ou descartar.

---

## 🚫 Decisões Técnicas e Recursos Descartados (Dropped)

- **Discord Rich Presence (Desktop)**: Descartado; o ecossistema Xbox Live / Game Pass já fornece presença nativa de atividade.
- **Assistente de Primeiro Uso (Onboarding)**: Descartado para manter navegação direta, rápida e sem telas forçadas.
- **Captura de Vídeo dos Últimos 30s da Partida**: Descartado; impacto desnecessário em memória e I/O de disco no console.
- **GeForce Now**: Descartado; protocolo totalmente distinto e incompatível com o propósito do aplicativo.
- **Transmissão HDR / Surround 5.1 / Frame Generation**: xCloud transmite nativamente em áudio estéreo e vídeo SDR 1080p; interpolação artificial de quadros adicionaria latência intolerável.
- **Múltiplas Contas Xbox Simultâneas**: Não suportado; simplificação de cache local e segurança de credenciais.
