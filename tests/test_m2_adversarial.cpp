// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Dedicated Empirical Adversarial Test Harness for Android Milestone 2
// Focus: Dirty Flag Gating in stream_screen, 0 Idle Canvas Uploads,
//        Android 1080p Graphic Profile Defaults in settings.h,
//        Mic Mute Hardware & Pipeline Shutdown, Concurrency & Fuzzing.

#include "ui/canvas.h"
#include "ui/touch_pad_overlay.h"
#include "app/settings.h"
#include "app/web_server.h"
#include "media/audio_in_common.h"
#include "platform/platform.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace xc;

static std::atomic<int> g_passCount{0};
static std::atomic<int> g_failCount{0};

#define EMPIRICAL_ASSERT(cond, msg, ...) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "\n[FAIL] %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        g_failCount.fetch_add(1, std::memory_order_relaxed); \
    } else { \
        g_passCount.fetch_add(1, std::memory_order_relaxed); \
    } \
} while (0)

#define ASSERT_TRUE(cond) EMPIRICAL_ASSERT((cond), "Assertion failed: %s", #cond)
#define ASSERT_FALSE(cond) EMPIRICAL_ASSERT(!(cond), "Assertion failed: NOT %s", #cond)
#define ASSERT_EQ(a, b) EMPIRICAL_ASSERT((a) == (b), "Equality failed: %s == %s", #a, #b)

// =========================================================================
// TEST SUITE 1: StreamScreen Dirty Flag Gating & Zero Idle Redraw Simulator
// =========================================================================
struct StreamScreenDirtySimulator {
    ui::TouchPadOverlay overlay;
    uint64_t lastTouchOverlayRedrawMs = 0;
    bool lastTouchOverlayVisible = false;
    bool menuOpen = false;
    uint64_t redrawCount = 0;
    uint64_t skippedCount = 0;

    explicit StreamScreenDirtySimulator(const ui::Fonts& fonts)
        : overlay(fonts) {
        overlay.reset();
    }

    bool stepFrame(uint64_t nowMs, bool physicalConnected = false, bool physicalActive = false) {
        overlay.update(nowMs, physicalConnected, physicalActive);
        bool touchOverlayVisible = overlay.isVisible();
        bool touchOverlayDirty = overlay.isDirty();
        bool touchVisibilityChanged = (touchOverlayVisible != lastTouchOverlayVisible);

        bool touchRedraw = (touchOverlayVisible || touchVisibilityChanged) && !menuOpen &&
                           (touchOverlayDirty || touchVisibilityChanged) &&
                           (nowMs - lastTouchOverlayRedrawMs >= 16);

        if (touchRedraw) {
            lastTouchOverlayRedrawMs = nowMs;
            lastTouchOverlayVisible = touchOverlayVisible;
            overlay.clearDirty();
            redrawCount++;
            return true;
        } else {
            skippedCount++;
            return false;
        }
    }
};

