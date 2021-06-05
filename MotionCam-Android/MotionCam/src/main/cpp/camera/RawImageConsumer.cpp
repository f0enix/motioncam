#include "RawImageConsumer.h"
#include "RawPreviewListener.h"
#include "CameraDescription.h"
#include "Logger.h"
#include "ClHelper.h"
#include "NativeClBuffer.h"
#include "Exceptions.h"

#ifdef GPU_CAMERA_PREVIEW
    #include <HalideRuntimeOpenCL.h>
    #include <motioncam/CameraPreview.h>
#endif

#include <chrono>
#include <memory>
#include <utility>

#include <motioncam/Util.h>
#include <motioncam/Measure.h>
#include <motioncam/Settings.h>
#include <motioncam/RawBufferManager.h>
#include <motioncam/RawContainer.h>
#include "motioncam/CameraProfile.h"
#include "motioncam/Temperature.h"
#include <motioncam/ImageProcessor.h>

#include <camera/NdkCameraMetadata.h>

namespace motioncam {
    static const int COPY_THREADS = 1; // More than one copy thread breaks RAW preview
    static const int MINIMUM_BUFFERS = 16;
    static const float SHADOW_BIAS = 16.0f;
    static const int ESTIMATE_SHADOWS_FRAME_INTERVAL = 8;

#ifdef GPU_CAMERA_PREVIEW
    void VERIFY_RESULT(int32_t errCode, const std::string& errString)
    {
        if(errCode != 0)
            throw RawPreviewException(errString);
    }
#endif

    //

    RawImageConsumer::RawImageConsumer(std::shared_ptr<CameraDescription> cameraDescription, const size_t maxMemoryUsageBytes) :
        mMaximumMemoryUsageBytes(maxMemoryUsageBytes),
        mRunning(false),
        mEnableRawPreview(false),
        mRawPreviewQuality(4),
        mCopyCaptureColorTransform(true),
        mOverrideWhiteBalance(false),
        mShadowBoost(0.0f),
        mTempOffset(0.0f),
        mTintOffset(0.0f),
        mPreviewShadows(0.0f),
        mPreviewShadowStep(0.0f),
        mCameraDesc(std::move(cameraDescription))
    {
    }

    RawImageConsumer::~RawImageConsumer() {
        stop();
        RawBufferManager::get().reset();
    }

    void RawImageConsumer::start() {
        LOGD("Starting image consumer");

        if(mRunning) {
            LOGD("Attempting to start already running image consumer");
            return;
        }

        mRunning = true;

        // Start consumer threads
        for(int i = 0; i < COPY_THREADS; i++) {
            mConsumerThreads.push_back(std::make_shared<std::thread>(&RawImageConsumer::doCopyImage, this));
        }
    }

    void RawImageConsumer::stop() {
        if(!mRunning) {
            return;
        }

        disableRawPreview();

        // Stop consumer threads
        mRunning = false;

        for(auto& mConsumerThread : mConsumerThreads) {
            mConsumerThread->join();
        }

        mConsumerThreads.clear();
    }

    void RawImageConsumer::queueImage(AImage* image) {
        mImageQueue.enqueue(std::shared_ptr<AImage>(image, AImage_delete));
    }

    void RawImageConsumer::queueMetadata(const ACameraMetadata* cameraMetadata, ScreenOrientation screenOrientation, RawType rawType) {
        using namespace std::chrono;

        RawImageMetadata metadata;

        if(!copyMetadata(metadata, cameraMetadata)) {
            LOGW("Failed to copy frame metadata. Buffer will be unusable!");
            return;
        }

        // Keep screen orientation and RAW type at time of capture
        metadata.screenOrientation = screenOrientation;
        metadata.rawType = rawType;
        metadata.recvdTimestampMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        // Add pending metadata
        mPendingMetadata.enqueue(std::move(metadata));
    }

