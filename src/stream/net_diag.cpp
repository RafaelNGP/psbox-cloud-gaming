// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstddef>
#include <cstdint>
#include "stream/net_diag.h"

#include "net/socket.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace xc::stream::netdiag {

namespace {
std::atomic<bool> g_enabled{false};
std::atomic<int> g_afk{90};
std::atomic<int> g_abAudio{0};
std::atomic<int> g_abFiller{0};
}  // namespace

void refresh() {
    const std::string path = platform::dataDir() + "/netdiag.txt";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        g_enabled = false;
        return;
    }
    char buf[256] = {};
    const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    buf[n] = 0;
    int afk = 90;
    if (const char* a = std::strstr(buf, "afk=")) afk = std::atoi(a + 4);
    g_afk = std::clamp(afk, 0, 3600);
    int abAudio = 0;
    if (const char* a = std::strstr(buf, "abaudio=")) abAudio = std::atoi(a + 8);
    g_abAudio = std::clamp(abAudio, 0, 3600);
    int abFiller = 0;
    if (const char* a = std::strstr(buf, "abfiller=")) abFiller = std::atoi(a + 9);
    g_abFiller = std::clamp(abFiller, 0, 3600);
    if (!g_enabled.exchange(true)) XC_LOGI("netdiag: on (%s), anti-AFK every %d s, audio A/B %d s", path.c_str(), g_afk.load(), g_abAudio.load());
}

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
int afkSeconds() { return g_afk.load(std::memory_order_relaxed); }
int abAudioSeconds() { return g_abAudio.load(std::memory_order_relaxed); }
int abFillerSeconds() { return g_abFiller.load(std::memory_order_relaxed); }

