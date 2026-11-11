// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/components/gyro_wheel_dialog.h"
#include "app/settings_manager.h"
#include "ui/app_ui_internal.h"
#include "ui/pad_glyphs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace xc::ui {

namespace {

constexpr Color kCustomFocusBg = rgba(255, 255, 255, 28);
constexpr Color kCustomFocusBorder = rgba(74, 222, 128, 200);
constexpr Color kRawSensorColor = rgba(170, 170, 170);
constexpr Color kGameOutputColor = rgba(74, 222, 128);

template <typename... Args>
std::string fmt(const char* format, Args... args) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), format, args...);
    return std::string(buf);
}

}  // namespace

GyroWheelDialog::GyroWheelDialog() {
    syncFromSettings();
}

void GyroWheelDialog::open(int slot, const std::string& gameKey) {
    open_ = true;
    slot_ = slot;
    gameKey_ = gameKey;
    selected_ = Item::Calibrate;
    calSuccess_ = false;
    calFailed_ = false;
    calDoneTime_ = 0;
    syncFromSettings();
}

void GyroWheelDialog::close() {
    open_ = false;
    calibrator_.reset();
}

void GyroWheelDialog::setLevel(int lvl) {
    level_ = std::clamp(lvl, 1, 10);
    applyToSettings();
}

void GyroWheelDialog::setYawScale(int s) {
    yawScale_ = std::clamp(s, 20, 200);
    applyToSettings();
}

void GyroWheelDialog::setPitchScale(int s) {
    pitchScale_ = std::clamp(s, 20, 200);
    applyToSettings();
}

void GyroWheelDialog::setCurve(int startPct, int exponentTenths) {
    curveStart_ = std::clamp(startPct, input::kGyroCurveStartMin, input::kGyroCurveStartMax);
    curveExp_ = std::clamp(exponentTenths, input::kGyroCurveExpMin, input::kGyroCurveExpMax);
    applyToSettings();
}

void GyroWheelDialog::setInvert(bool x, bool y) {
    invertX_ = x;
    invertY_ = y;
    applyToSettings();
}

int GyroWheelDialog::presetIndex() const {
    for (int i = 0; i < static_cast<int>(std::size(input::kGyroPresets)); ++i)
        if (input::kGyroPresets[i].startPct == curveStart_ && input::kGyroPresets[i].exponentTenths == curveExp_) return i;
    return -1;
}

void GyroWheelDialog::stepPreset(int dir) {
    const int n = static_cast<int>(std::size(input::kGyroPresets));
    const int cur = presetIndex();
    const int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : (cur + dir + n) % n;
    // Through a curve the same turn of the hand moves the aim much further at
    // low speeds than the straight line did: start from a gentle intensity.
    const bool wasLinear = curveExp_ == 10, isLinear = input::kGyroPresets[next].exponentTenths == 10;
    if (wasLinear && !isLinear && level_ > 3) level_ = 3;
    curveStart_ = input::kGyroPresets[next].startPct;
    curveExp_ = input::kGyroPresets[next].exponentTenths;
    applyToSettings();
}

void GyroWheelDialog::setBias(const float b[3]) {
    if (b) {
        bias_[0] = b[0];
        bias_[1] = b[1];
        bias_[2] = b[2];
        applyToSettings();
    }
}

bool GyroWheelDialog::isCalibrating() const {
    return calibrator_.state == input::GyroCalibrator::State::Calibrating;
}

float GyroWheelDialog::calibrationProgress() const {
    return calibrator_.progress();
}

void GyroWheelDialog::startCalibration(int samples) {
    calibrator_.start(samples);
    calSuccess_ = false;
    calFailed_ = false;
    calDoneTime_ = 0;
}

