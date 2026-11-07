// SPDX-License-Identifier: GPL-3.0-or-later
// Empirical Adversarial Stress Test & Verification Harness for Milestone 4 (Bloco D)
// Gyroscope Pro Zero-Drift Calibration & Decoupled Sensitivity

#include "input/gyro.h"
#include "input/controller_backend.h"
#include "app/settings.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "input/controller.h"
#include <fstream>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

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

#define STRESS_ASSERT(cond, msg, ...) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "[FAIL] " msg " at %s:%d\n", ##__VA_ARGS__, __FILE__, __LINE__); \
        g_failCount++; \
    } else { \
        g_passCount++; \
    } \
} while(0)

// -----------------------------------------------------------------------------
// Test 1: GyroCalibrator statistical accuracy across 10,000 synthetic trials
// mu_true in [-0.1, 0.1], sigma_noise in [0.001, 0.03]
// -----------------------------------------------------------------------------
static void testCalibratorStatisticalAccuracy10k() {
    std::printf("--- Starting Test 1: GyroCalibrator 10,000 Synthetic Trials ---\n");
    std::mt19937 rng(133742);
    std::uniform_real_distribution<float> distTrueBias(-0.1f, 0.1f);
    std::uniform_real_distribution<float> distNoiseSigma(0.001f, 0.030f);

    constexpr int kTrials = 10000;
    constexpr int kSamples = 240;

    double sumErrorX = 0.0;
    double sumErrorY = 0.0;
    double sumErrorZ = 0.0;
    float maxError = 0.0f;
    int successCount = 0;

    for (int trial = 0; trial < kTrials; ++trial) {
        float trueBias[3] = {
            distTrueBias(rng),
            distTrueBias(rng),
            distTrueBias(rng)
        };
        float sigma = distNoiseSigma(rng);
        std::normal_distribution<float> normDist(0.0f, sigma);

        GyroCalibrator cal;
        cal.start(kSamples);

        for (int s = 0; s < kSamples; ++s) {
            float sample[3] = {
                trueBias[0] + normDist(rng),
                trueBias[1] + normDist(rng),
                trueBias[2] + normDist(rng)
            };
            cal.feed(sample);
        }

        if (cal.state == GyroCalibrator::State::Succeeded) {
            successCount++;
        }

        float errX = std::abs(cal.resultBias[0] - trueBias[0]);
        float errY = std::abs(cal.resultBias[1] - trueBias[1]);
        float errZ = std::abs(cal.resultBias[2] - trueBias[2]);

        sumErrorX += errX;
        sumErrorY += errY;
        sumErrorZ += errZ;

        if (errX > maxError) maxError = errX;
        if (errY > maxError) maxError = errY;
        if (errZ > maxError) maxError = errZ;
    }

    double maeX = sumErrorX / kTrials;
    double maeY = sumErrorY / kTrials;
    double maeZ = sumErrorZ / kTrials;
    double overallMAE = (maeX + maeY + maeZ) / 3.0;

    std::printf("  10k Trials Completed:\n");
    std::printf("  Success rate: %d / %d (%.2f%%)\n", successCount, kTrials, (100.0 * successCount) / kTrials);
    std::printf("  Mean Absolute Error: X: %.6f, Y: %.6f, Z: %.6f rad/s (Overall MAE: %.6f rad/s)\n",
                maeX, maeY, maeZ, overallMAE);
    std::printf("  Max Observed Error across all trials: %.6f rad/s\n", maxError);

    // Max theoretical standard error: sigma / sqrt(240) = 0.03 / 15.49 = 0.001936 rad/s.
    // Mean error of normal distribution is sqrt(2/pi) * SE ~ 0.8 * 0.001936 = 0.00155 rad/s.
    // Overall MAE should be comfortably under 0.002 rad/s!
    STRESS_ASSERT(overallMAE < 0.0020, "Overall MAE across 10,000 trials (%.6f) must be < 0.002 rad/s", overallMAE);
    STRESS_ASSERT(successCount >= 9990, "Stationary trials with sigma <= 0.03 must pass variance threshold (>99.9%%)");
}

