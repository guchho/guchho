#include "guchho/filesystem.hpp"
#include "guchho/helpers.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>

#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "guchho/miniz.hpp"

namespace guchho::filesystem {

    namespace {

        std::string ToLower(const std::string& s)
        {
            return helpers::ToLowerASCII(s);
        }

        std::string ReplaceBackslashWithSlash(std::string s)
        {
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        }

        // Represents a directory inside a zip archive.  Each directory is
        // stored with its original-case path and a map of child entries
        // (base name => EntryKind).  The DirEntries snapshot is computed
        // lazily on the first ReadDirectory call and cached for subsequent
        // lookups.  The mutex guards concurrent access to the cache.
        struct CompressedDir {
            std::unordered_map<std::string, EntryKind> entries;
            std::string path;
            std::mutex mutex;
            std::optional<DirEntries> dirEntries;
        };

        // Represents a file inside a zip archive.  The file index is the
        // ordinal position within the archive (used by miniz for extraction).
        // Contents are read lazily on the first ReadFile call and cached.
        // The "error" field stores any extraction failure so subsequent reads
        // can return it without re-attempting.
        struct CompressedFile {
            int file_index = -1;
            std::string filename;
            std::mutex mutex;
            std::string contents;
            std::string error;
            bool wasRead = false;
        };

        // Represents an opened zip archive.  Directories and files are
        // indexed by lower-cased paths for case-insensitive lookup.  The
        // condition variable allows multiple concurrent readers to wait
        // until the initial archive scan completes.
        struct ZipFile {
            std::mutex mutex;
            std::condition_variable cv;
            bool ready = false;
            bool has_error = false;
            std::string error_msg;
            std::string zipPath;

            std::unordered_map<std::string, std::shared_ptr<CompressedDir>> dirs;
            std::unordered_map<std::string, std::shared_ptr<CompressedFile>> files;
        };

        // Performs the initial scan of a zip archive at "zipPath".  Iterates
        // every entry, splitting each filename into directory and base
        // components, and populates the archive's dirs and files maps.
        // Parent directories that are not explicitly stored in the archive
        // are synthesized so that ReadDirectory can return complete listings.
        //
        // Example:
        //   Archive contains: ["src/main.cpp", "src/util.cpp"]
        //   After scan: dirs["src"] = {entries: {"main.cpp": kFile, "util.cpp": kFile}}
        void TryToReadZipArchive(const std::string& zipPath, std::shared_ptr<ZipFile> archive)
        {
            mz_zip_archive zip_archive{};
            if (!mz_zip_reader_init_file(&zip_archive, zipPath.c_str(), 0)) {
                archive->has_error = true;
                mz_zip_error err = mz_zip_get_last_error(&zip_archive);
                const char* msg = mz_zip_get_error_string(err);
                archive->error_msg = msg ? msg : "failed to open zip";
                mz_zip_reader_end(&zip_archive);
                return;
            }

            mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
            std::vector<std::string> seeds;
            seeds.reserve(num_files);

            for (mz_uint i = 0; i < num_files; i++) {
                mz_zip_archive_file_stat stat;
                if (!mz_zip_reader_file_stat(&zip_archive, i, &stat)) {
                    continue;
                }
                std::string fileName(stat.m_filename);
                bool isDir = stat.m_is_directory != 0;
                std::string baseName = fileName;
                if (!baseName.empty() && baseName.back() == '/') {
                    baseName.pop_back();
                }
                std::string dirPath;
                std::string base;
                size_t slash = baseName.find_last_of('/');
                if (slash != std::string::npos) {
                    dirPath = baseName.substr(0, slash);
                    base = baseName.substr(slash + 1);
                } else {
                    dirPath = "";
                    base = baseName;
                }

                if (isDir) {
                    std::string lowerDir = ToLower(dirPath);
                    auto it = archive->dirs.find(lowerDir);
                    if (it == archive->dirs.end()) {
                        auto dir = std::make_shared<CompressedDir>();
                        dir->path = dirPath;
                        archive->dirs.emplace(lowerDir, dir);
                        archive->dirs.emplace(lowerDir + "/", dir);
                        seeds.push_back(lowerDir);
                    }
                } else {
                    std::string lowerName = ToLower(fileName);
                    auto file = std::make_shared<CompressedFile>();
                    file->file_index = int(i);
                    file->filename = fileName;
                    archive->files.emplace(lowerName, std::move(file));

                    std::string lowerDir = ToLower(dirPath);
                    auto it = archive->dirs.find(lowerDir);
                    std::shared_ptr<CompressedDir> dir;
                    if (it == archive->dirs.end()) {
                        dir = std::make_shared<CompressedDir>();
                        dir->path = dirPath;
                        archive->dirs.emplace(lowerDir, dir);
                        archive->dirs.emplace(lowerDir + "/", dir);
                        seeds.push_back(lowerDir);
                    } else {
                        dir = it->second;
                    }
                    dir->entries[base] = EntryKind::kFile;
                }
            }

            // Synthesize parent directories.  For each directory seed
            // (e.g. "a/b/c"), walk upward and create entries for "a/b",
            // "a", etc. so that ReadDirectory("a") returns "b" as a child.
            for (const std::string& seed : seeds) {
                std::string baseName = seed;
                while (!baseName.empty()) {
                    std::string dirPath;
                    std::string base;
                    size_t slash = baseName.find_last_of('/');
                    if (slash != std::string::npos) {
                        dirPath = baseName.substr(0, slash);
                        base = baseName.substr(slash + 1);
                    } else {
                        dirPath = "";
                        base = baseName;
                    }
                    std::string lowerDir = ToLower(dirPath);
                    auto it = archive->dirs.find(lowerDir);
                    std::shared_ptr<CompressedDir> dir;
                    if (it == archive->dirs.end()) {
                        dir = std::make_shared<CompressedDir>();
                        dir->path = dirPath;
                        archive->dirs.emplace(lowerDir, dir);
                        archive->dirs.emplace(lowerDir + "/", dir);
                    } else {
                        dir = it->second;
                    }
                    dir->entries[base] = EntryKind::kDir;
                    baseName = dirPath;
                }
            }

            mz_zip_reader_end(&zip_archive);
        }

