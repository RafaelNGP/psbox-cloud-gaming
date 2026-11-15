// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Stub implementation of trophy interfaces for non-console platforms (Linux, Windows, Android).
#include "trophy/trophy_sync.h"
#include "trophy/trophy_registry.h"
#include "trophy/trophy_backend.h"
#include "trophy/trophy_rules.h"

namespace xc::trophy {

void startTrophyThread() {}
bool stopTrophyThread(uint32_t /*timeoutMs*/) { return true; }
void resumeTrophySync() {}
void requestTrophySync(bool /*forceFetchIcons*/) {}
void requestTrophySync(const std::string& /*xblAuth*/, const std::string& /*xuid*/,
                       const std::vector<ui::GameTile>& /*ownedGames*/,
                       bool /*forceFetchIcons*/) {}
void cancelTrophySync() {}
void requestRemoveTrophies() {}
bool dispatchStreamPoll(const std::string& /*xblAuth*/, const std::string& /*xuid*/,
                        const std::string& /*xboxTitleId*/, const std::string& /*titleName*/,
                        const std::string& /*tileUrl*/) {
    return false;
}

bool isSyncing() { return false; }
int titlesDone() { return 0; }
int titlesTotal() { return 0; }

void setTrophyContextProvider(TrophyContextProvider /*provider*/) {}
void setTrophyUserProvider(std::function<uint32_t()> /*provider*/) {}

void runPsboxNativeSelfTest() {}

std::string normalizeTrophyName(const std::string& name) { return name; }
const KnownTitleMapping* findKnownTitle(const std::string& /*xboxTitleId*/, const std::string& /*productId*/) {
    return nullptr;
}

const char* gradeToString(TrophyGrade /*grade*/) { return ""; }
int gradeToPoints(TrophyGrade /*grade*/) { return 0; }

ITrophyBackend* createPlatformTrophyBackend() { return nullptr; }

}  // namespace xc::trophy