// -----------------------------------------------------------------------------
// Test 2: Motion Injection (variance sigma^2 > 0.0025 rad^2/s^2) -> 100% Rejection Rate
// -----------------------------------------------------------------------------
static void testMotionInjectionRejection() {
    std::printf("--- Starting Test 2: Motion Injection (sigma^2 > 0.0025) 100%% Rejection Rate ---\n");
    std::mt19937 rng(999123);

    constexpr int kTrials = 10000;
    constexpr int kSamples = 240;
    int rejectionCount = 0;
    double maxVarDiff = 0.0;

    for (int trial = 0; trial < kTrials; ++trial) {
        int motionType = trial % 4;
        GyroCalibrator cal;
        cal.start(kSamples);

        std::vector<float[3]> samples(kSamples);
        float inputSamples[kSamples][3];

        if (motionType == 0) {
            // High Gaussian noise (sigma in [0.055, 0.20], so sigma^2 in [0.0030, 0.040] > 0.0025)
            std::uniform_real_distribution<float> distSigma(0.055f, 0.20f);
            float sigma = distSigma(rng);
            std::normal_distribution<float> normDist(0.0f, sigma);
            for (int s = 0; s < kSamples; ++s) {
                inputSamples[s][0] = normDist(rng);
                inputSamples[s][1] = normDist(rng);
                inputSamples[s][2] = normDist(rng);
            }
        } else if (motionType == 1) {
            // Sinusoidal movement (amp in [0.085, 1.0], so var ~ amp^2/2 >= 0.0036 > 0.0025)
            std::uniform_real_distribution<float> distAmp(0.085f, 1.0f);
            std::uniform_real_distribution<float> distFreq(0.5f, 10.0f);
            float amp = distAmp(rng);
            float freq = distFreq(rng);
            for (int s = 0; s < kSamples; ++s) {
                float t = static_cast<float>(s) / 120.0f;
                inputSamples[s][0] = amp * std::sin(6.2831853f * freq * t);
                inputSamples[s][1] = amp * std::cos(6.2831853f * freq * t);
                inputSamples[s][2] = 0.02f;
            }
        } else if (motionType == 2) {
            // Sudden pickup / step jerk (v in [0.35, 1.0], so step var ~ v^2 / 4 >= 0.03 > 0.0025)
            std::uniform_real_distribution<float> distV(0.35f, 1.0f);
            float v = distV(rng);
            for (int s = 0; s < kSamples; ++s) {
                float val = (s >= kSamples / 2) ? v : 0.02f;
                inputSamples[s][0] = val;
                inputSamples[s][1] = -val;
                inputSamples[s][2] = val * 0.5f;
            }
        } else {
            // Frequent hand tremors / shaking (every 3 samples, pulse in [-0.25, 0.25] -> var > 0.003)
            std::uniform_real_distribution<float> distPulse(-0.25f, 0.25f);
            for (int s = 0; s < kSamples; ++s) {
                float p = (s % 3 == 0) ? distPulse(rng) : 0.01f;
                inputSamples[s][0] = p;
                inputSamples[s][1] = p * 0.5f;
                inputSamples[s][2] = p * 0.2f;
            }
        }

        // Verify ground truth two-pass variance in double precision
        double sum[3] = {0, 0, 0};
        for (int s = 0; s < kSamples; ++s) {
            sum[0] += inputSamples[s][0];
            sum[1] += inputSamples[s][1];
            sum[2] += inputSamples[s][2];
        }
        double mean[3] = {sum[0] / kSamples, sum[1] / kSamples, sum[2] / kSamples};
        double sumSqDiff[3] = {0, 0, 0};
        for (int s = 0; s < kSamples; ++s) {
            double dx = inputSamples[s][0] - mean[0];
            double dy = inputSamples[s][1] - mean[1];
            double dz = inputSamples[s][2] - mean[2];
            sumSqDiff[0] += dx * dx;
            sumSqDiff[1] += dy * dy;
            sumSqDiff[2] += dz * dz;
        }
        double trueMaxVar = std::max({sumSqDiff[0] / kSamples, sumSqDiff[1] / kSamples, sumSqDiff[2] / kSamples});

        // Feed to calibrator
        for (int s = 0; s < kSamples; ++s) {
            cal.feed(inputSamples[s]);
        }

        double varDiff = std::abs(static_cast<double>(cal.maxVariance) - trueMaxVar);
        if (varDiff > maxVarDiff) maxVarDiff = varDiff;

        // If trueMaxVar > 0.0025, it must be rejected!
        if (trueMaxVar > 0.0025) {
            if (cal.state == GyroCalibrator::State::Failed) {
                rejectionCount++;
            }
        }
    }

    std::printf("  Motion Injection Results: %d / %d rejected (%.2f%%)\n",
                rejectionCount, kTrials, (100.0 * rejectionCount) / kTrials);
    std::printf("  Max Variance Discrepancy (one-pass float vs two-pass double): %.8f rad^2/s^2\n", maxVarDiff);
    STRESS_ASSERT(rejectionCount == kTrials, "Motion injection (sigma^2 > 0.0025) must produce 100%% rejection rate (got %d / %d)",
                  rejectionCount, kTrials);
    STRESS_ASSERT(maxVarDiff < 1e-4, "One-pass variance formula must remain within 0.0001 of two-pass reference");
}

