#include "CameraStateManager.h"
#include "CameraSessionContext.h"
#include "Logger.h"

#include <camera/NdkCameraDevice.h>

namespace motioncam {

    static std::string ToString(State state) {
        switch(state) {
            case State::AUTO_FOCUS_WAIT:
                return "AUTO_FOCUS_WAIT";

            case State::TRIGGER_AUTO_FOCUS:
                return "TRIGGER_AUTO_FOCUS";

            case State::AUTO_FOCUS_LOCKED:
                return "AUTO_FOCUS_LOCKED";

            case State::AUTO_FOCUS_ACTIVE:
                return "AUTO_FOCUS_ACTIVE";

            case State::USER_FOCUS_WAIT:
                return "USER_FOCUS_WAIT";

            case State::TRIGGER_USER_FOCUS:
                return "TRIGGER_USER_FOCUS";

            case State::USER_FOCUS_LOCKED:
                return "USER_FOCUS_LOCKED";

            case State::USER_FOCUS_ACTIVE:
                return "USER_FOCUS_ACTIVE";

            case State::PAUSED:
                return "PAUSED";

            default:
                return "UNKNOWN";
        }
    }

    static std::string ToString(Action action) {
        switch(action) {
            case Action::NONE:
                return "NONE";

            case Action::REQUEST_USER_FOCUS:
                return "REQUEST_USER_FOCUS";

            case Action::REQUEST_AUTO_FOCUS:
                return "REQUEST_AUTO_FOCUS";

            default:
                return "UNKNOWN";
        }
    }

    CameraStateManager::CameraStateManager(const CameraCaptureSessionContext& context, const CameraDescription& cameraDescription) :
        mSessionContext(context),
        mCameraDescription(cameraDescription),
        mCameraMode(CameraMode::AUTO),
        mState(State::AUTO_FOCUS_ACTIVE),
        mRequestedAction(Action::NONE),
        mExposureCompensation(0),
        mRequestedFocusX(0),
        mRequestedFocusY(0),
        mUserIso(0),
        mUserExposureTime(0)
    {
    }

    void CameraStateManager::start() {
        setState(State::AUTO_FOCUS_WAIT);
        triggerAutoFocus();
    }

    void CameraStateManager::setState(State state) {
        LOGD("setState(%s -> %s)", ToString(mState).c_str(), ToString(state).c_str());
        mState = state;
    }

    void CameraStateManager::setNextAction(Action action) {
        LOGD("setNextAction(%s -> %s)", ToString(mRequestedAction).c_str(), ToString(action).c_str());
        mRequestedAction = action;
    }

    void CameraStateManager::requestUserExposure(int32_t iso, int64_t exposureTime) {
        mUserIso = iso;
        mUserExposureTime = exposureTime;

        if(mState == State::AUTO_FOCUS_ACTIVE) {
            setAutoFocus();
        }
        else if(mState == State::USER_FOCUS_LOCKED) {
            setUserFocus();
        }
    }

    void CameraStateManager::requestMode(CameraMode mode) {
        mCameraMode = mode;
    }

    void CameraStateManager::requestPause() {
        LOGD("requestPause()");

        mState = State::PAUSED;
        ACameraCaptureSession_stopRepeating(mSessionContext.captureSession.get());
    }

    void CameraStateManager::requestResume() {
        LOGD("requestResume()");

        if( mState == State::PAUSED ) {
            setState(State::AUTO_FOCUS_WAIT);
            triggerAutoFocus();
        }
    }

    void CameraStateManager::requestUserFocus(float x, float y) {
        mRequestedFocusX = x;
        mRequestedFocusY = y;

        LOGD("requestUserFocus(%.2f, %.2f)", x, y);

        if( mState == State::AUTO_FOCUS_ACTIVE ||
            mState == State::USER_FOCUS_ACTIVE)
        {
            mState = State::USER_FOCUS_WAIT;
            ACameraCaptureSession_stopRepeating(mSessionContext.captureSession.get());
        }
        else {
            setNextAction(Action::REQUEST_USER_FOCUS);
        }
    }

