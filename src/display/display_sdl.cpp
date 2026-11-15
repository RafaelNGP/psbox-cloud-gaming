// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// display.h on SDL3 (the desktop app): a resizable window showing the app's
// 1920x1080 picture letterboxed. When Vulkan 1.3 comes up on the window,
// display/gpu.cpp runs the same compute pipeline as the PS5 (BT.709 YUV +
// deband, Anime4K Restore, FSR 1 EASU or Anime4K x2 upscale, RCAS sharpening
// and overlay blend); otherwise an SDL_Renderer is used as a fallback.
//
// Either way, window and GPU/renderer calls stay on the main thread: drawing
// threads fill a back frame under frameMutex(), present() swaps it into a
// one-slot mailbox (the newest wins, nobody waits for the screen), and
// pump(), each pass of the main loop, presents it.
#include <cstdint>
#include "display/display.h"
#include "display/display_sdl.h"
#include "display/gpu.h"

#include "platform/platform.h"
#include "util/log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <utility>

namespace xc::display {

namespace {

enum class Kind { None, Menu, Video };

// A picture handed from a drawing thread to the main thread.
struct Frame {
    Kind kind = Kind::None;
    std::vector<uint32_t> rgba;    // Menu: kWidth x kHeight, 0xAABBGGRR
    std::vector<uint8_t> y, u, v;  // Video: the planes, rows packed
    bool nv12 = false;             // Video: u holds the interleaved UV plane (the Vulkan path)
    bool nv21 = false;             // Video: u holds the interleaved VU plane (the Vulkan path)
    int width = 0, height = 0;     // Video
    uint64_t id = 0;               // present()'s count when handed over
};

struct Overlay {
    std::vector<uint32_t> pixels;
    int x = 0, y = 0, w = 0, h = 0;
    uint8_t opacity = 0;
    bool on = false;
};

// Main thread only.
SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_uiTex = nullptr;
SDL_Texture* g_videoTex = nullptr;
int g_videoW = 0, g_videoH = 0;
SDL_Texture* g_overlayTex = nullptr;
int g_overlayW = 0, g_overlayH = 0;
SDL_Texture* g_readTex = nullptr;  // readBackRgb()'s target
SDL_ThreadID g_mainThread = 0;
bool g_startFullscreen = true;  // the setting's default
bool g_startForced = false;     // the command line chose
bool g_shown = false;  // the window shows from its first picture on
std::atomic<bool> g_lowLatencyPresent{true};  // setLowLatencyPresent(): MAILBOX when offered
std::atomic<bool> g_suspended{false};
std::atomic<bool> g_needSurfaceRecreate{false};
SafeAreaInsets g_safeArea{};

// Video frames handed to the GPU, by present id, until the screen shows them
// (VK_KHR_present_wait, polled without waiting in every pump()): each frame
// gets the time it reached the screen itself, not the one an earlier present
// got there at. Main thread only.
struct PendingShow {
    uint64_t presentId, frameId;
};
std::deque<PendingShow> g_pendingShows;
Frame g_front;         // the picture on screen
Overlay g_overlayShown;
bool g_frontPending = false, g_overlayPending = false;  // not uploaded yet

std::recursive_mutex g_frameMutex;  // display.h's: held around a draw and its present()
Frame g_back;             // being drawn (under g_frameMutex)

std::mutex g_mailboxMutex;
std::condition_variable g_mailboxCv;
Frame g_mailbox;  // the newest picture handed over
bool g_mailboxFull = false;
uint64_t g_handed = 0;
std::atomic<uint64_t> g_lastHanded{0};
// readBackRgb() from another thread (the video thread's screenshots): asked
// here, done by the next pump() on the main thread (under g_mailboxMutex).
struct ReadBack {
    bool asked = false, done = false, ok = false;
    std::vector<uint8_t> rgb;
    int w = 0, h = 0;
};
ReadBack g_readBack;
std::condition_variable g_readBackCv;

std::mutex g_overlayMutex;
Overlay g_overlay;
bool g_overlayChanged = false;

std::atomic<bool> g_repaint{false};
std::mutex g_shownMutex;
uint64_t g_shownId = 0, g_shownAtUs = 0;  // the last video frame presented

bool onMainThread() { return g_mainThread != 0 && SDL_GetCurrentThreadID() == g_mainThread; }

// The video texture at the stream's size: BT.709 limited range, what the
// decoder gives (the GPU converts it).
bool videoTexture(int w, int h) {
    if (g_videoTex && g_videoW == w && g_videoH == h) return true;
    if (g_videoTex) SDL_DestroyTexture(g_videoTex);
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_IYUV);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STREAMING);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_BT709_LIMITED);
    g_videoTex = SDL_CreateTextureWithProperties(g_renderer, p);
    SDL_DestroyProperties(p);
    if (!g_videoTex) {
        g_videoW = g_videoH = 0;
        XC_LOGE("display: video texture %dx%d: %s", w, h, SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(g_videoTex, SDL_SCALEMODE_LINEAR);
    g_videoW = w, g_videoH = h;
    XC_LOGI("display: video texture %dx%d (BT.709 limited)", w, h);
    return true;
}

// What pump() took from the mailbox and the overlay, into the textures.
void upload() {
    if (g_frontPending) {
        g_frontPending = false;
        if (g_front.kind == Kind::Menu) {
            SDL_UpdateTexture(g_uiTex, nullptr, g_front.rgba.data(), static_cast<int>(kWidth * 4));
        } else if (g_front.kind == Kind::Video && videoTexture(g_front.width, g_front.height)) {
            int cw = (g_front.width + 1) / 2;
            SDL_UpdateYUVTexture(g_videoTex, nullptr, g_front.y.data(), g_front.width, g_front.u.data(), cw,
                                 g_front.v.data(), cw);
        }
    }
    if (g_overlayPending) {
        g_overlayPending = false;
        const Overlay& o = g_overlayShown;
        if (o.on) {
            if (!g_overlayTex || g_overlayW != o.w || g_overlayH != o.h) {
                if (g_overlayTex) SDL_DestroyTexture(g_overlayTex);
                g_overlayTex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, o.w, o.h);
                g_overlayW = g_overlayTex ? o.w : 0, g_overlayH = g_overlayTex ? o.h : 0;
                if (g_overlayTex) SDL_SetTextureBlendMode(g_overlayTex, SDL_BLENDMODE_BLEND);
            }
            if (g_overlayTex) {
                SDL_UpdateTexture(g_overlayTex, nullptr, o.pixels.data(), o.w * 4);
                SDL_SetTextureAlphaMod(g_overlayTex, o.opacity);
            }
        }
    }
}

