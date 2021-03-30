#include <jni.h>
#include <string>
#include <android/native_window_jni.h>
#include <android/bitmap.h>

#include <motioncam/Settings.h>
#include <motioncam/ImageProcessor.h>
#include <motioncam/RawBufferManager.h>

#include "NativeCameraBridgeListener.h"
#include "NativeRawPreviewListener.h"

#include "camera/CameraSession.h"
#include "camera/Exceptions.h"
#include "camera/Logger.h"
#include "camera/CameraSessionListener.h"
#include "camera/RawPreviewListener.h"
#include "camera/CaptureSessionManager.h"

using namespace motioncam;

static const jlong INVALID_NATIVE_OBJ = -1;

namespace {
    std::shared_ptr<NativeRawPreviewListener> gRawPreviewListener = nullptr;
    std::shared_ptr<NativeCameraBridgeListener> gCameraSessionListener = nullptr;
    std::shared_ptr<CaptureSessionManager> gCaptureSessionManager = nullptr;
    std::shared_ptr<motioncam::ImageProcessor> gImageProcessor = nullptr;
    int gCaptureSessionManagerRefs = 0;

    std::string gLastError;
}

std::shared_ptr<CaptureSessionManager> getCameraSessionManager(jlong handle) {
    if(!handle || !gCaptureSessionManager) {
        gLastError = "No camera session available";
        return nullptr;
    }

    auto* sessionManager = reinterpret_cast<CaptureSessionManager*>(handle);
    if(sessionManager != gCaptureSessionManager.get()) {
        gLastError = "Invalid handle";
        return nullptr;
    }

    return gCaptureSessionManager;
}

extern "C" JNIEXPORT
jlong JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_Create(
        JNIEnv* env, jobject instance, jobject listener, jlong maxMemoryUsageBytes, jstring jNativeLibPath) {

    if(gCaptureSessionManager != nullptr) {
        gLastError = "Capture session already exists";
        return INVALID_NATIVE_OBJ;
    }

    // Try to create the session
    try {
        gCameraSessionListener = std::make_shared<NativeCameraBridgeListener>(env, listener);
        gCaptureSessionManager = std::make_shared<CaptureSessionManager>(maxMemoryUsageBytes);

        gCaptureSessionManagerRefs = 1;
    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
        return INVALID_NATIVE_OBJ;
    }

    return reinterpret_cast<long> (gCaptureSessionManager.get());
}

extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_Acquire(JNIEnv *env, jobject thiz, jlong handle)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(sessionManager) {
        ++gCaptureSessionManagerRefs;
        return JNI_TRUE;
    }

    return JNI_FALSE;
}

extern "C" JNIEXPORT
void JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_Destroy(JNIEnv *env, jobject instance, jlong handle) {
    if(handle == INVALID_NATIVE_OBJ || !gCaptureSessionManager) {
        LOGW("Trying to delete invalid camera session handle");
        return;
    }

    auto* cameraSession = reinterpret_cast<CaptureSessionManager*>(handle);
    if(cameraSession != gCaptureSessionManager.get()) {
        LOGE("Trying to destroy mismatched session pointer!");
        return;
    }

    --gCaptureSessionManagerRefs;

    if(gCaptureSessionManagerRefs == 0) {
        gCaptureSessionManager = nullptr;
        gRawPreviewListener = nullptr;
        gCameraSessionListener = nullptr;
    }
}

