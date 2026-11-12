// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Plays a provisioned xCloud session on the console: WebRTC session, H.264
// decode thread drawing to the screen, Opus to the audio output, and the
// DualSense state forwarded as an Xbox controller.
#pragma once

#include "input/controller.h"
#include "input/kbm.h"
#include "stream/stream_session.h"
#include "xcloud/gssv.h"

#include <functional>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace xc::media {
class AudioQueue;
}

namespace xc::app {

struct AudioLatency {
    uint64_t nextMs = 0, calmSinceMs = 0;
    int targetMs = 40;
    bool warm = false;  // past the first 3 s, while the queue fills
    size_t lows[5] = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};  // each second's lowest
    size_t lowNext = 0;
    void reset();
    void update(uint64_t now, media::AudioQueue* queue = nullptr);
};

class StreamPlayer {
public:
    explicit StreamPlayer(xcloud::GssvClient& gssv);
    ~StreamPlayer();

    // Blocking: connects the stream and starts playback.
    bool start(std::string& err);
    bool running() const;
    std::string endReason() const;
    // From the input thread, once per polled pad state; `index` 0..3 is the
    // controller's slot (input::pollPad).
    void sendInput(const input::ControllerState& pad, int index = 0);
    // USB mouse and keyboard (input/kbm.h), for the games that take them.
    void sendKbm(const input::KbmInput& in);
    // False while another window has the focus on the cloud console (the
    // account picker, a publisher's page).
    bool titleFocused() const;
    // Touch input announced on or off (off at the start).
    void setTouchEnabled(bool on);
    // Controllers 1..3 coming and going (0 is there from the start).
    void setPadConnected(int index, bool connected);
    // The game's requests for text, oldest first (main thread): false when
    // none. Each one is answered once with answerTextInput() (accepted false
    // = cancelled); textInputWithdrawn() turns true when the game gave up
    // waiting, and the answer is then dropped.
    bool takeTextInput(stream::TextInputRequest& out);
    bool textInputWithdrawn(const std::string& id);
    void answerTextInput(const std::string& id, bool accepted, const std::string& text);
    // A new key frame (a clean picture), as after packet loss.
    void requestKeyframe();
    // How many times the stream reconnected after losing the connection (a
    // new session: controllers 1..3 must be announced again).
    uint32_t reconnects() const;
    // Triggers a seamless WebRTC/ICE session reconnection via reconnect() without leaving the game.
    void requestReconnect();
    bool isReconnecting() const;
    // Test hook: drops the connection as a network outage would.
    void simulateDrop();
    // Another stream tier mid-session: "720", "720HQ", "1080", "1080HQ", "1440".
    void requestResolution(const std::string& alias);
    // Keepalives etc.; about once a second from the thread owning `gssv`.
    void tick();
    void stop();
    // Appends every received access unit to `path` for `seconds` (diagnostics).
    void dumpVideo(const std::string& path, int seconds);
    // Writes the next decoded picture, half size, as a binary PPM.
    void requestSnapshot(const std::string& path);
    // Measurements: the next picture drawn with each look, crops saved (autoplay "imgtest").
    void requestImageTest();
    // Voice chat / microphone controls
    // Measurements only (autoplay "legacy"): the stream as it was tuned
    // before 2026-11-04, to compare against. Before start().
    void setLegacyTuning(bool on);
    // The screen shows this stream at 119.88 Hz: a couple of frames that
    // came at once are shown instead of skipped (a refresh drains them).
    void setHighRefreshOutput(bool on);
    // Measurements only (autoplay "abcycle"): the hardware or the software
    // decoder from the next frame on, while streaming.
    void switchDecoder(bool hardware);
    // Measurements only (autoplay "bench", the file mark.req): logs the
    // window since the last mark (every frame's decode, draw and on-screen
    // times as percentiles; the counters' increase) and starts a new one.
    void markBenchWindow(const std::string& name);
    // Measurements only (autoplay "bench", hwcfg.req): the hardware decoder
    // made again from the next frame, with media::HwDecoder::tuning().
    void recreateHwDecoder();
    // Measurements only (autoplay "tierseq="): StreamOptions::tierSequence. Before start().
    void setTierSequence(const std::string& seq);
    // Measurements only (autoplay "bench", gyro.req): how far the picture moved
    // from each frame to the next (the camera's turn), for `seconds`; then
    // motionTake() gives the samples once.
    struct MotionSample {
        uint64_t us;      // platform::nowUs() when the frame was presented
        float dx, dy;     // picture pixels, sub-pixel; + = the content moved right / down
        float fitX, fitY; // best match error / mean error (0 = certain, 1 = no match)
        float stickX, stickY;  // the sweep's right stick then (setMotionStick)
    };
    void startMotionRecording(int seconds);
    bool motionTake(std::vector<MotionSample>& out);
    using MotionStick = bool (*)(uint64_t nowUs, float& x, float& y);
    void setMotionStick(MotionStick fn);
    // Each picture shown, its luma plane, on the video thread (gyro aiming's
    // calibration keeps small copies). Empty: none. Returns once no call is running.
    using FrameTap = std::function<void(const uint8_t* luma, int stride, int width, int height, uint64_t us)>;
    void setFrameTap(FrameTap tap);
    // Measurements: with the samples, each frame's column averages (576 floats)
    // and a 128x72 luma thumbnail, to study the meter itself. Taken once.
    void setMotionRawDump(bool on);
    bool motionRawTake(std::string& out);
    void setMicEnabled(bool enabled);
    bool micEnabled() const;
    void setMicMuted(bool muted);
    bool micMuted() const;
    bool isMicHardwareMuted() const;
    void setMicGain(float gain);
    float micGain() const;
    float micLevel() const;