// -----------------------------------------------------------------------------
// Test 3: Stationary resting (sigma^2 < 0.0015) -> 100% Success & Mean Error < 0.002
// -----------------------------------------------------------------------------
static void testStationaryRestingSuccess() {
    std::printf("--- Starting Test 3: Stationary Resting 100%% Success Rate & Accuracy ---\n");
    std::mt19937 rng(456789);
    // Resting noise sigma in [0.001, 0.035] -> sigma^2 in [1e-6, 0.001225] < 0.0015
    std::uniform_real_distribution<float> distTrueBias(-0.08f, 0.08f);
    std::uniform_real_distribution<float> distSigma(0.001f, 0.035f);

    constexpr int kTrials = 10000;
    constexpr int kSamples = 240;
    int successCount = 0;
    double totalError = 0.0;

    for (int trial = 0; trial < kTrials; ++trial) {
        float trueBias[3] = {
            distTrueBias(rng),
            distTrueBias(rng),
            distTrueBias(rng)
        };
        float sigma = distSigma(rng);
        std::normal_distribution<float> normDist(0.0f, sigma);

        GyroCalibrator cal;
        cal.start(kSamples);

        for (int s = 0; s < kSamples; ++s) {
            float sample[3] = {
                trueBias[0] + normDist(rng),
                trueBias[1] + normDist(rng),
                trueBias[2] + normDist(rng)
            };
            cal.feed(sample);
        }

        if (cal.state == GyroCalibrator::State::Succeeded) {
            successCount++;
        }

        float err = (std::abs(cal.resultBias[0] - trueBias[0]) +
                     std::abs(cal.resultBias[1] - trueBias[1]) +
                     std::abs(cal.resultBias[2] - trueBias[2])) / 3.0f;
        totalError += err;
    }

    double meanError = totalError / kTrials;
    std::printf("  Stationary Resting Results: %d / %d succeeded (%.2f%%)\n",
                successCount, kTrials, (100.0 * successCount) / kTrials);
    std::printf("  Stationary Mean Error across trials: %.6f rad/s\n", meanError);

    STRESS_ASSERT(successCount == kTrials, "Stationary resting (sigma^2 < 0.0015) must have 100%% success (got %d / %d)",
                  successCount, kTrials);
    STRESS_ASSERT(meanError < 0.0020, "Stationary resting mean error (%.6f) must be < 0.002 rad/s", meanError);
}

