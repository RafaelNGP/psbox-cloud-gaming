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

static void testRegions() {
    using xc::xcloud::regionsByExpectedRtt;
    std::vector<std::string> all = {"BRAZILSOUTH", "EASTUS", "WESTEUROPE", "CHILECENTRAL", "JAPANEAST", "UNKNOWN"};
    // Nothing measured: by distance, the unknown region last.
    auto order = regionsByExpectedRtt("BRAZILSOUTH", all, {});
    CHECK(order.size() == 5);
    CHECK(order.front() == "CHILECENTRAL");
    CHECK(order[1] == "EASTUS");
    CHECK(order.back() == "UNKNOWN");
    // A measured round trip beats an estimate.
    order = regionsByExpectedRtt("BRAZILSOUTH", all, {{"BRAZILSOUTH", 15}, {"EASTUS", 40}});
    CHECK(order.front() == "EASTUS");
    double km = xc::xcloud::regionDistanceKm("brazilsouth", "CHILECENTRAL");
    CHECK(km > 2500 && km < 3000);
}

static void testPrices() {
    using xc::xcloud::formatPrice;
    CHECK(formatPrice(78.82, "BRL") == "R$ 78,82");
    CHECK(formatPrice(1299.9, "BRL") == "R$ 1.299,90");
    CHECK(formatPrice(59.99, "USD") == "$59.99");
    CHECK(formatPrice(69.99, "EUR") == "69,99 \xE2\x82\xAC");
    CHECK(formatPrice(4, "XYZ") == "XYZ 4.00");

    // A bundle is a valid regional offer; failure to inspect it is not proof
    // that the base game cannot be bought.
    bool failBundle = false, malformed = false;
    xc::net::setTestTransport([&](const xc::net::Request& req) {
        xc::net::Response r;
        r.status = 200;
        CHECK(req.url.find("market=BR") != std::string::npos);
        if (malformed) { r.body = "{}"; return r; }
        if (req.url.find("bigIds=SELLER") != std::string::npos) {
            if (failBundle) { r.status = 503; r.body = "{}"; return r; }
            r.body = R"({"Products":[{"ProductId":"SELLER","DisplaySkuAvailabilities":[{"Availabilities":[{"Actions":["Purchase","Browse"],"OrderManagementData":{"Price":{"ListPrice":7.8,"MSRP":39,"CurrencyCode":"BRL"}}}]}]}]})";
        } else {
            r.body = R"({"Products":[{"ProductId":"BASE","MarketProperties":[{"RelatedProducts":[{"RelationshipType":"Bundle","RelatedProductId":"SELLER"}]}]},{"ProductId":"UNAVAILABLE"}]})";
        }
        return r;
    });
    std::map<std::string, xc::xcloud::Price> offers;
    std::string err;
    CHECK(xc::xcloud::fetchPrices({"BASE", "UNAVAILABLE"}, "BR", "pt-br", offers, err));
    CHECK(offers.count("BASE") && offers["BASE"].list == 7.8 && !offers.count("UNAVAILABLE"));
    offers.clear();
    failBundle = true;
    CHECK(!xc::xcloud::fetchPrices({"BASE"}, "BR", "pt-br", offers, err));
    malformed = true;
    CHECK(!xc::xcloud::fetchPrices({"BASE"}, "BR", "pt-br", offers, err));
    xc::net::setTestTransport({});

    xc::ui::Fonts fonts;
    xc::ui::ImageCache images([] {});
    xc::ui::AppUi ui(fonts, images);
    xc::ui::GameTile candidate, owned;
    candidate.productId = "REGIONAL";
    candidate.titleId = "REGIONAL_TITLE";
    candidate.name = "Regional Game";
    candidate.purchasable = true;
    candidate.playable = false;
    owned = candidate;
    owned.productId = "OWNED";
    owned.titleId = "OWNED_TITLE";
    owned.name = "Owned Game";
    owned.playable = true;
    owned.purchasable = false;
    ui.setOwned({owned}, {candidate}, true);
    ui.setSearchPools({}, {candidate, owned});
    ui.showHome();
    CHECK(ui.pricesWanted(20, true).size() == 1);
    ui.pricesFailed({"REGIONAL"});
    xc::ui::GameTile tile;
    CHECK(ui.purchasableAt(0, tile)); // offline does not remove the candidate
    ui.pricesNone({"REGIONAL", "OWNED"});
    CHECK(!ui.purchasableAt(0, tile));
    CHECK(ui.isAcquired(owned));
    ui.setSearchTab(xc::ui::Tab::Library);
    ui.setSearchQuery("Game");
    CHECK(ui.searchResults(10).size() == 1);
    // Switching from BR (not sold) to JP rechecks the original source.
    ui.resetStorePrices();
    CHECK(ui.pricesWanted(20, true).size() == 1);
    ui.setPrices({{"REGIONAL", {"JPY 1990", "", 1990, 1990}}});
    CHECK(ui.purchasableAt(0, tile));
    CHECK(ui.searchResults(10).size() == 2);

}

static void testTitleListRetriesAndIncompleteLibrary() {
    int page1 = 0, page2 = 0, failuresLeft = 1;
    bool down = false;
    net::setTestTransport([&](const net::Request& req) {
        net::Response r;
        r.status = 200;
        r.body = "{}";
        if (req.url.find("/v2/titles") == std::string::npos) return r;
        if (down) {
            r.status = 500;
            r.body = "Internal Server Error";
            return r;
        }
        if (req.url.find("ct=t2") == std::string::npos) {
            ++page1;
            r.body = R"({"results":[{"titleId":"GAME1","details":{"productId":"P1"}}],"continuationToken":"t2"})";
        } else {
            ++page2;
            if (failuresLeft > 0) {
                --failuresLeft;
                r.status = 500;
                r.body = "Internal Server Error";
            } else {
                r.body = R"({"results":[{"titleId":"GAME2","details":{"productId":"P2"}}]})";
            }
        }
        return r;
    });
    xcloud::GssvClient gssv;
    std::vector<xcloud::Title> titles;
    std::string err;
    CHECK(gssv.listTitles(titles, err));
    CHECK(titles.size() == 2 && page1 == 1 && page2 == 2);  // the second page asked twice, the first not again

    failuresLeft = 99;  // the second page never answers: the list fails, after three tries
    page1 = page2 = 0;
    CHECK(!gssv.listTitles(titles, err) && page2 == 3);
    CHECK(err.find("title list failed") != std::string::npos);

    app::Library lib;
    CHECK(!lib.incomplete());
    down = true;
    CHECK(lib.syncDelta(gssv, [] {}, err) == 0 && lib.incomplete());
    down = false;
    failuresLeft = 0;
    lib.syncDelta(gssv, [] {}, err);
    CHECK(!lib.incomplete());
    net::setTestTransport({});
}

// The fonts' folder out of reach, or a font cut short by a transfer: the copies inside the binary
// stand in, so the screens always have text.

