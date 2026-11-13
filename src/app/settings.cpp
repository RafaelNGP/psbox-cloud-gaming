// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include <cstdint>
#include "app/settings.h"
#include "app/settings_manager.h"
#include "input/gyro.h"

#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdlib>

namespace xc::app {

std::string normalizeStoreCountry(const std::string& country) {
    std::string code = country;
    for (char& c : code) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* supported : {"br", "jp", "kr", "pl", "us"})
        if (code == supported) return code;
    return {};
}

std::string Settings::storeMarket(const std::string& sessionMarket) const {
    std::string market = normalizeStoreCountry(storeCountry);
    if (market.empty()) market = sessionMarket.empty() ? "US" : sessionMarket;
    for (char& c : market) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return market;
}

json::Value keyMapToJson(const input::KeyMap& m) {
    json::Value v = json::Value::array();
    for (const auto& keys : m)
        for (uint8_t k : keys) v.push(static_cast<int>(k));
    return v;
}

input::KeyMap keyMapFromJson(const json::Value& v, const input::KeyMap& fallback) {
    const auto& items = v.items();
    if (items.size() != static_cast<size_t>(input::kKeyActions) * 2) return fallback;
    input::KeyMap m{};
    for (size_t i = 0; i < items.size(); ++i)
        m[i / 2][i % 2] = static_cast<uint8_t>(std::clamp<int64_t>(items[i].asInt(0), 0, 255));
    return m;
}

namespace {

// "padMap": absent keeps `fallback`; present but not a complete map of
// this version (an edited or damaged file) gives the default map.
input::PadMap readPadMapOr(const json::Value& v, const input::PadMap& fallback, const char* where) {
    if (v.isNull()) return fallback;
    input::PadMap m;
    if (!input::readPadMap(v, m)) XC_LOGW("settings: controller buttons of %s not valid; back to the default", where);
    return m;
}

input::TouchpadShortcuts readTouchpadShortcutsOr(const json::Value& v, const input::TouchpadShortcuts& fallback) {
    if (v.isNull()) return fallback;
    input::TouchpadShortcuts ts = fallback;
    input::readTouchpadShortcuts(v, ts);
    return ts;
}

}  // namespace

