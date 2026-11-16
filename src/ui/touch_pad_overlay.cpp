// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/touch_pad_overlay.h"
#include "ui/strings.h"
#include "app/web_server.h"
#include "platform/platform.h"
#include "util/json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace xc::ui {

TouchLayout defaultTouchLayout() {
    return TouchLayout{};
}

bool saveTouchLayout(const std::string& path, const TouchLayout& l) {
    json::Value root = json::Value::object();
    root.set("version", 1);
    auto writePos = [&](const char* key, const TouchControlPos& p) {
        json::Value a = json::Value::array();
        a.push(static_cast<double>(p.x));
        a.push(static_cast<double>(p.y));
        root.set(key, a);
    };
    writePos("leftStick", l.leftStick);
    writePos("rightStick", l.rightStick);
    writePos("dpad", l.dpad);
    writePos("btnA", l.btnA);
    writePos("btnB", l.btnB);
    writePos("btnX", l.btnX);
    writePos("btnY", l.btnY);
    writePos("lt", l.triggerLt);
    writePos("lb", l.bumperLb);
    writePos("rt", l.triggerRt);
    writePos("rb", l.bumperRb);
    writePos("l3", l.btnL3);
    writePos("r3", l.btnR3);
    writePos("view", l.btnView);
    writePos("nexus", l.btnNexus);
    writePos("menu", l.btnMenu);
    return platform::writeFileAtomic(path, root.dump());
}

bool loadTouchLayout(const std::string& path, TouchLayout& l) {
    std::string text;
    if (!platform::readFile(path, text) || text.empty()) return false;
    auto doc = json::parse(text);
    if (!doc || !doc->isObject()) return false;
    auto readPos = [&](const char* key, TouchControlPos& p) {
        if (doc->has(key)) {
            const auto& a = (*doc)[key];
            if (a.isArray() && a.size() >= 2) {
                p.x = static_cast<float>(a[0].asNumber(p.x));
                p.y = static_cast<float>(a[1].asNumber(p.y));
            }
        }
    };
    readPos("leftStick", l.leftStick);
    readPos("rightStick", l.rightStick);
    readPos("dpad", l.dpad);
    readPos("btnA", l.btnA);
    readPos("btnB", l.btnB);
    readPos("btnX", l.btnX);
    readPos("btnY", l.btnY);
    readPos("lt", l.triggerLt);
    readPos("lb", l.bumperLb);
    readPos("rt", l.triggerRt);
    readPos("rb", l.bumperRb);
    readPos("l3", l.btnL3);
    readPos("r3", l.btnR3);
    readPos("view", l.btnView);
    readPos("nexus", l.btnNexus);
    readPos("menu", l.btnMenu);
    return true;
}

namespace {

inline float distSq(float x0, float y0, float x1, float y1) {
    float dx = x0 - x1, dy = y0 - y1;
    return dx * dx + dy * dy;
}

inline bool inRect(float x, float y, Rect r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

}  // namespace

TouchPadOverlay::TouchPadOverlay(const Fonts& fonts)
    : fonts_(fonts) {
    loadLayout(platform::dataDir() + "/touch_layout.json");
    reset();
}

void TouchPadOverlay::applyLayoutAnchors_() {
    leftStickAnchorX_ = currentLayout_.leftStick.x;
    leftStickAnchorY_ = currentLayout_.leftStick.y;
    leftStickKnobX_ = leftStickAnchorX_;
    leftStickKnobY_ = leftStickAnchorY_;
    rightStickAnchorX_ = currentLayout_.rightStick.x;
    rightStickAnchorY_ = currentLayout_.rightStick.y;
    rightStickKnobX_ = rightStickAnchorX_;
    rightStickKnobY_ = rightStickAnchorY_;
}

void TouchPadOverlay::setLayout(const TouchLayout& layout) {
    currentLayout_ = layout;
    applyLayoutAnchors_();
    markDirty();
}

void TouchPadOverlay::resetToDefaultLayout() {
    currentLayout_ = defaultTouchLayout();
    applyLayoutAnchors_();
    markDirty();
}

bool TouchPadOverlay::loadLayout(const std::string& path) {
    TouchLayout loaded;
    if (loadTouchLayout(path, loaded)) {
        currentLayout_ = loaded;
        applyLayoutAnchors_();
        markDirty();
        return true;
    }
    currentLayout_ = defaultTouchLayout();
    applyLayoutAnchors_();
    return false;
}

bool TouchPadOverlay::saveLayout(const std::string& path) const {
    return saveTouchLayout(path, currentLayout_);
}

void TouchPadOverlay::openLayoutStudio() {
    layoutStudio_ = true;
    backupLayout_ = currentLayout_;
    selectedStudioControl_ = TouchControl::None;
    activeDragFingerId_ = 0;
    currentOpacity_ = targetOpacity_ = 0.80f;
    xc::app::setVirtualGamepad(0, input::ControllerState{});
    markDirty();
}

void TouchPadOverlay::closeLayoutStudio() {
    layoutStudio_ = false;
    selectedStudioControl_ = TouchControl::None;
    activeDragFingerId_ = 0;
    applyLayoutAnchors_();
    xc::app::setVirtualGamepad(0, input::ControllerState{});
    markDirty();
}

void TouchPadOverlay::reset() {
    for (auto& t : touches_) {
        t = ActiveTouchPoint{};
    }
    state_ = input::ControllerState{};
    applyLayoutAnchors_();
    markDirty();
    updateStateAndInject_();
}

void TouchPadOverlay::setSettings(const app::Settings& settings) {
    overlayMode_ = std::clamp(settings.touchOverlayMode, 0, 2);
    invertSticks_ = settings.touchInvertSticks;
    float baseOp = std::clamp(settings.touchOpacity, 10, 100) / 100.0f;
    if (overlayMode_ == 0) {
        targetOpacity_ = baseOp;
    } else if (overlayMode_ == 1) {
        targetOpacity_ = baseOp;
        currentOpacity_ = baseOp;
        physicalSuppressed_ = false;
    } else if (overlayMode_ == 2) {
        for (auto& t : touches_) {
            t = ActiveTouchPoint{};
        }
        currentOpacity_ = targetOpacity_ = 0.0f;
        updateStateAndInject_();
    }
    markDirty();
}

void TouchPadOverlay::setOpacity(float op) {
    targetOpacity_ = std::clamp(op, 0.10f, 1.0f);
    if (overlayMode_ != 2 && !physicalSuppressed_) {
        currentOpacity_ = targetOpacity_;
    }
    markDirty();
}

void TouchPadOverlay::setVisible(bool visible) {
    overlayMode_ = visible ? 0 : 2;
    targetOpacity_ = visible ? 0.80f : 0.0f;
    currentOpacity_ = targetOpacity_;
    if (visible) {
        physicalSuppressed_ = false;
    } else {
        for (auto& t : touches_) {
            t = ActiveTouchPoint{};
        }
    }
    markDirty();
    updateStateAndInject_();
}

void TouchPadOverlay::setInvertSticks(bool invert) {
    if (invertSticks_ == invert) return;
    invertSticks_ = invert;
    updateStateAndInject_();
}

void TouchPadOverlay::toggleVisible() {
    setVisible(!isVisible());
}

TouchControl TouchPadOverlay::hitTestStudioControls_(float x, float y) const {
    // 1. Center of ABXY diamond (Cluster drag)
    float abxyCx = (currentLayout_.btnA.x + currentLayout_.btnB.x + currentLayout_.btnX.x + currentLayout_.btnY.x) * 0.25f;
    float abxyCy = (currentLayout_.btnA.y + currentLayout_.btnB.y + currentLayout_.btnX.y + currentLayout_.btnY.y) * 0.25f;
    if (distSq(x, y, abxyCx, abxyCy) <= 65.0f * 65.0f) return TouchControl::AbxyGroup;

    // 2. Individual ABXY buttons
    if (distSq(x, y, currentLayout_.btnA.x, currentLayout_.btnA.y) <= 78.0f * 78.0f) return TouchControl::ButtonA;
    if (distSq(x, y, currentLayout_.btnB.x, currentLayout_.btnB.y) <= 78.0f * 78.0f) return TouchControl::ButtonB;
    if (distSq(x, y, currentLayout_.btnX.x, currentLayout_.btnX.y) <= 78.0f * 78.0f) return TouchControl::ButtonX;
    if (distSq(x, y, currentLayout_.btnY.x, currentLayout_.btnY.y) <= 78.0f * 78.0f) return TouchControl::ButtonY;

    // 3. System buttons
    if (distSq(x, y, currentLayout_.btnNexus.x, currentLayout_.btnNexus.y) <= 54.0f * 54.0f) return TouchControl::ButtonNexus;
    if (distSq(x, y, currentLayout_.btnView.x, currentLayout_.btnView.y) <= 46.0f * 46.0f) return TouchControl::ButtonView;
    if (distSq(x, y, currentLayout_.btnMenu.x, currentLayout_.btnMenu.y) <= 46.0f * 46.0f) return TouchControl::ButtonMenu;

    // 4. Triggers & Bumpers
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.triggerLt.x), static_cast<int>(currentLayout_.triggerLt.y), 230, 90})) return TouchControl::TriggerLT;
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.bumperLb.x), static_cast<int>(currentLayout_.bumperLb.y), 230, 84})) return TouchControl::BumperLB;
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.triggerRt.x), static_cast<int>(currentLayout_.triggerRt.y), 230, 90})) return TouchControl::TriggerRT;
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.bumperRb.x), static_cast<int>(currentLayout_.bumperRb.y), 230, 84})) return TouchControl::BumperRB;

    // 5. Stick Clicks
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.btnL3.x), static_cast<int>(currentLayout_.btnL3.y), 136, 66})) return TouchControl::ButtonL3;
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.btnR3.x), static_cast<int>(currentLayout_.btnR3.y), 136, 66})) return TouchControl::ButtonR3;

    // 6. D-Pad
    if (distSq(x, y, currentLayout_.dpad.x, currentLayout_.dpad.y) <= 145.0f * 145.0f) return TouchControl::DpadUp;

    // 7. Thumbsticks
    if (distSq(x, y, currentLayout_.leftStick.x, currentLayout_.leftStick.y) <= 150.0f * 150.0f) return TouchControl::LeftStick;
    if (distSq(x, y, currentLayout_.rightStick.x, currentLayout_.rightStick.y) <= 150.0f * 150.0f) return TouchControl::RightStick;

    return TouchControl::None;
}

