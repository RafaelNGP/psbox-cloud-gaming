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
    detached_ = false;
    slot_ = slot;
    gameKey_ = gameKey;
    selected_ = Item::Preset;
    page_ = Page::Main;
    calRequested_ = false;
    calSuccess_ = false;
    calFailed_ = false;
    calDoneTime_ = 0;
    syncFromSettings();
}

void GyroWheelDialog::openDetached(int slot, int level, int yawScale, int pitchScale, const float bias[3], int curveStart,
                                   int curveExp, bool invertX, bool invertY) {
    open_ = true;
    detached_ = true;
    slot_ = slot;
    gameKey_.clear();
    selected_ = Item::Preset;
    page_ = Page::Main;
    calRequested_ = false;
    calSuccess_ = false;
    calFailed_ = false;
    calDoneTime_ = 0;
    level_ = std::clamp(level, 1, 10);
    yawScale_ = std::clamp(yawScale, 20, 200);
    pitchScale_ = std::clamp(pitchScale, 20, 200);
    for (int i = 0; i < 3; ++i) bias_[i] = bias ? bias[i] : 0.0f;
    curveStart_ = std::clamp(curveStart, input::kGyroCurveStartMin, input::kGyroCurveStartMax);
    curveExp_ = std::clamp(curveExp, input::kGyroCurveExpMin, input::kGyroCurveExpMax);
    invertX_ = invertX;
    invertY_ = invertY;
    applyToSettings();  // the test wheel answers with these values
}

