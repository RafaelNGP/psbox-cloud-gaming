// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "trophy/trophy_types.h"
#include <set>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xc::trophy {

class ITrophyBackend {
public:
    virtual ~ITrophyBackend() = default;

    // Initializes backend with active PlayStation user ID (e.g. 0x1ea2f4da)
    virtual bool init(uint32_t userId) = 0;

    // Registers the title's trophy set with the system
    virtual bool registerTitle(const TitleTrophySet& set) = 0;

    // Unlocks a trophy for the title (calling native OS API on PS5/PS4)
    virtual bool unlockTrophy(const std::string& npCommId, int trophyId, TrophyGrade grade, const std::string& trophyName = {}) = 0;

    // Checks if trophy is already recorded as unlocked in backend
    virtual bool isTrophyUnlocked(const std::string& npCommId, int trophyId) = 0;

    // Checks if title's trophy package and user data are already fully deployed and intact
    virtual bool isTitlePackageDeployed(const std::string& npCommId, const std::string& sonyTitleId) {
        (void)npCommId; (void)sonyTitleId; return true;
    }

    // Flushes pending trophy commits to the system profile/database
    virtual void flush() {}

    // Removes the current user's trophy data for these titles (npCommId, title id),
    // and what the console keeps once per title for all its users (the trophy pack,
    // the app's metadata, the database rows), except for the npCommIds in
    // `sharedInUse`: titles another console user still has trophies in.
    virtual bool removeAllTrophies(const std::vector<std::pair<std::string, std::string>>& titles, int& removedCount, std::string& err,
                                   const std::set<std::string>* sharedInUse = nullptr) {
        (void)titles; (void)err; (void)sharedInUse; removedCount = 0; return true;
    }

    // The titles this app made trophies for that the current user has data of on
    // the console (npCommId, title id), whatever the app's own records say: a
    // user's trophies must be removable even when their records are gone.
    virtual void listUserTitles(std::vector<std::pair<std::string, std::string>>& out) { (void)out; }

    // Backend identifier name
    virtual std::string name() const = 0;
};

// Creates platform-appropriate backend (Prospero PS5, Orbis PS4, or Desktop Null)
ITrophyBackend* createPlatformTrophyBackend();

// Native self-test: register + unlock a trophy set bound to the RUNNING app's
// own title (PSBox PPSA99810) via the normal v1 flow. PS5-only; no-op elsewhere.
void runPsboxNativeSelfTest();

}  // namespace xc::trophy
