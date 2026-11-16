<!-- LANG:EN -->
# PSBox Cloud Gaming v1.7.5

The major multi-platform expansion of PSBox: official debut of the native **Android** port (featuring adaptive touch controls and seamless handheld support for devices like the Anbernic RG 557) and the official standalone 64-bit **Windows** port, along with mobile ergonomic UI refinements and deep architectural stability.

## 🪟 Official Windows Port (Standalone 64-bit Portable Edition)
- **Native x86_64 Executable (`psbox.exe`)**: Full 64-bit native build powered by high-performance Vulkan graphics rendering and SDL3.
- **Zero-Installation Portable ZIP**: Self-contained package that runs out-of-the-box upon extraction without requiring administrator privileges, registry changes, or bloated installers.
- **Comprehensive Gamepad, Keyboard & Mouse Support**: Instant plug-and-play mapping for Xbox, PlayStation DualSense/DualShock, and generic gamepads via SDL3, plus complete keyboard and mouse navigation.
- **Native Crash Guard (SEH)**: Robust Structured Exception Handling integration providing automated crash diagnostics and reports on Windows.
- **Modern Socket & File Subsystems**: High-speed Winsock2 networking layer and UTF-8 path abstraction ensuring compatibility across non-ASCII usernames and directories.

## 🤖 Official Native Android Port
- **Vulkan Rendering Engine**: Direct NDK and SDL3 architecture offering ultra-low glass-to-glass display latency and maximum battery efficiency.
- **Hardware-Accelerated Video Decoding**: Native NDK `AMediaCodec` backend for hardware H.264 video decoding, significantly lowering thermal throttling and battery drain during extended play sessions.
- **16KB Modern Kernel Alignment**: Full 16KB memory page alignment compliant with modern Android 14, 15, and future OS kernels.
- **Seamless Lifecycle & Audio**: Native Android lifecycle handling (background stream suspension and instant resume) and low-latency audio output with microphone capture integration.

## 🎮 Built-in Handheld Controllers (Anbernic RG 557) & Universal Touchpad Overlay
- **Direct Handheld & USB Controller Support**: Zero-latency native mapping for integrated controls in dedicated Android handheld consoles (tested and verified on the **Anbernic RG 557**, Retroid Pocket, etc.) without requiring third-party keymappers.
- **Vulkan Touchpad Overlay**: Low-latency virtual controller overlay rendered directly on the GPU for smartphones and tablets.
- **Zero-Pollution Auto-Hide**: The virtual touch overlay automatically hides the moment an attached, USB, or Bluetooth controller is engaged, and wakes up immediately upon tapping the touchscreen.
- **Touch Layout Studio ("Customize controller")**: Real-time on-screen editor allowing players to reposition, resize, and tune virtual buttons and analog sticks, persisting layouts to custom JSON profiles.
- **Ergonomics & Ghost-Touch Suppression**: "Always Off" touch suppression mode for physical controller purists, vertical analog axis inversion, opacity slider, and configurable touch haptic vibration.

## 📱 Mobile UI/UX Ergonomic Refinements
- **Adaptive Mobile Typography & HUD**: PSBox brand logo, gamertag, gamerscore, and subscription badges dynamically enlarged for optimal legibility on 5.5" to 7" screens.
- **Intelligent Glyph Visibility**: Physical controller button legends (A, B, X, Y) automatically hide in pure touch mode, surfacing only when a physical controller is connected.
- **Centered Touch Settings Button**: Enlarged header gear button centered with comfortable touch hitboxes.
- **Android System Back Key / Gesture Integration**: Native Android `Back` button and swipe gestures now seamlessly dismiss menus, modals, and prompts across the app.
- **Fluid Touch Navigation**:
  - Touch exit button (`[‹]`) in Settings header.
  - Dedicated touch dismiss controls (`[✕]`, `[Back]`, `[Open in browser]`) on crash reports and QR dialogs.
  - Interactive touch buttons on the exit confirmation prompt ("Exit PSBox?").
  - Seamless scroll transitions out of the library's "All Games" grid without getting trapped.
  - Real-time cursor-follow highlighting during touch scrolling in menus.

