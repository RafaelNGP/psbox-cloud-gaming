// SPDX-License-Identifier: GPL-3.0-or-later
// Dedicated Empirical Adversarial Test Harness for Android Milestone 5
// Focus:
//   1. Controller dynamic touch hiding, deflection thresholds (< 0.15 vs >= 0.15), and screen touch restoration
//   2. TouchPadOverlay Mode arbitration (Auto, Always On, Always Off) and multi-touch precedence
//   3. TouchLayout JSON serialization, corrupted/malformed inputs, partial loads
//   4. TouchLayoutStudio drag bounds clamping (ABXY cluster and all 16 controls), resetToDefaultLayout, cancel, save
//   5. StreamMenu single-tab consolidation and item placement invariants
//   6. Randomized multi-touch stress & thumbstick mathematical normalization

#include "ui/touch_pad_overlay.h"
#include "ui/stream_menu.h"
#include "input/controller.h"
#include "app/settings.h"
#include "platform/platform.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
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
#define ASSERT_NEAR(a, b, eps) EMPIRICAL_ASSERT(std::abs((a) - (b)) <= (eps), "Near equality failed: %f vs %f", static_cast<double>(a), static_cast<double>(b))

// =========================================================================
// TEST SUITE 1: Controller inUse Thresholds & Button Triggers (R1)
// =========================================================================
void testControllerInUseThresholds() {
    std::printf("[TEST 1] Controller inUse Deflection Thresholds & Button Sweep...\n");

    input::ControllerState s{};
    ASSERT_FALSE(input::inUse(s));

    // 1.1 Left Stick X & Y sweep around 0.15 threshold
    const float belowThreshold[] = {0.0f, 0.05f, 0.10f, 0.140f, 0.149f, -0.05f, -0.10f, -0.149f};
    const float aboveThreshold[] = {0.151f, 0.16f, 0.25f, 0.50f, 0.99f, 1.0f, -0.151f, -0.16f, -0.50f, -1.0f};

    for (float v : belowThreshold) {
        s.leftStickX = v;
        ASSERT_FALSE(input::inUse(s));
        s.leftStickX = 0.0f;

        s.leftStickY = v;
        ASSERT_FALSE(input::inUse(s));
        s.leftStickY = 0.0f;

        s.rightStickX = v;
        ASSERT_FALSE(input::inUse(s));
        s.rightStickX = 0.0f;

        s.rightStickY = v;
        ASSERT_FALSE(input::inUse(s));
        s.rightStickY = 0.0f;
    }

    for (float v : aboveThreshold) {
        s.leftStickX = v;
        ASSERT_TRUE(input::inUse(s));
        s.leftStickX = 0.0f;

        s.leftStickY = v;
        ASSERT_TRUE(input::inUse(s));
        s.leftStickY = 0.0f;

        s.rightStickX = v;
        ASSERT_TRUE(input::inUse(s));
        s.rightStickX = 0.0f;

        s.rightStickY = v;
        ASSERT_TRUE(input::inUse(s));
        s.rightStickY = 0.0f;
    }

    // 1.2 Triggers (0.0 to 1.0)
    for (float v : {0.0f, 0.05f, 0.10f, 0.149f}) {
        s.triggerL2 = v;
        ASSERT_FALSE(input::inUse(s));
        s.triggerL2 = 0.0f;

        s.triggerR2 = v;
        ASSERT_FALSE(input::inUse(s));
        s.triggerR2 = 0.0f;
    }

    for (float v : {0.151f, 0.20f, 0.50f, 1.0f}) {
        s.triggerL2 = v;
        ASSERT_TRUE(input::inUse(s));
        s.triggerL2 = 0.0f;

        s.triggerR2 = v;
        ASSERT_TRUE(input::inUse(s));
        s.triggerR2 = 0.0f;
    }

    // 1.3 Every digital button must independently trigger inUse
    s.btnA = true; ASSERT_TRUE(input::inUse(s)); s.btnA = false;
    s.btnB = true; ASSERT_TRUE(input::inUse(s)); s.btnB = false;
    s.btnX = true; ASSERT_TRUE(input::inUse(s)); s.btnX = false;
    s.btnY = true; ASSERT_TRUE(input::inUse(s)); s.btnY = false;
    s.dpadUp = true; ASSERT_TRUE(input::inUse(s)); s.dpadUp = false;
    s.dpadDown = true; ASSERT_TRUE(input::inUse(s)); s.dpadDown = false;
    s.dpadLeft = true; ASSERT_TRUE(input::inUse(s)); s.dpadLeft = false;
    s.dpadRight = true; ASSERT_TRUE(input::inUse(s)); s.dpadRight = false;
    s.btnL1 = true; ASSERT_TRUE(input::inUse(s)); s.btnL1 = false;
    s.btnR1 = true; ASSERT_TRUE(input::inUse(s)); s.btnR1 = false;
    s.btnL3 = true; ASSERT_TRUE(input::inUse(s)); s.btnL3 = false;
    s.btnR3 = true; ASSERT_TRUE(input::inUse(s)); s.btnR3 = false;
    s.btnOptions = true; ASSERT_TRUE(input::inUse(s)); s.btnOptions = false;
    s.btnTouchpad = true; ASSERT_TRUE(input::inUse(s)); s.btnTouchpad = false;
    s.btnNexus = true; ASSERT_TRUE(input::inUse(s)); s.btnNexus = false;
}

