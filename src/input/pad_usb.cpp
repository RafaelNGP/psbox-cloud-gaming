// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB pads (pad_usb.h): their reports parsed, and their state kept for
// pollUsbPad(). The report layouts are those of the Linux xpad driver and
// OmniPad CE (usb_controllers.c).
#include <cstddef>
#include <cstdint>
#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include <initializer_list>

#include "input/keyboard_nav.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>

namespace xc::input {

namespace {

std::atomic<bool> g_usbPadsEnabled{true};

int16_t le16(const uint8_t* p) { return static_cast<int16_t>(p[0] | (p[1] << 8)); }


float stick(int16_t v) { return std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f); }

// XInput and GIP sticks have +y up; ControllerState has +y down.
void setSticks(ControllerState& s, const uint8_t* p) {
    s.rawLeftX = s.leftStickX = stick(le16(p));
    s.rawLeftY = s.leftStickY = -stick(le16(p + 2));
    s.rawRightX = s.rightStickX = stick(le16(p + 4));
    s.rawRightY = s.rightStickY = -stick(le16(p + 6));
}

std::atomic<bool> g_guitarStrum{false};
std::atomic<bool> g_guitarTilt{true};
std::atomic<float> g_guitarTiltAt{-0.9f};

struct Slot {
    bool attached = false;
    std::string name;
    UsbPadKind kind = UsbPadKind::Gamepad;
    InstrumentState instrument;
    ControllerState game;  // an instrument's state as the game gets it (instrumentForGame)
    ControllerState state;  // as read; connected once its first input came
    int player = -1;        // -1: given one at the next poll
    uint64_t order = 0;     // when it came, for the players' order
    // The Xbox / Home button held: LB / RB move the pad to another player;
    // let go without them, it asks for the in-game menu. It never reaches
    // the game itself (the menu has an Xbox button item).
    bool guideHeld = false, comboUsed = false;
    bool waitRelease = false;  // out of the game until every button is let go
    uint64_t activeAt = 0;
};

std::mutex g_mutex;
std::array<Slot, kMaxUsbPads> g_slots;
std::vector<UsbPadEvent> g_events;
uint64_t g_attachCount = 0;

// The players for the pads that came since the last poll, in their order.
void assignPlayers() {
    for (;;) {
        Slot* next = nullptr;
        for (Slot& s : g_slots)
            if (s.attached && s.player < 0 && (!next || s.order < next->order)) next = &s;
        if (!next) return;
        next->player = 0;  // all taken: the first player
        for (int p = 0; p < kMaxPads; ++p) {
            bool taken = padConnected(p);
            for (const Slot& o : g_slots) taken |= o.attached && o.player == p && &o != next;
            if (!taken) {
                next->player = p;
                break;
            }
        }
        XC_LOGI("usb pad: %s plays as player %d", next->name.c_str(), next->player + 1);
        g_events.push_back({UsbPadEvent::Kind::Connected, next->player, next->name, static_cast<int>(next - g_slots.data())});
    }
}

// A pad's state for the game and the menus: the settings applied, LB / RB
// held back while the Xbox button is down, the Xbox button itself never.
ControllerState shown(const Slot& slot) {
    ControllerState s = slot.state;
    applyPadSettings(s);
    if (slot.guideHeld) s.btnL1 = s.btnR1 = false;
    s.btnNexus = false;
    return s;
}

void movePlayer(Slot& s, int slot, int player) {
    s.player = player;
    XC_LOGI("usb pad %d: %s moved to player %d", slot, s.name.c_str(), player + 1);
    g_events.push_back({UsbPadEvent::Kind::Moved, player, s.name, slot});
}

// What the game gets from a pad: an instrument passed on as a gamepad (the menus get it as read).
ControllerState shownInGame(const Slot& slot) {
    if (slot.kind == UsbPadKind::Gamepad) return shown(slot);
    ControllerState s = slot.game;
    applyStrumGaps(s, slot.instrument, platform::nowMs());
    applyPadSettings(s);
    if (slot.guideHeld) s.btnL1 = s.btnR1 = false;
    s.btnNexus = false;
    return s;
}

// `player` -1: every pad (the menus, so a pad waiting for its buttons to be
// let go too); otherwise those playing as it, but `except`.
bool pollWhere(int player, int except, ControllerState& out) {
    out = ControllerState{};
    if (!g_usbPadsEnabled.load(std::memory_order_relaxed)) return false;
    bool any = false;
    std::lock_guard<std::mutex> lock(g_mutex);
    assignPlayers();
    for (size_t i = 0; i < g_slots.size(); ++i) {
        const Slot& slot = g_slots[i];
        if (!slot.attached || !slot.state.connected) continue;
        if (player >= 0 && (slot.player != player || slot.waitRelease || static_cast<int>(i) == except)) continue;
        ControllerState s = player >= 0 ? shownInGame(slot) : shown(slot);
        if (any) mergePads(out, s);
        else out = s;
        any = true;
    }
    return any;
}

}  // namespace

bool isBootKeyboardOrMouse(uint8_t ifaceClass, uint8_t ifaceSubclass, uint8_t ifaceProtocol) {
    if (ifaceClass != 0x03) return false;
    if (ifaceProtocol == 0x01 || ifaceProtocol == 0x02) return true;
    if (ifaceSubclass == 0x01) return true;
    return false;
}

namespace {

struct HidCollectionInfo {
    bool hasKeyboard = false;
    bool hasMouse = false;
    bool hasGamepad = false;
};

HidCollectionInfo scanHidReportCollections(const uint8_t* d, size_t n) {
    HidCollectionInfo info;
    if (!d || n == 0) return info;

    uint16_t page = 0;
    uint32_t usages[64] = {};
    int nusages = 0;
    int depth = 0;
    uint16_t pageStack[8] = {};
    int pageDepth = 0;

    size_t i = 0;
    while (i < n) {
        uint8_t p = d[i];
        if (p == 0xFE) { // Long item
            if (i + 2 >= n) break;
            size_t dataLen = d[i + 1];
            i += 3 + dataLen;
            continue;
        }

        int size = p & 0x03;
        if (size == 3) size = 4;
        int type = (p >> 2) & 0x03;
        int tag = (p >> 4) & 0x0F;
        const uint8_t* v = d + i + 1;
        if (i + 1 + static_cast<size_t>(size) > n) break;
        i += 1 + size;

        uint32_t val = 0;
        for (int k = 0; k < size; ++k) {
            val |= static_cast<uint32_t>(v[k]) << (8 * k);
        }

        if (type == 1) { // Global
            if (tag == 0) { // Usage Page
                page = static_cast<uint16_t>(val);
            } else if (tag == 10) { // Push
                if (pageDepth < 8) pageStack[pageDepth++] = page;
            } else if (tag == 11) { // Pop
                if (pageDepth > 0) page = pageStack[--pageDepth];
            }
        } else if (type == 2) { // Local
            if (tag == 0) { // Usage
                uint32_t fullUsage = 0;
                if (size <= 2) {
                    fullUsage = (static_cast<uint32_t>(page) << 16) | (val & 0xFFFF);
                } else {
                    fullUsage = val;
                }
                if (nusages < 64) usages[nusages++] = fullUsage;
            }
        } else if (type == 0) { // Main
            if (tag == 10) { // Collection
                uint32_t collType = val;
                if (depth == 0 || collType == 0x01) {
                    if (nusages > 0) {
                        for (int uIdx = 0; uIdx < nusages; ++uIdx) {
                            uint32_t u = usages[uIdx];
                            uint16_t uPage = static_cast<uint16_t>(u >> 16);
                            uint16_t uId = static_cast<uint16_t>(u & 0xFFFF);
                            if (uPage == 0) uPage = page;

                            if (uPage == 0x01) { // Generic Desktop
                                if (uId == 0x01 || uId == 0x02) {
                                    info.hasMouse = true;
                                } else if (uId == 0x06 || uId == 0x07) {
                                    info.hasKeyboard = true;
                                } else if (uId == 0x04 || uId == 0x05 || uId == 0x08) {
                                    info.hasGamepad = true;
                                }
                            } else if (uPage == 0x07) { // Keyboard/Keypad
                                info.hasKeyboard = true;
                            } else if (uPage == 0x05) { // Game Controls
                                info.hasGamepad = true;
                            }
                        }
                    } else {
                        if (page == 0x07) info.hasKeyboard = true;
                        else if (page == 0x05) info.hasGamepad = true;
                    }
                }
                depth++;
                nusages = 0;
            } else if (tag == 12) { // End Collection
                if (depth > 0) depth--;
                nusages = 0;
            } else {
                nusages = 0;
            }
        }
    }
    return info;
}

}  // namespace

