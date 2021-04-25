package com.motioncam.camera;

import android.graphics.Bitmap;
import android.os.Handler;
import android.os.Looper;
import android.util.Pair;
import android.util.Size;

import java.io.Closeable;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public class AsyncNativeCameraOps implements Closeable {
    public enum PreviewSize {
        SMALL(8),
        MEDIUM(4),
        LARGE(2);

        private final int scale;

        PreviewSize(int scale) {
            this.scale = scale;
        }
    }

    private ExecutorService mBackgroundProcessor = Executors.newSingleThreadExecutor();
    private final NativeCameraSessionBridge mCameraSessionBridge;
    private final Handler mMainHandler;
    private Size mUnscaledSize;

    public interface PreviewListener {
        void onPreviewAvailable(NativeCameraBuffer buffer, Bitmap image);
    }

    public interface PostProcessSettingsListener {
        void onSettingsEstimated(PostProcessSettings settings);
    }

    public interface CaptureImageListener {
        void onCaptured(long handle);
    }

    public interface SharpnessMeasuredListener {
        void onSharpnessMeasured(List<Pair<NativeCameraBuffer, Double>> sharpnessList);
    }

    public AsyncNativeCameraOps(NativeCameraSessionBridge cameraSessionBridge) {
        mCameraSessionBridge = cameraSessionBridge;
        mMainHandler = new Handler(Looper.getMainLooper());
    }

    @Override
    public void close() {
        mBackgroundProcessor.shutdown();

        try {
            if(!mBackgroundProcessor.awaitTermination(500, TimeUnit.MILLISECONDS)) {
                mBackgroundProcessor.shutdownNow();
            }
        }
        catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    public void captureImage(long bufferHandle, int numSaveImages, boolean writeDNG, PostProcessSettings settings, String outputPath, CaptureImageListener listener) {
        mBackgroundProcessor.submit(() -> {
            mCameraSessionBridge.captureImage(bufferHandle, numSaveImages, writeDNG, settings, outputPath);
            mMainHandler.post(() -> listener.onCaptured(bufferHandle));
        });
    }

    public void estimateSettings(boolean basicSettings, float shadowsBias, PostProcessSettingsListener listener) {
        mBackgroundProcessor.submit(() -> {
            try {
                PostProcessSettings result = mCameraSessionBridge.estimatePostProcessSettings(basicSettings, shadowsBias);
                mMainHandler.post(() -> listener.onSettingsEstimated(result));

            }
            catch (IOException e) {
                e.printStackTrace();
            }
        });
    }

    public void measureSharpness(List<NativeCameraBuffer> buffers, SharpnessMeasuredListener listener) {
        if(buffers.isEmpty())
            return;

        mBackgroundProcessor.submit(() -> {
            List<Pair<NativeCameraBuffer, Double>> result = new ArrayList<>();

            for(NativeCameraBuffer buffer : buffers) {
                double sharpness = mCameraSessionBridge.measureSharpness(buffer.timestamp);
                result.add(new Pair<>(buffer, sharpness));
            }

            result.sort((l, r) -> l.second.compareTo(r.second));

            mMainHandler.post(() -> listener.onSharpnessMeasured(result));
        });
    }

    public Size getPreviewSize(PreviewSize generateSize, NativeCameraBuffer buffer) {
        if (mUnscaledSize == null)
            mUnscaledSize = mCameraSessionBridge.getPreviewSize(1);

        if(mUnscaledSize == null)
            return new Size(0, 0);

        int width = mUnscaledSize.getWidth() / generateSize.scale;
        int height = mUnscaledSize.getHeight() / generateSize.scale;

        if( buffer.screenOrientation == NativeCameraBuffer.ScreenOrientation.PORTRAIT ||
            buffer.screenOrientation == NativeCameraBuffer.ScreenOrientation.REVERSE_PORTRAIT) {

            int temp = width;

            width = height;
            height = temp;

        }

        return new Size(width, height);
    }

    public void generatePreview(NativeCameraBuffer buffer,
                                PostProcessSettings settings,
                                PreviewSize generateSize,
                                Bitmap useBitmap,
                                PreviewListener listener)
    {
        PostProcessSettings postProcessSettings = settings.clone();

        mBackgroundProcessor.submit(() -> {
            Bitmap preview = useBitmap;
            Size size = getPreviewSize(generateSize, buffer);

            // Create bitmap if the provided one was incompatible (or null)
            if( preview == null ||
                preview.getWidth() != size.getWidth() ||
                preview.getHeight() != size.getHeight() )
            {
                preview = Bitmap.createBitmap(size.getWidth(), size.getHeight(), Bitmap.Config.ARGB_8888);
            }

            mCameraSessionBridge.createPreviewImage(
                    buffer.timestamp,
                    postProcessSettings,
                    generateSize.scale,
                    preview);

            final Bitmap resultBitmap = preview;

            // On the main thread, let listeners know that an image is ready
            mMainHandler.post(() -> listener.onPreviewAvailable(buffer, resultBitmap));
        });
    }
}
