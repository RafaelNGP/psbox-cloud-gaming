// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "app/updater.h"

#include "net/http.h"
#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#if defined(MBEDTLS_PSA_CRYPTO_C)
#include <psa/crypto.h>
#endif

// stb_image's inflate, for the deflated entries (its own static copy).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "util/fs.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace xc::app {

const char* const kReleaseKey =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEGyPKcF2L65xh03F1ZRzfhv48LOWS\n"
    "OBKQ55SVLod7HOZZiViUTbY47qETC8l9A1ez1RflBTH94WILhlHhC4xTeQ==\n"
    "-----END PUBLIC KEY-----\n";

namespace {

constexpr size_t kMaxUnpacked = 256u << 20;  // far above a release (~50 MB)

uint32_t le16(const std::string& s, size_t at) {
    return static_cast<uint8_t>(s[at]) | static_cast<uint8_t>(s[at + 1]) << 8;
}
uint32_t le32(const std::string& s, size_t at) {
    return le16(s, at) | le16(s, at + 2) << 16;
}

std::string bytesToHex(const unsigned char* bytes, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    std::string out(len * 2, '\0');
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = kHex[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[bytes[i] & 0x0F];
    }
    return out;
}

bool isHex64(const std::string& token, std::string& lowerOut) {
    if (token.size() != 64) return false;
    lowerOut.resize(64);
    for (size_t i = 0; i < 64; ++i) {
        unsigned char c = static_cast<unsigned char>(token[i]);
        if (c >= '0' && c <= '9') {
            lowerOut[i] = static_cast<char>(c);
        } else if (c >= 'a' && c <= 'f') {
            lowerOut[i] = static_cast<char>(c);
        } else if (c >= 'A' && c <= 'F') {
            lowerOut[i] = static_cast<char>(c - 'A' + 'a');
        } else {
            return false;
        }
    }
    return true;
}

std::string sanitizeBrackets(std::string s) {
    for (char& c : s) {
        if (c == '[' || c == ']') c = '.';
    }
    return s;
}

std::string sanitizeAssetFilename(const std::string& rawName, const std::string& fallbackName) {
    if (rawName.empty()) return fallbackName;
    size_t slash = rawName.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? rawName : rawName.substr(slash + 1);
    if (base.empty() || base == "." || base == ".." || base.find("..") != std::string::npos) {
        return fallbackName;
    }
    return base;
}

bool download(const std::string& url, long size, std::string& out, const std::function<void(double)>& progress,
              std::string& err) {
    net::Request req;
    req.url = url;
    req.followRedirects = true;  // GitHub hands the files out from another host
    req.timeoutMs = 30000;
    req.headers = {{"Accept", "application/octet-stream"}, {"User-Agent", "PSBox-Cloud-Gaming"}};
    out.clear();
    if (size > 0) out.reserve(static_cast<size_t>(size));
    req.onBody = [&](const char* data, size_t len) {
        if (out.size() + len > kMaxUnpacked) return false;
        out.append(data, len);
        return true;
    };
    if (progress)
        req.onProgress = [&](size_t got, long total) {
            if (total <= 0) total = size;
            progress(total > 0 ? std::min(1.0, static_cast<double>(got) / static_cast<double>(total)) : -1);
        };
    auto r = net::perform(req);
    if (!r.ok()) {
        err = "download " + url + ": " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    if (out.empty() && !r.body.empty()) {
        if (r.body.size() > kMaxUnpacked) {
            err = "download " + url + ": body too large";
            return false;
        }
        out = r.body;
    }
    if (size > 0 && static_cast<long>(out.size()) != size) {
        err = "download " + url + ": " + std::to_string(out.size()) + " bytes, " + std::to_string(size) + " expected";
        return false;
    }
    return true;
}

bool downloadToFile(const std::string& url, long size, const std::string& dstPath,
                    const std::function<void(double)>& progress, std::string& err) {
    if (!fs::makeDirsForFile(dstPath)) {
        err = "could not create directory for " + dstPath;
        return false;
    }
    FILE* f = std::fopen(dstPath.c_str(), "wb");
    if (!f) {
        err = "could not open " + dstPath + " for writing";
        return false;
    }
    size_t written = 0;
    bool writeErr = false;
    net::Request req;
    req.url = url;
    req.followRedirects = true;
    req.timeoutMs = 60000;
    req.headers = {{"Accept", "application/octet-stream"}, {"User-Agent", "PSBox-Cloud-Gaming"}};
    req.onBody = [&](const char* data, size_t len) {
        if (written + len > kMaxUnpacked) {
            writeErr = true;
            return false;
        }
        if (std::fwrite(data, 1, len, f) != len) {
            writeErr = true;
            return false;
        }
        written += len;
        return true;
    };
    if (progress) {
        req.onProgress = [&](size_t got, long total) {
            if (total <= 0) total = size;
            progress(total > 0 ? std::min(1.0, static_cast<double>(got) / static_cast<double>(total)) : -1);
        };
    }
    auto r = net::perform(req);
    if (r.ok() && written == 0 && !r.body.empty()) {
        if (r.body.size() > kMaxUnpacked || std::fwrite(r.body.data(), 1, r.body.size(), f) != r.body.size()) {
            writeErr = true;
        } else {
            written = r.body.size();
        }
    }
    bool flushOk = (std::fflush(f) == 0);
    bool closeOk = (std::fclose(f) == 0);
    if (writeErr || !flushOk || !closeOk) {
        fs::removeFile(dstPath);
        err = "could not write " + dstPath;
        return false;
    }
    if (!r.ok()) {
        fs::removeFile(dstPath);
        err = "download " + url + ": " + (r.status ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    if (size > 0 && static_cast<long>(written) != size) {
        fs::removeFile(dstPath);
        err = "download " + url + ": " + std::to_string(written) + " bytes, " + std::to_string(size) + " expected";
        return false;
    }
    return true;
}

bool computeFileSha256(const std::string& filePath, unsigned char outHash[32], std::string& err) {
    FILE* f = std::fopen(filePath.c_str(), "rb");
    if (!f) {
        err = "could not read " + filePath;
        return false;
    }
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    int rc = mbedtls_sha256_starts(&ctx, 0);
    unsigned char buf[16384];
    size_t n = 0;
    while (rc == 0 && (n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        rc = mbedtls_sha256_update(&ctx, buf, n);
    }
    if (std::ferror(f)) rc = -1;
    std::fclose(f);
    if (rc == 0) {
        rc = mbedtls_sha256_finish(&ctx, outHash);
    }
    mbedtls_sha256_free(&ctx);
    if (rc != 0) {
        err = "sha256 computation failed for " + filePath;
        return false;
    }
    return true;
}

bool verifyHashSignature(const unsigned char hash[32], const std::string& sigDer, const char* publicKeyPem,
                         std::string& err) {
#if defined(MBEDTLS_PSA_CRYPTO_C)
    psa_crypto_init();
#endif
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(&pk, reinterpret_cast<const unsigned char*>(publicKeyPem),
                                         std::strlen(publicKeyPem) + 1);
    if (rc == 0)
        rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32,
                               reinterpret_cast<const unsigned char*>(sigDer.data()), sigDer.size());
    mbedtls_pk_free(&pk);
    if (rc != 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "-0x%04x", static_cast<unsigned>(-rc));
        err = std::string("the signature doesn't match (") + buf + ")";
    }
    return rc == 0;
}

bool verifyFileSignature(const std::string& filePath, const std::string& sigDer, const char* publicKeyPem,
                         std::string& err) {
    unsigned char hash[32];
    if (!computeFileSha256(filePath, hash, err)) return false;
    return verifyHashSignature(hash, sigDer, publicKeyPem, err);
}

// The files in place go back where they were (an update cut short). True
// when there were some; `complete` false when one couldn't go back (then
// update.old and the journal must stay, for the next start to try again).
bool rollBack(const std::string& dir, bool& complete) {
    std::vector<std::string> old = fs::listTree(dir + "/update.old");
    complete = true;
    for (const auto& rel : old) {
        std::string target = dir + "/" + rel;
        if (!fs::makeDirsForFile(target) || !fs::renameReplace(dir + "/update.old/" + rel, target)) {
            XC_LOGE("update: could not put back %s", rel.c_str());
            complete = false;
        }
    }
    return !old.empty();
}

}  // namespace

std::string sha256Hex(const std::string& data) {
    unsigned char hash[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash, 0);
    return bytesToHex(hash, sizeof(hash));
}

std::string parseSha256Manifest(const std::string& manifestText, const std::string& expectedFilename) {
    std::string firstValidHash;
    std::string expectedSanitized = sanitizeBrackets(expectedFilename);

    size_t pos = 0;
    while (pos < manifestText.size()) {
        size_t eol = manifestText.find('\n', pos);
        if (eol == std::string::npos) eol = manifestText.size();
        std::string line = manifestText.substr(pos, eol - pos);
        pos = eol + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        if (lead > 0) line.erase(0, lead);
        if (line.empty() || line[0] == '#') continue;

        // Format 1: BSD style "SHA256 (filename) = <64-hex>"
        size_t eq = line.rfind('=');
        if (eq != std::string::npos && line.rfind("SHA256", 0) == 0) {
            std::string candidate = line.substr(eq + 1);
            while (!candidate.empty() && (candidate.front() == ' ' || candidate.front() == '\t')) {
                candidate.erase(0, 1);
            }
            std::string lower;
            if (isHex64(candidate, lower)) {
                if (firstValidHash.empty()) firstValidHash = lower;
                if (!expectedFilename.empty() &&
                    (line.find(expectedFilename) != std::string::npos ||
                     line.find(expectedSanitized) != std::string::npos)) {
                    return lower;
                }
            }
            continue;
        }

        // Format 2: GNU sha256sum "<64-hex>  filename" or "<64-hex> *filename" or raw "<64-hex>"
        size_t space = 0;
        while (space < line.size() && line[space] != ' ' && line[space] != '\t') ++space;
        std::string token = line.substr(0, space);
        std::string lower;
        if (isHex64(token, lower)) {
            if (firstValidHash.empty()) firstValidHash = lower;
            if (!expectedFilename.empty() && space < line.size()) {
                std::string rest = line.substr(space);
                while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == '*')) {
                    rest.erase(0, 1);
                }
                if (rest == expectedFilename || sanitizeBrackets(rest) == expectedSanitized) {
                    return lower;
                }
            }
        }
    }
    return firstValidHash;
}