extern "C" JNIEXPORT
jstring JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_GetLastError(JNIEnv *env, jobject instance) {
    return env->NewStringUTF(gLastError.c_str());
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_StartCapture(
        JNIEnv *env, jobject instance,
        jlong sessionHandle,
        jstring jcameraId,
        jobject previewSurface,
        jboolean setupForRawPreview)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);
    if(!sessionManager) {
        return JNI_FALSE;
    }

    const char* cameraIdChars = env->GetStringUTFChars(jcameraId, nullptr);
    if(cameraIdChars == nullptr) {
        LOGE("Failed to get camera id");
        return JNI_FALSE;
    }

    std::string cameraId(cameraIdChars);
    env->ReleaseStringUTFChars(jcameraId, cameraIdChars);

    try {
        std::shared_ptr<ANativeWindow> window(ANativeWindow_fromSurface(env, previewSurface), ANativeWindow_release);

        sessionManager->startCamera(cameraId, gCameraSessionListener, window, setupForRawPreview);
    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_StopCapture(JNIEnv *env, jobject thiz, jlong sessionHandle) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return JNI_FALSE;
    }

    try {
        sessionManager->stopCamera();
    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jobject JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_GetRawOutputSize(JNIEnv *env, jobject instance, jlong sessionHandle, jstring jcameraId) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return nullptr;
    }

    const char* cameraIdChars = env->GetStringUTFChars(jcameraId, nullptr);
    if(cameraIdChars == nullptr) {
        LOGE("Failed to get camera id");
        gLastError = "Failed to get camera id";
        return nullptr;
    }

    std::string cameraId(cameraIdChars);
    env->ReleaseStringUTFChars(jcameraId, cameraIdChars);

    try {
        motioncam::OutputConfiguration rawOutputConfig;
        auto cameraDesc = sessionManager->getCameraDescription(cameraId);

        if(sessionManager->getRawConfiguration(*cameraDesc, rawOutputConfig)) {
            jclass cls = env->FindClass("android/util/Size");

            jobject captureSize =
                env->NewObject(
                    cls,
                    env->GetMethodID(cls, "<init>", "(II)V"),
                    rawOutputConfig.outputSize.width(),
                    rawOutputConfig.outputSize.height());

            return captureSize;
        }

    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
    }

    return nullptr;
}

