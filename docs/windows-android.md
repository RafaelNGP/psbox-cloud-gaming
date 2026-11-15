# Windows e Android: mapeamento do que falta

Estudo de 2026-11-14 sobre a base da v1.7.2. Ele complementa o [multiplatform.md](multiplatform.md), que descreve o que já foi feito para o Linux (M0, M1 e M3).

## Resumo

- **Base reaproveitável:**
  - o núcleo do app já está separado do PS5 (M0);
  - o app de desktop em SDL3 (`psbox`) roda no Linux com display Vulkan, FSR/Anime4K, controles, áudio e microfone;
  - as funções exclusivas de uma plataforma são consultadas por capacidades (`display::caps()`, `input::padCaps()`, `shell::canSelfUpdate()`).

  O SDL3 também roda no Windows e no Android, então os dois devem usar o mesmo `psbox` e os mesmos backends, e não um app à parte.
- **Windows:** é o trabalho menor. Falta portar o código de rede e de arquivos que ainda é POSIX (14 arquivos compartilhados), criar um alvo de FFmpeg para Windows, montar o CI e o empacotamento. **Nunca foi compilado:** nenhum job do CI gera Windows.
- **Android:** é o trabalho maior, e não pelo C++, que compila quase como no Linux. Os pontos que pesam são:
  - decodificação por MediaCodec;
  - ciclo de vida do app (segundo plano, superfície destruída);
  - assets dentro do APK;
  - toque para quem não tem controle;
  - permissões;
  - assinatura e distribuição do APK.

  O esqueleto atual (`android/`) serve pouco: não tem `MainActivity`, compila só dois arquivos (a camada de plataforma e o decodificador) e usa GameActivity em vez do SDL3.

## O que já existe

| Item | Windows | Android |
|---|---|---|
| Camada de plataforma (`platform.h`) | `src/platform/windows/platform_windows.cpp` (402 linhas, todas as funções da API) | `src/platform/android/platform_android.cpp` (158 linhas) |
| Ligação no CMake | sim (`WIN32` → ws2_32, crypt32, iphlpapi, winmm, secur32) | não: `android/app/src/main/cpp/CMakeLists.txt` à parte, com 2 arquivos |
| App | `psbox WIN32` (SDL3) já declarado no CMakeLists | nenhum |
| Decodificador por hardware | FFmpeg com Vulkan (o mesmo do Linux, falta testar) | `src/media/hw_decoder_android.cpp` (125 linhas: modo ByteBuffer, cópia para a CPU, sem as chaves de baixa latência) |
| Empacotamento | `tools/desktop/package-windows.sh` (zip portátil) e `windows/README-windows.txt` | Gradle (AGP, compileSdk 34, minSdk 26, arm64-v8a + x86_64), manifesto com INTERNET, RECORD_AUDIO, VIBRATE e LEANBACK |
| CI | nenhum | nenhum |

## Windows

### Build e dependências

- **Toolchain:** usar llvm-mingw em compilação cruzada a partir do Linux, como o M2 do multiplatform.md previa. O mesmo toolchain serve na máquina local e no CI (job ubuntu).
  - Testes e `--selftest` rodam sob wine (já instalado aqui).
  - Um job `windows-latest` para rodar o selftest num Windows de verdade é opcional.
  - O CMake já tem guardas `if(NOT MSVC)`, então o MSVC continua possível, mas não é necessário.
- **FFmpeg:** `tools/build-ffmpeg.sh` só conhece os alvos `ps5`, `ps4` e `host`, e `cmake/deps/ffmpeg.cmake` escolhe entre eles. Falta:
  - um alvo `windows` (`--target-os=mingw32 --arch=x86_64 --cross-prefix=x86_64-w64-mingw32-`);
  - hwaccel Vulkan, como no Linux; o d3d11va é a alternativa se algum driver falhar;
  - não ligar `m` no Windows.