static void testDetailsHydrationAndPlaytimeBadge() {
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);
    app.setSubscription("GamePass");
    CHECK(!app.isFreeAccount());

    // Tile on home row with only titleId and productId (no description, no rating)
    ui::GameTile lightTile;
    lightTile.titleId = "GAME_WITH_FULL_DATA";
    lightTile.productId = "PROD_FULL_DATA";
    lightTile.name = "My Full Game";
    lightTile.playable = true;

    // Detailed tile in gamePassPool
    ui::GameTile fullTile = lightTile;
    fullTile.description = "Full detailed description of the game.";
    fullTile.heroUrl = "https://example.com/hero.jpg";
    fullTile.publisher = "Awesome Studio";
    fullTile.categories = {"Action", "Adventure"};
    fullTile.ratingAge = "14+";
    fullTile.ratingSystem = "ClassInd";
    fullTile.ratingDescriptors = {"Violência"};

    app.setSearchPools({fullTile}, {});

    ui::GameRow row;
    row.title = "Jump Back In";
    row.tiles = {lightTile};
    app.setRows({row});
    app.showHome();

    // Set playtime in settings
    ui::SettingsChoice c = app.settings();
    c.titlePlaytime[lightTile.productId] = 3600; // 1 hour
    app.setSettings(c);

    // Open details
    ui::NavInput in;
    in.accept = true;
    app.handle(in); // Opens Details

    // Detail tile must have description, rating, publisher, categories, heroUrl hydrated!
    // And since description is < 80 chars (static fallback length), detailWanted() must still request full Store hydration!
    CHECK(app.detailWanted(1000) == "PROD_FULL_DATA");

    // Once setDetailInfo delivers a full description (>= 80 chars), detailWanted() returns empty and Detail tile upgrades to the full description
    const std::string longDesc =
        "This is the full localized Store API description of the game with more than eighty characters of detailed text.";
    app.setDetailInfo("PROD_FULL_DATA", longDesc, "Awesome Studio", {"Action", "Adventure"},
                      "https://example.com/hero.jpg", "14+", "ClassInd", {"Violência"});
    CHECK(app.detailWanted(7000).empty());

    ui::NavInput acceptInDetails;
    acceptInDetails.accept = true;
    ui::UiEvent ev = app.handle(acceptInDetails);
    CHECK(ev.action == ui::Action::Play);
    CHECK(ev.game.description == longDesc);
    CHECK(ev.game.publisher == "Awesome Studio");
    CHECK(ev.game.ratingAge == "14+");
    CHECK(ev.game.ratingSystem == "ClassInd");
    CHECK(ev.game.heroUrl == "https://example.com/hero.jpg");
    CHECK(!ev.game.ratingDescriptors.empty());
    CHECK(ev.game.ratingDescriptors[0] == "Violência");
}

static void testLibraryRowsOnce() {
    std::string dir = "/tmp/xcloud-library-test";
    xc::fs::removeTree(dir);
    xc::fs::makeDirs(dir);
    std::string cache = dir + "/library.json";
    net::setTestTransport([](const net::Request& req) {
        net::Response r;
        r.status = 200;
        if (req.url.find("/sigls/") != std::string::npos) {
            std::string id = req.url.substr(req.url.find("id=") + 3);
            id = id.substr(0, id.find('&'));
            r.body = R"([{"siglId":")" + id + R"(","title":"List )" + id + R"("},{"id":"P1"},{"id":"P2"}])";
        } else if (req.url.find("/v3/products") != std::string::npos) {
            r.body = R"({"Products":{"P1":{"XCloudTitleId":"T1","ProductTitle":"Game One"},)"
                     R"("P2":{"XCloudTitleId":"T2","ProductTitle":"Game Two"}}})";
        } else {
            r.status = 503;  // the account's titles: not needed here
        }
        return r;
    });
    auto titlesOf = [](const std::vector<ui::GameRow>& rows) {
        std::vector<std::string> t;
        for (const auto& r : rows) t.push_back(r.title);
        return t;
    };
    auto unique = [](std::vector<std::string> t) {
        std::sort(t.begin(), t.end());
        return std::adjacent_find(t.begin(), t.end()) == t.end();
    };
    auto layoutSize = [&] {
        std::string text;
        if (!xc::platform::readFile(cache, text)) return size_t(0);
        auto j = json::parse(text);
        return j ? (*j)["layout"].size() : size_t(0);
    };
    xcloud::GssvClient gssv;
    std::string err;
    size_t fresh = 0;
    {
        app::Library lib;
        lib.setCachePath(cache);
        CHECK(lib.load(gssv, "en-US", [] {}, err));
        fresh = lib.rows().size();
        CHECK(fresh >= 4 && unique(titlesOf(lib.rows())));
        lib.saveCache();
        CHECK(layoutSize() == fresh);
    }
    {
        // The next start: the cache has rows, load() still shows each once.
        app::Library lib;
        lib.setCachePath(cache);
        CHECK(lib.load(gssv, "en-US", [] {}, err));
        CHECK(lib.rows().size() == fresh && unique(titlesOf(lib.rows())));
        lib.saveCache();
        CHECK(layoutSize() == fresh);
    }
    {
        // A cache written with every row twice (the old bug).
        std::string text;
        CHECK(xc::platform::readFile(cache, text));
        auto j = json::parse(text);
        CHECK(j.has_value());
        if (j) {
            json::Value doubled = json::Value::array();
            for (int k = 0; k < 2; ++k)
                for (const auto& row : (*j)["layout"].items()) doubled.push(row);
            j->set("layout", doubled);
            xc::platform::writeFileAtomic(cache, j->dump());
        }
        CHECK(layoutSize() == 2 * fresh);
        app::Library instant;  // the instant start: each row once
        instant.setCachePath(cache);
        CHECK(instant.loadCache(true));
        CHECK(instant.rows().size() == fresh && unique(titlesOf(instant.rows())));
        app::Library lib;
        lib.setCachePath(cache);
        CHECK(lib.load(gssv, "en-US", [] {}, err));
        CHECK(lib.rows().size() == fresh);
        lib.saveCache();
        CHECK(layoutSize() == fresh);  // healed
    }
    net::setTestTransport({});
    xc::fs::removeTree(dir);
}

static void testStoreLeftovers() {
    // ProsperoStore replaced the app folder and kept the old one: the
    // sign-in and settings come back from it, once.
    std::string dir = "/tmp/xcloud-store-test/app", prev = "/tmp/xcloud-store-test/previous";
    xc::fs::removeTree("/tmp/xcloud-store-test");
    xc::fs::makeDirs("/tmp/xcloud-store-test/app");
    xc::fs::makeDirs("/tmp/xcloud-store-test/previous");
    xc::platform::writeFileAtomic(prev + "/account.json", "{\"a\":1}");
    xc::platform::writeFileAtomic(prev + "/settings.json", "{\"language\":\"pt-BR\"}");
    xc::platform::writeFileAtomic(prev + "/xcloud.log", "old log");
    std::string text;
    CHECK(app::importStoreLeftovers(dir, prev));
    CHECK(xc::platform::readFile(dir + "/account.json", text) && text == "{\"a\":1}");
    CHECK(xc::platform::readFile(dir + "/settings.json", text) && text.find("pt-BR") != std::string::npos);
    CHECK(!xc::platform::readFile(dir + "/xcloud.log", text));             // only the user's settings and sign-in
    CHECK(xc::platform::readFile(prev + "/account.json", text));           // the store's folder stays
    // Signed out later: the old account doesn't come back.
    std::remove((dir + "/account.json").c_str());
    CHECK(!app::importStoreLeftovers(dir, prev) && !xc::platform::readFile(dir + "/account.json", text));
    // Already signed in when the store's folder is found: never imported.
    xc::fs::removeTree("/tmp/xcloud-store-test/app");
    xc::fs::makeDirs("/tmp/xcloud-store-test/app");
    xc::platform::writeFileAtomic(dir + "/account.json", "{\"b\":2}");
    CHECK(!app::importStoreLeftovers(dir, prev));
    std::remove((dir + "/account.json").c_str());
    CHECK(!app::importStoreLeftovers(dir, prev));
    // No store folder: nothing.
    xc::fs::removeTree("/tmp/xcloud-store-test/app");
    xc::fs::makeDirs("/tmp/xcloud-store-test/app");
    CHECK(!app::importStoreLeftovers(dir, "/tmp/xcloud-store-test/none"));
    CHECK(!app::updatablePath("store_import.done"));
    xc::fs::removeTree("/tmp/xcloud-store-test");
}