## ⚡ Stream Menu & Settings Streamlining
- **Streamlined 15-Item In-Game Menu**: Single-scroll vertical order optimized for mobile touch and handheld displays.
- **One-Touch Microphone Toggle**: Quick mic mute/unmute control synchronized in real-time with WebRTC audio channels.
- **Scoped Instrument Options**: Guitar and drum settings moved exclusively to the dedicated Guitar tab, keeping the core stream menu clean.
- **Mobile Settings Parity**: Omitted console-only toggles on Android (legacy hardware decoding, X/O swap, DualSense adaptive triggers, and audio haptics).

## 🧩 Architecture, Security & Stability
- **Decoupled Trophy Subsystem**: PlayStation-specific kernel and database dependencies decoupled into a modular backend; desktop and mobile platforms utilize a lightweight zero-overhead stub (`trophy_off.cpp`).
- **Comprehensive Empirical Test Battery**: Over 10 automated test suites with thousands of assertions validating chaotic multitouch, hot-unplugging of controllers, and platform boundaries.
- **Full Localization**: Complete 6-language support (EN, PT-BR, ES, DE, FR, IT) across all mobile settings and touch overlays.

---

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.5.zip` | Official standalone ZIP package for PlayStation 5 (manual or homebrew manager installation). |
| `PSBoxCloudGaming[PS5]-v1.7.5.zip.sig` | ECDSA digital signature for verifying the PS5 standalone package. |
| `PSBoxCloudGaming[PS5]-v1.7.5.zip.sha256` | SHA256 checksum for the PS5 standalone package. |
| `PPSA99810.zip` | Canonical PS5 package required by the in-app auto-updater. |
| `PPSA99810.zip.sig` | Cryptographic signature for the canonical PS5 package (verified by in-app updater). |
| `PPSA99810.zip.sha256` | SHA256 checksum for the canonical PS5 package. |
| `PSBoxCloudGaming[PS4]-v1.7.5.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.5.pkg.sha256` | SHA256 checksum for the PS4 package. |
| `PSBoxCloudGaming[Linux]-v1.7.5.AppImage` | Universal standalone executable for Linux distributions and Steam Deck (Desktop & Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.7.5.AppImage.sha256` | SHA256 checksum for the Linux AppImage. |
| `PSBoxCloudGaming[Windows]-v1.7.5.zip` | Official standalone 64-bit portable package for Windows (no installation required). |
| `PSBoxCloudGaming[Windows]-v1.7.5.zip.sha256` | SHA256 checksum for the Windows portable package. |
| `PSBoxCloudGaming[Android]-v1.7.5.apk` | Official signed release APK package for Android phones, tablets, Android TV, and handhelds. |
| `PSBoxCloudGaming[Android]-v1.7.5.apk.sha256` | SHA256 checksum for the Android APK package. |

### 🚀 How to Install

- **PlayStation 5**: Update in the app (Settings → Updates), through ProsperoStore, or download `PSBoxCloudGaming[PS5]-v1.7.5.zip` and copy the `PPSA99810` folder to `/data/homebrew/`.
- **PlayStation 4**: Install `PSBoxCloudGaming[PS4]-v1.7.5.pkg` via GoldHEN Package Installer.
- **Linux / Steam Deck**: Download `PSBoxCloudGaming[Linux]-v1.7.5.AppImage`, make it executable (`chmod +x`), and launch in Desktop Mode or add as a non-Steam game.
- **Windows**: Download `PSBoxCloudGaming[Windows]-v1.7.5.zip`, extract to any directory, and run `psbox.exe`. No installation required.
- **Android**: Download `PSBoxCloudGaming[Android]-v1.7.5.apk` and install directly on your phone, tablet, Android TV, or handheld console (e.g. Anbernic RG 557) via file manager or `adb install`.

---

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.7.5

A grande expansão multiplataforma do PSBox: estreia oficial do porte nativo para **Android** (com controles por toque e suporte a portáteis como o Anbernic RG 557) e do porte oficial para **Windows** (versão portátil 64-bit), além de profundas otimizações ergonômicas de interface e estabilidade geral.

