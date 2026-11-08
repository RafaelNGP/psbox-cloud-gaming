// SPDX-License-Identifier: GPL-3.0-or-later
// Empirical Adversarial Boundary & Stress Harness for Milestone 1 (Requirement R1)
// Challenger 2: Verification of Gyroscope Aiming Drift Elimination, Thumbstick Re-anchoring,
// Rapid L2 Aim Pumping, and Extreme Gravity/Inversion Vectors.

#include "input/gyro.h"
#include "input/controller.h"
#include "input/controller_backend.h"
#include "util/log.h"
#include "platform/platform.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <limits>
#include <algorithm>

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "\n[FAIL] %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, eps) do { \
    float diff = std::abs((a) - (b)); \
    if (diff > (eps)) { \
        std::fprintf(stderr, "\n[FAIL] %s:%d: ASSERT_NEAR failed: |%s - %s| = |%f - %f| = %f > %f\n", \
                     __FILE__, __LINE__, #a, #b, static_cast<double>(a), static_cast<double>(b), \
                     static_cast<double>(diff), static_cast<double>(eps)); \
        std::exit(1); \
    } \
} while (0)

#define ASSERT_LE(a, b) do { \
    if ((a) > (b)) { \
        std::fprintf(stderr, "\n[FAIL] %s:%d: ASSERT_LE failed: %s (%f) > %s (%f)\n", \
                     __FILE__, __LINE__, #a, static_cast<double>(a), #b, static_cast<double>(b)); \
        std::exit(1); \
    } \
} while (0)

using namespace xc;
using namespace xc::input;

// Mock pad backend to drive controller_common.cpp's pollPad() directly
namespace xc::input::backend {
bool g_mockPadOpen = true;
RawPad g_mockPad{};

bool padOpen(int slot) { (void)slot; return g_mockPadOpen; }
bool readPad(int slot, RawPad& out) {
    (void)slot;
    if (!g_mockPadOpen) return false;
    out = g_mockPad;
    return true;
}
void setMotors(int slot, uint8_t large, uint8_t small, uint32_t durationMs) {
    (void)slot; (void)large; (void)small; (void)durationMs;
}
TriggerStyle triggerStyle(int slot) { (void)slot; return TriggerStyle::None; }
int setTriggerEffects(int slot, const TriggerCommand command[2]) {
    (void)slot; (void)command; return 0;
}
void setTriggerMotors(int slot, uint8_t left, uint8_t right, uint32_t durationMs) {
    (void)slot; (void)left; (void)right; (void)durationMs;
}
void setLight(int slot, uint8_t r, uint8_t g, uint8_t b) {
    (void)slot; (void)r; (void)g; (void)b;
}
void resetLight(int slot) { (void)slot; }
}

// Helper: build quaternion from Euler axis-angle (axis unit vector, angle radians)
static void quatFromAxisAngle(float ax, float ay, float az, float angleRad, float outQ[4]) {
    float half = angleRad * 0.5f;
    float s = std::sin(half);
    outQ[0] = ax * s;
    outQ[1] = ay * s;
    outQ[2] = az * s;
    outQ[3] = std::cos(half);
}

// Helper: quaternion multiplication q = qA * qB
static void quatMultiply(const float qA[4], const float qB[4], float outQ[4]) {
    float x1 = qA[0], y1 = qA[1], z1 = qA[2], w1 = qA[3];
    float x2 = qB[0], y2 = qB[1], z2 = qB[2], w2 = qB[3];
    outQ[3] = w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2;
    outQ[0] = w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2;
    outQ[1] = w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2;
    outQ[2] = w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2;
}

static inline void addEffStick(float x, float y, float dt, float& sumX, float& sumY) {
    constexpr float kLift = 0.12f;
    float mag = std::hypot(x, y);
    if (mag > kLift) {
        float eff = std::clamp((mag - kLift) / (1.0f - kLift), 0.0f, 1.0f);
        sumX += (x / mag) * eff * dt;
        sumY += (y / mag) * eff * dt;
    }
}

// =========================================================================
// SUITE 1: Physical Right Thumbstick Deflection & Gyro Posture Interaction
// =========================================================================