// The picture on the current target, in the 1920x1080 logical space.
void renderScene() {
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    const SDL_FRect full{0, 0, static_cast<float>(kWidth), static_cast<float>(kHeight)};
    if (g_front.kind == Kind::Menu) {
        SDL_RenderTexture(g_renderer, g_uiTex, nullptr, &full);
    } else if (g_front.kind == Kind::Video && g_videoTex) {
        SDL_RenderTexture(g_renderer, g_videoTex, nullptr, &full);
        const Overlay& o = g_overlayShown;
        if (o.on && g_overlayTex) {
            SDL_FRect d{static_cast<float>(o.x), static_cast<float>(o.y), static_cast<float>(o.w), static_cast<float>(o.h)};
            SDL_RenderTexture(g_renderer, g_overlayTex, nullptr, &d);
        }
    }
}

// The newest handed frame the GPU says is on the screen, with that time.
void pollShown() {
    if (g_pendingShows.empty()) return;
    uint64_t gpuId = 0, atUs = 0;
    if (!gpu::lastShown(gpuId, atUs)) return;
    uint64_t frame = 0;
    while (!g_pendingShows.empty() && g_pendingShows.front().presentId <= gpuId) {
        frame = g_pendingShows.front().frameId;
        g_pendingShows.pop_front();
    }
    if (!frame) return;
    std::lock_guard<std::mutex> lock(g_shownMutex);
    g_shownId = frame;
    g_shownAtUs = atUs;
}

bool drawGpuFront() {
    if (g_suspended.load() || g_needSurfaceRecreate.load()) return false;
    if (g_front.kind == Kind::Menu) {
        if (!gpu::drawRgba(g_front.rgba.data())) return false;
    } else if (g_front.kind == Kind::Video) {
        int cw = (g_front.width + 1) / 2;
        bool drawn = g_front.nv12 ? gpu::drawNv12(g_front.y.data(), g_front.u.data(), g_front.width, cw * 2,
                                                  g_front.width, g_front.height, true, g_front.nv21)
                                  : gpu::drawYuv420(g_front.y.data(), g_front.u.data(), g_front.v.data(), g_front.width,
                                                    cw, cw, g_front.width, g_front.height, true);
        if (!drawn) return false;
    } else {
        return false;
    }
    g_frontPending = false;
    g_overlayPending = false;
    if (!g_shown) {
        SDL_ShowWindow(g_window);
        g_shown = true;
    }
    gpu::present();
    if (g_front.kind == Kind::Video) {
        g_pendingShows.push_back({gpu::lastPresentId(), g_front.id});
        if (g_pendingShows.size() > 16) g_pendingShows.pop_front();  // never shown (hidden window)
    }
    pollShown();
    return true;
}

}  // namespace

