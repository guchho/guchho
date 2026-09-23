#include "guchho/cache.hpp"

namespace guchho::cache {

    //------------------------------------------------------------------------------
    // CSSCache
    //
    // Caches the output of css::Parse keyed by the source's key path so a
    // file is parsed only once per build (or across builds) when neither its
    // text nor its parser options have changed. Diagnostics produced during a
    // parse are stored alongside the AST and replayed on every hit, keeping
    // the log output identical no matter how many times the file is queried.

    // Parses `source` into a css::AST, serving a cached result when the same
    // path was previously parsed with an identical source and equivalent
    // options.
    //
    // Behavior:
    //   1. A pointer to the stored entry for source.key_path, if any, is
    //      fetched under the mutex, then the lock is released so parsing and
    //      logging never contend on the cache.
    //   2. On a hit — entry exists, its stored source exactly matches, and
    //      its stored options compare equal to the request — every stored
    //      message is replayed through `log`, `was_cached` (if supplied) is
    //      set true, and the stored AST is returned.
    //   3. On a miss, parsing happens against a deferred log so diagnostics
    //      can be captured. The emitted messages are forwarded to `log`, a
    //      fresh entry is stored under the key path, `was_cached` is set
    //      false, and the freshly parsed AST is returned.
    //
    // Edge cases:
    //   - The cache is keyed by source.key_path only; two different sources
    //     sharing one key path would collide, so callers must supply stable
    //     key paths. The full source equality check on hits additionally
    //     guards against reusing an entry whose text drifted behind the same
    //     key.
    //   - Messages are deliberately not duplicated: they are emitted once
    //     from the entry and not re-logged on later reads.
    //
    //   Parse(log, source("app.css"), opts)       -> AST   (parsed now)
    //   Parse(log, source("app.css"), opts, &hit) -> AST   (replayed), hit == true
    css::AST CSSCache::Parse(logger::Log& log, const logger::Source& source,
                             const css::ParserOptions& options, bool* was_cached) {
        const CSSCacheEntry* entry;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(source.key_path);
            entry = it != entries_.end() ? &it->second : nullptr;
        }

        if (entry != nullptr && entry->source == source && entry->options == options) {
            for (const auto& msg : entry->msgs) {
                log.add_msg(msg);
            }
            if (was_cached != nullptr) *was_cached = true;
            return entry->ast;
        }

