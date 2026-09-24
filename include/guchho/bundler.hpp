#pragma once

#include <algorithm>
#include <any>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "guchho/cache.hpp"
#include "guchho/graph.hpp"
#include "guchho/linker.hpp"
#include "guchho/logger.hpp"
#include "guchho/config.hpp"
#include "guchho/helpers.hpp"
#include "guchho/resolver.hpp"
#include "guchho/sourcemap.hpp"
#include "guchho/filesystem.hpp"

#include "guchho/html/html_ast.hpp"
#include "guchho/html/html_bridge.hpp"


// ---------------------------------------------------------------------------
// Public interface of the Guchho bundler: the scan phase that discovers and
// parses every input reachable from the entry points, the helpers that turn
// those inputs into stable output names, the diagnostics emitted when an
// import cannot be resolved, and the HTML output pipeline that rewrites each
// HTML entry so its references point at the chunks the build produces.
// ---------------------------------------------------------------------------
namespace guchho::bundler {

    using filesystem::Fs;
    using logger::Log;
    using logger::Msg;
    using logger::MsgData;
    using logger::Path;
    using logger::PrettyPaths;
    using logger::Range;
    using logger::Source;


    // One input file as recorded during the scan phase of a Guchho bundle.
    // Besides the parsed representation carried inside "input_file", each
    // entry holds the JSON metadata chunk that will later be concatenated
    // into the build's metafile, plus an opaque "plugin_data" slot that lets
    // plugins attach per-file state which survives from scanning through to
    // linking.
    struct ScannerFile {

        // Serialized metadata for this file (path, loader, size, chunk
        // membership) awaiting concatenation into the final metafile JSON.
        std::string json_metadata_chunk;

        // Free slot owned by plugins: whatever they stash here during
        // scanning is handed back to them for the same file later on.
        std::any plugin_data;
        // The file itself: source text, key path, parsed representation, and
        // the source index every other structure uses to refer to it.
        graph::InputFile input_file;
    };


    // Everything the link phase needs in order to emit source maps for one
    // input file once its chunks exist. Both fields are computed up front so
    // the linking loop never has to redo per-character work while it is
    // writing output.
    struct DataForSourceMap {
        // Byte offsets stored on AST nodes are translated through these
        // tables into the UTF-16 column numbers that source maps require, so
        // reported positions stay correct on lines containing multi-byte
        // characters.
        std::vector<sourcemap::LineOffsetTable> line_offset_tables;

        // The original file text, already JSON-quoted, ready to be appended
        // to the "sourcesContent" array of a source map. Quoting is done once
        // here because repeating the escape pass for every chunk that
        // references this file would be wasteful.
        std::vector<std::string> quoted_contents;
    };

    // Lazily invoked by the link phase to materialize DataForSourceMap
    // entries for each source file, deferring the line-offset and quoting
    // work until a source map is actually requested.
    using DataForSourceMapsFn = std::function<std::vector<DataForSourceMap>()>;


    // The complete state of one Guchho bundling operation: the scanned input
    // files, the entry points that anchor the module graph, the options that
    // steer the build, and the shared filesystem/resolver services every
    // phase consults. A Bundle is produced by ScanBundle and consumed by
    // Compile; nothing else should mutate it.
    struct Bundle {

        // Random prefix shared by every symbol this bundle generates, so two
        // bundles living on the same page can never collide.
        std::string unique_key_prefix;

        filesystem::Fs* fs{};
        std::shared_ptr<resolver::Resolver> res;
        // Every input file discovered while scanning, kept in an order that
        // is stable for the lifetime of the bundle.
        std::vector<ScannerFile> files;
        std::vector<graph::EntryPoint> entry_points;
        // For each HTML entry point (keyed by its source index), the virtual
        // JS/CSS entry points synthesized from its inline <script> and
        // <style> content, so inline code is scanned and linked exactly like
        // a file-based entry.
        std::map<uint32_t, graph::HtmlInlineInfo> html_inline;
        config::Options options;

