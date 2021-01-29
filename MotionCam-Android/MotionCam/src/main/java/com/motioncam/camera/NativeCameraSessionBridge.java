package com.motioncam.camera;

import android.graphics.Bitmap;
import android.graphics.PointF;
import android.util.Size;
import android.view.Surface;

import com.squareup.moshi.JsonAdapter;
import com.squareup.moshi.Moshi;

import java.io.IOException;

public class NativeCameraSessionBridge implements NativeCameraSessionListener, NativeCameraRawPreviewListener {
    // Load our native camera library
    static {
        System.loadLibrary("native-camera");
    }

    public final static long INVALID_NATIVE_HANDLE = -1;

    // Camera states
    public enum CameraState {
        INVALID(-1),
        READY(0),
        ACTIVE(1),
        CLOSED(2);

        private final int mState;

        CameraState(int state) {
            mState = state;
        }

        static public CameraState FromInt(int state) {
            for(CameraState cameraState : CameraState.values()) {
                if(cameraState.mState == state)
                    return cameraState;
            }

            return INVALID;
        }
    }

    public enum CameraFocusState {
        INVALID(-1),
        INACTIVE(0),
        PASSIVE_SCAN(1),
        PASSIVE_FOCUSED(2),
        ACTIVE_SCAN(3),
        FOCUS_LOCKED(4),
        NOT_FOCUS_LOCKED(5),
        PASSIVE_UNFOCUSED(6);

        private final int mState;

        CameraFocusState(int state) {
            mState = state;
        }

        static public CameraFocusState FromInt(int state) {
            for(CameraFocusState cameraFocusState : CameraFocusState.values()) {
                if(cameraFocusState.mState == state)
                    return cameraFocusState;
            }

            return INVALID;
        }
    }

    public enum CameraExposureState {
        INVALID(-1),
        INACTIVE(0),
        SEARCHING(1),
        CONVERGED(2),
        LOCKED(3),
        FLASH_REQUIRED(4),
        PRECAPTURE(5);

        private final int mState;

        CameraExposureState(int state) {
            mState = state;
        }

        static public CameraExposureState FromInt(int state) {
            for(CameraExposureState cameraExposureState : CameraExposureState.values()) {
                if(cameraExposureState.mState == state)
                    return cameraExposureState;
            }

            return INVALID;
        }
    }

    public interface CameraSessionListener {
        void onCameraDisconnected();
        void onCameraError(int error);
        void onCameraSessionStateChanged(CameraState state);
        void onCameraExposureStatus(int iso, long exposureTime);
        void onCameraAutoFocusStateChanged(CameraFocusState state);
        void onCameraAutoExposureStateChanged(CameraExposureState state);
        void onCameraHdrImageCaptureProgress(int progress);
        void onCameraHdrImageCaptureCompleted();
    }

    public interface CameraRawPreviewListener {
        void onRawPreviewCreated(Bitmap bitmap);
        void onRawPreviewUpdated();
    }

    public static class CameraException extends RuntimeException {
        CameraException(String error) {
            super(error);
        }
    }

    private long mNativeCameraHandle;
    private Moshi mJson = new Moshi.Builder().build();
    private CameraSessionListener mListener;
    private CameraRawPreviewListener mRawPreviewListener;
    private NativeCameraMetadata mNativeCameraMetadata;

    public NativeCameraSessionBridge(long nativeHandle) {
        mNativeCameraHandle = nativeHandle;

        // Add to reference count
        Acquire(nativeHandle);
    }

    public NativeCameraSessionBridge(CameraSessionListener cameraListener, long maxMemoryUsageBytes, String nativeLibPath) {
        mNativeCameraHandle = Create(this, maxMemoryUsageBytes, nativeLibPath);
        mListener = cameraListener;

        if(mNativeCameraHandle == INVALID_NATIVE_HANDLE) {
            throw new CameraException(GetLastError());
        }
    }

    private void ensureValidHandle() {
        if(mNativeCameraHandle == INVALID_NATIVE_HANDLE) {
            throw new CameraException("Camera not ready");
        }
    }

    public long getHandle() {
        return mNativeCameraHandle;
    }

    public void destroy() {
        ensureValidHandle();

        DestroyImageProcessor();
        Destroy(mNativeCameraHandle);

        mListener = null;
        mNativeCameraHandle = INVALID_NATIVE_HANDLE;
    }

    public NativeCameraInfo[] getSupportedCameras() {
        ensureValidHandle();

        return GetSupportedCameras(mNativeCameraHandle);
    }

    public void startCapture(NativeCameraInfo cameraInfo, Surface previewOutput) {
        ensureValidHandle();

        if(!StartCapture(mNativeCameraHandle, cameraInfo.cameraId, previewOutput)) {
            throw new CameraException(GetLastError());
        }
    }