extern "C" JNIEXPORT
jobject JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_GetPreviewOutputSize(
        JNIEnv *env,
        jobject instance,
        jlong sessionHandle,
        jstring jcameraId,
        jobject captureSize,
        jobject displaySize) {

    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);
    if(!sessionManager) {
        return nullptr;
    }

    jclass sizeClass = env->GetObjectClass(captureSize);

    jmethodID getWidthMethod = env->GetMethodID(sizeClass, "getWidth", "()I");
    jmethodID getHeightMethod = env->GetMethodID(sizeClass, "getHeight", "()I");

    int captureWidth = env->CallIntMethod(captureSize, getWidthMethod);
    int captureHeight = env->CallIntMethod(captureSize, getHeightMethod);

    int displayWidth = env->CallIntMethod(displaySize, getWidthMethod);
    int displayHeight = env->CallIntMethod(displaySize, getHeightMethod);

    const char* cameraIdChars = env->GetStringUTFChars(jcameraId, nullptr);
    if(cameraIdChars == nullptr) {
        LOGE("Failed to get camera id");
        gLastError = "Failed to get camera id";
        return nullptr;
    }

    std::string cameraId(cameraIdChars);
    env->ReleaseStringUTFChars(jcameraId, cameraIdChars);

    try {
        DisplayDimension captureDimens = DisplayDimension(captureWidth, captureHeight);
        DisplayDimension displayDimens = DisplayDimension(displayWidth, displayHeight);
        auto cameraDesc = sessionManager->getCameraDescription(cameraId);

        motioncam::OutputConfiguration previewSize;

        if(sessionManager->getPreviewConfiguration(*cameraDesc, captureDimens, displayDimens, previewSize)) {
            jobject previewSizeInstance =
                env->NewObject(
                    sizeClass,
                    env->GetMethodID(sizeClass, "<init>", "(II)V"),
                    previewSize.outputSize.width(),
                    previewSize.outputSize.height());

            return previewSizeInstance;
        }
    }
    catch(const CameraSessionException& e) {
        gLastError = e.what();
    }

    return nullptr;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_CaptureImage(
        JNIEnv *env,
        jobject instance,
        jlong sessionHandle,
        jlong bufferHandle,
        jint saveNumImages,
        jboolean writeDNG,
        jstring postProcessSettings_,
        jstring outputPath_)
{

    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);
    if(!sessionManager) {
        return JNI_FALSE;
    }

    const char *coutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    if(coutputPath == nullptr) {
        LOGE("Failed to get output path");
        return JNI_FALSE;
    }

    std::string outputPath(coutputPath);

    env->ReleaseStringUTFChars(outputPath_, coutputPath);

    const char* cjsonString = env->GetStringUTFChars(postProcessSettings_, nullptr);
    if(cjsonString == nullptr) {
        LOGE("Failed to get settings");
        return JNI_FALSE;
    }

    std::string settingsJson(cjsonString);

    env->ReleaseStringUTFChars(outputPath_, cjsonString);

    // Parse post process settings
    std::string err;
    json11::Json json = json11::Json::parse(settingsJson, err);

    // Can't parse the settings
    if(!err.empty()) {
        return JNI_FALSE;
    }

    auto cameraId = sessionManager->getSelectedCameraId();
    auto metadata = sessionManager->getCameraDescription(cameraId)->metadata;

    motioncam::PostProcessSettings settings(json);

    RawBufferManager::get().save(metadata, bufferHandle, saveNumImages, writeDNG, settings, std::string(outputPath));

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_PauseCapture(JNIEnv *env, jobject thiz, jlong sessionHandle) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);
    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->pauseCamera(true);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_ResumeCapture(JNIEnv *env, jobject thiz, jlong sessionHandle) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);
    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->pauseCamera(false);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_SetManualExposure(
        JNIEnv* env,
        jobject thiz,
        jlong sessionHandle,
        jint iso,
        jlong exposure_time) {

    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->setManualExposure(iso, exposure_time);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_SetAutoExposure(JNIEnv *env, jobject thiz, jlong sessionHandle) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->setAutoExposure();

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jobject JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_GetMetadata(JNIEnv *env, jobject thiz, jlong sessionHandle, jstring jcameraId) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        LOGW("Session manager null. Failed to get metadata");
        return nullptr;
    }

    const char* cameraIdChars = env->GetStringUTFChars(jcameraId, nullptr);
    if(cameraIdChars == nullptr) {
        LOGE("Failed to get camera id");
        gLastError = "Failed to get camera id";
        return nullptr;
    }

    std::string cameraId(cameraIdChars);
    env->ReleaseStringUTFChars(jcameraId, cameraIdChars);

    auto cameraDesc = sessionManager->getCameraDescription(cameraId);

    // Construct result
    jclass nativeCameraMetadataClass = env->FindClass("com/motioncam/camera/NativeCameraMetadata");

    jfloatArray apertures = env->NewFloatArray(cameraDesc->metadata.apertures.size());

    env->SetFloatArrayRegion(
            apertures,
            0,
            cameraDesc->metadata.apertures.size(),
            &cameraDesc->metadata.apertures[0]);

    jobject obj =
        env->NewObject(
                nativeCameraMetadataClass,
                env->GetMethodID(nativeCameraMetadataClass, "<init>", "(IIIJJII[F)V"),
                cameraDesc->sensorOrientation,
                cameraDesc->isoRange[0],
                cameraDesc->isoRange[1],
                cameraDesc->exposureRange[0],
                cameraDesc->exposureRange[1],
                cameraDesc->maxAfRegions,
                cameraDesc->maxAeRegions,
                apertures);

    return obj;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_EnableRawPreview(
        JNIEnv *env, jobject thiz, jlong sessionHandle, jobject listener, jint previewQuality, jboolean overrideWb)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return JNI_FALSE;
    }

    // Check if we have already enabled RAW preview
    if(gRawPreviewListener)
        return JNI_FALSE;

    gRawPreviewListener = std::make_shared<NativeRawPreviewListener>(env, listener);

    sessionManager->enableRawPreview(gRawPreviewListener, previewQuality, overrideWb);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_DisableRawPreview(JNIEnv *env, jobject thiz, jlong sessionHandle) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return JNI_FALSE;
    }

    gRawPreviewListener = nullptr;

    sessionManager->disableRawPreview();

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_UpdateOrientation(JNIEnv *env, jobject thiz, jlong sessionHandle, jint orientation) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->updateOrientation(static_cast<ScreenOrientation>(orientation));

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCameraSessionBridge_SetFocusPoint(
        JNIEnv *env, jobject thiz, jlong sessionHandle, jfloat focusX, jfloat focusY, jfloat exposureX, jfloat exposureY) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->setFocusPoint(focusX, focusY, exposureX, exposureY);

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCameraSessionBridge_SetAutoFocus(JNIEnv *env, jobject thiz, jlong sessionHandle) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(sessionHandle);

    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->setAutoFocus();

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jobjectArray JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_GetAvailableImages(
        JNIEnv *env,
        jobject thiz,
        jlong handle)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return nullptr;
    }

    // Construct result
    jclass nativeCameraBufferClass = env->FindClass("com/motioncam/camera/NativeCameraBuffer");
    jobjectArray result = nullptr;

    // Return available buffers
    {
        auto lockedBuffers = RawBufferManager::get().lockAllBuffers(true);
        if (!lockedBuffers)
            return nullptr;

        auto buffers = lockedBuffers->getBuffers();

        result = env->NewObjectArray(static_cast<jsize>(buffers.size()), nativeCameraBufferClass, nullptr);
        if(result == nullptr)
            return nullptr;

        for (size_t i = 0; i < buffers.size(); i++) {
            jobject obj =
                    env->NewObject(
                            nativeCameraBufferClass,
                            env->GetMethodID(nativeCameraBufferClass, "<init>", "(JIJIII)V"),
                            buffers[i]->metadata.timestampNs,
                            buffers[i]->metadata.iso,
                            buffers[i]->metadata.exposureTime,
                            static_cast<int>(buffers[i]->metadata.screenOrientation),
                            buffers[i]->width,
                            buffers[i]->height);

            env->SetObjectArrayElement(result, i, obj);
        }
    }

    return result;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_motioncam_camera_NativeCameraSessionBridge_GetPreviewSize(
        JNIEnv *env,
        jobject thiz,
        jlong handle,
        jint downscaleFactor)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return nullptr;
    }

    // Get first available buffer and return size divided by downscale factor
    jobject captureSize = nullptr;

    {
        auto lockedBuffer = RawBufferManager::get().lockNumBuffers(1, true);
        if (!lockedBuffer || lockedBuffer->getBuffers().empty())
            return nullptr;

        auto imageBuffer = lockedBuffer->getBuffers().front();

        // Return as Size instance
        jclass cls = env->FindClass("android/util/Size");
        captureSize =
                env->NewObject(
                        cls,
                        env->GetMethodID(cls, "<init>", "(II)V"),
                        imageBuffer->width / downscaleFactor / 2,
                        imageBuffer->height / downscaleFactor / 2
                );
    }

    return captureSize;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_motioncam_camera_NativeCameraSessionBridge_CreateImagePreview(
        JNIEnv *env,
        jobject thiz,
        jlong handle,
        jlong bufferHandle,
        jstring postProcessSettings_,
        jint downscaleFactor,
        jobject dst)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager || !gImageProcessor) {
        return JNI_FALSE;
    }

    // Parse post process settings
    const char* cJsonString = env->GetStringUTFChars(postProcessSettings_, nullptr);
    if(cJsonString == nullptr) {
        LOGE("Failed to get settings");
        return JNI_FALSE;
    }

    std::string jsonString(cJsonString);

    env->ReleaseStringUTFChars(postProcessSettings_, cJsonString);

    std::string err;
    json11::Json json = json11::Json::parse(jsonString, err);

    // Can't parse the settings
    if(!err.empty()) {
        return JNI_FALSE;
    }

    motioncam::PostProcessSettings settings(json);

    auto lockedBuffer = RawBufferManager::get().lockBuffer(bufferHandle, true);
    if(!lockedBuffer || lockedBuffer->getBuffers().empty()) {
        LOGE("Failed to get image buffer");
        return JNI_FALSE;
    }

    auto imageBuffer = lockedBuffer->getBuffers().front();

    // Create preview from image buffer
    auto cameraId = sessionManager->getSelectedCameraId();
    auto metadata = sessionManager->getCameraDescription(cameraId)->metadata;
    auto output = gImageProcessor->createPreview(*imageBuffer, downscaleFactor, metadata, settings);

    // Get bitmap info
    AndroidBitmapInfo bitmapInfo;

    int result = AndroidBitmap_getInfo(env, dst, &bitmapInfo);

    if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
        LOGE("AndroidBitmap_getInfo() failed, error=%d", result);
        return JNI_FALSE;
    }

    if( bitmapInfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888    ||
        bitmapInfo.stride != output.width() * 4                 ||
        bitmapInfo.width  != output.width()                     ||
        bitmapInfo.height != output.height())
    {
        LOGE("Invalid bitmap format format=%d, stride=%d, width=%d, height=%d, output.width=%d, output.height=%d",
             bitmapInfo.format, bitmapInfo.stride, bitmapInfo.width, bitmapInfo.height, output.width(), output.height());

        return JNI_FALSE;
    }

    // Copy pixels
    size_t size = bitmapInfo.width * bitmapInfo.height * 4;
    if(output.size_in_bytes() != size) {
        LOGE("buffer sizes do not match, buffer0=%ld, buffer1=%ld", output.size_in_bytes(), size);
        return JNI_FALSE;
    }

    // Copy pixels to bitmap
    void* pixels = nullptr;

    // Lock
    result = AndroidBitmap_lockPixels(env, dst, &pixels);
    if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
        LOGE("AndroidBitmap_lockPixels() failed, error=%d", result);
        return JNI_FALSE;
    }

    std::copy(output.data(), output.data() + size, (uint8_t*) pixels);

    // Unlock
    result = AndroidBitmap_unlockPixels(env, dst);
    if(result != ANDROID_BITMAP_RESULT_SUCCESS) {
        LOGE("AndroidBitmap_unlockPixels() failed, error=%d", result);
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

extern "C"
JNIEXPORT jdouble JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_MeasureSharpness(
        JNIEnv *env,
        jobject thiz,
        jlong handle,
        jlong bufferHandle)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return JNI_FALSE;
    }

    auto lockedBuffer = RawBufferManager::get().lockBuffer(bufferHandle, true);
    if(!lockedBuffer || lockedBuffer->getBuffers().empty())
        return -1e10;

    auto cameraId = sessionManager->getSelectedCameraId();
    auto metadata = sessionManager->getCameraDescription(cameraId)->metadata;
    auto imageBuffer = lockedBuffer->getBuffers().front();

    return gImageProcessor->measureSharpness(*imageBuffer);
}

