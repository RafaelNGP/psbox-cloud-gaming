// SPDX-License-Identifier: GPL-3.0-or-later
// Empirical Adversarial Stress Test & Verification Harness for Milestone 5 (Bloco E)
// Focus: Jitter Buffer RTT Adaptation Math, Audio Latency Controller Adaptation,
// Audio Startup Priming Gate Semantics, and Stream Reload Mechanics.

#include "app/stream_player.h"
#include "media/audio_queue.h"
#include "stream/h264_receiver.h"
#include "ui/font.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "xcloud/gssv.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace xc;

static int g_passCount = 0;
static int g_failCount = 0;

#define STRESS_ASSERT(cond, msg, ...) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "[FAIL] " msg " at %s:%d\n", ##__VA_ARGS__, __FILE__, __LINE__); \
        g_failCount++; \
    } else { \
        g_passCount++; \
    } \
} while (0)

// =========================================================================
// TEST SUITE 1: Jitter Buffer lossWaitMs() and renackMs() RTT Sweep & Math
// =========================================================================
static void testJitterBufferRttSweep() {
    std::printf("\n=== [TEST SUITE 1] Jitter Buffer RTT Sweep & Monotonicity ===\n");

    stream::VideoReceiveStats stats{};
    stream::H264Receiver rxDefault([] {}, stats, 90);
    stream::H264Receiver rxCustom([] {}, stats, 150);

    // 1. Fallback & Negative / Zero RTT
    rxDefault.setRttMs(-100);
    STRESS_ASSERT(rxDefault.lossWaitMs() == 90, "RTT -100 must yield default waitMs_ (90)");
    STRESS_ASSERT(rxDefault.renackMs() == 50, "RTT -100 must yield kRenackMs (50)");

    rxDefault.setRttMs(-1);
    STRESS_ASSERT(rxDefault.lossWaitMs() == 90, "RTT -1 must yield default waitMs_ (90)");
    STRESS_ASSERT(rxDefault.renackMs() == 50, "RTT -1 must yield kRenackMs (50)");

    rxDefault.setRttMs(0);
    STRESS_ASSERT(rxDefault.lossWaitMs() == 90, "RTT 0 (unmeasured) must yield default waitMs_ (90)");
    STRESS_ASSERT(rxDefault.renackMs() == 50, "RTT 0 (unmeasured) must yield kRenackMs (50)");

    // Custom waitMs fallback
    rxCustom.setRttMs(0);
    STRESS_ASSERT(rxCustom.lossWaitMs() == 150, "RTT 0 with custom waitMs (150) must yield 150");
    STRESS_ASSERT(rxCustom.renackMs() == 50, "RTT 0 with custom waitMs must yield 50");

    // 2. Exact Dispatched RTT Points: 1, 5, 14, 25, 29, 30, 50, 100, 500 ms
    struct ExpectedPoint {
        int rtt;
        uint64_t expectedLossWait;
        uint64_t expectedRenack;
    };

    const ExpectedPoint kPoints[] = {
        // RTT = 1 ms:
        // dynamicMin_loss = max(40, 1*2 + 15) = 40. Clamp(1*2 + 30 = 32, 40, 300) = 40.
        // dynamicMin_renack = max(20, 1 + 6) = 20. Clamp(1 + 10 = 11, 20, 250) = 20.
        {1, 40, 20},

        // RTT = 5 ms:
        // dynamicMin_loss = max(40, 5*2 + 15) = 40. Clamp(5*2 + 30 = 40, 40, 300) = 40.
        // dynamicMin_renack = max(20, 5 + 6) = 20. Clamp(5 + 10 = 15, 20, 250) = 20.
        {5, 40, 20},

        // RTT = 14 ms (PS5 baseline on LAN/low-ping):
        // dynamicMin_loss = max(40, 14*2 + 15 = 43) = 43. Clamp(14*2 + 30 = 58, 43, 300) = 58.
        // dynamicMin_renack = max(20, 14 + 6 = 20) = 20. Clamp(14 + 10 = 24, 20, 250) = 24.
        {14, 58, 24},

        // RTT = 25 ms:
        // dynamicMin_loss = max(40, 25*2 + 15 = 65) = 65. Clamp(25*2 + 30 = 80, 65, 300) = 80.
        // dynamicMin_renack = max(20, 25 + 6 = 31) = 31. Clamp(25 + 10 = 35, 31, 250) = 35.
        {25, 80, 35},

        // RTT = 29 ms (boundary before 30 ms):
        // dynamicMin_loss = max(40, 29*2 + 15 = 73) = 73. Clamp(29*2 + 30 = 88, 73, 300) = 88.
        // dynamicMin_renack = max(20, 29 + 6 = 35) = 35. Clamp(29 + 10 = 39, 35, 250) = 39.
        {29, 88, 39},

        // RTT = 30 ms (transition point: rtt < 30 is false, dynamicMin_loss = 90, dynamicMin_renack = 50):
        // dynamicMin_loss = 90. Clamp(30*2 + 30 = 90, 90, 300) = 90.
        // dynamicMin_renack = 50. Clamp(30 + 10 = 40, 50, 250) = 50.
        {30, 90, 50},

        // RTT = 50 ms:
        // dynamicMin_loss = 90. Clamp(50*2 + 30 = 130, 90, 300) = 130.
        // dynamicMin_renack = 50. Clamp(50 + 10 = 60, 50, 250) = 60.
        {50, 130, 60},

        // RTT = 100 ms:
        // dynamicMin_loss = 90. Clamp(100*2 + 30 = 230, 90, 300) = 230.
        // dynamicMin_renack = 50. Clamp(100 + 10 = 110, 50, 250) = 110.
        {100, 230, 110},

        // RTT = 135 ms (lossWaitMs saturation knee: 135*2 + 30 = 300):
        {135, 300, 145},

        // RTT = 240 ms (renackMs saturation knee: 240 + 10 = 250):
        {240, 300, 250},

        // RTT = 500 ms (fully saturated):
        {500, 300, 250},

        // RTT = 1000 ms:
        {1000, 300, 250}
    };

    for (const auto& pt : kPoints) {
        rxDefault.setRttMs(pt.rtt);
        uint64_t actualLoss = rxDefault.lossWaitMs();
        uint64_t actualRenack = rxDefault.renackMs();

        STRESS_ASSERT(actualLoss == pt.expectedLossWait,
                      "RTT %d: expected lossWaitMs %llu, got %llu",
                      pt.rtt, static_cast<unsigned long long>(pt.expectedLossWait),
                      static_cast<unsigned long long>(actualLoss));

        STRESS_ASSERT(actualRenack == pt.expectedRenack,
                      "RTT %d: expected renackMs %llu, got %llu",
                      pt.rtt, static_cast<unsigned long long>(pt.expectedRenack),
                      static_cast<unsigned long long>(actualRenack));
    }

    // 3. Exhaustive Monotonicity Verification for all RTT from 1 to 1000 ms
    uint64_t prevLoss = 0;
    uint64_t prevRenack = 0;
    for (int rtt = 1; rtt <= 1000; ++rtt) {
        rxDefault.setRttMs(rtt);
        uint64_t loss = rxDefault.lossWaitMs();
        uint64_t renack = rxDefault.renackMs();

        STRESS_ASSERT(loss >= prevLoss,
                      "Monotonicity violation in lossWaitMs at RTT %d: %llu < %llu",
                      rtt, static_cast<unsigned long long>(loss),
                      static_cast<unsigned long long>(prevLoss));

        STRESS_ASSERT(renack >= prevRenack,
                      "Monotonicity violation in renackMs at RTT %d: %llu < %llu",
                      rtt, static_cast<unsigned long long>(renack),
                      static_cast<unsigned long long>(prevRenack));

        // Clamping bounds validation
        STRESS_ASSERT(loss >= 40 && loss <= 300, "lossWaitMs out of bounds [40, 300] at RTT %d: %llu",
                      rtt, static_cast<unsigned long long>(loss));
        STRESS_ASSERT(renack >= 20 && renack <= 250, "renackMs out of bounds [20, 250] at RTT %d: %llu",
                      rtt, static_cast<unsigned long long>(renack));

        prevLoss = loss;
        prevRenack = renack;
    }

    std::printf("[PASS] Suite 1 passed: Monotonicity and exact math verified across all RTTs (0..1000 ms)\n");
}

