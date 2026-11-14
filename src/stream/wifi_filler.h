// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Keeps a Wi-Fi link's downstream busy while the stream is light. A Wi-Fi link holds the packets
// behind a late one until about 64 more have come (its block-ack window), so the freeze lasts as
// long as those take to arrive: 300-600 ms at a lobby's 100-200 packets a second, 60-90 ms in a
// song (diagnosed 2026-11-13, stream/net_diag.h). 500 more packets a second cut those freezes to a
// third in the A/B. They come from the home router: connection attempts to one of its closed ports
// (1, 9, 8080), each refused at once by its kernel with one small packet back (the console keeps
// apps off DNS: its answers never came). Nothing leaves the home: only a router that refuses in a
// few ms is used. The stream asks for a rate (worker_stream: only after such freezes, while light).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xc::stream {

// Starts its thread and looks for the router (the local network's .1, then .254) and a closed port
// of it that it refuses at once; stop() ends it. Without one, it stays idle (logged once).
void wifiFillerStart();
void wifiFillerStop();
// Refusals (packets back) wanted a second (0: none); capped at kWifiFillerMax.
void wifiFillerSetRate(int perSecond);
// Whether a router to ask was found.
bool wifiFillerReady();
constexpr int kWifiFillerMax = 500;

}  // namespace xc::stream
