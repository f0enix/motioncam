#include "CameraSession.h"
#include "Exceptions.h"
#include "Logger.h"
#include "CameraSessionListener.h"
#include "RawImageConsumer.h"

#include <motioncam/Util.h>
#include <motioncam/Settings.h>

#include <string>
#include <utility>
#include <utility>
#include <vector>

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraMetadata.h>
#include <queue/blockingconcurrentqueue.h>

namespace motioncam {
    const static uint32_t MAX_BUFFERED_RAW_IMAGES = 4;

    struct CaptureCallbackContext {
        ACameraCaptureSession_captureCallbacks callbacks;
        CaptureEvent event;
        int sequenceId;
        CameraSession* cameraSession;
    };

    enum class EventAction : int {
        ACTION_OPEN_CAMERA = 0,
        ACTION_CLOSE_CAMERA,
        ACTION_PAUSE_CAPTURE,
        ACTION_RESUME_CAPTURE,

        ACTION_SET_AUTO_EXPOSURE,
        ACTION_SET_MANUAL_EXPOSURE,
        ACTION_SET_EXPOSURE_COMP_VALUE,
        ACTION_SET_AUTO_FOCUS,
        ACTION_SET_FOCUS_POINT,
        ACTION_CAPTURE_HDR,

        EVENT_SAVE_HDR_DATA,

        EVENT_CAMERA_ERROR,
        EVENT_CAMERA_DISCONNECTED,
        EVENT_CAMERA_SESSION_CHANGED,

        EVENT_CAMERA_EXPOSURE_STATUS_CHANGED,
        EVENT_CAMERA_AUTO_EXPOSURE_STATE_CHANGED,
        EVENT_CAMERA_AUTO_FOCUS_STATE_CHANGED,

        STOP
    };

    struct EventLoopData {
        EventLoopData(EventAction eventAction, json11::Json data) :
                eventAction(eventAction),
                data(std::move(data))
        {
        }

        const EventAction eventAction;
        const json11::Json data;
    };

    struct CaptureRequest {
        CaptureRequest(ACaptureRequest* captureRequest, bool isPreviewOutput) :
            captureRequest(captureRequest), isPreviewOutput(isPreviewOutput) {
        }

        ~CaptureRequest() {
            if(captureRequest)
                ACaptureRequest_free(captureRequest);
        }

        ACaptureRequest* captureRequest;
        bool isPreviewOutput;
    };

    struct CameraCaptureSessionContext {
        // Event loop
        moodycamel::BlockingConcurrentQueue<EventLoopDataPtr> eventLoopQueue;
        std::unique_ptr<std::thread> eventLoopThread;

        // Setup
        OutputConfiguration outputConfig;
        std::shared_ptr<ACameraManager> cameraManager;
        std::shared_ptr<ANativeWindow> nativeWindow;

        // Callbacks
        ACameraDevice_stateCallbacks deviceStateCallbacks;
        ACameraCaptureSession_stateCallbacks sessionStateCallbacks;
        std::map<CaptureEvent, std::shared_ptr<CaptureCallbackContext>> captureCallbacks;

        std::shared_ptr<ACameraDevice> activeCamera;

        // Session
        std::shared_ptr<ACaptureSessionOutputContainer> captureSessionContainer;
        std::shared_ptr<ACameraCaptureSession> captureSession;

        std::shared_ptr<CaptureRequest> afCaptureRequest;
        std::shared_ptr<CaptureRequest> repeatCaptureRequest;
        std::shared_ptr<CaptureRequest> hdrCaptureRequests[2];

        std::shared_ptr<ACaptureSessionOutput> previewSessionOutput;
        std::shared_ptr<ACameraOutputTarget> previewOutputTarget;

        std::shared_ptr<ACaptureSessionOutput> rawSessionOutput;
        std::shared_ptr<ACameraOutputTarget> rawOutputTarget;

        // Image reader
        std::shared_ptr<AImageReader> rawImageReader;
        AImageReader_ImageListener rawImageListener;
    };

    CameraFocusState GetFocusState(acamera_metadata_enum_android_control_af_state_t state) {
        switch(state) {
            default:
            case ACAMERA_CONTROL_AF_STATE_INACTIVE:
                return CameraFocusState::INACTIVE;

            case ACAMERA_CONTROL_AF_STATE_PASSIVE_SCAN:
                return CameraFocusState::PASSIVE_SCAN;

            case ACAMERA_CONTROL_AF_STATE_PASSIVE_FOCUSED:
                return CameraFocusState::PASSIVE_FOCUSED;

            case ACAMERA_CONTROL_AF_STATE_ACTIVE_SCAN:
                return CameraFocusState::ACTIVE_SCAN;

            case ACAMERA_CONTROL_AF_STATE_FOCUSED_LOCKED:
                return CameraFocusState::FOCUS_LOCKED;

            case ACAMERA_CONTROL_AF_STATE_NOT_FOCUSED_LOCKED:
                return CameraFocusState::NOT_FOCUS_LOCKED;

            case ACAMERA_CONTROL_AF_STATE_PASSIVE_UNFOCUSED:
                return CameraFocusState::PASSIVE_UNFOCUSED;
        }
    }

    CameraExposureState GetExposureState(acamera_metadata_enum_android_control_ae_state_t state) {
        switch(state) {
            default:
            case ACAMERA_CONTROL_AE_STATE_INACTIVE:
                return CameraExposureState::INACTIVE;

            case ACAMERA_CONTROL_AE_STATE_SEARCHING:
                return CameraExposureState::SEARCHING;

            case ACAMERA_CONTROL_AE_STATE_CONVERGED:
                return CameraExposureState::CONVERGED;

            case ACAMERA_CONTROL_AE_STATE_LOCKED:
                return CameraExposureState::LOCKED;

            case ACAMERA_CONTROL_AE_STATE_FLASH_REQUIRED:
                return CameraExposureState::FLASH_REQUIRED;

            case ACAMERA_CONTROL_AE_STATE_PRECAPTURE:
                return CameraExposureState::PRECAPTURE;
        }
    }

    namespace {
        void OnImageAvailable(void *context, AImageReader *reader) {
            reinterpret_cast<CameraSession *>(context)->onRawImageAvailable(reader);
        }

        void OnCameraError(void* context, ACameraDevice* device, int error) {
            reinterpret_cast<CameraSession *>(context)->onCameraError(error);
        }

        void OnCameraDisconnected(void* context, ACameraDevice* device) {
            reinterpret_cast<CameraSession *>(context)->onCameraDisconnected();
        }

        void OnCameraSessionClosed(void* context, ACameraCaptureSession* session) {
            reinterpret_cast<CameraSession *>(context)->onCameraSessionStateClosed();
        }

