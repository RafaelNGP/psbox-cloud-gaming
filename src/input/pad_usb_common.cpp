// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "input/pad_usb_common.h"

#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace xc::input {

int UsbPadSlotPool::take() {
    for (int s = 0; s < kMaxUsbPads; ++s) {
        bool expected = false;
        if (taken[s].compare_exchange_strong(expected, true)) return s;
    }
    return -1;
}

void UsbPadSlotPool::release(int slot) {
    if (slot >= 0 && slot < kMaxUsbPads) taken[slot] = false;
}

std::string usbPadHex(const uint8_t* p, int n) {
    std::string s;
    char b[4];
    for (int i = 0; i < n && i < 32; ++i) {
        std::snprintf(b, sizeof b, "%02x ", p[i]);
        s += b;
    }
    return s;
}

unsigned usbPadButtonBits(const ControllerState& s) {
    const bool b[] = {s.btnA,       s.btnB,       s.btnX,        s.btnY,
                      s.dpadUp,     s.dpadDown,   s.dpadLeft,    s.dpadRight,
                      s.btnL1,      s.btnR1,      s.btnL3,       s.btnR3,
                      s.btnOptions, s.btnTouchpad, s.btnNexus,
                      s.triggerL2 > 0.5f, s.triggerR2 > 0.5f};
    unsigned bits = 0;
    for (size_t i = 0; i < sizeof b; ++i) bits |= b[i] ? 1u << i : 0;
    return bits;
}

std::string usbPadButtonNames(unsigned bits) {
    static const char* const kNames[] = {"A",    "B",    "X",    "Y",    "Up",   "Down", "Left", "Right", "LB",
                                         "RB",   "LS",   "RS",   "Menu", "View", "Xbox", "LT",   "RT"};
    std::string s;
    for (size_t i = 0; i < sizeof kNames / sizeof *kNames; ++i) {
        if (bits & (1u << i)) s += s.empty() ? kNames[i] : std::string(" ") + kNames[i];
    }
    return s.empty() ? "none" : s;
}

std::string usbPadReceiverDeviceName(int subtype) {
    switch (usbPadKindForSubtype(subtype)) {
        case UsbPadKind::Guitar: return "Xbox 360 Guitar";
        case UsbPadKind::Drums: return "Xbox 360 Drum Kit";
        case UsbPadKind::Other: return std::string("Xbox 360 ") + xinputSubtypeName(subtype);
        default: return subtype < 0 ? "Xbox 360 Wireless" : "Xbox 360 Controller";
    }
}

UsbPadInterface usbPadXrayInterface(const uint8_t* config, size_t configLen) {
    UsbPadInterface found;
    bool candidate = false;
    for (size_t off = 0; off + 2 <= configLen; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > configLen) break;
        if (config[off + 1] == 0x04 && l >= 9) {
            if (candidate && found.epIn) return found;
            found = {UsbPadProtocol::GenericHid, config[off + 2], 0, 0, false};
            candidate =
                (config[off + 5] == 0x03 && !isBootKeyboardOrMouse(config[off + 5], config[off + 6], config[off + 7])) ||
                config[off + 5] == 0xFF;
        } else if (config[off + 1] == 0x05 && l >= 7 && candidate && (config[off + 3] & 0x03) == 0x03) {
            uint8_t& ep = (config[off + 2] & 0x80) ? found.epIn : found.epOut;
            if (!ep) ep = config[off + 2];
        }
    }
    return candidate && found.epIn ? found : UsbPadInterface{};
}

void usbPadXrayReadHidReports(
    const std::string& where, const uint8_t* config, size_t configLen,
    const std::function<int(uint8_t iface, uint8_t* buf, uint16_t want)>& readReportDesc,
    std::vector<uint8_t>* outGamepadReport) {
    if (!readReportDesc) return;
    uint8_t iface = 0, sub = 0xff;
    bool hid = false;
    for (size_t off = 0; off + 2 <= configLen; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > configLen) break;
        if (config[off + 1] == 0x04 && l >= 9) {
            iface = config[off + 2];
            hid = config[off + 5] == 0x03;
            sub = config[off + 6];
        }
        if (config[off + 1] != 0x21 || l < 9 || !hid || sub != 0x00) continue;  // a HID descriptor, no boot device
        uint8_t desc[1024] = {};
        const uint16_t want =
            std::min<uint16_t>(static_cast<uint16_t>(config[off + 7] | (config[off + 8] << 8)), sizeof desc);
        if (!want) continue;
        const int r = readReportDesc(iface, desc, want);
        if (r <= 0) continue;
        const size_t got = std::min<size_t>(static_cast<size_t>(r), want);
        usbXrayReportDescriptor(where, iface, desc, got);
        if (outGamepadReport && outGamepadReport->empty() && isGamepadReportDescriptor(desc, got)) {
            outGamepadReport->assign(desc, desc + got);
        }
    }
}

