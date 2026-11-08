// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/components/telemetry_overlay.h"
#include "ui/stream_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace xc::ui {

namespace {

template <typename... Args>
std::string fmt(const char* format, Args... args) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), format, args...);
    return std::string(buf);
}

constexpr Color kPanel = rgba(24, 24, 24);
constexpr Color kWhite = rgba(255, 255, 255);
constexpr Color kGray = rgba(170, 170, 170);
constexpr Color kWarnColor = rgba(255, 170, 0);

}  // namespace

TelemetryOverlay::TelemetryOverlay() {
    frametimes_.reserve(maxFrametimes_);
}

void TelemetryOverlay::reset() {
    frametimes_.clear();
}

void TelemetryOverlay::recordFrametime(float ms) {
    if (ms <= 0.0f) return;
    if (frametimes_.size() >= maxFrametimes_) {
        frametimes_.erase(frametimes_.begin());
    }
    frametimes_.push_back(ms);
}

void TelemetryOverlay::updateFrametime(float ms) {
    if (ms > 0.0f) {
        recordFrametime(ms);
    }
}

void TelemetryOverlay::setFrametimes(const std::vector<float>& ft) {
    frametimes_ = ft;
    if (frametimes_.size() > maxFrametimes_) {
        frametimes_.erase(frametimes_.begin(), frametimes_.end() - maxFrametimes_);
    }
}

void TelemetryOverlay::drawTwoColumnStats(Canvas& c, const Fonts& fonts, int x, int y, int w, int h,
                                         const StreamInfo& info) const {
    (void)h;
    const std::string none = "-";
    int colGap = 20;
    int colW = (w - colGap) / 2;
    int col1X = x;
    int col2X = x + colW + colGap;

    auto drawRow = [&](int rx, int ry, int rw, Str labelKey, const std::string& val) {
        const char* label = tr(labelKey);
        fonts.regular.draw(c, label, rx, ry, 16, kGray);
        int vw = fonts.semibold.measure(val, 16);
        fonts.semibold.draw(c, val, rx + rw - vw, ry, 16, kWhite);
    };

    // Column 1: Region, Latency, Bitrate, Framerate/Resolution
    int r1Y = y;
    drawRow(col1X, r1Y, colW, Str::StatRegion, info.region.empty() ? none : info.region);
    r1Y += 20;

    std::string latStr = info.rttMs > 0 ? (std::to_string(info.rttMs) + " ms") : none;
    drawRow(col1X, r1Y, colW, Str::StatLatency, latStr);
    r1Y += 20;

    std::string bitStr = info.mbps > 0 ? fmt("%.1f Mbps", info.mbps) : none;
    drawRow(col1X, r1Y, colW, Str::StatBitrate, bitStr);
    r1Y += 20;

    std::string fpsStr = info.fps > 0 ? fmt("%.0f fps", info.fps) : none;
    if (info.width > 0 && info.height > 0) {
        fpsStr = std::to_string(info.height) + "p  " + fpsStr;
    }
    drawRow(col1X, r1Y, colW, Str::StatFrameRate, fpsStr);

    // Subtle vertical separator between column 1 and column 2
    int sepX = col1X + colW + colGap / 2;
    c.line(sepX, y + 2, sepX, y + 78, 1.0f, rgba(255, 255, 255, 25));

    // Column 2: Loss, Decode (HW/SW), Screen Delay
    int r2Y = y;
    std::string lossStr = fmt("%.1f %%", info.lossPct);
    drawRow(col2X, r2Y, colW, Str::StatLoss, lossStr);
    r2Y += 20;

    std::string decStr = info.decodeMs > 0 ? (fmt("%.1f ms", info.decodeMs) + (info.isHwDecode ? " (HW)" : " (SW)")) : none;
    drawRow(col2X, r2Y, colW, Str::StatDecode, decStr);
    r2Y += 20;

    std::string scrStr = info.onScreenMs > 0 ? fmt("%.1f ms", info.onScreenMs) : none;
    drawRow(col2X, r2Y, colW, Str::StatOnScreen, scrStr);
}

