# Handoff: builds de Windows e Android do PSBox Cloud Gaming

Guia de execução para o agente que vai implementar as builds de **Windows** e **Android**. Escrito em 2026-11-14 sobre a v1.7.2 (`main` = `48de20b`, tag `v1.7.2`).

O resumo de decisões e o porquê estão em [docs/windows-android.md](docs/windows-android.md). Este arquivo é o **como**: passo a passo, comandos, arquivos, armadilhas e critérios de pronto. Leia inteiro antes de começar.

> **Tamanho do trabalho:** o Windows é de médio porte, o Android é grande. Faça em fases (seção 5 para Windows, seção 6 para Android). Cada fase termina com build verde em **todos** os alvos e um PR próprio. Não junte fases num PR.

---

## 0. Índice

1. Regras do repositório que você não pode quebrar
2. Estado atual: o que existe, o que funciona, o que é só esqueleto
3. Arquitetura do app: o que é núcleo, o que é backend e onde cada plataforma entra
4. Como buildar hoje (PS5, PS4, Linux) e como provar que nada quebrou
5. Windows: passo a passo (W1 a W4)
6. Android: passo a passo (A1 a A6)
7. Release, CI e distribuição
8. Armadilhas conhecidas
9. Checklists de pronto por fase
10. Referências de código (onde fica cada coisa)
11. Perguntas para o usuário antes das fases com hardware

**Apêndices** (nível de linha de código):
- A. Inventário das chamadas de socket a portar
- B. Operações de arquivo e **troféus só nos consoles** (decisão do usuário)
- C. Pontos de inserção para o ciclo de vida (desktop_main, shell_desktop, gpu.cpp)
- D. Revisão do `platform_android.cpp` e extração dos assets
- E. O contrato do `HwDecoder`
- F. Licenças nos pacotes (inclui a falha do nanosvg)
- G. Testes unitários
- H. SDL3, `__linux__` e crash guard por plataforma
- I. Protocolo do xCloud, WebRTC e o contexto das travadas de Wi-Fi
- J. Plano de PRs em sequência
- K. Variáveis de ambiente e arquivos de controle (autoplay, capturas) para testar

---

## 1. Regras do repositório que você não pode quebrar

Leia também o [AGENTS.md](AGENTS.md) da raiz. As regras abaixo são as que mais pesam neste trabalho.

### 1.1 Multiplataforma

- **O PS5 é o produto principal.** Nenhuma mudança para Windows ou Android pode mudar o comportamento, o layout ou o desempenho do PS5 nem do PS4. Toda função exclusiva de uma plataforma é **no-op** nas outras, nunca crash e nunca erro de build.
- O código compartilhado não pode citar APIs de console. O script [tools/check-platform-boundaries.sh](tools/check-platform-boundaries.sh) roda no CI e falha se aparecerem, fora dos arquivos de plataforma:
  - `sce*`;
  - `XCLOUD_PS5`, `XC_PLATFORM_PS5`, `XC_TARGET_PS4`, `__PROSPERO__`, `__ORBIS__`, `__PS4__`;
  - `#if ... __FreeBSD__`;
  - `#ifndef XC_PLATFORM_DESKTOP`.
- As diferenças entre plataformas vão por **capacidades** e não por `#ifdef` espalhado (seção 3.3).
- **Troféus são exclusivos do PS5 e do PS4** (decisão do usuário). Linux, Windows e Android não compilam a lógica de troféus nem mostram nada de troféus na interface. Detalhes no Apêndice B.2.
- Arquivos de backend seguem o padrão de nome `*_ps5.cpp`, `*_ps4.cpp`, `*_sdl.cpp`, `*_null.cpp`. Para Android, use `*_android.cpp`; para Windows, `*_windows.cpp` (ou `src/platform/windows/`).

### 1.2 Git e PRs

- **Nunca** rode `git restore .` nem `git checkout -- .`. Antes de descartar algo, leia o `git diff`; use `git stash` ou uma branch.
- Trabalhe em branch (`feat/windows-w1`, `feat/android-a1` etc.) e abra um PR por fase. Quem faz o merge é o usuário.
- **Nunca commite:**
  - a pasta `reddit/` (feedback salvo, privado);
  - `HANDOFF_NEXT_SESSION.md` e `QUALITY_CANDIDATES.md` (notas locais);
  - `account.json` nem tokens.
- O submódulo `deps/libdatachannel` aparece como modificado (`m deps/libdatachannel`) por patches locais do PS4. **Não commite** esse estado.
- Commits pequenos, mensagens claras em inglês, no estilo do histórico: `feat(windows): ...`, `fix(android): ...`, `build(ci): ...`.

### 1.3 Textos do app

- O texto da interface fica em [src/ui/strings.h](src/ui/strings.h) e [src/ui/strings.cpp](src/ui/strings.cpp), com uma tradução por idioma em `src/ui/i18n/strings_{en,pt_br,es,fr,de,it}.inc`. O inglês é o padrão.
- Toda string nova entra **nos 6 arquivos**.
- Não deixe texto fixo no código da interface.

### 1.4 Validação

- Testar é responsabilidade sua: build, testes unitários, `--selftest`, capturas de tela. **O usuário só homologa no fim.** Não peça a ele para "testar se compila".
- **O usuário NÃO tem PS4.** Não peça teste em PS4 e não liste "testar no PS4" como pendência dele. O PS4 só é conferido pelo build (`build-ps4`) e, quando der, no emulador shadPS4 (`tools/ps4/autotest.sh`).
- Antes de pedir validação em hardware de Windows ou Android, confirme com o usuário qual aparelho ele tem. Isso ainda não foi perguntado.
- Mudança de desempenho (decodificador, áudio, rede) exige o A/B do AGENTS.md §7: números de antes e depois, nas mesmas condições.

### 1.5 Release

- O AGENTS.md §6 exige **exatamente 10 arquivos** por release (PS5 ×6, PS4 ×2, Linux ×2), conferidos por `tools/release-verify.sh vX.Y.Z --verify-sigs`.
- Windows e Android **mudam essa regra**. Não publique arquivos novos numa release sem antes atualizar o §6, o `release-verify.sh`, o `publish-release.sh` e as tabelas bilíngues do `notes.md`, e sem o OK do usuário (seção 7).
- Um binário já publicado **nunca é substituído** na mesma tag (AGENTS.md §9.2): correção sai em versão nova.

---

## 2. Estado atual

### 2.1 O que já funciona

| Plataforma | Estado | Como é feito |
|---|---|---|
| PS5 | produto principal, completo | SDK de payload (`build-ps5`), backends `*_ps5.cpp`, decodificador por hardware da Sony |
| PS4 | builda e é publicado; sem teste em hardware | OpenOrbis (`build-ps4`), backends `*_ps4.cpp` |
| Linux | app `psbox` em SDL3, publicado como AppImage | `build-desktop`, backends `*_sdl.cpp`, display Vulkan com FSR/Anime4K, FFmpeg com hwaccel Vulkan |

O Linux é o **modelo** para Windows e Android: mesmo app (`psbox`), mesmos backends SDL3, mesma camada de display.

### 2.2 O que existe para Windows (esqueleto nunca compilado)

| Arquivo | O que tem | Situação |
|---|---|---|
| [src/platform/windows/platform_windows.cpp](src/platform/windows/platform_windows.cpp) | todas as funções de [src/platform/platform.h](src/platform/platform.h) (402 linhas): pasta em `%LOCALAPPDATA%\psbox`, threads Win32, `getaddrinfo`, `CryptGenRandom` | **nunca compilado**; revisar função por função. **Bug de segurança:** se `CryptAcquireContextA` falhar, `randomBytes` cai em `std::rand()` (linhas 121–137). Essa função gera chaves (selo da conta, nonces): trocar por `BCryptGenRandom(nullptr, ..., BCRYPT_USE_SYSTEM_PREFERRED_RNG)` e **retornar false** na falha, sem fallback |
| [CMakeLists.txt](CMakeLists.txt) (por volta da linha 291) | `if(WIN32)`: liga `ws2_32 crypt32 iphlpapi winmm secur32` e usa `platform_windows.cpp` no lugar de `posix_common.cpp` + `platform_linux.cpp` | pronto, sem teste |
| [CMakeLists.txt](CMakeLists.txt) (por volta da linha 372) | `add_executable(psbox WIN32 ...)`: app GUI com o entry point do `SDL_main.h` | pronto, sem teste |
| [cmake/platform.cmake](cmake/platform.cmake) | `WIN32` → `XC_PLATFORM=windows` → define `XC_PLATFORM_WINDOWS=1` e `XC_PLATFORM_DESKTOP=1` | pronto |
| [tools/desktop/package-windows.sh](tools/desktop/package-windows.sh) | monta `PSBox-<versão>-windows-x86_64.zip` (`psbox.exe`, `SDL3.dll`, `assets/`, `licenses/`) | nunca rodou de verdade. **Bug:** sem binário, cria um `psbox.exe` vazio com `touch` (linhas 44–49) e segue. Isso precisa **falhar** (`fail "psbox.exe not found"`), senão a release sai com exe vazio. Também falta copiar as DLLs de runtime do llvm-mingw (seção 5) |
| [windows/README-windows.txt](windows/README-windows.txt) | README do zip portátil (`--windowed`, `--fullscreen`, `--data-dir`, `--selftest`) | texto pronto |
| `src/stream/net_diag.cpp`, `src/stream/wifi_filler.cpp`, `src/app/stream_player.cpp` | já têm `#if defined(_WIN32)` | no Windows viram stubs (o filler de Wi-Fi não roda lá) |

**Não existe:** job de CI, alvo de FFmpeg para Windows, toolchain configurado nem teste sob wine.

### 2.3 O que existe para Android (esqueleto fraco)

| Arquivo | O que tem | Situação |
|---|---|---|
| [android/settings.gradle.kts](android/settings.gradle.kts), [android/build.gradle.kts](android/build.gradle.kts) | projeto Gradle | básico |
| [android/app/build.gradle.kts](android/app/build.gradle.kts) | `com.rafaelngp.psbox`; compileSdk/targetSdk 34, minSdk 26; ABIs arm64-v8a + x86_64; dependência `androidx.games:games-activity:3.0.5`; `versionName 1.4.0` | **trocar** a GameActivity pelo SDL3 (seção 6) |
| [android/app/src/main/AndroidManifest.xml](android/app/src/main/AndroidManifest.xml) | INTERNET, ACCESS_NETWORK_STATE, RECORD_AUDIO, VIBRATE; activity `.MainActivity` landscape; LAUNCHER + LEANBACK_LAUNCHER | a `MainActivity` **não existe** |
| [android/app/src/main/cpp/CMakeLists.txt](android/app/src/main/cpp/CMakeLists.txt) | compila só `platform_android.cpp` + `hw_decoder_android.cpp` | **descartar**: o Gradle deve apontar para o CMakeLists da raiz |
| [src/platform/android/platform_android.cpp](src/platform/android/platform_android.cpp) | 158 linhas, POSIX; pasta de dados fixa em `/data/data/com.rafaelngp.psbox/files` | caminho fixo errado (seção 6) |
| [src/media/hw_decoder_android.cpp](src/media/hw_decoder_android.cpp) | 125 linhas, AMediaCodec em modo ByteBuffer (cópia para a CPU), timeouts de 2 ms, sem as chaves de baixa latência | serve só como referência; reescrever (seção 6, A3) |

**Não existe:** activity, ligação com o núcleo do app, FFmpeg para Android, assets dentro do APK, CI, assinatura nem ícones (`@mipmap/ic_launcher` é citado mas não existe).

---

## 3. Arquitetura do app

### 3.1 Bibliotecas (CMakeLists.txt)

| Alvo | O que tem | Plataforma |
|---|---|---|
| `xcloud_core` (STATIC) | JSON, log, **HTTP/TLS** (`net/http.cpp`), login Microsoft/Xbox (`auth/*`), APIs do xCloud (`xcloud/*`), mapeamento de controles (`input/pad_map`, `kbm_map`, `pad_usb*`), **troféus** (`trophy/*`) e a camada de plataforma (`src/platform/<plat>/`) | todas. Os troféus hoje entram em todas, mas **passam a entrar só no PS5 e no PS4** (Apêndice B.2) |
| `xcloud_stream` (STATIC) | WebRTC (libdatachannel), RTP de vídeo (`h264_receiver`), pacotes de entrada, RTCP, `net_diag`, `wifi_filler`, `stream_session` | todas |
| `xcloud_media` (STATIC) | `decoder.cpp` (FFmpeg H.264 + hwaccel), fila de áudio, Opus (`audio_encoder`), base do microfone | todas |
| `xcloud_ui` (STATIC) | canvas por software, fontes, SVG, telas (`ui/app_ui*`), menu in-game (`ui/stream_menu`), strings, biblioteca e configurações (`app/library*`, `app/settings*`), updater | todas |
| `xcloud_appcore` (OBJECT) | estado do app, laço principal (`app_core`), worker (login, catálogo, streaming), `stream_player`, `stream_screen`, autoplay | todas |
| `xcloud_backends_null` (OBJECT) | display, controle, teclado/mouse, decodificador por hardware, áudio e troféus **nulos** | desktop sem SDL e testes |
| `xcloud_backends_sdl_io` (OBJECT) | `sdl_input`, `controller_sdl`, `kbm_sdl`, `audio_out_sdl`, `audio_in_sdl` | desktop com SDL3 |
| `xcloud_backends_desktop` (OBJECT) | `display_sdl` (Vulkan, com SDL_Renderer de reserva), `gpu.cpp` (FSR, Anime4K, RCAS, deband), `shell_desktop`, `desktop_selftest`, `desktop_main`, `hw_decoder_null` e `trophy_backend_null`, que **sai**: troféus só nos consoles (Apêndice B.2) | desktop com SDL3 |
| `psbox` (exe, `WIN32` no Windows) | o app de desktop | Linux e Windows (e Android, seção 6) |
| `psbox-headless`, `xcloud-cli`, `xcloud-tests` | sem janela, ferramenta de linha de comando, testes unitários | desktop |
| `xcloud_app` → `eboot.bin` | o app do PS5 (linkado por `tools/ps5/link.sh`) | PS5 |
| `xcloud_ps4.elf` | o app do PS4 | PS4 |

**Para Windows** você muda o `xcloud_core` (sockets e arquivos), o `xcloud_stream` (sockets), a plataforma e o CI. Os backends SDL3 servem como estão.

**Para Android** você monta o `psbox` como **biblioteca compartilhada** `libmain.so`, carregada pela `SDLActivity`, com os mesmos backends SDL3. O que muda é o decodificador por hardware, os assets e o ciclo de vida.

### 3.2 Interfaces de backend que cada plataforma implementa

São funções livres em namespaces. A plataforma liga a implementação certa no CMake; não há classes virtuais.

| Header | Funções principais | Implementações hoje |
|---|---|---|
| [src/platform/platform.h](src/platform/platform.h) | `init`, `dataDir`, `appDir`, `assetDir`, `caBundlePath`, `randomBytes`, `resolveIPv4`, `localIPv4`, `nowMs`/`nowUs`, `sleepMs`, `startThread`, `setThreadAffinity`, `setThreadRealtimePriority`, `isHandheld`, `notify`, teclado do sistema, `restartApp`, `platformName`, `imageCacheBudget`, `readFile`, `writeFileAtomic`, `makePrivate` | ps5, ps4, linux (+ `posix_common.cpp`), windows, android |
| [src/display/display.h](src/display/display.h) | `init`, `present`, `pump`, `drawYuv420`, `drawNv12`, `videoSlots` (só o PS5), `setOverlay`, upscaler/deband/sharpen, `caps()`, `setHighRefresh`, `readBackRgb` (capturas) | ps5, ps4, sdl, null |
| [src/media/hw_decoder.h](src/media/hw_decoder.h) | a classe `HwDecoder`: `init(maxW, maxH)`, `decode(data, len, pts, Picture&)`. `Picture` tem ponteiro, pitch e formato na memória da CPU | ps5, ps4, null, android (esqueleto) |
| [src/app/shell.h](src/app/shell.h) | `beforeSettings`, `beforeDisplay`, `afterDisplay`, `pumpEvents`, `hasFocus`, `canSelfUpdate`, `caps()`, `setFullscreen`, `openUrl`, `setTextInput`/`takeTextInput`, eventos de mouse | ps5, ps4, desktop, null |
| [src/input/controller.h](src/input/controller.h) | `init`, `poll`/`pollPad`, `refreshPads`, `setRumble`, `setTriggerRumble`, gatilhos adaptativos, giroscópio, `applyPadSettings` | ps5, ps4, sdl, null |
| `src/input/kbm.h`, `src/input/input_ext.h` | teclado/mouse; capacidades por controle (`padCaps`) | ps5, ps4, sdl, null |
| [src/media/audio_out.h](src/media/audio_out.h), `src/media/audio_in.h` | `audioStart`, `audioPush` (float estéreo intercalado), rotas, alto-falante do controle, voz; microfone | ps5, ps4, sdl, null |
| `src/trophy/trophy_backend*.h` | troféus nativos | ps5 (e o motor no ps4). **Nas outras, nenhum**: tudo vira o `trophy_off.cpp` vazio (Apêndice B.2) |

### 3.3 Capacidades (como esconder o que uma plataforma não tem)

Leia a seção "Capabilities" do [docs/multiplatform.md](docs/multiplatform.md). Em resumo:

- `display::caps()` devolve um `DisplayCaps`: `upscalers`, `deband`, `sharpen`, `highRefreshSwitch`, `presentTiming`.
- `input::padCaps(i)` / `anyPadCaps()`: vibração, vibração nos gatilhos, gatilhos adaptativos, light bar, giroscópio, touchpad.
- `shell::canSelfUpdate()`: hoje só o PS5 responde true.
- `app::uiCaps()` junta tudo, e `ui::visibleSettingsRows()` esconde as linhas de Configurações que a plataforma não suporta. Existe teste unitário garantindo que, com as capacidades do PS5, aparecem todas as linhas na mesma ordem. **Não quebre esse teste.**

Uma função nova que só uma plataforma tem segue o mesmo caminho: capacidade no header, implementação real na plataforma, no-op nas outras e linha escondida na interface.

### 3.4 Como o vídeo chega à tela no desktop (importa para o Android)

1. `stream_session` recebe RTP e `h264_receiver` remonta os access units.
2. `stream_player` passa cada AU a `media::decoder` (`src/media/decoder.cpp`).
3. O `decoder.cpp` tenta o `HwDecoder` da plataforma. No desktop ele é nulo e falha, então o código cai no FFmpeg com hwaccel Vulkan (linha 612) ou no FFmpeg por software.
4. Com hwaccel, o quadro é **copiado de volta para a CPU** (`av_hwframe_transfer_data`, linha 790) e entregue a `display::drawNv12()` ou `drawYuv420()`, que sobem os planos para o Vulkan e passam pelo `gpu.cpp` (FSR, Anime4K, deband, overlay do menu).

Portanto, hoje **não existe caminho zero-copy no desktop**: só o PS5 tem, via `videoSlots`. No Android, a primeira versão com MediaCodec pode usar o mesmo caminho com cópia (`Picture` na CPU → `drawNv12`). O zero-copy (AHardwareBuffer importado no Vulkan) é uma otimização posterior e exige uma função nova em `display.h` (seção 6, A4).

### 3.5 Pontos de entrada

| Arquivo | Plataforma |
|---|---|
| `src/app/ps5_main.cpp` | PS5 |
| `src/app/ps4_main.cpp` | PS4 |
| `src/app/desktop_main.cpp` | `psbox`: Linux, Windows e, via `SDL_main`, Android |
| `src/app/headless_main.cpp` | `psbox-headless` |
| `src/app/cli_main.cpp` | `xcloud-cli` |
| `tests/test_main.cpp` | `xcloud-tests` |

O `desktop_main.cpp` aceita `--selftest <dir>` (abre a janela e roda telas, entrada e som sem conta) e `--windowed`/`--fullscreen`/`--data-dir`. A variável `XCLOUD_DATA_DIR` troca a pasta de dados (o CI usa isso).

---

## 4. Como buildar hoje e provar que nada quebrou

Rode isto **antes** de mudar qualquer coisa, para ter a linha de base, e **antes de cada PR**. O caminho do repositório tem espaço (`PS5 Ports`): sempre use aspas.

### 4.1 Linux (desktop com SDL3)

```bash
cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DXCLOUD_DESKTOP_APP=ON
ninja -C build-desktop
./build-desktop/xcloud-tests            # todos devem passar
XCLOUD_DATA_DIR=/tmp/psbox-data ./build-desktop/psbox --selftest /tmp/psbox-selftest
```

- O SDL3 precisa ser 3.2 ou mais novo. O CI usa o **3.4.18**, compilado da fonte (`.github/workflows/ci.yml`, job `linux-app`).
- O FFmpeg é compilado automaticamente na primeira configuração por `tools/build-ffmpeg.sh host <build>`, num cache em `~/.cache/xcloud-ps5/ffmpeg-host`, porque o configure do FFmpeg não aceita espaço no caminho.

### 4.2 Linux sem SDL (o que o job `build` do CI faz)

