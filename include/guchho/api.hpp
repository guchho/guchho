// =============================================================================
// guchho/api.hpp — the public C++ surface of Guchho
// =============================================================================
//
// Guchho is a bundler, a source-to-source transformer and a development
// server. This header is the single place where an embedder — a build tool, a
// test harness, an editor integration, or the command line driver shipped in
// src/ — talks to the engine. Nothing here reaches into the parser, the
// linker, the resolver or the file system abstraction, so those internals can
// be reorganised freely as long as the types below keep their shape.
//
// The surface is organised in layers that mirror the way a caller actually
// works:
//
//   1. Enumerations        - the vocabulary used by every option struct.
//   2. Result types        - diagnostics, output files, metafile text.
//   3. One-shot calls      - Build() and Transform().
//   4. Long-lived sessions - Context() returns a BuildContext that can
//                            rebuild, watch, serve, cancel and dispose.
//   5. Extension points    - the plugin hooks, and the lower level Serve() and
//                            Watcher primitives the session layer is made of.
//
// Rules that hold everywhere below:
//
//   * Configuration is input only. BuildOptions and TransformOptions are plain
//     structs whose members all have defaults, so a caller names just the few
//     fields it cares about and the rest of the behaviour falls into place.
//   * Failure is data, not an exception. Anything that can go wrong while
//     building arrives as Message entries in a result struct, so diagnostics
//     can be filtered, reworded, re-coloured or forwarded to an editor without
//     a single try/catch. Exceptions are reserved for programming mistakes
//     such as using a disposed context.
//   * Nothing touches the disk unless asked. Build() and Transform() hand back
//     the bytes they produced; only the "write" option commits them, which is
//     what makes the whole surface testable against an in-memory
//     filesystem::Fs.
//   * Threads are explicit. Work is either synchronous (Build/Transform) or
//     owned by an object the caller creates and destroys (BuildContext,
//     Watcher, ServeResult::stop). Nothing is started implicitly.
// =============================================================================

#pragma once

#include <any>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "guchho/filesystem.hpp"
#include "guchho/logger.hpp"

#include "guchho/config.hpp"

