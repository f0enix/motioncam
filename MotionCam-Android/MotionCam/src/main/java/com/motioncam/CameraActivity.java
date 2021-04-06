package com.motioncam;

import android.Manifest;
import android.animation.ObjectAnimator;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.graphics.PointF;
import android.graphics.SurfaceTexture;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.util.Size;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.animation.LinearInterpolator;
import android.widget.FrameLayout;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import com.motioncam.camera.AsyncNativeCameraOps;
import com.motioncam.camera.CameraManualControl;
import com.motioncam.camera.NativeCameraBuffer;
import com.motioncam.camera.NativeCameraInfo;
import com.motioncam.camera.NativeCameraMetadata;
import com.motioncam.camera.NativeCameraSessionBridge;
import com.motioncam.camera.PostProcessSettings;
import com.motioncam.databinding.CameraActivityBinding;
import com.motioncam.model.CameraProfile;
import com.motioncam.model.SettingsViewModel;
import com.motioncam.processor.ProcessorReceiver;
import com.motioncam.processor.ProcessorService;
import com.motioncam.ui.BitmapDrawView;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.CompletableFuture;

public class CameraActivity extends AppCompatActivity implements
        SensorEventManager.SensorEventHandler,
        TextureView.SurfaceTextureListener,
        NativeCameraSessionBridge.CameraSessionListener,
        NativeCameraSessionBridge.CameraRawPreviewListener,
        View.OnTouchListener, ProcessorReceiver.Receiver {

    public static final String TAG = "MotionCam";

    private static final int PERMISSION_REQUEST_CODE = 1;
    private static final CameraManualControl.SHUTTER_SPEED MAX_EXPOSURE_TIME = CameraManualControl.SHUTTER_SPEED.EXPOSURE_1__0;
    private static final int HDR_UNDEREXPOSED_SHUTTER_SPEED_DIV = 8;
    public static final int SHADOW_UPDATE_FREQUENCY_MS = 500;

    private enum FocusState {
        AUTO,
        FIXED,
        FIXED_AF_AE
    }

    private enum CaptureMode {
        HDR,
        ZSL,
        BURST
    }

    private enum PreviewControlMode {
        CONTRAST,
        COLOUR,
        TINT,
        WARMTH
    }

    private boolean mHavePermissions;
    private TextureView mTextureView;
    private Surface mSurface;
    private CameraActivityBinding mBinding;
    private List<CameraManualControl.SHUTTER_SPEED> mExposureValues;
    private List<CameraManualControl.ISO> mIsoValues;
    private NativeCameraSessionBridge mNativeCamera;
    private List<NativeCameraInfo> mCameraInfos;
    private NativeCameraInfo mSelectedCamera;
    private int mSelectedCameraIdx;
    private NativeCameraMetadata mCameraMetadata;
    private SensorEventManager mSensorEventManager;
    private ProcessorReceiver mProgressReceiver;
    private Timer mShadowsUpdateTimer;

    private PostProcessSettings mPostProcessSettings;
    private float mTemperatureOffset;
    private float mTintOffset;
    private AsyncNativeCameraOps mAsyncNativeCameraOps;

    private boolean mManualControlsEnabled;
    private boolean mManualControlsSet;
    private CaptureMode mCaptureMode = CaptureMode.HDR;
    private PreviewControlMode mPreviewControlMode = PreviewControlMode.CONTRAST;

    private FocusState mFocusState = FocusState.AUTO;
    private PointF mAutoFocusPoint;
    private PointF mAutoExposurePoint;
    private int mIso;
    private long mExposureTime;
    private ObjectAnimator mShadowsAnimator;
    private ShadowTimerTask mShadowUpdateTimerTask;
    private float mShadowEstimated;
    private float mShadowOffset;

    private class ShadowTimerTask extends TimerTask {
        @Override
        public void run() {
            runOnUiThread(() -> {
                if(mNativeCamera == null || mPostProcessSettings == null)
                    return;

                float shadows = mNativeCamera.estimateShadows(12.0f);
                if(shadows <= 0)
                    return;

                if(mShadowsAnimator != null)
                    mShadowsAnimator.cancel();

                mShadowsAnimator =
                        ObjectAnimator.ofFloat(CameraActivity.this, "shadowValue", mShadowEstimated, shadows);

                mShadowsAnimator.setDuration(400);
                mShadowsAnimator.setInterpolator(new LinearInterpolator());
                mShadowsAnimator.setAutoCancel(true);

                mShadowsAnimator.start();
            });
        }
    }

    private final SeekBar.OnSeekBarChangeListener mManualControlsSeekBar = new SeekBar.OnSeekBarChangeListener() {
        @Override
        public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
            if(mManualControlsEnabled && mNativeCamera != null && fromUser) {
                int shutterSpeedIdx = ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).getProgress();
                int isoIdx = ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).getProgress();

                CameraManualControl.SHUTTER_SPEED shutterSpeed = mExposureValues.get(shutterSpeedIdx);
                CameraManualControl.ISO iso = mIsoValues.get(isoIdx);

                mNativeCamera.setManualExposureValues(iso.getIso(), shutterSpeed.getExposureTime());

                ((TextView) findViewById(R.id.manualControlIsoText)).setText(iso.toString());
                ((TextView) findViewById(R.id.manualControlShutterSpeedText)).setText(shutterSpeed.toString());

                mManualControlsSet = true;

                if(mFocusState == FocusState.FIXED_AF_AE)
                    setFocusState(FocusState.FIXED, mAutoFocusPoint);
            }
        }

        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
        }

        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        onWindowFocusChanged(true);

        mBinding = CameraActivityBinding.inflate(getLayoutInflater());
        setContentView(mBinding.getRoot());

        mProgressReceiver = new ProcessorReceiver(new Handler());

        mBinding.focusLockPointFrame.setOnClickListener(v -> onFixedFocusCancelled());
        mBinding.exposureLockPointFrame.setOnClickListener(v -> onFixedExposureCancelled());
        mBinding.settingsBtn.setOnClickListener(v -> onSettingsClicked());

        mBinding.shadowsSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if(fromUser)
                    onShadowsSeekBarChanged(progress);
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        mBinding.exposureSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if(fromUser)
                    onHighlightsSeekBarChanged(progress);
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        // Preview settings
        mBinding.previewFrame.previewSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if(fromUser)
                    updatePreviewControlsParam(progress);
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        // Buttons
        mBinding.captureBtn.setOnClickListener(v -> onCaptureClicked());
        mBinding.switchCameraBtn.setOnClickListener(v -> onSwitchCameraClicked());

        mBinding.hdrModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.burstModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.zslModeBtn.setOnClickListener(this::onCaptureModeClicked);

        mBinding.previewFrame.contrastBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.colourBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.tintBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.warmthBtn.setOnClickListener(this::onPreviewModeClicked);

        mBinding.hintText.setOnClickListener(this::onHintClicked);

        // Hide hint if already clicked
        int versionCode = -1;

        try {
            PackageInfo packageInfo = getPackageManager().getPackageInfo(getPackageName(), 0);
            versionCode = packageInfo.versionCode;
        }
        catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
        }

        SharedPreferences prefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        int hintVersion = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_HINT_VERSION, 0);
        if(hintVersion >= versionCode)
            mBinding.hintText.setVisibility(View.GONE);

        ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).setOnSeekBarChangeListener(mManualControlsSeekBar);
        ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).setOnSeekBarChangeListener(mManualControlsSeekBar);

        ((Switch) findViewById(R.id.manualControlSwitch)).setOnCheckedChangeListener((buttonView, isChecked) -> onCameraManualControlEnabled(isChecked));

        mSensorEventManager = new SensorEventManager(this, this);

        requestPermissions();
    }

    private void onHintClicked(View v) {
        int versionCode = 0;

        try {
            PackageInfo packageInfo = getPackageManager().getPackageInfo(getPackageName(), 0);
            versionCode = packageInfo.versionCode;
        }
        catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
        }

        SharedPreferences prefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        prefs.edit().putInt(SettingsViewModel.PREFS_KEY_UI_HINT_VERSION, versionCode).commit();

        // Hide the hint
        mBinding.hintText.setVisibility(View.GONE);

        onSettingsClicked();
    }

    private void onHighlightsSeekBarChanged(int progress) {
        if(mNativeCamera != null) {
            float value = progress / 100.0f;
            mNativeCamera.setExposureCompensation(value);
        }
    }

    private void onSettingsClicked() {
        Intent intent = new Intent(this, SettingsActivity.class);
        startActivity(intent);
    }

    private void onFixedFocusCancelled() {
        setFocusState(FocusState.AUTO, null);
    }

    private void onFixedExposureCancelled() {
        setFocusState(FocusState.FIXED, mAutoFocusPoint);
    }

    private void onCameraManualControlEnabled(boolean enabled) {
        if(mManualControlsEnabled == enabled)
            return;

        mManualControlsEnabled = enabled;
        mManualControlsSet = false;

        if(mManualControlsEnabled) {
            findViewById(R.id.cameraManualControlFrame).setVisibility(View.VISIBLE);
            findViewById(R.id.infoFrame).setVisibility(View.GONE);
            mBinding.exposureLayout.setVisibility(View.GONE);
        }
        else {
            findViewById(R.id.cameraManualControlFrame).setVisibility(View.GONE);
            findViewById(R.id.infoFrame).setVisibility(View.VISIBLE);
            mBinding.exposureLayout.setVisibility(View.VISIBLE);

            if(mSelectedCamera.supportsLinearPreview)
                findViewById(R.id.exposureCompFrame).setVisibility(View.VISIBLE);

            if(mNativeCamera != null) {
                mNativeCamera.setAutoExposure();
            }
        }

        updateManualControlView(mSensorEventManager.getOrientation());
    }

    private void setPostProcessingDefaults() {
        // Set initial preview values
        mPostProcessSettings = new PostProcessSettings();

        SharedPreferences prefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        int jpegQuality = prefs.getInt(SettingsViewModel.PREFS_KEY_JPEG_QUALITY, CameraProfile.DEFAULT_JPEG_QUALITY);

        mPostProcessSettings.shadows = 1.0f;
        mPostProcessSettings.contrast = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, 0.5f);
        mPostProcessSettings.saturation = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, 1.0f);
        mPostProcessSettings.greenSaturation = 1.0f;
        mPostProcessSettings.blueSaturation = 1.0f;
        mPostProcessSettings.sharpen0 = 3.5f;
        mPostProcessSettings.sharpen1 = 2.5f;
        mPostProcessSettings.whitePoint = -1;
        mPostProcessSettings.blacks = -1;
        mPostProcessSettings.tonemapVariance = 0.25f;
        mPostProcessSettings.jpegQuality = jpegQuality;

        mTemperatureOffset = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, 0);
        mTintOffset = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, 0);

        mShadowEstimated = 1.0f;
        mShadowOffset = 0.0f;

        updatePreviewTabUi(true);

        // Update capture mode
        mCaptureMode = getCaptureMode(prefs);

        updateCaptureModeUi();
    }

    @Override
    protected void onResume() {
        super.onResume();

        mSensorEventManager.enable();
        mProgressReceiver.setReceiver(this);

        mBinding.rawCameraPreview.setBitmap(null);

        // Reset manual controls
        ((Switch) findViewById(R.id.manualControlSwitch)).setChecked(false);
        updateManualControlView(mSensorEventManager.getOrientation());

        mBinding.focusLockPointFrame.setVisibility(View.INVISIBLE);
        mBinding.exposureLockPointFrame.setVisibility(View.INVISIBLE);
        mBinding.exposureSeekBar.setProgress(50);
        mBinding.shadowsSeekBar.setProgress(50);

        mFocusState = FocusState.AUTO;

        // Start camera when we have all the permissions
        if(mHavePermissions)
            initCamera();
    }

    @Override
    protected void onPause() {
        super.onPause();

        mSensorEventManager.disable();
        mProgressReceiver.setReceiver(null);

        if(mShadowsUpdateTimer != null) {
            mShadowsUpdateTimer.cancel();
            mShadowsUpdateTimer = null;
            mShadowUpdateTimerTask = null;
        }

        if(mNativeCamera != null) {
            mNativeCamera.stopCapture();
        }

        if(mSurface != null) {
            mSurface.release();
            mSurface = null;
        }

        mBinding.cameraFrame.removeView(mTextureView);
        mTextureView = null;

        // Save UI state (TODO: do this per camera id)
        if(mPostProcessSettings != null) {
            SharedPreferences prefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
            prefs
                .edit()
                .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, mPostProcessSettings.contrast)
                .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, mPostProcessSettings.saturation)
                .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, mTemperatureOffset)
                .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, mTintOffset)
                .putString(SettingsViewModel.PREFS_KEY_UI_CAPTURE_MODE, mCaptureMode.name())
                .apply();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        if(mNativeCamera != null) {
            mNativeCamera.destroy();
            mNativeCamera = null;
        }
    }

    private void onSwitchCameraClicked() {
        // Rotate switch camera button
        mBinding.switchCameraBtn.setEnabled(false);

        int rotation = (int) mBinding.switchCameraBtn.getRotation();
        rotation = (rotation + 180) % 360;

        mBinding.switchCameraBtn.animate()
                .rotation(rotation)
                .setDuration(250)
                .start();

        // Fade out current preview
        mTextureView.animate()
                .alpha(0)
                .setDuration(250)
                .start();

        // Select next camera if possible
        if(mCameraInfos != null) {
            mSelectedCameraIdx = (mSelectedCameraIdx + 1) % mCameraInfos.size();
            mSelectedCamera = mCameraInfos.get(mSelectedCameraIdx);
        }

        // Stop the camera in the background then start the new camera
        CompletableFuture
                .runAsync(() -> mNativeCamera.stopCapture())
                .thenRun(() -> runOnUiThread(() -> {
                    mBinding.cameraFrame.removeView(mTextureView);
                    mTextureView = null;

                    if(mSurface != null) {
                        mSurface.release();
                        mSurface = null;
                    }

                    initCamera();
                }));
    }

    private void updatePreviewTabUi(boolean updateModeSelection) {
        final float seekBarMax = mBinding.previewFrame.previewSeekBar.getMax();
        int progress = Math.round(seekBarMax / 2);

        View selectionView = null;

        mBinding.previewFrame.contrastValue.setText(Math.round(mPostProcessSettings.contrast * 100) + "%");
        mBinding.previewFrame.colourValue.setText(Math.round(mPostProcessSettings.saturation / 2.0f * 100)  + "%");
        mBinding.previewFrame.warmthValue.setText( Math.round((mTemperatureOffset + 1000.0f) / 2000.0f * 100.0f) + "%" );
        mBinding.previewFrame.tintValue.setText( Math.round((mTintOffset + 50.0f) / 100.0f * 100.0f) + "%" );

        switch(mPreviewControlMode) {
            case CONTRAST:
                progress = Math.round(mPostProcessSettings.contrast * seekBarMax);
                selectionView = mBinding.previewFrame.contrastBtn;
                break;

            case COLOUR:
                progress = Math.round(mPostProcessSettings.saturation / 2.0f * seekBarMax);
                selectionView = mBinding.previewFrame.colourBtn;
                break;

            case TINT:
                progress = Math.round(((mTintOffset + 50.0f) / 100.0f * seekBarMax));
                selectionView = mBinding.previewFrame.tintBtn;
                break;

            case WARMTH:
                progress = Math.round(((mTemperatureOffset + 1000.0f) / 2000.0f * seekBarMax));
                selectionView = mBinding.previewFrame.warmthBtn;
                break;
        }

        if(updateModeSelection) {
            mBinding.previewFrame.contrastBtn.setBackground(null);
            mBinding.previewFrame.colourBtn.setBackground(null);
            mBinding.previewFrame.tintBtn.setBackground(null);
            mBinding.previewFrame.warmthBtn.setBackground(null);

            selectionView.setBackgroundColor(getColor(R.color.colorPrimaryDark));
            mBinding.previewFrame.previewSeekBar.setProgress(progress);
        }
    }

    private void updateCaptureModeUi() {
        mBinding.hdrModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.zslModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.burstModeBtn.setTextColor(getColor(R.color.textColor));

        switch(mCaptureMode) {
            case HDR:
                mBinding.hdrModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;

            case ZSL:
                mBinding.zslModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;

            case BURST:
                mBinding.burstModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;
        }
    }

    private void onCaptureModeClicked(View v) {
        if(v == mBinding.hdrModeBtn) {
            mCaptureMode = CaptureMode.HDR;
        }
        else if(v == mBinding.zslModeBtn) {
            mCaptureMode = CaptureMode.ZSL;
        }
        else if(v == mBinding.burstModeBtn) {
            mCaptureMode = CaptureMode.BURST;
        }

        updateCaptureModeUi();
    }

    private void onPreviewModeClicked(View v) {
        if(v == mBinding.previewFrame.contrastBtn) {
            mPreviewControlMode = PreviewControlMode.CONTRAST;
        }
        else if(v == mBinding.previewFrame.colourBtn) {
            mPreviewControlMode = PreviewControlMode.COLOUR;
        }
        else if(v == mBinding.previewFrame.tintBtn) {
            mPreviewControlMode = PreviewControlMode.TINT;
        }
        else if(v == mBinding.previewFrame.warmthBtn) {
            mPreviewControlMode = PreviewControlMode.WARMTH;
        }

        updatePreviewTabUi(true);
    }

    private void updatePreviewControlsParam(int progress) {
        final float seekBarMax = mBinding.previewFrame.previewSeekBar.getMax();

        switch(mPreviewControlMode) {
            case CONTRAST:
                mPostProcessSettings.contrast = progress / seekBarMax;
                break;

            case COLOUR:
                mPostProcessSettings.saturation = (progress / seekBarMax) * 2.0f;
                break;

            case TINT:
                mTintOffset = (progress / seekBarMax - 0.5f) * 100.0f;
                break;

            case WARMTH:
                mTemperatureOffset = (progress / seekBarMax - 0.5f) * 2000.0f;
                break;
        }

        updatePreviewSettings();
        updatePreviewTabUi(false);
    }

    private void onCaptureClicked() {
        mPostProcessSettings.shadows = calculateShadows();

        if(mCaptureMode == CaptureMode.BURST) {
            // Pass native camera handle
            Intent intent = new Intent(this, PostProcessActivity.class);

            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_HANDLE, mNativeCamera.getHandle());
            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_ID, mSelectedCamera.cameraId);
            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_FRONT_FACING, mSelectedCamera.isFrontFacing);

            startActivity(intent);
        }
        else if(mCaptureMode == CaptureMode.ZSL) {
            mBinding.captureBtn.setEnabled(false);

            mBinding.cameraFrame.setAlpha(0.25f);
            mBinding.cameraFrame
                    .animate()
                    .alpha(1.0f)
                    .setDuration(250)
                    .start();

            PostProcessSettings settings = mPostProcessSettings.clone();
            DenoiseSettings denoiseSettings = new DenoiseSettings(mIso, mExposureTime, settings.shadows);

            settings.chromaEps = denoiseSettings.chromaEps;
            settings.spatialDenoiseAggressiveness = denoiseSettings.spatialWeight;

            Log.i(TAG, "Requested ZSL capture (denoiseSettings=" + denoiseSettings.toString() + ")");

            mAsyncNativeCameraOps.captureImage(
                    Long.MIN_VALUE,
                    denoiseSettings.numMergeImages,
                    false,
                    settings,
                    CameraProfile.generateCaptureFile(this).getPath(),
                    handle -> {
                        Log.i(TAG, "Image captured");

                        mBinding.captureBtn.setEnabled(true);
                        startImageProcessor();
                    }
            );
        }
        else if(mCaptureMode == CaptureMode.HDR) {
            mBinding.captureBtn.setEnabled(false);

            mBinding.cameraFrame
                    .animate()
                    .alpha(0.25f)
                    .setDuration(250)
                    .start();

            mAsyncNativeCameraOps.estimateSettings(true, estimatedSettings ->
                {
                    if(estimatedSettings == null || mNativeCamera == null)
                        return;

                    PostProcessSettings settings = mPostProcessSettings.clone();

                    // Map camera exposure to our own
                    long cameraExposure = mExposureTime;

                    // If the ISO is very high, use our estimated exposure compensation to boost the shutter speed
                    if(!mManualControlsSet && mIso >= 1600) {
                        cameraExposure = Math.round(mExposureTime * Math.pow(2.0f, estimatedSettings.exposure));

                        // Reduce shadows to account for the increase in exposure
                        settings.shadows = settings.shadows / (float) Math.pow(2.0f, estimatedSettings.exposure);
                    }

                    CameraManualControl.Exposure baseExposure = CameraManualControl.Exposure.Create(
                            CameraManualControl.GetClosestShutterSpeed(cameraExposure),
                            CameraManualControl.GetClosestIso(mIsoValues, mIso)
                    );

                    CameraManualControl.Exposure hdrExposure = CameraManualControl.Exposure.Create(
                            CameraManualControl.GetClosestShutterSpeed(cameraExposure / HDR_UNDEREXPOSED_SHUTTER_SPEED_DIV),
                            CameraManualControl.GetClosestIso(mIsoValues, mIso)
                    );

                    DenoiseSettings denoiseSettings = new DenoiseSettings(baseExposure.iso.getIso(), baseExposure.shutterSpeed.getExposureTime(), settings.shadows);

                    Log.i(TAG, "Requested HDR capture (denoiseSettings=" + denoiseSettings.toString() + ")");

                    // If the user has not override the shutter speed/iso, pick our own
                    if(!mManualControlsSet) {
                        baseExposure = CameraManualControl.MapToExposureLine(1.0, baseExposure);
                        hdrExposure = CameraManualControl.MapToExposureLine(1.0, hdrExposure);
                    }

                    settings.chromaEps = denoiseSettings.chromaEps;
                    settings.spatialDenoiseAggressiveness = denoiseSettings.spatialWeight;
                    settings.exposure = 0.0f;
                    settings.temperature = estimatedSettings.temperature + mTemperatureOffset;
                    settings.tint = estimatedSettings.tint + mTintOffset;

                    mNativeCamera.captureHdrImage(
                            denoiseSettings.numMergeImages,
                            baseExposure.iso.getIso(),
                            baseExposure.shutterSpeed.getExposureTime(),
                            hdrExposure.iso.getIso(),
                            hdrExposure.shutterSpeed.getExposureTime(),
                            settings,
                            CameraProfile.generateCaptureFile(this).getPath());
                }
            );
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);

        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE           |
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION  |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN       |
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION         |
                    View.SYSTEM_UI_FLAG_FULLSCREEN              |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    private void requestPermissions() {
        String[] requestPermissions = { Manifest.permission.CAMERA, Manifest.permission.WRITE_EXTERNAL_STORAGE };
        ArrayList<String> needPermissions = new ArrayList<>();

        for(String permission : requestPermissions) {
            if (ActivityCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                needPermissions.add(permission);
            }
        }

        if(!needPermissions.isEmpty()) {
            String[] permissions = needPermissions.toArray(new String[0]);
            ActivityCompat.requestPermissions(this, permissions, PERMISSION_REQUEST_CODE);
        }
        else {
            onPermissionsGranted();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (PERMISSION_REQUEST_CODE != requestCode) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
            return;
        }

        boolean grantedAll = true;

        for(int hasGranted : grantResults) {
            grantedAll = grantedAll & (hasGranted == PackageManager.PERMISSION_GRANTED);
        }

        if(grantedAll) {
            runOnUiThread(this::onPermissionsGranted);
        }
        else {
            runOnUiThread(this::onPermissionsDenied);
        }
    }

    private void onPermissionsGranted() {
        mHavePermissions = true;

        // Kick off image processor in case there are images we have not processed
        startImageProcessor();
    }

    private void onPermissionsDenied() {
        mHavePermissions = false;
        finish();
    }

    private void initCamera() {
        // Get how much memory to allow the native camera
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        // Make sure we don't exceed our maximum memory use
        long nativeCameraMemoryUseMb = sharedPrefs.getInt(SettingsViewModel.PREFS_KEY_MEMORY_USE_MBYTES, 256);
        nativeCameraMemoryUseMb = Math.min(nativeCameraMemoryUseMb, SettingsViewModel.MAXIMUM_MEMORY_USE_MB);

        boolean enableRawPreview = sharedPrefs.getBoolean(SettingsViewModel.PREFS_KEY_DUAL_EXPOSURE_CONTROLS, false);

        // Create camera bridge
        long nativeCameraMemoryUseBytes = nativeCameraMemoryUseMb * 1024 * 1024;

        if (mNativeCamera == null) {
            // Load our native camera library
            if(enableRawPreview) {
                try {
                    System.loadLibrary("native-camera-opencl");
                } catch (Exception e) {
                    e.printStackTrace();
                    System.loadLibrary("native-camera-host");
                }
            }
            else {
                System.loadLibrary("native-camera-host");
            }

            mNativeCamera = new NativeCameraSessionBridge(this, nativeCameraMemoryUseBytes, null);
            mAsyncNativeCameraOps = new AsyncNativeCameraOps(mNativeCamera);

            // Get supported cameras and filter out ignored ones
            NativeCameraInfo[] cameraInfos = mNativeCamera.getSupportedCameras();
//            Set<String> ignoreCameraIds = sharedPrefs.getStringSet(SettingsViewModel.PREFS_KEY_IGNORE_CAMERA_IDS, new HashSet<>());
//            if(ignoreCameraIds == null)
//                ignoreCameraIds = new HashSet<>();

            mCameraInfos = new ArrayList<>();
            for(NativeCameraInfo cameraInfo : cameraInfos) {
//                if(ignoreCameraIds.contains(cameraInfo.cameraId))
//                    continue;

                mCameraInfos.add(cameraInfo);
            }

            if(mCameraInfos.size() == 0) {
                mNativeCamera.destroy();
                mNativeCamera = null;

                // No supported cameras. Display message to user and exist
                AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                        .setCancelable(false)
                        .setTitle(R.string.error)
                        .setMessage(R.string.not_supported_error)
                        .setPositiveButton(R.string.ok, (dialog, which) -> finish());

                dialogBuilder.show();
                return;
            }
            else {
                mSelectedCameraIdx = Math.min(mSelectedCameraIdx, mCameraInfos.size() - 1);
                mSelectedCamera = mCameraInfos.get(mSelectedCameraIdx);
            }
        }

        // Exposure compensation frame
        findViewById(R.id.exposureCompFrame).setVisibility(View.VISIBLE);

        // Set up camera manual controls
        mCameraMetadata = mNativeCamera.getMetadata(mSelectedCamera);

        // Keep range of valid ISO/shutter speeds
        mIsoValues = CameraManualControl.GetIsoValuesInRange(mCameraMetadata.isoMin, mCameraMetadata.isoMax);

        ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).setMax(mIsoValues.size() - 1);

        mExposureValues = CameraManualControl.GetExposureValuesInRange(
                mCameraMetadata.exposureTimeMin,
                Math.min(MAX_EXPOSURE_TIME.getExposureTime(), mCameraMetadata.exposureTimeMax));

        ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).setMax(mExposureValues.size() - 1);

        if(mCameraInfos.size() < 2)
            mBinding.switchCameraBtn.setVisibility(View.GONE);
        else
            mBinding.switchCameraBtn.setVisibility(View.VISIBLE);

        // Create texture view for camera preview
        mTextureView = new TextureView(this);
        mBinding.cameraFrame.addView(
                mTextureView,
                0,
                new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        mTextureView.setSurfaceTextureListener(this);
        mTextureView.setOnTouchListener(this);

        if (mTextureView.isAvailable()) {
            onSurfaceTextureAvailable(
                    mTextureView.getSurfaceTexture(),
                    mTextureView.getWidth(),
                    mTextureView.getHeight());
        }
    }

    /**
     * configureTransform()
     * Courtesy to https://github.com/google/cameraview/blob/master/library/src/main/api14/com/google/android/cameraview/TextureViewPreview.java#L108
     */
    private void configureTransform(int textureWidth, int textureHeight, Size previewOutputSize) {
        int displayOrientation = getWindowManager().getDefaultDisplay().getRotation() * 90;

        int width = textureWidth;
        int height = textureWidth * previewOutputSize.getWidth() / previewOutputSize.getHeight();

        if (Surface.ROTATION_90 == displayOrientation || Surface.ROTATION_270 == displayOrientation) {
            height = (textureWidth * previewOutputSize.getHeight()) / previewOutputSize.getWidth();
        }

        Matrix cameraMatrix = new Matrix();

        if (displayOrientation % 180 == 90) {
            // Rotate the camera preview when the screen is landscape.
            cameraMatrix.setPolyToPoly(
                    new float[]{
                            0.f, 0.f, // top left
                            width, 0.f, // top right
                            0.f, height, // bottom left
                            width, height, // bottom right
                    }, 0,
                    displayOrientation == 90 ?
                            // Clockwise
                            new float[]{
                                    0.f, height, // top left
                                    0.f, 0.f,    // top right
                                    width, height, // bottom left
                                    width, 0.f, // bottom right
                            } : // mDisplayOrientation == 270
                            // Counter-clockwise
                            new float[]{
                                    width, 0.f, // top left
                                    width, height, // top right
                                    0.f, 0.f, // bottom left
                                    0.f, height, // bottom right
                            }, 0,
                    4);
        }
        else if (displayOrientation == 180) {
            cameraMatrix.postRotate(180, width / 2.0f, height / 2.0f);
        }

        if(mSelectedCamera.isFrontFacing)
            cameraMatrix.preScale(1, -1, width / 2.0f, height / 2.0f);

        mTextureView.setTransform(cameraMatrix);
    }

    private int getCameraPreviewQuality(SharedPreferences sharedPrefs) {
        // Get preview quality
        int cameraPreviewQuality = sharedPrefs.getInt(SettingsViewModel.PREFS_KEY_CAMERA_PREVIEW_QUALITY, 0);

        switch(cameraPreviewQuality) {
            default:
            case 0:
                return 4;
            case 1:
                return 3;
            case 2:
                return 2;
        }
    }

    private CaptureMode getCaptureMode(SharedPreferences sharedPrefs) {
        // Always in burst mode if raw preview is disabled
        if(!sharedPrefs.getBoolean(SettingsViewModel.PREFS_KEY_DUAL_EXPOSURE_CONTROLS, false))
            return CaptureMode.BURST;

        return CaptureMode.valueOf(
                sharedPrefs.getString(SettingsViewModel.PREFS_KEY_UI_CAPTURE_MODE, CaptureMode.HDR.name()));
    }

    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture surfaceTexture, int width, int height) {
        Log.d(TAG, "onSurfaceTextureAvailable() w: " + width + " h: " + height);

        if(mNativeCamera == null || mSelectedCamera == null) {
            Log.e(TAG, "Native camera not available");
            return;
        }

        if(mSurface != null) {
            Log.w(TAG, "Surface still exists, releasing");
            mSurface.release();
            mSurface = null;
        }

        startCamera(surfaceTexture, width, height);
    }

    private void startCamera(SurfaceTexture surfaceTexture, int width, int height) {
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        boolean enableRawPreview = sharedPrefs.getBoolean(SettingsViewModel.PREFS_KEY_DUAL_EXPOSURE_CONTROLS, false);
        int displayWidth;
        int displayHeight;

        if(enableRawPreview) {
            // Use small preview window since we're not using the camera preview.
            displayWidth = 240;
            displayHeight = 480;
        }
        else {
            // Get display size
            Display display = getWindowManager().getDefaultDisplay();

            displayWidth = display.getMode().getPhysicalWidth();
            displayHeight = display.getMode().getPhysicalHeight();
        }

        // Get capture size so we can figure out the correct aspect ratio
        Size captureOutputSize = mNativeCamera.getRawConfigurationOutput(mSelectedCamera);

        // If we couldn't find any RAW outputs, this camera doesn't actually support RAW10
        if(captureOutputSize == null) {
            displayUnsupportedCameraError();
            return;
        }

        Size previewOutputSize =
                mNativeCamera.getPreviewConfigurationOutput(mSelectedCamera, captureOutputSize, new Size(displayWidth, displayHeight));
        surfaceTexture.setDefaultBufferSize(previewOutputSize.getWidth(), previewOutputSize.getHeight());

        configureTransform(width, height, previewOutputSize);

        mSurface = new Surface(surfaceTexture);
        mNativeCamera.startCapture(mSelectedCamera, mSurface, enableRawPreview);

        // Update orientation in case we've switched front/back cameras
        NativeCameraBuffer.ScreenOrientation orientation = mSensorEventManager.getOrientation();
        if(orientation != null)
            onOrientationChanged(orientation);

        // Schedule timer to update shadows
        if(enableRawPreview) {
            mBinding.previewFrame.previewControls.setVisibility(View.VISIBLE);
            mBinding.rawCameraPreview.setVisibility(View.VISIBLE);
            mBinding.shadowsLayout.setVisibility(View.VISIBLE);

            mTextureView.setAlpha(0);

            mNativeCamera.enableRawPreview(this, getCameraPreviewQuality(sharedPrefs), false);
        }
        else {
            mBinding.previewFrame.previewControls.setVisibility(View.GONE);
            mBinding.rawCameraPreview.setVisibility(View.GONE);
            mBinding.shadowsLayout.setVisibility(View.GONE);

            mTextureView.setAlpha(1);
        }

        mBinding.previewFrame.previewAdjustmentsBtn.setOnClickListener(v -> {
            mBinding.previewFrame.previewAdjustmentsBtn.setVisibility(View.GONE);
            mBinding.previewFrame.previewAdjustments.setVisibility(View.VISIBLE);
        });

        mBinding.previewFrame.previewAdjustments.findViewById(R.id.closePreviewAdjustmentsBtn).setOnClickListener(v -> {
            mBinding.previewFrame.previewAdjustmentsBtn.setVisibility(View.VISIBLE);
            mBinding.previewFrame.previewAdjustments.setVisibility(View.GONE);
        });

        mShadowsUpdateTimer = new Timer("ShadowsUpdateTimer");

        mShadowUpdateTimerTask = new ShadowTimerTask();
        mShadowsUpdateTimer.scheduleAtFixedRate(mShadowUpdateTimerTask, 0, SHADOW_UPDATE_FREQUENCY_MS);
    }

    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
        Log.d(TAG, "onSurfaceTextureSizeChanged() w: " + width + " h: " + height);
    }

    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
        Log.d(TAG, "onSurfaceTextureDestroyed()");

        // Release camera
        if(mNativeCamera != null) {
            mNativeCamera.disableRawPreview();
            mNativeCamera.stopCapture();
        }

        if(mSurface != null) {
            mSurface.release();
            mSurface = null;
        }

        return true;
    }

    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture surface) {
    }

    @Override
    public void onCameraDisconnected() {
        Log.i(TAG, "Camera has disconnected");
    }

    private void displayUnsupportedCameraError() {
//        // Add camera id to ignore list
//        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
//        Set<String> ignoreCameraIds = sharedPrefs.getStringSet(SettingsViewModel.PREFS_KEY_IGNORE_CAMERA_IDS, new HashSet<>());
//        if(ignoreCameraIds == null)
//            ignoreCameraIds = new HashSet<>();
//
//        HashSet<String> updatedIgnoreCameraIds = new HashSet<>(ignoreCameraIds);
//
//        updatedIgnoreCameraIds.add(mSelectedCamera.cameraId);
//
//        sharedPrefs.edit()
//                .putStringSet(SettingsViewModel.PREFS_KEY_IGNORE_CAMERA_IDS, updatedIgnoreCameraIds)
//                .apply();

        AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                .setCancelable(false)
                .setTitle(R.string.error)
                .setMessage(R.string.camera_error)
                .setPositiveButton(R.string.ok, (dialog, which) -> finish());

        dialogBuilder.show();
    }

    @Override
    public void onCameraError(int error) {
        Log.e(TAG, "Camera has failed");

        runOnUiThread(this::displayUnsupportedCameraError);
    }

    @Override
    public void onCameraSessionStateChanged(NativeCameraSessionBridge.CameraState cameraState) {
        Log.i(TAG, "Camera state changed " + cameraState.name());

        if(cameraState == NativeCameraSessionBridge.CameraState.ACTIVE) {
            runOnUiThread(() ->
            {
                mBinding.switchCameraBtn.setEnabled(true);

                setPostProcessingDefaults();
            });
        }
    }

    @Override
    public void onCameraExposureStatus(int iso, long exposureTime) {
//        Log.i(TAG, "ISO: " + iso + " Exposure Time: " + exposureTime/(1000.0*1000.0));

        CameraManualControl.ISO cameraIso = CameraManualControl.GetClosestIso(mIsoValues, iso);
        CameraManualControl.SHUTTER_SPEED cameraShutterSpeed = CameraManualControl.GetClosestShutterSpeed(exposureTime);

        runOnUiThread(() -> {
            if(!mManualControlsSet) {
                ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).setProgress(cameraIso.ordinal());
                ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).setProgress(cameraShutterSpeed.ordinal());

                ((TextView) findViewById(R.id.manualControlIsoText)).setText(cameraIso.toString());
                ((TextView) findViewById(R.id.manualControlShutterSpeedText)).setText(cameraShutterSpeed.toString());
            }

            ((TextView) findViewById(R.id.infoIsoText)).setText(cameraIso.toString());
            ((TextView) findViewById(R.id.infoShutterSpeedText)).setText(cameraShutterSpeed.toString());

            mIso = iso;
            mExposureTime = exposureTime;
        });
    }

    @Override
    public void onCameraAutoFocusStateChanged(NativeCameraSessionBridge.CameraFocusState state) {
        Log.i(TAG, "Focus state: " + state.name());
    }

    private void startImageProcessor() {
        // Start service to process the image
        Intent intent = new Intent(this, ProcessorService.class);

        intent.putExtra(ProcessorService.METADATA_PATH_KEY, CameraProfile.getRootOutputPath(this).getPath());
        intent.putExtra(ProcessorService.RECEIVER_KEY, mProgressReceiver);

        Objects.requireNonNull(startService(intent));
    }

    @Override
    public void onCameraAutoExposureStateChanged(NativeCameraSessionBridge.CameraExposureState state) {
        Log.i(TAG, "Exposure state: " + state.name());
        runOnUiThread(() -> setAutoExposureState(state));
    }

    @Override
    public void onCameraHdrImageCaptureProgress(int progress) {
        runOnUiThread( () -> {
            mBinding.hdrProgressBar.setVisibility(View.VISIBLE);
            mBinding.hdrProgressBar.setProgress(progress);
        });
    }

    @Override
    public void onCameraHdrImageCaptureFailed() {
        Log.i(TAG, "HDR capture failed");

        runOnUiThread( () ->
        {
            mBinding.captureBtn.setEnabled(true);
            mBinding.hdrProgressBar.setVisibility(View.INVISIBLE);

            mBinding.cameraFrame
                    .animate()
                    .alpha(1.0f)
                    .setDuration(250)
                    .start();

                // Tell user we didn't capture image
                AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(this, R.style.BasicDialog)
                        .setCancelable(false)
                        .setTitle(R.string.error)
                        .setMessage(R.string.capture_failed)
                        .setPositiveButton(R.string.ok, (dialog, which) -> {});

                dialogBuilder.show();
        });
    }

    @Override
    public void onCameraHdrImageCaptureCompleted() {
        Log.i(TAG, "HDR capture completed");

        runOnUiThread( () ->
        {
            mBinding.captureBtn.setEnabled(true);
            mBinding.hdrProgressBar.setVisibility(View.INVISIBLE);

            mBinding.cameraFrame
                    .animate()
                    .alpha(1.0f)
                    .setDuration(250)
                    .start();

            startImageProcessor();
        });
    }

    @Override
    public void onRawPreviewCreated(Bitmap bitmap) {
        runOnUiThread(() -> ((BitmapDrawView) findViewById(R.id.rawCameraPreview)).setBitmap(bitmap));
    }

    @Override
    public void onRawPreviewUpdated() {
        runOnUiThread(() -> findViewById(R.id.rawCameraPreview).invalidate());
    }

    private void updateManualControlView(NativeCameraBuffer.ScreenOrientation orientation) {
        mBinding.manualControlsFrame.setAlpha(0.0f);
        mBinding.manualControlsFrame.post(() ->  {
            final int rotation;
            final int translationX;
            final int translationY;

            // Update position of manual controls
            if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT) {
                rotation = 180;
                translationX = 0;
                translationY = 0;
            }
            else if(orientation == NativeCameraBuffer.ScreenOrientation.LANDSCAPE) {
                rotation = 90;
                translationX = -mBinding.cameraFrame.getWidth() / 2 + mBinding.manualControlsFrame.getHeight() / 2;
                translationY = mBinding.cameraFrame.getHeight() / 2 - mBinding.manualControlsFrame.getHeight() / 2;
            }
            else if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_LANDSCAPE) {
                rotation = -90;
                translationX = mBinding.cameraFrame.getWidth() / 2 - mBinding.manualControlsFrame.getHeight() / 2;
                translationY = mBinding.cameraFrame.getHeight() / 2 - mBinding.manualControlsFrame.getHeight() / 2;
            }
            else {
                // Portrait
                rotation = 0;
                translationX = 0;
                translationY = mBinding.cameraFrame.getHeight() - mBinding.manualControlsFrame.getHeight();
            }

            mBinding.manualControlsFrame.setRotation(rotation);
            mBinding.manualControlsFrame.setTranslationX(translationX);
            mBinding.manualControlsFrame.setTranslationY(translationY);

            mBinding.manualControlsFrame
                    .animate()
                    .setDuration(500)
                    .alpha(1.0f)
                    .start();
        });
    }

    @Override
    public void onOrientationChanged(NativeCameraBuffer.ScreenOrientation orientation) {
        Log.i(TAG, "Orientation is " + orientation);

        if(mNativeCamera != null) {
            if(mSelectedCamera.isFrontFacing) {
                if(orientation == NativeCameraBuffer.ScreenOrientation.PORTRAIT)
                    mNativeCamera.updateOrientation(NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT);
                else if(orientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT)
                    mNativeCamera.updateOrientation(NativeCameraBuffer.ScreenOrientation.PORTRAIT);
            }
            else
                mNativeCamera.updateOrientation(orientation);
        }

        updateManualControlView(orientation);

        final int duration = 500;

        mBinding.switchCameraBtn
                .animate()
                .rotation(orientation.angle)
                .setDuration(duration)
                .start();

        mBinding.settingsBtn
                .animate()
                .rotation(orientation.angle)
                .setDuration(duration)
                .start();
    }

    private void onShadowsSeekBarChanged(int progress) {
        mShadowOffset = 6.0f * ((progress - 50.0f) / 100.0f);
    }

    private void setShadowValue(float value) {
        mShadowEstimated = value;

        updatePreviewSettings();
    }

    private float calculateShadows() {
        return (float) Math.min(32.0f, Math.pow(2.0, Math.log(mShadowEstimated) / Math.log(2.0) + mShadowOffset));
    }

    private void updatePreviewSettings() {
        if(mPostProcessSettings != null && mNativeCamera != null) {
            float shadows = calculateShadows();

            mNativeCamera.setRawPreviewSettings(
                    shadows,
                    mPostProcessSettings.contrast,
                    mPostProcessSettings.saturation,
                    0.03f,
                    1.0f,
                    mTemperatureOffset,
                    mTintOffset);
        }
    }

    private void setAutoExposureState(NativeCameraSessionBridge.CameraExposureState state) {
    }

    private void setFocusState(FocusState state, PointF focusPt) {
        mFocusState = state;

        if(state == FocusState.FIXED) {
            mAutoExposurePoint = focusPt;
            mAutoFocusPoint = focusPt;

            mBinding.focusLockPointFrame.setVisibility(View.VISIBLE);
            mBinding.exposureLockPointFrame.setVisibility(View.INVISIBLE);

            mNativeCamera.setFocusPoint(mAutoFocusPoint, mAutoExposurePoint);
        }
//        else if(state == FocusState.FIXED_AF_AE) {
//            if(mAutoFocusPoint == null)
//                mAutoFocusPoint = focusPt;
//
//            mAutoExposurePoint = focusPt;
//
//            mBinding.focusLockPointFrame.setVisibility(View.VISIBLE);
//            mBinding.exposureLockPointFrame.setVisibility(View.VISIBLE);
//
//            mNativeCamera.setFocusPoint(mAutoFocusPoint, mAutoExposurePoint);
//        }
        else if(state == FocusState.AUTO) {
            mBinding.focusLockPointFrame.setVisibility(View.INVISIBLE);
            mBinding.exposureLockPointFrame.setVisibility(View.INVISIBLE);

            mNativeCamera.setAutoFocus();
        }
    }

    private void onSetFocusPt(float touchX, float touchY) {
        if(mNativeCamera == null)
            return;

        // If settings AF regions is not supported, do nothing
        if(mCameraMetadata.maxAfRegions <= 0)
            return;

        float x = touchX / mTextureView.getWidth();
        float y = touchY / mTextureView.getHeight();

        // Ignore edges
        if (x < 0.05 || x > 0.95 ||
            y < 0.05 || y > 0.95)
        {
            return;
        }

        Matrix m = new Matrix();
        float[] pts = new float[] { x, y };

        m.setRotate(-mCameraMetadata.sensorOrientation, 0.5f, 0.5f);
        m.mapPoints(pts);

        PointF pt = new PointF(pts[0], pts[1]);

        if(mFocusState == FocusState.AUTO) {
            FrameLayout.LayoutParams layoutParams =
                    (FrameLayout.LayoutParams) mBinding.focusLockPointFrame.getLayoutParams();

            layoutParams.setMargins(
                    Math.round(touchX) - mBinding.focusLockPointFrame.getWidth() / 2,
                    Math.round(touchY) - mBinding.focusLockPointFrame.getHeight() / 2,
                    0,
                    0);

            mBinding.focusLockPointFrame.setLayoutParams(layoutParams);

            setFocusState(FocusState.FIXED, pt);
        }
//        else if(mFocusState == FocusState.FIXED) {
//            // If the camera does not support setting AE regions or manual controls are set, go back to auto
//            if(mCameraMetadata.maxAeRegions <= 0 || mManualControlsSet) {
//                setFocusState(FocusState.AUTO, null);
//            }
//            else {
//                FrameLayout.LayoutParams layoutParams =
//                        (FrameLayout.LayoutParams) mBinding.exposureLockPointFrame.getLayoutParams();
//
//                layoutParams.setMargins(
//                        Math.round(touchX) - mBinding.exposureLockPointFrame.getWidth() / 2,
//                        Math.round(touchY) - mBinding.exposureLockPointFrame.getHeight() / 2,
//                        0,
//                        0);
//
//                mBinding.exposureLockPointFrame.setLayoutParams(layoutParams);
//
//                setFocusState(FocusState.FIXED_AF_AE, pt);
//            }
//        }
        else {
            setFocusState(FocusState.AUTO, null);
        }
    }

    @Override
    public boolean onTouch(View v, MotionEvent event) {
        if(v == mTextureView) {
            if(event.getAction() == MotionEvent.ACTION_UP) {
                onSetFocusPt(event.getX(), event.getY());
            }
        }

        return true;
    }

    @Override
    public void onProcessingStarted() {

    }

    @Override
    public void onProcessingProgress(int progress) {

    }

    @Override
    public void onProcessingCompleted() {
    }
}