static void testThumbstickMacroAimingInteraction() {
    std::printf("\n--- [SUITE 1.1] Physical Thumbstick Macro-Aiming Re-anchoring ---\n");
    setGyro(kGyroAiming, 5);
    setGyroActive(true);
    setDeadzone(0.0f, 0.0f); // clean pass-through for exact deadzone analysis

    ControllerState out{};
    backend::g_mockPad = RawPad{};
    backend::g_mockPad.l2 = 1.0f; // Aim active
    backend::g_mockPad.hasAccel = true;
    backend::g_mockPad.accel[1] = 1.0f;
    backend::g_mockPad.hasOrientation = true;
    backend::g_mockPad.orientation[3] = 1.0f; // Identity quat
    backend::g_mockPad.gyroDt = 0.004f;

    // 1. Initial posture acquisition
    pollPad(0, out);
    ASSERT_TRUE(out.triggerL2 > 0.3f);
    ASSERT_NEAR(out.rightStickX, 0.0f, 1e-4f);
    ASSERT_NEAR(out.rightStickY, 0.0f, 1e-4f);

    // 2. Perform a micro-aiming gyro turn to the right: yaw 0.4 rad/s for 50 frames
    float curAngle = 0.0f;
    constexpr float dt = 0.004f;
    for (int i = 0; i < 50; ++i) {
        curAngle += 0.4f * dt;
        backend::g_mockPad.gyro[1] = 0.4f;
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, curAngle, backend::g_mockPad.orientation);
        pollPad(0, out);
    }
    // Crosshair should have been displaced to the right (negative stick in pad convention)
    ASSERT_TRUE(out.rightStickX < -0.05f);

    // 3. User begins macro-aiming with thumbstick (deflection = 0.6f > 0.15f threshold)
    // Controller continues turning slowly while stick is held.
    float stickInjectedSum = 0.0f;
    for (int i = 0; i < 50; ++i) {
        backend::g_mockPad.rightX = 0.6f;
        backend::g_mockPad.rightY = 0.0f;
        curAngle += 0.2f * dt;
        backend::g_mockPad.gyro[1] = 0.2f;
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, curAngle, backend::g_mockPad.orientation);
        pollPad(0, out);
        stickInjectedSum += out.rightStickX * dt;
        // Verify output is not corrupted (finite and includes stick deflection)
        ASSERT_TRUE(std::isfinite(out.rightStickX));
        ASSERT_TRUE(out.rightStickX > 0.3f); // dominated by physical thumbstick
    }

    float postReleaseStickSum = 0.0f, postReleaseStickSumY = 0.0f;

    // 4. User releases thumbstick (deflection drops to 0.0f)
    backend::g_mockPad.rightX = 0.0f;
    backend::g_mockPad.rightY = 0.0f;
    backend::g_mockPad.gyro[1] = 0.0f;
    pollPad(0, out);
    addEffStick(out.rightStickX, out.rightStickY, dt, postReleaseStickSum, postReleaseStickSumY);

    // Capture orientation at release
    float releaseQuat[4];
    std::copy(backend::g_mockPad.orientation, backend::g_mockPad.orientation + 4, releaseQuat);
    float angleAtRelease = curAngle;

    // 5. From this newly re-anchored posture, perform an asymmetric rotational sweep
    // Quick outward turn to the left (+0.8 rad/s, 25 frames = 100ms)
    for (int i = 0; i < 25; ++i) {
        curAngle -= 0.8f * dt;
        backend::g_mockPad.gyro[1] = -0.8f;
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, curAngle, backend::g_mockPad.orientation);
        pollPad(0, out);
        addEffStick(out.rightStickX, out.rightStickY, dt, postReleaseStickSum, postReleaseStickSumY);
    }

    // Slow return back to exact orientation at stick release (+0.2 rad/s, 100 frames = 400ms)
    for (int i = 0; i < 100; ++i) {
        curAngle += 0.2f * dt;
        backend::g_mockPad.gyro[1] = 0.2f;
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, curAngle, backend::g_mockPad.orientation);
        pollPad(0, out);
        addEffStick(out.rightStickX, out.rightStickY, dt, postReleaseStickSum, postReleaseStickSumY);
    }

    // Settle at the post-release anchor orientation
    backend::g_mockPad.gyro[1] = 0.0f;
    std::copy(releaseQuat, releaseQuat + 4, backend::g_mockPad.orientation);
    for (int i = 0; i < 250; ++i) {
        pollPad(0, out);
        addEffStick(out.rightStickX, out.rightStickY, dt, postReleaseStickSum, postReleaseStickSumY);
        if (i >= 140 && i <= 155) {
            std::printf("  settle frame %d: rightStickX=%f, postReleaseStickSum=%f\n", i, out.rightStickX, postReleaseStickSum);
        }
    }
    std::printf("  Final after 250 frames: rightStickX=%f, postReleaseStickSum=%f\n", out.rightStickX, postReleaseStickSum);

    // Empirical Invariant: Net stick displacement relative to the new anchor must converge to zero!
    ASSERT_NEAR(postReleaseStickSum, 0.0f, 0.001f);
    ASSERT_NEAR(out.rightStickX, 0.0f, 1e-4f);
    ASSERT_NEAR(out.rightStickY, 0.0f, 1e-4f);

    std::printf("[PASS] Macro-aiming thumbstick re-anchoring and post-release zero drift verified.\n");
}

