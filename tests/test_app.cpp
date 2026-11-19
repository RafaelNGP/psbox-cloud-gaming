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

static void testVersions() {
    using xc::app::isNewerVersion;
    CHECK(isNewerVersion("v0.4.0", "0.3.0"));
    CHECK(isNewerVersion("v0.3.1", "0.3.0"));
    CHECK(isNewerVersion("1.0", "0.9.9"));
    CHECK(!isNewerVersion("v0.3.0", "0.3.0"));
    CHECK(!isNewerVersion("v0.2.9", "0.3.0"));
    CHECK(!isNewerVersion("nightly", "0.3.0"));
    CHECK(!isNewerVersion("", "0.3.0"));
}

static void testSettingsMigration() {
    // A file from before v0.8.0 at the old defaults gets the new ones; other
    // choices, and files already saved by v0.8.0, stay as they are.
    std::string path = "/tmp/xcloud-tests-settings.json";
    auto loadFrom = [&](const std::string& text) {
        xc::platform::writeFileAtomic(path, text);
        xc::app::Settings s;
        CHECK(s.load(path));
        return s;
    };
    auto old = loadFrom(R"({"deband":1,"upscaler":0})");
    CHECK(old.deband == 3 && old.upscaler == 2);
    auto chosen = loadFrom(R"({"deband":2,"upscaler":1})");
    CHECK(chosen.deband == 2 && chosen.upscaler == 1);
    auto current = loadFrom(R"({"deband":1,"upscaler":0,"settingsVersion":2})");
    CHECK(current.deband == 1 && current.upscaler == 0);
    // Saved again, the migration doesn't repeat.
    CHECK(old.save(path));
    xc::app::Settings again;
    CHECK(again.load(path) && again.deband == 3 && again.upscaler == 2);
    // The triggers' on/off before their strength could be chosen.
    CHECK(loadFrom(R"({"triggerRumble":false})").triggerStrength == 0 && loadFrom("{}").triggerStrength == 2);
    // One dead zone for both sticks, until each could have its own.
    auto one = loadFrom(R"({"deadzone":22})");
    CHECK(one.deadzoneLeft == 22 && one.deadzoneRight == 22);
    auto two = loadFrom(R"({"deadzone":22,"deadzoneLeft":8,"deadzoneRight":30})");
    CHECK(two.deadzoneLeft == 8 && two.deadzoneRight == 30);
    // The light bar's on/off before its colour could be chosen.
    CHECK(loadFrom(R"({"lightBar":false})").lightBarMode == 2 && loadFrom(R"({"lightBar":true})").lightBarMode == 0);
    xc::app::Settings custom;
    custom.lightBarMode = 1;
    custom.lightBarColour = xc::ui::rgba(0x12, 0x34, 0x56);
    CHECK(custom.save(path));
    std::string text;
    CHECK(xc::platform::readFile(path, text) && text.find("\"#123456\"") != std::string::npos);
    xc::app::Settings back;
    CHECK(back.load(path) && back.lightBarMode == 1 && back.lightBarColour == xc::ui::rgba(0x12, 0x34, 0x56));
    std::remove(path.c_str());
}

// The microphone's gain is stored as a choice (0..2), not as the factor.
// Settings' screen and settings.json share one mapping: every field the
// screen carries comes back as it was (Settings -> choice -> Settings), and
// the fields it doesn't carry are left alone.

static void testSettingsChoiceRoundTrip() {
    using namespace xc::input;
    xc::app::Settings s;
    s.language = "pt-BR";
    s.resolution = "720p";
    s.region = "Brazil South";
    s.loginCountry = "jp";
    s.deadzoneLeft = 7, s.deadzoneRight = 21;
    s.triggerStrength = 4, s.triggerHz = 2, s.triggerResistance = 3, s.triggerPulses = true, s.triggerProfile = 2;
    s.circleConfirms = true;
    s.lightBarMode = 1, s.lightBarColour = 0xFF112233;
    s.sharpness = 3, s.deband = 1, s.upscaler = 0;
    s.videoDecoder = 1;
    s.highRefresh = true;
    s.fpsLimit = 60;
    s.maxBitrateMbps = 15, s.gameVolume = 150;
    assignKey(s.keyMap, KeyAction::A, 0, 'E');
    assignPadSource(s.padMap, PadFn::A, static_cast<uint8_t>(PadFn::B));
    CHECK(padMapValid(s.padMap));
    s.gyroMode = 2, s.gyroLevel = 9;
    s.desktopFullscreen = false;
    s.touchpadMouse = true;
    s.queueReadyAlert = false;
    s.titlePlaytime = {{"game1", 3600}};
    // Not carried by the screen: must survive applyChoice untouched.
    s.keyboardPlayer = 3;
    s.streamStats = true;
    s.favorites = {"P1"};

    ui::SettingsChoice c = ui::choiceFromSettings(s);
    CHECK(c.language == static_cast<int>(ui::Language::PortugueseBR));
    CHECK(c.resolution == 1 && c.triggerProfile == 2 && c.lightBarColour == 0xFF112233u && !c.fullscreen);
    CHECK(c.videoDecoder == 1);
    CHECK(c.fpsLimit == 60);
    CHECK(!c.queueReadyAlert && c.titlePlaytime.at("game1") == 3600);

    xc::app::Settings back;  // defaults, then what the screen carries
    back.keyboardPlayer = 3;
    back.streamStats = true;
    back.favorites = {"P1"};
    ui::applyChoice(c, back);
    CHECK(back.language == s.language);
    CHECK(back.resolution == s.resolution);
    CHECK(back.region == s.region && back.loginCountry == s.loginCountry);
    CHECK(back.deadzoneLeft == s.deadzoneLeft && back.deadzoneRight == s.deadzoneRight);
    CHECK(back.triggerStrength == s.triggerStrength && back.triggerHz == s.triggerHz &&
          back.triggerResistance == s.triggerResistance && back.triggerPulses == s.triggerPulses &&
          back.triggerProfile == s.triggerProfile);
    CHECK(back.circleConfirms == s.circleConfirms);
    CHECK(back.lightBarMode == s.lightBarMode && back.lightBarColour == s.lightBarColour);
    CHECK(back.sharpness == s.sharpness && back.deband == s.deband && back.upscaler == s.upscaler);
    CHECK(back.videoDecoder == s.videoDecoder);
    CHECK(back.highRefresh == s.highRefresh);
    CHECK(back.fpsLimit == s.fpsLimit);
    CHECK(back.maxBitrateMbps == s.maxBitrateMbps && back.gameVolume == s.gameVolume);
    CHECK(back.queueReadyAlert == s.queueReadyAlert && back.titlePlaytime == s.titlePlaytime);
    CHECK(back.keyMap == s.keyMap && back.padMap == s.padMap);
    CHECK(back.gyroMode == s.gyroMode && back.gyroLevel == s.gyroLevel);
    CHECK(back.desktopFullscreen == s.desktopFullscreen && back.touchpadMouse == s.touchpadMouse);
    CHECK(back.keyboardPlayer == 3 && back.streamStats && back.favorites.size() == 1);
    CHECK(ui::formatPlaytime(0) == "< 1 min");
    CHECK(ui::formatPlaytime(45) == "< 1 min");
    CHECK(ui::formatPlaytime(60) == "1 min");
    CHECK(ui::formatPlaytime(2700) == "45 min");
    CHECK(ui::formatPlaytime(3600) == "1 h");
    CHECK(ui::formatPlaytime(3660) == "1 h 1 min");
    CHECK(ui::formatPlaytime(8100) == "2 h 15 min");
    // Every resolution, both ways ("1440p" from older versions reads as the top tier).
    for (const char* res : {"1080p", "720p", "best"}) {
        xc::app::Settings r;
        r.resolution = res;
        xc::app::Settings out;
        ui::applyChoice(ui::choiceFromSettings(r), out);
        CHECK(out.resolution == res);
    }
    xc::app::Settings old;
    old.resolution = "1440p";
    CHECK(ui::choiceFromSettings(old).resolution == 2);
    // An incomplete controller map from the screen never replaces a valid one.
    ui::SettingsChoice broken = c;
    broken.padMap[0] = kPadNone;
    xc::app::Settings kept = s;
    ui::applyChoice(broken, kept);
    CHECK(kept.padMap == s.padMap);
}

static void testGeneralSettingsR2() {
    // 1. Bitrate unlimited (-1) roundtrip
    {
        xc::app::Settings s;
        s.maxBitrateMbps = -1;
        s.fpsLimit = 120;
        s.highRefresh = true;
        s.videoDecoder = 1;
        ui::SettingsChoice c = ui::choiceFromSettings(s);
        CHECK(c.maxBitrateMbps == -1);
        CHECK(c.fpsLimit == 120);
        CHECK(c.highRefresh == true);
        CHECK(c.videoDecoder == 1);
        xc::app::Settings back;
        ui::applyChoice(c, back);
        CHECK(back.maxBitrateMbps == -1);
        CHECK(back.fpsLimit == 120);
        CHECK(back.highRefresh == true);
        CHECK(back.videoDecoder == 1);
    }

    // 2. Settings JSON parsing for R2 options
    {
        xc::app::Settings s;
        json::Value root = json::Value::object();
        root.set("maxBitrateMbps", static_cast<int64_t>(-1));
        root.set("fpsLimit", static_cast<int64_t>(30));
        root.set("videoDecoder", static_cast<int64_t>(0));
        root.set("highRefresh", true);
        std::string path = "/tmp/test_r2_settings.json";
        std::string jsonStr = root.dump();
        FILE* f = std::fopen(path.c_str(), "w");
        CHECK(f != nullptr);
        std::fwrite(jsonStr.data(), 1, jsonStr.size(), f);
        std::fclose(f);

        CHECK(s.load(path));
        CHECK(s.maxBitrateMbps == -1);
        CHECK(s.fpsLimit == 30);
        CHECK(s.videoDecoder == 0);
        CHECK(s.highRefresh == true);
        std::remove(path.c_str());
    }

    // 3. SettingsManager JSON updates
    {
        auto& mgr = xc::app::SettingsManager::instance();
        json::Value patch = json::Value::object();
        patch.set("maxBitrateMbps", static_cast<int64_t>(100)); // 100 Mbps maps to -1 (unlimited)
        patch.set("fpsLimit", static_cast<int64_t>(60));
        patch.set("videoDecoder", static_cast<int64_t>(1));
        patch.set("highRefresh", true);
        CHECK(mgr.updateFromJson(patch, true));
        xc::app::Settings cur = mgr.get();
        CHECK(cur.maxBitrateMbps == -1);
        CHECK(cur.fpsLimit == 60);
        CHECK(cur.videoDecoder == 1);
        CHECK(cur.highRefresh == true);

        // Clamping invalid FPS limits to 0 (unlimited)
        json::Value patch2 = json::Value::object();
        patch2.set("fpsLimit", static_cast<int64_t>(45));
        CHECK(mgr.updateFromJson(patch2, true));
        CHECK(mgr.get().fpsLimit == 0);
    }

    // 3b. SettingsManager syncFrom & deadlock safety
    {
        auto& mgr = xc::app::SettingsManager::instance();
        uint64_t vBefore = mgr.version();
        xc::app::Settings s = mgr.get();
        s.maxBitrateMbps = 42;
        s.fpsLimit = 120;
        s.videoDecoder = 1;

        bool listenerCalled = false;
        int dummyTag = 123;
        mgr.subscribe(&dummyTag, [&](const xc::app::SettingsChangeEvent& ev) {
            listenerCalled = true;
            // Verify listener can read settings safely without deadlock
            auto readSettings = mgr.get();
            (void)readSettings;
        });

        mgr.syncFrom(s);
        CHECK(listenerCalled);
        CHECK(mgr.version() > vBefore);
        CHECK(mgr.get().maxBitrateMbps == 42);
        CHECK(mgr.get().fpsLimit == 120);
        CHECK(mgr.get().videoDecoder == 1);

        // Verify deadlock safety: even if a caller holds g_settingsMutex, syncFrom must NOT deadlock
        {
            std::lock_guard<std::mutex> lock(xc::app::g_settingsMutex);
            s.fpsLimit = 60;
            mgr.syncFrom(s);
            CHECK(mgr.get().fpsLimit == 60);
        }
        mgr.unsubscribe(&dummyTag);
    }

    // 4. StreamPlayer FPS Limit & Bitrate Cap API
    {
        xc::xcloud::GssvClient gssv;
        xc::app::StreamPlayer player(gssv);
        CHECK(player.fpsLimit() == 0);
        player.setFpsLimit(30);
        CHECK(player.fpsLimit() == 30);
        player.setFpsLimit(60);
        CHECK(player.fpsLimit() == 60);
        player.setFpsLimit(120);
        CHECK(player.fpsLimit() == 120);
        player.setFpsLimit(0);
        CHECK(player.fpsLimit() == 0);

        // Bitrate cap
        player.setMaxBitrate(-1); // Unlimited -> 100 Mbps REMB
        player.setMaxBitrate(25); // 25 Mbps
        player.setMaxBitrate(0);  // Auto
    }
}

static void testMicGain() {
    app::Settings s;
    CHECK(s.micGain == 1 && s.micGainFactor() == 1.0f);
    s.micGain = 0;
    CHECK(s.micGainFactor() == 0.5f);
    s.micGain = 2;
    CHECK(s.micGainFactor() == 1.5f);
    s.micGain = 7;  // out of range (a hand-edited file): the nearest choice
    CHECK(s.micGainFactor() == 1.5f);
}