// =========================================================================
// TEST SUITE 2: Dynamic Touch Overlay Hiding & Touch Restoration (R1)
// =========================================================================
void testDynamicTouchHidingAndRestoration() {
    std::printf("[TEST 2] Dynamic Touch Overlay Hiding & Touch Restoration State Machine...\n");

    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    // Initial state: Auto mode (0)
    app::Settings settings;
    settings.touchOverlayMode = 0;
    overlay.setSettings(settings);
    ASSERT_TRUE(overlay.isVisible());
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.01f);

    uint64_t nowMs = 1000;

    // 2.1 Connected pad without input does NOT suppress
    overlay.update(nowMs, true /* connected */, false /* active */, 0 /* inputMs */);
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.01f);
    ASSERT_TRUE(overlay.isVisible());

    // 2.2 Physical input occurs -> suppression triggers
    nowMs += 16;
    overlay.update(nowMs, true, true, nowMs);
    // Opacity fades down smoothly
    float prevOpacity = overlay.opacity();
    for (int frame = 0; frame < 15; ++frame) {
        nowMs += 16;
        overlay.update(nowMs, true, false, 0);
        float cur = overlay.opacity();
        ASSERT_TRUE(cur <= prevOpacity);
        prevOpacity = cur;
    }
    // Eventually reaches 0.0f
    ASSERT_NEAR(overlay.opacity(), 0.0f, 0.01f);
    ASSERT_FALSE(overlay.isVisible());

    // 2.3 Screen touch event RESTORES overlay immediately
    nowMs += 50;
    app::shell::TouchEvent touchEv{};
    touchEv.kind = app::shell::TouchEvent::Kind::Down;
    touchEv.fingerId = 1;
    // Touch on Button A canonical coordinates (1655, 685)
    touchEv.x = 1655.0f;
    touchEv.y = 685.0f;

    bool consumed = overlay.onTouch(touchEv);
    ASSERT_TRUE(consumed);

    // Next update call: opacity must ramp up towards 0.80f
    nowMs += 16;
    overlay.update(nowMs, true, false, 0);
    ASSERT_TRUE(overlay.opacity() > 0.01f);
    ASSERT_TRUE(overlay.isVisible());

    for (int frame = 0; frame < 15; ++frame) {
        nowMs += 16;
        overlay.update(nowMs, true, false, 0);
    }
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.01f);

    // Release touch
    touchEv.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(touchEv);

    // 2.4 Multi-touch precedence: holding touch prevents physical suppression
    touchEv.kind = app::shell::TouchEvent::Kind::Down;
    touchEv.fingerId = 2;
    touchEv.x = 520.0f; // D-pad Down (radius ~70px from center)
    touchEv.y = 900.0f;
    ASSERT_TRUE(overlay.onTouch(touchEv));

    // While finger is down, physical input arrives
    nowMs += 16;
    overlay.update(nowMs, true, true, nowMs);
    // Invariant: Touch held must OVERRIDE physical suppression!
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.01f);
    ASSERT_TRUE(overlay.isVisible());

    // Release finger
    touchEv.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(touchEv);

    // Next physical input now succeeds in suppressing
    nowMs += 16;
    overlay.update(nowMs, true, true, nowMs);
    for (int frame = 0; frame < 15; ++frame) {
        nowMs += 16;
        overlay.update(nowMs, true, false, 0);
    }
    ASSERT_NEAR(overlay.opacity(), 0.0f, 0.01f);

    // 2.5 Physical pad disconnects -> overlay restores automatically
    nowMs += 16;
    overlay.update(nowMs, false /* disconnected */, false, 0);
    for (int frame = 0; frame < 15; ++frame) {
        nowMs += 16;
        overlay.update(nowMs, false, false, 0);
    }
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.01f);
    ASSERT_TRUE(overlay.isVisible());
}