bool verifySha256(const std::string& data, const std::string& manifestOrHex, std::string& err,
                  const std::string& expectedFilename) {
    std::string expected = parseSha256Manifest(manifestOrHex, expectedFilename);
    if (expected.empty()) {
        err = "invalid sha256 manifest";
        return false;
    }
    std::string actual = sha256Hex(data);
    if (actual != expected) {
        err = "sha256 mismatch: expected " + expected + ", got " + actual;
        return false;
    }
    return true;
}

bool verifyFileSha256(const std::string& filePath, const std::string& manifestOrHex, std::string& err,
                      const std::string& expectedFilename) {
    std::string expected = parseSha256Manifest(manifestOrHex, expectedFilename);
    if (expected.empty()) {
        err = "invalid sha256 manifest";
        return false;
    }
    unsigned char hash[32];
    if (!computeFileSha256(filePath, hash, err)) return false;
    std::string actual = bytesToHex(hash, sizeof(hash));
    if (actual != expected) {
        err = "sha256 mismatch: expected " + expected + ", got " + actual;
        return false;
    }
    return true;
}

bool verifySignature(const std::string& data, const std::string& sigDer, const char* publicKeyPem, std::string& err) {
    unsigned char hash[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash, 0);
    return verifyHashSignature(hash, sigDer, publicKeyPem, err);
}

