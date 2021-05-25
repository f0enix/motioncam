#include "motioncam/ImageProcessor.h"
#include "motioncam/RawContainer.h"
#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include "motioncam/Exceptions.h"
#include "motioncam/Util.h"
#include "motioncam/Logger.h"
#include "motioncam/Measure.h"
#include "motioncam/Settings.h"
#include "motioncam/ImageOps.h"

// Halide
#include "generate_edges.h"
#include "measure_image.h"
#include "deinterleave_raw.h"
#include "forward_transform.h"
#include "inverse_transform.h"
#include "fuse_image.h"
#include "fuse_denoise.h"

#include "linear_image.h"
#include "hdr_mask.h"

#include "preview_landscape2.h"
#include "preview_portrait2.h"
#include "preview_reverse_portrait2.h"
#include "preview_reverse_landscape2.h"
#include "preview_landscape4.h"
#include "preview_portrait4.h"
#include "preview_reverse_portrait4.h"
#include "preview_reverse_landscape4.h"
#include "preview_landscape8.h"
#include "preview_portrait8.h"
#include "preview_reverse_portrait8.h"
#include "preview_reverse_landscape8.h"

#include "postprocess.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <memory>

#include <exiv2/exiv2.hpp>
#include <opencv2/ximgproc/edge_filter.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/xfeatures2d.hpp>

#ifdef DNG_SUPPORT

#include <dng/dng_host.h>
#include <dng/dng_negative.h>
#include <dng/dng_camera_profile.h>
#include <dng/dng_file_stream.h>
#include <dng/dng_memory_stream.h>
#include <dng/dng_image_writer.h>
#include <dng/dng_render.h>
#include <dng/dng_gain_map.h>

#endif

using std::ios;
using std::string;
using std::shared_ptr;
using std::vector;
using std::to_string;
using std::pair;

static std::vector<Halide::Runtime::Buffer<float>> createWaveletBuffers(int width, int height) {
    std::vector<Halide::Runtime::Buffer<float>> buffers;
    
    for(int level = 0; level < 6; level++) {
        width = width / 2;
        height = height / 2;
        
        buffers.emplace_back(width, height, 4, 4);
    }
    
    return buffers;
}

extern "C" int extern_denoise(halide_buffer_t *in, int32_t width, int32_t height, int c, float weight, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        in->dim[0].min = 0;
        in->dim[1].min = 0;
        in->dim[2].min = 0;
        
        in->dim[0].extent = width;
        in->dim[1].extent = height;
        in->dim[2].extent = 2;
    }
    else {
        auto inputBuffers = createWaveletBuffers(width, height);
        
        forward_transform(in,
                          width,
                          height,
                          c,
                          inputBuffers[0],
                          inputBuffers[1],
                          inputBuffers[2],
                          inputBuffers[3],
                          inputBuffers[4],
                          inputBuffers[5]);

        cv::Mat hh(inputBuffers[0].height(),
                   inputBuffers[0].width(),
                   CV_32F,
                   inputBuffers[0].data() + 3*inputBuffers[0].stride(2));

        float noiseSigma = motioncam::estimateNoise(hh);
        
        inverse_transform(inputBuffers[0],
                          inputBuffers[1],
                          inputBuffers[2],
                          inputBuffers[3],
                          inputBuffers[4],
                          inputBuffers[5],
                          weight*noiseSigma,
                          true,
                          1,
                          0,
                          out);
    }
    
    return 0;
}

extern "C" int extern_min_max(halide_buffer_t *in, int32_t width, int32_t height, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        in->dim[0].min = 0;
        in->dim[1].min = 0;
        
        in->dim[0].extent = width;
        in->dim[1].extent = height;
    }
    else {
        Halide::Runtime::Buffer<float> inBuf(*in);
        Halide::Runtime::Buffer<float> outBuf(*out);
        
        double min, max;
        
        cv::Mat m(inBuf.height(), inBuf.width(), CV_32F, inBuf.data());
        cv::minMaxLoc(m, &min, &max);

        outBuf.begin()[0] = min;
        outBuf.begin()[1] = max;
    }
    
    return 0;
}

namespace motioncam {
    const int DENOISE_LEVELS    = 6;
    const int EXPANDED_RANGE    = 16384;
    const float MAX_HDR_ERROR   = 0.06f;

    struct RawData {
        Halide::Runtime::Buffer<uint16_t> rawBuffer;
        Halide::Runtime::Buffer<uint8_t> previewBuffer;
        RawImageMetadata metadata;
    };

    struct HdrMetadata {
        float exposureScale;
        float error;
        Halide::Runtime::Buffer<uint16_t> hdrInput;
        Halide::Runtime::Buffer<uint8_t> mask;
    };

    struct PreviewMetadata {
        std::vector<cv::Rect> faces;
        
        PreviewMetadata(const std::string& metadata) {
            std::string err;
            json11::Json metadataJson = json11::Json::parse(metadata, err);

            if(!err.empty()) {
                return;
            }

            auto facesIt = metadataJson.object_items().find("faces");
            if(facesIt != metadataJson.object_items().end()) {
                auto facesArray = facesIt->second.array_items();
                for(auto& f : facesArray) {
                    auto data = f.object_items();
                    
                    int left = data["left"].int_value();
                    int top = data["top"].int_value();
                    int right = data["right"].int_value();
                    int bottom = data["bottom"].int_value();
                    
                    auto boundingBox = cv::Rect(left, top, right - left, bottom - top);
                    
                    faces.push_back(boundingBox);
                }
            }
        }
    };

    // https://exiv2.org/doc/geotag_8cpp-example.html
    static std::string toExifString(double d, bool isRational, bool isLatitude)
    {
        const char* NS   = d>=0.0?"N":"S";
        const char* EW   = d>=0.0?"E":"W";
        const char* NSEW = isLatitude  ? NS: EW;
        
        if ( d < 0 ) d = -d;
        
        int deg = (int) d;
            d  -= deg;
            d  *= 60;
        
        int min = (int) d ;
            d  -= min;
            d  *= 60;
        
        int sec = (int)d;
        
        char result[200];
        
        if (isRational)
            sprintf(result,"%d/1 %d/1 %d/1",deg, min, sec);
        else
            sprintf(result,"%03d%s%02d'%02d\"%s", deg, "deg", min, sec, NSEW);
        
        return std::string(result);
    }

    static std::string toExifString(double d)
    {
        char result[200];
        d *= 100;
        sprintf(result,"%d/100",abs((int)d));
        
        return std::string(result);
    }

    template<typename T>
    static Halide::Runtime::Buffer<T> ToHalideBuffer(const cv::Mat& input) {
        return Halide::Runtime::Buffer<T>((T*) input.data, input.cols, input.rows);
    }

    struct NativeBufferContext {
        NativeBufferContext(NativeBuffer& buffer, bool write) : nativeBuffer(buffer)
        {
            nativeBufferData = nativeBuffer.lock(write);
        }
        
        Halide::Runtime::Buffer<uint8_t> getHalideBuffer() {
            return Halide::Runtime::Buffer<uint8_t>(nativeBufferData, (int) nativeBuffer.len());
        }
        
        ~NativeBufferContext() {
            nativeBuffer.unlock();
        }
        
    private:
        NativeBuffer& nativeBuffer;
        uint8_t* nativeBufferData;
    };

    ImageProgressHelper::ImageProgressHelper(const ImageProcessorProgress& progressListener, int numImages, int start) :
        mStart(start), mProgressListener(progressListener), mNumImages(numImages), mCurImage(0)
    {
        // Per fused image increment is numImages over a 75% progress amount.
        mPerImageIncrement = 75.0 / numImages;
    }
    
    void ImageProgressHelper::postProcessCompleted() {
        mProgressListener.onProgressUpdate(mStart + 95);
    }
    
    void ImageProgressHelper::denoiseCompleted() {
        // Starting point is mStart, denoising takes 50%, progress should now be mStart + 50%
        mProgressListener.onProgressUpdate(mStart + 75);
    }

    void ImageProgressHelper::nextFusedImage() {
        ++mCurImage;
        mProgressListener.onProgressUpdate(static_cast<int>(mStart + (mPerImageIncrement * mCurImage)));
    }

    void ImageProgressHelper::imageSaved() {
        mProgressListener.onProgressUpdate(100);
        mProgressListener.onCompleted();
    }

    double ImageProcessor::calcEv(const RawCameraMetadata& cameraMetadata, const RawImageMetadata& metadata) {
        double a = 1.8;
        if(!cameraMetadata.apertures.empty())
            a = cameraMetadata.apertures[0];

        double s = a*a;
        return std::log2(s / (metadata.exposureTime / (1.0e9))) - std::log2(metadata.iso / 100.0);
    }


    cv::Mat ImageProcessor::postProcess(std::vector<Halide::Runtime::Buffer<uint16_t>>& inputBuffers,
                                        const shared_ptr<HdrMetadata>& hdrMetadata,
                                        int offsetX,
                                        int offsetY,
                                        const RawImageMetadata& metadata,
                                        const RawCameraMetadata& cameraMetadata,
                                        const PostProcessSettings& settings)
    {
        Measure measure("postProcess");

        Halide::Runtime::Buffer<float> shadingMapBuffer[4];
        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(metadata.lensShadingMap[i]);
        }

        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;

