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

static void testInputPacket() {
    using namespace stream;
    auto meta = clientMetadataReport(0, 0.0, 1);
    CHECK(meta.size() == 15 && meta[0] == 8 && meta[1] == 0 && meta[14] == 1);

    GamepadFrame f;
    f.buttons = kA | kDPadUp;
    f.leftX = 1.0f;
    f.leftY = -1.0f;
    f.rightTrigger = 1.0f;
    auto p = gamepadReport(7, 1.5, f);
    CHECK(p.size() == 38);
    CHECK(p[0] == 2 && p[2] == 7 && p[3] == 0);
    CHECK(p[13] == 0x3f && p[12] == 0xf8);  // 1.5 as little-endian float64
    CHECK(p[14] == 1 && p[15] == 0);        // one frame, gamepad 0
    CHECK(p[16] == 0x10 && p[17] == 0x01);  // A | DPadUp
    CHECK(p[18] == 0xff && p[19] == 0x7f);  // left X = 32767
    CHECK(p[20] == 0x01 && p[21] == 0x80);  // left Y = -32767
    CHECK(p[28] == 0xff && p[29] == 0xff);  // right trigger
    CHECK(gamepadPhysicality(f) == 0x000e1001);
    CHECK(p[30] == 0x01 && p[31] == 0x10 && p[32] == 0x0e && p[33] == 0x00);  // physicality LE
    CHECK(p[34] == 0 && p[35] == 0 && p[36] == 0 && p[37] == 0);              // virtual physicality LE = 0

    GamepadFrame idle;
    CHECK(gamepadPhysicality(idle) == 0);
    auto pIdle = gamepadReport(8, 2.0, idle);
    CHECK(pIdle[30] == 0 && pIdle[31] == 0 && pIdle[32] == 0 && pIdle[33] == 0);
    CHECK(pIdle[34] == 0 && pIdle[35] == 0 && pIdle[36] == 0 && pIdle[37] == 0);

    FrameMetadata fm;
    fm.serverDataKey = 0x01020304;
    fm.firstPacketArrivalMs = 100.0;
    fm.submittedMs = 100.05;
    fm.decodedMs = 102.5;
    fm.renderedMs = 110.0;
    auto md = metadataReport(9, 111.0, {fm});
    CHECK(md.size() == 14 + 1 + 28);
    CHECK(md[0] == 1 && md[1] == 0 && md[2] == 9 && md[14] == 1);
    CHECK(md[15] == 0x04 && md[18] == 0x01);                  // RTP timestamp, little-endian
    CHECK(md[19] == 0xE8 && md[20] == 0x03);                  // 100 ms = 1000 tenths
    CHECK(md[27] == 0x01 && md[28] == 0x04);                  // 102.5 ms = 1025
    CHECK(md[39] == 0x56 && md[40] == 0x04);                  // report time 111 ms = 1110

    uint8_t vib[13] = {128, 0, 0, 0, 50, 25, 0, 0, 0x10, 0x00, 0, 0, 1};
    Vibration v;
    CHECK(parseVibration(vib, sizeof vib, v) && v.leftMotor == 50 && v.rightMotor == 25 && v.durationMs == 16);
    uint8_t sm[10] = {16, 0, 0x38, 0x04, 0, 0, 0x80, 0x07, 0, 0};
    uint32_t w = 0, h = 0;
    CHECK(parseServerMetadata(sm, sizeof sm, w, h) && w == 1920 && h == 1080);
}

static void testTeredo() {
    std::string ip;
    int port = 0;
    // RFC 4380 example: server 65.54.227.120, client 192.0.2.45:40000.
    CHECK(stream::decodeTeredo("2001:0:4136:e378:8000:63bf:3fff:fdd2", ip, port));
    CHECK(ip == "192.0.2.45" && port == 40000);
    CHECK(!stream::decodeTeredo("2a01::1", ip, port));
    CHECK(!stream::decodeTeredo("10.0.0.1", ip, port));
}

