// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "stream/h264_receiver.h"

#include "stream/net_diag.h"
#include "stream/rtcp_reporter.h"

#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace xc::stream {

namespace {
void raiseMax(std::atomic<uint64_t>& a, uint64_t v) {
    uint64_t cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}
}  // namespace

namespace {

// How long a hole may wait for its retransmission (a couple of RTTs), how
// often a NACK is repeated, and how many times.
constexpr uint64_t kRenackMs = 50;
constexpr int kMaxNacks = 5;
constexpr size_t kMaxBuffered = 1800;      // packets (within 2048-slot ring)

constexpr uint8_t kStartCode[] = {0, 0, 0, 1};

// NAL types that begin a decodable access unit after loss.
bool isKeyNal(uint8_t type) { return type == 5 || type == 7; }

// Whether an RTP payload is the first of an access unit a decoder can start at, told from the
// payload alone: parameter sets or an access unit delimiter come first in a frame, and so does
// the key slice whose first macroblock is 0 (first_mb_in_slice, ue(v): one set bit). A key
// slice by itself is taken only once the parameter sets it needs have been through.
bool startsKeyAccessUnit(const uint8_t* p, size_t n, bool paramSetsSeen) {
    if (n < 2) return false;
    const uint8_t type = p[0] & 0x1F;
    auto firstNal = [&](uint8_t t, const uint8_t* body, size_t bodyLen) {
        if (t == 7 || t == 9) return true;
        return t == 5 && paramSetsSeen && bodyLen >= 1 && (body[0] & 0x80) != 0;
    };
    if (type >= 1 && type <= 23) return firstNal(type, p + 1, n - 1);
    if (type == 24) {  // STAP-A: its first unit
        if (n < 4) return false;
        const size_t len = (static_cast<size_t>(p[1]) << 8) | p[2];
        if (len == 0 || 3 + len > n) return false;
        return firstNal(p[3] & 0x1F, p + 4, len - 1);
    }
    if (type == 28 && n > 2 && (p[1] & 0x80)) return firstNal(p[1] & 0x1F, p + 2, n - 2);  // FU-A, its start
    return false;
}

void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
}

const std::byte* asBytes(const std::vector<uint8_t>& v) { return reinterpret_cast<const std::byte*>(v.data()); }

}  // namespace

H264Receiver::H264Receiver(std::function<void()> onKeyframeNeeded, VideoReceiveStats& stats, uint64_t waitMs)
    : onKeyframeNeeded_(std::move(onKeyframeNeeded)),
      stats_(stats),
      waitMs_(waitMs),
      ring_(std::make_unique<PacketSlot[]>(kRingSize)) {
    clearRing();
}

void H264Receiver::putPacket(int64_t s, uint32_t ts, bool marker, const uint8_t* data, size_t len) {
    size_t idx = static_cast<size_t>(s) & kRingMask;
    if (ring_[idx].seq == -1) {
        ++packetCount_;
    }
    ring_[idx].seq = s;
    ring_[idx].timestamp = ts;
    ring_[idx].marker = marker;
    ring_[idx].length = static_cast<uint16_t>(std::min(len, kMaxPayload));  // accept() refuses larger ones
    if (ring_[idx].length > 0 && data) {
        std::memcpy(ring_[idx].payload, data, ring_[idx].length);
    }
}

void H264Receiver::erasePacket(int64_t s) {
    size_t idx = static_cast<size_t>(s) & kRingMask;
    if (ring_[idx].seq == s) {
        ring_[idx].seq = -1;
        ring_[idx].length = 0;
        if (packetCount_ > 0) --packetCount_;
    }
}

void H264Receiver::clearRing() {
    for (size_t i = 0; i < kRingSize; ++i) {
        ring_[i].seq = -1;
        ring_[i].length = 0;
    }
    packetCount_ = 0;
}

int64_t H264Receiver::unwrap(uint16_t seq) {
    if (highest_ < 0) return seq;
    int64_t base = highest_ & ~int64_t(0xFFFF);
    int64_t candidate = base | seq;
    // Pick the value closest to the highest seen (handles both wrap directions).
    if (candidate - highest_ > 0x8000) candidate -= 0x10000;
    else if (highest_ - candidate > 0x8000) candidate += 0x10000;
    return candidate;
}

