// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/gyro.h"
#include "input/controller_backend.h"

#include <atomic>
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

// Read by the polling thread, set by the UI's: plain atomics.
static std::atomic<float> g_responseStart{0.12f}, g_responseExponent{1.0f};
static std::atomic<bool> g_invertX{false}, g_invertY{false};

void setGyroResponse(const GyroResponse& r) {
    g_responseStart = std::clamp(r.start, 0.0f, 0.6f);
    g_responseExponent = std::clamp(r.exponent, 0.5f, 5.0f);
    g_invertX = r.invertX;
    g_invertY = r.invertY;
}

GyroResponse gyroResponse() {
    GyroResponse r;
    r.start = g_responseStart;
    r.exponent = g_responseExponent;
    r.invertX = g_invertX;
    r.invertY = g_invertY;
    return r;
}

// The stick level that gives camera speed `v` (0..1), and the speed a level gives.
static float responseLevel(float v, float start, float exponent) {
    if (v >= 1.0f) return 1.0f;
    if (v <= 0.0f) return start;
    return start + (1.0f - start) * (exponent == 1.0f ? v : std::pow(v, 1.0f / exponent));
}
static float responseSpeed(float level, float start, float exponent) {
    if (level <= start) return 0.0f;
    const float u = std::min(1.0f, (level - start) / (1.0f - start));
    return exponent == 1.0f ? u : std::pow(u, exponent);
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

namespace {

bool isValidUnitQuat(const float* q, float outNormed[4]) {
    if (!q) return false;
    if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2]) || !std::isfinite(q[3])) return false;
    float normSq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (normSq <= 0.5f || normSq >= 1.5f) return false;
    float invN = 1.0f / std::sqrt(normSq);
    for (int i = 0; i < 4; ++i) outNormed[i] = q[i] * invN;
    return true;
}

bool normalizeAccelUp(const float* accel, float outUp[3]) {
    if (!accel) return false;
    if (!std::isfinite(accel[0]) || !std::isfinite(accel[1]) || !std::isfinite(accel[2])) return false;
    float m = std::sqrt(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
    if (m <= 0.5f || m >= 1.5f) return false;
    float invM = 1.0f / m;
    outUp[0] = accel[0] * invM;
    outUp[1] = accel[1] * invM;
    outUp[2] = accel[2] * invM;
    return true;
}

}  // namespace

void anchorGyroPosture(GyroState& s, const float* orientationQuat, const float* accel) {
    s.aimingActive = true;
    float upNow[3];
    if (normalizeAccelUp(accel, upNow)) {
        for (int i = 0; i < 3; ++i) s.up[i] = upNow[i];
        s.upInitialized = true;
        for (int i = 0; i < 3; ++i) s.baseUp[i] = upNow[i];
        s.baseUpInitialized = true;
    } else {
        float n = std::sqrt(s.up[0] * s.up[0] + s.up[1] * s.up[1] + s.up[2] * s.up[2]);
        if (n > 1e-3f) {
            float invN = 1.0f / n;
            for (int i = 0; i < 3; ++i) s.baseUp[i] = s.up[i] * invN;
        } else {
            s.baseUp[0] = 0.0f;
            s.baseUp[1] = 1.0f;
            s.baseUp[2] = 0.0f;
        }
        s.baseUpInitialized = s.upInitialized;
    }

    float qNorm[4];
    if (isValidUnitQuat(orientationQuat, qNorm)) {
        std::copy(qNorm, qNorm + 4, s.baseQuat);
        s.hasBaseQuat = true;
    } else {
        s.baseQuat[0] = s.baseQuat[1] = s.baseQuat[2] = 0.0f;
        s.baseQuat[3] = 1.0f;
        s.hasBaseQuat = false;
    }
    s.softQuat[0] = s.softQuat[1] = s.softQuat[2] = 0.0f;
    s.softQuat[3] = 1.0f;
    s.targetAngle[0] = 0.0f;
    s.targetAngle[1] = 0.0f;
    s.injected[0] = 0.0f;
    s.injected[1] = 0.0f;
    s.x = 0.0f;
    s.y = 0.0f;
}

