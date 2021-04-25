#include "motioncam/RawBufferManager.h"

#include <utility>
#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/Logger.h"
#include "motioncam/Measure.h"

namespace motioncam {

    RawBufferManager::RawBufferManager() :
        mMemoryUseBytes(0),
        mNumBuffers(0)
    {
    }

    RawBufferManager::LockedBuffers::LockedBuffers() = default;
    RawBufferManager::LockedBuffers::LockedBuffers(
        std::vector<std::shared_ptr<RawImageBuffer>> buffers) : mBuffers(std::move(buffers)) {}

    std::vector<std::shared_ptr<RawImageBuffer>> RawBufferManager::LockedBuffers::getBuffers() const {
        return mBuffers;
    }

    RawBufferManager::LockedBuffers::~LockedBuffers() {
        RawBufferManager::get().returnBuffers(mBuffers);
    }

    void RawBufferManager::addBuffer(std::shared_ptr<RawImageBuffer>& buffer) {
        mUnusedBuffers.enqueue(buffer);
        
        ++mNumBuffers;
        mMemoryUseBytes += static_cast<int>(buffer->data->len());
    }

    int RawBufferManager::numBuffers() const {
        return mNumBuffers;
    }

    int RawBufferManager::memoryUseBytes() const {
        return mMemoryUseBytes;
    }