```bash
cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXCLOUD_DESKTOP_APP=OFF
ninja -C build-host && ctest --test-dir build-host --output-on-failure
```

### 4.3 PS5 (precisa compilar sempre; não precisa rodar no console para o seu trabalho)

```bash
cmake -S . -B build-ps5 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/ps5.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
ninja -C build-ps5 xcloud_app
bash tools/ps5/link.sh build-ps5         # -> build-ps5/eboot.bin
```

- O toolchain do PS5 fica em `../PS5_Vulkan`, ao lado do repositório. Se não existir na máquina, o build do PS5 não roda localmente: diga isso no PR em vez de pular calado.
- **Não faça deploy no console** por conta própria. O fluxo de deploy do AGENTS.md §2 é para mudanças de interface do PS5. O seu trabalho só precisa garantir que o PS5 **compila** e que os testes passam.

### 4.4 PS4 (só compilar)

```bash
tools/ps4/fetch-toolchain.sh             # uma vez
cmake -S . -B build-ps4 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/ps4.toolchain.cmake
ninja -C build-ps4
```

### 4.5 Checagens que o CI roda

```bash
bash tools/check-platform-boundaries.sh                   # nada de API de console no código comum
(cd .github/triage && python3 -m unittest -q test_triage)  # regras de triagem de issues
```

### 4.6 Critério de "não quebrou"

Todos estes passam, comparados com a linha de base:
- `xcloud-tests` (Linux);
- `--selftest` (Linux);
- o build do PS5 (`eboot.bin` gerado);
- o build do PS4;
- o `check-platform-boundaries`.

Se você mexer em código compartilhado (sockets, arquivos), o comportamento no Linux e no PS5 tem que ser **idêntico**: mesma ordem de chamadas, mesmos timeouts, mesmos erros no log.

---

## 5. Windows: passo a passo

**Alvo:** Windows 10 1809 ou mais novo, x86_64, o `psbox.exe` em SDL3 como zip portátil. A compilação é cruzada a partir do Linux com **llvm-mingw** (UCRT), o mesmo toolchain local e no CI. Não use MSVC: o CMakeLists tem `if(NOT MSVC)` por compatibilidade, mas ninguém testa esse caminho.

### W1: toolchain e núcleo compilando (`xcloud-tests.exe` passando sob wine)

**W1.1 Toolchain.**
- Baixe uma versão **fixa** do llvm-mingw (por exemplo `20261104`, asset `llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64.tar.xz`) para `~/.cache/xcloud-ps5/llvm-mingw-<versão>`.
- Faça isso num script `tools/windows/fetch-toolchain.sh`, no estilo de `tools/ps4/fetch-toolchain.sh`, e confira o sha256 do download dentro do script.
- Crie `cmake/windows.toolchain.cmake`:

```cmake
# SPDX-License-Identifier: GPL-3.0-or-later
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(XC_MINGW_ROOT "$ENV{XC_MINGW_ROOT}" CACHE PATH "llvm-mingw root")
set(CMAKE_C_COMPILER   "${XC_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${XC_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER  "${XC_MINGW_ROOT}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "${XC_MINGW_ROOT}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# A self-contained exe: libc++, libunwind and winpthreads inside it (no extra DLLs but SDL3.dll).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
```

- Configure com:

```bash
XC_MINGW_ROOT=~/.cache/xcloud-ps5/llvm-mingw-20261104 \
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/windows.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXCLOUD_DESKTOP_APP=OFF
ninja -C build-win xcloud-tests
wine build-win/xcloud-tests.exe
```

- No W1 o `XCLOUD_DESKTOP_APP=OFF` é proposital: primeiro o núcleo e os testes, sem SDL3.
- Cuidado com o `NOMINMAX`: sem ele, `<windows.h>` define as macros `min`/`max` e quebra `std::min`/`std::max` no código todo. Com ele, o `platform_windows.cpp` também precisa funcionar; se ele depender de `min`/`max`, troque para `std::`.

**W1.2 FFmpeg para Windows.**
- Em [tools/build-ffmpeg.sh](tools/build-ffmpeg.sh), acrescente o alvo `windows` ao lado de `ps5`/`ps4`/`host`, aproveitando o array `common` do script:

```bash
elif [[ $target == windows ]]; then
    mingw=${XC_MINGW_ROOT:?set XC_MINGW_ROOT}
    "$src/configure" "${common[@]}" \
        --enable-cross-compile --target-os=mingw32 --arch=x86_64 \
        --cross-prefix="$mingw/bin/x86_64-w64-mingw32-" --cc="$mingw/bin/x86_64-w64-mingw32-clang" \
        --enable-vulkan --enable-hwaccel=h264_vulkan \
        --enable-d3d11va --enable-hwaccel=h264_d3d11va "${vkflags[@]}"
```

- Os headers do Vulkan: reuse o bloco `vkcheck`/`vkflags` que já existe no ramo `host`. Mova-o para antes do `if` para servir aos dois.
- O `nasm` é o mesmo do host (o script já baixa um se faltar).
- Em [cmake/deps/ffmpeg.cmake](cmake/deps/ffmpeg.cmake):
  - acrescente `elseif(WIN32) set(_target windows)` na escolha do alvo;
  - não ligue `m` no Windows (`if(NOT XCLOUD_PS5 AND NOT XC_TARGET_PS4 AND NOT WIN32)`);
  - ligue `bcrypt` (o FFmpeg usa para números aleatórios) e, com d3d11va, `d3d11 dxgi`;
  - o cache do FFmpeg fica em `~/.cache/xcloud-ps5/ffmpeg-windows`.
- Em [src/media/decoder.cpp](src/media/decoder.cpp) (linha ~606), a lista `kHwConfigs` tenta Vulkan, VAAPI, CUDA e VDPAU. Acrescente `{AV_HWDEVICE_TYPE_D3D11VA, AV_PIX_FMT_D3D11}` **depois** do Vulkan. A lista é a mesma para todas as plataformas e o que não estiver no build simplesmente falha, então não precisa de `#ifdef`.

**W1.3 Camada de sockets: `src/net/socket.h` (+ `socket.cpp`).**

O mesmo código para POSIX e Winsock, com estas funções:

```cpp
namespace xc::net {
#if defined(_WIN32)
using Socket = uintptr_t;            // SOCKET
constexpr Socket kInvalidSocket = ~static_cast<uintptr_t>(0);
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif
bool socketsInit();                         // WSAStartup(2.2) once; no-op on POSIX
void closeSocket(Socket s);                 // closesocket / ::close
bool setNonBlocking(Socket s, bool on);     // ioctlsocket(FIONBIO) / fcntl(O_NONBLOCK) or ioctl(FIONBIO) as today
int  lastSocketError();                     // WSAGetLastError() / errno
bool wouldBlock(int err);                   // WSAEWOULDBLOCK / EAGAIN, EWOULDBLOCK
bool inProgress(int err);                   // WSAEWOULDBLOCK, WSAEINPROGRESS / EINPROGRESS
int  pollSockets(pollfd* fds, size_t n, int timeoutMs);   // WSAPoll / ::poll
bool bytesAvailable(Socket s, size_t& n);   // ioctlsocket(FIONREAD) / ioctl(FIONREAD)
bool setRecvTimeout(Socket s, int ms);      // SEE THE WARNING BELOW
bool setSendTimeout(Socket s, int ms);
int  sendNoSignal(Socket s, const void* p, size_t n);     // MSG_NOSIGNAL where it exists
}
```

- Chame `socketsInit()` dentro de `platform::init()` no Windows.
- Troque o uso direto nestes arquivos: [src/net/http.cpp](src/net/http.cpp), [src/app/web_server.cpp](src/app/web_server.cpp), [src/app/worker_diagnostics.cpp](src/app/worker_diagnostics.cpp), [src/stream/stream_session.cpp](src/stream/stream_session.cpp) e, depois, `net_diag.cpp` e `wifi_filler.cpp` (que hoje têm stubs `_WIN32`).
- **Mantenha o comportamento POSIX byte por byte.** No PS5, o `ioctl(FIONBIO)` atual funciona e o `fcntl` pode não funcionar igual: preserve a mesma chamada que existe hoje em cada plataforma.

> **Armadilhas do Winsock (todas reais, todas silenciosas):**
> 1. **`SO_RCVTIMEO`/`SO_SNDTIMEO` recebem um `DWORD` em milissegundos no Windows, não um `struct timeval`.** O `http.cpp:110` passa `timeval`; portado do jeito ingênuo, o timeout vira lixo. Além disso, um recv que estoura o timeout no Windows deixa o socket em estado indefinido (documentação da Microsoft). **No Windows, prefira `pollSockets` antes de cada `recv`/`send` e não dependa de `SO_RCVTIMEO`.**
> 2. `WSAPoll` só reporta corretamente um `connect` recusado a partir do Windows 10 2004. Em versões anteriores ele fica esperando até o timeout. Para o `connect` não bloqueante, use `select()` com `exceptfds` ou exija Windows 10 2004+ e documente.
> 3. Os erros vêm de `WSAGetLastError()`, **não** de `errno`. `errno` fica 0 e todo `if (errno == EAGAIN)` falha calado.
> 4. `setsockopt` recebe `const char*` no Windows: faça o cast em `socket.cpp`, não em cada chamada.
> 5. Não existe `SIGPIPE` nem `MSG_NOSIGNAL`: no Windows, `send` num socket fechado só retorna erro. O `#if defined(MSG_NOSIGNAL)` do `http.cpp:84` já lida com isso.
> 6. `inet_pton` e `getaddrinfo` precisam de `<ws2tcpip.h>`.
> 7. O `SOCKET` é unsigned de 64 bits. Comparar com `< 0` está errado: use `kInvalidSocket`.

**W1.4 Camada de arquivos: `src/util/fs.h` (+ `fs.cpp`) sobre `std::filesystem`.**

```cpp
namespace xc::fs {
bool exists(const std::string& utf8);
bool isDir(const std::string& utf8);
bool makeDirs(const std::string& utf8);                   // mkdir -p
bool removeFile(const std::string& utf8);
bool renameReplace(const std::string& from, const std::string& to); // atomic replace
std::vector<std::string> listDir(const std::string& utf8);  // names only, no "." or ".."
bool fileSize(const std::string& utf8, uint64_t& out);
bool modifiedTime(const std::string& utf8, int64_t& unixSeconds);
}
```

- **Todos os caminhos no app são `std::string` UTF-8.** O `platform_windows.cpp` atual usa as APIs "A" (`GetFileAttributesA`, `MoveFileExA`, `fopen`), que interpretam o texto na página de código ANSI. Com nome de usuário acentuado (`C:\Users\João\AppData\...`), a pasta de dados quebra. A solução mais barata e global é um **manifesto de aplicativo com `<activeCodePage>UTF-8</activeCodePage>`** (Windows 10 1903+). Com ele, todas as APIs "A" e o `fopen` passam a aceitar UTF-8, sem converter chamada por chamada. Junte no mesmo `windows/psbox.manifest`:
  - `<dpiAware>`/`<dpiAwareness>PerMonitorV2</dpiAwareness>`, para a janela não sair borrada com escala de 125–150%;
  - `supportedOS` do Windows 10.

  Embuta pelo `.rc` (`1 RT_MANIFEST "psbox.manifest"`). Ponto de atenção: isso **sobe o mínimo para o Windows 10 1903**. Caminho alternativo, se for preciso suportar versões mais antigas: as APIs `...W()` com `MultiByteToWideChar(CP_UTF8)` em `fs.cpp` e `platform_windows.cpp`.
- `renameReplace` no Windows é `MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. O `rename()` do C falha quando o destino existe.
- Troque o uso direto em: [src/app/updater.cpp](src/app/updater.cpp), [src/app/app_state.cpp](src/app/app_state.cpp), [src/ui/image_cache.cpp](src/ui/image_cache.cpp), [src/app/worker.cpp](src/app/worker.cpp).
- **No PS5, `std::filesystem` pode não existir ou se comportar diferente** (SDK de payload e libc++ do FreeBSD). Por isso o `fs.cpp` deve ter duas implementações:
  - POSIX (`opendir`/`stat`/`rename`), que é o código de hoje, movido sem mudança;
  - Windows (`std::filesystem` + `MoveFileExW`).

  Não troque o PS5 para `std::filesystem` sem testar no console.

**W1.5 Troféus só nos consoles (vale para Linux, Windows e Android).**
- Os 5 arquivos de `src/trophy/` estão hoje no `xcloud_core` de todas as plataformas e usam muito POSIX (`stat`, `unlink`, `opendir`).
- **Decisão do usuário (2026-11-14): troféus são exclusivos do PS5 e do PS4.** Linux, Windows e Android não compilam nenhum código de troféus (nem o Linux, que hoje compila). O código comum chama funções de troféus, então elas ganham uma implementação vazia, `src/trophy/trophy_off.cpp`, nas plataformas sem troféus. As linhas de troféus de Configurações somem por uma capacidade nova (`UiCaps::trophies`). Passo a passo e a questão dos testes no Apêndice B.2.
- **O comportamento de troféus no PS5 não pode mudar:** é a área mais delicada do app (AGENTS.md §8).

**W1.6 Crash guard e sinais.**
- `src/app/crash_guard.cpp` usa `sigaction`. Está na lista de permitidos do `check-platform-boundaries.sh` com `TODO(M2)`.
- Divida em `crash_guard_posix.cpp` (o atual) e `crash_guard_windows.cpp`, que usa `SetUnhandledExceptionFilter` e grava o código da exceção e o endereço no log, como o POSIX faz. O minidump (`MiniDumpWriteDump`, `dbghelp`) é opcional.
- Remova a exceção do script quando terminar.
- `src/app/shell_null.cpp` usa `signal()`: no Windows, `SIGINT` existe pelo CRT e basta. Se não compilar, use `SetConsoleCtrlHandler`.

**W1.7 Regra nova no `check-platform-boundaries.sh`.**
- Acrescente uma regra que proíbe `#include <sys/socket.h|netinet/*|arpa/inet.h|poll.h|unistd.h|dirent.h|sys/ioctl.h|fcntl.h>` fora de `src/platform/`, `src/net/socket.cpp`, `src/util/fs.cpp` e dos arquivos `*_ps5`/`*_ps4`/`*_sdl`/`*_android`.
- Assim ninguém reintroduz POSIX no código comum.
- Rode o script e conserte o que aparecer.

**W1.8 Revisar o `platform_windows.cpp` função por função.**
- `randomBytes`: o bug de segurança da seção 2.2.
- `resolveIPv4`: ok (`getaddrinfo` com `AF_INET`).
- `localIPv4`: usa o truque do `connect` UDP para 8.8.8.8 + `getsockname`, que pega a interface da rota padrão. Ok; só confira que não falha sem internet (deve retornar vazio, sem travar).
- `writeFileAtomic`: já grava em `.tmp` e faz `MoveFileExA(REPLACE_EXISTING)`. Ok com o manifesto UTF-8 do W1.4. Acrescente `MOVEFILE_WRITE_THROUGH`, para não perder o arquivo numa queda de energia.
- `assetDir()`: `<pasta do exe>/assets`, com as variáveis `XCLOUD_ASSETS` e `XCLOUD_CA_BUNDLE` como override, e `"assets"` relativo como último recurso. Ok para o zip portátil.
- `makePrivate`: no Windows a pasta em `%LOCALAPPDATA%` já é do usuário; no-op aceitável.
- `nowMs`/`nowUs`: `QueryPerformanceCounter`.
- `sleepMs`: com `timeBeginPeriod(1)` ligado em `init()` (e `timeEndPeriod` em `shutdown()`); sem isso, `Sleep(1)` dorme ~15,6 ms e o ritmo de quadros desanda.
- `setThreadRealtimePriority`: `AvSetMmThreadCharacteristicsW(L"Games")` (Avrt.lib) nas threads de áudio e vídeo, em vez de `THREAD_PRIORITY_TIME_CRITICAL`, que pode travar a máquina.

**Critério de pronto do W1:**
- `wine build-win/xcloud-tests.exe` passa em tudo;
- `build-desktop`, `build-ps5` e `build-ps4` continuam compilando;
- os testes do Linux passam;
- o `check-platform-boundaries` passa com a regra nova.

### W2: o app `psbox.exe` (SDL3) e o selftest

**W2.1 SDL3 para mingw.**
- Baixe o `SDL3-devel-3.4.18-mingw.tar.gz` das releases do SDL (a mesma versão do CI do Linux) para `~/.cache/xcloud-ps5/sdl3-3.4.18-mingw`.
- O CMake config fica em `x86_64-w64-mingw32/lib/cmake/SDL3`. Passe `-DCMAKE_PREFIX_PATH=<pasta>/x86_64-w64-mingw32`.
- Como o toolchain restringe a busca a `CMAKE_FIND_ROOT_PATH`, acrescente essa pasta a `CMAKE_FIND_ROOT_PATH` ou use `-DSDL3_DIR=` direto.
- O `SDL3.dll` vai junto do exe, em `x86_64-w64-mingw32/bin/SDL3.dll`.

**W2.2 Configurar com o app:**

```bash
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/windows.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DXCLOUD_DESKTOP_APP=ON \
      -DSDL3_DIR=$HOME/.cache/xcloud-ps5/sdl3-3.4.18-mingw/x86_64-w64-mingw32/lib/cmake/SDL3
ninja -C build-win
cp ~/.cache/xcloud-ps5/sdl3-3.4.18-mingw/x86_64-w64-mingw32/bin/SDL3.dll build-win/
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy XCLOUD_DATA_DIR='Z:\tmp\psbox-data' \
    wine build-win/psbox.exe --selftest 'Z:\tmp\psbox-selftest'
```

- **`XC_SDL3_LICENSE`:** o build de desktop procura a licença do SDL3 para o pacote (`/usr/share/licenses/SDL3/LICENSE.txt` no Fedora). Aponte para o `LICENSE.txt` do pacote mingw.
- **Ícone e metadados do exe:** crie `windows/psbox.rc` com `IDI_ICON1 ICON "psbox.ico"` e um `VERSIONINFO` com a versão do `project()`. Acrescente-o às fontes do `psbox` quando `WIN32`. Gere o `.ico` a partir de `ps5/sce_sys/icon0.png` com tamanhos 16, 32, 48 e 256 e **commite o .ico**, para o build não depender de ImageMagick.
- **Assets:**
  - [src/platform/windows/platform_windows.cpp](src/platform/windows/platform_windows.cpp) resolve `assetDir()` relativo ao exe (`getExeDir()`). Confira que é `<exe>\assets`, o mesmo layout do zip.
  - O `caBundlePath()` precisa apontar para `assets\cacert.pem`. **Sem ele, o TLS falha e não há login.**

**W2.3 Coisas do desktop que mudam no Windows:**
- **Atalhos:** F11 e Alt+Enter alternam a tela cheia (já no README). Confira que o `shell_desktop` não usa nada de X11/Wayland.
- **Navegador:** `shell::openUrl` deve usar `SDL_OpenURL` (que no Windows chama `ShellExecute`). Confira.
- **Fontes:** `shell::iconFontPaths()` usa as fontes do próprio app (`assets/fonts`). Não dependa de fontes do sistema.
- **WebUI:** o servidor web (`web_server.cpp`) escuta numa porta. No primeiro uso, o Windows mostra o aviso do Firewall do Windows Defender. É normal; documente no README do Windows. Se o usuário negar, a WebUI só funciona em `localhost`.
- **Wine não testa:** Vulkan real, controles, áudio de verdade, latência. Isso é o W3.

**Critério de pronto do W2:**
- `psbox.exe --selftest` termina com sucesso sob wine (offscreen) e gera as capturas;
- o zip sai de `tools/desktop/package-windows.sh build-win` (com a correção da seção 2.2);
- o CI tem o job `windows-app` (seção 7.2) verde.

### W3: Windows de verdade (precisa de hardware: pergunte ao usuário)

Pergunte ao usuário se ele tem um PC com Windows 10/11 para testar e qual GPU. Sem isso, o W3 depende de testadores da comunidade. Nesse caso, publique como build de teste (seção 7.1).

O que validar, nesta ordem:
1. **Inicialização:** abre a janela, a pasta `%LOCALAPPDATA%\psbox` é criada e o log `xcloud.log` sai lá.
2. **Login:** o código de dispositivo aparece; login, catálogo e capas carregam.
3. **Display:** Vulkan sobe (o log diz qual backend); FSR e Anime4K aparecem nas opções. Numa GPU sem Vulkan, o `SDL_Renderer` assume e as opções de GPU somem (capacidades).
4. **Streaming:** 1080p60 com som; `decodeMs` no overlay. A ordem esperada de hwaccel é Vulkan, depois D3D11VA, depois software.
5. **Controles:** Xbox (XInput), DualSense e DS4 por USB e Bluetooth (vibração, gatilhos, giroscópio pelo HIDAPI do SDL), teclado e mouse nos jogos com suporte.
6. **Microfone:** chat de voz.
7. **Ritmo de quadros:** sem `timeBeginPeriod(1)` aparecem micro travadas regulares. Confira `onScreenMs` e quadros pulados.

