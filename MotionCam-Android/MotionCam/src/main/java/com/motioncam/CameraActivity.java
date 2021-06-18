package com.motioncam;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.graphics.PointF;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.graphics.SurfaceTexture;
import android.graphics.drawable.Drawable;
import android.location.Location;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.Size;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.animation.BounceInterpolator;
import android.widget.FrameLayout;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.constraintlayout.motion.widget.MotionLayout;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.viewpager2.widget.ViewPager2;

import com.bumptech.glide.Glide;
import com.google.android.gms.location.FusedLocationProviderClient;
import com.google.android.gms.location.LocationCallback;
import com.google.android.gms.location.LocationRequest;
import com.google.android.gms.location.LocationResult;
import com.google.android.gms.location.LocationServices;
import com.jakewharton.processphoenix.ProcessPhoenix;
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
import com.motioncam.ui.CameraCapturePreviewAdapter;

import org.apache.commons.io.FileUtils;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicBoolean;

public class CameraActivity extends AppCompatActivity implements
        SensorEventManager.SensorEventHandler,
        TextureView.SurfaceTextureListener,
        NativeCameraSessionBridge.CameraSessionListener,
        NativeCameraSessionBridge.CameraRawPreviewListener,
        View.OnTouchListener,
        ProcessorReceiver.Receiver,
        MotionLayout.TransitionListener, AsyncNativeCameraOps.CaptureImageListener {

    public static final String TAG = "MotionCam";

    private static final int PERMISSION_REQUEST_CODE = 1;
    private static final int SETTINGS_ACTIVITY_REQUEST_CODE = 0x10;

    private static final CameraManualControl.SHUTTER_SPEED MAX_EXPOSURE_TIME = CameraManualControl.SHUTTER_SPEED.EXPOSURE_1__0;

    private static final String[] REQUEST_PERMISSIONS = {
            Manifest.permission.CAMERA,
            Manifest.permission.ACCESS_FINE_LOCATION
    };

    private enum FocusState {
        AUTO,
        FIXED,
        FIXED_AF_AE
    }

    private enum CaptureMode {
        NIGHT,
        ZSL,
        BURST
    }

    private enum PreviewControlMode {
        CONTRAST,
        COLOUR,
        TINT,
        WARMTH
    }

    private static class Settings {
        boolean useDualExposure;
        boolean saveDng;
        boolean autoNightMode;
        boolean hdr;
        float contrast;
        float saturation;
        float temperatureOffset;
        float tintOffset;
        float hdrEv;
        int jpegQuality;
        long memoryUseBytes;
        CaptureMode captureMode;
        SettingsViewModel.RawMode rawMode;
        int cameraPreviewQuality;

        void load(SharedPreferences prefs) {
            this.jpegQuality = prefs.getInt(SettingsViewModel.PREFS_KEY_JPEG_QUALITY, CameraProfile.DEFAULT_JPEG_QUALITY);
            this.contrast = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, CameraProfile.DEFAULT_CONTRAST / 100.0f);
            this.saturation = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, 1.0f);
            this.temperatureOffset = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, 0);
            this.tintOffset = prefs.getFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, 0);
            this.saveDng = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_SAVE_RAW, false);
            this.autoNightMode = prefs.getBoolean(SettingsViewModel.PREFS_KEY_AUTO_NIGHT_MODE, true);
            this.hdr = prefs.getBoolean(SettingsViewModel.PREFS_KEY_UI_HDR, true);
            this.hdrEv = (float) Math.pow(2.0f, prefs.getInt(SettingsViewModel.PREFS_KEY_HDR_EV, 4) / 2.0f);

            long nativeCameraMemoryUseMb = prefs.getInt(SettingsViewModel.PREFS_KEY_MEMORY_USE_MBYTES, SettingsViewModel.MINIMUM_MEMORY_USE_MB);
            nativeCameraMemoryUseMb = Math.min(nativeCameraMemoryUseMb, SettingsViewModel.MAXIMUM_MEMORY_USE_MB);

            this.useDualExposure = prefs.getBoolean(SettingsViewModel.PREFS_KEY_DUAL_EXPOSURE_CONTROLS, false);
            this.memoryUseBytes = nativeCameraMemoryUseMb * 1024 * 1024;

            this.captureMode =
                    CaptureMode.valueOf(prefs.getString(SettingsViewModel.PREFS_KEY_UI_CAPTURE_MODE, CaptureMode.ZSL.name()));

            String captureModeStr = prefs.getString(SettingsViewModel.PREFS_KEY_CAPTURE_MODE, SettingsViewModel.RawMode.RAW10.name());
            this.rawMode = SettingsViewModel.RawMode.valueOf(captureModeStr);

            switch (prefs.getInt(SettingsViewModel.PREFS_KEY_CAMERA_PREVIEW_QUALITY, 0)) {
                default:
                case 0: // Low
                    this.cameraPreviewQuality = 4;
                    break;
                case 1: // Medium
                    this.cameraPreviewQuality = 3;
                    break;
                case 2: // High
                    this.cameraPreviewQuality = 2;
                    break;
            }
        }

        void save(SharedPreferences prefs) {
            prefs.edit()
                    .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_CONTRAST, this.contrast)
                    .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_COLOUR, this.saturation)
                    .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TEMPERATURE_OFFSET, this.temperatureOffset)
                    .putFloat(SettingsViewModel.PREFS_KEY_UI_PREVIEW_TINT_OFFSET, this.tintOffset)
                    .putBoolean(SettingsViewModel.PREFS_KEY_UI_SAVE_RAW, this.saveDng)
                    .putBoolean(SettingsViewModel.PREFS_KEY_UI_HDR, this.hdr)
                    .putString(SettingsViewModel.PREFS_KEY_UI_CAPTURE_MODE, this.captureMode.name())
                    .apply();
        }

        @Override
        public String toString() {
            return "Settings{" +
                    "useDualExposure=" + useDualExposure +
                    ", saveDng=" + saveDng +
                    ", hdr=" + hdr +
                    ", autoNightMode=" + autoNightMode +
                    ", contrast=" + contrast +
                    ", saturation=" + saturation +
                    ", temperatureOffset=" + temperatureOffset +
                    ", tintOffset=" + tintOffset +
                    ", hdrEv=" + hdrEv +
                    ", jpegQuality=" + jpegQuality +
                    ", memoryUseBytes=" + memoryUseBytes +
                    ", captureMode=" + captureMode +
                    ", rawMode=" + rawMode +
                    ", cameraPreviewQuality=" + cameraPreviewQuality +
                    '}';
        }
    }

    private Settings mSettings;
    private boolean mHavePermissions;
    private TextureView mTextureView;
    private Surface mSurface;
    private CameraActivityBinding mBinding;
    private List<CameraManualControl.SHUTTER_SPEED> mExposureValues;
    private List<CameraManualControl.ISO> mIsoValues;
    private NativeCameraSessionBridge mNativeCamera;
    private AsyncNativeCameraOps mAsyncNativeCameraOps;
    private List<NativeCameraInfo> mCameraInfos;
    private NativeCameraInfo mSelectedCamera;
    private int mSelectedCameraIdx;
    private NativeCameraMetadata mCameraMetadata;
    private SensorEventManager mSensorEventManager;
    private FusedLocationProviderClient mFusedLocationClient;
    private ProcessorReceiver mProgressReceiver;
    private Location mLastLocation;

    private CameraCapturePreviewAdapter mCameraCapturePreviewAdapter;

    private final ViewPager2.OnPageChangeCallback mCapturedPreviewPagerListener = new ViewPager2.OnPageChangeCallback() {
        @Override
        public void onPageSelected(int position) {
            onCapturedPreviewPageChanged(position);
        }
    };

    private final LocationCallback mLocationCallback = new LocationCallback() {
        public void onLocationResult(LocationResult locationResult) {
            if (locationResult == null) {
                return;
            }

            onReceivedLocation(locationResult.getLastLocation());
        }
    };

    private PostProcessSettings mPostProcessSettings = new PostProcessSettings();

    private float mTemperatureOffset;
    private float mTintOffset;

    private boolean mManualControlsEnabled;
    private boolean mManualControlsSet;
    private CaptureMode mCaptureMode = CaptureMode.NIGHT;
    private PreviewControlMode mPreviewControlMode = PreviewControlMode.CONTRAST;
    private boolean mUserCaptureModeOverride;

    private FocusState mFocusState = FocusState.AUTO;
    private PointF mAutoFocusPoint;
    private PointF mAutoExposurePoint;
    private int mIso;
    private long mExposureTime;
    private float mShadowOffset;
    private AtomicBoolean mImageCaptureInProgress = new AtomicBoolean(false);
    private long mFocusRequestedTimestampMs;

    private final SeekBar.OnSeekBarChangeListener mManualControlsSeekBar = new SeekBar.OnSeekBarChangeListener() {
        @Override
        public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
            if (mManualControlsEnabled && mNativeCamera != null && fromUser) {
                int shutterSpeedIdx = ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).getProgress();
                int isoIdx = ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).getProgress();

                CameraManualControl.SHUTTER_SPEED shutterSpeed = mExposureValues.get(shutterSpeedIdx);
                CameraManualControl.ISO iso = mIsoValues.get(isoIdx);

                mNativeCamera.setManualExposureValues(iso.getIso(), shutterSpeed.getExposureTime());

                ((TextView) findViewById(R.id.manualControlIsoText)).setText(iso.toString());
                ((TextView) findViewById(R.id.manualControlShutterSpeedText)).setText(shutterSpeed.toString());

                mManualControlsSet = true;

                // Don't allow night mode
                if (mCaptureMode == CaptureMode.NIGHT)
                    setCaptureMode(CaptureMode.ZSL);
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
    public void onBackPressed() {
        if (mBinding.main.getCurrentState() == mBinding.main.getEndState()) {
            mBinding.main.transitionToStart();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        onWindowFocusChanged(true);

        // Clear out previous preview files
        File previewDirectory = new File(getFilesDir(), ProcessorService.PREVIEW_PATH);
        try {
            FileUtils.deleteDirectory(previewDirectory);
        } catch (IOException e) {
            e.printStackTrace();
        }

        mBinding = CameraActivityBinding.inflate(getLayoutInflater());
        setContentView(mBinding.getRoot());

        mSettings = new Settings();
        mProgressReceiver = new ProcessorReceiver(new Handler());

        mBinding.focusLockPointFrame.setOnClickListener(v -> onFixedFocusCancelled());
        mBinding.previewFrame.settingsBtn.setOnClickListener(v -> onSettingsClicked());

        mCameraCapturePreviewAdapter = new CameraCapturePreviewAdapter(getApplicationContext());
        mBinding.previewPager.setAdapter(mCameraCapturePreviewAdapter);

        mBinding.shareBtn.setOnClickListener(this::share);
        mBinding.openBtn.setOnClickListener(this::open);

        mBinding.onBackFromPreviewBtn.setOnClickListener(v -> mBinding.main.transitionToStart());
        mBinding.main.setTransitionListener(this);

        mBinding.shadowsSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
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
                onExposureCompSeekBarChanged(progress);
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
                if (fromUser)
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

        mBinding.nightModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.burstModeBtn.setOnClickListener(this::onCaptureModeClicked);
        mBinding.zslModeBtn.setOnClickListener(this::onCaptureModeClicked);

        mBinding.previewFrame.contrastBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.colourBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.tintBtn.setOnClickListener(this::onPreviewModeClicked);
        mBinding.previewFrame.warmthBtn.setOnClickListener(this::onPreviewModeClicked);

        ((SeekBar) findViewById(R.id.manualControlIsoSeekBar)).setOnSeekBarChangeListener(mManualControlsSeekBar);
        ((SeekBar) findViewById(R.id.manualControlShutterSpeedSeekBar)).setOnSeekBarChangeListener(mManualControlsSeekBar);

        ((Switch) findViewById(R.id.manualControlSwitch)).setOnCheckedChangeListener((buttonView, isChecked) -> onCameraManualControlEnabled(isChecked));

        mSensorEventManager = new SensorEventManager(this, this);
        mFusedLocationClient = LocationServices.getFusedLocationProviderClient(this);

        requestPermissions();
    }

    private void open(View view) {
        Uri uri = mCameraCapturePreviewAdapter.getOutput(mBinding.previewPager.getCurrentItem());
        if (uri == null)
            return;

        Intent openIntent = new Intent();

        openIntent.setAction(Intent.ACTION_VIEW);
        openIntent.setDataAndType(uri, "image/jpeg");
        openIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_ACTIVITY_NO_HISTORY);

        startActivity(openIntent);
    }

    private void share(View view) {
        Uri uri = mCameraCapturePreviewAdapter.getOutput(mBinding.previewPager.getCurrentItem());
        if (uri == null)
            return;

        Intent shareIntent = new Intent();

        shareIntent.setAction(Intent.ACTION_SEND);
        shareIntent.putExtra(Intent.EXTRA_STREAM, uri);
        shareIntent.setType("image/jpeg");

        startActivity(Intent.createChooser(shareIntent, getResources().getText(R.string.send_to)));
    }

    private void onExposureCompSeekBarChanged(int progress) {
        if (mNativeCamera != null) {
            float value = progress / (float) mBinding.exposureSeekBar.getMax();
            mNativeCamera.setExposureCompensation(value);
        }
    }

    private void onSettingsClicked() {
        Intent intent = new Intent(this, SettingsActivity.class);
        startActivityForResult(intent, SETTINGS_ACTIVITY_REQUEST_CODE);
    }

    private void onFixedFocusCancelled() {
        setFocusState(FocusState.AUTO, null);
    }

    private void onCameraManualControlEnabled(boolean enabled) {
        if (mManualControlsEnabled == enabled)
            return;

        mManualControlsEnabled = enabled;
        mManualControlsSet = false;

        if (mManualControlsEnabled) {
            findViewById(R.id.cameraManualControlFrame).setVisibility(View.VISIBLE);
            findViewById(R.id.infoFrame).setVisibility(View.GONE);
            mBinding.exposureLayout.setVisibility(View.GONE);
        } else {
            findViewById(R.id.cameraManualControlFrame).setVisibility(View.GONE);
            findViewById(R.id.infoFrame).setVisibility(View.VISIBLE);
            findViewById(R.id.exposureCompFrame).setVisibility(View.VISIBLE);

            mBinding.exposureLayout.setVisibility(View.VISIBLE);

            if (mNativeCamera != null) {
                mNativeCamera.setAutoExposure();
            }
        }

        updateManualControlView(mSensorEventManager.getOrientation());
    }

    private void setPostProcessingDefaults() {
        // Set initial preview values
        mPostProcessSettings.shadows = 1.0f;
        mPostProcessSettings.contrast = mSettings.contrast;
        mPostProcessSettings.saturation = mSettings.saturation;
        mPostProcessSettings.greens = 3.0f;
        mPostProcessSettings.blues = 6.0f;
        mPostProcessSettings.sharpen0 = 3.0f;
        mPostProcessSettings.sharpen1 = 2.5f;
        mPostProcessSettings.whitePoint = -1;
        mPostProcessSettings.blacks = -1;
        mPostProcessSettings.tonemapVariance = 0.25f;
        mPostProcessSettings.jpegQuality = mSettings.jpegQuality;

        mTemperatureOffset = mSettings.temperatureOffset;
        mTintOffset = mSettings.tintOffset;
        mPostProcessSettings.dng = mSettings.saveDng;

        mShadowOffset = 0.0f;
    }

    @Override
    protected void onResume() {
        super.onResume();

        mSensorEventManager.enable();
        mProgressReceiver.setReceiver(this);

        mBinding.rawCameraPreview.setBitmap(null);
        mBinding.main.transitionToStart();

        // Load UI settings
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);
        mSettings.load(sharedPrefs);

        Log.d(TAG, mSettings.toString());

        updatePreviewTabUi(true);
        setPostProcessingDefaults();

        setCaptureMode(mSettings.captureMode);
        setSaveRaw(mSettings.saveDng);
        setHdr(mSettings.hdr);

        // Reset manual controls
        ((Switch) findViewById(R.id.manualControlSwitch)).setChecked(false);
        updateManualControlView(mSensorEventManager.getOrientation());

        mBinding.focusLockPointFrame.setVisibility(View.INVISIBLE);
        mBinding.previewPager.registerOnPageChangeCallback(mCapturedPreviewPagerListener);

        mFocusState = FocusState.AUTO;
        mAutoFocusPoint = null;
        mAutoExposurePoint = null;
        mUserCaptureModeOverride = false;

        // Start camera when we have all the permissions
        if (mHavePermissions) {
            initCamera();

            // Request location updates
            if (    ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
                ||  ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED)
            {
                LocationRequest locationRequest = LocationRequest.create();

                mFusedLocationClient.requestLocationUpdates(locationRequest, mLocationCallback, Looper.getMainLooper());
            }
        }
    }

    @Override
    protected void onPause() {
        super.onPause();

        // Save UI settings
        SharedPreferences sharedPrefs = getSharedPreferences(SettingsViewModel.CAMERA_SHARED_PREFS, Context.MODE_PRIVATE);

        // Update the settings
        mSettings.contrast = mPostProcessSettings.contrast;
        mSettings.saturation = mPostProcessSettings.saturation;
        mSettings.tintOffset = mTintOffset;
        mSettings.temperatureOffset = mTemperatureOffset;
        mSettings.saveDng = mPostProcessSettings.dng;
        mSettings.captureMode = mCaptureMode;

        mSettings.save(sharedPrefs);

        mSensorEventManager.disable();
        mProgressReceiver.setReceiver(null);
        mBinding.previewPager.unregisterOnPageChangeCallback(mCapturedPreviewPagerListener);

        if(mNativeCamera != null) {
            mNativeCamera.stopCapture();
        }

        if(mSurface != null) {
            mSurface.release();
            mSurface = null;
        }

        mBinding.cameraFrame.removeView(mTextureView);
        mTextureView = null;

        mFusedLocationClient.removeLocationUpdates(mLocationCallback);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        if(mNativeCamera != null) {
            mNativeCamera.destroy();
            mNativeCamera = null;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if(requestCode == SETTINGS_ACTIVITY_REQUEST_CODE) {
            // Restart process when coming back from settings since we may need to reload the camera library
            ProcessPhoenix.triggerRebirth(this);
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

        mBinding.previewFrame.contrastValue.setText(
                getString(R.string.value_percent, Math.round(mPostProcessSettings.contrast * 100)));

        mBinding.previewFrame.colourValue.setText(
                getString(R.string.value_percent, Math.round(mPostProcessSettings.saturation / 2.0f * 100)));

        mBinding.previewFrame.warmthValue.setText(
                getString(R.string.value_percent, Math.round((mTemperatureOffset + 1000.0f) / 2000.0f * 100.0f)));

        mBinding.previewFrame.tintValue.setText(
                getString(R.string.value_percent, Math.round((mTintOffset + 50.0f) / 100.0f * 100.0f)));

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

    private void setCaptureMode(CaptureMode captureMode) {
        if(mCaptureMode == captureMode)
            return;

        mBinding.nightModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.zslModeBtn.setTextColor(getColor(R.color.textColor));
        mBinding.burstModeBtn.setTextColor(getColor(R.color.textColor));

        mCaptureMode = captureMode;

        switch(mCaptureMode) {
            case NIGHT:
                mBinding.nightModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;

            case ZSL:
                mBinding.zslModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;

            case BURST:
                mBinding.burstModeBtn.setTextColor(getColor(R.color.colorAccent));
                break;
        }
    }

    private void setHdr(boolean hdr) {
        int color = hdr ? R.color.colorAccent : R.color.white;
        mBinding.previewFrame.hdrEnableBtn.setTextColor(getColor(color));

        for (Drawable drawable : mBinding.previewFrame.hdrEnableBtn.getCompoundDrawables()) {
            if (drawable != null) {
                drawable.setColorFilter(new PorterDuffColorFilter(ContextCompat.getColor(this, color), PorterDuff.Mode.SRC_IN));
            }
        }

        mSettings.hdr = hdr;
    }

    private void setSaveRaw(boolean saveRaw) {
        int color = saveRaw ? R.color.colorAccent : R.color.white;
        mBinding.previewFrame.rawEnableBtn.setTextColor(getColor(color));

        for (Drawable drawable : mBinding.previewFrame.rawEnableBtn.getCompoundDrawables()) {
            if (drawable != null) {
                drawable.setColorFilter(new PorterDuffColorFilter(ContextCompat.getColor(this, color), PorterDuff.Mode.SRC_IN));
            }
        }

        mPostProcessSettings.dng = saveRaw;
    }

    private void onCaptureModeClicked(View v) {
        mUserCaptureModeOverride = true;

        if(v == mBinding.nightModeBtn) {
            setCaptureMode(CaptureMode.NIGHT);
        }
        else if(v == mBinding.zslModeBtn) {
            setCaptureMode(CaptureMode.ZSL);
        }
        else if(v == mBinding.burstModeBtn) {
            setCaptureMode(CaptureMode.BURST);
        }
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

    private void capture(CaptureMode mode) {
        if(mNativeCamera == null)
            return;

        if(!mImageCaptureInProgress.compareAndSet(false, true)) {
            Log.e(TAG, "Aborting capture, one is already in progress");
            return;
        }

        PostProcessSettings estimatedSettings;

        try
        {
            estimatedSettings = mNativeCamera.getRawPreviewEstimatedPostProcessSettings();
        }
        catch (IOException e) {
            Log.e(TAG, "Failed to get estimated settings", e);
            estimatedSettings = new PostProcessSettings();

        }

        // Use estimated shadows
        mPostProcessSettings.shadows = estimatedSettings.shadows;

        // Store capture mode
        mPostProcessSettings.captureMode = mCaptureMode.name();

        if(mode == CaptureMode.BURST) {
            mImageCaptureInProgress.set(false);

            // Pass native camera handle
            Intent intent = new Intent(this, PostProcessActivity.class);

            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_HANDLE, mNativeCamera.getHandle());
            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_ID, mSelectedCamera.cameraId);
            intent.putExtra(PostProcessActivity.INTENT_NATIVE_CAMERA_FRONT_FACING, mSelectedCamera.isFrontFacing);

            startActivity(intent);
        }
        else if(mode == CaptureMode.NIGHT || mode == CaptureMode.ZSL) {
            mBinding.captureBtn.setEnabled(false);

            mBinding.captureProgressBar.setVisibility(View.VISIBLE);
            mBinding.captureProgressBar.setIndeterminateMode(false);

            PostProcessSettings settings = mPostProcessSettings.clone();

            // Map camera exposure to our own
            long cameraExposure = mExposureTime;

            if(!mManualControlsSet) {
                cameraExposure = Math.round(mExposureTime * Math.pow(2.0f, estimatedSettings.exposure));

                // We'll estimate the shadows again since the exposure has been adjusted
                if(mode == CaptureMode.NIGHT)
                    settings.shadows = -1;
            }

            CameraManualControl.Exposure baseExposure = CameraManualControl.Exposure.Create(
                    CameraManualControl.GetClosestShutterSpeed(cameraExposure),
                    CameraManualControl.GetClosestIso(mIsoValues, mIso));

            CameraManualControl.Exposure hdrExposure = CameraManualControl.Exposure.Create(
                    CameraManualControl.GetClosestShutterSpeed(Math.round(cameraExposure / mSettings.hdrEv)),
                    CameraManualControl.GetClosestIso(mIsoValues, mIso));

            // If the user has not override the shutter speed/iso, pick our own
            if(!mManualControlsSet) {
                baseExposure = CameraManualControl.MapToExposureLine(1.0, baseExposure);
            }

            hdrExposure = CameraManualControl.MapToExposureLine(1.0, hdrExposure, CameraManualControl.HDR_EXPOSURE_LINE);

            float a = 1.6f;
            if(mCameraMetadata.cameraApertures.length > 0)
                a = mCameraMetadata.cameraApertures[0];

            DenoiseSettings denoiseSettings = new DenoiseSettings(
                    estimatedSettings.noiseSigma,
                    (float) baseExposure.getEv(a),
                    settings.shadows);

            // Don't bother with HDR if the scene is underexposed/few clipped pixels or the noise of the
            // underexposed image will be too high
            boolean noHdr = !mSettings.hdr || estimatedSettings.exposure > 0.01f || hdrExposure.getEv(a) < 3.99f;
            if(noHdr) {
                hdrExposure = baseExposure;
            }

            settings.chromaFilterEps = denoiseSettings.chromaFilterEps;
            settings.chromaBlendWeight = denoiseSettings.chromaBlendWeight;
            settings.spatialDenoiseAggressiveness = denoiseSettings.spatialWeight;
            settings.exposure = 0.0f;
            settings.temperature = estimatedSettings.temperature + mTemperatureOffset;
            settings.tint = estimatedSettings.tint + mTintOffset;

            long exposure = baseExposure.shutterSpeed.getExposureTime();
            int iso = baseExposure.iso.getIso();

            if(mCaptureMode == CaptureMode.ZSL) {
                if(noHdr) {
                    Log.i(TAG, "Requested ZSL capture (denoiseSettings=" + denoiseSettings.toString() + ")");

                    mBinding.cameraFrame
                            .animate()
                            .alpha(0.25f)
                            .setDuration(125)
                            .withEndAction(() -> mBinding.cameraFrame
                                    .animate()
                                    .alpha(1.0f)
                                    .setDuration(125)
                                    .start())
                            .start();

                    mAsyncNativeCameraOps.captureImage(
                            -1,
                            denoiseSettings.numMergeImages,
                            settings,
                            CameraProfile.generateCaptureFile(this).getPath(),
                            this);

                    return;
                }
                else {
                    // Use a single underexposed image
                    exposure = -1;
                    iso = -1;
                }
            }

            Log.i(TAG, "Requested HDR capture (denoiseSettings=" + denoiseSettings.toString() + ")");

            mBinding.cameraFrame
                    .animate()
                    .alpha(0.25f)
                    .setDuration(250)
                    .start();

            mNativeCamera.captureHdrImage(
                    denoiseSettings.numMergeImages,
                    iso,
                    exposure,
                    hdrExposure.iso.getIso(),
                    hdrExposure.shutterSpeed.getExposureTime(),
                    settings,
                    CameraProfile.generateCaptureFile(this).getPath());
        }
    }

    private void onCaptureClicked() {
        capture(mCaptureMode);
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
        ArrayList<String> needPermissions = new ArrayList<>();

        for(String permission : REQUEST_PERMISSIONS) {
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

        // Check if camera permission has been denied
        for(int i = 0; i < permissions.length; i++) {
            if(grantResults[i] == PackageManager.PERMISSION_DENIED && permissions[i].equals(Manifest.permission.CAMERA)) {
                runOnUiThread(this::onPermissionsDenied);
                return;
            }
        }

        runOnUiThread(this::onPermissionsGranted);
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
        if (mNativeCamera == null) {
            // Load our native camera library
            if(mSettings.useDualExposure) {
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

            mNativeCamera = new NativeCameraSessionBridge(this, mSettings.memoryUseBytes, null);
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

        int numEvSteps = mSelectedCamera.exposureCompRangeMax - mSelectedCamera.exposureCompRangeMin;
        mBinding.exposureSeekBar.setMax(numEvSteps);
        mBinding.exposureSeekBar.setProgress(numEvSteps / 2);

        mBinding.shadowsSeekBar.setProgress(50);

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

    @Override
    public void onSurfaceTextureAvailable(@NonNull SurfaceTexture surfaceTexture, int width, int height) {
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
        int displayWidth;
        int displayHeight;

        if(mSettings.useDualExposure) {
            // Use small preview window since we're not using the camera preview.
            displayWidth = 640;
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
        mNativeCamera.startCapture(mSelectedCamera, mSurface, mSettings.useDualExposure, mSettings.rawMode == SettingsViewModel.RawMode.RAW16);

        // Update orientation in case we've switched front/back cameras
        NativeCameraBuffer.ScreenOrientation orientation = mSensorEventManager.getOrientation();
        if(orientation != null)
            onOrientationChanged(orientation);

        if(mSettings.useDualExposure) {
            mBinding.rawCameraPreview.setVisibility(View.VISIBLE);
            mBinding.shadowsLayout.setVisibility(View.VISIBLE);

            mTextureView.setAlpha(0);

            mNativeCamera.enableRawPreview(this, mSettings.cameraPreviewQuality, false);
        }
        else {
            mBinding.rawCameraPreview.setVisibility(View.GONE);
            mBinding.shadowsLayout.setVisibility(View.GONE);

            mTextureView.setAlpha(1);
        }

        mBinding.previewFrame.previewControls.setVisibility(View.VISIBLE);

        mBinding.previewFrame.previewAdjustmentsBtn.setOnClickListener(v -> {
            mBinding.previewFrame.previewControlBtns.setVisibility(View.GONE);
            mBinding.previewFrame.previewAdjustments.setVisibility(View.VISIBLE);
        });

        mBinding.previewFrame.previewAdjustments.findViewById(R.id.closePreviewAdjustmentsBtn).setOnClickListener(v -> {
            mBinding.previewFrame.previewControlBtns.setVisibility(View.VISIBLE);
            mBinding.previewFrame.previewAdjustments.setVisibility(View.GONE);
        });

        mBinding.previewFrame.rawEnableBtn.setOnClickListener(v -> setSaveRaw(!mPostProcessSettings.dng));
        mBinding.previewFrame.hdrEnableBtn.setOnClickListener(v -> setHdr(!mSettings.hdr));
    }

    @Override
    public void onSurfaceTextureSizeChanged(@NonNull SurfaceTexture surface, int width, int height) {
        Log.d(TAG, "onSurfaceTextureSizeChanged() w: " + width + " h: " + height);
    }

    @Override
    public boolean onSurfaceTextureDestroyed(@NonNull SurfaceTexture surface) {
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
    public void onSurfaceTextureUpdated(@NonNull SurfaceTexture surface) {
    }

    private void autoSwitchCaptureMode() {
        if(!mSettings.autoNightMode || mCaptureMode == CaptureMode.BURST || mManualControlsSet || mUserCaptureModeOverride)
            return;

        // Switch to night mode if we high ISO/shutter speed
        if(mIso >= 1600 || mExposureTime > CameraManualControl.SHUTTER_SPEED.EXPOSURE_1_40.getExposureTime())
            setCaptureMode(CaptureMode.NIGHT);
        else
            setCaptureMode(CaptureMode.ZSL);
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
                updatePreviewSettings();
            });
        }
    }

    @Override
    public void onCameraExposureStatus(int iso, long exposureTime) {
//        Log.i(TAG, "ISO: " + iso + " Exposure Time: " + exposureTime/(1000.0*1000.0));

        final CameraManualControl.ISO cameraIso = CameraManualControl.GetClosestIso(mIsoValues, iso);
        final CameraManualControl.SHUTTER_SPEED cameraShutterSpeed = CameraManualControl.GetClosestShutterSpeed(exposureTime);

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

            autoSwitchCaptureMode();
        });
    }

    @Override
    public void onCameraAutoFocusStateChanged(NativeCameraSessionBridge.CameraFocusState state) {
        runOnUiThread(() -> {
            onFocusStateChanged(state);
        });
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
            if(progress < 100) {
                mBinding.captureProgressBar.setProgress(progress);
            }
            else {
                mBinding.captureProgressBar.setIndeterminateMode(true);
            }
        });
    }

    @Override
    public void onCameraHdrImageCaptureFailed() {
        Log.i(TAG, "HDR capture failed");

        runOnUiThread( () ->
        {
            mBinding.captureBtn.setEnabled(true);
            mBinding.captureProgressBar.setVisibility(View.INVISIBLE);

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

        mImageCaptureInProgress.set(false);

        runOnUiThread( () ->
        {
            mBinding.captureBtn.setEnabled(true);
            mBinding.captureProgressBar.setVisibility(View.INVISIBLE);

            mBinding.cameraFrame
                    .animate()
                    .alpha(1.0f)
                    .setDuration(250)
                    .start();

            startImageProcessor();
        });
    }

    @Override
    public void onCaptured(long handle) {
        Log.i(TAG, "ZSL capture completed");

        mImageCaptureInProgress.set(false);

        mBinding.captureBtn.setEnabled(true);
        mBinding.captureProgressBar.setVisibility(View.INVISIBLE);

        startImageProcessor();
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

        mBinding.previewFrame.settingsBtn
                .animate()
                .rotation(orientation.angle)
                .setDuration(duration)
                .start();

        mBinding.capturePreview
                .animate()
                .rotation(orientation.angle)
                .setDuration(duration)
                .start();

    }

    private void onShadowsSeekBarChanged(int progress) {
        mShadowOffset = 6.0f * ((progress - 50.0f) / 100.0f);

        updatePreviewSettings();
    }

    private void updatePreviewSettings() {
        if(mPostProcessSettings != null && mNativeCamera != null) {
            mNativeCamera.setRawPreviewSettings(
                    mShadowOffset,
                    mPostProcessSettings.contrast,
                    mPostProcessSettings.saturation,
                    0.05f,
                    1.0f,
                    mTemperatureOffset,
                    mTintOffset);
        }
    }

    private void onFocusStateChanged(NativeCameraSessionBridge.CameraFocusState state) {
        Log.i(TAG, "Focus state: " + state.name());

        if( state == NativeCameraSessionBridge.CameraFocusState.PASSIVE_SCAN ||
            state == NativeCameraSessionBridge.CameraFocusState.ACTIVE_SCAN)
        {
            if(mAutoFocusPoint == null) {
                FrameLayout.LayoutParams layoutParams =
                        (FrameLayout.LayoutParams) mBinding.focusLockPointFrame.getLayoutParams();

                layoutParams.setMargins(
                        (mTextureView.getWidth() - mBinding.focusLockPointFrame.getWidth()) / 2,
                        (mTextureView.getHeight() - mBinding.focusLockPointFrame.getHeight()) / 2,
                        0,
                        0);

                mBinding.focusLockPointFrame.setAlpha(1.0f);
                mBinding.focusLockPointFrame.setLayoutParams(layoutParams);
            }

            if(mBinding.focusLockPointFrame.getVisibility() == View.INVISIBLE) {
                mBinding.focusLockPointFrame.setVisibility(View.VISIBLE);
                mBinding.focusLockPointFrame.setAlpha(0.0f);

                mBinding.focusLockPointFrame
                        .animate()
                        .alpha(1)
                        .setDuration(250)
                        .start();
            }
        }
        else if(state == NativeCameraSessionBridge.CameraFocusState.PASSIVE_FOCUSED) {
            if(mBinding.focusLockPointFrame.getVisibility() == View.VISIBLE) {
                mBinding.focusLockPointFrame
                        .animate()
                        .alpha(0)
                        .setStartDelay(500)
                        .setDuration(250)
                        .withEndAction(() -> mBinding.focusLockPointFrame.setVisibility(View.INVISIBLE))
                        .start();
            }
        }
    }

    private void setAutoExposureState(NativeCameraSessionBridge.CameraExposureState state) {
        boolean timePassed = System.currentTimeMillis() - mFocusRequestedTimestampMs > 5000;

        if(state == NativeCameraSessionBridge.CameraExposureState.SEARCHING && timePassed) {
            setFocusState(FocusState.AUTO, null);
        }
    }

    private void setFocusState(FocusState state, PointF focusPt) {
        if(mFocusState == FocusState.AUTO && state == FocusState.AUTO)
            return;

        mFocusState = state;

        if(state == FocusState.FIXED) {
            mAutoExposurePoint = focusPt;
            mAutoFocusPoint = focusPt;

            mNativeCamera.setFocusPoint(mAutoFocusPoint, mAutoExposurePoint);
            mFocusRequestedTimestampMs = System.currentTimeMillis();
        }
        else if(state == FocusState.AUTO) {
            mAutoFocusPoint = null;
            mAutoExposurePoint = null;

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

        FrameLayout.LayoutParams layoutParams =
                (FrameLayout.LayoutParams) mBinding.focusLockPointFrame.getLayoutParams();

        layoutParams.setMargins(
                Math.round(touchX) - mBinding.focusLockPointFrame.getWidth() / 2,
                Math.round(touchY) - mBinding.focusLockPointFrame.getHeight() / 2,
                0,
                0);

        mBinding.focusLockPointFrame.setLayoutParams(layoutParams);
        mBinding.focusLockPointFrame.setVisibility(View.VISIBLE);
        mBinding.focusLockPointFrame.setAlpha(1.0f);
        mBinding.focusLockPointFrame.animate().cancel();

        setFocusState(FocusState.FIXED, pt);
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
    public void onPreviewSaved(String outputPath) {
        Glide.with(this)
                .load(outputPath)
                .dontAnimate()
                .into(mBinding.capturePreview);

        mCameraCapturePreviewAdapter.add(new File(outputPath));

        mBinding.capturePreview.setScaleX(0.5f);
        mBinding.capturePreview.setScaleY(0.5f);
        mBinding.capturePreview
                .animate()
                .scaleX(1)
                .scaleY(1)
                .setInterpolator(new BounceInterpolator())
                .setDuration(500)
                .start();
    }

    @Override
    public void onProcessingCompleted(File internalPath, Uri contentUri) {
        mCameraCapturePreviewAdapter.complete(internalPath, contentUri);

        if(mCameraCapturePreviewAdapter.isProcessing(mBinding.previewPager.getCurrentItem())) {
            mBinding.previewProcessingFrame.setVisibility(View.VISIBLE);
        }
        else {
            mBinding.previewProcessingFrame.setVisibility(View.INVISIBLE);
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if(keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) {
            onCaptureClicked();
            return true;
        }
        else if (keyCode == KeyEvent.KEYCODE_VOLUME_UP) {
            capture(CaptureMode.BURST);
            return true;
        }

        return super.onKeyDown(keyCode, event);
    }

    @Override
    public void onTransitionStarted(MotionLayout motionLayout, int startId, int endId)  {
        mBinding.previewProcessingFrame.setVisibility(View.INVISIBLE);
    }

    @Override
    public void onTransitionChange(MotionLayout motionLayout, int startId, int endId, float progress) {

    }

    @Override
    public void onTransitionCompleted(MotionLayout motionLayout, int currentId)  {
        if(mNativeCamera == null)
            return;

        if(currentId == mBinding.main.getEndState()) {
            // Reset exposure/shadows
            mBinding.exposureSeekBar.setProgress(mBinding.exposureSeekBar.getMax() / 2);
            mBinding.shadowsSeekBar.setProgress(mBinding.shadowsSeekBar.getMax() / 2);

            mBinding.previewPager.setCurrentItem(0);

            mNativeCamera.pauseCapture();

            if(mCameraCapturePreviewAdapter.isProcessing(mBinding.previewPager.getCurrentItem())) {
                mBinding.previewProcessingFrame.setVisibility(View.VISIBLE);
            }
        }
        else if(currentId == mBinding.main.getStartState()) {
            mNativeCamera.resumeCapture();
        }
    }

    @Override
    public void onTransitionTrigger(MotionLayout motionLayout, int triggerId, boolean positive, float progress) {
    }

    void onCapturedPreviewPageChanged(int position) {
        if(mCameraCapturePreviewAdapter.isProcessing(position)) {
            mBinding.previewProcessingFrame.setVisibility(View.VISIBLE);
        }
        else {
            mBinding.previewProcessingFrame.setVisibility(View.INVISIBLE);
        }
    }

    void onReceivedLocation(Location lastLocation) {
        mLastLocation = lastLocation;

        if(mPostProcessSettings != null) {
            mPostProcessSettings.gpsLatitude = mLastLocation.getLatitude();
            mPostProcessSettings.gpsLongitude = mLastLocation.getLongitude();
            mPostProcessSettings.gpsAltitude = mLastLocation.getAltitude();
            mPostProcessSettings.gpsTime = String.valueOf(mLastLocation.getTime());
        }
    }
}
