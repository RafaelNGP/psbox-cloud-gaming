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

void testWebStreamingAndGamepad() {
    xc::app::WebServer srv;
    bool streamingFlag = false;
    std::string currentQuery = "";
    std::string currentTab = "gamepass";
    json::Value currentFilters = json::Value::object();

    xc::app::WebServerHooks hooks;
    hooks.isStreaming = [&]() { return streamingFlag; };
    hooks.getSearchParams = [&](std::string& q, std::string& tab, json::Value& f) {
        q = currentQuery;
        tab = currentTab;
        f = currentFilters;
    };
    hooks.setSearchParams = [&](const std::string* q, const std::string* tab, const json::Value* f) {
        if (q) currentQuery = *q;
        if (tab) currentTab = *tab;
        if (f) currentFilters = *f;
    };
    xc::app::setWebServerHooks(hooks);

    // 1. GET /api/status when not streaming
    {
        auto res = srv.handleRequest("GET", "/api/status", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j)["ok"].asBool(false) == true);
        CHECK((*j)["streaming"].asBool(true) == false);
    }

    // 2. POST /api/search when not streaming should succeed
    {
        auto res = srv.handleRequest("POST", "/api/search", "{\"query\":\"halo\",\"tab\":\"gamepass\"}");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["ok"].asBool(false) == true);
        CHECK(currentQuery == "halo");
    }

    // 3. GET /api/status when streaming is active
    streamingFlag = true;
    {
        auto res = srv.handleRequest("GET", "/api/status", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["streaming"].asBool(false) == true);
    }

    // 4. POST /api/search when streaming must be blocked with 409 Conflict
    {
        auto res = srv.handleRequest("POST", "/api/search", "{\"query\":\"gears\",\"tab\":\"gamepass\"}");
        CHECK(res.status == 409);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["ok"].asBool(true) == false);
        CHECK((*j)["error"].asString() == "streaming_active");
        CHECK(currentQuery == "halo"); // Search state was preserved and not changed
    }

    // 5. Virtual gamepad: Player 1 (main controller)
    {
        xc::app::resetVirtualGamepads();
        input::ControllerState s0;
        bool conn0 = false;
        xc::app::virtualGamepadState(0, s0, conn0);
        CHECK(!conn0);

        auto res = srv.handleRequest("POST", "/api/gamepad",
            "{\"player\":1,\"btnA\":true,\"leftStickX\":0.75,\"leftStickY\":-0.5,\"btnNexus\":true}");
        CHECK(res.status == 200);
        bool ok = xc::app::virtualGamepadState(0, s0, conn0);
        CHECK(ok);
        CHECK(conn0);
        CHECK(s0.btnA == true);
        CHECK(s0.btnB == false);
        CHECK(s0.btnNexus == true);
        CHECK(std::abs(s0.leftStickX - 0.75f) < 0.01f);
        CHECK(std::abs(s0.leftStickY - (-0.5f)) < 0.01f);
    }

    // 6. Virtual gamepad: Player 2 (co-op controller)
    {
        input::ControllerState s1;
        bool conn1 = false;
        xc::app::virtualGamepadState(1, s1, conn1);
        CHECK(!conn1);

        auto res = srv.handleRequest("POST", "/api/gamepad",
            "{\"player\":2,\"btnB\":true,\"triggerR2\":0.9,\"btnView\":true,\"dpadUp\":true}");
        CHECK(res.status == 200);
        bool ok = xc::app::virtualGamepadState(1, s1, conn1);
        CHECK(ok);
        CHECK(conn1);
        CHECK(s1.btnB == true);
        CHECK(s1.btnA == false);
        CHECK(s1.btnTouchpad == true); // btnView maps to btnTouchpad
        CHECK(s1.dpadUp == true);
        CHECK(std::abs(s1.triggerR2 - 0.9f) < 0.01f);
        // Test L3 and R3 buttons on Virtual Gamepad
        input::ControllerState s0;
        bool conn0 = false;
        auto resL3R3 = srv.handleRequest("POST", "/api/gamepad",
            "{\"player\":1,\"btnL3\":true,\"btnR3\":true}");
        CHECK(resL3R3.status == 200);
        bool okL3R3 = xc::app::virtualGamepadState(0, s0, conn0);
        CHECK(okL3R3);
        CHECK(s0.btnL3 == true);
        CHECK(s0.btnR3 == true);
    }

    // 7. Reset virtual gamepads
    {
        xc::app::resetVirtualGamepads();
        input::ControllerState s0, s1;
        bool conn0 = false, conn1 = false;
        xc::app::virtualGamepadState(0, s0, conn0);
        xc::app::virtualGamepadState(1, s1, conn1);
        CHECK(!conn0);
        CHECK(!conn1);
    }

    // 8. Test AppUi::detailWanted prevents infinite re-fetching when detailInfo_ exists
    {
        ui::Fonts dummyFonts{};
        ui::ImageCache images([] {});
        ui::AppUi uiApp(dummyFonts, images);
        ui::GameTile tile;
        tile.productId = "PROD_UNRATED";
        tile.name = "Unrated Game";
        tile.description = "Short desc";
        tile.heroUrl = "http://example.com/hero.jpg";
        tile.ratingAge = ""; // Empty age rating

        uiApp.showDetails(tile);
        // First wanted call should ask for details
        std::string asked = uiApp.detailWanted(1000);
        CHECK(asked == "PROD_UNRATED");

        // Simulate priceLoop fetching details and storing in detailInfo_
        uiApp.setDetailInfo("PROD_UNRATED", "Longer desc", "Publisher X", {"Indie"}, "http://example.com/hero.jpg",
                            "", "", {});

        // Subsequent detailWanted calls (even after 5000ms) must NOT ask again
        std::string askedAfter = uiApp.detailWanted(10000);
        CHECK(askedAfter.empty());
    }

    xc::app::setWebServerHooks({});
}

