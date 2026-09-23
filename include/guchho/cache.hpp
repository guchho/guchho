#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "guchho/logger.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/css/css_ast.hpp"
#include "guchho/css/css_parser.hpp"
#include "guchho/html/html_bridge.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_parser.hpp"


namespace guchho::cache {

    // Classifies what role a file is being indexed under. kNormal indexes a
    // real, user-authored file. kJSStubForCSS marks the synthetic module that
    // Guchho fabricates when a JavaScript file imports a CSS file directly:
    // the CSS is compiled into a small generated module that exports its
    // rules, and this kind guarantees that the same path on disk can hold two
    // independent source identities (its genuine CSS form and the generated
    // stub) without ever colliding.
    enum class SourceIndexKind : uint8_t {
        kNormal,
        kJSStubForCSS,
    };

    //------------------------------------------------------------------------------
    // SourceIndexCache
    //
    // Gives every file that participates in a build a stable, dense, numeric
    // identity (its "source index") so that per-file state can be stored in
    // flat, index-addressed arrays. Assignment is monotonic: the first
    // distinct (path, kind) pair handed to Get() receives index 1, the next
    // index 2, and so on. Because a file can be seen through more than one
    // role, the kind is part of the lookup key, so a single path can
    // legitimately occupy two slots.
    //
    // The cache is shared across the workers of a parallel build, so every
    // assignment and lookup is serialized behind one mutex. Lookups for an
    // already-known pair are cheap map reads; the cost of hashing the path is
    // paid only on a pair's first encounter.

    class SourceIndexCache {
    public:
        SourceIndexCache()
            : next_source_index_(kInitialSourceIndex + 1) {}

        // Reports how many source-index slots currently exist, padded with a
        // small margin of headroom.
        //
        // Callers use this number to size per-source containers (arrays
        // indexed by source index) before they begin populating them. The
        // padding reserves a couple of extra slots so that a new file
        // discovered partway through the build does not force the container
        // to reallocate after it was sized from this hint.
        //
        //   (12 entries assigned so far)  ->  LenHint()  ~14   (actual count + margin)
        uint32_t LenHint();

        // Returns the stable source index for `path` seen through `kind`.
        //
        // The first time a (path, kind) pair is seen, a fresh, increasing
        // index is minted and remembered; every later call with the same pair
        // returns that original index unchanged. It is the pair that is
        // remembered, so the same on-disk path queried once as kNormal and
        // once as kJSStubForCSS yields two different indices. Equivalent
        // spellings of a path (e.g. different relative/absolute forms) are
        // considered distinct unless the caller normalizes them first.
        //
        //   Get("/src/site.css", kNormal)        -> 1
        //   Get("/src/site.css", kNormal)        -> 1   (reused)
        //   Get("/src/site.css", kJSStubForCSS)  -> 2   (different slot)
        //   Get("/src/site.js",  kNormal)        -> 3
        uint32_t Get(const logger::Path& path, SourceIndexKind kind);

        // Returns a dedicated source index for a single file matched by a
        // glob expansion, identified by the index of the import site
        // (`parent_source_index`) and the position of the match within that
        // glob's results (`glob_index`).
        //
        // A glob can match many files, and distinct glob expressions can
        // pull in the same on-disk file. Giving every (parent, glob position)
        // pair its own slot guarantees that each matched module gets a unique
        // identity even when several matches resolve to identical paths,
        // keeping glob-expanded modules isolated from one another in the
        // per-source-index state.
        //
        //   GetGlob(3 /* site that performed the import */, 0 /* first match */)  -> 4
        //   GetGlob(3 /* site that performed the import */, 1 /* second match */) -> 5
        uint32_t GetGlob(uint32_t parent_source_index, uint32_t glob_index);

    private:
        // Index 0 is permanently reserved as the slot of the shared runtime
        // helper module that Guchho synthesizes internally. It is never
        // handed out by Get(), so the first caller-submitted file starts at
        // index 1.
        static constexpr uint32_t kInitialSourceIndex = 0;

        // Identity key for one source-index slot. Both the resolved path and
        // the role it is being queried as must agree for two lookups to be
        // treated as the same file.
        struct SourceIndexKey {
            logger::Path path;
            SourceIndexKind kind{};

            bool operator==(const SourceIndexKey&) const = default;
        };

        // Hashes a SourceIndexKey from its full path and kind so that
        // distinct (path, kind) pairs are never conflated.
        struct SourceIndexKeyHash {
            size_t operator()(const SourceIndexKey& key) const;
        };

        std::unordered_map<uint64_t, uint32_t> glob_entries_;
        std::unordered_map<SourceIndexKey, uint32_t, SourceIndexKeyHash> entries_;
        std::mutex mutex_;
        uint32_t next_source_index_;
    };