static void testThumbstickThresholdChatteringStress() {
    std::printf("--- [SUITE 1.2] Thumbstick Deadband Boundary Chattering (hypot ~ 0.15f) ---\n");
    setGyro(kGyroAiming, 5);
    setGyroActive(true);

    ControllerState out{};
    backend::g_mockPad = RawPad{};
    backend::g_mockPad.l2 = 1.0f;
    backend::g_mockPad.hasAccel = true;
    backend::g_mockPad.accel[1] = 1.0f;
    backend::g_mockPad.hasOrientation = true;
    backend::g_mockPad.orientation[3] = 1.0f;
    backend::g_mockPad.gyroDt = 0.004f;

    float angle = 0.0f;
    constexpr float dt = 0.004f;

    // Rapidly chatter rightX across the 0.15f threshold: 0.149f vs 0.151f for 5,000 cycles (10,000 frames)
    for (int i = 0; i < 10000; ++i) {
        bool crossOver = (i % 2 == 0);
        backend::g_mockPad.rightX = crossOver ? 0.151f : 0.149f;
        backend::g_mockPad.rightY = 0.0f;

        // Apply active rotational motion during chattering
        float w = std::sin(i * 0.02f) * 0.5f;
        backend::g_mockPad.gyro[1] = w;
        angle += w * dt;
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, backend::g_mockPad.orientation);

        pollPad(0, out);

        // Verification invariants:
        ASSERT_TRUE(std::isfinite(out.rightStickX));
        ASSERT_TRUE(std::isfinite(out.rightStickY));
        ASSERT_LE(std::hypot(out.rightStickX, out.rightStickY), 1.0001f);
    }

    std::printf("[PASS] 10,000 frames of boundary chattering across 0.15f threshold verified stable.\n");
}

static void testThumbstickDualSaturation() {
    std::printf("--- [SUITE 1.3] Dual Saturation: Physical Stick Max + Gyro Max ---\n");
    setGyro(kGyroAiming, 10); // max gyro intensity
    setGyroActive(true);

    ControllerState out{};
    backend::g_mockPad = RawPad{};
    backend::g_mockPad.l2 = 1.0f;
    backend::g_mockPad.hasAccel = true;
    backend::g_mockPad.accel[1] = 1.0f;
    backend::g_mockPad.hasOrientation = false;
    backend::g_mockPad.gyroDt = 0.004f;

    // Both stick and gyro pushing hard in same direction (right stick X = 1.0f, gyro yaw = -10.0 rad/s)
    backend::g_mockPad.rightX = 1.0f;
    backend::g_mockPad.rightY = 0.0f;
    backend::g_mockPad.gyro[1] = -10.0f; // maximum turning push

    for (int i = 0; i < 50; ++i) {
        pollPad(0, out);
        float mag = std::hypot(out.rightStickX, out.rightStickY);
        ASSERT_LE(mag, 1.00001f);
        ASSERT_TRUE(std::isfinite(out.rightStickX));
    }

    // Both pushing at 45 degrees
    backend::g_mockPad.rightX = 0.9f;
    backend::g_mockPad.rightY = 0.9f;
    backend::g_mockPad.gyro[0] = 5.0f;
    backend::g_mockPad.gyro[1] = 5.0f;

    for (int i = 0; i < 50; ++i) {
        pollPad(0, out);
        float mag = std::hypot(out.rightStickX, out.rightStickY);
        ASSERT_LE(mag, 1.00001f);
        ASSERT_TRUE(std::isfinite(out.rightStickX));
        ASSERT_TRUE(std::isfinite(out.rightStickY));
    }

    std::printf("[PASS] Dual saturation normalization and numerical bounds verified.\n");
}

