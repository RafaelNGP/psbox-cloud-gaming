// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "ui/font.h"

#include <cstdio>

#include "platform/platform.h"
#include "util/log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include <cstddef>

// The three fonts as built into the binary (CMakeLists.txt, cmake/embed_file.cmake).
extern const unsigned char xc_builtin_font_Regular[];
extern const std::size_t xc_builtin_font_Regular_size;
extern const unsigned char xc_builtin_font_SemiBold[];
extern const std::size_t xc_builtin_font_SemiBold_size;
extern const unsigned char xc_builtin_font_Bold[];
extern const std::size_t xc_builtin_font_Bold_size;

#include <atomic>
#include <cmath>

namespace xc::ui {

struct Font::Impl {
    stbtt_fontinfo info{};
    int ascent = 0, descent = 0, lineGap = 0;
    int capY1 = 0;
    bool hasCapBox = false;
};

Font::Font() : impl_(std::make_unique<Impl>()) {}
Font::~Font() = default;

bool Font::load(const std::string& path) {
    loaded_ = false;
    std::string bytes;
    if (!platform::readFile(path, bytes)) {
        XC_LOGW("font %s: cannot read", path.c_str());
        return false;
    }
    return loadMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), path.c_str());
}

bool Font::loadMemory(const unsigned char* bytes, size_t size, const char* what) {
    loaded_ = false;
    // A file cut short would send the parser past its end: the table directory must fit.
    if (!bytes || size < 12) {
        XC_LOGW("font %s: too short (%zu bytes)", what, size);
        return false;
    }
    // Every table must lie inside the bytes: the parser trusts the offsets it reads, and a file
    // cut short by a transfer has tables that point past its end.
    auto be16 = [&](size_t at) { return static_cast<uint32_t>(bytes[at] << 8 | bytes[at + 1]); };
    auto be32 = [&](size_t at) { return static_cast<uint64_t>(be16(at)) << 16 | be16(at + 2); };
    const size_t tables = be16(4);
    bool whole = tables > 0 && 12 + 16 * tables <= size;
    for (size_t t = 0; whole && t < tables; ++t) {
        uint64_t at = be32(12 + 16 * t + 8), len = be32(12 + 16 * t + 12);
        whole = at <= size && len <= size - at;
    }
    if (!whole) {
        XC_LOGW("font %s: incomplete (%zu bytes)", what, size);
        return false;
    }
    data_.assign(bytes, bytes + size);
    int offset = stbtt_GetFontOffsetForIndex(data_.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&impl_->info, data_.data(), offset)) {
        XC_LOGW("font %s: not a TrueType font", what);
        return false;
    }
    stbtt_GetFontVMetrics(&impl_->info, &impl_->ascent, &impl_->descent, &impl_->lineGap);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    impl_->hasCapBox = stbtt_GetCodepointBox(&impl_->info, 'H', &x0, &y0, &x1, &y1) != 0;
    impl_->capY1 = y1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
    loaded_ = true;
    return true;
}

bool Font::usable() const {
    if (loaded_) return true;
    static std::atomic<bool> told{false};
    if (!told.load(std::memory_order_relaxed) && !told.exchange(true)) XC_LOGE("a font is missing: its text isn't drawn");
    return false;
}

int Font::centeredY(int boxY, int boxH, int px) const {
    if (!usable()) return boxY;
    float s = scale(px);
    float capH = impl_->hasCapBox ? impl_->capY1 * s : 0.7f * px;
    float ascent = impl_->ascent * s;  // line top to baseline
    // Cap top = y + ascent - capH; centre it: (boxH - capH) / 2 below boxY.
    return static_cast<int>(std::lround(boxY + (boxH - capH) / 2.0f - (ascent - capH)));
}

float Font::scale(int px) const { return stbtt_ScaleForPixelHeight(&impl_->info, static_cast<float>(px)); }

int Font::lineHeight(int px) const {
    if (!usable()) return px;
    return static_cast<int>(std::ceil((impl_->ascent - impl_->descent) * scale(px)));
}

uint32_t nextCodepoint(std::string_view s, size_t& i) {
    auto c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0) return 0xFFFD;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}

const Font::Glyph& Font::glyph(uint32_t cp, int px) const {
    static const Glyph kNone;
    if (!usable()) return kNone;
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t key = (static_cast<uint64_t>(cp) << 32) | static_cast<uint32_t>(px);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    Glyph g;
    // Characters the font lacks are skipped instead of drawn as boxes.
    if (cp != ' ' && stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp)) == 0)
        return cache_.emplace(key, std::move(g)).first->second;
    float s = scale(px);
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&impl_->info, static_cast<int>(cp), &advance, &lsb);
    g.advance = static_cast<int>(std::lround(advance * s));
    unsigned char* bmp =
        stbtt_GetCodepointBitmap(&impl_->info, 0, s, static_cast<int>(cp), &g.w, &g.h, &g.xoff, &g.yoff);
    if (bmp) {
        g.bitmap.assign(bmp, bmp + static_cast<size_t>(g.w) * g.h);
        stbtt_FreeBitmap(bmp, nullptr);
    }
    return cache_.emplace(key, std::move(g)).first->second;
}

