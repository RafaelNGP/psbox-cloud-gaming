<!-- LANG:EN -->
# PSBox Cloud Gaming v1.7.1

A reliability update: fixes for a sign-in screen without text, a saved sign-in that never asked for a new code, and a library stuck on old data, plus a reorganised WebUI.

## 🔤 The Screens Always Have Text
- **PS5: fixed the screens with no text at all** (the sign-in screen showing only the QR code and an empty box). It happened when the app was not installed in `/data/homebrew/PPSA99810`: the fonts were not found.
- **The fonts are now also built into the app**, so text is drawn even when the fonts' files are missing or damaged (an incomplete transfer, a folder out of reach).

## 🔑 Sign-in
- A saved sign-in that Microsoft refuses for good no longer loops on the error screen: the app asks for a **new code**.
- Tip: on any sign-in error screen, **hold the touchpad for 5 seconds** to sign out and get a new code.

## 📚 Library
- A page of the game list that fails for a moment (server error, timeout) is **asked again** instead of leaving the library on old data.
- When something still could not be loaded, the app says **"Library incomplete, trying again"** and refreshes by itself a little later.

## 🌐 SmartGlass WebUI
- **Mapping** has a new **Gyro** tab (response start, curve, invert horizontal / vertical), and the **DualSense** tab now holds the controller's settings: dead zones, speaker volume, trigger vibration, smart triggers and light bar.
- **Settings** reorganised, one option per line, with **Resolution** added: video decoder, resolution, upscaler, deband, FPS limit, sharpness, maximum bitrate, 120 Hz, queue chime, voice chat output, store country.
- **Current Game** is now a read-only telemetry page with many more numbers for picture and network (packet gaps, frames arriving together, skipped frames, peaks), coloured when they go past a limit.

## 🧹 Other
- Trophy sync no longer opens the saved sign-in a second time on its own.

---

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.1.zip` | Official standalone ZIP package for PlayStation 5 (manual or homebrew manager installation). |
| `PSBoxCloudGaming[PS5]-v1.7.1.zip.sig` | ECDSA digital signature for verifying the PS5 standalone package. |
| `PSBoxCloudGaming[PS5]-v1.7.1.zip.sha256` | SHA256 checksum for the PS5 standalone package. |
| `PPSA99810.zip` | Canonical PS5 package required by the in-app auto-updater. |
| `PPSA99810.zip.sig` | Cryptographic signature for the canonical PS5 package (verified by in-app updater). |
| `PPSA99810.zip.sha256` | SHA256 checksum for the canonical PS5 package. |
| `PSBoxCloudGaming[PS4]-v1.7.1.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.1.pkg.sha256` | SHA256 checksum for the PS4 package. |
| `PSBoxCloudGaming[Linux]-v1.7.1.AppImage` | Universal standalone executable for Linux distributions and Steam Deck (Desktop & Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.7.1.AppImage.sha256` | SHA256 checksum for the Linux AppImage. |

### 🚀 How to Install

- **PlayStation 5**: Update in the app (Settings → Updates), through ProsperoStore, or download `PSBoxCloudGaming[PS5]-v1.7.1.zip` and copy the `PPSA99810` folder to `/data/homebrew/`.
- **PlayStation 4**: Install `PSBoxCloudGaming[PS4]-v1.7.1.pkg` via GoldHEN Package Installer.
- **Linux / Steam Deck**: Download `PSBoxCloudGaming[Linux]-v1.7.1.AppImage`, make it executable (`chmod +x`), and launch in Desktop Mode or add as a non-Steam game.

> Coming from v1.6.9 or older: each PS5 profile signs in once; the existing sign-in goes to the first profile that opens the app.

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.7.1

Uma atualização de confiabilidade: correções para a tela de login sem texto, para o login salvo que nunca pedia um código novo e para a biblioteca presa em dados antigos, além da WebUI reorganizada.