static void testHostPacer() {
    // Five a second per Microsoft host; other hosts never wait; a 429 holds
    // the host for its Retry-After (30 s at most).
    net::HostPacer p;
    CHECK(p.reserve("xgpuweb.gssv-play-prod.xboxlive.com", 1000) == 0);
    CHECK(p.reserve("xgpuweb.gssv-play-prod.xboxlive.com", 1000) == 200);
    CHECK(p.reserve("xgpuweb.gssv-play-prod.xboxlive.com", 1100) == 300);
    CHECK(p.reserve("titlehub.xboxlive.com", 1100) == 0);  // its own slots
    CHECK(p.reserve("login.live.com", 5000) == 0 && p.reserve("login.live.com", 5000) == 200);
    for (int i = 0; i < 10; ++i) CHECK(p.reserve("store-images.s-microsoft.com", 1000) == 0);
    CHECK(!net::HostPacer::paced("evilxboxlive.com") && net::HostPacer::paced("a.xboxlive.com"));
    p.throttled("peoplehub.xboxlive.com", 7, 10000);
    CHECK(p.reserve("peoplehub.xboxlive.com", 10000) == 7000);
    p.throttled("peoplehub.xboxlive.com", 3600, 20000);
    CHECK(p.reserve("peoplehub.xboxlive.com", 20000) == 30000);
    p.throttled("store-images.s-microsoft.com", 60, 0);
    CHECK(p.reserve("store-images.s-microsoft.com", 0) == 0);
}

static void testH264LossWaitFollowsRtt() {
    xc::stream::VideoReceiveStats stats;
    xc::stream::H264Receiver rx([] {}, stats);
    CHECK(rx.lossWaitMs() == 90 && rx.renackMs() == 50);
    rx.setRttMs(15);  // dynamic low-RTT adaptation (< 30 ms): lossWait 60 ms, renack 25 ms
    CHECK(rx.lossWaitMs() == 60 && rx.renackMs() == 25);
    rx.setRttMs(68);  // issue #317 (PS4): 166 ms, a second NACK after 78
    CHECK(rx.lossWaitMs() == 166 && rx.renackMs() == 78);
    rx.setRttMs(141);  // issue #342: 300 ms at most
    CHECK(rx.lossWaitMs() == 300 && rx.renackMs() == 151);
    rx.setRttMs(-1);  // not measured any more: the default again
    CHECK(rx.lossWaitMs() == 90);
}

// The motors of a USB pad: each kind's own packet, strengths in its own scale.

