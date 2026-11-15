// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "stream/wifi_filler.h"
#include <cstddef>
#include <cstdint>

#include "net/socket.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace xc::stream {

namespace {
// A router that refuses in this (median) is the home's own (it answers a
// refused connection at once, from its kernel; over a busy Wi-Fi as the stream
// starts, 7 ms was seen); farther, not used.
constexpr double kLocalMs = 25.0;
// Looked for again this often, this many times, when not found (the stream's
// start is busy).
constexpr int kProbeTries = 5;
constexpr int kProbeEverySeconds = 20;
// Closed ports asked (refused at once on home routers); the first one refused
// is used.
constexpr uint16_t kPorts[] = {1, 9, 8080};

std::mutex g_mutex; // start / stop
std::thread g_thread;
std::atomic<bool> g_running{false}, g_ready{false};
std::atomic<int> g_rate{0};

// The address the default route leaves by: a UDP socket "connected" to a public
// address (nothing is sent), then asked its own.
uint32_t localAddress() {
  xc::net::Socket s = xc::net::createUdpSocket();
  if (!s.valid())
    return 0;
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(53);
  xc::net::parseIpv4Address("1.1.1.1", &to.sin_addr);
  uint32_t me = 0;
  if (xc::net::connectSocket(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to))) {
    sockaddr_in a{};
    socklen_t l = sizeof(a);
#if defined(_WIN32)
    if (::getsockname(static_cast<SOCKET>(s.fd), reinterpret_cast<sockaddr*>(&a), &l) == 0)
      me = ntohl(a.sin_addr.s_addr);
#else
    if (::getsockname(static_cast<int>(s.fd), reinterpret_cast<sockaddr*>(&a), &l) == 0)
      me = ntohl(a.sin_addr.s_addr);
#endif
  }
  xc::net::closeSocket(s);
  return me;
}

uint64_t nowUs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// A connection attempt, not waited for: its socket, invalid if it could not start.
xc::net::Socket startAttempt(const sockaddr_in& to) {
  xc::net::Socket s = xc::net::createTcpSocket();
  if (!s.valid())
    return xc::net::kInvalidSocket;
  xc::net::setNonBlocking(s, true);
  if (xc::net::connectSocket(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) ||
      !xc::net::inProgress(xc::net::lastSocketError())) {
    xc::net::closeSocket(s);
    return xc::net::kInvalidSocket;
  }
  return s;
}

// Whether an attempt ended (refused: true in `refused`).
bool attemptDone(xc::net::Socket s, bool& refused) {
  xc::net::Socket writeSock = s;
  if (xc::net::pollSockets(nullptr, 0, &writeSock, 1, 0) <= 0)
    return false;
  int err = 0;
  xc::net::getSocketError(s, err);
#if defined(_WIN32)
  refused = (err == WSAECONNREFUSED);
#else
  refused = (err == ECONNREFUSED);
#endif
  return true;
}

// Ten attempts at `to`, one after the other: the median time to the refusal
// (ms), -1 if fewer than eight were refused within 50 ms.
double probe(const sockaddr_in& to) {
  std::vector<double> times;
  for (int k = 0; k < 10 && g_running; ++k) {
    const uint64_t at = nowUs();
    xc::net::Socket s = startAttempt(to);
    if (!s.valid())
      continue;
    bool refused = false;
    while (nowUs() - at < 50000 && !attemptDone(s, refused))
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    xc::net::closeSocket(s);
    if (refused)
      times.push_back(static_cast<double>(nowUs() - at) / 1000.0);
  }
  if (times.size() < 8)
    return -1;
  std::sort(times.begin(), times.end());
  return times[times.size() / 2];
}

void loop() {
  const uint32_t me = localAddress();
  if (!me) {
    XC_LOGI("wifi filler: no local address: off");
    return;
  }
  sockaddr_in router{};
  bool found = false;
  for (int attempt = 0; attempt < kProbeTries && !found && g_running;
       ++attempt) {
    if (attempt)
      for (int k = 0; k < kProbeEverySeconds * 10 && g_running; ++k)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (uint32_t host : {1u, 254u}) {
      const uint32_t addr = (me & 0xFFFFFF00u) | host;
      if (addr == me)
        continue;
      char ip[INET_ADDRSTRLEN] = {};
      for (uint16_t port : kPorts) {
        router = {};
        router.sin_family = AF_INET;
        router.sin_port = htons(port);
        router.sin_addr.s_addr = htonl(addr);
        inet_ntop(AF_INET, &router.sin_addr, ip, sizeof ip);
        const double ms = probe(router);
        if (ms < 0)
          continue;
        XC_LOGI("wifi filler: %s refuses port %u in %.1f ms%s", ip, port, ms,
                ms <= kLocalMs ? " (used)" : " (too far: not used)");
        if (ms <= kLocalMs)
          found = true;
        break;
      }
      if (found)
        break;
      XC_LOGI("wifi filler: %s refuses none of its closed ports", ip);
    }
  }
  if (!found)
    return;
  g_ready = true;
  struct Pending {
    xc::net::Socket sock;
    uint64_t at;
  };
  std::vector<Pending> pending;
  double carry = 0;
  int lastRate = 0, transitions = 0;
  uint64_t sent = 0, refusedCount = 0, windowStart = nowUs(), pausedUntil = 0;
  while (g_running) {
    const uint64_t now = nowUs();
    const int rate =
        now >= pausedUntil ? g_rate.load(std::memory_order_relaxed) : 0;
    if ((rate > 0) != (lastRate > 0) && transitions++ < 100)
      XC_LOGI("wifi filler: %s",
              rate > 0 ? ("on, " + std::to_string(rate) + " a second").c_str()
                       : "off");
    lastRate = rate;
    if (rate > 0) {
      carry += rate / 100.0; // every 10 ms
      for (int n = static_cast<int>(carry); n > 0 && pending.size() < 256;
           --n, carry -= 1.0) {
        if (xc::net::Socket s = startAttempt(router); s.valid())
          pending.push_back({s, now}), ++sent;
      }
      carry = std::min(carry, 1.0);
    } else {
      carry = 0;
    }
    // The refusals that came back; an attempt left unanswered for 300 ms is let
    // go.
    for (size_t i = 0; i < pending.size();) {
      bool refused = false;
      if (attemptDone(pending[i].sock, refused) || now - pending[i].at > 300000) {
        refusedCount += refused;
        xc::net::closeSocket(pending[i].sock);
        pending[i] = pending.back();
        pending.pop_back();
      } else {
        ++i;
      }
    }
    // A router that stops refusing (it started dropping them): left alone for a
    // minute.
    if (now - windowStart >= 1000000) {
      if (sent >= 50 && refusedCount * 2 < sent) {
        XC_LOGW(
            "wifi filler: the router refused %llu of %llu: paused for a minute",
            static_cast<unsigned long long>(refusedCount),
            static_cast<unsigned long long>(sent));
        pausedUntil = now + 60000000;
      }
      sent = refusedCount = 0, windowStart = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  for (Pending &p : pending)
    xc::net::closeSocket(p.sock);
}
} // namespace

void wifiFillerStart() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_running)
    return;
  g_rate = 0, g_ready = false, g_running = true;
  g_thread = std::thread(loop);
}

void wifiFillerStop() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_running)
    return;
  g_running = false;
  if (g_thread.joinable())
    g_thread.join();
  g_rate = 0, g_ready = false;
}

void wifiFillerSetRate(int perSecond) {
  g_rate = std::clamp(perSecond, 0, kWifiFillerMax);
}

bool wifiFillerReady() { return g_ready.load(std::memory_order_relaxed); }

} // namespace xc::stream
