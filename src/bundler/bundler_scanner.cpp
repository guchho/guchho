
// Scan phase driver: the implementation of the Scanner class methods
// (declared in guchho/bundler.hpp) plus the ScanBundle entry point. This is
// the part of the bundler that starts from the configured entry points and
// walks the whole module graph, parsing every reachable file once and
// resolving every import until the graph is closed. The scan is
// parallelized: parse work is pushed onto worker threads with Spawn(), and
// the results flow back through a Channel that the single ScanAllDependencies
// loop drains. The WaitGroup/TimerScope/runtime-cache helpers that make this
// happen are defined below and shared by every method in this file.

#include "guchho/bundler.hpp"
#include "guchho/html/html_analysis.hpp"
#include "guchho/javascript/js_runtime.hpp"


namespace guchho::bundler {


    // A counter that tracks how many concurrent pieces of work are still
    // outstanding. The scan phase uses it to wait for a set of worker
    // threads before continuing: each worker calls Add(1) before starting
    // and Done() when finished, and the coordinating thread calls Wait() to
    // block until the count reaches zero.
    class WaitGroup {
    public:
        void Add(int delta) {
            std::lock_guard<std::mutex> lock(mutex_);
            count_ += delta;
        }

        void Done() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                count_--;
            }
            cv_.notify_all();
        }

        void Wait() {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return count_ == 0; });
        }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        int count_ = 0;
    };


    // RAII helper that attributes a named region of scan time to the timing
    // report. Begin() runs on construction and End() on destruction, so a
    // region is always bracketed correctly even when the code inside returns
    // early or throws. Every phase of the scan is wrapped in one of these so
    // the "--timing" output shows where the time went.
    struct TimerScope {
        helpers::Timer* timer{};
        std::string name;

        TimerScope(helpers::Timer* t, std::string n)
            : timer(t), name(std::move(n))
        {
            if (timer != nullptr) {
                timer->Begin(name);
            }
        }
        ~TimerScope()
        {
            if (timer != nullptr) {
                timer->End(name);
            }
        }
    };

    // The runtime module is parsed once per distinct combination of the options
    // that change how it compiles: the set of JS features that must be
    // transformed away for the target browsers plus the two minification
    // switches. Together they form the key for the RuntimeCache below.
    struct RuntimeCacheKey {
        compat::JSFeature unsupported_js_features{};
        bool minify_syntax{};
        bool minify_identifiers{};

        bool operator==(const RuntimeCacheKey&) const = default;
    };

    // Hasher for RuntimeCacheKey. All three fields are mixed into a single
    // size_t; there are only three small integers involved so the hash is
    // cheap to compute and collisions are unlikely to matter in practice.
    struct RuntimeCacheKeyHash {
        size_t operator()(const RuntimeCacheKey& key) const {
            size_t hash = std::hash<uint64_t>()(static_cast<uint64_t>(key.unsupported_js_features));
            hash ^= std::hash<bool>()(key.minify_syntax) + 0x9e3779b97f4a7c15ULL +
                    (hash << 6) + (hash >> 2);
            hash ^= std::hash<bool>()(key.minify_identifiers) + 0x9e3779b97f4a7c15ULL +
                    (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    // A process-wide cache for the parsed runtime module. The runtime source
    // text is fixed, so parsing it fresh for every bundle would waste time;
    // instead the parsed AST is computed once per RuntimeCacheKey and reused
    // by every scan that needs the same variant. A lock guards the map
    // because several builds may scan concurrently.
    //
    // Parse(options) -> {source, ast, ok}: on a hit the stored AST is
    // returned along with a freshly constructed Source; on a miss the runtime
    // is parsed with tree shaking always forced on (the runtime must never
    // ship dead code) and the result is stored for next time. A failed
    // internal parse is not fatal: the error is logged and ok=false is
    // returned so the caller can surface it as a normal build diagnostic.
    class RuntimeCache {
    public:
        struct Parsed {
            Source source;
            javascript::AST ast;
            bool ok{};
        };

        Parsed Parse(config::Options& options) {
            RuntimeCacheKey key{
                .unsupported_js_features = options.UnsupportedJSFeatures,
                .minify_syntax           = options.MinifySyntax,
                .minify_identifiers      = options.MinifyIdentifiers,
            };

            Source source = javascript::Source(key.unsupported_js_features);

            {
                std::lock_guard<std::mutex> lock(ast_mutex_);
                if (!ast_map_.empty()) {
                    auto found = ast_map_.find(key);
                    if (found != ast_map_.end()) {
                        return Parsed{std::move(source), found->second, true};
                    }
                }
            }

            Log log = logger::NewDeferLog(logger::DeferLogKind::kDeferLogAll,
                std::unordered_map<logger::MsgID, logger::LogLevel>{});
            config::Options runtime_options;
            runtime_options.UnsupportedJSFeatures = key.unsupported_js_features;
            runtime_options.MinifySyntax          = key.minify_syntax;
            runtime_options.MinifyIdentifiers     = key.minify_identifiers;

            runtime_options.TreeShaking = true;

            auto [runtime_ast, ok] =
                javascript::Parse(log, source, javascript::OptionsFromConfig(&runtime_options));
            if (log.has_errors()) {
                std::string msgs = "Internal error: failed to parse runtime:\n";
                for (Msg& msg : log.done()) {
                    msgs += msg.String(
                        logger::OutputOptions{.include_source = true}, logger::TerminalInfo{});
                }
                log.AddError(nullptr, Range{}, msgs);
            }

            if (ok) {
                std::lock_guard<std::mutex> lock(ast_mutex_);
                ast_map_[key] = runtime_ast;
            }
            return Parsed{std::move(source), std::move(runtime_ast), ok};
        }

    private:
        std::mutex ast_mutex_;
        std::unordered_map<RuntimeCacheKey, javascript::AST, RuntimeCacheKeyHash> ast_map_;
    };

    // Returns the single process-global RuntimeCache instance. A function-local
    // static guarantees it is created once on first use and shared across all
    // builds in this process, which is exactly the sharing the cache exists
    // to provide.
    static RuntimeCache& GlobalRuntimeCache() {
        static RuntimeCache cache;
        return cache;
    }

// ---------------------------------------------------------------------------
// Scanner method definitions. These are the workhorses of the scan phase.
// ---------------------------------------------------------------------------

// Pushes "fn" onto a fresh worker thread. The thread is tracked so that
// JoinThreads can wait for it later. Any exception that escapes "fn" is
// captured here and replayed on the main thread during JoinThreads,
// instead of letting std::terminate kill the whole process from inside
// a worker.
//
// Input:  a callable doing one unit of scan work (usually ParseFile).
// Output: nothing; the thread handle is appended to this->threads.
void Scanner::Spawn(std::function<void()> fn) {
            threads.emplace_back([this, fn = std::move(fn)]() {
                try {
                    fn();
                } catch (...) {
                    std::lock_guard<std::mutex> lock(thrown_exceptions_mu);
                    thrown_exceptions.push_back(std::current_exception());
                }
            });
        }

// Blocks until every worker thread spawned by this Scanner has exited.
// Any exceptions that were captured by those workers are rethrown here
// on the calling (main) thread so a worker failure becomes a normal,
// catchable error instead of aborting the process.
//
// Input:  this->threads with zero or more joinable threads.
// Output: all threads joined and cleared; the first captured exception
//         (if any) is rethrown to the caller.
void Scanner::JoinThreads() {
            for (auto& thread : threads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            threads.clear();
            for (auto& e : thrown_exceptions) {
                if (e) std::rethrow_exception(e);
            }
        }

// Resolves a single resolved import into a parsed file, or returns the
// already-assigned source index if the same file was parsed before. This
// is the chokepoint that guarantees each file is parsed at most once per
// bundle: the canonical path is looked up in the "visited" map, and a hit
// short-circuits to the existing index (notifying the caller through
// the inject channel if one was given).
//
// On a miss the source index is allocated up front, the path is recorded,
// the options are cloned (per-file module-type rules, stdin handling, and
// forced bundling for injected files all applied), and the actual parse
// is handed to a worker via Spawn(). The worker publishes the finished
// ParseResult through the shared result channel.
//
// Input:  a ResolveResult (primary/secondary paths plus per-file metadata
//         like module type and JSX settings), pretty paths for display,
//         the importing source/range (for error messages), and an
//         optional inject channel to signal a file that was already seen.
// Output: the source index of the (possibly already known) file.
uint32_t Scanner::MaybeParseFile(
            const resolver::ResolveResult& resolve_result,
            PrettyPaths pretty_paths,
            const Source* import_source,
            Range import_path_range,
            const compiler::ImportAssertOrWith* import_with,
            InputKind kind,
            InjectChannelPtr inject)
        {
            Path path = resolve_result.path_pair.primary;
            Path visited_key = path;
            if (visited_key.namespace_ == "file") {
                visited_key.text = CanonicalFileSystemPathForWindows(visited_key.text);
            }

            auto visited_iter = visited.find(visited_key);
            if (visited_iter != visited.end()) {
                if (inject != nullptr) {
                    inject->Send(config::InjectedFile{});
                }
                return visited_iter->second.source_index;
            }

            VisitedFile visited_file{
                .source_index = AllocateSourceIndex(visited_key, cache::SourceIndexKind::kNormal),
            };
            visited[visited_key] = visited_file;
            remaining++;
            config::Options options_clone = options;
            if (kind != InputKind::kStdin) {
                options_clone.Stdin = nullptr;
            }

            config::TSConfigJSXApplyTo(resolve_result.tsconfig_jsx, options_clone.JSX);
            if (resolve_result.tsconfig != nullptr) {
                options_clone.TS.Config = *resolve_result.tsconfig;
            }
            if (resolve_result.ts_always_strict != nullptr) {
                options_clone.TSAlwaysStrictData =
                    const_cast<config::TSAlwaysStrict*>(resolve_result.ts_always_strict);
            }

            if (EndsWith(path.text, ".mjs")) {
                options_clone.ModuleTypeData.type = javascript::ModuleType::kESM_MJS;
            } else if (EndsWith(path.text, ".mts")) {
                options_clone.ModuleTypeData.type = javascript::ModuleType::kESM_MTS;
            } else if (EndsWith(path.text, ".cjs")) {
                options_clone.ModuleTypeData.type = javascript::ModuleType::kCommonJS_CJS;
            } else if (EndsWith(path.text, ".cts")) {
                options_clone.ModuleTypeData.type = javascript::ModuleType::kCommonJS_CTS;
            } else if (EndsWith(path.text, ".js") || EndsWith(path.text, ".jsx") ||
                       EndsWith(path.text, ".ts") || EndsWith(path.text, ".tsx")) {
                options_clone.ModuleTypeData = resolve_result.module_type_data;
            } else {
                options_clone.ModuleTypeData.type = javascript::ModuleType::kUnknown;
            }

            bool skip_resolve = false;
            if (inject != nullptr && options_clone.BuildMode != config::Mode::kBundle) {
                options_clone.BuildMode = config::Mode::kBundle;
                skip_resolve = true;
            }

            if (path.namespace_ == "dataurl") {
                if (helpers::ParseDataURL(path.text).has_value()) {
                    std::string pretty_path = path.text;
                    if (pretty_path.size() > 65) {
                        pretty_path = pretty_path.substr(0, 65);
                    }
                    std::string sanitized;
                    for (char c : pretty_path) {
                        if (c == '\n') {
                            sanitized += "\\n";
                        } else {
                            sanitized.push_back(c);
                        }
                    }
                    pretty_path = std::move(sanitized);
                    if (pretty_path.size() > 64) {
                        pretty_path = pretty_path.substr(0, 64) + "...";
                    }
                    pretty_path = "<" + pretty_path + ">";
                    pretty_paths.abs = pretty_path;
                    pretty_paths.rel = pretty_path;
                }
            }

            graph::SideEffects side_effects;
            if (resolve_result.primary_side_effects_data != nullptr) {
                side_effects.kind = graph::SideEffectsKind::kNoSideEffectsPackageJSON;
                side_effects.data = resolve_result.primary_side_effects_data;
            }

            ParseArgs parse_args{
                .fs               = fs,
                .log              = log,
                .res              = res.get(),
                .caches           = caches,
                .pretty_paths     = std::move(pretty_paths),
                .import_source    = import_source,
                .import_source_copy = import_source ? *import_source : Source{},
                .import_with      = import_with,
                .side_effects     = std::move(side_effects),
                .plugin_data      = resolve_result.plugin_data,
                .results          = result_channel,
                .inject           = std::move(inject),
                .unique_key_prefix = unique_key_prefix,
                .key_path         = std::move(path),
                .options          = std::move(options_clone),
                .import_path_range = import_path_range,
                .source_index     = visited_file.source_index,
                .skip_resolve     = skip_resolve,
            };
            Spawn([args = std::move(parse_args)]() mutable {
                try {
                    ParseFile(std::move(args));
                } catch (...) {
                    args.results->Send(ParseResult{});
                }
            });

            return visited_file.source_index;
        }

// Allocates a new source index for a canonical path using the shared source
// index cache, and grows the results array so the returned index is always
// within bounds. Reusing the source-index cache keeps the same virtual
// paths on the same indices across separate builds, which also lets
// parsed results survive across builds.
//
// Input:  the canonical path plus which kind of virtual file it is.
// Output: a fresh uint32_t source index that is valid in this->results.
uint32_t Scanner::AllocateSourceIndex(const Path& path, cache::SourceIndexKind kind) {
            uint32_t source_index = caches->source_index_cache.Get(path, kind);

            size_t new_len = static_cast<size_t>(source_index) + 1;
            if (results.size() < new_len) {
                results.resize(new_len);
            }

            return source_index;
        }

// Same as AllocateSourceIndex but for glob imports: the source index is
// derived from the parent module index plus the position of the glob
// pattern within it, so every file matched by a glob gets a stable,
// unique index.
//
// Input:  the source index of the file containing the glob pattern and
//         the index of the import record that produced the glob match.
// Output: a fresh uint32_t source index valid in this->results.
uint32_t Scanner::AllocateGlobSourceIndex(uint32_t parent_source_index, uint32_t glob_index) {
            uint32_t source_index =
                caches->source_index_cache.GetGlob(parent_source_index, glob_index);

            size_t new_len = static_cast<size_t>(source_index) + 1;
            if (results.size() < new_len) {
                results.resize(new_len);
            }

            return source_index;
        }

// Prepares injected files before entry points are parsed. Injected files are
// modules whose contents are prepended to every output chunk or exposed
// as globals, so they must be parsed and registered in the graph up
// front. Two kinds are handled:
//   - virtual "--define" files, built inline here from on-the-fly ASTs
//     (never routed through resolver plugins),
//   - user "--inject" paths, resolved through the normal plugin pipeline
//     in parallel, then parsed via MaybeParseFile while a separate wait
//     group collects their contents from inject channels.
//
// Input:  options.InjectedDefines and options.InjectPaths.
// Output: options.InjectedFiles populated with every injected module, and
//         each file registered in this->results.
void Scanner::PreprocessInjectedFiles() {
            TimerScope scope(timer, "Preprocess injected files");

            std::vector<config::InjectedFile> injected_files;
            injected_files.reserve(options.InjectedDefines.size() + options.InjectPaths.size());

            for (const config::InjectedDefine& define : options.InjectedDefines) {
                Path visited_key{.text = "<define:" + define.Name + ">"};
                uint32_t source_index =
                    AllocateSourceIndex(visited_key, cache::SourceIndexKind::kNormal);
                visited[visited_key] = VisitedFile{.source_index = source_index};
                Source source{
                    .pretty_paths    = resolver::MakePrettyPaths(*fs, visited_key),
                    .identifier_name = javascript::EnsureValidIdentifier(visited_key.text),
                    .contents        = define.SourceData.contents,
                    .key_path        = visited_key,
                    .index           = source_index,
                };

                config::InjectedFile injected_file;
                injected_file.SourceData = source;
                injected_file.DefineName = define.Name;
                injected_files.push_back(std::move(injected_file));

                javascript::AST ast = javascript::LazyExportAST(log, source,
                    javascript::OptionsFromConfig(&options),
                    javascript::Expr( define.Data, {}), nullptr);
                ParseResult result;
                result.ok = true;
                result.file.input_file.source   = std::move(source);
                result.file.input_file.repr     = graph::InputFileRepr{
                    std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                result.file.input_file.loader   = config::Loader::kJSON;
                result.file.input_file.side_effects.kind =
                    graph::SideEffectsKind::kNoSideEffectsPureData;

                remaining++;
                ResultChannelPtr result_channel_copy = result_channel;
                Spawn([result = std::move(result), result_channel_copy]() mutable {
                    result_channel_copy->Send(std::move(result));
                });
            }

            std::vector<OnResolveOutcome> inject_resolve_results(options.InjectPaths.size());
            std::string inject_abs_resolve_dir = fs->Cwd();
            WaitGroup inject_resolve_wait_group;
            inject_resolve_wait_group.Add(static_cast<int>(options.InjectPaths.size()));
            for (size_t i = 0; i < options.InjectPaths.size(); i++) {
                const std::string import_path_original = options.InjectPaths[i];
                OnResolveOutcome* slot = &inject_resolve_results[i];
                Spawn([this, slot, import_path_original, inject_abs_resolve_dir,
                          &inject_resolve_wait_group]() {
                    Path importer;
                    std::string import_path = import_path_original;

                    std::string abs_path = import_path;
                    if (!fs->IsAbs(abs_path)) {
                        abs_path = fs->Join({inject_abs_resolve_dir, abs_path});
                    }
                    std::string dir  = fs->Dir(abs_path);
                    std::string base = fs->Base(abs_path);
                    filesystem::FsResult<filesystem::DirEntries> entries =
                        fs->ReadDirectory(dir);
                    if (entries.Ok()) {
                        auto [entry, diff_case] = entries.value.Get(base);
                        if (entry != nullptr &&
                            entry->Kind(*fs) == filesystem::EntryKind::kFile)
                        {
                            importer.namespace_ = "file";
                            if (!fs->IsAbs(import_path) && resolver::IsPackagePath(import_path)) {
                                import_path = "./" + import_path;
                            }
                        }
                    } else if (log.level >= logger::LogLevel::kDebug &&
                               !entries.original_error.empty())
                    {
                        log.AddID(logger::MsgID::kNone, logger::MsgKind::kDebug, nullptr, Range{},
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_FailedToReadDirectory, abs_path,
                                entries.original_error));
                    }

                    OnResolveOutcome outcome = RunOnResolvePlugins(
                        options.Plugins,
                        res.get(),
                        log,
                        *fs,
                        caches->fs_cache,
                        nullptr,
                        Range{},
                        importer,
                        import_path,
                        logger::ImportAttributes{},
                        compiler::ImportKind::kEntryPoint,
                        inject_abs_resolve_dir,
                        std::any{},
                        options.LogPathStyle);
                    if (outcome.resolve_result.has_value()) {
                        if (outcome.resolve_result->path_pair.is_external) {
                            log.AddError(nullptr, Range{},
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_InjectedPathCannotBeExternal, import_path));
                        } else {
                            *slot = std::move(outcome);
                        }
                    } else if (!outcome.did_log_error) {
                        outcome.debug_meta.LogErrorMsg(log, nullptr, Range{},
                            "Could not resolve \"" + import_path + "\"", "", {});
                    }
                    inject_resolve_wait_group.Done();
                });
            }
            inject_resolve_wait_group.Wait();

            if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
                return;
            }

            std::vector<config::InjectedFile> injected_results(options.InjectPaths.size());
            size_t j = 0;
            WaitGroup inject_wait_group;
            for (OnResolveOutcome& outcome : inject_resolve_results) {
                if (outcome.resolve_result.has_value()) {
                    auto inject_channel = std::make_shared<Channel<config::InjectedFile>>();
                    MaybeParseFile(*outcome.resolve_result,
                        resolver::MakePrettyPaths(*fs, outcome.resolve_result->path_pair.primary),
                        nullptr, Range{}, nullptr, InputKind::kNormal, inject_channel);
                    config::InjectedFile* result_slot = &injected_results[j];
                    inject_wait_group.Add(1);

                    Spawn([result_slot, inject_channel, &inject_wait_group]() {
                        *result_slot = inject_channel->Recv();
                        inject_wait_group.Done();
                    });
                    j++;
                }
            }
            inject_wait_group.Wait();
            injected_files.insert(injected_files.end(), injected_results.begin(),
                injected_results.begin() + static_cast<long>(j));

            options.InjectedFiles = std::move(injected_files);
        }

// Registers every entry point from the configuration (plus an implicit
// "<stdin>" entry when stdin input is configured) and produces the
// metadata the linker later needs: for each entry, a source index and an
// output path. Real header checks run here too (e.g. whether a relative
// path names a real file, which decides if a "./" prefix must be
// inserted so it is treated as relative rather than as a package path).
// Glob patterns are expanded into one entry point per match, and output
// paths are auto-generated from the input paths when the user did not
// provide one. All resolution runs in parallel, then the entry points are
// parsed once "outbase" is known so auto-generated output paths can be
// expressed relative to it.
//
// Input:  a list of {input path, optional output path} from the API layer.
// Output: a parallel list of graph::EntryPoint with source index and
//         (absolute-then-relative) output path filled in; each entry
//         module is now present in this->results.
std::vector<graph::EntryPoint> Scanner::AddEntryPoints(
            const std::vector<config::EntryPoint>& entry_points)
        {
            TimerScope scope(timer, "Add entry points");

            std::vector<graph::EntryPoint> entry_metas;
            entry_metas.reserve(entry_points.size() + 1);

            if (options.Stdin != nullptr) {
                config::StdinInfo* stdin_info = options.Stdin;
                Path stdin_path{.text = "<stdin>"};
                if (!stdin_info->SourceFile.empty()) {
                    if (stdin_info->AbsResolveDir.empty()) {
                        stdin_path = Path{.text = stdin_info->SourceFile};
                    } else if (fs->IsAbs(stdin_info->SourceFile)) {
                        stdin_path = Path{
                            .text       = stdin_info->SourceFile,
                            .namespace_ = "file",
                        };
                    } else {
                        stdin_path = Path{
                            .text = fs->Join({stdin_info->AbsResolveDir, stdin_info->SourceFile}),
                            .namespace_ = "file",
                        };
                    }
                }
                resolver::ResolveResult resolve_result;
                resolve_result.path_pair.primary = stdin_path;
                uint32_t source_index = MaybeParseFile(resolve_result,
                    resolver::MakePrettyPaths(*fs, stdin_path), nullptr, Range{}, nullptr,
                    InputKind::kStdin, nullptr);
                graph::EntryPoint meta;
                meta.output_path  = "stdin";
                meta.source_index = source_index;
                entry_metas.push_back(std::move(meta));
            }

            if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
                return {};
            }

            std::vector<config::EntryPoint> local_entry_points = entry_points;
            std::string entry_point_abs_resolve_dir = fs->Cwd();
            for (config::EntryPoint& entry_point : local_entry_points) {
                std::string abs_path = entry_point.InputPath;
                if (abs_path.find('*') != std::string::npos) {
                    continue;
                }
                if (!fs->IsAbs(abs_path)) {
                    abs_path = fs->Join({entry_point_abs_resolve_dir, abs_path});
                }
                std::string dir  = fs->Dir(abs_path);
                std::string base = fs->Base(abs_path);
                filesystem::FsResult<filesystem::DirEntries> entries =
                    fs->ReadDirectory(dir);
                if (entries.Ok()) {
                    auto [entry, diff_case] = entries.value.Get(base);
                    if (entry != nullptr &&
                        entry->Kind(*fs) == filesystem::EntryKind::kFile)
                    {
                        entry_point.InputPathInFileNamespace = true;

                        if (!fs->IsAbs(entry_point.InputPath) &&
                            resolver::IsPackagePath(entry_point.InputPath))
                        {
                            entry_point.InputPath = "./" + entry_point.InputPath;
                        }
                    }
                } else if (log.level >= logger::LogLevel::kDebug &&
                           !entries.original_error.empty())
                {
                    log.AddID(logger::MsgID::kNone, logger::MsgKind::kDebug, nullptr, Range{},
                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_FailedToReadDirectory, abs_path,
                            entries.original_error));
                }
            }

            if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
                return {};
            }

            struct EntryPointInfo {
                std::vector<resolver::ResolveResult> results;
                bool is_glob{};
            };
            std::vector<EntryPointInfo> entry_point_infos(local_entry_points.size());
            WaitGroup entry_point_wait_group;
            entry_point_wait_group.Add(static_cast<int>(local_entry_points.size()));
            for (size_t i = 0; i < local_entry_points.size(); i++) {
                const config::EntryPoint entry_point = local_entry_points[i];
                EntryPointInfo* info_slot = &entry_point_infos[i];
                Spawn([this, info_slot, entry_point, entry_point_abs_resolve_dir,
                          &entry_point_wait_group]() {
                    Path importer;
                    if (entry_point.InputPathInFileNamespace) {
                        importer.namespace_ = "file";
                    }

                    if (entry_point.InputPath.find('*') != std::string::npos) {
                        std::vector<helpers::GlobPart> pattern =
                            helpers::ParseGlobPattern(entry_point.InputPath);
                        if (pattern.size() > 1) {
                            std::string pretty_pattern =
                                "\"" + entry_point.InputPath + "\"";
                            logger::Msg msg;
                            std::optional<std::map<std::string, resolver::ResolveResult>>
                                glob_results = res->ResolveGlob(entry_point_abs_resolve_dir,
                                    pattern, compiler::ImportKind::kEntryPoint, pretty_pattern,
                                    &msg);
                            if (glob_results.has_value()) {
                                std::vector<std::string> keys;
                                keys.reserve(glob_results->size());
                                for (const auto& [key, value] : *glob_results) {
                                    keys.push_back(key);
                                }
                                std::sort(keys.begin(), keys.end());
                                EntryPointInfo info;
                                info.is_glob = true;
                                for (const std::string& key : keys) {
                                    info.results.push_back((*glob_results)[key]);
                                }
                                *info_slot = std::move(info);
                                if (msg.id != logger::MsgID::kNone) {
                                    log.AddID(msg.id, msg.kind, nullptr, Range{}, msg.data.text);
                                }
                            } else {
                                log.AddError(nullptr, Range{},
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CouldNotResolveEntryPoint, entry_point.InputPath));
                            }
                            entry_point_wait_group.Done();
                            return;
                        }
                    }

                    OnResolveOutcome outcome = RunOnResolvePlugins(
                        options.Plugins,
                        res.get(),
                        log,
                        *fs,
                        caches->fs_cache,
                        nullptr,
                        Range{},
                        importer,
                        entry_point.InputPath,
                        logger::ImportAttributes{},
                        compiler::ImportKind::kEntryPoint,
                        entry_point_abs_resolve_dir,
                        std::any{},
                        options.LogPathStyle);
                    if (outcome.resolve_result.has_value()) {
                        if (outcome.resolve_result->path_pair.is_external) {
                            log.AddError(nullptr, Range{},
                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_EntryPointCannotBeExternal, entry_point.InputPath));
                        } else {
                            EntryPointInfo info;
                            info.results.push_back(std::move(*outcome.resolve_result));
                            *info_slot = std::move(info);
                        }
                    } else if (!outcome.did_log_error) {
                        std::vector<MsgData> notes;
                        if (!fs->IsAbs(entry_point.InputPath)) {
                            resolver::DebugMeta throwaway_debug;
                            std::optional<resolver::ResolveResult> query =
                                res->ProbeResolvePackageAsRelative(entry_point_abs_resolve_dir,
                                    entry_point.InputPath, compiler::ImportKind::kEntryPoint,
                                    &throwaway_debug);
                            if (query.has_value()) {
                                PrettyPaths pretty_paths = resolver::MakePrettyPaths(*fs,
                                    query->path_pair.primary);
                                MsgData note;
                                note.text = "Use the relative path \"./" + entry_point.InputPath +
                                            "\" to reference the file \"" +
                                            pretty_paths.Select(options.LogPathStyle) +
                                            "\". Without the leading \"./\", the path \"" +
                                            entry_point.InputPath +
                                            "\" is being interpreted as a package path instead.";
                                notes.push_back(std::move(note));
                            }
                        }
                        outcome.debug_meta.LogErrorMsg(log, nullptr, Range{},
                            "Could not resolve \"" + entry_point.InputPath + "\"", "",
                            std::move(notes));
                    }
                    entry_point_wait_group.Done();
                });
            }
            entry_point_wait_group.Wait();

            if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
                return {};
            }

            struct EntryPointToParse {
                size_t index{};
                std::function<uint32_t()> parse;
            };
            std::vector<EntryPointToParse> entry_points_to_parse;
            for (size_t i = 0; i < entry_point_infos.size(); i++) {
                const EntryPointInfo& info = entry_point_infos[i];
                if (info.results.empty()) {
                    continue;
                }

                for (const resolver::ResolveResult& resolve_result : info.results) {
                    PrettyPaths pretty_paths =
                        resolver::MakePrettyPaths(*fs, resolve_result.path_pair.primary);
                    std::string output_path = local_entry_points[i].OutputPath;
                    bool output_path_was_auto_generated = false;

                    if (output_path.empty()) {
                        if (info.is_glob) {
                            output_path = pretty_paths.rel;
                        } else {
                            output_path = local_entry_points[i].InputPath;
                        }
                        std::string windows_volume_label;

                        if (fs->IsAbs(output_path) && output_path.size() >= 3 &&
                            output_path[1] == ':')
                        {
                            char drive = output_path[0];
                            if ((drive >= 'a' && drive <= 'z') || (drive >= 'A' && drive <= 'Z'))
                            {
                                char sep = output_path[2];
                                if (sep == '/' || sep == '\\') {
                                    windows_volume_label = output_path.substr(0, 3);
                                    output_path          = output_path.substr(3);
                                }
                            }
                        }

                        output_path = SanitizeFilePathForVirtualModulePath(output_path);
                        if (!windows_volume_label.empty()) {
                            output_path = windows_volume_label + output_path;
                        }
                        output_path_was_auto_generated = true;
                    }

                    resolver::ResolveResult captured_result = resolve_result;
                    PrettyPaths captured_paths              = pretty_paths;
                    entry_points_to_parse.push_back(EntryPointToParse{
                        .index = entry_metas.size(),
                        .parse = [this, captured_result, captured_paths]() -> uint32_t {
                            return MaybeParseFile(captured_result, captured_paths, nullptr,
                                Range{}, nullptr, InputKind::kEntryPoint, nullptr);
                        },
                    });

                    graph::EntryPoint meta;
                    meta.output_path                   = output_path;
                    meta.output_path_was_auto_generated = output_path_was_auto_generated;
                    entry_metas.push_back(std::move(meta));
                }
            }

            for (graph::EntryPoint& entry_point : entry_metas) {
                if (entry_point.output_path_was_auto_generated &&
                    !fs->IsAbs(entry_point.output_path))
                {
                    entry_point.output_path = fs->Join(
                        {entry_point_abs_resolve_dir, entry_point.output_path});
                }
            }

            if (options.AbsOutputBase.empty()) {
                options.AbsOutputBase = LowestCommonAncestorDirectory(*fs, entry_metas);
                if (options.AbsOutputBase.empty()) {
                    options.AbsOutputBase = entry_point_abs_resolve_dir;
                }
            }

            for (EntryPointToParse& to_parse : entry_points_to_parse) {
                entry_metas[to_parse.index].source_index = to_parse.parse();
            }

            for (graph::EntryPoint& entry_point : entry_metas) {
                if (fs->IsAbs(entry_point.output_path)) {
                    if (!entry_point.output_path_was_auto_generated) {
                        std::optional<std::string> rel_path =
                            fs->Rel(options.AbsOutputDir, entry_point.output_path);
                        if (rel_path.has_value()) {
                            entry_point.output_path = *rel_path;
                        }
                    } else {
                        std::optional<std::string> rel_path =
                            fs->Rel(options.AbsOutputBase, entry_point.output_path);
                        if (rel_path.has_value()) {
                            entry_point.output_path = *rel_path;
                        }

                        size_t last = entry_point.output_path.find_last_of("/.\\");
                        if (last != std::string::npos &&
                            entry_point.output_path[last] == '.')
                        {
                            entry_point.output_path = entry_point.output_path.substr(0, last);
                        }
                    }
                }
            }

            return entry_metas;
        }

