#ifndef MOTIONCAM_ANDROID_LOGGER_H
#define MOTIONCAM_ANDROID_LOGGER_H

#include <string>
#include <android/log.h>

namespace motioncam {

    #define LOG_TAG "libMotionCam"

    #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
    #define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

}

#endif //MOTIONCAM_ANDROID_LOGGER_H
