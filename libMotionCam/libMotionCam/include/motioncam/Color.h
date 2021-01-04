#ifndef Color_hpp
#define Color_hpp

#include "Types.h"

namespace motioncam {
    namespace color {

        extern const XYCoord D50XYCoord;
        
        enum Illuminant {
            StandardA,
            StandardB,
            StandardC,
            D50,
            D55,
            D65,
            D75
        };
        
        XYZCoord XYToXYZ(const XYCoord& xy);
        XYCoord XYZToXY(const XYZCoord& xyz);
        
        XYCoord PCSToXY();
        
        XYZCoord PCSToXYZ();
        
        std::string IlluminantToString(const Illuminant illuminant);
        Illuminant IlluminantFromString(const std::string& illuminant);
        float IlluminantToTemperature(const Illuminant illuminant);
    }
}

#endif /* Color_hpp */