        void OnCameraSessionReady(void* context, ACameraCaptureSession* session) {
            reinterpret_cast<CameraSession *>(context)->onCameraSessionStateReady();
        }

        void OnCameraSessionActive(void* context, ACameraCaptureSession* session) {
            reinterpret_cast<CameraSession *>(context)->onCameraSessionStateActive();
        }

        //
        // Capture callbacks
        //

        void OnCameraCaptureStarted(void* context, ACameraCaptureSession* session, const ACaptureRequest* request, int64_t timestamp) {
            auto* callbackContext = reinterpret_cast<CaptureCallbackContext*>(context);
            callbackContext->cameraSession->onCameraCaptureStarted(*callbackContext, request, timestamp);
        }

        void OnCameraCaptureCompleted(void* context, ACameraCaptureSession* session, ACaptureRequest* request, const ACameraMetadata* result) {
            auto* callbackContext = reinterpret_cast<CaptureCallbackContext*>(context);
            callbackContext->cameraSession->onCameraCaptureCompleted(*callbackContext, result);
        }

        void OnCameraCaptureFailed(void* context, ACameraCaptureSession* session, ACaptureRequest* request, ACameraCaptureFailure* failure) {
            auto* callbackContext = reinterpret_cast<CaptureCallbackContext*>(context);
            callbackContext->cameraSession->onCameraCaptureFailed(*callbackContext, failure);
        }

        void OnCameraCaptureProgressed(void* context, ACameraCaptureSession* session, ACaptureRequest* request, const ACameraMetadata* result) {
            auto* callbackContext = reinterpret_cast<CaptureCallbackContext*>(context);
            callbackContext->cameraSession->onCameraCaptureProgressed(*callbackContext, result);
        }

        void OnCameraCaptureBufferLost(void* context, ACameraCaptureSession* session, ACaptureRequest* request, ANativeWindow* window, int64_t frameNumber) {
            auto* callbackContext = reinterpret_cast<CaptureCallbackContext*>(context);
            callbackContext->cameraSession->onCameraCaptureBufferLost(*callbackContext, frameNumber);
        }

        void OnCameraCaptureSequenceCompleted(void* context, ACameraCaptureSession* session, int sequenceId, int64_t frameNumber) {
            auto* callbackContext = reinterpret_cast<CaptureCallbackContext*>(context);
            callbackContext->cameraSession->onCameraCaptureSequenceCompleted(*callbackContext, sequenceId);
        }

        void OnCameraCaptureSequenceAborted(void* context, ACameraCaptureSession* session, int sequenceId) {
            auto* callbackContext = reinterpret_cast<CaptureCallbackContext*>(context);
            callbackContext->cameraSession->onCameraCaptureSequenceAborted(*callbackContext, sequenceId);
        }
    }

    CameraSession::CameraSession(std::shared_ptr<CameraSessionListener> listener, const std::shared_ptr<CameraDescription>& cameraDescription, std::shared_ptr<RawImageConsumer> rawImageConsumer) :
        mState(CameraCaptureSessionState::CLOSED),
        mIsPaused(false),
        mMode(CameraMode::AUTO),
        mLastIso(0),
        mLastExposureTime(0),
        mLastFocusState(CameraFocusState::INACTIVE),
        mLastExposureState(CameraExposureState::INACTIVE),
        mCameraDescription(cameraDescription),
        mImageConsumer(std::move(rawImageConsumer)),
        mSessionListener(std::move(listener)),
        mScreenOrientation(ScreenOrientation::PORTRAIT),
        mRequestedHdrCaptures(0),
        mHdrCaptureInProgress(false),
        mExposureCompensation(0),
        mUserIso(100),
        mUserExposureTime(10000000)
    {
    }

    CameraSession::~CameraSession() {
        closeCamera();
    }

    void CameraSession::openCamera(
        const OutputConfiguration& rawOutputConfig,
        std::shared_ptr<ACameraManager> cameraManager,
        std::shared_ptr<ANativeWindow> previewOutputWindow)
    {
        if(mSessionContext) {
            LOGE("Trying to open camera while already running!");
            return;
        }

        mMode = CameraMode::AUTO;

        // Create new session context and set up callbacks
        mSessionContext = std::make_shared<CameraCaptureSessionContext>();

        mSessionContext->outputConfig = rawOutputConfig;
        mSessionContext->cameraManager = std::move(cameraManager);
        mSessionContext->nativeWindow = std::move(previewOutputWindow);

        setupCallbacks();

        // Create event loop and start
        mSessionContext->eventLoopThread = std::make_unique<std::thread>(std::thread(&CameraSession::doEventLoop, this));

        pushEvent(EventAction::ACTION_OPEN_CAMERA);
    }

    void CameraSession::closeCamera() {
        if(!mSessionContext)
            return;

        pushEvent(EventAction::ACTION_CLOSE_CAMERA);
        pushEvent(EventAction::STOP);

        if(mSessionContext->eventLoopThread->joinable()) {
            mSessionContext->eventLoopThread->join();
        }

        mSessionContext = nullptr;
    }

    void CameraSession::pauseCapture() {
        pushEvent(EventAction::ACTION_PAUSE_CAPTURE);
    }

    void CameraSession::resumeCapture() {
        pushEvent(EventAction::ACTION_RESUME_CAPTURE);
    }

    void CameraSession::setManualExposure(int32_t iso, int64_t exposureTime) {
        json11::Json::object data = {
                { "iso", iso },
                { "exposureTime", std::to_string(exposureTime) }
        };

        pushEvent(EventAction::ACTION_SET_MANUAL_EXPOSURE, data);
    }

    void CameraSession::setAutoExposure() {
        pushEvent(EventAction::ACTION_SET_AUTO_EXPOSURE);
    }

    void CameraSession::setExposureCompensation(float value) {
        json11::Json::object data = { { "value", value } };
        pushEvent(EventAction::ACTION_SET_EXPOSURE_COMP_VALUE, data);
    }

    void CameraSession::setFocusPoint(float focusX, float focusY, float exposureX, float exposureY) {
        json11::Json::object data = {
                { "focusX", focusX },
                { "focusY", focusY },
                { "exposureX", exposureX },
                { "exposureY", exposureY }
        };

        pushEvent(EventAction::ACTION_SET_FOCUS_POINT, data);
    }

    void CameraSession::setAutoFocus() {
        pushEvent(EventAction::ACTION_SET_AUTO_FOCUS);
    }

