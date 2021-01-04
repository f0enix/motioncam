package com.motioncam.model;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;

public class SettingsViewModel extends ViewModel {
    public static final String CAMERA_SHARED_PREFS          = "camera_prefs";

    public static final int MINIMUM_MEMORY_USE_MB           = 256;
    public static final int MAXIMUM_MEMORY_USE_MB           = 2048;

    public static final String PREFS_KEY_MEMORY_USE_MBYTES  = "memory_use_megabytes";
    public static final String PREFS_KEY_JPEG_QUALITY       = "jpeg_quality";
    public static final String PREFS_KEY_HDR_ENABLED        = "hdr_enabled";
    public static final String PREFS_KEY_DEBUG_MODE         = "debug_mode";

    public static final String PREFS_KEY_CONTRAST           = "camera_profile_contrast";
    public static final String PREFS_KEY_SHARPNESS          = "camera_profile_sharpness";
    public static final String PREFS_KEY_DETAIL             = "camera_profile_detail";

    public static final String PREFS_KEY_SATURATION         = "camera_profile_saturation";
    public static final String PREFS_KEY_GREEN_SATURATION   = "camera_profile_green_saturation";
    public static final String PREFS_KEY_BLUE_SATURATION    = "camera_profile_blue_saturation";

    public static final String PREFS_KEY_IGNORE_CAMERA_IDS  = "ignore_camera_ids";

    final public MutableLiveData<Integer> memoryUseMb = new MutableLiveData<>();
    final public MutableLiveData<Integer> jpegQuality = new MutableLiveData<>();
    final public MutableLiveData<Boolean> debugMode = new MutableLiveData<>();
    final public MutableLiveData<Integer> contrast = new MutableLiveData<>();
    final public MutableLiveData<Integer> saturation = new MutableLiveData<>();
    final public MutableLiveData<Integer> greenSaturation = new MutableLiveData<>();
    final public MutableLiveData<Integer> blueSaturation = new MutableLiveData<>();
    final public MutableLiveData<Integer> sharpness = new MutableLiveData<>();
    final public MutableLiveData<Integer> detail = new MutableLiveData<>();

    public void load(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        memoryUseMb.setValue(prefs.getInt(PREFS_KEY_MEMORY_USE_MBYTES, 512) - MINIMUM_MEMORY_USE_MB);
        jpegQuality.setValue(prefs.getInt(PREFS_KEY_JPEG_QUALITY, 98));
        debugMode.setValue(prefs.getBoolean(PREFS_KEY_DEBUG_MODE, false));

        contrast.setValue(prefs.getInt(PREFS_KEY_CONTRAST, CameraProfile.DEFAULT_CONTRAST));
        sharpness.setValue(prefs.getInt(PREFS_KEY_SHARPNESS, CameraProfile.DEFAULT_SHARPNESS));
        detail.setValue(prefs.getInt(PREFS_KEY_DETAIL, CameraProfile.DEFAULT_DETAIL));

        saturation.setValue(prefs.getInt(PREFS_KEY_SATURATION, CameraProfile.DEFAULT_SATURATION));
        greenSaturation.setValue(prefs.getInt(PREFS_KEY_GREEN_SATURATION, CameraProfile.DEFAULT_GREEN_SATURATION));
        blueSaturation.setValue(prefs.getInt(PREFS_KEY_BLUE_SATURATION, CameraProfile.DEFAULT_BLUE_SATURATION));
    }

    private <T> T getSetting(LiveData<T> setting, T defaultValue) {
        return setting.getValue() == null ? defaultValue : setting.getValue();
    }

    public void save(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = prefs.edit();

        editor.putInt(PREFS_KEY_MEMORY_USE_MBYTES, MINIMUM_MEMORY_USE_MB + getSetting(memoryUseMb, 256));
        editor.putInt(PREFS_KEY_JPEG_QUALITY, getSetting(jpegQuality, 95));
        editor.putBoolean(PREFS_KEY_DEBUG_MODE, getSetting(debugMode, false));

        editor.putInt(PREFS_KEY_CONTRAST, getSetting(contrast, CameraProfile.DEFAULT_CONTRAST));

        editor.putInt(PREFS_KEY_SATURATION, getSetting(saturation, CameraProfile.DEFAULT_SATURATION));
        editor.putInt(PREFS_KEY_GREEN_SATURATION, getSetting(greenSaturation, CameraProfile.DEFAULT_GREEN_SATURATION));
        editor.putInt(PREFS_KEY_BLUE_SATURATION, getSetting(blueSaturation, CameraProfile.DEFAULT_BLUE_SATURATION));

        editor.putInt(PREFS_KEY_SHARPNESS, getSetting(sharpness, CameraProfile.DEFAULT_SHARPNESS));
        editor.putInt(PREFS_KEY_DETAIL, getSetting(detail, CameraProfile.DEFAULT_DETAIL));

        editor.apply();
    }
}