bool isKeyboardOrMouseReportDescriptor(const uint8_t* reportDesc, size_t len) {
    if (!reportDesc || len == 0) return false;
    HidCollectionInfo info = scanHidReportCollections(reportDesc, len);
    return info.hasKeyboard || info.hasMouse;
}

bool isGamepadReportDescriptor(const uint8_t* reportDesc, size_t len) {
    if (!reportDesc || len == 0) return false;
    HidCollectionInfo info = scanHidReportCollections(reportDesc, len);
    return info.hasGamepad && !info.hasKeyboard && !info.hasMouse;
}

UsbPadProtocol usbPadProtocolFor(uint8_t ifaceClass, uint8_t ifaceSubclass, uint8_t ifaceProtocol) {
    if (ifaceClass == 0x03) {
        // Standard HID: DualShock 4 is subclass 0, protocol 0.
        // Skip keyboards (protocol 1) and mice (protocol 2) so they are never claimed as gamepads.
        if (isBootKeyboardOrMouse(ifaceClass, ifaceSubclass, ifaceProtocol)) {
            return UsbPadProtocol::None;
        }
        return UsbPadProtocol::DualShock4;
    }
    if (ifaceClass == 0xFF) {
        if (ifaceSubclass == 0x5D && (ifaceProtocol == 0x01 || ifaceProtocol == 0x81 || ifaceProtocol == 0x00))
            return UsbPadProtocol::XInput;
        if (ifaceSubclass == 0x47 && ifaceProtocol == 0xD0)
            return UsbPadProtocol::Gip;
    }
    return UsbPadProtocol::None;
}

UsbPadInterface findUsbPadInterface(const uint8_t* d, size_t len, const uint8_t* reportDesc, size_t reportLen) {
    if (reportDesc && reportLen > 0) {
        if (isKeyboardOrMouseReportDescriptor(reportDesc, reportLen) || !isGamepadReportDescriptor(reportDesc, reportLen)) {
            return UsbPadInterface{};
        }
    }
    bool hasBootKbdOrMouse = false;
    for (size_t off = 0; d && off + 2 <= len; off += d[off]) {
        const size_t l = d[off];
        if (l < 2 || off + l > len) break;
        if (d[off + 1] == 0x04 && l >= 9 && isBootKeyboardOrMouse(d[off + 5], d[off + 6], d[off + 7])) {
            hasBootKbdOrMouse = true;
            break;
        }
    }
    const bool hasVerifiedGamepadReport =
        reportDesc && reportLen > 0 && isGamepadReportDescriptor(reportDesc, reportLen) && !isKeyboardOrMouseReportDescriptor(reportDesc, reportLen);

    UsbPadInterface pad;
    bool inPad = false;  // the endpoints that follow are the pad interface's
    for (size_t off = 0; d && off + 2 <= len; off += d[off]) {
        const size_t l = d[off];
        if (l < 2 || off + l > len) break;
        if (d[off + 1] == 0x04 && l >= 9) {  // an interface
            if (inPad && pad.epIn) return pad;
            UsbPadProtocol proto = usbPadProtocolFor(d[off + 5], d[off + 6], d[off + 7]);
            if (d[off + 5] == 0x03 && hasBootKbdOrMouse && !hasVerifiedGamepadReport) {
                proto = UsbPadProtocol::None;
            }
            pad = {proto, d[off + 2], 0, 0, proto == UsbPadProtocol::XInput && d[off + 7] == 0x81};
            inPad = pad.protocol != UsbPadProtocol::None && d[off + 3] == 0;
        } else if (d[off + 1] == 0x05 && l >= 7 && inPad && (d[off + 3] & 0x03) == 0x03) {  // an interrupt endpoint
            const uint8_t addr = d[off + 2];
            uint8_t& ep = (addr & 0x80) ? pad.epIn : pad.epOut;
            if (!ep) ep = addr;
        }
    }
    if (inPad && pad.epIn) {
        if ((pad.protocol == UsbPadProtocol::DualShock4 || pad.protocol == UsbPadProtocol::GenericHid) &&
            reportDesc && reportLen > 0) {
            if (isKeyboardOrMouseReportDescriptor(reportDesc, reportLen) || !isGamepadReportDescriptor(reportDesc, reportLen)) {
                return UsbPadInterface{};
            }
        }
        return pad;
    }
    return UsbPadInterface{};
}

UsbPadInterface probeUsbPadPs5(uint16_t vid, uint16_t pid, const uint8_t* configDesc, size_t configLen,
                              const uint8_t* reportDesc, size_t reportLen) {
    if (vid == 0x0e8d || (vid == 0x054C && pid == 0x0CE6)) {
        return UsbPadInterface{};
    }

    if (reportDesc && reportLen > 0) {
        if (isKeyboardOrMouseReportDescriptor(reportDesc, reportLen)) {
            return UsbPadInterface{};
        }
    }

    bool hasBootKbdOrMouse = false;
    for (size_t off = 0; configDesc && off + 2 <= configLen; off += configDesc[off]) {
        const size_t l = configDesc[off];
        if (l < 2 || off + l > configLen) break;
        if (configDesc[off + 1] == 0x04 && l >= 9 &&
            isBootKeyboardOrMouse(configDesc[off + 5], configDesc[off + 6], configDesc[off + 7])) {
            hasBootKbdOrMouse = true;
            break;
        }
    }

    UsbPadInterface iface = findUsbPadInterface(configDesc, configLen, reportDesc, reportLen);

    if (!hasBootKbdOrMouse && vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC)) {
        if (reportDesc && reportLen > 0 && isKeyboardOrMouseReportDescriptor(reportDesc, reportLen)) {
            return UsbPadInterface{};
        }
        iface.protocol = UsbPadProtocol::DualShock4;
        iface.number = 0;
        if (!iface.epIn) iface.epIn = 0x81;
    } else if (!hasBootKbdOrMouse && vid == 0x045E && pid == 0x0719) {
        iface.protocol = UsbPadProtocol::XInput;
        iface.number = 0;
        iface.wireless = true;
        if (!iface.epIn) iface.epIn = 0x81;
    }

    if (iface.protocol == UsbPadProtocol::DualShock4 || iface.protocol == UsbPadProtocol::GenericHid) {
        if (reportDesc && reportLen > 0) {
            if (isKeyboardOrMouseReportDescriptor(reportDesc, reportLen) || !isGamepadReportDescriptor(reportDesc, reportLen)) {
                return UsbPadInterface{};
            }
        }
    }

    return iface;
}

