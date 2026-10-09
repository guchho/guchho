// =============================================================================
// include/guchho/pack.hpp — turning paths on disk into an archive
// =============================================================================
//
// This header is the boundary between "somebody asked for an archive" and the
// bytes of one. Everything above it — reading a command line, answering a
// service request, validating the options a JavaScript caller passed — speaks
// in terms of the two structures below. Everything below is a function of
// those two values plus the state of the disk, and it knows nothing about a
// command line, a pipe or a module.
//
// That split exists for two reasons. The first is that the interesting half of
// a packing operation is the half that decides what goes into the archive:
// which entries, under which names, with which metadata, and what is
// deliberately left out. A command that both parses a flag and walks a tree
// can only be tested by running the command; a function that takes a list of
// paths and hands back a result can be tested by comparing the archive it
// produced against the list it was given.
//
// The second is that there are two callers with nothing else in common. The
// CLI prints; the npm package returns data. Neither of them may find its own
// vocabulary down here, which is why there is no "flag", no exit code and no
// logger in this file — the refusal, the warning and the message are data, and
// where they go is the caller's decision.
//
// Two things this layer deliberately does not do:
//
//   * It does not extract. Creating an archive and reading one are different
//     problems, and the reading half already exists in
//     src/core/filesystem/filesystem_zip.cpp with its own conventions.
//
//   * It does not encrypt. There is no password field below, and an option
//     called "date" or "mode" is never going to become one: both are archive
//     metadata, written into the entry headers, and neither has anything to do
//     with reading the archive back.
//
// The engine is miniz, the copy vendored in src/core/filesystem/miniz and
// declared in include/guchho/miniz.hpp. Nothing here introduces a second ZIP
// implementation.
//
// The last section below is marked "internal" and means it. It holds the
// pieces every archive writer is given — the entry type, the shared helpers,
// and one declaration per format — and it lives here rather than in a header
// of its own only because those pieces are the same question the structures
// above are: what goes into an archive, and with which metadata. It is not
// part of this API, the names in it are in namespace detail for that reason,
// and nothing outside src/core/pack is meant to reach for them.
// =============================================================================

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace guchho::pack {

// =========================================================================
// The formats
// =========================================================================

// Every format this API writes, in the order a list of them is printed, with
// the default first.
//
// This is the whole of the format vocabulary. "pack" is the command and the
// format is a fact about the bytes inside it: a request says pack, and
// --format (or the npm `format` option) says what those bytes are. The list
// lives here rather than in the command line or in JavaScript so that every
// front end reads the same one and an unsupported format is refused with one
// message, whoever asked.
//
// "tar.gz" is one name rather than two: a person who wants it types it in one
// piece, the bytes after the dot are not a hint about the format underneath,
// and the compression that produces it is part of what the name promises. It
// sits last because it is the only one of the three that is not simply an
// archive format on its own.
inline constexpr std::array<std::string_view, 3> kFormats = {
    "zip", "tar", "tar.gz",
};

// The format this API writes when the request does not name one.
inline constexpr const char* kDefaultFormat = "zip";

static_assert(kFormats.front() == kDefaultFormat,
              "the default format must be the first one listed");

// Whether a format name is one this API can write.
//
// The name is compared exactly: a format is spelled in lower case and a
// spelling that differs in case is a different answer, not a near miss.
// IsSupportedFormat() does not know about the default — an empty name is not
// a supported format, it is a name that was never given, which is why
// PackOptions::format is normalized to kDefaultFormat before it is asked.
//
// Input:  IsSupportedFormat("zip")     -> true
// Input:  IsSupportedFormat("tar")     -> true
// Input:  IsSupportedFormat("tar.gz")  -> true
// Input:  IsSupportedFormat("7z")      -> false
// Input:  IsSupportedFormat("ZIP")     -> false
// Input:  IsSupportedFormat("")        -> false, "" is not a format name
inline bool IsSupportedFormat(const std::string& format)
{
    for (const std::string_view candidate : kFormats) {
        if (candidate == format) return true;
    }
    return false;
}

// Whether a compression level means anything to this format.
//
// zip and tar.gz are compressed: the level says how hard to try. tar is not,
// it stores its entries exactly as they are on disk, so a level on a tar
// request would be a number that changed nothing. It is refused rather than
// ignored, because a caller who asked for level 9 and received an
// uncompressed archive has either mistyped the format or is expecting an
// effect that is not coming, and both are worth saying out loud rather than
// discovering by comparing file sizes afterwards.
//
// Input:  LevelAppliesTo("zip")     -> true
// Input:  LevelAppliesTo("tar.gz")  -> true
// Input:  LevelAppliesTo("tar")     -> false
inline bool LevelAppliesTo(const std::string& format) { return format != "tar"; }

// The note that accompanies "Unsupported archive format", naming every format
// this API writes.
//
// One sentence built here so the library and the command line cannot print
// different lists: cli_pack.cpp refuses a format before it reaches the
// library, and a person who reads that refusal in a test and then types it
// must be given the same answer both times. The npm package assembles the
// same sentence from its own copy of the same three names, and a test runs
// the two against each other for the same reason.
//
// Output: "Only \"zip\", \"tar\" and \"tar.gz\" are supported today."
std::string SupportedFormatsNote();

// =========================================================================
// What was asked for
// =========================================================================

// Everything an archive is asked to be, and nothing about how the answer is
// reported.
//
// There is no default for outFile. A default that guessed from a list of
// inputs would be a guess, and the one case it could get right — a single
// input — is a case the caller already knows how to answer, which is what
// DefaultOutFile() below is for. The CLI computes a name and passes it in;
// the npm API requires one.
struct PackOptions {
    // The files and directories to archive, in the order they were given.
    //
    // Each input becomes an archive root named after itself, so
    // { "dist/", "src/" } produces "dist/..." and "src/..." inside one
    // archive rather than the two trees dumped on top of each other. The
    // name is the input's last path component: "dist/" and "./dist" and
    // "a/b/../dist" all root at "dist", and no absolute host path, no
    // "." and no ".." is ever stored.
    //
    // An input may be a symbolic link. It is followed exactly once, at the
    // top level, because naming a link is naming the thing it points at.
    // Links found *inside* the walk are never followed; see CreatePack().
    std::vector<std::string> inputs;

    // Where the archive is written, as a UTF-8 path. Its parent directory is
    // created if it does not exist. Required.
    std::string outFile;

    // Which archive format to write: one of kFormats, spelled exactly as it
    // is listed there. Empty means kDefaultFormat, because a caller that did
    // not ask for a particular format has not asked for anything surprising.
    // Anything else that IsSupportedFormat() refuses is rejected by
    // CreatePack() before a single entry is read, so a format nobody can
    // write never turns into an archive nobody can open.
    std::string format;

    // Compression level, 0 through 9, for the formats LevelAppliesTo() says
    // are compressed. 0 stores every entry uncompressed; 9 compresses
    // hardest. An empty value means the library's own default, which is 6.
    // Anything outside 0..9 is rejected rather than clamped, so a typo
    // cannot quietly turn into a different level. A level paired with tar is
    // rejected too — see LevelAppliesTo().
    std::optional<int> level;

    // Whether outFile may be replaced when it is already there. Off by
    // default: a command that silently overwrote somebody's release.zip
    // would be a command people stopped running twice.
    bool overwrite = false;

    // The modification time written into every entry's header, as seconds
    // since the Unix epoch. An empty value means each entry keeps the time
    // it has on disk.
    //
    // What the value survives depends on the format, which is a fact about
    // the format and not a choice this API makes:
    //
    //   * tar stores whole seconds for any date from the epoch onwards, and
    //     keeps a value its fixed field cannot hold by writing an extended
    //     header instead of rounding it.
    //
    //   * zip stores this in MS-DOS form: a local timestamp with two-second
    //     precision, covering 1980 through 2107 and nothing else. The value
    //     is interpreted in the machine's local time zone, which is how
    //     miniz converts it and how every zip reader converts it back. A
    //     value outside that range is rejected rather than wrapped — and it
    //     is only rejected for zip, because tar has no such range to be
    //     outside of.
    std::optional<std::int64_t> date;

    // The permission bits written into every entry's external attributes,
    // as Unix mode bits masked to 07777 (setuid, setgid and sticky included
    // when they are asked for). An empty value means each entry keeps the
    // permissions it has on disk.
    //
    // Directories are not given this number verbatim: when mode is set, the
    // execute bits are OR'd into the directory's copy so that a file-style
    // value like 0644 never produces a directory nobody can enter. When
    // mode is empty, the directory's own on-disk permissions are used
    // unchanged — they already had to be traversable for this walk to
    // happen.
    //
    // This is metadata. It is written into the archive's entry headers and
    // describes the file; it is not a password, not an access-control
    // mechanism, and not something a reader is asked for.
    std::optional<std::uint32_t> mode;
};

// Forward-declared because the writers in the section below report into it,
// and its own definition comes after that section; a reference needs the
// name, not the layout. It is declared here rather than inside detail so
// that the unqualified PackResult a writer names resolves to this one
// rather than to an empty type of detail's own.
struct PackResult;

// =========================================================================
// Internal — the pieces every archive writer is given
// =========================================================================
//
// CreatePack() is not one program; it is one request with several possible
// answers, and the question this section exists to settle is where the shared
// half stops. The answer: everything up to "which files, under which names,
// with which metadata" belongs to the request and lives in pack.cpp, and
// everything from "and here are the bytes" belongs to the format.
//
// What crosses that line is deliberately small. A writer is handed the list
// of entries, the options, and the path of the temporary file to fill; it
// reports a failure in the project's sentence shape and returns. It is not
// given the destination, the decision about whether to overwrite, or the
// temporary-file guard — those are about *where* the archive goes, which is
// the same question for every format, and answering them once is what stops
// three writers from becoming three slightly different opinions about whether
// a failed run leaves a file behind.
//
// The helpers below are shared because the answer to each is the same for
// every format, not because ZIP and tar happen to agree today:
//
//   * Quoted() is how this project puts a path in a sentence.
//   * PermissionBits() is "the mode this entry should carry", which is a
//     question about the request and the disk. What each format does with
//     the number is that format's business.
//   * EntryTime() is "the time this entry should carry", before any format
//     has had its say about what it can hold. A format that cannot hold the
//     answer — zip, with its 1980-to-2107 MS-DOS field — narrows it itself
//     rather than narrowing it here, where tar would inherit a limit it does
//     not have.
//
// Nothing here is installed as API. It is in namespace detail, nothing
// outside src/core/pack names it, and it carries no guarantee.
namespace detail {

namespace fs = std::filesystem;

// =========================================================================
// What of a mode is a mode
// =========================================================================

// setuid, setgid and sticky are the top three bits of the low twelve, so this
// keeps them and drops anything above.
inline constexpr std::uint32_t kPermissionMask = 07777u;

// The execute bits a directory needs before a reader can walk into it. Used
// as a floor rather than as a value: see PermissionBits() below.
inline constexpr std::uint32_t kExecuteBits = 0111u;

// The years a ZIP entry header can express. DOS timestamps start in 1980 and
// the year field is seven bits wide, so 2107 is not a limit somebody chose —
// it is what seven bits of "years since 1980" adds up to.
//
// These live here rather than in zip.cpp because the request's own validation
// asks the same question: a date outside this range is refused before any
// writer is chosen, and it is refused only for the format that has the limit.
inline constexpr int kMinDosYear = 1980;
inline constexpr int kMaxDosYear = 2107;

// =========================================================================
// What goes in
// =========================================================================

// One thing that goes into the archive.
//
// Everything here is format-neutral on purpose. A ZIP writer and a tar writer
// agree about what an entry is because the walk that produced the list did
// not know which of them was going to read it; a format-specific field added
// here would be a field the other format had to pretend about.
struct Entry {
    // The archive path, "/"-separated, with a trailing "/" for a directory.
    std::string name;

    // Where it came from. Used for the timestamp and, for a directory, for
    // nothing else — the contents were read while the tree was walked.
    fs::path source;

    // The status the walk already read. Carried rather than read again so
    // that a file costs one stat instead of two: the permission bits are the
    // only thing a later step needs, and asking the disk for them a second
    // time would double the system calls of the whole operation for an
    // answer that has not changed since the entry was typed.
    fs::file_status status;
};

// How this project puts a path into a sentence.
inline std::string Quoted(const std::string& text)
{
    return "\"" + text + "\"";
}

// =========================================================================
// Time
// =========================================================================

// The calendar year "t" falls in, read in the machine's own time zone.
//
// The zone matters because that is how a ZIP entry is written and how one is
// read back: miniz converts with localtime() going out and mktime() coming
// in, so a timestamp only round trips if both ends agree about the offset,
// and a timestamp converted here in UTC would be an hour out on a machine
// that is not.
inline bool LocalYear(std::time_t t, int& year)
{
#ifdef _WIN32
    struct tm parts {};
    if (localtime_s(&parts, &t) != 0) return false;
#else
    struct tm parts {};
    if (localtime_r(&t, &parts) == nullptr) return false;
#endif
    year = parts.tm_year + 1900;
    return true;
}

// A timestamp the DOS field can hold, for one it cannot.
//
// Wrapping is the alternative and is worse: the year field is unsigned, so a
// file dated 1970 would be recorded as 2107 or thereabouts and read back as
// a date nobody meant. Clamping to the edge of the range keeps the ordering
// of old files intact and is honest about being an approximation. The clamp
// is expressed as a local midnight, so the result is still a date a reader
// will show as a date.
//
// ZIP's answer, applied by ZIP's writer. It is a no-op for a timestamp inside
// the range, which is what makes it safe to call on one that was already
// checked.
inline std::time_t ClampToDosRange(std::time_t t)
{
    int year = 0;
    if (LocalYear(t, year)) {
        if (year >= kMinDosYear && year <= kMaxDosYear) return t;

        struct tm bound {};
        bound.tm_isdst = -1;
        if (year < kMinDosYear) {
            bound.tm_year = kMinDosYear - 1900;
            bound.tm_mon  = 0;
            bound.tm_mday = 1;
        } else {
            bound.tm_year = kMaxDosYear - 1900;
            bound.tm_mon  = 11;
            bound.tm_mday = 31;
            bound.tm_hour = 23;
            bound.tm_min  = 59;
            bound.tm_sec  = 59;
        }
        const std::time_t clamped = std::mktime(&bound);
        if (clamped != static_cast<std::time_t>(-1)) return clamped;
    }
    return t;
}

// The modification time of a file on disk, as seconds since the epoch,
// exactly as the disk has it.
//
// std::filesystem hands back a time on its own clock, which is not the system
// clock and has no guaranteed epoch. The two are related by reading both now
// and taking the difference, which is the portable way to do it without
// reaching for a platform-specific conversion: the error is the gap between
// the two calls to now(), which is far below the second of precision any of
// these formats is going to store anyway.
//
// Deliberately not clamped to any format's range. This is the truth about the
// file; a format that cannot hold it says so itself.
inline std::time_t DiskWriteTime(const fs::path& source)
{
    std::error_code ec;
    const fs::file_time_type written = fs::last_write_time(source, ec);
    if (ec) return std::time(nullptr);

    const auto file_now = fs::file_time_type::clock::now();
    const auto sys_now  = std::chrono::system_clock::now();
    const auto adjusted = sys_now + std::chrono::duration_cast<
                                     std::chrono::system_clock::duration>(
                                         written - file_now);
    return std::chrono::system_clock::to_time_t(adjusted);
}

// The timestamp one entry should carry: the one that was asked for when there
// was one, and the file's own otherwise. Before any format has narrowed it.
inline std::int64_t EntryTime(const fs::path& source, const PackOptions& options)
{
    if (options.date) return *options.date;
    return static_cast<std::int64_t>(DiskWriteTime(source));
}

// =========================================================================
// Metadata
// =========================================================================

// The permission bits an entry should carry, as Unix mode bits.
//
// An explicit mode is taken at face value; otherwise the bits the walk already
// read are used, with a sensible default for the case where the filesystem has
// none (a Windows volume reporting permissions as "unknown" is not a file
// nobody may read).
//
// A directory is never given a mode without execute bits. Leaving them off
// would describe a directory nobody can walk into, which is not a thing the
// mode field is for: mode is a record of what the file is, not a request to
// make it that way, and 0644 on a directory has never described anything.
// The floor is applied only when mode was asked for, because when it is empty
// the directory's own on-disk permissions are already correct — they had to
// be traversable for this walk to have happened — and turning them into 0711
// to satisfy a rule would be the archive disagreeing with the disk about who
// may walk where.
inline std::uint32_t PermissionBits(const Entry& entry, const PackOptions& options)
{
    std::uint32_t bits;
    if (options.mode) {
        bits = *options.mode & kPermissionMask;
    } else {
        bits = static_cast<std::uint32_t>(entry.status.permissions()) & kPermissionMask;
        if (entry.status.permissions() == fs::perms::unknown) bits = 0;
        bits = bits ? bits : (fs::is_directory(entry.status) ? 0755u : 0644u);
    }

    if (fs::is_directory(entry.status) && (bits & kExecuteBits) == 0) {
        bits |= kExecuteBits;
    }
    return bits;
}

// =========================================================================
// The writers
// =========================================================================

// Each writer opens `temp`, writes every entry into it, and closes it. On
// success it returns true with the file closed and on disk; on failure it
// returns false with result.error set to a sentence that can be printed as it
// stands. Neither renames, neither removes, and neither touches the
// destination: the temporary file belongs to the caller's guard, and the move
// into place is one step that every format takes the same way.
//
// Both are given `const`: a writer reads entries and must not reorder or
// re-tag them, because the list is the record of what the walk decided and
// two writers handed the same list have to produce archives with the same
// contents in the same order.
bool WriteZip(const std::vector<Entry>& entries, const PackOptions& options,
              const fs::path& temp, PackResult& result);

bool WriteTar(const std::vector<Entry>& entries, const PackOptions& options,
              const fs::path& temp, PackResult& result);

} // namespace detail

// =========================================================================
// What came back
// =========================================================================

// The outcome of one attempt to write an archive.
//
// The three ways it can be read are all supported on purpose. A caller that
// only cares whether it worked reads Ok(); one that wants the message reads
// error; one that wants to know what was quietly left out reads warnings.
// Nothing else is in here, because nothing else is knowable until the archive
// has been finalized and renamed into place.
struct PackResult {
    // The archive that was written, as the path it was asked for. Set only
    // on success: an operation that failed part way through has not produced
    // a file at that path, and saying otherwise would be the misleading
    // success the plan forbids.
    std::string path;

    // Its size in bytes, read from disk after the archive was closed rather
    // than accumulated while it was written, so it counts whatever the
    // format keeps at the end — a zip central directory, a tar's end blocks —
    // as well as the entries themselves.
    std::uint64_t size = 0;

    // What was left out on purpose, in the order it was decided. A symbolic
    // link that was not followed, a file type with no representation in an
    // archive, the output file sitting inside its own input tree. None
    // of these is a failure — an archive that skipped a symlink is still a
    // correct archive — but none of them may happen silently either, or a
    // person who expected a link to be in there has no way to find out that
    // it is not.
    std::vector<std::string> warnings;

    // Empty when the archive was written. Otherwise a sentence that can be
    // printed as it stands, starting with a capital and carrying no prefix:
    // the prefix belongs to whoever prints it.
    std::string error;

    // How to act on "error", when there is something to act on. Empty for
    // most failures, because most of them are already complete. Set for the
    // few where the fix is a different spelling of the same request, so the
    // caller can put it on a second line instead of burying it in the
    // sentence.
    std::string note;

    bool Ok() const { return error.empty(); }
    explicit operator bool() const { return Ok(); }
};

// =========================================================================
// Generation
// =========================================================================

// The output file a single input would get if nobody named one, for a request
// that writes `format`.
//
// The rule is one line long and never guesses: drop any trailing separators,
// replace the last component's extension with the format's own, or append it
// when the name has none. It is deliberately the extension that is replaced
// rather than the whole name, so "dist" and "dist/" both become "dist.zip"
// and a file called "report.json" becomes "report.zip" instead of
// "report.json.zip". A name with no extension — a directory called "dist", a
// file called "Makefile" — keeps its whole name and gains one.
//
// The format's extension is appended as it stands, which means it may be two
// components long: tar.gz writes ".tar.gz", and the rule above stops as soon
// as the name already ends in it, because erasing only the last component of
// "release.tar.gz" would leave "release.tar" to which ".tar.gz" would then be
// added again. A name already written for this format is returned unchanged.
//
// A hidden file keeps its dot: ".bashrc" has an extension only by accident
// of the rule above, and turning it into ".zip" would be nonsense.
//
// Input:  DefaultOutFile("dist/",       "zip")     -> "dist.zip"
// Input:  DefaultOutFile("dist/",       "tar")     -> "dist.tar"
// Input:  DefaultOutFile("dist/",       "tar.gz")  -> "dist.tar.gz"
// Input:  DefaultOutFile("src/app.js",  "tar")     -> "src/app.tar"
// Input:  DefaultOutFile("release.tar.gz", "tar.gz") -> "release.tar.gz"
// Input:  DefaultOutFile("archive.zip", "zip")     -> "archive.zip", which is
//         the input itself; CreatePack refuses that combination rather than
//         archiving a file into itself.
// Input:  DefaultOutFile(".bashrc",     "zip")     -> ".bashrc.zip"
std::string DefaultOutFile(const std::string& input, const std::string& format);

// Writes the archive the request describes, or explains why it did not.
//
// The format is settled first, before an input is read or the destination is
// touched: an empty options.format means kDefaultFormat, and a name
// IsSupportedFormat() refuses comes back as an error with a note, with no
// file written and no temporary file left behind. A format nobody can write
// must not become an archive nobody can open. A level that LevelAppliesTo()
// says the format does not take is refused the same way, at the same step.
//
// Input:  { format = "7z", inputs = { "dist/" }, outFile = "release.7z" }
// Output: no path, error "Unsupported archive format: \"7z\"", and the note
//         naming the formats that are supported.
//
// Input:  { format = "tar", inputs = { "dist/" }, outFile = "release.tar",
//           level = 9 }
// Output: no path, error naming the level as something tar does not take,
//         and a note saying what does take one.
//
// On success the archive is complete, finalized and closed before this
// returns: path, size and Ok() are all about a finished file, and there is no
// state left open that a later call has to finish. On failure the destination
// is left exactly as it was found — an existing archive is untouched, and no
// partial file is left behind under the requested name.
//
// That last guarantee is the reason the work happens in two steps. The
// archive is written to a temporary file next to the destination, finalized
// there, and only then renamed over the destination. A rename within one
// directory is atomic on every platform this builds on, so the window in
// which the destination does not exist is empty rather than merely short, and
// a crash part way through leaves a stray temporary file instead of a
// truncated archive that looks finished.
//
// The archive itself:
//
//   * Entry names use "/" on every platform, are never absolute, and never
//     contain "." or "..". Each input contributes one root, named after the
//     input's last path component; "dist/" and "src/" side by side produce
//     "dist/..." and "src/...".
//
//   * Hidden files are included. Nothing is filtered by name, so ".gitignore"
//     and ".env" are archived like anything else.
//
//   * Symbolic links found inside a walk are skipped with a warning and never
//     followed. Following one is how an archive silently grows to contain a
//     copy of somewhere else entirely, and there is no depth at which that
//     stops being a surprise. A link named on the command line is the
//     exception: naming it is naming what it points at, so it is followed
//     exactly once, and the root it produces is the link's own name.
//
//   * Two inputs that would produce the same archive path are an error, not a
//     silent second copy: a reader would extract whichever the central
//     directory listed last, which makes the archive's contents depend on
//     an ordering nobody chose.
//
//   * The output file is excluded when it sits inside one of the input trees,
//     with a warning, so "guchho pack dist/ -o dist/out.zip --allow-overwrite"
//     does not archive the previous run's output into the next one.
//
//   * Empty directories are represented by a trailing-slash entry, because a
//     directory that held nothing would otherwise leave no trace in an
//     archive that is otherwise a faithful copy of the tree.
//
//   * Every other file type — a fifo, a socket, a device — is skipped with a
//     warning, since none of the formats written here has anywhere to put
//     one.
//
// Input:  { inputs = { "dist/" }, outFile = "release.zip" }
// Output: Ok(), path = "release.zip", size = whatever the finished archive
//         weighs, and every file under dist/ stored as "dist/<relative>".
//
// Input:  { inputs = { "missing/" }, outFile = "release.zip" }
// Output: no path and no size, error naming the input, and no file at
//         "release.zip".
//
// Input:  { inputs = { "dist/" }, outFile = "release.zip" } when release.zip
//         is already there
// Output: error naming it, note saying overwrite is off, and the old archive
//         untouched.
PackResult CreatePack(const PackOptions& options);

} // namespace guchho::pack
