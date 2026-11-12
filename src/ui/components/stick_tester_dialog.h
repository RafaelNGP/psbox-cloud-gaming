// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Standalone Stick Deadzone Testing & Calibration Modal.
#pragma once

#include <cstdint>
#include "ui/app_ui.h"
#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/strings.h"

#include <string>

namespace xc::ui {

class StickTesterDialog {
public:
    StickTesterDialog();
    ~StickTesterDialog() = default;

    void open(int deadzoneLeft = 15, int deadzoneRight = 15, const std::string& gameKey = "");
    void close();
    bool isOpen() const { return open_; }

    void handleInput(const NavInput& in);
    void update(uint64_t nowMs);
    void draw(Canvas& c, const Fonts& fonts, int x = 0, int y = 0, int w = 0, int h = 0,
              bool circleConfirms = false) const;

    int deadzoneLeft() const { return deadzone_[0]; }
    int deadzoneRight() const { return deadzone_[1]; }
    int deadzone() const { return deadzone_[testerStick_]; }
    void setDeadzone(int left, int right);

    int activeStick() const { return testerStick_; }
    void setActiveStick(int s) { testerStick_ = (s != 0) ? 1 : 0; }

private:
    bool open_ = false;
    int testerStick_ = 0;          // 0 = left, 1 = right
    int deadzone_[2] = {15, 15};
    mutable float testSticks_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::string gameKey_;
};

}  // namespace xc::ui
