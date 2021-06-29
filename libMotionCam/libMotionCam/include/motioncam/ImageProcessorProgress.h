#ifndef ImageProcessorProgress_h
#define ImageProcessorProgress_h

#include <string>

namespace motioncam {
    class ImageProcessorProgress {
    public:
        virtual std::string onPreviewSaved(const std::string& outputPath) const = 0;
        virtual bool onProgressUpdate(int progress) const = 0;
        virtual void onCompleted() const = 0;
        virtual void onError(const std::string& error) const = 0;
    };
}

#endif /* ImageProcessorProgress_h */
