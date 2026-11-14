// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/pad_icons.h"
#include "ui/svg_icon.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace xc::ui {

Color playerColor(int index) {
    switch (index) {
    case 0: return rgba(0, 112, 220);
    case 1: return rgba(220, 50, 50);
    case 2: return rgba(40, 170, 80);
    default: return rgba(225, 90, 175);
    }
}

void drawKeyboardGlyph(Canvas& c, int x, int y, int w, Color color) {
    int h = w * 10 / 24, t = std::max(2, w / 14), r = std::max(2, w / 10);
    c.strokeRect({x, y, w, h}, color, t, r);
    int key = std::max(2, w / 12), gap = (w - 2 * t - 2 * key) / 6;
    for (int row = 0; row < 2; ++row)  // two rows of six keys
        for (int k = 0; k < 6; ++k)
            c.fillRect({x + t + key + k * gap, y + t + key / 2 + row * (key + key / 2 + 1), std::max(1, gap - key / 2), key},
                       color, 1);
    c.fillRect({x + w / 4, y + h - t - key - key / 2, w / 2, key}, color, 1);  // the space bar
}

void drawPhoneGlyph(Canvas& c, int x, int y, int w, Color color, Color background) {
    int ph = w * 7 / 10;
    int pw = std::max(12, w * 11 / 24);
    int px = x + (w - pw) / 2;
    int py = y;
    int r = std::max(3, pw / 5);
    c.fillRect({px, py, pw, ph}, color, r);
    int border = std::max(2, w / 20);
    int topBar = std::max(3, ph / 7);
    int botBar = std::max(4, ph / 6);
    c.fillRect({px + border, py + topBar, std::max(2, pw - 2 * border), std::max(2, ph - topBar - botBar)},
               background, std::max(1, border / 2));
    c.fillRect({px + pw / 3, py + std::max(1, topBar / 3), std::max(2, pw / 3), std::max(1, topBar / 3)}, background, 1);
    c.fillCircle(px + pw / 2.0f, py + ph - botBar / 2.0f, std::max(1.5f, pw / 10.0f), background);
}

void drawGuitarGlyph(Canvas& c, int x, int y, int w, Color color, Color background) {
    // assets/icons/guitar.svg (its box is w by 0.7 w, as this glyph's); drawn by hand below only
    // when it can't be read.
    if (drawSvgIcon(c, SvgIcon::Guitar, x, y, w, color)) return;
    const float W = static_cast<float>(w), H = W * 0.7f, X = static_cast<float>(x), Y = static_cast<float>(y);
    // The neck, from the body up to the right, and its head.
    c.line(X + W * 0.40f, Y + H * 0.62f, X + W * 0.80f, Y + H * 0.12f, std::max(2.0f, W * 0.07f), color);
    c.fillRect({static_cast<int>(X + W * 0.74f), static_cast<int>(Y), static_cast<int>(W * 0.14f), static_cast<int>(H * 0.20f)},
               color, std::max(1, w / 30));
    // The body: two lobes, the sound hole and the bridge cut out.
    c.fillCircle(X + W * 0.30f, Y + H * 0.72f, W * 0.20f, color);
    c.fillCircle(X + W * 0.42f, Y + H * 0.52f, W * 0.14f, color);
    c.fillCircle(X + W * 0.36f, Y + H * 0.62f, W * 0.055f, background);
    c.line(X + W * 0.20f, Y + H * 0.86f, X + W * 0.30f, Y + H * 0.94f, std::max(1.5f, W * 0.035f), background);
}