void sdlStartFullscreen(bool on) { g_startFullscreen = on, g_startForced = true; }
void sdlStartFullscreenSetting(bool on) {
    if (!g_startForced) g_startFullscreen = on;
}
SDL_Window* sdlWindow() { return g_window; }
const char* sdlRendererName() {
    if (gpu::ready()) return "vulkan";
    return g_renderer ? SDL_GetRendererName(g_renderer) : "";
}
void sdlRepaint() { g_repaint = true; }

bool sdlIsFullscreen() { return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0; }

void sdlSetFullscreen(bool on) {
    if (!g_window || sdlIsFullscreen() == on) return;
    // No SDL_SetWindowFullscreenMode(): borderless at the desktop's own mode.
    if (!SDL_SetWindowFullscreen(g_window, on)) XC_LOGW("display: fullscreen: %s", SDL_GetError());
    XC_LOGI("display: %s", on ? "fullscreen" : "windowed");
    g_repaint = true;
}

void sdlToggleFullscreen() { sdlSetFullscreen(!sdlIsFullscreen()); }

std::recursive_mutex& frameMutex() { return g_frameMutex; }

bool init(bool preferGpu) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        XC_LOGE("display: SDL video: %s", SDL_GetError());
        return false;
    }
    g_mainThread = SDL_GetCurrentThreadID();
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (g_startFullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    const char* forceSdl = std::getenv("XC_SDL_RENDERER");
    if (preferGpu && !(forceSdl && forceSdl[0] && forceSdl[0] != '0')) {
        g_window = SDL_CreateWindow("PSBox Cloud Gaming", 1600, 900, flags | SDL_WINDOW_VULKAN);
        if (g_window) {
            SDL_SetWindowMinimumSize(g_window, 640, 360);
            Uint32 extCount = 0;
            const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&extCount);
            gpu::Wsi wsi;
            wsi.getInstanceProcAddr = reinterpret_cast<void*>(SDL_Vulkan_GetVkGetInstanceProcAddr());
            wsi.extensions = exts;
            wsi.extensionCount = extCount;
            wsi.user = g_window;
            wsi.createSurface = [](void* u, void* inst, void* surfOut) -> bool {
                return SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(u), static_cast<VkInstance>(inst), nullptr,
                                                static_cast<VkSurfaceKHR*>(surfOut));
            };
            wsi.getExtent = [](void* u, uint32_t& w, uint32_t& h) {
                int pw = 0, ph = 0;
                SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(u), &pw, &ph);
                w = pw > 0 ? static_cast<uint32_t>(pw) : 0u;
                h = ph > 0 ? static_cast<uint32_t>(ph) : 0u;
            };
            // Poll present completion without blocking the main thread's event loop.
            gpu::setPresentWait(false);
            gpu::setMailbox(g_lowLatencyPresent);
            if (wsi.getInstanceProcAddr && exts && gpu::init(wsi)) {
                XC_LOGI("display: SDL %d.%d.%d, video driver %s, renderer vulkan%s",
                        SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
                        SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetCurrentVideoDriver(),
                        g_startFullscreen ? ", fullscreen" : "");
                return true;
            }
            gpu::shutdown();
            SDL_DestroyWindow(g_window);
            g_window = nullptr;
        }
        XC_LOGW("display: Vulkan GPU path unavailable; falling back to SDL_Renderer");
    }

    g_window = SDL_CreateWindow("PSBox Cloud Gaming", 1600, 900, flags);
    if (!g_window) {
        XC_LOGE("display: window: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_SetWindowMinimumSize(g_window, 640, 360);
    g_renderer = SDL_CreateRenderer(g_window, nullptr);
    if (!g_renderer) {
        XC_LOGE("display: renderer: %s", SDL_GetError());
        shutdown();
        return false;
    }
    if (!SDL_SetRenderVSync(g_renderer, 1)) XC_LOGW("display: no vsync (%s)", SDL_GetError());
    SDL_SetRenderLogicalPresentation(g_renderer, kWidth, kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    g_uiTex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
    if (!g_uiTex) {
        XC_LOGE("display: UI texture: %s", SDL_GetError());
        shutdown();
        return false;
    }
    SDL_SetTextureScaleMode(g_uiTex, SDL_SCALEMODE_LINEAR);
    XC_LOGI("display: SDL %d.%d.%d, video driver %s, renderer %s%s", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
            SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetCurrentVideoDriver(),
            SDL_GetRendererName(g_renderer), g_startFullscreen ? ", fullscreen" : "");
#if defined(__ANDROID__)
    setHighRefresh(false);
#endif
    return true;
}

void shutdown() {
    if (!g_window) return;
    gpu::shutdown();
    for (SDL_Texture** t : {&g_uiTex, &g_videoTex, &g_overlayTex, &g_readTex})
        if (*t) SDL_DestroyTexture(*t), *t = nullptr;
    if (g_renderer) SDL_DestroyRenderer(g_renderer), g_renderer = nullptr;
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
    g_shown = false;
    g_front = Frame{};
    g_back = Frame{};
    g_mailbox = Frame{};
    g_mailboxFull = false;
    g_frontPending = g_overlayPending = false;
    g_videoW = g_videoH = g_overlayW = g_overlayH = 0;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void drawRgba(const uint32_t* pixels) {
    g_back.kind = Kind::Menu;
    g_back.rgba.assign(pixels, pixels + static_cast<size_t>(kWidth) * kHeight);
}

bool videoSlots(size_t, uint8_t* out[2]) {
    out[0] = out[1] = nullptr;
    return false;  // no hardware decoder writes into the display here
}
void waitVideoSlot(const uint8_t*) {}

bool drawNv12(const uint8_t* y, const uint8_t* uv, int strideY, int strideUV,
              int width, int height, bool, bool isNv21) {
    if (!y || !uv || width <= 0 || height <= 0 || strideY < width || strideUV < width) return false;
    const int cw = (width + 1) / 2, ch = (height + 1) / 2;
    g_back.kind = Kind::Video;
    g_back.width = width, g_back.height = height;
    g_back.nv21 = isNv21;
    g_back.y.resize(static_cast<size_t>(width) * height);
    for (int r = 0; r < height; ++r)
        std::memcpy(&g_back.y[static_cast<size_t>(r) * width], y + static_cast<size_t>(r) * strideY, width);
    // The Vulkan path takes the UV plane as it is (a hardware decoder's
    // NV12/NV21); SDL_Renderer's IYUV texture needs it split.
    g_back.nv12 = gpu::ready();
    if (g_back.nv12) {
        const size_t row = static_cast<size_t>(cw) * 2;
        g_back.u.resize(row * ch);
        for (int r = 0; r < ch; ++r) std::memcpy(&g_back.u[r * row], uv + static_cast<size_t>(r) * strideUV, row);
        return true;
    }
    g_back.u.resize(static_cast<size_t>(cw) * ch);
    g_back.v.resize(static_cast<size_t>(cw) * ch);
    for (int r = 0; r < ch; ++r) {
        const uint8_t* src = uv + static_cast<size_t>(r) * strideUV;
        uint8_t* du = &g_back.u[static_cast<size_t>(r) * cw];
        uint8_t* dv = &g_back.v[static_cast<size_t>(r) * cw];
        for (int c = 0; c < cw; ++c) {
            if (isNv21) {
                dv[c] = src[c * 2];
                du[c] = src[c * 2 + 1];
            } else {
                du[c] = src[c * 2];
                dv[c] = src[c * 2 + 1];
            }
        }
    }
    return true;
}

bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV,
                int width, int height, bool) {
    // Never waits for the screen: the mailbox keeps the newest frame.
    if (width <= 0 || height <= 0) return false;
    const int cw = (width + 1) / 2, ch = (height + 1) / 2;
    g_back.kind = Kind::Video;
    g_back.nv12 = false;
    g_back.width = width, g_back.height = height;
    g_back.y.resize(static_cast<size_t>(width) * height);
    g_back.u.resize(static_cast<size_t>(cw) * ch);
    g_back.v.resize(static_cast<size_t>(cw) * ch);
    for (int r = 0; r < height; ++r)
        std::memcpy(&g_back.y[static_cast<size_t>(r) * width], y + static_cast<size_t>(r) * strideY, width);
    for (int r = 0; r < ch; ++r) {
        std::memcpy(&g_back.u[static_cast<size_t>(r) * cw], u + static_cast<size_t>(r) * strideU, cw);
        std::memcpy(&g_back.v[static_cast<size_t>(r) * cw], v + static_cast<size_t>(r) * strideV, cw);
    }
    return true;
}

void present() {
    std::lock_guard<std::mutex> lock(g_mailboxMutex);
    if (g_back.kind == Kind::None) return;
    g_back.id = ++g_handed;
    g_lastHanded = g_handed;
    std::swap(g_back, g_mailbox);  // a frame pump() didn't take is dropped
    g_back.kind = Kind::None;      // its buffers kept for the next draw
    g_mailboxFull = true;
    g_mailboxCv.notify_all();
}

namespace {
bool readBackMain(std::vector<uint8_t>& rgb, int& width, int& height);

// Another thread's readBackRgb(), done here with the newest picture.
void serveReadBack() {
    {
        std::lock_guard<std::mutex> lock(g_mailboxMutex);
        if (!g_readBack.asked || g_readBack.done) return;
    }
    ReadBack r;
    r.ok = readBackMain(r.rgb, r.w, r.h);
    std::lock_guard<std::mutex> lock(g_mailboxMutex);
    if (!g_readBack.asked || g_readBack.done) return;  // given up meanwhile
    r.asked = r.done = true;
    g_readBack = std::move(r);
    g_readBackCv.notify_all();
}
}  // namespace

void pump() {
    if (g_suspended.load()) return;
    if ((!g_renderer && !gpu::ready()) || !onMainThread()) return;
    if (g_needSurfaceRecreate.load()) {
        if (gpu::ready()) {
            if (gpu::recreateSurface()) {
                g_needSurfaceRecreate.store(false);
            } else {
                return;
            }
        } else {
            g_needSurfaceRecreate.store(false);
        }
    }
    if (gpu::ready()) pollShown();
    {
        std::lock_guard<std::mutex> lock(g_mailboxMutex);
        if (g_mailboxFull) {
            std::swap(g_front, g_mailbox);
            g_mailboxFull = false;
            g_frontPending = true;
        }
    }
    serveReadBack();
    {
        std::lock_guard<std::mutex> lock(g_overlayMutex);
        if (g_overlayChanged) {
            std::swap(g_overlayShown, g_overlay);
            g_overlayChanged = false;
            g_overlayPending = true;
        }
    }
    // Minimized or covered: nothing drawn, the mailbox still emptied (the
    // newest frame goes up once the window shows again).
    if (g_shown && (SDL_GetWindowFlags(g_window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED))) return;
    bool repaint = g_repaint.exchange(false);
    if (!g_frontPending && !g_overlayPending && !repaint) return;
    if (g_front.kind == Kind::None) return;
    if (gpu::ready()) {
        drawGpuFront();
        return;
    }
    upload();
    renderScene();
    if (!g_shown) {
        SDL_ShowWindow(g_window);
        g_shown = true;
    }
    SDL_RenderPresent(g_renderer);
    if (g_front.kind == Kind::Video) {
        std::lock_guard<std::mutex> lock(g_shownMutex);
        g_shownId = g_front.id;
        g_shownAtUs = platform::nowUs();
    }
}

void idleWait(uint32_t ms) {
    std::unique_lock<std::mutex> lock(g_mailboxMutex);
    g_mailboxCv.wait_for(lock, std::chrono::milliseconds(ms), [] { return g_mailboxFull || (g_readBack.asked && !g_readBack.done); });
}

void setOverlay(const uint32_t* pixels, int x, int y, int w, int h, uint8_t opacity) {
    gpu::setOverlay(pixels, x, y, w, h, opacity);
    Overlay next;
    if (pixels && w > 0 && h > 0) {
        next.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h);
        next.x = x, next.y = y, next.w = w, next.h = h;
        next.opacity = opacity;
        next.on = true;
    }
    std::lock_guard<std::mutex> lock(g_overlayMutex);
    g_overlay = std::move(next);
    g_overlayChanged = true;
    g_mailboxCv.notify_all();
}

void setSharpness(int amount) {
    gpu::setSharpness(amount);
    g_repaint = true;
}
void setDeband(int level) {
    gpu::setDeband(level);
    g_repaint = true;
}
void setUpscaler(int mode) {
    gpu::setUpscaler(mode);
    g_repaint = true;
}
void setRestore(bool on) {
    gpu::setRestore(on);
    g_repaint = true;
}
void setLegacyPrecision(bool on) {
    gpu::setLegacyPrecision(on);
    g_repaint = true;
}
void setChromaGuide(bool on) {
    gpu::setChromaGuide(on);
    g_repaint = true;
}
void setOverlayRefreshAlways(bool on) { gpu::setOverlayRefreshAlways(on); }
DisplayCaps caps() {
    DisplayCaps c;
    c.upscalers = c.deband = c.sharpen = gpu::ready();
    c.presentTiming = true;  // the frame handed over, and when pump() presented it
    c.highRefreshSwitch = canSwitchRefresh();
    return c;
}
void setSwapImages(int count) { gpu::setSwapImages(count); }
void setLowLatencyPresent(bool on) {
    g_lowLatencyPresent = on;
    gpu::setMailbox(on);
}
void setPresentWait(bool on) { gpu::setPresentWait(on); }

#if defined(__ANDROID__)
#include <android/native_window.h>
#include <dlfcn.h>
#include <jni.h>

typedef int32_t (*PFN_setFrameRate)(ANativeWindow*, float, int8_t);

static void applyAndroidFrameRate(SDL_Window* window, float fps) {
    if (!window) return;
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    auto* nw = static_cast<ANativeWindow*>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
    if (!nw) return;

    static auto pfnSetFrameRate = reinterpret_cast<PFN_setFrameRate>(dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));
    if (pfnSetFrameRate) {
        int32_t rc = pfnSetFrameRate(nw, fps, 1);
        XC_LOGI("display: ANativeWindow_setFrameRate(%.1f Hz, FIXED_SOURCE) -> rc=%d", fps, rc);
    }
}