void GyroWheelDialog::syncFromSettings() {
    auto& mgr = app::SettingsManager::instance();
    if (!gameKey_.empty() && mgr.hasCustomProfile(gameKey_)) {
        app::GameProfile prof = mgr.getProfile(gameKey_, gameKey_);
        level_ = prof.gyroLevel;
        yawScale_ = prof.gyroYawScale;
        pitchScale_ = prof.gyroPitchScale;
        curveStart_ = prof.gyroCurveStart;
        curveExp_ = prof.gyroCurveExp;
        invertX_ = prof.gyroInvertX;
        invertY_ = prof.gyroInvertY;
        bias_[0] = prof.gyroBias[0];
        bias_[1] = prof.gyroBias[1];
        bias_[2] = prof.gyroBias[2];
    } else {
        app::Settings s = mgr.get();
        level_ = s.gyroLevel;
        yawScale_ = s.gyroYawScale;
        pitchScale_ = s.gyroPitchScale;
        curveStart_ = s.gyroCurveStart;
        curveExp_ = s.gyroCurveExp;
        invertX_ = s.gyroInvertX;
        invertY_ = s.gyroInvertY;
        bias_[0] = s.gyroBias[0];
        bias_[1] = s.gyroBias[1];
        bias_[2] = s.gyroBias[2];
    }
    gyroState_.yawScale = yawScale_ / 100.0f;
    gyroState_.pitchScale = pitchScale_ / 100.0f;
    gyroState_.bias[0] = bias_[0];
    gyroState_.bias[1] = bias_[1];
    gyroState_.bias[2] = bias_[2];
}

void GyroWheelDialog::applyToSettings() {
    gyroState_.yawScale = yawScale_ / 100.0f;
    gyroState_.pitchScale = pitchScale_ / 100.0f;
    gyroState_.bias[0] = bias_[0];
    gyroState_.bias[1] = bias_[1];
    gyroState_.bias[2] = bias_[2];

    input::setGlobalGyroCalibration(bias_, gyroState_.yawScale, gyroState_.pitchScale);
    input::setGyroResponse({curveStart_ / 100.0f, curveExp_ / 10.0f, invertX_, invertY_});

    auto& mgr = app::SettingsManager::instance();
    if (!gameKey_.empty() && mgr.hasCustomProfile(gameKey_)) {
        mgr.update([this](app::Settings& s) {
            auto& prof = s.perGame[gameKey_];
            prof.gyroLevel = level_;
            prof.gyroYawScale = yawScale_;
            prof.gyroPitchScale = pitchScale_;
            prof.gyroCurveStart = curveStart_;
            prof.gyroCurveExp = curveExp_;
            prof.gyroInvertX = invertX_;
            prof.gyroInvertY = invertY_;
            prof.gyroBias[0] = bias_[0];
            prof.gyroBias[1] = bias_[1];
            prof.gyroBias[2] = bias_[2];
        }, "gyro_dialog_profile");
    } else {
        mgr.update([this](app::Settings& s) {
            s.gyroLevel = level_;
            s.gyroYawScale = yawScale_;
            s.gyroPitchScale = pitchScale_;
            s.gyroCurveStart = curveStart_;
            s.gyroCurveExp = curveExp_;
            s.gyroInvertX = invertX_;
            s.gyroInvertY = invertY_;
            s.gyroBias[0] = bias_[0];
            s.gyroBias[1] = bias_[1];
            s.gyroBias[2] = bias_[2];
        }, "gyro_dialog_global");
    }
}

