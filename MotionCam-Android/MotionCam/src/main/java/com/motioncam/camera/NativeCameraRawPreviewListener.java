package com.motioncam.camera;

import android.graphics.Bitmap;

public interface NativeCameraRawPreviewListener {
    Bitmap onRawPreviewBitmapNeeded(int width, int height);
    void onRawPreviewUpdated();
}