- **SDL3:** o pacote oficial `SDL3-devel-*-mingw` ou compilar da fonte, como o job do Linux faz.
- **mbedTLS e libdatachannel:** suportam Windows. Falta conferir as definições que o CMakeLists aplica ao `datachannel-static` (linha 84).
- **Vulkan:** o `vulkan-1.dll` vem com o driver da GPU e o SDL o carrega em tempo de execução. Basta ter os headers no build.

### Código compartilhado ainda POSIX

São os arquivos que entram no build de desktop e usam APIs POSIX fora de `src/platform/`. Os que têm `_PS5`/`_PS4` no nome ficam fora.

| Arquivo | O que usa | Tratamento |
|---|---|---|
| `src/net/http.cpp` | socket, poll, ioctl(FIONREAD), setsockopt, close | **crítico:** é todo o HTTP/TLS. Winsock |
| `src/app/web_server.cpp` | socket, bind, listen, accept, poll, signal | Winsock; o Windows pede liberação no firewall ao escutar |
| `src/app/worker_diagnostics.cpp` | connect, fcntl(O_NONBLOCK), poll | Winsock |
| `src/stream/stream_session.cpp` | close, send, inet_pton | Winsock |
| `src/app/updater.cpp`, `src/app/app_state.cpp`, `src/ui/image_cache.cpp`, `src/app/worker.cpp` | opendir, readdir, rename, unlink, stat, mkdir | `std::filesystem` |
| `src/trophy/trophy_builder.cpp`, `trophy_sync.cpp`, `trophy_db.cpp` | o mesmo, muito mais vezes (trophy_sync: 12 stat, 10 unlink, 8 opendir) | **decisão do usuário: troféus são exclusivos do PS5 e do PS4.** Saem de Linux, Windows e Android, com uma implementação vazia no lugar (detalhes no HANDOFF_WINDOWS_ANDROID.md, Apêndice B.2) |
| `src/app/crash_guard.cpp` | sigaction | SEH (`SetUnhandledExceptionFilter`, minidump opcional) |
| `src/app/shell_null.cpp` | signal | `SetConsoleCtrlHandler` ou nada |
| `src/stream/net_diag.cpp`, `src/stream/wifi_filler.cpp`, `src/app/stream_player.cpp` | já têm `#if defined(_WIN32)` | hoje são stubs no Windows: o filler de Wi-Fi não roda lá |

Proposta para não espalhar `#ifdef` pelo código:
- um `src/net/socket.h`, um embrulho pequeno do Winsock: `closeSocket`, `setNonBlocking`, `lastSocketError`, `pollSockets` (WSAPoll), `bytesAvailable` (ioctlsocket), com o `WSAStartup` dentro de `platform::init()`;
- um `src/util/fs.h` sobre `std::filesystem`, com caminhos UTF-8. No Windows isso importa: nomes de usuário com acento, como `C:\Users\João`, quebram APIs ANSI.
- uma regra nova em `tools/check-platform-boundaries.sh` proibindo headers POSIX fora de `src/platform/`, para não regredir.

### Comportamento específico do Windows

- **Pasta de dados:** `%LOCALAPPDATA%\psbox`, já implementada.
- **Selo da conta:** `account_seal.cpp` procura `/etc/machine-id`. No Windows cai no arquivo de chave, o que funciona, mas o DPAPI (`CryptProtectData`) é o jeito nativo de selar por usuário.
- **Gravação atômica:** `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`; falta conferir o `writeFileAtomic` atual.
- **Ritmo de quadros e prioridade:**
  - `timeBeginPeriod(1)`, cujo winmm já está ligado;
  - um waitable timer de alta resolução;
  - MMCSS (`AvSetMmThreadCharacteristics("Games")`) nas threads de áudio e decodificação.
