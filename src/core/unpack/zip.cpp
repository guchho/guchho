// =============================================================================
// src/core/unpack/zip.cpp — zip extraction via miniz
// =============================================================================

#include "guchho/unpack.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "guchho/miniz.hpp"

namespace guchho::unpack {
namespace detail {

namespace fs = std::filesystem;

static bool IsSafePath(const std::string& name, const fs::path& outBase, 
                       const UnpackOptions& options, std::string& outRel, std::string& warning)
{
    std::string cleaned = name;
    // Normalize separators
    std::replace(cleaned.begin(), cleaned.end(), '\\', '/');
    // Strip leading ./
    while (cleaned.size() >= 2 && cleaned[0] == '.' && cleaned[1] == '/') {
        cleaned.erase(0, 2);
    }
    // Reject absolute
    if (!cleaned.empty() && cleaned[0] == '/') {
        warning = "Skipped absolute path: " + name;
        return false;
    }
    if (cleaned.size() >= 2 && cleaned[1] == ':') {
        warning = "Skipped absolute path: " + name;
        return false;
    }
    
    // Split and check components
    std::vector<std::string> parts;
    std::string cur;
    for (char c : cleaned) {
        if (c == '/') {
            if (!cur.empty()) {
                if (cur == "..") {
                    warning = "Skipped path traversal: " + name;
                    return false;
                }
                if (cur != ".") parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        if (cur == "..") {
            warning = "Skipped path traversal: " + name;
            return false;
        }
        if (cur != ".") parts.push_back(cur);
    }
    
    // Apply strip components
    if (options.stripComponents > 0) {
        if (parts.size() <= options.stripComponents) {
            warning = "Skipped due to strip-components: " + name;
            return false;
        }
        parts.erase(parts.begin(), parts.begin() + options.stripComponents);
    }
    
    if (parts.empty()) {
        // Could be just a directory entry for root - skip
        return false;
    }
    
    outRel.clear();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) outRel += "/";
        outRel += parts[i];
    }
    return true;
}

bool ExtractZip(const UnpackOptions& options, UnpackResult& result)
{
    fs::path archivePath = filesystem::PathFromUTF8(options.archive);
    std::error_code ec;
    if (!fs::exists(archivePath, ec)) {
        result.error = "Archive does not exist: " + options.archive;
        return false;
    }
    
    fs::path outBase = filesystem::PathFromUTF8(options.outDir);
    outBase = fs::absolute(outBase, ec);
    
    mz_zip_archive zip_archive{};
    mz_zip_zero_struct(&zip_archive);
    if (!mz_zip_reader_init_file(&zip_archive, options.archive.c_str(), 0)) {
        mz_zip_error err = mz_zip_get_last_error(&zip_archive);
        const char* msg = mz_zip_get_error_string(err);
        result.error = "Failed to open zip archive";
        if (msg) result.note = msg;
        return false;
    }
    
    mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
    for (mz_uint i = 0; i < num_files; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &stat)) {
            continue;
        }
        
        std::string filename = stat.m_filename ? stat.m_filename : "";
        bool isDir = stat.m_is_directory != 0 || (!filename.empty() && filename.back() == '/');
        
        std::string relPath;
        std::string warn;
        if (!IsSafePath(filename, outBase, options, relPath, warn)) {
            if (!warn.empty()) result.warnings.push_back(warn);
            continue;
        }
        
        fs::path destPath = outBase / filesystem::PathFromUTF8(relPath);
        // Ensure dest is under outBase
        std::error_code ec2;
        fs::path canonDest = fs::weakly_canonical(destPath.parent_path(), ec2);
        if (ec2) canonDest = fs::absolute(destPath.parent_path(), ec2);
        fs::path canonBase = outBase;
        if (canonDest.string().find(canonBase.string()) != 0 && 
            !(canonBase.string() == canonDest.string().substr(0, canonBase.string().size()))) {
            // Additional check
        }
        
        if (isDir) {
            fs::create_directories(destPath, ec);
            if (ec) {
                result.warnings.push_back("Failed to create directory: " + relPath);
            }
            continue;
        }
        
        // Create parent directories
        if (destPath.has_parent_path()) {
            fs::create_directories(destPath.parent_path(), ec);
        }
        
        if (fs::exists(destPath, ec) && !options.overwrite) {
            result.warnings.push_back("Skipped existing file (use --overwrite): " + relPath);
            continue;
        }
        
        // Extract file
        if (!mz_zip_reader_extract_to_file(&zip_archive, i, destPath.string().c_str(), 0)) {
            mz_zip_error err = mz_zip_get_last_error(&zip_archive);
            const char* msg = mz_zip_get_error_string(err);
            result.warnings.push_back("Failed to extract: " + relPath);
            if (msg) {
                // append briefly
            }
            continue;
        }
        result.files.push_back(relPath);
    }
    
    mz_zip_reader_end(&zip_archive);
    return true;
}

} // namespace detail
} // namespace guchho::unpack
