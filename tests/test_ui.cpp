// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "test_harness.h"

#include "app/auto_deband.h"
#include "app/crash_guard.h"
#include "app/gyro_calibration.h"
#include "app/library.h"
#include "app/report.h"
#include "app/settings.h"
#include "app/settings_manager.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "app/web_server.h"
#include "auth/account_seal.h"
#include "auth/auth_manager.h"
#include "auth/msa.h"
#include "auth/xbox_live.h"
#include "display/display.h"
#include "display/gpu.h"
#include "input/controller.h"
#include "input/gyro.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "input/pad_map.h"
#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include "input/sdl_mapping.h"
#include "input/tuning.h"
#include "input/usb_xray.h"
#include "media/audio_encoder.h"
#include "media/audio_in.h"
#include "media/audio_in_common.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "media/decoder.h"
#include "media/hw_decoder.h"
#include "net/http.h"
#include "net/socket.h"
#include "platform/handheld.h"
#include "platform/platform.h"
#include "platform/toasts.h"
#include "stream/h264_receiver.h"
#include "stream/input_packet.h"
#include "stream/stream_session.h"
#include "trophy/trophy_backend.h"
#include "trophy/trophy_registry.h"
#include "trophy/trophy_sync.h"
#include "ui/accent_color.h"
#include "ui/app_ui.h"
#include "ui/app_ui_internal.h"
#include "ui/cloud_time.h"
#include "ui/image_cache.h"
#include "ui/input_mode.h"
#include "ui/pad_glyphs.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "ui/touch_pad_overlay.h"
#include "util/fs.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/achievements.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/prices.h"
#include "xcloud/purchase.h"
#include "xcloud/regions.h"
#include "xcloud/titlehub.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace xc;

static void testStrings() {
    using namespace ui;
    auto placeholders = [](const std::string& t) {
        int n = 0;
        for (size_t at = t.find("%s"); at != std::string::npos; at = t.find("%s", at + 2)) ++n;
        return n;
    };
    for (int l = 0; l < static_cast<int>(Language::Count); ++l) {
        setLanguage(Language::English);
        std::vector<std::string> english;
        for (int i = 0; i < static_cast<int>(Str::Count); ++i) english.push_back(tr(static_cast<Str>(i)));
        setLanguage(static_cast<Language>(l));
        CHECK(languageFromCode(languageCode(static_cast<Language>(l))) == static_cast<Language>(l));
        for (int i = 0; i < static_cast<int>(Str::Count); ++i) {
            std::string t = tr(static_cast<Str>(i));
            CHECK(!t.empty());
            CHECK(placeholders(t) == placeholders(english[static_cast<size_t>(i)]));
        }
    }
    setLanguage(Language::English);
    CHECK(languageFromCode("xx") == Language::English);
    CHECK(prettyRegion("SOUTHCENTRALUS") == "South Central US");
    CHECK(prettyRegion("EASTUS2") == "East US 2");
    CHECK(prettyRegion("BRAZILSOUTH") == "Brazil South");
    CHECK(prettyRegion("CHILECENTRAL") == "Chile Central");
    CHECK(prettyRegion("MEXICOCENTRAL") == "Mexico Central");
    CHECK(prettyRegion("SWEDENCENTRAL") == "Sweden Central");
    CHECK(prettyRegion("UAENORTH") == "UAE North");
}

static void testAccentColor() {
    using namespace xc::ui;
    Image img;
    img.w = img.h = 64;
    img.px.assign(64 * 64, rgba(60, 60, 60));                            // grey background
    for (int i = 0; i < 64 * 24; ++i) img.px[i] = rgba(200, 40, 30);    // a red third
    for (int i = 64 * 60; i < 64 * 64; ++i) img.px[i] = rgba(30, 60, 200);  // a little blue
    Color c = 0;
    CHECK(dominantColor(img, c));
    CHECK((c & 0xFF) == 255 && ((c >> 8) & 0xFF) < 90 && ((c >> 16) & 0xFF) < 90);  // red, at full
    Image grey;
    grey.w = grey.h = 16;
    grey.px.assign(16 * 16, rgba(128, 128, 128));
    CHECK(!dominantColor(grey, c));
}

static void testCloudTimeClock() {
    using xc::ui::CloudTimeClock;
    using xc::ui::formatCloudTime;
    // Whole minutes, rounded down.
    CHECK(formatCloudTime(15000) == "4 h 10 min");
    CHECK(formatCloudTime(54000) == "15 h");
    CHECK(formatCloudTime(2879) == "47 min");
    CHECK(formatCloudTime(59) == "< 1 min");
    CHECK(formatCloudTime(0) == "0 min");
    // After a session: an answer the service has not counted yet keeps the
    // count; one over the earlier answer is new hours; none keeps the count.
    CHECK((CloudTimeClock::reconcile(3000, 6600, 6600) == 3000));
    CHECK((CloudTimeClock::reconcile(3000, 2000, 6600) == 2000));
    CHECK((CloudTimeClock::reconcile(3000, 54000, 6600) == 54000));
    CHECK((CloudTimeClock::reconcile(3000, -1, 6600) == 3000));
    CHECK((CloudTimeClock::reconcile(-1, 900, -1) == 900));

    CloudTimeClock c;
    c.start(-1, 0);
    CHECK(!c.limited());
    CHECK(c.left(60000) == -1);
    CHECK(c.noticeDue(60000) == -1);

    c.start(16 * 60, 0);  // 16 min left
    CHECK(c.left(0) == 16 * 60);
    CHECK(c.noticeDue(0) == -1);
    CHECK(c.left(30500) == 16 * 60 - 30);
    CHECK(c.noticeDue(60000) == 15 * 60);  // 15 min: the first notice, once
    CHECK(c.noticeDue(61000) == -1);
    c.answer(16 * 60, 120000);  // stale: the service had not counted the stream
    CHECK(c.left(120000) == 14 * 60);
    c.answer(10 * 60, 120000);  // lower: taken
    CHECK(c.left(120000) == 10 * 60);
    CHECK(c.noticeDue(420000) == 5 * 60);  // 5 min
    CHECK(c.noticeDue(500000) == -1);
    CHECK(c.left(10000000) == 0);
    c.answer(15 * 3600, 10000000);  // hours bought: over the start's answer
    CHECK(c.left(10000000) == 15 * 3600);

    c.start(10 * 60, 0);  // a stream that starts under 15 minutes: one notice now
    CHECK(c.noticeDue(0) == 10 * 60);
    CHECK(c.noticeDue(1000) == -1);
    CHECK(c.noticeDue(300000) == 5 * 60);
    c.start(4 * 60, 0);  // under 5: just the one
    CHECK(c.noticeDue(0) == 4 * 60);
    CHECK(c.noticeDue(60000) == -1);
    c.start(5 * 60 + 10, 0);  // "5 min" already shown: not said twice
    CHECK(c.noticeDue(0) == 5 * 60 + 10);
    CHECK(c.noticeDue(10000) == -1);
    c.start(16 * 60 + 30, 0);  // the notice when "15 min" first shows
    CHECK(c.noticeDue(30000) == -1);
    CHECK(c.noticeDue(31000) == 15 * 60 + 59);
}

static void testFontsFallBackToBuiltin() {
    ui::Fonts missing;
    CHECK(missing.load("/nonexistent/psbox/fonts"));
    CHECK(missing.usedBuiltin);
    CHECK(missing.regular.loaded() && missing.semibold.loaded() && missing.bold.loaded());
    CHECK(missing.regular.lineHeight(24) > 0);

    std::string dir = "/tmp/xcloud-test-fonts-" + std::to_string(::getpid());
    xc::fs::removeTree(dir);
    xc::fs::makeDirs(dir);
    std::string whole;
    CHECK(platform::readFile("assets/fonts/Inter-Regular.ttf", whole) || platform::readFile("../assets/fonts/Inter-Regular.ttf", whole));
    CHECK(platform::writeFileAtomic(dir + "/Inter-Regular.ttf", whole.substr(0, whole.size() / 2)));  // cut short
    CHECK(platform::writeFileAtomic(dir + "/Inter-SemiBold.ttf", std::string(4096, 'x')));  // not a font
    CHECK(platform::writeFileAtomic(dir + "/Inter-Bold.ttf", whole));  // whole: the file is used
    ui::Fonts damaged;
    CHECK(damaged.load(dir));
    CHECK(damaged.usedBuiltin && damaged.regular.loaded() && damaged.semibold.loaded() && damaged.bold.loaded());

    ui::Fonts whole3;
    std::string assets = platform::readFile("assets/fonts/Inter-Bold.ttf", whole) ? "assets/fonts" : "../assets/fonts";
    CHECK(whole3.load(assets) && !whole3.usedBuiltin);
    xc::fs::removeTree(dir);
}