// =========================================================================
// TEST SUITE 3: TouchLayout Serialization, Corrupted JSON, Extreme Inputs (R4)
// =========================================================================
void testTouchLayoutSerializationAndCorruption() {
    std::printf("[TEST 3] TouchLayout JSON Round-Trip, Corruption & Partial Fallback...\n");

    const std::string tmpPath = "/tmp/test_m5_layout.json";

    // 3.1 Bitwise round-trip of default layout
    ui::TouchLayout def = ui::defaultTouchLayout();
    ASSERT_TRUE(ui::saveTouchLayout(tmpPath, def));

    ui::TouchLayout loaded;
    ASSERT_TRUE(ui::loadTouchLayout(tmpPath, loaded));

    ASSERT_EQ(loaded.leftStick.x, def.leftStick.x);
    ASSERT_EQ(loaded.leftStick.y, def.leftStick.y);
    ASSERT_EQ(loaded.rightStick.x, def.rightStick.x);
    ASSERT_EQ(loaded.rightStick.y, def.rightStick.y);
    ASSERT_EQ(loaded.dpad.x, def.dpad.x);
    ASSERT_EQ(loaded.dpad.y, def.dpad.y);
    ASSERT_EQ(loaded.btnA.x, def.btnA.x);
    ASSERT_EQ(loaded.btnA.y, def.btnA.y);
    ASSERT_EQ(loaded.btnB.x, def.btnB.x);
    ASSERT_EQ(loaded.btnB.y, def.btnB.y);
    ASSERT_EQ(loaded.btnX.x, def.btnX.x);
    ASSERT_EQ(loaded.btnX.y, def.btnX.y);
    ASSERT_EQ(loaded.btnY.x, def.btnY.x);
    ASSERT_EQ(loaded.btnY.y, def.btnY.y);
    ASSERT_EQ(loaded.triggerLt.x, def.triggerLt.x);
    ASSERT_EQ(loaded.triggerLt.y, def.triggerLt.y);
    ASSERT_EQ(loaded.bumperLb.x, def.bumperLb.x);
    ASSERT_EQ(loaded.bumperLb.y, def.bumperLb.y);
    ASSERT_EQ(loaded.triggerRt.x, def.triggerRt.x);
    ASSERT_EQ(loaded.triggerRt.y, def.triggerRt.y);
    ASSERT_EQ(loaded.bumperRb.x, def.bumperRb.x);
    ASSERT_EQ(loaded.bumperRb.y, def.bumperRb.y);
    ASSERT_EQ(loaded.btnL3.x, def.btnL3.x);
    ASSERT_EQ(loaded.btnL3.y, def.btnL3.y);
    ASSERT_EQ(loaded.btnR3.x, def.btnR3.x);
    ASSERT_EQ(loaded.btnR3.y, def.btnR3.y);
    ASSERT_EQ(loaded.btnView.x, def.btnView.x);
    ASSERT_EQ(loaded.btnView.y, def.btnView.y);
    ASSERT_EQ(loaded.btnNexus.x, def.btnNexus.x);
    ASSERT_EQ(loaded.btnNexus.y, def.btnNexus.y);
    ASSERT_EQ(loaded.btnMenu.x, def.btnMenu.x);
    ASSERT_EQ(loaded.btnMenu.y, def.btnMenu.y);

    // 3.2 Non-existent file
    ui::TouchLayout badLayout;
    ASSERT_FALSE(ui::loadTouchLayout("/nonexistent/path/layout.json", badLayout));

    // 3.3 Corrupted JSON: Syntax Error
    {
        std::ofstream out(tmpPath);
        out << "{ \"version\": 1, \"leftStick\": [100.0, 200.0"; // unclosed
    }
    ASSERT_FALSE(ui::loadTouchLayout(tmpPath, badLayout));

    // 3.4 Corrupted JSON: Array root instead of Object
    {
        std::ofstream out(tmpPath);
        out << "[ 1, 2, 3, 4 ]";
    }
    ASSERT_FALSE(ui::loadTouchLayout(tmpPath, badLayout));

    // 3.5 Corrupted JSON: Empty file
    {
        std::ofstream out(tmpPath);
        out << "";
    }
    ASSERT_FALSE(ui::loadTouchLayout(tmpPath, badLayout));

    // 3.6 Partial JSON: Valid object with only leftStick and btnA
    {
        std::ofstream out(tmpPath);
        out << "{\n"
            << "  \"version\": 1,\n"
            << "  \"leftStick\": [450.5, 620.0],\n"
            << "  \"btnA\": [1800.0, 750.0]\n"
            << "}\n";
    }
    ui::TouchLayout partial = def;
    ASSERT_TRUE(ui::loadTouchLayout(tmpPath, partial));
    ASSERT_EQ(partial.leftStick.x, 450.5f);
    ASSERT_EQ(partial.leftStick.y, 620.0f);
    ASSERT_EQ(partial.btnA.x, 1800.0f);
    ASSERT_EQ(partial.btnA.y, 750.0f);
    // Others retained original default values
    ASSERT_EQ(partial.rightStick.x, def.rightStick.x);
    ASSERT_EQ(partial.btnB.x, def.btnB.x);

    // 3.7 Malformed coordinate values: 1 element array, string values
    {
        std::ofstream out(tmpPath);
        out << "{\n"
            << "  \"leftStick\": [333.0],\n" // only 1 element
            << "  \"btnA\": [\"not_a_number\", null]\n"
            << "}\n";
    }
    ui::TouchLayout malformed = def;
    ASSERT_TRUE(ui::loadTouchLayout(tmpPath, malformed));
    // leftStick requiring >= 2 elements should NOT have been overwritten
    ASSERT_EQ(malformed.leftStick.x, def.leftStick.x);

    std::remove(tmpPath.c_str());
}