void drawDrumsGlyph(Canvas& c, int x, int y, int w, Color color, Color background) {
    const float W = static_cast<float>(w), H = W * 0.7f, X = static_cast<float>(x), Y = static_cast<float>(y);
    const float t = std::max(2.0f, W * 0.055f);
    // The sticks, crossed above the drum.
    c.line(X + W * 0.20f, Y + H * 0.02f, X + W * 0.66f, Y + H * 0.44f, t, color);
    c.line(X + W * 0.80f, Y + H * 0.02f, X + W * 0.34f, Y + H * 0.44f, t, color);
    // The drum: its shell and, on top, its head (a filled oval, its rim cut out).
    const float cx = X + W * 0.5f, cy = Y + H * 0.50f, a = W * 0.36f, b = H * 0.13f;
    c.fillRect({static_cast<int>(cx - a), static_cast<int>(cy), static_cast<int>(2 * a), static_cast<int>(H * 0.46f)}, color,
               std::max(2, w / 16));
    for (float dy = -b; dy <= b; dy += 0.5f) {
        const float hw = a * std::sqrt(std::max(0.0f, 1.0f - (dy / b) * (dy / b)));
        c.line(cx - hw, cy + dy, cx + hw, cy + dy, 1.0f, color);
    }
    const float edge = std::max(1.0f, W * 0.03f);
    float px = cx + a * 0.86f, py = cy;
    for (int k = 1; k <= 36; ++k) {  // the rim, an oval just inside the head
        const float ang = 6.2831853f * static_cast<float>(k) / 36.0f;
        const float nx = cx + a * 0.86f * std::cos(ang), ny = cy + b * 0.70f * std::sin(ang);
        c.line(px, py, nx, ny, edge, background);
        px = nx, py = ny;
    }
    // The tension rods, a zigzag around the shell.
    const float top = cy + b + H * 0.05f, bottom = Y + H * 0.88f;
    for (int k = 0; k < 4; ++k) {
        const float x0 = cx - a + 2 * a * (static_cast<float>(k) + 0.25f) / 4.0f, x1 = x0 + 2 * a / 8.0f;
        c.line(x0, top, x1, bottom, edge, background);
        if (k < 3) c.line(x1, bottom, x0 + 2 * a / 4.0f, top, edge, background);
    }
}

int padIconHeight(int w) { return w * 7 / 10 + 4 + w * 3 / 10; }

void drawPadIcon(Canvas& c, const Font& font, int x, int y, int w, int index, bool connected, Color background) {
    const float W = static_cast<float>(w), H = W * 0.7f, X = static_cast<float>(x), Y = static_cast<float>(y);
    auto px = [&](float f) { return X + f * W; };
    auto py = [&](float f) { return Y + f * H; };
    // Opaque even when dim: overlapping translucent parts would show seams.
    Color body = connected ? playerColor(index) : rgba(70, 70, 70);
    // A DualSense from the front: the body, its two grips, and the controls
    // cut out of it in the background's colour.
    c.fillRect({static_cast<int>(px(0.05f)), static_cast<int>(py(0.04f)), static_cast<int>(W * 0.90f),
                static_cast<int>(H * 0.56f)},
               body, static_cast<int>(H * 0.24f));
    c.fillRect({static_cast<int>(px(0.24f)), static_cast<int>(py(0.36f)), static_cast<int>(W * 0.52f),
                static_cast<int>(H * 0.36f)},
               body, static_cast<int>(H * 0.14f));
    for (int side = 0; side < 2; ++side) {
        float x0 = side ? 0.74f : 0.26f, x1 = side ? 0.86f : 0.14f;
        for (int k = 0; k <= 10; ++k) {  // a capsule, out and down
            float t = k / 10.0f;
            c.fillCircle(px(x0 + (x1 - x0) * t), py(0.42f + 0.40f * t), W * (0.13f - 0.015f * t), body);
        }
    }
    c.fillRect({static_cast<int>(px(0.355f)), static_cast<int>(py(0.12f)), static_cast<int>(W * 0.29f),
                static_cast<int>(H * 0.22f)},
               background, static_cast<int>(W * 0.035f));  // touchpad
    float dx = px(0.19f), dy = py(0.30f), arm = W * 0.075f, bar = W * 0.045f;  // D-pad
    c.fillRect({static_cast<int>(dx - arm), static_cast<int>(dy - bar / 2), static_cast<int>(2 * arm), static_cast<int>(bar)},
               background, 1);
    c.fillRect({static_cast<int>(dx - bar / 2), static_cast<int>(dy - arm), static_cast<int>(bar), static_cast<int>(2 * arm)},
               background, 1);
    float bx = px(0.81f), by = py(0.30f), off = W * 0.06f;  // the four buttons
    for (auto [ox, oy] : {std::pair{0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}})
        c.fillCircle(bx + ox * off, by + oy * off, W * 0.027f, background);
    for (float sx : {0.37f, 0.63f}) {  // the sticks
        c.fillCircle(px(sx), py(0.58f), W * 0.085f, background);
        c.fillCircle(px(sx), py(0.58f), W * 0.05f, body);
    }
    c.fillCircle(px(0.5f), py(0.58f), W * 0.02f, background);  // PS button

    std::string n = std::to_string(index + 1);
    int npx = w * 3 / 10;
    font.draw(c, n, x + (w - font.measure(n, npx)) / 2, y + static_cast<int>(H) + 4, npx,
              connected ? rgba(255, 255, 255) : rgba(110, 110, 110));
}

