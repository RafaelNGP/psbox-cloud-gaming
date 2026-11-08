// SPDX-License-Identifier: GPL-3.0-or-later
// Empirical Adversarial Verification Harness for Milestone 4 (Bloco D)
// Focus: GyroCalibrator State Machine, Settings Persistence & Shadowing, StreamMenu Transitions

#include "input/gyro.h"
#include "app/settings.h"
#include "ui/stream_menu.h"
#include "ui/font.h"
#include "ui/canvas.h"
#include "util/json.h"
#include "util/log.h"
#include "platform/platform.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "[FAIL] %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(std::abs((a) - (b)) <= (eps))

using namespace xc;

static std::string readTestFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// =========================================================================
// TEST SUITE 1: GyroCalibrator State Machine Lifecycle & Robustness
// =========================================================================
static void testCalibratorLifecycle() {
    std::printf("--- [TEST 1] GyroCalibrator State Machine Lifecycle ---\n");

    input::GyroCalibrator cal;

    // 1. Initial State
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Idle);
    ASSERT_EQ(cal.sampleCount, 0);
    ASSERT_EQ(cal.targetSamples, 240);
    ASSERT_FALSE(cal.isCalibrating());
    ASSERT_FALSE(cal.isDone());
    ASSERT_EQ(cal.progress(), 0.0f);

    // Feeding in Idle must be rejected safely
    float dummySample[3] = {0.01f, -0.02f, 0.03f};
    ASSERT_FALSE(cal.feed(dummySample));
    ASSERT_EQ(cal.sampleCount, 0);
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Idle);

    // 2. Start calibration with custom count (100)
    cal.start(100);
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Calibrating);
    ASSERT_TRUE(cal.isCalibrating());
    ASSERT_FALSE(cal.isDone());
    ASSERT_EQ(cal.sampleCount, 0);
    ASSERT_EQ(cal.targetSamples, 100);
    ASSERT_EQ(cal.progress(), 0.0f);

    // Feeding nullptr must return false without modifying sampleCount
    ASSERT_FALSE(cal.feed(nullptr));
    ASSERT_EQ(cal.sampleCount, 0);

    // Feed 99 stationary samples
    const float trueBias[3] = {0.025f, -0.040f, 0.015f};
    for (int i = 0; i < 99; ++i) {
        float sample[3] = {trueBias[0], trueBias[1], trueBias[2]};
        bool done = cal.feed(sample);
        ASSERT_FALSE(done);
        ASSERT_TRUE(cal.isCalibrating());
        ASSERT_FALSE(cal.isDone());
        ASSERT_EQ(cal.sampleCount, i + 1);
        ASSERT_NEAR(cal.progress(), (i + 1) / 100.0f, 0.001f);
    }

    // 100th sample completes and transitions to Succeeded
    bool finished = cal.feed(dummySample);
    ASSERT_TRUE(finished);
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Succeeded);
    ASSERT_FALSE(cal.isCalibrating());
    ASSERT_TRUE(cal.isDone());
    ASSERT_EQ(cal.progress(), 1.0f);
    ASSERT_EQ(cal.sampleCount, 100);
    ASSERT_TRUE(cal.maxVariance <= 0.0025f);
    ASSERT_NEAR(cal.resultBias[0], trueBias[0], 0.005f);

    // Feeding while Succeeded must return false and not increment
    ASSERT_FALSE(cal.feed(dummySample));
    ASSERT_EQ(cal.sampleCount, 100);
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Succeeded);

    // 3. Restart via start(50) from Succeeded -> transitions back to Calibrating
    cal.start(50);
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Calibrating);
    ASSERT_TRUE(cal.isCalibrating());
    ASSERT_FALSE(cal.isDone());
    ASSERT_EQ(cal.sampleCount, 0);
    ASSERT_EQ(cal.targetSamples, 50);
    ASSERT_EQ(cal.progress(), 0.0f);

    // Feed 50 moving/noisy samples (sinusoidal motion with amplitude 0.2 rad/s)
    for (int i = 0; i < 49; ++i) {
        float t = i * 0.1f;
        float movingSample[3] = {std::sin(t) * 0.2f, std::cos(t) * 0.2f, 0.0f};
        ASSERT_FALSE(cal.feed(movingSample));
    }
    float lastMoving[3] = {std::sin(4.9f) * 0.2f, std::cos(4.9f) * 0.2f, 0.0f};
    bool doneFail = cal.feed(lastMoving);
    ASSERT_FALSE(doneFail); // feed returns false when Failed
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Failed);
    ASSERT_FALSE(cal.isCalibrating());
    ASSERT_TRUE(cal.isDone());
    ASSERT_EQ(cal.progress(), 1.0f);
    ASSERT_TRUE(cal.maxVariance > 0.0025f);

    // Feeding while Failed must return false and not modify state
    ASSERT_FALSE(cal.feed(dummySample));
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Failed);

    // 4. Restart via start() from Failed -> can succeed on stationary data
    cal.start(60);
    ASSERT_TRUE(cal.isCalibrating());
    for (int i = 0; i < 60; ++i) {
        float sample[3] = {0.05f, 0.05f, 0.05f};
        cal.feed(sample);
    }
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Succeeded);
    ASSERT_NEAR(cal.resultBias[0], 0.05f, 0.0001f);

    // 5. Reset() transitions to Idle
    cal.reset();
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Idle);
    ASSERT_EQ(cal.sampleCount, 0);
    ASSERT_EQ(cal.targetSamples, 240);
    ASSERT_EQ(cal.progress(), 0.0f);
    ASSERT_EQ(cal.maxVariance, 0.0f);

    // 6. Edge cases: zero or negative samples fall back to 240
    cal.start(0);
    ASSERT_EQ(cal.targetSamples, 240);
    cal.start(-50);
    ASSERT_EQ(cal.targetSamples, 240);

    // 7. Variance threshold boundary precision test
    // Var = (sumSq / N) - (mean)^2. Construct exact variance test:
    // N=2 samples: -A and +A. Mean = 0, Var = A^2.
    // When A = sqrt(0.00249) ~ 0.049899 -> Var <= 0.0025 -> Succeeded
    cal.start(2);
    float a1 = 0.04989f;
    float s1[3] = {a1, 0, 0};
    float s2[3] = {-a1, 0, 0};
    cal.feed(s1);
    ASSERT_TRUE(cal.feed(s2));
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Succeeded);

    // When A = sqrt(0.00251) ~ 0.05010 -> Var > 0.0025 -> Failed
    cal.start(2);
    float a2 = 0.05015f;
    float s3[3] = {a2, 0, 0};
    float s4[3] = {-a2, 0, 0};
    cal.feed(s3);
    ASSERT_FALSE(cal.feed(s4));
    ASSERT_EQ(cal.state, input::GyroCalibrator::State::Failed);

    std::printf("[PASS] GyroCalibrator State Machine Lifecycle & Edge Cases verified successfully.\n");
}