static void testWebSettingsAndStreamMenu() {
    xc::app::WebServer srv;
    bool streamMenuUpdated = false;
    std::string lastStreamAction;
    json::Value lastStreamUpdate;
    json::Value currentSettings = json::Value::object();
    currentSettings.set("bitrateMbps", 25);
    currentSettings.set("resolution", "1080p");
    currentSettings.set("voiceRouting", 1);

    xc::app::WebServerHooks hooks;
    hooks.isStreaming = []() { return true; };
    hooks.getSettings = [&](json::Value& out) {
        out = currentSettings;
    };
    hooks.updateSettings = [&](const json::Value& root) {
        if (root.has("bitrateMbps")) currentSettings.set("bitrateMbps", root["bitrateMbps"].asInt());
        if (root.has("voiceRouting")) currentSettings.set("voiceRouting", root["voiceRouting"].asInt());
        return true;
    };
    hooks.getTelemetry = [](json::Value& stats) {
        stats = json::Value::object();
        stats.set("fps", 60.0);
        stats.set("decodeMs", 3.2);
        stats.set("rtt", 14.5);
        stats.set("packetLoss", 0.0);
        stats.set("bitrateKbps", 15200);
    };
    hooks.getStreamMenuState = [](json::Value& state) {
        state = json::Value::object();
        state.set("volume", 80);
        state.set("micMuted", false);
        state.set("sharpness", 2);
        state.set("casSharpness", 3);
        state.set("deband", true);
        state.set("stats", true);
        state.set("statsMode", 1);
        state.set("touchPadMouse", false);
        state.set("virtualGamepad", true);
    };
    hooks.updateStreamMenu = [&](const json::Value& update) {
        streamMenuUpdated = true;
        lastStreamUpdate = update;
        return true;
    };
    hooks.streamAction = [&](const std::string& action) {
        lastStreamAction = action;
    };
    xc::app::setWebServerHooks(hooks);

    // 1. GET /api/settings
    {
        auto res = srv.handleRequest("GET", "/api/settings", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j)["bitrateMbps"].asInt() == 25);
        CHECK((*j)["resolution"].str() == "1080p");
        CHECK((*j)["voiceRouting"].asInt() == 1);
    }

    // 2. POST /api/settings
    {
        auto res = srv.handleRequest("POST", "/api/settings", R"({"bitrateMbps":40,"voiceRouting":2})");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["ok"].asBool() == true);
        CHECK(currentSettings["bitrateMbps"].asInt() == 40);
        CHECK(currentSettings["voiceRouting"].asInt() == 2);
    }

    // 3. GET /api/stats
    {
        auto res = srv.handleRequest("GET", "/api/stats", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j)["fps"].asNumber() == 60.0);
        CHECK((*j)["decodeMs"].asNumber() == 3.2);
        CHECK((*j)["rtt"].asNumber() == 14.5);
    }

    // 4. GET /api/stream
    {
        auto res = srv.handleRequest("GET", "/api/stream", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && j->isObject());
        CHECK((*j)["volume"].asInt() == 80);
        CHECK((*j)["micMuted"].asBool() == false);
        CHECK((*j)["sharpness"].asInt() == 2);
        CHECK((*j)["casSharpness"].asInt() == 3);
        CHECK((*j)["virtualGamepad"].asBool() == true);
    }

    // 5. POST /api/stream with action
    {
        lastStreamAction.clear();
        auto res = srv.handleRequest("POST", "/api/stream", R"({"action":"disconnect"})");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j && (*j)["ok"].asBool() == true);
        CHECK(lastStreamAction == "disconnect");
    }

    // 6. POST /api/stream with stream menu update
    {
        streamMenuUpdated = false;
        auto res = srv.handleRequest("POST", "/api/stream", R"({"volume":65,"sharpness":1})");
        CHECK(res.status == 200);
        CHECK(streamMenuUpdated == true);
        CHECK(lastStreamUpdate["volume"].asInt() == 65);
        CHECK(lastStreamUpdate["sharpness"].asInt() == 1);
    }

    // 7. Error handling: invalid JSON
    {
        auto res = srv.handleRequest("POST", "/api/stream", "invalid{json");
        CHECK(res.status == 400);
    }

    xc::app::setWebServerHooks({});
}

