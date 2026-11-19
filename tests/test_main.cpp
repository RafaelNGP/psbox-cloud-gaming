// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Offline unit test runner for the portable core (no network).
#include "test_harness.h"

int g_failures = 0;

int main() {
    runCoreTests();
    runAuthTests();
    runXcloudTests();
    runStreamTests();
    runMediaTests();
    runInputTests();
    runUiTests();
    runWebUiTests();
    runAppTests();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