static void testSplashInputBlocking() {
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    CHECK(app.screen() == ui::Screen::Splash);

    ui::NavInput in;
    in.accept = true;
    in.down = true;
    in.options = true;
    in.triangle = true;
    in.r1 = true;
    ui::UiEvent ev = app.handle(in);
    CHECK(ev.action == ui::Action::None);
    CHECK(app.screen() == ui::Screen::Splash);

    app.showSplash("Signing in...");
    CHECK(app.screen() == ui::Screen::Splash);
    ev = app.handle(in);
    CHECK(ev.action == ui::Action::None);

    app.showHome();
    CHECK(app.screen() == ui::Screen::Home);
}

static void testStreamMenu() {
    using xc::ui::MenuAction;
    xc::ui::Fonts fonts;  // not loaded: handle() never draws
    xc::ui::StreamMenu menu(fonts);
    auto press = [&](void (*set)(xc::ui::NavInput&)) {
        xc::ui::NavInput n;
        set(n);
        return menu.handle(n);
    };
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::None);  // closed
    menu.open(0, false);
    CHECK(menu.isOpen());
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::XboxButton);  // first; it closes
    CHECK(!menu.isOpen());
    menu.open(0, false);
    press([](xc::ui::NavInput& n) { n.down = true; }); // Stats (Profile was removed)
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Stats);
    CHECK(menu.statsOn());
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Upscaler);  // FSR -> FSR + clean-up
    CHECK(menu.upscaler() == 2);
    press([](xc::ui::NavInput& n) { n.right = true; });  // -> AI
    CHECK(menu.upscaler() == 1);
    press([](xc::ui::NavInput& n) { n.right = true; });  // -> AI + clean-up
    CHECK(menu.upscaler() == 3);
    press([](xc::ui::NavInput& n) { n.right = true; });  // wraps to FSR
    CHECK(menu.upscaler() == 0);
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.left = true; }) == MenuAction::Sharpness);  // off -> high
    CHECK(menu.sharpness() == 3);
    press([](xc::ui::NavInput& n) { n.down = true; });
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Deband);  // low -> high
    CHECK(menu.deband() == 2);
    press([](xc::ui::NavInput& n) { n.right = true; });  // -> auto
    CHECK(menu.deband() == 3);
    press([](xc::ui::NavInput& n) { n.down = true; });
    press([](xc::ui::NavInput& n) { n.left = true; });  // 1080p -> 720p
    CHECK(menu.resolution() == 1);
    press([](xc::ui::NavInput& n) { n.left = true; });  // wraps to 1440p
    CHECK(menu.resolution() == 2);
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Resolution);
    press([](xc::ui::NavInput& n) { n.down = true; }); // VideoDecoderItem
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::VideoDecoder);
    CHECK(menu.videoDecoder() == 1);
    press([](xc::ui::NavInput& n) { n.down = true; }); // FpsLimiterItem
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::FpsLimiter);
    CHECK(menu.fpsLimit() == 30);
    press([](xc::ui::NavInput& n) { n.down = true; }); // BitrateLimiterItem
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::BitrateLimiter);
    press([](xc::ui::NavInput& n) { n.down = true; }); // VrrItem
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::VrrToggle);
    CHECK(menu.highRefresh());
    press([](xc::ui::NavInput& n) { n.down = true; });  // Reload Stream
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::ReloadStream);
    CHECK(!menu.isOpen());

    // Test Controls Tab (reordered: Confirm -> Triggers -> Deadzone -> TouchpadShortcuts -> Gyro -> AudioHaptics -> PadSpeakerVolume)
    menu.open(0, false);
    press([](xc::ui::NavInput& n) { n.r1 = true; }); // Switch to Controls Tab
    CHECK(menu.tab() == xc::ui::MenuTab::Controls);
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::ConfirmButton);
    CHECK(menu.circleConfirms());
    press([](xc::ui::NavInput& n) { n.down = true; }); // Triggers
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Triggers);
    press([](xc::ui::NavInput& n) { n.down = true; }); // Deadzone
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Deadzone);
    press([](xc::ui::NavInput& n) { n.down = true; }); // TouchpadShortcuts
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::TouchpadShortcuts);
    CHECK(press([](xc::ui::NavInput& n) { n.back = true; }) == MenuAction::TouchpadShortcuts);
    press([](xc::ui::NavInput& n) { n.down = true; }); // GyroItem (ATIVADO / DESATIVADO toggle)
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::Gyro && menu.gyroMode() == 1);
    CHECK(press([](xc::ui::NavInput& n) { n.left = true; }) == MenuAction::Gyro && menu.gyroMode() == 0);
    // Confirm on GyroItem opens single-wheel modal
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::None);
    CHECK(menu.isGyroDialogOpen());
    menu.setGyroYawScale(110);
    CHECK(menu.gyroYawScale() == 110);
    menu.setGyroPitchScale(70);
    CHECK(menu.gyroPitchScale() == 70);
    menu.startGyroCalibration(60);
    CHECK(menu.isGyroCalibrating());
    menu.closeGyroDialog();
    CHECK(!menu.isGyroDialogOpen());
    press([](xc::ui::NavInput& n) { n.down = true; }); // AudioHapticsItem
    CHECK(press([](xc::ui::NavInput& n) { n.right = true; }) == MenuAction::AudioHaptics && menu.audioHaptics() == 1);
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::AudioHaptics && menu.audioHaptics() == 2);
    press([](xc::ui::NavInput& n) { n.down = true; }); // PadSpeakerVolume
    CHECK(press([](xc::ui::NavInput& n) { n.left = true; }) == MenuAction::PadSpeakerVolume && menu.padSpeakerVolume() == 95);

    menu.open(0, false);
    press([](xc::ui::NavInput& n) { n.up = true; });  // "Leave game", last (wraps from top)
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Leave);
    CHECK(!menu.isOpen());
    menu.open(1, true);
    CHECK(press([](xc::ui::NavInput& n) { n.back = true; }) == MenuAction::Close);
    // Best (Auto) offered as resolution 2; cycling is 720p (1) -> 1080p (0) -> Best (2).
    menu.open(2, false, 0, 1, 0, false, false);
    CHECK(menu.resolution() == 2);
    for (int i = 0; i < 5; ++i) press([](xc::ui::NavInput& n) { n.down = true; });  // to the resolution (Xbox -> Stats -> Upscaler -> Sharpness -> Deband -> Resolution)
    press([](xc::ui::NavInput& n) { n.right = true; });
    CHECK(menu.resolution() == 1);  // Best (2) -> wraps to 720p (1)
    press([](xc::ui::NavInput& n) { n.right = true; });
    CHECK(menu.resolution() == 0);  // 720p -> 1080p
    press([](xc::ui::NavInput& n) { n.right = true; });
    CHECK(menu.resolution() == 2);  // 1080p -> Best (2)

    // Test stats indicator customization
    menu.open(0, true);
    press([](xc::ui::NavInput& n) { n.down = true; });  // -> Stats
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Stats);
    CHECK(menu.isCustomizingStats());
    // Indicator 0 (StatsFps) is on by default in StatsDefaultMask; toggle it off:
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Stats);
    CHECK((menu.statsMask() & xc::ui::StatsFps) == 0);
    // Toggle it back on:
    CHECK(press([](xc::ui::NavInput& n) { n.accept = true; }) == MenuAction::Stats);
    CHECK((menu.statsMask() & xc::ui::StatsFps) != 0);
    // Back returns to main menu:
    press([](xc::ui::NavInput& n) { n.back = true; });
    CHECK(!menu.isCustomizingStats());
    CHECK(menu.isOpen());
}