void GyroWheelDialog::handleInput(const NavInput& in) {
    if (!open_) return;

    if (in.up) {
        int idx = static_cast<int>(selected_) - 1;
        if (idx < 0) idx = static_cast<int>(Item::ItemCount) - 1;
        selected_ = static_cast<Item>(idx);
    } else if (in.down) {
        int idx = static_cast<int>(selected_) + 1;
        if (idx >= static_cast<int>(Item::ItemCount)) idx = 0;
        selected_ = static_cast<Item>(idx);
    }

    if (in.left) {
        switch (selected_) {
            case Item::Intensity:
                level_ = std::max(1, level_ - 1);
                applyToSettings();
                break;
            case Item::YawScale:
                yawScale_ = std::max(20, yawScale_ - 10);
                applyToSettings();
                break;
            case Item::PitchScale:
                pitchScale_ = std::max(20, pitchScale_ - 10);
                applyToSettings();
                break;
            case Item::Preset:
                stepPreset(-1);
                break;
            case Item::CurveStart:
                setCurve(curveStart_ - 1, curveExp_);
                break;
            case Item::CurveExp:
                setCurve(curveStart_, curveExp_ - 1);
                break;
            case Item::InvertX:
                setInvert(!invertX_, invertY_);
                break;
            case Item::InvertY:
                setInvert(invertX_, !invertY_);
                break;
            default:
                break;
        }
    } else if (in.right) {
        switch (selected_) {
            case Item::Intensity:
                level_ = std::min(10, level_ + 1);
                applyToSettings();
                break;
            case Item::YawScale:
                yawScale_ = std::min(200, yawScale_ + 10);
                applyToSettings();
                break;
            case Item::PitchScale:
                pitchScale_ = std::min(200, pitchScale_ + 10);
                applyToSettings();
                break;
            case Item::Preset:
                stepPreset(1);
                break;
            case Item::CurveStart:
                setCurve(curveStart_ + 1, curveExp_);
                break;
            case Item::CurveExp:
                setCurve(curveStart_, curveExp_ + 1);
                break;
            case Item::InvertX:
                setInvert(!invertX_, invertY_);
                break;
            case Item::InvertY:
                setInvert(invertX_, !invertY_);
                break;
            default:
                break;
        }
    }

    if (in.accept) {
        if (selected_ == Item::Calibrate) {
            startCalibration();
        } else if (selected_ == Item::InvertX) {
            setInvert(!invertX_, invertY_);
        } else if (selected_ == Item::InvertY) {
            setInvert(invertX_, !invertY_);
        }
    }

    if (in.back) {
        close();
    }
}

void GyroWheelDialog::update(uint64_t nowMs) {
    if (!open_) return;
    lastNowMs_ = nowMs;

    input::pollPadRawGyro(slot_, rawVector_);

    if (calibrator_.state == input::GyroCalibrator::State::Calibrating) {
        bool done = calibrator_.feed(rawVector_);
        if (done) {
            bias_[0] = calibrator_.resultBias[0];
            bias_[1] = calibrator_.resultBias[1];
            bias_[2] = calibrator_.resultBias[2];
            calSuccess_ = true;
            calFailed_ = false;
            calDoneTime_ = nowMs;
            applyToSettings();
        } else if (calibrator_.state == input::GyroCalibrator::State::Failed) {
            calSuccess_ = false;
            calFailed_ = true;
            calDoneTime_ = nowMs;
        }
    }

    float accel[3] = {0.0f, 1.0f, 0.0f};
    gyroState_.yawScale = yawScale_ / 100.0f;
    gyroState_.pitchScale = pitchScale_ / 100.0f;
    gyroState_.bias[0] = bias_[0];
    gyroState_.bias[1] = bias_[1];
    gyroState_.bias[2] = bias_[2];
    input::gyroToStick(rawVector_, accel, level_, gyroState_, outStick_[0], outStick_[1]);
}

