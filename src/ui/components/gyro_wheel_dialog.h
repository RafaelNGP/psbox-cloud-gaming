// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Standalone Single-Wheel Gyroscope Testing & Calibration Modal.
#pragma once

#include "input/gyro.h"
#include "ui/app_ui.h"
#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/strings.h"

#include <string>

namespace xc::ui {

class GyroWheelDialog {
public:
    enum class Item {
        Calibrate = 0,
        Intensity,
        YawScale,
        PitchScale,
        Preset,      // the game's stick response, by name
        CurveStart,  // its two numbers, for a response of one's own
        CurveExp,
        InvertX,
        InvertY,
        ItemCount
    };

    GyroWheelDialog();
    ~GyroWheelDialog() = default;

    void open(int slot = 0, const std::string& gameKey = "");
    void close();
    bool isOpen() const { return open_; }

    void handleInput(const NavInput& in);
    void update(uint64_t nowMs);
    void draw(Canvas& c, const Fonts& fonts, int x = 0, int y = 0, int w = 0, int h = 0);

    // Getters / Setters
    int level() const { return level_; }
    void setLevel(int lvl);

    int yawScale() const { return yawScale_; }
    void setYawScale(int s);

    int pitchScale() const { return pitchScale_; }
    void setPitchScale(int s);

    int curveStart() const { return curveStart_; }
    int curveExp() const { return curveExp_; }
    bool invertX() const { return invertX_; }
    bool invertY() const { return invertY_; }
    void setCurve(int startPct, int exponentTenths);
    void setInvert(bool x, bool y);

    const float* bias() const { return bias_; }
    void setBias(const float b[3]);

    bool isCalibrating() const;
    float calibrationProgress() const;
    void startCalibration(int samples = 240);

private:
    void applyToSettings();
    int presetIndex() const;  // in input::kGyroPresets, -1: one's own numbers
    void stepPreset(int dir);
    void syncFromSettings();

    bool open_ = false;
    int slot_ = 0;
    std::string gameKey_;

    int level_ = 5;         // 1..10
    int yawScale_ = 100;    // 20..200%
    int pitchScale_ = 80;   // 20..200%
    float bias_[3] = {0.0f, 0.0f, 0.0f};
    int curveStart_ = 12;   // percent of the stick (input::GyroResponse::start)
    int curveExp_ = 10;     // tenths (input::GyroResponse::exponent)
    bool invertX_ = false, invertY_ = false;

    Item selected_ = Item::Calibrate;
    input::GyroCalibrator calibrator_;
    input::GyroState gyroState_{};

    float rawVector_[3] = {0.0f, 0.0f, 0.0f};
    float outStick_[2] = {0.0f, 0.0f};

    uint64_t calDoneTime_ = 0;
    bool calSuccess_ = false;
    bool calFailed_ = false;

    uint64_t lastNowMs_ = 0;
};

}  // namespace xc::ui
