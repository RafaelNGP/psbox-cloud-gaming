// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/decoder.h"

#include "util/log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/cpu.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavutil/video_enc_params.h>
}

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

namespace xc::media {

namespace {

std::string averr(int rc) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(rc, buf, sizeof buf);
    return buf;
}

// FFmpeg's default log callback probes the terminal (isatty, getenv) and
// writes to stderr, which the console's sandbox does not support: route its
// messages to our log instead.
void ffmpegLog(void*, int level, const char* fmt, va_list ap) {
    if (level > AV_LOG_WARNING) return;
    char line[512];
    std::vsnprintf(line, sizeof line, fmt, ap);
    size_t n = std::strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (level <= AV_LOG_ERROR)
        XC_LOGE("ffmpeg: %s", line);
    else
        XC_LOGD("ffmpeg: %s", line);
}

void installLogCallback() {
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_callback(ffmpegLog); });
}

void freeAll(AVCodecContext*& ctx, AVFrame*& frame, AVPacket*& packet) {
    avcodec_free_context(&ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

}  // namespace

std::vector<std::pair<size_t, size_t>> splitAccessUnits(const uint8_t* data, size_t size) {
    std::vector<std::pair<size_t, size_t>> aus;
    size_t start = SIZE_MAX;
    int prevType = -1;
    for (size_t i = 0; i + 4 < size; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            int type = data[i + 3] & 0x1f;
            size_t sc = (i > 0 && data[i - 1] == 0) ? i - 1 : i;
            bool boundary = type == 9 || (type == 7 && prevType != 9) ||
                            ((type == 1 || type == 5) && prevType != 7 && prevType != 8 && prevType != 9 &&
                             prevType != 6 && (data[i + 4] & 0x80));
            if (boundary) {
                if (start != SIZE_MAX) aus.emplace_back(start, sc - start);
                start = sc;
            }
            prevType = type;
            i += 3;
        }
    }
    if (start != SIZE_MAX) aus.emplace_back(start, size - start);
    return aus;
}

namespace {

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool hasError() const { return error_; }

    uint32_t readBit() {
        if (bitPos_ >= size_ * 8) {
            error_ = true;
            return 0;
        }
        uint8_t byte = data_[bitPos_ >> 3];
        uint32_t bit = (byte >> (7 - (bitPos_ & 7))) & 1;
        ++bitPos_;
        return bit;
    }

    uint32_t readBits(int n) {
        if (n <= 0) return 0;
        if (n > 32 || bitPos_ + n > size_ * 8) {
            error_ = true;
            return 0;
        }
        uint32_t val = 0;
        while (n > 0) {
            size_t bitOffset = bitPos_ & 7;
            size_t byteOffset = bitPos_ >> 3;
            int bitsInByte = 8 - static_cast<int>(bitOffset);
            int take = std::min(n, bitsInByte);
            uint8_t byte = data_[byteOffset];
            uint32_t chunk = (byte >> (bitsInByte - take)) & ((1u << take) - 1u);
            val = (val << take) | chunk;
            bitPos_ += take;
            n -= take;
        }
        return val;
    }

    uint32_t readUe() {
        int leadingZeros = 0;
        while (!error_ && bitPos_ < size_ * 8) {
            size_t bitOffset = bitPos_ & 7;
            size_t byteOffset = bitPos_ >> 3;
            uint8_t byte = static_cast<uint8_t>(data_[byteOffset] << bitOffset);
            if (byte != 0) {
                int z = __builtin_clz(static_cast<unsigned int>(byte) << 24);
                leadingZeros += z;
                bitPos_ += z + 1;
                break;
            } else {
                int bitsLeft = 8 - static_cast<int>(bitOffset);
                leadingZeros += bitsLeft;
                bitPos_ += bitsLeft;
            }
        }
        if (error_ || leadingZeros > 31) {
            error_ = true;
            return 0;
        }
        if (leadingZeros == 0) return 0;
        uint32_t info = readBits(leadingZeros);
        return ((1u << leadingZeros) - 1u) + info;
    }

    int32_t readSe() {
        uint32_t codeNum = readUe();
        if (error_) return 0;
        if (codeNum & 1) {
            return static_cast<int32_t>((codeNum + 1) >> 1);
        } else {
            return -static_cast<int32_t>(codeNum >> 1);
        }
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t bitPos_ = 0;
    bool error_ = false;
};

size_t unescapeRbsp(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstMax) {
    size_t d = 0;
    int zeros = 0;
    for (size_t i = 0; i < srcLen && d < dstMax; ++i) {
        uint8_t b = src[i];
        if (zeros >= 2 && b == 3) {
            zeros = 0;
            continue;
        }
        if (b == 0) {
            ++zeros;
        } else {
            zeros = 0;
        }
        dst[d++] = b;
    }
    return d;
}

}  // namespace

