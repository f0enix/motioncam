package com.motioncam.camera;

public class NativeCameraInfo {
    public final String cameraId;
    public final boolean isFrontFacing;
    public final int exposureCompRangeMin;
    public final int exposureCompRangeMax;

    public NativeCameraInfo(String cameraId, boolean isFrontFacing, int exposureCompRangeMin, int exposureCompRangeMax) {
        this.cameraId = cameraId;
        this.isFrontFacing = isFrontFacing;
        this.exposureCompRangeMin = exposureCompRangeMin;
        this.exposureCompRangeMax = exposureCompRangeMax;
    }
}
