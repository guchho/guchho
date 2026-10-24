// =============================================================================
// src/core/unpack/unpack.cpp — extracting archives to disk
// =============================================================================

#include "guchho/unpack.hpp"

#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <string>
#include <vector>
#include <cctype>

namespace guchho::unpack {

namespace fs = std::filesystem;

// Forward declarations
namespace detail {
bool ExtractZip(const UnpackOptions& options, UnpackResult& result);
bool ExtractTar(const UnpackOptions& options, UnpackResult& result, bool gzip);
}

std::string SupportedFormatsNote()
{
    std::string list;
    for (std::size_t i = 0; i < kFormats.size(); ++i) {
        if (i != 0) list += (i + 1 == kFormats.size()) ? " and " : ", ";
        list += "\"" + std::string(kFormats[i]) + "\"";
    }
    return logger::FormatMsg(logger::MsgCat::kPack_OnlySupportedFormats, list);
}

std::string DefaultOutDir(const std::string& archive)
{
    std::string name = archive;
    while (name.size() > 1 && (name.back() == '/' || name.back() == '\\')) {
        name.pop_back();
    }
    // Strip known extensions
    if (name.ends_with(".tar.gz")) {
        name.erase(name.size() - 7);
    } else if (name.ends_with(".tgz")) {
        name.erase(name.size() - 4);
    } else {
        const size_t sep = name.find_last_of("/\\");
        const size_t first = (sep == std::string::npos) ? 0 : sep + 1;
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos && dot > first) {
            name.erase(dot);
        }
    }
    if (name.empty()) name = "extracted";
    return name;
}

static std::string DetectFormat(const std::string& archive)
{
    std::string lower = archive;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (lower.ends_with(".tar.gz") || lower.ends_with(".tgz")) return "tar.gz";
    if (lower.ends_with(".tar")) return "tar";
    if (lower.ends_with(".zip")) return "zip";
    
    // Try magic bytes
    std::error_code ec;
    fs::path p = filesystem::PathFromUTF8(archive);
    if (fs::exists(p, ec)) {
        std::ifstream f(p, std::ios::binary);
        if (f) {
            unsigned char magic[4] = {0};
            f.read(reinterpret_cast<char*>(magic), 4);
            if (magic[0] == 0x50 && magic[1] == 0x4b) return "zip"; // PK
            if (magic[0] == 0x1f && magic[1] == 0x8b) return "tar.gz"; // gzip
        }
    }
    return "";
}

UnpackResult CreateUnpack(const UnpackOptions& options)
{
    UnpackResult result;
    result.outDir = options.outDir;

    if (options.archive.empty()) {
        result.error = logger::FormatMsg(logger::MsgCat::kUnpack_NoArchiveFile);
        result.note = logger::FormatMsg(logger::MsgCat::kUnpack_NoArchiveFileNote);
        return result;
    }

    std::string format = options.format;
    if (format.empty()) {
        format = DetectFormat(options.archive);
    }
    if (format.empty()) {
        result.error = logger::FormatMsg(logger::MsgCat::kUnpack_CouldNotDetectFormat);
        result.note = logger::FormatMsg(logger::MsgCat::kUnpack_CouldNotDetectFormatNote);
        return result;
    }
    if (!IsSupportedFormat(format)) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_UnsupportedFormat, format); // reuse style
        result.note = SupportedFormatsNote();
        return result;
    }

    if (options.outDir.empty()) {
        result.error = logger::FormatMsg(logger::MsgCat::kUnpack_NoOutputDirectory);
        result.note = logger::FormatMsg(logger::MsgCat::kUnpack_NoOutputDirectoryNote);
        return result;
    }

    fs::path outPath = filesystem::PathFromUTF8(options.outDir);
    std::error_code ec;
    fs::file_status outStatus = fs::symlink_status(outPath, ec);
    if (fs::is_regular_file(outStatus)) {
        result.error = logger::FormatMsg(logger::MsgCat::kUnpack_OutputPathIsFile,
                                         options.outDir);
        return result;
    }
    if (!fs::exists(outPath, ec)) {
        fs::create_directories(outPath, ec);
        if (ec) {
            result.error = logger::FormatMsg(
                logger::MsgCat::kUnpack_CannotCreateOutputDirectory, options.outDir);
            result.note = ec.message();
            return result;
        }
    }

    if (format == "zip") {
        if (!detail::ExtractZip(options, result)) {
            return result;
        }
    } else if (format == "tar.gz") {
        if (!detail::ExtractTar(options, result, true)) {
            return result;
        }
    } else if (format == "tar") {
        if (!detail::ExtractTar(options, result, false)) {
            return result;
        }
    } else {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_UnsupportedFormat, format);
        result.note = SupportedFormatsNote();
        return result;
    }

    result.outDir = options.outDir;
    return result;
}

} // namespace guchho::unpack
