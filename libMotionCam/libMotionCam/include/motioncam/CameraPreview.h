#ifndef CameraPreview_hpp
#define CameraPreview_hpp

#include <HalideBuffer.h>

namespace motioncam {
    class RawCameraMetadata;
    class RawImageBuffer;

    class CameraPreview {
    public:
        static void generate(const RawImageBuffer& rawBuffer,
                             const RawCameraMetadata& cameraMetadata,
                             const int downscaleFactor,
                             const bool flipped,
                             const float shadows,
                             const float contrast,
                             const float saturation,
                             const float blacks,
                             const float whitePoint,
                             const float temperatureOffset,
                             const float tintOffset,
                             const float tonemapVariance,
                             Halide::Runtime::Buffer<uint8_t>& inputBuffer,
                             Halide::Runtime::Buffer<uint8_t>& outputBuffer);

    };
}

#endif /* CameraPreview_hpp */
