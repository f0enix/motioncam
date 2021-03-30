#include "motioncam/RawBufferManager.h"

#include <utility>
#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"

namespace motioncam {

    RawBufferManager::RawBufferManager() :
        mMemoryUseBytes(0),
        mNumBuffers(0)
    {
    }

    RawBufferManager::LockedBuffers::LockedBuffers() = default;
    RawBufferManager::LockedBuffers::LockedBuffers(
        std::vector<std::shared_ptr<RawImageBuffer>> buffers,
        bool returnBuffers) : mBuffers(std::move(buffers)), mReturnBuffers(returnBuffers) {}

    std::vector<std::shared_ptr<RawImageBuffer>> RawBufferManager::LockedBuffers::getBuffers() const {
        return mBuffers;
    }

    RawBufferManager::LockedBuffers::~LockedBuffers() {
        if(mReturnBuffers)
            RawBufferManager::get().returnBuffers(mBuffers);
        else
            RawBufferManager::get().discardBuffers(mBuffers);
    }

    void RawBufferManager::addBuffer(std::shared_ptr<RawImageBuffer>& buffer) {
        mUnusedBuffers.enqueue(buffer);
        
        ++mNumBuffers;
        mMemoryUseBytes += buffer->data->len();
    }

    int RawBufferManager::numBuffers() const {
        return mNumBuffers;
    }

    int RawBufferManager::memoryUseBytes() const {
        return mMemoryUseBytes;
    }

    std::shared_ptr<RawImageBuffer> RawBufferManager::dequeueUnusedBuffer() {
        std::shared_ptr<RawImageBuffer> buffer;

        if(mUnusedBuffers.try_dequeue(buffer))
            return buffer;
        
        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);

            if(!mReadyBuffers.empty()) {
                buffer = mReadyBuffers.front();
                mReadyBuffers.erase(mReadyBuffers.begin());

                return buffer;
            }
        }

        return nullptr;
    }

    void RawBufferManager::enqueueReadyBuffer(const std::shared_ptr<RawImageBuffer>& buffer) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        
        mReadyBuffers.push_back(buffer);
    }

    void RawBufferManager::discardBuffer(const std::shared_ptr<RawImageBuffer>& buffer) {
        mUnusedBuffers.enqueue(buffer);
    }

    void RawBufferManager::discardBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers) {
        mUnusedBuffers.enqueue_bulk(buffers.begin(), buffers.size());
    }

    void RawBufferManager::returnBuffers(const std::vector<std::shared_ptr<RawImageBuffer>>& buffers) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        std::move(buffers.begin(), buffers.end(), std::back_inserter(mReadyBuffers));
    }

    void RawBufferManager::save(RawCameraMetadata& metadata,
                                int64_t referenceTimestamp,
                                int numSaveBuffers,
                                const bool writeDNG,
                                const PostProcessSettings& settings,
                                const std::string& outputPath)
    {
        std::vector<std::shared_ptr<RawImageBuffer>> allBuffers;
        std::vector<std::shared_ptr<RawImageBuffer>> buffers;
        
        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);
            
            if(mReadyBuffers.empty())
                return;
    
            allBuffers = mReadyBuffers;
            mReadyBuffers.clear();
        }

        // Find reference frame
        int referenceIdx = static_cast<int>(allBuffers.size()) - 1;

        for(int i = 0; i < allBuffers.size(); i++) {
            if(allBuffers[i]->metadata.timestampNs == referenceTimestamp) {
                referenceIdx = i;
                buffers.push_back(allBuffers[i]);
                break;
            }
        }

        // Update timestamp
        referenceTimestamp = allBuffers[referenceIdx]->metadata.timestampNs;

        // Add closest images
        int leftIdx  = referenceIdx - 1;
        int rightIdx = referenceIdx + 1;

        while(numSaveBuffers > 0 && (leftIdx > 0 || rightIdx < allBuffers.size())) {
            int64_t leftDifference = std::numeric_limits<long>::max();
            int64_t rightDifference = std::numeric_limits<long>::max();

            if(leftIdx >= 0)
                leftDifference = std::abs(allBuffers[leftIdx]->metadata.timestampNs - allBuffers[referenceIdx]->metadata.timestampNs);

            if(rightIdx < mReadyBuffers.size())
                rightDifference = std::abs(allBuffers[rightIdx]->metadata.timestampNs - allBuffers[referenceIdx]->metadata.timestampNs);

            // Add closest buffer to reference
            if(leftDifference < rightDifference) {
                buffers.push_back(allBuffers[leftIdx]);
                --leftIdx;
            }
            else {
                buffers.push_back(allBuffers[rightIdx]);
                ++rightIdx;
            }

            --numSaveBuffers;
        }

        // Removed matched buffers
        auto predicate = [&](const auto& key) -> bool {
              return std::find(buffers.begin(), buffers.end(), key) != buffers.end();
        };
        
        allBuffers.erase(std::remove_if(allBuffers.begin(), allBuffers.end(), predicate), allBuffers.end());

        // Construct container and save
        if(!buffers.empty()) {
            std::vector<std::string> frames;
            std::map<std::string, std::shared_ptr<RawImageBuffer>> frameBuffers;

            auto it = buffers.begin();
            int filenameIdx = 0;

            while(it != buffers.end()) {
                std::string filename = "frame" + std::to_string(filenameIdx) + ".raw";

                frames.push_back(filename);
                frameBuffers[filename] = *it;

                ++it;
                ++filenameIdx;
            }
            
            std::unique_ptr<LockedBuffers> lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers(buffers, false));
            
            auto rawContainer = std::make_shared<RawContainer>(
                metadata,
                settings,
                referenceTimestamp,
                false,
                writeDNG,
                frames,
                frameBuffers,
                std::move(lockedBuffers));
            
            mPendingContainers.enqueue(rawContainer);
        }
                        
        // Return buffers
        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);

            mReadyBuffers = allBuffers;
        }
    }

    std::shared_ptr<RawContainer> RawBufferManager::dequeuePendingContainer() {
        std::shared_ptr<RawContainer> result;

        mPendingContainers.try_dequeue(result);

        return result;
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::lockBuffer(int64_t timestampNs, bool returnBuffers) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        auto it = std::find_if(
            mReadyBuffers.begin(), mReadyBuffers.end(),
            [&](const auto& x) { return x->metadata.timestampNs == timestampNs; }
         );
        
        if(it != mReadyBuffers.end()) {
            auto lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers( { *it }, returnBuffers ));
            mReadyBuffers.erase(it);
            
            return lockedBuffers;
        }
        
        return std::unique_ptr<LockedBuffers>();
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::lockNumBuffers(int numBuffers, bool returnBuffers) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        numBuffers = std::min(static_cast<int>(mReadyBuffers.size()), numBuffers);

        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        std::move(mReadyBuffers.end() - numBuffers, mReadyBuffers.end(), std::back_inserter(buffers));
        mReadyBuffers.erase(mReadyBuffers.end() - numBuffers, mReadyBuffers.end());

        return std::unique_ptr<LockedBuffers>(new LockedBuffers(buffers, returnBuffers));
    }
    
    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::lockAllBuffers(bool returnBuffers) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        auto lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers(mReadyBuffers, returnBuffers));
        
        mReadyBuffers.clear();
        
        return lockedBuffers;
    }
}
