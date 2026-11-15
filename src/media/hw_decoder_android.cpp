// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// H.264 on Android via NDK AMediaCodec in ByteBuffer mode (linear CPU-accessible YUV420).
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <chrono>
#include "media/hw_decoder.h"
#include "util/log.h"

#if defined(__ANDROID__)
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <dlfcn.h>
#endif

namespace xc::media {

#if defined(__ANDROID__)
// AOSP MediaImage2 descriptor passed via "image-data" in MediaFormat (Android 6.0+)
struct AndroidMediaImage2 {
    uint32_t type = 0;
    uint32_t numPlanes = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bitDepth = 0;
    uint32_t bitDepthAllocated = 0;
    struct PlaneInfo {
        uint32_t offset = 0;
        int32_t colInc = 0;
        int32_t rowInc = 0;
        uint32_t horizSubsampling = 0;
        uint32_t vertSubsampling = 0;
    } plane[3];
};

struct AndroidCodecState {
    AMediaCodec* codec = nullptr;
    ssize_t currentOutIdx = -1;
    int width = 1920;
    int height = 1080;
    int stride = 1920;
    int sliceHeight = 1080;
    int cropLeft = 0;
    int cropTop = 0;
    int cropRight = 0;
    int cropBottom = 0;
    int colorFormat = 21; // 19 = Planar (I420), 21 = SemiPlanar (NV12), 0x7f420888 = Flexible
    bool hasImageData = false;
    AndroidMediaImage2 imageInfo{};
};
#endif

HwDecoder::~HwDecoder() {
#if defined(__ANDROID__)
    if (decoder_) {
        auto* s = static_cast<AndroidCodecState*>(decoder_);
        if (s->currentOutIdx >= 0 && s->codec) {
            AMediaCodec_releaseOutputBuffer(s->codec, static_cast<size_t>(s->currentOutIdx), false);
            s->currentOutIdx = -1;
        }
        if (s->codec) {
            AMediaCodec_stop(s->codec);
            AMediaCodec_delete(s->codec);
            s->codec = nullptr;
        }
        delete s;
        decoder_ = nullptr;
    }
#endif
}

bool HwDecoder::init(int maxWidth, int maxHeight) {
#if defined(__ANDROID__)
    auto* s = new AndroidCodecState();
    s->width = maxWidth > 0 ? maxWidth : 1920;
    s->height = maxHeight > 0 ? maxHeight : 1088;
    s->stride = s->width;
    s->sliceHeight = s->height;

    // 1. Create AMediaCodec for H.264
    s->codec = AMediaCodec_createDecoderByType("video/avc");
    if (!s->codec) {
        XC_LOGE("hw_decoder_android: AMediaCodec_createDecoderByType(video/avc) failed");
        delete s;
        return false;
    }

    // 2. Configure format with low-latency flags and COLOR_FormatYUV420Flexible
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, s->width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, s->height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7f420888); // COLOR_FormatYUV420Flexible
    AMediaFormat_setInt32(format, "max-width", s->width);
    AMediaFormat_setInt32(format, "max-height", s->height);

    // Standard low-latency keys
    AMediaFormat_setInt32(format, "low-latency", 1);
    AMediaFormat_setInt32(format, "priority", 0);         // Real-time priority
    AMediaFormat_setInt32(format, "operating-rate", 120); // 120 Hz processing rate

    // Vendor low-latency extensions
    AMediaFormat_setInt32(format, "vendor.qcom-ext-dec-low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.qti-ext-dec-low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.qti-ext-dec-picture-order.enable", 1);
    AMediaFormat_setInt32(format, "vendor.rtc-ext-dec-low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.sec-ext-dec-low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.hisi-ext-low-latency-video-dec.video-scene-for-low-latency-req", 1);

