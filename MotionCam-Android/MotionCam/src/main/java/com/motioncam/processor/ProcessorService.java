package com.motioncam.processor;

import android.app.IntentService;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.BitmapFactory;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.PowerManager;
import android.os.ResultReceiver;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;

import com.motioncam.R;

import org.apache.commons.io.FileUtils;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.Callable;

public class ProcessorService extends IntentService {
    static final String TAG = "MotionCamService";

    public static final String METADATA_PATH_KEY                = "metadataPath";
    public static final String DELETE_AFTER_PROCESSING_KEY      = "deleteAfterProcessing";
    public static final String RECEIVER_KEY                     = "receiver";

    public static final int NOTIFICATION_ID                     = 1;
    public static final String NOTIFICATION_CHANNEL_ID          = "MotionCamNotification";

    static class ProcessFile implements Callable<Void>, NativeProcessorProgressListener {
        private final File mPendingFile;
        private final File mTempFileJpeg;
        private final File mTempFileDng;
        private final File mOutputFileJpeg;
        private final File mOutputFileDng;
        private final NotificationManager mNotifyManager;
        private final NotificationCompat.Builder mBuilder;
        private final ResultReceiver mReceiver;
        private final NativeProcessor mNativeProcessor;
        private final boolean mDeleteAfterProcessing;

        static private String fileNoExtension(String filename) {
            int pos = filename.lastIndexOf(".");
            if (pos > 0) {
                return  filename.substring(0, pos);
            }

            return filename;
        }

        ProcessFile(Context context, File pendingFile, File tempPath, File outputPath, boolean deleteAfterProcessing, ResultReceiver receiver) {
            mNativeProcessor = new NativeProcessor();
            mReceiver = receiver;
            mPendingFile = pendingFile;
            mDeleteAfterProcessing = deleteAfterProcessing;
            mNotifyManager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);

            String outputFileNameJpeg = fileNoExtension(pendingFile.getName()) + ".jpg";
            String outputFileNameDng = fileNoExtension(pendingFile.getName()) + ".dng";

            mTempFileJpeg = new File(tempPath, outputFileNameJpeg);
            mTempFileDng = new File(tempPath, outputFileNameDng);

            mOutputFileJpeg = new File(outputPath, outputFileNameJpeg);
            mOutputFileDng = new File(outputPath, outputFileNameDng);

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                NotificationChannel notificationChannel = new NotificationChannel(
                        NOTIFICATION_CHANNEL_ID,
                        "Motion Cam Notification",
                        NotificationManager.IMPORTANCE_MIN);

                // Configure the notification channel.
                notificationChannel.setDescription("Motion Cam process service");
                notificationChannel.enableLights(false);
                notificationChannel.enableVibration(false);
                notificationChannel.setImportance(NotificationManager.IMPORTANCE_MIN);

                mNotifyManager.createNotificationChannel(notificationChannel);
            }

            mBuilder = new NotificationCompat.Builder(context, NOTIFICATION_CHANNEL_ID);
            mBuilder.setContentTitle("Motion Cam")
                    .setContentText("Processing Image")
                    .setLargeIcon(BitmapFactory.decodeResource(context.getResources(), R.mipmap.icon))
                    .setSmallIcon(R.drawable.ic_processing_notification);