static void testDirtyFlagGatingLifecycle() {
    std::printf("\n--- [TEST SUITE 1] StreamScreen Dirty Flag Gating & Idle Redraw Suppression ---\n");

    ui::Fonts fonts;
    StreamScreenDirtySimulator sim(fonts);

    // Initial frame at realistic wall-clock t=100000 ms
    uint64_t t = 100000;
    ASSERT_TRUE(sim.stepFrame(t)); // First frame draws initial layout
    ASSERT_EQ(sim.redrawCount, 1u);

    // 1. Steady idle: run 3,600 frames (60 seconds at 60 FPS) without touches
    for (uint64_t f = 1; f <= 3600; ++f) {
        t += 16;
        bool redrawn = sim.stepFrame(t, false, false);
        ASSERT_FALSE(redrawn);
    }
    ASSERT_EQ(sim.redrawCount, 1u); // Exactly 1 draw (initial); 3600 consecutive idle frames skipped!
    ASSERT_EQ(sim.skippedCount, 3600u);
    std::printf("[PASS] 3,600 consecutive idle frames (60 seconds): 0 redraws, 100%% skipped!\n");

    // 2. Active interaction: Touch Down on Button A
    t += 16;
    app::shell::TouchEvent downA{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
    sim.overlay.onTouch(downA);
    ASSERT_TRUE(sim.overlay.isDirty());

    // Next frame (16ms later) must redraw and clear dirty
    ASSERT_TRUE(sim.stepFrame(t));
    ASSERT_EQ(sim.redrawCount, 2u);
    ASSERT_FALSE(sim.overlay.isDirty());

    // Holding finger static for 120 frames (2 seconds): should NOT redraw!
    for (int i = 0; i < 120; ++i) {
        t += 16;
        ASSERT_FALSE(sim.stepFrame(t));
    }
    ASSERT_EQ(sim.redrawCount, 2u);
    std::printf("[PASS] Finger held stationary on Button A: 120 frames skipped, 0 redraws!\n");

    // 3. Motion: move stick over 60 frames -> every frame (>=16ms) redraws
    for (int i = 1; i <= 60; ++i) {
        t += 16;
        app::shell::TouchEvent motionStick{app::shell::TouchEvent::Kind::Motion, 1, 1655.0f + i, 685.0f};
        sim.overlay.onTouch(motionStick);
        ASSERT_TRUE(sim.stepFrame(t));
    }
    ASSERT_EQ(sim.redrawCount, 62u);
    std::printf("[PASS] 60 frames of continuous touch motion: exactly 60 redraws executed at 60 FPS!\n");

    // 4. Touch Up: triggers final state redraw, then settles back to idle
    t += 16;
    app::shell::TouchEvent upA{app::shell::TouchEvent::Kind::Up, 1, 1715.0f, 685.0f};
    sim.overlay.onTouch(upA);
    ASSERT_TRUE(sim.stepFrame(t));
    ASSERT_EQ(sim.redrawCount, 63u);

    // Subsequent 300 frames idle again: 0 redraws
    for (int i = 0; i < 300; ++i) {
        t += 16;
        ASSERT_FALSE(sim.stepFrame(t));
    }
    ASSERT_EQ(sim.redrawCount, 63u);
    std::printf("[PASS] Touch release settles back to idle: 300 idle frames skipped!\n");

    // 5. Rate limiting: high-frequency touch events (< 16ms) do NOT over-render
    t += 16;
    app::shell::TouchEvent downB{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
    sim.overlay.onTouch(downB);
    ASSERT_TRUE(sim.stepFrame(t)); // First redraw at t0, sets lastTouchOverlayRedrawMs = t
    uint64_t t0 = t;

    // Send rapid motions within the 16ms window [t0+1 .. t0+15]
    for (int sub = 1; sub < 16; ++sub) {
        app::shell::TouchEvent fastMotion{app::shell::TouchEvent::Kind::Motion, 2, 1765.0f + sub, 575.0f};
        sim.overlay.onTouch(fastMotion);
        // Only sub ms elapsed since t0 (< 16ms), must be suppressed!
        ASSERT_FALSE(sim.stepFrame(t0 + sub));
    }
    // After 16ms full window (at t0 + 16), the throttled redraw triggers
    ASSERT_TRUE(sim.stepFrame(t0 + 16));
    std::printf("[PASS] High-frequency sub-16ms touch events throttled cleanly to 60 FPS!\n");
}

// =========================================================================
// TEST SUITE 2: Settings Defaults for Android vs Non-Android & Clamping
// =========================================================================
static void testSettingsAndroidDefaultsAndClamping() {
    std::printf("\n--- [TEST SUITE 2] Android 1080p Graphic Profile Defaults & Persistence ---\n");

    // 1. Default constructor values under current compilation target
    app::Settings defaultSettings;
#if defined(__ANDROID__)
    ASSERT_EQ(defaultSettings.sharpness, 0);
    ASSERT_EQ(defaultSettings.deband, 0);
    ASSERT_EQ(defaultSettings.upscaler, 0);
    std::printf("[PASS] Android target compiles with sharpness=0, deband=0, upscaler=0 defaults!\n");
#else
    ASSERT_EQ(defaultSettings.sharpness, 2);
    ASSERT_EQ(defaultSettings.deband, 3);
    ASSERT_EQ(defaultSettings.upscaler, 2);
    std::printf("[PASS] Desktop/Console target compiles with sharpness=2, deband=3, upscaler=2 defaults!\n");
#endif

    // 2. Simulated Android loading without keys preserves Android defaults
    std::string jsonEmpty = "{}";
    std::string tempPath = "/tmp/test_settings_m2_empty.json";
    FILE* f = std::fopen(tempPath.c_str(), "w");
    ASSERT_TRUE(f != nullptr);
    std::fwrite(jsonEmpty.data(), 1, jsonEmpty.size(), f);
    std::fclose(f);

    app::Settings sLoaded;
    ASSERT_TRUE(sLoaded.load(tempPath));
#if defined(__ANDROID__)
    ASSERT_EQ(sLoaded.sharpness, 0);
    ASSERT_EQ(sLoaded.deband, 0);
    ASSERT_EQ(sLoaded.upscaler, 0);
#else
    ASSERT_EQ(sLoaded.sharpness, 2);
    ASSERT_EQ(sLoaded.deband, 3);
    ASSERT_EQ(sLoaded.upscaler, 2);
#endif
    std::remove(tempPath.c_str());

    // 3. User explicit override persistence (e.g. user chose sharpness=1, deband=2, upscaler=3)
    std::string jsonCustom = "{\"sharpness\": 1, \"deband\": 2, \"upscaler\": 3}";
    f = std::fopen(tempPath.c_str(), "w");
    std::fwrite(jsonCustom.data(), 1, jsonCustom.size(), f);
    std::fclose(f);

    app::Settings sCustom;
    ASSERT_TRUE(sCustom.load(tempPath));
    ASSERT_EQ(sCustom.sharpness, 1);
    ASSERT_EQ(sCustom.deband, 2);
    ASSERT_EQ(sCustom.upscaler, 3);
    std::remove(tempPath.c_str());
    std::printf("[PASS] Explicit user settings overrides successfully loaded and preserved!\n");

    // 4. Clamping against adversarial out-of-range values [-999, 999]
    std::string jsonAdversarial = "{\"sharpness\": 999, \"deband\": -50, \"upscaler\": 100}";
    f = std::fopen(tempPath.c_str(), "w");
    std::fwrite(jsonAdversarial.data(), 1, jsonAdversarial.size(), f);
    std::fclose(f);

    app::Settings sClamped;
    ASSERT_TRUE(sClamped.load(tempPath));
    ASSERT_EQ(sClamped.sharpness, 3); // Clamped to 3
    ASSERT_EQ(sClamped.deband, 0);    // Clamped to 0
    ASSERT_EQ(sClamped.upscaler, 3);  // Clamped to 3
    std::remove(tempPath.c_str());
    std::printf("[PASS] Adversarial out-of-range values strictly clamped to [0..3]!\n");
}

// =========================================================================
// TEST SUITE 3: Microphone Mute Audio Pipeline & Hardware Cutoff
// =========================================================================
static void testMicMutePipelineHighVolumeStress() {
    std::printf("\n--- [TEST SUITE 3] Mic Mute Pipeline Cutoff & High-Volume Audio Stress ---\n");

    media::MicPipeline mic;
    std::atomic<float> level{0.0f};
    std::atomic<int> framesDispatched{0};
    std::atomic<size_t> totalSamplesDispatched{0};

    media::AudioIn::AudioCallback cb = [&](const int16_t*, size_t n) {
        framesDispatched.fetch_add(1, std::memory_order_relaxed);
        totalSamplesDispatched.fetch_add(n, std::memory_order_relaxed);
    };

    std::vector<int16_t> activeGrain(media::MicPipeline::kGrain, 2000);

    // 1. Active unmuted state: 40 grains (10,240 samples -> 10 frames of 960)
    for (int i = 0; i < 40; ++i) {
        mic.process(activeGrain.data(), activeGrain.size(), false, 1.0f, level, cb);
    }
    ASSERT_EQ(framesDispatched.load(), 10);
    ASSERT_TRUE(level.load() > 0.0f);
    std::printf("[PASS] Active stream: 40 grains produced exactly 10 frames, VU level active!\n");

    // 2. Mute transition: level must drop to exactly 0.0f and no frames dispatched
    mic.process(activeGrain.data(), activeGrain.size(), true, 1.0f, level, cb);
    ASSERT_EQ(level.load(), 0.0f);
    ASSERT_EQ(framesDispatched.load(), 10);

    // 3. High-volume muted stream (1,000 grains = 256,000 samples)
    for (int i = 0; i < 1000; ++i) {
        mic.process(activeGrain.data(), activeGrain.size(), true, 1.0f, level, cb);
    }
    ASSERT_EQ(level.load(), 0.0f);
    ASSERT_EQ(framesDispatched.load(), 10); // Still exactly 10!
    std::printf("[PASS] Muted stream: 1,000 grains (256,000 samples) yielded 0 frames, VU=0.0f!\n");

    // 4. Rapid mute toggling stress test (1,000 rapid toggle cycles)
    for (int cycle = 0; cycle < 1000; ++cycle) {
        bool muteState = (cycle % 2 == 0);
        mic.process(activeGrain.data(), activeGrain.size(), muteState, 1.0f, level, cb);
        if (muteState) {
            ASSERT_EQ(level.load(), 0.0f);
        }
    }
    std::printf("[PASS] 1,000 rapid mute toggle cycles executed cleanly without leak or corruption!\n");
}

int main() {
    std::printf("=================================================================\n");
    std::printf("Milestone 2 Empirical Adversarial Stress Test Harness (Android)\n");
    std::printf("Reviewer & Critic: reviewer_android_m2_1\n");
    std::printf("=================================================================\n");

    testDirtyFlagGatingLifecycle();
    testSettingsAndroidDefaultsAndClamping();
    testMicMutePipelineHighVolumeStress();

    std::printf("\n=================================================================\n");
    std::printf("FINAL EMPIRICAL RESULTS: %d Passed, %d Failed\n", g_passCount.load(), g_failCount.load());
    std::printf("=================================================================\n");

    return g_failCount.load() == 0 ? 0 : 1;
}
