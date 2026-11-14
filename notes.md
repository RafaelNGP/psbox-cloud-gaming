<!-- LANG:EN -->
# PSBox Cloud Gaming v1.7.2

Guitars and drums in Fortnite Festival, fewer and shorter freezes on Wi-Fi, a cloud queue that is never given up, and the PS4 catching up with the PS5's USB controllers.

## 🎸 Guitars and Drums
- **Xbox 360 wireless receiver**: each guitar, drum kit or controller linked to it is a player of its own (up to four), recognised with its own icon, even when it was linked before the app started.
- **The guitar's own menu tab**: optional strum, and **each lane's button chosen from a list** (the list is also what to bind in Fortnite). By default **Overdrive is A**, as in Fortnite Festival's own controls: nothing to change in the game.
- With strum on, the green fret no longer fires Overdrive in a song, and **holding yellow to start a song** works.
- **Star power** by the Back button, or by **raising the guitar** (with a sensitivity setting; still being calibrated: on some guitars it fires as the guitar is lowered).
- In the app's own menus, the whammy bar and the tilt sensor no longer move the selection.
- **PS3 instruments** (Guitar Hero and Rock Band guitars and drums, Guitar Hero Live, DJ Hero) through their USB dongles, on PS5 and PS4. *New and not yet tested on a console: please report with your log.*

## 🎮 USB Controllers
- **Rumble** on USB controllers and generic HID controllers read from their own descriptor (PS5).
- **The "X-ray" of controllers the app does not know**: plug it in with the app open, press each button once when asked, and attach the log to an issue; that is all we need to add it.
- **PS4**: the Xbox 360 receiver, instruments, PS3 instruments and the X-ray, as on the PS5. *Built, not yet tested on a PS4: testers welcome.*
- Turning USB controllers off with one plugged in no longer hangs the app.

## 📶 Wi-Fi
- **Freezes caused by the Wi-Fi are found and shown**: "Wi-Fi holding packets" on the screen when they repeat, and what helps in the game menu.
- **The app now cuts them short by itself** while the picture is light (lobbies, menus): in a test, lobby freezes went from 11.6 to 4.8 a minute and became 40% shorter. Nothing to set up, and nothing changes on a cable.
- The sound no longer drifts later after each freeze.

## ⏳ Queue
- The app **waits in the cloud's queue for as long as it takes** (it gave up after about 18 minutes and lost the place). Only Cancel ends it.

## 🎯 Gyro
- Moving the controller up and down no longer turns the aim sideways, the thumb on the right stick no longer weakens the gyro, and the calibration is steadier.

## 🖼️ Picture and Stream
- Sharper colour edges (the colour follows the brightness edges).
- The picture resumes at a key frame even when the packet before it never came.
- A title limited to some devices says where it can be played.

## 👤 Accounts
- Hidden games, favourites and play time follow the **Xbox account** signed in.

## 🧹 Other
- A start that cannot read the app's own files says so on the screen.

---

### 📦 Attached Files (English)

| File Name | Description |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.2.zip` | Official standalone ZIP package for PlayStation 5 (manual or homebrew manager installation). |
| `PSBoxCloudGaming[PS5]-v1.7.2.zip.sig` | ECDSA digital signature for verifying the PS5 standalone package. |
| `PSBoxCloudGaming[PS5]-v1.7.2.zip.sha256` | SHA256 checksum for the PS5 standalone package. |
| `PPSA99810.zip` | Canonical PS5 package required by the in-app auto-updater. |
| `PPSA99810.zip.sig` | Cryptographic signature for the canonical PS5 package (verified by in-app updater). |
| `PPSA99810.zip.sha256` | SHA256 checksum for the canonical PS5 package. |
| `PSBoxCloudGaming[PS4]-v1.7.2.pkg` | Installable FPKG package for PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.2.pkg.sha256` | SHA256 checksum for the PS4 package. |
| `PSBoxCloudGaming[Linux]-v1.7.2.AppImage` | Universal standalone executable for Linux distributions and Steam Deck (Desktop & Game Mode). |
| `PSBoxCloudGaming[Linux]-v1.7.2.AppImage.sha256` | SHA256 checksum for the Linux AppImage. |

### 🚀 How to Install

- **PlayStation 5**: Update in the app (Settings → Updates), through ProsperoStore, or download `PSBoxCloudGaming[PS5]-v1.7.2.zip` and copy the `PPSA99810` folder to `/data/homebrew/`.
- **PlayStation 4**: Install `PSBoxCloudGaming[PS4]-v1.7.2.pkg` via GoldHEN Package Installer.
- **Linux / Steam Deck**: Download `PSBoxCloudGaming[Linux]-v1.7.2.AppImage`, make it executable (`chmod +x`), and launch in Desktop Mode or add as a non-Steam game.

> Coming from v1.6.9 or older: each PS5 profile signs in once; the existing sign-in goes to the first profile that opens the app.

<!-- LANG:PT -->
# PSBox Cloud Gaming v1.7.2

Guitarras e baterias no Fortnite Festival, travadas menores e menos frequentes no Wi-Fi, uma fila da nuvem que nunca é abandonada, e o PS4 alcançando o PS5 nos controles USB.