        if(settings.temperature > 0 || settings.tint > 0) {
            Temperature t(settings.temperature, settings.tint);

            createSrgbMatrix(cameraMetadata, metadata, t, cameraWhite, cameraToPcs, pcsToSrgb);
        }
        else {
            createSrgbMatrix(cameraMetadata, metadata, metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);
        }

        Halide::Runtime::Buffer<float> cameraToPcsBuffer = ToHalideBuffer<float>(cameraToPcs);
        Halide::Runtime::Buffer<float> pcsToSrgbBuffer = ToHalideBuffer<float>(pcsToSrgb);

        // Trim a bit more from the edges to compensate from issues when registering/fusing
        offsetX += 8;
        offsetY += 8;
        
        cv::Mat output((inputBuffers[0].height() - offsetY)*2, (inputBuffers[0].width() - offsetX)*2, CV_8UC3);
        
        Halide::Runtime::Buffer<uint8_t> outputBuffer(
            Halide::Runtime::Buffer<uint8_t>::make_interleaved(output.data, output.cols, output.rows, 3));

        // Edges are garbage, don't process them
        outputBuffer.translate(0, offsetX);
        outputBuffer.translate(1, offsetY);

        for(int i = 0; i < 4; i++) {
            inputBuffers[i].set_host_dirty();
            shadingMapBuffer[i].set_host_dirty();
        }
        
        Halide::Runtime::Buffer<uint16_t> hdrInput;
        Halide::Runtime::Buffer<uint8_t> hdrMask;
        float hdrScale = 1.0f;
        float shadows = settings.shadows;

        cv::Mat blankMask(16, 16, CV_8U, cv::Scalar(0));
        cv::Mat blankInput(16, 16, CV_16UC3, cv::Scalar(0));

        if(hdrMetadata && hdrMetadata->error < MAX_HDR_ERROR) {
            hdrInput = hdrMetadata->hdrInput;
            hdrMask = hdrMetadata->mask;
            hdrScale = hdrMetadata->exposureScale;
            
            // Boost shadows to rougly match what the user selected.
            shadows = settings.shadows * (1.0/hdrScale);
            
            hdrMask.translate(0, offsetX);
            hdrMask.translate(1, offsetY);
        }
        else {
            // Don't apply underexposed image when error is too high
            if(hdrMetadata)
                logger::log("Not using HDR image, error too high (" + std::to_string(hdrMetadata->error) + ")");
                        
            hdrMask = ToHalideBuffer<uint8_t>(blankMask);
            hdrInput = Halide::Runtime::Buffer<uint16_t>((uint16_t*) blankInput.data, blankInput.cols, blankInput.rows, 3);
        }
        
        // Linearly adjust sharpen threshold based on exposure
        double ev = calcEv(cameraMetadata, metadata);
        double sharpenThreshold = std::max(4.0, 1.5*ev + 4);
        
        postprocess(inputBuffers[0],
                    inputBuffers[1],
                    inputBuffers[2],
                    inputBuffers[3],
                    hdrInput,
                    hdrMask,
                    hdrScale,
                    metadata.asShot[0],
                    metadata.asShot[1],
                    metadata.asShot[2],
                    cameraToPcsBuffer,
                    pcsToSrgbBuffer,
                    shadingMapBuffer[0],
                    shadingMapBuffer[1],
                    shadingMapBuffer[2],
                    shadingMapBuffer[3],
                    EXPANDED_RANGE,
                    static_cast<int>(cameraMetadata.sensorArrangment),
                    settings.gamma,
                    shadows,
                    settings.tonemapVariance,
                    settings.blacks,
                    settings.exposure,
                    settings.whitePoint,
                    settings.contrast,
                    settings.blueSaturation,
                    settings.saturation,
                    settings.greenSaturation,
                    settings.sharpen0,
                    settings.sharpen1,
                    sharpenThreshold,
                    settings.chromaEps,
                    outputBuffer);
        
        outputBuffer.device_sync();
        outputBuffer.copy_to_host();