static void testWebSocketRFC6455() {
    // 1. RFC 6455 Handshake Accept Key test vector
    // Standard test vector from RFC 6455 Section 4.2.2:
    // Key: "dGhlIHNhbXBsZSBub25jZQ==" -> Accept: "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    std::string accept = app::ws::computeAccept("dGhlIHNhbXBsZSBub25jZQ==");
    CHECK(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    // 2. Unmasked Text Frame
    std::string textMsg = "{\"type\":\"ping\"}";
    std::vector<uint8_t> frameText = app::ws::makeTextFrame(textMsg);
    app::ws::Frame parsed;
    size_t consumed = 0;
    CHECK(app::ws::parseFrame(frameText.data(), frameText.size(), parsed, consumed));
    CHECK(consumed == frameText.size());
    CHECK(parsed.fin);
    CHECK(parsed.opcode == app::ws::Opcode::Text);
    CHECK(std::string(parsed.payload.begin(), parsed.payload.end()) == textMsg);

    // 3. Masked Text Frame (as sent from client / browser)
    uint8_t maskKey[4] = {0x37, 0xfa, 0x21, 0x3d};
    std::string rawPayload = "Hello WebSocket";
    std::vector<uint8_t> maskedFrame;
    maskedFrame.push_back(static_cast<uint8_t>(0x81)); // FIN + Text
    maskedFrame.push_back(static_cast<uint8_t>(0x80 | rawPayload.size())); // Masked + len
    for (int i = 0; i < 4; ++i) maskedFrame.push_back(maskKey[i]);
    for (size_t i = 0; i < rawPayload.size(); ++i) {
        maskedFrame.push_back(static_cast<uint8_t>(rawPayload[i] ^ maskKey[i % 4]));
    }
    CHECK(app::ws::parseFrame(maskedFrame.data(), maskedFrame.size(), parsed, consumed));
    CHECK(consumed == maskedFrame.size());
    CHECK(std::string(parsed.payload.begin(), parsed.payload.end()) == rawPayload);

    // 4. Binary Frame
    std::vector<uint8_t> binData = {0x00, 0x01, 0x02, 0x03, 0x04, 0xFF, 0xFE};
    std::vector<uint8_t> frameBin = app::ws::makeBinaryFrame(binData.data(), binData.size());
    CHECK(app::ws::parseFrame(frameBin.data(), frameBin.size(), parsed, consumed));
    CHECK(parsed.opcode == app::ws::Opcode::Binary);
    CHECK(parsed.payload == binData);

    // 5. Ping & Pong Frames
    std::string pingStr = "heartbeat";
    std::vector<uint8_t> pingPayload(pingStr.begin(), pingStr.end());
    std::vector<uint8_t> framePing = app::ws::makeFrame(app::ws::Opcode::Ping, pingPayload.data(), pingPayload.size(), true);
    CHECK(app::ws::parseFrame(framePing.data(), framePing.size(), parsed, consumed));
    CHECK(parsed.opcode == app::ws::Opcode::Ping);
    CHECK(parsed.payload == pingPayload);

    std::vector<uint8_t> framePong = app::ws::makePongFrame(pingPayload);
    CHECK(app::ws::parseFrame(framePong.data(), framePong.size(), parsed, consumed));
    CHECK(parsed.opcode == app::ws::Opcode::Pong);
    CHECK(parsed.payload == pingPayload);

    // 6. Close Frame
    std::vector<uint8_t> frameClose = app::ws::makeCloseFrame(1000);
    CHECK(app::ws::parseFrame(frameClose.data(), frameClose.size(), parsed, consumed));
    CHECK(parsed.opcode == app::ws::Opcode::Close);

    // 7. parseGamepadJson test
    std::string gpJson = R"({
        "buttons": {
            "a": true, "b": false, "x": true, "y": false,
            "lb": true, "rb": false, "view": true, "menu": false,
            "dpad_up": true, "dpad_down": false, "dpad_left": false, "dpad_right": true
        },
        "axes": {
            "lx": -0.5, "ly": 0.75, "rx": 0.25, "ry": -0.8,
            "lt": 0.6, "rt": 1.0
        }
    })";
    auto j = json::parse(gpJson);
    CHECK(j.has_value());
    int slot = -1;
    input::ControllerState cs{};
    CHECK(app::parseGamepadJson(*j, slot, cs));
    CHECK(slot == 0);
    CHECK(cs.btnA == true);
    CHECK(cs.btnB == false);
    CHECK(cs.btnX == true);
    CHECK(cs.btnY == false);
    CHECK(cs.btnL1 == true);
    CHECK(cs.btnR1 == false);
    CHECK(cs.btnTouchpad == true);
    CHECK(cs.btnOptions == false);
    CHECK(cs.dpadUp == true);
    CHECK(cs.dpadRight == true);
    CHECK(std::abs(cs.leftStickX - (-0.5f)) < 0.01f);
    CHECK(std::abs(cs.leftStickY - 0.75f) < 0.01f);
    CHECK(std::abs(cs.rightStickX - 0.25f) < 0.01f);
    CHECK(std::abs(cs.rightStickY - (-0.8f)) < 0.01f);
    CHECK(std::abs(cs.triggerL2 - 0.6f) < 0.01f);
    CHECK(std::abs(cs.triggerR2 - 1.0f) < 0.01f);

    // 8. W3C Gamepad API standard arrays
    std::string w3cJson = R"({
        "player": 2,
        "buttons": [
            {"pressed": true, "value": 1.0},
            {"pressed": false, "value": 0.0},
            {"pressed": true, "value": 1.0},
            {"pressed": false, "value": 0.0},
            {"pressed": true, "value": 1.0},
            {"pressed": false, "value": 0.0},
            {"pressed": true, "value": 0.85},
            {"pressed": true, "value": 0.95},
            {"pressed": true, "value": 1.0},
            {"pressed": false, "value": 0.0},
            {"pressed": false, "value": 0.0},
            {"pressed": true, "value": 1.0},
            {"pressed": true, "value": 1.0},
            {"pressed": false, "value": 0.0},
            {"pressed": false, "value": 0.0},
            {"pressed": true, "value": 1.0},
            {"pressed": true, "value": 1.0}
        ],
        "axes": [0.45, -0.65, -0.3, 0.8]
    })";
    auto jW3c = json::parse(w3cJson);
    CHECK(jW3c.has_value());
    int w3cSlot = -1;
    input::ControllerState csW3c{};
    CHECK(app::parseGamepadJson(*jW3c, w3cSlot, csW3c));
    CHECK(w3cSlot == 1); // player 2 -> slot 1
    CHECK(csW3c.btnA == true);
    CHECK(csW3c.btnB == false);
    CHECK(csW3c.btnX == true);
    CHECK(csW3c.btnY == false);
    CHECK(csW3c.btnL1 == true);
    CHECK(csW3c.btnR1 == false);
    CHECK(std::abs(csW3c.triggerL2 - 0.85f) < 0.01f);
    CHECK(std::abs(csW3c.triggerR2 - 0.95f) < 0.01f);
    CHECK(csW3c.btnTouchpad == true); // index 8 View
    CHECK(csW3c.btnOptions == false); // index 9 Menu
    CHECK(csW3c.btnL3 == false);       // index 10
    CHECK(csW3c.btnR3 == true);        // index 11
    CHECK(csW3c.dpadUp == true);       // index 12
    CHECK(csW3c.dpadDown == false);    // index 13
    CHECK(csW3c.dpadLeft == false);    // index 14
    CHECK(csW3c.dpadRight == true);    // index 15
    CHECK(csW3c.btnNexus == true);     // index 16 Guide
    CHECK(std::abs(csW3c.leftStickX - 0.45f) < 0.01f);
    CHECK(std::abs(csW3c.leftStickY - (-0.65f)) < 0.01f);
    CHECK(std::abs(csW3c.rightStickX - (-0.3f)) < 0.01f);
    CHECK(std::abs(csW3c.rightStickY - 0.8f) < 0.01f);
}

