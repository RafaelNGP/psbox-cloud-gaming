# Handoff — próxima sessão (após a v1.7.0, 2026-11-12)

Leia antes: `AGENTS.md` (fluxo de build, deploy e validação no console; releases).
Console de teste: PS5 em 192.168.15.17 (FTP 2121). Deploy: `ninja -C build-ps5 xcloud_app && bash tools/ps5/link.sh build-ps5 && bash tools/ps5/package.sh build-ps5 && PS5_HOST=192.168.15.17 ./tools/ps5/deploy.sh build-ps5`.
Testes de desktop: `ninja -C build-desktop && (cd build-desktop && ctest)`. O hook de commit roda `tools/check-platform-boundaries.sh`: código específico de PS5 só em arquivos `*_ps5.cpp`.

## Como o usuário quer que se trabalhe
- Validar tudo no console (prints dos autotests) antes de dizer que está pronto; o usuário homologa na TV.
- Economizar contexto: saídas de comando curtas, trabalho em lote.
- Quando o usuário afirmar um defeito, conferir com dados antes de discordar.
- Perfis de teste no PS5: devRafaelNGP (conta principal, Game Pass Ultimate) e User1 (conta gratuita WormyDisc36). Testes de conta sempre no User1.
- Nunca entrar numa partida do Fortnite por atalho cego: o usuário entra e avisa.
- Fechar o app não encerra a sessão na nuvem; para encerrar, deixar o stream terminar normalmente.

## Estado
- v1.7.0 publicada e verificada (`tools/release-verify.sh v1.7.0 --verify-sigs` passou, 10 arquivos, PS5 + PS4 + Linux). A tag aponta para `b8cc2fc`: o build do Linux no CI (Ubuntu 22.04, Clang 14) só passou depois de incluir `<cstddef>`/`<cstdint>` explicitamente em 122 arquivos. Para checar isso antes de uma release: compilar as fontes com Clang 14 num contêiner `ubuntu:22.04` (Docker está instalado).
- ProsperoStore: o download da v1.6.9 falhava porque o arquivo foi trocado depois que o catálogo gravou o hash. PR de correção aberto: darkxex/ps5-homebrew-catalog#1268. Com a v1.7.0 o catálogo deve indexar a versão nova sozinho (job diário); conferir em `https://homebrew.page/api/v1/apps/PPSA99810.json` que `version` é 1.7.0 e `sha256` bate com `PPSA99810.zip.sha256` da release. Se a v1.7.0 entrar, o PR #1268 pode ser fechado.
- Nova regra (AGENTS.md §9.2): arquivo publicado nunca é substituído na mesma tag; correção = versão nova.

## Validação pendente do usuário (no console)
1. User1: desligar "Sincronizar troféus", fechar e abrir o app; deve continuar desligado e sem jogos ocultos vindos do outro perfil (correção: o usuário do console agora é lido antes de carregar as configurações).
2. Painel do teste ao vivo do giroscópio com cantos retos (antes ficavam pontas pretas).
3. Relogar a conta gratuita no User1 (login pelo código na tela).
4. PS4: nada desta versão foi executado num PS4, só compilado (troféus, perfil do console, login salvo).

## A fazer — giroscópio
- WebUI: controles para os campos novos de cada jogo e do geral: `gyroCurveStart` (0–40), `gyroCurveExp` (10–40, décimos), `gyroInvertX`, `gyroInvertY`. A API de configurações já aceita esses campos (`settings_manager.cpp`); falta a página (`web_server_html.cpp`).
- Calibração para jogos de corrida (modo volante): hoje a calibração mede a câmera; o volante é o analógico esquerdo.
- Movimento vertical vazando para a horizontal com o controle inclinado de lado (`src/input/gyro.cpp`, projeção no vertical do mundo inclui o eixo de pitch).
- O giro perde força enquanto o polegar está no analógico direito (a postura é re-ancorada a cada leitura).
- A sensibilidade geral medida varia até ~20% entre calibrações (a velocidade máxima lida oscila); investigar segurar o nível máximo mais tempo ou usar a mediana de mais janelas.

## A fazer — contas, biblioteca e loja
- Ocultos, favoritos e tempo de jogo são do perfil do PS5, não da conta Xbox: trocar de conta dentro do mesmo perfil mantém essas listas. Decidir com o usuário se devem ser por conta.
- O User1 tem um ajuste por jogo do Fortnite que pode ter vindo do outro perfil; perguntar ao usuário antes de remover.
- Quando a lista de títulos responde HTTP 500, o app não tenta de novo sozinho (fica com o cache). Adicionar nova tentativa com espera.
- O texto "sem venda avulsa" (`NotSoldAloneBadge`) ficou sem uso: os jogos sem oferta agora somem. Remover a string e o ramo em `app_ui_render.cpp`.
- Código já preenchido no QR do login: não funciona com a identidade atual do app na Microsoft (erro de consentimento). Só com registro próprio do app, como o XCloud4. Não fazer sem decisão do usuário.

## A fazer — transmissão e imagem (de `QUALITY_CANDIDATES.md`)
- Croma guiado pelo brilho (bordas de cor mais nítidas): `nv122rgb.comp` / `yuv2rgb.comp`, medir com o `imgtest` (looks A–D já existem para comparar no mesmo quadro).
- Overlay de tela cheia reenviado à GPU a cada segundo (picos de desenho): parar de reenviar quando nada mudou (`gpu::setOverlay`, `stream_screen.cpp`).
- Aproveitar keyframe completo quando falta o pacote anterior (`h264_receiver.cpp`, `resync()`), com teste unitário.

## A fazer — fora do código
- Responder às issues no GitHub (análise já feita):
  - #420: relatório vazio enviado pela WebUI; o trecho de log mostra uma sessão saudável. Perguntar qual era o problema e o jogo.
  - #415: outro usuário (Breno568p2), mesma época da #416/#417 sobre jogos ocultos; provavelmente duplicata da #417.
  - #405 (PUBG): o jogo não está no Xbox Cloud Gaming (só console instalado); nada a fazer no app.
- `ROADMAP.md` item 6 (jogos locais de PC Game Pass): só pesquisa, não implementar.