        return output;
    }

    float ImageProcessor::estimateShadows(const cv::Mat& histogram, float keyValue) {
        float avgLuminance = 0.0f;
        
        float totalPixels = 0;
        
        int lowerBound = 0;
        int upperBound = histogram.cols;
        
        for(int i = lowerBound; i < upperBound; i++) {
            avgLuminance += histogram.at<float>(i) * log(1e-5 + i / 255.0f);
            totalPixels += histogram.at<float>(i);
        }
        
        avgLuminance = exp(avgLuminance / (totalPixels + 1e-5f));
        
        return std::max(1.0f, std::min(keyValue / avgLuminance, 32.0f));
    }

    float ImageProcessor::estimateExposureCompensation(const cv::Mat& histogram, float threshold) {
        int bin = 0;
        float total = 0.0f;
        
        // Exposure compensation
        for(int i = histogram.cols - 1; i >= 0; i--) {
            total += histogram.at<float>(i);
            if(total >= threshold) {
                bin = i;
                break;
            }
        }
        
        double m = histogram.cols / static_cast<double>(bin + 1);
        return std::log2(m);
    }

    cv::Mat ImageProcessor::estimateBlacks(const RawImageBuffer& rawBuffer,
                                           const RawCameraMetadata& cameraMetadata,
                                           float shadows,
                                           float& outBlacks) {
        
        PostProcessSettings settings;
        
        settings.shadows = shadows;
        settings.blacks = 0;
        settings.contrast = 0.5f;
        settings.chromaEps = 0;
        settings.sharpen0 = 0;
        settings.sharpen1 = 0;
        
        auto previewBuffer = createPreview(rawBuffer, 2, cameraMetadata, settings);
        
        cv::Mat preview(previewBuffer.height(), previewBuffer.width(), CV_8UC4, previewBuffer.data());
        cv::Mat histogram;
        
        cv::cvtColor(preview, preview, cv::COLOR_BGRA2GRAY);

        vector<cv::Mat> inputImages     = { preview };
        const vector<int> channels      = { 0 };
        const vector<int> histBins      = { 255 };
        const vector<float> histRange   = { 0, 256 };

        cv::calcHist(inputImages, channels, cv::Mat(), histogram, histBins, histRange);
        
        histogram = histogram / (preview.rows * preview.cols);
        
        // Cumulative histogram
        for(int i = 1; i < histogram.rows; i++) {
            histogram.at<float>(i) += histogram.at<float>(i - 1);
        }
                
        // Estimate blacks
        const float maxDehazePercent = 0.03f;
        const int maxEndBin = 12; // Max bin

        int endBin = 0;
        
        for(endBin = 0; endBin < maxEndBin; endBin++) {
            float binPx = histogram.at<float>(endBin);

            if(binPx > maxDehazePercent)
                break;
        }

        outBlacks = static_cast<float>(std::max(0, endBin - 1)) / static_cast<float>(histogram.rows - 1);

        return preview;
    }

    cv::Mat ImageProcessor::estimateWhitePoint(const RawImageBuffer& rawBuffer,
                                               const RawCameraMetadata& cameraMetadata,
                                               float shadows,
                                               float blacks,
                                               float threshold,
                                               float& outWhitePoint) {
        PostProcessSettings settings;
        
        settings.shadows = shadows;
        settings.blacks = blacks;
        settings.contrast = 0.5f;
        settings.chromaEps = 0;
        settings.sharpen0 = 0;
        settings.sharpen1 = 0;

        auto previewBuffer = createPreview(rawBuffer, 2, cameraMetadata, settings);
        
        cv::Mat preview(previewBuffer.height(), previewBuffer.width(), CV_8UC4, previewBuffer.data());
        cv::Mat histogram;
        
        cv::cvtColor(preview, preview, cv::COLOR_BGRA2GRAY);

        vector<cv::Mat> inputImages     = { preview };
        const vector<int> channels      = { 0 };
        const vector<int> histBins      = { 255 };
        const vector<float> histRange   = { 0, 256 };

        cv::calcHist(inputImages, channels, cv::Mat(), histogram, histBins, histRange);
        
        histogram = histogram / (preview.rows * preview.cols);
        
        // Cumulative histogram
        for(int i = 1; i < histogram.rows; i++) {
            histogram.at<float>(i) += histogram.at<float>(i - 1);
        }

        // Estimate white point
        int endBin = 0;
        for(endBin = histogram.rows - 1; endBin >= 192; endBin--) {
            float binPx = histogram.at<float>(endBin);

            if(binPx < threshold)
                break;
        }

        outWhitePoint = static_cast<float>(endBin) / ((float) histogram.rows - 1);
        
        return preview;
    }

    void ImageProcessor::estimateBasicSettings(const RawImageBuffer& rawBuffer,
                                               const RawCameraMetadata& cameraMetadata,
                                               const float shadowsKeyValue,
                                               PostProcessSettings& outSettings)
    {
        //Measure measure("estimateBasicSettings()");

        // Start with basic initial values
        CameraProfile cameraProfile(cameraMetadata, rawBuffer.metadata);
        Temperature temperature;

        cameraProfile.temperatureFromVector(rawBuffer.metadata.asShot, temperature);

        cv::Mat histogram = calcHistogram(cameraMetadata, rawBuffer, false, 4);

        outSettings.temperature    = static_cast<float>(temperature.temperature());
        outSettings.tint           = static_cast<float>(temperature.tint());
        outSettings.shadows        = estimateShadows(histogram, shadowsKeyValue);
        outSettings.exposure       = estimateExposureCompensation(histogram, 1e-2f);
    }

    void ImageProcessor::estimateWhiteBalance(const RawImageBuffer& rawBuffer,
                                              const RawCameraMetadata& cameraMetadata,
                                              float& outR,
                                              float& outG,
                                              float& outB)
    {
        Measure measure("estimateWhiteBalance()");

        outR = 1.0f;
        outG = 1.0f;
        outB = 1.0f;
    }

    void ImageProcessor::estimateSettings(const RawImageBuffer& rawBuffer,
                                          const RawCameraMetadata& cameraMetadata,
                                          const float shadowsKeyValue,
                                          PostProcessSettings& outSettings)
    {
        Measure measure("estimateSettings");
        
        // Start with basic initial values
        PostProcessSettings settings;
        
        // Calculate white balance from metadata
        CameraProfile cameraProfile(cameraMetadata, rawBuffer.metadata);
        Temperature temperature;
        
        cameraProfile.temperatureFromVector(rawBuffer.metadata.asShot, temperature);
        
        cv::Mat histogram = calcHistogram(cameraMetadata, rawBuffer, false, 4);
        
        settings.temperature    = static_cast<float>(temperature.temperature());
        settings.tint           = static_cast<float>(temperature.tint());
        settings.shadows        = estimateShadows(histogram, shadowsKeyValue);
        settings.exposure       = estimateExposureCompensation(histogram);

        estimateBlacks(rawBuffer, cameraMetadata, settings.shadows, settings.blacks);

        auto preview =
            estimateWhitePoint(rawBuffer, cameraMetadata, settings.shadows, settings.blacks, 0.995f, settings.whitePoint);
        
        //
        // Scene luminance
        //

        preview.convertTo(preview, CV_32F, 1.0/255.0);
        cv::log(preview + 0.001f, preview);

        settings.sceneLuminance = static_cast<float>(cv::exp(1.0/(preview.cols*preview.rows) * cv::sum(preview)[0]));

        //
        // Use faster method for noise estimate
        //

        auto rawImage = loadRawImage(rawBuffer, cameraMetadata);

        cv::Mat rawImageInput(rawImage->rawBuffer.height(),
                              rawImage->rawBuffer.width(),
                              CV_16U,
                              rawImage->rawBuffer.data());

        cv::Mat k(3, 3, CV_32F);

        k.at<float>(0, 0) =  1;
        k.at<float>(0, 1) = -2;
        k.at<float>(0, 2) =  1;

        k.at<float>(1, 0) = -2;
        k.at<float>(1, 1) =  4;
        k.at<float>(1, 2) = -2;

        k.at<float>(2, 0) =  1;
        k.at<float>(2, 1) = -2;
        k.at<float>(2, 2) =  1;

        cv::filter2D(rawImageInput, rawImageInput, CV_32F, k);

        const double pi = 3.14159265358979323846;
        double p = 1.0 / ( 6.0 * (rawImageInput.cols - 2.0) * (rawImageInput.rows - 2.0) );
        p = sqrt(0.5*pi) * p;

        auto sigma = p * cv::sum(cv::abs(rawImageInput));

        settings.noiseSigma = sigma[0];
        
        // Update estimated settings
        outSettings = settings;
    }

    void ImageProcessor::createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                          const RawImageMetadata& rawImageMetadata,
                                          const Temperature& temperature,
                                          cv::Vec3f& cameraWhite,
                                          cv::Mat& outCameraToPcs,
                                          cv::Mat& outPcsToSrgb)
    {
        cv::Mat pcsToCamera, cameraToPcs;
        cv::Mat pcsToSrgb, srgbToPcs;
        
        CameraProfile cameraProfile(cameraMetadata, rawImageMetadata);

        cameraProfile.cameraToPcs(temperature, pcsToCamera, cameraToPcs, cameraWhite);
        motioncam::CameraProfile::pcsToSrgb(pcsToSrgb, srgbToPcs);

        cameraToPcs.copyTo(outCameraToPcs);
        pcsToSrgb.copyTo(outPcsToSrgb);
    }

    void ImageProcessor::createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                          const RawImageMetadata& rawImageMetadata,
                                          const cv::Vec3f& asShot,
                                          cv::Vec3f& cameraWhite,
                                          cv::Mat& outCameraToPcs,
                                          cv::Mat& outPcsToSrgb)
    {
        cv::Mat pcsToCamera, cameraToPcs;
        cv::Mat pcsToSrgb, srgbToPcs;
        
        CameraProfile cameraProfile(cameraMetadata, rawImageMetadata);
        Temperature temperature;

        cv::Vec3f asShotVector = asShot;
        float max = math::max(asShotVector);
        
        if(max > 0) {
            asShotVector[0] = asShotVector[0] * (1.0f / max);
            asShotVector[1] = asShotVector[1] * (1.0f / max);
            asShotVector[2] = asShotVector[2] * (1.0f / max);
        }
        else {
            throw InvalidState("Camera white balance vector is zero");
        }

        cameraProfile.temperatureFromVector(asShotVector, temperature);

        cameraProfile.cameraToPcs(temperature, pcsToCamera, cameraToPcs, cameraWhite);
        motioncam::CameraProfile::pcsToSrgb(pcsToSrgb, srgbToPcs);

        cameraToPcs.copyTo(outCameraToPcs);
        pcsToSrgb.copyTo(outPcsToSrgb);
    }

    Halide::Runtime::Buffer<uint8_t> ImageProcessor::createPreview(const RawImageBuffer& rawBuffer,
                                                                   const int downscaleFactor,
                                                                   const RawCameraMetadata& cameraMetadata,
                                                                   const PostProcessSettings& settings)
    {
//        Measure measure("createPreview()");
        
        if(downscaleFactor != 2 && downscaleFactor != 4 && downscaleFactor != 8) {
            throw InvalidState("Invalid downscale factor");
        }
        
        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;
        
        if(settings.temperature > 0 || settings.tint > 0) {
            Temperature t(settings.temperature, settings.tint);

            createSrgbMatrix(cameraMetadata, rawBuffer.metadata, t, cameraWhite, cameraToPcs, pcsToSrgb);
        }
        else {
            createSrgbMatrix(cameraMetadata, rawBuffer.metadata, rawBuffer.metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);
        }

        Halide::Runtime::Buffer<float> cameraToPcsBuffer = ToHalideBuffer<float>(cameraToPcs);
        Halide::Runtime::Buffer<float> pcsToSrgbBuffer = ToHalideBuffer<float>(pcsToSrgb);

        Halide::Runtime::Buffer<float> shadingMapBuffer[4];

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(rawBuffer.metadata.lensShadingMap[i]);
        }

        NativeBufferContext inputBufferContext(*rawBuffer.data, false);

        // Set up rotation based on orientation of image
        int width = rawBuffer.width / 2 / downscaleFactor; // Divide by 2 because we are not demosaicing the RAW data
        int height = rawBuffer.height / 2 / downscaleFactor;
        
        auto method = &preview_landscape2;
        
        switch(rawBuffer.metadata.screenOrientation) {
            case ScreenOrientation::REVERSE_PORTRAIT:
                if(downscaleFactor == 2)
                    method = &preview_reverse_portrait2;
                else if(downscaleFactor == 4)
                    method = &preview_reverse_portrait4;
                else
                    method = &preview_reverse_portrait8;

                std::swap(width, height);
                break;

            case ScreenOrientation::REVERSE_LANDSCAPE:
                if(downscaleFactor == 2)
                    method = &preview_reverse_landscape2;
                else if(downscaleFactor == 4)
                    method = &preview_reverse_landscape4;
                else
                    method = &preview_reverse_landscape8;

                break;

            case ScreenOrientation::PORTRAIT:
                if(downscaleFactor == 2)
                    method = &preview_portrait2;
                else if(downscaleFactor == 4)
                    method = &preview_portrait4;
                else
                    method = &preview_portrait8;

                std::swap(width, height);
                break;

            default:
            case ScreenOrientation::LANDSCAPE:
                if(downscaleFactor == 2)
                    method = &preview_landscape2;
                else if(downscaleFactor == 4)
                    method = &preview_landscape4;
                else
                    method = &preview_landscape8;
                break;
        }
       
        Halide::Runtime::Buffer<uint8_t> outputBuffer =
            Halide::Runtime::Buffer<uint8_t>::make_interleaved(width, height, 4);
        
        method(
            inputBufferContext.getHalideBuffer(),
            shadingMapBuffer[0],
            shadingMapBuffer[1],
            shadingMapBuffer[2],
            shadingMapBuffer[3],
            rawBuffer.metadata.asShot[0],
            rawBuffer.metadata.asShot[1],
            rawBuffer.metadata.asShot[2],
            cameraToPcsBuffer,
            pcsToSrgbBuffer,
            rawBuffer.width / 2 / downscaleFactor,
            rawBuffer.height / 2 / downscaleFactor,
            rawBuffer.rowStride,
            static_cast<int>(rawBuffer.pixelFormat),
            static_cast<int>(cameraMetadata.sensorArrangment),
            cameraMetadata.blackLevel[0],
            cameraMetadata.blackLevel[1],
            cameraMetadata.blackLevel[2],
            cameraMetadata.blackLevel[3],
            static_cast<uint16_t>(cameraMetadata.whiteLevel),
            settings.gamma,
            settings.shadows,
            settings.whitePoint,
            settings.tonemapVariance,
            settings.blacks,
            settings.exposure,
            settings.contrast,
            settings.blueSaturation,
            settings.saturation,
            settings.greenSaturation,
            settings.sharpen0,
            settings.sharpen1,
            settings.flipped,
            outputBuffer);

        outputBuffer.device_sync();
        outputBuffer.copy_to_host();

        return outputBuffer;
    }
    
    std::shared_ptr<RawData> ImageProcessor::loadRawImage(const RawImageBuffer& rawBuffer,
                                                          const RawCameraMetadata& cameraMetadata,
                                                          const bool extendEdges,
                                                          const float scalePreview)
    {
        // Extend the image so it can be downscaled by 'LEVELS' for the denoising step
        int extendX = 0;
        int extendY = 0;

        int halfWidth  = rawBuffer.width / 2;
        int halfHeight = rawBuffer.height / 2;

        if(extendEdges) {
            const int T = pow(2, DENOISE_LEVELS);

            extendX = static_cast<int>(T * ceil(halfWidth / (double) T) - halfWidth);
            extendY = static_cast<int>(T * ceil(halfHeight / (double) T) - halfHeight);
        }
        
        auto rawData = std::make_shared<RawData>();

        NativeBufferContext inputBufferContext(*rawBuffer.data, false);
        
        rawData->previewBuffer  = Halide::Runtime::Buffer<uint8_t>(halfWidth + extendX, halfHeight + extendY);
        rawData->rawBuffer      = Halide::Runtime::Buffer<uint16_t>(halfWidth + extendX, halfHeight + extendY, 4);
        rawData->metadata       = rawBuffer.metadata;
                
        deinterleave_raw(inputBufferContext.getHalideBuffer(),
                         rawBuffer.rowStride,
                         static_cast<int>(rawBuffer.pixelFormat),
                         static_cast<int>(cameraMetadata.sensorArrangment),
                         halfWidth,
                         halfHeight,
                         extendX / 2,
                         extendY / 2,
                         cameraMetadata.whiteLevel,
                         cameraMetadata.blackLevel[0],
                         cameraMetadata.blackLevel[1],
                         cameraMetadata.blackLevel[2],
                         cameraMetadata.blackLevel[3],
                         scalePreview,
                         rawData->rawBuffer,
                         rawData->previewBuffer);
                        
        return rawData;
    }

    void ImageProcessor::measureImage(RawImageBuffer& rawBuffer, const RawCameraMetadata& cameraMetadata, float& outSceneLuminosity)
    {
//        Measure measure("measureImage");

        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;
        
        createSrgbMatrix(cameraMetadata, rawBuffer.metadata, rawBuffer.metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);

        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;
        
        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = ToHalideBuffer<float>(cameraToSrgb);
        Halide::Runtime::Buffer<float> shadingMapBuffer[4];

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(rawBuffer.metadata.lensShadingMap[i]);
        }

        int halfWidth  = rawBuffer.width / 2;
        int halfHeight = rawBuffer.height / 2;

        const int downscale = 4;

        NativeBufferContext inputBufferContext(*rawBuffer.data, false);
        Halide::Runtime::Buffer<uint32_t> histogramBuffer(2u << 7u);

        measure_image(inputBufferContext.getHalideBuffer(),
                      rawBuffer.rowStride,
                      static_cast<int>(rawBuffer.pixelFormat),
                      halfWidth,
                      halfHeight,
                      downscale,
                      cameraMetadata.blackLevel[0],
                      cameraMetadata.blackLevel[1],
                      cameraMetadata.blackLevel[2],
                      cameraMetadata.blackLevel[3],
                      cameraMetadata.whiteLevel,
                      cameraWhite[0],
                      cameraWhite[1],
                      cameraWhite[2],
                      cameraToSrgbBuffer,
                      shadingMapBuffer[0],
                      shadingMapBuffer[1],
                      shadingMapBuffer[2],
                      shadingMapBuffer[3],
                      static_cast<int>(cameraMetadata.sensorArrangment),
                      histogramBuffer);

        histogramBuffer.device_sync();
        histogramBuffer.copy_to_host();

        cv::Mat histogram(histogramBuffer.height(), histogramBuffer.width(), CV_32S, histogramBuffer.data());

        // Normalize
        histogram.convertTo(histogram, CV_32F, 1.0 / (halfWidth/downscale * halfHeight/downscale));

        outSceneLuminosity = 0;
    }

    cv::Mat ImageProcessor::registerImage(
        const Halide::Runtime::Buffer<uint8_t>& referenceBuffer, const Halide::Runtime::Buffer<uint8_t>& toAlignBuffer)
    {
        Measure measure("registerImage()");
        
        cv::Mat referenceImage(referenceBuffer.height(), referenceBuffer.width(), CV_8U, (void*) referenceBuffer.data());
        cv::Mat toAlignImage(toAlignBuffer.height(), toAlignBuffer.width(), CV_8U, (void*) toAlignBuffer.data());

        auto detector = cv::ORB::create(2000);

        std::vector<cv::KeyPoint> keypoints1, keypoints2;
        cv::Mat descriptors1, descriptors2;
        auto extractor = cv::xfeatures2d::BriefDescriptorExtractor::create();

        detector->detect(referenceImage, keypoints1);
        detector->detect(toAlignImage, keypoints2);

        extractor->compute(referenceImage, keypoints1, descriptors1);
        extractor->compute(toAlignImage, keypoints2, descriptors2);

        auto matcher = cv::BFMatcher::create(cv::NORM_HAMMING, false);
        std::vector< std::vector<cv::DMatch> > knn_matches;
        
        matcher->knnMatch( descriptors1, descriptors2, knn_matches, 2 );
        
        // Filter matches using the Lowe's ratio test
        const float ratioThresh = 0.75f;
        std::vector<cv::DMatch> goodMatches;

        for (auto& m : knn_matches)
        {
            if (m[0].distance < ratioThresh * m[1].distance)
                goodMatches.push_back(m[0]);
        }

        std::vector<cv::Point2f> obj;
        std::vector<cv::Point2f> scene;
        
        for(auto& m : goodMatches)
        {
            obj.push_back( keypoints1[ m.queryIdx ].pt );
            scene.push_back( keypoints2[ m.trainIdx ].pt );
        }
        
        if(obj.empty()) {
            return cv::Mat();
        }

        return findHomography( scene, obj, cv::RANSAC );
    }

    cv::Mat ImageProcessor::calcHistogram(const RawCameraMetadata& cameraMetadata,
                                          const RawImageBuffer& buffer,
                                          const bool cumulative,
                                          const int downscale)
    {
        //Measure measure("calcHistogram()");
        const int halfWidth  = buffer.width / 2;
        const int halfHeight = buffer.height / 2;

        NativeBufferContext inputBufferContext(*buffer.data, false);
        Halide::Runtime::Buffer<float> shadingMapBuffer[4];
        Halide::Runtime::Buffer<uint32_t> histogramBuffer(2u << 7u);

        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        cv::Vec3f cameraWhite;
        
        createSrgbMatrix(cameraMetadata, buffer.metadata, buffer.metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);

        cv::Mat cameraToSrgb = pcsToSrgb * cameraToPcs;

        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = ToHalideBuffer<float>(cameraToSrgb);

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(buffer.metadata.lensShadingMap[i]);
        }

        measure_image(inputBufferContext.getHalideBuffer(),
                      buffer.rowStride,
                      static_cast<int>(buffer.pixelFormat),
                      halfWidth,
                      halfHeight,
                      downscale,
                      cameraMetadata.blackLevel[0],
                      cameraMetadata.blackLevel[1],
                      cameraMetadata.blackLevel[2],
                      cameraMetadata.blackLevel[3],
                      cameraMetadata.whiteLevel,
                      cameraWhite[0],
                      cameraWhite[1],
                      cameraWhite[2],
                      cameraToSrgbBuffer,
                      shadingMapBuffer[0],
                      shadingMapBuffer[1],
                      shadingMapBuffer[2],
                      shadingMapBuffer[3],
                      static_cast<int>(cameraMetadata.sensorArrangment),
                      histogramBuffer);
        
        cv::Mat histogram(histogramBuffer.height(), histogramBuffer.width(), CV_32S, histogramBuffer.data());
        
        histogram.convertTo(histogram, CV_32F);
        
        if(cumulative) {
            for(int i = 1; i < histogram.cols; i++) {
                histogram.at<float>(i) += histogram.at<float>(i - 1);
            }
            
            histogram /= histogram.at<float>(histogram.cols - 1);
        }
        else {
            for(int i = 0; i < histogram.cols; i++) {
                histogram.at<float>(i) /= (halfWidth/downscale * halfHeight/downscale);
            }
        }
        
        return histogram;
    }

    void ImageProcessor::matchExposures(
        const RawCameraMetadata& cameraMetadata, const RawImageBuffer& reference, const RawImageBuffer& toMatch, float& outScale, float& outWhitePoint)
    {
        auto refHistogram = calcHistogram(cameraMetadata, reference, true, 4);
        auto toMatchHistogram = calcHistogram(cameraMetadata, toMatch, true, 4);
        
        std::vector<float> matches;
        
        for(int i = 0; i < toMatchHistogram.cols; i++) {
            float a = toMatchHistogram.at<float>(i);

            for(int j = 1; j < refHistogram.cols; j++) {
                float b = refHistogram.at<float>(j);

                if(a <= b) {
                    double match = j / (i + 1.0);
                    matches.push_back(match);
                    break;
                }
            }
        }
        
        float scale = 0;
        
        int Imin = std::min((int) matches.size(), 4);
        int Imax = std::min((int) matches.size(), 32);
        
        for(int i = Imin; i < Imax; i++) {
            scale += matches[i];
        }
        
        if(Imax - Imin <= 0)
            outScale = 1.0f;
        else
            outScale = scale / (float) (Imax - Imin);
        
        outWhitePoint = 1;
    }

    void ImageProcessor::process(RawContainer& rawContainer, const std::string& outputPath, const ImageProcessorProgress& progressListener)
    {
        // If this is a HDR capture then find the underexposed images.
        std::vector<std::shared_ptr<RawImageBuffer>> underexposedImages;
        
        // Started
        progressListener.onProgressUpdate(0);
        
        if(rawContainer.isHdr()) {
            double maxEv = -1e10;
            double minEv = 1e10;
            
            // Figure out where the base & underexposed images are
            for(auto frameName : rawContainer.getFrames()) {
                auto frame = rawContainer.getFrame(frameName);
                auto ev = calcEv(rawContainer.getCameraMetadata(), frame->metadata);
                
                if(ev > maxEv)
                    maxEv = ev;
                
                if(ev < minEv)
                    minEv = ev;
            }
                        
            // Make sure there's enough of a difference between the base and underexposed images
            if(std::abs(maxEv - minEv) > 0.49) {
                for(auto frameName : rawContainer.getFrames()) {
                    auto frame = rawContainer.getFrame(frameName);
                    auto ev = calcEv(rawContainer.getCameraMetadata(), frame->metadata);
                
                    if(std::abs(ev - maxEv) < std::abs(ev - minEv)) {
                        // Load the frame since we intend to remove it from the container
                        auto raw = rawContainer.loadFrame(frameName);
                        underexposedImages.push_back(raw);
                        
                        rawContainer.removeFrame(frameName);
                    }
                }
            }
        }

        auto referenceRawBuffer = rawContainer.loadFrame(rawContainer.getReferenceImage());
        PostProcessSettings settings = rawContainer.getPostProcessSettings();
        
        // Estimate black point of not provided
        if(settings.blacks < 0) {
            estimateBlacks(*referenceRawBuffer,
                           rawContainer.getCameraMetadata(),
                           settings.shadows,
                           settings.blacks);
        }
        
        // Estimate white point if not supplied
        if(settings.whitePoint < 0) {
            estimateWhitePoint(*referenceRawBuffer,
                               rawContainer.getCameraMetadata(),
                               settings.shadows,
                               settings.blacks,
                               0.999f,
                               settings.whitePoint);
        }

        //
        // Save preview
        //
        
        auto preview = createPreview(*referenceRawBuffer, 2, rawContainer.getCameraMetadata(), settings);
        std::string basePath, filename;

        cv::Mat previewImage(preview.height(), preview.width(), CV_8UC4, preview.data());
                
        util::GetBasePath(outputPath, basePath, filename);
        std::string previewPath = basePath + "/PREVIEW_" + filename;
        
        cv::cvtColor(previewImage, previewImage, cv::COLOR_RGBA2BGR);
        cv::imwrite(previewPath, previewImage);
        
        // Parse the returned metadata
        std::string metadataJson = progressListener.onPreviewSaved(previewPath);
        PreviewMetadata previewMetadata(metadataJson);

        // Adjust shadows to lighten any faces in the image
        float shadowsScale = adjustShadowsForFaces(previewImage, previewMetadata);
        logger::log("Adjusting shadows by " + std::to_string(shadowsScale));
        
        settings.shadows *= shadowsScale;

        previewImage.release();

        //
        // HDR
        //
        
        shared_ptr<HdrMetadata> hdrMetadata;
        shared_ptr<RawImageBuffer> underExposedImage;
        
        if(!underexposedImages.empty()) {
            auto referenceRawBuffer = rawContainer.loadFrame(rawContainer.getReferenceImage());
            auto underexposedFrameIt = underexposedImages.begin();

            auto hist = calcHistogram(rawContainer.getCameraMetadata(), *referenceRawBuffer, false, 4);
            const int bound = (int) (hist.cols * 0.95f);
            float sum = 0;

            for(int x = hist.cols - 1; x >= bound; x--) {
                sum += hist.at<float>(x);
            }

            // Check if there's any point even using the underexposed image (less than 0.1% in the >95% bins)
            float p = sum * 100.0f;
            if(p < 0.01f) {
                logger::log("Skipping HDR processing (" + std::to_string(p) + ")");
            }
            // Try each underexposed image
            else {
                logger::log("Using HDR processing (" + std::to_string(p) + ")");
                
                while(underexposedFrameIt != underexposedImages.end()) {
                    hdrMetadata =
                        prepareHdr(rawContainer.getCameraMetadata(),
                                   settings,
                                   *referenceRawBuffer,
                                   *(*underexposedFrameIt));

                    if(hdrMetadata->error < MAX_HDR_ERROR) {
                        logger::log("HDR error (" + std::to_string(hdrMetadata->error) + ")");
                        
                        // Reduce the shadows if applying HDR to avoid the image looking too flat due to
                        // extreme dynamic range compression
                        settings.shadows = std::max(0.75f * settings.shadows, 4.0f);
                        underExposedImage = *underexposedFrameIt;
                        
                        // Update white point
                        estimateWhitePoint(*underExposedImage,
                                           rawContainer.getCameraMetadata(),
                                           settings.shadows * (1.0f/hdrMetadata->exposureScale),
                                           settings.blacks,
                                           0.999f,
                                           settings.whitePoint);

                        break;
                    }
                    else {
                        logger::log("HDR error too high (" + std::to_string(hdrMetadata->error) + ")");
                    }

                    hdrMetadata = nullptr;
                    ++underexposedFrameIt;
                }
            }
        }
        
        //
        // Denoise
        //
        
        ImageProgressHelper progressHelper(progressListener, static_cast<int>(rawContainer.getFrames().size()), 0);
        
        std::vector<Halide::Runtime::Buffer<uint16_t>> denoiseOutput;
        denoiseOutput = denoise(rawContainer, progressHelper);
        
        progressHelper.denoiseCompleted();
        
        //
        // Post process
        //
        
        const int rawWidth  = referenceRawBuffer->width / 2;
        const int rawHeight = referenceRawBuffer->height / 2;

        const int T = pow(2, DENOISE_LEVELS);
        
        const int offsetX = static_cast<int>(T * ceil(rawWidth / (double) T) - rawWidth);
        const int offsetY = static_cast<int>(T * ceil(rawHeight / (double) T) - rawHeight);

        // Check if we should write a DNG file
#ifdef DNG_SUPPORT
        if(rawContainer.getPostProcessSettings().dng) {
            std::vector<cv::Mat> rawChannels;
            rawChannels.reserve(4);

            for(int i = 0; i < 4; i++) {
                rawChannels.emplace_back(denoiseOutput[i].height(), denoiseOutput[i].width(), CV_16U, denoiseOutput[i].data());
            }

            switch(rawContainer.getCameraMetadata().sensorArrangment) {
                default:
                case ColorFilterArrangment::RGGB:
                    // All good
                    break;

                case ColorFilterArrangment::GRBG:
                {
                    std::vector<cv::Mat> tmp = rawChannels;
                    
                    rawChannels[0] = tmp[1];
                    rawChannels[1] = tmp[0];
                    rawChannels[2] = tmp[3];
                    rawChannels[3] = tmp[2];
                }
                break;

                case ColorFilterArrangment::GBRG:
                {
                    std::vector<cv::Mat> tmp = rawChannels;
                    
                    rawChannels[0] = tmp[2];
                    rawChannels[1] = tmp[0];
                    rawChannels[2] = tmp[3];
                    rawChannels[3] = tmp[1];
                }
                break;

                case ColorFilterArrangment::BGGR:
                    std::swap(rawChannels[0], rawChannels[3]);
                    break;
            }

            cv::Mat rawImage = buildRawImage(rawChannels, offsetX, offsetY);
            
            size_t extensionStartIdx = outputPath.find_last_of('.');
            std::string rawOutputPath;
            
            if(extensionStartIdx != std::string::npos) {
                rawOutputPath = outputPath.substr(0, extensionStartIdx);
            }
            else {
                rawOutputPath = outputPath;
            }
            
            writeDng(rawImage, rawContainer.getCameraMetadata(), referenceRawBuffer->metadata, rawOutputPath + ".dng");
        }
#endif

        cv::Mat outputImage = postProcess(
            denoiseOutput,
            hdrMetadata,
            offsetX,
            offsetY,
            referenceRawBuffer->metadata,
            rawContainer.getCameraMetadata(),
            settings);
        
        progressHelper.postProcessCompleted();

        // Write image
        std::vector<int> writeParams = { cv::IMWRITE_JPEG_QUALITY, rawContainer.getPostProcessSettings().jpegQuality };
        cv::imwrite(outputPath, outputImage, writeParams);

        // Create thumbnail
        cv::Mat thumbnail;

        int width = 320;
        int height = (int) std::lround((outputImage.rows / (double) outputImage.cols) * width);

        cv::resize(outputImage, thumbnail, cv::Size(width, height));

        // Add exif data to the output image
        addExifMetadata(underExposedImage == nullptr ? referenceRawBuffer->metadata : underExposedImage->metadata,
                        thumbnail,
                        rawContainer.getCameraMetadata(),
                        rawContainer.getPostProcessSettings(),
                        outputPath);
        
        progressHelper.imageSaved();
    }

    void ImageProcessor::process(const std::string& inputPath,
                                 const std::string& outputPath,
                                 const ImageProcessorProgress& progressListener)
    {
        Measure measure("process()");

        // Open RAW container
        RawContainer rawContainer(inputPath);

        if(rawContainer.getFrames().empty()) {
            progressListener.onError("No frames found");
            return;
        }
        
        process(rawContainer, outputPath, progressListener);
    }

    float ImageProcessor::adjustShadowsForFaces(cv::Mat input, PreviewMetadata& metadata) {
        if(metadata.faces.empty())
            return 1.0f;

        cv::Mat gray;
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);

        float L = 0;
        
        for(auto f : metadata.faces) {
            auto m = cv::mean(gray(f & cv::Rect(0, 0, input.cols, input.rows)));
            L += m[0];
        }

        L = L / (float) metadata.faces.size();
        return std::min(4.0f, std::max(1.0f, 128.0f / L));
    }
    
    void ImageProcessor::addExifMetadata(const RawImageMetadata& metadata,
                                         const cv::Mat& thumbnail,
                                         const RawCameraMetadata& cameraMetadata,
                                         const PostProcessSettings& settings,
                                         const std::string& inputOutput)
    {
        auto image = Exiv2::ImageFactory::open(inputOutput);
        if(image.get() == nullptr)
            return;
        
        image->readMetadata();
        
        Exiv2::ExifData& exifData = image->exifData();
        
        // sRGB color space
        exifData["Exif.Photo.ColorSpace"]       = uint16_t(1);
        
        // Capture settings
        exifData["Exif.Photo.ISOSpeedRatings"]  = uint16_t(metadata.iso);
        exifData["Exif.Photo.ExposureTime"]     = Exiv2::floatToRationalCast(metadata.exposureTime / ((float) 1e9));
        
        switch(metadata.screenOrientation)
        {
            default:
            case ScreenOrientation::LANDSCAPE:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(2) : uint16_t(1);
                break;
                
            case ScreenOrientation::PORTRAIT:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(5) : uint16_t(6);
                break;
                                
            case ScreenOrientation::REVERSE_LANDSCAPE:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(4) : uint16_t(3);
                break;
                
            case ScreenOrientation::REVERSE_PORTRAIT:
                exifData["Exif.Image.Orientation"] = settings.flipped ? uint16_t(7) : uint16_t(8);
                break;
        }
                
        if(!cameraMetadata.apertures.empty())
            exifData["Exif.Photo.ApertureValue"] = Exiv2::floatToRationalCast(cameraMetadata.apertures[0]);

        if(!cameraMetadata.focalLengths.empty())
            exifData["Exif.Photo.FocalLength"] = Exiv2::floatToRationalCast(cameraMetadata.focalLengths[0]);
        
        // Misc bits
        exifData["Exif.Photo.LensModel"]   = "MotionCam";
        exifData["Exif.Photo.LensMake"]    = "MotionCam";
        
        exifData["Exif.Photo.SceneType"]    = uint8_t(1);
        exifData["Exif.Image.XResolution"]  = Exiv2::Rational(72, 1);
        exifData["Exif.Image.YResolution"]  = Exiv2::Rational(72, 1);
        exifData["Exif.Photo.WhiteBalance"] = uint8_t(0);
        
        // Store GPS coords
        if(!settings.gpsTime.empty()) {
            exifData["Exif.GPSInfo.GPSProcessingMethod"]    = "65 83 67 73 73 0 0 0 72 89 66 82 73 68 45 70 73 88"; // ASCII HYBRID-FIX
            exifData["Exif.GPSInfo.GPSVersionID"]           = "2 2 0 0";
            exifData["Exif.GPSInfo.GPSMapDatum"]            = "WGS-84";
            
            exifData["Exif.GPSInfo.GPSLatitude"]            = toExifString(settings.gpsLatitude, true, true);
            exifData["Exif.GPSInfo.GPSLatitudeRef"]         = settings.gpsLatitude > 0 ? "N" : "S";

            exifData["Exif.GPSInfo.GPSLongitude"]           = toExifString(settings.gpsLongitude, true, false);
            exifData["Exif.GPSInfo.GPSLongitudeRef"]        = settings.gpsLongitude > 0 ? "E" : "W";
            
            exifData["Exif.GPSInfo.GPSAltitude"]            = toExifString(settings.gpsAltitude);
            exifData["Exif.GPSInfo.GPSAltitudeRef"]         = settings.gpsAltitude < 0.0 ? "1" : "0";
            
            exifData["Exif.Image.GPSTag"]                   = 4908;
        }
        
        // Set thumbnail
        if(!thumbnail.empty()) {
            Exiv2::ExifThumb exifThumb(exifData);
            std::vector<uint8_t> thumbnailBuffer;
            
            cv::imencode(".jpg", thumbnail, thumbnailBuffer);
            
            exifThumb.setJpegThumbnail(thumbnailBuffer.data(), thumbnailBuffer.size());
        }
        
        image->writeMetadata();
    }

    double ImageProcessor::measureSharpness(const RawImageBuffer& rawBuffer) {
//        Measure measure("measureSharpness()");
        
        int halfWidth  = rawBuffer.width / 2;
        int halfHeight = rawBuffer.height / 2;

        NativeBufferContext inputBufferContext(*rawBuffer.data, false);
        Halide::Runtime::Buffer<uint16_t> outputBuffer(halfWidth, halfHeight);
                
        generate_edges(inputBufferContext.getHalideBuffer(),
                       rawBuffer.rowStride,
                       static_cast<int>(rawBuffer.pixelFormat),
                       halfWidth,
                       halfHeight,
                       outputBuffer);
        
        outputBuffer.device_sync();
        outputBuffer.copy_to_host();
        
        cv::Mat output(outputBuffer.height(), outputBuffer.width(), CV_16U, outputBuffer.data());

        return cv::mean(output)[0];
    }

    std::vector<Halide::Runtime::Buffer<uint16_t>> ImageProcessor::denoise(RawContainer& rawContainer, ImageProgressHelper& progressHelper)
    {
        Measure measure("denoise()");
        
        typedef Halide::Runtime::Buffer<float> WaveletBuffer;
        
        std::shared_ptr<RawImageBuffer> referenceRawBuffer = rawContainer.loadFrame(rawContainer.getReferenceImage());
        auto reference = loadRawImage(*referenceRawBuffer, rawContainer.getCameraMetadata());
                
        std::vector<Halide::Runtime::Buffer<uint16_t>> result;
        
        cv::Mat referenceFlowImage(reference->previewBuffer.height(), reference->previewBuffer.width(), CV_8U, reference->previewBuffer.data());
        Halide::Runtime::Buffer<float> fuseOutput(reference->rawBuffer.width(), reference->rawBuffer.height(), 4);
        
        fuseOutput.fill(0);
        
        auto processFrames = rawContainer.getFrames();
        auto it = processFrames.begin();
        
        // Pixels that have moved a lot will contribute less since we are less certain about them
        float motionVectorsWeight = 20*20;
        
        // Linearly increase difference threshold based on the exposure value
        float ev = calcEv(rawContainer.getCameraMetadata(), reference->metadata);
        float differenceWeight = std::max(1.0f, std::min(16.0f, -ev + 16.0f));
        
        while(it != processFrames.end()) {
            if(rawContainer.getReferenceImage() == *it) {
                ++it;
                continue;
            }
            
            auto frame = rawContainer.loadFrame(*it);
            auto current = loadRawImage(*frame, rawContainer.getCameraMetadata());
            
            cv::Mat flow;
            cv::Mat currentFlowImage(current->previewBuffer.height(),
                                     current->previewBuffer.width(),
                                     CV_8U,
                                     current->previewBuffer.data());

            cv::Ptr<cv::DISOpticalFlow> opticalFlow =
                cv::DISOpticalFlow::create(cv::DISOpticalFlow::PRESET_ULTRAFAST);
            
            opticalFlow->setPatchSize(16);
            opticalFlow->setPatchStride(8);
            opticalFlow->calc(referenceFlowImage, currentFlowImage, flow);
            
            Halide::Runtime::Buffer<float> flowBuffer =
                Halide::Runtime::Buffer<float>::make_interleaved((float*) flow.data, flow.cols, flow.rows, 2);
            
                        
            fuse_denoise(reference->rawBuffer,
                         current->rawBuffer,
                         fuseOutput,
                         flowBuffer,
                         reference->rawBuffer.width(),
                         reference->rawBuffer.height(),
                         rawContainer.getCameraMetadata().whiteLevel,
                         motionVectorsWeight,
                         differenceWeight,
                         fuseOutput);
            
            progressHelper.nextFusedImage();

            ++it;
        }
        
        const int width = reference->rawBuffer.width();
        const int height = reference->rawBuffer.height();

        Halide::Runtime::Buffer<uint16_t> denoiseInput(width, height, 4);
        
        if(processFrames.size() <= 1)
            denoiseInput.for_each_element([&](int x, int y, int c) {
                float p = reference->rawBuffer(x, y, c) - rawContainer.getCameraMetadata().blackLevel[c];
                float s = EXPANDED_RANGE / (float) (rawContainer.getCameraMetadata().whiteLevel-rawContainer.getCameraMetadata().blackLevel[c]);
                
                denoiseInput(x, y, c) = static_cast<uint16_t>( std::max(0.0f, std::min(p * s, (float) EXPANDED_RANGE) )) ;
            });
        else {
            const float n = (float) processFrames.size() - 1;

            denoiseInput.for_each_element([&](int x, int y, int c) {
                float p = fuseOutput(x, y, c) / n - rawContainer.getCameraMetadata().blackLevel[c];
                float s = EXPANDED_RANGE / (float) (rawContainer.getCameraMetadata().whiteLevel-rawContainer.getCameraMetadata().blackLevel[c]);
                
                denoiseInput(x, y, c) = static_cast<uint16_t>( std::max(0.0f, std::min(p * s, (float) EXPANDED_RANGE) ) ) ;
            });
        }
        
        // Don't need this anymore
        reference->rawBuffer = Halide::Runtime::Buffer<uint16_t>();

        //
        // Spatial denoising
        //

        std::vector<Halide::Runtime::Buffer<uint16_t>> denoiseOutput;

        if(rawContainer.getPostProcessSettings().spatialDenoiseAggressiveness > 0) {
            vector<vector<WaveletBuffer>> refWavelet;
            vector<float> noiseSigma;

            for(int c = 0; c < 4; c++) {
                refWavelet.push_back(
                     createWaveletBuffers(denoiseInput.width(), denoiseInput.height()));

                forward_transform(denoiseInput,
                                  denoiseInput.width(),
                                  denoiseInput.height(),
                                  c,
                                  refWavelet[c][0],
                                  refWavelet[c][1],
                                  refWavelet[c][2],
                                  refWavelet[c][3],
                                  refWavelet[c][4],
                                  refWavelet[c][5]);

                int offset = 3 * refWavelet[c][0].stride(2);

                cv::Mat hh(refWavelet[c][0].height(), refWavelet[c][0].width(), CV_32F, refWavelet[c][0].data() + offset);
                noiseSigma.push_back(estimateNoise(hh));
            }

            // Invert output wavelet
            for(int c = 0; c < 4; c++) {
                Halide::Runtime::Buffer<uint16_t> outputBuffer(width, height);
                
                inverse_transform(refWavelet[c][0],
                                  refWavelet[c][1],
                                  refWavelet[c][2],
                                  refWavelet[c][3],
                                  refWavelet[c][4],
                                  refWavelet[c][5],
                                  rawContainer.getPostProcessSettings().spatialDenoiseAggressiveness*noiseSigma[c],
                                  false,
                                  1,
                                  1,
                                  outputBuffer);

                // Clean up
                denoiseOutput.push_back(outputBuffer);
            }
        }
        else {
            for(int c = 0; c < 4; c++) {
                denoiseOutput.push_back(denoiseInput.sliced(2, c));
            }
        }
        
        return denoiseOutput;
    }

