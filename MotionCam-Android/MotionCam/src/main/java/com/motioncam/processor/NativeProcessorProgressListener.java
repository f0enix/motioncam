package com.motioncam.processor;


public interface NativeProcessorProgressListener {
    String onPreviewSaved(String outputPath);
    boolean onProgressUpdate(int progress);
    void onCompleted();
    void onError(String error);
}
