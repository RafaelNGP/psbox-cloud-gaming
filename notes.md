<!-- LANG:EN -->
# PSBox Cloud Gaming v1.6.9

## 🎯 Overhauled DualSense Gyro: Natural & Precise Aiming
- **Effortless Micro-Adjustments for Shooters**: Gyro aiming has been completely re-engineered for pinpoint precision on subtle wrist movements. No more needing exaggerated physical motions to register a small turn — fine aiming in games like *Fortnite* is now instant, smooth, and 1:1 responsive.
- **Natural Player Space Rotation**: Horizontal aim strictly tracks room gravity and your physical body posture instead of controller tilt. Tilting your wrists or resting the controller on your lap keeps your aim fully aligned and natural.
- **Virtual Steering Wheel with Auto-Centering**: Turn your DualSense into a real racing wheel! Tilting the gamepad turns your vehicle naturally, auto-centering when you release the triggers. Includes 4 driving presets: Casual, Normal, Aggressive, and Drift.
- **Quick In-Game Calibration**: Fine-tune your horizontal/vertical sensitivity and wheel modes directly from the in-game stream menu with instant visual feedback.

## ⚡ Higher Performance & Lower Latency on PS5
- **Zero-Copy Direct Rendering**: Cloud video is now fed straight to the screen via the PS5 GPU without taxing the CPU, reducing display latency for snappier response times.
- **Memory Leak Elimination**: The video decoder safely recycles memory buffers when switching games or toggling stream settings, preventing unnecessary memory build-up.
- **Stable Video Quality Readings**: Video quality telemetry (QP) now shows clean, real-time integer numbers in both the stream overlay and menu.

## 👤 Per-User Independent Profiles on PS5
- **Your Settings, Your Way**: The app now saves independent settings files for each user account on your PS5. Your gyro sensitivities, button mappings, and bitrate limits won't affect other profiles on the console.

## 🏆 Instant Trophy Delta Synchronization
- **No More Waiting on Game Library**: Trophy checks now instantly skip games that are already 100% complete or unchanged. Scanning a catalog of hundreds of games dropped from nearly a minute to just ~1 second.
- **Full User Control**: Easily toggle background trophy sync on or off whenever you want.

