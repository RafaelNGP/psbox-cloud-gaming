// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The state the app's threads share (app_state.h).
#include "app/app_state.h"

#include "input/controller.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

namespace xc::app {

std::unique_ptr<ui::AppUi> g_ui;
std::string settingsPath() {
    int32_t uid = input::padUserId(0);
    if (uid > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "/settings_%08x.json", static_cast<unsigned int>(uid));
        std::string userPath = platform::dataDir() + buf;
        // If user-specific settings don't exist yet, check if legacy global settings exist to migrate
        std::string legacy = platform::dataDir() + "/settings.json";
        struct stat st {};
        if (::stat(userPath.c_str(), &st) != 0 && ::stat(legacy.c_str(), &st) == 0) {
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
bool allow1440Locked() { return std::max(g_settings.maxHeightCloud, g_settings.maxHeightHome) >= 1440; }
void refreshSettingsUiLocked() {
    ++g_settingsVersion;
    if (g_ui) g_ui->setSettings(ui::choiceFromSettings(g_settings));
}
std::atomic<int> g_command{kSignIn};
std::atomic<bool> g_cancel{false};
std::mutex g_argMutex;
ui::GameTile g_playTile;
std::mutex g_playerMutex;
StreamPlayer* g_player = nullptr;
std::mutex g_infoMutex;
ui::StreamInfo g_streamInfo;
std::atomic<uint32_t> g_infoSeq{0};
std::atomic<bool> g_playingHome{false};
std::atomic<bool> g_tierPicked{false};
std::atomic<uint64_t> g_xboxButtonUntil{0};
std::atomic<bool> g_restartWanted{false};
std::atomic<bool> g_quit{false};

}  // namespace xc::app
