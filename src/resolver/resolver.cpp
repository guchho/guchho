// This file implements Guchho's module resolver: the component that turns a
// bare import specifier (such as "lodash", "./util.js", or "/abs/path") into
// the absolute on-disk path a bundle will actually load. Resolution follows a
// layered set of rules:
//
//   - Relative and absolute specifiers are probed against the filesystem as
//     files (with extension candidates) and then as directories (with "main"
//     fields and index files).
//   - Package specifiers are located by walking outward through every
//     enclosing node_modules directory, or rerouted through a Yarn Plug'n'Play
//     manifest when the importer lives under one.
//   - package.json "main"/"module"/"browser" fields, browser remap maps,
//     "exports"/"imports" maps, and the path remapping supplied by a project's
//     compiler configuration all shape the search.
//   - Node built-ins and explicitly external paths short-circuit immediately.
//
// The public surface is Resolver::Resolve (a single import), Resolver::
// ResolveGlob (a wildcard pattern), and the entry-point probe used by the
// bundler; everything below is the machinery those three rely on.

#include "guchho/resolver.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>
#include <utility>

#include "guchho/compat.hpp"
#include "guchho/config.hpp"
#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"

namespace guchho::resolver {

    namespace {

        // The helpers below are stateless utilities shared by every query in
        // this file. They live outside the resolver class on purpose: keeping
        // them free-standing makes them reusable from more than one resolution
        // step and keeps each query small. Several of them exist purely to
        // keep the debug trace readable, because the resolver narrates every
        // decision it makes when verbose logging is switched on.

        // Wraps a value in double quotes the way JSON would, escaping any
        // characters that would break out of the string. Used everywhere a
        // path or specifier is interpolated into a debug note or log message
        // so the trace stays unambiguous.
        //
        //   Input:  lodash      ->  "lodash"
        //   Input:  say "hi"    ->  "say \"hi\""
        std::string Q(std::string_view text)
        {
            return helpers::QuoteForJSON(text, false);
        }

        // Coerces a failed filesystem operation into one readable string. The
        // filesystem layer records failure on two channels: the original
        // message (often platform-specific and wordy) and a canonical error
        // code. This helper prefers the original text and falls back to a
        // generic label when even that is empty.
        //
        //   Input:  a result carrying errc::no_such_file_or_directory
        //   Output: "No such file or directory" (or the host's wording)
        template <typename T>
        std::string ErrorText(const filesystem::FsResult<T>& res)
        {
            if (!res.original_error.empty()) {
                return res.original_error;
            }
            if (res.canonical_error) {
                return std::make_error_code(*res.canonical_error).message();
            }
            return "unknown error";
        }

        // True when the failure simply means the path does not exist. Callers
        // treat this as a benign miss and quietly move on to the next
        // candidate rather than surface an error.
        bool IsENOENT(const std::optional<std::errc>& error)
        {
            return error == std::errc::no_such_file_or_directory;
        }

        // True when a path component turned out to be a regular file instead
        // of a directory. Like a missing path, this is a benign miss.
        bool IsENOTDIR(const std::optional<std::errc>& error)
        {
            return error == std::errc::not_a_directory;
        }

        // True when the operating system refused access. Directory reads that
        // fail this way degrade to an empty listing so resolution can continue
        // with whatever entries are still visible.
        bool IsEACCESOrEPERM(const std::optional<std::errc>& error)
        {
            return error == std::errc::permission_denied;
        }

        std::string QuoteRegexMeta(std::string_view text)
        {
            // Escapes a literal string so it can be pasted verbatim into a
            // std::regex using the default ECMAScript grammar. Only characters
            // that are meaningful to the regex engine are backslash-prefixed;
            // escaping anything else would be an invalid "identity escape"
            // that makes std::regex throw. Callers compose their expressions
            // out of pieces escaped this way, so a stray metacharacter inside
            // user-supplied text can never change what the pattern matches.
            //
            //   Input:  "a+b"    ->  "a\+b"
            //   Input:  "a.b"    ->  "a\.b"
            std::string escaped;
            escaped.reserve(text.size());
            for (char c : text) {
                if (strchr("\\^$.|?*+()[]{}", c) != nullptr) {
                    escaped += '\\';
                    escaped += c;
                } else {
                    escaped += c;
                }
            }
            return escaped;
        }

        // Case-insensitive end-of-string comparison, used where a file extension
        // or marker must be recognised no matter which case the author used.
        // The tails are folded with the locale-independent lowercase mapping
        // character by character.
        //
        //   Input:  s = "README.MD", suffix = ".md"  ->  true
        //   Input:  s = "readme", suffix = ".md"     ->  false
        bool HasCaseInsensitiveSuffix(std::string_view s, std::string_view suffix)
        {
            return s.size() >= suffix.size() &&
                   std::equal(suffix.begin(), suffix.end(), s.end() - static_cast<long>(suffix.size()),
                              [](char a, char b) {
                                  return std::tolower(static_cast<unsigned char>(a)) ==
                                         std::tolower(static_cast<unsigned char>(b));
                              });
        }

        // Substitutes the first occurrence of "*" in a template with a replacement
        // value, leaving any later asterisks untouched. This backs the pattern
        // remapping rules where a key like "src/*" is turned into a concrete
        // path by dropping in whatever text actually matched the wildcard.
        //
        //   Input:  text = "src/*", replacement = "util"  ->  "src/util"
        //   Input:  text = "plain" (no star)              ->  "plain"
        std::string ReplaceFirstStar(std::string text, std::string_view replacement)
        {
            size_t star = text.find('*');
            if (star != std::string::npos) {
                text.replace(star, 1, replacement);
            }
            return text;
        }

        // Joins dot-separated identifier fragments into a single string, inserting
        // "." between them. Used to render the dotted name of a JSX factory
        // (for example the factory expressed as the pair ["preact", "h"]).
        //
        //   Input:  {"preact", "h"}  ->  "preact.h"
        //   Input:  {"React"}        ->  "React"
        std::string JoinWithDot(const std::vector<std::string>& parts)
        {
            std::string joined;
            for (size_t i = 0; i < parts.size(); i++) {
                if (i > 0) {
                    joined += '.';
                }
                joined += parts[i];
            }
            return joined;
        }

    } // namespace

    ////////////////////////////////////////////////////////////////////////////////
    // Standalone helper functions

    // The configuration-independent part of "main" field selection: this returns
    // the default list of package.json fields consulted when the user never
    // configured any, keyed by the target platform. Order matters -- the first
    // field that names a real file wins. For browsers, the module-oriented
    // "module" field is preferred over the classic "main" field; for Node the
    // preference is reversed so the CommonJS entry point stays the default.
    // The neutral platform has no defaults at all, since there is no runtime
    // to optimise the choice for.
    //
    //   Input:  config::Platform::kBrowser
    //   Output: {"browser", "module", "main"}
    //   Input:  config::Platform::kNode
    //   Output: {"main", "module"}
    //   Input:  config::Platform::kNeutral
    //   Output: {}
    const std::vector<std::string>& DefaultMainFields(config::Platform platform)
    {
        static const std::vector<std::string> browser = {"browser", "module", "main"};
        static const std::vector<std::string> node    = {"main", "module"};
        static const std::vector<std::string> neutral = {};
        switch (platform) {
            case config::Platform::kBrowser: return browser;
            case config::Platform::kNode: return node;
            default: return neutral;
        }
    }

    // The reference list of main fields used when resolution reports why it
    // failed. It is ordered only to decide which field to complain about: the
    // first one the package actually declares becomes the subject of the
    // "this field was ignored" note.
    //
    //   Output: {"main", "module", "browser"}
    const std::vector<std::string>& MainFieldsForFailure()
    {
        static const std::vector<std::string> fields = {"main", "module", "browser"};
        return fields;
    }

    // Maps a plain JavaScript extension to the type-annotated extensions that
    // should be probed in its place when a file cannot be found under its
    // given name. This is what keeps imports working when the code on disk is
    // written with type annotations but the importer refers to the plain
    // spelling. The table is consulted only after the exact name and the
    // ordinary extension order have both failed.
    //
    //   Input:  base = "util.js"
    //   Output: retries "util" joined with each replacement extension listed
    //           for the ".js" entry
    const std::map<std::string, std::vector<std::string>>& RewrittenFileExtensions()
    {
        static const std::map<std::string, std::vector<std::string>> extensions = {
                {".js", {".ts", ".tsx"}},
                {".jsx", {".ts", ".tsx"}},
                {".mjs", {".mts"}},
                {".cjs", {".cts"}},
        };
        return extensions;
    }

    // Reports whether a bare specifier names a module that ships with Node
    // itself. When the output platform is Node these names must never resolve
    // to anything on disk: they stay external and pass through to the runtime
    // verbatim. The list below is the standard Node built-in vocabulary,
    // including the grouped subpaths ("fs/promises", "path/posix", ...) that
    // Node exposes.
    //
    //   Input:  "fs"          ->  true
    //   Input:  "path/posix"  ->  true
    //   Input:  "lodash"      ->  false
    bool IsBuiltInNodeModule(std::string_view name)
    {
        static const std::unordered_set<std::string_view> modules = {
                "_http_agent",
                "_http_client",
                "_http_common",
                "_http_incoming",
                "_http_outgoing",
                "_http_server",
                "_stream_duplex",
                "_stream_passthrough",
                "_stream_readable",
                "_stream_transform",
                "_stream_wrap",
                "_stream_writable",
                "_tls_common",
                "_tls_wrap",
                "assert",
                "assert/strict",
                "async_hooks",
                "buffer",
                "child_process",
                "cluster",
                "console",
                "constants",
                "crypto",
                "dgram",
                "diagnostics_channel",
                "dns",
                "dns/promises",
                "domain",
                "events",
                "fs",
                "fs/promises",
                "http",
                "http2",
                "https",
                "inspector",
                "module",
                "net",
                "os",
                "path",
                "path/posix",
                "path/win32",
                "perf_hooks",
                "process",
                "punycode",
                "querystring",
                "readline",
                "repl",
                "stream",
                "stream/consumers",
                "stream/promises",
                "stream/web",
                "string_decoder",
                "sys",
                "timers",
                "timers/promises",
                "tls",
                "trace_events",
                "tty",
                "url",
                "util",
                "util/types",
                "v8",
                "vm",
                "wasi",
                "worker_threads",
                "zlib",
        };
        return modules.count(name) > 0;
    }

    // Renders a resolved path for human-facing diagnostics in two forms at once:
    // an absolute rendering and a relative one, leaving the caller free to pick
    // whichever the configured path style prefers. File-namespace paths are
    // relativised against the working directory and normalised to forward
    // slashes; paths in other namespaces are prefixed with their namespace
    // name. Disabled paths carry a "(disabled)" marker, and any ignored suffix
    // that was stripped during resolution (such as a query string) is joined
    // back onto both renderings.
    //
    //   Input:  path = "C:\\app\\src\\index.js", namespace "file",
    //           cwd = "C:\\app"
    //   Output: abs = "C:\\app\\src\\index.js", rel = "src/index.js"
    logger::PrettyPaths MakePrettyPaths(filesystem::Fs& fs, const logger::Path& path)
    {
        std::string abs_path = path.text;
        std::string rel_path = path.text;

        if (path.namespace_ == "file") {
            if (std::optional<std::string> rel = fs.Rel(fs.Cwd(), rel_path)) {
                rel_path = *rel;
            }

            std::replace(rel_path.begin(), rel_path.end(), '\\', '/');
        } else if (!path.namespace_.empty()) {
            abs_path = path.namespace_ + ":" + abs_path;
            rel_path = path.namespace_ + ":" + rel_path;
        }

        if (path.IsDisabled()) {
            abs_path = "(disabled):" + abs_path;
            rel_path = "(disabled):" + rel_path;
        }

        logger::PrettyPaths pretty;
        pretty.abs = abs_path + path.ignored_suffix;
        pretty.rel = rel_path + path.ignored_suffix;
        return pretty;
    }

    // Turns one resolver failure into a fully-formed log message and queues it.
    // The message body is the primary error text anchored to the offending
    // range; notes gathered while resolving are appended in front of the
    // caller's extra notes. A suggestion rides on the message either for the
    // whole range or just its closing position, so the terminal can underline
    // the proposed fix. When no source file is available the ranges are
    // dropped and the message carries only its text.
    //
    //   Input:  text = "Could not resolve \"foo\"", r = the range of "foo"
    //   Output: a kError message queued on the log with notes attached
    void DebugMeta::LogErrorMsg(logger::Log&                          log,
                                const logger::Source*                 source,
                                logger::Range                         r,
                                const std::string&                    text,
                                const std::string&                    suggestion,
                                const std::vector<logger::MsgData>&   extra_notes) const
    {
        auto make_data = [&](logger::Range range, const std::string& msg_text) -> logger::MsgData {
            if (source != nullptr) {
                logger::LineColumnTracker tracker(source);
                return tracker.MakeMsgData(range, msg_text);
            }
            logger::MsgData data;
            data.text = msg_text;
            return data;
        };

        std::vector<logger::MsgData> notes_data = this->notes;

        if (source != nullptr && !suggestion_message.empty()) {
            logger::Range msg_suggestion_range = r;
            if (suggestion_range == SuggestionRange::kEnd) {
                msg_suggestion_range = logger::Range{.loc = logger::Loc{.start = r.End() - 1}};
            }
            logger::MsgData data = make_data(msg_suggestion_range, suggestion_message);
            if (data.location) {
                data.location->suggestion = suggestion_text;
            }
            notes_data.push_back(std::move(data));
        }

        logger::Msg msg;
        msg.kind = logger::MsgKind::kError;
        msg.data = make_data(r, text);
        msg.notes = std::move(notes_data);
        msg.notes.insert(msg.notes.end(), extra_notes.begin(), extra_notes.end());

        if (msg.data.location && !suggestion.empty()) {
            msg.data.location->suggestion = suggestion;
        }

        log.add_msg(std::move(msg));
    }

