#include "motioncam/RawBufferManager.h"

namespace motioncam {

    RawBufferManager::RawBufferManager() :
        mMemoryUseBytes(0),
        mNumBuffers(0)
    {
    }

    void RawBufferManager::addBuffers(std::vector<std::shared_ptr<RawImageBuffer>>& buffers) {
        auto it = buffers.begin();

        while(it != buffers.end()) {
            mUnusedBuffers.push_back(*it);
            mMemoryUseBytes += (*it)->data->len();

            ++mNumBuffers;
            ++it;
        }

        buffers.clear();
    }

    int RawBufferManager::numBuffers() const {
        return mNumBuffers;
    }

    int RawBufferManager::memoryUseBytes() const {
        return mMemoryUseBytes;
    }

    std::shared_ptr<RawImageBuffer> RawBufferManager::allocateBuffer() {
        // Easiest case we have unused buffers available
        if(!mUnusedBuffers.empty()) {
            auto buffer = mUnusedBuffers.back();
            mUnusedBuffers.pop_back();

            return buffer;
        }

        // Check short term buffers
        if(!mBuffers.empty()) {
            auto oldestIt = mBuffers.begin();
            auto oldest = *oldestIt;
            
            mBuffers.erase(oldestIt);

            return oldest;
        }

        // Nothing available
        return nullptr;
    }

    void RawBufferManager::returnBuffer(const std::shared_ptr<RawImageBuffer>& buffer) {
        // We don't allow any duplicate buffers
        if(mBuffers.find(buffer) != mBuffers.end()) {
            discardBuffer(buffer);
            return;
        }

        mBuffers.insert(buffer);
    }

    void RawBufferManager::discardBuffer(const std::shared_ptr<RawImageBuffer>& buffer) {
        mUnusedBuffers.push_back(buffer);
    }

    void RawBufferManager::lock() {
        // Move buffers to locked list
        mLockedBuffers.insert(mLockedBuffers.end(), mBuffers.begin(), mBuffers.end());
        
        mBuffers.clear();
    }

    void RawBufferManager::unlock() {
        // Move from locked list to long term
        std::copy(mLockedBuffers.begin(), mLockedBuffers.end(), std::inserter(mUnusedBuffers, mUnusedBuffers.end()));

        mLockedBuffers.clear();
    }

    std::shared_ptr<RawImageBuffer> RawBufferManager::getBuffer(int64_t timestamp) {
        auto it = mLockedBuffers.begin();

        while(it != mLockedBuffers.end()) {
            if((*it)->metadata.timestampNs == timestamp) {
                return *it;
            }

            ++it;
        }

        return nullptr;
    }

    std::vector<std::shared_ptr<RawImageBuffer>> RawBufferManager::getBuffers() {
        return mLockedBuffers;
    }

    std::shared_ptr<RawImageBuffer> RawBufferManager::lockLatest() {
        if(mBuffers.empty())
            return nullptr;
        
        auto newestIt = --mBuffers.end();
        auto latestBuffer = (*newestIt);

        mBuffers.erase(newestIt);
        mLockedBuffers.push_back(latestBuffer);

        return *newestIt;
    }
}