    // ByteBuffer mode: pass window = nullptr so CCodec outputs CPU-readable buffers
    media_status_t cStatus = AMediaCodec_configure(s->codec, format, nullptr, nullptr, 0);
    if (cStatus != AMEDIA_OK) {
        XC_LOGW("hw_decoder_android: AMediaCodec_configure with 0x7f420888 failed (%d), retrying 21", cStatus);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 21); // COLOR_FormatYUV420SemiPlanar
        cStatus = AMediaCodec_configure(s->codec, format, nullptr, nullptr, 0);
        if (cStatus != AMEDIA_OK) {
            XC_LOGW("hw_decoder_android: retry with 21 failed (%d), retrying without COLOR_FORMAT", cStatus);
            AMediaCodec_delete(s->codec);
            s->codec = AMediaCodec_createDecoderByType("video/avc");
            if (s->codec) {
                AMediaFormat_delete(format);
                format = AMediaFormat_new();
                AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
                AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, s->width);
                AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, s->height);
                AMediaFormat_setInt32(format, "low-latency", 1);
                cStatus = AMediaCodec_configure(s->codec, format, nullptr, nullptr, 0);
                if (cStatus != AMEDIA_OK) {
                    // Level 3 fallback: barebones format for legacy / restrictive vendor drivers
                    XC_LOGW("hw_decoder_android: retry with low-latency failed (%d), retrying barebones", cStatus);
                    AMediaCodec_delete(s->codec);
                    s->codec = AMediaCodec_createDecoderByType("video/avc");
                    if (s->codec) {
                        AMediaFormat_delete(format);
                        format = AMediaFormat_new();
                        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
                        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, s->width);
                        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, s->height);
                        cStatus = AMediaCodec_configure(s->codec, format, nullptr, nullptr, 0);
                    }
                }
            }
        }
    }
    AMediaFormat_delete(format);
    if (cStatus != AMEDIA_OK || !s->codec) {
        XC_LOGE("hw_decoder_android: AMediaCodec_configure failed: %d", cStatus);
        if (s->codec) AMediaCodec_delete(s->codec);
        delete s;
        return false;
    }

    // 3. Start decoder
    media_status_t startStatus = AMediaCodec_start(s->codec);
    if (startStatus != AMEDIA_OK) {
        XC_LOGE("hw_decoder_android: AMediaCodec_start failed: %d", startStatus);
        AMediaCodec_delete(s->codec);
        delete s;
        return false;
    }

    decoder_ = s;
    XC_LOGI("hw_decoder_android: AMediaCodec ByteBuffer mode initialized (%dx%d, low-latency)",
            s->width, s->height);
    return true;
#else
    (void)maxWidth;
    (void)maxHeight;
    return false;
#endif
}