        // Runs the link phase over the scanned files and produces the final
        // build artifacts.
        //
        //   input : the logger, a timer, and a mangle_cache that caches
        //           minified identifier mappings across chunks
        //   output: a pair whose first element lists every file to write
        //           (chunk bodies, stylesheets, source maps, copied assets)
        //           and whose second element is the metafile JSON describing
        //           the build for consuming tools
        //
        // Example: a bundle with entry_points = {"src/app.js"} where app.js
        // imports "./style.css" returns output files such as
        // {"app-4fd3k2a8.js", "app-4fd3k2a8.css"} plus a metafile_json string
        // mapping each output back to the input files it contains.
        std::pair<std::vector<graph::OutputFile>, std::string> Compile(
            logger::Log& log,
            helpers::Timer& timer,
            std::unordered_map<std::string, bool>& mangle_cache);
    };


    // Performs the scan phase of a Guchho bundle: applies option defaults,
    // runs plugin "onStart" hooks, resolves and parses every file reachable
    // from the given entry points (following imports, HTML references, and
    // inline script/style content), and gathers the results into a Bundle.
    //
    //   input : the API call context, logger, filesystem, caches, the entry
    //           point list, the caller's options, and an optional timer
    //   output: a fully populated Bundle (files, entry points, resolver,
    //           options) ready to hand to Bundle::Compile
    //
    // Example: entry_points = {"src/main.js"} where main.js imports
    // "util.js" yields a Bundle whose files contain both of those files plus
    // the runtime support file the build always injects.
    Bundle ScanBundle(
        config::APICall call,
        logger::Log& log,
        filesystem::Fs& fs,
        cache::CacheSet& caches,
        const std::vector<config::EntryPoint>& entry_points,
        config::Options options,
        helpers::Timer* timer);


    // Fills every option the caller left unset with the value Guchho would
    // choose on its own (platform, output format, target environment,
    // splitting and minify flags, outbase, and so on) while leaving anything
    // the caller explicitly provided untouched. ScanBundle calls this before
    // any other work.
    //
    //   input : Options where some fields are still at their zero/unset state
    //   output: the same Options object with those fields replaced by the
    //           documented defaults, e.g. an unset outbase becomes the
    //           common ancestor directory of all entry points
    void ApplyOptionDefaults(config::Options& options);


    // Returns the built-in mapping from file extension to the loader Guchho
    // applies when the caller has not overridden it for that extension. The
    // empty extension maps to the JS loader so extensionless imports still
    // parse.
    //
    //   input : nothing
    //   output: a map where e.g. map[".css"] == Loader::kCSS,
    //           map[".jsx"] == Loader::kJSX, and map[""] == Loader::kJS
    std::unordered_map<std::string, config::Loader> DefaultExtensionToLoaderMap();


    // Turns raw content-hash bytes into the short token Guchho embeds in
    // generated file names, keeping them unique per content while staying
    // short enough to read. The token is the first 8 characters of the
    // standard base32 encoding of the supplied bytes.
    //
    //   input : the leading bytes of a chunk's content digest (8 bytes)
    //   output: an 8-character token such as "4fd3k2a8", which is used to
    //           build names like "app-4fd3k2a8.js"
    std::string HashForFileName(std::span<const uint8_t> hash_bytes);


    // Works out where an input file sits relative to the build's output base
    // directory, which is what determines the name it is emitted under. The
    // first element of the returned pair is the normalized directory prefix:
    // it always begins with "/", always uses forward slashes, and never
    // escapes the output base (leading "../" segments are rewritten to
    // "_.._/"). The second element is the base name with its extension
    // stripped, unless a custom output path was supplied, in which case the
    // extension is kept. When "avoid_index" is true, a file literally named
    // "index" is replaced by its parent directory name so many chunks do not
    // all collapse onto the same emitted name.
    //
    //   input : the input file (or a custom output path), options,
    //           filesystem, and the avoid_index flag
    //   output: a {dir, base} pair; for "./src/app.js" with outbase "./src"
    //           this is {"/", "app"}, so the chunk is written as
    //           "app-<hash>.js"
    std::pair<std::string, std::string> PathRelativeToOutbase(
        const graph::InputFile& input_file,
        const config::Options& options,
        filesystem::Fs& fs,
        bool avoid_index,
        const std::string& custom_file_path);


