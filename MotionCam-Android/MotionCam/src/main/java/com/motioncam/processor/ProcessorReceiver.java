package com.motioncam.processor;

import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.ResultReceiver;
import android.util.Log;

import java.io.File;

import static com.motioncam.processor.ProcessorService.TAG;

public class ProcessorReceiver extends ResultReceiver {
    private Receiver mReceiver;

    final static int PROCESS_CODE_STARTED       = 1000;
    final static int PROCESS_CODE_PROGRESS      = 1001;
    final static int PROCESS_CODE_PREVIEW_READY = 1002;
    final static int PROCESS_CODE_COMPLETED     = 1003;

    final static String PROCESS_CODE_OUTPUT_FILE_PATH_KEY = "outputFilePath";
    final static String PROCESS_CODE_CONTENT_URI_KEY = "contentUri";
    final static String PROCESS_CODE_PROGRESS_VALUE_KEY = "progressValue";

    public ProcessorReceiver(Handler handler) {
        super(handler);
    }

    public interface Receiver {
        void onPreviewSaved(String ouputPath);
        void onProcessingStarted();
        void onProcessingProgress(int progress);
        void onProcessingCompleted(File internalPath, Uri contentUri);

    }

    public void setReceiver(Receiver receiver) {
        mReceiver = receiver;
    }

    @Override
    protected void onReceiveResult(int resultCode, Bundle resultData) {
        if (mReceiver == null)
            return;

        Log.d(TAG, "onReceiveResult(" + resultCode + ")");

        switch(resultCode)
        {
            case PROCESS_CODE_STARTED: {
                mReceiver.onProcessingStarted();
            }
            break;

            case PROCESS_CODE_PREVIEW_READY: {
                String outputPath = resultData.getString(PROCESS_CODE_OUTPUT_FILE_PATH_KEY);
                mReceiver.onPreviewSaved(outputPath);
            }
            break;

            case PROCESS_CODE_PROGRESS: {
                int progress = resultData.getInt(PROCESS_CODE_PROGRESS_VALUE_KEY, 0);
                mReceiver.onProcessingProgress(progress);
            }
            break;

            case PROCESS_CODE_COMPLETED: {
                String outputPath = resultData.getString(PROCESS_CODE_OUTPUT_FILE_PATH_KEY);
                String contentUri = resultData.getString(PROCESS_CODE_CONTENT_URI_KEY);

                mReceiver.onProcessingCompleted(new File(outputPath), Uri.parse(contentUri));
            }
            break;

            default:
                break;
        }
    }
}
