// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "media/hw_decoder.h"

#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

extern "C" {
int sceSysmoduleLoadModule(uint32_t id);
size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t searchStart, int64_t searchEnd, size_t length, size_t alignment,
                                  int memoryType, int64_t* physicalAddress);
int sceKernelMapDirectMemory(void** address, size_t length, int protection, int flags, int64_t physicalAddress,
                             size_t alignment);
int sceKernelMapNamedFlexibleMemory(void** address, size_t length, int protection, int flags, const char* name);

// libSceVideodec2 as the PS5 lays it out (not the PS4's: 32-bit flags, no
// extra-config pointer), after BlackBearReloaded's ProsperoLight (GPL-3.0)
// and its ps5-hardware-video-decoding-research.
struct Vdec2DecoderConfig {
    uint64_t size;
    uint32_t resourceType, codecType, profile, maxLevel;
    int32_t maxWidth, maxHeight, maxDpbFrames;
    uint32_t pipelineDepth;
    uint64_t computeQueue, cpuAffinity;
    int32_t cpuPriority;
    uint32_t optimizeProgressive, checkMemoryType, reserved;
};
struct Vdec2DecoderMemory {
    uint64_t size, cpuSize;
    void* cpu;
    uint64_t gpuSize;
    void* gpu;
    uint64_t cpuGpuSize;
    void* cpuGpu;
    uint64_t maxFrameSize;
    uint32_t frameAlignment, reserved;
};
struct Vdec2ComputeConfig {
    uint64_t size;
    uint16_t pipeId, queueId;
    uint8_t checkMemoryType, reserved0;
    uint16_t reserved1;
};
struct Vdec2ComputeMemory {
    uint64_t size, cpuGpuSize;
    void* cpuGpu;
};
struct Vdec2Input {
    uint64_t size;
    void* au;
    uint64_t auSize, pts, dts, attached;
};
struct Vdec2Frame {
    uint64_t size;
    void* buffer;
    uint64_t bufferSize;
    uint32_t accepted, reserved;
};
struct Vdec2Output {
    uint64_t size;
    uint8_t valid, error, pictureCount, padding;
    uint32_t codec, width, pitch, height, reserved;
    void* buffer;
    uint64_t bufferSize;
    uint32_t frameFormat, pitchBytes;
};

int sceVideodec2QueryComputeMemoryInfo(Vdec2ComputeMemory* memory);
int sceVideodec2AllocateComputeQueue(const Vdec2ComputeConfig* config, const Vdec2ComputeMemory* memory,
                                     void** queue);
int sceVideodec2ReleaseComputeQueue(void* queue);
int sceVideodec2QueryDecoderMemoryInfo(const Vdec2DecoderConfig* config, Vdec2DecoderMemory* memory);
int sceVideodec2CreateDecoder(const Vdec2DecoderConfig* config, const Vdec2DecoderMemory* memory, void** decoder);
int sceVideodec2DeleteDecoder(void* decoder);
int sceVideodec2Decode(void* decoder, Vdec2Input* input, Vdec2Frame* frame, Vdec2Output* output);
int sceVideodec2Flush(void* decoder, Vdec2Frame* frame, Vdec2Output* output);
}

