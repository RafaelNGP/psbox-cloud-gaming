// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "auth/msa.h"

#include "net/http.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <ctime>

namespace xc::auth {

namespace {
constexpr const char* kV2DeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
constexpr const char* kV2TokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
constexpr const char* kLegacyConnectUrl = "https://login.live.com/oauth20_connect.srf";
constexpr const char* kLegacyTokenUrl = "https://login.live.com/oauth20_token.srf";

bool isPassportScope(const std::string& scope) { return scope.rfind("service::", 0) == 0; }

net::Response postForm(const char* url, const std::vector<std::pair<std::string, std::string>>& fields) {
    net::Request req;
    req.method = "POST";
    req.url = url;
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    req.body = net::formEncode(fields);
    return net::perform(req);
}


bool readTokens(const json::Value& v, MsaTokens& out) {
    // The console-transfer purpose answers with "lpt" instead of access_token.
    out.accessToken = v["access_token"].str(v["lpt"].str());
    if (v["refresh_token"].isString()) out.refreshToken = v["refresh_token"].str();
    out.userId = v["user_id"].str(out.userId);
    out.expiresAt = unixNow() + v["expires_in"].asInt(3600);
    return !out.accessToken.empty();
}

std::string describe(const net::Response& r) {
    if (r.status == 0) return r.error;
    auto j = json::parse(r.body);
    std::string msg = "HTTP " + std::to_string(r.status);
    if (j) {
        if (auto e = (*j)["error"].str(); !e.empty()) msg += " " + e;
        if (auto d = (*j)["error_description"].str(); !d.empty()) msg += ": " + d;
    }
    return msg;
}
}  // namespace

int64_t unixNow() { return static_cast<int64_t>(std::time(nullptr)); }

bool MsaClient::startDeviceCode(DeviceCode& out, std::string& err) const {
    net::Response r;
    constexpr int kTries = 3;
    const char* url = cfg_.oauthV2 ? kV2DeviceCodeUrl : kLegacyConnectUrl;
    std::vector<std::pair<std::string, std::string>> fields = {
        {"client_id", cfg_.clientId},
        {"scope", cfg_.scope},
    };
    if (!cfg_.oauthV2) fields.emplace_back("response_type", "device_code");
    for (int attempt = 1; attempt <= kTries; ++attempt) {
        r = postForm(url, fields);
        bool transient = r.status == 0 || r.status == 408 || r.status == 429 || r.status >= 500;
        if (!transient || attempt == kTries) break;
        XC_LOGW("device code request: %s, trying again (%d/%d)", describe(r).c_str(), attempt + 1, kTries);
        platform::sleepMs(1000u << (attempt - 1));
    }
    auto j = json::parse(r.body);
    if (!r.ok() || !j) {
        err = "device code request failed: " + describe(r);
        return false;
    }
    out.userCode = (*j)["user_code"].str();
    out.deviceCode = (*j)["device_code"].str();
    out.verificationUri = (*j)["verification_uri"].str("https://www.microsoft.com/link");
    out.intervalSec = static_cast<int>(std::clamp<int64_t>((*j)["interval"].asInt(5), 1, 60));
    out.expiresInSec = static_cast<int>(std::clamp<int64_t>((*j)["expires_in"].asInt(900), 60, 3600));
    if (out.userCode.empty() || out.deviceCode.empty()) {
        err = "device code response missing fields";
        return false;
    }
    return true;
}

PollResult MsaClient::pollDeviceCode(const DeviceCode& dc, MsaTokens& out, std::string& err) const {
    const char* url = cfg_.oauthV2 ? kV2TokenUrl : kLegacyTokenUrl;
    auto r = postForm(url, {{"client_id", cfg_.clientId},
                            {"device_code", dc.deviceCode},
                            {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}});
    if (r.status == 0 || r.status >= 500) {
        err = describe(r);
        return PollResult::Pending;  // the network or the service, for now: keep polling
    }
    if (r.status == 429) {
        err = describe(r);
        return PollResult::SlowDown;  // too many requests
    }
    auto j = json::parse(r.body);
    if (r.ok() && j && readTokens(*j, out)) return PollResult::Success;
    std::string e = j ? (*j)["error"].str() : std::string();
    if (e == "authorization_pending") return PollResult::Pending;
    if (e == "slow_down") return PollResult::SlowDown;
    if (e == "expired_token") return PollResult::Expired;
    if (e == "authorization_declined" || e == "access_denied") return PollResult::Denied;
    err = describe(r);
    return PollResult::Error;
}

bool MsaClient::refresh(const std::string& refreshToken, MsaTokens& out, std::string& err,
                        const std::string& scope, bool* networkError, bool* invalidGrant, bool* rejected) const {
    net::Response r;
    constexpr int kTries = 3;
    const std::string effectiveScope = scope.empty() ? cfg_.scope : scope;
    // Passport purpose scopes (e.g. console transfer token for /connect) are issued by
    // login.live.com even when the account signed in through login.microsoftonline.com v2.0.
    const char* url = (cfg_.oauthV2 && !isPassportScope(effectiveScope)) ? kV2TokenUrl : kLegacyTokenUrl;
    for (int attempt = 1; attempt <= kTries; ++attempt) {
        r = postForm(url, {{"client_id", cfg_.clientId},
                           {"grant_type", "refresh_token"},
                           {"refresh_token", refreshToken},
                           {"scope", effectiveScope}});
        bool transient = r.status == 0 || r.status == 408 || r.status == 429 || r.status >= 500;
        if (!transient || attempt == kTries) break;
        XC_LOGW("token refresh: %s, trying again (%d/%d)", describe(r).c_str(), attempt + 1, kTries);
        platform::sleepMs(1000u << (attempt - 1));
    }
    // Throttled or timed out isn't a sign-in gone bad: the stored one stays.
    if (networkError) *networkError = r.status == 0 || r.status >= 500 || r.status == 429 || r.status == 408;
    auto j = json::parse(r.body);
    const std::string oauthError = j && j->isObject() ? (*j)["error"].str() : std::string();
    if (invalidGrant) *invalidGrant = (oauthError == "invalid_grant");
    // The sign-in service itself said no (its own JSON error on a 4xx that isn't "slow down"): this
    // token won't ever be taken. A page from a proxy or a captive portal carries no such error.
    if (rejected)
        *rejected = r.status >= 400 && r.status < 500 && r.status != 408 && r.status != 429 && !oauthError.empty();
    if (!r.ok() || !j || !readTokens(*j, out)) {
        err = "token refresh failed: " + describe(r);
        return false;
    }
    if (out.refreshToken.empty()) out.refreshToken = refreshToken;
    return true;
}

}  // namespace xc::auth
