// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "app/worker_internal.h"

#include "app/autoplay.h"
#include "app/crash_guard.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "app/web_server.h"
#include "display/display.h"
#include "input/controller.h"
#include "input/gyro.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "media/hw_decoder.h"
#include "platform/platform.h"
#include "stream/net_diag.h"
#include "stream/wifi_filler.h"
#include "ui/cloud_time.h"
#include "ui/strings.h"
#include "util/log.h"
#include "xcloud/catalog.h"
#include "xcloud/regions.h"
#include "trophy/trophy_sync.h"
#include "trophy/trophy_registry.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <vector>

namespace xc::app {

namespace {

std::string formatWait(int seconds) {
    if (seconds < 0) return "...";
    if (seconds < 60) return std::to_string(seconds) + " s";
    return std::to_string((seconds + 59) / 60) + " min";
}

}  // namespace

void playQueueReadyAlert(bool notifyToast) {
    // 1. DualSense rumble pulse
    input::setRumble(180, 180, 500, 0);

    // 2. Synthesize a pleasant 3-tone harmonic chime (E5 -> G#5 -> B5, E Major triad)
    constexpr int kRate = 48000;
    constexpr float kPi = 3.14159265358979323846f;
    struct Tone {
        float freq;
        int ms;
    };
    const Tone tones[3] = {
        {659.25f, 100}, // E5
        {830.61f, 100}, // G#5
        {987.77f, 220}  // B5
    };

    std::vector<float> pcm;
    pcm.reserve(kRate * 2);
    for (int t = 0; t < 3; ++t) {
        int frames = (kRate * tones[t].ms) / 1000;
        float freq = tones[t].freq;
        float decayRate = (t == 2) ? 6.0f : 4.0f;
        for (int i = 0; i < frames; ++i) {
            float sec = static_cast<float>(i) / static_cast<float>(kRate);
            float attack = std::min(1.0f, sec / 0.005f);
            float decay = std::exp(-decayRate * sec);
            float env = attack * decay * 0.70f;
            float s = std::sin(2.0f * kPi * freq * sec) + 0.25f * std::sin(4.0f * kPi * freq * sec);
            float sample = s * env;
            pcm.push_back(sample); // Left
            pcm.push_back(sample); // Right
        }
    }

    // Play directly through the DualSense controller speaker (falls back to main audio)
    media::playPadSpeakerAudio(pcm.data(), pcm.size() / 2);

    // 3. System notification toast (only when stream queue actually finishes)
    if (notifyToast) {
        platform::notify(ui::tr(ui::Str::QueueReadyAlert));
    }
}

namespace {

// Streams until the player ends or the user leaves; returns a status line.
// The cloud title being played, for crash markers (set by play()).
std::string g_stageTitle;
std::string g_stageGameName;
std::string g_stageProduct;
std::string g_stageXboxTitleId;
std::string g_stageTileUrl;

std::string stream(xcloud::GssvClient& gssv, const auth::CloudTimeQuery& cloudQuery) {
    crash::setStage(gssv.isHome() ? "streaming own Xbox" : ("streaming " + g_stageTitle).c_str());
    g_gameClosedOnServer = false;
    app::StreamPlayer player(gssv);
    if (g_autoplay.dump) player.dumpVideo(platform::dataDir() + "/stream.aus", g_autoplay.seconds + 15);
    player.setDecodeThreads(g_autoplay.decodeThreads);
    player.setSimulatedLoss(g_autoplay.videoLoss);
    player.setHwDecodeProbe(g_autoplay.hwDecode);
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        bool hasCustom = false;
        GameProfile prof = g_settings.profileForGame(g_stageProduct, g_stageTitle, &hasCustom);
        bool preferHw = true;
        if (g_autoplay.gyroLevel > 0) g_settings.gyroLevel = std::clamp(g_autoplay.gyroLevel, 1, 10);  // the settings are guarded under autoplay
        if (g_autoplay.gyroCurve == "fortnite") g_settings.gyroCurveStart = 15, g_settings.gyroCurveExp = 24;
        if (g_autoplay.swDecode) {
            preferHw = false;
            // The in-game menu shows the decoder forced here (not the saved
            // one), so a test switches it from the menu (autoplay "bench").
            if (!hasCustom) {
                g_settings.videoDecoder = 1;
            } else if (auto it = g_settings.perGame.find(gameProfileKey(g_stageProduct, g_stageTitle));
                       it != g_settings.perGame.end()) {
                it->second.videoDecoder = 1;
            }
        } else if (g_autoplay.preferHwDecode) {
            preferHw = (hasCustom ? (prof.videoDecoder == 0) : (g_settings.videoDecoder == 0));
        } else {
            preferHw = false;
        }
        player.setPreferHwDecode(preferHw);
        XC_LOGI("stream: video decoder %s", preferHw ? "hardware preferred" : "software (CPU)");
        player.setMicGain(g_settings.micGainFactor());
        player.setMicEnabled(g_settings.micEnabled);
        int fpsLim = hasCustom ? prof.fpsLimit : g_settings.fpsLimit;
        player.setFpsLimit(fpsLim);
        if (fpsLim > 0) XC_LOGI("stream: FPS limit %d FPS", fpsLim);
        else XC_LOGI("stream: FPS limit unlimited");
        player.setLegacyTuning(g_autoplay.legacy);
        player.setTierSequence(g_autoplay.tierSeq);

        int cap = g_autoplay.bitrateMbps > 0 ? g_autoplay.bitrateMbps : g_settings.maxBitrateMbps;
        player.setMaxBitrate(cap);
        media::audioSetVolume(g_settings.gameVolume / 100.0f);
        XC_LOGI("stream: bitrate cap %s, game volume %d%%",
                (cap == -1 || cap >= 100) ? "unlimited" : cap ? (std::to_string(cap) + " Mbps").c_str() : "automatic",
                g_settings.gameVolume);
        media::audioSetRoute(static_cast<media::AudioRoute>(g_settings.audioRoute));
        media::audioSetVoiceRouting(g_settings.voiceRouting);
        int hapticLvl = hasCustom ? prof.audioHaptics : g_settings.audioHaptics;
        media::setAudioHapticsLevel(hapticLvl);
    }
    std::string err;
    if (!player.start(err)) return "ERROR: " + ui::trf(ui::Str::StreamFailed, err);
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = &player;
    }
    g_ui->showStreaming();
    {
        // The touchpad's gestures, in the first three streams only.
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        if (g_settings.gestureHints < 3) {
            ++g_settings.gestureHints;
            g_settings.save(settingsPath());
            // (Without a touchpad: the buttons and keys that do the same.)
            platform::notify(ui::tr(g_ui->caps().touchpad ? ui::Str::GestureHint : ui::Str::GestureHintButtons));
        }
    }