TouchControl TouchPadOverlay::hitTest_(float x, float y) const {
    // 0. Top-left pull-tab zone (flush to screen edge)
    if (x >= 0.0f && x <= 140.0f && y >= 0.0f && y <= 100.0f) return TouchControl::OverlayToggle;

    // 1. Center Guide / System buttons (top-center)
    if (distSq(x, y, currentLayout_.btnNexus.x, currentLayout_.btnNexus.y) <= 54.0f * 54.0f) return TouchControl::ButtonNexus;
    if (distSq(x, y, currentLayout_.btnView.x, currentLayout_.btnView.y) <= 46.0f * 46.0f) return TouchControl::ButtonView;
    if (distSq(x, y, currentLayout_.btnMenu.x, currentLayout_.btnMenu.y) <= 46.0f * 46.0f) return TouchControl::ButtonMenu;

    // 2. Bumpers & Triggers (top corners, vertically stacked pills)
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.triggerLt.x), static_cast<int>(currentLayout_.triggerLt.y), 230, 90})) return TouchControl::TriggerLT;
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.bumperLb.x), static_cast<int>(currentLayout_.bumperLb.y), 230, 84})) return TouchControl::BumperLB;
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.triggerRt.x), static_cast<int>(currentLayout_.triggerRt.y), 230, 90})) return TouchControl::TriggerRT;
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.bumperRb.x), static_cast<int>(currentLayout_.bumperRb.y), 230, 84})) return TouchControl::BumperRB;

    // 3. L3 / R3 Stick Clicks (bottom corners)
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.btnL3.x), static_cast<int>(currentLayout_.btnL3.y), 136, 66})) return TouchControl::ButtonL3;
    if (inRect(x, y, Rect{static_cast<int>(currentLayout_.btnR3.x), static_cast<int>(currentLayout_.btnR3.y), 136, 66})) return TouchControl::ButtonR3;

    // 4. ABXY Diamond Buttons
    if (distSq(x, y, currentLayout_.btnA.x, currentLayout_.btnA.y) <= 78.0f * 78.0f) return TouchControl::ButtonA;
    if (distSq(x, y, currentLayout_.btnB.x, currentLayout_.btnB.y) <= 78.0f * 78.0f) return TouchControl::ButtonB;
    if (distSq(x, y, currentLayout_.btnX.x, currentLayout_.btnX.y) <= 78.0f * 78.0f) return TouchControl::ButtonX;
    if (distSq(x, y, currentLayout_.btnY.x, currentLayout_.btnY.y) <= 78.0f * 78.0f) return TouchControl::ButtonY;

    // 5. 8-Way Radial D-Pad
    float dpadCx = currentLayout_.dpad.x, dpadCy = currentLayout_.dpad.y;
    float dpadDistSq = distSq(x, y, dpadCx, dpadCy);
    if (dpadDistSq >= 40.0f * 40.0f && dpadDistSq <= 145.0f * 145.0f) {
        float dx = x - dpadCx;
        float dy = y - dpadCy;
        float angle = std::atan2(dy, dx) * 180.0f / 3.14159265358979323846f;
        if (angle >= -22.5f && angle < 22.5f) return TouchControl::DpadRight;
        if (angle >= 22.5f && angle < 67.5f) return TouchControl::DpadDownRight;
        if (angle >= 67.5f && angle < 112.5f) return TouchControl::DpadDown;
        if (angle >= 112.5f && angle < 157.5f) return TouchControl::DpadDownLeft;
        if (angle >= 157.5f || angle < -157.5f) return TouchControl::DpadLeft;
        if (angle >= -157.5f && angle < -112.5f) return TouchControl::DpadUpLeft;
        if (angle >= -112.5f && angle < -67.5f) return TouchControl::DpadUp;
        if (angle >= -67.5f && angle < -22.5f) return TouchControl::DpadUpRight;
    }

    // 6. Right Aiming Thumbstick quadrant
    if (distSq(x, y, rightStickAnchorX_, rightStickAnchorY_) <= 165.0f * 165.0f) {
        return TouchControl::RightStick;
    }
    if (x >= 1180.0f && x <= 1520.0f && y >= 640.0f && y <= 1040.0f) {
        return TouchControl::RightStick;
    }

    // 7. Left Movement Thumbstick quadrant (floating or fixed)
    if (dynamicStick_) {
        if (x >= 40.0f && x <= 750.0f && y >= 360.0f && y <= 1040.0f) {
            // Avoid conflict with DPad wings and L3 button
            if (distSq(x, y, dpadCx, dpadCy) > 140.0f * 140.0f &&
                !inRect(x, y, Rect{static_cast<int>(currentLayout_.btnL3.x), static_cast<int>(currentLayout_.btnL3.y), 136, 66})) {
                return TouchControl::LeftStick;
            }
        }
    } else {
        if (distSq(x, y, leftStickAnchorX_, leftStickAnchorY_) <= 165.0f * 165.0f) {
            return TouchControl::LeftStick;
        }
    }

    return TouchControl::None;
}