static void testSmartGlassInGameMenuInvariants() {
    using xc::ui::MenuAction;
    xc::ui::Fonts fonts;
    fonts.load("assets/fonts");
    xc::ui::StreamMenu menu(fonts);

    // =========================================================================
    // 1. Permanent QR card geometry and dynamic layout collapse invariants
    // =========================================================================
    CHECK(!menu.isOpen());

    menu.open(0, false);
    CHECK(menu.isOpen());

    xc::ui::StreamInfo sinfo;
    xc::ui::Canvas cOpen = menu.renderMenu(sinfo);
    CHECK(cOpen.width() == xc::ui::StreamMenu::kCombinedW); // 1200
    CHECK(cOpen.height() == xc::ui::StreamMenu::kMenuH); // 1064
    CHECK(xc::ui::StreamMenu::kCombinedW == xc::ui::StreamMenu::kMenuW + xc::ui::StreamMenu::kCardGap + xc::ui::StreamMenu::kCardW);
    CHECK(xc::ui::StreamMenu::kMenuW == 620);
    CHECK(xc::ui::StreamMenu::kCardGap == 40);
    CHECK(xc::ui::StreamMenu::kCardW == 540);
    CHECK(xc::ui::StreamMenu::kCombinedW == 1200);

    // Navigation in General tab: wrap around XboxButton -> Leave -> ReloadStreamItem -> VrrItem
    xc::ui::NavInput navUp, navDown, navLeft, navRight, navAccept, navBack, navR1, navL1;
    navUp.up = true;
    navDown.down = true;
    navLeft.left = true;
    navRight.right = true;
    navAccept.accept = true;
    navBack.back = true;
    navR1.r1 = true;
    navL1.l1 = true;

    menu.handle(navUp); // Up from XboxButton wraps to Leave
    menu.handle(navUp); // Up from Leave moves to ReloadStreamItem
    menu.handle(navUp); // Up from ReloadStreamItem moves to VrrItem (no SmartGlassItem)
    CHECK(menu.handle(navRight) == MenuAction::VrrToggle);

    // Customization collapse invariants: stats customization collapses width to 620
    menu.open(0, true);
    CHECK(menu.renderMenu(sinfo).width() == xc::ui::StreamMenu::kCombinedW);
    menu.handle(navDown); // Stats (Profile was removed from General tab)
    CHECK(menu.handle(navAccept) == MenuAction::Stats);
    CHECK(menu.isCustomizingStats());
    CHECK(menu.renderMenu(sinfo).width() == xc::ui::StreamMenu::kMenuW); // 620
    menu.handle(navBack); // Back returns to main menu
    CHECK(!menu.isCustomizingStats());
    CHECK(menu.renderMenu(sinfo).width() == xc::ui::StreamMenu::kCombinedW); // 1200

    // Touchpad shortcuts customization collapses width to 620
    menu.open(0, false);
    menu.handle(navR1); // Switch to Controls tab
    for (int i = 0; i < 3; ++i) menu.handle(navDown); // ConfirmItem -> Triggers -> DeadzoneItem -> TouchpadShortcutsItem
    CHECK(menu.handle(navAccept) == MenuAction::TouchpadShortcuts);
    CHECK(menu.isCustomizingTouchpad());
    CHECK(menu.renderMenu(sinfo).width() == xc::ui::StreamMenu::kMenuW); // 620
    menu.handle(navBack); // Back returns to main menu
    CHECK(!menu.isCustomizingTouchpad());
    CHECK(menu.renderMenu(sinfo).width() == xc::ui::StreamMenu::kCombinedW); // 1200

    // =========================================================================
    // 2. Submenus (StickTesterDialog, TriggerTesterDialog, AudioHaptics, Jitter)
    // =========================================================================
    // StickTesterDialog integration and deadzone sync
    menu.open(0, false);
    menu.handle(navR1); // Controls tab
    menu.setDeadzone(15);
    CHECK(!menu.isStickDialogOpen());
    // Navigate to DeadzoneItem: ConfirmItem -> Triggers -> DeadzoneItem
    menu.handle(navDown); // Triggers
    menu.handle(navDown); // DeadzoneItem
    CHECK(menu.handle(navRight) == MenuAction::Deadzone && menu.deadzone() == 20);
    CHECK(menu.handle(navAccept) == MenuAction::None);
    CHECK(menu.isStickDialogOpen());
    // Draw stick dialog into a test canvas
    xc::ui::Canvas stickCanvas(1920, 1080);
    menu.drawStickDialog(stickCanvas);
    CHECK(stickCanvas.data()[540 * 1920 + 960] != 0); // dialog rendered non-empty
    // Input forwarding while stick dialog is open
    xc::ui::NavInput stickIn;
    stickIn.dpadLeft = true;
    menu.handle(stickIn); // adjusts deadzone inside dialog
    stickIn = {};
    stickIn.back = true;
    CHECK(menu.handle(stickIn) == MenuAction::Deadzone);
    CHECK(!menu.isStickDialogOpen());

    // TriggerTesterDialog integration and trigger feel sync
    menu.open(0, false);
    menu.handle(navR1); // Controls tab
    menu.setTriggerStrength(2);
    CHECK(!menu.isTriggerDialogOpen());
    menu.handle(navDown); // -> Triggers
    CHECK(menu.handle(navRight) == MenuAction::Triggers && menu.triggerStrength() == 3);
    CHECK(menu.handle(navAccept) == MenuAction::None);
    CHECK(menu.isTriggerDialogOpen());
    // Draw trigger dialog into a test canvas
    xc::ui::Canvas trigCanvas(1920, 1080);
    menu.drawTriggerDialog(trigCanvas);
    CHECK(trigCanvas.data()[540 * 1920 + 960] != 0); // dialog rendered non-empty
    // Input forwarding while trigger dialog is open
    xc::ui::NavInput trigIn;
    trigIn.down = true;
    menu.handle(trigIn); // navigates rows
    trigIn = {};
    trigIn.back = true;
    CHECK(menu.handle(trigIn) == MenuAction::Triggers);
    CHECK(!menu.isTriggerDialogOpen());

    // Audio to Haptics in Controls Tab (0..3 cycling and actions)
    menu.open(0, false);
    menu.handle(navR1); // Controls tab
    menu.setAudioHaptics(0);
    CHECK(menu.audioHaptics() == 0);
    for (int i = 0; i < 5; ++i) menu.handle(navDown); // ConfirmItem -> Triggers -> DeadzoneItem -> TouchpadShortcutsItem -> GyroItem -> AudioHapticsItem
    CHECK(menu.handle(navRight) == MenuAction::AudioHaptics && menu.audioHaptics() == 1);
    CHECK(menu.handle(navRight) == MenuAction::AudioHaptics && menu.audioHaptics() == 2);
    CHECK(menu.handle(navRight) == MenuAction::AudioHaptics && menu.audioHaptics() == 3);
    CHECK(menu.handle(navRight) == MenuAction::AudioHaptics && menu.audioHaptics() == 0);
    CHECK(menu.handle(navLeft) == MenuAction::AudioHaptics && menu.audioHaptics() == 3);
    CHECK(menu.handle(navAccept) == MenuAction::AudioHaptics && menu.audioHaptics() == 0);
    menu.setAudioHaptics(2);
    CHECK(menu.audioHaptics() == 2);

    // TelemetryOverlay real-time jitter sparkline integration
    std::vector<float> jitterValues = {3.5f, 10.2f, 16.0f, 22.4f, 31.8f, 38.0f, 12.0f};
    menu.setFrametimes(jitterValues);
    xc::ui::TelemetryOverlay telemetry;
    telemetry.setFrametimes(jitterValues);
    xc::ui::Canvas sparkCanvas(400, 100);
    sparkCanvas.clear(xc::ui::rgba(0, 0, 0, 0));
    telemetry.drawSparkline(sparkCanvas, 10, 10, 200, 40);
    bool sparklineDrawn = false;
    for (int y = 10; y < 50; ++y) {
        for (int x = 10; x < 210; ++x) {
            if (sparkCanvas.data()[y * 400 + x] != 0) {
                sparklineDrawn = true;
                break;
            }
        }
        if (sparklineDrawn) break;
    }
    CHECK(sparklineDrawn);

    // Dynamic sub-5ms scaling: small jitter fluctuations visibly vary bar heights
    std::vector<float> lowJitter = {0.4f, 0.8f, 1.5f, 3.0f, 0.5f};
    telemetry.setFrametimes(lowJitter);
    xc::ui::Canvas lowCanvas(220, 60);
    lowCanvas.clear(0);
    telemetry.drawSparkline(lowCanvas, 10, 10, 172, 22);
    int minBarH = 100, maxBarH = 0;
    const uint32_t barGreen = xc::ui::rgba(40, 210, 80);
    for (int col = 10; col < 182; ++col) {
        int colH = 0;
        for (int row = 10; row < 32; ++row) {
            if (lowCanvas.data()[row * 220 + col] == barGreen) ++colH;
        }
        if (colH > 0) {
            minBarH = std::min(minBarH, colH);
            maxBarH = std::max(maxBarH, colH);
        }
    }
    CHECK(maxBarH - minBarH >= 8);

    // StickTesterDialog: sticks never alter deadzone; D-pad L/R adjusts per-stick
    xc::ui::StickTesterDialog stickDlg;
    stickDlg.open(15, 15, "testgame");
    xc::ui::NavInput stickTestIn;
    stickTestIn.left = true;
    stickTestIn.rawLX = -1.0f;
    stickDlg.handleInput(stickTestIn);
    CHECK(stickDlg.deadzoneLeft() == 15);
    stickTestIn = {};
    stickTestIn.dpadRight = true;
    stickDlg.handleInput(stickTestIn);
    CHECK(stickDlg.deadzoneLeft() == 16);
    stickTestIn = {};
    stickTestIn.r1 = true;
    stickDlg.handleInput(stickTestIn);
    CHECK(stickDlg.activeStick() == 1);
    stickTestIn = {};
    stickTestIn.dpadLeft = true;
    stickDlg.handleInput(stickTestIn);
    CHECK(stickDlg.deadzoneRight() == 14);
    stickDlg.close();

    // Asymmetric deadzones & full trigger configuration preservation across open() + dialogs
    menu.open(0, false);
    menu.setDeadzones(10, 22);
    menu.setTriggerConfig(3, 2, 1, true, 1);
    menu.handle(navR1); // Controls tab
    menu.handle(navDown); // Triggers
    CHECK(menu.handle(navAccept) == MenuAction::None);
    CHECK(menu.isTriggerDialogOpen());
    CHECK(menu.triggerDialog().strength() == 3);
    CHECK(menu.triggerDialog().hz() == 2);
    CHECK(menu.triggerDialog().resistance() == 1);
    CHECK(menu.triggerDialog().pulses());
    CHECK(menu.triggerDialog().profile() == 1);
    menu.handle(navBack);
    CHECK(!menu.isTriggerDialogOpen());
    menu.handle(navDown); // DeadzoneItem (immediately below Triggers in new order)
    CHECK(menu.handle(navAccept) == MenuAction::None);
    CHECK(menu.isStickDialogOpen());
    CHECK(menu.stickDialog().deadzoneLeft() == 10);
    CHECK(menu.stickDialog().deadzoneRight() == 22);
    menu.handle(navBack);
    CHECK(!menu.isStickDialogOpen());
    CHECK(menu.deadzoneLeft() == 10);
    CHECK(menu.deadzoneRight() == 22);

    // Submenu pill accent border renders cleanly on focused submenu items
    xc::ui::Canvas cCtrl = menu.renderMenu(sinfo);
    bool pillGreenFound = false;
    const uint32_t pillGreen = xc::ui::rgba(22, 163, 74);
    for (int py = 100; py < 450 && !pillGreenFound; ++py) {
        for (int pxCol = 530; pxCol < 605; ++pxCol) {
            if (cCtrl.data()[py * cCtrl.width() + pxCol] == pillGreen) {
                pillGreenFound = true;
                break;
            }
        }
    }
    CHECK(pillGreenFound);


    // =========================================================================
    // 3. Transparency invariants across the gap [620, 660) and surrounding regions
    // =========================================================================
    const uint32_t* px = cOpen.data();
    const int w = cOpen.width();
    const int h = cOpen.height();

    // Verify 100% of gap pixels (x in [620, 660)) have alpha == 0 and p == 0u
    size_t gapViolationCount = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = xc::ui::StreamMenu::kMenuW; x < xc::ui::StreamMenu::kMenuW + xc::ui::StreamMenu::kCardGap; ++x) {
            uint32_t p = px[y * w + x];
            if (xc::ui::alphaOf(p) != 0 || p != 0u) {
                gapViolationCount++;
            }
        }
    }
    CHECK(gapViolationCount == 0);

    // Verify above-card region in right quadrant (x in [660, 1200), y in [0, 204)) is transparent
    size_t topQuadrantViolations = 0;
    for (int y = 0; y < 204; ++y) {
        for (int x = xc::ui::StreamMenu::kMenuW + xc::ui::StreamMenu::kCardGap; x < w; ++x) {
            uint32_t p = px[y * w + x];
            if (xc::ui::alphaOf(p) != 0 || p != 0u) {
                topQuadrantViolations++;
            }
        }
    }
    CHECK(topQuadrantViolations == 0);

    // Verify below-card region in right quadrant (x in [660, 1200), y in [844, 1064)) is transparent
    size_t bottomQuadrantViolations = 0;
    for (int y = 844; y < h; ++y) {
        for (int x = xc::ui::StreamMenu::kMenuW + xc::ui::StreamMenu::kCardGap; x < w; ++x) {
            uint32_t p = px[y * w + x];
            if (xc::ui::alphaOf(p) != 0 || p != 0u) {
                bottomQuadrantViolations++;
            }
        }
    }
    CHECK(bottomQuadrantViolations == 0);

    // Verify left panel is actually rendered (not blank)
    CHECK(xc::ui::alphaOf(px[500 * w + 300]) == 255);

    // Verify side card is actually rendered (not blank)
    int cardCenterX = 660 + 270;
    int cardCenterY = 204 + 320;
    CHECK(xc::ui::alphaOf(px[cardCenterY * w + cardCenterX]) > 0);

    // =========================================================================
    // 4. QR code generation, readability & module validation
    // =========================================================================
    std::vector<std::string> testUrls = {
        "http://192.168.15.17:8080/gamepad",
        "http://127.0.0.1:8080/gamepad",
        "http://10.0.0.1:8080/gamepad",
        "http://172.16.42.1:8080/gamepad",
        "http://" + xc::platform::localIPv4() + ":8080/gamepad",
        "http://psbox.local:8080/gamepad",
        "http://192.168.100.254:8080/gamepad?player=1&layout=custom_studio_v1"
    };

    xc::ui::Canvas testCanvas(1200, 800);
    for (const auto& url : testUrls) {
        testCanvas.clear(0);
        xc::ui::Rect box = xc::ui::drawQr(testCanvas, url, 1000, 50, 300, qrcodegen_Ecc_LOW);
        CHECK(box.w > 0 && box.h > 0);
        CHECK(box.w == box.h);
        CHECK(box.x + box.w == 1000);

        // Verify independent QR encoder output matches canvas
        uint8_t qrBuf[qrcodegen_BUFFER_LEN_MAX], tmpBuf[qrcodegen_BUFFER_LEN_MAX];
        bool ok = qrcodegen_encodeText(url.c_str(), tmpBuf, qrBuf, qrcodegen_Ecc_LOW,
                                       qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                       qrcodegen_Mask_AUTO, true);
        CHECK(ok);
        int n = qrcodegen_getSize(qrBuf);
        CHECK(n >= 21);
        int mod = 300 / (n + 2);
        CHECK(mod > 0);

        // Check top-left finder pattern corner module at (0, 0)
        CHECK(qrcodegen_getModule(qrBuf, 0, 0));
        int ox = box.x + 20 + mod;
        int oy = box.y + 20 + mod;
        uint32_t modulePixel = testCanvas.data()[(oy + mod / 2) * testCanvas.width() + (ox + mod / 2)];
        CHECK(xc::ui::alphaOf(modulePixel) == 255);
        // Finder pattern module is drawn with kBg = rgba(14, 14, 14)
        CHECK(modulePixel == xc::ui::rgba(14, 14, 14));

        // Check quiet zone around the QR code (at ox - mod/2, oy - mod/2) is white
        uint32_t quietPixel = testCanvas.data()[(oy - mod / 2) * testCanvas.width() + (ox - mod / 2)];
        CHECK(quietPixel == xc::ui::rgba(255, 255, 255));
    }
}