namespace guchho::api {

// =============================================================================
// Enums
// =============================================================================
//
// Every decision a caller can make about a build is a scoped enumeration, so
// the option structs further down stay a flat list of assignments with no
// accidental truthiness or integer/enum mixing.
//
// Enumerators named "kDefault" mean "not specified": the engine resolves them
// against the surrounding options during validation, which is what allows a
// caller to override one setting without restating the ones next to it. The
// resolved meaning is documented per enumerator below.

// How the source map for each emitted file is produced.
//   kNone             - no map is generated.
//   kInline           - the map is appended to the file as a trailing
//                       "//# sourceMappingURL=data:..." comment, so a single
//                       artefact is enough to debug in a browser.
//   kLinked           - a "//# sourceMappingURL=<name>.map" comment plus a
//                       separate ".map" file. The usual choice for a build
//                       whose files are uploaded to a web server.
//   kExternal         - the ".map" file is emitted with no linking comment, for
//                       pipelines that upload maps to a symbol server.
//   kInlineAndExternal - both of the above, so the artefact works on its own
//                       and a symbol server still has a file to fetch.
enum class SourceMap : uint8_t {
    kNone,
    kInline,
    kLinked,
    kExternal,
    kInlineAndExternal,
};

// Whether the pre-transform source text is embedded in the map.
//   kInclude - write a "sourcesContent" array, making the map self-contained:
//              a debugger can show the original source without asking the file
//              system for it.
//   kExclude - omit it. Maps get much smaller, which pays off when the same
//              sources are already reachable from wherever the debugger runs.
enum class SourcesContent : uint8_t {
    kInclude,
    kExclude,
};

// What happens to licence banners found in the inputs ("/*! ... */" headers,
// "//! ..." comments and similar markers that a rewriter must not delete).
//   kDefault   - resolved to kEndOfFile when bundling, kInline otherwise.
//   kNone      - drop the banners. Only legal when the licence allows it.
//   kInline    - keep each banner next to the code it covers, so the text stays
//                with its module after concatenation.
//   kEndOfFile - collect every banner and emit them once, at the top of the
//                output file.
//   kLinked    - put the text in a "<name>.LEGAL.txt" file and leave a short
//                pointer comment in the output.
//   kExternal  - write the "<name>.LEGAL.txt" files with no pointer comment.
enum class LegalComments : uint8_t {
    kDefault,
    kNone,
    kInline,
    kEndOfFile,
    kLinked,
    kExternal,
};

// How markup in .jsx-style sources is lowered.
//   kTransform - emit classic createElement calls, using "jsx_factory" and
//                "jsx_fragment" for the element and the fragment tag.
//   kPreserve  - leave the markup untouched, so the file is copied through
//                still containing JSX and must be processed again later.
//   kAutomatic - emit imports of the runtime's jsx()/jsxs()/Fragment helpers,
//                taken from "jsx_import_source". This is the modern default
//                for new code bases because it needs no global pragma.
enum class JSX : uint8_t {
    kTransform,
    kPreserve,
    kAutomatic,
};

// The language level the output must stay within. Syntax or APIs that a
// target does not support are lowered automatically; everything else is passed
// through untouched, so this doubles as a promise about the emitted file.
//   kDefault - no lowering based on a language level; only the "supported"
//              overrides and the "platform" rules apply.
//   kESNext  - everything the current parser understands, including syntax no
//              engine ships yet.
//   kES5..kES2025 - lower everything above the named edition.
enum class Target : uint8_t {
    kDefault,
    kESNext,
    kES5,
    kES2015,
    kES2016,
    kES2017,
    kES2018,
    kES2019,
    kES2020,
    kES2021,
    kES2022,
    kES2023,
    kES2024,
    kES2025,
};

// How a file's bytes become a module. The loader is the single biggest lever
// on what a file contributes to the graph: the same bytes under "kText" are an
// inert string, under "kJSON" a real object literal that participates in
// bundling, and under "kCopy" not a module at all but an opaque asset that is
// emitted verbatim and referenced by URL.
//   kNone       - not chosen yet; the engine picks one from the file extension,
//                 the nearest package metadata, and any per-path overrides in
//                 BuildOptions::loader.
//   kBase64     - a data:...;base64 URL string.
//   kBinary     - a typed-array view over the raw bytes, for real binary data.
//   kCopy       - pass the file through untouched and expose its output URL.
//   kCSS        - a stylesheet that participates in CSS bundling.
//   kDataURL    - a data: URL for the file's contents.
//   kDefault    - defer to the same guessing logic as kNone, chosen explicitly.
//   kEmpty      - an empty module, useful for stubbing a file out entirely.
//   kFile       - a file wrapper object exposing its path and bytes.
//   kGlobalCSS  - a stylesheet whose rules land in the global scope rather
//                 than in a local scope, so it cannot be tree-shaken.
//   kJS         - JavaScript.
//   kJSON       - a JSON document, parsed into an object whose keys may be
//                 referenced as named imports.
//   kJSX        - JavaScript containing markup.
//   kLocalCSS   - a stylesheet whose rules stay local to the importing module,
//                 so unused rules can be dropped.
//   kText       - a plain string, and nothing else.
//   kTS         - a typed source: annotations are erased, the runtime is
//                 plain JavaScript.
//   kTSX        - a typed source that also contains markup.
enum class Loader : uint16_t {
    kNone,
    kBase64,
    kBinary,
    kCopy,
    kCSS,
    kDataURL,
    kDefault,
    kEmpty,
    kFile,
    kGlobalCSS,
    kJS,
    kJSON,
    kJSX,
    kLocalCSS,
    kText,
    kTS,
    kTSX,
};

// Which environment the output is destined for. The platform decides which
// export conditions are active, the meaning of a bare specifier, the globals a
// module may assume, and a long tail of smaller rules, so it is normally the
// first thing a caller sets.
//   kDefault - resolved to kBrowser.
//   kBrowser - activates the "browser" export condition and the "browser" field
//              of package metadata, so a package can ship a browser-specific
//              file. The output is expected to run against a document.
//   kNode    - activates the "node" export condition; bare specifiers are
//              looked up in node_modules and the runtime's built-in modules are
//              reachable.
//   kNeutral - activates neither condition, so package metadata is used as
//              written. Useful for a library that emits declarations only and
//              commits to neither environment.
enum class Platform : uint8_t {
    kDefault,
    kBrowser,
    kNode,
    kNeutral,
};

// The module syntax of the emitted file. This is what makes one bundle
// loadable in more than one kind of loader.
//   kDefault   - resolved from the other options. A bundle becomes an IIFE for
//                a browser build, a CommonJS module for a Node build and an ES
//                module for a neutral one; a file that is only being converted
//                keeps the import/export syntax of the input. Set it explicitly
//                when the output has to be predictable across platforms.
//   kIIFE      - wrap everything in a self-invoking function; the result is a
//                script with no module system at all.
//   kCommonJS  - emit module.exports and require(); works with any CommonJS
//                host.
//   kESModule  - emit import/export; the only format that supports "splitting".
//   kUMD       - universal wrapper: one file that works as a script, as a
//                CommonJS module and as an AMD module, and also publishes a
//                global name.
//   kAMD       - asynchronous module definition, with the "amd_*" options
//                below.
//   kSystem    - SystemJS, with the "system_null_setters" option below.
enum class Format : uint8_t {
    kDefault,
    kIIFE,
    kCommonJS,
    kESModule,
    kUMD,
    kAMD,
    kSystem,
};

// How imports that name a package are treated.
//   kDefault  - resolved to kBundle.
//   kBundle   - resolve the package and inline its code into the output.
//   kExternal - keep the import statement and never touch the package.
enum class Packages : uint8_t {
    kDefault,
    kBundle,
    kExternal,
};

// The engines a build claims compatibility with. Each entry pairs an engine
// with a version, and the union of their capabilities becomes the set of
// features the output is allowed to use. It is the alternative to "target":
// name the browsers and runtimes you actually test on, and the engine computes
// the necessary lowering itself.
enum class EngineName : uint8_t {
    kChrome,
    kDeno,
    kEdge,
    kFirefox,
    kHermes,
    kIE,
    kIOS,
    kNode,
    kOpera,
    kRhino,
    kSafari,
};

// Whether diagnostics written to stderr may contain ANSI colour escapes.
//   kColorIfTerminal - colour only when stderr is a real terminal. This is the
//                      right default for a library: redirected output stays
//                      free of escape codes.
//   kColorNever      - never colour.
//   kColorAlways     - always colour, including into a pipe or a log file.
enum class StderrColor : uint8_t {
    kColorIfTerminal,
    kColorNever,
    kColorAlways,
};

// How much Guchho says about its own progress. Anything quieter than the
// configured level is dropped, and a message about one specific subsystem can
// be turned up or down on its own through BuildOptions::log_override.
//   kSilent   - print nothing at all.
//   kVerbose  - trace every step, including internal caching decisions.
//   kDebug   - trace phases and timings.
//   kInfo    - summaries, timings and the "rebuild started/finished" lines.
//   kWarning - only things that may indicate a mistake in the inputs.
//   kError   - only failures.
enum class LogLevel : uint8_t {
    kSilent,
    kVerbose,
    kDebug,
    kInfo,
    kWarning,
    kError,
};

// How non-ASCII characters are escaped in the output.
//   kDefault - resolved to kASCII, which escapes every non-ASCII character as
//              a "\uXXXX" sequence so the file survives any encoding chain
//              between the build machine and the browser.
//   kASCII    - always escape.
//   kUTF8     - emit the characters as-is when the output charset allows it,
//               which is smaller and easier to read.
enum class Charset : uint8_t {
    kDefault,
    kASCII,
    kUTF8,
};

// Whether unused exports and unreachable statements are removed.
//   kDefault - resolved to enabled when bundling or when the output is an
//              IIFE, because in both cases there is no way for a consumer to
//              concatenate more code onto the end of the file and rely on the
//              parts that were dropped; otherwise disabled, so that a file
//              being rewritten still behaves like the original.
//   kFalse   - never remove anything.
//   kTrue    - always remove, which is also safe for a non-bundled file.
enum class TreeShaking : uint8_t {
    kDefault,
    kFalse,
    kTrue,
};

// Which debugging constructs are deleted from the output. These are bit flags,
// so they combine with operator| and are tested with Has():
//
//   BuildOptions::drop = DropFlags::kDropConsole | DropFlags::kDropDebugger;
//
// An empty value keeps everything.
enum class DropFlags : uint8_t {
    kNone = 0,
    kDropConsole = 1 << 0,  // every console.* call is replaced with undefined
    kDropDebugger = 1 << 1, // debugger statements
};

// Combines two flag sets. kNone is the identity:
//   kDropConsole | kNone => kDropConsole
inline DropFlags operator|(DropFlags a, DropFlags b) {
    return static_cast<DropFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

// Accumulates flags into an existing option value, so several drop rules can
// be gathered before being assigned:
//   drop = kNone;  drop |= kDropConsole;  drop |= kDropDebugger;
inline DropFlags& operator|=(DropFlags& a, DropFlags b) {
    a = a | b;
    return a;
}

// Tests whether "a" and "b" have any flag in common. It is an overlap test, not
// a subset test: a bit that is missing from "a" does not make the answer false
// as long as one bit is shared.
//   Has(kDropConsole | kDropDebugger, kDropDebugger) => true
//   Has(kDropConsole, kDropDebugger)                 => false
inline bool Has(DropFlags a, DropFlags b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// Whether property names may be shortened when they appear as string keys as
// well as identifiers. This trades a bit of output size for output that no
// longer round-trips through JSON, so it is opt-in.
//   kFalse - only real property names are mangled.
//   kTrue  - "obj.foo" and obj["foo"] get the same shortened name, which keeps
//            lookups working but changes the strings in the output.
enum class MangleQuoted : uint8_t {
    kFalse,
    kTrue,
};

// Which surfaces print fully absolute paths. Paths are normally shown relative
// to the working directory, because absolute paths differ on every machine and
// make build logs and caches impossible to share. These flags opt specific
// surfaces out of that shortening, and they combine like DropFlags.
enum class AbsPathsFlags : uint8_t {
    kNone = 0,
    kCodeAbsPath = 1 << 0,     // paths inside generated source, e.g. import specifiers
    kLogAbsPath = 1 << 1,      // paths inside diagnostics and log lines
    kMetafileAbsPath = 1 << 2, // paths inside the metafile JSON
};

// Tests whether "a" and "b" have any flag in common, as above.
//   Has(kLogAbsPath | kMetafileAbsPath, kLogAbsPath) => true
inline bool Has(AbsPathsFlags a, AbsPathsFlags b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// The answer an onResolve hook gives to "may this module be dropped when
// nothing imports anything from it?".
//   kTrue  - keep it, even if unused. The safe answer, and the default.
//   kFalse - it is side-effect free, so drop it when nothing uses it. Only say
//            this when loading the module genuinely has no observable effect,
//            because getting it wrong deletes code that was doing work.
enum class SideEffects : uint8_t {
    kTrue,
    kFalse,
};

// What triggered a resolution request, so an onResolve hook can react
// differently to an entry point than to a nested import.
//   kNone              - no trigger recorded.
//   kEntryPoint        - a path named in BuildOptions::entry_points.
//   kJSImportStatement - a static import statement.
//   kJSRequireCall     - a require() call.
//   kJSDynamicImport   - a dynamic import() with a literal specifier.
//   kJSRequireResolve  - require.resolve().
//   kCSSImportRule     - an @import rule in a stylesheet.
//   kCSSComposesFrom   - "composes: ... from ..." in a CSS module.
//   kCSSURLToken       - a url() reference inside a stylesheet, which is
//                        fetched as a file rather than resolved as a module.
enum class ResolveKind : uint8_t {
    kNone,
    kEntryPoint,
    kJSImportStatement,
    kJSRequireCall,
    kJSDynamicImport,
    kJSRequireResolve,
    kCSSImportRule,
    kCSSComposesFrom,
    kCSSURLToken,
};

// The severity a batch of messages is being formatted as. It decides the
// bracketed tag in front of the text ("[ERROR]" or "[WARNING]") and the colour
// that tag and its icon are painted in.
enum class MessageKind : uint8_t {
    kError,
    kWarning,
};

// =============================================================================
// Shared types
// =============================================================================
//
// The vocabulary of results. A build produces files and problems; these are
// the types both are expressed in.

// One engine and the version the caller intends to support. The version is read
// as "major[.minor[.patch]]" with an optional pre-release suffix and compared
// component by component, so "100.0.0" is newer than "90.0.0" and "12.0.0" is
// newer than "9.9.9".
//
// Input:  engines = { { kFirefox, "115" }, { kSafari, "16.4" } }
// Output: the build is only allowed to use what both versions support, and
//         the pair is reported back in the summary as a target string such as
//         "firefox115,safari16.4". A version that does not parse — "latest",
//         for instance — becomes a message in result.errors rather than a
//         silent guess.
struct Engine {
    EngineName name{};
    std::string version;
};

// Where a problem was found. Line and column follow the same convention the
// editor integrations expect: lines count from 1, columns count from 0 in
// bytes, and length is a byte count, so a multi-byte character advances the
// column by its encoded size.
struct Location {
    std::string file;
    std::string namespace_;
    int line{};
    int column{};
    int length{};
    std::string line_text;
    std::string suggestion;
};

// An extra paragraph attached to a Message. Warnings often come with a hint
// ("did you mean ...?") or a list of related files, and a Note is how that
// extra context is carried. A note may have no location at all, in which case
// it is a plain remark.
struct Note {
    std::string text;
    std::optional<Location> location;
};

// A single problem found while parsing, resolving or printing. Messages are
// values, not exceptions: a build collects all of them and hands them back, so
// one pass over a large graph can report every bad import at once instead of
// stopping at the first one.
struct Message {
    std::string id;          // stable machine-readable code, e.g. "import-is-undefined"
    std::string plugin_name; // set when a plugin produced this message
    std::string text;        // the human-readable sentence
    std::optional<Location> location;
    std::vector<Note> notes;

    // Opaque payload supplied by whoever created the message, passed through
    // untouched so an editor integration can recover the AST node, the original
    // exception or whatever else it stashed there.
    std::any detail;
};

// One artefact a build produced. Contents are bytes rather than text because
// the loader set includes binary assets, and a caller should be able to treat
// every output the same way.
struct OutputFile {
    std::string path;               // absolute path, or "<stdout>"
    std::vector<uint8_t> contents;  // the file, exactly as it would be written

    // A short digest of the contents. Comparing hashes tells you whether a
    // file really changed without reading both copies, which is how the
    // incremental path and the writer both avoid pointless disk traffic.
    std::string hash;
};

// =============================================================================
// Build API
// =============================================================================
//
// BuildOptions is the complete description of one bundling job: where the
// entry points are, how the graph should be followed, how each file should be
// interpreted, and what the emitted files should look like. Every member has a
// default, so the smallest useful call is "name one entry point, set bundle or
// outfile, read the files back from the result".

// One input file paired with the output path it should produce. Use these when
// the mapping from input to output cannot be expressed by the naming templates
// alone.
struct EntryPoint {
    std::string input_path;
    std::string output_path;
};

// Text handed to the build in place of a file on disk. This is how a language
// server feeds the buffer the user is editing into the bundler, so the graph
// sees the unsaved version of a file rather than the last one written.
struct StdinOptions {
    std::string contents;    // the source text itself
    std::string resolve_dir; // directory that relative imports resolve against
    std::string sourcefile;  // name to blame in diagnostics, e.g. "<stdin>"
    Loader loader{Loader::kNone};
};

struct Plugin;
struct PluginBuild;

// Everything a build can be told. The members are grouped by the phase they
// affect, and the blank lines between groups mark those boundaries.
struct BuildOptions {
    // -- Diagnostics -------------------------------------------------------
    StderrColor    color{StderrColor::kColorIfTerminal};
    LogLevel       log_level{LogLevel::kInfo};
    int            log_limit{}; // cap on reported messages; 0 means no cap
    std::unordered_map<std::string, LogLevel> log_override; // per-subsystem levels
    AbsPathsFlags  abs_paths{AbsPathsFlags::kNone};

    // -- Source maps -------------------------------------------------------
    SourceMap      sourcemap{SourceMap::kNone};
    std::string    source_root;  // URL prefix prepended to every source path
    SourcesContent sources_content{SourcesContent::kInclude};

    // -- Language level ----------------------------------------------------
    // "target" names a single edition; "engines" names the engines you support
    // and lets the engine compute the level itself. "supported" is the escape
    // hatch: it turns one specific feature on or off regardless of both.
    Target                              target{Target::kDefault};
    std::vector<Engine>                 engines;
    std::unordered_map<std::string, bool> supported;

    // -- Minification ------------------------------------------------------
    // The three minify_* switches are independent, so "minify_whitespace" alone
    // is a safe way to shrink a file for debugging while keeping names intact.
    // "mangle_props" is a regular expression matching property names to
    // shorten; "reserve_props" names the ones to protect. "mangle_cache" lets a
    // long-lived caller carry the property-name decisions of a previous build
    // forward, so a hot-reload session keeps stable names between runs.
    std::string mangle_props;
    std::string reserve_props;
    MangleQuoted mangle_quoted{MangleQuoted::kFalse};
    std::unordered_map<std::string, bool> mangle_cache;
    DropFlags    drop{DropFlags::kNone};
    // Names of statement labels whose labelled statement should be deleted
    // outright — the classic way of fencing off development-only code:
    //   drop_labels = {"DEBUG"};  while (DEBUG) { ... }   // the loop is gone
    std::vector<std::string> drop_labels;
    bool         minify_whitespace{};
    bool         minify_identifiers{};
    bool         minify_syntax{};
    int          line_limit{}; // error out if any single line grows past this
    Charset      charset{Charset::kDefault};
    TreeShaking  tree_shaking{TreeShaking::kDefault};
    bool         ignore_annotations{}; // treat /* @__PURE__ */ as an ordinary comment
    LegalComments legal_comments{LegalComments::kDefault};

    // -- Markup ------------------------------------------------------------
    // The classic runtime is configured by naming the factory and the fragment
    // ("React.createElement" and "React.Fragment" for the usual choice); the
    // automatic runtime instead imports from "jsx_import_source", and appends
    // "/jsx-dev-runtime" to it when "jsx_dev" is set so the development build
    // of the runtime is used.
    JSX      jsx{JSX::kTransform};
    std::string jsx_factory;       // classic runtime: the element factory
    std::string jsx_fragment;      // classic runtime: the fragment name
    std::string jsx_import_source; // automatic runtime: which module to import from
    bool     jsx_dev{};            // use the development build of that runtime
    bool     jsx_side_effects{};   // treat used JSX elements as side effects

    // -- Compile-time substitution -----------------------------------------
    // "define" replaces identifiers and dotted paths with literal values;
    // "pure" lists functions that may be dropped or reordered when unused.
    // Both are validated up front, so a typo in a define becomes a reported
    // message rather than a silent mismatch.
    std::unordered_map<std::string, std::string> define;
    std::vector<std::string> pure;
    bool keep_names{}; // preserve function and class names while minifying

    // -- Global name -------------------------------------------------------
    // Used by the wrapper formats to publish the bundle's exports as a global.
    std::string global_name;

    // -- UMD format --------------------------------------------------------
    // "extend" appends to an existing global instead of replacing it,
    // "no_conflict" wraps the assignment so two copies of the library can
    // coexist, and "globals" maps external module ids to the global names the
    // wrapper should read them from.
    bool        extend{};
    bool        no_conflict{};
    bool        strict{true};
    std::unordered_map<std::string, std::string> globals;

    // -- AMD format --------------------------------------------------------
    // "amd_id" is the module id this file registers as; "amd_define" renames
    // the define() function itself; "amd_base_path" prefixes the ids of the
    // modules this one pulls in.
    std::string amd_id;
    bool        amd_auto_id{};
    std::string amd_base_path;
    std::string amd_define{"define"};
    bool        amd_force_js_extension_for_imports{};

    // -- System format -----------------------------------------------------
    // Emits null setters for namespace imports, so reading a missing export
    // returns undefined instead of throwing.
    bool        system_null_setters{};

    // -- Graph construction ------------------------------------------------
    // "bundle" inlines the whole graph into the output; without it each input
    // file is merely converted. Two consequences worth knowing: "external" and
    // "alias" only mean something when bundling, and using either without it
    // is reported as an error, because there is no graph for them to act on.
    //
    // "splitting" extracts code shared by several entry points into chunks that
    // those entries import, which requires kESModule output. "external" keeps
    // the named paths out of the graph and leaves the import statement in the
    // output; "alias" redirects a specifier before resolution starts. The
    // loader map overrides how individual paths are interpreted, and
    // "resolve_extensions" lists the suffixes tried for a specifier that has
    // none.
    //
    // Output location: exactly one of "outfile", "outdir" or neither. With
    // neither, the result is a single unnamed file meant to be written to
    // stdout by the caller, and options that need a directory — external source
    // maps, linked legal-comment files, the file and copy loaders — are
    // rejected. With more than one input, or an input containing "*", "outdir"
    // is required; setting both "outfile" and "outdir" is an error. "outbase"
    // chooses which directory the output tree is measured from when computing
    // relative names, and "abs_working_dir" replaces the process working
    // directory for the whole build.
    bool        bundle{};
    bool        preserve_symlinks{}; // resolve through, instead of collapsing, links
    bool        splitting{};
    std::string outfile;             // single output file, relative to outdir
    bool        metafile{};          // fill BuildResult::metafile with build stats
    std::string outdir;              // root of the output tree
    std::string outbase;             // directory the output tree is measured from
    std::string abs_working_dir;     // overrides the process working directory
    Platform    platform{Platform::kDefault};
    Format      format{Format::kDefault};
    std::vector<std::string> external;
    Packages    packages{Packages::kDefault};
    std::unordered_map<std::string, std::string> alias;
    std::vector<std::string> main_fields;  // package metadata fields, in order
    std::vector<std::string> conditions;   // export conditions to activate
    std::unordered_map<std::string, Loader> loader;
    std::vector<std::string> resolve_extensions;
    std::string tsconfig;     // path to a compiler config file
    std::string tsconfig_raw; // or its contents, used when no file is needed
    std::unordered_map<std::string, std::string> out_extension; // e.g. {".js": ".mjs"}
    std::string public_path;   // prefix for asset URLs in the output
    std::vector<std::string> inject; // files whose exports are prepended
    std::unordered_map<std::string, std::string> banner; // text prepended per file kind
    std::unordered_map<std::string, std::string> footer; // text appended per file kind
    std::vector<std::string> node_paths; // extra module search roots

    // -- Output naming -----------------------------------------------------
    // Templates built from the [dir], [name], [hash] and [ext] placeholders,
    // for example "chunks/[name]-[hash].js". Anything outside a placeholder is
    // literal text, and an empty template means "use the default name".
    std::string entry_names;
    std::string chunk_names;
    std::string asset_names;

    // -- Inputs ------------------------------------------------------------
    // Two ways to name inputs: "entry_points" is a flat list of paths, and
    // "entry_points_advanced" carries an explicit output path for each one.
    // Both may be used at once; the advanced entries are appended after the
    // plain ones. A path containing "*" is expanded as a pattern.
    std::vector<std::string> entry_points;
    std::vector<EntryPoint>  entry_points_advanced;

    // -- Execution ---------------------------------------------------------
    // "write" is the only switch that touches the disk: without it the caller
    // receives the bytes and decides what to do with them. "allow_overwrite"
    // permits an output file to land on top of a file the build itself read,
    // which is otherwise reported as an error. "stdin_data" feeds one virtual
    // file into the graph in place of a path on disk.
    std::optional<StdinOptions>    stdin_data;
    bool         write{};
    bool         allow_overwrite{};
    std::vector<Plugin> plugins;
};

// What a build produced. On failure "errors" is non-empty and the output
// fields stay empty; a build with only warnings is still a successful build.
struct BuildResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::vector<OutputFile> output_files;
    std::string metafile;                              // JSON, when metafile is set
    std::unordered_map<std::string, bool> mangle_cache; // carry into the next build
};

// Runs one complete build: resolve the entry points, walk the graph, compile
// and print everything, then tear the session down again. Nothing is written
// unless "write" is set.
//
// This is the one-shot entry point. Reach for Context() instead when the
// caller intends to build more than once, since a build that is thrown away
// after use cannot be cancelled and re-runs its setup work every time.
//
// Input:  entry_points = {"src/index.js"}, bundle = true, format = kESModule,
//         write = false, minify_whitespace = true
// Output: one OutputFile per chunk in "output_files", with "contents" holding
//         the generated bytes; "errors" empty on success; "metafile" empty
//         because "metafile" was not requested; the disk untouched.
//
// Failures are reported, not thrown: a missing entry point comes back as
// result.errors[0] with a Message whose "location" points at the offending
// path.
BuildResult Build(const BuildOptions& options);

// =============================================================================
// Transform API
// =============================================================================
//
// The transform layer works on a single string in memory. It has no file
// system, no module graph and no package resolution, which makes it the right
// tool for editor integrations, "paste this code into my app" endpoints, and
// pipelines that already did their own bundling.

// The per-file subset of BuildOptions. Options that only make sense for a
// graph — entry points, outdir, splitting, external, plugin hooks, and the
// module-resolution fields — are absent by construction, so a transform can
// never accidentally depend on them.
struct TransformOptions {
    // -- Diagnostics -------------------------------------------------------
    StderrColor color{StderrColor::kColorIfTerminal};
    LogLevel log_level{LogLevel::kInfo};
    int log_limit{};
    std::unordered_map<std::string, LogLevel> log_override;
    AbsPathsFlags abs_paths{AbsPathsFlags::kNone};

    // -- Source maps -------------------------------------------------------
    SourceMap sourcemap{SourceMap::kNone};
    std::string source_root;
    SourcesContent sources_content{SourcesContent::kInclude};

    // -- Language level ----------------------------------------------------
    Target target{Target::kDefault};
    std::vector<Engine> engines;
    std::unordered_map<std::string, bool> supported;

    // -- Output shape ------------------------------------------------------
    // Without a bundle there is no wrapper to emit, so "format" only takes
    // effect for the formats that can express a single file on its own (an
    // IIFE, or a CommonJS export of the entry bindings). "global_name" names
    // what an IIFE publishes.
    Platform platform{Platform::kDefault};
    Format format{Format::kDefault};
    std::string global_name;

    // -- Minification ------------------------------------------------------
    // Same knobs as the build API. "mangle_cache" can be fed back in from a
    // previous TransformResult, which keeps property names stable while a user
    // types into a file and the editor re-transforms it after every keystroke.
    std::string mangle_props;
    std::string reserve_props;
    MangleQuoted mangle_quoted{MangleQuoted::kFalse};
    std::unordered_map<std::string, bool> mangle_cache;
    DropFlags drop{DropFlags::kNone};
    std::vector<std::string> drop_labels;
    bool minify_whitespace{};
    bool minify_identifiers{};
    bool minify_syntax{};
    int line_limit{};
    Charset charset{Charset::kDefault};
    TreeShaking tree_shaking{TreeShaking::kDefault};
    bool ignore_annotations{};
    LegalComments legal_comments{LegalComments::kDefault};

    // -- Markup ------------------------------------------------------------
    JSX jsx{JSX::kTransform};
    std::string jsx_factory;
    std::string jsx_fragment;
    std::string jsx_import_source;
    bool jsx_dev{};
    bool jsx_side_effects{};

    // -- Compiler configuration -------------------------------------------
    // The configuration is passed as text rather than as a path, because a
    // transform may be asked to process a buffer that has no file yet. Only
    // the settings that affect parsing are honoured here (path mapping for
    // type-only imports, JSX defaults, and similar).
    std::string tsconfig_raw;
    std::string banner; // text prepended to the output
    std::string footer; // text appended to the output

    // -- Compile-time substitution -----------------------------------------
    std::unordered_map<std::string, std::string> define;
    std::vector<std::string> pure;
    bool keep_names{};

    // -- Input description -------------------------------------------------
    // "sourcefile" is the name that appears in diagnostics and in the map's
    // "sources" array; it defaults to "<stdin>". "loader" decides how the
    // string is read and defaults to kJS, so plain JavaScript needs no
    // configuration at all.
    std::string sourcefile;
    Loader loader{Loader::kNone};
};

// The transformed source plus whatever else the transform produced. The
// vectors are bytes so a source map, which is JSON but never really text to be
// read by a human, needs no special case.
struct TransformResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::vector<uint8_t> code;            // the transformed source
    std::vector<uint8_t> map;             // the map itself, when one was asked for
    std::vector<uint8_t> legal_comments; // extracted banners, if any were pulled out

    std::unordered_map<std::string, bool> mangle_cache; // pass into the next call
};

// Transforms one string of source code into JavaScript and returns the bytes
// without writing anything anywhere. This is the operation behind
// "compile this buffer", "minify this snippet" and "rewrite this file for the
// browser", and it is the same parser and printer the bundler uses, so a file
// that survives a transform will also survive being bundled.
//
// Input:  "const f = (x: number) => x * 2;" with loader = kTS,
//         target = kES2015, minify_whitespace = true
// Output: code = "const f=x=>x*2;" (types erased, arrow preserved), errors
//         empty, map empty because "sourcemap" was kNone.
//
// A syntax error is reported the same way a build reports one: result.errors
// gets a Message with the "location" of the offending token and code stays
// empty. Nothing is thrown for bad input.
TransformResult Transform(const std::string& input, const TransformOptions& options);

// =============================================================================
// Context API (rebuild / watch / serve / cancel / dispose)
// =============================================================================
//
// A BuildContext is a build that is still alive. It keeps the validated
// options, the caches and the watch state between runs, so a rebuild is much
// cheaper than a fresh Build() and can additionally be watching the file
// system, serving the output over HTTP, or stopped on request.
//
// The lifecycle is: Context() to create, Rebuild() any number of times, then
// Dispose(). Everything else is optional and may be enabled at most once.

// Origins allowed to make cross-origin requests. An origin is a protocol, host
// and port ("http://localhost:3000"); "*" matches any origin, and it may
// appear at most once per entry.
struct CORSOptions {
    std::vector<std::string> origin;
};

struct ServeOnRequestArgs;

// How to configure the development server. Port 0 asks the system for any free
// port, and the port that was actually bound is reported back in
// ServeResult::port.
//
// Every GET or HEAD request produces the current build first, so a reload
// always shows the latest output. The requested path is then looked for in
// three places, in order: inside the build's output directory (served straight
// from memory, whether or not the build wrote anything to disk), then inside
// "servedir", and finally in the file named by "fallback" — which is how a
// single-page app gets its own index document back for any unknown route. A
// build that produced errors answers with status 503 and the error text as the
// body, so a mistake shows up in the browser instead of silently serving the
// previous build.
struct ServeOptions {
    uint16_t port{};
    std::string host; // interface to bind; empty means every interface

    std::string servedir;  // directory of static files to fall back to
    std::string keyfile;   // must be set together with certfile
    std::string certfile;  // must be set together with keyfile
    std::string fallback;  // served when nothing else matches

    CORSOptions cors;

    // Invoked once per request, after the response has been produced, with the
    // method, path, status and how long the response took to generate. A good
    // place to count requests, or to log anything the server itself did not
    // handle.
    std::function<void(const ServeOnRequestArgs&)> on_request;
};

// The facts about one served request, handed to ServeOptions::on_request.
// "time_in_ms" measures how long the response took to generate; it says
// nothing about how long the client took to download it, which the server
// cannot observe.
struct ServeOnRequestArgs {
    std::string remote_address;
    std::string method;
    std::string path;
    int status{};
    int time_in_ms{};
};

// The running server, plus the only way to stop it. Call "stop" exactly once;
// it blocks until the listening socket is closed and every in-flight request
// has been answered, so it is safe to use during shutdown.
struct ServeResult {
    uint16_t port{};             // the port actually bound, which may differ
    std::vector<std::string> hosts; // URLs to print, one per reachable address

    std::function<void()> stop;
};

// How long to wait after a change is noticed before rebuilding. The delay
// exists so a burst of editor writes (save, format, save again) produces one
// rebuild instead of three. A delay of 0 rebuilds immediately.
struct WatchOptions {
    int delay{};
};

// A live build. Create one with Context(), use it as much as you like, and
// Dispose() it last.
//
// The interface is abstract so a test can substitute a context that serves
// canned results, and so the implementation details — the file system
// instance, the caches, the watcher thread — can change without touching
// callers.
class BuildContext {
public:
    virtual ~BuildContext() = default;

    // Runs the build again with the options this context was created with.
    // Concurrent callers are fine: the second one waits for the build already
    // in progress and receives that result rather than starting a competing
    // build. The files are returned, not written, unless "write" was
    // requested.
    //
    // Input:  a context created with bundle = true and one entry point
    // Output: a BuildResult describing this run in full — every output file
    //         the build produced, plus any errors and warnings. Rebuilding
    //         reuses the validated options and the caches, which is what makes
    //         it cheaper than calling Build() again.
    //
    // A disposed context is inert rather than dangerous: this returns an empty
    // BuildResult instead of throwing, so a late call from another thread
    // cannot take the process down.
    virtual BuildResult Rebuild() = 0;

    // Starts watching every file the build read, plus whatever plugins asked to
    // watch, and rebuilds automatically whenever one of them changes. Watch
    // data is only collected in builds that run after this call, so enable it
    // before the first build if you want that first build to be covered.
    //
    // Input:  WatchOptions{ delay = 200 }
    // Output: nothing; changes now produce "[watch] build started"/"finished"
    //         lines on the log stream, and later Rebuild() calls return the new
    //         output.
    //
    // Throws "std::runtime_error" when the context is disposed or when watch
    // mode is already enabled.
    virtual void Watch(const WatchOptions& options) = 0;

    // Starts an HTTP server over the build's output and returns as soon as it
    // is accepting connections. Anything the server serves from the build
    // triggers a rebuild first, so editing a file and reloading the page is
    // enough to see the change.
    //
    // Input:  ServeOptions{ servedir = "dist", port = 0 }
    // Output: ServeResult with the bound "port", the "hosts" to print, and a
    //         "stop" function that must be called to shut the server down.
    //
    // Throws "std::runtime_error" on a bad configuration (a port already in
    // use, a half-specified TLS pair, a malformed CORS origin), on a disposed
    // context, or when the context is already watching.
    virtual ServeResult Serve(const ServeOptions& options) = 0;

    // Asks the build that is running right now to stop at the next convenient
    // point, and blocks until it has actually stopped. A build that is
    // cancelled this way reports it: "errors" contains a single message saying
    // the build was cancelled and no output is produced. Calling this while
    // nothing is running is a no-op, and calling it on a disposed context is
    // also a no-op.
    virtual void Cancel() = 0;

    // Stops the watcher thread, shuts the build down and releases the caches.
    // Every other method on a disposed context either throws or does nothing,
    // so this is the last call and it must be made exactly once. Calling it
    // twice is harmless.
    virtual void Dispose() = 0;
};

// Creates a BuildContext from a set of options. Options are validated up front
// and the context is a working, ready-to-use object: the caller still decides
// when the first build happens.
//
// Validation failures are returned rather than thrown, because they are
// ordinary user-facing problems: a bad option combination should be reported
// with a Message the caller can print, exactly like a compile error.
//
// Input:  entry_points = {"src/index.js"}, bundle = true, write = true
// Output: a context, or nullptr with "errors" filled in; for example a null
//         context whose first error says "Must use \"outdir\" when there are
//         multiple input files" if only "outfile" was set.
//
// Input:  an invalid LogLevel value cast to the enum
// Output: nullptr, with a message naming the option that failed to validate.
std::unique_ptr<BuildContext> Context(const BuildOptions& options, std::vector<Message>& errors);

// =============================================================================
// Plugin API
// =============================================================================
//
// A plugin is a struct with a name and a setup function. Guchho hands the
// setup function a PluginBuild object, which is the plugin's entire interface
// to the engine: it registers callbacks for the hooks it cares about, and it
// can call "resolve" to take part in resolution itself.
//
// Setup runs once, when the context is created and before the first build. The
// hooks it registers then run for every build of that context, so a plugin
// registers its callbacks once and lets the engine call them repeatedly.
//
// Hooks are matched by filter rather than by registration order, so two
// plugins can both say they handle "*.png" and the first one that decides it
// has an answer wins.

// One plugin. Add instances to BuildOptions::plugins. The name is not optional:
// it appears in the file names and error messages of everything the plugin
// does, and a plugin with an empty name is rejected. Setup is a chance to
// inspect the options and register hooks, not to do the work; whatever the
// plugin builds or caches is then reused across rebuilds.
struct Plugin {
    std::string name;
    std::function<void(struct PluginBuild&)> setup;
};

// The request handed to PluginBuild::resolve when a plugin resolves a path
// itself. It mirrors what a callback registered with on_resolve would receive,
// so one piece of resolution logic can be used either way. Only "kind" is
// mandatory; the other fields describe the situation, and a plugin that leaves
// them empty is asking its question with as little context as it can manage.
struct ResolveOptions {
    std::string plugin_name; // names the plugin in a resolution error
    std::string importer;    // the file that asked for it, empty for an entry point
    std::string namespace_;  // virtual filesystem the request came from
    std::string resolve_dir; // directory relative paths resolve against
    ResolveKind kind{ResolveKind::kNone};
    std::any plugin_data;                        // payload from the previous step
    std::unordered_map<std::string, std::string> with; // values passed via "with"
};

// The answer to a resolve request. Leaving "path" empty declines the request,
// which lets the next plugin — and finally the built-in rules — have a go;
// filling in "errors" instead fails the build with those messages. Setting
// "external" ends the search and leaves the specifier as written in the output,
// which is how a plugin says "this import is somebody else's problem".
struct ResolveResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::string path;      // the resolved path, absolute
    bool external{};       // leave the import alone in the output
    bool side_effects{};   // true unless something vouched that the module is side-effect free
    std::string namespace_;
    std::string suffix;    // part of the module's identity but not its name, e.g. "?raw"
    std::any plugin_data;  // payload handed to the matching load hook
};

// What a plugin may report from its on_start callback. Adding an error here
// aborts the build before any file is read, which makes it the place to reject
// a whole configuration early and cheaply.
struct OnStartResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;
};