bool Settings::load(const std::string& path) {
    std::string text;
    if (!platform::readFile(path, text)) return false;
    auto j = json::parse(text);
    if (!j) return false;
    language = (*j)["language"].str(language);
    resolution = (*j)["resolution"].str(resolution);
    region = (*j)["region"].str(region);
    loginCountry = (*j)["loginCountry"].str(loginCountry);
    storeCountry = normalizeStoreCountry((*j)["storeCountry"].str(storeCountry));
    if (resolution != "720p" && resolution != "1440p" && resolution != "best") resolution = "1080p";
    hidden.clear();
    for (const auto& id : (*j)["hidden"].items())
        if (!id.str().empty()) hidden.push_back(id.str());
    favorites.clear();
    for (const auto& id : (*j)["favorites"].items())
        if (!id.str().empty()) favorites.push_back(id.str());
    wishlist.clear();
    for (const auto& id : (*j)["wishlist"].items())
        if (!id.str().empty()) wishlist.push_back(id.str());
    librarySort = (*j)["librarySort"].str(librarySort);
    streamStats = (*j)["streamStats"].asBool(streamStats);
    statsMask = static_cast<uint32_t>((*j)["statsMask"].asInt(static_cast<int64_t>(statsMask)));
    sharpness = static_cast<int>(std::clamp<int64_t>((*j)["sharpness"].asInt(sharpness), 0, 3));
    deband = static_cast<int>(std::clamp<int64_t>((*j)["deband"].asInt(deband), 0, 3));
    upscaler = static_cast<int>(std::clamp<int64_t>((*j)["upscaler"].asInt(upscaler), 0, 3));
    videoDecoder = static_cast<int>(std::clamp<int64_t>((*j)["videoDecoder"].asInt(videoDecoder), 0, 1));
    int parsedFps = static_cast<int>((*j)["fpsLimit"].asInt(fpsLimit));
    fpsLimit = (parsedFps == 30 || parsedFps == 60 || parsedFps == 120) ? parsedFps : 0;
    // "deadzone": one for both sticks, until each could have its own.
    int both = static_cast<int>(std::clamp<int64_t>((*j)["deadzone"].asInt(15), 0, 50));
    deadzoneLeft = static_cast<int>(std::clamp<int64_t>((*j)["deadzoneLeft"].asInt(both), 0, 50));
    deadzoneRight = static_cast<int>(std::clamp<int64_t>((*j)["deadzoneRight"].asInt(both), 0, 50));
    // "triggerRumble" (on/off) until the strength could be chosen.
    triggerStrength = (*j)["triggerRumble"].asBool(true) ? 2 : 0;
    triggerStrength = static_cast<int>(std::clamp<int64_t>((*j)["triggerStrength"].asInt(triggerStrength), 0, 4));
    triggerHz = static_cast<int>(std::clamp<int64_t>((*j)["triggerHz"].asInt(triggerHz), 0, 2));
    triggerResistance = static_cast<int>(std::clamp<int64_t>((*j)["triggerResistance"].asInt(0), 0, 3));
    triggerPulses = (*j)["triggerPulses"].asBool(false);
    triggerProfile = static_cast<int>(std::clamp<int64_t>((*j)["triggerProfile"].asInt(0), 0, 2));
    audioHaptics = static_cast<int>(std::clamp<int64_t>((*j)["audioHaptics"].asInt(audioHaptics), 0, 3));
    circleConfirms = (*j)["circleConfirms"].asBool(circleConfirms);
    // "lightBar" (on/off) until the colour could be chosen.
    lightBarMode = (*j)["lightBar"].asBool(true) ? 0 : 2;
    lightBarMode = static_cast<int>(std::clamp<int64_t>((*j)["lightBarMode"].asInt(lightBarMode), 0, 2));
    if (std::string hex = (*j)["lightBarColour"].str(); hex.size() == 7 && hex[0] == '#') {
        uint32_t rgb = static_cast<uint32_t>(std::strtoul(hex.c_str() + 1, nullptr, 16));
        lightBarColour = 0xFF000000u | (rgb >> 16 & 0xFF) | (rgb & 0xFF00) | (rgb & 0xFF) << 16;
    }
    gestureHints = static_cast<int>((*j)["gestureHints"].asInt(gestureHints));
    micEnabled = (*j)["micEnabled"].asBool(micEnabled);
    highRefresh = (*j)["highRefresh"].asBool(highRefresh);
    desktopFullscreen = (*j)["desktopFullscreen"].asBool(desktopFullscreen);
    handheldProfile = (*j)["handheldProfile"].asBool(handheldProfile);
    int parsedBitrate = static_cast<int>((*j)["maxBitrateMbps"].asInt(maxBitrateMbps));
    if (parsedBitrate == -1 || parsedBitrate >= 100) maxBitrateMbps = -1;
    else maxBitrateMbps = std::clamp(parsedBitrate, 0, 50);
    gameVolume = static_cast<int>(std::clamp<int64_t>((*j)["gameVolume"].asInt(gameVolume), 0, 200));
    keyMap = keyMapFromJson((*j)["keyMap"], input::defaultKeyMap());
    padMap = readPadMapOr((*j)["padMap"], input::defaultPadMap(), "the settings");
    keyboardPlayer = static_cast<int>(std::clamp<int64_t>((*j)["keyboardPlayer"].asInt(keyboardPlayer), 0, 3));
    gyroMode = static_cast<int>(std::clamp<int64_t>((*j)["gyroMode"].asInt(gyroMode), 0, 4));
    gyroLevel = static_cast<int>(std::clamp<int64_t>((*j)["gyroLevel"].asInt(gyroLevel), 1, 10));
    gyroYawScale = static_cast<int>(std::clamp<int64_t>((*j)["gyroYawScale"].asInt(gyroYawScale), 20, 200));
    gyroPitchScale = static_cast<int>(std::clamp<int64_t>((*j)["gyroPitchScale"].asInt(gyroPitchScale), 20, 200));
    gyroCurveStart = static_cast<int>(std::clamp<int64_t>((*j)["gyroCurveStart"].asInt(gyroCurveStart), 0, 40));
    gyroCurveExp = static_cast<int>(std::clamp<int64_t>((*j)["gyroCurveExp"].asInt(gyroCurveExp), 10, 40));
    gyroInvertX = (*j)["gyroInvertX"].asBool(gyroInvertX);
    gyroInvertY = (*j)["gyroInvertY"].asBool(gyroInvertY);
    if ((*j)["gyroBias"].isArray() && (*j)["gyroBias"].items().size() >= 3) {
        gyroBias[0] = static_cast<float>((*j)["gyroBias"][0].asNumber(gyroBias[0]));
        gyroBias[1] = static_cast<float>((*j)["gyroBias"][1].asNumber(gyroBias[1]));
        gyroBias[2] = static_cast<float>((*j)["gyroBias"][2].asNumber(gyroBias[2]));
    }
    input::setGlobalGyroCalibration(gyroBias, gyroYawScale / 100.0f, gyroPitchScale / 100.0f);
    touchpadMouse = (*j)["touchpadMouse"].asBool(touchpadMouse);
    touchpadShortcuts = readTouchpadShortcutsOr((*j)["touchpadShortcuts"], input::defaultTouchpadShortcuts());
    micGain = static_cast<int>(std::clamp<int64_t>((*j)["micGain"].asInt(micGain), 0, 2));
    audioRoute = static_cast<int>(std::clamp<int64_t>((*j)["audioRoute"].asInt(audioRoute), 0, 1));
    voiceRouting = static_cast<int>(std::clamp<int64_t>((*j)["voiceRouting"].asInt(voiceRouting), 0, 1));
    padSpeakerVolume = static_cast<int>(std::clamp<int64_t>((*j)["padSpeakerVolume"].asInt(padSpeakerVolume), 0, 100));
    skippedUpdate = (*j)["skippedUpdate"].str();
    maxHeightCloud = static_cast<int>((*j)["maxHeightCloud"].asInt(0));
    maxHeightHome = static_cast<int>((*j)["maxHeightHome"].asInt(0));
    probedCloud = (*j)["probedCloud"].asInt(0);
    probedHome = (*j)["probedHome"].asInt(0);
    // Files from before v0.8.0 (no version): what still holds the old
    // defaults moves to the new ones (block smoothing low -> auto, FSR ->
    // FSR + clean-up); anything chosen otherwise stays.
    if ((*j)["settingsVersion"].asInt(1) < 2) {
        if (deband == 1) deband = 3;
        if (upscaler == 0) upscaler = 2;
        XC_LOGD("settings: from before v0.8.0; block smoothing %d, upscaling %d", deband, upscaler);
    }
    if ((*j)["settingsVersion"].asInt(1) < 3) {
        if (sharpness == 0) sharpness = 2;
        XC_LOGD("settings: upgraded default sharpness to %d (FidelityFX RCAS)", sharpness);
    }
    regionRtt.clear();
    for (const auto& [name, ms] : (*j)["regionRtt"].members())
        if (ms.asInt() > 0) regionRtt[name] = static_cast<int>(ms.asInt());
    queueReadyAlert = (*j)["queueReadyAlert"].asBool(queueReadyAlert);
    previewFeatures = (*j)["previewFeatures"].asBool(previewFeatures);
    syncTrophies = (*j)["syncTrophies"].asBool(syncTrophies);
    usbControllers = (*j)["usbControllers"].asBool((*j)["usb_controllers"].asBool((*j)["enable_onefamily"].asBool(usbControllers)));
    soloInputRedirect = (*j)["soloInputRedirect"].asBool((*j)["solo_input_redirect"].asBool(soloInputRedirect));
    titlePlaytime.clear();
    for (const auto& [id, sec] : (*j)["titlePlaytime"].members())
        if (sec.asInt() > 0) titlePlaytime[id] = static_cast<uint64_t>(sec.asInt());
    listsAccount = (*j)["listsAccount"].str();
    accountLists.clear();
    for (const auto& [xuid, lists] : (*j)["accountLists"].members()) {
        if (xuid.empty() || xuid == listsAccount) continue;
        AccountLists& l = accountLists[xuid];
        for (const auto& id : lists["hidden"].items())
            if (!id.str().empty()) l.hidden.push_back(id.str());
        for (const auto& id : lists["favorites"].items())
            if (!id.str().empty()) l.favorites.push_back(id.str());
        for (const auto& [id, sec] : lists["titlePlaytime"].members())
            if (sec.asInt() > 0) l.titlePlaytime[id] = static_cast<uint64_t>(sec.asInt());
    }
    perGame.clear();
    for (const auto& [id, obj] : (*j)["perGame"].members()) {
        GameProfile p;
        p.sharpness = static_cast<int>(std::clamp<int64_t>(obj["sharpness"].asInt(sharpness), 0, 3));
        p.deband = static_cast<int>(std::clamp<int64_t>(obj["deband"].asInt(deband), 0, 3));
        p.upscaler = static_cast<int>(std::clamp<int64_t>(obj["upscaler"].asInt(upscaler), 0, 3));
        p.videoDecoder = static_cast<int>(std::clamp<int64_t>(obj["videoDecoder"].asInt(videoDecoder), 0, 1));
        int pFps = static_cast<int>(obj["fpsLimit"].asInt(fpsLimit));
        p.fpsLimit = (pFps == 30 || pFps == 60 || pFps == 120) ? pFps : 0;
        int resDef = resolution == "720p" ? 1 : resolution == "1440p" ? 2 : 0;
        p.resolution = static_cast<int>(std::clamp<int64_t>(obj["resolution"].asInt(resDef), 0, 2));
        p.deadzoneLeft = static_cast<int>(std::clamp<int64_t>(obj["deadzoneLeft"].asInt(deadzoneLeft), 0, 50));
        p.deadzoneRight = static_cast<int>(std::clamp<int64_t>(obj["deadzoneRight"].asInt(deadzoneRight), 0, 50));
        p.triggerStrength = static_cast<int>(std::clamp<int64_t>(obj["triggerStrength"].asInt(triggerStrength), 0, 4));
        p.triggerHz = static_cast<int>(std::clamp<int64_t>(obj["triggerHz"].asInt(triggerHz), 0, 2));
        p.triggerResistance = static_cast<int>(std::clamp<int64_t>(obj["triggerResistance"].asInt(triggerResistance), 0, 3));
        p.triggerPulses = obj["triggerPulses"].asBool(triggerPulses);
        p.triggerProfile = static_cast<int>(std::clamp<int64_t>(obj["triggerProfile"].asInt(triggerProfile), 0, 2));
        p.audioHaptics = static_cast<int>(std::clamp<int64_t>(obj["audioHaptics"].asInt(audioHaptics), 0, 3));
        p.circleConfirms = obj["circleConfirms"].asBool(circleConfirms);
        p.highRefresh = obj["highRefresh"].asBool(highRefresh);
        p.keyMap = keyMapFromJson(obj["keyMap"], keyMap);
        p.padMap = readPadMapOr(obj["padMap"], padMap, id.c_str());
        p.gyroMode = static_cast<int>(std::clamp<int64_t>(obj["gyroMode"].asInt(gyroMode), 0, 4));
        p.gyroLevel = static_cast<int>(std::clamp<int64_t>(obj["gyroLevel"].asInt(gyroLevel), 1, 10));
        p.gyroYawScale = static_cast<int>(std::clamp<int64_t>(obj["gyroYawScale"].asInt(gyroYawScale), 20, 200));
        p.gyroPitchScale = static_cast<int>(std::clamp<int64_t>(obj["gyroPitchScale"].asInt(gyroPitchScale), 20, 200));
        p.gyroCurveStart = static_cast<int>(std::clamp<int64_t>(obj["gyroCurveStart"].asInt(gyroCurveStart), 0, 40));
        p.gyroCurveExp = static_cast<int>(std::clamp<int64_t>(obj["gyroCurveExp"].asInt(gyroCurveExp), 10, 40));
        p.gyroInvertX = obj["gyroInvertX"].asBool(gyroInvertX);
        p.gyroInvertY = obj["gyroInvertY"].asBool(gyroInvertY);
        if (obj["gyroBias"].isArray() && obj["gyroBias"].items().size() >= 3) {
            p.gyroBias[0] = static_cast<float>(obj["gyroBias"][0].asNumber(gyroBias[0]));
            p.gyroBias[1] = static_cast<float>(obj["gyroBias"][1].asNumber(gyroBias[1]));
            p.gyroBias[2] = static_cast<float>(obj["gyroBias"][2].asNumber(gyroBias[2]));
        } else {
            p.gyroBias[0] = gyroBias[0];
            p.gyroBias[1] = gyroBias[1];
            p.gyroBias[2] = gyroBias[2];
        }
        p.touchpadMouse = obj["touchpadMouse"].asBool(touchpadMouse);
        p.touchpadShortcuts = readTouchpadShortcutsOr(obj["touchpadShortcuts"], touchpadShortcuts);
        p.locale = obj["locale"].str();
        // A profile written before the choice was recorded has "syncTrophies" too, false by
        // default from merely being made: without the choice it follows the general setting.
        p.syncTrophiesChosen = obj["syncTrophiesChosen"].asBool(false);
        p.syncTrophies = p.syncTrophiesChosen ? obj["syncTrophies"].asBool(syncTrophies) : syncTrophies;
        p.xboxTitleId = obj["xboxTitleId"].str();
        p.gameName = obj["gameName"].str();
        p.soloInputRedirect = obj["soloInputRedirect"].asBool(obj["solo_input_redirect"].asBool(soloInputRedirect));
        perGame[id] = p;
    }
    return true;
}