static void testPurchase() {
    using namespace xcloud;
    std::string err;
    int status = 200;
    std::string body;
    net::Request lastReq;

    net::setTestTransport([&](const net::Request& req) {
        lastReq = req;
        net::Response r;
        r.status = status;
        r.body = body;
        return r;
    });

    // 1. fetchPurchaseOffer for a free product
    body = R"({
        "Products": [
            {
                "ProductId": "FREE123",
                "DisplaySkuAvailabilities": [
                    {
                        "Sku": {"SkuId": "0001"},
                        "Availabilities": [
                            {
                                "AvailabilityId": "AVAIL_FREE",
                                "Actions": ["Details", "Purchase"],
                                "OrderManagementData": {
                                    "Price": {
                                        "ListPrice": 0.0,
                                        "MSRP": 0.0,
                                        "CurrencyCode": "USD"
                                    }
                                }
                            }
                        ]
                    }
                ]
            }
        ]
    })";

    PurchaseOffer offer;
    CHECK(fetchPurchaseOffer("FREE123", "US", "en-us", offer, err));
    CHECK(offer.productId == "FREE123");
    CHECK(offer.skuId == "0001");
    CHECK(offer.availabilityId == "AVAIL_FREE");
    CHECK(offer.isFree);
    CHECK(offer.listPrice == 0.0);
    CHECK(lastReq.url.find("displaycatalog.mp.microsoft.com") != std::string::npos);

    // 2. fetchPurchaseOffer for a paid product: isFree must be false
    body = R"({
        "Products": [
            {
                "ProductId": "PAID123",
                "DisplaySkuAvailabilities": [
                    {
                        "Sku": {"SkuId": "0002"},
                        "Availabilities": [
                            {
                                "AvailabilityId": "AVAIL_PAID",
                                "Actions": ["Purchase"],
                                "OrderManagementData": {
                                    "Price": {
                                        "ListPrice": 69.99,
                                        "MSRP": 69.99,
                                        "CurrencyCode": "USD"
                                    }
                                }
                            }
                        ]
                    }
                ]
            }
        ]
    })";
    PurchaseOffer paidOffer;
    CHECK(fetchPurchaseOffer("PAID123", "US", "en-us", paidOffer, err));
    CHECK(!paidOffer.isFree);
    CHECK(paidOffer.listPrice == 69.99);

    // 3. Safety check: claimFreeProduct MUST refuse paid products
    CHECK(!claimFreeProduct("XBL3.0 x=test;token", paidOffer, "US", "en-US", err));
    CHECK(err.find("safety block") != std::string::npos);

    // 4. claimFreeProduct with empty auth or incomplete offer
    PurchaseOffer invalidOffer = offer;
    invalidOffer.availabilityId.clear();
    CHECK(!claimFreeProduct("XBL3.0 x=test;token", invalidOffer, "US", "en-US", err));
    CHECK(!claimFreeProduct("", offer, "US", "en-US", err));

    // 5. Successful claim of free product (2-step POST create and PUT finalize)
    net::Request lastPostReq, lastPutReq;
    net::setTestTransport([&](const net::Request& req) {
        net::Response r;
        if (req.method == "POST") {
            lastPostReq = req;
            r.status = 201;
            r.body = R"({"orderId":"ORDER-12345","orderState":"Editing"})";
        } else if (req.method == "PUT") {
            lastPutReq = req;
            r.status = 200;
            r.body = R"({"orderId":"ORDER-12345","orderState":"Purchased"})";
        }
        return r;
    });

    CHECK(claimFreeProduct("XBL3.0 x=test;token", offer, "US", "en-US", err));
    CHECK(lastPostReq.url.find("purchase.mp.microsoft.com/v7.0/users/me/orders") != std::string::npos);
    std::string authVal;
    for (const auto& h : lastPostReq.headers) {
        if (h.first == "Authorization") authVal = h.second;
    }
    CHECK(authVal == "XBL3.0 x=test;token");
    auto postJson = json::parse(lastPostReq.body);
    CHECK(postJson && (*postJson)["items"].isArray());
    CHECK((*postJson)["items"][0]["productId"].str() == "FREE123");
    CHECK((*postJson)["items"][0]["skuId"].str() == "0001");
    CHECK((*postJson)["items"][0]["availabilityId"].str() == "AVAIL_FREE");
    CHECK((*postJson)["items"][0]["price"].asNumber() == 0.0);
    CHECK((*postJson)["clientContext"]["client"].str() == "Xbox");
    CHECK(!(*postJson)["orderId"].str().empty());

    CHECK(lastPutReq.method == "PUT");
    auto putJson = json::parse(lastPutReq.body);
    CHECK(putJson && (*putJson)["orderState"].str() == "Purchased");

    // 6. claimFreeProduct error handling (must extract innererror code/message, not innererror.data[0] productId)
    net::setTestTransport([&](const net::Request&) {
        net::Response r;
        r.status = 400;
        r.body = R"({"code":"PaymentRequired","innererror":{"code":"MissingPaymentInstrument","data":["FREE123"],"message":"Payment method needed"}})";
        return r;
    });
    CHECK(!claimFreeProduct("XBL3.0 x=test;token", offer, "US", "en-US", err));
    CHECK(err.find("Payment method needed") != std::string::npos);
    CHECK(err != "FREE123");

    // 7. UserAlreadyOwnsContent on finalize (HTTP 403, Issue #104) is treated as already acquired
    net::setTestTransport([&](const net::Request& req) {
        net::Response r;
        if (req.method == "POST") {
            r.status = 201;
            r.body = R"({"orderId":"ORDER-12345","orderState":"Editing"})";
        } else {
            r.status = 403;
            r.body = R"({"code":"Forbidden","innererror":{"code":"UserAlreadyOwnsContent","data":["9P22THR57FV7"],"message":"Purchase policy restricts user from purchasing certain content multiple times"}})";
        }
        return r;
    });
    CHECK(claimFreeProduct("XBL3.0 x=test;token", offer, "US", "en-US", err));

    // 8. fetchPurchaseOffer skips RemediationRequired upsells and picks the non-remediated free availability
    net::setTestTransport([&](const net::Request&) {
        net::Response r;
        r.status = 200;
        r.body = R"({
            "Products": [{
                "DisplaySkuAvailabilities": [{
                    "Sku": {"SkuId": "0001"},
                    "Availabilities": [
                        {
                            "AvailabilityId": "AVAIL_UPSELL_REMEDIATION",
                            "Actions": ["Purchase"],
                            "RemediationRequired": true,
                            "OrderManagementData": {"Price": {"ListPrice": 0.0, "MSRP": 0.0, "CurrencyCode": "USD"}}
                        },
                        {
                            "AvailabilityId": "AVAIL_DIRECT_FREE",
                            "Actions": ["Purchase"],
                            "RemediationRequired": false,
                            "OrderManagementData": {"Price": {"ListPrice": 0.0, "MSRP": 0.0, "CurrencyCode": "USD"}}
                        }
                    ]
                }]
            }]
        })";
        return r;
    });
    PurchaseOffer directOffer;
    CHECK(fetchPurchaseOffer("FREE123", "US", "en-us", directOffer, err));
    CHECK(directOffer.isFree);
    CHECK(directOffer.availabilityId == "AVAIL_DIRECT_FREE");

    // 9. fetchPurchaseOffer rejects non-purchaseable actions like "Browse" or "License"
    net::setTestTransport([](const net::Request&) {
        net::Response r;
        r.status = 200;
        r.body = R"({
            "Products": [{
                "ProductId": "JUSTDANCE",
                "DisplaySkuAvailabilities": [{
                    "Sku": { "SkuId": "0001" },
                    "Availabilities": [{
                        "AvailabilityId": "AVAIL_JUSTDANCE",
                        "Actions": ["Browse", "License"],
                        "OrderManagementData": {
                            "Price": { "ListPrice": 0.0, "MSRP": 0.0, "CurrencyCode": "BRL" }
                        }
                    }]
                }]
            }]
        })";
        return r;
    });
    PurchaseOffer trialOffer;
    CHECK(!fetchPurchaseOffer("JUSTDANCE", "BR", "pt-br", trialOffer, err));

    net::setTestTransport({});
}