// -----------------------------------------------------------------------------
// Test 4: gyroToStick Output Range [-1.0f, +1.0f] under Extreme Velocity & Sensitivity
// Extreme angular velocity: 100 rad/s (~5700 deg/s)
// Extreme sensitivity scales: 200% (2.0x)
// -----------------------------------------------------------------------------
static void testGyroToStickOutputRangeExtreme() {
    std::printf("--- Starting Test 4: gyroToStick Output Range [-1.0f, +1.0f] Extreme Stress ---\n");

    std::mt19937 rng(777888);
    // Extreme angular velocities [-100.0, 100.0] rad/s
    std::uniform_real_distribution<float> distOmega(-100.0f, 100.0f);
    // Sensitivity scales: 20% to 200% (0.2x to 2.0x)
    std::uniform_real_distribution<float> distScale(0.2f, 2.0f);
    // Gyro levels: 1 to 10
    std::uniform_int_distribution<int> distLevel(1, 10);
    // Static biases: [-0.2, 0.2]
    std::uniform_real_distribution<float> distBias(-0.2f, 0.2f);
    // Accelerometer orientations: [-1.5, 1.5] g
    std::uniform_real_distribution<float> distAccel(-1.5f, 1.5f);

    constexpr int kStressRounds = 50000;
    float maxStickMagnitude = 0.0f;
    float maxStickX = -1.0f, minStickX = 1.0f;
    float maxStickY = -1.0f, minStickY = 1.0f;
    int nanOrInfCount = 0;
    int outOfBoundsCount = 0;

    for (int r = 0; r < kStressRounds; ++r) {
        GyroState state{};
        state.bias[0] = distBias(rng);
        state.bias[1] = distBias(rng);
        state.bias[2] = distBias(rng);
        state.yawScale = distScale(rng);
        state.pitchScale = distScale(rng);
        state.up[0] = 0.0f; state.up[1] = 1.0f; state.up[2] = 0.0f;

        int level = distLevel(rng);

        float rawW[3] = {distOmega(rng), distOmega(rng), distOmega(rng)};
        float accel[3] = {distAccel(rng), distAccel(rng), distAccel(rng)};

        // Run multiple frames to allow the easing filter (s.x, s.y) to accumulate
        int frames = (r % 20) + 1;
        float x = 0.0f, y = 0.0f;
        for (int f = 0; f < frames; ++f) {
            gyroToStick(rawW, accel, level, state, x, y);

            if (std::isnan(x) || std::isnan(y) || std::isinf(x) || std::isinf(y)) {
                nanOrInfCount++;
            }

            float mag = std::hypot(x, y);
            if (mag > maxStickMagnitude) maxStickMagnitude = mag;
            if (x > maxStickX) maxStickX = x;
            if (x < minStickX) minStickX = x;
            if (y > maxStickY) maxStickY = y;
            if (y < minStickY) minStickY = y;

            if (x < -1.00001f || x > 1.00001f || y < -1.00001f || y > 1.00001f || mag > 1.00001f) {
                outOfBoundsCount++;
            }
        }
    }

    std::printf("  Tested %d frames across %d rounds:\n", kStressRounds * 10, kStressRounds);
    std::printf("  Stick X range: [%.6f, %.6f]\n", minStickX, maxStickX);
    std::printf("  Stick Y range: [%.6f, %.6f]\n", minStickY, maxStickY);
    std::printf("  Max Stick Vector Magnitude: %.6f\n", maxStickMagnitude);
    std::printf("  NaN/Inf count: %d\n", nanOrInfCount);
    std::printf("  Out-of-bounds count: %d\n", outOfBoundsCount);

    STRESS_ASSERT(nanOrInfCount == 0, "gyroToStick must NEVER produce NaN or Inf");
    STRESS_ASSERT(outOfBoundsCount == 0, "gyroToStick output stick coordinates must stay within [-1.0, +1.0]");
    STRESS_ASSERT(maxStickMagnitude <= 1.00001f, "gyroToStick vector magnitude must never exceed 1.0f (got %.6f)", maxStickMagnitude);
}

