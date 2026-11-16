<!-- LANG:EN -->
# PSBox Cloud Gaming v1.7.6 (Hotfix)

This hotfix release strictly isolates the virtual touch controller overlay to Android, ensuring desktop (Linux, Windows) and console (PlayStation 5, PlayStation 4) platforms remain clean and completely free of on-screen touch elements. It also delivers the major multi-platform expansion from v1.7.5: official debut of the native **Android** port and the standalone 64-bit **Windows** port, along with mobile ergonomic UI refinements and deep architectural stability.

### 🛡️ Hotfix Highlights
- **Exclusive Touch Overlay on Android**: Resolved an issue where virtual touch overlay buttons and the top pull-tab could be displayed on desktop and console builds when playing without a gamepad. The touch overlay is now exclusively compiled and rendered on Android.

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
- **Decoupled Trophy Subsystem**: PlayStation-specific kernel and database calls cleanly isolated; desktop and mobile platforms link against a stub implementation (`trophy_off.cpp`) with zero dead code.
- **Automated Multi-Touch Test Suites**: Over 10 automated test suites covering chaotic multi-finger inputs, touch cancellation, and simultaneous physical pad switches.
- **Full Localization**: Comprehensive multi-language support (PT-BR, EN, ES, DE, FR, IT) across all new mobile and audio controls.

---

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.6.zip` | Official standalone ZIP package for PlayStation 5 (manual install or homebrew manager). |
| `PSBoxCloudGaming[PS5]-v1.7.6.zip.sig` | ECDSA digital signature to verify the PS5 package authenticity. |
| `PSBoxCloudGaming[PS5]-v1.7.6.zip.sha256` | SHA256 checksum of the standalone PS5 package. |
| `PPSA99810.zip` | Canonical PS5 package required by the in-app automatic updater. |
| `PPSA99810.zip.sig` | Canonical PS5 signature required by the in-app automatic updater. |
| `PPSA99810.zip.sha256` | SHA256 checksum of the canonical PS5 package. |
| `PSBoxCloudGaming[PS4]-v1.7.6.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.6.pkg.sha256` | SHA256 checksum of the PS4 package. |
| `PSBoxCloudGaming[Linux]-v1.7.6.AppImage` | Universal executable for Linux distributions and Steam Deck (Desktop and Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.7.6.AppImage.sha256` | SHA256 checksum of the Linux AppImage package. |
| `PSBoxCloudGaming[Windows]-v1.7.6.zip` | Official portable 64-bit ZIP package for Windows (no installation needed). |
| `PSBoxCloudGaming[Windows]-v1.7.6.zip.sha256` | SHA256 checksum of the Windows portable package. |
| `PSBoxCloudGaming[Android]-v1.7.6.apk` | Official signed APK package for Android phones, tablets, Android TV, and handhelds. |
| `PSBoxCloudGaming[Android]-v1.7.6.apk.sha256` | SHA256 checksum of the Android APK package. |

### 🚀 How to Install

- **PlayStation 5**: update directly from the app (Settings → Updates), via ProsperoStore, or download `PSBoxCloudGaming[PS5]-v1.7.6.zip` and copy the `PPSA99810` folder to `/data/homebrew/`.
- **PlayStation 4**: install `PSBoxCloudGaming[PS4]-v1.7.6.pkg` using GoldHEN Package Installer.
- **Linux / Steam Deck**: download `PSBoxCloudGaming[Linux]-v1.7.6.AppImage`, make it executable (`chmod +x`), and launch in Desktop Mode or add as a Non-Steam Game.
- **Windows**: download `PSBoxCloudGaming[Windows]-v1.7.6.zip`, extract anywhere, and launch `psbox.exe`. No installation required.
- **Android**: download `PSBoxCloudGaming[Android]-v1.7.6.apk` and install directly on your device (phone, tablet, TV, or handheld such as Anbernic RG 557) via file manager or `adb install`.

<!-- LANG:PT -->
<!-- LANG:PT-BR -->
# PSBox Cloud Gaming v1.7.6 (Hotfix)

Esta atualização de hotfix isola estritamente o overlay de controles virtuais de toque para o Android, garantindo que as plataformas de desktop (Linux, Windows) e consoles (PlayStation 5, PlayStation 4) permaneçam limpas e completamente livres de botões de toque na tela. Traz também toda a expansão multiplataforma oficial: estreia nativa no **Android** (com controles touch adaptativos e suporte nativo a portáteis como o Anbernic RG 557) e o porte portátil oficial de 64-bit para **Windows**, além de ergonomia aprimorada e alta estabilidade de sistema.

### 🛡️ Destaques do Hotfix
- **Overlay Touch Exclusivo no Android**: Corrigido um problema onde os botões virtuais de toque e a aba pull-tab podiam surgir nas builds de desktop e console ao jogar sem controle físico. O overlay agora é compilado e renderizado exclusivamente no Android.

