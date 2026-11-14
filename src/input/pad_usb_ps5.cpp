// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB pads on the PS5 (pad_usb.h). The console has no driver for an Xbox
// or XInput pad: the FreeBSD kernel leaves it to ugen, whose control node
// (/dev/ugenB.A) is open to every process. Each pad gets a thread that
// keeps one interrupt IN transfer queued through the USB_FS ioctls, as
// OmniPad CE does (usb_hotplug.c, github.com/jpemendonca/OmniPad-CE,
// GPL-3.0); the wake-up packets each kind of pad wants come from there too.
// A thread looks for pads plugged in and unplugged every 1.5 s.
#include <cstdint>
#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include "input/usb_xray.h"
#include "util/log.h"

#include <dev/usb/usb_ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xc::input {

namespace {

constexpr int kUsbErrNormal = 0;      // USB_ERR_NORMAL_COMPLETION (dev/usb/usbdi.h)
constexpr int kUsbErrShortXfer = 21;  // USB_ERR_SHORT_XFER
constexpr uint16_t kShortXferOk = 0x0004;  // USB_SHORT_XFER_OK (usbdi.h): a reply shorter than asked is fine
constexpr uint16_t kMediaTekVid = 0x0e8d;  // the console's own Bluetooth radio: never touched
// One pad on a USB device: a wired pad is the device's only one; an Xbox 360 wireless receiver
// has one per device it can link (up to four), each its own player once something links to it.
// Its IN transfer sits at endpoint index 2k, its OUT at 2k + 1, all on the device's one fd.
struct Link {
    UsbPadInterface iface;
    int inIndex = 0, outIndex = 1;
    bool opened = false, hasOut = false;
    uint8_t rx[64]{}, tx[64]{};
    void* frames[2]{};
    uint32_t lengths[2]{};
    bool inPending = false, inDone = false, outDone = false, clearStall = false;
    int slot = -1;          // its player slot; -1: nothing linked yet (a receiver's)
    int subtype = -1;       // XInput device subtype, once a receiver's link report told it
    // The motors asked for (large << 8 | small) by any thread; the device's thread sends them.
    std::atomic<uint32_t> rumble{0};
    std::atomic<bool> rumbleAsked{false};
    uint8_t rumbleSequence = 0;
    ControllerState state;
    int shown = 0, changesLogged = 0, lightPlayer = -1, sticksLogged = 0;
    float loggedRightX = 0, loggedRightY = 0;
    unsigned buttons = 0;
    std::chrono::steady_clock::time_point lastOther{};
    UsbXrayCapture xray;  // its reports in the log, for a while (usb_xray.h)
};

struct Device {
    std::string path;
    int fd = -1;
    uint16_t vid = 0, pid = 0;
    std::string name;
    bool receiver = false;  // an Xbox 360 wireless receiver: not a pad itself
    bool xrayOnly = false;  // a controller not known: opened only to log its reports (usb_xray.h)
    Ps3Instrument ps3 = Ps3Instrument::None;  // a PS3 instrument's dongle: how its reports read
    uint64_t keepAliveAt = 0;                 // ...Guitar Hero Live's: when to send its keep-alive
    std::vector<std::unique_ptr<Link>> links;
    usb_fs_endpoint eps[8]{};
    std::atomic<bool> stop{false}, dead{false}, failed{false};
    // A generic HID pad read as its own report descriptor says (hidReady); without one, the
    // usual layout is guessed.
    GenericHidContext hid{};
    bool hidReady = false;
    std::thread thread;
};

// The player slots taken (any thread; a receiver's pad takes one as it links).
std::array<std::atomic<bool>, kMaxUsbPads> g_slotTaken{};

int takeSlot() {
    for (int s = 0; s < kMaxUsbPads; ++s) {
        bool expected = false;
        if (g_slotTaken[s].compare_exchange_strong(expected, true)) return s;
    }
    return -1;
}

void releaseSlot(int slot) {
    if (slot >= 0 && slot < kMaxUsbPads) g_slotTaken[slot] = false;
}

std::mutex g_mutex;  // g_devices, g_ignored
std::vector<std::unique_ptr<Device>> g_devices;
std::set<std::pair<std::string, ino_t>> g_ignored;  // not a pad, or it failed: until it is plugged again
std::thread g_scanner;
std::atomic<bool> g_running{false};

const char* protocolName(UsbPadProtocol p) {
    return usbPadProtocolName(p);
}

std::string hex(const uint8_t* p, int n) {
    std::string s;
    char b[4];
    for (int i = 0; i < n && i < 32; ++i) std::snprintf(b, sizeof b, "%02x ", p[i]), s += b;
    return s;
}

int controlRequest(int fd, uint8_t type, uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t len) {
    usb_ctl_request r{};
    r.ucr_data = data;
    r.ucr_flags = kShortXferOk;
    r.ucr_request.bmRequestType = type;
    r.ucr_request.bRequest = request;
    USETW(r.ucr_request.wValue, value);
    USETW(r.ucr_request.wIndex, index);
    USETW(r.ucr_request.wLength, len);
    return ioctl(fd, USB_DO_REQUEST, &r) == 0 ? 0 : errno;
}

// The product's name from its string descriptor (UTF-16, ASCII kept).
std::string productName(int fd, uint8_t index) {
    if (!index) return {};
    uint8_t raw[256] = {};
    usb_gen_descriptor gd{};
    gd.ugd_data = raw;
    gd.ugd_maxlen = sizeof raw;
    gd.ugd_string_index = index;
    gd.ugd_lang_id = 0x0409;
    if (ioctl(fd, USB_GET_STRING_DESC, &gd) != 0) return {};
    std::string s;
    const size_t n = std::min<size_t>({gd.ugd_actlen, raw[0], sizeof raw});
    for (size_t i = 2; i + 1 < n; i += 2)
        if (raw[i + 1] == 0 && raw[i] >= 0x20 && raw[i] < 0x7f) s += static_cast<char>(raw[i]);
    return s;
}

// A device that is no pad of ours: its interfaces in the log (class,
// subclass, protocol and endpoints) and, for a HID one that is no boot
// keyboard / mouse, its report descriptor (a read-only request), to tell
// what another pad needs.
void logInterfaces(int fd, const std::string& path, const uint8_t* d, size_t len) {
    std::string line;
    for (size_t off = 0; off + 2 <= len; off += d[off]) {
        const size_t l = d[off];
        if (l < 2 || off + l > len) break;
        char b[64];
        if (d[off + 1] == 0x04 && l >= 9) {
            std::snprintf(b, sizeof b, " | if %u alt %u: %02x/%02x/%02x", d[off + 2], d[off + 3], d[off + 5], d[off + 6],
                          d[off + 7]);
            line += b;
        } else if (d[off + 1] == 0x05 && l >= 7) {
            std::snprintf(b, sizeof b, " ep %02x/%u", d[off + 2], d[off + 3] & 3);
            line += b;
        }
    }
    XC_LOGI("usb pad: %s: interfaces%s", path.c_str(), line.c_str());
    uint8_t iface = 0, sub = 0xff;
    bool hid = false;
    for (size_t off = 0; off + 2 <= len; off += d[off]) {
        const size_t l = d[off];
        if (l < 2 || off + l > len) break;
        if (d[off + 1] == 0x04 && l >= 9) iface = d[off + 2], hid = d[off + 5] == 0x03, sub = d[off + 6];
        if (d[off + 1] != 0x21 || l < 9 || !hid || sub != 0x00) continue;  // a HID descriptor, no boot device
        const uint16_t size = static_cast<uint16_t>(d[off + 7] | (d[off + 8] << 8));
        uint8_t report[512] = {};
        const uint16_t want = std::min<uint16_t>(size, sizeof report);
        int err = controlRequest(fd, 0x81, 0x06, 0x2200, iface, report, want);
        if (err) {
            XC_LOGI("usb pad: %s: if %u report descriptor: errno %d", path.c_str(), iface, err);
            continue;
        }
        for (int at = 0; at < want; at += 24)
            XC_LOGI("usb pad: %s: if %u report descriptor @%d: %s", path.c_str(), iface, at,
                    hex(report + at, std::min(24, want - at)).c_str());
    }
}

bool setupTransfers(Device& d) {
    int config = 0;
    if (ioctl(d.fd, USB_GET_CONFIG, &config) == 0 && (config & 0xff) == 0xff) {
        config = 0;
        if (ioctl(d.fd, USB_SET_CONFIG, &config) != 0) XC_LOGW("usb pad: %s: set config: errno %d", d.path.c_str(), errno);
    }
    usb_fs_init init{};
    init.pEndpoints = d.eps;
    init.ep_index_max = static_cast<uint8_t>(2 * d.links.size());
    if (ioctl(d.fd, USB_FS_INIT, &init) != 0) {
        XC_LOGW("usb pad: %s: USB_FS_INIT: errno %d (%s)", d.path.c_str(), errno, std::strerror(errno));
        return false;
    }
    int opened = 0;
    for (size_t k = 0; k < d.links.size(); ++k) {
        Link& l = *d.links[k];
        l.inIndex = static_cast<int>(2 * k), l.outIndex = static_cast<int>(2 * k + 1);
        usb_fs_open op{};
        op.max_bufsize = sizeof l.rx;
        op.max_frames = 1;
        op.ep_index = static_cast<uint8_t>(l.inIndex);
        op.ep_no = l.iface.epIn;
        if (ioctl(d.fd, USB_FS_OPEN, &op) != 0) {
            int err = errno;
            char driver[64] = {};
            usb_gen_descriptor gd{};
            gd.ugd_data = driver;
            gd.ugd_maxlen = sizeof driver - 1;
            gd.ugd_iface_index = l.iface.number;
            bool claimed = ioctl(d.fd, USB_GET_IFACE_DRIVER, &gd) == 0;
            XC_LOGW("usb pad: %s: opening IN 0x%02x: errno %d (%s)%s%s", d.path.c_str(), l.iface.epIn, err,
                    std::strerror(err), claimed ? ", interface held by the driver " : "", claimed ? driver : "");
            continue;
        }
        if (l.iface.epOut) {
            op = {};
            op.max_bufsize = sizeof l.tx;
            op.max_frames = 1;
            op.ep_index = static_cast<uint8_t>(l.outIndex);
            op.ep_no = l.iface.epOut;
            l.hasOut = ioctl(d.fd, USB_FS_OPEN, &op) == 0;
            if (!l.hasOut) XC_LOGW("usb pad: %s: opening OUT 0x%02x: errno %d", d.path.c_str(), l.iface.epOut, errno);
        }
        for (int i : {0, 1}) {
            const int index = i == 0 ? l.inIndex : l.outIndex;
            l.frames[i] = i == 0 ? l.rx : l.tx;
            l.lengths[i] = sizeof l.rx;
            d.eps[index].ppBuffer = &l.frames[i];
            d.eps[index].pLength = &l.lengths[i];
            d.eps[index].nFrames = 1;
            d.eps[index].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
        }
        l.opened = true;
        ++opened;
    }
    return opened > 0;
}

// The link a finished transfer belongs to, marked done.
void noteComplete(Device& d, int epIndex) {
    for (auto& l : d.links) {
        if (epIndex == l->inIndex) l->inDone = true;
        else if (epIndex == l->outIndex) l->outDone = true;
    }
}

// Each opened link keeps one IN transfer queued (no kernel timeout): a pad that reports only on
// change (the 8BitDo dongle) would lose a press landing between a timed-out transfer and the next.
bool queueIn(Device& d, Link& l) {
    if (!l.opened || l.inPending) return true;
    l.lengths[0] = sizeof l.rx;
    usb_fs_endpoint& ep = d.eps[l.inIndex];
    ep.timeout = 0;
    ep.flags = USB_FS_FLAG_SINGLE_SHORT_OK | (l.clearStall ? USB_FS_FLAG_CLEAR_STALL : 0);
    l.clearStall = false;
    usb_fs_start start{};
    start.ep_index = static_cast<uint8_t>(l.inIndex);
    if (ioctl(d.fd, USB_FS_START, &start) != 0 && errno != EBUSY) return false;
    l.inPending = true;
    return true;
}

// Waits up to `waitMs` for any IN transfer to finish; false on an error.
bool waitIn(Device& d, int waitMs) {
    for (int spins = waitMs / 4 + 1; spins > 0; --spins) {
        for (;;) {
            usb_fs_complete complete{};
            if (ioctl(d.fd, USB_FS_COMPLETE, &complete) != 0) {
                if (errno != EBUSY && errno != EAGAIN && errno != ETIMEDOUT) return false;
                break;
            }
            noteComplete(d, complete.ep_index);
        }
        for (auto& l : d.links)
            if (l->inDone) return true;
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(d.fd, &rset);
        timeval wait{0, 4000};
        select(d.fd + 1, &rset, nullptr, nullptr, &wait);
    }
    return true;
}

// A finished IN transfer's report: its size, -1 when it ended in an error (the stall cleared on
// the next one).
int takeIn(Device& d, Link& l, uint8_t* out) {
    l.inDone = l.inPending = false;
    const usb_fs_endpoint& ep = d.eps[l.inIndex];
    if (ep.status != kUsbErrNormal && ep.status != kUsbErrShortXfer) {
        l.clearStall = true;
        return -1;
    }
    const int n = static_cast<int>(std::min<uint32_t>(l.lengths[0], sizeof l.rx));
    std::memcpy(out, l.rx, static_cast<size_t>(n));
    return n;
}

// One packet out a link's OUT endpoint; false when it was not sent.
bool send(Device& d, Link& l, const uint8_t* data, size_t len, int timeoutMs = 80) {
    if (!l.hasOut) return false;
    usb_fs_endpoint& ep = d.eps[l.outIndex];
    len = std::min(len, sizeof l.tx);
    std::memcpy(l.tx, data, len);
    l.lengths[1] = static_cast<uint32_t>(len);
    ep.timeout = static_cast<uint16_t>(timeoutMs);
    l.outDone = false;
    usb_fs_start start{};
    start.ep_index = static_cast<uint8_t>(l.outIndex);
    if (ioctl(d.fd, USB_FS_START, &start) != 0 && errno != EBUSY) return false;
    for (int spins = timeoutMs / 4 + 1; spins > 0; --spins) {
        usb_fs_complete complete{};
        if (ioctl(d.fd, USB_FS_COMPLETE, &complete) == 0) {
            noteComplete(d, complete.ep_index);  // a queued IN transfer: the read loop takes it
            if (l.outDone) return ep.status == kUsbErrNormal || ep.status == kUsbErrShortXfer;
            continue;
        }
        if (errno != EBUSY && errno != EAGAIN) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    return false;
}

// What each kind of pad waits for before it reports.
void wakeUp(Device& d, Link& l) {
    if (l.iface.protocol == UsbPadProtocol::Gip) {
        // Power on, the "S" init and the Xbox button's light.
        static const uint8_t powerOn[] = {0x05, 0x20, 0x00, 0x01, 0x00};
        static const uint8_t sInit[] = {0x05, 0x20, 0x00, 0x0f, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x55, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t ledOn[] = {0x0a, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14};
        bool a = send(d, l, powerOn, sizeof powerOn);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bool b = send(d, l, sInit, sizeof sInit);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bool c = send(d, l, ledOn, sizeof ledOn);
        XC_LOGI("usb pad %d: GIP power on %s, init %s, light %s", l.slot, a ? "ok" : "failed", b ? "ok" : "failed",
                c ? "ok" : "failed");
    } else if (l.iface.wireless) {
        // The Xbox 360 receiver: ask whether something is linked on this channel (a presence
        // message comes back).
        const auto query = xinputWirelessPresenceQuery();
        send(d, l, query.data(), query.size());
    } else if (l.iface.protocol == UsbPadProtocol::XInput) {
        // The player 1 light, which XInput hosts set as a pad connects: the
        // 8BitDo Ultimate dongle stays silent without it and drops off USB
        // after ~10 s.
        static const uint8_t led[] = {0x01, 0x03, 0x02};
        bool ok = send(d, l, led, sizeof led);
        XC_LOGI("usb pad %d: XInput light %s", l.slot, ok ? "sent" : "not sent");
    }
}

// The motors, when another thread asked: from the device's own thread, as every transfer is.
void sendRumble(Device& d, Link& l, uint8_t large, uint8_t small) {
    uint8_t packet[32];
    const size_t n =
        usbPadRumblePacket(l.iface.protocol, large, small, l.rumbleSequence, packet, sizeof packet, l.iface.wireless);
    if (!n) return;
    const bool ok = send(d, l, packet, n, 40);
    static std::atomic<int> logged{0};
    if (logged < 6 && ++logged) XC_LOGI("usb pad %d: motors %u / %u %s", l.slot, large, small, ok ? "sent" : "not sent");
}

unsigned buttonBits(const ControllerState& s) {
    const bool b[] = {s.btnA, s.btnB, s.btnX, s.btnY, s.dpadUp, s.dpadDown, s.dpadLeft, s.dpadRight,
                      s.btnL1, s.btnR1, s.btnL3, s.btnR3, s.btnOptions, s.btnTouchpad, s.btnNexus,
                      s.triggerL2 > 0.5f, s.triggerR2 > 0.5f};
    unsigned bits = 0;
    for (size_t i = 0; i < sizeof b; ++i) bits |= b[i] ? 1u << i : 0;
    return bits;
}

std::string buttonNames(unsigned bits) {
    static const char* names[] = {"A", "B", "X", "Y", "Up", "Down", "Left", "Right", "LB",
                                  "RB", "LS", "RS", "Menu", "View", "Xbox", "LT", "RT"};
    std::string s;
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i)
        if (bits & (1u << i)) s += s.empty() ? names[i] : std::string(" ") + names[i];
    return s.empty() ? "none" : s;
}

// The name a receiver's device goes by, from what it says it is.
std::string receiverDeviceName(int subtype) {
    switch (usbPadKindForSubtype(subtype)) {
        case UsbPadKind::Guitar: return "Xbox 360 Guitar";
        case UsbPadKind::Drums: return "Xbox 360 Drum Kit";
        case UsbPadKind::Other: return std::string("Xbox 360 ") + xinputSubtypeName(subtype);
        default: return subtype < 0 ? "Xbox 360 Wireless" : "Xbox 360 Controller";
    }
}

// A receiver's channel got something linked to it (or reporting): it becomes a player.
void linkAttach(Device& d, Link& l, int channel) {
    if (l.slot >= 0) return;
    l.slot = takeSlot();
    if (l.slot < 0) {
        XC_LOGW("usb pad: %s channel %d: no free player for it", d.path.c_str(), channel);
        return;
    }
    l.lightPlayer = -1;
    l.state = ControllerState{};
    l.shown = l.changesLogged = 0;
    usbPadAttached(l.slot, receiverDeviceName(l.subtype), usbPadKindForSubtype(l.subtype));
    if (l.subtype < 0) {
        // Linked before the app started: its link report is not sent again. Ask for its
        // capabilities (00 00 02 80, answered with a 00 05 report: vendor, product, version).
        static const uint8_t ask[12] = {0x00, 0x00, 0x02, 0x80};
        send(d, l, ask, sizeof ask, 40);
    }
}

void linkDetach(Link& l) {
    if (l.slot < 0) return;
    usbPadDetached(l.slot);
    releaseSlot(l.slot);
    l.slot = -1;
    l.subtype = -1;
}

uint64_t msNow() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

// The X-ray of a device (usb_xray.h): its identity, names and descriptors (each HID interface's
// report descriptor read); `report` gets the first game controller's, for the caller.
void xrayDescribe(int fd, const std::string& path, const usb_device_descriptor& dd, const uint8_t* config,
                  size_t configLen, const std::string& why, std::vector<uint8_t>* report = nullptr) {
    UsbXrayDevice x;
    x.where = path;
    x.vid = UGETW(dd.idVendor), x.pid = UGETW(dd.idProduct), x.release = UGETW(dd.bcdDevice);
    x.deviceClass = dd.bDeviceClass, x.deviceSubclass = dd.bDeviceSubClass, x.deviceProtocol = dd.bDeviceProtocol;
    x.manufacturer = productName(fd, dd.iManufacturer);
    x.product = productName(fd, dd.iProduct);
    x.config = config, x.configLen = configLen, x.why = why;
    usbXrayDescribe(x);
    uint8_t iface = 0, sub = 0xff;
    bool hid = false;
    for (size_t off = 0; off + 2 <= configLen; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > configLen) break;
        if (config[off + 1] == 0x04 && l >= 9) iface = config[off + 2], hid = config[off + 5] == 0x03, sub = config[off + 6];
        if (config[off + 1] != 0x21 || l < 9 || !hid || sub != 0x00) continue;  // a HID descriptor, no boot device
        uint8_t desc[1024] = {};
        const uint16_t want = std::min<uint16_t>(static_cast<uint16_t>(config[off + 7] | (config[off + 8] << 8)), sizeof desc);
        if (const int err = controlRequest(fd, 0x81, 0x06, 0x2200, iface, desc, want)) {
            XC_LOGI("usb x-ray: %s interface %u report descriptor: errno %d", path.c_str(), iface, err);
            continue;
        }
        usbXrayReportDescriptor(path, iface, desc, want);
        if (report && report->empty() && isGamepadReportDescriptor(desc, want)) report->assign(desc, desc + want);
    }
}

// A controller's interface by its descriptors, for the X-ray only: the first HID (not a boot
// keyboard or mouse) or vendor-class interface with an interrupt IN endpoint, and its OUT if any.
UsbPadInterface xrayInterface(const uint8_t* config, size_t configLen) {
    UsbPadInterface found, cur;
    bool candidate = false;
    for (size_t off = 0; off + 2 <= configLen; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > configLen) break;
        if (config[off + 1] == 0x04 && l >= 9) {
            if (candidate && found.epIn) return found;
            cur = {UsbPadProtocol::GenericHid, config[off + 2], 0, 0, false};
            candidate = (config[off + 5] == 0x03 && !isBootKeyboardOrMouse(config[off + 5], config[off + 6], config[off + 7])) ||
                        config[off + 5] == 0xFF;
            found = cur;
        } else if (config[off + 1] == 0x05 && l >= 7 && candidate && (config[off + 3] & 0x03) == 0x03) {
            uint8_t& ep = (config[off + 2] & 0x80) ? found.epIn : found.epOut;
            if (!ep) ep = config[off + 2];
        }
    }
    return candidate && found.epIn ? found : UsbPadInterface{};
}

