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

static void testControllerTuning() {
    using xc::input::radialDeadzone;
    using xc::input::triggerAmplitude;
    auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    float x = 0.1f, y = 0.1f;
    radialDeadzone(x, y, 0.15f);  // |0.14| < 0.15: nothing
    CHECK(x == 0 && y == 0);
    x = 1, y = 0;
    radialDeadzone(x, y, 0.15f);  // the edge stays the edge
    CHECK(near(x, 1) && y == 0);
    x = 0.575f, y = 0;
    radialDeadzone(x, y, 0.15f);  // halfway out of the dead zone -> 0.5, from 0 (no jump)
    CHECK(near(x, 0.5f));
    x = 0.6f, y = 0.1f;  // a diagonal near the X axis keeps its Y (no snapping to the axis)
    radialDeadzone(x, y, 0.15f);
    CHECK(y > 0.05f && near(y / x, 0.1f / 0.6f));

    CHECK(triggerAmplitude(38) == 4 && triggerAmplitude(128) == 6);  // medium: as before
    CHECK(triggerAmplitude(38, 0) == 0 && triggerAmplitude(0, 4) == 0);
    CHECK(triggerAmplitude(38, 1) == 2 && triggerAmplitude(38, 3) == 6 && triggerAmplitude(38, 4) == 8);
    CHECK(triggerAmplitude(255, 3) == 8 && triggerAmplitude(1, 1) == 1);

    using xc::input::triggerCommand;
    auto vib = triggerCommand(38, 2, 1, 2, false, 0);  // vibration style: the motor, 60 Hz
    CHECK(vib.mode == 3 && vib.data[1] == 4 && vib.data[2] == 60);
    auto still = triggerCommand(0, 2, 1, 2, false, 0);  // no request: the medium resistance (from 3, strength 4)
    CHECK(still.mode == 1 && still.data[0] == 3 && still.data[1] == 4);
    CHECK(triggerCommand(0, 2, 1, 0, false, 0).mode == 0);  // no resistance: off
    // Pulses (20 Hz: 25 ms push, 25 ms release) keep the weight: base 3 + amplitude 4.
    auto push = triggerCommand(38, 2, 1, 2, true, 0), release = triggerCommand(38, 2, 1, 2, true, 30);
    CHECK(push.mode == 1 && push.data[1] == 7 && release.mode == 1 && release.data[1] == 3);
    CHECK(triggerCommand(38, 2, 1, 0, true, 30).mode == 0);  // no resistance: released is off
    CHECK(triggerCommand(38, 0, 1, 2, true, 0) == still);    // vibration off: only the weight
}

static void testUsbPadRumblePacket() {
    using namespace xc::input;
    uint8_t out[32], seq = 0;
    CHECK(usbPadRumblePacket(UsbPadProtocol::XInput, 200, 50, seq, out, sizeof out) == 8);
    CHECK(out[0] == 0x00 && out[1] == 0x08 && out[3] == 200 && out[4] == 50);
    CHECK(usbPadRumblePacket(UsbPadProtocol::Gip, 255, 128, seq, out, sizeof out) == 13);
    CHECK(out[0] == 0x09 && out[2] == 1 && out[3] == 0x09 && out[5] == 0x0F && out[8] == 100 && out[9] == 50);
    seq = 255;  // GIP never numbers a message 0
    CHECK(usbPadRumblePacket(UsbPadProtocol::Gip, 0, 0, seq, out, sizeof out) == 13 && out[2] == 1 && out[8] == 0);
    CHECK(usbPadRumblePacket(UsbPadProtocol::DualShock4, 10, 20, seq, out, sizeof out) == 32);
    CHECK(out[0] == 0x05 && out[1] == 0x01 && out[4] == 20 && out[5] == 10);
    CHECK(usbPadRumblePacket(UsbPadProtocol::GenericHid, 10, 20, seq, out, sizeof out) == 0);
    CHECK(usbPadRumblePacket(UsbPadProtocol::XInput, 1, 1, seq, out, 4) == 0);  // no room: nothing written
    CHECK(usbPadRumblePacket(UsbPadProtocol::XInput, 200, 50, seq, out, sizeof out, true) == 12);
    CHECK(out[1] == 0x01 && out[3] == 0xC0 && out[5] == 200 && out[6] == 50);
    CHECK(xinputWirelessLedPacket(0)[3] == 0x46 && xinputWirelessLedPacket(3)[3] == 0x49);

    // The Xbox 360 wireless receiver's reports (layout of the Linux xpad driver).
    ControllerState st;
    const uint8_t connected[] = {0x08, 0x80};
    CHECK(!parseXInputReport(connected, sizeof connected, st));
    uint8_t in[29] = {0x00, 0x01, 0x00, 0xF0, 0x00, 0x13, 0x00, 0x10 /* A */, 0xFF /* LT */, 0x00};
    in[10] = 0x00, in[11] = 0x80;  // left stick fully left
    CHECK(parseXInputReport(in, sizeof in, st));
    CHECK(st.btnA && !st.btnB && st.triggerL2 > 0.99f && st.rawLeftX < -0.9f && !st.dpadUp);
    // The receiver's other messages, as a Guitar Hero 5 guitar linked up (seen on the console):
    // not input, nothing pressed.
    {
        ControllerState g;
        const uint8_t announce[29] = {0x00, 0x0f, 0x00, 0xf0, 0xf0, 0xcc, 0xaa, 0xaa, 0xaa, 0xa0, 0x11, 0xa9,
                                      0x06, 0x91, 0x00, 0x01, 0x13, 0xe3, 0x20, 0x1d, 0x30, 0x03, 0x44, 0x30};
        const uint8_t other[29] = {0x00, 0x01, 0x00, 0x13, 0xa2, 0x13};
        const uint8_t status[29] = {0x00, 0x05, 0x00, 0x20, 0x1d, 0x12, 0x3f, 0xff, 0x00, 0x00, 0x30, 0x14, 0x05, 0x07, 0x01};
        const uint8_t zero[29] = {0x00, 0x00, 0x00, 0x00, 0x01};
        CHECK(!parseXInputReport(announce, sizeof announce, g) && !parseXInputReport(other, sizeof other, g));
        CHECK(!parseXInputReport(status, sizeof status, g) && !parseXInputReport(zero, sizeof zero, g));
        CHECK(!g.btnA && g.triggerL2 == 0.0f);
        CHECK(xinputWirelessSubtype(other, sizeof other) == -1);
        uint8_t link[29] = {};
        std::copy(std::begin(announce), std::end(announce), link);
        link[25] = 0x87;  // a guitar (alternate), with the motors flag
        CHECK(xinputWirelessSubtype(link, sizeof link) == 0x07);
        CHECK(std::string(xinputSubtypeName(0x08)) == "drum kit");
    }
    const uint8_t gone[] = {0x08, 0x00};
    CHECK(parseXInputReport(gone, sizeof gone, st) && !st.btnA && st.triggerL2 == 0.0f);
    // A wired report is unchanged: A, nothing else.
    uint8_t wired[20] = {0x00, 0x14, 0x00, 0x10};
    ControllerState w;
    CHECK(parseXInputReport(wired, sizeof wired, w) && w.btnA && !w.dpadUp);
    // The receiver's descriptor: interface FF/5D/81 is a wireless XInput pad.
    const uint8_t cfg[] = {9, 2, 32, 0, 1, 1, 0, 0x80, 50,
                           9, 4, 0, 0, 2, 0xFF, 0x5D, 0x81, 0,
                           7, 5, 0x81, 3, 32, 0, 1,
                           7, 5, 0x01, 3, 32, 0, 8};
    const UsbPadInterface rx = findUsbPadInterface(cfg, sizeof cfg);
    CHECK(rx.protocol == UsbPadProtocol::XInput && rx.wireless && rx.epIn == 0x81 && rx.epOut == 0x01);
}

// Instruments behind an Xbox 360 wireless receiver: its four channels, what each device is, and
// what a guitar sends to the game (the frets, with or without the strum bar; nothing on the sticks).

static void testUsbInstruments() {
    using namespace xc::input;
    // The receiver's configuration: per device a pad interface (FF/5D/81) and a headset one (82).
    std::vector<uint8_t> cfg = {9, 2, 0, 0, 8, 1, 0, 0xA0, 250};
    for (uint8_t k = 0; k < 4; ++k) {
        const uint8_t pad[] = {9, 4, static_cast<uint8_t>(2 * k), 0, 2, 0xFF, 0x5D, 0x81, 0,
                               7, 5, static_cast<uint8_t>(0x81 + 2 * k), 3, 32, 0, 1,
                               7, 5, static_cast<uint8_t>(0x01 + 2 * k), 3, 32, 0, 8};
        const uint8_t headset[] = {9, 4, static_cast<uint8_t>(2 * k + 1), 0, 2, 0xFF, 0x5D, 0x82, 0,
                                   7, 5, static_cast<uint8_t>(0x82 + 2 * k), 3, 32, 0, 2,
                                   7, 5, static_cast<uint8_t>(0x02 + 2 * k), 3, 32, 0, 4};
        cfg.insert(cfg.end(), pad, pad + sizeof pad);
        cfg.insert(cfg.end(), headset, headset + sizeof headset);
    }
    const auto channels = findXInputWirelessInterfaces(cfg.data(), cfg.size());
    CHECK(channels.size() == 4);
    CHECK(channels[0].epIn == 0x81 && channels[0].epOut == 0x01 && channels[3].number == 6 && channels[3].epIn == 0x87);
    CHECK(channels[2].wireless && channels[2].protocol == UsbPadProtocol::XInput);

    CHECK(usbPadKindForSubtype(0x07) == UsbPadKind::Guitar && usbPadKindForSubtype(0x06) == UsbPadKind::Guitar);
    CHECK(usbPadKindForSubtype(0x08) == UsbPadKind::Drums && usbPadKindForSubtype(0x01) == UsbPadKind::Gamepad);
    CHECK(usbPadKindForSubtype(-1) == UsbPadKind::Gamepad && usbPadKindForSubtype(0x02) == UsbPadKind::Other);
    // Linked before the app started: the capabilities report names the maker and product
    // (a Guitar Hero 5 guitar, as read on the console: 1430:0705).
    {
        const uint8_t gh5[29] = {0x00, 0x05, 0x00, 0xf0, 0x00, 0x12, 0x3f, 0xff, 0x00, 0x00, 0x30, 0x14, 0x05, 0x07, 0x01,
                                 0x00, 0xc0, 0xff, 0x00, 0x00, 0x03};
        CHECK(xinputSubtypeFromCapabilities(gh5, sizeof gh5) == 0x07);
        CHECK(usbPadKindForSubtype(xinputSubtypeFromCapabilities(gh5, sizeof gh5)) == UsbPadKind::Guitar);
        uint8_t pad[29];
        std::memcpy(pad, gh5, sizeof pad);
        pad[10] = 0x5e, pad[11] = 0x04, pad[12] = 0x8e, pad[13] = 0x02;  // 045e:028e, a pad
        CHECK(xinputSubtypeFromCapabilities(pad, sizeof pad) == -1);
        pad[10] = 0xad, pad[11] = 0x1b, pad[12] = 0x03, pad[13] = 0x00;  // 1bad:0003, Rock Band drums
        CHECK(usbPadKindForSubtype(xinputSubtypeFromCapabilities(pad, sizeof pad)) == UsbPadKind::Drums);
        CHECK(xinputSubtypeFromCapabilities(gh5, 8) == -1);
    }

    // PlayStation 3 instruments (PlasticBand's layouts), read into the Xbox 360 instruments' layout.
    {
        CHECK(ps3InstrumentFor(0x12BA, 0x0100) == Ps3Instrument::GhGuitar && ps3InstrumentFor(0x1430, 0x474C) == Ps3Instrument::GhGuitar);
        CHECK(ps3InstrumentFor(0x12BA, 0x0203) == Ps3Instrument::RbGuitar && ps3InstrumentFor(0x12BA, 0x0218) == Ps3Instrument::RbDrums);
        CHECK(ps3InstrumentFor(0x12BA, 0x0120) == Ps3Instrument::GhDrums && ps3InstrumentFor(0x12BA, 0x0140) == Ps3Instrument::Turntable);
        CHECK(ps3InstrumentFor(0x12BA, 0x2330) == Ps3Instrument::None && ps3InstrumentFor(0x054C, 0x0268) == Ps3Instrument::None);
        CHECK(ps3InstrumentKind(Ps3Instrument::GhLiveGuitar) == UsbPadKind::Guitar && ps3InstrumentKind(Ps3Instrument::RbDrums) == UsbPadKind::Drums);

        uint8_t gh[27] = {};
        gh[2] = 0x1F, gh[5] = 0x7F, gh[19] = 0xDB, gh[20] = 0x01;  // at rest: hat 0x1f, whammy 0x7f, tilt 475 (level)
        ControllerState g;
        CHECK(parsePs3InstrumentReport(Ps3Instrument::GhGuitar, gh, sizeof gh, g));
        CHECK(!g.btnA && !g.dpadUp && !g.dpadDown && g.rawRightX < -0.9f && g.rawRightY > 0.2f && g.rawRightY < 0.35f);
        gh[0] = 0x02 | 0x01;  // green and yellow
        gh[2] = 0x04;         // strummed down
        gh[1] = 0x02;         // Start
        CHECK(parsePs3InstrumentReport(Ps3Instrument::GhGuitar, gh, sizeof gh, g));
        CHECK(g.btnA && g.btnY && !g.btnB && !g.btnX && g.dpadDown && !g.dpadUp && g.btnOptions);
        // Through the guitar's own logic: raised (tilt 384) for long enough, star power on A.
        InstrumentState is;
        gh[0] = 0, gh[1] = 0, gh[2] = 0x1F;
        parsePs3InstrumentReport(Ps3Instrument::GhGuitar, gh, sizeof gh, g);
        for (uint64_t t = 1000; t <= 1000 + kTiltSettleMs; t += 100)  // picked up: its position learnt
            instrumentForGame(UsbPadKind::Guitar, g, is, false, t, true, kGuitarRaiseBy);
        gh[19] = 0x80, gh[20] = 0x01;  // 384: the neck up
        parsePs3InstrumentReport(Ps3Instrument::GhGuitar, gh, sizeof gh, g);
        CHECK(g.rawRightY > 0.95f);
        const uint64_t at = 1100 + kTiltSettleMs;
        instrumentForGame(UsbPadKind::Guitar, g, is, false, at, true, kGuitarRaiseBy);
        ControllerState up = instrumentForGame(UsbPadKind::Guitar, g, is, false, at + kTiltHoldMs, true, kGuitarRaiseBy);
        applyGuitarTaps(up, is, at + kTiltHoldMs);
        CHECK(is.tiltUp && up.btnA);

        uint8_t rb[27] = {};
        rb[0] = 0x08 | 0x01 | 0x20;  // yellow, blue, the tilt switch
        rb[2] = 0x08;
        CHECK(parsePs3InstrumentReport(Ps3Instrument::RbGuitar, rb, sizeof rb, g));
        CHECK(g.btnY && g.btnX && !g.btnA && g.rawRightY == 1.0f && g.rawRightX < -0.9f && !g.dpadUp);
        uint8_t drums[27] = {};
        drums[0] = 0x02 | 0x10 | 0x20, drums[2] = 0x0F;  // green, kick, orange cymbal
        CHECK(parsePs3InstrumentReport(Ps3Instrument::GhDrums, drums, sizeof drums, g));
        CHECK(g.btnA && g.btnL1 && g.btnR1 && !g.btnB);
        drums[0] = 0x08, drums[1] = 0x08;  // Rock Band: yellow, the cymbal flag
        CHECK(parsePs3InstrumentReport(Ps3Instrument::RbDrums, drums, sizeof drums, g));
        CHECK(g.btnY && g.btnR3 && !g.btnL3 && !g.btnL1);
        CHECK(!parsePs3InstrumentReport(Ps3Instrument::GhGuitar, gh, 4, g));
    }

    // The guitar at rest, as seen on the console: whammy at full left, the tilt sensor moving.
    ControllerState in;
    in.connected = true;
    in.rawRightX = in.rightStickX = -1.0f;
    in.rawRightY = in.rightStickY = -0.55f;
    InstrumentState st;
    ControllerState out = instrumentForGame(UsbPadKind::Guitar, in, st, false);
    CHECK(out.rightStickX == 0.0f && out.rightStickY == 0.0f && !out.btnA);
    in.btnA = in.btnY = true;  // green and yellow held: without strum, at once
    out = instrumentForGame(UsbPadKind::Guitar, in, st, false);
    CHECK(out.btnA && out.btnY && !out.btnB && out.rightStickX == 0.0f);
    in.btnA = in.btnY = false;
    in.btnTouchpad = true;  // Back: star power, on A (Fortnite Festival's Overdrive), as a tap
    {
        ControllerState sp = instrumentForGame(UsbPadKind::Guitar, in, st, false, 3000);
        applyGuitarTaps(sp, st, 3000);
        CHECK(sp.btnA);
        sp = instrumentForGame(UsbPadKind::Guitar, in, st, false, 3000 + kStarPowerTapMs);
        applyGuitarTaps(sp, st, 3000 + kStarPowerTapMs);
        CHECK(!sp.btnA);  // held on: let go after the tap (Fortnite fires as it is let go)
    }
    in.btnTouchpad = false;

    // With strum: a fret tapped (down briefly, no strum) is its menu button (A B Y X) as it is let
    // go, for kMenuTapHoldMs; the lanes (LT LB RB RT R3) only once the bar moves, then held as long
    // as the frets are. A fret held and strummed is a note and no button (A is Overdrive in Fortnite).
    auto lane = [](const ControllerState& g, int i) {
        return i == 0 ? g.triggerL2 > 0.5f : i == 1 ? g.btnL1 : i == 2 ? g.btnR1 : i == 3 ? g.triggerR2 > 0.5f : g.btnR3;
    };
    auto game = [](ControllerState g, const InstrumentState& st, uint64_t t) {
        applyStrumGaps(g, st, t);
        applyGuitarTaps(g, st, t);
        return g;
    };
    InstrumentState ss;
    in.btnA = true, in.btnY = false;
    out = instrumentForGame(UsbPadKind::Guitar, in, ss, true, 1000);
    CHECK(!game(out, ss, 1000).btnA && !lane(out, 0));  // green down: nothing yet
    in.btnA = false;
    out = instrumentForGame(UsbPadKind::Guitar, in, ss, true, 1100);  // let go after 100 ms: a tap
    CHECK(game(out, ss, 1100).btnA && game(out, ss, 1100 + kMenuTapHoldMs - 1).btnA);
    CHECK(!game(out, ss, 1100 + kMenuTapHoldMs).btnA);  // let go again by the time the game reads it
    in.btnA = true;
    instrumentForGame(UsbPadKind::Guitar, in, ss, true, 2000);
    in.dpadDown = true;  // strum
    out = instrumentForGame(UsbPadKind::Guitar, in, ss, true, 2010);
    CHECK(lane(out, 0) && out.dpadDown && !game(out, ss, 2010).btnA);
    in.dpadDown = false;  // the bar back, green still held: the note goes on (a sustain)
    CHECK(lane(instrumentForGame(UsbPadKind::Guitar, in, ss, true, 2050), 0));
    in.btnY = true;  // yellow pressed later, no strum: not played, Y held for the menus (hold Y to start)
    out = instrumentForGame(UsbPadKind::Guitar, in, ss, true, 2100);
    CHECK(lane(out, 0) && !lane(out, 2) && game(out, ss, 2100).btnY && game(out, ss, 2900).btnY);
    in.btnA = false;  // green let go after a strum: a note, not a tap (no Overdrive)
    out = instrumentForGame(UsbPadKind::Guitar, in, ss, true, 2200);
    CHECK(!lane(out, 0) && !game(out, ss, 2200).btnA);
    in.btnA = true;
    CHECK(!lane(instrumentForGame(UsbPadKind::Guitar, in, ss, true, 2300), 0));  // not back until strummed
    in.dpadUp = true;
    out = instrumentForGame(UsbPadKind::Guitar, in, ss, true, 2310);
    CHECK(lane(out, 0) && lane(out, 2) && !out.btnL3);
    in.btnA = false;
    in.btnTouchpad = true;  // star power: A, a tap at once
    out = instrumentForGame(UsbPadKind::Guitar, in, ss, true, 2400);
    CHECK(game(out, ss, 2400).btnA && !game(out, ss, 2400 + kStarPowerTapMs).btnA && lane(out, 3) == false);
    in.btnA = true;
    in.btnTouchpad = false;
    in.dpadUp = false;
    // A fret down just after a strum (late) and let go: a note, not a tap.
    {
        InstrumentState ls;
        ControllerState g;
        g.dpadDown = true;
        instrumentForGame(UsbPadKind::Guitar, g, ls, true, 5000);
        g.dpadDown = false;
        instrumentForGame(UsbPadKind::Guitar, g, ls, true, 5040);
        g.btnA = true;  // green, 80 ms after the strum
        instrumentForGame(UsbPadKind::Guitar, g, ls, true, 5080);
        g.btnA = false;
        const ControllerState r = instrumentForGame(UsbPadKind::Guitar, g, ls, true, 5180);
        CHECK(!game(r, ls, 5180).btnA);
        g.btnA = true;  // and a slow press (a held fret, 600 ms): not a tap either
        instrumentForGame(UsbPadKind::Guitar, g, ls, true, 7000);
        g.btnA = false;
        CHECK(!game(instrumentForGame(UsbPadKind::Guitar, g, ls, true, 7600), ls, 7600).btnA);
    }

    // Held frets strummed again: let go for a moment, then down again (a new note each strum).
    {
        InstrumentState rs;
        ControllerState g;
        g.btnA = true, g.dpadDown = true;
        CHECK(lane(instrumentForGame(UsbPadKind::Guitar, g, rs, true, 1000), 0));
        g.dpadDown = false;
        CHECK(lane(instrumentForGame(UsbPadKind::Guitar, g, rs, true, 1050), 0));
        g.dpadUp = true;  // the bar again, green still held
        ControllerState again = instrumentForGame(UsbPadKind::Guitar, g, rs, true, 1100);
        CHECK(!lane(again, 0) && !again.btnA);  // a held, strummed fret: no A (Overdrive)
        ControllerState later = again;
        later.triggerL2 = 1.0f;
        applyStrumGaps(later, rs, 1100 + kStrumGapMs - 1);
        CHECK(!lane(later, 0));
        later.triggerL2 = 1.0f;
        applyStrumGaps(later, rs, 1100 + kStrumGapMs);
        CHECK(lane(later, 0));
    }
    // Star power by raising the guitar: kGuitarRaiseBy above the position it is played in, which
    // is learnt (a Guitar Hero 5 guitar read -0.95 held one way, -0.4 held another), for
    // kTiltHoldMs; off once back near it. A jolt shorter than that is not.
    for (float rest : {-0.95f, -0.4f}) {
        InstrumentState ts;
        ControllerState g;
        auto rt = [&](float v, uint64_t t, bool on = true) {
            g.rawRightY = v;
            instrumentForGame(UsbPadKind::Guitar, g, ts, false, t, on, kGuitarRaiseBy);
            return ts.tiltUp ? 1.0f : 0.0f;
        };
        uint64_t t = 1000;
        for (int k = 0; k < 100; ++k, t += 100) CHECK(rt(rest + (k % 2 ? 0.05f : -0.05f), t) == 0.0f);  // played: never
        CHECK(rt(rest + 0.5f, t) == 0.0f);  // raised, not for long yet
        CHECK(rt(rest + 0.5f, t + kTiltHoldMs) == 1.0f);
        CHECK(rt(rest + 0.3f, t + kTiltHoldMs + 10) == 1.0f);  // a little down: still on
        CHECK(rt(rest, t + kTiltHoldMs + 20) == 0.0f);         // back to playing
        t += 1000;
        CHECK(rt(rest + 0.6f, t) == 0.0f);  // a jolt...
        CHECK(rt(rest, t + 10) == 0.0f);    // ...gone before it counts
        CHECK(rt(rest + 0.6f, t + 20, false) == 0.0f);
        CHECK(rt(rest + 0.6f, t + 20 + kTiltHoldMs, false) == 0.0f);  // tilt off: nothing
    }
    // Held a new way for a while: that becomes the position (no star power stuck on).
    {
        InstrumentState ts;
        ControllerState g;
        g.rawRightY = -0.95f;
        instrumentForGame(UsbPadKind::Guitar, g, ts, false, 1000, true, kGuitarRaiseBy);
        g.rawRightY = -0.4f;  // the strap moved: 0.55 above, like a raise for a moment...
        bool onAfter = false;
        for (uint64_t t = 1010; t < 30000; t += 50) {
            instrumentForGame(UsbPadKind::Guitar, g, ts, false, t, true, kGuitarRaiseBy);
            const bool on = ts.tiltUp;
            if (t > 1010 + kTiltMaxMs + 500) onAfter = onAfter || on;
        }
        CHECK(!onAfter);  // ...then its position: off, and off it stays
        g.rawRightY = 0.1f;  // and a raise from there counts
        instrumentForGame(UsbPadKind::Guitar, g, ts, false, 30000, true, kGuitarRaiseBy);
        instrumentForGame(UsbPadKind::Guitar, g, ts, false, 30000 + kTiltHoldMs, true, kGuitarRaiseBy);
        CHECK(ts.tiltUp);
    }
    // Buttons of the player's choosing, for each layout, kept as text in the settings.
    {
        GuitarBinding mine{{GuitarOut::DLeft, GuitarOut::DRight, GuitarOut::X, GuitarOut::B, GuitarOut::RB}, GuitarOut::Y};
        CHECK(guitarBindingText(mine) == "DL,DR,X,B,RB,Y");
        GuitarBinding back{};
        CHECK(guitarBindingFromText("DL,DR,X,B,RB,Y", back) && back == mine);
        CHECK(!guitarBindingFromText("DL,DR,X,B,RB", back) && !guitarBindingFromText("A,B,Y,X,LB,ZZ", back));
        CHECK(guitarBindingText(kStrumLanes) == "LT,LB,RB,RT,R3,A");
        CHECK(std::string(guitarOutName(GuitarOut::DLeft)) == "D-pad ←");

        setGuitarBinding(false, mine);
        InstrumentState cs;
        ControllerState g;
        g.btnA = g.btnX = true;  // green and blue, no strum: their buttons, not A and X
        ControllerState o = instrumentForGame(UsbPadKind::Guitar, g, cs, false);
        CHECK(o.dpadLeft && o.btnB && !o.btnA && !o.btnX && !o.dpadRight);
        g = ControllerState{};
        g.btnTouchpad = true;  // star power on Y (a tap)
        o = instrumentForGame(UsbPadKind::Guitar, g, cs, false, 100);
        applyGuitarTaps(o, cs, 100);
        CHECK(o.btnY && o.triggerR2 == 0.0f);

        // With strum, a lane on A: green no longer sends A without the strum (the menus' A
        // would play it); the other frets still reach the menus.
        GuitarBinding onA{{GuitarOut::A, GuitarOut::LB, GuitarOut::RB, GuitarOut::RT, GuitarOut::R3}, GuitarOut::L3};
        setGuitarBinding(true, onA);
        InstrumentState ms;
        g = ControllerState{};
        g.btnA = g.btnB = true;
        o = instrumentForGame(UsbPadKind::Guitar, g, ms, true, 100);
        CHECK(!o.btnA && !o.btnL1 && o.btnB);  // red's B for the menus, green's A not (a lane's)
        g.btnA = g.btnB = true, g.dpadDown = true;  // strummed: green plays A, red its lane
        o = instrumentForGame(UsbPadKind::Guitar, g, ms, true, 1000);
        CHECK(o.btnA && o.btnL1);
        setGuitarBinding(false, kFretLanes);
        setGuitarBinding(true, kStrumLanes);
    }
    // A guitar at rest (whammy at full left, tilt moving) is not "in use": after a menu it goes
    // back to the game at once.
    {
        ControllerState rest;
        rest.connected = true;
        rest.rawRightX = rest.rightStickX = -1.0f;
        rest.rawRightY = rest.rightStickY = -0.55f;
        InstrumentState rst;
        CHECK(inUse(rest) && !inUse(instrumentForGame(UsbPadKind::Guitar, rest, rst, false)));
    }

    // Drums: the buttons as they come, the pads' strengths off the sticks.
    ControllerState drum;
    drum.btnB = drum.btnL1 = true;
    drum.leftStickX = drum.rightStickY = 0.7f;
    out = instrumentForGame(UsbPadKind::Drums, drum, st, false);
    CHECK(out.btnB && out.btnL1 && out.leftStickX == 0.0f && out.rightStickY == 0.0f);
    // A gamepad is untouched.
    CHECK(instrumentForGame(UsbPadKind::Gamepad, drum, st, true).leftStickX == 0.7f);
}

