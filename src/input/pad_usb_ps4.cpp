// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// USB pads on the PS4 (pad_usb.h).
// Native in-app USB controller driver subsystem using libSceUsbd (Orbis OS userland USB).
// Ported and adapted from OneFamily (ChiricoG/OneFamily, MIT) and OmniPad CE.
// As on the PS5 (pad_usb_ps5.cpp), an Xbox 360 wireless receiver's channels become players as
// devices link to them, PS3 instruments' dongles play as instruments, and a controller the app
// does not know (or knows only by a guess) gets its X-ray in the log (usb_xray.h). Each pad, and
// each receiver channel, has a thread of its own reading its interrupt IN endpoint with
// synchronous transfers; a thread looks for pads plugged in and unplugged every 1.5 s.
#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include "input/usb_xray.h"
#include "platform/platform.h"
#include "util/log.h"

#include <stdint.h>
#include <cstring>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <orbis/Usbd.h>

extern "C" {
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
int32_t sceKernelDlsym(int32_t handle, const char* symbol, void** address);
int sceKernelStopUnloadModule(int handle, size_t argc, const void* argv, uint32_t flags, void* opt, int* res);
}

namespace xc::input {

namespace {

// libSceUsbd dynamic function pointer types
typedef int32_t (*sceUsbdInit_t)(void);
typedef int32_t (*sceUsbdExit_t)(void);
typedef int32_t (*sceUsbdGetDeviceList_t)(libusb_device***);
typedef int32_t (*sceUsbdFreeDeviceList_t)(libusb_device**);
typedef int32_t (*sceUsbdGetDeviceDescriptor_t)(libusb_device*, struct libusb_device_descriptor*);
typedef int32_t (*sceUsbdGetActiveConfigDescriptor_t)(libusb_device*, struct libusb_config_descriptor**);
typedef int32_t (*sceUsbdGetConfigDescriptor_t)(libusb_device*, uint8_t, struct libusb_config_descriptor**);
typedef int32_t (*sceUsbdFreeConfigDescriptor_t)(struct libusb_config_descriptor*);
typedef int32_t (*sceUsbdOpen_t)(libusb_device*, libusb_device_handle**);
typedef void    (*sceUsbdClose_t)(libusb_device_handle*);
typedef int32_t (*sceUsbdClaimInterface_t)(libusb_device_handle*, int32_t);
typedef int32_t (*sceUsbdReleaseInterface_t)(libusb_device_handle*, int32_t);
typedef int32_t (*sceUsbdSetInterfaceAltSetting_t)(libusb_device_handle*, int32_t, int32_t);
typedef int32_t (*sceUsbdInterruptTransfer_t)(libusb_device_handle*, unsigned char, unsigned char*, int32_t, int32_t*, uint32_t);
typedef int32_t (*sceUsbdControlTransfer_t)(libusb_device_handle*, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char*, uint16_t, uint32_t);
typedef int32_t (*sceUsbdCheckConnected_t)(libusb_device_handle*);
typedef int32_t (*sceUsbdClearHalt_t)(libusb_device_handle*, unsigned char);
typedef void    (*sceUsbdDetachKernelDriver_t)(libusb_device_handle*, int32_t);
typedef int32_t (*sceUsbdGetStringDescriptorAscii_t)(libusb_device_handle*, uint8_t, uint16_t, unsigned char*, int32_t);
typedef uint8_t (*sceUsbdGetBusNumber_t)(libusb_device*);
typedef uint8_t (*sceUsbdGetDeviceAddress_t)(libusb_device*);

static int g_hUsbd = 0;
static sceUsbdInit_t                       p_sceUsbdInit = nullptr;
static sceUsbdExit_t                       p_sceUsbdExit = nullptr;
static sceUsbdGetDeviceList_t              p_sceUsbdGetDeviceList = nullptr;
static sceUsbdFreeDeviceList_t             p_sceUsbdFreeDeviceList = nullptr;
static sceUsbdGetDeviceDescriptor_t        p_sceUsbdGetDeviceDescriptor = nullptr;
static sceUsbdGetActiveConfigDescriptor_t  p_sceUsbdGetActiveConfigDescriptor = nullptr;
static sceUsbdGetConfigDescriptor_t        p_sceUsbdGetConfigDescriptor = nullptr;
static sceUsbdFreeConfigDescriptor_t       p_sceUsbdFreeConfigDescriptor = nullptr;
static sceUsbdOpen_t                       p_sceUsbdOpen = nullptr;
static sceUsbdClose_t                      p_sceUsbdClose = nullptr;
static sceUsbdClaimInterface_t             p_sceUsbdClaimInterface = nullptr;
static sceUsbdReleaseInterface_t           p_sceUsbdReleaseInterface = nullptr;
static sceUsbdSetInterfaceAltSetting_t     p_sceUsbdSetInterfaceAltSetting = nullptr;
static sceUsbdInterruptTransfer_t          p_sceUsbdInterruptTransfer = nullptr;
static sceUsbdControlTransfer_t            p_sceUsbdControlTransfer = nullptr;
static sceUsbdCheckConnected_t             p_sceUsbdCheckConnected = nullptr;
static sceUsbdClearHalt_t                  p_sceUsbdClearHalt = nullptr;
static sceUsbdDetachKernelDriver_t         p_sceUsbdDetachKernelDriver = nullptr;
static sceUsbdGetStringDescriptorAscii_t   p_sceUsbdGetStringDescriptorAscii = nullptr;
static sceUsbdGetBusNumber_t               p_sceUsbdGetBusNumber = nullptr;
static sceUsbdGetDeviceAddress_t           p_sceUsbdGetDeviceAddress = nullptr;

bool loadUsbd() {
    if (g_hUsbd > 0) return true;
    int res = 0;
    int h = sceKernelLoadStartModule("/system/common/lib/libSceUsbd.sprx", 0, nullptr, 0, nullptr, &res);
    if (h < 0) {
        h = sceKernelLoadStartModule("libSceUsbd.sprx", 0, nullptr, 0, nullptr, &res);
    }
    if (h <= 0) {
        XC_LOGW("usb pad: failed to load libSceUsbd.sprx: 0x%08x", static_cast<unsigned>(h));
        return false;
    }
    g_hUsbd = h;

#define RESOLVE(fn) sceKernelDlsym(g_hUsbd, #fn, reinterpret_cast<void**>(&p_##fn))
    RESOLVE(sceUsbdInit);
    RESOLVE(sceUsbdExit);
    RESOLVE(sceUsbdGetDeviceList);
    RESOLVE(sceUsbdFreeDeviceList);
    RESOLVE(sceUsbdGetDeviceDescriptor);
    RESOLVE(sceUsbdGetActiveConfigDescriptor);
    RESOLVE(sceUsbdGetConfigDescriptor);
    RESOLVE(sceUsbdFreeConfigDescriptor);
    RESOLVE(sceUsbdOpen);
    RESOLVE(sceUsbdClose);
    RESOLVE(sceUsbdClaimInterface);
    RESOLVE(sceUsbdReleaseInterface);
    RESOLVE(sceUsbdSetInterfaceAltSetting);
    RESOLVE(sceUsbdInterruptTransfer);
    RESOLVE(sceUsbdControlTransfer);
    RESOLVE(sceUsbdCheckConnected);
    RESOLVE(sceUsbdClearHalt);
    RESOLVE(sceUsbdDetachKernelDriver);
    RESOLVE(sceUsbdGetStringDescriptorAscii);
    RESOLVE(sceUsbdGetBusNumber);
    RESOLVE(sceUsbdGetDeviceAddress);
#undef RESOLVE

    if (!p_sceUsbdInit || !p_sceUsbdGetDeviceList || !p_sceUsbdOpen || !p_sceUsbdInterruptTransfer) {
        XC_LOGE("usb pad: failed to resolve required libSceUsbd symbols");
        return false;
    }
    p_sceUsbdInit();
    XC_LOGI("usb pad: libSceUsbd loaded and initialized (handle 0x%08x)", g_hUsbd);
    return true;
}

int32_t getActiveConfigDescriptor(libusb_device* dev, struct libusb_config_descriptor** config) {
    if (p_sceUsbdGetActiveConfigDescriptor && p_sceUsbdGetActiveConfigDescriptor(dev, config) == 0 && *config) {
        return 0;
    }
    if (p_sceUsbdGetConfigDescriptor && p_sceUsbdGetConfigDescriptor(dev, 0, config) == 0 && *config) {
        return 0;
    }
    return -1;
}

void freeConfigDescriptor(struct libusb_config_descriptor* config) {
    if (p_sceUsbdFreeConfigDescriptor && config) {
        p_sceUsbdFreeConfigDescriptor(config);
    }
}

// Controller hardware identifiers and interface masks
constexpr uint16_t SONY_VID = 0x054C;
constexpr uint16_t SONY_DS3_PID = 0x0268;
constexpr uint16_t SONY_DS4_V1_PID = 0x05C4;
constexpr uint16_t SONY_DS4_V2_PID = 0x09CC;
constexpr uint16_t SONY_DS4_PROT_PID = 0x05C5;
constexpr uint16_t SONY_DS4_DONGLE_PID = 0x0BA0;
constexpr uint16_t SONY_DUALSENSE_PID = 0x0CE6;
constexpr uint16_t SONY_DUALSENSE_EDGE_PID = 0x0DF2;

constexpr uint16_t XBOX_VID = 0x045E;
constexpr uint16_t XBOX360_PID = 0x028E;
constexpr uint16_t XBOX360_RECEIVER_PID = 0x0719;     // Xbox 360 Wireless Receiver for Windows
constexpr uint16_t XBOX360_RECEIVER_V1_PID = 0x0291;  // ...its first version (and many clones)
constexpr uint16_t PDP_VID = 0x0E6F;
constexpr uint16_t VICTRIX_PRO_BFG_PID = 0x024C;

constexpr uint16_t SWITCH_PRO_VID = 0x057E;
constexpr uint16_t SWITCH_PRO_PID = 0x2009;
constexpr uint16_t SWITCH_ROCKCANDY_PID = 0x0187;

constexpr uint16_t GOOGLE_VID = 0x18D1;
constexpr uint16_t STADIA_PID = 0x9400;

bool isNativePs4Controller(uint16_t vid, uint16_t pid) {
    if (vid == SONY_VID) {
        if (pid == SONY_DS4_V1_PID || pid == SONY_DS4_V2_PID || pid == SONY_DS4_PROT_PID || pid == SONY_DS4_DONGLE_PID) {
            return true;
        }
    }
    return false;
}

bool isXboxOnePid(uint16_t pid) {
    static const uint16_t pids[] = {0x02D1, 0x02DD, 0x02E3, 0x02EA, 0x0B00, 0x0B0A, 0x0B12};
    for (uint16_t p : pids) {
        if (p == pid) return true;
    }
    return false;
}

bool isSwitchProPid(uint16_t pid) {
    static const uint16_t pids[] = {0x2009, 0x2006, 0x2007, 0x200E, 0x2017, 0x2019, 0x201E};
    for (uint16_t p : pids) {
        if (p == pid) return true;
    }
    return false;
}

struct DetectedPad {
    UsbPadProtocol protocol = UsbPadProtocol::None;
    uint8_t interfaceNum = 0;
    uint8_t altSetting = 0;
    uint8_t epIn = 0;
    uint8_t epOut = 0;
    uint8_t kbdInterfaceNum = 0xFF;
    std::string name;
    bool receiver = false;  // an Xbox 360 wireless receiver: the interface above is its first channel
    Ps3Instrument ps3 = Ps3Instrument::None;  // a PS3 instrument's dongle: which one
};

bool detectXboxByDescriptor(libusb_device* dev, DetectedPad& out) {
    struct libusb_config_descriptor* cfg = nullptr;
    if (getActiveConfigDescriptor(dev, &cfg) != 0 || !cfg) return false;

    bool found = false;
    for (int i = 0; i < cfg->bNumInterfaces && !found; ++i) {
        const struct libusb_interface* intf = &cfg->interface[i];
        for (int a = 0; a < intf->num_altsetting && !found; ++a) {
            const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
            if (alt->bInterfaceClass != 0xFF) continue;

            UsbPadProtocol proto = UsbPadProtocol::None;
            if (alt->bInterfaceSubClass == 0x5D && (alt->bInterfaceProtocol == 0x01 || alt->bInterfaceProtocol == 0x81 || alt->bInterfaceProtocol == 0x00)) {
                proto = UsbPadProtocol::XInput;
            } else if (alt->bInterfaceSubClass == 0x47 && alt->bInterfaceProtocol == 0xD0) {
                proto = UsbPadProtocol::Gip;
            } else {
                continue;
            }

            uint8_t epIn = 0, epOut = 0;
            for (int k = 0; k < alt->bNumEndpoints; ++k) {
                const struct libusb_endpoint_descriptor* ep = &alt->endpoint[k];
                if ((ep->bmAttributes & 0x03) != 0x03) continue; // Interrupt
                if (ep->bEndpointAddress & 0x80) {
                    if (!epIn) epIn = ep->bEndpointAddress;
                } else {
                    if (!epOut) epOut = ep->bEndpointAddress;
                }
            }
            if (!epIn) continue;

            out.protocol = proto;
            out.interfaceNum = alt->bInterfaceNumber;
            out.altSetting = alt->bAlternateSetting;
            out.epIn = epIn;
            out.epOut = epOut;
            out.kbdInterfaceNum = 0xFF;
            found = true;
        }
    }

    freeConfigDescriptor(cfg);
    return found;
}

// An Xbox 360 wireless receiver: its pad interfaces are FF/5D/81, one per device it can link (the
// headset ones that follow each, FF/5D/82, are not read). The first one, with its endpoints, as a
// fallback for when the whole configuration descriptor cannot be read.
bool detectXInputReceiver(libusb_device* dev, DetectedPad& out) {
    struct libusb_config_descriptor* cfg = nullptr;
    if (getActiveConfigDescriptor(dev, &cfg) != 0 || !cfg) return false;

    bool found = false;
    for (int i = 0; i < cfg->bNumInterfaces && !found; ++i) {
        const struct libusb_interface* intf = &cfg->interface[i];
        for (int a = 0; a < intf->num_altsetting && !found; ++a) {
            const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
            if (alt->bAlternateSetting != 0 || alt->bInterfaceClass != 0xFF || alt->bInterfaceSubClass != 0x5D ||
                alt->bInterfaceProtocol != 0x81)
                continue;
            uint8_t epIn = 0, epOut = 0;
            for (int k = 0; k < alt->bNumEndpoints; ++k) {
                const struct libusb_endpoint_descriptor* ep = &alt->endpoint[k];
                if ((ep->bmAttributes & 0x03) != 0x03) continue;
                uint8_t& address = (ep->bEndpointAddress & 0x80) ? epIn : epOut;
                if (!address) address = ep->bEndpointAddress;
            }
            if (!epIn) continue;
            out.protocol = UsbPadProtocol::XInput;
            out.interfaceNum = alt->bInterfaceNumber;
            out.altSetting = 0;
            out.epIn = epIn;
            out.epOut = epOut;
            out.receiver = true;
            found = true;
        }
    }

    freeConfigDescriptor(cfg);
    return found;
}

bool detectGenericHidEndpoints(libusb_device* dev, DetectedPad& out, bool allowCompositeKbdMouse = false) {
    struct libusb_config_descriptor* cfg = nullptr;
    if (getActiveConfigDescriptor(dev, &cfg) != 0 || !cfg) return false;

    bool hasBootKbdOrMouse = false;
    for (int i = 0; i < cfg->bNumInterfaces; ++i) {
        const struct libusb_interface* intf = &cfg->interface[i];
        for (int a = 0; a < intf->num_altsetting; ++a) {
            const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
            if (isBootKeyboardOrMouse(alt->bInterfaceClass, alt->bInterfaceSubClass, alt->bInterfaceProtocol)) {
                hasBootKbdOrMouse = true;
                break;
            }
        }
    }
    if (hasBootKbdOrMouse && !allowCompositeKbdMouse) {
        freeConfigDescriptor(cfg);
        return false;
    }

    bool found = false;
    for (int i = 0; i < cfg->bNumInterfaces && !found; ++i) {
        const struct libusb_interface* intf = &cfg->interface[i];
        for (int a = 0; a < intf->num_altsetting && !found; ++a) {
            const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
            if (alt->bInterfaceClass != 0x03) continue; // HID
            if (isBootKeyboardOrMouse(alt->bInterfaceClass, alt->bInterfaceSubClass, alt->bInterfaceProtocol)) continue; // Skip boot keyboard & mouse

            uint8_t epIn = 0, epOut = 0;
            for (int k = 0; k < alt->bNumEndpoints; ++k) {
                const struct libusb_endpoint_descriptor* ep = &alt->endpoint[k];
                if ((ep->bmAttributes & 0x03) != 0x03) continue;
                if (ep->bEndpointAddress & 0x80) {
                    if (!epIn) epIn = ep->bEndpointAddress;
                } else {
                    if (!epOut) epOut = ep->bEndpointAddress;
                }
            }
            if (!epIn) continue;

            out.protocol = UsbPadProtocol::GenericHid;
            out.interfaceNum = alt->bInterfaceNumber;
            out.altSetting = alt->bAlternateSetting;
            out.epIn = epIn;
            out.epOut = epOut;
            out.kbdInterfaceNum = 0xFF;
            found = true;
        }
    }

    freeConfigDescriptor(cfg);
    return found;
}

bool isDualShock4Controller(uint16_t vid, uint16_t pid) {
    if (vid == SONY_VID && (pid == SONY_DS4_V1_PID || pid == SONY_DS4_V2_PID || pid == SONY_DS4_PROT_PID || pid == SONY_DS4_DONGLE_PID)) {
        return false; // Handled natively by Orbis ScePad
    }
    if (vid == 0x146B && (pid == 0x0601 || pid == 0x0602 || pid == 0x0604 || pid == 0x0609)) return true; // Nacon
    if (vid == 0x1532 && (pid == 0x0401 || pid == 0x1000 || pid == 0x1004 || pid == 0x1007 || pid == 0x1008)) return true; // Razer
    if (vid == 0x0F0D && (pid == 0x0055 || pid == 0x0066 || pid == 0x0084 || pid == 0x00A0 || pid == 0x00EE)) return true; // Hori
    if (vid == 0x2C22 && (pid == 0x2000 || pid == 0x2300 || pid == 0x2500)) return true; // Qanba
    if (vid == 0x0C12 && (pid == 0x0E10 || pid == 0x0CF2 || pid == 0x1CF2)) return true; // Brook / Zeroplus
    return false;
}

bool identifyController(libusb_device* dev, const struct libusb_device_descriptor& desc, DetectedPad& out) {
    uint16_t vid = desc.idVendor;
    uint16_t pid = desc.idProduct;

    // 0. Skip native PS4 controllers (handled natively by ScePad)
    if (isNativePs4Controller(vid, pid)) return false;

    // 1. Sony DualShock 3
    if (vid == SONY_VID && pid == SONY_DS3_PID) {
        out.protocol = UsbPadProtocol::DualShock3;
        out.interfaceNum = 0;
        out.epIn = 0x81;
        out.epOut = 0x02;
        out.name = "DualShock 3";
        return true;
    }

    // 1b. Third-party DualShock 4 controllers (Nacon, Razer, Hori, Qanba, Brook, etc.)
    if (isDualShock4Controller(vid, pid)) {
        out.protocol = UsbPadProtocol::DualShock4;
        out.name = "PS4 Controller";
        if (!detectGenericHidEndpoints(dev, out)) {
            out.interfaceNum = 0;
            out.epIn = 0x81;
            out.epOut = 0x01;
        }
        out.protocol = UsbPadProtocol::DualShock4;
        return true;
    }

    // 1c. PlayStation 3 instruments' USB dongles: their HID interface, read as the instrument.
    // Without one the device is not known (its X-ray tells what it has).
    if (const Ps3Instrument ps3 = ps3InstrumentFor(vid, pid); ps3 != Ps3Instrument::None) {
        if (!detectGenericHidEndpoints(dev, out)) {
            XC_LOGW("usb pad: %04x:%04x: %s without an interrupt IN endpoint", vid, pid, ps3InstrumentName(ps3));
            out = DetectedPad{};
            return false;
        }
        out.protocol = UsbPadProtocol::Ps3Instrument;
        out.ps3 = ps3;
        out.name = ps3InstrumentName(ps3);
        return true;
    }

    // 2. Sony DualSense / DualSense Edge
    if (vid == SONY_VID && (pid == SONY_DUALSENSE_PID || pid == SONY_DUALSENSE_EDGE_PID)) {
        out.protocol = UsbPadProtocol::DualSense;
        out.name = (pid == SONY_DUALSENSE_EDGE_PID) ? "DualSense Edge" : "DualSense";
        if (!detectGenericHidEndpoints(dev, out)) {
            out.interfaceNum = 3;
            out.epIn = 0x84;
            out.epOut = 0x03;
        }
        out.protocol = UsbPadProtocol::DualSense;
        return true;
    }

    // 2b. Xbox 360 wireless receiver: not a pad itself, a channel for each device it links
    if (detectXInputReceiver(dev, out) ||
        (vid == XBOX_VID && (pid == XBOX360_RECEIVER_PID || pid == XBOX360_RECEIVER_V1_PID))) {
        if (!out.receiver) {  // its descriptors not read: the first channel where it always is
            out.protocol = UsbPadProtocol::XInput;
            out.interfaceNum = 0;
            out.altSetting = 0;
            out.epIn = 0x81;
            out.epOut = 0x01;
            out.receiver = true;
        }
        out.name = "Xbox 360 Wireless Receiver";
        return true;
    }

    // 3. Official Xbox 360 & Xbox One
    if (vid == XBOX_VID) {
        if (pid == XBOX360_PID) {
            out.protocol = UsbPadProtocol::XInput;
            out.name = "Xbox 360 Controller";
            if (!detectXboxByDescriptor(dev, out)) {
                out.interfaceNum = 0;
                out.epIn = 0x81;
                out.epOut = 0x02;
            }
            return true;
        }
        if (isXboxOnePid(pid)) {
            out.protocol = UsbPadProtocol::Gip;
            out.name = "Xbox Wireless Controller";
            if (!detectXboxByDescriptor(dev, out)) {
                out.interfaceNum = 0;
                out.altSetting = 1;
                out.epIn = 0x82;
                out.epOut = 0x02;
            }
            return true;
        }
    }

    // 4. PDP Victrix Pro BFG Xbox
    if (vid == PDP_VID && pid == VICTRIX_PRO_BFG_PID) {
        out.protocol = UsbPadProtocol::Gip;
        out.name = "Victrix Pro BFG";
        if (!detectXboxByDescriptor(dev, out)) {
            out.interfaceNum = 0;
            out.altSetting = 1;
            out.epIn = 0x82;
            out.epOut = 0x02;
        }
        return true;
    }

    // 5. Nintendo Switch Pro & Joy-Con Grip
    if (vid == SWITCH_PRO_VID && isSwitchProPid(pid)) {
        out.protocol = UsbPadProtocol::SwitchPro;
        out.name = "Nintendo Switch Pro Controller";
        if (!detectGenericHidEndpoints(dev, out)) {
            out.interfaceNum = 0;
            out.epIn = 0x81;
            out.epOut = 0x01;
        }
        out.protocol = UsbPadProtocol::SwitchPro;
        return true;
    }

    // 6. Switch Input-Only (PDP Rock Candy)
    if (vid == PDP_VID && pid == SWITCH_ROCKCANDY_PID) {
        out.protocol = UsbPadProtocol::SwitchInputOnly;
        out.interfaceNum = 0;
        out.epIn = 0x81;
        out.epOut = 0;
        out.name = "Switch Input-Only Controller";
        return true;
    }

    // 7. Google Stadia
    if (vid == GOOGLE_VID && pid == STADIA_PID) {
        out.protocol = UsbPadProtocol::Stadia;
        out.interfaceNum = 0;
        out.epIn = 0x81;
        out.epOut = 0x01;
        out.name = "Google Stadia Controller";
        return true;
    }

    // 8. Descriptor-based detection (XInput 8BitDo/clones and GIP pads)
    if (detectXboxByDescriptor(dev, out)) {
        out.name = (out.protocol == UsbPadProtocol::Gip) ? "GIP Controller" : "XInput Controller";
        return true;
    }

    // 9. Generic HID fallback
    if (detectGenericHidEndpoints(dev, out)) {
        out.name = "Generic USB Gamepad";
        return true;
    }

    return false;
}

// The device as the log names it.
std::string whereName(uint8_t bus, uint8_t addr, uint16_t vid, uint16_t pid) {
    char b[48];
    if (bus || addr) std::snprintf(b, sizeof b, "bus %u address %u", bus, addr);
    else std::snprintf(b, sizeof b, "usb %04x:%04x", vid, pid);
    return b;
}

std::string hex(const uint8_t* p, int n) {
    std::string s;
    char b[4];
    for (int i = 0; i < n && i < 32; ++i) std::snprintf(b, sizeof b, "%02x ", p[i]), s += b;
    return s;
}

// A string descriptor (UTF-16, ASCII kept), asked for with GET_DESCRIPTOR: the arguments the
// library's own ASCII reader takes are not known for sure.
std::string stringDescriptor(libusb_device_handle* h, uint8_t index) {
    if (!index || !h || !p_sceUsbdControlTransfer) return {};
    uint8_t raw[255] = {};
    const int r = p_sceUsbdControlTransfer(h, 0x80, 0x06, static_cast<uint16_t>(0x0300 | index), 0x0409, raw, sizeof raw, 200);
    if (r < 2 || raw[1] != 0x03) return {};
    std::string s;
    const size_t n = std::min<size_t>({static_cast<size_t>(r), raw[0], sizeof raw});
    for (size_t i = 2; i + 1 < n; i += 2)
        if (raw[i + 1] == 0 && raw[i] >= 0x20 && raw[i] < 0x7f) s += static_cast<char>(raw[i]);
    return s;
}

// The whole configuration descriptor, as the device sends it (GET_DESCRIPTOR, the first
// configuration: its 9-byte head says how long it is): the library only gives it parsed, the
// shared helpers read it raw. Its length; 0 when it could not be read.
size_t readConfigDescriptor(libusb_device_handle* h, const std::string& where, uint8_t* out, size_t capacity) {
    if (!h || !p_sceUsbdControlTransfer) return 0;
    uint8_t head[9] = {};
    int r = p_sceUsbdControlTransfer(h, 0x80, 0x06, 0x0200, 0, head, sizeof head, 300);
    if (r < 4 || head[1] != 0x02) {
        XC_LOGW("usb pad: %s: configuration descriptor: 0x%08x", where.c_str(), static_cast<unsigned>(r));
        return 0;
    }
    const auto want = static_cast<uint16_t>(std::min<size_t>(static_cast<size_t>(head[2] | head[3] << 8), capacity));
    r = p_sceUsbdControlTransfer(h, 0x80, 0x06, 0x0200, 0, out, want, 300);
    if (r <= 0) {
        XC_LOGW("usb pad: %s: configuration descriptor, %u bytes: 0x%08x", where.c_str(), want, static_cast<unsigned>(r));
        return 0;
    }
    return std::min<size_t>(static_cast<size_t>(r), want);
}

// An interface's class (alternate setting 0) in a configuration descriptor; -1: not there.
int interfaceClass(const uint8_t* config, size_t len, uint8_t number) {
    for (size_t off = 0; off + 2 <= len; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > len) break;
        if (config[off + 1] == 0x04 && l >= 9 && config[off + 2] == number && config[off + 3] == 0) return config[off + 5];
    }
    return -1;
}

// An endpoint's largest packet (wMaxPacketSize) in a configuration descriptor; 0: not there.
int endpointMaxPacket(const uint8_t* config, size_t len, uint8_t address) {
    for (size_t off = 0; off + 2 <= len; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > len) break;
        if (config[off + 1] == 0x05 && l >= 7 && config[off + 2] == address) return (config[off + 4] | config[off + 5] << 8) & 0x7FF;
    }
    return 0;
}