static void testMissingFont() {
    ui::Font font;
    CHECK(!font.load("/nonexistent/Inter-Regular.ttf") && !font.loaded());
    ui::Canvas c(64, 32);
    c.clear(ui::rgba(1, 2, 3));
    CHECK(font.measure("Hello", 24) == 0);
    CHECK(font.draw(c, "Hello", 5, 4, 24, ui::rgba(255, 255, 255)) == 5);
    CHECK(c.data()[5 * 64 + 10] == ui::rgba(1, 2, 3));
    CHECK(font.centeredY(10, 40, 24) == 10 && font.lineHeight(24) == 24);
    CHECK(font.fit("Hello", 24, 100) == "Hello");
    CHECK(font.wrap("Hello world", 24, 100, 2).size() == 1);
}

// Settings' rows with the capabilities: all of them, in the same order and
// groups as ever, on the PS5; the desktop's hide what can't work there.

static void testVisibleSettingsRows() {
    using ui::SettingsRowPos;
    auto ids = [](const std::vector<SettingsRowPos>& v) {
        std::vector<int> out;
        for (const auto& r : v) out.push_back(r.row);
        return out;
    };
    auto groups = [](const std::vector<SettingsRowPos>& v) {
        std::vector<bool> out;
        for (const auto& r : v) out.push_back(r.groupStart);
        return out;
    };
    const ui::UiCaps ps5;
    auto all = ui::visibleSettingsRows(ps5);
    CHECK((ids(all) == std::vector<int>{0, 2, 8, 27, 17, 18, 24, 25, 1, 20, 9, 26, 10, 14, 12, 3, 15, 4, 21, 5, 6, 16, 22, 19, 7, 23}));
    CHECK((groups(all) == std::vector<bool>{false, false, false, false, false, false, false, false, true, false, false, false, false, true, false, false, false, false, false, false, false, false, false, true, false, true}));
    ui::UiCaps pc;
    pc.lightBar = pc.triggerRumble = pc.adaptiveTriggers = pc.gyro = pc.touchpad = false;
    pc.upscalers = pc.sharpen = pc.deband = pc.highRefresh = pc.console = pc.selfUpdate = pc.trophies = false;
    pc.openUrl = true;
    auto desk = ui::visibleSettingsRows(pc);
    CHECK((ids(desk) == std::vector<int>{0, 2, 8, 27, 17, 18, 1, 20, 26, 10, 14, 12, 3, 5, 16, 22, 19, 7, 23}));
    CHECK((groups(desk) == std::vector<bool>{false, false, false, false, false, false, true, false, false, false, true, false, false, false, false, false, true, false, true}));
    // The desktop app's window: Display mode after the resolution, in the stream's group.
    ui::UiCaps win = pc;
    win.desktopWindow = true;
    auto w = ui::visibleSettingsRows(win);
    CHECK((ids(w) == std::vector<int>{0, 2, 8, 27, 17, 18, 1, 20, 13, 26, 10, 14, 12, 3, 5, 16, 22, 19, 7, 23}));
    CHECK((groups(w) == std::vector<bool>{false, false, false, false, false, false, true, false, false, false, false, true, false, false, false, false, false, true, false, true}));
    auto deskIds = ids(desk);
    CHECK(std::find(deskIds.begin(), deskIds.end(), 24) == deskIds.end());
    CHECK(std::find(deskIds.begin(), deskIds.end(), 25) == deskIds.end());
    auto allIds = ids(all);
    CHECK(std::find(allIds.begin(), allIds.end(), 24) != allIds.end());
    CHECK(std::find(allIds.begin(), allIds.end(), 25) != allIds.end());
    CHECK(ui::settingsRowPosition(pc, 24) == -1);
    CHECK(ui::settingsRowPosition(pc, 25) == -1);
    ui::UiCaps xbox = pc;  // an Xbox pad: impulse triggers, no light bar
    xbox.triggerRumble = true;
    auto x = ids(ui::visibleSettingsRows(xbox));
    CHECK(std::find(x.begin(), x.end(), 4) != x.end() && std::find(x.begin(), x.end(), 6) == x.end());
    // PS4 console UI caps: DualShock 4 (touchpad/gyro/lightbar, no adaptive triggers) and no PS5 display features (120Hz, FSR1, deband).
    ui::UiCaps ps4;
    ps4.console = true;
    ps4.openUrl = false;
    ps4.selfUpdate = false;
    ps4.desktopWindow = false;
    ps4.lightBar = true;
    ps4.gyro = true;
    ps4.touchpad = true;
    ps4.triggerRumble = false;
    ps4.adaptiveTriggers = false;
    ps4.upscalers = false;
    ps4.sharpen = false;
    ps4.deband = false;
    ps4.highRefresh = false;
    auto ps4Rows = ids(ui::visibleSettingsRows(ps4));
    CHECK(std::find(ps4Rows.begin(), ps4Rows.end(), 24) != ps4Rows.end());
    CHECK(std::find(ps4Rows.begin(), ps4Rows.end(), 25) != ps4Rows.end());
    CHECK(std::find(ps4Rows.begin(), ps4Rows.end(), 9) == ps4Rows.end());
    CHECK(std::find(ps4Rows.begin(), ps4Rows.end(), 4) == ps4Rows.end());
    CHECK(std::find(ps4Rows.begin(), ps4Rows.end(), 21) == ps4Rows.end());
    CHECK(std::find(ps4Rows.begin(), ps4Rows.end(), 6) != ps4Rows.end());
    CHECK(std::find(ps4Rows.begin(), ps4Rows.end(), 15) != ps4Rows.end());
    auto ps4Game = ids(ui::visibleGameSettingsRows(ps4, true, true));
    CHECK(std::find(ps4Game.begin(), ps4Game.end(), 1) == ps4Game.end());
    CHECK(std::find(ps4Game.begin(), ps4Game.end(), 2) == ps4Game.end());
    CHECK(std::find(ps4Game.begin(), ps4Game.end(), 3) == ps4Game.end());
    CHECK(std::find(ps4Game.begin(), ps4Game.end(), 6) == ps4Game.end());
    CHECK(std::find(ps4Game.begin(), ps4Game.end(), 8) == ps4Game.end());
    CHECK(std::find(ps4Game.begin(), ps4Game.end(), 13) != ps4Game.end());
    // A game's settings.
    CHECK((ids(ui::visibleGameSettingsRows(ps5, true, false)) == std::vector<int>{0, 4, 8, 15, 14, 18, 1, 2, 3, 5, 6, 16, 7, 12, 11, 9, 10}));
    CHECK((ids(ui::visibleGameSettingsRows(ps5, true, true)) == std::vector<int>{0, 4, 8, 15, 14, 18, 1, 2, 3, 5, 6, 16, 7, 12, 11, 13, 9, 10}));
    CHECK((groups(ui::visibleGameSettingsRows(ps5, true, true)) ==
           std::vector<bool>{false, true, false, false, false, false, true, false, false, true, false, false, false, false, false, false, false, true}));
    CHECK((ids(ui::visibleGameSettingsRows(ps5, false)) == std::vector<int>{0, 4, 8, 15, 14, 18, 1, 2, 3, 5, 6, 16, 7, 12, 11, 9}));
    ui::UiCaps noTouch = ps5;
    noTouch.touchpad = false;
    CHECK((ids(ui::visibleGameSettingsRows(noTouch, true, true)) == std::vector<int>{0, 4, 8, 15, 14, 18, 1, 2, 3, 5, 6, 16, 7, 12, 11, 9, 10}));
    auto game = ui::visibleGameSettingsRows(pc, false);
    CHECK((ids(game) == std::vector<int>{0, 4, 15, 14, 18, 5, 7, 11, 9}));
    CHECK((groups(game) == std::vector<bool>{false, true, false, false, false, true, false, false, false}));
    // How far down the autoplay tests walk to each row (autoplay.cpp's downsTo).
    CHECK(ui::settingsRowPosition(ps5, 24) == 6);
    CHECK(ui::settingsRowPosition(ps5, 25) == 7);
    CHECK(ui::settingsRowPosition(ps5, 27) == 3 && ui::settingsRowPosition(ps5, 1) == 8 && ui::settingsRowPosition(ps5, 3) == 15);
    CHECK(ui::settingsRowPosition(ps5, 16) == 21);
    CHECK(ui::settingsRowPosition(ps5, 5) == 19 && ui::settingsRowPosition(ps5, 6) == 20);
    CHECK(ui::settingsRowPosition(ps5, 12) == 14 && ui::settingsRowPosition(ps5, 14) == 13);
    CHECK(ui::settingsRowPosition(ps5, 22) == 22);
    CHECK(ui::settingsRowPosition(ps5, 7) == 24 && ui::settingsRowPosition(ps5, 19) == 23 && ui::settingsRowPosition(ps5, 2) == 1);
    CHECK(ui::settingsRowPosition(ps5, 21) == 18);
    CHECK(ui::settingsRowPosition(pc, 5) == 13 && ui::settingsRowPosition(pc, 6) == -1 && ui::settingsRowPosition(pc, 4) == -1);
}

