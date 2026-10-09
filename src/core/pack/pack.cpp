// =============================================================================
// src/core/pack/pack.cpp — turning paths on disk into an archive
// =============================================================================
//
// The whole of CreatePack() is one sequence in four steps: settle what was
// asked for, work out what goes in, write it somewhere safe, move it into
// place. Each step is a separate group of functions below rather than a
// paragraph of one long one, because the three failures a caller cares about
// happen at three different steps and a message is only useful if it names
// the step it came from — "output already exists" is a request that was
// refused before anything was read, "cannot read X" is a disk that said no
// halfway through, and the difference is what tells a person whether to
// change the command or check the permissions.
//
// Two decisions are worth stating before the code, because each one is a
// place where a shorter implementation existed and was not taken.
//
// The archive is written through miniz's streaming reader rather than by
// loading a file and handing it over. mz_zip_writer_add_file() would have
// done the whole job in one call, but it reads the entry's timestamp off the
// disk itself, and the npm API can ask for a different one. Splitting the two
// cases would have meant two code paths that have to agree about what an
// entry is; one path with the time decided in one place cannot disagree with
// itself.
//
// And every file is opened through std::filesystem::path rather than through
// the C library's fopen(). The difference only shows up in a project with a
// non-ASCII name in it: on Windows a narrow path is the active code page, so
// "café/dist" either opens the wrong file or opens nothing. PathFromUTF8()
// is the project's existing answer to that, and it is used everywhere here
// for the same reason it is used everywhere else.
//
// Nothing in this file prints. The warnings are returned, the message is
// returned, and whether a warning goes to stderr or into a JavaScript array
// is a question for whoever called.
// =============================================================================

#include "guchho/pack.hpp"

#include "guchho/filesystem.hpp"
#include "guchho/miniz.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace guchho::pack {