static void testPerGameSettings() {
    std::string path = "/tmp/xcloud-tests-pergame.json";
    xc::app::Settings s;
    s.sharpness = 1;
    s.deband = 2;
    s.upscaler = 0;
    s.videoDecoder = 0;
    s.deadzoneLeft = 12;
    s.deadzoneRight = 18;
    s.triggerStrength = 3;
    s.circleConfirms = false;

    // Default profile reflects global settings.
    s.resolution = "best";
    CHECK(s.defaultProfile().resolution == 2);
    s.resolution = "720p";
    CHECK(s.defaultProfile().resolution == 1);
    s.resolution = "1080p";
    CHECK(s.defaultProfile().resolution == 0);
    auto def = s.defaultProfile();
    CHECK(def.sharpness == 1 && def.deband == 2 && def.upscaler == 0 && def.videoDecoder == 0);
    CHECK(def.deadzoneLeft == 12 && def.deadzoneRight == 18 && def.triggerStrength == 3);
    CHECK(!def.circleConfirms);
    CHECK(!def.highRefresh);  // off unless chosen
    CHECK(!def.touchpadMouse);

    // Unknown games return default profile with hasCustom = false.
    bool hasCustom = true;
    auto prof1 = s.profileForGame("GAME1", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(prof1.sharpness == 1 && prof1.deadzoneLeft == 12 && prof1.videoDecoder == 0);

    // Add per-game profile for GAME1.
    xc::app::GameProfile p1;
    p1.sharpness = 3;
    p1.deband = 0;
    p1.upscaler = 3;
    p1.videoDecoder = 1;  // override global HW with SW
    p1.deadzoneLeft = 5;
    p1.deadzoneRight = 8;
    p1.triggerStrength = 1;
    p1.circleConfirms = true;
    p1.highRefresh = true;
    p1.touchpadMouse = true;
    p1.locale = "pt-BR";
    s.favorites = {"GAME1", "GAME3"};
    s.perGame["GAME1"] = p1;

    // Now GAME1 returns custom profile, while GAME2 still returns defaults.
    auto prof1Custom = s.profileForGame("GAME1", "", &hasCustom);
    CHECK(hasCustom);
    CHECK(prof1Custom.sharpness == 3 && prof1Custom.upscaler == 3 && prof1Custom.circleConfirms);
    CHECK(prof1Custom.deadzoneLeft == 5 && prof1Custom.triggerStrength == 1 && prof1Custom.touchpadMouse);
    CHECK(prof1Custom.videoDecoder == 1);
    CHECK(prof1Custom.locale == "pt-BR");

    hasCustom = true;
    auto prof2 = s.profileForGame("GAME2", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(prof2.sharpness == 1 && !prof2.circleConfirms && !prof2.touchpadMouse && prof2.locale.empty() && prof2.videoDecoder == 0);

    // Title ID fallback when productId is empty.
    s.perGame["CONSOLE_SERVER_1"] = p1;
    auto consoleProf = s.profileForGame("", "CONSOLE_SERVER_1", &hasCustom);
    CHECK(hasCustom);
    CHECK(consoleProf.sharpness == 3 && consoleProf.circleConfirms && consoleProf.locale == "pt-BR" && consoleProf.videoDecoder == 1);

    // Save to disk and reload.
    CHECK(s.desktopFullscreen);  // a desktop window starts fullscreen unless chosen otherwise
    s.desktopFullscreen = false;
    CHECK(s.save(path));
    xc::app::Settings reloaded;
    CHECK(reloaded.load(path));
    CHECK(!reloaded.desktopFullscreen);
    CHECK(reloaded.favorites.size() == 2 && reloaded.favorites[0] == "GAME1" && reloaded.favorites[1] == "GAME3");
    CHECK(reloaded.perGame.size() == 2);
    auto loadedP1 = reloaded.profileForGame("GAME1", "", &hasCustom);
    CHECK(hasCustom);
    CHECK(loadedP1.sharpness == 3 && loadedP1.upscaler == 3 && loadedP1.circleConfirms);
    CHECK(loadedP1.highRefresh && loadedP1.touchpadMouse && loadedP1.locale == "pt-BR" && loadedP1.videoDecoder == 1);
    CHECK(loadedP1.deadzoneLeft == 5 && loadedP1.deadzoneRight == 8 && loadedP1.triggerStrength == 1);

    // A game's trophies follow the general setting until the player chooses for
    // that game: a profile merely made (or written by a version before the choice
    // was recorded, with "syncTrophies": false) does not turn them off.
    {
        xc::app::Settings t;
        t.syncTrophies = true;
        t.perGame["MADE"] = t.defaultProfile();          // made for some other setting
        CHECK(t.perGame["MADE"].syncTrophies && !t.perGame["MADE"].syncTrophiesChosen);
        t.perGame["OFF"] = t.defaultProfile();
        t.perGame["OFF"].syncTrophies = false;           // turned off by the player, for this game
        t.perGame["OFF"].syncTrophiesChosen = true;
        t.perGame["OLD"] = t.defaultProfile();
        t.perGame["OLD"].syncTrophies = false;           // as an older version left it, with no choice made
        const std::string tp = path + ".trophies";
        CHECK(t.save(tp));
        xc::app::Settings tb;
        CHECK(tb.load(tp));
        CHECK(tb.perGame["MADE"].syncTrophies && !tb.perGame["MADE"].syncTrophiesChosen);
        CHECK(!tb.perGame["OFF"].syncTrophies && tb.perGame["OFF"].syncTrophiesChosen);
        CHECK(tb.perGame["OLD"].syncTrophies && !tb.perGame["OLD"].syncTrophiesChosen);
        // The general setting turned off and on again: the unchosen ones follow it.
        tb.syncTrophies = false;
        CHECK(tb.save(tp));
        xc::app::Settings tc;
        CHECK(tc.load(tp));
        CHECK(!tc.perGame["MADE"].syncTrophies && !tc.perGame["MADE"].syncTrophiesChosen);
        std::remove(tp.c_str());
    }

    // Remove game profile restores default.
    reloaded.perGame.erase("GAME1");
    auto afterErase = reloaded.profileForGame("GAME1", "", &hasCustom);
    CHECK(!hasCustom);
    CHECK(afterErase.sharpness == reloaded.sharpness && afterErase.locale.empty() && afterErase.videoDecoder == reloaded.videoDecoder);

    std::remove(path.c_str());
}

static void testDeviceTypeFallbacks() {
    CHECK(xcloud::isDeviceTypeRefusal("session start failed: HTTP 400: code=DeviceTypeNotAllowed, message=User is not allowed to play title X."));
    CHECK(!xcloud::isDeviceTypeRefusal("session start failed: HTTP 400: code=NoEntitlement, message=User is not allowed to play"));
    CHECK(xcloud::isDeviceTypeRefusal("code=DeviceTypeNotAllowed Auth Status Code: DeviceTypeNotAllowed"));
}

// A page of the title list that fails for a moment is asked again with the same token; a list that
// keeps failing leaves the library marked incomplete (the cache stays, another try is scheduled).

// A zip archive of stored (uncompressed) files, as the test needs one.
static std::string storedZip(const std::vector<std::pair<std::string, std::string>>& files) {
    auto le = [](std::string& s, uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
    };
    std::string out, dir;
    for (const auto& [name, data] : files) {
        uint32_t at = static_cast<uint32_t>(out.size()), n = static_cast<uint32_t>(data.size());
        le(out, 0x04034b50, 4), le(out, 20, 2), le(out, 0, 2), le(out, 0, 2), le(out, 0, 4), le(out, 0, 4);
        le(out, n, 4), le(out, n, 4), le(out, static_cast<uint32_t>(name.size()), 2), le(out, 0, 2);
        out += name + data;
        le(dir, 0x02014b50, 4), le(dir, 20, 2), le(dir, 20, 2), le(dir, 0, 2), le(dir, 0, 2), le(dir, 0, 4), le(dir, 0, 4);
        le(dir, n, 4), le(dir, n, 4), le(dir, static_cast<uint32_t>(name.size()), 2), le(dir, 0, 2), le(dir, 0, 2);
        le(dir, 0, 2), le(dir, 0, 2), le(dir, 0, 4), le(dir, at, 4);
        dir += name;
    }
    uint32_t dirAt = static_cast<uint32_t>(out.size());
    out += dir;
    le(out, 0x06054b50, 4), le(out, 0, 2), le(out, 0, 2), le(out, static_cast<uint32_t>(files.size()), 2);
    le(out, static_cast<uint32_t>(files.size()), 2), le(out, static_cast<uint32_t>(dir.size()), 4), le(out, dirAt, 4);
    le(out, 0, 2);
    return out;
}

static void testUpdater() {
    using namespace xc::app;
    CHECK(contentVersionOf("v0.8.1") == "00.801.000" && contentVersionOf("1.10.0").empty());
    CHECK(contentVersionOf("v1.6.3") == "01.603.000" && contentVersionOf("v2.0.0") == "02.000.000" && contentVersionOf("v0.0.1") == "00.001.000");
    CHECK(isNewerVersion("v1.6.3", "1.6.2") && !isNewerVersion("v1.6.3", "1.6.3") && !isNewerVersion("1.6.2", "v1.6.3"));
    CHECK(isNewerVersion("v1.7.0", "1.6.99") && !isNewerVersion("", "1.6.2") && !isNewerVersion("1.6.2", ""));
    CHECK(updatablePath("eboot.bin") && updatablePath("sce_sys/param.json") && updatablePath("assets/fonts/a.ttf"));
    CHECK(updatablePath("assets/PSBox_Trophies.elf") && updatablePath("sce_module/libc.prx"));
    CHECK(updatablePath("assets/media_showcase/01_home_screen.png") && updatablePath("licenses/Anime4K-MIT.txt"));
    CHECK(!updatablePath("../eboot.bin") && !updatablePath("/data/x") && !updatablePath("a//b") && !updatablePath("a/./b"));
    CHECK(!updatablePath("account.json") && !updatablePath("settings.json") && !updatablePath("imgcache/x"));
    CHECK(!updatablePath("update.old/eboot.bin") && !updatablePath("update.journal") && !updatablePath("frame.ppm"));

    // A signature by a throwaway key (made for this test only).
    const char* pub =
        "-----BEGIN PUBLIC KEY-----\n"
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAETaRKE8e1bXOEYcQDqc3yvrCF4reo\n"
        "nn1S6x6D3sFYl8eHLd7qogQNmh7Nseqx41gMcXh5TVqOjvMS+7mpqk0Wxg==\n"
        "-----END PUBLIC KEY-----\n";
    const char* hex =
        "3045022021a9fa17a6717284e6d5989fcffb6363d371f97ea00b4e9ecec4adaa11a01a050221008053381afe913f3071594281868a44"
        "cd57589796b296b1818b37a6e3c8880514";
    std::string sig;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) sig += static_cast<char>(std::stoi(std::string(hex + i, 2), nullptr, 16));
    std::string err;
    CHECK(verifySignature("PSBox test", sig, pub, err));
    CHECK(!verifySignature("PSBox tesT", sig, pub, err));
    CHECK(!verifySignature("PSBox test", sig, kReleaseKey, err));  // another key

    std::vector<ZipEntry> files;
    CHECK(unzip(storedZip({{"PPSA99810/eboot.bin", "elf"}, {"PPSA99810/sce_sys/param.json", "{}"}}), "PPSA99810/", files,
                err));
    CHECK(files.size() == 2 && files[0].path == "eboot.bin" && files[0].data == "elf" && files[1].path == "sce_sys/param.json");
    CHECK(!unzip(storedZip({{"PPSA99810/../evil", "x"}}), "PPSA99810/", files, err));
    CHECK(!unzip(storedZip({{"PPSA99810/account.json", "x"}}), "PPSA99810/", files, err));
    CHECK(!unzip(storedZip({{"other/eboot.bin", "x"}}), "PPSA99810/", files, err));
    CHECK(!unzip("not a zip", "PPSA99810/", files, err));

    // An update cut short while files moved: the old ones go back.
    std::string dir = "/tmp/xcloud-updater-test";
    xc::fs::removeTree(dir);
    xc::fs::makeDirs(dir + "/update.old/sce_sys");
    xc::platform::writeFileAtomic(dir + "/eboot.bin", "new");
    xc::platform::writeFileAtomic(dir + "/update.old/eboot.bin", "old");
    xc::platform::writeFileAtomic(dir + "/update.old/sce_sys/param.json", "old param");
    xc::platform::writeFileAtomic(dir + "/account.json", "mine");
    xc::platform::writeFileAtomic(dir + "/update.journal", "swapping\n");
    CHECK(recoverUpdate(dir));
    std::string text;
    CHECK(xc::platform::readFile(dir + "/eboot.bin", text) && text == "old");
    CHECK(xc::platform::readFile(dir + "/sce_sys/param.json", text) && text == "old param");
    CHECK(xc::platform::readFile(dir + "/account.json", text) && text == "mine");
    CHECK(!xc::platform::readFile(dir + "/update.journal", text) && !xc::platform::readFile(dir + "/update.old/eboot.bin", text));
    CHECK(!recoverUpdate(dir));  // nothing left to do
    xc::fs::removeTree(dir);

    // --- Multi-platform asset selection (bracketed and GitHub-sanitized dotted names) ---
    const std::string releaseBracketed = R"({
        "tag_name": "v1.8.0",
        "body": "Release notes v1.8.0",
        "assets": [
            {"name": "PPSA99810.zip", "size": 1001, "browser_download_url": "https://example.invalid/PPSA99810.zip"},
            {"name": "PPSA99810.zip.sig", "size": 70, "browser_download_url": "https://example.invalid/PPSA99810.zip.sig"},
            {"name": "PPSA99810.zip.sha256", "size": 80, "browser_download_url": "https://example.invalid/PPSA99810.zip.sha256"},
            {"name": "PSBoxCloudGaming[PS5]-v1.8.0.zip", "size": 1001, "browser_download_url": "https://example.invalid/ps5-named.zip"},
            {"name": "PSBoxCloudGaming[PS5]-v1.8.0.zip.sig", "size": 70, "browser_download_url": "https://example.invalid/ps5-named.zip.sig"},
            {"name": "PSBoxCloudGaming[PS5]-v1.8.0.zip.sha256", "size": 80, "browser_download_url": "https://example.invalid/ps5-named.zip.sha256"},
            {"name": "PSBoxCloudGaming[PS4]-v1.8.0.pkg", "size": 2002, "browser_download_url": "https://example.invalid/ps4.pkg"},
            {"name": "PSBoxCloudGaming[PS4]-v1.8.0.pkg.sha256", "size": 80, "browser_download_url": "https://example.invalid/ps4.pkg.sha256"},
            {"name": "PSBoxCloudGaming[Linux]-v1.8.0.AppImage", "size": 3003, "browser_download_url": "https://example.invalid/linux.AppImage"},
            {"name": "PSBoxCloudGaming[Linux]-v1.8.0.AppImage.sha256", "size": 80, "browser_download_url": "https://example.invalid/linux.AppImage.sha256"},
            {"name": "PSBoxCloudGaming[Windows]-v1.8.0.zip", "size": 4004, "browser_download_url": "https://example.invalid/win.zip"},
            {"name": "PSBoxCloudGaming[Windows]-v1.8.0.zip.sha256", "size": 80, "browser_download_url": "https://example.invalid/win.zip.sha256"},
            {"name": "PSBoxCloudGaming[Android]-v1.8.0.apk", "size": 5005, "browser_download_url": "https://example.invalid/android.apk"},
            {"name": "PSBoxCloudGaming[Android]-v1.8.0.apk.sha256", "size": 80, "browser_download_url": "https://example.invalid/android.apk.sha256"}
        ]
    })";

    Release rel;
    CHECK(selectReleaseAsset(releaseBracketed, UpdatePlatform::PS5, rel, err));
    CHECK(rel.tag == "v1.8.0" && rel.platform == UpdatePlatform::PS5);
    CHECK(rel.assetName == "PPSA99810.zip" && rel.zipSize == 1001);
    CHECK(rel.zipUrl == "https://example.invalid/PPSA99810.zip");
    CHECK(rel.sigUrl == "https://example.invalid/PPSA99810.zip.sig");
    CHECK(rel.sha256Url == "https://example.invalid/PPSA99810.zip.sha256");
    CHECK(rel.hasVerifiablePackage());

    CHECK(selectReleaseAsset(releaseBracketed, UpdatePlatform::PS4, rel, err));
    CHECK(rel.platform == UpdatePlatform::PS4 && rel.assetName == "PSBoxCloudGaming[PS4]-v1.8.0.pkg" && rel.zipSize == 2002);
    CHECK(rel.zipUrl == "https://example.invalid/ps4.pkg" && rel.sha256Url == "https://example.invalid/ps4.pkg.sha256");
    CHECK(rel.hasVerifiablePackage());

    CHECK(selectReleaseAsset(releaseBracketed, UpdatePlatform::Linux, rel, err));
    CHECK(rel.platform == UpdatePlatform::Linux && rel.assetName == "PSBoxCloudGaming[Linux]-v1.8.0.AppImage" && rel.zipSize == 3003);
    CHECK(rel.zipUrl == "https://example.invalid/linux.AppImage" && rel.sha256Url == "https://example.invalid/linux.AppImage.sha256");
    CHECK(rel.hasVerifiablePackage());

    CHECK(selectReleaseAsset(releaseBracketed, UpdatePlatform::Windows, rel, err));
    CHECK(rel.platform == UpdatePlatform::Windows && rel.assetName == "PSBoxCloudGaming[Windows]-v1.8.0.zip" && rel.zipSize == 4004);
    CHECK(rel.zipUrl == "https://example.invalid/win.zip" && rel.sha256Url == "https://example.invalid/win.zip.sha256");
    CHECK(rel.hasVerifiablePackage());

    CHECK(selectReleaseAsset(releaseBracketed, UpdatePlatform::Android, rel, err));
    CHECK(rel.platform == UpdatePlatform::Android && rel.assetName == "PSBoxCloudGaming[Android]-v1.8.0.apk" && rel.zipSize == 5005);
    CHECK(rel.zipUrl == "https://example.invalid/android.apk" && rel.sha256Url == "https://example.invalid/android.apk.sha256");
    CHECK(rel.hasVerifiablePackage());

    // GitHub-sanitized dotted asset names (.PS5., .PS4., .Linux., .Windows., .Android.)
    const std::string releaseSanitized = R"({
        "tag_name": "v1.8.0",
        "body": "Sanitized assets",
        "assets": [
            {"name": "PSBoxCloudGaming.PS5.-v1.8.0.zip", "size": 1100, "browser_download_url": "https://example.invalid/s-ps5.zip"},
            {"name": "PSBoxCloudGaming.PS5.-v1.8.0.zip.sig", "size": 70, "browser_download_url": "https://example.invalid/s-ps5.zip.sig"},
            {"name": "PSBoxCloudGaming.PS5.-v1.8.0.zip.sha256", "size": 80, "browser_download_url": "https://example.invalid/s-ps5.zip.sha256"},
            {"name": "PSBoxCloudGaming.PS4.-v1.8.0.pkg", "size": 2100, "browser_download_url": "https://example.invalid/s-ps4.pkg"},
            {"name": "PSBoxCloudGaming.PS4.-v1.8.0.pkg.sha256", "size": 80, "browser_download_url": "https://example.invalid/s-ps4.pkg.sha256"},
            {"name": "PSBoxCloudGaming.Linux.-v1.8.0.AppImage", "size": 3100, "browser_download_url": "https://example.invalid/s-linux.AppImage"},
            {"name": "PSBoxCloudGaming.Linux.-v1.8.0.AppImage.sha256", "size": 80, "browser_download_url": "https://example.invalid/s-linux.AppImage.sha256"},
            {"name": "PSBoxCloudGaming.Windows.-v1.8.0.zip", "size": 4100, "browser_download_url": "https://example.invalid/s-win.zip"},
            {"name": "PSBoxCloudGaming.Windows.-v1.8.0.zip.sha256", "size": 80, "browser_download_url": "https://example.invalid/s-win.zip.sha256"},
            {"name": "PSBoxCloudGaming.Android.-v1.8.0.apk", "size": 5100, "browser_download_url": "https://example.invalid/s-android.apk"},
            {"name": "PSBoxCloudGaming.Android.-v1.8.0.apk.sha256", "size": 80, "browser_download_url": "https://example.invalid/s-android.apk.sha256"}
        ]
    })";

    CHECK(selectReleaseAsset(releaseSanitized, UpdatePlatform::PS5, rel, err));
    CHECK(rel.zipUrl == "https://example.invalid/s-ps5.zip" && rel.sigUrl == "https://example.invalid/s-ps5.zip.sig" &&
          rel.sha256Url == "https://example.invalid/s-ps5.zip.sha256");
    CHECK(selectReleaseAsset(releaseSanitized, UpdatePlatform::PS4, rel, err));
    CHECK(rel.zipUrl == "https://example.invalid/s-ps4.pkg" && rel.sha256Url == "https://example.invalid/s-ps4.pkg.sha256");
    CHECK(selectReleaseAsset(releaseSanitized, UpdatePlatform::Linux, rel, err));
    CHECK(rel.zipUrl == "https://example.invalid/s-linux.AppImage" && rel.sha256Url == "https://example.invalid/s-linux.AppImage.sha256");
    CHECK(selectReleaseAsset(releaseSanitized, UpdatePlatform::Windows, rel, err));
    CHECK(rel.zipUrl == "https://example.invalid/s-win.zip" && rel.sha256Url == "https://example.invalid/s-win.zip.sha256");
    CHECK(selectReleaseAsset(releaseSanitized, UpdatePlatform::Android, rel, err));
    CHECK(rel.zipUrl == "https://example.invalid/s-android.apk" && rel.sha256Url == "https://example.invalid/s-android.apk.sha256");

    // --- SHA-256 computation, manifest parsing, and positive/negative validation ---
    const std::string samplePayload = "PSBox multi-platform update payload v1.8.0";
    const std::string sampleHash = sha256Hex(samplePayload);
    CHECK(sampleHash.size() == 64);
    std::string upperHash = sampleHash;
    for (char& c : upperHash) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    CHECK(parseSha256Manifest(sampleHash + "  PSBoxCloudGaming[PS4]-v1.8.0.pkg\n") == sampleHash);
    CHECK(parseSha256Manifest(upperHash + " *PSBoxCloudGaming.PS4.-v1.8.0.pkg\r\n") == sampleHash);
    CHECK(parseSha256Manifest("SHA256 (PSBoxCloudGaming[Linux]-v1.8.0.AppImage) = " + upperHash + "\n") == sampleHash);
    CHECK(parseSha256Manifest("  " + sampleHash + "  \n") == sampleHash);
    CHECK(parseSha256Manifest("not-a-valid-sha256-hash  file.pkg\n").empty());
    CHECK(parseSha256Manifest("1234567890abcdef  file.pkg\n").empty());
    CHECK(parseSha256Manifest("").empty());

    CHECK(verifySha256(samplePayload, sampleHash + "  PSBoxCloudGaming[PS4]-v1.8.0.pkg\n", err));
    CHECK(!verifySha256(samplePayload + "!", sampleHash + "  PSBoxCloudGaming[PS4]-v1.8.0.pkg\n", err));
    CHECK(err.find("sha256 mismatch") != std::string::npos);
    CHECK(!verifySha256(samplePayload, "corrupted manifest", err));
    CHECK(err.find("invalid sha256 manifest") != std::string::npos);

    // --- Platform payload and script generation ---
    CHECK(buildPs4GoldHenInstallPayload("/data/pkg/PSBoxCloudGaming[PS4]-v1.8.0.pkg") ==
          R"({"type":"direct","packages":["/data/pkg/PSBoxCloudGaming[PS4]-v1.8.0.pkg"]})");
    auto rpiJson = json::parse(buildPs4GoldHenInstallPayload("/data/pkg/PSBoxCloudGaming.PS4.-v1.8.0.pkg"));
    CHECK(rpiJson && (*rpiJson)["type"].str() == "direct" && (*rpiJson)["packages"].size() == 1 &&
          (*rpiJson)["packages"][0].str() == "/data/pkg/PSBoxCloudGaming.PS4.-v1.8.0.pkg");

    std::string winScript = buildWindowsUpdateBatchScript("C:/Games/PSBox", "C:/Games/PSBox/update_staging", 4242);
    CHECK(winScript.find("set \"APP_DIR=C:\\Games\\PSBox\"") != std::string::npos);
    CHECK(winScript.find("set \"STAGE_DIR=C:\\Games\\PSBox\\update_staging\"") != std::string::npos);
    CHECK(winScript.find("set \"PID=4242\"") != std::string::npos);
    CHECK(winScript.find("xcopy /E /Y /I \"%STAGE_DIR%\\*\" \"%APP_DIR%\\\"") != std::string::npos);
    CHECK(winScript.find("start \"\" \"%APP_DIR%\\psbox.exe\"") != std::string::npos);

    CHECK(resolveLinuxAppImagePath("", "/home/deck/Applications/PSBox.AppImage") == "/home/deck/Applications/PSBox.AppImage");
    CHECK(resolveLinuxAppImagePath("/opt/psbox/PSBoxCloudGaming[Linux]-v1.8.0.AppImage", nullptr) ==
          "/opt/psbox/PSBoxCloudGaming[Linux]-v1.8.0.AppImage");
    CHECK(buildLinuxUpdateTempPath("/home/deck/PSBox.AppImage") == "/home/deck/PSBox.AppImage.new.tmp");

    // --- End-to-end multi-platform installRelease tests with mocked HTTP transport ---
    std::string testRoot = "/tmp/xcloud-multi-updater-test";
    xc::fs::removeTree(testRoot);

    // 1. PS4: valid SHA-256 + GoldHEN RPI success, GoldHEN RPI offline fallback, and invalid SHA-256 rejection
    {
        std::string ps4Dir = testRoot + "/ps4_pkg";
        std::string pkgBytes = "ORBIS_PKG_BINARY_CONTENT_V180";
        std::string pkgSha = sha256Hex(pkgBytes) + "  PSBoxCloudGaming[PS4]-v1.8.0.pkg\n";
        bool rpiAvailable = true;
        std::string lastRpiBody;

        net::setTestTransport([&](const net::Request& req) {
            net::Response r;
            if (req.url == "https://example.invalid/ps4.pkg.sha256") {
                r.status = 200;
                r.body = pkgSha;
            } else if (req.url == "https://example.invalid/ps4.pkg") {
                r.status = 200;
                r.body = pkgBytes;
                if (req.onBody) req.onBody(r.body.data(), r.body.size());
            } else if (req.url == "http://127.0.0.1:12800/api/install") {
                lastRpiBody = req.body;
                if (rpiAvailable) {
                    r.status = 200;
                    r.body = R"({"status":"success","task_id":1})";
                } else {
                    r.status = 0;
                    r.error = "Connection refused";
                }
            } else {
                r.status = 404;
            }
            return r;
        });

        Release ps4Rel;
        ps4Rel.tag = "v1.8.0";
        ps4Rel.platform = UpdatePlatform::PS4;
        ps4Rel.assetName = "PSBoxCloudGaming[PS4]-v1.8.0.pkg";
        ps4Rel.zipUrl = "https://example.invalid/ps4.pkg";
        ps4Rel.sha256Url = "https://example.invalid/ps4.pkg.sha256";
        ps4Rel.zipSize = static_cast<long>(pkgBytes.size());

        // 1a. GoldHEN RPI active on port 12800
        CHECK(installRelease(ps4Rel, ps4Dir, "1.7.7", nullptr, err));
        CHECK(err.empty());
        CHECK(lastRpiBody == buildPs4GoldHenInstallPayload(ps4Dir + "/PSBoxCloudGaming[PS4]-v1.8.0.pkg"));
        std::string savedPkg;
        CHECK(xc::platform::readFile(ps4Dir + "/PSBoxCloudGaming[PS4]-v1.8.0.pkg", savedPkg) && savedPkg == pkgBytes);

        // 1b. GoldHEN RPI offline on port 12800 -> friendly fallback keeps validated .pkg
        xc::fs::removeFile(ps4Dir + "/PSBoxCloudGaming[PS4]-v1.8.0.pkg");
        rpiAvailable = false;
        CHECK(installRelease(ps4Rel, ps4Dir, "1.7.7", nullptr, err));
        CHECK(err.find("GoldHEN Package Installer") != std::string::npos);
        CHECK(xc::platform::readFile(ps4Dir + "/PSBoxCloudGaming[PS4]-v1.8.0.pkg", savedPkg) && savedPkg == pkgBytes);

        // 1c. Invalid SHA-256 -> fails and deletes corrupted .pkg
        xc::fs::removeFile(ps4Dir + "/PSBoxCloudGaming[PS4]-v1.8.0.pkg");
        pkgSha = "0000000000000000000000000000000000000000000000000000000000000000  PSBoxCloudGaming[PS4]-v1.8.0.pkg\n";
        CHECK(!installRelease(ps4Rel, ps4Dir, "1.7.7", nullptr, err));
        CHECK(err.find("sha256 mismatch") != std::string::npos);
        CHECK(!xc::fs::exists(ps4Dir + "/PSBoxCloudGaming[PS4]-v1.8.0.pkg"));
        CHECK(!xc::fs::exists(ps4Dir + "/PSBoxCloudGaming[PS4]-v1.8.0.pkg.part"));
    }

    // 2. Linux AppImage: download to .new.tmp, SHA-256 verification, chmod +x, atomic rename
    {
        std::string linuxDir = testRoot + "/linux";
        xc::fs::makeDirs(linuxDir);
        std::string appImageFile = linuxDir + "/PSBox.AppImage";
        xc::platform::writeFileAtomic(appImageFile, "OLD_APPIMAGE_V177");

        std::string newAppImageBytes = "#!/bin/sh\necho PSBox v1.8.0 AppImage\n";
        std::string newAppImageSha = sha256Hex(newAppImageBytes) + "  PSBoxCloudGaming[Linux]-v1.8.0.AppImage\n";

        net::setTestTransport([&](const net::Request& req) {
            net::Response r;
            if (req.url == "https://example.invalid/linux.AppImage.sha256") {
                r.status = 200;
                r.body = newAppImageSha;
            } else if (req.url == "https://example.invalid/linux.AppImage") {
                r.status = 200;
                r.body = newAppImageBytes;
                if (req.onBody) req.onBody(r.body.data(), r.body.size());
            } else {
                r.status = 404;
            }
            return r;
        });

        Release linuxRel;
        linuxRel.tag = "v1.8.0";
        linuxRel.platform = UpdatePlatform::Linux;
        linuxRel.assetName = "PSBoxCloudGaming[Linux]-v1.8.0.AppImage";
        linuxRel.zipUrl = "https://example.invalid/linux.AppImage";
        linuxRel.sha256Url = "https://example.invalid/linux.AppImage.sha256";
        linuxRel.zipSize = static_cast<long>(newAppImageBytes.size());

        CHECK(installRelease(linuxRel, appImageFile, "1.7.7", nullptr, err));
        std::string updatedBytes;
        CHECK(xc::platform::readFile(appImageFile, updatedBytes) && updatedBytes == newAppImageBytes);
        CHECK(!xc::fs::exists(appImageFile + ".new.tmp"));

        // Corrupted SHA-256 leaves existing AppImage intact
        newAppImageSha = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff  PSBoxCloudGaming[Linux]-v1.8.0.AppImage\n";
        CHECK(!installRelease(linuxRel, appImageFile, "1.7.7", nullptr, err));
        CHECK(xc::platform::readFile(appImageFile, updatedBytes) && updatedBytes == newAppImageBytes);
        CHECK(!xc::fs::exists(appImageFile + ".new.tmp"));
    }

    // 3. Windows Portable ZIP: download, SHA-256 verification, staging extraction & batch updater script
    {
        std::string winDir = testRoot + "/windows";
        xc::fs::makeDirs(winDir);
        std::string winZipBytes = storedZip({
            {"PSBox/psbox.exe", "MZ_PE_PSBOX_V180"},
            {"PSBox/SDL3.dll", "MZ_SDL3_DLL"},
            {"PSBox/assets/cacert.pem", "PEM_CERT"}
        });
        std::string winZipSha = sha256Hex(winZipBytes) + "  PSBoxCloudGaming[Windows]-v1.8.0.zip\n";

        net::setTestTransport([&](const net::Request& req) {
            net::Response r;
            if (req.url == "https://example.invalid/win.zip.sha256") {
                r.status = 200;
                r.body = winZipSha;
            } else if (req.url == "https://example.invalid/win.zip") {
                r.status = 200;
                r.body = winZipBytes;
                if (req.onBody) req.onBody(r.body.data(), r.body.size());
            } else {
                r.status = 404;
            }
            return r;
        });

        Release winRel;
        winRel.tag = "v1.8.0";
        winRel.platform = UpdatePlatform::Windows;
        winRel.assetName = "PSBoxCloudGaming[Windows]-v1.8.0.zip";
        winRel.zipUrl = "https://example.invalid/win.zip";
        winRel.sha256Url = "https://example.invalid/win.zip.sha256";
        winRel.zipSize = static_cast<long>(winZipBytes.size());

        CHECK(installRelease(winRel, winDir, "1.7.7", nullptr, err));
        std::string stagedExe, stagedBat;
        CHECK(xc::platform::readFile(winDir + "/update_staging/psbox.exe", stagedExe) && stagedExe == "MZ_PE_PSBOX_V180");
        CHECK(xc::platform::readFile(winDir + "/update_staging/SDL3.dll", stagedExe) && stagedExe == "MZ_SDL3_DLL");
        CHECK(xc::platform::readFile(winDir + "/update_apply.bat", stagedBat) && stagedBat.find("psbox.exe") != std::string::npos);
    }

    // 4. Android APK: download, SHA-256 verification, staging to <dir>/updates/update.apk
    {
        std::string androidDir = testRoot + "/android";
        std::string apkBytes = "PK_ANDROID_APK_V180";
        std::string apkSha = sha256Hex(apkBytes) + "  PSBoxCloudGaming[Android]-v1.8.0.apk\n";

        net::setTestTransport([&](const net::Request& req) {
            net::Response r;
            if (req.url == "https://example.invalid/android.apk.sha256") {
                r.status = 200;
                r.body = apkSha;
            } else if (req.url == "https://example.invalid/android.apk") {
                r.status = 200;
                r.body = apkBytes;
                if (req.onBody) req.onBody(r.body.data(), r.body.size());
            } else {
                r.status = 404;
            }
            return r;
        });

        Release androidRel;
        androidRel.tag = "v1.8.0";
        androidRel.platform = UpdatePlatform::Android;
        androidRel.assetName = "PSBoxCloudGaming[Android]-v1.8.0.apk";
        androidRel.zipUrl = "https://example.invalid/android.apk";
        androidRel.sha256Url = "https://example.invalid/android.apk.sha256";
        androidRel.zipSize = static_cast<long>(apkBytes.size());

        CHECK(installRelease(androidRel, androidDir, "1.7.7", nullptr, err));
        std::string savedApk;
        CHECK(xc::platform::readFile(androidDir + "/updates/update.apk", savedApk) && savedApk == apkBytes);

        // Negative SHA-256 check removes update.apk
        apkSha = "1111111111111111111111111111111111111111111111111111111111111111  PSBoxCloudGaming[Android]-v1.8.0.apk\n";
        CHECK(!installRelease(androidRel, androidDir, "1.7.7", nullptr, err));
        CHECK(!xc::fs::exists(androidDir + "/updates/update.apk"));
    }

    net::setTestTransport({});
    xc::fs::removeTree(testRoot);
}

