// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "auth/account_seal.h"

#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace xc::auth {
namespace {

std::string b64(const uint8_t* data, size_t len) {
    std::vector<unsigned char> out(len * 4 / 3 + 8);
    size_t n = 0;
    if (mbedtls_base64_encode(out.data(), out.size(), &n, data, len) != 0) return {};
    return std::string(reinterpret_cast<const char*>(out.data()), n);
}

bool unb64(const std::string& text, std::vector<uint8_t>& out) {
    out.assign(text.size(), 0);
    size_t n = 0;
    if (mbedtls_base64_decode(out.data(), out.size(), &n, reinterpret_cast<const unsigned char*>(text.data()), text.size()) != 0)
        return false;
    out.resize(n);
    return true;
}

// What only this device has. A random key file where the system offers no id.
std::string deviceSecret() {
    // The machine's id where there is one (a desktop). The consoles have none this app can
    // read (the PS5 does not export its OpenPsId to it): they use the key file below.
    for (const char* path : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        std::string id;
        if (platform::readFile(path, id) && id.size() >= 16) return "machine:" + id;
    }
    const std::string keyPath = platform::dataDir() + "/.device_key";
    std::string key;
    if (platform::readFile(keyPath, key) && key.size() == 32) return "file:" + key;
    uint8_t fresh[32];
    if (!platform::randomBytes(fresh, sizeof fresh)) return {};
    key.assign(reinterpret_cast<const char*>(fresh), sizeof fresh);
    if (!platform::writeFileAtomic(keyPath, key)) return {};
    platform::makePrivate(keyPath);
    return "file:" + key;
}

bool deviceKey(uint8_t key[32]) {
    const std::string secret = deviceSecret();
    if (secret.empty()) return false;
    const std::string material = "psbox-account-seal-v2\n" + secret;
    return mbedtls_sha256(reinterpret_cast<const unsigned char*>(material.data()), material.size(), key, 0) == 0;
}

constexpr size_t kNonce = 12, kTag = 16;

}  // namespace

bool isSealedAccount(const std::string& text) {
    auto j = json::parse(text);
    return j && j->isObject() && (*j)["version"].asInt(0) == 2 && (*j)["data"].isString();
}

std::string sealAccount(const std::string& plain) {
    uint8_t key[32], nonce[kNonce];
    if (!deviceKey(key) || !platform::randomBytes(nonce, sizeof nonce)) return {};
    std::vector<uint8_t> out(plain.size() + kTag);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0)
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain.size(), nonce, sizeof nonce, nullptr, 0,
                                       reinterpret_cast<const unsigned char*>(plain.data()), out.data(), kTag,
                                       out.data() + plain.size());
    mbedtls_gcm_free(&gcm);
    std::memset(key, 0, sizeof key);
    if (rc != 0) return {};
    json::Value v = json::Value::object();
    v.set("version", 2);
    v.set("cipher", "aes-256-gcm");
    v.set("nonce", b64(nonce, sizeof nonce));
    v.set("data", b64(out.data(), out.size()));
    return v.dump();
}

bool unsealAccount(const std::string& sealed, std::string& plain) {
    auto j = json::parse(sealed);
    if (!j || !j->isObject() || (*j)["version"].asInt(0) != 2) return false;
    std::vector<uint8_t> nonce, data;
    if (!unb64((*j)["nonce"].str(), nonce) || !unb64((*j)["data"].str(), data)) return false;
    if (nonce.size() != kNonce || data.size() < kTag) return false;
    uint8_t key[32];
    if (!deviceKey(key)) return false;
    const size_t len = data.size() - kTag;
    std::vector<uint8_t> out(len + 1);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0)
        rc = mbedtls_gcm_auth_decrypt(&gcm, len, nonce.data(), nonce.size(), nullptr, 0, data.data() + len, kTag, data.data(),
                                      out.data());
    mbedtls_gcm_free(&gcm);
    std::memset(key, 0, sizeof key);
    if (rc != 0) return false;  // another device's, or altered
    plain.assign(reinterpret_cast<const char*>(out.data()), len);
    return true;
}

}  // namespace xc::auth