bool probeUsbPadPs4(uint16_t vid, uint16_t pid, const uint8_t* configDesc, size_t configLen,
                    const uint8_t* reportDesc, size_t reportLen) {
    if (reportDesc && reportLen > 0) {
        if (isKeyboardOrMouseReportDescriptor(reportDesc, reportLen)) {
            return false;
        }
    }

    bool hasBootKbdOrMouse = false;
    for (size_t off = 0; configDesc && off + 2 <= configLen; off += configDesc[off]) {
        const size_t l = configDesc[off];
        if (l < 2 || off + l > configLen) break;
        if (configDesc[off + 1] == 0x04 && l >= 9 &&
            isBootKeyboardOrMouse(configDesc[off + 5], configDesc[off + 6], configDesc[off + 7])) {
            hasBootKbdOrMouse = true;
            break;
        }
    }

    if (!hasBootKbdOrMouse && vid == 0x045E) {
        if (pid == 0x028E || pid == 0x02D1 || pid == 0x02DD || pid == 0x02E3 || pid == 0x02EA ||
            pid == 0x0B00 || pid == 0x0B0A || pid == 0x0B12 || pid == 0x0719) {
            return true;
        }
    }

    if (!hasBootKbdOrMouse && vid == 0x054C && (pid == 0x0268 || pid == 0x0CE6 || pid == 0x0DF2)) return true;
    if (!hasBootKbdOrMouse && vid == 0x057E && (pid == 0x2009 || pid == 0x2006 || pid == 0x2007 || pid == 0x200E ||
                          pid == 0x2017 || pid == 0x2019 || pid == 0x201E)) return true;
    if (!hasBootKbdOrMouse && vid == 0x18D1 && pid == 0x9400) return true;

    if (!hasBootKbdOrMouse && (vid == 0x054C || vid == 0x146B || vid == 0x1532 || vid == 0x0F0D || vid == 0x2C22 || vid == 0x0C12)) {
        if (reportDesc && reportLen > 0) {
            if (isGamepadReportDescriptor(reportDesc, reportLen)) return true;
            if (isKeyboardOrMouseReportDescriptor(reportDesc, reportLen)) return false;
        } else if (pid == 0x05C4 || pid == 0x09CC) {
            return true;
        }
    }

    bool found = false;
    for (size_t off = 0; configDesc && off + 2 <= configLen; off += configDesc[off]) {
        const size_t l = configDesc[off];
        if (l < 2 || off + l > configLen) break;
        if (configDesc[off + 1] == 0x04 && l >= 9) {
            uint8_t ifClass = configDesc[off + 5];
            uint8_t ifSubClass = configDesc[off + 6];
            uint8_t ifProto = configDesc[off + 7];

            if (ifClass == 0xFF) {
                if ((ifSubClass == 0x5D && (ifProto == 0x01 || ifProto == 0x81 || ifProto == 0x00)) ||
                    (ifSubClass == 0x47 && ifProto == 0xD0)) {
                    found = true;
                }
            } else if (ifClass == 0x03) {
                if (isBootKeyboardOrMouse(ifClass, ifSubClass, ifProto)) {
                    continue;
                }
                if (!reportDesc || reportLen == 0) {
                    continue;
                }
                if (reportDesc && reportLen > 0) {
                    if (isKeyboardOrMouseReportDescriptor(reportDesc, reportLen) || !isGamepadReportDescriptor(reportDesc, reportLen)) {
                        continue;
                    }
                }
                found = true;
            }
        }
    }

    return found;
}

std::vector<std::array<uint8_t, 13>> gipAcksFor(const uint8_t* data, size_t len) {
    std::vector<std::array<uint8_t, 13>> acks;
    for (size_t off = 0; data && off + 4 <= len;) {
        const uint8_t* m = data + off;
        const size_t payload = m[3];
        if ((m[3] & 0x80) || payload < 2 || payload > 60 || off + 4 + payload > len) break;
        off += 4 + payload;
        if (!(m[1] & 0x10)) continue;
        const auto client = static_cast<uint8_t>((m[1] & 0x0f) | 0x20);
        acks.push_back({0x01, client, m[2], 0x09, 0x00, m[0], client, static_cast<uint8_t>(payload), 0, 0, 0, 0, 0});
    }
    return acks;
}

// 00 14 | buttons (16 bits) | LT | RT | LX LY RX RY (16 bits each) | ...
// Clones fill the length byte in their own way; the type and the size
// tell an input report from the LED (01), rumble (03) and status ones.
bool parseXInputReport(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data) return false;
    // The wireless receiver (after the Linux xpad driver): presence, then input behind a header.
    if (len >= 2 && data[0] == 0x08) {
        if (data[1] & 0x80) return false;  // a pad connected: nothing pressed yet
        s = ControllerState{};             // gone: let go of everything
        return true;
    }
    // Input from the receiver: 00 01 00 F0 then the wired report (00 13 ...). Its other messages
    // (00 0F announce, 00 05 / 00 00 status, 00 01 00 13 ...) are not input: read as a wired
    // report they pressed A B X Y and both triggers when a pad linked up.
    if (len >= 18 && data[0] == 0x00 && data[1] == 0x01 && data[3] == 0xF0) return parseXInputReport(data + 4, len - 4, s);
    // A wired report: 00, then its length (0x14; 0x13 behind the receiver's header).
    if (len < 14 || data[0] != 0x00 || (data[1] != 0x14 && data[1] != 0x13)) return false;
    const unsigned b = static_cast<unsigned>(data[2] | (data[3] << 8));
    s.dpadUp = b & 0x0001;
    s.dpadDown = b & 0x0002;
    s.dpadLeft = b & 0x0004;
    s.dpadRight = b & 0x0008;
    s.btnOptions = b & 0x0010;   // Start (Menu)
    s.btnTouchpad = b & 0x0020;  // Back (View)
    s.btnL3 = b & 0x0040;
    s.btnR3 = b & 0x0080;
    s.btnL1 = b & 0x0100;
    s.btnR1 = b & 0x0200;
    s.btnNexus = b & 0x0400;  // Guide
    s.btnA = b & 0x1000;
    s.btnB = b & 0x2000;
    s.btnX = b & 0x4000;
    s.btnY = b & 0x8000;
    s.triggerL2 = static_cast<float>(data[4]) / 255.0f;
    s.triggerR2 = static_cast<float>(data[5]) / 255.0f;
    setSticks(s, data + 6);
    s.connected = true;
    return true;
}

// A GIP message: command | options | sequence | payload length | payload.
// 0x20, the input: buttons (16 bits) | LT RT (0..1023) | LX LY RX RY. The
// Xbox button comes by itself, in a 0x07 message (bit 0 of its payload).
bool parseGipReport(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 5) return false;
    if (data[0] == 0x07) {
        s.btnNexus = data[4] & 0x01;
        s.connected = true;
        return true;
    }
    if (data[0] != 0x20 || len < 18) return false;
    const unsigned b = static_cast<unsigned>(data[4] | (data[5] << 8));
    s.btnOptions = b & 0x0004;   // Menu
    s.btnTouchpad = b & 0x0008;  // View
    s.btnA = b & 0x0010;
    s.btnB = b & 0x0020;
    s.btnX = b & 0x0040;
    s.btnY = b & 0x0080;
    s.dpadUp = b & 0x0100;
    s.dpadDown = b & 0x0200;
    s.dpadLeft = b & 0x0400;
    s.dpadRight = b & 0x0800;
    s.btnL1 = b & 0x1000;
    s.btnR1 = b & 0x2000;
    s.btnL3 = b & 0x4000;
    s.btnR3 = b & 0x8000;
    s.triggerL2 = std::min(static_cast<float>(static_cast<uint16_t>(le16(data + 6))) / 1023.0f, 1.0f);
    s.triggerR2 = std::min(static_cast<float>(static_cast<uint16_t>(le16(data + 8))) / 1023.0f, 1.0f);
    setSticks(s, data + 10);
    s.connected = true;
    return true;
}