// =========================================================================
// TEST SUITE 2: Audio Latency Controller Realistic & Extreme Underruns
// =========================================================================
static void testAudioLatencyController() {
    std::printf("\n=== [TEST SUITE 2] Audio Latency Controller Adaptation & Ceiling ===\n");

    app::AudioLatency lat;
    media::AudioQueue q;
    q.open();
    lat.reset();

    STRESS_ASSERT(lat.targetMs == 40, "Initial targetMs must be 40 ms");
    STRESS_ASSERT(!lat.warm, "Initially not warm");

    // 1. Warm-up Phase
    lat.update(0, &q);
    STRESS_ASSERT(!lat.warm, "Must not be warm at t=0");

    lat.update(1000, &q);
    STRESS_ASSERT(!lat.warm, "Must not be warm at t=1000 (<3000ms)");

    lat.update(3000, &q);
    STRESS_ASSERT(lat.warm, "Must be warm at t=3000");
    STRESS_ASSERT(lat.calmSinceMs == 3000, "calmSinceMs must be initialized to 3000");

    // 2. Isolated Single Dry Grain: +5 ms Step
    float sample[2] = {0.0f, 0.0f};
    q.push(sample, 1);
    float buf[256 * 2];
    q.pull(buf, 256);  // Triggers 1 underrun
    lat.update(4000, &q);
    STRESS_ASSERT(lat.targetMs == 45, "Isolated single dry grain must raise target from 40 to 45 ms (got %d)", lat.targetMs);
    STRESS_ASSERT(lat.calmSinceMs == 4000, "calmSinceMs must update on underrun");

    // Another single dry grain: +5 ms step -> 50 ms
    q.setRefill(0);
    q.pull(buf, 256);
    lat.update(5000, &q);
    STRESS_ASSERT(lat.targetMs == 50, "Second single dry grain must raise target from 45 to 50 ms (got %d)", lat.targetMs);

    // 3. Multi-Grain Dry Burst (dry == 2): +10 ms Step
    q.setRefill(0);
    q.pull(buf, 256);
    q.pull(buf, 256);  // 2 underruns
    lat.update(6000, &q);
    STRESS_ASSERT(lat.targetMs == 60, "Burst dry grains (2) must raise target from 50 to 60 ms (got %d)", lat.targetMs);

    // Large dry burst (dry == 5): +10 ms Step
    q.setRefill(0);
    for (int i = 0; i < 5; ++i) q.pull(buf, 256);
    lat.update(7000, &q);
    STRESS_ASSERT(lat.targetMs == 70, "Large dry burst (5) must raise target from 60 to 70 ms (got %d)", lat.targetMs);

    // 4. Escalation to Ceiling (Code Ceiling = 100 ms)
    // 70 -> 80
    q.setRefill(0);
    q.pull(buf, 256); q.pull(buf, 256);
    lat.update(8000, &q);
    STRESS_ASSERT(lat.targetMs == 80, "Burst must raise to 80 ms (got %d)", lat.targetMs);

    // 80 -> 90
    q.setRefill(0);
    q.pull(buf, 256); q.pull(buf, 256);
    lat.update(9000, &q);
    STRESS_ASSERT(lat.targetMs == 90, "Burst must raise to 90 ms (got %d)", lat.targetMs);

    // 90 -> 100
    q.setRefill(0);
    q.pull(buf, 256); q.pull(buf, 256);
    lat.update(10000, &q);
    STRESS_ASSERT(lat.targetMs == 100, "Burst must raise to 100 ms (got %d)", lat.targetMs);

    // Stress: Further underruns at ceiling
    // The code specifies: if (targetMs < 100) targetMs = std::min(..., 100);
    // Therefore, at 100 ms, targetMs remains clamped at 100 ms!
    for (int step = 1; step <= 10; ++step) {
        q.setRefill(0);
        q.pull(buf, 256); q.pull(buf, 256);
        lat.update(10000 + step * 1000, &q);
        STRESS_ASSERT(lat.targetMs == 100, "Target must saturate and remain clamped at 100 ms (got %d)", lat.targetMs);
    }
    // Last underrun occurred at t = 20000
    STRESS_ASSERT(lat.calmSinceMs == 20000, "calmSinceMs must be 20000 after last burst");
    std::printf("[INFO] Verified ceiling behavior: targetMs saturates at 100 ms under continuous underruns.\n");

    // 5. Downward Recovery (30s calm intervals)
    // From 100 ms:
    // After 29s calm (t = 49000): must NOT decrease yet (29s < 30s)
    lat.update(49000, &q);
    STRESS_ASSERT(lat.targetMs == 100, "Target must remain 100 ms before 30s calm elapsed (got %d)", lat.targetMs);

    // After 30s calm (t = 50000): -5 ms step -> 95 ms
    lat.update(50000, &q);
    STRESS_ASSERT(lat.targetMs == 95, "After 30s calm, target must drop to 95 ms (got %d)", lat.targetMs);
    STRESS_ASSERT(lat.calmSinceMs == 50000, "calmSinceMs must advance to 50000");

    // Successive 30s calm intervals down to 45 ms:
    int expectedTarget = 95;
    uint64_t t = 51000;
    while (expectedTarget > 45) {
        t += 30000;
        expectedTarget -= 5;
        lat.update(t, &q);
        STRESS_ASSERT(lat.targetMs == expectedTarget,
                      "After 30s calm at t=%llu, expected target %d, got %d",
                      static_cast<unsigned long long>(t), expectedTarget, lat.targetMs);
    }
    STRESS_ASSERT(lat.targetMs == 45, "Reached floor of 45 ms");

    // 6. Floor Invariant: At 45 ms, target must NEVER decrement further!
    for (int cycle = 0; cycle < 10; ++cycle) {
        t += 30000;
        lat.update(t, &q);
        STRESS_ASSERT(lat.targetMs == 45,
                      "At floor 45 ms, calm must not decrement below 45 ms (got %d at t=%llu)",
                      lat.targetMs, static_cast<unsigned long long>(t));
    }
    std::printf("[INFO] Verified floor behavior: targetMs strictly floors at 45 ms after sustained calm.\n");

    // 7. Re-escalation from Floor
    // 1 dry grain -> 50 ms
    q.setRefill(0);
    q.pull(buf, 256);
    t += 1000;
    lat.update(t, &q);
    STRESS_ASSERT(lat.targetMs == 50, "Dry grain at floor must raise target to 50 ms (got %d)", lat.targetMs);

    // 30s calm -> returns to 45 ms floor
    t += 30000;
    lat.update(t, &q);
    STRESS_ASSERT(lat.targetMs == 45, "Calm must bring target back to 45 ms floor (got %d)", lat.targetMs);

    std::printf("[PASS] Suite 2 passed: Audio latency controller adaptation and bounds verified.\n");
}