// The X-ray of a device (usb_xray.h): its identity, names and descriptors (each HID interface's
// report descriptor read); `report` gets the first game controller's, for the caller.
void xrayDescribe(libusb_device_handle* h, const std::string& where, const struct libusb_device_descriptor& dd,
                  const uint8_t* config, size_t configLen, const std::string& why, std::vector<uint8_t>* report = nullptr) {
    UsbXrayDevice x;
    x.where = where;
    x.vid = dd.idVendor, x.pid = dd.idProduct, x.release = dd.bcdDevice;
    x.deviceClass = dd.bDeviceClass, x.deviceSubclass = dd.bDeviceSubClass, x.deviceProtocol = dd.bDeviceProtocol;
    x.manufacturer = stringDescriptor(h, dd.iManufacturer);
    x.product = stringDescriptor(h, dd.iProduct);
    x.config = config, x.configLen = configLen, x.why = why;
    usbXrayDescribe(x);
    if (!p_sceUsbdControlTransfer) return;
    uint8_t iface = 0, sub = 0xff;
    bool hid = false;
    for (size_t off = 0; off + 2 <= configLen; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > configLen) break;
        if (config[off + 1] == 0x04 && l >= 9) iface = config[off + 2], hid = config[off + 5] == 0x03, sub = config[off + 6];
        if (config[off + 1] != 0x21 || l < 9 || !hid || sub != 0x00) continue;  // a HID descriptor, no boot device
        uint8_t desc[1024] = {};
        const uint16_t want = std::min<uint16_t>(static_cast<uint16_t>(config[off + 7] | (config[off + 8] << 8)), sizeof desc);
        if (!want) continue;
        const int r = p_sceUsbdControlTransfer(h, 0x81, 0x06, 0x2200, iface, desc, want, 300);
        if (r <= 0) {
            XC_LOGI("usb x-ray: %s interface %u report descriptor: 0x%08x", where.c_str(), iface, static_cast<unsigned>(r));
            continue;
        }
        const size_t got = std::min<size_t>(static_cast<size_t>(r), want);
        usbXrayReportDescriptor(where, iface, desc, got);
        if (report && report->empty() && isGamepadReportDescriptor(desc, got)) report->assign(desc, desc + got);
    }
}

