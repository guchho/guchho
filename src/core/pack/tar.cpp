// =============================================================================
// src/core/pack/tar.cpp — the tar and tar.gz answers to a pack request
// =============================================================================
//
// A tar archive is not a compressed thing. It is a sequence of 512-byte
// headers, each followed by that entry's bytes, each rounded up to a whole
// block — and the whole file rounded up to a whole 10240-byte record, which
// is the unit GNU tar has used for blocking since before POSIX. Everything a
// reader needs is in the header itself: there is no central directory to
// fix up, so the archive is finished the moment the last block is written,
// and a run that is killed halfway leaves a file that is still readable up
// to the point it stopped.
//
// tar.gz is tar and gzip, in that order and nowhere else. The gzip layer
// below writes its own container — header, raw deflate stream, trailer —
// and is a template argument to the tar writer rather than a wrapper around
// the finished file, because a wrapper would have to buffer the archive to
// know when to write the trailer. Compressing as the blocks go out keeps the
// peak memory at one buffer whatever the size of the tree.
//
// The two limits this format has that ZIP does not are stated here rather
// than hidden. A ustar name field is 100 bytes and a prefix field is 155, so
// a longer name is split across the two; a name that cannot be split is
// written with a PAX record in front of it, which is the POSIX answer and
// the one GNU tar has been writing by default for two decades. The numeric
// fields are eleven octal digits, which caps a size and a timestamp at
// 8589934591 — about eight gigabytes and the year 2286. A number outside the
// range goes into the same PAX record rather than into a field that cannot
// hold it.
//
// What is deliberately *not* done is the ZIP writer's narrowing of the
// timestamp. DOS timestamps start in 1980 and tar does not: a file from 1975
// belongs in a tar archive with its own date on it, and clamping it to 1980
// here would be ZIP's limit travelling into a format that does not have it.
// =============================================================================

#include "guchho/pack.hpp"

#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"
#include "guchho/miniz.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace guchho::pack::detail {

namespace {

namespace fs = std::filesystem;

// The ustar geometry, as the offsets the format defines rather than as
// arithmetic on the field widths. Both spellings would be right; this one
// can be checked against a hex dump of the header by eye.
constexpr std::size_t kBlockSize    = 512;
constexpr std::size_t kRecordSize   = 10240; // GNU tar's blocking factor of 20
constexpr std::size_t kNameWidth    = 100;
constexpr std::size_t kPrefixWidth  = 155;

// Eleven octal digits, which is what a ustar size or mtime field holds.
constexpr std::uint64_t kMaxUstarNumber = 8589934591ULL;

// How much is read and compressed at a time. Large enough that the ratio of
// system calls to bytes is irrelevant, small enough that a thousand
// concurrent packs would still fit in a comfortable amount of memory.
constexpr std::size_t kChunk = 64 * 1024;

using Block = std::array<std::uint8_t, kBlockSize>;

// =========================================================================
// A header block
// =========================================================================

// Writes `width` octal digits and a NUL. A value that does not fit is
// zeroed rather than left half-written, so that the block is never left with
// a number in it that the field cannot hold — the caller is expected to have
// asked for a PAX record and is about to overwrite the field anyway.
bool PutOctal(Block& block, std::size_t offset, std::size_t width, std::uint64_t value)
{
    for (std::size_t i = width; i-- > 0;) {
        block[offset + i] = static_cast<std::uint8_t>('0' + (value & 7u));
        value >>= 3;
    }
    if (value != 0) {
        for (std::size_t i = 0; i <= width; ++i) block[offset + i] = 0;
        return false;
    }
    block[offset + width] = 0;
    return true;
}

// Writes the checksum field: six octal digits, a NUL, and a space. That
// trailing space is the one thing about a tar header that looks like a
// mistake and is not — six digits is the most a checksum can need, and the
// last byte of the field has always been a space rather than padding.
void PutChecksum(Block& block, std::uint32_t sum)
{
    PutOctal(block, 148, 6, sum);
    block[155] = ' ';
}

// Copies a name into a field. The block starts zeroed, so anything the name
// does not cover is already the NUL that terminates it.
void PutText(Block& block, std::size_t offset, std::size_t width, const std::string& text)
{
    const std::size_t count = std::min(width, text.size());
    if (count != 0) std::memcpy(block.data() + offset, text.data(), count);
}

// The header's own checksum: every byte summed with the checksum field read
// as spaces. Computed before the field is written and written afterwards,
// which is the only order that can be self-consistent.
std::uint32_t Checksum(const Block& block)
{
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        sum += (i >= 148 && i < 156) ? static_cast<std::uint32_t>(' ')
                                     : static_cast<std::uint32_t>(block[i]);
    }
    return sum;
}

