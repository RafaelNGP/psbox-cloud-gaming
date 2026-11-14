// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The "X-ray" of a USB controller the app does not know, or knows only by a guess (a generic HID
// layout, an instrument not yet seen on a console): everything needed to add it, in the log, so
// that an issue with the log is enough. Its identity and names, its descriptors (the HID report
// descriptor included), then the reports it sends while the player presses each button (a notice
// asks for that): the first in full, then each change as the bytes that moved. Every line starts
// with "usb x-ray". Keyboards and mice are never captured: what one types stays out of the log.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xc::input {

// What a backend knows of a device as it finds it.
struct UsbXrayDevice {
    std::string where;  // the backend's name for it (/dev/ugen1.3, bus 1 address 3)
    uint16_t vid = 0, pid = 0, release = 0;
    uint8_t deviceClass = 0, deviceSubclass = 0, deviceProtocol = 0;
    std::string manufacturer, product;  // its string descriptors (never the serial number)
    const uint8_t* config = nullptr;    // its whole configuration descriptor
    size_t configLen = 0;
    std::string why;  // "not known", "a generic HID pad, its layout guessed"...
};

// Logs the device: identity, names, interfaces and endpoints, the configuration in full.
void usbXrayDescribe(const UsbXrayDevice& d);
// Logs an interface's HID report descriptor (what its reports hold), in full.
void usbXrayReportDescriptor(const std::string& where, int iface, const uint8_t* desc, size_t len);
// Whether a device is a game controller worth capturing: an interface of the HID class whose
// report descriptor says joystick or game pad, or of a vendor's own class (FF, like Xbox pads),
// and none of it a keyboard or a mouse. `reportDesc` may be null (not read).
bool usbXrayWorthCapturing(const uint8_t* config, size_t configLen, const uint8_t* reportDesc, size_t reportLen);

// The reports one endpoint sends, for kMaxReports changes or kMaxMs: the first in full, then each
// one that differs as "[byte]=old>new" (an analog byte, one that changes often, marked "~" and
// only once it moved far). The capture is told to the app once (takeUsbXrayNotices),
// which asks on the screen for each button to be pressed.
class UsbXrayCapture {
public:
    static constexpr int kMaxReports = 300;
    static constexpr uint64_t kMaxMs = 120000;
    // `askPresses`: the app's notice asks for each button (a device not known); a device known
    // but not yet confirmed is recorded as it is played.
    void start(const std::string& where, const std::string& name, uint64_t nowMs, bool askPresses = true);
    bool active() const { return active_; }
    void feed(const uint8_t* data, size_t len, uint64_t nowMs);

private:
    std::string where_;
    std::vector<uint8_t> last_, raw_;  // as last logged, as last received
    std::vector<uint16_t> flips_;      // how often each byte changed: often, an analog one (marked ~)
    uint64_t startMs_ = 0, lastLineMs_ = 0;
    int reports_ = 0, changes_ = 0;
    bool active_ = false;
};

// The devices being captured since the last call (their names), for the app's notice.
std::vector<std::string> takeUsbXrayNotices();

// Hex of `len` bytes, `perLine` to a line, each line "  +offset: .." (for the log).
std::vector<std::string> usbXrayHexLines(const uint8_t* data, size_t len, size_t perLine = 32);

}  // namespace xc::input