bool HwDecoder::decode(const uint8_t* data, size_t len, uint64_t pts, Picture& out) {
#if defined(__ANDROID__)
    if (!decoder_ || !data || len == 0) return false;
    auto* s = static_cast<AndroidCodecState*>(decoder_);
    const auto t0 = std::chrono::steady_clock::now();

    // Release previously held buffer to return it to the codec pool
    if (s->currentOutIdx >= 0) {
        AMediaCodec_releaseOutputBuffer(s->codec, static_cast<size_t>(s->currentOutIdx), false);
        s->currentOutIdx = -1;
    }

    // 1. Submit input packet (fast non-blocking poll first, fallback wait 1500 us)
    ssize_t inIdx = AMediaCodec_dequeueInputBuffer(s->codec, 0);
    if (inIdx < 0) inIdx = AMediaCodec_dequeueInputBuffer(s->codec, 1500);
    if (inIdx >= 0) {
        size_t bufSize = 0;
        uint8_t* inBuf = AMediaCodec_getInputBuffer(s->codec, static_cast<size_t>(inIdx), &bufSize);
        if (inBuf && bufSize >= len) {
            std::memcpy(inBuf, data, len);
            AMediaCodec_queueInputBuffer(s->codec, static_cast<size_t>(inIdx), 0, len, pts, 0);
        } else {
            AMediaCodec_queueInputBuffer(s->codec, static_cast<size_t>(inIdx), 0, 0, pts, 0);
        }
    } else {
        static uint32_t s_inDrops = 0;
        if (++s_inDrops % 60 == 1) {
            XC_LOGW("hw_decoder_android: dequeueInputBuffer busy/timed out (%u dropped)", s_inDrops);
        }
    }

    // 2. Drain output buffers in a loop to keep lowest latency.
    // First call waits up to 3.5ms for real-time 1-in-1-out hardware decode;
    // subsequent calls drain queued backlogs non-blockingly (waitUs = 0).
    ssize_t latestIdx = -1;
    AMediaCodecBufferInfo latestInfo{};
    int64_t waitUs = 3500;

    while (true) {
        AMediaCodecBufferInfo info{};
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(s->codec, &info, waitUs);
        waitUs = 0;
        if (outIdx >= 0) {
            if (latestIdx >= 0) {
                // Drop older queued frame immediately for lowest display latency
                AMediaCodec_releaseOutputBuffer(s->codec, static_cast<size_t>(latestIdx), false);
            }
            latestIdx = outIdx;
            latestInfo = info;
        } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* fmt = AMediaCodec_getOutputFormat(s->codec);
            if (fmt) {
                XC_LOGI("hw_decoder_android: output format changed: %s", AMediaFormat_toString(fmt));
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &s->width);
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &s->height);
                if (!AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_STRIDE, &s->stride) || s->stride <= 0) {
                    s->stride = s->width;
                }
                if (!AMediaFormat_getInt32(fmt, "slice-height", &s->sliceHeight) || s->sliceHeight <= 0) {
                    s->sliceHeight = s->height;
                }
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, &s->colorFormat);

                typedef bool (*PFN_AMediaFormat_getRect)(AMediaFormat*, const char*, int32_t*, int32_t*, int32_t*, int32_t*);
                static auto pfnGetRect = reinterpret_cast<PFN_AMediaFormat_getRect>(dlsym(RTLD_DEFAULT, "AMediaFormat_getRect"));
                int32_t cl = 0, ct = 0, cr = 0, cb = 0;
                if (pfnGetRect && pfnGetRect(fmt, "crop", &cl, &ct, &cr, &cb)) {
                    s->cropLeft = cl;
                    s->cropTop = ct;
                    s->cropRight = cr;
                    s->cropBottom = cb;
                } else {
                    AMediaFormat_getInt32(fmt, "crop-left", &s->cropLeft);
                    AMediaFormat_getInt32(fmt, "crop-top", &s->cropTop);
                    AMediaFormat_getInt32(fmt, "crop-right", &s->cropRight);
                    AMediaFormat_getInt32(fmt, "crop-bottom", &s->cropBottom);
                }

                void* imgData = nullptr;
                size_t imgSize = 0;
                if (AMediaFormat_getBuffer(fmt, "image-data", &imgData, &imgSize) && imgData && imgSize >= sizeof(AndroidMediaImage2)) {
                    const auto* m = reinterpret_cast<const AndroidMediaImage2*>(imgData);
                    if (m->numPlanes == 3 && m->plane[0].rowInc > 0) {
                        s->hasImageData = true;
                        s->imageInfo = *m;
                        XC_LOGI("hw_decoder_android: image-data MediaImage2 parsed: type=%u planes=%u %ux%u bitDepth=%u/%u; "
                                "Y[off=%u col=%d row=%d] U[off=%u col=%d row=%d] V[off=%u col=%d row=%d]",
                                m->type, m->numPlanes, m->width, m->height, m->bitDepth, m->bitDepthAllocated,
                                m->plane[0].offset, m->plane[0].colInc, m->plane[0].rowInc,
                                m->plane[1].offset, m->plane[1].colInc, m->plane[1].rowInc,
                                m->plane[2].offset, m->plane[2].colInc, m->plane[2].rowInc);
                    }
                }

                AMediaFormat_delete(fmt);
            }
        } else {
            break;
        }
    }

    if (latestIdx < 0) {
        out.pending = true;
        out.error = false;
        return false;
    }

    s->currentOutIdx = latestIdx;
    size_t bufSize = 0;
    uint8_t* buf = AMediaCodec_getOutputBuffer(s->codec, static_cast<size_t>(latestIdx), &bufSize);
    if (!buf || bufSize == 0) {
        XC_LOGE("hw_decoder_android: getOutputBuffer failed or size 0");
        AMediaCodec_releaseOutputBuffer(s->codec, static_cast<size_t>(latestIdx), false);
        s->currentOutIdx = -1;
        out.error = true;
        return false;
    }

    uint8_t* frameBase = buf + latestInfo.offset;
    int stride = s->stride > 0 ? s->stride : s->width;
    int sliceH = s->sliceHeight > 0 ? s->sliceHeight : s->height;
    int visW = (s->cropRight > s->cropLeft) ? (s->cropRight - s->cropLeft + 1) : s->width;
    int visH = (s->cropBottom > s->cropTop) ? (s->cropBottom - s->cropTop + 1) : s->height;
    visW = std::clamp(visW & ~1, 2, s->width);
    visH = std::clamp(visH & ~1, 2, s->height);

    out.width = visW;
    out.height = visH;
    out.visibleHeight = visH;
    out.pts = static_cast<uint64_t>(latestInfo.presentationTimeUs);
    out.error = false;
    out.pending = false;

    if (s->hasImageData) {
        const auto& p0 = s->imageInfo.plane[0];
        const auto& p1 = s->imageInfo.plane[1];
        const auto& p2 = s->imageInfo.plane[2];

        uint8_t* yBase = frameBase + p0.offset + s->cropTop * p0.rowInc + s->cropLeft * p0.colInc;
        out.data = yBase;
        out.pitchBytes = p0.rowInc;

        if (p1.colInc == 1) {
            // Planar (I420 / YV12)
            out.isYuv420p = true;
            out.nv21 = false;
            out.u = frameBase + p1.offset + (s->cropTop / 2) * p1.rowInc + (s->cropLeft / 2);
            out.v = frameBase + p2.offset + (s->cropTop / 2) * p2.rowInc + (s->cropLeft / 2);
            out.strideU = p1.rowInc;
            out.strideV = p2.rowInc;
            out.uv = nullptr;
            out.uvPitchBytes = 0;
        } else {
            // SemiPlanar (NV12 / NV21)
            out.isYuv420p = false;
            out.u = nullptr;
            out.v = nullptr;
            out.strideU = 0;
            out.strideV = 0;
            if (p2.offset < p1.offset) {
                // NV21: V precedes U
                out.uv = frameBase + p2.offset + (s->cropTop / 2) * p2.rowInc + (s->cropLeft & ~1);
                out.nv21 = true;
            } else {
                // NV12: U precedes V
                out.uv = frameBase + p1.offset + (s->cropTop / 2) * p1.rowInc + (s->cropLeft & ~1);
                out.nv21 = false;
            }
            out.uvPitchBytes = p1.rowInc;
        }
    } else {
        // Fallback without image-data
        uint8_t* yBase = frameBase + s->cropTop * stride + s->cropLeft;
        uint8_t* chromaBase = frameBase + stride * sliceH;
        out.data = yBase;
        out.pitchBytes = stride;

        if (s->colorFormat == 19) {
            // Planar I420
            out.isYuv420p = true;
            out.nv21 = false;
            int uStride = stride / 2;
            int vStride = stride / 2;
            out.u = chromaBase + (s->cropTop / 2) * uStride + (s->cropLeft / 2);
            out.v = chromaBase + uStride * (sliceH / 2) + (s->cropTop / 2) * vStride + (s->cropLeft / 2);
            out.strideU = uStride;
            out.strideV = vStride;
            out.uv = nullptr;
            out.uvPitchBytes = 0;
        } else {
            // SemiPlanar NV12
            out.isYuv420p = false;
            out.nv21 = false;
            out.u = nullptr;
            out.v = nullptr;
            out.strideU = 0;
            out.strideV = 0;
            out.uv = chromaBase + (s->cropTop / 2) * stride + (s->cropLeft & ~1);
            out.uvPitchBytes = stride;
        }
    }

    static bool s_firstLogged = false;
    if (!s_firstLogged) {
        s_firstLogged = true;
        XC_LOGI("hw_decoder_android: first frame: bufSize=%zu infoSize=%d stride=%d sliceH=%d visW=%d visH=%d colorFmt=%d hasImgData=%d isPlanar=%d",
                bufSize, latestInfo.size, stride, sliceH, visW, visH, s->colorFormat, s->hasImageData ? 1 : 0, out.isYuv420p ? 1 : 0);
    }

    timing_.decodeUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    return true;
#else
    (void)data;
    (void)len;
    (void)pts;
    (void)out;
    return false;
#endif
}

}  // namespace xc::media
