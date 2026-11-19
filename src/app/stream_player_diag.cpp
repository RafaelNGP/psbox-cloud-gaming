// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/stream_player_diag.h"

#include "display/display.h"
#include "util/log.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <exception>
#if !defined(_WIN32)
#include <sys/resource.h>
#endif

namespace xc::app {

namespace {

// The process's CPU time, every thread (system libraries' too): -1 where unknown.
int64_t processCpuUs() {
#if defined(_WIN32)
    return -1;
#else
    rusage u{};
    if (getrusage(RUSAGE_SELF, &u) != 0) return -1;
    return (static_cast<int64_t>(u.ru_utime.tv_sec) + u.ru_stime.tv_sec) * 1000000 + u.ru_utime.tv_usec +
           u.ru_stime.tv_usec;
#endif
}

}  // namespace

// --- StreamRecorder ---------------------------------------------------------

StreamRecorder::~StreamRecorder() { stop(); }

bool StreamRecorder::start(const std::string& dir, int seconds) {
    stop();
    video_ = std::fopen((dir + "/rec.h264").c_str(), "wb");
    index_ = std::fopen((dir + "/rec.idx").c_str(), "wb");
    if (!video_ || !index_) {
        XC_LOGW("recording: cannot open %s/rec.h264 or rec.idx", dir.c_str());
        stop();
        return false;
    }
    startMs_ = platform::nowMs();
    untilMs_ = startMs_ + static_cast<uint64_t>(seconds) * 1000;
    frames_ = bytes_ = dropped_ = 0;
    running_ = true;
    writer_ = std::thread([this] { writeLoop(); });
    XC_LOGI("recording: started, %d s, into %s/rec.h264 (frame times in rec.idx)", seconds, dir.c_str());
    return true;
}

void StreamRecorder::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && !writer_.joinable()) return;
        running_ = false;
    }
    cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (video_) std::fclose(video_), video_ = nullptr;
    if (index_) std::fclose(index_), index_ = nullptr;
    if (frames_ || dropped_) {
        XC_LOGI("recording: stopped, %llu frames, %llu MB, %llu dropped", static_cast<unsigned long long>(frames_),
                static_cast<unsigned long long>(bytes_ >> 20), static_cast<unsigned long long>(dropped_));
    }
    frames_ = dropped_ = 0;
}

bool StreamRecorder::expired() const { return active() && platform::nowMs() >= untilMs_; }

void StreamRecorder::frame(const uint8_t* d, size_t n) {
    if (!active()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= 240) {
        ++dropped_;
        return;
    }
    queue_.push_back({std::vector<uint8_t>(d, d + n), platform::nowMs() - startMs_});
    cv_.notify_one();
}

void StreamRecorder::writeLoop() {
    for (;;) {
        Item item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (queue_.empty()) return;
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        std::fwrite(item.data.data(), 1, item.data.size(), video_);
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char clock[16];
        std::strftime(clock, sizeof clock, "%H:%M:%S", &local);
        std::fprintf(index_, "%llu %llu %s\n", static_cast<unsigned long long>(frames_),
                     static_cast<unsigned long long>(item.atMs), clock);
        ++frames_;
        bytes_ += item.data.size();
    }
}

// --- StreamSnapshotter ------------------------------------------------------

StreamSnapshotter::~StreamSnapshotter() { stop(); }

void StreamSnapshotter::request(const std::string& path) {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshotPath_ = path;
}

