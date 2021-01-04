package com.motioncam.processor;

public class NativeProcessor {
    private static final long INVALID_OBJECT = -1;
    private long mNativeObject = INVALID_OBJECT;

    public void processFile(String inputPath, String outputPath, NativeProcessorProgressListener listener) {
        mNativeObject = CreateProcessor();
        if(mNativeObject == INVALID_OBJECT) {
            throw new IllegalStateException(GetLastError());
        }

        ProcessFile(mNativeObject, inputPath, outputPath, listener);

        DestroyProcessor(mNativeObject);

        mNativeObject = INVALID_OBJECT;
    }

    native long CreateProcessor();
    native boolean ProcessFile(long processorObj, String inputPath, String outputPath, NativeProcessorProgressListener progressListener);
    native void DestroyProcessor(long processorObj);
    native String GetLastError();
}
