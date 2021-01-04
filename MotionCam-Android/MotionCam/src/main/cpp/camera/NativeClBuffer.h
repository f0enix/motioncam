#ifndef MOTIONCAM_ANDROID_NATIVECLBUFFER_H
#define MOTIONCAM_ANDROID_NATIVECLBUFFER_H

#include <motioncam/RawImageMetadata.h>

#ifdef __ANDROID__
    #define CL_TARGET_OPENCL_VERSION 120
    #include <CL/cl.h>
#else
    #include <OpenCL/OpenCL.h>
#endif

namespace motioncam {
    class NativeClBuffer : public NativeBuffer {
    public:
        NativeClBuffer(size_t bufferLength);
        ~NativeClBuffer();

        uint8_t* lock(bool write);
        void unlock();

        uint64_t nativeHandle();

        size_t len();
        void allocate(size_t len);

        const std::vector<uint8_t>& hostData();
        void copyHostData(const std::vector<uint8_t>& other);

        void release();

    private:
        size_t mBufferLength;
        cl_mem mClBuffer;
        void* mLockedBuffer;
        std::vector<uint8_t> mHostBuffer;
    };
}

#endif //MOTIONCAM_ANDROID_NATIVECLBUFFER_H