Extras do W3, cada um com o A/B do AGENTS.md §7 antes de aceitar:
- **Modo de streaming do Wi-Fi.** Com `wlanapi`:
  - abrir com `WlanOpenHandle`;
  - para cada interface de `WlanEnumInterfaces`, chamar `WlanSetInterface(h, &guid, wlan_intf_opcode_media_streaming_mode, sizeof(BOOL), &on, nullptr)`;
  - ligar quando a transmissão começa e desligar ao fechar (e no `beforeShutdown`).

  Isso corta as varreduras de fundo do Windows, que causam travadas periódicas de 100 a 300 ms em Wi-Fi. É o mesmo sintoma investigado no PS5 (Apêndice I.4). Fica no backend de plataforma, exposto por uma função nova `platform::setLowLatencyNetwork(bool)`, no-op nas outras plataformas.
- **Filler de Wi-Fi** (`src/stream/wifi_filler.cpp`): hoje é stub no Windows. Com o `socket.h` do W1 ele pode ser portado. Ele só arma depois de 2 travadas em 2 minutos, então quem usa cabo não é afetado.
- **MMCSS** nas threads de vídeo e áudio (W1.8).

### W4: empacotamento e release

Fica na seção 7. Resumo:
- o zip portátil `PSBoxCloudGaming[Windows]-vX.Y.Z.zip` com `psbox.exe`, `SDL3.dll`, `assets\`, `licenses\` e `README-windows.txt`;
- o `.sha256`;
- de preferência um `.sig` (a mesma chave ECDSA do PS5), para um futuro atualizador;
- o atualizador continua desligado (`canSelfUpdate() == false`) até ser implementado (seção 7.4).

---

## 6. Android: passo a passo

**Alvo:** Android 10 (API 29) ou mais novo em celulares, tablets, TV boxes e Android TV. ABIs `arm64-v8a` (principal) e `x86_64` (emulador e Chromebook). O app é o mesmo `psbox` do desktop, compilado como `libmain.so` e aberto pela `SDLActivity` do SDL3.

**Por que SDL3 e não GameActivity:** o SDL3 já dá janela e superfície Vulkan, áudio AAudio, controles (InputDevice do Android + HIDAPI para DualSense), teclado virtual, toque e eventos de ciclo de vida, todos pelos **mesmos backends `*_sdl.cpp` que o Linux usa**. Com a GameActivity, cada um desses backends teria que ser reescrito.

### A1: o projeto compila e abre (selftest no emulador)

**A1.1 SDL3 como submódulo.**

```bash
git submodule add https://github.com/libsdl-org/SDL.git deps/SDL
git -C deps/SDL checkout release-3.4.18      # a mesma versão do CI do Linux
```

Em [cmake/deps/sdl3.cmake](cmake/deps/sdl3.cmake), no Android, use a fonte em vez de `find_package`. Acrescente um primeiro ramo à cadeia `if` que já existe:

```cmake
set(XC_DESKTOP_APP OFF)
if(ANDROID)
    # Android: SDL3 from the deps/SDL submodule (one libSDL3.so per ABI, packed by Gradle).
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/deps/SDL ${CMAKE_BINARY_DIR}/SDL EXCLUDE_FROM_ALL)
    set(XC_DESKTOP_APP ON)
elseif(XCLOUD_PS5 OR _xc_desktop MATCHES "^(OFF|0|NO|FALSE|N)$")
    # ... the existing branches, unchanged
```

- O `add_subdirectory` define o alvo `SDL3::SDL3`, que o resto do CMakeLists já usa.
- Linux e Windows continuam no `find_package`. **Não** passe o Linux para o submódulo: o AppImage depende do SDL compilado no CI com todos os backends carregados em tempo de execução.
- O submódulo só é necessário para o Android. Clonar o repositório sem `--recursive` continua funcionando para os outros alvos.

O `libSDL3.so` precisa ir para o APK; o Gradle empacota as `.so` que o CMake gera.

**A1.2 Plataforma `android` no CMake.**
- Em [cmake/platform.cmake](cmake/platform.cmake), acrescente `elseif(ANDROID) set(XC_PLATFORM android)` **antes** do `else()` do Linux, porque `UNIX` também é verdadeiro no Android. Isso define `XC_PLATFORM_ANDROID=1` e também `XC_PLATFORM_DESKTOP=1`, o que é correto: o Android usa os backends de desktop.
- No [CMakeLists.txt](CMakeLists.txt) (por volta da linha 291), o ramo não-Windows liga `posix_common.cpp` + `platform_linux.cpp`. Acrescente um ramo `ANDROID`:

```cmake
elseif(ANDROID)
    target_sources(xcloud_core PRIVATE src/platform/posix_common.cpp src/platform/android/platform_android.cpp)
    target_link_libraries(xcloud_core PUBLIC log android mediandk)
```

  O `posix_common.cpp` dá `readFile`, `writeFileAtomic`, threads, `nowMs`/`nowUs`, `sleepMs` e `localIPv4`. O `platform_android.cpp` dá o resto. Já conferi que os dois **não definem as mesmas funções**.
- No Android, o `psbox` deve ser **biblioteca compartilhada** chamada `main`:

```cmake
if(ANDROID)
    add_library(psbox SHARED ${psbox_sources})   # as mesmas fontes do add_executable(psbox ...)
    set_target_properties(psbox PROPERTIES OUTPUT_NAME main)
else()
    add_executable(psbox WIN32 ${psbox_sources})
endif()
```

  Para isso, extraia a lista de fontes do `add_executable(psbox WIN32 ...)` atual (por volta da linha 372) para uma variável.
- `psbox-headless`, `xcloud-cli` e `xcloud-tests` **não** fazem sentido no APK: não os gere quando `ANDROID`.
- **`hw_decoder`:** no Android, troque `hw_decoder_null.cpp` por `hw_decoder_android.cpp` em `xcloud_backends_desktop`. No A1 ele pode continuar falhando no `init()`, e o FFmpeg decodifica por software. O MediaCodec de verdade é o A4.

**A1.3 FFmpeg para Android.** Em [tools/build-ffmpeg.sh](tools/build-ffmpeg.sh), acrescente um alvo `android-<abi>`:

```bash
elif [[ $target == android-* ]]; then
    abi=${target#android-}
    ndk=${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}
    tc=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin
    api=29
    case $abi in
        arm64-v8a) arch=aarch64; triple=aarch64-linux-android; asm=(--enable-neon) ;;
        x86_64)    arch=x86_64;  triple=x86_64-linux-android;  asm=(--disable-x86asm) ;;  # emulador: simplicidade > velocidade
    esac
    "$src/configure" "${common[@]}" \
        --enable-cross-compile --target-os=android --arch=$arch \
        --cc="$tc/$triple$api-clang" --cxx="$tc/$triple$api-clang++" \
        --ar="$tc/llvm-ar" --nm="$tc/llvm-nm" --ranlib="$tc/llvm-ranlib" --strip="$tc/llvm-strip" \
        "${asm[@]}"
```

- **Não** ligue `--enable-mediacodec`/`--enable-jni` no FFmpeg: o MediaCodec é usado direto pelo NDK, no nosso `HwDecoder` (A4).
- Em [cmake/deps/ffmpeg.cmake](cmake/deps/ffmpeg.cmake), acrescente `elseif(ANDROID) set(_target android-${ANDROID_ABI})`. O Gradle roda o CMake uma vez por ABI, cada um com seu `CMAKE_BINARY_DIR`, então cada ABI tem seu FFmpeg. O cache do script fica em `~/.cache/xcloud-ps5/ffmpeg-android-arm64-v8a` e assim por diante.
- O caminho do repositório tem espaço; o script já contorna isso com o link simbólico no cache. Não remova esse truque.

**A1.4 mbedTLS e libdatachannel.**
- Os dois compilam no Android pelo CMake do NDK (libjuice e usrsctp têm suporte).
- Se o usrsctp reclamar de `HAVE_SA_LEN`, **não** reaproveite as definições do PS5 (linha 84 do CMakeLists, que são do PS5): no Bionic/Linux, `sockaddr` não tem `sa_len`.
- Os patches de `ps4/patches/` só se aplicam ao PS4.

**A1.5 Gradle (refazer o projeto em `android/`).**
- **Wrapper do Gradle:** não existe `gradlew`. Gere com `gradle wrapper --gradle-version 8.11.1` (ou a mais nova estável) e commite `gradlew`, `gradlew.bat`, `gradle/wrapper/gradle-wrapper.properties` e `gradle-wrapper.jar`.
- **Plugins:** `com.android.application` (AGP 8.7 ou mais novo). O Kotlin não é necessário: remova `org.jetbrains.kotlin.android` e escreva a activity em Java, para ter menos dependência.
- **`android/app/build.gradle.kts` (pontos que mudam):**

```kotlin
android {
    namespace = "com.rafaelngp.psbox"
    compileSdk = 35
    ndkVersion = "27.2.12479018"          // NDK r27c (LTS); fixe a versão, nunca "latest"
    defaultConfig {
        applicationId = "com.rafaelngp.psbox"
        minSdk = 29
        targetSdk = 35
        versionCode = /* major*10000 + minor*100 + patch, lido do project() do CMakeLists.txt */
        versionName = /* "1.7.2" do mesmo lugar */
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake {
            arguments += listOf("-DXCLOUD_DESKTOP_APP=ON", "-DANDROID_STL=c++_shared", "-DCMAKE_BUILD_TYPE=Release")
        } }
    }
    externalNativeBuild { cmake {
        path = file("../../CMakeLists.txt")    // o CMakeLists da RAIZ, não app/src/main/cpp
        version = "3.22.1"
    } }
    sourceSets["main"].apply {
        java.srcDirs("src/main/java", "../../deps/SDL/android-project/app/src/main/java")  // SDLActivity sem copiar
        assets.srcDirs("../../assets")
    }
}
```

  - Leia a versão do `CMakeLists.txt` com regex no próprio Gradle (`Regex("project\\(xcloud_ps5 VERSION ([0-9.]+)")`), para nunca dessincronizar. 1.7.2 vira `versionCode = 10702`.
  - Remova as dependências `games-activity`, `appcompat`, `material` e `core-ktx`; o SDL não precisa delas.
  - Apague `android/app/src/main/cpp/CMakeLists.txt`.
- **`MainActivity`** (`android/app/src/main/java/com/rafaelngp/psbox/MainActivity.java`):

```java
package com.rafaelngp.psbox;
import org.libsdl.app.SDLActivity;
public class MainActivity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[] { "SDL3", "main" }; }
    // Argumentos para o main() nativo: permite rodar o selftest por intent (A1.7).
    @Override protected String[] getArguments() {
        String st = getIntent().getStringExtra("selftest");
        return st == null ? new String[0] : new String[] { "--selftest", st };
    }
}
```

- **AndroidManifest:** parta do `deps/SDL/android-project/app/src/main/AndroidManifest.xml`. Ele já lista os `configChanges`, as features de USB host e as permissões que o SDL precisa (Bluetooth para HIDAPI, vibração). Acrescente:
  - permissões: `INTERNET`, `ACCESS_NETWORK_STATE`, `ACCESS_WIFI_STATE` e `WAKE_LOCK` (lock de Wi-Fi, A5), `RECORD_AUDIO` (chat de voz, pedido em tempo de execução);
  - `<uses-feature>` com `required="false"` para `android.hardware.touchscreen`, `android.software.leanback`, `android.hardware.gamepad` e `android.hardware.vulkan.version`, para a loja/instalador não recusar TVs nem aparelhos sem toque;
  - `android:screenOrientation="sensorLandscape"`;
  - `LAUNCHER` e `LEANBACK_LAUNCHER` (este exige `android:banner`, 320×180, A6).
- **Ícones:** `@mipmap/ic_launcher` é citado mas não existe. Gere a partir de `ps5/sce_sys/icon0.png` (mdpi 48, hdpi 72, xhdpi 96, xxhdpi 144, xxxhdpi 192, mais um ícone adaptativo) e commite os PNGs.

**A1.6 Pastas e assets no Android.**
- **Pasta de dados:** o `platform_android.cpp` usa o caminho fixo `/data/data/com.rafaelngp.psbox/files`, que **falha** em perfil secundário e em perfil de trabalho (`/data/user/<n>/...`). Ele já lê `XCLOUD_DATA_DIR` antes disso. A correção mínima, sem fazer o `xcloud_core` depender do SDL, fica no `desktop_main.cpp`, antes de `platform::init()`:

```cpp
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
    if (const char* p = SDL_GetAndroidInternalStoragePath()) setenv("XCLOUD_DATA_DIR", p, 0);
#endif
```

- **Assets** (`assets/`: fontes, ícones, `cacert.pem`, WebUI): no APK eles não são arquivos comuns, e o app lê caminhos (`assetDir() + "/fonts/..."`), inclusive o mbedTLS, que recebe o caminho do `cacert.pem`. A solução mais simples e robusta é **extrair na primeira execução**:
  1. No build, gere `assets/asset-list.txt` com todos os arquivos e o hash ou a versão. Faça isso numa task do Gradle ou num passo do CMake, e **não** commite o arquivo gerado.
  2. No `desktop_main.cpp` (Android), compare `asset-list.txt` do APK, lido por `SDL_IOFromFile("asset-list.txt", "rb")` (o SDL abre assets do APK de forma transparente), com a cópia em `<dataDir>/assets/asset-list.txt`. Se forem diferentes, copie cada arquivo por `SDL_IOFromFile` para `<dataDir>/assets/` e grave a lista por último.
  3. `setenv("XCLOUD_ASSETS", "<dataDir>/assets", 1)` e `setenv("XCLOUD_CA_BUNDLE", "<dataDir>/assets/cacert.pem", 1)`, antes de `platform::init()`.
  4. Confira que o `assetDir()` e o `caBundlePath()` do `platform_android.cpp` respeitam essas variáveis, como os do Windows fazem.
- `assets/media_showcase` (7,7 MB de capturas) é servido pela WebUI (`web_server.cpp`). Mantenha, ou meça se vale tirar do APK.

**A1.7 Selftest no emulador.**
- Crie um AVD x86_64 com imagem Google APIs (API 34 ou 35) e rode com `emulator -avd <nome> -gpu host`, que dá Vulkan pelo gfxstream.

```bash
cd android && ./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.rafaelngp.psbox/.MainActivity --es selftest /data/user/0/com.rafaelngp.psbox/files/selftest
adb logcat -s SDL psbox xcloud     # confira as tags que o log do app usa no Android (util/log.cpp)
adb exec-out run-as com.rafaelngp.psbox tar c -C files selftest | tar x -C /tmp/psbox-android-selftest
```

- O `util/log.cpp` precisa mandar o log para o logcat no Android (`__android_log_write`) **além** do arquivo `xcloud.log`. Confira e implemente se faltar, porque sem isso você depura às cegas.

**Critério de pronto do A1:**
- o APK de debug instala e abre no emulador;
- o selftest termina e gera as capturas, que você confere visualmente;
- Linux, PS5 e PS4 continuam compilando e os testes do Linux passam.

### A2: primeira transmissão (decodificação por software)

1. **Login:** o login por código de dispositivo (`auth/msa.cpp`) não depende de navegador embutido. O `shell::openUrl` (SDL_OpenURL) abre o navegador do Android para a página do código.
2. **Ciclo de vida.** O SDL3 entrega estes eventos; trate todos no `shell_desktop.cpp` ou no laço do `desktop_main`:
   - `SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_BACKGROUND`: **pare de desenhar**. A superfície Android é destruída e qualquer `vkQueuePresentKHR` depois disso falha com `VK_ERROR_SURFACE_LOST_KHR`. Se houver transmissão, encerre a sessão de forma limpa: a primeira versão sai da transmissão e volta à biblioteca; manter o som em segundo plano é decisão do usuário, não implemente sem perguntar.
   - `SDL_EVENT_WILL_ENTER_FOREGROUND` / `SDL_EVENT_DID_ENTER_FOREGROUND`: **recrie** `VkSurfaceKHR` + swapchain (`SDL_Vulkan_CreateSurface` de novo) no `display_sdl.cpp`. Hoje o `display_sdl` assume que a superfície vive o app inteiro. Este é o ponto de maior risco de crash do A2.
   - `SDL_EVENT_TERMINATING` e `SDL_EVENT_LOW_MEMORY`: grave as configurações e encolha o cache de imagens.
   - O `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` (padrão "1") bloqueia o laço de eventos em segundo plano. Mantenha, mas as threads de rede e áudio **continuam rodando**: elas precisam ver o estado "em segundo plano" e parar.
3. **Vulkan ou GLES:** o `display_sdl.cpp` já tenta Vulkan e cai no `SDL_Renderer`, que no Android usa GLES. Confira as duas rotas: Vulkan num Android real e GLES forçando a falha do Vulkan.
4. **Áudio:** o SDL3 usa AAudio. Meça a fila (`queued ms`) como no PS5. O `audio_out_sdl.cpp` deve funcionar como está.
5. **Controle:** Xbox por Bluetooth (o InputDevice do Android já mapeia) e DualSense (pelo HIDAPI do SDL; vibração e giroscópio exigem `SDL_HINT_JOYSTICK_HIDAPI_PS5` e permissão Bluetooth no Android 12+).
6. **Botão Voltar:** o SDL entrega o Voltar do Android como tecla `SDL_SCANCODE_AC_BACK`. Ligue `SDL_HINT_ANDROID_TRAP_BACK_BUTTON=1`, para o app não fechar, e mapeie para "voltar" (o B da interface) em `input/keyboard_nav.cpp` ou `kbm_sdl.cpp`. Em transmissão, o Voltar abre o menu in-game em vez de sair.
7. **Tela:** `SDL_DisableScreenSaver()` durante a transmissão (no Android isso liga o `FLAG_KEEP_SCREEN_ON`); modo imersivo (`SDL_SetWindowFullscreen`); `SDL_HINT_ORIENTATIONS="LandscapeLeft LandscapeRight"`.
8. **Perfil "handheld": NÃO ligue no Android.**
   - O `platform::isHandheld()` dispara, na primeira abertura, o perfil do Steam Deck (`app_core.cpp:322–334`). Esse perfil **troca a resolução para 720p** e desliga o Anime4K Restore.
   - Pela decisão do usuário, a resolução é escolha dele e a plataforma nunca a rebaixa (Apêndice I.1). Por isso, no Android, `isHandheld()` **continua devolvendo false**, como hoje, em celular, tablet, portátil (Anbernic RG 557) e TV.
   - O app abre com o padrão normal (1080p) e a pessoa escolhe.
   - **O tipo de aparelho (TV × resto) continua útil para outras coisas:** mostrar ou não o controle de toque e a navegação só por D-pad. Para isso, use uma informação separada, por exemplo `platform::formFactor()` ou uma capacidade, alimentada pela `MainActivity` (`UiModeManager.getCurrentModeType() == UI_MODE_TYPE_TELEVISION`). **Nunca** reaproveite o `isHandheld()` para isso.

**Critério de pronto do A2:** num aparelho real (pergunte ao usuário qual tem), login, catálogo, transmissão de 5 minutos com som e controle, sair para a tela inicial e voltar sem crash. Com os números de `decodeMs`, `onScreenMs`, quadros pulados e fila de áudio no log.

### A3: toque (celular sem controle)

1. **Navegar a interface por toque.** O SDL3 sintetiza eventos de mouse a partir do toque (`SDL_HINT_TOUCH_MOUSE_EVENTS`, padrão ligado), e o desktop já tem navegação por mouse (`shell::MouseEvent` em `shell.h`, commit a349ac7). Então tocar já clica. Falta a **rolagem por arrasto**: converter `SDL_EVENT_FINGER_MOTION` vertical em rolagem das grades (`ui/app_ui_input.cpp`). Teste numa tela de 6 polegadas: os alvos de toque precisam ter ≥ 48 dp.
2. **Teclado:** `shell::setTextInput(true)` deve chamar `SDL_StartTextInput(window)`, que abre o teclado virtual do Android. Confira na busca e no login.
3. **Controle virtual na tela** (para os jogos, durante a transmissão):
   - Desenhe com o canvas da interface (`ui/canvas`) como overlay (`display::setOverlay`), com transparência.
   - Converta os toques (`SDL_EVENT_FINGER_*`, multitoque com `fingerID`) num `input::ControllerState` e injete. **Já existe uma entrada de controle virtual:** `setVirtualGamepad(playerIndex, ControllerState)` em `src/app/web_server.h:42`, usada pelo controle da WebUI no celular. Reaproveite esse caminho em vez de criar outro.
   - Layout padrão: analógico esquerdo flutuante, ABXY, LB/RB/LT/RT, View/Menu, e um botão para esconder. A configuração do layout fica para depois.
   - Esconda automaticamente quando um controle físico for conectado; mostre quando a pessoa tocar na tela sem controle.
   - Só no Android: use uma capacidade nova (por exemplo `shell::caps().touchScreen`) e no-op nas outras plataformas.
4. **Layouts de toque oficiais do xCloud:** fica fora deste trabalho. O protocolo existe: `stream_session.cpp:273` envia `touchInputEnabled=false`. Mas é preciso baixar e desenhar o layout de cada jogo (Touch Adaptation Kit). Documente como pendência; não implemente sem pedido.

### A4: decodificação por hardware (MediaCodec)

Reescreva o [src/media/hw_decoder_android.cpp](src/media/hw_decoder_android.cpp). A interface `HwDecoder` (`init(maxW, maxH)`, `decode(data, len, pts, Picture&)`) entrega o quadro **na memória da CPU** (`Picture::data`, `pitch`, `format`), e o `decoder.cpp` manda para `display::drawNv12()`.

**Etapa 1, com cópia** (funciona em todo aparelho; é o que entra primeiro):
- `AMediaCodec_createDecoderByType("video/avc")`, configurado com `AMediaFormat` (largura, altura, mime) e **saída para um `AImageReader`** com `AIMAGE_FORMAT_YUV_420_888`.
  - Por quê: o ByteBuffer do MediaCodec tem formatos de cor de fabricante (NV12, NV21, tiled de Qualcomm `QOMX_COLOR_FormatYUV420PackedSemiPlanar32m`) com `stride` e `slice-height` alinhados. O `AImageReader` com `YUV_420_888` dá planos com `rowStride` e `pixelStride` padronizados (`AImage_getPlaneData`, `AImage_getPlaneRowStride`, `AImage_getPlanePixelStride`).
  - Com `pixelStride == 2` e os planos U/V intercalados, o quadro é NV12 ou NV21: entregue por `drawNv12` (trocando U/V se for NV21). Com `pixelStride == 1`, use `drawYuv420`.
  - Respeite o recorte (`AImage_getCropRect`): 1080p costuma vir em 1088 linhas.
- **Baixa latência** (sem isso o decodificador segura vários quadros):
  - `AMediaFormat_setInt32(fmt, "low-latency", 1)`, o `KEY_LOW_LATENCY` do Android 11+ (ignorado onde não existe);
  - `AMediaFormat_setInt32(fmt, "priority", 0)` (tempo real);
  - as chaves de fabricante. A referência é o `MediaCodecHelper.java` do **moonlight-android** (GPL-3.0, como o PSBox), que mantém a lista testada por SoC. Exemplos: Qualcomm `vendor.qti-ext-dec-low-latency.enable=1` e `vendor.qti-ext-dec-picture-order.enable=1`; Exynos `vendor.rtc-ext-dec-low-latency.enable=1`. Copie a lista de lá **com o crédito no código**; não invente chaves.
  - Alimente a entrada sem esperar (timeout 0 em `dequeueInputBuffer`) e drene a saída a cada quadro.
- **SPS/PPS:** o xCloud manda SPS/PPS dentro do fluxo (Annex B, com start codes). A maioria dos decodificadores aceita assim. Se algum aparelho recusar, extraia SPS/PPS do primeiro IDR e passe como `csd-0`/`csd-1` no `configure`. Reconfigure o codec se a resolução mudar no meio da transmissão.
- **Recuperação:** em erro do codec (`AMEDIA_ERROR_*`, `AMediaCodec_getOutputBuffer` falhando), devolva `Picture::error = true`. O `decoder.cpp` já tem a lógica de pedir um key frame novo (resync) e de cair no software.
- **Medição obrigatória** (AGENTS.md §7): A/B no mesmo aparelho, mesmo jogo, mesma cena, 60 ou 120 s, comparando software e MediaCodec: `decodeMs`, `onScreenMs`, quadros pulados, temperatura e bateria. Só ligue o hardware como padrão se ganhar.

**Etapa 2, zero-copy** (otimização; só depois da etapa 1 estável e medida):
- `AImageReader` com `AIMAGE_FORMAT_PRIVATE` e uso `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE` → `AImage_getHardwareBuffer` → importar no Vulkan com `VK_ANDROID_external_memory_android_hardware_buffer`:
  1. `vkGetAndroidHardwareBufferPropertiesANDROID`;
  2. `VkExternalFormatANDROID`;
  3. `VkSamplerYcbcrConversion`;
  4. amostrar no shader do `gpu.cpp`.
- Isso exige uma função nova em `display.h`, algo como `drawExternalFrame(const ExternalFrame&)` com no-op nas outras plataformas, mais uma capacidade. Varia muito por GPU (Adreno, Mali, PowerVR): teste em pelo menos dois fabricantes antes de ativar por padrão.

### A5: rede, microfone e sistema

- **Lock de Wi-Fi de baixa latência:** `WifiManager.createWifiLock(WIFI_MODE_FULL_LOW_LATENCY, "psbox")` (API 29), adquirido no começo da transmissão e liberado no fim. Ele desliga a economia de energia e as varreduras do Wi-Fi, a mesma família de travadas investigada no PS5 (Apêndice I.4).
  - Implemente em Java na `MainActivity`, como método estático `setLowLatencyWifi(boolean)`.
  - Chame do nativo por JNI: `SDL_GetAndroidJNIEnv()` + `SDL_GetAndroidActivity()` + `CallStaticVoidMethod`.
  - Exponha como `platform::setLowLatencyNetwork(bool)`, a mesma função do Windows (W3), no-op nas outras plataformas.
  - Permissões `WAKE_LOCK` e `ACCESS_WIFI_STATE`.
- **Filler de Wi-Fi** (`wifi_filler.cpp`): é POSIX e deve funcionar no Android sem mudança. Confirme pelo log ("wifi filler: on, N a second") num aparelho em Wi-Fi com travadas.
- **Microfone:** antes de abrir a captura, `SDL_RequestAndroidPermission("android.permission.RECORD_AUDIO", cb, userdata)`. Se for negado, esconda as opções de voz pela capacidade (`audio_in`), sem erro.
- **Taxa de atualização:** em telas de 90/120 Hz, `ANativeWindow_setFrameRate(window, 60.0f, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE)` (API 30) durante a transmissão de 60 fps evita judder. O `ANativeWindow*` sai das propriedades da janela do SDL (`SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER`). Ligue isso à capacidade `highRefreshSwitch` existente.
- **Recorte da câmera e barras:** use a área segura do SDL (`SDL_GetWindowSafeArea`) no layout da interface, sem mexer no layout do PS5.

### A6: Android TV, assinatura e release

- **Android TV:**
  - `android:banner` (320×180) é obrigatório para o `LEANBACK_LAUNCHER`;
  - navegação inteira pelo D-pad do controle remoto (setas, OK = A, Voltar = B), sem depender de toque;
  - nada de controle virtual na TV (`UI_MODE_TYPE_TELEVISION`).
- **Assinatura:** fica na seção 7.3. **A keystore de release é o ativo mais crítico deste trabalho.**
- **Release:** fica na seção 7.1.

---

## 7. Release, CI e distribuição

### 7.1 Como os arquivos novos entram na release (decisão do usuário)

Hoje a release tem exatamente 10 arquivos (AGENTS.md §6). **Antes de publicar qualquer arquivo de Windows ou Android, pergunte ao usuário** qual das duas opções ele quer:

- **Opção A, build de teste (recomendada até W3 e A4 estarem prontos):** os arquivos vão para uma **pre-release** separada (`vX.Y.Z-windows-beta1`, `vX.Y.Z-android-beta1`) ou só como artefato do CI. A release oficial continua com os 10 arquivos e o `release-verify.sh` não muda.
- **Opção B, plataforma oficial:** a release passa a ter 14 arquivos:
  - `PSBoxCloudGaming[Windows]-vX.Y.Z.zip` e `.sha256`;
  - `PSBoxCloudGaming[Android]-vX.Y.Z.apk` e `.sha256`.

  Mude, no mesmo PR:
  1. o **AGENTS.md §6**: a lista dos arquivos obrigatórios e o texto "exatamente 10";
  2. **`tools/release-verify.sh`**: um bloco "Target 4: Windows" e "Target 5: Android", no estilo do bloco do Linux (linhas ~147–149, `check_file` com o nome, o padrão aceito e o tamanho mínimo), e o cabeçalho que lista os arquivos (linhas 5–17);
  3. **`tools/publish-release.sh`**: a fase 4 espera o AppImage do CI. Faça esperar também o zip do Windows e o APK, e remova os nomes intermediários do CI (`PSBox-*-windows-x86_64.zip`, `PSBox-*-android.apk`), como já faz com `PSBox-*-linux-x86_64.*`;
  4. **`notes.md`**: as tabelas bilíngues "Attached Files" / "Arquivos Anexados" e as seções "How to Install" / "Como Instalar" ganham Windows e Android. O `release-verify.sh` confere a presença dessas tabelas.

  A regra dos colchetes (`#label` no `gh release upload`) vale para os nomes novos.