// After the whole module graph has been scanned, promotes every resource
// referenced by an HTML entry point (via its import records) into a
// bundling sub-entry point. This lets CSS files, JS files, and other
// resources referenced from HTML receive their own output paths, copying
// behavior, and chunking exactly like a user-declared entry point.
//
// Non-stylesheet <link> elements (favicon, preload, ...) are preserved
// as-is and never become sub-entries, and duplicate references to the
// same file are coalesced through a "seen" set so nothing is added twice.
//
// Input:  the entry point metadata collected so far.
// Output: the same list modified in place, with one extra EntryPoint per
//         new referenced resource.
void Scanner::ExpandHtmlEntryPoints(std::vector<graph::EntryPoint>& entry_points) {
            TimerScope scope(timer, "Expand HTML entry points");

            auto html_repr_of = [&](uint32_t source_index) -> graph::HTMLRepr* {
                if (source_index >= results.size()) {
                    return nullptr;
                }
                ParseResult& result = results[source_index];
                if (!result.ok) {
                    return nullptr;
                }
                auto* ptr = std::get_if<std::shared_ptr<graph::HTMLRepr>>(
                    &result.file.input_file.repr);
                return (ptr != nullptr && *ptr != nullptr) ? ptr->get() : nullptr;
            };

            std::unordered_set<uint32_t> seen;
            for (const graph::EntryPoint& ep : entry_points) {
                seen.insert(ep.source_index);
            }

            const std::vector<graph::EntryPoint> original_entries = entry_points;
            for (const graph::EntryPoint& html_entry : original_entries) {
                graph::HTMLRepr* html_repr = html_repr_of(html_entry.source_index);
                if (html_repr == nullptr) {
                    continue;
                }

                for (size_t record_index = 0;
                     record_index < html_repr->ImportRecords().size();
                     record_index++)
                {
                    const compiler::ImportRecord& record =
                        html_repr->ImportRecords()[record_index];
                    if (record_index < html_repr->ast.record_origins.size()) {
                        const html::ImportRecordOrigin& origin =
                            html_repr->ast.record_origins[record_index];
                        if (origin.element != nullptr &&
                            origin.element->tag_name == "link" &&
                            !html::IsStylesheetLink(*origin.element))
                        {
                            continue;
                        }
                    }
                    if (!record.source_index.IsValid()) {
                        continue;
                    }
                    uint32_t target_index = record.source_index.GetIndex();
                    if (seen.count(target_index) != 0) {
                        continue;
                    }
                    seen.insert(target_index);
                    if (target_index >= results.size() || !results[target_index].ok) {
                        continue;
                    }

                    const graph::InputFile& target = results[target_index].file.input_file;

                    if (target.source.key_path.namespace_ != "file") {
                        continue;
                    }
                    std::optional<std::string> rel =
                        fs->Rel(options.AbsOutputBase, target.source.key_path.text);
                    if (!rel.has_value()) {
                        continue;
                    }

                    graph::EntryPoint sub_entry;
                    sub_entry.source_index                = target_index;
                    sub_entry.output_path                 = *rel;
                    sub_entry.output_path_was_auto_generated = true;

                    size_t last = sub_entry.output_path.find_last_of("/.\\");
                    if (last != std::string::npos &&
                        sub_entry.output_path[last] == '.')
                    {
                        sub_entry.output_path = sub_entry.output_path.substr(0, last);
                    }

                    entry_points.push_back(std::move(sub_entry));
                }
            }
        }

