// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "ui/components/web_control_card.h"
#include "platform/platform.h"
#include "ui/app_ui_internal.h"

#include <algorithm>

namespace xc::ui {

std::string WebControlCard::resolveUrl(const std::string& customUrl, const std::string& path) {
    if (!customUrl.empty()) return customUrl;
    std::string ip = platform::localIPv4();
    if (ip.empty() || ip == "0.0.0.0") ip = "127.0.0.1";
    return "http://" + ip + ":8080" + path;
}

void WebControlCard::draw(Canvas& c, const Fonts& fonts) const {
    draw(c, fonts, opts_);
}

void WebControlCard::draw(Canvas& c, const Fonts& fonts, const Options& opts) const {
    const Rect card{opts.x, opts.y, opts.w, opts.h};
    c.fillRect(card, opts.bgColor, opts.cornerRadius);
    if (opts.showBorder) {
        c.strokeRect(card, opts.borderColor, 2, opts.cornerRadius);
    }

    int cy = card.y + 30;

    // Canonical Title (tr(Str::WebControlTitle))
    const char* title = tr(Str::WebControlTitle);
    int titlePx = (card.w < 440) ? 24 : 26;
    int tw = fonts.bold.measure(title, titlePx);
    fonts.bold.draw(c, title, card.x + (card.w - tw) / 2, cy, titlePx, kWhite);
    cy += titlePx + 12;

    // Canonical Description (tr(Str::WebControlDesc)) with proper lateral padding
    int descPx = (card.w < 440) ? 15 : 16;
    auto lines = fonts.regular.wrap(tr(Str::WebControlDesc), descPx, card.w - 48, 4);
    for (const auto& l : lines) {
        int lw = fonts.regular.measure(l, descPx);
        fonts.regular.draw(c, l, card.x + (card.w - lw) / 2, cy, descPx, kGray);
        cy += descPx + 6;
    }
    cy += 14;

    std::string finalUrl = resolveUrl(opts.url, opts.path);

    // Compute available space for QR code and bottom pill
    int pillH = opts.showPill ? 44 : 0;
    int bottomPad = 24;
    int pillY = card.y + card.h - bottomPad - pillH;

    int availH = (opts.showPill ? pillY : (card.y + card.h - bottomPad)) - cy;
    int targetQr = std::clamp(std::min(card.w - 80, availH - 20), 180, 320);

    // Encode to determine exact card width
    uint8_t qrBuf[qrcodegen_BUFFER_LEN_MAX], tmpBuf[qrcodegen_BUFFER_LEN_MAX];
    int qrCardW = targetQr + 40;
    if (qrcodegen_encodeText(finalUrl.c_str(), tmpBuf, qrBuf, qrcodegen_Ecc_LOW,
                             qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        int n = qrcodegen_getSize(qrBuf);
        int mod = targetQr / (n + 2);
        qrCardW = mod * (n + 2) + 40;
    }

    int qrTop = cy + std::max(0, (availH - qrCardW) / 2);
    int qrRight = card.x + (card.w + qrCardW) / 2;
    drawQr(c, finalUrl, qrRight, qrTop, targetQr, qrcodegen_Ecc_LOW);

    // Bottom URL pill
    if (opts.showPill) {
        Rect pill{card.x + 32, pillY, card.w - 64, pillH};
        c.fillRect(pill, rgba(0, 0, 0, 140), 12);
        c.strokeRect(pill, rgba(255, 255, 255, 40), 1, 12);

        int urlPx = (card.w < 440) ? 17 : 20;
        int uw = fonts.semibold.measure(finalUrl, urlPx);
        fonts.semibold.draw(c, finalUrl, pill.x + (pill.w - uw) / 2,
                            fonts.semibold.centeredY(pill.y, pill.h, urlPx), urlPx, rgba(74, 222, 128));
    }
}

void WebControlCard::drawCard(Canvas& c, const Fonts& fonts, int x, int y, int w, int h,
                             const std::string& customUrl, const std::string& path) {
    Options opts;
    opts.x = x;
    opts.y = y;
    opts.w = w;
    opts.h = h;
    opts.url = customUrl;
    opts.path = path;
    WebControlCard card(opts);
    card.draw(c, fonts);
}

}  // namespace xc::ui