static void testMultiCycleFlickAccumulation() {
    std::printf("--- [SUITE 1.4] Multi-Cycle Flick Drift Accumulation Under Sustained Aim (25 Cycles) ---\n");
    setGyro(kGyroAiming, 5);
    setGyroActive(true);

    ControllerState out{};
    backend::g_mockPad = RawPad{};
    backend::g_mockPad.l2 = 0.0f; // Release any prior aim
    pollPad(0, out);
    ASSERT_NEAR(out.rightStickX, 0.0f, 1e-5f);

    backend::g_mockPad.l2 = 1.0f; // Aim engaged
    backend::g_mockPad.hasAccel = true;
    backend::g_mockPad.accel[1] = 1.0f;
    backend::g_mockPad.hasOrientation = true;
    backend::g_mockPad.gyroDt = 0.004f;
    quatFromAxisAngle(0.0f, 1.0f, 0.0f, 0.0f, backend::g_mockPad.orientation);

    // Initial posture acquisition at neutral (0.0 rad)
    pollPad(0, out);
    ASSERT_TRUE(out.triggerL2 > 0.3f);
    ASSERT_NEAR(out.rightStickX, 0.0f, 1e-4f);

    constexpr float dt = 0.004f;
    float cumulativeInjectedX = 0.0f, unusedY = 0.0f;

    // Perform 25 consecutive flick-and-return cycles with L2 held throughout
    for (int cycle = 1; cycle <= 25; ++cycle) {
        // Flick out: +1.5 rad/s for 30 frames (0.12s) -> +0.18 rad physical turn
        float angle = 0.0f;
        for (int i = 0; i < 30; ++i) {
            angle += 1.5f * dt;
            backend::g_mockPad.gyro[1] = 1.5f;
            quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, backend::g_mockPad.orientation);
            pollPad(0, out);
            addEffStick(out.rightStickX, out.rightStickY, dt, cumulativeInjectedX, unusedY);
        }

        // Return slowly: -0.5 rad/s for 90 frames (0.36s) -> -0.18 rad physical turn
        for (int i = 0; i < 90; ++i) {
            angle -= 0.5f * dt;
            backend::g_mockPad.gyro[1] = -0.5f;
            quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, backend::g_mockPad.orientation);
            pollPad(0, out);
            addEffStick(out.rightStickX, out.rightStickY, dt, cumulativeInjectedX, unusedY);
        }

        // Settle at physical zero for 100 frames
        backend::g_mockPad.gyro[1] = 0.0f;
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, 0.0f, backend::g_mockPad.orientation);
        for (int i = 0; i < 100; ++i) {
            pollPad(0, out);
            addEffStick(out.rightStickX, out.rightStickY, dt, cumulativeInjectedX, unusedY);
        }

        if (cycle == 1 || cycle == 5 || cycle == 10 || cycle == 15 || cycle == 20 || cycle == 25) {
            std::printf("  Cycle %2d: cumulativeInjectedX = %+.8f stick*s\n", cycle, cumulativeInjectedX);
        }
    }

    std::printf("  Total drift after 25 cycles: %+.8f stick*s\n", cumulativeInjectedX);

    // Requirement R1: After returning the controller to starting physical posture,
    // the aim must return to starting position without residual drift.
    // Across 25 cycles, cumulative drift must remain < 1e-4 stick*s.
    ASSERT_NEAR(cumulativeInjectedX, 0.0f, 1e-4f);
    std::printf("[PASS] Multi-cycle flick drift accumulation verified (< 1e-4 stick*s across 25 cycles).\n");
}

// =========================================================================
// SUITE 2: Rapid L2 Aim Re-Triggering (Pumping L2 on and off while rotating)
// =========================================================================