// The controller's glyphs by pad kind (ui/pad_glyphs.h).

static void testPadGlyphs() {
    using input::PadKind;
    using ui::GlyphStyle;
    using ui::PadButton;
    CHECK(ui::glyphStyleFor(PadKind::DualSense) == GlyphStyle::PlayStation);
    CHECK(ui::glyphStyleFor(PadKind::DualShock4) == GlyphStyle::PlayStation);
    CHECK(ui::glyphStyleFor(PadKind::None) == GlyphStyle::PlayStation);
    CHECK(ui::glyphStyleFor(PadKind::Xbox) == GlyphStyle::Xbox);
    CHECK(ui::glyphStyleFor(PadKind::Switch) == GlyphStyle::Xbox);
    CHECK(ui::glyphStyleFor(PadKind::Generic) == GlyphStyle::Xbox);
    // The PS5's pads, whatever the desktop shows.
    CHECK(ui::glyphStyleFor(input::anyPadCaps().kind) == GlyphStyle::PlayStation);  // null backend: no pad
    const PadButton all[] = {PadButton::Cross, PadButton::Circle, PadButton::Square, PadButton::Triangle,
                             PadButton::Options, PadButton::Touchpad, PadButton::L1, PadButton::R1,
                             PadButton::L2, PadButton::R2, PadButton::L3, PadButton::R3};
    std::string ps, xb;
    for (PadButton b : all) {
        ps += std::string(ui::buttonName(GlyphStyle::PlayStation, b)) + ",";
        xb += std::string(ui::buttonName(GlyphStyle::Xbox, b)) + ",";
    }
    CHECK(ps == "Cross,Circle,Square,Triangle,Options,Touchpad,L1,R1,L2,R2,L3,R3,");
    CHECK(xb == "A,B,X,Y,Menu,View,LB,RB,LT,RT,LS,RS,");
    // Xbox letters: A green, B red, X blue, Y yellow; no colour off the face.
    ui::Color a = ui::faceColor(GlyphStyle::Xbox, PadButton::Cross), b = ui::faceColor(GlyphStyle::Xbox, PadButton::Circle);
    CHECK(a != b && a != 0 && b != 0);
    CHECK(ui::faceColor(GlyphStyle::Xbox, PadButton::L1) == 0);
    CHECK(ui::faceColor(GlyphStyle::PlayStation, PadButton::Cross) == ui::rgba(124, 178, 232));
    CHECK(ui::faceColor(GlyphStyle::Xbox, PadButton::Cross) == ui::rgba(84, 178, 58));
    CHECK(ui::faceColor(GlyphStyle::Xbox, PadButton::Triangle) == ui::rgba(236, 184, 30));
}