static void testH264Resync() {
    xc::stream::VideoReceiveStats stats;
    int keyframeAsks = 0;
    xc::stream::H264Receiver rx([&] { ++keyframeAsks; }, stats, 150);
    auto packet = [](uint16_t seq, uint32_t ts, bool marker, uint8_t nal) {
        std::vector<uint8_t> b = {0x80, static_cast<uint8_t>((marker ? 0x80 : 0) | 102),
                                  static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq),
                                  static_cast<uint8_t>(ts >> 24), static_cast<uint8_t>(ts >> 16),
                                  static_cast<uint8_t>(ts >> 8), static_cast<uint8_t>(ts), 0, 0, 0, 7,
                                  nal, 0xAA, 0xBB};
        return rtc::make_message(reinterpret_cast<const std::byte*>(b.data()),
                                 reinterpret_cast<const std::byte*>(b.data()) + b.size());
    };
    size_t keyframesOut = 0, framesOut = 0;
    auto feed = [&](rtc::message_vector in) {
        rx.incoming(in, [](rtc::message_ptr) { return true; });
        for (const auto& m : in) {
            ++framesOut;
            if (m->size() > 4 && (static_cast<uint8_t>((*m)[4]) & 0x1F) == 5) ++keyframesOut;
        }
    };
    // The end of a frame from before (a boundary to start at), a key frame, a P frame.
    feed({packet(0, 0, true, 0x41), packet(1, 1500, true, 0x65), packet(2, 3000, true, 0x41)});
    CHECK(framesOut == 2 && keyframesOut == 1);
    // seq 4 never comes; seq 7 never comes; 9-10 a key frame.
    feed({packet(3, 4500, false, 0x41), packet(5, 6000, true, 0x41), packet(6, 7500, false, 0x41),
          packet(8, 7500, true, 0x41), packet(9, 9000, false, 0x65), packet(10, 9000, true, 0x65)});
    CHECK(framesOut == 2);  // waiting for the retransmission
    xc::platform::sleepMs(200);
    feed({});
    CHECK(keyframesOut == 2);  // resumed at the key frame
    CHECK(framesOut == 3);
    CHECK(stats.framesDropped >= 1);
    CHECK(keyframeAsks >= 1);

    // A session's first packets are its key frame (parameter sets first): nothing came before
    // them, and it goes out at once, with no key frame asked for.
    {
        xc::stream::VideoReceiveStats st;
        int asks = 0;
        xc::stream::H264Receiver first([&] { ++asks; }, st, 150);
        rtc::message_vector in = {packet(100, 0, false, 0x67), packet(101, 0, true, 0x65)};
        first.incoming(in, [](rtc::message_ptr) { return true; });
        CHECK(in.size() == 1 && asks == 0 && st.keyframeRequests == 0);
    }
    // The last packet of a frame is lost for good and a key frame follows it: resumed at that
    // key frame, though the packet before it never came. A key slice that isn't the first of
    // its picture (its first macroblock isn't 0) is no place to start.
    {
        xc::stream::VideoReceiveStats st;
        xc::stream::H264Receiver lossy([] {}, st, 60);
        size_t out = 0;
        auto give = [&](rtc::message_vector in) {
            lossy.incoming(in, [](rtc::message_ptr) { return true; });
            out += in.size();
        };
        give({packet(0, 0, false, 0x67), packet(1, 0, true, 0x65), packet(2, 1500, false, 0x41)});
        CHECK(out == 1);
        // seq 3 (the end of that P frame) never comes; 4-5 a key frame; 6 a P frame.
        give({packet(4, 3000, false, 0x65), packet(5, 3000, true, 0x65), packet(6, 4500, true, 0x41)});
        CHECK(out == 1);
        xc::platform::sleepMs(100);
        give({});
        CHECK(out == 3);  // the key frame and the P frame after it
        // Now seq 7 is lost and what follows is the second slice of a key picture (0x25: first_mb != 0).
        auto slice2 = [&](uint16_t seq, uint32_t ts) {
            auto m = packet(seq, ts, true, 0x65);
            (*m)[13] = std::byte{0x25};
            return m;
        };
        give({slice2(8, 6000)});
        xc::platform::sleepMs(100);
        give({});
        CHECK(out == 3);
    }
}

// A padding-only RTP packet (payload all padding) inside a frame is skipped,
// not a reason to fail that frame; a payload larger than a buffer slot is a
// lost packet (NACKed, then the frame dropped), never a cut NAL unit.

static void testH264PaddingPacket() {
    xc::stream::VideoReceiveStats stats;
    int keyframeAsks = 0;
    xc::stream::H264Receiver rx([&] { ++keyframeAsks; }, stats, 60);
    auto packet = [](uint16_t seq, uint32_t ts, bool marker, std::vector<uint8_t> payload, uint8_t padding = 0) {
        std::vector<uint8_t> b = {static_cast<uint8_t>(0x80 | (padding ? 0x20 : 0)),
                                  static_cast<uint8_t>((marker ? 0x80 : 0) | 102),
                                  static_cast<uint8_t>(seq >> 8), static_cast<uint8_t>(seq),
                                  static_cast<uint8_t>(ts >> 24), static_cast<uint8_t>(ts >> 16),
                                  static_cast<uint8_t>(ts >> 8), static_cast<uint8_t>(ts), 0, 0, 0, 7};
        b.insert(b.end(), payload.begin(), payload.end());
        for (uint8_t i = 1; i <= padding; ++i) b.push_back(i == padding ? padding : 0);
        return rtc::make_message(reinterpret_cast<const std::byte*>(b.data()),
                                 reinterpret_cast<const std::byte*>(b.data()) + b.size());
    };
    auto nal = [](uint8_t type) { return std::vector<uint8_t>{type, 0xAA, 0xBB}; };
    size_t framesOut = 0;
    auto feed = [&](rtc::message_vector in) {
        rx.incoming(in, [](rtc::message_ptr) { return true; });
        framesOut += in.size();
    };
    // A boundary, a key frame, a P frame, then a padding-only packet opening
    // the next P frame (same timestamp, no marker), that P frame and another.
    feed({packet(0, 0, true, nal(0x41)), packet(1, 1500, true, nal(0x65)), packet(2, 3000, true, nal(0x41)),
          packet(3, 4500, false, {}, 4), packet(4, 4500, true, nal(0x41)), packet(5, 6000, true, nal(0x41))});
    CHECK(framesOut == 4);
    CHECK(stats.framesDropped == 0 && keyframeAsks == 0);
    // A payload too large for a slot: a hole (NACKed), then the frame dropped.
    std::vector<uint8_t> big(2000, 0xCC);
    big[0] = 0x41;
    uint64_t nacksBefore = stats.nacks;
    feed({packet(6, 7500, true, big), packet(7, 9000, true, nal(0x41))});
    CHECK(framesOut == 4);  // waiting for the retransmission
    CHECK(stats.nacks > nacksBefore && stats.lost >= 1);
    xc::platform::sleepMs(100);
    feed({});
    CHECK(framesOut == 4 && stats.framesDropped >= 1 && keyframeAsks >= 1);
    // The key frame asked for resumes the picture.
    feed({packet(8, 10500, true, nal(0x65)), packet(9, 12000, true, nal(0x41))});
    CHECK(framesOut == 6);
}