extern "C"
JNIEXPORT jstring JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_EstimatePostProcessSettings(
        JNIEnv *env,
        jobject thiz,
        jlong handle,
        jboolean basicSettings)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return nullptr;
    }

    motioncam::PostProcessSettings settings;

    {
        auto lockedBuffer = RawBufferManager::get().lockNumBuffers(1, true);
        if (!lockedBuffer || lockedBuffer->getBuffers().empty())
            return nullptr;

        auto imageBuffer = lockedBuffer->getBuffers().front();

        auto cameraId = sessionManager->getSelectedCameraId();
        auto metadata = sessionManager->getCameraDescription(cameraId)->metadata;

        if (basicSettings)
            gImageProcessor->estimateBasicSettings(*imageBuffer, metadata, settings);
        else
            gImageProcessor->estimateSettings(*imageBuffer, metadata, settings);
    }

    auto settingsJson = settings.toJson();
    return env->NewStringUTF(settingsJson.dump().c_str());
}

extern "C"
JNIEXPORT jobjectArray JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_GetSupportedCameras(
        JNIEnv *env,
        jobject thiz,
        jlong handle)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return nullptr;
    }

    auto supportedCameras = sessionManager->getSupportedCameras();

    jclass nativeCameraInfoClass = env->FindClass("com/motioncam/camera/NativeCameraInfo");

    jobjectArray result = env->NewObjectArray(static_cast<jsize>(supportedCameras.size()), nativeCameraInfoClass, nullptr);
    if(result == nullptr)
        return nullptr;

    for(size_t i = 0; i < supportedCameras.size(); i++) {
        auto desc = sessionManager->getCameraDescription(supportedCameras[i]);

        jstring jcameraId = env->NewStringUTF(supportedCameras[i].c_str());
        jboolean supportsLinearPreview =
                std::find(desc->tonemapModes.begin(), desc->tonemapModes.end(), ACAMERA_TONEMAP_MODE_CONTRAST_CURVE) != desc->tonemapModes.end();

        jobject obj =
                env->NewObject(
                        nativeCameraInfoClass,
                        env->GetMethodID(nativeCameraInfoClass, "<init>", "(Ljava/lang/String;ZZ)V"),
                        jcameraId,
                        desc->lensFacing == ACAMERA_LENS_FACING_FRONT,
                        supportsLinearPreview
                );

        env->DeleteLocalRef(jcameraId);

        env->SetObjectArrayElement(result, i, obj);
    }

    return result;
}