// What a plugin may report from its on_end callback. The BuildResult handed to
// the callback is still being assembled, so a plugin can add messages, and may
// also inspect the output files that were produced.
struct OnEndResult {
    std::vector<Message> errors;
    std::vector<Message> warnings;
};

// Which import paths a resolve callback should see, and in which namespace.
struct OnResolveOptions {
    std::string filter;    // a path pattern, or "" for every path
    std::string namespace_;
};

// The arguments of a single resolve callback invocation.
struct OnResolveArgs {
    std::string path;      // the specifier as written in the source
    std::string importer;  // the file containing the import
    std::string namespace_;
    std::string resolve_dir;
    ResolveKind kind{ResolveKind::kNone};
    std::any plugin_data;
    std::unordered_map<std::string, std::string> with;
};

// What a resolve callback returns. Filling in "path" claims the request and
// stops the search; leaving everything empty declines it and lets the next
// plugin try. "watch_files" and "watch_dirs" add paths that this answer
// depends on, so a change to them triggers a rebuild even though no module
// imports them.
//
// A few combinations are worth spelling out. "suffix" must start with "?" or
// "#", and it is part of the module's identity without being part of its name,
// so "img.png?v=2" can be a different module from "img.png?v=1" that resolves
// to the same file. "side_effects" only has an effect when it is kFalse, which
// promises that nothing is lost by dropping the module when no export of it is
// used. And anything returned alongside an empty path — namespace, suffix,
// plugin data, watch paths — is reported as a warning, because a claim about a
// path that was never claimed cannot be honoured.
struct OnResolveResult {
    std::string plugin_name; // set automatically to the owning plugin

    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::string path;
    bool external{};
    SideEffects side_effects{SideEffects::kTrue};
    std::string namespace_;
    std::string suffix;
    std::any plugin_data;

