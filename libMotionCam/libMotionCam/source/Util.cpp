#include "motioncam/Util.h"
#include "motioncam/Exceptions.h"

#include <fstream>

#ifdef ZSTD_AVAILABLE
    #include <zstd.h>
#endif

using std::string;
using std::vector;
using std::ios;
using std::set;

namespace motioncam {
    namespace util {
    
        //
        // Very basic zip writer
        //
            
        ZipWriter::ZipWriter(const string& filename) : m_zip{ 0 }, m_commited(false) {
            if(!mz_zip_writer_init_file(&m_zip, filename.c_str(), 0)) {
                throw IOException("Can't create " + filename);
            }
        }
    
        void ZipWriter::addFile(const std::string& filename, const std::string& data) {
            addFile(filename, vector<uint8_t>(data.begin(), data.end()));
        }

        void ZipWriter::addFile(const std::string& filename, const std::vector<uint8_t>& data) {
            if(m_commited) {
                throw IOException("Can't add " + filename + " because archive has been commited");
            }
            
            if(!mz_zip_writer_add_mem(&m_zip, filename.c_str(), data.data(), data.size(), MZ_NO_COMPRESSION)) {
                throw IOException("Can't add " + filename);
            }
        }
    
        void ZipWriter::commit() {
            if(!mz_zip_writer_finalize_archive(&m_zip)) {
                throw IOException("Failed to finalize archive!");
            }
        
            if(!mz_zip_writer_end(&m_zip)) {
                throw IOException("Failed to complete archive!");
            }
            
            m_commited = true;
        }
        
        ZipWriter::~ZipWriter() {
            if(!m_commited) {
                mz_zip_writer_finalize_archive(&m_zip);
                mz_zip_writer_end(&m_zip);
            }
        }
    
        //
        // Very basic zip reader
        //
        
        ZipReader::ZipReader(const string& filename) : m_zip{ 0 } {
            if(!mz_zip_reader_init_file(&m_zip, filename.c_str(), 0)) {
                throw IOException("Can't read " + filename);
            }
            
            auto numFiles = mz_zip_reader_get_num_files(&m_zip);
            char entry[512];

            for(auto i = 0; i < numFiles; i++) {
                auto len = mz_zip_reader_get_filename(&m_zip, i, entry, 512);
                if(len == 0) {
                    throw IOException("Failed to parse " + filename);
                }
                
                m_files.emplace_back(entry, len - 1);
            }
        }
    
        ZipReader::~ZipReader() {
            mz_zip_reader_end(&m_zip);
        }
    
        void ZipReader::read(const std::string& filename, std::string& output) {
            vector<uint8_t> tmp;
            
            read(filename, tmp);
            
            output = std::string(tmp.begin(), tmp.end());
        }
    
        void ZipReader::read(const string& filename, vector<uint8_t>& output) {
            auto it = std::find(m_files.begin(), m_files.end(), filename);
            if(it == m_files.end()) {
                throw IOException("Unable to find " + filename);
            }
            
            size_t index = it - m_files.begin();
            mz_zip_archive_file_stat stat;
            
            if (!mz_zip_reader_file_stat(&m_zip, static_cast<mz_uint>(index), &stat))
                throw IOException("Failed to stat " + filename);

            // Resize output
            output.resize(stat.m_uncomp_size);
            
            if(!mz_zip_reader_extract_to_mem(&m_zip, static_cast<mz_uint>(index), &output[0], output.size(), 0)) {
                throw IOException("Failed to load " + filename);
            }
        }
    
        //

#ifdef ZSTD_AVAILABLE
        void ReadCompressedFile(const string& inputPath, vector<uint8_t>& output) {
            std::ifstream file(inputPath, std::ios::binary);
            
            if (file.eof() || file.fail())
                throw IOException("Can't read file " + inputPath);

            const size_t buffInSize = ZSTD_DStreamInSize();
            const size_t buffOutSize = ZSTD_DStreamOutSize();

            vector<uint8_t> buffIn(buffInSize);
            vector<uint8_t> buffOut(buffOutSize);

            std::shared_ptr<ZSTD_DCtx> ctx(ZSTD_createDCtx(), ZSTD_freeDCtx);

            size_t err = 0;
            
            while(!file.eof() || !file.fail()) {
                file.read(reinterpret_cast<char*>(buffIn.data()), buffInSize);
                size_t readBytes = file.gcount();
                
                ZSTD_inBuffer inputBuffer = { buffIn.data(), readBytes, 0 };
                
                while (inputBuffer.pos < inputBuffer.size) {
                    ZSTD_outBuffer outputBuffer = { buffOut.data(), buffOut.size(), 0 };

                    err = ZSTD_decompressStream(ctx.get(), &outputBuffer, &inputBuffer);
                    if(ZSTD_isError(err)) {
                        throw IOException("Failed to decompress file " + inputPath + " error: " + ZSTD_getErrorName(err));
                    }
                    
                    output.insert(output.end(), buffOut.begin(), buffOut.end());
                }
            }

            if (err != 0) {
                throw IOException("Failed to decompress file " + inputPath + " input is truncated." + ZSTD_getErrorName(err));
            }
        }
    