H264BitstreamParser::H264BitstreamParser() {
    reset();
}

void H264BitstreamParser::reset() {
    for (auto& s : spsMap_) s = H264Sps{};
    for (auto& p : ppsMap_) p = H264Pps{};

    // Stream default fallback
    spsMap_[0].valid = true;
    spsMap_[0].spsId = 0;
    spsMap_[0].chromaFormatIdc = 1;
    spsMap_[0].frameMbsOnly = true;

    ppsMap_[0].valid = true;
    ppsMap_[0].ppsId = 0;
    ppsMap_[0].spsId = 0;
    ppsMap_[0].entropyCodingMode = true;
    ppsMap_[0].picInitQpMinus26 = 0;
}

bool H264BitstreamParser::parseSps(const uint8_t* payload, size_t payloadLen) {
    if (!payload || payloadLen == 0) return false;
    uint8_t rbsp[256];
    size_t rbspLen = unescapeRbsp(payload, std::min<size_t>(payloadLen, 256), rbsp, sizeof(rbsp));
    if (rbspLen == 0) return false;

    BitReader br(rbsp, rbspLen);
    H264Sps sps;
    sps.profileIdc = br.readBits(8);
    br.readBits(8); // constraint_set flags + reserved
    br.readBits(8); // level_idc
    sps.spsId = br.readUe();
    if (sps.spsId >= 32 || br.hasError()) return false;

    if (sps.profileIdc == 100 || sps.profileIdc == 110 || sps.profileIdc == 122 || sps.profileIdc == 244 ||
        sps.profileIdc == 44 || sps.profileIdc == 83 || sps.profileIdc == 86 || sps.profileIdc == 118 ||
        sps.profileIdc == 128 || sps.profileIdc == 138 || sps.profileIdc == 139 || sps.profileIdc == 134 ||
        sps.profileIdc == 135) {
        sps.chromaFormatIdc = br.readUe();
        if (sps.chromaFormatIdc == 3) {
            sps.separateColourPlane = br.readBit() != 0;
        }
        br.readUe(); // bit_depth_luma_minus8
        br.readUe(); // bit_depth_chroma_minus8
        br.readBit(); // qpprime_y_zero_transform_bypass_flag
        if (br.readBit()) { // seq_scaling_matrix_present_flag
            int count = (sps.chromaFormatIdc != 3) ? 8 : 12;
            for (int i = 0; i < count && !br.hasError(); ++i) {
                if (br.readBit()) {
                    int size = (i < 6) ? 16 : 64;
                    int lastScale = 8, nextScale = 8;
                    for (int j = 0; j < size && !br.hasError(); ++j) {
                        if (nextScale != 0) {
                            int delta = br.readSe();
                            nextScale = (lastScale + delta + 256) % 256;
                        }
                        lastScale = (nextScale == 0) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    sps.log2MaxFrameNumMinus4 = br.readUe();
    sps.picOrderCntType = br.readUe();
    if (sps.picOrderCntType == 0) {
        sps.log2MaxPicOrderCntLsbMinus4 = br.readUe();
    } else if (sps.picOrderCntType == 1) {
        sps.deltaPicOrderAlwaysZero = br.readBit() != 0;
        br.readSe(); // offset_for_non_ref_pic
        br.readSe(); // offset_for_top_to_bottom_field
        uint32_t numRefFramesInCycle = br.readUe();
        for (uint32_t i = 0; i < numRefFramesInCycle && !br.hasError(); ++i) {
            br.readSe();
        }
    }

    br.readUe(); // max_num_ref_frames
    br.readBit(); // gaps_in_frame_num_value_allowed_flag
    br.readUe(); // pic_width_in_mbs_minus1
    br.readUe(); // pic_height_in_map_units_minus1
    sps.frameMbsOnly = br.readBit() != 0;

    if (!br.hasError()) {
        sps.valid = true;
        spsMap_[sps.spsId] = sps;
        return true;
    }
    return false;
}

bool H264BitstreamParser::parsePps(const uint8_t* payload, size_t payloadLen) {
    if (!payload || payloadLen == 0) return false;
    uint8_t rbsp[256];
    size_t rbspLen = unescapeRbsp(payload, std::min<size_t>(payloadLen, 256), rbsp, sizeof(rbsp));
    if (rbspLen == 0) return false;

    BitReader br(rbsp, rbspLen);
    H264Pps pps;
    pps.ppsId = br.readUe();
    if (pps.ppsId >= 256 || br.hasError()) return false;
    pps.spsId = br.readUe();
    pps.entropyCodingMode = br.readBit() != 0;
    pps.bottomFieldPicOrderPresent = br.readBit() != 0;
    pps.numSliceGroupsMinus1 = br.readUe();
    if (pps.numSliceGroupsMinus1 > 0) {
        uint32_t type = br.readUe();
        if (type == 0) {
            for (uint32_t i = 0; i <= pps.numSliceGroupsMinus1 && !br.hasError(); ++i) br.readUe();
        } else if (type == 2) {
            for (uint32_t i = 0; i < pps.numSliceGroupsMinus1 && !br.hasError(); ++i) {
                br.readUe(); br.readUe();
            }
        } else if (type >= 3 && type <= 5) {
            br.readBit(); br.readUe();
        } else if (type == 6) {
            uint32_t picSize = br.readUe();
            int bits = 0;
            uint32_t temp = pps.numSliceGroupsMinus1;
            while (temp > 0) { bits++; temp >>= 1; }
            for (uint32_t i = 0; i <= picSize && !br.hasError(); ++i) br.readBits(bits);
        }
    }

    pps.numRefIdxL0DefaultActiveMinus1 = br.readUe();
    pps.numRefIdxL1DefaultActiveMinus1 = br.readUe();
    pps.weightedPred = br.readBit() != 0;
    pps.weightedBipredIdc = br.readBits(2);
    pps.picInitQpMinus26 = br.readSe();
    br.readSe(); // pic_init_qs_minus26
    br.readSe(); // chroma_qp_index_offset
    br.readBit(); // deblocking_filter_control_present_flag
    br.readBit(); // constrained_intra_pred_flag
    pps.redundantPicCntPresent = br.readBit() != 0;

    if (!br.hasError()) {
        pps.valid = true;
        ppsMap_[pps.ppsId] = pps;
        return true;
    }
    return false;
}

int H264BitstreamParser::parseSliceHeaderQp(const uint8_t* payload, size_t payloadLen, uint8_t nalUnitType, uint8_t nalRefIdc) {
    if (!payload || payloadLen == 0) return -1;

    uint8_t rbsp[256];
    size_t rbspLen = unescapeRbsp(payload, std::min<size_t>(payloadLen, 256), rbsp, sizeof(rbsp));
    if (rbspLen == 0) return -1;

    BitReader br(rbsp, rbspLen);
    br.readUe(); // first_mb_in_slice
    uint32_t sliceType = br.readUe();
    uint32_t sliceTypeMod = sliceType % 5; // 0: P, 1: B, 2: I, 3: SP, 4: SI
    uint32_t ppsId = br.readUe();
    if (ppsId >= 256 || br.hasError()) return -1;

    const H264Pps& pps = ppsMap_[ppsId].valid ? ppsMap_[ppsId] : ppsMap_[0];
    uint32_t spsId = pps.spsId < 32 ? pps.spsId : 0;
    const H264Sps& sps = spsMap_[spsId].valid ? spsMap_[spsId] : spsMap_[0];

    if (sps.separateColourPlane) {
        br.readBits(2); // colour_plane_id
    }

    uint32_t frameNumBits = sps.log2MaxFrameNumMinus4 + 4;
    br.readBits(static_cast<int>(frameNumBits));

    bool fieldPicFlag = false;
    if (!sps.frameMbsOnly) {
        fieldPicFlag = br.readBit() != 0;
        if (fieldPicFlag) {
            br.readBit(); // bottom_field_flag
        }
    }

    if (nalUnitType == 5) {
        br.readUe(); // idr_pic_id
    }

    if (sps.picOrderCntType == 0) {
        uint32_t pocLsbBits = sps.log2MaxPicOrderCntLsbMinus4 + 4;
        br.readBits(static_cast<int>(pocLsbBits));
        if (pps.bottomFieldPicOrderPresent && !fieldPicFlag) {
            br.readSe(); // delta_pic_order_cnt_bottom
        }
    } else if (sps.picOrderCntType == 1 && !sps.deltaPicOrderAlwaysZero) {
        br.readSe(); // delta_pic_order_cnt[0]
        if (pps.bottomFieldPicOrderPresent && !fieldPicFlag) {
            br.readSe(); // delta_pic_order_cnt[1]
        }
    }

    if (pps.redundantPicCntPresent) {
        br.readUe(); // redundant_pic_cnt
    }

    if (sliceTypeMod == 1) { // B slice
        br.readBit(); // direct_spatial_mv_pred_flag
    }

    uint32_t numRefIdxL0 = pps.numRefIdxL0DefaultActiveMinus1;
    uint32_t numRefIdxL1 = pps.numRefIdxL1DefaultActiveMinus1;
    if (sliceTypeMod == 0 || sliceTypeMod == 3 || sliceTypeMod == 1) { // P, SP, B
        if (br.readBit()) { // num_ref_idx_active_override_flag
            numRefIdxL0 = br.readUe();
            if (sliceTypeMod == 1) {
                numRefIdxL1 = br.readUe();
            }
        }
    }

    // ref_pic_list_modification()
    if (nalUnitType != 20 && nalUnitType != 21) {
        if (sliceTypeMod != 2 && sliceTypeMod != 4) { // not I and not SI
            if (br.readBit()) { // ref_pic_list_modification_flag_l0
                while (!br.hasError()) {
                    uint32_t idc = br.readUe();
                    if (idc == 3 || br.hasError()) break;
                    br.readUe(); // abs_diff_pic_num_minus1 or long_term_pic_num
                }
            }
        }
        if (sliceTypeMod == 1) { // B
            if (br.readBit()) { // ref_pic_list_modification_flag_l1
                while (!br.hasError()) {
                    uint32_t idc = br.readUe();
                    if (idc == 3 || br.hasError()) break;
                    br.readUe();
                }
            }
        }
    }

    // pred_weight_table()
    if ((pps.weightedPred && (sliceTypeMod == 0 || sliceTypeMod == 3)) ||
        (pps.weightedBipredIdc == 1 && sliceTypeMod == 1)) {
        br.readUe(); // luma_log2_weight_denom
        if (sps.chromaFormatIdc != 0) {
            br.readUe(); // chroma_log2_weight_denom
        }
        for (uint32_t i = 0; i <= numRefIdxL0 && !br.hasError(); ++i) {
            if (br.readBit()) {
                br.readSe(); // luma_weight_l0
                br.readSe(); // luma_offset_l0
            }
            if (sps.chromaFormatIdc != 0 && br.readBit()) {
                for (int j = 0; j < 2; ++j) {
                    br.readSe(); // chroma_weight_l0
                    br.readSe(); // chroma_offset_l0
                }
            }
        }
        if (sliceTypeMod == 1) {
            for (uint32_t i = 0; i <= numRefIdxL1 && !br.hasError(); ++i) {
                if (br.readBit()) {
                    br.readSe(); // luma_weight_l1
                    br.readSe(); // luma_offset_l1
                }
                if (sps.chromaFormatIdc != 0 && br.readBit()) {
                    for (int j = 0; j < 2; ++j) {
                        br.readSe(); // chroma_weight_l1
                        br.readSe(); // chroma_offset_l1
                    }
                }
            }
        }
    }

    // dec_ref_pic_marking()
    if (nalRefIdc != 0) {
        if (nalUnitType == 5) {
            br.readBit(); // no_output_of_prior_pics_flag
            br.readBit(); // long_term_reference_flag
        } else {
            if (br.readBit()) { // adaptive_ref_pic_marking_mode_flag
                while (!br.hasError()) {
                    uint32_t mmco = br.readUe();
                    if (mmco == 0 || br.hasError()) break;
                    if (mmco == 1 || mmco == 3) br.readUe();
                    if (mmco == 2) br.readUe();
                    if (mmco == 3 || mmco == 6) br.readUe();
                    if (mmco == 4) br.readUe();
                }
            }
        }
    }

    // cabac_init_idc
    if (pps.entropyCodingMode && sliceTypeMod != 2 && sliceTypeMod != 4) {
        br.readUe(); // cabac_init_idc
    }

    int32_t sliceQpDelta = br.readSe();
    if (br.hasError()) return -1;

    int qp = 26 + pps.picInitQpMinus26 + sliceQpDelta;
    return std::clamp(qp, 0, 51);
}

double H264BitstreamParser::parseAccessUnitQp(const uint8_t* data, size_t len) {
    if (!data || len < 4) return -1.0;

    // Fast-path: SPS, PPS and the first slice header are always within the first few KB.
    // Scanning the full access unit (which can be 20-50KB) wastes CPU cycles on the critical path.
    const size_t scanLimit = std::min<size_t>(len, 4096);
    size_t scOffsets[16];
    size_t scCount = 0;
    for (size_t i = 0; i + 2 < scanLimit && scCount < 16; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            scOffsets[scCount++] = i;
            i += 2;
        }
    }
    if (scCount == 0) return -1.0;

    for (size_t k = 0; k < scCount; ++k) {
        size_t nalStart = scOffsets[k] + 3;
        size_t nextSc = (k + 1 < scCount) ? scOffsets[k + 1] : len;
        size_t nalEnd = nextSc;
        while (nalEnd > nalStart && data[nalEnd - 1] == 0) {
            --nalEnd;
        }
        if (nalStart >= nalEnd) continue;

        uint8_t header = data[nalStart];
        uint8_t nalUnitType = header & 0x1F;
        uint8_t nalRefIdc = (header >> 5) & 3;
        const uint8_t* payload = data + nalStart + 1;
        size_t payloadLen = nalEnd - (nalStart + 1);

        if (nalUnitType == 7) {
            parseSps(payload, payloadLen);
        } else if (nalUnitType == 8) {
            parsePps(payload, payloadLen);
        } else if (nalUnitType == 1 || nalUnitType == 5) {
            int qp = parseSliceHeaderQp(payload, payloadLen, nalUnitType, nalRefIdc);
            if (qp >= 0) {
                // In streaming, all slices in an access unit share the base frame QP.
                // Stopping at the first slice eliminates parsing subsequent slices.
                return static_cast<double>(qp);
            }
        }
    }

    return -1.0;
}

void disableSimd() {
    av_force_cpu_flags(0);
    XC_LOGW("FFmpeg SIMD disabled (cpu flags 0)");
}

// --- Video --------------------------------------------------------------------

static enum AVPixelFormat getHwFormat(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    auto* self = static_cast<VideoDecoder*>(ctx->opaque);
    if (self && self->hwPixFmt() >= 0) {
        for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (static_cast<int>(*p) == self->hwPixFmt()) return *p;
        }
        // Hardware format not supported by the codec for this stream; fall back to software
        self->setHardware(false);
    }
    for (const enum AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_YUV420P || *p == AV_PIX_FMT_YUVJ420P || *p == AV_PIX_FMT_NV12) return *p;
    }
    return AV_PIX_FMT_NONE;
}

VideoDecoder::~VideoDecoder() {
    freeAll(ctx_, frame_, packet_);
    if (swFrame_) av_frame_free(&swFrame_);
    if (hwDeviceCtx_) {
        auto* ref = reinterpret_cast<AVBufferRef*>(hwDeviceCtx_);
        av_buffer_unref(&ref);
        hwDeviceCtx_ = nullptr;
    }
}

bool VideoDecoder::init(int threads, bool preferHw) {
    installLogCallback();
    preferHw_ = preferHw;
    isHw_ = false;
    consecutiveHwErrors_ = 0;
    consecutivePending_ = 0;
    lastQp_ = -1;
    qpSampleCounter_ = 0;
    h264Parser_.reset();

    // 1. Hardware video decoder (PS5 libSceVideodec2 via HwDecoder)
    if (preferHw_) {
        if (hw_.init(1920, 1088)) {
            isHw_ = true;
            XC_LOGI("h264 decoder: hardware decoder (libSceVideodec2) active");
        } else {
            XC_LOGI("h264 decoder: hardware decoder not available");
        }
    }

    // 2. FFmpeg context (primary software decoder or fallback)
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        XC_LOGE("no H.264 decoder in this FFmpeg build");
        return isHw_;
    }
    ctx_ = avcodec_alloc_context3(codec);
    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!ctx_ || !frame_ || !packet_) return isHw_;

    // 3. If hardware requested and PS5 hardware decoder is not available, try FFmpeg HWAccel
    if (preferHw_ && !isHw_) {
        static const struct {
            enum AVHWDeviceType devType;
            enum AVPixelFormat pixFmt;
        } kHwConfigs[] = {
            // Vulkan Video first: the desktop build's FFmpeg has it (libvulkan
            // opened at run time); the others only where a build adds them.
            {AV_HWDEVICE_TYPE_VULKAN, AV_PIX_FMT_VULKAN},
            {AV_HWDEVICE_TYPE_VAAPI, AV_PIX_FMT_VAAPI},
            {AV_HWDEVICE_TYPE_CUDA,  AV_PIX_FMT_CUDA},
            {AV_HWDEVICE_TYPE_VDPAU, AV_PIX_FMT_VDPAU},
        };
        for (const auto& cfg : kHwConfigs) {
            AVBufferRef* hwCtx = nullptr;
            int rc = av_hwdevice_ctx_create(&hwCtx, cfg.devType, nullptr, nullptr, 0);
            if (rc >= 0 && hwCtx) {
                hwDeviceCtx_ = hwCtx;
                hwPixFmt_ = cfg.pixFmt;
                swFrame_ = av_frame_alloc();
                ctx_->hw_device_ctx = av_buffer_ref(hwCtx);
                ctx_->opaque = this;
                ctx_->get_format = getHwFormat;
                isHw_ = true;
                XC_LOGI("h264 decoder: FFmpeg hardware acceleration (%s) active",
                        av_hwdevice_get_type_name(cfg.devType));
                break;
            }
        }
    }

    ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
    ctx_->thread_count = std::max(1, threads);
    if (threads > 1 && !isHw_) {
        ctx_->thread_type = FF_THREAD_FRAME;
    } else {
        ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        ctx_->thread_type = FF_THREAD_SLICE;
    }
    XC_LOGI("h264 decoder: cpu flags 0x%x, %d thread(s), mode: %s",
            av_get_cpu_flags(), ctx_->thread_count, isHw_ ? "Hardware" : "Software");
    ctx_->export_side_data |= AV_CODEC_EXPORT_DATA_VIDEO_ENC_PARAMS;
    int rc = avcodec_open2(ctx_, codec, nullptr);
    if (rc < 0 && isHw_ && !hw_.ready()) {
        XC_LOGW("h264 decoder: avcodec_open2 with hwaccel failed (%s), retrying software", averr(rc).c_str());
        isHw_ = false;
        if (swFrame_) { av_frame_free(&swFrame_); swFrame_ = nullptr; }
        if (hwDeviceCtx_) {
            auto* ref = reinterpret_cast<AVBufferRef*>(hwDeviceCtx_);
            av_buffer_unref(&ref);
            hwDeviceCtx_ = nullptr;
        }
        avcodec_free_context(&ctx_);
        ctx_ = avcodec_alloc_context3(codec);
        if (!ctx_) return false;
        ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
        ctx_->thread_count = std::max(1, threads);
        ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        ctx_->thread_type = FF_THREAD_SLICE;
        ctx_->export_side_data |= AV_CODEC_EXPORT_DATA_VIDEO_ENC_PARAMS;
        rc = avcodec_open2(ctx_, codec, nullptr);
    }
    if (rc < 0) {
        XC_LOGE("avcodec_open2(h264): %s", averr(rc).c_str());
        if (!hw_.ready()) return false;
    }
    return true;
}

