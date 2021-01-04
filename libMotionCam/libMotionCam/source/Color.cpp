#include "motioncam/Color.h"
#include "motioncam/Math.h"
#include "motioncam/Exceptions.h"

#include <string>

using std::string;

namespace motioncam {
    namespace color {
        
        const XYCoord D50XYCoord = XYCoord(0.3457, 0.3585);
        
        namespace illuminant {
            const char* StandardA = "standarda";
            const char* StandardB = "standardb";
            const char* StandardC = "standardc";
            const char* D50 = "d50";
            const char* D55 = "d55";
            const char* D65 = "d65";
            const char* D75 = "d75";
        }
        
        XYCoord XYZToXY(const XYZCoord& xyz) {
            float X = xyz[0];
            float Y = xyz[1];
            float Z = xyz[2];
            
            float total = X + Y + Z;
            
            if (total > 0.0) {
                return XYCoord(X / total, Y / total);
            }
            
            return D50XYCoord;
        }
        
        XYZCoord XYToXYZ(const XYCoord& xy) {
            XYCoord temp = xy;
            XYZCoord outXyz;
            
            temp[0] = math::clamp<float>(0.000001, temp[0], 0.999999);
            temp[1] = math::clamp<float>(0.000001, temp[1], 0.999999);
            
            if (temp[0] + temp[1] > 0.999999)
            {
                double scale = 0.999999 / (temp[0] + temp[1]);
                temp[0] *= scale;
                temp[1] *= scale;
            }
            
            outXyz[0] = temp[0] / temp[1];
            outXyz[1] = 1.0f;
            outXyz[2] = (1.0 - temp[0] - temp[1]) / temp[1];
            
            return outXyz;
        }
        
        XYCoord PCSToXY() {
            return D50XYCoord;
        }
        
        XYZCoord PCSToXYZ() {
            return XYToXYZ( PCSToXY() );
        }
        
        string IlluminantToString(const Illuminant illuminant) {
            switch (illuminant) {
                case color::StandardA:
                    return illuminant::StandardA;
                    
                case color::StandardB:
                    return illuminant::StandardB;
                    
                case color::StandardC:
                    return illuminant::StandardC;
                    
                case color::D55:
                    return illuminant::D55;
                    
                case color::D65:
                    return illuminant::D65;
                    
                case color::D75:
                    return illuminant::D75;
                    
                case color::D50:
                default:
                    return illuminant::D50;
            }
        }
        
        Illuminant IlluminantFromString(const std::string& illuminant) {
            string cmp = illuminant;
            std::transform(cmp.begin(), cmp.end(), cmp.begin(), ::tolower);

            if(cmp == illuminant::D50) {
                return Illuminant::D50;
            }
            else if(cmp == illuminant::D55) {
                return Illuminant::D55;
            }
            else if(cmp == illuminant::D65) {
                return Illuminant::D65;
            }
            else if(cmp == illuminant::D75) {
                return Illuminant::D75;
            }
            else if(cmp == illuminant::StandardA) {
                return Illuminant::StandardA;
            }
            else if(cmp == illuminant::StandardB) {
                return Illuminant::StandardB;
            }
            else if(cmp == illuminant::StandardC) {
                return Illuminant::StandardC;
            }

            throw InvalidState("Invalid illuminant " + illuminant);
        }
        
        float IlluminantToTemperature(const Illuminant illuminant) {
            switch(illuminant) {
                case Illuminant::D50:
                    return 5000.0;

                case Illuminant::D55:
                    return 5500.0;

                case Illuminant::D65:
                    return 6500.0;

                case Illuminant::D75:
                    return 7500.0;

                case Illuminant::StandardA:
                    return 2850.0;

                case Illuminant::StandardB:
                    return 5500.0;

                case Illuminant::StandardC:
                    return 6500.0;

                default:
                    throw InvalidState("Invalid illuminant " + std::to_string(static_cast<int>(illuminant)));
            }
        }
    }
}