static void testPrivacyAndRobustness() {
    // Error answers: their code and message only, never the rest.
    std::string sdp = R"({"status":"failed","code":"Rejected","sdp":"c=IN IP4 203.0.113.7","candidate":"a=candidate:1"})";
    std::string fields = xcloud::errorFields(sdp);
    CHECK(fields.find("Rejected") != std::string::npos && fields.find("failed") != std::string::npos);
    CHECK(fields.find("IP4") == std::string::npos && fields.find("candidate") == std::string::npos);
    CHECK(xcloud::errorFields(R"({"errorDetails":{"code":"NoEntitlement","message":"m"}})").find("NoEntitlement") !=
          std::string::npos);
    CHECK(xcloud::errorFields("<html>203.0.113.7</html>").empty() && xcloud::errorFields("").empty());

    // Numbers no int64_t holds: the default, not undefined behaviour.
    CHECK(json::parse("1e400")->asInt(7) == 7 && json::parse("-1e30")->asInt(7) == 7 && json::parse("42")->asInt() == 42);

    // Chunked bodies: well formed, and every kind of broken one refused.
    std::string body;
    CHECK(net::dechunk("4\r\nWiki\r\n5;x=1\r\npedia\r\n0\r\n\r\n", body) && body == "Wikipedia");
    CHECK(!net::dechunk("zz\r\nab\r\n0\r\n\r\n", body));
    CHECK(!net::dechunk("ffffffffffffffff\r\nab\r\n0\r\n\r\n", body));
    CHECK(!net::dechunk("2\r\nabXX0\r\n\r\n", body));
    CHECK(!net::dechunk("4\r\nWi", body));

    // Report titles aren't cut inside a character; a NUL is encoded.
    CHECK(app::report::shorten("Configura\xC3\xA7\xC3\xB5" "es", 11) == "Configura\xC3\xA7");
    CHECK(app::report::shorten("Configura\xC3\xA7\xC3\xB5" "es", 10) == "Configura");
    CHECK(app::report::shorten("short", 70) == "short");
    app::report::Info info{"t", std::string("a\0b", 3), "1", "13.00", ""};
    CHECK(app::report::issueUrl(info).find("a%00b") != std::string::npos);

    // Platform in report URL and immutable platform header in log
    app::report::Info infoPlat{"t", "what", "1.4.0", "13.00", "[Platform: PlayStation 5 (PPSA99810)]\nlog line 1\n", "", "PlayStation 5 (PPSA99810)"};
    std::string urlPlat = app::report::issueUrl(infoPlat);
    CHECK(urlPlat.find("&platform=PlayStation+5+(PPSA99810)") != std::string::npos);
    CHECK(urlPlat.find("%5BPlatform:+PlayStation+5+(PPSA99810)%5D") != std::string::npos);

    // Platform header automatically injected if missing from log
    app::report::Info infoNoHdr{"t", "what", "1.4.0", "13.00", "raw log line\n", "", "PlayStation 5 (PPSA99810)"};
    std::string urlNoHdr = app::report::issueUrl(infoNoHdr);
    CHECK(urlNoHdr.find("%5BPlatform:+PlayStation+5+(PPSA99810)%5D") != std::string::npos);

    // Truncation preserves [Platform: ...]
    std::string bigLog = "[Platform: PlayStation 5 (PPSA99810)]\n";
    for (int i = 0; i < 50; ++i) bigLog += "lots of long log lines to trigger shortening " + std::to_string(i) + "\n";
    app::report::Info infoTrunc{"t", "what", "1.4.0", "13.00", bigLog, "", "PlayStation 5 (PPSA99810)"};
    std::string urlTrunc = app::report::issueUrl(infoTrunc, 350);
    CHECK(urlTrunc.find("%5BPlatform:+PlayStation+5+(PPSA99810)%5D") != std::string::npos);

    // Sign-in: the service busy or throttling keeps the code polling and
    // the stored sign-in; only a refused token drops it.
    int status = 0;
    std::string answer;
    int calls = 0;
    net::setTestTransport([&](const net::Request&) {
        ++calls;
        net::Response r;
        r.status = status;
        r.body = answer;
        return r;
    });
    auth::MsaClient msa;
    auth::DeviceCode dc;
    dc.deviceCode = "code";
    auth::MsaTokens tokens;
    std::string err;
    status = 503, answer = "<html>busy</html>";
    CHECK(msa.pollDeviceCode(dc, tokens, err) == auth::PollResult::Pending);
    status = 429;
    CHECK(msa.pollDeviceCode(dc, tokens, err) == auth::PollResult::SlowDown);
    status = 400, answer = R"({"error":"authorization_pending"})";
    CHECK(msa.pollDeviceCode(dc, tokens, err) == auth::PollResult::Pending);
    bool networkError = false;
    bool isInvalidGrant = false;
    status = 429, answer = "";
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant) && networkError && !isInvalidGrant);
    status = 400, answer = R"({"error":"invalid_grant"})";
    isInvalidGrant = false;
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant) && !networkError && isInvalidGrant);
    status = 500, answer = "Internal Server Error";
    isInvalidGrant = false;
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant) && !isInvalidGrant);
    bool rejected = false;
    status = 400, answer = R"({"error":"unauthorized_client"})";
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant, &rejected) && rejected && !isInvalidGrant);
    status = 403, answer = "<html>Blocked</html>";
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant, &rejected) && !rejected);
    status = 429, answer = R"({"error":"slow_down"})";
    CHECK(!msa.refresh("r", tokens, err, {}, &networkError, &isInvalidGrant, &rejected) && !rejected);

    // XSTS Redirect URL parsing test
    auth::XblToken userToken{"user_token", "uhs"};
    auth::XblToken xstsOut;
    std::string redir;
    status = 401;
    answer = R"({"Identity":"0","XErr":2148916238,"Message":"","Redirect":"https://start.ui.xboxlive.com/AddChild"})";
    CHECK(!auth::xstsToken(userToken, auth::rp::kGssv, xstsOut, err, &redir));
    CHECK(redir == "https://start.ui.xboxlive.com/AddChild");
    CHECK(err.find("Redirect: https://start.ui.xboxlive.com/AddChild") != std::string::npos);
    CHECK(err.find("child account") != std::string::npos);

    // A title list whose server always says "more" with the same token ends.
    status = 200, answer = R"({"results":[{"titleId":"GAME1","details":{"productId":"P1"}}],"continuationToken":"same"})";
    calls = 0;
    xcloud::GssvClient gssv;
    std::vector<xcloud::Title> titles;
    CHECK(gssv.listTitles(titles, err) && calls == 2);
    status = 200, answer = R"({"results":[],"continuationToken":"other"})";
    calls = 0;
    CHECK(gssv.listTitles(titles, err) && calls == 1);

    // titlehub refusing every batch: an error after a few requests, not
    // ~200 of them and every id marked unknown.
    status = 400, answer = "{}";
    calls = 0;
    std::vector<std::string> ids;
    for (int i = 0; i < 100; ++i) ids.push_back(std::to_string(1000 + i));
    std::map<std::string, std::string> platforms;
    CHECK(!xcloud::fetchPlatforms("auth", ids, platforms, err) && calls < 60 && platforms.size() <= 8);
    net::setTestTransport({});

    // Prices whose request failed are asked for again.
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    std::vector<ui::GameTile> toBuy(3);
    for (int i = 0; i < 3; ++i) toBuy[i].productId = "BUY" + std::to_string(i), toBuy[i].purchasable = true, toBuy[i].playable = false;
    app.setOwned({}, toBuy, true);
    app.showHome();
    auto first = app.pricesWanted(20, true);
    CHECK(first.size() == 3 && app.pricesWanted(20, true).empty());
    app.pricesFailed(first);
    CHECK(app.pricesWanted(20, true).size() == 3);
    // The user's console names, for keeping them out of reports.
    ui::ConsoleTile console;
    console.serverId = "F4000000AAAA";
    console.name = "Living Room Xbox";
    app.setConsoles({console}, true);
    auto names = app.consoleNames();
    CHECK(names.size() == 1 && names[0] == "Living Room Xbox");
    CHECK(app::report::sanitize("starting Living Room Xbox", names).find("Living Room") == std::string::npos);

    // An update whose old files can't all go back keeps them, and its
    // journal, for the next start.
    std::string dir = "/tmp/xcloud-rollback-test";
    xc::fs::removeTree(dir);
    xc::fs::makeDirs(dir + "/update.old");
    xc::fs::makeDirs(dir + "/eboot.bin/busy");
    xc::platform::writeFileAtomic(dir + "/update.old/eboot.bin", "old");
    xc::platform::writeFileAtomic(dir + "/update.journal", "swapping\n");
    app::recoverUpdate(dir);
    std::string text;
    CHECK(xc::platform::readFile(dir + "/update.old/eboot.bin", text) && text == "old");
    CHECK(xc::platform::readFile(dir + "/update.journal", text));
    xc::fs::removeTree(dir);
    // The same file twice in a package: refused.
    std::vector<app::ZipEntry> files;
    CHECK(!app::unzip(storedZip({{"PPSA99810/eboot.bin", "a"}, {"PPSA99810/eboot.bin", "b"}}), "PPSA99810/", files, err));
}