// An empty audio packet (seen after loss) must not end the sound.

static void testMouseKeyboardReports() {
    using namespace xc::stream;
    auto m = mouseReport(7, 1.0, {MouseFrame{-3, 5, 0, 1, 1}});
    CHECK(m.size() == 14 + 1 + 18);
    CHECK(m[0] == 32 && m[1] == 0 && m[2] == 7);  // type 32 (mouse), sequence 7
    CHECK(m[14] == 1);                              // one frame
    CHECK(m[15] == 0xFD && m[16] == 0xFF && m[17] == 0xFF && m[18] == 0xFF);  // dx -3, little-endian
    CHECK(m[19] == 5 && m[27] == 1 && m[31] == 1 && m[32] == 0);              // dy, wheel Y, buttons, relative
    auto k = keyboardReport(8, 1.0, {KeyFrame{0x41, true}, KeyFrame{0x41, false}});
    CHECK(k.size() == 14 + 1 + 6);
    CHECK(k[0] == 64 && k[14] == 2);
    CHECK(k[15] == 2 && k[16] == 1 && k[17] == 0x41);
    CHECK(k[18] == 2 && k[19] == 0 && k[20] == 0x41);
}

// USB keyboard: HID usages and modifier bits to Windows virtual keys.

static void testMilestone5TransmissionQualityAndStreamReload() {
    // 1. Dynamic Low-RTT Jitter Buffer Adaptation in H264Receiver
    {
        xc::stream::VideoReceiveStats stats;
        xc::stream::H264Receiver rx([] {}, stats, 90);

        // Low RTT (RTT = 14 ms):
        // lossWaitMs: dynamicMin = max(40, 14*2 + 15) = 43. 14*2 + 30 = 58. Clamp(58, 43, 300) = 58 ms.
        // renackMs: dynamicMin = max(20, 14 + 6) = 20. 14 + 10 = 24. Clamp(24, 20, 250) = 24 ms.
        rx.setRttMs(14);
        CHECK(rx.lossWaitMs() == 58);
        CHECK(rx.renackMs() == 24);

        // Boundary low RTT (RTT = 2 ms):
        // lossWaitMs: dynamicMin = max(40, 4 + 15) = 40. 2*2 + 30 = 34. Clamp(34, 40, 300) = 40 ms.
        // renackMs: dynamicMin = max(20, 2 + 6) = 20. 2 + 10 = 12. Clamp(12, 20, 250) = 20 ms.
        rx.setRttMs(2);
        CHECK(rx.lossWaitMs() == 40);
        CHECK(rx.renackMs() == 20);

        // Intermediate low RTT (RTT = 25 ms):
        // lossWaitMs: dynamicMin = max(40, 50 + 15) = 65. 25*2 + 30 = 80. Clamp(80, 65, 300) = 80 ms.
        // renackMs: dynamicMin = max(20, 25 + 6) = 31. 25 + 10 = 35. Clamp(35, 31, 250) = 35 ms.
        rx.setRttMs(25);
        CHECK(rx.lossWaitMs() == 80);
        CHECK(rx.renackMs() == 35);

        // High RTT (RTT = 60 ms):
        // lossWaitMs: dynamicMin = 90. 60*2 + 30 = 150. Clamp(150, 90, 300) = 150 ms.
        // renackMs: dynamicMin = 50. 60 + 10 = 70. Clamp(70, 50, 250) = 70 ms.
        rx.setRttMs(60);
        CHECK(rx.lossWaitMs() == 150);
        CHECK(rx.renackMs() == 70);

        // Boundary RTT (RTT = 30 ms):
        // lossWaitMs: dynamicMin = 90. 30*2 + 30 = 90. Clamp(90, 90, 300) = 90 ms.
        // renackMs: dynamicMin = 50. 30 + 10 = 40. Clamp(40, 50, 250) = 50 ms.
        rx.setRttMs(30);
        CHECK(rx.lossWaitMs() == 90);
        CHECK(rx.renackMs() == 50);

        // Unmeasured / negative RTT
        rx.setRttMs(-1);
        CHECK(rx.lossWaitMs() == 90);
        CHECK(rx.renackMs() == 50);
    }

    // 2. Accelerated Downward Audio Latency Recovery
    {
        xc::app::AudioLatency lat;
        xc::media::AudioQueue q;
        q.open();
        lat.reset();
        CHECK(lat.targetMs == 50);
        CHECK(!lat.warm);

        // Warm-up phase
        lat.update(1000, &q);
        CHECK(!lat.warm);
        lat.update(4000, &q);
        CHECK(lat.warm);
        CHECK(lat.calmSinceMs == 4000);

        // Step up on single dry grain (dry == 1): +5 ms step
        float sample[2] = {0.0f, 0.0f};
        q.push(sample, 1);
        float buf[256 * 2];
        q.pull(buf, 256);  // 1 underrun
        lat.update(5000, &q);
        CHECK(lat.targetMs == 55);
        CHECK(lat.calmSinceMs == 5000);

        // Step up on multi-dry grain (dry == 2): +10 ms step
        q.setRefill(0);
        q.pull(buf, 256);
        q.pull(buf, 256);  // 2 underruns
        lat.update(6000, &q);
        CHECK(lat.targetMs == 65);
        CHECK(lat.calmSinceMs == 6000);

        // Downward recovery: 30s calm -> -5 ms step down to 60 ms
        lat.update(36000, &q);
        CHECK(lat.targetMs == 60);
        CHECK(lat.calmSinceMs == 36000);

        // Another 30s calm -> -5 ms step down to 55 ms
        lat.update(66000, &q);
        CHECK(lat.targetMs == 55);
        CHECK(lat.calmSinceMs == 66000);

        // Another 30s calm -> -5 ms step down to 50 ms floor
        lat.update(96000, &q);
        CHECK(lat.targetMs == 50);
        CHECK(lat.calmSinceMs == 96000);

        // Another 30s calm: at 50 ms floor, must NOT decrease further
        lat.update(126000, &q);
        CHECK(lat.targetMs == 50);

        // Reset check
        lat.reset();
        CHECK(lat.targetMs == 50);
        CHECK(!lat.warm);
    }

    // 2b. A network hold (no packet for longer than any target, then all at once): its dry
    //     grains leave the target and the calm as they were, and the burst's excess is cut at
    //     once; counted as jitter (holdAware off, the A/B's old side), as before.
    for (bool aware : {true, false}) {
        xc::app::AudioLatency lat;
        xc::media::AudioQueue q;
        q.open();
        lat.reset();
        lat.holdAware = aware;
        lat.update(1000, &q);
        lat.update(4000, &q);  // warm
        float sample[2] = {0.0f, 0.0f}, buf[256 * 2];
        q.push(sample, 1);
        q.pull(buf, 256);  // ran dry during the hold
        lat.noteHold(4500, 350);
        std::vector<float> burst(48 * 200 * 2, 0.0f);  // then 200 ms at once
        q.push(burst.data(), 48 * 200);
        lat.update(5000, &q);
        if (aware) {
            CHECK(lat.targetMs == 50 && lat.calmSinceMs == 4000);
            CHECK(q.queuedFrames() <= 50 * 48 + 48);  // down to the target
        } else {
            CHECK(lat.targetMs == 55 && lat.calmSinceMs == 5000);
            CHECK(q.queuedFrames() > 150 * 48);  // left to the slow trim
        }
        CHECK(lat.holds == 1);
    }

    // 3. StreamPlayer::requestReconnect() sets reconnecting state
    {
        xc::xcloud::GssvClient gssv;
        xc::app::StreamPlayer player(gssv);
        CHECK(!player.isReconnecting());
        player.requestReconnect();
        CHECK(player.isReconnecting());
    }

    // 4. StreamMenu returns MenuAction::ReloadStream on reload item accept
    {
        using xc::ui::MenuAction;
        xc::ui::Fonts fonts;
        xc::ui::StreamMenu menu(fonts);
        menu.open(0, false);
        CHECK(menu.isOpen());

        // Navigating Up twice from XboxButton:
        // Up 1: wraps to Leave
        // Up 2: moves to ReloadStreamItem
        xc::ui::NavInput navUp;
        navUp.up = true;
        menu.handle(navUp);
        menu.handle(navUp);

        xc::ui::NavInput navAccept;
        navAccept.accept = true;
        MenuAction action = menu.handle(navAccept);
        CHECK(action == MenuAction::ReloadStream);
        CHECK(!menu.isOpen());
    }
}

