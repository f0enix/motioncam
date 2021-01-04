#include <camera/NdkCameraMetadata.h>
#include <opencv2/opencv.hpp>
#include <utility>

#include <motioncam/Color.h>
#include <motioncam/Logger.h>
#include <motioncam/RawImageMetadata.h>

#include "CaptureSessionManager.h"
#include "CameraSession.h"
#include "CameraSessionListener.h"
#include "CameraDescription.h"
#include "DisplayDimension.h"
#include "RawImageConsumer.h"
#include "Exceptions.h"
#include "Logger.h"

namespace motioncam {
    const int MAX_PREVIEW_PIXELS = 1280*720;

    namespace {
        color::Illuminant getIlluminant(acamera_metadata_enum_android_sensor_reference_illuminant1_t illuminant) {
            switch(illuminant) {
                case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_STANDARD_A:
                    return color::StandardA;

                case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_STANDARD_B:
                    return color::StandardB;

                case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_STANDARD_C:
                    return color::StandardC;

                case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_D55:
                    return color::D55;

                case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_D65:
                    return color::D65;

                case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_D75:
                    return color::D75;

                case ACAMERA_SENSOR_REFERENCE_ILLUMINANT1_D50:
                    return color::D50;

                default:
                    return color::D65;
            }
        }

        cv::Mat getColorMatrix(ACameraMetadata_const_entry& entry) {
            cv::Mat m(3, 3, CV_32F);

            for(int y = 0; y < 3; y++) {
                for(int x = 0; x < 3; x++) {
                    int i = y * 3 + x;

                    m.at<float>(y, x) = (float) entry.data.r[i].numerator / (float) entry.data.r[i].denominator;
                }
            }

            return m;
        }

        ColorFilterArrangment getSensorArrangement(acamera_metadata_enum_android_sensor_info_color_filter_arrangement_t arrangement) {
            switch(arrangement) {
                case ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGGB:
                    return ColorFilterArrangment::RGGB;

                case ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_GRBG:
                    return ColorFilterArrangment::GRBG;

                case ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_GBRG:
                    return ColorFilterArrangment::GBRG;

                case ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_BGGR:
                    return ColorFilterArrangment::BGGR;

                case ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGB:
                    return ColorFilterArrangment::RGB;

                case ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_MONO:
                    return ColorFilterArrangment::MONO;

                default:
                    return ColorFilterArrangment::RGGB;
            }
        }
    }

    CaptureSessionManager::CaptureSessionManager(size_t maxMemoryUsageBytes) :
        mCameraManager(std::shared_ptr<ACameraManager>(ACameraManager_create(), ACameraManager_delete)),
        mMaxMemoryUsageBytes(maxMemoryUsageBytes)
    {
        // Get list of cameras on the device
        enumerateCameras();

        // Set up supported cameras
        for (const auto& cameraDescription : mCameras) {
            if (isCameraSupported(*cameraDescription)) {
                mSupportedCameras[cameraDescription->id] = cameraDescription;
                LOGI("Camera %s is supported", cameraDescription->id.c_str());
            }
        }

        LOGI("Found %d supported cameras", (int) mSupportedCameras.size());
    }

    bool CaptureSessionManager::isCameraSupported(const CameraDescription& cameraDescription) {
        bool supportsRaw = false;

        // The camera needs to support RAW capture
        std::string caps;

        for (uint8_t cap : cameraDescription.supportedCaps) {
            if (cap == ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_RAW) {
                supportsRaw = true;
            }

            caps += std::to_string(cap);
            caps += ",";
        }

        LOGD("Camera %s hardwareLevel=%d facing=%d supportedCaps=%s",
                cameraDescription.id.c_str(), static_cast<int>(cameraDescription.hardwareLevel), static_cast<int>(cameraDescription.lensFacing), caps.c_str());

        // Check for RAW outputs
        OutputConfiguration outputConfig;
        bool hasRawOutput = getRawConfiguration(cameraDescription, outputConfig);

        return ( cameraDescription.hardwareLevel == ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED ||
                 cameraDescription.hardwareLevel == ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_FULL ||
                 cameraDescription.hardwareLevel == ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_3 ) &&
                 supportsRaw && hasRawOutput;
    }

    void CaptureSessionManager::updateCameraMetadata(const std::shared_ptr<ACameraMetadata>& cameraChars, CameraDescription& cameraDescription) {
        ACameraMetadata_const_entry entry;

        // ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL, &entry) == ACAMERA_OK) {
            cameraDescription.hardwareLevel = static_cast<acamera_metadata_enum_android_info_supported_hardware_level_t> (entry.data.u8[0]);
        }

