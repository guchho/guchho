// Unit tests for the FSCache defined in src/cache/cache_fs.cpp: the
// metadata-fingerprint file cache that avoids re-reading unchanged bytes from
// disk. FSCache::ReadFile consults filesystem::ModKey first and only touches
// fs::ReadFile when the fingerprint (or its trustworthiness) has changed, so
// the tests below script a fake Fs to prove both the "served straight from
// memory" path and every circumstance that forces a fresh read.

#include "test/guchho_test.hpp"

#include "guchho/cache.hpp"
#include "guchho/filesystem.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace cache = guchho::cache;
namespace filesystem = guchho::filesystem;

namespace {

// A minimal, scripted implementation of the Fs interface tuned for FSCache.
// Each path has its own contents and metadata fingerprint; the test mutates
// either between calls to simulate an edited file, an unreliable key, or a
// deleted file. ReadFile counts its own invocations so a test can prove the
// cached path never touched the file system.
class ScriptedFS : public filesystem::Fs {
public:
    // One scripted file. "mod_key_usable" is false to simulate a file whose
    // metadata cannot be trusted (the "unusable" state the cache treats as a
    // forced re-read); "exists" is false to simulate a deleted file.
    struct File {
        std::string contents;
        filesystem::ModKey mod_key;
        bool mod_key_usable = true;
        bool exists = true;
    };

    std::unordered_map<std::string, File> files;
    size_t read_calls = 0;

    // Stores or replaces the contents of "path" for subsequent reads.
    void SetContents(const std::string& path, std::string contents) {
        files[path].contents = std::move(contents);
    }

    // Stores or replaces the metadata fingerprint of "path".
    void SetModKey(const std::string& path, filesystem::ModKey key) {
        files[path].mod_key = key;
    }

    // Marks "path"'s metadata as usable or not. An unusable key means the
    // file exists but the cache must not trust it.
    void SetModKeyUsable(const std::string& path, bool usable) {
        files[path].mod_key_usable = usable;
    }

    // Deletes "path" so both ModKey and ReadFile fail for it.
    void RemoveFile(const std::string& path) {
        files.erase(path);
    }

    filesystem::ModKeyResult ModKey(const std::string& path) override {
        filesystem::ModKeyResult result;
        auto it = files.find(path);
        if (it == files.end() || !it->second.exists) {
            result.canonical_error = std::errc::no_such_file_or_directory;
            result.original_error = "no such file or directory";
            return result;
        }
        result.value = it->second.mod_key;
        if (!it->second.mod_key_usable) {
            result.unusable = true;
        }
        return result;
    }

    filesystem::FsResult<std::string> ReadFile(const std::string& path) override {
        ++read_calls;
        filesystem::FsResult<std::string> result;
        auto it = files.find(path);
        if (it == files.end() || !it->second.exists) {
            result.canonical_error = std::errc::no_such_file_or_directory;
            result.original_error = "no such file or directory";
            return result;
        }
        result.value = it->second.contents;
        return result;
    }

    // The remaining Fs methods are unused by FSCache and are stubbed.
    filesystem::FsResult<filesystem::DirEntries> ReadDirectory(const std::string&) override {
        return {};
    }
    filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> OpenFile(const std::string&) override {
        filesystem::FsResult<std::shared_ptr<filesystem::OpenedFile>> result;
        result.value = std::make_shared<filesystem::InMemoryOpenedFile>();
        return result;
    }
    bool IsAbs(std::string_view) override { return true; }
    std::optional<std::string> Abs(std::string_view) override { return std::nullopt; }
    std::string Dir(std::string_view) override { return "/"; }
    std::string Base(std::string_view) override { return ""; }
    std::string Ext(std::string_view) override { return ""; }
    std::string Join(std::initializer_list<std::string_view>) override { return ""; }
    std::string Cwd() override { return "/"; }
    std::optional<std::string> Rel(std::string_view, std::string_view) override { return std::nullopt; }
    std::optional<std::string> EvalSymlinks(std::string_view) override { return std::nullopt; }
    std::pair<std::string, filesystem::EntryKind> Kind(std::string_view dir, std::string_view) override {
        return {std::string(dir), filesystem::EntryKind::kFile};
    }
    filesystem::WatchData GetWatchData() override { return {}; }
};

// A stable metadata fingerprint for the tests below. Each field participates
// in the cache's equality check, so changing any one of them invalidates the
// key. Callers pass distinct arguments to distinguish two versions of a file.
filesystem::ModKey MakeKey(uint64_t inode, int64_t mtime_sec, int64_t size) {
    filesystem::ModKey key;
    key.inode = inode;
    key.mtime_sec = mtime_sec;
    key.size = size;
    key.mode = 0100644;
    key.uid = 1000;
    return key;
}

} // namespace

