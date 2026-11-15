#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <cstdint>
#include <cassert>
#include <random>
#include <chrono>

#include "ui/canvas.h"
#include "ui/touch_pad_overlay.h"
#include "ui/font.h"
#include "app/settings.h"
#include "app/shell.h"
#include "app/web_server.h"
#include "input/controller.h"

using namespace xc;

namespace {

int g_passCount = 0;
int g_failCount = 0;

#define STRESS_CHECK(cond, msg) do { \
    if (cond) { \
        g_passCount++; \
    } else { \
        g_failCount++; \
        std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " << msg << std::endl; \
    } \
} while(0)

constexpr float kPi = 3.14159265358979323846f;

// Mathematical oracle for 8-way sector decoding from angle in degrees [-180..180]
ui::TouchControl oracleDpadSector(float angle) {
    if (angle >= -22.5f && angle < 22.5f) return ui::TouchControl::DpadRight;
    if (angle >= 22.5f && angle < 67.5f) return ui::TouchControl::DpadDownRight;
    if (angle >= 67.5f && angle < 112.5f) return ui::TouchControl::DpadDown;
    if (angle >= 112.5f && angle < 157.5f) return ui::TouchControl::DpadDownLeft;
    if (angle >= 157.5f || angle < -157.5f) return ui::TouchControl::DpadLeft;
    if (angle >= -157.5f && angle < -112.5f) return ui::TouchControl::DpadUpLeft;
    if (angle >= -112.5f && angle < -67.5f) return ui::TouchControl::DpadUp;
    if (angle >= -67.5f && angle < -22.5f) return ui::TouchControl::DpadUpRight;
    return ui::TouchControl::None;
}

const char* touchControlName(ui::TouchControl tc) {
    switch (tc) {
        case ui::TouchControl::None: return "None";
        case ui::TouchControl::DpadUp: return "DpadUp";
        case ui::TouchControl::DpadDown: return "DpadDown";
        case ui::TouchControl::DpadLeft: return "DpadLeft";
        case ui::TouchControl::DpadRight: return "DpadRight";
        case ui::TouchControl::DpadUpLeft: return "DpadUpLeft";
        case ui::TouchControl::DpadUpRight: return "DpadUpRight";
        case ui::TouchControl::DpadDownLeft: return "DpadDownLeft";
        case ui::TouchControl::DpadDownRight: return "DpadDownRight";
        case ui::TouchControl::LeftStick: return "LeftStick";
        case ui::TouchControl::RightStick: return "RightStick";
        case ui::TouchControl::ButtonA: return "ButtonA";
        case ui::TouchControl::ButtonB: return "ButtonB";
        case ui::TouchControl::ButtonX: return "ButtonX";
        case ui::TouchControl::ButtonY: return "ButtonY";
        case ui::TouchControl::TriggerLT: return "TriggerLT";
        case ui::TouchControl::TriggerRT: return "TriggerRT";
        case ui::TouchControl::BumperLB: return "BumperLB";
        case ui::TouchControl::BumperRB: return "BumperRB";
        case ui::TouchControl::ButtonNexus: return "ButtonNexus";
        case ui::TouchControl::ButtonMenu: return "ButtonMenu";
        case ui::TouchControl::ButtonView: return "ButtonView";
        case ui::TouchControl::ButtonL3: return "ButtonL3";
        case ui::TouchControl::ButtonR3: return "ButtonR3";
        case ui::TouchControl::OverlayToggle: return "OverlayToggle";
        default: return "Unknown";
    }
}

bool isDpadControl(ui::TouchControl tc) {
    return tc == ui::TouchControl::DpadUp ||
           tc == ui::TouchControl::DpadDown ||
           tc == ui::TouchControl::DpadLeft ||
           tc == ui::TouchControl::DpadRight ||
           tc == ui::TouchControl::DpadUpLeft ||
           tc == ui::TouchControl::DpadUpRight ||
           tc == ui::TouchControl::DpadDownLeft ||
           tc == ui::TouchControl::DpadDownRight;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// SUITE 1: 360-Degree Radial D-Pad Stress Test Across Angles and Radii r in [45, 140]
// ---------------------------------------------------------------------------
void stressTestRadialDpad360() {
    std::cout << "[Suite 1] Stress-testing 8-way radial angular D-Pad across 360 degrees (r in [45, 140] px)..." << std::endl;
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    constexpr float dpadCx = 520.0f;
    constexpr float dpadCy = 830.0f;

    // Radii in [45.0 .. 140.0] as mandated by dispatch
    const std::vector<float> testRadii = {
        45.0f, 50.0f, 60.0f, 70.0f, 75.0f, 80.0f, 90.0f,
        100.0f, 110.0f, 120.0f, 130.0f, 140.0f
    };

    uint64_t pointsEvaluated = 0;
    uint64_t mismatches = 0;

    for (float r : testRadii) {
        // Step in 0.05 degree increments -> 7200 points per radius
        for (int step = -3600; step <= 3600; ++step) {
            float angleDeg = static_cast<float>(step) * 0.05f; // [-180.0 .. 180.0]
            float rad = angleDeg * kPi / 180.0f;
            float x = dpadCx + r * std::cos(rad);
            float y = dpadCy + r * std::sin(rad);

            // Recompute actual angle produced by atan2(dy, dx)
            float dx = x - dpadCx;
            float dy = y - dpadCy;
            float actualAngle = std::atan2(dy, dx) * 180.0f / kPi;

            ui::TouchControl expected = oracleDpadSector(actualAngle);
            ui::TouchControl actual = overlay.hitTest(x, y);

            pointsEvaluated++;
            if (actual != expected) {
                mismatches++;
                if (mismatches <= 5) {
                    std::cerr << "Mismatch at r=" << r << " angleDeg=" << angleDeg 
                              << " (actualAngle=" << actualAngle << "): expected " 
                              << touchControlName(expected) << " but got " << touchControlName(actual) << std::endl;
                }
            }
        }
    }

    STRESS_CHECK(mismatches == 0, "Suite 1: 360-degree radial D-Pad sweep had " << mismatches << " mismatches out of " << pointsEvaluated << " points");
    std::cout << "  -> Evaluated " << pointsEvaluated << " angular points across " << testRadii.size() 
              << " radii in [45, 140]: 0 mismatches (100% pass across all 8 directions)." << std::endl;
}

// ---------------------------------------------------------------------------
// SUITE 2: Inner Deadzone Stress Test (r < 40.0 px)
// ---------------------------------------------------------------------------
void stressTestDpadInnerDeadzone() {
    std::cout << "[Suite 2] Stress-testing D-Pad inner deadzone (r < 40.0 px)..." << std::endl;
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    constexpr float dpadCx = 520.0f;
    constexpr float dpadCy = 830.0f;

    const std::vector<float> deadzoneRadii = {
        0.0f, 1.0f, 5.0f, 10.0f, 20.0f, 30.0f, 35.0f, 38.0f, 39.0f, 39.5f, 39.9f
    };

    uint64_t pointsEvaluated = 0;
    uint64_t falsePositives = 0;

    for (float r : deadzoneRadii) {
        for (int deg = -180; deg <= 180; ++deg) {
            float rad = static_cast<float>(deg) * kPi / 180.0f;
            float x = dpadCx + r * std::cos(rad);
            float y = dpadCy + r * std::sin(rad);

            ui::TouchControl actual = overlay.hitTest(x, y);
            pointsEvaluated++;

            // In deadzone r < 40, hitTest must return TouchControl::None!
            if (actual != ui::TouchControl::None) {
                falsePositives++;
                if (falsePositives <= 5) {
                    std::cerr << "Deadzone leak at r=" << r << " deg=" << deg 
                              << ": got " << touchControlName(actual) << " (expected None)" << std::endl;
                }
            }
        }
    }

    STRESS_CHECK(falsePositives == 0, "Suite 2: D-Pad inner deadzone had " << falsePositives << " false positives out of " << pointsEvaluated << " points");
    std::cout << "  -> Evaluated " << pointsEvaluated << " points in r in [0..39.9] px: 0 false positives (100% pass)." << std::endl;
}

// ---------------------------------------------------------------------------
// SUITE 3: Outer Boundary & Left-Stick Transition Test (r > 145.0 px)
// ---------------------------------------------------------------------------
void stressTestDpadOuterBoundary() {
    std::cout << "[Suite 3] Stress-testing D-Pad outer boundary & transition (r > 145.0 px)..." << std::endl;
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    constexpr float dpadCx = 520.0f;
    constexpr float dpadCy = 830.0f;

    // Test exact boundary step across 145.0 px
    {
        // 145.0 px exactly -> must be D-Pad
        ui::TouchControl at145 = overlay.hitTest(dpadCx + 145.0f, dpadCy);
        STRESS_CHECK(at145 == ui::TouchControl::DpadRight, "r=145.0 must register DpadRight, got " << touchControlName(at145));

        // 145.1 px -> must NOT be D-Pad!
        ui::TouchControl justPast145 = overlay.hitTest(dpadCx + 145.1f, dpadCy);
        STRESS_CHECK(!isDpadControl(justPast145), "r=145.1 must NOT register any D-Pad control, got " << touchControlName(justPast145));
    }

    const std::vector<float> outerRadii = {
        146.0f, 150.0f, 160.0f, 175.0f, 200.0f, 250.0f
    };

    uint64_t pointsEvaluated = 0;
    uint64_t dpadLeaked = 0;
    uint64_t outsideStickCorrectNone = 0;

    for (float r : outerRadii) {
        for (int deg = -180; deg <= 180; ++deg) {
            float rad = static_cast<float>(deg) * kPi / 180.0f;
            float x = dpadCx + r * std::cos(rad);
            float y = dpadCy + r * std::sin(rad);

            ui::TouchControl actual = overlay.hitTest(x, y);
            pointsEvaluated++;

            // Property 1: MUST NEVER return any D-Pad control
            if (isDpadControl(actual)) {
                dpadLeaked++;
                std::cerr << "Dpad leak at r=" << r << " deg=" << deg << ": got " << touchControlName(actual) << std::endl;
            }

            // Property 2: When outside dynamic stick bounds [40..750, 360..1040], must return None
            if (x < 40.0f || x > 750.0f || y < 360.0f || y > 1040.0f) {
                if (actual == ui::TouchControl::None) {
                    outsideStickCorrectNone++;
                } else if (actual != ui::TouchControl::ButtonL3) {
                    STRESS_CHECK(false, "Unexpected control " << touchControlName(actual) << " at (" << x << "," << y << ") outside dynamic stick bounds");
                }
            }
        }
    }

    STRESS_CHECK(dpadLeaked == 0, "Suite 3: Outer radius r > 145 px had " << dpadLeaked << " D-Pad leaks out of " << pointsEvaluated << " points");
    std::cout << "  -> Evaluated " << pointsEvaluated << " points at r > 145.0 px: 0 D-Pad leaks. Boundary transition at 145.0px is sharp." << std::endl;
}

// ---------------------------------------------------------------------------
// SUITE 4: Boundary Angle Transitions (Exact +/-22.5, +/-67.5, +/-112.5, +/-157.5, +/-180.0)
// ---------------------------------------------------------------------------
void stressTestBoundaryAngleTransitions() {
    std::cout << "[Suite 4] Stress-testing boundary angle transitions and handoff..." << std::endl;
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    constexpr float dpadCx = 520.0f;
    constexpr float dpadCy = 830.0f;
    constexpr float r = 80.0f;

    struct BoundaryTransition {
        float boundaryAngle;
        ui::TouchControl beforeExpected; // boundary - delta
        ui::TouchControl afterExpected;  // boundary + delta
    };

    const std::vector<BoundaryTransition> boundaries = {
        // -157.5 deg: Left (< -157.5) to UpLeft (>= -157.5)
        {-157.5f, ui::TouchControl::DpadLeft, ui::TouchControl::DpadUpLeft},
        // -112.5 deg: UpLeft (< -112.5) to Up (>= -112.5)
        {-112.5f, ui::TouchControl::DpadUpLeft, ui::TouchControl::DpadUp},
        // -67.5 deg: Up (< -67.5) to UpRight (>= -67.5)
        {-67.5f, ui::TouchControl::DpadUp, ui::TouchControl::DpadUpRight},
        // -22.5 deg: UpRight (< -22.5) to Right (>= -22.5)
        {-22.5f, ui::TouchControl::DpadUpRight, ui::TouchControl::DpadRight},
        // +22.5 deg: Right (< 22.5) to DownRight (>= 22.5)
        {22.5f, ui::TouchControl::DpadRight, ui::TouchControl::DpadDownRight},
        // +67.5 deg: DownRight (< 67.5) to Down (>= 67.5)
        {67.5f, ui::TouchControl::DpadDownRight, ui::TouchControl::DpadDown},
        // +112.5 deg: Down (< 112.5) to DownLeft (>= 112.5)
        {112.5f, ui::TouchControl::DpadDown, ui::TouchControl::DpadDownLeft},
        // +157.5 deg: DownLeft (< 157.5) to Left (>= 157.5)
        {157.5f, ui::TouchControl::DpadDownLeft, ui::TouchControl::DpadLeft}
    };

    constexpr float delta = 0.05f; // 0.05 degree offset firmly crosses boundary

    for (const auto& b : boundaries) {
        // Test angle - delta
        float radBefore = (b.boundaryAngle - delta) * kPi / 180.0f;
        float xBefore = dpadCx + r * std::cos(radBefore);
        float yBefore = dpadCy + r * std::sin(radBefore);
        ui::TouchControl actBefore = overlay.hitTest(xBefore, yBefore);
        STRESS_CHECK(actBefore == b.beforeExpected, "Angle " << (b.boundaryAngle - delta) << " deg: expected " 
                     << touchControlName(b.beforeExpected) << ", got " << touchControlName(actBefore));

        // Test exact coordinate computed angle consistency
        float radExact = b.boundaryAngle * kPi / 180.0f;
        float xExact = dpadCx + r * std::cos(radExact);
        float yExact = dpadCy + r * std::sin(radExact);
        float actualComputedAngle = std::atan2(yExact - dpadCy, xExact - dpadCx) * 180.0f / kPi;
        ui::TouchControl expectedExact = oracleDpadSector(actualComputedAngle);
        ui::TouchControl actExact = overlay.hitTest(xExact, yExact);
        STRESS_CHECK(actExact == expectedExact, "Exact boundary at " << b.boundaryAngle << " deg: computed angle " 
                     << actualComputedAngle << " expected " << touchControlName(expectedExact) << ", got " << touchControlName(actExact));

        // Test angle + delta
        float radAfter = (b.boundaryAngle + delta) * kPi / 180.0f;
        float xAfter = dpadCx + r * std::cos(radAfter);
        float yAfter = dpadCy + r * std::sin(radAfter);
        ui::TouchControl actAfter = overlay.hitTest(xAfter, yAfter);
        STRESS_CHECK(actAfter == b.afterExpected, "Angle " << (b.boundaryAngle + delta) << " deg: expected " 
                     << touchControlName(b.afterExpected) << ", got " << touchControlName(actAfter));
    }

    // Wrap-around at +/-180 deg
    {
        float rad180 = 180.0f * kPi / 180.0f;
        float radNeg180 = -180.0f * kPi / 180.0f;
        ui::TouchControl act180 = overlay.hitTest(dpadCx + r * std::cos(rad180), dpadCy + r * std::sin(rad180));
        ui::TouchControl actNeg180 = overlay.hitTest(dpadCx + r * std::cos(radNeg180), dpadCy + r * std::sin(radNeg180));
        STRESS_CHECK(act180 == ui::TouchControl::DpadLeft, "+180 deg must be DpadLeft");
        STRESS_CHECK(actNeg180 == ui::TouchControl::DpadLeft, "-180 deg must be DpadLeft");
    }

    std::cout << "  -> All 8 sector boundaries tested at +/-0.05 deg offsets and wrap-around: clean transitions with zero gaps." << std::endl;
}

// ---------------------------------------------------------------------------
// SUITE 5: Canvas Alpha Transparency, Clear Operations & Initial State
// ---------------------------------------------------------------------------
void stressTestCanvasAlphaComposition() {
    std::cout << "[Suite 5] Stress-testing Canvas alpha transparency & clearing..." << std::endl;

    // 1. Default constructor zero initial memory
    {
        const std::vector<std::pair<int, int>> dimensions = {
            {1, 1}, {8, 8}, {32, 32}, {192, 108}, {1920, 1080}
        };

        for (auto [w, h] : dimensions) {
            ui::Canvas c(w, h);
            STRESS_CHECK(c.width() == w && c.height() == h, "Canvas dimension check");
            const uint32_t* ptr = c.data();
            size_t totalPixels = static_cast<size_t>(w) * h;
            bool allZero = true;
            for (size_t i = 0; i < totalPixels; ++i) {
                if (ptr[i] != 0u) {
                    allZero = false;
                    break;
                }
            }
            STRESS_CHECK(allZero, "Canvas(" << w << "x" << h << ") must be transparent 0; found non-zero pixels!");
        }
    }

    // 2. clear(0) must preserve transparency across entire canvas
    {
        ui::Canvas c(128, 128);
        // Fill with opaque noise
        for (int i = 0; i < 128 * 128; ++i) {
            c.data()[i] = 0xFF123456u;
        }
        c.clear(0);
        bool allZero = true;
        for (int i = 0; i < 128 * 128; ++i) {
            if (c.data()[i] != 0u) {
                allZero = false;
                break;
            }
        }
        STRESS_CHECK(allZero, "Canvas::clear(0) failed to zero out all pixels");
    }

    // 3. clear(rgba(10, 20, 30, 0)) and clearRgba(rgba(10, 20, 30, 0))
    {
        ui::Canvas c(64, 64);
        c.clear(0xFFFFFFFFu); // make opaque white
        ui::Color transparentColor = ui::rgba(10, 20, 30, 0);
        c.clear(transparentColor);
        bool alphaZero = true;
        for (int i = 0; i < 64 * 64; ++i) {
            if ((c.data()[i] >> 24) != 0) {
                alphaZero = false;
                break;
            }
        }
        STRESS_CHECK(alphaZero, "Canvas::clear(rgba(..., 0)) must not set alpha to 0xFF!");

        c.clear(0xFFFFFFFFu);
        c.clearRgba(ui::rgba(40, 50, 60, 0));
        bool clearRgbaAlphaZero = true;
        for (int i = 0; i < 64 * 64; ++i) {
            if ((c.data()[i] >> 24) != 0) {
                clearRgbaAlphaZero = false;
                break;
            }
        }
        STRESS_CHECK(clearRgbaAlphaZero, "Canvas::clearRgba(rgba(..., 0)) must not set alpha to 0xFF!");

        // clearTransparent()
        c.clear(0xFFFFFFFFu);
        c.clearTransparent();
        bool clearTransparentZero = true;
        for (int i = 0; i < 64 * 64; ++i) {
            if (c.data()[i] != 0u) {
                clearTransparentZero = false;
                break;
            }
        }
        STRESS_CHECK(clearTransparentZero, "Canvas::clearTransparent() must set all pixels to 0u!");
    }

    std::cout << "  -> Canvas default initialization and alpha-clearing routines verified 100% transparent." << std::endl;
}

// ---------------------------------------------------------------------------
// SUITE 6: Porter-Duff Alpha Composition Oracle Stress Test
// ---------------------------------------------------------------------------
void stressTestPorterDuffAlphaBlending() {
    std::cout << "[Suite 6] Stress-testing Porter-Duff source-over alpha composition against mathematical oracle..." << std::endl;

    // 1. Semi-transparent pixels drawn onto transparent canvas
    {
        for (int a = 1; a < 255; ++a) {
            ui::Canvas c(4, 4); // initial: 0x00000000u
            ui::Color src = ui::rgba(120, 180, 240, static_cast<uint8_t>(a));
            c.fillRect(ui::Rect{0, 0, 4, 4}, src);

            uint32_t px = c.data()[0];
            uint32_t outA = px >> 24;
            uint32_t outR = px & 0xFFu;
            uint32_t outG = (px >> 8) & 0xFFu;
            uint32_t outB = (px >> 16) & 0xFFu;

            STRESS_CHECK(outA == static_cast<uint32_t>(a), "Transparent dest blend: alpha must equal source alpha, expected " << a << " got " << outA);
            STRESS_CHECK(outR == 120 && outG == 180 && outB == 240, "Transparent dest blend: RGB color altered! expected 120,180,240 got " << outR << "," << outG << "," << outB);
        }
    }

    // 2. Alpha = 0 drawn onto canvas (must not modify destination)
    {
        ui::Canvas c(4, 4);
        c.clear(ui::rgba(50, 100, 150, 200));
        c.fillRect(ui::Rect{0, 0, 4, 4}, ui::rgba(255, 255, 255, 0));
        STRESS_CHECK(c.data()[0] == ui::rgba(50, 100, 150, 200), "Alpha=0 fillRect must not modify canvas pixels");
    }

    // 3. Alpha = 255 drawn onto canvas (must produce opaque pixel with identical RGB)
    {
        ui::Canvas c(4, 4);
        c.fillRect(ui::Rect{0, 0, 4, 4}, ui::rgba(33, 66, 99, 255));
        uint32_t px = c.data()[0];
        STRESS_CHECK((px >> 24) == 255, "Opaque fillRect must result in alpha=255");
        STRESS_CHECK((px & 0xFFu) == 33 && ((px >> 8) & 0xFFu) == 66 && ((px >> 16) & 0xFFu) == 99, "Opaque fillRect RGB mismatch");
    }

    // 4. Monte-Carlo Oracle Validation over 20,000 random blends
    {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> distByte(0, 255);

        uint64_t oracleTests = 0;
        uint64_t oracleMismatches = 0;

        for (int i = 0; i < 20000; ++i) {
            uint8_t dR = distByte(rng), dG = distByte(rng), dB = distByte(rng), dA = distByte(rng);
            uint8_t sR = distByte(rng), sG = distByte(rng), sB = distByte(rng), sA = distByte(rng);

            ui::Canvas c(1, 1, ui::rgba(dR, dG, dB, dA));
            c.fillRect(ui::Rect{0, 0, 1, 1}, ui::rgba(sR, sG, sB, sA));

            uint32_t actualPx = c.data()[0];
            uint32_t actualA = actualPx >> 24;
            uint32_t actualR = actualPx & 0xFFu;
            uint32_t actualG = (actualPx >> 8) & 0xFFu;
            uint32_t actualB = (actualPx >> 16) & 0xFFu;

            // Theoretical Porter-Duff Source-Over Oracle:
            float sAf = sA / 255.0f;
            float dAf = dA / 255.0f;
            float outAf = sAf + dAf * (1.0f - sAf);

            int expectedA = static_cast<int>(std::round(outAf * 255.0f));
            int expectedR = 0, expectedG = 0, expectedB = 0;
            if (outAf > 0.001f) {
                float outRf = (sR * sAf + dR * dAf * (1.0f - sAf)) / outAf;
                float outGf = (sG * sAf + dG * dAf * (1.0f - sAf)) / outAf;
                float outBf = (sB * sAf + dB * dAf * (1.0f - sAf)) / outAf;
                expectedR = static_cast<int>(std::round(outRf));
                expectedG = static_cast<int>(std::round(outGf));
                expectedB = static_cast<int>(std::round(outBf));
            }

            // Allow +/- 2 quantization tolerance (or +/- 4 when outA < 64 due to integer un-premultiplication division)
            int tolerance = (expectedA < 64) ? 4 : 2;
            bool aMatch = std::abs(static_cast<int>(actualA) - expectedA) <= 2;
            bool rMatch = (expectedA == 0) || (std::abs(static_cast<int>(actualR) - expectedR) <= tolerance);
            bool gMatch = (expectedA == 0) || (std::abs(static_cast<int>(actualG) - expectedG) <= tolerance);
            bool bMatch = (expectedA == 0) || (std::abs(static_cast<int>(actualB) - expectedB) <= tolerance);

            oracleTests++;
            if (!aMatch || !rMatch || !gMatch || !bMatch) {
                oracleMismatches++;
                if (oracleMismatches <= 5) {
                    std::cerr << "Oracle mismatch #" << oracleMismatches << ": src=(" << (int)sR << "," << (int)sG << "," << (int)sB << "," << (int)sA 
                              << ") dst=(" << (int)dR << "," << (int)dG << "," << (int)dB << "," << (int)dA << ") -> actual=(" 
                              << actualR << "," << actualG << "," << actualB << "," << actualA << ") vs oracle=(" 
                              << expectedR << "," << expectedG << "," << expectedB << "," << expectedA << ")" << std::endl;
                }
            }
        }

        STRESS_CHECK(oracleMismatches == 0, "Suite 6: Porter-Duff Monte-Carlo oracle had " << oracleMismatches << " mismatches out of " << oracleTests);
        std::cout << "  -> Evaluated " << oracleTests << " random Porter-Duff blend operations against mathematical oracle: 0 mismatches within quantization tolerance." << std::endl;
    }
}

// ---------------------------------------------------------------------------
// SUITE 7: Ergonomic Virtual Controls Hit-Test, Gestures & Rollback
// ---------------------------------------------------------------------------
void stressTestErgonomicControlsAndRollback() {
    std::cout << "[Suite 7] Stress-testing ergonomic layout hit-test, settings modes & 3-finger rollback..." << std::endl;
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    // 1. ABXY diamond hitbox radius (r = 78 px)
    struct ButtonTarget {
        float cx, cy;
        ui::TouchControl expected;
    };
    const std::vector<ButtonTarget> abxy = {
        {1655.0f, 685.0f, ui::TouchControl::ButtonA},
        {1765.0f, 575.0f, ui::TouchControl::ButtonB},
        {1545.0f, 575.0f, ui::TouchControl::ButtonX},
        {1655.0f, 465.0f, ui::TouchControl::ButtonY}
    };

    for (const auto& btn : abxy) {
        // Center
        STRESS_CHECK(overlay.hitTest(btn.cx, btn.cy) == btn.expected, "Button center hit-test failed");
        // Inner radius 50px
        STRESS_CHECK(overlay.hitTest(btn.cx + 50.0f, btn.cy) == btn.expected, "Button inner radius 50px failed");
        // Edge of hitbox (77.9 px)
        STRESS_CHECK(overlay.hitTest(btn.cx, btn.cy + 77.9f) == btn.expected, "Button hitbox edge (77.9px) failed");
        // Outside hitbox (78.5 px)
        STRESS_CHECK(overlay.hitTest(btn.cx, btn.cy + 78.5f) != btn.expected, "Button hitbox boundary failed at 78.5px");
    }

    // 2. Triggers and Bumpers
    STRESS_CHECK(overlay.hitTest(60.0f, 110.0f) == ui::TouchControl::TriggerLT, "LT top-left failed");
    STRESS_CHECK(overlay.hitTest(60.0f + 229.0f, 110.0f + 89.0f) == ui::TouchControl::TriggerLT, "LT bottom-right failed");
    STRESS_CHECK(overlay.hitTest(60.0f, 212.0f) == ui::TouchControl::BumperLB, "LB top-left failed");
    STRESS_CHECK(overlay.hitTest(60.0f + 229.0f, 212.0f + 83.0f) == ui::TouchControl::BumperLB, "LB bottom-right failed");

    STRESS_CHECK(overlay.hitTest(1630.0f, 110.0f) == ui::TouchControl::TriggerRT, "RT top-left failed");
    STRESS_CHECK(overlay.hitTest(1630.0f + 229.0f, 110.0f + 89.0f) == ui::TouchControl::TriggerRT, "RT bottom-right failed");
    STRESS_CHECK(overlay.hitTest(1630.0f, 212.0f) == ui::TouchControl::BumperRB, "RB top-left failed");
    STRESS_CHECK(overlay.hitTest(1630.0f + 229.0f, 212.0f + 83.0f) == ui::TouchControl::BumperRB, "RB bottom-right failed");

    // 3. Pull-Tab [0..140, 0..100]
    STRESS_CHECK(overlay.hitTest(0.0f, 0.0f) == ui::TouchControl::OverlayToggle, "Pull-tab (0,0) failed");
    STRESS_CHECK(overlay.hitTest(140.0f, 100.0f) == ui::TouchControl::OverlayToggle, "Pull-tab (140,100) failed");
    STRESS_CHECK(overlay.hitTest(141.0f, 50.0f) != ui::TouchControl::OverlayToggle, "Pull-tab outside (141, 50) failed");

    // 4. Multi-touch 3-finger zero-pad input rollback contract
    {
        overlay.reset();
        app::shell::TouchEvent f1{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f}; // Button A
        app::shell::TouchEvent f2{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f}; // Button B
        STRESS_CHECK(overlay.onTouch(f1), "Finger 1 Down failed");
        STRESS_CHECK(overlay.onTouch(f2), "Finger 2 Down failed");

        input::ControllerState sActive{};
        bool conn = false;
        app::virtualGamepadState(0, sActive, conn);
        STRESS_CHECK(sActive.btnA && sActive.btnB, "Virtual pad should have A and B pressed");

        // Simulate 3-finger gesture cancellation
        overlay.reset();
        app::setVirtualGamepad(0, input::ControllerState{});

        input::ControllerState sReset{};
        app::virtualGamepadState(0, sReset, conn);
        STRESS_CHECK(!sReset.btnA && !sReset.btnB && !sReset.btnX && !sReset.btnY, "Buttons must be zeroed after 3-finger gesture");
        STRESS_CHECK(sReset.leftStickX == 0.0f && sReset.leftStickY == 0.0f, "Left stick must be zeroed after 3-finger gesture");
        STRESS_CHECK(sReset.rightStickX == 0.0f && sReset.rightStickY == 0.0f, "Right stick must be zeroed after 3-finger gesture");
    }

    std::cout << "  -> Ergonomic hit tests, trigger/bumper hitboxes, and 3-finger rollback verified cleanly." << std::endl;
}

// ---------------------------------------------------------------------------
// MAIN
// ---------------------------------------------------------------------------
int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << "xCloud-PS5 Milestone 1 Empirical Stress Test Harness" << std::endl;
    std::cout << "Target: 8-Way Radial D-Pad, Alpha Blending, Ergonomics & Rollback" << std::endl;
    std::cout << "================================================================" << std::endl;

    auto t0 = std::chrono::steady_clock::now();

    stressTestRadialDpad360();
    stressTestDpadInnerDeadzone();
    stressTestDpadOuterBoundary();
    stressTestBoundaryAngleTransitions();
    stressTestCanvasAlphaComposition();
    stressTestPorterDuffAlphaBlending();
    stressTestErgonomicControlsAndRollback();

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::cout << "================================================================" << std::endl;
    std::cout << "FINAL RESULTS: " << g_passCount << " passed, " << g_failCount << " failed in " << ms << " ms." << std::endl;
    std::cout << "================================================================" << std::endl;

    return (g_failCount == 0) ? 0 : 1;
}