std::string buildPs4GoldHenInstallPayload(const std::string& pkgPath) {
    std::string escaped;
    escaped.reserve(pkgPath.size());
    for (char c : pkgPath) {
        if (c == '\\' || c == '"') escaped += '\\';
        escaped += c;
    }
    return "{\"type\":\"direct\",\"packages\":[\"" + escaped + "\"]}";
}

std::string buildWindowsUpdateBatchScript(const std::string& appDir, const std::string& stagingDir, uint32_t pid) {
    auto toWinPath = [](std::string p) {
        for (char& c : p) {
            if (c == '/') c = '\\';
        }
        while (p.size() > 3 && p.back() == '\\') p.pop_back();
        return p;
    };
    std::string winApp = toWinPath(appDir);
    std::string winStage = toWinPath(stagingDir);
    std::string s = "@echo off\r\n"
                    "setlocal\r\n"
                    "set \"APP_DIR=" + winApp + "\"\r\n"
                    "set \"STAGE_DIR=" + winStage + "\"\r\n"
                    "set \"PID=" + std::to_string(pid) + "\"\r\n"
                    "if not \"%PID%\"==\"0\" (\r\n"
                    ":wait_loop\r\n"
                    "tasklist /FI \"PID eq %PID%\" 2>NUL | find /I \"%PID%\" >NUL\r\n"
                    "if not errorlevel 1 (\r\n"
                    "    timeout /t 1 /nobreak >NUL\r\n"
                    "    goto wait_loop\r\n"
                    ")\r\n"
                    ")\r\n"
                    "xcopy /E /Y /I \"%STAGE_DIR%\\*\" \"%APP_DIR%\\\" >NUL\r\n"
                    "rmdir /S /Q \"%STAGE_DIR%\"\r\n"
                    "start \"\" \"%APP_DIR%\\psbox.exe\"\r\n"
                    "del \"%~f0\"\r\n";
    return s;
}

