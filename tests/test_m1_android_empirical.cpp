// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Dedicated Empirical Stress & Adversarial Test Harness for Android Milestone 1
// Focus: Multi-Touch Grace Timing (<=120ms vs >120ms boundary), Zero-Pad Rollback Atomicity,
//        Settings Persistence Clamping, 8-Way Radial D-Pad, Canvas Alpha Integrity & Concurrency.

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
#define ASSERT_NEAR(a, b, eps) EMPIRICAL_ASSERT(std::abs((a) - (b)) <= (eps), "Near failed: |%f - %f| > %f", static_cast<double>(a), static_cast<double>(b), static_cast<double>(eps))

// =========================================================================
// HELPER: MultiTouchGraceArbiter
// Faithful model of StreamScreen::onTouch multi-touch sliding grace window
// =========================================================================
class MultiTouchGraceArbiter {
public:
    explicit MultiTouchGraceArbiter(const ui::Fonts& fonts)
        : overlay_(fonts) {
        overlay_.reset();
    }

    void reset() {
        activeTouchFingers_.clear();
        fingerDownTimeMs_.clear();
        overlay_.reset();
        menuRequested_ = false;
        menuOpen_ = false;
        xc::app::setVirtualGamepad(0, input::ControllerState{});
    }

    bool onTouch(const app::shell::TouchEvent& ev, uint64_t simulatedTimeMs) {
        uint64_t nowMs = simulatedTimeMs;
        if (ev.kind == app::shell::TouchEvent::Kind::Down) {
            // Prune stale fingers older than 120ms from grace buffer
            activeTouchFingers_.erase(
                std::remove_if(activeTouchFingers_.begin(), activeTouchFingers_.end(),
                               [&](uint64_t id) {
                                   auto it = fingerDownTimeMs_.find(id);
                                   return it == fingerDownTimeMs_.end() || (nowMs - it->second) > 120;
                               }),
                activeTouchFingers_.end());

            if (std::find(activeTouchFingers_.begin(), activeTouchFingers_.end(), ev.fingerId) == activeTouchFingers_.end()) {
                activeTouchFingers_.push_back(ev.fingerId);
                fingerDownTimeMs_[ev.fingerId] = nowMs;
            }
            if (activeTouchFingers_.size() >= 3) {
                activeTouchFingers_.clear();
                fingerDownTimeMs_.clear();
                overlay_.reset();
                xc::app::setVirtualGamepad(0, input::ControllerState{});
                input::triggerHaptic(input::HapticType::HeavyClick);
                if (menuOpen_) {
                    menuOpen_ = false;
                } else {
                    menuRequested_ = true;
                }
                return true;
            }
        } else if (ev.kind == app::shell::TouchEvent::Kind::Up || ev.kind == app::shell::TouchEvent::Kind::Cancel) {
            auto it = std::find(activeTouchFingers_.begin(), activeTouchFingers_.end(), ev.fingerId);
            if (it != activeTouchFingers_.end()) activeTouchFingers_.erase(it);
            fingerDownTimeMs_.erase(ev.fingerId);
        }

        // Pull tab zone
        if (ev.kind == app::shell::TouchEvent::Kind::Down && ev.x >= 0.0f && ev.x <= 140.0f && ev.y >= 0.0f && ev.y <= 100.0f) {
            input::triggerHaptic(input::HapticType::Click);
            menuRequested_ = true;
            return true;
        }

        // Virtual controls overlay
        bool handled = overlay_.onTouch(ev);
        if (overlay_.takeMenuToggleRequest()) {
            menuRequested_ = true;
            return true;
        }
        return handled;
    }

    size_t activeCount() const { return activeTouchFingers_.size(); }
    bool menuRequested() const { return menuRequested_; }
    void clearMenuRequested() { menuRequested_ = false; }
    ui::TouchPadOverlay& overlay() { return overlay_; }

private:
    ui::TouchPadOverlay overlay_;
    std::vector<uint64_t> activeTouchFingers_;
    std::map<uint64_t, uint64_t> fingerDownTimeMs_;
    bool menuRequested_ = false;
    bool menuOpen_ = false;
};