// The name and prefix fields, split if they need splitting.
//
// Splits at the rightmost separator that leaves a non-empty base and fits
// both fields, because that is the split that keeps the base as short as it
// can be — and the base is the field with the smaller budget. A directory's
// name ends in "/" and that separator is not a candidate: splitting there
// would leave an empty base and drop the trailing slash that is the only
// thing in the header saying the entry is a directory.
//
// Returns false when no split fits, which is exactly the case PAX exists
// for.
bool SplitName(const std::string& name, std::string& prefix, std::string& base)
{
    if (name.size() <= kNameWidth) {
        prefix.clear();
        base = name;
        return true;
    }

    std::size_t slash = name.find_last_of('/');
    while (slash != std::string::npos) {
        if (slash > 0 && slash + 1 < name.size()) {
            const std::size_t head = slash;
            const std::size_t tail = name.size() - slash - 1;
            if (head <= kPrefixWidth && tail <= kNameWidth) {
                prefix = name.substr(0, head);
                base   = name.substr(slash + 1);
                return true;
            }
        }
        if (slash == 0) break;
        slash = name.find_last_of('/', slash - 1);
    }
    return false;
}

// =========================================================================
// PAX extended headers
// =========================================================================

// One PAX record, in the form "<length> <key>=<value>\n" where the length
// counts itself.
//
// The length cannot be written down before it is known, and it contains its
// own digit count, so the answer is a fixed point: guess, see how many
// digits the guess needs, and use that. The iteration settles within two or
// three steps for any value this can be asked about, because adding a digit
// adds one to the length and one to the digit count and nothing else.
std::string PaxRecord(const std::string& key, const std::string& value)
{
    const std::size_t body = key.size() + value.size() + 3; // ' ', '=', '\n'
    std::size_t       len  = body;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::string digits = std::to_string(len);
        if (digits.size() + body == len) return digits + " " + key + "=" + value + "\n";
        len = digits.size() + body;
    }
    // Unreachable: the two sides agree after the digit count stops changing.
    const std::string digits = std::to_string(len);
    return digits + " " + key + "=" + value + "\n";
}

// =========================================================================
// Where the bytes go
// =========================================================================

// Straight to the file.
struct FileSink {
    std::ofstream& out;
    std::string    error;

    explicit FileSink(std::ofstream& file) : out(file) {}

    bool Write(const void* data, std::size_t count)
    {
        if (count == 0) return true;
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(count));
        if (out) return true;
        error = logger::FormatMsg(logger::MsgCat::kPack_FileCouldNotBeWritten);
        return false;
    }

    bool Close()
    {
        out.close();
        if (out.fail() && error.empty()) {
            error = logger::FormatMsg(logger::MsgCat::kPack_FileCouldNotBeWritten);
        }
        return error.empty();
    }
};

// Through gzip on the way to the file.
//
// The container is written here rather than by a separate gzip pass over the
// finished archive, so that the archive is never held in memory twice and a
// run that is killed halfway still leaves a gzip file that decompresses to
// everything that got written. The XFL byte is informational — no reader
// checks it — and is set to what this machine's own gzip sets for the same
// level, so a gzip and a guchho archive of the same bytes at the same level
// differ only where they have to.
struct GzipSink {
    std::ofstream&     out;
    std::string        error;

    mz_stream          stream{};
    bool               started = false;
    bool               ended   = false;

    std::array<unsigned char, kChunk> buffer{};
    std::uint32_t                     crc  = 0;
    std::uint64_t                     size = 0;

