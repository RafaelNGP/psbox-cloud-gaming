// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "input/controller.h"
#include "ui/canvas.h"
#include "ui/font.h"
#include "app/shell.h"
#include "app/settings.h"

#include <cstdint>
#include <vector>

namespace xc::ui {

enum class TouchControl {
    None = 0,
    LeftStick,
    RightStick,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    DpadUpLeft,
    DpadUpRight,
    DpadDownLeft,
    DpadDownRight,
    ButtonA,
    ButtonB,
    ButtonX,
    ButtonY,
    BumperLB,
    BumperRB,
    TriggerLT,
    TriggerRT,
    ButtonL3,
    ButtonR3,
    ButtonView,
    ButtonMenu,
    ButtonNexus,
    OverlayToggle,
    AbxyGroup
};

struct ActiveTouchPoint {
    uint64_t fingerId = 0;
    TouchControl control = TouchControl::None;
    float startX = 0.0f;
    float startY = 0.0f;
    float currentX = 0.0f;
    float currentY = 0.0f;
    uint64_t downTimeMs = 0;
};

struct TouchControlPos {
    float x = 0.0f;
    float y = 0.0f;
};

struct TouchLayout {
    TouchControlPos leftStick{250.0f, 545.0f};
    TouchControlPos rightStick{1355.0f, 825.0f};
    TouchControlPos dpad{520.0f, 830.0f};
    TouchControlPos btnA{1655.0f, 685.0f};
    TouchControlPos btnB{1765.0f, 575.0f};
    TouchControlPos btnX{1545.0f, 575.0f};
    TouchControlPos btnY{1655.0f, 465.0f};
    TouchControlPos triggerLt{60.0f, 110.0f};
    TouchControlPos bumperLb{60.0f, 212.0f};
    TouchControlPos triggerRt{1630.0f, 110.0f};
    TouchControlPos bumperRb{1630.0f, 212.0f};
    TouchControlPos btnL3{74.0f, 845.0f};
    TouchControlPos btnR3{1710.0f, 845.0f};
    TouchControlPos btnView{830.0f, 68.0f};
    TouchControlPos btnNexus{960.0f, 68.0f};
    TouchControlPos btnMenu{1090.0f, 68.0f};
};

TouchLayout defaultTouchLayout();
bool loadTouchLayout(const std::string& path, TouchLayout& layout);
bool saveTouchLayout(const std::string& path, const TouchLayout& layout);

class TouchPadOverlay {
public:
    explicit TouchPadOverlay(const Fonts& fonts);
    ~TouchPadOverlay() = default;

    void setSettings(const app::Settings& settings);
    bool onTouch(const app::shell::TouchEvent& ev);
    void update(uint64_t nowMs, bool physicalPadConnected, bool physicalPadActive, uint64_t physicalInputMs = 0);
    void render(Canvas& c);

    bool isVisible() const { return overlayMode_ != 2 && currentOpacity_ > 0.01f; }
    float opacity() const { return currentOpacity_; }
    int overlayMode() const { return overlayMode_; }
    void setVisible(bool visible);
    void toggleVisible();
    bool invertSticks() const { return invertSticks_; }
    void setInvertSticks(bool invert);
    void setOpacity(float op);
    void reset();

    bool takeMenuToggleRequest() {
        bool req = menuToggleRequested_;
        menuToggleRequested_ = false;
        return req;
    }

    void drawPullTab(Canvas& c, float alpha) const;

    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void clearDirty() { dirty_ = false; }

    const input::ControllerState& state() const { return state_; }
    TouchControl hitTest(float x, float y) const { return hitTest_(x, y); }

    // Touch Layout Studio
    const TouchLayout& layout() const { return currentLayout_; }
    void setLayout(const TouchLayout& layout);
    void resetToDefaultLayout();
    bool loadLayout(const std::string& path);
    bool saveLayout(const std::string& path) const;
    void openLayoutStudio();
    void closeLayoutStudio();
    bool isLayoutStudioActive() const { return layoutStudio_; }

private:
    void updateStateAndInject_();
    TouchControl hitTest_(float x, float y) const;
    TouchControl hitTestStudioControls_(float x, float y) const;
    void applyLayoutAnchors_();
    void drawStick_(Canvas& c, float cx, float cy, float knobX, float knobY, float radius, float alpha);
    void drawDpad_(Canvas& c, float cx, float cy, float alpha);
    void drawButton_(Canvas& c, float cx, float cy, float r, const char* label, Color accent, bool pressed, float alpha);
    void drawPillButton_(Canvas& c, Rect r, const char* label, bool pressed, float alpha);
    void drawLayoutStudioHud_(Canvas& c);

    const Fonts& fonts_;
    input::ControllerState state_{};

    // Configuration & styling
    int overlayMode_ = 0;         // 0: Auto-hide, 1: Always on, 2: Always off
    float targetOpacity_ = 0.80f; // Configured base opacity [0.1 .. 1.0]
    float currentOpacity_ = 0.80f;
    bool dynamicStick_ = true;
    bool invertSticks_ = false;

    // Multi-touch tracking (up to 10 simultaneous touch points)
    static constexpr size_t kMaxFingers = 10;
    ActiveTouchPoint touches_[kMaxFingers] = {};

    // Custom Layout & Studio state
    TouchLayout currentLayout_{};
    TouchLayout backupLayout_{};
    bool layoutStudio_ = false;
    TouchControl selectedStudioControl_ = TouchControl::None;
    uint64_t activeDragFingerId_ = 0;
    TouchControlPos dragOffset_{};

    // Stick anchors
    float leftStickAnchorX_ = 250.0f;
    float leftStickAnchorY_ = 545.0f;
    float leftStickKnobX_ = 250.0f;
    float leftStickKnobY_ = 545.0f;

    float rightStickAnchorX_ = 1355.0f;
    float rightStickAnchorY_ = 825.0f;
    float rightStickKnobX_ = 1355.0f;
    float rightStickKnobY_ = 825.0f;

    uint64_t lastTouchInteractionMs_ = 0;
    uint64_t lastPhysicalInputMs_ = 0;
    bool physicalSuppressed_ = false;
    bool menuToggleRequested_ = false;
    bool dirty_ = true;
};

}  // namespace xc::ui
