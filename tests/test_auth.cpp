// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "test_harness.h"

#include "app/auto_deband.h"
#include "app/crash_guard.h"
#include "app/gyro_calibration.h"
#include "app/library.h"
#include "app/report.h"
#include "app/settings.h"
#include "app/settings_manager.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "app/web_server.h"
#include "auth/account_seal.h"
#include "auth/auth_manager.h"
#include "auth/msa.h"
#include "auth/xbox_live.h"
#include "display/display.h"
#include "display/gpu.h"
#include "input/controller.h"
#include "input/gyro.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "input/pad_map.h"
#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include "input/sdl_mapping.h"
#include "input/tuning.h"
#include "input/usb_xray.h"
#include "media/audio_encoder.h"
#include "media/audio_in.h"
#include "media/audio_in_common.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "media/decoder.h"
#include "media/hw_decoder.h"
#include "net/http.h"
#include "net/socket.h"
#include "platform/handheld.h"
#include "platform/platform.h"
#include "platform/toasts.h"
#include "stream/h264_receiver.h"
#include "stream/input_packet.h"
#include "stream/stream_session.h"
#include "trophy/trophy_backend.h"
#include "trophy/trophy_registry.h"
#include "trophy/trophy_sync.h"
#include "ui/accent_color.h"
#include "ui/app_ui.h"
#include "ui/app_ui_internal.h"
#include "ui/cloud_time.h"
#include "ui/image_cache.h"
#include "ui/input_mode.h"
#include "ui/pad_glyphs.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "ui/touch_pad_overlay.h"
#include "util/fs.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/achievements.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/prices.h"
#include "xcloud/purchase.h"
#include "xcloud/regions.h"
#include "xcloud/titlehub.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace xc;

static void testLoginCountry() {
    // Every country the UI offers has an address and a name; off and
    // anything else sends none.
    for (const char* country : ui::kLoginCountries) {
        const char* address = xcloud::loginCountryAddress(country);
        CHECK(address && std::string(address).find('.') != std::string::npos);
        CHECK(!ui::loginCountryName(country).empty());
    }
    CHECK(!xcloud::loginCountryAddress("") && !xcloud::loginCountryAddress("xx") && !xcloud::loginCountryAddress("BR"));
    CHECK(ui::loginCountryName("xx").empty());
    // Saved, and off when the file has none.
    std::string path = "/tmp/xcloud-tests-country.json";
    app::Settings s;
    s.loginCountry = "jp";
    CHECK(s.save(path));
    app::Settings back;
    CHECK(back.load(path) && back.loginCountry == "jp");
    xc::platform::writeFileAtomic(path, "{}");
    app::Settings none;
    CHECK(none.load(path) && none.loginCountry.empty());
    std::remove(path.c_str());
    // No login yet, or a new sign-in: not refused.
    xcloud::GssvClient gssv;
    CHECK(!gssv.loginRefused() && gssv.loginStatus() == 0);
}

// Protobuf wire format for the Content Access tests.
static std::string pbVarint(uint64_t v) {
    std::string s;
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        s += static_cast<char>(b | (v ? 0x80 : 0));
    } while (v);
    return s;
}
static std::string pbInt(int field, uint64_t v) { return pbVarint(static_cast<uint64_t>(field) << 3) + pbVarint(v); }
static std::string pbMsg(int field, const std::string& body) {
    return pbVarint((static_cast<uint64_t>(field) << 3) | 2) + pbVarint(body.size()) + body;
}
// A product as Content Access sends it: its ID, then for each user (PUID)
// the access types and, when timed, the streaming gameplay time.
static std::string pbProduct(const std::string& id, const std::string& puid, int64_t remaining, int64_t max) {
    std::string streaming = pbInt(1, 2);
    if (remaining >= 0) streaming += pbInt(2, static_cast<uint64_t>(remaining));
    if (max >= 0) streaming += pbInt(4, static_cast<uint64_t>(max));
    std::string access = pbInt(1, 1) + pbInt(2, 1788310800u) + pbMsg(11, streaming);  // aggregate types: a big bit mask
    return pbMsg(2, pbMsg(1, id) + pbMsg(2, pbMsg(2, pbMsg(1, puid) + pbMsg(2, access))));
}

