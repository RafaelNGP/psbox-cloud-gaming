// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Empirical Challenger Deep Stress & Oracle Verification Harness for Milestone 2 Remediation
// Author: challenger_android_m2_remediation (teamwork_challenger)

#include "ui/touch_pad_overlay.h"
#include "app/settings.h"
#include "platform/platform.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace xc;

static std::atomic<int> g_passCount{0};
static std::atomic<int> g_failCount{0};

#define TEST_ASSERT(cond, msg, ...) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "\n[ASSERTION FAILED] %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        g_failCount.fetch_add(1, std::memory_order_relaxed); \
    } else { \
        g_passCount.fetch_add(1, std::memory_order_relaxed); \
    } \
} while (0)

#define ASSERT_TRUE(cond) TEST_ASSERT((cond), "Expected TRUE: %s", #cond)
#define ASSERT_FALSE(cond) TEST_ASSERT(!(cond), "Expected FALSE: %s", #cond)
#define ASSERT_EQ(a, b) TEST_ASSERT((a) == (b), "Expected %s == %s (%ld vs %ld)", #a, #b, static_cast<int64_t>(a), static_cast<int64_t>(b))
#define ASSERT_NEAR(a, b, eps) TEST_ASSERT(std::abs((a) - (b)) <= (eps), "Expected near: |%f - %f| <= %f", static_cast<double>(a), static_cast<double>(b), static_cast<double>(eps))

// -----------------------------------------------------------------------------
// Oracle 1: Mode Transition Matrix (Mode 0, Mode 1, Mode 2)
// -----------------------------------------------------------------------------
static void testModeTransitionMatrix() {
    std::printf("\n--- [ORACLE 1] Comprehensive Mode Transition Matrix & Target Opacity ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    const int modes[] = {0, 1, 2};
    for (int fromMode : modes) {
        for (int toMode : modes) {
            // Set initial mode
            app::Settings sFrom;
            sFrom.touchOverlayMode = fromMode;
            overlay.setSettings(sFrom);

            // Settle initial mode
            uint64_t t = 10000;
            for (int f = 0; f < 30; ++f) {
                overlay.update(t, false, false);
                t += 16;
            }

            // Transition to target mode
            app::Settings sTo;
            sTo.touchOverlayMode = toMode;
            overlay.setSettings(sTo);

            if (toMode == 1) {
                // Mode 1: Always on -> Immediate opacity 0.80f
                ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);
                ASSERT_TRUE(overlay.isVisible());
            } else if (toMode == 2) {
                // Mode 2: Always off -> Immediate opacity 0.0f
                ASSERT_NEAR(overlay.opacity(), 0.0f, 0.001f);
                ASSERT_FALSE(overlay.isVisible());
            } else if (toMode == 0) {
                // Mode 0: Auto-hide -> Smooth fade-in towards 0.80f when no controller is connected
                for (int f = 0; f < 30; ++f) {
                    overlay.update(t, false, false);
                    t += 16;
                }
                ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);
                ASSERT_TRUE(overlay.isVisible());
            }
        }
    }

    // Stress test 1,000 rapid transitions between Mode 2 and Mode 0
    for (int i = 0; i < 1000; ++i) {
        app::Settings s2;
        s2.touchOverlayMode = 2;
        overlay.setSettings(s2);
        ASSERT_NEAR(overlay.opacity(), 0.0f, 0.001f);

        app::Settings s0;
        s0.touchOverlayMode = 0;
        overlay.setSettings(s0);

        uint64_t t = 50000 + i * 1000;
        for (int f = 0; f < 15; ++f) {
            overlay.update(t, false, false);
            t += 16;
        }
        ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);
    }

    std::printf("[PASS] Oracle 1: All permutations of Mode 0/1/2 and 1,000 rapid mode flips verified.\n");
}

// -----------------------------------------------------------------------------
// Oracle 2: Clock Jitter, Timestamp Underflow & Monotonicity Oracles
// -----------------------------------------------------------------------------
static void testTimestampUnderflowOracles() {
    std::printf("\n--- [ORACLE 2] Timestamp Underflow, Jitter & Monotonicity Oracles ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    app::Settings s0;
    s0.touchOverlayMode = 0;
    overlay.setSettings(s0);

    // Initial steady state at 0.80f
    uint64_t baseTime = platform::nowMs();
    for (int i = 0; i < 20; ++i) {
        overlay.update(baseTime, false, false);
        baseTime += 16;
    }
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);

    // Simulate active touch at baseTime
    app::shell::TouchEvent touch{};
    touch.kind = app::shell::TouchEvent::Kind::Down;
    touch.fingerId = 1;
    touch.x = 250.0f;
    touch.y = 545.0f;
    overlay.onTouch(touch);
    touch.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(touch);

    baseTime = platform::nowMs();
    // Test various retrograde timestamps (nowMs < lastTouchInteractionMs_)
    const uint64_t pastTimestamps[] = {
        baseTime - 1,
        baseTime - 16,
        baseTime - 100,
        baseTime - 4000,
        baseTime - 4001,
        baseTime - 50000,
        0ULL,
        1ULL,
        100ULL
    };

    for (uint64_t tPast : pastTimestamps) {
        // Physical gamepad connected, but user touched screen recently
        // Even with retrograde timestamp, it MUST NOT underflow to > 4000 and suppress
        overlay.update(tPast, true, false);
        ASSERT_NEAR(overlay.opacity(), 0.80f, 0.05f);
    }

    // Now test forward progression boundaries:
    // 1. Within 4000ms: target should remain 0.80f
    overlay.update(baseTime + 3999, true, false);
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.05f);

    // 2. Exactly at 4000ms: not > 4000, target remains 0.80f
    overlay.update(baseTime + 4000, true, false);
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.05f);

    // 3. At 4001ms: nowMs - lastTouchInteractionMs_ > 4000 -> target becomes 0.0f
    // Run 20 frames to fade out
    uint64_t tFade = baseTime + 4001;
    for (int i = 0; i < 20; ++i) {
        overlay.update(tFade, true, false);
        tFade += 16;
    }
    ASSERT_NEAR(overlay.opacity(), 0.0f, 0.001f);

    std::printf("[PASS] Oracle 2: Timestamp underflow guards and boundary timeouts verified.\n");
}

