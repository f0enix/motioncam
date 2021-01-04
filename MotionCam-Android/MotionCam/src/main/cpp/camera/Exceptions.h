#ifndef MOTIONCAM_ANDROID_EXCEPTIONS_H
#define MOTIONCAM_ANDROID_EXCEPTIONS_H

#include <stdexcept>
#include <string>

class CameraSessionException : public std::runtime_error {
public:
    CameraSessionException(const std::string& error) : std::runtime_error(error) {
    }
};


class RawPreviewException : public std::runtime_error {
public:
    RawPreviewException(const std::string& error) : std::runtime_error(error) {
    }
};

#endif //MOTIONCAM_ANDROID_EXCEPTIONS_H