extern "C" JNIEXPORT
void JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_InitImageProcessor(JNIEnv *env, jobject thiz)
{
    LOGI("Creating image processor");
    gImageProcessor = std::make_shared<motioncam::ImageProcessor>();
}

extern "C" JNIEXPORT
void JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_DestroyImageProcessor(JNIEnv *env, jobject thiz)
{
    LOGI("Destroying image processor");
    gImageProcessor = nullptr;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_SetExposureCompensation(JNIEnv *env, jobject thiz, jlong handle, jfloat value)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->setExposureCompensation(value);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jfloat JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_EstimateShadows(JNIEnv *env, jobject thiz, jlong handle, jfloat bias) {
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return -1.0f;
    }

    auto lockedBuffer = RawBufferManager::get().lockNumBuffers(1, false);
    if(!lockedBuffer || lockedBuffer->getBuffers().empty())
        return -1;

    auto cameraId = sessionManager->getSelectedCameraId();
    auto metadata = sessionManager->getCameraDescription(cameraId)->metadata;
    auto imageBuffer = lockedBuffer->getBuffers().front();

    cv::Mat histogram = motioncam::ImageProcessor::calcHistogram(metadata, *imageBuffer, false, 8);

    double s = 1.8*1.8;
    double ev = std::log2(s / (imageBuffer->metadata.exposureTime / (1.0e9))) - std::log2(imageBuffer->metadata.iso / 100.0);
    double keyValue = 1.03 - bias / (bias + std::log10(std::pow(10.0, ev) + 1));

    float result = motioncam::ImageProcessor::estimateShadows(histogram, keyValue);

    return result;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_SetRawPreviewSettings(
        JNIEnv *env,
        jobject thiz,
        jlong handle,
        jfloat shadows,
        jfloat contrast,
        jfloat saturation,
        jfloat blacks,
        jfloat whitePoint,
        jfloat tempOffset,
        jfloat tintOffset)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return JNI_FALSE;
    }

    sessionManager->updateRawPreviewSettings(
            shadows, contrast, saturation, blacks, whitePoint, tempOffset, tintOffset);

    return JNI_TRUE;
}

