// SPDX-License-Identifier: GPL-3.0-or-later
// Empirical Adversarial Stress Test & Fuzz Harness for Milestone 3 (Bloco C)
#include "input/controller.h"
#include "input/keyboard_nav.h"
#include "input/pad_usb.h"

#include <cassert>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace xc;
using namespace xc::input;

// Reference oracle for ControllerState merging across 4 slots
static ControllerState referenceMerge4(const ControllerState& s0,
                                       const ControllerState& s1,
                                       const ControllerState& s2,
                                       const ControllerState& s3) {
    ControllerState res = s0;
    const ControllerState* aux[3] = {&s1, &s2, &s3};
    for (int i = 0; i < 3; ++i) {
        const ControllerState& e = *aux[i];
        res.dpadUp |= e.dpadUp;
        res.dpadDown |= e.dpadDown;
        res.dpadLeft |= e.dpadLeft;
        res.dpadRight |= e.dpadRight;
        res.btnA |= e.btnA;
        res.btnB |= e.btnB;
        res.btnX |= e.btnX;
        res.btnY |= e.btnY;
        res.btnL1 |= e.btnL1;
        res.btnR1 |= e.btnR1;
        res.btnL3 |= e.btnL3;
        res.btnR3 |= e.btnR3;
        res.btnOptions |= e.btnOptions;
        res.btnTouchpad |= e.btnTouchpad;
        res.btnNexus |= e.btnNexus;

        auto further = [](float& a, float b) {
            if (std::abs(b) > std::abs(a)) a = b;
        };
        further(res.leftStickX, e.leftStickX);
        further(res.leftStickY, e.leftStickY);
        further(res.rightStickX, e.rightStickX);
        further(res.rightStickY, e.rightStickY);
        res.triggerL2 = std::max(res.triggerL2, e.triggerL2);
        res.triggerR2 = std::max(res.triggerR2, e.triggerR2);
    }
    return res;
}

