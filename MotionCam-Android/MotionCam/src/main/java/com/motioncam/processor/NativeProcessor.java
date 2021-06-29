package com.motioncam.processor;

public class NativeProcessor {
    public boolean processInMemory(String outputPath, NativeProcessorProgressListener listener) {
        boolean result = ProcessInMemory(outputPath, listener);

        return result;
    }

    public void processFile(String inputPath, String outputPath, NativeProcessorProgressListener listener) {
        ProcessFile(inputPath, outputPath, listener);
    }

    native boolean ProcessInMemory(String outputPath, NativeProcessorProgressListener progressListener);
    native boolean ProcessFile(String inputPath, String outputPath, NativeProcessorProgressListener progressListener);
    native String GetLastError();
}
