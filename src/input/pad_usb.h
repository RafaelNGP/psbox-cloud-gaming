// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Controllers the PS5 itself does not take (Xbox One / Series pads on a
// cable, 8BitDo and other XInput pads and their 2.4G dongles), read by the
// app straight from USB. Every one drives the app's menus; in a game each
// plays as a player of its own (usbPadPlayer).
//
// The reports are parsed here, on every platform (the tests feed them);
// reading them off USB is the platform's (pad_usb_ps5.cpp: /dev/ugen,
// after OmniPad CE, github.com/jpemendonca/OmniPad-CE, GPL-3.0). Elsewhere
// there is no transport and no USB pad.
#pragma once

#include "input/controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xc::input {

// What a pad speaks, from its USB interface (class 0xFF):
//   XInput: subclass 0x5D, protocol 0x01 (Xbox 360 and its clones: 8BitDo,
//           GameSir, Machenike... in their PC mode);
//   GIP:    subclass 0x47, protocol 0xD0 (Xbox One and Series pads).
enum class UsbPadProtocol {
    None,
    XInput,
    Gip,
    DualShock3,
    DualShock4,
    DualSense,
    SwitchPro,
    SwitchInputOnly,
    Stadia,
    GenericHid
};
// What a USB pad is, for its icon and for what reaches the game.
enum class UsbPadKind { Gamepad, Guitar, Drums, Other };
const char* usbPadProtocolName(UsbPadProtocol protocol);
UsbPadProtocol usbPadProtocolFor(uint8_t ifaceClass, uint8_t ifaceSubclass, uint8_t ifaceProtocol);

// Returns true if interface class/subclass/protocol indicates a boot keyboard or mouse.
bool isBootKeyboardOrMouse(uint8_t ifaceClass, uint8_t ifaceSubclass, uint8_t ifaceProtocol);

// Returns true if HID report descriptor defines a top-level collection for Mouse (0x01:0x02),
// Pointer (0x01:0x01), or Keyboard (0x01:0x06 / page 0x07).
bool isKeyboardOrMouseReportDescriptor(const uint8_t* reportDesc, size_t len);

// Returns true if HID report descriptor defines a top-level collection for Gamepad (0x01:0x05),
// Joystick (0x01:0x04), or Game Controls (page 0x05).
bool isGamepadReportDescriptor(const uint8_t* reportDesc, size_t len);

// The pad's interface in a configuration descriptor (as USB_GET_FULL_DESC
// gives it): the first XInput / GIP one (alternate setting 0) and its
// interrupt endpoints. protocol None when the device has none.
// If reportDesc is provided, filters out keyboard and mouse HID devices.
struct UsbPadInterface {
    UsbPadProtocol protocol = UsbPadProtocol::None;
    uint8_t number = 0;
    uint8_t epIn = 0, epOut = 0;  // endpoint addresses (epIn has bit 7); 0: none
    // The Xbox 360 wireless receiver (XInput protocol 0x81): its reports carry a 4-byte header
    // and presence messages, its lights and motors take 12-byte packets of their own.
    bool wireless = false;
};
UsbPadInterface findUsbPadInterface(const uint8_t* configDesc, size_t len,
                                    const uint8_t* reportDesc = nullptr, size_t reportLen = 0);

// Probes a USB device against PS5 gamepad acceptance rules:
// Rejects keyboards, mice, audio, and Bluetooth radios.
// Accepts DualShock 4 (VID 0x054C with gamepad report / proto 0), Xbox 360 (XInput), and Xbox One (GIP).
UsbPadInterface probeUsbPadPs5(uint16_t vid, uint16_t pid, const uint8_t* configDesc, size_t configLen,
                              const uint8_t* reportDesc = nullptr, size_t reportLen = 0);

// Probes a USB device against PS4 gamepad acceptance rules:
// Rejects keyboards, mice (boot protocol and report descriptor).
// Accepts DualShock 4 HID gamepads, DualShock 3, DualSense, Switch Pro, Stadia, Xbox 360, Xbox One.
bool probeUsbPadPs4(uint16_t vid, uint16_t pid, const uint8_t* configDesc, size_t configLen,
                    const uint8_t* reportDesc = nullptr, size_t reportLen = 0);

// GIP: the acknowledgements (13 bytes each) a report asks for, one per
// message in it with the ACK option (0x10); the pad repeats them otherwise.
std::vector<std::array<uint8_t, 13>> gipAcksFor(const uint8_t* data, size_t len);

// One report into `state`, as read: sticks -1..1 (+y down) with no dead
// zone, triggers 0..1. `state` comes in with the pad's last state: a
// report that carries only some of the buttons (GIP's Xbox button) leaves
// the rest as they were. False for a report that is not input (LED,
// rumble and status reports, GIP's announce / keep-alive...).
// XInput takes both the wired report (00 14 ...) and the wireless receiver's: its presence
// message (08 80: a pad connected; 08 00: gone, every button let go) and its input (00 01 00 F0
// then the wired report).
bool parseXInputReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseGipReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseDualShock3Report(const uint8_t* data, size_t len, ControllerState& state);
bool parseDualShock4Report(const uint8_t* data, size_t len, ControllerState& state);
bool parseDualSenseReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseSwitchProReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseSwitchInputOnlyReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseStadiaReport(const uint8_t* data, size_t len, ControllerState& state);
bool parseUsbPadReport(UsbPadProtocol protocol, const uint8_t* data, size_t len, ControllerState& state);

