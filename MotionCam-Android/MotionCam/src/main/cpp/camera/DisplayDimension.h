#ifndef MOTIONCAM_ANDROID_DISPLAYDIMENSION_H
#define MOTIONCAM_ANDROID_DISPLAYDIMENSION_H

#include <cstdint>
#include <math.h>

namespace motioncam {
    class DisplayDimension {
    public:
        DisplayDimension(int32_t w, int32_t h) : mWidth(w), mHeight(h), mPortrait(false) {
            if (h > w) {
                // make it landscape
                mWidth = h;
                mHeight = w;
                mPortrait = true;
            }
        }

        DisplayDimension(const DisplayDimension& other) {
            mWidth = other.mWidth;
            mHeight = other.mHeight;
            mPortrait = other.mPortrait;
        }

        DisplayDimension(void) {
            mWidth = 0;
            mHeight = 0;
            mPortrait = false;
        }

        DisplayDimension& operator = (const DisplayDimension& other) {
            mWidth = other.mWidth;
            mHeight = other.mHeight;
            mPortrait = other.mPortrait;

            return (*this);
        }

        __unused bool isSameRatio(const DisplayDimension& other) const {
            return (mWidth * other.mHeight == mHeight * other.mWidth);
        }

        bool isCloseRatio(const DisplayDimension& other) const {
            double ratioDiff = fabs((double) mWidth / (double) mHeight) - ((double) other.mWidth / (double) other.mHeight);
            return ratioDiff < 0.01;
        }

        bool operator > (const DisplayDimension& other) const {
            return (mWidth >= other.mWidth && mHeight >= other.mHeight);
        }

        bool operator < (const DisplayDimension& other) const {
            return (mWidth <= other.mWidth && mHeight <= other.mHeight);
        }

        bool operator == (const DisplayDimension& other) const {
            return (mWidth == other.mWidth && mHeight == other.mHeight && mPortrait == other.mPortrait);
        }

        DisplayDimension operator - (DisplayDimension& other) const {
            DisplayDimension delta(mWidth - other.mWidth, mHeight - other.mHeight);
            return delta;
        }

        void flip() { mPortrait = !mPortrait; }

        bool isPortrait() const  { return mPortrait; }

        int32_t width() const  { return mWidth; }

        int32_t height() const { return mHeight; }

        int32_t originalWidth() const { return (mPortrait ? mHeight : mWidth); }

        int32_t originalHeight() const { return (mPortrait ? mWidth : mHeight); }

    private:
        int32_t mWidth, mHeight;
        bool mPortrait;
    };
}

#endif //MOTIONCAM_ANDROID_DISPLAYDIMENSION_H