    //------------------------------------------------------------------------------
    // FSCache
    //
    // Avoids re-reading file bytes from disk across builds. Instead of
    // comparing contents, this cache fingerprints a file's metadata (size,
    // modification time, inode, mode, owner) through filesystem::ModKey.
    // Inspecting metadata is far cheaper than reading the file's bytes, so an
    // unchanged file can be served straight from the in-memory copy.
    //
    // Trust is not unconditional. The file-system layer can report a key as
    // "unusable" when the modification time is zero or falls inside the mtime
    // safety window, because coarse-resolution file systems cannot always
    // distinguish two writes made close together. In that situation the cache
    // refuses to serve the cached bytes and forces a fresh read rather than
    // risk returning stale content.

    // One cached file. `contents` holds the raw bytes from the most recent
    // successful read, `mod_key` the metadata fingerprint those bytes were
    // validated against, and `is_mod_key_usable` records whether that
    // fingerprint was reliable enough to act on (false forces a re-read).
    struct FSEntry {
        std::string contents;
        filesystem::ModKey mod_key;
        bool is_mod_key_usable = false;
    };

    class FSCache {
    public:
        // Reads the entire file at `path`, serving cached bytes when the
        // file's metadata proves it has not changed since the last read.
        //
        // The first sight reads the file, stores its bytes and their metadata
        // fingerprint, and returns the result. On later calls the current
        // ModKey is compared with the stored one: identical, usable keys
        // return the cached bytes without touching the file's contents; a
        // changed key, an unusable key, or a missing file triggers a fresh
        // read that replaces the cached entry. A missing file is reported
        // through the FsResult error fields, not by serving stale data.
        //
        //   ReadFile(fs, "/src/app.css")               -> FsResult{Ok(), value = "…bytes…"}
        //   ReadFile(fs, "/src/app.css") /* unchanged */-> same bytes from cache, no disk read
        filesystem::FsResult<std::string> ReadFile(filesystem::Fs& fs,
                                                   const std::string& path);

    private:
        std::unordered_map<std::string, FSEntry> entries_;
        std::mutex mutex_;
    };

    //------------------------------------------------------------------------------
    // CSSCache
    //
    // Caches the result of the CSS parser per source file so that a file is
    // never re-parsed between builds when neither its text nor its parser
    // options have changed. The cache key is the resolved path of the source,
    // and an entry is only reused when the options of the current request are
    // equivalent to the ones the entry was built with.

    // One cached CSS parse: the AST produced, the diagnostics recorded along
    // the way, the source the AST was built from, and the parser options it
    // was built with.
    struct CSSCacheEntry {
        logger::Source source;
        std::vector<logger::Msg> msgs;
        css::AST ast;
        css::ParserOptions options;
    };

    class CSSCache {
    public:
        // Parses `source` into a css::AST, reusing a cached result when the
        // same path has already been parsed with equivalent options.
        //
        // A hit returns the stored AST and replays the stored diagnostic
        // messages through `log`, so warnings from the original parse are
        // reported on every request rather than silently swallowed. A miss
        // parses once, stores the result and its messages, and forwards both
        // to the caller. `was_cached`, when non-null, is set to true for a
        // hit and false otherwise so callers can distinguish the two cases.
        //
        //   Parse(log, source("app.css"), opts)       -> AST   (parsed now)
        //   Parse(log, source("app.css"), opts, &hit) -> AST   (cached), hit == true
        css::AST Parse(logger::Log& log, const logger::Source& source,
                       const css::ParserOptions& options,
                       bool* was_cached = nullptr);

    private:
        std::unordered_map<logger::Path, CSSCacheEntry, logger::PathHash> entries_;
        std::mutex mutex_;
    };

    //------------------------------------------------------------------------------
    // JSONCache
    //
    // Caches the result of the JSON parser per source file, mirroring the
    // structure of the CSS and JS caches. Because JSONOptions cannot be
    // compared with a plain operator==, cache hits rely on the
    // JsonOptionsEqual helper to decide whether a stored entry is reusable.

    // One cached JSON parse: the parsed expression, the diagnostics, the
    // source, the JSON options, and whether the parse succeeded.
    struct JSONCacheEntry {
        javascript::Expr expr;
        std::vector<logger::Msg> msgs;
        logger::Source source;
        javascript::JSONOptions options;
        bool ok = false;
    };

    // Compares two sets of JSON parser options field by field.
    //
    // Every member that influences parsing — the set of unsupported runtime
    // features, the flavor (plain JSON versus the superset syntax), the error
    // suffix appended to diagnostics, and define mode — must agree for a
    // cached AST to be safely reused. Any difference invalidates the entry.
    //
    //   JsonOptionsEqual(a, b) -> true   (every field identical)
    //   JsonOptionsEqual(a, b) -> false  (at least one field differs)
    bool JsonOptionsEqual(const javascript::JSONOptions& a,
                          const javascript::JSONOptions& b);

    class JSONCache {
    public:
        // Parses `source` as JSON, reusing a cached expression when the same
        // path has already been parsed with equivalent options.
        //
        // The returned pair holds the parsed expression and whether parsing
        // succeeded. A hit replays the stored diagnostics through `log` and
        // returns the cached expression and success flag; a miss parses once
        // and stores the outcome. A failed parse is still cached, so a later
        // identical request does not repeat the same work. `was_cached`, when
        // non-null, reports hit versus miss.
        //
        //   Parse(log, source("data.json"), jopts)       -> {expr, true}   (parsed now)
        //   Parse(log, source("data.json"), jopts, &hit) -> {expr, true}   (cached), hit == true
        std::pair<javascript::Expr, bool> Parse(logger::Log& log,
                                                const logger::Source& source,
                                                const javascript::JSONOptions& options,
                                                bool* was_cached = nullptr);

