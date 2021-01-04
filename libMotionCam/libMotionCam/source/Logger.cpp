#include "motioncam/Logger.h"

#include <iostream>

#ifdef __ANDROID__
    #include <android/log.h>
#endif

namespace motioncam {
    #ifdef __ANDROID__
        static const char* ANDROID_TAG = "libMotionCam";
    #endif
    
    namespace logger {
        void log(const std::string& str) {
#ifdef __ANDROID__
            __android_log_print(ANDROID_LOG_INFO, ANDROID_TAG, "%s", str.c_str());
#else
            std::cout << str << std::endl;
#endif
        }
    }
}