## 🔤 As Telas Sempre Têm Texto
- **PS5: corrigidas as telas sem nenhum texto** (a tela de login mostrando só o QR code e uma caixa vazia). Acontecia quando o app não estava instalado em `/data/homebrew/PPSA99810`: as fontes não eram encontradas.
- **As fontes agora também vão dentro do app**, então o texto é desenhado mesmo quando os arquivos de fonte faltam ou estão danificados (transferência incompleta, pasta fora de alcance).

## 🔑 Login
- Um login salvo que a Microsoft recusa de vez não fica mais em loop na tela de erro: o app pede um **código novo**.
- Dica: em qualquer tela de erro de login, **segure o touchpad por 5 segundos** para sair da conta e receber um código novo.

## 📚 Biblioteca
- Uma página da lista de jogos que falha por um instante (erro do servidor, tempo esgotado) é **pedida de novo**, em vez de deixar a biblioteca com dados antigos.
- Quando algo ainda não pôde ser carregado, o app avisa **"Biblioteca incompleta, tentando de novo"** e atualiza sozinho um pouco depois.

## 🌐 SmartGlass WebUI
- **Mapeamento** ganhou a aba **Giroscópio** (início da resposta, curva, inverter horizontal / vertical), e a aba **DualSense** agora traz os ajustes do controle: zonas mortas, volume do alto-falante, vibração dos gatilhos, gatilho inteligente e barra de luz.
- **Configurações** reorganizadas, uma opção por linha, com **Resolução** adicionada: decodificador de vídeo, resolução, upscaler, deband, limitador de FPS, nitidez, bitrate máximo, 120 Hz, aviso sonoro de fila, saída do chat de voz, país da loja.
- **Jogo Atual** agora é uma página de telemetria só de leitura, com muito mais números de imagem e rede (pausas entre pacotes, quadros chegando juntos, quadros pulados, picos), coloridos quando passam de um limite.

## 🧹 Outros
- A sincronização de troféus não abre mais o login salvo uma segunda vez por conta própria.

---

### 📦 Arquivos Anexados (Português)

| Nome do Arquivo | Descrição |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.1.zip` | Pacote ZIP oficial para PlayStation 5 (instalação manual ou por gerenciador homebrew). |
| `PSBoxCloudGaming[PS5]-v1.7.1.zip.sig` | Assinatura digital ECDSA para verificar o pacote de PS5. |
| `PSBoxCloudGaming[PS5]-v1.7.1.zip.sha256` | Checksum SHA256 do pacote de PS5. |
| `PPSA99810.zip` | Pacote canônico de PS5 usado pelo atualizador automático do app. |
| `PPSA99810.zip.sig` | Assinatura do pacote canônico de PS5 (verificada pelo atualizador do app). |
| `PPSA99810.zip.sha256` | Checksum SHA256 do pacote canônico de PS5. |
| `PSBoxCloudGaming[PS4]-v1.7.1.pkg` | Pacote FPKG instalável para PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.1.pkg.sha256` | Checksum SHA256 do pacote de PS4. |
| `PSBoxCloudGaming[Linux]-v1.7.1.AppImage` | Executável universal para distribuições Linux e Steam Deck (Modo Desktop e Modo Jogo). |
| `PSBoxCloudGaming[Linux]-v1.7.1.AppImage.sha256` | Checksum SHA256 do AppImage de Linux. |

### 🚀 Como Instalar

- **PlayStation 5**: atualize pelo app (Configurações → Atualizações), pelo ProsperoStore, ou baixe `PSBoxCloudGaming[PS5]-v1.7.1.zip` e copie a pasta `PPSA99810` para `/data/homebrew/`.
- **PlayStation 4**: instale `PSBoxCloudGaming[PS4]-v1.7.1.pkg` pelo GoldHEN Package Installer.
- **Linux / Steam Deck**: baixe `PSBoxCloudGaming[Linux]-v1.7.1.AppImage`, torne executável (`chmod +x`) e abra no Modo Desktop ou adicione como jogo não-Steam.

> Vindo da v1.6.9 ou anterior: cada perfil do PS5 faz login uma vez; o login que já existia fica com o primeiro perfil que abrir o app.