        // ACAMERA_REQUEST_AVAILABLE_CAPABILITIES
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_REQUEST_AVAILABLE_CAPABILITIES, &entry) == ACAMERA_OK) {
            for (int cnt = 0; cnt < entry.count; cnt++) {
                cameraDescription.supportedCaps.push_back(
                    static_cast<acamera_metadata_enum_android_request_available_capabilities_t>(entry.data.u8[cnt]));
            }
        }

        // ACAMERA_LENS_FACING
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_LENS_FACING, &entry) == ACAMERA_OK) {
            cameraDescription.lensFacing = static_cast<acamera_metadata_enum_android_lens_facing_t> (entry.data.u8[0]);
        }

        // ACAMERA_CONTROL_AE_COMPENSATION_RANGE
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_CONTROL_AE_COMPENSATION_RANGE, &entry) == ACAMERA_OK) {
            cameraDescription.exposureCompensationRange[0] = entry.data.i32[0];
            cameraDescription.exposureCompensationRange[1] = entry.data.i32[1];
        }

        // ACAMERA_CONTROL_AE_COMPENSATION_STEP
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_CONTROL_AE_COMPENSATION_STEP, &entry) == ACAMERA_OK) {
            cameraDescription.exposureCompensationStepFraction[0] = entry.data.r[0].numerator;
            cameraDescription.exposureCompensationStepFraction[1] = entry.data.r[0].denominator;
        }

        // ACAMERA_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, &entry) == ACAMERA_OK) {
            for(int cnt = 0; cnt < entry.count; cnt++) {
                auto mode = static_cast<acamera_metadata_enum_android_lens_optical_stabilization_mode_t>(entry.data.u8[0]);

                cameraDescription.oisModes.push_back(mode);
            }
        }

        // ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) == ACAMERA_OK) {
            for (int cnt = 0; cnt < entry.count; cnt += 4) {
                int32_t format  = entry.data.i32[cnt + 0];
                int32_t width   = entry.data.i32[cnt + 1];
                int32_t height  = entry.data.i32[cnt + 2];
                int32_t isInput = entry.data.i32[cnt + 3];

                // Ignore inputs
                if (isInput) {
                    continue;
                }

                // Add to our structure for easier access
                cameraDescription.outputConfigs[format].push_back({ format, DisplayDimension(width, height) });

                LOGD("Found output configuration for camera %s format: %d width: %d height: %d", cameraDescription.id.c_str(), format, width, height);
            }
        }

        // ACAMERA_SCALER_AVAILABLE_STALL_DURATIONS
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SCALER_AVAILABLE_STALL_DURATIONS, &entry) == ACAMERA_OK) {
            for (int cnt = 0; cnt < entry.count; cnt += 4) {
                int64_t format  = entry.data.i64[cnt + 0];
                int64_t width   = entry.data.i64[cnt + 1];
                int64_t height  = entry.data.i64[cnt + 2];
                int64_t stall   = entry.data.i64[cnt + 3];

                LOGD("Found stall duration for camera %s format: %ld width: %ld height: %ld duration: %ld", cameraDescription.id.c_str(), format, width, height, stall);
            }
        }

        // ACAMERA_SENSOR_BLACK_LEVEL_PATTERN
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_BLACK_LEVEL_PATTERN, &entry) == ACAMERA_OK) {
            for (int i = 0; i < 4; i++) {
                cameraDescription.metadata.blackLevel.push_back(entry.data.i32[i]);
            }
        }

        // ACAMERA_SENSOR_INFO_WHITE_LEVEL
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_INFO_WHITE_LEVEL, &entry) == ACAMERA_OK) {
            cameraDescription.metadata.whiteLevel = entry.data.i32[0];
        }

        // ACAMERA_SENSOR_CALIBRATION_TRANSFORM1
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_CALIBRATION_TRANSFORM1, &entry) == ACAMERA_OK) {
            cameraDescription.metadata.calibrationMatrix1 = getColorMatrix(entry);
        }
        else {
            cv::Mat mat(3, 3, CV_32F);
            cv::setIdentity(mat);

            cameraDescription.metadata.calibrationMatrix1 = mat;
        }

        // ACAMERA_SENSOR_CALIBRATION_TRANSFORM2
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_CALIBRATION_TRANSFORM2, &entry) == ACAMERA_OK) {
            cameraDescription.metadata.calibrationMatrix2 = getColorMatrix(entry);
        }
        else {
            cv::Mat mat(3, 3, CV_32F);
            cv::setIdentity(mat);

            cameraDescription.metadata.calibrationMatrix2 = mat;
        }

        // ACAMERA_SENSOR_FORWARD_MATRIX1
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_FORWARD_MATRIX1, &entry) == ACAMERA_OK) {
            cameraDescription.metadata.forwardMatrix1 = getColorMatrix(entry);
        }
        else {
            cv::Mat mat(3, 3, CV_32F);
            cv::setIdentity(mat);

            cameraDescription.metadata.forwardMatrix1 = mat;
        }

        // ACAMERA_SENSOR_FORWARD_MATRIX2
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_FORWARD_MATRIX2, &entry) == ACAMERA_OK) {
            cameraDescription.metadata.forwardMatrix2 = getColorMatrix(entry);
        }
        else {
            cv::Mat mat(3, 3, CV_32F);
            cv::setIdentity(mat);

            cameraDescription.metadata.forwardMatrix2 = mat;
        }

        // ACAMERA_SENSOR_COLOR_TRANSFORM1
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_COLOR_TRANSFORM1, &entry) == ACAMERA_OK) {
            cameraDescription.metadata.colorMatrix1 = getColorMatrix(entry);
        }

        // ACAMERA_SENSOR_COLOR_TRANSFORM2
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_COLOR_TRANSFORM2, &entry) == ACAMERA_OK) {
            cameraDescription.metadata.colorMatrix2 = getColorMatrix(entry);
        }

        // ACAMERA_SENSOR_REFERENCE_ILLUMINANT1
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_REFERENCE_ILLUMINANT1, &entry) == ACAMERA_OK) {
            auto illuminant1 = static_cast<acamera_metadata_enum_android_sensor_reference_illuminant1_t>(entry.data.u8[0]);
            cameraDescription.metadata.colorIlluminant1 = getIlluminant(illuminant1);
        }

        // ACAMERA_SENSOR_REFERENCE_ILLUMINANT2
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_REFERENCE_ILLUMINANT2, &entry) == ACAMERA_OK) {
            auto illuminant2 = static_cast<acamera_metadata_enum_android_sensor_reference_illuminant1_t>(entry.data.u8[0]);
            cameraDescription.metadata.colorIlluminant2 = getIlluminant(illuminant2);
        }

        // ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, &entry) == ACAMERA_OK) {
            auto value = static_cast<acamera_metadata_enum_android_sensor_info_color_filter_arrangement_t>(entry.data.u8[0]);
            cameraDescription.metadata.sensorArrangment = getSensorArrangement(value);
        }

        // ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE, &entry) == ACAMERA_OK) {
            cameraDescription.isoRange[0] = entry.data.i32[0];
            cameraDescription.isoRange[1] = entry.data.i32[1];
        }

        // ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE, &entry) == ACAMERA_OK) {
            cameraDescription.exposureRange[0] = entry.data.i64[0];
            cameraDescription.exposureRange[1] = entry.data.i64[1];
        }

        // ACAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, &entry) == ACAMERA_OK) {
            for (int n = 0; n < entry.count; n++) {
                cameraDescription.metadata.focalLengths.push_back(entry.data.f[n]);
            }
        }

        // ACAMERA_LENS_INFO_AVAILABLE_APERTURES
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_LENS_INFO_AVAILABLE_APERTURES, &entry) == ACAMERA_OK) {
            for (int n = 0; n < entry.count; n++) {
                cameraDescription.metadata.apertures.push_back(entry.data.f[n]);
            }
        }

        // ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &entry) == ACAMERA_OK) {
            for (int n = 0; n < 4; n++) {
                cameraDescription.sensorSize[n] = entry.data.i32[n];
            }
        }

        // ACAMERA_CONTROL_MAX_REGIONS
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_CONTROL_MAX_REGIONS, &entry) == ACAMERA_OK) {
            cameraDescription.maxAeRegions = entry.data.i32[0];
            cameraDescription.maxAwbRegions = entry.data.i32[1];
            cameraDescription.maxAfRegions = entry.data.i32[2];
        }

        // ACAMERA_SENSOR_ORIENTATION
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_SENSOR_ORIENTATION, &entry) == ACAMERA_OK) {
            cameraDescription.sensorOrientation = entry.data.i32[0];
        }

        // ACAMERA_TONEMAP_AVAILABLE_TONE_MAP_MODES
        if(ACameraMetadata_getConstEntry(cameraChars.get(), ACAMERA_TONEMAP_AVAILABLE_TONE_MAP_MODES, &entry) == ACAMERA_OK) {
            for (int n = 0; n < entry.count; n++) {
                cameraDescription.tonemapModes.push_back(static_cast<acamera_metadata_enum_android_tonemap_mode_t>(entry.data.u8[n]));
            }
        }
    }

    void CaptureSessionManager::enumerateCameras() {
        ACameraIdList* cameraIds = nullptr;
        std::vector<CameraDescription> cameras;

        if(ACameraManager_getCameraIdList(mCameraManager.get(), &cameraIds) != ACAMERA_OK)
            throw CameraSessionException("Failed to enumerate camera");

        // Wrap in smart pointer
        std::shared_ptr<ACameraIdList> safeCameraIds(cameraIds, ACameraManager_deleteCameraIdList);

        LOGI("Device has %d exposed camera", safeCameraIds->numCameras);

        // Go through all the cameras on this device
        for (int i = 0; i < safeCameraIds->numCameras; i++) {
            const char *id = safeCameraIds->cameraIds[i];
            ACameraMetadata* cameraChars = nullptr;

            // Get camera characteristics
            if(ACameraManager_getCameraCharacteristics(mCameraManager.get(), id, &cameraChars) == ACAMERA_OK) {
                std::shared_ptr<ACameraMetadata> safeCameraChars(cameraChars, ACameraMetadata_free);
                std::shared_ptr<CameraDescription> cameraDescription = std::make_shared<CameraDescription>();

                cameraDescription->id = id;

                updateCameraMetadata(safeCameraChars, *cameraDescription);
                mCameras.push_back(cameraDescription);
            }
        }
    }

    bool CaptureSessionManager::getRawConfiguration(const CameraDescription& cameraDesc, OutputConfiguration& rawConfiguration) {
        auto outputConfigs = cameraDesc.outputConfigs;
        auto rawIt = outputConfigs.find(AIMAGE_FORMAT_RAW10);

        if (rawIt == outputConfigs.end()) {
            rawIt = outputConfigs.find(AIMAGE_FORMAT_RAW16);
        }

        if (rawIt != outputConfigs.end()) {
            auto configurations = (*rawIt).second;

            if (!configurations.empty()) {
                rawConfiguration = configurations[0];
                return true;
            }
        }

        return false;
    }

    bool CaptureSessionManager::getPreviewConfiguration(
            const CameraDescription& cameraDesc,
            const DisplayDimension& captureSize,
            const DisplayDimension& displaySize,
            OutputConfiguration& outputConfiguration)
    {
        auto outputConfigs = cameraDesc.outputConfigs;

        OutputConfiguration closestConfig = { AIMAGE_FORMAT_YUV_420_888, DisplayDimension() };
        bool foundConfig = false;

        // Find the closest preview configuration to our display resolution
        auto yuvIt = outputConfigs.find(AIMAGE_FORMAT_YUV_420_888);

        if (yuvIt != outputConfigs.end()) {
            auto configurations = (*yuvIt).second;

            for (const auto& config : configurations) {
                if (config.outputSize.width()*config.outputSize.height() <= MAX_PREVIEW_PIXELS &&
                    config.outputSize.isCloseRatio(captureSize))
                {
                    if (config.outputSize < displaySize && config.outputSize > closestConfig.outputSize) {
                        closestConfig = config;
                        foundConfig = true;
                    }
                }
            }

            if (foundConfig) {
                outputConfiguration = closestConfig;
            }

            return foundConfig;
        }

        return false;
    }

    std::vector<std::string> CaptureSessionManager::getSupportedCameras() const {
        std::vector<std::string> cameraIds;

        auto it = mSupportedCameras.begin();

        while(it != mSupportedCameras.end()) {
            cameraIds.push_back(it->first);
            ++it;
        }

        return cameraIds;
    }

    std::string CaptureSessionManager::getSelectedCameraId() const {
        return mSelectedCameraId;
    }

    std::shared_ptr<CameraDescription> CaptureSessionManager::getCameraDescription(const std::string& cameraId) const {
        auto it = mSupportedCameras.find(cameraId);
        if(it != mSupportedCameras.end())
            return it->second;

        return nullptr;
    }

    void CaptureSessionManager::startCamera(
            const std::string& cameraId,
            std::shared_ptr<CameraSessionListener> listener,
            std::shared_ptr<ANativeWindow> previewOutputWindow)
    {
        OutputConfiguration outputConfig;

        if(mCameraSession != nullptr) {
            throw CameraSessionException("Camera session is already open");
        }

        auto cameraDesc = getCameraDescription(cameraId);
        if(!cameraDesc)
            throw CameraSessionException("Invalid camera");

        if(!getRawConfiguration(*cameraDesc, outputConfig)) {
            throw CameraSessionException("Failed to get output configuration");
        }

        // Create image consumer if we have not done so
        if(!mImageConsumer || cameraId != mSelectedCameraId)
            mImageConsumer = std::make_shared<RawImageConsumer>(cameraDesc->metadata, mMaxMemoryUsageBytes);

        // Create the camera session and open the camera
        mSelectedCameraId = cameraId;

        LOGI("Opening camera %s", cameraId.c_str());

        mCameraSession = std::make_shared<CameraSession>(listener, cameraDesc, mImageConsumer);
        mCameraSession->openCamera(outputConfig, mCameraManager, std::move(previewOutputWindow));
    }

    void CaptureSessionManager::stopCamera() {
        mCameraSession = nullptr;
    }

    void CaptureSessionManager::pauseCamera(bool pause) {
        if(!mCameraSession)
            return;

        if(pause) {
            mCameraSession->pauseCapture();
        }
        else {
            mCameraSession->resumeCapture();
        }
    }

    std::vector<std::shared_ptr<RawImageBuffer>> CaptureSessionManager::getBuffers() {
        if(mImageConsumer)
            return mImageConsumer->getBuffers();

        return std::vector<std::shared_ptr<RawImageBuffer>>();
    }

    std::shared_ptr<RawImageBuffer> CaptureSessionManager::getBuffer(int64_t timestamp) {
        if(mImageConsumer)
            return mImageConsumer->getBuffer(timestamp);

        return nullptr;
    }

    std::shared_ptr<RawImageBuffer> CaptureSessionManager::lockLatest() {
        if(mImageConsumer)
            return mImageConsumer->lockLatest();

        return nullptr;
    }

    void CaptureSessionManager::lockBuffers() {
        if(mImageConsumer)
            mImageConsumer->lockBuffers();
    }

    void CaptureSessionManager::unlockBuffers() {
        if(mImageConsumer)
            mImageConsumer->unlockBuffers();
    }

    void CaptureSessionManager::captureImage(
            const long handle,
            const int numSaveImages,
            const bool writeDNG,
            const motioncam::PostProcessSettings& settings,
            const std::string &outputPath)
    {
        if(mImageConsumer)
            mImageConsumer->save(handle, numSaveImages, writeDNG, settings, outputPath);
    }

    void CaptureSessionManager::setManualExposure(int32_t iso, int64_t exposureTime) {
        if(mCameraSession)
            mCameraSession->setManualExposure(iso, exposureTime);
    }

    void CaptureSessionManager::setAutoExposure() {
        if(mCameraSession)
            mCameraSession->setAutoExposure();
    }

    void CaptureSessionManager::setExposureCompensation(float value) {
        if(mCameraSession)
            mCameraSession->setExposureCompensation(value);
    }

    void CaptureSessionManager::enableRawPreview(std::shared_ptr<RawPreviewListener> listener) {
        if(mImageConsumer)
            mImageConsumer->enableRawPreview(std::move(listener));
    }

    void CaptureSessionManager::updateRawPreviewSettings(float shadows, float contrast, float saturation, float blacks, float whitePoint) {
        if(mImageConsumer)
            mImageConsumer->updateRawPreviewSettings(shadows, contrast, saturation, blacks, whitePoint);
    }

    void CaptureSessionManager::disableRawPreview() {
        if(mImageConsumer)
            mImageConsumer->disableRawPreview();
    }

    void CaptureSessionManager::updateOrientation(ScreenOrientation orientation) {
        if(mCameraSession)
            mCameraSession->updateOrientation(orientation);
    }

    void CaptureSessionManager::setFocusPoint(float focusX, float focusY, float exposureX, float exposureY) {
        if(mCameraSession)
            mCameraSession->setFocusPoint(focusX, focusY, exposureX, exposureY);
    }

    void CaptureSessionManager::setAutoFocus() {
        if(mCameraSession)
            mCameraSession->setAutoFocus();
    }
}