// -----------------------------------------------------------------------------
// Test 5: Edge cases, zero deadband, and boundary conditions
// -----------------------------------------------------------------------------
static void testEdgeCasesAndBoundaries() {
    std::printf("--- Starting Test 5: Edge Cases and Boundary Conditions ---\n");

    // Case 5.1: Null pointers
    {
        GyroState s{};
        float x = 5.0f, y = 5.0f;
        gyroToStick(nullptr, nullptr, 5, s, x, y);
        STRESS_ASSERT(x == 0.0f && y == 0.0f, "Null rawW must result in x=0, y=0");

        GyroCalibrator cal;
        cal.start(10);
        bool fed = cal.feed(nullptr);
        STRESS_ASSERT(!fed, "Calibrator feed(nullptr) must safely return false");
    }

    // Case 5.2: Exact zero-drift rest: raw gyro equals static bias
    {
        GyroState s{};
        s.bias[0] = 0.0521f;
        s.bias[1] = -0.0384f;
        s.bias[2] = 0.0195f;
        s.yawScale = 2.0f;   // 200%
        s.pitchScale = 2.0f; // 200%
        s.up[0] = 0.0f; s.up[1] = 1.0f; s.up[2] = 0.0f;

        float rawAtRest[3] = {s.bias[0], s.bias[1], s.bias[2]};
        float accel[3] = {0.0f, 1.0f, 0.0f};
        float x = 123.0f, y = 456.0f;
        for (int i = 0; i < 60; ++i) {
            gyroToStick(rawAtRest, accel, 10, s, x, y);
        }
        STRESS_ASSERT(x == 0.0f && y == 0.0f, "Raw gyro matching bias must produce exact 0.0 stick output");
    }

    // Case 5.3: Catastrophic cancellation stress in variance computation
    // Large constant DC offset with virtually zero variance
    {
        GyroCalibrator cal;
        cal.start(240);
        float highDc[3] = {10.0f, -10.0f, 5.0f};
        for (int i = 0; i < 240; ++i) {
            cal.feed(highDc);
        }
        STRESS_ASSERT(cal.state == GyroCalibrator::State::Succeeded,
                      "Constant high DC offset must not cause negative variance or failure");
        STRESS_ASSERT(cal.maxVariance >= 0.0f && cal.maxVariance < 1e-5f,
                      "Variance of constant input must be 0.0 (got %.8f)", cal.maxVariance);
        STRESS_ASSERT(std::abs(cal.resultBias[0] - 10.0f) < 1e-4f, "DC offset X mean accuracy");
        STRESS_ASSERT(std::abs(cal.resultBias[1] - -10.0f) < 1e-4f, "DC offset Y mean accuracy");
        STRESS_ASSERT(std::abs(cal.resultBias[2] - 5.0f) < 1e-4f, "DC offset Z mean accuracy");
    }

    // Case 5.4: Post-completion immutability
    {
        GyroCalibrator cal;
        cal.start(10);
        float s1[3] = {0.01f, 0.01f, 0.01f};
        for (int i = 0; i < 10; ++i) cal.feed(s1);
        STRESS_ASSERT(cal.state == GyroCalibrator::State::Succeeded, "Cal completed");
        float savedBiasX = cal.resultBias[0];

        // Further feeds should return false and not alter state or resultBias
        float badSample[3] = {50.0f, 50.0f, 50.0f};
        bool extra = cal.feed(badSample);
        STRESS_ASSERT(!extra, "feed() after completion must return false");
        STRESS_ASSERT(cal.state == GyroCalibrator::State::Succeeded, "State must remain Succeeded");
        STRESS_ASSERT(cal.resultBias[0] == savedBiasX, "resultBias must not be mutated");
    }

    // Case 5.5: Non-standard sample count (e.g. 10 samples or 1200 samples)
    {
        GyroCalibrator cal1;
        cal1.start(10);
        for (int i = 0; i < 10; ++i) {
            float s[3] = {0.02f, 0.02f, 0.02f};
            cal1.feed(s);
        }
        STRESS_ASSERT(cal1.sampleCount == 10 && cal1.state == GyroCalibrator::State::Succeeded,
                      "Calibrator handles small sample counts");

        GyroCalibrator cal2;
        cal2.start(0); // 0 or negative must default to 240
        STRESS_ASSERT(cal2.targetSamples == 240, "Calibrator defaults 0/negative target samples to 240");
    }
}

