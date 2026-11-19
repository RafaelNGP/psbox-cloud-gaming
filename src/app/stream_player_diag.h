// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Stream diagnostics, recording, snapshot capture, motion measurement, image-look testing,
// and benchmark percentile tracking extracted from StreamPlayer.
#pragma once

#include "app/stream_player.h"
#include "media/decoder.h"
#include "platform/platform.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xc::app {

// A recording of the game's picture as it arrives (the H.264 the cloud sends, as it is: nothing
// re-encoded, no sound, none of the app's overlay), for diagnosis: rec.h264, and rec.idx with each
// frame's number, its arrival (ms after the start) and the wall clock, to match the log's times.
// Written by a thread of its own; frames beyond a backlog of 240 are dropped, not waited for.
class StreamRecorder {
public:
    ~StreamRecorder();

    bool start(const std::string& dir, int seconds);
    void stop();
    bool active() const { return running_.load(std::memory_order_relaxed); }
    bool expired() const;
    // From the receive thread: a complete access unit (Annex B).
    void frame(const uint8_t* d, size_t n);

private:
    struct Item {
        std::vector<uint8_t> data;
        uint64_t atMs;
    };
    void writeLoop();

    std::FILE* video_ = nullptr;
    std::FILE* index_ = nullptr;
    std::thread writer_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    std::atomic<bool> running_{false};
    uint64_t startMs_ = 0, untilMs_ = 0, frames_ = 0, bytes_ = 0, dropped_ = 0;
};

// Async PPM snapshot of the decoded YUV/NV12 frame and full-res scan-out screen readback.
class StreamSnapshotter {
public:
    ~StreamSnapshotter();

    void request(const std::string& path);
    // Returns true when a snapshot was taken: the caller then saves what the screen shows too (saveScreen()).
    bool saveSnapshotIfAsked(const media::Picture& pic);
    void saveScreen();
    void stop();

private:
    std::mutex snapshotMutex_;
    std::string snapshotPath_;
    std::string screenPath_;  // video thread only
    platform::Thread snapshotThread_;
    platform::Thread screenThread_;
};

// Measurements (startMotionRecording): the picture's shift between frames, from its luma's
// column and row averages over the upper middle (away from the HUD and the player's character).
class StreamMotionRecorder {
public:
    void setStick(StreamPlayer::MotionStick fn) { motionStick_ = fn; }
    void setRawDump(bool on);
    bool rawTake(std::string& out);
    void start(int seconds);
    bool take(std::vector<StreamPlayer::MotionSample>& out);
    void measure(const media::Picture& pic, uint64_t nowUs);

private:
    static float profileShift(const std::vector<float>& prev, const std::vector<float>& cur, int range, float& fit);

    std::mutex mutex_;
    std::vector<StreamPlayer::MotionSample> rec_;
    std::vector<float> prevCols_, prevRows_;
    uint64_t untilUs_ = 0;
    std::atomic<int> state_{0};  // 0 idle, 1 recording, 2 done
    StreamPlayer::MotionStick motionStick_ = nullptr;
    bool rawDump_ = false;
    std::string raw_;
};

// Measurement windows (markBenchWindow): every frame's times from one mark to the next,
// for the percentiles a per-second average can't give. Off (nothing recorded, no lock taken)
// until the first mark.
class StreamBenchTracker {
public:
    struct Hist {
        static constexpr uint64_t kStepUs = 10, kBuckets = 10000;  // 10 us steps up to 100 ms
        std::vector<uint32_t> n = std::vector<uint32_t>(kBuckets + 1, 0);
        uint64_t count = 0, sumUs = 0, maxUs = 0;

        void add(uint64_t us);
        double avgMs() const;
        double maxMs() const;
        double pctMs(double p) const;
    };

    struct Window {
        std::string name;
        uint64_t startMs = 0;
        Hist decode, hwDecode, hwDecodeCpu, hwFlush, qpParse, draw, onScreen, glass;
        int64_t cpu0Us = -1;  // the process's CPU time at the start (processCpuUs())
        uint64_t swCalls = 0, hwCalls = 0, flushed = 0;
        uint64_t decoded0 = 0, dropped0 = 0, late0 = 0, failures0 = 0;
        uint64_t rtpLost0 = 0, rtpRecovered0 = 0, rtpDropped0 = 0;
    };

    struct Counters {
        uint64_t decoded = 0, dropped = 0, late = 0, failures = 0;
        uint64_t rtpLost = 0, rtpRecovered = 0, rtpDropped = 0;
    };

    void recordDecode(uint64_t us, const media::VideoDecoder& d);
    void record(Hist Window::*h, uint64_t us);
    void markWindow(const std::string& name, const Counters& counters);

private:
    std::atomic<bool> on_{false};
    std::mutex mutex_;
    Window window_;
};

// Measurements (autoplay "imgtest"): draws `pic` with each upscaler/sharpening/chroma look
// and writes a 1280x720 crop (or full frame) of the output as <dataDir>/img_<name>.ppm.
void runStreamImageTest(const media::Picture& pic,
                        const std::function<bool(const media::Picture&, bool)>& drawPic);

}  // namespace xc::app
