#include "guchho/filesystem.hpp"
#include "guchho/helpers.hpp"

#include <algorithm>
#include <cstdlib>
#include <semaphore>
#include <ctime>
#include <fstream>
#include <tuple>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace guchho::filesystem {

    namespace {

        // Counting semaphore that limits how many files can be open at the
        // same time across the entire process.  Some operating systems impose
        // a per-process limit on open file descriptors (e.g. 256 on older
        // macOS, 1024 on many Linux kernels).  By acquiring a permit before
        // every open and releasing it after every close, we guarantee the
        // process never exceeds 32 concurrent file handles.
        std::counting_semaphore<32> file_open_limit{32};

        // Converts a string to lowercase using ASCII-only rules (no locale).
        std::string Lowercase(std::string_view text)
        {
            return helpers::ToLowerASCII(text);
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // Entry: lazy stat + symlink resolution
    // ---------------------------------------------------------------------------

    // Returns the file type (regular file, directory, symlink, etc.) for this
    // directory entry. The first call triggers an actual filesystem stat() call;
    // subsequent calls return the cached result. The stat is performed under a
    // mutex so concurrent calls from multiple threads are safe.
    //
    // Example:
    //   Entry entry{.dir = "/home/user", .base = "file.txt"};
    //   entry.Kind(fs) => EntryKind::kFile
    //   entry.Kind(fs) => EntryKind::kFile  (cached, no syscall)
    //
    // Edge case: If the entry is a symlink, the symlink target path is also
    // captured and stored for later retrieval via Symlink().
    EntryKind Entry::Kind(Fs& fs)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (need_stat) {
            need_stat = false;
            std::pair<std::string, EntryKind> result = fs.Kind(dir, base);
            symlink = std::move(result.first);
            kind    = result.second;
        }
        return kind;
    }

    // Returns the symlink target path for this entry. Like Kind(), the first
    // call triggers a stat() if needed, then caches the result. If the entry
    // is not a symlink, returns an empty string.
    //
    // Example:
    //   Entry entry{.dir = "/usr/lib", .base = "libfoo.so"};
    //   entry.Symlink(fs) => "libfoo.so.1.2.3"  (if symlink)
    //   entry.Symlink(fs) => ""                  (if not symlink)
    std::string Entry::Symlink(Fs& fs)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (need_stat) {
            need_stat = false;
            std::pair<std::string, EntryKind> result = fs.Kind(dir, base);
            symlink = std::move(result.first);
            kind    = result.second;
        }
        return symlink;
    }

    // ---------------------------------------------------------------------------
    // DirEntries: directory listing and lookup
    // ---------------------------------------------------------------------------

    // Creates an empty DirEntries object for the given directory path.
    // The internal data map is initialized but contains no entries.
    // This is useful as a sentinel value or for testing.
    //
    // Example:
    //   DirEntries e = MakeEmptyDirEntries("/src");
    //   e.PeekEntryCount() => 0
    DirEntries MakeEmptyDirEntries(const std::string& dir)
    {
        DirEntries entries;
        entries.dir  = dir;
        entries.data = std::map<std::string, std::shared_ptr<Entry>>{};
        return entries;
    }

    // Looks up a file or directory by name within this directory listing.
    // The query is case-insensitive on all platforms (normalized to lowercase
    // for the internal map key). Returns a pair of:
    //   - A shared_ptr to the Entry (nullptr if not found)
    //   - An optional DifferentCase struct if the file exists but with a
    //     different capitalization (e.g. query "Readme.md" found "README.md")
    //
    // If watch mode is active (accessed_entries is non-null), this method
    // records whether the entry was present or absent, which is later used
    // to detect file system changes.
    //
    // Example:
    //   DirEntries entries = ...;  // contains "index.ts"
    //   auto [entry, diff] = entries.Get("Index.TS");
    //   entry != nullptr       => true (found case-insensitively)
    //   diff.has_value()       => true (case mismatch)
    //   diff->actual           => "index.ts"
    std::pair<std::shared_ptr<Entry>, std::optional<DifferentCase>> DirEntries::Get(const std::string& query)
    {
        if (data) {
            std::string key      = Lowercase(query);
            auto        iterator = data->find(key);
            std::shared_ptr<Entry> entry =
                iterator != data->end() ? iterator->second : nullptr;

            if (accessed_entries) {
                std::lock_guard<std::mutex> lock(accessed_entries->mutex);
                accessed_entries->was_present[key] = entry != nullptr;
            }

            if (entry) {
                if (entry->base != query) {
                    DifferentCase different_case;
                    different_case.dir    = dir;
                    different_case.query  = query;
                    different_case.actual = entry->base;
                    return {entry, different_case};
                }
                return {entry, std::nullopt};
            }
        }

        return {nullptr, std::nullopt};
    }

    // Returns the number of entries in this directory without allocating
    // a vector. Returns 0 if the directory data has not been loaded.
    //
    // Example:
    //   DirEntries e = MakeEmptyDirEntries("/src");
    //   e.PeekEntryCount() => 0
    int DirEntries::PeekEntryCount() const
    {
        if (data) {
            return int(data->size());
        }
        return 0;
    }

    // Returns a sorted vector of all entry names (base names) in this
    // directory. The names are sorted lexicographically. If watch mode
    // is active, the full sorted list is recorded for change detection.
    //
    // This allocates a new vector on each call; for read-only counts,
    // use PeekEntryCount() instead.
    //
    // Example:
    //   DirEntries e = ...;  // contains {"b.txt", "a.txt", "c.txt"}
    //   e.SortedKeys() => {"a.txt", "b.txt", "c.txt"}
    std::vector<std::string> DirEntries::SortedKeys()
    {
        std::vector<std::string> keys;
        if (data) {
            keys.reserve(data->size());
            for (const auto& [key, entry] : *data) {
                keys.push_back(entry->base);
            }
            std::sort(keys.begin(), keys.end());

            if (accessed_entries) {
                std::lock_guard<std::mutex> lock(accessed_entries->mutex);
                accessed_entries->all_entries = keys;
            }
        }
        return keys;
    }

    // ---------------------------------------------------------------------------
    // InMemoryOpenedFile: in-memory file handle for virtual/test file systems
    // ---------------------------------------------------------------------------

    // Returns the length of the in-memory file contents in bytes.
    //
    // Example:
    //   InMemoryOpenedFile f("hello world");
    //   f.Len() => 11
    int InMemoryOpenedFile::Len() const
    {
        return int(contents.size());
    }

    // Reads a substring from [start, end) of the in-memory file contents.
    // Both start and end are zero-based byte offsets. If end exceeds the
    // file length, the read is truncated to the available data.
    //
    // Example:
    //   InMemoryOpenedFile f("hello world");
    //   f.Read(0, 5)  => "hello"
    //   f.Read(6, 11) => "world"
    std::string InMemoryOpenedFile::Read(int start, int end)
    {
        return contents.substr(size_t(start), size_t(end - start));
    }

    // Closes the in-memory file. Always returns true since there are no
    // OS resources to release.
    bool InMemoryOpenedFile::Close()
    {
        return true;
    }

    // ---------------------------------------------------------------------------
    // File open/close rate limiting
    // ---------------------------------------------------------------------------

    // Acquires a permit from the file-open semaphore, blocking if all 32
    // permits are already held. Must be called before opening any file to
    // prevent hitting the OS file descriptor limit.
    void BeforeFileOpen()
    {
        file_open_limit.acquire();
    }

    // Releases a permit back to the file-open semaphore. Must be called
    // after closing a file that was previously opened with BeforeFileOpen().
    void AfterFileClose()
    {
        file_open_limit.release();
    }

    // ---------------------------------------------------------------------------
    // Path conversion utilities
    // ---------------------------------------------------------------------------

    // Converts a UTF-8 encoded std::string_view to a std::filesystem::path.
    // On Windows this performs the appropriate wide-character conversion;
    // on Unix systems it is essentially a no-op since paths are byte strings.
    //
    // Example:
    //   PathFromUTF8("src/index.ts") => std::filesystem::path("src/index.ts")
    std::filesystem::path PathFromUTF8(std::string_view utf8)
    {
        const char8_t* first = reinterpret_cast<const char8_t*>(utf8.data());
        const char8_t* last  = first + utf8.size();
        return std::filesystem::path(first, last);
    }

    // Converts a std::filesystem::path to a UTF-8 encoded std::string.
    // On Windows this performs the appropriate narrow-character conversion;
    // on Unix systems it is essentially a no-op.
    //
    // Example:
    //   PathToUTF8(std::filesystem::path("src/index.ts")) => "src/index.ts"
    std::string PathToUTF8(const std::filesystem::path& path)
    {
        std::u8string u8 = path.u8string();
        return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
    }

    // ---------------------------------------------------------------------------
    // CanonicalizeError: normalize platform-specific error codes
    // ---------------------------------------------------------------------------

    // Maps a platform-specific error code from std::error_code to a
    // normalized std::errc value. This is used during path canonicalization
    // to ensure consistent error handling across Windows and Unix.
    //
    // The key normalization is that Windows-specific error codes like
    // ERROR_INVALID_NAME (123), ERROR_FILE_NOT_FOUND (2), and
    // ERROR_PATH_NOT_FOUND (3) are all mapped to ENOENT, so the resolver
    // treats them as "file not found" and continues trying alternative
    // paths instead of aborting.
    //
    // On Windows, ENOTDIR is also mapped to ENOENT because Windows returns
    // ENOTDIR even for non-directory lookups, which is misleading.
    //
    // Returns std::nullopt if there is no error.
    //
    // Example:
    //   std::error_code ec = ...;  // Windows ERROR_FILE_NOT_FOUND
    //   CanonicalizeError(true, ec) => std::errc::no_such_file_or_directory
    std::optional<std::errc> CanonicalizeError(bool is_windows, const std::error_code& ec)
    {
        if (!ec) {
            return std::nullopt;
        }

        if (ec == std::errc::no_such_file_or_directory) {
            return std::errc::no_such_file_or_directory;
        }

        if (ec == std::errc::not_a_directory) {
            return std::errc::no_such_file_or_directory;
        }

#ifdef _WIN32
        if (ec.category() == std::system_category()) {
            constexpr int kErrorInvalidName  = 123;
            constexpr int kErrorFileNotFound = 2;
            constexpr int kErrorPathNotFound = 3;
            constexpr int kErrorDirectory    = 267;
            if (ec.value() == kErrorInvalidName ||
                ec.value() == kErrorFileNotFound ||
                ec.value() == kErrorPathNotFound ||
                ec.value() == kErrorDirectory) {
                return std::errc::no_such_file_or_directory;
            }
        }
#else
        (void) is_windows;
#endif

        std::error_condition condition = ec.default_error_condition();
        if (condition.category() == std::generic_category()) {
            return std::errc(condition.value());
        }

        return std::errc::io_error;
    }

    // ---------------------------------------------------------------------------
    // MkdirAll: recursive directory creation
    // ---------------------------------------------------------------------------

    // Recursively creates directories along the given path, similar to
    // `mkdir -p` on Unix. If the path already exists as a directory,
    // returns true immediately. If the path exists as a non-directory
    // file, returns false with an error message.
    //
    // The algorithm works in two phases:
    //   1. Fast path: check if the path already exists (directory or file).
    //   2. Slow path: recursively ensure the parent exists, then create
    //      the final directory component.
    //
    // After creating the directory, a second check handles edge cases like
    // "foo/." where create_directory() fails but the directory already exists.
    //
    // Example:
    //   std::string error;
    //   MkdirAll(fs, "/tmp/build/output", error) => true
    //   // Creates /tmp, /tmp/build, /tmp/build/output as needed
    //
    // Edge case: Paths ending in "." or ".." are handled gracefully by
    // the double-check after create_directory() fails.
    namespace {
        bool MkdirAllImpl(Fs& fs, const std::string& path, std::string& error)
        {
            std::error_code ec;
            auto status = std::filesystem::status(PathFromUTF8(path), ec);
            if (!ec) {
                if (status.type() == std::filesystem::file_type::directory) {
                    return true;
                }
                error = "mkdir " + path + ": not a directory";
                return false;
            }

            std::string parent = fs.Dir(path);
            if (parent != path) {
                if (!MkdirAllImpl(fs, parent, error)) {
                    return false;
                }
            }

            std::filesystem::create_directory(PathFromUTF8(path), ec);
            if (ec) {
                std::error_code ec2;
                auto s2 = std::filesystem::symlink_status(PathFromUTF8(path), ec2);
                if (!ec2 && s2.type() == std::filesystem::file_type::directory) {
                    return true;
                }
                error = ec.message();
                return false;
            }
            return true;
        }
    } // namespace

    // Public entry point for MkdirAll. Normalizes the path by running it
    // through Join() (which cleans trailing slashes and redundant separators)
    // before delegating to the recursive implementation.
    bool MkdirAll(Fs& fs, const std::string& path, std::string& error)
    {
        return MkdirAllImpl(fs, fs.Join({path}), error);
    }

    // ---------------------------------------------------------------------------
    // Slash character utilities (internal)
    // ---------------------------------------------------------------------------

    // Returns true if the character is a forward slash or backslash.
    // Used by the Yarn PnP virtual path parser to handle both Unix and
    // Windows path separators interchangeably.
    namespace {

        bool IsSlashChar(char c)
        {
            return c == '/' || c == '\\';
        }

        // Finds the index of the first slash character (forward or back) at or
        // after position 'start'. Returns std::string::npos if none found.
        size_t IndexAnySlash(const std::string& s, size_t start)
        {
            for (size_t i = start; i < s.size(); i++) {
                if (IsSlashChar(s[i])) return i;
            }
            return std::string::npos;
        }

        // Finds the index of the last slash character before position
        // 'end_exclusive'. Searches backwards from end_exclusive-1 to 0.
        // Returns std::string::npos if no slash is found.
        size_t LastIndexAnySlash(const std::string& s, size_t end_exclusive)
        {
            if (end_exclusive == 0) return std::string::npos;
            for (size_t i = end_exclusive; i-- > 0;) {
                if (IsSlashChar(s[i])) return i;
                if (i == 0) break;
            }
            return std::string::npos;
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // Yarn PnP virtual path handling
    // ---------------------------------------------------------------------------

    // Parses a Yarn Plug'n'Play virtual path into its prefix and suffix
    // components. Yarn PnP uses "__virtual__" or "$$virtual" segments
    // in paths to represent packages that are stored inside other packages'
    // zip archives. The virtual path format encodes a depth count that
    // indicates how many directory levels to go up from the virtual
    // segment to reach the real location.
    //
    // The parser scans for "__virtual__" or "$$virtual" segments, reads
    // the numeric depth count, and applies the corresponding number of
    // ".." operations to the prefix path.
    //
    // Returns std::nullopt if the path does not contain a virtual segment.
    //
    // Example:
    //   Input:  "node_modules/pkg/__virtual__/0/node_modules/dep"
    //   Output: { prefix: "node_modules/pkg", suffix: "node_modules/dep" }
    //
    // Example:
    //   Input:  "a/b/$$virtual/2/c/d"
    //   Output: { prefix: "a", suffix: "c/d" }
    //
    // Edge case: If the depth count exceeds the number of available parent
    // directories, the prefix is reduced to the root (or "." if empty).
    std::optional<YarnPnPVirtualPath> ParseYarnPnPVirtualPath(const std::string& path)
    {
        size_t i = 0;
        while (true) {
            size_t start = i;
            size_t slash = IndexAnySlash(path, i);
            if (slash == std::string::npos) {
                break;
            }
            i = slash + 1;

            std::string_view segment(path.data() + start, slash - start);
            if (segment != "__virtual__" && segment != "$$virtual") {
                continue;
            }

            size_t slash2 = IndexAnySlash(path, i);
            if (slash2 == std::string::npos) {
                continue;
            }

            size_t j = slash2 + 1;
            std::string count;
            std::string suffix;

            size_t slash3 = IndexAnySlash(path, j);
            if (slash3 != std::string::npos) {
                count  = path.substr(j, slash3 - j);
                suffix = path.substr(slash3);
            } else {
                count = path.substr(j);
            }

            if (count.empty()) continue;
            char* end_ptr = nullptr;
            long long n = std::strtoll(count.c_str(), &end_ptr, 10);
            if (end_ptr == nullptr || *end_ptr != '\0' || end_ptr == count.c_str()) {
                continue;
            }

            std::string prefix = path.substr(0, start);

            while (n > 0 && !prefix.empty() && IsSlashChar(prefix.back())) {
                size_t prefix_without_trail = prefix.size() - 1;
                size_t last_slash = LastIndexAnySlash(prefix, prefix_without_trail);
                if (last_slash == std::string::npos) {
                    break;
                }
                prefix.resize(last_slash + 1);
                n--;
            }

            if (suffix.empty() && IndexAnySlash(prefix, 0) != LastIndexAnySlash(prefix, prefix.size())) {
                if (!prefix.empty() && IsSlashChar(prefix.back())) {
                    prefix.pop_back();
                }
            } else if (prefix.empty()) {
                prefix = ".";
            } else if (!suffix.empty() && IsSlashChar(suffix.front())) {
                suffix.erase(suffix.begin());
            }

            YarnPnPVirtualPath result;
            result.prefix = std::move(prefix);
            result.suffix = std::move(suffix);
            return result;
        }

        return std::nullopt;
    }

    // Simplifies a Yarn PnP virtual path by removing the virtual segment
    // and applying the depth navigation. If the path is not a virtual
    // path, it is returned unchanged.
    //
    // Example:
    //   MangleYarnPnPVirtualPath("a/b/__virtual__/1/c") => "a/c"
    //   MangleYarnPnPVirtualPath("a/b/c")                => "a/b/c"
    std::string MangleYarnPnPVirtualPath(const std::string& path)
    {
        if (auto parsed = ParseYarnPnPVirtualPath(path)) {
            return parsed->prefix + parsed->suffix;
        }
        return path;
    }


    // Returns true when the current process is running on Windows.  Used to
    // select path-convention rules at construction time.
    bool CheckIfWindows()
    {
        #ifdef _WIN32
                return true;
        #else
                return false;
        #endif
    }

    namespace {

        // Describes how the watch-mode closure for a given path should detect
        // changes.  The state transitions are:
        //   kNone => kFileNeedModKey => kFileHasModKey / kFileUnusableModKey
        //   kNone => kFileMissing
        //   kNone => kDirHasAccessedEntries / kDirUnreadable
        enum class WatchState : uint8_t {
            kNone,
            kDirHasAccessedEntries, // Compare "accessed_entries"
            kDirUnreadable,         // Compare directory readability
            kFileHasModKey,         // Compare "mod_key"
            kFileNeedModKey,        // Need to transition to "kFileHasModKey" or "kFileUnusableModKey" before watch data is returned
            kFileMissing,           // Compare file presence
            kFileUnusableModKey,    // Compare "file_contents"
        };

        struct EntriesOrErr {
            DirEntries                entries;
            std::optional<std::errc>  canonical_error;
            std::string               original_error;
        };

        struct PrivateWatchData {
            std::shared_ptr<AccessedEntries> accessed_entries;
            std::string                      file_contents;
            ModKey                           mod_key;
            WatchState                       state = WatchState::kNone;
        };

        // Opens a real file on disk for random-access reads.  The file is
        // kept open for the lifetime of the object; Close() releases the
        // underlying ifstream.
        class RealOpenedFile : public OpenedFile {
        public:
            RealOpenedFile(std::ifstream stream, int len)
                : stream_(std::move(stream)),
                  len_(len)
            {
            }

            int Len() const override
            {
                return len_;
            }

            // Reads bytes in the range [start, end) from the file.  The
            // stream is seeked to the start offset and read in a loop until
            // all requested bytes are obtained or EOF is reached.
            std::string Read(int start, int end) override
            {
                std::string bytes(size_t(end - start), '\0');
                if (bytes.empty()) {
                    return bytes;
                }
                stream_.clear();
                stream_.seekg(std::streamoff(start));
                if (!stream_) {
                    return {};
                }
                size_t total = 0;
                while (total < bytes.size()) {
                    stream_.read(bytes.data() + ptrdiff_t(total), std::streamsize(bytes.size() - total));
                    total += size_t(stream.gcount());
                    if (!stream_.good()) {
                        break;
                    }
                }
                bytes.resize(total);
                return bytes;
            }

            bool Close() override
            {
                stream_.close();
                return true;
            }

        private:
            std::ifstream stream_;
            int           len_;
        };

        // Reads the entire contents of a file into a string.  The file is
        // opened in binary mode and read in 32 KB chunks.  Returns the
        // contents and an empty error code on success, or an empty string
        // and the OS error code on failure.
        //
        // Example:
        //   auto [contents, ec] = ReadWholeFile("/etc/hosts");
        //   ec == std::errc{}  =>  true on success
        std::pair<std::string, std::error_code> ReadWholeFile(const std::string& path)
        {
            std::ifstream stream(PathFromUTF8(path), std::ios_base::binary);
            if (!stream.is_open()) {
                return {"", std::error_code(errno, std::generic_category())};
            }
            std::string contents;
            char        buffer[32768];
            while (stream) {
                stream.read(buffer, sizeof(buffer));
                contents.append(buffer, size_t(stream.gcount()));
            }
            if (stream.bad()) {
                return {"", std::error_code(EIO, std::generic_category())};
            }
            return {contents, {}};
        }

#ifdef _WIN32

        // Computes a metadata fingerprint for a file on Windows using _wstat64.
        // Returns a ModKeyResult with the file's size, mtime, and mode.  If
        // the mtime is zero or the file is too new (within kModKeySafetyGap
        // seconds of the current time), the result is marked as unusable.
        //
        // Example:
        //   ModKeyResult r = ModKeyImpl("C:\\src\\main.cpp");
        //   r.Ok()          =>  true if file exists and mtime is trustworthy
        //   r.unusable      =>  true if mtime is zero or too fresh
        ModKeyResult ModKeyImpl(const std::string& path)
        {
            ModKeyResult result;

            BeforeFileOpen();
            struct _stat64 st;
            std::wstring   wide_path = PathFromUTF8(path).wstring();
            if (_wstat64(wide_path.c_str(), &st) != 0) {
                std::error_code ec(errno, std::generic_category());
                AfterFileClose();
                result.canonical_error = CanonicalizeError(true, ec);
                result.original_error  = ec.message();
                return result;
            }

            // We can't detect changes if the file system zeros out the modification time
            if (st.st_mtime == 0) {
                AfterFileClose();
                result.unusable = true;
                return result;
            }

            // Don't generate a modification key if the file is too new
            if (st.st_mtime + kModKeySafetyGap > time(nullptr)) {
                AfterFileClose();
                result.unusable = true;
                return result;
            }

            result.value.size      = st.st_size;
            result.value.mtime_sec = int64_t(st.st_mtime);
            result.value.mode      = uint32_t(st.st_mode);

            AfterFileClose();
            return result;
        }

#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(__FreeBSD__)

        // Computes a metadata fingerprint for a file on Unix using stat(2).
        // Returns a ModKeyResult with the file's inode, size, mtime (with
        // nanosecond precision), mode, and uid.  If the mtime is zero or the
        // file is too new, the result is marked as unusable.
        //
        // Example:
        //   ModKeyResult r = ModKeyImpl("/src/main.cpp");
        //   r.value.inode      =>  inode number
        //   r.value.mtime_sec  =>  seconds since epoch
        //   r.value.mtime_nsec =>  nanosecond remainder
        ModKeyResult ModKeyImpl(const std::string& path)
        {
            ModKeyResult result;

            BeforeFileOpen();
            struct stat st;
            if (::stat(path.c_str(), &st) != 0) {
                std::error_code ec(errno, std::generic_category());
                AfterFileClose();
                result.canonical_error = CanonicalizeError(false, ec);
                result.original_error  = ec.message();
                return result;
            }

#if defined(__APPLE__)
            long mtime_sec  = long(st.st_mtimespec.tv_sec);
            long mtime_nsec = long(st.st_mtimespec.tv_nsec);
#else
            long mtime_sec  = long(st.st_mtim.tv_sec);
            long mtime_nsec = long(st.st_mtim.tv_nsec);
#endif

            // We can't detect changes if the file system zeros out the modification time
            if (mtime_sec == 0 && mtime_nsec == 0) {
                AfterFileClose();
                result.unusable = true;
                return result;
            }

            // Don't generate a modification key if the file is too new
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (mtime_sec + kModKeySafetyGap > now.tv_sec ||
                (mtime_sec + kModKeySafetyGap == now.tv_sec && mtime_nsec > now.tv_nsec)) {
                AfterFileClose();
                result.unusable = true;
                return result;
            }

            result.value.inode      = uint64_t(st.st_ino);
            result.value.size       = st.st_size;
            result.value.mtime_sec  = int64_t(mtime_sec);
            result.value.mtime_nsec = int64_t(mtime_nsec);
            result.value.mode       = uint32_t(st.st_mode);
            result.value.uid        = uint32_t(st.st_uid);

            AfterFileClose();
            return result;
        }

#else

#error Unsupported platform: no ModKey implementation

#endif

        // Real file system implementation backed by the host OS.  All file
        // and directory operations delegate to std::filesystem or platform-
        // specific APIs.  An optional directory-entry cache avoids redundant
        // stat() calls when the same directory is listed multiple times.
        class RealFS : public Fs {
        public:
            RealFS(GoFilepath fp, const RealFsOptions& options)
                : fp_(std::move(fp)),
                  do_not_cache_(options.do_not_cache)
            {
                // Only allocate memory for watch data if necessary
                if (options.want_watch_data) {
                    watch_data_.emplace();
                }
            }

            // Lists directory entries for "dir".  When caching is enabled,
            // a cache hit returns the stored result immediately.  On a cache
            // miss the directory is read via directory_iterator, entries are
            // created with lazy stat (need_stat = true), and the result is
            // stored for future lookups.
            //
            // Example:
            //   auto result = ReadDirectory("/src");
            //   result.Ok()           =>  true if directory was readable
            //   result.value.PeekEntryCount()  =>  number of entries
            FsResult<DirEntries> ReadDirectory(const std::string& dir) override
            {
                if (!do_not_cache_) {
                    // First, check the cache
                    std::lock_guard<std::mutex> lock(entries_mutex_);
                    auto                        found = entries_.find(dir);
                    if (found != entries_.end()) {
                        // Cache hit: stop now
                        FsResult<DirEntries> result;
                        result.value           = found->second.entries;
                        result.canonical_error = found->second.canonical_error;
                        result.original_error  = found->second.original_error;
                        return result;
                    }
                }

                // Cache miss: read the directory entries
                auto [names, canonical_ec, original_error] = ReadDir(dir);
                DirEntries dir_entries                     = MakeEmptyDirEntries(dir);

                // Preserve ENOTDIR from readdir (file-as-directory) instead of mapping to ENOENT,
                // mirroring Go where readdir's ENOTDIR is not passed through canonicalizeError.
                std::optional<std::errc> canonical_error;
                if (canonical_ec == std::errc::not_a_directory) {
                    canonical_error = std::errc::not_a_directory;
                } else {
                    canonical_error = CanonicalizeError(fp_.is_windows, canonical_ec);
                    if (canonical_error == std::errc::invalid_argument) {
                        canonical_error = std::errc::not_a_directory;
                    }
                }

                if (!canonical_error) {
                    for (const std::string& name : names) {
                        // Call "stat" lazily for performance. The "@material-ui/icons" package
                        // contains a directory with over 11,000 entries in it and running "stat"
                        // for each entry was a big performance issue for that package.
                        auto entry        = std::make_shared<Entry>();
                        entry->dir        = dir;
                        entry->base       = name;
                        entry->need_stat  = true;
                        dir_entries.data->emplace(helpers::ToLowerASCII(name), std::move(entry));
                    }
                }

                // Store data for watch mode
                if (watch_data_) {
                    std::lock_guard<std::mutex>  lock(watch_mutex_);
                    PrivateWatchData             data;
                    data.state = canonical_error ? WatchState::kDirUnreadable : WatchState::kDirHasAccessedEntries;
                    dir_entries.accessed_entries = std::make_shared<AccessedEntries>();
                    data.accessed_entries        = dir_entries.accessed_entries;
                    (*watch_data_)[dir]          = std::move(data);
                }

                // Update the cache unconditionally. Even if the read failed, we don't want to
                // retry again later. The directory is inaccessible so trying again is wasted.
                if (canonical_error) {
                    dir_entries.data.reset();
                }
                if (!do_not_cache_) {
                    std::lock_guard<std::mutex> lock(entries_mutex_);
                    EntriesOrErr cached;
                    cached.entries         = dir_entries;
                    cached.canonical_error = canonical_error;
                    cached.original_error  = original_error;
                    entries_[dir]          = std::move(cached);
                }

                FsResult<DirEntries> result;
                result.value           = dir_entries;
                result.canonical_error = canonical_error;
                result.original_error  = original_error;
                return result;
            }

            // Reads the entire file at "path" into a string.  When watch
            // mode is active the file contents are stored so that future
            // change-detection can compare against them (for files whose
            // mtime is unreliable).
            //
            // Example:
            //   auto result = ReadFile("/src/main.cpp");
            //   result.Ok()    =>  true on success
            //   result.value   =>  file contents as a string
            FsResult<std::string> ReadFile(const std::string& path) override
            {
                BeforeFileOpen();
                auto [contents, ec] = ReadWholeFile(path);
                AfterFileClose();

                std::optional<std::errc> canonical_error = CanonicalizeError(fp_.is_windows, ec);

                // Store data for watch mode
                if (watch_data_) {
                    std::lock_guard<std::mutex> lock(watch_mutex_);
                    PrivateWatchData&           data = (*watch_data_)[path];
                    if (canonical_error) {
                        data.state = WatchState::kFileMissing;
                    } else if (data.state == WatchState::kNone || data.state == WatchState::kDirUnreadable) {
                        // Note: If "ReadDirectory" is called before "ReadFile" with this same
                        // path, then "data.state" will be "kDirUnreadable". In that case
                        // we want to transition to "kFileNeedModKey" because it's a file.
                        data.state = WatchState::kFileNeedModKey;
                    }
                    data.file_contents = contents;
                }

                FsResult<std::string> result;
                result.value           = std::move(contents);
                result.canonical_error = canonical_error;
                result.original_error  = ec.message();
                return result;
            }

            // Opens the file at "path" for random-access reads.  The returned
            // handle keeps the file open until Close() is called.
            //
            // Example:
            //   auto result = OpenFile("/src/main.cpp");
            //   result.Ok()              =>  true on success
            //   result.value->Len()      =>  file size in bytes
            FsResult<std::shared_ptr<OpenedFile>> OpenFile(const std::string& path) override
            {
                BeforeFileOpen();

                errno = 0;
                std::ifstream stream(PathFromUTF8(path), std::ios_base::binary);
                if (!stream.is_open()) {
                    std::error_code ec(errno, std::generic_category());
                    AfterFileClose();
                    FsResult<std::shared_ptr<OpenedFile>> result;
                    result.canonical_error = CanonicalizeError(fp_.is_windows, ec);
                    result.original_error  = ec.message();
                    return result;
                }

                stream.seekg(0, std::ios_base::end);
                std::streamoff size = stream.tellg();
                stream.clear();
                stream.seekg(0, std::ios_base::beg);
                if (size < 0) {
                    std::error_code ec(EIO, std::generic_category());
                    AfterFileClose();
                    FsResult<std::shared_ptr<OpenedFile>> result;
                    result.canonical_error = CanonicalizeError(fp_.is_windows, ec);
                    result.original_error  = ec.message();
                    return result;
                }

                AfterFileClose();

                FsResult<std::shared_ptr<OpenedFile>> result;
                result.value = std::make_shared<RealOpenedFile>(std::move(stream), int(size));
                return result;
            }

            // Computes a metadata fingerprint for "path" via the platform-
            // specific ModKeyImpl.  When watch mode is active the key is
            // stored so GetWatchData() can later produce a closure that
            // compares against it.
            //
            // Example:
            //   ModKeyResult r = ModKey("/src/main.cpp");
            //   r.Ok()     =>  true if key is trustworthy
            //   r.unusable =>  true if mtime is zero or too fresh
            ModKeyResult ModKey(const std::string& path) override
            {
                ModKeyResult result = ModKeyImpl(path);

                // Store data for watch mode
                if (watch_data_) {
                    std::lock_guard<std::mutex> lock(watch_mutex_);
                    PrivateWatchData&           data = (*watch_data_)[path];
                    if (data.state == WatchState::kNone) {
                        if (result.unusable) {
                            data.state = WatchState::kFileUnusableModKey;
                        } else if (!result.Ok()) {
                            data.state = WatchState::kFileMissing;
                        } else {
                            data.state = WatchState::kFileHasModKey;
                        }
                    } else if (data.state == WatchState::kFileNeedModKey) {
                        data.state = WatchState::kFileHasModKey;
                    }
                    data.mod_key = result.value;
                }

                return result;
            }

            bool IsAbs(std::string_view p) override
            {
                return fp_.IsAbs(p);
            }

            std::optional<std::string> Abs(std::string_view p) override
            {
                return fp_.Abs(p);
            }

            std::string Dir(std::string_view p) override
            {
                return fp_.Dir(p);
            }

            std::string Base(std::string_view p) override
            {
                return fp_.Base(p);
            }

            std::string Ext(std::string_view p) override
            {
                return fp_.Ext(p);
            }

            std::string Join(std::initializer_list<std::string_view> parts) override
            {
                std::vector<std::string> converted;
                converted.reserve(parts.size());
                for (std::string_view part : parts) {
                    converted.emplace_back(part);
                }
                return fp_.Clean(fp_.Join(converted));
            }

            std::string Cwd() override
            {
                return fp_.cwd;
            }

            std::optional<std::string> Rel(std::string_view base, std::string_view target) override
            {
                return fp_.Rel(base, target);
            }

            std::optional<std::string> EvalSymlinks(std::string_view path) override
            {
                return fp_.EvalSymlinks(path);
            }

            // Classifies the entry at (dir, base) as a file or directory.
            // If the entry is a symlink, the target is resolved and the
            // symlink target path is returned as the first element of the
            // pair.  Returns kInvalid if the stat fails or the symlink
            // cannot be resolved.
            //
            // Example:
            //   Kind("/usr/lib", "libfoo.so") => ("/usr/lib/libfoo.so.1", kFile)
            std::pair<std::string, EntryKind> Kind(std::string_view dir, std::string_view base) override
            {
                std::string entry_path = fp_.Join({std::string(dir), std::string(base)});

                std::error_code ec;
                // Use "symlink_status" since we want information about symbolic links
                BeforeFileOpen();
                auto status = std::filesystem::symlink_status(PathFromUTF8(entry_path), ec);
                AfterFileClose();
                if (ec) {
                    return {"", EntryKind::kInvalid};
                }

                std::string symlink;

                // Follow symlinks now so the cache contains the translation
                if (status.type() == std::filesystem::file_type::symlink) {
                    auto link = fp_.EvalSymlinks(entry_path);
                    if (!link) {
                        return {"", EntryKind::kInvalid}; // Skip over this entry
                    }

                    // Re-run "lstat" on the symlink target to see if it's a file or not
                    std::error_code ec2;
                    BeforeFileOpen();
                    status = std::filesystem::symlink_status(PathFromUTF8(*link), ec2);
                    AfterFileClose();
                    if (ec2) {
                        return {"", EntryKind::kInvalid}; // Skip over this entry
                    }
                    if (status.type() == std::filesystem::file_type::symlink) {
                        return {"", EntryKind::kInvalid}; // This should no longer be a symlink, so this is unexpected
                    }
                    symlink = *link;
                }

                // We consider the entry either a directory or a file
                EntryKind kind = status.type() == std::filesystem::file_type::directory
                                     ? EntryKind::kDir
                                     : EntryKind::kFile;
                return {symlink, kind};
            }

            // Builds and returns the full set of watch-mode closures.  Each
            // entry in the returned WatchData maps a file-system path to a
            // thunk that, when called, returns a non-empty path if the entry
            // has been modified since the last build, or an empty string if
            // the entry is unchanged.
            //
            // The closure strategy depends on the WatchState:
            //   - kDirHasAccessedEntries: re-lists the directory and
            //     compares against the recorded accessed_entries snapshot.
            //   - kFileHasModKey: re-computes the ModKey and compares.
            //   - kFileUnusableModKey: re-reads the file and compares.
            //   - kFileMissing: checks whether the file now exists.
            //   - kDirUnreadable: checks whether the directory is now
            //     readable.
            WatchData GetWatchData() override
            {
                WatchData result;

                if (!watch_data_) {
                    return result;
                }

                std::lock_guard<std::mutex> lock(watch_mutex_);

                for (auto& [path, stored] : *watch_data_) {
                    PrivateWatchData data = stored; // Each closure needs its own copy

                    if (data.state == WatchState::kFileNeedModKey) {
                        ModKeyResult key_result = ModKeyImpl(path);
                        if (key_result.unusable) {
                            data.state = WatchState::kFileUnusableModKey;
                        } else if (!key_result.Ok()) {
                            data.state = WatchState::kFileMissing;
                        } else {
                            data.state    = WatchState::kFileHasModKey;
                            data.mod_key  = key_result.value;
                        }
                    }

                    switch (data.state) {
                    case WatchState::kDirUnreadable: {
                        std::string watched = path;
                        result.paths.emplace(watched, [this, watched]() -> std::string {
                            auto [names, ec, original_error] = ReadDir(watched);
                            if (!ec) {
                                return watched;
                            }
                            return "";
                        });
                        break;
                    }

                    case WatchState::kDirHasAccessedEntries: {
                        std::string     watched  = path;
                        PrivateWatchData captured = data;
                        result.paths.emplace(watched, [this, watched, captured]() -> std::string {
                            auto [names, ec, original_error] = ReadDir(watched);
                            if (ec) {
                                return watched;
                            }
                            auto accessed = captured.accessed_entries;
                            std::lock_guard<std::mutex> entries_lock(accessed->mutex);
                            if (accessed->all_entries) {
                                // Check all entries
                                const std::vector<std::string>& all_entries = *accessed->all_entries;
                                if (names.size() != all_entries.size()) {
                                    return watched;
                                }
                                std::vector<std::string> sorted(names);
                                std::sort(sorted.begin(), sorted.end());
                                for (size_t i = 0; i < sorted.size(); i++) {
                                    if (sorted[i] != all_entries[i]) {
                                        return watched;
                                    }
                                }
                            } else {
                                // Check individual entries
                                std::unordered_map<std::string, std::string> lookup;
                                lookup.reserve(names.size());
                                for (const std::string& name : names) {
                                    lookup.emplace(helpers::ToLowerASCII(name), name);
                                }
                                for (const auto& [name, was_present] : accessed->was_present) {
                                    auto found = lookup.find(name);
                                    bool is_present = found != lookup.end();
                                    if (was_present != is_present) {
                                        if (is_present) {
                                            return Join({watched, found->second});
                                        } else {
                                            return watched;
                                        }
                                    }
                                }
                            }
                            return "";
                        });
                        break;
                    }

                    case WatchState::kFileMissing: {
                        std::string watched = path;
                        result.paths.emplace(watched, [watched]() -> std::string {
                            std::error_code ec;
                            auto            status = std::filesystem::status(PathFromUTF8(watched), ec);
                            if (!ec && status.type() != std::filesystem::file_type::directory &&
                                status.type() != std::filesystem::file_type::not_found &&
                                status.type() != std::filesystem::file_type::none) {
                                return watched;
                            }
                            return "";
                        });
                        break;
                    }

                    case WatchState::kFileHasModKey: {
                        std::string watched = path;
                        auto        key     = data.mod_key;
                        result.paths.emplace(watched, [watched, key]() -> std::string {
                            ModKeyResult current = ModKeyImpl(watched);
                            if (!current.Ok() || !(current.value == key)) {
                                return watched;
                            }
                            return "";
                        });
                        break;
                    }

                    case WatchState::kFileUnusableModKey: {
                        std::string watched    = path;
                        std::string contents_of = data.file_contents;
                        result.paths.emplace(watched, [watched, contents_of]() -> std::string {
                            auto [buffer, ec] = ReadWholeFile(watched);
                            if (ec || buffer != contents_of) {
                                return watched;
                            }
                            return "";
                        });
                        break;
                    }

                    case WatchState::kNone:
                    case WatchState::kFileNeedModKey:
                        break;
                    }
                }

                return result;
            }

        private:
            // Reads the names inside a directory using directory_iterator.
            // Returns the names, an error code (empty on success), and the
            // original OS-level message.  EINVAL from the iterator is
            // canonicalized to ENOTDIR so path resolution continues
            // traversing instead of aborting.
            //
            // Example:
            //   auto [names, ec, msg] = ReadDir("/src");
            //   ec == std::errc{}  =>  true on success
            //   names              =>  {"main.cpp", "util.cpp"}
            std::tuple<std::vector<std::string>, std::error_code, std::string> ReadDir(const std::string& dirname)
            {
                BeforeFileOpen();

                std::error_code          ec;
                std::filesystem::directory_iterator iterator(PathFromUTF8(dirname), ec);
                if (ec) {
                    if (ec == std::errc::invalid_argument) {
                        ec = std::make_error_code(std::errc::not_a_directory);
                    }
                    std::string message = ec.message();
                    AfterFileClose();
                    return {{}, ec, message};
                }

                std::vector<std::string>             names;
                std::filesystem::directory_iterator  end;
                for (; iterator != end; iterator.increment(ec)) {
                    if (ec) {
                        break;
                    }
                    names.push_back(PathToUTF8(iterator->path().filename()));
                }

                if (ec == std::errc::invalid_argument) {
                    ec = std::make_error_code(std::errc::not_a_directory);
                }

                std::string message = ec ? ec.message() : "";
                AfterFileClose();
                return {names, ec, message};
            }

            // Stores the file entries for directories we've listed before
            std::unordered_map<std::string, EntriesOrErr> entries_;
            std::mutex                                    entries_mutex_;

            // This stores data that will end up being returned by "GetWatchData()"
            std::optional<std::unordered_map<std::string, PrivateWatchData>> watch_data_;
            std::mutex                                                       watch_mutex_;

            GoFilepath fp_;

            // If true, do not use the "entries_" cache
            bool do_not_cache_;
        };

    } // namespace

    // Creates a real file system backed by the host OS.  "options" must
    // provide an absolute working directory; passing a relative path causes
    // this function to return nullptr and set "error".
    //
    // When "want_watch_data" is true, every ReadDirectory and ReadFile call
    // records the path into WatchData so GetWatchData() can later report the
    // full dependency set.
    //
    // The result is wrapped with a zip-aware overlay so that Yarn PnP
    // ".zip" archives are served transparently.
    //
    // Example:
    //   std::string error;
    //   auto fs = MakeRealFS({.abs_working_dir = "/project"}, error);
    //   fs != nullptr  =>  true on success
    std::unique_ptr<Fs> MakeRealFS(const RealFsOptions& options, std::string& error)
    {
        GoFilepath fp;
        if (CheckIfWindows()) {
            fp.is_windows     = true;
            fp.path_separator = '\\';
        } else {
            fp.is_windows     = false;
            fp.path_separator = '/';
        }

        // Come up with a default working directory if one was not specified
        fp.cwd = options.abs_working_dir;
        if (fp.cwd.empty()) {
            std::error_code ec;
            std::string     cwd = PathToUTF8(std::filesystem::current_path(ec));
            if (!ec) {
                fp.cwd = cwd;
            } else if (fp.is_windows) {
                fp.cwd = "C:\\";
            } else {
                fp.cwd = "/";
            }
        } else if (!fp.IsAbs(fp.cwd)) {
            error = "The working directory \"" + fp.cwd + "\" is not an absolute path";
            return nullptr;
        }

        // Resolve symlinks in the current working directory. Symlinks are resolved
        // when input file paths are converted to absolute paths because we need to
        // recognize an input file as unique even if it has multiple symlinks
        // pointing to it. The build will generate relative paths from the current
        // working directory to the absolute input file paths for error messages,
        // so the current working directory should be processed the same way. Not
        // doing this causes test failures with esbuild when run from inside a
        // symlinked directory.
        //
        // This deliberately ignores errors due to e.g. infinite loops. If there is
        // an error, we will just use the original working directory and likely
        // encounter an error later anyway. And if we don't encounter an error
        // later, then the current working directory didn't even matter and the
        // error is unimportant.
        if (auto resolved = fp.EvalSymlinks(fp.cwd)) {
            fp.cwd = *resolved;
        }

        auto real = std::make_unique<RealFS>(std::move(fp), options);
        // Wrap with ZipFS to support Yarn PnP ".zip" overlays, mirroring Go's RealFS.
        return MakeZipFS(std::move(real));
    }
}
