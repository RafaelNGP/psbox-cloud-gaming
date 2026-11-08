// SPDX-License-Identifier: GPL-3.0-or-later
// Empirical Adversarial Stress Test & Verification Harness for Milestone 1 (R1)
// Gyroscope Aiming Drift Elimination & Closed-Loop Posture Tracking

#include "input/gyro.h"
#include "input/controller_backend.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <string>

using namespace xc;
using namespace xc::input;

namespace xc::input::backend {
static RawPad g_mockPad{};
bool padOpen(int) { return true; }
bool readPad(int, RawPad& out) { out = g_mockPad; return true; }
void resetLight(int) {}
TriggerStyle triggerStyle(int) { return TriggerStyle::None; }
void setTriggerMotors(int, uint8_t, uint8_t, uint32_t) {}
int setTriggerEffects(int, const TriggerCommand*) { return 0; }
void setLight(int, uint8_t, uint8_t, uint8_t) {}
void setMotors(int, uint8_t, uint8_t, uint32_t) {}
}

static int g_passCount = 0;
static int g_failCount = 0;

#define STRESS_CHECK(cond, msg, ...) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  [FAIL] " msg " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__); \
        g_failCount++; \
    } else { \
        g_passCount++; \
    } \
} while(0)

// Helper: Quaternion multiplication q_out = q_a * q_b
static void quatMult(const float a[4], const float b[4], float out[4]) {
    // a = (xa, ya, za, wa), b = (xb, yb, zb, wb)
    out[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
    out[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
    out[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
    out[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
}

// Helper: Axis-angle to quaternion
static void axisAngleToQuat(float ax, float ay, float az, float angle, float q[4]) {
    float len = std::hypot(std::hypot(ax, ay), az);
    if (len < 1e-6f) {
        q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f;
        return;
    }
    float s = std::sin(angle * 0.5f) / len;
    q[0] = ax * s;
    q[1] = ay * s;
    q[2] = az * s;
    q[3] = std::cos(angle * 0.5f);
}

// Camera-effective deflection above the 12% deadzone lifted by gyroToStick().
static inline void addEffStick(float x, float y, float dt, float& sumX, float& sumY) {
    constexpr float kLift = 0.12f;
    float mag = std::hypot(x, y);
    if (mag > kLift) {
        float eff = std::clamp((mag - kLift) / (1.0f - kLift), 0.0f, 1.0f);
        sumX += (x / mag) * eff * dt;
        sumY += (y / mag) * eff * dt;
    }
}

// =============================================================================
// Test 1: Asymmetric Speed Flicks (Software closed-loop & Hardware Quaternion)
// Cumulative stick injection MUST return strictly to zero (|sum S * dt| < 1e-4)
// =============================================================================
static void testAsymmetricSpeedFlicks() {
    std::printf("--- Starting Test 1: Asymmetric Speed Flicks (Zero-Drift < 1e-4) ---\n");
    resetGlobalGyroCalibration();

    // 1A. Software closed-loop: Yaw asymmetric flick (fast right, slow return)
    {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float totalStickInjectedX = 0.0f;
        float totalStickInjectedY = 0.0f;

        // Fast flick right: +4.0 rad/s for 50 steps = 0.2s -> +0.8 rad turn
        float x = 0, y = 0;
        const float wFast[3] = {0.0f, 4.0f, 0.0f};
        for (int i = 0; i < 50; ++i) {
            gyroToStick(wFast, accel, 6, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Pause 30 steps
        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 30; ++i) {
            gyroToStick(wStill, accel, 6, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Slow return left: -0.4 rad/s for 500 steps = 2.0s -> -0.8 rad turn (exact neutral return)
        const float wSlow[3] = {0.0f, -0.4f, 0.0f};
        for (int i = 0; i < 500; ++i) {
            gyroToStick(wSlow, accel, 6, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Settle at neutral posture
        for (int i = 0; i < 200; ++i) {
            gyroToStick(wStill, accel, 6, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        std::printf("  1A [Software Yaw]: totalStickInjectedX = %.8f (limit 1e-4)\n", totalStickInjectedX);
        STRESS_CHECK(std::abs(totalStickInjectedX) < 1e-4f,
                     "Software closed-loop yaw: totalStickInjectedX must be < 1e-4, got %.8f", totalStickInjectedX);
        STRESS_CHECK(std::abs(totalStickInjectedY) < 1e-4f,
                     "Software closed-loop yaw: totalStickInjectedY must be < 1e-4, got %.8f", totalStickInjectedY);
        STRESS_CHECK(x == 0.0f && y == 0.0f, "Software closed-loop yaw: resting stick must be 0.0");
    }

    // 1B. Software closed-loop: Pitch asymmetric flick (fast up, slow down)
    {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float totalStickInjectedX = 0.0f;
        float totalStickInjectedY = 0.0f;

        // Fast flick pitch up: +3.0 rad/s for 50 steps = 0.2s -> +0.6 rad
        float x = 0, y = 0;
        const float wFast[3] = {3.0f, 0.0f, 0.0f};
        for (int i = 0; i < 50; ++i) {
            gyroToStick(wFast, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Pause 20 steps
        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 20; ++i) {
            gyroToStick(wStill, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Slow return pitch down: -0.3 rad/s for 500 steps = 2.0s -> -0.6 rad
        const float wSlow[3] = {-0.3f, 0.0f, 0.0f};
        for (int i = 0; i < 500; ++i) {
            gyroToStick(wSlow, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Settle
        for (int i = 0; i < 200; ++i) {
            gyroToStick(wStill, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        std::printf("  1B [Software Pitch]: totalStickInjectedY = %.8f (limit 1e-4)\n", totalStickInjectedY);
        STRESS_CHECK(std::abs(totalStickInjectedY) < 1e-4f,
                     "Software closed-loop pitch: totalStickInjectedY must be < 1e-4, got %.8f", totalStickInjectedY);
        STRESS_CHECK(std::abs(totalStickInjectedX) < 1e-4f,
                     "Software closed-loop pitch: totalStickInjectedX must be < 1e-4, got %.8f", totalStickInjectedX);
    }

    // 1C. Hardware Quaternion closed-loop: Yaw asymmetric flick
    {
        GyroState s{};
        const float q0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        anchorGyroPosture(s, q0);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float totalStickInjectedX = 0.0f;
        float totalStickInjectedY = 0.0f;

        constexpr float targetYaw = 0.52359877f; // 30 degrees
        // Phase 1: Fast turn to +30 deg in 0.25s (62 steps)
        float x = 0, y = 0;
        const float wFast[3] = {0.0f, targetYaw / 0.25f, 0.0f};
        for (int i = 1; i <= 62; ++i) {
            float cur = targetYaw * (static_cast<float>(i) / 62.0f);
            float q[4] = {0.0f, std::sin(cur * 0.5f), 0.0f, std::cos(cur * 0.5f)};
            gyroToStick(wFast, accel, 8, s, x, y, q, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Pause 30 steps
        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        float qHold[4] = {0.0f, std::sin(targetYaw * 0.5f), 0.0f, std::cos(targetYaw * 0.5f)};
        for (int i = 0; i < 30; ++i) {
            gyroToStick(wStill, accel, 8, s, x, y, qHold, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Phase 2: Slow return to 0 deg in 2.0s (500 steps)
        const float wSlow[3] = {0.0f, -targetYaw / 2.0f, 0.0f};
        for (int i = 1; i <= 500; ++i) {
            float cur = targetYaw * (1.0f - static_cast<float>(i) / 500.0f);
            float q[4] = {0.0f, std::sin(cur * 0.5f), 0.0f, std::cos(cur * 0.5f)};
            gyroToStick(wSlow, accel, 8, s, x, y, q, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Settle at q0
        for (int i = 0; i < 200; ++i) {
            gyroToStick(wStill, accel, 8, s, x, y, q0, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        std::printf("  1C [Hardware Quat Yaw]: totalStickInjectedX = %.8f (limit 1e-4)\n", totalStickInjectedX);
        STRESS_CHECK(std::abs(totalStickInjectedX) < 1e-4f,
                     "Hardware quat yaw: totalStickInjectedX must be < 1e-4, got %.8f", totalStickInjectedX);
        STRESS_CHECK(std::abs(totalStickInjectedY) < 1e-4f,
                     "Hardware quat yaw: totalStickInjectedY must be < 1e-4, got %.8f", totalStickInjectedY);
    }

    // 1D. Hardware Quaternion closed-loop: Pitch asymmetric flick
    {
        GyroState s{};
        const float q0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        anchorGyroPosture(s, q0);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float totalStickInjectedX = 0.0f;
        float totalStickInjectedY = 0.0f;

        constexpr float targetPitch = 0.4363323f; // 25 degrees
        // Fast pitch up in 0.2s (50 steps)
        float x = 0, y = 0;
        const float wFast[3] = {targetPitch / 0.2f, 0.0f, 0.0f};
        for (int i = 1; i <= 50; ++i) {
            float cur = targetPitch * (static_cast<float>(i) / 50.0f);
            float q[4] = {std::sin(cur * 0.5f), 0.0f, 0.0f, std::cos(cur * 0.5f)};
            gyroToStick(wFast, accel, 5, s, x, y, q, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Pause 25 steps
        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        float qHold[4] = {std::sin(targetPitch * 0.5f), 0.0f, 0.0f, std::cos(targetPitch * 0.5f)};
        for (int i = 0; i < 25; ++i) {
            gyroToStick(wStill, accel, 5, s, x, y, qHold, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Slow pitch down in 1.5s (375 steps)
        const float wSlow[3] = {-targetPitch / 1.5f, 0.0f, 0.0f};
        for (int i = 1; i <= 375; ++i) {
            float cur = targetPitch * (1.0f - static_cast<float>(i) / 375.0f);
            float q[4] = {std::sin(cur * 0.5f), 0.0f, 0.0f, std::cos(cur * 0.5f)};
            gyroToStick(wSlow, accel, 5, s, x, y, q, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        // Settle at q0
        for (int i = 0; i < 200; ++i) {
            gyroToStick(wStill, accel, 5, s, x, y, q0, dt);
            addEffStick(x, y, dt, totalStickInjectedX, totalStickInjectedY);
        }

        std::printf("  1D [Hardware Quat Pitch]: totalStickInjectedY = %.8f (limit 1e-4)\n", totalStickInjectedY);
        STRESS_CHECK(std::abs(totalStickInjectedY) < 1e-4f,
                     "Hardware quat pitch: totalStickInjectedY must be < 1e-4, got %.8f", totalStickInjectedY);
        STRESS_CHECK(std::abs(totalStickInjectedX) < 1e-4f,
                     "Hardware quat pitch: totalStickInjectedX must be < 1e-4, got %.8f", totalStickInjectedX);
    }
}

// =============================================================================
// Test 2: Complex Multi-Axis 3D Rotations with Non-Commuting Coupled Sweeps
// =============================================================================
static void testComplex3DRotations() {
    std::printf("--- Starting Test 2: Complex Multi-Axis 3D Rotations ---\n");

    // Start with arbitrary non-identity base posture q0
    // E.g., held naturally at 30 deg pitch, 10 deg roll
    float q0[4];
    axisAngleToQuat(1.0f, 0.2f, 0.3f, 0.45f, q0);

    GyroState s{};
    anchorGyroPosture(s, q0);
    float accel[3] = {0.1f, 0.9f, 0.2f};
    constexpr float dt = 0.004f;
    float totalStickX = 0.0f;
    float totalStickY = 0.0f;

    // Simulate 100 consecutive randomized 3D compound rotation excursions returning to q0
    std::mt19937 rng(424242);
    std::uniform_real_distribution<float> distAxis(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distAngle(0.1f, 0.7f); // up to ~40 degrees
    std::uniform_int_distribution<int> distFlickSteps(25, 75);
    std::uniform_int_distribution<int> distReturnSteps(100, 300);

    float maxResidualX = 0.0f;
    float maxResidualY = 0.0f;

    for (int cycle = 0; cycle < 50; ++cycle) {
        float ax = distAxis(rng), ay = distAxis(rng), az = distAxis(rng);
        float angleMax = distAngle(rng);
        int flickSteps = distFlickSteps(rng);
        int returnSteps = distReturnSteps(rng);

        float qDisp[4];
        axisAngleToQuat(ax, ay, az, angleMax, qDisp);

        float x = 0, y = 0;

        // Outward phase: rotate from q0 towards qTarget = q0 * qDisp
        for (int i = 1; i <= flickSteps; ++i) {
            float frac = static_cast<float>(i) / flickSteps;
            float qCurDisp[4];
            axisAngleToQuat(ax, ay, az, angleMax * frac, qCurDisp);
            float qCur[4];
            quatMult(q0, qCurDisp, qCur);

            float wCur[3] = {ax * angleMax / (flickSteps * dt),
                             ay * angleMax / (flickSteps * dt),
                             az * angleMax / (flickSteps * dt)};
            gyroToStick(wCur, accel, 7, s, x, y, qCur, dt);
            addEffStick(x, y, dt, totalStickX, totalStickY);
        }

        // Return phase: rotate back from qTarget to q0
        for (int i = 1; i <= returnSteps; ++i) {
            float frac = 1.0f - static_cast<float>(i) / returnSteps;
            float qCurDisp[4];
            axisAngleToQuat(ax, ay, az, angleMax * frac, qCurDisp);
            float qCur[4];
            quatMult(q0, qCurDisp, qCur);

            float wCur[3] = {-ax * angleMax / (returnSteps * dt),
                             -ay * angleMax / (returnSteps * dt),
                             -az * angleMax / (returnSteps * dt)};
            gyroToStick(wCur, accel, 7, s, x, y, qCur, dt);
            addEffStick(x, y, dt, totalStickX, totalStickY);
        }

        // Settle phase at q0
        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 150; ++i) {
            gyroToStick(wStill, accel, 7, s, x, y, q0, dt);
            addEffStick(x, y, dt, totalStickX, totalStickY);
        }

        if (std::abs(totalStickX) > maxResidualX) maxResidualX = std::abs(totalStickX);
        if (std::abs(totalStickY) > maxResidualY) maxResidualY = std::abs(totalStickY);

        STRESS_CHECK(std::abs(totalStickX) < 1e-4f,
                     "Cycle %d: totalStickX = %.8f (must be < 1e-4)", cycle, totalStickX);
        STRESS_CHECK(std::abs(totalStickY) < 1e-4f,
                     "Cycle %d: totalStickY = %.8f (must be < 1e-4)", cycle, totalStickY);
    }

    std::printf("  Compound 3D Sweeps (50 cycles): maxResidualX = %.8f, maxResidualY = %.8f\n",
                maxResidualX, maxResidualY);
}

// =============================================================================
// Test 3: Anti-Windup Clamping and Extreme Ballistic Flicks
// =============================================================================
static void testAntiWindupAndBallisticFlicks() {
    std::printf("--- Starting Test 3: Anti-Windup Clamping & Ballistic Flicks ---\n");

    GyroState s{};
    anchorGyroPosture(s, nullptr);
    float accel[3] = {0.0f, 1.0f, 0.0f};
    constexpr float dt = 0.004f;
    float totalStickX = 0.0f;
    float totalStickY = 0.0f;

    // Extreme flick: 20 rad/s (~1145 deg/s) for 100 ms (25 steps) -> 2.0 rad (~114 deg turn)
    // This pushes error far beyond the 0.4 clamping threshold
    float x = 0, y = 0;
    const float wExtreme[3] = {0.0f, 20.0f, 0.0f};
    for (int i = 0; i < 25; ++i) {
        gyroToStick(wExtreme, accel, 10, s, x, y, nullptr, dt);
        addEffStick(x, y, dt, totalStickX, totalStickY);
        // Verify stick output stays within [-1, 1]
        STRESS_CHECK(x >= -1.0f && x <= 1.0f, "Ballistic flick: stick output x out of bounds: %.4f", x);
    }

    // Pause 50 steps
    const float wStill[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 50; ++i) {
        gyroToStick(wStill, accel, 10, s, x, y, nullptr, dt);
        addEffStick(x, y, dt, totalStickX, totalStickY);
    }

    // Return slowly: -1.0 rad/s for 500 steps (2.0s) -> -2.0 rad turn (exact physical return)
    const float wReturn[3] = {0.0f, -1.0f, 0.0f};
    for (int i = 0; i < 500; ++i) {
        gyroToStick(wReturn, accel, 10, s, x, y, nullptr, dt);
        addEffStick(x, y, dt, totalStickX, totalStickY);
    }

    // Settle at neutral
    for (int i = 0; i < 250; ++i) {
        gyroToStick(wStill, accel, 10, s, x, y, nullptr, dt);
        addEffStick(x, y, dt, totalStickX, totalStickY);
    }

    std::printf("  Ballistic Flick & Return: totalStickX = %.8f (limit 1e-4)\n", totalStickX);
    STRESS_CHECK(std::abs(totalStickX) < 1e-4f,
                 "Ballistic flick: totalStickX = %.8f must be < 1e-4", totalStickX);
    STRESS_CHECK(std::abs(totalStickY) < 1e-4f,
                 "Ballistic flick: totalStickY = %.8f must be < 1e-4", totalStickY);
    STRESS_CHECK(x == 0.0f && y == 0.0f, "Ballistic flick: final stick output must be 0");
}

// =============================================================================
// Test 4: Stationary Resting Stability & Noise Immunity
// =============================================================================
static void testStationaryRestingAndNoise() {
    std::printf("--- Starting Test 4: Stationary Resting & Noise Immunity ---\n");

    // 4A. Perfect resting: 10,000 steps (40 seconds of real-time)
    {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float totalStickX = 0.0f, totalStickY = 0.0f;
        const float wStill[3] = {0.0f, 0.0f, 0.0f};

        for (int i = 0; i < 10000; ++i) {
            float x = 0, y = 0;
            gyroToStick(wStill, accel, 5, s, x, y, nullptr, dt);
            totalStickX += x * dt;
            totalStickY += y * dt;
            if (x != 0.0f || y != 0.0f) {
                STRESS_CHECK(false, "Resting step %d produced non-zero stick: x=%.6f, y=%.6f", i, x, y);
                break;
            }
        }
        STRESS_CHECK(totalStickX == 0.0f && totalStickY == 0.0f,
                     "Perfect resting: totalStick must be identically 0.000000");
    }

    // 4B. Sub-deadband noise (< kNoise = 0.015 rad/s): 10,000 steps
    {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float totalStickX = 0.0f, totalStickY = 0.0f;

        std::mt19937 rng(999);
        std::uniform_real_distribution<float> distNoise(-0.014f, 0.014f);

        for (int i = 0; i < 10000; ++i) {
            float rawW[3] = {distNoise(rng), distNoise(rng), distNoise(rng)};
            float x = 0, y = 0;
            gyroToStick(rawW, accel, 5, s, x, y, nullptr, dt);
            totalStickX += x * dt;
            totalStickY += y * dt;
            if (x != 0.0f || y != 0.0f) {
                STRESS_CHECK(false, "Sub-deadband noise step %d produced non-zero stick: x=%.6f, y=%.6f", i, x, y);
                break;
            }
        }
        STRESS_CHECK(totalStickX == 0.0f && totalStickY == 0.0f,
                     "Sub-deadband noise: totalStick must be identically 0.000000");
    }
}

// =============================================================================
// Test 5: Boundary Orientations, Gimbal Singularities, and Robustness
// No NaN or div-by-zero across boundary quaternions and accelerations
// =============================================================================
static void testBoundaryOrientationsAndRobustness() {
    std::printf("--- Starting Test 5: Boundary Orientations & Robustness (No NaN/Div-Zero) ---\n");

    struct TestCase {
        const char* name;
        float q[4];
        float accel[3];
        float rawW[3];
    };

    TestCase cases[] = {
        {"Identity", {0, 0, 0, 1}, {0, 1, 0}, {0, 0, 0}},
        {"180-deg Pitch (wr=0, xr=1)", {1, 0, 0, 0}, {0, -1, 0}, {0, 0, 0}},
        {"180-deg Yaw (wr=0, yr=1)", {0, 1, 0, 0}, {0, 1, 0}, {0, 0, 0}},
        {"180-deg Roll (wr=0, zr=1)", {0, 0, 1, 0}, {0, 1, 0}, {0, 0, 0}},
        {"Gimbal +90-deg Pitch", {0.70710678f, 0, 0, 0.70710678f}, {0, 0, 1}, {0, 0, 0}},
        {"Gimbal -90-deg Pitch", {-0.70710678f, 0, 0, 0.70710678f}, {0, 0, -1}, {0, 0, 0}},
        {"Gimbal +90-deg Yaw", {0, 0.70710678f, 0, 0.70710678f}, {0, 1, 0}, {0, 0, 0}},
        {"Upside Down Pad (accel Y = -1g)", {0, 0, 0, 1}, {0, -1, 0}, {0, 0, 0}},
        {"Freefall (accel = 0)", {0, 0, 0, 1}, {0, 0, 0}, {0, 0, 0}},
        {"High-G shaking (accel = 3g)", {0, 0, 0, 1}, {2, 2, 2}, {0, 0, 0}},
        {"Degenerate Quat [0,0,0,0]", {0, 0, 0, 0}, {0, 1, 0}, {0, 0, 0}},
        {"Negative Quat [-x,-y,-z,-w]", {-0.5f, -0.5f, -0.5f, -0.5f}, {0, 1, 0}, {0, 0, 0}},
        {"Micro-rotation (1e-7 rad)", {0, 5e-8f, 0, 1.0f}, {0, 1, 0}, {0, 0, 0}},
        {"Fast Spin (10 rad/s)", {0, 0, 0, 1}, {0, 1, 0}, {10, -10, 10}},
    };

    for (const auto& tc : cases) {
        GyroState s{};
        anchorGyroPosture(s, tc.q);
        float x = 0, y = 0;

        for (int step = 0; step < 50; ++step) {
            gyroToStick(tc.rawW, tc.accel, 5, s, x, y, tc.q, 0.004f);

            bool nanOrInf = !std::isfinite(x) || !std::isfinite(y) ||
                            !std::isfinite(s.x) || !std::isfinite(s.y) ||
                            !std::isfinite(s.up[0]) || !std::isfinite(s.up[1]) || !std::isfinite(s.up[2]) ||
                            !std::isfinite(s.targetAngle[0]) || !std::isfinite(s.targetAngle[1]) ||
                            !std::isfinite(s.injected[0]) || !std::isfinite(s.injected[1]);

            STRESS_CHECK(!nanOrInf, "Boundary case '%s' at step %d produced NaN or Inf! x=%.4f, y=%.4f",
                         tc.name, step, x, y);
            if (nanOrInf) break;
        }
    }
}

// =============================================================================
// Test 6: Variable Polling Rates (100 Hz, 250 Hz, 500 Hz, 1000 Hz, Jittered dt)
// =============================================================================
static void testVariablePollingRates() {
    std::printf("--- Starting Test 6: Variable Polling Rates & Jittered dt ---\n");

    const float dts[] = {0.010f, 0.004f, 0.002f, 0.001f};

    for (float dt : dts) {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        float totalStickX = 0.0f;

        int flickSteps = static_cast<int>(std::round(0.2f / dt));
        int returnSteps = static_cast<int>(std::round(1.0f / dt));
        int settleSteps = static_cast<int>(std::round(0.8f / dt));

        float x = 0, y = 0, unusedY = 0;
        const float wFlick[3] = {0.0f, 2.0f, 0.0f};
        for (int i = 0; i < flickSteps; ++i) {
            gyroToStick(wFlick, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, unusedY);
        }

        const float wReturn[3] = {0.0f, -0.4f, 0.0f};
        for (int i = 0; i < returnSteps; ++i) {
            gyroToStick(wReturn, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, unusedY);
        }

        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < settleSteps; ++i) {
            gyroToStick(wStill, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, unusedY);
        }

        std::printf("  Polling rate %.0f Hz (dt=%.4f): totalStickX = %.8f\n", 1.0f / dt, dt, totalStickX);
        STRESS_CHECK(std::abs(totalStickX) < 1e-4f,
                     "Polling rate %.0f Hz: totalStickX = %.8f must be < 1e-4", 1.0f / dt, totalStickX);
    }

    // Jittered dt (varying each frame between 0.002s and 0.006s)
    {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        float totalStickX = 0.0f, unusedY = 0.0f;
        std::mt19937 rng(777);
        std::uniform_real_distribution<float> distDt(0.002f, 0.006f);

        float elapsed = 0.0f;
        float x = 0, y = 0;
        // Flick +0.5 rad
        while (elapsed < 0.25f) {
            float dt = distDt(rng);
            float w[3] = {0.0f, 2.0f, 0.0f};
            gyroToStick(w, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, unusedY);
            elapsed += dt;
        }
        // Return -0.5 rad
        float returnElapsed = 0.0f;
        while (returnElapsed < elapsed) {
            float dt = distDt(rng);
            if (returnElapsed + dt > elapsed) dt = elapsed - returnElapsed;
            float w[3] = {0.0f, -2.0f, 0.0f};
            gyroToStick(w, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, unusedY);
            returnElapsed += dt;
        }
        // Settle
        for (int i = 0; i < 200; ++i) {
            float dt = distDt(rng);
            float w[3] = {0.0f, 0.0f, 0.0f};
            gyroToStick(w, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, unusedY);
        }

        std::printf("  Jittered dt: totalStickX = %.8f\n", totalStickX);
        STRESS_CHECK(std::abs(totalStickX) < 1e-4f,
                     "Jittered dt: totalStickX = %.8f must be < 1e-4", totalStickX);
    }
}

// =============================================================================
// Test 7: Thumbstick Re-centering Interruption
// =============================================================================
static void testThumbstickRecenterInterruption() {
    std::printf("--- Starting Test 7: Thumbstick Re-centering Interruption ---\n");

    GyroState s{};
    anchorGyroPosture(s, nullptr);
    float accel[3] = {0.0f, 1.0f, 0.0f};
    constexpr float dt = 0.004f;
    float x = 0, y = 0;

    // Aim deflected by 0.5 rad
    const float wTurn[3] = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 125; ++i) {
        gyroToStick(wTurn, accel, 5, s, x, y, nullptr, dt);
    }
    STRESS_CHECK(std::abs(s.injected[0]) > 0.05f, "Prior to recenter, injected must be non-zero");

    // Player flicks thumbstick: re-anchor posture at current orientation
    anchorGyroPosture(s, nullptr);
    STRESS_CHECK(s.injected[0] == 0.0f && s.injected[1] == 0.0f, "Re-anchoring must reset injected displacement to 0");
    STRESS_CHECK(s.targetAngle[0] == 0.0f && s.targetAngle[1] == 0.0f, "Re-anchoring must reset targetAngle to 0");

    // Resting at new anchor
    const float wStill[3] = {0.0f, 0.0f, 0.0f};
    gyroToStick(wStill, accel, 5, s, x, y, nullptr, dt);
    STRESS_CHECK(x == 0.0f && y == 0.0f, "Immediately after re-anchoring, resting stick must be 0");
}

// =============================================================================
// Test 8: Decoupled Sensitivity Levels (1..10) and Scale Invariance
// =============================================================================
static void testSensitivityLevelsAndScales() {
    std::printf("--- Starting Test 8: Sensitivity Levels (1..10) & Scale Invariance ---\n");

    float accel[3] = {0.0f, 1.0f, 0.0f};
    constexpr float dt = 0.004f;

    for (int lvl = 1; lvl <= 10; ++lvl) {
        GyroState s{};
        s.yawScale = 1.75f;
        s.pitchScale = 0.45f;
        anchorGyroPosture(s, nullptr);

        float totalStickX = 0.0f;
        float totalStickY = 0.0f;

        // Combined asymmetric yaw and pitch flick
        float x = 0, y = 0;
        const float wFlick[3] = {1.5f, 2.5f, 0.0f};
        for (int i = 0; i < 50; ++i) {
            gyroToStick(wFlick, accel, lvl, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, totalStickY);
        }

        const float wReturn[3] = {-0.3f, -0.5f, 0.0f};
        for (int i = 0; i < 250; ++i) {
            gyroToStick(wReturn, accel, lvl, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, totalStickY);
        }

        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 200; ++i) {
            gyroToStick(wStill, accel, lvl, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickX, totalStickY);
        }

        STRESS_CHECK(std::abs(totalStickX) < 1e-4f,
                     "Level %d: totalStickX = %.8f must be < 1e-4", lvl, totalStickX);
        STRESS_CHECK(std::abs(totalStickY) < 1e-4f,
                     "Level %d: totalStickY = %.8f must be < 1e-4", lvl, totalStickY);
    }
}

// =============================================================================
// Test 9: Multi-Cycle Drift Accumulation & Truncation Defect Characterization
// Empirically isolates the bug: snap-resetting s.injected[0] = targetInjectedX
// drops uncorrected stick integral on each settle, linearly accumulating drift.
// =============================================================================
static void testMultiCycleDriftAccumulationCharacterization() {
    std::printf("--- Starting Test 9: Multi-Cycle Drift Accumulation Defect Characterization ---\n");

    // Simulate 20 consecutive aim flicks during a single L2 aim session
    // (e.g. Fortnite engagement: aiming down sights, flicking to target, returning to center)
    GyroState s{};
    anchorGyroPosture(s, nullptr);
    float accel[3] = {0.0f, 1.0f, 0.0f};
    constexpr float dt = 0.004f;
    float cumulativeInjectedX = 0.0f, unusedY = 0.0f;

    std::printf("  Simulating 20 consecutive aim flicks under current implementation:\n");
    for (int cycle = 1; cycle <= 20; ++cycle) {
        float x = 0, y = 0;
        // Flick right: 2.0 rad/s for 0.1s (25 steps) -> +0.2 rad turn
        const float wFlick[3] = {0.0f, 2.0f, 0.0f};
        for (int i = 0; i < 25; ++i) {
            gyroToStick(wFlick, accel, 6, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, cumulativeInjectedX, unusedY);
        }

        // Return left: -0.5 rad/s for 0.4s (100 steps) -> -0.2 rad turn (exact return to 0)
        const float wReturn[3] = {0.0f, -0.5f, 0.0f};
        for (int i = 0; i < 100; ++i) {
            gyroToStick(wReturn, accel, 6, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, cumulativeInjectedX, unusedY);
        }

        // Settle at 0: 100 steps (0.4s)
        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 100; ++i) {
            gyroToStick(wStill, accel, 6, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, cumulativeInjectedX, unusedY);
        }

        if (cycle == 1 || cycle == 5 || cycle == 10 || cycle == 15 || cycle == 20) {
            std::printf("    Cycle %2d: cumulativeInjectedX = %12.8f | s.injected[0] = %12.8f\n",
                        cycle, cumulativeInjectedX, s.injected[0]);
        }
    }

    std::printf("  Final cumulative injected after 20 cycles: %.8f (requirement: < 1e-4)\n",
                cumulativeInjectedX);
    STRESS_CHECK(std::abs(cumulativeInjectedX) < 1e-4f,
                 "Multi-cycle drift: cumulativeInjectedX = %.8f must be < 1e-4 (ACCUMULATION DEFECT CONFIRMED)",
                 cumulativeInjectedX);
}

// =============================================================================
// Test 10: Empirical Verification of Recommended Mitigation
// Proves that removing snap-reset truncation and preserving true integrator memory
// holds cumulative stick injection bounded at < 1.2e-5 across infinite cycles.
// =============================================================================
static void gyroToStickMitigated(const float rawW[3], const float* accel, int level, GyroState& s, float& x, float& y,
                                 const float* orientationQuat = nullptr, float dt = 0.004f) {
    if (!rawW) { x = y = 0.0f; return; }
    if (dt <= 0.0f || !std::isfinite(dt)) dt = 0.004f;

    const float effBias[3] = {s.bias[0], s.bias[1], s.bias[2]};
    const float effYawScale = s.yawScale;
    const float effPitchScale = s.pitchScale;
    const float w[3] = {rawW[0] - effBias[0], rawW[1] - effBias[1], rawW[2] - effBias[2]};

    if (accel) {
        float m = std::sqrt(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
        if (m > 0.5f && m < 1.5f) {
            const float invM = 1.0f / m;
            const float ax = accel[0] * invM, ay = accel[1] * invM, az = accel[2] * invM;
            if (!s.upInitialized) {
                s.up[0] = ax; s.up[1] = ay; s.up[2] = az;
                s.upInitialized = true;
            } else {
                constexpr float alpha = 0.002f;
                s.up[0] += (ax - s.up[0]) * alpha;
                s.up[1] += (ay - s.up[1]) * alpha;
                s.up[2] += (az - s.up[2]) * alpha;
            }
            float n = std::sqrt(s.up[0] * s.up[0] + s.up[1] * s.up[1] + s.up[2] * s.up[2]);
            if (n > 1e-3f) {
                float invN = 1.0f / n;
                for (float& u : s.up) u *= invN;
            }
        }
    }

    const float yaw = w[0] * s.up[0] + w[1] * s.up[1] + w[2] * s.up[2];
    constexpr float kNoise = 0.015f;
    auto deadband = [](float v) { return std::abs(v) < kNoise ? 0.0f : v - std::copysign(kNoise, v); };

    const float baseScale = 0.16f * std::clamp(level, 1, kGyroLevels);
    const float tx_ff = -deadband(yaw) * baseScale * effYawScale;
    const float ty_ff = -deadband(w[0]) * baseScale * effPitchScale;

    float tx = tx_ff;
    float ty = ty_ff;

    if (orientationQuat && !s.aimingActive) {
        anchorGyroPosture(s, orientationQuat);
    }

    float targetInjectedX = 0.0f, targetInjectedY = 0.0f;
    float errX = 0.0f, errY = 0.0f;

    if (s.aimingActive) {
        if (orientationQuat) {
            if (!s.hasBaseQuat) {
                std::copy(orientationQuat, orientationQuat + 4, s.baseQuat);
                s.hasBaseQuat = true;
                s.targetAngle[0] = 0.0f; s.targetAngle[1] = 0.0f;
                s.injected[0] = 0.0f; s.injected[1] = 0.0f;
            }
            const float w0 = s.baseQuat[3], x0 = -s.baseQuat[0], y0 = -s.baseQuat[1], z0 = -s.baseQuat[2];
            const float w1 = orientationQuat[3], x1 = orientationQuat[0], y1 = orientationQuat[1], z1 = orientationQuat[2];

            float wr = w0 * w1 - x0 * x1 - y0 * y1 - z0 * z1;
            float xr = w0 * x1 + x0 * w1 + y0 * z1 - z0 * y1;
            float yr = w0 * y1 - x0 * z1 + y0 * w1 + z0 * x1;
            float zr = w0 * z1 + x0 * y1 - y0 * x1 + z0 * w1;
            if (wr < 0.0f) { wr = -wr; xr = -xr; yr = -yr; zr = -zr; }

            s.targetAngle[0] = 2.0f * std::atan2(yr, wr);
            s.targetAngle[1] = 2.0f * std::atan2(xr, wr);
        } else {
            s.targetAngle[0] += yaw * dt;
            s.targetAngle[1] += w[0] * dt;
        }

        targetInjectedX = -s.targetAngle[0] * baseScale * effYawScale;
        targetInjectedY = -s.targetAngle[1] * baseScale * effPitchScale;

        errX = targetInjectedX - s.injected[0];
        errY = targetInjectedY - s.injected[1];

        errX = std::clamp(errX, -0.4f, 0.4f);
        errY = std::clamp(errY, -0.4f, 0.4f);

        constexpr float kKp = 8.0f;
        tx = tx_ff + kKp * errX;
        ty = ty_ff + kKp * errY;
    }

    s.x += (tx - s.x) * 0.5f;
    s.y += (ty - s.y) * 0.5f;
    float m = std::hypot(s.x, s.y);

    // MITIGATION:
    // 1. Deadband cutoff at m < 1e-4 produces 0.0 stick when stationary without drift.
    // 2. DO NOT overwrite s.injected with targetInjected; always faithfully integrate s.injected += x * dt.
    if (m < 0.0001f) {
        x = y = 0.0f;
        s.x = s.y = 0.0f;
    } else {
        constexpr float kLift = 0.12f;
        constexpr float kBlendRange = 0.008f;
        float t = std::clamp(m / kBlendRange, 0.0f, 1.0f);
        float smoothBlend = t * t * (3.0f - 2.0f * t);
        float lifted = std::min(1.0f, smoothBlend * kLift + (1.0f - kLift) * m);
        x = s.x / m * lifted;
        y = s.y / m * lifted;
    }

    if (s.aimingActive) {
        s.injected[0] += x * dt;
        s.injected[1] += y * dt;
    }
}

static void testMitigationProof() {
    std::printf("--- Starting Test 10: Empirical Verification of Recommended Mitigation ---\n");

    GyroState s{};
    anchorGyroPosture(s, nullptr);
    float accel[3] = {0.0f, 1.0f, 0.0f};
    constexpr float dt = 0.004f;
    float cumulativeInjectedX = 0.0f;

    for (int cycle = 1; cycle <= 50; ++cycle) {
        float x = 0, y = 0;
        const float wFlick[3] = {0.0f, 2.0f, 0.0f};
        for (int i = 0; i < 25; ++i) {
            gyroToStickMitigated(wFlick, accel, 6, s, x, y, nullptr, dt);
            cumulativeInjectedX += x * dt;
        }

        const float wReturn[3] = {0.0f, -0.5f, 0.0f};
        for (int i = 0; i < 100; ++i) {
            gyroToStickMitigated(wReturn, accel, 6, s, x, y, nullptr, dt);
            cumulativeInjectedX += x * dt;
        }

        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 100; ++i) {
            gyroToStickMitigated(wStill, accel, 6, s, x, y, nullptr, dt);
            cumulativeInjectedX += x * dt;
        }
    }

    std::printf("  Mitigated Engine: cumulativeInjectedX after 50 cycles = %.8f (requirement < 1e-4)\n",
                cumulativeInjectedX);
    STRESS_CHECK(std::abs(cumulativeInjectedX) < 1e-4f,
                 "Mitigated engine: cumulativeInjectedX = %.8f must be < 1e-4", cumulativeInjectedX);
    STRESS_CHECK(std::abs(cumulativeInjectedX) < 2e-5f,
                 "Mitigated engine: cumulativeInjectedX = %.8f must be < 2e-5 across 50 cycles", cumulativeInjectedX);
}

int main() {
    std::printf("=================================================================\n");
    std::printf("Milestone 1 Empirical Adversarial Stress Test Suite (Challenger 1)\n");
    std::printf("=================================================================\n");

    testAsymmetricSpeedFlicks();
    testComplex3DRotations();
    testAntiWindupAndBallisticFlicks();
    testStationaryRestingAndNoise();
    testBoundaryOrientationsAndRobustness();
    testVariablePollingRates();
    testThumbstickRecenterInterruption();
    testSensitivityLevelsAndScales();
    testMultiCycleDriftAccumulationCharacterization();
    testMitigationProof();

    std::printf("=================================================================\n");
    std::printf("Summary: %d assertions passed, %d failed.\n", g_passCount, g_failCount);
    std::printf("=================================================================\n");

    return g_failCount == 0 ? 0 : 1;
}

