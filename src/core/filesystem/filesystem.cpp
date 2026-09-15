#include "guchho/filesystem.hpp"
#include "guchho/helpers.hpp"

#include <algorithm>
#include <cstdlib>
#include <semaphore>

namespace guchho::filesystem {

    namespace {

        // A counting semaphore that caps the number of simultaneously open file
        // descriptors. Each call to BeforeFileOpen() decrements the semaphore
        // (blocking if zero), and each call to AfterFileClose() increments it.
        // This prevents hitting the OS ulimit on systems that allow only a
        // limited number of open files.
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
}
