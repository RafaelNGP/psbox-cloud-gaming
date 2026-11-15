// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// psbox, the desktop app (SDL3): the app core (app_core.cpp) with the SDL
// display, input and sound backends and shell_desktop.cpp. SDL_main.h gives
// Windows its GUI entry point (M2); elsewhere this is main().
#include "app/app_core.h"
#include "app/desktop_selftest.h"
#include "display/display_sdl.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void usage(std::FILE* to) {
    std::fprintf(to,
                 "usage: psbox [--data-dir <dir>] [--fullscreen | --windowed] [--selftest <dir>]\n"
                 "  --data-dir <dir>   sign-in, settings and caches there (default $XCLOUD_DATA_DIR,\n"
                 "                     else $XDG_DATA_HOME/psbox)\n"
                 "  --fullscreen       start fullscreen this time (F11 or Alt+Enter switches)\n"
                 "  --windowed         start in a window this time (default: Settings > Display mode,\n"
                 "                     fullscreen at first)\n"
                 "  --selftest <dir>   check the window, screens, input and sound; screenshots in <dir>\n");
}

}  // namespace

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_iostream.h>
#include "util/fs.h"
#include <sstream>

static void ensureAndroidAssets() {
    const char* internalStorage = SDL_GetAndroidInternalStoragePath();
    if (!internalStorage || !*internalStorage) return;
    std::string dataDir = internalStorage;
    SDL_setenv_unsafe("XCLOUD_DATA_DIR", dataDir.c_str(), 1);

    std::string assetsDir = dataDir + "/assets";
    SDL_setenv_unsafe("XCLOUD_ASSETS", assetsDir.c_str(), 1);
    SDL_setenv_unsafe("XCLOUD_CA_BUNDLE", (assetsDir + "/cacert.pem").c_str(), 1);

    SDL_IOStream* listIo = SDL_IOFromFile("asset-list.txt", "r");
    if (!listIo) return;

    Sint64 sz = SDL_GetIOSize(listIo);
    std::string apkList;
    if (sz > 0) {
        apkList.resize(static_cast<size_t>(sz));
        SDL_ReadIO(listIo, &apkList[0], static_cast<size_t>(sz));
    }
    SDL_CloseIO(listIo);

    std::string targetListPath = assetsDir + "/asset-list.txt";
    std::string currentList;
    if (xc::fs::exists(targetListPath)) {
        if (FILE* f = std::fopen(targetListPath.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            long len = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (len > 0) {
                currentList.resize(static_cast<size_t>(len));
                std::fread(&currentList[0], 1, static_cast<size_t>(len), f);
            }
            std::fclose(f);
        }
    }
    if (!apkList.empty() && apkList == currentList) return;

    xc::fs::makeDirs(assetsDir);
    std::istringstream iss(apkList);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.back() == '\r') line.pop_back();

        SDL_IOStream* in = SDL_IOFromFile(line.c_str(), "rb");
        if (!in) continue;
        std::string outPath = assetsDir + "/" + line;
        size_t lastSlash = outPath.rfind('/');
        if (lastSlash != std::string::npos) {
            xc::fs::makeDirs(outPath.substr(0, lastSlash));
        }
        if (FILE* out = std::fopen(outPath.c_str(), "wb")) {
            char buf[8192];
            size_t bytes;
            while ((bytes = SDL_ReadIO(in, buf, sizeof(buf))) > 0) {
                std::fwrite(buf, 1, bytes, out);
            }
            std::fclose(out);
        }
        SDL_CloseIO(in);
    }
    if (FILE* f = std::fopen(targetListPath.c_str(), "wb")) {
        std::fwrite(apkList.data(), 1, apkList.size(), f);
        std::fclose(f);
    }
}
#endif

int main(int argc, char* argv[]) {
#if defined(__ANDROID__)
    ensureAndroidAssets();
#endif
    std::string selftest;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        bool hasValue = i + 1 < argc;
        if (!std::strcmp(a, "--data-dir") && hasValue) {
            SDL_setenv_unsafe("XCLOUD_DATA_DIR", argv[++i], 1);  // platform::dataDir() reads it
        } else if (!std::strcmp(a, "--fullscreen")) {
            xc::display::sdlStartFullscreen(true);
        } else if (!std::strcmp(a, "--windowed")) {
            xc::display::sdlStartFullscreen(false);
        } else if (!std::strcmp(a, "--selftest") && hasValue) {
            selftest = argv[++i];
        } else if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else {
            std::fprintf(stderr, "psbox: unknown argument '%s'\n", a);
            usage(stderr);
            return 2;
        }
    }
    int code = selftest.empty() ? xc::app::runApp() : xc::app::desktopSelftest(selftest);
    // Not a return from main: the worker's threads may still run, and the
    // static destructors would free what they use (as on the PS5).
    std::fflush(nullptr);
    std::_Exit(code);
}

#if defined(_WIN32)
#if defined(UNICODE) && UNICODE
extern "C" int __stdcall WinMain(void*, void*, void*, int) {
    return SDL_RunApp(0, nullptr, SDL_main, nullptr);
}
#endif
#endif
