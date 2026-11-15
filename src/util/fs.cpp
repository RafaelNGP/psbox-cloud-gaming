// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "util/fs.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace xc::fs {

namespace {

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], len);
    return out;
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &out[0], len, nullptr, nullptr);
    return out;
}

void listTreeInternal(const std::string& root, const std::string& rel, std::vector<std::string>& out) {
    std::string fullDir = rel.empty() ? root : root + "/" + rel;
    for (const auto& name : listDir(fullDir)) {
        std::string r = rel.empty() ? name : rel + "/" + name;
        std::string fullPath = root + "/" + r;
        if (isDir(fullPath)) {
            listTreeInternal(root, r, out);
        } else {
            out.push_back(r);
        }
    }
}

}  // namespace

bool exists(const std::string& path) {
    std::wstring wpath = toWide(path);
    DWORD attr = GetFileAttributesW(wpath.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool isDir(const std::string& path) {
    std::wstring wpath = toWide(path);
    DWORD attr = GetFileAttributesW(wpath.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool isFile(const std::string& path) {
    std::wstring wpath = toWide(path);
    DWORD attr = GetFileAttributesW(wpath.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool fileSize(const std::string& path, uint64_t& outBytes) {
    std::wstring wpath = toWide(path);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    outBytes = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

bool modifiedTime(const std::string& path, int64_t& outUnixSeconds) {
    std::wstring wpath = toWide(path);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &data)) return false;
    ULARGE_INTEGER ull;
    ull.LowPart = data.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = data.ftLastWriteTime.dwHighDateTime;
    constexpr uint64_t kEpochDiff = 116444736000000000ULL;  // 100-ns intervals between 1601 and 1970
    if (ull.QuadPart < kEpochDiff) return false;
    outUnixSeconds = static_cast<int64_t>((ull.QuadPart - kEpochDiff) / 10000000ULL);
    return true;
}

std::string parentPath(const std::string& path) {
    size_t idx = path.find_last_of("/\\");
    if (idx == std::string::npos) return "";
    return path.substr(0, idx);
}

bool makeDirs(const std::string& path) {
    if (path.empty()) return true;
    if (isDir(path)) return true;
    std::string norm = path;
    for (char& c : norm) {
        if (c == '/') c = '\\';
    }
    for (size_t i = 1; i <= norm.size(); ++i) {
        if (i == norm.size() || norm[i] == '\\') {
            std::string sub = norm.substr(0, i);
            if (sub.size() >= 2 && sub[1] == ':' && sub.size() <= 3) continue;  // Drive root (e.g. C: or C:\)
            std::wstring wsub = toWide(sub);
            if (!CreateDirectoryW(wsub.c_str(), nullptr)) {
                DWORD err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS && !isDir(sub)) return false;
            }
        }
    }
    return isDir(path);
}

bool makeDirsForFile(const std::string& filePath) {
    std::string p = parentPath(filePath);
    return p.empty() ? true : makeDirs(p);
}

bool removeFile(const std::string& path) {
    std::wstring wpath = toWide(path);
    if (DeleteFileW(wpath.c_str())) return true;
    DWORD err = GetLastError();
    return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND);
}

bool removeTree(const std::string& path) {
    if (!exists(path)) return true;
    if (!isDir(path)) return removeFile(path);
    std::wstring pattern = toWide(path);
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') {
        pattern += L"\\*";
    } else {
        pattern += L"*";
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::string childPath = path + "/" + toUtf8(fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeTree(childPath);
            } else {
                removeFile(childPath);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::wstring wpath = toWide(path);
    return RemoveDirectoryW(wpath.c_str()) != 0 || GetLastError() == ERROR_PATH_NOT_FOUND ||
           GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool renameReplace(const std::string& from, const std::string& to) {
    std::wstring wFrom = toWide(from);
    std::wstring wTo = toWide(to);
    return MoveFileExW(wFrom.c_str(), wTo.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> out;
    std::wstring pattern = toWide(path);
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') {
        pattern += L"\\*";
    } else {
        pattern += L"*";
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        out.push_back(toUtf8(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

std::vector<std::string> listTree(const std::string& root) {
    std::vector<std::string> out;
    listTreeInternal(root, "", out);
    return out;
}

bool makeWorldWritable(const std::string&) {
    return true;  // Permissions are not POSIX mode bits on Windows
}

bool readFile(const std::string& path, std::string& out) {
    std::wstring wpath = toWide(path);
    FILE* f = _wfopen(wpath.c_str(), L"rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool writeFile(const std::string& path, const std::string& data) {
    std::wstring wpath = toWide(path);
    FILE* f = _wfopen(wpath.c_str(), L"wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fflush(f) == 0) && ok;
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    if (!writeFile(tmp, data)) {
        removeFile(tmp);
        return false;
    }
    if (!renameReplace(tmp, path)) {
        removeFile(tmp);
        return false;
    }
    return true;
}

}  // namespace xc::fs

#else
// --- POSIX Implementation (PS5, PS4, Linux, Android) ------------------------
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace xc::fs {

namespace {

void listTreeInternal(const std::string& root, const std::string& rel, std::vector<std::string>& out) {
    std::string fullDir = rel.empty() ? root : root + "/" + rel;
    for (const auto& name : listDir(fullDir)) {
        std::string r = rel.empty() ? name : rel + "/" + name;
        std::string fullPath = root + "/" + r;
        if (isDir(fullPath)) {
            listTreeInternal(root, r, out);
        } else {
            out.push_back(r);
        }
    }
}

}  // namespace

bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

bool isDir(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool fileSize(const std::string& path, uint64_t& outBytes) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    outBytes = static_cast<uint64_t>(st.st_size);
    return true;
}

bool modifiedTime(const std::string& path, int64_t& outUnixSeconds) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    outUnixSeconds = static_cast<int64_t>(st.st_mtime);
    return true;
}

std::string parentPath(const std::string& path) {
    size_t idx = path.rfind('/');
    if (idx == std::string::npos) return "";
    return path.substr(0, idx);
}

bool makeDirs(const std::string& path) {
    if (path.empty()) return true;
    if (isDir(path)) return true;
    for (size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1)) {
        std::string d = path.substr(0, at);
        if (!isDir(d) && ::mkdir(d.c_str(), 0777) != 0 && errno != EEXIST) return false;
    }
    if (!isDir(path) && ::mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) return false;
    return isDir(path);
}

bool makeDirsForFile(const std::string& filePath) {
    std::string p = parentPath(filePath);
    return p.empty() ? true : makeDirs(p);
}

bool removeFile(const std::string& path) {
    if (::unlink(path.c_str()) == 0) return true;
    return errno == ENOENT;
}

bool removeTree(const std::string& path) {
    if (!exists(path)) return true;
    if (!isDir(path)) return removeFile(path);
    bool ok = true;
    if (DIR* d = ::opendir(path.c_str())) {
        while (dirent* e = ::readdir(d)) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            std::string p = path + "/" + name;
            struct stat st{};
            if (::lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                if (!removeTree(p)) ok = false;
            } else {
                if (::unlink(p.c_str()) != 0 && errno != ENOENT) ok = false;
            }
        }
        ::closedir(d);
        if (::rmdir(path.c_str()) != 0 && errno != ENOENT) ok = false;
    } else {
        ok = false;
    }
    return ok;
}

bool renameReplace(const std::string& from, const std::string& to) {
    return ::rename(from.c_str(), to.c_str()) == 0;
}

std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> out;
    DIR* d = ::opendir(path.c_str());
    if (!d) return out;
    while (dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        out.push_back(std::move(name));
    }
    ::closedir(d);
    return out;
}

std::vector<std::string> listTree(const std::string& root) {
    std::vector<std::string> out;
    listTreeInternal(root, "", out);
    return out;
}

bool makeWorldWritable(const std::string& path) {
    return ::chmod(path.c_str(), 0777) == 0;
}

bool readFile(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool writeFile(const std::string& path, const std::string& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fflush(f) == 0) && ok;
    int fd = ::fileno(f);
    if (fd >= 0) {
        ok = (::fsync(fd) == 0) && ok;
    }
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    if (!writeFile(tmp, data)) {
        removeFile(tmp);
        return false;
    }
    if (!renameReplace(tmp, path)) {
        removeFile(tmp);
        return false;
    }
    return true;
}

}  // namespace xc::fs

#endif
