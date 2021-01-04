#include "motioncam/RawContainer.h"
#include "motioncam/Util.h"
#include "motioncam/Exceptions.h"
#include "motioncam/Math.h"

#include <utility>
#include <vector>

using std::string;
using std::vector;
using json11::Json;

namespace motioncam {
    static const char* METATDATA_FILENAME = "metadata";
    
    json11::Json::array RawContainer::toJsonArray(cv::Mat m) {
        assert(m.type() == CV_32F);

        json11::Json::array result;

        for(int y = 0; y < m.rows; y++) {
            for(int x = 0; x< m.cols; x++) {
                result.push_back(m.at<float>(y, x));
            }
        }

        return result;
    }

    cv::Vec3f RawContainer::toVec3f(const vector<Json>& array) {
        if(array.size() != 3) {
            throw InvalidState("Can't convert to vector. Invalid number of items.");
        }

        cv::Vec3f result;

        result[0] = array[0].number_value();
        result[1] = array[1].number_value();
        result[2] = array[2].number_value();

        return result;
    }

    cv::Mat RawContainer::toMat3x3(const vector<Json>& array) {
        cv::Mat mat(3, 3, CV_32F);
        cv::setIdentity(mat);
        
        if(array.size() < 9)
            return mat;
        
        auto* data = mat.ptr<float>(0);

        data[0] = array[0].number_value();
        data[1] = array[1].number_value();
        data[2] = array[2].number_value();

        data[3] = array[3].number_value();
        data[4] = array[4].number_value();
        data[5] = array[5].number_value();

        data[6] = array[6].number_value();
        data[7] = array[7].number_value();
        data[8] = array[8].number_value();

        return mat;
    }
    
    string RawContainer::toString(PixelFormat format) {
        switch(format) {
            case PixelFormat::RAW12:
                return "raw12";
            
            case PixelFormat::RAW16:
                return "raw16";

            case PixelFormat::YUV_420_888:
                return "yuv_420_888";

            default:
            case PixelFormat::RAW10:
                return "raw10";
        }
    }

    string RawContainer::toString(ColorFilterArrangment sensorArrangment) {
        switch(sensorArrangment) {
            case ColorFilterArrangment::GRBG:
                return "grbg";

            case ColorFilterArrangment::GBRG:
                return "gbrg";

            case ColorFilterArrangment::BGGR:
                return "bggr";

            case ColorFilterArrangment::RGB:
                return "rgb";

            case ColorFilterArrangment::MONO:
                return "mono";

            default:
            case ColorFilterArrangment::RGGB:
                return "rggb";
        }
    }

    int RawContainer::getOptionalSetting(const json11::Json& json, const string& key, const int defaultValue) {
        if(json.object_items().find(key) == json.object_items().end()) {
            return defaultValue;
        }
        
        if(!json[key].is_number())
            return defaultValue;
        
        return json[key].int_value();
    }

    bool RawContainer::getOptionalSetting(const json11::Json& json, const string& key, const bool defaultValue) {
        if(json.object_items().find(key) == json.object_items().end()) {
            return defaultValue;
        }
        
        if(!json[key].is_bool())
            return defaultValue;
        
        return json[key].bool_value();
    }

    string RawContainer::getOptionalStringSetting(const json11::Json& json, const string& key, const string& defaultValue) {
        if(json.object_items().find(key) == json.object_items().end()) {
            return defaultValue;
        }
        
        if(!json[key].is_string())
            return defaultValue;
        
        return json[key].string_value();
    }

    int RawContainer::getRequiredSettingAsInt(const json11::Json& json, const string& key) {
        if(json.object_items().find(key) == json.object_items().end() || !json[key].is_number()) {
            throw InvalidState("Invalid metadata. Missing " + key);
        }
        
        return json[key].int_value();
    }

    string RawContainer::getRequiredSettingAsString(const json11::Json& json, const string& key) {
        if(json.object_items().find(key) == json.object_items().end() || !json[key].is_string()) {
            throw InvalidState("Invalid metadata. Missing " + key);
        }
        
        return json[key].string_value();
    }

