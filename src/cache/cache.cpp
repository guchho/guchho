#include "guchho/cache.hpp"

namespace guchho::cache {

    // Hashes a SourceIndexKey by combining the hash of its resolved path with
    // an encoding of its kind.
    //
    // logger::PathHash already spreads the path reliably; the kind is folded in
    // via a small shift so that a given path can occupy two slots (kNormal and
    // kJSStubForCSS) without the two keys ever mapping to the same bucket for
    // the same path under common hash distributions. Matching pairs must hash
    // identically for the lookup to succeed — that is guaranteed because the
    // hash is derived purely from the key's two fields, which are compared
    // member-wise by operator==.
    //
    //   key{.path = "/src/site.css", .kind = kNormal}       -> some hash value H
    //   key{.path = "/src/site.css", .kind = kJSStubForCSS} -> some hash value H'
    //   (H and H' may coincide, but the == test then disambiguates them)
    size_t SourceIndexCache::SourceIndexKeyHash::operator()(const SourceIndexKey& key) const {
        size_t h = logger::PathHash{}(key.path);
        return h ^ (static_cast<size_t>(static_cast<uint8_t>(key.kind)) << 2);
    }

    // Reports the number of source-index slots currently assigned.
    //
    // This is exactly the size of the (path, kind) entry table and does not
    // include glob-assigned slots, which live in a separate table. The value
    // is guarded by the mutex because worker threads can be minting indices
    // concurrently. Callers use it to reserve space in index-addressed
    // per-source arrays before populating them.
    //
    //   (three files assigned so far)  ->  LenHint()  == 3
    uint32_t SourceIndexCache::LenHint() {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<uint32_t>(entries_.size());
    }

    // Returns the stable source index for `path` seen through `kind`,
    // assigning a fresh one on the pair's first encounter.
    //
    // Behavior:
    //   - If the (path, kind) pair is already in the table, its existing index
    //     is returned and the counters are untouched.
    //   - Otherwise the current value of next_source_index_ is taken, the
    //     counter is advanced, and the pair is recorded so future lookups
    //     reuse the same index.
    //
    // The lookups always return a small, dense, monotonically increasing
    // integer starting at 1 (index 0 is reserved for the shared runtime
    // module). Because kind is part of the key, one path can be registered
    // twice: once as a kNormal file and once as the generated kJSStubForCSS
    // module it expands into. Path-equivalence masking (e.g. two spellings
    // that resolve to one file) is not performed here; callers must deliver a
    // normalized path for reuse to be effective.
    //
    //   Get("/src/site.css", kNormal)        -> 1
    //   Get("/src/site.css", kNormal)        -> 1   (reused, no new slot)
    //   Get("/src/site.css", kJSStubForCSS)  -> 2
    //   Get("/src/site.js",  kNormal)        -> 3
    uint32_t SourceIndexCache::Get(const logger::Path& path, SourceIndexKind kind) {
        std::lock_guard<std::mutex> lock(mutex_);

        SourceIndexKey key;
        key.path = path;
        key.kind = kind;

        auto it = entries_.find(key);
        if (it != entries_.end()) {
            return it->second;
        }

        uint32_t index = next_source_index_;
        next_source_index_++;
        entries_.emplace(std::move(key), index);
        return index;
    }

    // Returns a dedicated source index for one match of a glob expansion,
    // keyed by the importing site and the match's position in the results.
    //
    // Behavior:
    //   - The 32-bit parent source index (the site that performed the import)
    //     and the 32-bit glob position are packed into a single 64-bit key so
    //     one unordered_map serves every (parent, match) combination.
    //   - An existing key returns its stored index; a new key consumes the
    //     shared next_source_index_ counter and is recorded for reuse.
    //
    // Each distinct (site, match position) pair always yields its own slot,
    // even when two matches expand to the same on-disk file. This isolation is
    // what keeps glob-produced modules separate in per-source-index state.
    // Indices drawn here share the same monotonic counter as Get(), so they
    // never collide with file-assigned indices. Note that LenHint() does not
    // count these slots, since they belong to the separate glob table.
    //
    //   GetGlob(3 /* site that did the import */, 0 /* first match */)  -> 4
    //   GetGlob(3 /* site that did the import */, 0 /* first match */)  -> 4  (reused)
    //   GetGlob(3 /* site that did the import */, 1 /* second match */) -> 5
    uint32_t SourceIndexCache::GetGlob(uint32_t parent_source_index, uint32_t glob_index) {
        std::lock_guard<std::mutex> lock(mutex_);

        uint64_t key = (static_cast<uint64_t>(parent_source_index) << 32) |
                       static_cast<uint64_t>(glob_index);

        auto it = glob_entries_.find(key);
        if (it != glob_entries_.end()) {
            return it->second;
        }

        uint32_t index = next_source_index_;
        next_source_index_++;
        glob_entries_.emplace(key, index);
        return index;
    }

    // Constructs a fresh CacheSet holding every cache object on the heap.
    //
    // The set contains caches whose members own std::mutex instances, which
    // makes the aggregate non-copyable and non-movable under the standard
    // library. Default-constructing the set inside a make_unique is therefore
    // the only safe way to obtain one: the object is created directly in its
    // final heap location and can never be relocated. A caller should create a
    // single set per build and let it accumulate reused entries.
    //
    //   MakeCacheSet() -> unique_ptr<CacheSet> holding fs_cache, css_cache,
    //                     json_cache, js_cache, html_cache, source_index_cache
    std::unique_ptr<CacheSet> MakeCacheSet() {
        return std::make_unique<CacheSet>();
    }

} // namespace guchho::cache