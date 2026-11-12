// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Measuring how a game turns the right stick into camera speed, for gyro
// aiming (input/gyro.h GyroResponse): the stick is held at one level after
// another, each way, while small copies of the picture are kept; afterwards
// the picture's motion at each level gives the game's response curve, and the
// two numbers that describe it (where the camera starts to answer, and the
// curve's exponent).
//
// The motion is read per tile of the picture and the tiles that move together
// decide: what stays put on the screen (the HUD, the player's character) is
// left out, where one whole-picture match would be held at zero by it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xc::app::gyrocal {

constexpr int kThumbW = 128, kThumbH = 72;

// The sweep: the right stick at `ms` since its start. False once it is over.
bool sweepStickAt(uint64_t ms, float& x, float& y);
uint32_t sweepDurationMs();

// What was seen during a sweep: one small luma picture per frame, with the
// time it was shown and the stick sent then.
class Recording {
public:
    void clear();
    void reserve(size_t frames);
    // A decoded picture's luma plane, any size from 640x360 up.
    void add(const uint8_t* luma, int stride, int width, int height, uint64_t us, float stickX, float stickY);
    // A thumbnail made elsewhere (tests, saved measurements).
    void addThumb(const uint8_t* thumb, uint64_t us, float stickX, float stickY);
    size_t size() const { return us_.size(); }
    const uint8_t* thumb(size_t i) const { return thumbs_.data() + i * kThumbW * kThumbH; }
    uint64_t us(size_t i) const { return us_[i]; }
    float stickX(size_t i) const { return sx_[i]; }
    float stickY(size_t i) const { return sy_[i]; }
    int sourceWidth() const { return sourceWidth_; }

private:
    std::vector<uint8_t> thumbs_;
    std::vector<uint64_t> us_;
    std::vector<float> sx_, sy_;
    int sourceWidth_ = 1920;
};

// How far the picture's moving part went from thumbnail `a` to `b`, in
// thumbnail pixels along x (or y when `vertical`); + = to the right / down.
// False when the tiles don't agree (no texture, a cut, too fast for `a`..`b`).
// `moving`: how many tiles moved together (0 with a shift of 0: nothing moved).
bool pictureShift(const uint8_t* a, const uint8_t* b, bool vertical, float& shift, int& moving);

struct Point {
    float level = 0;  // the stick
    float speed = 0;  // picture pixels per second (the source's), both ways averaged
};

enum class Failure {
    None = 0,
    TooShort,      // the sweep did not run to its end
    NoMotion,      // the picture never moved: a menu open, no control of the camera
    Unreadable,    // too few levels could be read: a plain wall, the sky, too dark
    NoFit,         // the speeds don't follow one curve: something else moved the camera
};

struct Result {
    Failure failure = Failure::TooShort;
    bool ok() const { return failure == Failure::None; }
    int startPct = 12;         // GyroResponse::start, percent
    int exponentTenths = 10;   // GyroResponse::exponent, tenths
    float fitError = 0;        // rms distance of the curve from the measured levels, in stick units
    float topSpeed = 0;        // picture pixels per second at full stick
    float verticalRatio = 0;   // the game's vertical speed as a share of its horizontal one (0: not read)
    std::vector<Point> horizontal, vertical;
};

Result analyze(const Recording& rec);

// The two numbers that fit points best (speeds as shares of the top speed);
// the rms stick error is returned.
float fitResponse(const std::vector<Point>& points, float topSpeed, int& startPct, int& exponentTenths);

}  // namespace xc::app::gyrocal