void GyroWheelDialog::draw(Canvas& c, const Fonts& fonts, int x, int y, int w, int h) {
    if (!open_) return;

    // Full screen darkening overlay
    constexpr int kW = 1920, kH = 1080;
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 160));

    (void)x; (void)y; (void)w; (void)h;

    // Modal panel
    constexpr int modalW = 1140, modalH = 720;
    Rect panel{(kW - modalW) / 2, (kH - modalH) / 2 - 30, modalW, modalH};
    c.fillRect({panel.x + 8, panel.y + 12, panel.w, panel.h}, rgba(0, 0, 0, 120), 26); // shadow
    c.fillRect(panel, kPanel, 24);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 24);

    // Title
    fonts.bold.draw(c, tr(Str::Gyro), panel.x + 48, panel.y + 40, 36, kWhite);

    // -------------------------------------------------------------
    // LEFT SIDE: Single Test Wheel
    // -------------------------------------------------------------
    const float cx = static_cast<float>(panel.x + 230);
    const float cy = static_cast<float>(panel.y + 285);
    const float R = 145.0f;

    // Wheel background & concentric circles
    c.fillCircle(cx, cy, R, rgba(255, 255, 255, 12));
    c.strokeArc(cx, cy, R, 2.0f, 0, 6.2832f, rgba(255, 255, 255, 70));
    c.strokeArc(cx, cy, R * 0.5f, 1.0f, 0, 6.2832f, rgba(255, 255, 255, 25));

    // Reticle crosshair lines
    c.line(cx - R, cy, cx + R, cy, 1.0f, rgba(255, 255, 255, 30));
    c.line(cx, cy - R, cx, cy + R, 1.0f, rgba(255, 255, 255, 30));

    // Deadband threshold circle (15%)
    const float deadbandR = R * (curveStart_ / 100.0f);
    c.fillCircle(cx, cy, deadbandR, rgba(230, 70, 70, 40));
    c.strokeArc(cx, cy, deadbandR, 1.5f, 0, 6.2832f, rgba(230, 90, 90, 140));

    // Vector calculations
    // Raw angular velocity vector (clamped to circle edge)
    float rawLen = std::hypot(rawVector_[1], rawVector_[0]);
    float rawNorm = std::min(1.0f, rawLen * 1.5f);
    float rawAngle = (rawLen > 0.001f) ? std::atan2(rawVector_[0], rawVector_[1]) : 0.0f;
    float rawDotX = cx + std::cos(rawAngle) * rawNorm * R;
    float rawDotY = cy + std::sin(rawAngle) * rawNorm * R;

    // Calibrated game stick vector
    float outLen = std::hypot(outStick_[0], outStick_[1]);
    float outNorm = std::min(1.0f, outLen);
    float outAngle = (outLen > 0.001f) ? std::atan2(outStick_[1], outStick_[0]) : 0.0f;
    float outDotX = cx + std::cos(outAngle) * outNorm * R;
    float outDotY = cy + std::sin(outAngle) * outNorm * R;

    // Draw vector lines from center
    c.line(cx, cy, rawDotX, rawDotY, 1.5f, rgba(170, 170, 170, 120));
    c.line(cx, cy, outDotX, outDotY, 2.0f, rgba(74, 222, 128, 160));

    // Draw Dots
    c.fillCircle(rawDotX, rawDotY, 9.0f, kRawSensorColor);
    c.fillCircle(outDotX, outDotY, 12.0f, kGameOutputColor);

    // Legend below wheel (stacked vertically to prevent text collision across languages)
    int ly1 = static_cast<int>(cy + R + 22);
    int ly2 = ly1 + 26;
    float legX = cx - 120.0f;
    c.fillCircle(legX, ly1 + 9.0f, 7.0f, kRawSensorColor);
    fonts.regular.draw(c, tr(Str::GyroSensorPos), static_cast<int>(legX + 16.0f), ly1, 18, kGray);

    c.fillCircle(legX, ly2 + 9.0f, 8.0f, kGameOutputColor);
    fonts.regular.draw(c, tr(Str::GyroGameOutput), static_cast<int>(legX + 16.0f), ly2, 18, kGray);

    // -------------------------------------------------------------
    // RIGHT SIDE: Lateral Adjustment Sliders & Controls
    // -------------------------------------------------------------
    int rx = panel.x + 460;
    int rw = panel.w - 508;
    int ry = panel.y + 108;
    constexpr int rowH = 56;

    auto drawRow = [&](Item it, const std::string& label, const std::string& val, bool isBtn = false) {
        bool chosen = (selected_ == it);
        Rect itemBox{rx, ry, rw, rowH - 12};
        c.fillRect(itemBox, chosen ? kCustomFocusBg : rgba(255, 255, 255, 8), 12);
        if (chosen) {
            c.strokeRect(itemBox, kCustomFocusBorder, 2, 12);
        }

        fonts.semibold.draw(c, label, itemBox.x + 20, fonts.semibold.centeredY(itemBox.y, itemBox.h, 20), 20,
                            chosen ? kWhite : kGray);

        if (!val.empty()) {
            if (isBtn) {
                int vw = fonts.bold.measure(val, 20);
                fonts.bold.draw(c, val, itemBox.x + itemBox.w - vw - 24,
                                fonts.bold.centeredY(itemBox.y, itemBox.h, 20), 20,
                                chosen ? kWhite : kGray);
            } else {
                std::string controlStr = "‹  " + val + "  ›";
                int vw = fonts.bold.measure(controlStr, 20);
                fonts.bold.draw(c, controlStr, itemBox.x + itemBox.w - vw - 20,
                                fonts.bold.centeredY(itemBox.y, itemBox.h, 20), 20,
                                chosen ? kGameOutputColor : kWhite);
            }
        }
        ry += rowH;
    };

    // Row 0: Calibration button / progress
    std::string calLabel = tr(Str::GyroCalibrateAction);
    if (isCalibrating()) {
        int pct = static_cast<int>(calibrationProgress() * 100.0f);
        calLabel = trf(Str::GyroCalibratingPct, std::to_string(pct));
    } else if (calSuccess_) {
        calLabel = tr(Str::GyroCalibratedSuccessShort);
    } else if (calFailed_) {
        calLabel = tr(Str::GyroCalibratedFailedShort);
    }
    drawRow(Item::Calibrate, tr(Str::GyroCalibration), calLabel, true);

    // If calibrating, show a dedicated progress bar below
    if (isCalibrating()) {
        Rect barBox{rx + 20, ry - 14, rw - 40, 5};
        c.fillRect(barBox, rgba(255, 255, 255, 30), 4);
        int fw = static_cast<int>(barBox.w * calibrationProgress());
        c.fillRect({barBox.x, barBox.y, fw, barBox.h}, kGameOutputColor, 4);
    }

    // Row 1: Sensitivity (1..10)
    drawRow(Item::Intensity, tr(Str::GyroSensitivityIntensity), fmt("%d / 10", level_));

    // Row 2: Horizontal (Yaw) scale
    drawRow(Item::YawScale, tr(Str::GyroYawSensitivity), fmt("%d %%", yawScale_));

    // Row 3: Vertical (Pitch) scale
    drawRow(Item::PitchScale, tr(Str::GyroPitchSensitivity), fmt("%d %%", pitchScale_));

    // Rows 4-6: the game's stick response, by name or by its two numbers
    static constexpr Str kPresetNames[] = {Str::GyroPresetLinear, Str::GyroPresetSmooth, Str::GyroPresetExponential};
    const int preset = presetIndex();
    const std::string presetName = preset < 0 ? tr(Str::GyroPresetCustom)
                                   : preset < static_cast<int>(std::size(kPresetNames)) ? tr(kPresetNames[preset])
                                                                                        : "Fortnite";
    drawRow(Item::Preset, tr(Str::GyroGameResponse), presetName);
    drawRow(Item::CurveStart, tr(Str::GyroCurveStart), fmt("%d %%", curveStart_));
    drawRow(Item::CurveExp, tr(Str::GyroCurveShape), fmt("%d.%d", curveExp_ / 10, curveExp_ % 10));

    // Rows 7-8: the aim turned the other way
    drawRow(Item::InvertX, tr(Str::GyroInvertX), tr(invertX_ ? Str::On : Str::Off));
    drawRow(Item::InvertY, tr(Str::GyroInvertY), tr(invertY_ ? Str::On : Str::Off));

    // The setup in one line, to write down and repeat in another game.
    {
        const int nx = panel.x + 48, ny = static_cast<int>(cy + R + 92);
        fonts.semibold.draw(c, tr(Str::GyroYourNumbers), nx, ny, 18, kGray);
        fonts.bold.draw(c, fmt("%d %% · %d.%d · %d/10 · H %d %% · V %d %%", curveStart_, curveExp_ / 10, curveExp_ % 10, level_,
                               yawScale_, pitchScale_),
                        nx, ny + 26, 20, kWhite);
    }

    // Help text at bottom
    fonts.regular.draw(c, tr(Str::GyroTestHelp),
                       panel.x + 48, panel.y + panel.h - 44, 18, kDim);

    // Footer Hints
    c.fillRect({0, kH - 64, kW, 64}, kBg);
    int hx = panel.x + panel.w - 50;
    int hy = kH - 46;

    auto drawHint = [&](PadButton b, const char* label) {
        int lw = fonts.semibold.measure(label, 20);
        hx -= lw;
        fonts.semibold.draw(c, label, hx, hy, 20, kGray);
        hx -= 36;
        drawFaceButton(c, fonts.bold, g_glyphStyle, b, static_cast<float>(hx + 14), static_cast<float>(hy + 12), 13.0f);
        hx -= 32;
    };

    drawHint(PadButton::Circle, tr(Str::Back));
    const char* actLabel = (selected_ == Item::Calibrate) ? tr(Str::Select) : tr(Str::Adjust);
    drawHint(PadButton::Cross, actLabel);
}

}  // namespace xc::ui
