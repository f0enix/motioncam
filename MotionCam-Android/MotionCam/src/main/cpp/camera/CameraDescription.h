#ifndef MOTIONCAM_ANDROID_CAMERADESCRIPTION_H
#define MOTIONCAM_ANDROID_CAMERADESCRIPTION_H

#include <vector>
#include <map>
#include <string>

#include <camera/NdkCameraMetadataTags.h>
#include <motioncam/RawImageMetadata.h>

#include "DisplayDimension.h"

namespace motioncam {

    struct OutputConfiguration
    {
        int32_t format;
        DisplayDimension outputSize;
    };

    struct CameraDescription {
        CameraDescription() :
            hardwareLevel(ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY),
            lensFacing(ACAMERA_LENS_FACING_BACK),
            exposureCompensationRange{0},
            isoRange{0},
            exposureRange{0},
            exposureCompensationStepFraction{1},
            sensorSize{0},
            maxAfRegions(0),
            maxAeRegions(0),
            maxAwbRegions(0),
            sensorOrientation(0)
        {
        }

        std::string id;
        std::vector<acamera_metadata_enum_android_request_available_capabilities_t> supportedCaps;
        acamera_metadata_enum_android_info_supported_hardware_level_t hardwareLevel;
        acamera_metadata_enum_android_lens_facing_t lensFacing;
        std::map<int32_t, std::vector<OutputConfiguration>> outputConfigs;
        std::vector<acamera_metadata_enum_android_lens_optical_stabilization_mode_t> oisModes;
        std::vector<acamera_metadata_enum_android_tonemap_mode_t> tonemapModes;

        RawCameraMetadata metadata;

        int32_t exposureCompensationStepFraction[2];
        int32_t exposureCompensationRange[2];
        int32_t isoRange[2];
        int64_t exposureRange[2];

        int sensorSize[4];
        int maxAfRegions;
        int maxAeRegions;
        int maxAwbRegions;

        int sensorOrientation;
    };
}

#endif //MOTIONCAM_ANDROID_CAMERADESCRIPTION_H