int Font::measure(std::string_view text, int px) const {
    int w = 0;
    for (size_t i = 0; i < text.size();) w += glyph(nextCodepoint(text, i), px).advance;
    return w;
}

int Font::draw(Canvas& c, std::string_view text, int x, int y, int px, Color color) const {
    if (!usable()) return x;
    int baseline = y + static_cast<int>(std::lround(impl_->ascent * scale(px)));
    for (size_t i = 0; i < text.size();) {
        const Glyph& g = glyph(nextCodepoint(text, i), px);
        if (!g.bitmap.empty()) c.drawMask(g.bitmap.data(), g.w, g.h, g.w, x + g.xoff, baseline + g.yoff, color);
        x += g.advance;
    }
    return x;
}

std::string Font::fit(std::string_view text, int px, int maxWidth) const {
    const char* dots = "...";
    int dotsW = measure(dots, px);
    int budget = maxWidth - dotsW;
    int totalW = 0;
    size_t cutIdx = 0;
    bool cutSet = false;
    for (size_t i = 0; i < text.size();) {
        size_t start = i;
        int adv = glyph(nextCodepoint(text, i), px).advance;
        if (!cutSet && totalW + adv > budget) {
            cutIdx = start;
            cutSet = true;
        }
        totalW += adv;
        if (totalW > maxWidth && cutSet) {
            std::string out(text.substr(0, cutIdx));
            while (!out.empty() && out.back() == ' ') out.pop_back();
            return out + dots;
        }
    }
    if (totalW <= maxWidth) return std::string(text);
    std::string out(text.substr(0, cutSet ? cutIdx : 0));
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + dots;
}

std::vector<std::string> Font::wrap(std::string_view input, int px, int maxWidth, int maxLines) const {
    // Store descriptions use \r\n and the odd tab.
    std::string cleaned;
    cleaned.reserve(input.size());
    for (char ch : input) {
        if (ch == '\r') continue;
        cleaned += ch == '\t' ? ' ' : ch;
    }
    std::string_view text = cleaned;
    std::vector<std::string> lines;
    std::string current;
    size_t i = 0;
    auto flush = [&] {
        while (!current.empty() && current.back() == ' ') current.pop_back();
        lines.push_back(current);
        current.clear();
    };
    while (i < text.size()) {
        if (text[i] == '\n') {
            ++i;
            if (!current.empty()) flush();
            if (static_cast<int>(lines.size()) >= maxLines) break;
            continue;
        }
        size_t end = text.find_first_of(" \n", i);
        if (end == std::string_view::npos) end = text.size();
        std::string word(text.substr(i, end - i));
        std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && measure(candidate, px) > maxWidth) {
            flush();
            if (static_cast<int>(lines.size()) >= maxLines) break;
            current = word;
        } else {
            current = candidate;
        }
        i = end < text.size() && text[end] == ' ' ? end + 1 : end;
    }
    if (!current.empty() && static_cast<int>(lines.size()) < maxLines) flush();
    bool truncated = i < text.size();
    if (truncated && !lines.empty()) lines.back() = fit(lines.back() + " ...", px, maxWidth);
    for (auto& l : lines) l = fit(l, px, maxWidth);
    return lines;
}

bool Fonts::load(const std::string& dir) {
    // The file when it is there and whole; otherwise the copy inside the binary, so that a folder
    // out of reach or a transfer that left the fonts out never leaves the screens without text.
    struct Face {
        Font& font;
        const char* file;
        const unsigned char* builtin;
        std::size_t builtinSize;
    } faces[] = {{regular, "Inter-Regular.ttf", xc_builtin_font_Regular, xc_builtin_font_Regular_size},
                 {semibold, "Inter-SemiBold.ttf", xc_builtin_font_SemiBold, xc_builtin_font_SemiBold_size},
                 {bold, "Inter-Bold.ttf", xc_builtin_font_Bold, xc_builtin_font_Bold_size}};
    bool all = true;
    usedBuiltin = false;
    for (auto& f : faces) {
        if (f.font.load(dir + "/" + f.file)) continue;
        // The embedding adds a closing zero byte: not part of the font.
        bool ok = f.font.loadMemory(f.builtin, f.builtinSize ? f.builtinSize - 1 : 0, "built-in");
        XC_LOGW("fonts: %s not usable in %s; the copy inside the app %s", f.file, dir.c_str(), ok ? "is used" : "failed too");
        usedBuiltin = true;
        all = all && ok;
    }
    return all;
}

bool Fonts::loadIcons(const std::vector<std::string>& paths) {
    for (const auto& p : paths) {
        FILE* f = std::fopen(p.c_str(), "rb");  // a path the system doesn't have here: tried quietly
        if (!f) continue;
        std::fclose(f);
        if (icons.load(p)) return hasIcons = true;
    }
    return false;
}

}  // namespace xc::ui
