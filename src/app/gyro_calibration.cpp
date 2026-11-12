// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "app/gyro_calibration.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xc::app::gyrocal {

namespace {

// Each level is held one way, rested, held the other way (back to where the
// camera was), rested. The second half of each hold is what gets measured:
// games ease into a turn.
constexpr float kLevelsX[] = {0.14f, 0.18f, 0.22f, 0.26f, 0.30f, 0.35f, 0.40f,
                              0.45f, 0.50f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f};
constexpr float kLevelsY[] = {0.35f, 0.50f, 0.70f, 1.00f};
constexpr uint32_t kLeadMs = 500, kHoldXMs = 900, kHoldYMs = 500, kRestMs = 300;
// Full stick is held longer: games keep speeding the turn up for a while there,
// and every other speed is measured against this one.
constexpr uint32_t kHoldTopMs = 1600;

constexpr int kTileW = 16, kTileH = 12, kRange = 6;  // thumbnail pixels; shifts searched: -kRange..kRange
constexpr float kPictureDelayS = 0.06f;              // stick to picture, about (the stream's delay)

float median(std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    float m = v[mid];
    if (v.size() % 2 == 0) {
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid) - 1, v.end());
        m = 0.5f * (m + v[mid - 1]);
    }
    return m;
}

// One tile of `b` against `a` moved by each shift: the best one (sub-pixel),
// and how sharp the match is (its error over the mean error; small = sure).
bool tileShift(const uint8_t* a, const uint8_t* b, int tx, int ty, bool vertical, float& shift, float& fit) {
    // A flat tile matches anywhere.
    uint32_t sum = 0, sumSq = 0;
    for (int y = 0; y < kTileH; ++y)
        for (int x = 0; x < kTileW; ++x) {
            const uint32_t v = b[(ty + y) * kThumbW + tx + x];
            sum += v, sumSq += v * v;
        }
    const float n = static_cast<float>(kTileW * kTileH), mean = static_cast<float>(sum) / n;
    if (static_cast<float>(sumSq) / n - mean * mean < 16.0f) return false;
    float cost[2 * kRange + 1];
    int best = 0;
    float total = 0;
    for (int d = -kRange; d <= kRange; ++d) {
        uint32_t c = 0;
        const int ox = vertical ? 0 : -d, oy = vertical ? -d : 0;
        for (int y = 0; y < kTileH; ++y) {
            const uint8_t* rb = b + (ty + y) * kThumbW + tx;
            const uint8_t* ra = a + (ty + y + oy) * kThumbW + tx + ox;
            for (int x = 0; x < kTileW; ++x) c += static_cast<uint32_t>(std::abs(static_cast<int>(rb[x]) - static_cast<int>(ra[x])));
        }
        cost[d + kRange] = static_cast<float>(c);
        total += static_cast<float>(c);
        if (cost[d + kRange] < cost[best + kRange]) best = d;
    }
    const float c0 = cost[best + kRange], avg = total / static_cast<float>(2 * kRange + 1);
    fit = avg > 1e-3f ? c0 / avg : 1.0f;
    float sub = 0;
    if (best > -kRange && best < kRange) {
        const float l = cost[best + kRange - 1], r = cost[best + kRange + 1], den = l - 2 * c0 + r;
        if (den > 1e-3f) sub = 0.5f * (l - r) / den;
    }
    shift = static_cast<float>(best) + sub;
    return true;
}

// Picture pixels per second around frame `i`, from the shortest span of frames
// that shows a clear shift: one frame apart for a fast turn, up to 64 for a
// slow one. False when nothing could be read.
bool speedAt(const Recording& rec, size_t i, bool vertical, float& speed) {
    const float pxPerThumb = static_cast<float>(rec.sourceWidth()) / static_cast<float>(kThumbW);
    bool have = false;
    for (size_t k : {size_t(1), size_t(2), size_t(4), size_t(8), size_t(16), size_t(32), size_t(64)}) {
        if (i < k) break;
        float shift = 0;
        int moving = 0;
        if (!pictureShift(rec.thumb(i - k), rec.thumb(i), vertical, shift, moving)) {
            if (have) break;  // further apart only gets worse
            continue;
        }
        if (std::abs(shift) > static_cast<float>(kRange) - 1.5f) break;  // at the search's edge: the shorter span stands
        const float dt = static_cast<float>(rec.us(i) - rec.us(i - k)) / 1e6f;
        if (dt <= 0) break;
        speed = shift * pxPerThumb / dt;
        have = true;
        if (std::abs(shift) >= 2.5f) break;  // precise enough
    }
    return have;
}

struct Hold {
    bool vertical = false;
    float level = 0;  // signed
    size_t first = 0, last = 0;
};

}  // namespace

