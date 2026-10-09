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
// What this file does not do is turn the entries into bytes. That is the one
// question here with more than one answer, so it is the one question this
// file hands over: the list of entries, the options and a temporary file go
// to a writer, and the archive comes back. Everything on this side of that
// line — which formats exist, which level a format takes, where the archive
// lands, what happens to a half-written one — is the same for every answer,
// and answering it once is what stops three formats from becoming three
// opinions about the same request.
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
#include "guchho/logger.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace guchho::pack {

// What crosses into this file from the writers' side. They are brought in by
// name rather than by `using namespace detail` because the rest of this file
// is not detail: the validation below asks what a mode is, what year a
// timestamp falls in, and what an entry looks like, and should not inherit
// every other name in the writers' section along the way.
using detail::Entry;
using detail::kMaxDosYear;
using detail::kMinDosYear;
using detail::kPermissionMask;
using detail::LocalYear;
using detail::Quoted;

namespace {

namespace fs = std::filesystem;

// How deep a single input tree may be before the walk gives up. A real
// filesystem cannot get anywhere near this without exceeding its own path
// limit first; it exists because a directory that resolves back into itself
// through a Windows junction is a tree with no bottom, and the cycle check
// below depends on being able to resolve a path, which is exactly the thing
// that can fail.
constexpr int kMaxDepth = 1024;

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
        error = logger::FormatMsg(logger::MsgCat::kPack_EmptyInputPath);
        note  = logger::FormatMsg(logger::MsgCat::kPack_EmptyInputPathNote);
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
        error = logger::FormatMsg(logger::MsgCat::kPack_InputCannotBeArchiveRoot, input);
        note  = logger::FormatMsg(logger::MsgCat::kPack_DotDotRootNote);
        return false;
    }

    // A colon survives the split above when the input is drive-relative
    // — "C:build" — and would be written into the archive as part of a
    // name. A reader on another machine sees a file called "C:build" and
    // has no idea what to do with it; miniz's own name validation does not
    // catch this, because it only rejects a leading slash.
    if (name.find(':') != std::string::npos) {
        error = logger::FormatMsg(logger::MsgCat::kPack_UnusableArchiveName, input);
        note  = logger::FormatMsg(logger::MsgCat::kPack_DriveRelativeNote, name);
        return false;
    }

    root = name;
    return true;
}

