// =============================================================================
// src/core/unpack/tar.cpp — tar/tar.gz extraction
// =============================================================================

#include "guchho/unpack.hpp"
#include "guchho/filesystem.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>

#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "guchho/miniz.hpp"

namespace guchho::unpack {
namespace detail {

namespace fs = std::filesystem;

static unsigned long ParseOctal(const char* s, size_t len)
{
    unsigned long v = 0;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] == 0 || s[i] == ' ' || s[i] == '\n' || s[i] == '\r') break;
        v = v * 8 + (unsigned long)(s[i] - '0');
    }
    return v;
}

static bool IsSafePathTar(const std::string& name, const UnpackOptions& options, std::string& outRel, std::string& warning)
{
    std::string cleaned = name;
    std::replace(cleaned.begin(), cleaned.end(), '\\', '/');
    while (cleaned.size() >= 2 && cleaned[0] == '.' && cleaned[1] == '/') {
        cleaned.erase(0, 2);
    }
    if (!cleaned.empty() && cleaned[0] == '/') {
        warning = "Skipped absolute path: " + name;
        return false;
    }
    if (cleaned.size() >= 2 && cleaned[1] == ':') {
        warning = "Skipped absolute path: " + name;
        return false;
    }
    
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
    
    if (options.stripComponents > 0) {
        if (parts.size() <= options.stripComponents) {
            warning = "Skipped due to strip-components: " + name;
            return false;
        }
        parts.erase(parts.begin(), parts.begin() + options.stripComponents);
    }
    
    if (parts.empty()) return false;
    
    outRel.clear();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) outRel += "/";
        outRel += parts[i];
    }
    return true;
}

struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};