## 🛠️ PS4 Fixes & Visual Polish
- **OneFamily Gamepad Fix (#369)**: Full input recognition and button parity restored for OneFamily controllers configured in options 2, 3, and 4 (tested in *Remnant II*).
- **Keyboard & Mouse Protection on PS4 (#379)**: Fixed a regression where plugging in USB gamepads detached keyboards and mice from the kernel driver, restoring full compatibility with `libSceKeyboard` and `libSceMouse`.
- **Polished In-Game Menu**: Centered shoulder button badges (L1/R1) across tabs and clean, non-intrusive animated toasts.

---

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.6.9.zip` | Official standalone ZIP package for PlayStation 5 (manual or homebrew manager installation). |
| `PSBoxCloudGaming[PS5]-v1.6.9.zip.sig` | ECDSA digital signature for verifying the PS5 standalone package. |
| `PSBoxCloudGaming[PS5]-v1.6.9.zip.sha256` | SHA256 checksum for the PS5 standalone package. |
| `PPSA99810.zip` | Canonical PS5 package required by the in-app auto-updater. |
| `PPSA99810.zip.sig` | Cryptographic signature for the canonical PS5 package (verified by in-app updater). |
| `PPSA99810.zip.sha256` | SHA256 checksum for the canonical PS5 package. |
| `PSBoxCloudGaming[PS4]-v1.6.9.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.6.9.pkg.sha256` | SHA256 checksum for the PS4 package. |
| `PSBoxCloudGaming[Linux]-v1.6.9.AppImage` | Universal standalone executable for Linux distributions and Steam Deck (Desktop & Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.6.9.AppImage.sha256` | SHA256 checksum for the Linux AppImage. |

### 🚀 How to Install

- **PlayStation 5**: Download `PSBoxCloudGaming[PS5]-v1.6.9.zip`, extract to the root of an exFAT USB drive (`/data/homebrew/PPSA99810/`), or install via Itemzflow / Homebrew Store. If previously installed, update directly from the in-app settings menu.
- **PlayStation 4**: Install `PSBoxCloudGaming[PS4]-v1.6.9.pkg` via GoldHEN Package Installer.
- **Linux / Steam Deck**: Download `PSBoxCloudGaming[Linux]-v1.6.9.AppImage`, make it executable (`chmod +x`), and launch in Desktop Mode or add as a non-Steam game.

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.6.9

## 🎯 Giroscópio DualSense Reformulado: Mira Natural e Precisa
- **Micro-ajustes Perfeitos para Jogos de Tiro**: A mira por giroscópio foi totalmente reconstruída para responder com precisão cirúrgica a pequenos movimentos das mãos. Chega de precisar girar o controle com força para a mira finalmente se mover — ajustes finos em jogos como *Fortnite* agora respondem de forma imediata, suave e 1:1.
- **Espaço do Jogador Natural (Player Space)**: A rotação horizontal agora acompanha a gravidade e o movimento real do jogador, em vez de depender da inclinação do controle. Girar os punhos ou jogar com o controle relaxado no colo mantém a mira sempre alinhada e intuitiva.
- **Modo Volante Virtual com Retorno Automático**: Transforme seu DualSense em um volante de corrida! Ao inclinar o controle para os lados, o jogo responde como um volante real, centralizando sozinho assim que você soltar os gatilhos. Conta com 4 estilos de direção: Casual, Normal, Agressivo e Drift.
- **Novo Menu Rápido de Calibração**: Ajuste a sensibilidade horizontal, vertical e o modo volante diretamente pelo menu in-game da transmissão com resposta visual imediata.

## ⚡ Mais Desempenho e Menos Latência no PS5
- **Renderização Direta Zero-Copy**: O vídeo da nuvem agora é transferido diretamente para a tela pela placa de vídeo do PS5, sem sobrecarregar o processador e reduzindo a latência de imagem para respostas ainda mais rápidas.
- **Correção de Vazamento de Memória**: O decodificador de vídeo agora reutiliza a memória com segurança, evitando consumo excessivo ao trocar de jogo ou alternar configurações durante a partida.
- **Qualidade de Vídeo em Tempo Real**: Indicador de qualidade (QP) agora exibe números claros e estáveis no menu e nas estatísticas da tela.

## 👤 Perfis e Configurações Independentes por Usuário
- **Cada Jogador com Suas Preferências**: O aplicativo agora salva configurações separadas para cada conta de usuário do PS5. Suas sensibilidades de giroscópio, limites de bitrate e botões personalizados não afetam os outros perfis do console.

## 🏆 Sincronização Instantânea de Troféus
- **Fim da Espera na Biblioteca**: A verificação de conquistas agora pula instantaneamente os jogos já concluídos ou sem alterações. Sincronizar centenas de jogos da sua conta Xbox passou de quase um minuto para apenas 1 segundo.
- **Controle Total**: Opção de ativar ou pausar a sincronização de troféus quando preferir.

## 🛠️ Correções no PS4 e Polimento Visual
- **Correção de Controles OneFamily (#369)**: Reconhecimento de botões e comandos restaurado nas opções 2, 3 e 4 do driver OneFamily no PS4 (resolvido em jogos como *Remnant II*).
- **Proteção de Teclado e Mouse no PS4 (#379)**: Corrigida a regressão no PS4 onde periféricos USB eram desconectados do sistema operacional, restaurando a compatibilidade nativa com `libSceKeyboard` e `libSceMouse`.
- **Menu In-Game Mais Bonito**: Botões L1 e R1 perfeitamente centralizados nas abas do menu de transmissão e notificações do app mais limpas e integradas.

---

### 📦 Arquivos Anexados (Português)

| Nome do Arquivo | Descrição |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.6.9.zip` | Pacote ZIP oficial standalone para PlayStation 5 (instalação manual ou via gerenciadores homebrew). |
| `PSBoxCloudGaming[PS5]-v1.6.9.zip.sig` | Assinatura digital ECDSA para validação do pacote de PS5. |
| `PSBoxCloudGaming[PS5]-v1.6.9.zip.sha256` | Checksum SHA256 do pacote de PS5. |
| `PPSA99810.zip` | Pacote canônico de PS5 obrigatório para o atualizador automático in-app. |
| `PPSA99810.zip.sig` | Assinatura canônica obrigatória para validação do in-app updater. |
| `PPSA99810.zip.sha256` | Checksum SHA256 do pacote canônico de PS5. |
| `PSBoxCloudGaming[PS4]-v1.6.9.pkg` | Pacote FPKG instalável para PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.6.9.pkg.sha256` | Checksum SHA256 do pacote de PS4. |
| `PSBoxCloudGaming[Linux]-v1.6.9.AppImage` | Pacote executável universal para distribuições Linux e Steam Deck (Desktop e Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.6.9.AppImage.sha256` | Checksum SHA256 do pacote AppImage Linux. |

### 🚀 Como Instalar

- **PlayStation 5**: Baixe o `PSBoxCloudGaming[PS5]-v1.6.9.zip`, extraia o conteúdo para a raiz de um pendrive USB em exFAT (`/data/homebrew/PPSA99810/`) ou instale pelo Itemzflow / Store Homebrew. Se já possui o app instalado, a atualização pode ser feita diretamente pelo menu interno do aplicativo.
- **PlayStation 4**: Instale o arquivo `PSBoxCloudGaming[PS4]-v1.6.9.pkg` através do GoldHEN Package Installer.
- **Linux / Steam Deck**: Baixe o `PSBoxCloudGaming[Linux]-v1.6.9.AppImage`, dê permissão de execução (`chmod +x`) e inicie no modo Desktop ou adicione como jogo não-Steam.