// ---------------------------------------------------------------------------
// One archive's worth of state
// ---------------------------------------------------------------------------

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
            c.warnings.push_back(
                logger::FormatMsg(logger::MsgCat::kPack_SkippedArchiveBeingWritten, name));
            return true;
        }
    }

    std::string key = name;
    if (!key.empty() && key.back() == '/') key.pop_back();

    if (!c.roots_taken.insert(key).second) {
        c.error = logger::FormatMsg(logger::MsgCat::kPack_SameArchivePath, name);
        c.note  = logger::FormatMsg(logger::MsgCat::kPack_SameArchivePathNote);
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

    c.error = logger::FormatMsg(logger::MsgCat::kPack_InputNotRegularFileOrDirectory,
                                input);
    c.note  = logger::FormatMsg(logger::MsgCat::kPack_RegularFilesOnlyNote);
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
        c.error = logger::FormatMsg(
            logger::MsgCat::kPack_DirectoryTooDeep,
            prefix.empty() ? filesystem::PathToUTF8(dir) : prefix, kMaxDepth);
        c.note = logger::FormatMsg(logger::MsgCat::kPack_DirectoryTooDeepNote);
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
        c.warnings.push_back(
            logger::FormatMsg(logger::MsgCat::kPack_SkippedAlreadyBeingArchived,
                              prefix.empty() ? filesystem::PathToUTF8(dir) : prefix));
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
        c.error = logger::FormatMsg(
            logger::MsgCat::kPack_CannotReadDirectory,
            prefix.empty() ? filesystem::PathToUTF8(dir) : prefix, ec.message());
        return false;
    }

    std::vector<fs::directory_entry> children;
    const fs::directory_iterator    end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        children.push_back(*it);
    }
    if (ec) {
        c.error = logger::FormatMsg(
            logger::MsgCat::kPack_CannotReadDirectory,
            prefix.empty() ? filesystem::PathToUTF8(dir) : prefix, ec.message());
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
            c.warnings.push_back(logger::FormatMsg(logger::MsgCat::kPack_SkippedUnreadable,
                                                   name, ec.message()));
            ec.clear();
            continue;
        }
        if (fs::is_symlink(link)) {
            c.warnings.push_back(
                logger::FormatMsg(logger::MsgCat::kPack_SkippedSymbolicLink, name));
            continue;
        }

        // Cached by the iterator, so this is not a second system call: the
        // type the walk needs and the permissions the entry header needs come
        // from one reading of the same thing.
        const fs::file_status status = child.status(ec);
        if (ec) {
            c.warnings.push_back(logger::FormatMsg(logger::MsgCat::kPack_SkippedUnreadable,
                                                   name, ec.message()));
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

        c.warnings.push_back(
            logger::FormatMsg(logger::MsgCat::kPack_SkippedNotRegularFile, name));
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
            c.error = logger::FormatMsg(logger::MsgCat::kPack_InputDoesNotExist, input);
            c.note  = logger::FormatMsg(logger::MsgCat::kPack_InputDoesNotExistNote);
            return false;
        }
        if (ec) {
            c.error = logger::FormatMsg(logger::MsgCat::kPack_CannotReadInput, input,
                                        ec.message());
            return false;
        }

        // The one place a link is followed. Naming a link on the command line
        // is naming the thing it points at, so the root is the link's own name
        // — which is the name the person typed — and the contents are the
        // target's.
        const fs::file_status status =
            fs::is_symlink(link) ? fs::status(path, ec) : link;
        if (ec || status.type() == fs::file_type::not_found) {
            c.error = logger::FormatMsg(logger::MsgCat::kPack_SymlinkDoesNotResolve,
                                        input);
            return false;
        }
        if (ec) {
            c.error = logger::FormatMsg(logger::MsgCat::kPack_CannotReadInput, input,
                                        ec.message());
            return false;
        }

        if (c.out_exists && !fs::is_directory(status) &&
            SameFile(path, c.out_file, true)) {
            c.error = logger::FormatMsg(logger::MsgCat::kPack_OutputFileAlsoInput, input);
            c.note  = logger::FormatMsg(logger::MsgCat::kPack_OutputFileAlsoInputNote);
            return false;
        }

        if (!AddInput(c, path, status, root, input)) return false;
    }
    return true;
}

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
// The formats
// =========================================================================

std::string SupportedFormatsNote()
{
    std::string list;
    for (size_t i = 0; i < kFormats.size(); ++i) {
        if (i != 0) list += (i + 1 == kFormats.size()) ? " and " : ", ";
        list += Quoted(std::string(kFormats[i]));
    }
    return logger::FormatMsg(logger::MsgCat::kPack_OnlySupportedFormats, list);
}

// =========================================================================
// Generation
// =========================================================================

