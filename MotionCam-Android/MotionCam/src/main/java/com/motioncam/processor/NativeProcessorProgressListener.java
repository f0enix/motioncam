package com.motioncam.processor;


public interface NativeProcessorProgressListener {
    boolean onProgressUpdate(int progress);
    void onCompleted();
    void onError(String error);
}