bool VideoDecoder::decode(const uint8_t* data, size_t len, Picture& out, uint32_t tag) {
    pending_ = false;
    if (!data || len == 0) return false;

    // --- Path 1: PS5 Hardware Decoder (libSceVideodec2) ---
    if (isHw_ && hw_.ready()) {
        const bool isKey = (len >= 5) && (data[0] == 0 && data[1] == 0 &&
            ((data[2] == 1 && ((data[3] & 0x1F) == 5 || (data[3] & 0x1F) == 7)) ||
             (data[2] == 0 && data[3] == 1 && ((data[4] & 0x1F) == 5 || (data[4] & 0x1F) == 7))));

        qpParseUs_ = -1;
        if (lastQp_ < 0 || isKey || (++qpSampleCounter_ % 8 == 0)) {
            const auto qp0 = std::chrono::steady_clock::now();
            double parsedQp = h264Parser_.parseAccessUnitQp(data, len);
            qpParseUs_ = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - qp0).count());
            if (parsedQp >= 0) {
                lastQp_ = parsedQp;
            }
        }

        HwDecoder::Picture hp;
        bool ok = hw_.decode(data, len, tag, hp);
        if (ok && !hp.error) {
            consecutiveHwErrors_ = 0;
            consecutivePending_ = 0;
            needsKeyframe_ = false;
            out.format = Picture::Format::Nv12;
            out.width = hp.width;
            out.height = (hp.height == 1088) ? 1080 : hp.height;
            out.strideY = hp.pitchBytes;
            out.strideU = hp.pitchBytes;
            out.strideV = 0;
            out.y = hp.data;
            out.u = hp.data + hp.pitchBytes * hp.height;
            out.v = nullptr;
            out.tag = static_cast<uint32_t>(hp.pts);
            if (!profileLogged_) {
                profileLogged_ = true;
                XC_LOGI("hwdec stream active: %ux%u (pitch %u, nv12)", out.width, out.height, out.strideY);
            }
            return true;
        }
        if (hp.pending) {
            ++consecutivePending_;
            if (consecutivePending_ <= 10) {
                pending_ = true;
                return false;
            }
            XC_LOGW("hwdec: priming ceiling exceeded (%d consecutive pending frames)", consecutivePending_);
        } else {
            consecutivePending_ = 0;
        }

        needsKeyframe_ = true;
        ++consecutiveHwErrors_;
        if (failuresLogged_++ < 20) {
            XC_LOGW("hwdec: frame decode failed (consecutive: %d, len: %zu)", consecutiveHwErrors_, len);
        }