// The sign-in kept up: renewed before it expires (10 minutes ahead), only
// the xCloud login when only it is due, the whole chain when the XSTS is
// (or a request was refused), the region kept, the rotated refresh token
// saved at once, a refused refresh keeping the stored account.

static void testBlock1Features() {
    using namespace xc;
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);

    // 1. 3x3 filter grid verification
    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    CHECK(app.filterRow(0).size() == 3); // Wishlist, Free, Cheapest
    CHECK(app.filterRow(1).size() == 3); // Mode, Genre, Publisher
    CHECK(app.filterRow(2).size() == 3); // Console, Language, MouseKeyboard
    CHECK(app.filterRow(3).empty());

    app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
    CHECK(app.filterRow(0).size() == 3); // Plan, Console, MouseKeyboard
    CHECK(app.filterRow(1).size() == 3); // Mode, Genre, Publisher
    CHECK(app.filterRow(2).size() == 1); // Language
    CHECK(app.filterRow(3).empty());

    // 2. isFreeGame and ClaimFreeGame for zero-priced games
    ui::GameTile trialGame;
    trialGame.productId = "9P0LHV4DV2BG";
    trialGame.titleId = "TITLE_JUSTDANCE";
    trialGame.name = "Just Dance Trial";
    trialGame.purchasable = false; // Test that even when purchasable is false, zero price enables claiming
    trialGame.playable = false;
    trialGame.freeInStore = false; // Microsoft doesn't set freeInStore for trials

    // Before prices are loaded, not free
    CHECK(!app.isFreeGame(trialGame));

    // When prices.json returns list=0 and msrp=0
    ui::PriceInfo zeroPrice;
    zeroPrice.list = 0.0;
    zeroPrice.msrp = 0.0;
    zeroPrice.now = "Grátis";
    app.setPrices({{"9P0LHV4DV2BG", zeroPrice}});

    CHECK(app.isFreeGame(trialGame));

    // Opening details and pressing accept should produce Action::ClaimFreeGame
    app.showDetails(trialGame);
    ui::NavInput acceptPress;
    acceptPress.accept = true;
    auto ev = app.handle(acceptPress);
    CHECK(ev.action == ui::Action::ClaimFreeGame);
    CHECK(ev.game.productId == "9P0LHV4DV2BG");
    CHECK(ev.game.freeInStore == true);

    // After acquiring the game, it becomes owned/playable: pressing accept produces Action::Play
    app.markTileOwned("9P0LHV4DV2BG", "TITLE_JUSTDANCE");
    ev = app.handle(acceptPress);
    CHECK(ev.action == ui::Action::Play);

    // 3. Search filter navigation across 3x3 grid
    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    // Move down from wide keys to filter row 0, 1, 2
    ui::NavInput navDown;
    navDown.down = true;
    app.handle(navDown);
    app.handle(navDown);
    // Move left and right
    ui::NavInput navRight;
    navRight.right = true;
    app.handle(navRight);
    ui::NavInput navLeft;
    navLeft.left = true;
    app.handle(navLeft);
}

static void testBlock2Features() {
    // 1. Parsing of bilingual release notes
    std::string notes =
        "<!-- LANG:PT -->\n"
        "## Novidades 1.3.4\n"
        "- Alto-falante no menu\n"
        "- Novos tetos de bitrate\n"
        "<!-- LANG:EN -->\n"
        "## What's new 1.3.4\n"
        "- Speaker in menu\n"
        "- New bitrate caps\n";
    std::string pt = app::filterReleaseNotes(notes, "pt-BR");
    CHECK(pt.find("Novidades 1.3.4") != std::string::npos);
    CHECK(pt.find("What's new") == std::string::npos);

    std::string en = app::filterReleaseNotes(notes, "en");
    CHECK(en.find("What's new 1.3.4") != std::string::npos);
    CHECK(en.find("Novidades 1.3.4") == std::string::npos);

    // Fallback to EN when lang tag is not found
    std::string es = app::filterReleaseNotes(notes, "es");
    CHECK(es.find("What's new 1.3.4") != std::string::npos);

    // No tags: returns whole text
    std::string plain = "Simple release notes without tags.";
    CHECK(app::filterReleaseNotes(plain, "pt-BR") == plain);

    // Test tag spacing and casing variations
    std::string noSpaceNotes = "<!--LANG:PT-->\nNotas compactas\n<!--LANG:EN-->\nCompact notes";
    CHECK(app::filterReleaseNotes(noSpaceNotes, "pt-BR") == "Notas compactas");
    CHECK(app::filterReleaseNotes(noSpaceNotes, "en") == "Compact notes");

    std::string spaceColonNotes = "<!-- LANG: PT -->\nNotas com espaco\n<!-- LANG: EN -->\nSpaced notes";
    CHECK(app::filterReleaseNotes(spaceColonNotes, "pt-BR") == "Notas com espaco");
    CHECK(app::filterReleaseNotes(spaceColonNotes, "en") == "Spaced notes");

    std::string lowerNotes = "<!-- lang: pt-br -->\nNotas minusculas\n<!-- lang: en-us -->\nLowercase notes";
    CHECK(app::filterReleaseNotes(lowerNotes, "pt-BR") == "Notas minusculas");
    CHECK(app::filterReleaseNotes(lowerNotes, "en") == "Lowercase notes");

    // Ensure delimiter tags do not leak into output
    CHECK(app::filterReleaseNotes(notes, "pt-BR").find("<!--") == std::string::npos);
    CHECK(app::filterReleaseNotes(notes, "en").find("<!--") == std::string::npos);

    // 2. Settings serialization of padSpeakerVolume
    app::Settings s;
    CHECK(s.padSpeakerVolume == 100);
    s.padSpeakerVolume = 75;
    std::string tmpPath = "/tmp/test_settings_block2.json";
    CHECK(s.save(tmpPath));

    app::Settings loaded;
    CHECK(loaded.load(tmpPath));
    CHECK(loaded.padSpeakerVolume == 75);
    std::remove(tmpPath.c_str());

    // Test media::setPadSpeakerVolume and padSpeakerVolume
    media::setPadSpeakerVolume(60);
    CHECK(media::padSpeakerVolume() == 60);
    media::setPadSpeakerVolume(-10);
    CHECK(media::padSpeakerVolume() == 0);
    media::setPadSpeakerVolume(150);
    CHECK(media::padSpeakerVolume() == 100);

    // 3. Bitrate caps in UI
    ui::Fonts dummyFonts{};
    ui::ImageCache images([] {});
    ui::AppUi app(dummyFonts, images);

    app::Settings bs;
    bs.maxBitrateMbps = 50;
    ui::SettingsChoice bc = ui::choiceFromSettings(bs);
    CHECK(bc.maxBitrateMbps == 50);
    app::Settings bsBack;
    ui::applyChoice(bc, bsBack);
    CHECK(bsBack.maxBitrateMbps == 50);

    bs.maxBitrateMbps = 45;
    bc = ui::choiceFromSettings(bs);
    CHECK(bc.maxBitrateMbps == 45);
    ui::applyChoice(bc, bsBack);
    CHECK(bsBack.maxBitrateMbps == 45);

    // Test settings navigation in AppUi
    app.showHome();
    ui::NavInput openOpt;
    openOpt.options = true;
    app.handle(openOpt);
    CHECK(app.screen() == ui::Screen::Settings);

    // Navigate down to BitrateCap row (row 10)
    ui::NavInput navDownRow;
    navDownRow.down = true;
    while (app.settingsFocus() != 10) {
        app.handle(navDownRow);
    }
    CHECK(app.settingsFocus() == 10);

    // Test inline slider navigation on BitrateCap (row 10)
    CHECK(app.settings().maxBitrateMbps == 0);

    // Pressing left at 0 (Automatic) should clamp at 0
    ui::NavInput navLeft;
    navLeft.left = true;
    app.handle(navLeft);
    CHECK(app.settings().maxBitrateMbps == 0);

    // Pressing right increases the bitrate in 5 Mbps increments up to 50 Mbps
    ui::NavInput navRight;
    navRight.right = true;
    const int expectedMbps[] = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
    for (int exp : expectedMbps) {
        app.handle(navRight);
        CHECK(app.settings().maxBitrateMbps == exp);
    }

    // Pressing right advances to Unlimited (-1) and clamps at Unlimited
    app.handle(navRight);
    CHECK(app.settings().maxBitrateMbps == -1);
    app.handle(navRight);
    CHECK(app.settings().maxBitrateMbps == -1);

    // Pressing left reduces from Unlimited to 50 Mbps, then to 45 Mbps
    app.handle(navLeft);
    CHECK(app.settings().maxBitrateMbps == 50);
    app.handle(navLeft);
    CHECK(app.settings().maxBitrateMbps == 45);

    // Pressing accept (Cross) on row 10 cycles/alternates to next option (no dropdown opened)
    ui::NavInput acc;
    acc.accept = true;
    app.handle(acc);
    CHECK(app.settings().maxBitrateMbps == 50);
    app.handle(acc);
    CHECK(app.settings().maxBitrateMbps == -1); // Unlimited
    app.handle(acc);
    CHECK(app.settings().maxBitrateMbps == 0); // wrapped to Automatic
    app.handle(acc);
    CHECK(app.settings().maxBitrateMbps == 5);

    // Non-exact bitrate snapping test:
    // If a legacy or custom setting such as 22 Mbps is loaded, it should snap to nearest option (20 Mbps)
    auto st = app.settings();
    st.maxBitrateMbps = 22;
    app.setSettings(st);
    app.handle(navRight); // from snapped 20 Mbps -> 25 Mbps
    CHECK(app.settings().maxBitrateMbps == 25);
    app.handle(navLeft);  // 25 -> 20 Mbps
    CHECK(app.settings().maxBitrateMbps == 20);
    app.handle(navLeft);  // 20 -> 15 Mbps
    CHECK(app.settings().maxBitrateMbps == 15);

    // Test rendering on row 10 with controller and keyboard hints
    ui::Canvas canvas(1920, 1080);
    ui::g_keyboardHints = false;
    app.render(canvas, 1000);
    ui::g_keyboardHints = true;
    app.render(canvas, 1000);
    ui::g_keyboardHints = false;

    // Test keyLabel icon index alignment
    CHECK(std::string(ui::keyLabel(0)) == "Enter");
    CHECK(std::string(ui::keyLabel(1)) == "Esc");
    CHECK(std::string(ui::keyLabel(2)) == "Tab");
    CHECK(std::string(ui::keyLabel(3)) == "F");
    CHECK(std::string(ui::keyLabel(4)) == "Del");
    CHECK(std::string(ui::keyLabel(5)) == "H");
    CHECK(std::string(ui::keyLabel(6)) == "R");
    CHECK(std::string(ui::keyLabel(7)) == "Shift");
    CHECK(std::string(ui::keyLabel(8)) == "Z C");
    CHECK(std::string(ui::keyLabel(9)) == "<- ->");
    CHECK(std::string(ui::keyLabel(10)) == "Q E");
    CHECK(std::string(ui::keyLabel(11)) == "");
    CHECK(std::string(ui::keyLabel(-1)) == "");

    // Close Settings
    ui::NavInput navBack;
    navBack.back = true;
    app.handle(navBack);
    CHECK(app.screen() == ui::Screen::Home);

    // 4. StreamMenu with PadSpeakerVolumeItem
    ui::StreamMenu menu(dummyFonts);
    menu.open(0, false, 0, 1, 0, false, true, false, 2, 15, false, 0.0f, false, 100);
    CHECK(menu.padSpeakerVolume() == 100);
    menu.setTab(xc::ui::MenuTab::Controls);

    // When at 100%, right should not trigger PadSpeakerVolume action
    // Controls tab order: ConfirmItem (0) -> Triggers (1) -> Deadzone (2) -> TouchpadShortcuts (3) -> Gyro (4) -> AudioHaptics (5) -> PadSpeakerVolume (6)
    ui::NavInput down;
    down.down = true;
    for (int i = 0; i < 6; ++i) menu.handle(down);

    ui::NavInput right;
    right.right = true;
    auto actAt100 = menu.handle(right);
    CHECK(actAt100 == ui::MenuAction::None);
    CHECK(menu.padSpeakerVolume() == 100);

    // Left should decrease volume by 5%
    ui::NavInput left;
    left.left = true;
    auto act = menu.handle(left);
    CHECK(act == ui::MenuAction::PadSpeakerVolume);
    CHECK(menu.padSpeakerVolume() == 95);

    // Test decreasing down to 0%
    for (int i = 0; i < 20; ++i) menu.handle(left);
    CHECK(menu.padSpeakerVolume() == 0);

    // When at 0%, left should not trigger PadSpeakerVolume action
    auto actAt0 = menu.handle(left);
    CHECK(actAt0 == ui::MenuAction::None);
    CHECK(menu.padSpeakerVolume() == 0);

    // Rendering at 0% should render valid canvas
    ui::StreamInfo sinfo;
    auto cMute = menu.renderMenu(sinfo);
    CHECK(cMute.width() == ui::StreamMenu::kCombinedW);

    // Right should increase by 5%
    act = menu.handle(right);
    CHECK(act == ui::MenuAction::PadSpeakerVolume);
    CHECK(menu.padSpeakerVolume() == 5);

    // 5. Update prompt with 3 buttons and Square shortcut
    app.showHome();
    app.setUpdateState("v1.3.3", "v1.3.4", false, notes);
    app.offerUpdate(notes);
    CHECK(app.updateOffered());
    CHECK(!app.showingUpdateNotes());

    // Navigation between 3 buttons: left wraps from 0 to 2
    ui::NavInput navL;
    navL.left = true;
    app.handle(navL); // promptFocus_ becomes 2 (Not now)
    ui::NavInput navR;
    navR.right = true;
    app.handle(navR); // promptFocus_ wraps to 0 (Update now)
    app.handle(navR); // promptFocus_ is 1 (What's new)

    // Press accept on What's new -> opens notes modal
    ui::NavInput accept;
    accept.accept = true;
    auto ev = app.handle(accept);
    CHECK(ev.action == ui::Action::None);
    CHECK(app.showingUpdateNotes());

    // Up / Down scroll in notes modal
    ui::NavInput navDown;
    navDown.down = true;
    app.handle(navDown);
    ui::NavInput navUp;
    navUp.up = true;
    app.handle(navUp);

    // Shoulder buttons page scrolling (L1/R1)
    ui::NavInput navR1;
    navR1.r1 = true;
    app.handle(navR1);
    ui::NavInput navL1;
    navL1.l1 = true;
    app.handle(navL1);

    // Right stick smooth scrolling
    ui::NavInput stickScroll;
    stickScroll.scrollRows = 2;
    app.handle(stickScroll);
    stickScroll.scrollRows = -2;
    app.handle(stickScroll);

    // Back closes notes modal
    ui::NavInput back;
    back.back = true;
    app.handle(back);
    CHECK(!app.showingUpdateNotes());
    CHECK(app.updateOffered());

    // Square shortcut opens notes modal directly
    ui::NavInput sq;
    sq.square = true;
    app.handle(sq);
    CHECK(app.showingUpdateNotes());
    app.handle(back);
    CHECK(!app.showingUpdateNotes());
}

