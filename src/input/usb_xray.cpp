// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstddef>
#include <cstdint>
#include "input/usb_xray.h"

#include "input/pad_usb.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace xc::input {

namespace {
std::mutex g_noticesMutex;
std::vector<std::string> g_notices;

const char* transferName(uint8_t attributes) {
    switch (attributes & 0x03) {
        case 0: return "control";
        case 1: return "isochronous";
        case 2: return "bulk";
        default: return "interrupt";
    }
}
}  // namespace

std::vector<std::string> usbXrayHexLines(const uint8_t* data, size_t len, size_t perLine) {
    std::vector<std::string> lines;
    for (size_t off = 0; data && off < len; off += perLine) {
        char head[16];
        std::snprintf(head, sizeof head, "+%03zu:", off);
        std::string s = head;
        for (size_t i = off; i < len && i < off + perLine; ++i) {
            char b[4];
            std::snprintf(b, sizeof b, " %02x", data[i]);
            s += b;
        }
        lines.push_back(std::move(s));
    }
    return lines;
}

void usbXrayDescribe(const UsbXrayDevice& d) {
    XC_LOGI("usb x-ray: %04x:%04x release %x.%02x, class %02x/%02x/%02x, maker \"%s\", product \"%s\", at %s: %s", d.vid,
            d.pid, d.release >> 8, d.release & 0xFF, d.deviceClass, d.deviceSubclass, d.deviceProtocol,
            d.manufacturer.c_str(), d.product.c_str(), d.where.c_str(), d.why.c_str());
    // Its interfaces and endpoints, read off the configuration.
    for (size_t off = 0; d.config && off + 2 <= d.configLen; off += d.config[off]) {
        const size_t l = d.config[off];
        if (l < 2 || off + l > d.configLen) break;
        const uint8_t* p = d.config + off;
        if (p[1] == 0x04 && l >= 9) {
            XC_LOGI("usb x-ray:   interface %u alt %u: class %02x/%02x/%02x, %u endpoint(s)", p[2], p[3], p[5], p[6], p[7], p[4]);
        } else if (p[1] == 0x05 && l >= 7) {
            XC_LOGI("usb x-ray:     endpoint 0x%02x %s %s, %u bytes, interval %u", p[2], (p[2] & 0x80) ? "in" : "out",
                    transferName(p[3]), p[4] | p[5] << 8, p[6]);
        } else if (p[1] == 0x21 && l >= 9) {
            XC_LOGI("usb x-ray:     HID %x.%02x, report descriptor %u bytes", p[3], p[2], p[7] | p[8] << 8);
        }
    }
    XC_LOGI("usb x-ray: configuration, %zu bytes:", d.configLen);
    for (const std::string& line : usbXrayHexLines(d.config, d.configLen)) XC_LOGI("usb x-ray:   %s", line.c_str());
}

void usbXrayReportDescriptor(const std::string& where, int iface, const uint8_t* desc, size_t len) {
    XC_LOGI("usb x-ray: %s interface %d HID report descriptor, %zu bytes:", where.c_str(), iface, len);
    for (const std::string& line : usbXrayHexLines(desc, len)) XC_LOGI("usb x-ray:   %s", line.c_str());
}

bool usbXrayWorthCapturing(const uint8_t* config, size_t configLen, const uint8_t* reportDesc, size_t reportLen) {
    bool hid = false, vendor = false;
    for (size_t off = 0; config && off + 2 <= configLen; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > configLen) break;
        if (config[off + 1] != 0x04 || l < 9) continue;
        const uint8_t cls = config[off + 5], sub = config[off + 6], proto = config[off + 7];
        if (isBootKeyboardOrMouse(cls, sub, proto)) return false;
        hid = hid || cls == 0x03;
        vendor = vendor || cls == 0xFF;
    }
    if (reportDesc && reportLen) {
        if (isKeyboardOrMouseReportDescriptor(reportDesc, reportLen)) return false;
        if (hid) return isGamepadReportDescriptor(reportDesc, reportLen);
    } else if (hid) {
        return false;  // a HID device whose descriptor was not read: it could be a keyboard
    }
    return vendor;
}

void UsbXrayCapture::start(const std::string& where, const std::string& name, uint64_t nowMs, bool askPresses) {
    where_ = where;
    last_.clear();
    startMs_ = nowMs;
    reports_ = changes_ = 0;
    active_ = true;
    XC_LOGI("usb x-ray: %s: recording its reports for %llu s: press each button, move each stick, trigger and "
            "pedal once, slowly",
            where_.c_str(), static_cast<unsigned long long>(kMaxMs / 1000));
    if (!askPresses) return;
    std::lock_guard<std::mutex> lock(g_noticesMutex);
    g_notices.push_back(name.empty() ? where : name);
}

void UsbXrayCapture::feed(const uint8_t* data, size_t len, uint64_t nowMs) {
    if (!active_ || !data || !len) return;
    const double at = static_cast<double>(nowMs - startMs_) / 1000.0;
    if (nowMs - startMs_ > kMaxMs || changes_ >= kMaxReports) {
        active_ = false;
        XC_LOGI("usb x-ray: %s: recording over: %d reports, %d with changes logged", where_.c_str(), reports_, changes_);
        return;
    }
    ++reports_;
    if (last_.size() != len) {  // the first (or another length): in full
        XC_LOGI("usb x-ray: %s: report at +%.3f s, %zu bytes:", where_.c_str(), at, len);
        for (const std::string& line : usbXrayHexLines(data, len)) XC_LOGI("usb x-ray:   %s", line.c_str());
        last_.assign(data, data + len);
        raw_ = last_;
        flips_.assign(len, 0);
        lastLineMs_ = nowMs;
        ++changes_;
        return;
    }
    // The bytes that moved since the last line. A byte that changes often (a stick, a sensor, a
    // counter) counts only once it moved far (16) and at most every 100 ms, so the presses are not
    // lost among its noise; any other (buttons, a hat) at once.
    std::string diff;
    int moved = 0;
    bool quiet = false;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] != raw_[i] && flips_[i] < 0xFFFF) ++flips_[i];
        raw_[i] = data[i];
    }
    const bool analogDue = nowMs - lastLineMs_ >= 100;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == last_[i]) continue;
        const bool analog = flips_[i] > 8;
        if (analog && (!analogDue || std::abs(static_cast<int>(data[i]) - static_cast<int>(last_[i])) < 16)) continue;
        quiet = quiet || !analog;
        char b[24];
        std::snprintf(b, sizeof b, " [%zu]=%02x>%02x%s", i, last_[i], data[i], analog ? "~" : "");
        if (moved++ < 24) diff += b;
        last_[i] = data[i];
    }
    if (!moved) return;
    ++changes_;
    lastLineMs_ = nowMs;
    XC_LOGI("usb x-ray: %s: +%.3f s:%s%s", where_.c_str(), at, diff.c_str(), moved > 24 ? " ..." : "");
    (void)quiet;
}

std::vector<std::string> takeUsbXrayNotices() {
    std::lock_guard<std::mutex> lock(g_noticesMutex);
    std::vector<std::string> out;
    out.swap(g_notices);
    return out;
}

}  // namespace xc::input