// The desktop app's in-app notices (platform/toasts.h).

static void testDesktopSearchAndTextInput() {
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);

    // Desktop Search & text input
    app.showHome();
    ui::NavInput tri;
    tri.triangle = true;
    app.handle(tri); // opens search
    CHECK(app.wantsTextInput());

    app.typeText("Fortnite");
    CHECK(app.searchQuery() == "Fortnite");

    // Test backspace / delete
    app.typeText("\b");
    CHECK(app.searchQuery() == "Fortnit");

    // Close search
    ui::NavInput back;
    back.back = true;
    app.handle(back);
    CHECK(!app.wantsTextInput());
}

static void testM3PlatformAndBitrateUi() {
    // 1. Platform detection helpers on host (Linux)
    CHECK(!platform::isPS4());
    CHECK(!platform::isPS5());
    CHECK(!platform::platformName().empty());

    // 2. WebServer /api/settings exposes platform flags even without custom hooks
    {
        app::WebServer srv;
        auto res = srv.handleRequest("GET", "/api/settings", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j).has("platform") && !(*j)["platform"].str().empty());
        CHECK((*j).has("isPS4") && (*j)["isPS4"].asBool() == platform::isPS4());
        CHECK((*j).has("isPS5") && (*j)["isPS5"].asBool() == platform::isPS5());

        // HTML hides OneFamily toggle unless PS4, rounds FPS/QP, and marks > 15 Mbps experimental
        auto htmlRes = srv.handleRequest("GET", "/", "");
        CHECK(htmlRes.status == 200);
        CHECK(htmlRes.body.find("id=\"rowUsbControllers\" style=\"display:none;\"") != std::string::npos);
        CHECK(htmlRes.body.find("rowUsb.style.display = s.isPS4 ? 'flex' : 'none'") != std::string::npos);
        CHECK(htmlRes.body.find("Math.round(fps)") != std::string::npos);
        CHECK(htmlRes.body.find("Math.round(qp)") != std::string::npos);
        // The controller's settings and the gyro live under Mapeamento; the current game's tab only reads.
        CHECK(htmlRes.body.find("id=\"subRemapGyro\"") != std::string::npos);
        CHECK(htmlRes.body.find("onclick=\"saveControllerSettings()\"") != std::string::npos);
        CHECK(htmlRes.body.find("sendStreamParam('") == std::string::npos);
        CHECK(htmlRes.body.find("' Mbps*'") != std::string::npos);
        CHECK(htmlRes.body.find("Automático:") != std::string::npos);
        CHECK(htmlRes.body.find("Sem Limite:") != std::string::npos);
    }

    // 3. Bitrate labels & description distinguish Auto vs Unlimited and mark > 15 Mbps (experimental)
    {
        ui::setLanguage(ui::Language::PortugueseBR);
        CHECK(ui::StreamMenu::formatBitrateLabel(0) == "Automático");
        CHECK(ui::StreamMenu::formatBitrateLabel(-1) == "Sem Limite");
        CHECK(ui::StreamMenu::formatBitrateLabel(15) == "15 Mbps");
        CHECK(ui::StreamMenu::formatBitrateLabel(20) == "20 Mbps (experimental)");
        CHECK(ui::StreamMenu::formatBitrateLabel(50) == "50 Mbps (experimental)");
        std::string descPt = ui::tr(ui::Str::SettingDescBitrateCap);
        CHECK(descPt.find("Automático") != std::string::npos);
        CHECK(descPt.find("Sem Limite") != std::string::npos);
        ui::setLanguage(ui::Language::English);
        CHECK(ui::StreamMenu::formatBitrateLabel(0) == "Automatic");
        CHECK(ui::StreamMenu::formatBitrateLabel(-1) == "Unlimited");
        CHECK(ui::StreamMenu::formatBitrateLabel(25) == "25 Mbps (experimental)");
    }
}

static void testM4DebugNoticesExterminationAndToasts() {
    // 1. Verify localized strings for M4 exist and have content across all 6 languages
    for (int l = 0; l < static_cast<int>(ui::Language::Count); ++l) {
        ui::setLanguage(static_cast<ui::Language>(l));
        CHECK(std::strlen(ui::tr(ui::Str::SettingsSavedToast)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GameSettingsSavedToast)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::SecureConnUnavailableToast)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::TelemetryOnToast)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::TelemetryOffToast)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::MicMutedToast)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::MicUnmutedToast)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroCalibrationTitle)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroCalibratingPrompt)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroCalibratedSuccess)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroCalibratedFailed)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroCalibrateAction)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroSensorPos)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroGameOutput)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroSensitivityIntensity)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroYawSensitivity)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroPitchSensitivity)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroCalibration)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroDemoMode)) > 0);
        CHECK(std::strlen(ui::tr(ui::Str::GyroTestHelp)) > 0);
    }

    // 2. Gyro options and labels must not contain engineering/debug jargon (e.g. "Pad 0", "120 Hz")
    {
        ui::setLanguage(ui::Language::PortugueseBR);
        std::string cal = ui::tr(ui::Str::GyroCalibrateAction);
        std::string yaw = ui::trf(ui::Str::GyroYawSens, "100");
        std::string pitch = ui::trf(ui::Str::GyroPitchSens, "100");
        std::string prog = ui::trf(ui::Str::GyroCalibratingProgress, "10/100");
        std::vector<std::string> opts = {cal, yaw, pitch, prog};
        for (const auto& opt : opts) {
            CHECK(opt.find("Pad 0") == std::string::npos);
            CHECK(opt.find("pad 0") == std::string::npos);
            CHECK(opt.find("120 Hz") == std::string::npos);
            CHECK(opt.find("rotational variance") == std::string::npos);
        }
        CHECK(cal == "Calibrar Giroscópio");
    }

    // 3. Toasts queue integration with platform::notify
    {
        platform::enableToasts(true);
        platform::toasts().clear();
        platform::notify("Test In-App Toast");
        auto vis = platform::toasts().visible(platform::nowMs());
        CHECK(!vis.empty());
        CHECK(vis.back() == "Test In-App Toast");
    }
}