static void applyAndroidDisplayMode(float fps) {
    typedef void* (*PFN_GetJNIEnv)();
    static auto pfnGetJNIEnv = reinterpret_cast<PFN_GetJNIEnv>(dlsym(RTLD_DEFAULT, "SDL_GetAndroidJNIEnv"));
    if (!pfnGetJNIEnv) return;
    auto* env = static_cast<JNIEnv*>(pfnGetJNIEnv());
    if (!env) return;
    jclass cls = env->FindClass("com/rafaelngp/psbox/MainActivity");
    if (!cls) {
        env->ExceptionClear();
        return;
    }
    jmethodID mid = env->GetStaticMethodID(cls, "setDisplayRefreshRate", "(F)V");
    if (mid) {
        env->CallStaticVoidMethod(cls, mid, static_cast<jfloat>(fps));
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
}
#endif

bool canSwitchRefresh() {
#if defined(__ANDROID__)
    typedef void* (*PFN_GetJNIEnv)();
    static auto pfnGetJNIEnv = reinterpret_cast<PFN_GetJNIEnv>(dlsym(RTLD_DEFAULT, "SDL_GetAndroidJNIEnv"));
    if (pfnGetJNIEnv) {
        if (auto* env = static_cast<JNIEnv*>(pfnGetJNIEnv())) {
            jclass cls = env->FindClass("com/rafaelngp/psbox/MainActivity");
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, "is120HzSupported", "()Z");
                if (mid) {
                    jboolean supp = env->CallStaticBooleanMethod(cls, mid);
                    env->DeleteLocalRef(cls);
                    if (supp) return true;
                } else {
                    env->DeleteLocalRef(cls);
                }
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }
    if (!g_window) return false;
    SDL_DisplayID displayID = SDL_GetDisplayForWindow(g_window);
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayID, &count);
    bool has120 = false;
    if (modes) {
        for (int i = 0; i < count; ++i) {
            if (modes[i] && modes[i]->refresh_rate >= 119.0f) {
                has120 = true;
                break;
            }
        }
        SDL_free(modes);
    }
    return has120;
#else
    return false;
#endif
}