void TelemetryOverlay::drawSparkline(Canvas& c, int x, int y, int w, int h) const {
    if (w <= 0 || h <= 0) return;

    // Baseline
    c.line(x, y + h, x + w, y + h, 1.0f, rgba(80, 80, 80, 160));

    if (frametimes_.empty()) return;

    const size_t count = frametimes_.size();
    const float barW = std::max(2.0f, static_cast<float>(w) / 60.0f - 1.0f);
    const float step = barW + 1.0f;
    const size_t maxBars = std::max<size_t>(1, static_cast<size_t>((static_cast<float>(w) + 1.0f) / step));
    const size_t start = count > maxBars ? (count - maxBars) : 0;

    float peakMs = 0.0f;
    for (size_t i = start; i < count; ++i) {
        if (frametimes_[i] > peakMs) peakMs = frametimes_[i];
    }
    const float scaleMax = std::clamp(peakMs * 1.25f, 4.0f, 40.0f);

    if (scaleMax >= 15.0f) {
        float targetLineY = (y + h) - (15.0f / scaleMax) * static_cast<float>(h);
        c.line(x, targetLineY, x + w, targetLineY, 1.0f, rgba(60, 160, 90, 120));
    } else {
        float midLineY = static_cast<float>(y) + static_cast<float>(h) * 0.5f;
        c.line(x, midLineY, x + w, midLineY, 1.0f, rgba(255, 255, 255, 25));
    }

    for (size_t i = start; i < count; ++i) {
        const float ms = frametimes_[i];
        const float norm = std::clamp(ms / scaleMax, 0.0f, 1.0f);
        const float bh = std::max(2.0f, norm * static_cast<float>(h));
        const float bx = static_cast<float>(x) + static_cast<float>(i - start) * step;
        if (bx + barW > static_cast<float>(x + w)) break;

        Color barColor = (ms <= 8.0f)  ? rgba(40, 210, 80)
                       : (ms <= 20.0f) ? rgba(255, 190, 40)
                                       : rgba(255, 60, 60);

        const int ibh = std::max(2, static_cast<int>(std::lround(bh)));
        c.fillRect({static_cast<int>(bx), y + h - ibh,
                    std::max(1, static_cast<int>(barW)), ibh}, barColor);
    }
}

Canvas TelemetryOverlay::renderWarningPill(const Fonts& fonts) const {
    const char* text = tr(Str::ConnectionUnstable);
    constexpr int kPx = 20, kH = 36, kDot = 6;
    int w = fonts.semibold.measure(text, kPx) + 28 + 2 * kDot + 10;
    Canvas c(w, kH);
    c.clear(kPanel);
    c.fillCircle(14.0f + kDot, kH / 2.0f, static_cast<float>(kDot), kWarnColor);
    fonts.semibold.draw(c, text, 14 + 2 * kDot + 10, fonts.semibold.centeredY(0, kH, kPx), kPx, kWhite);
    return c;
}

void TelemetryOverlay::drawWarningPill(Canvas& c, const Fonts& fonts, int x, int y, int w, int h) const {
    (void)w;
    (void)h;
    const char* text = tr(Str::ConnectionUnstable);
    constexpr int kPx = 20, pillH = 36, kDot = 6;
    int pillW = fonts.semibold.measure(text, kPx) + 28 + 2 * kDot + 10;
    c.fillRect({x, y, pillW, pillH}, kPanel, 12);
    c.fillCircle(static_cast<float>(x + 14 + kDot), static_cast<float>(y + pillH / 2), static_cast<float>(kDot), kWarnColor);
    fonts.semibold.draw(c, text, x + 14 + 2 * kDot + 10, fonts.semibold.centeredY(y, pillH, kPx), kPx, kWhite);
}

Canvas TelemetryOverlay::renderHud(const Fonts& fonts, const StreamInfo& info, bool micMuted) const {
    std::string text;
    if (statsMask_ & StatsFps) {
        if (info.height > 0) text += std::to_string(info.height) + "p  ";
        text += fmt("%.0f fps", info.fps);
    }
    if (statsMask_ & StatsBitrate) {
        if (!text.empty()) text += "  ";
        text += fmt("%.1f Mbps", info.mbps);
    }
    if (statsMask_ & StatsLatency) {
        if (!text.empty()) text += "  ";
        text += info.rttMs > 0 ? std::to_string(info.rttMs) + " ms" : "- ms";
    }
    if (statsMask_ & StatsLoss) {
        if (!text.empty()) text += "  ";
        text += fmt("%.1f%%", info.lossPct);
    }
    if ((statsMask_ & StatsQp) && info.qp >= 0) {
        if (!text.empty()) text += "  ";
        text += "QP " + fmt("%.0f", info.qp);
    }
    if (statsMask_ & StatsDelay) {
        if (info.onScreenMs > 0) {
            if (!text.empty()) text += "  ";
            text += "Screen " + fmt("%.1f ms", info.onScreenMs);
        }
        if (info.decodeMs > 0) {
            if (!text.empty()) text += "  ";
            text += "Dec " + fmt("%.1f ms", info.decodeMs) + (info.isHwDecode ? " HW" : " SW");
        }
    }
    if ((statsMask_ & StatsMic) && !micMuted) {
        if (!text.empty()) text += "  ";
        text += "MIC";
    }
    constexpr int kPx = 18;
    std::string warn = info.degraded ? std::string("  ") + tr(Str::ConnectionUnstable) : std::string();
    int tw = fonts.semibold.measure(text, kPx);
    int w = tw + fonts.semibold.measure(warn, kPx) + 28;
    w = std::max(w, 200);

    bool showGraph = (statsMask_ & StatsGraph) != 0;
    int kH = showGraph ? 62 : 36;
    Canvas c(w, kH);
    c.clear(kPanel);
    if (!text.empty()) fonts.semibold.draw(c, text, 14, 8, kPx, kWhite);
    if (!warn.empty()) fonts.semibold.draw(c, warn, 14 + tw, 8, kPx, kWarnColor);

    if (showGraph) {
        drawSparkline(c, 14, 32, w - 28, 22);
    }
    return c;
}

}  // namespace xc::ui
