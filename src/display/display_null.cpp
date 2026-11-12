// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No screen (a headless build): init() succeeds so the app runs as usual,
// pictures go nowhere and present() waits about a 60 Hz refresh, so a loop
// paced by it doesn't spin.
#include <cstddef>
#include <cstdint>
#include "display/display.h"

#include "platform/platform.h"

namespace xc::display {

std::recursive_mutex& frameMutex() {
    static std::recursive_mutex m;
    return m;
}
bool init(bool) { return true; }
void shutdown() {}
void present() { platform::sleepMs(16); }
bool drawYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v, int strideY, int strideU, int strideV, int width, int height, bool) {
    if (!y || !u || !v || width <= 0 || height <= 0 || strideY < width) return false;
    return true;
}
bool videoSlots(size_t, uint8_t* out[2]) {
    out[0] = out[1] = nullptr;
    return false;  // no hardware decoder writes into the display here
}
void waitVideoSlot(const uint8_t*) {}

bool drawNv12(const uint8_t* y, const uint8_t* uv, int strideY, int strideUV, int width, int height, bool) {
    if (!y || !uv || width <= 0 || height <= 0 || strideY < width || strideUV < width) return false;
    return true;
}
void drawRgba(const uint32_t*) {}
void pump() {}
void idleWait(uint32_t ms) { platform::sleepMs(ms); }
void setOverlay(const uint32_t*, int, int, int, int, uint8_t) {}
void setSharpness(int) {}
void setDeband(int) {}
void setUpscaler(int) {}
void setRestore(bool) {}
void setLegacyPrecision(bool) {}
DisplayCaps caps() { return {}; }
void setSwapImages(int) {}
void setLowLatencyPresent(bool) {}
void setPresentWait(bool) {}
bool setHighRefresh(bool) { return false; }
bool canSwitchRefresh() { return false; }
bool bringUpTest(int) { return false; }
uint64_t lastPresentId() { return 0; }
bool lastShown(uint64_t&, uint64_t&) { return false; }
bool readBackRgb(std::vector<uint8_t>&, int&, int&) { return false; }

}  // namespace xc::display
