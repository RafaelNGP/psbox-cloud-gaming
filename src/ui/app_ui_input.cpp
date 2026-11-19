// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstddef>
#include "ui/app_ui_internal.h"
#include "app/shell.h"

namespace xc::ui {

void AppUi::handleHome(const NavInput& in, UiEvent& ev) {
    // Caller holds mutex_.
    if ((in.scrollRows || in.scrollCards) && !(searching_ && searchOnKeys_)) {
        // Smooth scrolling: the rows and cards it moved by since the last
        // pass, one step at a time (the view glides after the focus).
        auto steps = [&](int n, bool vertical) {
            NavInput step;
            step.nowMs = in.nowMs;
            (vertical ? (n > 0 ? step.down : step.up) : (n > 0 ? step.right : step.left)) = true;
            for (int k = 0; k < std::abs(n); ++k) {
                // In the search's results, sideways stops at the row's edge
                // (left of it is the keyboard).
                if (searching_ && !vertical && (n < 0 ? resultFocus_ % kResultCols == 0
                                                      : (resultFocus_ + 1) % kResultCols == 0))
                    break;
                handleHome(step, ev);
            }
        };
        if (in.scrollRows) steps(in.scrollRows, true);
        if (in.scrollCards) steps(in.scrollCards, false);
        return;
    }
    if (in.l1 || in.r1) {
        // Game Pass, Your games, My consoles, round.
        int n = static_cast<int>(tab_) + (in.r1 ? 1 : 2);
        tab_ = static_cast<Tab>(n % 3);
        searching_ = false;
        dirty_ = true;
        if (tab_ == Tab::Consoles) ev.action = Action::ConsolesShown;
        return;
    }
    if (in.l3 && !searching_) {
        ev.action = Action::RefreshLibrary;
        return;
    }
    if (in.options) {
        screen_ = Screen::Settings;
        settingsRow_ = 0;
        settingsFromError_ = false;
        settingsCountry_ = settings_.loginCountry;
        dropdownOpen_ = false;
        filterList_ = -1;
        settingsScrollY_ = 0.0f;
        dirty_ = true;
        return;
    }
    if (in.triangle && !searching_ && tab_ != Tab::Consoles) {  // search this tab
        searching_ = true;
        searchOnKeys_ = true;
        query_.clear();
        runSearch();
        dirty_ = true;
        return;
    }
    if (searching_) {
        if (searchOnKeys_) {
            if ((in.back || in.triangle) && filterList_ < 0) {  // close the search (Circle closes an open list first)
                searching_ = false;
                dirty_ = true;
                return;
            }
            handleSearchKeys(in);
            return;
        }
        if (in.back || in.triangle || (in.left && resultFocus_ % kResultCols == 0)) {  // back to the keys
            searchOnKeys_ = true;
            dirty_ = true;
            return;
        }
        if (in.square && !query_.empty()) {  // delete without going back to the keys
            query_.pop_back();
            runSearch();
            searchOnKeys_ = results_.empty();
            dirty_ = true;
            return;
        }
        if (moveInGrid(resultFocus_, static_cast<int>(results_.size()), kResultCols, in)) {
            resultScroll_.target = gridTarget(resultFocus_, kResultCols);
            dirty_ = true;
        }
        if (in.accept && resultFocus_ < static_cast<int>(results_.size()))
            openDetails(results_[static_cast<size_t>(resultFocus_)]);
        return;
    }
    switch (tab_) {
        case Tab::Consoles: {
            int n = static_cast<int>(consoles_.size());
            // Search again: Cross with none listed, Triangle otherwise.
            if (consolesKnown_ && ((n == 0 && in.accept) || in.triangle)) {
                if (n == 0) consolesKnown_ = false;  // the spinner while it looks
                ev.action = Action::ConsolesShown;
                dirty_ = true;
                break;
            }
            if (n == 0) break;
            int before = consoleFocus_;
            if (in.right && consoleFocus_ + 1 < n) ++consoleFocus_;
            if (in.left && consoleFocus_ > 0) --consoleFocus_;
            if (consoleFocus_ != before) dirty_ = true;
            if (in.accept) {
                const ConsoleTile& con = consoles_[static_cast<size_t>(consoleFocus_)];
                ev.action = Action::Play;
                ev.game.titleId = con.serverId;
                ev.game.name = con.name.empty() ? "Xbox" : con.name;
                ev.game.homeConsole = true;
            }
            break;
        }
        case Tab::GamePass: {
            if (rows_.empty()) break;
            int rows = static_cast<int>(rows_.size());
            const GameRow& curRow = rows_[static_cast<size_t>(focusRow_)];
            int& col = focusCol_[static_cast<size_t>(focusRow_)];
            int count = static_cast<int>(curRow.tiles.size());

            if (curRow.isGrid) {
                if (in.up && col < kLibraryCols) {
                    if (focusRow_ > 0) {
                        --focusRow_;
                        int upCols = static_cast<int>(rows_[static_cast<size_t>(focusRow_)].tiles.size());
                        focusCol_[static_cast<size_t>(focusRow_)] = std::clamp(focusCol_[static_cast<size_t>(focusRow_)], 0, upCols - 1);
                    }
                } else if (in.down && col + kLibraryCols >= count && col / kLibraryCols == (count - 1) / kLibraryCols) {
                    if (focusRow_ + 1 < rows) {
                        ++focusRow_;
                        int downCols = static_cast<int>(rows_[static_cast<size_t>(focusRow_)].tiles.size());
                        focusCol_[static_cast<size_t>(focusRow_)] = std::clamp(focusCol_[static_cast<size_t>(focusRow_)], 0, downCols - 1);
                    }
                } else {
                    moveInGrid(col, count, kLibraryCols, in);
                }
            } else {
                if (in.down && focusRow_ + 1 < rows) {
                    ++focusRow_;
                    if (rows_[static_cast<size_t>(focusRow_)].isGrid) {
                        int gCols = static_cast<int>(rows_[static_cast<size_t>(focusRow_)].tiles.size());
                        int targetCol = std::min(col % kLibraryCols, gCols - 1);
                        focusCol_[static_cast<size_t>(focusRow_)] = std::max(0, targetCol);
                    }
                }
                if (in.up && focusRow_ > 0) --focusRow_;
                if (in.right && col + 1 < count) ++col;
                if (in.left && col > 0) --col;
            }
            if (in.up || in.down || in.left || in.right) {
                retarget();
                dirty_ = true;
            }
            if (in.accept && focusedTile()) openDetails(*focusedTile());
            if (in.square && focusedTile()) toggleHidden(*focusedTile(), ev);
            break;
        }
        case Tab::Library: {
            if (!libraryTile(0)) break;
            const LibraryLayout& L = libraryLayout();
            int count = static_cast<int>(L.rowOf.size());
            int f = std::min(gridFocus_, count - 1), before = f;
            int row = L.rowOf[static_cast<size_t>(f)], col = L.colOf[static_cast<size_t>(f)];
            int rows = static_cast<int>(L.rowY.size());
            auto rowLen = [&](int r) {
                int next = r + 1 < rows ? L.rowFirst[static_cast<size_t>(r + 1)] : count;
                return next - L.rowFirst[static_cast<size_t>(r)];
            };
            if (in.right && f + 1 < count && L.rowOf[static_cast<size_t>(f + 1)] == row) ++f;
            if (in.left && col > 0) --f;
            if (in.down && row + 1 < rows) f = L.rowFirst[static_cast<size_t>(row + 1)] + std::min(col, rowLen(row + 1) - 1);
            if (in.up && row > 0) f = L.rowFirst[static_cast<size_t>(row - 1)] + std::min(col, rowLen(row - 1) - 1);
            if (f != before) {
                gridFocus_ = f;
                int y = L.rowY[static_cast<size_t>(L.rowOf[static_cast<size_t>(f)])];
                gridScroll_.target = static_cast<float>(std::max(0, y - kGridPitchY - 40));
                dirty_ = true;
            }
            if (in.accept && libraryTile(f)) openDetails(*libraryTile(f));
            if (in.square && libraryTile(f)) {
                GameTile t = *libraryTile(f);
                toggleHidden(t, ev);
            }
            if (in.l2 || in.r2) {
                // Jump between sections: the account's games, games to buy,
                // hidden games. L2 goes to the start of this section first.
                int starts[3] = {0, static_cast<int>(owned_.size()),
                                 static_cast<int>(owned_.size() + purchasable_.size())};
                int total = starts[2] + static_cast<int>(hiddenTiles_.size());
                int section = f >= starts[2] ? 2 : f >= starts[1] ? 1 : 0;
                int target = f;
                if (in.r2) {
                    for (int k = section + 1; k < 3; ++k)
                        if (starts[k] < total && (k == 2 ? !hiddenTiles_.empty() : starts[k] < starts[k + 1])) {
                            target = starts[k];
                            break;
                        }
                } else if (f != starts[section]) {
                    target = starts[section];
                } else {
                    for (int k = section - 1; k >= 0; --k)
                        if (starts[k] < starts[k + 1]) {
                            target = starts[k];
                            break;
                        }
                }
                if (target != f) {
                    gridFocus_ = target;
                    int section2 = target >= starts[2] ? 2 : target >= starts[1] ? 1 : 0;
                    gridScroll_.target = static_cast<float>(L.headerY[section2]);  // its heading at the top
                    dirty_ = true;
                }
            }
            if (in.r3) {  // next sort order
                librarySort_ = static_cast<LibrarySort>((static_cast<int>(librarySort_) + 1) %
                                                         static_cast<int>(LibrarySort::Count));
                rebuild(true);  // same place on screen, not back to the top
                prefsEvent(ev);
            }
            break;
        }
    }
}

void AppUi::setMouseCursor(float x, float y, bool visible) {
    std::lock_guard<std::mutex> lock(mutex_);
    mouseCursorX_ = std::clamp(x, 0.0f, 1919.0f);
    mouseCursorY_ = std::clamp(y, 0.0f, 1079.0f);
    mouseCursorVisible_ = visible;
    if (visible) {
        mouseCursorVisibleUntil_ = platform::nowMs() + 3000;
    }
    dirty_ = true;
}

UiEvent AppUi::onMouseMove(float x, float y) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    mouseCursorX_ = std::clamp(x, 0.0f, 1919.0f);
    mouseCursorY_ = std::clamp(y, 0.0f, 1079.0f);
    mouseCursorVisible_ = true;
    mouseCursorVisibleUntil_ = platform::nowMs() + 3000;
    dirty_ = true;
    if (screen_ == Screen::Streaming || screen_ == Screen::Splash) {
        return ev;
    }
    if (screen_ == Screen::Home && quitPrompt_) {
        constexpr int kButtonW = 300, kButtonH = 72, kPanelW = 2 * kButtonW + 24 + 112, kPanelH = 250;
        Rect panel{(kW - kPanelW) / 2, (kH - kPanelH) / 2 - 30, kPanelW, kPanelH};
        Rect b0{panel.x + 56, panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        Rect b1{panel.x + 56 + (kButtonW + 24), panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        auto inRect = [&](const Rect& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
        if (inRect(b0) && promptFocus_ != 0) {
            promptFocus_ = 0;
            dirty_ = true;
        } else if (inRect(b1) && promptFocus_ != 1) {
            promptFocus_ = 1;
            dirty_ = true;
        }
        return ev;
    }
    if (screen_ == Screen::Home && !searching_ && !reportOpen_ && !updatePrompt_ && !quitPrompt_) {
        if (tab_ == Tab::GamePass && y >= 140 && y < kH - 80) {
            constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
            const int gridLeft = (kW - kGridW) / 2;
            int afterGridOffset = 0;
            for (size_t r = 0; r < rows_.size(); ++r) {
                float rel = static_cast<float>(r) - rowY_.value;
                int rowBaseY = kRowTop + static_cast<int>(std::lround(rel * kRowPitch)) + afterGridOffset;
                const GameRow& row = rows_[r];
                if (row.isGrid) {
                    int offset = static_cast<int>(std::lround(rowScroll_[r].value));
                    int cardY = rowBaseY - offset + 48;
                    for (size_t i = 0; i < row.tiles.size(); ++i) {
                        int c_col = static_cast<int>(i) % kLibraryCols;
                        int c_row = static_cast<int>(i) / kLibraryCols;
                        int cy = cardY + c_row * kGridPitchY;
                        int cx = gridLeft + c_col * kCardPitch;
                        if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                            if (focusRow_ != static_cast<int>(r) || focusCol_[r] != static_cast<int>(i)) {
                                focusRow_ = static_cast<int>(r);
                                focusCol_[r] = static_cast<int>(i);
                                retarget();
                                dirty_ = true;
                            }
                            return ev;
                        }
                    }
                    int gridHeight = 48 + ((static_cast<int>(row.tiles.size()) + kLibraryCols - 1) / kLibraryCols) * kGridPitchY;
                    afterGridOffset += gridHeight - offset - kRowPitch;
                } else {
                    int cardY = rowBaseY + 48;
                    int scroll = static_cast<int>(std::lround(rowScroll_[r].value));
                    int firstCol = std::max(0, scroll / kCardPitch - 1);
                    for (size_t col = static_cast<size_t>(firstCol); col < row.tiles.size(); ++col) {
                        int cx = kMargin + static_cast<int>(col) * kCardPitch - scroll;
                        if (cx > kW) break;
                        if (x >= cx && x < cx + kCard && y >= cardY && y < cardY + kCard) {
                            if (focusRow_ != static_cast<int>(r) || focusCol_[r] != static_cast<int>(col)) {
                                focusRow_ = static_cast<int>(r);
                                focusCol_[r] = static_cast<int>(col);
                                retarget();
                                dirty_ = true;
                            }
                            return ev;
                        }
                    }
                }
            }
        } else if (tab_ == Tab::Library && y >= 150 && y < kH - 80) {
            constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
            const int left = (kW - kGridW) / 2;
            const LibraryLayout& L = libraryLayout();
            int offset = static_cast<int>(std::lround(gridScroll_.value));
            for (size_t i = 0; i < L.rowOf.size(); ++i) {
                int cy = kLibraryTop + L.rowY[static_cast<size_t>(L.rowOf[i])] - offset;
                int cx = left + L.colOf[i] * kCardPitch;
                if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                    if (gridFocus_ != static_cast<int>(i)) {
                        gridFocus_ = static_cast<int>(i);
                        dirty_ = true;
                    }
                    return ev;
                }
            }
        }
    } else if (screen_ == Screen::Settings) {
        if (dropdownOpen_ || tester_ != Tester::None || pickerOpen_ || countryPickerOpen_ || reportOpen_) {
            return ev;
        }
        int curY = kSettingsListTop - static_cast<int>(std::lround(settingsScrollY_));
        for (const auto& [i, groupStart] : visibleSettingsRows(caps_)) {
            if (groupStart) curY += kSettingsGroupGap;
            if (x >= kMargin && x < kMargin + kSettingsRowW && y >= curY && y < curY + kSettingsRowH &&
                y >= kSettingsClipTop && y < kH - 80) {
                if (settingsRow_ != i) {
                    settingsRow_ = i;
                    dirty_ = true;
                }
                return ev;
            }
            curY += kSettingsRowH + kSettingsGap;
        }
    } else if (screen_ == Screen::Details) {
        if (achievementsOpen_ || reportOpen_) {
            return ev;
        }
        if (gameSettingsOpen_) {
            constexpr int kModalW = 1140, kModalH = 960;
            constexpr int kRowH = 38, kGap = 4, kGroupGap = 7;
            const auto rows = visibleGameSettingsRows(caps_, activeGameHasCustom_, activeGameMouseSupported_);
            const auto most = visibleGameSettingsRows(caps_, true, activeGameMouseSupported_), all = visibleGameSettingsRows(UiCaps{}, true, true);
            auto groups = [](const std::vector<SettingsRowPos>& v) {
                return static_cast<int>(std::count_if(v.begin(), v.end(), [](const SettingsRowPos& r) { return r.groupStart; }));
            };
            const int modalH = kModalH - static_cast<int>(all.size() - most.size()) * (kRowH + kGap) -
                               (groups(all) - groups(most)) * kGroupGap;
            Rect panel{(kW - kModalW) / 2, (kH - modalH) / 2, kModalW, modalH};
            int curY = panel.y + 92;
            for (int pos = 0; pos < static_cast<int>(rows.size()); ++pos) {
                if (rows[static_cast<size_t>(pos)].groupStart) curY += kGroupGap;
                Rect r{panel.x + 50, curY, panel.w - 100, kRowH};
                if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                    if (gameSettingsRow_ != pos) {
                        gameSettingsRow_ = pos;
                        dirty_ = true;
                    }
                    return ev;
                }
                curY += kRowH + kGap;
            }
            return ev;
        }
        auto inRect = [&](const Rect& r) {
            return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
        };
        Rect play = playRect_.w > 0 ? playRect_ : Rect{kMargin, 620, 300, 76};
        if (inRect(play) || inRect(claimRect_)) {
            if (detailButtonFocus_ != 0) {
                detailButtonFocus_ = 0;
                dirty_ = true;
            }
        } else if (inRect(achRect_)) {
            if (detailButtonFocus_ != 1) {
                detailButtonFocus_ = 1;
                dirty_ = true;
            }
        }
    }
    return ev;
}