        // ---------------------------------------------------------------------------
        // ZipFS implementation
        // ---------------------------------------------------------------------------

        // File system overlay that transparently serves files from zip
        // archives.  When a path contains ".zip/" (or is a directory ending
        // in ".zip"), the overlay intercepts the operation and serves the
        // entry from the archive instead of the inner file system.  All
        // other operations delegate to the wrapped inner Fs.
        //
        // This enables Yarn PnP imports like
        //   "import foo from 'pkg/dist/index.js'"
        // to read from inside a zip archive without the caller knowing.
        class ZipFS : public Fs {
        public:
            explicit ZipFS(std::unique_ptr<Fs> inner)
                : inner_(std::move(inner))
            {
            }

            // Reads a directory listing.  First attempts the inner FS; if
            // that fails with a "not found" / "not a directory" error, checks
            // whether the path points inside a zip archive and serves the
            // listing from there.
            //
            // Example:
            //   ReadDirectory("node_modules/pkg.zip/dist")  =>  DirEntries from archive
            FsResult<DirEntries> ReadDirectory(const std::string& path_in) override
            {
                std::string path = MangleYarnPnPVirtualPath(path_in);
                FsResult<DirEntries> result = inner_->ReadDirectory(path);

                if (result.canonical_error != std::errc::no_such_file_or_directory &&
                    result.canonical_error != std::errc::not_a_directory &&
                    result.canonical_error != std::errc::invalid_argument) {
                    return result;
                }

                auto zipAndTail = CheckForZip(path, EntryKind::kDir);
                if (!zipAndTail.first) {
                    return result;
                }
                auto archive = zipAndTail.first;
                const std::string& pathTail = zipAndTail.second;

                std::string lowerTail = ToLower(pathTail);
                auto it = archive->dirs.find(lowerTail);
                if (it == archive->dirs.end()) {
                    FsResult<DirEntries> err;
                    err.canonical_error = std::errc::no_such_file_or_directory;
                    err.original_error = "no such file or directory";
                    return err;
                }
                auto dir = it->second;
                {
                    std::lock_guard<std::mutex> lock(dir->mutex);
                    if (dir->dirEntries.has_value() && dir->dirEntries->data.has_value()) {
                        FsResult<DirEntries> ok;
                        ok.value = *dir->dirEntries;
                        return ok;
                    }
                    DirEntries entries;
                    entries.dir = path;
                    entries.data = std::map<std::string, std::shared_ptr<Entry>>{};
                    for (auto& kv : dir->entries) {
                        const std::string& name = kv.first;
                        EntryKind kind = kv.second;
                        auto entry = std::make_shared<Entry>();
                        entry->dir = path;
                        entry->base = name;
                        entry->kind = kind;
                        entry->need_stat = false;
                        entries.data->emplace(ToLower(name), std::move(entry));
                    }
                    dir->dirEntries = entries;
                    FsResult<DirEntries> ok;
                    ok.value = entries;
                    return ok;
                }
            }