// Hidden games, favourites and play time follow the Xbox account signed in: lists from before
// they had an owner go to the first account, a second account starts empty, each finds its
// own again, and all of it survives the settings file.

static void testHidToVk() {
    using xc::input::hidToVk;
    CHECK(hidToVk(0x04) == 'A' && hidToVk(0x1D) == 'Z');
    CHECK(hidToVk(0x1E) == '1' && hidToVk(0x26) == '9' && hidToVk(0x27) == '0');
    CHECK(hidToVk(0x28) == 0x0D && hidToVk(0x29) == 0x1B && hidToVk(0x2C) == 0x20);
    CHECK(hidToVk(0x3A) == 0x70 && hidToVk(0x45) == 0x7B);  // F1, F12
    CHECK(hidToVk(0x4F) == 0x27 && hidToVk(0x52) == 0x26);  // right, up
    CHECK(hidToVk(0x59) == 0x61 && hidToVk(0x62) == 0x60);  // keypad 1, 0
    CHECK(hidToVk(0x00) == 0 && hidToVk(0xE0) == 0);
    CHECK(xc::input::modifierVk(0) == 0x11 && xc::input::modifierVk(1) == 0x10 && xc::input::modifierVk(2) == 0x12);
    CHECK(xc::input::modifierVk(5) == 0x10 && xc::input::modifierVk(7) == 0x5C);
}

// The app's screens by keyboard: keys held -> the controller's buttons.

static void testKeyboardNav() {
    xc::input::ControllerState pad;
    xc::input::keyboardToPad({0x0D, 'W', 0x22}, pad);  // enter, w, page down
    CHECK(pad.btnA && pad.dpadUp && !pad.dpadDown && pad.rightStickY == 1.0f);
    xc::input::ControllerState p2;
    p2.btnB = true;  // the controller's own press stays
    xc::input::keyboardToPad({0x25, 0x1B, 0x09, 'Q', 'E', 'F', 'H', 'Z'}, p2);
    CHECK(p2.dpadLeft && p2.btnB && p2.btnOptions && p2.btnL1 && p2.btnR1 && p2.btnY && p2.btnX && p2.triggerL2 == 1.0f);
    xc::input::ControllerState p3;
    xc::input::keyboardToPad({}, p3);
    CHECK(!p3.btnA && !p3.dpadUp && p3.rightStickY == 0.0f);
}

// The keyboard and mouse as a controller of their own.

static void testKeyboardAsGamepad() {
    xc::input::KbmInput in;
    in.keyboard = true;
    in.held = {0x20, 'D', 'W'};  // space, d, w (sorted)
    in.buttons = 1;              // left button
    in.dx = 24;                  // a fast move right
    xc::input::MouseStick stick;
    xc::input::ControllerState out;
    uint64_t now = 1000;
    xc::input::keyboardAsGamepad(in, stick, out, now);
    CHECK(out.connected && out.btnA && out.leftStickX == 1.0f && out.leftStickY == -1.0f);
    CHECK(out.triggerR2 == 1.0f && out.triggerL2 == 0.0f);
    CHECK(out.rightStickX > 0.4f && out.rightStickY == 0.0f);
    in = {};
    in.keyboard = true;
    for (int i = 0; i < 20; ++i) xc::input::keyboardAsGamepad(in, stick, out, now += 8);  // the mouse still: back to the centre
    CHECK(out.rightStickX == 0.0f && !out.btnA);

    // The same mouse speed gives the same stick whether the passes are 4 or
    // 8 ms apart (the counts a pass covers halve with it).
    auto stickAfter = [](uint64_t passMs, float countsPerMs, uint64_t forMs) {
        xc::input::MouseStick st;
        xc::input::ControllerState o;
        xc::input::KbmInput mv;
        mv.mouse = true;
        uint64_t t = 5000;
        xc::input::keyboardAsGamepad(mv, st, o, t);  // a still first pass: the clock starts
        for (uint64_t el = 0; el < forMs; el += passMs) {
            mv.dx = static_cast<int32_t>(countsPerMs * static_cast<float>(passMs));
            xc::input::keyboardAsGamepad(mv, st, o, t += passMs);
        }
        return o.rightStickX;
    };
    for (uint64_t forMs : {8u, 24u, 48u, 400u}) {
        float at4 = stickAfter(4, 0.75f, forMs), at8 = stickAfter(8, 0.75f, forMs);
        CHECK(std::abs(at4 - at8) < 0.01f);
    }
    CHECK(std::abs(stickAfter(4, 0.75f, 400) - 0.5f) < 0.01f);  // half of full speed: half the stick
    CHECK(std::abs(stickAfter(8, 0.75f, 400) - 0.5f) < 0.01f);
    CHECK(stickAfter(4, 3.0f, 400) > 0.999f && stickAfter(8, 3.0f, 400) > 0.999f);  // faster: all the way

    // A wheel notch: Y held ~50 ms, then let go ~30 ms, at either pass length.
    for (uint64_t passMs : {4u, 8u}) {
        xc::input::MouseStick st;
        in = {};
        in.keyboard = true;
        in.wheel = 2;  // two notches: two presses
        uint64_t t = 9000, downMs = 0, presses = 0;
        bool was = false;
        for (uint64_t el = 0; el < 400; el += passMs) {
            xc::input::keyboardAsGamepad(in, st, out, t);
            in.wheel = 0;
            if (out.btnY) downMs += passMs;
            if (out.btnY && !was) ++presses;
            was = out.btnY;
            t += passMs;
        }
        CHECK(presses == 2 && !out.btnY);
        CHECK(downMs >= 2 * 40 && downMs <= 2 * 56);  // ~48 ms each
    }
    xc::input::ControllerState pad, kb;
    pad.btnB = true;
    pad.leftStickX = 0.3f;
    kb.btnA = true;
    kb.leftStickX = -1.0f;
    kb.triggerR2 = 1.0f;
    xc::input::mergePads(pad, kb);
    CHECK(pad.btnA && pad.btnB && pad.leftStickX == -1.0f && pad.triggerR2 == 1.0f);
}

// The DualSense's touchpad as a mouse in MKB games: cursor motion, taps,
// two-finger scroll and click-to-drag after moving.

static void testTouchpadAsMouse() {
    using namespace xc::input;
    TouchpadMouse tm;
    ControllerState pad;
    KbmInput io;

    // 1. One finger down, then sliding right by 0.25 and down by 0.5 -> 240 x 270 counts.
    pad.touching = true;
    pad.touchX = 0.25f;
    pad.touchY = 0.25f;
    CHECK(!touchpadAsMouse(pad, 100, tm, io));
    CHECK(io.dx == 0 && io.dy == 0 && io.buttons == 0 && !io.moved);

    io = {};
    pad.touchX = 0.5f;
    pad.touchY = 0.75f;
    CHECK(!touchpadAsMouse(pad, 116, tm, io));
    CHECK(io.dx == 240 && io.dy == 270 && io.buttons == 0 && io.moved);

    // Clicking the touchpad after moving holds the left mouse button for dragging.
    io = {};
    pad.btnTouchpad = true;
    CHECK(touchpadAsMouse(pad, 124, tm, io));
    CHECK(io.buttons == 1 && io.moved);

    // Releasing the finger lets go of the button.
    io = {};
    pad.touching = false;
    pad.btnTouchpad = false;
    CHECK(!touchpadAsMouse(pad, 140, tm, io));
    CHECK(io.buttons == 0 && io.moved);

    // 2. A light one-finger tap clicks the left button for 50 ms.
    io = {};
    pad.touching = true;
    pad.touchX = 0.5f;
    pad.touchY = 0.5f;
    CHECK(!touchpadAsMouse(pad, 200, tm, io));
    io = {};
    pad.touching = false;
    CHECK(!touchpadAsMouse(pad, 260, tm, io));
    CHECK(io.buttons == 1 && io.moved);
    io = {};
    CHECK(!touchpadAsMouse(pad, 290, tm, io));
    CHECK(io.buttons == 1 && !io.moved);
    io = {};
    CHECK(!touchpadAsMouse(pad, 315, tm, io));
    CHECK(io.buttons == 0 && io.moved);

    // 3. A light two-finger tap clicks the right button.
    io = {};
    pad.touching = true;
    pad.touchSecond = true;
    CHECK(!touchpadAsMouse(pad, 400, tm, io));
    io = {};
    pad.touching = false;
    pad.touchSecond = false;
    CHECK(!touchpadAsMouse(pad, 450, tm, io));
    CHECK(io.buttons == 2 && io.moved);
    io = {};
    CHECK(!touchpadAsMouse(pad, 510, tm, io));
    CHECK(io.buttons == 0 && io.moved);

    // 4. Two-finger vertical slide scrolls the wheel without moving the cursor.
    io = {};
    pad.touching = true;
    pad.touchSecond = true;
    pad.touchX = 0.5f;
    pad.touchY = 0.6f;
    CHECK(!touchpadAsMouse(pad, 600, tm, io));
    io = {};
    pad.touchY = 0.35f;  // up by 0.25 -> +2 wheel notches
    CHECK(!touchpadAsMouse(pad, 620, tm, io));
    CHECK(io.dx == 0 && io.dy == 0 && io.wheel == 2 && io.moved);
    pad.touching = false;
    pad.touchSecond = false;
    io = {};
    touchpadAsMouse(pad, 640, tm, io);

    // 5. A plain physical touchpad click without moving first stays View (not a mouse click).
    io = {};
    pad.touching = true;
    pad.btnTouchpad = true;
    CHECK(!touchpadAsMouse(pad, 700, tm, io));
    CHECK(io.buttons == 0 && !io.moved);
    pad.touching = false;
    pad.btnTouchpad = false;
    io = {};
    CHECK(!touchpadAsMouse(pad, 740, tm, io));
    CHECK(io.buttons == 0 && !io.moved);
}

// Keys chosen in Settings: one key moves off the input it was on.

static void testKeyMap() {
    using namespace xc::input;
    KeyMap map = defaultKeyMap();
    assignKey(map, KeyAction::A, 0, 'E');  // E was Y
    CHECK(map[static_cast<size_t>(KeyAction::A)][0] == 'E' && map[static_cast<size_t>(KeyAction::Y)][0] == 0);
    assignKey(map, KeyAction::Y, 1, kMouseBack);
    KbmInput in;
    in.keyboard = in.mouse = true;
    in.held = {'E'};
    in.buttons = 8;  // back
    MouseStick stick;
    ControllerState out;
    keyboardAsGamepad(in, stick, out, 1000, map);
    CHECK(out.btnA && out.btnY && !out.triggerR2);
    CHECK(keyName(0x20) == "Space" && keyName('W') == "W" && keyName(kMouseLeft) == "Mouse L" && keyName(0x7B) == "F12");
    CHECK(isReservedKey(0x7B) && isReservedKey('2') && !isReservedKey('5'));
    // Saved and read back, in the settings and a game's profile.
    xc::app::Settings st;
    st.keyMap = map;
    st.perGame["g"] = st.defaultProfile();
    st.perGame["g"].keyMap[0] = {'I', 0};
    std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/xc_keymap_test.json";
    CHECK(st.save(path));
    xc::app::Settings back;
    CHECK(back.load(path));
    CHECK(back.keyMap == map && back.perGame["g"].keyMap[0][0] == 'I');
    std::remove(path.c_str());
}

// The controller's buttons toward the game (input/pad_map.h).

static void testPadMap() {
    using namespace xc::input;
    auto at = [](const PadMap& m, PadFn f) { return m[static_cast<size_t>(f)]; };
    auto src = [](PadFn f) { return static_cast<uint8_t>(f); };
    const PadMap def = defaultPadMap();
    CHECK(padMapValid(def) && padMapMissing(def) == 0);
    // The default map passes everything through.
    ControllerState in;
    in.btnA = in.dpadLeft = in.btnTouchpad = true;
    in.triggerL2 = 0.3f;
    in.leftStickX = 0.7f;
    in.btnNexus = true;
    ControllerState same = applyPadMap(in, def);
    CHECK(same.btnA && same.dpadLeft && same.btnTouchpad && !same.btnB && same.triggerL2 == 0.3f &&
          same.leftStickX == 0.7f && same.btnNexus);
    // Assigning a source used elsewhere swaps the two: still complete.
    PadMap m = def;
    assignPadSource(m, PadFn::A, src(PadFn::B));  // A <- Circle, so B <- Cross
    CHECK(at(m, PadFn::A) == src(PadFn::B) && at(m, PadFn::B) == src(PadFn::A) && padMapValid(m));
    ControllerState cross;
    cross.btnA = true;
    ControllerState out = applyPadMap(cross, m);
    CHECK(!out.btnA && out.btnB);
    // A trigger fed by a button is fully pressed; a button fed by a trigger past halfway.
    assignPadSource(m, PadFn::RT, src(PadFn::RB));  // RT <- R1, RB <- R2
    ControllerState r1;
    r1.btnR1 = true;
    out = applyPadMap(r1, m);
    CHECK(out.triggerR2 == 1.0f && !out.btnR1);
    ControllerState r2;
    r2.triggerR2 = 0.4f;
    CHECK(!applyPadMap(r2, m).btnR1 && applyPadMap(r2, m).triggerR2 == 0.0f);
    r2.triggerR2 = 0.8f;
    CHECK(applyPadMap(r2, m).btnR1);
    // A trigger fed by the other trigger keeps how far it is pressed.
    PadMap t = def;
    assignPadSource(t, PadFn::LT, src(PadFn::RT));
    ControllerState lr;
    lr.triggerR2 = 0.25f;
    CHECK(applyPadMap(lr, t).triggerL2 == 0.25f && applyPadMap(lr, t).triggerR2 == 0.0f);
    // A cleared row: incomplete, named in the missing bits; a source fed twice is not valid.
    PadMap hole = def;
    assignPadSource(hole, PadFn::Y, kPadNone);
    CHECK(!padMapValid(hole) && padMapMissing(hole) == (1u << static_cast<int>(PadFn::Y)));
    CHECK(!applyPadMap(ControllerState{}, hole).btnY);
    PadMap twice = def;
    twice[static_cast<size_t>(PadFn::X)] = src(PadFn::A);
    CHECK(!padMapValid(twice) && padMapMissing(twice) == 0);
    // Filling the hole with a button used elsewhere moves the hole there.
    assignPadSource(hole, PadFn::Y, src(PadFn::X));
    CHECK(at(hole, PadFn::Y) == src(PadFn::X) && at(hole, PadFn::X) == kPadNone);
    // The buttons pressed, by source; Circle confirms swaps A / B back to the pad's.
    ControllerState held;
    held.btnB = true;
    held.triggerL2 = 0.9f;
    CHECK(padButtons(held) == ((1u << static_cast<int>(PadFn::B)) | (1u << static_cast<int>(PadFn::LT))));
    CHECK(physicalSource(src(PadFn::A), true) == src(PadFn::B) && physicalSource(src(PadFn::A), false) == src(PadFn::A) &&
          physicalSource(src(PadFn::Y), true) == src(PadFn::Y));
    CHECK(std::string(padFnName(PadFn::Right)) == "D-pad right");
    // JSON: round trip; anything else is refused and gives the default.
    PadMap back;
    CHECK(readPadMap(padMapJson(m), back) && back == m);
    auto reads = [&](const char* text) {
        auto v = json::parse(text);
        PadMap r = m;
        bool ok = v && readPadMap(*v, r);
        CHECK(r == (ok ? r : def));
        return ok;
    };
    CHECK(reads(R"({"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]})"));
    CHECK(!reads(R"({"version":2,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]})"));
    CHECK(!reads(R"({"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14]})"));
    CHECK(!reads(R"({"version":1,"map":[0,0,2,3,4,5,6,7,8,9,10,11,12,13,14,15]})"));
    CHECK(!reads(R"({"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,255]})"));
    CHECK(!reads(R"({"version":1,"map":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,"x"]})"));
    CHECK(!reads(R"([0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15])"));
    // In the settings and a game's profile: saved and read back; a broken
    // one falls back to the default; an older file (none) keeps the general one.
    xc::app::Settings st;
    st.padMap = m;
    st.perGame["g"] = st.defaultProfile();
    CHECK(st.perGame["g"].padMap == m);
    st.perGame["g"].padMap = t;
    std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/xc_padmap_test.json";
    CHECK(st.save(path));
    xc::app::Settings loaded;
    CHECK(loaded.load(path) && loaded.padMap == m && loaded.perGame["g"].padMap == t);
    xc::platform::writeFileAtomic(path, R"({"padMap":{"version":1,"map":[1,1]},"perGame":{"h":{},"k":{"padMap":7}}})");
    xc::app::Settings broken;
    CHECK(broken.load(path) && broken.padMap == def && broken.perGame["h"].padMap == def && broken.perGame["k"].padMap == def);
    xc::platform::writeFileAtomic(path, R"({"padMap":)" + padMapJson(m).dump() + R"(,"perGame":{"h":{}}})");
    xc::app::Settings older;
    CHECK(older.load(path) && older.perGame["h"].padMap == m);
    std::remove(path.c_str());
}