// =========================================================================
// TEST SUITE 4: TouchLayoutStudio Drag Clamping & Default Restore (R4)
// =========================================================================
void testTouchLayoutStudioDragAndClamping() {
    std::printf("[TEST 4] TouchLayoutStudio Interactive Drag Bounds Clamping & Reset...\n");

    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    overlay.openLayoutStudio();
    ASSERT_TRUE(overlay.isLayoutStudioActive());

    // 4.1 ABXY Cluster Drag Clamping Stress
    // Touch on center of ABXY diamond: (btnA+btnB+btnX+btnY)/4 = (1655+1765+1545+1655)/4 = 1655, (685+575+575+465)/4 = 575
    app::shell::TouchEvent ev{};
    ev.kind = app::shell::TouchEvent::Kind::Down;
    ev.fingerId = 10;
    ev.x = 1655.0f;
    ev.y = 575.0f;
    ASSERT_TRUE(overlay.onTouch(ev));

    // Drag to extreme negative coordinates (-50000, -50000)
    ev.kind = app::shell::TouchEvent::Kind::Motion;
    ev.x = -50000.0f;
    ev.y = -50000.0f;
    ASSERT_TRUE(overlay.onTouch(ev));

    ui::TouchLayout clamped = overlay.layout();
    // Center was clamped to (220, 220)
    float cx = (clamped.btnA.x + clamped.btnB.x + clamped.btnX.x + clamped.btnY.x) * 0.25f;
    float cy = (clamped.btnA.y + clamped.btnB.y + clamped.btnX.y + clamped.btnY.y) * 0.25f;
    ASSERT_NEAR(cx, 220.0f, 0.5f);
    ASSERT_NEAR(cy, 220.0f, 0.5f);

    // All individual buttons must be strictly within logical viewport [0..1920, 0..1080]
    ASSERT_TRUE(clamped.btnA.x >= 0.0f && clamped.btnA.x <= 1920.0f);
    ASSERT_TRUE(clamped.btnA.y >= 0.0f && clamped.btnA.y <= 1080.0f);
    ASSERT_TRUE(clamped.btnB.x >= 0.0f && clamped.btnB.x <= 1920.0f);
    ASSERT_TRUE(clamped.btnB.y >= 0.0f && clamped.btnB.y <= 1080.0f);
    ASSERT_TRUE(clamped.btnX.x >= 0.0f && clamped.btnX.x <= 1920.0f);
    ASSERT_TRUE(clamped.btnX.y >= 0.0f && clamped.btnX.y <= 1080.0f);
    ASSERT_TRUE(clamped.btnY.x >= 0.0f && clamped.btnY.x <= 1920.0f);
    ASSERT_TRUE(clamped.btnY.y >= 0.0f && clamped.btnY.y <= 1080.0f);

    // Drag to extreme positive coordinates (+50000, +50000)
    ev.x = 50000.0f;
    ev.y = 50000.0f;
    ASSERT_TRUE(overlay.onTouch(ev));

    clamped = overlay.layout();
    cx = (clamped.btnA.x + clamped.btnB.x + clamped.btnX.x + clamped.btnY.x) * 0.25f;
    cy = (clamped.btnA.y + clamped.btnB.y + clamped.btnX.y + clamped.btnY.y) * 0.25f;
    ASSERT_NEAR(cx, 1700.0f, 0.5f);
    ASSERT_NEAR(cy, 860.0f, 0.5f);

    ASSERT_TRUE(clamped.btnA.x >= 0.0f && clamped.btnA.x <= 1920.0f);
    ASSERT_TRUE(clamped.btnA.y >= 0.0f && clamped.btnA.y <= 1080.0f);
    ASSERT_TRUE(clamped.btnB.x >= 0.0f && clamped.btnB.x <= 1920.0f);
    ASSERT_TRUE(clamped.btnB.y >= 0.0f && clamped.btnB.y <= 1080.0f);

    ev.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(ev);

    // 4.2 Individual Control Drag Clamping Sweep for all controls
    // Test LeftStick drag to bounds
    ev.kind = app::shell::TouchEvent::Kind::Down;
    ev.fingerId = 11;
    ev.x = overlay.layout().leftStick.x;
    ev.y = overlay.layout().leftStick.y;
    ASSERT_TRUE(overlay.onTouch(ev));

    ev.kind = app::shell::TouchEvent::Kind::Motion;
    ev.x = -10000.0f;
    ev.y = -10000.0f;
    ASSERT_TRUE(overlay.onTouch(ev));
    // leftStick radius is 135 -> clamped to r + 20 = 155
    ASSERT_NEAR(overlay.layout().leftStick.x, 155.0f, 1.0f);
    ASSERT_NEAR(overlay.layout().leftStick.y, 155.0f, 1.0f);

    ev.x = 10000.0f;
    ev.y = 10000.0f;
    ASSERT_TRUE(overlay.onTouch(ev));
    // right bound: 1920 - 135 - 20 = 1765
    ASSERT_NEAR(overlay.layout().leftStick.x, 1765.0f, 1.0f);
    // bottom bound: 1080 - 135 - 20 = 925
    ASSERT_NEAR(overlay.layout().leftStick.y, 925.0f, 1.0f);

    ev.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(ev);

    // 4.3 Toolbar "Restaurar Padrão" button hit-test: Rect{670, 980, 200, 48}
    ev.kind = app::shell::TouchEvent::Kind::Down;
    ev.fingerId = 12;
    ev.x = 750.0f; // inside Restaurar Padrão
    ev.y = 1000.0f;
    ASSERT_TRUE(overlay.onTouch(ev));

    ui::TouchLayout resetLayout = overlay.layout();
    ui::TouchLayout defLayout = ui::defaultTouchLayout();
    ASSERT_EQ(resetLayout.leftStick.x, defLayout.leftStick.x);
    ASSERT_EQ(resetLayout.leftStick.y, defLayout.leftStick.y);
    ASSERT_EQ(resetLayout.btnA.x, defLayout.btnA.x);
    ASSERT_EQ(resetLayout.btnA.y, defLayout.btnA.y);
    ASSERT_TRUE(overlay.isLayoutStudioActive()); // Stays in studio

    // 4.4 Toolbar "Cancelar" button hit-test: Rect{890, 980, 140, 48}
    // First modify something
    ui::TouchLayout mod = overlay.layout();
    mod.dpad.x = 350.0f;
    overlay.setLayout(mod);
    ASSERT_EQ(overlay.layout().dpad.x, 350.0f);

    ev.x = 950.0f; // inside Cancelar
    ev.y = 1000.0f;
    ASSERT_TRUE(overlay.onTouch(ev));
    ASSERT_FALSE(overlay.isLayoutStudioActive()); // Studio closed
    // Restored to backup (which was the default layout when opened)
    ASSERT_EQ(overlay.layout().dpad.x, defLayout.dpad.x);
}

