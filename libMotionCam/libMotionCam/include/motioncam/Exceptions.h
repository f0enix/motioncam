#ifndef Exceptions_hpp
#define Exceptions_hpp

#include <exception>
#include <string>

namespace motioncam {
    
    class MotionCamException : public std::runtime_error {
    public:
        MotionCamException(const std::string& error) : runtime_error(error) {}
    };
    
    class IOException : public MotionCamException {
    public:
        IOException(const std::string& error) : MotionCamException(error) {}
    };
    
    class InvalidState : public MotionCamException {
    public:
        InvalidState(const std::string& error) : MotionCamException(error) {}
    };
}

#endif /* Exceptions_hpp */