    cv::Mat getColorMatrix(ACameraMetadata_const_entry& entry) {
        cv::Mat m(3, 3, CV_32F);

        for(int y = 0; y < 3; y++) {
            for(int x = 0; x < 3; x++) {
                int i = y * 3 + x;

                m.at<float>(y, x) = (float) entry.data.r[i].numerator / (float) entry.data.r[i].denominator;
            }
        }

        return m;
    }

    bool RawImageConsumer::copyMetadata(RawImageMetadata& dst, const ACameraMetadata* src) {
        ACameraMetadata_const_entry metadataEntry;

        // Without the timestamp this metadata is useless
        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_TIMESTAMP, &metadataEntry) == ACAMERA_OK) {
            dst.timestampNs = metadataEntry.data.i64[0];
        }
        else {
            LOGE("ACAMERA_SENSOR_TIMESTAMP error");
            return false;
        }

        // Color balance
        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_NEUTRAL_COLOR_POINT, &metadataEntry) == ACAMERA_OK) {
            dst.asShot[0] = (float) metadataEntry.data.r[0].numerator / (float) metadataEntry.data.r[0].denominator;
            dst.asShot[1] = (float) metadataEntry.data.r[1].numerator / (float) metadataEntry.data.r[1].denominator;
            dst.asShot[2] = (float) metadataEntry.data.r[2].numerator / (float) metadataEntry.data.r[2].denominator;
        }
        else {
            LOGE("ACAMERA_SENSOR_NEUTRAL_COLOR_POINT error");
            return false;
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_SENSITIVITY, &metadataEntry) == ACAMERA_OK) {
            dst.iso = metadataEntry.data.i32[0];
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_EXPOSURE_TIME, &metadataEntry) == ACAMERA_OK) {
            dst.exposureTime = metadataEntry.data.i64[0];
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, &metadataEntry) == ACAMERA_OK) {
            dst.exposureCompensation = metadataEntry.data.i32[0];
        }

        // Get lens shading map
        int lensShadingMapWidth;
        int lensShadingMapHeight;

        if(ACameraMetadata_getConstEntry(src, ACAMERA_LENS_INFO_SHADING_MAP_SIZE, &metadataEntry) == ACAMERA_OK) {
            lensShadingMapWidth  = metadataEntry.data.i32[0];
            lensShadingMapHeight = metadataEntry.data.i32[1];

            for (int i = 0; i < 4; i++) {
                cv::Mat m(lensShadingMapHeight, lensShadingMapWidth, CV_32F, cv::Scalar(1.0f));
                dst.lensShadingMap.push_back(m);
            }
        }
        else {
            LOGE("ACAMERA_LENS_INFO_SHADING_MAP_SIZE error");
            return false;
        }

        if(ACameraMetadata_getConstEntry(src, ACAMERA_STATISTICS_LENS_SHADING_MAP, &metadataEntry) == ACAMERA_OK) {
            for (int y = 0; y < lensShadingMapHeight; y++) {
                int i = y * lensShadingMapWidth * 4;

                for (int x = 0; x < lensShadingMapWidth * 4; x += 4) {
                    dst.lensShadingMap[0].at<float>(y, x / 4) = metadataEntry.data.f[i + x];
                    dst.lensShadingMap[1].at<float>(y, x / 4) = metadataEntry.data.f[i + x + 1];
                    dst.lensShadingMap[2].at<float>(y, x / 4) = metadataEntry.data.f[i + x + 2];
                    dst.lensShadingMap[3].at<float>(y, x / 4) = metadataEntry.data.f[i + x + 3];
                }
            }
        }
        else {
            LOGE("ACAMERA_STATISTICS_LENS_SHADING_MAP error");
            return false;
        }