void GyroWheelDialog::close() {
    open_ = false;
    liveTest_ = false;
    page_ = Page::Main;
    calRequested_ = false;
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

bool GyroWheelDialog::takeCalibrationRequest() {
    if (!calRequested_) return false;
    calRequested_ = false;
    return true;
}

void GyroWheelDialog::showCalibrationResult(const app::gyrocal::Result& result, bool cancelled) {
    calResult_ = result;
    calCancelled_ = cancelled;
    page_ = Page::Result;
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
    if (detached_) return;

    auto& mgr = app::SettingsManager::instance();
    // A response of one's own belongs to the game it was tuned in: its profile, made here if it has none.
    if (!gameKey_.empty() && (mgr.hasCustomProfile(gameKey_) || presetIndex() < 0)) {
        mgr.update([this](app::Settings& s) {
            auto it = s.perGame.find(gameKey_);
            if (it == s.perGame.end()) {
                app::GameProfile fresh = s.profileForGame(gameKey_, gameKey_);
                if (fresh.gameName.empty()) fresh.gameName = gameName_;
                // A new profile turns trophy sync off for its game unless told otherwise (and the
                // trophy thread then removes that game's trophies): keep what applied until now.
                fresh.syncTrophies = s.syncTrophies;
                fresh.syncTrophiesChosen = false;
                it = s.perGame.emplace(gameKey_, fresh).first;
            }
            auto& prof = it->second;
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
    if (page_ == Page::Running) return;  // the sweep has the pad; any button ends it there
    if (page_ == Page::Prepare) {
        if (in.accept && inGame_) {
            page_ = Page::Running;
            calRequested_ = true;
        } else if (in.back) {
            page_ = Page::Main;
        }
        return;
    }
    if (page_ == Page::Result) {
        if (in.accept && calResult_.ok() && !calCancelled_) {
            // As picking a curve by name does: a gentle intensity to start from.
            if (curveExp_ == 10 && calResult_.exponentTenths != 10 && level_ > 3) level_ = 3;
            setCurve(calResult_.startPct, calResult_.exponentTenths);
            page_ = Page::Main;
        } else if (in.accept) {
            page_ = Page::Prepare;  // try again
        } else if (in.back) {
            page_ = Page::Main;
        }
        return;
    }

    if (in.triangle && inGame_) liveTest_ = !liveTest_;

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
        } else if (selected_ == Item::AutoCalibrate) {
            page_ = Page::Prepare;
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

    if (page_ != Page::Main) {
        drawCalibrationPage(c, fonts, panel);
        return;
    }

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

    // The rows, and under them what the chosen one does.
    const int rx = panel.x + 460, rw = panel.w - 508;
    int ry = drawRows(c, fonts, rx, panel.y + 96, rw, 50);
    ry += 6;
    for (const std::string& line : fonts.regular.wrap(tr(helpFor(selected_)), 18, rw - 8, 3)) {
        fonts.regular.draw(c, line, rx + 4, ry, 18, kGray);
        ry += 26;
    }

    // The setup in one line, to write down and repeat in another game.
    {
        const int nx = panel.x + 48, ny = static_cast<int>(cy + R + 92);
        fonts.semibold.draw(c, tr(Str::GyroYourNumbers), nx, ny, 18, kGray);
        fonts.bold.draw(c, fmt("%d %% · %d.%d · %d/10 · H %d %% · V %d %%", curveStart_, curveExp_ / 10, curveExp_ % 10, level_,
                               yawScale_, pitchScale_),
                        nx, ny + 26, 20, kWhite);
    }


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
    if (inGame_) drawHint(PadButton::Triangle, tr(Str::GyroLiveTest));
    const char* actLabel = (selected_ == Item::Calibrate || selected_ == Item::AutoCalibrate) ? tr(Str::Select) : tr(Str::Adjust);
    drawHint(PadButton::Cross, actLabel);
}

void GyroWheelDialog::drawCalibrationPage(Canvas& c, const Fonts& fonts, const Rect& panel) {
    constexpr int kW = 1920, kH = 1080;
    const int x = panel.x + 48, w = panel.w - 96;
    int y = panel.y + 40;
    fonts.bold.draw(c, tr(Str::GyroAutoCalTitle), x, y, 36, kWhite);
    y += 76;
    auto paragraph = [&](const std::string& text, int px, Color color, int gap) {
        for (const std::string& line : fonts.regular.wrap(text, px, w, 4)) {
            fonts.regular.draw(c, line, x, y, px, color);
            y += px + 10;
        }
        y += gap;
    };
    const char* accept = nullptr;
    if (page_ == Page::Prepare) {
        paragraph(tr(Str::GyroAutoCalIntro), 24, kWhite, 22);
        for (Str step : {Str::GyroAutoCalStep1, Str::GyroAutoCalStep2, Str::GyroAutoCalStep3, Str::GyroAutoCalStep4})
            paragraph(tr(step), 22, kGray, 8);
        if (inGame_) {
            accept = tr(Str::GyroAutoCalStart);
        } else {
            y += 16;
            paragraph(tr(Str::GyroAutoCalOnlyInGame), 22, kGameOutputColor, 0);
        }
    } else if (page_ == Page::Result) {
        fonts.semibold.draw(c, tr(Str::GyroAutoCalResult), x, y, 26, kGray);
        y += 50;
        if (calCancelled_) {
            paragraph(tr(Str::GyroAutoCalCancelled), 24, kWhite, 0);
            accept = tr(Str::GyroAutoCalRetry);
        } else if (!calResult_.ok()) {
            const Str why = calResult_.failure == app::gyrocal::Failure::NoMotion ? Str::GyroAutoCalFailNoMotion
                            : calResult_.failure == app::gyrocal::Failure::NoFit  ? Str::GyroAutoCalFailNoFit
                                                                                   : Str::GyroAutoCalFailUnreadable;
            paragraph(tr(why), 24, kWhite, 0);
            accept = tr(Str::GyroAutoCalRetry);
        } else {
            auto line = [&](const char* label, const std::string& value, Color color) {
                fonts.semibold.draw(c, label, x, y, 24, kGray);
                fonts.bold.draw(c, value, x + 620, y, 26, color);
                y += 52;
            };
            line(tr(Str::GyroAutoCalMeasured),
                 fmt("%d %% · %d.%d", calResult_.startPct, calResult_.exponentTenths / 10, calResult_.exponentTenths % 10),
                 kGameOutputColor);
            line(tr(Str::GyroAutoCalCurrent), fmt("%d %% · %d.%d", curveStart_, curveExp_ / 10, curveExp_ % 10), kWhite);
            if (calResult_.verticalRatio > 0)
                line(tr(Str::GyroAutoCalVertical), fmt("%d %%", static_cast<int>(calResult_.verticalRatio * 100.0f + 0.5f)), kWhite);
            accept = tr(Str::GyroAutoCalApply);
        }
    }

    c.fillRect({0, kH - 64, kW, 64}, kBg);
    int hx = panel.x + panel.w - 50;
    const int hy = kH - 46;
    auto hint = [&](PadButton b, const char* label) {
        hx -= fonts.semibold.measure(label, 20);
        fonts.semibold.draw(c, label, hx, hy, 20, kGray);
        hx -= 36;
        drawFaceButton(c, fonts.bold, g_glyphStyle, b, static_cast<float>(hx + 14), static_cast<float>(hy + 12), 13.0f);
        hx -= 32;
    };
    hint(PadButton::Circle, tr(Str::Back));
    if (accept) hint(PadButton::Cross, accept);
}

int GyroWheelDialog::drawRows(Canvas& c, const Fonts& fonts, int rx, int ry, int rw, int rowH) {
    auto drawRow = [&](Item it, const std::string& label, const std::string& val, bool isBtn = false) {
        const bool chosen = (selected_ == it);
        Rect itemBox{rx, ry, rw, rowH - 10};
        c.fillRect(itemBox, chosen ? kCustomFocusBg : rgba(255, 255, 255, 8), 12);
        if (chosen) c.strokeRect(itemBox, kCustomFocusBorder, 2, 12);
        fonts.semibold.draw(c, label, itemBox.x + 20, fonts.semibold.centeredY(itemBox.y, itemBox.h, 20), 20,
                            chosen ? kWhite : kGray);
        if (!val.empty()) {
            const std::string shown = isBtn ? val : "‹  " + val + "  ›";
            const int vw = fonts.bold.measure(shown, 20);
            fonts.bold.draw(c, shown, itemBox.x + itemBox.w - vw - 20, fonts.bold.centeredY(itemBox.y, itemBox.h, 20), 20,
                            isBtn ? (chosen ? kWhite : kGray) : (chosen ? kGameOutputColor : kWhite));
        }
        ry += rowH;
    };

    // The game's stick response, by name; one's own carries the game's name.
    static constexpr Str kPresetNames[] = {Str::GyroPresetLinear, Str::GyroPresetSmooth, Str::GyroPresetExponential};
    const int preset = presetIndex();
    std::string presetName = preset < 0 ? tr(Str::GyroPresetCustom)
                             : preset < static_cast<int>(std::size(kPresetNames)) ? tr(kPresetNames[preset])
                                                                                  : "Fortnite";
    if (preset < 0 && !gameName_.empty()) presetName += " · " + gameName_;
    if (fonts.bold.measure(presetName, 20) > rw / 2) {  // a long game name
        const auto cut = fonts.bold.wrap(presetName, 20, rw / 2, 1);
        if (!cut.empty()) presetName = cut.front();
    }
    drawRow(Item::Preset, tr(Str::GyroGameResponse), presetName);

    // The sensor's drift
    std::string calLabel = tr(Str::GyroCalibrateAction);
    if (isCalibrating()) {
        calLabel = trf(Str::GyroCalibratingPct, std::to_string(static_cast<int>(calibrationProgress() * 100.0f)));
    } else if (calSuccess_) {
        calLabel = tr(Str::GyroCalibratedSuccessShort);
    } else if (calFailed_) {
        calLabel = tr(Str::GyroCalibratedFailedShort);
    }
    drawRow(Item::Calibrate, tr(Str::GyroCalibration), calLabel, true);
    if (isCalibrating()) {
        Rect barBox{rx + 20, ry - 13, rw - 40, 5};
        c.fillRect(barBox, rgba(255, 255, 255, 30), 4);
        c.fillRect({barBox.x, barBox.y, static_cast<int>(barBox.w * calibrationProgress()), barBox.h}, kGameOutputColor, 4);
    }

    // The game's response measured
    drawRow(Item::AutoCalibrate, tr(Str::GyroAutoCal), tr(Str::GyroAutoCalStart), true);

    drawRow(Item::InvertX, tr(Str::GyroInvertX), tr(invertX_ ? Str::On : Str::Off));
    drawRow(Item::InvertY, tr(Str::GyroInvertY), tr(invertY_ ? Str::On : Str::Off));
    drawRow(Item::Intensity, tr(Str::GyroSensitivityIntensity), fmt("%d / 10", level_));
    drawRow(Item::YawScale, tr(Str::GyroYawSensitivity), fmt("%d %%", yawScale_));
    drawRow(Item::PitchScale, tr(Str::GyroPitchSensitivity), fmt("%d %%", pitchScale_));
    drawRow(Item::CurveStart, tr(Str::GyroCurveStart), fmt("%d %%", curveStart_));
    drawRow(Item::CurveExp, tr(Str::GyroCurveShape), fmt("%d.%d", curveExp_ / 10, curveExp_ % 10));
    return ry;
}

Str GyroWheelDialog::helpFor(Item it) const {
    switch (it) {
        case Item::Preset: return Str::GyroHelpPreset;
        case Item::Calibrate: return Str::GyroHelpCalibrate;
        case Item::AutoCalibrate: return Str::GyroHelpAutoCal;
        case Item::InvertX: return Str::GyroHelpInvertX;
        case Item::InvertY: return Str::GyroHelpInvertY;
        case Item::Intensity: return Str::GyroHelpIntensity;
        case Item::YawScale: return Str::GyroHelpYaw;
        case Item::PitchScale: return Str::GyroHelpPitch;
        case Item::CurveStart: return Str::GyroHelpCurveStart;
        default: return Str::GyroHelpCurveExp;
    }
}

// The side panel of the live test: the rows and the chosen one's help, nothing
// over the middle of the picture.
void GyroWheelDialog::drawCompact(Canvas& c, const Fonts& fonts) {
    Rect panel{0, 0, kCompactW, kCompactH};
    // Square corners: the overlay is laid over the game with one opacity, and what a rounded
    // corner leaves outside it showed as dark tips.
    c.fillRect(panel, kPanel);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2);
    fonts.bold.draw(c, tr(Str::Gyro), 24, 20, 26, kWhite);
    int y = 62;
    for (const std::string& line : fonts.regular.wrap(tr(Str::GyroLiveHint), 16, kCompactW - 48, 2)) {
        fonts.regular.draw(c, line, 24, y, 16, kGameOutputColor);
        y += 22;
    }
    y = drawRows(c, fonts, 16, y + 8, kCompactW - 32, 44) + 4;
    for (const std::string& line : fonts.regular.wrap(tr(helpFor(selected_)), 16, kCompactW - 48, 4)) {
        fonts.regular.draw(c, line, 24, y, 16, kGray);
        y += 22;
    }
    // The buttons that still belong to the panel.
    int hx = kCompactW - 24;
    const int hy = kCompactH - 36;
    auto hint = [&](PadButton b, const char* label) {
        hx -= fonts.semibold.measure(label, 16);
        fonts.semibold.draw(c, label, hx, hy, 16, kGray);
        hx -= 30;
        drawFaceButton(c, fonts.bold, g_glyphStyle, b, static_cast<float>(hx + 12), static_cast<float>(hy + 10), 11.0f);
        hx -= 22;
    };
    hint(PadButton::Circle, tr(Str::Back));
    hint(PadButton::Triangle, tr(Str::GyroLiveTestOff));
}

}  // namespace xc::ui