// Appends the source text of an element's direct text children to "output".
// Used when harvesting the body of an inline <script> or <style> element,
// whose text contents live as child text nodes in the HTML tree.
//
// Input:  the element node and the buffer to append into.
// Output: "output" extended with the element's text content.
void Scanner::AppendInlineElementText(const html::Node& element, std::string& output) {
            for (const auto& child : element.child_nodes) {
                if (child->type == html::NodeType::kText) {
                    output += child->value;
                }
            }
        }

// Turns the inline <script>/<style> contents of an HTML entry point into
// real bundled modules. Consecutive classic inline scripts are merged
// into one virtual module (a "run"), broken apart by any external
// <script src>, by inline <script type="module">, or by an external
// <link rel="stylesheet">; separate JS and CSS runs are collected. A
// "guchho-ignore" element never participates in a run. Each non-empty
// run is parsed as its own virtual stdin-style chunk (so it gets real
// bundling, code splitting, and source generation) and recorded as an
// HtmlInlineSegment so the HTML output pass can re-split the saved file
// into its chunk pieces later.
//
// Input:  the already-scanned ParseResult of one HTML file, its HTML
//         representation, and the entry point list to extend.
// Output: graph::HtmlInlineInfo stored in this->html_inline under the
//         HTML file's source index (JS and CSS segment vectors), plus one
//         new entry point per inline run.
void Scanner::ExpandHtmlInlineContent(const ParseResult& result, graph::HTMLRepr& repr,
                                     std::vector<graph::EntryPoint>& entry_points) {
            const graph::InputFile& html_file = result.file.input_file;
            const std::string& html_path = html_file.source.key_path.text;

            auto inline_output_path = [&](const char* suffix) {
                std::optional<std::string> rel =
                    fs->Rel(options.AbsOutputBase, html_path);
                std::string rel_path = rel.value_or(fs->Base(html_path));
                size_t ext_start = rel_path.rfind('.');
                if (ext_start != std::string::npos && rel_path.find('/', ext_start + 1) ==
                    std::string::npos)
                {
                    rel_path = rel_path.substr(0, ext_start);
                }
                return rel_path + suffix;
            };

            auto generate = [&](std::string contents, config::Loader loader,
                                const char* suffix) -> uint32_t {
                if (contents.empty()) {
                    return UINT32_MAX;
                }

                auto stdin_info = std::make_unique<config::StdinInfo>();
                stdin_info->Contents = std::move(contents);
                stdin_info->Ldr = loader;
                stdin_info->AbsResolveDir = fs->Dir(html_path);
                config::StdinInfo* stdin_ptr = stdin_info.get();
                stdin_info_holder.push_back(std::move(stdin_info));

                Path key_path{
                    .text       = html_path + suffix,
                    .namespace_ = "file",
                };
                resolver::ResolveResult resolve_result;
                resolve_result.path_pair.primary = key_path;

                options.Stdin = stdin_ptr;
                uint32_t source_index = MaybeParseFile(
                    resolve_result, resolver::MakePrettyPaths(*fs, key_path),
                    &html_file.source, Range{}, nullptr, InputKind::kStdin, nullptr);
                options.Stdin = nullptr;

                graph::EntryPoint inline_entry;
                inline_entry.source_index = source_index;
                inline_entry.output_path = inline_output_path(suffix);
                inline_entry.output_path_was_auto_generated = true;
                entry_points.push_back(std::move(inline_entry));
                return source_index;
            };

            const html::Node* root = repr.ast.node.get();

            graph::HtmlInlineInfo info;

            std::vector<std::vector<const html::Node*>> script_runs;
            script_runs.emplace_back();
            std::function<void(const html::Node&)> visit_scripts = [&](const html::Node& node) {
                if (IsElementNode(node) && node.tag_name == "script") {
                    for (const html::Attribute& attr : node.attrs) {
                        if (attr.name == "guchho-ignore") {
                            return;
                        }
                    }
                    const std::string* src = nullptr;
                    bool is_module = false;
                    bool is_import_map = false;
                    for (const html::Attribute& attr : node.attrs) {
                        if (attr.name == "src") {
                            src = &attr.value;
                        } else if (attr.name == "type" && attr.value == "module") {
                            is_module = true;
                        } else if (attr.name == "type" && attr.value == "importmap") {
                            is_import_map = true;
                        }
                    }
                    if (src != nullptr || is_module || is_import_map) {
                        script_runs.emplace_back();
                    } else {
                        script_runs.back().push_back(&node);
                    }
                    return;
                }
                for (const auto& child : GetChildNodes(node)) {
                    visit_scripts(*child);
                }
            };
            visit_scripts(*root);
            size_t script_run_index = 0;
            for (auto& run : script_runs) {
                if (run.empty()) {
                    continue;
                }
                std::string contents;
                for (const html::Node* script : run) {
                    if (!contents.empty()) {
                        contents += '\n';
                    }
                    AppendInlineElementText(*script, contents);
                }
                std::string suffix = ":inline-scripts";
                if (script_run_index > 0) {
                    suffix += ":" + std::to_string(script_run_index);
                }
                uint32_t chunk = generate(std::move(contents), config::Loader::kJS,
                                          suffix.c_str());
                info.js_segments.push_back(graph::HtmlInlineSegment{
                    .members = std::move(run),
                    .chunk_source_index = chunk,
                });
                script_run_index++;
            }

            std::vector<std::vector<const html::Node*>> style_runs;
            style_runs.emplace_back();
            std::function<void(const html::Node&)> visit_styles = [&](const html::Node& node) {
                if (IsElementNode(node) && node.tag_name == "style") {
                    for (const html::Attribute& attr : node.attrs) {
                        if (attr.name == "guchho-ignore") {
                            return;
                        }
                    }
                    style_runs.back().push_back(&node);
                    return;
                }
                if (IsElementNode(node) && node.tag_name == "link" &&
                    html::IsStylesheetLink(node))
                {
                    style_runs.emplace_back();
                    return;
                }
                for (const auto& child : GetChildNodes(node)) {
                    visit_styles(*child);
                }
            };
            visit_styles(*root);
            size_t style_run_index = 0;
            for (auto& run : style_runs) {
                if (run.empty()) {
                    continue;
                }
                std::string contents;
                for (const html::Node* style : run) {
                    if (!contents.empty()) {
                        contents += '\n';
                    }
                    AppendInlineElementText(*style, contents);
                }
                std::string suffix = ":inline-styles";
                if (style_run_index > 0) {
                    suffix += ":" + std::to_string(style_run_index);
                }
                uint32_t chunk = generate(std::move(contents), config::Loader::kCSS,
                                          suffix.c_str());
                info.css_segments.push_back(graph::HtmlInlineSegment{
                    .members = std::move(run),
                    .chunk_source_index = chunk,
                });
                style_run_index++;
            }

            html_inline[html_file.source.index] = std::move(info);
        }