float Settings::micGainFactor() const {
    static constexpr float kFactors[] = {0.5f, 1.0f, 1.5f};
    return kFactors[std::clamp(micGain, 0, 2)];
}

GameProfile Settings::defaultProfile() const {
    GameProfile p;
    p.sharpness = sharpness;
    p.deband = deband;
    p.upscaler = upscaler;
    p.videoDecoder = videoDecoder;
    p.fpsLimit = fpsLimit;
    p.resolution = resolution == "720p" ? 1 : resolution == "1440p" ? 2 : 0;
    p.deadzoneLeft = deadzoneLeft;
    p.deadzoneRight = deadzoneRight;
    p.triggerStrength = triggerStrength;
    p.triggerHz = triggerHz;
    p.triggerResistance = triggerResistance;
    p.triggerPulses = triggerPulses;
    p.triggerProfile = triggerProfile;
    p.audioHaptics = audioHaptics;
    p.circleConfirms = circleConfirms;
    p.highRefresh = highRefresh;
    p.keyMap = keyMap;
    p.padMap = padMap;
    p.gyroMode = gyroMode;
    p.gyroLevel = gyroLevel;
    p.gyroYawScale = gyroYawScale;
    p.syncTrophies = syncTrophies;  // follows the general setting until chosen for the game
    p.syncTrophiesChosen = false;
    p.gyroPitchScale = gyroPitchScale;
    p.gyroCurveStart = gyroCurveStart;
    p.gyroCurveExp = gyroCurveExp;
    p.gyroInvertX = gyroInvertX;
    p.gyroInvertY = gyroInvertY;
    p.gyroBias[0] = gyroBias[0];
    p.gyroBias[1] = gyroBias[1];
    p.gyroBias[2] = gyroBias[2];
    p.touchpadMouse = touchpadMouse;
    p.touchpadShortcuts = touchpadShortcuts;
    p.soloInputRedirect = soloInputRedirect;
    return p;
}

