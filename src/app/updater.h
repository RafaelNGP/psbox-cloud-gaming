// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Updates the app in place from a release (update_check.h).
//
// On PS5, the release's PPSA99810.zip must carry a signature (PPSA99810.zip.sig,
// ECDSA P-256 over SHA-256) by the project's own key, which tools/release.sh
// makes and only the maintainer holds: kReleaseKey here is its public half.
// On PS4, Linux, Windows and Android, the platform package is verified against
// its .sha256 manifest (and ECDSA .sig when present) before applying.
// Nothing is changed without verification, nor for a version that isn't newer.
//
// The user's files (sign-in, settings, caches, log) are never touched.
#pragma once

#include "app/update_check.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xc::app {

// The project's release signing key (public).
extern const char* const kReleaseKey;

enum class UpdateStep { Downloading, Verifying, Installing };
// `fraction` 0..1 within the step (downloading), -1 when unknown.
using UpdateProgress = std::function<void(UpdateStep step, double fraction)>;

// Downloads `release`, checks it and puts it in place in `dir` (the app's
// folder). `currentVersion` as XC_APP_VERSION. False with `err` (in English,
// for the log) when nothing was changed.
bool installRelease(const Release& release, const std::string& dir, const std::string& currentVersion,
                    const UpdateProgress& progress, std::string& err);

// At start: puts back the old files of an update cut short (true when it
// did), and removes what updates left behind.
bool recoverUpdate(const std::string& dir);

// After an update by ProsperoStore (the homebrew store): it replaces the
// whole app folder and keeps the old one in `previousDir`
// (/data/prosperostore/previous/PPSA99810), so the sign-in and settings
// stayed there. When `dir` has no account.json, the user's files come back
// from there (copied; the store keeps its folder). Once only: `dir`'s
// store_import.done then says so, and a sign-out later doesn't bring the
// old account back. True when some did.
bool importStoreLeftovers(const std::string& dir, const std::string& previousDir);

// --- The parts, for the tests ---------------------------------------------------

// True when `sigDer` is a valid signature of `data` by `publicKeyPem`.
bool verifySignature(const std::string& data, const std::string& sigDer, const char* publicKeyPem, std::string& err);

// Lowercase 64-character hex SHA-256 digest of `data`.
std::string sha256Hex(const std::string& data);

// Extracts the 64-character lowercase hex SHA-256 hash from a .sha256 manifest
// (GNU/BSD sha256sum output or raw hex). Empty when malformed.
std::string parseSha256Manifest(const std::string& manifestText, const std::string& expectedFilename = {});

// True when `data`'s SHA-256 matches the expected digest in `manifestOrHex`.
bool verifySha256(const std::string& data, const std::string& manifestOrHex, std::string& err,
                  const std::string& expectedFilename = {});

// True when the file at `filePath` exists and its SHA-256 matches `manifestOrHex`.
bool verifyFileSha256(const std::string& filePath, const std::string& manifestOrHex, std::string& err,
                      const std::string& expectedFilename = {});

// Builds the JSON payload for PS4 GoldHEN Remote Package Installer (http://127.0.0.1:12800/api/install).
std::string buildPs4GoldHenInstallPayload(const std::string& pkgPath);

// Builds the detached Windows batch script that waits for `pid` to exit, copies
// files from `stagingDir` into `appDir`, cleans up `stagingDir`, and relaunches psbox.exe.
std::string buildWindowsUpdateBatchScript(const std::string& appDir, const std::string& stagingDir, uint32_t pid = 0);

// Resolves the target AppImage path on Linux ($APPIMAGE, explicit path, or /proc/self/exe).
std::string resolveLinuxAppImagePath(const std::string& explicitPathOrDir = {}, const char* envAppImage = nullptr);

// Temporary path in the same filesystem/directory for atomic AppImage replacement.
std::string buildLinuxUpdateTempPath(const std::string& appImagePath);

struct ZipEntry {
    std::string path;  // inside the archive, without `prefix`
    std::string data;
};
// The files of a zip archive (stored or deflated) under `prefix` ("PPSA99810/").
// False on a damaged archive, an entry outside `prefix` or an unsafe path.
bool unzip(const std::string& zip, const std::string& prefix, std::vector<ZipEntry>& out, std::string& err);

// A path an update may write: relative, no "..", and not one of the user's
// files or the updater's own.
bool updatablePath(const std::string& path);

}  // namespace xc::app
