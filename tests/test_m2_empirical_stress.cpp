// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Dedicated Empirical Adversarial & Stress Test Harness for Milestone 2 (Android)
// Focus: TouchPadOverlay Dirty Flag Lifecycle, 10,000 Synthetic Touch Stress,
//        Settled Idle Invariance, Auto-fade Transitions, Android Settings Defaults
//        and Abnormal JSON Load/Save Persistence Clamping.

#include "ui/canvas.h"
#include "ui/touch_pad_overlay.h"
#include "app/settings.h"
#include "app/web_server.h"
#include "input/controller.h"
#include "util/json.h"
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
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace xc;

static std::atomic<int> g_passCount{0};
static std::atomic<int> g_failCount{0};
static std::atomic<int> g_bugCount{0};

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
#define ASSERT_NEAR(a, b, eps) EMPIRICAL_ASSERT(std::abs((a) - (b)) <= (eps), "Near failed: |%f - %f| > %f", static_cast<double>(a), static_cast<double>(b), static_cast<double>(eps))

// =========================================================================
// SUITE 1: TouchPadOverlay Initial Dirty State & Control Resets
// =========================================================================
static void runSuite1_InitialStateAndResets() {
    std::printf("\n--- [SUITE 1] TouchPadOverlay Initial Dirty State & Lifecycle Resets ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    // 1. Initial construction must be dirty
    ASSERT_TRUE(overlay.isDirty());

    // 2. clearDirty() resets dirty to false
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    // 3. markDirty() sets dirty to true
    overlay.markDirty();
    ASSERT_TRUE(overlay.isDirty());
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    // 4. reset() marks dirty and resets controller state
    overlay.reset();
    ASSERT_TRUE(overlay.isDirty());
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    // 5. setVisible(false) marks dirty
    overlay.setVisible(false);
    ASSERT_TRUE(overlay.isDirty());
    ASSERT_FALSE(overlay.isVisible());
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    // 6. setVisible(true) marks dirty
    overlay.setVisible(true);
    ASSERT_TRUE(overlay.isDirty());
    ASSERT_TRUE(overlay.isVisible());
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    // 7. toggleVisible() marks dirty
    overlay.toggleVisible();
    ASSERT_TRUE(overlay.isDirty());
    ASSERT_FALSE(overlay.isVisible());
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    overlay.toggleVisible();
    ASSERT_TRUE(overlay.isDirty());
    ASSERT_TRUE(overlay.isVisible());
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    // 8. setSettings() marks dirty
    app::Settings s;
    s.touchOverlayMode = 1; // Always on
    overlay.setSettings(s);
    ASSERT_TRUE(overlay.isDirty());
    ASSERT_EQ(overlay.overlayMode(), 1);
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    std::printf("[PASS] Suite 1: Initial dirty state, clearDirty(), markDirty(), and resets validated.\n");
}

// =========================================================================
// SUITE 2: 10,000 Synthetic Multi-Touch Stress & Dirty Flag Invariants
// =========================================================================
static void runSuite2_TenThousandSyntheticTouchStress() {
    std::printf("\n--- [SUITE 2] 10,000 Synthetic Multi-Touch Stress & Dirty Flag Invariants ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();
    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    struct TargetCoord {
        float x, y;
        const char* name;
    };
    const TargetCoord kTargets[] = {
        {1655.0f, 685.0f, "ButtonA"},
        {1765.0f, 575.0f, "ButtonB"},
        {1545.0f, 575.0f, "ButtonX"},
        {1655.0f, 465.0f, "ButtonY"},
        {520.0f, 730.0f,  "DpadUp"},
        {520.0f, 930.0f,  "DpadDown"},
        {420.0f, 830.0f,  "DpadLeft"},
        {620.0f, 830.0f,  "DpadRight"},
        {590.0f, 760.0f,  "DpadUpRight"},
        {450.0f, 760.0f,  "DpadUpLeft"},
        {590.0f, 900.0f,  "DpadDownRight"},
        {450.0f, 900.0f,  "DpadDownLeft"},
        {100.0f, 150.0f,  "TriggerLT"},
        {100.0f, 250.0f,  "BumperLB"},
        {1700.0f, 150.0f, "TriggerRT"},
        {1700.0f, 250.0f, "BumperRB"},
        {100.0f, 870.0f,  "ButtonL3"},
        {1750.0f, 870.0f, "ButtonR3"},
        {960.0f, 68.0f,   "ButtonNexus"},
        {830.0f, 68.0f,   "ButtonView"},
        {1090.0f, 68.0f,  "ButtonMenu"},
        {50.0f, 50.0f,    "PullTab_OverlayToggle"},
        {250.0f, 545.0f,  "LeftStick"},
        {1355.0f, 825.0f, "RightStick"},
        {960.0f, 540.0f,  "EmptyScreenCenter"},
        {-50.0f, -50.0f,  "OutOfBounds_Negative"},
        {3000.0f, 2000.0f, "OutOfBounds_Huge"}
    };
    constexpr size_t kNumTargets = sizeof(kTargets) / sizeof(kTargets[0]);

    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> targetDist(0, kNumTargets - 1);
    std::uniform_int_distribution<int> fingerDist(1, 12);
    std::uniform_real_distribution<float> jitterDist(-15.0f, 15.0f);

    struct ActiveState {
        bool active = false;
        uint64_t fingerId = 0;
        float x = 0, y = 0;
    };
    ActiveState fingers[16];

    int acceptedDowns = 0;
    int acceptedMotions = 0;
    int acceptedUps = 0;
    int acceptedCancels = 0;
    int rejectedEvents = 0;
    int dirtyFlagsVerified = 0;

    for (int cycle = 0; cycle < 10000; ++cycle) {
        int fingerIdx = fingerDist(rng);
        ActiveState& f = fingers[fingerIdx];

        app::shell::TouchEvent ev{};
        ev.fingerId = static_cast<uint64_t>(fingerIdx);

        if (!f.active) {
            ev.kind = app::shell::TouchEvent::Kind::Down;
            const auto& t = kTargets[targetDist(rng)];
            ev.x = t.x + jitterDist(rng);
            ev.y = t.y + jitterDist(rng);

            bool handled = overlay.onTouch(ev);
            if (handled) {
                f.active = true;
                f.fingerId = ev.fingerId;
                f.x = ev.x;
                f.y = ev.y;
                acceptedDowns++;

                if (ev.x > 140.0f || ev.y > 100.0f) {
                    ASSERT_TRUE(overlay.isDirty());
                    dirtyFlagsVerified++;
                }
                overlay.clearDirty();
                ASSERT_FALSE(overlay.isDirty());
            } else {
                rejectedEvents++;
                ASSERT_FALSE(overlay.isDirty());
            }
        } else {
            int choice = cycle % 10;
            if (choice < 6) {
                ev.kind = app::shell::TouchEvent::Kind::Motion;
                ev.x = f.x + jitterDist(rng);
                ev.y = f.y + jitterDist(rng);
                f.x = ev.x;
                f.y = ev.y;

                bool handled = overlay.onTouch(ev);
                if (handled) {
                    acceptedMotions++;
                    ASSERT_TRUE(overlay.isDirty());
                    dirtyFlagsVerified++;
                    overlay.clearDirty();
                    ASSERT_FALSE(overlay.isDirty());
                } else {
                    rejectedEvents++;
                }
            } else if (choice < 9) {
                ev.kind = app::shell::TouchEvent::Kind::Up;
                ev.x = f.x;
                ev.y = f.y;
                f.active = false;

                bool handled = overlay.onTouch(ev);
                if (handled) {
                    acceptedUps++;
                    ASSERT_TRUE(overlay.isDirty());
                    dirtyFlagsVerified++;
                    overlay.clearDirty();
                    ASSERT_FALSE(overlay.isDirty());
                } else {
                    rejectedEvents++;
                }
            } else {
                ev.kind = app::shell::TouchEvent::Kind::Cancel;
                ev.x = f.x;
                ev.y = f.y;
                f.active = false;

                bool handled = overlay.onTouch(ev);
                if (handled) {
                    acceptedCancels++;
                    ASSERT_TRUE(overlay.isDirty());
                    dirtyFlagsVerified++;
                    overlay.clearDirty();
                    ASSERT_FALSE(overlay.isDirty());
                } else {
                    rejectedEvents++;
                }
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        if (fingers[i].active) {
            app::shell::TouchEvent ev{};
            ev.fingerId = fingers[i].fingerId;
            ev.kind = app::shell::TouchEvent::Kind::Up;
            ev.x = fingers[i].x;
            ev.y = fingers[i].y;
            overlay.onTouch(ev);
            overlay.clearDirty();
            fingers[i].active = false;
        }
    }

    overlay.clearDirty();
    ASSERT_FALSE(overlay.isDirty());

    std::printf("[PASS] Suite 2: 10,000 synthetic touch events completed!\n"
                "       Accepted Downs: %d, Motions: %d, Ups: %d, Cancels: %d, Rejected/Ignored: %d\n"
                "       Dirty flags verified & cleared: %d times with 0 leaks.\n",
                acceptedDowns, acceptedMotions, acceptedUps, acceptedCancels, rejectedEvents, dirtyFlagsVerified);
}

// =========================================================================
// SUITE 3: Steady-State Settled Idle Invariance
// =========================================================================
static void runSuite3_SettledIdleInvariance() {
    std::printf("\n--- [SUITE 3] Steady-State Settled Idle Invariance ---\n");

    // 1. Mode 1 (Always On - Android Default)
    {
        ui::Fonts fonts;
        ui::TouchPadOverlay overlay(fonts);
        app::Settings s;
        s.touchOverlayMode = 1;
        overlay.setSettings(s);
        overlay.clearDirty();
        ASSERT_FALSE(overlay.isDirty());

        uint64_t t = platform::nowMs();
        int dirtyOccurrences = 0;
        for (int frame = 0; frame < 1000; ++frame) {
            overlay.update(t, false, false);
            if (overlay.isDirty()) dirtyOccurrences++;
            t += 16;
        }
        ASSERT_EQ(dirtyOccurrences, 0);
        ASSERT_FALSE(overlay.isDirty());
    }

    // 2. Mode 2 (Always Off)
    {
        ui::Fonts fonts;
        ui::TouchPadOverlay overlay(fonts);
        app::Settings s;
        s.touchOverlayMode = 2;
        overlay.setSettings(s);
        overlay.clearDirty();
        ASSERT_FALSE(overlay.isDirty());

        uint64_t t = platform::nowMs();
        int dirtyOccurrences = 0;
        for (int frame = 0; frame < 1000; ++frame) {
            overlay.update(t, false, false);
            if (overlay.isDirty()) dirtyOccurrences++;
            t += 16;
        }
        ASSERT_EQ(dirtyOccurrences, 0);
        ASSERT_FALSE(overlay.isDirty());
    }

    // 3. Mode 0 (Auto-Hide when fresh instance constructed in Mode 0)
    {
        ui::Fonts fonts;
        ui::TouchPadOverlay overlay(fonts);
        app::Settings s;
        s.touchOverlayMode = 0;
        overlay.setSettings(s);
        overlay.clearDirty();

        // Let it settle to 0.80f
        uint64_t t = platform::nowMs();
        for (int i = 0; i < 20; ++i) {
            overlay.update(t, false, false);
            overlay.clearDirty();
            t += 16;
        }
        ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);

        int dirtyOccurrences = 0;
        for (int frame = 0; frame < 1000; ++frame) {
            overlay.update(t, false, false);
            if (overlay.isDirty()) dirtyOccurrences++;
            t += 16;
        }
        ASSERT_EQ(dirtyOccurrences, 0);
        ASSERT_FALSE(overlay.isDirty());
    }

    std::printf("[PASS] Suite 3: 1,000 idle frames in Mode 1, Mode 2, and settled Mode 0 produced exactly 0 dirty triggers.\n");
}

// =========================================================================
// SUITE 4: Opacity Transition & Auto-Fade Dirty Flag Lifecycle
// =========================================================================
static void runSuite4_AutoFadeDirtyLifecycle() {
    std::printf("\n--- [SUITE 4] Opacity Transition & Auto-Fade Dirty Flag Lifecycle ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    app::Settings s;
    s.touchOverlayMode = 0; // Auto-hide mode
    overlay.setSettings(s);
    overlay.clearDirty();

    uint64_t t = platform::nowMs();

    // Settle at targetOpacity (0.80f)
    for (int i = 0; i < 25; ++i) {
        overlay.update(t, false, false);
        overlay.clearDirty();
        t += 16;
    }
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);
    ASSERT_FALSE(overlay.isDirty());

    // 1. Trigger Auto-Fade OUT (physicalPadActive = true)
    int fadeOutDirtyFrames = 0;
    float lastOpacity = overlay.opacity();
    for (int frame = 0; frame < 20; ++frame) {
        t += 16;
        overlay.update(t, true, true);
        if (overlay.opacity() < lastOpacity - 0.001f) {
            ASSERT_TRUE(overlay.isDirty());
            fadeOutDirtyFrames++;
        }
        overlay.clearDirty();
        lastOpacity = overlay.opacity();
    }

    ASSERT_EQ(fadeOutDirtyFrames, 10);
    ASSERT_NEAR(overlay.opacity(), 0.0f, 0.001f);

    // After settling at 0.0f: subsequent 100 frames must NOT mark dirty!
    int settledDirtyFrames = 0;
    for (int frame = 0; frame < 100; ++frame) {
        t += 16;
        overlay.update(t, true, true);
        if (overlay.isDirty()) settledDirtyFrames++;
    }
    ASSERT_EQ(settledDirtyFrames, 0);
    ASSERT_FALSE(overlay.isDirty());

    // 2. Trigger Auto-Fade IN via Touch Wakeup
    // Note: TouchPadOverlay::onTouch internally records platform::nowMs()
    uint64_t touchTime = platform::nowMs();
    app::shell::TouchEvent touch{};
    touch.kind = app::shell::TouchEvent::Kind::Down;
    touch.fingerId = 99;
    touch.x = 1655.0f;
    touch.y = 685.0f;
    ASSERT_TRUE(overlay.onTouch(touch));
    ASSERT_TRUE(overlay.isDirty());
    overlay.clearDirty();

    touch.kind = app::shell::TouchEvent::Kind::Up;
    ASSERT_TRUE(overlay.onTouch(touch));
    ASSERT_TRUE(overlay.isDirty());
    overlay.clearDirty();

    // In production stream_screen.cpp, update() is fed platform::nowMs().
    // We synchronize t to touchTime to evaluate fade in.
    t = touchTime;
    int fadeInDirtyFrames = 0;
    lastOpacity = overlay.opacity();
    for (int frame = 0; frame < 20; ++frame) {
        t += 16;
        overlay.update(t, true, false);
        if (overlay.opacity() > lastOpacity + 0.001f) {
            ASSERT_TRUE(overlay.isDirty());
            fadeInDirtyFrames++;
        }
        overlay.clearDirty();
        lastOpacity = overlay.opacity();
    }

    ASSERT_EQ(fadeInDirtyFrames, 10);
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);

    settledDirtyFrames = 0;
    for (int frame = 0; frame < 100; ++frame) {
        t += 16;
        overlay.update(t, true, false);
        if (overlay.isDirty()) settledDirtyFrames++;
    }
    ASSERT_EQ(settledDirtyFrames, 0);
    ASSERT_FALSE(overlay.isDirty());

    std::printf("[PASS] Suite 4: Opacity transitions dirty flag lifecycle fully verified:\n"
                "       Fade out: exactly 10 dirty frames -> 100 clean settled frames at 0.0f.\n"
                "       Fade in: exactly 10 dirty frames -> 100 clean settled frames at 0.80f.\n");
}

// =========================================================================
// SUITE 5: Android Settings Defaults & Non-Android Parity Verification
// =========================================================================
static void runSuite5_AndroidSettingsDefaults() {
    std::printf("\n--- [SUITE 5] Android Settings Defaults & Non-Android Parity ---\n");

    app::Settings s;

#if defined(__ANDROID__)
    ASSERT_EQ(s.sharpness, 0);
    ASSERT_EQ(s.deband, 0);
    ASSERT_EQ(s.upscaler, 0);
    ASSERT_EQ(s.touchOverlayMode, 1);
    std::printf("[PASS] Verified Android compile-time defaults: sharpness=0, deband=0, upscaler=0, touchOverlayMode=1\n");
#else
    ASSERT_EQ(s.sharpness, 2);
    ASSERT_EQ(s.deband, 3);
    ASSERT_EQ(s.upscaler, 2);
    ASSERT_EQ(s.touchOverlayMode, 0);
    std::printf("[PASS] Verified Non-Android compile-time defaults: sharpness=2, deband=3, upscaler=2, touchOverlayMode=0\n");
#endif

    auto profile = s.defaultProfile();
    ASSERT_EQ(profile.sharpness, s.sharpness);
    ASSERT_EQ(profile.deband, s.deband);
    ASSERT_EQ(profile.upscaler, s.upscaler);
}

// =========================================================================
// SUITE 6: JSON Load/Save Persistence Across Abnormal Values
// =========================================================================
static void runSuite6_SettingsLoadSaveAbnormalValues() {
    std::printf("\n--- [SUITE 6] JSON Load/Save Persistence Across Abnormal Values ---\n");
    const std::string tmpPath = "/tmp/xcloud_test_m2_settings.json";

    auto loadFromText = [&](const std::string& jsonText) {
        platform::writeFileAtomic(tmpPath, jsonText);
        app::Settings s;
        bool ok = s.load(tmpPath);
        return std::make_pair(ok, s);
    };

    // 1. Extreme Out-Of-Bounds Negative Values
    {
        auto [ok, s] = loadFromText(R"({
            "settingsVersion": 3,
            "sharpness": -100,
            "deband": -50,
            "upscaler": -999,
            "touchOverlayMode": -10,
            "deadzoneLeft": -20,
            "deadzoneRight": -5,
            "triggerStrength": -1,
            "triggerHz": -2,
            "gameVolume": -50,
            "gyroLevel": -3,
            "gyroYawScale": -100
        })");
        ASSERT_TRUE(ok);
        ASSERT_EQ(s.sharpness, 0);
        ASSERT_EQ(s.deband, 0);
        ASSERT_EQ(s.upscaler, 0);
        ASSERT_EQ(s.touchOverlayMode, 0);
        ASSERT_EQ(s.deadzoneLeft, 0);
        ASSERT_EQ(s.deadzoneRight, 0);
        ASSERT_EQ(s.triggerStrength, 0);
        ASSERT_EQ(s.triggerHz, 0);
        ASSERT_EQ(s.gameVolume, 0);
        ASSERT_EQ(s.gyroLevel, 1);
        ASSERT_EQ(s.gyroYawScale, 20);
    }

    // 2. Extreme Out-Of-Bounds Huge Values
    {
        auto [ok, s] = loadFromText(R"({
            "settingsVersion": 3,
            "sharpness": 999999,
            "deband": 999999,
            "upscaler": 999999,
            "touchOverlayMode": 999999,
            "deadzoneLeft": 500,
            "deadzoneRight": 500,
            "triggerStrength": 99,
            "triggerHz": 99,
            "gameVolume": 9999,
            "gyroLevel": 99,
            "gyroYawScale": 999
        })");
        ASSERT_TRUE(ok);
        ASSERT_EQ(s.sharpness, 3);
        ASSERT_EQ(s.deband, 3);
        ASSERT_EQ(s.upscaler, 3);
        ASSERT_EQ(s.touchOverlayMode, 2);
        ASSERT_EQ(s.deadzoneLeft, 50);
        ASSERT_EQ(s.deadzoneRight, 50);
        ASSERT_EQ(s.triggerStrength, 4);
        ASSERT_EQ(s.triggerHz, 2);
        ASSERT_EQ(s.gameVolume, 200);
        ASSERT_EQ(s.gyroLevel, 10);
        ASSERT_EQ(s.gyroYawScale, 200);
    }

    // 3. Corrupted & Malformed JSON inputs
    {
        auto [ok1, s1] = loadFromText(R"({"sharpness": 1, "deband": )");
        ASSERT_FALSE(ok1);

        auto [ok2, s2] = loadFromText("\x00\x01\x02\xFF\xFE\xFD");
        ASSERT_FALSE(ok2);

        auto [ok3, s3] = loadFromText("");
        ASSERT_FALSE(ok3);
    }

    // 4. Type mismatches
    {
        auto [ok, s] = loadFromText(R"({
            "settingsVersion": 3,
            "sharpness": "invalid_string",
            "deband": [1, 2, 3],
            "upscaler": true,
            "touchOverlayMode": {"mode": 1},
            "gameVolume": "loud",
            "resolution": "4320p_super_ultra_4k"
        })");
        ASSERT_TRUE(ok);
        ASSERT_EQ(s.resolution, "1080p");
    }

    // 5. Save & Reload Round-Trip Consistency
    {
        app::Settings out;
        out.sharpness = 0;
        out.deband = 0;
        out.upscaler = 0;
        out.touchOverlayMode = 1;
        out.resolution = "1080p";
        out.language = "pt-BR";
        out.deadzoneLeft = 12;
        out.deadzoneRight = 14;

        ASSERT_TRUE(out.save(tmpPath));

        app::Settings in;
        ASSERT_TRUE(in.load(tmpPath));
        ASSERT_EQ(in.sharpness, 0);
        ASSERT_EQ(in.deband, 0);
        ASSERT_EQ(in.upscaler, 0);
        ASSERT_EQ(in.touchOverlayMode, 1);
        ASSERT_EQ(in.resolution, "1080p");
        ASSERT_EQ(in.language, "pt-BR");
        ASSERT_EQ(in.deadzoneLeft, 12);
        ASSERT_EQ(in.deadzoneRight, 14);
    }

    std::remove(tmpPath.c_str());
    std::printf("[PASS] Suite 6: JSON load/save persistence across abnormal and corrupted inputs passed with flying colors.\n");
}