namespace {

namespace fs = std::filesystem;

// The file type bits the ZIP external attribute field carries in its upper
// half, as the same numbers the S_ macros spell on every POSIX system. They
// are written out rather than picked up from <sys/stat.h> because that header
// does not exist on Windows, and because a number that appears in an archive
// should not depend on which machine produced it.
constexpr std::uint32_t kRegularFileBits = 0100000u;
constexpr std::uint32_t kDirectoryBits   = 0040000u;

// What of a mode is a mode. setuid, setgid and sticky are the top three bits
// of the low twelve, so this keeps them and drops anything above.
constexpr std::uint32_t kPermissionMask = 07777u;

// The execute bits a directory needs before a reader can walk into it. Used
// as a floor rather than as a value: see DirectoryBits below.
constexpr std::uint32_t kExecuteBits = 0111u;

// The years a ZIP entry header can express. DOS timestamps start in 1980 and
// the year field is seven bits wide, so 2107 is not a limit somebody chose —
// it is what seven bits of "years since 1980" adds up to.
constexpr int kMinDosYear = 1980;
constexpr int kMaxDosYear = 2107;

// How deep a single input tree may be before the walk gives up. A real
// filesystem cannot get anywhere near this without exceeding its own path
// limit first; it exists because a directory that resolves back into itself
// through a Windows junction is a tree with no bottom, and the cycle check
// below depends on being able to resolve a path, which is exactly the thing
// that can fail.
constexpr int kMaxDepth = 1024;

std::string Quoted(const std::string& text)
{
    return "\"" + text + "\"";
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

// The calendar year "t" falls in, read in the machine's own time zone.
//
// The zone matters because that is how a ZIP entry is written and how one is
// read back: miniz converts with localtime() going out and mktime() coming
// in, so a timestamp only round trips if both ends agree about the offset,
// and a timestamp converted here in UTC would be an hour out on a machine
// that is not.
bool LocalYear(std::time_t t, int& year)
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
std::time_t ClampToDosRange(std::time_t t)
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
// already inside the range a ZIP entry can carry.
//
// std::filesystem hands back a time on its own clock, which is not the system
// clock and has no guaranteed epoch. The two are related by reading both now
// and taking the difference, which is the portable way to do it without
// reaching for a platform-specific conversion: the error is the gap between
// the two calls to now(), which is far below the two-second precision the
// archive is going to store anyway.
std::time_t DiskWriteTime(const fs::path& source)
{
    std::error_code ec;
    const fs::file_time_type written = fs::last_write_time(source, ec);
    if (ec) return ClampToDosRange(std::time(nullptr));

    const auto file_now = fs::file_time_type::clock::now();
    const auto sys_now  = std::chrono::system_clock::now();
    const auto adjusted = sys_now + std::chrono::duration_cast<
                                     std::chrono::system_clock::duration>(
                                         written - file_now);
    return ClampToDosRange(std::chrono::system_clock::to_time_t(adjusted));
}

// The timestamp one entry is written with: the one that was asked for when
// there was one, and the file's own otherwise.
MZ_TIME_T EntryTime(const fs::path& source, const PackOptions& options)
{
    if (options.date) return static_cast<MZ_TIME_T>(*options.date);
    return static_cast<MZ_TIME_T>(DiskWriteTime(source));
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// Whether two paths name the same thing.
//
// Compared as normalized absolute paths first, because that answers the case
// that actually happens — the same file named two ways from the same working
// directory — without touching the disk. fs::equivalent() answers what the
// first comparison cannot: a file reached through a different route, or a
// name spelled in the other case on a machine that does not care about case.
// It is only reached when the second path is known to exist, because asking
// it about one that does not is a failed system call per entry for an answer
// of "no".
bool PathsEqual(const fs::path& a, const fs::path& b)
{
#ifdef _WIN32
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
#else
    return a == b;
#endif
}

bool SameFile(const fs::path& a, const fs::path& b, bool b_exists)
{
    std::error_code ec;
    const fs::path left = fs::absolute(a, ec);
    if (ec) return false;
    const fs::path right = fs::absolute(b, ec);
    if (ec) return false;

    if (PathsEqual(left.lexically_normal(), right.lexically_normal())) return true;
    if (!b_exists) return false;

    std::error_code equivalent_error;
    return fs::equivalent(a, b, equivalent_error) && !equivalent_error;
}

// The archive root an input contributes: the name its contents are stored
// under, which is its own last path component.
//
// The name is taken from the string rather than from a resolved path on
// purpose. A person who typed "./dist" asked for the root "dist", and a
// person who typed "../../../dist" asked for the root "dist" too — what they
// wanted is not the absolute path they happen to be standing at the far end
// of, and storing that would put a piece of this machine's layout into an
// archive that is going to be unpacked somewhere else entirely.
//
// An empty result means "no root": the input names the current directory, or
// a filesystem root, and its contents belong at the top of the archive
// rather than underneath a name that would be this directory's name here and
// somebody else's there.
bool ArchiveRoot(const std::string& input, std::string& root, std::string& error,
                 std::string& note)
{
    root.clear();

    if (input.empty()) {
        error = "An input path is empty";
        note  = "Give a file or a directory to archive.";
        return false;
    }

    std::string trimmed = input;
    while (trimmed.size() > 1 && (trimmed.back() == '/' || trimmed.back() == '\\')) {
        trimmed.pop_back();
    }

    const size_t separator = trimmed.find_last_of("/\\");
    const std::string name =
        (separator == std::string::npos) ? trimmed : trimmed.substr(separator + 1);

    // "", "." and a path that was nothing but separators all mean the same
    // thing: the tree this command is standing in, stored from its top.
    if (name.empty() || name == ".") return true;

    // ".." has no name to store under. Every other input with a ".." in it
    // — "../../project" — takes its root from the component after the last
    // separator and never sees the ".." at all; this is the one spelling
    // where there is no component after it, and guessing a name from a
    // resolved path would archive a directory under a name the person never
    // typed.
    if (name == "..") {
        error = std::format("Input {} cannot be an archive root", Quoted(input));
        note  = "A \"..\" has no name of its own to store contents under; "
                "give the directory's own name instead.";
        return false;
    }

    // A colon survives the split above when the input is drive-relative
    // — "C:build" — and would be written into the archive as part of a
    // name. A reader on another machine sees a file called "C:build" and
    // has no idea what to do with it; miniz's own name validation does not
    // catch this, because it only rejects a leading slash.
    if (name.find(':') != std::string::npos) {
        error = std::format("Input {} does not have a usable archive name", Quoted(input));
        note  = std::format("{} is a drive-relative path; give the directory's "
                            "full path instead.",
                            Quoted(name));
        return false;
    }

    root = name;
    return true;
}

// ---------------------------------------------------------------------------
// One archive's worth of state
// ---------------------------------------------------------------------------

// One thing that goes into the archive.
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

// Everything one call to CreatePack() accumulates, before anything is written.
struct Collector {
    // The archive being written, so a tree that contains it does not archive
    // its own output. Whether it is there yet is a separate question: on a
    // first run there is nothing to exclude.
    fs::path out_file;
    bool     out_exists = false;

    std::vector<Entry>      entries;
    std::vector<std::string> warnings;

    // Archive roots already taken, with any trailing "/" removed, so that two
    // inputs that would produce the same path are reported rather than
    // silently resolved by whichever the central directory listed last.
    std::unordered_set<std::string> roots_taken;

    // The canonical path of every directory currently being walked, so a tree
    // that points back into itself stops instead of recursing forever.
    std::unordered_set<std::string> ancestors;

    std::string error;
    std::string note;

    bool Failed() const { return !error.empty(); }
};

// Records one entry under "name", or explains why it cannot be recorded.
//
// The output file is checked first, and the duplicate check second. That
// order matters for the one case where they overlap: an output that appears
// twice in a walk is excluded twice, which is correct — it is not in the
// archive, so there is nothing for a reader to find twice.
bool AddEntry(Collector& c, std::string name, const fs::path& source,
              const fs::file_status& status)
{
    if (!fs::is_directory(status)) {
        if (c.out_exists && SameFile(source, c.out_file, true)) {
            c.warnings.push_back(std::format(
                "Skipped {}, which is the archive being written", Quoted(name)));
            return true;
        }
    }

    std::string key = name;
    if (!key.empty() && key.back() == '/') key.pop_back();

    if (!c.roots_taken.insert(key).second) {
        c.error = std::format("Two inputs would produce the same archive path: {}",
                              Quoted(name));
        c.note  = "Keep the inputs to distinct trees, or archive them separately.";
        return false;
    }

    c.entries.push_back(Entry{name, source, status});
    return true;
}

bool WalkDirectory(Collector& c, const fs::path& dir, const std::string& prefix,
                   int depth);

// Adds one input — a file, a directory, or a link to either — under the root
// ArchiveRoot() worked out for it.
bool AddInput(Collector& c, const fs::path& path, const fs::file_status& status,
              const std::string& root, const std::string& input)
{
    if (fs::is_regular_file(status)) {
        return AddEntry(c, root, path, status);
    }

    if (fs::is_directory(status)) {
        const std::string prefix = root.empty() ? std::string() : root + "/";
        if (!prefix.empty() && !AddEntry(c, prefix, path, status)) return false;
        return WalkDirectory(c, path, prefix, 0);
    }

    c.error = std::format("Input is not a regular file or directory: {}", Quoted(input));
    c.note  = "A zip archive holds regular files and directories.";
    return false;
}

// Walks one directory, adding every regular file and directory beneath it.
//
// Symbolic links are never followed here. Following one is how an archive
// silently grows a copy of somewhere else entirely — a link to the home
// directory inside a build output would archive the home directory — and
// there is no depth at which that stops being a surprise. They are reported
// instead, because an archive that quietly omits something a person expected
// to be in it is worse than one that says what it left out.
//
// The children are sorted first. A directory iteration order is whatever the
// filesystem felt like, which makes the archive's byte order — and therefore
// the archive itself — differ between two runs over an identical tree.
bool WalkDirectory(Collector& c, const fs::path& dir, const std::string& prefix,
                   int depth)
{
    if (depth > kMaxDepth) {
        c.error = std::format("Directory nesting under {} is more than {} levels deep",
                              Quoted(prefix.empty() ? filesystem::PathToUTF8(dir) : prefix),
                              kMaxDepth);
        c.note = "A tree that deep usually means a directory that resolves back "
                 "into itself.";
        return false;
    }

    // Resolved before the contents are read, so that a directory reached by
    // two routes — through a junction on Windows, which is not a symbolic
    // link and is therefore not caught by the rule above — stops the second
    // time instead of walking into itself forever.
    std::error_code resolve_error;
    const fs::path resolved = fs::canonical(dir, resolve_error);
    const fs::path fallback = fs::absolute(dir, resolve_error);
    const std::string key =
        (resolve_error ? fallback : resolved).lexically_normal().generic_string();

    if (!c.ancestors.insert(key).second) {
        c.warnings.push_back(std::format(
            "Skipped {}: it resolves to a directory already being archived",
            Quoted(prefix.empty() ? filesystem::PathToUTF8(dir) : prefix)));
        return true;
    }

    // Erased on the way out of every path below, including the error ones:
    // an ancestor left in the set would make a later input that legitimately
    // passes through this directory look like a cycle.
    struct AncestorGuard {
        std::unordered_set<std::string>& keys;
        const std::string&               key;
        ~AncestorGuard() { keys.erase(key); }
    } guard{c.ancestors, key};

    std::error_code         ec;
    fs::directory_iterator  it(dir, ec);
    if (ec) {
        c.error = std::format("Cannot read directory {}: {}",
                              Quoted(prefix.empty() ? filesystem::PathToUTF8(dir) : prefix),
                              ec.message());
        return false;
    }

    std::vector<fs::directory_entry> children;
    const fs::directory_iterator    end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        children.push_back(*it);
    }
    if (ec) {
        c.error = std::format("Cannot read directory {}: {}",
                              Quoted(prefix.empty() ? filesystem::PathToUTF8(dir) : prefix),
                              ec.message());
        return false;
    }

    std::sort(children.begin(), children.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  return a.path().filename() < b.path().filename();
              });

    for (const fs::directory_entry& child : children) {
        const std::string name = prefix + filesystem::PathToUTF8(child.path().filename());

        const fs::file_status link = child.symlink_status(ec);
        if (ec) {
            c.warnings.push_back(
                std::format("Skipped {}, which cannot be read: {}", Quoted(name),
                            ec.message()));
            ec.clear();
            continue;
        }
        if (fs::is_symlink(link)) {
            c.warnings.push_back(
                std::format("Skipped symbolic link {}", Quoted(name)));
            continue;
        }

        // Cached by the iterator, so this is not a second system call: the
        // type the walk needs and the permissions the entry header needs come
        // from one reading of the same thing.
        const fs::file_status status = child.status(ec);
        if (ec) {
            c.warnings.push_back(
                std::format("Skipped {}, which cannot be read: {}", Quoted(name),
                            ec.message()));
            ec.clear();
            continue;
        }

        if (fs::is_regular_file(status) || fs::is_directory(status)) {
            if (!AddEntry(c, fs::is_directory(status) ? name + "/" : name,
                          child.path(), status)) {
                return false;
            }
            if (fs::is_directory(status) &&
                !WalkDirectory(c, child.path(), name + "/", depth + 1)) {
                return false;
            }
            continue;
        }

        c.warnings.push_back(std::format(
            "Skipped {}, which is not a regular file", Quoted(name)));
    }

    return true;
}

// Collects every entry for the whole request, or reports the first thing that
// stops one being produced at all.
bool Collect(Collector& c, const PackOptions& options)
{
    for (const std::string& input : options.inputs) {
        std::string root;
        if (!ArchiveRoot(input, root, c.error, c.note)) return false;

        const fs::path         path = filesystem::PathFromUTF8(input);
        std::error_code        ec;
        const fs::file_status  link = fs::symlink_status(path, ec);

        if (link.type() == fs::file_type::not_found) {
            c.error = std::format("Input does not exist: {}", Quoted(input));
            c.note  = "Check the spelling, and that the path is relative to the "
                      "directory this command is running in.";
            return false;
        }
        if (ec) {
            c.error = std::format("Cannot read input {}: {}", Quoted(input), ec.message());
            return false;
        }

        // The one place a link is followed. Naming a link on the command line
        // is naming the thing it points at, so the root is the link's own name
        // — which is the name the person typed — and the contents are the
        // target's.
        const fs::file_status status =
            fs::is_symlink(link) ? fs::status(path, ec) : link;
        if (ec || status.type() == fs::file_type::not_found) {
            c.error = std::format("Input is a symbolic link that does not resolve: {}",
                                  Quoted(input));
            return false;
        }
        if (ec) {
            c.error = std::format("Cannot read input {}: {}", Quoted(input), ec.message());
            return false;
        }

        if (c.out_exists && !fs::is_directory(status) &&
            SameFile(path, c.out_file, true)) {
            c.error = std::format("The output file is also an input: {}", Quoted(input));
            c.note  = "An archive cannot contain itself; choose a different output.";
            return false;
        }

        if (!AddInput(c, path, status, root, input)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

// The permission bits to record for a file or a directory.
//
// A directory is never given a value that would make it unenterable. The fix
// is a floor rather than an assignment — OR the execute bits in only when
// none of them are there — because a tree that is 0700 is a deliberate
// choice, and turning it into 0711 to satisfy a rule would be the archive
// disagreeing with the disk about who may walk where.
std::uint32_t PermissionBits(const Entry& entry, const PackOptions& options)
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

// ---------------------------------------------------------------------------
// Reading an entry
// ---------------------------------------------------------------------------

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
            error = std::format("Cannot open {} for reading",
                                Quoted(filesystem::PathToUTF8(source)));
            return false;
        }

        stream.seekg(0, std::ios::end);
        const std::streamoff end = stream.tellg();
        if (end < 0) {
            error = std::format("Cannot measure {}",
                                Quoted(filesystem::PathToUTF8(source)));
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
    return message ? message : "unknown zip error";
}

// Closes a writer on every way out of the writing step, including the ones
// that leave through a return in the middle. An unfinalized archive left open
// holds a file handle and a heap block, and neither is released by the
// temporary-file guard below because that one only knows about the path.
struct WriterGuard {
    mz_zip_archive* archive = nullptr;
    bool            active  = false;

    ~WriterGuard()
    {
        if (active) mz_zip_writer_end(archive);
    }
};

// Removes a half-written archive on every way out of the writing step, unless
// it has been moved into place.
struct TempGuard {
    fs::path path;
    bool     committed = false;

    ~TempGuard()
    {
        if (committed) return;
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// The temporary file to write into: next to the destination, so that the move
// into place at the end is a rename within one directory, which no platform
// this builds on implements by copying.
//
// A name that is already taken is not reused. It is almost always a temporary
// file a previous run left behind when it was killed, and writing over it
// would be right — but "almost always" is not a guarantee, and the price of
// being sure is a counter rather than a guess.
fs::path ChooseTempFile(const fs::path& out)
{
    std::error_code ec;
    for (int attempt = 0; attempt < 1000; attempt++) {
        fs::path candidate = out;
        candidate += attempt == 0 ? std::string(".tmp")
                                  : std::format(".tmp.{}", attempt);
        if (!fs::exists(candidate, ec)) return candidate;
        ec.clear();
    }
    return fs::path();
}

} // namespace

// =========================================================================
// Generation
// =========================================================================

std::string DefaultOutFile(const std::string& input)
{
    std::string name = input;
    while (name.size() > 1 && (name.back() == '/' || name.back() == '\\')) {
        name.pop_back();
    }

    // Only the extension is replaced, and only when the last component has
    // one that is not its first character. A name beginning with a dot is a
    // hidden file, not an extension: ".bashrc" has no extension to replace,
    // and turning it into ".zip" would produce an archive named after the
    // wrong thing.
    const size_t separator = name.find_last_of("/\\");
    const size_t first     = (separator == std::string::npos) ? 0 : separator + 1;
    const size_t dot       = name.find_last_of('.');
    if (dot != std::string::npos && dot > first) name.erase(dot);

    name += ".zip";
    return name;
}

PackResult CreatePack(const PackOptions& options)
{
    PackResult result;

    // -------------------------------------------------------------------------
    // 1. What was asked for
    // -------------------------------------------------------------------------
    //
    // Everything here is settled before the disk is touched, so a request that
    // could never have worked leaves nothing behind — no directories created,
    // no temporary file, no partial archive.

    const std::string format =
        options.format.empty() ? std::string(kDefaultFormat) : options.format;
    if (!IsSupportedFormat(format)) {
        result.error = std::format("Unsupported archive format: {}", Quoted(format));
        result.note =
            std::format("Only \"{}\" is supported today.", kDefaultFormat);
        return result;
    }

    if (options.inputs.empty()) {
        result.error = "No input files were given";
        result.note  = "Name at least one file or directory to archive.";
        return result;
    }

    if (options.outFile.empty()) {
        result.error = "No output file was given";
        result.note  = "Name the archive to write, with -o/--outfile or outFile.";
        return result;
    }

    if (options.level && (*options.level < 0 || *options.level > 9)) {
        result.error =
            std::format("Compression level {} is out of range", *options.level);
        result.note = "The level runs from 0 (store, do not compress) to 9.";
        return result;
    }

    if (options.mode && (*options.mode & ~kPermissionMask) != 0) {
        result.error = std::format("mode 0{:o} is not a permission mask", *options.mode);
        result.note  = "The value is Unix permission bits, from 0 to 07777.";
        return result;
    }

    if (options.date) {
        const std::time_t t = static_cast<std::time_t>(*options.date);
        int               year = 0;
        if (static_cast<std::int64_t>(t) != *options.date) {
            result.error = "date is outside the range this platform can represent";
            return result;
        }
        if (!LocalYear(t, year)) {
            result.error = "date cannot be read as a local timestamp";
            return result;
        }
        if (year < kMinDosYear || year > kMaxDosYear) {
            result.error = std::format(
                "date is outside the range a ZIP entry can hold ({} through {})",
                kMinDosYear, kMaxDosYear);
            result.note = "The value is seconds since the Unix epoch, and ZIP "
                          "timestamps start in 1980.";
            return result;
        }
    }

    // -------------------------------------------------------------------------
    // 2. Where it goes
    // -------------------------------------------------------------------------

    const fs::path        out = filesystem::PathFromUTF8(options.outFile);
    std::error_code       ec;
    const fs::file_status out_status = fs::symlink_status(out, ec);
    if (fs::is_directory(out_status)) {
        result.error = std::format("Output path is a directory: {}", Quoted(options.outFile));
        return result;
    }

    const bool out_exists = fs::exists(out, ec);
    if (out_exists && !options.overwrite) {
        result.error = std::format("Output file already exists: {}",
                                   Quoted(options.outFile));
        result.note  = "Enable overwrite (--allow-overwrite, or overwrite: true) "
                       "to replace it.";
        return result;
    }

    // -------------------------------------------------------------------------
    // 3. What goes in
    // -------------------------------------------------------------------------

    Collector c;
    c.out_file   = out;
    c.out_exists = out_exists;

    if (!Collect(c, options)) {
        result.error = std::move(c.error);
        result.note  = std::move(c.note);
        result.warnings = std::move(c.warnings);
        return result;
    }
    result.warnings = std::move(c.warnings);

    // The output directory is created only now, after every check that could
    // refuse the request has passed. A command that made a directory and then
    // failed would have left something behind for a failure that had nothing
    // to do with writing.
    const fs::path parent = out.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            result.error = std::format("Cannot create the output directory {}: {}",
                                       Quoted(filesystem::PathToUTF8(parent)),
                                       ec.message());
            return result;
        }
    }

    // -------------------------------------------------------------------------
    // 4. Write it somewhere safe, then move it into place
    // -------------------------------------------------------------------------

    const fs::path temp = ChooseTempFile(out);
    if (temp.empty()) {
        result.error = std::format("Cannot find a free temporary name next to {}",
                                   Quoted(options.outFile));
        return result;
    }
    TempGuard temp_guard{temp};

    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!mz_zip_writer_init_file(&archive, filesystem::PathToUTF8(temp).c_str(), 0)) {
        result.error = std::format("Cannot start writing {}: {}",
                                   Quoted(options.outFile), MinizError(&archive));
        return result;
    }
    WriterGuard writer_guard{&archive, true};

    const mz_uint level =
        options.level ? static_cast<mz_uint>(*options.level)
                      : static_cast<mz_uint>(MZ_DEFAULT_LEVEL);

    for (const Entry& entry : c.entries) {
        if (!mz_zip_writer_set_default_attributes(&archive,
                                                   ExternalAttributes(entry, options))) {
            result.error = std::format("Cannot set the metadata for {}: {}",
                                       Quoted(entry.name), MinizError(&archive));
            return result;
        }

        MZ_TIME_T when = EntryTime(entry.source, options);

        if (fs::is_directory(entry.status)) {
            // An empty buffer and a name ending in "/" is how miniz spells a
            // directory: it sets the DOS directory bit itself, and refuses
            // anything but empty contents, which is exactly the constraint.
            if (!mz_zip_writer_add_mem_ex_v2(&archive, entry.name.c_str(), "", 0,
                                             nullptr, 0, level, 0, 0, &when, nullptr, 0,
                                             nullptr, 0)) {
                result.error = std::format("Cannot add {}: {}", Quoted(entry.name),
                                           MinizError(&archive));
                return result;
            }
            continue;
        }

        FileSource source;
        if (!source.Open(entry.source, result.error)) return result;

        if (!mz_zip_writer_add_read_buf_callback(&archive, entry.name.c_str(),
                                                 &ReadEntry, &source, source.size,
                                                 &when, nullptr, 0, level, nullptr, 0,
                                                 nullptr, 0)) {
            result.error =
                std::format("Cannot add {}: {}", Quoted(entry.name), MinizError(&archive));
            return result;
        }
        if (source.failed) {
            result.error = std::format("Cannot read {} in full",
                                       Quoted(filesystem::PathToUTF8(entry.source)));
            return result;
        }
    }

    if (!mz_zip_writer_finalize_archive(&archive)) {
        result.error = std::format("Cannot finish {}: {}", Quoted(options.outFile),
                                   MinizError(&archive));
        return result;
    }

    // Closed before the rename, so the bytes are on disk before anything can
    // see the file under its final name. The guard is disarmed rather than
    // allowed to run: after this the archive owns the handle it has already
    // released, and ending it twice would be a double free.
    if (!mz_zip_writer_end(&archive)) {
        result.error = std::format("Cannot close {}: {}", Quoted(options.outFile),
                                   MinizError(&archive));
        return result;
    }
    writer_guard.active = false;

    std::error_code move_error;
    fs::rename(temp, out, move_error);
    if (move_error && options.overwrite) {
        // Windows and POSIX both replace an existing file in a rename, so
        // this is a belt-and-braces path for an implementation that does not.
        // Removing first is only safe when overwriting was asked for, which
        // is why the removal is inside the branch rather than unconditional.
        std::error_code ignored;
        fs::remove(out, ignored);
        move_error.clear();
        fs::rename(temp, out, move_error);
    }
    if (move_error) {
        result.error = std::format("Cannot move the finished archive to {}: {}",
                                   Quoted(options.outFile), move_error.message());
        return result;
    }
    temp_guard.committed = true;

    result.path = options.outFile;
    const auto bytes = fs::file_size(out, ec);
    result.size = ec ? 0 : static_cast<std::uint64_t>(bytes);
    return result;
}

} // namespace guchho::pack