// One report off a link.
void handleReport(Device& d, Link& l, int channel, const uint8_t* buf, int n) {
    if (l.xray.active()) l.xray.feed(buf, static_cast<size_t>(n), msNow());
    if (d.xrayOnly) return;
    if (l.iface.protocol == UsbPadProtocol::Gip)
        for (const auto& ack : gipAcksFor(buf, static_cast<size_t>(n))) send(d, l, ack.data(), ack.size(), 40);
    if (l.iface.wireless) {
        // Linked (08 80) or gone (08 00); the link report that follows says what it is.
        if (n >= 2 && buf[0] == 0x08) {
            const bool linked = (buf[1] & 0x80) != 0;
            XC_LOGI("usb pad: %s channel %d: device %s", d.path.c_str(), channel, linked ? "linked" : "gone");
            if (!linked) linkDetach(l);
            return;
        }
        if (n >= 16 && buf[0] == 0x00 && buf[1] == 0x05 && buf[5] == 0x12) {
            XC_LOGI("usb pad: %s channel %d: capabilities: vendor %04x product %04x version %04x: %s", d.path.c_str(),
                    channel, buf[10] | buf[11] << 8, buf[12] | buf[13] << 8, buf[14] | buf[15] << 8, hex(buf, n).c_str());
            // No link report (linked before the app started): an instrument told by its maker.
            if (l.subtype < 0)
                if (const int subtype = xinputSubtypeFromCapabilities(buf, static_cast<size_t>(n)); subtype >= 0) {
                    l.subtype = subtype;
                    XC_LOGI("usb pad: %s channel %d: device is a %s (subtype 0x%02x, from its maker and product)",
                            d.path.c_str(), channel, xinputSubtypeName(subtype), subtype);
                    if (l.slot >= 0) usbPadSetKind(l.slot, usbPadKindForSubtype(subtype));
                }
            return;
        }
        if (const int subtype = xinputWirelessSubtype(buf, static_cast<size_t>(n)); subtype >= 0) {
            l.subtype = subtype;
            XC_LOGI("usb pad: %s channel %d: device is a %s (subtype 0x%02x%s): %s", d.path.c_str(), channel,
                    xinputSubtypeName(subtype), subtype, (buf[25] & 0x80) ? ", motors flag" : "", hex(buf, n).c_str());
            if (l.slot < 0) linkAttach(d, l, channel);
            else usbPadSetKind(l.slot, usbPadKindForSubtype(subtype));
            return;
        }
    }
    ControllerState state = l.state;
    const bool parsed = d.ps3 != Ps3Instrument::None ? parsePs3InstrumentReport(d.ps3, buf, static_cast<size_t>(n), state)
                        : d.hidReady ? parseGenericHidReport(d.hid, buf, static_cast<size_t>(n), state)
                                     : parseUsbPadReport(l.iface.protocol, buf, static_cast<size_t>(n), state);
    // Input from a receiver's channel nobody announced (linked before the app started): a player
    // too, of a kind not known until it links again.
    if (parsed && l.iface.wireless && l.slot < 0) linkAttach(d, l, channel);
    if (l.shown < 6) XC_LOGI("usb pad %d: report %d: %d bytes: %s", l.slot, ++l.shown, n, hex(buf, n).c_str());
    if (!parsed) {
        if (std::chrono::steady_clock::now() - l.lastOther > std::chrono::seconds(5)) {
            l.lastOther = std::chrono::steady_clock::now();
            XC_LOGD("usb pad %d: other report, %d bytes: %s", l.slot, n, hex(buf, n).c_str());
        }
        return;
    }
    l.state = state;
    if (l.slot < 0) return;
    usbPadState(l.slot, state);
    // An instrument's sticks (a guitar's whammy and tilt, a drum kit's pad strengths): their moves,
    // to set what raising the guitar reads.
    const UsbPadKind kind = d.ps3 != Ps3Instrument::None ? ps3InstrumentKind(d.ps3) : usbPadKindForSubtype(l.subtype);
    if (kind != UsbPadKind::Gamepad && l.sticksLogged < 3000 &&
        (std::abs(state.rawRightY - l.loggedRightY) > 0.08f || std::abs(state.rawRightX - l.loggedRightX) > 0.15f)) {
        ++l.sticksLogged;
        l.loggedRightX = state.rawRightX, l.loggedRightY = state.rawRightY;
        XC_LOGI("usb pad %d: %s sticks: left %.2f,%.2f right %.2f,%.2f (whammy x, tilt y)", l.slot, usbPadKindName(kind),
                state.rawLeftX, state.rawLeftY, state.rawRightX, state.rawRightY);
    }
    // The first presses, to check the buttons from the log (an instrument's, more of them).
    const unsigned now = buttonBits(state);
    if (now != l.buttons && l.changesLogged < (kind != UsbPadKind::Gamepad ? 400 : 80)) {
        ++l.changesLogged;
        XC_LOGI("usb pad %d: buttons %s, sticks %.2f,%.2f %.2f,%.2f, triggers %.2f %.2f", l.slot, buttonNames(now).c_str(),
                state.rawLeftX, state.rawLeftY, state.rawRightX, state.rawRightY, state.triggerL2, state.triggerR2);
    }
    l.buttons = now;
}

