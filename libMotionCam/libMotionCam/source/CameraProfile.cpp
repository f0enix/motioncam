#include "motioncam/CameraProfile.h"
#include "motioncam/Math.h"
#include "motioncam/Exceptions.h"
#include "motioncam/Temperature.h"
#include "motioncam/Color.h"
#include "motioncam/RawImageMetadata.h"

#include <json11/json11.hpp>
#include <vector>

using json11::Json;
using std::vector;

namespace motioncam {
    
    CameraProfile::CameraProfile(const RawCameraMetadata& cameraMetadata, const RawImageMetadata& imageMetadata) {
        cv::Mat calibration1 =
            imageMetadata.calibrationMatrix1.empty() ? cameraMetadata.calibrationMatrix1 : imageMetadata.calibrationMatrix1;

        cv::Mat calibration2 =
            imageMetadata.calibrationMatrix2.empty() ? cameraMetadata.calibrationMatrix2 : imageMetadata.calibrationMatrix2;

        //

        cv::Mat colorMatrix1 =
            calibration1 * normalizeColorMatrix(imageMetadata.colorMatrix1.empty() ? cameraMetadata.colorMatrix1 : imageMetadata.colorMatrix1);

        cv::Mat colorMatrix2 =
            calibration2 * normalizeColorMatrix(imageMetadata.colorMatrix2.empty() ? cameraMetadata.colorMatrix2 : imageMetadata.colorMatrix2);

        //

        cv::Mat forwardMatrix1 =
            normalizeForwardMatrix(imageMetadata.forwardMatrix1.empty() ? cameraMetadata.forwardMatrix1 : imageMetadata.forwardMatrix1);

        cv::Mat forwardMatrix2 =
            normalizeForwardMatrix(imageMetadata.forwardMatrix2.empty() ? cameraMetadata.forwardMatrix2 : imageMetadata.forwardMatrix2);

        //

        float colorTemperature1 = color::IlluminantToTemperature(cameraMetadata.colorIlluminant1);
        float colorTemperature2 = color::IlluminantToTemperature(cameraMetadata.colorIlluminant2);

        if(colorTemperature1 > colorTemperature2) {
            mColorMatrix1 = colorMatrix2;
            mColorMatrix2 = colorMatrix1;

            mForwardMatrix1 = forwardMatrix2;
            mForwardMatrix2 = forwardMatrix1;

            mCameraCalibration1 = calibration2;
            mCameraCalibration2 = calibration1;

            mColorTemperature1 = colorTemperature2;
            mColorTemperature2 = colorTemperature1;
        }
        else {
            mColorMatrix1 = colorMatrix1;
            mColorMatrix2 = colorMatrix2;

            mForwardMatrix1 = forwardMatrix1;
            mForwardMatrix2 = forwardMatrix2;

            mCameraCalibration1 = calibration1;
            mCameraCalibration2 = calibration2;

            mColorTemperature1 = colorTemperature1;
            mColorTemperature2 = colorTemperature2;
        }
    }

    cv::Mat CameraProfile::normalizeForwardMatrix(const cv::Mat& forwardMatrix) {
        
        cv::Mat cameraOne = cv::Mat::ones(1, forwardMatrix.cols, forwardMatrix.type());
        
        XYZCoord xyz(cameraOne.dot(forwardMatrix.rowRange(0, 1)),
                     cameraOne.dot(forwardMatrix.rowRange(1, 2)),
                     cameraOne.dot(forwardMatrix.rowRange(2, 3)));

        cv::Mat invXyz = cv::Mat::diag(cv::Mat(xyz)).inv();
        
        return cv::Mat::diag(cv::Mat(color::PCSToXYZ())) * invXyz * forwardMatrix;
    }
    
    cv::Mat CameraProfile::normalizeColorMatrix(const cv::Mat& colorMatrix) {
        
        cv::Mat coord = colorMatrix * cv::Mat(color::PCSToXYZ());
        
        auto maxCoordIt = std::max_element(coord.begin<float>(), coord.end<float>());
        float maxCoord = *maxCoordIt;

        if(maxCoord > 0.0 && (maxCoord < 0.99 || maxCoord > 1.01))
        {
            return colorMatrix * (1.0f / maxCoord);
        }
        
        return colorMatrix;
    }
    
