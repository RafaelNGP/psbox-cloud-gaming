// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "ui/components/stick_tester_dialog.h"
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
constexpr Color kRaw = rgba(170, 170, 170);
constexpr Color kGame = rgba(90, 220, 110);

}  // namespace

StickTesterDialog::StickTesterDialog() = default;

void StickTesterDialog::open(int deadzoneLeft, int deadzoneRight, const std::string& gameKey) {
    open_ = true;
    testerStick_ = 0;
    deadzone_[0] = std::clamp(deadzoneLeft, 0, kMaxDeadzone);
    deadzone_[1] = std::clamp(deadzoneRight, 0, kMaxDeadzone);
    gameKey_ = gameKey;
    for (float& s : testSticks_) s = 0.0f;
}

void StickTesterDialog::close() {
    open_ = false;
    for (float& s : testSticks_) s = 0.0f;
}

void StickTesterDialog::setDeadzone(int left, int right) {
    deadzone_[0] = std::clamp(left, 0, kMaxDeadzone);
    deadzone_[1] = std::clamp(right, 0, kMaxDeadzone);
}

void StickTesterDialog::handleInput(const NavInput& in) {
    if (!open_) return;

    if (in.l1 || in.r1) {
        testerStick_ = in.r1 ? 1 : 0;
    }

    int& zone = deadzone_[testerStick_];
    // D-pad only: the sticks themselves are what is being tested.
    int delta = (in.dpadRight ? 1 : 0) - (in.dpadLeft ? 1 : 0);
    if (delta != 0) {
        zone = std::clamp(zone + delta, 0, kMaxDeadzone);
    }

    testSticks_[0] = in.rawLX;
    testSticks_[1] = in.rawLY;
    testSticks_[2] = in.rawRX;
    testSticks_[3] = in.rawRY;

    if (in.accept || in.back || in.options) {
        close();
    }
}

void StickTesterDialog::update(uint64_t nowMs) {
    (void)nowMs;
}

void StickTesterDialog::draw(Canvas& c, const Fonts& fonts, int x, int y, int w, int h,
                             bool circleConfirms) const {
    if (!open_) return;
    (void)x; (void)y; (void)w; (void)h;

    constexpr int kW = 1920, kH = 1080;
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 160));

    constexpr int modalW = 1240, modalH = 740;
    Rect panel{(kW - modalW) / 2, (kH - modalH) / 2 - 30, modalW, modalH};
    c.fillRect({panel.x + 8, panel.y + 12, panel.w, panel.h}, rgba(0, 0, 0, 120), 26);  // shadow
    c.fillRect(panel, kPanel, 24);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 24);
    fonts.bold.draw(c, tr(Str::Deadzone), panel.x + 56, panel.y + 44, 40, kWhite);

    constexpr int R = 180;
    for (int s = 0; s < 2; ++s) {
        const float dz = deadzone_[s] / 100.0f;
        const bool chosen = s == testerStick_;
        const float cx = panel.x + (s ? 930.0f : 310.0f), cy = panel.y + 170.0f + R;

        std::string value = std::to_string(deadzone_[s]) + " %";
        fonts.bold.draw(c, value, static_cast<int>(cx) - fonts.bold.measure(value, 34) / 2,
                         static_cast<int>(cy) - R - 56, 34, chosen ? kWhite : kDim);

        c.fillCircle(cx, cy, R, rgba(255, 255, 255, chosen ? 22 : 10));
        c.line(cx - R, cy, cx + R, cy, 1, rgba(255, 255, 255, 30));
        c.line(cx, cy - R, cx, cy + R, 1, rgba(255, 255, 255, 30));
        c.strokeArc(cx, cy, R, chosen ? 4.0f : 2.0f, 0, 6.2832f, chosen ? kWhite : rgba(255, 255, 255, 70));

        if (dz > 0) {
            c.fillCircle(cx, cy, dz * R, rgba(230, 70, 70, 60));
            c.strokeArc(cx, cy, dz * R, 2, 0, 6.2832f, rgba(230, 90, 90, 180));
        }

        float sxVal = testSticks_[2 * s], syVal = testSticks_[2 * s + 1], gx = sxVal, gy = syVal;
        input::radialDeadzone(gx, gy, dz);
        c.fillCircle(cx + sxVal * R, cy + syVal * R, 9, kRaw);
        c.fillCircle(cx + gx * R, cy + gy * R, 12, kGame);

        const char* name = tr(s ? Str::StickRight : Str::StickLeft);
        fonts.semibold.draw(c, name, static_cast<int>(cx) - fonts.semibold.measure(name, 26) / 2,
                             static_cast<int>(cy) + R + 22, 26, kWhite);

        auto pct = [](float a, float b) {
            return std::to_string(static_cast<int>(std::lround(std::min(1.0f, std::hypot(a, b)) * 100))) + " %";
        };
        std::string line = pct(sxVal, syVal) + "  \xE2\x86\x92  " + pct(gx, gy);
        int tw = fonts.regular.measure(line, 24);
        fonts.regular.draw(c, line, static_cast<int>(cx) - tw / 2, static_cast<int>(cy) + R + 58, 24, kGray);
    }

    // Legend & help
    int ly = panel.y + panel.h - 92, lx = panel.x + 56;
    c.fillCircle(lx + 9.0f, ly + 13.0f, 9, kRaw);
    fonts.regular.draw(c, tr(Str::StickPosition), lx + 28, ly, 24, kGray);
    lx += 28 + fonts.regular.measure(tr(Str::StickPosition), 24) + 48;
    c.fillCircle(lx + 12.0f, ly + 13.0f, 12, kGame);
    fonts.regular.draw(c, tr(Str::StickGameGets), lx + 32, ly, 24, kGray);
    fonts.regular.draw(c, tr(Str::StickTestHelp), panel.x + 56, ly + 44, 22, kDim);

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