namespace {
constexpr uint16_t kBeaconPort = 9123;
constexpr const char* kClass[4] = {"BE", "BK", "VI", "VO"};
constexpr size_t kQueueRing = 8192;   // a sample every 2 ms: ~16 s
constexpr size_t kBeaconRing = 8192;  // 4 x 25 a second: ~80 s

struct QueueSample {
    uint64_t us = 0;
    int32_t bytes = 0;  // -1: read failed
};
struct BeaconHit {
    uint64_t us = 0;
    uint32_t seq = 0;
    uint8_t cls = 0;
};

std::mutex g_mutex;
std::vector<QueueSample> g_queue(kQueueRing);
size_t g_queueNext = 0;
std::vector<BeaconHit> g_beacons(kBeaconRing);
size_t g_beaconNext = 0;
uint64_t g_lastBeaconUs = 0;

std::mutex g_watchMutex;
std::atomic<bool> g_watching{false};
std::thread g_watchThread;
std::atomic<bool> g_beaconStarted{false};

uint16_t portOf(xc::net::Socket s) {
    int port = 0;
    if (xc::net::getSocketPort(s, port) && port > 0) return static_cast<uint16_t>(port);
    return 0;
}

bool isUdp(xc::net::Socket s) {
#if defined(_WIN32)
    int type = 0;
    int tl = sizeof(type);
    return getsockopt(static_cast<SOCKET>(s.fd), SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &tl) == 0 && type == SOCK_DGRAM;
#else
    int type = 0;
    socklen_t tl = sizeof(type);
    return getsockopt(static_cast<int>(s.fd), SOL_SOCKET, SO_TYPE, &type, &tl) == 0 && type == SOCK_DGRAM;
#endif
}

xc::net::Socket findSocket(uint16_t port, std::string& seen) {
    for (int fd = 0; fd < 4096; ++fd) {
        xc::net::Socket s(fd);
        if (!isUdp(s)) continue;
        const uint16_t p = portOf(s);
        char b[32];
        std::snprintf(b, sizeof b, " fd %d port %u", fd, p);
        seen += b;
        if (p == port) return s;
    }
    return xc::net::kInvalidSocket;
}

void watchLoop(xc::net::Socket sock) {
    uint64_t nextCheck = platform::nowUs() + 1000000;
    while (g_watching.load(std::memory_order_relaxed)) {
        size_t availBytes = 0;
        const bool ok = xc::net::bytesAvailable(sock, availBytes);
        const int n = ok ? static_cast<int>(availBytes) : -1;
        const uint64_t now = platform::nowUs();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_queue[g_queueNext++ % kQueueRing] = {now, n};
        }
        // The socket closed under us (the session ended): stop.
        if (now >= nextCheck) {
            nextCheck = now + 1000000;
            if (!ok) {
                XC_LOGW("netdiag: socket queue of fd %ld unreadable: stopped", static_cast<long>(sock.fd));
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void beaconLoop() {
    xc::net::Socket s = xc::net::createUdpSocket();
    if (!s.valid() || !xc::net::bindSocket(s, "0.0.0.0", kBeaconPort)) {
        XC_LOGW("netdiag: cannot listen for beacons on UDP %u", kBeaconPort);
        xc::net::closeSocket(s);
        return;
    }
    xc::net::setRecvTimeout(s, 200);
    XC_LOGI("netdiag: listening for the PC's beacons on UDP %u", kBeaconPort);
    bool seen[4] = {};
    uint32_t lastSeq[4] = {};
    uint64_t lastUs[4] = {};
    uint64_t got[4] = {}, lost[4] = {}, longest[4] = {};
    uint64_t nextSummary = platform::nowUs() + 60000000;
    uint8_t buf[64];
    for (;;) {
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        const ssize_t n = xc::net::recvFromSocket(s, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        const uint64_t now = platform::nowUs();
        if (n >= 13 && std::memcmp(buf, "PSBXBCN1", 8) == 0) {
            const uint8_t c = buf[8] & 3;
            const uint32_t seq = (uint32_t(buf[9]) << 24) | (uint32_t(buf[10]) << 16) | (uint32_t(buf[11]) << 8) | buf[12];
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_beacons[g_beaconNext++ % kBeaconRing] = {now, seq, c};
                g_lastBeaconUs = now;
            }
            if (!seen[c]) {
                seen[c] = true;
                char ip[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
                XC_LOGI("netdiag: beacons with priority %s arriving from %s", kClass[c], ip);
            } else {
                if (seq > lastSeq[c] + 1) lost[c] += seq - lastSeq[c] - 1;
                longest[c] = std::max(longest[c], now - lastUs[c]);
            }
            ++got[c];
            lastSeq[c] = seq, lastUs[c] = now;
        }
        if (now >= nextSummary) {
            nextSummary = now + 60000000;
            if (got[0] + got[1] + got[2] + got[3]) {
                char line[256];
                int o = 0;
                for (int c = 0; c < 4; ++c)
                    o += std::snprintf(line + o, sizeof line - o, " %s %llu (%llu lost, longest wait %llu ms)", kClass[c],
                                       static_cast<unsigned long long>(got[c]), static_cast<unsigned long long>(lost[c]),
                                       static_cast<unsigned long long>(longest[c] / 1000));
                XC_LOGI("netdiag: beacons in the last 60 s:%s", line);
            }
            for (int c = 0; c < 4; ++c) got[c] = lost[c] = longest[c] = 0;
        }
    }
}
}  // namespace

void watchSocket(uint16_t localPort) {
    std::lock_guard<std::mutex> lock(g_watchMutex);
    if (!g_beaconStarted.exchange(true)) std::thread(beaconLoop).detach();
    if (g_watching) {
        g_watching = false;
        if (g_watchThread.joinable()) g_watchThread.join();
    }
    std::string seen;
    const xc::net::Socket s = findSocket(localPort, seen);
    if (!s.valid()) {
        XC_LOGW("netdiag: no UDP socket on port %u (UDP sockets:%s)", localPort, seen.c_str());
        return;
    }
    int rcvbuf = 0;
#if defined(_WIN32)
    int rl = sizeof rcvbuf;
    getsockopt(static_cast<SOCKET>(s.fd), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcvbuf), &rl);
#else
    socklen_t rl = sizeof rcvbuf;
    getsockopt(static_cast<int>(s.fd), SOL_SOCKET, SO_RCVBUF, &rcvbuf, &rl);
#endif
    XC_LOGI("netdiag: watching the stream socket (fd %ld, port %u, receive buffer %d bytes)", static_cast<long>(s.fd), localPort, rcvbuf);
    g_watching = true;
    g_watchThread = std::thread(watchLoop, s);
}

void stopWatch() {
    std::lock_guard<std::mutex> lock(g_watchMutex);
    if (!g_watching) return;
    g_watching = false;
    if (g_watchThread.joinable()) g_watchThread.join();
}

Window describeWindow(uint64_t t0, uint64_t t1) {
    Window w;
    std::lock_guard<std::mutex> lock(g_mutex);
    w.watched = g_watching.load();
    for (const auto& q : g_queue) {
        if (q.us <= t0 || q.us > t1) continue;
        ++w.samples;
        if (q.bytes > 0) {
            ++w.withData;
            w.maxBytes = std::max(w.maxBytes, static_cast<uint32_t>(q.bytes));
            const int64_t at = static_cast<int64_t>((q.us - t0) / 1000);
            if (w.firstDataMs < 0 || at < w.firstDataMs) w.firstDataMs = at;
        }
    }
    w.anyBeacons = g_lastBeaconUs && t1 - std::min(t1, g_lastBeaconUs) < 5000000;
    std::vector<BeaconHit> hits;
    for (const auto& b : g_beacons)
        if (b.us && b.us + 2000000 >= t0 && b.us <= t1 + 2000000) hits.push_back(b);
    std::sort(hits.begin(), hits.end(), [](const BeaconHit& x, const BeaconHit& y) { return x.us < y.us; });
    for (int c = 0; c < 4; ++c) {
        uint64_t prev = 0;
        for (const auto& b : hits) {
            if (b.cls != c) continue;
            if (b.us <= t0) {
                prev = b.us;
                continue;
            }
            if (b.us <= t1) ++w.beaconsIn[c];
            if (prev) w.beaconWaitMs[c] = std::max(w.beaconWaitMs[c], (b.us - prev) / 1000);
            prev = b.us;
            if (b.us > t1) break;  // the first one after the window closes it
        }
    }
    char buf[512];
    int o = 0;
    if (!w.watched && !w.samples)
        o += std::snprintf(buf + o, sizeof buf - o, "socket queue not watched");
    else if (!w.withData)
        o += std::snprintf(buf + o, sizeof buf - o, "socket queue empty all along (%zu samples)", w.samples);
    else
        o += std::snprintf(buf + o, sizeof buf - o, "socket queue had data in %zu of %zu samples, from +%lld ms, up to %u bytes",
                           w.withData, w.samples, static_cast<long long>(w.firstDataMs), w.maxBytes);
    if (!w.anyBeacons) {
        o += std::snprintf(buf + o, sizeof buf - o, "; no beacons");
    } else {
        o += std::snprintf(buf + o, sizeof buf - o, "; beacons");
        for (int c = 0; c < 4; ++c)
            o += std::snprintf(buf + o, sizeof buf - o, " %s %d in (longest wait %llu ms)", kClass[c], w.beaconsIn[c],
                               static_cast<unsigned long long>(w.beaconWaitMs[c]));
    }
    w.text = buf;
    return w;
}

}  // namespace xc::stream::netdiag