void rebaseGyroPosture(GyroState& s, const float* orientationQuat) {
    if (!s.aimingActive) {
        anchorGyroPosture(s, orientationQuat, nullptr);
        return;
    }
    float n = std::sqrt(s.up[0] * s.up[0] + s.up[1] * s.up[1] + s.up[2] * s.up[2]);
    if (s.upInitialized && n > 1e-3f) {
        for (int i = 0; i < 3; ++i) s.baseUp[i] = s.up[i] / n;
        s.baseUpInitialized = true;
    }
    float qNorm[4];
    if (isValidUnitQuat(orientationQuat, qNorm)) {
        std::copy(qNorm, qNorm + 4, s.baseQuat);
        s.hasBaseQuat = true;
    } else {
        s.baseQuat[0] = s.baseQuat[1] = s.baseQuat[2] = 0.0f;
        s.baseQuat[3] = 1.0f;
        s.hasBaseQuat = false;
    }
    s.softQuat[0] = s.softQuat[1] = s.softQuat[2] = 0.0f;
    s.softQuat[3] = 1.0f;
    s.targetAngle[0] = s.targetAngle[1] = 0.0f;
    s.injected[0] = s.injected[1] = 0.0f;
}

void resetGyroPosture(GyroState& s) {
    s.aimingActive = false;
    s.hasBaseQuat = false;
    s.baseQuat[0] = s.baseQuat[1] = s.baseQuat[2] = 0.0f;
    s.baseQuat[3] = 1.0f;
    s.softQuat[0] = s.softQuat[1] = s.softQuat[2] = 0.0f;
    s.softQuat[3] = 1.0f;
    s.targetAngle[0] = 0.0f;
    s.targetAngle[1] = 0.0f;
    s.injected[0] = 0.0f;
    s.injected[1] = 0.0f;
    s.x = 0.0f;
    s.y = 0.0f;
    s.steerX = 0.0f;
    s.steerAngle = 0.0f;
    s.up[0] = 0.0f;
    s.up[1] = 1.0f;
    s.up[2] = 0.0f;
    s.baseUp[0] = 0.0f;
    s.baseUp[1] = 1.0f;
    s.baseUp[2] = 0.0f;
    s.upInitialized = false;
    s.baseUpInitialized = false;
}

