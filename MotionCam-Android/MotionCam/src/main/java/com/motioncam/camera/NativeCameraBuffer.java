package com.motioncam.camera;

public class NativeCameraBuffer implements Comparable<NativeCameraBuffer> {
    public enum ScreenOrientation {
        PORTRAIT(0, 0),
        REVERSE_PORTRAIT(1, 180),
        LANDSCAPE(2, 90),
        REVERSE_LANDSCAPE(3, -90);

        public final int value;
        public final int angle;

        ScreenOrientation(int value, int angle) {
            this.value = value;
            this.angle = angle;
        }

        static ScreenOrientation FromInt(int screenOrientation) {
            for(ScreenOrientation orientation : ScreenOrientation.values()) {
                if(orientation.value == screenOrientation)
                    return orientation;
            }

            // Default to landscape
            return LANDSCAPE;
        }
    }

    public final long timestamp;
    public final int iso;
    public final long exposureTime;
    public final ScreenOrientation screenOrientation;
    public final int width;
    public final int height;

    public NativeCameraBuffer(long timestamp, int iso, long exposureTime, int screenOrientation, int width, int height) {
        this.timestamp = timestamp;
        this.iso = iso;
        this.exposureTime = exposureTime;
        this.screenOrientation = ScreenOrientation.FromInt(screenOrientation);
        this.width = width;
        this.height = height;
    }

    @Override
    public int compareTo(NativeCameraBuffer o) {
        return Long.compare(this.timestamp, o.timestamp);
    }
}