#if defined(XC_HWDEC_NO_SOFTWARE_FALLBACK)
        // PS4: the hardware decoder resets itself and waits for the IDR asked for here.
        // FFmpeg is no fallback on this CPU: it decodes 1080p far below the frame rate, and
        // fed P-frames it has no references for, so switching to it froze the stream.
        return false;
#endif
        // A broken frame (a lost reference, a corrupt slice): the key frame
        // asked for above brings the hardware decoder back, as on the PS4.
        // Only errors that outlast several of them (~2 s at 60 fps) move the
        // stream to FFmpeg for good: one CPU thread does not keep up with
        // 1080p60, so that fallback is a last resort. Until then, this frame
        // is lost.
        if (consecutiveHwErrors_ < 120) return false;
        if (consecutiveHwErrors_ == 120) {
            XC_LOGE("hwdec: %d decode errors in a row, falling back to the CPU software decoder", consecutiveHwErrors_);
            isHw_ = false;
            if (ctx_) avcodec_flush_buffers(ctx_);
        }
        // Fall back to CPU FFmpeg decode for this packet
    }

    if (!ctx_) return false;
    packet_->data = const_cast<uint8_t*>(data);
    packet_->size = static_cast<int>(len);
    packet_->pts = tag;
    int rc = avcodec_send_packet(ctx_, packet_);
    if (rc == AVERROR_EOF) {
        avcodec_flush_buffers(ctx_);
        rc = avcodec_send_packet(ctx_, packet_);
    }
    if (rc < 0 && rc != AVERROR(EAGAIN)) {
        needsKeyframe_ = true;
        if (failuresLogged_++ == 0) XC_LOGD("h264 send failed: %s (%zu bytes)", averr(rc).c_str(), len);
        return false;
    }
    int sent = rc;
    rc = avcodec_receive_frame(ctx_, frame_);
    if (rc == AVERROR(EAGAIN)) {
        pending_ = true;
        return false;
    }
    if (rc < 0) {
        needsKeyframe_ = true;
        if (failuresLogged_++ == 0)
            XC_LOGD("h264 no picture: send=%d receive=%s (%zu bytes, first NAL type %d)", sent, averr(rc).c_str(),
                    len, len > 4 ? data[4] & 0x1f : -1);
        return false;
    }
    if (sent == AVERROR(EAGAIN)) {
        int again = avcodec_send_packet(ctx_, packet_);
        if (again < 0) {
            needsKeyframe_ = true;
            if (failuresLogged_++ == 0) XC_LOGD("h264 packet dropped: %s (%zu bytes)", averr(again).c_str(), len);
        }
    }

    AVFrame* targetFrame = frame_;
    if (hwDeviceCtx_ && frame_->format == hwPixFmt_ && swFrame_) {
        int trans = av_hwframe_transfer_data(swFrame_, frame_, 0);
        if (trans < 0) {
            needsKeyframe_ = true;
            XC_LOGW("ffmpeg: av_hwframe_transfer_data failed: %s", averr(trans).c_str());
            return false;
        }
        targetFrame = swFrame_;
        targetFrame->pts = frame_->pts;
    }

    if (AVFrameSideData* sd = av_frame_get_side_data(frame_, AV_FRAME_DATA_VIDEO_ENC_PARAMS)) {
        auto* par = reinterpret_cast<AVVideoEncParams*>(sd->data);
        double sum = 0;
        for (unsigned i = 0; i < par->nb_blocks; ++i) sum += par->qp + av_video_enc_params_block(par, i)->delta_qp;
        lastQp_ = par->nb_blocks ? sum / par->nb_blocks : par->qp;
    } else {
        double parsedQp = h264Parser_.parseAccessUnitQp(data, len);
        if (parsedQp >= 0) {
            lastQp_ = parsedQp;
        }
    }
    if (frame_->decode_error_flags || (frame_->flags & AV_FRAME_FLAG_CORRUPT)) {
        needsKeyframe_ = true;
    } else if (frame_->flags & AV_FRAME_FLAG_KEY) {
        needsKeyframe_ = false;
    }

    if (targetFrame->format == AV_PIX_FMT_NV12) {
        out.format = Picture::Format::Nv12;
        out.y = targetFrame->data[0];
        out.u = targetFrame->data[1];
        out.v = nullptr;
        out.strideY = targetFrame->linesize[0];
        out.strideU = targetFrame->linesize[1];
        out.strideV = 0;
        out.width = targetFrame->width;
        out.height = targetFrame->height;
    } else if (targetFrame->format == AV_PIX_FMT_YUV420P || targetFrame->format == AV_PIX_FMT_YUVJ420P) {
        out.format = Picture::Format::Yuv420p;
        out.y = targetFrame->data[0];
        out.u = targetFrame->data[1];
        out.v = targetFrame->data[2];
        out.strideY = targetFrame->linesize[0];
        out.strideU = targetFrame->linesize[1];
        out.strideV = targetFrame->linesize[2];
        out.width = targetFrame->width;
        out.height = targetFrame->height;
    } else {
        if (failuresLogged_++ < 20) XC_LOGW("unexpected pixel format %d", targetFrame->format);
        return false;
    }

    if (!profileLogged_) {
        profileLogged_ = true;
        XC_LOGI("h264 stream: profile %d, level %d (%s)", ctx_->profile, ctx_->level,
                out.format == Picture::Format::Nv12 ? "NV12" : "YUV420P");
    }
    out.tag = targetFrame->pts == AV_NOPTS_VALUE ? 0 : static_cast<uint32_t>(targetFrame->pts);
    return true;
}