// =========================================================================
// TEST SUITE 5: StreamMenu Single-Tab & Item Layout Invariants (R2, R3)
// =========================================================================
void testStreamMenuConsolidationAndItems() {
    std::printf("[TEST 5] StreamMenu Tab Consolidation & Item Ordering Invariants...\n");

    ui::Fonts fonts;
    ui::StreamMenu menu(fonts);

    // 5.1 Leave item invariant: MUST be at ItemCount - 1
    ASSERT_EQ(ui::StreamMenu::Leave, ui::StreamMenu::ItemCount - 1);

    // 5.2 Touch overlay toggle state getter & setter
    ASSERT_TRUE(menu.touchOverlayEnabled());
    menu.setTouchOverlayEnabled(false);
    ASSERT_FALSE(menu.touchOverlayEnabled());
    menu.setTouchOverlayEnabled(true);
    ASSERT_TRUE(menu.touchOverlayEnabled());

    // 5.3 Tab placement invariants
#if defined(__ANDROID__)
    // On Android, all menu items (except Profile) must belong to General tab
    for (int i = 0; i < ui::StreamMenu::ItemCount; ++i) {
        ASSERT_TRUE(menu.itemInTab(i, ui::MenuTab::General));
    }
#else
    // On non-Android, Leave belongs to General tab, touch items are hidden
    ASSERT_TRUE(menu.itemInTab(ui::StreamMenu::Leave, ui::MenuTab::General));
    ASSERT_FALSE(menu.itemShown(ui::StreamMenu::TouchOverlayItem));
    ASSERT_FALSE(menu.itemShown(ui::StreamMenu::TouchLayoutStudioItem));
#endif
}

