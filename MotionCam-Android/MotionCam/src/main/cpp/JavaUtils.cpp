#include "JavaUtils.h"
#include "camera/Logger.h"

namespace {
    JNIEnv *GetEnv(JavaVM* javaVm, bool& needDetach) {
        JNIEnv *env = nullptr;
        jint envStatus = javaVm->GetEnv((void **) &env, JNI_VERSION_1_6);
        needDetach = false;
        if (envStatus == JNI_EDETACHED) {
            needDetach = true;

            if (javaVm->AttachCurrentThread(&env, nullptr) != 0) {
                LOGE("Failed to attach to thread.");
                return nullptr;
            }
        }

        return env;
    }
}

namespace motioncam {
    JavaEnv::JavaEnv(JavaVM* javaVm) : mVm(javaVm), mShouldDetach(false)
    {
        mJniEnv = GetEnv(javaVm, mShouldDetach);
    }

    JavaEnv::~JavaEnv()
    {
        if(mShouldDetach)
            mVm->DetachCurrentThread();
    }
}
