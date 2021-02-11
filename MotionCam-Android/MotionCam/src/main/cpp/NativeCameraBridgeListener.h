#ifndef MOTIONCAM_ANDROID_NATIVECAMERABRIDGELISTENER_H
#define MOTIONCAM_ANDROID_NATIVECAMERABRIDGELISTENER_H

#include <jni.h>
#include "camera/CameraSessionListener.h"

namespace motioncam {
    class NativeCameraBridgeListener : public CameraSessionListener {
    public:
        NativeCameraBridgeListener(JNIEnv *env, jobject listener);
        ~NativeCameraBridgeListener();

        void onCameraStateChanged(const CameraCaptureSessionState state);
        void onCameraError(int error);
        void onCameraDisconnected();
        void onCameraExposureStatus(const int32_t iso, const int64_t exposureTime);
        void onCameraAutoFocusStateChanged(const CameraFocusState state);
        void onCameraAutoExposureStateChanged(const CameraExposureState state);
        void onCameraHdrImageCaptureProgress(int progress);
        void onCameraHdrImageCaptureCompleted();
        void onCameraHdrImageCaptureFailed();

    private:
        JavaVM *mJavaVm;
        jobject mListenerInstance;
        jclass mListenerClass;
    };
}

#endif //MOTIONCAM_ANDROID_NATIVECAMERABRIDGELISTENER_H