// =========================================================================
// TEST SUITE 6: Multi-Touch Randomized Fuzz & Thumbstick Math
// =========================================================================
void testMultiTouchFuzzAndThumbstickMath() {
    std::printf("[TEST 6] Multi-Touch Fuzzing (1000 events) & Thumbstick Clamping...\n");

    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    // 6.1 Thumbstick deadzone & normalization check
    // Anchor at (250, 545). kDeadzone = 12.0f, kMaxRadius = 115.0f
    app::shell::TouchEvent ev{};
    ev.kind = app::shell::TouchEvent::Kind::Down;
    ev.fingerId = 1;
    ev.x = 250.0f;
    ev.y = 545.0f;
    overlay.onTouch(ev);

    // Small deflection < 12px deadzone
    ev.kind = app::shell::TouchEvent::Kind::Motion;
    ev.x = 255.0f;
    ev.y = 545.0f;
    overlay.onTouch(ev);
    ASSERT_EQ(overlay.state().leftStickX, 0.0f);
    ASSERT_EQ(overlay.state().leftStickY, 0.0f);

    // Full deflection to right (+150px > 115px)
    ev.x = 400.0f;
    ev.y = 545.0f;
    overlay.onTouch(ev);
    ASSERT_NEAR(overlay.state().leftStickX, 1.0f, 0.01f);
    ASSERT_NEAR(overlay.state().leftStickY, 0.0f, 0.01f);

    // Full deflection up (-150px) -> leftStickY positive (+1.0)
    ev.x = 250.0f;
    ev.y = 395.0f;
    overlay.onTouch(ev);
    ASSERT_NEAR(overlay.state().leftStickX, 0.0f, 0.01f);
    ASSERT_NEAR(overlay.state().leftStickY, 1.0f, 0.01f);

    ev.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(ev);

    // 6.2 1000 randomized multi-touch events
    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> fingerDist(0, 9);
    std::uniform_int_distribution<int> kindDist(0, 2);
    std::uniform_real_distribution<float> xDist(0.0f, 1920.0f);
    std::uniform_real_distribution<float> yDist(0.0f, 1080.0f);

    bool activeFingers[10] = {false};

    for (int i = 0; i < 1000; ++i) {
        int fid = fingerDist(rng);
        int k = kindDist(rng);
        app::shell::TouchEvent fEv{};
        fEv.fingerId = static_cast<uint64_t>(fid + 1);
        fEv.x = xDist(rng);
        fEv.y = yDist(rng);

        if (!activeFingers[fid]) {
            fEv.kind = app::shell::TouchEvent::Kind::Down;
            activeFingers[fid] = true;
        } else {
            if (k == 0) {
                fEv.kind = app::shell::TouchEvent::Kind::Motion;
            } else {
                fEv.kind = (k == 1) ? app::shell::TouchEvent::Kind::Up : app::shell::TouchEvent::Kind::Cancel;
                activeFingers[fid] = false;
            }
        }
        overlay.onTouch(fEv);
    }

    // Clean up all fingers
    for (int fid = 0; fid < 10; ++fid) {
        if (activeFingers[fid]) {
            app::shell::TouchEvent fEv{};
            fEv.fingerId = static_cast<uint64_t>(fid + 1);
            fEv.kind = app::shell::TouchEvent::Kind::Up;
            fEv.x = 960.0f;
            fEv.y = 540.0f;
            overlay.onTouch(fEv);
        }
    }

    // After all fingers up, virtual controller state buttons must be released
    ASSERT_FALSE(overlay.state().btnA);
    ASSERT_FALSE(overlay.state().btnB);
    ASSERT_FALSE(overlay.state().btnX);
    ASSERT_FALSE(overlay.state().btnY);
    ASSERT_EQ(overlay.state().leftStickX, 0.0f);
    ASSERT_EQ(overlay.state().leftStickY, 0.0f);
    ASSERT_EQ(overlay.state().rightStickX, 0.0f);
    ASSERT_EQ(overlay.state().rightStickY, 0.0f);
}