bool StreamSnapshotter::saveSnapshotIfAsked(const media::Picture& pic) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        path.swap(snapshotPath_);
    }
    if (path.empty()) return false;
    screenPath_ = path.substr(0, path.rfind('/') + 1) + "screen.ppm";
    int w = pic.width / 2, h = pic.height / 2;
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    const bool isNv12 = (pic.format == media::Picture::Format::Nv12);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int c = (pic.y[(2 * y) * pic.strideY + 2 * x] - 16) * 1192;
            int d = isNv12 ? (pic.u[y * pic.strideU + 2 * x] - 128) : (pic.u[y * pic.strideU + x] - 128);
            int e = isNv12 ? (pic.u[y * pic.strideU + 2 * x + 1] - 128) : (pic.v[y * pic.strideV + x] - 128);
            auto clamp = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); };
            uint8_t* o = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            o[0] = clamp((c + 1836 * e + 512) >> 10);
            o[1] = clamp((c - 218 * d - 546 * e + 512) >> 10);
            o[2] = clamp((c + 2163 * d + 512) >> 10);
        }
    }
    if (snapshotThread_.joinable()) snapshotThread_.join();
    platform::startThread(
        snapshotThread_,
        [path, rgb = std::move(rgb), w, h]() mutable {
            std::string data = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
            data.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
            bool ok = platform::writeFileAtomic(path, data);
            XC_LOGI("snapshot %dx%d -> %s: %s", w, h, path.c_str(), ok ? "ok" : "failed");
        },
        512u << 10);
    return true;
}

void StreamSnapshotter::saveScreen() {
    try {
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        if (!display::readBackRgb(rgb, w, h)) return;
        std::string path = screenPath_;
        if (screenThread_.joinable()) screenThread_.join();
        platform::startThread(
            screenThread_,
            [path, rgb = std::move(rgb), w, h]() mutable {
                try {
                    std::string data = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
                    data.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
                    std::vector<uint8_t>().swap(rgb);  // release the 6.2 MB vector immediately
                    bool ok = platform::writeFileAtomic(path, data);
                    XC_LOGI("screen -> %s: %s", path.c_str(), ok ? "ok" : "failed");
                } catch (const std::exception& e) {
                    XC_LOGW("screen thread error: %s", e.what());
                }
            },
            512u << 10);
    } catch (const std::exception& e) {
        XC_LOGW("saveScreen failed: %s", e.what());
    }
}

void StreamSnapshotter::stop() {
    if (snapshotThread_.joinable()) snapshotThread_.join();
    if (screenThread_.joinable()) screenThread_.join();
}

// --- StreamMotionRecorder ---------------------------------------------------

float StreamMotionRecorder::profileShift(const std::vector<float>& prev, const std::vector<float>& cur, int range,
                                         float& fit) {
    const int n = static_cast<int>(cur.size());
    std::vector<float> cost(static_cast<size_t>(2 * range + 1));
    float sum = 0;
    int best = 0;
    for (int s = -range; s <= range; ++s) {
        float c = 0;
        int m = 0;
        for (int i = range; i < n - range; ++i, ++m) c += std::abs(cur[i] - prev[i - s]);
        c /= static_cast<float>(m > 0 ? m : 1);
        cost[static_cast<size_t>(s + range)] = c;
        sum += c;
        if (c < cost[static_cast<size_t>(best + range)]) best = s;
    }
    const float mean = sum / static_cast<float>(cost.size()), c0 = cost[static_cast<size_t>(best + range)];
    fit = mean > 1e-6f ? c0 / mean : 1.0f;
    float sub = 0;
    if (best > -range && best < range) {
        const float a = cost[static_cast<size_t>(best + range - 1)], b = cost[static_cast<size_t>(best + range + 1)];
        const float den = a - 2 * c0 + b;
        if (den > 1e-9f) sub = 0.5f * (a - b) / den;
    }
    return static_cast<float>(best) + sub;
}

void StreamMotionRecorder::setRawDump(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    rawDump_ = on;
    raw_.clear();
    if (on) raw_.reserve(48u * 1024 * 1024);
}

bool StreamMotionRecorder::rawTake(std::string& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!rawDump_ || state_ == 1 || raw_.empty()) return false;
    out.swap(raw_);
    raw_ = std::string();
    rawDump_ = false;
    return true;
}

void StreamMotionRecorder::start(int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    seconds = std::clamp(seconds, 1, 180);
    rec_.clear();
    rec_.reserve(static_cast<size_t>(seconds) * 130);
    prevCols_.clear();
    prevRows_.clear();
    untilUs_ = platform::nowUs() + static_cast<uint64_t>(seconds) * 1000000u;
    state_ = 1;
}

