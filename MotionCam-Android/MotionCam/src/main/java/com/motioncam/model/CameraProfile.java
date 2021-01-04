package com.motioncam.model;

import android.os.Environment;

import java.io.File;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.Random;

public class CameraProfile {
    private static final String CAPTURE_OUTPUT_PATH_NAME = "motionCam";
    private static final SimpleDateFormat OUTPUT_DATE_FORMAT = new SimpleDateFormat("yyyyMMdd_Hms", Locale.forLanguageTag("en-US"));
    private static final Random RANDOM = new Random();

    static private String generateFilename() {
        return String.format(
                Locale.getDefault(),
                "IMG_%s%d.zip",
                OUTPUT_DATE_FORMAT.format(new Date()), RANDOM.nextInt(Short.MAX_VALUE));
    }

    static public File getRootOutputPath() {
        File root = new File(
                Environment.getExternalStorageDirectory().getPath() +
                        java.io.File.separator +
                        CameraProfile.CAPTURE_OUTPUT_PATH_NAME);

        if(!root.exists() && !root.mkdirs()) {
            throw new IllegalStateException("Can't create output directory");
        }

        return root;
    }

    static public File generateCaptureFile() {
        return  new File(getRootOutputPath(), CameraProfile.generateFilename());
    }

    public static final int DEFAULT_JPEG_QUALITY = 95;

    // Light
    public static final int DEFAULT_CONTRAST = 50;
    public static final int DEFAULT_WHITE_POINT = 50;

    // Saturation
    public static final int DEFAULT_SATURATION = 50;
    public static final int DEFAULT_GREEN_SATURATION = 60;
    public static final int DEFAULT_BLUE_SATURATION = 60;

    // Detail
    public static final int DEFAULT_SHARPNESS = 60;
    public static final int DEFAULT_DETAIL = 20;

    // Denoise
    public static final float DEFAULT_CHROMA_EPS = 4;
}