## 🪟 Porte Oficial para Windows (Versão Portátil 64-bit)
- **Executável Nativo (`psbox.exe`)**: Compilação x86_64 nativa de alto desempenho com renderização gráfica Vulkan de baixa latência e SDL3.
- **Pacote Portátil sem Instalação**: Distribuição em arquivo `.zip` autocontido que roda direto ao descompactar, sem exigir privilégios de administrador nem instaladores pesados.
- **Suporte Amplo a Controles, Teclado e Mouse**: Reconhecimento automático de controles de Xbox, PlayStation DualSense/DualShock e controles genéricos via SDL3, além de navegação completa por teclado e mouse.
- **Proteção contra Falhas Nativa (SEH)**: Módulo *Crash Guard* com Structured Exception Handling integrado para captura e diagnóstico de falhas no Windows.
- **Subsistema Portátil de Rede e Arquivos**: Comunicação de alta performance baseada em Winsock2 e abstração de sistema de arquivos com suporte nativo a caminhos UTF-8 (usuários com acentos e caracteres especiais).

## 🤖 Porte Oficial para Android
- **Renderização Vulkan Nativa**: Desenvolvido diretamente com NDK e SDL3, proporcionando gráficos com latência mínima e máxima eficiência energética.
- **Decodificação de Vídeo por Hardware**: Backend integrado com NDK `AMediaCodec` (H.264 acelerado por hardware), reduzindo drasticamente o consumo de bateria e aquecimento do aparelho durante jogatinas longas.
- **Compatibilidade Moderna de 16KB**: Binários compilados com alinhamento de página de 16KB, garantindo suporte pleno aos kernels modernos do Android 14, 15 e posteriores.
- **Ciclo de Vida e Áudio**: Gerenciamento de ciclo de vida nativo (suspensão/retomada limpa da transmissão ao alternar de app) e áudio de baixa latência com captura de microfone.

## 🎮 Controles Acoplados (Anbernic RG 557) & Touch para Qualquer Aparelho
- **Controles Físicos e Portáteis Dedicados**: Suporte e mapeamento automático com zero atraso para controles embutidos em consoles portáteis Android (validado e testado no **Anbernic RG 557**, Retroid Pocket e similares), sem necessidade de apps de terceiros.
- **TouchPad Overlay Inteligente**: Overlay de controles virtuais na tela desenhado diretamente em Vulkan com resposta tátil instantânea para celulares e tablets.
- **Alternância Automática Sem Poluição**: O overlay de toque desaparece da tela instantaneamente assim que um controle físico (acoplado, USB ou Bluetooth) é utilizado; ao tocar na tela com o dedo, os controles reaparecem de imediato.
- **Touch Layout Studio ("Personalizar controle")**: Editor visual em tempo real embutido que permite reposicionar, redimensionar e personalizar qualquer botão ou analógico, salvando as preferências em perfis JSON.
- **Ergonomia e Supressão Total**: Modo "Sempre Desligado" com supressão de toques fantasmas para quem joga 100% no controle físico, inversão do eixo vertical dos analógicos virtuais, controle de opacidade e toggle de vibração háptica (*touch haptics*).

## 📱 Modificações de Interface (UI/UX) para Android
- **HUD e Tipografia Escaláveis**: Logotipo do PSBox, gamertag, pontuação e selo de assinatura aumentados proporcionalmente para legibilidade perfeita em telas de 5.5" a 7".
- **Ocultação Inteligente de Glifos**: Dicas visuais de botões (A, B, X, Y) ficam ocultas no modo toque puro, surgindo apenas quando um controle físico está conectado.
- **Botão de Configurações Acessível**: Ícone de engrenagem centralizado no topo com área de clique ampliada para o toque dos dedos.
- **Integração Total com Botão "Voltar"**: O botão físico e o gesto de "Voltar" nativo do Android (`Back`) agora navegam e fecham todos os menus, modais e telas de confirmação.
- **Navegação por Toque Fluida**:
  - Botão de saída por toque (`[‹]`) no cabeçalho de Configurações.
  - Botões touch (`[✕]`, `[Voltar]`, `[Abrir no navegador]`) no relatório de crash/QR Code.
  - Diálogo de saída ("Deseja fechar o PSBox?") com botões responsivos ao toque.
  - Rolagem contínua desobstruída na biblioteca, permitindo sair da grade "Todos os jogos" de volta para as estantes superiores sem travamentos.
  - Cursor de seleção que acompanha o dedo durante a rolagem das opções (*cursor-follow*).