bool TouchPadOverlay::onTouch(const app::shell::TouchEvent& ev) {
    if (layoutStudio_) {
        // --- Studio Mode: Interactive Drag & Drop ---
        if (ev.kind == app::shell::TouchEvent::Kind::Down) {
            // Check Toolbar buttons:
            // [ Restaurar Padrão ]: Rect{670, 980, 200, 48}
            if (inRect(ev.x, ev.y, Rect{670, 980, 200, 48})) {
                currentLayout_ = defaultTouchLayout();
                applyLayoutAnchors_();
                selectedStudioControl_ = TouchControl::None;
                activeDragFingerId_ = 0;
                markDirty();
                input::triggerHaptic(input::HapticType::Click);
                return true;
            }
            // [ Cancelar ]: Rect{890, 980, 140, 48}
            if (inRect(ev.x, ev.y, Rect{890, 980, 140, 48})) {
                currentLayout_ = backupLayout_;
                applyLayoutAnchors_();
                closeLayoutStudio();
                input::triggerHaptic(input::HapticType::Click);
                return true;
            }
            // [ Salvar ]: Rect{1050, 980, 160, 48}
            if (inRect(ev.x, ev.y, Rect{1050, 980, 160, 48})) {
                saveLayout(platform::dataDir() + "/touch_layout.json");
                closeLayoutStudio();
                input::triggerHaptic(input::HapticType::HeavyClick);
                return true;
            }

            TouchControl ctrl = hitTestStudioControls_(ev.x, ev.y);
            selectedStudioControl_ = ctrl;
            activeDragFingerId_ = ev.fingerId;

            if (ctrl == TouchControl::AbxyGroup) {
                float abxyCx = (currentLayout_.btnA.x + currentLayout_.btnB.x + currentLayout_.btnX.x + currentLayout_.btnY.x) * 0.25f;
                float abxyCy = (currentLayout_.btnA.y + currentLayout_.btnB.y + currentLayout_.btnX.y + currentLayout_.btnY.y) * 0.25f;
                dragOffset_ = { abxyCx - ev.x, abxyCy - ev.y };
            } else if (ctrl != TouchControl::None) {
                TouchControlPos* p = nullptr;
                switch (ctrl) {
                    case TouchControl::LeftStick: p = &currentLayout_.leftStick; break;
                    case TouchControl::RightStick: p = &currentLayout_.rightStick; break;
                    case TouchControl::DpadUp: case TouchControl::DpadDown:
                    case TouchControl::DpadLeft: case TouchControl::DpadRight:
                    case TouchControl::DpadUpLeft: case TouchControl::DpadUpRight:
                    case TouchControl::DpadDownLeft: case TouchControl::DpadDownRight:
                        p = &currentLayout_.dpad; break;
                    case TouchControl::ButtonA: p = &currentLayout_.btnA; break;
                    case TouchControl::ButtonB: p = &currentLayout_.btnB; break;
                    case TouchControl::ButtonX: p = &currentLayout_.btnX; break;
                    case TouchControl::ButtonY: p = &currentLayout_.btnY; break;
                    case TouchControl::TriggerLT: p = &currentLayout_.triggerLt; break;
                    case TouchControl::BumperLB: p = &currentLayout_.bumperLb; break;
                    case TouchControl::TriggerRT: p = &currentLayout_.triggerRt; break;
                    case TouchControl::BumperRB: p = &currentLayout_.bumperRb; break;
                    case TouchControl::ButtonL3: p = &currentLayout_.btnL3; break;
                    case TouchControl::ButtonR3: p = &currentLayout_.btnR3; break;
                    case TouchControl::ButtonView: p = &currentLayout_.btnView; break;
                    case TouchControl::ButtonNexus: p = &currentLayout_.btnNexus; break;
                    case TouchControl::ButtonMenu: p = &currentLayout_.btnMenu; break;
                    default: break;
                }
                if (p) {
                    dragOffset_ = { p->x - ev.x, p->y - ev.y };
                }
            }
            if (ctrl != TouchControl::None) {
                input::triggerHaptic(input::HapticType::Click);
            }
            markDirty();
            return true;
        }

        if (ev.kind == app::shell::TouchEvent::Kind::Motion) {
            if (ev.fingerId == activeDragFingerId_ && selectedStudioControl_ != TouchControl::None) {
                if (selectedStudioControl_ == TouchControl::AbxyGroup) {
                    float targetCx = std::clamp(ev.x + dragOffset_.x, 220.0f, 1700.0f);
                    float targetCy = std::clamp(ev.y + dragOffset_.y, 220.0f, 860.0f);
                    float curCx = (currentLayout_.btnA.x + currentLayout_.btnB.x + currentLayout_.btnX.x + currentLayout_.btnY.x) * 0.25f;
                    float curCy = (currentLayout_.btnA.y + currentLayout_.btnB.y + currentLayout_.btnX.y + currentLayout_.btnY.y) * 0.25f;
                    float dx = targetCx - curCx;
                    float dy = targetCy - curCy;
                    currentLayout_.btnA.x += dx; currentLayout_.btnA.y += dy;
                    currentLayout_.btnB.x += dx; currentLayout_.btnB.y += dy;
                    currentLayout_.btnX.x += dx; currentLayout_.btnX.y += dy;
                    currentLayout_.btnY.x += dx; currentLayout_.btnY.y += dy;
                } else {
                    TouchControlPos* p = nullptr;
                    float r = 50.0f, w = 0.0f, h = 0.0f;
                    switch (selectedStudioControl_) {
                        case TouchControl::LeftStick: p = &currentLayout_.leftStick; r = 135.0f; break;
                        case TouchControl::RightStick: p = &currentLayout_.rightStick; r = 135.0f; break;
                        case TouchControl::DpadUp: case TouchControl::DpadDown:
                        case TouchControl::DpadLeft: case TouchControl::DpadRight:
                        case TouchControl::DpadUpLeft: case TouchControl::DpadUpRight:
                        case TouchControl::DpadDownLeft: case TouchControl::DpadDownRight:
                            p = &currentLayout_.dpad; r = 114.0f; break;
                        case TouchControl::ButtonA: p = &currentLayout_.btnA; r = 72.0f; break;
                        case TouchControl::ButtonB: p = &currentLayout_.btnB; r = 72.0f; break;
                        case TouchControl::ButtonX: p = &currentLayout_.btnX; r = 72.0f; break;
                        case TouchControl::ButtonY: p = &currentLayout_.btnY; r = 72.0f; break;
                        case TouchControl::TriggerLT: p = &currentLayout_.triggerLt; w = 230.0f; h = 90.0f; break;
                        case TouchControl::BumperLB: p = &currentLayout_.bumperLb; w = 230.0f; h = 84.0f; break;
                        case TouchControl::TriggerRT: p = &currentLayout_.triggerRt; w = 230.0f; h = 90.0f; break;
                        case TouchControl::BumperRB: p = &currentLayout_.bumperRb; w = 230.0f; h = 84.0f; break;
                        case TouchControl::ButtonL3: p = &currentLayout_.btnL3; w = 136.0f; h = 66.0f; break;
                        case TouchControl::ButtonR3: p = &currentLayout_.btnR3; w = 136.0f; h = 66.0f; break;
                        case TouchControl::ButtonView: p = &currentLayout_.btnView; r = 42.0f; break;
                        case TouchControl::ButtonNexus: p = &currentLayout_.btnNexus; r = 50.0f; break;
                        case TouchControl::ButtonMenu: p = &currentLayout_.btnMenu; r = 42.0f; break;
                        default: break;
                    }
                    if (p) {
                        if (w > 0.0f) {
                            p->x = std::clamp(ev.x + dragOffset_.x, 20.0f, 1920.0f - w - 20.0f);
                            p->y = std::clamp(ev.y + dragOffset_.y, 20.0f, 1080.0f - h - 20.0f);
                        } else {
                            p->x = std::clamp(ev.x + dragOffset_.x, r + 20.0f, 1920.0f - r - 20.0f);
                            p->y = std::clamp(ev.y + dragOffset_.y, r + 20.0f, 1080.0f - r - 20.0f);
                        }
                    }
                }
                applyLayoutAnchors_();
                markDirty();
            }
            return true;
        }

        if (ev.kind == app::shell::TouchEvent::Kind::Up || ev.kind == app::shell::TouchEvent::Kind::Cancel) {
            if (ev.fingerId == activeDragFingerId_) {
                activeDragFingerId_ = 0;
                markDirty();
            }
            return true;
        }
        return true;
    }

    if (overlayMode_ == 2) {
        if (ev.kind == app::shell::TouchEvent::Kind::Down &&
            ev.x >= 0.0f && ev.x <= 140.0f && ev.y >= 0.0f && ev.y <= 100.0f) {
            input::triggerHaptic(input::HapticType::Click);
            menuToggleRequested_ = true;
            return true;
        }
        return false; // Always off
    }

    if (ev.kind == app::shell::TouchEvent::Kind::Down || ev.kind == app::shell::TouchEvent::Kind::Motion) {
        lastTouchInteractionMs_ = std::max(platform::nowMs(), lastPhysicalInputMs_ + 1);
        if (physicalSuppressed_) {
            physicalSuppressed_ = false;
            markDirty();
        }
    }

    if (ev.kind == app::shell::TouchEvent::Kind::Down) {
        // Find empty touch slot
        int freeSlot = -1;
        for (size_t i = 0; i < kMaxFingers; ++i) {
            if (touches_[i].control == TouchControl::None && freeSlot < 0) {
                freeSlot = static_cast<int>(i);
            }
        }
        if (freeSlot < 0) return false;

        TouchControl ctrl = hitTest_(ev.x, ev.y);
        if (ctrl == TouchControl::None) return false;

        if (ctrl != TouchControl::LeftStick && ctrl != TouchControl::RightStick) {
            input::triggerHaptic(input::HapticType::Click);
        }

        if (ctrl == TouchControl::OverlayToggle) {
            menuToggleRequested_ = true;
            return true;
        }

        touches_[freeSlot].fingerId = ev.fingerId;
        touches_[freeSlot].control = ctrl;
        touches_[freeSlot].startX = ev.x;
        touches_[freeSlot].startY = ev.y;
        touches_[freeSlot].currentX = ev.x;
        touches_[freeSlot].currentY = ev.y;
        touches_[freeSlot].downTimeMs = lastTouchInteractionMs_;

        if (ctrl == TouchControl::LeftStick && dynamicStick_) {
            leftStickAnchorX_ = ev.x;
            leftStickAnchorY_ = ev.y;
            leftStickKnobX_ = ev.x;
            leftStickKnobY_ = ev.y;
        } else if (ctrl == TouchControl::RightStick && dynamicStick_) {
            rightStickAnchorX_ = ev.x;
            rightStickAnchorY_ = ev.y;
            rightStickKnobX_ = ev.x;
            rightStickKnobY_ = ev.y;
        }

        updateStateAndInject_();
        return true;
    }

    // Motion or Up or Cancel: find matching finger
    int slot = -1;
    for (size_t i = 0; i < kMaxFingers; ++i) {
        if (touches_[i].control != TouchControl::None && touches_[i].fingerId == ev.fingerId) {
            slot = static_cast<int>(i);
            break;
        }
    }
    if (slot < 0) return false;

    if (ev.kind == app::shell::TouchEvent::Kind::Motion) {
        touches_[slot].currentX = ev.x;
        touches_[slot].currentY = ev.y;
        updateStateAndInject_();
        return true;
    }

    if (ev.kind == app::shell::TouchEvent::Kind::Up || ev.kind == app::shell::TouchEvent::Kind::Cancel) {
        TouchControl ctrl = touches_[slot].control;
        touches_[slot] = ActiveTouchPoint{};
        if (ctrl == TouchControl::LeftStick) {
            leftStickKnobX_ = leftStickAnchorX_;
            leftStickKnobY_ = leftStickAnchorY_;
        } else if (ctrl == TouchControl::RightStick) {
            rightStickKnobX_ = rightStickAnchorX_;
            rightStickKnobY_ = rightStickAnchorY_;
        }
        updateStateAndInject_();
        return true;
    }

    return false;
}

