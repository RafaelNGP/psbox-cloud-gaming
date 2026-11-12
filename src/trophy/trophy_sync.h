// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "trophy/trophy_backend.h"
#include "trophy/trophy_types.h"
#include "ui/app_ui.h"

#include <utility>
#include <set>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace xc::trophy {

struct TitleCacheSummary {
    int schemaVersion = 0;
    int unlockedCount = -1;
    int totalCount = -1;
    int unlockedGamerscore = -1;
    int totalGamerscore = -1;
    std::string npCommId;
    std::string sonyTitleId;
    std::string titleName;
    bool hasPlatinum = false;
    time_t lastSync = 0;
};

class TrophySyncEngine {
public:
    TrophySyncEngine();
    explicit TrophySyncEngine(std::unique_ptr<ITrophyBackend> backend);
    ~TrophySyncEngine();

    // Initializes engine with user ID (default 0 detects active user)
    bool init(uint32_t userId = 0);
    // The console user the trophies are for (0: not known, as on the desktop).
    // Each user has their own record of what was synced (trophyCacheDir());
    // what the console keeps once per title for everyone (the slot map, the
    // icons) stays in trophySharedDir().
    void setUser(uint32_t userId);
    uint32_t user() const { return userId_; }
    // This user's trophies of these titles removed; what the console shares
    // between its users goes only for titles no other user has synced.
    bool removeTitles(const std::vector<std::pair<std::string, std::string>>& titles, int& removedCount, std::string& err);

    // Sync achievements for a single title against Xbox Live and PlayStation backend
    bool syncTitleAchievements(const std::string& xblAuth, const std::string& xuid,
                               const std::string& xboxTitleId, const std::string& titleName,
                               int& newlyUnlocked, std::string& err,
                               const std::string& tileUrl = "",
                               bool fetchTrophyIcons = true,
                               int expectedEarned = -1,
                               int expectedGamerscore = -1);

    // Called every 20s during active stream (non-blocking async dispatch)
    bool pollActiveStream(const std::string& xblAuth, const std::string& xuid,
                          const std::string& xboxTitleId, const std::string& titleName,
                          const std::string& tileUrl = "");

    // Retro-synchronization (Back-Sync) for all titles in the user's account
    bool syncAllAccountTrophies(const std::string& xblAuth, const std::string& xuid,
                               const std::vector<ui::GameTile>& ownedGames,
                               int& totalTitles, int& totalTrophies, std::string& err,
                               const std::function<bool()>& cancelCheck = nullptr,
                               const std::function<void(int done, int total, const std::string& title)>& progressCb = nullptr,
                               bool forceFetchMissingIcons = true,
                               const std::function<bool(const std::string& xboxTitleId, const std::string& titleName)>& isTitleEnabled = nullptr);

    // Removes all local trophies and packages created by PSBox
    bool removeAllTrophies(int& removedCount, std::string& err);

    // Swap backend (useful for testing or platform initialization)
    void setBackend(std::unique_ptr<ITrophyBackend> backend);
    ITrophyBackend* backend() const { return backend_.get(); }

    bool isSyncing() const;

    std::string trophyCacheDir() const;
    std::string trophySharedDir() const;
    std::string trophyCachePath(const std::string& xboxTitleId) const;

    // Fast Delta Sync Cache Index (ultra-low overhead memory-backed cache summary)
    bool loadTitleCacheSummary(const std::string& xboxTitleId, TitleCacheSummary& outSummary);
    void updateTitleCacheSummary(const std::string& xboxTitleId, const TitleCacheSummary& summary);
    void clearFastCache();

private:
    std::unique_ptr<ITrophyBackend> backend_;
    std::recursive_mutex mutex_;
    std::atomic<bool> pollInFlight_{false};
    std::atomic<int> syncingDepth_{0};
    bool initialized_ = false;
    uint32_t userId_ = 0;
    std::set<std::string> titlesOfOtherUsers() const;  // npCommIds in the other users' records
    std::unordered_map<std::string, TitleCacheSummary> fastCacheIndex_;
};

// Global singleton instance
TrophySyncEngine& syncEngine();

// --- Dedicated Background Trophy Sync Thread (g_trophyThread) ---

// Thread lifecycle
void startTrophyThread();
bool stopTrophyThread(uint32_t timeoutMs = 3000);
void resumeTrophySync();

// Asynchronous dispatch requests
void requestTrophySync(bool forceFetchIcons = true);
void requestTrophySync(const std::string& xblAuth, const std::string& xuid,
                       const std::vector<ui::GameTile>& ownedGames = {},
                       bool forceFetchIcons = true);
void cancelTrophySync();
void requestRemoveTrophies();

// Stream real-time achievement poll (bypasses batch queue, urgent priority)
bool dispatchStreamPoll(const std::string& xblAuth, const std::string& xuid,
                        const std::string& xboxTitleId, const std::string& titleName,
                        const std::string& tileUrl = "");

// Status queries
bool isSyncing();
int titlesDone();
int titlesTotal();

struct TrophySyncContext {
    std::string xblAuth;
    std::string xuid;
    std::vector<ui::GameTile> ownedGames;
    std::function<bool(const std::string& xboxTitleId, const std::string& titleName)> isTitleEnabled;
    std::function<bool()> isStreaming;
    std::function<void(bool loading, int done, int total, const std::string& title)> progressCb;
    std::function<void(int totalTitles, int totalTrophies, const std::string& err)> completeCb;
};

using TrophyContextProvider = std::function<bool(TrophySyncContext& ctx)>;
void setTrophyContextProvider(TrophyContextProvider provider);
// The console user the trophies are for, asked before each task (0: not known
// yet; the engine keeps the one it has).
void setTrophyUserProvider(std::function<uint32_t()> provider);

}  // namespace xc::trophy
