package com.motioncam;

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

    private void estimateFromExposure(float ev, float shadows) {
        int mergeImages;
        float chromaEps;
        float spatialDenoiseWeight;

        if(ev > 11.99) {
            spatialDenoiseWeight    = 0.0f;
            chromaEps               = 8.0f;
            mergeImages             = 1;
        }
        else if(ev > 9.99) {
            spatialDenoiseWeight    = 0.0f;
            chromaEps               = 8.0f;
            mergeImages             = 4;
        }
        else if(ev > 7.99) {
            spatialDenoiseWeight    = 0.5f;
            chromaEps               = 8.0f;
            mergeImages             = 4;
        }
        else if(ev > 5.99) {
            spatialDenoiseWeight    = 1.0f;
            chromaEps               = 16.0f;
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

        if(shadows > 7.99) {
            mergeImages             += 4;
            chromaEps               = Math.max(8.0f, chromaEps);
            spatialDenoiseWeight    = Math.max(0.5f, spatialDenoiseWeight);
        }

        // Limit capture to 5 seconds
//        if(mergeImages * (exposure / 1.0e9) > 5.0f) {
//            mergeImages = (int) Math.round(5.0f / (exposure / 1.0e9));
//        }

        this.numMergeImages = mergeImages;
        this.chromaEps      = chromaEps;
        this.spatialWeight  = spatialDenoiseWeight;
    }

    public DenoiseSettings(float noiseProfile, float ev, float shadows) {
        estimateFromExposure(ev, shadows);
    }
}