    const uint64_t started = platform::nowMs();
    uint64_t nextTick = started;
    // The account's cloud time (a limited account's, in the cloud): counted
    // down for the game menu, a notice at 15 and at 5 minutes left (Xbox
    // ends the stream at none, issue #342), and the service asked again every
    // 10 minutes from a thread of its own (the loop never waits on it).
    ui::CloudTimeClock cloud;
    const bool cloudTest = g_autoplay.cloudLeft >= 0;
    if (!gssv.isHome()) cloud.start(cloudTest ? g_autoplay.cloudLeft : s_playtimeRemainingSeconds, started);
    if (cloud.limited()) XC_LOGI("cloud time: %s left at the start", ui::formatCloudTime(cloud.left(started)).c_str());
    struct CloudAsk {
        platform::Thread thread;
        std::atomic<bool> done{false};
        std::atomic<int64_t> remaining{-1};
    } ask;
    const uint64_t kAskEveryMs = cloudTest ? 20000 : 600000;
    uint64_t nextAsk = started + kAskEveryMs;
    auto cloudTimeStep = [&](uint64_t nowMs) {
        if (!cloud.limited()) return;
        if (const int64_t due = cloud.noticeDue(nowMs); due > 0)
            platform::notify(ui::trf(ui::Str::CloudTimeWarn, ui::formatCloudTime(due)),
                             "cloud time: " + ui::formatCloudTime(due) + " left (notification)");
        if (ask.done) {
            ask.thread.join();
            ask.done = false;
            const int64_t counted = cloud.left(nowMs);
            cloud.answer(ask.remaining, nowMs);
            XC_LOGI("cloud time: the service says %s, counted %s: %s left",
                    ask.remaining < 0 ? "no limit or nothing" : ui::formatCloudTime(ask.remaining).c_str(),
                    ui::formatCloudTime(counted).c_str(), ui::formatCloudTime(cloud.left(nowMs)).c_str());
        }
        if (!ask.thread.joinable() && nowMs >= nextAsk && !cloudQuery.marketplace.token.empty()) {
            nextAsk = nowMs + kAskEveryMs;
            platform::startThread(ask.thread, [&ask, cloudQuery] {
                auth::ContentAccessPlaytime t;
                ask.remaining = auth::queryCloudTime(cloudQuery, t, 8000) ? t.accountRemaining : -1;
                ask.done = true;
            }, 1u << 20);
        }
    };
    bool snapshot1 = false, snapshot2 = false, snapshot3 = false, snapshot4 = false;
    uint64_t nextPeriodicShot = 150000;
    int periodicShotIdx = 5;
    bool autoplayDone = false;
    bool benchMove = false;  // autoplay "bench": the camera turning (move.req)
    int bestRtt = -1;  // lowest round trip seen: the region's latency
    int tallest = 0;   // the tallest picture seen (the probe's measure)
    // The connection's state for the warning and the end-of-session summary:
    // a second is bad when the jitter buffer dropped a frame (lost for good),
    // no video arrived at all, or the round trip went far above the best seen.
    // Not the frame rate: games send fewer frames in some scenes.
    uint64_t degradedUntil = 0, lastTickAt = 0;
    bool degradedLogged = false;
    uint32_t reconnectsSeen = 0;
    struct {
        uint64_t seconds = 0, badSeconds = 0, kbps = 0, rttSum = 0, rttCount = 0, holds = 0;
    } session;
    // Packets held and delivered at once (VideoReceiveStats::holds): when, over the last 2 minutes;
    // three of them, and each new one is shown for a few seconds (most likely the Wi-Fi).
    std::vector<uint64_t> holdTimes;
    uint64_t wifiHoldUntil = 0;
    // The Wi-Fi filler (stream/wifi_filler.h): armed for the session once such freezes are seen
    // (2 in 2 minutes; a wired console never has them), it tops the stream up to ~600 packets a
    // second while it is light. Its A/B (netdiag's abfiller=) logged as it turns.
    bool wifiFlaky = false;
    int fillerAb = -1;
    uint64_t prevPlaytime = 0;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        prevPlaytime = g_settings.playtimeForGame(g_stageProduct, g_stageTitle);
    }
    bool probeDone = !g_probing;
    auto recordProbe = [&] {
        probeDone = true;
        if (tallest <= 0) return;
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        (gssv.isHome() ? g_settings.maxHeightHome : g_settings.maxHeightCloud) = tallest;
        (gssv.isHome() ? g_settings.probedHome : g_settings.probedCloud) = auth::unixNow();
        g_settings.save(settingsPath());
        g_ui->setAllow1440(allow1440Locked());
        XC_LOGI("resolution probe (%s): %dp at most", gssv.isHome() ? "own Xbox" : "cloud", tallest);
    };
    app::StreamPlayer::Stats last{};
    uint64_t nextSnapCheck = 0, nextRequestCheck = 0;
    // A watch on this loop (issue #317: a PS4 stream froze for good with no
    // line logged, the "no video" check never firing): past 5 s without a
    // pass, which step it is stuck in goes into the log, once a stall.
    struct LoopWatch {
        std::atomic<const char*> step{"start"};
        std::atomic<uint64_t> beatMs{platform::nowMs()};
        std::atomic<bool> stop{false};
        platform::Thread thread;
        void run() {
            bool told = false;
            while (!stop) {
                for (int i = 0; i < 10 && !stop; ++i) platform::sleepMs(100);
                const uint64_t idle = platform::nowMs() - beatMs.load();
                if (idle > 5000 && !told) {
                    XC_LOGW("stream loop: stuck for %llu ms in \"%s\"", static_cast<unsigned long long>(idle), step.load());
                    told = true;
                } else if (idle <= 5000 && told) {
                    XC_LOGW("stream loop: going again after \"%s\"", step.load());
                    told = false;
                }
            }
        }
    } watch;
    platform::startThread(watch.thread, [&watch] { watch.run(); }, 64u << 10);
    auto beat = [&watch](const char* step) {
        watch.step = step;
        watch.beatMs = platform::nowMs();
    };
    // A/B tests in one stream (autoplay "abcycle=N"): every N s the next
    // condition, so all of them share the queue wait, the game and the network.
    struct AbCondition {
        const char* name;
        bool mailbox, hardware, restore;
    };
    static constexpr AbCondition kAb[] = {{"new", true, true, true},
                                          {"fifo", false, true, true},
                                          {"sw", true, false, true},
                                          {"old", false, false, true},
                                          {"norestore", true, true, false}};
    int abIndex = -1;
    bool abHardware = true;
    uint64_t abNext = started + 20000;
    // abimage: the colour samples following the brightness and the overlay left alone while
    // hidden ("image-new") against both as before ("image-old"), in turns.
    uint64_t abImageNext = started + 15000;
    bool abImageNew = false;
    uint64_t afkNext = 0;
    int afkStep = 0;
    stream::wifiFillerStart();  // finds the router to ask; idle until a rate is set
    while (player.running() && !g_cancel) {
        beat("loop");
        // netdiag: R1 and, 3 s later, L1 (back to the same tab) every N
        // seconds, so an idle lobby is not dropped; logged, to leave those
        // moments out of the analysis.
        if (stream::netdiag::enabled() && stream::netdiag::afkSeconds() > 0) {
            const uint64_t t = platform::nowMs();
            const uint64_t every = static_cast<uint64_t>(stream::netdiag::afkSeconds()) * 1000;
            if (!afkNext) afkNext = t + every;
            if (t >= afkNext) {
                input::ControllerState tap{};
                tap.btnR1 = afkStep == 0;
                tap.btnL1 = afkStep == 2;
                setVirtualGamepad(0, tap);  // steps 0 and 2 press, 1 and 3 let go
                if (afkStep == 0 || afkStep == 2) XC_LOGI("netdiag: anti-AFK %s", afkStep == 0 ? "R1" : "L1");
                afkStep = (afkStep + 1) % 4;
                afkNext = t + (afkStep == 1 || afkStep == 3 ? 200 : afkStep == 2 ? 2800 : std::max<uint64_t>(every, 4200) - 3200);
            }
        }
        uint64_t elapsed = platform::nowMs() - started;
        if (g_autoplay.abCycleSec > 0 && started + elapsed >= abNext) {
            abIndex = (abIndex + 1) % static_cast<int>(std::size(kAb));
            const AbCondition& c = kAb[abIndex];
            display::setLowLatencyPresent(c.mailbox);
            display::setRestore(c.restore);
            if (c.hardware != abHardware || abIndex == 0) player.switchDecoder(c.hardware);
            abHardware = c.hardware;
            abNext = started + elapsed + static_cast<uint64_t>(g_autoplay.abCycleSec) * 1000u;
            XC_LOGI("AUTOPLAY AB: condition %s", c.name);
        }
        if (g_autoplay.abImageSec > 0 && started + elapsed >= abImageNext) {
            abImageNew = !abImageNew;
            display::setChromaGuide(abImageNew);
            display::setOverlayRefreshAlways(!abImageNew);
            player.markBenchWindow(abImageNew ? "image-new" : "image-old");
            abImageNext = started + elapsed + static_cast<uint64_t>(g_autoplay.abImageSec) * 1000u;
        }
        beat("the cloud time");
        cloudTimeStep(started + elapsed);
        // Asked for from outside, in any mode (files in the data folder, checked every 500 ms):
        // snap.req, the picture now (now.ppm); rec.req, the game's picture to rec.h264 for its
        // seconds (300 if it says none), ended by rec.stop or the time (StreamPlayer::startRecording).
        if (const uint64_t at = platform::nowMs() - started; at >= nextRequestCheck) {
            nextRequestCheck = at + 500;
            std::string text;
            const std::string snap = platform::dataDir() + "/snap.req";
            if (platform::readFile(snap, text)) {
                std::remove(snap.c_str());
                player.requestSnapshot(platform::dataDir() + "/now.ppm");
            }
            const std::string rec = platform::dataDir() + "/rec.req";
            if (platform::readFile(rec, text)) {
                std::remove(rec.c_str());
                const int secs = std::atoi(text.c_str());
                player.startRecording(secs > 0 ? secs : 300);
            }
            const std::string recStop = platform::dataDir() + "/rec.stop";
            if (platform::readFile(recStop, text) || player.recordingExpired()) {
                std::remove(recStop.c_str());
                player.stopRecording();
            }
        }
        if (!g_autoplay.title.empty()) {
            if (elapsed >= static_cast<uint64_t>(g_autoplay.seconds) * 1000u) {
                autoplayDone = true;
                break;
            }
            if (!snapshot1 && !g_autoplay.bench && elapsed >= 10000) {
                snapshot1 = true;
                player.requestSnapshot(platform::dataDir() + "/frame.ppm");
            }
            bool press = !g_autoplay.idle &&
                         ((elapsed >= 22000 && elapsed < 22300) ||
                          (elapsed >= 26000 && elapsed < 26300) ||
                          (elapsed >= 70000 && (elapsed % 6000 < 300)));
            if (g_autoplay.fnPlay) {
                // Fortnite: nothing pressed for 150 s (the match is reached by
                // hand, through the web gamepad, as Fortnite's lobby and its
                // pop-ups change from one session to the next); then the
                // camera keeps turning and the player runs for 4 s of every 10
                // and jumps: motion like a game's, the same each run.
                press = false;
                const bool moving = elapsed >= 150000;
                const float t = static_cast<float>(elapsed % 8000) / 8000.0f;
                g_syntheticRightX = moving ? 0.7f * std::sin(t * 6.2831853f) : 0.0f;
                g_syntheticLeftY = moving && elapsed % 10000 < 4000 ? -1.0f : 0.0f;
                if (moving && elapsed % 10000 >= 4000 && elapsed % 10000 < 4200) press = true;
            }
            if (press != g_syntheticA.exchange(press)) XC_LOGI("autoplay: A %s", press ? "down" : "up");
            if (g_autoplay.bench) {
                // The same slow camera turn in every measured window (the
                // right stick only: the player stays put), so the picture
                // keeps moving as in a game; off while the menus are used.
                const float t = static_cast<float>(elapsed % 8000) / 8000.0f;
                g_syntheticRightX = benchMove ? 0.6f * std::sin(t * 6.2831853f) : 0.0f;
            }
            static bool dropped = false;
            if (g_autoplay.dropTest && !dropped && elapsed >= 20000) {
                dropped = true;
                player.simulateDrop();
            }
            if (!snapshot2 && !g_autoplay.bench && elapsed >= 28000) {
                snapshot2 = true;
                player.requestSnapshot(platform::dataDir() + "/frame2.ppm");
            }
            if (!snapshot3 && !g_autoplay.bench && elapsed >= 70000) {
                snapshot3 = true;
                player.requestSnapshot(platform::dataDir() + "/frame3.ppm");
            }
            if (!snapshot4 && !g_autoplay.bench && elapsed >= 110000) {
                snapshot4 = true;
                player.requestSnapshot(platform::dataDir() + "/frame4.ppm");
            }
            static bool imageTested = false;
            if (g_autoplay.imgTest && !imageTested && elapsed + 25000 >= static_cast<uint64_t>(g_autoplay.seconds) * 1000u) {
                imageTested = true;
                player.requestImageTest();
            }
            if (elapsed >= nextSnapCheck) {
                nextSnapCheck = elapsed + 500;
                std::string unused;
                // img.req: the image test now, on this picture (autoplay "bench").
                const std::string img = platform::dataDir() + "/img.req";
                if (g_autoplay.bench && platform::readFile(img, unused)) {
                    std::remove(img.c_str());
                    player.requestImageTest();
                    XC_LOGI("BENCH image test asked");
                }
                // gyro.req ("N": seconds): pad 0's motion sensors recorded, then
                // written to gyro.rec (autoplay "bench").
                const std::string gyroReq = platform::dataDir() + "/gyro.req";
                std::string gyroSecs;
                if (g_autoplay.bench && platform::readFile(gyroReq, gyroSecs)) {
                    std::remove(gyroReq.c_str());
                    const int secs = std::max(1, std::atoi(gyroSecs.c_str()));
                    input::startGyroRecording(secs);
                    player.startMotionRecording(secs);
                    XC_LOGI("BENCH gyro recording for %d s", secs);
                }
                // sweep.req: the right stick swept level by level with L2 down
                // (sweepStick), the picture's motion recorded meanwhile.
                const std::string sweepReq = platform::dataDir() + "/sweep.req";
                std::string sweepOpts;
                if (g_autoplay.bench && platform::readFile(sweepReq, sweepOpts)) {
                    std::remove(sweepReq.c_str());
                    g_sweepL2 = sweepOpts.find("nol2") == std::string::npos;
                    player.setMotionRawDump(sweepOpts.find("raw") != std::string::npos);
                    player.setMotionStick(sweepStick);
                    player.startMotionRecording(64);
                    input::startGyroRecording(64);
                    g_sweepStartUs = platform::nowUs() + 500000;
                    XC_LOGI("BENCH stick sweep starts");
                }
                std::vector<app::StreamPlayer::MotionSample> motionRec;
                if (g_autoplay.bench && player.motionTake(motionRec)) {
                    std::string data(reinterpret_cast<const char*>(motionRec.data()), motionRec.size() * sizeof(app::StreamPlayer::MotionSample));
                    const bool ok = platform::writeFileAtomic(platform::dataDir() + "/motion.rec", data);
                    XC_LOGI("BENCH motion recording: %zu frames of %zu bytes %s", motionRec.size(),
                            sizeof(app::StreamPlayer::MotionSample), ok ? "saved" : "not saved");
                }
                std::string motionRaw;
                if (g_autoplay.bench && player.motionRawTake(motionRaw)) {
                    const bool ok = platform::writeFileAtomic(platform::dataDir() + "/motion_raw.rec", motionRaw);
                    XC_LOGI("BENCH motion raw dump: %zu bytes %s", motionRaw.size(), ok ? "saved" : "not saved");
                }
                std::vector<input::GyroSample> gyroRec;
                if (g_autoplay.bench && input::gyroRecordingTake(gyroRec)) {
                    std::string data(reinterpret_cast<const char*>(gyroRec.data()), gyroRec.size() * sizeof(input::GyroSample));
                    const bool ok = platform::writeFileAtomic(platform::dataDir() + "/gyro.rec", data);
                    XC_LOGI("BENCH gyro recording: %zu samples of %zu bytes %s", gyroRec.size(), sizeof(input::GyroSample),
                            ok ? "saved" : "not saved");
                }
                // A measurement window marked from outside (the file mark.req,
                // its text the new window's name): the last one is logged. An
                // empty file is one still being written (FTP creates it first):
                // left for the next look.
                const std::string mark = platform::dataDir() + "/mark.req";
                std::string name;
                if (platform::readFile(mark, name)) {
                    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
                    if (!name.empty()) {
                        std::remove(mark.c_str());
                        player.markBenchWindow(name);
                    }
                }
                // move.req: "1" turns the camera, "0" stops it.
                const std::string move = platform::dataDir() + "/move.req";
                std::string on;
                if (g_autoplay.bench && platform::readFile(move, on) && !on.empty() && (on[0] == '0' || on[0] == '1')) {
                    std::remove(move.c_str());
                    benchMove = on[0] == '1';
                    XC_LOGI("BENCH camera turning %s", benchMove ? "on" : "off");
                }
                // hwcfg.req: "pipe=N queue=N prio=N aff=0xN opt=N" (any of them;
                // the others back to the shipped setup), then the hardware
                // decoder made again with it.
                const std::string hwcfg = platform::dataDir() + "/hwcfg.req";
                std::string cfg;
                if (g_autoplay.bench && platform::readFile(hwcfg, cfg) && !cfg.empty()) {
                    std::remove(hwcfg.c_str());
                    media::HwDecoder::Tuning t{};
                    bool zeroCopy = true;  // "zc=0": the picture copied by the CPU, as before
                    size_t pos = 0;
                    while (pos < cfg.size()) {
                        size_t end = cfg.find_first_of(" \t\r\n", pos);
                        if (end == std::string::npos) end = cfg.size();
                        const std::string kv = cfg.substr(pos, end - pos);
                        pos = end + 1;
                        const size_t eq = kv.find('=');
                        if (eq == std::string::npos) continue;
                        const std::string k = kv.substr(0, eq);
                        const long long v = std::strtoll(kv.c_str() + eq + 1, nullptr, 0);
                        if (k == "pipe") t.pipe = static_cast<int>(v);
                        else if (k == "queue") t.queue = static_cast<int>(v);
                        else if (k == "prio") t.cpuPriority = static_cast<int>(v);
                        else if (k == "aff") t.cpuAffinity = static_cast<uint64_t>(v);
                        else if (k == "opt") t.optimizeProgressive = static_cast<int>(v);
                        else if (k == "res") t.resourceType = static_cast<int>(v);
                        else if (k == "zc") zeroCopy = v != 0;
                        else if (k == "sw" && v == 1) t.resourceType = -1;  // the software decoder (FFmpeg) instead
                    }
                    XC_LOGI("BENCH hwdec config \"%s\"", cfg.c_str());
                    media::HwDecoder::output().enabled = zeroCopy;
                    if (t.resourceType == -1) {
                        player.switchDecoder(false);
                    } else {
                        media::HwDecoder::tuning() = t;
                        player.recreateHwDecoder();
                    }
                }
            }
            if (!g_autoplay.bench && elapsed >= nextPeriodicShot) {
                nextPeriodicShot = elapsed + 30000;
                player.requestSnapshot(platform::dataDir() + "/frame" + std::to_string(periodicShotIdx++) + ".ppm");
            }
        }
        if (platform::nowMs() >= nextTick) {
            uint64_t tickNow = platform::nowMs();
            double dt = lastTickAt ? (tickNow - lastTickAt) / 1000.0 : 1.0;
            nextTick = tickNow + 1000;
            beat("player.tick (reconnects, keyframe requests)");
            player.tick();
            beat("player.stats (counters, round trip)");
            auto st = player.stats();
            beat("the stats line");
            XC_LOGI("stream: %llu frames, %llu decoded, %llu skipped, %llu failed, %llu resets, %llu kf req, "
                    "%llu queued, %llu audio; rtp %llu pkts, %llu lost, %llu recovered, %llu nacks, "
                    "%llu frames dropped; %llu kbps (remb %llu); %llu rumble; decode %.1f/%.1f ms (%s), draw %.1f/%.1f ms, %llu late; "
                    "rtt %d ms; %dx%d; on screen %.1f/%.1f ms after arrival; qp %.1f; packets %llu ms apart at most, "
                    "%llu frames at once, a frame %llu ms late; first packet to screen %.1f/%.1f ms",
                    static_cast<unsigned long long>(st.videoFrames), static_cast<unsigned long long>(st.decodedFrames),
                    static_cast<unsigned long long>(st.droppedFrames), static_cast<unsigned long long>(st.decodeFailures),
                    static_cast<unsigned long long>(st.queueResets), static_cast<unsigned long long>(st.keyframeRequests),
                    static_cast<unsigned long long>(st.queued), static_cast<unsigned long long>(st.audioPackets),
                    static_cast<unsigned long long>(st.rtpPackets), static_cast<unsigned long long>(st.rtpLost),
                    static_cast<unsigned long long>(st.rtpRecovered), static_cast<unsigned long long>(st.rtpNacks),
                    static_cast<unsigned long long>(st.rtpDroppedFrames), static_cast<unsigned long long>(st.rtpKbps),
                    static_cast<unsigned long long>(st.rembKbps), static_cast<unsigned long long>(st.vibrations),
                    st.decodeAvgUs / 1000.0, st.decodeMaxUs / 1000.0, st.isHwDecode ? "HW" : "SW",
                    st.drawAvgUs / 1000.0, st.drawMaxUs / 1000.0,
                    static_cast<unsigned long long>(st.lateFrames), st.rttMs, st.width, st.height,
                    st.displayAvgUs / 1000.0, st.displayMaxUs / 1000.0, st.qpAvg,
                    static_cast<unsigned long long>(st.rtpGapMs), static_cast<unsigned long long>(st.rtpBurst),
                    static_cast<unsigned long long>(st.rtpLateMs), st.glassAvgUs / 1000.0, st.glassMaxUs / 1000.0);
            if (st.rttMs > 0 && (bestRtt < 0 || st.rttMs < bestRtt)) bestRtt = st.rttMs;
            tallest = std::max(tallest, st.height);
            if (!probeDone && platform::nowMs() - started >= 20000) {
                recordProbe();
                if (!g_afterProbeAlias.empty() && !g_tierPicked) player.requestResolution(g_afterProbeAlias);
            }
            {
                ui::StreamInfo info;
                info.region = ui::prettyRegion(gssv.region().name);
                info.rttMs = st.rttMs;
                info.fps = dt > 0.05
                               ? std::max(0.0, static_cast<double>(st.decodedFrames - last.decodedFrames -
                                                                   (st.droppedFrames - last.droppedFrames)) /
                                                   dt)
                               : 60.0;
                info.mbps = st.rtpKbps / 1000.0;
                uint64_t lost = st.rtpLost - last.rtpLost, got = st.rtpPackets - last.rtpPackets;
                info.lossPct = got + lost ? 100.0 * lost / (got + lost) : 0;
                info.decodeMs = st.decodeAvgUs / 1000.0;
                info.isHwDecode = st.isHwDecode;
                info.onScreenMs = st.displayAvgUs / 1000.0;
                info.width = st.width;
                info.height = st.height;
                info.qp = st.qpAvg;
                if (last.decodedFrames > 0) {  // once the picture is running
                    uint64_t dropped = st.rtpDroppedFrames - last.rtpDroppedFrames;
                    uint64_t now = platform::nowMs();
                    // No video for a second; or the loop itself held up (a
                    // reconnection blocks it) / a new session after a drop.
                    bool stalled = st.videoFrames == last.videoFrames || (lastTickAt && now - lastTickAt > 2500) ||
                                   player.reconnects() != reconnectsSeen;
                    reconnectsSeen = player.reconnects();
                    bool slow = st.rttMs > 0 && bestRtt > 0 && st.rttMs > bestRtt + 60;
                    if (dropped || stalled || slow) {
                        if (!degradedLogged)  // once per episode
                            XC_LOGW("connection degraded: %llu frame(s) lost, %s, rtt %d ms (best %d)",
                                    static_cast<unsigned long long>(dropped), stalled ? "no video for a second" : "video arriving",
                                    st.rttMs, bestRtt);
                        degradedLogged = true;
                        degradedUntil = now + 3000;
                        ++session.badSeconds;
                    } else if (now >= degradedUntil) {
                        degradedLogged = false;
                    }
                    info.degraded = now < degradedUntil;
                    const uint64_t newHolds = st.rtpHolds >= last.rtpHolds ? st.rtpHolds - last.rtpHolds : st.rtpHolds;
                    for (uint64_t k = 0; k < newHolds; ++k) holdTimes.push_back(now);
                    holdTimes.erase(std::remove_if(holdTimes.begin(), holdTimes.end(),
                                                   [&](uint64_t t) { return now - t > 120000; }),
                                    holdTimes.end());
                    if (newHolds) {
                        session.holds += newHolds;
                        XC_LOGI("network: packets held %llu ms and then delivered at once (most likely the Wi-Fi); %zu in the last 2 min",
                                static_cast<unsigned long long>(st.rtpHoldMs), holdTimes.size());
                        if (holdTimes.size() >= 3) wifiHoldUntil = now + 4000;
                    }
                    info.wifiHolds = static_cast<int>(holdTimes.size());
                    info.wifiHoldNow = now < wifiHoldUntil;
                    if (!wifiFlaky && holdTimes.size() >= 2) {
                        wifiFlaky = true;
                        XC_LOGI("wifi filler: armed (packets held by the Wi-Fi %zu times in 2 min)", holdTimes.size());
                    }
                    const uint64_t videoPps = st.rtpPackets >= last.rtpPackets ? st.rtpPackets - last.rtpPackets : 0;
                    const uint64_t audioPps = st.audioPackets >= last.audioPackets ? st.audioPackets - last.audioPackets : 0;
                    const uint64_t pps = videoPps + audioPps;
                    int fill = wifiFlaky && pps < 600 ? std::clamp(600 - static_cast<int>(pps), 100, stream::kWifiFillerMax) : 0;
                    if (stream::netdiag::enabled() && stream::netdiag::abFillerSeconds() > 0) {
                        const int on = (now / (static_cast<uint64_t>(stream::netdiag::abFillerSeconds()) * 1000)) % 2 == 0;
                        if (on != fillerAb) XC_LOGI("wifi filler A/B: %s", on ? "filler-on" : "filler-off");
                        fillerAb = on;
                        if (!on) fill = 0;
                    }
                    stream::wifiFillerSetRate(fill);
                    lastTickAt = tickNow;
                    ++session.seconds;
                    session.kbps += st.rtpKbps;
                    if (st.rttMs > 0) session.rttSum += static_cast<uint64_t>(st.rttMs), ++session.rttCount;
                }
                info.cloudTimeSeconds = cloud.left(platform::nowMs());
                info.playtimeSeconds = prevPlaytime + session.seconds;
                player.getRecentFrametimes(info.frametimes);
                StreamTelemetry tel;
                tel.decodePeakMs = st.decodeMaxUs / 1000.0;
                tel.drawMs = st.drawAvgUs / 1000.0, tel.drawPeakMs = st.drawMaxUs / 1000.0;
                tel.onScreenPeakMs = st.displayMaxUs / 1000.0;
                tel.glassMs = st.glassAvgUs / 1000.0, tel.glassPeakMs = st.glassMaxUs / 1000.0;
                tel.skippedNow = st.droppedFrames - last.droppedFrames, tel.skippedTotal = st.droppedFrames;
                tel.lateFrames = st.lateFrames, tel.decodeFailures = st.decodeFailures, tel.keyframeRequests = st.keyframeRequests;
                tel.rtpLost = st.rtpLost, tel.rtpRecovered = st.rtpRecovered, tel.rtpNacks = st.rtpNacks;
                tel.rtpDroppedFrames = st.rtpDroppedFrames;
                tel.gapMs = st.rtpGapMs, tel.burst = st.rtpBurst, tel.frameLateMs = st.rtpLateMs, tel.rembKbps = st.rembKbps;
                tel.bestRttMs = bestRtt, tel.reconnects = static_cast<int>(player.reconnects());
                tel.seconds = static_cast<uint64_t>(session.seconds), tel.badSeconds = static_cast<uint64_t>(session.badSeconds);
                std::lock_guard<std::mutex> lock(g_infoMutex);
                g_streamInfo = info;
                g_streamTelemetry = tel;
                ++g_infoSeq;
            }
            last = st;
        }
        beat("sleep");
        platform::sleepMs(100);
    }
    stream::wifiFillerStop();
    watch.stop = true;
    if (watch.thread.joinable()) watch.thread.join();
    if (ask.thread.joinable()) ask.thread.join();  // 8 s at most
    if (cloud.limited()) {
        if (ask.done) cloud.answer(ask.remaining, platform::nowMs());
        if (!cloudTest) s_playtimeRemainingSeconds = cloud.left(platform::nowMs());
        XC_LOGI("cloud time: %s left at the end", ui::formatCloudTime(cloud.left(platform::nowMs())).c_str());
    }
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        g_player = nullptr;
    }
    if (!probeDone && player.stats().decodedFrames > 0) recordProbe();  // a short stream still measured
    std::string reason = g_cancel ? "left the game" : autoplayDone ? "autoplay finished" : player.endReason();
    player.stop();
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        media::setAudioHapticsLevel(g_settings.audioHaptics);
    }
    if (session.seconds >= 5 && (!g_stageTitle.empty() || !g_stageProduct.empty())) {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        uint64_t newTotal = prevPlaytime + session.seconds;
        if (!g_stageTitle.empty()) g_settings.titlePlaytime[g_stageTitle] = newTotal;
        if (!g_stageProduct.empty()) g_settings.titlePlaytime[g_stageProduct] = newTotal;
        g_settings.save(settingsPath());
        if (g_ui) g_ui->setSettings(ui::choiceFromSettings(g_settings));
        XC_LOGI("playtime: updated for %s (%s) to %llu seconds (%s)", g_stageTitle.c_str(),
                g_stageProduct.c_str(), static_cast<unsigned long long>(newTotal),
                ui::formatPlaytime(newTotal).c_str());

        std::shared_ptr<app::Library> lib;
        {
            std::lock_guard<std::mutex> lockLib(s_libraryMutex);
            lib = s_library;
        }
        if (lib) {
            if (!g_stageProduct.empty()) lib->recordRecent(g_stageProduct);
            else if (!g_stageTitle.empty()) lib->recordRecent(g_stageTitle);
            lib->setPlaytime(g_settings.titlePlaytime);
            g_ui->setRows(lib->rows());
            g_ui->setOwned(lib->owned(), lib->purchasable(), lib->ownedKnown());
        }
    }
    if (bestRtt > 0) {
        // Remembered per region: Settings shows it, the region fallback uses it.
        std::map<std::string, int> rtt;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            g_settings.regionRtt[gssv.region().name] = bestRtt;
            g_settings.save(settingsPath());
            rtt = g_settings.regionRtt;
        }
        XC_LOGI("region %s: %d ms round trip", gssv.region().name.c_str(), bestRtt);
        g_ui->setRegionLatency(rtt);
    }
    auto st = player.stats();
    if (session.seconds >= 10) {
        // The session in one line: the log (and so a report) and a notification.
        double lossPct = st.rtpPackets + st.rtpLost ? 100.0 * (st.rtpLost - std::min(st.rtpRecovered, st.rtpLost)) /
                                                          static_cast<double>(st.rtpPackets + st.rtpLost)
                                                    : 0;
        char details[160];
        std::snprintf(details, sizeof details, "%llu min, %dp, %.1f Mbps, %.2f%% lost, %d ms",
                      static_cast<unsigned long long>((session.seconds + 30) / 60), tallest,
                      session.kbps / 1000.0 / static_cast<double>(session.seconds), lossPct,
                      session.rttCount ? static_cast<int>(session.rttSum / session.rttCount) : -1);
        XC_LOGI("session summary: %s; %llu s with a degraded connection, %llu frames lost, %llu recovered packets, "
                "%llu network holds",
                details, static_cast<unsigned long long>(session.badSeconds),
                static_cast<unsigned long long>(st.rtpDroppedFrames), static_cast<unsigned long long>(st.rtpRecovered),
                static_cast<unsigned long long>(session.holds));
    }

    // Check trophies/achievements on stream exit (outside of gameplay to guarantee zero stutters)
    if (!g_stageXboxTitleId.empty()) {
        bool syncTrophies = false;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            syncTrophies = g_settings.syncTrophies;
            if (syncTrophies) {
                bool hasCustom = false;
                GameProfile prof = g_settings.profileForGame(g_stageProduct, g_stageTitle, &hasCustom);
                if (hasCustom && !prof.syncTrophies) {
                    syncTrophies = false;
                }
            }
        }
        if (syncTrophies) {
            std::string xblAuth, xuid;
            {
                std::lock_guard<std::mutex> lock(g_argMutex);
                xblAuth = g_xblAuth;
                xuid = g_xuid;
            }
            if (!xblAuth.empty()) {
                XC_LOGI("trophy: checking trophies on stream exit for %s", g_stageXboxTitleId.c_str());
                trophy::dispatchStreamPoll(xblAuth, xuid, g_stageXboxTitleId,
                                           g_stageGameName.empty() ? g_stageTitle : g_stageGameName,
                                           g_stageTileUrl);
            }
        }
    }

    // An abnormal stream end (connection lost, WebRTC failure, decoder failure):
    // report it as an error so the user sees the error screen and can report or retry.
    bool normalEnd = g_cancel || autoplayDone || reason == "stopped" ||
                     reason.find("KickForStopCommand") != std::string::npos ||
                     reason.find("KickByNewSession") != std::string::npos ||
                     reason.find("KickForServerShutdown") != std::string::npos ||
                     reason.find("KickForBeingIdle") != std::string::npos;
    if (!normalEnd) {
        g_gameClosedOnServer = reason.rfind("the game closed on the server", 0) == 0;
        return "ERROR: " + ui::trf(ui::Str::StreamFailed, reason);
    }
    return "Stream ended (" + reason + "), " + std::to_string(st.decodedFrames) + " frames shown";
}