static void testPlaytimeLimit() {
    int64_t rem = -1, max = -1;
    const std::string passes = pbMsg(1, pbMsg(1, "puid-1") + pbMsg(2, pbMsg(1, pbMsg(1, "CFQ7TTC0KHS0") + pbMsg(2, pbInt(1, 0)))));

    // 1. The account's allowance: the same remaining / max on its limited
    // products (15 h a month, 9 h 30 left), not the access bit masks.
    std::string body = passes + pbProduct("9NBLGGH4R315", "puid-1", 34200, 54000) +
                       pbProduct("BT5P2X999VH2", "puid-1", 34200, 54000) + pbProduct("9PNCL2R6G8D0", "puid-1", 34200, 54000) +
                       pbProduct("9NZQPT0MWTD0", "puid-1", -1, -1);
    auth::ContentAccessPlaytime t;
    CHECK(auth::parseContentAccessPlaytime(body, t));
    CHECK(t.products == 4 && t.limited == 3 && t.byProduct.size() == 3);
    CHECK(t.accountRemaining == 34200 && t.accountMax == 54000);
    CHECK(t.byProduct["BT5P2X999VH2"].first == 34200 && t.byProduct["BT5P2X999VH2"].second == 54000);
    CHECK(auth::parsePlaytimeLimit(body, rem, max) && rem == 34200 && max == 54000);

    // 2. One timed product (a game's own free time): no account allowance.
    std::string one = passes + pbProduct("BT5P2X999VH2", "puid-1", 0, 3600) + pbProduct("9NZQPT0MWTD0", "puid-1", -1, -1);
    CHECK(auth::parseContentAccessPlaytime(one, t));
    CHECK(t.limited == 1 && t.accountRemaining == -1 && t.byProduct["BT5P2X999VH2"].first == 0);
    CHECK(!auth::parsePlaytimeLimit(one, rem, max) && rem == -1 && max == -1);

    // 3. Another user's access (a family member) is not this account's.
    std::string other = passes + pbProduct("A", "puid-2", 100, 200) + pbProduct("B", "puid-2", 100, 200) +
                        pbProduct("C", "puid-2", 100, 200);
    CHECK(auth::parseContentAccessPlaytime(other, t) && t.byProduct.empty() && t.accountRemaining == -1);

    // 4. JSON: the same rule over its objects.
    std::string json = R"({"products":[{"productId":"A","remainingGameplayTimeInSeconds":7200,"maxGameplayTimeInSeconds":18000},
        {"productId":"B","remainingGameplayTimeInSeconds":"7200","maxGameplayTimeInSeconds":"18000"},
        {"productId":"C","remainingGameplayTimeInSeconds":7200,"maxGameplayTimeInSeconds":18000}]})";
    CHECK(auth::parsePlaytimeLimit(json, rem, max) && rem == 7200 && max == 18000);

    // 5. The account's own record (top-level field 3): its first bucket.
    auto account = [&](uint64_t remaining, uint64_t max) {
        std::string bucket = pbInt(2, remaining) + pbInt(3, 1791335750u) + pbInt(4, max) + pbInt(5, 1);
        std::string other = pbInt(3, 1791335750u) + pbInt(5, 1);
        return pbMsg(3, pbMsg(1, "puid-1") + pbMsg(2, pbMsg(2, bucket) + pbMsg(3, other) + pbMsg(4, other)));
    };
    // As this console's Ultimate account answered on 2026-11-04: no limit
    // (all bits set), and no product with a time of its own.
    std::string unlimited = passes + pbProduct("9NZQPT0MWTD0", "puid-1", -1, -1) + account(~0ull, ~0ull);
    CHECK(auth::parseContentAccessPlaytime(unlimited, t));
    CHECK(t.unlimited && t.accountRemaining == -1 && t.accountBuckets.size() == 3);
    CHECK(t.accountBuckets[2].first == auth::ContentAccessPlaytime::kUnlimited && t.accountBuckets[3].first == -1);
    CHECK(!auth::parsePlaytimeLimit(unlimited, rem, max));
    // A limited account: 15 h a month, 4 h 10 min left; the record wins over
    // the products' own times.
    std::string limited = passes + pbProduct("A", "puid-1", 100, 200) + pbProduct("B", "puid-1", 100, 200) +
                          pbProduct("C", "puid-1", 100, 200) + account(15000, 54000);
    CHECK(auth::parseContentAccessPlaytime(limited, t) && !t.unlimited);
    CHECK(t.accountRemaining == 15000 && t.accountMax == 54000);
    CHECK(auth::parsePlaytimeLimit(limited, rem, max) && rem == 15000 && max == 54000);

    // 6. Empty and invalid answers.
    CHECK(!auth::parsePlaytimeLimit("", rem, max));
    CHECK(!auth::parsePlaytimeLimit("not a valid payload", rem, max));
    CHECK(!auth::parseContentAccessPlaytime(std::string("\x0a\x05\x01", 3), t));  // length past the end
}

