// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// PSBox Cloud Gaming is an independent open-source project. This fork changes
// the product branding; original project and third-party rights remain intact.
#pragma once

#include "ui/canvas.h"
#include "ui/font.h"

namespace xc::ui {

// Aurora Stream: original teal-to-violet palette and a distinct orbital mark.
constexpr Color kBrandGreen = rgba(54, 220, 196);
constexpr Color kBrandDeep = rgba(7, 13, 30);
constexpr Color kBrandViolet = rgba(157, 91, 255);

void drawBrandMark(Canvas& c, float cx, float cy, float r, Color color = rgba(225, 255, 250));
void drawAppIcon(Canvas& c, const Fonts& fonts);
void drawHomeArt(Canvas& c, const Fonts& fonts, bool launch);

}  // namespace xc::ui