    std::vector<std::string> watch_files;
    std::vector<std::string> watch_dirs;
};

// Which paths a load callback should see, and in which namespace.
struct OnLoadOptions {
    std::string filter;    // a path pattern, or "" for every path
    std::string namespace_;
};

// The arguments of a single load callback invocation. "plugin_data" is
// whatever the matching resolve step left behind, which is how the two halves
// of a plugin pass state: the resolve step stores a token, and the load step
// recognises it.
struct OnLoadArgs {
    std::string path;
    std::string namespace_;
    std::string suffix;
    std::any plugin_data;
    std::unordered_map<std::string, std::string> with;
};

// What a load callback returns. Setting "contents" means the engine uses that
// string instead of reading the path, so a plugin can synthesise a module that
// never existed on disk; leaving it unset declines, and a declined load falls
// back to the file system. "resolve_dir" becomes the base for that module's
// own relative imports, and an empty one means "the directory the path is in".
// As with resolve, returning an error fails the load instead of declining it.
struct OnLoadResult {
    std::string plugin_name; // set automatically to the owning plugin

    std::vector<Message> errors;
    std::vector<Message> warnings;

    std::optional<std::string> contents;
    std::string resolve_dir;
    Loader loader{Loader::kNone};
    std::any plugin_data;

    std::vector<std::string> watch_files;
    std::vector<std::string> watch_dirs;
};