// ---------------------------------------------------------------------------
// Cached reads
// ---------------------------------------------------------------------------

TEST(CacheFs, UnchangedFileIsServedWithoutReadingBytes) {
    ScriptedFS fs;
    const std::string path = "/src/app.css";
    fs.SetContents(path, ".a { color: red; }");
    fs.SetModKey(path, MakeKey(11, 1000, 18));

    cache::FSCache container;

    filesystem::FsResult<std::string> first = container.ReadFile(fs, path);
    EXPECT_TRUE(first.Ok());
    EXPECT_EQ(first.value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 1u);
    EXPECT_FALSE(first.canonical_error.has_value());
    EXPECT_TRUE(first.original_error.empty());

    filesystem::FsResult<std::string> second = container.ReadFile(fs, path);
    EXPECT_TRUE(second.Ok());
    EXPECT_EQ(second.value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 1u);
}

TEST(CacheFs, UnchangedFileIsServedMatchingTheFirstReadExactly) {
    ScriptedFS fs;
    const std::string path = "/src/data.bin";
    fs.SetContents(path, std::string("\x00\x01\xff\xfe", 4));
    fs.SetModKey(path, MakeKey(5, 900, 4));

    cache::FSCache container;

    std::string first = container.ReadFile(fs, path).value;
    std::string second = container.ReadFile(fs, path).value;
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, std::string("\x00\x01\xff\xfe", 4));
}

TEST(CacheFs, ModifiedFileIsReadAgainAndUpdatesTheEntry) {
    ScriptedFS fs;
    const std::string path = "/src/app.css";
    fs.SetContents(path, ".a { color: red; }");
    fs.SetModKey(path, MakeKey(11, 1000, 18));

    cache::FSCache container;

    EXPECT_EQ(container.ReadFile(fs, path).value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 1u);

    // The file is rewritten: new bytes and new metadata. The changed key must
    // force a re-read that returns the fresh contents.
    fs.SetContents(path, ".b { color: blue; }");
    fs.SetModKey(path, MakeKey(11, 1010, 18));

    filesystem::FsResult<std::string> result = container.ReadFile(fs, path);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.value, ".b { color: blue; }");
    EXPECT_EQ(fs.read_calls, 2u);

    // The freshly stored entry is usable again, so the next call is cached.
    EXPECT_EQ(container.ReadFile(fs, path).value, ".b { color: blue; }");
    EXPECT_EQ(fs.read_calls, 2u);
}

TEST(CacheFs, SameMtimeButDifferentSizeStillTriggersAReread) {
    ScriptedFS fs;
    const std::string path = "/src/app.css";
    fs.SetContents(path, ".a { color: red; }");
    fs.SetModKey(path, MakeKey(11, 1000, 18));

    cache::FSCache container;

    EXPECT_EQ(container.ReadFile(fs, path).value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 1u);

    // Coarse file systems can report the same mtime for two writes. The
    // cache compares every ModKey field, so a size change alone is enough to
    // invalidate the key and force a re-read.
    fs.SetContents(path, ".a { color: red; } /* plus a comment */");
    fs.SetModKey(path, MakeKey(11, 1000, 32));

    filesystem::FsResult<std::string> result = container.ReadFile(fs, path);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(fs.read_calls, 2u);
}

// ---------------------------------------------------------------------------
// Untrustworthy metadata
// ---------------------------------------------------------------------------