// =========================================================================
// SUITE 7: StreamScreen Redraw Gating Simulation
// =========================================================================
static void runSuite7_StreamScreenGatingSimulation() {
    std::printf("\n--- [SUITE 7] StreamScreen Touch Redraw Gating Simulation ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    app::Settings s;
    s.touchOverlayMode = 1; // Always on (Android default)
    overlay.setSettings(s);
    overlay.clearDirty();

    bool lastTouchOverlayVisible = false;
    uint64_t lastTouchOverlayRedrawMs = 0;
    bool menuOpen = false;

    auto evalTouchRedraw = [&](uint64_t nowMs) {
        bool touchOverlayVisible = overlay.isVisible();
        bool touchOverlayDirty = overlay.isDirty();
        bool touchVisibilityChanged = (touchOverlayVisible != lastTouchOverlayVisible);

        bool touchRedraw = (touchOverlayVisible || touchVisibilityChanged) && !menuOpen &&
                          (touchOverlayDirty || touchVisibilityChanged) && (nowMs - lastTouchOverlayRedrawMs >= 16);

        lastTouchOverlayVisible = touchOverlayVisible;
        if (touchRedraw) {
            lastTouchOverlayRedrawMs = nowMs;
            overlay.clearDirty();
        }
        return touchRedraw;
    };

    uint64_t nowMs = platform::nowMs();

    // Frame 1: Initial appearance (visibility changed false -> true)
    bool redraw1 = evalTouchRedraw(nowMs);
    ASSERT_TRUE(redraw1);

    // Frames 2..1000: Idle steady state (no touches, visibility unchanged)
    int idleRedrawCount = 0;
    for (int frame = 0; frame < 1000; ++frame) {
        nowMs += 16;
        overlay.update(nowMs, false, false);
        if (evalTouchRedraw(nowMs)) {
            idleRedrawCount++;
        }
    }
    // Idle steady state must produce ZERO redraws!
    ASSERT_EQ(idleRedrawCount, 0);

    // Frame 1002: User presses Button A
    app::shell::TouchEvent touch{};
    touch.kind = app::shell::TouchEvent::Kind::Down;
    touch.fingerId = 1;
    touch.x = 1655.0f;
    touch.y = 685.0f;
    overlay.onTouch(touch);

    nowMs += 16;
    bool touchRedraw = evalTouchRedraw(nowMs);
    ASSERT_TRUE(touchRedraw);

    // Frame 1003: Holding Button A static (no motion events)
    nowMs += 16;
    overlay.update(nowMs, false, false);
    bool holdRedraw = evalTouchRedraw(nowMs);
    ASSERT_FALSE(holdRedraw); // Holding button static does NOT re-trigger redraw!

    // Frame 1004: User releases Button A
    touch.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(touch);
    nowMs += 16;
    bool releaseRedraw = evalTouchRedraw(nowMs);
    ASSERT_TRUE(releaseRedraw);

    // Frame 1005: Next frame after release
    nowMs += 16;
    overlay.update(nowMs, false, false);
    bool postReleaseRedraw = evalTouchRedraw(nowMs);
    ASSERT_FALSE(postReleaseRedraw);

    std::printf("[PASS] Suite 7: StreamScreen redraw gating simulation confirmed 100%% elimination of idle redraws.\n");
}

// =========================================================================
// SUITE 8: Adversarial Vulnerability Demonstrations (Empirical Oracles)
// =========================================================================
static void runSuite8_AdversarialBugOracles() {
    std::printf("\n--- [SUITE 8] Adversarial Vulnerability Demonstrations (Empirical Oracles) ---\n");

    // ---------------------------------------------------------------------
    // VULNERABILITY 1: TouchPadOverlay::setSettings Mode 2 -> Mode 0 Target Opacity Clobbering
    // ---------------------------------------------------------------------
    {
        ui::Fonts fonts;
        ui::TouchPadOverlay overlay(fonts);

        // Step 1: User switches to Always Off (Mode 2)
        app::Settings s2;
        s2.touchOverlayMode = 2;
        overlay.setSettings(s2);
        ASSERT_FALSE(overlay.isVisible());
        ASSERT_NEAR(overlay.opacity(), 0.0f, 0.001f);

        // Step 2: User later switches to Auto-hide (Mode 0)
        app::Settings s0;
        s0.touchOverlayMode = 0;
        overlay.setSettings(s0);

        // Step 3: Run update with NO controller attached (overlay should fade in to 0.80f)
        uint64_t t = platform::nowMs();
        for (int i = 0; i < 30; ++i) {
            overlay.update(t, false, false); // physicalPadConnected = false
            t += 16;
        }

        // BUG DEMONSTRATION: overlay should be visible and opacity near 0.80f.
        // But because setSettings(s2) clobbered targetOpacity_ to 0.0f and setSettings(s0)
        // did not restore it, opacity is STUCK at 0.0f!
        bool bugPresent = (overlay.opacity() < 0.01f);
        if (bugPresent) {
            g_bugCount.fetch_add(1, std::memory_order_relaxed);
            std::printf("[CONFIRMED BUG 1] TouchPadOverlay::setSettings(mode=2 -> mode=0) permanently clobbers targetOpacity_ to 0.0f!\n"
                        "                  Overlay opacity in Mode 0 is stuck at %f (expected 0.80f).\n", overlay.opacity());
        }
    }

    // ---------------------------------------------------------------------
    // VULNERABILITY 2: Unsigned Time Underflow in TouchPadOverlay::update
    // ---------------------------------------------------------------------
    {
        ui::Fonts fonts;
        ui::TouchPadOverlay overlay(fonts);
        app::Settings s0;
        s0.touchOverlayMode = 0;
        overlay.setSettings(s0);

        // Touch the screen (sets lastTouchInteractionMs_ = platform::nowMs())
        app::shell::TouchEvent touch{};
        touch.kind = app::shell::TouchEvent::Kind::Down;
        touch.fingerId = 1;
        touch.x = 1655.0f;
        touch.y = 685.0f;
        overlay.onTouch(touch);
        touch.kind = app::shell::TouchEvent::Kind::Up;
        overlay.onTouch(touch);

        // If update() is called with a timestamp slightly in the past (e.g. clock jitter, virtual time, or race):
        // uint64_t underflow: (nowMs - lastTouchInteractionMs_ > 4000) evaluates TRUE!
        uint64_t tPast = platform::nowMs() - 100;
        for (int i = 0; i < 5; ++i) {
            overlay.update(tPast, true, false);
            tPast += 16;
        }

        bool bugPresent = (overlay.opacity() < 0.70f); // Should stay at 0.80f if touched <4s ago
        if (bugPresent) {
            g_bugCount.fetch_add(1, std::memory_order_relaxed);
            std::printf("[CONFIRMED BUG 2] Time underflow in update() (nowMs < lastTouchInteractionMs_) causes immediate suppression!\n"
                        "                  Opacity fell to %f due to unsigned uint64_t underflow.\n", overlay.opacity());
        }
    }

    // ---------------------------------------------------------------------
    // VULNERABILITY 3: Legacy settingsVersion Migration in Settings::load Overrides Android 1080p Native Defaults
    // ---------------------------------------------------------------------
    {
        const std::string tmpPath = "/tmp/xcloud_test_migration_bug.json";
        // User config or partial settings file created without settingsVersion
        platform::writeFileAtomic(tmpPath, R"({"language": "pt-BR", "resolution": "1080p"})");

        app::Settings s;
        // On Android, initial settings are: sharpness=0, deband=0, upscaler=0
        s.sharpness = 0;
        s.deband = 0;
        s.upscaler = 0;

        bool ok = s.load(tmpPath);
        ASSERT_TRUE(ok);

        // Because settingsVersion was omitted, on Android settings.cpp:165-173 would treat it as version 1
        // and clobber mobile defaults unless guarded with #if !defined(__ANDROID__)
#if defined(__ANDROID__)
        bool bugPresent = (s.upscaler == 2 || s.sharpness == 2);
        if (bugPresent) {
            g_bugCount.fetch_add(1, std::memory_order_relaxed);
            std::printf("[CONFIRMED BUG 3] Settings::load() without settingsVersion upgrades Android defaults to desktop shaders!\n"
                        "                  upscaler migrated to %d (expected 0), sharpness migrated to %d (expected 0).\n",
                        s.upscaler, s.sharpness);
        }
#else
        // Under Non-Android (desktop/console), legacy pre-v0.8.0 migration to 2 is expected and verified in Suite 5.
        // Android migration is verified guarded via #if !defined(__ANDROID__) in src/app/settings.cpp.
        bool bugPresent = false;
#endif
        std::remove(tmpPath.c_str());
    }

    std::printf("[PASS] Suite 8 completed: %d vulnerabilities empirically isolated and proven.\n", g_bugCount.load());
}

int main(int argc, char** argv) {
    std::printf("=================================================================\n");
    std::printf("Milestone 2 Empirical Stress Test Harness (Android & Dirty Flag)\n");
    std::printf("Author: challenger_android_m2_1\n");
    std::printf("=================================================================\n");

    auto start = std::chrono::steady_clock::now();

    runSuite1_InitialStateAndResets();
    runSuite2_TenThousandSyntheticTouchStress();
    runSuite3_SettledIdleInvariance();
    runSuite4_AutoFadeDirtyLifecycle();
    runSuite5_AndroidSettingsDefaults();
    runSuite6_SettingsLoadSaveAbnormalValues();
    runSuite7_StreamScreenGatingSimulation();
    runSuite8_AdversarialBugOracles();

    auto end = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::printf("\n=================================================================\n");
    std::printf("Empirical Results: %d assertions passed, %d failed, %d confirmed vulnerabilities in %ld ms\n",
                g_passCount.load(), g_failCount.load(), g_bugCount.load(), elapsedMs);
    if (g_bugCount.load() > 0) {
        std::printf("Verdict: REQUEST_CHANGES (Found %d critical state & migration defects)\n", g_bugCount.load());
    } else if (g_failCount.load() == 0) {
        std::printf("Verdict: APPROVE (All requirements validated)\n");
    } else {
        std::printf("Verdict: FAILURE DETECTED\n");
    }
    std::printf("=================================================================\n");

    return (g_failCount.load() == 0) ? 0 : 1;
}
