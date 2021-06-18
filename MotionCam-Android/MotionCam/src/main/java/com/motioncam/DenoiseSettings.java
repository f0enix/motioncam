package com.motioncam;

public class DenoiseSettings {
    public float spatialWeight;
    public float chromaFilterEps;
    public float chromaBlendWeight;
    public int numMergeImages;

    double log2(double v) {
        return Math.log(v) / Math.log(2);
    }

    @Override
    public String toString() {
        return "DenoiseSettings{" +
                "spatialWeight=" + spatialWeight +
                ", chromaFilterEps=" + chromaFilterEps +
                ", chromaBlendWeight=" + chromaBlendWeight +
                ", numMergeImages=" + numMergeImages +
                '}';
    }

    private void estimateFromExposure(float ev, float shadows) {
        int mergeImages;
        float chromaBlendWeight;
        float spatialDenoiseWeight;

        if(ev > 11.99) {
            spatialDenoiseWeight    = 0.0f;
            chromaBlendWeight       = 8.0f;
            mergeImages             = 1;
        }
        else if(ev > 9.99) {
            spatialDenoiseWeight    = 0.0f;
            chromaBlendWeight       = 4.0f;
            mergeImages             = 4;
        }
        else if(ev > 7.99) {
            spatialDenoiseWeight    = 0.5f;
            chromaBlendWeight       = 4.0f;
            mergeImages             = 4;
        }
        else if(ev > 5.99) {
            spatialDenoiseWeight    = 1.0f;
            chromaBlendWeight       = 2.0f;
            mergeImages             = 4;
        }
        else if(ev > 3.99) {
            spatialDenoiseWeight    = 1.0f;
            chromaBlendWeight       = 1.0f;
            mergeImages             = 6;
        }
        else if(ev > 0) {
            spatialDenoiseWeight    = 1.0f;
            chromaBlendWeight       = 0.0f;
            mergeImages             = 9;
        }
        else {
            spatialDenoiseWeight    = 1.0f;
            chromaBlendWeight       = 0.0f;
            mergeImages             = 12;
        }

        if(shadows > 3.99) {
            mergeImages             += 2;
            chromaBlendWeight        = Math.min(2.0f, chromaBlendWeight);
        }

        if(shadows > 7.99) {
            mergeImages             += 2;
        }

        // Limit capture to 5 seconds
//        if(mergeImages * (exposure / 1.0e9) > 5.0f) {
//            mergeImages = (int) Math.round(5.0f / (exposure / 1.0e9));
//        }

        this.numMergeImages     = mergeImages;
        this.chromaFilterEps    = 0.02f;
        this.chromaBlendWeight  = chromaBlendWeight;
        this.spatialWeight      = spatialDenoiseWeight;
    }

    public DenoiseSettings(float noiseProfile, float ev, float shadows) {
        estimateFromExposure(ev, shadows);
    }
}
