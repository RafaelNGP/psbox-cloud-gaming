// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace xc::app {
enum SessionChoice { Reconnect, ShowClock, ClockSize, ClockPosition, ClockOpacity, ClockColor, ClockSkin, BasicFill, BasicAlpha, PeekGesture, SessionChoiceCount };
using SessionChoices = std::array<int, SessionChoiceCount>;
inline constexpr SessionChoices inheritedSession{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
inline constexpr SessionChoices defaultSession{0,1,0,2,2,0,0,0,3,0};
enum ClockMode { ClockHidden = 0, ClockLastFive = 1, ClockWholeSession = 2 };
inline constexpr SessionChoices sessionChoiceCounts{2,3,3,6,4,8,10,5,5,2};

struct SessionPreferences {
    SessionChoices general = defaultSession;
    std::map<std::string, SessionChoices> games;
    SessionChoices overrides(const std::string& key) const {
        auto it = games.find(key);
        return it == games.end() ? inheritedSession : it->second;
    }
    SessionChoices effective(const std::string& key) const {
        auto values = overrides(key);
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i] < 0) values[i] = general[i];
        return values;
    }
};

struct SessionClockRecord {
    std::string id, title, account;
    int64_t startUtc = 0;
    int limit = 0;
    bool skipReconnect = false;

    // Update once after provisioning, before the stream menu is shown.
    int64_t open(const std::string& path, const std::string& game, const std::string& user,
                 int reportedLimit, int64_t now, bool knownNew) {
        const bool same = !path.empty() && !user.empty() && path == id && game == title && user == account;
        if (!same) {
            id = path;
            title = game;
            account = user;
            startUtc = knownNew ? now : 0;
            limit = reportedLimit;
            skipReconnect = false;
        } else if (reportedLimit > 0) {
            limit = reportedLimit;
        }
        return startUtc > 0 && now >= startUtc ? now - startUtc : -1;
    }
};

inline int64_t estimatedSessionLeft(int64_t elapsed, int limit) {
    return elapsed < 0 || limit <= 0 ? -1 : std::max<int64_t>(0, limit - elapsed);
}
inline bool timerVisible(int64_t left, int mode) {
    return left >= 0 && (mode == ClockWholeSession || (mode == ClockLastFive && left <= 300));
}
inline bool restartableEnd(const std::string& reason, bool reachedGame, bool outOfAccountTime) {
    if (!reachedGame) return false;
    for (const char* stop : {"KickForStopCommand", "KickForClosedGame", "KickByNewSession",
                             "KickForBeingIdle", "MonthlyLimitExceeded"})
        if (reason.find(stop) != std::string::npos) return false;
    if (outOfAccountTime && reason.find("OutOfStreamingTime") != std::string::npos) return false;
    return reason.find("the server ended the session") == 0 ||
           reason.find("the session ended on the server") == 0 ||
           reason == "the connection was lost and couldn't be restored";
}
} // namespace xc::app