bool setHighRefresh(bool on) {
#if defined(__ANDROID__)
    if (!g_window) return false;
    float targetFps = on ? 120.0f : 60.0f;
    applyAndroidFrameRate(g_window, targetFps);
    applyAndroidDisplayMode(targetFps);
    return true;
#else
    (void)on;
    return false;
#endif
}

void sdlSetSuspended(bool suspended) {
    g_suspended.store(suspended);
    if (!suspended) {
        g_needSurfaceRecreate.store(true);
        sdlRepaint();
    }
}

bool sdlIsSuspended() {
    return g_suspended.load();
}

void sdlRendererReset() {
    if (gpu::ready()) {
        g_needSurfaceRecreate.store(true);
        sdlRepaint();
        return;
    }
    if (!onMainThread() || !g_renderer) return;
    if (g_uiTex) { SDL_DestroyTexture(g_uiTex); g_uiTex = nullptr; }
    if (g_videoTex) { SDL_DestroyTexture(g_videoTex); g_videoTex = nullptr; g_videoW = g_videoH = 0; }
    if (g_overlayTex) { SDL_DestroyTexture(g_overlayTex); g_overlayTex = nullptr; g_overlayW = g_overlayH = 0; }
    if (g_readTex) { SDL_DestroyTexture(g_readTex); g_readTex = nullptr; }
    g_frontPending = true;
    g_overlayPending = true;
    sdlRepaint();
}