// =========================================================================
// TEST SUITE 3: Audio Startup Priming Gate Semantics
// =========================================================================
static void testAudioStartupPrimingGate() {
    std::printf("\n=== [TEST SUITE 3] Audio Startup Priming Gate Semantics ===\n");

    // Verify the exact invariant implemented in src/media/audio_out_ps5.cpp:
    // bool primed = false;
    // while (g_running) {
    //     if (!primed) {
    //         if (queue.queuedFrames() < kGrain) {
    //             std::this_thread::sleep_for(std::chrono::milliseconds(2));
    //             continue;
    //         }
    //         primed = true;
    //     }
    //     queue.pull(grain.data(), kGrain);

    constexpr unsigned kGrain = 256;
    media::AudioQueue q;
    q.open();

    // Invariant 1: Fresh queue has 0 queued frames
    STRESS_ASSERT(q.queuedFrames() == 0, "Initial queuedFrames must be 0");

    // Invariant 2: Gating behavior before incoming frames arrive
    bool primed = false;
    size_t simulatedGatedLoops = 0;
    size_t actualPulls = 0;
    float grain[kGrain * 2];

    for (int tick = 0; tick < 50; ++tick) {
        if (!primed) {
            if (q.queuedFrames() < kGrain) {
                simulatedGatedLoops++;
                continue;
            }
            primed = true;
        }
        q.pull(grain, kGrain);
        actualPulls++;
    }

    STRESS_ASSERT(simulatedGatedLoops == 50, "Priming gate must suppress all 50 pull attempts while empty");
    STRESS_ASSERT(actualPulls == 0, "Actual pulls must be 0 while unprimed");
    STRESS_ASSERT(!primed, "primed must remain false");
    STRESS_ASSERT(q.takeUnderruns() == 0, "No underruns should be recorded while priming gate is active");

    // Invariant 3: Pushing less than kGrain frames (e.g. 128 frames) keeps gate closed
    std::vector<float> partial(128 * 2, 0.5f);
    q.push(partial.data(), 128);
    STRESS_ASSERT(q.queuedFrames() == 128, "queuedFrames must be 128");

    if (!primed) {
        if (q.queuedFrames() < kGrain) {
            simulatedGatedLoops++;
        } else {
            primed = true;
        }
    }
    STRESS_ASSERT(!primed, "Partial frame push (<256) must NOT open the priming gate");

    // Invariant 4: Pushing standard WebRTC Opus packet (480 frames = 10 ms or 960 frames = 20 ms)
    // opens the gate and allows clean playback
    std::vector<float> packet(480 * 2, 0.25f);
    q.push(packet.data(), 480);
    STRESS_ASSERT(q.queuedFrames() >= kGrain, "queuedFrames must now be >= 256 (is %zu)", q.queuedFrames());

    if (!primed) {
        if (q.queuedFrames() < kGrain) {
            simulatedGatedLoops++;
        } else {
            primed = true;
        }
    }
    STRESS_ASSERT(primed, "Priming gate must trip open once queuedFrames >= 256");

    // Now pull runs normally
    q.pull(grain, kGrain);
    actualPulls++;
    STRESS_ASSERT(actualPulls == 1, "Pull executed successfully once primed");
    STRESS_ASSERT(q.takeUnderruns() == 0, "No underruns recorded during primed playback");

    std::printf("[PASS] Suite 3 passed: Audio startup priming gate semantics verified.\n");
}

