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

    // The turn about the room's vertical: flat, that is the pad's yaw alone.
    const float yaw = w[0] * s.up[0] + w[1] * s.up[1] + w[2] * s.up[2];
    // Horizon pitch axis orthogonal to live s.up (reduces to +w[0] when flat or upside down)
    float px0 = 1.0f - s.up[0] * s.up[0];
    float px1 = -s.up[0] * s.up[1];
    float px2 = -s.up[0] * s.up[2];
    float pxLen = std::sqrt(px0 * px0 + px1 * px1 + px2 * px2);
    if (pxLen > 1e-3f) {
        float invPx = 1.0f / pxLen;
        px0 *= invPx;
        px1 *= invPx;
        px2 *= invPx;
    } else {
        px0 = 1.0f;
        px1 = 0.0f;
        px2 = 0.0f;
    }
    const float pitchRate = w[0] * px0 + w[1] * px1 + w[2] * px2;
    constexpr float kNoise = 0.015f;
    auto deadband = [](float v) { return std::abs(v) < kNoise ? 0.0f : v - std::copysign(kNoise, v); };

    // 3. Decoupled sensitivity scaling
    const float baseScale = 0.16f * std::clamp(level, 1, kGyroLevels);
    // In closed-loop aiming, preserve full calibrated velocity above noise floor so slow
    // fine-aiming matches target derivative without static deadband lag.
    const float tx_ff = s.aimingActive
        ? (std::abs(yaw) < kNoise ? 0.0f : -yaw * baseScale * effYawScale)
        : (-deadband(yaw) * baseScale * effYawScale);
    const float ty_ff = s.aimingActive
        ? (std::abs(pitchRate) < kNoise ? 0.0f : -pitchRate * baseScale * effPitchScale)
        : (-deadband(pitchRate) * baseScale * effPitchScale);

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
                std::abs(w[0]) >= kNoise ? w[0] : 0.0f,
                std::abs(w[1]) >= kNoise ? w[1] : 0.0f,
                std::abs(w[2]) >= kNoise ? w[2] : 0.0f,
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

        // World vertical yaw in anchor body frame (100% accurate even when held tilted)
        const float ux = s.baseUp[0], uy = s.baseUp[1], uz = s.baseUp[2];
        s.targetAngle[0] = rx * ux + ry * uy + rz * uz;

        // Horizon pitch axis orthogonal to gravity (removes roll-induced yaw->pitch crosstalk)
        float ex0 = 1.0f - ux * ux;
        float ex1 = -ux * uy;
        float ex2 = -ux * uz;
        float exLen = std::sqrt(ex0 * ex0 + ex1 * ex1 + ex2 * ex2);
        if (exLen > 1e-3f) {
            float invEx = 1.0f / exLen;
            ex0 *= invEx;
            ex1 *= invEx;
            ex2 *= invEx;
        } else {
            ex0 = 1.0f;
            ex1 = 0.0f;
            ex2 = 0.0f;
        }
        s.targetAngle[1] = rx * ex0 + ry * ex1 + rz * ex2;

        targetInjectedX = -s.targetAngle[0] * baseScale * effYawScale;
        targetInjectedY = -s.targetAngle[1] * baseScale * effPitchScale;

        errX = std::clamp(targetInjectedX - s.injected[0], -0.4f, 0.4f);
        errY = std::clamp(targetInjectedY - s.injected[1], -0.4f, 0.4f);

        const float kp = std::min(16.0f, 0.45f / dt);
        tx = tx_ff + kp * errX;
        ty = ty_ff + kp * errY;
    }

    constexpr float kLift = 0.12f;
    constexpr float kBlendRange = 0.010f;
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
        float lifted = std::min(1.0f, smoothBlend * (kLift + (1.0f - kLift) * m));
        x = s.x / m * lifted;
        y = s.y / m * lifted;
    } else {
        // In closed-loop aiming, emit lifted output >= kLift so fine aiming still moves the camera,
        // and integrate ONLY the effective deflection above deadzone into s.injected.
        float lifted = std::min(1.0f, kLift + (1.0f - kLift) * m);
        x = s.x / m * lifted;
        y = s.y / m * lifted;
    }

    if (s.aimingActive) {
        float outMag = std::hypot(x, y);
        if (outMag > kLift) {
            float effMag = std::clamp((outMag - kLift) / (1.0f - kLift), 0.0f, 1.0f);
            s.injected[0] += (x / outMag) * effMag * dt;
            s.injected[1] += (y / outMag) * effMag * dt;
        }
    }
}

}  // namespace xc::input