SafeAreaInsets sdlSafeArea() {
    return g_safeArea;
}

void sdlUpdateSafeArea() {
    if (!g_window) return;
#if defined(__ANDROID__)
    typedef void* (*PFN_GetJNIEnv)();
    static auto pfnGetJNIEnv = reinterpret_cast<PFN_GetJNIEnv>(dlsym(RTLD_DEFAULT, "SDL_GetAndroidJNIEnv"));
    if (pfnGetJNIEnv) {
        if (auto* env = static_cast<JNIEnv*>(pfnGetJNIEnv())) {
            jclass cls = env->FindClass("com/rafaelngp/psbox/MainActivity");
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, "getDisplayCutoutInsets", "()[I");
                if (mid) {
                    auto insetsArr = static_cast<jintArray>(env->CallStaticObjectMethod(cls, mid));
                    if (insetsArr) {
                        jint* insets = env->GetIntArrayElements(insetsArr, nullptr);
                        if (insets) {
                            g_safeArea.left = insets[0];
                            g_safeArea.top = insets[1];
                            g_safeArea.right = insets[2];
                            g_safeArea.bottom = insets[3];
                            env->ReleaseIntArrayElements(insetsArr, insets, JNI_ABORT);
                        }
                        env->DeleteLocalRef(insetsArr);
                        env->DeleteLocalRef(cls);
                        int winW = 0, winH = 0;
                        SDL_GetWindowSize(g_window, &winW, &winH);
                        XC_LOGI("display: cutout insets L=%d T=%d R=%d B=%d (window %dx%d)",
                                g_safeArea.left, g_safeArea.top, g_safeArea.right, g_safeArea.bottom, winW, winH);
                        gpu::setSafeAreaInsets(g_safeArea.left, g_safeArea.top, g_safeArea.right, g_safeArea.bottom);
                        if (winH > winW) {
                            gpu::setPresentationMode(gpu::DisplayPresentationMode::TopScreenOnly);
                        } else {
                            gpu::setPresentationMode(gpu::DisplayPresentationMode::Standard);
                        }
                        return;
                    }
                }
                env->DeleteLocalRef(cls);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }
#endif
    SDL_Rect safeRect{0, 0, 0, 0};
    if (SDL_GetWindowSafeArea(g_window, &safeRect)) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(g_window, &winW, &winH);
        if (winW > 0 && winH > 0) {
            g_safeArea.left = safeRect.x;
            g_safeArea.top = safeRect.y;
            g_safeArea.right = winW - (safeRect.x + safeRect.w);
            g_safeArea.bottom = winH - (safeRect.y + safeRect.h);
            XC_LOGI("display: safe area insets L=%d T=%d R=%d B=%d (window %dx%d)",
                    g_safeArea.left, g_safeArea.top, g_safeArea.right, g_safeArea.bottom, winW, winH);
            gpu::setSafeAreaInsets(g_safeArea.left, g_safeArea.top, g_safeArea.right, g_safeArea.bottom);
            if (winH > winW) {
                gpu::setPresentationMode(gpu::DisplayPresentationMode::TopScreenOnly);
            } else {
                gpu::setPresentationMode(gpu::DisplayPresentationMode::Standard);
            }
        }
    }
}
bool bringUpTest(int) {
    XC_LOGW("display: vktest is the PS5's GPU check");
    return false;
}

