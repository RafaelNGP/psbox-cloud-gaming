// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Standalone Single-Wheel Gyroscope Testing & Calibration Modal.
#pragma once

#include <cstdint>
#include "app/gyro_calibration.h"
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
        Preset = 0,     // the game's stick response, by name
        Calibrate,      // the sensor's drift
        AutoCalibrate,  // the game's response measured (in a game)
        InvertX,
        InvertY,
        Intensity,
        YawScale,
        PitchScale,
        CurveStart,     // the response's two numbers, for one of one's own
        CurveExp,
        ItemCount
    };

    GyroWheelDialog();
    ~GyroWheelDialog() = default;

    void open(int slot = 0, const std::string& gameKey = "");
    // For a game's profile being edited elsewhere (the game's settings screen):
    // opens on these values and saves nothing itself; the caller reads them
    // back through the getters when it closes.
    void openDetached(int slot, int level, int yawScale, int pitchScale, const float bias[3], int curveStart, int curveExp,
                      bool invertX, bool invertY);
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

    // Measuring the game's response (app/gyro_calibration.h). The dialog only
    // explains, asks and shows the result; whoever owns the stream runs the
    // sweep: it takes the request, and gives the result back.
    void setInGame(bool inGame) { inGame_ = inGame; }
    // The game the values are for: named beside a response of one's own, which
    // is then kept in that game's profile alone.
    void setGameName(const std::string& name) { gameName_ = name; }
    // Experimental, in a game: the dialog shrunk to a side panel while the game
    // keeps L2 and the gyro, to tune against the game itself (triangle).
    bool liveTest() const { return open_ && inGame_ && liveTest_ && page_ == Page::Main; }
    static constexpr int kCompactW = 620, kCompactH = 700;
    void drawCompact(Canvas& c, const Fonts& fonts);
    bool takeCalibrationRequest();
    bool calibrationRunning() const { return open_ && page_ == Page::Running; }
    void showCalibrationResult(const app::gyrocal::Result& result, bool cancelled);

    bool isCalibrating() const;
    float calibrationProgress() const;
    void startCalibration(int samples = 240);

private:
    void applyToSettings();
    int presetIndex() const;  // in input::kGyroPresets, -1: one's own numbers
    void stepPreset(int dir);
    void drawCalibrationPage(Canvas& c, const Fonts& fonts, const Rect& panel);
    // The rows at (rx, ry), each rowH high; returns the y after the last.
    int drawRows(Canvas& c, const Fonts& fonts, int rx, int ry, int rw, int rowH);
    Str helpFor(Item it) const;
    std::string gameName_;
    bool liveTest_ = false;
    enum class Page { Main, Prepare, Running, Result };
    Page page_ = Page::Main;
    bool inGame_ = false;
    bool calRequested_ = false;
    bool calCancelled_ = false;
    app::gyrocal::Result calResult_;
    void syncFromSettings();

    bool open_ = false;
    bool detached_ = false;  // openDetached(): nothing written to the settings
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