    private:
        std::unordered_map<logger::Path, JSONCacheEntry, logger::PathHash> entries_;
        std::mutex mutex_;
    };

    //------------------------------------------------------------------------------
    // JSCache
    //
    // Caches the result of the JavaScript parser per source file, in the same
    // shape as the CSS and JSON caches. The cache key is the resolved path of
    // the source; an entry is reused only when the supplied options compare
    // equal to those it was built with.

    // One cached JS parse: the source, the diagnostics, the parser options,
    // the resulting AST, and whether parsing succeeded.
    struct JSCacheEntry {
        logger::Source source;
        std::vector<logger::Msg> msgs;
        javascript::Options options;
        javascript::AST ast;
        bool ok = false;
    };

    class JSCache {
    public:
        // Parses `source` into a javascript::AST, reusing a cached result
        // when the same path has already been parsed with equivalent options.
        //
        // The returned pair holds the AST and whether parsing succeeded. A
        // hit replays the stored diagnostics through `log` and returns the
        // cached AST and success flag; a miss parses once and stores the
        // outcome — success or failure — for the next request. `was_cached`,
        // when non-null, reports hit versus miss.
        //
        //   Parse(log, source("app.js"), jopts)       -> {ast, true}   (parsed now)
        //   Parse(log, source("app.js"), jopts, &hit) -> {ast, true}   (cached), hit == true
        std::pair<javascript::AST, bool> Parse(logger::Log& log,
                                               const logger::Source& source,
                                               const javascript::Options& options,
                                               bool* was_cached = nullptr);

    private:
        std::unordered_map<logger::Path, JSCacheEntry, logger::PathHash> entries_;
        std::mutex mutex_;
    };

    //------------------------------------------------------------------------------
    // HtmlCache
    //
    // Caches the output of the HTML bridge per source file, reusing the same
    // shape as the CSS, JSON, and JS caches. The HTML pipeline is a Guchho
    // extension: it offers a capability beyond those three language caches,
    // and this cache keeps it consistent with their interface so the bundler
    // can treat all four uniformly.

    // One cached HTML parse: the source, the diagnostics, the resulting AST,
    // and the bridge options it was built with.
    struct HtmlCacheEntry {
        logger::Source source;
        std::vector<logger::Msg> msgs;
        html::AST ast;
        html::BridgeOptions options;
    };

    class HtmlCache {
    public:
        // Parses `source` through the HTML bridge, reusing a cached AST when
        // the same path has already been parsed with equivalent options.
        //
        // The HTML pipeline never fails catastrophically, so the second half
        // of the returned pair is always true; it exists solely to keep this
        // signature in lock-step with the JS and JSON caches. The returned
        // AST is a fresh, mutable copy private to the caller: hits copy the
        // stored entry, misses parse once, then store a copy and hand their
        // result out. That ownership separation matters because html::AST
        // holds pointers into its own tree that must be re-established every
        // time an entry leaves the cache. As with the other caches, a hit
        // replays the stored diagnostics through `log`, and `was_cached`,
        // when non-null, reports hit versus miss.
        //
        //   Parse(log, source("page.html"), bopts)       -> {ast, true}   (parsed now)
        //   Parse(log, source("page.html"), bopts, &hit) -> {ast, true}   (cached), hit == true
        std::pair<html::AST, bool> Parse(logger::Log& log,
                                         const logger::Source& source,
                                         const html::BridgeOptions& options,
                                         bool* was_cached = nullptr);

    private:
        std::unordered_map<logger::Path, HtmlCacheEntry, logger::PathHash> entries_;
        std::mutex mutex_;
    };

    //------------------------------------------------------------------------------
    // CacheSet
    //
    // One aggregated object holding every cache the bundler consults during a
    // build: the raw file contents, the four language AST caches (CSS, JSON,
    // JS, HTML), and the source-index allocator.
    //
    // Each member owns a mutex, which makes the set non-copyable and
    // non-movable. It is therefore created through MakeCacheSet() and reached
    // through an owning unique_ptr instead of being embedded or passed around
    // by value.

    struct CacheSet {
        FSCache fs_cache;
        CSSCache css_cache;
        JSONCache json_cache;
        JSCache js_cache;
        HtmlCache html_cache;
        SourceIndexCache source_index_cache;
    };

    // Creates a freshly constructed CacheSet on the heap.
    //
    // The individual caches contain mutexes and cannot be copied or moved, so
    // callers receive an owning unique_ptr. One CacheSet is expected to live
    // for the lifetime of a build so entries accumulate across repeated file
    // reads and are available to reuse on later builds.
    //
    //   MakeCacheSet() -> unique_ptr<CacheSet> wrapping one of every cache
    std::unique_ptr<CacheSet> MakeCacheSet();

} // namespace guchho::cache