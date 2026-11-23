// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/settings.h"
#include "ui/strings.h"
#include <string>

namespace xc::ui {
inline constexpr Str sessionLabels[] = {Str::AutoReconnect, Str::ShowSessionClock, Str::ClockTextSize,
                                       Str::ClockPlacement, Str::ClockTransparency, Str::ClockDigitColor, Str::ClockStyle, Str::BasicFillLabel, Str::BasicAlphaLabel, Str::PeekLabel};
inline std::string sessionChoiceLabel(int field, int value) {
    if (value < 0) return tr(Str::InheritSession);
    constexpr Str sizes[] = {Str::ClockSmall, Str::ClockMedium, Str::ClockLarge};
    constexpr Str positions[] = {Str::TopLeft, Str::TopCenter, Str::TopRight, Str::BottomLeft, Str::BottomCenter, Str::BottomRight};
    if (field == app::ShowClock) {
        constexpr Str modes[] = {Str::Deactivated, Str::ClockLastFive, Str::ClockWholeSession};
        return tr(modes[value]);
    }
    if (field == app::ClockSkin) {
        constexpr Str skins[] = {Str::SkinClassic, Str::SkinCrystal, Str::SkinScore, Str::SkinMinimal, Str::SkinAurora, Str::SkinRadar,
                                 Str::SkinEmber, Str::SkinOcean, Str::SkinOrbit, Str::SkinSakura};
        return tr(skins[value]);
    }
    if (field == app::BasicFill) {
        constexpr Str fills[] = {Str::FillCarbon, Str::FillNavy, Str::FillPlum, Str::FillForest, Str::FillBurgundy};
        return tr(fills[value]);
    }
    if (field == app::BasicAlpha) return std::to_string(25 * value) + "%";
    if (field == app::ClockColor) {
        constexpr Str colors[] = {Str::ClockWhite, Str::ClockYellow, Str::ClockGreen, Str::ClockCyan,
                                  Str::ClockBlue, Str::ClockOrange, Str::ClockPink, Str::ClockRed};
        return tr(colors[value]);
    }
    if (field == app::ClockSize) return tr(sizes[value]);
    if (field == app::ClockPosition) return tr(positions[value]);
    if (field == app::ClockOpacity) return std::to_string(25 * (value + 1)) + "%";
    return tr(value ? Str::Activated : Str::Deactivated);
}
inline void cycleOverride(app::SessionChoices& choices, int field, int direction) {
    const int count = app::sessionChoiceCounts[field] + 1;
    choices[field] = (choices[field] + 1 + direction + count) % count - 1;
}
} // namespace xc::ui
