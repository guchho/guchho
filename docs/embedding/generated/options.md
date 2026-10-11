---
title: Option structs
description: The plain structs that describe a build, a transform, a server and a watcher.
---

<!-- GENERATED from include/guchho/api.hpp and include/guchho/cli.hpp by scripts/gen-docs-embedding.mjs. Do not edit by hand. -->

# Option structs

Configuration is input only. These structs have defaults for every member, so a caller names just the fields it cares about.

In this page:

- [`BuildOptions`](#buildoptions)
- [`TransformOptions`](#transformoptions)
- [`ServeOptions`](#serveoptions)
- [`WatchOptions`](#watchoptions)
- [`FormatMessagesOptions`](#formatmessagesoptions)
- [`AnalyzeMetafileOptions`](#analyzemetafileoptions)
- [`OnResolveOptions`](#onresolveoptions)
- [`OnLoadOptions`](#onloadoptions)
- [`ResolveOptions`](#resolveoptions)
- [`MangleCacheResult`](#manglecacheresult)
- [`ErrorWithNote`](#errorwithnote)
- [`ParseOptionsExtras`](#parseoptionsextras)

## BuildOptions

Everything a build can be told. The members are grouped by the phase they
affect, and the blank lines between groups mark those boundaries.

```cpp
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
    // All three default to off and are asked for by name: minification is a
    // choice about the bytes a build leaves behind, and a person who has not
    // made it should get output they can still read. "--minify" on a command
    // line is the shorthand for all three, and each of them keeps working on its
    // own, so a caller that wants two passes says two.
    // The flag is not shared with Format, because the wrapper's own quoting and
    // interop constraints are not negotiable.
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

    // -- Markup output ------------------------------------------------------
    // HTML is re-emitted from the parsed tree, so both of these describe the
    // printer rather than the file. "pretty" indents the tree on output and is
    // on by default: the result is meant to be opened, read and edited, and a
    // single line of markup is none of those. "minify_html" collapses it, and
    // is off by default because the two are opposites and only one can be the
    // unasked-for behaviour. Neither touches content that whitespace is
    // meaningful in, so turning either on is always safe.
    bool     pretty{true};
    bool     minify_html{};

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
    //
    // "exports" chooses which entry export the bundle publishes: kUnset
    // keeps the namespace object, kDefault publishes the entry's default
    // export directly, kNamed is the namespace object spelled the 
    // way, kNone publishes nothing, and kAuto lets the entry's actual
    // exports decide. It is spelled "--exports=<value>" on the command line
    // and "exports: '<value>'" through the API or a guchho.json "build"
    // object, and it is read for every output format.
    bool        extend{};
    bool        no_conflict{};
    bool        strict{true};
    std::unordered_map<std::string, std::string> globals;
    Exports     exports{Exports::kUnset};

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

    // Which of the above somebody actually said out loud. Filled in by the
    // command-line parser and consumed by ResolveEffectiveBuildConfigs; null
    // means the struct was assembled by hand and every value in it is
    // deliberate. See ExplicitlySet for why this is needed at all.
    std::shared_ptr<const ExplicitlySet> explicit_set;

    // Whether "format" was named by the user or the config file rather than
    // arriving as a built-in default. Filled in by ResolveEffectiveBuildConfigs,
    // which is the one place the command line's "explicit_set", the config
    // file's "FormatFromConfig" and the built-in "esm" default are all visible
    // at once. The HTML-entry warnings consume it to tell an explicit format
    // (which is ignored for HTML output) from the default (which is not a
    // mistake). It only means anything after a resolution has run.
    bool format_was_explicit{};
};
```

## TransformOptions

The per-file subset of BuildOptions. Options that only make sense for a
graph — entry points, outdir, splitting, external, plugin hooks, and the
module-resolution fields — are absent by construction, so a transform can
never accidentally depend on them.

```cpp
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
```

## ServeOptions

How to configure the development server. Port 0 asks the system for any free
port, and the port that was actually bound is reported back in
ServeResult::port.

Every GET or HEAD request produces the current build first, so a reload
always shows the latest output. The requested path is then looked for in
three places, in order: inside the build's output directory (served straight
from memory, whether or not the build wrote anything to disk), then inside
"servedir", and finally in the file named by "fallback" — which is how a
single-page app gets its own index document back for any unknown route. A
build that produced errors answers with status 503 and the error text as the
body, so a mistake shows up in the browser instead of silently serving the
previous build.

```cpp
struct ServeOptions {
    uint16_t port{};
    std::string host; // interface to bind; empty means every interface

    std::string servedir;  // directory of static files to fall back to
    std::string keyfile;   // must be set together with certfile
    std::string certfile;  // must be set together with keyfile
    std::string fallback;  // served when nothing else matches

    // Turns the server into a live-reload host. While it is on, every HTML
    // response is given a script tag pointing at the reserved "/guchho.js"
    // path, and that script keeps an event stream open to the server; when a
    // rebuild changes the output, the page reloads itself. Off by default:
    // "serve" is also used for looking at a build without a browser attached,
    // and injecting into responses is only wanted when a person is watching
    // one. A caller that rebuilds out of band — a watch session driving the
    // server — tells it a rebuild happened through ServeResult::notify.
    bool live_reload{};

    CORSOptions cors;

    // Invoked once per request, after the response has been produced, with the
    // method, path, status and how long the response took to generate. A good
    // place to count requests, or to log anything the server itself did not
    // handle.
    std::function<void(const ServeOnRequestArgs&)> on_request;
};
```

## WatchOptions

How long to wait after a change is noticed before rebuilding. The delay
exists so a burst of editor writes (save, format, save again) produces one
rebuild instead of three. A delay of 0 rebuilds immediately.

```cpp
struct WatchOptions {
    int delay{};
};
```

## FormatMessagesOptions

How a batch of messages should be rendered. Width 0 means "do not wrap",
which is usually what a log file or an editor wants; a positive width wraps
the text and breaks the source snippet to fit.

```cpp
struct FormatMessagesOptions {
    int terminal_width{};
    MessageKind kind{MessageKind::kError};
    bool color{};
};
```

## AnalyzeMetafileOptions

How the metafile summary should be rendered. "verbose" only changes the
filler between the columns: it is true they are joined with a horizontal
rule, and false they are padded with spaces. Either way the same rows are
produced.

```cpp
struct AnalyzeMetafileOptions {
    bool color{};
    bool verbose{}; // draw column separators as rules instead of blank padding
};
```

## OnResolveOptions

Which import paths a resolve callback should see, and in which namespace.

```cpp
struct OnResolveOptions {
    std::string filter;    // a path pattern, or "" for every path
    std::string namespace_;
};
```

## OnLoadOptions

Which paths a load callback should see, and in which namespace.

```cpp
struct OnLoadOptions {
    std::string filter;    // a path pattern, or "" for every path
    std::string namespace_;
};
```

## ResolveOptions

The request handed to PluginBuild::resolve when a plugin resolves a path
itself. It mirrors what a callback registered with on_resolve would receive,
so one piece of resolution logic can be used either way. Only "kind" is
mandatory; the other fields describe the situation, and a plugin that leaves
them empty is asking its question with as little context as it can manage.

```cpp
struct ResolveOptions {
    std::string plugin_name; // names the plugin in a resolution error
    std::string importer;    // the file that asked for it, empty for an entry point
    std::string namespace_;  // virtual filesystem the request came from
    std::string resolve_dir; // directory relative paths resolve against
    ResolveKind kind{ResolveKind::kNone};
    std::any plugin_data;                        // payload from the previous step
    std::unordered_map<std::string, std::string> with; // values passed via "with"
};
```

## MangleCacheResult

The file as it was read, in the two shapes the build needs.

```cpp
struct MangleCacheResult {
        // Original property name -> the short name chosen for it. An empty
        // optional is a property that is deliberately left alone: the file
        // recorded "false" for it, and shortening it would break whatever
        // outside the bundle reads it by name — a template, a configuration
        // payload, a test that reflects over the result.
        std::unordered_map<std::string, std::optional<std::string>> cache;

        // The same keys in the order the file listed them. The map cannot carry
        // that, and the order is what keeps the rewritten file a small diff
        // instead of a reshuffle on every build.
        std::vector<std::string> order;
    };
```

## ErrorWithNote

Two part command line errors

A rejected flag is only half a message on its own. "Invalid build flag:
\"-o\"" says that something is wrong; the note is what lets a user fix
it, because it names the spelling that does work. The two travel
together so a caller can print the advice only when there is some, and
keep the complaint itself free of it.

```cpp
struct ErrorWithNote {
        // The complaint, always present.
        std::string text;

        // How to fix it, and often empty: there is nothing useful to suggest
        // for a value that is simply not a number.
        std::string note;
    };
```

## ParseOptionsExtras

The three settings a build needs that are not part of api::BuildOptions
because they describe the run rather than the build: watching, and the
two files the run is asked to write alongside the output.

```cpp
struct ParseOptionsExtras {
        bool watch{};
        int watch_delay{};
        std::optional<std::string> metafile;
        std::optional<std::string> mangle_cache;
    };
```