UiEvent AppUi::onMouseDown(float x, float y, int button) {
    return handlePointerDown(x, y, button, true);
}

UiEvent AppUi::handlePointerDown(float x, float y, int button, bool showCursor) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    mouseCursorX_ = std::clamp(x, 0.0f, 1919.0f);
    mouseCursorY_ = std::clamp(y, 0.0f, 1079.0f);
    if (showCursor) {
        mouseCursorVisible_ = true;
        mouseCursorVisibleUntil_ = platform::nowMs() + 3000;
    } else {
        mouseCursorVisible_ = false;
        mouseCursorVisibleUntil_ = 0;
    }
    dirty_ = true;
    if (screen_ == Screen::Streaming || screen_ == Screen::Splash) {
        return ev;
    }
    if (button == 3) {
        // Right click: Back
        if (reportOpen_) {
            reportOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if (screen_ == Screen::Home && updatePrompt_) {
            if (showingUpdateNotes_) {
                showingUpdateNotes_ = false;
            } else {
                ev.action = Action::UpdateLater;
                updatePrompt_ = false;
            }
            dirty_ = true;
            return ev;
        }
        if (countryPickerOpen_) {
            countryPickerOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if (screen_ == Screen::Home && quitPrompt_) {
            quitPrompt_ = false;
            dirty_ = true;
            return ev;
        }
        if (screen_ == Screen::Settings) {
            if (pickerOpen_) {
                handleColourPicker(NavInput{.back = true});
                dirty_ = true;
                return ev;
            }
            if (tester_ == Tester::SignInCheck || tester_ == Tester::NetworkTest || tester_ == Tester::Donation) {
                tester_ = Tester::None;
                dirty_ = true;
                return ev;
            }
            if (tester_ != Tester::None) {
                handleTester(NavInput{.back = true});
                if (tester_ == Tester::None) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
                dirty_ = true;
                return ev;
            }
        }
        if (dropdownOpen_) {
            dropdownOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if (achievementsOpen_) {
            achievementsOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if (gameSettingsOpen_) {
            handleGameSettings(NavInput{.back = true}, ev);
            return ev;
        }
        if (searching_) {
            searching_ = false;
            dirty_ = true;
            return ev;
        }
        if (screen_ == Screen::Settings) {
            resetSettingsHoldStart_ = 0;
            screen_ = settingsFromError_ ? Screen::Error : Screen::Home;
            dirty_ = true;
            ev.action = settings_.loginCountry != settingsCountry_ ? Action::LoginCountry : Action::SettingsChanged;
            ev.settings = settings_;
            return ev;
        }
        if (screen_ == Screen::Details) {
            syncHeroToRowsLocked(detail_.productId, detail_.titleId, detail_.heroUrl, detail_.description, detail_.publisher, detail_.categories);
            screen_ = Screen::Home;
            dirty_ = true;
            return ev;
        }
        return ev;
    }
    if (button != 1) return ev;

    // Check bottom-bar action hints across all screens
    if (y >= kH - 90) {
        for (const auto& [hintIcon, r] : hintRects_) {
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                NavInput nav{};
                if (hintIcon == kIconCross) nav.accept = true;
                else if (hintIcon == kIconCircle) nav.back = true;
                else if (hintIcon == kIconTriangle) nav.triangle = true;
                else if (hintIcon == kIconSquare) nav.square = true;
                else if (hintIcon == kIconOptions) nav.options = true;
                else if (hintIcon == kIconL3) nav.l3 = true;
                else if (hintIcon == kIconR3) nav.r3 = true;
                else if (hintIcon == kIconTouchpad) nav.touchpad = true;
                return handleLocked(nav);
            }
        }
    }

    if (screen_ == Screen::SignIn && !code_.empty()) {
        constexpr int left = 180;
        int panelW = std::max(400, static_cast<int>(code_.size()) * 82 + 64);
        if (x >= left && x < left + panelW && y >= 600 && y < 830) {
            ev.action = Action::CopyClipboard;
            ev.clipboardText = code_;
            return ev;
        }
#if defined(__ANDROID__)
        Rect btnBox{1180, 480, 560, 124};
        if (x >= btnBox.x && x < btnBox.x + btnBox.w && y >= btnBox.y && y < btnBox.y + btnBox.h) {
            ev.action = Action::OpenUrl;
            ev.url = codeUrl_.empty() ? "https://www.microsoft.com/link" : codeUrl_;
            if (!code_.empty() && ev.url.find("otc=") == std::string::npos) {
                ev.url += (ev.url.find('?') == std::string::npos ? "?otc=" : "&otc=") + code_;
            }
            return ev;
        }
#endif
    }

    if (reportOpen_) {
        constexpr int kPanelW = 1500, kPanelH = 720;
        Rect panel{(kW - kPanelW) / 2, (kH - kPanelH) / 2 - 30, kPanelW, kPanelH};
        Rect closeBtn{panel.x + panel.w - 64, panel.y + 16, 48, 48};
        Rect backBtn{panel.x + 56, panel.y + panel.h - 80, 220, 54};
        Rect urlBtn{backBtn.x + backBtn.w + 20, backBtn.y, 340, 54};

        if ((x >= closeBtn.x - 12 && x <= closeBtn.x + closeBtn.w + 12 && y >= closeBtn.y - 12 && y <= closeBtn.y + closeBtn.h + 12) ||
            (x >= backBtn.x && x <= backBtn.x + backBtn.w && y >= backBtn.y && y <= backBtn.y + backBtn.h)) {
            reportOpen_ = false;
            dirty_ = true;
            return ev;
        }
        if ((caps_.openUrl
#if defined(__ANDROID__)
            || true
#endif
            ) && !reportUrl_.empty() && x >= urlBtn.x && x <= urlBtn.x + urlBtn.w && y >= urlBtn.y && y <= urlBtn.y + urlBtn.h) {
            ev.action = Action::OpenUrl;
            ev.url = reportUrl_;
            return ev;
        }
        // Clicking outside the panel also dismisses the crash modal
        if (x < panel.x || x > panel.x + panel.w || y < panel.y || y > panel.y + panel.h) {
            reportOpen_ = false;
            dirty_ = true;
            return ev;
        }
        return ev;
    }

    if (screen_ == Screen::Home && quitPrompt_) {
        constexpr int kButtonW = 300, kButtonH = 72, kPanelW = 2 * kButtonW + 24 + 112, kPanelH = 250;
        Rect panel{(kW - kPanelW) / 2, (kH - kPanelH) / 2 - 30, kPanelW, kPanelH};
        Rect b0{panel.x + 56, panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        Rect b1{panel.x + 56 + (kButtonW + 24), panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        auto inRect = [&](const Rect& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
        if (inRect(b0)) {
            ev.action = Action::Quit;
            quitPrompt_ = false;
            dirty_ = true;
            return ev;
        } else if (inRect(b1) || !inRect(panel)) {
            quitPrompt_ = false;
            dirty_ = true;
            return ev;
        }
        return ev;
    }

    if (screen_ == Screen::Home && updatePrompt_) {
        constexpr int kPanelW = 960, kPanelH = 340, kButtonW = 260, kButtonH = 72;
        Rect panel{(kW - kPanelW) / 2, (kH - kPanelH) / 2 - 40, kPanelW, kPanelH};
        constexpr int kGap = (kPanelW - 112 - 3 * kButtonW) / 2;
        Rect b0{panel.x + 56, panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        Rect b1{panel.x + 56 + (kButtonW + kGap), panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        Rect b2{panel.x + 56 + 2 * (kButtonW + kGap), panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        auto inRect = [&](const Rect& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
        if (inRect(b0)) {
            ev.action = Action::UpdateNow;
            updatePrompt_ = false;
            dirty_ = true;
            return ev;
        } else if (inRect(b1)) {
            showingUpdateNotes_ = true;
            updateNotesScroll_ = 0;
            dirty_ = true;
            return ev;
        } else if (inRect(b2) || !inRect(panel)) {
            updatePrompt_ = false;
            dirty_ = true;
            return ev;
        }
        return ev;
    }

    if (countryPickerOpen_) {
        return ev;
    }

    // Check avatar / settings button click to open settings
    if (screen_ == Screen::Home && !searching_) {
        constexpr int kAvatarRight = kW - 120;
        Rect settingsBtn{kAvatarRight + (120 - 52) / 2, 46, 52, 52};
        if ((x >= settingsBtn.x - 12 && x <= settingsBtn.x + settingsBtn.w + 12 && y >= settingsBtn.y - 12 && y <= settingsBtn.y + settingsBtn.h + 12) ||
            (x >= kAvatarRight - 70 && x <= kW && y >= 30 && y <= 120)) {
            screen_ = Screen::Settings;
            settingsRow_ = 0;
            settingsFromError_ = false;
            settingsCountry_ = settings_.loginCountry;
            dropdownOpen_ = false;
            filterList_ = -1;
            settingsScrollY_ = 0.0f;
            dirty_ = true;
            return ev;
        }
    }

    // Touch exit from Settings back to Home
    if (screen_ == Screen::Settings) {
        Rect backBtn{kMargin, 94, 52, 52};
        if ((x >= backBtn.x - 20 && x <= backBtn.x + backBtn.w + 20 && y >= backBtn.y - 20 && y <= backBtn.y + backBtn.h + 20) ||
            (x >= backBtn.x && x <= backBtn.x + 260 && y >= 70 && y <= 160)) {
            if (dropdownOpen_) {
                dropdownOpen_ = false;
                dirty_ = true;
                return ev;
            }
            if (trophyDisclaimerOpen_) {
                trophyDisclaimerOpen_ = false;
                dirty_ = true;
                return ev;
            }
            if (removeTrophiesConfirmOpen_) {
                removeTrophiesConfirmOpen_ = false;
                dirty_ = true;
                return ev;
            }
            if (tester_ != Tester::None) {
                tester_ = Tester::None;
                dirty_ = true;
                ev.action = Action::SettingsChanged;
                ev.settings = settings_;
                return ev;
            }
            resetSettingsHoldStart_ = 0;
            screen_ = settingsFromError_ ? Screen::Error : Screen::Home;
            dirty_ = true;
            ev.action = settings_.loginCountry != settingsCountry_ ? Action::LoginCountry : Action::SettingsChanged;
            ev.settings = settings_;
            return ev;
        }
    }

    // Check top bar tabs
    if (screen_ == Screen::Home && !updatePrompt_ && !quitPrompt_ && !reportOpen_ && y >= 34 && y <= 110) {
        const char* labels[3] = {tr(Str::TabGamePass), tr(Str::YourGames), tr(Str::TabConsoles)};
        constexpr int kPx = 22, kPad = 26, kGap = 12;
        int widths[3], total = 0;
        for (int i = 0; i < 3; ++i) {
            widths[i] = fonts_.semibold.measure(labels[i], kPx) + 2 * kPad;
            total += widths[i + 0] + (i ? kGap : 0);
        }
        int tabX = (kW - total) / 2;
        for (int i = 0; i < 3; ++i) {
            if (x >= tabX - 8 && x < tabX + widths[i] + 8) {
                tab_ = static_cast<Tab>(i);
                searching_ = false;
                searchOnKeys_ = false;
                query_.clear();
                dirty_ = true;
                if (tab_ == Tab::Consoles) ev.action = Action::ConsolesShown;
                return ev;
            }
            tabX += widths[i] + kGap;
        }
    }

    if (screen_ == Screen::Home && searching_) {
        if (filterList_ >= 0) {
            Filter f = static_cast<Filter>(filterList_);
            int openRow = -1, openCol = -1;
            for (int r = 0; r < 3; ++r) {
                auto chips = filterRow(r);
                for (size_t c = 0; c < chips.size(); ++c) {
                    if (static_cast<int>(chips[c]) == filterList_) {
                        openRow = r;
                        openCol = static_cast<int>(c);
                        break;
                    }
                }
                if (openRow >= 0) break;
            }
            if (openRow >= 0) {
                const int gap = kChipGap, fh = 48;
                const int fy0 = kKeysY + (kKeyRows + 1) * (kKeyH + kKeyGap) + 8;
                constexpr int kCols = 3;
                const int chipW = (kKeysW - gap * (kCols - 1)) / kCols;
                int openChipX = kMargin + openCol * (chipW + gap);
                int openChipY = fy0 + openRow * (fh + 8);
                auto options = filterOptions(f);
                constexpr int kItemH = 54, kVisible = 9;
                int n = static_cast<int>(options.size()), visible = std::min(n, kVisible);
                int listW = std::max(chipW, 460);
                Rect panel{openChipX, std::max(120, openChipY - 10 - (visible * kItemH + 16)), listW, visible * kItemH + 16};
                if (x >= panel.x && x < panel.x + panel.w && y >= panel.y && y < panel.y + panel.h) {
                    for (int k = 0; k < visible; ++k) {
                        Rect item{panel.x + 8, panel.y + 8 + k * kItemH, panel.w - 16, kItemH};
                        if (x >= item.x && x < item.x + item.w && y >= item.y && y < item.y + item.h) {
                            int i = filterListTop_ + k;
                            if (i >= 0 && i < n) {
                                chooseFilterLocked(f, i);
                                filterList_ = -1;
                                runSearch();
                                dirty_ = true;
                                return ev;
                            }
                        }
                    }
                } else {
                    filterList_ = -1;
                    dirty_ = true;
                    return ev;
                }
            }
            return ev;
        }

        // Virtual keyboard keys
        for (int r = 0; r <= kKeyRows; ++r) {
            int n = r == kKeyRows ? 3 : kKeyCols;
            for (int k = 0; k < n; ++k) {
                int col = r == kKeyRows ? kWideCols[k] : k;
                int span = r == kKeyRows ? kWideSpan[k] : 1;
                Rect key{kMargin + col * (kKeyW + kKeyGap), kKeysY + r * (kKeyH + kKeyGap),
                         span * kKeyW + (span - 1) * kKeyGap, kKeyH};
                if (x >= key.x && x < key.x + key.w && y >= key.y && y < key.y + key.h) {
                    searchOnKeys_ = true;
                    keyRow_ = r;
                    keyCol_ = k;
                    if (r < kKeyRows) {
                        query_ += kKeys[r * kKeyCols + k];
                    } else if (k == 0) {
                        if (!query_.empty() && query_.back() != ' ') query_ += ' ';
                    } else if (k == 1) {
                        if (!query_.empty()) query_.pop_back();
                    } else {
                        query_.clear();
                    }
                    runSearch();
                    dirty_ = true;
                    return ev;
                }
            }
        }

        // Filter chips
        const int gap = kChipGap, fh = 48;
        const int fy0 = kKeysY + (kKeyRows + 1) * (kKeyH + kKeyGap) + 8;
        constexpr int kCols = 3;
        const int chipW = (kKeysW - gap * (kCols - 1)) / kCols;
        for (int row = 0; row < 3; ++row) {
            auto chips = filterRow(row);
            if (chips.empty()) continue;
            int fy = fy0 + row * (fh + 8);
            for (size_t k = 0; k < chips.size(); ++k) {
                Rect chip{kMargin + static_cast<int>(k) * (chipW + gap), fy, chipW, fh};
                if (x >= chip.x && x < chip.x + chip.w && y >= chip.y && y < chip.y + chip.h) {
                    searchOnKeys_ = true;
                    keyRow_ = kKeyRows + 1 + row;
                    keyCol_ = static_cast<int>(k);
                    pressFilter(chips[k]);
                    dirty_ = true;
                    return ev;
                }
            }
        }

        // Tap on query box to focus keyboard
        const int kbW = kKeyCols * (kKeyW + kKeyGap) - kKeyGap;
        Rect box{kMargin, 140, kbW, 76};
        if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) {
            searchOnKeys_ = true;
            dirty_ = true;
            return ev;
        }

        // Search results grid
        if (x >= kResultsX && y >= kGridTop - 20 && y < kH - 80) {
            int offset = static_cast<int>(std::lround(resultScroll_.value));
            int firstRow = std::max(0, (kGridTop - 20 - kGridTop + offset - kCard - 40) / kGridPitchY);
            size_t startIdx = static_cast<size_t>(firstRow) * static_cast<size_t>(kResultCols);
            for (size_t i = startIdx; i < results_.size(); ++i) {
                int col = static_cast<int>(i) % kResultCols;
                int row = static_cast<int>(i) / kResultCols;
                int cx = kResultsX + col * kCardPitch;
                int cy = kGridTop + row * kGridPitchY - offset;
                if (cy + kCard + 40 < kGridTop - 20) continue;
                if (cy > kH - 80) break;
                if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                    searchOnKeys_ = false;
                    resultFocus_ = static_cast<int>(i);
                    openDetails(results_[i]);
                    return ev;
                }
            }
        }
    }

    if (screen_ == Screen::Home && !searching_) {
        if (tab_ == Tab::GamePass && y >= 140 && y < kH - 80) {
            constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
            const int gridLeft = (kW - kGridW) / 2;
            int afterGridOffset = 0;
            for (size_t r = 0; r < rows_.size(); ++r) {
                float rel = static_cast<float>(r) - rowY_.value;
                int rowBaseY = kRowTop + static_cast<int>(std::lround(rel * kRowPitch)) + afterGridOffset;
                const GameRow& row = rows_[r];
                if (row.isGrid) {
                    int offset = static_cast<int>(std::lround(rowScroll_[r].value));
                    int cardY = rowBaseY - offset + 48;
                    for (size_t i = 0; i < row.tiles.size(); ++i) {
                        int c_col = static_cast<int>(i) % kLibraryCols;
                        int c_row = static_cast<int>(i) / kLibraryCols;
                        int cy = cardY + c_row * kGridPitchY;
                        int cx = gridLeft + c_col * kCardPitch;
                        if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                            focusRow_ = static_cast<int>(r);
                            focusCol_[r] = static_cast<int>(i);
                            openDetails(row.tiles[i]);
                            return ev;
                        }
                    }
                    int gridHeight = 48 + ((static_cast<int>(row.tiles.size()) + kLibraryCols - 1) / kLibraryCols) * kGridPitchY;
                    afterGridOffset += gridHeight - offset - kRowPitch;
                } else {
                    int cardY = rowBaseY + 48;
                    int scroll = static_cast<int>(std::lround(rowScroll_[r].value));
                    int firstCol = std::max(0, scroll / kCardPitch - 1);
                    for (size_t col = static_cast<size_t>(firstCol); col < row.tiles.size(); ++col) {
                        int cx = kMargin + static_cast<int>(col) * kCardPitch - scroll;
                        if (cx > kW) break;
                        if (x >= cx && x < cx + kCard && y >= cardY && y < cardY + kCard) {
                            focusRow_ = static_cast<int>(r);
                            focusCol_[r] = static_cast<int>(col);
                            openDetails(row.tiles[col]);
                            return ev;
                        }
                    }
                }
            }
        } else if (tab_ == Tab::Library) {
            if (y >= 150 && y < kH - 80) {
                constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
                const int left = (kW - kGridW) / 2;
                const LibraryLayout& L = libraryLayout();
                int offset = static_cast<int>(std::lround(gridScroll_.value));
                for (size_t i = 0; i < L.rowOf.size(); ++i) {
                    int cy = kLibraryTop + L.rowY[static_cast<size_t>(L.rowOf[i])] - offset;
                    int cx = left + L.colOf[i] * kCardPitch;
                    if (x >= cx && x < cx + kCard && y >= cy && y < cy + kCard) {
                        gridFocus_ = static_cast<int>(i);
                        const GameTile* t = libraryTile(static_cast<int>(i));
                        if (t) openDetails(*t);
                        return ev;
                    }
                }
            }
            constexpr int kAvatar = 52;
            int right = kW - kMargin - kAvatar - 16;
            if (x >= right - 200 && x <= right && y >= 110 && y <= 145) {
                librarySort_ = static_cast<LibrarySort>((static_cast<int>(librarySort_) + 1) %
                                                       static_cast<int>(LibrarySort::Count));
                rebuild(true);
                prefsEvent(ev);
                dirty_ = true;
                return ev;
            }
        } else if (tab_ == Tab::Consoles && y >= 320 && y < 700) {
            constexpr int kCardW = 480, kCardH = 380, kGap = 40, kTop = 320;
            for (size_t i = 0; i < consoles_.size(); ++i) {
                int cx = kMargin + static_cast<int>(i) * (kCardW + kGap);
                if (x >= cx && x < cx + kCardW && y >= kTop && y < kTop + kCardH) {
                    consoleFocus_ = static_cast<int>(i);
                    const ConsoleTile& con = consoles_[i];
                    ev.action = Action::Play;
                    ev.game.titleId = con.serverId;
                    ev.game.name = con.name.empty() ? "Xbox" : con.name;
                    ev.game.homeConsole = true;
                    dirty_ = true;
                    return ev;
                }
            }
        }
    } else if (screen_ == Screen::Details) {
        if (gameSettingsOpen_) {
            constexpr int kModalW = 1140, kModalH = 960;
            constexpr int kRowH = 38, kGap = 4, kGroupGap = 7;
            const auto rows = visibleGameSettingsRows(caps_, activeGameHasCustom_, activeGameMouseSupported_);
            const auto most = visibleGameSettingsRows(caps_, true, activeGameMouseSupported_), all = visibleGameSettingsRows(UiCaps{}, true, true);
            auto groups = [](const std::vector<SettingsRowPos>& v) {
                return static_cast<int>(std::count_if(v.begin(), v.end(), [](const SettingsRowPos& r) { return r.groupStart; }));
            };
            const int modalH = kModalH - static_cast<int>(all.size() - most.size()) * (kRowH + kGap) -
                               (groups(all) - groups(most)) * kGroupGap;
            Rect panel{(kW - kModalW) / 2, (kH - modalH) / 2, kModalW, modalH};
            int curY = panel.y + 92;
            for (int pos = 0; pos < static_cast<int>(rows.size()); ++pos) {
                if (rows[static_cast<size_t>(pos)].groupStart) curY += kGroupGap;
                Rect r{panel.x + 50, curY, panel.w - 100, kRowH};
                if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                    gameSettingsRow_ = pos;
                    handleGameSettings(NavInput{.accept = true}, ev);
                    dirty_ = true;
                    return ev;
                }
                curY += kRowH + kGap;
            }
            if (x < panel.x || x >= panel.x + panel.w || y < panel.y || y >= panel.y + panel.h) {
                handleGameSettings(NavInput{.back = true}, ev);
            }
            return ev;
        }
        if (achievementsOpen_) {
            return ev;
        }
        const GameTile* g = &detail_;
        if (g->productId.empty() && g->titleId.empty()) return ev;
        bool isF2P = isFreeGame(*g);
        bool effectivePlayable = isEffectivePlayableLocked(*g);
        auto inRect = [&](const Rect& r) {
            return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
        };
        if (!effectivePlayable && isF2P && !g->playable) {
            Rect claim = claimRect_.w > 0 ? claimRect_ : Rect{kMargin, 620, 380, 68};
            if (inRect(claim)) {
                detailButtonFocus_ = 0;
                ev.action = Action::ClaimFreeGame;
                ev.game = detail_;
                ev.game.freeInStore = true;
                return ev;
            }
        } else if (effectivePlayable) {
            Rect play = playRect_.w > 0 ? playRect_ : Rect{kMargin, 620, 300, 76};
            if (inRect(play)) {
                detailButtonFocus_ = 0;
                ev.action = Action::Play;
                ev.game = detail_;
                return ev;
            }
            if (inRect(achRect_) && !detail_.xboxTitleId.empty()) {
                detailButtonFocus_ = 1;
                openAchievements();
                return ev;
            }
        }
    } else if (screen_ == Screen::Settings) {
        if (trophyDisclaimerOpen_) {
            Rect panel{(kW - 1100) / 2, (kH - 520) / 2 - 30, 1100, 520};
            constexpr int kBtnW = 260, kBtnH = 58, kBtnGap = 40;
            int totalBtnsW = 2 * kBtnW + kBtnGap;
            int btnStartX = panel.x + (panel.w - totalBtnsW) / 2;
            int btnY = panel.y + panel.h - 90;
            Rect btn0{btnStartX, btnY, kBtnW, kBtnH};
            Rect btn1{btnStartX + kBtnW + kBtnGap, btnY, kBtnW, kBtnH};
            auto inRect = [&](const Rect& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
            if (inRect(btn0)) {
                settings_.syncTrophies = true;
                trophyDisclaimerOpen_ = false;
                dirty_ = true;
                ev.action = Action::SyncTrophies;
                ev.settings = settings_;
            } else if (inRect(btn1) || !inRect(panel)) {
                trophyDisclaimerOpen_ = false;
                dirty_ = true;
            }
            return ev;
        }
        if (removeTrophiesConfirmOpen_) {
            Rect panel{(kW - 1100) / 2, (kH - 480) / 2 - 30, 1100, 480};
            constexpr int kBtnW = 260, kBtnH = 58, kBtnGap = 40;
            int totalBtnsW = 2 * kBtnW + kBtnGap;
            int btnStartX = panel.x + (panel.w - totalBtnsW) / 2;
            int btnY = panel.y + panel.h - 90;
            Rect btn0{btnStartX, btnY, kBtnW, kBtnH};
            Rect btn1{btnStartX + kBtnW + kBtnGap, btnY, kBtnW, kBtnH};
            auto inRect = [&](const Rect& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
            if (inRect(btn0)) {
                settings_.syncTrophies = false;
                removeTrophiesConfirmOpen_ = false;
                dirty_ = true;
                ev.action = Action::RemoveTrophies;
                ev.settings = settings_;
            } else if (inRect(btn1) || !inRect(panel)) {
                removeTrophiesConfirmOpen_ = false;
                dirty_ = true;
            }
            return ev;
        }
        if (tester_ != Tester::None || pickerOpen_) {
            return ev;
        }
        if (dropdownOpen_) {
            auto options = settingOptions(settingsRow_);
            constexpr int kItemH = 62, kVisible = 8, kListW = 640;
            int n = static_cast<int>(options.size());
            int visible = std::min(n, kVisible);
            Rect panel{kMargin + kSettingsRowW - kListW, 184, kListW, visible * kItemH + 16};
            int curY = kSettingsListTop - static_cast<int>(std::lround(settingsScrollY_));
            for (const auto& [i, groupStart] : visibleSettingsRows(caps_)) {
                if (groupStart) curY += kSettingsGroupGap;
                if (i == settingsRow_) {
                    panel.y = curY + kSettingsRowH + 8;
                    break;
                }
                curY += kSettingsRowH + kSettingsGap;
            }
            if (panel.y + panel.h > kH - 100) panel.y = std::max(120, kH - 100 - panel.h);

            if (x >= panel.x && x < panel.x + panel.w && y >= panel.y && y < panel.y + panel.h) {
                for (int k = 0; k < visible; ++k) {
                    int i = dropdownTop_ + k;
                    Rect item{panel.x + 8, panel.y + 8 + k * kItemH, panel.w - 16, kItemH};
                    if (x >= item.x && x < item.x + item.w && y >= item.y && y < item.y + item.h && i < n) {
                        int before = settingSelected(settingsRow_);
                        applySetting(settingsRow_, i);
                        dropdownOpen_ = false;
                        dirty_ = true;
                        if (settingsRow_ == 6 && i == 1) {
                            openColourPicker(before);
                        } else if (settingsRow_ != 8) {
                            ev.action = settingsRow_ == 27 ? Action::StoreCountry : Action::SettingsChanged;
                            ev.settings = settings_;
                        }
                        return ev;
                    }
                }
            } else {
                dropdownOpen_ = false;
                settingsScrollY_ = 0.0f;
                dirty_ = true;
                return ev;
            }
        }
        int curY = kSettingsListTop - static_cast<int>(std::lround(settingsScrollY_));
        for (const auto& [i, groupStart] : visibleSettingsRows(caps_)) {
            if (groupStart) curY += kSettingsGroupGap;
            if (x >= kMargin && x < kMargin + kSettingsRowW && y >= curY && y < curY + kSettingsRowH &&
                y >= kSettingsClipTop && y < kH - 80) {
                settingsRow_ = i;
                if (i == 14) {
                    openPadTester();
                    return ev;
                } else if (i == 3 || i == 4 || i == 12) {
                    openTester(i == 3 ? Tester::Sticks : i == 4 ? Tester::Triggers : Tester::Keys);
                    return ev;
                } else if (i == 17) {
                    openTester(Tester::SignInCheck);
                    ev.action = Action::CheckSignIn;
                    return ev;
                } else if (i == 18) {
                    openTester(Tester::NetworkTest);
                    ev.action = Action::RunNetworkTest;
                    return ev;
                } else if (i == 24) {
                    if (!settings_.syncTrophies) {
                        trophyDisclaimerOpen_ = true;
                        trophyDisclaimerFocus_ = 0;
                    } else {
                        settings_.syncTrophies = false;
                        ev.action = Action::SettingsChanged;
                        ev.settings = settings_;
                    }
                    dirty_ = true;
                    return ev;
                } else if (i == 25) {
                    removeTrophiesConfirmOpen_ = true;
                    removeTrophiesFocus_ = 1;
                    dirty_ = true;
                    return ev;
                } else if (i == 19) {
                    openTester(Tester::Donation);
                    return ev;
                } else if (i == 7) {
                    if (!caps_.selfUpdate) {
                        if (caps_.openUrl) {
                            ev.action = Action::OpenUrl;
                            ev.url = kReleasesPage;
                        }
                    } else if (!updateAvailable_.empty() && !updateChecking_) {
                        ev.action = Action::UpdateNow;
                        ev.settings = settings_;
                    }
                    return ev;
                } else if (i == 10) {
                    int n = static_cast<int>(settingOptions(10).size());
                    if (n > 0) applySetting(10, (settingSelected(10) + 1) % n);
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                    dirty_ = true;
                    return ev;
                } else if (i == 23) {
                    return ev;
                } else if (i == 5) {
                    settings_.circleConfirms = !settings_.circleConfirms;
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                    dirty_ = true;
                    return ev;
                } else {
                    auto opts = settingOptions(i);
                    if (opts.size() > 2) {
                        dropdownOpen_ = true;
                        dropdownIndex_ = settingSelected(i);
                        dropdownTop_ = std::max(0, dropdownIndex_ - 4);
                        dirty_ = true;
                        return ev;
                    }
                    changeSetting(1);
                    if (i != 8) {
                        ev.action = Action::SettingsChanged;
                        ev.settings = settings_;
                    }
                    dirty_ = true;
                    return ev;
                }
            }
            curY += kSettingsRowH + kSettingsGap;
        }
    }
    return ev;
}

UiEvent AppUi::onMouseWheel(float dx, float dy) {
    std::lock_guard<std::mutex> lock(mutex_);
    UiEvent ev;
    mouseCursorVisible_ = true;
    mouseCursorVisibleUntil_ = platform::nowMs() + 3000;
    dirty_ = true;
    if (screen_ == Screen::Streaming || screen_ == Screen::Splash) {
        return ev;
    }
    if (screen_ == Screen::Home && updatePrompt_ && showingUpdateNotes_) {
        updateNotesScroll_ = std::clamp(updateNotesScroll_ - static_cast<int>(dy * 2), 0, updateNotesMaxScroll_);
        dirty_ = true;
        return ev;
    }
    if (screen_ == Screen::Home && !searching_ && !updatePrompt_ && !quitPrompt_ && !reportOpen_) {
        if (tab_ == Tab::Library) {
            const LibraryLayout& L = libraryLayout();
            float maxScroll = L.rowY.empty() ? 0.0f : static_cast<float>(std::max(0, L.rowY.back() + kGridPitchY - (kH - kLibraryTop)));
            gridScroll_.target = std::clamp(gridScroll_.target - dy * 140.0f, 0.0f, maxScroll);
            animating_ = true;
            dirty_ = true;
        } else if (tab_ == Tab::GamePass) {
            if (!rows_.empty()) {
                const GameRow& curRow = rows_[static_cast<size_t>(focusRow_)];
                if (curRow.isGrid) {
                    int totalRows = (static_cast<int>(curRow.tiles.size()) + kLibraryCols - 1) / kLibraryCols;
                    float maxScroll = std::max(0.0f, static_cast<float>(totalRows * kGridPitchY - (kH - kRowTop - 48)));
                    if (dy > 0 && rowScroll_[static_cast<size_t>(focusRow_)].target <= 0.0f) {
                        if (focusRow_ > 0) {
                            --focusRow_;
                            retarget();
                            animating_ = true;
                            dirty_ = true;
                        }
                    } else {
                        rowScroll_[static_cast<size_t>(focusRow_)].target =
                            std::clamp(rowScroll_[static_cast<size_t>(focusRow_)].target - dy * 140.0f, 0.0f, maxScroll);
                        animating_ = true;
                        dirty_ = true;
                    }
                } else {
                    if (std::abs(dy) > std::abs(dx)) {
                        int nextRow = std::clamp(static_cast<int>(rowY_.target - dy), 0, static_cast<int>(rows_.size()) - 1);
                        if (nextRow != focusRow_) {
                            focusRow_ = nextRow;
                            retarget();
                            animating_ = true;
                            dirty_ = true;
                        }
                    } else if (dx != 0.0f) {
                        float maxScroll = std::max(0.0f, static_cast<float>(static_cast<int>(curRow.tiles.size()) * kCardPitch - (kW - kMargin)));
                        rowScroll_[static_cast<size_t>(focusRow_)].target =
                            std::clamp(rowScroll_[static_cast<size_t>(focusRow_)].target - dx * 160.0f, 0.0f, maxScroll);
                        animating_ = true;
                        dirty_ = true;
                    }
                }
            }
        }
    } else if (screen_ == Screen::Details && achievementsOpen_) {
        achScroll_ = std::max(0, achScroll_ - static_cast<int>(dy * 3));
        dirty_ = true;
    } else if (screen_ == Screen::Settings && !dropdownOpen_ && tester_ == Tester::None && !pickerOpen_ && !trophyDisclaimerOpen_) {
        float maxScroll = settingsMaxScroll(caps_);
        settingsScrollY_ = std::clamp(settingsScrollY_ - dy * 120.0f, 0.0f, maxScroll);
        dirty_ = true;
    }
    return ev;
}

UiEvent AppUi::onTouch(const app::shell::TouchEvent& ev) {
    std::unique_lock<std::mutex> lock(mutex_);
    UiEvent uiEv;
    if (screen_ == Screen::Streaming || screen_ == Screen::Splash) return uiEv;

    uint64_t now = platform::nowMs();
    constexpr float kTouchSlopSq = 256.0f; // 16px threshold

    if (ev.kind == app::shell::TouchEvent::Kind::Down) {
        if (!touchTracker_.active) {
            touchTracker_.active = true;
            touchTracker_.fingerId = ev.fingerId;
            touchTracker_.startX = touchTracker_.lastX = ev.x;
            touchTracker_.startY = touchTracker_.lastY = ev.y;
            touchTracker_.startTimeMs = touchTracker_.lastTimeMs = now;
            touchTracker_.isDragging = false;
            touchTracker_.velocityX = touchTracker_.velocityY = 0.0f;
        }
        return uiEv;
    }

    if (!touchTracker_.active || touchTracker_.fingerId != ev.fingerId) return uiEv;

    if (ev.kind == app::shell::TouchEvent::Kind::Motion) {
        float dx = ev.x - touchTracker_.lastX;
        float dy = ev.y - touchTracker_.lastY;
        float totalDistSq = (ev.x - touchTracker_.startX) * (ev.x - touchTracker_.startX) +
                            (ev.y - touchTracker_.startY) * (ev.y - touchTracker_.startY);

        if (!touchTracker_.isDragging && totalDistSq >= kTouchSlopSq) {
            touchTracker_.isDragging = true;
        }

        if (touchTracker_.isDragging) {
            float dt = std::max(1.0f, static_cast<float>(now - touchTracker_.lastTimeMs));
            touchTracker_.velocityY = 0.8f * touchTracker_.velocityY + 0.2f * (dy / dt);
            touchTracker_.velocityX = 0.8f * touchTracker_.velocityX + 0.2f * (dx / dt);

            if (screen_ == Screen::Home && searching_ && touchTracker_.startX >= kResultsX) {
                int totalRows = (static_cast<int>(results_.size()) + kResultCols - 1) / kResultCols;
                float maxScroll = std::max(0.0f, static_cast<float>(totalRows * kGridPitchY - (kH - kGridTop - 80)));
                resultScroll_.target = std::clamp(resultScroll_.target - dy, 0.0f, maxScroll);
                resultScroll_.value = resultScroll_.target;
                animating_ = true;
                dirty_ = true;
            } else if (screen_ == Screen::Home && !searching_ && !updatePrompt_ && !quitPrompt_ && !reportOpen_) {
                if (tab_ == Tab::Library) {
                    const LibraryLayout& L = libraryLayout();
                    float maxScroll = L.rowY.empty() ? 0.0f : static_cast<float>(std::max(0, L.rowY.back() + kGridPitchY - (kH - kLibraryTop)));
                    gridScroll_.target = std::clamp(gridScroll_.target - dy, 0.0f, maxScroll);
                    gridScroll_.value = gridScroll_.target; // 1:1 finger tracking
                    animating_ = true;
                    dirty_ = true;
                } else if (tab_ == Tab::GamePass && !rows_.empty()) {
                    const GameRow& curRow = rows_[static_cast<size_t>(focusRow_)];
                    if (curRow.isGrid) {
                        int totalRows = (static_cast<int>(curRow.tiles.size()) + kLibraryCols - 1) / kLibraryCols;
                        float maxScroll = std::max(0.0f, static_cast<float>(totalRows * kGridPitchY - (kH - kRowTop - 48)));
                        if (dy > 0 && rowScroll_[static_cast<size_t>(focusRow_)].target <= 0.0f) {
                            // User is at top of grid and dragging down -> scroll back out to shelf above!
                            if (focusRow_ > 0) {
                                --focusRow_;
                                rowY_.target = std::clamp(static_cast<float>(focusRow_) - dy / static_cast<float>(kRowPitch),
                                                          0.0f, static_cast<float>(rows_.size() - 1));
                                rowY_.value = rowY_.target;
                                animating_ = true;
                                dirty_ = true;
                            }
                        } else {
                            float prevTarget = rowScroll_[static_cast<size_t>(focusRow_)].target;
                            float nextTarget = std::clamp(prevTarget - dy, 0.0f, maxScroll);
                            rowScroll_[static_cast<size_t>(focusRow_)].target = nextTarget;
                            rowScroll_[static_cast<size_t>(focusRow_)].value = nextTarget;
                            animating_ = true;
                            dirty_ = true;
                            if (dy > 0 && prevTarget > 0.0f && nextTarget == 0.0f && focusRow_ > 0) {
                                float overshoot = dy - prevTarget;
                                if (overshoot > 4.0f) {
                                    --focusRow_;
                                    rowY_.target = std::clamp(static_cast<float>(focusRow_) - overshoot / static_cast<float>(kRowPitch),
                                                              0.0f, static_cast<float>(rows_.size() - 1));
                                    rowY_.value = rowY_.target;
                                }
                            }
                        }
                    } else {
                        // Ribbon shelf: vertical drags move shelves, horizontal drags scroll cards
                        if (std::abs(dy) > std::abs(dx)) {
                            rowY_.target = std::clamp(rowY_.target - dy / static_cast<float>(kRowPitch),
                                                      0.0f, static_cast<float>(rows_.size() - 1));
                            rowY_.value = rowY_.target;
                            animating_ = true;
                            dirty_ = true;
                        } else {
                            float maxScroll = static_cast<float>(std::max(0, static_cast<int>(curRow.tiles.size()) * kCardPitch - kW));
                            rowScroll_[static_cast<size_t>(focusRow_)].target =
                                std::clamp(rowScroll_[static_cast<size_t>(focusRow_)].target - dx, 0.0f, maxScroll);
                            rowScroll_[static_cast<size_t>(focusRow_)].value = rowScroll_[static_cast<size_t>(focusRow_)].target;
                            animating_ = true;
                            dirty_ = true;
                        }
                    }
                }
            } else if (screen_ == Screen::Home && updatePrompt_ && showingUpdateNotes_) {
                updateNotesScroll_ = std::clamp(updateNotesScroll_ - static_cast<int>(dy), 0, updateNotesMaxScroll_);
                dirty_ = true;
            } else if (screen_ == Screen::Details && achievementsOpen_) {
                achScroll_ = std::max(0, achScroll_ - static_cast<int>(dy));
                dirty_ = true;
            } else if (screen_ == Screen::Settings && !dropdownOpen_ && tester_ == Tester::None && !pickerOpen_ && !trophyDisclaimerOpen_) {
                float maxScroll = settingsMaxScroll(caps_);
                settingsScrollY_ = std::clamp(settingsScrollY_ - dy, 0.0f, maxScroll);
                dirty_ = true;

                // Finger drag moves highlight cursor with the finger/scroll
                int curY = kSettingsListTop - static_cast<int>(std::lround(settingsScrollY_));
                auto rows = visibleSettingsRows(caps_);
                for (const auto& [i, groupStart] : rows) {
                    if (groupStart) curY += kSettingsGroupGap;
                    if (ev.y >= curY && ev.y < curY + kSettingsRowH) {
                        if (settingsRow_ != i) {
                            settingsRow_ = i;
                            resetSettingsHoldStart_ = 0;
                        }
                        break;
                    }
                    curY += kSettingsRowH + kSettingsGap;
                }
            }
        }
        touchTracker_.lastX = ev.x;
        touchTracker_.lastY = ev.y;
        touchTracker_.lastTimeMs = now;
        return uiEv;
    }

    if (ev.kind == app::shell::TouchEvent::Kind::Up) {
        if (touchTracker_.isDragging) {
            // Apply inertial deceleration if released with velocity
            if (std::abs(touchTracker_.velocityY) > 0.3f) {
                float flingDelta = touchTracker_.velocityY * 220.0f;
                if (screen_ == Screen::Home && searching_ && touchTracker_.startX >= kResultsX) {
                    int totalRows = (static_cast<int>(results_.size()) + kResultCols - 1) / kResultCols;
                    float maxScroll = std::max(0.0f, static_cast<float>(totalRows * kGridPitchY - (kH - kGridTop - 80)));
                    resultScroll_.target = std::clamp(resultScroll_.target - flingDelta, 0.0f, maxScroll);
                    animating_ = true;
                    dirty_ = true;
                } else if (tab_ == Tab::Library) {
                    const LibraryLayout& L = libraryLayout();
                    float maxScroll = L.rowY.empty() ? 0.0f : static_cast<float>(std::max(0, L.rowY.back() + kGridPitchY - (kH - kLibraryTop)));
                    gridScroll_.target = std::clamp(gridScroll_.target - flingDelta, 0.0f, maxScroll);
                    animating_ = true;
                    dirty_ = true;
                } else if (tab_ == Tab::GamePass && !rows_.empty()) {
                    const GameRow& curRow = rows_[static_cast<size_t>(focusRow_)];
                    if (curRow.isGrid) {
                        int totalRows = (static_cast<int>(curRow.tiles.size()) + kLibraryCols - 1) / kLibraryCols;
                        float maxScroll = std::max(0.0f, static_cast<float>(totalRows * kGridPitchY - (kH - kRowTop - 48)));
                        if (flingDelta < 0.0f && rowScroll_[static_cast<size_t>(focusRow_)].target - flingDelta < 0.0f && focusRow_ > 0) {
                            rowScroll_[static_cast<size_t>(focusRow_)].target = 0.0f;
                            --focusRow_;
                            rowY_.target = static_cast<float>(focusRow_);
                        } else {
                            rowScroll_[static_cast<size_t>(focusRow_)].target =
                                std::clamp(rowScroll_[static_cast<size_t>(focusRow_)].target - flingDelta, 0.0f, maxScroll);
                        }
                        animating_ = true;
                        dirty_ = true;
                    }
                } else if (screen_ == Screen::Settings && !dropdownOpen_ && tester_ == Tester::None && !pickerOpen_ && !trophyDisclaimerOpen_) {
                    float maxScroll = settingsMaxScroll(caps_);
                    settingsScrollY_ = std::clamp(settingsScrollY_ - flingDelta, 0.0f, maxScroll);
                    dirty_ = true;
                }
            }
            // Reconcile cursors upon touch drag release without snapping back
            if (screen_ == Screen::Home && !searching_ && !updatePrompt_ && !quitPrompt_ && !reportOpen_) {
                if (tab_ == Tab::Library) {
                    int topRow = std::max(0, static_cast<int>((gridScroll_.target + 20) / kGridPitchY));
                    const LibraryLayout& L = libraryLayout();
                    if (topRow < static_cast<int>(L.rowFirst.size())) {
                        gridFocus_ = L.rowFirst[static_cast<size_t>(topRow)];
                    }
                } else if (tab_ == Tab::GamePass && !rows_.empty()) {
                    focusRow_ = std::clamp(static_cast<int>(std::lround(rowY_.target)), 0, static_cast<int>(rows_.size()) - 1);
                    if (rows_[static_cast<size_t>(focusRow_)].isGrid) {
                        retarget();
                    } else {
                        rowY_.target = static_cast<float>(focusRow_);
                    }
                    animating_ = true;
                    dirty_ = true;
                }
            } else if (screen_ == Screen::Settings && !dropdownOpen_ && tester_ == Tester::None && !pickerOpen_ && !trophyDisclaimerOpen_) {
                auto rows = visibleSettingsRows(caps_);
                int curY = kSettingsListTop - static_cast<int>(std::lround(settingsScrollY_));
                int focusedY = -1;
                int firstVisibleRow = -1;
                int lastVisibleRow = -1;
                for (const auto& [i, groupStart] : rows) {
                    if (groupStart) curY += kSettingsGroupGap;
                    if (i == settingsRow_) focusedY = curY;
                    if (curY >= kSettingsClipTop && curY + kSettingsRowH <= kH - 80) {
                        if (firstVisibleRow < 0) firstVisibleRow = i;
                        lastVisibleRow = i;
                    }
                    curY += kSettingsRowH + kSettingsGap;
                }
                if (focusedY < kSettingsClipTop && firstVisibleRow >= 0) {
                    settingsRow_ = firstVisibleRow;
                    dirty_ = true;
                } else if (focusedY + kSettingsRowH > kH - 80 && lastVisibleRow >= 0) {
                    settingsRow_ = lastVisibleRow;
                    dirty_ = true;
                }
            }
            touchTracker_.active = false;
        } else {
            // Tap detected! Route to handlePointerDown without showing mouse cursor
            float tapX = touchTracker_.startX;
            float tapY = touchTracker_.startY;
            touchTracker_.active = false;
            lock.unlock();
            uiEv = handlePointerDown(tapX, tapY, 1, false);
        }
        return uiEv;
    }

    if (ev.kind == app::shell::TouchEvent::Kind::Cancel) {
        touchTracker_.active = false;
        return uiEv;
    }

    return uiEv;
}

UiEvent AppUi::handle(const NavInput& in) {
    std::lock_guard<std::mutex> lock(mutex_);
    return handleLocked(in);
}

UiEvent AppUi::handleLocked(const NavInput& in) {
    UiEvent ev;
    if (screen_ == Screen::Splash) {
        return ev;
    }
    if (reportOpen_) {  // the report's QR code: Circle (or Cross) closes it
        if (in.accept && caps_.openUrl) {
            // A desktop: Cross opens the form in the browser (the panel stays).
            ev.action = Action::OpenUrl;
            ev.url = reportUrl_;
        } else if (in.back || in.accept) {
            reportOpen_ = false, dirty_ = true;
        }
        return ev;
    }
    if (screen_ == Screen::Home && updatePrompt_) {
        if (showingUpdateNotes_) {
            if (in.up) {
                if (updateNotesScroll_ > 0) {
                    --updateNotesScroll_;
                    dirty_ = true;
                }
            } else if (in.down) {
                if (updateNotesScroll_ < updateNotesMaxScroll_) {
                    ++updateNotesScroll_;
                    dirty_ = true;
                }
            } else if (in.l1 || in.l2) {
                if (updateNotesScroll_ > 0) {
                    updateNotesScroll_ = std::max(0, updateNotesScroll_ - 5);
                    dirty_ = true;
                }
            } else if (in.r1 || in.r2) {
                if (updateNotesScroll_ < updateNotesMaxScroll_) {
                    updateNotesScroll_ = std::min(updateNotesMaxScroll_, updateNotesScroll_ + 5);
                    dirty_ = true;
                }
            } else if (in.scrollRows != 0) {
                int next = std::clamp(updateNotesScroll_ + in.scrollRows, 0, updateNotesMaxScroll_);
                if (next != updateNotesScroll_) {
                    updateNotesScroll_ = next;
                    dirty_ = true;
                }
            } else if (in.back || in.accept || in.square) {
                showingUpdateNotes_ = false;
                dirty_ = true;
            }
            return ev;
        }
        // 3 buttons: 0 Update now, 1 What's new, 2 Not now
        if (in.square) {
            showingUpdateNotes_ = true;
            updateNotesScroll_ = 0;
            formatUpdateNotes();
            dirty_ = true;
            return ev;
        }
        if (in.left) {
            promptFocus_ = (promptFocus_ + 2) % 3;
            dirty_ = true;
        } else if (in.right) {
            promptFocus_ = (promptFocus_ + 1) % 3;
            dirty_ = true;
        }
        if (in.accept) {
            if (promptFocus_ == 0) {
                ev.action = Action::UpdateNow;
                updatePrompt_ = false;
                dirty_ = true;
            } else if (promptFocus_ == 1) {
                showingUpdateNotes_ = true;
                updateNotesScroll_ = 0;
                formatUpdateNotes();
                dirty_ = true;
            } else {
                ev.action = Action::UpdateLater;
                updatePrompt_ = false;
                dirty_ = true;
            }
        } else if (in.back) {
            ev.action = Action::UpdateLater;
            updatePrompt_ = false;
            dirty_ = true;
        }
        return ev;
    }
    if (countryPickerOpen_) {
        // "Sign in as another country": up / down, Cross signs in as the
        // one chosen, Circle closes it unchanged.
        int n = 1 + static_cast<int>(std::size(kLoginCountries));
        if (in.down && countryIndex_ + 1 < n) ++countryIndex_, dirty_ = true;
        if (in.up && countryIndex_ > 0) --countryIndex_, dirty_ = true;
        if (in.accept) {
            settings_.loginCountry = countryIndex_ ? kLoginCountries[countryIndex_ - 1] : "";
            ev.action = Action::LoginCountry;
            ev.settings = settings_;
        }
        if (in.accept || in.back) countryPickerOpen_ = false, dirty_ = true;
        return ev;
    }
    switch (screen_) {
        case Screen::Home:
            if (quitPrompt_) {  // two buttons, Close / Cancel: left / right picks, Cross presses, Circle cancels
                if (in.left || in.right) promptFocus_ = in.left ? 0 : 1, dirty_ = true;
                if (in.accept && promptFocus_ == 0) ev.action = Action::Quit;
                if (in.accept || in.back) quitPrompt_ = false, dirty_ = true;
                break;
            }
            // A desktop window has no system menu to close it from (fullscreen,
            // a controller in hand): Circle / Esc at the top of the home screen
            // asks. On the PS5 the system closes the app; Circle does nothing here.
            if (caps_.desktopWindow && in.back && !searching_) {
                quitPrompt_ = true;
                promptFocus_ = 1;  // Cancel first: a second Circle / Esc never closes by accident
                dirty_ = true;
                break;
            }
            handleHome(in, ev);
            break;
        case Screen::SignIn:
            // Desktop or Android: Cross/A opens the sign-in page in the browser.
            if (in.accept && (caps_.openUrl
#if defined(__ANDROID__)
                || true
#endif
            )) {
                ev.action = Action::OpenUrl;
                ev.url = codeUrl_.empty() ? "https://www.microsoft.com/link" : codeUrl_;
                if (!code_.empty() && ev.url.find("otc=") == std::string::npos) {
                    ev.url += (ev.url.find('?') == std::string::npos ? "?otc=" : "&otc=") + code_;
                }
            }
            break;
        case Screen::Settings:
            if (trophyDisclaimerOpen_) {
                if (in.left || in.right) {
                    trophyDisclaimerFocus_ ^= 1;
                    dirty_ = true;
                } else if (in.back) {
                    trophyDisclaimerOpen_ = false;
                    dirty_ = true;
                } else if (in.accept) {
                    if (trophyDisclaimerFocus_ == 0) {
                        settings_.syncTrophies = true;
                        trophyDisclaimerOpen_ = false;
                        dirty_ = true;
                        ev.action = Action::SyncTrophies;
                        ev.settings = settings_;
                    } else {
                        trophyDisclaimerOpen_ = false;
                        dirty_ = true;
                    }
                }
                break;
            }
            if (removeTrophiesConfirmOpen_) {
                if (in.left || in.right) {
                    removeTrophiesFocus_ ^= 1;
                    dirty_ = true;
                } else if (in.back) {
                    removeTrophiesConfirmOpen_ = false;
                    dirty_ = true;
                } else if (in.accept) {
                    if (removeTrophiesFocus_ == 0) {
                        settings_.syncTrophies = false;
                        removeTrophiesConfirmOpen_ = false;
                        dirty_ = true;
                        ev.action = Action::RemoveTrophies;
                        ev.settings = settings_;
                    } else {
                        removeTrophiesConfirmOpen_ = false;
                        dirty_ = true;
                    }
                }
                break;
            }
            if (pickerOpen_) {
                handleColourPicker(in);
                if (!pickerOpen_ && in.accept) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
                break;
            }
            if (tester_ == Tester::SignInCheck) {
                if (in.back) {
                    tester_ = Tester::None;
                    dirty_ = true;
                } else if (in.accept && !signInCheck_.running) {
                    signInCheck_.running = true;
                    signInCheck_.stages = {
                        {"1. Account Storage", false, true, "Checking..."},
                        {"2. Microsoft OAuth (MSA)", false, true, "Waiting..."},
                        {"3. Xbox Live User Token", false, true, "Waiting..."},
                        {"4. XSTS Security Token", false, true, "Waiting..."},
                        {"5. xCloud Offering (xgpuweb)", false, true, "Waiting..."},
                        {"6. Recursos Preview (xCloud & Ads)", false, true, "Waiting..."},
                    };
                    dirty_ = true;
                    ev.action = Action::CheckSignIn;
                }
                break;
            }
            if (tester_ == Tester::NetworkTest) {
                if (in.back) {
                    tester_ = Tester::None;
                    dirty_ = true;
                } else if (in.accept && !networkTest_.running) {
                    networkTest_.running = true;
                    networkTest_.natStatus = "Probing UDP STUN...";
                    for (auto& reg : networkTest_.regions) {
                        reg.rttMs = -1;
                        reg.jitterMs = -1;
                    }
                    dirty_ = true;
                    ev.action = Action::RunNetworkTest;
                }
                break;
            }
            if (tester_ == Tester::Donation) {
                if (in.back || in.accept) {
                    tester_ = Tester::None;
                    dirty_ = true;
                }
                break;
            }
            if (tester_ != Tester::None) {
                handleTester(in);
                if (tester_ == Tester::None) {
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
                break;
            }
            if (dropdownOpen_) {
                // The list: up / down, Cross picks, Circle closes it unchanged.
                int n = static_cast<int>(settingOptions(settingsRow_).size());
                if (in.down && dropdownIndex_ + 1 < n) ++dropdownIndex_;
                if (in.up && dropdownIndex_ > 0) --dropdownIndex_;
                int before = settingSelected(settingsRow_);
                if (in.accept) applySetting(settingsRow_, dropdownIndex_);
                if (in.accept || in.back) dropdownOpen_ = false;
                if (in.accept && settingsRow_ == 6 && dropdownIndex_ == 1) {
                    openColourPicker(before);
                } else if (in.accept && settingsRow_ != 8) {
                    ev.action = settingsRow_ == 27 ? Action::StoreCountry : Action::SettingsChanged;
                    ev.settings = settings_;
                }
                dirty_ = true;
                break;
            }
            if (auto rows = visibleSettingsRows(caps_); true) {  // the rows on screen, in their order
                int pos = posOf(rows, settingsRow_);
                if (in.down && pos + 1 < static_cast<int>(rows.size())) {
                    settingsRow_ = rows[static_cast<size_t>(pos + 1)].row;
                    resetSettingsHoldStart_ = 0;
                    dirty_ = true;
                } else if (in.up && pos > 0) {
                    settingsRow_ = rows[static_cast<size_t>(pos - 1)].row;
                    resetSettingsHoldStart_ = 0;
                    dirty_ = true;
                }
                // Keep view scrolled so settingsRow_ is comfortably visible
                int unscaledY = kSettingsListTop;
                int focusedY = -1;
                for (const auto& [i, groupStart] : rows) {
                    if (groupStart) unscaledY += kSettingsGroupGap;
                    if (i == settingsRow_) {
                        focusedY = unscaledY;
                        break;
                    }
                    unscaledY += kSettingsRowH + kSettingsGap;
                }
                if (focusedY >= 0) {
                    float maxScroll = settingsMaxScroll(caps_);
                    float rowScreenY = static_cast<float>(focusedY) - settingsScrollY_;
                    if (rowScreenY < static_cast<float>(kSettingsClipTop + 10)) {
                        settingsScrollY_ = std::clamp(static_cast<float>(focusedY - (kSettingsClipTop + 10)), 0.0f, maxScroll);
                        dirty_ = true;
                    } else if (rowScreenY + kSettingsRowH > static_cast<float>(kH - 80 - 10)) {
                        settingsScrollY_ = std::clamp(static_cast<float>(focusedY + kSettingsRowH - (kH - 80 - 10)), 0.0f, maxScroll);
                        dirty_ = true;
                    }
                }
            }
            if (settingsRow_ == 23) {
                if (in.acceptHeld) {
                    if (!resetSettingsHoldStart_) resetSettingsHoldStart_ = in.nowMs ? in.nowMs : 1;
                    if (in.nowMs - resetSettingsHoldStart_ >= 5000) {
                        ev.action = Action::ResetSettings;
                        resetSettingsHoldStart_ = 0;
                    }
                    animating_ = true;
                    dirty_ = true;
                } else if (resetSettingsHoldStart_) {
                    resetSettingsHoldStart_ = 0;
                    dirty_ = true;
                }
            } else {
                resetSettingsHoldStart_ = 0;
            }
            if (in.right && settingsRow_ != 23 && settingsRow_ != 25) {
                changeSetting(+1);
                if (settingsRow_ != 8) {
                    ev.action = settingsRow_ == 27 ? Action::StoreCountry : Action::SettingsChanged;
                    ev.settings = settings_;
                }
            }
            if (in.left && settingsRow_ != 23 && settingsRow_ != 25) {
                changeSetting(-1);
                if (settingsRow_ != 8) {
                    ev.action = settingsRow_ == 27 ? Action::StoreCountry : Action::SettingsChanged;
                    ev.settings = settings_;
                }
            }
            if (in.square && settingsRow_ == 16) {
                ev.action = Action::TestQueueAlert;
            }
            if (in.square && settingsRow_ == 24) {
                if (trophySyncing_) {
                    // Sync already in progress; ignore repeated triggers
                } else if (settings_.syncTrophies) {
                    ev.action = Action::SyncTrophies;
                    ev.settings = settings_;
                } else {
                    trophyDisclaimerOpen_ = true;
                    trophyDisclaimerFocus_ = 0;
                    dirty_ = true;
                }
            }
            if (in.accept && settingsRow_ == 14) {
                openPadTester();  // controller buttons: their own screen
            } else if (in.accept && settingsRow_ == 15) {
                openTester(Tester::Gyro);
            } else if (in.accept && (settingsRow_ == 3 || settingsRow_ == 4 || settingsRow_ == 12)) {
                // Dead zone, trigger vibration, keyboard keys: their own screens instead of a list.
                openTester(settingsRow_ == 3 ? Tester::Sticks : settingsRow_ == 4 ? Tester::Triggers : Tester::Keys);
            } else if (in.accept && settingsRow_ == 17) {
                openTester(Tester::SignInCheck);
                ev.action = Action::CheckSignIn;
            } else if (in.accept && settingsRow_ == 18) {
                openTester(Tester::NetworkTest);
                ev.action = Action::RunNetworkTest;
            } else if (in.accept && settingsRow_ == 24) {
                if (!settings_.syncTrophies) {
                    trophyDisclaimerOpen_ = true;
                    trophyDisclaimerFocus_ = 0;
                    dirty_ = true;
                } else {
                    settings_.syncTrophies = false;
                    dirty_ = true;
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                }
            } else if (in.accept && settingsRow_ == 25) {
                removeTrophiesConfirmOpen_ = true;
                removeTrophiesFocus_ = 1;
                dirty_ = true;
            } else if (in.accept && settingsRow_ == 19) {
                openTester(Tester::Donation);
            } else if (in.accept && settingsRow_ == 7) {
                // Updates: a newer version is installed (the settings saved
                // first). Where the app doesn't install it, the release's
                // page opens in the browser instead.
                if (!caps_.selfUpdate) {
                    if (caps_.openUrl) {
                        ev.action = Action::OpenUrl;
                        ev.url = kReleasesPage;
                    }
                } else if (!updateAvailable_.empty() && !updateChecking_) {
                    ev.action = Action::UpdateNow;
                    ev.settings = settings_;
                }
            } else if (in.accept && settingsRow_ == 10) {
                // Inline bitrate slider: cycles to next option, no dropdown modal
                int n = static_cast<int>(settingOptions(10).size());
                if (n > 0) {
                    applySetting(10, (settingSelected(10) + 1) % n);
                    ev.action = Action::SettingsChanged;
                    ev.settings = settings_;
                    dirty_ = true;
                }
            } else if (in.accept && settingsRow_ == 23) {
                // Handled via hold
            } else if (in.accept) {
                dropdownOpen_ = true;
                dropdownIndex_ = settingSelected(settingsRow_);
                dropdownTop_ = 0;
                dirty_ = true;
            }
            if (in.back || in.options) {
                resetSettingsHoldStart_ = 0;
                // Back where Settings was opened. A new sign-in country
                // takes a new sign-in.
                screen_ = settingsFromError_ ? Screen::Error : Screen::Home;
                dirty_ = true;
                ev.action = settings_.loginCountry != settingsCountry_ ? Action::LoginCountry : Action::SettingsChanged;
                ev.settings = settings_;
            } else if (in.triangle) {
                ev.action = Action::Report;  // "Report a problem"
            }
            break;
        case Screen::Details:
            if (achievementsOpen_) {
                if (in.up && achScroll_ > 0) {
                    achScroll_--;
                    dirty_ = true;
                }
                int maxScroll = 0;
                if (auto it = achievements_.find(detail_.xboxTitleId); it != achievements_.end()) {
                    maxScroll = std::max(0, static_cast<int>(it->second.items.size()) - 5);
                }
                if (in.down && achScroll_ < maxScroll) {
                    achScroll_++;
                    dirty_ = true;
                }
                if (in.back || in.l3) {
                    achievementsOpen_ = false;
                    dirty_ = true;
                }
                break;
            }
            if (gameSettingsOpen_) {
                handleGameSettings(in, ev);
                break;
            }
            if (in.options) {
                openGameSettings();
                dirty_ = true;
                break;
            }
            {
                bool isF2P = isFreeGame(detail_);
                bool effectivePlayable = isEffectivePlayableLocked(detail_);
                bool hasAchievements = effectivePlayable && !detail_.xboxTitleId.empty();
                if (!hasAchievements && detailButtonFocus_ == 1) {
                    detailButtonFocus_ = 0;
                }
                if (in.right && hasAchievements && detailButtonFocus_ == 0) {
                    detailButtonFocus_ = 1;
                    dirty_ = true;
                } else if (in.left && detailButtonFocus_ == 1) {
                    detailButtonFocus_ = 0;
                    dirty_ = true;
                }
                if (in.accept) {
                    if (detailButtonFocus_ == 1 && hasAchievements) {
                        openAchievements();
                    } else if (!effectivePlayable && isF2P && !detail_.playable) {
                        ev.action = Action::ClaimFreeGame;
                        ev.game = detail_;
                        ev.game.freeInStore = true;
                    } else if (effectivePlayable) {
                        ev.action = Action::Play;
                        ev.game = detail_;
                    }
                }
            }
            if (in.square) toggleHidden(detail_, ev);  // the page stays open
            if (in.triangle) toggleFavorite(detail_, ev);
            if (in.r3 && !isAcquired(detail_)) toggleWishlist(detail_, ev);
            if (in.l3 && !detail_.xboxTitleId.empty()) openAchievements();
            if (in.back) {
                syncHeroToRowsLocked(detail_.productId, detail_.titleId, detail_.heroUrl, detail_.description, detail_.publisher, detail_.categories);
                screen_ = Screen::Home;
                dirty_ = true;
            }
            break;
        case Screen::Launching:
            if (in.back) ev.action = Action::CancelLaunch;
            break;
        case Screen::Error:
            if (!errorGame_.titleId.empty() || !errorGame_.productId.empty()) {
                if (in.accept) {
                    if ((isFreeGame(errorGame_) || errorGame_.freeInStore) && !errorGame_.playable) {
                        ev.action = Action::ClaimFreeGame;
                        ev.game = errorGame_;
                        ev.game.freeInStore = true;
                    } else if (errorGame_.playable && !errorGame_.titleId.empty()) {
                        ev.action = Action::Play;
                        ev.game = errorGame_;
                    } else {
                        openDetails(errorGame_);
                    }
                } else if (in.back && errorGame_.homeConsole) {
                    screen_ = Screen::Home;  // back to My consoles
                    tab_ = Tab::Consoles;
                    dirty_ = true;
                } else if (in.back) {
                    openDetails(errorGame_);
                }
            } else if (in.accept) {
                ev.action = Action::Retry;
            } else if ((in.square || in.back) && !regionRefused_ && (!rows_.empty() || !owned_.empty())) {
                screen_ = Screen::Home;
                loading_ = false;
                dirty_ = true;
            }
            if (regionRefused_ && in.square) {
                countryPickerOpen_ = true;
                countryIndex_ = settingSelected(8);
                dirty_ = true;
            } else if (regionRefused_ && in.options) {  // Settings, on the sign-in country
                screen_ = Screen::Settings;
                settingsRow_ = 8;
                settingsFromError_ = true;
                settingsCountry_ = settings_.loginCountry;
                dropdownOpen_ = false;
                settingsScrollY_ = 0.0f;
                dirty_ = true;
            }
            if (in.triangle) {  // "Report a problem", with the error shown
                ev.action = Action::Report;
                ev.reportWhat = error_;
            }
            break;
        default: break;
    }
    // Sign out: hold TOUCHPAD for 5 s on the home or error screen.
    bool canSignOut = screen_ == Screen::Home || screen_ == Screen::Error;
    if (canSignOut && in.touchpad && !signOutLatched_) {
        if (!signOutHoldStart_) signOutHoldStart_ = in.nowMs ? in.nowMs : 1;
        if (in.nowMs - signOutHoldStart_ >= kSignOutHoldMs) {
            ev.action = Action::SignOut;
            signOutHoldStart_ = 0;
            signOutLatched_ = true;
        }
        dirty_ = true;
    } else if (!in.touchpad || !canSignOut) {
        if (signOutHoldStart_) dirty_ = true;
        signOutHoldStart_ = 0;
        if (!in.touchpad) signOutLatched_ = false;
    }
    if (displayModeChanged_) {
        // A desktop's display mode applies (and is saved) as it is picked,
        // not when Settings closes.
        displayModeChanged_ = false;
        if (ev.action == Action::None) {
            ev.action = Action::SettingsChanged;
            ev.settings = settings_;
        }
    }
    return ev;
}

// --- Rendering -----------------------------------------------------------------

}  // namespace xc::ui