// Everything a plugin is given during setup. The function members are filled in
// by the engine before setup() is called; a plugin that does not need one can
// simply leave it alone.
struct PluginBuild {

    // The options the caller passed to Build() or Context(). Reading them lets
    // a plugin adapt to the build it is part of — to honour the outdir, to see
    // which loaders the caller configured, or to know whether the caller
    // wanted files written at all. The pointer is valid for as long as the
    // build lives; it is const, because a plugin changes what a build does
    // through its hooks rather than by editing the options behind the engine's
    // back.
    const BuildOptions* initial_options{};

    // Resolves a path immediately, from inside a hook, using the build's own
    // resolution rules and the same chain of resolve callbacks. This is the
    // escape hatch for a plugin that needs to know where an import points but
    // must not answer for it: ask the engine, then adjust the answer.
    //
    // Two rules make it safe to call: it cannot be used during setup, because
    // the build's options are not wired up yet, and "kind" must be stated
    // explicitly, because the call site is not an import statement.
    //
    // Input:  resolve("./helper.js", { importer = "src/index.js",
    //                                 namespace_ = "file",
    //                                 resolve_dir = "src",
    //                                 kind = kJSImportStatement })
    // Output: a ResolveResult whose "path" is the absolute resolved file and
    //         whose "plugin_data" is whatever that module's own resolve step
    //         stored. When nothing can resolve the path, the result carries an
    //         error whose notes list the candidates that came closest.
    std::function<ResolveResult(const std::string& path, const ResolveOptions& options)> resolve;