    cv::Mat CameraProfile::findXyzToCamera(const XYCoord& white, cv::Mat* outForwardMatrix, cv::Mat* outCameraCalibration) const {
        
        // Convert to temperature/offset space.
        Temperature t(white);

        // Find fraction to weight the first calibration.
        double g;

        if (t.temperature() <= mColorTemperature1) {
            g = 1.0;
        }
        else if (t.temperature() >= mColorTemperature2) {
            g = 0.0;
        }
        else {
            double invT = 1.0 / t.temperature();
            g = ( invT - (1.0 / mColorTemperature2) ) / ( (1.0 / mColorTemperature1) - (1.0 / mColorTemperature2) );
        }
        
        // Color matrices
        cv::Mat colorMatrix;
        
        if(g >= 1.0) {
            colorMatrix = mColorMatrix1;
        }
        else if (g <= 0.0) {
            colorMatrix = mColorMatrix2;
        }
        else {
            colorMatrix = g * mColorMatrix1 + (1.0 - g) * mColorMatrix2;
        }

        // Forward matrices
        if(outForwardMatrix) {
            bool has1 = !mForwardMatrix1.empty();
            bool has2 = !mForwardMatrix2.empty();
            
            if (has1 && has2)
            {
                if (g >= 1.0) {
                    *outForwardMatrix = mForwardMatrix1;
                }
                else if (g <= 0.0) {
                    *outForwardMatrix = mForwardMatrix2;
                }
                else {
                    *outForwardMatrix = g * mForwardMatrix1 + (1.0 - g) * mForwardMatrix2;
                }
            }
            else if(has1)
            {
                *outForwardMatrix = mForwardMatrix1;
            }
            else if (has2)
            {
                *outForwardMatrix = mForwardMatrix2;
            }
            else
            {
                outForwardMatrix->release();
            }
        }

        // Interpolate camera calibration matrix.
        if(outCameraCalibration) {
            if (g >= 1.0) {
                *outCameraCalibration = mCameraCalibration1;
            }
            else if (g <= 0.0) {
                *outCameraCalibration = mCameraCalibration2;
            }
            else {
                *outCameraCalibration = g * mCameraCalibration1 + (1.0 - g) * mCameraCalibration2;
            }
        }

        return colorMatrix;
    }
    
    XYCoord CameraProfile::cameraNeutralToXy(const cv::Vec3f& cameraNeutralVector) const {
        
        static const uint32_t maxIters = 30;
        
        XYCoord last = color::D50XYCoord;
        
        for(int i = 0; i < maxIters; i++) {
            
            cv::Mat xyzToCamera = findXyzToCamera(last, nullptr, nullptr);
            
            cv::Mat r = xyzToCamera.inv() * cv::Mat(cameraNeutralVector);
            XYCoord next = color::XYZToXY(cv::Vec3f(r));
            
            if(abs (next[0] - last[0]) +
               abs (next[1] - last[1]) < 0.0000001) {
                return next;
            }

            // If we reach the limit without converging, we are most likely
            // in a two value oscillation.  So take the average of the last
            // two estimates and give up.

            if (i == maxIters - 1) {
                next[0] = (last[0] + next[0]) * 0.5;
                next[1] = (last[1] + next[1]) * 0.5;
            }

            last = next;
        }

        return last;
    }
    
    cv::Mat CameraProfile::mapWhiteMatrix(const XYCoord& white1, const XYCoord& white2) {
        
        // Use the linearized Bradford adaptation matrix.
        static const float mb[] = { 0.8951,  0.2664, -0.1614,
                                   -0.7502,  1.7135,  0.0367,
                                    0.0389, -0.0685,  1.0296 };
        
        cv::Mat bradfordAdaptationMatrix(3, 3, CV_32F, (void*) &mb);

        cv::Mat w1Mat = bradfordAdaptationMatrix * cv::Mat(color::XYToXYZ(white1));
        cv::Mat w2Mat = bradfordAdaptationMatrix * cv::Mat(color::XYToXYZ(white2));
        
        cv::Vec3f w1 = w1Mat;
        cv::Vec3f w2 = w2Mat;
        
        // Negative white coordinates are kind of meaningless.
        w1[0] = std::max(w1[0], 0.0f);
        w1[1] = std::max(w1[1], 0.0f);
        w1[2] = std::max(w1[2], 0.0f);

        w2[0] = std::max(w2[0], 0.0f);
        w2[1] = std::max(w2[1], 0.0f);
        w2[2] = std::max(w2[2], 0.0f);

        // Limit scaling to something reasonable.
        cv::Mat A(3, 3, CV_32F, cv::Scalar(0));
        
        auto* dataA = A.ptr<float>();

        dataA[0] = math::clamp(0.1, w1[0] > 0.0 ? w2[0] / w1[0] : 10.0, 10.0);
        dataA[4] = math::clamp(0.1, w1[1] > 0.0 ? w2[1] / w1[1] : 10.0, 10.0);
        dataA[8] = math::clamp(0.1, w1[2] > 0.0 ? w2[2] / w1[2] : 10.0, 10.0);

        return bradfordAdaptationMatrix.inv() * A * bradfordAdaptationMatrix;
    }