void readLoop(Device* d) {
    Link& first = *d->links.front();
    // The XInput wake-up through the control endpoint some 2.4G dongles
    // want (OmniPad CE's handshake), before the transfers start.
    if (first.iface.protocol == UsbPadProtocol::XInput && !first.iface.wireless) {
        uint8_t buf[32];
        int r1 = controlRequest(d->fd, 0xc1, 0x01, 0x0100, first.iface.number, buf, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        uint8_t led[] = {0x01, 0x03, 0x02};
        int r2 = controlRequest(d->fd, 0x21, 0x09, 0x0200, first.iface.number, led, sizeof led);
        XC_LOGI("usb pad: %s: XInput wake-up requests: errno %d, %d", d->path.c_str(), r1, r2);
    }
    if (!setupTransfers(*d)) {
        d->failed = true;
        d->dead = true;
        return;
    }
    for (size_t k = 0; k < d->links.size(); ++k) {
        const Link& l = *d->links[k];
        XC_LOGI("usb pad: %s (%04x:%04x, %s%s, channel %zu: IN 0x%02x, OUT 0x%02x%s) on %s", d->name.c_str(), d->vid,
                d->pid, protocolName(l.iface.protocol), d->receiver ? " receiver" : "", k, l.iface.epIn, l.iface.epOut,
                !l.opened ? " not opened" : l.hasOut ? "" : " not opened", d->path.c_str());
    }
    // A wired pad is a player from the start; a receiver's channels once something links to them.
    if (!d->receiver)
        usbPadAttached(first.slot, d->name, d->ps3 != Ps3Instrument::None ? ps3InstrumentKind(d->ps3) : UsbPadKind::Gamepad);
    for (auto& l : d->links)
        if (l->opened) wakeUp(*d, *l);

    uint8_t buf[64];
    int errors = 0;
    while (!d->stop) {
        for (size_t k = 0; k < d->links.size(); ++k) {
            Link& l = *d->links[k];
            if (!l.opened) continue;
            // XInput pads show their player on their four lights.
            if (const int player = usbPadPlayer(l.slot);
                l.iface.protocol == UsbPadProtocol::XInput && player >= 0 && player != l.lightPlayer) {
                if (l.iface.wireless) {
                    const auto led = xinputWirelessLedPacket(player);
                    send(*d, l, led.data(), led.size(), 40);
                } else {
                    const uint8_t led[] = {0x01, 0x03, static_cast<uint8_t>(0x06 + player)};
                    send(*d, l, led, sizeof led, 40);
                }
                l.lightPlayer = player;
            }
            if (l.rumbleAsked.exchange(false)) {
                const uint32_t v = l.rumble;
                sendRumble(*d, l, static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v));
            }
            if (!queueIn(*d, l)) ++errors;
        }
        // Guitar Hero Live's dongle lets held frets go without its keep-alive (every 8 s).
        if (d->ps3 == Ps3Instrument::GhLiveGuitar && msNow() >= d->keepAliveAt) {
            uint8_t keep[sizeof kGhLiveKeepAlive];
            std::memcpy(keep, kGhLiveKeepAlive, sizeof keep);
            const int err = controlRequest(d->fd, 0x21, 0x09, 0x0202, first.iface.number, keep, sizeof keep);
            if (!d->keepAliveAt || err) XC_LOGI("usb pad: %s: Guitar Hero Live keep-alive: errno %d", d->path.c_str(), err);
            d->keepAliveAt = msNow() + kGhLiveKeepAliveMs;
        }
        if (!waitIn(*d, 20)) ++errors;
        for (size_t k = 0; k < d->links.size(); ++k) {
            Link& l = *d->links[k];
            if (!l.inDone) continue;
            const int n = takeIn(*d, l, buf);
            if (n < 0) {
                ++errors;
                continue;
            }
            errors = 0;
            if (n > 0) handleReport(*d, l, static_cast<int>(k), buf, n);
        }
        if (errors >= 50) {
            XC_LOGW("usb pad: %s: %d errors in a row (errno %d): closed", d->path.c_str(), errors, errno);
            break;
        }
        if (errors) std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    for (auto& l : d->links)
        if (l->opened && l->rumble) sendRumble(*d, *l, 0, 0);  // not left turning
    usb_fs_uninit uninit{};
    ioctl(d->fd, USB_FS_UNINIT, &uninit);
    d->dead = true;
}

void closeDevice(Device& d) {
    d.stop = true;
    if (d.thread.joinable()) d.thread.join();
    for (auto& l : d.links) {
        if (l->slot < 0) continue;
        usbPadDetached(l->slot);
        releaseSlot(l->slot);
        l->slot = -1;
    }
    if (d.fd >= 0) close(d.fd);
    d.fd = -1;
}

// A device seen for the first time: a pad gets a slot and its thread;
// anything else is left alone until it is plugged in again.
void probe(const std::string& path, ino_t ino) {
    const auto key = std::make_pair(path, ino);
    int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        XC_LOGW("usb pad: %s: open: errno %d (%s)", path.c_str(), errno, std::strerror(errno));
        g_ignored.insert(key);
        return;
    }
    usb_device_descriptor dd{};
    if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) != 0) {
        XC_LOGW("usb pad: %s: device descriptor: errno %d", path.c_str(), errno);
        close(fd);
        g_ignored.insert(key);
        return;
    }
    const uint16_t vid = UGETW(dd.idVendor), pid = UGETW(dd.idProduct);
    // Explicitly skip DualSense USB (0x054C:0x0CE6) to ensure native PS5 libScePad retains exclusive ownership
    if (vid == 0x054C && pid == 0x0CE6) {
        close(fd);
        g_ignored.insert(key);
        return;
    }
    UsbPadInterface iface;
    GenericHidContext hid{};
    bool hidReady = false;
    uint8_t config[1024] = {};
    size_t configLen = 0;
    // Not the console's radios (class E0, or MediaTek's), not a hub.
    const bool other = vid == kMediaTekVid || dd.bDeviceClass == 0x09 || dd.bDeviceClass == 0xe0;
    const Ps3Instrument ps3 = ps3InstrumentFor(vid, pid);
    if (!other && ps3 != Ps3Instrument::None) {  // a PS3 instrument's dongle: its HID interface
        usb_gen_descriptor gd{};
        gd.ugd_data = config;
        gd.ugd_maxlen = sizeof config;
        if (ioctl(fd, USB_GET_FULL_DESC, &gd) == 0) configLen = std::min<size_t>(gd.ugd_actlen, sizeof config);
        iface = xrayInterface(config, configLen);
        if (iface.epIn) iface.protocol = UsbPadProtocol::Ps3Instrument;
        else XC_LOGW("usb pad: %s: %s without an interrupt IN endpoint", path.c_str(), ps3InstrumentName(ps3));
    } else if (!other) {
        usb_gen_descriptor gd{};
        gd.ugd_data = config;
        gd.ugd_maxlen = sizeof config;
        if (ioctl(fd, USB_GET_FULL_DESC, &gd) == 0) {
            configLen = std::min<size_t>(gd.ugd_actlen, sizeof config);
            iface = probeUsbPadPs5(vid, pid, config, configLen);
            if (iface.protocol == UsbPadProtocol::DualShock4 || iface.protocol == UsbPadProtocol::GenericHid) {
                uint8_t report[512] = {};
                uint8_t curIf = 0xFF;
                uint16_t want = sizeof(report);
                for (size_t off = 0; off + 2 <= configLen; off += config[off]) {
                    const size_t l = config[off];
                    if (l < 2 || off + l > configLen) break;
                    if (config[off + 1] == 0x04 && l >= 9) curIf = config[off + 2];
                    if (config[off + 1] == 0x21 && l >= 9 && curIf == iface.number) {
                        uint16_t sz = static_cast<uint16_t>(config[off + 7] | (config[off + 8] << 8));
                        if (sz > 0) want = std::min<uint16_t>(sz, sizeof(report));
                        break;
                    }
                }
                if (controlRequest(fd, 0x81, 0x06, 0x2200, iface.number, report, want) == 0) {
                    iface = probeUsbPadPs5(vid, pid, config, configLen, report, want);
                    if (iface.protocol == UsbPadProtocol::GenericHid)
                        hidReady = parseHidDescriptor(hid, report, want, vid, pid);
                } else if (!(vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC))) {
                    // Reject unverified generic HID devices so USB keyboards/mice are never claimed
                    iface = UsbPadInterface{};
                }
            }
        }
    }
    bool xrayOnly = false;
    if (iface.protocol == UsbPadProtocol::None) {
        XC_LOGI("usb pad: %s: %04x:%04x (class %02x) is not an Xbox / XInput pad", path.c_str(), vid, pid,
                dd.bDeviceClass);
        // A game controller the app does not know: its X-ray, and its reports logged for a while,
        // so that it can be added from an issue's log. Anything else: its interfaces, as before.
        UsbPadInterface xi = other || !configLen ? UsbPadInterface{} : xrayInterface(config, configLen);
        if (xi.epIn) {
            std::vector<uint8_t> report;
            xrayDescribe(fd, path, dd, config, configLen, "not known", &report);
            if (usbXrayWorthCapturing(config, configLen, report.empty() ? nullptr : report.data(), report.size()))
                iface = xi, xrayOnly = true;
            else
                XC_LOGI("usb x-ray: %s: not a game controller, its reports are not recorded", path.c_str());
        } else if (!other && configLen) {
            logInterfaces(fd, path, config, configLen);
        }
        if (!xrayOnly) {
            close(fd);
            g_ignored.insert(key);
            return;
        }
    }
    auto d = std::make_unique<Device>();
    d->receiver = iface.wireless;
    d->xrayOnly = xrayOnly;
    if (xrayOnly) {  // no player: only its reports, for the log
        d->links.push_back(std::make_unique<Link>());
        d->links.back()->iface = iface;
    } else if (d->receiver) {
        // An Xbox 360 wireless receiver: a channel for each device it can link, no player yet.
        for (const UsbPadInterface& channel : findXInputWirelessInterfaces(config, configLen)) {
            if (d->links.size() >= std::size(d->eps) / 2) break;
            d->links.push_back(std::make_unique<Link>());
            d->links.back()->iface = channel;
        }
        if (d->links.empty()) {  // no descriptor to read them from: the first channel
            d->links.push_back(std::make_unique<Link>());
            d->links.back()->iface = iface;
        }
    } else {
        const int slot = takeSlot();
        if (slot < 0) {
            close(fd);  // tried again on the next scan, once a pad leaves
            return;
        }
        d->links.push_back(std::make_unique<Link>());
        d->links.back()->iface = iface;
        d->links.back()->slot = slot;
    }
    d->path = path;
    d->fd = fd;
    d->vid = vid;
    d->pid = pid;
    d->ps3 = iface.protocol == UsbPadProtocol::Ps3Instrument ? ps3 : Ps3Instrument::None;
    d->hid = hid;
    d->hidReady = hidReady;
    d->name = productName(fd, dd.iProduct);
    if (d->name.empty() && d->ps3 != Ps3Instrument::None) d->name = ps3InstrumentName(d->ps3);
    if (d->name.empty()) d->name = d->receiver ? "Xbox 360 Wireless Receiver" : protocolName(iface.protocol) + std::string(" pad");
    // Known only by a guess (a generic HID pad's layout read off its descriptor), or not at all:
    // the X-ray, and its reports logged for a while.
    if (xrayOnly) {
        d->links.front()->xray.start(path, d->name, msNow());
    } else if (d->ps3 != Ps3Instrument::None) {  // known from its docs, not yet seen on a console
        XC_LOGI("usb pad: %s: %s (%04x:%04x)", path.c_str(), ps3InstrumentName(d->ps3), vid, pid);
        xrayDescribe(fd, path, dd, config, configLen, "a PS3 instrument, not yet confirmed on a console");
        d->links.front()->xray.start(path, d->name, msNow(), false);
    } else if (iface.protocol == UsbPadProtocol::GenericHid) {
        xrayDescribe(fd, path, dd, config, configLen, "a generic HID pad, its layout guessed from its descriptor");
        d->links.front()->xray.start(path, d->name, msNow());
    }
    d->thread = std::thread(readLoop, d.get());
    g_devices.push_back(std::move(d));
}

