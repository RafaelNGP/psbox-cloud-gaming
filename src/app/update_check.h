// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Is there a newer release of the app on GitHub?
#pragma once

#include <string>

namespace xc::app {

enum class UpdatePlatform {
    PS5,
    PS4,
    Linux,
    Windows,
    Android,
};

// Returns the target update platform for the running build.
UpdatePlatform currentUpdatePlatform();

// "v0.4.0" against "0.3.0": true when `tag` is a later version. Tags that
// aren't versions are never newer.
bool isNewerVersion(const std::string& tag, const std::string& current);

// The package's contentVersion (sce_sys/param.json) for a version:
// "0.8.1" -> "00.801.000", as tools/release.sh writes it; empty when it
// isn't one.
std::string contentVersionOf(const std::string& version);

// A release and the files the updater needs from it.
struct Release {
    std::string tag;        // "v0.9.0"
    std::string zipUrl;     // Primary package URL (.zip, .pkg, .AppImage, .apk)
    std::string sigUrl;     // ECDSA signature URL (.sig), when available
    std::string sha256Url;  // SHA-256 manifest URL (.sha256), when available
    std::string assetName;  // Filename of the selected primary package
    long zipSize = -1;
    UpdatePlatform platform = UpdatePlatform::PS5;
    std::string notes;

    bool hasVerifiablePackage() const {
        if (zipUrl.empty()) return false;
        if (platform == UpdatePlatform::PS5) return !sigUrl.empty();
        return !sha256Url.empty() || !sigUrl.empty();
    }
};

// Pure helper: parses a GitHub release JSON payload and selects the asset
// bundle (package, .sha256, .sig) for `platform`.
bool selectReleaseAsset(const std::string& releaseJson, UpdatePlatform platform, Release& out, std::string& err);

// Filters release notes delimited by <!-- LANG:PT --> / <!-- LANG:EN --> etc.
// Returns the matching section or full notes if no language tags exist.
std::string filterReleaseNotes(const std::string& notes, const std::string& langCode);

// The latest GitHub release, or the release `feedUrl` describes (the same
// JSON; the update test's local server). False with `err` when there is
// none or it can't be asked (no network, rate limit).
bool findLatestRelease(Release& out, std::string& err, const std::string& feedUrl = {},
                       UpdatePlatform platform = currentUpdatePlatform());

}  // namespace xc::app
