// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// app/shell.h for the desktop app (SDL3): SDL comes up before the display,
// the window (display_sdl.cpp) goes to the input backend, and each pass
// empties SDL's event queue: closing the window (or Ctrl+C, which SDL turns
// into a quit event) ends the app in order, F11 / Alt+Enter switch
// fullscreen (the setting follows), every event goes on to the input backend (pads, keyboard,
// mouse); while a field wants text (setTextInput()) what is typed is kept
// for it. Notices are toasts in the window; links open in the browser; no
// update is installed (Settings opens the release page).
#include "app/shell.h"

#include "app/app_core.h"
#include "display/display.h"
#include "display/display_sdl.h"
#include "input/input_ext.h"
#include "media/audio_out.h"
#include "platform/toasts.h"
#include "util/log.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace xc::app::shell {

namespace {
bool g_quit = false;              // main thread
int g_switchedTo = -1;            // F11 / Alt+Enter: the fullscreen state asked (main thread), -1 none
std::atomic<bool> g_focus{true};  // the window has the keyboard
bool g_textOn = false;            // SDL text input started (main thread)
std::string g_typed;              // typed since takeTextInput() (main thread)
std::function<void(const MouseEvent&)> g_mouseHandler;
std::function<void(const TouchEvent&)> g_touchHandler;
volatile std::sig_atomic_t g_signalled = 0;
void onSignal(int) { g_signalled = 1; }
}  // namespace

void beforeSettings() {
    platform::enableToasts(true);  // platform::notify() -> a toast in the window
}

void beforeDisplay(const Settings& settings) {
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "1");
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
    display::sdlStartFullscreenSetting(settings.desktopFullscreen);  // unless --windowed / --fullscreen
    // The display, the pads and the sound start their own parts; the event
    // loop is ours.
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        XC_LOGE("SDL: %s", SDL_GetError());
    // Ctrl+C and SIGTERM close the app in order. SDL turns them into a quit
    // event only when they weren't ignored (a shell's background job
    // ignores SIGINT): ours, as psbox-headless does.
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
}

bool afterDisplay(bool haveDisplay) {
    if (!haveDisplay) {
        XC_LOGE("no window could be opened: closing");
        return false;
    }
    input::sdlInputInit(display::sdlWindow());
    SDL_HideCursor();
    return true;
}

std::vector<std::string> iconFontPaths() { return {}; }  // the UI draws its own icons

bool pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (!g_quit) XC_LOGI("the window was closed");
            g_quit = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_focus = false;
            input::kbmSetCaptured(false);  // the mouse is the system's again at once
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: g_focus = true; break;
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            display::sdlRepaint();
            break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            display::sdlSetSuspended(true);
            media::audioPause();
            break;
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            app::onEnteredBackground();
            break;
        case SDL_EVENT_WILL_ENTER_FOREGROUND:
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            display::sdlSetSuspended(false);
            media::audioResume();
            app::onEnteredForeground();
            display::sdlRepaint();
            break;
        case SDL_EVENT_TERMINATING:
            app::onSystemPressure(true);
            break;
        case SDL_EVENT_LOW_MEMORY:
            app::onSystemPressure(false);
            break;
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_TARGETS_RESET:
            display::sdlRendererReset();
            break;
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
            display::sdlUpdateSafeArea();
            break;
        case SDL_EVENT_KEY_DOWN:
            // F11 / Alt+Enter: fullscreen. Not passed on (Enter would accept).
            if (ev.key.key == SDLK_F11 || (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT))) {
                if (!ev.key.repeat) {
                    bool on = !display::sdlIsFullscreen();
                    display::sdlSetFullscreen(on);
                    g_switchedTo = on;
                }
                continue;
            }
            if (g_textOn && (ev.key.key == SDLK_BACKSPACE || ev.key.key == SDLK_DELETE)) g_typed += '\b';  // repeats too
            if (g_textOn && !ev.key.repeat && (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER)) g_typed += '\n';
#if defined(__ANDROID__)
            if (g_textOn && !ev.key.repeat) {
                if (ev.key.key >= SDLK_A && ev.key.key <= SDLK_Z) {
                    char c = (ev.key.mod & SDL_KMOD_SHIFT) ? static_cast<char>('A' + (ev.key.key - SDLK_A))
                                                           : static_cast<char>('a' + (ev.key.key - SDLK_A));
                    g_typed += c;
                } else if (ev.key.key >= SDLK_0 && ev.key.key <= SDLK_9) {
                    g_typed += static_cast<char>('0' + (ev.key.key - SDLK_0));
                } else if (ev.key.key == SDLK_SPACE) {
                    g_typed += ' ';
                }
            }