void gyroToStick(const float rawW[3], const float* accel, int level, GyroState& s, float& x, float& y,
                 const float* orientationQuat, float dt) {
    if (!rawW || !std::isfinite(rawW[0]) || !std::isfinite(rawW[1]) || !std::isfinite(rawW[2])) {
        x = y = 0.0f;
        return;
    }

    if (dt <= 0.0f || !std::isfinite(dt)) dt = 0.004f;
    else dt = std::clamp(dt, 0.0005f, 0.050f);

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

    // 2. Room vertical estimation from gravity (tau approx 2s, dt-invariant)
    float accUp[3];
    if (normalizeAccelUp(accel, accUp)) {
        if (!s.upInitialized) {
            for (int i = 0; i < 3; ++i) s.up[i] = accUp[i];
            s.upInitialized = true;
            if (!s.baseUpInitialized) {
                for (int i = 0; i < 3; ++i) s.baseUp[i] = accUp[i];
                s.baseUpInitialized = true;
            }
        } else {
            const float alpha = 1.0f - std::exp(-dt / 2.0f);
            for (int i = 0; i < 3; ++i) s.up[i] += (accUp[i] - s.up[i]) * alpha;
        }
        float n = std::sqrt(s.up[0] * s.up[0] + s.up[1] * s.up[1] + s.up[2] * s.up[2]);
        if (n > 1e-3f) {
            float invN = 1.0f / n;
            for (float& u : s.up) u *= invN;
        }
    }

    // 3. Player Space Gyro with Combined Yaw and Roll (JSL / Steam Input standard)
    // The turn about the room's vertical, read on the pad's yaw and roll axes only: its pitch
    // axis already aims up / down by itself, and with the pad rolled to one side that axis has
    // a share of the vertical too, so counting it here turned an up / down move sideways as well.
    const float wWorldProj = w[1] * s.up[1] + w[2] * s.up[2];

    // Total angular rate in turning plane (Y-Z plane of controller):
    const float yzMag = std::sqrt(w[1] * w[1] + w[2] * w[2]);

    // In player space, when controller is held near flat, rolling the wrists (w[2])
    // naturally rotates the aim horizontally without losing input:
    float yaw = wWorldProj;
    if (yzMag > 1e-5f) {
        float rollContrib = w[2] * (1.0f - std::abs(s.up[2]));
        float combinedTurn = wWorldProj + rollContrib;
        float maxAllowed = std::max(yzMag, std::abs(wWorldProj));
        yaw = std::copysign(std::min(std::abs(combinedTurn) * 1.4142f, maxAllowed), combinedTurn);
    }

    // Pitch: decoupled local pitch w[0] (standard in modern player-space aiming)
    const float pitchRate = w[0];

    // Smooth adaptive deadband: suppresses stationary IMU noise without sharp cliff for micro-aiming
    constexpr float kNoiseMin = 0.0105f;
    constexpr float kNoiseMax = 0.0220f;
    auto smoothDeadband = [](float v) -> float {
        float av = std::abs(v);
        if (av <= kNoiseMin) return 0.0f;
        if (av >= kNoiseMax) return v;
        float t = (av - kNoiseMin) / (kNoiseMax - kNoiseMin);
        float blend = t * t * (3.0f - 2.0f * t); // C1 continuous smoothstep
        return v * blend;
    };

    // 4. Decoupled sensitivity scaling
    const float baseScale = 0.16f * std::clamp(level, 1, kGyroLevels);
    // In closed-loop aiming, preserve full calibrated velocity above noise floor so slow
    // fine-aiming matches target derivative without static deadband lag.
    const float tx_ff = s.aimingActive
        ? (std::abs(yaw) < kNoiseMin ? 0.0f : -yaw * baseScale * effYawScale)
        : (-smoothDeadband(yaw) * baseScale * effYawScale);
    const float ty_ff = s.aimingActive
        ? (std::abs(pitchRate) < kNoiseMin ? 0.0f : -pitchRate * baseScale * effPitchScale)
        : (-smoothDeadband(pitchRate) * baseScale * effPitchScale);

    float tx = tx_ff;
    float ty = ty_ff;

    float qCur[4];
    const bool validQuat = isValidUnitQuat(orientationQuat, qCur);

    // Auto-anchor if valid hardware orientation quaternion is provided and not yet aiming
    if (validQuat && !s.aimingActive) {
        anchorGyroPosture(s, qCur, accel);
    }

    // 4. Closed-loop SO(3) posture tracking
    float targetInjectedX = 0.0f;
    float targetInjectedY = 0.0f;
    float errX = 0.0f;
    float errY = 0.0f;

    if (s.aimingActive) {
        float wr = 1.0f, xr = 0.0f, yr = 0.0f, zr = 0.0f;
        if (validQuat) {
            if (!s.hasBaseQuat) {
                std::copy(qCur, qCur + 4, s.baseQuat);
                s.hasBaseQuat = true;
                s.targetAngle[0] = 0.0f;
                s.targetAngle[1] = 0.0f;
                s.injected[0] = 0.0f;
                s.injected[1] = 0.0f;
            }
            // Relative quaternion: q_rel = q_0^* (x) q_1
            const float w0 = s.baseQuat[3], x0 = -s.baseQuat[0], y0 = -s.baseQuat[1], z0 = -s.baseQuat[2];
            const float w1 = qCur[3], x1 = qCur[0], y1 = qCur[1], z1 = qCur[2];
            wr = w0 * w1 - x0 * x1 - y0 * y1 - z0 * z1;
            xr = w0 * x1 + x0 * w1 + y0 * z1 - z0 * y1;
            yr = w0 * y1 - x0 * z1 + y0 * w1 + z0 * x1;
            zr = w0 * z1 + x0 * y1 - y0 * x1 + z0 * w1;
        } else {
            // Software SO(3) fallback: integrate calibrated body angular velocity above noise floor
            const float wg[3] = {
                smoothDeadband(w[0]),
                smoothDeadband(w[1]),
                smoothDeadband(w[2]),
            };
            const float wMag = std::sqrt(wg[0] * wg[0] + wg[1] * wg[1] + wg[2] * wg[2]);
            const float dPhi = wMag * dt;
            if (dPhi > 1e-7f) {
                const float half = 0.5f * dPhi;
                const float invW = std::sin(half) / wMag;
                const float dx = wg[0] * invW, dy = wg[1] * invW, dz = wg[2] * invW, dw = std::cos(half);
                const float sw = s.softQuat[3], sx = s.softQuat[0], sy = s.softQuat[1], sz = s.softQuat[2];
                float nw = sw * dw - sx * dx - sy * dy - sz * dz;
                float nx = sw * dx + sx * dw + sy * dz - sz * dy;
                float ny = sw * dy - sx * dz + sy * dw + sz * dx;
                float nz = sw * dz + sx * dy - sy * dx + sz * dw;
                float n = std::sqrt(nx * nx + ny * ny + nz * nz + nw * nw);
                if (n > 1e-6f) {
                    float invN = 1.0f / n;
                    s.softQuat[0] = nx * invN;
                    s.softQuat[1] = ny * invN;
                    s.softQuat[2] = nz * invN;
                    s.softQuat[3] = nw * invN;
                }
            }
            xr = s.softQuat[0];
            yr = s.softQuat[1];
            zr = s.softQuat[2];
            wr = s.softQuat[3];
        }

        if (wr < 0.0f) {
            wr = -wr;
            xr = -xr;
            yr = -yr;
            zr = -zr;
        }

        // Lie-algebra rotation vector r = theta * axis of q_rel in anchor body frame
        const float v = std::sqrt(xr * xr + yr * yr + zr * zr);
        const float angScale = (v > 1e-6f) ? (2.0f * std::atan2(v, wr) / v) : 2.0f;
        const float rx = xr * angScale;
        const float ry = yr * angScale;
        const float rz = zr * angScale;

        // World vertical yaw in anchor body frame
        const float uy = s.baseUp[1], uz = s.baseUp[2];
        float rTurn = ry * uy + rz * uz;  // not the pitch axis: see the rate above
        float rzMag = std::sqrt(ry * ry + rz * rz);
        if (rzMag > 1e-6f) {
            float rRollExtra = rz * (1.0f - std::abs(uz));
            float rCombined = rTurn + rRollExtra;
            s.targetAngle[0] = std::copysign(std::min(std::abs(rCombined), rzMag), rCombined);
        } else {
            s.targetAngle[0] = rTurn;
        }

        // Decoupled pitch tracking without yaw/roll cross-talk
        s.targetAngle[1] = rx;

        targetInjectedX = -s.targetAngle[0] * baseScale * effYawScale;
        targetInjectedY = -s.targetAngle[1] * baseScale * effPitchScale;

        errX = std::clamp(targetInjectedX - s.injected[0], -0.4f, 0.4f);
        errY = std::clamp(targetInjectedY - s.injected[1], -0.4f, 0.4f);

        const float kp = std::min(16.0f, 0.45f / dt);
        tx = tx_ff + kp * errX;
        ty = ty_ff + kp * errY;
    }

    const float kLift = g_responseStart.load(std::memory_order_relaxed);
    const float kExponent = g_responseExponent.load(std::memory_order_relaxed);
    constexpr float kBlendRange = 0.025f;
    if (s.aimingActive && tx_ff == 0.0f && ty_ff == 0.0f && std::hypot(errX, errY) < 5e-5f) {
        s.x = s.y = 0.0f;
        x = y = 0.0f;
        return;
    }

    const float ease = 1.0f - std::exp(-dt / 0.0058f);
    s.x += (tx - s.x) * ease;
    s.y += (ty - s.y) * ease;
    float m = std::hypot(s.x, s.y);
    const float minMag = s.aimingActive ? 5e-5f : 0.001f;
    if (m < minMag) {
        x = y = 0.0f;
    } else if (!s.aimingActive) {
        // The games ignore about the first 10-20 % of the stick: start past it with a C0 continuous ramp.
        float smoothBlend = std::clamp((m - minMag) / kBlendRange, 0.0f, 1.0f);
        float lifted = std::min(1.0f, smoothBlend * responseLevel(std::min(1.0f, m), kLift, kExponent));
        x = s.x / m * lifted;
        y = s.y / m * lifted;
    } else {
        // In closed-loop aiming, emit lifted output >= kLift so fine aiming still moves the camera,
        // and integrate ONLY the effective deflection above deadzone into s.injected.
        float lifted = responseLevel(std::min(1.0f, m), kLift, kExponent);
        x = s.x / m * lifted;
        y = s.y / m * lifted;
    }

    if (s.aimingActive) {
        float outMag = std::hypot(x, y);
        if (outMag > kLift) {
            float effMag = responseSpeed(outMag, kLift, kExponent);
            s.injected[0] += (x / outMag) * effMag * dt;
            s.injected[1] += (y / outMag) * effMag * dt;
        }
    }
    // Asked for by the player, as with an inverted stick: only what leaves here turns.
    if (g_invertX.load(std::memory_order_relaxed)) x = -x;
    if (g_invertY.load(std::memory_order_relaxed)) y = -y;
}