bool sweepStickAt(uint64_t ms, float& x, float& y) {
    x = y = 0.0f;
    if (ms < kLeadMs) return true;
    uint64_t t = ms - kLeadMs;
    const uint64_t perY = 2 * (kHoldYMs + kRestMs);
    for (float v : kLevelsX) {
        const uint64_t hold = v >= 0.999f ? kHoldTopMs : kHoldXMs, perX = 2 * (hold + kRestMs);
        if (t < perX) {
            x = t < hold ? v : t < hold + kRestMs ? 0.0f : t < 2 * hold + kRestMs ? -v : 0.0f;
            return true;
        }
        t -= perX;
    }
    for (float v : kLevelsY) {
        if (t < perY) {
            y = t < kHoldYMs ? v : t < kHoldYMs + kRestMs ? 0.0f : t < 2 * kHoldYMs + kRestMs ? -v : 0.0f;
            return true;
        }
        t -= perY;
    }
    return false;
}

uint32_t sweepDurationMs() {
    return kLeadMs + static_cast<uint32_t>(std::size(kLevelsX)) * 2 * (kHoldXMs + kRestMs) + 2 * (kHoldTopMs - kHoldXMs) +
           static_cast<uint32_t>(std::size(kLevelsY)) * 2 * (kHoldYMs + kRestMs);
}

void Recording::clear() {
    thumbs_.clear();
    us_.clear();
    sx_.clear();
    sy_.clear();
}

void Recording::reserve(size_t frames) {
    thumbs_.reserve(frames * kThumbW * kThumbH);
    us_.reserve(frames);
    sx_.reserve(frames);
    sy_.reserve(frames);
}

void Recording::add(const uint8_t* luma, int stride, int width, int height, uint64_t us, float stickX, float stickY) {
    if (!luma || width < 640 || height < 360) return;
    sourceWidth_ = width;
    const int bw = width / kThumbW, bh = height / kThumbH;
    const size_t at = thumbs_.size();
    thumbs_.resize(at + static_cast<size_t>(kThumbW) * kThumbH);
    uint8_t* out = thumbs_.data() + at;
    const uint32_t count = static_cast<uint32_t>(((bh + 2) / 3) * ((bw + 2) / 3));
    for (int ty = 0; ty < kThumbH; ++ty)
        for (int tx = 0; tx < kThumbW; ++tx) {
            uint32_t sum = 0;
            for (int y = 0; y < bh; y += 3) {
                const uint8_t* row = luma + static_cast<size_t>(ty * bh + y) * static_cast<size_t>(stride) + tx * bw;
                for (int x = 0; x < bw; x += 3) sum += row[x];
            }
            *out++ = static_cast<uint8_t>(sum / count);
        }
    us_.push_back(us);
    sx_.push_back(stickX);
    sy_.push_back(stickY);
}

void Recording::addThumb(const uint8_t* thumb, uint64_t us, float stickX, float stickY) {
    thumbs_.insert(thumbs_.end(), thumb, thumb + kThumbW * kThumbH);
    us_.push_back(us);
    sx_.push_back(stickX);
    sy_.push_back(stickY);
}

bool pictureShift(const uint8_t* a, const uint8_t* b, bool vertical, float& shift, int& moving) {
    std::vector<float> good;
    good.reserve(48);
    const int x0 = vertical ? 0 : kRange, x1 = kThumbW - kTileW - (vertical ? 0 : kRange);
    const int y0 = vertical ? kRange : 0, y1 = kThumbH - kTileH - (vertical ? kRange : 0);
    for (int ty = y0; ty <= y1; ty += kTileH)
        for (int tx = x0; tx <= x1; tx += kTileW) {
            float s = 0, fit = 1;
            if (tileShift(a, b, tx, ty, vertical, s, fit) && fit < 0.7f) good.push_back(s);
        }
    shift = 0.0f;
    moving = 0;
    if (good.size() < 5) return false;
    // What stays put on the screen reads 0; the scene's tiles read the turn.
    std::vector<float> mov;
    for (float s : good)
        if (std::abs(s) > 0.35f) mov.push_back(s);
    if (mov.size() < std::max<size_t>(3, good.size() * 12 / 100)) return true;  // nothing moved
    std::vector<float> tmp = mov;
    const float d = median(tmp);
    std::vector<float> near;
    for (float s : mov)
        if (std::abs(s - d) < std::max(0.5f, 0.25f * std::abs(d))) near.push_back(s);
    if (near.size() < std::max<size_t>(3, mov.size() * 6 / 10)) return false;  // the moving tiles disagree
    moving = static_cast<int>(near.size());
    shift = median(near);
    return true;
}

