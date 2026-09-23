#include "guchho/cache.hpp"

namespace guchho::cache {

    // Reads the entire file at `path`, serving cached bytes when the file's
    // metadata fingerprint proves its contents have not changed since the
    // previous read.
    //
    // Behavior:
    //   1. A copy of the cached entry for `path`, if any, is taken under the
    //      mutex and the lock is released before touching the file system, so
    //      the potentially slow ModKey and ReadFile calls never run while
    //      holding the cache lock.
    //   2. The file's current filesystem::ModKey is requested. If a cached
    //      entry exists, its recorded key is usable (is_mod_key_usable), the
    //      new key is trustworthy, and the two keys are equal, the cached
    //      contents are returned without a read — the whole point of this
    //      cache is that metadata inspection is cheaper than reading bytes.
    //   3. Otherwise the file is read from disk and stored alongside the
    //      metadata fingerprint just obtained.
    //   4. A read failure propagates the canonical and original error without
    //      touching the cache, so an existing stale entry for the path is left
    //      in place rather than being replaced by an empty result.
    //
    // Edge cases:
    //   - The "unusable" state is honored on both sides: a cached entry whose
    //     key was unusable is never trusted, and a fresh key that is unusable
    //     forces a re-read and is stored (with is_mod_key_usable = false) so
    //     the next call is also forced to re-read. This is what guarantees a
    //     file modified twice within the mtime safety window is never served
    //     stale bytes.
    //   - The comparison is exact (size, mtime, inode, mode, owner all must
    //     match), so a file with the same mtime but a different size is still
    //     re-read.
    //
    //   ReadFile(fs, "/src/app.css")                  -> FsResult{Ok(), value = "…bytes…"} (read from disk)
    //   ReadFile(fs, "/src/app.css")                  -> same bytes from cache, no disk read (key unchanged)
    //   ReadFile(fs, "/src/missing.css")              -> FsResult{Ok() = false, canonical_error = ENOENT}
    filesystem::FsResult<std::string> FSCache::ReadFile(filesystem::Fs& fs,
                                                        const std::string& path) {
        std::optional<FSEntry> entry;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(path);
            if (it != entries_.end()) {
                entry = it->second;
            }
        }

        filesystem::ModKeyResult mod_key_result = fs.ModKey(path);
        if (entry.has_value() && entry->is_mod_key_usable &&
            mod_key_result.Ok() && entry->mod_key == mod_key_result.value) {
            return {entry->contents, {}, {}};
        }

        filesystem::FsResult<std::string> result = fs.ReadFile(path);
        if (!result.Ok()) {
            return {{}, result.canonical_error, result.original_error};
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            FSEntry new_entry;
            new_entry.contents = result.value;
            new_entry.mod_key = mod_key_result.value;
            new_entry.is_mod_key_usable = mod_key_result.Ok();
            entries_.insert_or_assign(path, std::move(new_entry));
        }
        return result;
    }

} // namespace guchho::cache