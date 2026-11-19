// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "test_harness.h"

#include "app/auto_deband.h"
#include "app/crash_guard.h"
#include "app/gyro_calibration.h"
#include "app/library.h"
#include "app/report.h"
#include "app/settings.h"
#include "app/settings_manager.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "app/web_server.h"
#include "auth/account_seal.h"
#include "auth/auth_manager.h"
#include "auth/msa.h"
#include "auth/xbox_live.h"
#include "display/display.h"
#include "display/gpu.h"
#include "input/controller.h"
#include "input/gyro.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "input/pad_map.h"
#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include "input/sdl_mapping.h"
#include "input/tuning.h"
#include "input/usb_xray.h"
#include "media/audio_encoder.h"
#include "media/audio_in.h"
#include "media/audio_in_common.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "media/decoder.h"
#include "media/hw_decoder.h"
#include "net/http.h"
#include "net/socket.h"
#include "platform/handheld.h"
#include "platform/platform.h"
#include "platform/toasts.h"
#include "stream/h264_receiver.h"
#include "stream/input_packet.h"
#include "stream/stream_session.h"
#include "trophy/trophy_backend.h"
#include "trophy/trophy_registry.h"
#include "trophy/trophy_sync.h"
#include "ui/accent_color.h"
#include "ui/app_ui.h"
#include "ui/app_ui_internal.h"
#include "ui/cloud_time.h"
#include "ui/image_cache.h"
#include "ui/input_mode.h"
#include "ui/pad_glyphs.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "ui/touch_pad_overlay.h"
#include "util/fs.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/achievements.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/prices.h"
#include "xcloud/purchase.h"
#include "xcloud/regions.h"
#include "xcloud/titlehub.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif

using namespace xc;

static void testAutoDeband() {
    using xc::app::debandForMbps;
    CHECK(debandForMbps(0.3, 1) == -1);  // starting or standing still: no measure
    CHECK(debandForMbps(3, 0) == 2 && debandForMbps(7, 0) == 1 && debandForMbps(12, 0) == 1);
    // A 1 Mbps margin each way: near a limit the level in use stays.
    CHECK(debandForMbps(5.5, 2) == 2 && debandForMbps(4.5, 1) == 1 && debandForMbps(10.5, 1) == 1);
    CHECK(debandForMbps(9.5, 0) == 1 && debandForMbps(6.5, 2) == 1);

    xc::app::AutoDeband a;
    CHECK(a.level() == 1);
    int changedAt = 0;
    for (int s = 1; s <= 10 && !changedAt; ++s)
        if (a.update(1.5)) changedAt = s;
    CHECK(changedAt == 8);  // the first 5 s ignored, then 3 s in a row
    CHECK(a.level() == 2);
    // A one-second spike doesn't count; 3 s of a high bitrate do.
    CHECK(!a.update(20) && !a.update(1.5) && !a.update(20) && !a.update(20));
    CHECK(a.update(20) && a.level() == 1);
    CHECK(!a.update(0.2) && a.level() == 1);  // no measure: kept
    a.reset();
    CHECK(a.level() == 1);
    for (int s = 1; s <= 5; ++s) CHECK(!a.update(1.0));  // a new stream: the first 5 s again
}