// A title refused for the device kind: recognised, and the Samsung TV identity is a well-formed TV.

static void testTokenRefresh() {
    using namespace xc::auth;
    CHECK(refreshDue(0, 1000));                // never logged in
    CHECK(refreshDue(900, 1000));              // expired
    CHECK(refreshDue(1000 + 600, 1000));       // within the margin
    CHECK(!refreshDue(1000 + 601, 1000));
    CHECK(parseIsoUtc("1970-01-02T00:00:00Z") == 86400);
    CHECK(parseIsoUtc("2026-10-19T09:12:34.1234567Z") == 1792401154);
    CHECK(parseIsoUtc("2024-02-29T23:59:59Z") == 1709251199);
    CHECK(parseIsoUtc("") == 0 && parseIsoUtc("soon") == 0 && parseIsoUtc("2026-13-01T00:00:00Z") == 0);

    std::string dir = "/tmp/xcloud-auth-test";
    xc::fs::removeTree(dir);
    xc::fs::makeDirs(dir);
    std::string store = dir + "/account.json";
    xc::platform::writeFileAtomic(store, R"({"version":1,"clientId":"000000004c20a908","refreshToken":"_rt-0"})");
    int64_t xstsLife = 16 * 3600, loginLife = 300;
    int serial = 0, logins = 0;
    bool refuseRefresh = false;
    std::map<std::string, int> hits;
    auto iso = [](int64_t t) {
        std::time_t tt = static_cast<std::time_t>(t);
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &tt);
#else
        gmtime_r(&tt, &utc);
#endif
        char text[40];
        std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S.0000000Z", &utc);
        return std::string(text);
    };
    net::setTestTransport([&](const net::Request& req) {
        net::Response r;
        r.status = 200;
        const std::string& u = req.url;
        if (u.find("login.live.com/oauth20_token.srf") != std::string::npos) {
            ++hits["msa"];
            if (refuseRefresh) {
                r.status = 400;
                r.body = R"({"error":"invalid_grant"})";
            } else {
                r.body = R"({"access_token":"at","expires_in":3600,"refresh_token":"_rt-)" + std::to_string(++serial) + R"("})";
            }
        } else if (u.find("user.auth.xboxlive.com") != std::string::npos) {
            ++hits["user"];
            r.body = R"({"Token":"ut","NotAfter":")" + iso(unixNow() + 14 * 86400) + R"(","DisplayClaims":{"xui":[{"uhs":"h"}]}})";
        } else if (u.find("xsts.auth.xboxlive.com") != std::string::npos) {
            ++hits["xsts"];
            r.body = R"({"Token":"xt)" + std::to_string(serial) + R"(","NotAfter":")" + iso(unixNow() + xstsLife) +
                     R"(","DisplayClaims":{"xui":[{"uhs":"h","gtg":"Tester","xid":"1"}]}})";
        } else if (u.find("gssv-play-prod.xboxlive.com/v2/login/user") != std::string::npos) {
            ++hits["login"];
            r.body = R"({"gsToken":"gs)" + std::to_string(++logins) + R"(","market":"US","durationInSeconds":)" +
                     std::to_string(loginLife) +
                     R"(,"offeringSettings":{"regions":[{"name":"EastUS","baseUri":"https://eus.example","isDefault":true},)"
                     R"({"name":"BrazilSouth","baseUri":"https://brs.example","isDefault":false}]}})";
        } else {
            ++hits["other"];  // gamerpic, plan, playtime: optional
            r.status = 404;
            r.body = "{}";
        }
        return r;
    });
    auto storedRefresh = [&] {
        std::string text;
        xc::platform::readFile(store, text);
        CHECK(text.find("_rt-") == std::string::npos);  // sealed: no token readable in the file
        if (std::string plain; xc::auth::unsealAccount(text, plain)) text = plain;
        auto j = json::parse(text);
        return j ? (*j)["refreshToken"].str() : std::string();
    };
    std::string err;
    bool renewed = true;
    {
        AuthManager am(store);
        xcloud::GssvClient gssv;
        CHECK(!am.ensureFresh(&gssv, err, false, &renewed) && !renewed && hits.empty());  // not signed in: nothing sent
        CHECK(am.signIn(gssv, {}, err) && am.signedIn());
        CHECK(storedRefresh() == "_rt-1");
        // The xCloud login expires in 5 minutes: only it is renewed, in the region chosen.
        gssv.setRegion(gssv.session().regions[1]);
        std::string token = gssv.session().gsToken;
        loginLife = 14400;
        hits.clear();
        CHECK(am.ensureFresh(&gssv, err, false, &renewed) && renewed);
        CHECK(hits.size() == 1 && hits["login"] == 1);
        CHECK(gssv.session().gsToken != token && gssv.region().name == "BrazilSouth");
        // Nothing due: nothing sent.
        hits.clear();
        CHECK(am.ensureFresh(&gssv, err, false, &renewed) && !renewed && hits.empty());
        CHECK(am.ensureFresh(nullptr, err, false, &renewed) && !renewed && hits.empty());
        // A refused request: the whole chain, without the profile's extras;
        // the rotated refresh token saved at once.
        CHECK(am.ensureFresh(&gssv, err, true, &renewed) && renewed);
        CHECK(hits["msa"] == 1 && hits["user"] == 1 && hits["xsts"] == 2 && hits["login"] == 1 && hits["other"] == 0);
        CHECK(storedRefresh() == "_rt-2" && !am.profile().xblAuthorization.empty());
        CHECK(gssv.region().name == "BrazilSouth");
        // The XSTS about to expire: renewed without being asked.
        xstsLife = 120;
        CHECK(am.ensureFresh(&gssv, err, true, &renewed));  // now holds a 2-minute XSTS
        xstsLife = 16 * 3600;
        hits.clear();
        CHECK(am.ensureFresh(&gssv, err, false, &renewed) && renewed && hits["msa"] == 1 && hits["login"] == 1);
        hits.clear();
        CHECK(am.ensureFresh(&gssv, err, false, &renewed) && !renewed && hits.empty());
        // The refresh refused: false, and the stored account stays.
        refuseRefresh = true;
        std::string before = storedRefresh();
        CHECK(!am.ensureFresh(&gssv, err, true, &renewed) && !renewed);
        CHECK(err.find("token refresh failed") != std::string::npos);
        CHECK(storedRefresh() == before && am.hasStoredAccount());
        CHECK(err.find(before) == std::string::npos);  // no token in the message

        // signIn failure: 500 error preserves account.json; invalid_grant deletes it
        {
            AuthManager amTransient(store);
            net::setTestTransport([&](const net::Request&) {
                net::Response r;
                r.status = 500;
                r.body = "Internal Server Error";
                return r;
            });
            CHECK(!amTransient.signIn(gssv, {}, err));
            CHECK(storedRefresh() == before && amTransient.hasStoredAccount());

            // A page that isn't the sign-in service's (a proxy, a captive portal) on a 4xx: nothing is concluded.
            int codeRequests = 0;
            net::setTestTransport([&](const net::Request& req) {
                net::Response r;
                if (req.url.find("oauth20_connect") != std::string::npos) ++codeRequests;
                r.status = 403;
                r.body = "<html>Blocked</html>";
                return r;
            });
            CHECK(!amTransient.signIn(gssv, {}, err));
            CHECK(codeRequests == 0 && storedRefresh() == before);

            // Refused by the sign-in service for a reason other than invalid_grant: the same answer for
            // ever, so the code is asked for (and the file stays until a new sign-in replaces it).
            {
                AuthManager amRefused(store);
                net::setTestTransport([&](const net::Request& req) {
                    net::Response r;
                    if (req.url.find("oauth20_connect") != std::string::npos ||
                        req.url.find("oauth2/v2.0/devicecode") != std::string::npos) {
                        ++codeRequests;
                        r.status = 500;
                        return r;
                    }
                    r.status = 400;
                    r.body = R"({"error":"invalid_request","error_description":"x"})";
                    return r;
                });
                CHECK(!amRefused.signIn(gssv, {}, err));
                CHECK(codeRequests >= 1);
                CHECK(storedRefresh() == before);
            }

            net::setTestTransport([&](const net::Request&) {
                net::Response r;
                r.status = 400;
                r.body = R"({"error":"invalid_grant"})";
                return r;
            });
            CHECK(!amTransient.signIn(gssv, {}, err));
            CHECK(storedRefresh().empty() && !amTransient.hasStoredAccount());
        }
    }
    // OAuth v2.0 (play.xbox.com client id): device code + d= ticket + Passport transfer token
    // without overwriting the v2 refresh token, plus prefilled ?otc=CODE sign-in URL.
    {
        std::string v2Store = dir + "/account_v2.json";
        std::remove(v2Store.c_str());
        bool sawV2DeviceCode = false, sawV2Token = false, sawDTicket = false, sawPassportTransfer = false;
        net::setTestTransport([&](const net::Request& req) {
            net::Response r;
            r.status = 200;
            const std::string& u = req.url;
            if (u.find("login.microsoftonline.com/consumers/oauth2/v2.0/devicecode") != std::string::npos) {
                sawV2DeviceCode = (req.body.find(kV2ClientId) != std::string::npos);
                r.body = R"({"user_code":"ABCD1234","device_code":"dc1","verification_uri":"https://www.microsoft.com/link","interval":1,"expires_in":900})";
            } else if (u.find("login.microsoftonline.com/consumers/oauth2/v2.0/token") != std::string::npos) {
                sawV2Token = (req.body.find(kV2ClientId) != std::string::npos);
                r.body = std::string(R"({"access_token":"v2_at","expires_in":3600,"refresh_)") + R"(token":"v2_rt_main"})";
            } else if (u.find("login.live.com/oauth20_token.srf") != std::string::npos) {
                sawPassportTransfer = (req.body.find("PURPOSE_XBOX_CLOUD_CONSOLE_TRANSFER_TOKEN") != std::string::npos &&
                                       req.body.find(kV2ClientId) != std::string::npos);
                r.body = std::string(R"({"access_token":"passport_transfer","expires_in":3600,"refresh_)") + R"(token":"passport_rt_ignored"})";
            } else if (u.find("user.auth.xboxlive.com") != std::string::npos) {
                sawDTicket = (req.body.find("\"RpsTicket\":\"d=v2_at\"") != std::string::npos);
                r.body = R"({"Token":"ut","NotAfter":")" + iso(unixNow() + 14 * 86400) + R"(","DisplayClaims":{"xui":[{"uhs":"h"}]}})";
            } else if (u.find("xsts.auth.xboxlive.com") != std::string::npos) {
                r.body = R"({"Token":"xt","NotAfter":")" + iso(unixNow() + 16 * 3600) +
                         R"(","DisplayClaims":{"xui":[{"uhs":"h","gtg":"V2Tester","xid":"2"}]}})";
            } else if (u.find("gssv-play-prod.xboxlive.com/v2/login/user") != std::string::npos) {
                r.body = R"({"gsToken":"gs_v2","market":"US","durationInSeconds":3600,"offeringSettings":{"regions":[{"name":"EastUS","baseUri":"https://eus.example","isDefault":true}]}})";
            } else {
                r.status = 404;
            }
            return r;
        });
        AuthManager amV2(v2Store);
        xcloud::GssvClient gssvV2;
        std::string errV2;
        CHECK(amV2.signIn(gssvV2, {}, errV2));
        CHECK(sawV2DeviceCode && sawV2Token && sawDTicket);
        std::string transfer;
        CHECK(amV2.consoleTransferToken(transfer, errV2) && transfer == "passport_transfer" && sawPassportTransfer);
        // Reload and verify the v2 refresh token was not overwritten by Passport's refresh token.
        std::string raw;
        CHECK(xc::platform::readFile(v2Store, raw));
        std::string plain;
        if (isSealedAccount(raw) && unsealAccount(raw, plain)) raw = plain;
        auto jv2 = json::parse(raw);
        CHECK(jv2 && (*jv2)["clientId"].str() == kV2ClientId && (*jv2)["refreshToken"].str() == "v2_rt_main");

        // Verify SignIn OpenUrl appends ?otc=<CODE>.
        ui::Fonts fonts;
        ui::ImageCache images([] {});
        ui::AppUi appUi(fonts, images);
        ui::UiCaps caps;
        caps.openUrl = true;
        appUi.setCaps(caps);
        appUi.showSignIn("ABCD1234", "https://www.microsoft.com/link");
        ui::NavInput nav;
        nav.accept = true;
        auto ev = appUi.handle(nav);
        CHECK(ev.action == ui::Action::OpenUrl && ev.url == "https://www.microsoft.com/link?otc=ABCD1234");
    }
    net::setTestTransport({});
    xc::fs::removeTree(dir);
}