    // Registers a callback that runs at the very start of each build, before
    // any file has been read. All start callbacks run at the same time, each
    // on its own thread, and the build waits for every one of them before it
    // begins scanning, so they must not depend on each other's order or
    // results. An error returned from one aborts the build before any work is
    // done, which makes this the cheap place to reject a configuration.
    //
    // Input:  build.on_start([&]{ return OnStartResult{}; });
    // Output: nothing; the callback fires at the start of this build and of
    //         every later rebuild of the same context.
    std::function<void(std::function<OnStartResult()> callback)> on_start;

    // Registers a callback that runs at the end of each build, after the output
    // files have been produced but before they are written or returned. This is
    // where a plugin inspects the finished bundle, adds a warning about it, or
    // appends an extra output file of its own. End callbacks run one after
    // another in registration order, and each sees the changes the earlier ones
    // made to the result.
    //
    // Input:  build.on_end([&](BuildResult& r) {
    //             r.warnings.push_back(...);
    //             return OnEndResult{};
    //         });
    // Output: nothing; the BuildResult the caller receives carries whatever the
    //         callbacks added.
    std::function<void(std::function<OnEndResult(BuildResult&)> callback)> on_end;

    // Registers a resolve callback for import paths matching "filter" — a
    // pattern such as "*.png", a path prefix, or "" for everything — within an
    // optional namespace. Every matching callback is tried in the order the
    // plugins were registered, before any built-in resolution rule, and the
    // first one that returns a path settles the question for that import. The
    // remaining callbacks are not called.
    //
    // Two ways to get in trouble are worth knowing. Returning a path in the
    // "file" namespace that is not absolute is an error, not a hint, because
    // later stages need a complete path. Returning any error stops resolution
    // for that import outright; built-in rules are not consulted as a fallback.
    //
    // Input:  registered with filter = "virtual:*" and asked about
    //         "virtual:button" from "src/app.js"
    // Output: a result whose path = "/abs/generated/button.js" and whose
    //         plugin_data holds a token; that token is handed to the load step
    //         for the same path. An empty path declines, and normal resolution
    //         takes over. An empty path with external = true marks the specifier
    //         itself external, leaving the import statement untouched.
    std::function<void(const OnResolveOptions& options, std::function<OnResolveResult(const OnResolveArgs&)> callback)> on_resolve;

    // Registers a load callback for resolved paths matching "filter". This is
    // the step that produces the text the compiler sees, so it is where a
    // plugin substitutes content for a file, changes the loader, or changes
    // the directory that relative imports inside that file resolve against.
    //
    // Callbacks are tried in the same order as resolve, and the first one that
    // returns contents wins. Returning no contents declines. When every plugin
    // declines, a path in the "file" namespace is read from the file system as
    // usual; any other namespace has no contents and fails with "Do not know
    // how to load path", because only a plugin can say what such a path means.
    //
    // Input:  registered with filter = "/abs/generated/*.js" and asked about
    //         "/abs/generated/button.js" with plugin_data = 42
    // Output: contents = "export const Button = () => null;", loader = kJS;
    //         the engine parses that string instead of opening the path. A
    //         result that leaves the loader as kNone is loaded as kJS, and one
    //         that leaves abs_resolve_dir empty resolves relative imports
    //         against the file's own directory.
    std::function<void(const OnLoadOptions& options, std::function<OnLoadResult(const OnLoadArgs&)> callback)> on_load;

    // Registers a callback for when the build is torn down — the plugin's
    // chance to close files, flush caches or join helper threads. It runs
    // during Dispose(), which is the last thing that happens to a context, so
    // no build is in flight and no other hook can follow.
    //
    // Input:  build.on_dispose([]{ cache.Close(); });
    // Output: nothing; the callback runs once, when the context is disposed.
    std::function<void(std::function<void()> callback)> on_dispose;
};

// =============================================================================
// FormatMessages API
// =============================================================================

