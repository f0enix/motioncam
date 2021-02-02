package com.motioncam.camera;

import java.util.ArrayList;
import java.util.List;

public class CameraManualControl {

    public enum ISO {
        ISO_100(100),
        ISO_125(125),
        ISO_160(160),
        ISO_200(200),
        ISO_250(250),
        ISO_320(320),
        ISO_400(400),
        ISO_500(500),
        ISO_640(640),
        ISO_800(800),
        ISO_1000(1000),
        ISO_1250(1250),
        ISO_1600(1600),
        ISO_2000(2000),
        ISO_2500(2500),
        ISO_3200(3200),
        ISO_6400(6400);

        private final int value;

        ISO(int value)
        {
            this.value = value;
        }

        public int getIso() {
            return value;
        }

        @Override
        public String toString() {
            return String.valueOf(value);
        }
    }

    public enum SHUTTER_SPEED {
        EXPOSURE_1_8000(125000, "1/8000"),
        EXPOSURE_1_6400(156250, "1/6400"),
        EXPOSURE_1_5000(200000, "1/5000"),
        EXPOSURE_1_4000(250000, "1/4000"),
        EXPOSURE_1_3200(312500, "1/3200"),
        EXPOSURE_1_2500(400000, "1/2500"),
        EXPOSURE_1_2000(500000, "1/2000"),
        EXPOSURE_1_1600(625000, "1/1600"),
        EXPOSURE_1_1250(800000, "1/1250"),
        EXPOSURE_1_1000(1000000, "1/1000"),
        EXPOSURE_1_800(1250000, "1/800"),
        EXPOSURE_1_640(1562500, "1/640"),
        EXPOSURE_1_500(2000000, "1/500"),
        EXPOSURE_1_400(2500000, "1/400"),
        EXPOSURE_1_320(3125000, "1/320"),
        EXPOSURE_1_250(4000000, "1/250"),
        EXPOSURE_1_200(5000000, "1/200"),
        EXPOSURE_1_160(6250000, "1/160"),
        EXPOSURE_1_125(8000000, "1/125"),
        EXPOSURE_1_100(10000000, "1/100"),
        EXPOSURE_1_80(12500000, "1/80"),
        EXPOSURE_1_60(16666667, "1/60"),
        EXPOSURE_1_50(20000000, "1/50"),
        EXPOSURE_1_40(25000000, "1/40"),
        EXPOSURE_1_30(33333333, "1/30"),
        EXPOSURE_1_25(40000000, "1/25"),
        EXPOSURE_1_20(50000000, "1/20"),
        EXPOSURE_1_15(66666667, "1/15"),
        EXPOSURE_1_13(76923077, "1/13"),
        EXPOSURE_1_10(100000000, "1/10"),
        EXPOSURE_1_8(125000000, "1/8"),
        EXPOSURE_1_6(166666667, "1/6"),
        EXPOSURE_1_5(200000000, "1/5"),
        EXPOSURE_1_4(250000000, "1/4"),
        EXPOSURE_0__4(400000000, "0.4"),
        EXPOSURE_0__5(500000000, "0.5"),
        EXPOSURE_0__6(600000000, "0.6"),
        EXPOSURE_0__8(800000000, "0.8"),
        EXPOSURE_1__0(1000000000, "1.0"),
        EXPOSURE_1__3(1300000000, "1.3"),
        EXPOSURE_1__6(1600000000, "1.6"),
        EXPOSURE_2__0(2000000000, "2.0"),
        EXPOSURE_2__5(2500000000L, "2.5"),
        EXPOSURE_3__0(3000000000L, "3.0"),
        EXPOSURE_4__0(4000000000L, "4.0"),
        EXPOSURE_5__0(5000000000L, "5.0"),
        EXPOSURE_6__0(6000000000L, "6.0"),
        EXPOSURE_8__0(8000000000L, "8.0"),
        EXPOSURE_10__0(10000000000L, "10.0"),
        EXPOSURE_13__0(13000000000L, "13.0"),
        EXPOSURE_15__0(15000000000L, "15.0"),
        EXPOSURE_20__0(20000000000L, "20.0"),
        EXPOSURE_25__0(25000000000L, "25.0"),
        EXPOSURE_30__0(30000000000L, "30.0");

        private final long value;
        private final String display;

        SHUTTER_SPEED(long value, String display)
        {
            this.value = value;
            this.display = display;
        }

