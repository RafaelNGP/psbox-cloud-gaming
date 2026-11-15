// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xc::fs {

// --- Inspection -------------------------------------------------------------
// Returns true if the entry exists on disk (file, directory, symlink, etc.).
bool exists(const std::string& path);

// Returns true if path exists and is a directory.
bool isDir(const std::string& path);

// Returns true if path exists and is a regular file.
bool isFile(const std::string& path);

// Retrieves the size of the file in bytes. Returns false if path does not exist or is a directory.
bool fileSize(const std::string& path, uint64_t& outBytes);

// Retrieves the last modification timestamp in seconds since Unix epoch (1970-01-01T00:00:00Z).
bool modifiedTime(const std::string& path, int64_t& outUnixSeconds);

// Extracts the parent directory path (e.g. "a/b/c.txt" -> "a/b", "/data/app" -> "/data").
// Returns empty string if no directory separator exists.
std::string parentPath(const std::string& path);

// --- Directory & File Manipulation ------------------------------------------
// Creates directories recursively (mkdir -p). Returns true if path is a directory upon return.
bool makeDirs(const std::string& path);

// Ensures the parent directory of filePath exists. Equivalent to makeDirs(parentPath(filePath)).
bool makeDirsForFile(const std::string& filePath);

// Deletes a single file. Returns true if the file does not exist afterwards (idempotent).
bool removeFile(const std::string& path);

// Recursively deletes a directory tree and all its contents (rm -rf).
// Never follows symlinks. Returns true if the target does not exist afterwards (idempotent).
bool removeTree(const std::string& path);

// Atomically renames/replaces 'from' to 'to'. If 'to' already exists, it is overwritten atomically.
bool renameReplace(const std::string& from, const std::string& to);

// --- Directory Traversal ----------------------------------------------------
// Returns filenames of immediate children in directory 'path' (filtering out "." and "..").
// Does not recurse. Filenames are returned without path prefixes.
std::vector<std::string> listDir(const std::string& path);

// Recursively lists all regular files under 'root', returning paths relative to 'root'.
std::vector<std::string> listTree(const std::string& root);

// --- Platform Permissions ---------------------------------------------------
// Sets file/directory mode to 0777 on POSIX platforms (required for PS5 FTP accessibility).
// Safely operates as a no-op returning true on Windows.
bool makeWorldWritable(const std::string& path);

// --- Direct File I/O Helpers ------------------------------------------------
// Reads the full binary/text content of a file. Supports Unicode/UTF-8 paths on all platforms.
bool readFile(const std::string& path, std::string& out);
inline bool readFileString(const std::string& path, std::string& out) { return readFile(path, out); }

// Writes data to a file. Overwrites existing content.
bool writeFile(const std::string& path, const std::string& data);

// Atomically writes data to a temporary file in the same directory, flushes, and renames over path.
bool writeFileAtomic(const std::string& path, const std::string& data);

}  // namespace xc::fs