// Queue -> /connect -> Provisioned -> stream. Returns a status line; sets
// `failed` when the user should see an error rather than the home screen.
// `regionName` overrides the settings' region (empty: as set).

}  // namespace

std::string play(auth::AuthManager& am, xcloud::GssvClient& gssv, const ui::GameTile& game, bool& failed,
                 const std::string& regionName) {
    failed = true;
    // A home session's id is the console's: only its start in the log.
    XC_LOGI("starting %s (%s)", gssv.isHome() ? "own Xbox" : game.name.c_str(),  // not the console's name
            gssv.isHome() ? (game.titleId.substr(0, 4) + "...").c_str() : game.titleId.c_str());
    g_ui->showLaunching(game, regionName.empty() ? ui::tr(ui::Str::Connecting)
                                                 : ui::trf(ui::Str::TryingRegion, ui::prettyRegion(regionName)));
    std::string err;
    // A login about to expire is renewed first (its region kept); if that
    // fails the start is tried all the same.
    if (!keepSignedIn(am, gssv, err)) XC_LOGW("sign-in renewal: %s", err.c_str());
    err.clear();
    std::string streamLocale = ui::gameLocale();
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        bool hasCustom = false;
        GameProfile prof = g_settings.profileForGame(game.productId, game.titleId, &hasCustom);
        if (!prof.locale.empty()) streamLocale = prof.locale;
        std::string res;
        if (!g_autoplay.resolution.empty()) {
            res = g_autoplay.resolution;
        } else if (hasCustom) {
            res = prof.resolution == 1 ? "720p" : prof.resolution == 2 ? "best" : "1080p";
        } else {
            res = g_settings.resolution;
        }
        bool isBest = (res == "best" || res == "1440p");
        // "Best" asks for the top tier every time, nothing measured or
        // remembered: the service gives what the plan and the game allow
        // (1440p where it can, else 1080p). The user's own Xbox always gets
        // the top tier (it sends 1080p either way, at ~16 Mbps instead of ~9.5).
        bool home = gssv.isHome();
        g_probing = false;
        g_afterProbeAlias.clear();
        g_tierPicked = false;
        xcloud::Resolution tier;
        if (!g_autoplay.resolution.empty())
            tier = res == "720p"       ? xcloud::Resolution::P720
                   : res == "1440p"    ? xcloud::Resolution::P1440
                   : res == "1080p-hq" ? xcloud::Resolution::P1080HQ
                   : isBest            ? (allow1440Locked() ? xcloud::Resolution::P1440 : xcloud::Resolution::P1080HQ)
                                       : xcloud::Resolution::P1080HQ;
        else if (res == "720p")
            tier = xcloud::Resolution::P720;
        else if (home)
            tier = xcloud::Resolution::P1440;
        else if (res == "1440p" || (isBest && allow1440Locked()))
            tier = xcloud::Resolution::P1440;
        else
            tier = xcloud::Resolution::P1080HQ;
        gssv.setResolution(tier);
        gssv.setPreviewFeatures(g_settings.previewFeatures);
        const xcloud::Region* region = gssv.session().defaultRegion();
        const std::string& wanted = regionName.empty() ? g_settings.region : regionName;
        for (const auto& r : gssv.session().regions)
            if (r.name == wanted) region = &r;
        if (region) gssv.setRegion(*region);
        XC_LOGI("stream settings: %s%s, region %s, locale %s", res.c_str(),
                hasCustom ? " (per-game)" : "", region ? region->name.c_str() : "?", streamLocale.c_str());
    }
    // For a crash marker: what is being started (a console's id stays out).
    g_stageTitle = game.titleId;
    g_stageGameName = game.name;
    g_stageProduct = game.productId;
    g_stageXboxTitleId = game.xboxTitleId;
    g_stageTileUrl = game.tileUrl;
    if (g_stageXboxTitleId.empty() && !game.productId.empty()) {
        if (const auto* m = trophy::findKnownTitle("", game.productId)) {
            g_stageXboxTitleId = m->xboxTitleId;
            if (g_stageGameName.empty()) g_stageGameName = m->titleName;
        }
    }
    crash::setStage(gssv.isHome() ? "starting own Xbox" : ("starting " + game.titleId).c_str());
    if (std::string res = platform::diagnostics(); !res.empty()) XC_LOGI("resources before the stream: %s", res.c_str());
    int status = 0;
    bool started = gssv.startSession(game.titleId, streamLocale, err, &status);
    if (!started && status == 401 && !g_cancel) {
        // The token refused before its time: renewed once, asked again.
        XC_LOGW("%s; renewing the sign-in and asking again", err.c_str());
        std::string renewErr;
        if (keepSignedIn(am, gssv, renewErr, true)) started = gssv.startSession(game.titleId, streamLocale, err, &status);
        else XC_LOGW("sign-in renewal: %s", renewErr.c_str());
    }
    if (!started) {
        if (err.find("0x87DD043A") != std::string::npos || err.find("MonthlyLimitExceeded") != std::string::npos) {
            return std::string(ui::tr(ui::Str::MonthlyLimitExceeded));
        }
        return err;
    }
    if (game.heroUrl.empty() && !game.productId.empty()) {
        // Picked before the background hydration reached it: fetch the hero
        // art for the loading screen while the session queues.
        std::map<std::string, xcloud::Product> full;
        std::string e;
        std::string storeMarket;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            storeMarket = g_settings.storeMarket(gssv.session().market);
        }
        if (xcloud::fetchProducts({game.productId}, storeMarket,
                                  ui::catalogLanguage(), full, e, true) &&
            full.count(game.productId)) {
            ui::GameTile withArt = game;
            withArt.heroUrl = full[game.productId].heroUrl;
            XC_LOGI("launch art: %s", withArt.heroUrl.empty() ? "(none)" : withArt.heroUrl.c_str());
            g_ui->showLaunching(withArt, ui::tr(ui::Str::Connecting));
        }
    }
    bool connected = false;
    bool wasInQueue = false;
    std::string result = "cancelled";  // the loop below ends otherwise with its own reason
    // A console that is off for good (not asleep) never comes out of
    // Provisioning: give up after a minute (the cloud's queue may take long).
    const uint64_t provisioningSince = platform::nowMs();
    int pollErrors = 0;
    // The queue's estimate: the title's (the service has no per-session one),
    // asked every 15 s and counted down in between; -1 unknown.
    int queueSeconds = -1;
    uint64_t queueAskedAt = 0;
    uint64_t queueStartedAt = 0;
    // Waited for as long as it takes: only the player (Cancel), the service (a failed session, or
    // one it no longer knows) or an own Xbox that does not wake end it. Before, 900 polls (~18
    // minutes): a long queue (past 40 minutes at busy times) was given up while the service still
    // held the place, and the place lost.
    while (!g_cancel) {
        if (gssv.isHome() && platform::nowMs() - provisioningSince > 60000) {
            result = "ConsoleDidNotWake: no answer in 60 s";
            break;
        }
        xcloud::SessionStatus st;
        if (!gssv.sessionState(st, err)) {
            // A network hiccup (a TLS timeout) is asked again; three in a row end it.
            if (++pollErrors < 3) {
                XC_LOGW("%s (asking again)", err.c_str());
                platform::sleepMs(1000);
                continue;
            }
            result = err;
            break;
        }
        pollErrors = 0;
        XC_LOGI("session state: %s", st.raw.c_str());
        if (st.state == xcloud::SessionState::WaitingForResources) {
            wasInQueue = true;
            uint64_t now = platform::nowMs();
            if (!queueStartedAt) queueStartedAt = now;
            int elapsedSec = static_cast<int>((now - queueStartedAt) / 1000);
            if (!queueAskedAt || now - queueAskedAt >= 15000) {
                int s = gssv.titleWaitSeconds(game.titleId);
                XC_LOGI("queue: estimated %d s (elapsed %d s)", s, elapsedSec);
                if (s >= 0 || queueSeconds < 0) queueSeconds = s;
                queueAskedAt = now;
            }
            bool indeterminate = (queueSeconds < 0) || (queueSeconds <= 15 && elapsedSec >= 15);
            if (indeterminate) {
                g_ui->setLaunchStatus(ui::trf(ui::Str::InQueueIndeterminate, formatWait(elapsedSec)));
            } else {
                int left = std::max(0, queueSeconds - static_cast<int>((now - queueAskedAt) / 1000));
                g_ui->setLaunchStatus(ui::trf(ui::Str::InQueueEstimated, formatWait(left), formatWait(elapsedSec)));
            }
        }
        if (st.state == xcloud::SessionState::Provisioning && gssv.isHome())
            g_ui->setLaunchStatus(ui::tr(ui::Str::WakingConsole));  // a sleeping Xbox takes ~10 s
        if (st.state == xcloud::SessionState::Failed) {
            if (st.errorCode == "0x87DD043A" || st.errorCode == "MonthlyLimitExceeded" ||
                st.raw.find("0x87DD043A") != std::string::npos || st.raw.find("MonthlyLimitExceeded") != std::string::npos) {
                result = ui::tr(ui::Str::MonthlyLimitExceeded);
            } else {
                result = "the session failed: " + st.errorCode + " " + st.errorMessage;
            }
            break;
        }
        if (st.state == xcloud::SessionState::ReadyToConnect && !connected) {
            std::string transfer;
            if (!am.consoleTransferToken(transfer, err) || !gssv.connect(transfer, err)) {
                result = err;
                break;
            }
            connected = true;
        }
        if (st.state == xcloud::SessionState::Provisioned) {
            bool alertOn = false;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                alertOn = g_settings.queueReadyAlert;
            }
            if (alertOn && (wasInQueue || (platform::nowMs() - provisioningSince >= 2000))) {
                XC_LOGI("session: queue completed and provisioned; triggering queue-ready chime and rumble on DualSense");
                playQueueReadyAlert(true);
            }
            g_ui->setLaunchStatus(ui::tr(ui::Str::StartingStream));
            result = stream(gssv, am.cloudTimeQuery());
            failed = result.rfind("ERROR", 0) == 0;
            break;
        }
        platform::sleepMs(1000);
    }
    if (g_cancel && failed) {
        result = "cancelled";
        failed = false;
    }
    gssv.stopSession();
    return result;
}

// The name of the game being played (the stream screen's dialogs show it).
std::string stageGameName() { return g_stageGameName; }

}  // namespace xc::app
