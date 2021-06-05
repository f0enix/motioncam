package com.motioncam;

import android.util.Log;

public class DenoiseSettings {
    public float spatialWeight;
    public float chromaEps;
    public int numMergeImages;

    double log2(double v) {
        return Math.log(v) / Math.log(2);
    }

    @Override
    public String toString() {
        return "DenoiseSettings{" +
                "spatialWeight=" + spatialWeight +
                ", chromaEps=" + chromaEps +
                ", numMergeImages=" + numMergeImages +
                '}';
    }

    private void estimateFromExposure(float aperture, int iso, long exposure, float shadows) {
        final double s = aperture*aperture;
        final double ev = log2(s / (exposure / 1.0e9)) - log2(iso / 100.0);

        int mergeImages;
        float chromaEps;
        float spatialDenoiseWeight = 0.0f;

        if(ev > 11.99) {
            spatialDenoiseWeight    = 0.0f;
            chromaEps               = 8.0f;
            mergeImages             = 1;
        }
        else if(ev > 9.99) {
            spatialDenoiseWeight    = 0.5f;
            chromaEps               = 8.0f;
            mergeImages             = 1;
        }
        else if(ev > 7.99) {
            spatialDenoiseWeight    = 0.5f;
            chromaEps               = 8.0f;
            mergeImages             = 4;
        }
        else if(ev > 5.99) {
            spatialDenoiseWeight    = 1.0f;
            chromaEps               = 12.0f;
            mergeImages             = 4;
        }
        else if(ev > 3.99) {
            spatialDenoiseWeight    = 1.0f;
            chromaEps               = 16.0f;
            mergeImages             = 6;
        }
        else if(ev > 0) {
            spatialDenoiseWeight    = 1.0f;
            chromaEps               = 16.0f;
            mergeImages             = 9;
        }
        else {
            spatialDenoiseWeight    = 1.5f;
            chromaEps               = 16.0f;
            mergeImages             = 12;
        }

        // Limit capture to 5 seconds
        if(mergeImages * (exposure / 1.0e9) > 5.0f) {
            mergeImages = (int) Math.round(5.0f / (exposure / 1.0e9));
        }

        this.numMergeImages = mergeImages;
        this.chromaEps      = chromaEps;
        this.spatialWeight  = spatialDenoiseWeight;
    }

    public DenoiseSettings(float noiseProfile, float aperture, int iso, long exposure, float shadows) {
        estimateFromExposure(aperture, iso, exposure, shadows);
    }
}