// =========================================================================
// TEST SUITE 2: Settings Persistence & Per-Game Shadowing
// =========================================================================
static void testSettingsPersistenceAndShadowing() {
    std::printf("--- [TEST 2] Settings Persistence & Per-Game Shadowing ---\n");

    const std::string testFile = "/tmp/test_m4_persistence.json";
    unlink(testFile.c_str());

    // 1. Verify default values
    app::Settings s;
    ASSERT_EQ(s.gyroYawScale, 100);
    ASSERT_EQ(s.gyroPitchScale, 80);
    ASSERT_EQ(s.gyroBias[0], 0.0f);
    ASSERT_EQ(s.gyroBias[1], 0.0f);
    ASSERT_EQ(s.gyroBias[2], 0.0f);

    app::GameProfile defProf = s.defaultProfile();
    ASSERT_EQ(defProf.gyroYawScale, 100);
    ASSERT_EQ(defProf.gyroPitchScale, 80);
    ASSERT_EQ(defProf.gyroBias[0], 0.0f);

    // 2. Global customization & save
    s.gyroYawScale = 150;
    s.gyroPitchScale = 110;
    s.gyroBias[0] = 0.0125f;
    s.gyroBias[1] = -0.0350f;
    s.gyroBias[2] = 0.0085f;

    // Per-game customization for "game_halo" (productId)
    app::GameProfile haloProf = s.defaultProfile();
    haloProf.gyroYawScale = 180;
    haloProf.gyroPitchScale = 140;
    haloProf.gyroBias[0] = 0.0500f;
    haloProf.gyroBias[1] = -0.0200f;
    haloProf.gyroBias[2] = 0.0100f;
    s.perGame["game_halo"] = haloProf;

    // Per-game customization for "title_gears" (titleId)
    app::GameProfile gearsProf = s.defaultProfile();
    gearsProf.gyroYawScale = 60;
    gearsProf.gyroPitchScale = 40;
    s.perGame["title_gears"] = gearsProf;

    ASSERT_TRUE(s.save(testFile));

    // 3. Inspect raw JSON file to confirm serialization keys and formats
    std::string content = readTestFile(testFile);
    ASSERT_FALSE(content.empty());
    ASSERT_TRUE(content.find("\"gyroYawScale\": 150") != std::string::npos || content.find("\"gyroYawScale\":150") != std::string::npos);
    ASSERT_TRUE(content.find("\"gyroPitchScale\": 110") != std::string::npos || content.find("\"gyroPitchScale\":110") != std::string::npos);
    ASSERT_TRUE(content.find("\"gyroBias\":") != std::string::npos);
    ASSERT_TRUE(content.find("\"game_halo\"") != std::string::npos);
    ASSERT_TRUE(content.find("\"title_gears\"") != std::string::npos);

    // 4. Reload into fresh Settings instance
    app::Settings loaded;
    ASSERT_TRUE(loaded.load(testFile));
    ASSERT_EQ(loaded.gyroYawScale, 150);
    ASSERT_EQ(loaded.gyroPitchScale, 110);
    ASSERT_NEAR(loaded.gyroBias[0], 0.0125f, 0.0001f);
    ASSERT_NEAR(loaded.gyroBias[1], -0.0350f, 0.0001f);
    ASSERT_NEAR(loaded.gyroBias[2], 0.0085f, 0.0001f);

    // 5. Verify Per-Game Profile Resolution & Shadowing
    bool hasCustom = false;
    app::GameProfile p1 = loaded.profileForGame("game_halo", "", &hasCustom);
    ASSERT_TRUE(hasCustom);
    ASSERT_EQ(p1.gyroYawScale, 180);
    ASSERT_EQ(p1.gyroPitchScale, 140);
    ASSERT_NEAR(p1.gyroBias[0], 0.0500f, 0.0001f);

    // titleId fallback resolution
    hasCustom = false;
    app::GameProfile p2 = loaded.profileForGame("", "title_gears", &hasCustom);
    ASSERT_TRUE(hasCustom);
    ASSERT_EQ(p2.gyroYawScale, 60);
    ASSERT_EQ(p2.gyroPitchScale, 40);

    // Game without custom profile must inherit loaded global settings
    hasCustom = true;
    app::GameProfile p3 = loaded.profileForGame("unknown_game", "unknown_title", &hasCustom);
    ASSERT_FALSE(hasCustom);
    ASSERT_EQ(p3.gyroYawScale, 150);
    ASSERT_EQ(p3.gyroPitchScale, 110);
    ASSERT_NEAR(p3.gyroBias[0], 0.0125f, 0.0001f);

    // Mutate global settings: custom profile must remain shadowed!
    loaded.gyroYawScale = 200;
    loaded.gyroPitchScale = 200;
    app::GameProfile p1_after = loaded.profileForGame("game_halo", "");
    ASSERT_EQ(p1_after.gyroYawScale, 180); // Preserved custom override
    ASSERT_EQ(p1_after.gyroPitchScale, 140);

    app::GameProfile p3_after = loaded.profileForGame("unknown_game", "");
    ASSERT_EQ(p3_after.gyroYawScale, 200); // Updated with new global

    // 6. Robustness against JSON corruption and boundary violations
    // Test clamping of scales on load: 10 -> 20, 300 -> 200
    std::string corruptedJson = "{\n"
        "  \"gyroYawScale\": 10,\n"
        "  \"gyroPitchScale\": 300,\n"
        "  \"gyroBias\": [1.5, 2.5],\n" // Truncated array (only 2 elements)
        "  \"perGame\": {\n"
        "    \"broken\": {\n"
        "      \"gyroYawScale\": 500,\n"
        "      \"gyroPitchScale\": -20,\n"
        "      \"gyroBias\": \"not an array\"\n"
        "    }\n"
        "  }\n"
        "}";
    const std::string corruptFile = "/tmp/test_m4_corrupt.json";
    ASSERT_TRUE(platform::writeFileAtomic(corruptFile, corruptedJson));

    app::Settings safeSettings;
    safeSettings.gyroYawScale = 100;
    safeSettings.gyroPitchScale = 80;
    safeSettings.gyroBias[0] = 0.0f;
    safeSettings.gyroBias[1] = 0.0f;
    safeSettings.gyroBias[2] = 0.0f;

    ASSERT_TRUE(safeSettings.load(corruptFile));
    // Clamping verified
    ASSERT_EQ(safeSettings.gyroYawScale, 20); // Clamped from 10 to 20
    ASSERT_EQ(safeSettings.gyroPitchScale, 200); // Clamped from 300 to 200
    // Truncated array did not corrupt or segfault
    ASSERT_EQ(safeSettings.gyroBias[0], 0.0f);
    ASSERT_EQ(safeSettings.gyroBias[1], 0.0f);
    ASSERT_EQ(safeSettings.gyroBias[2], 0.0f);

    // Per-game clamping & type fallback
    app::GameProfile brokenProf = safeSettings.profileForGame("broken", "");
    ASSERT_EQ(brokenProf.gyroYawScale, 200); // Clamped from 500 to 200
    ASSERT_EQ(brokenProf.gyroPitchScale, 20); // Clamped from -20 to 20
    ASSERT_EQ(brokenProf.gyroBias[0], 0.0f);

    unlink(testFile.c_str());
    unlink(corruptFile.c_str());
    std::printf("[PASS] Settings Persistence & Per-Game Shadowing verified successfully.\n");
}

