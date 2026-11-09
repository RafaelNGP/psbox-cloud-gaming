// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// User-facing text in every supported language. English is the default; the
// user picks another in Settings. To add a language: a Language value, its
// codes in strings.cpp, and a column in the table there.
#pragma once

#include <string>

namespace xc::ui {

enum class Str {
    AppName, CloudGaming, SigningIn, RequestingCode, LoadingGames, SignInTitle, SignInStep1, SignInStep2,
    SignInScan, SignInWaiting, JumpBackIn, Play, Back, Select, SignOut, Cancel, TryAgain, GettingReady,
    InQueue, Connecting, StartingStream, MenuGestureHint, StreamEnded, ErrorTitle, SignedInAs, NoGames,
    PublisherBy, Settings, Language, Resolution, Region, RegionAuto, Res1080, Res720, ResBest, SettingsNote,
    Change, Adjust, HoldSignOut, SigningOutIn, SignInFailed, SignInInteractionRequired, LibraryFailed,
    StreamFailed, IdleWarning, YourGames, NotPlayable, TabGamePass, TabSearch, SearchHint, NoResults,
    ResultsCount, KeySpace, KeyDelete, KeyClear, GamesCount, KeyType, TryingRegion, AvailableToBuy, BuyBadge,
    BuyToPlay, BuyHint, ScanToBuy, SearchIn, Free, Hide, Unhide, HiddenSection, HiddenHint, HiddenToast,
    UnhiddenToast, SortLabel, SortRecent, SortAZ, SortConsole, SortHint, FilterFree, FilterCheapest,
    FilterAllConsoles, OneGame, Sections, UpdatingList, MenuTitle, MenuStats, On, Off, MenuResolution,
    MenuLeave, StatLatency, StatBitrate, StatFrameRate, StatLoss, StatRegion, StatDecode, ResolutionNote,
    MenuSharpness, SharpOff, SharpLow, SharpMedium, SharpHigh, Deadzone, TriggerRumble, ConfirmButton,
    Activated, Deactivated, UpdateAvailable, Controllers, PadConnected, PadDisconnected, MenuDeband,
    StatOnScreen, Reconnecting, Reconnected, LightBar, MenuUpscaler, UpscalerAi, FreeToPlay, FreeHint,
    ScanToGet, NoEntitlementFree, NoEntitlement, TabConsoles, ConsoleOn, ConsoleSleeping, ConsoleOff,
    ConsolesLoading, NoConsoles, ConsolesHint, WakingConsole, WakeFailed, ConsoleStep1, ConsoleStep2,
    ConsoleStep3, ScanForHelp, SearchAgain, MenuXboxButton, MenuEndStream, EndedOnXbox, EndedByOtherDevice,
    EndedXboxOff, StreamingStuck, GestureHint, UpscalerFsrClean, UpscalerAiClean, DebandAuto, UpdatePrompt,
    UpdateNow, NotNow, Updates, UpToDate, UpdateReady, UpdateChecking, UpdatingTo, UpdateDownloading,
    UpdateVerifying, UpdateInstalling, UpdateRestarting, UpdateReopen, UpdateFailed, LightBarGame,
    LightBarCustom, LightBarColour, ColourPickerHelp, TriggerLight, TriggerMedium, TriggerStrong,
    TriggerMax, TriggerIntensity, TriggerFrequency, FrequencyLow, FrequencyHigh, TriggerTestHelp,
    TriggerLevel, StickLeft, StickRight, StickPosition, StickGameGets, StickTestHelp, TriggerResistance,
    TriggerStyle, StyleVibration, StylePulses, FilterMode, FilterGenre, FilterLanguage, ModeAll, ModeSingle,
    ModeOnlineMulti, ModeOnlineCoop, ModeLocal, GenreAll, LanguageAll, LanguageNoun, LangSubtitles,
    LangAudio, GameSettings, Profile, ProfileDefault, ProfileCustom, ResetToDefault, FilterPublisher,
    PublisherAll, PublisherIndies, FriendsPlayingNow, MenuMicrophone, MicMuted, MicActive, MenuAudioOutput,
    AudioRouteTv, AudioRoutePad, ReportProblem, ReportScan, ReportIncludes, CrashedLastTime,
    LoginCountry, LoginCountryOff, TryOtherCountry, LoginCountryWarning, RegionRefused, RegionRefusedAs,
    CountryBR, CountryJP, CountryKR, CountryPL, CountryUS, ClockWrong, ConnectionIntercepted,
    ClockWrongNotice, SignInNotSaved, QueueSeconds, QueueMinutes, HighRefresh, HighRefreshOn,
    ConnectionUnstable, SessionSummary, BitrateCap, BitrateAuto, GameVolume, MenuKeyHint,
    FilterMouseKeyboard, MouseKeyboardSupport, KeyboardPlayer, InQueueNoEstimate, KeyboardKeys, DPad,
    PressKey, KeyReserved, ChangeKey, ClearKey, KeysHelp, Gyro, GyroAlways, GyroAiming, GyroIntensity,
    ClockWrongPc, ClockWrongNoticePc, LoginCountryOffPc, OpenInBrowser, UpdateReadyPc, GestureHintButtons,
    MenuButtonHint, DisplayMode, DisplayWindow, DisplayFullscreen, QuitTitle, QuitConfirm,
    ControllerButtons, PadPressButton, PadMissing, DiscardChanges, PadHelp, FilterPlan, PlanMine,
    PlanAll, IncludedWithPlan, TouchpadMouse, Favorites, AddFavorite, RemoveFavorite, AddedToFavorites,
    RemovedFromFavorites, GameLanguage, MonthlyLimitExceeded, PlaytimeRemaining, HoursRemaining,
    PlaytimeWarning, TriggerProfile, TriggerProfileStd, TriggerProfileShooter, TriggerProfileRacing,
    StorageNotWritable, StorageFixHint, StatsCustomize, StatsIndicatorFps, StatsIndicatorBitrate,
    StatsIndicatorLatency, StatsIndicatorLoss, StatsIndicatorGraph, StatsIndicatorDelay, StatsIndicatorQp,
    StatsIndicatorMic, ScanQrToResolve, ClaimFree, Claiming, ClaimSuccess, ClaimFailed, ProductNotFree,
    FreeAcquireHint, DnsBlockedNanoDns, XboxProfileMissing, QueueReadyAlert, QueueReadyNotification,
    PlaytimePlayed, RefreshLibrary, RefreshingLibrary, XboxAccountBanned, XboxRegionNotSupported,
    XboxAdultVerificationRequired, XboxChildAccountFamily, CheckSignIn, RunCheck, NetworkTest, RunTest,
    Achievements, NoAchievements, AchievementLocked, SecretAchievement, Wishlist, AddWishlist,
    RemoveWishlist, AddedToWishlist, RemovedFromWishlist, WishlistOnSale, WishlistInGamePass,
    SupportProject, SupportTitle, SupportMessage, SupportKoFi, ScanToDonate, TestSound, WishlistEmpty,
    TipTouchpadMenu, TipKeyboardMouse, TipFreeStore, TipBugReportQr, TipSupportKofi, Tip120HzVideo,
    TipAnalogDeadzone, TipDualSenseMic, TipTouchpadMouse, TipGyroAiming, TipFSRUpscaling,
    TipCustomBitrate, TipNativeTrophies, TipPerGameTrophies, TipSmartGlassWebUI, TipClaimFreeStore,
    TipButtonRemap, TipInstantResume, TipInGameMenu, TipBackgroundSync, SettingDescLanguage,
    SettingDescResolution, SettingDescRegion, SettingDescDeadzone, SettingDescTriggerRumble,
    SettingDescConfirmButton, SettingDescLightBar, SettingDescUpdates, SettingDescLoginCountry,
    SettingDescHighRefresh, SettingDescBitrateCap, SettingDescGameVolume, SettingDescKeyboardKeys,
    SettingDescDisplayMode, SettingDescControllerButtons, SettingDescGyro, SettingDescQueueReady,
    SettingDescCheckSignIn, SettingDescNetworkTest, SettingDescSupportProject, WhatsNew, WhatsNewTitle,
    PadSpeakerVolume, Mute, WebControlTitle, WebControlSubtitle, WebControlDesc, VideoDecoder,
    VideoDecoderHw, VideoDecoderSw, SettingDescVideoDecoder, AudioHaptics, SettingDescAudioHaptics,
    TouchpadShortcuts, SettingDescTouchpadShortcuts, TouchpadSwipeUp, TouchpadSwipeDown,
    TouchpadSwipeLeft, TouchpadSwipeRight, TouchpadClickLeft, TouchpadClickRight, TouchpadActNone,
    TouchpadActGameMenu, TouchpadActXboxButton, TouchpadActToggleStats, TouchpadActToggleMic,
    TouchpadActCycleGyro, TouchpadActCycleTrigger, TouchpadHelp, ResetSettings,
    SettingDescResetSettings, SettingsResetNotice, HoldToReset, LibraryUpToDate, LibraryUpdatedCount,
    OfflineMode, PreviewFeatures, SettingDescPreviewFeatures, GamePassRequired, InQueueIndeterminate,
    InQueueEstimated, F2pPreviewRequiredDesc, F2pPreviewAccountNeededDesc, MenuPlayer, CloudTimeUp,
    EndedIdle, CloudTimeLeft, CloudTimeOf, CloudTimeUnlimited, CloudTimeWarn, SyncTrophies,
    SettingDescSyncTrophies, RunSyncTrophies, TrophiesSyncedToast, TrophiesDisclaimerTitle,
    TrophiesDisclaimerBody, TrophiesDisclaimerAware, TrophiesDisclaimerCancel, RemoveTrophies,
    SettingDescRemoveTrophies, RemoveTrophiesConfirmTitle, RemoveTrophiesConfirmBody,
    RemoveTrophiesConfirm, TrophiesRemovedToast, SyncingTrophies, Count
};

enum class Language { English, PortugueseBR, Spanish, French, German, Italian, Count };
void setLanguage(Language lang);
Language language();
const char* languageName(Language lang);
const char* languageCode(Language lang);
Language languageFromCode(const std::string& code);
const char* catalogLanguage();
const char* gameLocale();
const char* tr(Str id);
std::string trf(Str id, const std::string& arg);
std::string trf(Str id, const std::string& arg1, const std::string& arg2);
std::string touchpadActionLabel(int actionIndex);
const char* touchpadTriggerLabel(int triggerIndex);

}  // namespace xc::ui