## ⚡ Otimizações do Menu In-Game e Configurações
- **Menu de Streaming em 15 Itens**: Reordenado para rolagem vertical contínua e natural em telas touch.
- **Microfone com Ação Direta**: Alternância rápida de ligar/mutar com sincronização de estado na sessão WebRTC.
- **Isolamento de Instrumentos**: Opções de guitarra e bateria movidas estritamente para a aba de Guitarra.
- **Higienização de Opções no Android**: Remoção de configurações exclusivas de console (decodificação de hardware legada, troca de X/O, gatilhos adaptativos e áudio háptico do DualSense).

## 🧩 Arquitetura, Segurança e Estabilidade Geral
- **Desacoplamento Modular de Troféus**: Código proprietário do PlayStation isolado; plataformas desktop e mobile utilizam implementação stub ultraleve (`trophy_off.cpp`) sem código morto.
- **Bateria de Testes Automatizados**: Mais de 10 novas suítes de testes cobrindo situações caóticas de multitoque e reconexões de controles.
- **Localização Completa**: Suporte multilíngue integral (PT-BR, EN, ES, DE, FR, IT) para todas as novas opções.

---

### 📦 Arquivos Anexados (Português)

| Nome do Arquivo | Descrição |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.5.zip` | Pacote ZIP oficial para PlayStation 5 (instalação manual ou por gerenciador homebrew). |
| `PSBoxCloudGaming[PS5]-v1.7.5.zip.sig` | Assinatura digital ECDSA para verificar o pacote de PS5. |
| `PSBoxCloudGaming[PS5]-v1.7.5.zip.sha256` | Checksum SHA256 do pacote de PS5. |
| `PPSA99810.zip` | Pacote canônico de PS5 usado pelo atualizador automático do app. |
| `PPSA99810.zip.sig` | Assinatura do pacote canônico de PS5 (verificada pelo atualizador do app). |
| `PPSA99810.zip.sha256` | Checksum SHA256 do pacote canônico de PS5. |
| `PSBoxCloudGaming[PS4]-v1.7.5.pkg` | Pacote FPKG instalável para PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.5.pkg.sha256` | Checksum SHA256 do pacote de PS4. |
| `PSBoxCloudGaming[Linux]-v1.7.5.AppImage` | Executável universal para distribuições Linux e Steam Deck (Modo Desktop e Modo Jogo). |
| `PSBoxCloudGaming[Linux]-v1.7.5.AppImage.sha256` | Checksum SHA256 do AppImage de Linux. |
| `PSBoxCloudGaming[Windows]-v1.7.5.zip` | Pacote ZIP oficial portátil de 64-bit para Windows (sem necessidade de instalação). |
| `PSBoxCloudGaming[Windows]-v1.7.5.zip.sha256` | Checksum SHA256 do pacote portátil de Windows. |
| `PSBoxCloudGaming[Android]-v1.7.5.apk` | Pacote APK oficial assinado para celulares, tablets, Android TV e consoles portáteis. |
| `PSBoxCloudGaming[Android]-v1.7.5.apk.sha256` | Checksum SHA256 do pacote APK Android. |

### 🚀 Como Instalar

- **PlayStation 5**: atualize pelo app (Configurações → Atualizações), pelo ProsperoStore, ou baixe `PSBoxCloudGaming[PS5]-v1.7.5.zip` e copie a pasta `PPSA99810` para `/data/homebrew/`.
- **PlayStation 4**: instale `PSBoxCloudGaming[PS4]-v1.7.5.pkg` pelo GoldHEN Package Installer.
- **Linux / Steam Deck**: baixe `PSBoxCloudGaming[Linux]-v1.7.5.AppImage`, torne executável (`chmod +x`) e abra no Modo Desktop ou adicione como jogo não-Steam.
- **Windows**: baixe `PSBoxCloudGaming[Windows]-v1.7.5.zip`, extraia para qualquer pasta e execute `psbox.exe`. Não requer instalação.
- **Android**: baixe `PSBoxCloudGaming[Android]-v1.7.5.apk` e instale diretamente no seu aparelho (celular, tablet, TV ou portátil como Anbernic RG 557) pelo gerenciador de arquivos ou via `adb install`.
