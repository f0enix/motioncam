#ifndef CameraProfile_hpp
#define CameraProfile_hpp

#include "motioncam/Types.h"

#include <opencv2/opencv.hpp>

namespace json11 {
    class Json;
}

namespace motioncam {
    struct RawCameraMetadata;
    struct RawImageMetadata;
    class Temperature;
    
    //
    // Most of this code is adapted from the Adobe DNG SDK
    //
    
    class CameraProfile {
    public:
        CameraProfile(const RawCameraMetadata& metadata, const RawImageMetadata& imageMetadata);
        
        void temperatureFromVector(const cv::Vec3f& asShotVector, Temperature& outTemperature);
        void cameraToPcs(const Temperature& temperature, cv::Mat& outPcsToCamera, cv::Mat& outCameraToPcs, cv::Vec3f& outCameraWhite) const;
        
        static void pcsToSrgb(cv::Mat& outPcsToSrgb, cv::Mat& outSrgbToPcs);

        __unused void cameraToSrgb(cv::Mat& cameraToSrgb) const;
        
    private:
        static cv::Mat normalizeForwardMatrix(const cv::Mat& forwardMatrix);
        static cv::Mat normalizeColorMatrix(const cv::Mat& colorMatrix);
        
        XYCoord cameraNeutralToXy(const cv::Vec3f& cameraNeutralVector) const;
        cv::Mat findXyzToCamera(const XYCoord& white, cv::Mat* outForwardMatrix, cv::Mat* outCameraCalibration) const;
        
        static cv::Mat mapWhiteMatrix(const XYCoord& white1, const XYCoord& white2) ;
        
    private:
        cv::Mat mColorMatrix1;
        cv::Mat mColorMatrix2;
        
        cv::Mat mCameraCalibration1;
        cv::Mat mCameraCalibration2;
        
        float mColorTemperature1;
        float mColorTemperature2;
        
        cv::Mat mForwardMatrix1;
        cv::Mat mForwardMatrix2;
    };
}

#endif /* CameraProfile_hpp */