    // Walks the import graph outward from every entry point and returns the
    // indices (into "files") of all input files some entry point can reach,
    // in a stable order. Files that are neither entry points nor imported by
    // anything reachable are left out; this is how Guchho decides which
    // inputs belong in per-entry-point output and which can be dropped.
    //
    //   input : the full file table and the entry point list
    //   output: e.g. with entry_points = {"a.js"} where a.js imports
    //           "b.js" and c.js is referenced by nobody, the result holds
    //           the indices of a.js and b.js only
    std::vector<uint32_t> FindReachableFiles(
        const std::vector<graph::InputFile>& files,
        const std::vector<graph::EntryPoint>& entry_points);


    // Outcome of running the "onResolve" hooks for a single import attempt.
    // A hook may claim the import outright (resolve_result engaged), decline
    // so resolution continues normally (resolve_result empty and
    // did_log_error false), or fail the build (did_log_error true).
    // debug_meta records which plugins took part so the resolver can explain
    // how the final path was decided.
    struct OnResolveOutcome {
        std::optional<resolver::ResolveResult> resolve_result;
        bool did_log_error{};
        resolver::DebugMeta debug_meta{};
    };

    // The three pre-assembled pieces of a "could not resolve" diagnostic,
    // built ahead of the logging call so the failure path can report them
    // without repeating path arithmetic: the headline message, an actionable
    // suggestion (such as a similar-looking path that does exist), and
    // structured notes carrying file and line context.
    struct ResolveFailureInfo {
        std::string text;
        std::string suggestion;
        std::vector<logger::MsgData> notes;
    };


    // Invokes each configured plugin's "onResolve" hook in order for one
    // import attempt, handing every hook the importer file, the raw import
    // string, its source range, import attributes, the import kind, the
    // absolute resolve directory, and the per-file plugin data slot. Hooks
    // are asked in sequence and the first one to claim the import ends the
    // walk.
    //
    //   input : plugin list, resolver, log, filesystem, filesystem cache, the
    //           import's source location and path range, the importer path,
    //           the import path (e.g. "./util"), its attributes and kind, the
    //           absolute resolve directory, plugin data, and log path style
    //   output: an OnResolveOutcome; a hook returning path "/abs/util.js"
    //           yields an engaged resolve_result holding that path, while a
    //           hook that throws sets did_log_error = true
    OnResolveOutcome RunOnResolvePlugins(
        const std::vector<config::Plugin>& plugins,
        resolver::Resolver* res,
        logger::Log log,
        filesystem::Fs& fs,
        cache::FSCache& fs_cache,
        const logger::Source* import_source,
        logger::Range import_path_range,
        const logger::Path& importer,
        const std::string& path,
        const logger::ImportAttributes& import_attributes,
        compiler::ImportKind kind,
        const std::string& abs_resolve_dir,
        const std::any& plugin_data,
        logger::PathStyle log_path_style);

    // Assembles the human-readable parts of an unresolvable-import error:
    // a headline naming the import and the file that requested it, a
    // suggestion when Guchho can infer a plausible fix (a nearby file with a
    // similar name, a missing extension, an import that only fails on the
    // current platform), and supporting notes with file and line context.
    //
    //   input : the resolver, the unresolved import path (e.g. "./utils"),
    //           its import kind, the plugin that last touched it, the
    //           filesystem, the absolute resolve directory, the target
    //           platform, pretty-path helpers, the possibly rewritten import
    //           path, and the log path style
    //   output: ResolveFailureInfo whose text reads like
    //           'Could not resolve "./utils" from "src/app.js"', accompanied
    //           by the matching suggestion and notes
    ResolveFailureInfo ResolveFailureErrorTextSuggestionNotes(
        resolver::Resolver& res,
        std::string path,
        compiler::ImportKind kind,
        const std::string& plugin_name,
        filesystem::Fs& fs,
        const std::string& abs_resolve_dir,
        config::Platform platform,
        const logger::PrettyPaths& originating_file_paths,
        const std::string& modified_import_path,
        logger::PathStyle log_path_style);




// ---------------------------------------------------------------------------
// HTML output support
//
// Guchho treats HTML entry points as first-class build inputs: their inline
// and external scripts and styles are scanned and bundled, and the document
// is rewritten afterwards so every reference points at the chunks the build
// produced. The structures and Pass* functions below implement that
// rewriting; GenerateHTMLOutput runs them in their required order.
// ---------------------------------------------------------------------------

// Per <script> element bookkeeping used when an HTML entry is emitted,
// filled during linking and consumed by the rewriting and head-link passes.
struct HTMLScriptInfo {
    // Output path of the chunk produced for this <script>, relative to the
    // output directory (e.g. "assets/app-Df8k9s.js"). Empty when the script
    // was inlined or produced no file of its own.
    std::string output_rel_path;