    void CameraSession::captureHdr(
        int numImages,
        int baseIso,
        int64_t baseExposure,
        int hdrIso,
        int64_t hdrExposure,
        const PostProcessSettings& postprocessSettings,
        const std::string& outputPath) {

        if(mHdrCaptureInProgress) {
            LOGW("HDR capture already in progress, ignoring request");
            return;
        }

        mHdrCaptureInProgress = true;
        mHdrCaptureOutputPath = outputPath;
        mHdrCaptureSettings = postprocessSettings;

        json11::Json::object data = {
                { "numImages", numImages },
                { "baseIso", baseIso },
                { "baseExposure", std::to_string(baseExposure) },
                { "hdrIso", hdrIso },
                { "hdrExposure", std::to_string(hdrExposure) }
        };

        pushEvent(EventAction::ACTION_CAPTURE_HDR, data);
    }

    void CameraSession::setupPreviewCaptureOutput(CameraCaptureSessionContext& state) {
        ACaptureSessionOutput* sessionOutput = nullptr;
        ACameraOutputTarget* outputTarget = nullptr;

        // Keep the native window
        if (ACaptureSessionOutput_create(state.nativeWindow.get(), &sessionOutput) != ACAMERA_OK)
            throw CameraSessionException("Failed to create preview window session output");

        state.previewSessionOutput = std::shared_ptr<ACaptureSessionOutput>(sessionOutput, ACaptureSessionOutput_free);

        if (ACameraOutputTarget_create(state.nativeWindow.get(), &outputTarget) != ACAMERA_OK)
            throw CameraSessionException("Failed to create preview target");

        state.previewOutputTarget = std::shared_ptr<ACameraOutputTarget>(outputTarget, ACameraOutputTarget_free);

        if (ACaptureSessionOutputContainer_add(state.captureSessionContainer.get(), sessionOutput) != ACAMERA_OK)
            throw CameraSessionException("Failed to add preview output to session container");

        //
        // Don't add capture requests to preview since we're not using it.
        //
    }

    void CameraSession::setupRawCaptureOutput(CameraCaptureSessionContext& state) {
        AImageReader* imageReader = nullptr;

        media_status_t result =
                AImageReader_new(
                        state.outputConfig.outputSize.originalWidth(),
                        state.outputConfig.outputSize.originalHeight(),
                        state.outputConfig.format,
                        MAX_BUFFERED_RAW_IMAGES,
                        &imageReader);

        if (result != AMEDIA_OK) {
            throw CameraSessionException(std::string("Failed to create RAW image reader") + " (" + std::to_string(result) + ")");
        }

        state.rawImageReader = std::shared_ptr<AImageReader>(imageReader, AImageReader_delete);

        state.rawImageListener.context = this;
        state.rawImageListener.onImageAvailable = OnImageAvailable;

        // Set up image listener callback
        AImageReader_setImageListener(state.rawImageReader.get(), &state.rawImageListener);

        // Set up RAW output
        ANativeWindow* nativeWindow = nullptr;
        AImageReader_getWindow(state.rawImageReader.get(), &nativeWindow);

        ACaptureSessionOutput* sessionOutput = nullptr;
        ACameraOutputTarget* outputTarget = nullptr;

        if (ACaptureSessionOutput_create(nativeWindow, &sessionOutput) != ACAMERA_OK)
            throw CameraSessionException("Failed to create raw image reader capture session output");

        state.rawSessionOutput = std::shared_ptr<ACaptureSessionOutput>(sessionOutput, ACaptureSessionOutput_free);

        if (ACameraOutputTarget_create(nativeWindow, &outputTarget) != ACAMERA_OK)
            throw CameraSessionException("Failed to create raw target");

        state.rawOutputTarget = std::shared_ptr<ACameraOutputTarget>(outputTarget, ACameraOutputTarget_free);

        if (ACaptureSessionOutputContainer_add(state.captureSessionContainer.get(), state.rawSessionOutput.get()) != ACAMERA_OK)
            throw CameraSessionException("Failed to add raw session output to container");

        // Add all RAW output captures
        if (ACaptureRequest_addTarget(mSessionContext->repeatCaptureRequest->captureRequest, outputTarget) != ACAMERA_OK)
            throw CameraSessionException("Failed to add RAW output target");

        if (ACaptureRequest_addTarget(mSessionContext->afCaptureRequest->captureRequest, outputTarget) != ACAMERA_OK)
            throw CameraSessionException("Failed to add AF RAW output target");

        for(int i = 0; i < 2; i++)
            if (ACaptureRequest_addTarget(mSessionContext->hdrCaptureRequests[i]->captureRequest, outputTarget) != ACAMERA_OK)
                throw CameraSessionException("Failed to add HDR RAW output target");
    }

