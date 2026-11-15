package com.rafaelngp.psbox;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends SDLActivity {
    private static final String TAG = "PSBox";
    private static final int PERMISSION_REQUEST_BT = 1001;
    private static final int PERMISSION_REQUEST_AUDIO = 1002;

    private static volatile boolean sStreamingActive = false;
    private static volatile boolean sIsForeground = false;
    private static WifiManager.WifiLock sWifiLock = null;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected String[] getArguments() {
        String selftest = getIntent().getStringExtra("selftest");
        if (selftest != null && !selftest.isEmpty()) {
            return new String[] { "--selftest", selftest };
        }
        return new String[0];
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (getWindow() != null) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
        configureEdgeToEdgeAndCutout();
        setImmersiveFullscreen();
        disableDefaultFocusHighlight();
        setDisplayRefreshRate(60.0f);
        checkAndRequestStartupPermissions();
    }

    @Override
    protected void onResume() {
        super.onResume();
        sIsForeground = true;
        updateWifiLock();
        setImmersiveFullscreen();
        disableDefaultFocusHighlight();
        setDisplayRefreshRate(60.0f);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            setImmersiveFullscreen();
            disableDefaultFocusHighlight();
        }
    }

    @Override
    protected void onPause() {
        sIsForeground = false;
        updateWifiLock();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        sStreamingActive = false;
        sIsForeground = false;
        updateWifiLock();
        super.onDestroy();
    }

    // --- 1. Edge-to-Edge & Cutout Inset Configuration ---
    private void configureEdgeToEdgeAndCutout() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            Window window = getWindow();
            if (window != null) {
                WindowManager.LayoutParams lp = window.getAttributes();
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
                } else {
                    lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                }
                window.setAttributes(lp);
            }
        }
    }

    private void setImmersiveFullscreen() {
        Window window = getWindow();
        if (window == null) return;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            View decorView = window.getDecorView();
            if (decorView != null) {
                int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                          | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                          | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                          | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                          | View.SYSTEM_UI_FLAG_FULLSCREEN
                          | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
                decorView.setSystemUiVisibility(flags);
            }
        }
    }

    private void disableDefaultFocusHighlight() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            try {
                Window window = getWindow();
                if (window != null && window.getDecorView() != null) {
                    window.getDecorView().setDefaultFocusHighlightEnabled(false);
                }
                if (mSurface != null) {
                    mSurface.setDefaultFocusHighlightEnabled(false);
                }
                if (mLayout != null) {
                    mLayout.setDefaultFocusHighlightEnabled(false);
                }
            } catch (Exception e) {
                Log.w(TAG, "disableDefaultFocusHighlight error: " + e.getMessage());
            }
        }
    }

    public static int[] getDisplayCutoutInsets() {
        try {
            Activity activity = (Activity) getContext();
            if (activity == null) return new int[]{0, 0, 0, 0};
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                Window window = activity.getWindow();
                if (window != null && window.getDecorView() != null) {
                    WindowInsets insets = window.getDecorView().getRootWindowInsets();
                    if (insets != null) {
                        DisplayCutout cutout = insets.getDisplayCutout();
                        if (cutout != null) {
                            return new int[]{
                                cutout.getSafeInsetLeft(),
                                cutout.getSafeInsetTop(),
                                cutout.getSafeInsetRight(),
                                cutout.getSafeInsetBottom()
                            };
                        }
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "getDisplayCutoutInsets error: " + e.getMessage());
        }
        return new int[]{0, 0, 0, 0};
    }

    // --- 2. Low-Latency Wi-Fi Lock with Lifecycle Awareness ---
    public static synchronized void setLowLatencyWifi(boolean enable) {
        sStreamingActive = enable;
        updateWifiLock();
    }

    private static synchronized void updateWifiLock() {
        try {
            Context ctx = getContext();
            if (ctx == null) return;
            WifiManager wm = (WifiManager) ctx.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm == null) return;

            if (sStreamingActive && sIsForeground) {
                if (sWifiLock == null) {
                    int mode = WifiManager.WIFI_MODE_FULL_HIGH_PERF;
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        mode = WifiManager.WIFI_MODE_FULL_LOW_LATENCY;
                    }
                    try {
                        sWifiLock = wm.createWifiLock(mode, "PSBox:StreamingLock");
                    } catch (Exception ex) {
                        Log.w(TAG, "Failed creating low-latency wifi lock, falling back to high-perf: " + ex.getMessage());
                        sWifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "PSBox:StreamingLock");
                    }
                    if (sWifiLock != null) {
                        sWifiLock.setReferenceCounted(false);
                    }
                }
                if (sWifiLock != null && !sWifiLock.isHeld()) {
                    sWifiLock.acquire();
                    Log.i(TAG, "Wi-Fi low-latency lock acquired");
                }
            } else {
                if (sWifiLock != null && sWifiLock.isHeld()) {
                    sWifiLock.release();
                    Log.i(TAG, "Wi-Fi low-latency lock released");
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "updateWifiLock error: " + e.getMessage());
        }
    }

    // --- 3. High Refresh Rate (120Hz / 60Hz Locking) ---
    public static boolean is120HzSupported() {
        try {
            Activity activity = (Activity) getContext();
            if (activity == null) return false;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                Display display = activity.getWindowManager().getDefaultDisplay();
                for (Display.Mode mode : display.getSupportedModes()) {
                    if (mode.getRefreshRate() >= 119.0f) {
                        return true;
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "is120HzSupported error: " + e.getMessage());
        }
        return false;
    }

    public static void setDisplayRefreshRate(float targetRate) {
        Activity activity = (Activity) getContext();
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            try {
                Window window = activity.getWindow();
                if (window == null) return;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    Display display = activity.getWindowManager().getDefaultDisplay();
                    Display.Mode[] modes = display.getSupportedModes();
                    Display.Mode bestMode = null;
                    for (Display.Mode mode : modes) {
                        if (Math.abs(mode.getRefreshRate() - targetRate) < 1.0f) {
                            if (bestMode == null ||
                                (mode.getPhysicalWidth() >= bestMode.getPhysicalWidth() &&
                                 mode.getPhysicalHeight() >= bestMode.getPhysicalHeight())) {
                                bestMode = mode;
                            }
                        }
                    }
                    WindowManager.LayoutParams lp = window.getAttributes();
                    int desiredModeId = (bestMode != null) ? bestMode.getModeId() : (targetRate <= 0.0f ? 0 : lp.preferredDisplayModeId);
                    if (lp.preferredDisplayModeId != desiredModeId) {
                        lp.preferredDisplayModeId = desiredModeId;
                        if (bestMode != null) {
                            Log.i(TAG, "Locking display mode to " + bestMode.getModeId() + " (" + bestMode.getRefreshRate() + " Hz)");
                        } else if (targetRate <= 0.0f) {
                            Log.i(TAG, "Resetting display mode to default");
                        }
                        window.setAttributes(lp);
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "setDisplayRefreshRate error: " + e.getMessage());
            }
        });
    }

    // --- 4. Runtime Permissions Flow ---
    private void checkAndRequestStartupPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            List<String> perms = new ArrayList<>();
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
                perms.add(Manifest.permission.BLUETOOTH_CONNECT);
            }
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
                perms.add(Manifest.permission.BLUETOOTH_SCAN);
            }
            if (!perms.isEmpty()) {
                Log.i(TAG, "Requesting Bluetooth permissions: " + perms);
                requestPermissions(perms.toArray(new String[0]), PERMISSION_REQUEST_BT);
            }
        }
    }

    public static boolean hasRecordAudioPermission() {
        Activity activity = (Activity) getContext();
        if (activity == null) return false;
        return activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED;
    }

    public static void requestRecordAudioPermission() {
        Activity activity = (Activity) getContext();
        if (activity == null) return;
        if (activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            activity.requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, PERMISSION_REQUEST_AUDIO);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSION_REQUEST_BT) {
            for (int i = 0; i < permissions.length; i++) {
                Log.i(TAG, "Permission " + permissions[i] + ": " +
                      (grantResults.length > i && grantResults[i] == PackageManager.PERMISSION_GRANTED ? "GRANTED" : "DENIED"));
            }
        }
    }

    // --- 5. Device Vibration / Haptics ---
    private static Vibrator sVibrator = null;
    private static boolean sVibratorInitialized = false;
    private static boolean sHasVibrator = false;
    private static boolean sHasAmplitudeControl = false;

    private static VibrationEffect sEffectClick = null;
    private static VibrationEffect sEffectTick = null;
    private static VibrationEffect sEffectHeavyClick = null;

    private static synchronized Vibrator getDeviceVibrator(Context ctx) {
        if (sVibratorInitialized) return sVibrator;
        if (ctx == null) return null;
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                VibratorManager vm = (VibratorManager) ctx.getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
                if (vm != null) sVibrator = vm.getDefaultVibrator();
            }
            if (sVibrator == null) {
                sVibrator = (Vibrator) ctx.getSystemService(Context.VIBRATOR_SERVICE);
            }
            if (sVibrator != null) {
                sHasVibrator = sVibrator.hasVibrator();
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    sHasAmplitudeControl = sVibrator.hasAmplitudeControl();
                }
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                sEffectClick = VibrationEffect.createPredefined(VibrationEffect.EFFECT_CLICK);
                sEffectTick = VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK);
                sEffectHeavyClick = VibrationEffect.createPredefined(VibrationEffect.EFFECT_HEAVY_CLICK);
            }
            sVibratorInitialized = true;
        } catch (Exception e) {
            Log.w(TAG, "vibrator init error: " + e.getMessage());
        }
        return sVibrator;
    }

    public static void vibrateDevice(int large, int small, int durationMs) {
        try {
            Activity activity = (Activity) getContext();
            Vibrator vib = getDeviceVibrator(activity);
            if (vib == null || !sHasVibrator) return;
            int amp = Math.max(large, (small * 3) / 4);
            if (amp <= 0 || durationMs <= 0) {
                vib.cancel();
                return;
            }
            int clampedAmp = Math.min(255, Math.max(1, amp));
            long dur = Math.min(5000L, Math.max(15L, (long) durationMs));
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                int actualAmp = sHasAmplitudeControl ? clampedAmp : VibrationEffect.DEFAULT_AMPLITUDE;
                vib.vibrate(VibrationEffect.createOneShot(dur, actualAmp));
            } else {
                vib.vibrate(dur);
            }
        } catch (Exception e) {
            // ignore
        }
    }

    public static void performHaptic(int type) {
        try {
            Activity activity = (Activity) getContext();
            if (activity == null) return;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                Vibrator vib = getDeviceVibrator(activity);
                if (vib != null && sHasVibrator) {
                    VibrationEffect effect = (type == 1) ? sEffectTick :
                                             (type == 2) ? sEffectHeavyClick :
                                                           sEffectClick;
                    if (effect != null) {
                        vib.vibrate(effect);
                        return;
                    }
                }
            }
            View decor = activity.getWindow().getDecorView();
            if (decor != null) {
                decor.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            }
        } catch (Exception ignored) {
        }
    }
}