### 7.2 CI

Acrescente ao [.github/workflows/ci.yml](.github/workflows/ci.yml) dois jobs, no modelo do `linux-app`:

**`windows-app`** (`runs-on: ubuntu-24.04`):
1. Instala `cmake ninja-build nasm wine64` (ou `wine`, conforme a imagem).
2. Restaura do cache, ou baixa, o llvm-mingw fixo (`tools/windows/fetch-toolchain.sh`, com sha256 conferido) e o `SDL3-devel-3.4.18-mingw`.
3. Restaura do cache o FFmpeg (`~/.cache/xcloud-ps5/ffmpeg-windows`), com a chave pelo hash de `tools/build-ffmpeg.sh` + o commit de `deps/ffmpeg`, como o job do Linux faz.
4. Configura `build-win` com o `cmake/windows.toolchain.cmake` e roda `ninja -C build-win`.
5. Roda `wine build-win/xcloud-tests.exe`.
6. Roda `SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy wine build-win/psbox.exe --selftest ...` (o wine sem display funciona com o driver offscreen do SDL; se não funcionar, rode com `xvfb-run`).
7. Roda `bash tools/desktop/package-windows.sh build-win build-win/pkg`.
8. Envia o artefato: `actions/upload-artifact`, `name: psbox-windows-x86_64`.

**Opcional:** um job `windows-selftest` em `windows-latest` que baixa o artefato e roda `psbox.exe --selftest` num Windows de verdade (sem GPU: o SDL cai no renderizador por software, o que basta para provar que abre, renderiza as telas e não crasha).

**`android-app`** (`runs-on: ubuntu-24.04`):
1. `actions/setup-java` (Temurin 17).
2. Fixa o NDK: `sdkmanager "ndk;27.2.12479018"`. As imagens do GitHub já têm o Android SDK, mas não necessariamente essa versão do NDK.
3. `git submodule update --init deps/SDL`.
4. Restaura do cache os dois FFmpeg (`ffmpeg-android-arm64-v8a`, `ffmpeg-android-x86_64`).
5. `cd android && ./gradlew :app:assembleRelease` (assinado só se os segredos existirem, 7.3) e `:app:assembleDebug`.
6. Envia o artefato: `name: psbox-android`, com o APK.

**Opcional, caro:** selftest no emulador com `reactivecircus/android-emulator-runner` (x86_64, API 34). Ligue só se o tempo do CI permitir.

**Workflows de release:**
- O [.github/workflows/release-linux.yml](.github/workflows/release-linux.yml) roda quando uma release é publicada, compila o AppImage, faz `gh release upload` e o `publish-release.sh` assina depois com `tools/desktop/sign-linux-release.sh`.
- Faça `release-windows.yml` e `release-android.yml` no mesmo modelo, ou junte os três num `release-desktop.yml` com matriz.
- Na opção A (7.1), eles só rodam em tags `*-beta*`.

**Fixe as versões das actions** (`@v4`, `@v7` etc.), como o repositório já faz.

### 7.3 Assinatura

**Windows:**
- **Sem assinatura de código:** o Windows mostra "O Windows protegeu o computador" (SmartScreen), e antivírus podem marcar o exe como suspeito, o que é comum em binário novo sem assinatura. Documente no README do Windows ("Mais informações → Executar assim mesmo").
- **Com assinatura:** exige um certificado pago (OV ou EV) ou o Azure Trusted Signing, que exige verificação de identidade. **É decisão do usuário, com custo.** Não contrate nem crie conta sem ele.
- **`.sig` ECDSA:** independente disso, gere o `.sig` com a mesma chave da release do PS5 (veja como `tools/release.sh` assina o `PPSA99810.zip` e como `sign-linux-release.sh` assina o AppImage). Ele é necessário para um futuro atualizador no Windows.

**Android: a keystore é o ponto mais crítico de todo o trabalho.**
- Um APK só atualiza por cima de outro assinado com a **mesma** chave. Perder a keystore ou a senha significa que todos os usuários precisam **desinstalar**, perdendo o login e as configurações, para instalar a próxima versão. Não há recuperação.
- **Quem cria a keystore é o usuário**, ou você cria e entrega a ele, com instruções explícitas de guardar duas cópias fora do repositório e do CI:

```bash
keytool -genkeypair -v -keystore psbox-release.jks -alias psbox -keyalg RSA -keysize 4096 -validity 36500
```

- **Nunca** commite o `.jks` nem as senhas. No CI, entram como segredos do GitHub: `ANDROID_KEYSTORE_BASE64` (o arquivo em base64), `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD`. O `build.gradle.kts` lê das variáveis de ambiente num `signingConfigs.create("release")`, e só aplica se existirem. Assim, o build de quem clona o repositório continua funcionando, em debug.
- Acrescente `*.jks`, `*.keystore` e `keystore.properties` ao `.gitignore`.
- O CI que faz o release precisa conferir que o APK final está assinado com a chave esperada: `apksigner verify --print-certs` + a impressão digital SHA-256 do certificado gravada no workflow. Isso pega a troca acidental de chave.

### 7.4 Atualização dentro do app

- Hoje `shell::canSelfUpdate()` é true só no PS5. **Não ligue nas outras plataformas neste trabalho.** Com false, uma versão nova só aparece no log e nunca é oferecida (multiplatform.md, "Capabilities").
- **Mais tarde, no Windows:** baixar o zip, verificar o `.sig`, renomear o `psbox.exe` em uso para `psbox.exe.old` (o Windows deixa renomear um exe aberto, mas não sobrescrever), extrair, reiniciar e apagar o `.old` na próxima execução.
- **Mais tarde, no Android:** o app não troca o próprio APK em silêncio. Ele baixa o APK e abre o instalador do sistema (`ACTION_VIEW` com `FileProvider` e a permissão `REQUEST_INSTALL_PACKAGES`). O usuário confirma. A assinatura igual é garantida pelo próprio Android.

### 7.5 Distribuição

- **Windows:** o zip na release do GitHub. Nada de instalador MSI/NSIS por enquanto (o README já descreve a edição portátil).
- **Android:**
  - o APK na release do GitHub (universal, com `arm64-v8a` + `x86_64`), instalável por sideload, pelo **Obtainium** (que acompanha releases do GitHub) e nas TVs por "Downloader" ou adb;
  - **a Play Store não é recomendada:** cliente não oficial de um serviço da Microsoft, com risco pelas políticas de representação e de propriedade intelectual, e a conta de desenvolvedor do usuário em jogo. Não publique sem pedido explícito;
  - F-Droid exige build reproduzível e sem blobs; fora de escopo.
- **Catálogo do PS5** (`darkxex/ps5-homebrew-catalog`): não tem nada a ver com Windows/Android. Não mexa.

---

## 8. Armadilhas conhecidas

### Gerais

1. **Espaço no caminho do repositório (`PS5 Ports`).** Use aspas em todo comando. O configure do FFmpeg não aceita espaço: o `build-ffmpeg.sh` compila num cache por um link simbólico; mantenha esse truque nos alvos novos.
2. **Cache de FFmpeg velho.** O build só roda o `build-ffmpeg.sh` se faltar `<build>/ffmpeg/lib/libswresample.a`. Se você mudar flags do FFmpeg, apague `<build>/ffmpeg` **e** `~/.cache/xcloud-ps5/ffmpeg-<alvo>`, ou o build usa a versão antiga sem avisar.
3. **O PS5 não é um Linux.** O `posix_common.cpp` também serve o PS5 (FreeBSD do SDK de payload). Mudanças em `posix_common.cpp`, `http.cpp` ou nas rotinas de arquivo afetam o console. Preserve as chamadas existentes nos caminhos POSIX e compile `build-ps5` a cada mudança.
4. **Submódulo `deps/libdatachannel` "sujo".** Ele aparece modificado por patches locais do PS4 (`ps4/patches/`, aplicados por `cmake/apply_patch.cmake`). Não commite esse estado e não rode `git submodule update --force` sem entender o efeito no build do PS4.
5. **Capacidades e o teste de linhas visíveis.** Ao acrescentar capacidade ou opção de configuração, rode o `xcloud-tests`: um teste garante que, com as capacidades do PS5, aparecem todas as linhas de Configurações na mesma ordem. O AGENTS.md §4.2 "blinda" a ordem dos 9 itens principais de Configurações.

### Windows

6. **`<windows.h>` define macros com nomes comuns.**
   - `rpcndr.h` (que entra via `objbase.h` e `shlobj.h`, por exemplo para `SHGetKnownFolderPath`) faz `#define small char`. **O código tem parâmetros chamados `small`:** `controller.h:83` (`setRumble(uint8_t large, uint8_t small, ...)`), `controller_backend.h:53` e `pad_usb.h:156`, `pad_usb.h:303–304`. Se esse header vier antes, `uint8_t small` vira `uint8_t char` e o build quebra com um erro confuso.
   - Também há `min`, `max`, `ERROR`, `DELETE`, `IN`, `OUT`, `near`, `far` e `interface`.
   - Solução: inclua `<windows.h>` **só** nos `.cpp` de plataforma (nunca num header comum), sempre com `WIN32_LEAN_AND_MEAN` e `NOMINMAX`. Se precisar de `shlobj.h`, inclua por último e dê `#undef small` logo depois. Não renomeie os parâmetros do código comum por causa disso.
7. **Winsock:** sete armadilhas na seção 5 (W1.3). A pior é o `SO_RCVTIMEO` com `DWORD` e o estado indefinido depois do timeout.
8. **`timeBeginPeriod(1)`:** sem ele, `Sleep(1)` dura ~15,6 ms e o ritmo de quadros e a fila de áudio sofrem. Ligue em `platform::init()` e desligue em `shutdown()`.
9. **CRT do `SDL3.dll`:** o pacote mingw oficial do SDL pode usar um CRT diferente do llvm-mingw UCRT. Isso não importa se nenhuma memória cruzar a fronteira: o que o SDL aloca, libere com `SDL_free`, e vice-versa. Nunca passe um `FILE*` entre os dois.
10. **O wine não prova nada sobre GPU, controles, áudio ou latência.** Ele só prova que compila, liga e roda a lógica. Não diga "funciona no Windows" com base no wine.
11. **Defender e SmartScreen:** um exe novo sem assinatura pode ser bloqueado ou posto em quarentena. Se um testador disser "o exe sumiu", é isso.

### Android

12. **`android:allowBackup="true"` no manifesto atual.** Com isso, o backup automático do Google leva a pasta de dados, **com o token da conta Microsoft e a chave do selo**, para a nuvem e restaura em outro aparelho. Troque para `android:allowBackup="false"`, com `android:dataExtractionRules`/`fullBackupContent` excluindo tudo. O token não pode sair do aparelho (o PS5 sela a conta por perfil pelo mesmo motivo).
13. **Páginas de 16 KB (Android 15+).** Aparelhos novos usam páginas de memória de 16 KB, e bibliotecas `.so` alinhadas em 4 KB não carregam neles. Com o NDK r27, ligue `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` (ou `-Wl,-z,max-page-size=16384`) para **todas** as `.so` (`libmain`, `libSDL3`, `libc++_shared` já vem certa) e confira o APK com `zipalign -c -P 16 -v 4 app.apk`.
14. **A thread do `main()` nativo não é a thread de interface do Android.** O SDL roda o `SDL_main` numa thread própria. Chamadas JNI precisam do `JNIEnv` da thread atual (`SDL_GetAndroidJNIEnv()` resolve). Coisas de interface Java (Toast, WifiLock) devem ir para a thread de UI por `runOnUiThread` do lado Java.
15. **O log não vai para o logcat.** O `src/util/log.cpp` não tem nada de `__ANDROID__`: no Android, o log só vai para o `xcloud.log` na pasta privada. Acrescente `__android_log_write` (tag `psbox`) no A1, antes de qualquer outra coisa.
16. **Afinidade de threads:** o Linux usa `pthread_setaffinity_np` (`platform_linux.cpp:243`), que não existe no Bionic. O `platform_android.cpp` já tem a própria `setThreadAffinity`. Não copie a do Linux; no Android, afinidade fixa costuma piorar (big.LITTLE): deixe no-op ou só dê prioridade.
17. **`getifaddrs`** (`posix_common.cpp:103`, em `localIPv4`) existe no Bionic a partir da API 24. Com minSdk 29, ok.
18. **Superfície destruída** (A2): o Vulkan precisa recriar superfície e swapchain ao voltar do segundo plano. Teste explicitamente a sequência botão Home → voltar ao app, **durante** uma transmissão e **durante** a tela de login.
19. **Rotação e redimensionamento:** em tela dividida e janelas livres (tablets, Chromebooks, Samsung DeX), a janela muda de tamanho. O `display_sdl` precisa tratar `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` sem recriar o app.
20. **`x86_64` sem asm:** com `--disable-x86asm`, o FFmpeg fica lento. Serve para o emulador; não meça desempenho nele.
21. **Formatos de cor do MediaCodec:** nunca assuma NV12 com `stride == width`. Leia sempre os strides e o recorte (A4).
22. **`__linux__` vale no Android.** `controller_sdl.cpp:49/105` (o contorno de hidraw do Linux) e `crash_guard.cpp:243` são afetados: no arm64, o crash sai sem PC. Detalhes e correções no Apêndice H.2.

