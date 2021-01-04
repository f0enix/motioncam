#ifndef RawBufferManager_hpp
#define RawBufferManager_hpp

#include "motioncam/RawImageMetadata.h"

#include <set>
#include <vector>
#include <memory>

namespace motioncam {
    class RawBufferManager {
    public:
        RawBufferManager();

        // Not copyable
        RawBufferManager(const RawBufferManager&) = delete;
        RawBufferManager& operator=(const RawBufferManager&) = delete;

        void addBuffers(std::vector<std::shared_ptr<RawImageBuffer>>& buffer);
        int memoryUseBytes() const;

        std::shared_ptr<RawImageBuffer> allocateBuffer();
        void returnBuffer(const std::shared_ptr<RawImageBuffer>& buffer);
        void discardBuffer(const std::shared_ptr<RawImageBuffer>& buffer);

        std::shared_ptr<RawImageBuffer> lockLatest();
        
        void lock();
        std::shared_ptr<RawImageBuffer> getBuffer(int64_t timestamp);
        std::vector<std::shared_ptr<RawImageBuffer>> getBuffers();
        void unlock();

        int numBuffers() const;

    private:
        int mMemoryUseBytes;
        int mNumBuffers;

        struct BufferCompare {
            bool operator() (const std::shared_ptr<RawImageBuffer>& lhs, const std::shared_ptr<RawImageBuffer>& rhs) const {
                return lhs->metadata.timestampNs < rhs->metadata.timestampNs;
            }
        };
        
        std::set<std::shared_ptr<RawImageBuffer>, BufferCompare> mBuffers;

        std::vector<std::shared_ptr<RawImageBuffer>> mUnusedBuffers;
        std::vector<std::shared_ptr<RawImageBuffer>> mLockedBuffers;
    };

} // namespace motioncam

#endif // RawBufferManager_hpp