//        // Keep Noise profile
//        if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_NOISE_PROFILE, &metadataEntry) == ACAMERA_OK) {
//            dst.noiseProfile.resize(metadataEntry.count);
//            for(int n = 0; n < metadataEntry.count; n++) {
//                dst.noiseProfile[n] = metadataEntry.data.d[n];
//            }
//        }

        // If the color transform is not part of the request we won't attempt tp copy it
        if(mCopyCaptureColorTransform) {
            // ACAMERA_SENSOR_CALIBRATION_TRANSFORM1
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_CALIBRATION_TRANSFORM1, &metadataEntry) == ACAMERA_OK) {
                dst.calibrationMatrix1 = getColorMatrix(metadataEntry);
            }

            // ACAMERA_SENSOR_CALIBRATION_TRANSFORM2
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_CALIBRATION_TRANSFORM2, &metadataEntry) == ACAMERA_OK) {
                dst.calibrationMatrix2 = getColorMatrix(metadataEntry);
            }

            // ACAMERA_SENSOR_FORWARD_MATRIX1
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_FORWARD_MATRIX1, &metadataEntry) == ACAMERA_OK) {
                dst.forwardMatrix1 = getColorMatrix(metadataEntry);
            }

            // ACAMERA_SENSOR_FORWARD_MATRIX2
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_FORWARD_MATRIX2, &metadataEntry) == ACAMERA_OK) {
                dst.forwardMatrix2 = getColorMatrix(metadataEntry);
            }

            // ACAMERA_SENSOR_COLOR_TRANSFORM1
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_COLOR_TRANSFORM1, &metadataEntry) == ACAMERA_OK) {
                dst.colorMatrix1 = getColorMatrix(metadataEntry);
            }
            else {
                mCopyCaptureColorTransform = false;
            }

            // ACAMERA_SENSOR_COLOR_TRANSFORM2
            if(ACameraMetadata_getConstEntry(src, ACAMERA_SENSOR_COLOR_TRANSFORM2, &metadataEntry) == ACAMERA_OK) {
                dst.colorMatrix2 = getColorMatrix(metadataEntry);
            }
            else {
                mCopyCaptureColorTransform = false;
            }
        }

        return true;
    }

    void RawImageConsumer::onBufferReady(const std::shared_ptr<RawImageBuffer>& buffer) {
        // Estimate settings every few frames
        if(mFramesSinceEstimatedSettings >= ESTIMATE_SHADOWS_FRAME_INTERVAL)
        {
            float ev = motioncam::ImageProcessor::calcEv(mCameraDesc->metadata, buffer->metadata);
            float keyValue = 1.03f - SHADOW_BIAS / (SHADOW_BIAS + std::log10(std::pow(10.0f, ev) + 1));

            // Keep previous value
            mPreviewShadows = mEstimatedSettings.shadows;

            motioncam::ImageProcessor::estimateBasicSettings(*buffer, mCameraDesc->metadata, keyValue, mEstimatedSettings);

            // Update shadows to include user selected boost
            float userShadows = std::pow(2.0f, std::log(mEstimatedSettings.shadows) / std::log(2.0f) + mShadowBoost);
            mEstimatedSettings.shadows = std::max(1.0f, std::min(32.0f, userShadows));

            // Store noise profile
            if(!buffer->metadata.noiseProfile.empty()) {
                mEstimatedSettings.noiseSigma = 1024 * sqrt(0.18 * buffer->metadata.noiseProfile[0] + buffer->metadata.noiseProfile[1]);
            }

            mPreviewShadowStep = (1.0f / ESTIMATE_SHADOWS_FRAME_INTERVAL) * (mEstimatedSettings.shadows - mPreviewShadows);
            mFramesSinceEstimatedSettings = 0;
        }
        else {
            ++mFramesSinceEstimatedSettings;

            // Interpolate shadows to make transition smoother
            mPreviewShadows += mPreviewShadowStep;
        }

        RawBufferManager::get().enqueueReadyBuffer(buffer);

    }

    void RawImageConsumer::doMatchMetadata() {
        using namespace std::chrono;

        auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        RawImageMetadata metadata;

        std::vector<RawImageMetadata> unmatched;

        while(mPendingMetadata.try_dequeue(metadata)) {
            auto pendingBufferIt = mPendingBuffers.find(metadata.timestampNs);

            // Found a match, set it to the image and remove from pending list
            if(pendingBufferIt != mPendingBuffers.end()) {
                // Update the metadata of the image
                pendingBufferIt->second->metadata = metadata;

                // Return buffer to either preprocess queue or normal queue if raw preview is not enabled
                if( mEnableRawPreview &&
                    mPreprocessQueue.size_approx() < 2 &&
                    pendingBufferIt->second->metadata.rawType == RawType::ZSL)
                {
                    mPreprocessQueue.enqueue(pendingBufferIt->second);
                }
                else {
                    onBufferReady(pendingBufferIt->second);
                }

                // Erase from pending buffer and metadata
                mPendingBuffers.erase(pendingBufferIt);
            }
            else {
                // If the metadata has not been matched within a reasonable amount of time we can
                // return it to the queue
                if(now - metadata.recvdTimestampMs < 5000) {
                    unmatched.push_back(std::move(metadata));
                }
                else {
                    LOGD("Discarding %ld metadata, too old.", metadata.recvdTimestampMs);
                }
            }
        }

        mPendingMetadata.enqueue_bulk(unmatched.begin(), unmatched.size());
    }

    void RawImageConsumer::doSetupBuffers(size_t bufferLength) {
        int memoryUseBytes = RawBufferManager::get().memoryUseBytes();

        LOGI("Setting up buffers");

#ifdef GPU_CAMERA_PREVIEW
        {
            // Make sure the OpenCL library is loaded/symbols looked up in Halide
            Halide::Runtime::Buffer<int32_t> buf(32);
            buf.device_malloc(halide_opencl_device_interface());

            // Use relaxed math
            halide_opencl_set_build_options("-cl-fast-relaxed-math");
        }
#endif

        while(  mRunning
                &&  ( memoryUseBytes + bufferLength < mMaximumMemoryUsageBytes
                    || RawBufferManager::get().numBuffers() < MINIMUM_BUFFERS) )
        {
            std::shared_ptr<RawImageBuffer> buffer;

#ifdef GPU_CAMERA_PREVIEW
            buffer = std::make_shared<RawImageBuffer>(std::make_unique<NativeClBuffer>(bufferLength));
#else
            buffer = std::make_shared<RawImageBuffer>(std::make_unique<NativeHostBuffer>(bufferLength));
#endif

            RawBufferManager::get().addBuffer(buffer);

            memoryUseBytes = RawBufferManager::get().memoryUseBytes();

            LOGI("Memory use: %d, max: %zu", memoryUseBytes, mMaximumMemoryUsageBytes);
        }

        LOGD("Finished setting up %d buffers", RawBufferManager::get().numBuffers());
    }

