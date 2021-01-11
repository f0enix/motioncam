#include "NativeRawPreviewListener.h"

#include "camera/Logger.h"
#include "JavaUtils.h"

namespace motioncam {

    NativeRawPreviewListener::NativeRawPreviewListener(JNIEnv *env, jobject listener) :
        mJavaVm(nullptr),
        mBitmap(nullptr)
    {
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

    NativeRawPreviewListener::~NativeRawPreviewListener() {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("~NativeRawPreviewListener() no environment");
            return;
        }

        if(mListenerClass)
            env.getEnv()->DeleteGlobalRef(mListenerClass);

        if(mListenerInstance)
            env.getEnv()->DeleteGlobalRef(mListenerInstance);

        if(mBitmap)
            env.getEnv()->DeleteGlobalRef(mBitmap);
    }

    void NativeRawPreviewListener::onPreviewGenerated(const void* data, const int len, const int width, const int height) {
        JavaEnv env(mJavaVm);
        if (!env.getEnv()) {
            LOGE("Dropped onPreviewGenerated()");
            return;
        }

        // Create bitmap if we don't have one
        if(!mBitmap) {
            jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onRawPreviewBitmapNeeded", "(II)Landroid/graphics/Bitmap;");
            if(!callbackMethod) {
                LOGE("onRawPreviewBitmapNeeded() missing");
                return;
            }

            jobject bitmap = env.getEnv()->CallObjectMethod(mListenerInstance, callbackMethod, width, height);
            if(!bitmap) {
                LOGE("Failed to create RAW preview bitmap");
                return;
            }

            mBitmap = env.getEnv()->NewGlobalRef(bitmap);
            if(!mBitmap) {
                LOGE("Failed to acquire bitmap global reference");
                return;
            }
        }

        AndroidBitmapInfo bitmapInfo;
        int result = AndroidBitmap_getInfo(env.getEnv(), mBitmap, &bitmapInfo);

        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_getInfo() failed, error=%d", result);
            return;
        }

        if( bitmapInfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888    ||
            bitmapInfo.stride != width * 4                          ||
            bitmapInfo.width  != width                              ||
            bitmapInfo.height != height)
        {
            LOGE("Invalid bitmap format format=%d, stride=%d, width=%d, height=%d, output.width=%d, output.height=%d",
                 bitmapInfo.format, bitmapInfo.stride, bitmapInfo.width, bitmapInfo.height, width, height);

            // Reset bitmap
            if(mBitmap)
                env.getEnv()->DeleteGlobalRef(mBitmap);

            mBitmap = nullptr;

            return;
        }

        // Copy pixels
        size_t size = bitmapInfo.width * bitmapInfo.height * 4;
        if(len != size) {
            LOGE("buffer sizes do not match, buffer0=%d, buffer1=%ld", len, size);
            return;
        }

        // Copy pixels to bitmap
        void* pixels = nullptr;

        // Lock
        result = AndroidBitmap_lockPixels(env.getEnv(), mBitmap, &pixels);
        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_lockPixels() failed, error=%d", result);
            return;
        }

        std::copy((uint8_t*)data, ((uint8_t*)data) + size, (uint8_t*) pixels);

        // Unlock
        result = AndroidBitmap_unlockPixels(env.getEnv(), mBitmap);
        if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
            LOGE("AndroidBitmap_unlockPixels() failed, error=%d", result);
            return;
        }

        // Report bitmap updated
        jmethodID callbackMethod = env.getEnv()->GetMethodID(mListenerClass, "onRawPreviewUpdated", "()V");
        if(!callbackMethod) {
            LOGE("onRawPreviewUpdated() missing");
            return;
        }

        env.getEnv()->CallVoidMethod(mListenerInstance, callbackMethod);
    }

} // namespace motioncam