// --- Audio --------------------------------------------------------------------

AudioDecoder::~AudioDecoder() { freeAll(ctx_, frame_, packet_); }

bool AudioDecoder::init() {
    installLogCallback();
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    if (!codec) {
        XC_LOGE("no Opus decoder in this FFmpeg build");
        return false;
    }
    ctx_ = avcodec_alloc_context3(codec);
    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!ctx_ || !frame_ || !packet_) return false;
    ctx_->sample_rate = kSampleRate;
    av_channel_layout_default(&ctx_->ch_layout, kChannels);
    int rc = avcodec_open2(ctx_, codec, nullptr);
    if (rc < 0) {
        XC_LOGE("avcodec_open2(opus): %s", averr(rc).c_str());
        return false;
    }
    return true;
}

void AudioDecoder::resetPlc() {
    history_.clear();
    plcDecay_ = 1.0f;
    lastWasConcealed_ = false;
}

void AudioDecoder::conceal(std::vector<float>& out, size_t frames) {
    if (frames == 0) return;
    size_t base = out.size();
    out.resize(base + frames * 2);
    size_t histFrames = history_.size() / 2;
    if (histFrames == 0) {
        std::fill(out.begin() + static_cast<long>(base), out.end(), 0.0f);
        return;
    }
    // Repeat recent waveform period with smooth decaying attenuation
    size_t period = std::min(frames, histFrames);
    size_t histStart = (histFrames - period) * 2;
    for (size_t i = 0; i < frames; ++i) {
        size_t srcIdx = histStart + (i % period) * 2;
        float progress = static_cast<float>(i) / static_cast<float>(frames);
        float decay = plcDecay_ * (1.0f - 0.4f * progress);
        if (decay < 0.0f) decay = 0.0f;
        out[base + 2 * i] = history_[srcIdx] * decay;
        out[base + 2 * i + 1] = history_[srcIdx + 1] * decay;
    }
    plcDecay_ = std::max(0.0f, plcDecay_ * 0.6f);
    lastWasConcealed_ = true;
}