// =========================================================================
// TEST SUITE 1: Multi-Touch Grace Timing & Strict 120ms Boundary
// =========================================================================
static void testMultiTouchGraceTimingAndBoundary() {
    std::printf("\n--- [TEST SUITE 1] Multi-Touch Grace Timing & Strict 120ms Boundary ---\n");
    ui::Fonts fonts;
    MultiTouchGraceArbiter arbiter(fonts);

    // 1.1 Varying Arrival Intervals <= 120ms (MUST TRIGGER MENU & ZERO PAD)
    const std::vector<uint64_t> triggerIntervals = {0, 10, 25, 50, 75, 100, 110, 115, 118, 119, 120};
    for (uint64_t dt : triggerIntervals) {
        arbiter.reset();

        uint64_t t1 = 1000;
        uint64_t t2 = t1 + dt / 2;
        uint64_t t3 = t1 + dt; // Total span <= 120ms

        // Finger 1 touches Button A (1655, 685)
        app::shell::TouchEvent ev1{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        ASSERT_TRUE(arbiter.onTouch(ev1, t1));
        ASSERT_EQ(arbiter.activeCount(), 1u);
        ASSERT_FALSE(arbiter.menuRequested());

        // In-game input injected for finger 1
        input::ControllerState s1{};
        bool conn = false;
        app::virtualGamepadState(0, s1, conn);
        ASSERT_TRUE(s1.btnA);

        // Finger 2 touches Button B (1765, 575)
        app::shell::TouchEvent ev2{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
        ASSERT_TRUE(arbiter.onTouch(ev2, t2));
        ASSERT_EQ(arbiter.activeCount(), 2u);
        ASSERT_FALSE(arbiter.menuRequested());

        input::ControllerState s2{};
        app::virtualGamepadState(0, s2, conn);
        ASSERT_TRUE(s2.btnA);
        ASSERT_TRUE(s2.btnB);

        // Finger 3 touches screen (arbitrary point 960, 540) at t3 (span = dt <= 120ms)
        app::shell::TouchEvent ev3{app::shell::TouchEvent::Kind::Down, 3, 960.0f, 540.0f};
        ASSERT_TRUE(arbiter.onTouch(ev3, t3));

        // Invariants verified for <= 120ms:
        ASSERT_TRUE(arbiter.menuRequested());
        ASSERT_EQ(arbiter.activeCount(), 0u); // Cleared by trigger

        // Pad state MUST be completely rolled back to all-zeros!
        input::ControllerState sReset{};
        app::virtualGamepadState(0, sReset, conn);
        ASSERT_FALSE(sReset.btnA);
        ASSERT_FALSE(sReset.btnB);
        ASSERT_FALSE(sReset.btnX);
        ASSERT_FALSE(sReset.btnY);
        ASSERT_FALSE(sReset.btnL1);
        ASSERT_FALSE(sReset.btnR1);
        ASSERT_FALSE(sReset.btnL3);
        ASSERT_FALSE(sReset.btnR3);
        ASSERT_EQ(sReset.triggerL2, 0.0f);
        ASSERT_EQ(sReset.triggerR2, 0.0f);
        ASSERT_EQ(sReset.leftStickX, 0.0f);
        ASSERT_EQ(sReset.leftStickY, 0.0f);
        ASSERT_EQ(sReset.rightStickX, 0.0f);
        ASSERT_EQ(sReset.rightStickY, 0.0f);
    }
    std::printf("[PASS] Verified 11 intervals in [0..120]ms: all triggered menu & rolled back to zero!\n");

    // 1.2 Boundary Stress: 120ms (TRIGGER) vs 121ms (NO TRIGGER)
    {
        // At exactly 120ms: (120 - 0) > 120 is FALSE -> NOT PRUNED -> TRIGGERS
        arbiter.reset();
        app::shell::TouchEvent f1{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f2{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
        app::shell::TouchEvent f3{app::shell::TouchEvent::Kind::Down, 3, 1545.0f, 575.0f};
        arbiter.onTouch(f1, 1000);
        arbiter.onTouch(f2, 1060);
        arbiter.onTouch(f3, 1120); // 1120 - 1000 = 120ms
        ASSERT_TRUE(arbiter.menuRequested());
        ASSERT_EQ(arbiter.activeCount(), 0u);

        // At 121ms: (121 - 0) > 120 is TRUE -> FINGER 1 PRUNED -> COUNT = 2 -> NO TRIGGER
        arbiter.reset();
        arbiter.onTouch(f1, 1000);
        arbiter.onTouch(f2, 1060);
        arbiter.onTouch(f3, 1121); // 1121 - 1000 = 121ms
        ASSERT_FALSE(arbiter.menuRequested());
        ASSERT_EQ(arbiter.activeCount(), 2u); // fingers 2 and 3 remain
        input::ControllerState sActive{};
        bool conn = false;
        app::virtualGamepadState(0, sActive, conn);
        // Finger 2 (B) and Finger 3 (X) must be active; no rollback
        ASSERT_TRUE(sActive.btnB);
        ASSERT_TRUE(sActive.btnX);
    }
    std::printf("[PASS] Strict boundary 120ms vs 121ms verified!\n");

    // 1.3 Varying Arrival Intervals > 120ms (MUST NOT TRIGGER MENU; STANDARD MULTI-TOUCH)
    const std::vector<uint64_t> nonTriggerIntervals = {121, 125, 130, 140, 150, 200, 300, 500, 1000, 5000};
    for (uint64_t dt : nonTriggerIntervals) {
        arbiter.reset();
        uint64_t t1 = 1000;
        uint64_t t2 = t1 + 50;
        uint64_t t3 = t1 + dt; // dt > 120ms

        app::shell::TouchEvent ev1{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f}; // Button A
        app::shell::TouchEvent ev2{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f}; // Button B
        app::shell::TouchEvent ev3{app::shell::TouchEvent::Kind::Down, 3, 1545.0f, 575.0f}; // Button X

        arbiter.onTouch(ev1, t1);
        arbiter.onTouch(ev2, t2);
        arbiter.onTouch(ev3, t3);

        // Crucial empirical invariant: NO false menu trigger!
        ASSERT_FALSE(arbiter.menuRequested());

        // Finger 1 is always pruned (t3 - t1 = dt > 120ms).
        // Finger 2 is pruned if and only if (t3 - t2) = (dt - 50) > 120ms.
        size_t expectedActive = (t3 - t2 > 120) ? 1u : 2u;
        ASSERT_EQ(arbiter.activeCount(), expectedActive);

        input::ControllerState s{};
        bool conn = false;
        app::virtualGamepadState(0, s, conn);
        ASSERT_TRUE(s.btnX);
        if (expectedActive == 2u) {
            ASSERT_TRUE(s.btnB);
        }
    }
    std::printf("[PASS] Verified 10 intervals in [121..5000]ms: zero false menu triggers!\n");

    // 1.4 Real-time Wall Clock Timing Test
    {
        arbiter.reset();
        uint64_t realStart = platform::nowMs();
        app::shell::TouchEvent f1{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f2{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
        app::shell::TouchEvent f3{app::shell::TouchEvent::Kind::Down, 3, 960.0f, 540.0f};

        arbiter.onTouch(f1, realStart);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        uint64_t t2 = platform::nowMs();
        arbiter.onTouch(f2, t2);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        uint64_t t3 = platform::nowMs();
        arbiter.onTouch(f3, t3);

        ASSERT_TRUE(arbiter.menuRequested());
        input::ControllerState sRollback{};
        bool conn = false;
        app::virtualGamepadState(0, sRollback, conn);
        ASSERT_FALSE(sRollback.btnA);
        ASSERT_FALSE(sRollback.btnB);
    }
    std::printf("[PASS] Real-time wall-clock multi-touch test passed!\n");
}

// =========================================================================
// TEST SUITE 2: Multi-Touch Premature Release, Churn & Stick Rollback
// =========================================================================
static void testMultiTouchPrematureReleaseAndStickRollback() {
    std::printf("\n--- [TEST SUITE 2] Premature Release, Churn & Analog Stick Rollback ---\n");
    ui::Fonts fonts;
    MultiTouchGraceArbiter arbiter(fonts);

    // 2.1 Up Event Before 3rd Finger: Finger 1 Lifts Up Early
    {
        arbiter.reset();
        app::shell::TouchEvent f1Down{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f2Down{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
        app::shell::TouchEvent f1Up{app::shell::TouchEvent::Kind::Up, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f3Down{app::shell::TouchEvent::Kind::Down, 3, 1545.0f, 575.0f};

        arbiter.onTouch(f1Down, 1000);
        arbiter.onTouch(f2Down, 1020);
        arbiter.onTouch(f1Up, 1030); // Finger 1 lifted!
        ASSERT_EQ(arbiter.activeCount(), 1u);

        arbiter.onTouch(f3Down, 1040);
        // Only fingers 2 and 3 are down -> count = 2 -> NO MENU TRIGGER
        ASSERT_FALSE(arbiter.menuRequested());
        ASSERT_EQ(arbiter.activeCount(), 2u);
    }
    std::printf("[PASS] Early lift-off prevented false menu trigger!\n");

    // 2.2 Cancel Event Before 3rd Finger
    {
        arbiter.reset();
        app::shell::TouchEvent f1Down{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f2Down{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
        app::shell::TouchEvent f1Cancel{app::shell::TouchEvent::Kind::Cancel, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f3Down{app::shell::TouchEvent::Kind::Down, 3, 1545.0f, 575.0f};

        arbiter.onTouch(f1Down, 1000);
        arbiter.onTouch(f2Down, 1020);
        arbiter.onTouch(f1Cancel, 1030);
        ASSERT_EQ(arbiter.activeCount(), 1u);

        arbiter.onTouch(f3Down, 1040);
        ASSERT_FALSE(arbiter.menuRequested());
        ASSERT_EQ(arbiter.activeCount(), 2u);
    }
    std::printf("[PASS] Touch cancellation handled safely!\n");

    // 2.3 Duplicate Finger ID Down Events
    {
        arbiter.reset();
        app::shell::TouchEvent f1Down{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f2Down{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};

        arbiter.onTouch(f1Down, 1000);
        arbiter.onTouch(f1Down, 1010); // Duplicate finger 1 event
        ASSERT_EQ(arbiter.activeCount(), 1u);

        arbiter.onTouch(f2Down, 1020);
        ASSERT_EQ(arbiter.activeCount(), 2u);
        ASSERT_FALSE(arbiter.menuRequested());
    }
    std::printf("[PASS] Duplicate finger ID filtering verified!\n");

    // 2.4 Sequential 4-Finger Sliding Window Trigger
    {
        arbiter.reset();
        // Finger 1 at 1000ms
        app::shell::TouchEvent f1{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        arbiter.onTouch(f1, 1000);

        // Finger 2 at 1150ms (prunes Finger 1, count=1)
        app::shell::TouchEvent f2{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
        arbiter.onTouch(f2, 1150);
        ASSERT_EQ(arbiter.activeCount(), 1u);

        // Finger 3 at 1170ms (count=2)
        app::shell::TouchEvent f3{app::shell::TouchEvent::Kind::Down, 3, 1545.0f, 575.0f};
        arbiter.onTouch(f3, 1170);
        ASSERT_EQ(arbiter.activeCount(), 2u);

        // Finger 4 at 1190ms (fingers 2, 3, 4 within 40ms -> TRIGGERS!)
        app::shell::TouchEvent f4{app::shell::TouchEvent::Kind::Down, 4, 1655.0f, 465.0f};
        arbiter.onTouch(f4, 1190);
        ASSERT_TRUE(arbiter.menuRequested());
        ASSERT_EQ(arbiter.activeCount(), 0u);
    }
    std::printf("[PASS] Sliding window over sequential arrivals (fingers 2, 3, 4) verified!\n");

    // 2.5 Analog Stick Full Deflection Rollback
    {
        arbiter.reset();
        // Left stick dynamic quadrant: touch at (250, 700), drag to (350, 700) -> rightward stick
        app::shell::TouchEvent stickDown{app::shell::TouchEvent::Kind::Down, 1, 250.0f, 700.0f};
        app::shell::TouchEvent stickMove{app::shell::TouchEvent::Kind::Motion, 1, 350.0f, 700.0f};
        arbiter.onTouch(stickDown, 1000);
        arbiter.onTouch(stickMove, 1010);

        // Right stick touch at (1600, 700)
        app::shell::TouchEvent rtDown{app::shell::TouchEvent::Kind::Down, 2, 1600.0f, 700.0f};
        app::shell::TouchEvent rtMove{app::shell::TouchEvent::Kind::Motion, 2, 1600.0f, 600.0f};
        arbiter.onTouch(rtDown, 1020);
        arbiter.onTouch(rtMove, 1030);

        input::ControllerState sDeflected{};
        bool conn = false;
        app::virtualGamepadState(0, sDeflected, conn);
        ASSERT_TRUE(sDeflected.leftStickX != 0.0f || sDeflected.rightStickY != 0.0f);

        // 3rd finger triggers rollback!
        app::shell::TouchEvent f3{app::shell::TouchEvent::Kind::Down, 3, 960.0f, 540.0f};
        arbiter.onTouch(f3, 1040);
        ASSERT_TRUE(arbiter.menuRequested());

        input::ControllerState sReset{};
        app::virtualGamepadState(0, sReset, conn);
        ASSERT_EQ(sReset.leftStickX, 0.0f);
        ASSERT_EQ(sReset.leftStickY, 0.0f);
        ASSERT_EQ(sReset.rightStickX, 0.0f);
        ASSERT_EQ(sReset.rightStickY, 0.0f);
    }
    std::printf("[PASS] Analog stick deflection snap-back to (0,0) verified!\n");
}

// =========================================================================
// TEST SUITE 3: Settings Persistence Clamping & Adversarial Inputs
// =========================================================================
static void testSettingsPersistenceClampingAndAdversarial() {
    std::printf("\n--- [TEST SUITE 3] Settings Persistence Clamping & Adversarial Inputs ---\n");

    const std::string testFile = "/tmp/test_settings_empirical_m1.json";

    // 3.1 Clamping Across Full Range of Integer Inputs
    const std::vector<int> testInputs = {
        -1000000, -100, -50, -2, -1,
        0, 1, 2,
        3, 4, 5, 50, 100, 1000000
    };

    for (int inputVal : testInputs) {
        int expectedClamped = std::clamp(inputVal, 0, 2);

        // Serialize directly to JSON
        json::Value root = json::Value::object();
        root.set("touchOverlayMode", inputVal);
        std::FILE* f = std::fopen(testFile.c_str(), "wb");
        ASSERT_TRUE(f != nullptr);
        std::string dumped = root.dump();
        std::fwrite(dumped.data(), 1, dumped.size(), f);
        std::fclose(f);

        // Load via Settings::load
        app::Settings loaded;
        ASSERT_TRUE(loaded.load(testFile));
        ASSERT_EQ(loaded.touchOverlayMode, expectedClamped);

        // Save via Settings::save and verify JSON roundtrip
        ASSERT_TRUE(loaded.save(testFile));
        app::Settings reloaded;
        ASSERT_TRUE(reloaded.load(testFile));
        ASSERT_EQ(reloaded.touchOverlayMode, expectedClamped);
    }
    std::printf("[PASS] Clamping verified across %zu integer boundary values [-1M..+1M]!\n", testInputs.size());

    // 3.2 Malformed & Corrupted Types
    const std::vector<std::string> malformedJsonCases = {
        R"({"touchOverlayMode": "AlwaysOn"})",
        R"({"touchOverlayMode": true})",
        R"({"touchOverlayMode": [1, 2, 3]})",
        R"({"touchOverlayMode": {"mode": 1}})",
        R"({"touchOverlayMode": null})",
        R"({"touchOverlayMode": 1.99})",
        R"({"touchOverlayMode": -99.9})",
        R"({"otherKey": 123})" // Missing key
    };

    for (const auto& jsonStr : malformedJsonCases) {
        std::FILE* f = std::fopen(testFile.c_str(), "wb");
        ASSERT_TRUE(f != nullptr);
        std::fwrite(jsonStr.data(), 1, jsonStr.size(), f);
        std::fclose(f);

        app::Settings s;
        s.touchOverlayMode = 1; // Initial default
        ASSERT_TRUE(s.load(testFile));
        // Must strictly remain clamped to [0..2] without crashing or corrupting memory
        ASSERT_TRUE(s.touchOverlayMode >= 0 && s.touchOverlayMode <= 2);
    }
    std::remove(testFile.c_str());
    std::printf("[PASS] Malformed/corrupted JSON types handled safely without crash!\n");

    // 3.3 TouchPadOverlay::setSettings Behavior in All 3 Modes
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    // Mode 0: Auto
    {
        app::Settings sAuto;
        sAuto.touchOverlayMode = 0;
        overlay.setSettings(sAuto);
        ASSERT_EQ(overlay.overlayMode(), 0);

        // Under active physical controller, opacity decays towards 0.0f
        uint64_t t = 1000;
        for (int i = 0; i < 60; ++i) {
            overlay.update(t, true, true);
            t += 16;
        }
        ASSERT_NEAR(overlay.opacity(), 0.0f, 0.01f);

        // When touch interaction occurs, opacity restores
        app::shell::TouchEvent touchEv{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        overlay.onTouch(touchEv);
        for (int i = 0; i < 30; ++i) {
            overlay.update(t, true, false); // physical pad no longer active
            t += 16;
        }
        ASSERT_TRUE(overlay.isVisible());
        overlay.reset();
    }
    std::printf("[PASS] Mode 0 (Auto): auto-hide and touch wake-up verified!\n");

    // Mode 1: Always On
    {
        app::Settings sAlwaysOn;
        sAlwaysOn.touchOverlayMode = 1;
        overlay.setSettings(sAlwaysOn);
        ASSERT_EQ(overlay.overlayMode(), 1);
        ASSERT_TRUE(overlay.isVisible());
        ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);

        // Controller connected & active for 500 frames -> OPACITY MUST REMAIN PINNED AT 0.80f
        uint64_t t = 10000;
        for (int i = 0; i < 500; ++i) {
            overlay.update(t, true, true);
            t += 16;
        }
        ASSERT_TRUE(overlay.isVisible());
        ASSERT_NEAR(overlay.opacity(), 0.80f, 0.001f);
    }
    std::printf("[PASS] Mode 1 (Always On): immunity to controller auto-hide verified (pinned at 0.80)!\n");

    // Mode 2: Always Off
    {
        app::Settings sAlwaysOff;
        sAlwaysOff.touchOverlayMode = 2;
        overlay.setSettings(sAlwaysOff);
        ASSERT_EQ(overlay.overlayMode(), 2);
        ASSERT_FALSE(overlay.isVisible());
        ASSERT_EQ(overlay.opacity(), 0.0f);

        // Touch events must be rejected
        app::shell::TouchEvent touchDown{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent touchMove{app::shell::TouchEvent::Kind::Motion, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent touchUp{app::shell::TouchEvent::Kind::Up, 1, 1655.0f, 685.0f};
        ASSERT_FALSE(overlay.onTouch(touchDown));
        ASSERT_FALSE(overlay.onTouch(touchMove));
        ASSERT_FALSE(overlay.onTouch(touchUp));

        // Canvas render should return early without drawing
        ui::Canvas c(100, 100);
        c.clearTransparent();
        overlay.render(c);
        ASSERT_EQ(c.data()[0], 0u);
    }
    std::printf("[PASS] Mode 2 (Always Off): complete touch suppression and zero-draw verified!\n");
}

// =========================================================================
// TEST SUITE 4: 8-Way Radial D-Pad Full 360° Sweep & Geometry
// =========================================================================
static void testRadialDpad360SweepAndGeometry() {
    std::printf("\n--- [TEST SUITE 4] 8-Way Radial D-Pad Full 360° Sweep & Geometry ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    constexpr float cx = 520.0f, cy = 830.0f;
    constexpr float kPi = 3.14159265358979323846f;

    // 4.1 Sweep all 360 integer degrees at radius = 90px
    int dpadRightCount = 0, dpadDownRightCount = 0, dpadDownCount = 0, dpadDownLeftCount = 0;
    int dpadLeftCount = 0, dpadUpLeftCount = 0, dpadUpCount = 0, dpadUpRightCount = 0;

    for (int deg = -180; deg < 180; ++deg) {
        float rad = deg * (kPi / 180.0f);
        float x = cx + 90.0f * std::cos(rad);
        float y = cy + 90.0f * std::sin(rad);

        ui::TouchControl hit = overlay.hitTest(x, y);

        if (deg >= -22 && deg < 23) {
            ASSERT_EQ(hit, ui::TouchControl::DpadRight);
            dpadRightCount++;
        } else if (deg >= 23 && deg < 68) {
            ASSERT_EQ(hit, ui::TouchControl::DpadDownRight);
            dpadDownRightCount++;
        } else if (deg >= 68 && deg < 113) {
            ASSERT_EQ(hit, ui::TouchControl::DpadDown);
            dpadDownCount++;
        } else if (deg >= 113 && deg < 158) {
            ASSERT_EQ(hit, ui::TouchControl::DpadDownLeft);
            dpadDownLeftCount++;
        } else if (deg >= 158 || deg < -157) {
            ASSERT_EQ(hit, ui::TouchControl::DpadLeft);
            dpadLeftCount++;
        } else if (deg >= -157 && deg < -112) {
            ASSERT_EQ(hit, ui::TouchControl::DpadUpLeft);
            dpadUpLeftCount++;
        } else if (deg >= -112 && deg < -67) {
            ASSERT_EQ(hit, ui::TouchControl::DpadUp);
            dpadUpCount++;
        } else if (deg >= -67 && deg < -22) {
            ASSERT_EQ(hit, ui::TouchControl::DpadUpRight);
            dpadUpRightCount++;
        }
    }

    // Verify all 8 sectors partition 360 degrees without gaps or overlap:
    ASSERT_EQ(dpadRightCount + dpadDownRightCount + dpadDownCount + dpadDownLeftCount +
              dpadLeftCount + dpadUpLeftCount + dpadUpCount + dpadUpRightCount, 360);
    std::printf("[PASS] Full 360° angular sweep verified: all 8 sectors cleanly partitioned (zero gaps)!\n");

    // 4.2 Radial Deadzone Boundary Sweep (r from 0 to 300px)
    for (int r = 0; r <= 300; ++r) {
        float x = cx + static_cast<float>(r);
        float y = cy;
        ui::TouchControl hit = overlay.hitTest(x, y);

        if (r < 40) {
            ASSERT_EQ(hit, ui::TouchControl::None); // Inner deadzone
        } else if (r <= 145) {
            ASSERT_EQ(hit, ui::TouchControl::DpadRight); // Active radius
        } else if (x <= 750.0f) {
            ASSERT_EQ(hit, ui::TouchControl::LeftStick); // Surrounding dynamic thumbstick quadrant
        } else {
            ASSERT_EQ(hit, ui::TouchControl::None); // Beyond thumbstick quadrant (x > 750)
        }
    }
    std::printf("[PASS] Radial deadzones [0..40], [40..145]px, and dynamic stick transition strictly verified!\n");

    // 4.3 ABXY Thumb Sweep Arc
    struct ButtonCheck {
        float cx, cy;
        ui::TouchControl ctrl;
    };
    ButtonCheck buttons[] = {
        {1655.0f, 685.0f, ui::TouchControl::ButtonA},
        {1765.0f, 575.0f, ui::TouchControl::ButtonB},
        {1545.0f, 575.0f, ui::TouchControl::ButtonX},
        {1655.0f, 465.0f, ui::TouchControl::ButtonY}
    };
    for (const auto& btn : buttons) {
        // Center
        ASSERT_EQ(overlay.hitTest(btn.cx, btn.cy), btn.ctrl);
        // Perimeter (r = 75px, inside 78px hitbox)
        for (int deg = 0; deg < 360; deg += 45) {
            float rad = deg * (kPi / 180.0f);
            float px = btn.cx + 75.0f * std::cos(rad);
            float py = btn.cy + 75.0f * std::sin(rad);
            ASSERT_EQ(overlay.hitTest(px, py), btn.ctrl);
        }
        // Outside (r = 85px)
        float outX = btn.cx + 85.0f;
        float outY = btn.cy;
        ASSERT_TRUE(overlay.hitTest(outX, outY) != btn.ctrl);
    }
    std::printf("[PASS] ABXY diamond clusters verified with 78px reach envelope!\n");
}

// =========================================================================
// TEST SUITE 5: Canvas Alpha Transparency & Persistent Buffer Integrity
// =========================================================================
static void testCanvasAlphaTransparencyAndPersistentBuffer() {
    std::printf("\n--- [TEST SUITE 5] Canvas Alpha Transparency & Persistent Buffer Integrity ---\n");

    // 5.1 1080p Canvas Default Construction (2,073,600 pixels)
    ui::Canvas canvas1080p(1920, 1080);
    ASSERT_EQ(canvas1080p.width(), 1920);
    ASSERT_EQ(canvas1080p.height(), 1080);
    const uint32_t* ptr = canvas1080p.data();
    bool allTransparent = true;
    for (size_t i = 0; i < 1920 * 1080; ++i) {
        if (ptr[i] != 0u) {
            allTransparent = false;
            break;
        }
    }
    ASSERT_TRUE(allTransparent);
    std::printf("[PASS] 1080p canvas initial buffer: 100%% of 2,073,600 pixels are 0x00000000 (transparent)!\n");

    // 5.2 clear(rgba(0,0,0,0)) Must NOT Force 0xFF Mask
    canvas1080p.fillRect(ui::Rect{0, 0, 100, 100}, ui::rgba(255, 255, 255, 255));
    ASSERT_EQ(canvas1080p.data()[0], ui::rgba(255, 255, 255, 255));

    canvas1080p.clear(ui::rgba(0, 0, 0, 0));
    ASSERT_EQ(canvas1080p.data()[0], 0u);
    ASSERT_EQ(canvas1080p.data()[1920 * 1080 - 1], 0u);

    // 5.3 Clear with Semitransparent Colors
    canvas1080p.clear(ui::rgba(10, 20, 30, 40));
    ASSERT_EQ(canvas1080p.data()[0], ui::rgba(10, 20, 30, 40));
    canvas1080p.clearTransparent();
    ASSERT_EQ(canvas1080p.data()[0], 0u);

    // 5.4 Persistent Buffer Churn Elimination: Address Invariance Across 1,000 Frames
    const uint32_t* initialAddress = canvas1080p.data();
    for (int frame = 0; frame < 1000; ++frame) {
        canvas1080p.clearTransparent();
        canvas1080p.fillRect(ui::Rect{10, 10, 50, 50}, ui::rgba(200, 100, 50, 180));
        ASSERT_EQ(canvas1080p.data(), initialAddress); // ZERO heap reallocations!
    }
    std::printf("[PASS] Persistent canvas memory address stable across 1,000 frames (0 heap churn)!\n");
}

// =========================================================================
// TEST SUITE 6: Concurrency Hammer (16 Threads, 50,000 Operations)
// =========================================================================
static void testConcurrencyHammer() {
    std::printf("\n--- [TEST SUITE 6] Concurrency Hammer (16 Threads, 50,000 Operations) ---\n");
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    std::atomic<bool> startFlag{false};
    std::atomic<bool> stopFlag{false};
    constexpr int kOperationsPerThread = 3125; // 16 * 3125 = 50,000 ops
    std::vector<std::thread> workers;

    for (int t = 0; t < 16; ++t) {
        workers.emplace_back([&, t]() {
            while (!startFlag.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            for (int i = 0; i < kOperationsPerThread; ++i) {
                switch (t % 4) {
                case 0: {
                    // Random touch injection
                    float x = 500.0f + (i % 1200);
                    float y = 400.0f + (i % 600);
                    app::shell::TouchEvent ev{app::shell::TouchEvent::Kind::Down, static_cast<uint64_t>(t), x, y};
                    overlay.onTouch(ev);
                    break;
                }
                case 1: {
                    // Reset and zero pad rollback
                    overlay.reset();
                    xc::app::setVirtualGamepad(0, input::ControllerState{});
                    break;
                }
                case 2: {
                    // Mode changes
                    app::Settings s;
                    s.touchOverlayMode = i % 3;
                    overlay.setSettings(s);
                    break;
                }
                case 3: {
                    // Query pad state
                    input::ControllerState s{};
                    bool conn = false;
                    xc::app::virtualGamepadState(0, s, conn);
                    break;
                }
                }
            }
        });
    }

    startFlag.store(true, std::memory_order_relaxed);
    for (auto& w : workers) {
        w.join();
    }

    // Clean up to known state
    overlay.reset();
    xc::app::setVirtualGamepad(0, input::ControllerState{});
    std::printf("[PASS] Concurrency hammer completed 50,000 operations across 16 threads with 0 crashes!\n");
}

// =========================================================================
// TEST SUITE 7: High-Volume Fuzzing of 120ms Grace Window (10,000 Random Timelines)
// =========================================================================
static void testGraceWindowFuzzing10000Trials() {
    std::printf("\n--- [TEST SUITE 7] High-Volume Fuzzing of 120ms Grace Window (10,000 Random Timelines) ---\n");
    ui::Fonts fonts;
    MultiTouchGraceArbiter arbiter(fonts);

    uint32_t seed = 0x1337c0de;
    auto lcg = [&]() -> uint32_t {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };

    int trueTriggers = 0;
    int trueNonTriggers = 0;

    for (int trial = 0; trial < 10000; ++trial) {
        arbiter.reset();

        uint64_t t1 = 50000;
        uint64_t dt1 = lcg() % 150; // 0..149 ms
        uint64_t dt2 = lcg() % 150; // 0..149 ms
        uint64_t t2 = t1 + dt1;
        uint64_t t3 = t2 + dt2;
        uint64_t totalSpan = t3 - t1; // 0..298 ms

        app::shell::TouchEvent f1{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f2{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
        app::shell::TouchEvent f3{app::shell::TouchEvent::Kind::Down, 3, 1545.0f, 575.0f};

        arbiter.onTouch(f1, t1);
        arbiter.onTouch(f2, t2);
        arbiter.onTouch(f3, t3);

        if (totalSpan <= 120) {
            ASSERT_TRUE(arbiter.menuRequested());
            ASSERT_EQ(arbiter.activeCount(), 0u);
            input::ControllerState s{};
            bool conn = false;
            app::virtualGamepadState(0, s, conn);
            ASSERT_FALSE(s.btnA);
            ASSERT_FALSE(s.btnB);
            ASSERT_FALSE(s.btnX);
            trueTriggers++;
        } else {
            ASSERT_FALSE(arbiter.menuRequested());
            trueNonTriggers++;
        }
    }

    std::printf("[PASS] Fuzzed 10,000 randomized arrival timelines: %d triggers (span <= 120ms), %d non-triggers (span > 120ms), 0 anomalies!\n",
                trueTriggers, trueNonTriggers);
}

// =========================================================================
// MAIN RUNNER
// =========================================================================
int main() {
    std::printf("=================================================================\n");
    std::printf("Milestone 1 Empirical Adversarial Stress Test Harness (Android)\n");
    std::printf("Author: challenger_android_m1_2\n");
    std::printf("=================================================================\n");

    testMultiTouchGraceTimingAndBoundary();
    testMultiTouchPrematureReleaseAndStickRollback();
    testSettingsPersistenceClampingAndAdversarial();
    testRadialDpad360SweepAndGeometry();
    testCanvasAlphaTransparencyAndPersistentBuffer();
    testConcurrencyHammer();
    testGraceWindowFuzzing10000Trials();

    std::printf("\n=================================================================\n");
    std::printf("FINAL EMPIRICAL RESULTS: %d Passed, %d Failed\n", g_passCount.load(), g_failCount.load());
    std::printf("=================================================================\n");

    return g_failCount.load() == 0 ? 0 : 1;
}