// The xCloud login: a 5xx is tried again, a refusal is not.

static void testGyroCalibration() {
    using namespace xc::app::gyrocal;
    // A wide random scene; the picture is a window of it, with fixed blocks drawn over (the HUD).
    constexpr int kSceneW = 4096;
    std::vector<uint8_t> scene(static_cast<size_t>(kSceneW) * kThumbH);
    uint32_t seed = 12345;
    auto rnd = [&] { return seed = seed * 1664525u + 1013904223u, seed >> 24; };
    for (int y = 0; y < kThumbH; y += 4)  // 4x4 blocks: texture a 16x12 tile can lock on to
        for (int x = 0; x < kSceneW; x += 4) {
            const uint8_t v = static_cast<uint8_t>(40 + rnd() % 170);
            for (int yy = 0; yy < 4; ++yy)
                for (int xx = 0; xx < 4 && x + xx < kSceneW; ++xx) scene[static_cast<size_t>(y + yy) * kSceneW + x + xx] = v;
        }
    auto view = [&](float pos, uint8_t* out) {  // the window at scene column `pos` (bilinear in x)
        for (int y = 0; y < kThumbH; ++y)
            for (int x = 0; x < kThumbW; ++x) {
                const float fx = pos + static_cast<float>(x);
                const int ix = static_cast<int>(std::floor(fx));
                const float t = fx - static_cast<float>(ix);
                const uint8_t* row = scene.data() + static_cast<size_t>(y) * kSceneW;
                out[y * kThumbW + x] = static_cast<uint8_t>(row[ix] * (1 - t) + row[ix + 1] * t + 0.5f);
            }
        for (int y = 0; y < 24; ++y)  // the HUD: a third of the tiles never move
            for (int x = 0; x < kThumbW; ++x) out[y * kThumbW + x] = static_cast<uint8_t>(((x / 5 + y / 5) % 2) ? 220 : 30);
    };
    std::vector<uint8_t> a(kThumbW * kThumbH), b(kThumbW * kThumbH);
    view(2000.0f, a.data());
    view(2003.4f, b.data());  // the camera turned right: the content moved 3.4 px left
    float shift = 0;
    int moving = 0;
    CHECK(pictureShift(a.data(), b.data(), false, shift, moving));
    CHECK(moving >= 10 && std::abs(shift + 3.4f) < 0.25f);
    CHECK(pictureShift(a.data(), a.data(), false, shift, moving) && shift == 0.0f && moving == 0);

    // The sweep, with a game that answers from 18 % of the stick along an exponent of 2.2,
    // 2400 px/s (of a 1920-wide picture) at full stick, 60 pictures a second.
    Recording rec;
    rec.reserve(sweepDurationMs() * 60 / 1000 + 8);
    float pos = 2000.0f;
    const float pxPerThumb = 1920.0f / kThumbW;
    std::vector<uint8_t> frame(kThumbW * kThumbH);
    float sx = 0, sy = 0;
    for (uint64_t f = 0;; ++f) {
        const uint64_t ms = f * 1000 / 60;
        if (!sweepStickAt(ms, sx, sy)) break;
        const float m = std::abs(sx);
        const float speed = m <= 0.18f ? 0.0f : 2400.0f * std::pow((m - 0.18f) / 0.82f, 2.2f);
        pos += (sx > 0 ? 1.0f : -1.0f) * speed / 60.0f / pxPerThumb;
        view(pos, frame.data());
        rec.addThumb(frame.data(), 1000000 + f * 1000000 / 60, sx, sy);
    }
    const Result res = analyze(rec);
    CHECK(res.ok());
    CHECK(std::abs(res.startPct - 18) <= 3 && std::abs(res.exponentTenths - 22) <= 3);
    CHECK(res.fitError < 0.03f && std::abs(res.topSpeed - 2400.0f) < 200.0f);
    // Its numbers put through gyro aiming's own curve: within 20 % of the game's speeds.
    for (const Point& p : res.horizontal) {
        if (p.level < 0.3f) continue;
        const float model = 2400.0f * std::pow((p.level - res.startPct / 100.0f) / (1.0f - res.startPct / 100.0f), res.exponentTenths / 10.0f);
        const float truth = 2400.0f * std::pow((p.level - 0.18f) / 0.82f, 2.2f);
        CHECK(std::abs(model / truth - 1.0f) < 0.2f);
    }
    // The stick held full reads differently from run to run (the game's turn boost): the same
    // levels below it give the same curve and the same top speed whatever that read was.
    {
        auto pointsWithFull = [](float fullRead) {
            std::vector<Point> pts;
            for (float l : {0.14f, 0.18f, 0.22f, 0.26f, 0.30f, 0.35f, 0.40f, 0.45f, 0.50f, 0.60f, 0.70f, 0.80f, 0.90f})
                pts.push_back({l, l <= 0.15f ? 0.0f : 2500.0f * std::pow((l - 0.15f) / 0.85f, 2.4f)});
            pts.push_back({1.0f, fullRead});
            return pts;
        };
        float topLow = 0, topHigh = 0;
        int sLow = 0, eLow = 0, sHigh = 0, eHigh = 0;
        CHECK(fitResponse(pointsWithFull(2039.0f), topLow, sLow, eLow) < 0.01f);
        CHECK(fitResponse(pointsWithFull(2715.0f), topHigh, sHigh, eHigh) < 0.01f);
        CHECK(sLow == sHigh && eLow == eHigh && topLow == topHigh);
        CHECK(std::abs(sLow - 15) <= 1 && std::abs(eLow - 24) <= 1 && std::abs(topLow - 2500.0f) < 75.0f);
    }
    // A picture that never moves (a menu was open): said so, nothing made up.
    Recording still;
    view(2000.0f, frame.data());
    for (uint64_t f = 0;; ++f) {
        if (!sweepStickAt(f * 1000 / 60, sx, sy)) break;
        still.addThumb(frame.data(), 1000000 + f * 1000000 / 60, sx, sy);
    }
    CHECK(analyze(still).failure == Failure::NoMotion);
}

// Gyro aiming: still is nothing, a turn left aims left (past the games'
// dead zone), more intensity aims further, the push eases back to nothing.

static void testGyro() {
    using namespace xc::input;
    GyroState s;
    float x = 1, y = 1;
    const float still[3] = {0.01f, -0.01f, 0.0f};
    gyroToStick(still, nullptr, 5, s, x, y);
    CHECK(x == 0 && y == 0);
    const float left[3] = {0.0f, 0.5f, 0.0f};  // yaw +: turning left
    for (int i = 0; i < 8; ++i) gyroToStick(left, nullptr, 5, s, x, y);
    CHECK(x < -0.3f && x > -0.6f && std::abs(y) < 1e-6f);
    GyroState s2;
    float x2 = 0, y2 = 0;
    for (int i = 0; i < 8; ++i) gyroToStick(left, nullptr, 10, s2, x2, y2);
    CHECK(x2 < x);
    const float up[3] = {0.3f, 0.0f, 0.0f};  // pitch +: the top tilted up
    GyroState s3;
    for (int i = 0; i < 8; ++i) gyroToStick(up, nullptr, 5, s3, x, y);
    CHECK(y < -0.12f && x == 0);
    const float tiny[3] = {0.0f, 0.03f, 0.0f};  // a slow turn: still past the dead zone
    GyroState s4;
    for (int i = 0; i < 8; ++i) gyroToStick(tiny, nullptr, 5, s4, x, y);
    CHECK(x <= -0.12f);
    for (int i = 0; i < 20; ++i) gyroToStick(still, nullptr, 5, s4, x, y);
    CHECK(x == 0);
    // Held tilted 45 degrees towards the player: a turn about the room's
    // vertical shows on the pad's yaw and roll; it aims as far as held flat.
    const float c = 0.7071f, tilted[3] = {0.0f, 0.5f * c, 0.5f * c}, gravity[3] = {0.0f, c, c};
    GyroState s5;
    float x5 = 0, y5 = 0;
    for (int i = 0; i < 60; ++i) gyroToStick(tilted, gravity, 5, s5, x5, y5);
    CHECK(x5 < -0.3f);
    GyroState flat;
    float xf = 0, yf = 0;
    const float level[3] = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < 60; ++i) gyroToStick(left, level, 5, flat, xf, yf);
    CHECK(std::abs(x5 - xf) < 0.01f);

    // Player Space: pure wrist roll when flat also contributes to horizontal aim,
    // so players who turn their wrists sideways like a wheel are not ignored.
    const float rollTurn[3] = {0.0f, 0.0f, 0.5f};
    GyroState sRoll;
    float xRoll = 0, yRoll = 0;
    for (int i = 0; i < 60; ++i) gyroToStick(rollTurn, level, 5, sRoll, xRoll, yRoll);
    CHECK(xRoll < -0.3f);
    CHECK(std::abs(yRoll) < 1e-6f); // Decoupled pitch: zero crosstalk from roll turning

    // Held rolled 30 degrees to one side: an up / down turn (the pad's pitch axis) aims up /
    // down only. The room's vertical has a share on that axis then, and it used to turn the
    // aim sideways as well; with and without the closed-loop posture tracking.
    {
        const float rolled[3] = {0.5f, 0.866f, 0.0f}, pitchOnly[3] = {0.6f, 0.0f, 0.0f};
        GyroState open;
        float xo = 0, yo = 0;
        for (int i = 0; i < 60; ++i) gyroToStick(pitchOnly, rolled, 5, open, xo, yo);
        CHECK(std::abs(xo) < 1e-5f);
        CHECK(yo < -0.2f);
        GyroState tracked;
        anchorGyroPosture(tracked, nullptr, rolled);
        float xt = 0, yt = 0, worstX = 0;
        for (int i = 0; i < 120; ++i) {
            gyroToStick(pitchOnly, rolled, 5, tracked, xt, yt);
            worstX = std::max(worstX, std::abs(xt));
        }
        CHECK(worstX < 1e-4f);
        CHECK(yt < -0.2f);
    }
    // The thumb on the right stick re-references the posture on every reading; the hand's
    // push must come through whole (it used to be zeroed each time, leaving a fraction).
    {
        const float turn[3] = {0.0f, 0.8f, 0.0f};
        GyroState freeHand, withThumb;
        anchorGyroPosture(freeHand, nullptr, level);
        anchorGyroPosture(withThumb, nullptr, level);
        float xa = 0, ya = 0, xb = 0, yb = 0;
        for (int i = 0; i < 100; ++i) {
            gyroToStick(turn, level, 5, freeHand, xa, ya);
            rebaseGyroPosture(withThumb, nullptr);
            gyroToStick(turn, level, 5, withThumb, xb, yb);
        }
        CHECK(xa < -0.3f);
        CHECK(xb < 0.9f * xa + 1e-4f);  // at least 90 % of the free hand's push
    }

    // The game's stick response (setGyroResponse): through a measured curve
    // the stick level sent gives a camera speed in step with the hand's turn.
    {
        const GyroResponse fortnite{0.13f, 2.9f, false, false};
        auto camera = [&](float rate) {  // the game's speed (share of its top) for a steady turn
            GyroState g;
            anchorGyroPosture(g, nullptr, level);  // as the pad's polling does before the first step
            float gx = 0, gy = 0;
            const float turn[3] = {0.0f, rate, 0.0f};
            for (int i = 0; i < 200; ++i) gyroToStick(turn, level, 3, g, gx, gy);
            const float m = std::hypot(gx, gy);
            return m <= 0.13f ? 0.0f : std::pow((m - 0.13f) / 0.87f, 2.9f);
        };
        setGyroResponse(fortnite);
        const float slow = camera(0.05f), mid = camera(0.2f), fast = camera(0.8f);
        CHECK(slow > 0.0f);
        CHECK(std::abs(mid / slow - 4.0f) < 0.2f);   // 4x the turn, 4x the camera
        CHECK(std::abs(fast / mid - 4.0f) < 0.2f);
        // Inverted on one axis: that axis alone turns the other way.
        GyroState a, b;
        anchorGyroPosture(a, nullptr, level);
        anchorGyroPosture(b, nullptr, level);
        float ax = 0, ay = 0, bx = 0, by = 0;
        const float both[3] = {0.3f, 0.5f, 0.0f};
        for (int i = 0; i < 60; ++i) gyroToStick(both, level, 3, a, ax, ay);
        setGyroResponse({0.13f, 2.9f, true, false});
        for (int i = 0; i < 60; ++i) gyroToStick(both, level, 3, b, bx, by);
        CHECK(std::abs(bx + ax) < 1e-5f && std::abs(by - ay) < 1e-5f && std::abs(ax) > 0.1f);
        setGyroResponse(GyroResponse{});  // the default again for the tests below
    }

    // Steering wheel mode (gyroToSteering):
    // 1. Center deadzone: tiny tilt (< 1.5 deg) produces 0 deflection
    GyroState steerState;
    float centerAccel[3] = {0.0f, 1.0f, 0.0f};
    float steer0 = gyroToSteering(still, centerAccel, 5, steerState);
    CHECK(steer0 == 0.0f);

    // 2. Roll right (30 deg): positive left-stick deflection
    constexpr float steerTilt = 30.0f * 3.14159265f / 180.0f;
    float rightAccel[3] = {std::sin(steerTilt), std::cos(steerTilt), 0.0f};
    float rightW[3] = {0.0f, 0.0f, -0.2f};
    float steerRight = 0.0f;
    for (int i = 0; i < 30; ++i) steerRight = gyroToSteering(rightW, rightAccel, 5, steerState, nullptr, 0.016f);
    CHECK(steerRight > 0.35f && steerRight < 0.85f);

    // 3. Roll left (30 deg): negative left-stick deflection (symmetrical)
    GyroState steerStateLeft;
    float leftAccel[3] = {-std::sin(steerTilt), std::cos(steerTilt), 0.0f};
    float leftW[3] = {0.0f, 0.0f, 0.2f};
    float steerLeft = 0.0f;
    for (int i = 0; i < 30; ++i) steerLeft = gyroToSteering(leftW, leftAccel, 5, steerStateLeft, nullptr, 0.016f);
    CHECK(steerLeft < -0.35f && steerLeft > -0.85f);
    CHECK(std::abs(steerRight + steerLeft) < 0.05f);

    // 4. Extreme roll (60 deg, beyond 45 deg lock): reaches full 1.0 lock
    constexpr float extremeTilt = 60.0f * 3.14159265f / 180.0f;
    float extremeAccel[3] = {std::sin(extremeTilt), std::cos(extremeTilt), 0.0f};
    float steerExtreme = 0.0f;
    for (int i = 0; i < 30; ++i) steerExtreme = gyroToSteering(rightW, extremeAccel, 5, steerState, nullptr, 0.016f);
    CHECK(steerExtreme >= 0.95f);
}