            // Reads a file's contents.  First attempts the inner FS; if that
            // returns "not found", checks whether the path points inside a
            // zip archive and extracts the file using miniz.  Extracted
            // contents are cached on the CompressedFile so subsequent reads
            // avoid re-extraction.
            //
            // Example:
            //   ReadFile("node_modules/pkg.zip/dist/index.js")  =>  file contents
            FsResult<std::string> ReadFile(const std::string& path_in) override
            {
                std::string path = MangleYarnPnPVirtualPath(path_in);
                FsResult<std::string> result = inner_->ReadFile(path);
                if (result.canonical_error != std::errc::no_such_file_or_directory) {
                    return result;
                }

                auto zipAndTail = CheckForZip(path, EntryKind::kFile);
                if (!zipAndTail.first) {
                    return result;
                }
                auto archive = zipAndTail.first;
                const std::string& pathTail = zipAndTail.second;

                std::string lowerTail = ToLower(pathTail);
                auto it = archive->files.find(lowerTail);
                if (it == archive->files.end()) {
                    FsResult<std::string> err;
                    err.canonical_error = std::errc::no_such_file_or_directory;
                    err.original_error = "no such file or directory";
                    return err;
                }
                auto file = it->second;
                {
                    std::lock_guard<std::mutex> lock(file->mutex);
                    if (file->wasRead) {
                        FsResult<std::string> res;
                        if (!file->error.empty()) {
                            res.canonical_error = std::errc::io_error;
                            res.original_error = file->error;
                        } else {
                            res.value = file->contents;
                        }
                        return res;
                    }
                    file->wasRead = true;

                    const std::string& zipPath = archive->zipPath;
                    if (zipPath.empty()) {
                        file->error = "zip path not found";
                        FsResult<std::string> err;
                        err.canonical_error = std::errc::io_error;
                        err.original_error = file->error;
                        return err;
                    }

                    mz_zip_archive za{};
                    if (!mz_zip_reader_init_file(&za, zipPath.c_str(), 0)) {
                        file->error = "failed to open zip for reading";
                        FsResult<std::string> err;
                        err.canonical_error = std::errc::io_error;
                        err.original_error = file->error;
                        mz_zip_reader_end(&za);
                        return err;
                    }

                    size_t uncomp_size = 0;
                    void* p = mz_zip_reader_extract_file_to_heap(&za, file->filename.c_str(), &uncomp_size, 0);
                    if (!p) {
                        mz_zip_error err = mz_zip_get_last_error(&za);
                        const char* msg = mz_zip_get_error_string(err);
                        file->error = msg ? msg : "failed to extract file";
                        mz_zip_reader_end(&za);
                        FsResult<std::string> errRes;
                        errRes.canonical_error = std::errc::io_error;
                        errRes.original_error = file->error;
                        return errRes;
                    }
                    file->contents.assign(static_cast<char*>(p), uncomp_size);
                    mz_free(p);
                    mz_zip_reader_end(&za);

                    FsResult<std::string> ok;
                    ok.value = file->contents;
                    return ok;
                }
            }

            // Opens a file for random access.  Currently delegates to the
            // inner FS since zip entries are not suitable for random access.
            FsResult<std::shared_ptr<OpenedFile>> OpenFile(const std::string& path_in) override
            {
                std::string path = MangleYarnPnPVirtualPath(path_in);
                return inner_->OpenFile(path);
            }

            // Computes a metadata fingerprint.  Delegates to the inner FS;
            // zip entries do not have meaningful stat metadata.
            ModKeyResult ModKey(const std::string& path_in) override
            {
                std::string path = MangleYarnPnPVirtualPath(path_in);
                return inner_->ModKey(path);
            }