// The main scan loop. Keeps pulling parsed files off the result channel
// until the outstanding-work counter "remaining" reaches zero, resolving
// every import record each file declares and scheduling any newly-found
// dependencies. This is where the whole reachable module graph gets
// discovered:
//   - import records generated for the runtime are re-pointed at the
//     real runtime source index,
//   - glob imports get a synthesized result per matched file,
//   - in-bundle paths are parsed via MaybeParseFile (deduplicated by the
//     visited map), and external paths are rewritten relative to the
//     output directory,
//   - HTML entry points have their inline script/style content expanded
//     into virtual modules.
//
// Input:  the entry point metadata (to find HTML entry points) and a
//         result channel seeded by the entry-point and runtime parses.
// Output: this->results fully populated for every reachable module.
void Scanner::ScanAllDependencies(std::vector<graph::EntryPoint>& entry_points) {
            TimerScope scope(timer, "Scan all dependencies");

            std::unordered_set<uint32_t> entry_indices;
            entry_indices.reserve(entry_points.size());
            for (const graph::EntryPoint& ep : entry_points) {
                entry_indices.insert(ep.source_index);
            }

            while (remaining > 0) {
                if (options.CancelFlagData != nullptr &&
                    options.CancelFlagData->DidCancel())
                {
                    return;
                }

                ParseResult result = result_channel->Recv();
                remaining--;
                if (!result.ok) {
                    continue;
                }

                std::vector<compiler::ImportRecord>* records = nullptr;
                if (options.BuildMode == config::Mode::kBundle) {
                    if (auto* js_repr = TryJSRepr(result.file.input_file.repr);
                        js_repr != nullptr)
                    {
                        records = &js_repr->ImportRecords();
                    } else if (auto* ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(
                                   &result.file.input_file.repr);
                               ptr != nullptr && *ptr != nullptr)
                    {
                        records = &(*ptr)->ImportRecords();
                    } else if (auto* html_repr_ptr = std::get_if<std::shared_ptr<graph::HTMLRepr>>(
                                   &result.file.input_file.repr);
                               html_repr_ptr != nullptr && *html_repr_ptr != nullptr)
                    {
                        records = &(*html_repr_ptr)->ImportRecords();

                        if (entry_indices.count(result.file.input_file.source.index) != 0) {
                            ExpandHtmlInlineContent(result, *html_repr_ptr->get(), entry_points);
                        }
                    }
                }

                if (records != nullptr) {
                    for (uint32_t import_record_index = 0;
                         import_record_index < records->size();
                         import_record_index++)
                    {
                        compiler::ImportRecord& record =
                            (*records)[import_record_index];

                        if (record.source_index.IsValid() &&
                            record.source_index.GetIndex() ==
                                javascript::kRuntimeSourceIndex)
                        {
                            record.source_index =
                                compiler::Index32::Make(javascript::kSourceIndex);
                            continue;
                        }

                        const compiler::ImportAssertOrWith* with = nullptr;
                        if (record.assert_or_with != nullptr &&
                            record.assert_or_with->keyword ==
                                compiler::AssertOrWithKeyword::kWith)
                        {
                            with = record.assert_or_with.get();
                        }

                        std::shared_ptr<resolver::ResolveResult> resolve_result;
                        if (import_record_index < result.resolve_results.size()) {
                            resolve_result = result.resolve_results[import_record_index];
                        }
                        if (resolve_result == nullptr) {
                            auto glob_iter = result.glob_resolve_results.find(
                                import_record_index);
                            if (glob_iter != result.glob_resolve_results.end())
                            {
                                uint32_t source_index = AllocateGlobSourceIndex(
                                    result.file.input_file.source.index,
                                    import_record_index);
                                record.source_index =
                                    compiler::Index32::Make(source_index);
                                results[source_index] = GenerateResultForGlobResolve(
                                    source_index, glob_iter->second.abs_path,
                                    result.file.input_file.source, record.range,
                                    with, record.glob_pattern->kind, record.phase,
                                    glob_iter->second, record.assert_or_with.get());
                            }
                            continue;
                        }

                        logger::Path path = resolve_result->path_pair.primary;
                        if (!resolve_result->path_pair.is_external) {
                            uint32_t source_index = MaybeParseFile(*resolve_result,
                                resolver::MakePrettyPaths(*fs, path),
                                &result.file.input_file.source, record.range, with,
                                InputKind::kNormal, nullptr);
                            record.source_index =
                                compiler::Index32::Make(source_index);
                        } else {
                            if (resolve_result->primary_side_effects_data != nullptr)
                            {
                                record.flags = record.flags |
                                    compiler::ImportRecordFlags::
                                        kIsExternalWithoutSideEffects;
                            }

                            if (path.namespace_ == "file") {
                                std::optional<std::string> rel_path =
                                    fs->Rel(options.AbsOutputDir, path.text);
                                if (rel_path.has_value()) {
                                    std::string rel;
                                    rel.reserve(rel_path->size());
                                    for (char c : *rel_path) {
                                        rel.push_back(c == '\\' ? '/' : c);
                                    }
                                    if (resolver::IsPackagePath(rel)) {
                                        rel = "./" + rel;
                                    }
                                    record.path.text = std::move(rel);
                                } else {
                                    record.path = path;
                                }
                            } else {
                                record.path = path;
                            }
                        }
                    }
                }

                uint32_t source_index = result.file.input_file.source.index;
                results[source_index] = std::move(result);
            }
        }