#ifdef DNG_SUPPORT
    cv::Mat ImageProcessor::buildRawImage(vector<cv::Mat> channels, int cropX, int cropY) {
        const uint32_t height = channels[0].rows * 2;
        const uint32_t width  = channels[1].cols * 2;
        
        cv::Mat outputImage(height, width, CV_16U);
        
        for (int y = 0; y < height; y+=2) {
            auto* outRow1 = outputImage.ptr<uint16_t>(y);
            auto* outRow2 = outputImage.ptr<uint16_t>(y + 1);
            
            int ry = y / 2;
            
            const uint16_t* inC0 = channels[0].ptr<uint16_t>(ry);
            const uint16_t* inC1 = channels[1].ptr<uint16_t>(ry);
            const uint16_t* inC2 = channels[2].ptr<uint16_t>(ry);
            const uint16_t* inC3 = channels[3].ptr<uint16_t>(ry);
            
            for(int x = 0; x < width; x+=2) {
                int rx = x / 2;
                
                outRow1[x]      = inC0[rx];
                outRow1[x + 1]  = inC1[rx];
                outRow2[x]      = inC2[rx];
                outRow2[x + 1]  = inC3[rx];
            }
        }
        
        return outputImage(cv::Rect(cropX, cropY, width - cropX*2, height - cropY*2)).clone();
    }

    void ImageProcessor::writeDng(cv::Mat& rawImage,
                                  const RawCameraMetadata& cameraMetadata,
                                  const RawImageMetadata& imageMetadata,
                                  const std::string& outputPath)
    {
        Measure measure("writeDng()");
        
        const int width  = rawImage.cols;
        const int height = rawImage.rows;
        
        dng_host host;

        host.SetSaveLinearDNG(false);
        host.SetSaveDNGVersion(dngVersion_SaveDefault);
        
        AutoPtr<dng_negative> negative(host.Make_dng_negative());
        
        // Create lens shading map for each channel
        for(int c = 0; c < 4; c++) {
            dng_point channelGainMapPoints(imageMetadata.lensShadingMap[c].rows, imageMetadata.lensShadingMap[c].cols);
            
            AutoPtr<dng_gain_map> gainMap(new dng_gain_map(host.Allocator(),
                                                           channelGainMapPoints,
                                                           dng_point_real64(1.0 / (imageMetadata.lensShadingMap[c].rows), 1.0 / (imageMetadata.lensShadingMap[c].cols)),
                                                           dng_point_real64(0, 0),
                                                           1));
            
            for(int y = 0; y < imageMetadata.lensShadingMap[c].rows; y++) {
                for(int x = 0; x < imageMetadata.lensShadingMap[c].cols; x++) {
                    gainMap->Entry(y, x, 0) = imageMetadata.lensShadingMap[c].at<float>(y, x);
                }
            }
            
            int left = 0;
            int top  = 0;
            
            if(c == 0) {
                left = 0;
                top = 0;
            }
            else if(c == 1) {
                left = 1;
                top = 0;
            }
            else if(c == 2) {
                left = 0;
                top = 1;
            }
            else if(c == 3) {
                left = 1;
                top = 1;
            }
            
            dng_rect gainMapArea(top, left, height, width);
            AutoPtr<dng_opcode> gainMapOpCode(new dng_opcode_GainMap(dng_area_spec(gainMapArea, 0, 1, 2, 2), gainMap));
            
            negative->OpcodeList2().Append(gainMapOpCode);
        }
        
        negative->SetModelName("MotionCam");
        negative->SetLocalName("MotionCam");
        
        // We always use RGGB at this point
        negative->SetColorKeys(colorKeyRed, colorKeyGreen, colorKeyBlue);
                
        negative->SetBayerMosaic(1);
        negative->SetColorChannels(3);
        
        negative->SetQuadBlacks(0, 0, 0, 0);
        negative->SetWhiteLevel(EXPANDED_RANGE);
        
        // Square pixels
        negative->SetDefaultScale(dng_urational(1,1), dng_urational(1,1));
        
        negative->SetDefaultCropSize(width, height);
        negative->SetNoiseReductionApplied(dng_urational(1,1));
        negative->SetCameraNeutral(dng_vector_3(imageMetadata.asShot[0], imageMetadata.asShot[1], imageMetadata.asShot[2]));

        dng_orientation orientation;
        
        switch(imageMetadata.screenOrientation)
        {
            default:
            case ScreenOrientation::PORTRAIT:
                orientation = dng_orientation::Rotate90CW();
                break;
            
            case ScreenOrientation::REVERSE_PORTRAIT:
                orientation = dng_orientation::Rotate90CCW();
                break;
                
            case ScreenOrientation::LANDSCAPE:
                orientation = dng_orientation::Normal();
                break;
                
            case ScreenOrientation::REVERSE_LANDSCAPE:
                orientation = dng_orientation::Rotate180();
                break;
        }
        
        negative->SetBaseOrientation(orientation);

        // Set up camera profile
        AutoPtr<dng_camera_profile> cameraProfile(new dng_camera_profile());
        
        // Color matrices
        cv::Mat color1 = cameraMetadata.colorMatrix1;
        cv::Mat color2 = cameraMetadata.colorMatrix2;
        
        dng_matrix_3by3 dngColor1 = dng_matrix_3by3(color1.at<float>(0, 0), color1.at<float>(0, 1), color1.at<float>(0, 2),
                                                    color1.at<float>(1, 0), color1.at<float>(1, 1), color1.at<float>(1, 2),
                                                    color1.at<float>(2, 0), color1.at<float>(2, 1), color1.at<float>(2, 2));
        
        dng_matrix_3by3 dngColor2 = dng_matrix_3by3(color2.at<float>(0, 0), color2.at<float>(0, 1), color2.at<float>(0, 2),
                                                    color2.at<float>(1, 0), color2.at<float>(1, 1), color2.at<float>(1, 2),
                                                    color2.at<float>(2, 0), color2.at<float>(2, 1), color2.at<float>(2, 2));
        
        cameraProfile->SetColorMatrix1(dngColor1);
        cameraProfile->SetColorMatrix2(dngColor2);
        
        // Forward matrices
        cv::Mat forward1 = cameraMetadata.forwardMatrix1;
        cv::Mat forward2 = cameraMetadata.forwardMatrix2;
        
        if(!forward1.empty() && !forward2.empty()) {
            dng_matrix_3by3 dngForward1 = dng_matrix_3by3( forward1.at<float>(0, 0), forward1.at<float>(0, 1), forward1.at<float>(0, 2),
                                                           forward1.at<float>(1, 0), forward1.at<float>(1, 1), forward1.at<float>(1, 2),
                                                           forward1.at<float>(2, 0), forward1.at<float>(2, 1), forward1.at<float>(2, 2) );
            
            dng_matrix_3by3 dngForward2 = dng_matrix_3by3( forward2.at<float>(0, 0), forward2.at<float>(0, 1), forward2.at<float>(0, 2),
                                                           forward2.at<float>(1, 0), forward2.at<float>(1, 1), forward2.at<float>(1, 2),
                                                           forward2.at<float>(2, 0), forward2.at<float>(2, 1), forward2.at<float>(2, 2) );
            
            cameraProfile->SetForwardMatrix1(dngForward1);
            cameraProfile->SetForwardMatrix2(dngForward2);
        }
        
        uint32_t illuminant1 = 0;
        uint32_t illuminant2 = 0;
        
        // Convert to DNG format
        switch(cameraMetadata.colorIlluminant1) {
            case color::StandardA:
                illuminant1 = lsStandardLightA;
                break;
            case color::StandardB:
                illuminant1 = lsStandardLightB;
                break;
            case color::StandardC:
                illuminant1 = lsStandardLightC;
                break;
            case color::D50:
                illuminant1 = lsD50;
                break;
            case color::D55:
                illuminant1 = lsD55;
                break;
            case color::D65:
                illuminant1 = lsD65;
                break;
            case color::D75:
                illuminant1 = lsD75;
                break;
        }
        
        switch(cameraMetadata.colorIlluminant2) {
            case color::StandardA:
                illuminant2 = lsStandardLightA;
                break;
            case color::StandardB:
                illuminant2 = lsStandardLightB;
                break;
            case color::StandardC:
                illuminant2 = lsStandardLightC;
                break;
            case color::D50:
                illuminant2 = lsD50;
                break;
            case color::D55:
                illuminant2 = lsD55;
                break;
            case color::D65:
                illuminant2 = lsD65;
                break;
            case color::D75:
                illuminant2 = lsD75;
                break;
        }
        
        cameraProfile->SetCalibrationIlluminant1(illuminant1);
        cameraProfile->SetCalibrationIlluminant2(illuminant2);
        
        cameraProfile->SetName("MotionCam");
        cameraProfile->SetEmbedPolicy(pepAllowCopying);
        
        // This ensures profile is saved
        cameraProfile->SetWasReadFromDNG();
        
        negative->AddProfile(cameraProfile);
        
        // Finally add the raw data to the negative
        dng_rect dngArea(height, width);
        dng_pixel_buffer dngBuffer;

        AutoPtr<dng_image> dngImage(host.Make_dng_image(dngArea, 1, ttShort));

        dngBuffer.fArea         = dngArea;
        dngBuffer.fPlane        = 0;
        dngBuffer.fPlanes       = 1;
        dngBuffer.fRowStep      = dngBuffer.fPlanes * width;
        dngBuffer.fColStep      = dngBuffer.fPlanes;
        dngBuffer.fPixelType    = ttShort;
        dngBuffer.fPixelSize    = TagTypeSize(ttShort);
        dngBuffer.fData         = rawImage.ptr();
        
        dngImage->Put(dngBuffer);
        
        // Build the DNG images
        negative->SetStage1Image(dngImage);
        negative->BuildStage2Image(host);
        negative->BuildStage3Image(host);
        
        negative->SynchronizeMetadata();
        
        // Create stream writer for output file
        dng_file_stream dngStream(outputPath.c_str(), true);
        
        // Write DNG file to disk
        AutoPtr<dng_image_writer> dngWriter(new dng_image_writer());
        
        dngWriter->WriteDNG(host, dngStream, *negative.Get(), nullptr, ccUncompressed);
    }