    ACaptureRequest* CameraSession::createCaptureRequest() {
        ACaptureRequest* captureRequest = nullptr;

        if(ACameraDevice_createCaptureRequest(mSessionContext->activeCamera.get(), TEMPLATE_PREVIEW, &captureRequest) != ACAMERA_OK)
            throw CameraSessionException("Failed to create capture request");

        const uint8_t captureIntent         = ACAMERA_CONTROL_CAPTURE_INTENT_PREVIEW;
        const uint8_t controlMode           = ACAMERA_CONTROL_MODE_AUTO;
        const uint8_t tonemapMode           = ACAMERA_TONEMAP_MODE_FAST;
        const uint8_t shadingMode           = ACAMERA_SHADING_MODE_OFF;
        const uint8_t lensShadingMapStats   = ACAMERA_STATISTICS_LENS_SHADING_MAP_MODE_ON;
        const uint8_t lensShadingMapApplied = ACAMERA_SENSOR_INFO_LENS_SHADING_APPLIED_FALSE;
        const uint8_t antiBandingMode       = ACAMERA_CONTROL_AE_ANTIBANDING_MODE_AUTO;
        const uint8_t noiseReduction        = ACAMERA_NOISE_REDUCTION_MODE_OFF;

        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_CONTROL_CAPTURE_INTENT, 1, &captureIntent);
        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_CONTROL_MODE, 1, &controlMode);
        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_SHADING_MODE, 1, &shadingMode);
        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_STATISTICS_LENS_SHADING_MAP_MODE, 1, &lensShadingMapStats);
        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_SENSOR_INFO_LENS_SHADING_APPLIED, 1, &lensShadingMapApplied);
        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_CONTROL_AE_ANTIBANDING_MODE, 1, &antiBandingMode);
        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_NOISE_REDUCTION_MODE, 1, &noiseReduction);

        // Enable OIS
        uint8_t omode = ACAMERA_LENS_OPTICAL_STABILIZATION_MODE_ON;

        for(auto& oisMode : mCameraDescription->oisModes) {
            if(oisMode == ACAMERA_LENS_OPTICAL_STABILIZATION_MODE_ON) {
                LOGD("Enabling OIS");
                ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_LENS_OPTICAL_STABILIZATION_MODE, 1, &omode);
                break;
            }
        }

        uint8_t aeMode  = ACAMERA_CONTROL_AE_MODE_ON;
        uint8_t afMode  = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        uint8_t awbMode = ACAMERA_CONTROL_AWB_MODE_AUTO;

        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(captureRequest, ACAMERA_CONTROL_AWB_MODE, 1, &awbMode);

        return captureRequest;
    }

    void CameraSession::doOpenCamera() {
        if(mState != CameraCaptureSessionState::CLOSED) {
            LOGE("Trying to open camera that isn't closed");
            return;
        }

        // Open the camera
        LOGD("Opening camera");
        ACameraDevice* device = nullptr;

        if (ACameraManager_openCamera(
                mSessionContext->cameraManager.get(),
                mCameraDescription->id.c_str(),
                &mSessionContext->deviceStateCallbacks,
                &device) != ACAMERA_OK)
        {
            throw CameraSessionException("Failed to open camera");
        }

        mSessionContext->activeCamera = std::shared_ptr<ACameraDevice>(device, ACameraDevice_close);

        LOGD("Camera has opened");

        // Create output container
        ACaptureSessionOutputContainer* container = nullptr;

        if (ACaptureSessionOutputContainer_create(&container) != ACAMERA_OK)
            throw CameraSessionException("Failed to create session container");

        mSessionContext->captureSessionContainer = std::shared_ptr<ACaptureSessionOutputContainer>(container, ACaptureSessionOutputContainer_free);

        // Create capture request
        mSessionContext->repeatCaptureRequest = std::make_shared<CaptureRequest>(createCaptureRequest(), true);

        // Create HDR requests
        mSessionContext->hdrCaptureRequests[0] = std::make_shared<CaptureRequest>(createCaptureRequest(), false);
        mSessionContext->hdrCaptureRequests[1] = std::make_shared<CaptureRequest>(createCaptureRequest(), false);

        // Create capture request for AF
        mSessionContext->afCaptureRequest = std::make_shared<CaptureRequest>(createCaptureRequest(), true);

        // Set up output for preview
        setupPreviewCaptureOutput(*mSessionContext);

        // Set up output for capture
        setupRawCaptureOutput(*mSessionContext);

        // Finally create and start the session
        ACameraCaptureSession* captureSession = nullptr;

        LOGD("Creating capture session");
        if (ACameraDevice_createCaptureSession(
                mSessionContext->activeCamera.get(),
                mSessionContext->captureSessionContainer.get(),
                &mSessionContext->sessionStateCallbacks,
                &captureSession) != ACAMERA_OK)
        {
            throw CameraSessionException("Failed to create capture session");
        }

        mSessionContext->captureSession =
                std::shared_ptr<ACameraCaptureSession>(captureSession, ACameraCaptureSession_close);

        mImageConsumer->start();

        // Start capture
        LOGD("Starting capture");

        doRepeatCapture();
    }

    void CameraSession::doCloseCamera() {
        // Close capture session
        LOGD("Closing capture session");
        mSessionContext->captureSession = nullptr;

        // Close active camera
        LOGD("Closing camera device");
        mSessionContext->activeCamera = nullptr;

        LOGD("Closing image reader");
        mSessionContext->rawImageReader = nullptr;

        // Free capture request
        if(mSessionContext->previewOutputTarget && mSessionContext->repeatCaptureRequest->isPreviewOutput)
            ACaptureRequest_removeTarget(mSessionContext->repeatCaptureRequest->captureRequest, mSessionContext->previewOutputTarget.get());

        if(mSessionContext->rawOutputTarget)
            ACaptureRequest_removeTarget(mSessionContext->repeatCaptureRequest->captureRequest, mSessionContext->rawOutputTarget.get());

        mSessionContext->previewOutputTarget    = nullptr;
        mSessionContext->rawOutputTarget        = nullptr;

        // Clear session container
        if(mSessionContext->captureSessionContainer) {
            if(mSessionContext->previewSessionOutput)
                ACaptureSessionOutputContainer_remove(mSessionContext->captureSessionContainer.get(), mSessionContext->previewSessionOutput.get());

            if(mSessionContext->rawSessionOutput)
                ACaptureSessionOutputContainer_remove(mSessionContext->captureSessionContainer.get(), mSessionContext->rawSessionOutput.get());
        }

        mSessionContext->captureSessionContainer    = nullptr;
        mSessionContext->previewSessionOutput       = nullptr;
        mSessionContext->rawSessionOutput           = nullptr;
        mSessionContext->nativeWindow               = nullptr;

        // Stop image consumer
        LOGD("Stopping image consumer");
        mImageConsumer->stop();

        mIsPaused = false;
    }

    bool CameraSession::doRepeatCapture() {
        if(mMode == CameraMode::AUTO) {
            uint8_t aeMode  = ACAMERA_CONTROL_AE_MODE_ON;

            // Auto exposure on
            ACaptureRequest_setEntry_u8(mSessionContext->repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);

            // Set exposure compensation
            ACaptureRequest_setEntry_i32(mSessionContext->repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &mExposureCompensation);
        }
        else if(mMode == CameraMode::MANUAL) {
            uint8_t aeMode  = ACAMERA_CONTROL_AE_MODE_OFF;

            ACaptureRequest_setEntry_u8(mSessionContext->repeatCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
            ACaptureRequest_setEntry_i32(mSessionContext->repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_SENSITIVITY, 1, &mUserIso);
            ACaptureRequest_setEntry_i64(mSessionContext->repeatCaptureRequest->captureRequest, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &mUserExposureTime);
        }

        return ACameraCaptureSession_setRepeatingRequest(
            mSessionContext->captureSession.get(),
            &mSessionContext->captureCallbacks[CaptureEvent::REPEAT]->callbacks,
            1,
            &mSessionContext->repeatCaptureRequest->captureRequest,
            &mSessionContext->captureCallbacks[CaptureEvent::REPEAT]->sequenceId) == ACAMERA_OK;
    }

    void CameraSession::doPauseCapture() {
        if(mIsPaused) {
            LOGW("Cannot pause capture, invalid state.");
            return;
        }

        // Stop capture if we are active
        if (mState == CameraCaptureSessionState::ACTIVE) {
            ACameraCaptureSession_stopRepeating(mSessionContext->captureSession.get());
            mIsPaused = true;

            mImageConsumer->lockBuffers();
        }
    }

    void CameraSession::doResumeCapture() {
        if(!mIsPaused) {
            LOGW("Cannot resume capture, invalid state.");
            return;
        }

        if (mState == CameraCaptureSessionState::READY) {
            doRepeatCapture();

            mIsPaused = false;
            mImageConsumer->unlockBuffers();
        }
    }

    void CameraSession::doSetAutoExposure() {
        if (mIsPaused) {
            LOGW("Cannot set auto exposure, invalid state");
            return;
        }

        if (mState == CameraCaptureSessionState::ACTIVE) {
            mMode = CameraMode::AUTO;
            mExposureCompensation = 0;

            doRepeatCapture();
        }
    }

    void CameraSession::doSetManualExposure(int32_t iso, int64_t exposureTime) {
        if (mIsPaused || mState != CameraCaptureSessionState::ACTIVE) {
            LOGW("Cannot set manual exposure, invalid state");
            return;
        }

        if (mState == CameraCaptureSessionState::ACTIVE) {
            mMode = CameraMode::MANUAL;
            mExposureCompensation = 0;
            mUserIso = iso;
            mUserExposureTime = exposureTime;

            doRepeatCapture();
        }
    }

    void CameraSession::doSetFocusPoint(double focusX, double focusY, double exposureX, double exposureY) {
        if(mIsPaused || mState != CameraCaptureSessionState::ACTIVE) {
            LOGW("Cannot set focus, invalid state");
            return;
        }

        // Need at least one AE region
        if(mCameraDescription->maxAfRegions <= 0) {
            LOGI("Can't set focus, zero AF regions");
            return;
        }

        // Stop existing capture
        ACameraCaptureSession_stopRepeating(mSessionContext->captureSession.get());

        uint8_t afMode      = ACAMERA_CONTROL_AF_MODE_AUTO;
        uint8_t afTrigger   = ACAMERA_CONTROL_AF_TRIGGER_START;

        ACaptureRequest_setEntry_u8(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);

        // Set the focus region

        // Clamp between 0 and 1
        focusX = std::max(0.0, std::min(1.0, focusX));
        focusY = std::max(0.0, std::min(1.0, focusY));

        int w = static_cast<int>(static_cast<float>(mCameraDescription->sensorSize[2]) * 0.02f);
        int h = static_cast<int>(static_cast<float>(mCameraDescription->sensorSize[3]) * 0.02f);

        int px = static_cast<int>(static_cast<float>(mCameraDescription->sensorSize[0] + mCameraDescription->sensorSize[2]) * focusX);
        int py = static_cast<int>(static_cast<float>(mCameraDescription->sensorSize[1] + mCameraDescription->sensorSize[3]) * focusY);

        int32_t afRegion[5] = { px - w / 2, py - h / 2,
                                px + w / 2, py + h / 2,
                                500 };

        afRegion[0] = std::max(mCameraDescription->sensorSize[0], afRegion[0]);
        afRegion[1] = std::max(mCameraDescription->sensorSize[1], afRegion[1]);

        afRegion[2] = std::min(mCameraDescription->sensorSize[2] - 1, afRegion[2]);
        afRegion[3] = std::min(mCameraDescription->sensorSize[3] - 1, afRegion[3]);

        ACaptureRequest_setEntry_i32(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afRegion[0]);

        // Set auto exposure region if supported
        if(mCameraDescription->maxAeRegions > 0) {
            uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_START;

            exposureX = std::max(0.0, std::min(1.0, exposureX));
            exposureY = std::max(0.0, std::min(1.0, exposureY));

            int sx = static_cast<int>((mCameraDescription->sensorSize[0] + mCameraDescription->sensorSize[2]) * exposureX);
            int sy = static_cast<int>((mCameraDescription->sensorSize[1] + mCameraDescription->sensorSize[3]) * exposureY);

            int32_t aeRegion[5] = { (int) (sx - w / 2), (int) (sy - h / 2),
                                    (int) (sx + w / 2), (int) (sy + h / 2),
                                    1000 };

            aeRegion[0] = std::max(mCameraDescription->sensorSize[0], aeRegion[0]);
            aeRegion[1] = std::max(mCameraDescription->sensorSize[1], aeRegion[1]);

            aeRegion[2] = std::min(mCameraDescription->sensorSize[2] - 1, aeRegion[2]);
            aeRegion[3] = std::min(mCameraDescription->sensorSize[3] - 1, aeRegion[3]);

            ACaptureRequest_setEntry_i32(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 5, &aeRegion[0]);
            ACaptureRequest_setEntry_u8(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);
        }

        if (ACameraCaptureSession_capture(
                mSessionContext->captureSession.get(),
                &mSessionContext->captureCallbacks[CaptureEvent::TRIGGER_AF]->callbacks,
                1,
                &mSessionContext->afCaptureRequest->captureRequest,
                &mSessionContext->captureCallbacks[CaptureEvent::TRIGGER_AF]->sequenceId) != ACAMERA_OK)
        {
            throw CameraSessionException("Failed to set auto focus point");
        }
    }

    void CameraSession::doSetAutoFocus() {
        if(mIsPaused || mState != CameraCaptureSessionState::ACTIVE) {
            LOGW("Cannot set auto focus, invalid state");
            return;
        }

        uint8_t afMode      = ACAMERA_CONTROL_AF_MODE_OFF;
        uint8_t afTrigger   = ACAMERA_CONTROL_AF_TRIGGER_CANCEL;
        uint8_t aeTrigger   = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_CANCEL;

        int32_t afAeRegion[5] = { 0, 0, 0, 0 };

        ACaptureRequest_setEntry_u8(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_MODE, 1, &afMode);
        ACaptureRequest_setEntry_u8(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
        ACaptureRequest_setEntry_u8(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);
        ACaptureRequest_setEntry_i32(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_REGIONS, 5, &afAeRegion[0]);
        ACaptureRequest_setEntry_i32(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_REGIONS, 5, &afAeRegion[0]);

        // Stop existing capture
        ACameraCaptureSession_stopRepeating(mSessionContext->captureSession.get());

        // Cancel AF/AE
        if (ACameraCaptureSession_capture(
                mSessionContext->captureSession.get(),
                &mSessionContext->captureCallbacks[CaptureEvent::CANCEL_AF]->callbacks,
                1,
                &mSessionContext->afCaptureRequest->captureRequest,
                &mSessionContext->captureCallbacks[CaptureEvent::CANCEL_AF]->sequenceId) != ACAMERA_OK)
        {
            throw CameraSessionException("Failed to cancel auto focus");
        }
    }

    void CameraSession::doCaptureHdr(int numImages, int baseIso, int64_t baseExposure, int hdrIso, int64_t hdrExposure) {
        if(numImages < 1) {
            LOGE("Invalid HDR capture requested (numImages < 1)");
            return;
        }

        uint8_t aeMode  = ACAMERA_CONTROL_AE_MODE_OFF;

        ACaptureRequest_setEntry_u8(mSessionContext->hdrCaptureRequests[0]->captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
        ACaptureRequest_setEntry_i32(mSessionContext->hdrCaptureRequests[0]->captureRequest, ACAMERA_SENSOR_SENSITIVITY, 1, &baseIso);
        ACaptureRequest_setEntry_i64(mSessionContext->hdrCaptureRequests[0]->captureRequest, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &baseExposure);

        ACaptureRequest_setEntry_u8(mSessionContext->hdrCaptureRequests[1]->captureRequest, ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
        ACaptureRequest_setEntry_i32(mSessionContext->hdrCaptureRequests[1]->captureRequest, ACAMERA_SENSOR_SENSITIVITY, 1, &hdrIso);
        ACaptureRequest_setEntry_i64(mSessionContext->hdrCaptureRequests[1]->captureRequest, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &hdrExposure);

        // Allocate enough for numImages + 1 underexposed images
        numImages = numImages + 1;
        std::vector<ACaptureRequest*> captureRequests(numImages);

        // Set up list of capture requests
        for(int i = 0; i < numImages; i++)
            captureRequests[i] = mSessionContext->hdrCaptureRequests[0]->captureRequest;

        // Interleave underexposed requests
        captureRequests[0] = mSessionContext->hdrCaptureRequests[1]->captureRequest;

        LOGI("Initiating HDR capture (numImages=%d, baseIso=%d, baseExposure=%ld, hdrIso=%d, hdrExposure=%ld)",
                numImages, baseIso, baseExposure, hdrIso, hdrExposure);

        // Set the number of requested captured we need
        mRequestedHdrCaptures = numImages;

        ACameraCaptureSession_capture(
            mSessionContext->captureSession.get(),
            &mSessionContext->captureCallbacks[CaptureEvent::HDR_CAPTURE]->callbacks,
            captureRequests.size(),
            captureRequests.data(),
            &mSessionContext->captureCallbacks[CaptureEvent::HDR_CAPTURE]->sequenceId);
    }

    void CameraSession::doAttemptSaveHdrData(int attempt) {

        int numHdrImages = mImageConsumer->getHdrBufferCount();

        // If we don't have the right number of images
        if(mImageConsumer->getHdrBufferCount() < mRequestedHdrCaptures) {
            LOGI("%d. Expected %d but got %d HDR images. Trying again.", attempt, mRequestedHdrCaptures, numHdrImages);
            return;
        }

        // Save HDR capture
        mHdrCaptureInProgress = false;

        LOGI("HDR capture completed. Saving data.");
        mImageConsumer->save(RawType::HDR, mHdrCaptureSettings, mHdrCaptureOutputPath);

        mSessionListener->onCameraHdrImageCaptureCompleted();
    }

    void CameraSession::doSetExposureCompensation(float value) {
        value = std::min(1.0f, std::max(0.0f, value));

        double range = mCameraDescription->exposureCompensationRange[1] - mCameraDescription->exposureCompensationRange[0];
        int exposureComp = static_cast<int>(value*range + mCameraDescription->exposureCompensationRange[0]);

        if(mExposureCompensation == exposureComp)
            return;

        mExposureCompensation = exposureComp;

        doRepeatCapture();
    }

    void CameraSession::updateOrientation(ScreenOrientation orientation) {
        mScreenOrientation = orientation;
    }

    std::shared_ptr<CaptureCallbackContext> CameraSession::createCaptureCallbacks(const CaptureEvent event) {
        std::shared_ptr<CaptureCallbackContext> context = std::make_shared<CaptureCallbackContext>();

        context->cameraSession = this;
        context->event = event;

        context->callbacks.context                      = static_cast<void*>(context.get());

        context->callbacks.onCaptureStarted             = OnCameraCaptureStarted;
        context->callbacks.onCaptureCompleted           = OnCameraCaptureCompleted;
        context->callbacks.onCaptureFailed              = OnCameraCaptureFailed;
        context->callbacks.onCaptureProgressed          = OnCameraCaptureProgressed;
        context->callbacks.onCaptureBufferLost          = OnCameraCaptureBufferLost;
        context->callbacks.onCaptureSequenceCompleted   = OnCameraCaptureSequenceCompleted;
        context->callbacks.onCaptureSequenceAborted     = OnCameraCaptureSequenceAborted;

        return context;
    }

    void CameraSession::setupCallbacks() {
        mSessionContext->deviceStateCallbacks.context                  = this;

        mSessionContext->deviceStateCallbacks.onError                  = OnCameraError;
        mSessionContext->deviceStateCallbacks.onDisconnected           = OnCameraDisconnected;

        mSessionContext->sessionStateCallbacks.context                 = this;

        mSessionContext->sessionStateCallbacks.onActive                = OnCameraSessionActive;
        mSessionContext->sessionStateCallbacks.onReady                 = OnCameraSessionReady;
        mSessionContext->sessionStateCallbacks.onClosed                = OnCameraSessionClosed;

        mSessionContext->captureCallbacks[CaptureEvent::REPEAT]     = createCaptureCallbacks(CaptureEvent::REPEAT);
        mSessionContext->captureCallbacks[CaptureEvent::CANCEL_AF]  = createCaptureCallbacks(CaptureEvent::CANCEL_AF);
        mSessionContext->captureCallbacks[CaptureEvent::TRIGGER_AF] = createCaptureCallbacks(CaptureEvent::TRIGGER_AF);
        mSessionContext->captureCallbacks[CaptureEvent::HDR_CAPTURE] = createCaptureCallbacks(CaptureEvent::HDR_CAPTURE);
    }

    void CameraSession::onCameraCaptureStarted(const CaptureCallbackContext& context, const ACaptureRequest* request, int64_t timestamp) {
    }

    void CameraSession::onCameraCaptureCompleted(const CaptureCallbackContext& context, const ACameraMetadata* metadata) {
        ACameraMetadata_const_entry metadataEntry;

        if(context.event == CaptureEvent::REPEAT) {
            mImageConsumer->queueMetadata(metadata, mScreenOrientation, RawType::ZSL);
        }
        else if(context.event == CaptureEvent::HDR_CAPTURE) {
            mImageConsumer->queueMetadata(metadata, mScreenOrientation, RawType::HDR);
        }
        else if(context.event == CaptureEvent::CANCEL_AF) {
            doRepeatCapture();
        }
        else if(context.event == CaptureEvent::TRIGGER_AF) {
            uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;
            uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;

            ACaptureRequest_setEntry_u8(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);
            ACaptureRequest_setEntry_u8(mSessionContext->afCaptureRequest->captureRequest, ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);

            camera_status_t result = ACameraCaptureSession_setRepeatingRequest(
                    mSessionContext->captureSession.get(),
                    &mSessionContext->captureCallbacks[CaptureEvent::REPEAT]->callbacks,
                    1,
                    &mSessionContext->afCaptureRequest->captureRequest,
                    &mSessionContext->captureCallbacks[CaptureEvent::REPEAT]->sequenceId);

            if(result != ACAMERA_OK) {
                LOGW("Failed to trigger AF");
            }
        }

        // Read the ISO/shutter speed values.
        int iso = 0;
        int64_t exposure = 0;

        if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_SENSITIVITY, &metadataEntry) == ACAMERA_OK) {
            iso = metadataEntry.data.i32[0];
        }

        if (ACameraMetadata_getConstEntry(metadata, ACAMERA_SENSOR_EXPOSURE_TIME, &metadataEntry) == ACAMERA_OK) {
            exposure = (double) metadataEntry.data.i64[0];
        }

        if (iso != mLastIso || exposure != mLastExposureTime) {
            json11::Json::object data = {
                    {"iso",          iso},
                    {"exposureTime", std::to_string(exposure)}
            };

            pushEvent(EventAction::EVENT_CAMERA_EXPOSURE_STATUS_CHANGED, data);

            mLastIso = iso;
            mLastExposureTime = exposure;
        }

        // Focus state
        if (ACameraMetadata_getConstEntry(metadata, ACAMERA_CONTROL_AF_STATE, &metadataEntry) == ACAMERA_OK) {
            auto afState = static_cast<acamera_metadata_enum_android_control_af_state_t>(metadataEntry.data.u8[0]);

            auto focusState = GetFocusState(afState);
            if(focusState != mLastFocusState) {
                json11::Json::object data = {
                        { "state", static_cast<int>(focusState) }
                };

                pushEvent(EventAction::EVENT_CAMERA_AUTO_FOCUS_STATE_CHANGED, data);
            }

            mLastFocusState = focusState;
        }

        // Exposure state
        if (ACameraMetadata_getConstEntry(metadata, ACAMERA_CONTROL_AE_STATE, &metadataEntry) == ACAMERA_OK) {
            auto aeState = static_cast<acamera_metadata_enum_android_control_ae_state_t>(metadataEntry.data.u8[0]);
            auto exposureState = GetExposureState(aeState);

            if(exposureState != mLastExposureState) {
                json11::Json::object data = {
                        { "state", static_cast<int>(exposureState) }
                };

                pushEvent(EventAction::EVENT_CAMERA_AUTO_EXPOSURE_STATE_CHANGED, data);
            }

            mLastExposureState = exposureState;
        }
    }

    void CameraSession::onCameraCaptureProgressed(const CaptureCallbackContext& context, const ACameraMetadata* result) {
    }

    void CameraSession::onCameraCaptureBufferLost(const CaptureCallbackContext& context, int64_t frameNumber) {
    }

    void CameraSession::onCameraCaptureFailed(const CaptureCallbackContext& context, ACameraCaptureFailure* failure) {
//        // Resume capture if auto focus trigger failed
//        if(context.event == CaptureEvent::TRIGGER_AF) {
//            uint8_t afMode = ACAMERA_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
//            uint8_t aeMode = ACAMERA_CONTROL_AE_MODE_ON;
//
//            uint8_t afTrigger = ACAMERA_CONTROL_AF_TRIGGER_IDLE;
//            uint8_t aeTrigger = ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
//
//            ACaptureRequest_setEntry_u8(mSessionContext->captureRequest.get(), ACAMERA_CONTROL_AF_MODE, 1, &afMode);
//            ACaptureRequest_setEntry_u8(mSessionContext->captureRequest.get(), ACAMERA_CONTROL_AE_MODE, 1, &aeMode);
//            ACaptureRequest_setEntry_u8(mSessionContext->captureRequest.get(), ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER, 1, &aeTrigger);
//            ACaptureRequest_setEntry_u8(mSessionContext->captureRequest.get(), ACAMERA_CONTROL_AF_TRIGGER, 1, &afTrigger);
//
//            if(doRepeatCapture()) {
//                json11::Json::object data = { { "error",  "AF failure" } };
//                pushEvent(EventAction::EVENT_INTERNAL_ERROR, data);
//            }
//        }
    }

    void CameraSession::onCameraCaptureSequenceCompleted(const CaptureCallbackContext& context, const int sequenceId) {
        if(context.event == CaptureEvent::HDR_CAPTURE) {
            LOGI("HDR capture sequence completed");

            json11::Json::object data = {
                    { "attempt", 0 }
            };

            pushEvent(EventAction::EVENT_SAVE_HDR_DATA, data);
        }
    }

    void CameraSession::onCameraCaptureSequenceAborted(const CaptureCallbackContext& context, int sequenceId) {
        if(context.event == CaptureEvent::HDR_CAPTURE) {
            LOGI("HDR capture sequence aborted");
            mHdrCaptureInProgress = false;
        }
    }

    void CameraSession::onCameraError(int error) {
        LOGE("Camera has failed with error %d", error);
        json11::Json::object data = {
                { "error", error }
        };

        pushEvent(EventAction::EVENT_CAMERA_ERROR, data);
    }

    void CameraSession::onCameraDisconnected() {
        pushEvent(EventAction::EVENT_CAMERA_DISCONNECTED);
    }

    void CameraSession::onCameraSessionStateActive() {
        json11::Json::object data = {
                { "state", static_cast<int>(CameraCaptureSessionState::ACTIVE) }
        };

        pushEvent(EventAction::EVENT_CAMERA_SESSION_CHANGED, data);
    }

    void CameraSession::onCameraSessionStateReady() {
        json11::Json::object data = {
                { "state", static_cast<int>(CameraCaptureSessionState::READY) }
        };

        pushEvent(EventAction::EVENT_CAMERA_SESSION_CHANGED, data);
    }

    void CameraSession::onCameraSessionStateClosed() {
        json11::Json::object data = {
                { "state", static_cast<int>(CameraCaptureSessionState::CLOSED) }
        };

        pushEvent(EventAction::EVENT_CAMERA_SESSION_CHANGED, data);
    }

    //

    void CameraSession::onRawImageAvailable(AImageReader *imageReader) {
        AImage *image = nullptr;

        while (AImageReader_acquireLatestImage(imageReader, &image) == AMEDIA_OK) {
            mImageConsumer->queueImage(image);
        }

        if(mHdrCaptureInProgress) {
            pushEvent(EventAction::EVENT_SAVE_HDR_DATA);
        }
    }

    //

    void CameraSession::doOnCameraError(int error) {
        LOGE("Camera has encountered an error (%d)", error);

        mSessionListener->onCameraError(error);
    }

    void CameraSession::doOnCameraDisconnected() {
        LOGI("Camera has disconnected");

        mSessionListener->onCameraDisconnected();
    }

    void CameraSession::doOnCameraSessionStateChanged(const CameraCaptureSessionState state) {
        LOGD("Camera session has changed state (%d)", state);

        mState = state;
        mSessionListener->onCameraStateChanged(mState);
    }

    void CameraSession::doOnCameraExposureStatusChanged(int32_t iso, int64_t exposureTime) {
        mSessionListener->onCameraExposureStatus(iso, exposureTime);
    }

    void CameraSession::doCameraAutoExposureStateChanged(CameraExposureState state) {
        mSessionListener->onCameraAutoExposureStateChanged(state);
    }

    void CameraSession::doCameraAutoFocusStateChanged(CameraFocusState state) {
        mSessionListener->onCameraAutoFocusStateChanged(state);
    }

    void CameraSession::doOnInternalError(const std::string& e) {
        LOGE("Internal error: %s", e.c_str());
        pushEvent(EventAction::ACTION_CLOSE_CAMERA);
    }

    //

    void CameraSession::pushEvent(const EventAction eventAction, const json11::Json& data) {
        if(!mSessionContext) {
            LOGW("Failed to queue event, event loop is gone (%d)", eventAction);
            return;
        }

        auto eventLoopData = std::make_shared<EventLoopData>(eventAction, data);

        mSessionContext->eventLoopQueue.enqueue(eventLoopData);
    }

    void CameraSession::pushEvent(const EventAction eventAction) {
        if(!mSessionContext) {
            LOGW("Failed to queue event, event loop is gone (%d)", eventAction);
            return;
        }

        json11::Json data;
        auto eventLoopData = std::make_shared<EventLoopData>(eventAction, data);

        mSessionContext->eventLoopQueue.enqueue(eventLoopData);
    }

    void CameraSession::doProcessEvent(const EventLoopDataPtr& eventLoopData) {
        switch(eventLoopData->eventAction) {
            //
            // Actions
            //

            case EventAction::ACTION_OPEN_CAMERA: {
                doOpenCamera();
                break;
            }

            case EventAction::ACTION_CLOSE_CAMERA: {
                doCloseCamera();
                break;
            }

            case EventAction::ACTION_PAUSE_CAPTURE: {
                doPauseCapture();
                break;
            }

            case EventAction::ACTION_RESUME_CAPTURE: {
                doResumeCapture();
                break;
            }

            case EventAction::ACTION_SET_AUTO_EXPOSURE: {
                doSetAutoExposure();
                break;
            }

            case EventAction::ACTION_SET_EXPOSURE_COMP_VALUE: {
                float expComp = static_cast<float>(eventLoopData->data["value"].number_value());
                doSetExposureCompensation(expComp);
                break;
            }

            case EventAction::ACTION_SET_MANUAL_EXPOSURE: {
                auto exposureTimeStr = eventLoopData->data["exposureTime"].string_value();
                auto exposureTime = std::stol(exposureTimeStr);

                doSetManualExposure(eventLoopData->data["iso"].int_value(), exposureTime);
                break;
            }

            case EventAction::ACTION_SET_AUTO_FOCUS: {
                doSetAutoFocus();
                break;
            }

            case EventAction::ACTION_CAPTURE_HDR: {
                int numImages = eventLoopData->data["numImages"].int_value();
                int baseIso = eventLoopData->data["baseIso"].int_value();
                int64_t baseExposure = std::stol(eventLoopData->data["baseExposure"].string_value());
                int hdrIso = eventLoopData->data["hdrIso"].int_value();
                int64_t hdrExposure = std::stol(eventLoopData->data["hdrExposure"].string_value());

                doCaptureHdr(numImages, baseIso, baseExposure, hdrIso, hdrExposure);
                break;
            }

            case EventAction::ACTION_SET_FOCUS_POINT: {
                doSetFocusPoint(
                        eventLoopData->data["focusX"].number_value(),
                        eventLoopData->data["focusY"].number_value(),
                        eventLoopData->data["exposureX"].number_value(),
                        eventLoopData->data["exposureY"].number_value());
                break;
            }

            //
            // Events
            //

            case EventAction::EVENT_SAVE_HDR_DATA: {
                doAttemptSaveHdrData(0);
                break;
            }

            case EventAction::EVENT_CAMERA_ERROR: {
                doOnCameraError(eventLoopData->data["error"].int_value());
                break;
            }

            case EventAction::EVENT_CAMERA_DISCONNECTED: {
                doOnCameraDisconnected();
                break;
            }

            case EventAction::EVENT_CAMERA_SESSION_CHANGED: {
                doOnCameraSessionStateChanged(static_cast<CameraCaptureSessionState>(eventLoopData->data["state"].int_value()));
                break;
            }

            case EventAction::EVENT_CAMERA_EXPOSURE_STATUS_CHANGED: {
                auto iso = eventLoopData->data["iso"].int_value();
                auto exposureTimeStr = eventLoopData->data["exposureTime"].string_value();
                auto exposureTime = std::stol(exposureTimeStr);

                doOnCameraExposureStatusChanged(iso, exposureTime);
                break;
            }

            case EventAction::EVENT_CAMERA_AUTO_EXPOSURE_STATE_CHANGED: {
                doCameraAutoExposureStateChanged(static_cast<CameraExposureState>(eventLoopData->data["state"].int_value()));
                break;
            }

            case EventAction::EVENT_CAMERA_AUTO_FOCUS_STATE_CHANGED: {
                doCameraAutoFocusStateChanged(static_cast<CameraFocusState>(eventLoopData->data["state"].int_value()));
                break;
            }

            default:
                LOGW("Unknown event in event loop!");
                break;
        }
    }

    void CameraSession::doEventLoop() {
        bool eventLoopRunning = true;
        bool recvdStop = false;

        while(eventLoopRunning) {
            EventLoopDataPtr eventLoopData;

            // Try to get event
            if(!mSessionContext->eventLoopQueue.wait_dequeue_timed(eventLoopData, std::chrono::milliseconds(100))) {
                // Stop when the camera session has closed and we've received a STOP message
                // We need to wait until the camera has closed otherwise the callback may be called after we've been
                // destroyed.
                if(recvdStop && mState == CameraCaptureSessionState::CLOSED)
                    eventLoopRunning = false;

                continue;
            }

            if(eventLoopData->eventAction == EventAction::STOP) {
                recvdStop = true;
            }
            else {
                try {
                    doProcessEvent(eventLoopData);
                }
                catch (std::exception &e) {
                    doOnInternalError(e.what());
                }
            }
        }
    }
 }
