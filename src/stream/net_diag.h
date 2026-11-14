// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Diagnostics for freezes of the stream (all packets stop, then arrive at
// once). On only while <dataDir>/netdiag.txt exists. It tells where the
// packets were during a pause:
//   - the system's queue of the stream's UDP socket, sampled every 2 ms:
//     data waiting there means the packets reached the console and the app
//     did not read them;
//   - beacons a PC on the same network sends to UDP 9123, in four Wi-Fi
//     priorities (tools/netdiag/beacon.py): if they keep arriving while the
//     stream is stopped, the console's Wi-Fi is receiving fine.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xc::stream::netdiag {

// Reads <dataDir>/netdiag.txt again; at each stream start. Options in the
// file: "afk=N", seconds between the anti-AFK taps (default 90, 0 = none);
// "abaudio=N", the audio's handling of network holds new and old in turns of
// N seconds (0, the default: always new); "abfiller=N", likewise the Wi-Fi filler.
void refresh();
bool enabled();
int afkSeconds();
int abAudioSeconds();
// "abfiller=N": the Wi-Fi filler (stream/wifi_filler.h) on and off in turns of N seconds (0: as usual).
int abFillerSeconds();

// Samples the queue of the UDP socket bound to `localPort` (the stream's),
// until stopWatch(); starts the beacon listener the first time.
void watchSocket(uint16_t localPort);
void stopWatch();

// What the socket's queue and the beacons did between t0 and t1
// (platform::nowUs()).
struct Window {
    size_t samples = 0, withData = 0;  // queue samples inside, and those with bytes waiting
    uint32_t maxBytes = 0;
    int64_t firstDataMs = -1;          // first sample with data, from t0
    bool watched = false;
    bool anyBeacons = false;           // any beacon heard recently
    int beaconsIn[4] = {};             // per priority (BE, BK, VI, VO): arrived inside
    uint64_t beaconWaitMs[4] = {};     // longest wait between two arrivals across the window
    std::string text;                  // all of it, for the log
};
Window describeWindow(uint64_t t0Us, uint64_t t1Us);

}  // namespace xc::stream::netdiag