    // Output paths of the chunks this script statically imports, ordered so
    // that <link rel="modulepreload"> tags generated from them appear in a
    // useful preloading sequence.
    std::vector<std::string> dependency_preloads;

    // When the JS chunk for this script also yielded a stylesheet (because
    // it imported a ".css" file), the output path of that CSS chunk; empty
    // when no companion stylesheet was emitted.
    std::string associated_css_rel_path;

    // True when the script is a module (<script type="module">), which makes
    // Guchho emit crossorigin attributes and modulepreload links for it.
    bool is_module{};
};


// One stylesheet destined for an HTML entry: where it will be written
// relative to the output directory, and its full text. The text is kept
// here so later passes (font resource hints, CSP nonce injection, the CSS
// loading strategy) can inspect the CSS without re-reading the file.
struct HTMLCssContent {

    std::string css_output_rel_path;


    std::string contents;
};


// The read-mostly packet of everything a Pass* function needs in order to
// rewrite one HTML entry: pointers into the scanned document and its inline
// entry metadata, maps from source index to emitted chunk text and output
// path, the serialization and URL options, and the out-parameters through
// which passes report side effects. Everything is a pointer or a small
// value so the large tables are never copied between passes.
struct HTMLOutputContext {

    // The HTML input file currently being emitted; null when a pass does not
    // need the source.
    const graph::InputFile* html_file = nullptr;

    // Inline <script>/<style> entry metadata recorded during scanning for
    // this document, consulted when bundled inline runs are injected.
    const graph::HtmlInlineInfo* inline_info = nullptr;


    // Source index of a chunk to the final text of that chunk, used to place
    // bundled code and CSS back into inline elements.
    const std::unordered_map<uint32_t, std::string>* chunk_texts = nullptr;


    // Source index of an input file to the output-relative path of the chunk
    // written for it; this is the table PassResourceRewrite uses to repair
    // href, src, and srcset values.
    const std::unordered_map<uint32_t, std::string>* record_source_to_rel_path =
        nullptr;


    // Output-relative path of this HTML file itself (e.g. "index.html"),
    // used to express chunk URLs relative to the document.
    std::string html_output_rel_path;


    // Per-import-record details for each <script> in the document, indexed
    // like the AST's import records (see HTMLScriptInfo).
    const std::vector<HTMLScriptInfo>* script_info = nullptr;


    // Serialization modes for the HTML printer.
    bool pretty_print = false;

    bool minify = false;


    // Configured public path or CDN prefix; when set, generated asset URLs
    // are absolute under it instead of relative to the document.
    std::string public_path;


    // Identifier to replacement text consumed by PassEnvSubstitution.
    const std::unordered_map<std::string, std::string>* define_map = nullptr;

    // Nonce value stamped into the document by PassCspNonce; empty disables
    // that pass entirely.
    std::string csp_nonce;


    // Configuration for the preconnect, dns-prefetch, and font hint links
    // inserted by PassResourceHints.
    config::ResourceHintsConfig resource_hints;

    // Output-relative path plus text of every stylesheet associated with
    // this document, scanned for @font-face sources and other pass-level
    // needs.
    const std::vector<HTMLCssContent>* css_contents = nullptr;


    // Subresource-integrity hash algorithm to apply; empty disables
    // integrity attributes.
    std::string sri_algorithm;


    // Output-relative path to the emitted bytes of that file, used to
    // compute integrity hashes without re-reading anything from disk.
    const std::unordered_map<std::string, std::string>* sri_rel_contents =
        nullptr;

    // Whether local stylesheet links should be rewritten to the non-blocking
    // loading pattern by PassCSSLoadingStrategy.
    config::CSSLoadingStrategy css_loading = config::CSSLoadingStrategy::kBlocking;