    void CameraStateManager::requestAutoFocus() {
        mRequestedFocusX = 0.5f;
        mRequestedFocusY = 0.5f;

        LOGD("requestAutoFocus()");

        if(mState == State::AUTO_FOCUS_ACTIVE) {
           return;
        }

        if( mState == State::USER_FOCUS_ACTIVE )
        {
            mState = State::AUTO_FOCUS_WAIT;
            ACameraCaptureSession_stopRepeating(mSessionContext.captureSession.get());
        }
        else {
            setNextAction(Action::REQUEST_AUTO_FOCUS);
        }
    }

    void CameraStateManager::requestExposureCompensation(int exposureCompensation) {
        if(mExposureCompensation == exposureCompensation)
            return;

        LOGD("exposureCompensation=%d", exposureCompensation);

        mExposureCompensation = exposureCompensation;

        if(mState == State::AUTO_FOCUS_ACTIVE) {
            setAutoFocus();
        }
        else if(mState == State::USER_FOCUS_LOCKED) {
            setUserFocus();
        }
    }

    void CameraStateManager::updateCaptureRequestExposure() {
        uint8_t aeMode;

        if(mCameraMode == CameraMode::AUTO) {
            aeMode = ACAMERA_CONTROL_AE_MODE_ON;

            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_SENSITIVITY, 0, nullptr);
            ACaptureRequest_setEntry_i64(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_EXPOSURE_TIME, 0, nullptr);
            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &mExposureCompensation);
        }
        else if(mCameraMode == CameraMode::MANUAL) {
            aeMode = ACAMERA_CONTROL_AE_MODE_OFF;

            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_SENSITIVITY, 1, &mUserIso);
            ACaptureRequest_setEntry_i64(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &mUserExposureTime);
            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 0, nullptr);

            LOGD("userIso=%d, userExposure=%.4f", mUserIso, mUserExposureTime/1.0e9f);
        }

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
    }

    bool CameraStateManager::triggerUserAutoFocus() {
        uint8_t afMode      = ACAMERA_CONTROL_AF_MODE_AUTO;
        uint8_t afTrigger   = ACAMERA_CONTROL_AF_TRIGGER_START;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);

        // Set the focus region
        int w = static_cast<int>(mCameraDescription.sensorSize.width * 0.125f);
        int h = static_cast<int>(mCameraDescription.sensorSize.height * 0.125f);

        int px = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.left + mCameraDescription.sensorSize.width) * mRequestedFocusX);
        int py = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.top + mCameraDescription.sensorSize.height) * mRequestedFocusY);

        int32_t afRegion[5] = { px - w, py - h,
                                px + w, py + h,
                                1000 };

        ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afRegion[0]);

        // Set auto exposure region if supported or use user settings for exposure
        uint8_t aeTrigger;

        if(mCameraMode == CameraMode::AUTO) {
            aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_START;

            if(mCameraDescription.maxAeRegions > 0) {
                ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 5, &afRegion[0]);
            }
        }
        else if(mCameraMode == CameraMode::MANUAL) {
            aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 0, nullptr);
        }

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        updateCaptureRequestExposure();

        LOGD("triggerUserAutoFocus()");

        return ACameraCaptureSession_capture(
            mSessionContext.captureSession.get(),
            &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
            1,
            &mSessionContext.repeatCaptureRequest->captureRequest,
            &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId) == ACAMERA_OK;
    }

    bool CameraStateManager::setUserFocus() {
        uint8_t afMode = ACAMERA_CONTROL_AF_MODE_AUTO;
        uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);

        uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        updateCaptureRequestExposure();

        LOGD("setUserFocus()");

        return ACameraCaptureSession_setRepeatingRequest(
            mSessionContext.captureSession.get(),
            &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
            1,
            &mSessionContext.repeatCaptureRequest->captureRequest,
            &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId) == ACAMERA_OK;
    }

    bool CameraStateManager::triggerAutoFocus() {
        uint8_t afMode      = ACAMERA_CONTROL_AF_MODE_AUTO;
        uint8_t afTrigger   = ACAMERA_CONTROL_AF_TRIGGER_START;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);

        int px = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.left + mCameraDescription.sensorSize.width) * 0.5f);
        int py = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.top + mCameraDescription.sensorSize.height) * 0.5f);

        int w = static_cast<int>(mCameraDescription.sensorSize.width * 0.25f);
        int h = static_cast<int>(mCameraDescription.sensorSize.height * 0.25f);

        int32_t afRegion[5] = { px - w, py - h,
                                px + w, py + h,
                                1000 };

        ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afRegion[0]);

        // Set auto exposure region if supported
        uint8_t aeTrigger;

        if(mCameraMode == CameraMode::AUTO) {
            aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_START;

            if(mCameraDescription.maxAeRegions > 0) {
                ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 5, &afRegion[0]);
            }
        }
        else {
            aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 0, nullptr);
        }

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        updateCaptureRequestExposure();

        LOGD("triggerAutoFocus()");

        return ACameraCaptureSession_capture(
                mSessionContext.captureSession.get(),
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
                1,
                &mSessionContext.repeatCaptureRequest->captureRequest,
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId) == ACAMERA_OK;
    }

    bool CameraStateManager::setAutoFocus() {
        uint8_t afMode  = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;
        uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &mExposureCompensation);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        updateCaptureRequestExposure();

        LOGD("setAutoFocus()");
        setState(State::AUTO_FOCUS_ACTIVE);

        return ACameraCaptureSession_setRepeatingRequest(
                mSessionContext.captureSession.get(),
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
                1,
                &mSessionContext.repeatCaptureRequest->captureRequest,
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId) == ACAMERA_OK;
    }

    void CameraStateManager::nextAction() {
        LOGD("nextAction(%s)", ToString(mRequestedAction).c_str());

        if(mRequestedAction == Action::REQUEST_AUTO_FOCUS) {
            requestAutoFocus();
        }
        else if(mRequestedAction == Action::REQUEST_USER_FOCUS) {
            requestUserFocus(mRequestedFocusX, mRequestedFocusY);
        }

        setNextAction(Action::NONE);
    }

    void CameraStateManager::onCameraSessionStateChanged(const CameraCaptureSessionState state) {
        //
        // Move state when camera session state changes
        //

        if(state == CameraCaptureSessionState::READY) {
            if(mState == State::TRIGGER_USER_FOCUS) {
                triggerUserAutoFocus();
            }
            else if(mState == State::TRIGGER_AUTO_FOCUS) {
                triggerAutoFocus();
            }
            else if(mState == State::AUTO_FOCUS_LOCKED) {
                setAutoFocus();
            }
            else if(mState == State::USER_FOCUS_LOCKED) {
                setUserFocus();
            }
        }
        else if(state == CameraCaptureSessionState::ACTIVE) {
            if(mState == State::AUTO_FOCUS_LOCKED) {
                setState(State::AUTO_FOCUS_ACTIVE);
            }
            else if(mState == State::USER_FOCUS_LOCKED) {
                setState(State::USER_FOCUS_ACTIVE);
            }

            // If there's a pending action execute it now.
            if(mRequestedAction != Action::NONE) {
                nextAction();
                return;
            }
        }
    }

    void CameraStateManager::onCameraCaptureSequenceCompleted(const int sequenceId) {
        if(mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId == sequenceId) {
            if(mState == State::USER_FOCUS_WAIT) {
                LOGD("USER_FOCUS_WAIT completed");
                setState(State::TRIGGER_USER_FOCUS);
            }
            else if(mState == State::TRIGGER_USER_FOCUS) {
                LOGD("TRIGGER_USER_FOCUS completed");
                setState(State::USER_FOCUS_LOCKED);
            }
            else if(mState == State::AUTO_FOCUS_WAIT) {
                LOGD("AUTO_FOCUS_WAIT completed");
                setState(State::TRIGGER_AUTO_FOCUS);
            }
            else if(mState == State::TRIGGER_AUTO_FOCUS) {
                LOGD("TRIGGER_AUTO_FOCUS completed");
                setState(State::AUTO_FOCUS_LOCKED);
            }
            else if(mState == State::AUTO_FOCUS_ACTIVE) {
                LOGD("AUTO_FOCUS_ACTIVE completed");
                setState(State::AUTO_FOCUS_WAIT);
            }
        }
    }
}