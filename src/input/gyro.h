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
enum GyroMode {
    kGyroOff = 0,
    kGyroAlways = 1,        // Gyro Aiming: right stick (Always)
    kGyroAiming = 2,        // Gyro Aiming: right stick (while L2 is held)
    kGyroSteering = 3,      // Steering Wheel: left stick X (Always)
    kGyroSteeringDrive = 4, // Steering Wheel: left stick X (while R2 is held)
};
constexpr int kGyroModeCount = 5;

struct GyroState {
    float x = 0, y = 0;            // the push given, eased (normalized effective drive)
    float steerX = 0;              // steering wheel eased deflection (-1..1)
    float steerAngle = 0;          // complementary filter integrated steering roll angle (rad)
    float up[3] = {0, 1, 0};       // the room's up in the pad's axes, eased
    float baseUp[3] = {0, 1, 0};   // room's up in anchor body axes
    float bias[3] = {0, 0, 0};     // zero-drift static calibration offset
    float yawScale = 1.0f;         // decoupled horizontal multiplier
    float pitchScale = 0.8f;       // decoupled vertical multiplier
    bool upInitialized = false;    // fast initial convergence for gravity vector
    bool baseUpInitialized = false; // whether baseUp captured from initial valid accel
    bool aimingActive = false;     // whether posture tracking is currently anchored (L2 aim)
    bool hasBaseQuat = false;      // whether baseQuat was captured from hardware
    float baseQuat[4] = {0, 0, 0, 1}; // reference orientation quat at aim start (x, y, z, w)
    float softQuat[4] = {0, 0, 0, 1}; // software SO(3) tracked posture quat when no hardware quat
    float targetAngle[2] = {0, 0}; // cumulative target angle: [0] yaw, [1] pitch (rad)
    float injected[2] = {0, 0};    // cumulative effective camera stick displacement: [0] x (yaw), [1] y (pitch)
};

// Anchors the reference orientation (and initial gravity up vector if provided) for closed-loop posture tracking.
void anchorGyroPosture(GyroState& s, const float* orientationQuat = nullptr, const float* accel = nullptr);

// The same while the aim is already on and the thumb is turning the camera too: where the pad
// is now becomes the reference (the thumb's turn isn't the hand's to make up), but the push the
// hand is giving and the eased vertical stay. Anchoring afresh on every reading zeroed the eased
// push each time, so the gyro gave only a fraction of its turn while the thumb was on the stick.
void rebaseGyroPosture(GyroState& s, const float* orientationQuat = nullptr);

// Resets posture tracking state when aiming ends.
void resetGyroPosture(GyroState& s);

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
// `orientationQuat`: optional hardware-fused quaternion [x, y, z, w] (nullptr for software fallback).
// `dt`: polling time step in seconds (defaults to 0.004f = 250 Hz).
void gyroToStick(const float rawW[3], const float* accel, int level, GyroState& s, float& x, float& y,
                 const float* orientationQuat = nullptr, float dt = 0.004f);

// `rawW`: the pad's angular velocity (rad/s; x pitch, y yaw, z roll), `accel`
// what its accelerometer reads (g) -> steering deflection to add to left stick X
// (pad convention: -1.0 full left, +1.0 full right).
float gyroToSteering(const float rawW[3], const float* accel, int level, GyroState& s,
                     const float* orientationQuat = nullptr, float dt = 0.004f);

// How the game turns the right stick into camera speed: nothing up to
// `start`, then ((level - start) / (1 - start)) ^ `exponent` of its top speed.
// Gyro aiming sends the stick level that gives the camera speed the hand's
// turn calls for. The default (12 %, a straight line) is what it assumed
// before this could be set. Measured in Fortnite aiming down sights
// (2026-11-11, per tile of the picture): 15 %, exponent 2.4. `invertX` / `invertY` turn the aim the
// other way on that axis.
struct GyroResponse {
    float start = 0.12f;
    float exponent = 1.0f;
    bool invertX = false, invertY = false;
};
void setGyroResponse(const GyroResponse& response);
GyroResponse gyroResponse();
// The curves offered by name (start in percent, exponent in tenths).
struct GyroPreset {
    const char* id;  // "linear", "smooth", "exponential", "fortnite"
    int startPct, exponentTenths;
};
constexpr GyroPreset kGyroPresets[] = {{"linear", 12, 10}, {"smooth", 15, 15}, {"exponential", 20, 20}, {"fortnite", 15, 24}};
constexpr int kGyroCurveStartMin = 0, kGyroCurveStartMax = 40, kGyroCurveExpMin = 10, kGyroCurveExpMax = 40;

// Global gyro calibration & hardware probing
void setGlobalGyroCalibration(const float bias[3], float yawScale, float pitchScale);
void getGlobalGyroCalibration(float bias[3], float& yawScale, float& pitchScale);
void resetGlobalGyroCalibration();
bool pollPadRawGyro(int padIndex, float outGyro[3]);

}  // namespace xc::input
