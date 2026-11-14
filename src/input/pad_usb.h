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
    GenericHid,
    Ps3Instrument  // a PlayStation 3 instrument's dongle (Ps3Instrument below)
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
// A device linked before the app started sends no link report again; the capabilities report it
// answers with (00 05 ..., asked with 00 00 02 80) names its maker and product instead (bytes
// 10..13, little-endian). For the instrument makers' products the subtype is known from them:
// Activision / RedOctane (1430) put it in the product's high byte (a Guitar Hero 5 guitar is 0705,
// subtype 07, as its link report says); Harmonix (1bad) Rock Band guitar 0002 and drums 0003.
// -1 for anything else (a pad stays a pad).
int xinputSubtypeFromCapabilities(const uint8_t* data, size_t len);

// PlayStation 3 instruments, through their USB dongles (TheNathannator's PlasticBand docs, Linux's
// hid-sony, RPCS3, Santroller): Guitar Hero guitars (12ba:0100, the World Tour PC dongle 1430:474c),
// Guitar Hero Live (12ba:074b), Rock Band guitars (12ba:0200..020f, Nyko's and PowerGig's too),
// Guitar Hero drums (12ba:0120), Rock Band drums (12ba:0210..021f, the MIDI Pro Adapter's drums
// mode included) and the DJ Hero turntable (12ba:0140). Their reports (27 bytes, no report id) are
// read into the Xbox 360 instruments' layout, so they play as those do: the frets as A B Y X LB
// (green .. orange), the strum bar as the D-pad, Select as Back (star power), Start, PS as the
// Xbox button, the whammy on the right stick's X and the tilt on its Y, higher as the neck goes up
// (a Rock Band guitar's tilt is a switch: -1 or 1). Drum pads: green A, red B, yellow Y, blue X,
// orange (a Guitar Hero cymbal) RB, kick LB, Rock Band's pad and cymbal flags as L3 and R3.
// Not yet confirmed on a console: their reports go to the X-ray too (usb_xray.h).
enum class Ps3Instrument { None, GhGuitar, GhLiveGuitar, RbGuitar, GhDrums, RbDrums, Turntable };
Ps3Instrument ps3InstrumentFor(uint16_t vid, uint16_t pid);
UsbPadKind ps3InstrumentKind(Ps3Instrument instrument);
const char* ps3InstrumentName(Ps3Instrument instrument);
bool parsePs3InstrumentReport(Ps3Instrument instrument, const uint8_t* data, size_t len, ControllerState& s);
// Guitar Hero Live's dongle lets held frets go without this output report every 10 s (SET_REPORT,
// value 0x0202; Linux and Santroller send it every 8 s).
constexpr uint8_t kGhLiveKeepAlive[8] = {0x02, 0x08, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint64_t kGhLiveKeepAliveMs = 8000;
const char* xinputSubtypeName(int subtype);
// Every pad interface of an Xbox 360 wireless receiver (FF/5D/81: one per device it can link,
// up to four), with its endpoints, in order.
std::vector<UsbPadInterface> findXInputWirelessInterfaces(const uint8_t* configDesc, size_t len);

UsbPadKind usbPadKindForSubtype(int subtype);  // -1 (not known): a gamepad
const char* usbPadKindName(UsbPadKind kind);

// The buttons a guitar's lane or its star power can send: those Fortnite Festival binds, not
// the D-pad's up and down (the strum bar's) nor Start, Back and Xbox (the menus').
enum class GuitarOut : uint8_t { A, B, X, Y, LB, RB, LT, RT, L3, R3, DLeft, DRight, Count };
const char* guitarOutName(GuitarOut b);   // as shown: "A", "LB", "D-pad ←"...
// What each lane (green .. orange) and star power send, so what to bind in the game.
struct GuitarBinding {
    GuitarOut lanes[5];
    GuitarOut starPower;
    bool usesForLane(GuitarOut b) const {
        for (GuitarOut l : lanes)
            if (l == b) return true;
        return false;
    }
    bool operator==(const GuitarBinding& o) const {
        for (int i = 0; i < 5; ++i)
            if (lanes[i] != o.lanes[i]) return false;
        return starPower == o.starPower;
    }
};
// The defaults: without strum the frets as they come; with it, lanes the menus don't use. Star
// power on A in both, Overdrive's own button in Fortnite Festival's default controls (no
// remapping there; its A also comes from the green fret, as on a pad).
constexpr GuitarBinding kFretLanes{{GuitarOut::A, GuitarOut::B, GuitarOut::Y, GuitarOut::X, GuitarOut::LB}, GuitarOut::A};
constexpr GuitarBinding kStrumLanes{{GuitarOut::LT, GuitarOut::LB, GuitarOut::RB, GuitarOut::RT, GuitarOut::R3},
                                    GuitarOut::A};
// The bindings in use for each layout (Settings), every guitar's.
void setGuitarBinding(bool strum, const GuitarBinding& b);
GuitarBinding guitarBinding(bool strum);
// As kept in the settings: "A,B,Y,X,LB,RT" (lanes, then star power); false if it is not one.
std::string guitarBindingText(const GuitarBinding& b);
bool guitarBindingFromText(const std::string& text, GuitarBinding& b);
// Instruments in a game. The cloud only takes gamepads, so an instrument is passed on as one:
//   guitar: the strum bar as the D-pad, Start and the Xbox button as they are, nothing on the
//           sticks (the whammy bar rests at full left and the tilt sensor moves all the time:
//           they turned the camera), and
//           - without `strum`: each fret as it comes, on its lane's button (kFretLanes by
//             default: green A, red B, yellow Y, blue X, orange LB), star power (Back, or the
//             guitar raised) on its own (A, as Fortnite Festival's Overdrive);
//           - with `strum`: the frets as A B Y X for the menus (star power's one, A by default,
//             only as a tap: a fret let go after a brief press with no strum; not a button a lane
//             uses), and the five lanes on their buttons (kStrumLanes by default: LT LB RB RT R3)
//             only once the bar is strummed while the fret is held, held while it is; star power
//             on its own (A). In Fortnite Festival the lanes are bound to those, so the menus
//             never wait for a strum.
//   drums:  the buttons as they come, nothing on the sticks (the pads' strengths).
// `prevStrum` / `armed` carry the strum state between reports.
struct InstrumentState {
    bool prevStrum = false;
    bool armed[5] = {};
    // A fret held through another strum is let go for a moment (until this time, ms), so the game
    // sees a new note: held frets can be strummed again and again.
    uint64_t gapUntil[5] = {};
    bool tiltUp = false;  // star power by raising the guitar, with some hysteresis
    uint64_t tiltAboveSinceMs = 0;  // ...raised since (+ 1), not yet for kTiltHoldMs
    float tiltRest = 0;             // ...the reading of the position it is played in
    bool tiltRestKnown = false;
    uint64_t tiltLastMs = 0, tiltUpSinceMs = 0, tiltSettleUntil = 0;
    bool starPower = false;  // as last sent (logged when it changes)
    // With strum, the frets reach the menus as A B Y X (held as they are held: Fortnite asks to hold
    // Y to start a song), but the one star power uses (A by default: Fortnite Festival's Overdrive,
    // which every green note fired) only as a tap: sent as the fret is let go, when it was down
    // briefly (kMenuTapMs) with no strum along or just before (kStrumBeforeFretMs), held
    // kMenuTapHoldMs (applyGuitarTaps).
    bool fretDown[4] = {}, fretStrummed[4] = {};
    uint64_t fretDownMs[4] = {}, menuTapUntil[4] = {}, lastStrumMs = 0;
    // Star power (raised, or Back) is a tap too, as it begins: kStarPowerTapMs of its button, not
    // held while the guitar stays up (Fortnite fires Overdrive as the button is let go).
    uint64_t starTapUntil = 0;
    bool starPowerTapped = false;  // star power on at the last report (its tap given)
    bool strumLayout = false;  // the last state was made with `strum` (the lanes on their own buttons)
    GuitarBinding binding = kStrumLanes;  // ...and with these
};
constexpr uint64_t kStrumGapMs = 30;
// `nowMs`: when the report came. The guitar's tilt (right stick Y, as read) `tiltBy` or more above
// the position it is played in, for kTiltHoldMs (with `tiltOn`), is star power too. A Guitar Hero 5
// guitar reads -0.95 to -0.4 held to play, depending on how it is held, and goes up as its neck is
// raised: the position is learnt (the reading's average over about kTiltRestMs while not raised).
// How far above that a lift counts, by sensitivity (Settings: guitarRaiseBy): a lift mid-song is
// smaller and quicker than one made on purpose (0.3 to 0.6 seen on a Guitar Hero 5 guitar).
constexpr float kGuitarRaiseLess = 0.45f, kGuitarRaiseBy = 0.32f, kGuitarRaiseMore = 0.22f;
constexpr uint64_t kTiltHoldMs = 60;
constexpr float kTiltRestMs = 4000.0f, kTiltFarRestMs = 20000.0f;
constexpr uint64_t kTiltSettleMs = 2000;  // after the first reading: the position learnt, no lift
constexpr uint64_t kTiltMaxMs = 5000;  // raised longer than this: held another way, the new position
ControllerState instrumentForGame(UsbPadKind kind, const ControllerState& in, InstrumentState& st, bool strum,
                                  uint64_t nowMs = 0, bool tiltOn = false, float tiltBy = kGuitarRaiseBy);
// The frets let go for a moment after a repeated strum, at the time the game reads them.
void applyStrumGaps(ControllerState& game, const InstrumentState& st, uint64_t nowMs);
// A guitar's taps, at the time the game reads them: star power's (InstrumentState::starTapUntil) and,
// with strum, the fret's menu tap (menuTapUntil).
void applyGuitarTaps(ControllerState& game, const InstrumentState& st, uint64_t nowMs);
constexpr uint64_t kMenuTapMs = 400, kMenuTapHoldMs = 80, kStrumBeforeFretMs = 150, kStarPowerTapMs = 120;
// The guitar's strum option (Settings), for every guitar.
void setGuitarStrum(bool on);
bool guitarStrum();
// Star power by raising the guitar: on / off, and how far above the position it is played in the
// tilt reading (right stick Y as read) must go (kGuitarRaiseBy by default).
void setGuitarTilt(bool on, float at);
bool guitarTilt();
float guitarRaiseBy();  // ...how far, as set
void setUsbPadRumble(int player, uint8_t large, uint8_t small);
void setUsbPadSlotRumble(int slot, uint8_t large, uint8_t small);

// PS4 & PS5: starts and stops looking for USB pads. Not defined on the other platforms.
void usbPadsStart();
void usbPadsStop();

}  // namespace xc::input

