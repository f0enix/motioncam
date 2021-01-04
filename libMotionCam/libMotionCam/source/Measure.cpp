#include "motioncam/Measure.h"

#include <utility>
#include "motioncam/Logger.h"

namespace motioncam {
    Measure::Measure(std::string reference) :
        mReference(std::move(reference)),
        mTimestamp(std::chrono::steady_clock::now()) {
            
        logger::log(mReference);
    }
    
    Measure::~Measure() {
        auto now = std::chrono::steady_clock::now();
        double durationMs = std::chrono::duration <double, std::milli>(now - mTimestamp).count();
        logger::log(mReference + " - taken " + std::to_string(durationMs) +  " ms");
    }
}