void drawSlotIcon(Canvas& c, const Font& font, int x, int y, int w, int index, const PadSlot& slot, Color background,
                  const Font* icons) {
    const bool isKb = slot.keyboard || slot.device == PadDevice::Keyboard;
    const bool isPhone = slot.device == PadDevice::Phone;
    const bool isInstrument = slot.device == PadDevice::Guitar || slot.device == PadDevice::Drums;
    if (isInstrument) {
        Color col = slot.connected ? playerColor(index) : rgba(70, 70, 70);
        if (slot.device == PadDevice::Guitar) drawGuitarGlyph(c, x, y, w, col, background);
        else drawDrumsGlyph(c, x, y, w, col, background);
        std::string n = std::to_string(index + 1);
        int npx = w * 3 / 10;
        font.draw(c, n, x + (w - font.measure(n, npx)) / 2, y + w * 7 / 10 + 4, npx,
                  slot.connected ? rgba(255, 255, 255) : rgba(110, 110, 110));
        return;
    }
    if (!isKb && !isPhone && !icons) {
        drawPadIcon(c, font, x, y, w, index, slot.connected, background);
        return;
    }
    // The console's own controller, keyboard-and-mouse, or smartphone icon,
    // in the player's colour (dim when free), numbered below.
    bool on = slot.connected || isKb || isPhone;
    Color col = on ? playerColor(index) : rgba(70, 70, 70);
    if (isPhone) {
        drawPhoneGlyph(c, x, y, w, col, background);
    } else if (icons) {
        const char* glyph = isKb ? kIconKeyboardMouse : kIconGamepad;
        int px2 = w * 9 / 10;  // as wide as a pad, centred on its height
        icons->draw(c, glyph, x + (w - icons->measure(glyph, px2)) / 2, y - w / 10, px2, col);
    } else {
        int kh = w * 10 / 24;
        int top = y + (w * 7 / 10 - kh) / 2;
        drawKeyboardGlyph(c, x, top, w * 3 / 4, col);
        c.fillRect({x + w * 3 / 4 + w / 16, top + kh / 6, w / 6, kh * 5 / 6}, col, w / 12);  // the mouse
    }
    std::string n = std::to_string(index + 1);
    int npx = w * 3 / 10;
    font.draw(c, n, x + (w - font.measure(n, npx)) / 2, y + w * 7 / 10 + 4, npx,
              on ? rgba(255, 255, 255) : rgba(110, 110, 110));
}

int drawPadRow(Canvas& c, const Font& font, int x, int y, int w, int gap, const PadSlots& pads, Color background,
               const Font* icons) {
    for (int i = 0; i < static_cast<int>(pads.size()); ++i)
        drawSlotIcon(c, font, x + i * (w + gap), y, w, i, pads[i], background, icons);
    return static_cast<int>(pads.size()) * (w + gap) - gap;
}

}  // namespace xc::ui