// =========================================================================
// TEST SUITE 3: StreamMenu & UI State Transitions
// =========================================================================
static void testStreamMenuTransitions() {
    std::printf("--- [TEST 3] StreamMenu & UI State Transitions ---\n");

    ui::Fonts fonts;
    ui::StreamMenu menu(fonts);

    // Default UiCaps has gyro = true
    ui::UiCaps caps;
    caps.gyro = true;
    menu.setCapabilities(caps);

    auto press = [&](void (*set)(ui::NavInput&)) {
        ui::NavInput n;
        set(n);
        return menu.handle(n);
    };

    menu.open(0, false);
    ASSERT_TRUE(menu.isOpen());

    // Navigate to GyroItem in Controls tab
    menu.setTab(ui::MenuTab::Controls);
    for (int i = 0; i < 5; ++i) {
        press([](ui::NavInput& n) { n.down = true; });
    }

    // 1. GyroItem toggle (ATIVADO / DESATIVADO)
    ASSERT_EQ(menu.gyroMode(), 0);
    ui::MenuAction act = press([](ui::NavInput& n) { n.right = true; });
    ASSERT_EQ(act, ui::MenuAction::Gyro);
    ASSERT_EQ(menu.gyroMode(), 1);

    act = press([](ui::NavInput& n) { n.left = true; });
    ASSERT_EQ(act, ui::MenuAction::Gyro);
    ASSERT_EQ(menu.gyroMode(), 0);

    // Accept on GyroItem opens single-wheel modal
    act = press([](ui::NavInput& n) { n.accept = true; });
    ASSERT_EQ(act, ui::MenuAction::None);
    ASSERT_TRUE(menu.isGyroDialogOpen());

    // Test GyroYawScale & GyroPitchScale getters and setters
    ASSERT_EQ(menu.gyroYawScale(), 100);
    menu.setGyroYawScale(110);
    ASSERT_EQ(menu.gyroYawScale(), 110);
    menu.setGyroYawScale(100);
    ASSERT_EQ(menu.gyroYawScale(), 100);

    menu.setGyroYawScale(20);
    ASSERT_EQ(menu.gyroYawScale(), 20);
    menu.setGyroYawScale(200);
    ASSERT_EQ(menu.gyroYawScale(), 200);

    ASSERT_EQ(menu.gyroPitchScale(), 80);
    menu.setGyroPitchScale(90);
    ASSERT_EQ(menu.gyroPitchScale(), 90);
    menu.setGyroPitchScale(80);
    ASSERT_EQ(menu.gyroPitchScale(), 80);

    menu.setGyroPitchScale(20);
    ASSERT_EQ(menu.gyroPitchScale(), 20);
    menu.setGyroPitchScale(200);
    ASSERT_EQ(menu.gyroPitchScale(), 200);

    // 2. Gyro Calibration state machine
    menu.startGyroCalibration(240);
    ASSERT_TRUE(menu.isGyroCalibrating());
    ASSERT_FALSE(menu.gyroCalSucceeded());
    ASSERT_FALSE(menu.gyroCalFailed());
    ASSERT_EQ(menu.gyroCalProgress(), 0.0f);

    // Feed 120 samples
    const float calBias[3] = {0.030f, -0.045f, 0.012f};
    for (int i = 0; i < 120; ++i) {
        float sample[3] = {calBias[0], calBias[1], calBias[2]};
        ASSERT_FALSE(menu.feedGyroCalibration(sample));
        ASSERT_TRUE(menu.isGyroCalibrating());
        ASSERT_NEAR(menu.gyroCalProgress(), (i + 1) / 240.0f, 0.001f);
    }
    ASSERT_NEAR(menu.gyroCalProgress(), 0.5f, 0.001f);

    // Feed remaining 120 samples
    for (int i = 120; i < 239; ++i) {
        float sample[3] = {calBias[0], calBias[1], calBias[2]};
        ASSERT_FALSE(menu.feedGyroCalibration(sample));
    }
    float lastSample[3] = {calBias[0], calBias[1], calBias[2]};
    bool calDone = menu.feedGyroCalibration(lastSample);
    ASSERT_TRUE(calDone);
    ASSERT_FALSE(menu.isGyroCalibrating());
    ASSERT_TRUE(menu.gyroCalSucceeded());
    ASSERT_FALSE(menu.gyroCalFailed());
    ASSERT_EQ(menu.gyroCalProgress(), 1.0f);

    // Verify calibrated bias in menu
    const float* resultB = menu.gyroBias();
    ASSERT_NEAR(resultB[0], calBias[0], 0.0001f);
    ASSERT_NEAR(resultB[1], calBias[1], 0.0001f);
    ASSERT_NEAR(resultB[2], calBias[2], 0.0001f);

    // Verify global calibration updated
    float gBias[3]{}, gYaw = 0, gPitch = 0;
    input::getGlobalGyroCalibration(gBias, gYaw, gPitch);
    ASSERT_NEAR(gBias[0], calBias[0], 0.0001f);
    ASSERT_NEAR(gBias[1], calBias[1], 0.0001f);
    ASSERT_NEAR(gBias[2], calBias[2], 0.0001f);

    // Test failure on noisy data
    menu.startGyroCalibration(240);
    ASSERT_TRUE(menu.isGyroCalibrating());
    for (int i = 0; i < 239; ++i) {
        float sample[3] = {std::sin(i * 0.1f) * 0.3f, 0.0f, 0.0f};
        menu.feedGyroCalibration(sample);
    }
    float noisyLast[3] = {std::sin(23.9f) * 0.3f, 0.0f, 0.0f};
    bool failDone = menu.feedGyroCalibration(noisyLast);
    ASSERT_FALSE(failDone);
    ASSERT_FALSE(menu.isGyroCalibrating());
    ASSERT_FALSE(menu.gyroCalSucceeded());
    ASSERT_TRUE(menu.gyroCalFailed());

    menu.closeGyroDialog();
    ASSERT_FALSE(menu.isGyroDialogOpen());

    std::printf("[PASS] StreamMenu & UI State Transitions verified successfully.\n");
}

int main() {
    std::printf("=================================================================\n");
    std::printf("Milestone 4 (Bloco D) Empirical Adversarial Verification Harness\n");
    std::printf("=================================================================\n");

    testCalibratorLifecycle();
    testSettingsPersistenceAndShadowing();
    testStreamMenuTransitions();

    std::printf("=================================================================\n");
    std::printf("ALL EMPIRICAL ADVERSARIAL STRESS TESTS PASSED SUCCESSFULLY!\n");
    std::printf("=================================================================\n");
    return 0;
}