GameProfile Settings::profileForGame(const std::string& productId, const std::string& titleId, bool* hasCustom) const {
    if (!productId.empty()) {
        auto it = perGame.find(productId);
        if (it != perGame.end()) {
            if (hasCustom) *hasCustom = true;
            return it->second;
        }
    }
    if (!titleId.empty()) {
        auto it = perGame.find(titleId);
        if (it != perGame.end()) {
            if (hasCustom) *hasCustom = true;
            return it->second;
        }
    }
    if (hasCustom) *hasCustom = false;
    return defaultProfile();
}

Settings::ListsSwitch Settings::useListsOf(const std::string& xuid) {
    if (xuid.empty() || xuid == listsAccount) return ListsSwitch::Same;
    if (listsAccount.empty()) {  // from before the lists had an owner: this account's
        listsAccount = xuid;
        accountLists.erase(xuid);
        return ListsSwitch::Adopted;
    }
    accountLists[listsAccount] = AccountLists{std::move(hidden), std::move(favorites), std::move(titlePlaytime)};
    AccountLists mine;
    if (auto it = accountLists.find(xuid); it != accountLists.end()) {
        mine = std::move(it->second);
        accountLists.erase(it);
    }
    hidden = std::move(mine.hidden);
    favorites = std::move(mine.favorites);
    titlePlaytime = std::move(mine.titlePlaytime);
    listsAccount = xuid;
    return ListsSwitch::Changed;
}