TEST(CacheFs, UnusableKeyForcesAReadOnEveryCallUntilAUsableKeyIsStored) {
    ScriptedFS fs;
    const std::string path = "/src/app.css";
    fs.SetContents(path, ".a { color: red; }");
    fs.SetModKey(path, MakeKey(11, 1000, 18));
    fs.SetModKeyUsable(path, false);

    cache::FSCache container;

    // A fresh unusable key forces a read and is stored as unusable, so...
    EXPECT_EQ(container.ReadFile(fs, path).value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 1u);

    // ...the very next call is likewise forced to re-read even though a
    // previously read key exists.
    EXPECT_EQ(container.ReadFile(fs, path).value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 2u);

    // Once the key becomes usable again, one more read stores a trusted
    // entry, and calls after that are served from the cache.
    fs.SetModKeyUsable(path, true);
    EXPECT_EQ(container.ReadFile(fs, path).value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 3u);

    EXPECT_EQ(container.ReadFile(fs, path).value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 3u);
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

TEST(CacheFs, MissingFileReportsTheErrorWithoutPoisoningTheCache) {
    ScriptedFS fs;
    const std::string path = "/src/missing.css";

    cache::FSCache container;

    filesystem::FsResult<std::string> result = container.ReadFile(fs, path);
    EXPECT_FALSE(result.Ok());
    EXPECT_EQ(result.canonical_error, std::errc::no_such_file_or_directory);
    EXPECT_FALSE(result.original_error.empty());
    EXPECT_EQ(fs.read_calls, 1u);
}

TEST(CacheFs, ReadFailureLeavesThePreviousEntryIntact) {
    ScriptedFS fs;
    const std::string path = "/src/app.css";
    fs.SetContents(path, ".a { color: red; }");
    fs.SetModKey(path, MakeKey(11, 1000, 18));

    cache::FSCache container;

    EXPECT_EQ(container.ReadFile(fs, path).value, ".a { color: red; }");

    // The file is deleted: reading fails and must report the error rather
    // than serve the stale cached bytes from the previous read.
    fs.RemoveFile(path);
    filesystem::FsResult<std::string> result = container.ReadFile(fs, path);
    EXPECT_FALSE(result.Ok());
    EXPECT_EQ(result.canonical_error, std::errc::no_such_file_or_directory);

    // A failed read never touches the cache: the previous entry is left in
    // place, so restoring the exact metadata fingerprint the entry was stored
    // under is served from memory without another disk read.
    fs.SetContents(path, ".a { color: red; }");
    fs.SetModKey(path, MakeKey(11, 1000, 18));
    EXPECT_EQ(container.ReadFile(fs, path).value, ".a { color: red; }");
    EXPECT_EQ(fs.read_calls, 2u);
}

TEST(CacheFs, DifferentFilesHaveIndependentEntries) {
    ScriptedFS fs;
    fs.SetContents("/a.txt", "a");
    fs.SetModKey("/a.txt", MakeKey(1, 10, 1));
    fs.SetContents("/b.txt", "b");
    fs.SetModKey("/b.txt", MakeKey(2, 20, 1));

    cache::FSCache container;

    EXPECT_EQ(container.ReadFile(fs, "/a.txt").value, "a");
    EXPECT_EQ(container.ReadFile(fs, "/b.txt").value, "b");
    EXPECT_EQ(fs.read_calls, 2u);

    // Both entries are cached independently; neither path borrows the other's
    // bytes.
    fs.SetContents("/b.txt", "B");
    fs.SetModKey("/b.txt", MakeKey(2, 21, 1));
    EXPECT_EQ(container.ReadFile(fs, "/a.txt").value, "a");
    EXPECT_EQ(container.ReadFile(fs, "/b.txt").value, "B");
    EXPECT_EQ(fs.read_calls, 3u);
}

TEST(CacheFs, SameMetadataAcrossTwoPathsDoesNotCollide) {
    ScriptedFS fs;
    filesystem::ModKey key = MakeKey(7, 50, 3);
    fs.SetContents("/first.txt", "one");
    fs.SetModKey("/first.txt", key);
    fs.SetContents("/second.txt", "two");
    fs.SetModKey("/second.txt", key);

    cache::FSCache container;

    EXPECT_EQ(container.ReadFile(fs, "/first.txt").value, "one");
    EXPECT_EQ(container.ReadFile(fs, "/second.txt").value, "two");

    // Editing only the second file leaves the first served from its own entry.
    fs.SetContents("/second.txt", "TWO");
    fs.SetModKey("/second.txt", MakeKey(7, 51, 3));
    EXPECT_EQ(container.ReadFile(fs, "/first.txt").value, "one");
    EXPECT_EQ(container.ReadFile(fs, "/second.txt").value, "TWO");
}