    public void stopCapture() {
        ensureValidHandle();

        if(!StopCapture(mNativeCameraHandle)) {
            throw new CameraException(GetLastError());
        }
    }

    public void pauseCapture() {
        ensureValidHandle();

        PauseCapture(mNativeCameraHandle);
    }

    public void resumeCapture() {
        ensureValidHandle();

        ResumeCapture(mNativeCameraHandle);
    }

    public void setAutoExposure() {
        ensureValidHandle();

        SetAutoExposure(mNativeCameraHandle);
    }

    public void setManualExposureValues(int iso, long exposureValue) {
        ensureValidHandle();

        SetManualExposure(mNativeCameraHandle, iso, exposureValue);
    }

    public void setExposureCompensation(float value) {
        ensureValidHandle();

        SetExposureCompensation(mNativeCameraHandle, value);
    }

    public Size getRawConfigurationOutput(NativeCameraInfo cameraInfo) {
        ensureValidHandle();

        Size rawOutput = GetRawOutputSize(mNativeCameraHandle, cameraInfo.cameraId);
        if(rawOutput == null) {
            throw new CameraException(GetLastError());
        }

        return rawOutput;
    }

    public Size getPreviewConfigurationOutput(NativeCameraInfo cameraInfo, Size captureSize, Size displaySize) {
        ensureValidHandle();

        Size previewOutput = GetPreviewOutputSize(mNativeCameraHandle, cameraInfo.cameraId, captureSize, displaySize);
        if(previewOutput == null) {
            throw new CameraException(GetLastError());
        }

        return previewOutput;
    }

    public void captureImage(long bufferHandle, int numSaveImages, boolean writeDNG, PostProcessSettings settings, String outputPath) {
        ensureValidHandle();

        // Serialize settings to json and pass to native code
        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        String json = jsonAdapter.toJson(settings);

        CaptureImage(mNativeCameraHandle, bufferHandle, numSaveImages, writeDNG, json, outputPath);
    }

    public void captureHdrImage(int numSaveImages, int baseIso, long baseExposure, int hdrIso, long hdrExposure, PostProcessSettings settings, String outputPath) {
        ensureValidHandle();

        // Serialize settings to json and pass to native code
        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        String json = jsonAdapter.toJson(settings);

        CaptureHdrImage(mNativeCameraHandle, numSaveImages, baseIso, baseExposure, hdrIso, hdrExposure, json, outputPath);
    }

    public Size getPreviewSize(int downscaleFactor) {
        ensureValidHandle();

        return GetPreviewSize(mNativeCameraHandle, downscaleFactor);
    }

    public void createPreviewImage(long bufferHandle, PostProcessSettings settings, int downscaleFactor, Bitmap dst) {
        if(mNativeCameraHandle == INVALID_NATIVE_HANDLE) {
            throw new CameraException("Camera not ready");
        }

        // Serialize settings to json and pass to native code
        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        String json = jsonAdapter.toJson(settings);

        CreateImagePreview(mNativeCameraHandle, bufferHandle, json, downscaleFactor, dst);
    }

    public NativeCameraBuffer[] getAvailableImages() {
        ensureValidHandle();

        return GetAvailableImages(mNativeCameraHandle);
    }

    public PostProcessSettings estimatePostProcessSettings(long bufferHandle, boolean basicSettings) throws IOException {
        ensureValidHandle();

        String settingsJson = EstimatePostProcessSettings(mNativeCameraHandle, bufferHandle, basicSettings);
        if(settingsJson == null) {
            return null;
        }

        JsonAdapter<PostProcessSettings> jsonAdapter = mJson.adapter(PostProcessSettings.class);
        return jsonAdapter.fromJson(settingsJson);
    }

    public float estimateShadows() {
        ensureValidHandle();

        return EstimateShadows(mNativeCameraHandle);
    }

    public double measureSharpness(long bufferHandle) {
        ensureValidHandle();

        return MeasureSharpness(mNativeCameraHandle, bufferHandle);
    }

    public NativeCameraMetadata getMetadata(NativeCameraInfo cameraInfo) {
        ensureValidHandle();

        return GetMetadata(mNativeCameraHandle, cameraInfo.cameraId);
    }

    public void enableRawPreview(CameraRawPreviewListener listener) {
        ensureValidHandle();

        mRawPreviewListener = listener;

        EnableRawPreview(mNativeCameraHandle, this);
    }

    public void setRawPreviewSettings(float shadows, float contrast, float saturation, float blacks, float whitePoint) {
        ensureValidHandle();

        SetRawPreviewSettings(mNativeCameraHandle, shadows, contrast, saturation, blacks, whitePoint);
    }