#endif // DNG_SUPPORT

    std::shared_ptr<HdrMetadata> ImageProcessor::prepareHdr(const RawCameraMetadata& cameraMetadata,
                                                            const PostProcessSettings& settings,
                                                            const RawImageBuffer& reference,
                                                            const RawImageBuffer& underexposed)
    {
        Measure measure("prepareHdr()");
        
        // Match exposures
        float exposureScale, whitePoint;
        
        matchExposures(cameraMetadata, reference, underexposed, exposureScale, whitePoint);
                
        //
        // Register images
        //
        
        const bool extendEdges = true;
        
        auto refImage = loadRawImage(reference, cameraMetadata, extendEdges, 1.0f);
        auto underexposedImage = loadRawImage(underexposed, cameraMetadata, extendEdges, exposureScale);
                
        auto warpMatrix = registerImage(refImage->previewBuffer, underexposedImage->previewBuffer);
        if(warpMatrix.empty())
            return nullptr;
        
        //
        // Create mask
        //

        cv::Mat underExposedExposure(
            underexposedImage->previewBuffer.height(), underexposedImage->previewBuffer.width(), CV_8U, underexposedImage->previewBuffer.data());
        cv::Mat alignedExposure;

        cv::warpPerspective(underExposedExposure, alignedExposure, warpMatrix, underExposedExposure.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);

        Halide::Runtime::Buffer<uint8_t> alignedBuffer = ToHalideBuffer<uint8_t>(alignedExposure);
        Halide::Runtime::Buffer<uint8_t> ghostMapBuffer(alignedBuffer.width(), alignedBuffer.height());
        Halide::Runtime::Buffer<uint8_t> maskBuffer(alignedBuffer.width(), alignedBuffer.height());
        
        hdr_mask(refImage->previewBuffer, alignedBuffer, 4.0f, ghostMapBuffer, maskBuffer);
                
        // Calculate error
        cv::Mat ghostMap(ghostMapBuffer.height(), ghostMapBuffer.width(), CV_8U, ghostMapBuffer.data());
        float error = cv::mean(ghostMap)[0] * 100;
        
        //
        // Create input image for post processing
        //
        
        Halide::Runtime::Buffer<float> shadingMapBuffer[4];
        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = ToHalideBuffer<float>(underexposedImage->metadata.lensShadingMap[i]);
        }

        cv::Mat cameraToSrgb;
        cv::Vec3f cameraWhite;
                
        // Warp the underexposed image
        cv::Mat alignedChannels[4];
        Halide::Runtime::Buffer<uint16_t> outputBuffer;
        Halide::Runtime::Buffer<uint16_t> inputBuffers[4];

        for(int c = 0; c < 4; c++) {
            int offset = c * underexposedImage->rawBuffer.stride(2);
            cv::Mat channel(underexposedImage->rawBuffer.height(), underexposedImage->rawBuffer.width(), CV_16U, underexposedImage->rawBuffer.data() + offset);

            cv::warpPerspective(channel, alignedChannels[c], warpMatrix, channel.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
            
            inputBuffers[c] = ToHalideBuffer<uint16_t>(alignedChannels[c]);
        }
        
        cv::Mat mask(maskBuffer.height(), maskBuffer.width(), CV_8U, maskBuffer.data());
        outputBuffer = Halide::Runtime::Buffer<uint16_t>(underexposedImage->rawBuffer.width()*2, underexposedImage->rawBuffer.height()*2, 3);
        
        // Upscale and blur mask
        cv::GaussianBlur(mask, mask, cv::Size(11, 11), -1);
        cv::resize(mask, mask, cv::Size(mask.cols*2, mask.rows*2));
        
        cv::Mat cameraToPcs;
        cv::Mat pcsToSrgb;
        
        if(settings.temperature > 0 || settings.tint > 0) {
            Temperature t(settings.temperature, settings.tint);

            createSrgbMatrix(cameraMetadata, underexposed.metadata, t, cameraWhite, cameraToPcs, pcsToSrgb);
        }
        else {
            createSrgbMatrix(cameraMetadata, underexposed.metadata, underexposed.metadata.asShot, cameraWhite, cameraToPcs, pcsToSrgb);
        }

        Halide::Runtime::Buffer<float> cameraToPcsBuffer = ToHalideBuffer<float>(cameraToPcs);
        
        linear_image(inputBuffers[0],
                    inputBuffers[1],
                    inputBuffers[2],
                    inputBuffers[3],
                    shadingMapBuffer[0],
                    shadingMapBuffer[1],
                    shadingMapBuffer[2],
                    shadingMapBuffer[3],
                    cameraWhite[0],
                    cameraWhite[1],
                    cameraWhite[2],
                    cameraToPcsBuffer,
                    inputBuffers[0].width(),
                    inputBuffers[0].height(),
                    static_cast<int>(cameraMetadata.sensorArrangment),
                    cameraMetadata.blackLevel[0],
                    cameraMetadata.blackLevel[1],
                    cameraMetadata.blackLevel[2],
                    cameraMetadata.blackLevel[3],
                    cameraMetadata.whiteLevel,
                    whitePoint,
                    outputBuffer);
        
        //
        // Return HDR metadata
        //
        
        auto hdrMetadata = std::make_shared<HdrMetadata>();
        
        hdrMetadata->exposureScale  = 1.0f / exposureScale;
        hdrMetadata->hdrInput       = outputBuffer;
        hdrMetadata->mask           = ToHalideBuffer<uint8_t>(mask).copy();
        hdrMetadata->error          = error;
        
        return hdrMetadata;
    }
}
