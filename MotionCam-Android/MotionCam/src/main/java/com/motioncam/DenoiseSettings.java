package com.motioncam;

public class DenoiseSettings {
    public final float spatialWeight;
    public final float chromaEps;
    public final int numMergeImages;

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

    public DenoiseSettings(int iso, long exposure, float shadows) {
        final double s = 1.8*1.8;
        final double ev = log2(s / (exposure / 1.0e9)) - log2(iso / 100.0);

        int mergeImages = 0;
        float chromaEps = 0;

        if(ev > 11.99) {
            this.spatialWeight  = 0.0f;
            chromaEps           = 1.0f;
            mergeImages         = 1;
        }
        else if(ev > 9.99) {
            this.spatialWeight  = 0.0f;
            chromaEps           = 2.0f;
            mergeImages         = 2;
        }
        else if(ev > 7.99) {
            this.spatialWeight  = 0.5f;
            chromaEps           = 8.0f;
            mergeImages         = 4;
        }
        else if(ev > 5.99) {
            this.spatialWeight  = 1.0f;
            chromaEps           = 8.0f;
            mergeImages         = 6;
        }
        else if(ev > 3.99) {
            this.spatialWeight  = 1.5f;
            chromaEps           = 16.0f;
            mergeImages         = 8;
        }
        else if(ev > 1.99) {
            this.spatialWeight  = 2.0f;
            chromaEps           = 16.0f;
            mergeImages         = 10;
        }
        else {
            this.spatialWeight  = 3.0f;
            chromaEps           = 20.0f;
            mergeImages         = 12;
        }

        // If shadows are increased by a significant amount, use more images
        if(shadows >= 3.99) {
            mergeImages += 2;
        }

        if(shadows >= 7.99) {
            mergeImages += 2;
            chromaEps   += 4;
        }

        // Limit capture to 3 seconds
        if(mergeImages * (exposure / 1.0e9) > 3.0f) {
            mergeImages = (int) Math.round(3.0f / (exposure / 1.0e9));
        }

        this.numMergeImages = mergeImages;
        this.chromaEps      = chromaEps;
    }
}
