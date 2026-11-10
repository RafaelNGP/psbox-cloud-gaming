# Documento de Handoff: Otimização de Performance da Decodificação HW no PS5 (VDEC2)

**Data:** 09 de Novembro de 2026  
**Repositório:** `xCloud-PS5`  
**Destinatário:** Claude Code / Desenvolvedor Subsequente  
**Status Atual da Branch:** `main` limpa e funcional (Commit recente: `9449ebb`)

---

## 1. Contexto e Objetivo Principal

Ao comparar a decodificação de vídeo do stream da nuvem (1080p60 H.264) no console PlayStation 5:
- **Software Decoder (FFmpeg multithread na CPU Zen 2):**
  - Métrica de decode no overlay (`decodeMs`): **2.6 ~ 2.8 ms** (com QP ~25).
- **Hardware Decoder (libSceVideodec2 / VDEC2 no chip Prospero):**
  - Métrica de decode no overlay (`decodeMs`): **3.3 ~ 3.8 ms** (com QP ~24).

Embora a latência fim-a-fim (*glass-to-glass*) do HW decoder seja menor em condições de alto movimento (7.9 ms no HW vs ~100 ms no SW quando há enfileiramento de frames), **o tempo de decodificação por frame do HW medido pelo loop de vídeo ainda está perdendo para a CPU**.

**Objetivo:** Investigar a fundo o pipeline de `libSceVideodec2` e o loop de renderização para reduzir o tempo de decodificação HW para abaixo de **2.5 ms**, superando a CPU sem introduzir instabilidades.

---

## 2. Arquitetura Atual do Pipeline de Vídeo