static void testWishlist() {
    using namespace xc::app;
    std::string tmp = "/tmp/xcloud-tests-wishlist.json";
    Settings s;
    s.wishlist = {"PROD_A", "PROD_B"};
    CHECK(s.save(tmp));

    Settings loaded;
    CHECK(loaded.load(tmp));
    CHECK(loaded.wishlist.size() == 2);
    if (loaded.wishlist.size() == 2) {
        CHECK(loaded.wishlist[0] == "PROD_A");
        CHECK(loaded.wishlist[1] == "PROD_B");
    }
    ::unlink(tmp.c_str());

    Library lib;
    lib.setWishlist({"PROD_WISH"});
    auto rows = lib.rows();
    // Standalone wishlist shelf must not exist in Game Pass home rows
    for (const auto& r : rows) {
        CHECK(r.title != ui::tr(ui::Str::Wishlist));
    }

    // AppUi Wishlist tests
    ui::Fonts fonts;
    ui::ImageCache images([] {});
    ui::AppUi app(fonts, images);

    ui::GameTile ownedTile, unownedTileA, unownedTileB;
    ownedTile.productId = "OWNED_1";
    ownedTile.titleId = "TITLE_OWNED_1";
    ownedTile.name = "Owned Game";

    unownedTileA.productId = "UNOWNED_A";
    unownedTileA.titleId = "TITLE_UNOWNED_A";
    unownedTileA.name = "Game A";

    unownedTileB.productId = "UNOWNED_B";
    unownedTileB.titleId = "TITLE_UNOWNED_B";
    unownedTileB.name = "Game B";

    // Set owned and purchasable: unownedTileB is wishlisted initially.
    // Also include ownedTile in wishlist initially to verify rebuild() prunes acquired games!
    app.setPrefs({}, ui::LibrarySort::AZ, {}, {"OWNED_1", "UNOWNED_B"});
    app.setOwned({ownedTile}, {unownedTileA, unownedTileB}, true);

    // In purchasable_, UNOWNED_B should be at index 0 because it's wishlisted, even though Game A precedes Game B alphabetically!
    ui::GameTile firstPurchasable;
    CHECK(app.purchasableAt(0, firstPurchasable));
    CHECK(firstPurchasable.productId == "UNOWNED_B");

    // Search filtering with Wishlist
    app.setSearchPools({}, {unownedTileA, unownedTileB});
    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    auto res = app.searchResults(10);
    // Only UNOWNED_B should be returned because it's in the wishlist
    CHECK(res.size() == 1);
    if (!res.empty()) {
        CHECK(res[0].first == "Game B");
    }

    // Test details screen: open details for ownedTile, press R3 -> should NOT add to wishlist!
    app.showDetails(ownedTile);
    ui::NavInput r3Press;
    r3Press.r3 = true;
    app.handle(r3Press);

    // Verify search with wishlist filter again: ownedTile was NOT added!
    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    CHECK(res.size() == 1);
    if (!res.empty()) {
        CHECK(res[0].first == "Game B");
    }

    // Open details for unownedTileA, press R3 -> should be added to wishlist!
    app.showDetails(unownedTileA);
    app.handle(r3Press);

    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    // Now both UNOWNED_B and UNOWNED_A are wishlisted!
    CHECK(res.size() == 2);

    // Press R3 again on unownedTileA -> toggles off!
    app.showDetails(unownedTileA);
    app.handle(r3Press);

    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    CHECK(res.size() == 1);

    // Test price updating via setPrices while wishlist filter is active
    ui::PriceInfo pi;
    pi.list = 49.99;
    pi.msrp = 49.99;
    pi.now = "R$ 49,99";
    app.setPrices({{"UNOWNED_B", pi}});
    res = app.searchResults(10);
    CHECK(res.size() == 1);
    CHECK(res[0].second.now == "R$ 49,99");

    // Add UNOWNED_A to wishlist
    app.showDetails(unownedTileA);
    app.handle(r3Press);

    // Test pruning of acquired game matching titleId
    ui::GameTile newlyOwned;
    newlyOwned.productId = "DIFF_PID";
    newlyOwned.titleId = "TITLE_UNOWNED_A";
    newlyOwned.name = "Game A";
    app.setOwned({ownedTile, newlyOwned}, {unownedTileA, unownedTileB}, true);

    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    // UNOWNED_A must be pruned because newlyOwned has matching titleId
    CHECK(res.size() == 1);
    CHECK(res[0].first == "Game B");

    // Unwishlist UNOWNED_B to test empty state
    app.showDetails(unownedTileB);
    app.handle(r3Press);

    app.showFilteredSearch(ui::Tab::Library, false, 0, "", 0, -1);
    app.chooseFilter(ui::AppUi::Filter::Wishlist, 1);
    res = app.searchResults(10);
    CHECK(res.empty());

    // Empty wishlist string test
    CHECK(std::string(ui::tr(ui::Str::WishlistEmpty)).find("vazia") != std::string::npos ||
          std::string(ui::tr(ui::Str::WishlistEmpty)).find("empty") != std::string::npos);
}

static void testAchievements() {
    using namespace xc::xcloud;
    TitleAchievements ach;
    std::string err;
    CHECK(!fetchTitleAchievements("", "123", "TITLE1", ach, err));
    CHECK(err == "xblAuth empty");
    CHECK(!fetchTitleAchievements("XBL3.0 x=test;tok", "123", "", ach, err));
    CHECK(err == "xboxTitleId empty");
}

static void testLibraryOfflineCache() {
    std::string cacheFile = "/tmp/test_library_cache.json";
    ::unlink(cacheFile.c_str());

    // 1. Populate a library instance and save cache
    {
        app::Library lib;
        lib.setCachePath(cacheFile);
        lib.markAsOwned("PROD_1001", "TITLE_1001");
        lib.markAsOwned("PROD_1002", "TITLE_1002");
        CHECK(lib.ownedKnown());
        CHECK(lib.owned().size() == 2);
        lib.saveCache();
    }

    // 2. Instant-resume load from library_cache.json without network
    {
        app::Library libResume;
        libResume.setCachePath(cacheFile);
        CHECK(libResume.loadCache(true));
        CHECK(libResume.ownedKnown());
        CHECK(libResume.owned().size() == 2);
    }

    // 3. Fallback to library.json when cachePath doesn't exist
    std::string legacyFile = "/tmp/library.json";
    ::rename(cacheFile.c_str(), legacyFile.c_str());
    {
        app::Library libFallback;
        libFallback.setCachePath(cacheFile); // pointing to non-existent library_cache.json
        CHECK(libFallback.loadCache(true));  // should successfully fall back to library.json
        CHECK(libFallback.ownedKnown());
        CHECK(libFallback.owned().size() == 2);
    }
    ::unlink(legacyFile.c_str());

    // 4. Empty cache robustness test
    std::string emptyCacheFile = "/tmp/test_empty_cache.json";
    ::unlink(emptyCacheFile.c_str());
    {
        app::Library libEmpty;
        libEmpty.setCachePath(emptyCacheFile);
        CHECK(!libEmpty.loadCache(true)); // no file
        platform::writeFileAtomic(emptyCacheFile, "{}");
        CHECK(!libEmpty.loadCache(true)); // version mismatch
    }
    ::unlink(emptyCacheFile.c_str());
}

