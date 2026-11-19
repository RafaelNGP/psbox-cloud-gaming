<!-- LANG:EN -->
# PSBox Cloud Gaming v1.7.7

This release delivers **1440p60 zero-copy Xbox Remote Play (`xHome`) on PlayStation 5**, **OAuth v2.0 prefilled QR sign-in**, major **Android TV / Nvidia Shield Pro stability fixes**, **32-bit `armeabi-v7a` Android TV Box support**, and **native D3D11VA hardware decoding priority on Windows**.

### 🎮 PlayStation 5 & Xbox Remote Play (`xHome`)
- **Native 1440p60 Zero-Copy Hardware Decoding**: Fixes the *"Starting the stream"* freeze when an Xbox Series X is set to 4K120. PSBox now parses SPS `level_idc`, reference frames, and coded dimensions, dynamically reconfiguring `libSceVideodec2` up to **H.264 Level 5.2** and expanding PS5 zero-copy video slots from 4 MiB to **8 MiB** (`2560×1440` directly in hardware, zero CPU copy).
- **Hardware Decoder Recovery (`sceVideodec2Reset`)**: Automatically resets the PS5 hardware decoder on fatal stream/packet errors (`0x811d0300`) so playback recovers cleanly on the next keyframe instead of stalling permanently.
- **Fast Pre-First-Frame Fallback**: Lowered the pre-first-frame hardware error threshold so an incompatible stream never stalls indefinitely on *"Starting the stream"*.

### 🔐 Faster Sign-In (All Platforms)
- **OAuth v2.0 + Prefilled `?otc=CODE` QR & Browser Links**: New sign-ins now pre-fill the device code automatically when scanning the QR code or clicking *"Open browser to sign in"*, while preserving full backward compatibility with already signed-in legacy accounts.
- **Android TV QR Code**: Non-touch Android TV devices (Nvidia Shield, Google TV, TV Boxes) now display the prefilled `?otc=CODE` QR code on the sign-in screen instead of a mobile browser button.

### 🤖 Android, Android TV, Nvidia Shield Pro & 32-bit TV Boxes
- **Nvidia Shield Pro / Android TV Stream Crash Fixes**:
  - Hardened `AMediaCodec` ByteBuffer decoding with full bounds checking on all YUV planes, proper codec recreation on configure retries, `OUTPUT_BUFFERS_CHANGED` handling for legacy OMX decoders, and `drawYuv420` pointer/stride guards.
  - Prevented `RECORD_AUDIO` from triggering a blocking runtime permission dialog on the stream worker thread when microphone permission is not granted.
  - Skipped CPU Vulkan software devices (e.g., SwiftShader) and guarded null `vkMapMemory` pointers so devices without hardware Vulkan 1.3 fall back cleanly to `SDL_Renderer` (GLES).
  - Fixed JNI `MainActivity` lookup from native worker threads via `SDL_GetAndroidActivity()`, enabling `setLowLatencyWifi(true)` and haptics reliably during gameplay.
