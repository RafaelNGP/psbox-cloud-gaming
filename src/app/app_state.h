// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// What the app's pieces share: app_core.cpp (the main thread: pad, UI,
// drawing), worker.cpp (the worker thread: all the network work),
// stream_screen.cpp (the streaming screen) and price_loop.cpp. Defined in
// app_state.cpp.
#pragma once

#include "app/settings.h"
#include "ui/app_ui.h"
#include "ui/stream_menu.h"
#include "xcloud/gssv.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace xc::app {

class StreamPlayer;
class Library;

extern std::unique_ptr<ui::AppUi> g_ui;

// User settings (settings.json); read by the worker, changed by the UI thread.
extern std::mutex g_settingsMutex;
// The app started on the copies inside the binary because its own files (the fonts in the assets
// folder) could not be read: said once on the screen after start, not only in the log.
extern std::atomic<bool> g_installIncomplete;
extern Settings g_settings;
extern std::atomic<uint64_t> g_settingsVersion;
std::string settingsPath();
// Files that belong to the console's user who opened the app (the pad's user),
// as the settings do: <dataDir>/<base>_<user id>.json. Each console profile
// has its own Xbox sign-in and its own library cache; signing out of one
// leaves the others signed in. The file from before this (one for the whole
// console) goes to the first profile that opens the app, once. Without a
// console user (the desktop) it is <dataDir>/<legacy name>.
std::string userFilePath(const char* base, const char* legacyName);
inline std::string accountPath() { return userFilePath("account", "account.json"); }
inline std::string libraryCachePath() { return userFilePath("library_cache", "library_cache.json"); }
// Caller holds g_settingsMutex. 1440p is offered once a stream delivered it.
bool allow1440Locked();
// Settings' screen takes g_settings again (ui::choiceFromSettings) after
// something outside it changed them (the game menu, a test putting them
// back): else leaving Settings would save its older copy over them. Caller
// holds g_settingsMutex (then AppUi's lock is taken: the usual order). Not
// while Settings is open with changes of its own (it would drop them).
void refreshSettingsUiLocked();

// --- Worker commands (worker.cpp) ----------------------------------------------
enum Command { kNone, kSignIn, kPlay, kSignOut, kReloadLibrary, kConsoles, kUpdate, kClaimFree, kCheckSignIn, kNetworkTest, kSyncTrophies, kRemoveTrophies };
extern std::atomic<int> g_command;
// A library refresh that came back incomplete: when (platform::nowMs) the next try is due, 0 none.
extern std::atomic<uint64_t> g_libraryRetryAt;
// Called when a refresh ends: incomplete -> the notice and the next try (30 s, 1, 2, 5 min, then it
// waits for the user's own refresh); complete -> nothing pending.
void libraryRefreshEnded(bool incomplete);
// Play was pressed and the worker has not taken it yet (it may be finishing a
// library refresh that takes seconds): the launch screen is up already, and a
// cancel made now withdraws the command instead of stopping a stream.
extern std::atomic<bool> g_playPending;
extern std::atomic<bool> g_cancel;
extern std::mutex g_argMutex;
extern ui::GameTile g_playTile;  // what kPlay plays
void worker();
// The worker thread started (detached), as coreStartup() does.
void startWorker();
// Plays the queue-ready chime through the DualSense speaker and vibrates.
void playQueueReadyAlert(bool notifyToast = false);
// The update pop-up's "Not now": that release isn't offered again on start.
void skipOfferedUpdate();
// Updates the running library's favorites list and refreshes UI rows.
void updateLibraryFavorites(const std::vector<std::string>& favorites);
// Updates the running library's wishlist and refreshes UI rows.
void updateLibraryWishlist(const std::vector<std::string>& wishlist);
// Sets the initial library loaded from offline cache before worker thread runs.
void setInitialLibrary(std::shared_ptr<Library> lib);
std::shared_ptr<Library> currentLibrary();

// The running stream, for the input thread.
extern std::mutex g_playerMutex;
extern StreamPlayer* g_player;
// The stream's numbers for the in-game menu, refreshed every second by the
// worker; the sequence number tells the main thread something changed.
extern std::mutex g_infoMutex;
extern ui::StreamInfo g_streamInfo;
extern std::atomic<uint32_t> g_infoSeq;
// More of the stream's numbers, for the WebUI's read-only telemetry (same mutex, same second).
struct StreamTelemetry {
    double decodePeakMs = 0, drawMs = 0, drawPeakMs = 0, onScreenPeakMs = 0, glassMs = 0, glassPeakMs = 0;
    uint64_t skippedNow = 0, skippedTotal = 0, lateFrames = 0, decodeFailures = 0, keyframeRequests = 0;
    uint64_t rtpLost = 0, rtpRecovered = 0, rtpNacks = 0, rtpDroppedFrames = 0;
    uint64_t gapMs = 0, burst = 0, frameLateMs = 0, rembKbps = 0;  // gap, burst, late: of the last second
    int bestRttMs = -1, reconnects = 0;
    uint64_t seconds = 0, badSeconds = 0;  // of this session, once the picture runs
};
extern StreamTelemetry g_streamTelemetry;
extern std::atomic<bool> g_playingHome;          // the stream is the user's own Xbox
extern std::atomic<bool> g_tierPicked;           // the game menu chose a tier: the probe leaves it
extern std::atomic<uint64_t> g_xboxButtonUntil;  // the Xbox button, held until then (ms)
// An update is in place: the main thread starts the app again (the system
// call that does it takes the app down when made from another thread).
extern std::atomic<bool> g_restartWanted;

// --- Closing (desktop: the PS5 is closed by the system, never by the app) ------
// The app is closing: the worker leaves its loop after what it is doing is
// cancelled (g_cancel): a stream is stopped and its xCloud session closed,
// so the account's streaming slot is free at once.
extern std::atomic<bool> g_quit;
void requestQuit();
// After requestQuit(): true once the worker has left its loop (with the
// threads it started), false if `ms` went by first (a request hanging on
// the network). True at once when it never started.
bool waitForWorker(uint32_t ms);

// --- Store prices (price_loop.cpp) -----------------------------------------------
void startPriceLoop();
// Closing: the price loop told to end; true once it has (joined) within
// `ms`, else it is left to the process's exit.
bool stopPriceLoop(uint32_t ms);
void setPriceMarket(const std::string& market, const std::string& language);
// Auth tokens for title achievements and store features.
void setPriceAuth(const std::string& xblAuth, const std::string& xuid);
// The signed-in xCloud client (a copy), for the open game page's queue
// estimate; nullptr after signing out.
void setQueueClient(const xcloud::GssvClient* gssv);

}  // namespace xc::app