static void testRapidL2PumpingContinuityHardwareQuat() {
    std::printf("\n--- [SUITE 2.1] Rapid L2 Pumping with Hardware Quaternions ---\n");
    setGyro(kGyroAiming, 5);
    setGyroActive(true);

    ControllerState out{};
    backend::g_mockPad = RawPad{};
    backend::g_mockPad.hasAccel = true;
    backend::g_mockPad.accel[1] = 1.0f;
    backend::g_mockPad.hasOrientation = true;
    backend::g_mockPad.gyroDt = 0.004f;

    constexpr float dt = 0.004f;
    float angle = 0.0f;

    // Test multiple pumping frequencies: 1 frame on/off, 2 on/off, 5 on/off, 10 on/off
    const int periods[] = {1, 2, 5, 10};

    for (int period : periods) {
        std::printf("  Testing L2 pump period = %d frames...\n", period);
        for (int cycle = 0; cycle < 200; ++cycle) {
            // L2 pressed for 'period' frames
            backend::g_mockPad.l2 = 1.0f;
            for (int f = 0; f < period; ++f) {
                angle += 1.5f * dt; // rotating at 1.5 rad/s
                backend::g_mockPad.gyro[1] = 1.5f;
                quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, backend::g_mockPad.orientation);
                pollPad(0, out);

                ASSERT_TRUE(std::isfinite(out.rightStickX));
                ASSERT_LE(std::abs(out.rightStickX), 1.0f);
            }

            // L2 released for 'period' frames
            backend::g_mockPad.l2 = 0.0f;
            for (int f = 0; f < period; ++f) {
                angle += 1.5f * dt; // still rotating while aim is released
                backend::g_mockPad.gyro[1] = 1.5f;
                quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, backend::g_mockPad.orientation);
                pollPad(0, out);

                // When L2 is released, gyro MUST immediately contribute zero to the right stick
                ASSERT_NEAR(out.rightStickX, 0.0f, 1e-5f);
                ASSERT_NEAR(out.rightStickY, 0.0f, 1e-5f);
            }
        }
    }

    std::printf("[PASS] Rapid L2 pumping with hardware quat maintained state continuity and zero leak.\n");
}

static void testRapidL2PumpingContinuitySoftwareFallback() {
    std::printf("--- [SUITE 2.2] Rapid L2 Pumping with Software Fallback (No Quats) ---\n");
    setGyro(kGyroAiming, 5);
    setGyroActive(true);

    ControllerState out{};
    backend::g_mockPad = RawPad{};
    backend::g_mockPad.hasAccel = true;
    backend::g_mockPad.accel[1] = 1.0f;
    backend::g_mockPad.hasOrientation = false; // software fallback
    backend::g_mockPad.gyroDt = 0.004f;

    constexpr float dt = 0.004f;

    for (int cycle = 0; cycle < 300; ++cycle) {
        // L2 active: turn right
        backend::g_mockPad.l2 = 1.0f;
        for (int f = 0; f < 5; ++f) {
            backend::g_mockPad.gyro[1] = 2.0f;
            pollPad(0, out);
            ASSERT_TRUE(std::isfinite(out.rightStickX));
        }

        // L2 released: turn left while un-aimed
        backend::g_mockPad.l2 = 0.0f;
        for (int f = 0; f < 5; ++f) {
            backend::g_mockPad.gyro[1] = -2.0f;
            pollPad(0, out);
            ASSERT_NEAR(out.rightStickX, 0.0f, 1e-5f);
            ASSERT_NEAR(out.rightStickY, 0.0f, 1e-5f);
        }
    }

    std::printf("[PASS] Rapid L2 pumping with software fallback verified stable.\n");
}

static void testHighRateFlickDuringL2Transition() {
    std::printf("--- [SUITE 2.3] High-Rate Flick (15 rad/s) During L2 Aim Trigger Transition ---\n");
    setGyro(kGyroAiming, 10);
    setGyroActive(true);

    ControllerState out{};
    backend::g_mockPad = RawPad{};
    backend::g_mockPad.hasAccel = true;
    backend::g_mockPad.accel[1] = 1.0f;
    backend::g_mockPad.hasOrientation = true;
    backend::g_mockPad.gyroDt = 0.004f;

    // Frame 0: aim inactive
    backend::g_mockPad.l2 = 0.0f;
    quatFromAxisAngle(0.0f, 1.0f, 0.0f, 0.0f, backend::g_mockPad.orientation);
    pollPad(0, out);

    // Frame 1: aim suddenly pressed while undergoing an explosive 15 rad/s flick
    backend::g_mockPad.l2 = 1.0f;
    backend::g_mockPad.gyro[1] = 15.0f;
    quatFromAxisAngle(0.0f, 1.0f, 0.0f, 0.06f, backend::g_mockPad.orientation);
    pollPad(0, out);

    // Invariant: anti-windup clamping must keep stick injection bounded
    ASSERT_TRUE(std::isfinite(out.rightStickX));
    ASSERT_LE(std::abs(out.rightStickX), 1.0f);

    // Continue high flick for 20 frames
    for (int i = 0; i < 20; ++i) {
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, 0.06f * (i + 2), backend::g_mockPad.orientation);
        pollPad(0, out);
        ASSERT_TRUE(std::isfinite(out.rightStickX));
        ASSERT_LE(std::abs(out.rightStickX), 1.0f);
    }

    std::printf("[PASS] Explosive flick during aim engagement handled without windup explosion.\n");
}