---

## 9. Checklists de pronto por fase

Cada fase é um PR. Ele só fica pronto quando todos os itens estão marcados e escritos na descrição do PR, com a saída dos comandos.

**Comum a todas as fases:**
- [ ] `ninja -C build-desktop` + `./build-desktop/xcloud-tests` (Linux) passam.
- [ ] `--selftest` do Linux passa.
- [ ] `ninja -C build-ps5 xcloud_app` + `tools/ps5/link.sh build-ps5` geram o `eboot.bin`.
- [ ] `ninja -C build-ps4` compila.
- [ ] `bash tools/check-platform-boundaries.sh` passa.
- [ ] Nenhum arquivo proibido no diff (`reddit/`, `account.json`, `HANDOFF_NEXT_SESSION.md`, `QUALITY_CANDIDATES.md`, `.jks`, tokens).
- [ ] Strings novas nos 6 arquivos `src/ui/i18n/strings_*.inc`.
- [ ] O [docs/multiplatform.md](docs/multiplatform.md) foi atualizado (tabela de plataformas, mapa de backends, milestones) e este handoff marca a fase como feita.

**W1:**
- [ ] `src/net/socket.h`/`.cpp` e `src/util/fs.h`/`.cpp` criados, com os caminhos POSIX idênticos aos de antes.
- [ ] Os arquivos da seção 5 (W1.3/W1.4) migrados.
- [ ] Troféus só nos consoles (Apêndice B.2):
  - `trophy_off.cpp` em Linux, Windows e Android;
  - os 5 arquivos reais só em ps5/ps4;
  - `UiCaps::trophies` escondendo as linhas 24 e 25;
  - os 4 testes de troféus e os `#include "trophy/..."` apagados do `test_main.cpp` (decisão do usuário), com `grep -n "trophy::" tests/test_main.cpp` vazio;
  - `nm` do `psbox` sem símbolos de troféus reais.
- [ ] O crash guard dividido (W1.6) e a exceção do `check-platform-boundaries.sh` removida.
- [ ] A regra nova de headers POSIX no script (W1.7).
- [ ] O `randomBytes` sem fallback para `std::rand` (seção 2.2).
- [ ] `wine build-win/xcloud-tests.exe`: todos passam.

**W2:**
- [ ] `psbox.exe --selftest` sob wine termina e gera as capturas (conferidas por você).
- [ ] O manifesto (UTF-8, DPI) e o ícone embutidos.
- [ ] O `package-windows.sh` falha sem binário e o zip tem exe + `SDL3.dll` + assets + licenças.
- [ ] O job `windows-app` está verde no CI.

**W3** (com hardware):
- [ ] A lista da seção 5 (W3) validada num Windows real, com os números do log.
- [ ] O modo de streaming do WLAN, com A/B.
- [ ] O OK do usuário.

**A1:**
- [ ] O submódulo SDL fixado em `release-3.4.18`.
- [ ] A plataforma `android` no CMake e o `psbox` como `libmain.so`.
- [ ] O FFmpeg `android-<abi>`.
- [ ] O Gradle refeito, com wrapper commitado.
- [ ] A `MainActivity` (SDLActivity).
- [ ] O manifesto com `allowBackup=false`.
- [ ] Os ícones.
- [ ] A pasta de dados pelo SDL e os assets extraídos com lista e versão.
- [ ] O log no logcat.
- [ ] O selftest no emulador com as capturas conferidas.
- [ ] O job `android-app` verde.

**A2** (com aparelho):
- [ ] Login, catálogo e transmissão de 5 minutos.
- [ ] Ciclo de vida (Home → voltar) sem crash, durante a transmissão e durante o login.
- [ ] O botão Voltar.
- [ ] Os números do log.

**A3:**
- [ ] Toque na interface com rolagem.
- [ ] Teclado virtual.
- [ ] O controle virtual na transmissão, que some com controle físico.
- [ ] Nada disso aparece no PS5, no Linux ou no Windows.

**A4:**
- [ ] O MediaCodec com `AImageReader` `YUV_420_888`, as chaves de baixa latência (com crédito ao Moonlight) e a recuperação de erro.
- [ ] A/B software × hardware (AGENTS.md §7) no mesmo aparelho, com a tabela no PR.

**A5:**
- [ ] O lock de Wi-Fi de baixa latência (`platform::setLowLatencyNetwork`).
- [ ] A permissão do microfone.
- [ ] A taxa de quadros da janela.
- [ ] A área segura.

**A6:**
- [ ] Banner e navegação por D-pad na Android TV.
- [ ] O APK de release assinado no CI.
- [ ] A conferência da impressão digital do certificado.
- [ ] A decisão de release do usuário (7.1) aplicada.

---

## 10. Referências de código

| O quê | Onde |
|---|---|
| Plano multiplataforma (M0, M1, M3) e regra de capacidades | [docs/multiplatform.md](docs/multiplatform.md) |
| Resumo deste estudo | [docs/windows-android.md](docs/windows-android.md) |
| Regras do repositório e de release | [AGENTS.md](AGENTS.md) §2–§9 |
| Escolha da plataforma | [cmake/platform.cmake](cmake/platform.cmake) |
| Dependências | [cmake/deps/](cmake/deps/) (`ffmpeg.cmake`, `sdl3.cmake`, `libdatachannel.cmake`, `mbedtls.cmake`, `mbedtls_user_config.h`) |
| Build do FFmpeg por alvo | [tools/build-ffmpeg.sh](tools/build-ffmpeg.sh) |
| Bibliotecas e alvos | [CMakeLists.txt](CMakeLists.txt): `xcloud_core` ~32, `xcloud_stream` ~89, `xcloud_media` ~106, `xcloud_ui` ~138, `xcloud_appcore` ~186, plataforma não-console ~289, backends nulos ~300, app SDL3 ~336–380 |
| API de plataforma | [src/platform/platform.h](src/platform/platform.h); Linux: `src/platform/linux/platform_linux.cpp` + `src/platform/posix_common.cpp`; Windows: `src/platform/windows/platform_windows.cpp`; Android: `src/platform/android/platform_android.cpp` |
| Display e GPU no desktop | `src/display/display.h`, `src/display/display_sdl.cpp` (Vulkan + reserva SDL_Renderer), `src/display/gpu.cpp` (FSR, Anime4K, RCAS, deband) |
| Decodificador | `src/media/decoder.cpp` (FFmpeg, lista de hwaccel ~606, cópia para CPU ~790), `src/media/hw_decoder.h`, `src/media/hw_decoder_android.cpp` |
| App de desktop | `src/app/desktop_main.cpp` (`main`, argumentos), `src/app/shell_desktop.cpp`, `src/app/desktop_selftest.cpp`, `src/app/shell.h` |
| Entrada SDL | `src/input/sdl_input.cpp`, `src/input/controller_sdl.cpp`, `src/input/kbm_sdl.cpp`, `src/input/keyboard_nav.cpp` |
| Áudio SDL | `src/media/audio_out_sdl.cpp`, `src/media/audio_in_sdl.cpp` |
| Rede (código a portar) | `src/net/http.cpp`, `src/app/web_server.cpp`, `src/app/worker_diagnostics.cpp`, `src/stream/stream_session.cpp`, `src/stream/net_diag.cpp`, `src/stream/wifi_filler.cpp` |
| Arquivos (código a portar) | `src/app/updater.cpp`, `src/app/app_state.cpp`, `src/ui/image_cache.cpp`, `src/app/worker.cpp`. Os troféus **não** são portados: saem das plataformas sem console (Apêndice B.2) |
| Troféus (só PS5/PS4) | `src/trophy/*`; a implementação vazia nova `src/trophy/trophy_off.cpp`; as linhas de Configurações em `src/ui/app_ui.cpp` (`visibleSettingsRows`, linhas 24 e 25) |
| Crash guard | `src/app/crash_guard.cpp` |
| Controle virtual existente (WebUI) | `src/app/web_server.h:42` `setVirtualGamepad` |
| Identificação no xCloud (Android/phone × Windows/desktop) | `src/xcloud/gssv.cpp` ~40–60 |
| Toque no protocolo do xCloud | `src/stream/stream_session.cpp:273` (`touchInputEnabled`), `src/stream/input_packet.h:85` |
| Selo da conta (machine-id ou arquivo de chave) | `src/auth/account_seal.cpp` ~36–52 |
| CI | `.github/workflows/ci.yml` (jobs `build`, `linux-app`), `.github/workflows/release-linux.yml` |
| Scripts de release | `tools/release.sh`, `tools/publish-release.sh`, `tools/release-verify.sh`, `tools/desktop/package-appimage.sh`, `tools/desktop/package-windows.sh`, `tools/desktop/sign-linux-release.sh` |
| Referência externa: MediaCodec de baixa latência | moonlight-android, `MediaCodecHelper.java` (GPL-3.0) |
| Referência externa: projeto Android do SDL | `deps/SDL/android-project/` (depois do A1.1) e `deps/SDL/docs/README-android.md` |

---

## 11. Perguntas para o usuário antes das fases com hardware

Não comece W3, A2 nem nenhuma publicação sem as respostas:

1. Você tem um PC com Windows 10/11 para testar? Qual GPU?
2. Você tem um aparelho Android? Qual modelo (celular, tablet, TV box, Android TV) e qual versão do Android?
3. Release: build de teste separado (opção A) ou plataforma oficial com 14 arquivos (opção B)? Veja 7.1.
4. Assinatura de código no Windows (paga): sim ou não? Quem cria e guarda a keystore do Android?

**Já decidido pelo usuário (2026-11-14), não pergunte de novo:**
- **Troféus:** exclusivos do PS5 e do PS4. Linux, Windows e Android não têm a lógica, e os testes de troféus do Linux são **apagados** (Apêndice B.2).
- **Resolução:** é opção do usuário (720p, 1080p, 1440p quando disponível, e 1080HQ), igual a hoje, em todas as plataformas. A plataforma nunca limita (Apêndice I.1).
- **Entrada no Android:** controle na tela (toque) **e** controles físicos USB e Bluetooth (GameSir, Xbox, DualSense, 8BitDo...), incluindo os controles embutidos de portáteis Android como o **Anbernic RG 557**. Os dois modos convivem (Apêndice L).

---

# Apêndices (detalhamento)

Os apêndices descem ao nível de linha de código. As linhas citadas são da v1.7.2 (`48de20b`). Se o arquivo mudou desde então, procure pelo trecho e não confie só no número.

## Apêndice A: inventário das chamadas de socket a portar (W1.3)

### A.1 `src/net/http.cpp` (HTTP/TLS do app inteiro: login, catálogo, APIs do xCloud)

| Linha | Hoje (POSIX) | No Windows | Cuidado |
|---|---|---|---|
| 18–26 | `#include <arpa/inet.h> <netdb.h> <netinet/in.h> <netinet/tcp.h> <sys/ioctl.h> <sys/socket.h> <sys/time.h> <poll.h> <unistd.h>` | só `#include "net/socket.h"` (que inclui `<winsock2.h> <ws2tcpip.h>` no Windows) | `winsock2.h` **antes** de qualquer `windows.h`, senão o `winsock.h` antigo entra e dá redefinições |
| 84–87 | `send(fd, buf, len, MSG_NOSIGNAL)` no callback de envio do mbedTLS | `send((SOCKET)fd, (const char*)buf, (int)len, 0)` | o `len` é `int` no Winsock: limite o tamanho ao máximo de `int` |
| 89–90 | `errno == EAGAIN/EWOULDBLOCK → MBEDTLS_ERR_SSL_TIMEOUT`; `EINTR → WANT_WRITE` | `lastSocketError()`: `WSAEWOULDBLOCK` **e `WSAETIMEDOUT`** → `MBEDTLS_ERR_SSL_TIMEOUT` | no Windows, o timeout de `SO_SNDTIMEO`/`SO_RCVTIMEO` devolve **`WSAETIMEDOUT`**, não `WSAEWOULDBLOCK`. Sem mapear isso, todo timeout vira erro fatal de conexão |
| 98–101 | `recv` + o mesmo mapeamento de erro | `recv((SOCKET)fd, (char*)buf, (int)len, 0)` + o mesmo mapeamento | idem |
| 110–111 | `setsockopt(SO_RCVTIMEO/SO_SNDTIMEO, struct timeval)` | `DWORD ms` | ver W1.3, armadilha 1. Melhor: `setRecvTimeout(s, ms)` no `socket.h` |
| 152–154 | `socket(AF_INET, SOCK_STREAM, 0)`, erro por `errno` | `socket()` devolve `INVALID_SOCKET`; o erro vem de `WSAGetLastError()` | a mensagem de log "errno N" passa a mostrar o código WSA: mantenha o texto "errno" para não quebrar as regras de triagem (`.github/triage/rules.json`), ou acrescente as regras novas |
| 159 | `TCP_NODELAY` | igual (`(const char*)&one`) | — |
| 160–161 | `SO_NOSIGPIPE` (só macOS/BSD) | não existe; o `#ifdef` já protege | — |
| 165 | `SO_RCVBUF` | igual | — |
| 168, 215 | `ioctl(fd, FIONBIO, &on)` (liga e desliga o não bloqueante em volta do `connect`) | `ioctlsocket(s, FIONBIO, &u_long)` | o PS5 usa este `ioctl`: no caminho POSIX, **mantenha `ioctl`**, não troque por `fcntl` |
| 175–177 | `connect` + `errno == EINPROGRESS/EWOULDBLOCK` | `WSAEWOULDBLOCK` (no Windows o connect não bloqueante devolve isso, não `EINPROGRESS`) | — |
| 185 | `poll(&pfd, 1, connectTimeout)` esperando o connect | `WSAPoll` ou `select` | ver W1.3, armadilha 2 (WSAPoll antes do Windows 10 2004) |
| 189, 199, 207, 437 | `close(fd)` | `closesocket` | — |
| 197 | `getsockopt(SO_ERROR)` | igual, com `(char*)&so_error` e `int len` | — |
| 464 | `poll(&pfd, 1, 0) == 0`: testa se uma conexão guardada (keep-alive) ainda está viva antes de reutilizar | `WSAPoll` com timeout 0 | se essa checagem falhar no Windows, o app reutiliza conexões mortas e o primeiro pedido depois de um tempo parado falha |

### A.2 `src/app/web_server.cpp` (WebUI: configurações e controle virtual pelo celular)

| Linha | Hoje | No Windows | Cuidado |
|---|---|---|---|
| 30–31 | `#ifndef MSG_NOSIGNAL #define MSG_NOSIGNAL 0` | igual | — |
| 76–77 | `signal(SIGPIPE, SIG_IGN)` com `#ifdef SIGPIPE` | o `#ifdef` já protege (não existe `SIGPIPE`) | — |
| 82–84 | `socket()` do servidor | `socket.h` | — |
| 89 | `SO_REUSEADDR` | **use `SO_EXCLUSIVEADDRUSE`** | no Windows, `SO_REUSEADDR` deixa **outro processo roubar a porta** enquanto o app escuta (semântica diferente do POSIX). É problema de segurança numa interface que controla o app |
| 96–105 | `bind`, `listen(8)` | igual; erro por `WSAGetLastError()` | na primeira escuta, o Windows mostra o aviso do firewall |
| 114, 127, 156 | `close` | `closesocket` | — |
| 126, 133 | `shutdown(fd, SHUT_RDWR)` | `shutdown(s, SD_BOTH)` | — |
| 147, 309 | `poll(..., 250)` no `accept` e `poll(..., 30)` no laço do WebSocket (~33 Hz) | `WSAPoll` | — |
| 152 | `accept` | igual; `socklen_t` → `int` | — |
| 165–166 | `SO_RCVTIMEO/SO_SNDTIMEO` com `timeval` no cliente | `DWORD ms` | armadilha 1 |
| 178, 318 | `recv` | igual com casts | — |
| 249, 288, 329, 333, 408 | `send(..., MSG_NOSIGNAL)` | `send(..., 0)` | linha 409: `errno != EAGAIN && != EWOULDBLOCK` → `wouldBlock(lastSocketError())` |
| 296 | `TCP_NODELAY` no WebSocket | igual | — |
| 311 | `errno == EINTR` depois do `poll` | não existe no Winsock; deixe o teste no caminho POSIX | — |

### A.3 `src/app/worker_diagnostics.cpp` (teste de conexão em Configurações)

| Linha | Hoje | No Windows | Cuidado |
|---|---|---|---|
| 220–249 | UDP: `socket` + `sendto` + `poll(900)` + `recv` + `close` (um teste de DNS/STUN) | `socket.h` | — |
| 265–291 | TCP: `socket` + **`fcntl(F_GETFL/F_SETFL, O_NONBLOCK)`** + `connect` + `EINPROGRESS` + `poll(timeoutMs)` + `getsockopt(SO_ERROR)` + `close` | `setNonBlocking(s, true)`, `inProgress(err)` | `fcntl` **não existe** no Winsock: este é o único lugar que usa `fcntl` para sockets. No PS5 ele funciona hoje: mantenha `fcntl` no caminho POSIX deste arquivo |

### A.4 `src/stream/stream_session.cpp`

- Só a linha 86 é POSIX: `inet_pton(AF_INET6, ...)`, para validar um candidato IPv6. No Windows, basta `<ws2tcpip.h>`.
- Os outros `send`/`close` do arquivo são métodos do libdatachannel (`ch->send`, `pc->close`) e **não mudam**.
- Confira os `#include` POSIX no topo e troque por `net/socket.h`.

### A.5 `src/stream/net_diag.cpp` e `src/stream/wifi_filler.cpp`

- Os dois já têm `#if !defined(_WIN32)` (net_diag: 21 e 67; wifi_filler: 18 e 30). No Windows compilam como stubs: o diagnóstico de rede e o filler ficam desligados.
- Depois do W1, porte de verdade usando o `socket.h`:
  - o filler faz `connect` não bloqueante em lote para o gateway e colhe as recusas (RST);
  - o net_diag lê `FIONREAD` a cada 2 ms (`bytesAvailable`) e escuta beacons UDP na porta 9123.

  Isso é W3 e precisa de A/B.

---

## Apêndice B: inventário das operações de arquivo (W1.4) e a decisão dos troféus (W1.5)

### B.1 Arquivos a portar para `xc::fs`

| Arquivo | Linhas | O que faz | Equivalente `xc::fs` | Cuidado |
|---|---|---|---|---|
| `src/app/updater.cpp` | 61, 65 | `stat` + `S_ISDIR` (é pasta? existe?) | `isDir`, `exists` | — |
| | 72 | `mkdir(d, 0777)` tolerando `EEXIST` | `makeDirs` | o 0777 é proposital no PS5 (pasta apagável por FTP); no Windows a permissão não se aplica |
| | 78–89 | apaga uma pasta recursivamente (`opendir`/`readdir`/`unlink`/`closedir`/`rmdir`) | `removeTree` (acrescente à API) | não siga links simbólicos (`std::filesystem::remove_all` não segue) |
| | 95–106 | lista arquivos recursivamente (relativos à raiz) | `listTree` (acrescente) | ignore `.` e `..` |
| | 148, 336–337 | `rename` (move entre `update.old/`, `staged/` e o destino) | `renameReplace` | é o coração da atualização atômica do PS5. **No PS5 nada muda.** No Windows o updater está desligado (`canSelfUpdate()==false`), mas o arquivo precisa compilar |
| | 319 | `writeFileAtomic` + `chmod(p, 0777)` | `platform::writeFileAtomic` + `fs::makeWorldWritable` (no-op no Windows) | — |
| | 342, 348, 381 | `unlink(update.journal)` | `removeFile` | — |
| `src/app/app_state.cpp` | 20–30 | lista a pasta de dados procurando configurações por perfil de usuário | `listDir` | — |
| | 46, 65–67 | `stat` + `std::rename(legacy, path)`: migra o `settings.json` antigo para o arquivo por perfil | `exists` + `renameReplace` | a migração só acontece uma vez: teste com um `settings.json` antigo na pasta |
| `src/ui/image_cache.cpp` | 101–102 | `mkdir` + `chmod 0777` do cache de capas | `makeDirs` + `makeWorldWritable` | — |
| | 175–184 | lista o cache e pega o tamanho de cada arquivo (`stat`, `S_ISREG`), para limpar os mais velhos | `listDir` + `fileSize` + `modifiedTime` | o orçamento vem de `platform::imageCacheBudget()` |
| | 194 | `unlink` | `removeFile` | — |
| `src/app/worker.cpp` | 710–711 | `unlink` de `library.json` e do cache da biblioteca | `removeFile` | — |

**API final sugerida para `src/util/fs.h`** (complementa a do W1.4):

