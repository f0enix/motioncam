package com.motioncam.processor;

import android.os.Bundle;
import android.os.Handler;
import android.os.ResultReceiver;

import java.io.File;

public class ProcessorReceiver extends ResultReceiver {
    private Receiver mReceiver;

    final static int PROCESS_CODE_STARTED   = 1000;
    final static int PROCESS_CODE_PROGRESS  = 1001;
    final static int PROCESS_CODE_COMPLETED = 1002;

    final static String PROCESS_CODE_OUTPUT_FILE_PATH_KEY = "outputFilePath";
    final static String PROCESS_CODE_PROGRESS_VALUE_KEY = "progressValue";

    public ProcessorReceiver(Handler handler) {
        super(handler);
    }

    public interface Receiver {
        void onProcessingStarted(File file);
        void onProcessingProgress(File file, int progress);
        void onProcessingCompleted(File file);

    }

    public void setReceiver(Receiver receiver) {
        mReceiver = receiver;
    }

    @Override
    protected void onReceiveResult(int resultCode, Bundle resultData) {
        if (mReceiver == null)
            return;

        switch(resultCode)
        {
            case PROCESS_CODE_STARTED: {
                String filePath = resultData.getString(PROCESS_CODE_OUTPUT_FILE_PATH_KEY, "");
                mReceiver.onProcessingStarted(new File(filePath));
            }
            break;

            case PROCESS_CODE_PROGRESS: {
                int progress = resultData.getInt(PROCESS_CODE_PROGRESS_VALUE_KEY, 0);
                String filePath = resultData.getString(PROCESS_CODE_OUTPUT_FILE_PATH_KEY, "");

                mReceiver.onProcessingProgress(new File(filePath), progress);
            }
            break;

            case PROCESS_CODE_COMPLETED: {
                String filePath = resultData.getString(PROCESS_CODE_OUTPUT_FILE_PATH_KEY, "");
                mReceiver.onProcessingCompleted(new File(filePath));
            }
            break;

            default:
                break;
        }
    }
}