- **Wi-Fi:** o Windows faz varreduras de fundo que geram travadas periódicas, o mesmo sintoma das travadas do lobby. O Moonlight e o Parsec ligam o *media streaming mode* com `WlanSetInterface(wlan_intf_opcode_media_streaming_mode)` durante a transmissão. O filler de Wi-Fi da v1.7.2 também pode ser portado, porque é só TCP.
- **Controles:** o SDL3 cobre XInput, GameInput e DualSense/DS4 por HIDAPI (vibração, gatilhos, giroscópio), como no Linux.
- **Distribuição:**
  - o zip portátil já está no script (`tools/desktop/package-windows.sh`), acompanhado de `.sha256`; a assinatura ECDSA (`.sig`) usada pelo pipeline oficial (`tools/desktop/sign-linux-release.sh` / `tools/release.sh`) permanece compatível e preservada na máquina de release;
  - um `.exe` sem assinatura Authenticode abre o aviso do SmartScreen ("O Windows protegeu o computador") e pode gerar falso positivo de antivírus;
  - plano de assinatura Authenticode sem bloquear o desenvolvimento: **SignPath.io** (plano gratuito para Open Source com revisão por mantenedor e integração direta no GitHub Actions), **Certum Open Source Code Signing** (certificado em nuvem/smartcard de baixo custo) ou **Azure Trusted Signing** (faturamento mensal com verificação de identidade); o `tools/release-verify.sh` segue travado nos **10 arquivos oficiais** até a homologação no hardware antes de entrar na matriz pública.
- **Atualizador:** hoje `canSelfUpdate()` só é verdadeiro no PS5. No Windows é viável: baixar o zip, verificar o `.sig`, renomear o exe em uso para `.old`, extrair e reiniciar. Fica para uma fase posterior.

## Android

### Arquitetura recomendada

- **SDL3 no Android (SDLActivity + `libmain.so`) no lugar da GameActivity.** Assim o Android usa os mesmos backends do desktop:
  - janela e superfície Vulkan;
  - áudio por AAudio;
  - controles: InputDevice do Android, mais HIDAPI para o DualSense;
  - teclado virtual para texto;
  - eventos de toque.

  Os arquivos Java do SDL (`android-project/`) entram em `app/src/main/java/org/libsdl/app`. A `MainActivity` estende `SDLActivity`.
- **Um só CMake:** o Gradle (`externalNativeBuild`) aponta para o `CMakeLists.txt` da raiz com um `XC_PLATFORM=android`, e não para o CMake separado de 2 arquivos.
- **ABIs:** arm64-v8a (celulares, TV boxes) e x86_64 (emulador, Chromebook). armeabi-v7a fica de fora.
- **minSdk:** sugiro 29 (Android 10): Vulkan 1.1 disponível na prática, AAudio estável e o lock de Wi-Fi de baixa latência. A chave `KEY_LOW_LATENCY` do MediaCodec exige o 30. Ficar no 26 obriga um caminho GLES via SDL_Renderer.
- **Pasta de dados:** `platform_android.cpp` usa o caminho fixo `/data/data/com.rafaelngp.psbox/files`, que falha em perfil secundário ou de trabalho (`/data/user/<n>/...`). O caminho deve vir de `SDL_GetAndroidInternalStoragePath()`.

### Decodificação de vídeo (o ponto mais crítico)

| Opção | Latência e consumo | Mantém FSR, Anime4K, deband e overlay | Complexidade |
|---|---|---|---|
| A. MediaCodec → Surface (SurfaceView), como o Moonlight faz | a melhor | não: o menu e as estatísticas precisam de outra camada por cima | média |
| B. MediaCodec → AImageReader (AHardwareBuffer) → importado no Vulkan (`VK_ANDROID_external_memory_android_hardware_buffer` + conversão YCbCr) → `gpu.cpp` | boa | sim: um só caminho de desenho, como no PS5 e no Linux | alta: varia por GPU (Adreno, Mali, PowerVR) |
| C. FFmpeg por software (NEON) | 1080p60 funciona em SoC recente, mas esquenta e gasta bateria | sim | baixa: já existe |

Recomendação:
- Começar por **C**, para o app rodar inteiro.
- Ir para **B** como caminho principal, medindo contra C com o A/B do AGENTS.md §7 num aparelho real.
- Deixar **A** como reserva para GPUs fracas.