// Builds a synthetic ParseResult describing the exports of a glob import
// (e.g. `import * as mods from "./dir/*.js"`). The resolver already
// expanded the glob into one ResolveResult per matched file; this turns
// those into a JS object whose properties are lazy arrow functions that
// (re)export the matched module via separate import records. External
// matches contribute their resolved path as the property value.
//
// Input:  a freshly allocated source index, the pretty path to display,
//         the importing file's source/range (for error messages), the
//         import kind/phase and attributes, and the resolver's per-file
//         glob results.
// Output: an ok=true ParseResult whose repr carries the synthesized
//         exports AST plus one import record per match; each matched file
//         has itself been parsed through MaybeParseFile already.
ParseResult Scanner::GenerateResultForGlobResolve(
            uint32_t source_index,
            const std::string& fake_source_path,
            const Source& import_source,
            Range import_range,
            const compiler::ImportAssertOrWith* import_with,
            compiler::ImportKind kind,
            compiler::ImportPhase phase,
            const GlobResolveResult& glob_result,
            const compiler::ImportAssertOrWith* assertions)
{
                    std::shared_ptr<javascript::EObject> object =
                        std::make_shared<javascript::EObject>();
                    object->properties.reserve(glob_result.resolve_results.size());
            std::vector<compiler::ImportRecord> import_records;
            import_records.reserve(glob_result.resolve_results.size());
            std::vector<std::shared_ptr<resolver::ResolveResult>> resolve_results;
            resolve_results.reserve(glob_result.resolve_results.size());

            for (const auto& [key, key_resolve_result] :
                glob_result.resolve_results)
            {
                javascript::Expr value;

                uint32_t import_record_index =
                    static_cast<uint32_t>(import_records.size());
                compiler::Index32 value_source_index;

                if (!key_resolve_result.path_pair.is_external) {
                    value_source_index = compiler::Index32::Make(MaybeParseFile(
                        key_resolve_result,
                        resolver::MakePrettyPaths(*fs,
                            key_resolve_result.path_pair.primary),
                        &import_source, import_range, import_with,
                        InputKind::kNormal, nullptr));
                }

                logger::Path path = key_resolve_result.path_pair.primary;

                if (path.namespace_ == "file") {
                    std::optional<std::string> rel_path =
                        fs->Rel(options.AbsOutputDir, path.text);
                    if (rel_path.has_value()) {
                        std::string rel;
                        rel.reserve(rel_path->size());
                        for (char c : *rel_path) {
                            rel.push_back(c == '\\' ? '/' : c);
                        }
                        if (resolver::IsPackagePath(rel)) {
                            rel = "./" + rel;
                        }
                        path.text = std::move(rel);
                    }
                }

                resolve_results.push_back(
                    std::make_shared<resolver::ResolveResult>(key_resolve_result));
                compiler::ImportRecord record;
                record.path          = path;
                record.source_index  = value_source_index;
                record.kind          = kind;
                record.phase         = phase;
                if (assertions != nullptr) {
                    record.assert_or_with =
                        std::make_shared<compiler::ImportAssertOrWith>(*assertions);
                }
                import_records.push_back(std::move(record));

                switch (kind) {
                    case compiler::ImportKind::kDynamic: {
                        auto data = std::make_shared<javascript::EImportString>();
                        data->import_record_index = import_record_index;
                        value.data = std::move(data);
                        break;
                    }
                    case compiler::ImportKind::kRequire: {
                        auto data = std::make_shared<javascript::ERequireString>();
                        data->import_record_index = import_record_index;
                        value.data = std::move(data);
                        break;
                    }
                    default:
                        break;
                }

                javascript::Property property;
                property.key.data = std::make_shared<javascript::EString>(
                    javascript::EString{.value = helpers::StringToUTF16(key)});
                auto arrow_data = std::make_shared<javascript::EArrow>();
                arrow_data->prefer_expr = true;
                arrow_data->body.block.stmts.push_back(javascript::Stmt{
                    .data = std::make_shared<javascript::SReturn>(
                        javascript::SReturn{.value_or_nil = std::move(value)}),
                });
                property.value_or_nil.data = std::move(arrow_data);
                object->properties.push_back(std::move(property));
            }

            Source source{
                .pretty_paths = glob_result.pretty_paths,
                .key_path     = Path{.text = fake_source_path, .namespace_ = "file"},
                .index        = source_index,
            };
            javascript::AST ast = javascript::GlobResolveAST(log, source,
                javascript::OptionsFromConfig(&options),
                std::move(import_records), std::move(object), glob_result.export_alias);

            while (resolve_results.size() < ast.import_records.size()) {
                resolve_results.push_back(nullptr);
            }

            ParseResult result;
            result.resolve_results = std::move(resolve_results);
            auto repr              = std::make_shared<graph::JSRepr>();
            repr->ast              = std::move(ast);
            result.file.input_file.repr = graph::InputFileRepr{std::move(repr)};
            result.file.input_file.omit_from_source_maps_and_metafile = true;
            result.file.input_file.source                             = std::move(source);
            result.ok                                                 = true;
            return result;
        }

