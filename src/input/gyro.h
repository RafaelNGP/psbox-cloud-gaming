// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Gyro aiming: turning the DualSense moves the right stick, for the cloud
// games that only know sticks. Left / right is the turn about the room's
// vertical (from gravity: a pad held tilted turns partly about its own roll
// axis), up / down the pad's pitch; eased a little and lifted past the games'
// own stick dead zone, so small turns still move the aim.
#pragma once

#include <cmath>

namespace xc::input {

constexpr int kGyroLevels = 10;  // intensity 1..kGyroLevels
enum GyroMode { kGyroOff, kGyroAlways, kGyroAiming };  // kGyroAiming: while L2 is held

struct GyroState {
    float x = 0, y = 0;            // the push given, eased
    float up[3] = {0, 1, 0};       // the room's up in the pad's axes, eased
    float bias[3] = {0, 0, 0};     // zero-drift static calibration offset
    float yawScale = 1.0f;         // decoupled horizontal multiplier
    float pitchScale = 0.8f;       // decoupled vertical multiplier
};

struct GyroCalibrator {
    enum class State { Idle, Calibrating, Succeeded, Failed };
    State state = State::Idle;
    int sampleCount = 0;
    int targetSamples = 240;
    float sum[3]{};
    float sumSq[3]{};
    float resultBias[3]{};
    float maxVariance = 0.0f;

    void start(int samples = 240) {
        state = State::Calibrating;
        sampleCount = 0;
        targetSamples = samples > 0 ? samples : 240;
        for (int i = 0; i < 3; ++i) {
            sum[i] = 0.0f;
            sumSq[i] = 0.0f;
            resultBias[i] = 0.0f;
        }
        maxVariance = 0.0f;
    }

    void reset() {
        state = State::Idle;
        sampleCount = 0;
        targetSamples = 240;
        for (int i = 0; i < 3; ++i) {
            sum[i] = 0.0f;
            sumSq[i] = 0.0f;
            resultBias[i] = 0.0f;
        }
        maxVariance = 0.0f;
    }

    bool isCalibrating() const { return state == State::Calibrating; }
    bool isDone() const { return state == State::Succeeded || state == State::Failed; }

    bool feed(const float rawW[3]) {
        if (state != State::Calibrating || !rawW) return false;
        if (!std::isfinite(rawW[0]) || !std::isfinite(rawW[1]) || !std::isfinite(rawW[2])) {
            state = State::Failed;
            return false;
        }
        for (int i = 0; i < 3; ++i) {
            sum[i] += rawW[i];
            sumSq[i] += rawW[i] * rawW[i];
        }
        if (++sampleCount >= targetSamples) {
            float maxVar = 0.0f;
            const float invN = 1.0f / static_cast<float>(sampleCount);
            for (int i = 0; i < 3; ++i) {
                float mean = sum[i] * invN;
                float var = (sumSq[i] * invN) - (mean * mean);
                if (var < 0.0f) var = 0.0f;
                resultBias[i] = mean;
                if (var > maxVar) maxVar = var;
            }
            maxVariance = maxVar;
            state = (maxVar > 0.0025f) ? State::Failed : State::Succeeded;
            return state == State::Succeeded;
        }
        return false;
    }

    float progress() const {
        return targetSamples > 0 ? (sampleCount >= targetSamples ? 1.0f : static_cast<float>(sampleCount) / static_cast<float>(targetSamples)) : 0.0f;
    }
};

// `rawW`: the pad's angular velocity (rad/s; x pitch, y yaw, z roll), `accel`
// what its accelerometer reads (g; nullptr: held flat) -> the push to add
// to the right stick (pad convention: +x right, +y down).
void gyroToStick(const float rawW[3], const float* accel, int level, GyroState& s, float& x, float& y);

// Global gyro calibration & hardware probing
void setGlobalGyroCalibration(const float bias[3], float yawScale, float pitchScale);
void getGlobalGyroCalibration(float bias[3], float& yawScale, float& pitchScale);
void resetGlobalGyroCalibration();
bool pollPadRawGyro(int padIndex, float outGyro[3]);

}  // namespace xc::input
