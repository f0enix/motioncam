#ifndef MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H
#define MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H

#include <memory>

#include "CameraSessionState.h"

struct ACameraCaptureSession;

namespace motioncam {
    struct CameraCaptureSessionContext;
    struct CameraDescription;

    enum class Action : int {
        NONE,
        REQUEST_USER_FOCUS,
        REQUEST_AUTO_FOCUS
    };

    enum class State : int {
        AUTO_FOCUS_WAIT = 0,
        TRIGGER_AUTO_FOCUS,
        AUTO_FOCUS_LOCKED,
        AUTO_FOCUS_ACTIVE,
        USER_FOCUS_WAIT,
        TRIGGER_USER_FOCUS,
        USER_FOCUS_LOCKED,
        USER_FOCUS_ACTIVE,
        PAUSED
    };

    class CameraStateManager {
    public:
        CameraStateManager(const CameraCaptureSessionContext& context, const CameraDescription& cameraDescription);

        void start();

        void requestPause();
        void requestResume();

        void requestUserFocus(float x, float y);
        void requestAutoFocus();
        void requestExposureCompensation(int exposureCompensation);

        void requestUserExposure(int32_t iso, int64_t exposureTime);
        void requestMode(CameraMode mode);

        void onCameraCaptureSequenceCompleted(const int sequenceId);
        void onCameraSessionStateChanged(const CameraCaptureSessionState state);

    private:
        bool triggerUserAutoFocus();
        bool triggerAutoFocus();
        bool setUserFocus();
        bool setAutoFocus();

        void setState(State state);
        void setNextAction(Action action);

        void nextAction();
        void nextState(CameraCaptureSessionState state);

        void updateCaptureRequestExposure();

    private:
        const CameraCaptureSessionContext& mSessionContext;
        const CameraDescription& mCameraDescription;

        CameraCaptureSessionState mCaptureSessionState;
        State mState;
        Action mRequestedAction;
        CameraMode mCameraMode;
        int mExposureCompensation;

        float mRequestedFocusX;
        float mRequestedFocusY;

        int32_t mUserIso;
        int64_t mUserExposureTime;
    };
}

#endif //MOTIONCAM_ANDROID_CAMERASTATEMANAGER_H
