// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Shared USB controller report processing, descriptor inspection, and diagnostics helpers
// used by console USB pad backends (pad_usb_ps5.cpp, pad_usb_ps4.cpp).
// Kept free of platform SDK headers so it stays inside xcloud_core.
#pragma once

#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include "input/usb_xray.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace xc::input {

// Thread-safe player slot allocator for USB pads (up to kMaxUsbPads concurrent slots).
struct UsbPadSlotPool {
    std::array<std::atomic<bool>, kMaxUsbPads> taken{};

    int take();
    void release(int slot);
};

// Per-link input/diagnostic state shared by PS5 and PS4 USB pad backends.
struct UsbPadLinkReportState {
    ControllerState state;
    int shown = 0;
    int changesLogged = 0;
    int sticksLogged = 0;
    float loggedRightX = 0.0f;
    float loggedRightY = 0.0f;
    unsigned buttons = 0;
    std::chrono::steady_clock::time_point lastOther{};
    UsbXrayCapture xray;
};

// Context passed to usbPadHandleReport() for a single incoming interrupt IN packet.
struct UsbPadReportContext {
    std::string_view where;
    int channel = 0;
    UsbPadProtocol protocol = UsbPadProtocol::None;
    bool wireless = false;
    bool xrayOnly = false;
    Ps3Instrument ps3 = Ps3Instrument::None;
    const GenericHidContext* hid = nullptr;
    bool useGenericHidFallback = false;
    uint64_t nowMs = 0;

    int* subtype = nullptr;
    UsbPadLinkReportState* link = nullptr;

    std::function<int()> getSlot;
    std::function<void(const uint8_t*, size_t)> sendAck;
    std::function<void()> onAttach;
    std::function<void()> onDetach;
    std::function<void(bool)> onPresent;  // optional (PS4 tracks receiver channel presence)
};

// Format up to 32 bytes as lowercase space-separated hex for diagnostic logs.
std::string usbPadHex(const uint8_t* p, int n);

// Pack button/trigger states into a diagnostic bitmask and human-readable names.
unsigned usbPadButtonBits(const ControllerState& s);
std::string usbPadButtonNames(unsigned bits);

// Human-readable name for a device linked to an Xbox 360 wireless receiver.
std::string usbPadReceiverDeviceName(int subtype);

// Controller family helpers shared between PS5 and PS4 USB pad backends.
bool isDualShock4VidPid(uint16_t vid, uint16_t pid);
bool isXbox360WirelessReceiverVidPid(uint16_t vid, uint16_t pid);
bool isXboxGipVidPid(uint16_t vid, uint16_t pid);
bool isSwitchProVidPid(uint16_t vid, uint16_t pid);
bool isKnown8BitDoDInputVidPid(uint16_t vid, uint16_t pid);

// Protocol-specific USB interrupt OUT rumble packet builders.
std::array<uint8_t, 32> buildDs4UsbRumblePacket(uint8_t leftStrong, uint8_t rightWeak,
                                                uint8_t ledR = 0, uint8_t ledG = 0, uint8_t ledB = 0);
std::array<uint8_t, 8> buildXInputRumblePacket(uint8_t leftStrong, uint8_t rightWeak);
std::array<uint8_t, 12> buildXbox360WirelessRumblePacket(uint8_t leftStrong, uint8_t rightWeak);
std::array<uint8_t, 13> buildGipRumblePacket(uint8_t leftStrong, uint8_t rightWeak, uint8_t seq = 1);

// First HID (non-boot keyboard/mouse) or vendor-class interface with an interrupt IN endpoint,
// and its interrupt OUT endpoint if present, for unknown-controller X-ray capture.
UsbPadInterface usbPadXrayInterface(const uint8_t* config, size_t configLen);

// Walk HID descriptors in a raw USB configuration descriptor, fetch each non-boot interface's
// HID report descriptor via `readReportDesc(iface, outBuf, wantBytes) -> bytesRead (<=0 on error)`,
// log it via usbXrayReportDescriptor(), and optionally capture the first gamepad descriptor.
void usbPadXrayReadHidReports(
    const std::string& where, const uint8_t* config, size_t configLen,
    const std::function<int(uint8_t iface, uint8_t* buf, uint16_t want)>& readReportDesc,
    std::vector<uint8_t>* outGamepadReport = nullptr);

// Process one raw USB interrupt IN report: X-ray capture, GIP ACKs, Xbox 360 wireless receiver
// link/unlink/capabilities/subtype messages, report parsing, slot publication, and logging.
void usbPadHandleReport(const UsbPadReportContext& ctx, const uint8_t* buf, int n);

}  // namespace xc::input
