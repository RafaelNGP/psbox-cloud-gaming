// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Standalone Trigger Vibration & Feel Testing Modal.
#pragma once

#include "ui/app_ui.h"
#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/strings.h"

#include <string>

namespace xc::ui {

class TriggerTesterDialog {
public:
    TriggerTesterDialog();
    ~TriggerTesterDialog() = default;

    void open(int strength = 2, int hz = 1, int resistance = 2, bool pulses = false,
              int profile = 0, bool adaptive = true, const std::string& gameKey = "");
    void close();
    bool isOpen() const { return open_; }

    void handleInput(const NavInput& in);
    void update(uint64_t nowMs);
    void draw(Canvas& c, const Fonts& fonts, int x = 0, int y = 0, int w = 0, int h = 0,
              bool circleConfirms = false) const;

    int strength() const { return strength_; }
    void setStrength(int s);

    int hz() const { return hz_; }
    void setHz(int h);

    int resistance() const { return resistance_; }
    void setResistance(int r);

    bool pulses() const { return pulses_; }
    void setPulses(bool p);

    int profile() const { return profile_; }
    void setProfile(int p);

    int selectedRow() const { return selectedRow_; }

private:
    int rowsCount() const { return adaptive_ ? 5 : 2; }

    bool open_ = false;
    bool adaptive_ = true;
    int strength_ = 2;       // 0..4
    int hz_ = 1;             // 0..2
    int resistance_ = 2;     // 0..3
    bool pulses_ = false;
    int profile_ = 0;        // 0..2
    int selectedRow_ = 0;    // 0..4
    mutable float testL2_ = 0.0f;
    mutable float testR2_ = 0.0f;
    std::string gameKey_;
};

}  // namespace xc::ui