void TouchPadOverlay::update(uint64_t nowMs, bool physicalPadConnected, bool physicalPadActive, uint64_t physicalInputMs) {
    (void)nowMs;
    if (layoutStudio_) {
        currentOpacity_ = targetOpacity_ = 0.80f;
        return;
    }
    if (overlayMode_ == 2) { // Always off
        currentOpacity_ = 0.0f;
        return;
    }
    if (overlayMode_ == 1) { // Always on
        currentOpacity_ = targetOpacity_;
        return;
    }

    // Auto-hide mode (0):
    if (physicalInputMs > 0) {
        if (physicalInputMs > lastPhysicalInputMs_) {
            lastPhysicalInputMs_ = physicalInputMs;
        }
        if (physicalInputMs >= lastTouchInteractionMs_) {
            physicalSuppressed_ = true;
        }
    } else if (physicalPadActive) {
        physicalSuppressed_ = true;
    }

    // Active touch points maintain visibility when touch was more recent
    bool hasActiveTouch = false;
    for (const auto& t : touches_) {
        if (t.control != TouchControl::None) {
            hasActiveTouch = true;
            break;
        }
    }
    if (hasActiveTouch && lastTouchInteractionMs_ > lastPhysicalInputMs_) {
        physicalSuppressed_ = false;
    }

    if (physicalSuppressed_) {
        bool hadTouch = false;
        for (auto& t : touches_) {
            if (t.control != TouchControl::None) {
                t = ActiveTouchPoint{};
                hadTouch = true;
            }
        }
        if (hadTouch) {
            leftStickKnobX_ = leftStickAnchorX_;
            leftStickKnobY_ = leftStickAnchorY_;
            rightStickKnobX_ = rightStickAnchorX_;
            rightStickKnobY_ = rightStickAnchorY_;
            updateStateAndInject_();
        }
    }

    float target = (physicalPadConnected && physicalSuppressed_) ? 0.0f : (physicalSuppressed_ ? 0.0f : targetOpacity_);

    // Smooth ease-out fade transition
    constexpr float kFadeSpeed = 0.08f;
    float prevOpacity = currentOpacity_;
    if (currentOpacity_ < target) {
        currentOpacity_ = std::min(target, currentOpacity_ + kFadeSpeed);
    } else if (currentOpacity_ > target) {
        currentOpacity_ = std::max(target, currentOpacity_ - kFadeSpeed);
        if (currentOpacity_ < 0.01f && target == 0.0f) {
            currentOpacity_ = 0.0f;
        }
    }
    if (std::abs(currentOpacity_ - prevOpacity) > 0.001f || (prevOpacity != currentOpacity_)) {
        markDirty();
    }
}