uint64_t lastPresentId() { return g_lastHanded; }

bool lastShown(uint64_t& id, uint64_t& atUs) {
    std::lock_guard<std::mutex> lock(g_shownMutex);
    if (!g_shownId) return false;
    id = g_shownId, atUs = g_shownAtUs;
    return true;
}

namespace {

// The picture on screen read back (main thread).
bool readBackMain(std::vector<uint8_t>& rgb, int& width, int& height) {
    if (g_front.kind == Kind::None) return false;
    if (g_suspended.load() || g_needSurfaceRecreate.load()) return false;
    if (gpu::ready()) {
        if (g_frontPending || g_overlayPending || g_repaint.exchange(false)) {
            if (!drawGpuFront()) return false;
        }
        return gpu::readBack(rgb, width, height);
    }
    upload();
    if (!g_readTex)
        g_readTex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
    if (!g_readTex || !SDL_SetRenderTarget(g_renderer, g_readTex)) {
        XC_LOGW("display: read back: %s", SDL_GetError());
        return false;
    }
    renderScene();
    SDL_Surface* read = SDL_RenderReadPixels(g_renderer, nullptr);
    SDL_SetRenderTarget(g_renderer, nullptr);
    g_repaint = true;  // the window's picture drawn again at the next pump()
    if (!read) {
        XC_LOGW("display: read back: %s", SDL_GetError());
        return false;
    }
    SDL_Surface* conv = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(read);
    if (!conv) return false;
    width = conv->w, height = conv->h;
    rgb.resize(static_cast<size_t>(width) * height * 3);
    for (int r = 0; r < height; ++r)
        std::memcpy(&rgb[static_cast<size_t>(r) * width * 3], static_cast<const uint8_t*>(conv->pixels) + r * conv->pitch,
                    static_cast<size_t>(width) * 3);
    SDL_DestroySurface(conv);
    return true;
}

}  // namespace