        logger::Log temp_log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll,
                                                   log.overrides);
        css::AST ast = css::Parse(temp_log, source, options);
        std::vector<logger::Msg> msgs = temp_log.done();
        for (const auto& msg : msgs) {
            log.add_msg(msg);
        }

        CSSCacheEntry new_entry;
        new_entry.source = source;
        new_entry.options = options;
        new_entry.ast = ast;
        new_entry.msgs = std::move(msgs);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.insert_or_assign(source.key_path, std::move(new_entry));
        }
        if (was_cached != nullptr) *was_cached = false;
        return ast;
    }

    //------------------------------------------------------------------------------
    // JSONCache
    //
    // Caches the output of the JSON parser keyed by the source's key path.
    // JSON files are parsed frequently and, while each parse is cheap, they
    // arrive in volume; caching collapses repeated parses of the same
    // unchanged file to a single map read plus a message replay.

    // Compares two JSON parser option sets member by member.
    //
    // Every option that influences parsing must agree before a cached parse
    // can be reused: the set of unsupported runtime features (which decides
    // how the "__proto__" key is handled), the flavor (plain JSON versus the
    // superset syntax), the diagnostic error suffix, and define mode. Any
    // mismatch invalidates the entry. Note this is a plain structural
    // comparison; it does not normalize values (e.g. two different suffixes
    // that render identically still compare unequal).
    //
    //   JsonOptionsEqual(a, b) -> true   (every field identical)
    //   JsonOptionsEqual(a, b) -> false  (at least one field differs)
    bool JsonOptionsEqual(const javascript::JSONOptions& a,
                          const javascript::JSONOptions& b) {
        return a.unsupported_js_features == b.unsupported_js_features &&
               a.flavor == b.flavor &&
               a.error_suffix == b.error_suffix &&
               a.is_for_define == b.is_for_define;
    }

    // Parses `source` as JSON, serving a cached expression when the same path
    // was previously parsed with an identical source and equivalent options.
    //
    // Behavior:
    //   1. A pointer to the stored JSONCacheEntry for source.key_path is
    //      fetched under the mutex and the lock is released before parsing.
    //   2. On a hit the stored messages are replayed through `log`,
    //      `was_cached` (if supplied) is set true, and the cached expression
    //      and success flag are returned.
    //   3. On a miss the parser runs against a deferred log so diagnostics
    //      are captured rather than printed immediately; the messages are
    //      forwarded to `log`, the outcome (expression plus success flag) is
    //      stored for next time, `was_cached` is set false, and the result is
    //      returned.
    //
    // Edge cases:
    //   - A failed parse is cached just like a successful one (ok == false is
    //     stored), so a broken file is not re-parsed on every request; the
    //     stored failure and its messages are replayed on the next hit.
    //   - Cogency depends on the caller supplying a key path that is stable
    //     for the file; the additional full-source equality check on hits
    //     prevents reuse after the text under a key path has changed.
    //
    //   Parse(log, source("data.json"), jopts)       -> {expr, true}   (parsed now)
    //   Parse(log, source("data.json"), jopts, &hit) -> {expr, true}   (replayed), hit == true
    std::pair<javascript::Expr, bool> JSONCache::Parse(logger::Log& log,
                                                       const logger::Source& source,
                                                       const javascript::JSONOptions& options,
                                                       bool* was_cached) {
        const JSONCacheEntry* entry;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(source.key_path);
            entry = it != entries_.end() ? &it->second : nullptr;
        }

        if (entry != nullptr && entry->source == source && JsonOptionsEqual(entry->options, options)) {
            for (const auto& msg : entry->msgs) {
                log.add_msg(msg);
            }
            if (was_cached != nullptr) *was_cached = true;
            return {entry->expr, entry->ok};
        }

        logger::Log temp_log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll,
                                                   log.overrides);
        auto [expr, ok] = javascript::Parser::ParseJSON(temp_log, source, options);
        std::vector<logger::Msg> msgs = temp_log.done();
        for (const auto& msg : msgs) {
            log.add_msg(msg);
        }

        JSONCacheEntry new_entry;
        new_entry.source = source;
        new_entry.options = options;
        new_entry.expr = expr;
        new_entry.ok = ok;
        new_entry.msgs = std::move(msgs);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.insert_or_assign(source.key_path, std::move(new_entry));
        }
        if (was_cached != nullptr) *was_cached = false;
        return {std::move(expr), ok};
    }

    //------------------------------------------------------------------------------
    // JSCache
    //
    // Caches the output of the JavaScript parser keyed by the source's key
    // path. Parsing a JavaScript module is by far the most expensive parse in
    // Guchho, so this cache is the biggest win: an unchanged file is reduced
    // to one lookup and a replay of its diagnostics.

    // Parses `source` into a javascript::AST, serving a cached result when
    // the same path was previously parsed with an identical source and
    // equivalent options.
    //
    // Behavior:
    //   1. A pointer to the stored JSCacheEntry for source.key_path is
    //      fetched under the mutex and the lock is released before parsing.
    //   2. On a hit the stored messages are replayed through `log`,
    //      `was_cached` (if supplied) is set true, and the cached AST and its
    //      success flag are returned.
    //   3. On a miss the parser runs against a deferred log so diagnostics
    //      are captured; the messages are forwarded to `log`, the outcome is
    //      stored, `was_cached` is set false, and the result is returned.
    //
    // Edge cases:
    //   - The options comparison uses javascript::Options::equal(), which
    //      accepts superficially different option objects that nevertheless
    //      produce identical parses (e.g. equivalent regular-expression
    //      profiles). This is intentional: it makes the hit ratio higher than
    //      a strict member-wise comparison would.
    //   - A parse failure is cached (ok == false) so the same broken file is
    //      not re-parsed relentlessly; the failure and its messages replay on
    //      the next request.
    //   - Hits must be treated as read-only: the stored AST is handed back by
    //      reference for reuse and is never mutated in place.
    //
    //   Parse(log, source("app.js"), jopts)       -> {ast, true}   (parsed now)
    //   Parse(log, source("app.js"), jopts, &hit) -> {ast, true}   (replayed), hit == true
    std::pair<javascript::AST, bool> JSCache::Parse(logger::Log& log,
                                                    const logger::Source& source,
                                                    const javascript::Options& options,
                                                    bool* was_cached) {
        const JSCacheEntry* entry;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(source.key_path);
            entry = it != entries_.end() ? &it->second : nullptr;
        }

        if (entry != nullptr && entry->source == source && entry->options.equal(options)) {
            for (const auto& msg : entry->msgs) {
                log.add_msg(msg);
            }
            if (was_cached != nullptr) *was_cached = true;
            return {entry->ast, entry->ok};
        }

        logger::Log temp_log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll,
                                                   log.overrides);
        auto [ast, ok] = javascript::Parse(temp_log, source, options);
        std::vector<logger::Msg> msgs = temp_log.done();
        for (const auto& msg : msgs) {
            log.add_msg(msg);
        }

        JSCacheEntry new_entry;
        new_entry.source = source;
        new_entry.options = options;
        new_entry.ast = ast;
        new_entry.ok = ok;
        new_entry.msgs = std::move(msgs);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.insert_or_assign(source.key_path, std::move(new_entry));
        }
        if (was_cached != nullptr) *was_cached = false;
        return {std::move(ast), ok};
    }

    //------------------------------------------------------------------------------
    // HtmlCache
    //
    // Caches the output of the HTML bridge keyed by the source's key path,
    // mirroring the CSS, JSON, and JS caches so the bundler treats every
    // language uniformly. Unlike those four, the HTML pipeline never fails
    // catastrophically, so the success flag is trivially true on every path.

    // Parses `source` through the HTML bridge, serving a cached AST when the
    // same path was previously parsed with an identical source and equivalent
    // options.
    //
    // Behavior:
    //   1. A pointer to the stored HtmlCacheEntry for source.key_path is
    //      fetched under the mutex and the lock is released before parsing.
    //   2. On a hit the stored messages are replayed through `log`,
    //      `was_cached` (if supplied) is set true, and a copy of the stored
    //      AST is returned with ok == true.
    //   3. On a miss the bridge runs against a deferred log, the messages are
    //      forwarded to `log`, a copy of the AST is stored, `was_cached` is
    //      set false, and a copy is returned.
    //
    // Important behavior: each call receives its own private copy of the AST.
    // The stored entry is never handed out directly, so a caller may mutate
    // the returned tree freely (rewriting resource URLs, moving nodes, ...)
    // without corrupting the cache, and a reuse on the next build still sees
    // the pristine original. Copying is not free, but it keeps the cache
    // safe against accidental caller mutation.
    //
    // Edge cases: the AST is stored before the caller receives its copy, and
    // the stored copy is the last thing touched, so even a caller that throws
    // away (or edits) its result does not disturb future hits.
    //
    //   Parse(log, source("page.html"), bopts)       -> {ast, true}   (parsed now)
    //   Parse(log, source("page.html"), bopts, &hit) -> {ast, true}   (replayed), hit == true
    std::pair<html::AST, bool> HtmlCache::Parse(logger::Log& log,
                                                const logger::Source& source,
                                                const html::BridgeOptions& options,
                                                bool* was_cached) {
        const HtmlCacheEntry* entry;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(source.key_path);
            entry = it != entries_.end() ? &it->second : nullptr;
        }

        if (entry != nullptr && entry->source == source && entry->options == options) {
            for (const auto& msg : entry->msgs) {
                log.add_msg(msg);
            }
            if (was_cached != nullptr) *was_cached = true;
            return {entry->ast, true};
        }

        logger::Log temp_log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll,
                                                   log.overrides);
        html::AST ast = html::Parse(temp_log, source, options);
        std::vector<logger::Msg> msgs = temp_log.done();
        for (const auto& msg : msgs) {
            log.add_msg(msg);
        }

        HtmlCacheEntry new_entry;
        new_entry.ast = ast;
        new_entry.options = options;
        new_entry.source = source;
        new_entry.msgs = std::move(msgs);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.insert_or_assign(source.key_path, std::move(new_entry));
        }
        if (was_cached != nullptr) *was_cached = false;
        return {std::move(ast), true};
    }

} // namespace guchho::cache