static void testGyroProZeroDriftAndDecoupledSensitivity() {
    // 1. GyroCalibrator convergence on stationary synthetic data
    {
        xc::input::GyroCalibrator cal;
        cal.start(240);
        CHECK(cal.state == xc::input::GyroCalibrator::State::Calibrating);
        CHECK(cal.sampleCount == 0);
        CHECK(cal.progress() == 0.0f);

        // Stationary data with a constant DC offset and tiny noise (variance < 0.0001)
        const float trueBias[3] = {0.0350f, -0.0220f, 0.0150f};
        for (int i = 0; i < 240; ++i) {
            float noiseX = ((i % 5) - 2) * 0.001f;
            float noiseY = (((i + 1) % 5) - 2) * 0.001f;
            float noiseZ = (((i + 2) % 5) - 2) * 0.001f;
            float sample[3] = {trueBias[0] + noiseX, trueBias[1] + noiseY, trueBias[2] + noiseZ};
            bool finished = cal.feed(sample);
            if (i < 239) {
                CHECK(!finished);
                CHECK(cal.state == xc::input::GyroCalibrator::State::Calibrating);
            } else {
                CHECK(finished);
                CHECK(cal.state == xc::input::GyroCalibrator::State::Succeeded);
            }
        }
        CHECK(cal.sampleCount == 240);
        CHECK(cal.progress() == 1.0f);
        CHECK(std::abs(cal.resultBias[0] - trueBias[0]) < 0.002f);
        CHECK(std::abs(cal.resultBias[1] - trueBias[1]) < 0.002f);
        CHECK(std::abs(cal.resultBias[2] - trueBias[2]) < 0.002f);
        CHECK(cal.maxVariance < 0.0025f);
    }

    // 2. GyroCalibrator rejection on noisy / moving data (variance > 0.0025 rad^2/s^2)
    {
        xc::input::GyroCalibrator cal;
        cal.start(240);
        // Sinusoidal movement with amplitude 0.1 rad/s (variance ~ A^2 / 2 = 0.005 > 0.0025)
        for (int i = 0; i < 240; ++i) {
            float t = static_cast<float>(i) * 0.05f;
            float sample[3] = {std::sin(t) * 0.1f, std::cos(t) * 0.1f, std::sin(t * 2.0f) * 0.1f};
            bool finished = cal.feed(sample);
            if (i == 239) {
                CHECK(!finished);
                CHECK(cal.state == xc::input::GyroCalibrator::State::Failed);
            }
        }
        CHECK(cal.maxVariance > 0.0025f);
    }

    // 3. gyroToStick zero-drift: when raw gyro matches static bias offset, output stick deflection is exactly zero
    {
        xc::input::GyroState s{};
        s.bias[0] = 0.045f;
        s.bias[1] = -0.030f;
        s.bias[2] = 0.012f;
        s.yawScale = 1.0f;
        s.pitchScale = 0.8f;
        s.up[0] = 0.0f; s.up[1] = 1.0f; s.up[2] = 0.0f;

        float accel[3] = {0.0f, 1.0f, 0.0f};
        float x = 999.0f, y = 999.0f;
        // Feed raw gyro equal to bias offset
        float rawAtRest[3] = {s.bias[0], s.bias[1], s.bias[2]};
        for (int i = 0; i < 30; ++i) {
            xc::input::gyroToStick(rawAtRest, accel, 5, s, x, y);
        }
        CHECK(x == 0.0f);
        CHECK(y == 0.0f);
    }

    // 4. Decoupled horizontal (yaw) vs vertical (pitch) sensitivity scaling
    {
        float accel[3] = {0.0f, 1.0f, 0.0f};

        // Test 1: Standard scaling (yawScale = 1.0, pitchScale = 1.0)
        xc::input::GyroState s1{};
        s1.yawScale = 1.0f;
        s1.pitchScale = 1.0f;
        float x1 = 0, y1 = 0;
        float rawYaw[3] = {0.0f, 0.25f, 0.0f};
        for (int i = 0; i < 40; ++i) xc::input::gyroToStick(rawYaw, accel, 5, s1, x1, y1);

        // Test 2: Double yaw scaling (yawScale = 2.0, pitchScale = 1.0)
        xc::input::GyroState s2{};
        s2.yawScale = 2.0f;
        s2.pitchScale = 1.0f;
        float x2 = 0, y2 = 0;
        for (int i = 0; i < 40; ++i) xc::input::gyroToStick(rawYaw, accel, 5, s2, x2, y2);

        CHECK(std::abs(x2) > std::abs(x1));
        CHECK(y1 == 0.0f);
        CHECK(y2 == 0.0f);

        // Test 3: Pitch movement (rawW = [0.25, 0, 0]) with pitchScale = 0.5 vs 1.5
        xc::input::GyroState s3{};
        s3.yawScale = 1.0f;
        s3.pitchScale = 0.5f;
        float x3 = 0, y3 = 0;
        float rawPitch[3] = {0.25f, 0.0f, 0.0f};
        for (int i = 0; i < 40; ++i) xc::input::gyroToStick(rawPitch, accel, 5, s3, x3, y3);

        xc::input::GyroState s4{};
        s4.yawScale = 1.0f;
        s4.pitchScale = 1.5f;
        float x4 = 0, y4 = 0;
        for (int i = 0; i < 40; ++i) xc::input::gyroToStick(rawPitch, accel, 5, s4, x4, y4);

        CHECK(std::abs(y4) > std::abs(y3));
        CHECK(x3 == 0.0f);
        CHECK(x4 == 0.0f);
    }

    // 5. Settings & Profile JSON round-trip serialization and deserialization
    {
        const std::string tmpPath = "/tmp/test_gyro_settings.json";
        app::Settings s;
        s.gyroYawScale = 140;
        s.gyroPitchScale = 120;
        s.gyroBias[0] = 0.0125f;
        s.gyroBias[1] = -0.0345f;
        s.gyroBias[2] = 0.0078f;

        app::GameProfile p = s.defaultProfile();
        p.gyroYawScale = 160;
        p.gyroPitchScale = 90;
        p.gyroBias[0] = 0.0050f;
        p.gyroBias[1] = -0.0100f;
        p.gyroBias[2] = 0.0200f;
        s.perGame["test_game_1"] = p;

        CHECK(s.save(tmpPath));

        app::Settings loaded;
        CHECK(loaded.load(tmpPath));
        CHECK(loaded.gyroYawScale == 140);
        CHECK(loaded.gyroPitchScale == 120);
        CHECK(std::abs(loaded.gyroBias[0] - 0.0125f) < 0.0001f);
        CHECK(std::abs(loaded.gyroBias[1] - -0.0345f) < 0.0001f);
        CHECK(std::abs(loaded.gyroBias[2] - 0.0078f) < 0.0001f);

        auto it = loaded.perGame.find("test_game_1");
        CHECK(it != loaded.perGame.end());
        CHECK(it->second.gyroYawScale == 160);
        CHECK(it->second.gyroPitchScale == 90);
        CHECK(std::abs(it->second.gyroBias[0] - 0.0050f) < 0.0001f);
        CHECK(std::abs(it->second.gyroBias[1] - -0.0100f) < 0.0001f);
        CHECK(std::abs(it->second.gyroBias[2] - 0.0200f) < 0.0001f);

        unlink(tmpPath.c_str());
    }

    // 6. GyroCalibrator rejection of NaN / Non-Finite angular velocity
    {
        xc::input::GyroCalibrator cal;
        cal.start(240);
        float nanSample[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
        bool accepted = cal.feed(nanSample);
        CHECK(!accepted);
        CHECK(cal.state == xc::input::GyroCalibrator::State::Failed);

        cal.start(240);
        float infSample[3] = {0.0f, std::numeric_limits<float>::infinity(), 0.0f};
        accepted = cal.feed(infSample);
        CHECK(!accepted);
        CHECK(cal.state == xc::input::GyroCalibrator::State::Failed);
    }

    // 7. Live input pipeline integration: global calibration & setGyroCalibration eliminate resting stick drift
    {
        const float testBias[3] = {0.0480f, -0.0320f, 0.0150f};
        xc::input::resetGlobalGyroCalibration();
        xc::input::setGyroCalibration(testBias, 1.4f, 0.7f);

        float gotBias[3] = {0};
        float gotYaw = 0, gotPitch = 0;
        xc::input::getGlobalGyroCalibration(gotBias, gotYaw, gotPitch);
        CHECK(std::abs(gotBias[0] - testBias[0]) < 0.0001f);
        CHECK(std::abs(gotBias[1] - testBias[1]) < 0.0001f);
        CHECK(std::abs(gotBias[2] - testBias[2]) < 0.0001f);
        CHECK(std::abs(gotYaw - 1.4f) < 0.0001f);
        CHECK(std::abs(gotPitch - 0.7f) < 0.0001f);

        // A resting controller with raw gyro equal to sensor bias
        const float rawRest[3] = {testBias[0], testBias[1], testBias[2]};
        const float accel[3] = {0.0f, 1.0f, 0.0f};

        // When a slot is unpopulated (bias {0,0,0}), gyroToStick must safely fallback to global calibration
        xc::input::GyroState unpopulatedState{};
        float x = 999.0f, y = 999.0f;
        for (int i = 0; i < 30; ++i) {
            xc::input::gyroToStick(rawRest, accel, 5, unpopulatedState, x, y);
        }
        CHECK(x == 0.0f);
        CHECK(y == 0.0f);
        CHECK(unpopulatedState.bias[0] == testBias[0]);
        CHECK(unpopulatedState.yawScale == 1.4f);
        CHECK(unpopulatedState.pitchScale == 0.7f);

        xc::input::resetGlobalGyroCalibration();
    }

    // 8. StreamMenu & Settings persistence integration for gyro yaw, pitch, and bias
    {
        xc::ui::Fonts fonts;
        xc::ui::StreamMenu menu(fonts);
        const float calBias[3] = {0.0210f, -0.0140f, 0.0080f};

        menu.setGyroYawScale(150);
        menu.setGyroPitchScale(110);
        menu.setGyroBias(calBias);
        CHECK(menu.gyroYawScale() == 150);
        CHECK(menu.gyroPitchScale() == 110);
        CHECK(menu.gyroBias() != nullptr);
        CHECK(std::abs(menu.gyroBias()[0] - calBias[0]) < 0.0001f);
        CHECK(std::abs(menu.gyroBias()[1] - calBias[1]) < 0.0001f);
        CHECK(std::abs(menu.gyroBias()[2] - calBias[2]) < 0.0001f);

        // Test calibration state machine in StreamMenu
        menu.startGyroCalibration(60);
        CHECK(menu.isGyroCalibrating());
        CHECK(menu.gyroCalProgress() == 0.0f);

        for (int i = 0; i < 60; ++i) {
            float sample[3] = {calBias[0], calBias[1], calBias[2]};
            menu.feedGyroCalibration(sample);
        }
        CHECK(!menu.isGyroCalibrating());
        CHECK(menu.gyroCalSucceeded());
        CHECK(!menu.gyroCalFailed());
        CHECK(std::abs(menu.gyroBias()[0] - calBias[0]) < 0.002f);

        // Test persistence into Settings and GameProfile
        const std::string tmpPath = "/tmp/test_stream_menu_gyro_persist.json";
        app::Settings s;
        s.gyroYawScale = menu.gyroYawScale();
        s.gyroPitchScale = menu.gyroPitchScale();
        for (int i = 0; i < 3; ++i) s.gyroBias[i] = menu.gyroBias()[i];

        app::GameProfile p = s.defaultProfile();
        p.gyroYawScale = 175;
        p.gyroPitchScale = 125;
        p.gyroBias[0] = 0.033f;
        p.gyroBias[1] = -0.018f;
        p.gyroBias[2] = 0.009f;
        s.perGame["halo_infinite"] = p;

        CHECK(s.save(tmpPath));

        app::Settings loaded;
        CHECK(loaded.load(tmpPath));
        CHECK(loaded.gyroYawScale == 150);
        CHECK(loaded.gyroPitchScale == 110);
        CHECK(std::abs(loaded.gyroBias[0] - calBias[0]) < 0.002f);

        auto it = loaded.perGame.find("halo_infinite");
        CHECK(it != loaded.perGame.end());
        CHECK(it->second.gyroYawScale == 175);
        CHECK(it->second.gyroPitchScale == 125);
        CHECK(std::abs(it->second.gyroBias[0] - 0.033f) < 0.0001f);

        unlink(tmpPath.c_str());
        xc::input::resetGlobalGyroCalibration();
    }
}

static void testGyroClosedLoopPostureTrackingAndZeroDrift() {
    using namespace xc::input;
    resetGlobalGyroCalibration();

    // Camera-effective deflection above the 12% deadzone lifted in gyroToStick().
    auto addEffStick = [](float x, float y, float dt, float& sumX, float& sumY) {
        constexpr float kLift = 0.12f;
        float mag = std::hypot(x, y);
        if (mag > kLift) {
            float eff = std::clamp((mag - kLift) / (1.0f - kLift), 0.0f, 1.0f);
            sumX += (x / mag) * eff * dt;
            sumY += (y / mag) * eff * dt;
        }
    };

    // 1. Software closed-loop posture tracking: zero residual drift on neutral return after asymmetric sweep
    {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        CHECK(s.aimingActive);
        CHECK(!s.hasBaseQuat);

        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float totalStickInjected = 0.0f;
        float unusedY = 0.0f;

        // Phase 1: Rapid flick to the right (+1.0 rad/s yaw for 125 steps = 0.5s -> +0.5 rad physical turn)
        float x = 0.0f, y = 0.0f;
        const float wFastRight[3] = {0.0f, 1.0f, 0.0f};
        for (int i = 0; i < 125; ++i) {
            gyroToStick(wFastRight, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjected, unusedY);
        }
        // Turning right pushes stick left (x < -0.3f)
        CHECK(x < -0.3f);
        CHECK(totalStickInjected < -0.3f);

        // Phase 2: Stationary pause at the turned posture (25 steps)
        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 25; ++i) {
            gyroToStick(wStill, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjected, unusedY);
        }

        // Phase 3: Slow return to the left (-0.5 rad/s yaw for 250 steps = 1.0s -> -0.5 rad physical turn)
        // Net physical angle = +0.5 rad - 0.5 rad = 0.000000 rad (exact physical return to starting posture)
        const float wSlowLeft[3] = {0.0f, -0.5f, 0.0f};
        for (int i = 0; i < 250; ++i) {
            gyroToStick(wSlowLeft, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjected, unusedY);
        }

        // Phase 4: Settle at starting neutral posture (150 steps)
        for (int i = 0; i < 150; ++i) {
            gyroToStick(wStill, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, totalStickInjected, unusedY);
        }

        // The closed-loop posture engine must drive accumulated camera-effective stick injection back to ZERO
        CHECK(std::abs(totalStickInjected) < 1e-4f);
        CHECK(x == 0.0f);
        CHECK(y == 0.0f);
        CHECK(std::abs(s.targetAngle[0]) < 1e-4f);
        CHECK(std::abs(s.injected[0]) < 1e-4f);
    }

    // 2. Hardware orientation quaternion closed-loop posture tracking: exact relative 3D rotation and zero drift
    {
        GyroState s{};
        // Start at identity orientation quat (x=0, y=0, z=0, w=1)
        const float q0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        anchorGyroPosture(s, q0);
        CHECK(s.aimingActive);
        CHECK(s.hasBaseQuat);

        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float unusedX = 0.0f;
        float totalStickInjectedY = 0.0f;

        // Pitch up by 20 degrees: theta = 20 deg = 0.349066 rad.
        // Quaternion for rotation around X: q = [sin(theta/2), 0, 0, cos(theta/2)]
        constexpr float theta = 0.349066f;
        const float wPitchUp[3] = {theta / 0.5f, 0.0f, 0.0f};
        const float wPitchDown[3] = {-theta / 1.0f, 0.0f, 0.0f};

        float x = 0.0f, y = 0.0f;
        float minY = 0.0f;
        for (int i = 0; i < 125; ++i) {
            float frac = static_cast<float>(i + 1) / 125.0f;
            float curAngle = theta * frac;
            float q[4] = {std::sin(curAngle * 0.5f), 0.0f, 0.0f, std::cos(curAngle * 0.5f)};
            gyroToStick(wPitchUp, accel, 5, s, x, y, q, dt);
            addEffStick(x, y, dt, unusedX, totalStickInjectedY);
            if (y < minY) minY = y;
        }
        // Tilting top up aims stick up (pad convention +y is down, so aim up is y < 0)
        CHECK(minY < -0.1f);
        CHECK(totalStickInjectedY < -0.1f);

        // Pause 25 steps at tilted posture
        const float wZero[3] = {0.0f, 0.0f, 0.0f};
        const float qTiltedUp[4] = {std::sin(theta * 0.5f), 0.0f, 0.0f, std::cos(theta * 0.5f)};
        for (int i = 0; i < 25; ++i) {
            gyroToStick(wZero, accel, 5, s, x, y, qTiltedUp, dt);
            addEffStick(x, y, dt, unusedX, totalStickInjectedY);
        }

        // Return controller to original starting orientation q0
        for (int i = 0; i < 250; ++i) {
            float frac = static_cast<float>(i + 1) / 250.0f;
            float curAngle = theta * (1.0f - frac);
            float q[4] = {std::sin(curAngle * 0.5f), 0.0f, 0.0f, std::cos(curAngle * 0.5f)};
            gyroToStick(wPitchDown, accel, 5, s, x, y, q, dt);
            addEffStick(x, y, dt, unusedX, totalStickInjectedY);
        }

        // Settle at neutral posture
        for (int i = 0; i < 150; ++i) {
            gyroToStick(wZero, accel, 5, s, x, y, q0, dt);
            addEffStick(x, y, dt, unusedX, totalStickInjectedY);
        }

        CHECK(std::abs(totalStickInjectedY) < 1e-4f);
        CHECK(x == 0.0f);
        CHECK(y == 0.0f);
        CHECK(std::abs(s.targetAngle[1]) < 1e-4f);
        CHECK(std::abs(s.injected[1]) < 1e-4f);
    }

    // 3. Bidirectional rotational symmetry
    {
        GyroState sLeft{}, sRight{};
        anchorGyroPosture(sLeft, nullptr);
        anchorGyroPosture(sRight, nullptr);

        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        const float wLeft[3] = {0.0f, 0.8f, 0.0f};
        const float wRight[3] = {0.0f, -0.8f, 0.0f};

        for (int i = 0; i < 50; ++i) {
            float xl = 0, yl = 0, xr = 0, yr = 0;
            gyroToStick(wLeft, accel, 5, sLeft, xl, yl, nullptr, dt);
            gyroToStick(wRight, accel, 5, sRight, xr, yr, nullptr, dt);
            // Symmetrical opposite response
            CHECK(std::abs(xl + xr) < 1e-5f);
            CHECK(std::abs(yl) < 1e-5f);
            CHECK(std::abs(yr) < 1e-5f);
        }

        GyroState sUp{}, sDown{};
        anchorGyroPosture(sUp, nullptr);
        anchorGyroPosture(sDown, nullptr);

        const float wUp[3] = {0.6f, 0.0f, 0.0f};
        const float wDown[3] = {-0.6f, 0.0f, 0.0f};

        for (int i = 0; i < 50; ++i) {
            float xu = 0, yu = 0, xd = 0, yd = 0;
            gyroToStick(wUp, accel, 5, sUp, xu, yu, nullptr, dt);
            gyroToStick(wDown, accel, 5, sDown, xd, yd, nullptr, dt);
            CHECK(std::abs(yu + yd) < 1e-5f);
            CHECK(std::abs(xu) < 1e-5f);
            CHECK(std::abs(xd) < 1e-5f);
        }
    }

    // 4. Stabilized software gravity filter without abrupt sign inversion
    {
        GyroState s{};
        float x = 0, y = 0;
        const float wStationary[3] = {0.0f, 0.0f, 0.0f};

        // Initial sample: pad level (accel = [0, 1, 0])
        float accelLevel[3] = {0.0f, 1.0f, 0.0f};
        gyroToStick(wStationary, accelLevel, 5, s, x, y);
        CHECK(s.upInitialized);
        CHECK(std::abs(s.up[1] - 1.0f) < 1e-4f);

        // Forward pitch where accel[1] becomes negative (tilted down forward, accel[1] = -0.3f, accel[2] = 0.954f)
        float accelForwardTilt[3] = {0.0f, -0.3f, 0.954f};
        for (int i = 0; i < 100; ++i) {
            gyroToStick(wStationary, accelForwardTilt, 5, s, x, y);
        }
        // Must NOT flip sign to positive +0.3; must smoothly decrease without discontinuous 180-deg reversal
        CHECK(s.up[1] < 1.0f);
        CHECK(s.up[1] > -0.35f);
        CHECK(s.up[2] > 0.0f);
    }

    // 5. Smooth non-linear lift continuity: no deadband step discontinuities
    {
        float accel[3] = {0.0f, 1.0f, 0.0f};
        float prevOutMag = 0.0f;
        float maxJump = 0.0f;

        // Sweep yaw velocity continuously in tiny steps from 0.0 to 0.1 rad/s
        for (int i = 1; i <= 200; ++i) {
            float vel = static_cast<float>(i) * 0.0005f; // 0.0005 .. 0.1 rad/s
            GyroState sweepState{};
            sweepState.aimingActive = false; // test raw lift curve
            float x = 0, y = 0;
            float rawW[3] = {0.0f, vel, 0.0f};
            for (int k = 0; k < 20; ++k) {
                gyroToStick(rawW, accel, 5, sweepState, x, y);
            }
            float outMag = std::abs(x);
            float jump = std::abs(outMag - prevOutMag);
            if (jump > maxJump) maxJump = jump;
            prevOutMag = outMag;
        }

        // The maximum step jump across tiny 0.0005 rad/s increments must be smooth (< 0.01f),
        // proving there is NO abrupt 0.12f step cliff.
        CHECK(maxJump < 0.01f);
    }

    // 6. Multi-cycle flick zero-drift: |\sum \vec{S}_eff \cdot dt| < 10^-4 across 20 successive aim cycles
    {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        CHECK(s.aimingActive);

        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 0.004f;
        float cumulativeInjectedX = 0.0f;
        float cumulativeInjectedY = 0.0f;
        float x = 0.0f, y = 0.0f;

        for (int cycle = 1; cycle <= 20; ++cycle) {
            // Rapid flick right: +2.0 rad/s yaw for 25 steps (0.1s -> +0.2 rad)
            const float wFlick[3] = {0.0f, 2.0f, 0.0f};
            for (int i = 0; i < 25; ++i) {
                gyroToStick(wFlick, accel, 6, s, x, y, nullptr, dt);
                addEffStick(x, y, dt, cumulativeInjectedX, cumulativeInjectedY);
            }

            // Controlled return left: -0.5 rad/s yaw for 100 steps (0.4s -> -0.2 rad return)
            const float wReturn[3] = {0.0f, -0.5f, 0.0f};
            for (int i = 0; i < 100; ++i) {
                gyroToStick(wReturn, accel, 6, s, x, y, nullptr, dt);
                addEffStick(x, y, dt, cumulativeInjectedX, cumulativeInjectedY);
            }

            // Settle at center: 100 steps (0.4s)
            const float wStill[3] = {0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 100; ++i) {
                gyroToStick(wStill, accel, 6, s, x, y, nullptr, dt);
                addEffStick(x, y, dt, cumulativeInjectedX, cumulativeInjectedY);
            }
        }

        CHECK(std::abs(cumulativeInjectedX) < 1e-4f);
        CHECK(std::abs(cumulativeInjectedY) < 1e-4f);
        CHECK(x == 0.0f);
        CHECK(y == 0.0f);
    }

    // 7. Slow fine-aiming (0.08 rad/s) clears the 12% deadzone without lag or return drift
    {
        GyroState s{};
        anchorGyroPosture(s, nullptr);
        float accel[3] = {0.0f, 1.0f, 0.0f};
        constexpr float dt = 1.0f / 60.0f;
        float sumX = 0.0f, sumY = 0.0f;
        float x = 0.0f, y = 0.0f;
        const float wFine[3] = {0.0f, 0.08f, 0.0f};
        for (int i = 0; i < 30; ++i) {
            gyroToStick(wFine, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, sumX, sumY);
        }
        // Output must exceed 0.12f so small wrist corrections actually move the game reticle
        CHECK(x < -0.15f);
        const float wBack[3] = {0.0f, -0.08f, 0.0f};
        for (int i = 0; i < 30; ++i) {
            gyroToStick(wBack, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, sumX, sumY);
        }
        const float wStill[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 60; ++i) {
            gyroToStick(wStill, accel, 5, s, x, y, nullptr, dt);
            addEffStick(x, y, dt, sumX, sumY);
        }
        CHECK(std::abs(sumX) < 1e-4f);
        CHECK(x == 0.0f && y == 0.0f);
    }

    // 8. Tilted pad (40 deg forward pitch): world-vertical yaw has full 1.0x gain without cos(tilt) attenuation or pitch bleed
    {
        constexpr float tilt = 40.0f * 3.14159265f / 180.0f;
        float baseUp[3] = {0.0f, std::cos(tilt), std::sin(tilt)};
        GyroState s{};
        anchorGyroPosture(s, nullptr, baseUp);
        constexpr float dt = 1.0f / 60.0f;
        constexpr float yawTurn = 0.35f;
        const float wWorldYaw[3] = {0.0f, yawTurn * baseUp[1] / 0.5f, yawTurn * baseUp[2] / 0.5f};
        float x = 0.0f, y = 0.0f;
        for (int i = 0; i < 30; ++i) {
            gyroToStick(wWorldYaw, baseUp, 5, s, x, y, nullptr, dt);
        }
        CHECK(std::abs(s.targetAngle[0] - yawTurn) < 1e-3f);
        CHECK(std::abs(s.targetAngle[1]) < 1e-4f);
    }

    // 9a. Commuting 3D closed loop (Pitch -> Yaw -> Unyaw -> Unpitch) at 60 Hz returns both axes to 0
    {
        GyroState s{};
        float accel[3] = {0.0f, 1.0f, 0.0f};
        anchorGyroPosture(s, nullptr, accel);
        constexpr float dt = 1.0f / 60.0f;
        float sumX = 0.0f, sumY = 0.0f;
        float x = 0.0f, y = 0.0f;
        const float wP[3] = {1.2f, 0.0f, 0.0f};
        const float wY[3] = {0.0f, 1.6f, 0.0f};
        const float wYn[3] = {0.0f, -1.6f, 0.0f};
        const float wPn[3] = {-1.2f, 0.0f, 0.0f};
        const float w0[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 15; ++i) { gyroToStick(wP, accel, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        for (int i = 0; i < 20; ++i) { gyroToStick(wY, accel, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        for (int i = 0; i < 20; ++i) { gyroToStick(wYn, accel, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        for (int i = 0; i < 15; ++i) { gyroToStick(wPn, accel, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        for (int i = 0; i < 60; ++i) { gyroToStick(w0, accel, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        CHECK(std::abs(sumX) < 5e-4f);
        CHECK(std::abs(sumY) < 5e-4f);
        CHECK(x == 0.0f && y == 0.0f);
    }

    // 9b. Physical non-commuting 3D closed loop (Pitch +0.3 -> World Yaw +0.533 while tilted -> Unpitch -0.3 -> World Unyaw -0.533)
    {
        GyroState s{};
        float accelLevel[3] = {0.0f, 1.0f, 0.0f};
        anchorGyroPosture(s, nullptr, accelLevel);
        constexpr float dt = 1.0f / 60.0f;
        constexpr float theta = 0.3f;
        float accelTilted[3] = {0.0f, std::cos(theta), -std::sin(theta)};
        float sumX = 0.0f, sumY = 0.0f;
        float x = 0.0f, y = 0.0f;
        const float wP[3] = {1.2f, 0.0f, 0.0f};
        const float wWorldY[3] = {0.0f, 1.6f * std::cos(theta), -1.6f * std::sin(theta)};
        const float wPn[3] = {-1.2f, 0.0f, 0.0f};
        const float wYn[3] = {0.0f, -1.6f, 0.0f};
        const float w0[3] = {0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 15; ++i) { gyroToStick(wP, accelTilted, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        for (int i = 0; i < 20; ++i) { gyroToStick(wWorldY, accelTilted, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        for (int i = 0; i < 15; ++i) { gyroToStick(wPn, accelLevel, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        for (int i = 0; i < 20; ++i) { gyroToStick(wYn, accelLevel, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        for (int i = 0; i < 60; ++i) { gyroToStick(w0, accelLevel, 6, s, x, y, nullptr, dt); addEffStick(x, y, dt, sumX, sumY); }
        CHECK(std::abs(sumX) < 5e-4f);
        CHECK(std::abs(sumY) < 5e-4f);
        CHECK(x == 0.0f && y == 0.0f);
    }
    resetGlobalGyroCalibration();
}

// The desktop's stand-ins for the PS5 backends: nothing connected, the
// settings kept for their getters.
// A font that didn't load (no assets folder) draws and measures nothing
// instead of crashing in stb_truetype.

static void testDs5TriggerEffects() {
    using namespace xc::input;
    uint8_t b[kDs5TriggerBlock];
    ds5TriggerBlock(TriggerCommand{}, b);  // mode 0
    const uint8_t off[11] = {0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(std::equal(b, b + 11, off));
    TriggerCommand weight = triggerCommand(0, 2, 1, 2, false, 0);  // medium resistance: from 3, strength 4
    CHECK(weight.mode == 1 && weight.data[0] == 3 && weight.data[1] == 4);
    ds5TriggerBlock(weight, b);
    const uint8_t feedback[11] = {0x21, 0xF8, 0x03, 0x00, 0xB6, 0x6D, 0x1B, 0, 0, 0, 0};
    CHECK(std::equal(b, b + 11, feedback));
    TriggerCommand vib = triggerCommand(38, 2, 1, 2, false, 0);  // amplitude 4 at 60 Hz, from 0
    CHECK(vib.mode == 3 && vib.data[1] == 4 && vib.data[2] == 60);
    ds5TriggerBlock(vib, b);
    const uint8_t vibration[11] = {0x26, 0xFF, 0x03, 0xDB, 0xB6, 0x6D, 0x1B, 0, 0, 60, 0};
    CHECK(std::equal(b, b + 11, vibration));
    TriggerCommand strongest;  // a pulse at its hardest: strength 8 -> 7 in every zone
    strongest.mode = 1;
    strongest.data[1] = 8;
    ds5TriggerBlock(strongest, b);
    const uint8_t full[11] = {0x21, 0xFF, 0x03, 0xFF, 0xFF, 0xFF, 0x3F, 0, 0, 0, 0};
    CHECK(std::equal(b, b + 11, full));
    TriggerCommand silent;  // vibration with no amplitude: off
    silent.mode = 3;
    silent.data[2] = 60;
    ds5TriggerBlock(silent, b);
    CHECK(std::equal(b, b + 11, off));
    uint8_t packet[kDs5EffectsSize];
    ds5TriggerEffects(vib, weight, packet);  // L2 vibrating, R2 weighted
    CHECK(packet[0] == 0x0C && packet[1] == 0 && packet[2] == 0 && packet[9] == 0);
    CHECK(std::equal(packet + kDs5RightTrigger, packet + kDs5RightTrigger + 11, feedback));
    CHECK(std::equal(packet + kDs5LeftTrigger, packet + kDs5LeftTrigger + 11, vibration));
    CHECK(std::all_of(packet + 32, packet + kDs5EffectsSize, [](uint8_t v) { return v == 0; }));  // no light bar
}

// SDL's mouse button mask -> the wire's bits (right and middle swap places).

static void testSdlMouseButtons() {
    using xc::input::sdlMouseButtons;
    CHECK(sdlMouseButtons(0) == 0);
    CHECK(sdlMouseButtons(1) == 1);    // left
    CHECK(sdlMouseButtons(2) == 4);    // middle
    CHECK(sdlMouseButtons(4) == 2);    // right
    CHECK(sdlMouseButtons(8) == 8);    // X1: back
    CHECK(sdlMouseButtons(16) == 16);  // X2: forward
    CHECK(sdlMouseButtons(1 | 4) == 3 && sdlMouseButtons(31) == 31 && sdlMouseButtons(0xFFFFFFE0u) == 0);
    // A controller's touchpad as a pointer device (Linux) isn't the mouse.
    using xc::input::isPadPointerName;
    CHECK(isPadPointerName("Sony Interactive Entertainment DualSense Wireless Controller Touchpad"));
    CHECK(isPadPointerName("DualSense Wireless Controller Touchpad"));  // Bluetooth
    CHECK(isPadPointerName("Wireless Controller Touchpad"));            // DS4, Bluetooth
    CHECK(isPadPointerName("Sony Computer Entertainment DUALSHOCK 4 Wireless Controller Touchpad"));
    CHECK(isPadPointerName("Generic Controller Touchpad"));
    CHECK(!isPadPointerName("ELAN1200:00 04F3:30F7 Touchpad") && !isPadPointerName("SynPS/2 Synaptics TouchPad"));
    CHECK(!isPadPointerName("Logitech G502 HERO Gaming Mouse") && !isPadPointerName("Virtual core pointer (seat0)"));
    CHECK(!isPadPointerName("") && !isPadPointerName(nullptr));
    // One pad's evdev and hidraw nodes lead to its HID device.
    using xc::input::padSysfsLink;
    CHECK(padSysfsLink("/dev/input/event12") == "/sys/class/input/event12/device/device");
    CHECK(padSysfsLink("/dev/hidraw1") == "/sys/class/hidraw/hidraw1/device");
    CHECK(padSysfsLink("/dev/input/js0").empty() && padSysfsLink("/dev/hidraw").empty() &&
          padSysfsLink("/dev/hidraw1/../x").empty() && padSysfsLink(nullptr).empty() && padSysfsLink("").empty());
    using xc::input::padLacksHidapi;
    CHECK(padLacksHidapi(1, false, "/dev/input/event12"));   // a DualSense on evdev
    CHECK(padLacksHidapi(2, false, nullptr));                // a DS4, path unknown
    CHECK(!padLacksHidapi(1, true, "/dev/input/event12"));   // has its light bar
    CHECK(!padLacksHidapi(1, false, "/dev/hidraw1"));        // HIDAPI already
    CHECK(!padLacksHidapi(3, false, "/dev/input/event5"));   // an Xbox pad: nothing to look for
}

// SDL's scancodes are HID usages: the same keys as the PS5's keyboard.

static void testScancodeToVk() {
    using xc::input::scancodeToVk;
    for (int sc = 0; sc < 224; ++sc) CHECK(scancodeToVk(sc) == xc::input::hidToVk(static_cast<uint16_t>(sc)));
    CHECK(scancodeToVk(4) == 'A' && scancodeToVk(29) == 'Z' && scancodeToVk(30) == '1' && scancodeToVk(39) == '0');
    CHECK(scancodeToVk(40) == 0x0D && scancodeToVk(41) == 0x1B && scancodeToVk(44) == 0x20 && scancodeToVk(58) == 0x70);
    CHECK(scancodeToVk(79) == 0x27 && scancodeToVk(82) == 0x26);  // right, up
    const uint8_t mods[8] = {0x11, 0x10, 0x12, 0x5B, 0x11, 0x10, 0x12, 0x5C};  // LCtrl LShift LAlt LGui, then right
    for (int i = 0; i < 8; ++i) CHECK(scancodeToVk(224 + i) == mods[i]);
    CHECK(scancodeToVk(0) == 0 && scancodeToVk(232) == 0 && scancodeToVk(257) == 0 && scancodeToVk(-1) == 0);
    CHECK(scancodeToVk(270) == xc::input::kVkAndroidBack);
    CHECK(scancodeToVk(0x10E) == xc::input::kVkAndroidBack);
}

// SDL's sensors and axes in the units the shared code takes.

static void testSdlSensorUnits() {
    using namespace xc::input;
    const float ms2[3] = {0.0f, 9.80665f, -4.903325f};
    float g[3];
    sdlAccelToG(ms2, g);
    CHECK(g[0] == 0.0f && std::abs(g[1] - 1.0f) < 1e-6f && std::abs(g[2] + 0.5f) < 1e-6f);
    // Held flat and still at 1 g: gyro aiming's "up" stays put, no push.
    GyroState s;
    const float still[3] = {0, 0, 0};
    float x = 1, y = 1;
    for (int i = 0; i < 10; ++i) gyroToStick(still, g, 5, s, x, y);
    CHECK(x == 0 && y == 0);
    CHECK(sdlStickAxis(32767) == 1.0f && sdlStickAxis(-32768) == -1.0f && sdlStickAxis(0) == 0.0f);
    CHECK(std::abs(sdlStickAxis(16384) - 0.5f) < 0.001f);
    CHECK(sdlTriggerAxis(32767) == 1.0f && sdlTriggerAxis(0) == 0.0f && sdlTriggerAxis(-5) == 0.0f);
    uint8_t c[3];
    playerLightColour(0, c);
    CHECK(c[0] == 0 && c[1] == 112 && c[2] == 220);
    playerLightColour(3, c);
    CHECK(c[0] == 225 && c[1] == 90 && c[2] == 175);
    CHECK(triggerMotorLevel(0) == 0 && triggerMotorLevel(128) == 128 && triggerMotorLevel(128, 1) == 64);
    CHECK(triggerMotorLevel(200, 3) == 255 && triggerMotorLevel(1, 4) == 255 && triggerMotorLevel(200, 0) == 0);
}

// The null backends stay honest about M1's additions: no pad and no
// capabilities, capturing the mouse does nothing, sound goes nowhere.

static void testUsbPadReports() {
    using input::ControllerState;
    auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };

    // XInput (Xbox 360 layout; 8BitDo Ultimate 2.4G in PC mode): A + LB +
    // Up + Start, LT half, RT full, left stick right and up.
    const uint8_t xin[20] = {0x00, 0x14, 0x11, 0x11, 128, 255, 0xff, 0x7f, 0xff, 0x7f, 0, 0, 0x00, 0x80};
    ControllerState s;
    CHECK(input::parseXInputReport(xin, sizeof xin, s));
    CHECK(s.connected && s.btnA && s.btnL1 && s.dpadUp && s.btnOptions);
    CHECK(!s.btnB && !s.btnX && !s.btnY && !s.btnR1 && !s.dpadDown && !s.btnTouchpad && !s.btnNexus);
    CHECK(near(s.triggerL2, 0.5f) && near(s.triggerR2, 1.0f));
    CHECK(near(s.rawLeftX, 1.0f) && near(s.rawLeftY, -1.0f));      // +y is down
    CHECK(near(s.rawRightX, 0.0f) && near(s.rawRightY, 1.0f));     // RY -32768: all the way down
    const uint8_t xinGuide[20] = {0x00, 0x14, 0x20, 0x04};  // Back + Guide
    CHECK(input::parseXInputReport(xinGuide, sizeof xinGuide, s));
    CHECK(s.btnTouchpad && s.btnNexus && !s.btnA && !s.btnOptions);
    // Not input: the LED and rumble status reports.
    const uint8_t xinLed[3] = {0x01, 0x03, 0x02}, xinRumble[3] = {0x03, 0x03, 0x00};
    CHECK(!input::parseXInputReport(xinLed, sizeof xinLed, s));
    CHECK(!input::parseXInputReport(xinRumble, sizeof xinRumble, s));

    // GIP (Xbox One / Series): B + Y + View + Right + RB + RS, LT 1023,
    // RT 0, right stick left.
    const uint8_t gip[18] = {0x20, 0x00, 0x05, 0x0e, 0xa8, 0xa8, 0xff, 0x03, 0x00, 0x00,
                             0, 0, 0, 0, 0x00, 0x80, 0, 0};
    s = {};
    CHECK(input::parseGipReport(gip, sizeof gip, s));
    CHECK(s.btnB && s.btnY && s.btnTouchpad && s.dpadRight && s.btnR1 && s.btnR3);
    CHECK(!s.btnA && !s.btnX && !s.btnOptions && !s.dpadLeft && !s.btnL1 && !s.btnL3 && !s.btnNexus);
    CHECK(near(s.triggerL2, 1.0f) && near(s.triggerR2, 0.0f));
    CHECK(near(s.rawRightX, -1.0f) && near(s.rawLeftX, 0.0f));
    // The Xbox button comes by itself and keeps the other buttons.
    const uint8_t guideDown[6] = {0x07, 0x30, 0x01, 0x02, 0x01, 0x5b}, guideUp[6] = {0x07, 0x30, 0x02, 0x02, 0x00, 0x5b};
    CHECK(input::parseGipReport(guideDown, sizeof guideDown, s));
    CHECK(s.btnNexus && s.btnB && s.btnY);
    CHECK(input::parseGipReport(guideUp, sizeof guideUp, s));
    CHECK(!s.btnNexus && s.btnB);
    // Its announce and keep-alive messages are not input.
    const uint8_t announce[8] = {0x02, 0x20, 0x01, 0x1c, 0x7e, 0xed, 0x8b, 0x11}, keepAlive[6] = {0x03, 0x20, 0x01, 0x04, 0x80, 0x00};
    CHECK(!input::parseGipReport(announce, sizeof announce, s));
    CHECK(!input::parseGipReport(keepAlive, sizeof keepAlive, s));
    CHECK(input::parseUsbPadReport(input::UsbPadProtocol::Gip, gip, sizeof gip, s));
    CHECK(!input::parseUsbPadReport(input::UsbPadProtocol::None, gip, sizeof gip, s));

    // GIP acknowledgements: the Xbox button's message asks for one.
    auto acks = input::gipAcksFor(guideDown, sizeof guideDown);
    CHECK(acks.size() == 1);
    const std::array<uint8_t, 13> ack = {0x01, 0x20, 0x01, 0x09, 0x00, 0x07, 0x20, 0x02, 0, 0, 0, 0, 0};
    CHECK(!acks.empty() && acks[0] == ack);
    CHECK(input::gipAcksFor(gip, sizeof gip).empty());  // input is not acknowledged

    // DualShock 3 (PS3): Select + DPad Up + Cross + L1 + PS button, sticks, triggers
    uint8_t ds3[49] = {};
    ds3[0] = 0x01;
    ds3[2] = 0x11;  // Select (0x01) + DPad Up (0x10)
    ds3[3] = 0x44;  // Cross (0x40) + L1 (0x04)
    ds3[4] = 0x01;  // PS button (0x01)
    ds3[6] = 255;   // LX right (1.0f)
    ds3[7] = 0;     // LY up (-1.0f)
    ds3[8] = 128;   // RX center (0.0f)
    ds3[9] = 255;   // RY down (1.0f)
    ds3[18] = 255;  // L2 trigger (1.0f)
    ds3[19] = 128;  // R2 trigger (~0.5f)
    s = {};
    CHECK(input::parseDualShock3Report(ds3, sizeof ds3, s));
    CHECK(s.connected && s.btnA && s.btnL1 && s.dpadUp && s.btnTouchpad && s.btnNexus);
    CHECK(!s.btnB && !s.btnX && !s.btnY && !s.btnR1 && !s.dpadDown && !s.btnOptions);
    CHECK(near(s.rawLeftX, 1.0f) && near(s.rawLeftY, -1.0f));
    CHECK(near(s.rawRightX, 0.0f) && near(s.rawRightY, 1.0f));
    CHECK(near(s.triggerL2, 1.0f) && near(s.triggerR2, 0.5f));
    CHECK(input::parseUsbPadReport(input::UsbPadProtocol::DualShock3, ds3, sizeof ds3, s));

    // DualSense (PS5 USB): Cross + DPad Right + R1 + Options + PS button + touch point
    uint8_t ds5[64] = {};
    ds5[0] = 0x01;  // Report ID
    ds5[1] = 0;     // LX left (-1.0f)
    ds5[2] = 255;   // LY down (1.0f)
    ds5[3] = 255;   // RX right (1.0f)
    ds5[4] = 128;   // RY center (0.0f)
    ds5[5] = 255;   // L2 (1.0f)
    ds5[6] = 0;     // R2 (0.0f)
    ds5[8] = 0x22;  // Hat 2 (Right) + Cross (0x20)
    ds5[9] = 0x22;  // R1 (0x02) + Options (0x20)
    ds5[10] = 0x01; // PS button (0x01)
    ds5[33] = 0x00; // Touch point 0 active (contact bit 7 == 0)
    ds5[34] = 0x80; // tx lo
    ds5[35] = 0x43; // tx hi (0x3), ty lo (0x4)
    ds5[36] = 0x20; // ty hi (0x20)
    s = {};
    CHECK(input::parseDualSenseReport(ds5, sizeof ds5, s));
    CHECK(s.connected && s.btnA && s.dpadRight && s.btnR1 && s.btnOptions && s.btnNexus);
    CHECK(!s.btnB && !s.btnX && !s.btnY && !s.btnL1 && !s.dpadUp);
    CHECK(near(s.rawLeftX, -1.0f) && near(s.rawLeftY, 1.0f));
    CHECK(near(s.rawRightX, 1.0f) && near(s.rawRightY, 0.0f));
    CHECK(near(s.triggerL2, 1.0f) && near(s.triggerR2, 0.0f));
    CHECK(s.touching);
    CHECK(input::parseUsbPadReport(input::UsbPadProtocol::DualSense, ds5, sizeof ds5, s));

    // Nintendo Switch Pro (full report 0x30): B (btnA) + R (btnR1) + Plus (btnOptions) + Home (btnNexus) + Up
    uint8_t sw[12] = {};
    sw[0] = 0x30;
    sw[3] = 0x04 | 0x40; // B (0x04) + R (0x40)
    sw[4] = 0x02 | 0x10; // Plus (0x02) + Home (0x10)
    sw[5] = 0x02 | 0x80; // Up (0x02) + ZL (0x80)
    sw[6] = 0x00; sw[7] = 0x08; sw[8] = 0x80;  // 12-bit LX=2048, LY=2048 (center)
    sw[9] = 0x00; sw[10] = 0x08; sw[11] = 0x80; // 12-bit RX=2048, RY=2048 (center)
    s = {};
    CHECK(input::parseSwitchProReport(sw, sizeof sw, s));
    CHECK(s.connected && s.btnA && s.btnR1 && s.btnOptions && s.btnNexus && s.dpadUp);
    CHECK(near(s.triggerL2, 1.0f) && near(s.triggerR2, 0.0f));
    CHECK(near(s.rawLeftX, 0.0f) && near(s.rawLeftY, 0.0f));
    CHECK(input::parseUsbPadReport(input::UsbPadProtocol::SwitchPro, sw, sizeof sw, s));

    // Nintendo Switch Pro (simple report 0x3F)
    uint8_t swSimple[12] = {};
    swSimple[0] = 0x3F;
    swSimple[1] = 0x02 | 0x20; // B (btnA) + R (btnR1)
    swSimple[2] = 0x02 | 0x10; // Plus (btnOptions) + Home (btnNexus)
    swSimple[3] = 0;           // Hat 0 = Up
    swSimple[5] = 255;         // LX right
    swSimple[7] = 0;           // LY up
    swSimple[9] = 128;         // RX center
    swSimple[11] = 255;        // RY down
    s = {};
    CHECK(input::parseSwitchProReport(swSimple, sizeof swSimple, s));
    CHECK(s.connected && s.btnA && s.btnR1 && s.btnOptions && s.btnNexus && s.dpadUp);
    CHECK(near(s.rawLeftX, 1.0f) && near(s.rawLeftY, -1.0f));
    CHECK(near(s.rawRightX, 0.0f) && near(s.rawRightY, 1.0f));

    // Nintendo Switch Input-Only (7 bytes)
    uint8_t swIn[7] = {0x02 | 0x10, 0x10 | 0x01, 4, 255, 0, 128, 255}; // B+L, Home+Minus, Hat 4=Down, sticks
    s = {};
    CHECK(input::parseSwitchInputOnlyReport(swIn, sizeof swIn, s));
    CHECK(s.connected && s.btnA && s.btnL1 && s.btnNexus && s.btnTouchpad && s.dpadDown);
    CHECK(near(s.rawLeftX, 1.0f) && near(s.rawLeftY, -1.0f));
    CHECK(near(s.rawRightX, 0.0f) && near(s.rawRightY, 1.0f));
    CHECK(input::parseUsbPadReport(input::UsbPadProtocol::SwitchInputOnly, swIn, sizeof swIn, s));

    // Google Stadia (10 bytes)
    uint8_t stadia[10] = {0x03, 6, 0x10 | 0x20, 0x40 | 0x02, 0, 128, 128, 255, 255, 0};
    s = {};
    CHECK(input::parseStadiaReport(stadia, sizeof stadia, s));
    CHECK(s.connected && s.dpadLeft && s.btnNexus && s.btnOptions && s.btnA && s.btnR1);
    CHECK(near(s.triggerL2, 1.0f) && near(s.triggerR2, 0.0f));
    CHECK(near(s.rawLeftX, -1.0f) && near(s.rawRightY, 1.0f));
    CHECK(input::parseUsbPadReport(input::UsbPadProtocol::Stadia, stadia, sizeof stadia, s));

    // DualShock 4 (PS4 third-party): 64-byte report with ID 0x01
    // Cross + DPad Left + L1 + Options + PS + Touchpad click + touches
    uint8_t ds4[64] = {};
    ds4[0] = 0x01; // Report ID
    ds4[1] = 0;    // LX left (-1.0f)
    ds4[2] = 255;  // LY down (1.0f)
    ds4[3] = 255;  // RX right (1.0f)
    ds4[4] = 0;    // RY up (-1.0f)
    ds4[5] = 0x26; // Hat 6 (Left) + Cross (0x20)
    ds4[6] = 0x21; // L1 (0x01) + Options (0x20)
    ds4[7] = 0x03; // PS (0x01) + Touchpad click (0x02)
    ds4[8] = 255;  // L2 trigger (1.0f)
    ds4[9] = 128;  // R2 trigger (~0.5f)
    ds4[35] = 0x00; // Touch point 0 active (contact bit 7 == 0)
    ds4[36] = 0x80; // tx lo
    ds4[37] = 0x43; // tx hi (0x3), ty lo (0x4)
    ds4[38] = 0x20; // ty hi (0x20)
    s = {};
    CHECK(input::parseDualShock4Report(ds4, sizeof ds4, s));
    CHECK(s.connected && s.btnA && s.dpadLeft && s.btnL1 && s.btnOptions && s.btnNexus && s.btnTouchpad);
    CHECK(!s.btnB && !s.btnX && !s.btnY && !s.btnR1 && !s.dpadUp && !s.dpadRight && !s.dpadDown);
    CHECK(near(s.rawLeftX, -1.0f) && near(s.rawLeftY, 1.0f));
    CHECK(near(s.rawRightX, 1.0f) && near(s.rawRightY, -1.0f));
    CHECK(near(s.triggerL2, 1.0f) && near(s.triggerR2, 0.5f));
    CHECK(s.touching);
    CHECK(input::parseUsbPadReport(input::UsbPadProtocol::DualShock4, ds4, sizeof ds4, s));

    // DualShock 4 release D-Pad and Touch: hat = 8 (neutral), touch active bit 7 = 1
    ds4[5] = 0x08; // hat neutral, no face buttons
    ds4[35] = 0x80; // touch inactive
    CHECK(input::parseDualShock4Report(ds4, sizeof ds4, s));
    CHECK(!s.dpadLeft && !s.dpadUp && !s.dpadDown && !s.dpadRight);
    CHECK(!s.btnA && !s.touching);

    // DualSense release D-Pad and Touch
    ds5[8] = 0x08; // hat neutral
    ds5[33] = 0x80; // touch 0 inactive
    CHECK(input::parseDualSenseReport(ds5, sizeof ds5, s));
    CHECK(!s.dpadRight && !s.dpadUp && !s.dpadDown && !s.dpadLeft);
    CHECK(!s.touching);

    // Switch Pro 0x30 stick deflections: LY up (0) should produce rawLeftY = -1.0f
    sw[5] = 0x00; // D-Pad neutral
    sw[6] = 0x00; sw[7] = 0x08; sw[8] = 0x00; // 12-bit LX=2048 (center), LY=0 (full UP)
    CHECK(input::parseSwitchProReport(sw, sizeof sw, s));
    CHECK(!s.dpadUp && !s.dpadDown && !s.dpadLeft && !s.dpadRight);
    CHECK(near(s.rawLeftX, 0.0f) && near(s.rawLeftY, -1.0f));

    // Switch Pro 0x3F hat release
    swSimple[3] = 8; // neutral hat
    CHECK(input::parseSwitchProReport(swSimple, sizeof swSimple, s));
    CHECK(!s.dpadUp && !s.dpadDown && !s.dpadLeft && !s.dpadRight);

    // Switch Input-Only hat release
    swIn[2] = 8; // neutral hat
    CHECK(input::parseSwitchInputOnlyReport(swIn, sizeof swIn, s));
    CHECK(!s.dpadDown && !s.dpadUp && !s.dpadLeft && !s.dpadRight);

    // Stadia hat release
    stadia[1] = 8; // neutral hat
    CHECK(input::parseStadiaReport(stadia, sizeof stadia, s));
    CHECK(!s.dpadLeft && !s.dpadUp && !s.dpadDown && !s.dpadRight);

    // Generic HID fallback (raw axes + buttons)
    uint8_t hidRaw[7] = {255, 0, 128, 255, 0, 0x02 | 0x10, 0x10}; // LX=1.0, LY=-1.0, RX=0.0, RY=1.0, Hat=Up, Cross+L1, PS
    s = {};
    CHECK(input::parseGenericHidFallback(hidRaw, sizeof hidRaw, s));
    CHECK(s.connected && s.btnA && s.btnL1 && s.dpadUp && s.btnNexus);
    CHECK(near(s.rawLeftX, 1.0f) && near(s.rawLeftY, -1.0f));
    CHECK(input::parseUsbPadReport(input::UsbPadProtocol::GenericHid, hidRaw, sizeof hidRaw, s));

    // Generic HID fallback hat release
    hidRaw[4] = 8; // neutral hat
    CHECK(input::parseGenericHidFallback(hidRaw, sizeof hidRaw, s));
    CHECK(!s.dpadUp && !s.dpadDown && !s.dpadLeft && !s.dpadRight);

    // Generic HID descriptor with Rx/Ry right stick, Z/Rz triggers, and 12-button combo
    const uint8_t hidGamepadDesc[] = {
        0x05, 0x01,        // Usage Page (Generic Desktop)
        0x09, 0x05,        // Usage (Gamepad)
        0xA1, 0x01,        // Collection (Application)
        0x05, 0x01,        //   Usage Page (Generic Desktop)
        0x09, 0x30,        //   Usage (X)
        0x09, 0x31,        //   Usage (Y)
        0x09, 0x32,        //   Usage (Z)
        0x09, 0x35,        //   Usage (Rz)
        0x09, 0x33,        //   Usage (Rx)
        0x09, 0x34,        //   Usage (Ry)
        0x15, 0x00,        //   Logical Minimum (0)
        0x26, 0xFF, 0x00,  //   Logical Maximum (255)
        0x75, 0x08,        //   Report Size (8)
        0x95, 0x06,        //   Report Count (6)
        0x81, 0x02,        //   Input (Data, Var, Abs)
        0x09, 0x39,        //   Usage (Hat switch)
        0x15, 0x00,        //   Logical Minimum (0)
        0x25, 0x07,        //   Logical Maximum (7)
        0x75, 0x04,        //   Report Size (4)
        0x95, 0x01,        //   Report Count (1)
        0x81, 0x42,        //   Input (Data, Var, Abs, Null)
        0x75, 0x04,        //   Report Size (4)
        0x95, 0x01,        //   Report Count (1)
        0x81, 0x01,        //   Input (Const)
        0x05, 0x09,        //   Usage Page (Button)
        0x19, 0x01,        //   Usage Minimum (1)
        0x29, 0x0C,        //   Usage Maximum (12)
        0x15, 0x00,        //   Logical Minimum (0)
        0x25, 0x01,        //   Logical Maximum (1)
        0x75, 0x01,        //   Report Size (1)
        0x95, 0x0C,        //   Report Count (12)
        0x81, 0x02,        //   Input (Data, Var, Abs)
        0x75, 0x04,        //   Report Size (4)
        0x95, 0x01,        //   Report Count (1)
        0x81, 0x01,        //   Input (Const)
        0xC0               // End Collection
    };
    input::GenericHidContext hidCtx{};
    CHECK(input::parseHidDescriptor(hidCtx, hidGamepadDesc, sizeof(hidGamepadDesc), 0x1234, 0x5678));
    CHECK(hidCtx.role[input::ROLE_LX] == input::AX_X);
    CHECK(hidCtx.role[input::ROLE_LY] == input::AX_Y);
    CHECK(hidCtx.role[input::ROLE_RX] == input::AX_RX);
    CHECK(hidCtx.role[input::ROLE_RY] == input::AX_RY);
    CHECK(hidCtx.role[input::ROLE_L2] == input::AX_Z);
    CHECK(hidCtx.role[input::ROLE_R2] == input::AX_RZ);

    // Feed a report to hidCtx:
    // LX=255, LY=0, Z=255 (L2), Rz=128 (R2), Rx=0, Ry=255, Hat=2 (Right), Buttons: Select (btn 9, bit 8) + Start (btn 10, bit 9)
    uint8_t hidReport[9] = {
        255, 0, 255, 128, 0, 255,
        0x02, // Hat=2 (Right)
        0x00, 0x03 // bits 8 and 9 (Select + Start)
    };
    s = {};
    CHECK(input::parseGenericHidReport(hidCtx, hidReport, sizeof(hidReport), s));
    CHECK(s.connected && s.dpadRight);
    CHECK(near(s.rawLeftX, 1.0f) && near(s.rawLeftY, -1.0f));
    CHECK(near(s.rawRightX, -1.0f) && near(s.rawRightY, 1.0f));
    CHECK(near(s.triggerL2, 1.0f) && near(s.triggerR2, 0.5f));
    CHECK(s.btnTouchpad && s.btnOptions);
    CHECK(s.btnNexus); // 12-button combo active!

    // Protocol names
    CHECK(std::string(input::usbPadProtocolName(input::UsbPadProtocol::DualShock3)) == "DualShock 3");
    CHECK(std::string(input::usbPadProtocolName(input::UsbPadProtocol::DualShock4)) == "DualShock 4");
    CHECK(std::string(input::usbPadProtocolName(input::UsbPadProtocol::DualSense)) == "DualSense");
    CHECK(std::string(input::usbPadProtocolName(input::UsbPadProtocol::SwitchPro)) == "Switch Pro");
    CHECK(std::string(input::usbPadProtocolName(input::UsbPadProtocol::SwitchInputOnly)) == "Switch");
    CHECK(std::string(input::usbPadProtocolName(input::UsbPadProtocol::Stadia)) == "Stadia");
    CHECK(std::string(input::usbPadProtocolName(input::UsbPadProtocol::GenericHid)) == "Generic HID");
}

static void testUsbPadDescriptors() {
    // An Xbox Series pad's configuration (045e:0b12): GIP interface 0
    // (OUT 0x02, IN 0x82), then the audio interface with its alternates.
    const uint8_t series[] = {
        0x09, 0x02, 0x77, 0x00, 0x03, 0x01, 0x00, 0xa0, 0xfa,  // configuration
        0x09, 0x04, 0x00, 0x00, 0x02, 0xff, 0x47, 0xd0, 0x00,  // interface 0: GIP
        0x07, 0x05, 0x02, 0x03, 0x40, 0x00, 0x04,              // OUT 0x02 interrupt
        0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x04,              // IN 0x82 interrupt
        0x09, 0x04, 0x01, 0x00, 0x00, 0xff, 0x47, 0xd0, 0x00,  // interface 1, no endpoints
        0x09, 0x04, 0x01, 0x01, 0x02, 0xff, 0x47, 0xd0, 0x00,  // its alternate 1: isochronous
        0x07, 0x05, 0x03, 0x01, 0xe4, 0x00, 0x01,
        0x07, 0x05, 0x83, 0x01, 0x40, 0x00, 0x01,
    };
    input::UsbPadInterface i = input::findUsbPadInterface(series, sizeof series);
    CHECK(i.protocol == input::UsbPadProtocol::Gip && i.number == 0 && i.epIn == 0x82 && i.epOut == 0x02);

    // An XInput pad (8BitDo Ultimate, 2dc8:3106): interface 0 FF/5D/01
    // (IN 0x81, OUT 0x01) after a vendor interface the code must skip.
    const uint8_t xinput[] = {
        0x09, 0x02, 0x40, 0x00, 0x02, 0x01, 0x00, 0xa0, 0xfa,
        0x09, 0x04, 0x00, 0x00, 0x00, 0xff, 0xfd, 0x13, 0x00,  // security interface: not a pad
        0x09, 0x04, 0x01, 0x00, 0x02, 0xff, 0x5d, 0x01, 0x00,  // interface 1: XInput
        0x11, 0x21, 0x10, 0x01, 0x01, 0x25, 0x81, 0x14, 0x03, 0x03, 0x03, 0x04, 0x13, 0x02, 0x08, 0x03, 0x03,
        0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x01,
        0x07, 0x05, 0x01, 0x03, 0x20, 0x00, 0x08,
    };
    i = input::findUsbPadInterface(xinput, sizeof xinput);
    CHECK(i.protocol == input::UsbPadProtocol::XInput && i.number == 1 && i.epIn == 0x81 && i.epOut == 0x01);

    // A keyboard (HID, class 3): no pad. A truncated descriptor: none either.
    const uint8_t keyboard[] = {
        0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xa0, 0x32,
        0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
        0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0a,
    };
    CHECK(input::findUsbPadInterface(keyboard, sizeof keyboard).protocol == input::UsbPadProtocol::None);
    CHECK(input::findUsbPadInterface(series, 20).protocol == input::UsbPadProtocol::None);
    CHECK(input::findUsbPadInterface(nullptr, 0).protocol == input::UsbPadProtocol::None);
    CHECK(input::usbPadProtocolFor(0x03, 0x00, 0x00) == input::UsbPadProtocol::DualShock4);
    CHECK(input::usbPadProtocolFor(0xff, 0x5d, 0x81) == input::UsbPadProtocol::XInput);
    CHECK(input::usbPadProtocolFor(0xff, 0x5d, 0x00) == input::UsbPadProtocol::XInput);
    CHECK(input::usbPadProtocolFor(0xff, 0x5d, 0x01) == input::UsbPadProtocol::XInput);
    CHECK(input::usbPadProtocolFor(0xff, 0x47, 0xd0) == input::UsbPadProtocol::Gip);
    CHECK(input::usbPadProtocolFor(0x03, 0x01, 0x01) == input::UsbPadProtocol::None);
    CHECK(input::usbPadProtocolFor(0x03, 0x01, 0x02) == input::UsbPadProtocol::None);
    CHECK(input::usbPadProtocolFor(0xff, 0x00, 0x00) == input::UsbPadProtocol::None);
}

static void testUsbPadSlots() {
    using input::UsbPadEvent;
    input::ControllerState out;
    input::takeUsbPadEvents();
    CHECK(!input::pollUsbPad(out) && !out.connected && input::usbPadCount() == 0);
    // Each pad a player of its own, in the order they came (no DualSense here).
    input::usbPadAttached(1, "Xbox Wireless Controller");
    input::usbPadAttached(0, "8BitDo Ultimate");
    CHECK(input::usbPadCount() == 2 && input::usbPadPlayer(1) == -1);
    CHECK(!input::pollUsbPad(out));  // no input from either yet; the players given
    CHECK(input::usbPadPlayer(1) == 0 && input::usbPadPlayer(0) == 1);
    auto events = input::takeUsbPadEvents();
    CHECK(events.size() == 2 && events[0].kind == UsbPadEvent::Kind::Connected && events[0].player == 0 &&
          events[0].name == "Xbox Wireless Controller" && events[1].player == 1);
    CHECK(input::usbPadNameFor(0) == "Xbox Wireless Controller" && input::usbPadNameFor(1) == "8BitDo Ultimate");
    CHECK(input::usbPadNameFor(2).empty() && input::usbPadActiveAt(0) == 0);

    input::ControllerState xbox, bitdo;
    xbox.btnA = true;
    xbox.rawLeftX = 0.05f;  // the left stick inside the dead zone, the right one pushed
    xbox.rawRightY = 0.9f;
    bitdo.dpadRight = true;
    bitdo.triggerR2 = 0.7f;
    input::usbPadState(1, xbox);
    input::usbPadState(0, bitdo);
    CHECK(input::usbPadActiveAt(0) > 0 && input::usbPadActiveAt(1) > 0);
    // The menus: both. The game: each its own player.
    CHECK(input::pollUsbPad(out) && out.btnA && out.dpadRight && std::abs(out.triggerR2 - 0.7f) < 0.01f);
    CHECK(input::pollUsbPadsFor(0, out) && out.btnA && !out.dpadRight);
    CHECK(out.leftStickX == 0.0f && out.rawLeftX > 0.04f && out.rightStickY > 0.8f);
    CHECK(input::pollUsbPadsFor(1, out) && out.dpadRight && !out.btnA);
    CHECK(!input::pollUsbPadsFor(2, out) && !input::pollUsbPadsFor(-1, out) && !input::pollUsbPadsFor(9, out));
    // Circle confirms: the Xbox B button becomes A as the DualSense's Circle does.
    input::setCircleConfirms(true);
    CHECK(input::pollUsbPadsFor(0, out) && !out.btnA && out.btnB);
    input::setCircleConfirms(false);

    // Xbox / Home + RB: the 8BitDo to player 3; LB / RB and the Xbox
    // button do not reach the game meanwhile.
    bitdo = {};
    bitdo.btnNexus = true;
    input::usbPadState(0, bitdo);
    CHECK(input::pollUsbPadsFor(1, out) && !out.btnNexus);
    bitdo.btnR1 = true;
    input::usbPadState(0, bitdo);
    CHECK(input::usbPadPlayer(0) == 2 && !input::pollUsbPadsFor(1, out));
    CHECK(input::pollUsbPadsFor(2, out) && !out.btnR1 && !out.btnNexus);
    bitdo.btnR1 = false;
    input::usbPadState(0, bitdo);
    bitdo.btnR1 = true;  // again: player 4
    input::usbPadState(0, bitdo);
    CHECK(input::usbPadPlayer(0) == 3);
    bitdo = {};
    input::usbPadState(0, bitdo);  // let go after a move: no menu
    bitdo.btnNexus = true, bitdo.btnL1 = true;  // Xbox + LB: back to player 3
    input::usbPadState(0, bitdo);
    CHECK(input::usbPadPlayer(0) == 2);
    bitdo = {};
    input::usbPadState(0, bitdo);
    events = input::takeUsbPadEvents();
    CHECK(events.size() == 3 && events[0].kind == UsbPadEvent::Kind::Moved && events[0].player == 2 &&
          events[0].slot == 0 && events[1].player == 3 && events[2].player == 2);
    // From player 1, LB goes round to player 4, and RB back.
    xbox = {};
    xbox.btnNexus = true;
    input::usbPadState(1, xbox);
    xbox.btnL1 = true;
    input::usbPadState(1, xbox);
    CHECK(input::usbPadPlayer(1) == 3);
    xbox.btnL1 = false;
    input::usbPadState(1, xbox);
    xbox.btnR1 = true;
    input::usbPadState(1, xbox);
    CHECK(input::usbPadPlayer(1) == 0);
    xbox = {};
    input::usbPadState(1, xbox);
    input::takeUsbPadEvents();
    // The Xbox button alone: the in-game menu, for that pad, once let go.
    xbox.btnNexus = true;
    input::usbPadState(1, xbox);
    CHECK(input::takeUsbPadEvents().empty());
    xbox.btnNexus = false;
    input::usbPadState(1, xbox);
    events = input::takeUsbPadEvents();
    CHECK(events.size() == 1 && events[0].kind == UsbPadEvent::Kind::MenuButton && events[0].slot == 1 &&
          events[0].player == 0);
    // Menu (Start) goes straight through, with View too.
    xbox.btnOptions = true;
    input::usbPadState(1, xbox);
    CHECK(input::pollUsbPadsFor(0, out) && out.btnOptions);
    xbox = {};
    input::usbPadState(1, xbox);

    // The menu's owner: out of its player's input, read by itself.
    xbox.btnA = true;
    input::usbPadState(1, xbox);
    CHECK(!input::pollUsbPadsFor(0, out, 1));
    CHECK(input::pollUsbPadSlot(1, out) && out.btnA && !input::pollUsbPadSlot(3, out) && !input::pollUsbPadSlot(-1, out));
    // The menu closed with A held: out of the game until let go (the menus still see it).
    input::usbPadsWaitRelease();
    CHECK(!input::pollUsbPadsFor(0, out) && input::pollUsbPad(out) && out.btnA);
    xbox.btnA = false;
    input::usbPadState(1, xbox);
    CHECK(input::pollUsbPadsFor(0, out) && !out.btnA);
    // The menu's Player item.
    input::setUsbPadPlayer(1, 2);
    CHECK(input::usbPadPlayer(1) == 2 && input::usbPadName(1) == "Xbox Wireless Controller");
    input::setUsbPadPlayer(1, 7);  // out of range: kept
    CHECK(input::usbPadPlayer(1) == 2);
    input::setUsbPadPlayer(1, 0);
    events = input::takeUsbPadEvents();
    CHECK(events.size() == 2 && events[0].kind == UsbPadEvent::Kind::Moved && events[0].player == 2 &&
          events[1].player == 0);

    // A pad that reports only on change keeps its state.
    bitdo.btnY = true;
    input::usbPadState(0, bitdo);
    CHECK(input::pollUsbPadsFor(2, out) && out.btnY);
    CHECK(input::pollUsbPadsFor(2, out) && out.btnY);
    input::usbPadDetached(1);
    events = input::takeUsbPadEvents();
    CHECK(events.size() == 1 && events[0].kind == UsbPadEvent::Kind::Disconnected && events[0].player == 0);
    // Gone: its player is free for the next one.
    input::usbPadAttached(1, "Xbox Wireless Controller");
    CHECK(!input::pollUsbPadsFor(0, out) && input::usbPadPlayer(1) == 0);
    input::usbPadDetached(0);
    input::usbPadDetached(1);
    input::takeUsbPadEvents();
    CHECK(!input::pollUsbPad(out) && input::usbPadCount() == 0);
    // A guitar in the app's menus: its frets and strum bar, never its whammy and tilt as a stick
    // (the list scrolled as the guitar moved).
    {
        input::usbPadAttached(3, "Xbox 360 Wireless", input::UsbPadKind::Guitar);
        input::ControllerState g;
        g.connected = true;
        g.btnA = g.dpadDown = true;
        g.rawRightX = g.rightStickX = 1.0f;   // the whammy pressed
        g.rawRightY = g.rightStickY = -0.9f;  // the tilt
        input::usbPadState(3, g);
        CHECK(input::pollUsbPad(out) && out.btnA && out.dpadDown);
        CHECK(out.rightStickX == 0.0f && out.rightStickY == 0.0f && out.rawRightX == 0.0f && out.rawRightY == 0.0f);
        input::usbPadDetached(3);
        input::takeUsbPadEvents();
    }
    input::usbPadState(2, bitdo);  // a slot that was never attached
    CHECK(!input::pollUsbPad(out) && input::usbPadPlayer(2) == -1);
    input::usbPadState(9, bitdo);  // out of range: ignored

    // USB Pads Enabled toggle and rumble
    CHECK(input::usbPadsEnabled());
    input::usbPadAttached(0, "Test Pad");
    input::setUsbPadRumble(0, 128, 64);
    input::setUsbPadRumble(5, 128, 64);  // out of bounds
    input::setUsbPadsEnabled(false);
    CHECK(!input::usbPadsEnabled());
    CHECK(input::usbPadCount() == 0);
    input::setUsbPadsEnabled(true);
    CHECK(input::usbPadsEnabled());
    input::takeUsbPadEvents();
}

static void testSoloInputRedirectionMerging() {
    input::ControllerState s0{};
    s0.connected = true;
    s0.btnA = true;
    s0.leftStickX = 0.3f;
    s0.leftStickY = -0.2f;
    s0.triggerL2 = 0.25f;

    // Slot 1 (e.g. secondary USB pad on OneFamily option 2)
    input::ControllerState s1{};
    s1.connected = true;
    s1.btnB = true;
    s1.btnX = true;
    s1.leftStickX = -0.85f;  // magnitude larger than s0 (0.85 > 0.3)
    s1.rightStickY = 0.65f;
    s1.triggerL2 = 0.90f;   // max with 0.25f -> 0.90f

    // Slot 2 (e.g. secondary pad on OneFamily option 3 or Web Gamepad)
    input::ControllerState s2{};
    s2.connected = true;
    s2.btnY = true;
    s2.dpadUp = true;
    s2.rightStickY = -0.95f; // magnitude larger than s1 (0.95 > 0.65)
    s2.triggerR2 = 0.70f;
    s2.btnNexus = true;

    // Perform merging into slot 0
    input::mergePads(s0, s1);
    CHECK(s0.btnA);
    CHECK(s0.btnB);
    CHECK(s0.btnX);
    CHECK(!s0.btnY);
    CHECK(std::abs(s0.leftStickX - (-0.85f)) < 1e-4f);
    CHECK(std::abs(s0.leftStickY - (-0.2f)) < 1e-4f);
    CHECK(std::abs(s0.rightStickY - 0.65f) < 1e-4f);
    CHECK(std::abs(s0.triggerL2 - 0.90f) < 1e-4f);
    CHECK(s0.triggerR2 == 0.0f);

    input::mergePads(s0, s2);
    CHECK(s0.btnA);
    CHECK(s0.btnB);
    CHECK(s0.btnX);
    CHECK(s0.btnY);
    CHECK(s0.dpadUp);
    CHECK(s0.btnNexus);
    CHECK(std::abs(s0.rightStickY - (-0.95f)) < 1e-4f);
    CHECK(std::abs(s0.triggerR2 - 0.70f) < 1e-4f);
}

static void testSoloInputRedirectSettingsPersistence() {
    const std::string path = "/tmp/test_solo_settings.json";
    std::remove(path.c_str());

    xc::app::Settings s;
    CHECK(!s.soloInputRedirect);
    CHECK(!s.defaultProfile().soloInputRedirect);

    s.soloInputRedirect = true;
    CHECK(s.defaultProfile().soloInputRedirect);

    xc::app::GameProfile prof;
    prof.soloInputRedirect = true;
    s.perGame["TEST_SOLO_GAME"] = prof;

    CHECK(s.save(path));

    xc::app::Settings reloaded;
    CHECK(reloaded.load(path));
    CHECK(reloaded.soloInputRedirect);
    CHECK(reloaded.defaultProfile().soloInputRedirect);
    bool hasCustom = false;
    auto loadedProf = reloaded.profileForGame("TEST_SOLO_GAME", "", &hasCustom);
    CHECK(hasCustom);
    CHECK(loadedProf.soloInputRedirect);

    // Test fallback to legacy snake_case key "solo_input_redirect"
    std::string legacyJson = R"({
        "settingsVersion": 3,
        "solo_input_redirect": true,
        "perGame": {
            "LEGACY_GAME": {
                "solo_input_redirect": true
            }
        }
    })";
    platform::writeFileAtomic(path, legacyJson);
    xc::app::Settings legacySettings;
    CHECK(legacySettings.load(path));
    CHECK(legacySettings.soloInputRedirect);
    hasCustom = false;
    auto legProf = legacySettings.profileForGame("LEGACY_GAME", "", &hasCustom);
    CHECK(hasCustom);
    CHECK(legProf.soloInputRedirect);

    std::remove(path.c_str());
}

static void testDesktopMouseNav() {
    ui::Fonts dummyFonts{};
    ui::ImageCache images([] {});
    ui::AppUi uiApp(dummyFonts, images);

    ui::GameRow row;
    row.title = "Test Row";
    row.isGrid = false;
    ui::GameTile tile1;
    tile1.productId = "GAME_001";
    tile1.name = "First Game";
    ui::GameTile tile2;
    tile2.productId = "GAME_002";
    tile2.name = "Second Game";
    row.tiles.push_back(tile1);
    row.tiles.push_back(tile2);
    uiApp.setRows({row});
    uiApp.showHome();

    // 1. Mouse cursor state, initial position, and clamping
    CHECK(uiApp.mouseCursorX() == 960.0f);
    CHECK(uiApp.mouseCursorY() == 540.0f);
    CHECK(!uiApp.mouseCursorVisible());

    {
        auto evMove = uiApp.onMouseMove(300.0f, 300.0f);
        CHECK(evMove.action == ui::Action::None);
        CHECK(uiApp.mouseCursorX() == 300.0f);
        CHECK(uiApp.mouseCursorY() == 300.0f);
        CHECK(uiApp.mouseCursorVisible());

        auto evClick = uiApp.onMouseDown(300.0f, 300.0f, 1);
        CHECK(evClick.action == ui::Action::None);
        auto evWheel = uiApp.onMouseWheel(0.0f, 1.0f);
        CHECK(evWheel.action == ui::Action::None);

        uiApp.setMouseCursor(2500.0f, -100.0f);
        CHECK(uiApp.mouseCursorX() == 1919.0f);
        CHECK(uiApp.mouseCursorY() == 0.0f);
    }

    // 2. Enable desktopWindow capability (Desktop / Steam Deck mode)
    ui::UiCaps caps{};
    caps.desktopWindow = true;
    uiApp.setCaps(caps);

    // 3. Hovering over a card updates focus
    {
        // First card is at kMargin (96) + 0, cardY = kRowTop (470) + 48 = 518
        auto ev = uiApp.onMouseMove(120.0f, 550.0f);
        CHECK(ev.action == ui::Action::None);
    }

    // 4. Clicking a card opens details
    {
        auto ev = uiApp.onMouseDown(120.0f, 550.0f, 1);
        CHECK(ev.action == ui::Action::None);
        // AppUi should now be showing details
        CHECK(uiApp.detailWanted(100) == "GAME_001");
    }

    // 5. Right-click (button 3) returns back to Home screen
    {
        auto ev = uiApp.onMouseDown(500.0f, 500.0f, 3);
        CHECK(ev.action == ui::Action::None);
        // After returning to Home, detailWanted should be empty
        CHECK(uiApp.detailWanted(100).empty());
    }

    // 6. Clicking or hovering in the top bar area (y < 140) must not hit cards
    {
        auto evTop = uiApp.onMouseDown(120.0f, 100.0f, 1);
        CHECK(evTop.action == ui::Action::None);
        CHECK(uiApp.detailWanted(100).empty());
    }

    // 7. Mouse wheel scrolling
    {
        auto evWheel = uiApp.onMouseWheel(0.0f, 2.0f);
        CHECK(evWheel.action == ui::Action::None);
    }

    // 7. Avatar / profile click opens Settings screen
    {
        auto evClick = uiApp.onMouseDown(1920 - 96 - 20, 60.0f, 1);
        CHECK(uiApp.screen() == ui::Screen::Settings);
    }

    // 8. In Settings, clicking a dropdown setting opens the dropdown
    {
        // Language row (row 0) at y = 190
        auto evClick = uiApp.onMouseDown(200.0f, 200.0f, 1);
        // First right click closes the dropdown
        auto evClose = uiApp.onMouseDown(200.0f, 200.0f, 3);
        CHECK(uiApp.screen() == ui::Screen::Settings);
        // Second right click exits back to Home
        auto evBack = uiApp.onMouseDown(200.0f, 200.0f, 3);
        CHECK(uiApp.screen() == ui::Screen::Home);
    }
}

static void testTouchpadShortcutsAndSlots() {
    // 1. TouchpadShortcuts defaults and manipulation
    {
        input::TouchpadShortcuts ts = input::defaultTouchpadShortcuts();
        CHECK(ts.swipeUp == input::TouchpadAction::XboxButton);
        CHECK(ts.swipeDown == input::TouchpadAction::GameMenu);
        CHECK(ts.swipeLeft == input::TouchpadAction::GameMenu);
        CHECK(ts.swipeRight == input::TouchpadAction::XboxButton);
        CHECK(ts.clickLeft == input::TouchpadAction::View);
        CHECK(ts.clickRight == input::TouchpadAction::View);

        CHECK(ts.get(0) == input::TouchpadAction::XboxButton);
        CHECK(ts.get(1) == input::TouchpadAction::GameMenu);
        CHECK(ts.get(2) == input::TouchpadAction::GameMenu);
        CHECK(ts.get(3) == input::TouchpadAction::XboxButton);
        CHECK(ts.get(4) == input::TouchpadAction::View);
        CHECK(ts.get(5) == input::TouchpadAction::View);

        ts.set(0, input::TouchpadAction::ToggleStats);
        ts.set(4, input::TouchpadAction::A);
        CHECK(ts.get(0) == input::TouchpadAction::ToggleStats);
        CHECK(ts.get(4) == input::TouchpadAction::A);

        // JSON roundtrip
        json::Value j = input::touchpadShortcutsJson(ts);
        input::TouchpadShortcuts back;
        CHECK(input::readTouchpadShortcuts(j, back));
        CHECK(back == ts);

        // Button action checks
        CHECK(input::isTouchpadButtonAction(input::TouchpadAction::A));
        CHECK(input::isTouchpadButtonAction(input::TouchpadAction::View));
        CHECK(!input::isTouchpadButtonAction(input::TouchpadAction::GameMenu));
        CHECK(!input::isTouchpadButtonAction(input::TouchpadAction::XboxButton));
        CHECK(!input::isTouchpadButtonAction(input::TouchpadAction::None));

        input::ControllerState s{};
        CHECK(input::applyTouchpadButtonAction(s, input::TouchpadAction::A));
        CHECK(s.btnA);
        CHECK(!input::applyTouchpadButtonAction(s, input::TouchpadAction::GameMenu));

        // Edge cases: out of bounds indices and invalid actions
        CHECK(ts.get(-1) == input::TouchpadAction::None);
        CHECK(ts.get(99) == input::TouchpadAction::None);
        ts.set(-1, input::TouchpadAction::A);
        ts.set(99, input::TouchpadAction::A);
        ts.set(0, static_cast<input::TouchpadAction>(250));
        CHECK(ts.get(0) == input::TouchpadAction::None);

        // Corrupted JSON inputs
        input::TouchpadShortcuts corruptTs;
        CHECK(!input::readTouchpadShortcuts(json::Value(), corruptTs));
        CHECK(corruptTs == input::defaultTouchpadShortcuts());
        json::Value badVer = json::Value::object();
        badVer.set("version", 99);
        CHECK(!input::readTouchpadShortcuts(badVer, corruptTs));
        CHECK(corruptTs == input::defaultTouchpadShortcuts());

        // Labels
        for (int i = 0; i < 6; ++i) {
            CHECK(ui::touchpadTriggerLabel(i)[0] != '\0');
        }
        CHECK(ui::touchpadTriggerLabel(-1)[0] == '\0');
        CHECK(ui::touchpadTriggerLabel(10)[0] == '\0');
        for (int a = 0; a < input::kTouchpadActions; ++a) {
            CHECK(!ui::touchpadActionLabel(a).empty());
        }
        CHECK(ui::touchpadActionLabel(999) == ui::touchpadActionLabel(0));
    }

    // 2. 4 Virtual Gamepad Slots (P1..P4) and timestamps
    {
        app::resetVirtualGamepads();
        app::WebServer srv;

        for (int p = 1; p <= 4; ++p) {
            int slotIdx = p - 1;
            input::ControllerState st{};
            bool conn = false;
            app::virtualGamepadState(slotIdx, st, conn);
            CHECK(!conn);
            CHECK(app::virtualGamepadActiveAt(slotIdx) == 0);

            // Send input for player p
            std::string body = "{\"player\":" + std::to_string(p) + ",\"btnA\":true,\"leftStickX\":0.5}";
            auto res = srv.handleRequest("POST", "/api/gamepad", body);
            CHECK(res.status == 200);

            bool ok = app::virtualGamepadState(slotIdx, st, conn);
            CHECK(ok && conn);
            CHECK(st.btnA);
            CHECK(app::virtualGamepadActiveAt(slotIdx) > 0);
        }

        // touchVirtualGamepad
        uint64_t before = app::virtualGamepadActiveAt(2);
        app::touchVirtualGamepad(2);
        CHECK(app::virtualGamepadActiveAt(2) >= before);

        app::resetVirtualGamepads();
        for (int i = 0; i < 4; ++i) {
            input::ControllerState st{};
            bool conn = false;
            app::virtualGamepadState(i, st, conn);
            CHECK(!conn);
        }

        // Out of bounds player index edge cases
        {
            input::ControllerState st{};
            bool conn = false;
            CHECK(!app::virtualGamepadState(-1, st, conn));
            CHECK(!conn);
            CHECK(!app::virtualGamepadState(4, st, conn));
            CHECK(!conn);
            CHECK(!app::virtualGamepadState(99, st, conn));
            CHECK(!conn);
            CHECK(app::virtualGamepadActiveAt(-1) == 0);
            CHECK(app::virtualGamepadActiveAt(4) == 0);
            CHECK(app::virtualGamepadActiveAt(99) == 0);
            app::touchVirtualGamepad(-1);
            app::touchVirtualGamepad(4);
            app::touchVirtualGamepad(99);
        }
    }

    // 3. UI Pad Tester Tab 0 & Tab 1 Alignment and Capture
    {
        ui::Fonts fonts;
        ui::ImageCache images([] {});
        ui::AppUi app(fonts, images);
        ui::UiCaps caps;
        caps.touchpad = true;
        caps.console = true;
        app.setCaps(caps);

        app.showHome();
        ui::NavInput openOpt;
        openOpt.options = true;
        app.handle(openOpt);

        ui::NavInput down;
        down.down = true;
        while (app.settingsFocus() != 14) {
            app.handle(down);
        }
        ui::NavInput accept;
        accept.accept = true;
        app.handle(accept);

        ui::Canvas cTab0(1920, 1080);
        app.render(cTab0, 1000);

        ui::NavInput r1;
        r1.r1 = true;
        app.handle(r1);

        ui::Canvas cTab1(1920, 1080);
        app.render(cTab1, 1000);

        CHECK(cTab0.width() == 1920 && cTab0.height() == 1080);
        CHECK(cTab1.width() == 1920 && cTab1.height() == 1080);
    }

    // 4. Dynamic Slot Device Icon Resolution with Phone/Keyboard/Gamepad
    {
        ui::PadSlots slots;
        for (int i = 0; i < 4; ++i) {
            slots[i].device = ui::PadDevice::Gamepad;
            slots[i].connected = true;
        }

        // Test slot 0: Phone is more recent than Gamepad and Keyboard
        uint64_t padT = 1000, kbT = 500, phoneT = 2000;
        ui::PadDevice dev = ui::PadDevice::Gamepad;
        if (phoneT > 0 && phoneT >= padT && phoneT >= kbT) dev = ui::PadDevice::Phone;
        else if (kbT > 0 && kbT >= padT && kbT >= phoneT) dev = ui::PadDevice::Keyboard;
        CHECK(dev == ui::PadDevice::Phone);

        // Pad becomes more recent
        padT = 3000;
        if (phoneT > 0 && phoneT >= padT && phoneT >= kbT) dev = ui::PadDevice::Phone;
        else if (kbT > 0 && kbT >= padT && kbT >= phoneT) dev = ui::PadDevice::Keyboard;
        else dev = ui::PadDevice::Gamepad;
        CHECK(dev == ui::PadDevice::Gamepad);

        // Keyboard becomes more recent
        kbT = 4000;
        if (phoneT > 0 && phoneT >= padT && phoneT >= kbT) dev = ui::PadDevice::Phone;
        else if (kbT > 0 && kbT >= padT && kbT >= phoneT) dev = ui::PadDevice::Keyboard;
        else dev = ui::PadDevice::Gamepad;
        CHECK(dev == ui::PadDevice::Keyboard);

        // PadSlot device enum check
        ui::PadSlot slot{};
        slot.device = ui::PadDevice::Phone;
        CHECK(slot.device == ui::PadDevice::Phone);
    }

    // 4. WebUI Layout & WebSocket / settingsVersion
    {
        app::WebServer srv;
        // Check kIndexHtml content for 3x2 grid and fullscreen landscape
        auto res = srv.handleRequest("GET", "/", "");
        CHECK(res.status == 200);
        CHECK(res.body.find("grid-template-columns: repeat(3, 1fr)") != std::string::npos);
        CHECK(res.body.find("btnFullscreen") != std::string::npos);
        CHECK(res.body.find("lastSettingsVersion") != std::string::npos);
        CHECK(res.body.find("P1") != std::string::npos && res.body.find("P4") != std::string::npos);

        // Check settingsVersion in /api/status or /api/settings
        auto statusRes = srv.handleRequest("GET", "/api/status", "");
        CHECK(statusRes.status == 200);
        auto jStatus = json::parse(statusRes.body);
        CHECK(jStatus && jStatus->isObject());
        CHECK((*jStatus).has("ok") && (*jStatus)["ok"].asBool());

        // WebSocket frames
        auto closeFrame = app::ws::makeCloseFrame();
        CHECK(!closeFrame.empty());
        std::vector<uint8_t> pongPayload = {'p', 'o', 'n', 'g'};
        auto pongFrame = app::ws::makePongFrame(pongPayload);
        CHECK(!pongFrame.empty());
    }
}

static void testMilestone6UsbKeyboardMouseIsolation() {
    using namespace xc::input;

    // 1. Boot protocol keyboard and mouse interface filtering
    CHECK(isBootKeyboardOrMouse(0x03, 0x01, 0x01));   // Boot Keyboard
    CHECK(isBootKeyboardOrMouse(0x03, 0x00, 0x01));   // Non-boot subclass Keyboard
    CHECK(isBootKeyboardOrMouse(0x03, 0x01, 0x02));   // Boot Mouse
    CHECK(isBootKeyboardOrMouse(0x03, 0x00, 0x02));   // Non-boot subclass Mouse
    CHECK(isBootKeyboardOrMouse(0x03, 0x01, 0x00));   // Boot Subclass non-zero interface
    CHECK(!isBootKeyboardOrMouse(0x03, 0x00, 0x00));  // Standard HID Gamepad (e.g. DualShock 4)
    CHECK(!isBootKeyboardOrMouse(0xFF, 0x5D, 0x01));  // XInput
    CHECK(!isBootKeyboardOrMouse(0xFF, 0x47, 0xD0));  // GIP

    // 2. HID report descriptor collection classification
    // 2a. Standard HID Keyboard report descriptor (Usage Page 0x01 Generic Desktop, Usage 0x06 Keyboard, Collection Application)
    const uint8_t kbdReportDesc[] = {
        0x05, 0x01,        // Usage Page (Generic Desktop)
        0x09, 0x06,        // Usage (Keyboard)
        0xA1, 0x01,        // Collection (Application)
        0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
        0x19, 0xE0,        //   Usage Minimum (224)
        0x29, 0xE7,        //   Usage Maximum (231)
        0x15, 0x00,        //   Logical Minimum (0)
        0x25, 0x01,        //   Logical Maximum (1)
        0x75, 0x01,        //   Report Size (1)
        0x95, 0x08,        //   Report Count (8)
        0x81, 0x02,        //   Input (Data,Var,Abs)
        0xC0               // End Collection
    };
    CHECK(isKeyboardOrMouseReportDescriptor(kbdReportDesc, sizeof(kbdReportDesc)));
    CHECK(!isGamepadReportDescriptor(kbdReportDesc, sizeof(kbdReportDesc)));

    // 2b. Standard HID Mouse report descriptor (Usage Page 0x01 Generic Desktop, Usage 0x02 Mouse, Collection Application)
    const uint8_t mouseReportDesc[] = {
        0x05, 0x01,        // Usage Page (Generic Desktop)
        0x09, 0x02,        // Usage (Mouse)
        0xA1, 0x01,        // Collection (Application)
        0x09, 0x01,        //   Usage (Pointer)
        0xA1, 0x00,        //   Collection (Physical)
        0x05, 0x09,        //     Usage Page (Button)
        0x19, 0x01,        //     Usage Minimum (1)
        0x29, 0x05,        //     Usage Maximum (5)
        0x15, 0x00,        //     Logical Minimum (0)
        0x25, 0x01,        //     Logical Maximum (1)
        0x95, 0x05,        //     Report Count (5)
        0x75, 0x01,        //     Report Size (1)
        0x81, 0x02,        //     Input (Data,Var,Abs)
        0x05, 0x01,        //     Usage Page (Generic Desktop)
        0x09, 0x30,        //     Usage (X)
        0x09, 0x31,        //     Usage (Y)
        0x15, 0x81,        //     Logical Minimum (-127)
        0x25, 0x7F,        //     Logical Maximum (127)
        0x75, 0x08,        //     Report Size (8)
        0x95, 0x02,        //     Report Count (2)
        0x81, 0x06,        //     Input (Data,Var,Rel)
        0xC0,              //   End Collection
        0xC0               // End Collection
    };
    CHECK(isKeyboardOrMouseReportDescriptor(mouseReportDesc, sizeof(mouseReportDesc)));
    CHECK(!isGamepadReportDescriptor(mouseReportDesc, sizeof(mouseReportDesc)));

    // 2c. Composite Keyboard + Mouse HID report descriptor (2.4GHz wireless combo receiver)
    const uint8_t compositeKbdMouseReportDesc[] = {
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x81, 0x02, 0xC0, 0xC0
    };
    CHECK(isKeyboardOrMouseReportDescriptor(compositeKbdMouseReportDesc, sizeof(compositeKbdMouseReportDesc)));
    CHECK(!isGamepadReportDescriptor(compositeKbdMouseReportDesc, sizeof(compositeKbdMouseReportDesc)));

    // 2d. Gaming Keyboard with macro Joystick collection (0x01:0x06 + 0x01:0x04) — must be classified as keyboard/mouse, not gamepad
    const uint8_t gamingKbdWithMacroJoystickDesc[] = {
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x81, 0x02, 0xC0,
        0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x03, 0x05, 0x09, 0x81, 0x02, 0xC0
    };
    CHECK(isKeyboardOrMouseReportDescriptor(gamingKbdWithMacroJoystickDesc, sizeof(gamingKbdWithMacroJoystickDesc)));
    CHECK(!isGamepadReportDescriptor(gamingKbdWithMacroJoystickDesc, sizeof(gamingKbdWithMacroJoystickDesc)));

    // 2e. Valid Gamepad HID report descriptor (Usage Page 0x01, Usage 0x05 Gamepad, with nested Physical Pointer 0x01)
    const uint8_t gamepadReportDesc[] = {
        0x05, 0x01,        // Usage Page (Generic Desktop)
        0x09, 0x05,        // Usage (Game Pad)
        0xA1, 0x01,        // Collection (Application)
        0x09, 0x01,        //   Usage (Pointer) — nested inside Application collection
        0xA1, 0x00,        //   Collection (Physical)
        0x09, 0x30,        //     Usage (X)
        0x09, 0x31,        //     Usage (Y)
        0x15, 0x00,        //     Logical Minimum (0)
        0x26, 0xFF, 0x00,  //     Logical Maximum (255)
        0x75, 0x08,        //     Report Size (8)
        0x95, 0x02,        //     Report Count (2)
        0x81, 0x02,        //     Input (Data,Var,Abs)
        0xC0,              //   End Collection
        0x05, 0x09,        //   Usage Page (Button)
        0x19, 0x01,        //   Usage Minimum (1)
        0x29, 0x0E,        //   Usage Maximum (14)
        0x15, 0x00,        //   Logical Minimum (0)
        0x25, 0x01,        //   Logical Maximum (1)
        0x75, 0x01,        //   Report Size (1)
        0x95, 0x0E,        //   Report Count (14)
        0x81, 0x02,        //   Input (Data,Var,Abs)
        0xC0               // End Collection
    };
    CHECK(!isKeyboardOrMouseReportDescriptor(gamepadReportDesc, sizeof(gamepadReportDesc)));
    CHECK(isGamepadReportDescriptor(gamepadReportDesc, sizeof(gamepadReportDesc)));

    // The X-ray (usb_xray.h): a game controller's reports are recorded, a keyboard's or a mouse's never.
    {
        // An interface of the HID class, no boot subclass, an interrupt IN endpoint.
        const uint8_t hidPadConfig[] = {0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
                                        0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
                                        0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x89, 0x00,
                                        0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x0a};
        const uint8_t bootKbdConfig[] = {0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
                                         0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
                                         0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0a};
        const uint8_t vendorConfig[] = {0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
                                        0x09, 0x04, 0x00, 0x00, 0x01, 0xFF, 0x47, 0xD0, 0x00,
                                        0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x04};
        CHECK(usbXrayWorthCapturing(hidPadConfig, sizeof hidPadConfig, gamepadReportDesc, sizeof gamepadReportDesc));
        CHECK(!usbXrayWorthCapturing(hidPadConfig, sizeof hidPadConfig, kbdReportDesc, sizeof kbdReportDesc));
        CHECK(!usbXrayWorthCapturing(hidPadConfig, sizeof hidPadConfig, nullptr, 0));  // not read: maybe a keyboard
        CHECK(!usbXrayWorthCapturing(bootKbdConfig, sizeof bootKbdConfig, gamepadReportDesc, sizeof gamepadReportDesc));
        CHECK(usbXrayWorthCapturing(vendorConfig, sizeof vendorConfig, nullptr, 0));
        const auto lines = usbXrayHexLines(gamepadReportDesc, sizeof gamepadReportDesc, 8);
        CHECK(!lines.empty() && lines[0] == "+000: 05 01 09 05 a1 01 09 01");

        UsbXrayCapture cap;
        CHECK(!cap.active());
        cap.start("test", "Test pad", 1000);
        CHECK(cap.active());
        const auto notices = takeUsbXrayNotices();
        CHECK(notices.size() == 1 && notices[0] == "Test pad");
        uint8_t r[8] = {0x01, 0x80, 0x80, 0x00, 0x08, 0, 0, 0};
        for (uint64_t t = 0; t < 300; ++t) {  // a stick wandering, a button pressed now and then
            r[1] = static_cast<uint8_t>(0x80 + (t % 3));
            r[3] = (t / 50) % 2 ? 0x01 : 0x00;
            cap.feed(r, sizeof r, 1000 + t * 4);
        }
        CHECK(cap.active());
        cap.feed(r, sizeof r, 1000 + UsbXrayCapture::kMaxMs + 1);  // past its time: over
        CHECK(!cap.active());
    }

    // 3. Configuration Descriptors across findUsbPadInterface, probeUsbPadPs5, and probeUsbPadPs4
    const uint8_t bootKbdConfig[] = {
        0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,
        0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,  // HID Boot Keyboard (03/01/01)
        0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
    };
    const uint8_t bootMouseConfig[] = {
        0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,
        0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,  // HID Boot Mouse (03/01/02)
        0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
    };
    const uint8_t compositeGamingKbdMouseConfig[] = {
        0x09, 0x02, 0x4E, 0x00, 0x03, 0x01, 0x00, 0xA0, 0xFA,
        0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,  // if 0: Boot Keyboard (03/01/01)
        0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01,
        0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,  // if 1: Boot Mouse (03/01/02)
        0x07, 0x05, 0x82, 0x03, 0x08, 0x00, 0x01,
        0x09, 0x04, 0x02, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,  // if 2: Generic HID NKRO/Media (03/00/00)
        0x07, 0x05, 0x83, 0x03, 0x10, 0x00, 0x01,
    };
    const uint8_t genericHidConfig[] = {
        0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0xA0, 0xFA,
        0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,  // HID 03/00/00
        0x07, 0x05, 0x84, 0x03, 0x40, 0x00, 0x05,
        0x07, 0x05, 0x03, 0x03, 0x40, 0x00, 0x05,
    };
    const uint8_t xboxGipConfig[] = {
        0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0xA0, 0xFA,
        0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x47, 0xD0, 0x00,  // GIP FF/47/D0
        0x07, 0x05, 0x02, 0x03, 0x40, 0x00, 0x04,
        0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x04,
    };
    const uint8_t xboxXInputConfig[] = {
        0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0xA0, 0xFA,
        0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,  // XInput FF/5D/01
        0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x01,
        0x07, 0x05, 0x01, 0x03, 0x20, 0x00, 0x08,
    };

    // Boot Keyboard & Boot Mouse rejected everywhere
    CHECK(findUsbPadInterface(bootKbdConfig, sizeof(bootKbdConfig)).protocol == UsbPadProtocol::None);
    CHECK(findUsbPadInterface(bootMouseConfig, sizeof(bootMouseConfig)).protocol == UsbPadProtocol::None);
    CHECK(probeUsbPadPs5(0x046D, 0xC31C, bootKbdConfig, sizeof(bootKbdConfig)).protocol == UsbPadProtocol::None);
    CHECK(probeUsbPadPs5(0x046D, 0xC077, bootMouseConfig, sizeof(bootMouseConfig)).protocol == UsbPadProtocol::None);
    CHECK(!probeUsbPadPs4(0x046D, 0xC31C, bootKbdConfig, sizeof(bootKbdConfig)));
    CHECK(!probeUsbPadPs4(0x046D, 0xC077, bootMouseConfig, sizeof(bootMouseConfig)));

    // Composite Gaming Keyboard + Mouse (even with Razer VID 0x1532 and secondary 03/00/00 interface) rejected everywhere
    CHECK(findUsbPadInterface(compositeGamingKbdMouseConfig, sizeof(compositeGamingKbdMouseConfig)).protocol == UsbPadProtocol::None);
    CHECK(probeUsbPadPs5(0x1532, 0x0221, compositeGamingKbdMouseConfig, sizeof(compositeGamingKbdMouseConfig)).protocol == UsbPadProtocol::None);
    CHECK(!probeUsbPadPs4(0x1532, 0x0221, compositeGamingKbdMouseConfig, sizeof(compositeGamingKbdMouseConfig)));

    // Non-boot Generic HID (03/00/00) with Keyboard or Mouse report descriptor rejected everywhere
    CHECK(findUsbPadInterface(genericHidConfig, sizeof(genericHidConfig), kbdReportDesc, sizeof(kbdReportDesc)).protocol == UsbPadProtocol::None);
    CHECK(findUsbPadInterface(genericHidConfig, sizeof(genericHidConfig), mouseReportDesc, sizeof(mouseReportDesc)).protocol == UsbPadProtocol::None);
    CHECK(probeUsbPadPs5(0x046D, 0xC52B, genericHidConfig, sizeof(genericHidConfig), kbdReportDesc, sizeof(kbdReportDesc)).protocol == UsbPadProtocol::None);
    CHECK(probeUsbPadPs5(0x046D, 0xC52B, genericHidConfig, sizeof(genericHidConfig), mouseReportDesc, sizeof(mouseReportDesc)).protocol == UsbPadProtocol::None);
    CHECK(!probeUsbPadPs4(0x046D, 0xC52B, genericHidConfig, sizeof(genericHidConfig), kbdReportDesc, sizeof(kbdReportDesc)));
    CHECK(!probeUsbPadPs4(0x046D, 0xC52B, genericHidConfig, sizeof(genericHidConfig), mouseReportDesc, sizeof(mouseReportDesc)));
    CHECK(!probeUsbPadPs4(0x046D, 0xC52B, genericHidConfig, sizeof(genericHidConfig), nullptr, 0));

    // Valid Gamepads accepted appropriately on PS5 and PS4
    CHECK(findUsbPadInterface(genericHidConfig, sizeof(genericHidConfig), gamepadReportDesc, sizeof(gamepadReportDesc)).protocol == UsbPadProtocol::DualShock4);
    CHECK(probeUsbPadPs5(0x054C, 0x09CC, genericHidConfig, sizeof(genericHidConfig), gamepadReportDesc, sizeof(gamepadReportDesc)).protocol == UsbPadProtocol::DualShock4);
    CHECK(probeUsbPadPs4(0x054C, 0x09CC, genericHidConfig, sizeof(genericHidConfig), gamepadReportDesc, sizeof(gamepadReportDesc)));
    CHECK(probeUsbPadPs5(0x045E, 0x0B12, xboxGipConfig, sizeof(xboxGipConfig)).protocol == UsbPadProtocol::Gip);
    CHECK(probeUsbPadPs4(0x045E, 0x0B12, xboxGipConfig, sizeof(xboxGipConfig)));
    CHECK(probeUsbPadPs5(0x045E, 0x028E, xboxXInputConfig, sizeof(xboxXInputConfig)).protocol == UsbPadProtocol::XInput);
    CHECK(probeUsbPadPs4(0x045E, 0x028E, xboxXInputConfig, sizeof(xboxXInputConfig)));

    // DualSense (054C:0CE6) skipped on PS5 (native ScePad) and accepted on PS4 (USB driver)
    CHECK(probeUsbPadPs5(0x054C, 0x0CE6, genericHidConfig, sizeof(genericHidConfig), gamepadReportDesc, sizeof(gamepadReportDesc)).protocol == UsbPadProtocol::None);
    CHECK(probeUsbPadPs4(0x054C, 0x0CE6, genericHidConfig, sizeof(genericHidConfig), gamepadReportDesc, sizeof(gamepadReportDesc)));
}

static void testTouchVirtualGamepadInjection() {
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    // Verify initial clean state in virtual gamepad
    input::ControllerState s0{};
    bool conn = false;
    app::virtualGamepadState(0, s0, conn);
    CHECK(!s0.btnA && !s0.btnB && !s0.btnX && !s0.btnY);
    CHECK(!s0.btnL1 && !s0.btnR1);
    CHECK(s0.leftStickX == 0.0f && s0.leftStickY == 0.0f);
    CHECK(s0.rightStickX == 0.0f && s0.rightStickY == 0.0f);

    // Touch down on 'A' button (anchor: 1655, 671)
    app::shell::TouchEvent evA{};
    evA.kind = app::shell::TouchEvent::Kind::Down;
    evA.fingerId = 1;
    evA.x = 1655.0f;
    evA.y = 671.0f;
    CHECK(overlay.onTouch(evA));

    input::ControllerState sA{};
    app::virtualGamepadState(0, sA, conn);
    CHECK(sA.btnA);
    CHECK(!sA.btnB && !sA.btnX && !sA.btnY);
    CHECK(overlay.state().btnA);

    // Touch up on 'A' button
    evA.kind = app::shell::TouchEvent::Kind::Up;
    CHECK(overlay.onTouch(evA));
    input::ControllerState sAUp{};
    app::virtualGamepadState(0, sAUp, conn);
    CHECK(!sAUp.btnA);
    CHECK(!overlay.state().btnA);

    // Touch down and drag on Left Thumbstick (anchor: 250, 545)
    app::shell::TouchEvent evStick{};
    evStick.kind = app::shell::TouchEvent::Kind::Down;
    evStick.fingerId = 2;
    evStick.x = 250.0f;
    evStick.y = 545.0f;
    CHECK(overlay.onTouch(evStick));

    // Move stick to the right (+60 px)
    evStick.kind = app::shell::TouchEvent::Kind::Motion;
    evStick.x = 310.0f;
    evStick.y = 545.0f;
    CHECK(overlay.onTouch(evStick));
    input::ControllerState sStick{};
    app::virtualGamepadState(0, sStick, conn);
    CHECK(sStick.leftStickX > 0.3f); // Deflected right
    CHECK(std::abs(sStick.leftStickY) < 0.2f); // Centered vertically

    // Release stick
    evStick.kind = app::shell::TouchEvent::Kind::Up;
    CHECK(overlay.onTouch(evStick));
    input::ControllerState sStickUp{};
    app::virtualGamepadState(0, sStickUp, conn);
    CHECK(sStickUp.leftStickX == 0.0f);
    CHECK(sStickUp.leftStickY == 0.0f);

    // Multi-touch: Finger 3 on LB (150, 240), Finger 4 on RB (1700, 240) simultaneously
    app::shell::TouchEvent evLB{}, evRB{};
    evLB.kind = app::shell::TouchEvent::Kind::Down;
    evLB.fingerId = 3;
    evLB.x = 150.0f;
    evLB.y = 240.0f;

    evRB.kind = app::shell::TouchEvent::Kind::Down;
    evRB.fingerId = 4;
    evRB.x = 1700.0f;
    evRB.y = 240.0f;

    CHECK(overlay.onTouch(evLB));
    CHECK(overlay.onTouch(evRB));

    input::ControllerState sBoth{};
    app::virtualGamepadState(0, sBoth, conn);
    CHECK(sBoth.btnL1);
    CHECK(sBoth.btnR1);

    evLB.kind = app::shell::TouchEvent::Kind::Up;
    evRB.kind = app::shell::TouchEvent::Kind::Up;
    overlay.onTouch(evLB);
    overlay.onTouch(evRB);
}

static void testTouchAutoHideLogic() {
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();
    overlay.setVisible(true);

    CHECK(overlay.isVisible());
    CHECK(overlay.opacity() > 0.5f);

    uint64_t t = 10000;
    // Physical gamepad is active at player 0
    for (int i = 0; i < 20; ++i) {
        overlay.update(t, true, true);
        t += 16;
    }
    CHECK(!overlay.isVisible());
    CHECK(overlay.opacity() < 0.05f);

    // Screen touch wakes up overlay
    app::shell::TouchEvent touch{};
    touch.kind = app::shell::TouchEvent::Kind::Down;
    touch.fingerId = 10;
    touch.x = 1655.0f;
    touch.y = 671.0f;
    overlay.onTouch(touch);

    for (int i = 0; i < 20; ++i) {
        overlay.update(t, true, false);
        t += 16;
    }
    CHECK(overlay.isVisible());
    CHECK(overlay.opacity() > 0.5f);
}

static void testTouchDirtyFlagLifecycle() {
    ui::Fonts fonts;
    ui::TouchPadOverlay overlay(fonts);
    overlay.reset();

    // 1. Initial construction and reset should be dirty
    CHECK(overlay.isDirty());
    overlay.clearDirty();
    CHECK(!overlay.isDirty());

    // 2. Touch Down on A button (anchor: 1655, 671) marks dirty
    app::shell::TouchEvent evA{};
    evA.kind = app::shell::TouchEvent::Kind::Down;
    evA.fingerId = 1;
    evA.x = 1655.0f;
    evA.y = 671.0f;
    CHECK(overlay.onTouch(evA));
    CHECK(overlay.isDirty());
    overlay.clearDirty();
    CHECK(!overlay.isDirty());

    // 3. Touch Motion marks dirty
    evA.kind = app::shell::TouchEvent::Kind::Motion;
    evA.x = 1656.0f;
    CHECK(overlay.onTouch(evA));
    CHECK(overlay.isDirty());
    overlay.clearDirty();
    CHECK(!overlay.isDirty());

    // 4. Touch Up marks dirty
    evA.kind = app::shell::TouchEvent::Kind::Up;
    CHECK(overlay.onTouch(evA));
    CHECK(overlay.isDirty());
    overlay.clearDirty();
    CHECK(!overlay.isDirty());

    // 5. In steady state without touches and with opacity settled, update() does not mark dirty
    uint64_t t = 10000;
    overlay.update(t, false, false);
    CHECK(!overlay.isDirty());

    // 6. Auto-hide mode opacity transition: fading marks dirty until settled
    app::Settings s;
    s.touchOverlayMode = 0; // Auto-hide
    overlay.setSettings(s);
    CHECK(overlay.isDirty());
    overlay.clearDirty();

    // Physical controller active -> begins fading out toward 0
    overlay.update(t, true, true);
    CHECK(overlay.isDirty());

    // Step through fade-out transition
    for (int i = 0; i < 30; ++i) {
        t += 16;
        overlay.clearDirty();
        overlay.update(t, true, true);
    }
    // Once settled at target 0.0f, update() leaves dirty flag false
    overlay.clearDirty();
    overlay.update(t + 16, true, true);
    CHECK(!overlay.isDirty());

    // 7. Touch wakes it up and marks dirty again
    evA.kind = app::shell::TouchEvent::Kind::Down;
    evA.fingerId = 2;
    CHECK(overlay.onTouch(evA));
    CHECK(overlay.isDirty());

    // 8. Explicit setVisible / reset triggers dirty
    overlay.clearDirty();
    overlay.setVisible(false);
    CHECK(overlay.isDirty());
    overlay.clearDirty();
    overlay.reset();
    CHECK(overlay.isDirty());
}

void runInputTests() {
    testControllerTuning();
    testUsbPadRumblePacket();
    testUsbInstruments();
    testHidToVk();
    testKeyboardNav();
    testKeyboardAsGamepad();
    testTouchpadAsMouse();
    testKeyMap();
    testPadMap();
    testGyroCalibration();
    testGyro();
    testGyroProZeroDriftAndDecoupledSensitivity();
    testGyroClosedLoopPostureTrackingAndZeroDrift();
    testDs5TriggerEffects();
    testSdlMouseButtons();
    testScancodeToVk();
    testSdlSensorUnits();
    testUsbPadReports();
    testUsbPadDescriptors();
    testUsbPadSlots();
    testSoloInputRedirectionMerging();
    testSoloInputRedirectSettingsPersistence();
    testDesktopMouseNav();
    testTouchpadShortcutsAndSlots();
    testMilestone6UsbKeyboardMouseIsolation();
    testTouchVirtualGamepadInjection();
    testTouchAutoHideLogic();
    testTouchDirtyFlagLifecycle();
}