bool readBackRgb(std::vector<uint8_t>& rgb, int& width, int& height) {
    if (!g_renderer && !gpu::ready()) return false;
    if (onMainThread()) return readBackMain(rgb, width, height);
    // The renderer is the main thread's: asked of its next pump(), waited
    // for a while (the main loop pumps each pass; not while it is closing).
    std::unique_lock<std::mutex> lock(g_mailboxMutex);
    if (g_readBack.asked) return false;  // one at a time
    g_readBack = ReadBack{};
    g_readBack.asked = true;
    g_mailboxCv.notify_all();  // idleWait() returns: the main loop pumps
    bool done = g_readBackCv.wait_for(lock, std::chrono::milliseconds(500), [] { return g_readBack.done; });
    g_readBack.asked = false;
    if (!done || !g_readBack.ok) {
        if (!done) XC_LOGW("display: read back from another thread: no pump() came");
        return false;
    }
    rgb = std::move(g_readBack.rgb);
    width = g_readBack.w, height = g_readBack.h;
    return true;
}

bool sdlWindowToLogical(float winX, float winY, float& logX, float& logY) {
    if (!g_window) return false;
    if (g_renderer) {
        return SDL_RenderCoordinatesFromWindow(g_renderer, winX, winY, &logX, &logY);
    }
    int winW = 0, winH = 0;
    SDL_GetWindowSize(g_window, &winW, &winH);
    if (winW <= 0 || winH <= 0) return false;

    float safeX = static_cast<float>(g_safeArea.left);
    float safeY = static_cast<float>(g_safeArea.top);
    float safeW = (winW > (g_safeArea.left + g_safeArea.right))
                      ? static_cast<float>(winW - g_safeArea.left - g_safeArea.right)
                      : static_cast<float>(winW);
    float safeH = (winH > (g_safeArea.top + g_safeArea.bottom))
                      ? static_cast<float>(winH - g_safeArea.top - g_safeArea.bottom)
                      : static_cast<float>(winH);

    bool topOnly = (winH > winW);
    float targetH = topOnly ? (safeH * 0.5f) : safeH;
    float scale = std::min(safeW / static_cast<float>(kWidth), targetH / static_cast<float>(kHeight));
    if (scale <= 0.0f) return false;
    float viewW = static_cast<float>(kWidth) * scale;
    float viewH = static_cast<float>(kHeight) * scale;
    float offsetX = safeX + (safeW - viewW) * 0.5f;
    float offsetY = safeY + (targetH - viewH) * 0.5f;
    logX = (winX - offsetX) / scale;
    logY = (winY - offsetY) / scale;
    return (logX >= 0.0f && logX <= static_cast<float>(kWidth) && logY >= 0.0f && logY <= static_cast<float>(kHeight));
}

}  // namespace xc::display