    public void disableRawPreview() {
        ensureValidHandle();

        DisableRawPreview(mNativeCameraHandle);
    }

    public void updateOrientation(NativeCameraBuffer.ScreenOrientation orientation) {
        ensureValidHandle();

        UpdateOrientation(mNativeCameraHandle, orientation.value);
    }

    public void setFocusPoint(PointF focusPt, PointF exposurePt) {
        ensureValidHandle();

        SetFocusPoint(mNativeCameraHandle, focusPt.x, focusPt.y, exposurePt.x, exposurePt.y);
    }

    public void setAutoFocus() {
        ensureValidHandle();

        SetAutoFocus(mNativeCameraHandle);
    }

    @Override
    public void onCameraDisconnected() {
        mListener.onCameraDisconnected();
    }

    @Override
    public void onCameraError(int error) {
        mListener.onCameraError(error);
    }

    @Override
    public void onCameraSessionStateChanged(int state) {
        mListener.onCameraSessionStateChanged(CameraState.FromInt(state));
    }

    @Override
    public void onCameraExposureStatus(int iso, long exposureTime) {
        mListener.onCameraExposureStatus(iso, exposureTime);
    }

    @Override
    public void onCameraAutoFocusStateChanged(int state) {
        mListener.onCameraAutoFocusStateChanged(CameraFocusState.FromInt(state));
    }

    @Override
    public void onCameraAutoExposureStateChanged(int state) {
        mListener.onCameraAutoExposureStateChanged(CameraExposureState.FromInt(state));
    }

    @Override
    public void onCameraHdrImageCaptureProgress(int image) {
        mListener.onCameraHdrImageCaptureProgress(image);
    }

    @Override
    public void onCameraHdrImageCaptureCompleted() {
        mListener.onCameraHdrImageCaptureCompleted();
    }

    @Override
    public Bitmap onRawPreviewBitmapNeeded(int width, int height) {
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);

        mRawPreviewListener.onRawPreviewCreated(bitmap);

        return bitmap;
    }

    public void initImageProcessor()
    {
        InitImageProcessor();
    }

    public void destroyImageProcessor()
    {
        DestroyImageProcessor();
    }

    @Override
    public void onRawPreviewUpdated() {
        mRawPreviewListener.onRawPreviewUpdated();
    }

    private native long Create(NativeCameraSessionListener listener, long maxMemoryUsageBytes, String nativeLibPath);
    private native void Destroy(long handle);
    private native boolean Acquire(long handle);

    private native String GetLastError();

    private native NativeCameraInfo[] GetSupportedCameras(long handle);

    private native boolean StartCapture(long handle, String cameraId, Surface previewSurface);
    private native boolean StopCapture(long handle);

    private native boolean PauseCapture(long handle);
    private native boolean ResumeCapture(long handle);

    private native boolean SetManualExposure(long handle, int iso, long exposureTime);
    private native boolean SetAutoExposure(long handle);
    private native boolean SetExposureCompensation(long handle, float value);

    private native boolean EnableRawPreview(long handle, NativeCameraRawPreviewListener listener);
    private native boolean SetRawPreviewSettings(long handle, float shadows, float contrast, float saturation, float blacks, float whitePoint);
    private native boolean DisableRawPreview(long handle);

    private native boolean SetFocusPoint(long handle, float focusX, float focusY, float exposureX, float exposureY);
    private native boolean SetAutoFocus(long handle);

    private native boolean UpdateOrientation(long handle, int orientation);

    private native void InitImageProcessor();
    private native void DestroyImageProcessor();
    private native NativeCameraMetadata GetMetadata(long handle, String cameraId);
    private native Size GetRawOutputSize(long handle, String cameraId);
    private native Size GetPreviewOutputSize(long handle, String cameraId, Size captureSize, Size displaySize);
    private native boolean CaptureImage(long handle, long bufferHandle, int numSaveImages, boolean writeDNG, String settings, String outputPath);
    private native boolean CaptureHdrImage(long handle, int numImages, int baseIso, long baseExposure, int hdrIso, long hdrExposure, String settings, String outputPath);

    private native NativeCameraBuffer[] GetAvailableImages(long handle);
    private native Size GetPreviewSize(long handle, int downscaleFactor);
    private native boolean CreateImagePreview(long handle, long bufferHandle, String settings, int downscaleFactor, Bitmap dst);

    private native double MeasureSharpness(long handle, long bufferHandle);

    private native float EstimateShadows(long handle);
    private native String EstimatePostProcessSettings(long handle, long bufferHandle, boolean basicSettings);
}
