#ifndef RawBufferManager_hpp
#define RawBufferManager_hpp

#include "motioncam/RawImageMetadata.h"

#include <queue/concurrentqueue.h>
#include <set>
#include <vector>
#include <memory>
#include <mutex>

namespace motioncam {
    class RawContainer;

    class RawBufferManager {
    public:
        // Not copyable
        RawBufferManager(const RawBufferManager&) = delete;
        RawBufferManager& operator=(const RawBufferManager&) = delete;

        static RawBufferManager& get() {
            static RawBufferManager instance;
            return instance;
        }
        
        struct LockedBuffers {
        public:
            ~LockedBuffers();
            std::vector<std::shared_ptr<RawImageBuffer>> getBuffers() const;
            
            friend class RawBufferManager;

        private:
            LockedBuffers(std::vector<std::shared_ptr<RawImageBuffer>> buffers);
            LockedBuffers();
            
            const std::vector<std::shared_ptr<RawImageBuffer>> mBuffers;
        };
        
        void addBuffer(std::shared_ptr<RawImageBuffer>& buffer);
        int memoryUseBytes() const;
        int numBuffers() const;
        void reset();

        std::shared_ptr<RawImageBuffer> dequeueUnusedBuffer();
        void enqueueReadyBuffer(const std::shared_ptr<RawImageBuffer>& buffer);
        void discardBuffer(const std::shared_ptr<RawImageBuffer>& buffer);
        void discardBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers);
        void returnBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers);

        int numHdrBuffers();
        
        std::shared_ptr<RawContainer> peekPendingContainer();
        void clearPendingContainer();
        
        std::unique_ptr<LockedBuffers> consumeLatestBuffer();
        std::unique_ptr<LockedBuffers> consumeAllBuffers();
        std::unique_ptr<LockedBuffers> consumeBuffer(int64_t timestampNs);
        
        void saveHdr(RawCameraMetadata& metadata,
                     const PostProcessSettings& settings,
                     const std::string& outputPath);

        void save(RawCameraMetadata& metadata,
                  int64_t referenceTimestamp,
                  int numSaveBuffers,
                  const bool writeDNG,
                  const PostProcessSettings& settings,
                  const std::string& outputPath);
        
    private:
        RawBufferManager();

        std::atomic<int> mMemoryUseBytes;
        std::atomic<int> mNumBuffers;
                
        std::recursive_mutex mMutex;
        
        std::vector<std::shared_ptr<RawImageBuffer>> mReadyBuffers;

        moodycamel::ConcurrentQueue<std::shared_ptr<RawImageBuffer>> mUnusedBuffers;
        std::shared_ptr<RawContainer> mPendingContainer;
    };

} // namespace motioncam

#endif // RawBufferManager_hpp