bool ExtractTar(const UnpackOptions& options, UnpackResult& result, bool gzip)
{
    fs::path archivePath = filesystem::PathFromUTF8(options.archive);
    std::error_code ec;
    if (!fs::exists(archivePath, ec)) {
        result.error = "Archive does not exist: " + options.archive;
        return false;
    }
    
    fs::path outBase = filesystem::PathFromUTF8(options.outDir);
    outBase = fs::absolute(outBase, ec);
    
    std::vector<unsigned char> data;
    if (gzip) {
        std::ifstream gf(archivePath, std::ios::binary);
        if (!gf) {
            result.error = "Failed to open archive";
            return false;
        }
        gf.seekg(0, std::ios::end);
        std::streamsize gsize = gf.tellg();
        gf.seekg(0, std::ios::beg);
        std::vector<unsigned char> gzbuf((size_t)gsize);
        gf.read(reinterpret_cast<char*>(gzbuf.data()), gsize);
        gf.close();
        
        if (gsize < 10) {
            result.error = "Invalid gzip";
            return false;
        }
        if (gzbuf[0] != 0x1f || gzbuf[1] != 0x8b) {
            result.error = "Invalid gzip";
            return false;
        }
        size_t pos = 10;
        if (gzbuf[3] & 0x04) { // FEXTRA
            if (pos + 2 > (size_t)gsize) { result.error = "Invalid gzip"; return false; }
            uint16_t xlen = (uint16_t)gzbuf[pos] | ((uint16_t)gzbuf[pos + 1] << 8);
            pos += 2 + xlen;
        }
        if (gzbuf[3] & 0x08) { // FNAME
            while (pos < (size_t)gsize && gzbuf[pos] != 0) pos++;
            pos++;
        }
        if (gzbuf[3] & 0x10) { // FCOMMENT
            while (pos < (size_t)gsize && gzbuf[pos] != 0) pos++;
            pos++;
        }
        if (gzbuf[3] & 0x02) pos += 2;
        
        if (pos >= (size_t)gsize - 8) { result.error = "Invalid gzip"; return false; }
        size_t deflateSize = (size_t)gsize - pos - 8;
        mz_ulong uncompressed_size = 1024 * 1024 * 512;
        data.resize((size_t)uncompressed_size);
        int status = mz_uncompress(data.data(), &uncompressed_size, gzbuf.data() + pos, (mz_ulong)deflateSize);
        if (status != MZ_OK) {
            result.error = "Failed to decompress gzip";
            return false;
        }
        data.resize((size_t)uncompressed_size);
    } else {
        std::ifstream tf(archivePath, std::ios::binary);
        if (!tf) {
            result.error = "Failed to open archive";
            return false;
        }
        tf.seekg(0, std::ios::end);
        std::streamsize tsize = tf.tellg();
        tf.seekg(0, std::ios::beg);
        data.resize((size_t)tsize);
        tf.read(reinterpret_cast<char*>(data.data()), tsize);
        tf.close();
    }
    
    size_t i = 0;
    std::string longname;
    std::string longlink;
    
    while (i + 512 <= data.size()) {
        const TarHeader* hdr = reinterpret_cast<const TarHeader*>(data.data() + i);
        i += 512;
        // Check for end of archive
        bool isZero = true;
        for (int j = 0; j < 512; ++j) {
            if (data[i - 512 + j] != 0) { isZero = false; break; }
        }
        if (isZero) {
            if (i + 512 <= data.size()) {
                bool isZero2 = true;
                for (int j = 0; j < 512; ++j) {
                    if (data[i + j] != 0) { isZero2 = false; break; }
                }
                if (isZero2) break;
            } else break;
        }
        
        std::string name(hdr->name, strnlen(hdr->name, 100));
        if (hdr->prefix[0] != 0) {
            std::string prefix(hdr->prefix, strnlen(hdr->prefix, 155));
            if (!prefix.empty()) name = prefix + "/" + name;
        }
        
        unsigned long size = ParseOctal(hdr->size, 12);
        char typeflag = hdr->typeflag;
        if (typeflag == 0) typeflag = '0';
        
        if (typeflag == 'L') { // GNU long name
            if (size > 0 && i + size <= data.size()) {
                longname = std::string(reinterpret_cast<const char*>(data.data() + i), size);
                // trim nulls
                while (!longname.empty() && longname.back() == '\0') longname.pop_back();
            }
            i += (size + 511) / 512 * 512;
            continue;
        }
        if (typeflag == 'K') { // GNU long link
            if (size > 0 && i + size <= data.size()) {
                longlink = std::string(reinterpret_cast<const char*>(data.data() + i), size);
                while (!longlink.empty() && longlink.back() == '\0') longlink.pop_back();
            }
            i += (size + 511) / 512 * 512;
            continue;
        }
        
        if (!longname.empty()) {
            name = longname;
            longname.clear();
        }
        
        std::string relPath;
        std::string warn;
        if (!IsSafePathTar(name, options, relPath, warn)) {
            if (!warn.empty()) result.warnings.push_back(warn);
            if (typeflag == '0' || typeflag == '\0') {
                i += (size + 511) / 512 * 512;
            } else {
                // skip data
                if (size > 0) i += (size + 511) / 512 * 512;
            }
            continue;
   
            continue;
        }
        
        fs::path destPath = outBase / filesystem::PathFromUTF8(relPath);
        
        if (typeflag == '5' || relPath.back() == '/') {
            fs::create_directories(destPath, ec);
            if (ec) {
                result.warnings.push_back("Failed to create directory: " + relPath);
            }
            continue;
        }
        if (typeflag == '2') {
            result.warnings.push_back("Skipped symlink: " + relPath);
            if (size > 0) i += (size + 511) / 512 * 512;
            continue;
        }
        if (typeflag == '1') {
            result.warnings.push_back("Skipped hardlink: " + relPath);
            if (size > 0) i += (size + 511) / 512 * 512;
            continue;
        }
        if (typeflag == '3' || typeflag == '4' || typeflag == '6') {
            result.warnings.push_back("Skipped special file: " + relPath);
            if (size > 0) i += (size + 511) / 512 * 512;
            continue;
        }
        
        if (destPath.has_parent_path()) {
            fs::create_directories(destPath.parent_path(), ec);
        }
        if (fs::exists(destPath, ec) && !options.overwrite) {
            result.warnings.push_back("Skipped existing file (use --overwrite): " + relPath);
            if (size > 0) i += (size + 511) / 512 * 512;
            continue;
        }
        
        std::ofstream out(destPath, std::ios::binary);
        if (!out) {
            result.warnings.push_back("Failed to extract: " + relPath);
            if (size > 0) i += (size + 511) / 512 * 512;
            continue;
        }
        if (size > 0 && i + size <= data.size()) {
            out.write(reinterpret_cast<const char*>(data.data() + i), (std::streamsize)size);
        }
        out.close();
        result.files.push_back(relPath);
        i += (size + 511) / 512 * 512;
    }
    
    return true;
}

} // namespace detail
} // namespace guchho::unpack
