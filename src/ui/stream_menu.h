// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What is drawn over a running game: the in-game menu (OPTIONS + TOUCHPAD)
// and the statistics line. Each is drawn into its own small canvas, which
// display::setOverlay() lays over the video.
#pragma once

#include <cstdint>
#include "ui/app_ui.h"
#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/pad_icons.h"
#include "input/gyro.h"
#include "ui/components/gyro_wheel_dialog.h"
#include "ui/components/stick_tester_dialog.h"
#include "ui/components/trigger_tester_dialog.h"
#include "ui/components/telemetry_overlay.h"
#include "ui/components/web_control_card.h"

#include <algorithm>
#include <string>
#include <vector>

namespace xc::ui {

// The stream's numbers over the last second.
struct StreamInfo {
    std::string region;
    int rttMs = -1;
    double fps = 0, mbps = 0, lossPct = 0, decodeMs = 0;
    double onScreenMs = 0;  // network arrival to on the TV (GPU path); 0 = unknown
    bool isHwDecode = false;
    int width = 0, height = 0;  // of the decoded picture
    double qp = -1;             // the pictures' average quantiser (lower: finer); -1 unknown
    bool degraded = false;      // frames lost / video stalled / round trip far up, in the last seconds
    int64_t cloudTimeSeconds = -1;  // the account's cloud time left (-1: no limit, or not known)
    uint64_t playtimeSeconds = 0; // total playtime for this title in seconds
    std::vector<float> frametimes;  // presentation frametimes (ms)
};

enum StatsIndicator : uint32_t {
    StatsFps       = 1 << 0,  // "1080p 60 fps"
    StatsBitrate   = 1 << 1,  // "12.5 Mbps"
    StatsLatency   = 1 << 2,  // "18 ms"
    StatsLoss      = 1 << 3,  // "0.0%"
    StatsGraph     = 1 << 4,  // Frametime sparkline histogram
    StatsDelay     = 1 << 5,  // "Screen 24.0 ms  Dec 5.3 ms"
    StatsQp        = 1 << 6,  // "QP 29"
    StatsMic       = 1 << 7,  // "MIC"
    StatsDefaultMask = StatsFps | StatsBitrate | StatsLatency | StatsLoss | StatsGraph,
};

// Close: the menu went away (Circle, OPTIONS, or the Xbox button): a fresh key frame is asked for.
enum class MenuAction {
    None, Close, Leave, Resolution, Stats, Sharpness, Deband, Upscaler, XboxButton,
    ProfileToggle, Triggers, Deadzone, ConfirmButton, MicToggle, Gyro, GyroCalibrate, PadSpeakerVolume,
    TouchpadShortcuts, Player, AudioHaptics, ReloadStream, GuitarStrum,
    VideoDecoder, FpsLimiter, BitrateLimiter, VrrToggle
};

enum class MenuTab {
    General = 0,
    Controls,
    TabCount
};

class StreamMenu {
public:
    static constexpr int kMenuW = 620, kMenuH = 1064;
    static constexpr int kCardW = 540, kCardGap = 40;
    static constexpr int kCombinedW = kMenuW + kCardGap + kCardW;
    static constexpr int kMenuX = 80, kMenuY = (1080 - kMenuH) / 2;
    static constexpr Color kPanel = rgba(24, 24, 24);
    static constexpr int kStatsX = 32, kStatsY = 28;
    // The connection warning, alone (the statistics line off), top right.
    static constexpr int kWarnRight = 1888, kWarnY = 28;

    explicit StreamMenu(const Fonts& fonts) : fonts_(fonts) {}

    // `resolution` as SettingsChoice::resolution (0 = 1080p, 1 = 720p, 2 = 1440p).
    // `sharpness` 0 = off, 1..3 = low, medium, high; `deband` 0..2 = off,
    // low, high.
    // `upscaler` 0 = FSR, 1 = Anime4K.
    // `homeConsole`: the user's own Xbox ("End the stream" instead of "Leave the game").
    // `allow1440`: offered only once a stream delivered it.
    void open(int resolution, bool stats, int sharpness = 0, int deband = 1, int upscaler = 0, bool homeConsole = false,
              bool allow1440 = true, bool hasCustomProfile = false, int triggerStrength = 2, int deadzone = 15,
              bool circleConfirms = false, float micLevel = 0.0f, bool micMuted = false, int padSpeakerVolume = 100);
    void close() { open_ = false; statsCustomizing_ = false; touchpadCustomizing_ = false; gyroDialog_.close(); stickDialog_.close(); triggerDialog_.close(); }
    bool isOpen() const { return open_; }
    MenuAction handle(const NavInput& in);

