// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstddef>
#include <cstdint>
#include "ui/svg_icon.h"

#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// nanosvg (extern/nanosvg, zlib licence): its own code, built here once, its warnings not ours.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// The SVG files, embedded by cmake/embed_file.cmake (NUL-terminated).
extern const unsigned char xc_icon_guitar_svg[];
extern const std::size_t xc_icon_guitar_svg_size;

namespace xc::ui {

namespace {
struct Mask {
    int w = 0, h = 0;
    std::vector<uint8_t> coverage;
};
struct Entry {
    NSVGimage* image = nullptr;
    bool read = false;
    std::map<int, Mask> sizes;  // by width
};
constexpr size_t kIcons = 1;
constexpr size_t kSizesKept = 16;
std::mutex g_mutex;
Entry g_icons[kIcons];

const unsigned char* sourceOf(SvgIcon icon, size_t& size) {
    switch (icon) {
        case SvgIcon::Guitar: size = xc_icon_guitar_svg_size; return xc_icon_guitar_svg;
    }
    size = 0;
    return nullptr;
}

const Mask* maskFor(SvgIcon icon, int w) {
    Entry& e = g_icons[static_cast<size_t>(icon)];
    if (!e.read) {
        e.read = true;
        size_t size = 0;
        const unsigned char* data = sourceOf(icon, size);
        std::string text(reinterpret_cast<const char*>(data), size);  // nsvgParse writes into it
        e.image = data ? nsvgParse(text.data(), "px", 96.0f) : nullptr;
        if (e.image && (e.image->width <= 0 || e.image->height <= 0)) nsvgDelete(e.image), e.image = nullptr;
        if (!e.image) XC_LOGW("svg icon %d: not readable", static_cast<int>(icon));
    }
    if (!e.image || w <= 0 || w > 1024) return nullptr;
    if (auto it = e.sizes.find(w); it != e.sizes.end()) return &it->second;
    if (e.sizes.size() >= kSizesKept) e.sizes.clear();
    const float scale = static_cast<float>(w) / e.image->width;
    Mask m;
    m.w = w;
    m.h = std::max(1, static_cast<int>(std::lround(e.image->height * scale)));
    std::vector<unsigned char> rgba(static_cast<size_t>(m.w) * static_cast<size_t>(m.h) * 4);
    NSVGrasterizer* r = nsvgCreateRasterizer();
    if (!r) return nullptr;
    nsvgRasterize(r, e.image, 0, 0, scale, rgba.data(), m.w, m.h, m.w * 4);
    nsvgDeleteRasterizer(r);
    m.coverage.resize(static_cast<size_t>(m.w) * static_cast<size_t>(m.h));
    for (size_t i = 0; i < m.coverage.size(); ++i) m.coverage[i] = rgba[i * 4 + 3];
    return &e.sizes.emplace(w, std::move(m)).first->second;
}
}  // namespace

bool drawSvgIcon(Canvas& c, SvgIcon icon, int x, int y, int w, Color color) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Mask* m = maskFor(icon, w);
    if (!m) return false;
    c.drawMask(m->coverage.data(), m->w, m->h, m->w, x, y, color);
    return true;
}

}  // namespace xc::ui