void scan() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Gone (unplugged, or its thread gave up): closed.
    for (auto it = g_devices.begin(); it != g_devices.end();) {
        Device& d = **it;
        struct stat st {};
        if (d.dead || stat(d.path.c_str(), &st) != 0) {
            if (d.failed) {
                if (stat(d.path.c_str(), &st) == 0) g_ignored.insert({d.path, st.st_ino});
                XC_LOGW("usb pad: %s left alone until it is plugged in again", d.path.c_str());
            }
            closeDevice(d);
            it = g_devices.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_ignored.begin(); it != g_ignored.end();) {
        struct stat st {};
        if (stat(it->first.c_str(), &st) != 0 || st.st_ino != it->second) it = g_ignored.erase(it);
        else ++it;
    }
    // New ones. Address 1 of each bus is its root hub.
    for (int bus = 0; bus < 8; ++bus) {
        for (int addr = 2; addr < 32; ++addr) {
            char path[32];
            std::snprintf(path, sizeof path, "/dev/ugen%d.%d", bus, addr);
            struct stat st {};
            if (stat(path, &st) != 0) continue;
            bool known = g_ignored.count({path, st.st_ino}) != 0;
            for (const auto& d : g_devices) known |= d->path == path;
            if (!known) probe(path, st.st_ino);
        }
    }
}

void scanLoop() {
    while (g_running) {
        scan();
        for (int i = 0; i < 15 && g_running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& d : g_devices) closeDevice(*d);
    g_devices.clear();
    g_ignored.clear();
}

}  // namespace

void setUsbPadSlotRumble(int slot, uint8_t large, uint8_t small) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& d : g_devices)
        for (const auto& l : d->links) {
            if (l->slot != slot) continue;
            const uint32_t v = static_cast<uint32_t>(large) << 8 | small;
            if (l->rumble.exchange(v) != v) l->rumbleAsked = true;
            return;
        }
}

void usbPadsStart() {
    if (g_running.exchange(true)) return;
    XC_LOGI("usb pad: looking for Xbox / XInput pads on /dev/ugen*");
    g_scanner = std::thread(scanLoop);
}

void usbPadsStop() {
    if (!g_running.exchange(false)) return;
    if (g_scanner.joinable()) g_scanner.join();
}

}  // namespace xc::input
