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
            LockedBuffers(std::vector<std::shared_ptr<RawImageBuffer>> buffers, bool returnBuffers);
            LockedBuffers();
            
            const std::vector<std::shared_ptr<RawImageBuffer>> mBuffers;
            bool mReturnBuffers;
        };
        
        void addBuffer(std::shared_ptr<RawImageBuffer>& buffer);
        int memoryUseBytes() const;
        int numBuffers() const;

        std::shared_ptr<RawImageBuffer> dequeueUnusedBuffer();
        void enqueueReadyBuffer(const std::shared_ptr<RawImageBuffer>& buffer);
        void discardBuffer(const std::shared_ptr<RawImageBuffer>& buffer);
        void discardBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers);
        void returnBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers);

        std::shared_ptr<RawContainer> dequeuePendingContainer();
        
        std::unique_ptr<LockedBuffers> lockNumBuffers(int numBuffers, bool returnBuffers);
        std::unique_ptr<LockedBuffers> lockAllBuffers(bool returnBuffers);
        std::unique_ptr<LockedBuffers> lockBuffer(int64_t timestampNs, bool returnBuffers);
        
        void save(RawCameraMetadata& metadata,
                  int64_t referenceTimestamp,
                  int numSaveBuffers,
                  const bool writeDNG,
                  const PostProcessSettings& settings,
                  const std::string& outputPath);
        
    private:
        RawBufferManager();

        int mMemoryUseBytes;
        int mNumBuffers;
        
        std::recursive_mutex mMutex;
        
        std::vector<std::shared_ptr<RawImageBuffer>> mReadyBuffers;
        moodycamel::ConcurrentQueue<std::shared_ptr<RawImageBuffer>> mUnusedBuffers;
        moodycamel::ConcurrentQueue<std::shared_ptr<RawContainer>> mPendingContainers;
    };

} // namespace motioncam

#endif // RawBufferManager_hpp