const char* usbPadProtocolName(UsbPadProtocol p) {
    switch (p) {
        case UsbPadProtocol::XInput: return "XInput";
        case UsbPadProtocol::Gip: return "GIP";
        case UsbPadProtocol::DualShock3: return "DualShock 3";
        case UsbPadProtocol::DualShock4: return "DualShock 4";
        case UsbPadProtocol::DualSense: return "DualSense";
        case UsbPadProtocol::SwitchPro: return "Switch Pro";
        case UsbPadProtocol::SwitchInputOnly: return "Switch";
        case UsbPadProtocol::Stadia: return "Stadia";
        case UsbPadProtocol::GenericHid: return "Generic HID";
        case UsbPadProtocol::None: break;
    }
    return "none";
}

bool parseDualShock3Report(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 14 || data[0] != 0x01) return false;
    const uint8_t b1 = data[2];
    const uint8_t b2 = data[3];
    const uint8_t b3 = (len > 4) ? data[4] : 0;

    s.btnTouchpad = (b1 & 0x01) != 0; // Select
    s.btnL3 = (b1 & 0x02) != 0;
    s.btnR3 = (b1 & 0x04) != 0;
    s.btnOptions = (b1 & 0x08) != 0;  // Start
    s.dpadUp = (b1 & 0x10) != 0;
    s.dpadRight = (b1 & 0x20) != 0;
    s.dpadDown = (b1 & 0x40) != 0;
    s.dpadLeft = (b1 & 0x80) != 0;

    s.btnL1 = (b2 & 0x04) != 0;
    s.btnR1 = (b2 & 0x08) != 0;
    s.btnY = (b2 & 0x10) != 0; // Triangle
    s.btnB = (b2 & 0x20) != 0; // Circle
    s.btnA = (b2 & 0x40) != 0; // Cross
    s.btnX = (b2 & 0x80) != 0; // Square

    s.btnNexus = (b3 & 0x01) != 0; // PS button

    if (len >= 10) {
        s.rawLeftX = s.leftStickX = (static_cast<float>(data[6]) - 128.0f) / 127.5f;
        s.rawLeftY = s.leftStickY = (static_cast<float>(data[7]) - 128.0f) / 127.5f;
        s.rawRightX = s.rightStickX = (static_cast<float>(data[8]) - 128.0f) / 127.5f;
        s.rawRightY = s.rightStickY = (static_cast<float>(data[9]) - 128.0f) / 127.5f;
    }
    if (len >= 20) {
        s.triggerL2 = static_cast<float>(data[18]) / 255.0f;
        s.triggerR2 = static_cast<float>(data[19]) / 255.0f;
    } else {
        s.triggerL2 = (b2 & 0x01) ? 1.0f : 0.0f;
        s.triggerR2 = (b2 & 0x02) ? 1.0f : 0.0f;
    }

    s.connected = true;
    return true;
}

bool parseDualShock4Report(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 10) return false;
    const uint8_t* p = (data[0] == 0x01 && len >= 11) ? data + 1 : data;
    const size_t effectiveLen = (data[0] == 0x01 && len >= 11) ? (len - 1) : len;
    if (effectiveLen < 9) return false;

    s.rawLeftX = s.leftStickX = (static_cast<float>(p[0]) - 128.0f) / 127.5f;
    s.rawLeftY = s.leftStickY = (static_cast<float>(p[1]) - 128.0f) / 127.5f;
    s.rawRightX = s.rightStickX = (static_cast<float>(p[2]) - 128.0f) / 127.5f;
    s.rawRightY = s.rightStickY = (static_cast<float>(p[3]) - 128.0f) / 127.5f;

    const uint8_t b0 = p[4];
    const uint8_t hat = b0 & 0x0F;
    s.dpadUp = s.dpadDown = s.dpadLeft = s.dpadRight = false;
    switch (hat) {
        case 0: s.dpadUp = true; break;
        case 1: s.dpadUp = s.dpadRight = true; break;
        case 2: s.dpadRight = true; break;
        case 3: s.dpadDown = s.dpadRight = true; break;
        case 4: s.dpadDown = true; break;
        case 5: s.dpadDown = s.dpadLeft = true; break;
        case 6: s.dpadLeft = true; break;
        case 7: s.dpadUp = s.dpadLeft = true; break;
        default: break;
    }
    s.btnX = (b0 & 0x10) != 0; // Square
    s.btnA = (b0 & 0x20) != 0; // Cross
    s.btnB = (b0 & 0x40) != 0; // Circle
    s.btnY = (b0 & 0x80) != 0; // Triangle

    const uint8_t b1 = p[5];
    s.btnL1 = (b1 & 0x01) != 0;
    s.btnR1 = (b1 & 0x02) != 0;
    s.btnOptions = (b1 & 0x20) != 0;  // Options
    s.btnL3 = (b1 & 0x40) != 0;
    s.btnR3 = (b1 & 0x80) != 0;

    const uint8_t b2 = p[6];
    s.btnNexus = (b2 & 0x01) != 0; // PS button
    s.btnTouchpad = ((b1 & 0x10) != 0) || ((b2 & 0x02) != 0); // Share or Touchpad click

    if (effectiveLen >= 9) {
        s.triggerL2 = static_cast<float>(p[7]) / 255.0f;
        s.triggerR2 = static_cast<float>(p[8]) / 255.0f;
    } else {
        s.triggerL2 = (b1 & 0x04) ? 1.0f : 0.0f;
        s.triggerR2 = (b1 & 0x08) ? 1.0f : 0.0f;
    }

    if (effectiveLen >= 39) {
        if ((p[34] & 0x80) == 0) { // active touch 1
            s.touching = true;
            uint16_t tx = static_cast<uint16_t>(p[35] | ((p[36] & 0x0F) << 8));
            uint16_t ty = static_cast<uint16_t>((p[36] >> 4) | (p[37] << 4));
            s.touchX = std::clamp(static_cast<float>(tx) / 1919.0f, 0.0f, 1.0f);
            s.touchY = std::clamp(static_cast<float>(ty) / 941.0f, 0.0f, 1.0f);
            s.touchSecond = (effectiveLen >= 43 && (p[38] & 0x80) == 0);
        } else {
            s.touching = false;
            s.touchSecond = false;
        }
    }

    s.connected = true;
    return true;
}