- **32-bit `armeabi-v7a` Support**: Added `armeabi-v7a` native libraries alongside `arm64-v8a` and `x86_64` in the universal APK (fixing `INSTALL_FAILED_NO_MATCHING_ABIS` on 32-bit Android TV Boxes such as Skyworth HP40A2/HP40A), while preserving 16 KB page alignment on 64-bit ABIs and zero `TEXTREL` on 32-bit.
- **Auto-Hide Touch Controls on TV**: Detects `FEATURE_TELEVISION` / `FEATURE_LEANBACK` / `FEATURE_TOUCHSCREEN` so virtual touch buttons and the top-left pull-tab never appear on Android TV, and auto-suppresses touch overlays on phones/tablets when a physical controller is already connected.
- **Smoother Android Decoding & In-Game Menu**: Adaptive `AMediaCodec` output timeout (improving frame pacing and reducing latency on 4–9 ms SoCs such as Dimensity 8300 / Anbernic RG 557), D-pad/stick scrolling in the Android in-game menu, and paused `crash_guard` watchdog while the Android app is suspended in the background (#507).

### 🪟 Windows Hardware Decoding
- **Native D3D11VA First**: Prioritized `D3D11VA` ahead of `Vulkan Video` in FFmpeg hardware decoding so Windows PCs without Vulkan or without `VK_KHR_video_decode_h264` automatically get native Direct3D 11 hardware decoding out of the box.

---

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.7.zip` | Official standalone ZIP package for PlayStation 5 (manual install or homebrew manager). |
| `PSBoxCloudGaming[PS5]-v1.7.7.zip.sig` | ECDSA digital signature to verify the PS5 package authenticity. |
| `PSBoxCloudGaming[PS5]-v1.7.7.zip.sha256` | SHA256 checksum of the standalone PS5 package. |
| `PPSA99810.zip` | Canonical PS5 package required by the in-app automatic updater. |
| `PPSA99810.zip.sig` | Canonical PS5 signature required by the in-app automatic updater. |
| `PPSA99810.zip.sha256` | SHA256 checksum of the canonical PS5 package. |
| `PSBoxCloudGaming[PS4]-v1.7.7.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.7.pkg.sha256` | SHA256 checksum of the PS4 package. |
| `PSBoxCloudGaming[Linux]-v1.7.7.AppImage` | Universal executable for Linux distributions and Steam Deck (Desktop and Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.7.7.AppImage.sha256` | SHA256 checksum of the Linux AppImage package. |
| `PSBoxCloudGaming[Windows]-v1.7.7.zip` | Official portable 64-bit ZIP package for Windows (no installation needed). |
| `PSBoxCloudGaming[Windows]-v1.7.7.zip.sha256` | SHA256 checksum of the Windows portable package. |
| `PSBoxCloudGaming[Android]-v1.7.7.apk` | Official signed universal APK (`arm64-v8a`, `armeabi-v7a`, `x86_64`) for Android phones, tablets, Android TV, and handhelds. |
| `PSBoxCloudGaming[Android]-v1.7.7.apk.sha256` | SHA256 checksum of the Android APK package. |

### 🚀 How to Install

- **PlayStation 5**: update directly from the app (Settings → Updates), via ProsperoStore, or download `PSBoxCloudGaming[PS5]-v1.7.7.zip` and copy the `PPSA99810` folder to `/data/homebrew/`.
- **PlayStation 4**: install `PSBoxCloudGaming[PS4]-v1.7.7.pkg` using GoldHEN Package Installer.
- **Linux / Steam Deck**: download `PSBoxCloudGaming[Linux]-v1.7.7.AppImage`, make it executable (`chmod +x`), and launch in Desktop Mode or add as a Non-Steam Game.
- **Windows**: download `PSBoxCloudGaming[Windows]-v1.7.7.zip`, extract anywhere, and launch `psbox.exe`. No installation required.
- **Android / Android TV**: download `PSBoxCloudGaming[Android]-v1.7.7.apk` and install directly on your device (phone, tablet, Nvidia Shield, Android TV Box, or handheld such as Anbernic RG 557) via file manager or `adb install`.

<!-- LANG:PT -->
<!-- LANG:PT-BR -->
# PSBox Cloud Gaming v1.7.7

Esta atualização traz **Xbox Remote Play (`xHome`) em 1440p60 zero-copy no PlayStation 5**, **QR Code de login já preenchido (OAuth v2.0)**, correções importantes de **estabilidade no Android TV / Nvidia Shield Pro**, suporte a **TV Boxes Android 32-bit (`armeabi-v7a`)** e prioridade para **decodificação hardware D3D11VA nativa no Windows**.

### 🎮 PlayStation 5 e Xbox Remote Play (`xHome`)
- **Decodificação Hardware 1440p60 Zero-Copy**: Corrige o congelamento em *"Iniciando a transmissão"* quando o Xbox Series X está configurado em 4K120. O PSBox agora lê `level_idc`, frames de referência e dimensões do SPS H.264, reconfigurando o `libSceVideodec2` dinamicamente até **H.264 Level 5.2** e ampliando os video slots zero-copy do PS5 de 4 MiB para **8 MiB** (`2560×1440` direto em hardware, sem cópia de CPU).
- **Recuperação Automática do Decoder (`sceVideodec2Reset`)**: Reseta automaticamente o decoder hardware do PS5 em erros fatais de pacote/stream (`0x811d0300`), recuperando a imagem no próximo keyframe em vez de congelar permanentemente.
- **Fallback Rápido Pré-Primeiro-Frame**: Reduzido o limite de erros antes do primeiro frame para evitar travamentos indefinidos na tela de carregamento.

### 🔐 Login Mais Rápido (Todas as Plataformas)
- **OAuth v2.0 + `?otc=CODE` Preenchido no QR Code e Navegador**: Novos logins agora já preenchem o código automaticamente ao escanear o QR Code ou clicar em *"Abrir navegador para entrar"*, mantendo 100% de compatibilidade com contas já conectadas.
- **QR Code no Android TV**: Dispositivos Android TV sem tela sensível ao toque (Nvidia Shield, Google TV, TV Boxes) agora exibem o QR Code preenchido (`?otc=CODE`) na tela de login em vez do botão de navegador mobile.

### 🤖 Android, Android TV, Nvidia Shield Pro e TV Boxes 32-bit
- **Correções de Crash na Nvidia Shield Pro / Android TV**:
  - Blindagem completa da decodificação `AMediaCodec` em modo ByteBuffer com validação de limites de todos os planos YUV, recriação limpa do codec em retries, tratamento de `OUTPUT_BUFFERS_CHANGED` para decoders OMX legados e proteções de ponteiro/stride em `drawYuv420`.
  - Evitado que `RECORD_AUDIO` abra diálogo bloqueante de permissão na thread de streaming quando a permissão de microfone não foi concedida.
  - Ignorados dispositivos Vulkan de CPU (como SwiftShader) e validado ponteiro nulo em `vkMapMemory`, garantindo fallback limpo para `SDL_Renderer` (GLES) sem crashes.
  - Corrigida a resolução JNI de `MainActivity` em threads de trabalho via `SDL_GetAndroidActivity()`, fazendo o `setLowLatencyWifi(true)` e hápticos funcionarem corretamente durante o jogo.
- **Suporte a 32-bit (`armeabi-v7a`)**: Adicionada a ABI `armeabi-v7a` junto a `arm64-v8a` e `x86_64` no APK universal (resolvendo `INSTALL_FAILED_NO_MATCHING_ABIS` em TV Boxes 32-bit como Skyworth HP40A2/HP40A), mantendo alinhamento de 16 KB em 64-bit e zero `TEXTREL` em 32-bit.
- **Ocultamento Automático de Controles Touch em TVs**: Detecta `FEATURE_TELEVISION` / `FEATURE_LEANBACK` / `FEATURE_TOUCHSCREEN` para nunca exibir botões touch nem a aba lateral no Android TV, além de suprimir automaticamente o touch em celulares/tablets quando já há um controle físico conectado.
- **Mais Fluidez no Android e Menu In-Game**: Timeout adaptativo no `AMediaCodec` (melhorando a cadência de frames e reduzindo latência em SoCs de 4–9 ms como Dimensity 8300 / Anbernic RG 557), rolagem com D-Pad/analógico no menu in-game do Android e pausa do watchdog `crash_guard` enquanto o app Android está em segundo plano (#507).

### 🪟 Decodificação Hardware no Windows
- **Prioridade para D3D11VA Nativo**: `D3D11VA` agora é tentado antes de `Vulkan Video` na decodificação hardware do FFmpeg, garantindo aceleração de vídeo nativa via Direct3D 11 mesmo em PCs Windows sem Vulkan ou sem suporte a `VK_KHR_video_decode_h264`.

---

### 📦 Arquivos Anexados (Português)

| Nome do Arquivo | Descrição |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.7.zip` | Pacote ZIP oficial para PlayStation 5 (instalação manual ou por gerenciador homebrew). |
| `PSBoxCloudGaming[PS5]-v1.7.7.zip.sig` | Assinatura digital ECDSA para verificar o pacote de PS5. |
| `PSBoxCloudGaming[PS5]-v1.7.7.zip.sha256` | Checksum SHA256 do pacote de PS5. |
| `PPSA99810.zip` | Pacote canônico de PS5 usado pelo atualizador automático do app. |
| `PPSA99810.zip.sig` | Assinatura do pacote canônico de PS5 (verificada pelo atualizador do app). |
| `PPSA99810.zip.sha256` | Checksum SHA256 do pacote canônico de PS5. |
| `PSBoxCloudGaming[PS4]-v1.7.7.pkg` | Pacote FPKG instalável para PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.7.pkg.sha256` | Checksum SHA256 do pacote de PS4. |
| `PSBoxCloudGaming[Linux]-v1.7.7.AppImage` | Executável universal para distribuições Linux e Steam Deck (Modo Desktop e Modo Jogo). |
| `PSBoxCloudGaming[Linux]-v1.7.7.AppImage.sha256` | Checksum SHA256 do AppImage de Linux. |
| `PSBoxCloudGaming[Windows]-v1.7.7.zip` | Pacote ZIP oficial portátil de 64-bit para Windows (sem necessidade de instalação). |
| `PSBoxCloudGaming[Windows]-v1.7.7.zip.sha256` | Checksum SHA256 do pacote portátil de Windows. |
| `PSBoxCloudGaming[Android]-v1.7.7.apk` | Pacote APK oficial universal assinado (`arm64-v8a`, `armeabi-v7a`, `x86_64`) para celulares, tablets, Android TV e portáteis. |
| `PSBoxCloudGaming[Android]-v1.7.7.apk.sha256` | Checksum SHA256 do pacote APK Android. |

### 🚀 Como Instalar

- **PlayStation 5**: atualize pelo app (Configurações → Atualizações), pelo ProsperoStore, ou baixe `PSBoxCloudGaming[PS5]-v1.7.7.zip` e copie a pasta `PPSA99810` para `/data/homebrew/`.
- **PlayStation 4**: instale `PSBoxCloudGaming[PS4]-v1.7.7.pkg` pelo GoldHEN Package Installer.
- **Linux / Steam Deck**: baixe `PSBoxCloudGaming[Linux]-v1.7.7.AppImage`, torne executável (`chmod +x`) e abra no Modo Desktop ou adicione como jogo não-Steam.
- **Windows**: baixe `PSBoxCloudGaming[Windows]-v1.7.7.zip`, extraia para qualquer pasta e execute `psbox.exe`. Não requer instalação.
- **Android / Android TV**: baixe `PSBoxCloudGaming[Android]-v1.7.7.apk` e instale diretamente no seu aparelho (celular, tablet, Nvidia Shield, TV Box ou portátil como Anbernic RG 557) pelo gerenciador de arquivos ou via `adb install`.