#ifdef GPU_CAMERA_PREVIEW
    Halide::Runtime::Buffer<uint8_t> RawImageConsumer::createCameraPreviewOutputBuffer(const RawImageBuffer& buffer, const int downscaleFactor) {
        const int width = buffer.width / 2 / downscaleFactor;
        const int height = buffer.height / 2 / downscaleFactor;
        const int bufSize = width * height * 4;

        cl_int errCode = 0;
        cl_context clContext = nullptr;
        cl_command_queue clQueue = nullptr;

        VERIFY_RESULT(CL_acquire(&clContext, &clQueue), "Failed to acquire CL context");

        cl_mem clOutputBuffer = CL_createBuffer(clContext, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, bufSize, nullptr, &errCode);
        VERIFY_RESULT(errCode, "Failed to create camera preview buffer");

        halide_buffer_t buf = {0};
        halide_dimension_t dim[3];

        buf.dim = &dim[0];
        buf.dimensions = 3;
        buf.dim[0].extent = width;
        buf.dim[0].stride = 4;
        buf.dim[1].extent = height;
        buf.dim[1].stride = width*4;
        buf.dim[2].extent = 4;
        buf.dim[2].stride = 1;
        buf.type = halide_type_of<uint8_t>();

        VERIFY_RESULT(halide_opencl_wrap_cl_mem(nullptr, &buf, (uint64_t) clOutputBuffer), "Failed to wrap camera preview");

        VERIFY_RESULT(CL_release(), "Failed to release CL context");

        return Halide::Runtime::Buffer<uint8_t>(buf);
    }

    void RawImageConsumer::releaseCameraPreviewOutputBuffer(Halide::Runtime::Buffer<uint8_t>& buffer) {
        cl_int errCode = 0;
        cl_context clContext = nullptr;
        cl_command_queue clQueue = nullptr;

        VERIFY_RESULT(CL_acquire(&clContext, &clQueue), "Failed to acquire CL context");

        auto clOutputBuffer = (cl_mem) halide_opencl_get_cl_mem(nullptr, buffer.raw_buffer());
        if(clOutputBuffer == nullptr) {
            throw RawPreviewException("Failed to get CL memory");
        }

        VERIFY_RESULT(halide_opencl_detach_cl_mem(nullptr, buffer.raw_buffer()), "Failed to detach CL memory from buffer");

        errCode = CL_releaseMemObject(clOutputBuffer);
        VERIFY_RESULT(errCode, "Failed to release camera preview buffer");

        CL_release();
    }

    Halide::Runtime::Buffer<uint8_t> RawImageConsumer::wrapCameraPreviewInputBuffer(const RawImageBuffer& buffer) {
        halide_buffer_t buf = {0};
        halide_dimension_t dim;

        buf.dim = &dim;
        buf.dimensions = 1;
        buf.dim[0].extent = buffer.data->len();
        buf.dim[0].stride = 1;
        buf.type = halide_type_of<uint8_t>();

        VERIFY_RESULT(
                halide_opencl_wrap_cl_mem(nullptr, &buf, (uint64_t) buffer.data->nativeHandle()),
                "Failed to wrap camera preview buffer");

        return Halide::Runtime::Buffer<uint8_t>(buf);
    }

    void RawImageConsumer::unwrapCameraPreviewInputBuffer(Halide::Runtime::Buffer<uint8_t>& buffer) {
        VERIFY_RESULT(
                halide_opencl_detach_cl_mem(nullptr, buffer.raw_buffer()),
                "Failed to unwrap camera preview buffer");
    }
