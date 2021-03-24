#ifndef RawImageMetadata_hpp
#define RawImageMetadata_hpp

#include "motioncam/Color.h"
#include "motioncam/Settings.h"

#include <string>
#include <map>
#include <set>
#include <vector>

#include <opencv2/opencv.hpp>
#include <json11/json11.hpp>

namespace motioncam {
    class ImageProcessorProgress;

    namespace util {
        class ZipReader;
    }

    enum class ColorFilterArrangment : int {
        RGGB = 0,
        GRBG,
        GBRG,
        BGGR,
        RGB,
        MONO
    };
    
    // This needs to match the generator input
    enum class PixelFormat : int {
        RAW10 = 0,
        RAW16 = 1,
        RAW12,
        YUV_420_888
    };

    enum class ScreenOrientation : int {
        PORTRAIT = 0,
        REVERSE_PORTRAIT,
        LANDSCAPE,
        REVERSE_LANDSCAPE
    };

    enum class RawType : int {
        ZSL,
        HDR
    };

    struct RawImageMetadata
    {
        RawImageMetadata() :
            exposureTime(0),
            iso(0),
            timestampNs(0),
            exposureCompensation(0),
            screenOrientation(ScreenOrientation::PORTRAIT),
            rawType(RawType::ZSL)
        {
        }

        RawImageMetadata(const RawImageMetadata& other) :
            asShot(other.asShot),
            lensShadingMap(other.lensShadingMap),
            exposureTime(other.exposureTime),
            iso(other.iso),
            exposureCompensation(other.exposureCompensation),
            timestampNs(other.timestampNs),
            screenOrientation(other.screenOrientation),
            rawType(other.rawType)
        {
        }

        RawImageMetadata(const RawImageMetadata&& other) noexcept :
            asShot(std::move(other.asShot)),
            lensShadingMap(std::move(other.lensShadingMap)),
            exposureTime(other.exposureTime),
            iso(other.iso),
            exposureCompensation(other.exposureCompensation),
            timestampNs(other.timestampNs),
            screenOrientation(other.screenOrientation),
            rawType(other.rawType)
        {
        }

        RawImageMetadata& operator=(const RawImageMetadata &obj) {
            asShot = obj.asShot;
            lensShadingMap = obj.lensShadingMap;
            exposureTime = obj.exposureTime;
            iso = obj.iso;
            exposureCompensation = obj.exposureCompensation;
            timestampNs = obj.timestampNs;
            screenOrientation = obj.screenOrientation;
            rawType = obj.rawType;

            return *this;
        }

        cv::Vec3f asShot;
        std::vector<cv::Mat> lensShadingMap;
        int64_t exposureTime;
        int32_t iso;
        int32_t exposureCompensation;
        int64_t timestampNs;
        ScreenOrientation screenOrientation;
        RawType rawType;
    };

    class NativeBuffer {
    public:
        NativeBuffer() {}        
        virtual ~NativeBuffer() {}

        virtual uint8_t* lock(bool write) = 0;
        virtual void unlock() = 0;
        virtual uint64_t nativeHandle() = 0;
        virtual size_t len() = 0;
        virtual const std::vector<uint8_t>& hostData() = 0;
        virtual void copyHostData(const std::vector<uint8_t>& data) = 0;
        virtual void release() = 0;
    };

    class NativeHostBuffer : public NativeBuffer {
    public:
        NativeHostBuffer()
        {
        }

        NativeHostBuffer(size_t length) : data(length)
        {
        }

        uint8_t* lock(bool write) {
            return data.data();
        }
        
        void unlock() {
        }
        
        uint64_t nativeHandle() {
            return 0;
        }
        
        size_t len() {
            return data.size();
        }
        
        void allocate(size_t len) {
            data.resize(len);
        }
        
        const std::vector<uint8_t>& hostData()
        {
            return data;
        }
        
        void copyHostData(const std::vector<uint8_t>& other)
        {
            data = std::move(other);
        }
        
        void release()
        {
            data.resize(0);
            data.shrink_to_fit();
        }
        
    private:
        std::vector<uint8_t> data;
    };

    struct RawImageBuffer {
        
        RawImageBuffer(std::unique_ptr<NativeBuffer> buffer) :
            data(std::move(buffer)),
            pixelFormat(PixelFormat::RAW10),
            width(0),
            height(0),
            rowStride(0)
        {
        }
        
        RawImageBuffer() :
            data(new NativeHostBuffer()),
            pixelFormat(PixelFormat::RAW10),
            width(0),
            height(0),
            rowStride(0)
        {
        }

//        RawImageBuffer(const RawImageBuffer&& other) noexcept :
//                data(std::move(other.data)),
//                metadata(std::move(other.metadata)),
//                pixelFormat(other.pixelFormat),
//                width(other.width),
//                height(other.height),
//                rowStride(other.rowStride)
//        {
//        }

        std::unique_ptr<NativeBuffer> data;
        RawImageMetadata metadata;
        PixelFormat pixelFormat;
        int32_t width;
        int32_t height;
        int32_t rowStride;
    };

    struct RawCameraMetadata {
        RawCameraMetadata() :
            sensorArrangment(ColorFilterArrangment::RGGB),
            colorIlluminant1(color::StandardA),
            colorIlluminant2(color::D50),
            whiteLevel(0)
        {
        }
        
        ColorFilterArrangment sensorArrangment;

        cv::Mat colorMatrix1;
        cv::Mat colorMatrix2;
        
        cv::Mat calibrationMatrix1;
        cv::Mat calibrationMatrix2;

        cv::Mat forwardMatrix1;
        cv::Mat forwardMatrix2;

        color::Illuminant colorIlluminant1;
        color::Illuminant colorIlluminant2;
      
        int whiteLevel;
        std::vector<int> blackLevel;

        std::vector<float> apertures;
        std::vector<float> focalLengths;
    };
}
#endif /* RawImageMetadata_hpp */