    void RawBufferManager::reset() {
        std::shared_ptr<RawImageBuffer> buffer;
        while(mUnusedBuffers.try_dequeue(buffer)) {
        }

        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);
            mReadyBuffers.clear();
        }
        
        mNumBuffers = 0;
        mMemoryUseBytes = 0;
    }

    std::shared_ptr<RawImageBuffer> RawBufferManager::dequeueUnusedBuffer() {
        std::shared_ptr<RawImageBuffer> buffer;

        if(mUnusedBuffers.try_dequeue(buffer)) {
            return buffer;
        }
        
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

    int RawBufferManager::numHdrBuffers() {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        
        int hdrBuffers = 0;
        
        for(auto& e : mReadyBuffers) {
            if(e->metadata.rawType == RawType::HDR) {
                ++hdrBuffers;
            }
        }
        
        return hdrBuffers;
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

    void RawBufferManager::save(
            RawType type,
            int numSaveBuffers,
            bool writeDNG,
            const RawCameraMetadata& metadata,
            const PostProcessSettings& settings,
            const std::string& outputPath)
    {
        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);

            if (mReadyBuffers.empty())
                return;

            auto it = mReadyBuffers.begin();

            while (it != mReadyBuffers.end() && numSaveBuffers > 0) {
                if((*it)->metadata.rawType == type) {
                    buffers.push_back(*it);
                    it = mReadyBuffers.erase(it);

                    --numSaveBuffers;
                }
                else {
                    ++it;
                }
            }

            // If we didn't have enough of the requested type, use ZSL buffers
            auto rit = mReadyBuffers.rbegin();

            while(rit != mReadyBuffers.rend() && numSaveBuffers > 0) {
                if((*rit)->metadata.rawType == RawType::ZSL) {
                    buffers.push_back(*rit);
                    rit = decltype(rit)(mReadyBuffers.erase( std::next(rit).base() ));

                    --numSaveBuffers;
                }
                else {
                    ++rit;
                }
            }
        }

        // Copy the buffers
        auto rawContainer = std::make_shared<RawContainer>(
                metadata,
                settings,
                -1,
                type == RawType::HDR,
                writeDNG,
                buffers);

        // Return buffers
        auto it = buffers.begin();
        while(it != buffers.end()) {
            mUnusedBuffers.enqueue(*it);
            ++it;
        }

        // Save the container
        if(mPendingContainers.size_approx() > 0) {
            rawContainer->save(outputPath);
        }
        else {
            mPendingContainers.enqueue(rawContainer);
        }
    }

    void RawBufferManager::save(RawCameraMetadata& metadata,
                                int64_t referenceTimestamp,
                                int numSaveBuffers,
                                const bool writeDNG,
                                const PostProcessSettings& settings,
                                const std::string& outputPath)
    {
        Measure measure("RawBufferManager::save()");
        
        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);

            if(mReadyBuffers.empty())
                return;

            // Find reference frame
            int referenceIdx = static_cast<int>(mReadyBuffers.size()) - 1;

            for(int i = 0; i < mReadyBuffers.size(); i++) {
                if(mReadyBuffers[i]->metadata.timestampNs == referenceTimestamp) {
                    referenceIdx = i;
                    buffers.push_back(mReadyBuffers[i]);
                    break;
                }
            }

            // Update timestamp
            referenceTimestamp = mReadyBuffers[referenceIdx]->metadata.timestampNs;

            // Add closest images
            int leftIdx  = referenceIdx - 1;
            int rightIdx = referenceIdx + 1;

            while(numSaveBuffers > 0 && (leftIdx > 0 || rightIdx < mReadyBuffers.size())) {
                int64_t leftDifference = std::numeric_limits<long>::max();
                int64_t rightDifference = std::numeric_limits<long>::max();

                if(leftIdx >= 0)
                    leftDifference = std::abs(mReadyBuffers[leftIdx]->metadata.timestampNs - mReadyBuffers[referenceIdx]->metadata.timestampNs);

                if(rightIdx < mReadyBuffers.size())
                    rightDifference = std::abs(mReadyBuffers[rightIdx]->metadata.timestampNs - mReadyBuffers[referenceIdx]->metadata.timestampNs);

                // Add closest buffer to reference
                if(leftDifference < rightDifference) {
                    buffers.push_back(mReadyBuffers[leftIdx]);
                    mReadyBuffers[leftIdx] = nullptr;

                    --leftIdx;
                }
                else {
                    buffers.push_back(mReadyBuffers[rightIdx]);
                    mReadyBuffers[rightIdx] = nullptr;

                    ++rightIdx;
                }

                --numSaveBuffers;
            }

            // Clear the reference frame
            mReadyBuffers[referenceIdx] = nullptr;

            // Clear out buffers we intend to copy
            auto it = mReadyBuffers.begin();
            while(it != mReadyBuffers.end()) {
                if(*it == nullptr) {
                    it = mReadyBuffers.erase(it);
                }
                else {
                    ++it;
                }
            }
        }

        // Copy the buffers
        auto rawContainer = std::make_shared<RawContainer>(
                metadata,
                settings,
                referenceTimestamp,
                false,
                writeDNG,
                buffers);

        // Return buffers
        {
            std::lock_guard<std::recursive_mutex> lock(mMutex);
            mReadyBuffers.insert(mReadyBuffers.end(), buffers.begin(), buffers.end());
        }

        // Save container
        if(mPendingContainers.size_approx() > 0) {
            rawContainer->save(outputPath);
        }
        else {
            mPendingContainers.enqueue(rawContainer);
        }
    }

    std::shared_ptr<RawContainer> RawBufferManager::popPendingContainer() {
        std::shared_ptr<RawContainer> container;
        mPendingContainers.try_dequeue(container);
        
        return container;
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::consumeLatestBuffer() {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        if(mReadyBuffers.empty()) {
            return std::unique_ptr<LockedBuffers>(new LockedBuffers());
        }

        std::vector<std::shared_ptr<RawImageBuffer>> buffers;

        std::move(mReadyBuffers.end() - 1, mReadyBuffers.end(), std::back_inserter(buffers));
        mReadyBuffers.erase(mReadyBuffers.end() - 1, mReadyBuffers.end());

        return std::unique_ptr<LockedBuffers>(new LockedBuffers(buffers));
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::consumeBuffer(int64_t timestampNs) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        auto it = std::find_if(
            mReadyBuffers.begin(), mReadyBuffers.end(),
            [&](const auto& x) { return x->metadata.timestampNs == timestampNs; }
        );

        if(it != mReadyBuffers.end()) {
            auto lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers( { *it }));
            mReadyBuffers.erase(it);

            return lockedBuffers;
        }

        return std::unique_ptr<LockedBuffers>(new LockedBuffers());
    }

    std::unique_ptr<RawBufferManager::LockedBuffers> RawBufferManager::consumeAllBuffers() {
        std::lock_guard<std::recursive_mutex> lock(mMutex);

        auto lockedBuffers = std::unique_ptr<LockedBuffers>(new LockedBuffers(mReadyBuffers));
        mReadyBuffers.clear();

        return lockedBuffers;
    }
}
