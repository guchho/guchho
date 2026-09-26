////////////////////////////////////////////////////////////////////////////////
// Bundler import resolution
//
// Drives the onResolve and onLoad plugin hooks and, when a bare import cannot be
// resolved, assembles the "could not resolve" error text with its hints and
// suggestion.
////////////////////////////////////////////////////////////////////////////////

#include "guchho/bundler.hpp"

#include <set>


namespace guchho::bundler {


    // Runs the onResolve plugin callbacks for one import, then falls back to the
    // built-in resolver.
    //
    // Plugins are tried in order; the first one that returns a result wins. Watch
    // files/dirs they report are read into the cache, and absolute-path rules are
    // enforced for the "file" namespace. A differing path case in a non-node_modules
    // directory is reported as a warning.
    //
    // Input : import path, importer, kind, attributes and resolve directory.
    // Output: OnResolveOutcome with the ResolveResult (or nullopt) and debug meta.
    OnResolveOutcome RunOnResolvePlugins(
        const std::vector<config::Plugin>& plugins,
        resolver::Resolver* res,
        Log log,
        Fs& fs,
        cache::FSCache& fs_cache,
        const Source* import_source,
        Range import_path_range,
        const Path& importer,
        const std::string& path,
        const logger::ImportAttributes& import_attributes,
        compiler::ImportKind kind,
        const std::string& abs_resolve_dir,
        const std::any& plugin_data,
        logger::PathStyle log_path_style)
    {
        config::OnResolveArgs resolver_args;
        resolver_args.PathData = path;
        resolver_args.ResolveDir = abs_resolve_dir;
        resolver_args.Kind = kind;
        resolver_args.PluginData = plugin_data;
        resolver_args.Importer = importer;
        resolver_args.With = import_attributes;

        Path apply_path{.text = path, .namespace_ = importer.namespace_};
        logger::LineColumnTracker tracker(import_source);

        for (const auto& plugin : plugins) {
            for (const auto& on_resolve : plugin.OnResolveList) {
                if (!config::PluginAppliesToPath(apply_path, on_resolve.Filter, on_resolve.Namespace)) {
                    continue;
                }

                config::OnResolveResult result = on_resolve.Callback(resolver_args);
                std::string plugin_name = result.PluginName;
                if (plugin_name.empty()) {
                    plugin_name = plugin.Name;
                }
                bool did_log_error = LogPluginMessages(fs, log, plugin_name, result.Msgs,
                    result.ThrownError, import_source, import_path_range);

                for (const auto& file : result.AbsWatchFiles) {
                    fs_cache.ReadFile(fs, file);
                }
                for (const auto& dir : result.AbsWatchDirs) {
                    auto entries_result = fs.ReadDirectory(dir);
                    if (entries_result.Ok()) {
                        entries_result.value.SortedKeys();
                    }
                }

                if (did_log_error) {
                    return {};
                }

                std::string ns_from_plugin = result.ResultPath.namespace_;
                if (result.ResultPath.namespace_.empty() && !result.External) {
                    result.ResultPath.namespace_ = "file";
                }

                if (result.ResultPath.text.empty()) {
                    if (result.External) {
                        result.ResultPath = Path{.text = path};
                    } else {
                        continue;
                    }
                }

                if (result.ResultPath.namespace_ == "file" && !fs.IsAbs(result.ResultPath.text)) {
                    if (ns_from_plugin == "file") {
                        log.AddError(&tracker, import_path_range,
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_PluginNotAbsolutePath, plugin_name,
                                result.ResultPath.text));
                    } else {
                        log.AddError(&tracker, import_path_range,
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_PluginNonAbsolutePath, plugin_name,
                                result.ResultPath.text));
                    }
                    return {};
                }

                std::shared_ptr<resolver::SideEffectsData> side_effects_data;
                if (result.IsSideEffectFree) {
                    side_effects_data = std::make_shared<resolver::SideEffectsData>();
                    side_effects_data->plugin_name = plugin_name;
                }

                resolver::ResolveResult resolve_result;
                resolve_result.path_pair.primary = result.ResultPath;
                resolve_result.path_pair.is_external = result.External;
                resolve_result.plugin_data = result.PluginData;
                resolve_result.primary_side_effects_data = side_effects_data;
                return OnResolveOutcome{
                    .resolve_result = std::move(resolve_result),
                    .did_log_error = false,
                };
            }
        }

        resolver::DebugMeta debug;
        std::optional<resolver::ResolveResult> result =
            res->Resolve(abs_resolve_dir, path, kind, &debug);

        if (result.has_value() && result->different_case.has_value() &&
            !helpers::IsInsideNodeModules(abs_resolve_dir))
        {
            const filesystem::DifferentCase& diff_case = *result->different_case;
            PrettyPaths actual_paths = resolver::MakePrettyPaths(fs,
                Path{.text = fs.Join({diff_case.dir, diff_case.actual}), .namespace_ = "file"});
            PrettyPaths query_paths = resolver::MakePrettyPaths(fs,
                Path{.text = fs.Join({diff_case.dir, diff_case.query}), .namespace_ = "file"});
            log.AddID(logger::MsgID::kBundler_DifferentPathCase, logger::MsgKind::kWarning, &tracker,
                import_path_range,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_DifferentPathCase,
                    actual_paths.Select(log_path_style), query_paths.Select(log_path_style)));
        }

        return OnResolveOutcome{
            .resolve_result = std::move(result),
            .did_log_error = false,
            .debug_meta = std::move(debug),
        };
    }

    // Runs the onLoad plugin callbacks for one source, then falls back to reading
    // the file, a disabled module, or a data URL.
    //
    // Plugins are tried in order and the first one that supplies contents wins; in
    // watch mode the loaded file is read into the cache so changes are detected.
    // Fallbacks cover disabled modules (empty), files on disk, and data: URLs whose
    // MIME type selects the JS/CSS/JSON loader.
    //
    // Input : the source, plugin data, watch flag and log path style.
    // Output: LoaderPluginResult plus the "ok" flag (false on a logged error).
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
        bool& ok)
    {
        config::OnLoadArgs loader_args;
        loader_args.PluginData = plugin_data;
        loader_args.LoadPath = source.key_path;
        logger::LineColumnTracker tracker(import_source);

        for (const auto& plugin : plugins) {
            for (const auto& on_load : plugin.OnLoadList) {
                if (!config::PluginAppliesToPath(source.key_path, on_load.Filter, on_load.Namespace)) {
                    continue;
                }

                config::OnLoadResult result = on_load.Callback(loader_args);
                std::string plugin_name = result.PluginName;
                if (plugin_name.empty()) {
                    plugin_name = plugin.Name;
                }
                bool did_log_error = LogPluginMessages(fs, log, plugin_name, result.Msgs,
                    result.ThrownError, import_source, import_path_range);

                for (const auto& file : result.AbsWatchFiles) {
                    fs_cache.ReadFile(fs, file);
                }
                for (const auto& dir : result.AbsWatchDirs) {
                    auto entries_result = fs.ReadDirectory(dir);
                    if (entries_result.Ok()) {
                        entries_result.value.SortedKeys();
                    }
                }

                if (did_log_error) {
                    if (is_watch_mode && source.key_path.namespace_ == "file") {
                        fs_cache.ReadFile(fs, source.key_path.text);
                    }
                    ok = false;
                    return {};
                }

                if (!result.Contents.has_value()) {
                    continue;
                }

                source.contents = *result.Contents;
                config::Loader loader = result.ResultLoader;
                if (loader == config::Loader::kNone) {
                    loader = config::Loader::kJS;
                }
                if (result.AbsResolveDir.empty() && source.key_path.namespace_ == "file") {
                    result.AbsResolveDir = fs.Dir(source.key_path.text);
                }
                if (is_watch_mode && source.key_path.namespace_ == "file") {
                    fs_cache.ReadFile(fs, source.key_path.text);
                }
                ok = true;
                return LoaderPluginResult{
                    .plugin_data = result.PluginData,
                    .abs_resolve_dir = result.AbsResolveDir,
                    .plugin_name = plugin_name,
                    .loader = loader,
                };
            }
        }

        if (source.key_path.IsDisabled()) {
            ok = true;
            LoaderPluginResult result;
            result.loader = config::Loader::kEmpty;
            return result;
        }

        if (source.key_path.namespace_ == "file") {
            auto contents_result = fs_cache.ReadFile(fs, source.key_path.text);
            if (contents_result.Ok()) {
                source.contents = std::move(contents_result.value);
                ok = true;
                LoaderPluginResult result;
                result.loader = config::Loader::kDefault;
                result.abs_resolve_dir = fs.Dir(source.key_path.text);
                return result;
            }

            if (contents_result.canonical_error == std::errc::no_such_file_or_directory) {
                log.AddError(&tracker, import_path_range,
                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotReadFile, source.key_path.text));
            } else {
                PrettyPaths pretty_paths = resolver::MakePrettyPaths(fs, source.key_path);
                log.AddError(&tracker, import_path_range,
                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotReadFileWithError,
                        pretty_paths.Select(log_path_style), contents_result.original_error));
            }
            ok = false;
            return {};
        }

        if (source.key_path.namespace_ == "dataurl") {
            std::optional<helpers::DataURL> parsed = helpers::ParseDataURL(source.key_path.text);
            if (parsed.has_value()) {
                std::string error_text;
                std::optional<std::string> contents = parsed->DecodeData(error_text);
                if (!contents.has_value()) {
                    log.AddError(&tracker, import_path_range,
                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CouldNotLoadDataURL, error_text));
                    ok = true;
                    LoaderPluginResult result;
                    result.loader = config::Loader::kNone;
                    return result;
                }

                source.contents = std::move(*contents);
                helpers::MIMEType mime_type = parsed->DecodeMIMEType();
                if (mime_type != helpers::MIMEType::kUnsupported) {
                    switch (mime_type) {
                        case helpers::MIMEType::kTextCSS:
                            ok = true;
                            {
                                LoaderPluginResult result;
                                result.loader = config::Loader::kCSS;
                                return result;
                            }
                        case helpers::MIMEType::kTextJavaScript:
                            ok = true;
                            {
                                LoaderPluginResult result;
                                result.loader = config::Loader::kJS;
                                return result;
                            }
                        case helpers::MIMEType::kApplicationJSON:
                            ok = true;
                            {
                                LoaderPluginResult result;
                                result.loader = config::Loader::kJSON;
                                return result;
                            }
                        default: break;
                    }
                }
            }
        }

        ok = true;
        LoaderPluginResult result;
        result.loader = config::Loader::kNone;
        return result;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Error message helpers
    //
    // Utilities that build the diagnostics shown when an import cannot be resolved.
    ////////////////////////////////////////////////////////////////////////////////

    // Returns true when the package name is one of node's built-in modules (e.g.
    // "fs", "path"), used to suggest "--platform=node" in resolution errors.
    //
    // Input : "fs".
    // Output: true.
    static bool IsBuiltInNodeModule(const std::string& pkg) {
        static const std::set<std::string> built_ins = {
            "assert",  "buffer",     "child_process", "cluster",   "console",
            "constants", "crypto",   "dgram",         "dns",       "domain",
            "events",  "fs",         "http",          "http2",     "https",
            "inspector", "module",   "net",           "os",        "path",
            "perf_hooks", "process", "punycode",     "querystring", "readline",
            "repl",    "stream",     "string_decoder", "sys",      "timers",
            "tls",     "trace_events", "tty",       "url",       "util",
            "v8",      "vm",         "worker_threads", "zlib",
        };
        return built_ins.count(pkg) > 0;
    }

    // Builds the human-readable text, notes and import-path suggestion for a failed
    // resolution.
    //
    // It explains alias remapping, suggests marking the path external (or wrapping
    // the require/dynamic import), points at the relative-path spelling when a
    // package was meant, mentions node built-ins, and notes when a plugin failed to
    // provide a resolve directory.
    //
    // Input : the resolver, path, import kind, plugin name and originating file.
    // Output: ResolveFailureInfo with text, notes and an optional suggestion.
    ResolveFailureInfo ResolveFailureErrorTextSuggestionNotes(
        resolver::Resolver& res,
        std::string path,
        compiler::ImportKind kind,
        const std::string& plugin_name,
        Fs& fs,
        const std::string& abs_resolve_dir,
        config::Platform platform,
        const PrettyPaths& originating_file_paths,
        const std::string& modified_import_path,
        logger::PathStyle log_path_style)
    {
        ResolveFailureInfo info;
        if (!modified_import_path.empty()) {
            info.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CouldNotResolveModifiedPath,
                modified_import_path, path);
            MsgData note;
            note.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_AliasRemappedPathNote,
                path, modified_import_path);
            info.notes.push_back(std::move(note));
            path = modified_import_path;
        } else {
            info.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CouldNotResolvePath, path);
        }
        std::string hint;

        if (resolver::IsPackagePath(path) && !fs.IsAbs(path)) {
            hint = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_MarkPathExternalHint, path);
            if (kind == compiler::ImportKind::kRequire) {
                hint += guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_RequireTryCatchHint);
            } else if (kind == compiler::ImportKind::kDynamic) {
                hint += guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_DynamicImportCatchHint);
            }
            if (plugin_name.empty() && !fs.IsAbs(path)) {
                resolver::DebugMeta throwaway_debug;
                std::optional<resolver::ResolveResult> query =
                    res.ProbeResolvePackageAsRelative(abs_resolve_dir, path, kind,
                        &throwaway_debug);
                if (query.has_value()) {
                    PrettyPaths pretty_paths = resolver::MakePrettyPaths(fs, query->path_pair.primary);
                    hint = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_UseRelativePathHint,
                        path, pretty_paths.Select(log_path_style), path);
                    info.suggestion = helpers::QuoteForJSON("./" + path, false);
                }
            }
        }

        if (platform != config::Platform::kNode) {
            std::string pkg = path;
            constexpr std::string_view node_prefix = "node:";
            if (pkg.rfind(node_prefix, 0) == 0) {
                pkg = pkg.substr(node_prefix.size());
            }
            if (IsBuiltInNodeModule(pkg)) {
                hint = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_BuiltIntoNodeHint, path);
            }
        }

        if (abs_resolve_dir.empty() && !plugin_name.empty()) {
            if (!originating_file_paths.abs.empty() || !originating_file_paths.rel.empty()) {
                hint = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_PluginNoResolveDirForFileHint,
                    plugin_name, originating_file_paths.Select(log_path_style), path);
            } else {
                hint = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_PluginNoResolveDirHint,
                    plugin_name, path);
            }
        }

        if (!hint.empty()) {
            if (!modified_import_path.empty()) {
                MsgData blank;
                info.notes.push_back(std::move(blank));

                info.suggestion.clear();
            }
            MsgData note;
            note.text = hint;
            info.notes.push_back(std::move(note));
        }
        return info;
    }

}