// How a batch of messages should be rendered. Width 0 means "do not wrap",
// which is usually what a log file or an editor wants; a positive width wraps
// the text and breaks the source snippet to fit.
struct FormatMessagesOptions {
    int terminal_width{};
    MessageKind kind{MessageKind::kError};
    bool color{};
};

// Renders diagnostics as text. The engine's own printing is optimised for a
// terminal it owns; this is the same rendering, returned as strings so another
// program can decide where they go — an editor's problems panel, a JSON
// response, a test's expected output.
//
// One string comes back per message, in the order given, so the mapping back
// to the input vector is positional.
//
// Input:  one Message whose text is "Expected \"}\" but found end of file",
//         location = { file = "src/app.js", line = 12, column = 4,
//                      line_text = "  function main() {" },
//         with terminal_width = 0, color = false, kind = kError
// Output: a single string, of this shape:
//           "✘ [ERROR] Expected \"}\" but found end of file\n" +
//           "\n" +
//           "    src/app.js:12:4:\n" +
//           "      12 │   function main() {\n" +
//           "          │   ^\n",
//         with no escape sequences anywhere in it.
//
// With color = true the same string carries ANSI escapes around the icon, the
// "[ERROR]" tag and the snippet, and the kind field decides whether that tag
// reads "ERROR" or "WARNING". A message with no location renders as the first
// line alone, and a message with notes gets each note appended as its own
// indented "NOTE" block.
std::vector<std::string> FormatMessages(
    const std::vector<Message>& msgs, const FormatMessagesOptions& options);

// =============================================================================
// AnalyzeMetafile API
// =============================================================================

// How the metafile summary should be rendered. "verbose" only changes the
// filler between the columns: it is true they are joined with a horizontal
// rule, and false they are padded with spaces. Either way the same rows are
// produced.
struct AnalyzeMetafileOptions {
    bool color{};
    bool verbose{}; // draw column separators as rules instead of blank padding
};

// Turns the metafile JSON produced by a build with "metafile" set into a
// human-readable size report. This is how a caller answers "why did my bundle
// grow" without writing a metafile parser of its own.
//
// The report is a two-level table, largest output first: each output file is
// one row, and the inputs that contributed bytes to it are listed underneath
// it with their share of that output. Source maps are left out — the ".map"
// entries in the metafile describe the maps, not the program, and they would
// otherwise dominate the numbers.
//
// Input:  the metafile string from a BuildResult, with verbose = true
// Output: a table shaped like this (sizes are abbreviated, shares rounded):
//
//             dist/app.js ─────── 241.3kb ── 100.0%
//              ├ src/app.js ─────── 2.1kb ──  0.9%
//              └ node_modules/x/index.js 239.2kb ─ 99.1%
//
//           which reads as "almost all of dist/app.js is one dependency".
//
// Input:  text that is not a metafile, or a metafile with no usable outputs
// Output: an empty string — a bad document is not worth an exception, because
//         the metafile is data the caller may have truncated or rewritten.
std::string AnalyzeMetafile(
    const std::string& metafile, const AnalyzeMetafileOptions& options);

// Removes a leading directory from a path when the path really is inside that
// directory, and reports whether it did. The check is textual, so it works for
// absolute paths, for output paths that do not exist yet, and for the
// URL-style paths the server sees. Only a boundary counts as a match: a prefix
// that stops in the middle of a name is rejected, otherwise "/project" would
// wrongly match "/projectile". An empty prefix leaves the path alone, and a
// path equal to the prefix strips down to the empty string.
//
// "separators" names the characters that separate path components, so a caller
// on a system with more than one kind passes both. Nothing is normalised here:
// a Windows path needs its backslashes listed, and the caller decides which
// form it wants back.
//
// Input:  path = "/project/src/app.js", prefix = "/project", separators = "/"
// Output: true, with out = "src/app.js"
// Input:  path = "/project/src", prefix = "/project/", separators = "/"
// Output: true, with out = "src" (the prefix already ended in a separator)
// Input:  path = "/projectile/app.js", prefix = "/project", separators = "/"
// Output: false, and out is cleared
bool strip_dir_prefix(const std::string& path, const std::string& prefix,
                      std::string_view separators, std::string& out);

// Splits an output-name template such as "chunks/[name]-[hash].js" into the
// segments the writer walks when it decides output names. Parsing happens once,
// up front, so the hot path is a sequence of cheap appends instead of repeated
// pattern matching.
//
// Each resulting segment is "the literal text that came before a placeholder,
// together with the placeholder that follows it" — the placeholder is glued to
// the text it is appended after, and a trailing run of literal text with no
// placeholder after it becomes one final segment. The template is read as if
// it were written relative to "./", so the leading directory segment is always
// part of the first entry.
//
// An empty template yields an empty vector, which the writer reads as "use the
// default name". A "[" that does not begin one of the four known placeholders
// is literal text, so a value that is not a template at all still round-trips.
//
// Input:  value = "chunks/[name]-[hash].js"
// Output: three segments, in order:
//           { Data = "./chunks/", Placeholder = kName }
//           { Data = "-",         Placeholder = kHash }
//           { Data = ".js",       Placeholder = kNoPlaceholder }
//
// Input:  value = "bundle.js"   (no placeholders)
// Output: { Data = "./bundle.js", Placeholder = kNoPlaceholder }
//
// Input:  value = ""            (nothing was configured)
// Output: an empty vector.
std::vector<config::PathTemplate> validate_path_template(std::string_view value);



// =============================================================================
// Serve API
// =============================================================================

// How the server obtains output for a request. The server calls it for every
// request that needs the latest build, and the function is expected to return
// either the result of the build that is already running or a freshly
// completed one. In practice this is a context's Rebuild(), which is why
// asking for a page in a browser after saving a file returns the new output
// without any extra bookkeeping: the server itself decides when a rebuild is
// needed.
using RebuildFn = std::function<BuildResult()>;

// Starts the development server and returns as soon as it is accepting
// connections. This is the primitive that BuildContext::Serve is built on, and
// it can be used directly when the caller wants the server without a build
// context — a proxy in front of another bundler, for instance, or a test that
// needs a real socket.
//
// The socket is bound before this returns, so the port in the result is final
// and the first request will not lose a race. Requests are then served on
// background threads until result.stop() closes the listener and the open
// event streams. At log_level kInfo the reachable URLs are printed as soon as
// the server is up, which is the only output this function produces itself.
//
// Failures are reported through "error" rather than thrown, matching the rest
// of the API: a port that cannot be bound, a half-specified TLS pair, a servedir
// or fallback that cannot be turned into an absolute path, or a CORS origin with
// more than one "*" in it all come back as a message and an empty ServeResult.
// The servedir and fallback are made absolute against the filesystem's working
// directory, so a relative value means the same thing here as it would in the
// build that produced the files.
//
// Asking for port 0 does not mean "any free port" straight away: the 8000-8009
// range is tried first, so a run finds the same port again instead of changing
// every time, and only then does the operating system pick one for us.
//
// Input:  rebuild = a BuildContext's Rebuild(), options = { port = 0,
//         servedir = "dist" }, log_level = kInfo, use_color = kColorIfTerminal
// Output: a ServeResult with the port that was bound and a stop callback, and
//         nothing in "error". The caller must eventually invoke result.stop()
//         to release the socket and the worker threads.
//
// The in-repo server speaks plain HTTP only: providing keyfile and certfile is
// rejected as unsupported, and providing just one of the two is a different
// error again, because the pair is checked before the feature is refused.
ServeResult Serve(
    filesystem::Fs&       fs,
    RebuildFn             rebuild,
    const ServeOptions&   options,
    logger::LogLevel      log_level,
    logger::UseColor      use_color,
    std::string&          error);

// =============================================================================
// Watch API
// =============================================================================
//
// The watcher is a polling loop, not an operating system notification client.
// Polling is what makes it behave the same on every platform and with the
// in-memory filesystem the tests use, and the tuning constants below are what
// keep the polling cheap enough to be invisible next to a build.
//
// The strategy has two tiers. Paths that changed recently are re-checked every
// single tick, because the file a developer just saved is overwhelmingly the
// one they are about to save again. Everything else is visited in a shuffled
// round-robin, so the whole watch list is covered at a predictable cadence
// while each tick only touches a slice of it.

// How long the loop sleeps between ticks. Every wake-up costs one full pass
// over the recent paths, so this is also the floor on detection latency for
// a file the developer is actively editing.
inline constexpr std::chrono::milliseconds kWatchIntervalSleep{100};