        void WriteCompressedFile(const vector<uint8_t>& data, const string& outputPath) {
            std::ofstream file(outputPath, std::ios::binary);
            
            // If we have a problem
            if(!file.is_open() || file.fail()) {
                throw IOException("Cannot write to " + outputPath);
            }

            // Create compression context
            std::shared_ptr<ZSTD_CCtx> ctx(ZSTD_createCCtx(), ZSTD_freeCCtx);
            
            const size_t buffInSize = ZSTD_CStreamInSize();
            const size_t buffOutSize = ZSTD_CStreamOutSize();

            vector<uint8_t> buffOut(buffOutSize);
            
            ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_compressionLevel, 1);
            ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_checksumFlag, 1);
            
            size_t pos = 0;
            bool lastChunk = false;
            
            while (!lastChunk) {
                size_t read = buffInSize;
                
                if(pos + buffInSize >= data.size()) {
                    read = data.size() - pos;
                    lastChunk = true;
                }

                const ZSTD_EndDirective mode = lastChunk ? ZSTD_e_end : ZSTD_e_continue;
                const char* buffIn = reinterpret_cast<const char*>(data.data() + pos);
                
                ZSTD_inBuffer input = { buffIn, read, 0 };
                
                int finished;
                
                do {
                   ZSTD_outBuffer output = { buffOut.data(), buffOutSize, 0 };
                   const size_t remaining = ZSTD_compressStream2(ctx.get(), &output, &input, mode);
                   
                    file.write(reinterpret_cast<char*>(buffOut.data()), output.pos);
                    
                    if(file.fail()) {
                        throw IOException("Cannot write to " + outputPath);
                    }
                    
                   finished = lastChunk ? (remaining == 0) : (input.pos == input.size);
                } while (!finished);
                
                pos += read;
            }
        }
#endif // ZSTD_AVAILABLE

        __unused void ReadFile(const string& inputPath, vector<uint8_t>& output) {
            std::ifstream file(inputPath, std::ios::binary);
            
            if (file.eof() || file.fail())
                throw IOException("Can't read file " + inputPath);
            
            file.seekg(0, ios::end);
            
            std::streampos fileSize = file.tellg();
            
            output.resize(fileSize);
            
            file.seekg(0, ios::beg);
            
            file.read(reinterpret_cast<char*>(&output[0]), fileSize);
            
            file.close();
        }

        __unused void WriteFile(const uint8_t* data, size_t size, const std::string& outputPath) {
            std::ofstream file(outputPath, std::ios::binary);
            
            // If we have a problem
            if(!file.is_open() || file.fail()) {
                throw IOException("Cannot write to " + outputPath);
            }
            
            file.write( (char*) data, size );
            
            // Error writing this file?
            if(file.fail()) {
                file.close();

                throw IOException("Cannot write " + outputPath);
            }
            
            file.close();
        }

        __unused json11::Json ReadJsonFromFile(const string& path) {
            // Read file to string
            std::ifstream file(path);
            string str, err;
            
            if(file.eof() || file.fail()) {
                throw IOException("Cannot open JSON file: " + path);
            }
            
            file.seekg(0, ios::end);
            
            str.reserve(file.tellg());
            
            file.seekg(0, ios::beg);
            
            str.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            
            // Parse the metadata
            json11::Json metadata = json11::Json::parse(str, err);
            
            if(!err.empty()) {
                throw IOException("Cannot parse JSON file: " + path);
            }
            
            return metadata;
        }

        __unused std::string GetBasePath(const std::string& path) {
            size_t index = path.find_last_of('/');
            if(index == std::string::npos) {
                return path;
            }
            
            return path.substr(0, index);
        }
    }
}