// Kept-alive connections: the end of a chunked body from its framing, and a
// connection kept only when the server keeps it and the body ended exactly.

static void testMicrosoftHost() {
    // The services whose refusal for the clock says the console's date is wrong.
    for (const char* h : {"login.live.com", "displaycatalog.mp.microsoft.com", "xhome.gssv-play-prod.xboxlive.com",
                          "store-images.s-microsoft.com", "www.xbox.com", "catalog.gamepass.com", "xboxlive.com"})
        CHECK(net::microsoftHost(h));
    for (const char* h : {"expired.badssl.com", "evilxboxlive.com", "microsoft.com.evil.example", "github.com", ""})
        CHECK(!net::microsoftHost(h));
}

static void testSubscriptionPlan() {
    // The store's entitlements: the highest active Game Pass plan.
    auto entry = [](const char* id, const char* status) {
        return std::string("\"") + id + "\":{\"productId\":\"" + id + "\",\"isOwned\":true,\"status\":\"" + status + "\"}";
    };
    std::string all = "{\"entitlements\":{" + entry("CFQ7TTC0K5DJ", "Active") + "," + entry("CFQ7TTC0KHS0", "Active") + "," +
                      entry("CFQ7TTC0P85B", "Active") + "}}";
    CHECK(auth::subscriptionPlan(all) == "Ultimate");
    CHECK(auth::subscriptionPlan("{\"entitlements\":{" + entry("CFQ7TTC0P85B", "Active") + "}}") == "Premium");
    CHECK(auth::subscriptionPlan("{\"entitlements\":{" + entry("CFQ7TTC0KHS0", "Expired") + "," + entry("CFQ7TTC0K5DJ", "Active") + "}}") ==
          "Essential");
    CHECK(auth::subscriptionPlan("{\"entitlements\":{\"cfq7ttc0kgq8\":{\"isSatisfyingEntitlement\":true}}}") == "PC");
    CHECK(auth::subscriptionPlan("{\"entitlements\":{}}").empty() && auth::subscriptionPlan("not json").empty());
    // The log's renewal line: no date, only days left.
    std::string renews = "{\"entitlements\":{\"CFQ7TTC0KHS0\":{\"status\":\"Active\",\"autoRenew\":true,\"endDate\":\"2026-11-14T00:00:00Z\"}}}";
    CHECK(auth::subscriptionRenewal(renews, 1791504000) == "renews automatically");
    std::string ends = "{\"entitlements\":{\"CFQ7TTC0KHS0\":{\"status\":\"Active\",\"autoRenew\":false,\"endDate\":\"2026-10-20T00:00:00.0000000Z\"}}}";
    CHECK(auth::subscriptionRenewal(ends, 1792108800) == "no auto-renew, ends in 4 day(s)");  // from 2026-10-16
    CHECK(auth::subscriptionRenewal("{\"entitlements\":{}}", 0).empty());

    // Catalog plan tiers from PassMetadataByPassProductId, and search Plan filter.
    using namespace xcloud;
    auto pass = [](const char* text) { return productPlanFromJson(text); };
    CHECK(pass(R"({"CFQ7TTC0K5DJ":{},"CFQ7TTC0P85B":{},"CFQ7TTC0KHS0":{}})") == kPlanEssential);
    CHECK(pass(R"({"CFQ7TTC0P85B":{},"CFQ7TTC0KHS0":{}})") == kPlanPremium);
    CHECK(pass(R"({"CFQ7TTC0KHS0":{},"CFQ7TTC0K5DH":{}})") == kPlanUltimate);
    CHECK(pass(R"({})") == kPlanUltimate);
    CHECK(planTier("Essential") == kPlanEssential && planTier("Premium") == kPlanPremium &&
          planTier("Ultimate") == kPlanUltimate && planTier("PC") == kPlanNone && planTier("") == kPlanNone);
    CHECK(planIncludes(kPlanEssential, kPlanEssential, false) && !planIncludes(kPlanEssential, kPlanPremium, false) &&
          !planIncludes(kPlanEssential, kPlanUltimate, false) && planIncludes(kPlanEssential, kPlanUltimate, true));
    CHECK(planIncludes(kPlanPremium, kPlanEssential, false) && planIncludes(kPlanPremium, kPlanPremium, false) &&
          !planIncludes(kPlanPremium, kPlanUltimate, false));
    CHECK(planIncludes(kPlanUltimate, kPlanEssential, false) && planIncludes(kPlanUltimate, kPlanPremium, false) &&
          planIncludes(kPlanUltimate, kPlanUltimate, false));

    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    ui::GameTile ess, prem, ult, f2p;
    ess.productId = "ESS", ess.name = "Alpha", ess.plan = kPlanEssential, ess.playable = true;
    prem.productId = "PREM", prem.name = "Beta", prem.plan = kPlanPremium, prem.playable = true;
    ult.productId = "ULT", ult.name = "Gamma", ult.plan = kPlanUltimate, ult.playable = false;
    f2p.productId = "F2P", f2p.name = "Delta", f2p.plan = kPlanNone, f2p.freeInStore = true, f2p.playable = false;
    app.setSubscription("Premium");
    app.setSearchPools({ess, prem, ult, f2p}, {});
    app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Plan, 3);  // Essential: Alpha + Delta (free-to-play)
    CHECK(app.searchResults(10).size() == 2);
    app.chooseFilter(ui::AppUi::Filter::Plan, 2);  // Premium: Alpha + Beta + Delta
    CHECK(app.searchResults(10).size() == 3);
    app.chooseFilter(ui::AppUi::Filter::Plan, 1);  // Ultimate: all 4
    CHECK(app.searchResults(10).size() == 4);
    app.chooseFilter(ui::AppUi::Filter::Plan, 4);  // All plans: all 4
    CHECK(app.searchResults(10).size() == 4);
    app.chooseFilter(ui::AppUi::Filter::Plan, 0);  // My plan (Premium), with query "a": Alpha, Beta, Delta (not Gamma)
    app.typeText("a");
    CHECK(app.searchResults(10).size() == 3);
}