### 2.1 Arquivos Envolvidos
1. **[`src/media/hw_decoder_ps5.cpp`](file:///home/notebook-storm/Documentos/PS5%20Ports/xCloud-PS5/src/media/hw_decoder_ps5.cpp)**:
   - Backend nativo da Sony para decodificação H.264 via `libSceVideodec2`.
   - Inicializa memória direta (`directMemory`), fila de computação (`Vdec2ComputeConfig`) e decodificador (`Vdec2DecoderConfig`).
2. **[`src/media/decoder.cpp`](file:///home/notebook-storm/Documentos/PS5%20Ports/xCloud-PS5/src/media/decoder.cpp)**:
   - Camada de abstração `VideoDecoder`. Gerencia ativação do HW vs fallback para FFmpeg, parse do bitstream H.264 para extração de QP e empacotamento de `Picture` (NV12).
3. **[`src/app/stream_player.cpp`](file:///home/notebook-storm/Documentos/PS5%20Ports/xCloud-PS5/src/app/stream_player.cpp)**:
   - `videoLoop()`: Recebe access units RTP (`au`), mede tempos com `platform::nowUs()`, chama `decoder->decode(...)` e despacha para apresentação.
4. **[`src/display/gpu.cpp`](file:///home/notebook-storm/Documentos/PS5%20Ports/xCloud-PS5/src/display/gpu.cpp)**:
   - Renderização Vulkan: recebe `Picture` no formato NV12, atualiza texturas e apresenta via swapchain.

---

## 3. O Que Já Foi Investigado e Corrigido (Commit `9449ebb`)

1. **Prioridade de Thread do Kernel Prospero**:
   - O kernel do PS5 rejeita prioridades de thread fora do intervalo de usuário `[256, 767]`. Configurações anteriores com `cpuPriority = 250` causavam falha de criação com erro `0x811d0208`.
   - Fixado com segurança para `cpuPriority = 700` (`SCE_KERNEL_PRIO_FIFO_DEFAULT`) e `cpuAffinity = 0`.
2. **Contenção na Fila de Computação GPU (Pipe 0 vs Pipe 1)**:
   - `Vdec2ComputeConfig` tentava alocar a fila começando do Pipe 0. Porém, o Pipe 0 é compartilhado com a Vulkan Queue Family 0 (gráficos e swapchain).
   - O código foi ajustado para `pipeOrder = {1, 2, 3, 4, 0}`, garantindo que o decoder rode de forma assíncrona no **Pipe 1**.
3. **Ajuste de Preset do Decodificador**:
   - Perfil configurado para H.264 High Level 4.2 com DPB depth = 2 (`{100, 42, 2}`), com fallback para Level 5.1 DPB = 4 (`{100, 51, 4}`).
4. **Remoção de Prioridade Realtime Indevida**:
   - A thread `videoLoop()` chamava `platform::setThreadRealtimePriority()`, o que preempitava as threads de serviço do kernel da Sony. Essa chamada foi removida.

---

## 4. Hipóteses Críticas & Oportunidades de Otimização

### Hipótese 1: O Custo do `sceVideodec2Flush` Síncrono (Gargalo Principal Suspeito)
Veja as linhas 218 a 231 de [`src/media/hw_decoder_ps5.cpp`](file:///home/notebook-storm/Documentos/PS5%20Ports/xCloud-PS5/src/media/hw_decoder_ps5.cpp):
```cpp
uint64_t t0 = platform::nowUs();
int rc = sceVideodec2Decode(decoder_, &in, &frame, &info);
uint64_t tDecode = platform::nowUs() - t0;
bool flushed = false;
uint64_t tFlush = 0;
// Depth 1: a picture not out yet is flushed out.
if (rc == 0 && !info.valid) {
    flushed = true;
    uint64_t tf0 = platform::nowUs();
    info = Vdec2Output{};
    info.size = sizeof info;
    rc = sceVideodec2Flush(decoder_, &frame, &info);
    tFlush = platform::nowUs() - tf0;
}
```
- **Ponto de Investigação:**
  Com `pipelineDepth = 1`, o `sceVideodec2Decode` pode retornar `info.valid == 0` com frequência, exigindo uma segunda chamada síncrona imediata para `sceVideodec2Flush`.
  - Inspecione nos logs do console quanto tempo está sendo gasto em `tDecode` vs `tFlush`.
  - Se `tFlush` estiver somando 1.0 ~ 1.5 ms a cada frame, investigue se é possível operar com `pipelineDepth = 0` (se suportado pelo firmware) ou gerenciar o buffer de forma que `sceVideodec2Decode` retorne o frame diretamente sem necessidade de flush forçado por chamada.

### Hipótese 2: Atributos de Memória Direta e Barramento CPU/GPU
Em [`src/media/hw_decoder_ps5.cpp:203-204`](file:///home/notebook-storm/Documentos/PS5%20Ports/xCloud-PS5/src/media/hw_decoder_ps5.cpp#L203-L204):
```cpp
frameBufferSize_ = align16k(memory.maxFrameSize);
frameBuffer_ = static_cast<uint8_t*>(directMemory(frameBufferSize_, kCpuGpuRw));
```
- `kCpuGpuRw` utiliza proteção `0x33`.
- No PS5, o hardware decoder escreve na memória direta da GPU (`memoryType = 12`).
- Quando a Vulkan ou a thread de vídeo lê esse frame para atualizar o descritor de textura NV12, a coerência de cache de CPU pode estar gerando *pipeline stalls*.
- Verifique se a memória de saída pode ser mapeada com flags otimizadas para leitura de GPU (ex: `kGpuRwCpuWrite` ou coerência não-bloqueante).

### Hipótese 3: Ping-Pong / Pipelining do `frameBuffer_`
Atualmente, `frameBuffer_` é um buffer estático único. Enquanto a CPU/GPU está processando e desenhando o frame N, o decoder não pode iniciar o frame N+1 no mesmo buffer.
- Investigar a implementação de double-buffering (2 slots de `frameBuffer_`) para permitir que o decode do próximo frame se sobreponha à cópia/renderização do anterior.

### Hipótese 4: Sobrecarga do Parser de QP em Espaço de Usuário
Em [`src/media/decoder.cpp:681-686`](file:///home/notebook-storm/Documentos/PS5%20Ports/xCloud-PS5/src/media/decoder.cpp#L681-L686):
```cpp
if (lastQp_ < 0 || isKey || (++qpSampleCounter_ % 8 == 0)) {
    double parsedQp = h264Parser_.parseAccessUnitQp(data, len);
    if (parsedQp >= 0) {
        lastQp_ = parsedQp;
    }
}
```
- O parser de NAL units / Exp-Golomb (`h264Parser_`) percorre os bytes do pacote H.264 no caminho crítico da CPU logo antes de disparar o decode HW.
- Verifique se esse parse adiciona frações mensuráveis de milissegundos no frame em que é executado.

---

## 5. Como Testar e Validar no Console PS5

### Regras Operacionais Estritas (AGENTS.md):
- **Zero Blind Deploys**: Sempre valide o funcionamento antes de entregar ao usuário.
- **Zero Unrelated Autotests**: É expressamente proibido rodar testes de menu de teclado (`keystest`). Use apenas testes focados de stream (`AUTOTEST 30 FORTNITE`).
- **Higiene Mandatória**: Sempre remova `/data/homebrew/PPSA99810/autoplay.txt` ao final.

### Comandos de Compilação e Deploy:
```bash
# 1. Matar processo no PS5:
PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810

# 2. Compilar e empacotar:
cd xCloud-PS5
ninja -C build-ps5 xcloud_app
bash tools/ps5/link.sh build-ps5
XC_INCLUDE_ACCOUNT=1 bash tools/ps5/package.sh build-ps5

# 3. Enviar para o console:
PS5_HOST=192.168.15.17 ./tools/ps5/deploy.sh build-ps5

# 4. Executar autotest com live stream (30 segundos em Fortnite):
python3 -c '
import ftplib
ftp = ftplib.FTP()
ftp.connect("192.168.15.17", 2121)
ftp.login()
import io
ftp.storbinary("STOR /data/homebrew/PPSA99810/autoplay.txt", io.BytesIO(b"AUTOTEST 30 FORTNITE\n"))
ftp.quit()
'
PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810

# 5. Após o teste, limpar autoplay.txt e restaurar app para o usuário:
python3 -c '
import ftplib
ftp = ftplib.FTP()
ftp.connect("192.168.15.17", 2121)
ftp.login()
try: ftp.delete("/data/homebrew/PPSA99810/autoplay.txt")
except: pass
ftp.quit()
'
PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py kill PPSA99810
PS5_HOST=192.168.15.17 python3 ../PS5_Vulkan/tools/ps5_console.py launch PPSA99810
```

### Validação de Regressão Local:
```bash
ninja -C build-desktop xcloud-tests && ./build-desktop/xcloud-tests
```
*(Deve passar com 100% de sucesso).*

---

## 6. Resultados das Medições (09/11/2026)

> **Atenção:** o comando `AUTOTEST 30 FORTNITE` da seção 5 **não abre stream**: com o título `AUTOTEST` o app só roda testes de tela. Para stream, o título é o ID do jogo: `FORTNITE 1800 bench` (ou `FORTNITE 1800 swdecode,bench` para começar em software).

### 6.1 Método (validado pelo usuário)
- Medir **somente dentro de uma partida** ("Diamond Bank Heist" ou "Destroy The World", co-op), nunca no lobby (o FPS cai com o lobby parado). Prints a cada 30 s até a partida começar.
- Opção de autoplay `bench`: nada é pressionado e não há capturas automáticas (que custam tempo na thread de vídeo). Arquivos de controle em `/data/psbox`, sempre enviados com nome temporário + rename (um arquivo meio escrito foi lido vazio):
  - `mark.req` (texto = nome da janela): fecha a janela anterior e grava no log uma linha `BENCH window "<nome>"` com média, p50, p95, p99 e pico de cada frame (decode total; no hardware, o decode separado do flush; parser de QP; desenho; até a tela; do primeiro pacote até a tela) e os contadores (pulados, atrasados, falhas, perdas).
  - `move.req` (`1`/`0`): giro lento e igual da câmera (analógico direito), para a imagem ter movimento constante nas janelas.
  - `hwcfg.req` (`pipe=N queue=N prio=N aff=0xN opt=N`): recria o decodificador de hardware com esse ajuste (`HwDecoder::tuning()`).
- Troca SW→HW pelo menu in-game (OPTIONS + touchpad, 6× para baixo, direita, B). Com `swdecode`, o menu passa a mostrar "Software".
- Os logs do app **não** passam pelo klog: leia `/data/psbox/xcloud.log` via FTP (REST). O ftpsrv não tem NLST (use MLSD após `cwd`).

### 6.2 Resultados (Diamond Bank Heist, câmera girando, 1080p60, ~12 Mbps)

| Janela | Decode médio | p50 | p95 | p99 | Até a tela (média / p95) |
|---|---|---|---|---|---|
| Software (FFmpeg, 1 thread) | 6,10 ms | 6,25 | 7,08 | 7,50 | 10,4 / 16,0 ms |
| Hardware (configuração original) | 6,12 ms | 6,08 | 6,82 | 7,24 | 11,0 / 19,3 ms |
| HW + upscaler IA + limpeza | 6,14 ms | 6,12 | 6,95 | 7,34 | 9,9 / 15,1 ms |
| HW + upscaler FSR | 6,14 ms | 6,07 | 7,11 | 7,54 | 9,7 / 14,6 ms |
| HW pipe 1 / prioridade 700 (original, 2 medições) | 6,14 / 6,11 ms | 6,11 / 6,09 | 6,99 / 6,92 | 7,43 / 7,38 | 9,7 / 9,4 ms |
| HW prioridade de CPU 256 | 6,11 ms | 6,08 | 6,92 | 7,38 | 9,5 ms |
| HW pipe 2 | 6,11 ms | 6,08 | 6,99 | 7,46 | 9,7 ms |
| HW `optimizeProgressive = 0` | 6,13 ms | 6,13 | 6,92 | 7,38 | 9,5 ms |

Outros dados:
- Com imagem quase parada (menus do Fortnite, ~0,1 Mbps): HW 3,4 ms, SW 1,7 ms. O hardware tem um **custo fixo de ~3,3 ms por chamada**, mais ~2,7 ms que crescem com o conteúdo. Com a câmera parada na partida, o HW mediu 5,7 ms.
- O stream do xCloud (25 s gravados com a opção `dump`): H.264 **Main**, nível 4.2, 1080p, sem B-frames, **um slice por frame**, 2 IDR em 1413 frames, ~25 KB por frame.

### 6.3 Conclusão por hipótese
1. **Flush síncrono — descartada.** 0 flushes em 3691 frames: `sceVideodec2Decode` já devolve a imagem. Todo o tempo está dentro dessa chamada.
2. **Atributos de memória — descartada.** A memória tipo 12 / `0x33` é cacheada e coerente nos dois sentidos (medido no PS5_Vulkan, `docs/HARDWARE_FINDINGS.md`, 2026-09-27), e a cópia do frame pela CPU custa ~0,4 ms (dentro do "desenho").
3. **Ping-pong do buffer — sem ganho de tempo.** O decode é síncrono e acaba bem antes do próximo frame (16,7 ms). No PS5 não há corrida no buffer: a apresentação assíncrona só existe no PS4.
4. **Parser de QP — descartada.** 5 µs por parse, em 1 a cada 8 frames.
5. **Disputa da GPU (upscaler), pipe, fila, prioridade e `optimizeProgressive` — sem efeito** (todos entre 6,11 e 6,14 ms).

**Resumo:** o tempo de decode por hardware (~6,1 ms numa partida) é da própria biblioteca e do hardware da Sony e não muda com a configuração. A meta de < 2,5 ms não é alcançável por ajuste. O software, com 1 thread, empata. Mais threads de "slice" não ajudam (um slice por frame), e threads de "frame" acrescentam atraso.

### 6.4 Achados à parte
- **Vazamento de memória ao recriar o decodificador de hardware:** cada recriação (por exemplo, alternar HW/SW no menu in-game) deixa ~58 MB mapeados (16,7 MB de memória flexível, de 448 MB no total). O próprio código assume "um decodificador por vida do app". Não foi corrigido: liberar exige garantir que nenhum frame ainda aponte para o buffer antigo.
- **Configurações por usuário x proteção do autoplay:** `guardSettingsForAutoplay` roda no boot com `settingsPath()`, que depende do usuário do controle; o arquivo `settings_<uid>.json` pode ficar fora do backup. Nos testes, o backup/restauração foi feito pelo script.

### 6.5 Rodadas 6 e 7 (09/11/2026, noite)

**O que o `VideoDec2` faz com a CPU.** Durante os ~6 ms da chamada, a thread que chama `sceVideodec2Decode` usa só **0,16 ms de CPU** (`CLOCK_THREAD_CPUTIME_ID`): ela espera. Mesmo assim, o processo inteiro gasta tanta CPU quanto com o FFmpeg (59% contra 58% de um núcleo na rodada 6; 56-57% contra 52% na rodada 7). Ou seja, uma thread interna da biblioteca consome o equivalente a um decode em software. Isso pode ser decode em software ou espera ocupada pelo hardware. O ProsperoLight usa o `VideoDec2` para HEVC 4K120 a 80 Mbps, o que só o hardware alcança, e mede um custo fixo de ~3,7 ms por frame (o nosso é ~3,3 ms). A leitura mais provável é **hardware com espera ocupada**. Em 1080p H.264 a 12 Mbps ele não vence o FFmpeg em tempo nem em CPU: na rodada 7, o SW fez 5,15 ms contra 5,8 ms do HW.

**Sem efeito:** `resourceType` 0 e 2 (a criação falha e cai para software) e afinidade `0x1fff`.

**FFmpeg:** já usa assembly externo (AVX2/SSSE3), assembly inline (CABAC, com EBX/EBP livres), `-march=znver2` e threads. Com um slice por frame, ~5-6 ms é o limite numa thread.

**Corrigido: vazamento ao recriar o decodificador.** A memória do decodificador (compute, CPU, GPU, CPU+GPU e frame) fica guardada e é reaproveitada pelo próximo, um por vez. Nada é desmapeado, porque um frame a caminho da tela pode ainda apontar para ela. Um segundo decodificador vivo ao mesmo tempo (o probe `hwdecode`) mapeia a própria memória, como antes. Medido: **55 MB no total** depois de 5 recriações, incluindo HW→SW→HW (antes, +58 MB a cada uma). Trocar para "hardware" com o hardware já ativo agora não faz nada.

**Latência: frame do hardware sem cópia pela CPU.** O decodificador escreve direto em dois buffers do Vulkan, usados alternadamente (`display::videoSlots`). A memória do RADV no PS5 é direta, tipo 12, `0x33`, a mesma que o decodificador exige. O `drawNv12` copia desses buffers para as imagens na GPU, sem `memcpy`. Antes de escrever num buffer que o frame em voo ainda lê, o decodificador espera a GPU (`display::waitVideoSlot`, com limite de tempo). A/B no mesmo stream (`hwcfg.req` `zc=0`), intercalado com cópia, sem cópia, sem cópia e com cópia:

| | Com cópia (2 janelas) | Sem cópia (2 janelas) |
|---|---|---|
| Desenho na CPU, médio / p95 | 0,44 / 0,97 ms | 0,20 / 0,73 ms |
| Até a tela, médio | 8,96 / 9,18 ms | 8,64 / 8,85 ms |
| Do primeiro pacote até a tela, médio | 11,04 / 11,31 ms | 10,78 / 11,01 ms |

**Próximo passo para o decode:** baixar de ~5-6 ms exige um caminho de hardware sem o custo fixo do `VideoDec2`: módulos nativos (`libSceVdecCore`, `libSceVdecSavc2`, ...) ou outra API. Isso é engenharia reversa das bibliotecas da Sony.

**PS5_Vulkan / RADV:** o RADV linkado no xCloud é de 15/10 (fork PS5_Mesa `993732c`). Essa revisão tem 2 commits de 120 Hz (`wsi_videoout_set_high_refresh`) que **só existem localmente** e não estão no `origin/main` do fork. O remoto tem correções novas (`radv/ps5`, de 31/10 a 09/11), uma delas de memória liberada ainda em uso pela GPU. Atualizar exige aplicar os 2 commits locais por cima do remoto, recompilar o RADV e testar o app inteiro.