bool parseDualSenseReport(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 10) return false;
    const uint8_t* p = (data[0] == 0x01 && len >= 11) ? data + 1 : data;
    const size_t effectiveLen = (data[0] == 0x01 && len >= 11) ? (len - 1) : len;
    if (effectiveLen < 10) return false;

    s.rawLeftX = s.leftStickX = (static_cast<float>(p[0]) - 128.0f) / 127.5f;
    s.rawLeftY = s.leftStickY = (static_cast<float>(p[1]) - 128.0f) / 127.5f;
    s.rawRightX = s.rightStickX = (static_cast<float>(p[2]) - 128.0f) / 127.5f;
    s.rawRightY = s.rightStickY = (static_cast<float>(p[3]) - 128.0f) / 127.5f;
    s.triggerL2 = static_cast<float>(p[4]) / 255.0f;
    s.triggerR2 = static_cast<float>(p[5]) / 255.0f;

    const uint8_t b0 = p[7];
    const uint8_t hat = b0 & 0x0F;
    s.dpadUp = s.dpadDown = s.dpadLeft = s.dpadRight = false;
    switch (hat) {
        case 0: s.dpadUp = true; break;
        case 1: s.dpadUp = s.dpadRight = true; break;
        case 2: s.dpadRight = true; break;
        case 3: s.dpadDown = s.dpadRight = true; break;
        case 4: s.dpadDown = true; break;
        case 5: s.dpadDown = s.dpadLeft = true; break;
        case 6: s.dpadLeft = true; break;
        case 7: s.dpadUp = s.dpadLeft = true; break;
        default: break;
    }
    s.btnX = (b0 & 0x10) != 0; // Square
    s.btnA = (b0 & 0x20) != 0; // Cross
    s.btnB = (b0 & 0x40) != 0; // Circle
    s.btnY = (b0 & 0x80) != 0; // Triangle

    const uint8_t b1 = p[8];
    s.btnL1 = (b1 & 0x01) != 0;
    s.btnR1 = (b1 & 0x02) != 0;
    s.btnOptions = (b1 & 0x20) != 0;  // Options
    s.btnL3 = (b1 & 0x40) != 0;
    s.btnR3 = (b1 & 0x80) != 0;

    const uint8_t b2 = p[9];
    s.btnNexus = (b2 & 0x01) != 0; // PS button
    s.btnTouchpad = ((b1 & 0x10) != 0) || ((b2 & 0x02) != 0); // Create/Share or Touchpad click

    if (effectiveLen >= 36) {
        if ((p[32] & 0x80) == 0) { // active touch 1
            s.touching = true;
            uint16_t tx = static_cast<uint16_t>(p[33] | ((p[34] & 0x0F) << 8));
            uint16_t ty = static_cast<uint16_t>((p[34] >> 4) | (p[35] << 4));
            s.touchX = std::clamp(static_cast<float>(tx) / 1919.0f, 0.0f, 1.0f);
            s.touchY = std::clamp(static_cast<float>(ty) / 1079.0f, 0.0f, 1.0f);
            s.touchSecond = (effectiveLen >= 40 && (p[36] & 0x80) == 0);
        } else {
            s.touching = false;
            s.touchSecond = false;
        }
    }

    s.connected = true;
    return true;
}

bool parseSwitchProReport(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 11) return false;
    // Full reports (0x30, 0x21, 0x31)
    if (data[0] == 0x30 || data[0] == 0x21 || data[0] == 0x31) {
        if (len < 12) return false;
        const uint8_t btnRight = data[3];
        const uint8_t btnShared = data[4];
        const uint8_t btnLeft = data[5];

        s.btnX = (btnRight & 0x01) != 0; // Y -> Square
        s.btnY = (btnRight & 0x02) != 0; // X -> Triangle
        s.btnA = (btnRight & 0x04) != 0; // B -> Cross
        s.btnB = (btnRight & 0x08) != 0; // A -> Circle
        s.btnR1 = (btnRight & 0x40) != 0;
        s.triggerR2 = (btnRight & 0x80) ? 1.0f : 0.0f;

        s.btnOptions = (btnShared & 0x02) != 0;  // Plus
        s.btnR3 = (btnShared & 0x04) != 0;
        s.btnL3 = (btnShared & 0x08) != 0;
        s.btnNexus = (btnShared & 0x10) != 0;    // Home
        s.btnTouchpad = ((btnShared & 0x01) != 0) || ((btnShared & 0x20) != 0); // Minus or Capture

        s.dpadDown = (btnLeft & 0x01) != 0;
        s.dpadUp = (btnLeft & 0x02) != 0;
        s.dpadRight = (btnLeft & 0x04) != 0;
        s.dpadLeft = (btnLeft & 0x08) != 0;
        s.btnL1 = (btnLeft & 0x40) != 0;
        s.triggerL2 = (btnLeft & 0x80) ? 1.0f : 0.0f;

        uint16_t lx12 = static_cast<uint16_t>(data[6] | ((data[7] & 0x0F) << 8));
        uint16_t ly12 = static_cast<uint16_t>((data[7] >> 4) | (data[8] << 4));
        uint16_t rx12 = static_cast<uint16_t>(data[9] | ((data[10] & 0x0F) << 8));
        uint16_t ry12 = static_cast<uint16_t>((data[10] >> 4) | (data[11] << 4));

        auto normStick = [](uint16_t raw) {
            return std::clamp((static_cast<float>(raw) - 2048.0f) / 2047.0f * 1.42f, -1.0f, 1.0f);
        };
        s.rawLeftX = s.leftStickX = normStick(lx12);
        // Switch Pro native Y axis: 0 is top (-1.0f in ControllerState), 4095 is bottom (+1.0f)
        s.rawLeftY = s.leftStickY = normStick(ly12);
        s.rawRightX = s.rightStickX = normStick(rx12);
        s.rawRightY = s.rightStickY = normStick(ry12);

        s.connected = true;
        return true;
    }
    // Simple reports (0x3F)
    if (data[0] == 0x3F && len >= 11) {
        const uint8_t b0 = data[1];
        const uint8_t b1 = data[2];
        const uint8_t hat = data[3];

        s.btnX = (b0 & 0x01) != 0; // Y -> Square
        s.btnA = (b0 & 0x02) != 0; // B -> Cross
        s.btnB = (b0 & 0x04) != 0; // A -> Circle
        s.btnY = (b0 & 0x08) != 0; // X -> Triangle
        s.btnL1 = (b0 & 0x10) != 0;
        s.btnR1 = (b0 & 0x20) != 0;
        s.triggerL2 = (b0 & 0x40) ? 1.0f : 0.0f;
        s.triggerR2 = (b0 & 0x80) ? 1.0f : 0.0f;

        s.btnOptions = (b1 & 0x02) != 0;  // Plus
        s.btnL3 = (b1 & 0x04) != 0;
        s.btnR3 = (b1 & 0x08) != 0;
        s.btnNexus = (b1 & 0x10) != 0;    // Home
        s.btnTouchpad = ((b1 & 0x01) != 0) || ((b1 & 0x20) != 0); // Minus or Capture

        s.dpadUp = s.dpadDown = s.dpadLeft = s.dpadRight = false;
        switch (hat) {
            case 0: s.dpadUp = true; break;
            case 1: s.dpadUp = s.dpadRight = true; break;
            case 2: s.dpadRight = true; break;
            case 3: s.dpadDown = s.dpadRight = true; break;
            case 4: s.dpadDown = true; break;
            case 5: s.dpadDown = s.dpadLeft = true; break;
            case 6: s.dpadLeft = true; break;
            case 7: s.dpadUp = s.dpadLeft = true; break;
            default: break;
        }

        s.rawLeftX = s.leftStickX = (static_cast<float>(data[5]) - 128.0f) / 127.5f;
        s.rawLeftY = s.leftStickY = (static_cast<float>(data[7]) - 128.0f) / 127.5f;
        s.rawRightX = s.rightStickX = (static_cast<float>(data[9]) - 128.0f) / 127.5f;
        s.rawRightY = s.rightStickY = (static_cast<float>(data[11]) - 128.0f) / 127.5f;

        s.connected = true;
        return true;
    }
    return false;
}