std::string DefaultOutFile(const std::string& input, const std::string& format)
{
    std::string name = input;
    while (name.size() > 1 && (name.back() == '/' || name.back() == '\\')) {
        name.pop_back();
    }

    // The format's name is its extension: zip writes ".zip", tar ".tar",
    // tar.gz ".tar.gz".
    const std::string extension = "." + format;

    // Only the extension is replaced, and only when the last component has
    // one that is not its first character. A name beginning with a dot is a
    // hidden file, not an extension: ".bashrc" has no extension to replace,
    // and turning it into ".zip" would produce an archive named after the
    // wrong thing.
    const size_t separator = name.find_last_of("/\\");
    const size_t first     = (separator == std::string::npos) ? 0 : separator + 1;
    const size_t dot       = name.find_last_of('.');
    if (dot != std::string::npos && dot > first) {
        // Already written for this format, so it is left alone. The test is
        // on the whole name rather than on the extension about to be erased,
        // because those are two different questions when the extension is two
        // components long: "release.tar.gz" ends in ".gz", and erasing that
        // would leave "release.tar" for ".tar.gz" to be added to again.
        if (name.ends_with(extension)) return name;
        name.erase(dot);
    }

    name += extension;
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
        result.error = logger::FormatMsg(logger::MsgCat::kPack_UnsupportedFormat, format);
        result.note  = SupportedFormatsNote();
        return result;
    }

    if (options.level && !LevelAppliesTo(format)) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_LevelDoesNotApplyToFormat,
                                         *options.level, format);
        result.note = logger::FormatMsg(logger::MsgCat::kPack_LevelDoesNotApplyNote);
        return result;
    }

    if (options.inputs.empty()) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_NoInputFiles);
        result.note  = logger::FormatMsg(logger::MsgCat::kPack_NoInputFilesNote);
        return result;
    }

    if (options.outFile.empty()) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_NoOutputFile);
        result.note  = logger::FormatMsg(logger::MsgCat::kPack_NoOutputFileNote);
        return result;
    }

    if (options.level && (*options.level < 0 || *options.level > 9)) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_LevelOutOfRange,
                                         *options.level);
        result.note = logger::FormatMsg(logger::MsgCat::kPack_LevelOutOfRangeNote);
        return result;
    }

    if (options.mode && (*options.mode & ~kPermissionMask) != 0) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_NotAPermissionMask,
                                         *options.mode);
        result.note  = logger::FormatMsg(logger::MsgCat::kPack_NotAPermissionMaskNote);
        return result;
    }

    if (options.date) {
        const std::time_t t = static_cast<std::time_t>(*options.date);
        if (static_cast<std::int64_t>(t) != *options.date) {
            result.error = logger::FormatMsg(logger::MsgCat::kPack_DateOutOfRangePlatform);
            return result;
        }

        // Only ZIP has a floor. The 1980-to-2107 window is a fact about the
        // MS-DOS field zip stores its time in, and asking tar to be outside
        // it would be a limit travelling into a format that does not have
        // one. Before the Unix epoch is still refused, and refused for
        // everything: there is no date there for any of these formats to
        // hold.
        if (format == kDefaultFormat) {
            int year = 0;
            if (!LocalYear(t, year)) {
                result.error =
                    logger::FormatMsg(logger::MsgCat::kPack_DateNotLocalTimestamp);
                return result;
            }
            if (year < kMinDosYear || year > kMaxDosYear) {
                result.error = logger::FormatMsg(
                    logger::MsgCat::kPack_DateOutsideZipRange, kMinDosYear, kMaxDosYear);
                result.note = logger::FormatMsg(logger::MsgCat::kPack_DateOutsideZipRangeNote);
                return result;
            }
        } else if (t < 0) {
            result.error = logger::FormatMsg(logger::MsgCat::kPack_DateBeforeUnixEpoch);
            result.note  = logger::FormatMsg(logger::MsgCat::kPack_DateBeforeUnixEpochNote);
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
        result.error =
            logger::FormatMsg(logger::MsgCat::kPack_OutputPathIsDirectory, options.outFile);
        return result;
    }

    const bool out_exists = fs::exists(out, ec);
    if (out_exists && !options.overwrite) {
        result.error =
            logger::FormatMsg(logger::MsgCat::kPack_OutputFileAlreadyExists, options.outFile);
        result.note  = logger::FormatMsg(logger::MsgCat::kPack_EnableOverwriteNote);
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
            result.error = logger::FormatMsg(
                logger::MsgCat::kPack_CannotCreateOutputDirectory,
                filesystem::PathToUTF8(parent), ec.message());
            return result;
        }
    }

    // -------------------------------------------------------------------------
    // 4. Write it somewhere safe, then move it into place
    // -------------------------------------------------------------------------

    const fs::path temp = ChooseTempFile(out);
    if (temp.empty()) {
        result.error = logger::FormatMsg(logger::MsgCat::kPack_NoFreeTempName,
                                         options.outFile);
        return result;
    }
    TempGuard temp_guard{temp};

    // Which bytes these entries become is the one question this file does
    // not answer. It hands the list to the format that was asked for and
    // reports whatever comes back; the temporary file, the guard and the
    // rename below are the same for every answer, which is the reason they
    // are here rather than in the writers.
    const bool wrote = format == kDefaultFormat
                           ? detail::WriteZip(c.entries, options, temp, result)
                           : detail::WriteTar(c.entries, options, temp, result);
    if (!wrote) return result;

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
        result.error = logger::FormatMsg(logger::MsgCat::kPack_CannotMoveFinishedArchive,
                                         options.outFile, move_error.message());
        return result;
    }
    temp_guard.committed = true;

    result.path = options.outFile;
    const auto bytes = fs::file_size(out, ec);
    result.size = ec ? 0 : static_cast<std::uint64_t>(bytes);
    return result;
}

} // namespace guchho::pack