    // Plugin list handed to PassApplyTransforms so transformIndexHtml hooks
    // can run against the serialized document.
    const std::vector<config::Plugin>* plugins = nullptr;

    // Out-flag set to true when PassImportMapReorder actually moved a map.
    bool* import_maps_reordered = nullptr;


    // Out-parameter receiving the byte range of the first import map that
    // was moved, for later diagnostics.
    logger::Range* import_map_reordered_range = nullptr;

    // Sink for warnings raised while plugin HTML transforms run.
    std::vector<std::string>* transform_warnings = nullptr;
};


// Replaces %KEY% placeholders throughout the document (text nodes and
// attribute values) with their values from ctx.define_map. <script> and
// <style> bodies are left alone because they travel through their own
// pipelines, and unrecognized %...% sequences are passed through untouched.
//   input : the HTML AST plus a context whose define_map holds e.g.
//           {"NODE_ENV": "\"production\""}
//   output: every occurrence of %NODE_ENV% in ordinary markup becomes
//           "production", while %UNKNOWN% stays exactly as it was
void PassEnvSubstitution(html::AST& ast, const HTMLOutputContext& ctx);

// Rewrites every bundled resource reference in the document — href, src,
// srcset candidates, and style url() occurrences — to the final URL of its
// emitted chunk, applying the public path when one is configured, adding
// crossorigin to module scripts, and attaching integrity/crossorigin
// attributes when SRI is enabled. External origins it encounters are
// collected so PassResourceHints can emit hints for them later.
//   input : the HTML AST plus a context carrying the chunk path table, per-
//           script info, public path, and SRI table, and an optional vector
//           that accumulates external origins
//   output: e.g. <script src="./app.js"> becomes
//           <script src="./assets/app-Df8k9s.js" crossorigin>, and the
//           origin of a third-party URL is appended to *external_origins
void PassResourceRewrite(html::AST& ast, const HTMLOutputContext& ctx,
                         std::vector<std::string>* external_origins);

// Injects the bundled bodies of inline <script>/<style> runs back into the
// document: each run's chunk text (HTML-escaped) is written into the run's
// first member and the remaining members are emptied. Module scripts never
// joined a run, so they are left untouched.
//   input : a context whose inline_info describes the runs of this document
//           and whose chunk_texts maps each chunk index to its final code or
//           CSS text
//   output: a run of three sibling <script> tags becomes one carrying the
//           bundled code followed by two emptied members
void PassInlineRuns(const HTMLOutputContext& ctx);

// Prepends <link rel="modulepreload"> and <link rel="stylesheet"> tags to
// <head> (creating <head> if the document lacks one) for each module script
// and its statically imported chunks, deduplicating URLs and honoring the
// configured public path. Scripts that are not modules are skipped.
//   input : the HTML AST plus a context whose script_info holds entries such
//           as {output_rel_path: "assets/app.js", dependency_preloads:
//           ["assets/util.js"], is_module: true}
//   output: a <head> that now begins with
//           <link rel="modulepreload" href="./assets/util.js"> followed by
//           the script's own chunk and any companion stylesheet link
void PassInjectHeadLinks(html::AST& ast, const HTMLOutputContext& ctx);

// Prepends resource-hint links to <head>: preconnect and dns-prefetch for
// every external origin discovered while rewriting, plus preload or prefetch
// links for local font files referenced by @font-face rules in this
// document's stylesheets. Nothing is emitted when resource hints are
// disabled.
//   input : the HTML AST, a context carrying the resource-hints config and
//           the stylesheet texts, and the external origins list built by
//           PassResourceRewrite
//   output: an origin such as "https://cdn.example.com" gains
//           <link rel="preconnect" href="https://cdn.example.com">, and a
//           font served from "./fonts/A.woff2" gains a font preload link
void PassResourceHints(html::AST& ast, const HTMLOutputContext& ctx,
                       const std::vector<std::string>& external_origins);