void TouchPadOverlay::updateStateAndInject_() {
    if (overlayMode_ == 2 || layoutStudio_) {
        state_ = input::ControllerState{};
        xc::app::setVirtualGamepad(0, state_);
        markDirty();
        return;
    }
    input::ControllerState s{};
    s.connected = isVisible();
    const float ySign = invertSticks_ ? -1.0f : 1.0f;

    for (const auto& t : touches_) {
        switch (t.control) {
        case TouchControl::ButtonA: s.btnA = true; break;
        case TouchControl::ButtonB: s.btnB = true; break;
        case TouchControl::ButtonX: s.btnX = true; break;
        case TouchControl::ButtonY: s.btnY = true; break;
        case TouchControl::BumperLB: s.btnL1 = true; break;
        case TouchControl::BumperRB: s.btnR1 = true; break;
        case TouchControl::TriggerLT: s.triggerL2 = 1.0f; break;
        case TouchControl::TriggerRT: s.triggerR2 = 1.0f; break;
        case TouchControl::DpadUp: s.dpadUp = true; break;
        case TouchControl::DpadDown: s.dpadDown = true; break;
        case TouchControl::DpadLeft: s.dpadLeft = true; break;
        case TouchControl::DpadRight: s.dpadRight = true; break;
        case TouchControl::DpadUpLeft: s.dpadUp = true; s.dpadLeft = true; break;
        case TouchControl::DpadUpRight: s.dpadUp = true; s.dpadRight = true; break;
        case TouchControl::DpadDownLeft: s.dpadDown = true; s.dpadLeft = true; break;
        case TouchControl::DpadDownRight: s.dpadDown = true; s.dpadRight = true; break;
        case TouchControl::ButtonL3: s.btnL3 = true; break;
        case TouchControl::ButtonR3: s.btnR3 = true; break;
        case TouchControl::ButtonView: s.btnTouchpad = true; break;
        case TouchControl::ButtonMenu: s.btnOptions = true; break;
        case TouchControl::ButtonNexus: s.btnNexus = true; break;
        case TouchControl::LeftStick: {
            float dx = t.currentX - leftStickAnchorX_;
            float dy = t.currentY - leftStickAnchorY_;
            float dist = std::sqrt(dx * dx + dy * dy);
            constexpr float kDeadzone = 12.0f;
            constexpr float kMaxRadius = 115.0f;
            if (dist < kDeadzone) {
                leftStickKnobX_ = leftStickAnchorX_;
                leftStickKnobY_ = leftStickAnchorY_;
                s.leftStickX = 0.0f;
                s.leftStickY = 0.0f;
            } else {
                float clamped = std::min(dist, kMaxRadius);
                leftStickKnobX_ = leftStickAnchorX_ + (dx / dist) * clamped;
                leftStickKnobY_ = leftStickAnchorY_ + (dy / dist) * clamped;
                float norm = (clamped - kDeadzone) / (kMaxRadius - kDeadzone);
                s.leftStickX = std::clamp((dx / dist) * norm, -1.0f, 1.0f);
                s.leftStickY = std::clamp(ySign * (dy / dist) * norm, -1.0f, 1.0f); // ControllerState: +down, -up
            }
            break;
        }
        case TouchControl::RightStick: {
            float dx = t.currentX - rightStickAnchorX_;
            float dy = t.currentY - rightStickAnchorY_;
            float dist = std::sqrt(dx * dx + dy * dy);
            constexpr float kDeadzone = 12.0f;
            constexpr float kMaxRadius = 115.0f;
            if (dist < kDeadzone) {
                rightStickKnobX_ = rightStickAnchorX_;
                rightStickKnobY_ = rightStickAnchorY_;
                s.rightStickX = 0.0f;
                s.rightStickY = 0.0f;
            } else {
                float clamped = std::min(dist, kMaxRadius);
                rightStickKnobX_ = rightStickAnchorX_ + (dx / dist) * clamped;
                rightStickKnobY_ = rightStickAnchorY_ + (dy / dist) * clamped;
                float norm = (clamped - kDeadzone) / (kMaxRadius - kDeadzone);
                s.rightStickX = std::clamp((dx / dist) * norm, -1.0f, 1.0f);
                s.rightStickY = std::clamp(ySign * (dy / dist) * norm, -1.0f, 1.0f); // ControllerState: +down, -up
            }
            break;
        }
        default: break;
        }
    }

    state_ = s;
    xc::app::setVirtualGamepad(0, state_);
    markDirty();
}

