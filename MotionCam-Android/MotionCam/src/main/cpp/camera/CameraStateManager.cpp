#include "CameraStateManager.h"
#include "CameraSessionContext.h"
#include "Logger.h"

#include <camera/NdkCameraDevice.h>

namespace motioncam {

    CameraStateManager::CameraStateManager(const CameraCaptureSessionContext& context, const CameraDescription& cameraDescription) :
        mSessionContext(context),
        mCameraDescription(cameraDescription),
        mState(State::AUTO_FOCUS_WAIT),
        mExposureCompensation(0),
        mRequestedFocusX(0),
        mRequestedFocusY(0)
    {
    }

    void CameraStateManager::start() {
        mState = State::AUTO_FOCUS_READY;
        setRepeatingCapture();
    }

    void CameraStateManager::requestUserFocus(float x, float y) {
        mRequestedFocusX = x;
        mRequestedFocusY = y;

        if(mState == State::AUTO_FOCUS_READY) {
            mState = State::USER_FOCUS_WAIT;
            ACameraCaptureSession_stopRepeating(mSessionContext.captureSession.get());
        }
    }

    void CameraStateManager::requestAutoFocus() {
        mRequestedFocusX = 0.5f;
        mRequestedFocusY = 0.5f;

        if(mState == State::USER_FOCUS_LOCKED) {
            mState = State::AUTO_FOCUS_WAIT;
            ACameraCaptureSession_stopRepeating(mSessionContext.captureSession.get());
        }
    }

    void CameraStateManager::requestExposureCompensation(int exposureCompensation) {
        if(mExposureCompensation == exposureCompensation)
            return;

        mExposureCompensation = exposureCompensation;
        LOGI("Updating exposure compensation to %d", mExposureCompensation);

        if(mState == State::AUTO_FOCUS_READY || mState == State::USER_FOCUS_LOCKED) {
            setRepeatingCapture();
        }
    }

    bool CameraStateManager::setRepeatingCapture() {
        uint8_t aeMode  = ACAMERA_CONTROL_AE_MODE_ON;
        uint8_t afMode  = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;
        uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &mExposureCompensation);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        return ACameraCaptureSession_setRepeatingRequest(
                mSessionContext.captureSession.get(),
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
                1,
                &mSessionContext.repeatCaptureRequest->captureRequest,
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId) == ACAMERA_OK;
    }

    bool CameraStateManager::triggerUserAutoFocus() {
        uint8_t afMode      = ACAMERA_CONTROL_AF_MODE_AUTO;
        uint8_t afTrigger   = ACAMERA_CONTROL_AF_TRIGGER_START;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);

        uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_START;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        // Set the focus region
        int w = static_cast<int>(mCameraDescription.sensorSize.width * 0.125f);
        int h = static_cast<int>(mCameraDescription.sensorSize.height * 0.125f);

        int px = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.left + mCameraDescription.sensorSize.width) * mRequestedFocusX);
        int py = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.top + mCameraDescription.sensorSize.height) * mRequestedFocusY);

        int32_t afRegion[5] = { px - w, py - h,
                                px + w, py + h,
                                1000 };

        ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afRegion[0]);

        // Set auto exposure region if supported
        if(mCameraDescription.maxAeRegions > 0) {
            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 5, &afRegion[0]);
        }

        LOGD("Initiating fixed focus");

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

        LOGD("Setting fixed focus");

        return ACameraCaptureSession_setRepeatingRequest(
            mSessionContext.captureSession.get(),
            &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
            1,
            &mSessionContext.repeatCaptureRequest->captureRequest,
            &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId) == ACAMERA_OK;
    }

    bool CameraStateManager::triggerAutoFocus() {
        uint8_t afMode      = ACAMERA_CONTROL_AF_MODE_AUTO;
        uint8_t aeMode      = ACAMERA_CONTROL_AE_MODE_ON;
        uint8_t afTrigger   = ACAMERA_CONTROL_AF_TRIGGER_START;
        uint8_t aeTrigger   = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_START;

        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
        ACaptureRequest_setEntry_u8(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);

        int px = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.left + mCameraDescription.sensorSize.width) * 0.5f);
        int py = static_cast<int>(static_cast<float>(mCameraDescription.sensorSize.top + mCameraDescription.sensorSize.height) * 0.5f);

        int w = static_cast<int>(mCameraDescription.sensorSize.width * 0.125f);
        int h = static_cast<int>(mCameraDescription.sensorSize.height * 0.125f);

        int32_t afRegion[5] = { px - w, py - h,
                                px + w, py + h,
                                1000 };

        ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afRegion[0]);

        // Set auto exposure region if supported
        if(mCameraDescription.maxAeRegions > 0) {
            ACaptureRequest_setEntry_i32(mSessionContext.repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 5, &afRegion[0]);
        }

        return ACameraCaptureSession_capture(
                mSessionContext.captureSession.get(),
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->callbacks,
                1,
                &mSessionContext.repeatCaptureRequest->captureRequest,
                &mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId) == ACAMERA_OK;
    }

    bool CameraStateManager::setAutoFocus() {
        setRepeatingCapture();

        return true;
    }

    void CameraStateManager::onCameraSessionStateChanged(const CameraCaptureSessionState state) {
        // When camera is ready we'll run the next step of our "state machine"
        if(state == CameraCaptureSessionState::READY) {
            if(mState == State::TRIGGER_USER_FOCUS) {
                triggerUserAutoFocus();
            }
            else if(mState == State::TRIGGER_AUTO_FOCUS) {
                triggerAutoFocus();
            }
            else if(mState == State::AUTO_FOCUS_READY) {
                setAutoFocus();
            }
            else if(mState == State::USER_FOCUS_LOCKED) {
                setUserFocus();
            }
        }
    }

    void CameraStateManager::onCameraCaptureSequenceCompleted(const int sequenceId) {
        if(mSessionContext.captureCallbacks.at(CaptureEvent::REPEAT)->sequenceId == sequenceId) {
            if(mState == State::USER_FOCUS_WAIT) {
                LOGD("USER_FOCUS_WAIT completed");
                mState = State::TRIGGER_USER_FOCUS;
            }
            else if(mState == State::TRIGGER_USER_FOCUS) {
                LOGD("TRIGGER_USER_FOCUS completed");
                mState = State::USER_FOCUS_LOCKED;
            }
            else if(mState == State::AUTO_FOCUS_WAIT) {
                LOGD("AUTO_FOCUS_WAIT completed");
                mState = State::TRIGGER_AUTO_FOCUS;
            }
            else if(mState == State::TRIGGER_AUTO_FOCUS) {
                LOGD("TRIGGER_AUTO_FOCUS completed");
                mState = State::AUTO_FOCUS_READY;
            }
            else if(mState == State::AUTO_FOCUS_READY) {
                LOGD("AUTO_FOCUS_READY completed");
            }
        }
    }
}