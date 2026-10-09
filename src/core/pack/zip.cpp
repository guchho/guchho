// =============================================================================
// src/core/pack/zip.cpp — the ZIP answer to a pack request
// =============================================================================
//
// Everything here knows it is writing a zip archive and nothing else: the
// MS-DOS timestamp field and the external attribute layout are in this file
// precisely so that they cannot leak into the parts of CreatePack() that are
// the same for every format. The list of entries arrives from the walk, which
// never asked what would be reading them.
//
// The engine is miniz, the copy vendored in src/core/filesystem/miniz and
// declared in include/guchho/miniz.hpp. Nothing here introduces a second ZIP
// implementation, and nothing here compresses anything by hand: deflate is
// miniz's job, and the level the request named is passed straight through to
// it rather than interpreted here.
//
// The one thing this file does that the request's own validation does not is
// narrow the timestamp. A file dated 1970 has a perfectly good modification
// time, and a zip entry has nowhere to put it; ClampToDosRange() keeps the
// ordering of such files instead of letting the year field wrap them into
// 2107. The request refuses a *supplied* date outside the range before any
// writer is chosen, because that value was asked for by name; a date read off
// the disk was not asked for, and clamping it is the difference between an
// approximate archive and an error on every archive of an old tree.
// =============================================================================

#include "guchho/pack.hpp"

#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"
#include "guchho/miniz.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace guchho::pack::detail {

namespace {

using detail::Entry;

namespace fs = std::filesystem;

// The file type bits the ZIP external attribute field carries in its upper
// half, as the same numbers the S_ macros spell on every POSIX system. They
// are written out rather than picked up from <sys/stat.h> because that header
// does not exist on Windows, and because a number that appears in an archive
// should not depend on which machine produced it.
constexpr std::uint32_t kRegularFileBits = 0100000u;
constexpr std::uint32_t kDirectoryBits   = 0040000u;

// A file being handed to miniz one buffer at a time.
//
// miniz asks for a range rather than for "the next chunk", so the source has
// to answer by position. Seeking on every call would be correct and needlessly
// slow; the last position is remembered and the seek skipped when it is
// already where the read starts, which is the shape miniz's own buffered
// reader produces.
struct FileSource {
    std::ifstream  stream;
    std::uint64_t  size   = 0;
    std::uint64_t  at     = 0;
    bool           failed = false;

    bool Open(const fs::path& source, std::string& error)
    {
        stream.open(source, std::ios::binary);
        if (!stream) {
            error = logger::FormatMsg(logger::MsgCat::kPack_CannotOpenForReading,
                                      filesystem::PathToUTF8(source));
            return false;
        }

        stream.seekg(0, std::ios::end);
        const std::streamoff end = stream.tellg();
        if (end < 0) {
            error = logger::FormatMsg(logger::MsgCat::kPack_CannotMeasure,
                                      filesystem::PathToUTF8(source));
            return false;
        }

        size = static_cast<std::uint64_t>(end);
        at   = 0;
        stream.seekg(0, std::ios::beg);
        return true;
    }
};

// The callback miniz reads through. Returns the number of bytes actually
// produced, and never more than the range the entry was declared to have —
// a file that grew while it was being archived keeps the size it was opened
// with, rather than putting bytes in an archive whose headers say otherwise.
std::size_t ReadEntry(void* opaque, mz_uint64 offset, void* buffer, std::size_t count)
{
    FileSource* source = static_cast<FileSource*>(opaque);
    if (source->failed || offset >= source->size) return 0;

    const std::uint64_t available = source->size - offset;
    std::size_t         want      = count;
    if (available < want) want = static_cast<std::size_t>(available);

    if (source->at != offset) {
        source->stream.clear();
        source->stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!source->stream) {
            source->failed = true;
            return 0;
        }
        source->at = offset;
    }

    source->stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(want));
    const std::size_t got = static_cast<std::size_t>(source->stream.gcount());
    if (got != want) {
        // The file is shorter than it was measured to be. miniz cannot be
        // told this from here, so it is recorded and checked once the call
        // it belongs to has returned.
        source->failed = true;
        return 0;
    }

    source->at = offset + got;
    return got;
}