// -----------------------------------------------------------------------------
// Oracle 3: Settings::load Architecture Parity & Isolation
// -----------------------------------------------------------------------------
static void testSettingsLoadArchitecture() {
    std::printf("\n--- [ORACLE 3] Settings::load Architecture Parity & Isolation ---\n");
    const std::string tmpPath = "/tmp/xcloud_test_arch_check.json";

    // Write a JSON payload omitting settingsVersion (legacy or partial WebUI payload)
    platform::writeFileAtomic(tmpPath, R"({
        "language": "en",
        "resolution": "1080p",
        "gameVolume": 100
    })");

    app::Settings s;
    s.sharpness = 0;
    s.deband = 0;
    s.upscaler = 0;

    bool ok = s.load(tmpPath);
    ASSERT_TRUE(ok);

#if defined(__ANDROID__)
    // On Android target: Mobile lightweight defaults MUST NOT be mutated!
    ASSERT_EQ(s.sharpness, 0);
    ASSERT_EQ(s.deband, 0);
    ASSERT_EQ(s.upscaler, 0);
    std::printf("[PASS] Oracle 3 (ANDROID MODE): Settings::load preserved sharpness=0, deband=0, upscaler=0.\n");
#else
    // On Desktop/Console target: Legacy migration to version 3 upgrades sharpness to 2, upscaler to 2
    ASSERT_EQ(s.sharpness, 2);
    ASSERT_EQ(s.upscaler, 2);
    std::printf("[PASS] Oracle 3 (DESKTOP MODE): Settings::load properly executed legacy desktop migration (sharpness=2, upscaler=2).\n");
#endif

    std::remove(tmpPath.c_str());
}

// -----------------------------------------------------------------------------
// Oracle 4: Dirty Flag Settling Invariance across Mode 0 / Mode 1 / Mode 2
// -----------------------------------------------------------------------------
static void testDirtyFlagSettlingInvariance() {
    std::printf("\n--- [ORACLE 4] Dirty Flag Settling Invariance ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    app::Settings s;
    s.touchOverlayMode = 0;
    overlay.setSettings(s);

    uint64_t t = 20000;
    // Let Mode 0 settle at 0.80f (no gamepad)
    for (int i = 0; i < 20; ++i) {
        overlay.update(t, false, false);
        t += 16;
    }
    overlay.clearDirty();

    // In steady state, 500 frames must produce 0 dirty flags
    int dirtyCount = 0;
    for (int i = 0; i < 500; ++i) {
        overlay.update(t, false, false);
        if (overlay.isDirty()) {
            dirtyCount++;
            overlay.clearDirty();
        }
        t += 16;
    }
    ASSERT_EQ(dirtyCount, 0);

    // Now connect gamepad and trigger active input (suppress Mode 0)
    overlay.update(t, true, true);
    t += 16;

    // Fading out should produce dirty flags while moving, then settle to 0
    int fadeOutDirtyFrames = 0;
    for (int i = 0; i < 20; ++i) {
        overlay.update(t, true, false);
        if (overlay.isDirty()) {
            fadeOutDirtyFrames++;
            overlay.clearDirty();
        }
        t += 16;
    }
    ASSERT_NEAR(overlay.opacity(), 0.0f, 0.001f);
    // Settled at 0.0f: 500 idle frames must produce 0 dirty flags
    int idleZeroDirty = 0;
    for (int i = 0; i < 500; ++i) {
        overlay.update(t, true, false);
        if (overlay.isDirty()) {
            idleZeroDirty++;
            overlay.clearDirty();
        }
        t += 16;
    }
    ASSERT_EQ(idleZeroDirty, 0);

    std::printf("[PASS] Oracle 4: Zero dirty flag leakage in all settled states.\n");
}

int main(int argc, char** argv) {
    std::printf("=================================================================\n");
    std::printf("Milestone 2 Remediation Deep Challenge Suite\n");
    std::printf("=================================================================\n");

    testModeTransitionMatrix();
    testTimestampUnderflowOracles();
    testSettingsLoadArchitecture();
    testDirtyFlagSettlingInvariance();

    std::printf("\n=================================================================\n");
    std::printf("Results: %d assertions passed, %d failed.\n", g_passCount.load(), g_failCount.load());
    std::printf("Verdict: %s\n", (g_failCount.load() == 0) ? "PASS" : "FAIL");
    std::printf("=================================================================\n");

    return (g_failCount.load() == 0) ? 0 : 1;
}
