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

import com.motioncam.BuildConfig;
import com.motioncam.R;
import com.motioncam.model.CameraProfile;

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
    public static final String RECEIVER_KEY                     = "receiver";

    public static final int NOTIFICATION_ID                     = 1;
    public static final String NOTIFICATION_CHANNEL_ID          = "MotionCamNotification";

    static class ProcessFile implements Callable<Boolean>, NativeProcessorProgressListener {
        private final Context mContext;
        private final File mRawContainerPath;
        private final boolean mProcessInMemory;
        private final File mTempFileJpeg;
        private final File mTempFileDng;
        private final String mOutputFileNameJpeg;
        private final String mOutputFileNameDng;
        private final NotificationManager mNotifyManager;
        private final ResultReceiver mReceiver;
        private final NativeProcessor mNativeProcessor;

        private NotificationCompat.Builder mBuilder;

        static private String fileNoExtension(String filename) {
            int pos = filename.lastIndexOf(".");
            if (pos > 0) {
                return  filename.substring(0, pos);
            }

            return filename;
        }

        ProcessFile(Context context, File rawContainerPath, File tempPath, boolean processInMemory, ResultReceiver receiver) {
            mContext = context;
            mNativeProcessor = new NativeProcessor();
            mReceiver = receiver;
            mRawContainerPath = rawContainerPath;
            mProcessInMemory = processInMemory;
            mNotifyManager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);

            mOutputFileNameJpeg = fileNoExtension(rawContainerPath.getName()) + ".jpg";
            mOutputFileNameDng = fileNoExtension(rawContainerPath.getName()) + ".dng";

            mTempFileJpeg = new File(tempPath, mOutputFileNameJpeg);
            mTempFileDng = new File(tempPath, mOutputFileNameDng);
        }

        @Override
        public Boolean call() throws IOException {
            if(mProcessInMemory) {
                if (!mNativeProcessor.processInMemory(mTempFileJpeg.getPath(), this)) {
                    Log.d(TAG, "No in-memory container found");
                    return false;
                }
            }
            else {
                mNativeProcessor.processFile(mRawContainerPath.getPath(), mTempFileJpeg.getPath(), this);
            }

            if(mReceiver != null) {
                Bundle bundle = new Bundle();

                bundle.putInt(ProcessorReceiver.PROCESS_CODE_PROGRESS_VALUE_KEY, 0);
                bundle.putString(ProcessorReceiver.PROCESS_CODE_OUTPUT_FILE_PATH_KEY, mRawContainerPath.getPath());

                mReceiver.send(ProcessorReceiver.PROCESS_CODE_STARTED, Bundle.EMPTY);
            }

            // Copy to media store
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                if(BuildConfig.DEBUG && !mProcessInMemory)
                    saveToFiles(mRawContainerPath, "application/zip", Environment.DIRECTORY_DOCUMENTS);

                if (mTempFileDng.exists()) {
                    saveToMediaStore(mTempFileDng, "image/x-adobe-dng", Environment.DIRECTORY_DCIM + File.separator + "Camera");
                    mTempFileDng.delete();
                }

                if (mTempFileJpeg.exists()) {
                    saveToMediaStore(mTempFileJpeg, "image/jpeg", Environment.DIRECTORY_DCIM + File.separator + "Camera");
                    mTempFileJpeg.delete();
                }
            }
            // Legacy copy file
            else {
                // Set up the output path to point to the camera DCIM folder
                File dcimDirectory = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DCIM);
                File outputDirectory = new File(dcimDirectory, "Camera");

                if(!outputDirectory.exists()) {
                    if(!outputDirectory.mkdirs()) {
                        Log.e(TAG, "Failed to create " + outputDirectory);
                        throw new IOException("Failed to create output directory");
                    }
                }

                if(mTempFileDng.exists()) {
                    File outputFileDng = new File(outputDirectory, mOutputFileNameDng);

                    Log.e(TAG, "Writing to " + outputFileDng.getPath());

                    FileUtils.copyFile(mTempFileDng, outputFileDng);
                    mTempFileDng.delete();
                }

                if(mTempFileJpeg.exists()) {
                    File outputFileJpeg = new File(outputDirectory, mOutputFileNameJpeg);

                    Log.e(TAG, "Writing to " + outputFileJpeg.getPath());

                    FileUtils.copyFile(mTempFileJpeg, outputFileJpeg);
                    mTempFileJpeg.delete();

                    Uri uri = Uri.fromFile(outputFileJpeg);
                    mContext.sendBroadcast(new Intent(Intent.ACTION_MEDIA_SCANNER_SCAN_FILE, uri));
                }
            }

            if(!mProcessInMemory)
                mRawContainerPath.delete();

            return true;
        }

        @RequiresApi(api = Build.VERSION_CODES.Q)
        private void saveToFiles(File inputFile, String mimeType, String relativePath) throws IOException {
            ContentResolver resolver = mContext.getApplicationContext().getContentResolver();

            Uri collection = MediaStore.Files.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
            ContentValues details = new ContentValues();

            details.put(MediaStore.Files.FileColumns.DISPLAY_NAME,  inputFile.getName());
            details.put(MediaStore.Files.FileColumns.MIME_TYPE,     mimeType);
            details.put(MediaStore.Files.FileColumns.DATE_ADDED,    System.currentTimeMillis());
            details.put(MediaStore.Files.FileColumns.DATE_TAKEN,    System.currentTimeMillis());
            details.put(MediaStore.Files.FileColumns.RELATIVE_PATH, relativePath);
            details.put(MediaStore.Files.FileColumns.IS_PENDING,    1);

            Uri imageContentUri = resolver.insert(collection, details);

            try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(imageContentUri, "w", null)) {
                FileOutputStream outStream = new FileOutputStream(pfd.getFileDescriptor());

                IOUtil.copy(new FileInputStream(inputFile), outStream);
            }

            details.clear();
            details.put(MediaStore.Images.Media.IS_PENDING, 0);

            resolver.update(imageContentUri, details, null, null);
        }

        @RequiresApi(api = Build.VERSION_CODES.Q)
        private void saveToMediaStore(File inputFile, String mimeType, String relativePath) throws IOException {
            ContentResolver resolver = mContext.getApplicationContext().getContentResolver();

            Uri imageCollection;

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                imageCollection = MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
            } else {
                imageCollection = MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
            }

            ContentValues imageDetails = new ContentValues();

            imageDetails.put(MediaStore.Images.Media.DISPLAY_NAME,  inputFile.getName());
            imageDetails.put(MediaStore.Images.Media.MIME_TYPE,     mimeType);
            imageDetails.put(MediaStore.Images.Media.DATE_ADDED,    System.currentTimeMillis());
            imageDetails.put(MediaStore.Images.Media.DATE_TAKEN,    System.currentTimeMillis());
            imageDetails.put(MediaStore.Images.Media.RELATIVE_PATH, relativePath);
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

                mReceiver.send(ProcessorReceiver.PROCESS_CODE_PROGRESS, bundle);
            }

            if(mBuilder == null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    NotificationChannel notificationChannel = new NotificationChannel(
                            NOTIFICATION_CHANNEL_ID,
                            "MotionCam Notification",
                            NotificationManager.IMPORTANCE_MIN);

                    // Configure the notification channel.
                    notificationChannel.setDescription("MotionCam process service");
                    notificationChannel.enableLights(false);
                    notificationChannel.enableVibration(false);
                    notificationChannel.setImportance(NotificationManager.IMPORTANCE_MIN);

                    mNotifyManager.createNotificationChannel(notificationChannel);
                }

                mBuilder = new NotificationCompat.Builder(mContext, NOTIFICATION_CHANNEL_ID);
                mBuilder.setContentTitle("MotionCam")
                        .setContentText("Processing Image")
                        .setLargeIcon(BitmapFactory.decodeResource(mContext.getResources(), R.mipmap.icon))
                        .setSmallIcon(R.drawable.ic_processing_notification);
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
                mReceiver.send(ProcessorReceiver.PROCESS_CODE_COMPLETED, bundle);
            }

            mNotifyManager.cancel(NOTIFICATION_ID);
        }

        @Override
        public void onError(String error) {
            Log.e(TAG, "Error processing image " + mOutputFileNameJpeg + " error: " + error);
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

        // Set receiver if we get one
        ResultReceiver receiver = intent.getParcelableExtra(RECEIVER_KEY);

        File tmpDirectory = new File(getFilesDir(), "tmp");

        // Create temporary directory
        if(!tmpDirectory.exists()) {
            if(!tmpDirectory.mkdirs()) {
                Log.e(TAG, "Failed to create " + tmpDirectory);
                return;
            }
        }

        File root = new File(metadataPath);

        // Process all in-memory requests first
        boolean moreToProcess = true;

        while(moreToProcess) {
            File inMemoryTmp = CameraProfile.generateCaptureFile(getApplicationContext());
            ProcessFile inMemoryProcess = new ProcessFile(getApplicationContext(), inMemoryTmp, tmpDirectory, true, receiver);

            try {
                Log.d(TAG, "Processing in-memory container");
                moreToProcess = inMemoryProcess.call();
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to process in-memory container", e);
                moreToProcess = false;
            }
        }

        // Find all pending files and process them
        File[] pendingFiles = root.listFiles((dir, name) -> name.toLowerCase().endsWith("zip"));
        if(pendingFiles == null)
            return;

        // Process all files
        for(File file : pendingFiles) {
            ProcessFile processFile = new ProcessFile(getApplicationContext(), file, tmpDirectory, false, receiver);

            try {
                Log.d(TAG, "Processing " + file.getPath());
                processFile.call();
            }
            catch (Exception e) {
                Log.e(TAG, "Failed to process " + file.getPath(), e);
                file.delete();
            }
        }
    }
}