            mBuilder.setProgress(100, 0, false);
            mNotifyManager.notify(NOTIFICATION_ID, mBuilder.build());
        }

        @Override
        public Void call() throws IOException {
            if(mReceiver != null) {
                Bundle bundle = new Bundle();

                bundle.putInt(ProcessorReceiver.PROCESS_CODE_PROGRESS_VALUE_KEY, 0);
                bundle.putString(ProcessorReceiver.PROCESS_CODE_OUTPUT_FILE_PATH_KEY, mPendingFile.getPath());

                mReceiver.send(ProcessorReceiver.PROCESS_CODE_STARTED, Bundle.EMPTY);
            }

            mNativeProcessor.processFile(mPendingFile.getPath(), mTempFileJpeg.getPath(), this);

            if(mDeleteAfterProcessing)
                mPendingFile.delete();
            else
                mPendingFile.renameTo(new File(mPendingFile.getPath() + ".complete"));

            // Copy to output folder
            if(mTempFileDng.exists())
                FileUtils.copyFile(mTempFileDng, mOutputFileDng);

            FileUtils.copyFile(mTempFileJpeg, mOutputFileJpeg);

            return null;
        }

        @Override
        public boolean onProgressUpdate(int progress) {
            if(mReceiver != null) {
                Bundle bundle = new Bundle();

                bundle.putInt(ProcessorReceiver.PROCESS_CODE_PROGRESS_VALUE_KEY, progress);
                bundle.putString(ProcessorReceiver.PROCESS_CODE_OUTPUT_FILE_PATH_KEY, mOutputFileJpeg.getPath());

                mReceiver.send(ProcessorReceiver.PROCESS_CODE_PROGRESS, bundle);
            }

            mBuilder.setProgress(100, progress, false);
            mNotifyManager.notify(NOTIFICATION_ID, mBuilder.build());

            return true;
        }

        @Override
        public void onCompleted() {
            if(mReceiver != null) {
                Bundle bundle = new Bundle();

                bundle.putInt(ProcessorReceiver.PROCESS_CODE_PROGRESS_VALUE_KEY, 100);
                bundle.putString(ProcessorReceiver.PROCESS_CODE_OUTPUT_FILE_PATH_KEY, mOutputFileJpeg.getPath());

                mReceiver.send(ProcessorReceiver.PROCESS_CODE_COMPLETED, bundle);
            }

            mNotifyManager.cancel(NOTIFICATION_ID);
        }

        @Override
        public void onError(String error) {
            Log.e(TAG, "Error processing image " + mOutputFileJpeg + " error: " + error);
            mNotifyManager.cancel(NOTIFICATION_ID);
        }
    }

    public ProcessorService() {
        super("MotionCamService");
    }

    @Override
    protected void onHandleIntent(@Nullable Intent intent) {
        if(intent == null) {
            return;
        }

        String metadataPath = intent.getStringExtra(METADATA_PATH_KEY);
        if(metadataPath == null) {
            Log.i(TAG, "Process service can't find base path");
            return;
        }

        boolean deleteAfterProcessing = intent.getBooleanExtra(DELETE_AFTER_PROCESSING_KEY, true);

        // Set receiver if we get one
        ResultReceiver receiver = intent.getParcelableExtra(RECEIVER_KEY);

        // Find all pending files and process them
        File root = new File(metadataPath);
        File[] pendingFiles = root.listFiles((dir, name) -> name.toLowerCase().endsWith("zip"));

        Log.i(TAG, "Found " + pendingFiles.length + " images to process");

        File dcimDirectory = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DCIM);
        File outputDirectory = new File(dcimDirectory, "Camera");
        File tmpDirectory = new File(getFilesDir(), "tmp");

        // Create final output directory
        if(!outputDirectory.exists()) {
            if(!outputDirectory.mkdirs()) {
                Log.e(TAG, "Failed to create " + outputDirectory);
                return;
            }
        }

        // Create temporary directory
        if(!tmpDirectory.exists()) {
            if(!tmpDirectory.mkdirs()) {
                Log.e(TAG, "Failed to create " + tmpDirectory);
                return;
            }
        }

        // Process all files
        PowerManager powerManager = (PowerManager) getSystemService(POWER_SERVICE);
        PowerManager.WakeLock wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "MotionCam::ProcessingLockTag");

        try
        {
            wakeLock.acquire();

            for(File file : pendingFiles) {
                ProcessFile processFile = new ProcessFile(getApplicationContext(), file, tmpDirectory, outputDirectory, deleteAfterProcessing, receiver);

                try {
                    processFile.call();
                }
                catch (IOException e) {
                    e.printStackTrace();
                }
            }
        }
        finally {
            wakeLock.release();
        }
    }
}
