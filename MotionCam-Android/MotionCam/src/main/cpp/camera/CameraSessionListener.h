#ifndef MOTIONCAM_ANDROID_CAMERASESSIONLISTENER_H
#define MOTIONCAM_ANDROID_CAMERASESSIONLISTENER_H

#include "CameraSession.h"
#include <stdint.h>

namespace motioncam {

    class CameraSessionListener {
    public:
        virtual void onCameraStateChanged(const CameraCaptureSessionState state) = 0;
        virtual void onCameraDisconnected() = 0;
        virtual void onCameraError(const int error) = 0;
        virtual void onCameraExposureStatus(const int32_t iso, const int64_t exposureTime) = 0;
        virtual void onCameraAutoFocusStateChanged(const CameraFocusState state) = 0;
        virtual void onCameraAutoExposureStateChanged(const CameraExposureState state) = 0;
    };
}

#endif //MOTIONCAM_ANDROID_CAMERASESSIONLISTENER_H