static void testAudioEncoder() {
    xc::media::AudioEncoder enc;
    CHECK(enc.init(24000));
    CHECK(xc::media::AudioEncoder::kFrameSize == 960);

    // 960 samples of 440 Hz tone
    std::vector<int16_t> pcm(960);
    for (size_t i = 0; i < pcm.size(); ++i) {
        pcm[i] = static_cast<int16_t>(10000.0 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
    }
    for (int frame = 0; frame < 10; ++frame) {
        std::vector<uint8_t> opus;
        CHECK(enc.encode(pcm.data(), pcm.size(), opus));
        CHECK(!opus.empty());
        CHECK(opus.size() > 5 && opus.size() < 1000);
    }
}

// A burst of loss: a frame lost for good, then a frame with a hole of its
// own, then a complete key frame. The receiver must resume at that key frame
// (it used to stop at the frame with the hole: a freeze of seconds).
// The wait for a retransmission follows the round trip: none known keeps
// the default; a slow connection gets time for a NACK and a second try.

static void testAudioDecoderEmptyPacket() {
    xc::media::AudioEncoder enc;
    xc::media::AudioDecoder dec;
    CHECK(enc.init(24000));
    CHECK(dec.init());
    std::vector<int16_t> pcm(960);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(8000.0 * std::sin(i * 0.05));
    std::vector<uint8_t> opus;
    std::vector<float> out;
    CHECK(enc.encode(pcm.data(), pcm.size(), opus));
    CHECK(dec.decode(opus.data(), opus.size(), out));
    CHECK(!dec.decode(nullptr, 0, out));
    out.clear();
    CHECK(enc.encode(pcm.data(), pcm.size(), opus));
    CHECK(dec.decode(opus.data(), opus.size(), out));
    CHECK(!out.empty());
}

static void testAudioDecoderPlc() {
    xc::media::AudioEncoder enc;
    xc::media::AudioDecoder dec;
    CHECK(enc.init(24000));
    CHECK(dec.init());

    // 1. Concealment without history produces silence (zeros)
    std::vector<float> pcm;
    dec.conceal(pcm, 960);
    CHECK(pcm.size() == 960 * 2);
    for (float sample : pcm) {
        CHECK(sample == 0.0f);
    }

    // 2. Decode a known tone
    std::vector<int16_t> tone(960);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<int16_t>(10000.0 * std::sin(i * 0.1));
    std::vector<uint8_t> opus;
    CHECK(enc.encode(tone.data(), tone.size(), opus));
    pcm.clear();
    CHECK(dec.decode(opus.data(), opus.size(), pcm));
    CHECK(pcm.size() == 960 * 2);

    // 3. Concealment after valid frame extrapolates non-zero samples
    std::vector<float> concealed1;
    dec.conceal(concealed1, 960);
    CHECK(concealed1.size() == 960 * 2);
    bool nonZero = false;
    for (float sample : concealed1) {
        if (std::abs(sample) > 0.001f) nonZero = true;
    }
    CHECK(nonZero);

    // 4. Consecutive concealments decay amplitude
    std::vector<float> concealed2;
    dec.conceal(concealed2, 960);
    CHECK(concealed2.size() == 960 * 2);
    float max1 = 0.0f, max2 = 0.0f;
    for (float s : concealed1) max1 = std::max(max1, std::abs(s));
    for (float s : concealed2) max2 = std::max(max2, std::abs(s));
    CHECK(max2 < max1);

    // 5. Subsequent decode applies smooth ramp-in
    pcm.clear();
    CHECK(dec.decode(opus.data(), opus.size(), pcm));
    CHECK(pcm.size() == 960 * 2);
    CHECK(pcm[0] == 0.0f && pcm[1] == 0.0f);  // Ramped from 0 at sample 0

    // 6. Reset PLC clears history
    dec.resetPlc();
    pcm.clear();
    dec.conceal(pcm, 960);
    CHECK(pcm.size() == 960 * 2);
    for (float sample : pcm) {
        CHECK(sample == 0.0f);
    }
}

static void testAudioGapLossConcealment() {
    constexpr uint32_t kOpusFrameSamples = 960;
    auto detectLoss = [](uint32_t lastTs, uint32_t newTs) -> int {
        int32_t diff = static_cast<int32_t>(newTs - lastTs);
        if (diff <= 0) return -1; // duplicate or late out-of-order
        if (diff > static_cast<int32_t>(kOpusFrameSamples) && diff <= static_cast<int32_t>(kOpusFrameSamples * 6)) {
            return static_cast<int>((static_cast<uint32_t>(diff) + kOpusFrameSamples / 2) / kOpusFrameSamples - 1);
        }
        return 0; // normal step or huge stall
    };

    CHECK(detectLoss(1000, 1960) == 0);   // in-order 20ms step
    CHECK(detectLoss(1960, 1960) == -1);  // duplicate packet
    CHECK(detectLoss(2920, 1960) == -1);  // late packet from past
    CHECK(detectLoss(1000, 2920) == 1);   // 1 lost packet
    CHECK(detectLoss(1000, 3880) == 2);   // 2 lost packets
    CHECK(detectLoss(1000, 6760) == 5);   // 5 lost packets
    CHECK(detectLoss(1000, 100000) == 0); // huge stall: resync without concealing 100 frames
}

// Native mouse and keyboard reports: header, counts and fields.

static void testVideoDecoder() {
    media::VideoDecoder dec;
    CHECK(dec.init(1, false));
    CHECK(!dec.isHardware());
    CHECK(dec.hwPixFmt() == -1);
    CHECK(dec.needsKeyframe());

    media::Picture pic;
    CHECK(!dec.decode(nullptr, 0, pic));
    CHECK(!dec.decode(nullptr, 100, pic));
    uint8_t garbage[16] = {0, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(!dec.decode(garbage, 0, pic));
    CHECK(!dec.decode(garbage, sizeof garbage, pic));
    CHECK(dec.needsKeyframe());

    // Hardware init preference test (probes VA-API / Vulkan Video / D3D11VA and
    // gracefully falls back to software when unavailable or unsupported).
    media::VideoDecoder hwDec;
    CHECK(hwDec.init(1, true));
    if (!hwDec.isHardware()) {
        CHECK(hwDec.hwPixFmt() == -1);
    } else {
        CHECK(hwDec.hwPixFmt() >= 0);
    }

    // splitAccessUnits tests
    std::vector<uint8_t> stream = {0, 0, 0, 1, 9, 0x10, 0, 0, 0, 1, 7, 0x42, 0, 0, 0, 1, 8, 0xCE, 0, 0, 0, 1, 5, 0x88};
    auto aus = media::splitAccessUnits(stream.data(), stream.size());
    CHECK(!aus.empty());

    // H264 Slice QP Bitstream Parser tests
    media::H264BitstreamParser parser;
    CHECK(parser.parseAccessUnitQp(nullptr, 0) < 0);
    CHECK(parser.parseAccessUnitQp(garbage, sizeof garbage) < 0);

    // Real Annex-B Keyframe AU (SPS + PPS + IDR Slice)
    // SPS: 1280x720 Baseline, poc_type 2
    // PPS: id 0, pic_init_qp_minus26 = -3
    // IDR slice: delta = +4 -> QP = 26 - 3 + 4 = 27
    std::vector<uint8_t> keyframeAu = {
        0, 0, 0, 1, 0x67, 0x42, 0xc0, 0x1f, 0xd9, 0x80, 0x50, 0x05, 0xbb, 0x01, 0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x03, 0xc0, 0xf1, 0x83, 0x26, 0x80,
        0, 0, 0, 1, 0x68, 0xc9, 0x60, 0xf2, 0xc8,
        0, 0, 0, 1, 0x65, 0x88, 0x84, 0x11, 0xfc, 0x9c, 0x4e, 0x4f, 0x77, 0x8e, 0xa0, 0xc2, 0x00, 0x02, 0x02, 0xbe, 0x15, 0xa8, 0xb5, 0x9e, 0xf7, 0x1f, 0x7c, 0x98, 0x0f, 0xb3, 0x9c, 0x48, 0x2f, 0x6c, 0x66, 0xfb
    };

    double idrQp = parser.parseAccessUnitQp(keyframeAu.data(), keyframeAu.size());
    CHECK(idrQp == 27.0);

    // Verify SPS and PPS states cached in parser
    const auto* sps = parser.getSps(0);
    CHECK(sps != nullptr);
    CHECK(sps->valid);
    CHECK(sps->profileIdc == 66);
    CHECK(sps->levelIdc == 31);
    CHECK(sps->codedWidth == 1280 && sps->codedHeight == 720);
    CHECK(sps->picOrderCntType == 2);
    CHECK(sps->frameMbsOnly);

    // Real Xbox Series X xhome 1440p60 Main Level 5.1 SPS (2560x1440, 1 ref frame)
    {
        media::H264BitstreamParser p1440;
        const uint8_t sps1440[] = {
            0x4d, 0x00, 0x33, 0x91, 0xa0, 0x0a, 0x00, 0x2d, 0x68, 0x40, 0x00,
            0x00, 0xfa, 0x40, 0x00, 0x75, 0x30, 0x23, 0x68, 0x22, 0x11, 0xa8
        };
        CHECK(p1440.parseSps(sps1440, sizeof sps1440));
        const auto* s1440 = p1440.lastSps();
        CHECK(s1440 != nullptr && s1440->valid);
        CHECK(s1440->profileIdc == 77);
        CHECK(s1440->levelIdc == 51);
        CHECK(s1440->maxNumRefFrames == 1);
        CHECK(s1440->codedWidth == 2560 && s1440->codedHeight == 1440);
    }

    const auto* pps = parser.getPps(0);
    CHECK(pps != nullptr);
    CHECK(pps->valid);
    CHECK(pps->picInitQpMinus26 == -3);

    // Real P-frame AU referencing PPS 0 (without repeating SPS/PPS in packet)
    // P-slice: delta = +5 -> QP = 26 - 3 + 5 = 28
    std::vector<uint8_t> pFrameAu = {
        0, 0, 0, 1, 0x41, 0x9a, 0x38, 0x2b, 0xbd, 0x98, 0xa1, 0xe8, 0xb0, 0xe3, 0xf2, 0xf9, 0x84, 0x53, 0xcf, 0xe9, 0x5f, 0x2f, 0xe1, 0x3e, 0xd2, 0xbc, 0x21, 0x54, 0x76, 0x97, 0xa2, 0xb5, 0x74, 0x47, 0xfa, 0x2d
    };

    double pQp = parser.parseAccessUnitQp(pFrameAu.data(), pFrameAu.size());
    CHECK(pQp == 28.0);

    // VideoDecoder integration test: lastQp() is updated on parse
    media::VideoDecoder testDec;
    CHECK(testDec.init(1, false));
    CHECK(testDec.lastQp() == -1.0);
    double decIdrQp = testDec.parseAccessUnitQp(keyframeAu.data(), keyframeAu.size());
    CHECK(decIdrQp == 27.0);
    double decPQp = testDec.parseAccessUnitQp(pFrameAu.data(), pFrameAu.size());
    CHECK(decPQp == 28.0);
}

static void testDrawNv12() {
    // Boundary and parameter checks
    CHECK(!display::drawNv12(nullptr, nullptr, 0, 0, 0, 0, false));
    CHECK(!display::drawNv12(nullptr, nullptr, 1920, 1920, 1920, 1080, false));
    uint8_t dummy[16] = {};
    CHECK(!display::drawNv12(dummy, dummy, 10, 10, -1, 1080, false));
    CHECK(!display::drawNv12(dummy, dummy, 10, 10, 1920, -1, false));
    CHECK(!display::drawNv12(dummy, dummy, 100, 100, 1920, 1080, false));  // stride < width

    // Valid dummy frame in null display backend
    std::vector<uint8_t> y(1920 * 1080, 128);
    std::vector<uint8_t> uv(1920 * 540, 128);
    CHECK(display::drawNv12(y.data(), uv.data(), 1920, 1920, 1920, 1080, false));
}

// --- M1 input / audio backends (pure parts, null backends) ----------------

// The audio queue: bounded at ~250 ms (cut back to 140 ms, oldest first),
// silence past its end, the volume, and what the periodic log counts.

static void testAudioQueueLatency() {
    media::AudioQueue q;
    q.open();
    CHECK(q.takeLowWater() == SIZE_MAX && q.takeUnderruns() == 0);
    std::vector<float> out(512);
    q.pull(out.data(), 256);  // before any sound: not a dry pull
    CHECK(q.takeUnderruns() == 0);
    std::vector<float> tone(4800 * 2, 0.5f);  // 100 ms, constant
    q.push(tone.data(), 4800);
    q.pull(out.data(), 256);
    q.pull(out.data(), 256);
    CHECK(q.takeLowWater() == 4800 - 256);  // the lowest seen at a pull
    CHECK(q.takeLowWater() == SIZE_MAX);
    const size_t before = q.queuedFrames();
    CHECK(q.cutAhead(960));
    CHECK(q.queuedFrames() == before - 960);
    // Both sides of the cut were 0.5: the crossfade keeps it 0.5, no click.
    std::vector<float> rest(before * 2);
    q.pull(rest.data(), q.queuedFrames());
    bool flat = true;
    for (size_t i = 0; i < (before - 960) * 2; ++i) flat &= std::abs(rest[i] - 0.5f) < 1e-4f;
    CHECK(flat);
    CHECK(!q.cutAhead(960));  // nothing queued
    q.pull(out.data(), 256);  // dry, after sound
    CHECK(q.takeUnderruns() == 1);
    q.clear();
    q.pull(out.data(), 256);
    CHECK(q.takeUnderruns() == 0);  // cleared: not started again

    // A refill level: after running dry the sound waits for 40 ms queued.
    q.setRefill(1920);
    q.push(tone.data(), 300);  // 6 ms: plays out, then dry
    q.pull(out.data(), 256);
    q.pull(out.data(), 256);   // dry: refilling (one event, not one per grain)
    CHECK(q.takeUnderruns() == 1);
    q.push(tone.data(), 960);  // 20 ms: under the level, still silent
    q.pull(out.data(), 256);
    CHECK(out[100] == 0.0f && q.queuedFrames() == 960 && q.takeUnderruns() == 0);
    q.push(tone.data(), 1200);  // 45 ms queued: resumes, ramping in
    q.pull(out.data(), 256);
    CHECK(out[0] == 0.0f && std::abs(out[400] - 0.5f) < 1e-4f && q.queuedFrames() == 2160 - 256);
}

static void testAudioQueue() {
    media::AudioQueue q;
    std::vector<float> in(48 * 2 * 10, 0.25f);  // 10 ms
    q.push(in.data(), 480);
    CHECK(q.queuedFrames() == 0);  // closed: ignored
    q.open();
    for (int i = 0; i < 24; ++i) q.push(in.data(), 480);  // 240 ms: under the bound
    CHECK(q.queuedFrames() == 11520 && q.stats().trimmed == 0);
    q.push(in.data(), 480);  // 250 ms
    q.push(in.data(), 480);  // 260 ms: past 250, back to 140
    CHECK(q.queuedFrames() == media::AudioQueue::kTrimTo && q.stats().trimmed == 12480 - 6720);
    // The oldest go first: mark the newest frame and find it last.
    float mark[2] = {0.5f, -0.5f};
    q.push(mark, 1);
    std::vector<float> out(6721 * 2);
    q.pull(out.data(), 6721);
    CHECK(out[6720 * 2] == 0.5f && out[6720 * 2 + 1] == -0.5f && out[100 * 2] == 0.25f);
    CHECK(q.queuedFrames() == 0);
    q.pull(out.data(), 256);  // empty: silence, counted
    CHECK(out[0] == 0.0f && out[511] == 0.0f);
    media::AudioQueue::Stats st = q.stats();
    CHECK(st.pulls == 2 && st.empty == 1 && st.frames == 6721 + 256 && st.depthMax == 6721);
    // Volume: clamped to 0..2, applied and clipped at full scale.
    q.setVolume(5.0f);
    CHECK(q.volume() == 2.0f);
    float loud[4] = {0.75f, -0.25f, 0.1f, -0.9f};
    q.push(loud, 2);
    q.pull(out.data(), 2);
    CHECK(out[0] == 1.0f && out[1] == -0.5f && std::abs(out[2] - 0.2f) < 1e-6f && out[3] == -1.0f);
    q.setVolume(-1.0f);
    CHECK(q.volume() == 0.0f);
    q.setVolume(1.0f);
    // ~10 s of sound pulled (480000 frames): logged, and the counts start
    // again. 6979 frames so far, so the 1848th grain of 256 crosses it.
    for (int i = 0; i < 1875; ++i) q.pull(out.data(), 256);
    CHECK(q.stats().pulls == 27 && q.stats().frames == 27 * 256 && q.stats().trimmed == 0);
    q.close();
    q.push(loud, 2);
    CHECK(q.queuedFrames() == 0);
}

// The microphone's pipeline: gain (clipped), mute, the meter, 20 ms frames.

static void testMicPipeline() {
    media::MicPipeline mic;
    std::atomic<float> level{0.0f};
    std::vector<std::vector<int16_t>> frames;
    media::AudioIn::AudioCallback cb = [&](const int16_t* pcm, size_t n) { frames.emplace_back(pcm, pcm + n); };
    std::vector<int16_t> grain(media::MicPipeline::kGrain, 1000);
    for (int i = 0; i < 3; ++i) mic.process(grain.data(), grain.size(), false, 1.0f, level, cb);
    CHECK(frames.empty() && level.load() > 0.3f);  // 768 samples: not a frame yet
    mic.process(grain.data(), grain.size(), false, 1.0f, level, cb);
    CHECK(frames.size() == 1 && frames[0].size() == media::AudioIn::kFrameSize && frames[0][959] == 1000);
    std::vector<int16_t> loud(media::MicPipeline::kGrain, 30000);
    mic.process(loud.data(), loud.size(), false, 1.5f, level, cb);
    CHECK(loud[0] == 32767 && level.load() == 1.0f);  // clipped at full scale
    std::vector<int16_t> half(media::MicPipeline::kGrain, 1000);
    mic.process(half.data(), half.size(), false, 0.5f, level, cb);
    CHECK(half[0] == 500);
    // Muted: immediate zeroing of level meter and suppression of frames
    mic.process(grain.data(), grain.size(), true, 1.0f, level, cb);
    CHECK(level.load() == 0.0f);
    CHECK(frames.size() == 1);  // No frames dispatched while muted
    mic.process(grain.data(), grain.size(), true, 1.0f, level, cb);
    CHECK(frames.size() == 1);  // Still no frames dispatched while muted
    // Unmuted: resumes fresh accumulation
    mic.process(grain.data(), grain.size(), false, 1.0f, level, cb);
    CHECK(level.load() > 0.0f);
    CHECK(frames.size() == 1);
}

// The DualSense's trigger effects through SDL: the bytes for off, feedback
// (resistance) and vibration, and where each trigger's block sits.

static void testVoiceRouting() {
    // Default should be 0 (All Audio to TV / Main)
    media::audioSetVoiceRouting(0);
    CHECK(media::audioVoiceRouting() == 0);

    // Test routing switching: 1 = Pad Voice / Speaker
    media::audioSetVoiceRouting(1);
    CHECK(media::audioVoiceRouting() == 1);

    // Values > 1 clamp to 1, < 0 clamp to 0
    media::audioSetVoiceRouting(5);
    CHECK(media::audioVoiceRouting() == 1);

    media::audioSetVoiceRouting(-1);
    CHECK(media::audioVoiceRouting() == 0);

    // Test Settings JSON persistence
    std::string testPath = "/tmp/xcloud-tests-voice.json";
    app::Settings s;
    s.voiceRouting = 1;
    CHECK(s.save(testPath));

    app::Settings loaded;
    CHECK(loaded.load(testPath));
    CHECK(loaded.voiceRouting == 1);
    std::remove(testPath.c_str());

    // Test playVoiceAudio fallback on platforms without pad speaker
    std::vector<float> samplePcm(480 * 2, 0.1f);
    CHECK(media::playVoiceAudio(samplePcm.data(), 480));

    // Test SettingsChoice round-trip
    app::Settings st;
    st.voiceRouting = 1;
    ui::SettingsChoice ch = ui::choiceFromSettings(st);
    CHECK(ch.voiceRouting == 1);
    ch.voiceRouting = 0;
    ui::applyChoice(ch, st);
    CHECK(st.voiceRouting == 0);
}

static void testAudioToHaptics() {
    media::AudioToHaptics filter;
    filter.init(48000.0f, 120.0f);

    // 1. Frequency response: 50 Hz sine wave (sub-bass in passband)
    constexpr size_t kSamples = 960; // 20 ms
    std::vector<float> sine50(kSamples * 2);
    for (size_t i = 0; i < kSamples; ++i) {
        float s = std::sin(2.0f * 3.14159265f * 50.0f * i / 48000.0f);
        sine50[i * 2] = s;
        sine50[i * 2 + 1] = s;
    }
    float energyPass = 0.0f;
    for (size_t i = 0; i < kSamples; ++i) {
        float filtered = filter.processSample(sine50[i * 2]);
        if (i >= 480) energyPass += filtered * filtered;
    }
    energyPass /= 480.0f;
    CHECK(energyPass > 0.35f); // High passband transmission

    // 2. Frequency response: 1000 Hz sine wave (treble in stopband)
    filter.reset();
    std::vector<float> sine1000(kSamples * 2);
    for (size_t i = 0; i < kSamples; ++i) {
        float s = std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
        sine1000[i * 2] = s;
        sine1000[i * 2 + 1] = s;
    }
    float energyStop = 0.0f;
    for (size_t i = 0; i < kSamples; ++i) {
        float filtered = filter.processSample(sine1000[i * 2]);
        if (i >= 480) energyStop += filtered * filtered;
    }
    energyStop /= 480.0f;
    CHECK(energyStop < 0.005f); // > 20 dB strong high-frequency attenuation

    // 3. Envelope decay (~100 ms decay test)
    filter.reset();
    constexpr size_t kBurstSamples = 2400; // 50 ms of 50 Hz sub-bass burst
    std::vector<float> burst(kBurstSamples * 2);
    for (size_t i = 0; i < kBurstSamples; ++i) {
        float s = std::sin(2.0f * 3.14159265f * 50.0f * i / 48000.0f);
        burst[i * 2] = s;
        burst[i * 2 + 1] = s;
    }
    filter.process(burst.data(), kBurstSamples, 2);
    float envPeak = filter.envelope();
    CHECK(envPeak > 0.0f);
    std::vector<float> silence(4800 * 2, 0.0f); // 100 ms of silence
    filter.process(silence.data(), 4800, 2);
    float envDecayed = filter.envelope();
    CHECK(envDecayed < envPeak * 0.40f); // Decayed significantly over ~100 ms

    // 4. Sensitivity levels & kick activation
    media::setAudioHapticsLevel(0);
    CHECK(media::audioHapticsLevel() == 0);
    CHECK(!input::audioHapticsEnabled());

    media::setAudioHapticsLevel(2); // Medium
    CHECK(media::audioHapticsLevel() == 2);
    CHECK(input::audioHapticsEnabled());

    // 5. Settings and GameProfile serialization roundtrip
    app::Settings s;
    s.audioHaptics = 3; // Alto
    app::GameProfile gp = s.defaultProfile();
    CHECK(gp.audioHaptics == 3);
    gp.audioHaptics = 1; // Baixo
    s.perGame["TEST_GAME"] = gp;

    std::string testPath = "/tmp/test_haptics_settings.json";
    CHECK(s.save(testPath));

    app::Settings sLoaded;
    CHECK(sLoaded.load(testPath));
    CHECK(sLoaded.audioHaptics == 3);
    CHECK(sLoaded.perGame["TEST_GAME"].audioHaptics == 1);
    ::unlink(testPath.c_str());
}

static void testPs4VideoPipelineAndSse2Converter() {
    using namespace xc;
    // 1. HwDecoder::Picture default state including pending flag
    media::HwDecoder::Picture hp;
    CHECK(!hp.error);
    CHECK(!hp.pending);
    CHECK(hp.data == nullptr);

    // 1b. Verify PS4 HW decoder ring buffer slot progression during decode and flush
    constexpr size_t kOutputSlots = 6;
    auto simulateSlotAdvance = [](size_t currentSlot, bool decodeAccepted, bool needsFlush, bool flushAccepted) -> size_t {
        size_t slot = currentSlot;
        size_t outSlotUsed = slot;
        if (decodeAccepted) {
            slot = (outSlotUsed + 1) % kOutputSlots;
        }
        if (needsFlush) {
            size_t flushSlot = slot;
            if (flushAccepted) {
                slot = (flushSlot + 1) % kOutputSlots;
            }
        }
        return slot;
    };
    // Case A: decode accepted buffer, valid frame immediately (no flush needed) -> advances 1
    CHECK(simulateSlotAdvance(0, true, false, false) == 1);
    // Case B: decode accepted buffer, !valid -> flush accepts next slot -> advances 2
    CHECK(simulateSlotAdvance(0, true, true, true) == 2);
    // Case C: decode accepted buffer, !valid -> flush flushes without accepting new buffer -> advances 1
    CHECK(simulateSlotAdvance(0, true, true, false) == 1);
    // Case D: decode did not accept buffer, !valid -> flush accepts buffer -> advances 1
    CHECK(simulateSlotAdvance(0, false, true, true) == 1);
    // Case E: neither accepts buffer -> advances 0
    CHECK(simulateSlotAdvance(0, false, true, false) == 0);
    // Case F: wrap-around at ring boundary (slot 5 + 2 -> slot 1)
    CHECK(simulateSlotAdvance(5, true, true, true) == 1);

    // 2. ImageCache::stop() idempotency (called in coreShutdown before destructor)
    {
        ui::ImageCache cache([] {}, 4u << 20);
        cache.stop();
        cache.stop();
    }

#if defined(__x86_64__) || defined(_M_X64)
    // 3. Verify 8-pixel 16-bit SSE2 BT.709 Limited Range kernel and 3/2 (720p->1080p) shuffle
    auto colorRgbEightTest = [](__m128i y16, __m128i u16, __m128i v16, __m128i& p_lo, __m128i& p_hi) {
        const __m128i y_bias = _mm_set1_epi16(16);
        const __m128i uv_bias = _mm_set1_epi16(128);
        const __m128i zero16 = _mm_setzero_si128();
        const __m128i round = _mm_set1_epi32(128);
        const __m128i k_y_r  = _mm_set1_epi32((403 << 16) | (298 & 0xFFFF));
        const __m128i k_y_b  = _mm_set1_epi32((541 << 16) | (298 & 0xFFFF));
        const __m128i k_y_gu = _mm_set1_epi32(((-55 & 0xFFFF) << 16) | (298 & 0xFFFF));
        const __m128i k_gv_0 = _mm_set1_epi32(-137 & 0xFFFF);
        const __m128i alpha  = _mm_set1_epi16(static_cast<short>(0xFF00));

        __m128i c = _mm_max_epi16(_mm_sub_epi16(y16, y_bias), zero16);
        __m128i d = _mm_sub_epi16(u16, uv_bias);
        __m128i e = _mm_sub_epi16(v16, uv_bias);

        __m128i ce_lo = _mm_unpacklo_epi16(c, e);
        __m128i ce_hi = _mm_unpackhi_epi16(c, e);
        __m128i cd_lo = _mm_unpacklo_epi16(c, d);
        __m128i cd_hi = _mm_unpackhi_epi16(c, d);
        __m128i e0_lo = _mm_unpacklo_epi16(e, zero16);
        __m128i e0_hi = _mm_unpackhi_epi16(e, zero16);

        __m128i r_lo = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(ce_lo, k_y_r), round), 8);
        __m128i r_hi = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(ce_hi, k_y_r), round), 8);

        __m128i g_lo = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(cd_lo, k_y_gu), _mm_madd_epi16(e0_lo, k_gv_0)), round), 8);
        __m128i g_hi = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_madd_epi16(cd_hi, k_y_gu), _mm_madd_epi16(e0_hi, k_gv_0)), round), 8);

        __m128i b_lo = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(cd_lo, k_y_b), round), 8);
        __m128i b_hi = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(cd_hi, k_y_b), round), 8);

        __m128i r16 = _mm_packs_epi32(r_lo, r_hi);
        __m128i g16 = _mm_packs_epi32(g_lo, g_hi);
        __m128i b16 = _mm_packs_epi32(b_lo, b_hi);

        __m128i r8 = _mm_packus_epi16(r16, r16);
        __m128i g8 = _mm_packus_epi16(g16, g16);
        __m128i b8 = _mm_packus_epi16(b16, b16);

        __m128i rg = _mm_unpacklo_epi8(r8, g8);
        __m128i ba = _mm_or_si128(_mm_unpacklo_epi8(b8, zero16), alpha);
        p_lo = _mm_unpacklo_epi16(rg, ba);
        p_hi = _mm_unpackhi_epi16(rg, ba);
    };

    // Test boundary Y values: Y=0 (sub-black), Y=16 (black), Y=235 (white), Y=255 (super-white) with neutral UV=128
    __m128i y16 = _mm_setr_epi16(0, 16, 235, 255, 16, 235, 0, 255);
    __m128i u16 = _mm_set1_epi16(128);
    __m128i v16 = _mm_set1_epi16(128);
    __m128i p_lo, p_hi;
    colorRgbEightTest(y16, u16, v16, p_lo, p_hi);
    alignas(16) uint32_t px[8] = {};
    _mm_store_si128(reinterpret_cast<__m128i*>(px), p_lo);
    _mm_store_si128(reinterpret_cast<__m128i*>(px + 4), p_hi);
    CHECK(px[0] == 0xFF000000u); // Y=0 clamped to black
    CHECK(px[1] == 0xFF000000u); // Y=16 -> black
    CHECK(px[2] == 0xFFFFFFFFu); // Y=235 -> white
    CHECK(px[3] == 0xFFFFFFFFu); // Y=255 clamped to white

    // Test 3/2 horizontal register shuffle ([0, 0, 1, 2], [2, 3, 4, 4], [5, 6, 6, 7])
    __m128i s_lo = _mm_setr_epi32(10, 20, 30, 40);
    __m128i s_hi = _mm_setr_epi32(50, 60, 70, 80);
    __m128i out0 = _mm_shuffle_epi32(s_lo, _MM_SHUFFLE(2, 1, 0, 0));
    __m128i p23  = _mm_srli_si128(s_lo, 8);
    __m128i p44  = _mm_shuffle_epi32(s_hi, _MM_SHUFFLE(0, 0, 0, 0));
    __m128i out1 = _mm_unpacklo_epi64(p23, p44);
    __m128i out2 = _mm_shuffle_epi32(s_hi, _MM_SHUFFLE(3, 2, 2, 1));
    alignas(16) uint32_t scaled[12] = {};
    _mm_store_si128(reinterpret_cast<__m128i*>(scaled + 0), out0);
    _mm_store_si128(reinterpret_cast<__m128i*>(scaled + 4), out1);
    _mm_store_si128(reinterpret_cast<__m128i*>(scaled + 8), out2);
    const uint32_t expected[12] = {10, 10, 20, 30, 30, 40, 50, 50, 60, 70, 70, 80};
    for (int i = 0; i < 12; ++i) {
        CHECK(scaled[i] == expected[i]);
    }

    // Test unpackNv12UvEight and red conversion (verifying V channel is preserved, not zeroed)
    auto unpackNv12UvEightTest = [](const uint8_t* uvPtr, __m128i& u16, __m128i& v16) {
        const __m128i mask_low = _mm_set1_epi16(0x00FF);
        __m128i uv_bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(uvPtr));
        __m128i uv_dup = _mm_unpacklo_epi16(uv_bytes, uv_bytes);
        u16 = _mm_and_si128(uv_dup, mask_low);
        v16 = _mm_srli_epi16(uv_dup, 8);
    };

    alignas(16) const uint8_t sampleUv[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    __m128i testU16, testV16;
    unpackNv12UvEightTest(sampleUv, testU16, testV16);
    alignas(16) uint16_t uArr[8] = {};
    alignas(16) uint16_t vArr[8] = {};
    _mm_store_si128(reinterpret_cast<__m128i*>(uArr), testU16);
    _mm_store_si128(reinterpret_cast<__m128i*>(vArr), testV16);
    const uint16_t expectedU[8] = {10, 10, 30, 30, 50, 50, 70, 70};
    const uint16_t expectedV[8] = {20, 20, 40, 40, 60, 60, 80, 80};
    for (int i = 0; i < 8; ++i) {
        CHECK(uArr[i] == expectedU[i]);
        CHECK(vArr[i] == expectedV[i]);
    }

    // Pure Red (BT.709): Y=54, U=100, V=239. Must convert to vibrant red (R > 200), not green/cyan (R=0)!
    alignas(16) const uint8_t redUv[8] = {100, 239, 100, 239, 100, 239, 100, 239};
    __m128i redY16 = _mm_set1_epi16(54);
    __m128i redU16, redV16, redP_lo, redP_hi;
    unpackNv12UvEightTest(redUv, redU16, redV16);
    colorRgbEightTest(redY16, redU16, redV16, redP_lo, redP_hi);
    alignas(16) uint32_t redPx[8] = {};
    _mm_store_si128(reinterpret_cast<__m128i*>(redPx), redP_lo);
    _mm_store_si128(reinterpret_cast<__m128i*>(redPx + 4), redP_hi);
    uint32_t rVal = redPx[0] & 0xFF;
    uint32_t gVal = (redPx[0] >> 8) & 0xFF;
    uint32_t bVal = (redPx[0] >> 16) & 0xFF;
    CHECK(rVal > 200);
    CHECK(gVal < 40);
    CHECK(bVal < 40);
#endif

    // 4. Verify per-flip ptime timestamp resolution & stale observation rejection after >1s stall
    auto resolveFlipShownUs = [](int64_t flipArg, uint64_t ptime, uint64_t nowUs, uint64_t procNowUs,
                                 int64_t subMarker, uint64_t subUs, uint64_t subProcUs) -> uint64_t {
        uint64_t shownUs = 0;
        if (subMarker == flipArg && subUs > 0) {
            if (ptime > 0 && ptime >= subProcUs && ptime <= procNowUs) {
                shownUs = subUs + (ptime - subProcUs);
            } else if (nowUs >= subUs && (nowUs - subUs) <= 50000ULL) {
                shownUs = nowUs;
            }
        }
        return shownUs;
    };
    // Case A: Flip submitted at t=100,000 us, flipped at ptime=108,333 us (+8.33ms), but observed 2.05s later
    // after a network stall (t=2,156,400 us). Must resolve to 108,333 us (8.33ms), NOT 2,156,400 us!
    CHECK(resolveFlipShownUs(42, 108333ULL, 2156400ULL, 2156400ULL, 42, 100000ULL, 100000ULL) == 108333ULL);
    // Case B: Emulator/invalid ptime (0) observed 2.05s later after a stall -> must be rejected (0), never 2,156,400 us!
    CHECK(resolveFlipShownUs(42, 0ULL, 2156400ULL, 2156400ULL, 42, 100000ULL, 100000ULL) == 0ULL);
    // Case C: Emulator/invalid ptime (0) observed promptly within 8ms -> accepted as nowUs
    CHECK(resolveFlipShownUs(42, 0ULL, 108000ULL, 108000ULL, 42, 100000ULL, 100000ULL) == 108000ULL);

    // 5. Verify 1080p coded height (1920x1088) framebuffer sizing covers full 1088 luma + 540 chroma rows
    const size_t minCoded1088 = static_cast<size_t>(1920) * 1088 * 3 / 2 + 64;
    const size_t underSized1080 = static_cast<size_t>(1920) * 1080 * 3 / 2;
    const size_t rawFrameSize = std::max<size_t>(underSized1080, minCoded1088);
    const size_t alignedFrameSize = (rawFrameSize + 0x4000 - 1) & ~(static_cast<size_t>(0x4000) - 1);
    const size_t requiredNv12OffsetEnd = static_cast<size_t>(1920) * 1088 + static_cast<size_t>(1920) * (1080 / 2);
    CHECK(alignedFrameSize >= requiredNv12OffsetEnd);

    // 6. Verify SWAR Canvas::gradientV and opaque Canvas::drawImage alpha blending
    {
        ui::Canvas c(32, 16);
        c.clear(ui::rgba(20, 40, 60, 255));
        c.gradientV({0, 0, 32, 16}, ui::rgba(0, 0, 0, 0), ui::rgba(10, 20, 30, 200));
        CHECK((c.data()[0] & 0x00FFFFFFu) == (ui::rgba(20, 40, 60, 255) & 0x00FFFFFFu));
        ui::Image img;
        img.w = 16;
        img.h = 16;
        img.opaque = true;
        img.px.assign(16 * 16, ui::rgba(200, 100, 50, 255));
        c.drawImage(img, 0, 0, 128, 0);
        uint32_t blended = c.data()[0];
        CHECK(((blended >> 24) & 0xFF) == 0xFF);
        CHECK((blended & 0xFF) > 100 && (blended & 0xFF) < 120);
    }
}

