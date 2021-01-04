#ifndef MOTIONCAM_ANDROID_JAVAUTILS_H
#define MOTIONCAM_ANDROID_JAVAUTILS_H

#include <jni.h>

namespace motioncam {
    class JavaEnv {
    public:
        JavaEnv(JavaVM* javaVm);
        ~JavaEnv();

        JNIEnv* getEnv() const {
            return mJniEnv;
        }

    private:
        JavaVM* mVm;
        JNIEnv* mJniEnv;
        bool mShouldDetach;
    };
}

#endif //MOTIONCAM_ANDROID_JAVAUTILS_H