void TouchPadOverlay::drawStick_(Canvas& c, float cx, float cy, float knobX, float knobY, float radius, float alpha) {
    uint8_t baseAlpha = static_cast<uint8_t>(std::clamp(alpha * 70.0f, 0.0f, 255.0f));
    uint8_t borderAlpha = static_cast<uint8_t>(std::clamp(alpha * 140.0f, 0.0f, 255.0f));
    uint8_t knobAlpha = static_cast<uint8_t>(std::clamp(alpha * 180.0f, 0.0f, 255.0f));

    c.fillCircle(cx, cy, radius, rgba(30, 30, 35, baseAlpha));
    c.strokeCircle(cx, cy, radius, 2.5f, rgba(200, 200, 210, borderAlpha));
    c.fillCircle(knobX, knobY, 60.0f, rgba(80, 80, 95, knobAlpha));
    c.strokeCircle(knobX, knobY, 60.0f, 2.5f, rgba(230, 230, 240, borderAlpha));
}

void TouchPadOverlay::drawButton_(Canvas& c, float cx, float cy, float r, const char* label, Color accent, bool pressed, float alpha) {
    uint8_t bgAlpha = static_cast<uint8_t>(std::clamp(alpha * (pressed ? 220.0f : 120.0f), 0.0f, 255.0f));
    uint8_t borderAlpha = static_cast<uint8_t>(std::clamp(alpha * 200.0f, 0.0f, 255.0f));

    Color fill = pressed ? withAlpha(accent, bgAlpha) : rgba(40, 40, 48, bgAlpha);
    c.fillCircle(cx, cy, r, fill);
    c.strokeCircle(cx, cy, r, 2.5f, withAlpha(accent, borderAlpha));

    if (label && *label) {
        int fontSize = r >= 55.0f ? 32 : (r >= 45.0f ? 28 : 24);
        int textW = fonts_.bold.measure(label, fontSize);
        int textY = fonts_.bold.centeredY(static_cast<int>(cy - r), static_cast<int>(r * 2), fontSize);
        fonts_.bold.draw(c, label, static_cast<int>(cx - textW / 2.0f), textY, fontSize,
                         rgba(255, 255, 255, static_cast<uint8_t>(alpha * 240.0f)));
    }
}

void TouchPadOverlay::drawPillButton_(Canvas& c, Rect r, const char* label, bool pressed, float alpha) {
    uint8_t bgAlpha = static_cast<uint8_t>(std::clamp(alpha * (pressed ? 200.0f : 110.0f), 0.0f, 255.0f));
    uint8_t borderAlpha = static_cast<uint8_t>(std::clamp(alpha * 180.0f, 0.0f, 255.0f));

    int cornerRadius = std::min(r.h / 2, 24);
    c.fillRect(r, rgba(40, 40, 48, bgAlpha), cornerRadius);
    c.strokeRect(r, rgba(180, 180, 195, borderAlpha), 2, cornerRadius);

    if (label && *label) {
        int fontSize = r.h >= 60 ? 26 : 22;
        int textW = fonts_.bold.measure(label, fontSize);
        int textY = fonts_.bold.centeredY(r.y, r.h, fontSize);
        fonts_.bold.draw(c, label, r.x + (r.w - textW) / 2, textY, fontSize,
                         rgba(255, 255, 255, static_cast<uint8_t>(alpha * 240.0f)));
    }
}

void TouchPadOverlay::drawDpad_(Canvas& c, float cx, float cy, float alpha) {
    uint8_t bgAlpha = static_cast<uint8_t>(std::clamp(alpha * 100.0f, 0.0f, 255.0f));
    uint8_t borderAlpha = static_cast<uint8_t>(std::clamp(alpha * 180.0f, 0.0f, 255.0f));
    uint8_t textAlpha = static_cast<uint8_t>(std::clamp(alpha * 220.0f, 0.0f, 255.0f));

    // Base cross shape (76px thick arms, 228px total span, radius 14)
    c.fillRect(Rect{static_cast<int>(cx - 38), static_cast<int>(cy - 114), 76, 228}, rgba(30, 30, 36, bgAlpha), 14);
    c.fillRect(Rect{static_cast<int>(cx - 114), static_cast<int>(cy - 38), 228, 76}, rgba(30, 30, 36, bgAlpha), 14);

    // Crisp outer border
    c.strokeRect(Rect{static_cast<int>(cx - 38), static_cast<int>(cy - 114), 76, 228}, rgba(180, 185, 200, borderAlpha), 2, 14);
    c.strokeRect(Rect{static_cast<int>(cx - 114), static_cast<int>(cy - 38), 228, 76}, rgba(180, 185, 200, borderAlpha), 2, 14);

    // Pressed directional highlights
    if (state_.dpadUp)
        c.fillRect(Rect{static_cast<int>(cx - 36), static_cast<int>(cy - 112), 72, 72}, rgba(80, 140, 240, borderAlpha), 12);
    if (state_.dpadDown)
        c.fillRect(Rect{static_cast<int>(cx - 36), static_cast<int>(cy + 40), 72, 72}, rgba(80, 140, 240, borderAlpha), 12);
    if (state_.dpadLeft)
        c.fillRect(Rect{static_cast<int>(cx - 112), static_cast<int>(cy - 36), 72, 72}, rgba(80, 140, 240, borderAlpha), 12);
    if (state_.dpadRight)
        c.fillRect(Rect{static_cast<int>(cx + 40), static_cast<int>(cy - 36), 72, 72}, rgba(80, 140, 240, borderAlpha), 12);

    // Directional chevrons
    Color chevCol = rgba(220, 225, 235, textAlpha);
    c.line(cx - 14, cy - 70, cx, cy - 84, 3.0f, chevCol);
    c.line(cx, cy - 84, cx + 14, cy - 70, 3.0f, chevCol);
    c.line(cx - 14, cy + 70, cx, cy + 84, 3.0f, chevCol);
    c.line(cx, cy + 84, cx + 14, cy + 70, 3.0f, chevCol);
    c.line(cx - 70, cy - 14, cx - 84, cy, 3.0f, chevCol);
    c.line(cx - 84, cy, cx - 70, cy + 14, 3.0f, chevCol);
    c.line(cx + 70, cy - 14, cx + 84, cy, 3.0f, chevCol);
    c.line(cx + 84, cy, cx + 70, cy + 14, 3.0f, chevCol);

    c.strokeCircle(cx, cy, 22.0f, 2.0f, rgba(160, 160, 175, borderAlpha));
}