static void testAndroidLifecycleAndMedia() {
    // 1. Android virtual key constant & SDL scancode mapping
    CHECK(xc::input::kVkAndroidBack == 0xA6);
    CHECK(xc::input::scancodeToVk(270) == xc::input::kVkAndroidBack);
    CHECK(xc::input::scancodeToVk(0x10E) == xc::input::kVkAndroidBack);

    // 2. Audio pause / resume API on null backend
    xc::media::audioPause();
    CHECK(!xc::media::audioIsPaused());
    xc::media::audioResume();
    CHECK(!xc::media::audioIsPaused());

    // 3. ImageCache shrink
    xc::ui::ImageCache cache([] {}, 10u << 20);
    cache.shrink();

    // 4. GPU DisplayPresentationMode enumeration
    CHECK(static_cast<int>(xc::display::gpu::DisplayPresentationMode::Standard) == 0);
    CHECK(static_cast<int>(xc::display::gpu::DisplayPresentationMode::TopScreenOnly) == 1);
}

static void testHwPictureContract() {
    using namespace xc::media;

    // 1. Verify default values ensure 100% backward compatibility
    Picture pic;
    CHECK(pic.uv == nullptr);
    CHECK(pic.uvPitchBytes == 0);
    CHECK(!pic.nv21);
    CHECK(pic.visibleHeight == 0);

    HwDecoder::Picture hp;
    CHECK(hp.uv == nullptr);
    CHECK(hp.uvPitchBytes == 0);
    CHECK(!hp.nv21);
    CHECK(hp.visibleHeight == 0);

    // 2. Simulate PS5/PS4 mapping: default fields yield standard NV12 layout
    uint8_t dummy[100];
    hp.data = dummy;
    hp.pitchBytes = 1920;
    hp.height = 1088;
    hp.width = 1920;
    hp.pts = 12345;

    Picture out;
    out.format = Picture::Format::Nv12;
    out.width = hp.width;
    out.height = hp.visibleHeight ? hp.visibleHeight : ((hp.height == 1088) ? 1080 : hp.height);
    out.strideY = hp.pitchBytes;
    out.strideU = hp.uvPitchBytes ? hp.uvPitchBytes : hp.pitchBytes;
    out.u = hp.uv ? hp.uv : (hp.data + hp.pitchBytes * hp.height);
    out.tag = static_cast<uint32_t>(hp.pts);
    out.nv21 = hp.nv21;

    CHECK(out.height == 1080);
    CHECK(out.strideU == 1920);
    CHECK(out.u == dummy + 1920 * 1088);
    CHECK(!out.nv21);

    // 3. Simulate Android NV21 mapping
    uint8_t uvDummy[100];
    hp.uv = uvDummy;
    hp.uvPitchBytes = 1920;
    hp.nv21 = true;
    hp.visibleHeight = 1080;

    Picture outNv21;
    outNv21.format = Picture::Format::Nv12;
    outNv21.width = hp.width;
    outNv21.height = hp.visibleHeight ? hp.visibleHeight : ((hp.height == 1088) ? 1080 : hp.height);
    outNv21.strideY = hp.pitchBytes;
    outNv21.strideU = hp.uvPitchBytes ? hp.uvPitchBytes : hp.pitchBytes;
    outNv21.u = hp.uv ? hp.uv : (hp.data + hp.pitchBytes * hp.height);
    outNv21.uv = hp.uv;
    outNv21.uvPitchBytes = hp.uvPitchBytes;
    outNv21.nv21 = hp.nv21;

    CHECK(outNv21.height == 1080);
    CHECK(outNv21.strideU == 1920);
    CHECK(outNv21.u == uvDummy);
    CHECK(outNv21.nv21);

    // 4. Test drawNv12 NV21 flag parameter
    std::vector<uint8_t> y(1920 * 1080, 128);
    std::vector<uint8_t> uv(1920 * 540, 128);
    CHECK(display::drawNv12(y.data(), uv.data(), 1920, 1920, 1920, 1080, false, true));
}

void runMediaTests() {
    testAutoDeband();
    testAudioEncoder();
    testAudioDecoderEmptyPacket();
    testAudioDecoderPlc();
    testAudioGapLossConcealment();
    testVideoDecoder();
    testDrawNv12();
    testAudioQueueLatency();
    testAudioQueue();
    testMicPipeline();
    testVoiceRouting();
    testAudioToHaptics();
    testPs4VideoPipelineAndSse2Converter();
    testAndroidLifecycleAndMedia();
    testHwPictureContract();
}
