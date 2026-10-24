// =============================================================================
// src/core/unpack/zip.cpp — zip extraction via miniz
// =============================================================================

#include "guchho/unpack.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "guchho/miniz.hpp"

namespace guchho::unpack {
namespace detail {

namespace fs = std::filesystem;

namespace {

// Unix file-type bits live in the high half of a zip entry's external
// attributes, but only when the archive says it was made on Unix. A DOS
// archive leaves that half zero and puts its read-only/hidden bits in the low
// half, so interpreting those as a mode would turn every entry into a regular
// file with mode 0.
constexpr std::uint32_t kUnixFileTypeMask = 0170000u;
constexpr std::uint32_t kUnixSymlink      = 0120000u;
constexpr std::uint32_t kPermissionMask   = 07777u;

std::uint16_t MadeByOs(const mz_zip_archive_file_stat& stat)
{
    return static_cast<std::uint16_t>(stat.m_version_made_by >> 8);
}

bool IsUnixArchive(const mz_zip_archive_file_stat& stat)
{
    const std::uint16_t os = MadeByOs(stat);
    return os == 3 || os == 19; // Unix, macOS
}

bool IsSymlinkEntry(const mz_zip_archive_file_stat& stat)
{
    if (!IsUnixArchive(stat)) return false;
    const std::uint32_t mode = stat.m_external_attr >> 16;
    return (mode & kUnixFileTypeMask) == kUnixSymlink;
}

std::uint32_t EntryMode(const mz_zip_archive_file_stat& stat)
{
    if (!IsUnixArchive(stat)) return 0;
    return (stat.m_external_attr >> 16) & kPermissionMask;
}

struct Metadata {
    fs::path path;
    std::uint32_t mode = 0;
    bool has_mode      = false;
    std::time_t mtime  = 0;
    bool has_mtime     = false;
};

void SetTimestamps(const fs::path& path, std::time_t mtime)
{
    std::error_code ec;
    const auto system_time = std::chrono::system_clock::from_time_t(mtime);
    const auto file_time   = fs::file_time_type::clock::now() +
                           (system_time - std::chrono::system_clock::now());
    fs::last_write_time(path, file_time, ec);
}

void ApplyMetadata(const std::vector<Metadata>& records, bool permissions,
                   bool timestamps)
{
    // Reverse discovery order, so a directory's own time and mode are set
    // after everything inside it. Creating a child touches the parent's mtime;
    // setting the parent first would leave it with the moment of extraction
    // instead of the moment it had in the archive.
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        std::error_code ec;
        if (timestamps && it->has_mtime) SetTimestamps(it->path, it->mtime);
        if (permissions && it->has_mode && it->mode != 0) {
            fs::permissions(it->path, static_cast<fs::perms>(it->mode),
                            fs::perm_options::replace, ec);
        }
    }
}

std::string FoldCase(std::string text)
{
#ifdef _WIN32
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return text;
}

// Whether "candidate" is inside "base". Both are made absolute and canonical
// first, so a path that walks through an existing symbolic link is judged by
// where it really lands rather than by how it was spelled. The candidate is
// canonicalized too: an existing destination symlink can otherwise point
// outside the output directory and be written through.
bool PathIsUnder(const fs::path& base, const fs::path& candidate)
{
    std::error_code ec;
    fs::path canon_base = fs::weakly_canonical(base, ec);
    if (ec) {
        ec.clear();
        canon_base = fs::absolute(base, ec);
    }
    if (ec) return false;

    fs::path canon_candidate = fs::weakly_canonical(candidate, ec);
    if (ec) {
        ec.clear();
        canon_candidate = fs::absolute(candidate, ec);
    }
    if (ec) return false;

    const std::string base_text = FoldCase(canon_base.generic_string());
    const std::string cand_text = FoldCase(canon_candidate.generic_string());

    if (cand_text == base_text) return true;
    if (cand_text.size() <= base_text.size()) return false;
    if (cand_text.compare(0, base_text.size(), base_text) != 0) return false;

    // A prefix match alone would accept "outdir-extra" for "outdir".
    return base_text.back() == '/' || cand_text[base_text.size()] == '/';
}

struct Sink {
    std::ofstream out;
};

size_t WriteSink(void* opaque, mz_uint64 file_ofs, const void* buffer, size_t size)
{
    auto* sink = static_cast<Sink*>(opaque);
    sink->out.seekp(static_cast<std::streamoff>(file_ofs));
    sink->out.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
    return sink->out.good() ? size : 0;
}

} // namespace

static bool IsSafePath(const std::string& name, const UnpackOptions& options,
                       std::string& outRel, std::string& warning)
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
        warning = logger::FormatMsg(logger::MsgCat::kUnpack_SkippedAbsolutePath, name);
        return false;
    }
    if (cleaned.size() >= 2 && cleaned[1] == ':') {
        warning = logger::FormatMsg(logger::MsgCat::kUnpack_SkippedAbsolutePath, name);
        return false;
    }

    // Split and check components
    std::vector<std::string> parts;
    std::string cur;
    for (char c : cleaned) {
        if (c == '/') {
            if (!cur.empty()) {
                if (cur == "..") {
                    warning = logger::FormatMsg(logger::MsgCat::kUnpack_SkippedPathTraversal, name);
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
            warning = logger::FormatMsg(logger::MsgCat::kUnpack_SkippedPathTraversal, name);
            return false;
        }
        if (cur != ".") parts.push_back(cur);
    }

    // Apply strip components
    if (options.stripComponents > 0) {
        if (parts.size() <= options.stripComponents) {
            warning = logger::FormatMsg(logger::MsgCat::kUnpack_SkippedStripComponents, name);
            return false;
        }
        parts.erase(parts.begin(), parts.begin() +
                                       static_cast<std::ptrdiff_t>(options.stripComponents));
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
        result.error = logger::FormatMsg(logger::MsgCat::kUnpack_ArchiveDoesNotExist, options.archive);
        return false;
    }

    fs::path outBase = filesystem::PathFromUTF8(options.outDir);
    outBase = fs::absolute(outBase, ec);

    mz_zip_archive zip_archive{};
    mz_zip_zero_struct(&zip_archive);
    if (!mz_zip_reader_init_file(&zip_archive, options.archive.c_str(), 0)) {
        mz_zip_error err = mz_zip_get_last_error(&zip_archive);
        const char* msg = mz_zip_get_error_string(err);
        result.error = logger::FormatMsg(logger::MsgCat::kUnpack_FailedToOpenZipArchive);
        if (msg) result.note = msg;
        return false;
    }

    std::vector<Metadata> metadata;

    mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
    for (mz_uint i = 0; i < num_files; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &stat)) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToReadEntryMetadata));
            continue;
        }

        std::string filename = stat.m_filename;
        bool isDir = stat.m_is_directory != 0 ||
                     (!filename.empty() && filename.back() == '/');

        std::string relPath;
        std::string warn;
        if (!IsSafePath(filename, options, relPath, warn)) {
            if (!warn.empty()) result.warnings.push_back(warn);
            continue;
        }

        fs::path destPath = outBase / filesystem::PathFromUTF8(relPath);
        if (!PathIsUnder(outBase, destPath)) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedOutsideOutputDir, relPath));
            continue;
        }

        Metadata meta;
        meta.path      = destPath;
        meta.mode      = EntryMode(stat);
        meta.has_mode  = meta.mode != 0;
        meta.mtime     = stat.m_time;
        meta.has_mtime = stat.m_time != 0;

        if (isDir) {
            fs::create_directories(destPath, ec);
            if (ec) {
                result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToCreateDirectory, relPath));
                continue;
            }
            metadata.push_back(meta);
            continue;
        }

        if (IsSymlinkEntry(stat)) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedSymlink, relPath));
            continue;
        }

        if (!stat.m_is_supported) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedUnsupportedEntry, relPath));
            continue;
        }

        // Create parent directories
        if (destPath.has_parent_path()) {
            fs::create_directories(destPath.parent_path(), ec);
            if (ec) {
                result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToCreateDirectoryFor, relPath));
                continue;
            }
        }

        if (fs::exists(destPath, ec) && !options.overwrite) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedExistingFile, relPath));
            continue;
        }

        Sink sink;
        sink.out.open(destPath, std::ios::binary | std::ios::trunc);
        if (!sink.out) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToExtract, relPath));
            continue;
        }

        if (!mz_zip_reader_extract_to_callback(&zip_archive, i, WriteSink, &sink, 0)) {
            sink.out.close();
            fs::remove(destPath, ec);
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToExtract, relPath));
            continue;
        }

        sink.out.close();
        if (!sink.out) {
            fs::remove(destPath, ec);
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToWrite, relPath));
            continue;
        }

        metadata.push_back(meta);
        result.files.push_back(relPath);
    }

    mz_zip_reader_end(&zip_archive);

    if (options.preservePermissions || options.preserveTimestamps) {
        ApplyMetadata(metadata, options.preservePermissions,
                      options.preserveTimestamps);
    }

    return true;
}

} // namespace detail
} // namespace guchho::unpack
