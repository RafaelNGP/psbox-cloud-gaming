// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// H.264 on the PS5's hardware video decoder (libSceVideodec2, the system's
// own: RADV offers no Vulkan Video on the console), in hw_decoder_ps5.cpp.
// Setup after BlackBearReloaded's ProsperoLight (GPL-3.0-or-later). Other
// builds link hw_decoder_null.cpp, whose init() fails.
#pragma once

#include <cstddef>
#include <cstdint>

namespace xc::media {

class HwDecoder {
public:
    // A decoded picture, as the decoder left it in its frame buffer.
    struct Picture {
        const uint8_t* data = nullptr;
        size_t size = 0;
        uint32_t width = 0, height = 0, pitch = 0, pitchBytes = 0, format = 0;
        uint64_t pts = 0;
        bool error = false;
        bool pending = false;
    };

    ~HwDecoder();
    // Loads the module and makes a decoder for pictures up to the size.
    bool init(int maxWidth, int maxHeight);
    bool ready() const { return decoder_ != nullptr; }
    // One access unit in; true with `out` when a picture came out.
    bool decode(const uint8_t* data, size_t len, uint64_t pts, Picture& out);

    // The last decode() call's time: the decode call itself, and the flush
    // that follows it when the picture was not out yet (measurements only).
    struct Timing {
        uint64_t decodeUs = 0, flushUs = 0;
        bool flushed = false;
        int64_t decodeCpuUs = -1;  // this thread's CPU time in the decode call (-1: unknown)
    };
    const Timing& lastTiming() const { return timing_; }

    // The PS5 decoder's setup, for measurements (autoplay "bench", hwcfg.req):
    // read by the next init(). The defaults are the shipped setup.
    struct Tuning {
        int pipe = -1;  // -1: the first free of pipes 1, 2, 3, 4, 0
        int queue = 0;
        int cpuPriority = 700;
        uint64_t cpuAffinity = 0;
        int optimizeProgressive = 1;
        int resourceType = 1;
    };
    static Tuning& tuning() {
        static Tuning t;
        return t;
    }

    // Where the pictures go (PS5): two buffers used in turn (the display's,
    // which takes them to the GPU without a CPU copy) and what is called
    // before one is written (the GPU done reading it). Unset or off: the
    // decoder's own buffer, copied by the display.
    struct Output {
        uint8_t* buffers[2] = {nullptr, nullptr};
        size_t size = 0;
        void (*beforeWrite)(const uint8_t*) = nullptr;
        bool enabled = true;  // measurements (hwcfg.req "zc=0") turn it off
    };
    static Output& output() {
        static Output o;
        return o;
    }

private:
    Timing timing_{};
    [[maybe_unused]] bool ownsPool_ = false;   // the memory kept between decoders is this one's
    [[maybe_unused]] uint32_t frameAlignment_ = 0;
    [[maybe_unused]] int nextOutput_ = 0;
    void* decoder_ = nullptr;
    // The PS5 backend's; the null one never sets them.
    [[maybe_unused]] void* computeQueue_ = nullptr;
    [[maybe_unused]] uint8_t* frameBuffer_ = nullptr;
    [[maybe_unused]] size_t frameBufferSize_ = 0;
};

}  // namespace xc::media