```cpp
namespace xc::fs {
bool exists(const std::string& p);
bool isDir(const std::string& p);
bool isFile(const std::string& p);
bool makeDirs(const std::string& p);                          // mkdir -p; true if it already exists
bool removeFile(const std::string& p);                        // true if gone (also when it never existed)
bool removeTree(const std::string& p);                        // rm -rf, never following symlinks
bool renameReplace(const std::string& from, const std::string& to);
std::vector<std::string> listDir(const std::string& p);       // names, no "." / ".."
std::vector<std::string> listTree(const std::string& root);   // relative paths of files, recursive
bool fileSize(const std::string& p, uint64_t& out);
bool modifiedTime(const std::string& p, int64_t& unixSeconds);
void makeWorldWritable(const std::string& p);                 // chmod 0777 on POSIX (PS5: FTP), no-op on Windows
}
```

O caminho POSIX de cada função deve ser **exatamente** o código que existe hoje nos arquivos acima, só movido. Escreva testes unitários para `fs` no `xcloud-tests` (Apêndice G): criar, listar, renomear por cima, apagar recursivo e caminho com acento.

### B.2 Troféus: exclusivos do PS5 e do PS4 (decisão do usuário, 2026-11-14)

> **Regra:** Linux, Windows e Android **não têm lógica de troféus**. Os troféus são exclusivos do PS5 e do PS4. Isso vale também para o **Linux de hoje**: atualmente o `xcloud_core` compila os 5 arquivos de troféus em todas as plataformas (CMakeLists.txt, linhas 58–62), e o AppImage leva esse código. O trabalho abaixo tira o código do Linux também.

**O que existe em `src/trophy/` e onde passa a entrar:**

| Arquivo | O que é | PS5/PS4 | Linux/Windows/Android |
|---|---|---|---|
| `trophy_rules.cpp`, `trophy_registry.cpp` | regras e tabela de títulos conhecidos (lógica pura) | sim | **não** |
| `trophy_builder.cpp`, `trophy_sync.cpp`, `trophy_db.cpp` | construção dos arquivos, sincronização e banco | sim | **não** |
| `trophy_backend_null.cpp` | backend vazio, hoje ligado no desktop (linhas 307 e 363) | — | **não** (substituído pelo arquivo novo abaixo) |
| `src/platform/ps5/trophy_backend_ps5.cpp` | backend real do PS5 | PS5 | não |
| **novo** `src/trophy/trophy_off.cpp` | implementação vazia de **tudo** que o código comum chama | não | **sim** |

**O que o código comum chama** e o `trophy_off.cpp` precisa definir, sem efeito nenhum:

| Função | Usada em |
|---|---|
| `trophy::startTrophyThread`, `stopTrophyThread`, `setTrophyUserProvider`, `setTrophyContextProvider` | `src/app/app_core.cpp` (~372, ~376, ~473) |
| `trophy::requestTrophySync` (4×), `requestRemoveTrophies` (3×), `resumeTrophySync` (2×), `cancelTrophySync`, `isSyncing` | `app_core.cpp` (~1244, ~1250), `worker.cpp`, `worker_stream.cpp` |
| `trophy::dispatchStreamPoll` | `worker_stream.cpp` |
| `trophy::runPsboxNativeSelfTest` | `src/app/autoplay.cpp:1121` |
| `trophy::normalizeTrophyName`, `findKnownTitle` | `app_core.cpp` (~411–428), dentro da função passada para `setTrophyContextProvider` |

Ninguém fora de `src/trophy/` chama `syncEngine()`. Só `trophy_sync.cpp` e `trophy_backend_ps5.cpp` incluem `trophy_builder.h` e `trophy_db.h`. Os headers (`trophy_sync.h`, `trophy_types.h` etc.) continuam incluídos pelo código comum, por causa dos tipos (`TrophySyncContext`): isso é só declaração, sem lógica.

**Passo a passo:**
1. **`src/trophy/trophy_off.cpp`** (compilado só fora dos consoles) define, sem efeito, todas as funções da tabela acima:
   - `isSyncing()` devolve false;
   - `titlesDone()`/`titlesTotal()` devolvem 0;
   - `request*`, `resume*`, `cancel*`, `start*`/`stopTrophyThread` e `dispatchStreamPoll` não fazem nada (`stopTrophyThread` devolve true);
   - os `set*Provider` descartam a função recebida, então o código de `app_core.cpp` ~376–430 nunca roda fora dos consoles;
   - `runPsboxNativeSelfTest` não faz nada;
   - `normalizeTrophyName` devolve o texto sem mudança e `findKnownTitle` devolve `nullptr`. São chamadas só dentro do provedor descartado, mas precisam existir para o link.

   Confira as assinaturas exatas em `src/trophy/*.h` (`grep -rn "dispatchStreamPoll\|runPsboxNativeSelfTest\|normalizeTrophyName\|findKnownTitle" src/trophy/*.h`).
2. **CMakeLists:** tire os 5 arquivos (linhas 58–62) do `xcloud_core` comum e acrescente-os só quando `XC_PLATFORM` for `ps5` ou `ps4`. Nos outros, entra o `trophy_off.cpp`. Tire `trophy_backend_null.cpp` dos alvos de desktop (linhas 307 e 363): sem motor de sincronização, ninguém precisa de backend.
3. **Interface:** hoje a linha 25 de Configurações ("Remover troféus") é escondida por `caps.console` (`src/ui/app_ui.cpp`, `visibleSettingsRows`). **Mas a linha 24 ("Sincronizar troféus") não tem condição e provavelmente aparece no Linux hoje.** Crie `UiCaps::trophies` (padrão `true`, então no PS5 nada muda), preenchido pelo `app::uiCaps()` (`true` só nos consoles), e esconda as linhas 24 e 25 com ele. Procure também qualquer outro ponto da interface que fale de troféus: o modal de confirmação em `app_ui_settings.cpp:689`/`1431`, as ações `SyncTrophies`/`RemoveTrophies` em `app_ui_input.cpp` (662, 683, 1031, 1052, 1185), os toasts e a WebUI. Nenhum deles pode aparecer nem ser acionável fora dos consoles.
4. **Configuração salva:** `syncTrophies` (`settings_manager`/`settings_choice.cpp`:39, 84) continua sendo lida e gravada, para não perder o valor de quem troca de plataforma com o mesmo arquivo, mas não tem efeito fora dos consoles.
5. **Testes:** o `tests/test_main.cpp` usa o motor real (Apêndice G.2): `testTrophySystem`, `testTrophyNetworkAndLifecycleResilienceStress`, `testTrophyStreamingPauseAndPriority`, `testTrophyModalsAndDesktopInput`, com `syncEngine()`, um `ITrophyBackend` falso e os headers do builder e do banco (linhas 63–69). **Decisão do usuário (2026-11-14): apagar os testes de troféus do Linux.** Troféus não existem nessa plataforma, então não há o que testar lá.
   - Apague as 4 funções do `test_main.cpp` e as chamadas delas no `main()`.
   - Apague também os `#include "trophy/..."` (linhas 63–69) e qualquer outro teste que use `trophy::` (confira com `grep -n "trophy::" tests/test_main.cpp` depois de apagar; o resultado tem que ser vazio).
   - **Não** crie alvo de teste separado para troféus.
   - O código de troféus passa a ser verificado só no console, pelo autotest `trophysynctest` (AGENTS.md §2.3 e §8), quando alguém mexer nele.
6. **PS5 e PS4:** compilam exatamente os mesmos arquivos de troféus de antes. Compile os dois para provar.
7. **Linux:** depois da mudança, o AppImage não tem mais código de troféus. Confira com `nm -C build-desktop/psbox | grep -c "trophy::"`: só podem aparecer os símbolos do `trophy_off.cpp`.

Isso também resolve o problema do Windows: os ~3.900 linhas POSIX dos troféus (12 `stat`, 10 `unlink`, 8 `opendir` só no `trophy_sync.cpp`) não precisam ser portadas, e nada do AGENTS.md §8 é tocado.

---

## Apêndice C: pontos exatos de inserção para o ciclo de vida (A1.6, A2)

### C.1 `src/app/desktop_main.cpp`

- **Linhas 34–52:** leitura dos argumentos. O padrão do arquivo para variáveis de ambiente é **`SDL_setenv_unsafe`** (linha 39: `--data-dir` vira `XCLOUD_DATA_DIR`, que `platform::dataDir()` lê). Use a mesma função no Android, não `setenv`.
- **Linha 55:** `int code = selftest.empty() ? xc::app::runApp() : xc::app::desktopSelftest(selftest);`. **O bloco do Android entra logo antes desta linha**, depois da leitura dos argumentos:

```cpp
#if defined(__ANDROID__)
    // Android: the app's private folder and the assets copied out of the APK (Appendix D).
    if (const char* p = SDL_GetAndroidInternalStoragePath()) {
        SDL_setenv_unsafe("XCLOUD_DATA_DIR", p, 0);            // 0: --data-dir still wins
        const std::string assets = std::string(p) + "/assets";
        if (!xc::app::androidExtractAssets(assets)) return 3;  // logged inside; nothing works without cacert.pem
        SDL_setenv_unsafe("XCLOUD_ASSETS", assets.c_str(), 1);
        SDL_setenv_unsafe("XCLOUD_CA_BUNDLE", (assets + "/cacert.pem").c_str(), 1);
    }
#endif
```

  - O `androidExtractAssets` fica num arquivo novo, `src/app/android_assets.cpp`, compilado só no Android (Apêndice D).
  - O comentário da linha 56 avisa que o fim do `main` não é um `return` comum, porque as threads do worker podem continuar rodando. No Android o processo pode ser reaproveitado pelo sistema: confira que um segundo `SDL_main` no mesmo processo (a activity recriada) não encontra estado estático sujo. O caminho seguro é encerrar o processo no fim (`_exit`), como o desktop já faz.

### C.2 `src/app/shell_desktop.cpp`, `pumpEvents()` (linha 67)

O laço `while (SDL_PollEvent(&ev)) switch (ev.type)` já trata:
- `QUIT` e `WINDOW_CLOSE_REQUESTED` (71–75);
- `FOCUS_LOST`, que solta a captura do mouse (76–79), e `FOCUS_GAINED` (80);
- `EXPOSED`, `RESTORED`, `SHOWN`, `PIXEL_SIZE_CHANGED` e `DISPLAY_SCALE_CHANGED`, que chamam `display::sdlRepaint()` (81–87);
- `KEY_DOWN` com F11/Alt+Enter (88–97) e as teclas do texto (98–100);
- `TEXT_INPUT` (101), `MOUSE_MOTION` (104), `MOUSE_BUTTON_DOWN`/`UP` (113, 122) e `MOUSE_WHEEL` (131).

**Casos a acrescentar** (os de ciclo de vida só existem no Android e no iOS, mas o `case` compila em todo lugar, então não precisa de `#ifdef`):

```cpp
case SDL_EVENT_WILL_ENTER_BACKGROUND:       // Android: the surface is about to go
    g_background = true;
    display::sdlSetSuspended(true);         // new: draw*/present return at once, nothing touches the surface
    break;
case SDL_EVENT_DID_ENTER_BACKGROUND:
    app::onEnteredBackground();             // new: ends a stream cleanly, saves settings (decision in A2)
    break;
case SDL_EVENT_WILL_ENTER_FOREGROUND: break;
case SDL_EVENT_DID_ENTER_FOREGROUND:
    g_background = false;
    display::sdlSetSuspended(false);        // rebuilds VkSurfaceKHR + swapchain (C.3) before the next frame
    display::sdlRepaint();
    break;
case SDL_EVENT_TERMINATING:                 // the system is ending the process: save now
case SDL_EVENT_LOW_MEMORY:                  // also: shrink the image cache
    app::onSystemPressure(ev.type == SDL_EVENT_TERMINATING);
    break;
case SDL_EVENT_RENDER_DEVICE_RESET:         // SDL_Renderer fallback (GLES): textures are gone
case SDL_EVENT_RENDER_TARGETS_RESET:
    display::sdlRendererReset();            // new: recreate the SDL_Texture(s) of the fallback path
    break;
```

- **Botão Voltar:** no `KEY_DOWN`, `ev.key.key == SDLK_AC_BACK` vira "voltar". Fora da transmissão, ele age como o B da interface; durante a transmissão, abre o menu in-game. Ligue a dica `SDL_HINT_ANDROID_TRAP_BACK_BUTTON` = "1" antes do `SDL_Init`, senão o Android fecha a activity.
- **Rolagem por toque (A3):** `SDL_EVENT_FINGER_DOWN/MOTION/UP` (`ev.tfinger.x/y`, normalizados 0..1, e `fingerID`). O toque já gera `MOUSE_*` sintéticos (com `ev.button.which == SDL_TOUCH_MOUSEID`), então o clique funciona sozinho. Para rolar, acumule o `dy` do dedo e converta em `MOUSE_WHEEL` sintético, ou chame direto a rolagem da grade.
- **Teclado virtual:** a linha 182 já faz `SDL_StartTextInput(w)` em `setTextInput(true)`. No Android isso abre o teclado do sistema. Confira o `SDL_StopTextInput` ao sair do campo, senão o teclado cobre a tela.

### C.3 `src/display/gpu.cpp`: a superfície Vulkan morre em segundo plano

Como está hoje:
- `createSwapchain()` (510) e `recreateSwapchain()` (812). O `recreateSwapchain()` **reconstrói só o swapchain** e **reaproveita o mesmo `VkSurfaceKHR`**. Ele sai cedo se a janela tiver 0×0 (817).
- `vkAcquireNextImageKHR` (~1004): em `VK_ERROR_OUT_OF_DATE_KHR` chama `recreateSwapchain()` e tenta de novo (1006–1009). `VK_SUBOPTIMAL_KHR` marca `needSwapchain` (1014).
- `vkQueuePresentKHR` (~1349): `OUT_OF_DATE`/`SUBOPTIMAL` marcam `needSwapchain`; qualquer outro erro chama `fail` (1351).
- **`VK_ERROR_SURFACE_LOST_KHR` não é tratado em lugar nenhum** (só aparece na linha 407, como código de falha sintético).
- A superfície é destruída só no `shutdown` (~1190).

No Android, quando o app vai para segundo plano, o `ANativeWindow` é destruído: o `VkSurfaceKHR` antigo fica inválido e o próximo acquire ou present devolve `VK_ERROR_SURFACE_LOST_KHR`, ou crasha em alguns drivers. Ao voltar, o SDL cria um `ANativeWindow` novo. **Reaproveitar o `VkSurfaceKHR` antigo, como o `recreateSwapchain` faz, é inválido.**

O que implementar:
1. **`recreateSurface()`**, ao lado do `recreateSwapchain()`:
   - `vkDeviceWaitIdle`;
   - destruir as image views e o swapchain;
   - `vkDestroySurfaceKHR(g.instance, g.surface)`;
   - `g.wsi.createSurface(g.wsi.user, g.instance, &g.surface)`, o mesmo callback da criação, em `display_sdl.cpp:275`, que chama `SDL_Vulkan_CreateSurface`;
   - `vkGetPhysicalDeviceSurfaceSupportKHR` na fila de apresentação, porque a nova superfície tem que ser suportada pela mesma fila: se não for, falhe para o caminho do `SDL_Renderer`;
   - por fim, `buildSwapchain`.
2. **Suspensão:** `display::sdlSetSuspended(true)` faz `present()`, `drawNv12()` e `drawYuv420()` voltarem na hora, com o quadro descartado. A thread de vídeo continua chamando essas funções enquanto a rede entrega quadros. `sdlSetSuspended(false)` chama `recreateSurface()` antes do próximo quadro.
3. **Erro no acquire e no present:** trate `VK_ERROR_SURFACE_LOST_KHR` chamando `recreateSurface()` uma vez. Se falhar de novo, marque a GPU como indisponível e caia no `SDL_Renderer` (o caminho de reserva que já existe), sem crash.
4. **No Linux e no Windows**, o `SURFACE_LOST` quase nunca acontece, mas o tratamento é o mesmo e não muda nada no caminho normal.
5. **Cuidado: o `gpu.cpp` também é usado pelo PS5.** Ele entra no `xcloud_app` junto com o `display_ps5.cpp` (CMakeLists.txt, linhas 222–223), e é o mesmo pipeline de FSR, Anime4K e deband. Todo código novo de superfície tem que ficar **atrás de `g.wsi.createSurface`**, o callback de WSI que só o desktop preenche. O `recreateSwapchain()` já segue esse padrão: sai na linha 813 quando o callback não existe. **Conferido:** só o `display_sdl.cpp:274` preenche `wsi.createSurface`; o `display_ps5.cpp` não. Então, atrás dessa condição, o PS5 não é afetado. Compile o PS5 e, se mexer em algo fora desse caminho, **teste no console** pelo fluxo do AGENTS.md §2.
5. **Teste:**
   - no emulador, botão Home → voltar ao app, 10 vezes seguidas, na biblioteca e durante uma transmissão;
   - girar a tela, se a orientação não estiver travada;
   - abrir a janela de apps recentes.

   O log precisa mostrar a superfície recriada a cada volta, sem `fail(...)`.

### C.4 O caminho de reserva (`SDL_Renderer`, GLES no Android)

- Quando o Vulkan falha, `display_sdl.cpp:308` cria um `SDL_Renderer`. No Android, ele usa GLES, e o contexto GL pode ser perdido em segundo plano. O SDL avisa com `SDL_EVENT_RENDER_DEVICE_RESET`: **todas as `SDL_Texture` precisam ser recriadas** (a textura do vídeo e a do overlay). Implemente o `display::sdlRendererReset()` citado em C.2.
- Sem isso, o sintoma é tela preta ao voltar para o app, com o som tocando.

---

## Apêndice D: revisão do `platform_android.cpp` e extração dos assets

### D.1 Função por função (linhas 60–158 do arquivo atual)

| Função | Hoje | Problema | O que fazer |
|---|---|---|---|
| `init()` | `XCLOUD_DATA_DIR` ou o caminho fixo `/data/data/com.rafaelngp.psbox/files`; `makeDirs` | caminho fixo errado em perfil secundário | com o C.1, a variável sempre vem do SDL. Mantenha o fixo só como último recurso e **registre no log** quando ele for usado |
| `dataDirWritable()` / `dataDirErrno()` | `true` / `0` fixos | a interface nunca avisa se a pasta não grava | teste de verdade, como o Linux faz: grave e apague um arquivo `.probe` e guarde o `errno` |
| `appDir()` | igual à `dataDir()` | — | ok; o updater está desligado |
| `assetDir()` | `dataDir() + "/assets"` | **ignora `XCLOUD_ASSETS`** | leia `XCLOUD_ASSETS` primeiro, como `platform_windows.cpp:104` faz. Sem isso, o C.1 não tem efeito |
| `caBundlePath()` | `assetDir() + "/cacert.pem"` | **ignora `XCLOUD_CA_BUNDLE`** | idem (`platform_windows.cpp:116`) |
| `randomBytes()` | lê `/dev/urandom` em laço, retorna false na falha | — | correto. Opcional: `getrandom()` (API 28+) |
| `resolveIPv4()` | `getaddrinfo` só IPv4, sem duplicatas | — | correto |
| `probeNetworking()` | vazio | — | ok |
| `notify(text, forLog)` | só `__android_log_print` | **a pessoa não vê nada**: no Linux, `notify` empurra um toast do próprio app (`toasts().push(text, nowMs())`, `platform_linux.cpp:184`) | faça igual ao Linux (toast do app + log). `src/platform/toasts.cpp` é comum; inclua-o no build do Android se ainda não estiver |
| `makePrivate()` | `chmod 0600` | — | ok |
| `systemKeyboardAvailable()` e afins | false / Closed | — | ok: o texto entra pelo `SDL_StartTextInput` do shell |
| `restartApp()` | false | — | ok (o Android não reinicia o próprio app; o updater está desligado) |
| `firmwareVersion()` / `diagnostics()` | vazios | **os logs de issue chegam sem modelo nem versão do Android**, e a triagem (`.github/triage`) precisa disso | `__system_property_get("ro.build.version.release")`, `("ro.build.version.sdk")`, `("ro.product.manufacturer")`, `("ro.product.model")`, `("ro.soc.model")` ou `("ro.board.platform")`. Coloque na `diagnostics()` no mesmo formato de linha que o Linux usa |
| `platformName()` | "Android" | — | ok |
| `imageCacheBudget()` | 96 MB fixo | arriscado em aparelho de 2–3 GB de RAM | pegue a memória total (`sysconf(_SC_PHYS_PAGES) * _SC_PAGESIZE`): 48 MB abaixo de 4 GB, 96 MB acima |
| `isHandheld()` / `forceHandheld()` | false / no-op | — | **mantenha `false`.** Um `true` aplicaria, na primeira abertura, o perfil do Steam Deck, que troca a resolução para 720p (`app_core.cpp:322–334`). Isso contraria a decisão do usuário (resolução é escolha dele; Apêndice I.1). O tipo de aparelho (TV ou não) vem por outra função (A2, item 8): a `MainActivity` passa `XCLOUD_FORM_FACTOR=tv\|mobile` e uma `platform::formFactor()` nova lê, só para toque e navegação |
| `setThreadAffinity()` | no-op | — | mantenha no-op (seção 8, item 16) |
| `setThreadRealtimePriority()` | no-op | as threads de vídeo e rede rodam com prioridade normal e disputam com o resto do sistema | `setpriority(PRIO_PROCESS, gettid(), nice)` com os valores do Android: vídeo -8 (`URGENT_DISPLAY`), áudio -16 (`AUDIO`). Confira o retorno e registre no log quando falhar. Meça com A/B antes de deixar ligado |
| `setNoThreadPinning()` | no-op | — | ok |

