#ifndef MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H
#define MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H

#include <memory>

#include "CameraSessionState.h"

struct ACameraCaptureSession;

namespace motioncam {
    struct CameraCaptureSessionContext;
    struct CameraDescription;

    enum class State : int {
        AUTO_FOCUS_WAIT = 0,
        TRIGGER_AUTO_FOCUS,
        AUTO_FOCUS_READY,
        USER_FOCUS_WAIT,
        TRIGGER_USER_FOCUS,
        USER_FOCUS_LOCKED
    };

    class CameraStateManager {
    public:
        CameraStateManager(const CameraCaptureSessionContext& context, const CameraDescription& cameraDescription);

        void start();

        void requestUserFocus(float x, float y);
        void requestAutoFocus();
        void requestExposureCompensation(int exposureCompensation);

        void onCameraCaptureSequenceCompleted(const int sequenceId);
        void onCameraSessionStateChanged(const CameraCaptureSessionState state);

    private:
        bool setRepeatingCapture();
        bool triggerUserAutoFocus();
        bool triggerAutoFocus();
        bool setUserFocus();
        bool setAutoFocus();

    private:
        const CameraCaptureSessionContext& mSessionContext;
        const CameraDescription& mCameraDescription;

        State mState;
        int mExposureCompensation;

        float mRequestedFocusX;
        float mRequestedFocusY;
    };
}

#endif //MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H