    RawContainer::RawContainer(const string& inputPath) :
        mZipReader(new util::ZipReader(inputPath)),
        mReferenceTimestamp(-1),
        mWriteDNG(false)
    {
        initialise();
    }

    RawContainer::RawContainer(RawCameraMetadata cameraMetadata,
                               const PostProcessSettings& postProcessSettings,
                               const int64_t referenceTimestamp,
                               const bool writeDNG,
                               std::vector<string> frames,
                               std::map<string, std::shared_ptr<RawImageBuffer>>  frameBuffers) :
        mCameraMetadata(std::move(cameraMetadata)),
        mPostProcessSettings(postProcessSettings),
        mReferenceTimestamp(referenceTimestamp),
        mWriteDNG(writeDNG),
        mFrames(std::move(frames)),
        mFrameBuffers(std::move(frameBuffers))
    {
    }

    void RawContainer::saveContainer(const std::string& outputPath) {
        auto it = mFrames.begin();
        
        json11::Json::object metadataJson;
        
        // Save reference timestamp
        metadataJson["referenceTimestamp"]  = std::to_string(mReferenceTimestamp);
        metadataJson["writeDNG"]            = mWriteDNG;
        
        // Global camera metadata
        metadataJson["colorIlluminant1"]    = color::IlluminantToString(mCameraMetadata.colorIlluminant1);
        metadataJson["colorIlluminant2"]    = color::IlluminantToString(mCameraMetadata.colorIlluminant2);
        metadataJson["forwardMatrix1"]      = toJsonArray(mCameraMetadata.forwardMatrix1);
        metadataJson["forwardMatrix2"]      = toJsonArray(mCameraMetadata.forwardMatrix2);
        metadataJson["colorMatrix1"]        = toJsonArray(mCameraMetadata.colorMatrix1);
        metadataJson["colorMatrix2"]        = toJsonArray(mCameraMetadata.colorMatrix2);
        metadataJson["calibrationMatrix1"]  = toJsonArray(mCameraMetadata.calibrationMatrix1);
        metadataJson["calibrationMatrix2"]  = toJsonArray(mCameraMetadata.calibrationMatrix2);
        metadataJson["blackLevel"]          = mCameraMetadata.blackLevel;
        metadataJson["whiteLevel"]          = mCameraMetadata.whiteLevel;
        metadataJson["sensorArrangment"]    = toString(mCameraMetadata.sensorArrangment);
        metadataJson["postProcessingSettings"] = mPostProcessSettings.toJson();
        metadataJson["apertures"]           = mCameraMetadata.apertures;
        metadataJson["focalLengths"]        = mCameraMetadata.focalLengths;
        
        json11::Json::array rawImages;
        util::ZipWriter zip(outputPath);
        
        // Write frames first
        while(it != mFrames.end()) {
            auto filename = *it;
            auto frameIt = mFrameBuffers.find(filename);
            if(frameIt == mFrameBuffers.end()) {
                throw InvalidState("Can't find buffer for " + filename);
            }
            
            auto frame = frameIt->second;
            
            // If the metadata has been set, remove the frame
            json11::Json::object imageMetadata;

            // Metadata
            imageMetadata["timestamp"]   = std::to_string(frame->metadata.timestampNs);
            imageMetadata["filename"]    = filename;
            imageMetadata["width"]       = frame->width;
            imageMetadata["height"]      = frame->height;
            imageMetadata["rowStride"]   = frame->rowStride;
            imageMetadata["pixelFormat"] = toString(frame->pixelFormat);

            vector<float> colorCorrectionGains = {
                frame->metadata.colorCorrection[0],
                frame->metadata.colorCorrection[1],
                frame->metadata.colorCorrection[2],
                frame->metadata.colorCorrection[3]
            };
            
            imageMetadata["colorCorrectionGains"] = colorCorrectionGains;

            vector<float> asShot = {
                frame->metadata.asShot[0],
                frame->metadata.asShot[1],
                frame->metadata.asShot[2],
            };

            imageMetadata["asShotNeutral"]          = asShot;
            
            imageMetadata["iso"]                    = frame->metadata.iso;
            imageMetadata["exposureCompensation"]   = frame->metadata.exposureCompensation;
            imageMetadata["exposureTime"]           = (double) frame->metadata.exposureTime;
            imageMetadata["orientation"]            = static_cast<int>(frame->metadata.screenOrientation);

            if(!frame->metadata.lensShadingMap.empty()) {
                imageMetadata["lensShadingMapWidth"]    = frame->metadata.lensShadingMap[0].cols;
                imageMetadata["lensShadingMapHeight"]   = frame->metadata.lensShadingMap[0].rows;
            
                vector<vector<float>> points;
                
                for(auto& i : frame->metadata.lensShadingMap) {
                    vector<float> p;
                    
                    for(int y = 0; y < i.rows; y++) {
                        for(int x = 0; x < i.cols; x++) {
                            p.push_back(i.at<float>(y, x));
                        }
                    }
                    
                    points.push_back(p);
                }
                
                imageMetadata["lensShadingMap"] = points;
            }
            else {
                imageMetadata["lensShadingMapWidth"] = 0;
                imageMetadata["lensShadingMapHeight"] = 0;
            }
            
            zip.addFile(filename, frame->data->hostData());

            rawImages.push_back(imageMetadata);

            ++it;
        }
        
        // Write the metadata
        metadataJson["frames"] = rawImages;
        
        std::string jsonOutput = json11::Json(metadataJson).dump();
        
        zip.addFile(METATDATA_FILENAME, jsonOutput);
        zip.commit();
    }

