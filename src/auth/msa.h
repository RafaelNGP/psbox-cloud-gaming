// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Microsoft account (MSA) OAuth via login.microsoftonline.com (v2.0) and
// legacy login.live.com (for existing stored accounts): device-code sign-in
// and refresh. The device-code flow fits a console: the TV shows a short code
// (and a pre-filled QR code) and the user signs in on a phone.
#pragma once

#include <cstdint>
#include <string>

namespace xc::auth {

constexpr const char* kV2ClientId = "1f907974-e22b-4810-a9de-d9647380c97e";
constexpr const char* kV2Scope = "xboxlive.signin openid profile offline_access";
constexpr const char* kLegacyClientId = "000000004c20a908";
constexpr const char* kLegacyScope = "service::user.auth.xboxlive.com::MBI_SSL";

struct MsaConfig {
    // Public OAuth v2.0 client of play.xbox.com / Xbox TV: supports prefilled
    // QR sign-in links (microsoft.com/link?otc=CODE) as well as Passport
    // console-transfer tokens. Existing accounts stored with kLegacyClientId
    // continue renewing on login.live.com without forcing a re-sign-in.
    std::string clientId = kV2ClientId;
    std::string scope = kV2Scope;
    bool oauthV2 = true;

    static MsaConfig legacy() {
        MsaConfig c;
        c.clientId = kLegacyClientId;
        c.scope = kLegacyScope;
        c.oauthV2 = false;
        return c;
    }
};

struct DeviceCode {
    std::string userCode;
    std::string deviceCode;
    std::string verificationUri;  // e.g. https://www.microsoft.com/link
    int intervalSec = 5;
    int expiresInSec = 900;
};

struct MsaTokens {
    std::string accessToken;
    std::string refreshToken;
    std::string userId;
    int64_t expiresAt = 0;  // unix seconds
};

enum class PollResult { Pending, SlowDown, Success, Expired, Denied, Error };

class MsaClient {
public:
    explicit MsaClient(MsaConfig cfg = {}) : cfg_(std::move(cfg)) {}

    bool startDeviceCode(DeviceCode& out, std::string& err) const;
    PollResult pollDeviceCode(const DeviceCode& dc, MsaTokens& out, std::string& err) const;
    // `scope` empty = the configured sign-in scope. `networkError` (optional)
    // tells a transport failure apart from a rejected token. `invalidGrant`
    // (optional) is true only when Microsoft explicitly returned "invalid_grant".
    bool refresh(const std::string& refreshToken, MsaTokens& out, std::string& err,
                 const std::string& scope = {}, bool* networkError = nullptr,
                 bool* invalidGrant = nullptr, bool* rejected = nullptr) const;

    const MsaConfig& config() const { return cfg_; }

private:
    MsaConfig cfg_;
};

int64_t unixNow();

}  // namespace xc::auth
