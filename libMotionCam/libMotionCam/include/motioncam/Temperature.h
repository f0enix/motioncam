#ifndef Temperature_hpp
#define Temperature_hpp

#include "Types.h"

#include <string>

namespace motioncam {
    
    //
    // This code is adapted from the DNG SDK
    //
    class Temperature {
    public:
        Temperature();
        Temperature(double temperature, double tint);
        Temperature(const XYCoord& xyCoord);

        double temperature() const {
            return mTemperature;
        }

        double tint() const {
            return mTint;
        }
        
        XYCoord getXyCoord() const;
        
    private:
        double mTemperature;
        double mTint;
    };
}

#endif /* Temperature_hpp */