    MenuTab tab() const { return activeTab_; }
    void setTab(MenuTab t) { activeTab_ = t; selectFirstItem(); }

    int resolution() const { return resolution_; }
    bool statsOn() const { return stats_; }
    void setStatsOn(bool s) { stats_ = s; }
    uint32_t statsMask() const { return statsMask_; }
    void setStatsMask(uint32_t mask) { statsMask_ = mask; }
    bool isCustomizingStats() const { return statsCustomizing_; }
    bool isCustomizingTouchpad() const { return touchpadCustomizing_; }
    const input::TouchpadShortcuts& touchpadShortcuts() const { return touchpadShortcuts_; }
    void setTouchpadShortcuts(const input::TouchpadShortcuts& ts) { touchpadShortcuts_ = ts; }
    void setFrametimes(const std::vector<float>& ft) const { frametimes_ = ft; telemetry_.setFrametimes(ft); }
    int sharpness() const { return sharpness_; }
    int deband() const { return deband_; }
    int upscaler() const { return upscaler_; }
    bool hasCustomProfile() const { return hasCustomProfile_; }
    void setHasCustomProfile(bool custom) { hasCustomProfile_ = custom; }
    bool showQr() const { return showQr_; }
    void setShowQr(bool s) { showQr_ = s; }

    int triggerStrength() const { return triggerStrength_; }
    void setTriggerStrength(int strength) { triggerStrength_ = strength; }
    int triggerHz() const { return triggerHz_; }
    int triggerResistance() const { return triggerResistance_; }
    bool triggerPulses() const { return triggerPulses_; }
    int triggerProfile() const { return triggerProfile_; }
    void setTriggerConfig(int strength, int hz, int resistance, bool pulses, int profile) {
        triggerStrength_ = strength;
        triggerHz_ = hz;
        triggerResistance_ = resistance;
        triggerPulses_ = pulses;
        triggerProfile_ = profile;
    }
    int deadzone() const { return deadzone_; }
    int deadzoneLeft() const { return deadzone_; }
    int deadzoneRight() const { return deadzoneRight_; }
    void setDeadzone(int deadzone) { deadzone_ = deadzoneRight_ = deadzone; }
    void setDeadzones(int left, int right) { deadzone_ = left; deadzoneRight_ = right; }
    void setGameKey(const std::string& key) { gameKey_ = key; }
    void setGameName(const std::string& name) { gyroDialog_.setGameName(name); }
    int audioHaptics() const { return audioHaptics_; }
    void setAudioHaptics(int lvl) { audioHaptics_ = std::clamp(lvl, 0, 3); }
    bool circleConfirms() const { return circleConfirms_; }
    void setCircleConfirms(bool cc) { circleConfirms_ = cc; }
    int padSpeakerVolume() const { return padSpeakerVolume_; }
    void setPadSpeakerVolume(int vol) { padSpeakerVolume_ = vol; }

    int videoDecoder() const { return videoDecoder_; }
    void setVideoDecoder(int d) { videoDecoder_ = d; }
    int fpsLimit() const { return fpsLimit_; }
    void setFpsLimit(int f) { fpsLimit_ = f; }
    int maxBitrateMbps() const { return maxBitrateMbps_; }
    void setMaxBitrateMbps(int b) { maxBitrateMbps_ = b; }
    static std::string formatBitrateLabel(int mbps);
    bool highRefresh() const { return highRefresh_; }
    void setHighRefresh(bool h) { highRefresh_ = h; }

