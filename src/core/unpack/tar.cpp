// =============================================================================
// src/core/unpack/tar.cpp — tar/tar.gz extraction
// =============================================================================

#include "guchho/unpack.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
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

constexpr std::size_t kBlockSize = 512;
constexpr std::uint32_t kPermissionMask = 07777u;

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

static_assert(sizeof(TarHeader) == kBlockSize, "tar header must be one block");

struct Metadata {
    fs::path path;
    std::uint32_t mode = 0;
    bool has_mode      = false;
    std::int64_t mtime = 0;
    bool has_mtime     = false;
};

struct PaxValues {
    std::optional<std::string> path;
    std::optional<std::uint64_t> size;
    std::optional<std::uint32_t> mode;
    std::optional<std::int64_t> mtime;
};

std::size_t RoundUp(std::size_t size)
{
    return ((size + kBlockSize - 1) / kBlockSize) * kBlockSize;
}

std::string FoldCase(std::string text)
{
#ifdef _WIN32
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return text;
}

bool PathIsUnder(const fs::path& base, const fs::path& candidate)
{
    std::error_code ec;
    fs::path canon_base = fs::weakly_canonical(base, ec);
    if (ec) canon_base = fs::absolute(base, ec);

    fs::path parent = candidate.parent_path();
    if (parent.empty()) parent = candidate;
    fs::path canon_parent = fs::weakly_canonical(parent, ec);
    if (ec) canon_parent = fs::absolute(parent, ec);

    const std::string base_text   = FoldCase(canon_base.generic_string());
    const std::string parent_text = FoldCase(canon_parent.generic_string());

    if (parent_text == base_text) return true;
    if (parent_text.size() <= base_text.size()) return false;
    if (parent_text.compare(0, base_text.size(), base_text) != 0) return false;
    return base_text.back() == '/' || parent_text[base_text.size()] == '/';
}

// tar numbers are octal text. GNU's base-256 extension sets the high bit of
// the first byte and stores a two's-complement binary number after it; the
// negative half is not something an extractor here needs, so those are
// refused rather than misread as a size.
bool ParseNumber(const char* field, std::size_t width, std::uint64_t& value)
{
    if (width == 0) return false;

    const auto first = static_cast<unsigned char>(field[0]);
    if ((first & 0x80u) != 0) {
        if ((first & 0x40u) != 0) return false;
        std::uint64_t v = first & 0x3fu;
        for (std::size_t i = 1; i < width; ++i) {
            v = (v << 8) | static_cast<unsigned char>(field[i]);
        }
        value = v;
        return true;
    }

    std::uint64_t v = 0;
    bool any = false;
    for (std::size_t i = 0; i < width; ++i) {
        const char c = field[i];
        if (c == '\0') break;
        if (c == ' ') continue;
        if (c < '0' || c > '7') return false;
        v = (v << 3) | static_cast<std::uint64_t>(c - '0');
        any = true;
    }
    value = v;
    return any;
}

std::uint32_t HeaderChecksum(const unsigned char* block, bool signed_bytes)
{
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        if (i >= 148 && i < 156) {
            sum += static_cast<std::uint32_t>(' ');
        } else if (signed_bytes) {
            sum += static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int8_t>(block[i])));
        } else {
            sum += static_cast<std::uint32_t>(block[i]);
        }
    }
    return sum;
}

bool ChecksumMatches(const unsigned char* block, std::uint64_t stored)
{
    return stored == HeaderChecksum(block, false) ||
           stored == HeaderChecksum(block, true);
}

bool ParseUnsignedDecimal(const std::string& text, std::uint64_t& value)
{
    if (text.empty()) return false;
    std::uint64_t v = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    value = v;
    return true;
}

bool ParseSignedDecimal(const std::string& text, std::int64_t& value)
{
    if (text.empty()) return false;
    std::size_t i = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        i = 1;
    } else if (text[0] == '+') {
        i = 1;
    }

    std::uint64_t v = 0;
    bool any = false;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '.' || c == ' ') break;
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
        any = true;
    }
    if (!any) return false;

    value = negative ? -static_cast<std::int64_t>(v) : static_cast<std::int64_t>(v);
    return true;
}