    GzipSink(std::ofstream& file, int level) : out(file)
    {
        // Negative window bits is a raw deflate stream with no zlib header
        // around it, which is what gzip wraps. The memory level is 8 —
        // zlib's own default — and miniz takes the argument without using it.
        if (mz_deflateInit2(&stream, level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 8,
                            MZ_DEFAULT_STRATEGY) != MZ_OK) {
            error = logger::FormatMsg(logger::MsgCat::kPack_CompressionCouldNotStart);
            return;
        }
        started = true;

        // CM is always deflate; FLG is zero because nothing below needs a
        // footer saying where the file came from; MTIME is zero because the
        // archive is a build product rather than a copy of one file, and a
        // zero it can be argued about is better than a timestamp that moves
        // every time the archive is rebuilt; OS is unknown rather than this
        // machine's, because the bytes are the same either way.
        unsigned char header[10] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0xff};
        header[8] = level <= 1 ? 4 : (level >= 9 ? 2 : 0);
        if (!Emit(header, sizeof(header))) {
            error = logger::FormatMsg(logger::MsgCat::kPack_CompressionCouldNotStart);
        }
    }

    ~GzipSink()
    {
        if (started && !ended) mz_deflateEnd(&stream);
    }

    GzipSink(const GzipSink&)            = delete;
    GzipSink& operator=(const GzipSink&) = delete;

    bool Write(const void* data, std::size_t count)
    {
        if (!error.empty()) return false;

        const auto* bytes = static_cast<const unsigned char*>(data);
        std::size_t left  = count;
        while (left != 0) {
            const std::size_t take = std::min<std::size_t>(left, kChunk);
            // The checksum is of the uncompressed stream, and miniz does not
            // keep one for a raw-deflate caller, so it is accumulated here —
            // in the chunks the files are read in, never in one call that
            // could outgrow mz_ulong.
            crc = static_cast<std::uint32_t>(
                mz_crc32(crc, bytes, static_cast<mz_ulong>(take)));
            size += take;
            if (!Compress(bytes, take)) return false;
            bytes += take;
            left -= take;
        }
        return true;
    }

    bool Close()
    {
        if (!error.empty()) return false;

        stream.next_in  = nullptr;
        stream.avail_in = 0;
        for (;;) {
            stream.next_out  = buffer.data();
            stream.avail_out = static_cast<unsigned int>(buffer.size());
            const int rc = mz_deflate(&stream, MZ_FINISH);
            const std::size_t made = buffer.size() - stream.avail_out;
            if (made != 0 && !Emit(buffer.data(), made)) return false;
            if (rc == MZ_STREAM_END) break;
            if (rc != MZ_OK && rc != MZ_BUF_ERROR) {
                error = logger::FormatMsg(logger::MsgCat::kPack_CompressionFailed);
                return false;
            }
            if (made == 0) {
                error = logger::FormatMsg(logger::MsgCat::kPack_CompressionFailed);
                return false;
            }
        }

        // Released before the trailer rather than at the end of the scope, so
        // that a failure to write the trailer is still reported instead of
        // being swallowed by the destructor on the way out.
        if (mz_deflateEnd(&stream) != MZ_OK) {
            started = false;
            error   = logger::FormatMsg(logger::MsgCat::kPack_CompressionFailed);
            return false;
        }
        started = false;
        ended   = true;

        // The CRC-32 of the uncompressed bytes and their length, both little
        // endian and both taken mod 2^32 — which is what the format says and
        // not a truncation, however large the archive is.
        unsigned char trailer[8];
        for (int i = 0; i < 4; ++i) trailer[i] = static_cast<unsigned char>(crc >> (i * 8));
        const std::uint32_t isize = static_cast<std::uint32_t>(size);
        for (int i = 0; i < 4; ++i) {
            trailer[4 + i] = static_cast<unsigned char>(isize >> (i * 8));
        }
        if (!Emit(trailer, sizeof(trailer))) return false;

        out.close();
        if (out.fail() && error.empty()) {
            error = logger::FormatMsg(logger::MsgCat::kPack_FileCouldNotBeWritten);
        }
        return error.empty();
    }