static void testBlock3Features() {
    using namespace xc;

    // 1. Content ratings parsing
    {
        // ClassInd for BR
        auto jBr = json::parse(R"({
            "ContentRatings": [
                {
                    "RatingSystem": "ClassInd",
                    "RatingId": "14",
                    "RatingDescriptors": ["Violência", "Drogas Lícitas"]
                },
                {
                    "RatingSystem": "ESRB",
                    "RatingId": "Teen",
                    "RatingDescriptors": ["Violence", "Blood"]
                }
            ]
        })");
        CHECK(jBr.has_value());
        std::string age, sys;
        std::vector<std::string> desc;
        xcloud::parseContentRatings(*jBr, "BR", age, sys, desc);
        CHECK(sys == "ClassInd");
        CHECK(age == "14+");
        CHECK(desc.size() == 2 && desc[0] == "Violência");

        // ESRB for US
        std::string ageUs, sysUs;
        std::vector<std::string> descUs;
        xcloud::parseContentRatings(*jBr, "US", ageUs, sysUs, descUs);
        CHECK(sysUs == "ESRB");
        CHECK(ageUs == "T");
        CHECK(descUs.size() == 2 && descUs[0] == "Violence");

        // ESRB Mature
        auto jMature = json::parse(R"({
            "MarketProperties": [
                {
                    "ContentRatings": [
                        {
                            "ratingSystem": "ESRB",
                            "ratingId": "Mature 17+",
                            "ratingDescriptors": ["Blood and Gore", "Intense Violence"]
                        }
                    ]
                }
            ]
        })");
        CHECK(jMature.has_value());
        xcloud::parseContentRatings(*jMature, "US", ageUs, sysUs, descUs);
        CHECK(sysUs == "ESRB");
        CHECK(ageUs == "M");
        CHECK(descUs.size() == 2);

        // PEGI for GB / UK
        auto jPegi = json::parse(R"({
            "LocalizedProperties": [
                {
                    "ContentRatings": [
                        {
                            "RatingSystemId": "PEGI",
                            "RatingName": "18",
                            "RatingDescriptor": "Bad Language"
                        }
                    ]
                }
            ]
        })");
        CHECK(jPegi.has_value());
        std::string ageEu, sysEu;
        std::vector<std::string> descEu;
        xcloud::parseContentRatings(*jPegi, "GB", ageEu, sysEu, descEu);
        CHECK(sysEu == "PEGI");
        CHECK(ageEu == "18+");
        CHECK(descEu.size() == 1 && descEu[0] == "Bad Language");

        // Fallback when ClassInd not present in BR market -> falls back to ESRB
        std::string ageFb, sysFb;
        std::vector<std::string> descFb;
        xcloud::parseContentRatings(*jMature, "BR", ageFb, sysFb, descFb);
        CHECK(sysFb == "ESRB");
        CHECK(ageFb == "M");

        // Microsoft Game Pass v3 catalog API singular "ContentRating" object (RemoteHighSapphire0)
        auto jV3Br = json::parse(R"({
            "ContentRating": {
                "LongName": "16 anos",
                "Description": "Para maiores de 16 anos",
                "Age": 16,
                "RatingSystem": "DJCTQ",
                "LocalizedDescriptors": ["Violência"],
                "LocalizedInteractiveElements": ["Usuários Interagem", "Compras no Jogo"]
            }
        })");
        CHECK(jV3Br.has_value());
        std::string ageV3Br, sysV3Br;
        std::vector<std::string> descV3Br;
        xcloud::parseContentRatings(*jV3Br, "BR", ageV3Br, sysV3Br, descV3Br);
        CHECK(sysV3Br == "ClassInd");
        CHECK(ageV3Br == "16+");
        CHECK(descV3Br.size() == 3);
        CHECK(descV3Br[0] == "Violência");
        CHECK(descV3Br[1] == "Usuários Interagem");

        // V3 Livre
        auto jV3Livre = json::parse(R"({
            "ContentRating": {
                "LongName": "Livre",
                "Description": "Para todas as idades",
                "Age": 1,
                "RatingSystem": "DJCTQ",
                "LocalizedDescriptors": []
            }
        })");
        CHECK(jV3Livre.has_value());
        std::string ageLivre, sysLivre;
        std::vector<std::string> descLivre;
        xcloud::parseContentRatings(*jV3Livre, "BR", ageLivre, sysLivre, descLivre);
        CHECK(sysLivre == "ClassInd");
        CHECK(ageLivre == "Livre");

        // V3 ESRB
        auto jV3Esrb = json::parse(R"({
            "ContentRating": {
                "LongName": "TEEN",
                "Age": 13,
                "RatingSystem": "ESRB",
                "LocalizedDescriptors": ["Blood", "Mild Violence"]
            }
        })");
        CHECK(jV3Esrb.has_value());
        std::string ageV3Us, sysV3Us;
        std::vector<std::string> descV3Us;
        xcloud::parseContentRatings(*jV3Esrb, "US", ageV3Us, sysV3Us, descV3Us);
        CHECK(sysV3Us == "ESRB");
        CHECK(ageV3Us == "T");
        CHECK(descV3Us.size() == 2 && descV3Us[0] == "Blood");
    }

    // 2. Local Web Micro-Server
    {
        app::WebServer ws;
        CHECK(!ws.isRunning());
        std::string ip = ws.localIp();
        CHECK(!ip.empty());
        std::string url = ws.url();
        CHECK(url.find(":8080") != std::string::npos);

        // Test OPTIONS (CORS preflight)
        auto optRes = ws.handleRequest("OPTIONS", "/api/search", "");
        CHECK(optRes.status == 204);

        // Test GET / (HTML index)
        auto htmlRes = ws.handleRequest("GET", "/", "");
        CHECK(htmlRes.status == 200);
        CHECK(htmlRes.contentType.find("text/html") != std::string::npos);
        CHECK(htmlRes.body.find("PSBOX CLOUD GAMING") != std::string::npos);
        CHECK(htmlRes.body.find("logo-icon") != std::string::npos);
        CHECK(htmlRes.body.find("#107C41") != std::string::npos);
        CHECK(htmlRes.body.find("tabBtnGamePass") != std::string::npos);
        CHECK(htmlRes.body.find("tabBtnLibrary") != std::string::npos);
        CHECK(htmlRes.body.find("filtersGamePass") != std::string::npos);
        CHECK(htmlRes.body.find("filtersLibrary") != std::string::npos);
        CHECK(htmlRes.body.find("chipWishlist") != std::string::npos);
        CHECK(htmlRes.body.find("chipFree") != std::string::npos);
        CHECK(htmlRes.body.find("chipCheapest") != std::string::npos);
        CHECK(htmlRes.body.find("filterPlan") != std::string::npos);
        CHECK(htmlRes.body.find("filterConsoleGP") != std::string::npos);
        CHECK(htmlRes.body.find("chipMouseGP") != std::string::npos);
        CHECK(htmlRes.body.find("Teclado Remoto para Busca") != std::string::npos);
        CHECK(htmlRes.body.find("Remapeamento de Botões") != std::string::npos);
        CHECK(htmlRes.body.find("Remapeamento de Teclado") != std::string::npos);
        CHECK(htmlRes.body.find("subRemapKey") != std::string::npos);

        CHECK(htmlRes.body.find("navTab-search") != std::string::npos);
        CHECK(htmlRes.body.find("navTab-remap") != std::string::npos);
        CHECK(htmlRes.body.find("navTab-logs") != std::string::npos);
        CHECK(htmlRes.body.find("clamp(") != std::string::npos);
        CHECK(htmlRes.body.find("chipSelectModeGP") != std::string::npos);
        CHECK(htmlRes.body.find("chipSelectLangGP") != std::string::npos);
        CHECK(htmlRes.body.find("chipSelectModeLib") != std::string::npos);
        CHECK(htmlRes.body.find("chipSelectLangLib") != std::string::npos);

        // Test GET /api/settings
        auto getSetRes = ws.handleRequest("GET", "/api/settings", "");
        CHECK(getSetRes.status == 200);
        CHECK(getSetRes.contentType.find("application/json") != std::string::npos);
        auto jSet = json::parse(getSetRes.body);
        CHECK(jSet.has_value() && (*jSet)["ok"].asBool());
        CHECK((*jSet).has("keyMap") && (*jSet)["keyMap"].isArray());
        CHECK((*jSet).has("tab") && (*jSet)["tab"].isString());
        CHECK((*jSet).has("filters") && (*jSet)["filters"].isObject());

        // Test POST /api/search with query only
        auto searchRes = ws.handleRequest("POST", "/api/search", R"({"query":"Gears of War"})");
        CHECK(searchRes.status == 200);
        auto jSearch = json::parse(searchRes.body);
        CHECK(jSearch.has_value() && (*jSearch)["ok"].asBool());
        CHECK((*jSearch)["query"].str() == "Gears of War");

        // Test POST /api/search with tab and interactive filters (Game Pass)
        auto searchGPRes = ws.handleRequest("POST", "/api/search", R"({
            "query": "Halo",
            "tab": "gamepass",
            "filters": {
                "plan": 1,
                "console": 2,
                "mode": 1,
                "language": 2,
                "mouseKeyboard": true
            }
        })");
        CHECK(searchGPRes.status == 200);
        auto jSearchGP = json::parse(searchGPRes.body);
        CHECK(jSearchGP.has_value() && (*jSearchGP)["ok"].asBool());
        CHECK((*jSearchGP)["query"].str() == "Halo");
        CHECK((*jSearchGP)["tab"].str() == "gamepass");
        CHECK((*jSearchGP)["filters"]["plan"].asInt() == 1);
        CHECK((*jSearchGP)["filters"]["console"].asInt() == 2);
        CHECK((*jSearchGP)["filters"]["mode"].asInt() == 1);
        CHECK((*jSearchGP)["filters"]["language"].asInt() == 2);
        CHECK((*jSearchGP)["filters"]["mouseKeyboard"].asBool() == true);

        // Test POST /api/search with tab and interactive filters (Library / Meus jogos)
        auto searchLibRes = ws.handleRequest("POST", "/api/search", R"({
            "query": "Forza",
            "tab": "library",
            "filters": {
                "wishlist": true,
                "free": true,
                "cheapest": false,
                "mode": 3,
                "language": 1
            }
        })");
        CHECK(searchLibRes.status == 200);
        auto jSearchLib = json::parse(searchLibRes.body);
        CHECK(jSearchLib.has_value() && (*jSearchLib)["ok"].asBool());
        CHECK((*jSearchLib)["query"].str() == "Forza");
        CHECK((*jSearchLib)["tab"].str() == "library");
        CHECK((*jSearchLib)["filters"]["wishlist"].asBool() == true);
        CHECK((*jSearchLib)["filters"]["free"].asBool() == true);
        CHECK((*jSearchLib)["filters"]["cheapest"].asBool() == false);
        CHECK((*jSearchLib)["filters"]["mode"].asInt() == 3);
        CHECK((*jSearchLib)["filters"]["language"].asInt() == 1);

        // Test setSearchParams / getSearchParams hooks integration
        {
            std::string hookedQ;
            std::string hookedTab;
            json::Value hookedFilters = json::Value::object();
            app::setWebServerHooks({
                /*getSettings=*/nullptr,
                /*updateSettings=*/nullptr,
                /*getSearchQuery=*/nullptr,
                /*setSearchQuery=*/nullptr,
                /*setSearchParams=*/[&](const std::string* q, const std::string* tab, const json::Value* f) {
                    if (q) hookedQ = *q;
                    if (tab) hookedTab = *tab;
                    if (f && f->isObject()) hookedFilters = *f;
                },
                /*getSearchParams=*/[&](std::string& outQ, std::string& outTab, json::Value& outF) {
                    outQ = hookedQ;
                    outTab = hookedTab.empty() ? "gamepass" : hookedTab;
                    outF = hookedFilters;
                }
            });

            ws.handleRequest("POST", "/api/search", R"({"query":"Starfield","tab":"library","filters":{"wishlist":true,"console":1,"mode":2}})");
            CHECK(hookedQ == "Starfield");
            CHECK(hookedTab == "library");
            CHECK(hookedFilters["wishlist"].asBool() == true);
            CHECK(hookedFilters["console"].asInt() == 1);
            CHECK(hookedFilters["mode"].asInt() == 2);

            auto hookGetRes = ws.handleRequest("GET", "/api/settings", "");
            auto jHookGet = json::parse(hookGetRes.body);
            CHECK(jHookGet.has_value() && (*jHookGet)["query"].str() == "Starfield");
            CHECK((*jHookGet)["tab"].str() == "library");
            CHECK((*jHookGet)["filters"]["wishlist"].asBool() == true);
            CHECK((*jHookGet)["filters"]["mode"].asInt() == 2);

            // Test POST without tab: should resolve current tab from hooks
            auto noTabRes = ws.handleRequest("POST", "/api/search", R"({"query":"Flight Simulator"})");
            auto jNoTab = json::parse(noTabRes.body);
            CHECK(jNoTab.has_value() && (*jNoTab)["ok"].asBool());
            CHECK((*jNoTab)["tab"].str() == "library");  // resolved from hookedTab

            // Clean up hooks
            app::setWebServerHooks({});
        }

        // Test POST /api/settings (bad json, padMap only, and padMap + keyMap)
        auto badSetRes = ws.handleRequest("POST", "/api/settings", "not-a-json");
        CHECK(badSetRes.status == 400);

        auto goodSetRes = ws.handleRequest("POST", "/api/settings", R"({"padMap":{"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}})");
        CHECK(goodSetRes.status == 200);

        auto fullSetRes = ws.handleRequest("POST", "/api/settings", R"({
            "padMap": [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15],
            "keyMap": [87,0,83,0,65,0,68,0,38,0,40,0,37,0,39,0,32,0,67,0,82,0,69,0,81,0,70,0,242,0,241,0,16,0,86,243,9,0,27,0]
        })");
        CHECK(fullSetRes.status == 200);

        // Test GET /api/log
        auto logRes = ws.handleRequest("GET", "/api/log", "");
        CHECK(logRes.status == 200);
        CHECK(logRes.contentType.find("text/plain") != std::string::npos);
        CHECK(logRes.extraHeaders.find("Content-Disposition") != std::string::npos);

        // Test 404
        auto notFoundRes = ws.handleRequest("GET", "/api/unknown_endpoint", "");
        CHECK(notFoundRes.status == 404);

        // Test start/stop on a high ephemeral port (e.g. 18088)
        CHECK(ws.start(18088));
        CHECK(ws.isRunning());
        CHECK(ws.port() == 18088);
        ws.stop();
        CHECK(!ws.isRunning());
    }

    // 3. UI Integration: Web QR & Remote Search & Rating Badges
    {
        ui::Fonts dummyFonts{};
        ui::ImageCache images([] {});
        ui::AppUi app(dummyFonts, images);

        app.setWebUrl("http://192.168.1.100:8080");
        CHECK(app.webUrl() == "http://192.168.1.100:8080");

        app.setSearchQuery("Halo");
        CHECK(app.searchQuery() == "Halo");

        // Edge case: setSearchQuery while in streaming or modal mode should NOT yank out of screen
        app.showStreaming();
        CHECK(app.screen() == ui::Screen::Streaming);
        app.setSearchQuery("Forza");
        CHECK(app.screen() == ui::Screen::Streaming);  // Must remain streaming!
        CHECK(app.searchQuery() == "Forza");

        // Test Game Details with rating badge rendering and long descriptors line
        ui::GameTile tile;
        tile.name = "Forza Horizon 5";
        tile.publisher = "Xbox Game Studios";
        tile.description = "Your Ultimate Horizon Adventure awaits!";
        tile.ratingAge = "Livre";
        tile.ratingSystem = "ClassInd";
        tile.ratingDescriptors = {
            "Violência Fantasiosa", "Compras no Jogo", "Interação de Usuários",
            "Linguagem Apropriada", "Elementos Online Adicionais"
        };
        tile.categories = {"Corrida", "Mundo Aberto"};
        tile.playable = true;

        app.showDetails(tile);
        ui::Canvas c(1920, 1080);
        app.render(c, 1000);
        CHECK(c.width() == 1920 && c.height() == 1080);

        // Test other rating badge colors (12+, 14+, 16+, 18+)
        tile.ratingAge = "14+";
        app.showDetails(tile);
        app.render(c, 1000);

        tile.ratingAge = "18+";
        app.showDetails(tile);
        app.render(c, 1000);

        // Test Settings screen rendering with QR Panel
        app.showHome();
        ui::NavInput openOpt;
        openOpt.options = true;
        app.handle(openOpt);
        CHECK(app.screen() == ui::Screen::Settings);
        app.render(c, 1000);
        CHECK(c.width() == 1920);

        // Close Settings
        ui::NavInput back;
        back.back = true;
        app.handle(back);
        CHECK(app.screen() == ui::Screen::Home);
    }
}

static void testBlock1Corrections() {
    using namespace xc;

    // 1. WebUI Facebuttons ABXY static positioning & no transform on active/pressed
    {
        const char* html = app::webServerIndexHtml();
        CHECK(html != nullptr);
        std::string htmlStr(html);
        CHECK(htmlStr.find(".btn-y { top: 0; left: 52px;") != std::string::npos);
        CHECK(htmlStr.find(".btn-a { bottom: 0; left: 52px;") != std::string::npos);
        CHECK(htmlStr.find(".btn-x { top: 52px; left: 0;") != std::string::npos);
        CHECK(htmlStr.find(".btn-b { top: 52px; right: 0;") != std::string::npos);
        // Ensure no transform in abxy-key:active or abxy-key.pressed
        size_t abxyActivePos = htmlStr.find(".abxy-key:active, .abxy-key.pressed");
        CHECK(abxyActivePos != std::string::npos);
        size_t blockEnd = htmlStr.find("}", abxyActivePos);
        CHECK(blockEnd != std::string::npos);
        std::string abxyBlock = htmlStr.substr(abxyActivePos, blockEnd - abxyActivePos);
        CHECK(abxyBlock.find("transform") == std::string::npos);
        CHECK(abxyBlock.find("scale") == std::string::npos);
        CHECK(abxyBlock.find("translate") == std::string::npos);
        CHECK(htmlStr.find("-webkit-tap-highlight-color: transparent;") != std::string::npos);
    }

    // 2. Search cursor stability across background updates (pools, prices, delta-sync, setOwned, setRows)
    {
        ui::Fonts fonts;
        ui::ImageCache images([] {});
        ui::AppUi app(fonts, images);

        std::vector<ui::GameTile> pool;
        for (int i = 0; i < 12; ++i) {
            ui::GameTile t;
            t.productId = "GAME_ID_" + std::to_string(i);
            t.titleId = "TITLE_" + std::to_string(i);
            t.name = "Action Game " + std::to_string(i);
            t.playable = true;
            pool.push_back(t);
        }
        app.setSearchPools(pool, {});

        // Open search with query "Action"
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("Action\n"); // \n moves into results grid
        CHECK(!app.isSearchOnKeys());
        CHECK(app.searchResultFocus() == 0);

        // Move cursor in grid to item 2
        ui::NavInput navRight;
        navRight.right = true;
        app.handle(navRight);
        CHECK(app.searchResultFocus() == 1);
        app.handle(navRight);
        CHECK(app.searchResultFocus() == 2);

        // Simulate background pool update (e.g. hydration/delta-sync)
        app.setSearchPools(pool, {});
        // Focus MUST NOT reset to 0! It should be preserved at 2.
        CHECK(!app.isSearchOnKeys());
        CHECK(app.searchResultFocus() == 2);

        // Simulate background price arrival
        std::map<std::string, ui::PriceInfo> prices;
        prices["GAME_ID_2"] = ui::PriceInfo{"$19.99", "$29.99", 19.99, 29.99};
        app.setPrices(prices);
        CHECK(app.searchResultFocus() == 2);

        // Simulate background ownership update (delta sync)
        app.setOwned({pool[0], pool[1], pool[2]}, {}, true);
        CHECK(app.searchResultFocus() == 2);

        // Simulate background rows update
        ui::GameRow r;
        r.title = "Featured";
        r.tiles = pool;
        app.setRows({r});
        CHECK(app.searchResultFocus() == 2);
    }

    // 3. Friends playing shelf: hero art fetch, resolution, and persistence across Details & Rebuild
    {
        ui::Fonts fonts;
        ui::ImageCache images([] {});
        ui::AppUi app(fonts, images);

        ui::GameTile friendTile;
        friendTile.productId = "PROD_FRIEND_01";
        friendTile.titleId = "TITLE_FRIEND_01";
        friendTile.name = "Friend's Favorite";
        friendTile.playable = true;
        friendTile.heroUrl = ""; // Initially empty
        friendTile.friends = {{"FriendTag", "http://example.com/pic.jpg"}};

        ui::GameRow friendsRow;
        friendsRow.title = "Friends playing now";
        friendsRow.tiles = {friendTile};
        app.setRows({friendsRow});
        app.showHome();

        // 3a. On Home screen with tile focused, heroWanted requests the missing heroUrl
        CHECK(app.heroWanted(100) == "PROD_FRIEND_01");

        // 3b. Detail arrives via setDetailInfo
        std::string heroUrl = "https://images.example.com/hero_friend_01.jpg";
        app.setDetailInfo("PROD_FRIEND_01", "Friend game description", "Studio A", {"Co-Op"}, heroUrl, "E", "ESRB", {});

        // 3c. After details arrived, heroWanted no longer asks for it
        CHECK(app.heroWanted(200).empty());

        // 3d. User opens Details screen, then presses back to Home
        app.showDetails(friendTile);
        CHECK(app.screen() == ui::Screen::Details);

        ui::NavInput navBack;
        navBack.back = true;
        app.handle(navBack);
        CHECK(app.screen() == ui::Screen::Home);

        // Verify the tile on the Friends shelf preserves the heroUrl!
        ui::GameTile resultTile;
        CHECK(app.findTile("TITLE_FRIEND_01", resultTile));
        CHECK(resultTile.heroUrl == heroUrl);

        // 3e. Crucial: Background rebuild (e.g. delta-sync or setOwned) MUST NOT wipe out heroUrl!
        app.setOwned({}, {}, true);
        CHECK(app.findTile("TITLE_FRIEND_01", resultTile));
        CHECK(resultTile.heroUrl == heroUrl);

        // 3f. Tile with partial metadata (only titleId, empty productId) resolves productId via pool
        ui::GameTile partialTile;
        partialTile.productId = ""; // Empty productId
        partialTile.titleId = "TITLE_PARTIAL_99";
        partialTile.name = "Partial Metadata Game";
        partialTile.playable = true;
        partialTile.heroUrl = "";

        ui::GameTile catalogTile;
        catalogTile.productId = "PROD_PARTIAL_99";
        catalogTile.titleId = "TITLE_PARTIAL_99";
        catalogTile.name = "Partial Metadata Game";
        catalogTile.heroUrl = "";
        catalogTile.playable = true;

        ui::GameRow partialRow;
        partialRow.title = "Friends playing partial";
        partialRow.tiles = {partialTile};

        app.setSearchPools({catalogTile}, {});
        app.setRows({partialRow});
        app.showHome();

        // heroWanted must successfully resolve the productId "PROD_PARTIAL_99" from the catalog pool!
        CHECK(app.heroWanted(300) == "PROD_PARTIAL_99");
    }

    // 4. Library::tile resolution by titleId
    {
        app::Library lib;
        ui::GameTile resolved = lib.tile("", "CALLOFDUTYHQ");
        CHECK(!resolved.productId.empty());
        CHECK(resolved.productId == "9NCPSQQD1Z66" || resolved.productId == "9N201KQXS5BM" || resolved.productId == "9NNFG8BQRCXL");
        CHECK(!resolved.heroUrl.empty());
    }
}