std::string resolveLinuxAppImagePath(const std::string& explicitPathOrDir, const char* envAppImage) {
    auto hasAppImageExt = [](const std::string& p) {
        return (p.size() >= 9 && p.rfind(".AppImage") == p.size() - 9) ||
               (p.size() >= 9 && p.rfind(".appimage") == p.size() - 9);
    };
    if (envAppImage && *envAppImage) {
        return envAppImage;
    }
    if (!explicitPathOrDir.empty() && hasAppImageExt(explicitPathOrDir)) {
        return explicitPathOrDir;
    }
    if (const char* env = std::getenv("APPIMAGE"); env && *env) {
        return env;
    }
    std::string detected = platform::appImagePath();
    if (!detected.empty()) {
        return detected;
    }
    if (!explicitPathOrDir.empty() && fs::isDir(explicitPathOrDir)) {
        for (const auto& entry : fs::listDir(explicitPathOrDir)) {
            if (hasAppImageExt(entry)) {
                return explicitPathOrDir + "/" + entry;
            }
        }
    }
    return {};
}

std::string buildLinuxUpdateTempPath(const std::string& appImagePath) {
    if (appImagePath.empty()) return {};
    return appImagePath + ".new.tmp";
}

bool updatablePath(const std::string& path) {
    if (path.empty() || path[0] == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos || path.find('\0') != std::string::npos)
        return false;
    for (size_t start = 0;;) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    static const std::set<std::string> kUsers = {"account.json", "settings.json",  "library.json",
                                                 "library_cache.json", "prices.json",  "xcloud.log", "xcloud.prev.log", "autoplay.txt",
                                                 "update.journal", "store_import.done", "settings.json.bad"};
    std::string top = path.substr(0, path.find('/'));
    return !kUsers.count(path) && top != "imgcache" && top.rfind("update.", 0) != 0 &&
           top != "update_staging" && path != "update_apply.bat" && path.find(".ppm") == std::string::npos;
}

bool unzip(const std::string& zip, const std::string& prefix, std::vector<ZipEntry>& out, std::string& err) {
    out.clear();
    // The end of central directory record: within the last 64 KiB + 22.
    size_t eocd = std::string::npos;
    for (size_t back = 22; back <= std::min<size_t>(zip.size(), 65557); ++back)
        if (le32(zip, zip.size() - back) == 0x06054b50) {
            eocd = zip.size() - back;
            break;
        }
    if (eocd == std::string::npos) {
        err = "not a zip archive";
        return false;
    }
    size_t count = le16(zip, eocd + 10), at = le32(zip, eocd + 16);
    size_t total = 0;
    std::set<std::string> seen;
    for (size_t n = 0; n < count; ++n) {
        if (at + 46 > zip.size() || le32(zip, at) != 0x02014b50) {
            err = "damaged zip directory";
            return false;
        }
        uint32_t method = le16(zip, at + 10), packed = le32(zip, at + 20), size = le32(zip, at + 24);
        size_t nameLen = le16(zip, at + 28), extraLen = le16(zip, at + 30), commentLen = le16(zip, at + 32);
        size_t local = le32(zip, at + 42);
        if (at + 46 + nameLen > zip.size()) {
            err = "damaged zip directory";
            return false;
        }
        std::string name = zip.substr(at + 46, nameLen);
        at += 46 + nameLen + extraLen + commentLen;
        if (!name.empty() && name.back() == '/') continue;  // a folder
        if (name.rfind(prefix, 0) != 0 || !updatablePath(name.substr(prefix.size()))) {
            err = "unexpected file in the archive: " + name;
            return false;
        }
        if (!seen.insert(name).second) {  // twice: the second would replace the first's backup
            err = "file twice in the archive: " + name;
            return false;
        }
        if (local + 30 > zip.size() || le32(zip, local) != 0x04034b50) {
            err = "damaged zip entry " + name;
            return false;
        }
        size_t data = local + 30 + le16(zip, local + 26) + le16(zip, local + 28);
        total += size;
        if (data + packed > zip.size() || total > kMaxUnpacked) {
            err = "damaged zip entry " + name;
            return false;
        }
        ZipEntry e;
        e.path = name.substr(prefix.size());
        if (method == 0 && packed == size) {
            e.data = zip.substr(data, size);
        } else if (method == 8) {
            e.data.resize(size);
            int got = stbi_zlib_decode_noheader_buffer(e.data.data(), static_cast<int>(size), zip.data() + data,
                                                       static_cast<int>(packed));
            if (got != static_cast<int>(size)) {
                err = "could not unpack " + name;
                return false;
            }
        } else {
            err = "unsupported compression in " + name;
            return false;
        }
        out.push_back(std::move(e));
    }
    return true;
}