float fitResponse(const std::vector<Point>& points, float topSpeed, int& startPct, int& exponentTenths) {
    float best = 1e9f;
    if (topSpeed <= 0) return best;
    for (int s = 0; s <= 40; ++s)
        for (int e = 10; e <= 40; ++e) {
            const float start = static_cast<float>(s) / 100.0f, exponent = static_cast<float>(e) / 10.0f;
            float sum = 0;
            int n = 0;
            for (const Point& p : points) {
                if (p.speed <= 0) continue;
                const float v = std::min(1.0f, p.speed / topSpeed);
                const float level = start + (1.0f - start) * std::pow(v, 1.0f / exponent);
                sum += (level - p.level) * (level - p.level);
                ++n;
            }
            if (n < 3) continue;
            const float err = std::sqrt(sum / static_cast<float>(n));
            if (err < best) best = err, startPct = s, exponentTenths = e;
        }
    return best;
}

Result analyze(const Recording& rec) {
    Result r;
    const size_t n = rec.size();
    if (n < 600) return r;  // under ~10 s of pictures
    // The holds: runs of frames with the same stick, off centre.
    std::vector<Hold> holds;
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && rec.stickX(j) == rec.stickX(i) && rec.stickY(j) == rec.stickY(i)) ++j;
        const float sx = rec.stickX(i), sy = rec.stickY(i);
        if ((sx != 0 || sy != 0) && j - i >= 12) holds.push_back({sx == 0, sx != 0 ? sx : sy, i, j - 1});
        i = j;
    }
    if (holds.size() < 16) return r;

    struct Level {
        float level;
        float speed[2];  // + and - holds, signed; NaN when unread
    };
    std::vector<Level> levels[2];  // horizontal, vertical
    const float nan = std::nanf("");
    for (const Hold& h : holds) {
        // The second half of the hold, as it reaches the picture.
        const uint64_t t0 = rec.us(h.first), t1 = rec.us(h.last);
        const uint64_t span = t1 - t0;
        const uint64_t from = t0 + (span > 1200000 ? span * 55 / 100 : span / 2) + static_cast<uint64_t>(kPictureDelayS * 1e6f);
        const uint64_t to = t1 + static_cast<uint64_t>(kPictureDelayS * 0.4e6f);
        std::vector<float> speeds;
        for (size_t i = h.first; i < n && rec.us(i) <= to; i += 3) {
            if (rec.us(i) < from) continue;
            float s = 0;
            if (speedAt(rec, i, h.vertical, s)) speeds.push_back(s);
        }
        const float speed = speeds.size() >= 3 ? median(speeds) : nan;
        auto& list = levels[h.vertical ? 1 : 0];
        const float level = std::abs(h.level);
        auto it = std::find_if(list.begin(), list.end(), [&](const Level& l) { return std::abs(l.level - level) < 1e-4f; });
        if (it == list.end()) list.push_back({level, {nan, nan}}), it = list.end() - 1;
        it->speed[h.level > 0 ? 0 : 1] = speed;
    }

    // Each level: the two ways must agree (the same turn back and forth).
    auto fold = [](std::vector<Level>& list) {
        std::vector<Point> out;
        std::sort(list.begin(), list.end(), [](const Level& a, const Level& b) { return a.level < b.level; });
        for (const Level& l : list) {
            const float a = std::abs(l.speed[0]), b = std::abs(l.speed[1]);
            if (std::isnan(a) || std::isnan(b)) continue;
            if (a == 0 && b == 0) {
                out.push_back({l.level, 0.0f});
                continue;
            }
            const bool opposite = (l.speed[0] > 0) != (l.speed[1] > 0);
            if (!opposite || std::abs(a - b) > 0.35f * std::max(a, b) + 6.0f) continue;
            out.push_back({l.level, 0.5f * (a + b)});
        }
        return out;
    };
    r.horizontal = fold(levels[0]);
    r.vertical = fold(levels[1]);

    int movingLevels = 0;
    for (const Point& p : r.horizontal)
        if (p.speed > 0) ++movingLevels;
    if (movingLevels == 0) {
        r.failure = Failure::NoMotion;
        return r;
    }
    if (movingLevels < 7 || r.horizontal.back().level < 0.85f || r.horizontal.back().speed <= 0) {
        r.failure = Failure::Unreadable;
        return r;
    }
    // The top speed: full stick, or the last level read scaled up along its neighbours' slope.
    r.topSpeed = r.horizontal.back().speed;
    if (r.horizontal.back().level < 0.999f) r.topSpeed /= std::pow(r.horizontal.back().level, 2.0f);
    r.fitError = fitResponse(r.horizontal, r.topSpeed, r.startPct, r.exponentTenths);
    if (r.fitError > 0.05f) {
        r.failure = Failure::NoFit;
        return r;
    }
    // The vertical speed against the horizontal one at the same levels.
    std::vector<float> ratios;
    for (const Point& v : r.vertical)
        for (const Point& h : r.horizontal)
            if (std::abs(h.level - v.level) < 1e-4f && h.speed > 20 && v.speed > 0) ratios.push_back(v.speed / h.speed);
    r.verticalRatio = ratios.size() >= 2 ? median(ratios) : 0.0f;
    r.failure = Failure::None;
    return r;
}

}  // namespace xc::app::gyrocal
