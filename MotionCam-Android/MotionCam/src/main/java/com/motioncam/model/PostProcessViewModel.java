package com.motioncam.model;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;

import com.motioncam.DenoiseSettings;
import com.motioncam.camera.AsyncNativeCameraOps;
import com.motioncam.camera.CameraManualControl;
import com.motioncam.camera.NativeCameraBuffer;
import com.motioncam.camera.NativeCameraInfo;
import com.motioncam.camera.NativeCameraMetadata;
import com.motioncam.camera.NativeCameraSessionBridge;
import com.motioncam.camera.PostProcessSettings;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public class PostProcessViewModel extends ViewModel {
    public enum SpatialDenoiseAggressiveness {
        OFF(0.0f, 0),
        NORMAL(0.5f, 1),
        HIGH(1.0f, 2);

        SpatialDenoiseAggressiveness(float weight, int optionValue) {
            mWeight = weight;
            mOptionValue = optionValue;
        }

        private float mWeight;
        private int mOptionValue;

        public float getWeight() {
            return mWeight;
        }

        public int getOptionValue() {
            return mOptionValue;
        }

        public static SpatialDenoiseAggressiveness GetFromOption(int option) {
            for(SpatialDenoiseAggressiveness denoiseAggressiveness : SpatialDenoiseAggressiveness.values()) {
                if(denoiseAggressiveness.getOptionValue() == option) {
                    return denoiseAggressiveness;
                }
            }

            return SpatialDenoiseAggressiveness.NORMAL;
        }
    }

    private List<NativeCameraBuffer> mAvailableImages;
    private MutableLiveData<PostProcessSettings> mPostProcessSettings = new MutableLiveData<>();
    private MutableLiveData<PostProcessSettings> mEstimatedSettings = new MutableLiveData<>();

    final public MutableLiveData<Integer> shadows = new MutableLiveData<>();
    final public MutableLiveData<Integer> whitePoint = new MutableLiveData<>();
    final public MutableLiveData<Integer> exposure = new MutableLiveData<>();
    final public MutableLiveData<Integer> contrast = new MutableLiveData<>();
    final public MutableLiveData<Integer> blacks = new MutableLiveData<>();
    final public MutableLiveData<Integer> saturation = new MutableLiveData<>();
    final public MutableLiveData<Integer> greens = new MutableLiveData<>();
    final public MutableLiveData<Integer> blues = new MutableLiveData<>();
    final public MutableLiveData<Integer> temperature = new MutableLiveData<>();
    final public MutableLiveData<Integer> tint = new MutableLiveData<>();
    final public MutableLiveData<Integer> sharpness = new MutableLiveData<>();
    final public MutableLiveData<Integer> detail = new MutableLiveData<>();
    final public MutableLiveData<Integer> numMergeImages = new MutableLiveData<>();
    final public MutableLiveData<Integer> spatialDenoiseAggressiveness = new MutableLiveData<>();
    final public MutableLiveData<Boolean> saveDng = new MutableLiveData<>();
    final public MutableLiveData<Boolean> isFlipped = new MutableLiveData<>();

    final private MutableLiveData<Integer> jpegQuality = new MutableLiveData<>();

    public List<NativeCameraBuffer> getAvailableImages(NativeCameraSessionBridge cameraSessionBridge) {
        if(mAvailableImages != null)
            return mAvailableImages;

        // Copy buffer handles, if there are any.
        NativeCameraBuffer[] nativeCameraBuffers = cameraSessionBridge.getAvailableImages();
        if(nativeCameraBuffers == null)
            return new ArrayList<>();

        mAvailableImages = new ArrayList<>(Arrays.asList(nativeCameraBuffers));

        // Return reverse order sorting
        Collections.sort(mAvailableImages, Collections.reverseOrder());

        return mAvailableImages;
    }

    private <T> T getSetting(LiveData<T> setting, T defaultValue) {
        return setting.getValue() == null ? defaultValue : setting.getValue();
    }

    public boolean getWriteDng() {
        return getSetting(saveDng, false);
    }

    public boolean getIsFlipped() {
        return getSetting(isFlipped, false);
    }

    public float getShadowsSetting() {
        return (float) Math.pow(2.0f, getSetting(shadows, 0) / 100.0f * 6.0f);
    }

    public float getWhitePointSetting() {
        return -0.005f * getSetting(whitePoint, CameraProfile.DEFAULT_WHITE_POINT) + 1.25f;
    }

    public float getContrastSetting() {
        return getSetting(contrast, CameraProfile.DEFAULT_CONTRAST) / 100.0f;
    }

    public float getBlacksSetting() {
        return getSetting(blacks, 0) / 400.0f;
    }

    public float getExposureSetting() {
        return (getSetting(exposure, 16) - 16.0f) / 4.0f;
    }

    public float getSaturationSetting() {
        return getSetting(saturation, CameraProfile.DEFAULT_SATURATION) / 100.0f * 2.0f;
    }

    public float getGreensSetting() {
        return (getSetting(greens, CameraProfile.DEFAULT_GREENS) - 50) / 100.0f * 40.0f;
    }

    private float getBluesSetting() {
        return (getSetting(blues, CameraProfile.DEFAULT_BLUES) - 50) / 100.0f * 40.0f;
    }

    public int getTemperatureSetting() {
        return getSetting(temperature, 2500) + 2000;
    }

    public void setTemperature(float value) {
        temperature.setValue(Math.round(value) - 2000);
    }

    public int getTintSetting() {
        return getSetting(tint, 150) - 150;
    }

    public void setTint(float value) {
        tint.setValue(Math.round(value + 150));
    }

    public float getSharpnessSetting() {
        return 1.0f + getSetting(sharpness, CameraProfile.DEFAULT_SHARPNESS) / 20.0f;
    }

    public float getDetailSetting() {
        return 1.0f + getSetting(detail, CameraProfile.DEFAULT_DETAIL) / 20.0f;
    }

    public SpatialDenoiseAggressiveness getSpatialDenoiseAggressivenessSetting() {
        return PostProcessViewModel.SpatialDenoiseAggressiveness.GetFromOption(getSetting(spatialDenoiseAggressiveness, 0));
    }

    public void load(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        jpegQuality.setValue(prefs.getInt(SettingsViewModel.PREFS_KEY_JPEG_QUALITY, CameraProfile.DEFAULT_JPEG_QUALITY));
        saveDng.setValue(prefs.getBoolean(SettingsViewModel.PREFS_KEY_SAVE_DNG, false));
    }

    public LiveData<PostProcessSettings> estimateSettings(
            Context context, NativeCameraSessionBridge cameraSessionBridge, NativeCameraInfo cameraInfo, AsyncNativeCameraOps asyncNativeCameraOps) {
        // Return existing value if set
        if(mEstimatedSettings.getValue() != null)
            return mEstimatedSettings;

        NativeCameraMetadata metadata = cameraSessionBridge.getMetadata(cameraInfo);
        if(metadata == null) {
            return mPostProcessSettings;
        }

        final float[] cameraApertures = metadata.cameraApertures;

        // Estimate settings from first image in list
        List<NativeCameraBuffer> images = getAvailableImages(cameraSessionBridge);

        if(images.isEmpty()) {
            mPostProcessSettings.setValue(new PostProcessSettings());

            update(cameraApertures,10000000, 100, mPostProcessSettings.getValue());

            return mPostProcessSettings;
        }

        final int iso = images.get(0).iso;
        final long shutterSpeed = images.get(0).exposureTime;

        asyncNativeCameraOps.estimateSettings(false, 12.0f, (settings) -> {
            // Load user settings
            load(context);

            if(settings == null)
                settings = new PostProcessSettings();

            // Set estimated settings to whatever we received
            mEstimatedSettings.setValue(settings.clone());

            // Update from user overrides
            PostProcessSettings postProcessSettings = settings.clone();

            postProcessSettings.contrast = getContrastSetting();

            postProcessSettings.saturation = getSaturationSetting();
            postProcessSettings.blues = getBluesSetting();
            postProcessSettings.greens = getGreensSetting();

            postProcessSettings.sharpen0 = getSharpnessSetting();
            postProcessSettings.sharpen1 = getDetailSetting();

            mPostProcessSettings.setValue(postProcessSettings);

            update(cameraApertures, shutterSpeed, iso, postProcessSettings);
        });

        return mPostProcessSettings;
    }

    private void update(float[] cameraApertures, long shutterSpeed, int iso, PostProcessSettings settings) {
        // Light
        shadows.setValue((int)Math.ceil(Math.log(settings.shadows)/Math.log(1.85f) / 6.0f * 100.0f));
        whitePoint.setValue(Math.round(-200.0f * settings.whitePoint + 250.0f));
        contrast.setValue(Math.round(settings.contrast * 100));
        blacks.setValue(Math.round(settings.blacks * 400));
        exposure.setValue(Math.round(settings.exposure * 4 + 16));

        // Saturation


        saturation.setValue(Math.round(settings.saturation * 100) / 2);
        greens.setValue(Math.round(((-settings.greens/40) * 100) + 50));
        blues.setValue(Math.round(((-settings.blues/40) * 100) + 50));

        // White balance
        temperature.setValue(Math.round(settings.temperature - 2000));
        tint.setValue(Math.round(settings.tint + 150));

        // Detail
        sharpness.setValue(Math.round((settings.sharpen0 - 1.0f) * 20.0f));
        detail.setValue(Math.round((settings.sharpen1 - 1.0f) * 20.0f));

        // Denoise settings
        CameraManualControl.Exposure exposure = CameraManualControl.Exposure.Create(
                CameraManualControl.GetClosestShutterSpeed(shutterSpeed),
                CameraManualControl.GetClosestIso(CameraManualControl.GetIsoValuesInRange(100, 6400), iso));

        float a = 1.6f;
        if(cameraApertures == null || cameraApertures.length == 0)
            a = cameraApertures[0];

        DenoiseSettings denoiseSettings = new DenoiseSettings(0, (float) exposure.getEv(a), settings.shadows);
        PostProcessViewModel.SpatialDenoiseAggressiveness spatialNoise = SpatialDenoiseAggressiveness.NORMAL;

        numMergeImages.setValue(denoiseSettings.numMergeImages);
        spatialDenoiseAggressiveness.setValue(spatialNoise.getOptionValue());
        saveDng.setValue(settings.dng);
    }

    public PostProcessSettings getPostProcessSettings() {

        //
        // Update the settings from the model
        //

        PostProcessSettings settings = new PostProcessSettings();

        // Light
        settings.shadows = getShadowsSetting();
        settings.whitePoint = getWhitePointSetting();
        settings.contrast = getContrastSetting();
        settings.blacks = getBlacksSetting();
        settings.exposure = getExposureSetting();

        // Color
        settings.saturation = getSaturationSetting();
        settings.greens = getGreensSetting();
        settings.blues = getBluesSetting();

        // White balance
        settings.temperature = getTemperatureSetting();
        settings.tint = getTintSetting();

        // Detail
        settings.sharpen0 = getSharpnessSetting();
        settings.sharpen1 = getDetailSetting();

        // Noise reduction
        settings.spatialDenoiseAggressiveness = getSpatialDenoiseAggressivenessSetting().getWeight();

        // Apply JPEG quality
        settings.jpegQuality = getSetting(jpegQuality, CameraProfile.DEFAULT_JPEG_QUALITY);
        settings.flipped = getSetting(isFlipped, false);
        settings.dng = getWriteDng();

        return settings;
    }

    public PostProcessSettings getEstimatedSettings() {
        return mEstimatedSettings.getValue();
    }
}