namespace {

bool installPs5Release(const Release& release, const std::string& dir, const std::function<void(UpdateStep, double)>& report,
                       std::string& err) {
    if (release.zipUrl.empty() || release.sigUrl.empty()) {
        err = release.tag + " has no signed package";
        return false;
    }
    report(UpdateStep::Downloading, 0);
    std::string sig, sha256Text, zip;
    if (!download(release.sigUrl, -1, sig, nullptr, err)) return false;
    if (!release.sha256Url.empty() && !download(release.sha256Url, -1, sha256Text, nullptr, err)) return false;
    if (!download(release.zipUrl, release.zipSize, zip, [&](double f) { report(UpdateStep::Downloading, f); }, err))
        return false;
    XC_LOGI("update: %s downloaded, %zu bytes", release.tag.c_str(), zip.size());

    report(UpdateStep::Verifying, -1);
    if (!release.sha256Url.empty() && !verifySha256(zip, sha256Text, err, release.assetName)) return false;
    if (!verifySignature(zip, sig, kReleaseKey, err)) return false;
    std::vector<ZipEntry> files;
    if (!unzip(zip, "PPSA99810/", files, err)) return false;
    zip.clear();
    zip.shrink_to_fit();
    // The package must be that version: an older signed one can't be
    // offered again under a new tag.
    std::string wanted = contentVersionOf(release.tag), packaged;
    bool hasEboot = false;
    for (const auto& f : files) {
        hasEboot |= f.path == "eboot.bin";
        if (f.path == "sce_sys/param.json")
            if (auto j = json::parse(f.data)) packaged = (*j)["contentVersion"].str();
    }
    if (!hasEboot || wanted.empty() || packaged != wanted) {
        err = "the package is not " + release.tag + " (contentVersion " + (packaged.empty() ? "?" : packaged) + ")";
        return false;
    }
    XC_LOGI("update: signature ok, %zu files, contentVersion %s", files.size(), packaged.c_str());

    report(UpdateStep::Installing, -1);
    recoverUpdate(dir);  // leftovers of an earlier try
    if (fs::exists(dir + "/update.journal")) {  // its old files couldn't all go back: not over them
        err = "an earlier update could not be undone";
        return false;
    }
    const std::string staged = dir + "/update.new", old = dir + "/update.old";
    for (const auto& f : files) {
        std::string p = staged + "/" + f.path;
        // As the console's FTP servers leave a package's files: without the
        // execute bit, eboot.bin doesn't start (the restart then fails).
        if (!fs::makeDirsForFile(p) || !platform::writeFileAtomic(p, f.data) || !fs::makeWorldWritable(p)) {
            err = "could not write " + p;
            fs::removeTree(staged);
            return false;
        }
    }
    // eboot.bin last: until then, a failure leaves the old app whole.
    std::stable_sort(files.begin(), files.end(),
                     [](const ZipEntry& a, const ZipEntry& b) { return a.path != "eboot.bin" && b.path == "eboot.bin"; });
    if (!platform::writeFileAtomic(dir + "/update.journal", "swapping\n")) {
        err = "could not write the update journal";
        fs::removeTree(staged);
        return false;
    }
    for (const auto& f : files) {
        std::string target = dir + "/" + f.path;
        std::string backup = old + "/" + f.path;
        bool moved = !fs::exists(target) || (fs::makeDirsForFile(backup) && fs::renameReplace(target, backup));
        if (!moved || !fs::makeDirsForFile(target) || !fs::renameReplace(staged + "/" + f.path, target)) {
            err = "could not put " + f.path + " in place";
            bool complete;
            rollBack(dir, complete);
            if (!complete) return false;  // the journal stays: the next start tries again
            fs::removeFile(dir + "/update.journal");
            fs::removeTree(staged);
            fs::removeTree(old);
            return false;
        }
    }
    fs::removeFile(dir + "/update.journal");
    fs::removeTree(staged);
    XC_LOGI("update: %s installed", release.tag.c_str());
    return true;
}

bool installPs4Release(const Release& release, const std::string& dir, const std::function<void(UpdateStep, double)>& report,
                       std::string& err) {
    if (release.zipUrl.empty() || (release.sha256Url.empty() && release.sigUrl.empty())) {
        err = release.tag + " has no verifiable PS4 package";
        return false;
    }
    std::string pkgDir = (dir.empty() || dir == "/app0") ? "/data/pkg" : dir;
    if (!fs::makeDirs(pkgDir)) {
        err = "could not create directory " + pkgDir;
        return false;
    }
    std::string filename = sanitizeAssetFilename(release.assetName, "PSBoxCloudGaming[PS4]-" + release.tag + ".pkg");
    std::string pkgPath = pkgDir + "/" + filename;
    std::string partPath = pkgPath + ".part";

    report(UpdateStep::Downloading, 0);
    std::string sha256Text, sigBytes;
    if (!release.sha256Url.empty() && !download(release.sha256Url, -1, sha256Text, nullptr, err)) return false;
    if (!release.sigUrl.empty() && !download(release.sigUrl, -1, sigBytes, nullptr, err)) return false;
    if (!downloadToFile(release.zipUrl, release.zipSize, partPath, [&](double f) { report(UpdateStep::Downloading, f); }, err)) {
        fs::removeFile(partPath);
        return false;
    }

    report(UpdateStep::Verifying, -1);
    if (!release.sha256Url.empty() && !verifyFileSha256(partPath, sha256Text, err, filename)) {
        fs::removeFile(partPath);
        fs::removeFile(pkgPath);
        return false;
    }
    if (!release.sigUrl.empty() && !verifyFileSignature(partPath, sigBytes, kReleaseKey, err)) {
        fs::removeFile(partPath);
        fs::removeFile(pkgPath);
        return false;
    }
    if (!fs::renameReplace(partPath, pkgPath)) {
        fs::removeFile(partPath);
        err = "could not move verified package to " + pkgPath;
        return false;
    }
    fs::makeWorldWritable(pkgPath);
    XC_LOGI("update ps4: %s verified at %s", release.tag.c_str(), pkgPath.c_str());

    report(UpdateStep::Installing, -1);
    net::Request req;
    req.method = "POST";
    req.url = "http://127.0.0.1:12800/api/install";
    req.headers = {{"Content-Type", "application/json"}, {"User-Agent", "PSBox-Cloud-Gaming"}};
    req.body = buildPs4GoldHenInstallPayload(pkgPath);
    req.timeoutMs = 15000;
    auto r = net::perform(req);

    bool rpiOk = r.ok();
    if (rpiOk && !r.body.empty()) {
        if (auto j = json::parse(r.body)) {
            std::string status = (*j)["status"].str();
            if (status == "fail" || status == "error") rpiOk = false;
        }
    }
    if (rpiOk) {
        XC_LOGI("update ps4: GoldHEN RPI accepted install of %s (%s)", pkgPath.c_str(), r.body.c_str());
        platform::notify("GoldHEN Package Installer: installing " + filename, "ps4 update via GoldHEN RPI");
        err.clear();
        return true;
    }

    std::string rpiReason = r.status ? ("HTTP " + std::to_string(r.status))
                                     : (r.error.empty() ? "RPC server unavailable" : r.error);
    err = "Package verified and saved to " + pkgPath + " (GoldHEN RPI 127.0.0.1:12800 unavailable: " + rpiReason +
          "; ready to install via GoldHEN Package Installer)";
    XC_LOGW("update ps4: %s", err.c_str());
    platform::notify("Update saved to " + pkgPath + " (install via GoldHEN Package Installer)", err);
    return true;
}

bool installLinuxRelease(const Release& release, const std::string& dir, const std::function<void(UpdateStep, double)>& report,
                         std::string& err) {
    if (release.zipUrl.empty() || (release.sha256Url.empty() && release.sigUrl.empty())) {
        err = release.tag + " has no verifiable Linux AppImage";
        return false;
    }
    std::string appImagePath = resolveLinuxAppImagePath(dir);
    if (appImagePath.empty()) {
        err = "not running from an AppImage (APPIMAGE not set)";
        return false;
    }
    std::string tmpPath = buildLinuxUpdateTempPath(appImagePath);

    report(UpdateStep::Downloading, 0);
    std::string sha256Text, sigBytes;
    if (!release.sha256Url.empty() && !download(release.sha256Url, -1, sha256Text, nullptr, err)) return false;
    if (!release.sigUrl.empty() && !download(release.sigUrl, -1, sigBytes, nullptr, err)) return false;
    if (!downloadToFile(release.zipUrl, release.zipSize, tmpPath, [&](double f) { report(UpdateStep::Downloading, f); }, err)) {
        fs::removeFile(tmpPath);
        return false;
    }

    report(UpdateStep::Verifying, -1);
    if (!release.sha256Url.empty() && !verifyFileSha256(tmpPath, sha256Text, err, release.assetName)) {
        fs::removeFile(tmpPath);
        return false;
    }
    if (!release.sigUrl.empty() && !verifyFileSignature(tmpPath, sigBytes, kReleaseKey, err)) {
        fs::removeFile(tmpPath);
        return false;
    }

    report(UpdateStep::Installing, -1);
    if (!platform::makeExecutable(tmpPath)) {
        fs::removeFile(tmpPath);
        err = "could not chmod +x " + tmpPath;
        return false;
    }
    if (!fs::renameReplace(tmpPath, appImagePath)) {
        fs::removeFile(tmpPath);
        err = "could not replace " + appImagePath;
        return false;
    }
    XC_LOGI("update linux: %s installed at %s", release.tag.c_str(), appImagePath.c_str());
    return true;
}

bool installWindowsRelease(const Release& release, const std::string& dir, const std::function<void(UpdateStep, double)>& report,
                           std::string& err) {
    if (release.zipUrl.empty() || (release.sha256Url.empty() && release.sigUrl.empty())) {
        err = release.tag + " has no verifiable Windows package";
        return false;
    }
    report(UpdateStep::Downloading, 0);
    std::string sha256Text, sigBytes, zip;
    if (!release.sha256Url.empty() && !download(release.sha256Url, -1, sha256Text, nullptr, err)) return false;
    if (!release.sigUrl.empty() && !download(release.sigUrl, -1, sigBytes, nullptr, err)) return false;
    if (!download(release.zipUrl, release.zipSize, zip, [&](double f) { report(UpdateStep::Downloading, f); }, err))
        return false;

    report(UpdateStep::Verifying, -1);
    if (!release.sha256Url.empty() && !verifySha256(zip, sha256Text, err, release.assetName)) return false;
    if (!release.sigUrl.empty() && !verifySignature(zip, sigBytes, kReleaseKey, err)) return false;

    std::vector<ZipEntry> files;
    if (!unzip(zip, "PSBox/", files, err)) {
        std::string fallbackErr;
        if (!unzip(zip, "", files, fallbackErr)) {
            return false;
        }
        err.clear();
    }
    zip.clear();
    zip.shrink_to_fit();

    bool hasExe = false;
    for (const auto& f : files) {
        if (f.path == "psbox.exe") {
            hasExe = true;
            break;
        }
    }
    if (!hasExe) {
        err = "the Windows package does not contain psbox.exe";
        return false;
    }

    report(UpdateStep::Installing, -1);
    const std::string staged = dir + "/update_staging";
    fs::removeTree(staged);
    for (const auto& f : files) {
        std::string p = staged + "/" + f.path;
        if (!fs::makeDirsForFile(p) || !platform::writeFileAtomic(p, f.data)) {
            err = "could not write " + p;
            fs::removeTree(staged);
            return false;
        }
    }
    std::string batPath = dir + "/update_apply.bat";
    std::string batScript = buildWindowsUpdateBatchScript(dir, staged, platform::currentProcessId());
    if (!platform::writeFileAtomic(batPath, batScript)) {
        err = "could not write " + batPath;
        fs::removeTree(staged);
        return false;
    }
    XC_LOGI("update windows: %s staged in %s (%zu files)", release.tag.c_str(), staged.c_str(), files.size());
    return true;
}

bool installAndroidRelease(const Release& release, const std::string& dir, const std::function<void(UpdateStep, double)>& report,
                           std::string& err) {
    if (release.zipUrl.empty() || (release.sha256Url.empty() && release.sigUrl.empty())) {
        err = release.tag + " has no verifiable Android APK";
        return false;
    }
    std::string baseDir = dir.empty() ? platform::appDir() : dir;
    std::string updatesDir = baseDir + "/updates";
    if (!fs::makeDirs(updatesDir)) {
        err = "could not create directory " + updatesDir;
        return false;
    }
    std::string apkPath = updatesDir + "/update.apk";
    std::string partPath = apkPath + ".part";

    report(UpdateStep::Downloading, 0);
    std::string sha256Text, sigBytes;
    if (!release.sha256Url.empty() && !download(release.sha256Url, -1, sha256Text, nullptr, err)) return false;
    if (!release.sigUrl.empty() && !download(release.sigUrl, -1, sigBytes, nullptr, err)) return false;
    if (!downloadToFile(release.zipUrl, release.zipSize, partPath, [&](double f) { report(UpdateStep::Downloading, f); }, err)) {
        fs::removeFile(partPath);
        return false;
    }

    report(UpdateStep::Verifying, -1);
    if (!release.sha256Url.empty() && !verifyFileSha256(partPath, sha256Text, err, release.assetName)) {
        fs::removeFile(partPath);
        fs::removeFile(apkPath);
        return false;
    }
    if (!release.sigUrl.empty() && !verifyFileSignature(partPath, sigBytes, kReleaseKey, err)) {
        fs::removeFile(partPath);
        fs::removeFile(apkPath);
        return false;
    }
    if (!fs::renameReplace(partPath, apkPath)) {
        fs::removeFile(partPath);
        err = "could not write " + apkPath;
        return false;
    }

    report(UpdateStep::Installing, -1);
    if (!platform::installAndroidApk(apkPath, err)) {
        return false;
    }
    XC_LOGI("update android: %s verified and ready at %s", release.tag.c_str(), apkPath.c_str());
    return true;
}

}  // namespace