    struct Stats {
        uint64_t videoFrames = 0, decodedFrames = 0, droppedFrames = 0, audioPackets = 0;
        uint64_t decodeFailures = 0, queueResets = 0, keyframeRequests = 0, queued = 0;
        // RTP: packets lost, recovered by NACK, frames the jitter buffer dropped.
        uint64_t rtpPackets = 0, rtpLost = 0, rtpRecovered = 0, rtpNacks = 0, rtpDroppedFrames = 0;
        uint64_t rtpGapMs = 0, rtpBurst = 0, rtpLateMs = 0;  // VideoReceiveStats::maxGapMs..., this interval
        uint64_t rtpKbps = 0, rembKbps = 0, vibrations = 0;
        uint64_t lateFrames = 0;  // decoded, not drawn: both flips still queued
        int rttMs = -1;  // to the stream server
        int width = 0, height = 0;  // of the last decoded picture
        // Since the previous stats() call (so call it from one place only).
        uint64_t decodeAvgUs = 0, decodeMaxUs = 0, drawAvgUs = 0, drawMaxUs = 0;
        // Network arrival to on the screen (GPU path), since the last call.
        uint64_t displayAvgUs = 0, displayMaxUs = 0;
        uint64_t glassAvgUs = 0, glassMaxUs = 0;  // from the frame's first packet to the screen
        // The pictures' average quantiser since the last call (lower: finer); -1 none.
        double qpAvg = -1;
        bool isHwDecode = false;
    };
    Stats stats() const;
    // Before start(): H.264 decoder threads (more than 1 = frame threading).
    void setDecodeThreads(int threads);
    // Before start(): the video bitrate offered to the server, in Mbps (0: the tier's).
    void setMaxBitrate(int mbps);
    // Test: hold back this percentage of the video packets (see H264Receiver).
    void setSimulatedLoss(int percent);
    // Before start(): also run every frame through the PS5's hardware
    // decoder and log how it does (the picture still comes from FFmpeg).
    void setHwDecodeProbe(bool on);
    // Before start(): prefer hardware video decoding (libSceVideodec2 on PS5,
    // FFmpeg HWAccel on Desktop) with automatic software CPU fallback.
    void setPreferHwDecode(bool on);
    bool preferHwDecode() const;
    // Presentation pacing FPS limit: 0 unlimited, 30, 60, 120.
    void setFpsLimit(int fps);
    int fpsLimit() const;
    // Recent presentation frametimes (ms, up to 60 frames)
    void getRecentFrametimes(std::vector<float>& out) const;
    // Recent network packet arrival jitter (ms, up to 60 samples)
    void getRecentNetworkJitter(std::vector<float>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xc::app