        public long getExposureTime() {
            return value;
        }

        @Override
        public String toString() {
            return display;
        }
    }

    public static class Exposure {
        Exposure(SHUTTER_SPEED shutterSpeed, ISO iso) {
            this.iso = iso;
            this.shutterSpeed = shutterSpeed;
        }

        public static Exposure Create(SHUTTER_SPEED shutterSpeed, ISO iso) {
            return new Exposure(shutterSpeed, iso);
        }

        private double log2(double x) {
            return Math.log(x)/Math.log(2);
        }

        double getEv(double cameraAperture) {
            double t = shutterSpeed.value / (1000.0*1000.0*1000.0);
            double ev100 = log2(cameraAperture*cameraAperture / t);

            return ev100 - log2(iso.value / 100.0);
        }

        public final ISO iso;
        public final SHUTTER_SPEED shutterSpeed;
    }

    private static Exposure[] EXPOSURE_LINE = new Exposure[] {
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_8000, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_6400, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_5000, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_4000, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_3200, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_2500, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_2000, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_1600, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_1250, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_1000, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_800, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_640, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_500, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_400, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_320, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_250, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_200, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_160, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_125, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_100, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_80, ISO.ISO_100),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_60, ISO.ISO_100),

            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_60, ISO.ISO_125),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_60, ISO.ISO_160),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_60, ISO.ISO_200),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_60, ISO.ISO_250),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_60, ISO.ISO_320),

            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_60, ISO.ISO_400),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_50, ISO.ISO_400),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_40, ISO.ISO_400),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_30, ISO.ISO_400),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_25, ISO.ISO_400),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_15, ISO.ISO_400),

            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_15, ISO.ISO_800),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_10, ISO.ISO_800),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_10, ISO.ISO_1000),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_10, ISO.ISO_1250),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_10, ISO.ISO_1250),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_10, ISO.ISO_1600),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_10, ISO.ISO_2000),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_10, ISO.ISO_2500),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_10, ISO.ISO_3200),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_8, ISO.ISO_3200),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_6, ISO.ISO_3200),
            Exposure.Create(SHUTTER_SPEED.EXPOSURE_1_4, ISO.ISO_3200),
    };

    public static Exposure MapToExposureLine(double cameraAperture, Exposure exposure) {
        double min = 1e10;
        Exposure result = exposure;

        for(Exposure srcExposure : EXPOSURE_LINE) {
            double d = Math.abs(srcExposure.getEv(cameraAperture) - exposure.getEv(cameraAperture));
            if(d < min) {
                result = srcExposure;
                min = d;
            }
        }

        return result;
    }

    public static List<SHUTTER_SPEED> GetExposureValuesInRange(long min, long max) {
        List<SHUTTER_SPEED> shutterSpeeds = new ArrayList<>();

        for(SHUTTER_SPEED shutterSpeed : SHUTTER_SPEED.values())
        {
            if(shutterSpeed.value >= min && shutterSpeed.value <= max)
                shutterSpeeds.add(shutterSpeed);
        }

        return shutterSpeeds;
    }

    public static List<ISO> GetIsoValuesInRange(int min, int max) {
        List<ISO> isoValues = new ArrayList<>();

        for(ISO iso : ISO.values())
        {
            if(iso.value >= min && iso.value <= max)
                isoValues.add(iso);
        }

        return isoValues;
    }

    public static ISO GetClosestIso(List<ISO> isoList, int iso) {
        ISO bestIsoMatch = ISO.ISO_100;

        for(ISO currentIso : isoList) {
            if(Math.abs(currentIso.value - iso) < Math.abs(bestIsoMatch.value - iso)) {
                bestIsoMatch = currentIso;
            }
        }

        return bestIsoMatch;
    }

    public static SHUTTER_SPEED GetClosestShutterSpeed(long exposureTime) {
        SHUTTER_SPEED bestShutterSpeedMatch = SHUTTER_SPEED.EXPOSURE_1_100;

        for(SHUTTER_SPEED currentShutterSpeed : SHUTTER_SPEED.values()) {
            if(Math.abs(currentShutterSpeed.value - exposureTime) < Math.abs(bestShutterSpeedMatch.value - exposureTime)) {
                bestShutterSpeedMatch = currentShutterSpeed;
            }
        }

        return bestShutterSpeedMatch;
    }
}