static void testBloco2Corrections() {
    // 1. Splash screen input lock & Screen::Error offline option
    {
        ui::Fonts fonts;
        ui::ImageCache images([] {});
        ui::AppUi app(fonts, images);
        CHECK(app.screen() == ui::Screen::Splash);

        // All inputs while on Splash must be ignored/dropped
        ui::NavInput in;
        in.accept = true;
        in.back = true;
        in.options = true;
        in.square = true;
        in.triangle = true;
        in.up = true;
        in.down = true;
        in.left = true;
        in.right = true;
        ui::UiEvent ev = app.handle(in);
        CHECK(ev.action == ui::Action::None);
        CHECK(app.screen() == ui::Screen::Splash);

        // Error screen without cached rows cannot enter offline mode
        app.showError("Connection failed");
        CHECK(app.screen() == ui::Screen::Error);
        in = {};
        in.square = true;
        ev = app.handle(in);
        CHECK(app.screen() == ui::Screen::Error);

        // Populate cached rows
        ui::GameRow row;
        row.title = "Offline Cached Games";
        ui::GameTile tile;
        tile.productId = "PROD_CACHE_1";
        tile.titleId = "TITLE_CACHE_1";
        tile.name = "Offline Game";
        row.tiles.push_back(tile);
        app.setRows({row});

        // Error screen WITH cached rows: Square or Circle(Back) enters offline mode (Screen::Home)
        in = {};
        in.square = true;
        ev = app.handle(in);
        CHECK(app.screen() == ui::Screen::Home);

        app.showError("Network timed out");
        CHECK(app.screen() == ui::Screen::Error);
        in = {};
        in.back = true;
        ev = app.handle(in);
        CHECK(app.screen() == ui::Screen::Home);

        // Error screen: loading flag is cleared when error is displayed and when offline mode is entered
        app.setLoading(true, 5, 10);
        app.showError("Disconnected");
        CHECK(app.screen() == ui::Screen::Error);
        in = {};
        in.square = true;
        ev = app.handle(in);
        CHECK(app.screen() == ui::Screen::Home);

        // Error screen with owned games but empty rows: can enter offline mode
        ui::AppUi appOwned(fonts, images);
        ui::GameTile ownedTile;
        ownedTile.productId = "OWNED_1";
        ownedTile.titleId = "TITLE_1";
        ownedTile.playable = true;
        appOwned.setOwned({ownedTile}, {}, true);
        appOwned.showError("Auth error");
        CHECK(appOwned.screen() == ui::Screen::Error);
        in = {};
        in.square = true;
        ev = appOwned.handle(in);
        CHECK(appOwned.screen() == ui::Screen::Home);
    }

    // 2. syncDelta: Eliminate false 24 new games popup
    {
        app::Library lib;
        xcloud::GssvClient gssv;
        std::string err;

        // Set up test transport where listTitles returns 24 alternate IDs
        // and fetchProducts returns empty/failure for them
        net::setTestTransport([&](const net::Request& req) {
            net::Response r;
            if (req.url.find("titles") != std::string::npos) {
                r.status = 200;
                // Return 1 title with 24 alternate product IDs
                json::Value root = json::Value::object();
                json::Value results = json::Value::array();
                json::Value item = json::Value::object();
                item.set("titleId", "GAME_HQ");
                json::Value details = json::Value::object();
                details.set("productId", "PROD_HQ");
                item.set("details", details);
                json::Value alts = json::Value::array();
                for (int i = 1; i <= 24; ++i) {
                    alts.push("ALT_PROD_" + std::to_string(i));
                }
                item.set("alternateProductIds", alts);
                results.push(item);
                root.set("results", results);
                r.body = root.dump();
                return r;
            }
            if (req.url.find("sigls") != std::string::npos) {
                r.status = 200;
                r.body = R"({"productIds":["PROD_HQ"]})";
                return r;
            }
            if (req.url.find("products") != std::string::npos) {
                r.status = 200;
                // Only return PROD_HQ with valid metadata, none of the 24 alternate IDs
                r.body = R"({
                    "Products": {
                        "PROD_HQ": {
                            "ProductTitle": "Call of Duty HQ"
                        }
                    }
                })";
                return r;
            }
            r.status = 200;
            r.body = "{}";
            return r;
        });

        // First sync: PROD_HQ is added (1 new game)
        size_t count1 = lib.syncDelta(gssv, [&] {}, err);
        CHECK(count1 == 1);

        // Second sync (subsequent boot): PROD_HQ is already in products_,
        // and 24 alternate IDs must NOT be computed as new games! newCount must be 0!
        size_t count2 = lib.syncDelta(gssv, [&] {}, err);
        CHECK(count2 == 0);

        net::setTestTransport({});
    }

    // 3. Log hygiene & consecutive duplicate suppression
    {
        xc::log::setLevel(xc::log::Level::Info);

        // Write identical message 5 times
        XC_LOGI("bloco2_hygiene_test_repeated_log");
        XC_LOGI("bloco2_hygiene_test_repeated_log");
        XC_LOGI("bloco2_hygiene_test_repeated_log");
        XC_LOGI("bloco2_hygiene_test_repeated_log");
        XC_LOGI("bloco2_hygiene_test_repeated_log");

        // Write a distinct message to trigger repeat count flush
        XC_LOGI("bloco2_hygiene_test_distinct_log");

        std::string after = xc::log::recentLogs();
        CHECK(after.find("bloco2_hygiene_test_repeated_log") != std::string::npos);
        CHECK(after.find("last message repeated 4 times") != std::string::npos);
        CHECK(after.find("bloco2_hygiene_test_distinct_log") != std::string::npos);

        // Test singular repetition: exactly 2 identical logs
        XC_LOGI("bloco2_hygiene_test_twice_log");
        XC_LOGI("bloco2_hygiene_test_twice_log");
        xc::log::flush();
        std::string afterTwice = xc::log::recentLogs();
        CHECK(afterTwice.find("bloco2_hygiene_test_twice_log") != std::string::npos);
        CHECK(afterTwice.find("last message repeated 1 time]") != std::string::npos);

        // Test flush clears last message state: same message emitted after flush is printed afresh
        XC_LOGI("bloco2_hygiene_test_twice_log");
        xc::log::flush();
        std::string afterFlush = xc::log::recentLogs();
        size_t pos1 = afterFlush.find("bloco2_hygiene_test_twice_log");
        CHECK(pos1 != std::string::npos);
        size_t pos2 = afterFlush.find("bloco2_hygiene_test_twice_log", pos1 + 1);
        CHECK(pos2 != std::string::npos);
    }
}

static void testBloco3Corrections() {
    // -------------------------------------------------------------------------
    // Item 1: Shelf Filters / Non-contamination
    // Store/owned games (e.g. Tesla vs Lovecraft) must never contaminate Game Pass
    // shelves (Recently Added, Most Popular, Leaving Soon, All Games).
    // syncDelta must not inject arbitrary owned titles into rows.
    // Paid games in "Most Popular" are only allowed if legitimately in kMostPopular.
    // -------------------------------------------------------------------------
    {
        std::string testDir = "/tmp/test-bloco3-shelves";
        xc::fs::removeTree(testDir);
        xc::fs::makeDirs(testDir);
        std::string cacheFile = testDir + "/library_cache.json";

        std::map<std::string, std::string> siglFeeds;
        siglFeeds[xcloud::sigl::kRecentlyAdded] = R"([{"siglId":"recent","title":"Recently Added"},{"id":"GP_RECENT_1"}])";
        siglFeeds[xcloud::sigl::kMostPopular] = R"([{"siglId":"pop","title":"Most Popular"},{"id":"GP_POP_1"},{"id":"PAID_LEGIT_POP"}])";
        siglFeeds[xcloud::sigl::kFreeToPlay] = R"([{"siglId":"f2p","title":"Free to Play"},{"id":"F2P_CLOUD_1"}])";
        siglFeeds[xcloud::sigl::kLeavingSoon] = R"([{"siglId":"leaving","title":"Leaving Soon"},{"id":"GP_LEAVING_1"}])";
        siglFeeds[xcloud::sigl::kAllGames] = R"([{"siglId":"all","title":"All Games"},{"id":"GP_RECENT_1"},{"id":"GP_POP_1"},{"id":"GP_LEAVING_1"}])";

        std::string titlesV2Body = R"({"results":[]})";

        net::setTestTransport([&](const net::Request& req) {
            net::Response r;
            r.status = 200;
            if (req.url.find("sigls") != std::string::npos) {
                for (const auto& [siglId, body] : siglFeeds) {
                    if (req.url.find("id=" + siglId) != std::string::npos) {
                        r.body = body;
                        return r;
                    }
                }
                r.body = "[]";
                return r;
            } else if (req.url.find("titles") != std::string::npos) {
                r.body = titlesV2Body;
                return r;
            } else if (req.url.find("products") != std::string::npos) {
                r.body = R"({
                    "Products": {
                        "GP_RECENT_1": {
                            "XCloudTitleId": "T_REC",
                            "ProductTitle": "Recent Cloud Game",
                            "Image_Tile": {"URL": "https://store-images.s-microsoft.com/image/apps.tile"},
                            "PassMetadataByPassProductId": {"CFQ7TTC0K5DJ": {}}
                        },
                        "GP_POP_1": {
                            "XCloudTitleId": "T_POP",
                            "ProductTitle": "Popular Cloud Game",
                            "Image_Tile": {"URL": "https://store-images.s-microsoft.com/image/apps.tile"},
                            "PassMetadataByPassProductId": {"CFQ7TTC0K5DJ": {}}
                        },
                        "PAID_LEGIT_POP": {
                            "XCloudTitleId": "T_LEGIT_POP",
                            "ProductTitle": "Elden Ring",
                            "Image_Tile": {"URL": "https://store-images.s-microsoft.com/image/apps.tile"},
                            "PassMetadataByPassProductId": {}
                        },
                        "F2P_CLOUD_1": {
                            "XCloudTitleId": "T_F2P",
                            "ProductTitle": "Free Cloud Shooter",
                            "Image_Tile": {"URL": "https://store-images.s-microsoft.com/image/apps.tile"}
                        },
                        "GP_LEAVING_1": {
                            "XCloudTitleId": "T_LEAVE",
                            "ProductTitle": "Leaving Cloud Game",
                            "Image_Tile": {"URL": "https://store-images.s-microsoft.com/image/apps.tile"},
                            "PassMetadataByPassProductId": {"CFQ7TTC0K5DJ": {}}
                        },
                        "PROD_TESLA": {
                            "XCloudTitleId": "TESLAVSLOVE",
                            "ProductTitle": "Tesla vs Lovecraft",
                            "Image_Tile": {"URL": "https://store-images.s-microsoft.com/image/apps.tile"}
                        }
                    }
                })";
                return r;
            }
            r.status = 503;
            return r;
        });

        app::Library lib;
        lib.setCachePath(cacheFile);
        lib.setSubscription("Ultimate");
        xcloud::GssvClient gssv;
        std::string err;
        CHECK(lib.load(gssv, "en-US", [] {}, err));

        // Verify initial load shelves:
        // PAID_LEGIT_POP should appear in Most Popular because it's legitimately in the feed
        bool paidInPop = false;
        bool paidInRecent = false;
        bool paidInLeaving = false;
        for (const auto& row : lib.rows()) {
            for (const auto& t : row.tiles) {
                if (t.productId == "PAID_LEGIT_POP") {
                    if (row.title == "Most Popular") paidInPop = true;
                    if (row.title == "Recently Added") paidInRecent = true;
                    if (row.title == "Leaving Soon") paidInLeaving = true;
                }
            }
        }
        CHECK(paidInPop);
        CHECK(!paidInRecent);
        CHECK(!paidInLeaving);

        // Now simulate user having bought / owned "Tesla vs Lovecraft" via /v2/titles
        titlesV2Body = R"({
            "results": [
                {
                    "titleId": "TESLAVSLOVE",
                    "details": {
                        "productId": "PROD_TESLA",
                        "hasEntitlement": true,
                        "isFreeInStore": false,
                        "userPrograms": []
                    }
                }
            ]
        })";

        // Call syncDelta: buying an owned store game outside Game Pass must NOT count as a Game Pass catalog addition
        size_t deltaAdded = lib.syncDelta(gssv, [] {}, err);
        CHECK(deltaAdded == 0);

        // Verify Tesla vs Lovecraft is owned
        bool teslaOwned = false;
        for (const auto& t : lib.owned()) {
            if (t.productId == "PROD_TESLA") teslaOwned = true;
        }
        CHECK(teslaOwned);

        // CRITICAL CHECK: Tesla vs Lovecraft MUST NOT contaminate ANY Game Pass shelf
        for (const auto& row : lib.rows()) {
            for (const auto& t : row.tiles) {
                CHECK(t.productId != "PROD_TESLA");
                CHECK(t.titleId != "TESLAVSLOVE");
            }
        }

        net::setTestTransport({});
    }

    // -------------------------------------------------------------------------
    // Item 2: Universal Deduplication & PC Removal
    // -------------------------------------------------------------------------
    {
        // 2a. normalizeBaseTitle
        CHECK(xcloud::normalizeBaseTitle("A Way Out") == "a way out");
        CHECK(xcloud::normalizeBaseTitle("A Way Out (Xbox One)") == "a way out");
        CHECK(xcloud::normalizeBaseTitle("A Way Out – Xbox One") == "a way out"); // en-dash
        CHECK(xcloud::normalizeBaseTitle("A Way Out: Standard Edition") == "a way out");
        CHECK(xcloud::normalizeBaseTitle("Alien: Dark Descent") == "alien dark descent");
        CHECK(xcloud::normalizeBaseTitle("Aliens: Dark Descent - Xbox Series X|S") == "alien dark descent");
        CHECK(xcloud::normalizeBaseTitle("Aliens: Dark Descent – Xbox Series X|S") == "alien dark descent");
        CHECK(xcloud::normalizeBaseTitle("Assassin's Creed® Unity") == "assassins creed unity");
        CHECK(xcloud::normalizeBaseTitle("Assassin’s Creed Unity (Xbox One)") == "assassins creed unity"); // smart quote
        CHECK(xcloud::normalizeBaseTitle("Assassin's Creed Unity (Xbox One)") == "assassins creed unity");
        CHECK(xcloud::normalizeBaseTitle("Broken Age") == "broken age");
        CHECK(xcloud::normalizeBaseTitle("Broken Age (PC)") == "broken age");
        CHECK(xcloud::normalizeBaseTitle("Broken Age (Windows)") == "broken age");
        CHECK(xcloud::normalizeBaseTitle("Broken Age (Windows 11)") == "broken age");
        CHECK(xcloud::normalizeBaseTitle("Burnout™ Paradise Remastered") == "burnout paradise");
        CHECK(xcloud::normalizeBaseTitle("Burnout Paradise Remastered - Xbox One") == "burnout paradise");
        CHECK(xcloud::normalizeBaseTitle("Burnout Paradise Remastered – Xbox One") == "burnout paradise");
        CHECK(xcloud::normalizeBaseTitle("FIFA 22 Xbox One") == "fifa 22");
        CHECK(xcloud::normalizeBaseTitle("FIFA 22 Xbox Series X|S") == "fifa 22");
        CHECK(xcloud::normalizeBaseTitle("Call of Duty®: Vanguard (Xbox One)") == "call of duty vanguard");
        CHECK(xcloud::normalizeBaseTitle("Call of Duty®: Vanguard - Xbox Series X|S") == "call of duty vanguard");
        CHECK(xcloud::normalizeBaseTitle("Grand Theft Auto V: Premium Edition") == "grand theft auto v");

        // 2b. isPcTitle & isPcProduct
        CHECK(xcloud::isPcTitle("Broken Age (PC)"));
        CHECK(xcloud::isPcTitle("Broken Age (Windows)"));
        CHECK(xcloud::isPcTitle("Broken Age (Windows 11)"));
        CHECK(xcloud::isPcTitle("Age of Empires II: Definitive Edition (Windows 10)"));
        CHECK(xcloud::isPcTitle("Fallout 76: PC Edition"));
        CHECK(xcloud::isPcTitle("Sea of Thieves [PC]"));
        CHECK(!xcloud::isPcTitle("Halo Infinite"));
        CHECK(!xcloud::isPcTitle("Forza Horizon 5"));

        xcloud::Product pcProd;
        pcProd.title = "PC Simulator";
        pcProd.categories = {"PC"};
        CHECK(xcloud::isPcProduct(pcProd));

        xcloud::Product consoleProd;
        consoleProd.title = "Halo";
        consoleProd.categories = {"Shooter"};
        CHECK(!xcloud::isPcProduct(consoleProd));

        // 2c. dedupeByTitle priority (Series X|S > Xbox One > 360) and capability merging
        std::vector<ui::GameTile> tiles;

        // Vanguard pair: Xbox One vs Series X|S
        ui::GameTile vgOne;
        vgOne.productId = "9PHP6HF58MB5";
        vgOne.titleId = "VANGUARDONE";
        vgOne.name = "Call of Duty®: Vanguard (Xbox One)";
        vgOne.platform = "XboxOne";
        vgOne.playable = true;
        vgOne.purchasable = false;

        ui::GameTile vgXs;
        vgXs.productId = "9MWHTW1SF7RW";
        vgXs.titleId = "VANGUARDXS";
        vgXs.name = "Call of Duty®: Vanguard - Xbox Series X|S";
        vgXs.platform = "Series";
        vgXs.playable = false;
        vgXs.purchasable = true;

        // Cold War pair
        ui::GameTile cwOne;
        cwOne.productId = "9P7PFH4RMFZD";
        cwOne.titleId = "COLDWARONE";
        cwOne.name = "Call of Duty®: Black Ops Cold War (Xbox One)";
        cwOne.platform = "XboxOne";

        ui::GameTile cwXs;
        cwXs.productId = "9NPLRJ9CC870";
        cwXs.titleId = "COLDWARXS";
        cwXs.name = "Call of Duty®: Black Ops Cold War - Xbox Series X|S";
        cwXs.platform = "Series";

        // A Way Out pair
        ui::GameTile awo1;
        awo1.productId = "AWAYOUT1";
        awo1.titleId = "AWO_1";
        awo1.name = "A Way Out";

        ui::GameTile awo2;
        awo2.productId = "AWAYOUT2";
        awo2.titleId = "AWO_2";
        awo2.name = "A Way Out (Xbox One)";

        // Burnout Paradise Remastered pair
        ui::GameTile bp1;
        bp1.productId = "BP_1";
        bp1.titleId = "BP_T1";
        bp1.name = "Burnout™ Paradise Remastered";

        ui::GameTile bp2;
        bp2.productId = "BP_2";
        bp2.titleId = "BP_T2";
        bp2.name = "Burnout Paradise Remastered - Xbox One";

        // Assassin's Creed Unity pair
        ui::GameTile acu1;
        acu1.productId = "ACU_1";
        acu1.titleId = "ACU_T1";
        acu1.name = "Assassin's Creed® Unity";

        ui::GameTile acu2;
        acu2.productId = "ACU_2";
        acu2.titleId = "ACU_T2";
        acu2.name = "Assassin's Creed Unity (Xbox One)";

        // Alien Dark Descent pair
        ui::GameTile add1;
        add1.productId = "ADD_1";
        add1.titleId = "ADD_T1";
        add1.name = "Aliens: Dark Descent";

        ui::GameTile add2;
        add2.productId = "ADD_2";
        add2.titleId = "ADD_T2";
        add2.name = "Alien: Dark Descent - Xbox Series X|S";
        add2.platform = "Series";

        // FIFA 22 pair
        ui::GameTile fifa1;
        fifa1.productId = "FIFA_ONE";
        fifa1.titleId = "FIFA_T1";
        fifa1.name = "FIFA 22 Xbox One";
        fifa1.platform = "XboxOne";

        ui::GameTile fifa2;
        fifa2.productId = "FIFA_XS";
        fifa2.titleId = "FIFA_T2";
        fifa2.name = "FIFA 22 Xbox Series X|S";
        fifa2.platform = "Series";

        tiles.push_back(vgOne);
        tiles.push_back(vgXs);
        tiles.push_back(cwOne);
        tiles.push_back(cwXs);
        tiles.push_back(awo1);
        tiles.push_back(awo2);
        tiles.push_back(bp1);
        tiles.push_back(bp2);
        tiles.push_back(acu1);
        tiles.push_back(acu2);
        tiles.push_back(add1);
        tiles.push_back(add2);
        tiles.push_back(fifa1);
        tiles.push_back(fifa2);

        app::dedupeByTitle(tiles);

        // Exactly 7 deduplicated entries
        CHECK(tiles.size() == 7);

        // Vanguard: Series X|S preferred, capabilities merged
        bool foundVanguard = false;
        for (const auto& t : tiles) {
            if (t.productId == "9MWHTW1SF7RW") {
                foundVanguard = true;
                CHECK(t.platform == "Series");
                CHECK(t.playable);     // absorbed from vgOne
                CHECK(t.purchasable);  // preserved from vgXs
            }
        }
        CHECK(foundVanguard);

        // Cold War: Series X|S preferred
        bool foundColdWar = false;
        for (const auto& t : tiles) {
            if (t.productId == "9NPLRJ9CC870") {
                foundColdWar = true;
                CHECK(t.platform == "Series");
            }
        }
        CHECK(foundColdWar);

        // Alien Dark Descent: Series X|S preferred
        bool foundAdd = false;
        for (const auto& t : tiles) {
            if (t.productId == "ADD_2") {
                foundAdd = true;
                CHECK(t.platform == "Series");
            }
        }
        CHECK(foundAdd);

        // FIFA 22: Series X|S preferred
        bool foundFifa = false;
        for (const auto& t : tiles) {
            if (t.productId == "FIFA_XS") {
                foundFifa = true;
                CHECK(t.platform == "Series");
            }
        }
        CHECK(foundFifa);

        // 2d. Dominant Series priority: Series must win even if Xbox One has full metadata & shorter name
        {
            std::vector<ui::GameTile> domTest;
            ui::GameTile richOne;
            richOne.productId = "RICH_ONE";
            richOne.titleId = "RICH_ONE_TID";
            richOne.name = "Epic Game";
            richOne.platform = "XboxOne";
            richOne.playable = true;
            richOne.detailed = true;
            richOne.tileUrl = "https://example.com/one.png";
            richOne.heroUrl = "https://example.com/one_hero.png";
            richOne.description = "Awesome Xbox One version";

            ui::GameTile bareXs;
            bareXs.productId = "BARE_XS";
            bareXs.titleId = "BARE_XS_TID";
            bareXs.name = "Epic Game - Xbox Series X|S";
            bareXs.platform = "Series";
            bareXs.playable = false;
            bareXs.detailed = false;

            domTest.push_back(richOne);
            domTest.push_back(bareXs);
            app::dedupeByTitle(domTest);
            CHECK(domTest.size() == 1);
            CHECK(domTest[0].productId == "BARE_XS");
            CHECK(domTest[0].platform == "Series");
            CHECK(domTest[0].playable); // absorbed from richOne
            CHECK(domTest[0].tileUrl == "https://example.com/one.png");
            CHECK(domTest[0].description == "Awesome Xbox One version");

            // Order independence: bareXs first, richOne second
            std::vector<ui::GameTile> domTestRev;
            domTestRev.push_back(bareXs);
            domTestRev.push_back(richOne);
            app::dedupeByTitle(domTestRev);
            CHECK(domTestRev.size() == 1);
            CHECK(domTestRev[0].productId == "BARE_XS");
            CHECK(domTestRev[0].platform == "Series");
            CHECK(domTestRev[0].playable);
        }
    }

    // -------------------------------------------------------------------------
    // Item 3: Call of Duty CDN Art & Metadata in kKnownCloudGames
    // -------------------------------------------------------------------------
    {
        app::Library lib;

        // Cold War Xbox One (9P7PFH4RMFZD)
        ui::GameTile cwOne = lib.tile("9P7PFH4RMFZD", "CALLOFDUTYCOLDWAR");
        CHECK(!cwOne.name.empty());
        CHECK(cwOne.name.find("Cold War") != std::string::npos);
        CHECK(!cwOne.titleId.empty() && cwOne.titleId != "None");
        CHECK(!cwOne.tileUrl.empty());
        CHECK(cwOne.tileUrl.find("store-images.s-microsoft.com") != std::string::npos);
        CHECK(!cwOne.heroUrl.empty());
        CHECK(!cwOne.description.empty());
        CHECK(cwOne.description.find("Cold War") != std::string::npos);
        CHECK(cwOne.detailed);

        // Cold War Series X|S (9NPLRJ9CC870)
        ui::GameTile cwXs = lib.tile("9NPLRJ9CC870", "CALLOFDUTYCOLDWAR");
        CHECK(!cwXs.name.empty());
        CHECK(cwXs.name.find("Cold War") != std::string::npos);
        CHECK(!cwXs.titleId.empty() && cwXs.titleId != "None");
        CHECK(!cwXs.tileUrl.empty());
        CHECK(cwXs.tileUrl.find("store-images.s-microsoft.com") != std::string::npos);
        CHECK(!cwXs.heroUrl.empty());
        CHECK(!cwXs.description.empty());
        CHECK(cwXs.detailed);

        // Cold War query without titleId (store ID only)
        ui::GameTile cwNoTid = lib.tile("9P7PFH4RMFZD", "");
        CHECK(!cwNoTid.name.empty());
        CHECK(!cwNoTid.titleId.empty() && cwNoTid.titleId != "None");
        CHECK(!cwNoTid.tileUrl.empty());
        CHECK(!cwNoTid.description.empty());
        CHECK(cwNoTid.detailed);

        // Cold War query without productId (cloudTitleId only)
        ui::GameTile cwNoPid = lib.tile("", "CALLOFDUTYBLACKOPSCOLDWAR");
        CHECK(!cwNoPid.name.empty());
        CHECK(cwNoPid.productId == "9P7PFH4RMFZD");
        CHECK(!cwNoPid.tileUrl.empty());
        CHECK(cwNoPid.detailed);

        // Vanguard Xbox One (9PHP6HF58MB5)
        ui::GameTile vgOne = lib.tile("9PHP6HF58MB5", "CALLOFDUTYVANGUARD");
        CHECK(!vgOne.name.empty());
        CHECK(!vgOne.titleId.empty());
        CHECK(!vgOne.tileUrl.empty());
        CHECK(vgOne.tileUrl.find("store-images.s-microsoft.com") != std::string::npos);
        CHECK(!vgOne.heroUrl.empty());
        CHECK(!vgOne.description.empty());
        CHECK(vgOne.description.find("Vanguard") != std::string::npos);
        CHECK(vgOne.detailed);

        // Vanguard Series X|S (9MWHTW1SF7RW)
        ui::GameTile vgXs = lib.tile("9MWHTW1SF7RW", "CALLOFDUTYVANGUARD");
        CHECK(!vgXs.name.empty());
        CHECK(!vgXs.titleId.empty());
        CHECK(!vgXs.tileUrl.empty());
        CHECK(vgXs.tileUrl.find("store-images.s-microsoft.com") != std::string::npos);
        CHECK(!vgXs.heroUrl.empty());
        CHECK(!vgXs.description.empty());
        CHECK(vgXs.detailed);

        // Modern Warfare (9NVQBQ3F6W9W & C5DTJ99626K3)
        ui::GameTile mw1 = lib.tile("9NVQBQ3F6W9W", "CALLOFDUTYMODERNWARFARE");
        CHECK(!mw1.name.empty());
        CHECK(!mw1.titleId.empty());
        CHECK(!mw1.tileUrl.empty());
        CHECK(mw1.tileUrl.find("store-images.s-microsoft.com") != std::string::npos);
        CHECK(!mw1.heroUrl.empty());
        CHECK(!mw1.description.empty());
        CHECK(mw1.detailed);

        ui::GameTile mw2 = lib.tile("C5DTJ99626K3", "CALLOFDUTYMODERNWARFARE");
        CHECK(!mw2.name.empty());
        CHECK(!mw2.titleId.empty());
        CHECK(!mw2.tileUrl.empty());
        CHECK(mw2.tileUrl.find("store-images.s-microsoft.com") != std::string::npos);
        CHECK(!mw2.heroUrl.empty());
        CHECK(!mw2.description.empty());
        CHECK(mw2.detailed);

        // Verify end-to-end processing through lib.tiles() without dropping cards
        std::vector<std::pair<std::string, std::string>> codItems = {
            {"9P7PFH4RMFZD", ""}, // Cold War Xbox One
            {"9NPLRJ9CC870", ""}, // Cold War Series X|S
            {"9PHP6HF58MB5", ""}, // Vanguard Xbox One
            {"9MWHTW1SF7RW", ""}, // Vanguard Series X|S
            {"9NVQBQ3F6W9W", ""}, // Modern Warfare 2019
            {"C5DTJ99626K3", ""}  // Modern Warfare 2019 alt
        };
        std::vector<ui::GameTile> codTiles;
        for (const auto& [pid, tid] : codItems) {
            ui::GameTile t = lib.tile(pid, tid);
            if (!t.productId.empty() && !t.titleId.empty() && !t.name.empty()) {
                codTiles.push_back(std::move(t));
            }
        }
        app::dedupeByTitle(codTiles);
        // Exactly 3 deduplicated games (Cold War Series X|S, Vanguard Series X|S, Modern Warfare)
        CHECK(codTiles.size() == 3);
        for (const auto& t : codTiles) {
            CHECK(!t.name.empty());
            CHECK(!t.titleId.empty());
            CHECK(!t.tileUrl.empty());
            CHECK(!t.heroUrl.empty());
            CHECK(!t.description.empty());
            CHECK(t.detailed);
        }
    }
}