bool parseSwitchInputOnlyReport(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 7) return false;
    const uint8_t b0 = data[0];
    const uint8_t b1 = data[1];
    const uint8_t hat = data[2];

    s.btnX = (b0 & 0x01) != 0; // Y -> Square
    s.btnA = (b0 & 0x02) != 0; // B -> Cross
    s.btnB = (b0 & 0x04) != 0; // A -> Circle
    s.btnY = (b0 & 0x08) != 0; // X -> Triangle
    s.btnL1 = (b0 & 0x10) != 0;
    s.btnR1 = (b0 & 0x20) != 0;
    s.triggerL2 = (b0 & 0x40) ? 1.0f : 0.0f;
    s.triggerR2 = (b0 & 0x80) ? 1.0f : 0.0f;

    s.btnOptions = (b1 & 0x02) != 0;  // Plus
    s.btnL3 = (b1 & 0x04) != 0;
    s.btnR3 = (b1 & 0x08) != 0;
    s.btnNexus = (b1 & 0x10) != 0;    // Home
    s.btnTouchpad = ((b1 & 0x01) != 0) || ((b1 & 0x20) != 0); // Minus or Capture

    s.dpadUp = s.dpadDown = s.dpadLeft = s.dpadRight = false;
    switch (hat) {
        case 0: s.dpadUp = true; break;
        case 1: s.dpadUp = s.dpadRight = true; break;
        case 2: s.dpadRight = true; break;
        case 3: s.dpadDown = s.dpadRight = true; break;
        case 4: s.dpadDown = true; break;
        case 5: s.dpadDown = s.dpadLeft = true; break;
        case 6: s.dpadLeft = true; break;
        case 7: s.dpadUp = s.dpadLeft = true; break;
        default: break;
    }

    s.rawLeftX = s.leftStickX = (static_cast<float>(data[3]) - 128.0f) / 127.5f;
    s.rawLeftY = s.leftStickY = (static_cast<float>(data[4]) - 128.0f) / 127.5f;
    s.rawRightX = s.rightStickX = (static_cast<float>(data[5]) - 128.0f) / 127.5f;
    s.rawRightY = s.rightStickY = (static_cast<float>(data[6]) - 128.0f) / 127.5f;

    s.connected = true;
    return true;
}

bool parseStadiaReport(const uint8_t* data, size_t len, ControllerState& s) {
    if (!data || len < 9) return false;
    const uint8_t* p = (data[0] == 0x03 && len >= 10) ? data + 1 : data;
    const size_t effectiveLen = (data[0] == 0x03 && len >= 10) ? (len - 1) : len;
    if (effectiveLen < 9) return false;

    const uint8_t hat = p[0];
    s.dpadUp = s.dpadDown = s.dpadLeft = s.dpadRight = false;
    switch (hat) {
        case 0: s.dpadUp = true; break;
        case 1: s.dpadUp = s.dpadRight = true; break;
        case 2: s.dpadRight = true; break;
        case 3: s.dpadDown = s.dpadRight = true; break;
        case 4: s.dpadDown = true; break;
        case 5: s.dpadDown = s.dpadLeft = true; break;
        case 6: s.dpadLeft = true; break;
        case 7: s.dpadUp = s.dpadLeft = true; break;
        default: break;
    }

    const uint8_t bMain = p[1];
    s.btnNexus = (bMain & 0x10) != 0;       // Stadia button
    s.btnOptions = (bMain & 0x20) != 0;     // Menu
    s.btnTouchpad = ((bMain & 0x03) != 0) || ((bMain & 0x40) != 0); // Capture / Assistant or Options
    s.btnR3 = (bMain & 0x80) != 0;

    const uint8_t bExtra = p[2];
    s.btnL3 = (bExtra & 0x01) != 0;
    s.btnR1 = (bExtra & 0x02) != 0;
    s.btnL1 = (bExtra & 0x04) != 0;
    s.btnY = (bExtra & 0x08) != 0;
    s.btnX = (bExtra & 0x10) != 0;
    s.btnB = (bExtra & 0x20) != 0;
    s.btnA = (bExtra & 0x40) != 0;

    s.rawLeftX = s.leftStickX = (static_cast<float>(p[3]) - 128.0f) / 127.5f;
    s.rawLeftY = s.leftStickY = (static_cast<float>(p[4]) - 128.0f) / 127.5f;
    s.rawRightX = s.rightStickX = (static_cast<float>(p[5]) - 128.0f) / 127.5f;
    s.rawRightY = s.rightStickY = (static_cast<float>(p[6]) - 128.0f) / 127.5f;

    s.triggerL2 = static_cast<float>(p[7]) / 255.0f;
    s.triggerR2 = static_cast<float>(p[8]) / 255.0f;
    if (bMain & 0x04) s.triggerL2 = std::max(s.triggerL2, 1.0f);
    if (bMain & 0x08) s.triggerR2 = std::max(s.triggerR2, 1.0f);

    s.connected = true;
    return true;
}

bool parseUsbPadReport(UsbPadProtocol protocol, const uint8_t* data, size_t len, ControllerState& state) {
    switch (protocol) {
        case UsbPadProtocol::XInput: return parseXInputReport(data, len, state);
        case UsbPadProtocol::Gip: return parseGipReport(data, len, state);
        case UsbPadProtocol::DualShock3: return parseDualShock3Report(data, len, state);
        case UsbPadProtocol::DualShock4: return parseDualShock4Report(data, len, state);
        case UsbPadProtocol::DualSense: return parseDualSenseReport(data, len, state);
        case UsbPadProtocol::SwitchPro: return parseSwitchProReport(data, len, state);
        case UsbPadProtocol::SwitchInputOnly: return parseSwitchInputOnlyReport(data, len, state);
        case UsbPadProtocol::Stadia: return parseStadiaReport(data, len, state);
        case UsbPadProtocol::GenericHid: return parseGenericHidFallback(data, len, state);
        case UsbPadProtocol::None: break;
    }
    return false;
}

void usbPadAttached(int slot, const std::string& name, UsbPadKind kind) {
    if (slot < 0 || slot >= kMaxUsbPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_slots[slot] = Slot{};
    g_slots[slot].attached = true;
    g_slots[slot].name = name;
    g_slots[slot].kind = kind;
    g_slots[slot].order = ++g_attachCount;
    XC_LOGI("usb pad %d: %s connected (%s)", slot, name.c_str(), usbPadKindName(kind));
}

void usbPadSetKind(int slot, UsbPadKind kind) {
    if (slot < 0 || slot >= kMaxUsbPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Slot& s = g_slots[slot];
    if (!s.attached || s.kind == kind) return;
    s.kind = kind;
    s.instrument = InstrumentState{};
    XC_LOGI("usb pad %d: %s is a %s", slot, s.name.c_str(), usbPadKindName(kind));
}

UsbPadKind usbPadSlotKind(int slot) {
    if (slot < 0 || slot >= kMaxUsbPads) return UsbPadKind::Gamepad;
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_slots[slot].attached ? g_slots[slot].kind : UsbPadKind::Gamepad;
}

UsbPadKind usbPadKindFor(int player) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Slot& s : g_slots)
        if (s.attached && s.player == player && player >= 0) return s.kind;
    return UsbPadKind::Gamepad;
}