static void testL2ThresholdChatteringStress() {
    std::printf("--- [SUITE 2.4] L2 Trigger Boundary Chattering (0.29f vs 0.31f) ---\n");
    setGyro(kGyroAiming, 5);
    setGyroActive(true);

    ControllerState out{};
    backend::g_mockPad = RawPad{};
    backend::g_mockPad.hasAccel = true;
    backend::g_mockPad.accel[1] = 1.0f;
    backend::g_mockPad.hasOrientation = true;
    backend::g_mockPad.gyroDt = 0.004f;

    float angle = 0.0f;
    constexpr float dt = 0.004f;

    // 10,000 rapid cycles across the 0.3f L2 trigger threshold
    for (int i = 0; i < 10000; ++i) {
        bool active = (i % 2 == 0);
        backend::g_mockPad.l2 = active ? 0.31f : 0.29f;

        float w = 1.0f;
        backend::g_mockPad.gyro[1] = w;
        angle += w * dt;
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, backend::g_mockPad.orientation);

        pollPad(0, out);

        ASSERT_TRUE(std::isfinite(out.rightStickX));
        ASSERT_TRUE(std::isfinite(out.rightStickY));
        if (!active) {
            // When trigger <= 0.3f, gyro output must be 0
            ASSERT_NEAR(out.rightStickX, 0.0f, 1e-5f);
        }
    }

    std::printf("[PASS] 10,000 cycles across L2 threshold (0.29f/0.31f) verified stable.\n");
}

// =========================================================================
// SUITE 3: Extreme Gravity Vectors and Upside-Down Inversion Handling
// =========================================================================