static void testCodSearchRomanAndSettingsReset() {
    // 1. Search matching: Call of Duty MW4, acronyms, Roman numerals
    {
        ui::Fonts fonts;
        ui::ImageCache images([] {});
        ui::AppUi app(fonts, images);

        ui::GameTile mw3, mw4, bo6, gta5, fn;
        mw3.productId = "COD_MW3";
        mw3.name = "Call of Duty: Modern Warfare III";
        mw3.playable = true;

        mw4.productId = "9NCPSQQD1Z66";
        mw4.name = "Call of Duty: Modern Warfare 4";
        mw4.playable = true;

        bo6.productId = "COD_BO6";
        bo6.name = "Call of Duty: Black Ops 6";
        bo6.playable = true;

        gta5.productId = "GTA_V";
        gta5.name = "Grand Theft Auto V";
        gta5.playable = true;

        fn.productId = "FN";
        fn.name = "Fortnite";
        fn.playable = true;

        app.setSearchPools({mw3, mw4, bo6, gta5, fn}, {});

        // Search "mw4" -> should match Call of Duty: Modern Warfare 4
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("mw4");
        auto res = app.searchResults(10);
        CHECK(res.size() == 1 && res[0].first == "Call of Duty: Modern Warfare 4");

        // Search "cod 4" -> should match Call of Duty: Modern Warfare 4
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("cod 4");
        res = app.searchResults(10);
        CHECK(!res.empty() && res[0].first == "Call of Duty: Modern Warfare 4");

        // Search "call of duty 4" -> should match Call of Duty: Modern Warfare 4
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("call of duty 4");
        res = app.searchResults(10);
        CHECK(!res.empty() && res[0].first == "Call of Duty: Modern Warfare 4");

        // Search "mw 3" -> Roman numeral 3 <-> iii
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("mw 3");
        res = app.searchResults(10);
        CHECK(!res.empty() && res[0].first == "Call of Duty: Modern Warfare III");

        // Search "gta 5" -> Roman numeral 5 <-> v
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("gta 5");
        res = app.searchResults(10);
        CHECK(res.size() == 1 && res[0].first == "Grand Theft Auto V");

        // Search "bo 6" -> Black Ops 6
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("bo 6");
        res = app.searchResults(10);
        CHECK(res.size() == 1 && res[0].first == "Call of Duty: Black Ops 6");

        // Search "cod" -> all 3 COD games
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("cod");
        res = app.searchResults(10);
        CHECK(res.size() == 3);
    }

    // 2. WebControl strings
    {
        for (int lang = 0; lang < 6; ++lang) {
            ui::setLanguage(static_cast<ui::Language>(lang));
            std::string desc = ui::tr(ui::Str::WebControlDesc);
            CHECK(!desc.empty());
            std::string title = ui::tr(ui::Str::WebControlTitle);
            CHECK(!title.empty());
            std::string resetDesc = ui::tr(ui::Str::SettingDescResetSettings);
            CHECK(!resetDesc.empty());
        }
        ui::setLanguage(ui::Language::English);
    }

    // 3. Settings Reset
    {
        app::Settings s{};
        s.queueReadyAlert = false;
        s.resolution = "720p";
        s.language = "fr-FR";
        s.padSpeakerVolume = 50;

        app::Settings def{};
        s = app::Settings{};
        CHECK(s.queueReadyAlert == def.queueReadyAlert);
        CHECK(s.resolution == def.resolution);
        CHECK(s.language == def.language);
        CHECK(s.padSpeakerVolume == def.padSpeakerVolume);
    }
}

