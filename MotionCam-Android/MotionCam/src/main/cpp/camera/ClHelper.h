#ifndef MOTIONCAM_ANDROID_CLHELPER_H
#define MOTIONCAM_ANDROID_CLHELPER_H

#include <string>

#ifdef __ANDROID__
    #define CL_TARGET_OPENCL_VERSION 120
    #include <CL/cl.h>
#else
    #include <OpenCL/OpenCL.h>
#endif

namespace motioncam {
    int CL_acquire(cl_context *ctx, cl_command_queue *q, bool create = true);
    int CL_release();

    cl_mem CL_createBuffer(cl_context context, cl_mem_flags flags, size_t size, void* hostPtr, cl_int* errCode);
    cl_int CL_releaseMemObject(cl_mem mem);

    void* CL_enqueueMapBuffer(
            cl_command_queue command_queue,
            cl_mem buffer,
            cl_bool blocking_map,
            cl_map_flags map_flags,
            size_t offset,
            size_t size,
            cl_uint num_events_in_wait_list,
            const cl_event* event_wait_list,
            cl_event* event,
            cl_int* errcode_ret);

    cl_int CL_enqueueUnmapMemObject(
            cl_command_queue command_queue,
            cl_mem memobj,
            void* mapped_ptr,
            cl_uint num_events_in_wait_list,
            const cl_event* event_wait_list,
            cl_event* event);
}

#endif //MOTIONCAM_ANDROID_CLHELPER_H