                       // Applies the configured CSS loading strategy. In the non-blocking mode
                       // every local stylesheet link becomes the preload-and-upgrade pattern
                       // (rel="preload" as="style" onload="this.rel='stylesheet'") with a
                       // <noscript> fallback link carrying the original attributes inserted
                       // right after it; any other strategy leaves the document alone.
                       //   input : the HTML AST plus a context whose css_loading is
                       //           kNonBlocking
                       //   output: <link rel="stylesheet" href="./app.css"> becomes that
                       //           preload link followed by
                       //           <noscript><link rel="stylesheet" href="./app.css"></noscript>
                       void PassCSSLoadingStrategy(html::AST& ast, const HTMLOutputContext& ctx);

// Moves any <script type="importmap"> that appears after the first module
// script or modulepreload link to just before it, preserving the relative
// order of the maps themselves, so speculative module fetching always knows
// the map before it starts fetching. Returns true when at least one map was
// moved, and records the byte range of the first moved map when the context
// supplies somewhere to put it.
//   input : the HTML AST plus a context for a document where an import map
//           follows a <script type="module">
//   output: the map now precedes that script, the function returns true, and
//           *ctx.import_map_reordered_range covers the moved start tag
bool PassImportMapReorder(html::AST& ast, const HTMLOutputContext& ctx);

// Removes every "guchho-ignore" marker attribute from the document tree,
// descending into <template> contents but never into <script>/<style>
// bodies. The marker guides the earlier passes, so this one runs last and
// its attribute is never part of the emitted document.
//   input : an HTML AST whose elements may carry guchho-ignore markers
//   output: the same tree with no guchho-ignore attribute anywhere in it
void PassStripGuchhoIgnore(html::AST& ast, const HTMLOutputContext& ctx);

// Stamps ctx.csp_nonce into the document: inserts a
// <meta property="csp-nonce" nonce="VALUE"> as the first child of <head>
// and sets nonce="VALUE" on every <script>, <style>, and CSP-relevant
// <link> that does not already have one. Existing user-provided nonce
// attributes are preserved and no policy header is generated; an empty
// nonce does nothing.
//   input : the HTML AST plus a context with csp_nonce = "abc123"
//   output: the meta tag appears in <head> and each eligible element carries
//           nonce="abc123"
void PassCspNonce(html::AST& ast, const HTMLOutputContext& ctx);

// Runs each plugin's transformIndexHtml hook over the serialized document in
// plugin order. A hook may return a replacement HTML string or a list of tag
// descriptors to inject at a named location; exceptions thrown by a hook are
// caught and reported as warnings instead of failing the build.
//   input : the serialized HTML text plus a context carrying the plugin list
//           and the filename used in transform contexts
//   output: the transformed HTML; a hook returning "<!-- injected -->"
//           yields that text, and a descriptor for
//           {tag: "script", inject_to: "head"} splices the tag into <head>
std::string PassApplyTransforms(std::string html, const HTMLOutputContext& ctx);

// Runs the full HTML output pipeline for one HTML entry, calling each Pass*
// function in the order the pipeline requires: environment substitution,
// resource rewriting, inline-run injection, head-link injection, resource
// hints, the CSS loading strategy, import-map reordering, marker stripping,
// CSP nonce injection, serialization, and finally the plugin HTML
// transforms.
//   input : a fully populated HTMLOutputContext for one HTML entry
//   output: the final HTML text for that entry — the source document with
//           script and link URLs pointing at the emitted chunks, preload
//           links prepended to <head>, and transformIndexHtml results
//           applied; when the file has no parsed representation, its original
//           contents are returned unchanged
std::string GenerateHTMLOutput(const HTMLOutputContext& context);




    ////////////////////////////////////////////////////////////////////////////////
    // Internal bundler machinery
    //
    // The scanner plumbing that used to live in the single-file
    // bundler.cpp. Since that unit is now split across several
    // translation units, the shared state is hosted here (mirroring
    // the linker, which hosts its private LinkerContext in the
    // corresponding public header).
    using filesystem::Fs;

    using logger::Log;

    using logger::Msg;

    using logger::MsgData;

    using logger::Path;

    using logger::PrettyPaths;

    using logger::Range;

    using logger::Source;


    ////////////////////////////////////////////////////////////////////////////////
    // Channels ("chan parseResult" / "chan config.InjectedFile" in Go)