static void testAndroidM5QualityAndUxRefinements() {
    // 1. Settings default touchOverlayMode invariant: must be 0 (Auto)
    {
        xc::app::Settings s;
        CHECK(s.touchOverlayMode == 0);
    }

    // 2. Controller inUse threshold and Nexus button responsiveness (R1)
    {
        xc::input::ControllerState s{};
        CHECK(!xc::input::inUse(s));

        // Nexus / Xbox guide button must trigger inUse
        s.btnNexus = true;
        CHECK(xc::input::inUse(s));
        s.btnNexus = false;

        // Stick deflection: 0.10f is within deadzone noise, 0.16f triggers inUse
        s.leftStickX = 0.10f;
        CHECK(!xc::input::inUse(s));
        s.leftStickX = 0.16f;
        CHECK(xc::input::inUse(s));
        s.leftStickX = 0.0f;

        s.rightStickY = -0.16f;
        CHECK(xc::input::inUse(s));
        s.rightStickY = 0.0f;

        // Triggers deflection: 0.10f is noise, 0.16f triggers inUse
        s.triggerL2 = 0.10f;
        CHECK(!xc::input::inUse(s));
        s.triggerL2 = 0.16f;
        CHECK(xc::input::inUse(s));
        s.triggerL2 = 0.0f;
    }

    // 3. TouchLayout serialization and TouchLayoutStudio (R4)
    {
        xc::ui::TouchLayout def = xc::ui::defaultTouchLayout();
        CHECK(def.leftStick.x == 250.0f);
        CHECK(def.leftStick.y == 545.0f);
        CHECK(def.rightStick.x == 1355.0f);
        CHECK(def.rightStick.y == 825.0f);
        CHECK(def.dpad.x == 520.0f);
        CHECK(def.dpad.y == 830.0f);
        CHECK(def.btnA.x == 1655.0f);
        CHECK(def.btnA.y == 685.0f);
        CHECK(def.btnB.x == 1765.0f);
        CHECK(def.btnB.y == 575.0f);
        CHECK(def.btnX.x == 1545.0f);
        CHECK(def.btnX.y == 575.0f);
        CHECK(def.btnY.x == 1655.0f);
        CHECK(def.btnY.y == 465.0f);

        // Test JSON round-trip of custom layout
        xc::ui::TouchLayout custom = def;
        custom.leftStick.x = 300.0f;
        custom.leftStick.y = 600.0f;
        custom.btnA.x = 1700.0f;
        custom.btnA.y = 720.0f;

        std::string testPath = "/tmp/test_touch_layout.json";
        CHECK(xc::ui::saveTouchLayout(testPath, custom));

        xc::ui::TouchLayout loaded;
        CHECK(xc::ui::loadTouchLayout(testPath, loaded));
        CHECK(loaded.leftStick.x == 300.0f);
        CHECK(loaded.leftStick.y == 600.0f);
        CHECK(loaded.btnA.x == 1700.0f);
        CHECK(loaded.btnA.y == 720.0f);
        CHECK(loaded.dpad.x == 520.0f); // Unmodified retains original
        std::remove(testPath.c_str());
    }

    // 4. TouchPadOverlay Studio state machine and reset
    {
        xc::ui::Fonts dummyFonts;
        xc::ui::TouchPadOverlay overlay(dummyFonts);
        CHECK(!overlay.isLayoutStudioActive());

        overlay.openLayoutStudio();
        CHECK(overlay.isLayoutStudioActive());
        CHECK(overlay.isVisible());

        // Modify layout
        xc::ui::TouchLayout mod = overlay.layout();
        mod.dpad.x = 400.0f;
        mod.dpad.y = 700.0f;
        overlay.setLayout(mod);
        CHECK(overlay.layout().dpad.x == 400.0f);

        // Reset to default
        overlay.resetToDefaultLayout();
        CHECK(overlay.layout().dpad.x == 520.0f);

        overlay.closeLayoutStudio();
        CHECK(!overlay.isLayoutStudioActive());
    }

    // 5. StreamMenu touch toggle, studio action, and tab consolidation (R2 & R3)
    {
        xc::ui::Fonts dummyFonts;
        xc::ui::StreamMenu menu(dummyFonts);

        // Touch overlay enabled by default
        CHECK(menu.touchOverlayEnabled());
        menu.setTouchOverlayEnabled(false);
        CHECK(!menu.touchOverlayEnabled());
        menu.setTouchOverlayEnabled(true);

        // Leave item is at the end of Item enum
        CHECK(xc::ui::StreamMenu::Leave == xc::ui::StreamMenu::ItemCount - 1);

        // Tab placement checks
#if defined(__ANDROID__)
        CHECK(menu.itemInTab(xc::ui::StreamMenu::TouchOverlayItem, xc::ui::MenuTab::General));
        CHECK(menu.itemInTab(xc::ui::StreamMenu::TouchInvertSticksItem, xc::ui::MenuTab::General));
        CHECK(menu.itemInTab(xc::ui::StreamMenu::TouchLayoutStudioItem, xc::ui::MenuTab::General));
        CHECK(menu.itemInTab(xc::ui::StreamMenu::MicMuteItem, xc::ui::MenuTab::General));
        CHECK(menu.itemInTab(xc::ui::StreamMenu::Leave, xc::ui::MenuTab::General));
        // Guitar lane items must NEVER show outside MenuTab::Guitar (preventing duplicate 'Sair do jogo')
        for (int it = xc::ui::StreamMenu::GuitarLane0; it <= xc::ui::StreamMenu::GuitarReset; ++it) {
            CHECK(!menu.itemInTab(it, xc::ui::MenuTab::General));
            CHECK(!menu.itemShown(it));
        }
#else
        CHECK(menu.itemInTab(xc::ui::StreamMenu::TouchOverlayItem, xc::ui::MenuTab::General));
        CHECK(menu.itemInTab(xc::ui::StreamMenu::TouchInvertSticksItem, xc::ui::MenuTab::General));
        CHECK(menu.itemInTab(xc::ui::StreamMenu::TouchLayoutStudioItem, xc::ui::MenuTab::General));
        CHECK(menu.itemInTab(xc::ui::StreamMenu::MicMuteItem, xc::ui::MenuTab::General));
        CHECK(menu.itemInTab(xc::ui::StreamMenu::Leave, xc::ui::MenuTab::General));
        // On non-Android, itemShown returns false for touch items
        CHECK(!menu.itemShown(xc::ui::StreamMenu::TouchOverlayItem));
        CHECK(!menu.itemShown(xc::ui::StreamMenu::TouchInvertSticksItem));
        CHECK(!menu.itemShown(xc::ui::StreamMenu::TouchLayoutStudioItem));
        CHECK(!menu.itemShown(xc::ui::StreamMenu::MicMuteItem));
#endif
    }

    // 6. TouchPadOverlay Stick Inversion and Always Off (Bug 3 & 4)
    {
        xc::ui::Fonts dummyFonts;
        xc::ui::TouchPadOverlay overlay(dummyFonts);
        xc::app::Settings s;
        s.touchOverlayMode = 0; // Auto-hide
        s.touchInvertSticks = false;
        overlay.setSettings(s);

        // Touch left stick: Down anchors the stick, Motion deflects downward (dy > 0)
        app::shell::TouchEvent evDown{app::shell::TouchEvent::Kind::Down, 1, 250.0f, 545.0f};
        CHECK(overlay.onTouch(evDown));
        app::shell::TouchEvent evMotionDown{app::shell::TouchEvent::Kind::Motion, 1, 250.0f, 600.0f};
        CHECK(overlay.onTouch(evMotionDown));
        // Default (invertSticks = false): dy > 0 -> leftStickY > 0 (+down)
        CHECK(overlay.state().leftStickY > 0.3f);

        // Toggle stick inversion
        overlay.setInvertSticks(true);
        CHECK(overlay.onTouch(evMotionDown));
        // Inverted: dy > 0 -> leftStickY < 0
        CHECK(overlay.state().leftStickY < -0.3f);

        // Release finger
        app::shell::TouchEvent evUp{app::shell::TouchEvent::Kind::Up, 1, 250.0f, 600.0f};
        CHECK(overlay.onTouch(evUp));

        // Disable touch overlay (Mode 2 = Always Off)
        s.touchOverlayMode = 2;
        overlay.setSettings(s);
        CHECK(!overlay.isVisible());
        // Touches on game buttons must be ignored when overlay is disabled
        app::shell::TouchEvent evBtnA{app::shell::TouchEvent::Kind::Down, 2, 1655.0f, 685.0f};
        CHECK(!overlay.onTouch(evBtnA));
        CHECK(!overlay.state().btnA);
        CHECK(!overlay.state().connected);
    }
}

static void testUiTouchDragVsTap() {
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);

    // 1. Tap: finger down, small delta (< 16px), finger up -> activates without error
    app::shell::TouchEvent tapDown{app::shell::TouchEvent::Kind::Down, 1, 500.0f, 300.0f};
    app::shell::TouchEvent tapMove{app::shell::TouchEvent::Kind::Motion, 1, 504.0f, 302.0f}; // 4.5px < 16px
    app::shell::TouchEvent tapUp{app::shell::TouchEvent::Kind::Up, 1, 504.0f, 302.0f};

    app.onTouch(tapDown);
    app.onTouch(tapMove);
    app.onTouch(tapUp);

    // 2. Drag: finger down, delta > 16px -> engages scrolling
    app::shell::TouchEvent dragDown{app::shell::TouchEvent::Kind::Down, 2, 500.0f, 500.0f};
    app::shell::TouchEvent dragMove{app::shell::TouchEvent::Kind::Motion, 2, 500.0f, 400.0f}; // 100px drag
    app::shell::TouchEvent dragUp{app::shell::TouchEvent::Kind::Up, 2, 500.0f, 400.0f};

    app.onTouch(dragDown);
    app.onTouch(dragMove);
    app.onTouch(dragUp);
}

static void testSafeAreaBoxCalculation() {
    // Applying safe area insets: Left=120, Top=60, Right=80, Bottom=40
    display::gpu::setSafeAreaInsets(120, 60, 80, 40);
    display::gpu::setPresentationMode(display::gpu::DisplayPresentationMode::Standard);

    // TopScreenOnly mode (split vertical orientation / clamshell top screen)
    display::gpu::setPresentationMode(display::gpu::DisplayPresentationMode::TopScreenOnly);

    // Reset back to standard with 0 insets
    display::gpu::setSafeAreaInsets(0, 0, 0, 0);
    display::gpu::setPresentationMode(display::gpu::DisplayPresentationMode::Standard);
}

