#include "NativeClBuffer.h"
#include "ClHelper.h"

#include <HalideRuntimeOpenCL.h>

namespace motioncam {
    NativeClBuffer::NativeClBuffer(size_t bufferLength) :
            mBufferLength(bufferLength),
            mLockedBuffer(nullptr) {
        cl_int errCode = 0;
        cl_context clContext = nullptr;
        cl_command_queue clQueue = nullptr;

        int halide_error = CL_acquire(&clContext, &clQueue);
        if (halide_error != 0) {
        }

        mClBuffer = CL_createBuffer(clContext, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, bufferLength, nullptr, &errCode);
        if(errCode != 0) {
        }

        CL_release();
    }

    NativeClBuffer::~NativeClBuffer()
    {
        unlock();
        release();
    }

    uint8_t* NativeClBuffer::lock(bool write) {
        cl_int errCode = 0;
        cl_context clContext = nullptr;
        cl_command_queue clQueue = nullptr;

        int halide_error = CL_acquire(&clContext, &clQueue);
        if (halide_error != 0) {
        }

        mLockedBuffer = CL_enqueueMapBuffer(clQueue, mClBuffer, CL_TRUE, write ? CL_MAP_WRITE : CL_MAP_READ, 0, mBufferLength, 0, nullptr, nullptr, &errCode);
        if(errCode != 0) {
        }

        CL_release();

        return (uint8_t*) mLockedBuffer;
    }

    void NativeClBuffer::unlock() {
        if(!mLockedBuffer)
            return;

        cl_context clContext = nullptr;
        cl_command_queue clQueue = nullptr;

        int halide_error = CL_acquire(&clContext, &clQueue);
        if (halide_error != 0) {
        }

        cl_int errCode = CL_enqueueUnmapMemObject(clQueue, mClBuffer, mLockedBuffer, 0, nullptr, nullptr);

        CL_release();

        mLockedBuffer = nullptr;
    }

    uint64_t NativeClBuffer::nativeHandle() {
        return (uint64_t) mClBuffer;
    }

    size_t NativeClBuffer::len() {
        return mBufferLength;
    }

    void NativeClBuffer::allocate(size_t len) {
    }

    const std::vector<uint8_t>& NativeClBuffer::hostData() {
        uint8_t* data = lock(false);

        mHostBuffer.resize(mBufferLength);
        mHostBuffer.assign(data, data + mBufferLength);

        unlock();

        return mHostBuffer;
    }

    void NativeClBuffer::copyHostData(const std::vector<uint8_t>& other) {
    }

    void NativeClBuffer::release() {
        if (mClBuffer) {
            CL_releaseMemObject(mClBuffer);
            mClBuffer = nullptr;
        }
    }
}
