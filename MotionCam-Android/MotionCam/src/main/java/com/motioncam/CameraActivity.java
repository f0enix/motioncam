package com.motioncam;

import android.Manifest;
import android.animation.ObjectAnimator;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.graphics.PointF;
import android.graphics.SurfaceTexture;
import android.net.Uri;
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
import android.view.animation.DecelerateInterpolator;
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

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
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
    private static final float MAX_SHADOWS_VALUE = 32.0f;

    private enum FocusState {
        AUTO,
        FIXED,
        FIXED_AF_AE
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

    private PostProcessSettings mPostProcessSettings;
    private AsyncNativeCameraOps mAsyncNativeCameraOps;
    private ObjectAnimator mShadowsAnimator;

    private boolean mManualControlsEnabled;
    private boolean mManualControlsSet;

    private FocusState mFocusState = FocusState.AUTO;
    private PointF mAutoFocusPoint;
    private PointF mAutoExposurePoint;
    private int mIso;
    private long mExposureTime;
    private long mShadowsChangedTimeMs;

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
        mBinding.contrastSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if(mPostProcessSettings != null) {
                    mPostProcessSettings.contrast = progress / 100.0f;
                    updatePreviewSettings();
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });

        mBinding.colourSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if(mPostProcessSettings != null) {
                    mPostProcessSettings.saturation = progress / 100.0f;
                    updatePreviewSettings();
                }
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

        ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).setOnSeekBarChangeListener(mManualControlsSeekBar);
        ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).setOnSeekBarChangeListener(mManualControlsSeekBar);

        ((Switch) findViewById(R.id.manualControlSwitch)).setOnCheckedChangeListener((buttonView, isChecked) -> onCameraManualControlEnabled(isChecked));

        mSensorEventManager = new SensorEventManager(this, this);

        requestPermissions();
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
            mBinding.exposureSeekBar.setVisibility(View.GONE);
        }
        else {
            findViewById(R.id.cameraManualControlFrame).setVisibility(View.GONE);
            findViewById(R.id.infoFrame).setVisibility(View.VISIBLE);
            mBinding.exposureSeekBar.setVisibility(View.VISIBLE);

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
        int contrast = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, 50);
        int colour = prefs.getInt(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, 50);
        boolean burst = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_PREVIEW_BURST, false);

        mPostProcessSettings.shadows = 1.0f;
        mPostProcessSettings.contrast = 0.5f;
        mPostProcessSettings.saturation = 1.0f;
        mPostProcessSettings.greenSaturation = 1.0f;
        mPostProcessSettings.blueSaturation = 1.0f;
        mPostProcessSettings.sharpen0 = 3.5f;
        mPostProcessSettings.sharpen1 = 1.4f;
        mPostProcessSettings.whitePoint = 1.0f;
        mPostProcessSettings.blacks = 0.0f;
        mPostProcessSettings.jpegQuality = jpegQuality;

        // Update UI
        mBinding.contrastSeekBar.setProgress(contrast);
        mBinding.colourSeekBar.setProgress(colour);
        mBinding.burstModeSwitch.setChecked(burst);
    }

    @Override
    protected void onResume() {
        super.onResume();

        mSensorEventManager.enable();
        mProgressReceiver.setReceiver(this);

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
        SharedPreferences prefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        prefs
            .edit()
            .putInt(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, mBinding.contrastSeekBar.getProgress())
            .putInt(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, mBinding.colourSeekBar.getProgress())
            .putBoolean(SettingsViewModel.PREFS_KEY_UI_PREVIEW_BURST, mBinding.burstModeSwitch.isChecked())
            .apply();
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

    static private int getNumImagesToMerge(int iso, long exposureTime, float shadows) {
        int numImages;

        if(iso <= 200 && exposureTime <= CameraManualControl.SHUTTER_SPEED.EXPOSURE_1_100.getExposureTime()) {
            numImages = 1;
        }
        else if (iso <= 800) {
            numImages = 3;
        }
        else {
            numImages = 5;
        }

        // If shadows are increased by a significant amount, use more images
        if(shadows >= 7.99) {
            numImages += 2;
        }

        return numImages;
    }

    static private float getChromaEps(int numImages) {
        if(numImages <= 0)
            return 8.0f;
        else if(numImages <= 3)
            return 16.0f;
        else
            return 32.0f;
    }

    private void onCaptureClicked() {
        if(mBinding.burstModeSwitch.isChecked()) {
            // Pass native camera handle
            Intent intent = new Intent(this, PostProcessActivity.class);

            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_HANDLE, mNativeCamera.getHandle());
            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_ID, mSelectedCamera.cameraId);
            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_FRONT_FACING, mSelectedCamera.isFrontFacing);

            startActivity(intent);
        }
        else {
            mBinding.captureBtn.setEnabled(false);

            mBinding.cameraFrame
                    .animate()
                    .alpha(0.25f)
                    .setDuration(250)
                    .start();

            // Capture latest image
            PostProcessSettings settings = mPostProcessSettings.clone();

            // Map camera exposure to our own
            CameraManualControl.Exposure baseExposure = CameraManualControl.Exposure.Create(
                CameraManualControl.GetClosestShutterSpeed(Math.round(mExposureTime)),
                CameraManualControl.GetClosestIso(mIsoValues, mIso)
            );

            CameraManualControl.Exposure hdrExposure = CameraManualControl.Exposure.Create(
                    CameraManualControl.GetClosestShutterSpeed(mExposureTime / 4),
                    CameraManualControl.GetClosestIso(mIsoValues, mIso)
            );

            baseExposure = CameraManualControl.MapToExposureLine(1.0, baseExposure);
            hdrExposure = CameraManualControl.MapToExposureLine(1.0, hdrExposure);

            int numMergeImages =
                    getNumImagesToMerge(baseExposure.iso.getIso(), baseExposure.shutterSpeed.getExposureTime(), mPostProcessSettings.shadows);

            settings.chromaEps = getChromaEps(numMergeImages);
            settings.shadows = settings.shadows;

            Log.i(TAG, "Requested HDR capture (numImages=" + numMergeImages + ")");

            mNativeCamera.captureHdrImage(
                    numMergeImages,
                    baseExposure.iso.getIso(),
                    baseExposure.shutterSpeed.getExposureTime(),
                    hdrExposure.iso.getIso(),
                    hdrExposure.shutterSpeed.getExposureTime(),
                    settings,
                    CameraProfile.generateCaptureFile().getPath());

//            mAsyncNativeCameraOps.captureImage(
//                    Long.MIN_VALUE,
//                    mNumMergeImages,
//                    false,
//                    settings,
//                    CameraProfile.generateCaptureFile().getPath(),
//                    handle -> {
//                        mBinding.captureBtn.setEnabled(true);
//
//                        // Start service to process the image
//                        Intent intent = new Intent(this, ProcessorService.class);
//
//                        intent.putExtra(ProcessorService.METADATA_PATH_KEY, CameraProfile.getRootOutputPath().getPath());
//                        intent.putExtra(ProcessorService.DELETE_AFTER_PROCESSING_KEY, true);
//                        intent.putExtra(ProcessorService.RECEIVER_KEY, mProgressReceiver);
//
//                        Objects.requireNonNull(startService(intent));
//                    }
//            );
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
    }

    private void onPermissionsDenied() {
        mHavePermissions = false;
        finish();
    }

    private void initCamera() {
        // Get how much memory to allow the native camera
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        // Make sure we don't exceed our maximum memory use
        long nativeCameraMemoryUseMb = sharedPrefs.getInt(SettingsViewModel.PREFS_KEY_MEMORY_USE_MBYTES, 512);
        nativeCameraMemoryUseMb = Math.min(nativeCameraMemoryUseMb, SettingsViewModel.MAXIMUM_MEMORY_USE_MB);

        // Create camera bridge
        long nativeCameraMemoryUseBytes = nativeCameraMemoryUseMb * 1024 * 1024;

        if (mNativeCamera == null) {
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
        mTextureView.setAlpha(0);

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

        Matrix cameraMatrix = new Matrix();

        if (mCameraMetadata.sensorOrientation % 180 == 90) {
            if(mCameraMetadata.sensorOrientation == 90) {
                cameraMatrix.setPolyToPoly(
                        new float[]{
                                0.f, 0.f, // top left
                                width, 0.f, // top right
                                0.f, height, // bottom left
                                width, height, // bottom right
                        }, 0,
                        new float[]{
                                width, 0.f, // top left
                                width, height, // top right
                                0.f, 0.f, // bottom left
                                0.f, height, // bottom right
                        }, 0, 4);
            }
            else {
                cameraMatrix.setPolyToPoly(
                        new float[]{
                                0.f, 0.f, // top left
                                width, 0.f, // top right
                                0.f, height, // bottom left
                                width, height, // bottom right
                        }, 0,
                        new float[]{
                                0.f, height, // top left
                                0.f, 0.f,    // top right
                                width, height, // bottom left
                                width, 0.f, // bottom right
                        }, 0, 4);
            }
        }
        else {
            cameraMatrix.postRotate(180, width / 2.0f, height / 2.0f);
        }

        if(mSelectedCamera.isFrontFacing)
            cameraMatrix.preScale(1, -1, width / 2.0f, height / 2.0f);

        mTextureView.setTransform(cameraMatrix);
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

        // Get display size
        Display display = getWindowManager().getDefaultDisplay();

        // Use small preview window since we're not using the camera preview.
        int displayWidth = 240;//display.getMode().getPhysicalWidth();
        int displayHeight = 480;//display.getMode().getPhysicalHeight();

        // Get capture size so we can figure out the correct aspect ratio
        Size captureOutputSize = mNativeCamera.getRawConfigurationOutput(mSelectedCamera);

        // If we couldn't find any RAW outputs, this camera doesn't actually support RAW10
        if(captureOutputSize == null) {
            displayUnsupportedCameraError();
            return;
        }

        Size previewOutputSize = mNativeCamera.getPreviewConfigurationOutput(mSelectedCamera, captureOutputSize, new Size(displayWidth, displayHeight));
        surfaceTexture.setDefaultBufferSize(previewOutputSize.getWidth(), previewOutputSize.getHeight());

//        configureTransform(width, height, previewOutputSize);

        mSurface = new Surface(surfaceTexture);

        mNativeCamera.startCapture(mSelectedCamera, mSurface);
        mNativeCamera.enableRawPreview(this);

        // Update orientation in case we've switched front/back cameras
        NativeCameraBuffer.ScreenOrientation orientation = mSensorEventManager.getOrientation();
        if(orientation != null)
            onOrientationChanged(orientation);
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

            // Start service to process the image
            Intent intent = new Intent(this, ProcessorService.class);

            intent.putExtra(ProcessorService.METADATA_PATH_KEY, CameraProfile.getRootOutputPath().getPath());
            intent.putExtra(ProcessorService.DELETE_AFTER_PROCESSING_KEY, false);
            intent.putExtra(ProcessorService.RECEIVER_KEY, mProgressReceiver);

            Objects.requireNonNull(startService(intent));
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
        float shadows = 1.0f + (progress / 100.0f * MAX_SHADOWS_VALUE);

        setShadowValue(shadows);
        mShadowsChangedTimeMs = System.currentTimeMillis();
    }

    private void setShadowValue(float value) {
        mPostProcessSettings.shadows = value;
        updatePreviewSettings();
    }

    private void updatePreviewSettings() {
        if(mPostProcessSettings != null) {
            mNativeCamera.setRawPreviewSettings(
                    mPostProcessSettings.shadows,
                    mPostProcessSettings.contrast,
                    mPostProcessSettings.saturation,
                    mPostProcessSettings.blacks,
                    mPostProcessSettings.whitePoint);
        }
    }

    private void setAutoExposureState(NativeCameraSessionBridge.CameraExposureState state) {
        // Update shadows based on new exposure
        if(state == NativeCameraSessionBridge.CameraExposureState.CONVERGED) {
            // Don't auto estimate shadows if the user has changed the shadows slider recently
            if(System.currentTimeMillis() - mShadowsChangedTimeMs < 5000)
                return;

            mAsyncNativeCameraOps.estimateSettings(null, true, settings -> {
                if(settings == null)
                    return;

                if(mShadowsAnimator != null)
                    mShadowsAnimator.cancel();

                mShadowsAnimator =
                        ObjectAnimator.ofFloat(CameraActivity.this, "shadowValue", mPostProcessSettings.shadows, settings.shadows);

                mShadowsAnimator.setDuration(750);
                mShadowsAnimator.setInterpolator(new DecelerateInterpolator());
                mShadowsAnimator.setAutoCancel(true);
                mShadowsAnimator.start();

                mPostProcessSettings.blacks = settings.blacks;
                mPostProcessSettings.whitePoint = settings.whitePoint;

                int shadowsProgress = Math.round(settings.shadows / MAX_SHADOWS_VALUE * 100);

                mBinding.shadowsSeekBar.setProgress(shadowsProgress, true);
            });
        }
        else if(state == NativeCameraSessionBridge.CameraExposureState.SEARCHING) {
            if(mShadowsAnimator != null)
                mShadowsAnimator.cancel();
            mShadowsAnimator = null;
        }
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
    public void onProcessingStarted(File file) {

    }

    @Override
    public void onProcessingProgress(File file, int progress) {

    }

    @Override
    public void onProcessingCompleted(File file) {
        Uri uri = Uri.fromFile(file);
        sendBroadcast(new Intent(Intent.ACTION_MEDIA_SCANNER_SCAN_FILE, uri));
    }
}