## 🎸 Guitarras e Baterias
- **Receptor sem fio de Xbox 360**: cada guitarra, bateria ou controle ligado a ele é um jogador próprio (até quatro), reconhecido com seu próprio ícone, mesmo quando foi ligado antes de o app abrir.
- **Aba própria da guitarra no menu do jogo**: palhetada opcional e **o botão de cada pista escolhido numa lista** (a lista também mostra o que configurar no Fortnite). Por padrão o **Overdrive é o A**, como nos controles do próprio Fortnite Festival: nada para mudar no jogo.
- Com a palhetada ligada, o traste verde não dispara mais o Overdrive no meio da música, e **segurar o amarelo para começar a música** funciona.
- **Star Power** pelo botão Back, ou **levantando a guitarra** (com ajuste de sensibilidade; ainda em calibração: em algumas guitarras dispara quando ela abaixa).
- Nos menus do próprio app, a alavanca e o sensor de inclinação não movem mais a seleção.
- **Instrumentos de PS3** (guitarras e baterias de Guitar Hero e Rock Band, Guitar Hero Live, DJ Hero) pelos seus adaptadores USB, no PS5 e no PS4. *Novidade ainda não testada em console: mande seu log se usar.*

## 🎮 Controles USB
- **Vibração** nos controles USB e controles HID genéricos lidos pelo próprio descritor (PS5).
- **O "raio-X" dos controles que o app não conhece**: conecte com o app aberto, aperte cada botão uma vez quando pedir e anexe o log numa issue; é tudo o que precisamos para adicioná-lo.
- **PS4**: receptor de Xbox 360, instrumentos, instrumentos de PS3 e o raio-X, como no PS5. *Compilado, ainda não testado num PS4: testadores são bem-vindos.*
- Desligar os controles USB com um conectado não trava mais o app.

## 📶 Wi-Fi
- **As travadas causadas pelo Wi-Fi são detectadas e mostradas**: "Wi-Fi segurando pacotes" na tela quando se repetem, e o que ajuda no menu do jogo.
- **O app agora as encurta sozinho** enquanto a imagem está leve (lobby, menus): num teste, as travadas no lobby caíram de 11,6 para 4,8 por minuto e ficaram 40% mais curtas. Nada para configurar, e nada muda no cabo.
- O som não fica mais atrasado depois de cada travada.

## ⏳ Fila
- O app **espera na fila da nuvem o tempo que for preciso** (desistia depois de uns 18 minutos e perdia o lugar). Só o Cancelar encerra.

## 🎯 Giroscópio
- Mover o controle para cima e para baixo não gira mais a mira para os lados, o polegar no analógico direito não enfraquece mais o giroscópio, e a calibração ficou mais estável.

## 🖼️ Imagem e Transmissão
- Bordas de cor mais nítidas (a cor segue as bordas do brilho).
- A imagem volta num quadro-chave mesmo quando o pacote anterior nunca chegou.
- Um jogo limitado a alguns aparelhos diz onde pode ser jogado.

## 👤 Contas
- Jogos ocultos, favoritos e tempo de jogo seguem a **conta Xbox** conectada.

## 🧹 Outros
- Um início que não consegue ler os arquivos do próprio app avisa na tela.

---

### 📦 Arquivos Anexados (Português)

| Nome do Arquivo | Descrição |
|---|---|
| `PSBoxCloudGaming[PS5]-v1.7.2.zip` | Pacote ZIP oficial para PlayStation 5 (instalação manual ou por gerenciador homebrew). |
| `PSBoxCloudGaming[PS5]-v1.7.2.zip.sig` | Assinatura digital ECDSA para verificar o pacote de PS5. |
| `PSBoxCloudGaming[PS5]-v1.7.2.zip.sha256` | Checksum SHA256 do pacote de PS5. |
| `PPSA99810.zip` | Pacote canônico de PS5 usado pelo atualizador automático do app. |
| `PPSA99810.zip.sig` | Assinatura do pacote canônico de PS5 (verificada pelo atualizador do app). |
| `PPSA99810.zip.sha256` | Checksum SHA256 do pacote canônico de PS5. |
| `PSBoxCloudGaming[PS4]-v1.7.2.pkg` | Pacote FPKG instalável para PlayStation 4 (GoldHEN Package Installer). |
| `PSBoxCloudGaming[PS4]-v1.7.2.pkg.sha256` | Checksum SHA256 do pacote de PS4. |
| `PSBoxCloudGaming[Linux]-v1.7.2.AppImage` | Executável universal para distribuições Linux e Steam Deck (Modo Desktop e Modo Jogo). |
| `PSBoxCloudGaming[Linux]-v1.7.2.AppImage.sha256` | Checksum SHA256 do AppImage de Linux. |

### 🚀 Como Instalar

- **PlayStation 5**: atualize pelo app (Configurações → Atualizações), pelo ProsperoStore, ou baixe `PSBoxCloudGaming[PS5]-v1.7.2.zip` e copie a pasta `PPSA99810` para `/data/homebrew/`.
- **PlayStation 4**: instale `PSBoxCloudGaming[PS4]-v1.7.2.pkg` pelo GoldHEN Package Installer.
- **Linux / Steam Deck**: baixe `PSBoxCloudGaming[Linux]-v1.7.2.AppImage`, torne executável (`chmod +x`) e abra no Modo Desktop ou adicione como jogo não-Steam.

> Vindo da v1.6.9 ou anterior: cada perfil do PS5 faz login uma vez; o login que já existia fica com o primeiro perfil que abrir o app.