// A controller's interface by its descriptors, for the X-ray only: the first HID (not a boot
// keyboard or mouse) or vendor-class interface with an interrupt IN endpoint, and its OUT if any.
UsbPadInterface xrayInterface(const uint8_t* config, size_t configLen) {
    UsbPadInterface found;
    bool candidate = false;
    for (size_t off = 0; off + 2 <= configLen; off += config[off]) {
        const size_t l = config[off];
        if (l < 2 || off + l > configLen) break;
        if (config[off + 1] == 0x04 && l >= 9) {
            if (candidate && found.epIn) return found;
            found = {UsbPadProtocol::GenericHid, config[off + 2], 0, 0, false};
            candidate = (config[off + 5] == 0x03 && !isBootKeyboardOrMouse(config[off + 5], config[off + 6], config[off + 7])) ||
                        config[off + 5] == 0xFF;
        } else if (config[off + 1] == 0x05 && l >= 7 && candidate && (config[off + 3] & 0x03) == 0x03) {
            uint8_t& ep = (config[off + 2] & 0x80) ? found.epIn : found.epOut;
            if (!ep) ep = config[off + 2];
        }
    }
    return candidate && found.epIn ? found : UsbPadInterface{};
}

// One pad on a USB device: a wired pad is its device's only one; an Xbox 360 wireless receiver has
// one per device it can link (up to four), each its own player once something links to it.
struct Link {
    UsbPadInterface iface;  // its interface and endpoints (wireless: a receiver's channel)
    int channel = 0;        // a receiver's: which of its pad interfaces, in order
    int readSize = 256;     // bytes asked of each IN transfer
    // Its player slot: a wired pad's from the start, a receiver channel's once something links to
    // it; -1: none. Other threads read it (setUsbPadSlotRumble).
    std::atomic<int> slot{-1};
    bool attached = false;      // usbPadAttached told about the slot
    bool present = false;       // a receiver's channel: something linked to it (08 80)
    bool noSlotLogged = false;  // ...linked while every player was taken: said once
    int subtype = -1;           // XInput device subtype, once a receiver's link report told it
    // The motors asked for (large << 8 | small) by any thread; the link's own thread sends them.
    std::atomic<uint32_t> pendingRumble{0};
    std::atomic<bool> hasPendingRumble{false};
    uint8_t rumbleSequence = 0;
    ControllerState state;
    int shown = 0, changesLogged = 0, lightPlayer = -1, sticksLogged = 0;
    float loggedRightX = 0, loggedRightY = 0;
    unsigned buttons = 0;
    std::chrono::steady_clock::time_point lastOther{};
    UsbXrayCapture xray;  // its reports in the log, for a while (usb_xray.h)
    std::atomic<bool> dead{false};
    std::thread thread;
};