bool installRelease(const Release& release, const std::string& dir, const std::string& currentVersion,
                    const UpdateProgress& progress, std::string& err) {
    auto report = [&](UpdateStep step, double f) {
        if (progress) progress(step, f);
    };
    if (!isNewerVersion(release.tag, currentVersion)) {
        err = release.tag + " is not newer than " + currentVersion;
        return false;
    }
    switch (release.platform) {
    case UpdatePlatform::PS5:
        return installPs5Release(release, dir, report, err);
    case UpdatePlatform::PS4:
        return installPs4Release(release, dir, report, err);
    case UpdatePlatform::Linux:
        return installLinuxRelease(release, dir, report, err);
    case UpdatePlatform::Windows:
        return installWindowsRelease(release, dir, report, err);
    case UpdatePlatform::Android:
        return installAndroidRelease(release, dir, report, err);
    }
    err = "unsupported update platform";
    return false;
}

bool importStoreLeftovers(const std::string& dir, const std::string& previousDir) {
    std::string text;
    const std::string done = dir + "/store_import.done";
    if (!platform::readFile(previousDir + "/account.json", text) || platform::readFile(done, text)) return false;
    // Signed in here already (the store's folder is older): never later.
    bool signedIn = platform::readFile(dir + "/account.json", text);
    platform::writeFileAtomic(done, "1\n");
    if (signedIn) return false;
    int copied = 0;
    for (const char* name : {"account.json", "settings.json", "library.json", "library_cache.json", "prices.json"})
        if (platform::readFile(previousDir + "/" + name, text) && platform::writeFileAtomic(dir + "/" + name, text))
            ++copied;
    XC_LOGI("store update: %d user file(s) brought back from the previous version's folder", copied);
    return copied > 0;
}

bool recoverUpdate(const std::string& dir) {
    bool rolledBack = false;
    if (fs::exists(dir + "/update.journal")) {
        XC_LOGW("update: the last one was cut short; the old files go back");
        bool complete;
        rolledBack = rollBack(dir, complete);
        if (!complete) {
            // What is left of the old files stays where it is, for the next try.
            XC_LOGE("update: the old files could not all go back; trying again at the next start");
            return rolledBack;
        }
        fs::removeFile(dir + "/update.journal");
    }
    fs::removeTree(dir + "/update.new");
    fs::removeTree(dir + "/update.old");
    return rolledBack;
}

}  // namespace xc::app
