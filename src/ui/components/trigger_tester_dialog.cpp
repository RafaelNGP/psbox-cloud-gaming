// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstddef>
#include <cstdint>
#include "ui/components/trigger_tester_dialog.h"
#include "input/tuning.h"
#include "ui/input_mode.h"
#include "ui/pad_glyphs.h"

#include <algorithm>
#include <cmath>

namespace xc::ui {

namespace {

constexpr Color kPanel = rgba(24, 24, 24);
constexpr Color kWhite = rgba(255, 255, 255);
constexpr Color kGray = rgba(170, 170, 170);
constexpr Color kDim = rgba(110, 110, 110);

}  // namespace

TriggerTesterDialog::TriggerTesterDialog() = default;

void TriggerTesterDialog::open(int strength, int hz, int resistance, bool pulses,
                               int profile, bool adaptive, const std::string& gameKey) {
    open_ = true;
    adaptive_ = adaptive;
    strength_ = std::clamp(strength, 0, input::kTriggerStrengths - 1);
    hz_ = std::clamp(hz, 0, 2);
    resistance_ = std::clamp(resistance, 0, input::kTriggerResistances - 1);
    pulses_ = pulses;
    profile_ = std::clamp(profile, 0, input::kTriggerProfiles - 1);
    selectedRow_ = 0;
    testL2_ = 0.0f;
    testR2_ = 0.0f;
    gameKey_ = gameKey;
}

void TriggerTesterDialog::close() {
    open_ = false;
    testL2_ = 0.0f;
    testR2_ = 0.0f;
}

void TriggerTesterDialog::setStrength(int s) {
    strength_ = std::clamp(s, 0, input::kTriggerStrengths - 1);
}

void TriggerTesterDialog::setHz(int h) {
    hz_ = std::clamp(h, 0, 2);
}

void TriggerTesterDialog::setResistance(int r) {
    resistance_ = std::clamp(r, 0, input::kTriggerResistances - 1);
}

void TriggerTesterDialog::setPulses(bool p) {
    pulses_ = p;
}

void TriggerTesterDialog::setProfile(int p) {
    profile_ = std::clamp(p, 0, input::kTriggerProfiles - 1);
}

void TriggerTesterDialog::handleInput(const NavInput& in) {
    if (!open_) return;

    const int rows = rowsCount();
    if (in.up && selectedRow_ > 0) --selectedRow_;
    if (in.down && selectedRow_ < rows - 1) ++selectedRow_;

    int delta = (in.right ? 1 : 0) - (in.left ? 1 : 0);
    if (delta != 0) {
        if (selectedRow_ == 0) strength_ = std::clamp(strength_ + delta, 0, input::kTriggerStrengths - 1);
        else if (selectedRow_ == 1) hz_ = std::clamp(hz_ + delta, 0, 2);
        else if (selectedRow_ == 2) resistance_ = std::clamp(resistance_ + delta, 0, input::kTriggerResistances - 1);
        else if (selectedRow_ == 3) pulses_ = !pulses_;
        else if (selectedRow_ == 4) profile_ = std::clamp(profile_ + delta, 0, input::kTriggerProfiles - 1);
    }

    testL2_ = in.l2Analog;
    testR2_ = in.r2Analog;

    if (in.accept || in.back || in.options) {
        close();
    }
}

void TriggerTesterDialog::update(uint64_t nowMs) {
    (void)nowMs;
}

void TriggerTesterDialog::draw(Canvas& c, const Fonts& fonts, int x, int y, int w, int h,
                               bool circleConfirms) const {
    if (!open_) return;
    (void)x; (void)y; (void)w; (void)h;

    const int rows = rowsCount(), lessY = (5 - rows) * 84;
    constexpr int kW = 1920, kH = 1080;
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 160));

    Rect panel{(kW - 1100) / 2, (kH - (890 - lessY)) / 2 - 30, 1100, 890 - lessY};
    c.fillRect({panel.x + 8, panel.y + 12, panel.w, panel.h}, rgba(0, 0, 0, 120), 26);  // shadow
    c.fillRect(panel, kPanel, 24);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 24);
    fonts.bold.draw(c, tr(Str::TriggerRumble), panel.x + 56, panel.y + 44, 40, kWhite);

    const char* strengths[] = {tr(Str::Deactivated), tr(Str::TriggerLight), tr(Str::TriggerMedium),
                               tr(Str::TriggerStrong), tr(Str::TriggerMax)};
    const char* freqs[] = {tr(Str::FrequencyLow), tr(Str::TriggerMedium), tr(Str::FrequencyHigh)};
    static constexpr Str kProfileStrs[] = {Str::TriggerProfileStd, Str::TriggerProfileShooter, Str::TriggerProfileRacing};
    const char* labels[] = {tr(Str::TriggerIntensity), tr(Str::TriggerFrequency), tr(Str::TriggerResistance),
                            tr(Str::TriggerStyle), tr(Str::TriggerProfile)};

    int hzVal = pulses_ ? input::kTriggerPulseHz[hz_] : input::kTriggerHz[hz_];
    int prof = std::clamp(profile_, 0, 2);
    const std::string values[] = {
        strengths[strength_],
        std::string(freqs[hz_]) + "  (" + std::to_string(hzVal) + " Hz)",
        strengths[resistance_],
        tr(pulses_ ? Str::StylePulses : Str::StyleVibration),
        tr(kProfileStrs[prof])
    };

    for (int i = 0; i < rows; ++i) {
        Rect r{panel.x + 56, panel.y + 120 + i * 84, panel.w - 112, 70};
        bool on = i == selectedRow_;
        c.fillRect(r, on ? rgba(255, 255, 255, 36) : rgba(255, 255, 255, 14), 16);
        if (on) c.strokeRect({r.x - 5, r.y - 5, r.w + 10, r.h + 10}, kWhite, 3, 20);
        fonts.semibold.draw(c, labels[i], r.x + 36, fonts.semibold.centeredY(r.y, r.h, 30), 30, kWhite);
        std::string v = "\xE2\x80\xB9  " + values[i] + "  \xE2\x80\xBA";  // ‹ value ›
        int vw = fonts.regular.measure(v, 30);
        fonts.regular.draw(c, v, r.x + r.w - 36 - vw, fonts.regular.centeredY(r.y, r.h, 30), 30, on ? kWhite : kGray);
    }

    const float pressed[2] = {testL2_, testR2_};
    for (int t = 0; t < 2; ++t) {
        constexpr int kBarW = 90, kBarH = 150;
        int bx = panel.x + panel.w / 2 + (t ? 80 : -80 - kBarW), by = panel.y + 560 - lessY;
        c.fillRect({bx, by, kBarW, kBarH}, rgba(255, 255, 255, 20), 16);
        int fill = static_cast<int>(kBarH * std::clamp(pressed[t], 0.0f, 1.0f));
        int level = input::triggerAmplitude(static_cast<uint8_t>(std::lround(pressed[t] * 255)), strength_);
        if (fill > 0) c.fillRect({bx, by + kBarH - fill, kBarW, fill}, level ? rgba(90, 220, 110) : rgba(120, 120, 120), 16);
        std::string name = buttonName(g_glyphStyle, t ? PadButton::R2 : PadButton::L2);
        fonts.bold.draw(c, name, bx + (kBarW - fonts.bold.measure(name, 26)) / 2, by + kBarH + 14, 26, kWhite);
        std::string lv = trf(Str::TriggerLevel, std::to_string(level) + " / 8");
        fonts.regular.draw(c, lv, bx + (kBarW - fonts.regular.measure(lv, 22)) / 2, by + kBarH + 48, 22, kGray);
    }

    auto help = fonts.regular.wrap(tr(Str::TriggerTestHelp), 22, panel.w - 112, 2);
    for (size_t i = 0; i < help.size(); ++i) {
        fonts.regular.draw(c, help[i], panel.x + 56, panel.y + panel.h - 70 + static_cast<int>(i) * 30, 22, kDim);
    }

    // Footer bar and hints
    c.fillRect({0, kH - 80, kW, 80}, rgba(14, 14, 14));
    int hx = 60, hy = kH - 52;
    if (g_keyboardHints) {
        auto cap = [&](const char* key) {
            int kw = fonts.bold.measure(key, 15) + 18;
            c.fillRect({hx, hy, kw, 26}, rgba(255, 255, 255, 30), 6);
            c.strokeRect({hx, hy, kw, 26}, kGray, 2, 6);
            fonts.bold.draw(c, key, hx + 9, fonts.bold.centeredY(hy, 26, 15), 15, kWhite);
            return kw;
        };
        int kw = cap("Enter");
        hx = fonts.semibold.draw(c, tr(Str::Select), hx + kw + 8, hy, 22, kGray) + 32;
        kw = cap("Esc");
        fonts.semibold.draw(c, tr(Str::Back), hx + kw + 8, hy, 22, kGray);
    } else {
        PadButton confirmBtn = circleConfirms ? PadButton::Circle : PadButton::Cross;
        PadButton backBtn = circleConfirms ? PadButton::Cross : PadButton::Circle;
        drawFaceButton(c, fonts.bold, g_glyphStyle, confirmBtn, static_cast<float>(hx + 13), static_cast<float>(hy + 13), 13.0f);
        hx = fonts.semibold.draw(c, tr(Str::Select), hx + 34, hy, 22, kGray) + 32;
        drawFaceButton(c, fonts.bold, g_glyphStyle, backBtn, static_cast<float>(hx + 13), static_cast<float>(hy + 13), 13.0f);
        fonts.semibold.draw(c, tr(Str::Back), hx + 34, hy, 22, kGray);
    }
}

}  // namespace xc::ui