float gyroToSteering(const float rawW[3], const float* accel, int level, GyroState& s,
                     const float* orientationQuat, float dt) {
    if (!rawW || !std::isfinite(rawW[0]) || !std::isfinite(rawW[1]) || !std::isfinite(rawW[2])) {
        return 0.0f;
    }
    if (dt <= 0.0f || !std::isfinite(dt)) dt = 0.004f;
    else dt = std::clamp(dt, 0.0005f, 0.050f);

    float effBias[3] = {s.bias[0], s.bias[1], s.bias[2]};
    if (effBias[0] == 0.0f && effBias[1] == 0.0f && effBias[2] == 0.0f && g_hasGlobalGyroBias) {
        effBias[0] = g_globalGyroBias[0];
        effBias[1] = g_globalGyroBias[1];
        effBias[2] = g_globalGyroBias[2];
        s.bias[0] = effBias[0];
        s.bias[1] = effBias[1];
        s.bias[2] = effBias[2];
    }
    float effScale = s.yawScale > 0.0f ? s.yawScale : 1.0f;
    if (effScale == 1.0f && g_hasGlobalGyroScales) {
        effScale = g_globalYawScale;
    }

    const float wz = rawW[2] - effBias[2];
    const float wy = rawW[1] - effBias[1];

    // 1. Calculate roll angle from gravity accelerometer
    float rollAccel = 0.0f;
    bool hasAccelRoll = false;
    if (accel && std::isfinite(accel[0]) && std::isfinite(accel[1]) && std::isfinite(accel[2])) {
        float ax = accel[0], ay = accel[1], az = accel[2];
        float amag = std::sqrt(ax * ax + ay * ay + az * az);
        if (amag >= 0.4f && amag <= 2.2f) {
            float ayz = std::sqrt(ay * ay + az * az);
            if (ayz > 0.05f) {
                // When right grip dips down, ax > 0 -> steer right (+); when left grip dips down, ax < 0 -> steer left (-)
                rollAccel = std::atan2(ax, ayz);
                hasAccelRoll = true;
            }
        }
    }

    // 2. Hardware quaternion fallback / enhancement if available
    float qNorm[4];
    if (isValidUnitQuat(orientationQuat, qNorm)) {
        // Roll from orientation quaternion: 2*(w*z + x*y)
        float sinRoll = 2.0f * (qNorm[3] * qNorm[2] + qNorm[0] * qNorm[1]);
        sinRoll = std::clamp(sinRoll, -1.0f, 1.0f);
        float rollQuat = std::asin(sinRoll);
        if (!hasAccelRoll) {
            rollAccel = rollQuat;
            hasAccelRoll = true;
        }
    }

    // 3. Update room vertical estimation for steering rate projection
    float accUp[3];
    if (normalizeAccelUp(accel, accUp)) {
        if (!s.upInitialized) {
            for (int i = 0; i < 3; ++i) s.up[i] = accUp[i];
            s.upInitialized = true;
        } else {
            const float alpha = 1.0f - std::exp(-dt / 2.0f);
            for (int i = 0; i < 3; ++i) s.up[i] += (accUp[i] - s.up[i]) * alpha;
        }
        float n = std::sqrt(s.up[0] * s.up[0] + s.up[1] * s.up[1] + s.up[2] * s.up[2]);
        if (n > 1e-3f) {
            float invN = 1.0f / n;
            for (float& u : s.up) u *= invN;
        }
    }

    // 4. Complementary filter: integrate gyro angular velocity & blend towards reference roll
    // Effective steering turn rate combines roll (wz) and tilted yaw (wy)
    float steerRate = wz;
    if (s.upInitialized) {
        steerRate = wz * std::abs(s.up[1]) + wy * std::abs(s.up[2]);
        if (wz < 0.0f && wy < 0.0f) steerRate = -std::abs(steerRate);
        else if (wz > 0.0f && wy > 0.0f) steerRate = std::abs(steerRate);
        else steerRate = wz;
    }

    if (!hasAccelRoll) {
        // Pure gyro integration with gradual center decay
        s.steerAngle += steerRate * dt;
        s.steerAngle *= std::exp(-dt / 1.5f);
    } else {
        // Fast complementary filter (tau = 0.12s): sub-millisecond gyro response + drift-free gravity reference
        s.steerAngle += steerRate * dt;
        const float alpha = 1.0f - std::exp(-dt / 0.12f);
        s.steerAngle += (rollAccel - s.steerAngle) * alpha;
    }

    // 5. Map steering angle to [-1.0, +1.0] deflection
    // Lock-to-lock max angle: 45 deg default at level 5, 25 deg at level 10, 90 deg at level 1
    const float baseMaxAngle = (45.0f * 3.14159265f / 180.0f) / (std::clamp(level, 1, 10) / 5.0f);
    const float maxAngle = std::max(0.15f, baseMaxAngle / std::max(0.2f, effScale));

    // Smooth deadzone around center: ~1.5 deg = 0.026 rad
    constexpr float kDeadzone = 0.026f;
    float absAngle = std::abs(s.steerAngle);
    float targetSteer = 0.0f;
    if (absAngle > kDeadzone) {
        float span = maxAngle - kDeadzone;
        float u = std::clamp((absAngle - kDeadzone) / span, 0.0f, 1.0f);
        // Slightly progressive steering curve (60% linear, 40% quadratic) for natural car feel
        float curve = 0.60f * u + 0.40f * u * u;
        targetSteer = std::copysign(std::min(1.0f, curve), s.steerAngle);
    }

    // 5ms low-pass easing to remove high-frequency hand tremor
    const float ease = 1.0f - std::exp(-dt / 0.005f);
    s.steerX += (targetSteer - s.steerX) * ease;
    return std::clamp(s.steerX, -1.0f, 1.0f);
}

}  // namespace xc::input
