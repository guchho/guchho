// =============================================================================
// include/guchho/zip.hpp — turning paths on disk into a zip archive
// =============================================================================
//
// This header is the boundary between "somebody asked for a zip file" and the
// bytes of one. Everything above it — reading a command line, answering a
// service request, validating the options a JavaScript caller passed — speaks
// in terms of the two structures below. Everything below is a function of
// those two values plus the state of the disk, and it knows nothing about a
// command line, a pipe or a module.
//
// That split exists for two reasons. The first is that the interesting half of
// a zip operation is the half that decides what goes into the archive: which
// entries, under which names, with which metadata, and what is deliberately
// left out. A command that both parses a flag and walks a tree can only be
// tested by running the command; a function that takes a list of paths and
// hands back a result can be tested by comparing the archive it produced
// against the list it was given.
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
// =============================================================================

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace guchho::zip {

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
struct ZipOptions {
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
    // Links found *inside* the walk are never followed; see CreateZip().
    std::vector<std::string> inputs;

    // Where the archive is written, as a UTF-8 path. Its parent directory is
    // created if it does not exist. Required.
    std::string outFile;

    // Compression level, 0 through 9. 0 stores every entry uncompressed;
    // 9 compresses hardest. An empty value means the library's own default,
    // which is 6. Anything outside 0..9 is rejected rather than clamped, so
    // a typo cannot quietly turn into a different level.
    std::optional<int> level;

    // Whether outFile may be replaced when it is already there. Off by
    // default: a command that silently overwrote somebody's release.zip
    // would be a command people stopped running twice.
    bool overwrite = false;

    // The modification time written into every entry's header, as seconds
    // since the Unix epoch. An empty value means each entry keeps the time
    // it has on disk.
    //
    // ZIP stores this in MS-DOS form: a local timestamp with two-second
    // precision, covering 1980 through 2107 and nothing else. The value is
    // interpreted in the machine's local time zone, which is how miniz
    // converts it and how every ZIP reader converts it back. A value
    // outside that range is rejected rather than wrapped.
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
struct ZipResult {
    // The archive that was written, as the path it was asked for. Set only
    // on success: an operation that failed part way through has not produced
    // a file at that path, and saying otherwise would be the misleading
    // success the plan forbids.
    std::string path;

    // Its size in bytes, read from disk after the archive was closed rather
    // than accumulated while it was written, so it counts the central
    // directory and the end-of-central-directory record too.
    std::uint64_t size = 0;

    // What was left out on purpose, in the order it was decided. A symbolic
    // link that was not followed, a file type with no representation in a
    // zip archive, the output file sitting inside its own input tree. None
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

// The output file a single input would get if nobody named one.
//
// The rule is one line long and never guesses: drop any trailing separators,
// replace the last component's extension with ".zip", or append ".zip" when
// it has none. It is deliberately the extension that is replaced rather than
// the whole name, so "dist" and "dist/" both become "dist.zip" and a file
// called "report.json" becomes "report.zip" instead of "report.json.zip".
// A name with no extension — a directory called "dist", a file called
// "Makefile" — keeps its whole name and gains ".zip".
//
// A hidden file keeps its dot: ".bashrc" has an extension only by accident
// of the rule above, and turning it into ".zip" would be nonsense.
//
// Input:  DefaultOutFile("dist/")     -> "dist.zip"
// Input:  DefaultOutFile("src/app.js") -> "src/app.zip"
// Input:  DefaultOutFile("archive.zip") -> "archive.zip", which is the input
//         itself; CreateZip refuses that combination rather than archiving a
//         file into itself.
// Input:  DefaultOutFile(".bashrc")    -> ".bashrc.zip"
std::string DefaultOutFile(const std::string& input);

// Writes the archive the request describes, or explains why it did not.
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
//     with a warning, so "guchho zip dist/ -o dist/out.zip --allow-overwrite"
//     does not archive the previous run's output into the next one.
//
//   * Empty directories are represented by a trailing-slash entry, because a
//     directory that held nothing would otherwise leave no trace in an
//     archive that is otherwise a faithful copy of the tree.
//
//   * Every other file type — a fifo, a socket, a device — is skipped with a
//     warning, since a zip archive has nowhere to put one.
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
ZipResult CreateZip(const ZipOptions& options);

} // namespace guchho::zip