#endif
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (g_textOn && ev.text.text) g_typed += ev.text.text;
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            if (ev.motion.which == SDL_TOUCH_MOUSEID) break;
            if (g_mouseHandler) {
                float lx = 0, ly = 0;
                if (display::sdlWindowToLogical(ev.motion.x, ev.motion.y, lx, ly)) {
                    g_mouseHandler({MouseEvent::Kind::Move, lx, ly, 0, 0, 0});
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (ev.button.which == SDL_TOUCH_MOUSEID) break;
            if (g_mouseHandler) {
                float lx = 0, ly = 0;
                if (display::sdlWindowToLogical(ev.button.x, ev.button.y, lx, ly)) {
                    g_mouseHandler({MouseEvent::Kind::ButtonDown, lx, ly, ev.button.button, 0, 0});
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (ev.button.which == SDL_TOUCH_MOUSEID) break;
            if (g_mouseHandler) {
                float lx = 0, ly = 0;
                if (display::sdlWindowToLogical(ev.button.x, ev.button.y, lx, ly)) {
                    g_mouseHandler({MouseEvent::Kind::ButtonUp, lx, ly, ev.button.button, 0, 0});
                }
            }
            break;
        }
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            if (g_touchHandler) {
                int winW = 0, winH = 0;
                SDL_Window* wnd = display::sdlWindow();
                if (wnd) SDL_GetWindowSize(wnd, &winW, &winH);
                if (winW > 0 && winH > 0) {
                    float winX = ev.tfinger.x * static_cast<float>(winW);
                    float winY = ev.tfinger.y * static_cast<float>(winH);
                    float lx = 0.0f, ly = 0.0f;
                    display::sdlWindowToLogical(winX, winY, lx, ly);

                    float ldx = ev.tfinger.dx * static_cast<float>(display::kWidth);
                    float ldy = ev.tfinger.dy * static_cast<float>(display::kHeight);

                    TouchEvent::Kind k = TouchEvent::Kind::Down;
                    if (ev.type == SDL_EVENT_FINGER_MOTION) k = TouchEvent::Kind::Motion;
                    else if (ev.type == SDL_EVENT_FINGER_UP) k = TouchEvent::Kind::Up;
                    else if (ev.type == SDL_EVENT_FINGER_CANCELED) k = TouchEvent::Kind::Cancel;

                    g_touchHandler({k, ev.tfinger.fingerID, lx, ly, ldx, ldy, ev.tfinger.pressure});
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            if (g_mouseHandler) {
                float mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                float lx = 0, ly = 0;
                display::sdlWindowToLogical(mx, my, lx, ly);
                g_mouseHandler({MouseEvent::Kind::Wheel, lx, ly, 0, ev.wheel.x, ev.wheel.y});
            }
            break;
        }
        default: break;
        }
        if ((ev.type == SDL_EVENT_MOUSE_MOTION && ev.motion.which == SDL_TOUCH_MOUSEID) ||
            ((ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
             ev.button.which == SDL_TOUCH_MOUSEID)) {
            continue;
        }
        input::sdlInputEvent(ev);
    }
    if (g_signalled && !g_quit) {
        XC_LOGI("closing on a signal");
        g_quit = true;
    }
    return !g_quit;
}

bool hasFocus() { return g_focus; }
bool canSelfUpdate() { return false; }
bool isInstalledCopy(const std::string&) { return false; }

void beforeShutdown() {
    input::kbmSetCaptured(false);
    input::sdlInputShutdown();
}

Caps caps() { return {false, true, false, true}; }

void setFullscreen(bool on) { display::sdlSetFullscreen(on); }

bool fullscreenSwitched(bool& on) {
    if (g_switchedTo < 0) return false;
    on = g_switchedTo == 1;
    g_switchedTo = -1;
    return true;
}

bool openUrl(const std::string& url) {
    if (SDL_OpenURL(url.c_str())) return true;
    XC_LOGW("browser: %s", SDL_GetError());
    return false;
}

bool setClipboardText(const std::string& text) {
    if (SDL_SetClipboardText(text.c_str())) return true;
    XC_LOGW("clipboard: %s", SDL_GetError());
    return false;
}

bool setTextInput(bool on) {
    if (on == g_textOn) return g_textOn;
#if defined(__ANDROID__)
    // On Android, Search has an in-app controller/touch keyboard. Calling SDL_StartTextInput
    // brings up the full-screen system IME covering the UI; suppress it while keeping physical
    // keyboard events working cleanly via SDL_EVENT_KEY_DOWN.
    g_textOn = on;
    if (!on) g_typed.clear();
    return g_textOn;
#else
    SDL_Window* w = display::sdlWindow();
    if (on) {
        g_textOn = w && SDL_StartTextInput(w);
        static bool told = false;
        if (!g_textOn && !told) told = true, XC_LOGW("text input: %s", SDL_GetError());
    } else {
        if (w) SDL_StopTextInput(w);
        g_textOn = false;
        g_typed.clear();
    }
    return g_textOn;
#endif
}

std::string takeTextInput() {
    std::string s;
    s.swap(g_typed);
    return s;
}

void setMouseHandler(std::function<void(const MouseEvent&)> handler) {
    g_mouseHandler = std::move(handler);
}

void setTouchHandler(std::function<void(const TouchEvent&)> handler) {
    g_touchHandler = std::move(handler);
}

void setTextInputArea(int x, int y, int w, int h) {
    SDL_Window* wnd = display::sdlWindow();
    if (!wnd) return;
    SDL_Rect r{x, y, w, h};
    SDL_SetTextInputArea(wnd, &r, 0);
}

void restartOrExit() {
    // Never asked (no update is installed here).
    std::fflush(nullptr);
    std::_Exit(0);
}

}  // namespace xc::app::shell