bool Settings::save(const std::string& path) const {
    json::Value v = json::Value::object();
    v.set("language", language);
    v.set("resolution", resolution);
    v.set("region", region);
    v.set("loginCountry", loginCountry);
    v.set("storeCountry", normalizeStoreCountry(storeCountry));
    json::Value rtt = json::Value::object();
    for (const auto& [name, ms] : regionRtt) rtt.set(name, ms);
    v.set("regionRtt", rtt);
    json::Value hiddenIds = json::Value::array();
    for (const auto& id : hidden) hiddenIds.push(id);
    v.set("hidden", hiddenIds);
    json::Value favIds = json::Value::array();
    for (const auto& id : favorites) favIds.push(id);
    v.set("favorites", favIds);
    json::Value wishIds = json::Value::array();
    for (const auto& id : wishlist) wishIds.push(id);
    v.set("wishlist", wishIds);
    v.set("librarySort", librarySort);
    v.set("streamStats", streamStats);
    v.set("statsMask", static_cast<int64_t>(statsMask));
    v.set("sharpness", sharpness);
    v.set("deband", deband);
    v.set("upscaler", upscaler);
    v.set("videoDecoder", videoDecoder);
    v.set("fpsLimit", fpsLimit);
    v.set("deadzoneLeft", deadzoneLeft);
    v.set("deadzoneRight", deadzoneRight);
    v.set("triggerStrength", triggerStrength);
    v.set("triggerHz", triggerHz);
    v.set("triggerResistance", triggerResistance);
    v.set("triggerPulses", triggerPulses);
    v.set("triggerProfile", triggerProfile);
    v.set("audioHaptics", audioHaptics);
    v.set("circleConfirms", circleConfirms);
    v.set("lightBarMode", lightBarMode);
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02X%02X%02X", static_cast<unsigned>(lightBarColour & 0xFF),
                  static_cast<unsigned>(lightBarColour >> 8 & 0xFF), static_cast<unsigned>(lightBarColour >> 16 & 0xFF));
    v.set("lightBarColour", std::string(hex));
    v.set("gestureHints", gestureHints);
    v.set("micEnabled", micEnabled);
    v.set("highRefresh", highRefresh);
    v.set("desktopFullscreen", desktopFullscreen);
    v.set("handheldProfile", handheldProfile);
    v.set("maxBitrateMbps", maxBitrateMbps);
    v.set("gameVolume", gameVolume);
    v.set("keyMap", keyMapToJson(keyMap));
    v.set("padMap", input::padMapJson(padMap));
    v.set("keyboardPlayer", keyboardPlayer);
    v.set("gyroMode", gyroMode);
    v.set("gyroLevel", gyroLevel);
    v.set("gyroYawScale", gyroYawScale);
    v.set("gyroPitchScale", gyroPitchScale);
    v.set("gyroCurveStart", gyroCurveStart);
    v.set("gyroCurveExp", gyroCurveExp);
    v.set("gyroInvertX", gyroInvertX);
    v.set("gyroInvertY", gyroInvertY);
    json::Value biasArr = json::Value::array();
    biasArr.push(static_cast<double>(gyroBias[0]));
    biasArr.push(static_cast<double>(gyroBias[1]));
    biasArr.push(static_cast<double>(gyroBias[2]));
    v.set("gyroBias", biasArr);
    v.set("touchpadMouse", touchpadMouse);
    v.set("touchpadShortcuts", input::touchpadShortcutsJson(touchpadShortcuts));
    v.set("micGain", micGain);
    v.set("audioRoute", audioRoute);
    v.set("voiceRouting", voiceRouting);
    v.set("padSpeakerVolume", padSpeakerVolume);
    v.set("queueReadyAlert", queueReadyAlert);
    v.set("previewFeatures", previewFeatures);
    v.set("syncTrophies", syncTrophies);
    v.set("usbControllers", usbControllers);
    v.set("soloInputRedirect", soloInputRedirect);
    json::Value pt = json::Value::object();
    for (const auto& [id, sec] : titlePlaytime)
        if (sec > 0) pt.set(id, static_cast<int64_t>(sec));
    v.set("titlePlaytime", pt);
    v.set("listsAccount", listsAccount);
    json::Value others = json::Value::object();
    for (const auto& [xuid, l] : accountLists) {
        json::Value o = json::Value::object(), h = json::Value::array(), f = json::Value::array(), t = json::Value::object();
        for (const auto& id : l.hidden) h.push(id);
        for (const auto& id : l.favorites) f.push(id);
        for (const auto& [id, sec] : l.titlePlaytime)
            if (sec > 0) t.set(id, static_cast<int64_t>(sec));
        o.set("hidden", h);
        o.set("favorites", f);
        o.set("titlePlaytime", t);
        others.set(xuid, o);
    }
    v.set("accountLists", others);
    v.set("skippedUpdate", skippedUpdate);
    v.set("maxHeightCloud", maxHeightCloud);
    v.set("maxHeightHome", maxHeightHome);
    v.set("probedCloud", probedCloud);
    v.set("probedHome", probedHome);
    json::Value pg = json::Value::object();
    for (const auto& [id, p] : perGame) {
        json::Value obj = json::Value::object();
        obj.set("sharpness", p.sharpness);
        obj.set("deband", p.deband);
        obj.set("upscaler", p.upscaler);
        obj.set("videoDecoder", p.videoDecoder);
        obj.set("fpsLimit", p.fpsLimit);
        obj.set("resolution", p.resolution);
        obj.set("deadzoneLeft", p.deadzoneLeft);
        obj.set("deadzoneRight", p.deadzoneRight);
        obj.set("triggerStrength", p.triggerStrength);
        obj.set("triggerHz", p.triggerHz);
        obj.set("triggerResistance", p.triggerResistance);
        obj.set("triggerPulses", p.triggerPulses);
        obj.set("triggerProfile", p.triggerProfile);
        obj.set("audioHaptics", p.audioHaptics);
        obj.set("circleConfirms", p.circleConfirms);
        obj.set("highRefresh", p.highRefresh);
        obj.set("keyMap", keyMapToJson(p.keyMap));
        obj.set("padMap", input::padMapJson(p.padMap));
        obj.set("gyroMode", p.gyroMode);
        obj.set("gyroLevel", p.gyroLevel);
        obj.set("gyroYawScale", p.gyroYawScale);
        obj.set("gyroPitchScale", p.gyroPitchScale);
        obj.set("gyroCurveStart", p.gyroCurveStart);
        obj.set("gyroCurveExp", p.gyroCurveExp);
        obj.set("gyroInvertX", p.gyroInvertX);
        obj.set("gyroInvertY", p.gyroInvertY);
        json::Value objBias = json::Value::array();
        objBias.push(static_cast<double>(p.gyroBias[0]));
        objBias.push(static_cast<double>(p.gyroBias[1]));
        objBias.push(static_cast<double>(p.gyroBias[2]));
        obj.set("gyroBias", objBias);
        obj.set("touchpadMouse", p.touchpadMouse);
        obj.set("touchpadShortcuts", input::touchpadShortcutsJson(p.touchpadShortcuts));
        if (!p.locale.empty()) obj.set("locale", p.locale);
        obj.set("syncTrophies", p.syncTrophiesChosen ? p.syncTrophies : syncTrophies);
        obj.set("syncTrophiesChosen", p.syncTrophiesChosen);
        if (!p.xboxTitleId.empty()) obj.set("xboxTitleId", p.xboxTitleId);
        if (!p.gameName.empty()) obj.set("gameName", p.gameName);
        obj.set("soloInputRedirect", p.soloInputRedirect);
        pg.set(id, obj);
    }
    v.set("perGame", pg);
    v.set("settingsVersion", 3);
    if (platform::writeFileAtomic(path, v.dump())) {
        SettingsManager::noteSaved(*this, path);  // the manager holds the same settings from here on
        return true;
    }
    XC_LOGW("settings: could not save %s", path.c_str());  // every caller's warning
    return false;
}

}  // namespace xc::app