bool AudioDecoder::decode(const uint8_t* data, size_t len, std::vector<float>& out) {
    // An empty packet is "end of stream" to libavcodec: every packet after
    // it would be refused (no sound for the rest of the session).
    if (!ctx_ || !data || len == 0) return false;
    packet_->data = const_cast<uint8_t*>(data);
    packet_->size = static_cast<int>(len);
    int rc = avcodec_send_packet(ctx_, packet_);
    if (rc == AVERROR_EOF) {  // drained anyway: reset and retry once
        avcodec_flush_buffers(ctx_);
        rc = avcodec_send_packet(ctx_, packet_);
    }
    if (rc < 0) return false;
    bool any = false;
    size_t startSize = out.size();
    while (avcodec_receive_frame(ctx_, frame_) == 0) {
        int n = frame_->nb_samples;
        int ch = frame_->ch_layout.nb_channels;
        // FFmpeg's Opus decoder outputs planar float.
        if (frame_->format != AV_SAMPLE_FMT_FLTP || ch < 1) continue;
        const float* l = reinterpret_cast<const float*>(frame_->data[0]);
        const float* r = reinterpret_cast<const float*>(frame_->data[ch > 1 ? 1 : 0]);
        size_t base = out.size();
        out.resize(base + static_cast<size_t>(n) * 2);
        for (int i = 0; i < n; ++i) {
            out[base + 2 * i] = l[i];
            out[base + 2 * i + 1] = r[i];
        }
        any = true;
    }
    if (any) {
        size_t decodedFloats = out.size() - startSize;
        if (lastWasConcealed_) {
            // Smoothly ramp in newly decoded audio over the first ~2 ms (96 stereo frames)
            // to eliminate DC step pops after concealed frames.
            size_t rampFrames = std::min<size_t>(96, decodedFloats / 2);
            for (size_t i = 0; i < rampFrames; ++i) {
                float ramp = static_cast<float>(i) / static_cast<float>(rampFrames);
                out[startSize + 2 * i] *= ramp;
                out[startSize + 2 * i + 1] *= ramp;
            }
            lastWasConcealed_ = false;
        }
        plcDecay_ = 1.0f;
        // Keep up to 100 ms of history for PLC synthesis
        history_.insert(history_.end(), out.begin() + static_cast<long>(startSize), out.end());
        constexpr size_t kMaxHistoryFloats = kSampleRate * 2 * 100 / 1000;
        if (history_.size() > kMaxHistoryFloats) {
            history_.erase(history_.begin(), history_.begin() + static_cast<long>(history_.size() - kMaxHistoryFloats));
        }
    }
    return any;
}

}  // namespace xc::media