## 🪟 Porte Oficial para Windows (Edição Portátil 64-bit Standalone)
- **Executável Nativo x86_64 (`psbox.exe`)**: Compilação nativa de alta performance baseada em renderização gráfica Vulkan e SDL3.
- **Pacote Portátil Zero-Instalação**: Roda direto após descompactar o arquivo ZIP sem exigir privilégios de administrador, sem mexer no registro e sem instaladores pesados.
- **Suporte Amplo a Controles, Teclado e Mouse**: Mapeamento instantâneo para controles de Xbox, PlayStation DualSense/DualShock e joysticks genéricos via SDL3, com suporte completo a mouse e teclado.
- **Proteção Nativa Contra Falhas (SEH)**: Integração com *Structured Exception Handling* nativo do Windows para diagnósticos e relatórios automáticos em caso de travamento.
- **Sockets e Sistema de Arquivos Modernos**: Camada de rede de alta velocidade via Winsock2 e abstração de caminhos em UTF-8 com suporte completo a pastas com acentos e caracteres especiais.

## 🤖 Porte Oficial Nativo para Android
- **Motor Gráfico Vulkan**: Arquitetura direta via NDK e SDL3 entregando latência de exibição ultra-baixa e máxima eficiência energética.
- **Decodificação de Vídeo Acelerada por Hardware**: Backend nativo via NDK `AMediaCodec` para decodificação H.264 em hardware, reduzindo drasticamente o aquecimento e o consumo de bateria em sessões prolongadas.
- **Alinhamento de Kernel de 16KB**: Totalmente compatível com as novas exigências de páginas de memória de 16KB do Android 14, Android 15 e superiores.
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
| `PSBoxCloudGaming[PS5]-v1.7.6.zip` | Pacote ZIP oficial para PlayStation 5 (instalação manual ou por gerenciador homebrew). |
| `PSBoxCloudGaming[PS5]-v1.7.6.zip.sig` | Assinatura digital ECDSA para verificar o pacote de PS5. |
| `PSBoxCloudGaming[PS5]-v1.7.6.zip.sha256` | Checksum SHA256 do pacote de PS5. |
| `PPSA99810.zip` | Pacote canônico de PS5 usado pelo atualizador automático do app. |
| `PPSA99810.zip.sig` | Assinatura do pacote canônico de PS5 (verificada pelo atualizador do app). |
| `PPSA99810.zip.sha256` | Checksum SHA256 do pacote canônico de PS5. |
| `PSBoxCloudGaming[PS4]-v1.7.6.pkg` | Pacote FPKG instalável para PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.6.pkg.sha256` | Checksum SHA256 do pacote de PS4. |
| `PSBoxCloudGaming[Linux]-v1.7.6.AppImage` | Executável universal para distribuições Linux e Steam Deck (Modo Desktop e Modo Jogo). |
| `PSBoxCloudGaming[Linux]-v1.7.6.AppImage.sha256` | Checksum SHA256 do AppImage de Linux. |
| `PSBoxCloudGaming[Windows]-v1.7.6.zip` | Pacote ZIP oficial portátil de 64-bit para Windows (sem necessidade de instalação). |
| `PSBoxCloudGaming[Windows]-v1.7.6.zip.sha256` | Checksum SHA256 do pacote portátil de Windows. |
| `PSBoxCloudGaming[Android]-v1.7.6.apk` | Pacote APK oficial assinado para celulares, tablets, Android TV e consoles portáteis. |
| `PSBoxCloudGaming[Android]-v1.7.6.apk.sha256` | Checksum SHA256 do pacote APK Android. |

### 🚀 Como Instalar

- **PlayStation 5**: atualize pelo app (Configurações → Atualizações), pelo ProsperoStore, ou baixe `PSBoxCloudGaming[PS5]-v1.7.6.zip` e copie a pasta `PPSA99810` para `/data/homebrew/`.
- **PlayStation 4**: instale `PSBoxCloudGaming[PS4]-v1.7.6.pkg` pelo GoldHEN Package Installer.
- **Linux / Steam Deck**: baixe `PSBoxCloudGaming[Linux]-v1.7.6.AppImage`, torne executável (`chmod +x`) e abra no Modo Desktop ou adicione como jogo não-Steam.
- **Windows**: baixe `PSBoxCloudGaming[Windows]-v1.7.6.zip`, extraia para qualquer pasta e execute `psbox.exe`. Não requer instalação.
- **Android**: baixe `PSBoxCloudGaming[Android]-v1.7.6.apk` e instale diretamente no seu aparelho (celular, tablet, TV ou portátil como Anbernic RG 557) pelo gerenciador de arquivos ou via `adb install`.
