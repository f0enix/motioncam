#ifndef Settings_h
#define Settings_h

#include <json11/json11.hpp>

namespace motioncam {
    float getSetting(const json11::Json& json, const std::string& key, const float defaultValue);
    int getSetting(const json11::Json& json, const std::string& key, const int defaultValue);
    bool getSetting(const json11::Json& json, const std::string& key, const bool defaultValue);

    struct PostProcessSettings {
        // Denoising
        float spatialDenoiseAggressiveness;

        // Post processing
        float temperature;
        float tint;
        
        float chromaEps;

        float gamma;
        float tonemapVariance;
        float shadows;
        float whitePoint;
        float contrast;
        float sharpen0;
        float sharpen1;
        float blacks;
        float exposure;
        
        float noiseSigma;
        float sceneLuminance;
        
        float saturation;
        float blueSaturation;
        float greenSaturation;
        
        int jpegQuality;
        bool flipped;
        bool dng;

        PostProcessSettings() :
            spatialDenoiseAggressiveness(1.0f),
            temperature(-1),
            tint(-1),
            chromaEps(0),
            gamma(2.2f),
            tonemapVariance(0.25f),
            shadows(1.0f),
            exposure(0.0f),
            noiseSigma(0.0f),
            sceneLuminance(0.0f),
            contrast(0.5f),
            sharpen0(4.0f),
            sharpen1(3.0f),
            blacks(0.0f),
            whitePoint(1.0f),
            saturation(1.0f),
            blueSaturation(1.0f),
            greenSaturation(1.0f),
            jpegQuality(95),
            flipped(false),
            dng(false)
        {
        }
        
        PostProcessSettings(const json11::Json& json) : PostProcessSettings() {
            spatialDenoiseAggressiveness    = getSetting(json, "spatialDenoiseAggressiveness",  spatialDenoiseAggressiveness);
            
            chromaEps                       = getSetting(json, "chromaEps",         chromaEps);
            tonemapVariance                 = getSetting(json, "tonemapVariance",   tonemapVariance);

            gamma                           = getSetting(json, "gamma",             gamma);
            temperature                     = getSetting(json, "temperature",       temperature);
            tint                            = getSetting(json, "tint",              tint);
            shadows                         = getSetting(json, "shadows",           shadows);
            whitePoint                      = getSetting(json, "whitePoint",        whitePoint);
            contrast                        = getSetting(json, "contrast",          contrast);
            exposure                        = getSetting(json, "exposure",          exposure);
            blacks                          = getSetting(json, "blacks",            blacks);
            
            noiseSigma                      = getSetting(json, "noiseSigma",        noiseSigma);
            sceneLuminance                  = getSetting(json, "sceneLuminance",    sceneLuminance);

            sharpen0                        = getSetting(json, "sharpen0",          sharpen0);
            sharpen1                        = getSetting(json, "sharpen1",          sharpen1);

            saturation                      = getSetting(json, "saturation",        saturation);
            blueSaturation                  = getSetting(json, "blueSaturation",    blueSaturation);
            greenSaturation                 = getSetting(json, "greenSaturation",   greenSaturation);
            
            jpegQuality                     = getSetting(json, "jpegQuality",       jpegQuality);
            flipped                         = getSetting(json, "flipped",       	flipped);
            dng                             = getSetting(json, "dng",       	    dng);
        }
        
        json11::Json toJson() const {
            json11::Json::object json;
            
            json["spatialDenoiseAggressiveness"]    = spatialDenoiseAggressiveness;
            json["chromaEps"]                       = chromaEps;
            json["gamma"]                           = gamma;
            json["tonemapVariance"]                 = tonemapVariance;
            json["shadows"]                         = shadows;
            json["whitePoint"]                      = whitePoint;
            json["contrast"]                        = contrast;
            json["sharpen0"]                        = sharpen0;
            json["sharpen1"]                        = sharpen1;
            json["blacks"]                          = blacks;
            json["exposure"]                        = exposure;
            json["temperature"]                     = temperature;
            json["tint"]                            = tint;
            
            json["noiseSigma"]                      = noiseSigma;
            json["sceneLuminance"]                  = sceneLuminance;
            
            json["saturation"]                      = saturation;
            json["blueSaturation"]                  = blueSaturation;
            json["greenSaturation"]                 = greenSaturation;

            json["jpegQuality"]                     = jpegQuality;
            json["flipped"]                         = flipped;
            json["dng"]                             = dng;
            
            return json;
        }
    };
}

#endif /* Settings_h */