    // Go's unbuffered channels become an unbounded blocking queue. Producers
    // never block, so a build that is cancelled can simply drain whatever has
    // been produced so far without deadlocking any producer threads.
    template <typename T>
    class Channel {
    public:
        void Send(T value) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push_back(std::move(value));
            }
            cv_.notify_one();
        }

        T Recv() {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty(); });
            T value = std::move(queue_.front());
            queue_.pop_front();
            return value;
        }

        bool TryRecv(T& out) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) return false;
            out = std::move(queue_.front());
            queue_.pop_front();
            return true;
        }

        void Drain() {
            T value;
            while (TryRecv(value)) {
                // Discard
            }
        }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        std::deque<T> queue_;
    };


    ////////////////////////////////////////////////////////////////////////////////
    // Internal types

    enum class InputKind : uint8_t {
        kNormal,
        kEntryPoint,
        kStdin,
    };


    struct VisitedFile {
        uint32_t source_index{};
    };


    struct GlobResolveResult {
        std::map<std::string, resolver::ResolveResult> resolve_results;
        std::string abs_path;
        PrettyPaths pretty_paths;
        std::string export_alias;
    };


    // "tlaCheck" in the reference (a value type there)
    struct TlaCheck {
        compiler::Index32 parent;
        uint32_t depth{};
        uint32_t import_record_index{};

        static TlaCheck Invalid() { return {}; }
    };


    struct ParseResult {
        std::vector<std::shared_ptr<resolver::ResolveResult>> resolve_results;
        std::map<uint32_t, GlobResolveResult> glob_resolve_results;

        ScannerFile file;

        TlaCheck tla_check;
        bool ok = false;
    };


    using ResultChannelPtr = std::shared_ptr<Channel<ParseResult>>;

    using InjectChannelPtr = std::shared_ptr<Channel<config::InjectedFile>>;


    struct ParseArgs {
        Fs* fs{};
        Log log;
        resolver::Resolver* res{};
        cache::CacheSet* caches{};
        PrettyPaths pretty_paths;
        const Source* import_source{};
        Source import_source_copy;
        const compiler::ImportAssertOrWith* import_with{};
        graph::SideEffects side_effects;
        std::any plugin_data;
        ResultChannelPtr results;
        InjectChannelPtr inject;
        std::string unique_key_prefix;
        Path key_path;
        config::Options options;
        Range import_path_range;
        uint32_t source_index{};
        bool skip_resolve{};
    };


    struct LoaderPluginResult {
        std::any plugin_data;
        std::string abs_resolve_dir;
        std::string plugin_name;
        config::Loader loader{config::Loader::kNone};
    };


    struct Scanner {
        Log log;
        Fs* fs{};
        std::shared_ptr<resolver::Resolver> res;
        cache::CacheSet* caches{};
        helpers::Timer* timer{};
        std::string unique_key_prefix;

        // These are not guarded by a mutex because they're only ever modified by a
        // single thread. Note that not all results in the "results" array are
        // necessarily valid. Make sure to check the "ok" flag before using them.
        //
        // Deviation from Go: this is a "std::deque" instead of a slice so that
        // growing the container never invalidates references to already-stored
        // results (Go's garbage collector keeps old array contents alive instead).
        std::deque<ParseResult> results;
        std::unordered_map<Path, VisitedFile, logger::PathHash> visited;
        ResultChannelPtr result_channel;

        config::Options options;

        // Also not guarded by a mutex for the same reason
        size_t remaining{};

        // All producer threads spawned during scanning are joined here
        std::vector<std::thread> threads;

        // For each HTML entry point (by source index), the virtual JS/CSS entry
        // points generated from its inline content. Filled while draining the
        // result channel and copied into "Bundle" by the scan orchestration.
        std::map<uint32_t, graph::HtmlInlineInfo> html_inline;

        // The inline <script>/<style> contents are handed to the scanner through
        // the stdin mechanism (a crafted "StdinInfo"). The parse threads read the
        // contents asynchronously, so the objects must outlive the scanner.
        std::vector<std::unique_ptr<config::StdinInfo>> stdin_info_holder;

        void Spawn(std::function<void()> fn) ;

        std::vector<std::exception_ptr> thrown_exceptions;
        std::mutex thrown_exceptions_mu;

        void JoinThreads() ;

        // This returns the source index of the resulting file
        uint32_t MaybeParseFile(
            const resolver::ResolveResult& resolve_result,
            PrettyPaths pretty_paths,
            const Source* import_source,
            Range import_path_range,
            const compiler::ImportAssertOrWith* import_with,
            InputKind kind,
            InjectChannelPtr inject)
        ;

        uint32_t AllocateSourceIndex(const Path& path, cache::SourceIndexKind kind) ;

        uint32_t AllocateGlobSourceIndex(uint32_t parent_source_index, uint32_t glob_index) ;

        void PreprocessInjectedFiles() ;

        std::vector<graph::EntryPoint> AddEntryPoints(
            const std::vector<config::EntryPoint>& entry_points)
        ;

        // Turn an HTML "container" entry point into one sub-entry point per
        // in-bundle resource the file references. The <script src>, <link href>,
        // <img src>, ... records were resolved during "ScanAllDependencies", so
        // each one now has a source index into a parsed target file. Making each
        // target its own entry point causes the linker to emit a separate output
        // file for it (a JS chunk for scripts, a CSS file for stylesheets, or a
        // copied asset for everything else).
        //
        // The HTML entry points themselves are kept in the list. They are the
        // containers that Step 6 (the HTML output pass) uses to locate the
        // rewritten output document, and the linker safely ignores files whose
        // representations are neither JS nor CSS.
        void ExpandHtmlEntryPoints(std::vector<graph::EntryPoint>& entry_points) ;

        // Concatenates an inline <script>/<style> element's text children in
        // document order (classic scripts share the global scope in browsers, so
        // concatenation preserves their semantics) and appends the result to
        // "output".
        void AppendInlineElementText(const html::Node& element, std::string& output) ;

        // HTML entry points are containers: the contents of their inline
        // <script>/<style> elements must be bundled like any other entry point.
        // Each HTML file gets one virtual JS entry (all of its classic inline
        // scripts, in order) and one virtual CSS entry (all of its inline
        // styles). The contents are handed to the scanner through the stdin
        // mechanism so that imports inside them resolve relative to the HTML
        // file. The resulting bundles are re-inlined into the HTML output.
        void ExpandHtmlInlineContent(const ParseResult& result, graph::HTMLRepr& repr,
                                     std::vector<graph::EntryPoint>& entry_points) ;

        void ScanAllDependencies(std::vector<graph::EntryPoint>& entry_points) ;

        ParseResult GenerateResultForGlobResolve(
            uint32_t source_index,
            const std::string& fake_source_path,
            const Source& import_source,
            Range import_range,
            const compiler::ImportAssertOrWith* import_with,
            compiler::ImportKind kind,
            compiler::ImportPhase phase,
            const GlobResolveResult& glob_result,
            const compiler::ImportAssertOrWith* assertions)
;

        std::vector<ScannerFile> ProcessScannedFiles(
            const std::vector<graph::EntryPoint>& entry_point_meta)
        ;

        TlaCheck ValidateTLA(uint32_t source_index) ;
    };
bool IsHtmlModuleScript(const html::Node& element);
graph::JSRepr* TryJSRepr(graph::InputFileRepr& repr);
std::string CanonicalFileSystemPathForWindows(const std::string& abs_path);
std::string GenerateUniqueKeyPrefix();
bool IsASCIIOnly(std::string_view text);
bool EndsWith(const std::string& text, const char* suffix);
std::string LowestCommonAncestorDirectory(
        Fs& fs, const std::vector<graph::EntryPoint>& entry_points);
std::string SanitizeFilePathForVirtualModulePath(const std::string& path);
void ParseFile(ParseArgs args);
bool LogPluginMessages(
        Fs& fs,
        Log log,
        const std::string& name,
        std::vector<Msg> msgs,
        const std::string& thrown_error,
        const Source* import_source,
        Range import_path_range);
LoaderPluginResult RunOnLoadPluginsImpl(
        const std::vector<config::Plugin>& plugins,
        Fs& fs,
        cache::FSCache& fs_cache,
        Log log,
        Source& source,
        const Source* import_source,
        Range import_path_range,
        const std::any& plugin_data,
        bool is_watch_mode,
        logger::PathStyle log_path_style,
        bool& ok);


} // namespace guchho::bundler
