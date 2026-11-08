// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/gyro.h"
#include "input/controller_backend.h"

#include <algorithm>
#include <cmath>

namespace xc::input {

namespace {
float g_globalGyroBias[3] = {0.0f, 0.0f, 0.0f};
float g_globalYawScale = 1.0f;
float g_globalPitchScale = 0.8f;
bool g_hasGlobalGyroBias = false;
bool g_hasGlobalGyroScales = false;
}

void setGlobalGyroCalibration(const float bias[3], float yawScale, float pitchScale) {
    if (bias) {
        g_globalGyroBias[0] = bias[0];
        g_globalGyroBias[1] = bias[1];
        g_globalGyroBias[2] = bias[2];
        g_hasGlobalGyroBias = (bias[0] != 0.0f || bias[1] != 0.0f || bias[2] != 0.0f);
    }
    g_globalYawScale = yawScale;
    g_globalPitchScale = pitchScale;
    g_hasGlobalGyroScales = true;
}

void getGlobalGyroCalibration(float bias[3], float& yawScale, float& pitchScale) {
    if (bias) {
        bias[0] = g_globalGyroBias[0];
        bias[1] = g_globalGyroBias[1];
        bias[2] = g_globalGyroBias[2];
    }
    yawScale = g_globalYawScale;
    pitchScale = g_globalPitchScale;
}

void resetGlobalGyroCalibration() {
    g_globalGyroBias[0] = g_globalGyroBias[1] = g_globalGyroBias[2] = 0.0f;
    g_globalYawScale = 1.0f;
    g_globalPitchScale = 0.8f;
    g_hasGlobalGyroBias = false;
    g_hasGlobalGyroScales = false;
}

bool pollPadRawGyro(int padIndex, float outGyro[3]) {
    RawPad pad;
    if (backend::readPad(padIndex, pad)) {
        if (outGyro) {
            outGyro[0] = pad.gyro[0];
            outGyro[1] = pad.gyro[1];
            outGyro[2] = pad.gyro[2];
        }
        return true;
    }
    return false;
}

void gyroToStick(const float rawW[3], const float* accel, int level, GyroState& s, float& x, float& y) {
    if (!rawW) {
        x = y = 0.0f;
        return;
    }

    // Effective bias and scales: fallback to global calibration if unpopulated or default
    float effBias[3] = {s.bias[0], s.bias[1], s.bias[2]};
    if (effBias[0] == 0.0f && effBias[1] == 0.0f && effBias[2] == 0.0f && g_hasGlobalGyroBias) {
        effBias[0] = g_globalGyroBias[0];
        effBias[1] = g_globalGyroBias[1];
        effBias[2] = g_globalGyroBias[2];
        s.bias[0] = effBias[0];
        s.bias[1] = effBias[1];
        s.bias[2] = effBias[2];
    }
    float effYawScale = s.yawScale;
    float effPitchScale = s.pitchScale;
    if (effYawScale == 1.0f && effPitchScale == 0.8f && g_hasGlobalGyroScales) {
        effYawScale = g_globalYawScale;
        effPitchScale = g_globalPitchScale;
        s.yawScale = effYawScale;
        s.pitchScale = effPitchScale;
    }

    // 1. Dynamic static bias subtraction
    const float w[3] = {rawW[0] - effBias[0], rawW[1] - effBias[1], rawW[2] - effBias[2]};

    // 2. Room vertical estimation from gravity
    if (accel) {
        float m = std::sqrt(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
        if (m > 0.5f && m < 1.5f) {  // about 1 g: the pad isn't being shaken
            float sign = accel[1] < 0 ? -1.0f : 1.0f;
            for (int i = 0; i < 3; ++i) s.up[i] += (sign * accel[i] / m - s.up[i]) * 0.1f;
            float n = std::sqrt(s.up[0] * s.up[0] + s.up[1] * s.up[1] + s.up[2] * s.up[2]);
            if (n > 1e-3f)
                for (float& u : s.up) u /= n;
        }
    }

    // The turn about the room's vertical: flat, that is the pad's yaw alone.
    const float yaw = w[0] * s.up[0] + w[1] * s.up[1] + w[2] * s.up[2];
    constexpr float kNoise = 0.015f;
    auto deadband = [](float v) { return std::abs(v) < kNoise ? 0.0f : v - std::copysign(kNoise, v); };

    // 3. Decoupled sensitivity scaling
    const float baseScale = 0.16f * std::clamp(level, 1, kGyroLevels);
    const float tx = -deadband(yaw) * baseScale * effYawScale;
    const float ty = -deadband(w[0]) * baseScale * effPitchScale;

    s.x += (tx - s.x) * 0.5f;
    s.y += (ty - s.y) * 0.5f;
    float m = std::hypot(s.x, s.y);
    if (m < 0.005f) {
        x = y = 0;
        return;
    }
    // The games ignore about the first 10-20 % of the stick: start past it.
    constexpr float kLift = 0.12f;
    float lifted = std::min(1.0f, kLift + (1.0f - kLift) * m);
    x = s.x / m * lifted;
    y = s.y / m * lifted;
}

}  // namespace xc::input