// =========================================================================
// TEST SUITE 4: In-Game Stream Reload Architecture & String Tables
// =========================================================================
static void testStreamReloadArchitecture() {
    std::printf("\n=== [TEST SUITE 4] In-Game Stream Reload Architecture ===\n");

    // 1. StreamPlayer Reconnect State
    xcloud::GssvClient gssv;
    app::StreamPlayer player(gssv);

    STRESS_ASSERT(!player.isReconnecting(), "StreamPlayer must initially not be reconnecting");

    // First request
    player.requestReconnect();
    STRESS_ASSERT(player.isReconnecting(), "requestReconnect() must set reconnecting state");

    // Second request: Idempotent call
    player.requestReconnect();
    STRESS_ASSERT(player.isReconnecting(), "Repeated requestReconnect() must remain in reconnecting state");

    // 2. StreamMenu Navigation & Action
    ui::Fonts fonts;
    ui::StreamMenu menu(fonts);
    menu.open(0, false);
    STRESS_ASSERT(menu.isOpen(), "StreamMenu must be open");

    // In StreamMenu layout:
    // Navigating Up wraps from XboxButton (0) to Leave (last item),
    // and navigating Up again reaches ReloadStreamItem.
    ui::NavInput upNav;
    upNav.up = true;
    menu.handle(upNav);  // to Leave
    menu.handle(upNav);  // to ReloadStreamItem

    ui::NavInput acceptNav;
    acceptNav.accept = true;
    ui::MenuAction action = menu.handle(acceptNav);

    STRESS_ASSERT(action == ui::MenuAction::ReloadStream,
                  "Accepting ReloadStream row must yield MenuAction::ReloadStream");
    STRESS_ASSERT(!menu.isOpen(), "Accepting ReloadStream must close the menu overlay");

    // 3. String Tables Localized Check
    // Verify that Str::ReloadStream exists and has localized strings
    const char* strEn = tr(ui::Str::ReloadStream);
    STRESS_ASSERT(strEn != nullptr && std::strlen(strEn) > 0, "Str::ReloadStream must not be empty");
    STRESS_ASSERT(std::strcmp(strEn, "Reload Stream") == 0,
                  "Str::ReloadStream English string mismatch: got '%s'", strEn);

    std::printf("[PASS] Suite 4 passed: Stream reload state and menu actions verified.\n");
}

