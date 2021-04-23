#include <jni.h>
#include <string>
#include <future>

#include <motioncam/ImageProcessor.h>
#include <motioncam/RawBufferManager.h>
#include <motioncam/Settings.h>
#include <motioncam/Exceptions.h>
#include <motioncam/Logger.h>

using namespace motioncam;

static const jlong INVALID_NATIVE_OBJ  = -1;

class ImageProcessListener : public ImageProcessorProgress {
public:
    ImageProcessListener(JNIEnv* env, jobject progressListener) :
        mEnv(env), mProgressListenerRef(env->NewGlobalRef(progressListener)) {
    }

    ~ImageProcessListener() {
        mEnv->DeleteGlobalRef(mProgressListenerRef);
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

static std::shared_ptr<ImageProcessor> gProcessorObj    = nullptr;
static std::shared_ptr<ImageProcessListener> gListener  = nullptr;

static std::string gLastError;

extern "C" JNIEXPORT
jlong JNICALL Java_com_motioncam_processor_NativeProcessor_CreateProcessor(__unused JNIEnv *env, __unused jobject instance) {
    if(gProcessorObj != nullptr) {
        gLastError = "Image processor already exists";
        return INVALID_NATIVE_OBJ;
    }

    try {
        gProcessorObj = std::make_shared<ImageProcessor>();
    }
    catch(const MotionCamException& e) {
        gLastError = e.what();
        return INVALID_NATIVE_OBJ;
    }

    return reinterpret_cast<long> (gProcessorObj.get());
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_ProcessInMemory(
        JNIEnv *env,
        __unused jobject instance,
        jlong processorObj,
        jstring outputPath_,
        jobject progressListener)
{
    auto* imageProcessor = reinterpret_cast<ImageProcessor*>(processorObj);
    if(imageProcessor != gProcessorObj.get()) {
        logger::log("Trying to use mismatched image processor pointer!");
        return JNI_TRUE;
    }

    gListener = std::make_shared<ImageProcessListener>(env, progressListener);

    const char *javaOutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    std::string outputPath(javaOutputPath);

    env->ReleaseStringUTFChars(outputPath_, javaOutputPath);

    auto container = RawBufferManager::get().peekPendingContainer();
    if(!container)
        return JNI_FALSE;

    motioncam::ImageProcessor::process(*container, outputPath, *gListener);

    RawBufferManager::get().clearPendingContainer();

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_processor_NativeProcessor_ProcessFile(
    JNIEnv *env,
    __unused jobject instance,
    jlong processorObj,
    jstring inputPath_,
    jstring outputPath_,
    jobject progressListener) {

    auto* imageProcessor = reinterpret_cast<ImageProcessor*>(processorObj);
    if(imageProcessor != gProcessorObj.get()) {
        logger::log("Trying to use mismatched image processor pointer!");
        gLastError = "Invalid image processor";
        return JNI_FALSE;
    }

    const char* javaInputPath = env->GetStringUTFChars(inputPath_, nullptr);
    std::string inputPath(javaInputPath);

    const char *javaOutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    std::string outputPath(javaOutputPath);

    env->ReleaseStringUTFChars(inputPath_, javaInputPath);
    env->ReleaseStringUTFChars(outputPath_, javaOutputPath);

    //
    // Process the image
    //

    gListener = std::make_shared<ImageProcessListener>(env, progressListener);

    try {
        motioncam::ImageProcessor::process(inputPath, outputPath, *gListener);
    }
    catch(std::runtime_error& e) {
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        if (exClass == NULL) {
            return JNI_FALSE;
        }

        env->ThrowNew(exClass, e.what());
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT
void JNICALL Java_com_motioncam_processor_NativeProcessor_DestroyProcessor(__unused JNIEnv *env, __unused jobject instance, jlong processorObj) {

    auto* imageProcessor = reinterpret_cast<ImageProcessor*>(processorObj);

    if(imageProcessor != gProcessorObj.get()) {
        logger::log("Trying to destroy mismatched image processor pointer!");
    }

    gProcessorObj   = nullptr;
    gListener       = nullptr;
}

extern "C" JNIEXPORT
jstring JNICALL Java_com_motioncam_processor_NativeProcessor_GetLastError(JNIEnv *env, __unused jobject instance) {
    return env->NewStringUTF(gLastError.c_str());
}