// How many recently-changed paths are remembered. They are re-checked on every
// tick, and one that changes again moves to the back of the list, so a file
// being actively edited keeps its place. The list is a fixed-size window: once
// it is full, the oldest entry is dropped and that path goes back to being
// visited by the round-robin like any other.
inline constexpr std::size_t kMaxRecentItemCount = 16;

// The floor on how many paths a single tick examines. Small watch lists are
// checked in one go rather than being spread over many ticks, so a typical
// project reacts immediately.
inline constexpr std::size_t kMinItemCountPerIter = 64;

// The target number of ticks a full pass over the watch list should take. The
// number of paths checked per tick is derived from it: the list is divided into
// this many slices, each slice sized so that the round-robin still finishes in
// that many ticks. At the default tick of 100ms this caps the worst-case
// detection latency for a long-idle file at about two seconds.
inline constexpr std::size_t kMaxIntervalsBeforeUpdate = 20;

// Polls the file system for changes among the paths the last build reported
// and triggers a rebuild whenever one is found.
//
// The object owns a thread, so it is created, started and stopped explicitly;
// nothing here begins polling on its own. A Watcher is normally owned by a
// BuildContext, but it is usable on its own: give it a filesystem, a function
// that performs a build and returns the new watch data, and it will drive that
// function on every change.
class Watcher {
public:
    // fs         - the filesystem to poll through, so a test can watch an
    //              in-memory tree instead of the disk.
    // rebuild    - called on the watcher thread after a change is found; it
    //              must run the build and return the paths that build read, so
    //              the next tick watches the new graph. It runs on no other
    //              thread, and must not call back into the Watcher.
    // delay_in_ms- how long to wait after noticing a change, so a burst of
    //              saves causes one rebuild. Zero means rebuild at once.
    // should_log - whether to print the "[watch]" progress lines.
    // use_color  - whether those lines may contain ANSI escapes.
    // path_style - how paths are rendered in those lines (relative or absolute).
    Watcher(
        filesystem::Fs&                                             fs,
        std::function<filesystem::WatchData()>                      rebuild,
        std::chrono::milliseconds                                   delay_in_ms,
        bool                                                        should_log,
        logger::UseColor                                            use_color,
        logger::PathStyle                                           path_style);

    // Replaces the watch list with the one the latest build produced. Call
    // this after every build, including the first: it is what makes the
    // watcher follow the graph as it changes, dropping paths that are no
    // longer part of the build and adding the ones that just became part of
    // it. Any path that was recently changed and is still present keeps its
    // place in the recent list, and the shuffled remainder is thrown away so
    // the next pass starts from the new list rather than finishing the old one.
    //
    // When logging is on, the first call also prints the "[watch] build
    // finished, watching for changes" line, which is the point at which the
    // watcher is genuinely ready to answer.
    //
    // Input:  the WatchData returned by the previous build
    // Output: nothing; the next tick polls the new set of paths.
    void SetWatchData(filesystem::WatchData data);

    // Starts the polling thread. Call it exactly once per Watcher: the thread
    // is stored by assignment, so a second Start() while the first thread is
    // still running ends the process rather than merely confusing it. A
    // Watcher cannot be restarted after Stop() either, because the stop flag
    // stays set and the new thread would exit at once.
    void Start();

    // Asks the thread to finish and waits for it. The current tick completes
    // first, so a change noticed during the final pass can still trigger one
    // last rebuild — stopping a watcher is a request to wrap up, not an
    // abort. The wait covers the settle delay and the rebuild as well, so a
    // build already under way is never abandoned half-finished. Safe to call
    // when the thread was never started, and safe to call twice.
    void Stop();

private:
    // Checks the watch list for something that changed and returns the path
    // that changed, or an empty string when nothing did. At most one path is
    // reported per call, because one rebuild answers for all of them. For a
    // file the returned path is the file itself; for a watched directory it may
    // be the directory or the child that was actually modified. The recent list
    // is checked first and the first hit ends the call, then one slice of the
    // remaining paths is taken off the shuffled scan list. Must be called with
    // "mutex_" held; refills and re-shuffles the scan list when that list has
    // been exhausted, and marks a path that changed as recently changed.
    std::string TryToFindDirtyPath();

    // The paths to poll, each paired with a closure that reports whether that
    // entry changed and, if so, which path inside it did.
    filesystem::WatchData              data_;
    filesystem::Fs&                    fs_;
    std::function<filesystem::WatchData()> rebuild_;
    std::chrono::milliseconds          delay_in_ms_;
    std::vector<std::string>           recent_items_;  // re-checked on every tick
    std::vector<std::string>           items_to_scan_; // the shuffled remainder
    std::mutex                         mutex_;
    std::size_t                        items_per_iteration_ = 0;
    std::atomic<bool>                  should_stop_{false};
    bool                               should_log_ = false;
    logger::UseColor                   use_color_{};
    logger::PathStyle                  path_style_{};

    // Thread bookkeeping: "thread_" is the polling loop, and the trio below is
    // what lets Stop() block until that loop has actually unwound rather than
    // merely having been asked to. "running_" is guarded by "stop_mutex_" and
    // cleared by the thread itself just before it returns.
    std::thread               thread_;
    std::mutex                stop_mutex_;
    std::condition_variable   stop_cv_;
    bool                      running_ = false;
};


// =============================================================================
// Internal implementation functions (implemented in api_impl.cpp)
// =============================================================================
//
// The public entry points above are one-line forwarders to these; the split
// keeps the wrappers in api.cpp down to a name and lets the implementations
// take their arguments the way they need them. They are declared here, in the
// same namespace as everything else, because a caller that wants the
// implementation without the wrapper can have it — "create_build_context" takes
// its options by value, for instance, while the public "Context" takes a
// reference. Nothing inside the engine outside src/api calls them today.

// Runs the whole build for a one-shot Build(): validates the options, creates a
// context, runs exactly one build, prints the file summary to the log stream
// when the log level allows it, and disposes the context before returning. The
// summary is skipped when the output is going to stdout, because the terminal
// is already carrying the build's own output then.
//
// Input:  the same options Build() takes
// Output: the single BuildResult, exactly as Build() would return it. Option
//         validation failures are placed in result.errors with no context
//         created at all.
BuildResult build_impl(const BuildOptions& options);

// Builds the object that BuildContext is an interface to. Options are
// validated here, which is the one step that can fail before any file is
// touched, and the validated configuration is then retained by the context so
// later rebuilds skip this work.
//
// Plugin setup runs before validation, because plugins are allowed to reject a
// configuration themselves. A plugin that needs to read the finished options —
// the resolved platform, the entry point list — gets them from
// PluginBuild::initial_options during the first build instead.
//
// Input:  options (taken by value, since validation normalises them)
// Output: a context, or nullptr with "errors" filled in. Unlike build_impl
//         this does not run a build, so nothing is logged and no file is
//         produced until Rebuild() is called.
std::unique_ptr<BuildContext> create_build_context(
    BuildOptions build_options, std::vector<Message>& errors);

// The real implementation behind Transform(). It runs the same parser, printer
// and minifier the bundler uses, so a snippet accepted here is also accepted
// inside a bundle, and the source arrives as a synthetic stdin entry rather
// than through a file: no file is read and none is written. Diagnostics do go
// to the log, exactly as they would during a build.
//
// The result is assembled from the output files by name, so "code" is the
// output itself, "map" is the same path with ".map" appended, and
// "legal_comments" is that path with ".LEGAL.txt" appended. Asking for a
// linked source map or linked legal comments is an error here, since there is
// no file to link from.
//
// Input:  source text plus TransformOptions
// Output: a TransformResult whose "code" is the rewritten source, plus "map"
//         and "legal_comments" when they were requested. Parse failures land
//         in result.errors and "code" stays empty.
TransformResult transform_impl(const std::string& input,
                               const TransformOptions& options);

// Renders a batch of messages exactly as FormatMessages does: each message is
// converted back into the engine's own message type and printed with the
// source snippet included, so a caller that formats messages itself gets the
// same text the engine would have printed.
//
// Input:  messages, a target width, a severity and a colour choice
// Output: one rendered string per message, in the same order.
std::vector<std::string> format_msgs_impl(
    const std::vector<Message>& msgs,
    const FormatMessagesOptions& options);

// Parses the metafile JSON and renders the size report described by
// AnalyzeMetafile. Returns an empty string when the text is not a metafile or
// when it cannot be parsed, because the metafile is an output artefact the
// caller may have filtered, truncated or generated itself.
//
// Input:  the metafile JSON string
// Output: the printable report, or "" for input that is not a metafile.
std::string analyze_metafile_impl(const std::string& metafile,
                                  const AnalyzeMetafileOptions& options);

} // namespace guchho::api
