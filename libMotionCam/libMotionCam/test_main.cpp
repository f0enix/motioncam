#include "ImageProcessor.h"

const std::string FILENAMES[] = {
};

class ProgressListener : public motioncam::ImageProcessorProgress {
public:
    bool onProgressUpdate(int progress) const {
        std::cout << "Progress update: " << progress << std::endl;
        return true;
    }
    
    void onCompleted() const {
        
    }
    
    void onError(const std::string& error) const {
        
    }
};

int main(int argc, const char * argv[]) {
    auto inPath = "./";
    auto outPath = "./";

    for(auto filename : FILENAMES) {
        std::cout << "processing " << filename << std::endl;

        ProgressListener progressListener;
        motioncam::ImageProcessor imageProcessor;

        imageProcessor.process(inPath + filename, outPath + filename + ".jpg", progressListener);
    }
    
    return 0;
}