void TouchPadOverlay::drawPullTab(Canvas& c, float alpha) const {
#if !defined(__ANDROID__)
    (void)c;
    (void)alpha;
    return;
#else
    uint8_t bgAlpha = static_cast<uint8_t>(std::clamp(alpha * 160.0f, 0.0f, 255.0f));
    uint8_t borderAlpha = static_cast<uint8_t>(std::clamp(alpha * 200.0f, 0.0f, 255.0f));
    uint8_t iconAlpha = static_cast<uint8_t>(std::clamp(alpha * 240.0f, 0.0f, 255.0f));

    Rect tab{0, 28, 68, 52};
    c.fillRect(tab, rgba(30, 30, 38, bgAlpha), 14);
    c.strokeRect(tab, rgba(160, 165, 180, borderAlpha), 2, 14);

    Color iconCol = rgba(240, 240, 250, iconAlpha);
    c.fillRect(Rect{14, 43, 20, 3}, iconCol);
    c.fillRect(Rect{14, 51, 20, 3}, iconCol);
    c.fillRect(Rect{14, 59, 20, 3}, iconCol);

    c.line(44, 44, 51, 52, 2.5f, iconCol);
    c.line(51, 52, 44, 60, 2.5f, iconCol);
#endif
}

void TouchPadOverlay::render(Canvas& c) {
#if !defined(__ANDROID__)
    (void)c;
    return;
#else
    if (currentOpacity_ <= 0.01f && !layoutStudio_) return;
    float a = layoutStudio_ ? 1.0f : currentOpacity_;

    if (layoutStudio_) {
        // Semi-transparent backdrop
        c.fillRect({0, 0, 1920, 1080}, rgba(0, 0, 0, 140));
        // Subtle alignment guides
        Color guideCol = rgba(255, 255, 255, 24);
        c.line(960, 0, 960, 1080, 1.0f, guideCol);
        c.line(0, 540, 1920, 540, 1.0f, guideCol);
        c.strokeRect({20, 20, 1880, 1040}, rgba(255, 255, 255, 18), 1, 8);
    } else {
        // 0. Top-left pull tab handle
        drawPullTab(c, a);
    }

    // 1. Sticks (outer ring radius 135.0f)
    drawStick_(c, leftStickAnchorX_, leftStickAnchorY_, leftStickKnobX_, leftStickKnobY_, 135.0f, a);
    drawStick_(c, rightStickAnchorX_, rightStickAnchorY_, rightStickKnobX_, rightStickKnobY_, 135.0f, a);

    // 2. D-Pad at (currentLayout_.dpad.x, currentLayout_.dpad.y)
    drawDpad_(c, currentLayout_.dpad.x, currentLayout_.dpad.y, a);

    // 3. ABXY Diamond (Green A, Red B, Blue X, Yellow Y - enlarged r = 72.0f)
    drawButton_(c, currentLayout_.btnA.x, currentLayout_.btnA.y, 72.0f, "A", rgba(40, 200, 60), state_.btnA, a);
    drawButton_(c, currentLayout_.btnB.x, currentLayout_.btnB.y, 72.0f, "B", rgba(230, 45, 45), state_.btnB, a);
    drawButton_(c, currentLayout_.btnX.x, currentLayout_.btnX.y, 72.0f, "X", rgba(40, 120, 240), state_.btnX, a);
    drawButton_(c, currentLayout_.btnY.x, currentLayout_.btnY.y, 72.0f, "Y", rgba(240, 200, 30), state_.btnY, a);

    // 4. Bumpers & Triggers (vertically stacked pills: 230x90 LT/RT, 230x84 LB/RB)
    drawPillButton_(c, Rect{static_cast<int>(currentLayout_.triggerLt.x), static_cast<int>(currentLayout_.triggerLt.y), 230, 90}, "LT", state_.triggerL2 > 0.5f, a);
    drawPillButton_(c, Rect{static_cast<int>(currentLayout_.bumperLb.x), static_cast<int>(currentLayout_.bumperLb.y), 230, 84}, "LB", state_.btnL1, a);
    drawPillButton_(c, Rect{static_cast<int>(currentLayout_.triggerRt.x), static_cast<int>(currentLayout_.triggerRt.y), 230, 90}, "RT", state_.triggerR2 > 0.5f, a);
    drawPillButton_(c, Rect{static_cast<int>(currentLayout_.bumperRb.x), static_cast<int>(currentLayout_.bumperRb.y), 230, 84}, "RB", state_.btnR1, a);

    // 5. L3 / R3 (bottom corners)
    drawPillButton_(c, Rect{static_cast<int>(currentLayout_.btnL3.x), static_cast<int>(currentLayout_.btnL3.y), 136, 66}, "L3", state_.btnL3, a);
    drawPillButton_(c, Rect{static_cast<int>(currentLayout_.btnR3.x), static_cast<int>(currentLayout_.btnR3.y), 136, 66}, "R3", state_.btnR3, a);

    // 6. Center System / Xbox Nexus at y = 68.0f
    drawButton_(c, currentLayout_.btnView.x, currentLayout_.btnView.y, 42.0f, "", rgba(160, 160, 175), state_.btnTouchpad, a);
    Color iconCol = rgba(255, 255, 255, static_cast<uint8_t>(a * 240.0f));
    c.strokeRect(Rect{static_cast<int>(currentLayout_.btnView.x - 12), static_cast<int>(currentLayout_.btnView.y - 12), 16, 16}, iconCol, 2);
    c.strokeRect(Rect{static_cast<int>(currentLayout_.btnView.x - 4), static_cast<int>(currentLayout_.btnView.y - 4), 16, 16}, iconCol, 2);

    drawButton_(c, currentLayout_.btnNexus.x, currentLayout_.btnNexus.y, 50.0f, "X", rgba(30, 180, 50), state_.btnNexus, a);

    drawButton_(c, currentLayout_.btnMenu.x, currentLayout_.btnMenu.y, 42.0f, "", rgba(160, 160, 175), state_.btnOptions, a);
    c.fillRect(Rect{static_cast<int>(currentLayout_.btnMenu.x - 14), static_cast<int>(currentLayout_.btnMenu.y - 9), 28, 3}, iconCol);
    c.fillRect(Rect{static_cast<int>(currentLayout_.btnMenu.x - 14), static_cast<int>(currentLayout_.btnMenu.y - 1), 28, 3}, iconCol);
    c.fillRect(Rect{static_cast<int>(currentLayout_.btnMenu.x - 14), static_cast<int>(currentLayout_.btnMenu.y + 7), 28, 3}, iconCol);

    if (layoutStudio_) {
        drawLayoutStudioHud_(c);
    }
#endif
}