void H264Receiver::sendNack(const std::vector<int64_t>& seqs, const rtc::message_callback& send) {
    if (seqs.empty() || !mediaSsrc_) return;
    // Generic NACK (RFC 4585 6.2.1): FCI = PID + bitmask of the next 16.
    std::vector<std::pair<uint16_t, uint16_t>> fci;
    for (size_t i = 0; i < seqs.size();) {
        int64_t pid = seqs[i];
        uint16_t blp = 0;
        size_t j = i + 1;
        while (j < seqs.size() && seqs[j] - pid <= 16) {
            blp |= static_cast<uint16_t>(1u << (seqs[j] - pid - 1));
            ++j;
        }
        fci.emplace_back(static_cast<uint16_t>(pid), blp);
        i = j;
    }
    std::vector<uint8_t> b;
    b.push_back(0x81);  // V=2, FMT=1
    b.push_back(205);   // RTPFB
    put16(b, static_cast<uint16_t>(2 + fci.size()));
    put32(b, kReceiverSsrc);
    put32(b, mediaSsrc_);
    for (auto [pid, blp] : fci) {
        put16(b, pid);
        put16(b, blp);
    }
    send(rtc::make_message(asBytes(b), asBytes(b) + b.size(), rtc::Message::Control));
    ++stats_.nacks;
    if (simulatedLoss_ > 0)
        for (int64_t s : seqs) {
            auto h = held_.find(static_cast<uint16_t>(s));
            if (h == held_.end()) continue;
            redeliver_.emplace_back(platform::nowMs() + 30, std::move(h->second));
            held_.erase(h);
        }
}

void H264Receiver::sendPli(const rtc::message_callback& send) {
    if (!mediaSsrc_) return;
    std::vector<uint8_t> b;
    b.push_back(0x81);  // V=2, FMT=1
    b.push_back(206);   // PSFB
    put16(b, 2);
    put32(b, kReceiverSsrc);
    put32(b, mediaSsrc_);
    send(rtc::make_message(asBytes(b), asBytes(b) + b.size(), rtc::Message::Control));
}

void H264Receiver::askKeyframe(const rtc::message_callback& send) {
    waitingKeyframe_ = true;
    uint64_t now = platform::nowMs();
    if (now - lastKeyframeRequestMs_ < 300) return;  // one request per burst
    lastKeyframeRequestMs_ = now;
    ++stats_.keyframeRequests;
    sendPli(send);
    if (onKeyframeNeeded_) onKeyframeNeeded_();
}

void H264Receiver::noteGap(int64_t from, int64_t to, uint64_t now, const rtc::message_callback& send) {
    if (to - from > 512) {  // a huge jump: not worth NACKing, resynchronise
        stats_.lost += static_cast<uint64_t>(to - from);
        return;
    }
    if (pause_.active && pause_.holes.size() < 8) {
        pause_.holes.emplace_back(from, to);
        if (!pause_.holeFoundUs) pause_.holeFoundUs = platform::nowUs();
    }
    std::vector<int64_t> seqs;
    for (int64_t s = from; s < to; ++s) {
        missing_[s] = Missing{now, now, 1};
        seqs.push_back(s);
    }
    stats_.lost += seqs.size();
    sendNack(seqs, send);
}

void H264Receiver::resendNacks(uint64_t now, const rtc::message_callback& send) {
    std::vector<int64_t> seqs;
    for (auto& [s, m] : missing_)
        if (m.nacks < kMaxNacks && now - m.lastNackMs >= renackMs()) {
            m.lastNackMs = now;
            ++m.nacks;
            seqs.push_back(s);
        }
    sendNack(seqs, send);
}

