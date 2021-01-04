#ifndef Math_hpp
#define Math_hpp

#include <algorithm>
#include <numeric>
#include <opencv2/opencv.hpp>

#define COMPARE_SWAP(a, b, tmp) tmp = cv::min(a, b); b = cv::max(a, b); a = tmp;
#define COMPARE_SWAP_V(a, b, tmp) tmp = cv::v_min(a, b); b = cv::v_max(a, b); a = tmp;

namespace motioncam {
    namespace math {
        
        template<typename T>
        inline static T clamp(T min, T x, T max)
        {
            return std::max(min, std::min(x, max));
        }
        
        inline static float max(const cv::Vec3f& coord) {
            return std::max({coord[0], coord[1], coord[2]});
        }

        inline static float max(const cv::Vec4f& coord) {
            return std::max({coord[0], coord[1], coord[2], coord[3]});
        }

        template<typename T>
        inline static T mean(std::vector<T> v, T initialSum) {
            T sum = std::accumulate(v.begin(), v.end(), initialSum);
            return sum / v.size();
        }
        
        inline static float gaussian(float x, float mean, float variance) {
            float s = ((x - mean) * (x - mean)) / (2 * (variance*variance));            
            return exp(-s);
        }
    }
}

#endif /* Math_hpp */
