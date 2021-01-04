package com.motioncam.camera;

public class NativeCameraInfo {
    public final String cameraId;
    public final boolean isFrontFacing;
    public final boolean supportsLinearPreview;

    public NativeCameraInfo(String cameraId, boolean isFrontFacing, boolean supportsLinearPreview) {
        this.cameraId = cameraId;
        this.isFrontFacing = isFrontFacing;
        this.supportsLinearPreview = supportsLinearPreview;
    }
}