private:
    bool Emit(const void* data, std::size_t count)
    {
        if (count == 0) return true;
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(count));
        if (out) return true;
        error = logger::FormatMsg(logger::MsgCat::kPack_FileCouldNotBeWritten);
        return false;
    }

    bool Compress(const unsigned char* data, std::size_t count)
    {
        stream.next_in  = const_cast<unsigned char*>(data);
        stream.avail_in = static_cast<unsigned int>(count);

        while (stream.avail_in != 0) {
            const unsigned int before = stream.avail_in;
            stream.next_out           = buffer.data();
            stream.avail_out          = static_cast<unsigned int>(buffer.size());

            const int rc = mz_deflate(&stream, MZ_NO_FLUSH);
            if (rc != MZ_OK && rc != MZ_BUF_ERROR) {
                error = logger::FormatMsg(logger::MsgCat::kPack_CompressionFailed);
                return false;
            }

            const std::size_t made = buffer.size() - stream.avail_out;
            if (made == 0 && stream.avail_in == before) {
                error = logger::FormatMsg(logger::MsgCat::kPack_CompressionMadeNoProgress);
                return false;
            }
            if (made != 0 && !Emit(buffer.data(), made)) return false;
        }
        return true;
    }
};

// =========================================================================
// The archive itself
// =========================================================================

// Splits a name into a ustar prefix and base, or reports that it cannot be
// split and therefore needs a PAX record.
void AddPathRecord(const Entry& entry, std::string& prefix, std::string& base,
                   std::string& pax)
{
    if (!SplitName(entry.name, prefix, base)) {
        pax += PaxRecord("path", entry.name);
        // The name that goes in the header still has to be something, and it
        // still has to fit. Truncating is safe here because the PAX record in
        // front of the header carries the real name and every PAX-aware
        // reader uses it; a reader that ignores PAX gets a wrong name rather
        // than a corrupt archive, which is the trade every tar implementation
        // makes.
        prefix.clear();
        base = entry.name.substr(0, kNameWidth);
    }
}