struct Device {
    libusb_device_handle* handle = nullptr;
    uint64_t key = 0;
    uint8_t bus = 0;
    uint8_t addr = 0;
    uint16_t vid = 0, pid = 0;
    std::string name;
    std::string where;  // as the log names it
    UsbPadProtocol protocol = UsbPadProtocol::None;
    bool receiver = false;  // an Xbox 360 wireless receiver: not a pad itself
    bool xrayOnly = false;  // a controller not known: opened only to log its reports (usb_xray.h)
    Ps3Instrument ps3 = Ps3Instrument::None;  // a PS3 instrument's dongle: how its reports read
    uint64_t keepAliveAt = 0;                 // ...Guitar Hero Live's: when to send its keep-alive
    bool hidConfigured = false;
    GenericHidContext hidCtx{};
    int disconnectErrors = 0;
    std::vector<std::unique_ptr<Link>> links;  // each with its thread
    std::atomic<bool> stop{false};
    std::atomic<bool> dead{false};
    std::atomic<bool> failed{false};  // left alone until it is plugged in again
};

std::mutex g_mutex;  // g_devices, g_ignored
std::vector<std::unique_ptr<Device>> g_devices;
std::set<uint64_t> g_ignored;
std::thread g_scanner;
std::atomic<bool> g_running{false};

// The player slots taken (any thread: a receiver's channel takes one as something links to it).
std::array<std::atomic<bool>, kMaxUsbPads> g_slotTaken{};

uint64_t deviceKey(uint8_t bus, uint8_t addr, uint16_t vid, uint16_t pid) {
    return (static_cast<uint64_t>(bus) << 40) | (static_cast<uint64_t>(addr) << 32) |
           (static_cast<uint64_t>(vid) << 16) | static_cast<uint64_t>(pid);
}

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

// One packet out a link's interrupt OUT endpoint; false when it was not sent.
bool sendOut(Device& d, Link& l, const uint8_t* data, size_t len, uint32_t timeoutMs = 80) {
    if (!d.handle || !l.iface.epOut || !p_sceUsbdInterruptTransfer) return false;
    uint8_t packet[64];
    len = std::min(len, sizeof packet);
    std::memcpy(packet, data, len);
    int sent = 0;
    return p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, packet, static_cast<int32_t>(len), &sent, timeoutMs) == 0;
}