static void fuzzInputMerging() {
    std::printf("[FUZZ 1] Starting input merging fuzzing across Slot 0, 1, 2, 3...\n");
    auto t0 = std::chrono::steady_clock::now();

    std::mt19937_64 rng(0x1337C0DE5A10ULL);
    std::uniform_int_distribution<int> boolDist(0, 1);
    std::uniform_real_distribution<float> triggerDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> stickDist(-1.0f, 1.0f);

    constexpr int kIterations = 100000;
    int checkedCount = 0;

    for (int iter = 0; iter < kIterations; ++iter) {
        ControllerState slots[4];
        for (int s = 0; s < 4; ++s) {
            slots[s].connected = true;
            slots[s].dpadUp = boolDist(rng);
            slots[s].dpadDown = boolDist(rng);
            slots[s].dpadLeft = boolDist(rng);
            slots[s].dpadRight = boolDist(rng);
            slots[s].btnA = boolDist(rng);
            slots[s].btnB = boolDist(rng);
            slots[s].btnX = boolDist(rng);
            slots[s].btnY = boolDist(rng);
            slots[s].btnL1 = boolDist(rng);
            slots[s].btnR1 = boolDist(rng);
            slots[s].btnL3 = boolDist(rng);
            slots[s].btnR3 = boolDist(rng);
            slots[s].btnOptions = boolDist(rng);
            slots[s].btnTouchpad = boolDist(rng);
            slots[s].btnNexus = boolDist(rng);

            slots[s].triggerL2 = triggerDist(rng);
            slots[s].triggerR2 = triggerDist(rng);

            slots[s].leftStickX = stickDist(rng);
            slots[s].leftStickY = stickDist(rng);
            slots[s].rightStickX = stickDist(rng);
            slots[s].rightStickY = stickDist(rng);
        }

        // Boundary injection at specific iterations
        if (iter % 100 == 0) {
            slots[0].leftStickX = -1.0f;
            slots[1].leftStickX = 1.0f;
            slots[2].leftStickX = 0.0f;
            slots[3].leftStickX = -0.0f;
            slots[0].triggerL2 = 0.0f;
            slots[1].triggerL2 = 1.0f;
        }

        ControllerState merged = slots[0];
        mergePads(merged, slots[1]);
        mergePads(merged, slots[2]);
        mergePads(merged, slots[3]);

        // Range assertions
        assert(merged.leftStickX >= -1.0f && merged.leftStickX <= 1.0f);
        assert(merged.leftStickY >= -1.0f && merged.leftStickY <= 1.0f);
        assert(merged.rightStickX >= -1.0f && merged.rightStickX <= 1.0f);
        assert(merged.rightStickY >= -1.0f && merged.rightStickY <= 1.0f);
        assert(merged.triggerL2 >= 0.0f && merged.triggerL2 <= 1.0f);
        assert(merged.triggerR2 >= 0.0f && merged.triggerR2 <= 1.0f);

        // NaN / Inf assertions
        assert(!std::isnan(merged.leftStickX) && !std::isinf(merged.leftStickX));
        assert(!std::isnan(merged.leftStickY) && !std::isinf(merged.leftStickY));
        assert(!std::isnan(merged.rightStickX) && !std::isinf(merged.rightStickX));
        assert(!std::isnan(merged.rightStickY) && !std::isinf(merged.rightStickY));
        assert(!std::isnan(merged.triggerL2) && !std::isinf(merged.triggerL2));
        assert(!std::isnan(merged.triggerR2) && !std::isinf(merged.triggerR2));

        // Comparison against independent reference oracle
        ControllerState oracle = referenceMerge4(slots[0], slots[1], slots[2], slots[3]);
        assert(merged.dpadUp == oracle.dpadUp);
        assert(merged.dpadDown == oracle.dpadDown);
        assert(merged.dpadLeft == oracle.dpadLeft);
        assert(merged.dpadRight == oracle.dpadRight);
        assert(merged.btnA == oracle.btnA);
        assert(merged.btnB == oracle.btnB);
        assert(merged.btnX == oracle.btnX);
        assert(merged.btnY == oracle.btnY);
        assert(merged.btnL1 == oracle.btnL1);
        assert(merged.btnR1 == oracle.btnR1);
        assert(merged.btnL3 == oracle.btnL3);
        assert(merged.btnR3 == oracle.btnR3);
        assert(merged.btnOptions == oracle.btnOptions);
        assert(merged.btnTouchpad == oracle.btnTouchpad);
        assert(merged.btnNexus == oracle.btnNexus);

        assert(merged.triggerL2 == oracle.triggerL2);
        assert(merged.triggerR2 == oracle.triggerR2);
        assert(merged.leftStickX == oracle.leftStickX);
        assert(merged.leftStickY == oracle.leftStickY);
        assert(merged.rightStickX == oracle.rightStickX);
        assert(merged.rightStickY == oracle.rightStickY);

        ++checkedCount;
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("[FUZZ 1 PASSED] %d iterations verified with zero NaN/Inf or range violations (%.2f ms)\n",
                checkedCount, ms);
}

static void testAdversarialEdgeCases() {
    std::printf("[ADVERSARIAL] Stress-testing numerical and combinatorial edge cases...\n");

    // 1. Denormals and exact boundaries
    ControllerState s0{}, s1{};
    s0.leftStickX = -1.0f;
    s1.leftStickX = 1.0f;
    mergePads(s0, s1);
    // Absolute values are equal: std::abs(1.0f) > std::abs(-1.0f) is false -> preserves s0 (-1.0f)
    assert(s0.leftStickX == -1.0f);

    s0.leftStickX = 1e-38f;
    s1.leftStickX = -2e-38f;
    mergePads(s0, s1);
    assert(s0.leftStickX == -2e-38f);

    // 2. Exact trigger bounds
    s0.triggerL2 = 0.0f;
    s1.triggerL2 = 0.0f;
    mergePads(s0, s1);
    assert(s0.triggerL2 == 0.0f);

    s0.triggerL2 = 1.0f;
    s1.triggerL2 = 0.99999f;
    mergePads(s0, s1);
    assert(s0.triggerL2 == 1.0f);

    // 3. All 15 buttons pressed in Slot 3 alone, propagated to Slot 0
    ControllerState base{};
    ControllerState fullButtons{};
    fullButtons.dpadUp = fullButtons.dpadDown = fullButtons.dpadLeft = fullButtons.dpadRight = true;
    fullButtons.btnA = fullButtons.btnB = fullButtons.btnX = fullButtons.btnY = true;
    fullButtons.btnL1 = fullButtons.btnR1 = fullButtons.btnL3 = fullButtons.btnR3 = true;
    fullButtons.btnOptions = fullButtons.btnTouchpad = fullButtons.btnNexus = true;

    mergePads(base, fullButtons);
    assert(base.dpadUp && base.dpadDown && base.dpadLeft && base.dpadRight);
    assert(base.btnA && base.btnB && base.btnX && base.btnY);
    assert(base.btnL1 && base.btnR1 && base.btnL3 && base.btnR3);
    assert(base.btnOptions && base.btnTouchpad && base.btnNexus);

    // 4. Repeated idempotent merges
    for (int i = 0; i < 100; ++i) {
        mergePads(base, fullButtons);
    }
    assert(base.dpadUp && base.btnA && base.btnNexus);

    std::printf("[ADVERSARIAL PASSED] Edge cases and idempotent properties verified.\n");
}

static void fuzzUsbProtocolExhaustive() {
    std::printf("[FUZZ 2] Starting exhaustive fuzzing of usbPadProtocolFor (16,777,216 combinations)...\n");
    auto t0 = std::chrono::steady_clock::now();

    uint64_t countNone = 0;
    uint64_t countDS4 = 0;
    uint64_t countXInput = 0;
    uint64_t countGip = 0;

    for (int cls = 0; cls <= 0xFF; ++cls) {
        for (int sub = 0; sub <= 0xFF; ++sub) {
            for (int proto = 0; proto <= 0xFF; ++proto) {
                uint8_t ifaceClass = static_cast<uint8_t>(cls);
                uint8_t ifaceSubclass = static_cast<uint8_t>(sub);
                uint8_t ifaceProtocol = static_cast<uint8_t>(proto);

                UsbPadProtocol result = usbPadProtocolFor(ifaceClass, ifaceSubclass, ifaceProtocol);

                if (ifaceClass == 0x03) {
                    if (ifaceSubclass == 0x01 && (ifaceProtocol == 0x01 || ifaceProtocol == 0x02)) {
                        assert(result == UsbPadProtocol::None);
                        ++countNone;
                    } else {
                        assert(result == UsbPadProtocol::DualShock4);
                        ++countDS4;
                    }
                } else if (ifaceClass == 0xFF) {
                    if (ifaceSubclass == 0x5D && (ifaceProtocol == 0x01 || ifaceProtocol == 0x81 || ifaceProtocol == 0x00)) {
                        assert(result == UsbPadProtocol::XInput);
                        ++countXInput;
                    } else if (ifaceSubclass == 0x47 && ifaceProtocol == 0xD0) {
                        assert(result == UsbPadProtocol::Gip);
                        ++countGip;
                    } else {
                        assert(result == UsbPadProtocol::None);
                        ++countNone;
                    }
                } else {
                    assert(result == UsbPadProtocol::None);
                    ++countNone;
                }
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Verify exact combinatorial partition
    // Total = 256 * 256 * 256 = 16,777,216
    // Class 0x03: 256 * 256 = 65,536 total.
    // Boot keyboard (0x01, 0x01) and boot mouse (0x01, 0x02) = 2.
    // DualShock4 = 65,536 - 2 = 65,534.
    // Class 0xFF:
    // Subclass 0x5D with proto 0x01, 0x81, 0x00 = 3 (XInput).
    // Subclass 0x47 with proto 0xD0 = 1 (GIP).
    // None in Class 0xFF = 65,536 - 4 = 65,532.
    // Other 254 classes: 254 * 65,536 = 16,646,144 (all None).
    // Total None = 2 + 65,532 + 16,646,144 = 16,711,678.
    assert(countDS4 == 65534);
    assert(countXInput == 3);
    assert(countGip == 1);
    assert(countNone == 16711678);
    assert(countDS4 + countXInput + countGip + countNone == 16777216ULL);

    std::printf("[FUZZ 2 PASSED] Exhaustively verified 16,777,216 protocol combinations (%.2f ms)\n", ms);
    std::printf("  - DualShock4: %lu (expected 65534)\n", countDS4);
    std::printf("  - XInput:     %lu (expected 3)\n", countXInput);
    std::printf("  - Gip:        %lu (expected 1)\n", countGip);
    std::printf("  - None:       %lu (expected 16711678, includes boot KBM exclusions)\n", countNone);
}

int main() {
    std::printf("=================================================================\n");
    std::printf("Milestone 3 (Bloco C) Empirical Fuzz & Adversarial Stress Harness\n");
    std::printf("=================================================================\n");

    fuzzInputMerging();
    testAdversarialEdgeCases();
    fuzzUsbProtocolExhaustive();

    std::printf("=================================================================\n");
    std::printf("ALL EMPIRICAL ADVERSARIAL STRESS TESTS PASSED SUCCESSFULLY!\n");
    std::printf("=================================================================\n");
    return 0;
}
