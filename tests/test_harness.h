// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdio>
#include <mutex>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#define getpid _getpid
#undef near
#undef far
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace xc::app {
extern std::mutex g_settingsMutex;
}

extern int g_failures;

#define CHECK(cond)                                                                               \
    do {                                                                                          \
        if (!(cond)) {                                                                            \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);         \
            ++g_failures;                                                                         \
        }                                                                                         \
    } while (0)

void runCoreTests();
void runAuthTests();
void runXcloudTests();
void runStreamTests();
void runMediaTests();
void runInputTests();
void runUiTests();
void runWebUiTests();
void runAppTests();
