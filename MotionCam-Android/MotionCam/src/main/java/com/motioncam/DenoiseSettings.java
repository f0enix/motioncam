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

    public DenoiseSettings(float aperture, int iso, long exposure, float shadows) {
        final double s = aperture*aperture;
        final double ev = log2(s / (exposure / 1.0e9)) - log2(iso / 100.0);

        int mergeImages;
        float chromaEps;
        float spatialDenoiseWeight = 0.0f;

        if(ev > 11.99) {
            spatialDenoiseWeight    = 0.0f;
            chromaEps               = 2.0f;
            mergeImages             = 1;
        }
        else if(ev > 9.99) {
            spatialDenoiseWeight    = 0.0f;
            chromaEps               = 4.0f;
            mergeImages             = 2;
        }
        else if(ev > 7.99) {
            spatialDenoiseWeight    = 0.5f;
            chromaEps               = 8.0f;
            mergeImages             = 4;
        }
        else if(ev > 5.99) {
            spatialDenoiseWeight    = 1.0f;
            chromaEps               = 8.0f;
            mergeImages             = 4;
        }
        else if(ev > 3.99) {
            spatialDenoiseWeight    = 1.0f;
            chromaEps               = 16.0f;
            mergeImages             = 9;
        }
        else if(ev > 1.99) {
            spatialDenoiseWeight    = 1.0f;
            chromaEps               = 16.0f;
            mergeImages             = 9;
        }
        else if(ev > 0) {
            spatialDenoiseWeight    = 1.5f;
            chromaEps               = 16.0f;
            mergeImages             = 9;
        }
        else {
            spatialDenoiseWeight    = 2.0f;
            chromaEps               = 16.0f;
            mergeImages             = 12;
        }

        // If shadows are increased by a significant amount, use more images
        if(shadows >= 3.99) {
            spatialDenoiseWeight = Math.max(1.0f, spatialDenoiseWeight);
            mergeImages += 2;
            chromaEps   += 4;
        }

        if(shadows >= 7.99) {
            mergeImages += 2;
            chromaEps   += 4;
        }

        // Limit capture to 5 seconds
        if(mergeImages * (exposure / 1.0e9) > 5.0f) {
            mergeImages = (int) Math.round(5.0f / (exposure / 1.0e9));
        }

        this.numMergeImages = mergeImages;
        this.chromaEps      = chromaEps;
        this.spatialWeight  = spatialDenoiseWeight;
    }
}