bool H264Receiver::assemble(int64_t first, int64_t last, std::vector<uint8_t>& out, bool& keyframe) const {
    out.clear();
    keyframe = false;
    for (int64_t s = first; s <= last; ++s) {
        const auto* slot = getPacket(s);
        if (!slot) return false;
        if (slot->length == 0) continue;  // padding only: nothing to add, the frame goes on
        const uint8_t* p = slot->payload;
        size_t pLen = slot->length;
        uint8_t type = p[0] & 0x1F;
        if (type >= 1 && type <= 23) {  // single NAL unit
            out.insert(out.end(), kStartCode, kStartCode + 4);
            out.insert(out.end(), p, p + pLen);
            keyframe |= isKeyNal(type);
        } else if (type == 24) {  // STAP-A
            size_t i = 1;
            while (i + 2 <= pLen) {
                size_t n = (static_cast<size_t>(p[i]) << 8) | p[i + 1];
                i += 2;
                if (n == 0 || i + n > pLen) break;
                out.insert(out.end(), kStartCode, kStartCode + 4);
                out.insert(out.end(), p + i, p + i + n);
                keyframe |= isKeyNal(p[i] & 0x1F);
                i += n;
            }
        } else if (type == 28 && pLen > 2) {  // FU-A
            bool start = (p[1] & 0x80) != 0;
            if (start) {
                out.insert(out.end(), kStartCode, kStartCode + 4);
                out.push_back(static_cast<uint8_t>((p[0] & 0xE0) | (p[1] & 0x1F)));
                keyframe |= isKeyNal(p[1] & 0x1F);
            }
            out.insert(out.end(), p + 2, p + pLen);
        }
    }
    return !out.empty();
}

bool H264Receiver::resync(rtc::message_vector& out) {
    // Find a frame boundary (the packet before is present and is a marker, or
    // carries another timestamp; or it is missing and this packet shows by itself
    // that it begins a key frame) followed by a complete key frame. Without the
    // second case a key frame right after a packet lost for good, and the first
    // one of a session (nothing came before it), could never be resumed at: the
    // picture stayed frozen until another key frame was asked for and sent.
    int64_t startSeq = std::max<int64_t>(0, highest_ - static_cast<int64_t>(kRingSize - 1));
    for (int64_t s = startSeq; s <= highest_; ++s) {
        const auto* cur = getPacket(s);
        if (!cur) continue;
        const auto* prev = getPacket(s - 1);
        if (!prev) {
            if (!startsKeyAccessUnit(cur->payload, cur->length, paramSetsSeen_)) continue;
        } else if (!prev->marker && prev->timestamp == cur->timestamp) {
            continue;
        }

        // Walk to the marker of this frame.
        int64_t end = s;
        bool complete = false;
        while (end <= highest_) {
            const auto* p = getPacket(end);
            if (!p) break;
            if (p->marker) {
                complete = true;
                break;
            }
            ++end;
        }
        if (!complete) continue;

        std::vector<uint8_t> frame;
        bool key = false;
        if (assemble(s, end, frame, key) && key) {
            nextSeq_ = s;
            for (int64_t e = startSeq; e < nextSeq_; ++e) erasePacket(e);
            for (auto e = missing_.begin(); e != missing_.end() && e->first < nextSeq_;) {
                e = missing_.erase(e);
            }
            (void)out;
            return true;
        }
    }
    return false;
}

