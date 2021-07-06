#ifndef _COMMON_H_
#define _COMMON_H_

enum class RawFormat : int {
    RAW10 = 0,
    RAW16
};

enum class SensorArrangement : int {
    RGGB = 0,
    GRBG,
    GBRG,
    BGGR
};

// YUV conversion coefficients
const float YUV_R = 0.299f;
const float YUV_G = 0.587f;
const float YUV_B = 0.114f;

const int TONEMAP_LEVELS = 11;

#endif // _COMMON_H_