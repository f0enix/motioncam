#include "motioncam/CameraPreview.h"
#include "motioncam/RawImageMetadata.h"
#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include "motioncam/ImageProcessor.h"

#include "camera_preview2_raw10.h"
#include "camera_preview3_raw10.h"
#include "camera_preview4_raw10.h"
#include "camera_preview2_raw16.h"
#include "camera_preview3_raw16.h"
#include "camera_preview4_raw16.h"

namespace motioncam {
    void CameraPreview::generate(const RawImageBuffer& rawBuffer,
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
                                 Halide::Runtime::Buffer<uint8_t>& outputBuffer)
    {
        ///Measure measure("cameraPreview()");
        
        int width = rawBuffer.width / 2 / downscaleFactor;
        int height = rawBuffer.height / 2 / downscaleFactor;

        // Setup buffers
        Halide::Runtime::Buffer<float> shadingMapBuffer[4];

        for(int i = 0; i < 4; i++) {
            shadingMapBuffer[i] = Halide::Runtime::Buffer<float>(
                (float*) rawBuffer.metadata.lensShadingMap[i].data,
                rawBuffer.metadata.lensShadingMap[i].cols,
                rawBuffer.metadata.lensShadingMap[i].rows);
            
            shadingMapBuffer[i].set_host_dirty();
        }
        
        cv::Mat cameraToSrgb;
        cv::Vec3f cameraWhite;
        
        // Use user tint/temperature offsets
        CameraProfile cameraProfile(cameraMetadata);
        Temperature temperature;
        
        cameraProfile.temperatureFromVector(rawBuffer.metadata.asShot, temperature);
        
        Temperature userTemperature(temperature.temperature() + temperatureOffset, temperature.tint() + tintOffset);
        
        ImageProcessor::createSrgbMatrix(cameraMetadata, userTemperature, cameraWhite, cameraToSrgb);
        
        Halide::Runtime::Buffer<float> cameraToSrgbBuffer = Halide::Runtime::Buffer<float>(
            (float*) cameraToSrgb.data, cameraToSrgb.cols, cameraToSrgb.rows);

        cameraToSrgbBuffer.set_host_dirty();

        auto camera_preview = &camera_preview4_raw10;
        
        if(rawBuffer.pixelFormat == PixelFormat::RAW10) {
            if(downscaleFactor == 2)
                camera_preview = &camera_preview2_raw10;
            else if(downscaleFactor == 3)
                camera_preview = &camera_preview3_raw10;
            else if(downscaleFactor == 4)
                camera_preview = &camera_preview4_raw10;
            else
                return;
        }
        else if(rawBuffer.pixelFormat == PixelFormat::RAW16) {
            if(downscaleFactor == 2)
                camera_preview = &camera_preview2_raw16;
            else if(downscaleFactor == 3)
                camera_preview = &camera_preview3_raw16;
            else if(downscaleFactor == 4)
                camera_preview = &camera_preview4_raw16;
            else
                return;
        }
        else
            return;
                
        camera_preview(inputBuffer,
                       rawBuffer.rowStride,
                       rawBuffer.metadata.asShot[0],
                       rawBuffer.metadata.asShot[1],
                       rawBuffer.metadata.asShot[2],
                       cameraToSrgbBuffer,
                       flipped,
                       width,
                       height,
                       cameraMetadata.blackLevel[0],
                       cameraMetadata.blackLevel[1],
                       cameraMetadata.blackLevel[2],
                       cameraMetadata.blackLevel[3],
                       cameraMetadata.whiteLevel,
                       shadingMapBuffer[0],
                       shadingMapBuffer[1],
                       shadingMapBuffer[2],
                       shadingMapBuffer[3],
                       static_cast<int>(cameraMetadata.sensorArrangment),
                       tonemapVariance,
                       2.2f,
                       shadows,
                       blacks,
                       whitePoint,
                       contrast,
                       saturation,
                       outputBuffer);
        
        outputBuffer.device_sync();
    }
}
