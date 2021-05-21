#include <jni.h>
#include <string>
#include <future>

#include <motioncam/ImageProcessor.h>
#include <motioncam/RawBufferManager.h>
#include <motioncam/Settings.h>
#include <motioncam/Exceptions.h>
#include <motioncam/Logger.h>

using namespace motioncam;

class ImageProcessListener : public ImageProcessorProgress {
public:
    ImageProcessListener(JNIEnv* env, jobject progressListener) :
        mEnv(env), mProgressListenerRef(env->NewGlobalRef(progressListener)) {
    }

    ~ImageProcessListener() {
        mEnv->DeleteGlobalRef(mProgressListenerRef);
    }

    std::string onPreviewSaved(const std::string& outputPath) const override {
        jmethodID onCompletedMethod = mEnv->GetMethodID(
                mEnv->GetObjectClass(mProgressListenerRef),
                "onPreviewSaved",
                "(Ljava/lang/String;)Ljava/lang/String;");

        auto result = (jstring)
                mEnv->CallObjectMethod(mProgressListenerRef, onCompletedMethod, mEnv->NewStringUTF(outputPath.c_str()));

        const char *resultStr = mEnv->GetStringUTFChars(result, nullptr);
        std::string metadata(resultStr);

        mEnv->ReleaseStringUTFChars(result, resultStr);

        return metadata;
    }

    bool onProgressUpdate(int progress) const override {
        jmethodID onProgressMethod = mEnv->GetMethodID(
                mEnv->GetObjectClass(mProgressListenerRef),
                "onProgressUpdate",
                "(I)Z");

        jboolean result = mEnv->CallBooleanMethod(mProgressListenerRef, onProgressMethod, progress);

        return result == 1;
    }

    void onCompleted() const override {
        jmethodID onCompletedMethod = mEnv->GetMethodID(
                mEnv->GetObjectClass(mProgressListenerRef),
                "onCompleted",
                "()V");

        mEnv->CallVoidMethod(mProgressListenerRef, onCompletedMethod);
    }

    void onError(const std::string& error) const override {
        jmethodID onErrorMethod = mEnv->GetMethodID(
                mEnv->GetObjectClass(mProgressListenerRef),
                "onError",
                "(Ljava/lang/String;)V");

        mEnv->CallObjectMethod(mProgressListenerRef, onErrorMethod, mEnv->NewStringUTF(error.c_str()));
    }

private:
    JNIEnv* mEnv;
    jobject mProgressListenerRef;
};

static std::string gLastError;

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_ProcessInMemory(
        JNIEnv *env,
        jobject instance,
        jstring outputPath_,
        jobject progressListener)
{
    const char *javaOutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    std::string outputPath(javaOutputPath);

    env->ReleaseStringUTFChars(outputPath_, javaOutputPath);

    auto container = RawBufferManager::get().popPendingContainer();
    if(!container)
        return JNI_FALSE;

    try {
        ImageProcessListener listener(env, progressListener);

        motioncam::ImageProcessor::process(*container, outputPath, listener);
    }
    catch(std::runtime_error& e) {
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        if (exClass == nullptr) {
            return JNI_FALSE;
        }

        env->ThrowNew(exClass, e.what());
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_ProcessFile(
    JNIEnv *env,
    jobject instance,
    jstring inputPath_,
    jstring outputPath_,
    jobject progressListener) {

    const char* javaInputPath = env->GetStringUTFChars(inputPath_, nullptr);
    std::string inputPath(javaInputPath);

    const char *javaOutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    std::string outputPath(javaOutputPath);

    env->ReleaseStringUTFChars(inputPath_, javaInputPath);
    env->ReleaseStringUTFChars(outputPath_, javaOutputPath);

    //
    // Process the image
    //

    try {
        ImageProcessListener listener(env, progressListener);

        motioncam::ImageProcessor::process(inputPath, outputPath, listener);
    }
    catch(std::runtime_error& e) {
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        if (exClass == nullptr) {
            return JNI_FALSE;
        }

        env->ThrowNew(exClass, e.what());
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jstring JNICALL Java_com_motioncam_processor_NativeProcessor_GetLastError(JNIEnv *env, __unused jobject instance) {
    return env->NewStringUTF(gLastError.c_str());
}