namespace xc::app {
void releaseVirtualGamepadSlot(int slot);
}

static void testPlayerSwitchAndUnresolvedTiles() {
    // 1. Virtual Gamepad Multi-Phone Concurrency & Slot Release
    {
        app::resetVirtualGamepads();
        app::WebServer srv;

        // Player 1 connects and sends input
        auto resP1 = srv.handleRequest("POST", "/api/gamepad", "{\"player\":1,\"btnA\":true}");
        CHECK(resP1.status == 200);
        input::ControllerState s0{};
        bool conn0 = false;
        CHECK(app::virtualGamepadState(0, s0, conn0) && conn0);
        CHECK(s0.btnA);
        CHECK(app::virtualGamepadActiveAt(0) > 0);

        // Player 2 connects concurrently and sends input
        auto resP2 = srv.handleRequest("POST", "/api/gamepad", "{\"player\":2,\"btnB\":true}");
        CHECK(resP2.status == 200);
        input::ControllerState s1{};
        bool conn1 = false;
        CHECK(app::virtualGamepadState(1, s1, conn1) && conn1);
        CHECK(s1.btnB);
        CHECK(app::virtualGamepadActiveAt(1) > 0);

        // Concurrency verification: Player 1 (Slot 0) must STILL be connected and active!
        // No cross-slot wiping occurred when Player 2 connected and sent inputs.
        input::ControllerState s0Check{};
        bool conn0Check = false;
        CHECK(app::virtualGamepadState(0, s0Check, conn0Check) && conn0Check);
        CHECK(s0Check.btnA);
        CHECK(app::virtualGamepadActiveAt(0) > 0);

        // Active switch without prevPlayer: touches slot 2 without dropping slots 0 or 1
        auto resP3 = srv.handleRequest("POST", "/api/gamepad", "{\"player\":3,\"activeSwitch\":true,\"btnX\":true}");
        CHECK(resP3.status == 200);
        input::ControllerState s2{};
        bool conn2 = false;
        CHECK(app::virtualGamepadState(2, s2, conn2) && conn2);
        CHECK(s2.btnX);
        CHECK(app::virtualGamepadState(0, s0Check, conn0Check) && conn0Check);
        CHECK(app::virtualGamepadState(1, s1, conn1) && conn1);

        // Migration with optional prevPlayer: Player 2 (slot 1) migrates to Player 1 (slot 0)
        auto resMigrate = srv.handleRequest("POST", "/api/gamepad",
            "{\"player\":1,\"activeSwitch\":true,\"prevPlayer\":1,\"btnY\":true}");
        CHECK(resMigrate.status == 200);
        CHECK(app::virtualGamepadState(0, s0, conn0) && conn0);
        CHECK(s0.btnY);

        // Slot 1 was explicitly released via prevPlayer
        input::ControllerState s1After{};
        bool conn1After = false;
        app::virtualGamepadState(1, s1After, conn1After);
        CHECK(!conn1After);
        CHECK(app::virtualGamepadActiveAt(1) == 0);

        // Direct non-destructive switchVirtualGamepadSlot test:
        // switchVirtualGamepadSlot(3) activates slot 3 without clearing slot 0
        app::switchVirtualGamepadSlot(3);
        input::ControllerState s3{};
        bool conn3 = false;
        CHECK(app::virtualGamepadState(3, s3, conn3) && conn3);
        CHECK(app::virtualGamepadActiveAt(3) > 0);
        CHECK(app::virtualGamepadState(0, s0, conn0) && conn0);

        // Direct releaseVirtualGamepadSlot test:
        // explicitly release slot 0
        app::releaseVirtualGamepadSlot(0);
        input::ControllerState s0After{};
        bool conn0After = false;
        app::virtualGamepadState(0, s0After, conn0After);
        CHECK(!conn0After);
        CHECK(app::virtualGamepadActiveAt(0) == 0);

        // Direct releaseVirtualGamepadSlot test on slot 3
        app::releaseVirtualGamepadSlot(3);
        app::virtualGamepadState(3, s3, conn3);
        CHECK(!conn3);
        CHECK(app::virtualGamepadActiveAt(3) == 0);
    }

    // 2. Library Filtering of Unresolved Store BigIds (e.g. bsr9nlhvf1kl)
    {
        app::Library lib;
        lib.markAsOwned("bsr9nlhvf1kl", "T_UNRESOLVED");
        auto ownedTiles = lib.owned();
        bool foundRawId = false;
        for (const auto& t : ownedTiles) {
            if (t.productId == "bsr9nlhvf1kl" || t.name == "bsr9nlhvf1kl" || t.tileUrl.empty()) {
                foundRawId = true;
            }
        }
        CHECK(!foundRawId);
    }

    // 3. WebUI HTML contains responsive layout, safe-area rules, and Layout Studio
    {
        app::WebServer srv;
        auto res = srv.handleRequest("GET", "/", "");
        CHECK(res.status == 200);
        CHECK(res.body.find("safe-area-inset-top") != std::string::npos);
        CHECK(res.body.find("safe-area-inset-bottom") != std::string::npos);
        CHECK(res.body.find("gamepad-viewport-wrapper") != std::string::npos);
        CHECK(res.body.find("gamepad-container") != std::string::npos);

        // Layout Studio components & storage persistence
        CHECK(res.body.find("studioToolbar") != std::string::npos);
        CHECK(res.body.find("btnToggleStudio") != std::string::npos);
        CHECK(res.body.find("Layout Studio") != std::string::npos);
        CHECK(res.body.find("psbox_layout_studio_v1") != std::string::npos);
        CHECK(res.body.find("exportStudioLayout") != std::string::npos);
        CHECK(res.body.find("importStudioLayout") != std::string::npos);
        CHECK(res.body.find("resetStudioLayout") != std::string::npos);
        CHECK(res.body.find("mod_stick_left") != std::string::npos);
        CHECK(res.body.find("mod_player_bar") != std::string::npos);

        // Layout JSON payload parsing verification
        std::string sampleLayout = R"({
            "hapticMs": 24,
            "stickSensitivity": 1.4,
            "modules": {
                "mod_stick_left": {"x": 50, "y": 100, "scale": 1.25},
                "mod_abxy": {"x": 700, "y": 110, "scale": 1.35}
            }
        })";
        auto parsedLayout = json::parse(sampleLayout);
        CHECK(parsedLayout.has_value());
        CHECK((*parsedLayout)["hapticMs"].asInt(0) == 24);
        CHECK((*parsedLayout)["stickSensitivity"].asNumber(0.0) > 1.39);
        CHECK((*parsedLayout)["modules"]["mod_stick_left"]["x"].asInt(0) == 50);

        auto resBoost = srv.handleRequest("GET", "/boost", "");
        CHECK(resBoost.status == 200);
        CHECK(resBoost.body.find("switchTab('stream')") != std::string::npos);

        auto resGenUi = srv.handleRequest("GET", "/generative_ui", "");
        CHECK(resGenUi.status == 200);
        CHECK(resGenUi.body.find("PSBox - Interactive UI Layout Editor") != std::string::npos);

        auto resGenUiHtml = srv.handleRequest("GET", "/generative_ui.html", "");
        CHECK(resGenUiHtml.status == 200);
        CHECK(resGenUiHtml.body.find("PSBox - Interactive UI Layout Editor") != std::string::npos);

        // Verify USB / secondary controller deadzone with unpopulated raw sticks
        input::setDeadzone(0.15f, 0.15f);
        input::ControllerState testPad{};
        testPad.leftStickX = 0.10f; // Inside deadzone (10% < 15%)
        testPad.leftStickY = 0.05f;
        input::applyPadSettings(testPad);
        CHECK(testPad.leftStickX == 0.0f);
        CHECK(testPad.leftStickY == 0.0f);

        input::ControllerState testPad2{};
        testPad2.leftStickX = 0.60f; // Outside deadzone (60% > 15%)
        input::applyPadSettings(testPad2);
        CHECK(testPad2.leftStickX > 0.0f);

        // Verify Circle button solid 360-degree rendering
        ui::Canvas canvas(64, 64);
        ui::Fonts fonts;
        ui::drawFaceButton(canvas, fonts.bold, ui::GlyphStyle::PlayStation, ui::PadButton::Circle, 32.0f, 32.0f, 16.0f);
        auto getPx = [&](int x, int y) { return canvas.data()[y * canvas.width() + x]; };
        // Ensure non-zero pixels exist in all 4 quadrants (solid 360 circle stroke)
        CHECK(getPx(32, 17) != 0); // Top
        CHECK(getPx(32, 47) != 0); // Bottom
        CHECK(getPx(17, 32) != 0); // Left
        CHECK(getPx(47, 32) != 0); // Right
    }
}