// Converts the fully-scanned results array into the dense list of input
// files the linker consumes, producing per-file metadata along the way.
// Runs after scanning so it operates on the closed graph; it handles:
//   - pretty-path collision detection (import attributes appended to
//     disambiguate files imported in several ways),
//   - automatic minification of the metafile for very large bundles,
//   - resolving "dual package hazard" imports toward the package.json
//     "main" field when both import styles hit the same package,
//   - validating import assertions (e.g. JSON loader), CSS import kinds,
//     and generating JS stubs for CSS files imported from JS,
//   - warning on side-effect-free bare imports,
//   - building each file's metafile JSON chunk and generating additional
//     output files for the "file"/"copy" loaders,
//   - running TLA validation on every parsed module.
//
// Input:  the entry point metadata (for copy-loader entries) and this->results.
// Output: one ScannerFile per source index, in order; the linker then
//         processes this list.
std::vector<ScannerFile> Scanner::ProcessScannedFiles(
            const std::vector<graph::EntryPoint>& entry_point_meta)
        {
            TimerScope scope(timer, "Process scanned files");

            std::unordered_map<uint32_t, uint32_t>
                entry_point_source_index_to_meta_index;
            entry_point_source_index_to_meta_index.reserve(entry_point_meta.size());
            for (size_t i = 0; i < entry_point_meta.size(); i++) {
                entry_point_source_index_to_meta_index.emplace(
                    entry_point_meta[i].source_index, static_cast<uint32_t>(i));
            }

            std::map<std::pair<std::string, std::string>, std::vector<uint32_t>>
                import_attribute_name_collisions;
            for (uint32_t source_index = 0; source_index < results.size();
                 source_index++)
            {
                ParseResult& result = results[source_index];
                if (result.ok) {
                    const PrettyPaths& pretty_paths =
                        result.file.input_file.source.pretty_paths;
                    import_attribute_name_collisions[{pretty_paths.abs,
                        pretty_paths.rel}]
                        .push_back(source_index);
                }
            }

            for (const auto& [paths, source_indices] :
                import_attribute_name_collisions)
            {
                if (source_indices.size() == 1) {
                    continue;
                }

                for (uint32_t source_index : source_indices) {
                    Source& source = results[source_index].file.input_file.source;
                    std::vector<logger::ImportAttribute> attrs =
                        source.key_path.import_attributes.DecodeIntoArray();
                    if (attrs.empty()) {
                        continue;
                    }

                    std::string suffix = " with {";
                    for (size_t i = 0; i < attrs.size(); i++) {
                        if (i > 0) {
                            suffix += ',';
                        }
                        suffix += ' ';
                        if (javascript::IsIdentifier(attrs[i].key)) {
                            suffix += attrs[i].key;
                        } else {
                            suffix += helpers::QuoteSingle(attrs[i].key, false);
                        }
                        suffix += ": ";
                        suffix += helpers::QuoteSingle(attrs[i].value, false);
                    }
                    suffix                       += " }";
                    source.pretty_paths.abs      += suffix;
                    source.pretty_paths.rel      += suffix;
                }
            }

            if (results.size() > 256) {
                options.MetafileFormatData = config::MetafileFormat::kMinified;
            }

            for (uint32_t source_index = 0; source_index < results.size();
                 source_index++)
            {
                ParseResult& parse_result   = results[source_index];
                if (!parse_result.ok) {
                    continue;
                }
                graph::InputFile& input_file = parse_result.file.input_file;

                std::string sb;
                bool is_first_import = true;

                if (options.NeedsMetafile) {
                    sb += helpers::QuoteForJSON(
                        input_file.source.pretty_paths.Select(
                            options.MetafilePathStyle),
                        options.ASCIIOnly);
                    sb += config::MaybeRemoveWhitespace(options.MetafileFormatData,
                        ": {\n      \"bytes\": " +
                            std::to_string(input_file.source.contents.size()) +
                            ",\n      \"imports\": [");
                }

                std::vector<compiler::ImportRecord>* records = nullptr;
                if (options.BuildMode == config::Mode::kBundle) {
                    if (auto* js_repr = TryJSRepr(input_file.repr);
                        js_repr != nullptr)
                    {
                        records = &js_repr->ImportRecords();
                    } else if (auto* ptr = std::get_if<std::shared_ptr<graph::CSSRepr>>(
                                   &input_file.repr);
                               ptr != nullptr && *ptr != nullptr)
                    {
                        records = &(*ptr)->ImportRecords();
                    }
                }

                if (records != nullptr) {
                    logger::LineColumnTracker tracker(&input_file.source);

                    for (uint32_t import_record_index = 0;
                         import_record_index < records->size();
                         import_record_index++)
                    {
                        compiler::ImportRecord& record =
                            (*records)[import_record_index];

                        std::string metafile_with;
                        if (options.NeedsMetafile) {
                            if (record.assert_or_with != nullptr &&
                                record.assert_or_with->keyword ==
                                    compiler::AssertOrWithKeyword::kWith &&
                                !record.assert_or_with->entries.empty())
                            {
                                std::string data =
                                    config::MaybeRemoveWhitespace(
                                        options.MetafileFormatData,
                                        ",\n          \"with\": {");
                                for (size_t i = 0;
                                     i < record.assert_or_with->entries.size();
                                     i++)
                                {
                                    const compiler::AssertOrWithEntry& entry =
                                        record.assert_or_with->entries[i];
                                    if (i > 0) {
                                        data += ',';
                                    }
                                    data += config::MaybeRemoveWhitespace(
                                        options.MetafileFormatData,
                                        "\n            ");
                                    data += helpers::QuoteForJSON(
                                        helpers::UTF16ToString(entry.key),
                                        options.ASCIIOnly);
                                    data += config::MaybeRemoveWhitespace(
                                        options.MetafileFormatData, ": ");
                                    data += helpers::QuoteForJSON(
                                        helpers::UTF16ToString(entry.value),
                                        options.ASCIIOnly);
                                }
                                data += config::MaybeRemoveWhitespace(
                                    options.MetafileFormatData, "\n          }");
                                metafile_with = std::move(data);
                            }
                        }

                        std::shared_ptr<resolver::ResolveResult> resolve_result;
                        if (import_record_index <
                            parse_result.resolve_results.size())
                        {
                            resolve_result =
                                parse_result.resolve_results[import_record_index];
                        }
                        if (resolve_result == nullptr ||
                            !record.source_index.IsValid())
                        {
                            if (options.NeedsMetafile) {
                                if (is_first_import) {
                                    is_first_import = false;
                                    sb += config::MaybeRemoveWhitespace(
                                        options.MetafileFormatData, "\n        ");
                                } else {
                                    sb += config::MaybeRemoveWhitespace(
                                        options.MetafileFormatData, ",\n        ");
                                }
                                sb += config::MaybeRemoveWhitespace(
                                          options.MetafileFormatData,
                                          "{\n          \"path\": ");
                                sb += helpers::QuoteForJSON(record.path.text,
                                    options.ASCIIOnly);
                                sb += config::MaybeRemoveWhitespace(
                                    options.MetafileFormatData,
                                    ",\n          \"kind\": ");
                                sb += helpers::QuoteForJSON(
                                    compiler::ImportKindToStringForMetafile(
                                        record.kind),
                                    options.ASCIIOnly);
                                sb += config::MaybeRemoveWhitespace(
                                    options.MetafileFormatData,
                                    ",\n          \"external\": true");
                                sb += metafile_with;
                                sb += config::MaybeRemoveWhitespace(
                                    options.MetafileFormatData, "\n        }");
                            }
                            continue;
                        }

                        if (resolve_result->path_pair.HasSecondary()) {
                            Path secondary_key =
                                resolve_result->path_pair.secondary;
                            if (secondary_key.namespace_ == "file") {
                                secondary_key.text =
                                    CanonicalFileSystemPathForWindows(
                                        secondary_key.text);
                            }
                            auto secondary_visited = visited.find(secondary_key);
                            if (secondary_visited != visited.end()) {
                                record.source_index = compiler::Index32::Make(
                                    secondary_visited->second.source_index);
                            }
                        }

                        ParseResult& other_result =
                            results[record.source_index.GetIndex()];
                        graph::InputFile& other_file =
                            other_result.file.input_file;
                        if (options.NeedsMetafile) {
                            if (is_first_import) {
                                is_first_import = false;
                                sb += config::MaybeRemoveWhitespace(
                                    options.MetafileFormatData, "\n        ");
                            } else {
                                sb += config::MaybeRemoveWhitespace(
                                    options.MetafileFormatData, ",\n        ");
                            }
                            sb += config::MaybeRemoveWhitespace(
                                      options.MetafileFormatData,
                                      "{\n          \"path\": ");
                            sb += helpers::QuoteForJSON(
                                other_file.source.pretty_paths.Select(
                                    options.MetafilePathStyle),
                                options.ASCIIOnly);
                            sb += config::MaybeRemoveWhitespace(
                                options.MetafileFormatData,
                                ",\n          \"kind\": ");
                            sb += helpers::QuoteForJSON(
                                compiler::ImportKindToStringForMetafile(
                                    record.kind),
                                options.ASCIIOnly);
                            sb += config::MaybeRemoveWhitespace(
                                options.MetafileFormatData,
                                ",\n          \"original\": ");
                            sb += helpers::QuoteForJSON(record.path.text,
                                options.ASCIIOnly);
                            sb += metafile_with;
                            sb += config::MaybeRemoveWhitespace(
                                options.MetafileFormatData, "\n        }");
                        }

                        if (compiler::Has(record.flags,
                                compiler::ImportRecordFlags::kAssertTypeJSON) &&
                            other_result.ok &&
                            other_file.loader != config::Loader::kJSON &&
                            other_file.loader != config::Loader::kCopy)
                        {
                            const compiler::AssertOrWithEntry* type_entry = nullptr;
                            if (record.assert_or_with != nullptr) {
                                type_entry = compiler::FindAssertOrWithEntry(
                                    record.assert_or_with->entries, "type");
                            }
                            if (type_entry != nullptr) {
                                std::vector<MsgData> notes;
                                notes.push_back(tracker.MakeMsgData(
                                    javascript::RangeOfImportAssertOrWith(
                                        input_file.source, *type_entry,
                                        javascript::KeyOrValue::kKeyAndValueRange),
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_ImportAssertionRequiresJSONLoaderNote)));
                                notes.push_back(MsgData{
                                    .text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_ReconfigureLoaderNote)});
                                log.AddErrorWithNotes(&tracker, record.range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_ImportAssertionRequiresJSONLoader,
                                        other_file.source.pretty_paths.Select(options.LogPathStyle),
                                        std::string(config::LoaderToString(other_file.loader))),
                                    notes);
                            }
                        }

                        switch (record.kind) {
                            case compiler::ImportKind::kComposesFrom: {
                                if (TryJSRepr(other_file.repr) != nullptr &&
                                    other_file.loader != config::Loader::kEmpty)
                                {
log.AddErrorWithNotes(&tracker, record.range,
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotUseComposesWith,
                                            other_file.source.pretty_paths.Select(options.LogPathStyle)),
                                        {MsgData{.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_ComposesOnlyCSSNote,
                                                     other_file.source.pretty_paths.Select(options.LogPathStyle),
                                                     std::string(config::LoaderToString(other_file.loader)))}});
                                }
                                break;
                            }

                            case compiler::ImportKind::kAt: {
                                if (TryJSRepr(other_file.repr) != nullptr &&
                                    other_file.loader != config::Loader::kEmpty)
                                {
log.AddErrorWithNotes(&tracker, record.range,
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotImportIntoCSS,
                                            other_file.source.pretty_paths.Select(options.LogPathStyle)),
                                        {MsgData{.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotImportIntoCSSNote,
                                                     other_file.source.pretty_paths.Select(options.LogPathStyle),
                                                     std::string(config::LoaderToString(other_file.loader)))}});
                                }
                                break;
                            }

                            case compiler::ImportKind::kUrl: {
                                if (auto* ptr = std::get_if<std::shared_ptr<
                                        graph::CSSRepr>>(&other_file.repr);
                                    ptr != nullptr && *ptr != nullptr)
                                {
log.AddErrorWithNotes(&tracker, record.range,
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotUseAsURL,
                                            other_file.source.pretty_paths.Select(options.LogPathStyle)),
                                        {MsgData{.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotUseAsURLNote,
                                                     other_file.source.pretty_paths.Select(options.LogPathStyle),
                                                     std::string(config::LoaderToString(other_file.loader)))}});
                                } else if (auto* other_repr =
                                               TryJSRepr(other_file.repr);
                                           other_repr != nullptr)
                                {
                                    if (other_repr->ast.url_for_css.empty() &&
                                        other_file.loader !=
                                            config::Loader::kEmpty)
                                    {
log.AddErrorWithNotes(&tracker, record.range,
                                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotUseAsURLNoURL,
                                                other_file.source.pretty_paths.Select(options.LogPathStyle)),
                                            {MsgData{.text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotUseAsURLNoURLNote,
                                                         other_file.source.pretty_paths.Select(options.LogPathStyle),
                                                         std::string(config::LoaderToString(other_file.loader)))}});
                                    }
                                }
                                break;
                            }

                            default:
                                break;
                        }

                        if (std::holds_alternative<std::shared_ptr<graph::CopyRepr>>(
                                other_file.repr))
                        {
                            record.copy_source_index = record.source_index;
                            record.source_index      = compiler::Index32{};
                            continue;
                        }

                        if (TryJSRepr(input_file.repr) != nullptr) {
                            if (auto* css_ptr = std::get_if<
                                    std::shared_ptr<graph::CSSRepr>>(
                                    &other_file.repr);
                                css_ptr != nullptr && *css_ptr != nullptr)
                            {
                                graph::CSSRepr* css = css_ptr->get();
                                if (options.WriteToStdout) {
log.AddError(&tracker, record.range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotImportWithoutOutputPath,
                                        other_file.source.pretty_paths.Select(options.LogPathStyle)));
                                } else if (!css->js_source_index.IsValid()) {
                                    Path stub_key = other_file.source.key_path;
                                    if (stub_key.namespace_ == "file") {
                                        stub_key.text =
                                            CanonicalFileSystemPathForWindows(
                                                stub_key.text);
                                    }
                                    uint32_t stub_source_index = AllocateSourceIndex(
                                        stub_key,
                                        cache::SourceIndexKind::kJSStubForCSS);
                                    Source stub_source    = other_file.source;
                                    stub_source.index     = stub_source_index;
                                    javascript::AST stub_ast =
                                        javascript::LazyExportAST(log, stub_source,
                                            javascript::OptionsFromConfig(&options),
                                            javascript::Expr( javascript::kENullShared, {}),
                                            nullptr);
                                    auto stub_repr =
                                        std::make_shared<graph::JSRepr>();
                                    stub_repr->ast             = std::move(stub_ast);
                                    stub_repr->css_source_index =
                                        record.source_index;
                                    ParseResult stub_result;
                                    stub_result.ok = true;
                                    stub_result.file.input_file.source =
                                        std::move(stub_source);
                                    stub_result.file.input_file.loader =
                                        other_file.loader;
                                    stub_result.file.input_file.repr =
                                        graph::InputFileRepr{std::move(stub_repr)};
                                    stub_result.file.input_file
                                        .omit_from_source_maps_and_metafile = true;
                                    results[stub_source_index] =
                                        std::move(stub_result);
                                    css->js_source_index =
                                        compiler::Index32::Make(stub_source_index);
                                }
                                record.source_index = css->js_source_index;
                                if (!css->js_source_index.IsValid()) {
                                    continue;
                                }
                            }
                        }

                        if (compiler::Has(record.flags,
                                compiler::ImportRecordFlags::
                                    kWasOriginallyBareImport) &&
                            !options.IgnoreDCEAnnotations &&
                            !helpers::IsInsideNodeModules(
                                input_file.source.key_path.text))
                        {
                            graph::InputFile& other_module =
                                results[record.source_index.GetIndex()]
                                    .file.input_file;
                            if (other_module.side_effects.kind !=
                                    graph::SideEffectsKind::kHasSideEffects &&

                                other_module.side_effects.kind !=
                                    graph::SideEffectsKind::
                                        kNoSideEffectsPureDataFromPlugin &&

                                other_module.side_effects.kind !=
                                    graph::SideEffectsKind::kNoSideEffectsEmptyAST)
                            {
                                std::vector<MsgData> notes;
                                std::string by;
                                if (other_module.side_effects.data != nullptr) {
                                    const resolver::SideEffectsData& data =
                                        *other_module.side_effects.data;
                                    if (!data.plugin_name.empty()) {
                                        by = " by plugin \"" + data.plugin_name + "\"";
                                    } else {
                                        std::string text;
                                        if (data.is_side_effects_array_in_json) {
                                            text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_SideEffectsExcludedFromArray);
                                        } else {
                                            text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_SideEffectsFalse);
                                        }
                                        logger::LineColumnTracker side_tracker(
                                            data.source.get());
                                        notes.push_back(side_tracker.MakeMsgData(
                                            data.range, text));
                                    }
                                }
                                log.AddIDWithNotes(
                                    logger::MsgID::kBundler_IgnoredBareImport,
                                    logger::MsgKind::kWarning, &tracker, record.range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_IgnoringImportNoSideEffects,
                                        other_module.source.pretty_paths.Select(options.LogPathStyle), by),
                                    notes);
                            }
                        }
                    }
                }

                if (options.NeedsMetafile) {
                    if (!is_first_import) {
                        sb += config::MaybeRemoveWhitespace(
                            options.MetafileFormatData, "\n      ");
                    }
                    graph::JSRepr* js_repr = TryJSRepr(input_file.repr);
                    if (js_repr != nullptr &&
                        (js_repr->ast.exports_kind ==
                                javascript::ExportsKind::kCommonJS ||
                            js_repr->ast.exports_kind ==
                                javascript::ExportsKind::kESM))
                    {
                        std::string format = "cjs";
                        if (js_repr->ast.exports_kind ==
                            javascript::ExportsKind::kESM)
                        {
                            format = "esm";
                        }
                        sb += config::MaybeRemoveWhitespace(
                            options.MetafileFormatData, "],\n      \"format\": ");
                        sb += helpers::QuoteForJSON(format, options.ASCIIOnly);
                    } else {
                        sb += "]";
                    }
                    std::vector<logger::ImportAttribute> attrs =
                        input_file.source.key_path.import_attributes
                            .DecodeIntoArray();
                    if (!attrs.empty()) {
                        sb += config::MaybeRemoveWhitespace(
                            options.MetafileFormatData, ",\n      \"with\": {");
                        for (size_t i = 0; i < attrs.size(); i++) {
                            if (i > 0) {
                                sb += ',';
                            }
                            sb += config::MaybeRemoveWhitespace(
                                options.MetafileFormatData, "\n        ");
                            sb += helpers::QuoteForJSON(attrs[i].key,
                                options.ASCIIOnly);
                            sb += ": ";
                            sb += helpers::QuoteForJSON(attrs[i].value,
                                options.ASCIIOnly);
                        }
                        sb += config::MaybeRemoveWhitespace(
                            options.MetafileFormatData, "\n      }");
                    }
                    sb += config::MaybeRemoveWhitespace(
                        options.MetafileFormatData, "\n    }");
                }

                parse_result.file.json_metadata_chunk = std::move(sb);

                if (!input_file.unique_key_for_additional_file.empty()) {
                    const std::string& bytes = input_file.source.contents;
                    std::vector<config::PathTemplate> path_template =
                        options.AssetPathTemplate;

                    std::string custom_file_path;
                    bool use_output_file = false;
                    bool is_entry_point  = false;
                    if (input_file.loader == config::Loader::kCopy) {
                        auto meta_iter =
                            entry_point_source_index_to_meta_index.find(source_index);
                        if (meta_iter !=
                            entry_point_source_index_to_meta_index.end())
                        {
                            path_template    = options.EntryPathTemplate;
                            custom_file_path =
                                entry_point_meta[meta_iter->second].output_path;
                            use_output_file = !options.AbsOutputFile.empty();
                            is_entry_point  = true;
                        }
                    }

                    std::string hash;
                    if (config::HasPlaceholder(path_template,
                            config::PathPlaceholder::kHash))
                    {
                        helpers::Xxh64 h;
                        h.Write(bytes.data(), bytes.size());
                        uint64_t sum = h.Sum64();
                        std::array<uint8_t, 8> hash_bytes;
                        for (size_t i = 0; i < 8; i++) {
                            hash_bytes[i] =
                                static_cast<uint8_t>(sum >> ((7 - i) * 8));
                        }
                        hash = HashForFileName(hash_bytes);
                    }

                    std::string dir, base, ext;
                    if (use_output_file) {
                        dir  = "/";
                        base = fs->Base(options.AbsOutputFile);
                        ext  = fs->Ext(base);
                        base = base.substr(0, base.size() - ext.size());
                    } else {
                        std::string original_dir, original_base, original_ext;
                        logger::PlatformIndependentPathDirBaseExt(
                            input_file.source.key_path.text, original_dir,
                            original_base, original_ext);
                        std::pair<std::string, std::string> derived =
                            PathRelativeToOutbase(input_file, options, *fs,
                                false, custom_file_path);
                        dir  = derived.first;
                        base = derived.second;
                        ext  = original_ext;
                    }

                    std::string template_ext = ext.starts_with(".")
                        ? ext.substr(1)
                        : ext;
                    config::PathPlaceholders placeholders{
                        .Dir  = &dir,
                        .Name = &base,
                        .Hash = &hash,
                        .Ext  = &template_ext,
                    };
                    std::string rel_path = config::TemplateToString(
                        config::SubstituteTemplate(path_template, placeholders)) +
                        ext;

                    std::string json_metadata_chunk;
                    if (options.NeedsMetafile) {
                        std::string inputs =
                            config::MaybeRemoveWhitespace(options.MetafileFormatData,
                                "{\n        ");
                        inputs += helpers::QuoteForJSON(
                            input_file.source.pretty_paths.Select(
                                options.MetafilePathStyle),
                            options.ASCIIOnly);
                        inputs +=
                            config::MaybeRemoveWhitespace(options.MetafileFormatData,
                                ": {\n          \"bytesInOutput\": ");
                        inputs += std::to_string(bytes.size());
                        inputs += config::MaybeRemoveWhitespace(
                            options.MetafileFormatData, "\n        }\n      }");

                        std::string entry_point_json;
                        if (is_entry_point) {
                            entry_point_json = helpers::QuoteForJSON(
                                input_file.source.pretty_paths.Select(
                                    options.MetafilePathStyle),
                                options.ASCIIOnly);
                            entry_point_json =
                                config::MaybeRemoveWhitespace(
                                    options.MetafileFormatData, "\"entryPoint\": ") +
                                entry_point_json +
                                config::MaybeRemoveWhitespace(
                                    options.MetafileFormatData, ",\n      ");
                        }

                        json_metadata_chunk =
                            config::MaybeRemoveWhitespace(options.MetafileFormatData,
                                "{\n      \"imports\": [],\n      \"exports\": [],\n      ") +
                            entry_point_json +
                            config::MaybeRemoveWhitespace(
                                options.MetafileFormatData, "\"inputs\": ") +
                            inputs +
                            config::MaybeRemoveWhitespace(options.MetafileFormatData,
                                ",\n      \"bytes\": ") +
                            std::to_string(bytes.size()) +
                            config::MaybeRemoveWhitespace(
                                options.MetafileFormatData, "\n    }");
                    }

                    graph::OutputFile additional_file;
                    additional_file.abs_path =
                        fs->Join({options.AbsOutputDir, rel_path});
                    additional_file.contents.assign(bytes.begin(), bytes.end());
                    additional_file.json_metadata_chunk =
                        std::move(json_metadata_chunk);
                    input_file.additional_files.clear();
                    input_file.additional_files.push_back(
                        std::move(additional_file));
                }
            }

            std::vector<ScannerFile> files(results.size());
            for (uint32_t source_index = 0; source_index < results.size();
                 source_index++)
            {
                ParseResult& result = results[source_index];
                if (result.ok) {
                    ValidateTLA(source_index);
                    files[source_index] = std::move(result.file);
                }
            }

            return files;
        }