// -----------------------------------------------------------------------------
// Test 6: Settings JSON serialization round-trip under stress
// Negative numbers, decimals, clamping, missing keys
// -----------------------------------------------------------------------------
static void testSettingsJsonStress() {
    std::printf("--- Starting Test 6: Settings JSON Serialization Stress ---\n");

    const std::string tmpPath = "/tmp/test_m4_stress_settings.json";

    // 6.1: Precision preservation of sub-milliradian floats
    {
        app::Settings s;
        s.gyroYawScale = 185;
        s.gyroPitchScale = 45;
        s.gyroBias[0] = -0.089765f;
        s.gyroBias[1] = 0.043219f;
        s.gyroBias[2] = -0.000123f;

        STRESS_ASSERT(s.save(tmpPath), "Save settings JSON");

        app::Settings loaded;
        STRESS_ASSERT(loaded.load(tmpPath), "Load settings JSON");
        STRESS_ASSERT(loaded.gyroYawScale == 185, "Preserve gyroYawScale 185%%");
        STRESS_ASSERT(loaded.gyroPitchScale == 45, "Preserve gyroPitchScale 45%%");
        STRESS_ASSERT(std::abs(loaded.gyroBias[0] - -0.089765f) < 0.00001f, "Bias X precision");
        STRESS_ASSERT(std::abs(loaded.gyroBias[1] - 0.043219f) < 0.00001f, "Bias Y precision");
        STRESS_ASSERT(std::abs(loaded.gyroBias[2] - -0.000123f) < 0.00001f, "Bias Z precision");
    }

    // 6.2: Clamping behavior on out-of-range sensitivity scales
    {
        std::ofstream out(tmpPath);
        out << R"({
            "gyroYawScale": 9999,
            "gyroPitchScale": -50,
            "gyroBias": [0.1, 0.2, 0.3]
        })";
        out.close();

        app::Settings loaded;
        STRESS_ASSERT(loaded.load(tmpPath), "Load malformed out-of-range scales JSON");
        STRESS_ASSERT(loaded.gyroYawScale == 200, "gyroYawScale 9999 clamped to 200");
        STRESS_ASSERT(loaded.gyroPitchScale == 20, "gyroPitchScale -50 clamped to 20");
        STRESS_ASSERT(std::abs(loaded.gyroBias[0] - 0.1f) < 0.001f, "Bias X loaded");
    }

    // 6.3: Malformed gyroBias (e.g. not an array, or array with < 3 elements)
    {
        std::ofstream out(tmpPath);
        out << R"({
            "gyroYawScale": 120,
            "gyroPitchScale": 70,
            "gyroBias": "not-an-array"
        })";
        out.close();

        app::Settings loaded;
        loaded.gyroBias[0] = 0.05f; loaded.gyroBias[1] = 0.05f; loaded.gyroBias[2] = 0.05f;
        STRESS_ASSERT(loaded.load(tmpPath), "Load malformed gyroBias JSON");
        // Should keep existing or default without crashing
        STRESS_ASSERT(loaded.gyroBias[0] == 0.05f, "Malformed gyroBias array ignored safely");
    }

    unlink(tmpPath.c_str());
}

// -----------------------------------------------------------------------------
// Test 7: End-to-end integration: Global Calibration -> controller_common pipeline
// -----------------------------------------------------------------------------
static void testEndToEndCalibrationPipeline() {
    std::printf("--- Starting Test 7: End-to-End Global Calibration -> Controller Pipeline ---\n");

    float bias[3] = {0.045f, 0.0f, 0.0f};
    setGlobalGyroCalibration(bias, 1.0f, 0.8f);

    setGyro(kGyroAlways, 5);
    setGyroActive(true);

    backend::g_mockPad = RawPad{};
    // Sensor reading has 0.045 rad/s static drift along X (pitch)
    backend::g_mockPad.gyro[0] = 0.045f;
    backend::g_mockPad.gyro[1] = 0.0f;
    backend::g_mockPad.gyro[2] = 0.0f;
    backend::g_mockPad.hasAccel = false;

    ControllerState out{};
    for (int i = 0; i < 30; ++i) {
        pollPad(0, out);
    }

    std::printf("  End-to-end pollPad result: rightStickX = %.6f, rightStickY = %.6f\n",
                out.rightStickX, out.rightStickY);

    // If zero-drift calibration is hooked up to the controller pipeline, stick output should be exactly 0
    bool zeroDriftEliminated = (out.rightStickX == 0.0f && out.rightStickY == 0.0f);
    if (!zeroDriftEliminated) {
        std::printf("  [VULNERABILITY/BUG CONFIRMED]: pollPad() produced rightStickY = %.6f despite global bias = %.4f!\n",
                    out.rightStickY, bias[0]);
    }
    STRESS_ASSERT(zeroDriftEliminated,
                  "End-to-end pipeline: global gyro calibration must eliminate stick deflection on resting controller");
}

int main() {
    std::printf("=================================================================\n");
    std::printf("Milestone 4 Empirical Adversarial Stress Test Suite\n");
    std::printf("=================================================================\n");

    testCalibratorStatisticalAccuracy10k();
    testMotionInjectionRejection();
    testStationaryRestingSuccess();
    testGyroToStickOutputRangeExtreme();
    testEdgeCasesAndBoundaries();
    testSettingsJsonStress();
    testEndToEndCalibrationPipeline();

    std::printf("=================================================================\n");
    std::printf("Summary: %d assertions passed, %d failed.\n", g_passCount, g_failCount);
    std::printf("=================================================================\n");

    return g_failCount == 0 ? 0 : 1;
}