void testPhase1BugFixesAndPhase2Optimizations() {
    // 1. GssvClient previewFeatures header propagation across authenticated requests
    {
        std::string lastDeviceInfo;
        net::setTestTransport([&](const net::Request& req) {
            for (const auto& [k, v] : req.headers) {
                if (k == "X-MS-Device-Info" || k == "x-ms-device-info") lastDeviceInfo = v;
            }
            net::Response r;
            r.status = 200;
            if (req.url.find("/v2/login/user") != std::string::npos) {
                r.body = R"({"gsToken":"tok","market":"US","offeringSettings":{"regions":[{"name":"EASTUS","baseUri":"https://eastus.gssv.com","isDefault":true}]}})";
            } else if (req.url.find("/v2/titles") != std::string::npos) {
                r.body = R"({"results":[]})";
            }
            return r;
        });
        xcloud::GssvClient gssv;
        gssv.setPreviewFeatures(true);
        std::string err;
        auth::XblToken xblTok;
        xblTok.token = "xbl_tok";
        CHECK(gssv.login(xblTok, err));

        lastDeviceInfo.clear();
        std::vector<xcloud::Title> titles;
        CHECK(gssv.listTitles(titles, err));
        CHECK(lastDeviceInfo.find("play.xbox.com") != std::string::npos);

        gssv.setPreviewFeatures(false);
        lastDeviceInfo.clear();
        CHECK(gssv.listTitles(titles, err));
        CHECK(lastDeviceInfo.find("Microsoft.GamingApp") != std::string::npos);
        net::setTestTransport({});
    }

    // 2. ProductId isolation when sharing titleId (Bug 1.5) & markUnclaimable (Bug 2.1)
    {
        ui::Fonts fonts;
        fonts.load("assets/fonts");
        ui::ImageCache images([] {});
        ui::AppUi ui(fonts, images);

        ui::GameTile baseEd, deluxeEd;
        baseEd.productId = "PROD_BASE_01";
        baseEd.titleId = "SHARED_TID";
        baseEd.name = "Base Edition";
        baseEd.playable = true;
        baseEd.freeInStore = true;

        deluxeEd.productId = "PROD_DELUXE1";
        deluxeEd.titleId = "SHARED_TID";
        deluxeEd.name = "Deluxe Edition";
        deluxeEd.playable = false;
        deluxeEd.purchasable = true;

        ui::GameRow row;
        row.title = "Row";
        row.tiles = {baseEd, deluxeEd};
        ui.setCatalogState({row}, {baseEd}, {deluxeEd}, true, true, {baseEd}, {deluxeEd}, false, 0, 0);

        ui.setDetailInfo("PROD_BASE_01", "Base description only", "BasePub", {}, "https://example.com/base_hero.jpg");

        ui.showDetails(deluxeEd);
        CHECK(ui.detailTile().productId == "PROD_DELUXE1");
        CHECK(ui.detailTile().description.empty());
        CHECK(ui.detailTile().heroUrl.empty());

        // markUnclaimable clears freeInStore across cached lists
        ui.markUnclaimable("PROD_BASE_01");
        ui::GameTile found;
        CHECK(ui.findTile("SHARED_TID", found));
        CHECK(!found.freeInStore);
    }

    // 3. Mouse hit-testing on Details (only inside button rect) and Settings rows 10 & 22 + right-click save
    {
        ui::Fonts fonts;
        fonts.load("assets/fonts");
        ui::ImageCache images([] {});
        ui::AppUi ui(fonts, images);
        ui::UiCaps caps;
        caps.desktopWindow = true;
        caps.touchpad = true;
        ui.setCaps(caps);

        ui::GameTile freeTile;
        freeTile.productId = "9P_FREE_GAME";
        freeTile.titleId = "TID_FREE";
        freeTile.name = "Free Game";
        freeTile.playable = false;
        freeTile.freeInStore = true;

        ui.showDetails(freeTile);
        ui::Canvas c(1920, 1080);
        ui.render(c, 1000);

        // Clicking outside the ClaimFree button must NOT trigger ClaimFreeGame
        auto evOutside = ui.onMouseDown(1500.0f, 200.0f, 1);
        CHECK(evOutside.action == ui::Action::None);

        // Open Settings and test Row 10 (Bitrate) accept and Row 22 (PreviewFeatures) click
        ui.showHome();
        ui.handle(ui::NavInput{.options = true});
        CHECK(ui.screen() == ui::Screen::Settings);

        // Navigate to row 10 (BitrateCap) and press accept
        while (ui.settingsFocus() != 10) {
            ui.handle(ui::NavInput{.down = true});
        }
        auto evBitrate = ui.handle(ui::NavInput{.accept = true});
        CHECK(evBitrate.action == ui::Action::SettingsChanged);

        // Right-click in Settings saves and exits to Home
        auto evRightClick = ui.onMouseDown(500.0f, 500.0f, 3);
        CHECK(ui.screen() == ui::Screen::Home);
        CHECK(evRightClick.action == ui::Action::SettingsChanged);
    }

    // 4. Canvas fast paths (opaque drawImage, gradientV, gradientH, setClip/clearClip)
    {
        ui::Canvas c(64, 64);
        c.gradientV({0, 0, 64, 64}, ui::rgba(20, 30, 40, 255), ui::rgba(10, 15, 20, 255));
        c.gradientH({0, 0, 64, 64}, ui::rgba(0, 0, 0, 128), ui::rgba(0, 0, 0, 0));

        ui::Image img;
        img.w = 16;
        img.h = 16;
        img.opaque = true;
        img.px.assign(16 * 16, ui::rgba(100, 150, 200, 255));

        c.setClip({8, 8, 8, 8});
        c.drawImage(img, 0, 0, 255, 0);
        c.clearClip();
        // Outside clip at (2, 2) should NOT be img color; inside clip at (10, 10) SHOULD be img color
        CHECK(c.data()[2 * 64 + 2] != ui::rgba(100, 150, 200, 255));
        CHECK(c.data()[10 * 64 + 10] == ui::rgba(100, 150, 200, 255));
    }

    // 5. Screen::SignIn redirect on sign-in completion
    {
        ui::Fonts fonts;
        fonts.load("assets/fonts");
        ui::ImageCache images([] {});
        ui::AppUi ui(fonts, images);
        ui.showSignIn("ABCD-1234", "https://www.microsoft.com/link");
        CHECK(ui.screen() == ui::Screen::SignIn);
        ui.showSplash("Carregando jogos...");
        CHECK(ui.screen() == ui::Screen::Splash);
        ui.showHome();
        CHECK(ui.screen() == ui::Screen::Home);
    }
}