    // Gyro aiming: mode (input::GyroMode) and intensity 1..input::kGyroLevels.
    int gyroMode() const { return gyroMode_; }
    int gyroLevel() const { return gyroLevel_; }
    void setGyro(int mode, int level) { gyroMode_ = mode, gyroLevel_ = level; }
    int gyroYawScale() const { return gyroYawScale_; }
    void setGyroYawScale(int s) { gyroYawScale_ = s; }
    int gyroPitchScale() const { return gyroPitchScale_; }
    void setGyroPitchScale(int s) { gyroPitchScale_ = s; }
    const float* gyroBias() const { return gyroBias_; }
    void setGyroBias(const float b[3]) { if (b) { gyroBias_[0] = b[0]; gyroBias_[1] = b[1]; gyroBias_[2] = b[2]; } }
    void startGyroCalibration(int samples = 240);
    bool feedGyroCalibration(const float rawW[3]);
    bool isGyroCalibrating() const { return calibrating_; }
    float gyroCalProgress() const { return calProgress_; }
    bool gyroCalSucceeded() const { return calSucceeded_; }
    bool gyroCalFailed() const { return calFailed_; }

    // GyroWheelDialog integration
    bool isGyroDialogOpen() const { return gyroDialog_.isOpen(); }
    // The game's response being measured (GyroWheelDialog): asked for with
    // MenuAction::GyroCalibrate; the result goes back into the dialog.
    bool gyroCalibrationRunning() const { return gyroDialog_.calibrationRunning(); }
    // The live test (experimental): the dialog as a side panel, the game keeping L2 and the gyro.
    bool gyroLiveTest() const { return open_ && gyroDialog_.liveTest(); }
    void drawGyroCompact(Canvas& c) { gyroDialog_.drawCompact(c, fonts_); }
    const GyroWheelDialog& gyroDialog() const { return gyroDialog_; }
    void showGyroCalibrationResult(const app::gyrocal::Result& r, bool cancelled) { gyroDialog_.showCalibrationResult(r, cancelled); }
    void openGyroDialog(const std::string& gameKey = "") { gyroDialog_.open(0, gameKey); }
    void closeGyroDialog() { gyroDialog_.close(); }
    void drawGyroDialog(Canvas& c) { gyroDialog_.draw(c, fonts_, 0, 0, c.width(), c.height()); }

    // StickTesterDialog integration
    bool isStickDialogOpen() const { return stickDialog_.isOpen(); }
    void openStickDialog(int deadzoneLeft = 15, int deadzoneRight = 15, const std::string& gameKey = "") {
        stickDialog_.open(deadzoneLeft, deadzoneRight, gameKey);
    }
    void closeStickDialog() { stickDialog_.close(); }
    void drawStickDialog(Canvas& c) const {
        stickDialog_.draw(c, fonts_, 0, 0, c.width(), c.height(), circleConfirms_);
    }
    const StickTesterDialog& stickDialog() const { return stickDialog_; }
    StickTesterDialog& stickDialog() { return stickDialog_; }

    // TriggerTesterDialog integration
    bool isTriggerDialogOpen() const { return triggerDialog_.isOpen(); }
    void openTriggerDialog(int strength = 2, int hz = 1, int resistance = 2, bool pulses = false,
                           int profile = 0, bool adaptive = true, const std::string& gameKey = "") {
        triggerDialog_.open(strength, hz, resistance, pulses, profile, adaptive, gameKey);
    }
    void closeTriggerDialog() { triggerDialog_.close(); }
    void drawTriggerDialog(Canvas& c) const {
        triggerDialog_.draw(c, fonts_, 0, 0, c.width(), c.height(), circleConfirms_);
    }
    const TriggerTesterDialog& triggerDialog() const { return triggerDialog_; }
    TriggerTesterDialog& triggerDialog() { return triggerDialog_; }

    void update(uint64_t nowMs);