void H264Receiver::drain(uint64_t now, rtc::message_vector& out, const rtc::message_callback& send) {
    for (;;) {
        if (nextSeq_ < 0) {
            if (!resync(out)) return;
        }
        // Collect the frame starting at nextSeq_.
        int64_t s = nextSeq_;
        const auto* it = getPacket(s);
        int64_t hole = -1;
        if (!it) {
            hole = s;
        } else {
            while (true) {
                const auto* p = getPacket(s);
                if (!p) {
                    hole = s;
                    break;
                }
                if (p->marker) break;
                ++s;
            }
        }
        if (hole >= 0) {
            if (hole > highest_) return;  // just not here yet
            auto m = missing_.find(hole);
            uint64_t since = m != missing_.end() ? m->second.sinceMs : now;
            if (m == missing_.end()) missing_[hole] = Missing{now, now, 0};
            if (now - since < lossWaitMs()) return;  // give the retransmission time
            // Lost for good: drop what we have of this frame and resync on
            // the next key frame.
            ++stats_.framesDropped;
            for (int64_t e = nextSeq_; e <= hole; ++e) erasePacket(e);
            for (auto e = missing_.begin(); e != missing_.end() && e->first <= hole;) e = missing_.erase(e);
            nextSeq_ = -1;
            askKeyframe(send);
            continue;
        }
        std::vector<uint8_t> frame;
        bool key = false;
        bool ok = assemble(nextSeq_, s, frame, key);
        const auto* markerPkt = getPacket(s);
        uint32_t ts = markerPkt ? markerPkt->timestamp : 0;
        for (int64_t e = nextSeq_; e <= s; ++e) erasePacket(e);
        for (auto e = missing_.begin(); e != missing_.end() && e->first <= s;) e = missing_.erase(e);
        nextSeq_ = s + 1;
        if (!ok) continue;
        if (waitingKeyframe_ && !key) {
            ++stats_.framesDropped;  // would decode against lost references
            continue;
        }
        if (key) waitingKeyframe_ = false, paramSetsSeen_ = true;
        ++stats_.framesOut;
        if (lastFrameMs_) {
            const int64_t late = static_cast<int64_t>(now - lastFrameMs_) - static_cast<int32_t>(ts - lastFrameTs_) / 90;
            if (late > 0) raiseMax(stats_.maxLateMs, static_cast<uint64_t>(late));
        }
        lastFrameMs_ = now, lastFrameTs_ = ts;
        out.push_back(rtc::make_message(asBytes(frame), asBytes(frame) + frame.size(), std::make_shared<rtc::FrameInfo>(ts)));
    }
}

void H264Receiver::accept(const uint8_t* d, size_t n, uint64_t now, const rtc::message_callback& send) {
    if (n < 12 || (d[0] >> 6) != 2) return;
    uint16_t rawSeq = static_cast<uint16_t>((d[2] << 8) | d[3]);
    if (simulatedLoss_ > 0) {
        lossState_ = lossState_ * 1103515245u + 12345u;
        if ((lossState_ >> 16) % 100 < static_cast<uint32_t>(simulatedLoss_) && !held_.count(rawSeq)) {
            held_[rawSeq].assign(d, d + n);
            if (held_.size() > 2000) held_.erase(held_.begin());
            return;
        }
    }
    size_t off = 12 + 4u * (d[0] & 0x0F);
    if (d[0] & 0x10) {  // header extension
        if (off + 4 > n) return;
        off += 4 + 4u * ((static_cast<size_t>(d[off + 2]) << 8) | d[off + 3]);
    }
    size_t end = n;
    if (d[0] & 0x20) {  // padding
        if (d[n - 1] > n) return;
        end -= d[n - 1];
    }
    if (off > end) return;
    mediaSsrc_ = (uint32_t(d[8]) << 24) | (uint32_t(d[9]) << 16) | (uint32_t(d[10]) << 8) | d[11];
    ++stats_.packets;

    int64_t seq = unwrap(rawSeq);
    lastSeq_ = seq;
    if (nextSeq_ >= 0 && seq < nextSeq_) return;  // too late or duplicate
    if (hasPacket(seq)) return;
    // A frame's first packet (the first with its timestamp): when it came.
    const uint32_t ts = (uint32_t(d[4]) << 24) | (uint32_t(d[5]) << 16) | (uint32_t(d[6]) << 8) | d[7];
    if (!anyTs_ || ts != lastNewTs_) {
        bool known = false;
        for (const auto& f : firstPacket_) known |= f.second && f.first == ts;
        if (!known) {
            firstPacket_[firstPacketNext_++ % kFirstPacketRing] = {ts, platform::nowUs()};
            lastNewTs_ = ts, anyTs_ = true;
        }
    }
    if (end - off > kMaxPayload) {
        // Larger than a slot: cut short it would corrupt the NAL unit. Left
        // as a hole instead, NACKed when the next packet shows it (and
        // still missing if it was), then the frame dropped and a key frame
        // asked for, like any loss.
        if (oversized_++ == 0)
            XC_LOGW("video: RTP payload of %zu bytes is larger than %zu, treated as lost", end - off,
                    kMaxPayload);
        return;
    }
    if (missing_.erase(seq)) {
        ++stats_.recovered;
        if (pause_.active)
            for (const auto& h : pause_.holes)
                if (seq >= h.first && seq < h.second) {
                    ++pause_.recovered;
                    pause_.lastRecoverUs = platform::nowUs();
                    break;
                }
    }

    bool marker = (d[1] & 0x80) != 0;
    putPacket(seq, ts, marker, d + off, end - off);

    if (highest_ >= 0 && seq > highest_ + 1) noteGap(highest_ + 1, seq, now, send);
    if (seq > highest_) highest_ = seq;
}