**Funções que vêm do `posix_common.cpp`** (não duplique): `readFile`, `writeFileAtomic`, `startThread`/`Thread::join`, `nowMs`/`nowUs`, `sleepMs`, `localIPv4` (`getifaddrs`, API 24+).

### D.2 Extração dos assets do APK

**Por que extrair:** o app lê assets por caminho (`assetDir() + "/fonts/..."`), e o mbedTLS recebe o caminho do `cacert.pem`. Dentro do APK, os assets só são acessíveis pelo `AAssetManager`, que o SDL embrulha no `SDL_IOFromFile`.

**1. Lista gerada no build.** No `android/app/build.gradle.kts`, uma task gera a lista e a pasta de assets gerados entra no APK junto com a pasta do repositório:

```kotlin
val assetList = tasks.register("psboxAssetList") {
    val src = rootProject.file("../assets")
    val out = layout.buildDirectory.file("generated/psbox-assets/asset-list.txt")
    inputs.dir(src); outputs.file(out)
    doLast {
        val lines = src.walkTopDown().filter { it.isFile }.sortedBy { it.path }.map {
            val rel = it.relativeTo(src).invariantSeparatorsPath
            val sha = java.security.MessageDigest.getInstance("SHA-256").digest(it.readBytes())
                .joinToString("") { b -> "%02x".format(b) }
            "$sha $rel"
        }
        out.get().asFile.apply { parentFile.mkdirs(); writeText(lines.joinToString("\n") + "\n") }
    }
}
android.sourceSets["main"].assets.srcDirs("../../assets", layout.buildDirectory.dir("generated/psbox-assets"))
tasks.named("preBuild") { dependsOn(assetList) }
```

Uma linha por arquivo: `<sha256> <caminho relativo>`. O hash faz uma atualização do app trocar só os arquivos que mudaram.

**2. `src/app/android_assets.cpp`** (só no Android), `bool androidExtractAssets(const std::string& dir)`:
1. Ler `asset-list.txt` do APK com `SDL_LoadFile_IO(SDL_IOFromFile("asset-list.txt", "rb"), &size, true)`.
2. Ler `<dir>/asset-list.txt`, se existir. **Se for igual, retornar true na hora:** é o caso de todas as aberturas depois da primeira, e precisa custar menos de 1 ms.
3. Para cada linha cujo hash mudou ou que é nova:
   - criar as pastas;
   - copiar do APK com `SDL_IOFromFile(rel, "rb")` para `<dir>/<rel>.tmp`;
   - renomear por cima.
4. Apagar os arquivos que estão em `<dir>` mas não na lista nova.
5. **Gravar `<dir>/asset-list.txt` por último.** Se o app morrer no meio, a próxima abertura refaz.
6. Registrar no log quantos arquivos foram copiados e quanto tempo levou (a primeira abertura copia ~9 MB).

**3. Testes:**
- primeira instalação;
- abrir de novo, sem cópia;
- atualização com um asset mudado: só ele é copiado;
- matar o app no meio da cópia e abrir de novo.

Faça o parser da lista uma função pura, com teste unitário no `xcloud-tests`.

---

## Apêndice E: o contrato do `HwDecoder` (A4)

Como `src/media/decoder.cpp` usa o decodificador da plataforma:

| Ponto | Linha | Comportamento | Consequência para o Android |
|---|---|---|---|
| Inicialização | 584–590 | se `preferHw_`, chama `hw_.init(1920, 1088)` (tamanho fixo). Se der true, `isHw_ = true` | o `init` do Android recebe sempre 1920×1088. Configure o `AMediaCodec` com isso; o tamanho real vem do fluxo (SPS) e do recorte da saída |
| Log | 587 | `"h264 decoder: hardware decoder (libSceVideodec2) active"` | o texto cita o decodificador do PS5. No Android, mude para algo neutro (`"...hardware decoder (%s) active"`, com o nome vindo do backend). **Só o texto do log muda**; nenhuma regra de triagem depende dele (conferido em `.github/triage/rules.json`) |
| Chamada | 694–695 | `HwDecoder::Picture hp; hw_.decode(data, len, tag, hp)` | `data` é **um access unit Annex B** (com start codes), exatamente como sai do `h264_receiver`. O `tag` vai como `pts` |
| Sucesso | 696–714 | se `ok && !hp.error`: `format = Nv12`, `width = hp.width`, `height = (hp.height == 1088) ? 1080 : hp.height`, `strideY = strideU = hp.pitchBytes`, `y = hp.data`, **`u = hp.data + hp.pitchBytes * hp.height`**, `tag = hp.pts` | **o contrato supõe um buffer NV12 contíguo**: o plano UV começa logo depois do Y, em `pitchBytes * height`. O `AImageReader` entrega planos **separados** (e às vezes em NV21). Veja abaixo |
| Pendente | 716–724 | `hp.pending` = "ainda não há quadro, não é erro" (o decodificador enchendo a fila). Até 10 seguidos são aceitos; o 11º conta como erro | o MediaCodec costuma entregar a saída 1–3 quadros depois da entrada. Devolva `pending = true` enquanto `dequeueOutputBuffer` não tiver nada. Se passar de 10, as chaves de baixa latência não estão funcionando: registre no log |
| Erro | 726–750 | erro → `needsKeyframe_ = true` (pede IDR), `++consecutiveHwErrors_`; com 120 seguidos, `isHw_ = false` e o FFmpeg por software assume | no Android há fallback para software, porque `XC_HWDEC_NO_SOFTWARE_FALLBACK` só existe nos consoles (`cmake/platform.cmake`). Erro do codec = `hp.error = true`, nunca exceção nem crash |
| Medição | `lastTiming()` | `decodeUs`, `flushUs`, `decodeCpuUs` aparecem no overlay e no log | preencha pelo menos `decodeUs` (o tempo da chamada `decode`), para o A/B do AGENTS.md §7 ter números |

### E.1 Mudança mínima no contrato, sem afetar o PS5

Acrescente campos **opcionais** ao `HwDecoder::Picture` (`src/media/hw_decoder.h`):

```cpp
struct Picture {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t width = 0, height = 0, pitch = 0, pitchBytes = 0, format = 0;
    uint64_t pts = 0;
    bool error = false;
    bool pending = false;
    // Optional (Android): a chroma plane that is not right after luma, and its own pitch.
    const uint8_t* uv = nullptr;   // null: data + pitchBytes * height (the PS5 / PS4 layout)
    uint32_t uvPitchBytes = 0;     // 0: pitchBytes
    bool nv21 = false;             // V before U (some Android decoders)
    uint32_t visibleHeight = 0;    // 0: the 1088 -> 1080 rule
};
```

E no `decoder.cpp` (linhas ~703–707):

```cpp
out.strideY = hp.pitchBytes;
out.strideU = hp.uvPitchBytes ? hp.uvPitchBytes : hp.pitchBytes;
out.u = hp.uv ? hp.uv : hp.data + hp.pitchBytes * hp.height;
out.height = hp.visibleHeight ? hp.visibleHeight : ((hp.height == 1088) ? 1080 : hp.height);
```

- **PS5 e PS4 não preenchem os campos novos**, então o resultado é idêntico ao de hoje. Compile os dois e confirme.
- **NV21:** o `display::drawNv12()` espera U antes de V. Para `nv21 = true`, faça a troca no shader (uma flag a mais no `drawNv12`) em vez de reordenar bytes na CPU.

### E.2 O tempo de vida do buffer

- O `Picture` aponta para memória do decodificador. O `decoder.cpp` entrega na hora ao display, que copia para a GPU (ou para os `videoSlots`, no PS5).
- No Android, com `AImageReader`, o `AImage` precisa ficar vivo **até o `drawNv12` voltar**. Libere o `AImage` anterior no começo da próxima chamada `decode`, e não no fim da atual.
- Use `maxImages = 3` no `AImageReader`: um quadro na tela, um sendo decodificado, um de folga. Com menos, o MediaCodec trava esperando buffer livre.

### E.3 O `tag` precisa atravessar o MediaCodec

- O `tag` (que entra como `pts`) identifica o quadro para as medições de latência (`onScreenMs`) e para o `rec.idx`.
- Passe-o como `presentationTimeUs` no `AMediaCodec_queueInputBuffer`. O MediaCodec devolve o mesmo valor no `AMediaCodecBufferInfo.presentationTimeUs` da saída, ou no `AImage_getTimestamp` (em nanossegundos, então multiplique e divida por 1000).
- **Não use o relógio** para isso: com quadros descartados pelo decodificador, a correspondência se perde.

---

## Apêndice F: licenças nos pacotes

O app é GPL-3.0. O que cada pacote precisa levar na pasta `licenses/`:

| Componente | Origem no repositório | Linux (hoje) | Windows | Android | Observação |
|---|---|---|---|---|---|
| PSBox (GPL-3.0) | `LICENSE` | sim | sim | sim | — |
| Avisos de terceiros | `THIRD_PARTY_NOTICES.md` | sim | sim | sim | **falta o nanosvg** (abaixo) |
| FFmpeg (LGPL-2.1) | `deps/ffmpeg/COPYING.LGPLv2.1` → `FFmpeg-LGPL-2.1.txt` + `linux/FFmpeg-NOTE.txt` | sim | sim | sim | o `FFmpeg-NOTE.txt` cita o Linux: faça uma versão neutra ou uma por plataforma |
| SDL3 (zlib) | a licença do pacote do SDL (`XC_SDL3_LICENSE`) → `SDL3-LICENSE.txt` | sim | sim (do pacote mingw) | sim (`deps/SDL/LICENSE.txt`) | — |
| mbedTLS | `deps/mbedtls/LICENSE` | sim | sim | sim | — |
| libdatachannel (MPL-2.0) e dependências: usrsctp, libsrtp, plog | `deps/libdatachannel/...` | sim | sim | sim | o libjuice aparece no `THIRD_PARTY_NOTICES.md`, mas não tem arquivo próprio na lista: confira se a licença dele precisa ir |
| Fonte Inter (OFL) | `assets/fonts/OFL.txt` | sim | sim | sim | — |
| stb, FSR1 (MIT), Anime4K (MIT) | `extern/...` | sim | sim | sim | — |
| qrcodegen | `extern/qrcodegen` | (no `THIRD_PARTY_NOTICES`) | idem | idem | — |
| **nanosvg (zlib)** | `extern/nanosvg/LICENSE.txt` | **não** | precisa | precisa | entrou na v1.7.2 (ícone da guitarra) e **não está no `THIRD_PARTY_NOTICES.md` nem na lista de `cmake/desktop_package.cmake` (linhas 75–84)**. Corrija num PR pequeno à parte, que vale para Linux e PS5 também |
| SQLite (domínio público) | `extern/sqlite3` | — | — | — | não exige aviso, mas não está citado no `THIRD_PARTY_NOTICES.md`. Cite por completude |
| Runtime do llvm-mingw: libc++, libunwind (Apache-2.0 com LLVM Exception) e winpthreads (licença estilo MIT, que pede o aviso) | o próprio toolchain (`<llvm-mingw>/share/...` ou a fonte do mingw-w64) | — | **sim**, o winpthreads, porque o exe é `-static` | — | — |
| libc++_shared do NDK (Apache-2.0 com LLVM Exception) | NDK | — | — | opcional | a LLVM Exception dispensa o aviso em binário |

**Como fazer sem duplicar listas:**
- **Linux:** o AppImage usa `cmake --install` com `cmake/desktop_package.cmake`, que já monta `PSBox/licenses/` (linhas 70–94). O `tools/desktop/package-appimage.sh` (linhas 64–67) confere a lista.
- **Windows:** faça o zip **também** pelo `cmake --install` com o mesmo `desktop_package.cmake` (com os ramos `if(WIN32)` para o README do Windows, o `SDL3.dll` e o aviso do winpthreads), em vez do `cp` manual do `package-windows.sh`. O script passa a só chamar o install, conferir a lista e compactar.
- **Android:** a pasta `licenses/` vai para os assets do APK (`assets/licenses/`, por uma task do Gradle que copia a saída do mesmo install, ou pelo próprio CMake). Se o app tiver uma tela "Sobre", mostre de lá.
- **Código-fonte:** a GPL-3.0 exige o código-fonte correspondente. O link para o repositório e para a tag exata na tela "Sobre" e nas notas da release cobre isso, como já é feito no PS5.

---

## Apêndice G: testes unitários

### G.1 Como os testes são escritos

- **Um arquivo só:** `tests/test_main.cpp` (~12.100 linhas, 118 funções `static void testXxx()`), sem framework externo.
- **Macro:** `CHECK(cond)` (linha 102): em caso de falha, imprime `arquivo:linha: CHECK failed: <expr>` e incrementa `g_failures`, sem abortar.
- **Registro:** cada função nova precisa ser **chamada no `main()`** (linha ~12005), que roda tudo em sequência. Uma função que não está no `main()` não roda e ninguém percebe.
- **Execução:** `./build-desktop/xcloud-tests` ou `ctest --test-dir build-desktop` (o CMake registra como `add_test(NAME unit ...)`, linha 333). Saída 0 = tudo passou.

### G.2 O que nos testes atuais é POSIX (e quebra no Windows)

| O quê | Onde | Tratamento no Windows |
|---|---|---|
| `#include <sys/wait.h> <unistd.h> <arpa/inet.h> <netinet/in.h> <sys/socket.h>` | linhas ~79–84 | atrás de `#if !defined(_WIN32)`, ou trocar por `net/socket.h` |
| `testCrashGuard` (linha 1753): usa `fork()` + `waitpid()` para provocar um crash num processo filho e conferir o log do crash guard | 1757–1784 | `fork` não existe. Escreva `testCrashGuardWindows` com `CreateProcess` do próprio exe com um argumento (`xcloud-tests.exe --crash-child`) que provoca a exceção, ou pule no Windows com uma linha no log ("skipped on Windows") |
| `testResolveIPv4AndConnectTcp` (linha 2158): abre um servidor TCP local com `::socket`/`::bind`/`::listen` para testar o connect | 2187–2194 | passe a usar o `net/socket.h`. É o teste natural do W1.3: tem que passar no Windows |
| **32 caminhos fixos em `"/tmp/..."`** (por exemplo, linhas 229, 392, 548, 653) | o arquivo todo | no wine, `"/tmp/x"` vira `Z:\tmp\x` se o diretório atual estiver no `Z:`, e funciona. No Windows de verdade vira `C:\tmp\x`, que não existe, e os testes falham. Troque todos por um helper `testPath("nome.json")` que usa `std::filesystem::temp_directory_path()` (ou `GetTempPathW`) |
| 4 testes de troféus | `testTrophySystem`, `testTrophyNetworkAndLifecycleResilienceStress`, `testTrophyStreamingPauseAndPriority`, `testTrophyModalsAndDesktopInput` | **apagados** (decisão do usuário: troféus não existem no Linux; Apêndice B.2, passo 5) |

### G.3 Testes novos a escrever

| Teste | Fase | O que cobre |
|---|---|---|
| `testSocketLayer` | W1 | `socketsInit`; `setNonBlocking`; `connect` não bloqueante a `127.0.0.1` numa porta aberta (servidor do próprio teste) e numa fechada (recusa detectada em < 1 s, inclusive no wine); `pollSockets` com timeout; `bytesAvailable` depois de um `send`; `setRecvTimeout` (o `recv` volta em ~X ms com `wouldBlock` **ou** timeout, sem fechar o socket) |
| `testFsLayer` | W1 | `makeDirs` aninhado; `listDir` sem `.`/`..`; `renameReplace` por cima de arquivo existente; `removeTree` com subpastas; `fileSize`/`modifiedTime`; **caminho com acento e espaço** (`"pasta com ç/ã.json"`); `removeFile` de inexistente = true |
| `testUtf8Paths` | W2 | `platform::writeFileAtomic` + `readFile` num caminho com acento, que depende do manifesto UTF-8 no Windows |
| `testRandomBytesNoFallback` | W1 | `randomBytes` de 64 bytes duas vezes: diferentes e não todos zero. Confira por inspeção que não há caminho com `rand()` |
| `testAssetListParse` | A1 | o parser do `asset-list.txt` (Apêndice D.2): linha válida, linha vazia, caminho com espaço, diferença entre duas listas (novos, mudados, removidos) |
| `testVersionCode` | A1 | `1.7.2 → 10702`, `1.10.0 → 11000`, `2.0.15 → 20015`, se a conta ficar em C++ (por exemplo, para o log). No Gradle, confira à mão no PR |
| `testHwPictureContract` | A4 | o mapeamento `HwDecoder::Picture → VideoDecoder::Picture` (Apêndice E.1): com os campos novos zerados, o resultado é idêntico ao de antes (layout do PS5); com `uv`/`uvPitchBytes`/`visibleHeight`, os ponteiros saem certos. Extraia o mapeamento para uma função pura para poder testar |
| `testVisibleSettingsRowsDesktopCaps` | W2/A3 | com as capacidades de Windows e Android (sem troféus, sem alto-falante do controle, com toque no Android), as linhas escondidas são as esperadas e **as do PS5 continuam todas** (o teste do PS5 já existe; não o altere) |

**Regra:** teste novo não pode depender de rede externa, de conta, de relógio de parede nem de GPU. Os testes rodam no CI do Linux, sob wine no CI do Windows (W2) e, se der, no emulador Android.

---

## Apêndice H: SDL3, `__linux__` e o crash guard em cada plataforma

### H.1 O que o app já configura no SDL