Em qualquer opção:
- `KEY_LOW_LATENCY` (API 30);
- as chaves de fabricante: Qualcomm `vendor.qti-ext-dec-low-latency.enable`, Exynos `vendor.rtc-ext-dec-low-latency.enable`, e assim por diante;
- `KEY_PRIORITY=0`.

O `hw_decoder_android.cpp` atual (ByteBuffer com timeout de 2 ms) não serve como está: a interface `HwDecoder` precisa de uma saída por AHardwareBuffer ou por Surface.

### Áudio e microfone

- O SDL3 usa AAudio, e o Opus continua no FFmpeg.
- O chat de voz pede `RECORD_AUDIO` em tempo de execução (`SDL_RequestAndroidPermission`) antes de abrir o microfone. Se for negado, o app só esconde a opção.

### Entrada

- **Controle:** pelo SDL3 (Xbox, DualSense, 8BitDo por Bluetooth ou USB). O botão Voltar do Android vira B / sair do menu. Na Android TV, o D-pad do controle remoto navega a interface.
- **Toque**, para celular sem controle, que é o maior item novo, em três degraus:
  1. Navegar a interface por toque (tocar e rolar). A navegação por mouse do desktop (commit a349ac7) pode ser reaproveitada, com o toque fazendo o papel do mouse.
  2. Um controle virtual genérico na tela (botões e analógicos desenhados pelo app), que serve para qualquer jogo.
  3. Os layouts de toque oficiais do xCloud (Touch Adaptation Kit). O protocolo já está lá: `stream_session.cpp` envia `touchInputEnabled=false`. Falta baixar o layout de cada jogo e desenhá-lo. É grande e fica por último.
- **Teclado e mouse físicos:** pelo SDL3, com captura de ponteiro (API 26) nos jogos com teclado e mouse.

### Ciclo de vida e sistema

- **Segundo plano:** quando o app sai da tela, a superfície é destruída. É preciso recriar o swapchain Vulkan ao voltar, pausar ou encerrar a transmissão (a decidir) e gravar o estado, porque o Android pode matar o processo.
- **Tela:**
  - tela sempre acesa durante a transmissão (`FLAG_KEEP_SCREEN_ON`);
  - modo imersivo;
  - recorte da câmera (safe area);
  - `Surface.setFrameRate` (API 30) para 60 ou 120 Hz.
- **Wi-Fi:** um `WifiLock` em `WIFI_MODE_FULL_LOW_LATENCY` (API 29) desliga a economia de energia e as varreduras do Wi-Fi durante a transmissão. É o mesmo problema da investigação das travadas do lobby, e o filler da v1.7.2 também roda no Android.
- **Temperatura (opcional):** o status térmico do `PowerManager` pode reduzir o bitrate antes de o aparelho limitar o desempenho.

### Assets e arquivos

- `assets/` (9,2 MB: fontes, ícones, `cacert.pem`) vai dentro do APK, onde não existe como arquivo comum.
- Opções:
  - ler pelo `SDL_IOFromFile`, que abre assets do APK de forma transparente, através de um `platform::readAsset()`;
  - ou extrair na primeira execução para a pasta do app, com um carimbo de versão.
- O `cacert.pem` precisa ser um arquivo de verdade, porque o mbedTLS recebe um caminho. Ele é extraído de qualquer jeito.
- **Selo da conta:** sem machine-id, cai no arquivo de chave, o que funciona. O Android Keystore (JNI) seria o jeito nativo, mas é opcional.

### Build, CI e distribuição

