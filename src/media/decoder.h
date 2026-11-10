// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// H.264 and Opus decoding through libavcodec (deps/ffmpeg, decoders only).
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

#include "media/hw_decoder.h"

namespace xc::media {

// A decoded picture in planar YUV 4:2:0 or semi-planar NV12; the planes stay
// valid until the next call to VideoDecoder::decode.
struct Picture {
    enum class Format : uint8_t {
        Yuv420p = 0,
        Nv12 = 1,
    };
    Format format = Format::Yuv420p;
    const uint8_t* y = nullptr;
    const uint8_t* u = nullptr;  // In NV12: points to interleaved UV plane
    const uint8_t* v = nullptr;  // In NV12: unused (nullptr)
    int strideY = 0, strideU = 0, strideV = 0;  // In NV12: strideU is UV row pitch
    int width = 0, height = 0;
    // The `tag` the access unit it came from was decoded with.
    uint32_t tag = 0;
};

// Splits an Annex-B H.264 stream into access units (offset, length): at AUD
// or SPS NALs and at first slices not preceded by parameter sets/SEI.
std::vector<std::pair<size_t, size_t>> splitAccessUnits(const uint8_t* data, size_t size);

// Restricts FFmpeg to plain C code paths (diagnostics); call before init().
void disableSimd();

struct H264Sps {
    bool valid = false;
    uint32_t profileIdc = 0;
    uint32_t spsId = 0;
    uint32_t chromaFormatIdc = 1;
    bool separateColourPlane = false;
    uint32_t log2MaxFrameNumMinus4 = 0;
    uint32_t picOrderCntType = 0;
    uint32_t log2MaxPicOrderCntLsbMinus4 = 0;
    bool deltaPicOrderAlwaysZero = false;
    bool frameMbsOnly = true;
};

struct H264Pps {
    bool valid = false;
    uint32_t ppsId = 0;
    uint32_t spsId = 0;
    bool entropyCodingMode = false;
    bool bottomFieldPicOrderPresent = false;
    uint32_t numSliceGroupsMinus1 = 0;
    uint32_t numRefIdxL0DefaultActiveMinus1 = 0;
    uint32_t numRefIdxL1DefaultActiveMinus1 = 0;
    bool weightedPred = false;
    uint32_t weightedBipredIdc = 0;
    int32_t picInitQpMinus26 = 0;
    bool redundantPicCntPresent = false;
};

class H264BitstreamParser {
public:
    H264BitstreamParser();
    void reset();

    // Parses NAL units in Annex-B access unit (updates SPS/PPS state and returns average Slice QP, or -1.0 if none)
    double parseAccessUnitQp(const uint8_t* data, size_t len);

    // Individual NAL parsers
    bool parseSps(const uint8_t* payload, size_t payloadLen);
    bool parsePps(const uint8_t* payload, size_t payloadLen);
    int parseSliceHeaderQp(const uint8_t* payload, size_t payloadLen, uint8_t nalUnitType, uint8_t nalRefIdc);

    const H264Sps* getSps(uint32_t id) const { return (id < 32 && spsMap_[id].valid) ? &spsMap_[id] : nullptr; }
    const H264Pps* getPps(uint32_t id) const { return (id < 256 && ppsMap_[id].valid) ? &ppsMap_[id] : nullptr; }

private:
    H264Sps spsMap_[32];
    H264Pps ppsMap_[256];
};

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // `threads` > 1 enables slice threading (frame threading would add a
    // frame of latency per thread).
    // `preferHw` enables hardware video decoding (libSceVideodec2 on PS5,
    // FFmpeg HWAccel on Desktop) with automatic software CPU fallback.
    bool init(int threads = 1, bool preferHw = true);
    // One Annex-B access unit in; true with `out` filled when a picture is
    // ready. A false return without a picture is normal at stream start.
    // `tag` travels with the access unit and comes back on its picture
    // (a picture can come out later than its input).
    bool decode(const uint8_t* data, size_t len, Picture& out, uint32_t tag = 0);
    // True after a decode error: the caller should ask for a key frame.
    bool needsKeyframe() const { return needsKeyframe_; }
    // The last decode() returned no picture without an error: the decoder
    // wants more input (frame threads still busy, or no reference yet).
    bool pending() const { return pending_; }
    // The quantiser of the last picture (its macroblocks' average, as the
    // encoder set it): lower is finer. -1 before any.
    double lastQp() const { return lastQp_; }
    // Whether hardware acceleration is currently active.
    bool isHardware() const { return isHw_; }
    // The hardware path's last decode() (measurements only): the decoder's
    // own decode and flush calls, and the QP parse before them (-1 when the
    // frame was not parsed).
    const HwDecoder::Timing& hwTiming() const { return hw_.lastTiming(); }
    int64_t lastQpParseUs() const { return qpParseUs_; }
    int hwPixFmt() const { return hwPixFmt_; }
    void setHardware(bool hw) { isHw_ = hw; }

    H264BitstreamParser& h264Parser() { return h264Parser_; }
    const H264BitstreamParser& h264Parser() const { return h264Parser_; }
    double parseAccessUnitQp(const uint8_t* data, size_t len) { return h264Parser_.parseAccessUnitQp(data, len); }

private:
    AVCodecContext* ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* swFrame_ = nullptr;
    void* hwDeviceCtx_ = nullptr;  // AVBufferRef*
    int hwPixFmt_ = -1;           // enum AVPixelFormat
    HwDecoder hw_;
    H264BitstreamParser h264Parser_;
    bool preferHw_ = true;
    bool isHw_ = false;
    int consecutiveHwErrors_ = 0;
    int consecutivePending_ = 0;
    bool needsKeyframe_ = true;
    bool profileLogged_ = false;
    bool pending_ = false;
    int failuresLogged_ = 0;
    double lastQp_ = -1;
    int64_t qpParseUs_ = -1;
    uint32_t qpSampleCounter_ = 0;
};

class AudioDecoder {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;

    AudioDecoder() = default;
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    bool init();
    // One Opus packet in; appends interleaved float stereo samples.
    bool decode(const uint8_t* data, size_t len, std::vector<float>& out);
    // Packet Loss Concealment (PLC): extrapolates audio across lost network frames.
    void conceal(std::vector<float>& out, size_t frames = 960);
    // Reset PLC state on stream start/reconnect.
    void resetPlc();

private:
    AVCodecContext* ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::vector<float> history_;
    float plcDecay_ = 1.0f;
    bool lastWasConcealed_ = false;
};

}  // namespace xc::media
