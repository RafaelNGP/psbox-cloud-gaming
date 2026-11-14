// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Icons drawn from SVG files kept inside the app (assets/icons, embedded at build time), in one
// colour like the glyphs of pad_icons.h: the shape's coverage, rasterised with nanosvg at the
// size asked and kept for that size. Holes in the shape (fill-rule evenodd) let the background
// show through.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/canvas.h"

namespace xc::ui {

enum class SvgIcon { Guitar };

// Draws `icon` `w` pixels wide (its height from its own aspect) with its top left at x, y, in
// `color`. False when the icon could not be read (nothing drawn).
bool drawSvgIcon(Canvas& c, SvgIcon icon, int x, int y, int w, Color color);

}  // namespace xc::ui