bool ParseOctalString(const std::string& text, std::uint32_t& value)
{
    if (text.empty()) return false;
    std::uint32_t v = 0;
    bool any = false;
    for (const char c : text) {
        if (c < '0' || c > '7') return false;
        v = (v << 3) | static_cast<std::uint32_t>(c - '0');
        any = true;
    }
    value = v;
    return any;
}

std::string TrimNuls(std::string text)
{
    while (!text.empty() && text.back() == '\0') text.pop_back();
    return text;
}

std::size_t FieldLength(const char* field, std::size_t width)
{
    std::size_t length = 0;
    while (length < width && field[length] != '\0') ++length;
    return length;
}

// PAX records are "LENGTH key=value\n", where LENGTH counts the length field,
// the space and the record itself. Parsing the length first is what makes a
// value containing a newline safe.
bool ParsePax(const unsigned char* data, std::size_t size, bool global, PaxValues& out)
{
    std::size_t offset = 0;
    while (offset < size) {
        std::size_t length = 0;
        std::size_t cursor = offset;
        while (cursor < size && data[cursor] >= '0' && data[cursor] <= '9') {
            length = length * 10 + static_cast<std::size_t>(data[cursor] - '0');
            ++cursor;
        }
        if (cursor == offset || cursor >= size || data[cursor] != ' ') return false;
        if (length == 0 || offset + length > size) return false;

        const std::size_t record_end = offset + length;
        const std::size_t key_start   = cursor + 1;
        std::size_t equals = key_start;
        while (equals < record_end && data[equals] != '=') ++equals;
        if (equals >= record_end) return false;

        const std::string key(reinterpret_cast<const char*>(data + key_start),
                              equals - key_start);
        std::size_t value_end = record_end;
        if (value_end > 0 && data[value_end - 1] == '\n') --value_end;
        const std::string value(reinterpret_cast<const char*>(data + equals + 1),
                                value_end - (equals + 1));

        if (!global && key == "path") {
            out.path = value;
        } else if (key == "size") {
            std::uint64_t parsed = 0;
            if (ParseUnsignedDecimal(value, parsed)) out.size = parsed;
        } else if (key == "mtime") {
            std::int64_t parsed = 0;
            if (ParseSignedDecimal(value, parsed)) out.mtime = parsed;
        } else if (key == "mode") {
            std::uint32_t parsed = 0;
            if (ParseOctalString(value, parsed)) out.mode = parsed;
        }

        offset += length;
    }
    return true;
}

void SetTimestamps(const fs::path& path, std::int64_t mtime)
{
    const std::time_t seconds = static_cast<std::time_t>(mtime);
    std::error_code ec;
    const auto system_time = std::chrono::system_clock::from_time_t(seconds);
    const auto file_time   = fs::file_time_type::clock::now() +
                           (system_time - std::chrono::system_clock::now());
    fs::last_write_time(path, file_time, ec);
}

void ApplyMetadata(const std::vector<Metadata>& records)
{
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        if (it->has_mtime) SetTimestamps(it->path, it->mtime);
        if (it->has_mode && it->mode != 0) {
            std::error_code ec;
            fs::permissions(it->path, static_cast<fs::perms>(it->mode & kPermissionMask),
                            fs::perm_options::replace, ec);
        }
    }
}

struct InflateOutput {
    std::vector<unsigned char> bytes;
};

int InflateCallback(const void* buffer, int length, void* user)
{
    auto* output = static_cast<InflateOutput*>(user);
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    output->bytes.insert(output->bytes.end(), bytes, bytes + length);
    return 1;
}