- **Ferramentas:** NDK r27 (LTS), AGP 8 e targetSdk 35.
- **Dependências:** FFmpeg ganha um alvo `android-arm64` (`--target-os=android`, clang do NDK, NEON ativo). mbedTLS, libdatachannel e SDL3 compilam pelo CMake.
- **CI:** job ubuntu com Android SDK e NDK que roda `assembleRelease` e assina com a keystore de release guardada como segredo do GitHub. **A keystore não pode ser perdida:** sem ela, nenhuma atualização instala por cima e todo mundo precisa desinstalar.
- **Versão:** o `versionCode` precisa subir a cada release. Pode ser derivado do contentVersion (1.7.2 → 10702). O esqueleto ainda diz `1.4.0`.
- **Distribuição:**
  - APK na release do GitHub, com `.sha256`, instalável pelo Obtainium e por sideload na Android TV;
  - a Play Store não é recomendada (cliente não oficial de serviço da Microsoft, risco pelas políticas de representação e de termos);
  - atualização no app: o Android não troca o APK em silêncio, mas o app pode baixar e abrir o instalador (`REQUEST_INSTALL_PACKAGES`). Fica para depois.
- **Resolução:** é escolha do usuário, igual em todas as plataformas (720p, 1080p, 1440p quando disponível, como hoje). A identificação enviada ao xCloud (`src/xcloud/gssv.cpp`) segue só essa opção, nunca o tipo de aparelho.

## Impacto no processo de release

O AGENTS.md §6 exige **exatamente 10 arquivos** por release. Windows e Android pedem novos arquivos: `PSBoxCloudGaming[Windows]-vX.Y.Z.zip`, `PSBoxCloudGaming[Android]-vX.Y.Z.apk` e os `.sha256` de cada um. Isso muda:
- o §6;
- `tools/release-verify.sh` e `tools/publish-release.sh`;
- as tabelas bilíngues do `notes.md`.

Até ficarem estáveis, os dois podem sair como builds de teste fora da contagem obrigatória. A decisão é sua.

## Fases propostas

**Windows**
1. **W1:** CI com llvm-mingw; os embrulhos `socket.h` e `fs.h`; os troféus só nos consoles (também no Linux); a regra nova no check-platform-boundaries. Meta: `xcloud-tests` compilado e passando sob wine.
2. **W2:** alvo de FFmpeg para Windows, SDL3 mingw e `psbox.exe --selftest` sob wine.
3. **W3:** validação num Windows de verdade (Vulkan, controles, áudio, login, transmissão), crash guard por SEH, MMCSS e timer, modo de streaming do WLAN e o filler portado.
4. **W4:** zip no release e as mudanças do §6.

**Android**
1. **A1:** trocar o esqueleto pela SDLActivity; compilar o núcleo para arm64 pelo CMake da raiz; FFmpeg para Android; assets pelo `SDL_IOFromFile`; pasta de dados pelo SDL. Meta: o selftest no emulador.
2. **A2:** a primeira transmissão, com decodificação por software, controle e AAudio; o ciclo de vida (pausar, voltar, recriar a superfície).
3. **A3:** MediaCodec com as chaves de baixa latência (opção B), medido contra o software num aparelho real.
4. **A4:** navegação por toque, o controle virtual, o WifiLock e a permissão do microfone.
5. **A5:** APK assinado no CI, entrada no release, e checagens da Android TV (banner leanback, D-pad).
6. **Depois:** layouts de toque oficiais do xCloud e atualização pelo instalador.

## Riscos

- **Variação do MediaCodec entre fabricantes:** a latência e os bugs mudam por SoC. Um teste em aparelho só não prova o resto.
- **Perda da keystore do Android:** quebra a atualização de todos os usuários.
- **SmartScreen e antivírus no Windows**, sem assinatura de código.
- **Perfil do xCloud:** os layouts de toque oficiais podem exigir outra identificação. Se exigirem, isso não pode passar por cima da resolução escolhida pelo usuário.
- **Regra multiplataforma:** nada disso pode quebrar o build do PS5 ou do PS4. O CI precisa compilar todos os alvos (regra do docs/multiplatform.md: todo PR compila todos os alvos).

## O que ainda não foi verificado neste estudo

- A compilação real para Windows: o ambiente bloqueou baixar o llvm-mingw nesta sessão. O W1 começa por ela, e é ela que dá o número real de erros.
- Nada foi compilado para Android: não há NDK nesta máquina.
- O que a Microsoft entrega para o perfil Android/phone.
