#ifndef MOTIONCAM_ANDROID_CAMERASESSIONCONTEXT_H
#define MOTIONCAM_ANDROID_CAMERASESSIONCONTEXT_H

#include "CameraDescription.h"

#include <memory>
#include <map>

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>

namespace motioncam {
    class CameraSession;

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

    struct CaptureCallbackContext {
        ACameraCaptureSession_captureCallbacks callbacks;
        CaptureEvent event;
        CameraSession* cameraSession;
        int sequenceId;
    };

    struct CameraCaptureSessionContext {
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

        std::shared_ptr<CaptureRequest> repeatCaptureRequest;
        std::shared_ptr<CaptureRequest> hdrCaptureRequests[2];

        std::shared_ptr<ACaptureSessionOutput> previewSessionOutput;
        std::shared_ptr<ACameraOutputTarget> previewOutputTarget;

        std::shared_ptr<ACaptureSessionOutput> rawSessionOutput;
        std::shared_ptr<ACameraOutputTarget> rawOutputTarget;

        std::shared_ptr<ACaptureSessionOutput> jpegSessionOutput;
        std::shared_ptr<ACameraOutputTarget> jpegOutputTarget;

        // Image reader
        std::shared_ptr<AImageReader> rawImageReader;
        std::shared_ptr<AImageReader> jpegImageReader;

        AImageReader_ImageListener rawImageListener;
    };
}

#endif //MOTIONCAM_ANDROID_CAMERASESSIONCONTEXT_H
