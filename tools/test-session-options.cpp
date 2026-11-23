#include "app/session_options.h"
#include <cassert>

using namespace xc::app;

int main() {
    SessionPreferences prefs;
    prefs.general[Reconnect] = 1;
    auto changed = prefs.overrides("A");
    changed[Reconnect] = 0;
    changed[ClockPosition] = 4;
    prefs.games["A"] = changed;
    assert(!prefs.effective("A")[Reconnect] && prefs.effective("B")[Reconnect]);
    assert(prefs.effective("A")[ClockPosition] == 4);
    prefs.general[ClockSize] = 2;
    assert(prefs.effective("A")[ClockSize] == 2);

    SessionClockRecord clock;
    assert(clock.open("/alpha", "A", "user", 1800, 10000, true) == 0);
    clock.skipReconnect = true;
    auto reopened = clock;
    assert(reopened.open("/alpha", "A", "user", 0, 11560, false) == 1560);
    assert(reopened.skipReconnect && estimatedSessionLeft(1560, reopened.limit) == 240);
    assert(reopened.open("/beta", "A", "user", 1800, 11600, true) == 0);
    assert(!reopened.skipReconnect);
    assert(reopened.open("/beta", "A", "other", 1800, 11700, false) == -1);
    assert(estimatedSessionLeft(-1, 1800) == -1 && estimatedSessionLeft(100, 0) == -1);
    assert(!timerVisible(301, true) && timerVisible(300, true) && timerVisible(1, true));
    assert(!timerVisible(299, false) && !timerVisible(-1, true));
    for (int left : {1800, 1500, 900, 301, 300, 0}) {
        assert(timerVisible(left, ClockWholeSession));
        assert(timerVisible(left, ClockLastFive) == (left <= 300));
        assert(!timerVisible(left, ClockHidden));
    }
    assert(!timerVisible(-1, ClockWholeSession));
    assert(defaultSession[ShowClock] == ClockLastFive);
    auto timerChoice = prefs.overrides("A");
    timerChoice[ShowClock] = ClockWholeSession;
    prefs.games["A"] = timerChoice;
    assert(timerVisible(1500, prefs.effective("A")[ShowClock]));
    assert(!timerVisible(1500, prefs.effective("B")[ShowClock]));
    assert(defaultSession[ClockColor] == 0);
    prefs.general[ClockColor] = 4;
    assert(prefs.effective("B")[ClockColor] == 4);
    prefs.games["A"][ClockColor] = 7;
    assert(prefs.effective("A")[ClockColor] == 7);
    assert(defaultSession[ClockSkin] == 0 && defaultSession[BasicAlpha] == 3);
    prefs.general[ClockSkin] = 2;
    assert(prefs.effective("A")[ClockSkin] == 2);
    prefs.games["A"][ClockSkin] = 1;
    assert(prefs.effective("A")[ClockSkin] == 1);
    assert(prefs.general[PeekGesture] == 0);
    prefs.general[ClockSkin] = 9;
    prefs.general[PeekGesture] = 1;
    assert(prefs.effective("B")[ClockSkin] == 9 && prefs.effective("A")[ClockSkin] == 1);
    assert(prefs.effective("A")[PeekGesture] == 1);
    prefs.games["A"][PeekGesture] = 0;
    prefs.games["A"][ClockSkin] = -1;
    assert(prefs.effective("A")[ClockSkin] == 9 && prefs.effective("A")[PeekGesture] == 0);
    const auto timeUp = "the server ended the session (KickForOutOfStreamingTime)";
    assert(restartableEnd(timeUp, true, false));
    assert(!restartableEnd(timeUp, false, false) && !restartableEnd(timeUp, true, true));
    assert(!restartableEnd("the server ended the session (KickForStopCommand)", true, false));
    assert(!restartableEnd("the game closed on the server (0x00000000)", true, false));
    assert(!restartableEnd("the session ended on the server while reconnecting (KickForClosedGame)", true, false));
    assert(!restartableEnd("left the game", true, false));
}
