// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The state the app's threads share (app_state.h).
#include <cstdint>
#include "app/app_state.h"

#include "input/controller.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace xc::app {

std::unique_ptr<ui::AppUi> g_ui;
static bool anyUserSettings() {
    DIR* d = ::opendir(platform::dataDir().c_str());
    if (!d) return false;
    bool found = false;
    while (struct dirent* de = ::readdir(d)) {
        const std::string n = de->d_name;
        if (n.rfind("settings_", 0) == 0 && n.size() > 14 && n.substr(n.size() - 5) == ".json") {
            found = true;
            break;
        }
    }
    ::closedir(d);
    return found;
}

std::string settingsPath() {
    int32_t uid = input::padUserId(0);
    if (uid > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "/settings_%08x.json", static_cast<unsigned int>(uid));
        std::string userPath = platform::dataDir() + buf;
        // If user-specific settings don't exist yet, check if legacy global settings exist to migrate
        std::string legacy = platform::dataDir() + "/settings.json";
        struct stat st {};
        // Only the first console user ever gets the settings from before they were per user: a
        // later one starts from the defaults (hidden games, per-game settings, favourites and play
        // time are each user's own).
        if (::stat(userPath.c_str(), &st) != 0 && ::stat(legacy.c_str(), &st) == 0 && !anyUserSettings()) {
            std::string content;
            if (platform::readFile(legacy, content)) {
                platform::writeFileAtomic(userPath, content);
                XC_LOGI("settings: migrated %s -> %s for user 0x%08x", legacy.c_str(), userPath.c_str(), static_cast<unsigned int>(uid));
            }
        }
        return userPath;
    }
    return platform::dataDir() + "/settings.json";
}
std::string userFilePath(const char* base, const char* legacyName) {
    const std::string legacy = platform::dataDir() + "/" + legacyName;
    const int32_t uid = input::padUserId(0);
    if (uid <= 0) return legacy;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "/%s_%08x.json", base, static_cast<unsigned int>(uid));
    const std::string path = platform::dataDir() + buf;
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0 && ::stat(legacy.c_str(), &st) == 0) {
        // Moved, not copied: the next profile to open the app must not find it.
        if (std::rename(legacy.c_str(), path.c_str()) == 0)
            XC_LOGI("user files: %s is now %s (user 0x%08x)", legacyName, buf + 1, static_cast<unsigned int>(uid));
    }
    return path;
}
bool allow1440Locked() { return std::max(g_settings.maxHeightCloud, g_settings.maxHeightHome) >= 1440; }
void refreshSettingsUiLocked() {
    ++g_settingsVersion;
    if (g_ui) g_ui->setSettings(ui::choiceFromSettings(g_settings));
}
std::atomic<int> g_command{kSignIn};
std::atomic<uint64_t> g_libraryRetryAt{0};

void libraryRefreshEnded(bool incomplete) {
    static std::atomic<int> tries{0};
    if (!incomplete) {
        if (tries.exchange(0)) XC_LOGI("library: complete again");
        g_libraryRetryAt = 0;
        return;
    }
    static const uint64_t kWaitMs[] = {30000, 60000, 120000, 300000};
    if (g_libraryRetryAt.load()) return;  // already said, already scheduled (the list and the details both report)
    int n = tries.fetch_add(1);
    if (n >= static_cast<int>(sizeof kWaitMs / sizeof kWaitMs[0])) {
        XC_LOGW("library: still incomplete after %d tries; the next one is the user's own refresh", n);
        return;
    }
    g_libraryRetryAt = platform::nowMs() + kWaitMs[n];
    XC_LOGW("library: incomplete; trying again in %llu s (%d)", static_cast<unsigned long long>(kWaitMs[n] / 1000), n + 1);
    platform::notify(ui::tr(ui::Str::LibraryIncomplete));
}
std::atomic<bool> g_playPending{false};
std::atomic<bool> g_cancel{false};
std::mutex g_argMutex;
ui::GameTile g_playTile;
std::mutex g_playerMutex;
StreamPlayer* g_player = nullptr;
std::mutex g_infoMutex;
ui::StreamInfo g_streamInfo;
StreamTelemetry g_streamTelemetry;
std::atomic<uint32_t> g_infoSeq{0};
std::atomic<bool> g_playingHome{false};
std::atomic<bool> g_tierPicked{false};
std::atomic<uint64_t> g_xboxButtonUntil{0};
std::atomic<bool> g_restartWanted{false};
std::atomic<bool> g_quit{false};

}  // namespace xc::app
