// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Reusable SmartGlass & Web Control QR card component.
#pragma once

#include "ui/canvas.h"
#include "ui/font.h"
#include "ui/strings.h"

#include <string>

namespace xc::ui {

class WebControlCard {
public:
    struct Options {
        int x = 0;
        int y = 0;
        int w = 540;
        int h = 640;
        std::string url;        // Explicit URL; if empty, uses platform::localIPv4():8080 + path
        std::string path = "";  // Subpath for default URL (empty for root SmartGlass landing page)
        Color bgColor = rgba(20, 24, 34, 245);
        Color borderColor = rgba(16, 124, 65, 200);
        bool showBorder = true;
        bool showPill = true;
        int cornerRadius = 20;
    };

    WebControlCard() = default;
    explicit WebControlCard(const Options& opts) : opts_(opts) {}

    void setOptions(const Options& opts) { opts_ = opts; }
    const Options& options() const { return opts_; }

    // Renders the card into canvas c
    void draw(Canvas& c, const Fonts& fonts) const;
    void draw(Canvas& c, const Fonts& fonts, const Options& opts) const;

    // Static convenience helper
    static void drawCard(Canvas& c, const Fonts& fonts, int x, int y, int w, int h,
                         const std::string& customUrl = "", const std::string& path = "");

    // Static URL resolver
    static std::string resolveUrl(const std::string& customUrl, const std::string& path = "");

private:
    Options opts_;
};

}  // namespace xc::ui
