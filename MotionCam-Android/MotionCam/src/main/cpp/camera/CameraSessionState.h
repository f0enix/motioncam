#ifndef MOTIONCAM_ANDROID_CAMERASESSIONSTATE_H
#define MOTIONCAM_ANDROID_CAMERASESSIONSTATE_H

namespace motioncam {
    enum class CameraCaptureSessionState : int {
        READY = 0,
        ACTIVE,
        CLOSED
    };

    enum class CameraMode : int {
        AUTO,
        MANUAL
    };

    enum class CaptureEvent : int {
        REPEAT = 0,
        HDR_CAPTURE
    };

    enum class CameraFocusState : int {
        INACTIVE = 0,
        PASSIVE_SCAN,
        PASSIVE_FOCUSED,
        ACTIVE_SCAN,
        FOCUS_LOCKED,
        NOT_FOCUS_LOCKED,
        PASSIVE_UNFOCUSED
    };

    enum class CameraExposureState : int {
        INACTIVE = 0,
        SEARCHING,
        CONVERGED,
        LOCKED,
        FLASH_REQUIRED,
        PRECAPTURE
    };
}

#endif //MOTIONCAM_ANDROID_CAMERASESSIONSTATE_H