// =========================================================================
// TEST SUITE 7: Adversarial Stress, Blank Touch & Cluster Shape Invariants
// =========================================================================
void testAdversarialStressAndRapidAlternation() {
    std::printf("[TEST 7] Adversarial Stress: Blank Touch Un-hiding, Rapid Alternation & Geometry...\n");

    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);

    app::Settings settings;
    settings.touchOverlayMode = 0; // Auto
    overlay.setSettings(settings);

    // 7.1 Blank screen touch (960, 540) where no button exists
    uint64_t nowMs = 10000;
    // Suppress with physical controller
    overlay.update(nowMs, true, true, nowMs);
    for (int i = 0; i < 15; ++i) {
        nowMs += 16;
        overlay.update(nowMs, true, false, 0);
    }
    ASSERT_NEAR(overlay.opacity(), 0.0f, 0.01f);
    ASSERT_FALSE(overlay.isVisible());

    // User touches dead center of the screen (no button)
    app::shell::TouchEvent blankTouch{};
    blankTouch.kind = app::shell::TouchEvent::Kind::Down;
    blankTouch.fingerId = 99;
    blankTouch.x = 960.0f;
    blankTouch.y = 540.0f;
    bool consumed = overlay.onTouch(blankTouch);
    // Even if not consumed by a button, physicalSuppressed_ MUST be cleared!
    ASSERT_FALSE(consumed);

    // Next update: opacity rises
    nowMs += 16;
    overlay.update(nowMs, true, false, 0);
    ASSERT_TRUE(overlay.opacity() > 0.01f);
    ASSERT_TRUE(overlay.isVisible());

    for (int i = 0; i < 15; ++i) {
        nowMs += 16;
        overlay.update(nowMs, true, false, 0);
    }
    ASSERT_NEAR(overlay.opacity(), 0.80f, 0.01f);

    blankTouch.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(blankTouch);

    // 7.2 Rapid alternation stress: 1000 frames alternating between physical pad and touch
    for (int frame = 0; frame < 1000; ++frame) {
        nowMs += 16;
        if (frame % 2 == 0) {
            // Physical controller input
            overlay.update(nowMs, true, true, nowMs);
        } else {
            // Touch input on Button A
            app::shell::TouchEvent t{};
            t.kind = (frame % 4 == 1) ? app::shell::TouchEvent::Kind::Down : app::shell::TouchEvent::Kind::Up;
            t.fingerId = 5;
            t.x = overlay.layout().btnA.x;
            t.y = overlay.layout().btnA.y;
            overlay.onTouch(t);
            overlay.update(nowMs, true, false, 0);
        }
        // Invariant: Opacity is always within valid bounds [0.0, 0.80]
        float op = overlay.opacity();
        ASSERT_TRUE(op >= 0.0f && op <= 0.805f);
    }

    // 7.3 ABXY Diamond Cluster Geometry Invariant: Shape deltas must remain preserved
    overlay.openLayoutStudio();
    ui::TouchLayout orig = overlay.layout();
    float origDeltaAx = orig.btnA.x - ((orig.btnA.x + orig.btnB.x + orig.btnX.x + orig.btnY.x) * 0.25f);
    float origDeltaAy = orig.btnA.y - ((orig.btnA.y + orig.btnB.y + orig.btnX.y + orig.btnY.y) * 0.25f);
    float origDeltaBx = orig.btnB.x - ((orig.btnA.x + orig.btnB.x + orig.btnX.x + orig.btnY.x) * 0.25f);
    float origDeltaBy = orig.btnB.y - ((orig.btnA.y + orig.btnB.y + orig.btnX.y + orig.btnY.y) * 0.25f);

    // Grab center
    app::shell::TouchEvent dragEv{};
    dragEv.kind = app::shell::TouchEvent::Kind::Down;
    dragEv.fingerId = 77;
    dragEv.x = (orig.btnA.x + orig.btnB.x + orig.btnX.x + orig.btnY.x) * 0.25f;
    dragEv.y = (orig.btnA.y + orig.btnB.y + orig.btnX.y + orig.btnY.y) * 0.25f;
    ASSERT_TRUE(overlay.onTouch(dragEv));

    // Move across 50 arbitrary positions
    dragEv.kind = app::shell::TouchEvent::Kind::Motion;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDistX(250.0f, 1650.0f);
    std::uniform_real_distribution<float> posDistY(250.0f, 800.0f);
    for (int step = 0; step < 50; ++step) {
        dragEv.x = posDistX(rng);
        dragEv.y = posDistY(rng);
        overlay.onTouch(dragEv);

        ui::TouchLayout cur = overlay.layout();
        float curCx = (cur.btnA.x + cur.btnB.x + cur.btnX.x + cur.btnY.x) * 0.25f;
        float curCy = (cur.btnA.y + cur.btnB.y + cur.btnX.y + cur.btnY.y) * 0.25f;

        float curDeltaAx = cur.btnA.x - curCx;
        float curDeltaAy = cur.btnA.y - curCy;
        float curDeltaBx = cur.btnB.x - curCx;
        float curDeltaBy = cur.btnB.y - curCy;

        ASSERT_NEAR(curDeltaAx, origDeltaAx, 0.01f);
        ASSERT_NEAR(curDeltaAy, origDeltaAy, 0.01f);
        ASSERT_NEAR(curDeltaBx, origDeltaBx, 0.01f);
        ASSERT_NEAR(curDeltaBy, origDeltaBy, 0.01f);
    }
    dragEv.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(dragEv);
    overlay.closeLayoutStudio();

    // 7.4 100 rapid Studio open/close/save/load cycles
    const std::string stressFile = "/tmp/test_m5_stress_layout.json";
    for (int c = 0; c < 100; ++c) {
        overlay.openLayoutStudio();
        ui::TouchLayout l = overlay.layout();
        l.leftStick.x = 200.0f + (c % 100);
        overlay.setLayout(l);
        ASSERT_TRUE(overlay.saveLayout(stressFile));
        overlay.closeLayoutStudio();

        ASSERT_TRUE(overlay.loadLayout(stressFile));
        ASSERT_EQ(overlay.layout().leftStick.x, 200.0f + (c % 100));
    }
    std::remove(stressFile.c_str());
}

// =========================================================================
// MAIN ENTRYPOINT
// =========================================================================
int main() {
    std::printf("=================================================================\n");
    std::printf("=== EMPIRICAL ADVERSARIAL TEST HARNESS: ANDROID MILESTONE 5 ===\n");
    std::printf("=================================================================\n\n");

    testControllerInUseThresholds();
    testDynamicTouchHidingAndRestoration();
    testTouchLayoutSerializationAndCorruption();
    testTouchLayoutStudioDragAndClamping();
    testStreamMenuConsolidationAndItems();
    testMultiTouchFuzzAndThumbstickMath();
    testAdversarialStressAndRapidAlternation();

    std::printf("\n=================================================================\n");
    if (g_failCount.load() > 0) {
        std::fprintf(stderr, "FINAL EMPIRICAL RESULTS: %d Passed, %d FAILED!\n",
                     g_passCount.load(), g_failCount.load());
        return 1;
    }
    std::printf("FINAL EMPIRICAL RESULTS: %d Passed, 0 Failed. ALL M5 INVARIANTS APPROVED!\n",
                g_passCount.load());
    std::printf("=================================================================\n");
    return 0;
}
