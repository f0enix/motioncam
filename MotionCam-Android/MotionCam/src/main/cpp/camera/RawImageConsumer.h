#ifndef RawImageConsumer_hpp
#define RawImageConsumer_hpp

#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <chrono>

#include <motioncam/RawImageMetadata.h>

#include <HalideBuffer.h>
#include <queue/blockingconcurrentqueue.h>
#include <atomic_queue/atomic_queue.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImage.h>

namespace motioncam {
    // Forward declarations
    class RawBufferManager;
    class RawPreviewListener;

    struct CameraDescription;
    struct RawImageMetadata;
    struct RawImageBuffer;
    struct PostProcessSettings;

    class RawImageConsumer {
    public:
        RawImageConsumer(std::shared_ptr<CameraDescription> cameraDescription, const size_t maxMemoryUsageBytes);
        ~RawImageConsumer();

        void start();
        void stop();

        void queueImage(AImage* image);
        void queueMetadata(const ACameraMetadata* metadata, ScreenOrientation screenOrientation);

        void save(int64_t referenceTimestamp, int numSaveBuffers, const bool writeDNG, const PostProcessSettings& settings, const std::string& outputPath);

        void lockBuffers();
        std::vector<std::shared_ptr<RawImageBuffer>> getBuffers();
        std::shared_ptr<RawImageBuffer> getBuffer(int64_t timestamp);
        std::shared_ptr<RawImageBuffer> lockLatest();
        void unlockBuffers();

        void enableRawPreview(std::shared_ptr<RawPreviewListener> listener);
        void updateRawPreviewSettings(float shadows, float contrast, float saturation, float blacks, float whitePoint);
        void disableRawPreview();

    private:
        static bool copyMetadata(RawImageMetadata& dst, const ACameraMetadata* src);
        void onBufferReady(const std::shared_ptr<RawImageBuffer>& buffer);

        void doSetupBuffers(size_t bufferLength);
        void doCopyImage();
        void doMatchMetadata();
        void doPreprocess();

        static Halide::Runtime::Buffer<uint8_t> createCameraPreviewOutputBuffer(const RawImageBuffer& buffer, const int downscaleFactor);
        static void releaseCameraPreviewOutputBuffer(Halide::Runtime::Buffer<uint8_t>& buffer);

        static Halide::Runtime::Buffer<uint8_t> wrapCameraPreviewInputBuffer(const RawImageBuffer& buffer);
        static void unwrapCameraPreviewInputBuffer(Halide::Runtime::Buffer<uint8_t>& buffer);

    private:
        size_t mMaximumMemoryUsageBytes;
        std::unique_ptr<RawBufferManager> mBufferManager;
        std::vector<std::shared_ptr<std::thread>> mConsumerThreads;
        std::shared_ptr<std::thread> mSetupBuffersThread;
        std::shared_ptr<std::thread> mPreprocessThread;
        std::atomic<bool> mRunning;
        std::atomic<bool> mEnableRawPreview;

        std::atomic<float> mShadows;
        std::atomic<float> mContrast;
        std::atomic<float> mSaturation;
        std::atomic<float> mBlacks;
        std::atomic<float> mWhitePoint;

        std::shared_ptr<CameraDescription> mCameraDesc;

        std::mutex mBufferMutex;

        moodycamel::BlockingConcurrentQueue<std::shared_ptr<AImage>> mImageQueue;
        atomic_queue::AtomicQueue2<std::shared_ptr<RawImageBuffer>, 2> mPreprocessQueue;

        std::vector<RawImageMetadata> mPendingMetadata;
        std::map<int64_t, std::shared_ptr<RawImageBuffer>> mPendingBuffers;

        std::shared_ptr<RawPreviewListener> mPreviewListener;
    };
}

#endif /* RawImageConsumer_hpp */