    // Constructs the Resolver that serves an entire compiler run, folding the
    // supplied options into every resolution it will later perform.
    //
    // A sizable amount of one-time work happens here so individual imports
    // stay cheap afterwards:
    //
    //   - The configured extension order is split into two specialised
    //     flavours: one restricted to CSS imports (which must never match
    //     JavaScript) and one for node_modules probing that separates the
    //     type-annotated extensions from the plain JavaScript ones so files
    //     are matched extension by extension in the intended priority.
    //   - The ESM conditions maps used by "exports"/"imports" resolution are
    //     initialised with "default", "import" and "require", then widened
    //     with the user's custom conditions and the platform-implied
    //     "browser" or "node" condition.
    //   - When a project configuration document is supplied (by filesystem
    //     path or as raw text) it is parsed eagerly, its failures reported
    //     through the log, and its compiler settings, JSX options and
    //     always-strict flag are written back into the run's options so every
    //     subsequent query sees them.
    //   - The working directory is read once so it is known before the first
    //     import asks for it.
    //
    //   Input:  options with platform set to Node and the extension order
    //           [".js", ".mjs"]
    //   Output: Resolver whose ESM import conditions include "node" and whose
    //           node_modules order probes plain extensions before annotated
    //           ones
    std::unique_ptr<Resolver> NewResolver(config::APICall      call,
                                          filesystem::Fs&      fs,
                                          logger::Log&         log,
                                          cache::CacheSet&     caches,
                                          config::Options*     options)
    {
        std::vector<std::string> css_extension_order;
        css_extension_order.reserve(options->ExtensionOrder.size());
        for (const std::string& ext : options->ExtensionOrder) {
            config::Loader loader = config::LoaderFromFileExtension(options->ExtensionToLoader, ext);
            if (loader == config::Loader::kNone || config::IsCSS(loader)) {
                css_extension_order.push_back(ext);
            }
        }

        std::vector<std::string> node_modules_extension_order;
        node_modules_extension_order.reserve(options->ExtensionOrder.size());
        size_t split = 0;
        for (size_t i = 0; i < options->ExtensionOrder.size(); i++) {
            config::Loader loader = config::LoaderFromFileExtension(options->ExtensionToLoader, options->ExtensionOrder[i]);
            if (loader == config::Loader::kJS || loader == config::Loader::kJSX) {
                split = i + 1;
            }
        }
        if (split != 0) {
            for (size_t i = 0; i < split; i++) {
                const std::string& ext = options->ExtensionOrder[i];
                if (!config::IsTypeScript(config::LoaderFromFileExtension(options->ExtensionToLoader, ext))) {
                    node_modules_extension_order.push_back(ext);
                }
            }
            for (const std::string& ext : options->ExtensionOrder) {
                if (config::IsTypeScript(config::LoaderFromFileExtension(options->ExtensionToLoader, ext))) {
                    node_modules_extension_order.push_back(ext);
                }
            }
            for (size_t i = split; i < options->ExtensionOrder.size(); i++) {
                const std::string& ext = options->ExtensionOrder[i];
                if (!config::IsTypeScript(config::LoaderFromFileExtension(options->ExtensionToLoader, ext))) {
                    node_modules_extension_order.push_back(ext);
                }
            }
        }

        ConditionsMap esm_conditions_default{{"default", true}};
        ConditionsMap esm_conditions_import{{"import", true}};
        ConditionsMap esm_conditions_require{{"require", true}};
        for (const std::string& condition : options->Conditions) {
            esm_conditions_default[condition] = true;
        }
        switch (options->OutputPlatform) {
            case config::Platform::kBrowser: {
                esm_conditions_default["browser"] = true;
                break;
            }
            case config::Platform::kNode: {
                esm_conditions_default["node"] = true;
                break;
            }
            default: break;
        }
        for (const auto& [key, value] : esm_conditions_default) {
            esm_conditions_import[key]  = value;
            esm_conditions_require[key] = value;
        }

        fs.Cwd();

        auto res                  = std::make_unique<Resolver>(fs, log, caches);
        res->options              = *options;
        res->css_extension_order  = std::move(css_extension_order);
        res->node_modules_extension_order = std::move(node_modules_extension_order);
        res->esm_conditions_default       = std::move(esm_conditions_default);
        res->esm_conditions_import        = std::move(esm_conditions_import);
        res->esm_conditions_require       = std::move(esm_conditions_require);

        if (!options->TSConfigPath.empty() || !options->TSConfigRaw.empty()) {
            DebugMeta debug_meta;
            ResolverQuery query{
                    .r          = res.get(),
                    .debug_meta = &debug_meta,
                    .debug_logs = nullptr,
                    .kind       = {},
            };

            std::unordered_map<std::string, bool> visited_map;
            std::unordered_map<std::string, bool>* visited = nullptr;
            if (call == config::APICall::kBuildCall) {
                visited = &visited_map;
            }

            TSConfigResult result;
            if (!options->TSConfigPath.empty()) {
                DebugLogs debug_logs;
                if (log.level <= logger::LogLevel::kDebug) {
                    debug_logs.what = "Resolving tsconfig file " + std::string(Q(options->TSConfigPath));
                    query.debug_logs = &debug_logs;
                }
                result = query.ParseTSConfig(options->TSConfigPath, visited, fs.Dir(options->TSConfigPath));
            } else {
                logger::Source source;
                source.key_path.text      = fs.Join({fs.Cwd(), "<tsconfig.json>"});
                source.key_path.namespace_ = "file";
                source.pretty_paths.abs   = "<tsconfig.json>";
                source.pretty_paths.rel   = "<tsconfig.json>";
                source.contents           = options->TSConfigRaw;
                result = query.ParseTSConfigFromSource(source, visited, fs.Cwd());
            }

            if (result.error != TSConfigError::kNone) {
                if (result.error == TSConfigError::kENOENT) {
                    logger::PrettyPaths pretty_paths = MakePrettyPaths(fs, logger::Path{.text = options->TSConfigPath, .namespace_ = "file"});
                    log.AddError(nullptr, logger::Range{},
                                 logger::FormatMsg(logger::MsgCat::kResolver_CannotFindTSConfig,
                                                   pretty_paths.Select(options->LogPathStyle)));
                } else if (result.error != TSConfigError::kAlreadyLogged) {
                    logger::PrettyPaths pretty_paths = MakePrettyPaths(fs, logger::Path{.text = options->TSConfigPath, .namespace_ = "file"});
                    std::string message = result.error_message.empty() ? "(import cycle)" : result.error_message;
                    log.AddError(nullptr, logger::Range{},
                                 logger::FormatMsg(logger::MsgCat::kResolver_CannotReadFile,
                                                   pretty_paths.Select(options->LogPathStyle), message));
                }
            } else {
                res->ts_config_override = result.result;
                query.FlushDebugLogs(FlushMode::kDueToSuccess);
            }
        }

        if (res->ts_config_override != nullptr) {
            options->TS.Config = res->ts_config_override->settings;
            config::TSConfigJSXApplyTo(res->ts_config_override->jsx_settings, options->JSX);
            options->TSAlwaysStrictData =
                    const_cast<config::TSAlwaysStrict*>(res->ts_config_override->TSAlwaysStrictOrStrict());
        }

        return res;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Public resolution entry points

    // Public entry point that resolves one single import to a concrete path.
    // This is the call the bundler makes for every import statement, require
    // call, and entry point. Resolution proceeds in stages:
    //
    //   1. Package aliases from the configuration are matched against the
    //      specifier; the longest matching key rewrites the import and the
    //      search restarts from the working directory.
    //   2. Paths that are external by configuration, that look like URLs, that
    //      are Node built-ins, or that are data URLs are returned immediately
    //      without touching the filesystem.
    //   3. A search directory is required, and wildcard specifiers are
    //      rejected here (they belong to ResolveGlob).
    //   4. Once per resolver, the Yarn Plug'n'Play manifest is located by
    //      walking outward from the working directory, so later imports can
    //      resolve packages without scanning node_modules.
    //   5. The real search is delegated to ResolveWithoutSymlinks. When that
    //      misses and the specifier carried a "?" or "#" suffix, the suffix
    //      is stripped, the search is retried, and the suffix is recorded as
    //      the resolved path's ignored tail.
    //
    //   Input:  source_dir = "/app/src", import_path = "lodash", kind = kStmt
    //   Output: ResolveResult whose primary text is the absolute path of the
    //           resolved file, or an external path for non-file imports
    //
    //   Input:  import_path = "./util.js?raw"
    //   Output: primary = "/app/src/util.js" with ignored_suffix "?raw"
    std::optional<ResolveResult> Resolver::Resolve(const std::string&    source_dir_in,
                                                   const std::string&    import_path_in,
                                                   compiler::ImportKind  kind,
                                                   DebugMeta*            debug_meta)
    {
        std::string source_dir  = source_dir_in;
        std::string import_path = import_path_in;

        DebugLogs debug_logs_storage;
        ResolverQuery query{
                .r          = this,
                .debug_meta = debug_meta,
                .debug_logs = nullptr,
                .kind       = kind,
        };
        if (log->level <= logger::LogLevel::kDebug) {
            debug_logs_storage.what = "Resolving import " + std::string(Q(import_path)) +
                                      " in directory " + std::string(Q(source_dir)) +
                                      " of type \"" + std::string(compiler::ImportKindToStringForMetafile(kind)) + "\"";
            query.debug_logs = &debug_logs_storage;
        }

        if (!options.PackageAliases.empty() && IsPackagePath(import_path)) {
            if (query.debug_logs) {
                query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_CheckingForPackageAliasMatches));
            }
            std::string longest_key;
            std::string longest_value;

            for (const auto& [key, value] : options.PackageAliases) {
                if (key.size() > longest_key.size() && import_path.starts_with(key) &&
                    (import_path.size() == key.size() || import_path[key.size()] == '/')) {
                    longest_key   = key;
                    longest_value = value;
                }
            }

            if (!longest_key.empty()) {
                debug_meta->modified_import_path = longest_value;
                std::string tail = import_path.substr(longest_key.size());
                if (tail != "/") {
                    debug_meta->modified_import_path += tail;
                }
                if (query.debug_logs) {
                    query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_MatchedWithAliasFromTo,
                            std::string(Q(longest_key)),
                            std::string(Q(longest_value))
                    ));
                    query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ModifiedImportPathFromTo,
                            std::string(Q(import_path)),
                            std::string(Q(debug_meta->modified_import_path))
                    ));
                }
                import_path = debug_meta->modified_import_path;

                source_dir = fs->Cwd();
                if (query.debug_logs) {
                    query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ChangedResolveDirectoryTo,
                            std::string(Q(source_dir))
                    ));
                }
            } else if (query.debug_logs) {
                query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToFindAnyPackageAliasMatches));
            }
        }

        bool is_explicitly_external = query.IsExternal(options.ExternalSettingsData.PreResolve, import_path, kind);
        if (is_explicitly_external ||
            (kind == compiler::ImportKind::kUrl && import_path.starts_with("#")) ||
            import_path.starts_with("http://") ||
            import_path.starts_with("https://") ||
            import_path.starts_with("//")) {

            if (query.debug_logs) {
                if (is_explicitly_external) {
                    query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ThePathWasMarkedAsExternalByTheUser,
                            std::string(Q(import_path))
                    ));
                } else {
                    query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_MarkingThisPathAsImplicitlyExternal));
                }
            }

            query.FlushDebugLogs(FlushMode::kDueToSuccess);
            ResolveResult result;
            result.path_pair.primary.text = import_path;
            result.path_pair.is_external  = true;
            return result;
        }

        if (std::optional<ResolveResult> builtin = query.CheckForBuiltInNodeModules(import_path)) {
            query.FlushDebugLogs(FlushMode::kDueToSuccess);
            return builtin;
        }

        if (std::optional<helpers::DataURL> parsed = helpers::ParseDataURL(import_path)) {
            if (parsed->DecodeMIMEType() != helpers::MIMEType::kUnsupported) {
                if (query.debug_logs) {
                    query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_PuttingThisPathInTheDataurlNamespace));
                }
                query.FlushDebugLogs(FlushMode::kDueToSuccess);
                ResolveResult result;
                result.path_pair.primary.text      = import_path;
                result.path_pair.primary.namespace_ = "dataurl";
                return result;
            }

            if (query.debug_logs) {
                query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_MarkingThisDataURLAsExternal));
            }
            query.FlushDebugLogs(FlushMode::kDueToSuccess);
            ResolveResult result;
            result.path_pair.primary.text = import_path;
            result.path_pair.is_external  = true;
            return result;
        }

        if (source_dir.empty()) {
            if (query.debug_logs) {
                query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_CannotResolveThisPathWithoutADirectory));
            }
            query.FlushDebugLogs(FlushMode::kDueToFailure);
            return std::nullopt;
        }

        if (import_path.find('*') != std::string::npos) {
            if (query.debug_logs) {
                query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_CannotResolveAPathContainingAWildcardCharacterInASinglePathContext));
            }
            query.FlushDebugLogs(FlushMode::kDueToFailure);
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(mutex);

        if (!pnp_manifest_was_checked) {
            pnp_manifest_was_checked = true;

            for (DirInfo* dir_info = query.DirInfoCached(fs->Cwd()); dir_info != nullptr; dir_info = dir_info->parent) {
                const std::string& abs_path = dir_info->pnp_manifest_abs_path;
                if (abs_path.empty()) {
                    continue;
                }
                if (abs_path.ends_with(".json")) {
                    ResolverQuery::ExtractedYarnPnPData extracted =
                            query.ExtractYarnPnPDataFromJSON(abs_path, PnpDataMode::kReportErrorsAboutMissingFiles);
                    bool has_expr = std::visit([](const auto& ptr) { return ptr != nullptr; }, extracted.expr.data);
                    if (has_expr) {
                        pnp_manifest = CompileYarnPnPData(abs_path, fs->Dir(abs_path), extracted.expr, extracted.source).release();
                        pnp_manifest_arena.emplace_back(pnp_manifest);
                    }
                } else {
                    ResolverQuery::ExtractedYarnPnPData extracted =
                            query.TryToExtractYarnPnPDataFromJS(abs_path, PnpDataMode::kReportErrorsAboutMissingFiles);
                    bool has_expr = std::visit([](const auto& ptr) { return ptr != nullptr; }, extracted.expr.data);
                    if (has_expr) {
                        pnp_manifest = CompileYarnPnPData(abs_path, fs->Dir(abs_path), extracted.expr, extracted.source).release();
                        pnp_manifest_arena.emplace_back(pnp_manifest);
                    }
                }
                if (query.debug_logs && pnp_manifest != nullptr && !pnp_manifest->invalid_ignore_pattern_data.empty()) {
                    query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_InvalidGoRegularExpressionForIgnorePatternData,
                            pnp_manifest->invalid_ignore_pattern_data
                    ));
                }
                break;
            }
        }

        DirInfo* source_dir_info = query.DirInfoCached(source_dir);
        if (source_dir_info == nullptr) {
            return std::nullopt;
        }

        std::optional<ResolveResult> result = query.ResolveWithoutSymlinks(source_dir, source_dir_info, import_path);
        if (!result) {
            size_t suffix = import_path.find_first_of("?#");
            if (suffix == std::string::npos || suffix < 1) {
                query.FlushDebugLogs(FlushMode::kDueToFailure);
                return std::nullopt;
            }
            if (query.debug_logs) {
                query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_RetryingResolutionAfterRemovingTheSuffix,
                        std::string(Q(import_path.substr(suffix)))
                ));
            }
            result = query.ResolveWithoutSymlinks(source_dir, source_dir_info, import_path.substr(0, suffix));
            if (!result) {
                query.FlushDebugLogs(FlushMode::kDueToFailure);
                return std::nullopt;
            }
            result->path_pair.primary.ignored_suffix = import_path.substr(suffix);
            if (result->path_pair.HasSecondary()) {
                result->path_pair.secondary.ignored_suffix = import_path.substr(suffix);
            }
        }

        query.FinalizeResolve(*result);
        query.FlushDebugLogs(FlushMode::kDueToSuccess);
        return result;
    }

    // Resolves a glob-style import pattern against the filesystem and returns
    // every matching file at once, backing multi-target entry points. The
    // pattern arrives already tokenised into literal prefixes and wildcard
    // kinds. The first prefix dictates the starting directory: everything up
    // to the first wildcard (or the end of a literal path) is taken as the
    // root, and the whole pattern is compiled into a single regular
    // expression. The tree is then walked recursively, each relative path is
    // tested against the expression, and every hit becomes a result just as a
    // single import would (subject to the same external matching). A pattern
    // that expands to nothing produces a warning when a sink is provided.
    //
    //   Input:  pattern ["./src/views/*.js"], source_dir = "/app"
    //   Output: {"src/views/home.js" -> "/app/src/views/home.js", ...}
    //   Input:  pattern matching no files
    //   Output: empty map plus a kBundler_EmptyGlob warning
    std::optional<std::map<std::string, ResolveResult>> Resolver::ResolveGlob(
            const std::string&                    source_dir_in,
            const std::vector<helpers::GlobPart>& import_path_pattern,
            compiler::ImportKind                  kind,
            const std::string&                    pretty_pattern,
            logger::Msg*                          warning)
    {
        std::string source_dir = source_dir_in;

        DebugLogs debug_logs_storage;
        ResolverQuery query{
                .r          = this,
                .debug_meta = nullptr,
                .debug_logs = nullptr,
                .kind       = kind,
        };
        DebugMeta debug_meta;
        query.debug_meta = &debug_meta;
        if (log->level <= logger::LogLevel::kDebug) {
            debug_logs_storage.what = "Resolving glob import " + pretty_pattern +
                                      " in directory " + std::string(Q(source_dir)) +
                                      " of type \"" + std::string(compiler::ImportKindToStringForMetafile(kind)) + "\"";
            query.debug_logs = &debug_logs_storage;
        }

        if (import_path_pattern.empty()) {
            if (query.debug_logs) {
                query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_IgnoringEmptyGlobPattern));
            }
            query.FlushDebugLogs(FlushMode::kDueToFailure);
            return std::nullopt;
        }
        std::string first_prefix = import_path_pattern[0].prefix;

        if (!first_prefix.starts_with("./") && !first_prefix.starts_with("../") &&
            !first_prefix.starts_with(".\\") && !first_prefix.starts_with("..\\") ) {
            if (kind == compiler::ImportKind::kEntryPoint) {
                if (!fs->IsAbs(first_prefix)) {
                    first_prefix = "./" + first_prefix;
                }
            } else {
                if (query.debug_logs) {
                    query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_IgnoringGlobImportThatDoesnTStartWithOr));
                }
                query.FlushDebugLogs(FlushMode::kDueToFailure);
                return std::nullopt;
            }
        }

        size_t dir_prefix = 0;
        while (true) {
            size_t slash = first_prefix.find_first_of("/\\", dir_prefix);
            if (slash == std::string::npos) {
                break;
            }
            size_t star = first_prefix.find('*', dir_prefix);
            if (star != std::string::npos && slash > star) {
                break;
            }
            dir_prefix = slash + 1;
        }

        std::string suffix = first_prefix.substr(0, dir_prefix);
        if (fs->IsAbs(suffix)) {
            source_dir = suffix;
        } else {
            source_dir = fs->Join({source_dir, suffix});
        }

        std::lock_guard<std::mutex> lock(mutex);

        DirInfo* source_dir_info = query.DirInfoCached(source_dir);
        if (source_dir_info == nullptr) {
            if (query.debug_logs) {
                query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToFindTheDirectory,
                        std::string(Q(source_dir))
                ));
            }
            query.FlushDebugLogs(FlushMode::kDueToFailure);
            return std::nullopt;
        }

        bool can_match_on_slash = false;
        bool was_glob_star      = false;
        std::string regex_text  = "^";
        for (size_t i = 0; i < import_path_pattern.size(); i++) {
            std::string prefix = import_path_pattern[i].prefix;
            if (i == 0) {
                prefix = first_prefix;
            }
            if (was_glob_star && !prefix.empty() && (prefix[0] == '/' || prefix[0] == '\\')) {
                prefix = prefix.substr(1);
            }
            regex_text += QuoteRegexMeta(prefix);
            switch (import_path_pattern[i].wildcard) {
                case helpers::GlobWildcard::kAllIncludingSlash: {
                    regex_text += "(?:[^/]*(?:/|$))*";
                    can_match_on_slash = true;
                    was_glob_star      = true;
                    break;
                }
                case helpers::GlobWildcard::kAllExceptSlash: {
                    regex_text += "[^/]*";
                    was_glob_star = false;
                    break;
                }
                default: break;
            }
        }
        regex_text += '$';
        std::regex re(regex_text);

        std::map<std::string, ResolveResult> results;

        std::function<void(DirInfo*, const std::string&)> visit =
                [&](DirInfo* dir_info, const std::string& dir) {
                    for (const std::string& key : dir_info->entries.SortedKeys()) {
                        auto [entry, diff_case_unused] = dir_info->entries.Get(key);
                        (void)diff_case_unused;
                        if (query.debug_logs) {
                            query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ConsideringEntry,
                                    std::string(Q(fs->Join({dir_info->abs_path, key})))
                            ));
                            query.debug_logs->IncreaseIndent();
                        }

                        switch (entry->Kind(*fs)) {
                            case filesystem::EntryKind::kDir: {
                                if (can_match_on_slash && entry->Symlink(*fs).empty()) {
                                    if (DirInfo* child_dir_info = query.DirInfoCached(fs->Join({dir_info->abs_path, key})); child_dir_info != nullptr) {
                                        visit(child_dir_info, dir + key + "/");
                                    }
                                }
                                break;
                            }
                            case filesystem::EntryKind::kFile: {
                                std::string rel_path = dir + key;
                                if (std::regex_search(rel_path, re)) {
                                    ResolveResult result;

                                    if (query.IsExternal(options.ExternalSettingsData.PreResolve, rel_path, kind)) {
                                        result.path_pair.primary.text = rel_path;
                                        result.path_pair.is_external  = true;

                                        if (query.debug_logs) {
                                            query.debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ThePathWasMarkedAsExternalByTheUser,
                                                    std::string(Q(result.path_pair.primary.text))
                                            ));
                                        }
                                    } else {
                                        std::string abs_path = fs->Join({dir_info->abs_path, key});
                                        result.path_pair.primary.text      = abs_path;
                                        result.path_pair.primary.namespace_ = "file";
                                    }

                                    query.FinalizeResolve(result);
                                    results[rel_path] = std::move(result);
                                }
                                break;
                            }
                            default: break;
                        }

                        if (query.debug_logs) {
                            query.debug_logs->DecreaseIndent();
                        }
                    }
                };

        visit(source_dir_info, first_prefix.substr(0, dir_prefix));

        if (results.empty() && warning != nullptr) {
            warning->id   = logger::MsgID::kBundler_EmptyGlob;
            warning->kind = logger::MsgKind::kWarning;
            warning->data.text = logger::FormatMsg(logger::MsgCat::kResolver_EmptyGlob, pretty_pattern);
        }

        query.FlushDebugLogs(FlushMode::kDueToSuccess);
        return results;
    }

    // A focused probe used by callers who already have an exact relative target
    // in mind: it joins the source directory with the import path and attempts
    // exactly one file-or-directory load, skipping aliases, browser maps,
    // package walking, and everything else. In effect it answers a simple
    // "does this file exist" question.
    //
    //   Input:  source_dir = "/app", import_path = "./lib/util.js"
    //   Output: ResolveResult for "/app/lib/util.js"
    //   Input:  "./missing.js"
    //   Output: nullopt
    std::optional<ResolveResult> Resolver::ProbeResolvePackageAsRelative(const std::string&   source_dir,
                                                                         const std::string&   import_path,
                                                                         compiler::ImportKind kind,
                                                                         DebugMeta*           debug_meta)
    {
        ResolverQuery query{
                .r          = this,
                .debug_meta = debug_meta,
                .debug_logs = nullptr,
                .kind       = kind,
        };
        std::string abs_path = fs->Join({source_dir, import_path});

        std::lock_guard<std::mutex> lock(mutex);

        LoadResult loaded = query.LoadAsFileOrDirectory(abs_path);
        if (loaded.ok) {
            ResolveResult result;
            result.path_pair      = loaded.pair;
            result.different_case = loaded.diff_case;
            query.FinalizeResolve(result);
            query.FlushDebugLogs(FlushMode::kDueToSuccess);
            return result;
        }

        return std::nullopt;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Per-query resolution state

    // Emits the accumulated trace of a single query to the log. On a failure the
    // notes are always flushed so the miss has a trail even when debug logging
    // is disabled; on success they are only flushed when verbose logging is
    // enabled, because an ordinary successful resolution needs no narrative.
    //
    //   Input:  debug_logs describing "Resolving import lodash ...",
    //           mode = kDueToFailure
    //   Output: the notes emitted on the log as a debug message
    void ResolverQuery::FlushDebugLogs(FlushMode mode)
    {
        if (debug_logs == nullptr) {
            return;
        }
        if (mode == FlushMode::kDueToFailure) {
            r->log->AddIDWithNotes(logger::MsgID::kNone, logger::MsgKind::kDebug, nullptr, logger::Range{},
                                   debug_logs->what, debug_logs->notes);
        } else if (r->log->level <= logger::LogLevel::kVerbose) {
            r->log->AddIDWithNotes(logger::MsgID::kNone, logger::MsgKind::kVerbose, nullptr, logger::Range{},
                                   debug_logs->what, debug_logs->notes);
        }
    }

    // Decides whether an import path counts as "external" for the given import
    // kind, meaning it should be left as a runtime import instead of being
    // bundled. Two matcher styles are supported: exact path matches and
    // prefix "*" suffix wildcard patterns. Entry points can never be external,
    // since an entry point always has to be a real file.
    //
    //   Input:  path = "lodash/zip", patterns = {"lodash/*"}
    //   Output: true
    //   Input:  path = "lodash/zip", patterns = {"react/*"}
    //   Output: false
    bool ResolverQuery::IsExternal(const config::ExternalMatchers& matchers,
                                   const std::string&              path,
                                   compiler::ImportKind            import_kind)
    {
        if (import_kind == compiler::ImportKind::kEntryPoint) {
            return false;
        }
        if (matchers.Exact.count(path) > 0) {
            return true;
        }
        for (const config::WildcardPattern& pattern : matchers.Patterns) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_CheckingAgainstTheExternalPattern,
                        std::string(Q(path)),
                        std::string(Q(pattern.Prefix + "*" + pattern.Suffix))
                ));
            }
            if (path.size() >= pattern.Prefix.size() + pattern.Suffix.size() &&
                path.starts_with(pattern.Prefix) &&
                path.ends_with(pattern.Suffix)) {
                return true;
            }
        }
        return false;
    }

    // Post-processes a successful resolution before it leaves the resolver. The
    // post-resolution external matchers are consulted first, and a match flips
    // the result to external. Otherwise every path in the result -- the
    // primary and any secondary -- is revisited to:
    //
    //   - follow the entry's symlink unless symlinks are preserved,
    //   - attach the enclosing package's side-effects verdict and module type
    //     when the package declares them,
    //   - attach the governing project configuration's settings (JSX factory,
    //     strictness) for the import's directory.
    //
    // The final state of the pair is then described for the debug trace.
    //
    //   Input:  result whose primary is "/app/node_modules/lodash/index.js"
    //           under a package that declares no side effects
    //   Output: same path (symlinks resolved) with the result flagged as
    //           side-effect-free
    void ResolverQuery::FinalizeResolve(ResolveResult& result)
    {
        if (!result.path_pair.is_external &&
            IsExternal(r->options.ExternalSettingsData.PostResolve, result.path_pair.primary.text, kind)) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ThePathWasMarkedAsExternalByTheUser,
                        std::string(Q(result.path_pair.primary.text))
                ));
            }
            result.path_pair.is_external = true;
        } else {
            size_t i = 0;
            for (logger::Path* path : result.path_pair.Iter()) {
                if (path->namespace_ != "file") {
                    i++;
                    continue;
                }
                DirInfo* dir_info = DirInfoCached(r->fs->Dir(path->text));
                if (dir_info == nullptr) {
                    i++;
                    continue;
                }
                std::string base = r->fs->Base(path->text);

                if (!r->options.PreserveSymlinks) {
                    auto [entry, diff_case_unused] = dir_info->entries.Get(base);
                    (void)diff_case_unused;
                    if (entry) {
                        std::string symlink = entry->Symlink(*r->fs);
if (!symlink.empty()) {
                                // The entry is itself the symlink, so its
                                // target is already recorded on the entry;
                                // nothing further needs chasing here.
                            } else if (!dir_info->abs_real_path.empty()) {
                            symlink = r->fs->Join({dir_info->abs_real_path, base});
                        }
                        if (!symlink.empty()) {
                            if (debug_logs) {
                                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ResolvedSymlinkTo,
                                        std::string(Q(path->text)),
                                        std::string(Q(symlink))
                                ));
                            }
                            path->text = symlink;

                            dir_info = DirInfoCached(r->fs->Dir(path->text));
                            if (dir_info == nullptr) {
                                i++;
                                continue;
                            }
                            base = r->fs->Base(path->text);
                        }
                    }
                }

                if (i > 0) {
                    i++;
                    continue;
                }

                if (path->IsDisabled()) {
                    i++;
                    continue;
                }

                PackageJSON* pkg_json = dir_info->enclosing_package_json;
                if (pkg_json != nullptr) {
                    if (pkg_json->side_effects_map.has_value()) {
                        bool has_side_effects = false;
                        std::string path_lookup = path->text;
                        std::replace(path_lookup.begin(), path_lookup.end(), '\\', '/');
                        if (pkg_json->side_effects_map->count(path_lookup) > 0) {
                            has_side_effects = true;
                        } else {
                            for (const std::regex& re : pkg_json->side_effects_regexps) {
                                if (std::regex_search(path_lookup, re)) {
                                    has_side_effects = true;
                                    break;
                                }
                            }
                        }
                        if (!has_side_effects) {
                            if (debug_logs) {
                                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_MarkingThisFileAsHavingNoSideEffectsDueTo,
                                        std::string(Q(pkg_json->source.key_path.text))
                                ));
                            }
                            result.primary_side_effects_data = pkg_json->side_effects_data;
                        }
                    }

                    result.module_type_data = pkg_json->module_type_data;
                }

                if (TSConfigJSON* tsconfig_json = TSConfigForDir(dir_info); tsconfig_json != nullptr) {
                    result.tsconfig          = &tsconfig_json->settings;
                    result.tsconfig_jsx      = tsconfig_json->jsx_settings;
                    result.ts_always_strict  = tsconfig_json->TSAlwaysStrictOrStrict();

                    if (debug_logs) {
                        debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ThisImportIsUnderTheEffectOf,
                                std::string(Q(tsconfig_json->abs_path))
                        ));
                        if (!result.tsconfig_jsx.JSXFactory.empty()) {
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_JsxFactoryIsDueTo,
                                    std::string(Q(JoinWithDot(result.tsconfig_jsx.JSXFactory))),
                                    std::string(Q(tsconfig_json->abs_path))
                            ));
                        }
                        if (!result.tsconfig_jsx.JSXFragmentFactory.empty()) {
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_JsxFragmentIsDueTo,
                                    std::string(Q(JoinWithDot(result.tsconfig_jsx.JSXFragmentFactory))),
                                    std::string(Q(tsconfig_json->abs_path))
                            ));
                        }
                    }
                }

                i++;
            }
        }

        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_PrimaryPathIsInNamespace,
                    std::string(Q(result.path_pair.primary.text)),
                    std::string(Q(result.path_pair.primary.namespace_))
            ));
            if (result.path_pair.HasSecondary()) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_SecondaryPathIsInNamespace,
                        std::string(Q(result.path_pair.secondary.text)),
                        std::string(Q(result.path_pair.secondary.namespace_))
                ));
            }
        }
    }

    // The core single-import search shared by Resolve and its helpers. It
    // dispatches on the shape of the specifier:
    //
    //   - Absolute and root-relative specifiers try the project's path
    //     remapping table, then load directly -- nothing else applies.
    //   - Relative specifiers are joined to the source directory, checked
    //     against the enclosing browser map, and loaded as a file or a
    //     directory (a trailing-slash specifier is always treated as a
    //     directory).
    //   - Package specifiers are checked against the browser map at the
    //     package level and then handed to the node_modules walker; paths
    //     rerouted by the browser map are re-resolved from the package's
    //     browser scope.
    //
    // Because delegation can succeed or fail at several layers, the code
    // deliberately avoids returning early: a relative miss can still let a
    // package search proceed, and vice versa. Only when every applicable route
    // has been exhausted does the function report failure.
    //
    //   Input:  source_dir = "/app/src", import_path = "./util"
    //   Output: result pointing at "/app/src/util.js" (or its index file)
    //
    //   Input:  source_dir = "/app/src", import_path = "lodash"
    //   Output: result pointing at "/app/node_modules/lodash/index.js"
    std::optional<ResolveResult> ResolverQuery::ResolveWithoutSymlinks(const std::string& source_dir,
                                                                       DirInfo*           source_dir_info,
                                                                       const std::string& import_path_in)
    {
        std::string import_path = import_path_in;
        ResolveResult result;

        if (!import_path.empty() && (import_path.starts_with("/") || r->fs->IsAbs(import_path))) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_TheImportIsBeingTreatedAsAnAbsolutePath,
                        std::string(Q(import_path))
                ));
            }

            if (TSConfigJSON* tsconfig_json = TSConfigForDir(source_dir_info); tsconfig_json != nullptr && tsconfig_json->paths) {
                LoadResult loaded = MatchTSConfigPaths(tsconfig_json, import_path);
                if (loaded.ok) {
                    ResolveResult matched;
                    matched.path_pair      = loaded.pair;
                    matched.different_case = loaded.diff_case;
                    return matched;
                }
            }

            LoadResult loaded = LoadAsFileOrDirectory(import_path);
            if (loaded.ok) {
                ResolveResult found;
                found.path_pair      = loaded.pair;
                found.different_case = loaded.diff_case;
                return found;
            }
            return std::nullopt;
        }

        bool is_package_path  = IsPackagePath(import_path);
        bool check_relative   = !is_package_path || compiler::ImportKindIsFromCSS(kind);
        bool check_package    = is_package_path;

        if (check_relative) {
            std::string abs_path = r->fs->Join({source_dir, import_path});

            if (IsExternal(r->options.ExternalSettingsData.PostResolve, abs_path, kind)) {
                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ThePathWasMarkedAsExternalByTheUser,
                            std::string(Q(abs_path))
                    ));
                }
                ResolveResult external;
                external.path_pair.primary.text      = abs_path;
                external.path_pair.primary.namespace_ = "file";
                external.path_pair.is_external        = true;
                return external;
            }

            bool has_trailing_slash = import_path == "." ||
                                      import_path == ".." ||
                                      import_path.ends_with("/") ||
                                      import_path.ends_with("/.") ||
                                      import_path.ends_with("/..");

            DirInfo* import_dir_info = DirInfoCached(r->fs->Dir(abs_path));
            if (import_dir_info != nullptr) {
                BrowserRemapResult remap = CheckBrowserMap(import_dir_info, abs_path, BrowserPathKind::kAbsolutePath);
                if (remap.ok) {
                    if (!remap.remapped.has_value()) {
                        ResolveResult disabled;
                        disabled.path_pair.primary.text      = abs_path;
                        disabled.path_pair.primary.namespace_ = "file";
                        disabled.path_pair.primary.flags      = logger::PathFlags::kPathDisabled;
                        return disabled;
                    }
                    SideEffectsResult remapped_result =
                            ResolveWithoutRemapping(import_dir_info->enclosing_browser_scope, *remap.remapped);
                    if (remapped_result.ok) {
                        result                         = ResolveResult{};
                        result.path_pair               = remapped_result.pair;
                        result.different_case          = remapped_result.diff_case;
                        result.primary_side_effects_data = remapped_result.side_effects;
                        has_trailing_slash = false;
                        check_relative     = false;
                        check_package      = false;
                    }
                }
            }

            if (has_trailing_slash) {
                LoadResult loaded = LoadAsDirectory(abs_path);
                if (loaded.ok) {
                    check_package  = false;
                    result         = ResolveResult{};
                    result.path_pair = loaded.pair;
                    result.different_case = loaded.diff_case;
                } else if (!check_package) {
                    return std::nullopt;
                }
            } else {
                if (check_relative) {
                    LoadResult loaded = LoadAsFileOrDirectory(abs_path);
                    if (loaded.ok) {
                        check_package = false;
                        result         = ResolveResult{};
                        result.path_pair = loaded.pair;
                        result.different_case = loaded.diff_case;
                    } else if (!check_package) {
                        return std::nullopt;
                    }
                }
            }
        }

        if (check_package) {
            BrowserRemapResult remap = CheckBrowserMap(source_dir_info, import_path, BrowserPathKind::kPackagePath);
            if (remap.ok) {
                if (!remap.remapped.has_value()) {
                    SideEffectsResult loaded = LoadNodeModules(import_path, source_dir_info, false);
                    if (loaded.ok) {
                        PathPair disabled_pair;
                        disabled_pair.primary.text      = loaded.pair.primary.text;
                        disabled_pair.primary.namespace_ = "file";
                        disabled_pair.primary.flags      = logger::PathFlags::kPathDisabled;
                        if (loaded.pair.HasSecondary()) {
                            disabled_pair.secondary.text      = loaded.pair.secondary.text;
                            disabled_pair.secondary.namespace_ = "file";
                            disabled_pair.secondary.flags      = logger::PathFlags::kPathDisabled;
                        }
                        ResolveResult disabled;
                        disabled.path_pair                 = std::move(disabled_pair);
                        disabled.different_case            = loaded.diff_case;
                        disabled.primary_side_effects_data = loaded.side_effects;
                        return disabled;
                    } else {
                        ResolveResult disabled;
                        disabled.different_case          = loaded.diff_case;
                        disabled.path_pair.primary.text  = import_path;
                        disabled.path_pair.primary.flags = logger::PathFlags::kPathDisabled;
                        return disabled;
                    }
                }

                import_path    = *remap.remapped;
                source_dir_info = source_dir_info->enclosing_browser_scope;
            }

            SideEffectsResult resolved = ResolveWithoutRemapping(source_dir_info, import_path);
            if (resolved.ok) {
                result                             = ResolveResult{};
                result.path_pair                   = resolved.pair;
                result.different_case              = resolved.diff_case;
                result.primary_side_effects_data   = resolved.side_effects;
            } else {
                return std::nullopt;
            }
        }

        return result;
    }

    // Re-entry point used after the browser map has redirected an import: it
    // performs the normal file-or-package search starting from a particular
    // directory scope. Package paths continue into LoadNodeModules; every
    // other specifier is joined to the scope directory and loaded as a file or
    // directory. The caller chooses the scope so rerouted relatives stay
    // anchored to the package that owns the browser map.
    //
    //   Input:  scope = the browser scope of /app/pkg, import_path = "./x"
    //   Output: the resolved form of "<scope>/x"
    SideEffectsResult ResolverQuery::ResolveWithoutRemapping(DirInfo* source_dir_info, const std::string& import_path)
    {
        if (IsPackagePath(import_path)) {
            return LoadNodeModules(import_path, source_dir_info, false);
        }
        std::string abs_path = r->fs->Join({source_dir_info->abs_path, import_path});
        LoadResult loaded = LoadAsFileOrDirectory(abs_path);
        SideEffectsResult result;
        result.pair      = loaded.pair;
        result.ok        = loaded.ok;
        result.diff_case = loaded.diff_case;
        return result;
    }

    // Finds the project configuration document that governs a directory, or the
    // run-wide override when one is in force. Directories inside node_modules
    // are never governed: third-party code must not be re-compiled under
    // host-project settings. The override, when set, applies everywhere except
    // inside node_modules.
    //
    //   Input:  dir_info for /app/src with a discovered config document
    //   Output: pointer to that config's parsed settings
    //   Input:  dir_info inside node_modules
    //   Output: nullptr
    TSConfigJSON* ResolverQuery::TSConfigForDir(DirInfo* dir_info)
    {
        if (dir_info != nullptr && dir_info->is_inside_node_modules) {
            return nullptr;
        }
        if (r->ts_config_override != nullptr) {
            return r->ts_config_override;
        }
        if (dir_info != nullptr) {
            return dir_info->enclosing_tsconfig_json;
        }
        return nullptr;
    }

    // Returns the cached directory snapshot for a path, reading and fully
    // annotating the directory on its first use (see DirInfoUncached). Failed
    // reads cache a null slot so a broken directory is not re-probed on every
    // import that touches it. Because every lookup in the resolver funnels
    // through here, the on-disk picture stays consistent for an entire run.
    //
    //   Input:  "/app/src"           ->  DirInfo for /app/src (cached)
    //   Input:  "/app/missing-dir"   ->  nullptr
    DirInfo* ResolverQuery::DirInfoCached(const std::string& path)
    {
        auto cached_it = r->dir_cache.find(path);
        DirInfo* cached = nullptr;
        bool found      = cached_it != r->dir_cache.end();

        if (!found) {
            r->dir_cache[path] = nullptr;

            cached = DirInfoUncached(path);

            if (cached != nullptr) {
                r->dir_cache[path] = cached;
            }
        } else {
            cached = cached_it->second;
        }

        if (debug_logs) {
            if (cached == nullptr) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToReadDirectory,
                        std::string(Q(path))
                ));
            } else {
                int count = cached->entries.PeekEntryCount();
                std::string entries_word = count == 1 ? "entry" : "entries";
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ReadForDirectory,
                        std::to_string(count),
                        entries_word,
                        std::string(Q(path))
                ));
            }
        }

        return cached;
    }

    // Reads and compiles one project configuration document, guarding against
    // import cycles and reporting failures through the log. Symlinked paths
    // are resolved first unless preservation is enabled, and the file's bytes
    // come through the shared filesystem cache. The visited set is threaded
    // through so an "extends" chain that circles back to a file already being
    // processed is caught by the recursive parser as a cycle.
    //
    //   Input:  file = "/app/config.json", visited = {}
    //   Output: TSConfigResult{result = <parsed>, error = kNone}
    //   Input:  file that does not exist
    //   Output: TSConfigResult{result = nullptr, error = kENOENT}
    TSConfigResult ResolverQuery::ParseTSConfig(const std::string&                     file_in,
                                                std::unordered_map<std::string, bool>* visited,
                                                const std::string&                     config_dir)
    {
        std::string file = file_in;

        if (!r->options.PreserveSymlinks) {
            if (std::optional<std::string> real = r->fs->EvalSymlinks(file)) {
                file = *real;
            }
        }

        if (visited != nullptr && (*visited)[file]) {
            return TSConfigResult{.result = nullptr, .error = TSConfigError::kImportCycle, .error_message = ""};
        }

        filesystem::FsResult<std::string> contents = r->caches->fs_cache.ReadFile(*r->fs, file);
        if (debug_logs && !contents.Ok()) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToReadFile,
                    std::string(Q(file)),
                    ErrorText(contents)
            ));
        }
        if (!contents.Ok()) {
            if (IsENOENT(contents.canonical_error)) {
                return TSConfigResult{.result = nullptr, .error = TSConfigError::kENOENT, .error_message = ""};
            }
            return TSConfigResult{.result = nullptr, .error = TSConfigError::kOther, .error_message = ErrorText(contents)};
        }
        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_TheFileExists, std::string(Q(file))));
        }

        logger::Path key_path;
        key_path.text      = file;
        key_path.namespace_ = "file";
        logger::Source source;
        source.pretty_paths = MakePrettyPaths(*r->fs, key_path);
        source.key_path     = key_path;
        source.contents     = contents.value;

        if (visited != nullptr) {
            (*visited)[file] = true;
        }
        TSConfigResult result = ParseTSConfigFromSource(source, visited, config_dir);
        if (visited != nullptr) {
            (*visited)[file] = false;
        }
        return result;
    }

    // Compiles a project configuration document straight from in-memory source,
    // resolving whatever "extends" chain it declares along the way. Resolving
    // "extends" is where most of the work happens:
    //
    //   - Package-form extends values are resolved through the nearest Yarn
    //     Plug'n'Play manifest (walked outward if one is not loaded yet) or,
    //     failing that, through a classic node_modules walk where each
    //     package's export map and its declared configuration file are
    //     honoured.
    //   - Path-form extends values are joined to the current document's
    //     directory and probed as a plain file, as a JSON-suffixed file, or as
    //     a directory holding a configuration file.
    //   - A base that cannot be found is surfaced with a warning (except
    //     inside node_modules) while parsing continues without it; a circular
    //     "extends" chain gets its own dedicated cycle warning.
    //
    // Afterwards, path-remapping entries that reference a base URL the
    // document never defined are filtered out, with a diagnostic pointing at
    // each offending entry. Every successful parse is parked in the
    // resolver's arena so the result outlives the current query.
    //
    //   Input:  source for "/app/config.json" with no "extends" field
    //   Output: TSConfigResult{result = <parsed>, error = kNone}
    //   Input:  source extending "missing-pkg/config.json" that cannot be
    //           resolved
    //   Output: parsing proceeds without the base, and a cannot-find-base
    //           warning is emitted unless the file lives in node_modules
    TSConfigResult ResolverQuery::ParseTSConfigFromSource(const logger::Source&                  source,
                                                          std::unordered_map<std::string, bool>* visited,
                                                          const std::string&                     config_dir)
    {
        logger::LineColumnTracker tracker(&source);
        std::string file_dir  = r->fs->Dir(source.key_path.text);
        bool is_extends       = visited != nullptr && visited->size() > 1;

        TSConfigExtendsCallback extends_callback =
                [&](const std::string& extends, logger::Range extends_range) -> TSConfigJSON* {
            if (visited == nullptr) {
                return nullptr;
            }

            auto finish_search = [&](TSConfigResult&& search_result,
                                     const std::string& extends_file) -> std::pair<TSConfigJSON*, bool> {
                if (search_result.error == TSConfigError::kNone) {
                    return {search_result.result, true};
                }

                if (search_result.error == TSConfigError::kENOENT) {
                    return {nullptr, false};
                }

                if (search_result.error == TSConfigError::kImportCycle) {
                    r->log->AddID(logger::MsgID::kTSConfigJSON_Cycle, logger::MsgKind::kWarning, &tracker, extends_range,
                                  logger::FormatMsg(logger::MsgCat::kResolver_BaseConfigCycle, extends));
                } else if (search_result.error != TSConfigError::kAlreadyLogged) {
                    logger::Path extends_path;
                    extends_path.text      = extends_file;
                    extends_path.namespace_ = "file";
                    logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, extends_path);
                r->log->AddError(&tracker, extends_range,
                                 logger::FormatMsg(logger::MsgCat::kResolver_CannotReadFile,
                                                   pretty_paths.Select(r->options.LogPathStyle),
                                                   search_result.error_message));
                }
                return {nullptr, true};
            };

            TSConfigJSON* outcome = nullptr;
            bool jump_to_pnp_error = false;
            std::string extends_rewritten;

            if (IsPackagePath(extends)) {
                PnpData* pnp_data = r->pnp_manifest;

                if (pnp_data == nullptr) {
                    std::string current = file_dir;
                    while (true) {
                        if (!filesystem::ParseYarnPnPVirtualPath(current).has_value()) {
                            std::string abs_path = r->fs->Join({current, ".pnp.data.json"});
                            ExtractedYarnPnPData extracted =
                                    ExtractYarnPnPDataFromJSON(abs_path, PnpDataMode::kIgnoreErrorsAboutMissingFiles);
                            bool has_expr = std::visit([](const auto& ptr) { return ptr != nullptr; }, extracted.expr.data);
                            if (has_expr) {
                                std::unique_ptr<PnpData> compiled =
                                        CompileYarnPnPData(abs_path, current, extracted.expr, extracted.source);
                                pnp_data = compiled.get();
                                r->pnp_manifest_arena.push_back(std::move(compiled));
                                break;
                            }

                            abs_path = r->fs->Join({current, ".pnp.cjs"});
                            extracted = TryToExtractYarnPnPDataFromJS(abs_path, PnpDataMode::kIgnoreErrorsAboutMissingFiles);
                            has_expr = std::visit([](const auto& ptr) { return ptr != nullptr; }, extracted.expr.data);
                            if (has_expr) {
                                std::unique_ptr<PnpData> compiled =
                                        CompileYarnPnPData(abs_path, current, extracted.expr, extracted.source);
                                pnp_data = compiled.get();
                                r->pnp_manifest_arena.push_back(std::move(compiled));
                                break;
                            }

                            abs_path = r->fs->Join({current, ".pnp.js"});
                            extracted = TryToExtractYarnPnPDataFromJS(abs_path, PnpDataMode::kIgnoreErrorsAboutMissingFiles);
                            has_expr = std::visit([](const auto& ptr) { return ptr != nullptr; }, extracted.expr.data);
                            if (has_expr) {
                                std::unique_ptr<PnpData> compiled =
                                        CompileYarnPnPData(abs_path, current, extracted.expr, extracted.source);
                                pnp_data = compiled.get();
                                r->pnp_manifest_arena.push_back(std::move(compiled));
                                break;
                            }
                        }

                        std::string next = r->fs->Dir(current);
                        if (current == next) {
                            break;
                        }
                        current = next;
                    }
                }

                if (pnp_data != nullptr) {
                    PnpResult pnp_result = ResolveToUnqualified(extends, file_dir, pnp_data);
                    if (pnp_result.status == PnpStatus::kErrorGeneric) {
                        if (debug_logs) {
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_TheYarnPnPPathResolutionAlgorithmReturnedAnError));
                        }
                        jump_to_pnp_error = true;
                    } else if (pnp_result.status == PnpStatus::kSuccess) {
                        filesystem::FsResult<filesystem::DirEntries> entries = r->fs->ReadDirectory(pnp_result.pkg_dir_path);
                        if (entries.Ok()) {
                            auto [pj_entry, diff_case_unused] = entries.value.Get("package.json");
                            (void)diff_case_unused;
                            if (pj_entry && pj_entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                                PackageJSON* package_json = ParsePackageJSON(pnp_result.pkg_dir_path);
                                if (package_json != nullptr && package_json->exports_map != nullptr) {
                                    LoadResult loaded = EsmResolveAlgorithm(
                                            FinalizeImportsExportsKind::kYarnPnPTSConfigExtends,
                                            pnp_result.pkg_ident, "." + pnp_result.pkg_subpath,
                                            package_json, pnp_result.pkg_dir_path, source.key_path.text);
                                    if (loaded.ok) {
                                        TSConfigResult base = ParseTSConfig(loaded.pair.primary.text, visited, config_dir);
                                        auto [found, should_return] = finish_search(std::move(base), loaded.pair.primary.text);
                                        if (should_return) {
                                            return found;
                                        }
                                    }
                                    jump_to_pnp_error = true;
                                }
                            }
                        }

                        if (!jump_to_pnp_error) {
                            extends_rewritten = r->fs->Join({pnp_result.pkg_dir_path, pnp_result.pkg_subpath});
                        }
                    }
                }
            }

            if (!jump_to_pnp_error) {
                std::string extends_final = extends_rewritten.empty() ? extends : extends_rewritten;

                if (IsPackagePath(extends_final) && !r->fs->IsAbs(extends_final)) {
                    std::string esm_package_name;
                    std::string esm_package_subpath;
                    bool esm_ok = EsmParsePackageName(extends_final, esm_package_name, esm_package_subpath);
                    if (debug_logs && esm_ok) {
                        debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ParsedTsconfigPackageNameAndPackageSubpath,
                                std::string(Q(esm_package_name)),
                                std::string(Q(esm_package_subpath))
                        ));
                    }

                    std::string current = file_dir;
                    while (true) {
                        if (r->fs->Base(current) != "node_modules") {
                            std::string join = r->fs->Join({current, "node_modules", extends_final});

                            std::string pkg_dir = r->fs->Join({current, "node_modules", esm_package_name});
                            std::string pj_file = r->fs->Join({pkg_dir, "package.json"});
                            filesystem::FsResult<std::string> pj_contents = r->fs->ReadFile(pj_file);
                            if (pj_contents.Ok()) {
                                PackageJSON* package_json = ParsePackageJSON(pkg_dir);
                                if (package_json != nullptr) {
                                    if (!package_json->tsconfig.empty()) {
                                        join = package_json->tsconfig;
                                        if (!r->fs->IsAbs(join)) {
                                            join = r->fs->Join({pkg_dir, join});
                                        }
                                    }

                                    if (package_json->exports_map != nullptr) {
                                        if (debug_logs) {
                                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_LookingForInExportsMapIn,
                                                    std::string(Q(esm_package_subpath)),
                                                    std::string(Q(package_json->source.key_path.text))
                                            ));
                                        }
                                        DebugIndentGuard indent_guard(debug_logs);

                                        ConditionsMap conditions = r->esm_conditions_require;
                                        EsmStep step = EsmPackageExportsResolve("/", esm_package_subpath,
                                                                                *package_json->exports_map->root, conditions);
                                        step = EsmHandlePostConditions(step.resolved_path, step.status, std::move(step.debug));

                                        if (step.status == PjStatus::kExact || step.status == PjStatus::kExactEndsWithStar) {
                                            std::string file_to_check = r->fs->Join({pkg_dir, step.resolved_path});
                                            TSConfigResult base = ParseTSConfig(file_to_check, visited, config_dir);

                                            auto [found, should_return] = finish_search(std::move(base), file_to_check);
                                            if (should_return) {
                                                return found;
                                            }
                                        }
                                    }
                                }
                            } else if (debug_logs && !pj_contents.Ok()) {
                                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToReadFile,
                                        std::string(Q(pj_file)),
                                        ErrorText(pj_contents)
                                ));
                            }

                            std::vector<std::string> files_to_check = {
                                    r->fs->Join({join, "tsconfig.json"}),
                                    join,
                                    join + ".json",
                            };
                            for (const std::string& file_to_check : files_to_check) {
                                TSConfigResult base = ParseTSConfig(file_to_check, visited, config_dir);

                                if (base.error != TSConfigError::kNone && base.error != TSConfigError::kENOENT) {
                                    filesystem::FsResult<filesystem::DirEntries> entries = r->fs->ReadDirectory(r->fs->Dir(file_to_check));
                                    if (entries.Ok()) {
                                        auto [entry, diff_case_unused] = entries.value.Get(r->fs->Base(file_to_check));
                                        (void)diff_case_unused;
                                        if (entry && entry->Kind(*r->fs) == filesystem::EntryKind::kDir) {
                                            continue;
                                        }
                                    }
                                }

                                auto [found, should_return] = finish_search(std::move(base), file_to_check);
                                if (should_return) {
                                    return found;
                                }
                            }
                        }

                        std::string next = r->fs->Dir(current);
                        if (current == next) {
                            break;
                        }
                        current = next;
                    }
                } else {
                    std::string extends_file = extends_final;

                    if (extends_file == "." || extends_file == "..") {
                        extends_file += "/tsconfig.json";
                    }

                    if (!r->fs->IsAbs(extends_file)) {
                        extends_file = r->fs->Join({file_dir, extends_file});
                    }
                    TSConfigResult base = ParseTSConfig(extends_file, visited, config_dir);

                    if (base.error != TSConfigError::kNone && !extends_file.ends_with(".json")) {
                        filesystem::FsResult<filesystem::DirEntries> entries = r->fs->ReadDirectory(r->fs->Dir(extends_file));
                        if (entries.Ok()) {
                            std::string extends_base = r->fs->Base(extends_file);
                            auto [entry, diff_case_unused] = entries.value.Get(extends_base);
                            (void)diff_case_unused;
                            if (!entry || entry->Kind(*r->fs) != filesystem::EntryKind::kFile) {
                                auto [json_entry, diff_case_unused2] = entries.value.Get(extends_base + ".json");
                                (void)diff_case_unused2;
                                if (json_entry && json_entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                                    base = ParseTSConfig(extends_file + ".json", visited, config_dir);
                                }
                            }
                        }
                    }

                    auto [found, should_return] = finish_search(std::move(base), extends_file);
                    if (should_return) {
                        return found;
                    }
                }
            }

            if (!helpers::IsInsideNodeModules(source.key_path.text)) {
                std::vector<logger::MsgData> notes;
                if (debug_logs) {
                    notes = debug_logs->notes;
                }
                r->log->AddIDWithNotes(logger::MsgID::kTSConfigJSON_Missing, logger::MsgKind::kWarning, &tracker,
                                       extends_range, logger::FormatMsg(logger::MsgCat::kResolver_CannotFindBaseConfig,
                                                                       extends), notes);
            }

            return outcome;
        };

        TSConfigJSON* parsed = ParseTSConfigJSON(*r->log, source, r->caches->json_cache, *r->fs,
                                                 file_dir, config_dir, extends_callback);

        if (parsed == nullptr) {
            return TSConfigResult{.result = nullptr, .error = TSConfigError::kAlreadyLogged, .error_message = ""};
        }

        if (!is_extends && parsed->paths != nullptr && !parsed->base_url.has_value()) {
            logger::LineColumnTracker* paths_tracker = nullptr;
            for (auto& [key, paths] : parsed->paths->map) {
                (void)key;
                size_t end = 0;
                for (size_t i = 0; i < paths.size(); i++) {
                    TSConfigPath& path = paths[i];
                    if (IsValidTSConfigPathNoBaseURLPattern(path.text, *r->log, parsed->paths->source, paths_tracker, path.loc)) {
                        paths[end] = path;
                        end++;
                    }
                }
                if (end < paths.size()) {
                    paths.resize(end);
                }
            }
            delete paths_tracker;
        }

        r->ts_config_arena.push_back(std::unique_ptr<TSConfigJSON>(parsed));
        return TSConfigResult{.result = parsed, .error = TSConfigError::kNone, .error_message = ""};
    }

    // Reads and annotates one directory from the filesystem, used to populate the
    // per-directory cache. The parent directory is always materialised first
    // so state inherited down the tree flows naturally. Beyond the raw entry
    // list, each snapshot records:
    //
    //   - whether this directory is (or sits inside) node_modules, and
    //     whether it has a node_modules child,
    //   - an optional package.json at this level plus the nearest enclosing
    //     package and browser scope,
    //   - an optional neighbouring configuration document, unless the run
    //     already has an override,
    //   - a Yarn Plug'n'Play manifest path when one lives here and the run has
    //     not settled on a manifest yet,
    //   - the real (symlink-resolved) path, unless preservation is enabled.
    //
    // Permission-denied reads degrade to an empty listing so resolution can
    // keep going with the visible entries; any other read failure is reported
    // and fails the lookup.
    //
    //   Input:  "/app/src" containing package.json
    //   Output: annotated DirInfo for /app/src
    //   Input:  "/app/missing"
    //   Output: nullptr
    DirInfo* ResolverQuery::DirInfoUncached(const std::string& path)
    {
        DirInfo* parent_info = nullptr;
        std::string parent_dir = r->fs->Dir(path);
        if (parent_dir != path) {
            parent_info = DirInfoCached(parent_dir);

            if (parent_info == nullptr) {
                return nullptr;
            }
        }

        filesystem::FsResult<filesystem::DirEntries> read = r->fs->ReadDirectory(path);
        filesystem::DirEntries entries;
        if (IsEACCESOrEPERM(read.canonical_error)) {
            entries = filesystem::MakeEmptyDirEntries(path);
        } else if (!read.Ok()) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToReadDirectory2,
                        std::string(Q(path)),
                        ErrorText(read)
                ));
            }
            if (!IsENOENT(read.canonical_error) && !IsENOTDIR(read.canonical_error)) {
                logger::Path dir_path;
                dir_path.text      = path;
                dir_path.namespace_ = "file";
                logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, dir_path);
                r->log->AddError(nullptr, logger::Range{},
                                 logger::FormatMsg(logger::MsgCat::kResolver_CannotReadDirectory,
                                                   pretty_paths.Select(r->options.LogPathStyle),
                                                   ErrorText(read)));
            }
            return nullptr;
        } else {
            entries = std::move(read.value);
        }

        if (debug_logs && !read.Ok() && !read.original_error.empty()) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToReadDirectory2,
                    std::string(Q(path)),
                    read.original_error
            ));
        }

        DirInfo* info = new DirInfo{};
        info->abs_path = path;
        info->parent   = parent_info;
        info->entries  = std::move(entries);

        std::string base = r->fs->Base(path);
        if (base == "node_modules") {
            info->is_node_modules       = true;
            info->is_inside_node_modules = true;
        } else if (auto [entry, diff_case_unused] = info->entries.Get("node_modules"); entry != nullptr) {
            (void)diff_case_unused;
            info->has_node_modules = entry->Kind(*r->fs) == filesystem::EntryKind::kDir;
        }

        if (parent_info != nullptr) {
            info->enclosing_package_json  = parent_info->enclosing_package_json;
            info->enclosing_browser_scope = parent_info->enclosing_browser_scope;
            info->enclosing_tsconfig_json = parent_info->enclosing_tsconfig_json;
            if (parent_info->is_inside_node_modules) {
                info->is_inside_node_modules = true;
            }

            if (!r->options.PreserveSymlinks) {
                auto [entry, diff_case_unused] = parent_info->entries.Get(base);
                (void)diff_case_unused;
                if (entry) {
                    std::string symlink = entry->Symlink(*r->fs);
                    if (!symlink.empty()) {
                        if (debug_logs) {
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ResolvedSymlinkTo,
                                    std::string(Q(path)),
                                    std::string(Q(symlink))
                            ));
                        }
                        info->abs_real_path = symlink;
                    } else if (!parent_info->abs_real_path.empty()) {
                        std::string joined_symlink = r->fs->Join({parent_info->abs_real_path, base});
                        if (debug_logs) {
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ResolvedSymlinkTo,
                                    std::string(Q(path)),
                                    std::string(Q(joined_symlink))
                            ));
                        }
                        info->abs_real_path = joined_symlink;
                    }
                }
            }
        }

        {
            auto [entry, diff_case_unused] = info->entries.Get("package.json");
            (void)diff_case_unused;
            if (entry && entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                info->package_json = ParsePackageJSON(path);

                if (info->package_json != nullptr) {
                    info->enclosing_package_json = info->package_json;
                    if (!info->package_json->browser_map.empty()) {
                        info->enclosing_browser_scope = info;
                    }
                }
            }
        }

        if (r->ts_config_override == nullptr) {
            std::string ts_config_path;
            {
                auto [entry, diff_case_unused] = info->entries.Get("tsconfig.json");
                (void)diff_case_unused;
                if (entry && entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                    ts_config_path = r->fs->Join({path, "tsconfig.json"});
                }
            }
            if (ts_config_path.empty()) {
                auto [entry, diff_case_unused] = info->entries.Get("jsconfig.json");
                (void)diff_case_unused;
                if (entry && entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                    ts_config_path = r->fs->Join({path, "jsconfig.json"});
                }
            }

            if (!ts_config_path.empty() && !info->is_inside_node_modules) {
                std::unordered_map<std::string, bool> visited;
                TSConfigResult parsed = ParseTSConfig(ts_config_path, &visited, r->fs->Dir(ts_config_path));
                if (parsed.error != TSConfigError::kNone) {
                    logger::Path config_file_path;
                    config_file_path.text      = ts_config_path;
                    config_file_path.namespace_ = "file";
                    logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, config_file_path);
                    if (parsed.error == TSConfigError::kENOENT) {
                        r->log->AddError(nullptr, logger::Range{},
                                         logger::FormatMsg(logger::MsgCat::kResolver_CannotFindTSConfig,
                                                           pretty_paths.Select(r->options.LogPathStyle)));
                    } else if (parsed.error != TSConfigError::kAlreadyLogged) {
                        std::string message = parsed.error_message.empty() ? "(import cycle)" : parsed.error_message;
                        r->log->AddID(logger::MsgID::kTSConfigJSON_Missing, logger::MsgKind::kDebug, nullptr, logger::Range{},
                                      logger::FormatMsg(logger::MsgCat::kResolver_CannotReadFile,
                                                        pretty_paths.Select(r->options.LogPathStyle), message));
                    }
                } else {
                    info->enclosing_tsconfig_json = parsed.result;
                }
            }
        }

        if (r->pnp_manifest == nullptr) {
            if (!filesystem::ParseYarnPnPVirtualPath(path).has_value()) {
                auto find_manifest = [&](const char* name) -> std::string {
                    auto [entry, diff_case_unused] = info->entries.Get(name);
                    (void)diff_case_unused;
                    if (entry && entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                        return r->fs->Join({path, name});
                    }
                    return "";
                };
                info->pnp_manifest_abs_path = find_manifest(".pnp.data.json");
                if (info->pnp_manifest_abs_path.empty()) {
                    info->pnp_manifest_abs_path = find_manifest(".pnp.cjs");
                }
                if (info->pnp_manifest_abs_path.empty()) {
                    info->pnp_manifest_abs_path = find_manifest(".pnp.js");
                }
            }
        }

        return info;
    }

    // Probes a single directory for one base name, trying it verbatim first,
    // then each extension in the supplied order, then the type-annotated
    // replacements for a plain JavaScript extension. On the first hit the
    // absolute path and any case-difference flag are returned; otherwise the
    // caller receives an empty result.
    //
    //   Input:  path = "/app/src/util", order = [".js", ".json"]
    //   Output: "/app/src/util.js" when present
    //   Input:  no spelling of the name exists
    //   Output: empty FileResult
    FileResult ResolverQuery::LoadAsFile(const std::string& path, const std::vector<std::string>& extension_order)
    {
        DebugIndentGuard indent_guard(debug_logs);
        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_AttemptingToLoadAsAFile,
                    std::string(Q(path))
            ));
        }

        std::string dir_path = r->fs->Dir(path);
        filesystem::FsResult<filesystem::DirEntries> read = r->fs->ReadDirectory(dir_path);
        if (!read.Ok()) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToReadDirectory2,
                        std::string(Q(dir_path)),
                        ErrorText(read)
                ));
            }
            if (!IsENOENT(read.canonical_error)) {
                logger::Path dir_logger_path;
                dir_logger_path.text      = dir_path;
                dir_logger_path.namespace_ = "file";
                logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, dir_logger_path);
                r->log->AddError(nullptr, logger::Range{},
                                 logger::FormatMsg(logger::MsgCat::kResolver_CannotReadDirectory,
                                                   pretty_paths.Select(r->options.LogPathStyle),
                                                   ErrorText(read)));
            }
            return {};
        }
        filesystem::DirEntries& entries = read.value;

        auto try_file = [&](const std::string& base) -> FileResult {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_CheckingForFile, std::string(Q(base))));
            }
            auto [entry, diff_case] = entries.Get(base);
            if (entry && entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FoundFile, std::string(Q(base))));
                }
                return FileResult{.absolute = r->fs->Join({dir_path, base}), .ok = true, .diff_case = diff_case};
            }
            return {};
        };

        std::string base = r->fs->Base(path);

        {
            FileResult found = try_file(base);
            if (found.ok) {
                return found;
            }
        }

        for (const std::string& ext : extension_order) {
            FileResult found = try_file(base + ext);
            if (found.ok) {
                return found;
            }
        }

        for (const auto& [old_ext, exts] : RewrittenFileExtensions()) {
            if (!base.ends_with(old_ext)) {
                continue;
            }
            size_t last_dot = base.rfind('.');
            for (const std::string& ext : exts) {
                FileResult found = try_file(base.substr(0, last_dot) + ext);
                if (found.ok) {
                    return found;
                }
            }
            break;
        }

        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToFindFile, std::string(Q(base))));
        }
        return {};
    }

    // Implements the directory index convention: within a directory's snapshot,
    // look for an "index" file carrying any of the given extensions and return
    // the first one found. This is the last resort after "main" fields have
    // been exhausted.
    //
    //   Input:  dir_info containing "index.js", order = [".js", ".mjs"]
    //   Output: "<dir>/index.js"
    //   Input:  no index file at all
    //   Output: empty LoadResult
    LoadResult ResolverQuery::LoadAsIndex(DirInfo* dir_info, const std::vector<std::string>& extension_order)
    {
        for (const std::string& ext : extension_order) {
            std::string base = "index" + ext;
            auto [entry, diff_case] = dir_info->entries.Get(base);
            if (entry && entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FoundFile,
                            std::string(Q(r->fs->Join({dir_info->abs_path, base})))
                    ));
                }
                LoadResult result;
                result.pair.primary.text      = r->fs->Join({dir_info->abs_path, base});
                result.pair.primary.namespace_ = "file";
                result.ok                      = true;
                result.diff_case               = diff_case;
                return result;
            }
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToFindFile,
                        std::string(Q(r->fs->Join({dir_info->abs_path, base})))
                ));
            }
        }

        return {};
    }

    // The index search with the enclosing browser map applied first. The
    // "<dir>/index" path is looked up in the map: a "false" entry disables the
    // index outright (a disabled path is returned), a string entry redirects
    // the probe to another file or to that file's own directory index, and no
    // entry at all falls through to the plain LoadAsIndex search.
    //
    //   Input:  package whose map pairs ".../index" with "./alt.js", and
    //           alt.js is present
    //   Output: "<dir>/alt.js"
    //   Input:  no map entry
    //   Output: "<dir>/index.js"
    LoadResult ResolverQuery::LoadAsIndexWithBrowserRemapping(DirInfo* dir_info,
                                                              const std::string& path,
                                                              const std::vector<std::string>& extension_order)
    {
        std::string abs_path = r->fs->Join({path, "index"});
        BrowserRemapResult remap = CheckBrowserMap(dir_info, abs_path, BrowserPathKind::kAbsolutePath);
        if (remap.ok) {
            if (!remap.remapped.has_value()) {
                LoadResult result;
                result.pair.primary.text      = abs_path;
                result.pair.primary.namespace_ = "file";
                result.pair.primary.flags      = logger::PathFlags::kPathDisabled;
                result.ok                      = true;
                return result;
            }
            std::string remapped_abs = r->fs->Join({path, *remap.remapped});

            FileResult file = LoadAsFile(remapped_abs, extension_order);
            if (file.ok) {
                LoadResult result;
                result.pair.primary.text      = file.absolute;
                result.pair.primary.namespace_ = "file";
                result.ok                      = true;
                result.diff_case               = file.diff_case;
                return result;
            }

            if (DirInfo* field_dir_info = DirInfoCached(remapped_abs); field_dir_info != nullptr) {
                LoadResult index = LoadAsIndex(field_dir_info, extension_order);
                if (index.ok) {
                    return index;
                }
            }

            return {};
        }

        return LoadAsIndex(dir_info, extension_order);
    }

    // Tries the file route (with all its extension probing) and, only if that
    // misses, the directory route (with "main" fields and index files). The
    // extension order is chosen by import kind: CSS imports use the CSS-only
    // order, paths inside node_modules use the node_modules order, and
    // everything else uses the plain order.
    //
    //   Input:  "/app/src/util"          ->  "<dir>/util.js" or similar
    //   Input:  "/app/pkg" with "main"   ->  "<pkg>/<main>"
    //   Input:  nothing matches          ->  LoadResult{ok = false}
    LoadResult ResolverQuery::LoadAsFileOrDirectory(const std::string& path)
    {
        const std::vector<std::string>* extension_order = &r->options.ExtensionOrder;
        if (compiler::ImportKindMustResolveToCSS(kind)) {
            extension_order = &r->css_extension_order;
        } else if (helpers::IsInsideNodeModules(path)) {
            extension_order = &r->node_modules_extension_order;
        }

        FileResult file = LoadAsFile(path, *extension_order);
        if (file.ok) {
            LoadResult result;
            result.pair.primary.text      = file.absolute;
            result.pair.primary.namespace_ = "file";
            result.ok                      = true;
            result.diff_case               = file.diff_case;
            return result;
        }

        return LoadAsDirectory(path);
    }

    // Resolves a path whose target is expected to be a directory: it consults
    // the directory's package.json "main" fields first and then its index
    // files, selecting the extension order by context. Both sub-steps live in
    // their own functions; this one just sequences them.
    //
    //   Input:  "/app/pkg" with package.json "main": "./entry.js"
    //   Output: LoadResult for "/app/pkg/entry.js"
    //   Input:  a directory with no usable entry
    //   Output: LoadResult{ok = false}
    LoadResult ResolverQuery::LoadAsDirectory(const std::string& path)
    {
        const std::vector<std::string>* extension_order = &r->options.ExtensionOrder;
        if (compiler::ImportKindMustResolveToCSS(kind)) {
            extension_order = &r->css_extension_order;
        } else if (helpers::IsInsideNodeModules(path)) {
            extension_order = &r->node_modules_extension_order;
        }

        DebugIndentGuard indent_guard(debug_logs);
        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_AttemptingToLoadAsADirectory,
                    std::string(Q(path))
            ));
        }
        DirInfo* dir_info = DirInfoCached(path);
        if (dir_info == nullptr) {
            return {};
        }

        {
            LoadResult loaded = LoadAsMainField(dir_info, path, *extension_order);
            if (loaded.ok) {
                return loaded;
            }
        }

        {
            LoadResult loaded = LoadAsIndexWithBrowserRemapping(dir_info, path, *extension_order);
            if (loaded.ok) {
                return loaded;
            }
        }

        return {};
    }

    // Implements package.json "main"-field resolution. The configured (or
    // default) field order is walked, and the first field that names a
    // loadable file wins. Two complications make this function longer than its
    // name suggests:
    //
    //   - A "module" hit under the automatic order is checked alongside the
    //     package's "main" field (or its index file) so both paths are known.
    //     For import-style kinds the result carries both, primary = module and
    //     secondary = main, letting the bundler pick per import kind; for
    //     require-style kinds only the main field is used.
    //   - When the package declares a main-like field that was never
    //     consulted, a note is recorded explaining which field was ignored and
    //     why, instead of failing silently.
    //
    //   Input:  package.json {"main":"./a.js","module":"./b.js"}, browser
    //           platform, import-style kind
    //   Output: primary = "<pkg>/b.js", secondary = "<pkg>/a.js"
    //   Input:  package.json {"main":"./a.js"} but no fields configured
    //   Output: empty LoadResult plus an ignored-"main" note
    LoadResult ResolverQuery::LoadAsMainField(DirInfo* dir_info,
                                              const std::string& path,
                                              const std::vector<std::string>& extension_order)
    {
        if (dir_info->package_json == nullptr) {
            return {};
        }

        const std::map<std::string, MainField>& main_field_values = dir_info->package_json->main_fields;
        const std::vector<std::string>* main_field_keys = nullptr;
        bool auto_main = false;

        if (!r->options.MainFieldsSet && r->options.MainFields.empty()) {
            main_field_keys = &DefaultMainFields(r->options.OutputPlatform);
            auto_main       = true;
        } else {
            main_field_keys = &r->options.MainFields;
        }

        auto load_main_field = [&](const std::string& field_rel_path, const std::string& field) -> LoadResult {
            DebugIndentGuard indent_guard(debug_logs);
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FoundMainFieldWithPath,
                        std::string(Q(field)),
                        std::string(Q(field_rel_path))
                ));
            }

            std::string field_abs_path = r->fs->Join({path, field_rel_path});
            BrowserRemapResult remap = CheckBrowserMap(dir_info, field_abs_path, BrowserPathKind::kAbsolutePath);
            if (remap.ok) {
                if (!remap.remapped.has_value()) {
                    LoadResult result;
                    result.pair.primary.text      = field_abs_path;
                    result.pair.primary.namespace_ = "file";
                    result.pair.primary.flags      = logger::PathFlags::kPathDisabled;
                    result.ok                      = true;
                    return result;
                }
                field_abs_path = r->fs->Join({path, *remap.remapped});
            }

            FileResult file = LoadAsFile(field_abs_path, extension_order);
            if (file.ok) {
                LoadResult result;
                result.pair.primary.text      = file.absolute;
                result.pair.primary.namespace_ = "file";
                result.ok                      = true;
                result.diff_case               = file.diff_case;
                return result;
            }

            if (DirInfo* field_dir_info = DirInfoCached(field_abs_path); field_dir_info != nullptr) {
                LoadResult index = LoadAsIndexWithBrowserRemapping(field_dir_info, field_abs_path, extension_order);
                if (index.ok) {
                    return index;
                }
            }

            return {};
        };

        DebugIndentGuard outer_indent_guard(debug_logs);
        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_SearchingForMainFieldsIn,
                    std::string(Q(dir_info->package_json->source.key_path.text))
            ));
        }

        bool found_something = false;

        for (const std::string& key : *main_field_keys) {
            auto value_it = main_field_values.find(key);
            if (value_it == main_field_values.end()) {
                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_DidNotFindMainField, std::string(Q(key))));
                }
                continue;
            }
            found_something = true;

            LoadResult loaded = load_main_field(value_it->second.rel_path, key);
            if (!loaded.ok) {
                continue;
            }

            if (auto_main && key == "module") {
                PathPair absolute_main;
                bool ok_main = false;
                std::optional<filesystem::DifferentCase> diff_case_main;

                auto main_it = main_field_values.find("main");
                if (main_it != main_field_values.end()) {
                    LoadResult main_loaded = load_main_field(main_it->second.rel_path, "main");
                    if (main_loaded.ok) {
                        absolute_main = main_loaded.pair;
                        ok_main       = true;
                        diff_case_main = main_loaded.diff_case;
                    }
                } else {
                    LoadResult index_loaded = LoadAsIndexWithBrowserRemapping(dir_info, path, extension_order);
                    if (index_loaded.ok) {
                        absolute_main = index_loaded.pair;
                        ok_main       = true;
                        diff_case_main = index_loaded.diff_case;
                    }
                }

                if (ok_main) {
                    if (kind != compiler::ImportKind::kRequire) {
                        if (debug_logs) {
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ResolvedToUsingTheModuleFieldIn,
                                    std::string(Q(loaded.pair.primary.text)),
                                    std::string(Q(dir_info->package_json->source.key_path.text))
                            ));
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_TheFallbackPathInCaseOfRequireIs,
                                    std::string(Q(absolute_main.primary.text))
                            ));
                        }
                        LoadResult result;
                        result.pair.primary   = loaded.pair.primary;
                        result.pair.secondary = absolute_main.primary;
                        result.ok             = true;
                        result.diff_case      = loaded.diff_case;
                        return result;
                    } else {
                        if (debug_logs) {
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ResolvedToBecauseOfRequire,
                                    std::string(Q(absolute_main.primary.text))
                            ));
                        }
                        LoadResult result;
                        result.pair      = absolute_main;
                        result.ok        = true;
                        result.diff_case = diff_case_main;
                        return result;
                    }
                }
            }

            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ResolvedToUsingTheFieldIn,
                        std::string(Q(loaded.pair.primary.text)),
                        std::string(Q(key)),
                        std::string(Q(dir_info->package_json->source.key_path.text))
                ));
            }
            return loaded;
        }

        if (!found_something) {
            for (const std::string& field : MainFieldsForFailure()) {
                auto main_it = main_field_values.find(field);
                if (main_it == main_field_values.end()) {
                    continue;
                }
                logger::LineColumnTracker tracker(&dir_info->package_json->source);
                logger::Range key_range = dir_info->package_json->source.RangeOfString(main_it->second.key_loc);
                if (main_field_keys->empty() && r->options.OutputPlatform == config::Platform::kNeutral) {
                    debug_meta->notes.push_back(tracker.MakeMsgData(
                            key_range,
                            logger::FormatMsg(logger::MsgCat::kResolver_MainFieldIgnoredNeutral, field)));
                } else {
                    debug_meta->notes.push_back(tracker.MakeMsgData(
                            key_range,
                            logger::FormatMsg(logger::MsgCat::kResolver_MainFieldIgnoredList,
                                              field, helpers::StringArrayToQuotedCommaSeparatedString(*main_field_keys))));
                }
                break;
            }
        }

        return {};
    }

    // Applies the path-remapping table of a project configuration document to a
    // bare specifier. Exact key matches are tried first and, on a miss, keys
    // containing "*" compete as fuzzy matches: the key with the longest prefix
    // (then the longest suffix) wins, and the text the specifier contributes
    // between prefix and suffix replaces the "*" in each substitute path.
    // Substitute paths that resolve to declaration files are skipped, and
    // every rendered path is resolved against the document's base URL.
    //
    //   Input:  path = "pkg/util", remap {"pkg/*": ["src/*.js"]}
    //   Output: resolved "src/util.js" relative to the base URL
    //   Input:  no key matches
    //   Output: empty LoadResult
    LoadResult ResolverQuery::MatchTSConfigPaths(TSConfigJSON* tsconfig, const std::string& path)
    {
        DebugIndentGuard indent_guard(debug_logs);
        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_MatchingAgainstPathsIn,
                    std::string(Q(path)),
                    std::string(Q(tsconfig->abs_path))
            ));
        }

        std::string abs_base_url = tsconfig->base_url_for_paths;

        if (tsconfig->base_url.has_value()) {
            abs_base_url = *tsconfig->base_url;
        }

        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_UsingAsBaseUrl,
                    std::string(Q(abs_base_url))
            ));
        }

        for (const auto& [key, original_paths] : tsconfig->paths->map) {
            if (key == path) {
                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FoundAnExactMatchForInPaths,
                            std::string(Q(key))
                    ));
                }
                for (const TSConfigPath& original_path : original_paths) {
                    if (HasCaseInsensitiveSuffix(original_path.text, ".d.ts")) {
                        if (debug_logs) {
                            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_IgnoringSubstitutionBecauseItEndsInDTs,
                                    std::string(Q(original_path.text))
                            ));
                        }
                        continue;
                    }

                    std::string absolute_original_path = original_path.text;
                    if (!r->fs->IsAbs(absolute_original_path)) {
                        absolute_original_path = r->fs->Join({abs_base_url, absolute_original_path});
                    }
                    LoadResult loaded = LoadAsFileOrDirectory(absolute_original_path);
                    if (loaded.ok) {
                        return loaded;
                    }
                }
                return {};
            }
        }

        struct Match {
            std::string                prefix;
            std::string                suffix;
            const std::vector<TSConfigPath>* original_paths = nullptr;
        };

        long longest_match_prefix_length  = -1;
        long longest_match_suffix_length  = -1;
        Match longest_match;

        for (const auto& [key, original_paths] : tsconfig->paths->map) {
            size_t star_index = key.find('*');
            if (star_index != std::string::npos) {
                std::string prefix = key.substr(0, star_index);
                std::string suffix = key.substr(star_index + 1);

                if (path.starts_with(prefix) && path.ends_with(suffix) &&
                    (static_cast<long>(prefix.size()) > longest_match_prefix_length ||
                     (static_cast<long>(prefix.size()) == longest_match_prefix_length &&
                      static_cast<long>(suffix.size()) > longest_match_suffix_length))) {
                    longest_match_prefix_length = static_cast<long>(prefix.size());
                    longest_match_suffix_length = static_cast<long>(suffix.size());
                    longest_match.prefix        = std::move(prefix);
                    longest_match.suffix        = std::move(suffix);
                    longest_match.original_paths = &original_paths;
                }
            }
        }

        if (longest_match_prefix_length != -1) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FoundAFuzzyMatchForInPaths,
                        std::string(Q(longest_match.prefix + "*" + longest_match.suffix))
                ));
            }

            for (const TSConfigPath& original_path : *longest_match.original_paths) {
                std::string matched_text = path.substr(longest_match.prefix.size(),
                                                       path.size() - longest_match.prefix.size() - longest_match.suffix.size());
                std::string substituted = ReplaceFirstStar(original_path.text, matched_text);

                if (HasCaseInsensitiveSuffix(substituted, ".d.ts")) {
                    if (debug_logs) {
                        debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_IgnoringSubstitutionBecauseItEndsInDTs,
                                std::string(Q(substituted))
                        ));
                    }
                    continue;
                }

                std::string absolute_original_path = substituted;
                if (!r->fs->IsAbs(substituted)) {
                    absolute_original_path = r->fs->Join({abs_base_url, substituted});
                }
                LoadResult loaded = LoadAsFileOrDirectory(absolute_original_path);
                if (loaded.ok) {
                    return loaded;
                }
            }
        }

        return {};
    }

    // Handles Node's own modules for Node-platform output. A plain built-in name
    // is returned as an external, side-effect-free import. A "node:"-prefixed
    // name is also external, but its prefix is dropped only when the target
    // environment is too old to parse the prefix for the specific import kind
    // (import statements versus require calls). Anything else returns nothing,
    // so the normal search can proceed.
    //
    //   Input:  "fs"        ->  external, side-effect-free
    //   Input:  "node:fs"   ->  external (prefix stripped only for old
    //                            targets)
    //   Input:  "lodash"    ->  nullopt
    std::optional<ResolveResult> ResolverQuery::CheckForBuiltInNodeModules(std::string import_path)
    {
        if (r->options.OutputPlatform == config::Platform::kNode && IsBuiltInNodeModule(import_path)) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_MarkingThisPathAsImplicitlyExternalDueToItBeingANodeBuiltIn));
            }

            FlushDebugLogs(FlushMode::kDueToSuccess);
            ResolveResult result;
            result.path_pair.primary.text = import_path;
            result.path_pair.is_external  = true;
            result.primary_side_effects_data = std::make_shared<SideEffectsData>();
            return result;
        }

        if (r->options.OutputPlatform == config::Platform::kNode && import_path.starts_with("node:")) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_MarkingThisPathAsImplicitlyExternalDueToTheNodePrefix));
            }

            std::shared_ptr<SideEffectsData> side_effects;
            if (IsBuiltInNodeModule(import_path.substr(5))) {
                side_effects = std::make_shared<SideEffectsData>();
            }

            bool convert_import_to_require = !config::FormatKeepESMImportExportSyntax(r->options.OutputFormat);
            bool is_import = !convert_import_to_require && (kind == compiler::ImportKind::kStmt || kind == compiler::ImportKind::kDynamic);
            bool is_require = kind == compiler::ImportKind::kRequire || kind == compiler::ImportKind::kRequireResolve ||
                              (convert_import_to_require && (kind == compiler::ImportKind::kStmt || kind == compiler::ImportKind::kDynamic));

            if (is_import && compat::Has(r->options.UnsupportedJSFeatures, compat::JSFeature::kNodeColonPrefixImport)) {
                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_RemovingTheNodePrefixBecauseTheTargetEnvironmentDoesnTSupportItWithImportStatements));
                }

                import_path = import_path.substr(5);
            }

            if (is_require && compat::Has(r->options.UnsupportedJSFeatures, compat::JSFeature::kNodeColonPrefixRequire)) {
                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_RemovingTheNodePrefixBecauseTheTargetEnvironmentDoesnTSupportItWithRequireCalls));
                }

                import_path = import_path.substr(5);
            }

            FlushDebugLogs(FlushMode::kDueToSuccess);
            ResolveResult result;
            result.path_pair.primary.text    = import_path;
            result.path_pair.is_external     = true;
            result.primary_side_effects_data = std::move(side_effects);
            return result;
        }

        return std::nullopt;
    }

    // The node_modules walker: starting from a directory, it climbs the tree
    // trying each node_modules folder for the requested package and returns
    // the first hit. Several early branches shape the search before any
    // scanning happens:
    //
    //   - the governing project configuration's path remapping and base URL
    //     are consulted first,
    //   - hash-prefixed specifiers resolve through the enclosing package's
    //     "#imports" map,
    //   - the ExternalPackages setting short-circuits package paths to
    //     external,
    //   - a Yarn Plug'n'Play manifest, when one governs the importer, resolves
    //     the package without touching node_modules at all.
    //
    // Each candidate directory is checked through its package's export map
    // (when present), its browser map, and finally plain file or directory
    // loading. After the tree is exhausted the run's explicit, absolute module
    // paths are tried. The outcome bundles the resolved path pair, a
    // case-difference flag, and the side-effects data so callers need not
    // re-derive them.
    //
    //   Input:  import_path = "lodash", a directory under "/app/src"
    //   Output: ok = true, "/app/node_modules/lodash/index.js"
    //   Input:  present nowhere up the tree
    //   Output: ok = false
    SideEffectsResult ResolverQuery::LoadNodeModules(const std::string& import_path_in,
                                                     DirInfo*           dir_info_in,
                                                     bool               forbid_imports)
    {
        std::string import_path = import_path_in;
        DirInfo* dir_info       = dir_info_in;

        DebugIndentGuard indent_guard(debug_logs);
        if (debug_logs) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_SearchingForInNodeModulesDirectoriesStartingFrom,
                    std::string(Q(import_path)),
                    std::string(Q(dir_info->abs_path))
            ));
        }

        if (TSConfigJSON* tsconfig_json = TSConfigForDir(dir_info); tsconfig_json != nullptr) {
            if (tsconfig_json->paths != nullptr) {
                LoadResult loaded = MatchTSConfigPaths(tsconfig_json, import_path);
                if (loaded.ok) {
                    SideEffectsResult result;
                    result.pair      = loaded.pair;
                    result.ok        = loaded.ok;
                    result.diff_case = loaded.diff_case;
                    return result;
                }
            }

            if (tsconfig_json->base_url.has_value()) {
                std::string base_path = r->fs->Join({*tsconfig_json->base_url, import_path});
                LoadResult loaded = LoadAsFileOrDirectory(base_path);
                if (loaded.ok) {
                    SideEffectsResult result;
                    result.pair      = loaded.pair;
                    result.ok        = loaded.ok;
                    result.diff_case = loaded.diff_case;
                    return result;
                }
            }
        }

        DirInfo* dir_info_package_json = dir_info;
        while (dir_info_package_json != nullptr && dir_info_package_json->package_json == nullptr) {
            dir_info_package_json = dir_info_package_json->parent;
        }

        if (dir_info_package_json != nullptr && import_path.starts_with("#") && !forbid_imports &&
            dir_info_package_json->package_json->imports_map != nullptr) {
            return LoadPackageImports(import_path, dir_info_package_json);
        }

        if (r->options.ExternalPackages && IsPackagePath(import_path)) {
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_MarkingThisPathAsExternalBecauseItSAPackagePath));
            }
            SideEffectsResult result;
            result.pair.primary.text = import_path;
            result.pair.is_external  = true;
            result.ok                = true;
            return result;
        }

        if (r->pnp_manifest != nullptr) {
            PnpResult pnp_result = ResolveToUnqualified(import_path, dir_info->abs_path, r->pnp_manifest);
            if (PnpStatusIsError(pnp_result.status)) {
                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_TheYarnPnPPathResolutionAlgorithmReturnedAnError));
                }

                switch (pnp_result.status) {
                    case PnpStatus::kErrorDependencyNotFound: {
                        debug_meta->notes.clear();
                        debug_meta->notes.push_back(r->pnp_manifest->tracker.MakeMsgData(
                                pnp_result.error_range,
                                logger::FormatMsg(logger::MsgCat::kResolver_PnPForbidsImport,
                                        pnp_result.error_ident)));
                        break;
                    }
                    case PnpStatus::kErrorUnfulfilledPeerDependency: {
                        debug_meta->notes.clear();
                        debug_meta->notes.push_back(r->pnp_manifest->tracker.MakeMsgData(
                                pnp_result.error_range,
                                logger::FormatMsg(logger::MsgCat::kResolver_PnPPeerDependency,
                                        pnp_result.error_ident, pnp_result.error_ident)));
                        break;
                    }
                    default: break;
                }

                return {};
            } else if (pnp_result.status == PnpStatus::kSuccess) {
                std::string abs_path = r->fs->Join({pnp_result.pkg_dir_path, pnp_result.pkg_subpath});

                if (DirInfo* pkg_dir_info = DirInfoCached(pnp_result.pkg_dir_path); pkg_dir_info != nullptr) {
                    PackageJSON* package_json = pkg_dir_info->package_json;
                    if (package_json != nullptr && package_json->exports_map != nullptr) {
                        LoadResult loaded = EsmResolveAlgorithm(FinalizeImportsExportsKind::kNormal,
                                                                pnp_result.pkg_ident, "." + pnp_result.pkg_subpath,
                                                                package_json, pkg_dir_info->abs_path, abs_path);
                        SideEffectsResult result;
                        result.pair      = loaded.pair;
                        result.ok        = loaded.ok;
                        result.diff_case = loaded.diff_case;
                        return result;
                    }

                    BrowserRemapResult remap = CheckBrowserMap(pkg_dir_info, abs_path, BrowserPathKind::kAbsolutePath);
                    if (remap.ok) {
                        if (!remap.remapped.has_value()) {
                            SideEffectsResult result;
                            result.pair.primary.text      = abs_path;
                            result.pair.primary.namespace_ = "file";
                            result.pair.primary.flags      = logger::PathFlags::kPathDisabled;
                            result.ok                      = true;
                            return result;
                        }
                        SideEffectsResult remapped_result =
                                ResolveWithoutRemapping(pkg_dir_info->enclosing_browser_scope, *remap.remapped);
                        if (remapped_result.ok) {
                            return remapped_result;
                        }
                    }

                    LoadResult loaded = LoadAsFileOrDirectory(abs_path);                    if (loaded.ok) {
                        SideEffectsResult result;
                        result.pair      = loaded.pair;
                        result.ok        = loaded.ok;
                        result.diff_case = loaded.diff_case;
                        return result;
                    }
                }

                if (debug_logs) {
                    debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_FailedToResolveToAFile,
                            std::string(Q(abs_path))
                    ));
                }
                return {};
            }
        }

        std::string esm_package_name;
        std::string esm_package_subpath;
        bool esm_ok = EsmParsePackageName(import_path, esm_package_name, esm_package_subpath);
        if (debug_logs && esm_ok) {
            debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_ParsedPackageNameAndPackageSubpath,
                    std::string(Q(esm_package_name)),
                    std::string(Q(esm_package_subpath))
            ));
        }

        if (dir_info_package_json != nullptr) {
            PackageJSON* package_json = dir_info_package_json->package_json;
            if (package_json->name == esm_package_name && package_json->exports_map != nullptr) {
                LoadResult loaded = EsmResolveAlgorithm(FinalizeImportsExportsKind::kNormal, esm_package_name,
                                                        esm_package_subpath, package_json,
                                                        dir_info_package_json->abs_path,
                                                        r->fs->Join({dir_info_package_json->abs_path, esm_package_subpath}));
                SideEffectsResult result;
                result.pair      = loaded.pair;
                result.ok        = loaded.ok;
                result.diff_case = loaded.diff_case;
                return result;
            }
        }

        struct TryPackageOutcome {
            SideEffectsResult result;
            bool should_stop = false;
        };

        auto try_to_resolve_package = [&](const std::string& abs_dir) -> TryPackageOutcome {
            std::string abs_path = r->fs->Join({abs_dir, import_path});
            if (debug_logs) {
                debug_logs->AddNote(logger::FormatMsg(logger::MsgCat::kResolverDebug_CheckingForAPackageInTheDirectory,
                        std::string(Q(abs_path))
                ));
            }

            if (esm_ok) {
                std::string abs_pkg_path = r->fs->Join({abs_dir, esm_package_name});
                if (DirInfo* pkg_dir_info = DirInfoCached(abs_pkg_path); pkg_dir_info != nullptr) {
                    PackageJSON* package_json = pkg_dir_info->package_json;
                    if (package_json != nullptr && package_json->exports_map != nullptr) {
                        LoadResult loaded = EsmResolveAlgorithm(FinalizeImportsExportsKind::kNormal, esm_package_name,
                                                                esm_package_subpath, package_json, abs_pkg_path, abs_path);
                        SideEffectsResult result;
                        result.pair      = loaded.pair;
                        result.ok        = loaded.ok;
                        result.diff_case = loaded.diff_case;
                        return TryPackageOutcome{.result = std::move(result), .should_stop = true};
                    }

                    BrowserRemapResult remap = CheckBrowserMap(pkg_dir_info, abs_path, BrowserPathKind::kAbsolutePath);
                    if (remap.ok) {
                        if (!remap.remapped.has_value()) {
                            SideEffectsResult result;
                            result.pair.primary.text      = abs_path;
                            result.pair.primary.namespace_ = "file";
                            result.pair.primary.flags      = logger::PathFlags::kPathDisabled;
                            result.ok                      = true;
                            return TryPackageOutcome{.result = std::move(result), .should_stop = true};
                        }
                        SideEffectsResult remapped_result =
                                ResolveWithoutRemapping(pkg_dir_info->enclosing_browser_scope, *remap.remapped);
                        if (remapped_result.ok) {
                            return TryPackageOutcome{.result = std::move(remapped_result), .should_stop = true};
                        }
                    }
                }
            }

            LoadResult loaded = LoadAsFileOrDirectory(abs_path);
            if (loaded.ok) {
                SideEffectsResult result;
                result.pair      = loaded.pair;
                result.ok        = loaded.ok;
                result.diff_case = loaded.diff_case;
                return TryPackageOutcome{.result = std::move(result), .should_stop = true};
            }

            return {};
        };

        while (true) {
            if (dir_info->has_node_modules) {
                TryPackageOutcome attempt =
                        try_to_resolve_package(r->fs->Join({dir_info->abs_path, "node_modules"}));
                if (attempt.should_stop) {
                    return std::move(attempt.result);
                }
            }

            dir_info = dir_info->parent;
            if (dir_info == nullptr) {
                break;
            }
        }

        for (const std::string& abs_dir : r->options.AbsNodePaths) {
            TryPackageOutcome attempt = try_to_resolve_package(abs_dir);
            if (attempt.should_stop) {
                return std::move(attempt.result);
            }
        }

        return {};
    }

} // namespace guchho::resolver

namespace guchho::resolver::internal {

    static bool IsHexDigitChar(char c)
    {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    }

    static int HexCharValue(char c)
    {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return c - 'A' + 10;
    }

    bool UnescapePath(std::string_view text, std::string& out, std::string& error)
    {
        out.clear();

        for (size_t i = 0; i < text.size();) {
            char c = text[i];
            if (c == '%') {
                if (i + 2 >= text.size() || !IsHexDigitChar(text[i + 1]) ||
                    !IsHexDigitChar(text[i + 2])) {
                    size_t length = std::min<size_t>(3, text.size() - i);
                    error         = "invalid URL escape " +
                            helpers::QuoteForJSON(text.substr(i, length), false);
                    return false;
                }
                out.push_back(static_cast<char>(
                    HexCharValue(text[i + 1]) * 16 + HexCharValue(text[i + 2])));
                i += 3;
            } else {
                out.push_back(c);
                ++i;
            }
        }
        return true;
    }

} // namespace guchho::resolver::internal
