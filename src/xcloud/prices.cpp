// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "xcloud/prices.h"

#include "net/http.h"
#include "util/json.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>

namespace xc::xcloud {

bool fetchPrices(const std::vector<std::string>& productIds, const std::string& market, const std::string& language,
                 std::map<std::string, Price>& out, std::string& err) {
    constexpr size_t kBatch = 20;
    // The game's own sale among a product's availabilities. It has "Browse"
    // too: a free trial of a game sold only in bundles has "Purchase" at 0
    // without it, and taking that for the price showed such games as free
    // (getting them gave a trial licence the cloud does not play).
    auto ownPrice = [](const json::Value& p, Price& pr) {
        for (const auto& dsa : p["DisplaySkuAvailabilities"].items()) {
            if (dsa["Sku"]["SkuType"].str() == "trial" || dsa["Sku"]["Properties"]["IsTrial"].asBool()) continue;
            for (const auto& av : dsa["Availabilities"].items()) {
                bool purchase = false, browse = false;
                for (const auto& a : av["Actions"].items()) {
                    const auto act = a.str();
                    if (act == "Purchase") purchase = true;
                    if (act == "Browse") browse = true;
                }
                const auto& price = av["OrderManagementData"]["Price"];
                if (!purchase || !browse || !price.isObject() || av["RemediationRequired"].asBool()) continue;
                pr.list = price["ListPrice"].asNumber();
                pr.msrp = price["MSRP"].asNumber();
                pr.currency = price["CurrencyCode"].str();
                return true;
            }
        }
        return false;
    };
    auto fetch = [&](const std::vector<std::string>& idsIn, bool full, const std::function<void(const json::Value&)>& each) {
        for (size_t i = 0; i < idsIn.size(); i += kBatch) {
            std::string ids;
            for (size_t k = i; k < idsIn.size() && k < i + kBatch; ++k) ids += (ids.empty() ? "" : ",") + idsIn[k];
            net::Request req;
            req.url = "https://displaycatalog.mp.microsoft.com/v7.0/products?bigIds=" + ids + "&market=" + market +
                      "&languages=" + language + (full ? "" : "&fieldsTemplate=browse");
            auto r = net::perform(req);
            auto j = json::parse(r.body);
            if (!r.ok() || !j || !(*j)["Products"].isArray()) {
                err = "prices: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
                return false;
            }
            for (const auto& p : (*j)["Products"].items()) each(p);
        }
        return true;
    };
    if (!fetch(productIds, false, [&](const json::Value& p) {
            Price pr;
            if (ownPrice(p, pr)) out[p["ProductId"].str()] = pr;
        }))
        return false;

    // A game with no sale of its own is sold by other products of the store: the
    // editions and bundles that contain it (Devil May Cry 5 through "Devil May
    // Cry 5 + Vergil", an Xbox One release through the edition of the same
    // name). Its price is then the cheapest of those; the store's page of the
    // game lists them. A failure here leaves these without a price, no more.
    std::vector<std::string> noPrice;
    for (const auto& id : productIds)
        if (!out.count(id)) noPrice.push_back(id);
    if (noPrice.empty()) return true;
    std::map<std::string, std::vector<std::string>> soldBy;  // product -> the products that contain it
    std::vector<std::string> sellers;
    if (!fetch(noPrice, true, [&](const json::Value& p) {
            std::vector<std::string>& list = soldBy[p["ProductId"].str()];
            for (const auto& mp : p["MarketProperties"].items())
                for (const auto& rel : mp["RelatedProducts"].items()) {
                    const std::string kind = rel["RelationshipType"].str();
                    if ((kind == "Bundle" || kind == "SellableBy") && list.size() < 16) {
                        list.push_back(rel["RelatedProductId"].str());
                        sellers.push_back(list.back());
                    }
                }
        })) {
        return false;
    }
    std::sort(sellers.begin(), sellers.end());
    sellers.erase(std::unique(sellers.begin(), sellers.end()), sellers.end());
    std::map<std::string, Price> sellerPrice;
    if (!sellers.empty() && !fetch(sellers, false, [&](const json::Value& p) {
            Price pr;
            if (ownPrice(p, pr) && pr.list > 0.005) sellerPrice[p["ProductId"].str()] = pr;
        })) {
        return false;
    }
    for (const auto& [id, list] : soldBy) {
        const Price* best = nullptr;
        for (const auto& seller : list) {
            auto it = sellerPrice.find(seller);
            if (it != sellerPrice.end() && (!best || it->second.list < best->list)) best = &it->second;
        }
        if (best) out[id] = *best;
    }
    return true;
}

std::string formatPrice(double amount, const std::string& currency) {
    long cents = std::lround(amount * 100);
    long whole = cents / 100, frac = cents % 100;
    // Thousands with `sep`, cents after `dec`.
    auto number = [&](char sep, char dec) {
        std::string digits = std::to_string(whole), grouped;
        for (size_t i = 0; i < digits.size(); ++i) {
            if (i && (digits.size() - i) % 3 == 0) grouped += sep;
            grouped += digits[i];
        }
        char tail[8];
        std::snprintf(tail, sizeof tail, "%c%02ld", dec, frac);
        return grouped + tail;
    };
    if (currency == "BRL") return "R$ " + number('.', ',');
    if (currency == "USD") return "$" + number(',', '.');
    if (currency == "CAD") return "CA$" + number(',', '.');
    if (currency == "MXN") return "MX$" + number(',', '.');
    if (currency == "GBP") return "\xC2\xA3" + number(',', '.');
    if (currency == "EUR") return number('.', ',') + " \xE2\x82\xAC";
    if (currency == "JPY") return "\xC2\xA5" + std::to_string(std::lround(amount));
    return currency + " " + number(',', '.');
}

}  // namespace xc::xcloud
