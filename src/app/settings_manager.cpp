// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "app/settings_manager.h"
#include "input/gyro.h"
#include "input/pad_map.h"
#include "input/key_map.h"
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace xc::app {

// Legacy global settings state definitions shared across appcore and ui
std::mutex g_settingsMutex;
Settings g_settings;
std::atomic<uint64_t> g_settingsVersion{1};

SettingsManager& SettingsManager::instance() {
    static SettingsManager s_instance;
    return s_instance;
}

SettingsManager::SettingsManager() {
    path_ = platform::dataDir() + "/settings.json";
    load();
}

std::string SettingsManager::path() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return path_;
}

void SettingsManager::adoptUser(const std::string& path, const Settings& s) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    path_ = path;
    settings_ = s;
    version_.fetch_add(1, std::memory_order_acq_rel);
}

void SettingsManager::noteSaved(const Settings& s, const std::string& path) {
    SettingsManager& m = instance();
    if (&s == &m.settings_) return;  // the manager's own save
    // Not waited for: the manager's save takes its lock and then g_settingsMutex, which the caller may hold.
    std::unique_lock<std::shared_mutex> lock(m.mutex_, std::try_to_lock);
    if (!lock.owns_lock() || path != m.path_) return;
    m.settings_ = s;
    m.version_.fetch_add(1, std::memory_order_acq_rel);
}

void SettingsManager::setPath(const std::string& path) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    path_ = path;
}

bool SettingsManager::load(const std::string& path) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!path.empty()) {
        path_ = path;
    }
    bool ok = settings_.load(path_);
    version_.fetch_add(1, std::memory_order_acq_rel);

    // Sync legacy g_settings if initialized
    {
        std::lock_guard<std::mutex> legacyLock(g_settingsMutex);
        g_settings = settings_;
        g_settingsVersion.store(version_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return ok;
}

void SettingsManager::syncFrom(const Settings& s) {
    SettingsChangeEvent ev;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        settings_ = s;
        uint64_t v = version_.fetch_add(1, std::memory_order_acq_rel) + 1;
        g_settingsVersion.store(v, std::memory_order_relaxed);
    }
    notifyListeners(ev);
}

bool SettingsManager::save() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return saveLocked();
}

bool SettingsManager::saveLocked() const {
    bool ok = settings_.save(path_);
    // Sync legacy g_settings so callers reading g_settings remain consistent
    {
        std::lock_guard<std::mutex> legacyLock(g_settingsMutex);
        g_settings = settings_;
        g_settingsVersion.store(version_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return ok;
}

Settings SettingsManager::get() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return settings_;
}

void SettingsManager::get(Settings& out) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    out = settings_;
}

GameProfile SettingsManager::getProfile(const std::string& productId, const std::string& titleId, bool* hasCustom) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return settings_.profileForGame(productId, titleId, hasCustom);
}

bool SettingsManager::hasCustomProfile(const std::string& gameKey) const {
    if (gameKey.empty()) return false;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return settings_.perGame.find(gameKey) != settings_.perGame.end();
}

void SettingsManager::subscribe(void* tag, Listener listener) {
    if (!tag || !listener) return;
    std::lock_guard<std::mutex> lock(listenersMutex_);
    listeners_[tag] = std::move(listener);
}

void SettingsManager::unsubscribe(void* tag) {
    if (!tag) return;
    std::lock_guard<std::mutex> lock(listenersMutex_);
    listeners_.erase(tag);
}

void SettingsManager::notifyListeners(const SettingsChangeEvent& ev) {
    std::vector<Listener> copy;
    {
        std::lock_guard<std::mutex> lock(listenersMutex_);
        copy.reserve(listeners_.size());
        for (const auto& [tag, fn] : listeners_) {
            if (fn) copy.push_back(fn);
        }
    }
    for (const auto& fn : copy) {
        try {
            fn(ev);
        } catch (const std::exception& e) {
            XC_LOGW("SettingsManager: listener exception: %s", e.what());
        } catch (...) {
            XC_LOGW("SettingsManager: unknown listener exception");
        }
    }
}

