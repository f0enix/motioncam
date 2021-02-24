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

    public static final String PREFS_KEY_MEMORY_USE_MBYTES          = "memory_use_megabytes";
    public static final String PREFS_KEY_JPEG_QUALITY               = "jpeg_quality";
    public static final String PREFS_KEY_CAMERA_PREVIEW_QUALITY     = "camera_preview_quality";
    public static final String PREFS_KEY_DUAL_EXPOSURE_CONTROLS     = "dual_exposure_controls";

    public static final String PREFS_KEY_IGNORE_CAMERA_IDS                  = "ignore_camera_ids";
    public static final String PREFS_KEY_UI_PREVIEW_CONTRAST                = "ui_preview_contrast";
    public static final String PREFS_KEY_UI_PREVIEW_COLOUR                  = "ui_preview_colour";
    public static final String PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET      = "ui_preview_temperature_offset";
    public static final String PREFS_KEY_UI_PREVIEW_TINT_OFFSET             = "ui_preview_tint_offset";
    public static final String PREFS_KEY_UI_CAPTURE_MODE                    = "ui_capture_mode";

    final public MutableLiveData<Integer> memoryUseMb = new MutableLiveData<>();
    final public MutableLiveData<Integer> cameraPreviewQuality = new MutableLiveData<>();
    final public MutableLiveData<Integer> jpegQuality = new MutableLiveData<>();
    final public MutableLiveData<Boolean> dualExposureControls = new MutableLiveData<>();

    public void load(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        memoryUseMb.setValue(prefs.getInt(PREFS_KEY_MEMORY_USE_MBYTES, 256) - MINIMUM_MEMORY_USE_MB);
        cameraPreviewQuality.setValue(prefs.getInt(PREFS_KEY_CAMERA_PREVIEW_QUALITY, 0));
        jpegQuality.setValue(prefs.getInt(PREFS_KEY_JPEG_QUALITY, CameraProfile.DEFAULT_JPEG_QUALITY));
        dualExposureControls.setValue(prefs.getBoolean(PREFS_KEY_DUAL_EXPOSURE_CONTROLS, false));
    }

    private <T> T getSetting(LiveData<T> setting, T defaultValue) {
        return setting.getValue() == null ? defaultValue : setting.getValue();
    }

    public void save(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = prefs.edit();

        editor.putInt(PREFS_KEY_MEMORY_USE_MBYTES, MINIMUM_MEMORY_USE_MB + getSetting(memoryUseMb, 256));
        editor.putInt(PREFS_KEY_CAMERA_PREVIEW_QUALITY, getSetting(cameraPreviewQuality, 0));
        editor.putInt(PREFS_KEY_JPEG_QUALITY, getSetting(jpegQuality, 95));
        editor.putBoolean(PREFS_KEY_DUAL_EXPOSURE_CONTROLS, getSetting(dualExposureControls, false));

        editor.apply();
    }
}
