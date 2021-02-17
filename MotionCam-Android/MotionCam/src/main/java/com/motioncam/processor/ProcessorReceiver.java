package com.motioncam.processor;

import android.os.Bundle;
import android.os.Handler;
import android.os.ResultReceiver;

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
        void onProcessingStarted();
        void onProcessingProgress(int progress);
        void onProcessingCompleted();

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
                mReceiver.onProcessingStarted();
            }
            break;

            case PROCESS_CODE_PROGRESS: {
                int progress = resultData.getInt(PROCESS_CODE_PROGRESS_VALUE_KEY, 0);
                mReceiver.onProcessingProgress(progress);
            }
            break;

            case PROCESS_CODE_COMPLETED: {
                mReceiver.onProcessingCompleted();
            }
            break;

            default:
                break;
        }
    }
}
