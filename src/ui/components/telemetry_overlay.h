// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Decoupled Telemetry Overlay component (2-column stats, sparkline, degradation pill).
#pragma once

#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/strings.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xc::ui {

struct StreamInfo;

class TelemetryOverlay {
public:
    enum StatsFlag : uint32_t {
        StatsFps       = 1 << 0,
        StatsBitrate   = 1 << 1,
        StatsLatency   = 1 << 2,
        StatsLoss      = 1 << 3,
        StatsGraph     = 1 << 4,
        StatsDelay     = 1 << 5,
        StatsQp        = 1 << 6,
        StatsMic       = 1 << 7,
        StatsDefaultMask = StatsFps | StatsBitrate | StatsLatency | StatsLoss | StatsGraph,
    };

    TelemetryOverlay();
    ~TelemetryOverlay() = default;

    void reset();
    void recordFrametime(float ms);
    void updateFrametime(float ms);

    uint32_t statsMask() const { return statsMask_; }
    void setStatsMask(uint32_t mask) { statsMask_ = mask; }

    const std::vector<float>& frametimes() const { return frametimes_; }
    void setFrametimes(const std::vector<float>& ft);

    // 1. Two-column connection stats block:
    // Column 1: Região, Latência, Taxa de bits, Quadros
    // Column 2: Perda de pacotes, Decodificação (HW/SW), Na tela após
    // Compact ~80-90px height replacing monolithic 160px vertical stack.
    void drawTwoColumnStats(Canvas& c, const Fonts& fonts, int x, int y, int w, int h,
                            const StreamInfo& info) const;

    // 2. Sparkline frametime chart (last 60 frames)
    void drawSparkline(Canvas& c, int x, int y, int w, int h) const;

    // 3. Degradation warning pill ("Conexão instável" / "Connection unstable")
    Canvas renderWarningPill(const Fonts& fonts) const;
    void drawWarningPill(Canvas& c, const Fonts& fonts, int x, int y, int w, int h) const;

    // 4. In-Game HUD overlay panel (floating overlay during active gameplay)
    Canvas renderHud(const Fonts& fonts, const StreamInfo& info, bool micMuted = false) const;

private:
    uint32_t statsMask_ = StatsDefaultMask;
    mutable std::vector<float> frametimes_;
    size_t maxFrametimes_ = 60;
};

}  // namespace xc::ui