static void testCallOfDutyModernWarfare4CatalogAndRefresh() {
    std::string testDir = "/tmp/test-cod-mw4";
    xc::fs::removeTree(testDir);
    xc::fs::makeDirs(testDir);
    std::string cacheFile = testDir + "/library_cache.json";

    net::setTestTransport([](const net::Request& req) {
        net::Response r;
        r.status = 200;
        if (req.url.find("/sigls/") != std::string::npos) {
            std::string id = req.url.substr(req.url.find("id=") + 3);
            id = id.substr(0, id.find('&'));
            r.body = R"([{"siglId":")" + id + R"(","title":"List )" + id + R"("},{"id":"P1"}])";
        } else if (req.url.find("/v3/products") != std::string::npos) {
            r.body = R"({"Products":{"P1":{"XCloudTitleId":"T1","ProductTitle":"Game One"}}})";
        } else {
            r.status = 503;
        }
        return r;
    });

    xcloud::GssvClient gssv;
    std::string err;

    // 1. Verify load() seeds Modern Warfare 4 and Call of Duty franchise
    {
        app::Library lib;
        lib.setCachePath(cacheFile);
        CHECK(lib.load(gssv, "en-US", [] {}, err));

        auto gpPool = lib.gamePassSearchPool();
        bool foundMw4 = false, foundCodHq = false, foundWarzone = false;
        for (const auto& t : gpPool) {
            if (t.productId == "9NCPSQQD1Z66" && t.name == "Call of Duty®: Modern Warfare® 4") foundMw4 = true;
            if (t.productId == "9N201KQXS5BM" && t.name == "Call of Duty®") foundCodHq = true;
            if (t.productId == "9NNFG8BQRCXL" && t.name == "Call of Duty®: Warzone™") foundWarzone = true;
        }
        CHECK(foundMw4);
        CHECK(foundCodHq);
        CHECK(foundWarzone);

        // Check tile details
        ui::GameTile mw4Tile;
        for (const auto& t : gpPool) {
            if (t.productId == "9NCPSQQD1Z66") {
                mw4Tile = t;
                break;
            }
        }
        CHECK(mw4Tile.productId == "9NCPSQQD1Z66");
        CHECK(mw4Tile.titleId == "CALLOFDUTYHQ");
        CHECK(mw4Tile.xboxTitleId == "2001700854");
        CHECK(mw4Tile.name == "Call of Duty®: Modern Warfare® 4");
        CHECK(!mw4Tile.tileUrl.empty());
        CHECK(!mw4Tile.heroUrl.empty());

        // Check All Games row has MW4
        bool mw4InRows = false;
        for (const auto& row : lib.rows()) {
            for (const auto& t : row.tiles) {
                if (t.productId == "9NCPSQQD1Z66") mw4InRows = true;
            }
        }
        CHECK(mw4InRows);

        // Save cache for offline test
        lib.saveCache();
    }

    // 2. Test offline cache / instant-resume withLayout retains MW4
    {
        app::Library libResume;
        libResume.setCachePath(cacheFile);
        CHECK(libResume.loadCache(true));
        auto gpPool = libResume.gamePassSearchPool();
        bool foundMw4 = false;
        for (const auto& t : gpPool) {
            if (t.productId == "9NCPSQQD1Z66") foundMw4 = true;
        }
        CHECK(foundMw4);
    }

    // 3. Test CALLOFDUTYHQ multi-experience deduplication and search
    {
        app::Library lib;
        lib.setCachePath(cacheFile);
        CHECK(lib.load(gssv, "en-US", [] {}, err));

        ui::Fonts fonts;
        ui::ImageCache images([] {});
        ui::AppUi app(fonts, images);
        app.setSearchPools(lib.gamePassSearchPool(), {});

        // Search "Modern Warfare 4"
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("Modern Warfare 4");
        auto res = app.searchResults(10);
        CHECK(!res.empty());
        CHECK(res[0].first == "Call of Duty®: Modern Warfare® 4");

        // Search "Warzone"
        app.showFilteredSearch(ui::Tab::GamePass, false, 0, "", 0, -1);
        app.typeText("Warzone");
        res = app.searchResults(10);
        CHECK(!res.empty());
        CHECK(res[0].first == "Call of Duty®: Warzone™");
    }

    // 4. Test loadOwned entitles COD games for Game Pass members
    {
        app::Library lib;
        lib.setCachePath(cacheFile);
        CHECK(lib.load(gssv, "en-US", [] {}, err));

        // Modern Warfare 4 is present in rows
        bool foundInShelf = false;
        for (const auto& row : lib.rows()) {
            for (const auto& t : row.tiles) {
                if (t.productId == "9NCPSQQD1Z66") foundInShelf = true;
            }
        }
        CHECK(foundInShelf);

        // Simulate loadOwned with Game Pass entitlement
        net::setTestTransport([](const net::Request& req) {
            net::Response r;
            r.status = 200;
            if (req.url.find("/v2/titles") != std::string::npos) {
                r.body = R"({"results":[{"titleId":"CALLOFDUTYHQ","details":{"productId":"9N201KQXS5BM","hasEntitlement":true,"isFreeInStore":false,"userPrograms":["CALLISTO"]}}]})";
            } else if (req.url.find("/v3/products") != std::string::npos) {
                r.body = R"({"Products":{"9NCPSQQD1Z66":{"XCloudTitleId":"CALLOFDUTYHQ","ProductTitle":"Call of Duty®: Modern Warfare® 4"}}})";
            } else {
                r.status = 503;
            }
            return r;
        });

        std::atomic<bool> stop{false};
        lib.loadOwned(gssv, [] {}, &stop);

        // Verify MW4 is owned and playable
        bool mw4Playable = false;
        for (const auto& t : lib.owned()) {
            if (t.productId == "9NCPSQQD1Z66") {
                mw4Playable = t.playable;
                break;
            }
        }
        CHECK(mw4Playable);
    }

    // 5. Test plan tier checking: Essential does not play Ultimate-tier games, Ultimate does
    {
        net::setTestTransport([](const net::Request& req) {
            net::Response r;
            r.status = 200;
            if (req.url.find("/sigls/") != std::string::npos) {
                std::string id = req.url.substr(req.url.find("id=") + 3);
                id = id.substr(0, id.find('&'));
                r.body = R"([{"siglId":")" + id + R"(","title":"List )" + id + R"("},{"id":"P1"}])";
            } else if (req.url.find("/v3/products") != std::string::npos) {
                r.body = R"({"Products":{"P1":{"XCloudTitleId":"T1","ProductTitle":"Game One"},"9NCPSQQD1Z66":{"XCloudTitleId":"CALLOFDUTYHQ","ProductTitle":"Call of Duty®: Modern Warfare® 4"}}})";
            } else {
                r.status = 503;
            }
            return r;
        });

        std::string planCache = testDir + "/plan_cache.json";
        platform::writeFileAtomic(planCache, R"({"version":2,"market":"US","language":"en-US","products":{},"owned":[],"purchasable":[],"ownedTitles":[],"ownedProducts":[],"gamePass":[]})");
        app::Library libPlan;
        libPlan.setCachePath(planCache);
        libPlan.setSubscription("Essential");
        CHECK(libPlan.load(gssv, "en-US", [] {}, err));
        for (const auto& t : libPlan.gamePassSearchPool()) {
            if (t.productId == "9NCPSQQD1Z66") {
                // Not playable on Essential plan without ownership
                CHECK(!t.playable);
            }
        }

        libPlan.setSubscription("Ultimate");
        for (const auto& t : libPlan.gamePassSearchPool()) {
            if (t.productId == "9NCPSQQD1Z66") {
                // Playable on Ultimate plan
                CHECK(t.playable);
            }
        }
    }

    // 6. Test forceRefresh: force-refresh reloads without dropping offline cache
    {
        app::Library libRefresh;
        libRefresh.setCachePath(cacheFile);
        CHECK(libRefresh.load(gssv, "en-US", [] {}, err, true));
        bool foundMw4 = false;
        for (const auto& row : libRefresh.rows()) {
            for (const auto& t : row.tiles) {
                if (t.productId == "9NCPSQQD1Z66") foundMw4 = true;
            }
        }
        CHECK(foundMw4);
    }

    // 7. Test loadOwned populates all owned and streamable titles, not just COD and Fortnite
    {
        net::setTestTransport([](const net::Request& req) {
            net::Response r;
            r.status = 200;
            if (req.url.find("/v2/titles") != std::string::npos) {
                r.body = R"({"results":[
                    {"titleId":"FORTNITE","details":{"productId":"BT5P2X999VH2","hasEntitlement":true,"isFreeInStore":true,"userPrograms":["F2P"]}},
                    {"titleId":"CYBERPUNK2077","details":{"productId":"BX3M8L83BBRW","hasEntitlement":true,"isFreeInStore":false,"userPrograms":[]}},
                    {"titleId":"HALOINFINITE","details":{"productId":"9PP5G1F0C2B6","hasEntitlement":true,"isFreeInStore":true,"userPrograms":["F2P"]}},
                    {"titleId":"CALLOFDUTYHQ","details":{"productId":"9N201KQXS5BM","hasEntitlement":true,"isFreeInStore":false,"userPrograms":["CALLISTO"]}}
                ]})";
            } else if (req.url.find("/v3/products") != std::string::npos) {
                r.body = R"({"Products":{
                    "BT5P2X999VH2":{"XCloudTitleId":"FORTNITE","ProductTitle":"Fortnite"},
                    "BX3M8L83BBRW":{"XCloudTitleId":"CYBERPUNK2077","ProductTitle":"Cyberpunk 2077"},
                    "9PP5G1F0C2B6":{"XCloudTitleId":"HALOINFINITE","ProductTitle":"Halo Infinite"},
                    "9N201KQXS5BM":{"XCloudTitleId":"CALLOFDUTYHQ","ProductTitle":"Call of Duty®"},
                    "9NCPSQQD1Z66":{"XCloudTitleId":"CALLOFDUTYHQ","ProductTitle":"Call of Duty®: Modern Warfare® 4"}
                }})";
            } else {
                r.status = 503;
            }
            return r;
        });

        app::Library libOwnedAll;
        std::string allCache = testDir + "/all_cache.json";
        libOwnedAll.setCachePath(allCache);
        std::atomic<bool> stop{false};
        libOwnedAll.loadOwned(gssv, [] {}, &stop);

        // Verify all 4 titles are in owned (Fortnite, Cyberpunk, Halo Infinite, Call of Duty)
        std::set<std::string> ownedPids;
        for (const auto& t : libOwnedAll.owned()) {
            ownedPids.insert(t.productId);
        }
        CHECK(ownedPids.count("BT5P2X999VH2")); // Fortnite
        CHECK(ownedPids.count("BX3M8L83BBRW")); // Cyberpunk 2077
        CHECK(ownedPids.count("9PP5G1F0C2B6")); // Halo Infinite
        CHECK(ownedPids.count("9NCPSQQD1Z66") || ownedPids.count("9N201KQXS5BM")); // COD
        CHECK(libOwnedAll.owned().size() >= 4);
    }

    // 8. Test loadCache auto-healing of contaminated gamePass and duplicate COD entries
    {
        std::string badCache = testDir + "/bad_cache.json";
        std::string gpItems = "";
        for (int i = 0; i < 1100; ++i) {
            if (i > 0) gpItems += ",";
            gpItems += "\"P_FAKE_" + std::to_string(i) + "\"";
        }
        std::string otItems = "";
        for (int i = 0; i < 150; ++i) {
            if (i > 0) otItems += ",";
            otItems += "\"T_FAKE_" + std::to_string(i) + "\"";
        }
        std::string codItems = "";
        for (int i = 0; i < 25; ++i) {
            if (i > 0) codItems += ",";
            codItems += "{\"p\":\"COD_" + std::to_string(i) + "\",\"t\":\"CALLOFDUTYHQ\"}";
        }
        std::string badJson = "{\"version\":2,\"market\":\"US\",\"language\":\"en-US\",\"products\":{},"
                              "\"owned\":[" + codItems + ",{\"p\":\"FORTNITE\",\"t\":\"FORTNITE\"}],"
                              "\"purchasable\":[],\"ownedTitles\":[" + otItems + "],\"ownedProducts\":[],\"gamePass\":[" + gpItems + "]}";
        platform::writeFileAtomic(badCache, badJson);

        app::Library healLib;
        healLib.setCachePath(badCache);
        CHECK(healLib.loadCache(false));
        // Verify ownership was invalidated due to duplicate COD entries from v1.5.0 bug
        CHECK(!healLib.ownedKnown());
        // Verify contaminated gamePass was discarded
        CHECK(healLib.gamePassSearchPool().empty());
    }

    // 9. Test loadCache preserves legitimate cache for users with few owned games (<= 37) and Game Pass
    {
        std::string legitCache = testDir + "/legit_cache.json";
        std::string otItems = "";
        for (int i = 0; i < 200; ++i) {
            if (i > 0) otItems += ",";
            otItems += "\"T_FAKE_" + std::to_string(i) + "\"";
        }
        std::string legitJson = "{\"version\":2,\"market\":\"US\",\"language\":\"en-US\",\"products\":{},"
                                "\"owned\":[{\"p\":\"FORTNITE\",\"t\":\"FORTNITE\"},{\"p\":\"CYBERPUNK\",\"t\":\"CYBERPUNK2077\"}],"
                                "\"purchasable\":[],\"ownedTitles\":[" + otItems + "],\"ownedProducts\":[\"FORTNITE\",\"CYBERPUNK\"],"
                                "\"gamePass\":[\"GP_GAME1\",\"GP_GAME2\"]}";
        platform::writeFileAtomic(legitCache, legitJson);

        app::Library legitLib;
        legitLib.setCachePath(legitCache);
        CHECK(legitLib.loadCache(false));
        // Verify legitimate ownership with <= 37 games is PRESERVED, not discarded!
        CHECK(legitLib.ownedKnown());
    }

    net::setTestTransport({});
    xc::fs::removeTree(testDir);
}

