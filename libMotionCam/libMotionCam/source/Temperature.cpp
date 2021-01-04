#include "motioncam/Temperature.h"

#include <locale>

using std::string;

namespace motioncam {
    
    // Scale factor between distances in uv space to a more user friendly "tint"
    // parameter.
    
    static const float TintScale = -3000.0;

    // Table from Wyszecki & Stiles, "Color Science", second edition, page 228.
    struct ruvt {
        float r;
        float u;
        float v;
        float t;
    };
    
    static const ruvt TempTable [] = {
        {   0, 0.18006, 0.26352, -0.24341 },
        {  10, 0.18066, 0.26589, -0.25479 },
        {  20, 0.18133, 0.26846, -0.26876 },
        {  30, 0.18208, 0.27119, -0.28539 },
        {  40, 0.18293, 0.27407, -0.30470 },
        {  50, 0.18388, 0.27709, -0.32675 },
        {  60, 0.18494, 0.28021, -0.35156 },
        {  70, 0.18611, 0.28342, -0.37915 },
        {  80, 0.18740, 0.28668, -0.40955 },
        {  90, 0.18880, 0.28997, -0.44278 },
        { 100, 0.19032, 0.29326, -0.47888 },
        { 125, 0.19462, 0.30141, -0.58204 },
        { 150, 0.19962, 0.30921, -0.70471 },
        { 175, 0.20525, 0.31647, -0.84901 },
        { 200, 0.21142, 0.32312, -1.0182 },
        { 225, 0.21807, 0.32909, -1.2168 },
        { 250, 0.22511, 0.33439, -1.4512 },
        { 275, 0.23247, 0.33904, -1.7298 },
        { 300, 0.24010, 0.34308, -2.0637 },
        { 325, 0.24702, 0.34655, -2.4681 },
        { 350, 0.25591, 0.34951, -2.9641 },
        { 375, 0.26400, 0.35200, -3.5814 },
        { 400, 0.27218, 0.35407, -4.3633 },
        { 425, 0.28039, 0.35577, -5.3762 },
        { 450, 0.28863, 0.35714, -6.7262 },
        { 475, 0.29685, 0.35823, -8.5955 },
        { 500, 0.30505, 0.35907, -11.324 },
        { 525, 0.31320, 0.35968, -15.628 },
        { 550, 0.32129, 0.36011, -23.325 },
        { 575, 0.32931, 0.36038, -40.770 },
        { 600, 0.33724, 0.36051, -116.45 }
    };

    Temperature::Temperature() :
        mTemperature(0), mTint(0)
    {
    }

    Temperature::Temperature(double temperature, double tint) :
        mTemperature(temperature), mTint(tint)
    {
    }
    
    Temperature::Temperature(const XYCoord& xy) {
        
        // Convert to uv space.
        double u = 2.0 * xy[0] / (1.5 - xy[0] + 6.0 * xy[1]);
        double v = 3.0 * xy[1] / (1.5 - xy[0] + 6.0 * xy[1]);
        
        // Search for line pair coordinate is between.
        double lastDt = 0.0;

        double lastDv = 0.0;
        double lastDu = 0.0;
        
        for (uint32_t index = 1; index <= 30; index++) {
            
            // Convert slope to delta-u and delta-v, with length 1.
            double du = 1.0;
            double dv = TempTable [index] . t;

            double len = sqrt (1.0 + dv * dv);
            
            du /= len;
            dv /= len;
            
            // Find delta from black body point to test coordinate.
            double uu = u - TempTable [index].u;
            double vv = v - TempTable [index].v;
            
            // Find distance above or below line.
            double dt = - uu * dv + vv * du;
            
            // If below line, we have found line pair.
            if (dt <= 0.0 || index == 30) {
                
                // Find fractional weight of two lines.
                if (dt > 0.0) {
                    dt = 0.0;
                }
                
                dt = -dt;

                double f;
                
                if (index == 1) {
                    f = 0.0;
                }
                else {
                    f = dt / (lastDt + dt);
                }
                
                // Interpolate the temperature.
                mTemperature = 1.0E6 / (TempTable [index - 1].r * f +
                                        TempTable [index    ].r * (1.0 - f));
                
                // Find delta from black body point to test coordinate.
                uu = u - (TempTable [index - 1].u * f +
                          TempTable [index    ].u * (1.0 - f));
                
                vv = v - (TempTable [index - 1].v * f +
                          TempTable [index    ].v * (1.0 - f));
                
                // Interpolate vectors along slope.
                du = du * (1.0 - f) + lastDu * f;
                dv = dv * (1.0 - f) + lastDv * f;
                
                len = sqrt (du * du + dv * dv);
                
                du /= len;
                dv /= len;
                
                // Find distance along slope.
                mTint = (uu * du + vv * dv) * TintScale;
                
                break;
            }
            
            // Try next line pair.
            lastDt = dt;
            
            lastDu = du;
            lastDv = dv;
        }
    }
    
    XYCoord Temperature::getXyCoord() const {
        XYCoord result;
        
        // Find inverse temperature to use as index.
        double r = 1.0E6 / mTemperature;
        
        // Convert tint to offset is uv space.
        double offset = mTint * (1.0 / TintScale);
        
        // Search for line pair containing coordinate.
        for (uint32_t index = 0; index <= 29; index++)
        {
            if (r < TempTable [index + 1] . r || index == 29)
            {
                // Find relative weight of first line.
                double f = (TempTable [index + 1] . r - r) / (TempTable [index + 1] . r - TempTable [index] . r);
                
                // Interpolate the black body coordinates.
                double u = TempTable [index    ] . u * f +
                TempTable [index + 1] . u * (1.0 - f);

                double v = TempTable [index    ] . v * f +
                TempTable [index + 1] . v * (1.0 - f);
                
                // Find vectors along slope for each line.
                double uu1 = 1.0;
                double vv1 = TempTable [index] . t;
                
                double uu2 = 1.0;
                double vv2 = TempTable [index + 1] . t;
                
                double len1 = sqrt (1.0 + vv1 * vv1);
                double len2 = sqrt (1.0 + vv2 * vv2);
                
                uu1 /= len1;
                vv1 /= len1;
                
                uu2 /= len2;
                vv2 /= len2;
                
                // Find vector from black body point.
                double uu3 = uu1 * f + uu2 * (1.0 - f);
                double vv3 = vv1 * f + vv2 * (1.0 - f);

                double len3 = sqrt (uu3 * uu3 + vv3 * vv3);
                
                uu3 /= len3;
                vv3 /= len3;
                
                // Adjust coordinate along this vector.
                u += uu3 * offset;
                v += vv3 * offset;
                
                // Convert to xy coordinates.
                result[0] = 1.5 * u / (u - 4.0 * v + 2.0);
                result[1] =       v / (u - 4.0 * v + 2.0);
                
                break;                
            }
        }
        
        return result;
    }
}