static void testMicMuteStateTransitions() {
    // 1. StreamPlayer mute state and transitions
    {
        xcloud::GssvClient gssv;
        app::StreamPlayer player(gssv);
        CHECK(player.micEnabled());
        CHECK(!player.micMuted());

        // Mute
        player.setMicMuted(true);
        CHECK(player.micMuted());

        // Unmute
        player.setMicMuted(false);
        CHECK(!player.micMuted());

        // Toggle micEnabled
        player.setMicEnabled(false);
        CHECK(!player.micEnabled());
        player.setMicEnabled(true);
        CHECK(player.micEnabled());
    }

    // 2. AudioIn mute state and isRecording state
    {
        media::AudioIn audioIn;
        CHECK(!audioIn.isMuted());
        CHECK(!audioIn.isRecording());

        audioIn.setMuted(true);
        CHECK(audioIn.isMuted());

        audioIn.setMuted(false);
        CHECK(!audioIn.isMuted());
    }

    // 3. MicPipeline mute cutoff: drops VU level to 0, drops buffer, produces NO frames
    {
        media::MicPipeline mic;
        std::atomic<float> level{0.0f};
        int framesReceived = 0;
        size_t totalSamples = 0;
        media::AudioIn::AudioCallback cb = [&](const int16_t*, size_t n) {
            framesReceived++;
            totalSamples += n;
        };

        std::vector<int16_t> grain(media::MicPipeline::kGrain, 1500);

        // When active (unmuted): 4 grains of 256 = 1024 samples -> 1 frame of 960
        for (int i = 0; i < 4; ++i) {
            mic.process(grain.data(), grain.size(), false, 1.0f, level, cb);
        }
        CHECK(framesReceived == 1);
        CHECK(totalSamples == media::AudioIn::kFrameSize);
        CHECK(level.load() > 0.0f);

        // Transition to muted: immediately drops level to 0.0f, clears accumulated
        mic.process(grain.data(), grain.size(), true, 1.0f, level, cb);
        CHECK(level.load() == 0.0f);
        CHECK(framesReceived == 1); // No new frames dispatched

        // Subsequent muted grains continue to produce 0 frames and level 0.0f
        for (int i = 0; i < 10; ++i) {
            mic.process(grain.data(), grain.size(), true, 1.0f, level, cb);
        }
        CHECK(framesReceived == 1); // Still exactly 1 frame
        CHECK(level.load() == 0.0f);

        // Transition back to unmuted: starts accumulation from fresh empty buffer
        for (int i = 0; i < 3; ++i) {
            mic.process(grain.data(), grain.size(), false, 1.0f, level, cb);
        }
        CHECK(framesReceived == 1); // 768 samples: not a frame yet
        mic.process(grain.data(), grain.size(), false, 1.0f, level, cb);
        CHECK(framesReceived == 2); // 1024 samples: second frame dispatched
        CHECK(level.load() > 0.0f);
    }
}

void runStreamTests() {
    testInputPacket();
    testTeredo();
    testHostPacer();
    testH264LossWaitFollowsRtt();
    testH264Resync();
    testH264PaddingPacket();
    testMouseKeyboardReports();
    testMilestone5TransmissionQualityAndStreamReload();
    testMicMuteStateTransitions();
}