static void testWebReportAndGamepadLayout() {
    app::WebServer srv;

    // 1. GET /api/report-issue
    {
        auto res = srv.handleRequest("GET", "/api/report-issue", "");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j.has_value() && (*j)["ok"].asBool());
        CHECK((*j).has("url") && (*j)["url"].str().find("github.com") != std::string::npos);
        CHECK((*j).has("platform") && !(*j)["platform"].str().empty());
        CHECK((*j).has("release") && (*j)["release"].str() == XC_APP_VERSION);
    }

    // 2. POST /api/report-issue with custom title and description
    {
        auto res = srv.handleRequest("POST", "/api/report-issue",
                                     "{\"title\":\"Audio crackling on PS5\",\"what\":\"Sound started stuttering during stream\"}");
        CHECK(res.status == 200);
        auto j = json::parse(res.body);
        CHECK(j.has_value() && (*j)["ok"].asBool());
        CHECK((*j)["title"].str() == "Audio crackling on PS5");
        CHECK((*j)["url"].str().find("Audio+crackling+on+PS5") != std::string::npos);
    }

    // 3. WebUI HTML checks (enlarged sticks/buttons, no L3/R3 tap-click, report UI, RT visibility layout)
    {
        auto res = srv.handleRequest("GET", "/", "");
        CHECK(res.status == 200);
        // Report form in tab-logs
        CHECK(res.body.find("openGitHubReport()") != std::string::npos);
        CHECK(res.body.find("copySanitizedReport()") != std::string::npos);
        CHECK(res.body.find("reportTitle") != std::string::npos);
        // Portrait 2-row layout classes
        CHECK(res.body.find("pad-bumper-left") != std::string::npos);
        CHECK(res.body.find("pad-bumper-right") != std::string::npos);
        // Enlarged fullscreen sizes
        CHECK(res.body.find("width: 152px !important") != std::string::npos);
        CHECK(res.body.find("width: 160px !important") != std::string::npos);
        // Tap-click removed from setupStick calls
        CHECK(res.body.find("setupStick('leftStickZone', 'leftStickHead', 'leftStickX', 'leftStickY')") != std::string::npos);
        CHECK(res.body.find("setupStick('leftStickZone', 'leftStickHead', 'leftStickX', 'leftStickY', 'btnL3')") == std::string::npos);
        // Stick heads have no static L3/R3 text
        CHECK(res.body.find("id=\"leftStickHead\"></div>") != std::string::npos);
        CHECK(res.body.find("id=\"rightStickHead\"></div>") != std::string::npos);
    }
}

void runWebUiTests() {
    testWebStreamingAndGamepad();
    testWebSettingsAndStreamMenu();
    testWebSocketRFC6455();
    testPlayerSwitchAndUnresolvedTiles();
    testWebReportAndGamepadLayout();
}