template <typename Sink>
bool WriteTarTo(const std::vector<Entry>& entries, const PackOptions& options, Sink& sink,
                PackResult& result)
{
    std::uint64_t written = 0;

    const Block zeros{};

    auto fail = [&]() {
        result.error = logger::FormatMsg(
            logger::MsgCat::kPack_CannotWriteArchive, options.outFile,
            sink.error.empty()
                ? logger::FormatMsg(logger::MsgCat::kPack_ArchiveCouldNotBeWritten)
                : sink.error);
        return false;
    };

    auto put = [&](const void* data, std::size_t count) -> bool {
        if (!sink.Write(data, count)) return fail();
        written += count;
        return true;
    };

    auto pad = [&](std::uint64_t count) -> bool {
        while (count != 0) {
            const std::size_t take =
                static_cast<std::size_t>(std::min<std::uint64_t>(count, kBlockSize));
            if (!put(zeros.data(), take)) return false;
            count -= take;
        }
        return true;
    };

    for (const Entry& entry : entries) {
        const bool is_directory = fs::is_directory(entry.status);

        std::uint64_t file_size = 0;
        if (!is_directory) {
            std::error_code ec;
            file_size = fs::file_size(entry.source, ec);
            if (ec) {
                result.error = logger::FormatMsg(
                    logger::MsgCat::kPack_CannotMeasureWithError,
                    filesystem::PathToUTF8(entry.source), ec.message());
                return false;
            }
        }

        // Unlike the ZIP writer, nothing is narrowed here. A timestamp
        // before the Unix epoch is refused rather than wrapped, because
        // wrapping it would make a file from 1969 look like one from the far
        // future and the only honest answers are the real date or none.
        const std::int64_t when = EntryTime(entry.source, options);
        if (when < 0) {
            result.error = logger::FormatMsg(logger::MsgCat::kPack_TimestampBeforeUnixEpoch,
                                             entry.name);
            result.note = logger::FormatMsg(
                logger::MsgCat::kPack_TimestampBeforeUnixEpochNote);
            return false;
        }
        const std::uint64_t mtime = static_cast<std::uint64_t>(when);

        std::string prefix;
        std::string base;
        std::string pax;
        AddPathRecord(entry, prefix, base, pax);

        Block header{};

        // Eleven octal digits is the field's whole capacity. A number outside
        // it is not written at all — the field stays as the zero the block
        // started with — and travels in the PAX record instead.
        const bool size_fits =
            file_size <= kMaxUstarNumber && PutOctal(header, 124, 11, file_size);
        const bool time_fits =
            mtime <= kMaxUstarNumber && PutOctal(header, 136, 11, mtime);
        if (!size_fits) pax += PaxRecord("size", std::to_string(file_size));
        if (!time_fits) pax += PaxRecord("mtime", std::to_string(mtime));

        if (!pax.empty()) {
            // A PAX entry is an ordinary header, its own bytes, and then the
            // header it belongs to. Its name is the fixed one every tar
            // writes, so that the record in front of a file never looks like
            // a file of its own to a tool that lists an archive.
            Block ext{};
            PutText(ext, 0, kNameWidth, "././@PaxHeader");
            PutOctal(ext, 124, 11, pax.size());
            PutOctal(ext, 136, 11, mtime);
            ext[156] = 'x';
            std::memcpy(ext.data() + 257, "ustar", 5);
            ext[263] = '0';
            ext[264] = '0';
            PutChecksum(ext, Checksum(ext));

            if (!put(ext.data(), ext.size())) return false;
            if (!put(pax.data(), pax.size())) return false;
            if (!pad((kBlockSize - (pax.size() % kBlockSize)) % kBlockSize)) return false;
        }

        PutText(header, 0, kNameWidth, base);
        PutOctal(header, 100, 7, PermissionBits(entry, options));
        PutOctal(header, 108, 7, 0); // uid: an archive is not about who built it
        PutOctal(header, 116, 7, 0); // gid
        header[156] = is_directory ? '5' : '0';
        std::memcpy(header.data() + 257, "ustar", 5);
        header[263] = '0';
        header[264] = '0';
        PutText(header, 345, kPrefixWidth, prefix);
        PutChecksum(header, Checksum(header));

        if (!put(header.data(), header.size())) return false;
        if (is_directory) continue;

        std::ifstream file(entry.source, std::ios::binary);
        if (!file) {
            result.error =
                logger::FormatMsg(logger::MsgCat::kPack_CannotOpenForReading,
                                  filesystem::PathToUTF8(entry.source));
            return false;
        }

        // Exactly as many bytes as the header promised. A file that grew
        // keeps its declared size; a file that shrank cannot be written
        // honestly at all, because the next block in the archive is the next
        // entry's header and a short read would put file contents there.
        std::uint64_t remaining = file_size;
        std::array<char, kChunk> chunk{};
        while (remaining != 0) {
            const std::size_t want =
                static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk.size()));
            file.read(chunk.data(), static_cast<std::streamsize>(want));
            const std::size_t got = static_cast<std::size_t>(file.gcount());
            if (got != want) {
                result.error =
                    logger::FormatMsg(logger::MsgCat::kPack_CannotReadInFull,
                                      filesystem::PathToUTF8(entry.source));
                return false;
            }
            if (!put(chunk.data(), got)) return false;
            remaining -= got;
        }

        if (!pad((kBlockSize - (file_size % kBlockSize)) % kBlockSize)) return false;
    }

    // Two empty blocks say the archive is over, and the padding out to a
    // whole record is what makes the file the size a reader expects it to
    // be. Both are parts of the format rather than an optimisation: a reader
    // stops at the empty blocks, and a tape does not like a short last file.
    if (!put(zeros.data(), zeros.size())) return false;
    if (!put(zeros.data(), zeros.size())) return false;
    if (written % kRecordSize != 0 && !pad(kRecordSize - (written % kRecordSize))) {
        return false;
    }

    if (!sink.Close()) return fail();
    return true;
}

} // namespace

bool WriteTar(const std::vector<Entry>& entries, const PackOptions& options,
              const fs::path& temp, PackResult& result)
{
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error =
            logger::FormatMsg(logger::MsgCat::kPack_CannotStartWritingFileNotOpened,
                              options.outFile);
        return false;
    }

    // Uncompressed unless the format said otherwise. The request's own
    // validation has already refused a level on plain tar, so by the time
    // this runs a level and a format that cannot take it cannot meet.
    if (options.format == "tar.gz") {
        const int level = options.level ? *options.level : MZ_DEFAULT_LEVEL;
        GzipSink  sink(out, level);
        if (!sink.error.empty()) {
            result.error = logger::FormatMsg(
                logger::MsgCat::kPack_CannotWriteArchive, options.outFile,
                sink.error.empty()
                    ? logger::FormatMsg(logger::MsgCat::kPack_ArchiveCouldNotBeWritten)
                    : sink.error);
            return false;
        }
        return WriteTarTo(entries, options, sink, result);
    }

    FileSink sink(out);
    return WriteTarTo(entries, options, sink, result);
}

} // namespace guchho::pack::detail