#endif

    void RawImageConsumer::doPreprocess() {
#ifdef GPU_CAMERA_PREVIEW
        Halide::Runtime::Buffer<uint8_t> outputBuffer;
        std::shared_ptr<RawImageBuffer> buffer;

        std::chrono::steady_clock::time_point fpsTimestamp = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point previewTimestamp;

        cl_int errCode = -1;

        bool outputCreated = false;
        int downscaleFactor = mRawPreviewQuality;
        int processedFrames = 0;
        double totalPreviewTimeMs = 0;

        while(mEnableRawPreview) {
            if(!mPreprocessQueue.wait_dequeue_timed(buffer, std::chrono::milliseconds(100))) {
                continue;
            }

            if(!outputCreated) {
                outputBuffer = createCameraPreviewOutputBuffer(*buffer, downscaleFactor);
                outputCreated = true;
            }

            Halide::Runtime::Buffer<uint8_t> inputBuffer = wrapCameraPreviewInputBuffer(*buffer);

            previewTimestamp = std::chrono::steady_clock::now();

            motioncam::CameraPreview::generate(
                    *buffer,
                    mCameraDesc->metadata,
                    downscaleFactor,
                    mCameraDesc->lensFacing == ACAMERA_LENS_FACING_FRONT,
                    mPreviewShadows,
                    mEstimatedSettings.contrast,
                    mEstimatedSettings.saturation,
                    mEstimatedSettings.blacks,
                    mEstimatedSettings.whitePoint,
                    mTempOffset,
                    mTintOffset,
                    0.25f,
                    inputBuffer,
                    outputBuffer);

            totalPreviewTimeMs +=
                    std::chrono::duration <double, std::milli>(std::chrono::steady_clock::now() - previewTimestamp).count();

            unwrapCameraPreviewInputBuffer(inputBuffer);

            cl_context clContext = nullptr;
            cl_command_queue clQueue = nullptr;

            VERIFY_RESULT(CL_acquire(&clContext, &clQueue), "Failed to acquire CL context");

            auto clOutputBuffer = (cl_mem) halide_opencl_get_cl_mem(nullptr, outputBuffer.raw_buffer());
            auto data = CL_enqueueMapBuffer(
                    clQueue, clOutputBuffer, CL_TRUE, CL_MAP_READ, 0, outputBuffer.size_in_bytes(), 0, nullptr, nullptr, &errCode);

            VERIFY_RESULT(errCode, "Failed to map output buffer");

            mPreviewListener->onPreviewGenerated(data, outputBuffer.size_in_bytes(), outputBuffer.width(), outputBuffer.height());

            errCode = CL_enqueueUnmapMemObject(clQueue, clOutputBuffer, data, 0, nullptr, nullptr);
            VERIFY_RESULT(errCode, "Failed to unmap output buffer");

            VERIFY_RESULT(CL_release(), "Failed to release CL context");

            // Return buffer
            onBufferReady(buffer);

            processedFrames += 1;

            auto now = std::chrono::steady_clock::now();
            double durationMs = std::chrono::duration <double, std::milli>(now - fpsTimestamp).count();

            // Print camera FPS + stats
            if(durationMs > 3000.0f) {
                double avgProcessTimeMs = totalPreviewTimeMs / processedFrames;

                LOGI("Camera FPS: %d, cameraQuality=%d processTimeMs=%.2f", processedFrames / 3, downscaleFactor, avgProcessTimeMs);

                processedFrames = 0;
                totalPreviewTimeMs = 0;

                fpsTimestamp = now;
            }
        }

        if(outputCreated)
            releaseCameraPreviewOutputBuffer(outputBuffer);

        while(mPreprocessQueue.try_dequeue(buffer)) {
            RawBufferManager::get().discardBuffer(buffer);
        }
#endif
        LOGD("Exiting preprocess thread");
    }

    void RawImageConsumer::enableRawPreview(std::shared_ptr<RawPreviewListener> listener, const int previewQuality) {
        if(mEnableRawPreview)
            return;

        LOGI("Enabling RAW preview mode");

        mPreviewListener  = std::move(listener);
        mEnableRawPreview = true;
        mRawPreviewQuality = previewQuality;
        mPreprocessThread = std::make_shared<std::thread>(&RawImageConsumer::doPreprocess, this);
    }

    void RawImageConsumer::updateRawPreviewSettings(
            float shadowBoost, float contrast, float saturation, float blacks, float whitePoint, float tempOffset, float tintOffset)
    {
        mShadowBoost = shadowBoost;
        mEstimatedSettings.contrast = contrast;
        mEstimatedSettings.saturation = saturation;
        mEstimatedSettings.blacks = blacks;
        mEstimatedSettings.whitePoint = whitePoint;
        mTempOffset = tempOffset;
        mTintOffset = tintOffset;
    }

    void RawImageConsumer::getEstimatedSettings(PostProcessSettings& outSettings) {
        outSettings = mEstimatedSettings;
    }

    void RawImageConsumer::disableRawPreview() {
        if(!mEnableRawPreview)
            return;

        LOGI("Disabling RAW preview mode");

        mEnableRawPreview = false;
        mPreprocessThread->join();

        mPreprocessThread = nullptr;
        mPreviewListener.reset();
    }

    void RawImageConsumer::setWhiteBalanceOverride(bool override) {
        // TODO This doesn't do anything yet
        mOverrideWhiteBalance = override;
    }

    void RawImageConsumer::doCopyImage() {
        while(mRunning) {
            std::shared_ptr<AImage> pendingImage = nullptr;

            // Wait for image
            if(!mImageQueue.wait_dequeue_timed(pendingImage, std::chrono::milliseconds(100))) {
                // Try to match buffers even if no image has been added
                doMatchMetadata();
                continue;
            }

            if(!pendingImage)
                continue;

            std::shared_ptr<RawImageBuffer> dst, previewBuffer;

            // Lock and get an image out
            if(!mSetupBuffersThread) {
                int length = 0;
                uint8_t* data = nullptr;

                // Get size of buffer
                if(AImage_getPlaneData(pendingImage.get(), 0, &data, &length) != AMEDIA_OK) {
                    LOGE("Failed to get size of camera buffer!");
                }
                else {
                    mSetupBuffersThread = std::make_shared<std::thread>(&RawImageConsumer::doSetupBuffers, this, static_cast<size_t>(length));
                }

                // Give the buffers thread a chance to create some buffers before we try to get one below.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            dst = RawBufferManager::get().dequeueUnusedBuffer();

            // If there are no buffers available, we can't do anything useful here
            if(!dst) {
                LOGW("Out of buffers");
                continue;
            }

            // Reset buffer
            dst->width     = 0;
            dst->height    = 0;
            dst->rowStride = 0;
            dst->metadata.timestampNs = 0;

            //
            // Copy buffer
            //

            int32_t format      = 0;
            int32_t width       = 0;
            int32_t height      = 0;
            int32_t rowStride   = 0;
            int64_t timestamp   = 0;
            uint8_t* data       = nullptr;
            int length          = 0;
            bool result         = true;

            // Copy frame metadata
            const AImage* image = pendingImage.get();

            result &= (AImage_getFormat(image, &format)                 == AMEDIA_OK);
            result &= (AImage_getWidth(image, &width)                   == AMEDIA_OK);
            result &= (AImage_getHeight(image, &height)                 == AMEDIA_OK);
            result &= (AImage_getPlaneRowStride(image, 0, &rowStride)   == AMEDIA_OK);
            result &= (AImage_getTimestamp(image, &timestamp)                   == AMEDIA_OK);
            result &= (AImage_getPlaneData(image, 0, &data, &length)    == AMEDIA_OK);

            // Copy raw data if were able to acquire it successfully
            if(result) {
                switch(format) {
                    default:
                    case AIMAGE_FORMAT_RAW10:
                        dst->pixelFormat = PixelFormat::RAW10;
                        break;

                    case AIMAGE_FORMAT_RAW12:
                        dst->pixelFormat = PixelFormat::RAW12;
                        break;

                    case AIMAGE_FORMAT_RAW16:
                        dst->pixelFormat = PixelFormat::RAW16;
                        break;

                    case AIMAGE_FORMAT_YUV_420_888:
                        dst->pixelFormat = PixelFormat::YUV_420_888;
                        break;
                }

                dst->width                  = width;
                dst->height                 = height;
                dst->rowStride              = rowStride;
                dst->metadata.timestampNs   = timestamp;

                if(dst->data->len() != length) {
                    LOGE("Unexpected buffer size!!");
                }
                else {
                    auto dstBuffer = dst->data->lock(true);

                    if(dstBuffer)
                        std::copy(data, data + length, dstBuffer);

                    dst->data->unlock();
                }
            }
            else {
                LOGW("Failed to copy image!");
            }

            // Insert back
            if(!result) {
                LOGW("Got error, discarding buffer");
                RawBufferManager::get().discardBuffer(dst);
            }
            else {
                auto imageIt = mPendingBuffers.find(timestamp);

                if (imageIt != mPendingBuffers.end()) {
                    LOGW("Pending timestamp already exists!");

                    RawBufferManager::get().discardBuffer(imageIt->second);
                    mPendingBuffers.erase(imageIt);
                }

                mPendingBuffers.insert(std::make_pair(timestamp, dst));
            }

            // Match buffers
            doMatchMetadata();
        }

        // Clear pending buffers
        std::shared_ptr<AImage> pendingImage = nullptr;

        while(mImageQueue.try_dequeue(pendingImage)) {
        }

        // Stop setup buffers thread and return all pending buffers
        std::shared_ptr<std::thread> bufferThread;

        // Return all pending buffers
        auto it = mPendingBuffers.begin();
        while(it != mPendingBuffers.end()) {
            RawBufferManager::get().discardBuffer(it->second);
            ++it;
        }

        mPendingBuffers.clear();

        if(mSetupBuffersThread)
            mSetupBuffersThread->join();

        mSetupBuffersThread = nullptr;

        LOGD("Exiting copy thread");
    }
}