void usbPadHandleReport(const UsbPadReportContext& ctx, const uint8_t* buf, int n) {
    if (!ctx.link || !ctx.subtype) return;
    UsbPadLinkReportState& link = *ctx.link;
    int& subtype = *ctx.subtype;

    if (link.xray.active()) link.xray.feed(buf, static_cast<size_t>(n), ctx.nowMs);
    if (ctx.xrayOnly) return;

    if (ctx.protocol == UsbPadProtocol::Gip && ctx.sendAck) {
        for (const auto& ack : gipAcksFor(buf, static_cast<size_t>(n))) {
            ctx.sendAck(ack.data(), ack.size());
        }
    }

    if (ctx.wireless) {
        // Linked (08 80) or gone (08 00); the link report that follows says what it is.
        if (n >= 2 && buf[0] == 0x08) {
            const bool linked = (buf[1] & 0x80) != 0;
            XC_LOGI("usb pad: %.*s channel %d: device %s", static_cast<int>(ctx.where.size()), ctx.where.data(),
                    ctx.channel, linked ? "linked" : "gone");
            if (ctx.onPresent) ctx.onPresent(linked);
            if (!linked && ctx.onDetach) ctx.onDetach();
            return;
        }
        if (n >= 16 && buf[0] == 0x00 && buf[1] == 0x05 && buf[5] == 0x12) {
            XC_LOGI("usb pad: %.*s channel %d: capabilities: vendor %04x product %04x version %04x: %s",
                    static_cast<int>(ctx.where.size()), ctx.where.data(), ctx.channel,
                    buf[10] | (buf[11] << 8), buf[12] | (buf[13] << 8), buf[14] | (buf[15] << 8),
                    usbPadHex(buf, n).c_str());
            // No link report (linked before the app started): an instrument told by its maker.
            if (subtype < 0) {
                if (const int st = xinputSubtypeFromCapabilities(buf, static_cast<size_t>(n)); st >= 0) {
                    subtype = st;
                    XC_LOGI("usb pad: %.*s channel %d: device is a %s (subtype 0x%02x, from its maker and product)",
                            static_cast<int>(ctx.where.size()), ctx.where.data(), ctx.channel,
                            xinputSubtypeName(st), st);
                    const int slot = ctx.getSlot ? ctx.getSlot() : -1;
                    if (slot >= 0) usbPadSetKind(slot, usbPadKindForSubtype(st));
                }
            }
            return;
        }
        if (const int st = xinputWirelessSubtype(buf, static_cast<size_t>(n)); st >= 0) {
            subtype = st;
            if (ctx.onPresent) ctx.onPresent(true);
            XC_LOGI("usb pad: %.*s channel %d: device is a %s (subtype 0x%02x%s): %s",
                    static_cast<int>(ctx.where.size()), ctx.where.data(), ctx.channel,
                    xinputSubtypeName(st), st, (buf[25] & 0x80) ? ", motors flag" : "", usbPadHex(buf, n).c_str());
            const int slot = ctx.getSlot ? ctx.getSlot() : -1;
            if (slot < 0) {
                if (ctx.onAttach) ctx.onAttach();
            } else {
                usbPadSetKind(slot, usbPadKindForSubtype(st));
            }
            return;
        }
    }

    ControllerState state = link.state;
    bool parsed = false;
    if (ctx.ps3 != Ps3Instrument::None) {
        parsed = parsePs3InstrumentReport(ctx.ps3, buf, static_cast<size_t>(n), state);
    } else if (ctx.hid != nullptr) {
        parsed = parseGenericHidReport(*ctx.hid, buf, static_cast<size_t>(n), state);
    } else if (ctx.useGenericHidFallback && ctx.protocol == UsbPadProtocol::GenericHid) {
        parsed = parseGenericHidFallback(buf, static_cast<size_t>(n), state);
    } else {
        parsed = parseUsbPadReport(ctx.protocol, buf, static_cast<size_t>(n), state);
    }

    // Input from a receiver's channel nobody announced (linked before the app started): a player
    // too, of a kind not known until its capabilities report says it.
    if (parsed && ctx.wireless && (!ctx.getSlot || ctx.getSlot() < 0)) {
        if (ctx.onAttach) ctx.onAttach();
    }

    const int slot = ctx.getSlot ? ctx.getSlot() : -1;
    if (link.shown < 6) {
        XC_LOGI("usb pad %d: report %d: %d bytes: %s", slot, ++link.shown, n, usbPadHex(buf, n).c_str());
    }
    if (!parsed) {
        if (std::chrono::steady_clock::now() - link.lastOther > std::chrono::seconds(5)) {
            link.lastOther = std::chrono::steady_clock::now();
            XC_LOGD("usb pad %d: other report, %d bytes: %s", slot, n, usbPadHex(buf, n).c_str());
        }
        return;
    }

    link.state = state;
    if (slot < 0) return;
    usbPadState(slot, state);

    // An instrument's sticks (a guitar's whammy and tilt, a drum kit's pad strengths): their moves,
    // to set what raising the guitar reads.
    const UsbPadKind kind =
        ctx.ps3 != Ps3Instrument::None ? ps3InstrumentKind(ctx.ps3) : usbPadKindForSubtype(subtype);
    if (kind != UsbPadKind::Gamepad && link.sticksLogged < 3000 &&
        (std::abs(state.rawRightY - link.loggedRightY) > 0.08f ||
         std::abs(state.rawRightX - link.loggedRightX) > 0.15f)) {
        ++link.sticksLogged;
        link.loggedRightX = state.rawRightX;
        link.loggedRightY = state.rawRightY;
        XC_LOGI("usb pad %d: %s sticks: left %.2f,%.2f right %.2f,%.2f (whammy x, tilt y)", slot,
                usbPadKindName(kind), state.rawLeftX, state.rawLeftY, state.rawRightX, state.rawRightY);
    }

    // The first presses, to check the buttons from the log (an instrument's, more of them).
    const unsigned now = usbPadButtonBits(state);
    if (now != link.buttons && link.changesLogged < (kind != UsbPadKind::Gamepad ? 400 : 80)) {
        ++link.changesLogged;
        XC_LOGI("usb pad %d: buttons %s, sticks %.2f,%.2f %.2f,%.2f, triggers %.2f %.2f", slot,
                usbPadButtonNames(now).c_str(), state.rawLeftX, state.rawLeftY, state.rawRightX, state.rawRightY,
                state.triggerL2, state.triggerR2);
    }
    link.buttons = now;
}

}  // namespace xc::input
