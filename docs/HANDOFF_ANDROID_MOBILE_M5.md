# Handoff: Android Mobile Touch, UI Adaptations & Localization (M5)

**Data**: 2026-11-15 19:38  
**Branch**: `android-port`  
**Último Commit**: `3f63340` (`feat(android): touch haptics gating, localization, menu scroll retention, mobile HUD and back button`)  
**Commit Anterior**: `0d4e06b`

---

## 1. Status Geral e Artefatos Gerados

- **APK Release Compilado**: `android/app/build/outputs/apk/release/app-release.apk`
- **Destino no NAS**: `/home/notebook-storm/wolfpad-NAS/Dev/PSBoxCloudGaming[Android]-v1.7.2-dev.apk` (atualizado às 19:37)
- **Instalação Manual no RG557 (se desejado)**:
  ```bash
  adb -s 192.168.15.38:45869 install -r "android/app/build/outputs/apk/release/app-release.apk"
  ```
- **Integridade Validada**:
  - `ninja -C build-desktop xcloud-tests`: 100% aprovado.
  - `tools/check-platform-boundaries.sh`: Limpo (zero vazamentos).
  - `ninja -C build-ps5 xcloud_app`: 100% compilado e íntegro (zero regressões).

---

## 2. O Que Foi Implementado

### R1. Desativação Estrita de Haptics / Vibração
- `src/input/controller.h` e `src/input/controller_common.cpp`: Adicionado check de `vibrationEnabled()` em `triggerHaptic()`.
- `src/input/controller_sdl.cpp`: `androidTriggerHaptic()` descarta qualquer pulso se `!vibrationEnabled()`.
- `src/ui/touch_pad_overlay.cpp`: Botões virtuais na tela e áreas de clique agora respeitam a configuração de vibração.

### R2. Localização Completa do Menu (6 Idiomas)
- `src/ui/strings.h` e arquivos `.inc` (`strings_pt_br.inc`, `strings_en.inc`, `strings_es.inc`, `strings_fr.inc`, `strings_de.inc`, `strings_it.inc`):
  - Criadas entradas oficiais para: `MenuTouchOverlay`, `MenuTouchOpacity`, `MenuTouchInvertSticks`, `MenuTouchLayoutStudio`, `MenuVibration`.
  - Descrições completas externalizadas via `tr(Str::...)` para todos os idiomas suportados.

### R3. Retenção de Scroll por Toque no Menu
- `src/ui/stream_menu.cpp`:
  - Removido o snap-back automático que forçava `scrollPixelOffset_ = startIdx * kRowH` para o índice de seleção quando o dedo era solto.
  - A rolagem por toque agora retém exatamente a posição onde o usuário parou.

### R4. Adaptações da Tela Inicial para Mobile Touch (5.5" 1080p)
- `src/ui/app_ui_render.cpp`:
  - **Ocultação de Legendas**: Ícones de botões e legendas de gamepad só aparecem na tela quando há um controle físico conectado e reconhecido.
  - **Tipografia Expandida**: Gamertag, tipo de assinatura, horas de jogo, gamerscore e logotipo ampliados em escala mobile.
  - **Botão Visual de Configurações**: Adicionado botão de engrenagem / configurações acessível diretamente por touch.
- `src/input/sdl_mapping.cpp` e `src/ui/app_ui_input.cpp`:
  - Mapeado o evento do botão físico/gesto **Back** do Android (`SDL_SCANCODE_AC_BACK`) para agir identicamente ao botão Círculo/(B) nas telas de navegação.
  - Menu de configurações agora suporta rolagem e seleção por touch.

### R5. Redesign da Tela de Login
- `src/ui/app_ui_render.cpp`:
  - Aumentado o espaçamento entre a mensagem de status e o código de pareamento.
  - Botão de abrir no navegador reposicionado e harmonizado com a identidade visual da aplicação.

---

## 3. Arquivos Modificados no Commit 3f63340

1. `src/app/app_core.cpp`
2. `src/input/controller.h`
3. `src/input/controller_common.cpp`
4. `src/input/controller_sdl.cpp`
5. `src/input/sdl_mapping.cpp`
6. `src/ui/app_ui.h`
7. `src/ui/app_ui_input.cpp`
8. `src/ui/app_ui_render.cpp`
9. `src/ui/app_ui_settings.cpp`
10. `src/ui/i18n/strings_de.inc`
11. `src/ui/i18n/strings_en.inc`
12. `src/ui/i18n/strings_es.inc`
13. `src/ui/i18n/strings_fr.inc`
14. `src/ui/i18n/strings_it.inc`
15. `src/ui/i18n/strings_pt_br.inc`
16. `src/ui/stream_menu.cpp`
17. `src/ui/strings.h`
18. `src/ui/touch_pad_overlay.cpp`
19. `tests/test_main.cpp`