static void testBlock4OwnershipAndRecents() {
    using namespace xc;
    std::string testDir = "/tmp/test_bloco4_" + std::to_string(platform::nowMs());
    xc::fs::removeTree(testDir);
    xc::fs::makeDirs(testDir);
    std::string cacheFile = testDir + "/library_cache.json";

    // 1. Universal License Acquisition (markAsOwned) with 12-character Store BigId and cross-gen purging
    {
        app::Library lib;
        lib.setCachePath(cacheFile);

        // Preload catalog games including real 12-character Microsoft Store BigID (Just Dance)
        // and an alternate edition sharing the same titleId (TID_JD)
        CHECK(app::isStoreBigId("9P368QBR2C5S"));
        std::string jsonText = "{\"version\":2,\"market\":\"US\",\"language\":\"en-US\",\"products\":{"
                               "\"9P368QBR2C5S\":{\"title\":\"Just Dance 2026\",\"xt\":\"TID_JD\"},"
                               "\"9P368QBRALT0\":{\"title\":\"Just Dance 2026 Deluxe\",\"xt\":\"TID_JD\"},"
                               "\"PROD_STORE1\":{\"title\":\"Store Game 1\",\"xt\":\"TID_SG1\"},"
                               "\"PROD_OWNED1\":{\"title\":\"Owned Game 1\",\"xt\":\"TID_OG1\"}},"
                               "\"owned\":[{\"p\":\"PROD_OWNED1\",\"t\":\"TID_OG1\"}],"
                               "\"purchasable\":[{\"p\":\"9P368QBR2C5S\",\"t\":\"TID_JD\"},{\"p\":\"9P368QBRALT0\",\"t\":\"TID_JD\"},{\"p\":\"PROD_STORE1\",\"t\":\"TID_SG1\"}],"
                               "\"freeInStore\":[\"9P368QBR2C5S\"],"
                               "\"ownedTitles\":[\"TID_OG1\"],\"ownedProducts\":[\"PROD_OWNED1\"]}";
        platform::writeFileAtomic(cacheFile, jsonText);
        CHECK(lib.loadCache(false));

        CHECK(lib.owned().size() == 1);
        CHECK(lib.purchasable().size() == 2); // 9P368QBR2C5S and 9P368QBRALT0 deduplicate to 1 Just Dance tile, + Store Game 1 = 2

        // User acquires 12-character Store BigID Just Dance (9P368QBR2C5S)
        lib.markAsOwned("9P368QBR2C5S", "TID_JD", "Just Dance 2026");

        // Verify it is immediately added to owned, removed from purchasable, and freeInStore
        CHECK(lib.owned().size() == 2);
        // Both the acquired edition AND alternate cross-gen edition sharing TID_JD must be purged from purchasable
        CHECK(lib.purchasable().size() == 1);
        CHECK(lib.purchasable().front().productId == "PROD_STORE1");

        bool jdFound = false;
        for (const auto& t : lib.owned()) {
            if (t.productId == "9P368QBR2C5S") {
                jdFound = true;
                CHECK(t.name == "Just Dance 2026");
                CHECK(t.playable);
                CHECK(!t.purchasable);
                CHECK(t.freeInStore);
                break;
            }
        }
        CHECK(jdFound);

        // Verify 9P368QBR2C5S is placed at front of recent_ (first in owned)
        CHECK(lib.owned().front().productId == "9P368QBR2C5S");
    }

    // 2. Resilience against slow GSSV entitlement propagation during loadOwned & loading bar dismissal
    {
        net::setTestTransport([](const net::Request& req) {
            net::Response r;
            r.status = 200;
            if (req.url.find("/v2/titles") != std::string::npos) {
                // Microsoft has NOT propagated entitlement for 9P368QBR2C5S yet! hasEntitlement is false
                r.body = R"({"results":[
                    {"titleId":"TID_OG1","details":{"productId":"PROD_OWNED1","hasEntitlement":true,"isFreeInStore":false,"userPrograms":[]}},
                    {"titleId":"TID_JD","details":{"productId":"9P368QBR2C5S","hasEntitlement":false,"isFreeInStore":true,"userPrograms":[]}},
                    {"titleId":"TID_SG1","details":{"productId":"PROD_STORE1","hasEntitlement":false,"isFreeInStore":false,"userPrograms":[]}}
                ]})";
            } else if (req.url.find("/v3/products") != std::string::npos) {
                r.body = R"({"Products":{
                    "9P368QBR2C5S":{"XCloudTitleId":"TID_JD","ProductTitle":"Just Dance 2026"},
                    "PROD_STORE1":{"XCloudTitleId":"TID_SG1","ProductTitle":"Store Game 1"},
                    "PROD_OWNED1":{"XCloudTitleId":"TID_OG1","ProductTitle":"Owned Game 1"}
                }})";
            } else {
                r.status = 503;
            }
            return r;
        });

        app::Library lib;
        lib.setCachePath(cacheFile);
        CHECK(lib.loadCache(false));

        // Before loadOwned, 9P368QBR2C5S was marked as owned
        CHECK(lib.owned().size() == 2);

        xcloud::GssvClient gssv;
        std::atomic<bool> stop{false};
        bool progressSawActive = false;
        lib.loadOwned(gssv, [&]() {
            if (lib.progress().active) progressSawActive = true;
        }, &stop);

        // Verify progress is cleared to inactive after loadOwned completes (no infinite spinner!)
        CHECK(progressSawActive);
        CHECK(!lib.progress().active);

        // Verify loadCatalogNames also guarantees clearing progress even when missing is empty
        lib.loadCatalogNames([&]() {}, &stop);
        CHECK(!lib.progress().active);

        // Verify 9P368QBR2C5S is STILL retained in owned, NOT dropped to purchasable!
        CHECK(lib.owned().size() == 2);
        bool foundStore1 = false;
        bool foundJdInPurchasable = false;
        for (const auto& t : lib.purchasable()) {
            if (t.productId == "PROD_STORE1") foundStore1 = true;
            if (t.productId == "9P368QBR2C5S" || t.productId == "9P368QBRALT0") foundJdInPurchasable = true;
        }
        CHECK(foundStore1);
        CHECK(!foundJdInPurchasable);

        bool jdFound = false;
        for (const auto& t : lib.owned()) {
            if (t.productId == "9P368QBR2C5S") {
                jdFound = true;
                CHECK(t.playable);
                CHECK(!t.purchasable);
                CHECK(t.freeInStore);
                break;
            }
        }
        CHECK(jdFound);

        net::setTestTransport({});
    }

    // 3. Cache roundtrip and instant-resume recency ordering on boot
    {
        // Fresh library instance loading from cache
        app::Library bootLib;
        bootLib.setCachePath(cacheFile);
        std::map<std::string, uint64_t> playtime = {
            {"PROD_OWNED1", 1200}
        };
        bootLib.setPlaytime(playtime);
        CHECK(bootLib.loadCache(true));

        // 9P368QBR2C5S was acquired and at front of recent; PROD_OWNED1 has playtime
        auto owned = bootLib.owned();
        CHECK(owned.size() == 2);
        CHECK(owned[0].productId == "9P368QBR2C5S");
        CHECK(owned[0].freeInStore);
        CHECK(owned[1].productId == "PROD_OWNED1");
        bool bootFoundStore1 = false;
        bool bootFoundJdInPurchasable = false;
        for (const auto& t : bootLib.purchasable()) {
            if (t.productId == "PROD_STORE1") bootFoundStore1 = true;
            if (t.productId == "9P368QBR2C5S" || t.productId == "9P368QBRALT0") bootFoundJdInPurchasable = true;
        }
        CHECK(bootFoundStore1);
        CHECK(!bootFoundJdInPurchasable);

        // 4. L3 Refresh in-memory continuity across Library instances (setAcquired)
        app::Library l3Lib;
        l3Lib.setAcquired(bootLib.acquiredProducts(), bootLib.acquiredTitles());
        CHECK(l3Lib.acquiredProducts().count("9P368QBR2C5S") == 1);
        CHECK(l3Lib.acquiredTitles().count("TID_JD") == 1);
    }

    // 5. "Recentes primeiro" sorting with playtime tie-breaking
    {
        app::Library sortLib;
        sortLib.setCachePath(testDir + "/sort_cache.json");
        std::string sortJson = "{\"version\":2,\"market\":\"US\",\"language\":\"en-US\",\"products\":{"
                               "\"PID_A\":{\"title\":\"Alpha Game\",\"xt\":\"TID_A\"},"
                               "\"PID_B\":{\"title\":\"Beta Game\",\"xt\":\"TID_B\"},"
                               "\"PID_C\":{\"title\":\"Charlie Game\",\"xt\":\"TID_C\"},"
                               "\"PID_D\":{\"title\":\"Delta Game\",\"xt\":\"TID_D\"}},"
                               "\"owned\":[{\"p\":\"PID_A\",\"t\":\"TID_A\"},{\"p\":\"PID_B\",\"t\":\"TID_B\"},"
                               "{\"p\":\"PID_C\",\"t\":\"TID_C\"},{\"p\":\"PID_D\",\"t\":\"TID_D\"}],"
                               "\"purchasable\":[],\"ownedTitles\":[\"TID_A\",\"TID_B\",\"TID_C\",\"TID_D\"],"
                               "\"ownedProducts\":[\"PID_A\",\"PID_B\",\"PID_C\",\"PID_D\"]}";
        platform::writeFileAtomic(testDir + "/sort_cache.json", sortJson);
        CHECK(sortLib.loadCache(false));

        // Without recents or playtime: strictly alphabetical A -> B -> C -> D
        auto initialOwned = sortLib.owned();
        CHECK(initialOwned.size() == 4);
        CHECK(initialOwned[0].productId == "PID_A");
        CHECK(initialOwned[1].productId == "PID_B");
        CHECK(initialOwned[2].productId == "PID_C");
        CHECK(initialOwned[3].productId == "PID_D");

        // Now set playtime for Charlie (7200s) and Delta (3600s)
        sortLib.setPlaytime({{"PID_C", 7200}, {"PID_D", 3600}});
        auto ptOwned = sortLib.owned();
        // Charlie (7200) > Delta (3600) > Alpha (0, alphabetical) > Beta (0, alphabetical)
        CHECK(ptOwned[0].productId == "PID_C");
        CHECK(ptOwned[1].productId == "PID_D");
        CHECK(ptOwned[2].productId == "PID_A");
        CHECK(ptOwned[3].productId == "PID_B");

        // Now record Beta as most recently played
        sortLib.recordRecent("PID_B");
        auto recentOwned = sortLib.owned();
        // Beta (recent) > Charlie (played 7200) > Delta (played 3600) > Alpha (unplayed)
        CHECK(recentOwned[0].productId == "PID_B");
        CHECK(recentOwned[1].productId == "PID_C");
        CHECK(recentOwned[2].productId == "PID_D");
        CHECK(recentOwned[3].productId == "PID_A");
    }

    // 6. AppUi::markTileOwned moves 12-character Store BigID tile from allPurchasable_ to allOwned_ and rebuilds
    {
        ui::Fonts dummyFonts;
        ui::ImageCache images([] {});
        ui::AppUi appUi(dummyFonts, images);
        appUi.setSubscription("Free");
        appUi.setMembership(false);

        ui::GameTile t1, t2;
        t1.productId = "PID_OWNED";
        t1.titleId = "TID_OWNED";
        t1.name = "Owned Title";
        t1.playable = true;

        t2.productId = "9P368QBR2C5S";
        t2.titleId = "TID_JD";
        t2.name = "Just Dance 2026";
        t2.playable = false;
        t2.purchasable = true;
        t2.freeInStore = true;

        appUi.setOwned({t1}, {t2}, true);
        ui::GameTile out;
        CHECK(appUi.firstTile(out));
        CHECK(out.productId == "PID_OWNED");

        // Before claim, 9P368QBR2C5S is purchasable
        CHECK(appUi.purchasableAt(0, out));
        CHECK(out.productId == "9P368QBR2C5S");

        // Claim 9P368QBR2C5S
        appUi.markTileOwned("9P368QBR2C5S", "TID_JD");

        // Now 9P368QBR2C5S should be in owned, playable, not purchasable, and still freeInStore so Free accounts can play it!
        CHECK(!appUi.purchasableAt(0, out)); // purchasable is now empty!
        CHECK(appUi.findTile("TID_JD", out));
        CHECK(out.productId == "9P368QBR2C5S");
        CHECK(out.playable);
        CHECK(!out.purchasable);
        CHECK(out.freeInStore);
        CHECK(appUi.isEffectivePlayable(out));
    }

    xc::fs::removeTree(testDir);
}