void H264Receiver::incoming(rtc::message_vector& messages, const rtc::message_callback& send) {
    rtc::message_vector out;
    uint64_t now = platform::nowMs();
    const bool diag = netdiag::enabled();
    const uint64_t callUs = diag ? platform::nowUs() : 0;
    if (diag && pause_.active && callUs - pause_.t1Us > 600000) finishPause();
    for (auto it = redeliver_.begin(); it != redeliver_.end();) {
        if (it->first > now) {
            ++it;
            continue;
        }
        std::vector<uint8_t> packet = std::move(it->second);
        it = redeliver_.erase(it);
        accept(packet.data(), packet.size(), now, send);
    }
    for (auto& message : messages) {
        if (message->type == rtc::Message::Control) continue;  // RTCP: handled down the chain
        if (lastPacketMs_) raiseMax(stats_.maxGapMs, now - lastPacketMs_);
        // Held and then handed over at once: the pause, then a burst right after it.
        if (lastPacketMs_ && now - lastPacketMs_ >= kHoldGapMs) holdStartMs_ = now, holdGapMs_ = now - lastPacketMs_, holdBurst_ = 0;
        if (holdStartMs_ && now - holdStartMs_ < kHoldWindowMs && ++holdBurst_ == kHoldBurst) {
            ++stats_.holds;
            raiseMax(stats_.holdMs, holdGapMs_);
        }
        if (diag) {
            if (lastPacketUs_ && callUs - lastPacketUs_ >= 120000 && !pause_.active) {
                pause_ = PauseTrace{};
                pause_.active = true;
                pause_.t0Us = lastPacketUs_, pause_.t1Us = callUs;
                pause_.prevCallUs = lastCallUs_;
                pause_.idleUs = lastCallEndUs_ ? callUs - lastCallEndUs_ : 0;
                pause_.highestBefore = highest_;
                pause_.missingBefore = missing_.size();
                for (const auto& [s, m] : missing_)
                    pause_.oldestMissingMs = std::max(pause_.oldestMissingMs, now - m.sinceMs);
            }
            if (pause_.active && callUs - pause_.t1Us < 50000) ++pause_.burstPackets, pause_.burstLastUs = callUs;
            lastPacketUs_ = callUs;
        }
        lastPacketMs_ = now;
        accept(reinterpret_cast<const uint8_t*>(message->data()), message->size(), now, send);
        if (diag && pause_.active && pause_.firstAfter < 0) pause_.firstAfter = lastSeq_;
    }
    resendNacks(now, send);
    drain(now, out, send);
    raiseMax(stats_.maxBurst, out.size());
    // Still without a key frame: ask again (askKeyframe allows one every
    // 300 ms), as the request or the key frame itself may have been lost;
    // otherwise the picture stays frozen until the buffer overflows (seconds).
    if (nextSeq_ < 0 && packetCount_ > 0) askKeyframe(send);
    if (packetCount_ > kMaxBuffered || (nextSeq_ >= 0 && highest_ - nextSeq_ > static_cast<int64_t>(kMaxBuffered))) {  // stuck: start over
        clearRing();
        missing_.clear();
        nextSeq_ = -1;
        askKeyframe(send);
    }
    if (diag) {
        const uint64_t endUs = platform::nowUs();
        lastCallUs_ = endUs - callUs, lastCallEndUs_ = endUs;
    }
    messages.swap(out);
}

