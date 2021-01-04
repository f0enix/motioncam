#include "motioncam/Settings.h"

namespace motioncam {
    float getSetting(const json11::Json& json, const std::string& key, const float defaultValue) {
        auto objs = json.object_items();
        if(objs.find(key) == objs.end()) {
            return defaultValue;
        }
        
        return static_cast<float>(json[key].is_number() ? json[key].number_value() : defaultValue);
    }

    int getSetting(const json11::Json& json, const std::string& key, const int defaultValue) {
        auto objs = json.object_items();
        if(objs.find(key) == objs.end()) {
            return defaultValue;
        }
        
        return json[key].is_number() ? json[key].int_value() : defaultValue;
    }

    bool getSetting(const json11::Json& json, const std::string& key, const bool defaultValue) {
        auto objs = json.object_items();
        if(objs.find(key) == objs.end()) {
            return defaultValue;
        }
        
        return json[key].is_bool() ? json[key].bool_value() : defaultValue;
    }

}
