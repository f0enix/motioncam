package com.motioncam.processor;

import android.app.IntentService;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.ResultReceiver;
import android.provider.MediaStore;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import androidx.core.app.NotificationCompat;

import com.motioncam.R;

import org.apache.commons.io.FileUtils;
import org.apache.commons.io.IOUtil;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
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
        private final Context mContext;
        private final File mPendingFile;
        private final File mTempFileJpeg;
        private final File mTempFileDng;
        private final File mOutputDirectory;
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

        ProcessFile(Context context, File pendingFile, File tempPath, boolean deleteAfterProcessing, ResultReceiver receiver) {
            mContext = context;
            mNativeProcessor = new NativeProcessor();
            mReceiver = receiver;
            mPendingFile = pendingFile;
            mDeleteAfterProcessing = deleteAfterProcessing;
            mNotifyManager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);

            String outputFileNameJpeg = fileNoExtension(pendingFile.getName()) + ".jpg";
            String outputFileNameDng = fileNoExtension(pendingFile.getName()) + ".dng";

            mTempFileJpeg = new File(tempPath, outputFileNameJpeg);
            mTempFileDng = new File(tempPath, outputFileNameDng);

            // Set up the output path to point to the camera DCIM folder
            File dcimDirectory = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DCIM);

            mOutputDirectory = new File(dcimDirectory, "Camera");
            mOutputFileJpeg = new File(mOutputDirectory, outputFileNameJpeg);
            mOutputFileDng = new File(mOutputDirectory, outputFileNameDng);

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

            // Copy to media store
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                if (mTempFileDng.exists())
                    saveToMediaStore(mTempFileDng);

                if (mTempFileJpeg.exists())
                    saveToMediaStore(mTempFileJpeg);
            }
            // Legacy copy file
            else {
                if(!mOutputDirectory.exists()) {
                    if(!mOutputDirectory.mkdirs()) {
                        Log.e(TAG, "Failed to create " + mOutputDirectory);
                        throw new IOException("Failed to create output directory");
                    }
                }

                if(mTempFileDng.exists()) {
                    FileUtils.copyFile(mTempFileDng, mOutputFileDng);
                    mTempFileDng.delete();
                }

                if(mTempFileJpeg.exists()) {
                    FileUtils.copyFile(mTempFileJpeg, mOutputFileJpeg);
                    mTempFileJpeg.delete();
                }
            }

            return null;
        }

        @RequiresApi(api = Build.VERSION_CODES.Q)
        private void saveToMediaStore(File inputFile) throws IOException {
            ContentResolver resolver = mContext.getApplicationContext().getContentResolver();

            Uri imageCollection;

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                imageCollection = MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
            } else {
                imageCollection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
            }

            ContentValues imageDetails = new ContentValues();

            imageDetails.put(MediaStore.Images.Media.DISPLAY_NAME,  inputFile.getName());
            imageDetails.put(MediaStore.Images.Media.MIME_TYPE,     "image/jpeg");
            imageDetails.put(MediaStore.Images.Media.DATE_ADDED,    System.currentTimeMillis());
            imageDetails.put(MediaStore.Images.Media.DATE_TAKEN,    System.currentTimeMillis());
            imageDetails.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_DCIM);
            imageDetails.put(MediaStore.Images.Media.IS_PENDING,    1);

            Uri imageContentUri = resolver.insert(imageCollection, imageDetails);

            try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(imageContentUri, "w", null)) {
                FileOutputStream outStream = new FileOutputStream(pfd.getFileDescriptor());

                IOUtil.copy(new FileInputStream(inputFile), outStream);
            }

            imageDetails.clear();
            imageDetails.put(MediaStore.Images.Media.IS_PENDING, 0);

            resolver.update(imageContentUri, imageDetails, null, null);
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

        if(pendingFiles == null)
            return;

        Log.i(TAG, "Found " + pendingFiles.length + " images to process");

        File tmpDirectory = new File(getFilesDir(), "tmp");

        // Create temporary directory
        if(!tmpDirectory.exists()) {
            if(!tmpDirectory.mkdirs()) {
                Log.e(TAG, "Failed to create " + tmpDirectory);
                return;
            }
        }

        // Process all files
        for(File file : pendingFiles) {
            ProcessFile processFile = new ProcessFile(getApplicationContext(), file, tmpDirectory, deleteAfterProcessing, receiver);

            try {
                processFile.call();
            }
            catch (IOException e) {
                e.printStackTrace();
            }
        }
    }
}