namespace xc::media {

namespace {

constexpr uint32_t kVideodecModule = 207;
constexpr int kDirectType = 12;  // the PS5's decoder memory type
constexpr int kCpuGpuRw = 0x33, kGpuRwCpuWrite = 0x32;

size_t align16k(size_t n) { return (n + 0x3FFF) & ~static_cast<size_t>(0x3FFF); }

void* directMemory(size_t size, int protection) {
    int64_t start = 0;
    int rc = sceKernelAllocateDirectMemory(0, static_cast<int64_t>(sceKernelGetDirectMemorySize()), size, 0x4000,
                                           kDirectType, &start);
    void* addr = nullptr;
    if (rc == 0) rc = sceKernelMapDirectMemory(&addr, size, protection, 0, start, 0x4000);
    if (rc != 0) {
        XC_LOGE("hwdec: direct memory %zu: 0x%08x", size, static_cast<unsigned>(rc));
        return nullptr;
    }
    return addr;
}

}  // namespace

HwDecoder::~HwDecoder() {
    // The memory stays mapped: one decoder for the app's life is the plan.
    if (decoder_) sceVideodec2DeleteDecoder(decoder_);
    if (computeQueue_) sceVideodec2ReleaseComputeQueue(computeQueue_);
}

bool HwDecoder::init(int maxWidth, int maxHeight) {
    if (decoder_) return true;
    int rc = sceSysmoduleLoadModule(kVideodecModule);
    XC_LOGI("hwdec: sysmodule %u -> 0x%08x", kVideodecModule, static_cast<unsigned>(rc));
    if (rc != 0 && rc != static_cast<int>(0x80020063)) return false;

    // A GPU compute queue for the decoder's own work.
    Vdec2ComputeMemory compute{};
    compute.size = sizeof compute;
    rc = sceVideodec2QueryComputeMemoryInfo(&compute);
    size_t computeSize = align16k(compute.cpuGpuSize);
    XC_LOGI("hwdec: compute memory 0x%08x: %zu bytes", static_cast<unsigned>(rc), computeSize);
    if (rc != 0 || computeSize == 0) {
        // Prospero native compute memory size: 0x498000 bytes
        computeSize = 0x498000;
        XC_LOGI("hwdec: using Prospero compute size fallback %zu bytes", computeSize);
    }
    compute.cpuGpu = directMemory(computeSize, kCpuGpuRw);
    compute.cpuGpuSize = computeSize;
    if (!compute.cpuGpu) return false;

    void* queue = nullptr;
    const uint16_t pipeOrder[] = {1, 2, 3, 4, 0};
    for (uint16_t pipe : pipeOrder) {
        Vdec2ComputeConfig computeConfig{};
        computeConfig.size = sizeof computeConfig;
        computeConfig.pipeId = pipe;
        rc = sceVideodec2AllocateComputeQueue(&computeConfig, &compute, &queue);
        XC_LOGI("hwdec: compute queue pipe %u -> 0x%08x (queue=%p)", pipe, static_cast<unsigned>(rc), queue);
        if (rc == 0 && queue) {
            computeQueue_ = queue;
            break;
        }
    }
    if (!computeQueue_) return false;

    // H.264 High up to level 4.2 (1080p60 stream), DPB depth 2 for ultra-low latency; fallback to 5.1 / 4.
    struct Preset {
        uint32_t profile;
        uint32_t maxLevel;
        int32_t dpbFrames;
    };
    const Preset presets[] = {
        {100, 42, 2},
        {100, 51, 4},
    };
    Vdec2DecoderConfig config{};
    Vdec2DecoderMemory memory{};
    for (const auto& p : presets) {
        config = Vdec2DecoderConfig{};
        config.size = sizeof config;
        config.resourceType = 1;
        config.codecType = 1;
        config.profile = p.profile;
        config.maxLevel = p.maxLevel;
        config.maxWidth = maxWidth;
        config.maxHeight = maxHeight;
        config.maxDpbFrames = p.dpbFrames;
        config.pipelineDepth = 1;
        config.computeQueue = reinterpret_cast<uint64_t>(computeQueue_);
        config.cpuPriority = 700;  // Standard Sony thread priority (SCE_KERNEL_PRIO_FIFO_DEFAULT)
        config.cpuAffinity = 0;    // Any core (unconstrained)
        config.optimizeProgressive = 1;
        memory = Vdec2DecoderMemory{};
        memory.size = sizeof memory;
        rc = sceVideodec2QueryDecoderMemoryInfo(&config, &memory);
        XC_LOGI("hwdec: query profile=%u level=%u dpb=%d -> 0x%08x: cpu %llu, gpu %llu, cpu+gpu %llu, frame %llu",
                p.profile, p.maxLevel, p.dpbFrames, static_cast<unsigned>(rc),
                static_cast<unsigned long long>(memory.cpuSize),
                static_cast<unsigned long long>(memory.gpuSize), static_cast<unsigned long long>(memory.cpuGpuSize),
                static_cast<unsigned long long>(memory.maxFrameSize));
        if (rc == 0) break;
    }
    if (rc != 0) return false;
    size_t cpuSize = align16k(memory.cpuSize);
    rc = sceKernelMapNamedFlexibleMemory(&memory.cpu, cpuSize, 0x03, 0, "PSBoxVdecCpu");
    if (rc != 0) {
        XC_LOGE("hwdec: flexible memory %zu: 0x%08x", cpuSize, static_cast<unsigned>(rc));
        return false;
    }
    memory.gpuSize = align16k(memory.gpuSize);
    memory.gpu = directMemory(memory.gpuSize, kGpuRwCpuWrite);
    if (memory.cpuGpuSize) {
        memory.cpuGpuSize = align16k(memory.cpuGpuSize);
        memory.cpuGpu = directMemory(memory.cpuGpuSize, kCpuGpuRw);
    }
    // The frame the decoder writes; CPU-readable while the picture is
    // copied from it.
    frameBufferSize_ = align16k(memory.maxFrameSize);
    frameBuffer_ = static_cast<uint8_t*>(directMemory(frameBufferSize_, kCpuGpuRw));
    if (!memory.gpu || (memory.cpuGpuSize && !memory.cpuGpu) || !frameBuffer_) return false;
    rc = sceVideodec2CreateDecoder(&config, &memory, &decoder_);
    XC_LOGI("hwdec: create decoder 0x%08x", static_cast<unsigned>(rc));
    if (rc != 0) decoder_ = nullptr;
    return decoder_ != nullptr;
}

bool HwDecoder::decode(const uint8_t* data, size_t len, uint64_t pts, Picture& out) {
    if (!decoder_) return false;
    Vdec2Input in{sizeof(Vdec2Input), const_cast<uint8_t*>(data), len, pts, pts, 0};
    Vdec2Frame frame{sizeof(Vdec2Frame), frameBuffer_, frameBufferSize_, 0, 0};
    Vdec2Output info{};
    info.size = sizeof info;
    uint64_t t0 = platform::nowUs();
    int rc = sceVideodec2Decode(decoder_, &in, &frame, &info);
    uint64_t tDecode = platform::nowUs() - t0;
    bool flushed = false;
    uint64_t tFlush = 0;
    // Depth 1: a picture not out yet is flushed out.
    if (rc == 0 && !info.valid) {
        flushed = true;
        uint64_t tf0 = platform::nowUs();
        info = Vdec2Output{};
        info.size = sizeof info;
        rc = sceVideodec2Flush(decoder_, &frame, &info);
        tFlush = platform::nowUs() - tf0;
    }
    static int logged = 0;
    if (logged < 15) {
        ++logged;
        XC_LOGI("hwdec: decode 0x%08x: valid %u error %u (decode %llu us, flush %llu us, flushed=%d), %ux%u pitch %u (%u bytes), "
                "%llu bytes at %s",
                static_cast<unsigned>(rc), info.valid, info.error,
                static_cast<unsigned long long>(tDecode), static_cast<unsigned long long>(tFlush), flushed ? 1 : 0,
                info.width, info.height, info.pitch, info.pitchBytes,
                static_cast<unsigned long long>(info.bufferSize),
                info.buffer == frameBuffer_ ? "our buffer" : "another address");
    }
    if (rc != 0 || !info.valid) return false;
    out.data = static_cast<const uint8_t*>(info.buffer);
    out.size = info.bufferSize;
    out.width = info.width;
    out.height = info.height;
    out.pitch = info.pitch;
    out.pitchBytes = info.pitchBytes;
    out.format = info.frameFormat;
    out.error = info.error != 0;
    out.pts = pts;
    return true;
}

}  // namespace xc::media
