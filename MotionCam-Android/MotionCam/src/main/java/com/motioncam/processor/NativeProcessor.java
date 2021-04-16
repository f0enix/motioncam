package com.motioncam.processor;

public class NativeProcessor {
    private static final long INVALID_OBJECT = -1;

    public boolean processInMemory(String outputPath, NativeProcessorProgressListener listener) {
        long handle = CreateProcessor();
        if(handle == INVALID_OBJECT) {
            throw new IllegalStateException(GetLastError());
        }

        boolean result = ProcessInMemory(handle, outputPath, listener);

        DestroyProcessor(handle);

        return result;
    }

    public void processFile(String inputPath, String outputPath, NativeProcessorProgressListener listener) {
        long handle = CreateProcessor();
        if(handle == INVALID_OBJECT) {
            throw new IllegalStateException(GetLastError());
        }

        ProcessFile(handle, inputPath, outputPath, listener);

        DestroyProcessor(handle);
    }

    native long CreateProcessor();
    native boolean ProcessInMemory(long processorObj, String outputPath, NativeProcessorProgressListener progressListener);
    native boolean ProcessFile(long processorObj, String inputPath, String outputPath, NativeProcessorProgressListener progressListener);
    native void DestroyProcessor(long processorObj);
    native String GetLastError();
}