    void RawContainer::initialise() {
        string jsonStr, err;

        mZipReader->read(METATDATA_FILENAME, jsonStr);
        
        json11::Json json = json11::Json::parse(jsonStr, err);

        if(!err.empty()) {
            throw IOException("Cannot parse metadata");
        }
        
        // Load post process settings if available
        if(json["postProcessingSettings"].is_object()) {
            mPostProcessSettings = PostProcessSettings(json["postProcessingSettings"]);
        }
        
        mReferenceTimestamp = std::stol(getOptionalStringSetting(json, "referenceTimestamp", "0"));
        mWriteDNG = getOptionalSetting(json, "writeDNG", false);
        
        // Black/white levels
        vector<Json> blackLevelValues = json["blackLevel"].array_items();
        for(auto& blackLevelValue : blackLevelValues) {
            mCameraMetadata.blackLevel.push_back(blackLevelValue.number_value());
        }
        
        mCameraMetadata.whiteLevel = getRequiredSettingAsInt(json, "whiteLevel");
        
        // Default to 64
        if(mCameraMetadata.blackLevel.empty()) {
            for(int i = 0; i < 4; i++)
                mCameraMetadata.blackLevel.push_back(64);
        }

        // Default to 1023
        if(mCameraMetadata.whiteLevel <= 0)
            mCameraMetadata.whiteLevel = 1023;

        // Color arrangement
        string colorFilterArrangment = getRequiredSettingAsString(json, "sensorArrangment");

        if(colorFilterArrangment == "grbg") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::GRBG;
        }
        else if(colorFilterArrangment == "gbrg") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::GBRG;
        }
        else if(colorFilterArrangment == "bggr") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::BGGR;
        }
        else if(colorFilterArrangment == "rgb") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::RGB;
        }
        else if(colorFilterArrangment == "mono") {
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::MONO;
        }
        else {
            // Default to RGGB
            mCameraMetadata.sensorArrangment = ColorFilterArrangment::RGGB;
        }
        
        // Matrices
        mCameraMetadata.colorIlluminant1 = color::IlluminantFromString(json["colorIlluminant1"].string_value());
        mCameraMetadata.colorIlluminant2 = color::IlluminantFromString(json["colorIlluminant2"].string_value());

        mCameraMetadata.colorMatrix1 = toMat3x3(json["colorMatrix1"].array_items());
        mCameraMetadata.colorMatrix2 = toMat3x3(json["colorMatrix2"].array_items());

        mCameraMetadata.calibrationMatrix1 = toMat3x3(json["calibrationMatrix1"].array_items());
        mCameraMetadata.calibrationMatrix2 = toMat3x3(json["calibrationMatrix2"].array_items());

        mCameraMetadata.forwardMatrix1 = toMat3x3(json["forwardMatrix1"].array_items());
        mCameraMetadata.forwardMatrix2 = toMat3x3(json["forwardMatrix2"].array_items());

        // Misc
        if(json["apertures"].is_array()) {
            for(int i = 0; i < json["apertures"].array_items().size(); i++)
                mCameraMetadata.apertures.push_back(json["apertures"].array_items().at(i).number_value());
        }

        if(json["focalLengths"].is_array()) {
            for(int i = 0; i < json["focalLengths"].array_items().size(); i++)
                mCameraMetadata.focalLengths.push_back(json["focalLengths"].array_items().at(i).number_value());
        }
        
        // Add the frames
        Json frames = json["frames"];
        if(!frames.is_array()) {
            throw IOException("No frames found in metadata");
        }

        // Add all frame metadata to a list
        vector<Json> frameList = frames.array_items();
        vector<Json>::const_iterator it = frameList.begin();
        
        while(it != frameList.end()) {
            std::shared_ptr<RawImageBuffer> buffer = std::make_shared<RawImageBuffer>();
            
            buffer->width        = getRequiredSettingAsInt(*it, "width");
            buffer->height       = getRequiredSettingAsInt(*it, "height");
            buffer->rowStride    = getRequiredSettingAsInt(*it, "rowStride");

            string pixelFormat = getOptionalStringSetting(*it, "pixelFormat", "raw10");

            if(pixelFormat == "raw16") {
                buffer->pixelFormat = PixelFormat::RAW16;
            }
            else if(pixelFormat == "raw12") {
                buffer->pixelFormat = PixelFormat::RAW12;
            }
            else if(pixelFormat == "yuv_420_888") {
                buffer->pixelFormat = PixelFormat::YUV_420_888;
            }
            else {
                // Default to RAW10
                buffer->pixelFormat = PixelFormat::RAW10;
            }

            buffer->metadata.exposureTime           = getOptionalSetting(*it, "exposureTime", 0);
            buffer->metadata.iso                    = getOptionalSetting(*it, "iso", 0);
            buffer->metadata.exposureCompensation   = getOptionalSetting(*it, "exposureCompensation", 0);
            buffer->metadata.screenOrientation      =
                static_cast<ScreenOrientation>(getOptionalSetting(*it, "orientation", static_cast<int>(ScreenOrientation::LANDSCAPE)));
            
            buffer->metadata.asShot             = toVec3f((*it)["asShotNeutral"].array_items());
            
            string timestamp                    = getRequiredSettingAsString(*it, "timestamp");
            buffer->metadata.timestampNs        = std::stol(timestamp);
                
            // Color correction
            vector<Json> colorCorrectionItems = (*it)["colorCorrectionGains"].array_items();
    
            if(colorCorrectionItems.size() < 4)
                throw InvalidState("Invalid metadata. Color correction gains are invalid");
            
            buffer->metadata.colorCorrection[0] = colorCorrectionItems[0].number_value();
            buffer->metadata.colorCorrection[1] = colorCorrectionItems[1].number_value();
            buffer->metadata.colorCorrection[2] = colorCorrectionItems[2].number_value();
            buffer->metadata.colorCorrection[3] = colorCorrectionItems[3].number_value();

            // Lens shading maps
            int lenShadingMapWidth  = getRequiredSettingAsInt(*it, "lensShadingMapWidth");
            int lenShadingMapHeight = getRequiredSettingAsInt(*it, "lensShadingMapHeight");
            
            // Make sure there are a reasonable number of points available
            if(lenShadingMapHeight < 4 || lenShadingMapWidth < 4) {
                lenShadingMapWidth = 16;
                lenShadingMapHeight = 12;
            }
            
            for(int i = 0; i < 4; i++) {
                cv::Mat m(lenShadingMapHeight, lenShadingMapWidth, CV_32F, cv::Scalar(1));
                buffer->metadata.lensShadingMap.push_back(m);
            }
                        
            // Load points for shading map
            auto shadingMapPts = (*it)["lensShadingMap"].array_items();
            
            if(shadingMapPts.size() == 4) {
                for(int i = 0; i < 4; i++) {
                    auto pts = shadingMapPts[i].array_items();
                    
                    // Check number of points matches
                    if(pts.size() == lenShadingMapWidth * lenShadingMapHeight) {
                        for(int y = 0; y < lenShadingMapHeight; y++) {
                            for(int x = 0; x < lenShadingMapWidth; x++) {
                                buffer->metadata.lensShadingMap[i].at<float>(y, x) = pts[y * lenShadingMapWidth + x].number_value();
                            }
                        }
                    }
                }
            }
            else {
                if(shadingMapPts.size() == lenShadingMapWidth * lenShadingMapHeight * 4) {
                    for(int y = 0; y < lenShadingMapHeight * 4; y+=4) {
                        for(int x = 0; x < lenShadingMapWidth * 4; x+=4) {
                            buffer->metadata.lensShadingMap[0].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 0].number_value();
                            buffer->metadata.lensShadingMap[1].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 1].number_value();
                            buffer->metadata.lensShadingMap[2].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 2].number_value();
                            buffer->metadata.lensShadingMap[3].at<float>(y/4, x/4) = shadingMapPts[y * lenShadingMapWidth + x + 3].number_value();
                        }
                    }
                }
            }

            string filename = getRequiredSettingAsString(*it, "filename");

            // If this is the reference image, keep the name
            if(buffer->metadata.timestampNs == mReferenceTimestamp) {
                mReferenceImage = filename;
            }
            
            mFrames.push_back(filename);
            mFrameBuffers.insert(make_pair(filename, buffer));
            
            ++it;
        }
        
        if(mReferenceImage.empty()) {
            mReferenceImage = *mFrames.begin();
        }
    }

    const RawCameraMetadata& RawContainer::getCameraMetadata() const {
        return mCameraMetadata;
    }

    const PostProcessSettings& RawContainer::getPostProcessSettings() const {
        return mPostProcessSettings;
    }

    bool RawContainer::getWriteDNG() const {
        return mWriteDNG;
    }

    string RawContainer::getReferenceImage() const {
        return mReferenceImage;
    }

    const std::vector<string>& RawContainer::getFrames() const {
        return mFrames;
    }

    std::shared_ptr<RawImageBuffer> RawContainer::loadFrame(const std::string& frame) const {
        auto buffer = mFrameBuffers.find(frame);
        if(buffer == mFrameBuffers.end()) {
            throw IOException("Cannot find " + frame + " in container");
        }
        
        // Load the data into the buffer
        std::vector<uint8_t> data;
        
        mZipReader->read(frame, data);
        
        buffer->second->data->copyHostData(data);
        
        return buffer->second;
    }

    std::shared_ptr<RawImageBuffer> RawContainer::getFrame(const std::string& frame) const {
        auto buffer = mFrameBuffers.find(frame);
        if(buffer == mFrameBuffers.end()) {
            throw IOException("Cannot find " + frame + " in container");
        }
        
        return buffer->second;
    }

    void RawContainer::releaseFrame(const std::string& frame) const {
        auto buffer = mFrameBuffers.find(frame);
        
        if(buffer == mFrameBuffers.end()) {
            throw IOException("Cannot find " + frame + " in container");
        }

        buffer->second->data->release();
    }
}