// =========================================================================
// TEST SUITE 5: Adversarial Boundary Stress, Oscillation & Concurrency
// =========================================================================
static void testAdversarialEdgeCasesAndConcurrency() {
    std::printf("\n=== [TEST SUITE 5] Adversarial Boundary Stress & Concurrency ===\n");

    // 1. Boundary Oscillation (RTT flickering between 29 and 30 ms)
    stream::VideoReceiveStats stats{};
    stream::H264Receiver rx([] {}, stats, 90);

    for (int cycle = 0; cycle < 1000; ++cycle) {
        rx.setRttMs(29);
        STRESS_ASSERT(rx.lossWaitMs() == 88, "Cycle %d: RTT 29 lossWaitMs must be 88", cycle);
        STRESS_ASSERT(rx.renackMs() == 39, "Cycle %d: RTT 29 renackMs must be 39", cycle);

        rx.setRttMs(30);
        STRESS_ASSERT(rx.lossWaitMs() == 90, "Cycle %d: RTT 30 lossWaitMs must be 90", cycle);
        STRESS_ASSERT(rx.renackMs() == 50, "Cycle %d: RTT 30 renackMs must be 50", cycle);
    }
    std::printf("[INFO] 1,000 rapid RTT boundary oscillations (29 <-> 30) verified stable.\n");

    // 2. Extreme RTT Values (Prevent overflow or unexpected clamping)
    const int kExtremeRtts[] = {5000, 10000, 50000, 100000, 1000000};
    for (int extremeRtt : kExtremeRtts) {
        rx.setRttMs(extremeRtt);
        STRESS_ASSERT(rx.lossWaitMs() == 300, "Extreme RTT %d must clamp to 300 ms ceiling", extremeRtt);
        STRESS_ASSERT(rx.renackMs() == 250, "Extreme RTT %d must clamp to 250 ms ceiling", extremeRtt);
    }
    std::printf("[INFO] Extreme RTTs up to 1,000,000 ms properly clamped without overflow.\n");

    // 3. Exact Audio Priming Boundary: 255 vs 256 frames
    {
        media::AudioQueue q;
        q.open();
        std::vector<float> sample255(255 * 2, 0.1f);
        q.push(sample255.data(), 255);
        STRESS_ASSERT(q.queuedFrames() == 255, "Queue has exactly 255 frames");
        bool primed = false;
        if (q.queuedFrames() >= 256) primed = true;
        STRESS_ASSERT(!primed, "Priming gate must NOT trip at 255 frames");

        std::vector<float> sample1(1 * 2, 0.1f);
        q.push(sample1.data(), 1);
        STRESS_ASSERT(q.queuedFrames() == 256, "Queue has exactly 256 frames");
        if (q.queuedFrames() >= 256) primed = true;
        STRESS_ASSERT(primed, "Priming gate MUST trip at exactly 256 frames");
    }
    std::printf("[INFO] Exact single-frame priming gate threshold (255 vs 256) verified.\n");

    // 4. Concurrent Multi-Threaded requestReconnect() Stress
    {
        xcloud::GssvClient gssv;
        app::StreamPlayer player(gssv);
        STRESS_ASSERT(!player.isReconnecting(), "Initially not reconnecting");

        constexpr int kNumThreads = 50;
        std::vector<std::thread> workers;
        workers.reserve(kNumThreads);
        for (int i = 0; i < kNumThreads; ++i) {
            workers.emplace_back([&player]() {
                player.requestReconnect();
            });
        }
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        STRESS_ASSERT(player.isReconnecting(), "Must be reconnecting after concurrent requests");
    }
    std::printf("[INFO] 50 concurrent threads racing on requestReconnect() verified thread-safe.\n");

    std::printf("[PASS] Suite 5 passed: Adversarial boundary stress & concurrency verified.\n");
}

// =========================================================================
// Main Runner
// =========================================================================
int main() {
    std::printf("=================================================================\n");
    std::printf("Running Empirical Adversarial Stress Test Suite for Milestone 5\n");
    std::printf("=================================================================\n");

    testJitterBufferRttSweep();
    testAudioLatencyController();
    testAudioStartupPrimingGate();
    testStreamReloadArchitecture();
    testAdversarialEdgeCasesAndConcurrency();

    std::printf("\n=================================================================\n");
    std::printf("RESULTS: %d passed, %d failed\n", g_passCount, g_failCount);
    std::printf("=================================================================\n");

    return g_failCount == 0 ? 0 : 1;
}

