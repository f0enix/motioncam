package com.motioncam.camera;

public interface NativeCameraSessionListener {
    void onCameraDisconnected();
    void onCameraError(int error);
    void onCameraSessionStateChanged(int state);
    void onCameraExposureStatus(int iso, long exposureTime);
    void onCameraAutoFocusStateChanged(int state);
    void onCameraAutoExposureStateChanged(int state);
    void onCameraHdrImageCaptureProgress(int progress);
    void onCameraHdrImageCaptureCompleted();
}