static void testAccountLists() {
    using xc::app::Settings;
    Settings s;
    s.hidden = {"A"};
    s.favorites = {"B"};
    s.titlePlaytime["A"] = 120;
    CHECK(s.useListsOf("") == Settings::ListsSwitch::Same);
    CHECK(s.useListsOf("111") == Settings::ListsSwitch::Adopted);
    CHECK(s.hidden.size() == 1 && s.listsAccount == "111");
    CHECK(s.useListsOf("111") == Settings::ListsSwitch::Same);
    CHECK(s.useListsOf("222") == Settings::ListsSwitch::Changed);
    CHECK(s.hidden.empty() && s.favorites.empty() && s.titlePlaytime.empty());
    s.hidden = {"C", "D"};
    s.titlePlaytime["C"] = 30;
    const std::string path = "/tmp/xc_test_account_lists.json";
    CHECK(s.save(path));
    Settings back;
    CHECK(back.load(path));
    std::remove(path.c_str());
    CHECK(back.listsAccount == "222" && back.hidden.size() == 2 && back.accountLists.count("111") == 1);
    CHECK(back.useListsOf("111") == Settings::ListsSwitch::Changed);
    CHECK(back.hidden == std::vector<std::string>{"A"} && back.favorites == std::vector<std::string>{"B"});
    CHECK(back.titlePlaytime["A"] == 120 && back.accountLists.count("111") == 0);
    CHECK(back.accountLists["222"].hidden.size() == 2 && back.accountLists["222"].titlePlaytime["C"] == 30);
}