// What miniz calls a failure, in the project's sentence shape.
std::string MinizError(mz_zip_archive* archive)
{
    const char* message = mz_zip_get_error_string(mz_zip_get_last_error(archive));
    return message ? message : logger::FormatMsg(logger::MsgCat::kPack_UnknownZipError);
}

// Closes a writer on every way out of the writing step, including the ones
// that leave through a return in the middle. An unfinalized archive left open
// holds a file handle and a heap block, and neither is released by the
// temporary-file guard at the caller because that one only knows about the
// path.
struct WriterGuard {
    mz_zip_archive* archive = nullptr;
    bool            active  = false;

    ~WriterGuard()
    {
        if (active) mz_zip_writer_end(archive);
    }
};

// The whole external attribute field: the file type and the permissions in
// the high half, the DOS flags in the low half. The DOS directory bit is not
// set here — miniz sets it itself when an entry's name ends in "/", which is
// the one place that knows whether the entry is one.
std::uint32_t ExternalAttributes(const Entry& entry, const PackOptions& options)
{
    const std::uint32_t bits = PermissionBits(entry, options);
    const std::uint32_t type = fs::is_directory(entry.status) ? kDirectoryBits
                                                              : kRegularFileBits;
    return (type | bits) << 16;
}

} // namespace

bool WriteZip(const std::vector<Entry>& entries, const PackOptions& options,
              const fs::path& temp, PackResult& result)
{
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!mz_zip_writer_init_file(&archive, filesystem::PathToUTF8(temp).c_str(), 0)) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_CannotStartWriting,
                                         options.outFile, MinizError(&archive));
        return false;
    }
    WriterGuard writer_guard{&archive, true};

    const mz_uint level =
        options.level ? static_cast<mz_uint>(*options.level)
                      : static_cast<mz_uint>(MZ_DEFAULT_LEVEL);

    for (const Entry& entry : entries) {
        if (!mz_zip_writer_set_default_attributes(&archive,
                                                   ExternalAttributes(entry, options))) {
            result.error = logger::FormatMsg(logger::MsgCat::kPack_CannotSetMetadata,
                                             entry.name, MinizError(&archive));
            return false;
        }

        MZ_TIME_T when = static_cast<MZ_TIME_T>(
            ClampToDosRange(static_cast<std::time_t>(EntryTime(entry.source, options))));

        if (fs::is_directory(entry.status)) {
            // An empty buffer and a name ending in "/" is how miniz spells a
            // directory: it sets the DOS directory bit itself, and refuses
            // anything but empty contents, which is exactly the constraint.
            if (!mz_zip_writer_add_mem_ex_v2(&archive, entry.name.c_str(), "", 0,
                                             nullptr, 0, level, 0, 0, &when, nullptr, 0,
                                             nullptr, 0)) {
                result.error = logger::FormatMsg(logger::MsgCat::kPack_CannotAddEntry,
                                                 entry.name, MinizError(&archive));
                return false;
            }
            continue;
        }

        FileSource source;
        if (!source.Open(entry.source, result.error)) return false;

        if (!mz_zip_writer_add_read_buf_callback(&archive, entry.name.c_str(),
                                                 &ReadEntry, &source, source.size,
                                                 &when, nullptr, 0, level, nullptr, 0,
                                                 nullptr, 0)) {
            result.error = logger::FormatMsg(logger::MsgCat::kPack_CannotAddEntry,
                                             entry.name, MinizError(&archive));
            return false;
        }
        if (source.failed) {
            result.error =
                logger::FormatMsg(logger::MsgCat::kPack_CannotReadInFull,
                                  filesystem::PathToUTF8(entry.source));
            return false;
        }
    }

    if (!mz_zip_writer_finalize_archive(&archive)) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_CannotFinish,
                                         options.outFile, MinizError(&archive));
        return false;
    }

    // Closed before the caller renames, so the bytes are on disk before
    // anything can see the file under its final name. The guard is disarmed
    // rather than allowed to run: after this the archive owns the handle it
    // has already released, and ending it twice would be a double free.
    if (!mz_zip_writer_end(&archive)) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_CannotClose,
                                         options.outFile, MinizError(&archive));
        return false;
    }
    writer_guard.active = false;
    return true;
}

} // namespace guchho::pack::detail