void sendInitHandshake(Device& d, Link& l) {
    if (!d.handle) return;
    const int slot = l.slot;

    if (d.protocol == UsbPadProtocol::XInput) {
        uint8_t dummy[20] = {};
        if (p_sceUsbdControlTransfer) {
            p_sceUsbdControlTransfer(d.handle, 0xC1, 0x01, 0x0100, l.iface.number, dummy, sizeof(dummy), 100);
        }
        if (l.iface.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t ledPkt[3] = {0x01, 0x03, 0x02}; // Player 1 LED
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, ledPkt, sizeof(ledPkt), &transferred, 100);
        }
        XC_LOGI("usb pad %d: XInput wake-up sent", slot);
    } else if (d.protocol == UsbPadProtocol::Gip) {
        static uint8_t s_seq = 1;
        auto sendPacket = [&](const uint8_t* p, size_t sz) {
            if (!l.iface.epOut || !p_sceUsbdInterruptTransfer) return;
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, const_cast<uint8_t*>(p), static_cast<int32_t>(sz), &transferred, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        };
        const uint8_t powerOn[] = {0x05, 0x20, s_seq++, 0x01, 0x00};
        const uint8_t sInit[]   = {0x05, 0x20, s_seq++, 0x0F, 0x06};
        const uint8_t horiInit[] = {0x01, 0x20, s_seq++, 0x09, 0x00, 0x04, 0x20, 0x3a, 0x00, 0x00, 0x00, 0x80, 0x00};
        const uint8_t pdpInit1[] = {0x0a, 0x20, s_seq++, 0x03, 0x00, 0x01, 0x14};
        const uint8_t pdpInit2[] = {0x06, 0x20, s_seq++, 0x02, 0x01, 0x00};
        sendPacket(powerOn, sizeof(powerOn));
        sendPacket(sInit, sizeof(sInit));
        sendPacket(horiInit, sizeof(horiInit));
        sendPacket(pdpInit1, sizeof(pdpInit1));
        sendPacket(pdpInit2, sizeof(pdpInit2));
        XC_LOGI("usb pad %d: GIP initialization handshake sent", slot);
    } else if (d.protocol == UsbPadProtocol::DualShock3) {
        uint8_t magic[5] = {0xF4, 0x42, 0x03, 0x00, 0x00};
        if (p_sceUsbdControlTransfer) {
            p_sceUsbdControlTransfer(d.handle, 0x21, 0x09, 0x03F4, l.iface.number, magic, sizeof(magic), 1000);
            uint8_t dummy[64] = {};
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x03F2, l.iface.number, dummy, 17, 100);
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x03F5, l.iface.number, dummy, 8, 100);
        }
        XC_LOGI("usb pad %d: DualShock 3 operational handshake sent", slot);
    } else if (d.protocol == UsbPadProtocol::DualSense) {
        if (p_sceUsbdControlTransfer) {
            uint8_t desc[512] = {};
            p_sceUsbdControlTransfer(d.handle, 0x81, 0x06, 0x2200, l.iface.number, desc, sizeof(desc), 200);
            uint8_t pairBuf[20] = {};
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x0309, l.iface.number, pairBuf, sizeof(pairBuf), 200);
            uint8_t fwBuf[64] = {};
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x0320, l.iface.number, fwBuf, sizeof(fwBuf), 200);
            uint8_t calBuf[41] = {};
            p_sceUsbdControlTransfer(d.handle, 0xA1, 0x01, 0x0305, l.iface.number, calBuf, sizeof(calBuf), 200);
        }
        if (p_sceUsbdClearHalt) {
            p_sceUsbdClearHalt(d.handle, l.iface.epIn);
            if (l.iface.epOut) p_sceUsbdClearHalt(d.handle, l.iface.epOut);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        if (l.iface.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t resetPkt[64] = {};
            resetPkt[0] = 0x02;
            resetPkt[39] = 0x02;
            resetPkt[42] = 0x02;
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, resetPkt, sizeof(resetPkt), &transferred, 150);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            uint8_t ledPkt[64] = {};
            ledPkt[0] = 0x02;
            ledPkt[2] = 0x04 | 0x10;
            ledPkt[44] = 0x04; // Player 1 LED
            ledPkt[47] = 128;  // Lightbar blue
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, ledPkt, sizeof(ledPkt), &transferred, 150);
        }
        XC_LOGI("usb pad %d: DualSense USB handshake sent", slot);
    } else if (d.protocol == UsbPadProtocol::SwitchPro) {
        if (l.iface.epIn && l.iface.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t rx[64];
            int transferred = 0;
            for (int i = 0; i < 3; ++i) {
                p_sceUsbdInterruptTransfer(d.handle, l.iface.epIn, rx, sizeof(rx), &transferred, 10);
            }
            uint8_t pkt[64] = {};
            pkt[0] = 0x80; pkt[1] = 0x02;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 50);
            pkt[1] = 0x04;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 50);
            // Subcommand 0x03 (Set report mode to 0x30 Full Mode)
            std::memset(pkt, 0, sizeof(pkt));
            pkt[0] = 0x01; pkt[1] = 0x00; pkt[10] = 0x03; pkt[11] = 0x30;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 100);
            // Subcommand 0x40 (Enable IMU)
            pkt[10] = 0x40; pkt[11] = 0x01;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 100);
            // Subcommand 0x30 (Player 1 LED)
            pkt[10] = 0x30; pkt[11] = 0x01;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 100);
        }
        XC_LOGI("usb pad %d: Switch Pro wake-up sent", slot);
    } else if (d.protocol == UsbPadProtocol::GenericHid) {
        if (p_sceUsbdControlTransfer) {
            uint8_t reportDesc[512] = {};
            int r = p_sceUsbdControlTransfer(d.handle, 0x81, 0x06, 0x2200, l.iface.number, reportDesc, sizeof(reportDesc), 200);
            if (r > 0) {
                if (isKeyboardOrMouseReportDescriptor(reportDesc, static_cast<size_t>(r)) ||
                    !isGamepadReportDescriptor(reportDesc, static_cast<size_t>(r))) {
                    XC_LOGI("usb pad %d: Generic HID interface %u is keyboard/mouse or non-gamepad, detaching", slot, l.iface.number);
                    d.stop = true;
                    d.dead = true;
                    return;
                }
                if (parseHidDescriptor(d.hidCtx, reportDesc, static_cast<size_t>(r), d.vid, d.pid)) {
                    d.hidConfigured = true;
                    XC_LOGI("usb pad %d: Generic HID descriptor parsed (%d bytes)", slot, r);
                }
            }
        }
    }
}