bool ParseGzip(const std::string& path, std::vector<unsigned char>& out,
               std::string& error)
{
    std::ifstream in(filesystem::PathFromUTF8(path), std::ios::binary);
    if (!in) {
        error = logger::FormatMsg(logger::MsgCat::kUnpack_FailedToOpenArchive);
        return false;
    }

    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 10) {
        error = logger::FormatMsg(logger::MsgCat::kUnpack_InvalidGzip);
        return false;
    }

    std::vector<unsigned char> gzip(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(gzip.data()), size);
    if (!in) {
        error = logger::FormatMsg(logger::MsgCat::kUnpack_FailedToReadArchive);
        return false;
    }

    if (gzip[0] != 0x1f || gzip[1] != 0x8b || gzip[2] != 8) {
        error = logger::FormatMsg(logger::MsgCat::kUnpack_InvalidGzip);
        return false;
    }

    const auto total = gzip.size();
    const unsigned char flags = gzip[3];
    std::size_t pos = 10;

    if ((flags & 0x04u) != 0) { // FEXTRA
        if (pos + 2 > total) {
            error = logger::FormatMsg(logger::MsgCat::kUnpack_InvalidGzip);
            return false;
        }
        const auto extra = static_cast<std::size_t>(gzip[pos]) |
                           (static_cast<std::size_t>(gzip[pos + 1]) << 8);
        pos += 2 + extra;
    }
    if ((flags & 0x08u) != 0) { // FNAME
        while (pos < total && gzip[pos] != 0) ++pos;
        ++pos;
    }
    if ((flags & 0x10u) != 0) { // FCOMMENT
        while (pos < total && gzip[pos] != 0) ++pos;
        ++pos;
    }
    if ((flags & 0x02u) != 0) { // FHCRC
        pos += 2;
    }

    if (pos > total || total - pos < 8) {
        error = logger::FormatMsg(logger::MsgCat::kUnpack_InvalidGzip);
        return false;
    }

    const std::size_t deflate_size = total - pos - 8;
    std::size_t consumed = deflate_size;
    InflateOutput output;
    if (tinfl_decompress_mem_to_callback(gzip.data() + pos, &consumed,
                                         InflateCallback, &output, 0) == 0) {
        error = logger::FormatMsg(logger::MsgCat::kUnpack_FailedToDecompressGzip);
        return false;
    }

    const auto* trailer = gzip.data() + total - 8;
    const std::uint32_t expected_crc =
        static_cast<std::uint32_t>(trailer[0]) |
        (static_cast<std::uint32_t>(trailer[1]) << 8) |
        (static_cast<std::uint32_t>(trailer[2]) << 16) |
        (static_cast<std::uint32_t>(trailer[3]) << 24);
    const std::uint32_t expected_size =
        static_cast<std::uint32_t>(trailer[4]) |
        (static_cast<std::uint32_t>(trailer[5]) << 8) |
        (static_cast<std::uint32_t>(trailer[6]) << 16) |
        (static_cast<std::uint32_t>(trailer[7]) << 24);

    const auto actual_crc = static_cast<std::uint32_t>(
        mz_crc32(MZ_CRC32_INIT, output.bytes.data(), output.bytes.size()));

    if (actual_crc != expected_crc ||
        static_cast<std::uint32_t>(output.bytes.size()) != expected_size) {
        error = logger::FormatMsg(logger::MsgCat::kUnpack_CorruptGzipArchive);
        return false;
    }

    out = std::move(output.bytes);
    return true;
}

} // namespace

static bool IsSafePathTar(const std::string& name, const UnpackOptions& options,
                          std::string& outRel, std::string& warning)
{
    std::string cleaned = name;
    std::replace(cleaned.begin(), cleaned.end(), '\\', '/');
    while (cleaned.size() >= 2 && cleaned[0] == '.' && cleaned[1] == '/') {
        cleaned.erase(0, 2);
    }
    if (!cleaned.empty() && cleaned[0] == '/') {
        warning = logger::FormatMsg(logger::MsgCat::kUnpack_SkippedAbsolutePath, name);
        return false;
    }
    if (cleaned.size() >= 2 && cleaned[1] == ':') {
        warning = logger::FormatMsg(logger::MsgCat::kUnpack_SkippedAbsolutePath, name);
        return false;
    }

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

    if (options.stripComponents > 0) {
        if (parts.size() <= options.stripComponents) {
            warning = logger::FormatMsg(logger::MsgCat::kUnpack_SkippedStripComponents, name);
            return false;
        }
        parts.erase(parts.begin(), parts.begin() +
                                       static_cast<std::ptrdiff_t>(options.stripComponents));
    }

    if (parts.empty()) return false;

    outRel.clear();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) outRel += "/";
        outRel += parts[i];
    }
    return true;
}