static void testLoginRetry() {
    using namespace xc;
    int calls = 0;
    net::setTestTransport([&](const net::Request&) {
        net::Response r;
        if (++calls < 3) {
            r.status = 500;
            return r;
        }
        r.status = 200;
        r.body = R"({"gsToken":"t","market":"BR","durationInSeconds":3600,"offeringSettings":{"regions":[{"name":"WestUS2","baseUri":"https://w","isDefault":true}]}})";
        return r;
    });
    xcloud::GssvClient gssv;
    std::string err;
    CHECK(gssv.login(auth::XblToken{}, err) && calls == 3);  // (3 s of waits)
    calls = 0;
    net::setTestTransport([&](const net::Request&) {
        ++calls;
        net::Response r;
        r.status = 403;
        return r;
    });
    CHECK(!gssv.login(auth::XblToken{}, err) && calls == 1 && gssv.loginRefused());
    net::setTestTransport({});
}

// The saved sign-in sealed for this device: opens here, not when altered, and
// a file from before is told apart.

static void testAccountSeal() {
    using namespace xc::auth;
    const std::string plain = R"({"version":1,"clientId":"c","refreshToken":"M.R3_secret","userId":"u","gamertag":"g","xuid":"1"})";
    const std::string sealed = sealAccount(plain);
    CHECK(!sealed.empty() && isSealedAccount(sealed) && !isSealedAccount(plain));
    CHECK(sealed.find("M.R3_secret") == std::string::npos && sealed.find("refreshToken") == std::string::npos);
    std::string back;
    CHECK(unsealAccount(sealed, back) && back == plain);
    CHECK(sealAccount(plain) != sealed);  // a new nonce each time
    std::string bad = sealed;
    const size_t at = bad.find("\"data\":\"") + 12;
    bad[at] = bad[at] == 'A' ? 'B' : 'A';
    CHECK(!unsealAccount(bad, back));
    CHECK(!unsealAccount(plain, back));
}