void sendDeviceRumble(Device& d, Link& l, uint8_t large, uint8_t small) {
    if (!d.handle) return;
    if (l.iface.wireless) {  // an Xbox 360 receiver's channel: its own 12-byte packet
        uint8_t packet[32];
        const size_t n =
            usbPadRumblePacket(UsbPadProtocol::XInput, large, small, l.rumbleSequence, packet, sizeof packet, true);
        const bool ok = n && sendOut(d, l, packet, n, 40);
        static std::atomic<int> logged{0};
        if (logged < 6 && ++logged)
            XC_LOGI("usb pad %d: motors %u / %u %s", static_cast<int>(l.slot), large, small, ok ? "sent" : "not sent");
        return;
    }
    if (d.protocol == UsbPadProtocol::XInput) {
        if (l.iface.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t pkt[8] = {0x00, 0x08, 0x00, large, small, 0x00, 0x00, 0x00};
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    } else if (d.protocol == UsbPadProtocol::Gip) {
        if (l.iface.epOut && p_sceUsbdInterruptTransfer) {
            static uint8_t s_rumbleSeq = 1;
            uint8_t pkt[13] = {
                0x09, 0x00, s_rumbleSeq++, 0x09, 0x00,
                0x0F,
                0x00, 0x00,
                large, small,
                0xFF, 0x00, 0xFF
            };
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    } else if (d.protocol == UsbPadProtocol::DualShock4) {
        if (l.iface.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t pkt[32] = {};
            pkt[0] = 0x05;
            pkt[1] = 0xFF;
            pkt[2] = 0x04;
            pkt[4] = small;
            pkt[5] = large;
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    } else if (d.protocol == UsbPadProtocol::DualSense) {
        if (l.iface.epOut && p_sceUsbdInterruptTransfer) {
            uint8_t pkt[48] = {};
            pkt[0] = 0x02;
            pkt[1] = 0x03;
            pkt[2] = 0x14;
            pkt[3] = small;
            pkt[4] = large;
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    } else if (d.protocol == UsbPadProtocol::DualShock3) {
        if (p_sceUsbdControlTransfer) {
            uint8_t pkt[49] = {};
            pkt[0] = 0x01;
            pkt[1] = 0x00;
            pkt[2] = small ? 0x01 : 0x00;
            pkt[3] = 0x00;
            pkt[4] = large;
            p_sceUsbdControlTransfer(d.handle, 0x21, 0x09, 0x0201, l.iface.number, pkt, sizeof(pkt), 50);
        }
    } else if (d.protocol == UsbPadProtocol::SwitchPro) {
        if (l.iface.epOut && p_sceUsbdInterruptTransfer) {
            static uint8_t s_proSeq = 0;
            uint8_t pkt[49] = {};
            pkt[0] = 0x10;
            pkt[1] = (s_proSeq++) & 0x0F;
            if (large > 0 || small > 0) {
                pkt[2] = 0x28; pkt[3] = 0x88; pkt[4] = 0x60; pkt[5] = 0x61;
                pkt[6] = 0x28; pkt[7] = 0x88; pkt[8] = 0x60; pkt[9] = 0x61;
            } else {
                pkt[2] = 0x00; pkt[3] = 0x01; pkt[4] = 0x40; pkt[5] = 0x40;
                pkt[6] = 0x00; pkt[7] = 0x01; pkt[8] = 0x40; pkt[9] = 0x40;
            }
            int transferred = 0;
            p_sceUsbdInterruptTransfer(d.handle, l.iface.epOut, pkt, sizeof(pkt), &transferred, 50);
        }
    }
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
void linkAttach(Device& d, Link& l) {
    if (l.slot >= 0) return;
    const int slot = takeSlot();
    if (slot < 0) {
        if (!l.noSlotLogged) XC_LOGW("usb pad: %s channel %d: no free player for it", d.where.c_str(), l.channel);
        l.noSlotLogged = true;
        return;
    }
    l.slot = slot;
    l.lightPlayer = -1;
    l.state = ControllerState{};
    l.shown = l.changesLogged = 0;
    usbPadAttached(slot, receiverDeviceName(l.subtype), usbPadKindForSubtype(l.subtype));
    l.attached = true;
    if (l.subtype < 0) {
        // Linked before the app started: its link report is not sent again. Ask for its
        // capabilities (00 00 02 80, answered with a 00 05 report: vendor, product, version).
        static const uint8_t ask[12] = {0x00, 0x00, 0x02, 0x80};
        sendOut(d, l, ask, sizeof ask, 40);
    }
}

// Its device gone from a receiver's channel (or the channel closing): no player any more.
void linkDetach(Link& l) {
    l.subtype = -1;
    l.noSlotLogged = false;
    const int slot = l.slot;
    if (slot < 0) return;
    usbPadDetached(slot);
    releaseSlot(slot);
    l.slot = -1;
    l.attached = false;
    l.hasPendingRumble = false;
}

// One report off a link.
void handleReport(Device& d, Link& l, const uint8_t* buf, int n) {
    if (l.xray.active()) l.xray.feed(buf, static_cast<size_t>(n), platform::nowMs());
    if (d.xrayOnly) return;
    // GIP ACK responses using standard gipAcksFor
    if (d.protocol == UsbPadProtocol::Gip)
        for (const auto& ack : gipAcksFor(buf, static_cast<size_t>(n))) sendOut(d, l, ack.data(), ack.size(), 40);
    if (l.iface.wireless) {
        // Linked (08 80) or gone (08 00); the link report that follows says what it is.
        if (n >= 2 && buf[0] == 0x08) {
            const bool linked = (buf[1] & 0x80) != 0;
            XC_LOGI("usb pad: %s channel %d: device %s", d.where.c_str(), l.channel, linked ? "linked" : "gone");
            l.present = linked;
            if (!linked) linkDetach(l);
            return;
        }
        if (n >= 16 && buf[0] == 0x00 && buf[1] == 0x05 && buf[5] == 0x12) {
            XC_LOGI("usb pad: %s channel %d: capabilities: vendor %04x product %04x version %04x: %s", d.where.c_str(),
                    l.channel, buf[10] | buf[11] << 8, buf[12] | buf[13] << 8, buf[14] | buf[15] << 8, hex(buf, n).c_str());
            // No link report (linked before the app started): an instrument told by its maker.
            if (l.subtype < 0)
                if (const int subtype = xinputSubtypeFromCapabilities(buf, static_cast<size_t>(n)); subtype >= 0) {
                    l.subtype = subtype;
                    XC_LOGI("usb pad: %s channel %d: device is a %s (subtype 0x%02x, from its maker and product)",
                            d.where.c_str(), l.channel, xinputSubtypeName(subtype), subtype);
                    if (l.slot >= 0) usbPadSetKind(l.slot, usbPadKindForSubtype(subtype));
                }
            return;
        }
        if (const int subtype = xinputWirelessSubtype(buf, static_cast<size_t>(n)); subtype >= 0) {
            l.subtype = subtype;
            l.present = true;
            XC_LOGI("usb pad: %s channel %d: device is a %s (subtype 0x%02x%s): %s", d.where.c_str(), l.channel,
                    xinputSubtypeName(subtype), subtype, (buf[25] & 0x80) ? ", motors flag" : "", hex(buf, n).c_str());
            if (l.slot < 0) linkAttach(d, l);
            else usbPadSetKind(l.slot, usbPadKindForSubtype(subtype));
            return;
        }
    }
    ControllerState state = l.state;
    bool parsed = false;
    if (d.ps3 != Ps3Instrument::None) {
        parsed = parsePs3InstrumentReport(d.ps3, buf, static_cast<size_t>(n), state);
    } else if (d.protocol == UsbPadProtocol::GenericHid) {
        parsed = d.hidConfigured ? parseGenericHidReport(d.hidCtx, buf, static_cast<size_t>(n), state)
                                 : parseGenericHidFallback(buf, static_cast<size_t>(n), state);
    } else {
        parsed = parseUsbPadReport(d.protocol, buf, static_cast<size_t>(n), state);
    }
    // Input from a receiver's channel nobody announced (linked before the app started): a player
    // too, of a kind not known until its capabilities report says it.
    if (parsed && l.iface.wireless && l.slot < 0) linkAttach(d, l);
    const int slot = l.slot;
    if (l.shown < 6) XC_LOGI("usb pad %d: report %d: %d bytes: %s", slot, ++l.shown, n, hex(buf, n).c_str());
    if (!parsed) {
        if (std::chrono::steady_clock::now() - l.lastOther > std::chrono::seconds(5)) {
            l.lastOther = std::chrono::steady_clock::now();
            XC_LOGD("usb pad %d: other report, %d bytes: %s", slot, n, hex(buf, n).c_str());
        }
        return;
    }
    l.state = state;
    if (slot < 0) return;
    usbPadState(slot, state);
    // An instrument's sticks (a guitar's whammy and tilt, a drum kit's pad strengths): their moves,
    // to set what raising the guitar reads.
    const UsbPadKind kind = d.ps3 != Ps3Instrument::None ? ps3InstrumentKind(d.ps3) : usbPadKindForSubtype(l.subtype);
    if (kind != UsbPadKind::Gamepad && l.sticksLogged < 3000 &&
        (std::abs(state.rawRightY - l.loggedRightY) > 0.08f || std::abs(state.rawRightX - l.loggedRightX) > 0.15f)) {
        ++l.sticksLogged;
        l.loggedRightX = state.rawRightX, l.loggedRightY = state.rawRightY;
        XC_LOGI("usb pad %d: %s sticks: left %.2f,%.2f right %.2f,%.2f (whammy x, tilt y)", slot, usbPadKindName(kind),
                state.rawLeftX, state.rawLeftY, state.rawRightX, state.rawRightY);
    }
    // The first presses, to check the buttons from the log (an instrument's, more of them).
    const unsigned now = buttonBits(state);
    if (now != l.buttons && l.changesLogged < (kind != UsbPadKind::Gamepad ? 400 : 80)) {
        ++l.changesLogged;
        XC_LOGI("usb pad %d: buttons %s, sticks %.2f,%.2f %.2f,%.2f, triggers %.2f %.2f", slot, buttonNames(now).c_str(),
                state.rawLeftX, state.rawLeftY, state.rawRightX, state.rawRightY, state.triggerL2, state.triggerR2);
    }
    l.buttons = now;
}

// One link's thread: a wired pad's from the start, as a player; a receiver's channel, a player
// once something links to it; an X-ray's, only its reports logged.
void linkLoop(Device* d, Link* l) {
    std::string tag, tname;
    if (d->xrayOnly) {
        tag = "usb x-ray: " + d->where;
        tname = "ps4_usb_xray";
    } else if (d->receiver) {
        tag = "usb pad: " + d->where + " channel " + std::to_string(l->channel);
        tname = "ps4_usb_rx" + std::to_string(l->channel);
    } else {
        tag = "usb pad " + std::to_string(l->slot.load());
        tname = "ps4_usb_" + std::to_string(l->slot.load());
    }
    platform::setThreadAffinity(platform::ThreadRole::Worker, tname.c_str());
    if (d->receiver) {
        XC_LOGI("usb pad: %s (%04x:%04x, XInput receiver, channel %d: interface %u, IN 0x%02x, OUT 0x%02x) on %s",
                d->name.c_str(), d->vid, d->pid, l->channel, l->iface.number, l->iface.epIn, l->iface.epOut,
                d->where.c_str());
        // Whether something is linked on this channel: a presence message comes back.
        const auto query = xinputWirelessPresenceQuery();
        const bool ok = sendOut(*d, *l, query.data(), query.size());
        XC_LOGI("%s: presence query %s", tag.c_str(), ok ? "sent" : "not sent");
    } else if (d->xrayOnly) {
        XC_LOGI("%s: reading %s (%04x:%04x): interface %u, IN 0x%02x, %d-byte transfers", tag.c_str(), d->name.c_str(),
                d->vid, d->pid, l->iface.number, l->iface.epIn, l->readSize);
    } else {
        const int slot = l->slot;
        XC_LOGI("usb pad %d: read thread started for %s (%04x:%04x, %s)", slot, d->name.c_str(), d->vid, d->pid,
                usbPadProtocolName(d->protocol));
        usbPadAttached(slot, d->name, d->ps3 != Ps3Instrument::None ? ps3InstrumentKind(d->ps3) : UsbPadKind::Gamepad);
        l->attached = true;
        sendInitHandshake(*d, *l);
    }

    auto checkRumble = [&]() {
        if (l->hasPendingRumble.exchange(false, std::memory_order_acq_rel)) {
            uint32_t val = l->pendingRumble.load(std::memory_order_relaxed);
            uint8_t large = static_cast<uint8_t>(val >> 8);
            uint8_t small = static_cast<uint8_t>(val & 0xFF);
            sendDeviceRumble(*d, *l, large, small);
        }
    };

    uint8_t buf[256];
    const int readSize = std::clamp(l->readSize, 1, static_cast<int>(sizeof buf));
    int errors = 0;
    while (!d->stop) {
        // A receiver's pads show their player on their four lights.
        if (l->iface.wireless) {
            const int player = usbPadPlayer(l->slot);
            if (player >= 0 && player != l->lightPlayer) {
                const auto led = xinputWirelessLedPacket(player);
                sendOut(*d, *l, led.data(), led.size(), 40);
                l->lightPlayer = player;
            }
        }
        checkRumble();
        // Guitar Hero Live's dongle lets held frets go without its keep-alive (every 8 s).
        if (d->ps3 == Ps3Instrument::GhLiveGuitar && p_sceUsbdControlTransfer && platform::nowMs() >= d->keepAliveAt) {
            uint8_t keep[sizeof kGhLiveKeepAlive];
            std::memcpy(keep, kGhLiveKeepAlive, sizeof keep);
            const int r = p_sceUsbdControlTransfer(d->handle, 0x21, 0x09, 0x0202, l->iface.number, keep, sizeof keep, 100);
            if (!d->keepAliveAt || r < 0)
                XC_LOGI("usb pad: %s: Guitar Hero Live keep-alive %s (0x%08x)", d->where.c_str(), r >= 0 ? "sent" : "failed",
                        static_cast<unsigned>(r));
            d->keepAliveAt = platform::nowMs() + kGhLiveKeepAliveMs;
        }
        // A receiver's channel with nothing linked is only looked at now and then: what it says
        // (08 80 as something links) waits in the receiver until read, and the channels in use
        // keep the transfers to themselves.
        const bool idle = l->iface.wireless && l->slot < 0 && !l->present;
        int transferred = 0;
        int rc = p_sceUsbdInterruptTransfer(d->handle, l->iface.epIn, buf, readSize, &transferred, idle ? 8 : 20);
        checkRumble();
        bool isTimeout = (rc == -7 || rc == static_cast<int>(0x80240007));
        if (rc != 0 || transferred <= 0) {
            if (rc != 0 && !isTimeout) {
                if (++errors >= 50) {
                    XC_LOGW("%s: %d transfer errors (0x%08x): closed", tag.c_str(), errors, static_cast<unsigned>(rc));
                    break;
                }
            } else {
                errors = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(idle && isTimeout ? 50 : 2));
            continue;
        }
        errors = 0;
        handleReport(*d, *l, buf, std::min(transferred, readSize));
    }

    if (!l->iface.wireless || l->slot >= 0) sendDeviceRumble(*d, *l, 0, 0);  // not left turning
    if (l->iface.wireless) linkDetach(*l);
    if (d->xrayOnly && !d->stop) d->failed = true;  // it cannot be read: not opened again until replugged
    l->dead = true;
    XC_LOGI("%s: read thread finished", tag.c_str());
}

// Every one of its threads is over (or the pad turned out to be none).
bool finished(const Device& d) {
    if (d.dead) return true;
    for (const auto& l : d.links)
        if (!l->dead) return false;
    return true;
}

void closeDevice(Device& d) {
    d.stop = true;
    for (auto& l : d.links)
        if (l->thread.joinable()) l->thread.join();
    for (auto& l : d.links) {
        const int slot = l->slot;
        if (slot < 0) continue;
        if (l->attached) usbPadDetached(slot);
        releaseSlot(slot);
        l->slot = -1;
    }
    if (d.handle) {
        if (p_sceUsbdReleaseInterface) {
            for (auto& l : d.links) p_sceUsbdReleaseInterface(d.handle, l->iface.number);
        }
        if (p_sceUsbdClose) {
            p_sceUsbdClose(d.handle);
        }
        d.handle = nullptr;
    }
}

void startDevice(std::unique_ptr<Device> d) {
    for (auto& l : d->links) l->thread = std::thread(linkLoop, d.get(), l.get());
    g_devices.push_back(std::move(d));
}

// What the library's parsed configuration says of a device: its interfaces for the log, whether
// one is a boot keyboard or mouse, and whether one could be a game controller's (HID, or the
// vendor's own class, with an interrupt IN endpoint).
struct DeviceLook {
    std::string interfaces;
    bool keyboardOrMouse = false;
    bool controllerLike = false;
};

DeviceLook lookAt(libusb_device* dev) {
    DeviceLook look;
    struct libusb_config_descriptor* cfg = nullptr;
    if (getActiveConfigDescriptor(dev, &cfg) != 0 || !cfg) return look;
    for (int i = 0; i < cfg->bNumInterfaces; ++i) {
        const struct libusb_interface* intf = &cfg->interface[i];
        for (int a = 0; a < intf->num_altsetting; ++a) {
            const struct libusb_interface_descriptor* alt = &intf->altsetting[a];
            char b[64];
            std::snprintf(b, sizeof b, " | if %u alt %u: %02x/%02x/%02x", alt->bInterfaceNumber, alt->bAlternateSetting,
                          alt->bInterfaceClass, alt->bInterfaceSubClass, alt->bInterfaceProtocol);
            look.interfaces += b;
            const bool keyboardOrMouse =
                isBootKeyboardOrMouse(alt->bInterfaceClass, alt->bInterfaceSubClass, alt->bInterfaceProtocol);
            look.keyboardOrMouse = look.keyboardOrMouse || keyboardOrMouse;
            const bool candidate = (alt->bInterfaceClass == 0x03 && !keyboardOrMouse) || alt->bInterfaceClass == 0xFF;
            for (int k = 0; k < alt->bNumEndpoints; ++k) {
                const struct libusb_endpoint_descriptor* ep = &alt->endpoint[k];
                std::snprintf(b, sizeof b, " ep %02x/%u", ep->bEndpointAddress, ep->bmAttributes & 3u);
                look.interfaces += b;
                if (candidate && (ep->bmAttributes & 0x03) == 0x03 && (ep->bEndpointAddress & 0x80))
                    look.controllerLike = true;
            }
        }
    }
    freeConfigDescriptor(cfg);
    return look;
}

// A device none of the above knows. A game controller among them gets its X-ray and, for a while,
// its reports logged (usb_xray.h), with no player; anything else stays as it is (null). Keyboards
// and mice are not even opened, nor anything of Sony's (the system's own pads, camera, VR...).
std::unique_ptr<Device> probeUnknown(libusb_device* dev, const struct libusb_device_descriptor& desc, uint8_t bus,
                                     uint8_t addr) {
    const uint16_t vid = desc.idVendor, pid = desc.idProduct;
    if (isNativePs4Controller(vid, pid)) return nullptr;
    const std::string where = whereName(bus, addr, vid, pid);
    const DeviceLook look = lookAt(dev);
    XC_LOGI("usb pad: %s: %04x:%04x (class %02x) is not a known pad, interfaces%s", where.c_str(), vid, pid,
            desc.bDeviceClass, look.interfaces.c_str());
    if (vid == SONY_VID || desc.bDeviceClass == 0x09 || desc.bDeviceClass == 0xE0 || look.keyboardOrMouse ||
        !look.controllerLike)
        return nullptr;
    libusb_device_handle* h = nullptr;
    if (p_sceUsbdOpen(dev, &h) != 0 || !h) {
        XC_LOGW("usb x-ray: %s: sceUsbdOpen failed", where.c_str());
        return nullptr;
    }
    uint8_t config[1024] = {};
    const size_t configLen = readConfigDescriptor(h, where, config, sizeof config);
    const UsbPadInterface xi = configLen ? xrayInterface(config, configLen) : UsbPadInterface{};
    if (!xi.epIn) {
        p_sceUsbdClose(h);
        return nullptr;
    }
    std::vector<uint8_t> report;
    xrayDescribe(h, where, desc, config, configLen, "not known", &report);
    if (!usbXrayWorthCapturing(config, configLen, report.empty() ? nullptr : report.data(), report.size())) {
        XC_LOGI("usb x-ray: %s: not a game controller, its reports are not recorded", where.c_str());
        p_sceUsbdClose(h);
        return nullptr;
    }
    // A HID game controller's interface is taken from the system's HID driver, as a generic HID
    // pad's is; one of the vendor's own class is read only if no driver of the system's holds it.
    if (interfaceClass(config, configLen, xi.number) == 0x03 && p_sceUsbdDetachKernelDriver)
        p_sceUsbdDetachKernelDriver(h, xi.number);
    if (p_sceUsbdClaimInterface && p_sceUsbdClaimInterface(h, xi.number) != 0) {
        XC_LOGW("usb x-ray: %s: claim interface %u failed", where.c_str(), xi.number);
        p_sceUsbdClose(h);
        return nullptr;
    }
    auto d = std::make_unique<Device>();
    d->handle = h;
    d->bus = bus;
    d->addr = addr;
    d->vid = vid;
    d->pid = pid;
    d->where = where;
    d->protocol = UsbPadProtocol::GenericHid;
    d->xrayOnly = true;
    d->name = stringDescriptor(h, desc.iProduct);
    if (d->name.empty()) d->name = usbPadProtocolName(UsbPadProtocol::GenericHid) + std::string(" pad");
    auto l = std::make_unique<Link>();
    l->iface = xi;
    // One report a transfer, as the device sends them, so that each is recorded as it is.
    const int maxPacket = endpointMaxPacket(config, configLen, xi.epIn);
    l->readSize = maxPacket > 0 ? std::min(maxPacket, 256) : 64;
    l->xray.start(where, d->name, platform::nowMs());
    d->links.push_back(std::move(l));
    return d;
}

// An Xbox 360 wireless receiver: each of its channels (one per device it can link, up to four) is
// claimed and read; none is a player until something links to it. Null when none could be.
std::unique_ptr<Device> openReceiver(libusb_device* dev, const struct libusb_device_descriptor& desc,
                                     const DetectedPad& det, uint8_t bus, uint8_t addr) {
    const std::string where = whereName(bus, addr, desc.idVendor, desc.idProduct);
    libusb_device_handle* h = nullptr;
    if (p_sceUsbdOpen(dev, &h) != 0 || !h) {
        XC_LOGW("usb pad: sceUsbdOpen failed for %s (%04x:%04x)", det.name.c_str(), desc.idVendor, desc.idProduct);
        return nullptr;
    }
    uint8_t config[1024] = {};
    const size_t configLen = readConfigDescriptor(h, where, config, sizeof config);
    std::vector<UsbPadInterface> channels = findXInputWirelessInterfaces(config, configLen);
    if (channels.size() > static_cast<size_t>(kMaxUsbPads)) channels.resize(kMaxUsbPads);
    if (channels.empty())  // no descriptor to read them from: the first channel
        channels.push_back({UsbPadProtocol::XInput, det.interfaceNum, det.epIn, det.epOut, true});
    auto d = std::make_unique<Device>();
    d->bus = bus;
    d->addr = addr;
    d->vid = desc.idVendor;
    d->pid = desc.idProduct;
    d->where = where;
    d->protocol = UsbPadProtocol::XInput;
    d->receiver = true;
    d->name = stringDescriptor(h, desc.iProduct);
    if (d->name.empty()) d->name = det.name;
    for (size_t k = 0; k < channels.size(); ++k) {
        const UsbPadInterface& channel = channels[k];
        if (p_sceUsbdDetachKernelDriver) p_sceUsbdDetachKernelDriver(h, channel.number);
        if (p_sceUsbdClaimInterface && p_sceUsbdClaimInterface(h, channel.number) != 0) {
            XC_LOGW("usb pad: %s channel %zu: claim interface %u failed", where.c_str(), k, channel.number);
            continue;
        }
        auto l = std::make_unique<Link>();
        l->iface = channel;
        l->channel = static_cast<int>(k);
        d->links.push_back(std::move(l));
    }
    XC_LOGI("usb pad: %s: %s (%04x:%04x): Xbox 360 wireless receiver, %zu of %zu channel(s) claimed", where.c_str(),
            d->name.c_str(), d->vid, d->pid, d->links.size(), channels.size());
    if (d->links.empty()) {
        p_sceUsbdClose(h);
        return nullptr;
    }
    d->handle = h;
    return d;
}

void scan() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!usbPadsEnabled()) {
        for (auto& d : g_devices) closeDevice(*d);
        g_devices.clear();
        g_ignored.clear();
        return;
    }

    // 1. Remove disconnected / dead devices
    for (auto it = g_devices.begin(); it != g_devices.end();) {
        Device& d = **it;
        bool disconnected = finished(d);
        if (!disconnected && d.handle && p_sceUsbdCheckConnected) {
            if (p_sceUsbdCheckConnected(d.handle) != 0) {
                if (++d.disconnectErrors >= 3) disconnected = true;
            } else {
                d.disconnectErrors = 0;
            }
        }
        if (disconnected) {
            if (d.failed) {
                g_ignored.insert(d.key);
                XC_LOGW("usb pad: %s left alone until it is plugged in again", d.where.c_str());
            }
            XC_LOGI("usb pad: disconnecting %s (%s)", d.name.c_str(), d.where.c_str());
            closeDevice(d);
            it = g_devices.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Discover new devices
    libusb_device** list = nullptr;
    int32_t count = p_sceUsbdGetDeviceList(&list);
    if (count <= 0 || !list) return;

    std::set<uint64_t> presentKeys;
    for (int i = 0; i < count; ++i) {
        libusb_device* dev = list[i];
        if (!dev) continue;

        uint8_t bus = p_sceUsbdGetBusNumber ? p_sceUsbdGetBusNumber(dev) : 0;
        uint8_t addr = p_sceUsbdGetDeviceAddress ? p_sceUsbdGetDeviceAddress(dev) : 0;

        struct libusb_device_descriptor desc{};
        if (p_sceUsbdGetDeviceDescriptor(dev, &desc) != 0) continue;

        const uint64_t key = deviceKey(bus, addr, desc.idVendor, desc.idProduct);
        presentKeys.insert(key);
        if (g_ignored.count(key) != 0) continue;

        // Skip if already managed (by bus/address or VID/PID/interface if bus/addr unavailable)
        bool alreadyOpen = false;
        for (const auto& d : g_devices) {
            if (bus != 0 && addr != 0) {
                if (d->bus == bus && d->addr == addr) {
                    alreadyOpen = true;
                    break;
                }
            } else if (d->vid == desc.idVendor && d->pid == desc.idProduct) {
                alreadyOpen = true;
                break;
            }
        }
        if (alreadyOpen) continue;

        DetectedPad det;
        if (!identifyController(dev, desc, det)) {
            if (auto d = probeUnknown(dev, desc, bus, addr)) {
                d->key = key;
                startDevice(std::move(d));
            } else {
                g_ignored.insert(key);
            }
            continue;
        }

        if (det.receiver) {
            if (auto d = openReceiver(dev, desc, det, bus, addr)) {
                d->key = key;
                startDevice(std::move(d));
            } else {
                g_ignored.insert(key);
            }
            continue;
        }

        const int slot = takeSlot();
        if (slot < 0) {
            XC_LOGW("usb pad: all %d slots taken, cannot attach %s", kMaxUsbPads, det.name.c_str());
            continue;
        }

        libusb_device_handle* h = nullptr;
        if (p_sceUsbdOpen(dev, &h) != 0 || !h) {
            XC_LOGW("usb pad: sceUsbdOpen failed for %s (%04x:%04x)", det.name.c_str(), desc.idVendor, desc.idProduct);
            releaseSlot(slot);
            g_ignored.insert(key);
            continue;
        }

        // For GenericHid, verify HID report descriptor BEFORE detaching kernel driver so keyboards/mice
        // are never severed from Orbis libSceKeyboard / libSceMouse.
        if (det.protocol == UsbPadProtocol::GenericHid) {
            bool verifiedGamepad = false;
            if (p_sceUsbdControlTransfer) {
                uint8_t reportDesc[512] = {};
                int r = p_sceUsbdControlTransfer(h, 0x81, 0x06, 0x2200, det.interfaceNum, reportDesc, sizeof(reportDesc), 300);
                if (r > 0 && isGamepadReportDescriptor(reportDesc, static_cast<size_t>(r)) &&
                    !isKeyboardOrMouseReportDescriptor(reportDesc, static_cast<size_t>(r))) {
                    verifiedGamepad = true;
                }
            }
            if (!verifiedGamepad) {
                XC_LOGI("usb pad: rejecting unverified / non-gamepad HID device (%04x:%04x, if %u)",
                        desc.idVendor, desc.idProduct, det.interfaceNum);
                p_sceUsbdClose(h);
                releaseSlot(slot);
                g_ignored.insert(key);
                continue;
            }
        }

        // Set alt setting if needed (e.g. Xbox One GIP alt 1)
        if (det.altSetting > 0 && p_sceUsbdSetInterfaceAltSetting) {
            p_sceUsbdSetInterfaceAltSetting(h, det.interfaceNum, det.altSetting);
        }

        if (p_sceUsbdDetachKernelDriver) p_sceUsbdDetachKernelDriver(h, det.interfaceNum);
        if (p_sceUsbdClaimInterface && p_sceUsbdClaimInterface(h, det.interfaceNum) != 0) {
            XC_LOGW("usb pad: claim interface %d failed for %s", det.interfaceNum, det.name.c_str());
            p_sceUsbdClose(h);
            releaseSlot(slot);
            g_ignored.insert(key);
            continue;
        }

        auto d = std::make_unique<Device>();
        d->handle = h;
        d->key = key;
        d->bus = bus;
        d->addr = addr;
        d->vid = desc.idVendor;
        d->pid = desc.idProduct;
        d->name = det.name;
        d->where = whereName(bus, addr, desc.idVendor, desc.idProduct);
        d->protocol = det.protocol;
        d->ps3 = det.ps3;
        auto l = std::make_unique<Link>();
        l->iface = {det.protocol, det.interfaceNum, det.epIn, det.epOut, false};
        l->slot = slot;
        // Known only from its docs (a PS3 instrument, not yet seen on a console) or by a guess (a
        // generic HID pad's layout read off its descriptor): the X-ray, and its reports logged for
        // a while.
        if (det.protocol == UsbPadProtocol::Ps3Instrument || det.protocol == UsbPadProtocol::GenericHid) {
            uint8_t config[1024] = {};
            const size_t configLen = readConfigDescriptor(h, d->where, config, sizeof config);
            const std::string product = stringDescriptor(h, desc.iProduct);
            if (det.protocol == UsbPadProtocol::Ps3Instrument) {
                if (!product.empty()) d->name = product;
                XC_LOGI("usb pad: %s: %s (%04x:%04x)", d->where.c_str(), ps3InstrumentName(det.ps3), d->vid, d->pid);
                xrayDescribe(h, d->where, desc, config, configLen, "a PS3 instrument, not yet confirmed on a console");
                l->xray.start(d->where, d->name, platform::nowMs(), false);
            } else {
                xrayDescribe(h, d->where, desc, config, configLen, "a generic HID pad, its layout guessed from its descriptor");
                l->xray.start(d->where, product.empty() ? d->name : product, platform::nowMs());
            }
        }
        d->links.push_back(std::move(l));
        startDevice(std::move(d));
    }

    for (auto it = g_ignored.begin(); it != g_ignored.end();) {
        if (presentKeys.count(*it) == 0) it = g_ignored.erase(it);
        else ++it;
    }

    p_sceUsbdFreeDeviceList(list);
}

void scanLoop() {
    platform::setThreadAffinity(platform::ThreadRole::Worker, "ps4_usb_scan");
    XC_LOGI("usb pad: starting PS4 USB scan loop");
    while (g_running) {
        scan();
        for (int i = 0; i < 15 && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& d : g_devices) closeDevice(*d);
    g_devices.clear();
    g_ignored.clear();
    XC_LOGI("usb pad: PS4 USB scan loop terminated");
}

}  // namespace

void setUsbPadSlotRumble(int slot, uint8_t large, uint8_t small) {
    if (slot < 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& d : g_devices) {
        for (auto& l : d->links) {
            if (l->slot != slot) continue;
            uint32_t val = (static_cast<uint32_t>(large) << 8) | small;
            l->pendingRumble.store(val, std::memory_order_relaxed);
            l->hasPendingRumble.store(true, std::memory_order_release);
            return;
        }
    }
}

void usbPadsStart() {
    if (!usbPadsEnabled()) {
        XC_LOGI("usb pad: USB controllers driver disabled in settings");
        return;
    }
    if (g_running.exchange(true)) return;
    if (!loadUsbd()) {
        g_running = false;
        return;
    }
    XC_LOGI("usb pad: looking for controllers on PS4 USB (libSceUsbd)");
    g_scanner = std::thread(scanLoop);
}

void usbPadsStop() {
    if (!g_running.exchange(false)) return;
    if (g_scanner.joinable()) g_scanner.join();
    if (p_sceUsbdExit) {
        p_sceUsbdExit();
    }
    if (g_hUsbd > 0) {
        int res = 0;
        sceKernelStopUnloadModule(g_hUsbd, 0, nullptr, 0, nullptr, &res);
        g_hUsbd = 0;
    }
}

}  // namespace xc::input