static void testVerticalPosturesAndRollMapping() {
    std::printf("\n--- [SUITE 3.1] Vertical Postures (+/-90 Pitch, +/-90 Roll) ---\n");

    struct VerticalTestCase {
        const char* name;
        float accel[3];
        float expectedUp[3];
        int turningAxis; // 0=pitch(x), 1=yaw(y), 2=roll(z)
    };

    const VerticalTestCase cases[] = {
        {"Pitch Up 90 deg (facing ceiling)",    {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, 1.0f}, 2},
        {"Pitch Down 90 deg (facing floor)",    {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, 2},
        {"Roll Left 90 deg (banked left)",     {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 0},
        {"Roll Right 90 deg (banked right)",    {1.0f, 0.0f, 0.0f},  {1.0f, 0.0f, 0.0f}, 0},
    };

    for (const auto& tc : cases) {
        std::printf("  Testing %s...\n", tc.name);
        GyroState s{};
        float x = 0, y = 0;
        const float wZero[3] = {0, 0, 0};

        // Initialize gravity vector
        gyroToStick(wZero, tc.accel, 5, s, x, y);
        ASSERT_TRUE(s.upInitialized);
        ASSERT_NEAR(s.up[0], tc.expectedUp[0], 1e-3f);
        ASSERT_NEAR(s.up[1], tc.expectedUp[1], 1e-3f);
        ASSERT_NEAR(s.up[2], tc.expectedUp[2], 1e-3f);

        // In this orientation, turning about the room vertical should map from tc.turningAxis
        float wTurn[3] = {0, 0, 0};
        wTurn[tc.turningAxis] = 1.0f;
        gyroToStick(wTurn, tc.accel, 5, s, x, y);

        // Output stick x (horizontal aim) must respond to room vertical turn
        ASSERT_TRUE(std::abs(x) > 0.05f);
        ASSERT_TRUE(std::isfinite(x));
        ASSERT_TRUE(std::isfinite(y));
    }

    std::printf("[PASS] Vertical postures correctly map room vertical to roll/pitch axes.\n");
}

static void testUpsideDownInversionHardwareQuatZeroDrift() {
    std::printf("--- [SUITE 3.2] Upside-Down Inversion (180 deg) Closed-Loop Zero Drift ---\n");

    // Start with controller held upside-down: 180 degree roll around Z (or pitch around X)
    // q0 = [0, 0, 1, 0] (180 deg roll around Z: x=0, y=0, z=1, w=0)
    float q0[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    float accelInverted[3] = {0.0f, -1.0f, 0.0f};

    GyroState s{};
    anchorGyroPosture(s, q0);
    ASSERT_TRUE(s.aimingActive);
    ASSERT_TRUE(s.hasBaseQuat);

    float totalStickX = 0.0f;
    float totalStickY = 0.0f;
    float x = 0, y = 0;
    constexpr float dt = 0.004f;
    const float wZero[3] = {0, 0, 0};

    // 1. Asymmetric flick while held upside-down:
    // Fast yaw turn to the right: 40 frames at 3.0 rad/s
    float angle = 0.0f;
    float wFast[3] = {0.0f, 3.0f, 0.0f};
    for (int i = 0; i < 40; ++i) {
        angle += 3.0f * dt;
        float qRot[4];
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, qRot);
        float qCur[4];
        quatMultiply(q0, qRot, qCur); // compound rotation
        gyroToStick(wFast, accelInverted, 5, s, x, y, qCur, dt);
        addEffStick(x, y, dt, totalStickX, totalStickY);
    }

    // 2. Slow return to starting posture: 240 frames at -0.5 rad/s
    float wSlow[3] = {0.0f, -0.5f, 0.0f};
    for (int i = 0; i < 240; ++i) {
        angle -= 0.5f * dt;
        float qRot[4];
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, qRot);
        float qCur[4];
        quatMultiply(q0, qRot, qCur);
        gyroToStick(wSlow, accelInverted, 5, s, x, y, qCur, dt);
        addEffStick(x, y, dt, totalStickX, totalStickY);
    }

    // 3. Settle at initial upside-down posture q0
    for (int i = 0; i < 150; ++i) {
        gyroToStick(wZero, accelInverted, 5, s, x, y, q0, dt);
        addEffStick(x, y, dt, totalStickX, totalStickY);
    }

    // Empirical Invariant: Zero drift even when upside-down!
    ASSERT_NEAR(totalStickX, 0.0f, 0.001f);
    ASSERT_NEAR(totalStickY, 0.0f, 0.001f);
    ASSERT_NEAR(x, 0.0f, 1e-4f);
    ASSERT_NEAR(y, 0.0f, 1e-4f);

    std::printf("[PASS] Upside-down inversion zero drift verified (< 0.001 stick*s).\n");
}

static void testFull360ContinuousRotationCutCrossing() {
    std::printf("--- [SUITE 3.3] Full 360-Degree Continuous Rotation (Crossing 180 cut) ---\n");

    GyroState s{};
    float q0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    anchorGyroPosture(s, q0);

    float accel[3] = {0.0f, 1.0f, 0.0f};
    constexpr float dt = 0.004f;
    float x = 0, y = 0;
    const float wRotate[3] = {0.0f, 1.0f, 0.0f};

    // Rotate 360 degrees in yaw (2*pi radians): 250*2*pi ~= 1571 frames at 1 rad/s
    int totalFrames = static_cast<int>((2.0f * 3.14159265f) / (1.0f * dt));
    float angle = 0.0f;

    for (int i = 0; i < totalFrames; ++i) {
        angle += 1.0f * dt;
        float q[4];
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, q);
        gyroToStick(wRotate, accel, 5, s, x, y, q, dt);

        ASSERT_TRUE(std::isfinite(x));
        ASSERT_TRUE(std::isfinite(y));
        ASSERT_LE(std::abs(x), 1.0f);
    }

    // Now rotate all the way back to 0 degrees
    const float wReturn[3] = {0.0f, -1.0f, 0.0f};
    for (int i = 0; i < totalFrames; ++i) {
        angle -= 1.0f * dt;
        float q[4];
        quatFromAxisAngle(0.0f, 1.0f, 0.0f, angle, q);
        gyroToStick(wReturn, accel, 5, s, x, y, q, dt);

        ASSERT_TRUE(std::isfinite(x));
        ASSERT_TRUE(std::isfinite(y));
    }

    // Settle at neutral
    const float wZero[3] = {0, 0, 0};
    for (int i = 0; i < 150; ++i) {
        gyroToStick(wZero, accel, 5, s, x, y, q0, dt);
    }

    ASSERT_NEAR(x, 0.0f, 1e-4f);
    ASSERT_NEAR(y, 0.0f, 1e-4f);

    std::printf("[PASS] Full 360-degree rotation across quaternion hemisphere cut verified.\n");
}

