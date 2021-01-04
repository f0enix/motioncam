#ifndef Measure_hpp
#define Measure_hpp

#include <chrono>
#include <string>

namespace motioncam {
    
    class Measure {
    public:
        Measure(std::string  reference);
        virtual ~Measure();
        
    private:
        std::string mReference;
        std::chrono::steady_clock::time_point mTimestamp;
    };
}

#endif /* Measure_hpp */
