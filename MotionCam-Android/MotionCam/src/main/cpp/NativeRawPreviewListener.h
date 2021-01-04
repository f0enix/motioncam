#ifndef MOTIONCAM_ANDROID_NATIVERAWPREVIEWLISTENER_H
#define MOTIONCAM_ANDROID_NATIVERAWPREVIEWLISTENER_H

#include <jni.h>
#include <android/bitmap.h>

#include "camera/RawPreviewListener.h"

namespace motioncam {
    class NativeRawPreviewListener : public RawPreviewListener {

    public:
        NativeRawPreviewListener(JNIEnv *env, jobject listener);
        ~NativeRawPreviewListener();

        void onPreviewGenerated(const void* data, const int len, const int width, const int height);

    private:
        JavaVM *mJavaVm;
        jobject mListenerInstance;
        jclass mListenerClass;
        jobject mBitmap;
    };

} // namespace motioncam

#endif //MOTIONCAM_ANDROID_NATIVERAWPREVIEWLISTENER_H