// Computes whether a module (transitively) contains top-level await and, if
// so, where it comes from, storing the answer in the module's tla_check.
// The linker needs this to decide whether a module's closure must be made
// async. A top-level await reached through a CommonJS "require()" is an
// error (require is synchronous and cannot await), and the whole import
// chain is walked to produce a helpful multi-line diagnostic.
//
// Input:  a source index of an already-parsed module.
// Output: the module's TlaCheck (parent/depth/import_record_index), also
//         stored in this->results[source_index].tla_check; may log an
//         error for a require of a TLA chain.
TlaCheck Scanner::ValidateTLA(uint32_t source_index) {
            ParseResult& result = results[source_index];

            if (result.ok && result.tla_check.depth == 0) {
                if (graph::JSRepr* repr = TryJSRepr(result.file.input_file.repr);
                    repr != nullptr)
                {
                    result.tla_check.depth = 1;
                    if (repr->ast.live_top_level_await_keyword.len > 0) {
                        result.tla_check.parent = compiler::Index32::Make(source_index);
                    }

                    for (uint32_t import_record_index = 0;
                         import_record_index < repr->ast.import_records.size();
                         import_record_index++)
                    {
                        const compiler::ImportRecord& record =
                            repr->ast.import_records[import_record_index];
                        if (!record.source_index.IsValid() ||
                            record.source_index.GetIndex() ==
                                javascript::kRuntimeSourceIndex ||
                            (record.kind != compiler::ImportKind::kRequire &&
                                record.kind != compiler::ImportKind::kStmt))
                        {
                            continue;
                        }
                        TlaCheck parent_check =
                            ValidateTLA(record.source_index.GetIndex());
                        if (!parent_check.parent.IsValid()) {
                            continue;
                        }

                        if (record.kind == compiler::ImportKind::kStmt &&
                            (!result.tla_check.parent.IsValid() ||
                                parent_check.depth < result.tla_check.depth))
                        {
                            result.tla_check.depth             = parent_check.depth + 1;
                            result.tla_check.parent            = record.source_index;
                            result.tla_check.import_record_index = import_record_index;
                            continue;
                        }

                        if (record.kind == compiler::ImportKind::kRequire) {
                            std::vector<MsgData> notes;
                            PrettyPaths tla_pretty_paths;
                            uint32_t other_source_index =
                                record.source_index.GetIndex();

                            while (true) {
                                ParseResult& parent_result = results[other_source_index];
                                graph::JSRepr* parent_repr =
                                    TryJSRepr(parent_result.file.input_file.repr);

                                if (parent_repr != nullptr &&
                                    parent_repr->ast.live_top_level_await_keyword.len >
                                        0)
                                {
                                    tla_pretty_paths =
                                        parent_result.file.input_file.source.pretty_paths;
                                    logger::LineColumnTracker parent_tracker(
                                        &parent_result.file.input_file.source);
                                    notes.push_back(parent_tracker.MakeMsgData(
                                        parent_repr->ast.live_top_level_await_keyword,
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_TopLevelAwaitHere,
                                            tla_pretty_paths.Select(options.LogPathStyle))));
                                    break;
                                }

                                if (parent_repr == nullptr ||
                                    !parent_result.tla_check.parent.IsValid())
                                {
                                    notes.push_back(MsgData{
                                        .text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_UnexpectedInvalidIndex)});
                                    break;
                                }

                                uint32_t next_source_index =
                                    parent_result.tla_check.parent.GetIndex();

                                logger::LineColumnTracker parent_tracker(
                                    &parent_result.file.input_file.source);
                                notes.push_back(parent_tracker.MakeMsgData(
                                    parent_repr->ast
                                        .import_records[parent_result.tla_check
                                                            .import_record_index]
                                        .range,
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_ImportsFileHere,
                                        parent_result.file.input_file.source.pretty_paths.Select(options.LogPathStyle),
                                        results[next_source_index].file.input_file.source.pretty_paths.Select(options.LogPathStyle))));

                                other_source_index = next_source_index;
                            }

                            std::string text;
                            const PrettyPaths& imported_pretty_paths =
                                results[record.source_index.GetIndex()]
                                    .file.input_file.source.pretty_paths;

                            if (imported_pretty_paths.abs == tla_pretty_paths.abs &&
                                imported_pretty_paths.rel == tla_pretty_paths.rel)
                            {
                                text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_RequireCallTopLevelAwait,
                                       imported_pretty_paths.Select(options.LogPathStyle));
                            } else {
                                text = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_RequireCallTransitiveTLA,
                                       tla_pretty_paths.Select(options.LogPathStyle));
                            }

                            logger::LineColumnTracker result_tracker(
                                &result.file.input_file.source);
                            log.AddErrorWithNotes(&result_tracker, record.range, text,
                                notes);
                        }
                    }

                    if (result.tla_check.parent.IsValid()) {
                        repr->meta.is_async_or_has_async_dependency = true;
                    }
                }
            }

            return result.tla_check;
        }


    // Creates a bundle by scanning the whole module graph starting from the
    // configured entry points until every reachable module has been parsed.
    // This drives the full scan pipeline: on-start plugins run in parallel
    // first, then a resolver and Scanner are created, the shared cached
    // runtime module is seeded, injected files and entry points are set up,
    // and finally every dependency is scanned until the graph is closed.
    // Cancellation is checked after each phase, in which case an empty Bundle
    // is returned so the caller can stop early.
    //
    // Input:  the API call kind, logger, filesystem, caches, entry point
    //         descriptors, resolved options, and an optional timing timer.
    // Output: a Bundle holding the scanned input files, entry point metadata,
    //         HTML inline-run info, the resolver, options, and a unique key
    //         prefix; the linker consumes this next.
    Bundle ScanBundle(
        config::APICall call,
        Log& log,
        Fs& fs,
        cache::CacheSet& caches,
        const std::vector<config::EntryPoint>& entry_points,
        config::Options options,
        helpers::Timer* timer)
    {
        TimerScope scan_scope(timer, "Scan phase");

        ApplyOptionDefaults(options);

        TimerScope start_scope(timer, "On-start callbacks");
        WaitGroup on_start_wait_group;
        std::vector<std::thread> on_start_threads;
        std::vector<std::exception_ptr> on_start_exceptions;
        for (const config::Plugin& plugin : options.Plugins) {
            for (const config::OnStart& on_start : plugin.OnStartList) {
                on_start_wait_group.Add(1);
                on_start_threads.emplace_back(
                    [plugin, on_start, &log, &fs, &on_start_wait_group, &on_start_exceptions]() {
                        try {
                        config::OnStartResult result = on_start.Callback();
                        LogPluginMessages(fs, log, plugin.Name, result.Msgs,
                            result.ThrownError, nullptr, Range{});
                        } catch (...) {
                            on_start_exceptions.push_back(std::current_exception());
                        }
                        on_start_wait_group.Done();
                    });
            }
        }

        std::string unique_key_prefix = GenerateUniqueKeyPrefix();

        std::shared_ptr<resolver::Resolver> res =
            resolver::NewResolver(call, fs, log, caches, &options);

        Scanner s{
            .log              = log,
            .fs               = &fs,
            .res              = res,
            .caches           = &caches,
            .timer            = timer,
            .unique_key_prefix = unique_key_prefix,
            .results          = {},
            .visited          = {},
            .result_channel   = std::make_shared<Channel<ParseResult>>(),
            .options          = options,
            .remaining        = {},
            .threads          = {},
            .html_inline      = {},
            .stdin_info_holder = {},
            .thrown_exceptions = {},
            .thrown_exceptions_mu = {},
        };

        s.results.emplace_back();
        s.remaining++;
        s.Spawn([&s, &options]() {
            RuntimeCache::Parsed parsed = GlobalRuntimeCache().Parse(options);
            ParseResult result;
            result.file.input_file.source       = parsed.source;
            result.file.input_file.repr         = graph::InputFileRepr{
                std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(parsed.ast)})};
            result.file.input_file.omit_from_source_maps_and_metafile = true;
            result.ok = parsed.ok;
            s.result_channel->Send(std::move(result));
        });

        for (auto& thread : on_start_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        for (auto& e : on_start_exceptions) {
            if (e) std::rethrow_exception(e);
        }
        start_scope.timer = nullptr;

        if (timer != nullptr) {
            timer->End("On-start callbacks");
        }

        if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
            s.JoinThreads();
            Bundle bundle;
            bundle.options = options;
            return bundle;
        }

        s.PreprocessInjectedFiles();

        if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
            s.JoinThreads();
            Bundle bundle;
            bundle.options = options;
            return bundle;
        }

        std::vector<graph::EntryPoint> entry_point_meta =
            s.AddEntryPoints(entry_points);

        if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
            s.JoinThreads();
            Bundle bundle;
            bundle.options = options;
            return bundle;
        }

        s.ScanAllDependencies(entry_point_meta);
        if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
            s.JoinThreads();
            Bundle bundle;
            bundle.options = options;
            return bundle;
        }

        s.ExpandHtmlEntryPoints(entry_point_meta);

        std::vector<ScannerFile> files = s.ProcessScannedFiles(entry_point_meta);

        if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
            s.JoinThreads();
            Bundle bundle;
            bundle.options = options;
            return bundle;
        }

        s.JoinThreads();

        Bundle bundle;
        bundle.fs               = &fs;
        bundle.res              = res;
        bundle.files            = std::move(files);
        bundle.entry_points     = std::move(entry_point_meta);
        bundle.html_inline      = std::move(s.html_inline);
        bundle.unique_key_prefix = unique_key_prefix;
        bundle.options          = s.options;
        return bundle;
    }

}
