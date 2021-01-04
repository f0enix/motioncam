#include "motioncam/ImageOps.h"
#include "motioncam/Measure.h"

using std::vector;

namespace motioncam {    

    float findMedian(std::vector<float> nums) {
        std::nth_element(nums.begin(), nums.begin() + nums.size() / 2, nums.end());
        
        return nums[nums.size()/2];
    }

    float findMedian(cv::Mat& input, float p) {
        std::vector<float> nums;
        nums.reserve(input.cols * input.rows);
        
        for (int i = 0; i < input.rows; ++i) {
            nums.insert(nums.end(), input.ptr<float>(i), input.ptr<float>(i) + input.cols);
        }

        if(p <= 0.5)
            std::nth_element(nums.begin(), nums.begin() + nums.size() / 2, nums.end());
        else
            std::sort(nums.begin(), nums.end());
        
        return nums[nums.size()*p];
    }
    
    float estimateNoise(cv::Mat& input) {
        cv::Mat d = cv::abs(input);
        float mad = findMedian(d);

        return mad / 0.6745;
    }
        
    float calculateEnergy(cv::Mat& image) {
        cv::Mat tmp;
        
        cv::Laplacian(image, tmp, CV_8U);
        cv::Scalar energy = cv::mean(tmp);
        
        return energy[0];
    }
}