            bool IsAbs(std::string_view p) override { return inner_->IsAbs(p); }
            std::optional<std::string> Abs(std::string_view p) override { return inner_->Abs(p); }

            // Returns the directory portion of a virtual path.  If the path
            // is a Yarn PnP virtual path with an empty suffix, the prefix is
            // returned directly.
            //
            // Example:
            //   Dir("node_modules/pkg/__virtual__/0/dist")  =>  "node_modules/pkg"
            std::string Dir(std::string_view path) override
            {
                std::string s(path);
                if (auto parsed = ParseYarnPnPVirtualPath(s)) {
                    if (parsed->suffix.empty()) {
                        return parsed->prefix;
                    }
                }
                return inner_->Dir(path);
            }

            std::string Base(std::string_view p) override { return inner_->Base(p); }
            std::string Ext(std::string_view p) override { return inner_->Ext(p); }
            std::string Join(std::initializer_list<std::string_view> parts) override { return inner_->Join(parts); }
            std::string Cwd() override { return inner_->Cwd(); }
            std::optional<std::string> Rel(std::string_view base, std::string_view target) override { return inner_->Rel(base, target); }
            std::optional<std::string> EvalSymlinks(std::string_view path) override { return inner_->EvalSymlinks(path); }

            std::pair<std::string, EntryKind> Kind(std::string_view dir, std::string_view base) override
            {
                return inner_->Kind(dir, base);
            }

            WatchData GetWatchData() override { return inner_->GetWatchData(); }

        private:
            // Checks whether "path" points inside a zip archive.  Returns the
            // archive object and the path tail (everything after ".zip/"), or
            // {nullptr, ""} if the path does not contain a zip segment.
            //
            // Example:
            //   CheckForZip("node_modules/pkg.zip/dist/index.js", kFile)
            //   =>  {archive, "dist/index.js"}
            std::pair<std::shared_ptr<ZipFile>, std::string> CheckForZip(const std::string& path, EntryKind kind)
            {
                std::string normalized = ReplaceBackslashWithSlash(path);
                std::string zipPath;
                std::string pathTail;

                size_t idx = normalized.find(".zip/");
                if (idx != std::string::npos) {
                    zipPath = normalized.substr(0, idx + 4);
                    pathTail = normalized.substr(idx + 5);
                } else if (kind == EntryKind::kDir && normalized.size() >= 4 &&
                           normalized.compare(normalized.size() - 4, 4, ".zip") == 0) {
                    zipPath = normalized;
                    pathTail = "";
                } else {
                    return {nullptr, ""};
                }

                std::shared_ptr<ZipFile> archive;
                {
                    std::unique_lock<std::mutex> lock(zipFilesMutex_);
                    auto it = zipFiles_.find(zipPath);
                    if (it != zipFiles_.end()) {
                        archive = it->second;
                        lock.unlock();
                        std::unique_lock<std::mutex> alock(archive->mutex);
                        archive->cv.wait(alock, [&] { return archive->ready; });
                    } else {
                        archive = std::make_shared<ZipFile>();
                        archive->zipPath = zipPath;
                        zipFiles_[zipPath] = archive;
                        lock.unlock();

                        TryToReadZipArchive(zipPath, archive);

                        {
                            std::lock_guard<std::mutex> alock(archive->mutex);
                            archive->ready = true;
                        }
                        archive->cv.notify_all();
                    }
                }

                if (archive->has_error) {
                    return {nullptr, ""};
                }
                return {archive, pathTail};
            }

            std::unique_ptr<Fs> inner_;
            std::mutex zipFilesMutex_;
            std::unordered_map<std::string, std::shared_ptr<ZipFile>> zipFiles_;
        };

    } // namespace

    // Creates a ZipFS overlay wrapping "inner".  The returned Fs
    // transparently serves files from ".zip" archives encountered during
    // path resolution, enabling Yarn PnP imports to work without the
    // caller knowing about the archive format.
    //
    // Example:
    //   auto fs = MakeZipFS(MakeRealFS(options, error));
    //   fs->ReadFile("node_modules/pkg.zip/dist/index.js")  =>  file contents
    std::unique_ptr<Fs> MakeZipFS(std::unique_ptr<Fs> inner)
    {
        return std::make_unique<ZipFS>(std::move(inner));
    }

} // namespace guchho::filesystem