    __unused void CameraProfile::cameraToSrgb(cv::Mat& cameraToSrgb) const {
        static const float srgbToXyz[] = { 0.4124564f, 0.3575761f, 0.1804375f,
                                           0.2126729f, 0.7151522f, 0.0721750f,
                                           0.0193339f, 0.1191920f, 0.9503041f };
        
        
        cv::Mat M(3, 3, CV_32F, (void*) &srgbToXyz);
        
        cameraToSrgb = M * mColorMatrix1;
        cameraToSrgb = cameraToSrgb.inv();
    }
    
    void CameraProfile::pcsToSrgb(cv::Mat& outPcsToSrgb, cv::Mat& outSrgbToPcs) {

        static const float srgbToPcs[] = { 0.4361, 0.3851, 0.1431,
                                           0.2225, 0.7169, 0.0606,
                                           0.0139, 0.0971, 0.7141 };
        
        // The matrix values are often rounded, so adjust to
        // get them to convert device white exactly to the PCS.

        cv::Mat M(3, 3, CV_32F, (void*) &srgbToPcs);
        
        cv::Vec3f w1 = cv::Vec3f(cv::Mat(M * cv::Mat::ones(3, 1, CV_32F)));
        cv::Vec3f w2 = color::PCSToXYZ();
        
        cv::Mat s(3, 3, CV_32F, cv::Scalar(0.0f));
        
        auto* sData = s.ptr<float>();
        
        sData[0] = w2[0] / w1[0];
        sData[4] = w2[1] / w1[1];
        sData[8] = w2[2] / w1[2];
        
        outSrgbToPcs = s * M;
        outPcsToSrgb = outSrgbToPcs.inv();
    }
    
    void CameraProfile::temperatureFromVector(const cv::Vec3f& asShotVector, Temperature& outTemperature) {
        cv::Vec3f asShot = asShotVector;

        float max = math::max(asShot);
        if(max > 0) {
            asShot[0] = asShot[0] * (1.0f / max);
            asShot[1] = asShot[1] * (1.0f / max);
            asShot[2] = asShot[2] * (1.0f / max);
        }
        else {
            throw InvalidState("Camera white balance vector is zero");
        }

        // Get our matrices for the as shot white balance
        XYCoord neutralXy = cameraNeutralToXy(asShot);

        outTemperature = Temperature(neutralXy);
    }
    
    void CameraProfile::cameraToPcs(const Temperature& temperature,
                                    cv::Mat& outPcsToCamera,
                                    cv::Mat& outCameraToPcs,
                                    cv::Vec3f& outCameraWhite) const
    {
        XYCoord neutralXy = temperature.getXyCoord();
        
        cv::Mat colorMatrix, forwardMatrix, cameraCalibrationMatrix;
        
        // Find camera white
        colorMatrix = findXyzToCamera(neutralXy, &forwardMatrix, &cameraCalibrationMatrix);
        
        cv::Mat cameraWhiteMat = colorMatrix * cv::Mat(color::XYToXYZ(neutralXy));
        XYZCoord cameraWhite = cameraWhiteMat;
        
        double whiteScale = 1.0 / math::max(cameraWhite);
        
        cameraWhite[0] = math::clamp<float>(0.001f, cameraWhite[0] * whiteScale, 1.0f);
        cameraWhite[1] = math::clamp<float>(0.001f, cameraWhite[1] * whiteScale, 1.0f);
        cameraWhite[2] = math::clamp<float>(0.001f, cameraWhite[2] * whiteScale, 1.0f);
        
        // Find PCS to Camera transform. Scale matrix so PCS white can just be
        // reached when the first camera channel saturates
        cv::Mat pcsToCameraMatrix = colorMatrix * mapWhiteMatrix(color::PCSToXY(), neutralXy);

        cv::Mat tmp = pcsToCameraMatrix * cv::Mat(color::PCSToXYZ());
        float scale = math::max(cv::Vec3f(tmp));

        outPcsToCamera = (1.0f / scale) * pcsToCameraMatrix;
        
        if(!forwardMatrix.empty()) {
            cv::Mat individualToReference = cameraCalibrationMatrix.inv();
            cv::Mat refCameraWhite = individualToReference * cv::Mat(cameraWhite);

            outCameraToPcs = forwardMatrix * cv::Mat::diag(refCameraWhite).inv() * individualToReference;
        }
        else {
            outCameraToPcs = pcsToCameraMatrix.inv();
        }
        
        outCameraWhite = cameraWhite;
    }
}
