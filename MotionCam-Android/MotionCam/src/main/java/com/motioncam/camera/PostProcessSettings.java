package com.motioncam.camera;

public class PostProcessSettings implements Cloneable {
    // Denoising
    public float spatialDenoiseAggressiveness   = 1.0f;

    // Post processing
    public float chromaEps                      = 4;
    public float tonemapVariance                = 0.25f;

    public float gamma                          = 2.2f;
    public float shadows                        = 1.0f;
    public float whitePoint                     = 1.0f;
    public float contrast                       = 0.25f;
    public float blacks                         = 0.0f;
    public float exposure                       = 0.0f;
    public float noiseSigma                     = 0.0f;
    public float sceneLuminance                 = 0.0f;
    public float saturation                     = 1.0f;
    public float blues                          = 10.0f;
    public float greens                         = 10.0f;
    public float temperature                    = -1.0f;
    public float tint                           = -1.0f;

    public float sharpen0                       = 2.5f;
    public float sharpen1                       = 1.3f;

    public int jpegQuality                      = 95;
    public boolean flipped                      = false;
    public boolean dng                          = false;

    public double gpsLatitude                   = 0;
    public double gpsLongitude                  = 0;
    public double gpsAltitude                   = 0;
    public String gpsTime                       = "";

    public String captureMode                   = "ZSL";

    @Override
    public PostProcessSettings clone() {
        try {
            return (PostProcessSettings) super.clone();
        }
        catch (CloneNotSupportedException e) {
            throw new RuntimeException(e);
        }
    }
}
