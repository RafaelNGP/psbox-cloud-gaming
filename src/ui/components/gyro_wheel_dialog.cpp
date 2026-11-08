// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/components/gyro_wheel_dialog.h"
#include "app/settings_manager.h"
#include "ui/app_ui_internal.h"
#include "ui/pad_glyphs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
    selected_ = Item::Intensity;
    demoMode_ = false;
    calSuccess_ = false;
    calFailed_ = false;
    calDoneTime_ = 0;
    syncFromSettings();
}

void GyroWheelDialog::close() {
    open_ = false;
    demoMode_ = false;
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
        bias_[0] = prof.gyroBias[0];
        bias_[1] = prof.gyroBias[1];
        bias_[2] = prof.gyroBias[2];
    } else {
        app::Settings s = mgr.get();
        level_ = s.gyroLevel;
        yawScale_ = s.gyroYawScale;
        pitchScale_ = s.gyroPitchScale;
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

    auto& mgr = app::SettingsManager::instance();
    if (!gameKey_.empty() && mgr.hasCustomProfile(gameKey_)) {
        mgr.update([this](app::Settings& s) {
            auto& prof = s.perGame[gameKey_];
            prof.gyroLevel = level_;
            prof.gyroYawScale = yawScale_;
            prof.gyroPitchScale = pitchScale_;
            prof.gyroBias[0] = bias_[0];
            prof.gyroBias[1] = bias_[1];
            prof.gyroBias[2] = bias_[2];
        }, "gyro_dialog_profile");
    } else {
        mgr.update([this](app::Settings& s) {
            s.gyroLevel = level_;
            s.gyroYawScale = yawScale_;
            s.gyroPitchScale = pitchScale_;
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
            case Item::DemoMode:
                demoMode_ = !demoMode_;
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
            case Item::DemoMode:
                demoMode_ = !demoMode_;
                break;
            default:
                break;
        }
    }

    if (in.accept) {
        if (selected_ == Item::Calibrate) {
            startCalibration();
        } else if (selected_ == Item::DemoMode) {
            demoMode_ = !demoMode_;
        }
    }

    if (in.back) {
        close();
    }
}

void GyroWheelDialog::update(uint64_t nowMs) {
    if (!open_) return;
    lastNowMs_ = nowMs;

    if (demoMode_) {
        demoPhase_ += 0.05f;
        rawVector_[0] = std::sin(demoPhase_) * 0.45f;
        rawVector_[1] = std::cos(demoPhase_ * 1.3f) * 0.45f;
        rawVector_[2] = 0.0f;
    } else {
        input::pollPadRawGyro(slot_, rawVector_);
    }

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
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 180));

    // Modal panel
    int pw = (w > 0) ? w : 1120;
    int ph = (h > 0) ? h : 620;
    int px = (x > 0) ? x : (kW - pw) / 2;
    int py = (y > 0) ? y : (kH - ph) / 2 - 20;

    Rect panel{px, py, pw, ph};
    c.fillRect({panel.x + 8, panel.y + 12, panel.w, panel.h}, rgba(0, 0, 0, 140), 24); // shadow
    c.fillRect(panel, kPanel, 20);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 20);

    // Title
    fonts.bold.draw(c, tr(Str::Gyro), panel.x + 48, panel.y + 40, 36, kWhite);

    // -------------------------------------------------------------
    // LEFT SIDE: Single Test Wheel
    // -------------------------------------------------------------
    const float cx = static_cast<float>(panel.x + 230);
    const float cy = static_cast<float>(panel.y + 300);
    const float R = 150.0f;

    // Wheel background & concentric circles
    c.fillCircle(cx, cy, R, rgba(255, 255, 255, 12));
    c.strokeArc(cx, cy, R, 2.0f, 0, 6.2832f, rgba(255, 255, 255, 70));
    c.strokeArc(cx, cy, R * 0.5f, 1.0f, 0, 6.2832f, rgba(255, 255, 255, 25));

    // Reticle crosshair lines
    c.line(cx - R, cy, cx + R, cy, 1.0f, rgba(255, 255, 255, 30));
    c.line(cx, cy - R, cx, cy + R, 1.0f, rgba(255, 255, 255, 30));

    // Deadband threshold circle (15%)
    const float deadbandR = R * 0.15f;
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

    // Legend below wheel
    int ly = static_cast<int>(cy + R + 28);
    c.fillCircle(cx - 140.0f, ly + 9.0f, 7.0f, kRawSensorColor);
    fonts.regular.draw(c, "Posição do giroscópio", static_cast<int>(cx - 124.0f), ly, 18, kGray);

    c.fillCircle(cx + 40.0f, ly + 9.0f, 9.0f, kGameOutputColor);
    fonts.regular.draw(c, "O que o jogo recebe", static_cast<int>(cx + 56.0f), ly, 18, kGray);

    // -------------------------------------------------------------
    // RIGHT SIDE: Lateral Adjustment Sliders & Controls
    // -------------------------------------------------------------
    int rx = panel.x + 470;
    int rw = panel.w - 520;
    int ry = panel.y + 110;
    constexpr int rowH = 68;

    auto drawRow = [&](Item it, const std::string& label, const std::string& val, bool isBtn = false) {
        bool chosen = (selected_ == it);
        Rect itemBox{rx, ry, rw, rowH - 12};
        c.fillRect(itemBox, chosen ? kCustomFocusBg : rgba(255, 255, 255, 8), 12);
        if (chosen) {
            c.strokeRect(itemBox, kCustomFocusBorder, 2, 12);
        }

        fonts.semibold.draw(c, label, itemBox.x + 20, fonts.semibold.centeredY(itemBox.y, itemBox.h, 22), 22,
                            chosen ? kWhite : kGray);

        if (!val.empty()) {
            if (isBtn) {
                int vw = fonts.bold.measure(val, 20);
                fonts.bold.draw(c, val, itemBox.x + itemBox.w - vw - 24,
                                fonts.bold.centeredY(itemBox.y, itemBox.h, 20), 20,
                                chosen ? kWhite : kGray);
            } else {
                std::string controlStr = "‹  " + val + "  ›";
                int vw = fonts.bold.measure(controlStr, 22);
                fonts.bold.draw(c, controlStr, itemBox.x + itemBox.w - vw - 20,
                                fonts.bold.centeredY(itemBox.y, itemBox.h, 22), 22,
                                chosen ? kGameOutputColor : kWhite);
            }
        }
        ry += rowH;
    };

    // Item 0: Sensitivity (1..10)
    drawRow(Item::Intensity, "Sensibilidade (Intensidade)", fmt("%d / 10", level_));

    // Item 1: Horizontal (Yaw) scale
    drawRow(Item::YawScale, "Sensibilidade Horizontal (Yaw)", fmt("%d %%", yawScale_));

    // Item 2: Vertical (Pitch) scale
    drawRow(Item::PitchScale, "Sensibilidade Vertical (Pitch)", fmt("%d %%", pitchScale_));

    // Item 3: Calibration button / progress
    std::string calLabel = "Calibrar Giroscópio (Parado)";
    if (isCalibrating()) {
        int pct = static_cast<int>(calibrationProgress() * 100.0f);
        calLabel = fmt("Calibrando... %d %%", pct);
    } else if (calSuccess_) {
        calLabel = "Calibrado com Sucesso!";
    } else if (calFailed_) {
        calLabel = "Falhou: Mantenha imóvel";
    }
    drawRow(Item::Calibrate, "Calibração Estática de Drift", calLabel, true);

    // If calibrating, show a dedicated progress bar below
    if (isCalibrating()) {
        Rect barBox{rx + 20, ry - 16, rw - 40, 8};
        c.fillRect(barBox, rgba(255, 255, 255, 30), 4);
        int fw = static_cast<int>(barBox.w * calibrationProgress());
        c.fillRect({barBox.x, barBox.y, fw, barBox.h}, kGameOutputColor, 4);
    }

    // Item 4: Demo / Auto-movement toggle
    drawRow(Item::DemoMode, "Modo Demonstração (Automático)", demoMode_ ? "ATIVADO" : "DESATIVADO");

    // Help text at bottom
    fonts.regular.draw(c, "Mova o controle para testar; use as setas para ajustar sensibilidade horizontal/vertical.",
                       panel.x + 48, panel.y + panel.h - 70, 18, kDim);

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
    const char* actLabel = (selected_ == Item::Calibrate || selected_ == Item::DemoMode) ? tr(Str::Select) : tr(Str::Adjust);
    drawHint(PadButton::Cross, actLabel);
}

}  // namespace xc::ui