bool SettingsManager::updateFromJson(const json::Value& root, bool fromWebUi) {
    (void)fromWebUi;
    SettingsChangeEvent ev;
    bool changed = false;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        if (root.has("storeCountry") && root["storeCountry"].isString()) {
            const auto raw = root["storeCountry"].str();
            const auto country = normalizeStoreCountry(raw);
            if ((raw.empty() || !country.empty()) && country != settings_.storeCountry) {
                settings_.storeCountry = country;
                changed = true;
            }
        }
        if (root.has("voiceRouting")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["voiceRouting"].asInt(settings_.voiceRouting), 0, 1));
            if (v != settings_.voiceRouting) {
                settings_.voiceRouting = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("audioRoute")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["audioRoute"].asInt(settings_.audioRoute), 0, 1));
            if (v != settings_.audioRoute) {
                settings_.audioRoute = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("maxBitrateMbps")) {
            int parsed = static_cast<int>(root["maxBitrateMbps"].asInt(settings_.maxBitrateMbps));
            int v = (parsed == -1 || parsed >= 100) ? -1 : std::clamp(parsed, 0, 50);
            if (v != settings_.maxBitrateMbps) {
                settings_.maxBitrateMbps = v;
                changed = true;
                ev.affectsStream = true;
            }
        }
        if (root.has("highRefresh")) {
            bool v = root["highRefresh"].asBool(settings_.highRefresh);
            if (v != settings_.highRefresh) {
                settings_.highRefresh = v;
                changed = true;
                ev.affectsDisplay = true;
            }
        }
        if (root.has("fpsLimit")) {
            int parsed = static_cast<int>(root["fpsLimit"].asInt(settings_.fpsLimit));
            int v = (parsed == 30 || parsed == 60 || parsed == 120) ? parsed : 0;
            if (v != settings_.fpsLimit) {
                settings_.fpsLimit = v;
                changed = true;
                ev.affectsDisplay = true;
            }
        }
        if (root.has("deadzoneLeft")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["deadzoneLeft"].asInt(settings_.deadzoneLeft), 0, 50));
            if (v != settings_.deadzoneLeft) {
                settings_.deadzoneLeft = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("deadzoneRight")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["deadzoneRight"].asInt(settings_.deadzoneRight), 0, 50));
            if (v != settings_.deadzoneRight) {
                settings_.deadzoneRight = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("lightBarMode")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["lightBarMode"].asInt(settings_.lightBarMode), 0, 2));
            if (v != settings_.lightBarMode) {
                settings_.lightBarMode = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("lightBarColour") && root["lightBarColour"].isString()) {
            std::string hexStr = root["lightBarColour"].str();
            if (hexStr.size() == 7 && hexStr[0] == '#') {
                uint32_t rgb = static_cast<uint32_t>(std::strtoul(hexStr.c_str() + 1, nullptr, 16));
                uint32_t col = 0xFF000000u | (rgb >> 16 & 0xFF) | (rgb & 0xFF00) | (rgb & 0xFF) << 16;
                if (col != settings_.lightBarColour) {
                    settings_.lightBarColour = col;
                    changed = true;
                    ev.affectsHardware = true;
                }
            }
        }
        if (root.has("triggerStrength")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["triggerStrength"].asInt(settings_.triggerStrength), 0, 4));
            if (v != settings_.triggerStrength) {
                settings_.triggerStrength = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("triggerHz")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["triggerHz"].asInt(settings_.triggerHz), 0, 2));
            if (v != settings_.triggerHz) {
                settings_.triggerHz = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("triggerResistance")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["triggerResistance"].asInt(settings_.triggerResistance), 0, 3));
            if (v != settings_.triggerResistance) {
                settings_.triggerResistance = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("triggerPulses")) {
            bool v = root["triggerPulses"].asBool(settings_.triggerPulses);
            if (v != settings_.triggerPulses) {
                settings_.triggerPulses = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("triggerProfile")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["triggerProfile"].asInt(settings_.triggerProfile), 0, 2));
            if (v != settings_.triggerProfile) {
                settings_.triggerProfile = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("audioHaptics")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["audioHaptics"].asInt(settings_.audioHaptics), 0, 3));
            if (v != settings_.audioHaptics) {
                settings_.audioHaptics = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("queueReadyAlert")) {
            bool v = root["queueReadyAlert"].asBool(settings_.queueReadyAlert);
            if (v != settings_.queueReadyAlert) {
                settings_.queueReadyAlert = v;
                changed = true;
            }
        }
        if (root.has("previewFeatures")) {
            bool v = root["previewFeatures"].asBool(settings_.previewFeatures);
            if (v != settings_.previewFeatures) {
                settings_.previewFeatures = v;
                changed = true;
            }
        }
        if (root.has("usbControllers") || root.has("usb_controllers") || root.has("enable_onefamily")) {
            bool v = root.has("usbControllers") ? root["usbControllers"].asBool(settings_.usbControllers)
                   : root.has("usb_controllers") ? root["usb_controllers"].asBool(settings_.usbControllers)
                   : root["enable_onefamily"].asBool(settings_.usbControllers);
            if (v != settings_.usbControllers) {
                settings_.usbControllers = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("soloInputRedirect") || root.has("solo_input_redirect")) {
            bool v = root.has("soloInputRedirect") ? root["soloInputRedirect"].asBool(settings_.soloInputRedirect)
                                                   : root["solo_input_redirect"].asBool(settings_.soloInputRedirect);
            if (v != settings_.soloInputRedirect) {
                settings_.soloInputRedirect = v;
                changed = true;
            }
        }
        if (root.has("syncTrophies")) {
            bool v = root["syncTrophies"].asBool(settings_.syncTrophies);
            if (v != settings_.syncTrophies) {
                settings_.syncTrophies = v;
                changed = true;
            }
        }
        if (root.has("videoDecoder")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["videoDecoder"].asInt(settings_.videoDecoder), 0, 1));
            if (v != settings_.videoDecoder) {
                settings_.videoDecoder = v;
                changed = true;
                ev.affectsDisplay = true;
            }
        }
        if (root.has("padSpeakerVolume")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["padSpeakerVolume"].asInt(settings_.padSpeakerVolume), 0, 100));
            if (v != settings_.padSpeakerVolume) {
                settings_.padSpeakerVolume = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("sharpness")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["sharpness"].asInt(settings_.sharpness), 0, 3));
            if (v != settings_.sharpness) {
                settings_.sharpness = v;
                changed = true;
                ev.affectsDisplay = true;
            }
        }
        if (root.has("upscaler")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["upscaler"].asInt(settings_.upscaler), 0, 3));
            if (v != settings_.upscaler) {
                settings_.upscaler = v;
                changed = true;
                ev.affectsDisplay = true;
            }
        }
        if (root.has("deband")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["deband"].asInt(settings_.deband), 0, 3));
            if (v != settings_.deband) {
                settings_.deband = v;
                changed = true;
                ev.affectsDisplay = true;
            }
        }
        if (root.has("gyroMode")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroMode"].asInt(settings_.gyroMode), 0, 4));
            if (v != settings_.gyroMode) {
                settings_.gyroMode = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("gyroLevel")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroLevel"].asInt(settings_.gyroLevel), 1, 10));
            if (v != settings_.gyroLevel) {
                settings_.gyroLevel = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("gyroCurveStart")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroCurveStart"].asInt(settings_.gyroCurveStart), 0, 40));
            if (v != settings_.gyroCurveStart) { settings_.gyroCurveStart = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroCurveExp")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroCurveExp"].asInt(settings_.gyroCurveExp), 10, 40));
            if (v != settings_.gyroCurveExp) { settings_.gyroCurveExp = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroInvertX")) {
            bool v = root["gyroInvertX"].asBool(settings_.gyroInvertX);
            if (v != settings_.gyroInvertX) { settings_.gyroInvertX = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroInvertY")) {
            bool v = root["gyroInvertY"].asBool(settings_.gyroInvertY);
            if (v != settings_.gyroInvertY) { settings_.gyroInvertY = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroYawScale")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroYawScale"].asInt(settings_.gyroYawScale), 20, 200));
            if (v != settings_.gyroYawScale) {
                settings_.gyroYawScale = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("gyroPitchScale")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroPitchScale"].asInt(settings_.gyroPitchScale), 20, 200));
            if (v != settings_.gyroPitchScale) {
                settings_.gyroPitchScale = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("gyroBias") && root["gyroBias"].isArray() && root["gyroBias"].items().size() >= 3) {
            float b0 = static_cast<float>(root["gyroBias"][0].asNumber(settings_.gyroBias[0]));
            float b1 = static_cast<float>(root["gyroBias"][1].asNumber(settings_.gyroBias[1]));
            float b2 = static_cast<float>(root["gyroBias"][2].asNumber(settings_.gyroBias[2]));
            if (b0 != settings_.gyroBias[0] || b1 != settings_.gyroBias[1] || b2 != settings_.gyroBias[2]) {
                settings_.gyroBias[0] = b0;
                settings_.gyroBias[1] = b1;
                settings_.gyroBias[2] = b2;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("touchpadMouse")) {
            bool v = root["touchpadMouse"].asBool(settings_.touchpadMouse);
            if (v != settings_.touchpadMouse) {
                settings_.touchpadMouse = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("circleConfirms")) {
            bool v = root["circleConfirms"].asBool(settings_.circleConfirms);
            if (v != settings_.circleConfirms) {
                settings_.circleConfirms = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("gameVolume")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gameVolume"].asInt(settings_.gameVolume), 0, 200));
            if (v != settings_.gameVolume) {
                settings_.gameVolume = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("micGain")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["micGain"].asInt(settings_.micGain), 0, 2));
            if (v != settings_.micGain) {
                settings_.micGain = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("micEnabled")) {
            bool v = root["micEnabled"].asBool(settings_.micEnabled);
            if (v != settings_.micEnabled) {
                settings_.micEnabled = v;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("resolution")) {
            std::string res = root["resolution"].str(settings_.resolution);
            if (res != settings_.resolution && (res == "1080p" || res == "720p" || res == "1440p" || res == "best")) {
                settings_.resolution = res;
                changed = true;
                ev.affectsDisplay = true;
            }
        }
        if (root.has("language")) {
            std::string lang = root["language"].str(settings_.language);
            if (lang != settings_.language && !lang.empty()) {
                settings_.language = lang;
                changed = true;
                ev.affectsUi = true;
            }
        }
        if (root.has("padMap")) {
            input::PadMap newPad;
            if (input::readPadMap(root["padMap"], newPad)) {
                if (std::memcmp(&newPad, &settings_.padMap, sizeof(input::PadMap)) != 0) {
                    settings_.padMap = newPad;
                    changed = true;
                    ev.affectsHardware = true;
                }
            }
        }
        if (root.has("keyMap")) {
            input::KeyMap newKm = keyMapFromJson(root["keyMap"], settings_.keyMap);
            if (std::memcmp(&newKm, &settings_.keyMap, sizeof(input::KeyMap)) != 0) {
                settings_.keyMap = newKm;
                changed = true;
                ev.affectsHardware = true;
            }
        }
        if (root.has("touchpadShortcuts")) {
            input::TouchpadShortcuts newTs;
            if (input::readTouchpadShortcuts(root["touchpadShortcuts"], newTs)) {
                if (std::memcmp(&newTs, &settings_.touchpadShortcuts, sizeof(input::TouchpadShortcuts)) != 0) {
                    settings_.touchpadShortcuts = newTs;
                    for (auto& [k, prof] : settings_.perGame) {
                        prof.touchpadShortcuts = newTs;
                    }
                    changed = true;
                    ev.affectsHardware = true;
                }
            }
        }

        if (changed) {
            version_.fetch_add(1, std::memory_order_acq_rel);
            saveLocked();
        }
    }

    if (changed) {
        notifyListeners(ev);
    }
    return changed;
}

bool SettingsManager::updateGameProfileFromJson(const std::string& gameKey, const json::Value& root) {
    if (gameKey.empty()) return false;
    SettingsChangeEvent ev;
    ev.gameKey = gameKey;
    bool changed = false;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        GameProfile& prof = settings_.perGame[gameKey];

        if (root.has("sharpness")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["sharpness"].asInt(prof.sharpness), 0, 3));
            if (v != prof.sharpness) { prof.sharpness = v; changed = true; ev.affectsDisplay = true; }
        }
        if (root.has("upscaler")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["upscaler"].asInt(prof.upscaler), 0, 3));
            if (v != prof.upscaler) { prof.upscaler = v; changed = true; ev.affectsDisplay = true; }
        }
        if (root.has("deband")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["deband"].asInt(prof.deband), 0, 3));
            if (v != prof.deband) { prof.deband = v; changed = true; ev.affectsDisplay = true; }
        }
        if (root.has("videoDecoder")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["videoDecoder"].asInt(prof.videoDecoder), 0, 1));
            if (v != prof.videoDecoder) { prof.videoDecoder = v; changed = true; ev.affectsDisplay = true; }
        }
        if (root.has("fpsLimit")) {
            int parsed = static_cast<int>(root["fpsLimit"].asInt(prof.fpsLimit));
            int v = (parsed == 30 || parsed == 60 || parsed == 120) ? parsed : 0;
            if (v != prof.fpsLimit) { prof.fpsLimit = v; changed = true; ev.affectsDisplay = true; }
        }
        if (root.has("resolution")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["resolution"].asInt(prof.resolution), 0, 2));
            if (v != prof.resolution) { prof.resolution = v; changed = true; ev.affectsDisplay = true; }
        }
        if (root.has("deadzoneLeft")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["deadzoneLeft"].asInt(prof.deadzoneLeft), 0, 50));
            if (v != prof.deadzoneLeft) { prof.deadzoneLeft = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("deadzoneRight")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["deadzoneRight"].asInt(prof.deadzoneRight), 0, 50));
            if (v != prof.deadzoneRight) { prof.deadzoneRight = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("triggerStrength")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["triggerStrength"].asInt(prof.triggerStrength), 0, 4));
            if (v != prof.triggerStrength) { prof.triggerStrength = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("triggerHz")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["triggerHz"].asInt(prof.triggerHz), 0, 2));
            if (v != prof.triggerHz) { prof.triggerHz = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("triggerResistance")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["triggerResistance"].asInt(prof.triggerResistance), 0, 3));
            if (v != prof.triggerResistance) { prof.triggerResistance = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("triggerPulses")) {
            bool v = root["triggerPulses"].asBool(prof.triggerPulses);
            if (v != prof.triggerPulses) { prof.triggerPulses = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("triggerProfile")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["triggerProfile"].asInt(prof.triggerProfile), 0, 2));
            if (v != prof.triggerProfile) { prof.triggerProfile = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("audioHaptics")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["audioHaptics"].asInt(prof.audioHaptics), 0, 3));
            if (v != prof.audioHaptics) { prof.audioHaptics = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("circleConfirms")) {
            bool v = root["circleConfirms"].asBool(prof.circleConfirms);
            if (v != prof.circleConfirms) { prof.circleConfirms = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("highRefresh")) {
            bool v = root["highRefresh"].asBool(prof.highRefresh);
            if (v != prof.highRefresh) { prof.highRefresh = v; changed = true; ev.affectsDisplay = true; }
        }
        if (root.has("gyroMode")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroMode"].asInt(prof.gyroMode), 0, 4));
            if (v != prof.gyroMode) { prof.gyroMode = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroLevel")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroLevel"].asInt(prof.gyroLevel), 1, 10));
            if (v != prof.gyroLevel) { prof.gyroLevel = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroCurveStart")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroCurveStart"].asInt(prof.gyroCurveStart), 0, 40));
            if (v != prof.gyroCurveStart) { prof.gyroCurveStart = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroCurveExp")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroCurveExp"].asInt(prof.gyroCurveExp), 10, 40));
            if (v != prof.gyroCurveExp) { prof.gyroCurveExp = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroInvertX")) {
            bool v = root["gyroInvertX"].asBool(prof.gyroInvertX);
            if (v != prof.gyroInvertX) { prof.gyroInvertX = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroInvertY")) {
            bool v = root["gyroInvertY"].asBool(prof.gyroInvertY);
            if (v != prof.gyroInvertY) { prof.gyroInvertY = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroYawScale")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroYawScale"].asInt(prof.gyroYawScale), 20, 200));
            if (v != prof.gyroYawScale) { prof.gyroYawScale = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroPitchScale")) {
            int v = static_cast<int>(std::clamp<int64_t>(root["gyroPitchScale"].asInt(prof.gyroPitchScale), 20, 200));
            if (v != prof.gyroPitchScale) { prof.gyroPitchScale = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("gyroBias") && root["gyroBias"].isArray() && root["gyroBias"].items().size() >= 3) {
            float b0 = static_cast<float>(root["gyroBias"][0].asNumber(prof.gyroBias[0]));
            float b1 = static_cast<float>(root["gyroBias"][1].asNumber(prof.gyroBias[1]));
            float b2 = static_cast<float>(root["gyroBias"][2].asNumber(prof.gyroBias[2]));
            if (b0 != prof.gyroBias[0] || b1 != prof.gyroBias[1] || b2 != prof.gyroBias[2]) {
                prof.gyroBias[0] = b0; prof.gyroBias[1] = b1; prof.gyroBias[2] = b2;
                changed = true; ev.affectsHardware = true;
            }
        }
        if (root.has("touchpadMouse")) {
            bool v = root["touchpadMouse"].asBool(prof.touchpadMouse);
            if (v != prof.touchpadMouse) { prof.touchpadMouse = v; changed = true; ev.affectsHardware = true; }
        }
        if (root.has("touchpadShortcuts")) {
            input::TouchpadShortcuts newTs;
            if (input::readTouchpadShortcuts(root["touchpadShortcuts"], newTs)) {
                if (std::memcmp(&newTs, &prof.touchpadShortcuts, sizeof(input::TouchpadShortcuts)) != 0) {
                    prof.touchpadShortcuts = newTs;
                    changed = true;
                    ev.affectsHardware = true;
                }
            }
        }
        if (root.has("syncTrophies")) {
            bool v = root["syncTrophies"].asBool(prof.syncTrophies);
            if (v != prof.syncTrophies || !prof.syncTrophiesChosen) { prof.syncTrophies = v; prof.syncTrophiesChosen = true; changed = true; }  // chosen for this game
        }
        if (root.has("soloInputRedirect") || root.has("solo_input_redirect")) {
            bool v = root.has("soloInputRedirect") ? root["soloInputRedirect"].asBool(prof.soloInputRedirect)
                                                   : root["solo_input_redirect"].asBool(prof.soloInputRedirect);
            if (v != prof.soloInputRedirect) { prof.soloInputRedirect = v; changed = true; }
        }

        if (changed) {
            version_.fetch_add(1, std::memory_order_acq_rel);
            saveLocked();
        }
    }

    if (changed) {
        notifyListeners(ev);
    }
    return changed;
}

}  // namespace xc::app