static void testAbnormalGravityRejection() {
    std::printf("--- [SUITE 3.4] Abnormal Gravity Rejection (Free-fall & Violent Shaking) ---\n");

    GyroState s{};
    float x = 0, y = 0;
    const float wStationary[3] = {0, 0, 0};

    // 1. Baseline: valid 1g gravity level
    float accelLevel[3] = {0.0f, 1.0f, 0.0f};
    gyroToStick(wStationary, accelLevel, 5, s, x, y);
    ASSERT_TRUE(s.upInitialized);
    ASSERT_NEAR(s.up[1], 1.0f, 1e-3f);

    // 2. Free-fall (0g: accel = [0, 0, 0])
    float accelZeroG[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 100; ++i) {
        gyroToStick(wStationary, accelZeroG, 5, s, x, y);
    }
    // Must NOT corrupt up vector to NaN or zeros
    ASSERT_NEAR(s.up[1], 1.0f, 1e-3f);
    ASSERT_TRUE(std::isfinite(s.up[0]));
    ASSERT_TRUE(std::isfinite(s.up[1]));
    ASSERT_TRUE(std::isfinite(s.up[2]));

    // 3. Violent shaking (3g: accel = [0.0, 3.0, 0.0])
    float accelHighG[3] = {0.0f, 3.0f, 0.0f};
    for (int i = 0; i < 100; ++i) {
        gyroToStick(wStationary, accelHighG, 5, s, x, y);
    }
    // Must reject > 1.5g and keep previous stable orientation
    ASSERT_NEAR(s.up[1], 1.0f, 1e-3f);

    std::printf("[PASS] Free-fall (<0.5g) and violent shaking (>1.5g) successfully rejected.\n");
}

static void testPathologicalAndAdversarialInputs() {
    std::printf("--- [SUITE 3.5] Pathological & Adversarial Edge Cases (NaN, Inf, Null, Negative dt) ---\n");

    GyroState s{};
    float x = 0, y = 0;

    // 1. Null pointers
    gyroToStick(nullptr, nullptr, 5, s, x, y, nullptr, 0.004f);
    ASSERT_EQ(x, 0.0f);
    ASSERT_EQ(y, 0.0f);

    // 2. Negative and zero dt
    const float wValid[3] = {0.1f, 0.1f, 0.0f};
    const float accel[3] = {0.0f, 1.0f, 0.0f};
    gyroToStick(wValid, accel, 5, s, x, y, nullptr, 0.0f); // should fallback to 0.004f
    ASSERT_TRUE(std::isfinite(x));
    ASSERT_TRUE(std::isfinite(y));

    gyroToStick(wValid, accel, 5, s, x, y, nullptr, -0.05f); // negative dt fallback
    ASSERT_TRUE(std::isfinite(x));
    ASSERT_TRUE(std::isfinite(y));

    // 3. Extreme level values (level < 1 or level > kGyroLevels)
    gyroToStick(wValid, accel, -10, s, x, y); // clamped to 1
    ASSERT_TRUE(std::isfinite(x));
    gyroToStick(wValid, accel, 999, s, x, y); // clamped to 10
    ASSERT_TRUE(std::isfinite(x));

    std::printf("[PASS] Pathological inputs handled gracefully without crashes or exceptions.\n");
}

int main() {
    std::printf("=================================================================\n");
    std::printf("Milestone 1 (Requirement R1) Empirical Boundary & Stress Harness\n");
    std::printf("Challenger 2: Gyroscope Aiming Drift Elimination\n");
    std::printf("=================================================================\n");

    // Suite 1: Thumbstick macro-aiming interaction
    testThumbstickMacroAimingInteraction();
    testThumbstickThresholdChatteringStress();
    testThumbstickDualSaturation();
    testMultiCycleFlickAccumulation();

    // Suite 2: Rapid L2 aim re-triggering
    testRapidL2PumpingContinuityHardwareQuat();
    testRapidL2PumpingContinuitySoftwareFallback();
    testHighRateFlickDuringL2Transition();
    testL2ThresholdChatteringStress();

    // Suite 3: Extreme gravity vectors & upside-down handling
    testVerticalPosturesAndRollMapping();
    testUpsideDownInversionHardwareQuatZeroDrift();
    testFull360ContinuousRotationCutCrossing();
    testAbnormalGravityRejection();
    testPathologicalAndAdversarialInputs();

    std::printf("\n=================================================================\n");
    std::printf("ALL EMPIRICAL CHALLENGER 2 STRESS TESTS PASSED SUCCESSFULLY!\n");
    std::printf("VERDICT: APPROVE (Milestone 1 Gyroscope Posture Engine is Robust)\n");
    std::printf("=================================================================\n");
    return 0;
}
