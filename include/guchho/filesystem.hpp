#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace guchho::filesystem {

    // File systems with coarse modification-time resolution (e.g. FAT32 with
    // 2-second granularity, or network drives that round to the nearest second)
    // can report the same mtime for two successive writes.  Guchho therefore
    // refuses to trust an mtime that is less than this many seconds old.  A
    // file whose mtime falls inside the safety window is treated as "unusable"
    // and the bundler falls back to a full re-read on the next build.
    //
    // Example:
    //   A file is written at t=10.0 s, stat'd at t=11.5 s.
    //   mtime_sec == 10, current time == 11.
    //   11 - 10 == 1 < kModKeySafetyGap(3)  →  unusable = true.
    constexpr int kModKeySafetyGap = 3; // In seconds

    enum class EntryKind : uint8_t {
        kInvalid = 0,
        kDir     = 1,
        kFile    = 2,
    };

    // Selects the path-convention rules that a mock file system should follow.
    // Unix uses '/' as separator and treats '/'-leading paths as absolute;
    // Windows uses '\\' and recognises drive letters and UNC prefixes.
    enum class MockKind : uint8_t {
        kUnix,
        kWindows,
    };

    struct Fs;


    // Entry / DirEntries

    // Recorded when a directory lookup resolves to a different case than what
    // was requested.  For example, on a case-insensitive file system a query
    // for "Readme.md" might match "README.md" — this struct captures both
    // sides so the caller can decide whether to warn or error.
    //
    // Example:
    //   dir  = "/project/src"
    //   query  = "readme.md"
    //   actual = "README.md"
    struct DifferentCase {
        std::string dir;
        std::string query;
        std::string actual;
    };

    // Observability bookkeeping for a directory listing.  The bundler's watch
    // mode needs to know exactly which parts of the file system a build read
    // so that it can invalidate only the affected outputs when a change occurs.
    //
    // Two distinct access patterns are tracked:
    //   1. Individual lookups — "was_present" records every entry that was
    //      tested and whether it existed.  A change is detected when a
    //      previously-present entry disappears (or vice-versa).
    //   2. Full listing — when "all_entries" is populated it means the caller
    //      iterated over the entire directory.  Any addition or removal of an
    //      entry, even one never individually queried, will invalidate the build.
    struct AccessedEntries {
        std::mutex mutex;

        // Maps entry name → was-present-at-lookup-time.  Entries only appear
        // here once they have been looked up at least once.
        std::unordered_map<std::string, bool> was_present;

        // Populated only when the caller requested the full sorted listing.
        // Contains the entry names as they were at the time of the listing.
        std::optional<std::vector<std::string>> all_entries;
    };

    // A cached record for a single directory entry.  The "kind" field is
    // resolved lazily on the first call to Kind() because a stat() call is
    // relatively expensive and many entries are never queried for their type.
    //
    // Thread safety: "mutex" protects concurrent first-access to Kind().
    struct Entry {
        EntryKind   Kind(Fs& fs);
        std::string Symlink(Fs& fs);

        std::string symlink;   // Resolved symlink target, empty if not a symlink
        std::string dir;       // Parent directory path
        std::string base;      // File or directory name
        std::mutex  mutex;     // Guards lazy kind resolution
        EntryKind   kind      = EntryKind::kInvalid;
        bool        need_stat = false;
    };

    // An immutable snapshot of a directory's contents.  The map is sorted by
    // entry name and is cached so that repeated reads within a single build
    // do not re-scan the directory.
    //
    // "data" is std::nullopt when the directory could not be read (e.g.
    // permission denied, path does not exist, or I/O error).
    //
    // Example:
    //   dir = "/project/src"
    //   data = { {"fs.cpp" → Entry}, {"fs.hpp" → Entry}, {"main.cpp" → Entry} }
    struct DirEntries {
        std::optional<std::map<std::string, std::shared_ptr<Entry>>> data;
        std::shared_ptr<AccessedEntries>                             accessed_entries;
        std::string                                                  dir;

        // Performs a case-sensitive (or case-insensitive, depending on the FS)
        // lookup of "query" within this directory.  Returns the matching entry
        // and, when the case differs from the stored name, a DifferentCase
        // describing the discrepancy.
        //
        // Example (case-insensitive FS):
        //   query = "readme.md"  →  entry for "README.md", DifferentCase{…}
        //   query = "fs.cpp"     →  entry for "fs.cpp", no DifferentCase.
        std::pair<std::shared_ptr<Entry>, std::optional<DifferentCase>> Get(const std::string& query);

        // Returns the number of entries in the directory without recording the
        // lookup in AccessedEntries.  Useful for diagnostics or formatting that
        // should not influence watch-mode invalidation.
        int PeekEntryCount() const;

        // Returns all entry names in sorted (lexicographic) order and marks the
        // full listing as observed so that watch mode can detect future
        // additions or removals.
        std::vector<std::string> SortedKeys();
    };

    // Constructs a DirEntries with an empty data map, suitable for representing
    // a directory that does not exist or could not be read.
    DirEntries MakeEmptyDirEntries(const std::string& dir);

    // Opened files

    // Abstract handle to a file opened for random access.  Concrete
    // implementations may hold the data entirely in memory or stream it from
    // the real file system.
    struct OpenedFile {
        virtual ~OpenedFile() = default;

        // Returns the total byte length of the file.
        virtual int         Len() const = 0;

        // Returns the byte range [start, end).  Both offsets are zero-based.
        // Requesting a range past the end of the file returns the bytes up to
        // EOF; requesting start >= Len() returns an empty string.
        //
        // Example:
        //   File contains "abcdefg" (7 bytes).
        //   Read(2, 5) → "cde"
        //   Read(5, 99) → "fg"
        //   Read(7, 10) → ""
        virtual std::string Read(int start, int end) = 0;

        // Releases any underlying resources.  Returns true on success.
        virtual bool        Close() = 0;
    };

    // A file whose contents are held entirely in memory.  This is used both
    // by the mock FS (for test data) and by the real FS when a file is small
    // enough to be slurped in a single read.
    struct InMemoryOpenedFile : OpenedFile {
        std::string contents;

        int         Len() const override;
        std::string Read(int start, int end) override;
        bool        Close() override;
    };


    // Results

    // A discriminated result type used by every FS operation.  On success
    // "value" holds the result and "canonical_error" is std::nullopt.  On
    // failure "value" is default-constructed, "canonical_error" holds a
    // platform-independent error code, and "original_error" preserves the
    // OS-specific message for diagnostics.
    //
    // Example:
    //   auto r = fs.ReadFile("/missing.txt");
    //   r.Ok()                    → false
    //   r.canonical_error         → std::errc::no_such_file_or_directory
    //   r.original_error          → "No such file or directory"
    template <typename T>
    struct FsResult {
        T                        value{};
        std::optional<std::errc> canonical_error;
        std::string              original_error; // Empty on success

        bool                   Ok() const { return !canonical_error.has_value(); }
        explicit operator bool() const { return Ok(); }
    };

    // A compact fingerprint of a file's metadata, used to decide quickly
    // whether the file has been modified since the last build.  The fields
    // are drawn from stat(2) and cover every axis that could change between
    // builds without the file's path changing.
    //
    // Example (Linux):
    //   inode=12345, size=1024, mtime_sec=1700000000, mtime_nsec=500000000,
    //   mode=0100644, uid=1000
    struct ModKey {
        uint64_t inode      = 0;
        int64_t  size       = 0;
        int64_t  mtime_sec  = 0;
        int64_t  mtime_nsec = 0;
        uint32_t mode       = 0;
        uint32_t uid        = 0;

        bool operator==(const ModKey&) const = default;
    };

    // The outcome of computing a ModKey.  On success "value" is filled and
    // Ok() returns true.  On failure the error fields explain why.  The
    // "unusable" flag is a third state: the file exists but its metadata is
    // unreliable (e.g. mtime is zero or falls inside the safety-gap window).
    // Callers must treat an unusable result as a forced re-read, not as a
    // "file missing" condition.
    //
    // Example:
    //   File exists, mtime_sec == current_time - 1, kModKeySafetyGap == 3.
    //   unusable = true, canonical_error = std::nullopt.
    struct ModKeyResult {
        ModKey                   value{};
        std::optional<std::errc> canonical_error;
        std::string              original_error; // Empty on success

        // Set when the file exists but its key cannot be trusted (a zero or too
        // fresh mtime). Callers must treat this differently from a missing file.
        // When set, "canonical_error" may be empty even though "Ok()" fails.
        bool unusable = false;

        bool                   Ok() const { return !canonical_error.has_value() && !unusable; }
        explicit operator bool() const { return Ok(); }
    };

    // Accumulated watch data for a build.  Each entry maps a file-system path
    // to a thunk that, when called, returns the path of the entry that was
    // actually modified (which may differ from the key when a directory
    // watcher fires for a child).  The bundler serialises these into the
    // incremental-build metadata after every successful build.
    struct WatchData {
        // Each function returns a non-empty path if the corresponding entry has
        // been modified. For a file the returned path is the file itself; for a
        // directory it is the directory or one of its changed children.
        std::unordered_map<std::string, std::function<std::string()>> paths;
    };


    // The FS interface

    // Virtual interface that decouples the bundler's core from the host
    // operating system.  Every file-system interaction — reading, writing,
    // stat'ing, path manipulation — goes through this surface.  This enables
    // two concrete backends:
    //   - RealFs: delegates to the OS.
    //   - MockFs: serves data from an in-memory map, allowing deterministic
    //     tests that never touch the real disk.
    //
    // A future overlay FS could intercept reads to transparently serve files
    // from a virtual layer (e.g. zip archives) without changing callers.
    struct Fs {
        virtual ~Fs() = default;

        // Returns a sorted snapshot of every entry in "path".  If the
        // directory does not exist or cannot be read, "data" in the result
        // is std::nullopt.
        virtual FsResult<DirEntries>                  ReadDirectory(const std::string& path) = 0;

        // Reads the entire contents of "path" into a string.  Fails with
        // std::errc::no_such_file_or_directory when the file does not exist.
        virtual FsResult<std::string>                 ReadFile(const std::string& path) = 0;

        // Opens "path" for random-access reads.  The returned handle can be
        // read in arbitrary byte ranges without loading the whole file.
        virtual FsResult<std::shared_ptr<OpenedFile>> OpenFile(const std::string& path) = 0;

        // Computes a metadata fingerprint for "path".  A change in the
        // returned key between two builds indicates the file was modified.
        //
        // Not guaranteed to detect every modification — for example, an
        // editor that preserves inode, size, and mtime will not be caught.
        virtual ModKeyResult ModKey(const std::string& path) = 0;

        // Path operations are virtualised so the mock can enforce platform-
        // specific rules (e.g. Windows drive letters) on any host OS.
        virtual bool                       IsAbs(std::string_view path) = 0;
        virtual std::optional<std::string> Abs(std::string_view path) = 0;
        virtual std::string                Dir(std::string_view path) = 0;
        virtual std::string                Base(std::string_view path) = 0;
        virtual std::string                Ext(std::string_view path) = 0;
        virtual std::string                Join(std::initializer_list<std::string_view> parts) = 0;
        virtual std::string                Cwd() = 0;
        virtual std::optional<std::string> Rel(std::string_view base, std::string_view target) = 0;
        virtual std::optional<std::string> EvalSymlinks(std::string_view path) = 0;

        // Classifies the entry at (dir, base) and returns the normalised
        // directory and the entry's kind.  On a case-insensitive FS the
        // returned dir may differ from the input (e.g. "C:\Users" → "c:\\users").
        //
        // Example:
        //   dir="/project/src", base="fs.cpp" → ("/project/src", kFile)
        //   dir="/project", base="src"        → ("/project", kDir)
        virtual std::pair<std::string, EntryKind> Kind(std::string_view dir, std::string_view base) = 0;

        // Returns the set of all file paths read and directory paths scanned
        // during this build.  The bundler persists this data so the next
        // incremental build can check each entry for modifications.
        virtual WatchData GetWatchData() = 0;
    };


    // Real FS (fs_real.cpp)

    struct RealFsOptions {
        std::string abs_working_dir;
        bool        want_watch_data = false;
        bool        do_not_cache    = false;
    };

    // Returns true when the current process is running on Windows.  Used to
    // select path-convention rules at construction time.
    bool CheckIfWindows();

    // Creates a real file system backed by the host OS.  "options" must
    // provide an absolute working directory; passing a relative path causes
    // this function to return nullptr and set "error".
    //
    // When "want_watch_data" is true, every ReadDirectory and ReadFile call
    // records the path into WatchData so GetWatchData() can later report the
    // full dependency set.
    std::unique_ptr<Fs> MakeRealFS(const RealFsOptions& options, std::string& error);


    // Shared helpers

    // The bundler opens many files in parallel during bundle-graph
    // construction.  Some operating systems impose a per-process limit on
    // open file descriptors (e.g. 256 on older macOS, 1024 on many Linux
    // kernels).  These two functions implement a simple semaphore that
    // caps the number of concurrent opens.
    //
    // Call BeforeFileOpen() before opening a file; it blocks if the limit
    // has been reached.  Call AfterFileClose() once the file is closed to
    // release the slot.
    void BeforeFileOpen();
    void AfterFileClose();

    // Yarn PnP virtual paths look like:
    //   <prefix>/__virtual__/<hash>/<depth>/<suffix>
    //
    // where <depth> is a decimal count of how many ".." operations must be
    // applied to <prefix> to reach the real location.  For example:
    //   "/a/b/__virtual__/abc123/2/c/d.js"
    //   prefix = "/a/b"  (after applying 2× ".."  →  "/a")
    //   suffix = "c/d.js"
    //
    // ParseYarnPnPVirtualPath returns std::nullopt for paths that do not
    // match this pattern.  MangleYarnPnPVirtualPath rewrites such paths
    // into their resolved form; non-virtual paths are returned unchanged.
    struct YarnPnPVirtualPath {
        std::string prefix; // Resolved prefix after applying the ".." count
        std::string suffix; // Remaining path after the virtual segment (may be empty)
    };

    // Legacy constant retained for compatibility; new code should not rely on it.
    inline const std::string yarn_pnp_virtual_prefix = "__virtual__";

    // Parses a Yarn PnP virtual path; returns nullopt when the path is not of
    // that form.
    std::optional<YarnPnPVirtualPath> ParseYarnPnPVirtualPath(const std::string& path);

    // Rewrites a Yarn PnP virtual path into its resolved form.  Non-virtual
    // paths are returned unchanged.
    //
    // Example:
    //   "/a/b/__virtual__/abc123/2/c/d.js" → "/a/c/d.js"
    //   "/src/main.cpp"                    → "/src/main.cpp"  (unchanged)
    std::string MangleYarnPnPVirtualPath(const std::string& path);

    // Wraps an inner FS so that ".zip" files are treated as directories and
    // Yarn PnP virtual paths are transparently resolved through the overlay.
    // This enables imports like "import foo from 'pkg/dist/index.js'" to
    // read from inside a zip archive without the caller knowing.
    std::unique_ptr<Fs> MakeZipFS(std::unique_ptr<Fs> inner);

    // Removes trailing slashes from "path", then creates the directory and
    // any missing parents (like `mkdir -p`).  Returns true on success; on
    // failure sets "error" to a human-readable description and returns false.
    //
    // Example:
    //   MkdirAll(fs, "/tmp/build/output", error) → true, creates /tmp, /tmp/build, /tmp/build/output
    //   MkdirAll(fs, "/proc/cant_write", error)  → false, error = "Permission denied"
    bool MkdirAll(Fs& fs, const std::string& path, std::string& error);

    // Converts between Guchho's canonical UTF-8 path representation and the
    // native std::filesystem::path type.  On Windows the native path uses
    // wide characters; on Unix the two are interchangeable.
    std::filesystem::path PathFromUTF8(std::string_view utf8);
    std::string           PathToUTF8(const std::filesystem::path& path);

    // Maps an OS-reported error_code onto a platform-independent std::errc
    // value.  On Windows, certain confusing errors (ERROR_INVALID_NAME,
    // ERROR_NOT_A_DIRECTORY) are collapsed to ENOENT so that path resolution
    // can continue gracefully rather than aborting with a hard error.
    //
    // Example:
    //   Windows error_code 123 (ERROR_INVALID_NAME) → std::errc::no_such_file_or_directory
    //   Linux   error_code ENOTDIR                   → std::errc::not_a_directory
    std::optional<std::errc> CanonicalizeError(bool is_windows, const std::error_code& ec);


    // Cross-platform path helpers used by the file system layer.  The
    // platform-specific rules (separator character, drive-letter handling,
    // UNC prefix parsing) are selected at construction time by "is_windows".
    // This allows the mock FS to simulate Windows-style paths on a Unix host
    // and vice-versa, enabling cross-platform tests without a VM.
    struct GoFilepath {
        std::string cwd;
        bool        is_windows     = false;
        char        path_separator = '/';

        // Returns true when "path" is absolute.  On Unix this means it starts
        // with '/'; on Windows it also recognises "C:\", "\\server\share", etc.
        //
        // Example:
        //   IsAbs("/src/main.cpp")   → true
        //   IsAbs("src/main.cpp")    → false
        //   IsAbs("C:\\Windows")     → true   (Windows mode)
        bool                       IsAbs(std::string_view path) const;

        // Returns an absolute form of "path".  If "path" is already absolute
        // it is returned unchanged (after normalisation).  Otherwise it is
        // joined with the stored cwd.
        //
        // Example:
        //   cwd = "/project"
        //   Abs("src/main.cpp")  → "/project/src/main.cpp"
        //   Abs("/etc/passwd")   → "/etc/passwd"
        std::string                Abs(std::string_view path) const;

        // Returns true when "c" is a path separator for the current platform.
        // On Unix only '/' qualifies; on Windows both '/' and '\\' do.
        bool                       IsPathSeparator(char c) const;

        // Returns the number of bytes that form the volume prefix at the
        // start of "path".  On Unix this is always 0.  On Windows it returns
        // the length of the drive letter (e.g. 2 for "C:") or the UNC share
        // path.
        //
        // Example:
        //   VolumeNameLen("C:\\src\\main.cpp") → 2
        //   VolumeNameLen("\\\\server\\share") → 14
        //   VolumeNameLen("/usr/bin")          → 0
        int                        VolumeNameLen(std::string_view path) const;

        // Resolves every symlink in "path" and returns the canonical
        // real path.  Returns std::nullopt when any component of the path
        // does not exist or a symlink loop is detected.
        std::optional<std::string> EvalSymlinks(std::string_view path) const;

        // Replaces every path separator in "path" with '/'.
        //
        // Example (Windows mode):
        //   FromSlash("C:\\src\\main.cpp") → "C:/src/main.cpp"
        std::string                FromSlash(std::string_view path) const;

        // Normalises "path" by removing redundant separators and resolving
        // "." and ".." segments.  Does not touch symlinks or case.
        //
        // Example:
        //   Clean("/a/b/../c/./d") → "/a/c/d"
        //   Clean("a//b")         → "a/b"
        std::string                Clean(std::string_view path) const;

        // Returns the volume prefix (e.g. "C:" or "\\\\server\\share"),
        // or an empty string on Unix.
        std::string                VolumeName(std::string_view path) const;

        // Returns the final element of the path (the file name or last
        // directory component).
        //
        // Example:
        //   Base("/src/main.cpp") → "main.cpp"
        //   Base("/usr/bin/")     → "bin"
        std::string                Base(std::string_view path) const;

        // Returns the directory portion of the path, dropping the final
        // element.
        //
        // Example:
        //   Dir("/src/main.cpp") → "/src"
        //   Dir("/a/b/c")        → "/a/b"
        std::string                Dir(std::string_view path) const;

        // Returns the file extension including the leading dot, or an empty
        // string when there is no extension.
        //
        // Example:
        //   Ext("/src/main.cpp") → ".cpp"
        //   Ext("/src/Makefile") → ""
        std::string                Ext(std::string_view path) const;

        // Joins path elements with the platform separator.  Empty elements
        // are skipped.
        //
        // Example:
        //   Join({"", "src", "main.cpp"}) → "src/main.cpp"
        std::string                Join(const std::vector<std::string>& elem) const;

        // Computes the relative path from "basepath" to "targpath".  Returns
        // std::nullopt when the two paths share no common ancestor (e.g.
        // different drive letters on Windows).
        //
        // Example:
        //   Rel("/a/b", "/a/b/c/d") → "c/d"
        //   Rel("/a/b", "/x/y")    → std::nullopt  (different roots)
        std::optional<std::string> Rel(std::string_view basepath, std::string_view targpath) const;

        // Returns true when "a" and "b" refer to the same path under the
        // platform's case rules.  On Unix this is a byte-for-byte comparison;
        // on Windows it is case-insensitive.
        //
        // Example:
        //   SameWord("README.md", "readme.md") → true  (Windows mode)
        //   SameWord("README.md", "readme.md") → false (Unix mode)
        bool                       SameWord(std::string_view a, std::string_view b) const;
    };
}