UsbPadKind usbPadKindForSubtype(int subtype) {
    switch (subtype) {
        case 0x06: case 0x07: case 0x0B: case 0x19: return UsbPadKind::Guitar;
        case 0x08: return UsbPadKind::Drums;
        case -1: case 0x01: case 0x03: case 0x13: return UsbPadKind::Gamepad;
        default: return UsbPadKind::Other;
    }
}

const char* usbPadKindName(UsbPadKind kind) {
    switch (kind) {
        case UsbPadKind::Guitar: return "guitar";
        case UsbPadKind::Drums: return "drum kit";
        case UsbPadKind::Other: return "other device";
        default: return "gamepad";
    }
}

void setGuitarStrum(bool on) {
    if (g_guitarStrum.exchange(on) != on) XC_LOGI("usb pad: guitar strum %s", on ? "on" : "off");
}
bool guitarStrum() { return g_guitarStrum.load(std::memory_order_relaxed); }

void setGuitarTilt(bool on, float at) {
    g_guitarTilt = on;
    g_guitarTiltAt = std::clamp(at, -1.0f, 1.0f);
}
bool guitarTilt() { return g_guitarTilt.load(std::memory_order_relaxed); }

namespace {
// Lane i of the strum layout down or up (kStrumLanes: LT LB RB RT R3).
void setStrumLane(ControllerState& g, int i, bool on) {
    switch (i) {
        case 0: g.triggerL2 = on ? 1.0f : 0.0f; break;
        case 1: g.btnL1 = on; break;
        case 2: g.btnR1 = on; break;
        case 3: g.triggerR2 = on ? 1.0f : 0.0f; break;
        default: g.btnR3 = on; break;
    }
}
}  // namespace

void applyStrumGaps(ControllerState& g, const InstrumentState& st, uint64_t nowMs) {
    if (!st.strumLayout) return;
    for (int i = 0; i < 5; ++i)
        if (nowMs < st.gapUntil[i]) setStrumLane(g, i, false);
}

ControllerState instrumentForGame(UsbPadKind kind, const ControllerState& in, InstrumentState& st, bool strum,
                                  uint64_t nowMs, bool tiltOn, float tiltAt) {
    if (kind == UsbPadKind::Gamepad || kind == UsbPadKind::Other) return in;
    ControllerState out;
    out.connected = in.connected;
    out.btnOptions = in.btnOptions;
    out.btnNexus = in.btnNexus;
    out.dpadUp = in.dpadUp, out.dpadDown = in.dpadDown, out.dpadLeft = in.dpadLeft, out.dpadRight = in.dpadRight;
    if (kind == UsbPadKind::Drums) {
        out.btnA = in.btnA, out.btnB = in.btnB, out.btnX = in.btnX, out.btnY = in.btnY;
        out.btnL1 = in.btnL1, out.btnR1 = in.btnR1, out.btnTouchpad = in.btnTouchpad;
        out.btnL3 = in.btnL3, out.btnR3 = in.btnR3;
        return out;
    }
    // The guitar.
    const bool frets[5] = {in.btnA, in.btnB, in.btnY, in.btnX, in.btnL1};
    const bool bar = in.dpadUp || in.dpadDown;
    const bool strummed = bar && !st.prevStrum;
    st.prevStrum = bar;
    bool lanes[5];
    for (int i = 0; i < 5; ++i) {
        if (!strum) {
            lanes[i] = frets[i];
            st.armed[i] = false;
            continue;
        }
        if (strummed && frets[i]) {
            if (st.armed[i]) st.gapUntil[i] = nowMs + kStrumGapMs;  // held through: a new note
            st.armed[i] = true;
        }
        if (!frets[i]) st.armed[i] = false, st.gapUntil[i] = 0;
        lanes[i] = st.armed[i];
    }
    st.strumLayout = strum;
    // Star power: Back, or the guitar raised (its tilt past `tiltAt`, let go a little before it).
    if (tiltOn && tiltAt != 0.0f) {
        const float v = in.rawRightY, margin = 0.15f;
        if (tiltAt < 0) st.tiltUp = st.tiltUp ? v < tiltAt + margin : v <= tiltAt;
        else st.tiltUp = st.tiltUp ? v > tiltAt - margin : v >= tiltAt;
    } else {
        st.tiltUp = false;
    }
    const bool starPower = in.btnTouchpad || st.tiltUp;
    if (!strum) {
        out.btnA = lanes[0], out.btnB = lanes[1], out.btnY = lanes[2], out.btnX = lanes[3], out.btnL1 = lanes[4];
        out.triggerR2 = starPower ? 1.0f : 0.0f;  // Overdrive (RT by default in Fortnite Festival)
        return out;
    }
    // The menus: the frets at once (orange has no menu use: it is LB, a lane, here).
    out.btnA = frets[0], out.btnB = frets[1], out.btnY = frets[2], out.btnX = frets[3];
    for (int i = 0; i < 5; ++i) setStrumLane(out, i, lanes[i]);
    out.btnL3 = starPower;
    applyStrumGaps(out, st, nowMs);
    return out;
}

std::vector<UsbPadInterface> findXInputWirelessInterfaces(const uint8_t* d, size_t len) {
    std::vector<UsbPadInterface> out;
    UsbPadInterface cur;
    bool inPad = false;
    for (size_t off = 0; d && off + 2 <= len; off += d[off]) {
        const size_t l = d[off];
        if (l < 2 || off + l > len) break;
        if (d[off + 1] == 0x04 && l >= 9) {
            if (inPad && cur.epIn) out.push_back(cur);
            inPad = d[off + 3] == 0 && d[off + 5] == 0xFF && d[off + 6] == 0x5D && d[off + 7] == 0x81;
            cur = {UsbPadProtocol::XInput, d[off + 2], 0, 0, true};
        } else if (d[off + 1] == 0x05 && l >= 7 && inPad && (d[off + 3] & 0x03) == 0x03) {
            uint8_t& ep = (d[off + 2] & 0x80) ? cur.epIn : cur.epOut;
            if (!ep) ep = d[off + 2];
        }
    }
    if (inPad && cur.epIn) out.push_back(cur);
    return out;
}