bool StreamMotionRecorder::take(std::vector<StreamPlayer::MotionSample>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == 1 && platform::nowUs() >= untilUs_) state_ = 2;
    if (state_ != 2) return false;
    out.swap(rec_);
    rec_.clear();
    state_ = 0;
    return true;
}

void StreamMotionRecorder::measure(const media::Picture& pic, uint64_t nowUs) {
    if (state_.load(std::memory_order_relaxed) != 1 || !pic.y || pic.width < 640 || pic.height < 360) return;
    const int x0 = pic.width / 5, x1 = pic.width * 4 / 5, y0 = pic.height / 8, y1 = pic.height / 2;
    std::vector<float> cols(static_cast<size_t>((x1 - x0) / 2)), rows(static_cast<size_t>((y1 - y0) / 2));
    for (size_t i = 0; i < cols.size(); ++i) {
        const int x = x0 + static_cast<int>(i) * 2;
        uint32_t sum = 0;
        for (int y = y0; y < y1; y += 4)
            sum += pic.y[static_cast<size_t>(y) * pic.strideY + x] +
                   pic.y[static_cast<size_t>(y) * pic.strideY + x + 1];
        cols[i] = static_cast<float>(sum) / static_cast<float>(2 * ((y1 - y0 + 3) / 4));
    }
    for (size_t j = 0; j < rows.size(); ++j) {
        const int y = y0 + static_cast<int>(j) * 2;
        uint32_t sum = 0;
        for (int x = x0; x < x1; x += 4)
            sum += pic.y[static_cast<size_t>(y) * pic.strideY + x] +
                   pic.y[static_cast<size_t>(y + 1) * pic.strideY + x];
        rows[j] = static_cast<float>(sum) / static_cast<float>(2 * ((x1 - x0 + 3) / 4));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (nowUs >= untilUs_ || rec_.size() >= rec_.capacity()) {
        state_ = 2;
        return;
    }
    if (rawDump_ && raw_.size() + 12000 < raw_.capacity() && prevCols_.size() == cols.size()) {
        raw_.append(reinterpret_cast<const char*>(cols.data()), cols.size() * sizeof(float));
        const int bw = pic.width / 128, bh = pic.height / 72;
        for (int ty = 0; ty < 72; ++ty) {
            for (int tx = 0; tx < 128; ++tx) {
                uint32_t sum = 0;
                for (int yy = 0; yy < bh; yy += 3)
                    for (int xx = 0; xx < bw; xx += 3)
                        sum += pic.y[static_cast<size_t>(ty * bh + yy) * pic.strideY + tx * bw + xx];
                raw_.push_back(static_cast<char>(sum / static_cast<uint32_t>(((bh + 2) / 3) * ((bw + 2) / 3))));
            }
        }
    }
    if (prevCols_.size() == cols.size() && prevRows_.size() == rows.size()) {
        StreamPlayer::MotionSample s{};
        s.us = nowUs;
        s.dx = 2.0f * profileShift(prevCols_, cols, 120, s.fitX);
        s.dy = 2.0f * profileShift(prevRows_, rows, 60, s.fitY);
        if (motionStick_) motionStick_(nowUs, s.stickX, s.stickY);
        rec_.push_back(s);
    }
    prevCols_.swap(cols);
    prevRows_.swap(rows);
}

// --- StreamBenchTracker -----------------------------------------------------

void StreamBenchTracker::Hist::add(uint64_t us) {
    ++n[std::min(us / kStepUs, kBuckets)];
    ++count;
    sumUs += us;
    maxUs = std::max(maxUs, us);
}

double StreamBenchTracker::Hist::avgMs() const {
    return count ? sumUs / 1000.0 / static_cast<double>(count) : 0.0;
}

double StreamBenchTracker::Hist::maxMs() const { return maxUs / 1000.0; }

double StreamBenchTracker::Hist::pctMs(double p) const {
    if (!count) return 0.0;
    const uint64_t want = static_cast<uint64_t>(p * static_cast<double>(count - 1)) + 1;
    uint64_t seen = 0;
    for (uint64_t i = 0; i <= kBuckets; ++i) {
        seen += n[i];
        if (seen >= want) return std::min((i + 0.5) * kStepUs, static_cast<double>(maxUs)) / 1000.0;
    }
    return maxMs();
}

void StreamBenchTracker::recordDecode(uint64_t us, const media::VideoDecoder& d) {
    if (!on_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    window_.decode.add(us);
    if (!d.isHardware()) {
        ++window_.swCalls;
        return;
    }
    ++window_.hwCalls;
    const auto& t = d.hwTiming();
    window_.hwDecode.add(t.decodeUs);
    if (t.decodeCpuUs >= 0) window_.hwDecodeCpu.add(static_cast<uint64_t>(t.decodeCpuUs));
    if (t.flushed) ++window_.flushed, window_.hwFlush.add(t.flushUs);
    if (d.lastQpParseUs() >= 0) window_.qpParse.add(static_cast<uint64_t>(d.lastQpParseUs()));
}

void StreamBenchTracker::record(Hist Window::*h, uint64_t us) {
    if (!on_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    (window_.*h).add(us);
}

void StreamBenchTracker::markWindow(const std::string& name, const Counters& counters) {
    const int64_t cpuNow = processCpuUs();
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t now = platform::nowMs();
    if (on_) {
        const Window& b = window_;
        const char* dec = b.hwCalls && b.swCalls ? "mixed" : b.hwCalls ? "HW" : b.swCalls ? "SW" : "none";
        XC_LOGI("BENCH window \"%s\": %.1f s, decoder %s (%llu sw, %llu hw calls); "
                "decode avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f ms; "
                "hw decode avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f ms; "
                "hw decode thread cpu avg %.2f p50 %.2f p95 %.2f ms; process cpu %.0f%% of a core; "
                "flush %llu of %llu, avg %.2f p50 %.2f p95 %.2f max %.2f ms; "
                "qp parse %llu, avg %.3f p95 %.3f max %.3f ms; "
                "draw avg %.2f p95 %.2f max %.2f ms; "
                "on screen avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f ms; "
                "first packet to screen avg %.2f p50 %.2f p95 %.2f max %.2f ms; "
                "%llu decoded, %llu skipped, %llu late, %llu failed; rtp %llu lost, %llu recovered, %llu frames dropped",
                b.name.c_str(), (now - b.startMs) / 1000.0, dec, static_cast<unsigned long long>(b.swCalls),
                static_cast<unsigned long long>(b.hwCalls),
                b.decode.avgMs(), b.decode.pctMs(0.5), b.decode.pctMs(0.95), b.decode.pctMs(0.99), b.decode.maxMs(),
                b.hwDecode.avgMs(), b.hwDecode.pctMs(0.5), b.hwDecode.pctMs(0.95), b.hwDecode.pctMs(0.99),
                b.hwDecode.maxMs(),
                b.hwDecodeCpu.avgMs(), b.hwDecodeCpu.pctMs(0.5), b.hwDecodeCpu.pctMs(0.95),
                (b.cpu0Us >= 0 && cpuNow >= 0 && now > b.startMs)
                    ? 100.0 * static_cast<double>(cpuNow - b.cpu0Us) / 1000.0 / static_cast<double>(now - b.startMs)
                    : -1.0,
                static_cast<unsigned long long>(b.flushed), static_cast<unsigned long long>(b.hwCalls),
                b.hwFlush.avgMs(), b.hwFlush.pctMs(0.5), b.hwFlush.pctMs(0.95), b.hwFlush.maxMs(),
                static_cast<unsigned long long>(b.qpParse.count), b.qpParse.avgMs(), b.qpParse.pctMs(0.95),
                b.qpParse.maxMs(),
                b.draw.avgMs(), b.draw.pctMs(0.95), b.draw.maxMs(),
                b.onScreen.avgMs(), b.onScreen.pctMs(0.5), b.onScreen.pctMs(0.95), b.onScreen.pctMs(0.99),
                b.onScreen.maxMs(),
                b.glass.avgMs(), b.glass.pctMs(0.5), b.glass.pctMs(0.95), b.glass.maxMs(),
                static_cast<unsigned long long>(counters.decoded - b.decoded0),
                static_cast<unsigned long long>(counters.dropped - b.dropped0),
                static_cast<unsigned long long>(counters.late - b.late0),
                static_cast<unsigned long long>(counters.failures - b.failures0),
                static_cast<unsigned long long>(counters.rtpLost - b.rtpLost0),
                static_cast<unsigned long long>(counters.rtpRecovered - b.rtpRecovered0),
                static_cast<unsigned long long>(counters.rtpDropped - b.rtpDropped0));
    }
    window_ = Window{};
    window_.name = name;
    window_.startMs = now;
    window_.cpu0Us = cpuNow;
    window_.decoded0 = counters.decoded;
    window_.dropped0 = counters.dropped;
    window_.late0 = counters.late;
    window_.failures0 = counters.failures;
    window_.rtpLost0 = counters.rtpLost;
    window_.rtpRecovered0 = counters.rtpRecovered;
    window_.rtpDropped0 = counters.rtpDropped;
    on_ = true;
    XC_LOGI("BENCH mark: window \"%s\" starts", name.c_str());
}

// --- runStreamImageTest -----------------------------------------------------

void runStreamImageTest(const media::Picture& pic,
                        const std::function<bool(const media::Picture&, bool)>& drawPic) {
    struct Look {
        const char* name;
        int upscaler, sharpness;  // stream_screen.cpp's applyUpscaler mode, kSharpAmount
        bool legacy8 = false;     // rounded to 8 bits between the passes, no dither (before the 16-bit images)
        bool full = false;        // the whole output saved, not a crop
        bool evenChroma = false;  // colour samples spread evenly (before they followed the brightness edges)
    };
    static constexpr Look kLooks[] = {
        {"1_clean_med", 2, 176}, {"2_fsr_med", 0, 176},  {"3_clean_high", 2, 256}, {"4_clean_low", 2, 96},
        {"5_clean_off", 2, 0},   {"6_fsr_off", 0, 0},    {"7_a4k_med", 1, 176},    {"8_a4k_clean_med", 3, 176},
        {"A_default_8bit", 2, 176, true, true},
        {"B_default_16bit", 2, 176, false, true}, {"C_fsr_8bit", 0, 176, true, true},
        {"D_fsr_16bit", 0, 176, false, true},     {"E_chroma_even", 2, 176, false, true, true},
        {"F_chroma_guided", 2, 176, false, true}, {"9_default", 2, 176},
    };
    for (const Look& look : kLooks) {
        display::setUpscaler(look.upscaler & 1);
        display::setRestore(look.upscaler >= 2);
        display::setSharpness(look.sharpness);
        display::setLegacyPrecision(look.legacy8);
        display::setChromaGuide(!look.evenChroma);
        std::vector<uint8_t> rgb;
        int w = 0, h = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(display::frameMutex());
            if (!drawPic(pic, true)) continue;
            display::present();
            if (!display::readBackRgb(rgb, w, h)) continue;
        }
        if (std::string(look.name) == "9_default") break;
        const int cw = look.full ? w : std::min(1280, w), ch = look.full ? h : std::min(720, h);
        const int x0 = look.full ? 0 : std::min(1100, w - cw), y0 = look.full ? 0 : std::min(300, h - ch);
        std::string data = "P6\n" + std::to_string(cw) + " " + std::to_string(ch) + "\n255\n";
        for (int y = 0; y < ch; ++y)
            data.append(reinterpret_cast<const char*>(rgb.data() + (static_cast<size_t>(y0 + y) * w + x0) * 3),
                        static_cast<size_t>(cw) * 3);
        const std::string path = platform::dataDir() + "/img_" + look.name + ".ppm";
        XC_LOGI("image test: %s (%dx%d of %dx%d) %s", look.name, cw, ch, w, h,
                platform::writeFileAtomic(path, data) ? "saved" : "not saved");
    }
}

}  // namespace xc::app