extern "C" JNIEXPORT
jboolean JNICALL Java_com_motioncam_camera_NativeCameraSessionBridge_CaptureHdrImage(
    JNIEnv *env,
    jobject thiz,
    jlong handle,
    jint numImages,
    jint baseIso,
    jlong baseExposure,
    jint hdrIso,
    jlong hdrExposure,
    jstring postProcessSettings_,
    jstring outputPath_)
{
    std::shared_ptr<CaptureSessionManager> sessionManager = getCameraSessionManager(handle);
    if(!sessionManager) {
        return JNI_FALSE;
    }

    const char *coutputPath = env->GetStringUTFChars(outputPath_, nullptr);
    if(coutputPath == nullptr) {
        LOGE("Failed to get output path");
        return JNI_FALSE;
    }

    std::string outputPath(coutputPath);

    env->ReleaseStringUTFChars(outputPath_, coutputPath);

    const char* cjsonString = env->GetStringUTFChars(postProcessSettings_, nullptr);
    if(cjsonString == nullptr) {
        LOGE("Failed to get settings");
        return JNI_FALSE;
    }

    std::string settingsJson(cjsonString);

    env->ReleaseStringUTFChars(outputPath_, cjsonString);

    // Parse post process settings
    std::string err;
    json11::Json json = json11::Json::parse(settingsJson, err);

    // Can't parse the settings
    if(!err.empty()) {
        return JNI_FALSE;
    }

    motioncam::PostProcessSettings settings(json);

    sessionManager->captureHdrImage(numImages, baseIso, baseExposure, hdrIso, hdrExposure, settings, outputPath);

    return JNI_TRUE;
}
