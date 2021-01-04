#ifndef Util_hpp
#define Util_hpp

#include <string>
#include <vector>
#include <set>

#include <miniz_zip.h>
#include <json11/json11.hpp>

namespace motioncam {
    namespace util {
        
        class ZipWriter {
        public:
            ZipWriter(const std::string& pathname);
            ~ZipWriter();
            
            void addFile(const std::string& filename, const std::string& data);
            void addFile(const std::string& filename, const std::vector<uint8_t>& data);
            
            void commit();
            
        private:
            mz_zip_archive m_zip;
            bool m_commited;
        };

        class ZipReader {
        public:
            ZipReader(const std::string& pathname);
            ~ZipReader();
            
            void read(const std::string& filename, std::string& output);
            void read(const std::string& filename, std::vector<uint8_t>& output);
            
        private:
            mz_zip_archive m_zip;
            std::vector<std::string> m_files;
        };

#ifdef ZSTD_AVAILABLE
        void ReadCompressedFile(const std::string& inputPath, std::vector<uint8_t>& output);
        void WriteCompressedFile(const std::vector<uint8_t>& data, const std::string& outputPath);
#endif

        __unused void ReadFile(const std::string& inputPath, std::vector<uint8_t>& output);
        __unused void WriteFile(const uint8_t* data, size_t size, const std::string& outputPath);
        __unused json11::Json ReadJsonFromFile(const std::string& path);
        __unused std::string GetBasePath(const std::string& path);
    }
}

#endif /* Util_hpp */