static void testMilestone1TouchOverlayCanvasAndHaptics() {
    // 1. Canvas alpha transparency & constructor default
    {
        ui::Canvas cDefault(32, 32);
        CHECK(cDefault.width() == 32 && cDefault.height() == 32);
        // Default constructor must initialize transparent (alpha == 0)
        for (int i = 0; i < 32 * 32; ++i) {
            CHECK(cDefault.data()[i] == 0u);
        }

        ui::Canvas cCustom(16, 16, ui::rgba(10, 20, 30, 80));
        CHECK(cCustom.data()[0] == ui::rgba(10, 20, 30, 80));

        cCustom.clearTransparent();
        CHECK(cCustom.data()[0] == 0u);

        cCustom.clearRgba(ui::rgba(100, 150, 200, 128));
        CHECK(cCustom.data()[0] == ui::rgba(100, 150, 200, 128));

        cCustom.clear(ui::rgba(0, 0, 0, 0));
        CHECK(cCustom.data()[0] == 0u); // clear() must preserve transparent alpha without forcing 0xFF

        // Source-over alpha blend onto transparent destination
        ui::Canvas cBlend(10, 10);
        cBlend.fillRect(ui::Rect{0, 0, 10, 10}, ui::rgba(255, 0, 0, 128));
        uint32_t px = cBlend.data()[0];
        CHECK((px >> 24) == 128);
        CHECK((px & 0xFF) == 255);
    }

    // 2. Ergonomic 8-Way Radial D-Pad hit-test and enlarged controls
    {
        ui::Fonts fonts;
        ui::TouchPadOverlay overlay(fonts);
        overlay.reset();

        constexpr float dpadCx = 520.0f, dpadCy = 830.0f;

        // 8 radial sectors around (520, 830) at radius = 80px (within [40..145])
        CHECK(overlay.hitTest(dpadCx + 80.0f, dpadCy) == ui::TouchControl::DpadRight);
        CHECK(overlay.hitTest(dpadCx + 60.0f, dpadCy + 60.0f) == ui::TouchControl::DpadDownRight);
        CHECK(overlay.hitTest(dpadCx, dpadCy + 80.0f) == ui::TouchControl::DpadDown);
        CHECK(overlay.hitTest(dpadCx - 60.0f, dpadCy + 60.0f) == ui::TouchControl::DpadDownLeft);
        CHECK(overlay.hitTest(dpadCx - 80.0f, dpadCy) == ui::TouchControl::DpadLeft);
        CHECK(overlay.hitTest(dpadCx - 60.0f, dpadCy - 60.0f) == ui::TouchControl::DpadUpLeft);
        CHECK(overlay.hitTest(dpadCx, dpadCy - 80.0f) == ui::TouchControl::DpadUp);
        CHECK(overlay.hitTest(dpadCx + 60.0f, dpadCy - 60.0f) == ui::TouchControl::DpadUpRight);

        // Deadzones
        CHECK(overlay.hitTest(dpadCx, dpadCy) == ui::TouchControl::None); // Center deadzone < 40px
        CHECK(overlay.hitTest(dpadCx + 25.0f, dpadCy) == ui::TouchControl::None); // Inner radius < 40px
        CHECK(overlay.hitTest(dpadCx, 1060.0f) == ui::TouchControl::None); // Outer radius > 145px

        // Enlarged ABXY diamond (r=78px hitbox)
        CHECK(overlay.hitTest(1655.0f, 685.0f) == ui::TouchControl::ButtonA);
        CHECK(overlay.hitTest(1765.0f, 575.0f) == ui::TouchControl::ButtonB);
        CHECK(overlay.hitTest(1545.0f, 575.0f) == ui::TouchControl::ButtonX);
        CHECK(overlay.hitTest(1655.0f, 465.0f) == ui::TouchControl::ButtonY);

        // Generous thumb reach coverage (±70px within 78px radius hitbox)
        CHECK(overlay.hitTest(1655.0f, 685.0f + 70.0f) == ui::TouchControl::ButtonA);
        CHECK(overlay.hitTest(1765.0f + 70.0f, 575.0f) == ui::TouchControl::ButtonB);
        CHECK(overlay.hitTest(1545.0f - 70.0f, 575.0f) == ui::TouchControl::ButtonX);
        CHECK(overlay.hitTest(1655.0f, 465.0f - 70.0f) == ui::TouchControl::ButtonY);

        // Enlarged Triggers (230x90) and Bumpers (230x84)
        CHECK(overlay.hitTest(100.0f, 150.0f) == ui::TouchControl::TriggerLT);
        CHECK(overlay.hitTest(100.0f, 250.0f) == ui::TouchControl::BumperLB);
        CHECK(overlay.hitTest(1700.0f, 150.0f) == ui::TouchControl::TriggerRT);
        CHECK(overlay.hitTest(1700.0f, 250.0f) == ui::TouchControl::BumperRB);

        // Pull tab zone
        CHECK(overlay.hitTest(50.0f, 50.0f) == ui::TouchControl::OverlayToggle);
    }

    // 3. Touch overlay settings wiring & persistence
    {
        app::Settings settings;
        settings.touchOverlayMode = 1; // Always On
        std::string tempPath = "/tmp/test_settings_m1.json";
        CHECK(settings.save(tempPath));

        app::Settings loaded;
        CHECK(loaded.load(tempPath));
        CHECK(loaded.touchOverlayMode == 1);
        std::remove(tempPath.c_str());

        ui::Fonts fonts;
        ui::TouchPadOverlay overlay(fonts);

        // Apply Always On mode (1)
        overlay.setSettings(settings);
        CHECK(overlay.overlayMode() == 1);
        CHECK(overlay.isVisible());
        CHECK(overlay.opacity() == 0.80f);

        // In Always On mode, even if physical controller is attached and active, overlay remains visible
        uint64_t t = 1000;
        for (int i = 0; i < 30; ++i) {
            overlay.update(t, true, true);
            t += 16;
        }
        CHECK(overlay.isVisible());
        CHECK(overlay.opacity() == 0.80f);

        // Apply Always Off mode (2)
        settings.touchOverlayMode = 2;
        overlay.setSettings(settings);
        CHECK(overlay.overlayMode() == 2);
        CHECK(!overlay.isVisible());
        CHECK(overlay.opacity() == 0.0f);

        // In Always Off mode, touch events are ignored
        app::shell::TouchEvent ev{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        CHECK(!overlay.onTouch(ev));

        // Apply Auto mode (0)
        settings.touchOverlayMode = 0;
        overlay.setSettings(settings);
        CHECK(overlay.overlayMode() == 0);
    }

    // 4. Multi-touch 3-finger zero-pad input rollback & haptics contract
    {
        ui::Fonts fonts;
        ui::TouchPadOverlay overlay(fonts);
        overlay.reset();

        // Simulate finger 1 touching A and finger 2 touching B
        app::shell::TouchEvent f1{app::shell::TouchEvent::Kind::Down, 1, 1655.0f, 685.0f};
        app::shell::TouchEvent f2{app::shell::TouchEvent::Kind::Down, 2, 1765.0f, 575.0f};
        CHECK(overlay.onTouch(f1));
        CHECK(overlay.onTouch(f2));

        input::ControllerState sActive{};
        bool conn = false;
        app::virtualGamepadState(0, sActive, conn);
        CHECK(sActive.btnA);
        CHECK(sActive.btnB);

        // 3-finger cancellation: overlay.reset() + force-inject zero ControllerState
        overlay.reset();
        app::setVirtualGamepad(0, input::ControllerState{});

        input::ControllerState sReset{};
        app::virtualGamepadState(0, sReset, conn);
        CHECK(!sReset.btnA);
        CHECK(!sReset.btnB);
        CHECK(!sReset.btnX);
        CHECK(!sReset.btnY);
        CHECK(sReset.leftStickX == 0.0f && sReset.leftStickY == 0.0f);
        CHECK(sReset.rightStickX == 0.0f && sReset.rightStickY == 0.0f);

        // Verify HapticType enum and non-throwing triggerHaptic contract
        input::triggerHaptic(input::HapticType::Click);
        input::triggerHaptic(input::HapticType::Tick);
        input::triggerHaptic(input::HapticType::HeavyClick);
    }
}

void runUiTests() {
    testStrings();
    testAccentColor();
    testCloudTimeClock();
    testFontsFallBackToBuiltin();
    testSplashInputBlocking();
    testStreamMenu();
    testSmartGlassInGameMenuInvariants();
    testMissingFont();
    testVisibleSettingsRows();
    testPadGlyphs();
    testDesktopSearchAndTextInput();
    testM3PlatformAndBitrateUi();
    testM4DebugNoticesExterminationAndToasts();
    testAndroidM5QualityAndUxRefinements();
    testUiTouchDragVsTap();
    testSafeAreaBoxCalculation();
    testMilestone1TouchOverlayCanvasAndHaptics();
}
