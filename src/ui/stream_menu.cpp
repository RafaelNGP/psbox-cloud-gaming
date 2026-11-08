// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/stream_menu.h"

#include "ui/cloud_time.h"
#include "ui/strings.h"
#include "ui/input_mode.h"
#include "ui/pad_glyphs.h"
#include "ui/app_ui_internal.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdio>

namespace xc::ui {

namespace {

constexpr Color kDark = rgba(20, 20, 20);
constexpr Color kWarnColor = rgba(255, 176, 32);  // the connection warning
constexpr int kBitrates[] = {-1, 0, 15, 20, 25, 30, 35, 40, 45, 50};
constexpr int kBitratesCount = sizeof(kBitrates) / sizeof(kBitrates[0]);

const char* resolutionName(int r) { return r == 1 ? "720p" : r == 2 ? tr(Str::ResBest) : "1080p"; }

std::string fmt(const char* f, double v) {
    char b[32];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

}  // namespace

bool StreamMenu::itemInTab(int item, MenuTab tab) const {
    if (tab == MenuTab::General) {
        return item == XboxButton || item == Profile || item == Stats ||
               item == Upscaler || item == Sharpness || item == Deband ||
               item == Resolution || item == VideoDecoderItem || item == FpsLimiterItem ||
               item == BitrateLimiterItem || item == VrrItem ||
               item == ReloadStreamItem || item == Leave;
    } else { // Controls
        return item == PlayerItem || item == ConfirmItem || item == Triggers ||
               item == AudioHapticsItem || item == DeadzoneItem || item == PadSpeakerVolumeItem ||
               item == TouchpadShortcutsItem || item == GyroItem;
    }
}

bool StreamMenu::itemShown(int item) const {
    if (!itemInTab(item, activeTab_)) return false;
    switch (item) {
    case VideoDecoderItem: return true;
    case FpsLimiterItem: return true;
    case BitrateLimiterItem: return true;
    case VrrItem: return caps_.highRefresh;
    case Upscaler: return caps_.upscalers;
    case Sharpness: return caps_.sharpen;
    case Deband: return caps_.deband;
    case Triggers: return caps_.triggerRumble || caps_.adaptiveTriggers;
    case AudioHapticsItem: return caps_.triggerRumble || caps_.adaptiveTriggers;
    case GyroItem: return caps_.gyro;
    case PadSpeakerVolumeItem: return caps_.triggerRumble || caps_.adaptiveTriggers;
    case TouchpadShortcutsItem: return caps_.touchpad;
    case PlayerItem: return usbOwnerPlayer_ >= 0;
    default: return true;
    }
}

void StreamMenu::selectFirstItem() {
    for (int i = 0; i < ItemCount; ++i) {
        if (itemShown(i)) {
            selected_ = i;
            return;
        }
    }
}

void StreamMenu::setCapabilities(const UiCaps& caps) {
    caps_ = caps;
    while (!itemShown(selected_)) selectFirstItem();
}

void StreamMenu::setProfileValues(int resolution, int sharpness, int deband, int upscaler,
                                  int triggerStrength, int deadzone, bool circleConfirms,
                                  bool hasCustomProfile) {
    resolution_ = applied_ = resolution;
    sharpness_ = sharpness;
    deband_ = deband;
    upscaler_ = upscaler;
    triggerStrength_ = triggerStrength;
    deadzone_ = deadzoneRight_ = deadzone;
    circleConfirms_ = circleConfirms;
    hasCustomProfile_ = hasCustomProfile;
}

void StreamMenu::open(int resolution, bool stats, int sharpness, int deband, int upscaler, bool homeConsole,
                      bool allow1440, bool hasCustomProfile, int triggerStrength, int deadzone,
                      bool circleConfirms, float micLevel, bool micMuted, int padSpeakerVolume) {
    open_ = true;
    homeConsole_ = homeConsole;
    allow1440_ = allow1440;
    stats_ = stats;
    sharpness_ = sharpness;
    deband_ = deband;
    upscaler_ = upscaler;
    hasCustomProfile_ = hasCustomProfile;
    triggerStrength_ = triggerStrength;
    deadzone_ = deadzoneRight_ = deadzone;
    circleConfirms_ = circleConfirms;
    micLevel_ = micLevel;
    micMuted_ = micMuted;
    padSpeakerVolume_ = std::clamp(padSpeakerVolume, 0, 100);
    activeTab_ = MenuTab::General;
    selected_ = XboxButton;
    resolution_ = applied_ = resolution;
    resolutionAsked_ = false;
    statsCustomizing_ = false;
    statsCustomIndex_ = 0;
    touchpadCustomizing_ = false;
    touchpadCustomIndex_ = 0;
    gyroDialog_.close();
    stickDialog_.close();
    triggerDialog_.close();
}

void StreamMenu::update(uint64_t nowMs) {
    if (!open_) return;
    if (gyroDialog_.isOpen()) {
        gyroDialog_.update(nowMs);
    }
    if (stickDialog_.isOpen()) {
        stickDialog_.update(nowMs);
    }
    if (triggerDialog_.isOpen()) {
        triggerDialog_.update(nowMs);
    }
}

MenuAction StreamMenu::handle(const NavInput& in) {
    if (!open_) return MenuAction::None;

    if (gyroDialog_.isOpen()) {
        gyroDialog_.handleInput(in);
        if (!gyroDialog_.isOpen()) {
            gyroLevel_ = gyroDialog_.level();
            gyroYawScale_ = gyroDialog_.yawScale();
            gyroPitchScale_ = gyroDialog_.pitchScale();
            const float* b = gyroDialog_.bias();
            if (b) {
                gyroBias_[0] = b[0]; gyroBias_[1] = b[1]; gyroBias_[2] = b[2];
            }
            return MenuAction::Gyro;
        }
        return MenuAction::None;
    }

    if (stickDialog_.isOpen()) {
        stickDialog_.handleInput(in);
        deadzone_ = stickDialog_.deadzoneLeft();
        deadzoneRight_ = stickDialog_.deadzoneRight();
        if (!stickDialog_.isOpen()) {
            return MenuAction::Deadzone;
        }
        return MenuAction::None;
    }

    if (triggerDialog_.isOpen()) {
        triggerDialog_.handleInput(in);
        triggerStrength_ = triggerDialog_.strength();
        triggerHz_ = triggerDialog_.hz();
        triggerResistance_ = triggerDialog_.resistance();
        triggerPulses_ = triggerDialog_.pulses();
        triggerProfile_ = triggerDialog_.profile();
        if (!triggerDialog_.isOpen()) {
            return MenuAction::Triggers;
        }
        return MenuAction::None;
    }

    if (statsCustomizing_) {
        if (in.back) {
            statsCustomizing_ = false;
            return MenuAction::Stats;
        }
        if (in.options) {
            statsCustomizing_ = false;
            open_ = false;
            return MenuAction::Close;
        }
        constexpr int kCount = 8;
        if (in.up) statsCustomIndex_ = (statsCustomIndex_ + kCount - 1) % kCount;
        if (in.down) statsCustomIndex_ = (statsCustomIndex_ + 1) % kCount;
        if (in.accept || in.left || in.right) {
            static constexpr uint32_t kMasks[kCount] = {
                StatsFps, StatsBitrate, StatsLatency, StatsLoss,
                StatsGraph, StatsDelay, StatsQp, StatsMic
            };
            statsMask_ ^= kMasks[statsCustomIndex_];
            return MenuAction::Stats;
        }
        return MenuAction::None;
    }

    if (touchpadCustomizing_) {
        if (in.back) {
            touchpadCustomizing_ = false;
            return MenuAction::TouchpadShortcuts;
        }
        if (in.options) {
            touchpadCustomizing_ = false;
            open_ = false;
            return MenuAction::Close;
        }
        constexpr int kRows = 7;  // 6 triggers + Reset to default
        if (in.up) touchpadCustomIndex_ = (touchpadCustomIndex_ + kRows - 1) % kRows;
        if (in.down) touchpadCustomIndex_ = (touchpadCustomIndex_ + 1) % kRows;
        input::TouchpadAction* fields[6] = {
            &touchpadShortcuts_.swipeUp,    &touchpadShortcuts_.swipeDown,
            &touchpadShortcuts_.swipeLeft,  &touchpadShortcuts_.swipeRight,
            &touchpadShortcuts_.clickLeft,  &touchpadShortcuts_.clickRight,
        };
        if (touchpadCustomIndex_ < 6) {
            if (in.left || in.right || in.accept) {
                int dir = in.left ? -1 : 1;
                int cur = static_cast<int>(*fields[touchpadCustomIndex_]);
                int next = (cur + input::kTouchpadActions + dir) % input::kTouchpadActions;
                *fields[touchpadCustomIndex_] = static_cast<input::TouchpadAction>(next);
                return MenuAction::TouchpadShortcuts;
            }
            if (in.square) {
                *fields[touchpadCustomIndex_] = input::TouchpadAction::None;
                return MenuAction::TouchpadShortcuts;
            }
        } else if (in.accept) {
            touchpadShortcuts_ = input::defaultTouchpadShortcuts();
            return MenuAction::TouchpadShortcuts;
        }
        return MenuAction::None;
    }

    if (in.back || in.options) {
        open_ = false;
        return MenuAction::Close;
    }

    // Shoulder tabs switching (L1 / R1)
    if (in.l1) {
        activeTab_ = (activeTab_ == MenuTab::Controls) ? MenuTab::General : MenuTab::Controls;
        selectFirstItem();
        return MenuAction::None;
    }
    if (in.r1) {
        activeTab_ = (activeTab_ == MenuTab::General) ? MenuTab::Controls : MenuTab::General;
        selectFirstItem();
        return MenuAction::None;
    }

    // Up / down within active tab
    if (in.up) do selected_ = (selected_ + ItemCount - 1) % ItemCount; while (!itemShown(selected_));
    if (in.down) do selected_ = (selected_ + 1) % ItemCount; while (!itemShown(selected_));

    if (selected_ == PlayerItem && (in.left || in.right || in.accept)) {
        usbOwnerPlayer_ = (usbOwnerPlayer_ + (in.left ? 3 : 1)) % 4;
        return MenuAction::Player;
    }
    if (selected_ == Profile && (in.left || in.right || in.accept)) {
        hasCustomProfile_ = !hasCustomProfile_;
        return MenuAction::ProfileToggle;
    }
    if (selected_ == Resolution && (in.left || in.right)) {
        int pos = 0;
        int n = 3;  // 720p, 1080p, Best (Auto)
        for (int i = 0; i < n; ++i)
            if (kResolutionOrder[i] == resolution_) pos = i;
        pos = in.left ? (pos + n - 1) % n : (pos + 1) % n;
        resolution_ = kResolutionOrder[pos];
    }
    if (selected_ == VideoDecoderItem && (in.left || in.right || in.accept)) {
        videoDecoder_ = (videoDecoder_ == 0) ? 1 : 0;
        return MenuAction::VideoDecoder;
    }
    if (selected_ == FpsLimiterItem && (in.left || in.right)) {
        static constexpr int kFpsLimits[4] = {0, 30, 60, 120};
        int pos = 0;
        for (int i = 0; i < 4; ++i) {
            if (kFpsLimits[i] == fpsLimit_) pos = i;
        }
        pos = in.left ? (pos + 3) % 4 : (pos + 1) % 4;
        fpsLimit_ = kFpsLimits[pos];
        return MenuAction::FpsLimiter;
    }
    if (selected_ == BitrateLimiterItem && (in.left || in.right)) {
        int pos = 1; // default to Auto (0)
        for (int i = 0; i < kBitratesCount; ++i) {
            if (kBitrates[i] == maxBitrateMbps_) pos = i;
        }
        pos = in.left ? (pos + kBitratesCount - 1) % kBitratesCount : (pos + 1) % kBitratesCount;
        maxBitrateMbps_ = kBitrates[pos];
        return MenuAction::BitrateLimiter;
    }
    if (selected_ == VrrItem && (in.left || in.right || in.accept)) {
        highRefresh_ = !highRefresh_;
        return MenuAction::VrrToggle;
    }
    if ((selected_ == Sharpness && (in.left || in.right)) || (selected_ == Sharpness && in.accept)) {
        sharpness_ = in.left ? (sharpness_ + 3) % 4 : (sharpness_ + 1) % 4;
        return MenuAction::Sharpness;
    }
    if (selected_ == Upscaler && (in.left || in.right || in.accept)) {
        static constexpr int kOrder[4] = {0, 2, 1, 3};
        int pos = 0;
        for (int i = 0; i < 4; ++i)
            if (kOrder[i] == upscaler_) pos = i;
        upscaler_ = kOrder[in.left ? (pos + 3) % 4 : (pos + 1) % 4];
        return MenuAction::Upscaler;
    }
    if (selected_ == Deband && (in.left || in.right || in.accept)) {
        deband_ = in.left ? (deband_ + 3) % 4 : (deband_ + 1) % 4;
        return MenuAction::Deband;
    }
    if (selected_ == Triggers && (in.left || in.right)) {
        triggerStrength_ = in.left ? (triggerStrength_ + 4) % 5 : (triggerStrength_ + 1) % 5;
        return MenuAction::Triggers;
    }
    if (selected_ == Triggers && in.accept) {
        triggerDialog_.open(triggerStrength_, triggerHz_, triggerResistance_, triggerPulses_,
                            triggerProfile_, caps_.adaptiveTriggers, gameKey_);
        return MenuAction::None;
    }
    if (selected_ == AudioHapticsItem && (in.left || in.right || in.accept)) {
        audioHaptics_ = in.left ? (audioHaptics_ + 3) % 4 : (audioHaptics_ + 1) % 4;
        return MenuAction::AudioHaptics;
    }
    if (selected_ == DeadzoneItem && (in.left || in.right)) {
        if (in.left && deadzone_ > 0) deadzone_ = std::max(0, deadzone_ - 5);
        else if (in.right && deadzone_ < 30) deadzone_ = std::min(30, deadzone_ + 5);
        deadzoneRight_ = deadzone_;
        return MenuAction::Deadzone;
    }
    if (selected_ == DeadzoneItem && in.accept) {
        stickDialog_.open(deadzone_, deadzoneRight_, gameKey_);
        return MenuAction::None;
    }
    if (selected_ == ConfirmItem && (in.left || in.right || in.accept)) {
        circleConfirms_ = !circleConfirms_;
        return MenuAction::ConfirmButton;
    }
    if (selected_ == GyroItem && (in.left || in.right)) {
        gyroMode_ = in.right ? (gyroMode_ + 1) % 3 : (gyroMode_ + 2) % 3;
        return MenuAction::Gyro;
    }
    if (selected_ == GyroItem && in.accept) {
        gyroDialog_.open(0, gameKey_);
        return MenuAction::None;
    }
    if (selected_ == PadSpeakerVolumeItem && (in.left || in.right)) {
        int newVol = std::clamp(padSpeakerVolume_ + (in.right ? 5 : -5), 0, 100);
        if (newVol == padSpeakerVolume_) return MenuAction::None;
        padSpeakerVolume_ = newVol;
        return MenuAction::PadSpeakerVolume;
    }
    if (selected_ == TouchpadShortcutsItem && (in.accept || in.left || in.right)) {
        touchpadCustomizing_ = true;
        touchpadCustomIndex_ = 0;
        return MenuAction::TouchpadShortcuts;
    }
    if (selected_ == Stats && (in.left || in.right)) {
        stats_ = !stats_;
        return MenuAction::Stats;
    }
    if (selected_ == Stats && (in.square || in.accept)) {
        statsCustomizing_ = true;
        stats_ = true;
        return MenuAction::Stats;
    }

    if (!in.accept) return MenuAction::None;
    switch (selected_) {
    case XboxButton: open_ = false; return MenuAction::XboxButton;
    case Resolution:
        applied_ = resolution_;
        resolutionAsked_ = true;
        return MenuAction::Resolution;
    case ReloadStreamItem: open_ = false; return MenuAction::ReloadStream;
    case Leave: open_ = false; return MenuAction::Leave;
    default: return MenuAction::None;
    }
}

Canvas StreamMenu::renderMenu(const StreamInfo& info) const {
    const int canvasW = (!statsCustomizing_ && !touchpadCustomizing_) ? kCombinedW : kMenuW;
    Canvas c(canvasW, kMenuH);
    std::fill(c.data(), c.data() + static_cast<size_t>(canvasW) * kMenuH, 0u);
    c.fillRect({0, 0, kMenuW, kMenuH}, kPanel, 0);
    c.fillRect({0, 0, kMenuW, 6}, kGreen);
    constexpr int kPad = 40;

    auto icon = [&](bool cross, int x, int hy) {
        if (g_glyphStyle == GlyphStyle::Xbox) {  // A / B
            drawFaceButton(c, fonts_.bold, GlyphStyle::Xbox, cross ? PadButton::Cross : PadButton::Circle, x + 13.0f,
                           hy + 12.0f, 13);
            return;
        }
        c.fillCircle(x + 13, hy + 12, 13, rgba(255, 255, 255, 40));
        if (cross) {
            c.line(x + 7, hy + 6, x + 19, hy + 18, 2.5f, rgba(124, 178, 232));
            c.line(x + 19, hy + 6, x + 7, hy + 18, 2.5f, rgba(124, 178, 232));
        } else {
            c.strokeArc(x + 13, hy + 12, 6.0f, 2.5f, 0, 6.2832f, rgba(255, 102, 102));
        }
    };

    if (statsCustomizing_) {
        fonts_.bold.draw(c, tr(Str::StatsCustomize), kPad, 38, 28, kWhite);
        fonts_.regular.draw(c, tr(Str::MenuStats), kPad, 74, 16, kDim);

        struct IndicatorDef {
            uint32_t mask;
            Str label;
        };
        static constexpr IndicatorDef kIndicators[8] = {
            {StatsFps, Str::StatsIndicatorFps},
            {StatsBitrate, Str::StatsIndicatorBitrate},
            {StatsLatency, Str::StatsIndicatorLatency},
            {StatsLoss, Str::StatsIndicatorLoss},
            {StatsGraph, Str::StatsIndicatorGraph},
            {StatsDelay, Str::StatsIndicatorDelay},
            {StatsQp, Str::StatsIndicatorQp},
            {StatsMic, Str::StatsIndicatorMic},
        };

        constexpr int kRowH = 46, kTop = 108, kPx = 20;
        for (int i = 0; i < 8; ++i) {
            Rect row{kPad - 16, kTop + i * kRowH, kMenuW - 2 * (kPad - 16), kRowH - 4};
            bool sel = i == statsCustomIndex_;
            if (sel) c.fillRect(row, kWhite, 8);
            Color fg = sel ? kDark : kWhite;
            bool on = (statsMask_ & kIndicators[i].mask) != 0;

            int boxX = row.x + 16;
            int boxY = row.y + (row.h - 22) / 2;
            if (on) {
                c.fillRect({boxX, boxY, 22, 22}, sel ? rgba(16, 124, 16) : rgba(30, 215, 96), 5);
                c.line(boxX + 5, boxY + 11, boxX + 9, boxY + 16, 2.5f, kWhite);
                c.line(boxX + 9, boxY + 16, boxX + 17, boxY + 7, 2.5f, kWhite);
            } else {
                c.strokeRect({boxX, boxY, 22, 22}, sel ? kDark : kDim, 2, 5);
            }

            int ty = fonts_.semibold.centeredY(row.y, row.h, kPx);
            fonts_.semibold.draw(c, tr(kIndicators[i].label), boxX + 34, ty, kPx, fg);

            const char* st = tr(on ? Str::On : Str::Off);
            int sw = fonts_.semibold.measure(st, 18);
            fonts_.semibold.draw(c, st, row.x + row.w - 16 - sw, ty, 18, sel ? kDark : kGray);
        }

        int hy = kMenuH - 50, x = kPad;
        if (g_keyboardHints) {
            auto cap = [&](const char* key) {
                int kw = fonts_.bold.measure(key, 15) + 18;
                c.fillRect({x, hy, kw, 26}, rgba(255, 255, 255, 30), 6);
                c.strokeRect({x, hy, kw, 26}, kGray, 2, 6);
                fonts_.bold.draw(c, key, x + 9, fonts_.bold.centeredY(hy, 26, 15), 15, kWhite);
                return kw;
            };
            int kw = cap("Enter");
            x = fonts_.semibold.draw(c, tr(Str::Select), x + kw + 8, hy, 20, kGray) + 32;
            kw = cap("Esc");
            fonts_.semibold.draw(c, tr(Str::Back), x + kw + 8, hy, 20, kGray);
        } else {
            icon(!circleConfirms_, x, hy);
            x = fonts_.semibold.draw(c, tr(Str::Select), x + 34, hy, 20, kGray) + 32;
            icon(circleConfirms_, x, hy);
            fonts_.semibold.draw(c, tr(Str::Back), x + 34, hy, 20, kGray);
        }
        return c;
    }

    if (touchpadCustomizing_) {
        fonts_.bold.draw(c, tr(Str::TouchpadShortcuts), kPad, 38, 28, kWhite);
        auto descLines = fonts_.regular.wrap(tr(Str::SettingDescTouchpadShortcuts), 16, kMenuW - 2 * kPad, 2);
        int dy = 74;
        for (const auto& l : descLines) {
            fonts_.regular.draw(c, l, kPad, dy, 16, kDim);
            dy += 20;
        }

        const input::TouchpadAction vals[6] = {
            touchpadShortcuts_.swipeUp,    touchpadShortcuts_.swipeDown,
            touchpadShortcuts_.swipeLeft,  touchpadShortcuts_.swipeRight,
            touchpadShortcuts_.clickLeft,  touchpadShortcuts_.clickRight,
        };
        constexpr int kRowH = 48, kTop = 126, kPx = 19;
        for (int i = 0; i < 6; ++i) {
            Rect row{kPad - 16, kTop + i * kRowH, kMenuW - 2 * (kPad - 16), kRowH - 4};
            bool sel = i == touchpadCustomIndex_;
            if (sel) c.fillRect(row, kWhite, 8);
            Color fg = sel ? kDark : kWhite;
            int ty = fonts_.semibold.centeredY(row.y, row.h, kPx);
            fonts_.semibold.draw(c, touchpadTriggerLabel(i), row.x + 16, ty, kPx, fg);
            std::string val = touchpadActionLabel(static_cast<int>(vals[i]));
            if (sel) val = "< " + val + " >";
            int sw = fonts_.semibold.measure(val, 18);
            fonts_.semibold.draw(c, val, row.x + row.w - 16 - sw, ty, 18, sel ? kDark : kGray);
        }
        Rect resetRow{kPad - 16, kTop + 6 * kRowH + 8, kMenuW - 2 * (kPad - 16), kRowH - 4};
        bool resetSel = (touchpadCustomIndex_ == 6);
        if (resetSel) c.fillRect(resetRow, kWhite, 8);
        const char* resetLabel = tr(Str::ResetToDefault);
        int rw = fonts_.semibold.measure(resetLabel, kPx);
        int rty = fonts_.semibold.centeredY(resetRow.y, resetRow.h, kPx);
        fonts_.semibold.draw(c, resetLabel, resetRow.x + (resetRow.w - rw) / 2, rty, kPx,
                             resetSel ? kDark : rgba(240, 100, 100));

        int hy = kMenuH - 50, x = kPad;
        icon(!circleConfirms_, x, hy);
        x = fonts_.semibold.draw(c, tr(Str::Change), x + 34, hy, 20, kGray) + 32;
        icon(circleConfirms_, x, hy);
        fonts_.semibold.draw(c, tr(Str::Back), x + 34, hy, 20, kGray);
        return c;
    }

    // Header: Title and Cloud Time
    const int titleEnd = fonts_.bold.draw(c, tr(Str::MenuTitle), kPad, 30, 28, kWhite);
    if (info.cloudTimeSeconds >= 0) {
        const std::string left = trf(Str::CloudTimeLeft, formatCloudTime(info.cloudTimeSeconds));
        const int w = fonts_.semibold.measure(left, 16);
        const int x = kMenuW - kPad - w;
        if (x - 24 > titleEnd + 16) {
            const Color col = info.cloudTimeSeconds <= 0        ? rgba(240, 100, 100)
                              : info.cloudTimeSeconds < 16 * 60 ? rgba(255, 160, 40)
                                                                 : kGray;
            drawClockIcon(c, x - 14.0f, 30 + 13.0f, 7.5f, col);
            fonts_.semibold.draw(c, left, x, fonts_.semibold.centeredY(30, 28, 16), 16, col);
        }
    }

    // Tabs Header: [L1]  Geral  |  Controles  [R1]
    int tabY = 66, tabH = 34;
    {
        int bx = kPad;
        // L1 badge
        c.fillRect({bx, tabY + 4, 34, 24}, rgba(255, 255, 255, 30), 4);
        fonts_.bold.draw(c, "L1", bx + 8, fonts_.bold.centeredY(tabY + 4, 24, 14), 14, kWhite);
        bx += 42;

        // General Tab
        bool genActive = (activeTab_ == MenuTab::General);
        const char* tabGen = tr(Str::MenuTabGeneral);
        int gw = fonts_.semibold.measure(tabGen, 18) + 24;
        Rect genRect{bx, tabY, gw, tabH};
        if (genActive) {
            c.fillRect(genRect, rgba(255, 255, 255, 24), 6);
            c.fillRect({genRect.x + 6, genRect.y + genRect.h - 3, genRect.w - 12, 3}, kGreen, 2);
        }
        fonts_.semibold.draw(c, tabGen, genRect.x + 12, fonts_.semibold.centeredY(genRect.y, genRect.h, 18), 18,
                             genActive ? kWhite : kDim);
        bx += gw + 10;

        // Separator
        fonts_.regular.draw(c, "|", bx, fonts_.regular.centeredY(tabY, tabH, 16), 16, rgba(80, 80, 80));
        bx += 16;

        // Controls Tab
        bool ctrlActive = (activeTab_ == MenuTab::Controls);
        const char* tabCtrl = tr(Str::Controllers);
        int cw = fonts_.semibold.measure(tabCtrl, 18) + 24;
        Rect ctrlRect{bx, tabY, cw, tabH};
        if (ctrlActive) {
            c.fillRect(ctrlRect, rgba(255, 255, 255, 24), 6);
            c.fillRect({ctrlRect.x + 6, ctrlRect.y + ctrlRect.h - 3, ctrlRect.w - 12, 3}, kGreen, 2);
        }
        fonts_.semibold.draw(c, tabCtrl, ctrlRect.x + 12, fonts_.semibold.centeredY(ctrlRect.y, ctrlRect.h, 18), 18,
                             ctrlActive ? kWhite : kDim);
        bx += cw + 10;

        // R1 badge
        c.fillRect({bx, tabY + 4, 34, 24}, rgba(255, 255, 255, 30), 4);
        fonts_.bold.draw(c, "R1", bx + 7, fonts_.bold.centeredY(tabY + 4, 24, 14), 14, kWhite);
    }

    // The Items List
    constexpr int kRowH = 38, kTop = 108, kPx = 18, kMaxVisible = 14;
    int shownList[ItemCount];
    int totalShown = 0;
    int selIdx = 0;
    for (int i = 0; i < ItemCount; ++i) {
        if (!itemShown(i)) continue;
        if (i == selected_) selIdx = totalShown;
        shownList[totalShown++] = i;
    }
    int startIdx = 0;
    if (totalShown > kMaxVisible) {
        startIdx = std::clamp(selIdx - kMaxVisible / 2, 0, totalShown - kMaxVisible);
    }
    int visibleCount = std::min(totalShown, kMaxVisible);

    for (int v = 0; v < visibleCount; ++v) {
        int i = shownList[startIdx + v];
        Rect row{kPad - 16, kTop + v * kRowH, kMenuW - 2 * (kPad - 16), kRowH - 4};
        bool sel = i == selected_;
        if (sel) c.fillRect(row, kWhite, 8);
        Color fg = sel ? kDark : kWhite;
        int ty = fonts_.semibold.centeredY(row.y, row.h, kPx);

        const char* label = i == XboxButton            ? tr(Str::MenuXboxButton)
                            : i == PlayerItem          ? tr(Str::MenuPlayer)
                            : i == Profile             ? tr(Str::Profile)
                            : i == Stats               ? tr(Str::MenuStats)
                            : i == Resolution          ? tr(Str::MenuResolution)
                            : i == VideoDecoderItem    ? tr(Str::VideoDecoder)
                            : i == FpsLimiterItem      ? tr(Str::FpsLimit)
                            : i == BitrateLimiterItem  ? tr(Str::BitrateCap)
                            : i == VrrItem             ? tr(Str::HighRefresh)
                            : i == Sharpness           ? tr(Str::MenuSharpness)
                            : i == Deband              ? tr(Str::MenuDeband)
                            : i == Upscaler            ? tr(Str::MenuUpscaler)
                            : i == ConfirmItem         ? tr(Str::ConfirmButton)
                            : i == Triggers            ? tr(Str::TriggerRumble)
                            : i == AudioHapticsItem    ? tr(Str::AudioHaptics)
                            : i == DeadzoneItem        ? tr(Str::Deadzone)
                            : i == PadSpeakerVolumeItem ? tr(Str::PadSpeakerVolume)
                            : i == TouchpadShortcutsItem ? tr(Str::TouchpadShortcuts)
                            : i == GyroItem            ? tr(Str::Gyro)
                            : i == ReloadStreamItem    ? tr(Str::ReloadStream)
                            : homeConsole_             ? tr(Str::MenuEndStream)
                                                       : tr(Str::MenuLeave);
        fonts_.semibold.draw(c, label, row.x + 16, ty, kPx, fg);
        std::string value;

        if (i == PlayerItem) {
            value = std::to_string(usbOwnerPlayer_ + 1);
            if (sel) value = "< " + value + " >";
        }
        if (i == Profile) {
            value = tr(hasCustomProfile_ ? Str::ProfileCustom : Str::ProfileDefault);
            if (sel) value = "< " + value + " >";
        }
        if (i == Stats) {
            value = tr(stats_ ? Str::On : Str::Off);
            if (sel) value = "< " + value + " >";
        }
        if (i == Resolution) {
            value = resolutionName(resolution_);
            if (sel) value = "< " + value + " >";
        }
        if (i == VideoDecoderItem) {
            value = (videoDecoder_ == 1) ? tr(Str::VideoDecoderSw) : tr(Str::VideoDecoderHw);
            if (sel) value = "< " + value + " >";
        }
        if (i == FpsLimiterItem) {
            value = (fpsLimit_ == 0) ? tr(Str::FpsUnlimited) : (std::to_string(fpsLimit_) + " fps");
            if (sel) value = "< " + value + " >";
        }
        if (i == BitrateLimiterItem) {
            value = (maxBitrateMbps_ == -1) ? tr(Str::BitrateUnlimited)
                    : (maxBitrateMbps_ == 0)  ? tr(Str::BitrateAuto)
                                              : (std::to_string(maxBitrateMbps_) + " Mbps");
            if (sel) value = "< " + value + " >";
        }
        if (i == VrrItem) {
            value = highRefresh_ ? "120 Hz (VRR)" : "60 Hz";
            if (sel) value = "< " + value + " >";
        }
        if (i == Sharpness) {
            static constexpr Str kLevels[] = {Str::SharpOff, Str::SharpLow, Str::SharpMedium, Str::SharpHigh};
            value = tr(kLevels[sharpness_]);
            if (sel) value = "< " + value + " >";
        }
        if (i == Deband) {
            static constexpr Str kLevels[] = {Str::SharpOff, Str::SharpLow, Str::SharpHigh};
            value = deband_ == 3 ? trf(Str::DebandAuto, tr(kLevels[std::clamp(debandInUse_, 0, 2)]))
                                 : tr(kLevels[std::clamp(deband_, 0, 2)]);
            if (sel) value = "< " + value + " >";
        }
        if (i == Upscaler) {
            value = upscaler_ == 3   ? tr(Str::UpscalerAiClean)
                    : upscaler_ == 2 ? tr(Str::UpscalerFsrClean)
                    : upscaler_ == 1 ? tr(Str::UpscalerAi)
                                     : "FSR";
            if (sel) value = "< " + value + " >";
        }
        if (i == ConfirmItem) {
            value = circleConfirms_ ? "○" : "×";
            if (sel) value = "< " + value + " >";
        }
        if (i == Triggers) {
            static constexpr Str kStrengths[] = {Str::Deactivated, Str::TriggerLight, Str::TriggerMedium,
                                                 Str::TriggerStrong, Str::TriggerMax};
            value = tr(kStrengths[std::clamp(triggerStrength_, 0, 4)]);
            if (sel) value = "< " + value + " >";
        }
        if (i == AudioHapticsItem) {
            static constexpr Str kHapticsLevels[] = {Str::Off, Str::SharpLow, Str::SharpMedium, Str::SharpHigh};
            value = tr(kHapticsLevels[std::clamp(audioHaptics_, 0, 3)]);
            if (sel) value = "< " + value + " >";
        }
        if (i == DeadzoneItem) {
            value = (deadzone_ == deadzoneRight_)
                  ? (std::to_string(deadzone_) + " %")
                  : (std::to_string(deadzone_) + "% / " + std::to_string(deadzoneRight_) + "%");
            if (sel) value = "< " + value + " >";
        }
        if (i == PadSpeakerVolumeItem) {
            value = padSpeakerVolume_ == 0 ? tr(Str::Mute) : std::to_string(padSpeakerVolume_) + " %";
            if (sel) value = "< " + value + " >";
        }
        if (i == TouchpadShortcutsItem) {
            value = tr(touchpadShortcuts_ == input::defaultTouchpadShortcuts() ? Str::ProfileDefault : Str::ProfileCustom);
        }
        if (i == GyroItem) {
            value = tr(gyroMode_ == 1 ? Str::GyroAlways
                     : gyroMode_ == 2 ? Str::GyroAiming
                                      : Str::Off);
            if (sel) value = "< " + value + " >";
        }

        const bool hasSubmenu = (i == Stats || i == Triggers || i == DeadzoneItem ||
                                 i == TouchpadShortcutsItem || i == GyroItem);
        int pillLeft = row.x + row.w - 14;
        if (hasSubmenu) {
            const int pillW = sel ? 46 : 26;
            const int pillH = 22;
            const int pillX = row.x + row.w - 12 - pillW;
            const int pillY = row.y + (row.h - pillH) / 2;
            pillLeft = pillX;

            c.fillRect({pillX, pillY, pillW, pillH},
                       sel ? rgba(24, 28, 32) : rgba(255, 255, 255, 22), 6);
            c.strokeRect({pillX, pillY, pillW, pillH},
                         sel ? rgba(22, 163, 74) : rgba(255, 255, 255, 55), 1, 6);

            if (sel) {
                if (g_keyboardHints) {
                    fonts_.bold.draw(c, "OK", pillX + 5, fonts_.bold.centeredY(pillY, pillH, 11), 11, rgba(74, 222, 128));
                } else {
                    drawFaceButton(c, fonts_.bold, g_glyphStyle,
                                   circleConfirms_ ? PadButton::Circle : PadButton::Cross,
                                   static_cast<float>(pillX + 14), static_cast<float>(pillY + pillH / 2), 8.0f);
                }
            }

            const float cx = static_cast<float>(pillX + pillW - 12);
            const float cy = static_cast<float>(pillY + pillH / 2);
            const Color arrowCol = sel ? rgba(74, 222, 128) : rgba(215, 215, 215);
            c.line(cx - 2.0f, cy - 4.0f, cx + 2.5f, cy, 2.0f, arrowCol);
            c.line(cx + 2.5f, cy, cx - 2.0f, cy + 4.0f, 2.0f, arrowCol);
        }

        if (!value.empty()) {
            int w = fonts_.semibold.measure(value, kPx);
            int rx = hasSubmenu ? (pillLeft - 10 - w) : (row.x + row.w - 16 - w);
            fonts_.semibold.draw(c, value, rx, ty, kPx, sel ? kDark : kGray);
        }
    }

    if (totalShown > kMaxVisible) {
        int trackH = visibleCount * kRowH - 8;
        int trackX = kMenuW - 14;
        int trackY = kTop + 2;
        c.fillRect({trackX, trackY, 4, trackH}, rgba(60, 60, 60), 2);
        int thumbH = std::max(24, trackH * kMaxVisible / totalShown);
        int thumbY = trackY + (trackH - thumbH) * startIdx / (totalShown - kMaxVisible);
        c.fillRect({trackX, thumbY, 4, thumbH}, kGray, 2);
    }

    // LOWER SECTION (Utilizing lower empty third):
    // 1. Connection Statistics: 2 Columns at y = 716
    c.fillRect({kPad, 704, kMenuW - 2 * kPad, 2}, rgba(60, 60, 60));
    telemetry_.drawTwoColumnStats(c, fonts_, kPad, 716, kMenuW - 2 * kPad, 85, info);

    // 2. Controllers Header & Microphone VU Meter at y = 824
    c.fillRect({kPad, 812, kMenuW - 2 * kPad, 1}, rgba(50, 50, 50));
    fonts_.regular.draw(c, tr(Str::Controllers), kPad, 824, 18, kGray);

    {
        int barW = 140, barH = 12;
        int barX = kMenuW - kPad - barW;
        int barY = 826;
        std::string micLabel = tr(Str::MenuMicrophone);
        int mw = fonts_.regular.measure(micLabel, 15);
        fonts_.regular.draw(c, micLabel, barX - mw - 10, 824, 15, micMuted_ ? kDim : kGray);

        c.fillRect({barX, barY, barW, barH}, rgba(40, 40, 40), 6);
        c.strokeRect({barX, barY, barW, barH}, rgba(70, 70, 70), 1, 6);

        if (micMuted_) {
            int tw = fonts_.semibold.measure(tr(Str::MicMuted), 12);
            fonts_.semibold.draw(c, tr(Str::MicMuted), barX + (barW - tw) / 2, barY - 1, 12, rgba(230, 120, 50));
        } else {
            float lvl = std::clamp(micLevel_, 0.0f, 1.0f);
            int fillW = static_cast<int>(lvl * (barW - 4));
            if (fillW > 0) {
                c.fillRect({barX + 2, barY + 2, fillW, barH - 4}, rgba(30, 215, 96), 4);
                if (lvl > 0.85f) {
                    int peakW = static_cast<int>((lvl - 0.85f) / 0.15f * (barW - 4));
                    c.fillRect({barX + barW - 2 - peakW, barY + 2, peakW, barH - 4}, rgba(255, 204, 0), 4);
                }
            } else {
                c.fillCircle(barX + 7, barY + barH / 2, 2.5f, rgba(30, 215, 96, 160));
            }
        }
    }

    // 3. Controller Slots at y = 852
    const int slotW = (kMenuW - 2 * kPad) / static_cast<int>(pads_.size());
    for (int i = 0; i < static_cast<int>(pads_.size()); ++i) {
        int sx = kPad + i * slotW;
        constexpr int kIconW = 56;
        drawSlotIcon(c, fonts_.bold, sx + (slotW - kIconW) / 2, 852, kIconW, i, pads_[i], kPanel,
                     fonts_.hasIcons ? &fonts_.icons : nullptr);
        if (!pads_[i].connected || pads_[i].name.empty()) continue;
        auto name = fonts_.regular.wrap(pads_[i].name, 15, slotW - 8, 1);
        if (!name.empty())
            fonts_.regular.draw(c, name[0], sx + (slotW - fonts_.regular.measure(name[0], 15)) / 2,
                                852 + padIconHeight(kIconW) + 2, 15, kGray);
        if (i == 0) {
            if (micMuted_) {
                c.fillCircle(sx + (slotW + kIconW) / 2 - 2, 852 + 8, 3.5f, rgba(230, 120, 50));
            } else {
                c.fillCircle(sx + (slotW + kIconW) / 2 - 2, 852 + 8, 3.5f, rgba(30, 215, 96));
            }
        }
    }

    // 4. Gestures and Button Hints at bottom
    c.fillRect({kPad, 930, kMenuW - 2 * kPad, 1}, rgba(50, 50, 50));
    int hy = kMenuH - 50, x = kPad;
    {
        Str hint = g_keyboardHints ? Str::MenuKeyHint : caps_.touchpad ? Str::MenuGestureHint : Str::MenuButtonHint;
        auto lines = fonts_.regular.wrap(tr(hint), 15, kMenuW - 2 * kPad, 2);
        int gy = 944;
        for (const auto& l : lines) {
            fonts_.regular.draw(c, l, kPad, gy, 15, kDim);
            gy += 20;
        }
    }

    // Side Card: SmartGlass Web Gamepad QR Card (permanent unless customizing)
    if (!statsCustomizing_ && !touchpadCustomizing_) {
        const int cardX = kMenuW + kCardGap;
        const int cardY = 204;
        WebControlCard::drawCard(c, fonts_, cardX, cardY, kCardW, 640);
    }

    const bool subItem = (selected_ == Stats || selected_ == Triggers || selected_ == DeadzoneItem ||
                          selected_ == TouchpadShortcutsItem || selected_ == GyroItem);
    const char* confirmLabel = (selected_ == Stats) ? tr(Str::StatsCustomize)
                             : subItem              ? tr(Str::Adjust)
                                                    : tr(Str::Select);

    // Action Hints
    if (g_keyboardHints) {
        auto cap = [&](const char* key) {
            int kw = fonts_.bold.measure(key, 15) + 18;
            c.fillRect({x, hy, kw, 26}, rgba(255, 255, 255, 30), 6);
            c.strokeRect({x, hy, kw, 26}, kGray, 2, 6);
            fonts_.bold.draw(c, key, x + 9, fonts_.bold.centeredY(hy, 26, 15), 15, kWhite);
            return kw;
        };
        int kw = cap("Enter");
        x = fonts_.semibold.draw(c, confirmLabel, x + kw + 8, hy, 20, kGray) + 24;
        kw = cap("Esc");
        x = fonts_.semibold.draw(c, tr(Str::Back), x + kw + 8, hy, 20, kGray) + 24;
        kw = cap("Q/E");
        fonts_.semibold.draw(c, tr(Str::MenuTabLabel), x + kw + 8, hy, 20, kGray);
        return c;
    }

    icon(!circleConfirms_, x, hy);
    x = fonts_.semibold.draw(c, confirmLabel, x + 34, hy, 20, kGray) + 24;
    icon(circleConfirms_, x, hy);
    x = fonts_.semibold.draw(c, tr(Str::Back), x + 34, hy, 20, kGray) + 24;

    // L1/R1 Tab Switch Hint
    c.fillRect({x, hy + 2, 54, 22}, rgba(255, 255, 255, 30), 4);
    fonts_.bold.draw(c, "L1/R1", x + 6, fonts_.bold.centeredY(hy + 2, 22, 13), 13, kWhite);
    fonts_.semibold.draw(c, tr(Str::MenuTabsLabel), x + 60, hy, 20, kGray);

    return c;
}

void StreamMenu::recordFrametime(float ms) const {
    if (ms <= 0.0f || ms > 250.0f) return;
    frametimes_.push_back(ms);
    if (frametimes_.size() > 60) frametimes_.erase(frametimes_.begin());
    telemetry_.recordFrametime(ms);
}

Canvas StreamMenu::renderStats(const StreamInfo& info) const {
    std::string text;
    if (statsMask_ & StatsFps) {
        if (info.height > 0) text += std::to_string(info.height) + "p  ";
        text += fmt("%.0f fps", info.fps);
    }
    if (statsMask_ & StatsBitrate) {
        if (!text.empty()) text += "  ";
        text += fmt("%.1f Mbps", info.mbps);
    }
    if (statsMask_ & StatsLatency) {
        if (!text.empty()) text += "  ";
        text += info.rttMs > 0 ? std::to_string(info.rttMs) + " ms" : "- ms";
    }
    if (statsMask_ & StatsLoss) {
        if (!text.empty()) text += "  ";
        text += fmt("%.1f%%", info.lossPct);
    }
    if ((statsMask_ & StatsQp) && info.qp >= 0) {
        if (!text.empty()) text += "  ";
        text += "QP " + fmt("%.0f", info.qp);
    }
    if (statsMask_ & StatsDelay) {
        if (info.onScreenMs > 0) {
            if (!text.empty()) text += "  ";
            text += "Screen " + fmt("%.1f ms", info.onScreenMs);
        }
        if (info.decodeMs > 0) {
            if (!text.empty()) text += "  ";
            text += "Dec " + fmt("%.1f ms", info.decodeMs) + (info.isHwDecode ? " HW" : " SW");
        }
    }
    if ((statsMask_ & StatsMic) && !micMuted_) {
        if (!text.empty()) text += "  ";
        text += "MIC";
    }
    constexpr int kPx = 18;
    std::string warn = info.degraded ? std::string("  ") + tr(Str::ConnectionUnstable) : std::string();
    int tw = fonts_.semibold.measure(text, kPx);
    int w = tw + fonts_.semibold.measure(warn, kPx) + 28;
    w = std::max(w, 200);

    bool showGraph = (statsMask_ & StatsGraph) != 0;
    int kH = showGraph ? 62 : 36;
    Canvas c(w, kH);
    c.clear(kPanel);
    if (!text.empty()) fonts_.semibold.draw(c, text, 14, 8, kPx, kWhite);
    if (!warn.empty()) fonts_.semibold.draw(c, warn, 14 + tw, 8, kPx, kWarnColor);

    if (showGraph) {
        telemetry_.drawSparkline(c, 14, 32, w - 28, 22);
    }
    return c;
}

Canvas StreamMenu::renderWarning() const {
    const char* text = tr(Str::ConnectionUnstable);
    constexpr int kPx = 20, kH = 36, kDot = 6;
    int w = fonts_.semibold.measure(text, kPx) + 28 + 2 * kDot + 10;
    Canvas c(w, kH);
    c.clear(kPanel);
    c.fillCircle(14.0f + kDot, kH / 2.0f, static_cast<float>(kDot), kWarnColor);
    fonts_.semibold.draw(c, text, 14 + 2 * kDot + 10, fonts_.semibold.centeredY(0, kH, kPx), kPx, kWhite);
    return c;
}

void StreamMenu::startGyroCalibration(int samples) {
    calibrator_.start(samples);
    calibrating_ = true;
    calSucceeded_ = false;
    calFailed_ = false;
    calProgress_ = 0.0f;
}

bool StreamMenu::feedGyroCalibration(const float rawW[3]) {
    if (!calibrating_) return false;
    bool done = calibrator_.feed(rawW);
    calProgress_ = calibrator_.progress();
    if (calibrator_.state == input::GyroCalibrator::State::Succeeded) {
        calibrating_ = false;
        calSucceeded_ = true;
        calFailed_ = false;
        gyroBias_[0] = calibrator_.resultBias[0];
        gyroBias_[1] = calibrator_.resultBias[1];
        gyroBias_[2] = calibrator_.resultBias[2];
        input::setGlobalGyroCalibration(gyroBias_, gyroYawScale_ / 100.0f, gyroPitchScale_ / 100.0f);
        return true;
    } else if (calibrator_.state == input::GyroCalibrator::State::Failed) {
        calibrating_ = false;
        calSucceeded_ = false;
        calFailed_ = true;
        return false;
    }
    return done;
}

}  // namespace xc::ui
