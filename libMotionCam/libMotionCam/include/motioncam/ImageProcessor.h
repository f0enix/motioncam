#ifndef ImageProcessor_hpp
#define ImageProcessor_hpp

#include "motioncam/RawImageMetadata.h"
#include "motioncam/ImageProcessorProgress.h"

#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <HalideBuffer.h>

namespace motioncam {
    class RawImage;
    class RawContainer;
    class PostProcessSettings;
    class Temperature;
    struct RawData;
    struct HdrMetadata;
    
    class ImageProgressHelper {
    public:
        ImageProgressHelper(const ImageProcessorProgress& progressListener, int numImages, int start);

        void nextFusedImage();        
        void denoiseCompleted();
        void postProcessCompleted();
        void imageSaved();
        
    private:
        const ImageProcessorProgress& mProgressListener;
        int mStart;
        int mNumImages;
        double mPerImageIncrement;
        int mCurImage;
    };
    
    class ImageProcessor {
    public:
        static void process(const std::string& inputPath,
                            const std::string& outputPath,
                            const ImageProcessorProgress& progressListener);

        static void process(RawContainer& rawContainer, const std::string& outputPath, const ImageProcessorProgress& progressListener);

        static Halide::Runtime::Buffer<uint8_t> createPreview(const RawImageBuffer& rawBuffer,
                                                       const int downscaleFactor,
                                                       const RawCameraMetadata& cameraMetadata,
                                                       const PostProcessSettings& settings);
        
        static cv::Mat calcHistogram(const RawCameraMetadata& cameraMetadata,
                                     const RawImageBuffer& reference,
                                     const bool cumulative,
                                     const int downscale);

        static void estimateBasicSettings(const RawImageBuffer& rawBuffer, const RawCameraMetadata& cameraMetadata, PostProcessSettings& outSettings);
        static void estimateSettings(const RawImageBuffer& rawBuffer, const RawCameraMetadata& cameraMetadata, PostProcessSettings& outSettings);
        static float estimateShadows(const cv::Mat& histogram, float keyValue=0.22f);
        static float estimateExposureCompensation(const cv::Mat& histogram);
        static void estimateWhiteBalance(const RawImageBuffer& rawBuffer,
                                         const RawCameraMetadata& cameraMetadata,
                                         float& outR,
                                         float& outG,
                                         float& outB);

        static cv::Mat estimateBlacks(const RawImageBuffer& rawBuffer,
                                      const RawCameraMetadata& cameraMetadata,
                                      float shadows,
                                      float& outBlacks);
        
        static cv::Mat estimateWhitePoint(const RawImageBuffer& rawBuffer,
                                          const RawCameraMetadata& cameraMetadata,
                                          float shadows,
                                          float& outWhitePoint);

        static double measureSharpness(const RawImageBuffer& rawBuffer);

        static void measureImage(RawImageBuffer& rawImage, const RawCameraMetadata& cameraMetadata, float& outSceneLuminosity);
        
        static cv::Mat registerImage(const Halide::Runtime::Buffer<uint8_t>& referenceBuffer,
                                     const Halide::Runtime::Buffer<uint8_t>& toAlignBuffer,
                                     int scale=1);
        
        static float matchExposures(const RawCameraMetadata& cameraMetadata, const RawImageBuffer& reference, const RawImageBuffer& toMatch);

        static std::shared_ptr<RawData> loadRawImage(const RawImageBuffer& rawImage,
                                                     const RawCameraMetadata& cameraMetadata,
                                                     const bool extendEdges=true,
                                                     const float scalePreview=1.0f);
        
        static void createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                     const RawImageMetadata& rawImageMetadata,
                                     const Temperature& temperature,
                                     cv::Vec3f& cameraWhite,
                                     cv::Mat& outCameraToPcs,
                                     cv::Mat& outPcsToSrgb);

        static void createSrgbMatrix(const RawCameraMetadata& cameraMetadata,
                                     const RawImageMetadata& rawImageMetadata,
                                     const cv::Vec3f& asShot,
                                     cv::Vec3f& cameraWhite,
                                     cv::Mat& outCameraToPcs,
                                     cv::Mat& outPcsToSrgb);

        static std::vector<Halide::Runtime::Buffer<uint16_t>> denoise(const RawContainer& rawContainer, ImageProgressHelper& progressHelper);
        
        static void addExifMetadata(const RawImageMetadata& metadata,
                                    const cv::Mat& thumbnail,
                                    const RawCameraMetadata& cameraMetadata,
                                    const bool isFlipped,
                                    const std::string& inputOutput);

        static cv::Mat postProcess(std::vector<Halide::Runtime::Buffer<uint16_t>>& inputBuffers,
                                   const std::shared_ptr<HdrMetadata>& hdrMetadata,
                                   int offsetX,
                                   int offsetY,
                                   const RawImageMetadata& metadata,
                                   const RawCameraMetadata& cameraMetadata,
                                   const PostProcessSettings& settings);
            
        static std::shared_ptr<HdrMetadata> prepareHdr(const RawCameraMetadata& cameraMetadata,
                                                       const PostProcessSettings& settings,
                                                       const RawImageBuffer& reference,
                                                       const RawImageBuffer& underexposed);

    #ifdef DNG_SUPPORT
        static cv::Mat buildRawImage(std::vector<cv::Mat> channels, int cropX, int cropY);
        
        static void writeDng(cv::Mat& rawImage,
                      const RawCameraMetadata& cameraMetadata,
                      const RawImageMetadata& imageMetadata,
                      const std::string& outputPath);
    #endif
    };
}

#endif /* ImageProcessor_hpp */