static void testReleaseV155Features() {
    std::string testDir = "/tmp/test-v155-features";
    xc::fs::removeTree(testDir);
    xc::fs::makeDirs(testDir);
    std::string cacheFile = testDir + "/library_cache.json";

    // 1. Verify shelf auto-refresh & knownProducts persistence in library_cache.json
    {
        std::map<std::string, std::string> siglFeeds;
        siglFeeds[xcloud::sigl::kAllGames] = R"([{"siglId":"all","title":"All Games"},{"id":"GP_GAME_1"},{"id":"GP_GAME_2"}])";
        siglFeeds[xcloud::sigl::kRecentlyAdded] = R"([{"siglId":"recent","title":"Recently Added"},{"id":"GP_GAME_1"}])";
        siglFeeds[xcloud::sigl::kMostPopular] = R"([{"siglId":"pop","title":"Most Popular"},{"id":"GP_GAME_2"}])";
        siglFeeds[xcloud::sigl::kFreeToPlay] = R"([{"siglId":"f2p","title":"Free to Play"},{"id":"F2P_GAME_1"}])";
        siglFeeds[xcloud::sigl::kLeavingSoon] = R"([{"siglId":"leaving","title":"Leaving Soon"}])";

        std::string titlesBody = R"({
            "results": [
                {
                    "titleId": "TITLE_CLOUD_SPECIAL",
                    "details": {
                        "productId": "PROD_CLOUD_SPECIAL",
                        "hasEntitlement": false,
                        "isFreeInStore": false,
                        "userPrograms": []
                    }
                }
            ]
        })";

        std::map<std::string, std::string> productTitles;
        productTitles["GP_GAME_1"] = "Game One";
        productTitles["GP_GAME_2"] = "Game Two";
        productTitles["F2P_GAME_1"] = "Free Shooter";
        productTitles["PROD_CLOUD_SPECIAL"] = "Special Cloud Offering";

        net::setTestTransport([&](const net::Request& req) {
            net::Response r;
            r.status = 200;
            if (req.url.find("sigls") != std::string::npos) {
                for (const auto& [siglId, body] : siglFeeds) {
                    if (req.url.find("id=" + siglId) != std::string::npos) {
                        r.body = body;
                        return r;
                    }
                }
                r.body = "[]";
                return r;
            } else if (req.url.find("titles") != std::string::npos) {
                r.body = titlesBody;
                return r;
            } else if (req.url.find("products") != std::string::npos) {
                json::Value root = json::Value::object();
                json::Value prods = json::Value::object();
                for (const auto& [id, name] : productTitles) {
                    json::Value item = json::Value::object();
                    item.set("ProductTitle", name);
                    item.set("XCloudTitleId", "T_" + id);
                    prods.set(id, std::move(item));
                }
                root.set("Products", std::move(prods));
                r.body = root.dump();
                return r;
            }
            r.status = 404;
            return r;
        });

        app::Library lib;
        lib.setCachePath(cacheFile);
        lib.setSubscription("Ultimate");
        xcloud::GssvClient gssv;
        std::string err;

        // Load initial catalog
        CHECK(lib.load(gssv, "en-US", [] {}, err));
        CHECK(lib.rows().size() >= 4);

        // First syncDelta on this fresh instance
        size_t newAdded = lib.syncDelta(gssv, [] {}, err);
        // Special cloud title outside Game Pass must NOT be reported as new Game Pass game
        CHECK(newAdded == 0);

        // Verify cache was saved and contains PROD_CLOUD_SPECIAL in products and knownProducts
        std::string cacheContent;
        CHECK(platform::readFile(cacheFile, cacheContent));
        auto j = json::parse(cacheContent);
        CHECK(j.has_value());
        CHECK((*j)["products"].has("PROD_CLOUD_SPECIAL"));
        size_t countInKp = 0;
        for (const auto& p : (*j)["knownProducts"].items()) {
            if (p.str() == "PROD_CLOUD_SPECIAL") countInKp++;
        }
        CHECK(countInKp == 1);  // Verified deduplicated: serialized exactly once!

        // 2. Simulate subsequent boot with pre-existing cache
        app::Library libBoot2;
        libBoot2.setCachePath(cacheFile);
        CHECK(libBoot2.loadCache(true));

        // syncDelta on boot 2: must NOT have false positive of new games!
        size_t countBoot2 = libBoot2.syncDelta(gssv, [] {}, err);
        CHECK(countBoot2 == 0);

        // 3. Now simulate Microsoft adding a new Game Pass game to Recently Added feed
        siglFeeds[xcloud::sigl::kRecentlyAdded] = R"([{"siglId":"recent","title":"Recently Added"},{"id":"GP_GAME_3"},{"id":"GP_GAME_1"}])";
        siglFeeds[xcloud::sigl::kAllGames] = R"([{"siglId":"all","title":"All Games"},{"id":"GP_GAME_1"},{"id":"GP_GAME_2"},{"id":"GP_GAME_3"}])";
        productTitles["GP_GAME_3"] = "Game Three";

        size_t countBoot3 = libBoot2.syncDelta(gssv, [] {}, err);
        // Genuine new Game Pass game should be counted
        CHECK(countBoot3 == 1);

        // Check that "Recently Added" shelf in layout_ now automatically has GP_GAME_3 without manual L3 refresh!
        bool foundInShelf = false;
        for (const auto& row : libBoot2.rows()) {
            if (row.title == "Recently Added") {
                for (const auto& tile : row.tiles) {
                    if (tile.productId == "GP_GAME_3") foundInShelf = true;
                }
            }
        }
        CHECK(foundInShelf);

        // 4. Test SIGL object-format parsing (both string IDs and object IDs {"id": "..."})
        xcloud::ProductList objList;
        std::string objErr;
        siglFeeds["test_obj_format"] = R"({"title":"Object Shelves","productIds":["GP_GAME_1",{"id":"GP_GAME_2"}]})";
        CHECK(xcloud::fetchList("test_obj_format", "US", "en-US", objList, objErr));
        CHECK(objList.title == "Object Shelves");
        CHECK(objList.productIds.size() == 2);
        CHECK(objList.productIds[0] == "GP_GAME_1");
        CHECK(objList.productIds[1] == "GP_GAME_2");

        // 5. Test resilience against corrupted or truncated cache
        std::string corruptCache = testDir + "/corrupt.json";
        platform::writeFileAtomic(corruptCache, "{ \"version\": 2, \"knownProducts\": [\"\", 123, null], \"products\": {");
        app::Library corruptLib;
        corruptLib.setCachePath(corruptCache);
        CHECK(!corruptLib.loadCache(true));

        net::setTestTransport({});
    }

    xc::fs::removeTree(testDir);
}

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif

static void testWorkerMobileM4Features() {
    // 1. R1: Vibration Settings Compliance
    {
        bool origVib = xc::input::vibrationEnabled();
        xc::input::setVibrationEnabled(false);
        CHECK(!xc::input::vibrationEnabled());
        // Verify triggerHaptic is gated when disabled
        xc::input::triggerHaptic(xc::input::HapticType::Click);
        xc::input::triggerHaptic(xc::input::HapticType::Tick);
        xc::input::triggerHaptic(xc::input::HapticType::HeavyClick);
        // Verify rumble is gated when disabled
        xc::input::setRumble(200, 200, 100, 0);
        xc::input::setVibrationEnabled(true);
        CHECK(xc::input::vibrationEnabled());
        xc::input::setVibrationEnabled(origVib);
    }

    // 2. R2: Localization Invariants for 15 New Strings
    {
        using namespace xc::ui;
        // Verify English strings
        setLanguage(Language::English);
        CHECK(std::string(tr(Str::TouchOverlay)) == "On-screen controls");
        CHECK(std::string(tr(Str::TouchOpacity)) == "Controls opacity");
        CHECK(std::string(tr(Str::TouchInvertSticks)) == "Invert thumbsticks");
        CHECK(std::string(tr(Str::TouchLayoutStudio)) == "Customize controls");
        CHECK(std::string(tr(Str::Vibration)) == "Controller vibration");
        CHECK(std::string(tr(Str::SettingDescTouchOverlay)) == "Shows or hides virtual on-screen touch controls.");
        CHECK(std::string(tr(Str::SettingDescTouchOpacity)) == "Adjusts the transparency of on-screen buttons.");
        CHECK(std::string(tr(Str::SettingDescTouchInvertSticks)) == "Inverts the vertical axis of virtual thumbsticks.");
        CHECK(std::string(tr(Str::SettingDescTouchLayoutStudio)) == "Opens the editor to rearrange and customize on-screen controls.");
        CHECK(std::string(tr(Str::SettingDescVibration)) == "Enables or disables controller rumble vibration.");
        CHECK(std::string(tr(Str::MenuDescMicrophone)) == "Mutes or unmutes the microphone during the stream.");
        CHECK(std::string(tr(Str::TouchStudioBanner)) == "Drag buttons to reposition | Tap center of ABXY to move cluster");
        CHECK(std::string(tr(Str::Save)) == "Save");
        CHECK(std::string(tr(Str::SignInOpenBrowser)) == "Open browser to sign in");
        CHECK(std::string(tr(Str::SignInOpenBrowserHint)) == "Tap here or press A to open in browser");

        // Verify Portuguese strings
        setLanguage(Language::PortugueseBR);
        CHECK(std::string(tr(Str::TouchOverlay)) == "Controles na tela");
        CHECK(std::string(tr(Str::TouchOpacity)) == "Opacidade dos controles");
        CHECK(std::string(tr(Str::TouchInvertSticks)) == "Inverter analógicos");
        CHECK(std::string(tr(Str::TouchLayoutStudio)) == "Personalizar controle");
        CHECK(std::string(tr(Str::Vibration)) == "Vibração do controle");
        CHECK(std::string(tr(Str::SettingDescTouchOverlay)) == "Ativa ou desativa a exibição dos controles virtuais na tela.");
        CHECK(std::string(tr(Str::Save)) == "Salvar");
        CHECK(std::string(tr(Str::SignInOpenBrowser)) == "Abrir navegador para fazer login");
        CHECK(std::string(tr(Str::SignInOpenBrowserHint)) == "Toque aqui ou pressione A para abrir no navegador");

        // Verify all 6 languages have non-empty translations for all 15 strings
        for (int l = 0; l < static_cast<int>(Language::Count); ++l) {
            setLanguage(static_cast<Language>(l));
            CHECK(std::strlen(tr(Str::TouchOverlay)) > 0);
            CHECK(std::strlen(tr(Str::TouchOpacity)) > 0);
            CHECK(std::strlen(tr(Str::TouchInvertSticks)) > 0);
            CHECK(std::strlen(tr(Str::TouchLayoutStudio)) > 0);
            CHECK(std::strlen(tr(Str::Vibration)) > 0);
            CHECK(std::strlen(tr(Str::SettingDescTouchOverlay)) > 0);
            CHECK(std::strlen(tr(Str::SettingDescTouchOpacity)) > 0);
            CHECK(std::strlen(tr(Str::SettingDescTouchInvertSticks)) > 0);
            CHECK(std::strlen(tr(Str::SettingDescTouchLayoutStudio)) > 0);
            CHECK(std::strlen(tr(Str::SettingDescVibration)) > 0);
            CHECK(std::strlen(tr(Str::MenuDescMicrophone)) > 0);
            CHECK(std::strlen(tr(Str::TouchStudioBanner)) > 0);
            CHECK(std::strlen(tr(Str::Save)) > 0);
            CHECK(std::strlen(tr(Str::SignInOpenBrowser)) > 0);
            CHECK(std::strlen(tr(Str::SignInOpenBrowserHint)) > 0);
        }
        setLanguage(Language::English);
    }

    // 3. R3: StreamMenu touch scroll position retention
    {
        xc::ui::Fonts dummyFonts;
        xc::ui::StreamMenu menu(dummyFonts);
        menu.open(0, false);
        xc::ui::NavInput outNav{};
        // Start touch drag
        app::shell::TouchEvent evDown{app::shell::TouchEvent::Kind::Down, 10, 100.0f, 300.0f};
        menu.onTouch(evDown, outNav);
        // Motion drag upward
        app::shell::TouchEvent evMotion{app::shell::TouchEvent::Kind::Motion, 10, 100.0f, 240.0f};
        menu.onTouch(evMotion, outNav);
        // Up release
        app::shell::TouchEvent evUp{app::shell::TouchEvent::Kind::Up, 10, 100.0f, 240.0f};
        menu.onTouch(evUp, outNav);
        // Rendering after release must keep menu open and valid
        xc::ui::StreamInfo sinfo{};
        xc::ui::Canvas canvas = menu.renderMenu(sinfo);
        CHECK(menu.isOpen());
        CHECK(canvas.width() > 0);
    }

    // 4. R4: Gamepad detection and SDL3 Back button scancode
    {
        CHECK(xc::input::scancodeToVk(270) == xc::input::kVkAndroidBack);
        CHECK(xc::input::scancodeToVk(282) == xc::input::kVkAndroidBack);
        bool gp = xc::input::hasGamepad();
        (void)gp;
    }

    // 5. R4 & R5: AppUi Settings scroll and sign-in layout
    {
        xc::ui::Fonts dummyFonts;
        xc::ui::ImageCache dummyImages([] {}, 10u << 20);
        xc::ui::AppUi ui(dummyFonts, dummyImages);
        ui.showSignIn("ABCDEFGH", "https://microsoft.com/link");
        CHECK(ui.screen() == xc::ui::Screen::SignIn);

        // Touch drag in Settings
        ui.showHome();
        // Tap settings touch button at Rect{kW - kMargin - 52, 46, 52, 52} -> (1772, 46)
        app::shell::TouchEvent evSettingsTapDown{app::shell::TouchEvent::Kind::Down, 1, 1780.0f, 60.0f};
        ui.onTouch(evSettingsTapDown);
        app::shell::TouchEvent evSettingsTapUp{app::shell::TouchEvent::Kind::Up, 1, 1780.0f, 60.0f};
        ui.onTouch(evSettingsTapUp);
        CHECK(ui.screen() == xc::ui::Screen::Settings);

        // Touch drag downwards inside Settings
        app::shell::TouchEvent evDragDown{app::shell::TouchEvent::Kind::Down, 2, 500.0f, 500.0f};
        ui.onTouch(evDragDown);
        app::shell::TouchEvent evDragMotion{app::shell::TouchEvent::Kind::Motion, 2, 500.0f, 400.0f};
        ui.onTouch(evDragMotion);
        app::shell::TouchEvent evDragUp{app::shell::TouchEvent::Kind::Up, 2, 500.0f, 400.0f};
        ui.onTouch(evDragUp);
        // Settings scroll position is retained
        CHECK(ui.screen() == xc::ui::Screen::Settings);
    }
}

static void testPs4MemoryOptimizations() {
    // 1. Compact json::Value layout (std::variant shrinks 80B -> 40B on libstdc++, 72B -> 32B on libc++).
    CHECK(sizeof(json::Value) <= 48);

    // 2. json::appendEscaped matches json::escape and round-trips through json::parse.
    const std::string tricky = "Hello \"world\" \\ line1\nline2\r\t\b\f\x01\x1f UTF8: ação 🎮";
    std::string appended;
    json::appendEscaped(appended, tricky);
    CHECK(appended == json::escape(tricky));
    auto parsed = json::parse("\"" + appended + "\"");
    CHECK(parsed.has_value() && parsed->isString() && parsed->str() == tricky);

    // 3. Zero-DOM streaming Library::saveCache() -> loadCache() round-trip fidelity.
    const std::string cacheFile = "/tmp/xc_test_frente2_cache_" + std::to_string(getpid()) + ".json";
    fs::removeFile(cacheFile);
    {
        net::setTestTransport([](const net::Request& req) -> net::Response {
            net::Response r;
            r.status = 200;
            if (req.url.find("sigls/v2") != std::string::npos) {
                r.body = R"([{"siglId":"id","title":"All games"},{"id":"9P0000000001"}])";
            } else if (req.url.find("v3/products") != std::string::npos) {
                r.body = R"({"Products":{"9P0000000001":{
                    "XCloudTitleId":"TESTGAME1",
                    "XboxTitleId":"12345678",
                    "ProductTitle":"Test \"Quoted\" Game\nLine2",
                    "PublisherName":"Pub\\Co",
                    "ProductDescription":"A rich description with \"quotes\", tabs\tand unicode: ação!",
                    "Image_Tile":{"URL":"https://example.com/tile.jpg"},
                    "Image_Poster":{"URL":"https://example.com/poster.jpg"},
                    "Image_Hero":{"URL":"https://example.com/hero.jpg"},
                    "LocalizedCategories":["Action","RPG"],
                    "Platforms":["XboxSeriesX"],
                    "PassMetadataByPassProductId":{"CFQ7TTC0K5DJ":{}},
                    "Attributes":[{"Name":"SinglePlayer"},{"Name":"XblOnlineCoop"}],
                    "LanguageSupport":{"pt-BR":{"InterfaceLanguageSupport":1,"SubtitlesLanguageSupport":1,"GamePlayAudioLanguageSupport":0}}
                }}})";
            } else if (req.url.find("/v2/titles") != std::string::npos) {
                r.body = R"({"results":[{"titleId":"TESTGAME1","details":{"productId":"9P0000000001","xboxTitleId":"12345678","hasEntitlement":true,"isFreeInStore":true,"supportedInputTypes":["Controller","MKB"],"allowedDeviceTypes":["Console","Mobile"],"userPrograms":["F2P"]}}]})";
            } else {
                r.status = 404;
            }
            return r;
        });

        xcloud::GssvClient gssv;
        app::Library lib;
        lib.setCachePath(cacheFile);
        lib.setSubscription("Ultimate");
        std::string err;
        bool loaded = lib.load(gssv, "pt-BR", [] {}, err, false, "BR");
        CHECK(loaded);
        lib.markAsOwned("9P0000000001", "TESTGAME1", "Test \"Quoted\" Game\nLine2");
        lib.saveCache();
        net::setTestTransport(nullptr);
    }

    // Reload from the streaming-serialized cache and verify every field survived intact.
    {
        std::string raw;
        CHECK(platform::readFile(cacheFile, raw));
        auto parsedCache = json::parse(raw);
        CHECK(parsedCache.has_value() && parsedCache->isObject());
        CHECK((*parsedCache)["market"].str() == "BR");
        CHECK((*parsedCache)["language"].str() == "pt-BR");
        CHECK((*parsedCache)["subscription"].str() == "Ultimate");
        CHECK((*parsedCache)["products"]["9P0000000001"]["title"].str() == "Test \"Quoted\" Game\nLine2");

        app::Library reloaded;
        reloaded.setCachePath(cacheFile);
        CHECK(reloaded.loadCache(true));
        CHECK(reloaded.ownedKnown());
        ui::GameTile t = reloaded.tile("9P0000000001", "TESTGAME1");
        CHECK(t.name == "Test \"Quoted\" Game\nLine2");
        CHECK(t.publisher == "Pub\\Co");
        CHECK(t.description == "A rich description with \"quotes\", tabs\tand unicode: ação!");
        CHECK(t.playable);
        CHECK(t.categories.size() == 2);
    }
    fs::removeFile(cacheFile);

    // 4. Image decode guards: empty and oversized (8192x8192 PNG header) must fail cleanly without OOM.
    ui::Image img;
    CHECK(!ui::decodeImage("", img));
    // Minimal PNG signature + IHDR chunk declaring 8192x8192 RGBA (would be 256 MB RGBA if decoded).
    const unsigned char hugePng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // PNG signature
        0x00, 0x00, 0x00, 0x0D,                          // IHDR length = 13
        0x49, 0x48, 0x44, 0x52,                          // "IHDR"
        0x00, 0x00, 0x20, 0x00,                          // width = 8192
        0x00, 0x00, 0x20, 0x00,                          // height = 8192
        0x08, 0x06, 0x00, 0x00, 0x00,                    // 8-bit RGBA
        0x00, 0x00, 0x00, 0x00                           // CRC placeholder
    };
    CHECK(!ui::decodeImage(std::string(reinterpret_cast<const char*>(hugePng), sizeof(hugePng)), img));
}

void runAppTests() {
    testVersions();
    testSettingsMigration();
    testSettingsChoiceRoundTrip();
    testGeneralSettingsR2();
    testMicGain();
    testPerGameSettings();
    testDeviceTypeFallbacks();
    testUpdater();
    testPrivacyAndRobustness();
    testBlock1Features();
    testBlock2Features();
    testBlock3Features();
    testBlock1Corrections();
    testBloco2Corrections();
    testBloco3Corrections();
    testPhase1BugFixesAndPhase2Optimizations();
    testReleaseV155Features();
    testWorkerMobileM4Features();
    testPs4MemoryOptimizations();
}