bool ExtractTar(const UnpackOptions& options, UnpackResult& result, bool gzip)
{
    fs::path archivePath = filesystem::PathFromUTF8(options.archive);
    std::error_code ec;
    if (!fs::exists(archivePath, ec)) {
        result.error = logger::FormatMsg(logger::MsgCat::kUnpack_ArchiveDoesNotExist, options.archive);
        return false;
    }

    fs::path outBase = filesystem::PathFromUTF8(options.outDir);
    outBase = fs::absolute(outBase, ec);

    std::vector<unsigned char> data;
    if (gzip) {
        std::string error;
        if (!ParseGzip(options.archive, data, error)) {
            result.error = error;
            return false;
        }
    } else {
        std::ifstream in(archivePath, std::ios::binary);
        if (!in) {
            result.error = logger::FormatMsg(logger::MsgCat::kUnpack_FailedToOpenArchive);
            return false;
        }
        in.seekg(0, std::ios::end);
        const std::streamsize size = in.tellg();
        in.seekg(0, std::ios::beg);
        if (size < 0) {
            result.error = logger::FormatMsg(logger::MsgCat::kUnpack_FailedToReadArchive);
            return false;
        }
        data.resize(static_cast<std::size_t>(size));
        in.read(reinterpret_cast<char*>(data.data()), size);
        if (!in) {
            result.error = logger::FormatMsg(logger::MsgCat::kUnpack_FailedToReadArchive);
            return false;
        }
    }

    const std::size_t total = data.size();
    std::size_t offset = 0;
    std::string longname;
    PaxValues pax;
    PaxValues global_pax;
    std::vector<Metadata> metadata;

    while (offset + kBlockSize <= total) {
        const unsigned char* raw = data.data() + offset;

        bool all_zero = true;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
            if (raw[i] != 0) {
                all_zero = false;
                break;
            }
        }
        if (all_zero) break;

        TarHeader header {};
        std::memcpy(&header, raw, kBlockSize);

        std::uint64_t stored_checksum = 0;
        if (!ParseNumber(header.chksum, 8, stored_checksum) ||
            !ChecksumMatches(raw, stored_checksum)) {
            result.error = logger::FormatMsg(logger::MsgCat::kUnpack_CorruptTarHeaderChecksum);
            return false;
        }

        std::uint64_t header_size = 0;
        if (!ParseNumber(header.size, 12, header_size)) {
            result.error = logger::FormatMsg(logger::MsgCat::kUnpack_CorruptTarSizeField);
            return false;
        }

        offset += kBlockSize;
        const auto data_size = static_cast<std::size_t>(header_size);
        if (data_size > total - offset) {
            result.error = logger::FormatMsg(logger::MsgCat::kUnpack_CorruptTarTruncatedEntry);
            return false;
        }

        const char typeflag = header.typeflag == 0 ? '0' : header.typeflag;

        if (typeflag == 'x' || typeflag == 'g') {
            PaxValues parsed;
            if (!ParsePax(data.data() + offset, data_size, typeflag == 'g', parsed)) {
                result.error = logger::FormatMsg(logger::MsgCat::kUnpack_CorruptTarPaxHeader);
                return false;
            }
            if (typeflag == 'x') {
                pax = parsed;
            } else {
                if (parsed.size) global_pax.size = parsed.size;
                if (parsed.mode) global_pax.mode = parsed.mode;
                if (parsed.mtime) global_pax.mtime = parsed.mtime;
            }
            offset += RoundUp(data_size);
            continue;
        }

        if (typeflag == 'L') {
            longname = TrimNuls(std::string(
                reinterpret_cast<const char*>(data.data() + offset), data_size));
            offset += RoundUp(data_size);
            continue;
        }
        if (typeflag == 'K') {
            offset += RoundUp(data_size);
            continue;
        }

        std::string name(header.name, FieldLength(header.name, sizeof(header.name)));
        if (header.prefix[0] != 0) {
            const std::string prefix(header.prefix,
                                     FieldLength(header.prefix, sizeof(header.prefix)));
            if (!prefix.empty()) name = prefix + "/" + name;
        }
        if (!longname.empty()) {
            name = longname;
            longname.clear();
        }
        if (pax.path) name = *pax.path;

        std::uint64_t entry_size = header_size;
        if (pax.size) entry_size = *pax.size;
        else if (global_pax.size) entry_size = *global_pax.size;

        std::uint64_t mode_value = 0;
        const bool has_mode = pax.mode.has_value() || global_pax.mode.has_value() ||
                              ParseNumber(header.mode, 8, mode_value);
        if (pax.mode) mode_value = *pax.mode;
        else if (global_pax.mode) mode_value = *global_pax.mode;

        std::uint64_t header_mtime = 0;
        bool has_mtime = pax.mtime.has_value() || global_pax.mtime.has_value() ||
                         ParseNumber(header.mtime, 12, header_mtime);
        std::int64_t mtime_value = static_cast<std::int64_t>(header_mtime);
        if (pax.mtime) mtime_value = *pax.mtime;
        else if (global_pax.mtime) mtime_value = *global_pax.mtime;

        pax = PaxValues{};

        if (entry_size > total - offset) {
            result.error = logger::FormatMsg(logger::MsgCat::kUnpack_CorruptTarTruncatedEntry);
            return false;
        }

        std::string relPath;
        std::string warn;
        const bool safe = IsSafePathTar(name, options, relPath, warn);
        if (!safe) {
            if (!warn.empty()) result.warnings.push_back(warn);
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }

        fs::path destPath = outBase / filesystem::PathFromUTF8(relPath);
        if (!PathIsUnder(outBase, destPath)) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedOutsideOutputDir, relPath));
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }

        Metadata meta;
        meta.path      = destPath;
        meta.mode      = static_cast<std::uint32_t>(mode_value & kPermissionMask);
        meta.has_mode  = has_mode;
        meta.mtime     = static_cast<std::int64_t>(mtime_value);
        meta.has_mtime = has_mtime;

        const bool is_directory = typeflag == '5' ||
                                  (!relPath.empty() && relPath.back() == '/');

        if (is_directory) {
            fs::create_directories(destPath, ec);
            if (ec) {
                result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToCreateDirectory, relPath));
            } else {
                metadata.push_back(meta);
            }
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }

        if (typeflag == '2') {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedSymlink, relPath));
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }
        if (typeflag == '1') {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedHardlink, relPath));
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }
        if (typeflag == '3' || typeflag == '4' || typeflag == '6') {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedSpecialFile, relPath));
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }
        if (typeflag != '0' && typeflag != '7') {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedUnsupportedEntry, relPath));
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }

        if (destPath.has_parent_path()) {
            fs::create_directories(destPath.parent_path(), ec);
        }
        if (fs::exists(destPath, ec) && !options.overwrite) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_SkippedExistingFile, relPath));
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }

        std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToExtract, relPath));
            offset += RoundUp(static_cast<std::size_t>(entry_size));
            continue;
        }

        if (entry_size > 0) {
            out.write(reinterpret_cast<const char*>(data.data() + offset),
                      static_cast<std::streamsize>(entry_size));
        }
        out.close();
        if (!out) {
            fs::remove(destPath, ec);
            result.warnings.push_back(logger::FormatMsg(logger::MsgCat::kUnpack_FailedToWrite, relPath));
        } else {
            metadata.push_back(meta);
            result.files.push_back(relPath);
        }

        offset += RoundUp(static_cast<std::size_t>(entry_size));
    }

    if (options.preservePermissions || options.preserveTimestamps) {
        ApplyMetadata(metadata);
    }

    return true;
}

} // namespace detail
} // namespace guchho::unpack