    bool micMuted() const { return micMuted_; }
    void setMicMuted(bool muted) { micMuted_ = muted; }
    float micLevel() const { return micLevel_; }
    void setMicLevel(float lvl) { micLevel_ = lvl; }
    // The block smoothing level "auto" picked for the bitrate (shown as "Auto (low)").
    void setDebandInUse(int level) { debandInUse_ = level; }
    // Circle confirms: the hints swap their buttons.
    // Update active profile values without resetting cursor position.
    void setProfileValues(int resolution, int sharpness, int deband, int upscaler,
                          int triggerStrength, int deadzone, bool circleConfirms,
                          bool hasCustomProfile);
    // The controllers in use, under the connection.
    void setPads(const PadSlots& pads) { pads_ = pads; }
    // Opened by a USB controller (input/pad_usb.h): the player it plays as
    // (0..3), which its Player item changes (MenuAction::Player); -1 when
    // the menu is player 1's own (no such item).
    void setUsbOwner(int player) { usbOwnerPlayer_ = player; }
    // The menu was opened by a USB guitar: its strum option is offered.
    void setUsbOwnerGuitar(bool guitar, bool strum) { usbOwnerGuitar_ = guitar, guitarStrum_ = strum; }
    bool guitarStrum() const { return guitarStrum_; }
    int usbOwnerPlayer() const { return usbOwnerPlayer_; }
    // What the display and the pads can do (UiCaps, all by default as on
    // the PS5): the upscaler, sharpness and block smoothing need the
    // display's; trigger vibration and gyro aiming a pad with them; the
    // footer names the buttons when no pad has a touchpad.
    void setCapabilities(const UiCaps& caps);
    const UiCaps& capabilities() const { return caps_; }

    Canvas renderMenu(const StreamInfo& info) const;
    Canvas renderStats(const StreamInfo& info) const;
    void recordFrametime(float ms) const;
    // "Unstable connection", on its own (when the statistics line is off).
    Canvas renderWarning() const;

    enum Item {
        // Tab General
        XboxButton = 0,
        Stats,
        Upscaler,
        Sharpness,
        Deband,
        Resolution,
        VideoDecoderItem,
        FpsLimiterItem,
        BitrateLimiterItem,
        VrrItem,
        ReloadStreamItem,
        Leave,

        // Tab Controls
        PlayerItem,
        StrumItem,
        ConfirmItem,
        Triggers,
        DeadzoneItem,
        TouchpadShortcutsItem,
        GyroItem,
        AudioHapticsItem,
        PadSpeakerVolumeItem,

        ItemCount,
        Profile = ItemCount
    };

    const char* itemDescription(int item) const;

private:
    bool itemInTab(int item, MenuTab tab) const;
    bool itemShown(int item) const;
    void selectFirstItem();

    const Fonts& fonts_;
    UiCaps caps_;
    bool open_ = false, stats_ = false;
    uint32_t statsMask_ = StatsDefaultMask;
    bool statsCustomizing_ = false;
    int statsCustomIndex_ = 0;
    bool touchpadCustomizing_ = false;
    int touchpadCustomIndex_ = 0;
    input::TouchpadShortcuts touchpadShortcuts_ = input::defaultTouchpadShortcuts();

    MenuTab activeTab_ = MenuTab::General;
    int selected_ = XboxButton;
    int resolution_ = 0, applied_ = 0;
    int videoDecoder_ = 0;
    int fpsLimit_ = 0;
    int maxBitrateMbps_ = 0;
    bool highRefresh_ = false;
    bool showQr_ = true;
    int sharpness_ = 0, deband_ = 1, upscaler_ = 0, debandInUse_ = 1;
    bool resolutionAsked_ = false;
    bool circleConfirms_ = false;
    bool homeConsole_ = false, allow1440_ = true;
    bool hasCustomProfile_ = false;
    int triggerStrength_ = 2;
    int triggerHz_ = 1;
    int triggerResistance_ = 2;
    bool triggerPulses_ = false;
    int triggerProfile_ = 0;
    int deadzone_ = 15;
    int deadzoneRight_ = 15;
    std::string gameKey_;
    int gyroMode_ = 0, gyroLevel_ = 5;
    int gyroYawScale_ = 100, gyroPitchScale_ = 80;
    float gyroBias_[3] = {0.0f, 0.0f, 0.0f};
    bool calibrating_ = false;
    bool calSucceeded_ = false;
    bool calFailed_ = false;
    float calProgress_ = 0.0f;
    input::GyroCalibrator calibrator_{};
    bool micMuted_ = false;
    float micLevel_ = 0.0f;
    int padSpeakerVolume_ = 100;
    PadSlots pads_{};
    int usbOwnerPlayer_ = -1;
    bool usbOwnerGuitar_ = false, guitarStrum_ = false;
    int audioHaptics_ = 0;
    mutable std::vector<float> frametimes_;

    GyroWheelDialog gyroDialog_;
    StickTesterDialog stickDialog_;
    TriggerTesterDialog triggerDialog_;
    mutable TelemetryOverlay telemetry_;
};

}  // namespace xc::ui
