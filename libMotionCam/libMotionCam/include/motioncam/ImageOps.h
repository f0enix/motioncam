#ifndef ImageOps_hpp
#define ImageOps_hpp

#include <math.h>
#include <vector>

#include "Math.h"

namespace motioncam {
    
    float estimateNoise(cv::Mat& input);
    
    float findMedian(cv::Mat& input, float p=0.5);
    float findMedian(std::vector<float> nums);
    
    float calculateEnergy(cv::Mat& image);
}

#endif /* ImageOps_hpp */