static void testSizedUrl() {
    // Protocol-relative URLs
    CHECK(ui::sizedUrl("//store-images.s-microsoft.com/image.png", 300, 450) ==
          "https://store-images.s-microsoft.com/image.png?w=300&h=450&mode=scale&q=75&format=jpg");
    // store-images q=75
    CHECK(ui::sizedUrl("https://store-images.s-microsoft.com/image.png", 1920, 1080) ==
          "https://store-images.s-microsoft.com/image.png?w=1920&h=1080&mode=scale&q=75&format=jpg");
    CHECK(ui::sizedUrl("https://store-images.s-microsoft.com/image.png?param=1", 100, 100) ==
          "https://store-images.s-microsoft.com/image.png?param=1&w=100&h=100&mode=scale&q=75&format=jpg");
    // images-eds-ssl.xboxlive.com format=jpg without quality
    CHECK(ui::sizedUrl("https://images-eds-ssl.xboxlive.com/image.png", 400, 600) ==
          "https://images-eds-ssl.xboxlive.com/image.png?w=400&h=600&format=jpg");
    CHECK(ui::sizedUrl("https://images-eds-ssl.xboxlive.com/image.png?foo=bar", 200, 300) ==
          "https://images-eds-ssl.xboxlive.com/image.png?foo=bar&w=200&h=300&format=jpg");
    // an EDS URL that already names a format keeps it (a second format= is HTTP 400),
    // and dimensions below 64 are clamped to 64x64 to avoid EDS HTTP 400 rejection
    CHECK(ui::sizedUrl("https://images-eds-ssl.xboxlive.com/image?url=abc&format=png", 20, 20) ==
          "https://images-eds-ssl.xboxlive.com/image?url=abc&format=png&w=64&h=64");
    // other URLs untouched
    CHECK(ui::sizedUrl("https://assets.xbox.com/img.png", 100, 100) == "https://assets.xbox.com/img.png");
    // invalid dimensions or empty string untouched
    CHECK(ui::sizedUrl("", 100, 100).empty());
    CHECK(ui::sizedUrl("https://store-images.s-microsoft.com/image.png", 0, 100) ==
          "https://store-images.s-microsoft.com/image.png");
    CHECK(ui::sizedUrl("https://images-eds-ssl.xboxlive.com/image.png", 100, -1) ==
          "https://images-eds-ssl.xboxlive.com/image.png");
}

void runXcloudTests() {
    testRegions();
    testPrices();
    testTitleListRetriesAndIncompleteLibrary();
    testDetailsHydrationAndPlaytimeBadge();
    testLibraryRowsOnce();
    testStoreLeftovers();
    testPurchase();
    testWishlist();
    testAchievements();
    testLibraryOfflineCache();
    testCodSearchRomanAndSettingsReset();
    testCallOfDutyModernWarfare4CatalogAndRefresh();
    testBlock4OwnershipAndRecents();
    testSizedUrl();
}