| Onde | O quê | Windows | Android |
|---|---|---|---|
| `src/app/shell_desktop.cpp:46` (`beforeDisplay`) | `SDL_Init(VIDEO \| GAMEPAD \| AUDIO \| EVENTS)` | igual | igual. As dicas do Android (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`, `SDL_HINT_ORIENTATIONS`) precisam ser definidas **antes** desta linha |
| `src/app/shell_desktop.cpp:38–40` (`beforeSettings`) | `platform::enableToasts(true)`: o `notify()` vira toast na janela | ok | o `notify()` do Android não usa os toasts (Apêndice D.1): corrija lá |
| `src/media/audio_out_sdl.cpp:57` | `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES = "256"`; stream `F32` estéreo a 48 kHz (`AudioQueue::kRate`) | WASAPI. 256 quadros = 5,3 ms; confira no log ("audio output open ... %d-frame buffer") se o driver aceitou | AAudio. Muitos aparelhos impõem o burst nativo (96, 192 ou 240 quadros) e outra taxa (44,1 kHz). O SDL converte, mas meça a latência pelo log |
| `src/input/controller_sdl.cpp` | `SDL_InitSubSystem(GAMEPAD)` (245, 405); vibração, gatilhos e giroscópio pelo HIDAPI do SDL | XInput e GameInput para Xbox, HIDAPI para DualSense e DS4 | Bluetooth/USB pelo `SDLControllerManager` Java; o DualSense completo exige o HIDAPI (permissão USB ou Bluetooth) |
| `src/display/display_sdl.cpp:254` | `SDL_InitSubSystem(VIDEO)`, Vulkan (275) ou `SDL_Renderer` (308) | Vulkan; reserva D3D11 pelo `SDL_Renderer` | Vulkan; reserva GLES. Ciclo de vida no Apêndice C |

### H.2 `__linux__` também é verdadeiro no Android

O NDK define `__linux__` (o Android é um kernel Linux) **e** `__ANDROID__`. Todo `#if defined(__linux__)` do código comum também vale no Android:

| Lugar | O que faz | No Android | O que fazer |
|---|---|---|---|
| `src/input/controller_sdl.cpp:49` e `:105` | contorno do Linux: quando o SDL pega um DualSense pelo evdev (`/dev/input/eventN`, sem light bar, gatilhos nem giroscópio), procura o nó `/dev/hidrawN` (`hidrawOf`, linha 96, que lê `/sys/.../hidraw`) e reinicia o subsistema de gamepad para o HIDAPI assumir | apps não leem `/sys/class/hidraw` nem `/dev/hidraw*` (SELinux): o contorno não faz nada útil, gera erros no log e pode **reiniciar o subsistema de gamepad à toa** | troque por `#if defined(__linux__) && !defined(__ANDROID__)` |
| `src/app/crash_guard.cpp:243` | lê `RIP`/`RBP`/`RSP` do `ucontext` em `x86_64 && __linux__` | no emulador x86_64 funciona; **no arm64 (os aparelhos reais), nenhum ramo vale** e o log do crash sai com PC = 0 | acrescente o ramo `#elif defined(__aarch64__) && defined(__linux__)`: `pc = uc->uc_mcontext.pc; rbp = uc->uc_mcontext.regs[29]; /* x29, frame pointer */ rsp = uc->uc_mcontext.sp;`. A caminhada pela pilha usando frame pointer funciona igual no arm64 (o par `[x29, x30]` em cada quadro) se o código for compilado com `-fno-omit-frame-pointer`: confira as flags do build do Android |

**Regra para o agente:** antes de escrever `#if defined(__linux__)`, pergunte se vale para o Android. Para "Linux desktop", use `XC_PLATFORM_LINUX` (definido pelo `cmake/platform.cmake`, que no Android passa a ser `XC_PLATFORM_ANDROID`).

### H.3 O crash guard em cada plataforma (resumo do W1.6)

| Plataforma | Mecanismo | Arquivo |
|---|---|---|
| PS5 | `sigaction`, registradores do FreeBSD (`mc_rip`) | `crash_guard.cpp` (hoje) → `crash_guard_posix.cpp` |
| PS4 | o mesmo arquivo, com o ramo `__ORBIS__` | idem |
| Linux x86_64 | `sigaction`, `gregs[REG_RIP]` | idem |
| Android arm64 | `sigaction`, `uc_mcontext.pc`/`regs[29]`/`sp` (ramo novo, H.2) | idem. Atenção: o `debuggerd` do Android também captura o sinal e grava um tombstone; o handler do app deve **encadear** para o handler anterior (guardado no `sigaction`) depois de gravar o log, para o tombstone e o "o app parou" continuarem existindo |
| Windows | `SetUnhandledExceptionFilter`, `EXCEPTION_POINTERS->ContextRecord->Rip/Rbp/Rsp` | `crash_guard_windows.cpp` (novo) |

O formato da linha do crash no log precisa ser **o mesmo** em todas as plataformas, porque `tools/symbolize.sh` e as regras de triagem leem esse formato. Copie o formato do POSIX.

---

## Apêndice I: o protocolo do xCloud não muda por plataforma

### I.1 Como o app se identifica (`src/xcloud/gssv.cpp`, ~25–110)

O cabeçalho `X-MS-Device-Info` é escolhido pela **resolução configurada no app** (`Resolution`), e **não pela plataforma onde o app roda**. O comentário do código (linhas ~25–26) diz: *"Windows" + tela 1080p é o que recebe 1080p; um Android com tela 720p recebe 720p.*

- **1080p:** finge ser o app Xbox do Windows (`make: Microsoft`, `model: Surface Pro`, `os: Windows 11`, `platform: desktop`, 1920×1080).
- **720p:** finge ser um Android (`Google`/`Pixel`, `Android 14`, `phone`, 1280×720).
- **1440p:** o cliente web do xbox.com no Edge (`makeQhd`, SDK 10.6.65, `sdkInstallId` aleatório por execução).

**Regra (decisão do usuário, 2026-11-14): a resolução é escolha do usuário, igual em todas as plataformas.**
- A opção de Configurações que já existe vale **igual** no Windows e no Android: `Settings::resolution`, em `src/app/settings.h:83` (`"720p"`, `"1080p"`, `"1440p"`), mais o modo 1080HQ (`Resolution::P1080HQ` em `src/xcloud/gssv.h:88`). O 1440p continua sendo oferecido só depois que o serviço entregou 1440p uma vez (`maxHeightCloud`/`maxHeightHome`, `settings.h:199`), como hoje.
- **A plataforma nunca escolhe nem limita a resolução.** Não use a tela do aparelho, o tipo de aparelho (celular, TV, portátil) nem o fato de ser Android para mudar o `X-MS-Device-Info`. A identificação continua saindo **só** da opção escolhida, pela mesma função `deviceInfo()`.
- **O padrão de quem instala do zero é o mesmo de hoje** (1080p). Não crie um padrão diferente por plataforma.
- **Teste no Android e no Windows** que cada uma das opções entrega a resolução pedida. A resolução medida aparece no overlay e no log da sessão. Registre a tabela opção × resolução recebida no PR.
- **Toque oficial do xCloud** (fora do escopo, A3 item 4): se um dia os layouts de toque oficiais exigirem outra identificação, isso **não pode** passar por cima da resolução escolhida. Investigue e leve as opções ao usuário antes de implementar. O controle de toque do próprio app (A3) não depende de nada disso e funciona em qualquer resolução.

### I.2 WebRTC (libdatachannel) por plataforma

- **ICE:** o libdatachannel usa o libjuice para ICE/STUN e o usrsctp para o canal de dados. Os dois rodam em Windows e Android sem mudança de protocolo.
- **No Windows:** o firewall não pergunta nada para UDP de saída (só o `web_server.cpp`, que escuta, dispara o aviso). Conexões ICE por IPv6 funcionam se a máquina tiver IPv6.
- **No Android:**
  - o app precisa da permissão `INTERNET`, e nada mais, para ICE;
  - o sistema pode suspender a rede do app em segundo plano (Doze), mais um motivo para encerrar a transmissão ao sair (A2);
  - celular em dados móveis costuma ter NAT de operadora (CGNAT): o xCloud resolve pelos próprios servidores TURN, sem mudança do lado do app.
- **`stream_session.cpp:86`** valida candidatos IPv6 com `inet_pton`, que existe nos dois.
- **O `wifi_filler.cpp`** descobre o gateway pelo IP local (`.1`/`.254`). Em rede de celular (dados móveis) não há gateway local que recuse TCP: o filler já se desliga sozinho quando menos de 80% das tentativas são recusadas (Apêndice I.4). Confira no log do Android em 4G/5G que ele **não arma**.

### I.3 O que não depende de plataforma (não mexa)

Login (`auth/msa.cpp`, `xbox_live.cpp`), catálogo (`xcloud/catalog.cpp`, `titlehub.cpp`), regiões, compra de jogos gratuitos, fila e sessão (`gssv.cpp`), entrada (`stream/input_packet.cpp`), áudio Opus, RTCP. Se algo disso falhar só no Windows ou no Android, o problema está na camada de plataforma (sockets, arquivos, relógio, aleatoriedade), não no protocolo. Comece a investigação por lá.

### I.4 Contexto: as travadas de Wi-Fi e o filler (v1.7.2)

Os itens de Wi-Fi do W3 e do A5 se apoiam nesta investigação, feita no PS5 em novembro de 2026.

- **Sintoma:** congelamentos de 150 a 600 ms na transmissão, sobretudo no lobby dos jogos, com picos no gráfico de rede, num PS5 em Wi-Fi de 5 GHz.
- **Diagnóstico (não era o app):**
  - a fila do socket da transmissão, lida a cada 2 ms com `FIONREAD` (`src/stream/net_diag.cpp`), estava vazia durante as pausas;
  - beacons UDP mandados do PC em quatro prioridades de Wi-Fi (`tools/netdiag/beacon.py`) congelavam **só na fila BE** (melhor esforço), enquanto BK, VI e VO continuavam chegando.

  Conclusão: a fila BE do roteador para o console segura ~64–70 pacotes atrás de um pacote atrasado (janela de block-ack do Wi-Fi). A pausa dura o tempo que esses pacotes levam para chegar: longa com poucos pacotes por segundo (lobby), curta durante o jogo (~60–90 ms).
- **O que o app faz:** `src/stream/wifi_filler.cpp`.
  - Depois de 2 travadas em 2 minutos (`src/app/worker_stream.cpp`; uma conexão com cabo nunca arma), ele completa o tráfego até ~600 pacotes por segundo, no máximo 500/s de enchimento.
  - O enchimento são tentativas de conexão TCP para uma porta fechada do gateway (`.1` ou `.254`; portas 1, 9 e 8080). O roteador recusa na hora com um RST, e cada recusa é um pacote pequeno chegando ao console.
  - Antes de ligar, testa se ≥ 8 de 10 tentativas são recusadas em ≤ 25 ms; senão, não arma.
- **Resultado (A/B no lobby, ~5 minutos de cada lado):**

  | Métrica | Sem filler | Com filler |
  |---|---|---|
  | Congelamentos ≥ 150 ms por minuto | 11,6 | 4,8 |
  | Mediana dos congelamentos | 334 ms | 200 ms |
  | Quadros pulados por minuto | 176 | 49 |
  | Pacotes perdidos | 69 | 25 |

- **Linhas de log:** "network: packets held" (detecção de retenção, `h264_receiver`) e "wifi filler: on, N a second" / "off".
- **Lição para Windows e Android:** os dois sistemas têm um mecanismo próprio que ataca a mesma família de problema (economia de energia e varreduras do Wi-Fi):
  - Windows: o modo de streaming do WLAN (W3);
  - Android: o `WifiLock` de baixa latência (A5).

  Use o mecanismo do sistema **e** o filler, e meça cada um separadamente com A/B.
- **Restrição do usuário:** a solução tem que funcionar para qualquer pessoa, sem mexer no roteador.

---

## Apêndice J: plano de PRs em sequência

Cada PR é pequeno o bastante para revisar e deixa **todos** os alvos verdes. A ordem importa: cada um prepara o seguinte.

| # | Branch | Escopo | Arquivos principais | Muda o PS5? | Pronto quando |
|---|---|---|---|---|---|
| 0 | `chore/notices-nanosvg` | aviso do nanosvg (e citação do SQLite) | `THIRD_PARTY_NOTICES.md`, `cmake/desktop_package.cmake` (lista de licenças), `tools/desktop/package-appimage.sh` (conferência) | só o texto de avisos | AppImage com `nanosvg-LICENSE.txt`; zip do PS5 com o aviso atualizado |
| 1 | `refactor/trophies-console-only` | troféus só no PS5/PS4 (Apêndice B.2) | `CMakeLists.txt` (linhas 58–62, 307, 363), `src/trophy/trophy_off.cpp` (novo), `src/ui/app_ui.h` (`UiCaps::trophies`), `src/ui/app_ui.cpp` (linhas 24/25), `app::uiCaps()`, `tests/test_main.cpp` (apagar os 4 testes de troféus e os includes, por decisão do usuário) | não: compila os mesmos arquivos de troféus de antes | `nm` do `psbox` sem troféus reais; linhas de troféus ausentes no selftest do Linux; testes passando |
| 2 | `refactor/net-socket-layer` | `src/net/socket.h/.cpp` e a migração dos 4 arquivos (Apêndice A) | `http.cpp`, `web_server.cpp`, `worker_diagnostics.cpp`, `stream_session.cpp` | o caminho POSIX tem que ser **idêntico** | testes do Linux, incluindo `testSocketLayer`; PS5 compilando; descrição do PR com um diff mostrando que só mudaram nomes de função no caminho POSIX |
| 3 | `refactor/fs-layer` | `src/util/fs.h/.cpp` e a migração (Apêndice B.1) | `updater.cpp`, `app_state.cpp`, `image_cache.cpp`, `worker.cpp` | idem | `testFsLayer`; teste manual da migração do `settings.json` antigo no Linux |
| 4 | `build/windows-toolchain` | W1: toolchain, `cmake/windows.toolchain.cmake`, FFmpeg `windows`, crash guard dividido, `randomBytes`, `platform_windows.cpp` revisado, regra nova do boundaries, testes com caminho temporário | `tools/windows/fetch-toolchain.sh`, `tools/build-ffmpeg.sh`, `cmake/deps/ffmpeg.cmake`, `src/app/crash_guard_*.cpp`, `tests/test_main.cpp` | não (o crash guard POSIX só é movido) | `wine xcloud-tests.exe` passando; job `windows-core` no CI |
| 5 | `feat/windows-app` | W2: `psbox.exe`, SDL3 mingw, manifesto UTF-8/DPI, ícone, `.rc`, empacotamento pelo `cmake --install`, `package-windows.sh` falhando sem binário, job `windows-app` | `windows/psbox.rc`, `windows/psbox.manifest`, `windows/psbox.ico`, `cmake/desktop_package.cmake`, `tools/desktop/package-windows.sh`, `.github/workflows/ci.yml` | não | selftest sob wine com as capturas conferidas; zip como artefato do CI |
| 6 | `feat/windows-tuning` | W3: `timeBeginPeriod`, MMCSS, modo de streaming do WLAN (`platform::setLowLatencyNetwork`), filler e net_diag portados | `platform_windows.cpp`, `platform.h`, `wifi_filler.cpp`, `net_diag.cpp`, `worker_stream.cpp` | `platform.h` ganha uma função, no-op no PS5 | **precisa de Windows real e do OK do usuário**, com A/B |
| 7 | `build/android-skeleton` | A1: submódulo SDL, plataforma `android`, `libmain.so`, FFmpeg Android, Gradle novo + wrapper, `MainActivity`, manifesto (`allowBackup=false`), ícones, logcat, pasta de dados, assets extraídos, correções do `platform_android.cpp` (Apêndice D.1), `__linux__` (Apêndice H.2) | `deps/SDL`, `cmake/platform.cmake`, `cmake/deps/sdl3.cmake`, `CMakeLists.txt`, `android/**`, `src/app/desktop_main.cpp`, `src/app/android_assets.cpp`, `src/util/log.cpp`, `src/platform/android/platform_android.cpp`, `src/input/controller_sdl.cpp`, `src/app/crash_guard*.cpp` | não | selftest no emulador com as capturas; job `android-app` no CI |
| 8 | `feat/android-lifecycle` | A2: eventos de ciclo de vida, `recreateSurface`, suspensão, reset do `SDL_Renderer`, botão Voltar, dicas do SDL, `platform::formFactor()` (TV ou não; nunca o perfil handheld) | `shell_desktop.cpp`, `display_sdl.cpp`, `gpu.cpp`, `keyboard_nav.cpp`/`kbm_sdl.cpp`, `app_core.cpp` (`onEnteredBackground`) | **cuidado:** o `gpu.cpp` é usado pelo PS5. O código novo fica atrás de `g.wsi.createSurface` (Apêndice C.3, item 5); compile o PS5 e confira no console se tocar no caminho comum | **precisa de aparelho**: transmissão de 5 minutos e a sequência Home → voltar 10 vezes |
| 9 | `feat/android-touch` | A3: rolagem por toque, teclado virtual, controle virtual na tela (`setVirtualGamepad`), capacidade `touchScreen` | `app_ui_input.cpp`, nova `ui/components/touch_pad_overlay.cpp`, `shell.h` (caps) | não (capacidade falsa no PS5) | capturas do controle virtual no emulador; teste em aparelho |
| 10 | `feat/android-mediacodec` | A4 etapa 1: `hw_decoder_android.cpp` reescrito (`AImageReader`, baixa latência, `tag`), campos opcionais do `Picture`, NV21 no `drawNv12` | `hw_decoder_android.cpp`, `hw_decoder.h`, `decoder.cpp`, `display_sdl.cpp`/`gpu.cpp` (flag NV21) | `hw_decoder.h`/`decoder.cpp` mudam, mas os campos novos zerados mantêm o PS5 **idêntico**: compile e rode o `testHwPictureContract` | **A/B no aparelho** (AGENTS.md §7) com a tabela no PR |
| 11 | `feat/android-system` | A5: `WifiLock`, permissão do microfone, `setFrameRate`, área segura, prioridade de threads | `MainActivity.java`, `platform_android.cpp`, `audio_in_sdl.cpp`, `display_sdl.cpp` | não | aparelho + A/B do `WifiLock` |
| 12 | `release/windows-android` | 7.1–7.3: workflows de release, assinatura do APK, `release-verify.sh`/`publish-release.sh`/AGENTS.md §6/`notes.md` (se o usuário escolher a opção B) | `.github/workflows/release-*.yml`, `tools/*.sh`, `AGENTS.md` | o processo de release muda | **decisão do usuário** e uma release de teste (pre-release) completa |
| 13 | depois | A4 etapa 2 (zero-copy AHardwareBuffer → Vulkan), A6 (Android TV), atualizador no Windows/Android, toque oficial do xCloud | — | — | cada um com pedido do usuário |

**Ordem mínima para ter algo nas mãos:**
- Windows: 0 → 1 → 2 → 3 → 4 → 5 (o zip roda sob wine e vira um build de teste).
- Android: 0 → 1 → 7 → 8 (APK de teste com decodificação por software).

Os PRs 2 e 3 servem aos dois, porque o Android também ganha com os embrulhos, embora não precise deles para compilar.

**Modelo de descrição de PR** (siga o estilo do repositório e termine com as linhas de atribuição que o seu ambiente exigir):

```markdown
## What
<uma frase>

## Why
<link para a fase deste handoff, por exemplo HANDOFF_WINDOWS_ANDROID.md §5 W1.3>

## How it was checked
- Linux: `ninja -C build-desktop && ./build-desktop/xcloud-tests` → <N> checks, 0 failures
- Linux selftest: <ok, capturas em ...>
- PS5: `ninja -C build-ps5 xcloud_app && tools/ps5/link.sh build-ps5` → eboot.bin <tamanho>
- PS4: `ninja -C build-ps4` → ok
- Boundaries: `bash tools/check-platform-boundaries.sh` → ok
- <Windows/Android: o que foi rodado e onde>

## Not checked / needs the user
- <o que precisa de hardware ou de decisão>
```

---

## Apêndice K: variáveis de ambiente e arquivos de controle (ferramentas de teste)

### K.1 Variáveis de ambiente que o app lê

| Variável | Efeito | Onde | Uso nos testes de Windows e Android |
|---|---|---|---|
| `XCLOUD_DATA_DIR` | pasta de dados (conta, configurações, caches, log) | `platform::init()`; `--data-dir` define essa variável | isolar testes; no Android, vem do SDL (C.1) |
| `XCLOUD_ASSETS` | pasta dos assets | `assetDir()` (Windows; corrigir o Android, D.1) | rodar o exe de dentro de `build-win` apontando para `assets/` do repositório |
| `XCLOUD_CA_BUNDLE` | `cacert.pem` | `caBundlePath()` | idem |
| `XC_SDL_RENDERER` | força o `SDL_Renderer` em vez do Vulkan | `display_sdl.cpp:262` | **testar o caminho de reserva**: D3D11 no Windows, GLES no Android (C.4) |
| `SDL_VIDEO_DRIVER=offscreen`, `SDL_AUDIO_DRIVER=dummy` | do próprio SDL: sem janela e sem som | CI do Linux | selftest sob wine e no CI |
| `PSBOX_HANDHELD` | força "handheld" no Linux (`0`/`1`) | `platform_linux.cpp:252` | só Linux (Steam Deck). **Não** use no Android: o perfil handheld rebaixa para 720p (D.1) |
| `XC_SIM_LOSS` | simula perda de pacotes de vídeo (`stream_player.cpp:1253`) | — | testar a recuperação do MediaCodec (A4) sem rede ruim de verdade |
| `XC_TOUCH_POINTS` | pontos de toque anunciados ao xCloud | `stream_session` | não mexa sem ler o Apêndice I.1 |
| `XC_ACCOUNT_PLAIN` | conta em texto (desenvolvimento) | `auth_manager.cpp:75` | **não use em build distribuído**; nunca commite um arquivo de conta |
| `XC_DUMP_LOGIN`, `XC_DUMP_TITLES`, `XC_DUMP_SUBSCRIPTION` | despejam respostas no log (depuração) | `xcloud-cli` e afins | o log fica com dados da conta: não anexe a issues nem PRs |

### K.2 Arquivos de controle na pasta de dados

Todos ficam em `platform::dataDir()`, portanto funcionam no Windows (`%LOCALAPPDATA%\psbox\`) e no Android (`/data/user/0/com.rafaelngp.psbox/files/`, acessível por `adb shell run-as com.rafaelngp.psbox`):

| Arquivo | Efeito | Fonte |
|---|---|---|
| `autoplay.txt` | testes autônomos de tela: `AUTOTEST <segundos> <alvo>[,opções]`, lido por `src/app/autoplay.cpp:127` | **funciona no desktop**. Use para capturar telas no Windows e no Android como o AGENTS.md §2 faz no PS5. Regras: prefixo `AUTOTEST` sempre; teste só a tela que você mudou; **nunca** `keystest`; apague o arquivo depois (o autoplay faz backup das configurações e restaura na abertura sem autoplay, AGENTS.md §2.4) |
| `snap.req` | captura a tela atual em `now.ppm` | `worker_stream.cpp:351` |
| `menu.req` | captura o menu in-game como desenhado (`menu_now.ppm`) | `stream_screen.cpp:1320` |
| `rec.req` | grava N segundos do vídeo do jogo em `rec.h264` + `rec.idx` (diagnóstico) | `worker_stream.cpp:356` |
| `netdiag.txt` | liga o diagnóstico de rede (`afk=N`, `abaudio=N`, `abfiller=N`) | `src/stream/net_diag.cpp` |
| `xcloud.log` | o log do app | todas as plataformas (no Android, também no logcat depois do A1) |

**No Android**, para colocar um arquivo de controle:

```bash
adb shell run-as com.rafaelngp.psbox sh -c 'echo "AUTOTEST 30 librarytest" > files/autoplay.txt'
adb shell am force-stop com.rafaelngp.psbox && adb shell am start -n com.rafaelngp.psbox/.MainActivity
# depois: buscar as capturas e APAGAR o autoplay.txt
adb exec-out run-as com.rafaelngp.psbox cat files/now.ppm > now.ppm
adb shell run-as com.rafaelngp.psbox rm files/autoplay.txt
```

Os `.ppm` são convertidos para PNG com Python (`PIL.Image`) para inspecionar, como no fluxo do PS5.

---

*Fim do handoff. Se terminar uma fase, atualize a seção 9 (marque os itens) e o `docs/multiplatform.md`.*
