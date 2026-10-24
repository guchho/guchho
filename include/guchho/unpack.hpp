// =============================================================================
// include/guchho/unpack.hpp — extracting archives to disk
// =============================================================================
//
// This header mirrors include/guchho/pack.hpp: the boundary between "somebody
// asked to extract an archive" and the bytes coming out of one. The split is
// the same — API structures here, implementation below, callers (CLI/npm)
// speak in terms of these types only.
//
// Two things this layer deliberately does not do:
//
//   * It does not create archives. Packing and unpacking are different
//     problems; packing lives in pack.hpp.
//
//   * It does not follow arbitrary symlinks outside the destination.
//     Path traversal and symlink attacks are refused/sanitized.
//
// The engine is miniz (vendored) for zip and gzip; tar is parsed in-house
// like tar writing is done in-house.
// =============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

namespace guchho::unpack {

// =========================================================================
// The formats
// =========================================================================

inline constexpr std::array<std::string_view, 3> kFormats = {
    "zip", "tar", "tar.gz",
};

// Empty means "auto-detect from extension/magic".
inline constexpr const char* kDefaultFormat = "";

// Whether a format name is one this API can read.
inline bool IsSupportedFormat(const std::string& format)
{
    if (format.empty()) return true; // auto-detect
    for (const std::string_view candidate : kFormats) {
        if (candidate == format) return true;
    }
    return false;
}

// The note that accompanies "Unsupported archive format".
std::string SupportedFormatsNote();

// =========================================================================
// What was asked for
// =========================================================================

struct UnpackOptions {
    // The archive file to extract.
    std::string archive;

    // Where to extract into. Required.
    std::string outDir;

    // Which archive format to read: one of kFormats, spelled exactly as listed.
    // Empty means auto-detect.
    std::string format;

    // Whether outDir may be created if missing (always done). Whether existing
    // files may be replaced.
    bool overwrite = false;

    // Strip leading path components from entries (like tar --strip-components).
    std::size_t stripComponents = 0;

    // Preserve permissions/timestamps where possible.
    bool preservePermissions = true;
    bool preserveTimestamps = true;
};

// =========================================================================
// What came back
// =========================================================================

struct UnpackResult {
    // The directory that was extracted into.
    std::string outDir;

    // Paths that were written (relative to outDir or as written; we return
    // paths relative to outDir where possible).
    std::vector<std::string> files;

    // What was left out on purpose, in the order it was decided.
    std::vector<std::string> warnings;

    // Empty when extracted successfully.
    std::string error;

    // Optional hint for fixing the error.
    std::string note;

    bool Ok() const { return error.empty(); }
    explicit operator bool() const { return Ok(); }
};

// =========================================================================
// Helpers
// =========================================================================

// Default output directory for a single archive input.
std::string DefaultOutDir(const std::string& archive);

// =========================================================================
// Extraction
// =========================================================================

// Extract the archive described by options.
UnpackResult CreateUnpack(const UnpackOptions& options);

} // namespace guchho::unpack
