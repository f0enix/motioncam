#include "NativeCameraBridgeListener.h"
#include "JavaUtils.h"

#include "camera/Logger.h"

namespace motioncam {
    NativeCameraBridgeListener::NativeCameraBridgeListener(JNIEnv* env, jobject listener) :
        mJavaVm(nullptr) {

        if (env->GetJavaVM(&mJavaVm) != 0) {
            LOGE("Failed to get Java VM!");
            throw std::runtime_error("Failed to obtain java vm");
        }

        mListenerInstance = env->NewGlobalRef(listener);
        if(!mListenerInstance)
            throw std::runtime_error("Failed to get listener instance reference");

        jobject listenerClass = env->GetObjectClass(mListenerInstance);
        if(!listenerClass)
            throw std::runtime_error("Failed to get listener class");

        mListenerClass = reinterpret_cast<jclass>(env->NewGlobalRef(listenerClass));
        if(!mListenerClass)
            throw std::runtime_error("Failed to get listener class reference");
    }

    NativeCameraBridgeListener::~NativeCameraBridgeListener() {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("~NativeRawPreviewListener() no environment");
            return;
        }

        if(mListenerClass)
            env.getEnv()->DeleteGlobalRef(mListenerClass);

        if(mListenerInstance)
            env.getEnv()->DeleteGlobalRef(mListenerInstance);
    }

    void NativeCameraBridgeListener::onCameraStateChanged(const CameraCaptureSessionState state) {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onCameraSessionStateChanged()");
            return;
        }

        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onCameraSessionStateChanged", "(I)V");
        if(callbackMethod)
            env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod, (int) state);
    }

    void NativeCameraBridgeListener::onCameraError(int error) {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onCameraError()");
            return;
        }

        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onCameraError", "(I)V");
        if(callbackMethod)
            env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod, error);
    }

    void NativeCameraBridgeListener::onCameraDisconnected() {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onCameraDisconnected()");
            return;
        }

        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onCameraDisconnected", "()V");
        if(callbackMethod)
            env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod);
    }

    void NativeCameraBridgeListener::onCameraExposureStatus(const int32_t iso, const int64_t exposureTime) {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onCameraExposureStatus()");
            return;
        }

        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onCameraExposureStatus", "(IJ)V");
        if(callbackMethod)
            env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod, iso, exposureTime);
    }

    void NativeCameraBridgeListener::onCameraAutoFocusStateChanged(const CameraFocusState state) {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onCameraAutoFocusStateChanged()");
            return;
        }

        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onCameraAutoFocusStateChanged", "(I)V");
        if(callbackMethod)
            env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod, (int) state);
    }

    void NativeCameraBridgeListener::onCameraAutoExposureStateChanged(const CameraExposureState state) {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onCameraAutoExposureStateChanged()");
            return;
        }

        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onCameraAutoExposureStateChanged", "(I)V");
        if(callbackMethod)
            env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod, (int) state);
    }

    void NativeCameraBridgeListener::onCameraHdrImageCaptureProgress() {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onCameraHdrImageCaptureProgress()");
            return;
        }

        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onCameraHdrImageCaptureProgress", "()V");
        if(callbackMethod)
            env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod);
    }

    void NativeCameraBridgeListener::onCameraHdrImageCaptureCompleted() {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onCameraHdrImageCaptureCompleted()");
            return;
        }

        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onCameraHdrImageCaptureCompleted", "()V");
        if(callbackMethod)
            env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod);
    }
}