void usbPadState(int slot, const ControllerState& in) {
    if (slot < 0 || slot >= kMaxUsbPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Slot& s = g_slots[slot];
    if (!s.attached) return;
    if (in.btnNexus && !s.guideHeld) s.guideHeld = true, s.comboUsed = false;
    if (s.guideHeld) {
        const int step = in.btnR1 && !s.state.btnR1 ? 1 : in.btnL1 && !s.state.btnL1 ? -1 : 0;
        if (step && s.player >= 0) {
            movePlayer(s, slot, (s.player + step + kMaxPads) % kMaxPads);
            s.comboUsed = true;
        }
        if (!in.btnNexus) {
            s.guideHeld = false;
            if (!s.comboUsed && s.player >= 0) g_events.push_back({UsbPadEvent::Kind::MenuButton, s.player, s.name, slot});
        }
    }
    s.state = in;
    s.state.connected = true;
    if (s.kind != UsbPadKind::Gamepad)
        s.game = instrumentForGame(s.kind, s.state, s.instrument, guitarStrum(), platform::nowMs(), guitarTilt(),
                                   g_guitarTiltAt.load(std::memory_order_relaxed));
    // An instrument's sticks rest away from the centre (the whammy bar, the tilt sensor): whether
    // it is in use is told from what the game gets.
    ControllerState used = s.kind != UsbPadKind::Gamepad ? s.game : in;
    applyPadSettings(used);
    const bool pressed = inUse(used) || in.btnNexus;
    if (pressed) s.activeAt = platform::nowMs();
    else s.waitRelease = false;
}

void usbPadDetached(int slot) {
    if (slot < 0 || slot >= kMaxUsbPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Slot& s = g_slots[slot];
    if (s.attached) {
        XC_LOGI("usb pad %d: %s disconnected", slot, s.name.c_str());
        if (s.player >= 0) g_events.push_back({UsbPadEvent::Kind::Disconnected, s.player, s.name, slot});
    }
    s = Slot{};
}

int usbPadPlayer(int slot) {
    if (slot < 0 || slot >= kMaxUsbPads) return -1;
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_slots[slot].attached ? g_slots[slot].player : -1;
}

std::vector<UsbPadEvent> takeUsbPadEvents() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<UsbPadEvent> events;
    events.swap(g_events);
    return events;
}

bool pollUsbPad(ControllerState& out) { return pollWhere(-1, -1, out); }

bool pollUsbPadsFor(int player, ControllerState& out, int except) {
    if (player < 0 || player >= kMaxPads) {
        out = ControllerState{};
        return false;
    }
    return pollWhere(player, except, out);
}

bool pollUsbPadSlot(int slot, ControllerState& out) {
    out = ControllerState{};
    if (slot < 0 || slot >= kMaxUsbPads) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    const Slot& s = g_slots[slot];
    if (!s.attached || !s.state.connected) return false;
    out = shown(s);
    return true;
}

void setUsbPadPlayer(int slot, int player) {
    if (slot < 0 || slot >= kMaxUsbPads || player < 0 || player >= kMaxPads) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    Slot& s = g_slots[slot];
    if (s.attached && s.player >= 0 && s.player != player) movePlayer(s, slot, player);
}

void usbPadsWaitRelease() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Slot& s : g_slots) {
        if (!s.attached) continue;
        ControllerState used = s.kind != UsbPadKind::Gamepad ? s.game : s.state;
        applyPadSettings(used);
        s.waitRelease = inUse(used) || s.state.btnNexus;
    }
}

std::string usbPadName(int slot) {
    if (slot < 0 || slot >= kMaxUsbPads) return {};
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_slots[slot].attached ? g_slots[slot].name : std::string();
}

int usbPadCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(std::count_if(g_slots.begin(), g_slots.end(), [](const Slot& s) { return s.attached; }));
}

std::string usbPadNameFor(int player) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Slot& s : g_slots)
        if (s.attached && s.player == player && player >= 0) return s.name;
    return {};
}

uint64_t usbPadActiveAt(int player) {
    std::lock_guard<std::mutex> lock(g_mutex);
    uint64_t at = 0;
    for (const Slot& s : g_slots)
        if (s.attached && s.player == player && player >= 0) at = std::max(at, s.activeAt);
    return at;
}

void setUsbPadsEnabled(bool enabled) {
    bool was = g_usbPadsEnabled.exchange(enabled);
    if (was && !enabled) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int s = 0; s < kMaxUsbPads; ++s) {
            Slot& slot = g_slots[s];
            if (slot.attached) {
                if (slot.player >= 0) {
                    g_events.push_back({UsbPadEvent::Kind::Disconnected, slot.player, slot.name, s});
                }
                slot = Slot{};
            }
        }
        usbPadsStop();
    } else if (!was && enabled) {
        usbPadsStart();
    }
}

bool usbPadsEnabled() {
    return g_usbPadsEnabled.load(std::memory_order_relaxed);
}

std::array<uint8_t, 12> xinputWirelessLedPacket(int player) {
    // 0x40 + the light pattern: 6..9 are "player 1..4 on".
    return {0x00, 0x00, 0x08, static_cast<uint8_t>(0x40 + 6 + std::clamp(player, 0, 3)), 0, 0, 0, 0, 0, 0, 0, 0};
}

int xinputWirelessSubtype(const uint8_t* data, size_t len) {
    if (!data || len < 26 || data[0] != 0x00 || data[1] != 0x0F || data[5] != 0xCC) return -1;
    return data[25] & 0x7F;
}

const char* xinputSubtypeName(int subtype) {
    switch (subtype) {
        case 0x01: return "gamepad";
        case 0x02: return "wheel";
        case 0x03: return "arcade stick";
        case 0x04: return "flight stick";
        case 0x05: return "dance pad";
        case 0x06: return "guitar";
        case 0x07: return "guitar (alternate)";
        case 0x08: return "drum kit";
        case 0x0B: return "bass guitar";
        case 0x0F: return "keyboard (Rock Band)";
        case 0x13: return "arcade pad";
        case 0x17: return "turntable";
        case 0x19: return "pro guitar";
        default: return "unknown";
    }
}

std::array<uint8_t, 12> xinputWirelessPresenceQuery() {
    return {0x08, 0x00, 0x0F, 0xC0, 0, 0, 0, 0, 0, 0, 0, 0};
}

size_t usbPadRumblePacket(UsbPadProtocol protocol, uint8_t large, uint8_t small, uint8_t& sequence, uint8_t* out,
                          size_t capacity, bool wireless) {
    auto put = [&](std::initializer_list<uint8_t> bytes) -> size_t {
        if (bytes.size() > capacity) return 0;
        std::copy(bytes.begin(), bytes.end(), out);
        return bytes.size();
    };
    switch (protocol) {
        case UsbPadProtocol::XInput:  // Xbox 360: 00 08 00 <large> <small> 00 00 00; its receiver, 12 bytes
            if (wireless) return put({0x00, 0x01, 0x0F, 0xC0, 0x00, large, small, 0x00, 0x00, 0x00, 0x00, 0x00});
            return put({0x00, 0x08, 0x00, large, small, 0x00, 0x00, 0x00});
        case UsbPadProtocol::Gip: {
            // Xbox One / Series: command 09, all four motors (mask 0x0F; the triggers' own at
            // 0), strengths 0..100, on for 0xFF x 10 ms with no pause, repeated 0xFF times: it
            // lasts until the next packet, so the app's own time-out is what ends it.
            if (++sequence == 0) sequence = 1;
            const auto pct = [](uint8_t v) { return static_cast<uint8_t>((v * 100 + 127) / 255); };
            return put({0x09, 0x00, sequence, 0x09, 0x00, 0x0F, 0x00, 0x00, pct(large), pct(small), 0xFF, 0x00, 0xFF});
        }
        case UsbPadProtocol::DualShock4: {  // output report 05: flags (motors), then small, large
            if (capacity < 32) return 0;
            std::fill(out, out + 32, uint8_t{0});
            out[0] = 0x05;
            out[1] = 0x01;
            out[4] = small;
            out[5] = large;
            return 32;
        }
        default:
            return 0;
    }
}

void setUsbPadRumble(int player, uint8_t large, uint8_t small) {
    if (player < 0 || player >= kMaxPads) return;
    int slot = -1;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int s = 0; s < kMaxUsbPads; ++s) {
            if (g_slots[s].attached && g_slots[s].player == player) {
                slot = s;
                break;
            }
        }
    }
    if (slot >= 0) {
        setUsbPadSlotRumble(slot, large, small);
    }
}

void __attribute__((weak)) usbPadsStart() {}
void __attribute__((weak)) usbPadsStop() {}
void __attribute__((weak)) setUsbPadSlotRumble(int /*slot*/, uint8_t /*large*/, uint8_t /*small*/) {}

}  // namespace xc::input
