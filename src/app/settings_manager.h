// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Centralized thread-safe settings state manager (Single Source of Truth).
#pragma once

#include "app/settings.h"
#include "util/json.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace xc::app {

struct SettingsChangeEvent {
    std::string key;        // Name of key modified, or empty for batch update
    std::string gameKey;    // Empty for global settings, or gameId for per-game profile
    bool affectsHardware = false; // Audio, rumble, triggers, gyro, USB pads
    bool affectsDisplay = false;  // Video decoder, deband, sharpness, highRefresh, fpsLimit
    bool affectsUi = false;       // Language, sort, layout
    bool affectsStream = false;   // Bitrate, resolution, network
};

class SettingsManager {
public:
    static SettingsManager& instance();

    // Lifecycle and persistence
    bool load(const std::string& path = "");
    bool save();

    // Thread-safe reads (Shared Lock)
    Settings get() const;
    void get(Settings& out) const;
    GameProfile getProfile(const std::string& productId, const std::string& titleId, bool* hasCustom = nullptr) const;
    bool hasCustomProfile(const std::string& gameKey) const;

    uint64_t version() const { return version_.load(std::memory_order_acquire); }
    void setVersion(uint64_t v) { version_.store(v, std::memory_order_release); }

    std::string path() const;
    void setPath(const std::string& path);

    // Thread-safe writes with mutation functor (Unique Lock)
    template <typename Fn>
    bool update(Fn&& modifier, const std::string& reason = "") {
        SettingsChangeEvent ev;
        ev.key = reason;
        bool saved = false;
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            modifier(settings_);
            version_.fetch_add(1, std::memory_order_acq_rel);
            saved = saveLocked();
        }
        notifyListeners(ev);
        return saved;
    }

    // JSON update with strict delta checking (oldVal != newVal)
    // Returns true only if at least one value actually changed.
    bool updateFromJson(const json::Value& root, bool fromWebUi = false);
    bool updateGameProfileFromJson(const std::string& gameKey, const json::Value& root);

    // Observer Pattern (reactive listeners)
    using Listener = std::function<void(const SettingsChangeEvent&)>;
    void subscribe(void* tag, Listener listener);
    void unsubscribe(void* tag);

    // Helper locks for direct read/write inspection
    template <typename Fn>
    auto withLock(Fn&& fn) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return fn(settings_);
    }

    template <typename Fn>
    auto withWriteLock(Fn&& fn) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto res = fn(settings_);
        version_.fetch_add(1, std::memory_order_acq_rel);
        saveLocked();
        return res;
    }

private:
    SettingsManager();
    ~SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    void notifyListeners(const SettingsChangeEvent& ev);
    bool saveLocked() const;

    mutable std::shared_mutex mutex_;
    Settings settings_;
    std::atomic<uint64_t> version_{1};
    std::string path_;

    mutable std::mutex listenersMutex_;
    std::unordered_map<void*, Listener> listeners_;
};

}  // namespace xc::app