void TouchPadOverlay::drawLayoutStudioHud_(Canvas& c) {
    // Top banner
    Rect banner{440, 20, 1040, 44};
    c.fillRect(banner, rgba(20, 20, 28, 220), 12);
    c.strokeRect(banner, rgba(255, 255, 255, 40), 1, 12);
    const char* bannerText = tr(Str::TouchStudioBanner);
    int bw = fonts_.semibold.measure(bannerText, 17);
    fonts_.semibold.draw(c, bannerText, banner.x + (banner.w - bw) / 2, fonts_.semibold.centeredY(banner.y, banner.h, 17), 17, rgba(240, 240, 250));

    // Bottom toolbar buttons
    // 1. Restaurar Padrão
    Rect rReset{670, 980, 200, 48};
    c.fillRect(rReset, rgba(35, 35, 42, 230), 12);
    c.strokeRect(rReset, rgba(240, 160, 50, 200), 2, 12);
    const char* txtReset = tr(Str::ResetToDefault);
    int rw = fonts_.semibold.measure(txtReset, 18);
    fonts_.semibold.draw(c, txtReset, rReset.x + (rReset.w - rw) / 2, fonts_.semibold.centeredY(rReset.y, rReset.h, 18), 18, rgba(240, 160, 50));

    // 2. Cancelar
    Rect rCancel{890, 980, 140, 48};
    c.fillRect(rCancel, rgba(50, 50, 58, 230), 12);
    c.strokeRect(rCancel, rgba(160, 160, 175, 180), 2, 12);
    const char* txtCancel = tr(Str::Cancel);
    int cw = fonts_.semibold.measure(txtCancel, 18);
    fonts_.semibold.draw(c, txtCancel, rCancel.x + (rCancel.w - cw) / 2, fonts_.semibold.centeredY(rCancel.y, rCancel.h, 18), 18, rgba(230, 230, 240));

    // 3. Salvar
    Rect rSave{1050, 980, 160, 48};
    c.fillRect(rSave, rgba(16, 124, 16, 240), 12);
    c.strokeRect(rSave, rgba(30, 215, 96, 240), 2, 12);
    const char* txtSave = tr(Str::Save);
    int sw = fonts_.bold.measure(txtSave, 18);
    fonts_.bold.draw(c, txtSave, rSave.x + (rSave.w - sw) / 2, fonts_.bold.centeredY(rSave.y, rSave.h, 18), 18, rgba(255, 255, 255));

    // Highlight selected control and show HUD coordinates
    if (selectedStudioControl_ != TouchControl::None) {
        float hx = 0.0f, hy = 0.0f;
        float hr = 0.0f, hw = 0.0f, hh = 0.0f;
        if (selectedStudioControl_ == TouchControl::AbxyGroup) {
            hx = (currentLayout_.btnA.x + currentLayout_.btnB.x + currentLayout_.btnX.x + currentLayout_.btnY.x) * 0.25f;
            hy = (currentLayout_.btnA.y + currentLayout_.btnB.y + currentLayout_.btnX.y + currentLayout_.btnY.y) * 0.25f;
            hr = 190.0f;
        } else {
            switch (selectedStudioControl_) {
                case TouchControl::LeftStick: hx = currentLayout_.leftStick.x; hy = currentLayout_.leftStick.y; hr = 140.0f; break;
                case TouchControl::RightStick: hx = currentLayout_.rightStick.x; hy = currentLayout_.rightStick.y; hr = 140.0f; break;
                case TouchControl::DpadUp: case TouchControl::DpadDown:
                case TouchControl::DpadLeft: case TouchControl::DpadRight:
                case TouchControl::DpadUpLeft: case TouchControl::DpadUpRight:
                case TouchControl::DpadDownLeft: case TouchControl::DpadDownRight:
                    hx = currentLayout_.dpad.x; hy = currentLayout_.dpad.y; hr = 120.0f; break;
                case TouchControl::ButtonA: hx = currentLayout_.btnA.x; hy = currentLayout_.btnA.y; hr = 76.0f; break;
                case TouchControl::ButtonB: hx = currentLayout_.btnB.x; hy = currentLayout_.btnB.y; hr = 76.0f; break;
                case TouchControl::ButtonX: hx = currentLayout_.btnX.x; hy = currentLayout_.btnX.y; hr = 76.0f; break;
                case TouchControl::ButtonY: hx = currentLayout_.btnY.x; hy = currentLayout_.btnY.y; hr = 76.0f; break;
                case TouchControl::TriggerLT: hx = currentLayout_.triggerLt.x; hy = currentLayout_.triggerLt.y; hw = 230.0f; hh = 90.0f; break;
                case TouchControl::BumperLB: hx = currentLayout_.bumperLb.x; hy = currentLayout_.bumperLb.y; hw = 230.0f; hh = 84.0f; break;
                case TouchControl::TriggerRT: hx = currentLayout_.triggerRt.x; hy = currentLayout_.triggerRt.y; hw = 230.0f; hh = 90.0f; break;
                case TouchControl::BumperRB: hx = currentLayout_.bumperRb.x; hy = currentLayout_.bumperRb.y; hw = 230.0f; hh = 84.0f; break;
                case TouchControl::ButtonL3: hx = currentLayout_.btnL3.x; hy = currentLayout_.btnL3.y; hw = 136.0f; hh = 66.0f; break;
                case TouchControl::ButtonR3: hx = currentLayout_.btnR3.x; hy = currentLayout_.btnR3.y; hw = 136.0f; hh = 66.0f; break;
                case TouchControl::ButtonView: hx = currentLayout_.btnView.x; hy = currentLayout_.btnView.y; hr = 46.0f; break;
                case TouchControl::ButtonNexus: hx = currentLayout_.btnNexus.x; hy = currentLayout_.btnNexus.y; hr = 54.0f; break;
                case TouchControl::ButtonMenu: hx = currentLayout_.btnMenu.x; hy = currentLayout_.btnMenu.y; hr = 46.0f; break;
                default: break;
            }
        }

        Color glowCol = rgba(30, 215, 96, 230);
        if (hw > 0.0f) {
            c.strokeRect(Rect{static_cast<int>(hx - 4), static_cast<int>(hy - 4), static_cast<int>(hw + 8), static_cast<int>(hh + 8)}, glowCol, 3, 14);
        } else if (hr > 0.0f) {
            c.strokeCircle(hx, hy, hr + 4.0f, 3.0f, glowCol);
        }

        char coordBuf[48];
        std::snprintf(coordBuf, sizeof(coordBuf), "X: %.0f  Y: %.0f", hx, hy);
        int cw = fonts_.bold.measure(coordBuf, 14) + 16;
        int cy = static_cast<int>(hw > 0.0f ? (hy - 26) : (hy - hr - 28));
        if (cy < 70) cy = static_cast<int>(hw > 0.0f ? (hy + hh + 6) : (hy + hr + 10));
        int cx = static_cast<int>(hw > 0.0f ? (hx + (hw - cw) / 2) : (hx - cw / 2));
        Rect rHud{cx, cy, cw, 22};
        c.fillRect(rHud, rgba(16, 24, 16, 230), 6);
        c.strokeRect(rHud, glowCol, 1, 6);
        fonts_.bold.draw(c, coordBuf, rHud.x + 8, fonts_.bold.centeredY(rHud.y, rHud.h, 14), 14, rgba(230, 255, 235));
    }
}

}  // namespace xc::ui