// The transport tells the shared code about each pad (slot 0..kMaxUsbPads-1).
constexpr int kMaxUsbPads = 4;
void usbPadAttached(int slot, const std::string& name, UsbPadKind kind = UsbPadKind::Gamepad);
void usbPadSetKind(int slot, UsbPadKind kind);  // learned after it attached
void usbPadState(int slot, const ControllerState& state);
void usbPadDetached(int slot);

// Players (0..kMaxPads-1). A pad takes the first free one as it connects,
// in the order they come (free: no DualSense connected there and no other
// USB pad on it), from the first pollUsbPad() after it came. Holding its
// Xbox / Home button, LB and RB move it to the player before / after; the
// button alone (let go without them) asks for the in-game menu, for that
// pad (UsbPadEvent::MenuButton). It never reaches the game itself, and
// LB / RB do not while it is held.
int usbPadPlayer(int slot);  // -1: no pad, or not given a player yet
void setUsbPadPlayer(int slot, int player);  // the in-game menu's choice
std::string usbPadName(int slot);

// What the app acts on, oldest first: notifications, and the in-game menu.
struct UsbPadEvent {
    enum class Kind { Connected, Moved, Disconnected, MenuButton } kind = Kind::Connected;
    int player = 0;
    std::string name;
    int slot = -1;
};
std::vector<UsbPadEvent> takeUsbPadEvents();

// The USB pads connected, added up, with the dead zones and the Circle /
// Cross swap applied as on the DualSense: every one (the menus), those
// playing as `player` but `except` (a slot whose pad has the in-game
// menu), or the one in `slot`. False (and `out` cleared) when there is none.
bool pollUsbPad(ControllerState& out);
bool pollUsbPadsFor(int player, ControllerState& out, int except = -1);
bool pollUsbPadSlot(int slot, ControllerState& out);
int usbPadCount();
// After the in-game menu closes: each pad stays out of the game until its
// buttons are let go (the one that closed the menu would press it there).
void usbPadsWaitRelease();
// The name of the first USB pad playing as `player` (empty: none), and
// when one of them was last used (ms; 0: never).
std::string usbPadNameFor(int player);
uint64_t usbPadActiveAt(int player);
UsbPadKind usbPadKindFor(int player);
UsbPadKind usbPadSlotKind(int slot);  // the first USB pad playing as `player` (Gamepad: none)

void setUsbPadsEnabled(bool enabled);
bool usbPadsEnabled();
// The packet that sets a pad's two motors (0..255 each), for its interrupt OUT endpoint: its
// length, 0 when that kind of pad has none known. `sequence` is the pad's own counter (GIP
// numbers its messages).
size_t usbPadRumblePacket(UsbPadProtocol protocol, uint8_t large, uint8_t small, uint8_t& sequence, uint8_t* out,
                          size_t capacity, bool wireless = false);
// The Xbox 360 wireless receiver's packets (12 bytes): the pad's lights showing `player`
// (0..3), and the question "is a pad connected?" (it answers with a presence message).
std::array<uint8_t, 12> xinputWirelessLedPacket(int player);
std::array<uint8_t, 12> xinputWirelessPresenceQuery();
// What kind of device an XInput pad is (XINPUT_DEVSUBTYPE): a pad, a guitar, a drum kit...
// A receiver tells it in the link report a device sends as it connects (00 0F 00 F0 F0 CC ...,
// byte 25, bit 7 the motors flag; layout after x360-research's "argon" notes and the Linux xpad
// driver). -1 when `data` is not that report.
int xinputWirelessSubtype(const uint8_t* data, size_t len);
const char* xinputSubtypeName(int subtype);
// Every pad interface of an Xbox 360 wireless receiver (FF/5D/81: one per device it can link,
// up to four), with its endpoints, in order.
std::vector<UsbPadInterface> findXInputWirelessInterfaces(const uint8_t* configDesc, size_t len);

UsbPadKind usbPadKindForSubtype(int subtype);  // -1 (not known): a gamepad
const char* usbPadKindName(UsbPadKind kind);

// Instruments in a game. The cloud only takes gamepads, so an instrument is passed on as one:
//   guitar: the frets as they come (green A, red B, yellow Y, blue X, orange LB), the strum bar
//           as the D-pad, Start and the Xbox button as they are, Back (star power) as RT, and
//           nothing on the sticks (the whammy bar rests at full left and the tilt sensor moves
//           all the time: they turned the camera). With `strum`, a fret reaches the game only
//           once the bar is strummed while it is held, and stays down while it is held.
//   drums:  the buttons as they come, nothing on the sticks (the pads' strengths).
// `prevStrum` / `armed` carry the strum state between reports.
struct InstrumentState {
    bool prevStrum = false;
    bool armed[5] = {};
};
ControllerState instrumentForGame(UsbPadKind kind, const ControllerState& in, InstrumentState& st, bool strum);
// The guitar's strum option (Settings), for every guitar.
void setGuitarStrum(bool on);
bool guitarStrum();
void setUsbPadRumble(int player, uint8_t large, uint8_t small);
void setUsbPadSlotRumble(int slot, uint8_t large, uint8_t small);

// PS4 & PS5: starts and stops looking for USB pads. Not defined on the other platforms.
void usbPadsStart();
void usbPadsStop();

}  // namespace xc::input