// The game's stick response measured from the picture (app/gyro_calibration.h):
// a scene that turns behind a HUD that stays put, through a known curve.

static void testFreeAccountPlayability() {
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    app.setSubscription("");  // Free account (no Game Pass)
    CHECK(app.isFreeAccount());

    ui::GameTile paidTile;
    paidTile.titleId = "PAID_TITLE";
    paidTile.productId = "PAID_PROD";
    paidTile.playable = true;
    paidTile.freeInStore = false;

    ui::GameTile f2pTile;
    f2pTile.titleId = "Fortnite";
    f2pTile.productId = "9NBLGGH52538";
    f2pTile.playable = true;
    f2pTile.freeInStore = true;

    ui::GameRow row;
    row.title = "Games";
    row.tiles = {paidTile, f2pTile};
    app.setRows({row});
    app.showHome();

    // Paid game on free account: cannot launch
    ui::NavInput in;
    in.accept = true;
    app.handle(in);  // Home -> Details
    ui::UiEvent evPaid = app.handle(in);
    CHECK(evPaid.action != ui::Action::Play);

    // Back to home, select F2P game (index 1)
    ui::NavInput back;
    back.back = true;
    app.handle(back);  // Details -> Home
    ui::NavInput right;
    right.right = true;
    app.handle(right);  // Move to F2P game
    app.handle(in);     // Home -> Details
    ui::UiEvent evF2p = app.handle(in);
    CHECK(evF2p.action == ui::Action::Play);
    CHECK(evF2p.game.titleId == "Fortnite");
}

void runAuthTests() {
    testLoginCountry();
    testPlaytimeLimit();
    testTokenRefresh();
    testMicrosoftHost();
    testSubscriptionPlan();
    testAccountLists();
    testLoginRetry();
    testAccountSeal();
    testFreeAccountPlayability();
}