void H264Receiver::finishPause() {
    PauseTrace p = std::move(pause_);
    pause_ = PauseTrace{};
    const netdiag::Window w = netdiag::describeWindow(p.t0Us, p.t1Us);
    const uint64_t pauseMs = (p.t1Us - p.t0Us) / 1000;
    char holes[160] = "none";
    if (!p.holes.empty()) {
        int o = 0;
        int count = 0;
        for (const auto& h : p.holes) count += static_cast<int>(h.second - h.first);
        o += std::snprintf(holes + o, sizeof holes - o, "%d packet(s) from %lld", count, static_cast<long long>(p.holes[0].first));
        if (p.holes[0].first == p.highestBefore + 1) o += std::snprintf(holes + o, sizeof holes - o, " (right after the last one before)");
        o += std::snprintf(holes + o, sizeof holes - o, ", seen at +%.1f ms, %d recovered", (p.holeFoundUs - p.t1Us) / 1000.0, p.recovered);
        if (p.recovered) std::snprintf(holes + o, sizeof holes - o, " at +%.1f ms", (p.lastRecoverUs - p.t1Us) / 1000.0);
    }
    // Where the packets were: waiting in the console's socket (the app did
    // not read them), or nowhere yet while the PC's beacons kept coming (held
    // before the console's Wi-Fi), or nothing at all arrived.
    const char* verdict;
    uint64_t bestBeaconWait = UINT64_MAX;
    for (int c = 0; c < 4; ++c)
        if (w.beaconWaitMs[c]) bestBeaconWait = std::min(bestBeaconWait, w.beaconWaitMs[c]);
    if (!w.samples)
        verdict = "unknown (socket not watched)";
    else if (w.withData * 2 > w.samples)
        verdict = "APP: the packets were in the console and the app did not read them";
    else if (!w.anyBeacons)
        verdict = "OUTSIDE THE APP: nothing reached the console (no beacons to tell Wi-Fi from internet)";
    else if (bestBeaconWait != UINT64_MAX && bestBeaconWait * 2 < pauseMs)
        verdict = "BEFORE THE CONSOLE: nothing reached it for the stream while the PC's beacons kept coming";
    else
        verdict = "CONSOLE WI-FI OR ALL TRAFFIC: the PC's beacons stopped too";
    XC_LOGI("netdiag: pause %llu ms, packets %lld then %lld; missing before it %zu (oldest %llu ms); holes after it: %s; "
            "%d packets in the first 50 ms (%.1f ms); app: the packet before took %.2f ms, idle %.1f ms after it; %s; verdict %s",
            static_cast<unsigned long long>(pauseMs), static_cast<long long>(p.highestBefore),
            static_cast<long long>(p.firstAfter), p.missingBefore, static_cast<unsigned long long>(p.oldestMissingMs), holes,
            p.burstPackets, (p.burstLastUs - p.t1Us) / 1000.0, p.prevCallUs / 1000.0, p.idleUs / 1000.0, w.text.c_str(), verdict);
}

uint64_t H264Receiver::firstPacketUs(uint32_t rtpTimestamp) const {
    for (const auto& f : firstPacket_)
        if (f.second && f.first == rtpTimestamp) return f.second;
    return 0;
}

uint64_t H264Receiver::lossWaitMs() const {
    const int rtt = rttMs_;
    if (rtt <= 0) return waitMs_;
    const uint64_t dynamicMin = (rtt < 30) ? std::max<uint64_t>(40, static_cast<uint64_t>(rtt) * 2 + 15) : waitMs_;
    return std::clamp<uint64_t>(static_cast<uint64_t>(rtt) * 2 + 30, dynamicMin, 300);
}

uint64_t H264Receiver::renackMs() const {
    const int rtt = rttMs_;
    if (rtt <= 0) return kRenackMs;
    const uint64_t dynamicMin = (rtt < 30) ? std::max<uint64_t>(20, static_cast<uint64_t>(rtt) + 6) : kRenackMs;
    return std::clamp<uint64_t>(static_cast<uint64_t>(rtt) + 10, dynamicMin, 250);
}

}  // namespace xc::stream
