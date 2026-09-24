// Bundle compilation phase: consumes the scanned module graph, the resolved
// entry point list and the normalized options, and produces the finished
// output file set plus an optional metafile. This unit also holds the shared
// path/hash helpers, default option resolution, reachable-file discovery and
// parallel source-map data computation used across the bundler.

#include "guchho/bundler.hpp"
#include "guchho/javascript/js_runtime.hpp"


namespace guchho::bundler {


    ////////////////////////////////////////////////////////////////////////////////
    // Free functions shared with other regions of the code base.
    //
    // These routines are callable from elsewhere in the pipeline; they are
    // defined here at file scope rather than nested inside Bundle::Compile.
    // They cover hash-derived file naming, outbase-relative path computation,
    // the default extension-to-loader table and option pre-processing.

    // Maps raw hash bytes to the short alphanumeric suffix used for hashed
    // chunk and asset file names. Any chunk that uses content hashing
    // receives an eight-character base32-encoded substring of the full
    // digest.
    //
    // Input : A span of raw hash bytes (typically a truncated digest).
    // Output: The first eight characters of the base32 encoding of those bytes.
    std::string HashForFileName(std::span<const uint8_t> hash_bytes) {
        return helpers::Base32StdEncode(std::string_view(
                   reinterpret_cast<const char*>(hash_bytes.data()),
                   hash_bytes.size()))
            .substr(0, 8);
    }

    // Computes the directory and base-name pair describing where an input file
    // should be emitted relative to the output base directory. It honors an
    // explicit override path, derives a synthetic location for virtual
    // sources, collapses "index" base names onto their parent directory, and
    // guards against parent-directory traversal that would escape past the
    // output root.
    //
    // Input : The input file, the resolved options, the filesystem, whether
    //         the "index" heuristic is enabled, and an optional override path.
    // Output: A pair holding the leading "/" relative directory and the base
    //         name of the output path.
    std::pair<std::string, std::string> PathRelativeToOutbase(
        const graph::InputFile& input_file,
        const config::Options& options,
        filesystem::Fs& fs,
        bool avoid_index,
        const std::string& custom_file_path)
    {
        std::string rel_dir = "/";
        std::string base_name;
        std::string abs_path = input_file.source.key_path.text;

        if (!custom_file_path.empty()) {
            abs_path = custom_file_path;
            if (!fs.IsAbs(abs_path)) {
                abs_path = fs.Join({options.AbsOutputBase, abs_path});
            }
        } else if (input_file.source.key_path.namespace_ != "file") {
            std::string dir, base, ext;
            logger::PlatformIndependentPathDirBaseExt(abs_path, dir, base, ext);
            if (avoid_index && base == "index") {
                logger::PlatformIndependentPathDirBaseExt(dir, dir, base, ext);
            }
            base_name = SanitizeFilePathForVirtualModulePath(base);
            return {rel_dir, base_name};
        } else {
            if (avoid_index) {
                std::string base = fs.Base(abs_path);
                base = base.substr(0, base.size() - fs.Ext(base).size());
                if (base == "index") {
                    abs_path = fs.Dir(abs_path);
                }
            }
        }

        std::optional<std::string> rel_path = fs.Rel(options.AbsOutputBase, abs_path);
        if (!rel_path.has_value()) {
            base_name = fs.Base(abs_path);
        } else {
            rel_dir   = fs.Dir(*rel_path) + "/";
            base_name = fs.Base(*rel_path);

            std::string forward_slashes;
            forward_slashes.reserve(rel_dir.size());
            for (char c : rel_dir) {
                forward_slashes.push_back(c == '\\' ? '/' : c);
            }
            rel_dir = std::move(forward_slashes);

            size_t dot_dot_count = 0;
            while (rel_dir.substr(dot_dot_count * 3).starts_with("../")) {
                dot_dot_count++;
            }
            if (dot_dot_count > 0) {
                std::string prefix;
                for (size_t i = 0; i < dot_dot_count; i++) {
                    prefix += "_.._/";
                }
                rel_dir = prefix + rel_dir.substr(dot_dot_count * 3);
            }
            while (rel_dir.ends_with("/")) {
                rel_dir.pop_back();
            }
            rel_dir = "/" + rel_dir;
            if (rel_dir.ends_with("/.")) {
                rel_dir.pop_back();
            }
        }

        if (custom_file_path.empty()) {
            std::string ext = fs.Ext(base_name);
            base_name       = base_name.substr(0, base_name.size() - ext.size());
        }
        return {std::move(rel_dir), std::move(base_name)};
    }

    ////////////////////////////////////////////////////////////////////////////////
    // DefaultExtensionToLoaderMap.
    //
    // The built-in table that maps file extensions (including the empty
    // extension) to the loader used to parse those files. Asset-oriented
    // extensions resolve to a copy loader so referenced files are emitted
    // verbatim instead of being wrapped in generated modules.

    // Returns the default mapping between file extensions and the loader that
    // parses each extension. The empty-string key covers extension-less files,
    // and every asset format is assigned the copy loader so those files pass
    // through unchanged.
    //
    // Input : None.
    // Output: An unordered map from extension key to loader value.
    std::unordered_map<std::string, config::Loader> DefaultExtensionToLoaderMap() {
        return {
            {"",            config::Loader::kJS},
            {".js",         config::Loader::kJS},
            {".mjs",        config::Loader::kJS},
            {".cjs",        config::Loader::kJS},
            {".jsx",        config::Loader::kJSX},
            {".ts",         config::Loader::kTS},
            {".cts",        config::Loader::kTSNoAmbiguousLessThan},
            {".mts",        config::Loader::kTSNoAmbiguousLessThan},
            {".tsx",        config::Loader::kTSX},
            {".css",        config::Loader::kCSS},
            {".module.css", config::Loader::kLocalCSS},
            {".html",       config::Loader::kHTML},
            {".json",       config::Loader::kJSON},
            {".txt",        config::Loader::kText},

            {".avif",       config::Loader::kCopy},
            {".bmp",        config::Loader::kCopy},
            {".cur",        config::Loader::kCopy},
            {".eot",        config::Loader::kCopy},
            {".gif",        config::Loader::kCopy},
            {".ico",        config::Loader::kCopy},
            {".jpeg",       config::Loader::kCopy},
            {".jpg",        config::Loader::kCopy},
            {".mp3",        config::Loader::kCopy},
            {".mp4",        config::Loader::kCopy},
            {".ogg",        config::Loader::kCopy},
            {".otf",        config::Loader::kCopy},
            {".pdf",        config::Loader::kCopy},
            {".png",        config::Loader::kCopy},
            {".svg",        config::Loader::kCopy},
            {".ttf",        config::Loader::kCopy},
            {".wav",        config::Loader::kCopy},
            {".webm",       config::Loader::kCopy},
            {".webp",       config::Loader::kCopy},
            {".woff",       config::Loader::kCopy},
            {".woff2",      config::Loader::kCopy},
        };
    }

    ////////////////////////////////////////////////////////////////////////////////
    // ApplyOptionDefaults.
    //
    // Fills in every usable default before the rest of the pipeline relies on
    // the option fields being populated. It also reconciles contradictory
    // unsupported-feature overrides and tunes the unsupported feature set
    // for non-browser output platforms.

    // Keeps the unsupported-feature bookkeeping self-consistent when one
    // feature override logically implies another. If a parent feature is
    // overridden as unsupported, every feature it implies becomes unsupported
    // as well, and the override bookkeeping is updated so later checks agree.
    //
    // Input : The options object, the implying feature flag and the implied
    //         feature mask.
    // Output: The options' unsupported-feature flags and override masks are
    //         refreshed in place.
    static void FixInvalidUnsupportedJSFeatureOverrides(
        config::Options& options,
        compat::JSFeature implies,
        compat::JSFeature implied)
    {
        if (compat::Has(options.UnsupportedJSFeatureOverrides, implies)) {
            options.UnsupportedJSFeatures |= implied;
            options.UnsupportedJSFeatureOverrides |= implied;
            options.UnsupportedJSFeatureOverridesMask |= implied;
        }
    }

    // Normalizes the user-supplied options by filling any blank field with the
    // built-in default: the extension-to-loader table, output extensions and
    // the path templates that control generated names. It also resolves
    // incompatible feature options before linking begins.
    //
    // Input : A reference to the options to normalize.
    // Output: The referenced options object is mutated in place.
    void ApplyOptionDefaults(config::Options& options) {
        if (options.ExtensionToLoader.empty()) {
            options.ExtensionToLoader = DefaultExtensionToLoaderMap();
        }
        if (options.OutputExtensionJS.empty()) {
            options.OutputExtensionJS = ".js";
        }
        if (options.OutputExtensionCSS.empty()) {
            options.OutputExtensionCSS = ".css";
        }

        if (options.EntryPathTemplate.empty()) {
            options.EntryPathTemplate = {
                { "./", config::PathPlaceholder::kDir },
                { "/",  config::PathPlaceholder::kName },
            };
        }
        if (options.ChunkPathTemplate.empty()) {
            options.ChunkPathTemplate = {
                { "./", config::PathPlaceholder::kName },
                { "-",  config::PathPlaceholder::kHash },
            };
        }
        if (options.AssetPathTemplate.empty()) {
            options.AssetPathTemplate = {
                { "./", config::PathPlaceholder::kName },
                { "-",  config::PathPlaceholder::kHash },
            };
        }

        options.ProfilerNames = !options.MinifyIdentifiers;

        FixInvalidUnsupportedJSFeatureOverrides(options,
            compat::JSFeature::kAsyncAwait,
            compat::JSFeature::kAsyncGenerator | compat::JSFeature::kForAwait | compat::JSFeature::kTopLevelAwait);
        FixInvalidUnsupportedJSFeatureOverrides(options,
            compat::JSFeature::kGenerator,
            compat::JSFeature::kAsyncGenerator);
        FixInvalidUnsupportedJSFeatureOverrides(options,
            compat::JSFeature::kObjectAccessors,
            compat::JSFeature::kClassPrivateAccessor | compat::JSFeature::kClassPrivateStaticAccessor);
        FixInvalidUnsupportedJSFeatureOverrides(options,
            compat::JSFeature::kClassField,
            compat::JSFeature::kClassPrivateField);
        FixInvalidUnsupportedJSFeatureOverrides(options,
            compat::JSFeature::kClassStaticField,
            compat::JSFeature::kClassPrivateStaticField);
        FixInvalidUnsupportedJSFeatureOverrides(options,
            compat::JSFeature::kClass,
            compat::JSFeature::kClassField | compat::JSFeature::kClassPrivateAccessor |
            compat::JSFeature::kClassPrivateBrandCheck | compat::JSFeature::kClassPrivateField |
            compat::JSFeature::kClassPrivateMethod | compat::JSFeature::kClassPrivateStaticAccessor |
            compat::JSFeature::kClassPrivateStaticField | compat::JSFeature::kClassPrivateStaticMethod |
            compat::JSFeature::kClassStaticBlocks | compat::JSFeature::kClassStaticField);

        if (options.OutputPlatform != config::Platform::kBrowser) {
            if (!compat::Has(options.UnsupportedJSFeatureOverridesMask, compat::JSFeature::kInlineScript)) {
                options.UnsupportedJSFeatures |= compat::JSFeature::kInlineScript;
            }
            if (!compat::Has(options.UnsupportedCSSFeatureOverridesMask, compat::CSSFeature::kInlineStyle)) {
                options.UnsupportedCSSFeatures |= compat::CSSFeature::kInlineStyle;
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // FindReachableFiles.
    //
    // Walks the module dependency graph depth-first from the runtime module
    // and every entry point, collecting the ordered set of source file indices
    // used by later phases for source-map generation and metafile layout.

    // Performs a depth-first traversal of the module graph, starting at the
    // runtime module and each entry point, and records every reachable source
    // index exactly once in dependency-first order so a parent always precedes
    // every child file in the returned list.
    //
    // Input : The complete input-file table and the resolved entry point list.
    // Output: The reachable source indices, each one appearing only after the
    //         modules that depend on it.
    std::vector<uint32_t> FindReachableFiles(
        const std::vector<graph::InputFile>& files,
        const std::vector<graph::EntryPoint>& entry_points)
    {
        std::vector<uint32_t> order;
        std::unordered_set<uint32_t> visited;

        // Examines a single source index, scheduling any dependency that has not
        // been seen yet before appending the index to the ordering.
        std::function<void(uint32_t)> visit = [&](uint32_t source_index) {
            if (!visited.insert(source_index).second) {
                return;
            }
            if (source_index < files.size()) {
                const auto& file = files[source_index];
                if (auto* js_repr = std::get_if<std::shared_ptr<graph::JSRepr>>(&file.repr)) {
                    if ((*js_repr)->css_source_index.IsValid()) {
                        visit((*js_repr)->css_source_index.GetIndex());
                    }
                    for (const auto& record : (*js_repr)->ImportRecords()) {
                        if (record.source_index.IsValid()) {
                            visit(record.source_index.GetIndex());
                        } else if (record.copy_source_index.IsValid()) {
                            visit(record.copy_source_index.GetIndex());
                        }
                    }
                } else if (auto* css_repr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&file.repr)) {
                    for (const auto& record : (*css_repr)->ImportRecords()) {
                        if (record.source_index.IsValid()) {
                            visit(record.source_index.GetIndex());
                        } else if (record.copy_source_index.IsValid()) {
                            visit(record.copy_source_index.GetIndex());
                        }
                    }
                }
            }
            order.push_back(source_index);
        };

        visit(javascript::kSourceIndex);

        for (const auto& ep : entry_points) {
            visit(ep.source_index);
        }

        return order;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Bundle::Compile – helpers.
    //
    // File-private utilities used during compilation: canonical path
    // comparison keys, entry output-path reconstruction, output path cleanup,
    // parallel source-map data precompute and metafile assembly.

    // Normalizes a filesystem path into a canonical comparison key so two
    // spellings of the same location compare equal. On Windows, backslashes
    // are folded to forward slashes and the text is lower-cased for a
    // case-insensitive match; elsewhere the text is lower-cased but slashes
    // are kept as written.
    //
    // Input : A filesystem path as text.
    // Output: The canonical key appropriate for comparing against another
    //         canonicalized path.
    static std::string CanonicalPathForComparison(std::string_view path) {
        std::string result(path);
#ifdef _WIN32
        std::transform(result.begin(), result.end(), result.begin(),
            [](char c) { return c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
#else
        std::transform(result.begin(), result.end(), result.begin(),
            [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
#endif
        return result;
    }

    // Rebuilds the output location of an entry point's chunk relative to the
    // output directory, following the same path-template substitution the
    // linker performs when it derives chunk locations. This is how the HTML
    // pass locates the emitted files of virtual inline script and style
    // entries so they can be folded back into their container document.
    //
    // Input : The options, the entry's input file, the filesystem, the
    //         configured output path and the standard output extension.
    // Output: The relative output location of that entry's chunk.
    static std::string ComputeEntryOutputRelPath(
        const config::Options& options,
        const graph::InputFile& input_file,
        filesystem::Fs& fs,
        const std::string& entry_output_path,
        const std::string& std_ext)
    {
        auto [rel_dir, rel_base] = PathRelativeToOutbase(input_file, options, fs,
            /* avoid_index */ false, entry_output_path);
        std::string template_ext = std_ext;
        if (!template_ext.empty() && template_ext[0] == '.') {
            template_ext = template_ext.substr(1);
        }
        std::vector<config::PathTemplate> tmpl = options.EntryPathTemplate;
        tmpl.push_back({.Data = std_ext});
        std::vector<config::PathTemplate> substituted = config::SubstituteTemplate(tmpl,
            config::PathPlaceholders{
                .Dir  = &rel_dir,
                .Name = &rel_base,
                .Ext  = &template_ext,
            });
        return config::TemplateToString(std::move(substituted));
    }

    // Cleans a raw output location by collapsing duplicate slashes, removing a
    // leading "/" or "./", and resolving inner "." pieces, so the coordinate
    // left over is a tidy forward-slash string suitable for computing relative
    // references between emitted files.
    //
    // Input : A location that may hold leftover template text.
    // Output: The cleaned coordinate, or "." when the location reduces to an
    //         empty value.
    static std::string NormalizeRelativeOutputPath(std::string p) {
        std::string out;
        bool last_slash = false;
        for (char c : p) {
            if (c == '/' || c == '\\') {
                if (!last_slash) {
                    out.push_back('/');
                }
                last_slash = true;
            } else {
                out.push_back(c);
                last_slash = false;
            }
        }
        while (!out.empty() && (out[0] == '/')) {
            out.erase(0, 1);
        }
        while (out.starts_with("./")) {
            out.erase(0, 2);
        }
        std::string cleaned;
        size_t i = 0;
        while (i < out.size()) {
            if (out[i] == '.' && (i + 1 == out.size() || out[i + 1] == '/')) {
                i += 1;
                while (i < out.size() && out[i] == '/') {
                    i += 1;
                }
                continue;
            }
            cleaned.push_back(out[i]);
            i += 1;
        }
        if (cleaned.empty()) {
            cleaned = ".";
        }
        return cleaned;
    }

    // Launches one detached thread per reachable source file that can carry a
    // source map, computing each file's line-offset table and JSON-quoted
    // source content in parallel with the linking work that follows. The
    // returned callable blocks until every spawned task has finished and then
    // hands the result vector to the caller.
    //
    // Input : The options, the scanned file table and the reachable source
    //         indices that need processing.
    // Output: A function which blocks until completion and provides the
    //         per-file source-map data vector.
    static DataForSourceMapsFn ComputeDataForSourceMapsInParallel(
        const config::Options& options,
        const std::vector<ScannerFile>& scanner_files,
        const std::vector<uint32_t>& reachable_files)
    {
        if (options.SourceMapData == config::SourceMap::kNone) {
            return []() -> std::vector<DataForSourceMap> { return {}; };
        }

        auto results = std::make_shared<std::vector<DataForSourceMap>>(scanner_files.size());
        auto remaining = std::make_shared<uint32_t>(0);
        auto mu = std::make_shared<std::mutex>();
        auto cv = std::make_shared<std::condition_variable>();

        for (uint32_t source_index : reachable_files) {
            if (source_index >= scanner_files.size()) {
                continue;
            }
            const auto& f = scanner_files[source_index];
            if (!config::CanHaveSourceMap(f.input_file.loader)) {
                continue;
            }

            int32_t approximate_line_count = 0;
            if (auto* js_repr = std::get_if<std::shared_ptr<graph::JSRepr>>(&f.input_file.repr)) {
                approximate_line_count = (*js_repr)->ast.approximate_line_count;
            } else if (auto* css_repr = std::get_if<std::shared_ptr<graph::CSSRepr>>(&f.input_file.repr)) {
                approximate_line_count = (*css_repr)->ast.approximate_line_count;
            }

            {
                std::lock_guard<std::mutex> lock(*mu);
                (*remaining)++;
            }

            std::thread([results, remaining, mu, cv,
                         source_index, approximate_line_count,
                         contents = f.input_file.source.contents,
                         input_source_map = f.input_file.input_source_map,
                         ascii_only = options.ASCIIOnly,
                         exclude = options.ExcludeSourcesContent]() {
                // Reduces the outstanding-task counter and wakes the waiter
                // whenever a thread finishes, whether normally or through an
                // exception.
                struct DoneGuard {
                    std::shared_ptr<std::mutex> mu;
                    std::shared_ptr<uint32_t> remaining;
                    std::shared_ptr<std::condition_variable> cv;
                    ~DoneGuard() {
                        {
                            std::lock_guard<std::mutex> lock(*mu);
                            (*remaining)--;
                        }
                        cv->notify_one();
                    }
                } done{mu, remaining, cv};

                try {
                auto& result = (*results)[source_index];
                result.line_offset_tables = sourcemap::GenerateLineOffsetTables(contents, approximate_line_count);

                if (!exclude) {
                    if (input_source_map == nullptr) {
                        result.quoted_contents.push_back(helpers::QuoteForJSON(contents, ascii_only));
                    } else {
                        const auto& sm = *input_source_map;
                        result.quoted_contents.resize(sm.sources.size());
                        const std::string null_contents = "null";
                        for (size_t i = 0; i < sm.sources.size(); i++) {
                            std::string_view quoted_contents = null_contents;
                            if (i < sm.sources_content.size()) {
                                const auto& value = sm.sources_content[i];
                                if (!value.quoted.empty() && (!ascii_only || !IsASCIIOnly(value.quoted))) {
                                    quoted_contents = value.quoted;
                                } else if (!value.value.empty()) {
                                    std::string s = helpers::UTF16ToString(value.value);
                                    std::string quoted = helpers::QuoteForJSON(s, ascii_only);
                                    result.quoted_contents[i] = std::move(quoted);
                                    continue;
                                }
                            }
                            result.quoted_contents[i] = std::string(quoted_contents);
                        }
                    }
                }
                } catch (...) {
                }
            }).detach();
        }

        // Blocks until every in-flight source-map task has finished and returns the
        // populated result vector by moving it out of the shared holder.
        return [results, remaining, mu, cv]() -> std::vector<DataForSourceMap> {
            std::unique_lock<std::mutex> lock(*mu);
            cv->wait(lock, [&]() { return *remaining == 0; });
            return std::move(*results);
        };
    }

    // Assembles the metafile JSON document by concatenating the metadata chunk
    // recorded for each reachable input and each emitted output, keyed by
    // pretty-printed path. An output path written more than once is kept only
    // on the first occurrence.
    //
    // Input : The options, filesystem, scanned file table, output file list
    //         and the reachable source indices to include.
    // Output: The finished metafile JSON text terminated by a newline.
    static std::string GenerateMetadataJSON(
        const config::Options& options,
        filesystem::Fs& fs,
        const std::vector<ScannerFile>& scanner_files,
        const std::vector<graph::OutputFile>& output_files,
        const std::vector<uint32_t>& all_reachable_files)
    {
        std::string json;
        json.append(config::MaybeRemoveWhitespace(options.MetafileFormatData, "{\n  \"inputs\": {"));

        bool is_first = true;
        for (uint32_t source_index : all_reachable_files) {
            if (source_index >= scanner_files.size()) {
                continue;
            }
            const auto& file = scanner_files[source_index];
            if (file.input_file.omit_from_source_maps_and_metafile) {
                continue;
            }
            if (file.json_metadata_chunk.empty()) {
                continue;
            }
            if (is_first) {
                is_first = false;
                json.append(config::MaybeRemoveWhitespace(options.MetafileFormatData, "\n    "));
            } else {
                json.append(config::MaybeRemoveWhitespace(options.MetafileFormatData, ",\n    "));
            }
            json.append(file.json_metadata_chunk);
        }

        json.append(config::MaybeRemoveWhitespace(options.MetafileFormatData, "\n  },\n  \"outputs\": {"));

        is_first = true;
        std::unordered_set<std::string> path_map;
        for (const auto& result : output_files) {
            if (result.json_metadata_chunk.empty()) {
                continue;
            }
            auto pretty_paths = resolver::MakePrettyPaths(fs,
                Path{result.abs_path, "file"});
            std::string path = pretty_paths.Select(options.MetafilePathStyle);
            if (!path_map.insert(path).second) {
                continue;
            }
            if (is_first) {
                is_first = false;
                json.append(config::MaybeRemoveWhitespace(options.MetafileFormatData, "\n    "));
            } else {
                json.append(config::MaybeRemoveWhitespace(options.MetafileFormatData, ",\n    "));
            }
            json.append(helpers::QuoteForJSON(path, options.ASCIIOnly));
            json.append(config::MaybeRemoveWhitespace(options.MetafileFormatData, ": "));
            json.append(result.json_metadata_chunk);
        }

        json.append(config::MaybeRemoveWhitespace(options.MetafileFormatData, "\n  }\n}"));
        json.push_back('\n');
        return json;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Bundle::Compile.
    //
    // The central entry point of the compile phase: turns the scanned module
    // graph and resolved options into the final set of output files, folding
    // inline script and style chunks into their HTML containers and producing
    // the optional metafile alongside.

    // Executes the compile phase end to end: copies the options for exclusive
    // mutation, normalizes generated runtime references, discovers all
    // reachable modules, begins source-map computation, links each entry point
    // group, folds inline script and style chunk text back into their HTML
    // output, writes the metafile JSON when enabled, and finally verifies that
    // no output path collides with an input file or with another output that
    // carries different content.
    //
    // Input : The diagnostic log, a phase timer, and a mutable cache shared
    //         across bundles for locally-scoped names.
    // Output: The finished output file list plus the metafile JSON (empty when
    //         the run was cancelled or no metafile was requested).
    std::pair<std::vector<graph::OutputFile>, std::string> Bundle::Compile(
        Log& log,
        helpers::Timer& timer,
        std::unordered_map<std::string, bool>& mangle_cache)
    {
        timer.Begin("Compile phase");

        if (options.CancelFlagData != nullptr && options.CancelFlagData->DidCancel()) {
            timer.End("Compile phase");
            return {{}, ""};
        }

        config::Options compile_options = options;

        std::unordered_map<std::string, bool> css_used_local_names;
        compile_options.ExclusiveMangleCacheUpdate = [&](config::MangleCacheCallback cb) {
            cb(mangle_cache, css_used_local_names);
        };

        std::vector<graph::InputFile> input_files;
        input_files.reserve(files.size());
        for (const auto& f : files) {
            input_files.push_back(f.input_file);
        }

        for (auto& input_file : input_files) {
            if (auto* js_repr = std::get_if<std::shared_ptr<graph::JSRepr>>(&input_file.repr)) {
                if (!*js_repr) continue;
                for (auto& record : (*js_repr)->ImportRecords()) {
                    if (record.source_index.IsValid() &&
                        record.source_index.GetIndex() == javascript::kRuntimeSourceIndex) {
                        record.source_index = compiler::Index32::Make(javascript::kSourceIndex);
                    }
                }
            }
        }

        std::vector<uint32_t> all_reachable_files = FindReachableFiles(input_files, entry_points);

        timer.Begin("Spawn source map tasks");
        DataForSourceMapsFn data_for_source_maps =
            ComputeDataForSourceMapsInParallel(compile_options, files, all_reachable_files);
        timer.End("Spawn source map tasks");

        std::unordered_set<uint32_t> separate_css_entry_sources;
        for (const graph::EntryPoint& ep : entry_points) {
            if (ep.source_index < input_files.size() &&
                std::holds_alternative<std::shared_ptr<graph::CSSRepr>>(
                    input_files[ep.source_index].repr))
            {
                separate_css_entry_sources.insert(ep.source_index);
            }
        }

        linker::LinkResult link_result;
        if (compile_options.CodeSplitting || entry_points.size() <= 1) {
            link_result = linker::Link(&compile_options, timer, log, *fs, *res,
                input_files, entry_points, unique_key_prefix,
                all_reachable_files, data_for_source_maps,
                separate_css_entry_sources);
        } else {
            std::unordered_set<uint32_t> inline_js_sources;
            for (const auto& [html_index, info] : html_inline) {
                for (const graph::HtmlInlineSegment& seg : info.js_segments) {
                    inline_js_sources.insert(seg.chunk_source_index);
                }
            }
            link_result.output_files.reserve(entry_points.size() * 2);
            size_t meta_offset = 0;
            for (const auto& ep : entry_points) {
                std::vector<graph::EntryPoint> single_ep = {ep};
                std::vector<uint32_t> ep_reachable = FindReachableFiles(input_files, single_ep);
                config::Options* link_options = &compile_options;
                config::Options inline_options;
                if (inline_js_sources.count(ep.source_index)) {
                    inline_options = compile_options;
                    inline_options.TreeShaking = false;
                    link_options = &inline_options;
                }
                auto group = linker::Link(link_options, timer, log, *fs, *res,
                    input_files, single_ep, unique_key_prefix,
                    ep_reachable, data_for_source_maps,
                    separate_css_entry_sources);
                link_result.output_files.insert(link_result.output_files.end(),
                    std::make_move_iterator(group.output_files.begin()),
                    std::make_move_iterator(group.output_files.end()));
                for (auto& meta : group.chunk_metadata) {
                    for (auto& imp : meta.cross_chunk_imports) {
                        imp += static_cast<uint32_t>(meta_offset);
                    }
                    link_result.chunk_metadata.push_back(std::move(meta));
                }
                meta_offset += group.chunk_metadata.size();
            }
        }
        std::vector<graph::OutputFile>& output_files = link_result.output_files;

        if (!html_inline.empty()) {
            timer.Begin("Generate HTML outputs");

            std::unordered_map<uint32_t, uint32_t> inline_source_to_meta;
            for (uint32_t i = 0; i < entry_points.size(); i++) {
                uint32_t source_index = entry_points[i].source_index;
                for (const auto& [html_index, info] : html_inline) {
                    for (const graph::HtmlInlineSegment& seg : info.js_segments) {
                        if (seg.chunk_source_index == source_index) {
                            inline_source_to_meta[source_index] = i;
                        }
                    }
                    for (const graph::HtmlInlineSegment& seg : info.css_segments) {
                        if (seg.chunk_source_index == source_index) {
                            inline_source_to_meta[source_index] = i;
                        }
                    }
                }
            }

            std::unordered_map<std::string, uint32_t> inline_abs_to_source;
            for (const auto& [source_index, meta_index] : inline_source_to_meta) {
                const graph::InputFile& file = files[source_index].input_file;
                const char* std_ext =
                    std::holds_alternative<std::shared_ptr<graph::JSRepr>>(file.repr)
                        ? compile_options.OutputExtensionJS.c_str()
                        : compile_options.OutputExtensionCSS.c_str();
                std::string rel = ComputeEntryOutputRelPath(compile_options, file, *fs,
                    entry_points[meta_index].output_path, std_ext);
                std::string abs = CanonicalPathForComparison(
                    fs->Join({compile_options.AbsOutputDir, rel}));
                inline_abs_to_source[abs] = source_index;
            }

            std::unordered_map<uint32_t, std::string> chunk_texts;
            for (auto it = output_files.begin(); it != output_files.end();) {
                auto found = inline_abs_to_source.find(
                    CanonicalPathForComparison(it->abs_path));
                if (found != inline_abs_to_source.end()) {
                    const auto& bytes = it->contents;
                    chunk_texts[found->second] =
                        std::string(bytes.begin(), bytes.end());
                    it = output_files.erase(it);
                } else {
                    ++it;
                }
            }

            std::unordered_map<uint32_t, const graph::EntryPoint*> source_to_entry;
            for (const graph::EntryPoint& ep : entry_points) {
                source_to_entry[ep.source_index] = &ep;
            }

            // Resolves the output location, relative to the output directory, of a
            // given entry source: copied asset modules read their recorded
            // output path while ordinary entries rebuild the location from the
            // path templates, normalizing the result for relative-link math.
            auto entry_output_rel_path = [&](uint32_t source_index) -> std::string {
                const graph::InputFile& file = files[source_index].input_file;
                const graph::EntryPoint* entry = source_to_entry[source_index];
                if (std::holds_alternative<std::shared_ptr<graph::CopyRepr>>(file.repr)) {
                    if (file.additional_files.empty()) {
                        return {};
                    }
                    const std::string& abs = file.additional_files[0].abs_path;
                    std::optional<std::string> rel =
                        fs->Rel(compile_options.AbsOutputDir, abs);
                    if (rel.has_value()) {
                        return NormalizeRelativeOutputPath(*rel);
                    }
                    size_t pos = abs.find(compile_options.AbsOutputDir);
                    if (pos != std::string::npos) {
                        return NormalizeRelativeOutputPath(abs.substr(
                            pos + compile_options.AbsOutputDir.size()));
                    }
                    return NormalizeRelativeOutputPath(abs);
                }
                if (entry == nullptr) {
                    return {};
                }
                const char* std_ext =
                    std::holds_alternative<std::shared_ptr<graph::JSRepr>>(file.repr)
                        ? compile_options.OutputExtensionJS.c_str()
                        : compile_options.OutputExtensionCSS.c_str();
                return NormalizeRelativeOutputPath(ComputeEntryOutputRelPath(
                    compile_options, file, *fs, entry->output_path, std_ext));
            };

            std::vector<HTMLCssContent> css_contents;
            for (const auto& [html_index, info] : html_inline) {
                for (const graph::HtmlInlineSegment& seg : info.css_segments) {
                    auto text_iter = chunk_texts.find(seg.chunk_source_index);
                    if (text_iter == chunk_texts.end()) {
                        continue;
                    }
                    HTMLCssContent css;
                    css.css_output_rel_path =
                        entry_output_rel_path(seg.chunk_source_index);
                    css.contents = text_iter->second;
                    css_contents.push_back(std::move(css));
                }
            }
            for (const graph::OutputFile& out : output_files) {
                std::optional<std::string> rel =
                    fs->Rel(compile_options.AbsOutputDir, out.abs_path);
                if (!rel.has_value()) {
                    continue;
                }
                std::string rel_path = NormalizeRelativeOutputPath(*rel);
                if (rel_path.size() < compile_options.OutputExtensionCSS.size() ||
                    rel_path.compare(
                        rel_path.size() - compile_options.OutputExtensionCSS.size(),
                        compile_options.OutputExtensionCSS.size(),
                        compile_options.OutputExtensionCSS) != 0) {
                    continue;
                }
                HTMLCssContent css;
                css.css_output_rel_path = std::move(rel_path);
                css.contents.assign(out.contents.begin(), out.contents.end());
                css_contents.push_back(std::move(css));
            }

            std::unordered_map<std::string, std::string> sri_rel_contents;
            if (compile_options.SRI) {
                for (const graph::OutputFile& out : output_files) {
                    std::optional<std::string> rel =
                        fs->Rel(compile_options.AbsOutputDir, out.abs_path);
                    if (!rel.has_value()) {
                        continue;
                    }
                    sri_rel_contents[NormalizeRelativeOutputPath(*rel)] =
                        std::string(out.contents.begin(), out.contents.end());
                }
            }

            std::unordered_map<uint32_t,
                std::unordered_map<uint32_t, std::string>>
                html_record_rel_paths;
            for (const auto& [html_index, info] : html_inline) {
                if (html_index >= files.size()) {
                    continue;
                }
                auto* html_repr_ptr = std::get_if<std::shared_ptr<graph::HTMLRepr>>(
                    &files[html_index].input_file.repr);
                if (html_repr_ptr == nullptr || *html_repr_ptr == nullptr) {
                    continue;
                }
                auto& rel_paths = html_record_rel_paths[html_index];
                for (const compiler::ImportRecord& record :
                     (*html_repr_ptr)->ImportRecords())
                {
                    if (!record.source_index.IsValid()) {
                        continue;
                    }
                    uint32_t target = record.source_index.GetIndex();
                    std::string rel = entry_output_rel_path(target);
                    if (!rel.empty()) {
                        rel_paths[target] = std::move(rel);
                    }
                }
            }

            std::unordered_multimap<uint32_t, const linker::ChunkMetadata*>
                source_to_chunk;
            for (const linker::ChunkMetadata& meta : link_result.chunk_metadata) {
                if (meta.source_index != UINT32_MAX) {
                    source_to_chunk.emplace(meta.source_index, &meta);
                }
            }

            for (uint32_t i = 0; i < entry_points.size(); i++) {
                const auto& ep = entry_points[i];
                if (ep.source_index >= files.size()) {
                    continue;
                }
                const graph::InputFile& html_file = files[ep.source_index].input_file;
                auto* html_repr_ptr = std::get_if<std::shared_ptr<graph::HTMLRepr>>(
                    &html_file.repr);
                if (html_repr_ptr == nullptr || *html_repr_ptr == nullptr) {
                    continue;
                }

                auto [rel_dir, rel_base] = PathRelativeToOutbase(html_file,
                    compile_options, *fs, false, ep.output_path);
                std::string html_base = rel_base;
                size_t ext_at = html_base.rfind('.');
                if (ext_at != std::string::npos) {
                    html_base = html_base.substr(0, ext_at);
                }
                std::string html_output_rel_path = NormalizeRelativeOutputPath(
                    rel_dir + html_base + ".html");
                std::string html_abs_path = fs->Join(
                    {compile_options.AbsOutputDir, rel_dir + html_base + ".html"});

                const graph::HtmlInlineInfo& info = html_inline.at(ep.source_index);
                const auto& record_rel_iter = html_record_rel_paths.find(ep.source_index);
                static const std::unordered_map<uint32_t, std::string> kEmptyRelPaths;
                const auto& record_rel_paths =
                    record_rel_iter != html_record_rel_paths.end()
                        ? record_rel_iter->second
                        : kEmptyRelPaths;

                std::vector<HTMLScriptInfo> script_info;
                const auto& html_records = (*html_repr_ptr)->ImportRecords();
                script_info.resize(html_records.size());
                for (size_t r = 0; r < html_records.size(); r++) {
                    const compiler::ImportRecord& record = html_records[r];
                    if (!record.source_index.IsValid()) {
                        continue;
                    }
                    const html::ImportRecordOrigin& origin =
                        r < (*html_repr_ptr)->ast.record_origins.size()
                            ? (*html_repr_ptr)->ast.record_origins[r]
                            : html::ImportRecordOrigin{};
                    if (origin.element == nullptr ||
                        origin.element->tag_name != "script") {
                        continue;
                    }
                    uint32_t source = record.source_index.GetIndex();
                    auto range = source_to_chunk.equal_range(source);
                    const linker::ChunkMetadata* chosen = nullptr;
                    for (auto it = range.first; it != range.second; ++it) {
                        if (it->second->is_entry_point) {
                            chosen = it->second;
                            break;
                        }
                        if (chosen == nullptr) {
                            chosen = it->second;
                        }
                    }
                    if (chosen == nullptr) {
                        continue;
                    }
                    HTMLScriptInfo& sinfo = script_info[r];
                    sinfo.is_module = IsHtmlModuleScript(*origin.element);

                    if (chosen->is_entry_point && !chosen->is_executable &&
                        chosen->cross_chunk_imports.size() == 1) {
                        const uint32_t sole_dep = chosen->cross_chunk_imports[0];
                        if (sole_dep < link_result.chunk_metadata.size()) {
                            chosen = &link_result.chunk_metadata[sole_dep];
                        }
                    }
                    sinfo.output_rel_path = chosen->final_rel_path;
                    for (uint32_t dep_chunk : chosen->cross_chunk_imports) {
                        if (dep_chunk < link_result.chunk_metadata.size()) {
                            sinfo.dependency_preloads.push_back(
                                link_result.chunk_metadata[dep_chunk].final_rel_path);
                        }
                    }
                    sinfo.associated_css_rel_path = chosen->associated_css_rel_path;
                }

                bool import_maps_reordered = false;
                logger::Range import_map_reordered_range;
                std::vector<std::string> transform_warnings;
                HTMLOutputContext html_ctx;
                html_ctx.html_file = &html_file;
                html_ctx.inline_info = &info;
                html_ctx.chunk_texts = &chunk_texts;
                html_ctx.record_source_to_rel_path = &record_rel_paths;
                html_ctx.html_output_rel_path = html_output_rel_path;
                html_ctx.script_info = &script_info;
                html_ctx.pretty_print = compile_options.PrettyPrint;
                html_ctx.minify = compile_options.MinifyHtml;
                html_ctx.public_path = compile_options.PublicPath;
                html_ctx.define_map = &compile_options.DefineMap;
                html_ctx.csp_nonce = compile_options.CspNonce;
                html_ctx.resource_hints = compile_options.ResourceHints;
                html_ctx.css_contents = &css_contents;
                html_ctx.sri_algorithm =
                    compile_options.SRI ? compile_options.SRIAlgorithm
                                        : std::string{};
                html_ctx.sri_rel_contents = &sri_rel_contents;
                html_ctx.css_loading = compile_options.CSSLoadingStrategyData;
                html_ctx.import_maps_reordered = &import_maps_reordered;
                html_ctx.import_map_reordered_range = &import_map_reordered_range;
                html_ctx.plugins = &compile_options.Plugins;
                html_ctx.transform_warnings = &transform_warnings;
                std::string output = GenerateHTMLOutput(html_ctx);
                if (import_maps_reordered) {
                    const logger::PrettyPaths& pretty = html_file.source.pretty_paths;
                    const std::string& filename =
                        pretty.rel.empty() ? pretty.abs : pretty.rel;
                    if (import_map_reordered_range.len > 0) {
                        logger::LineColumnTracker tracker(&html_file.source);
                        log.AddID(logger::MsgID::kHTML_ImportMapReordered,
                                  logger::MsgKind::kWarning, &tracker,
                                  import_map_reordered_range,
                                  guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_ImportMapReordered, filename));
                    } else {
                        log.AddID(logger::MsgID::kHTML_ImportMapReordered,
                                  logger::MsgKind::kWarning, nullptr, logger::Range{},
                                  guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_ImportMapReordered, filename));
                    }
                }
                for (const std::string& message : transform_warnings) {
                    log.AddID(logger::MsgID::kNone, logger::MsgKind::kWarning,
                              nullptr, logger::Range{}, message);
                }

                graph::OutputFile html_output;
                html_output.abs_path = html_abs_path;
                html_output.contents.assign(output.begin(), output.end());
                output_files.push_back(std::move(html_output));
            }

            timer.End("Generate HTML outputs");
        }

        std::string metafile_json;
        if (compile_options.NeedsMetafile) {
            timer.Begin("Generate metadata JSON");
            metafile_json = GenerateMetadataJSON(compile_options, *fs, files, output_files, all_reachable_files);
            timer.End("Generate metadata JSON");
        }

        if (!compile_options.WriteToStdout) {
            if (!compile_options.AllowOverwrite) {
                std::unordered_map<std::string, uint32_t> source_abs_paths;
                for (uint32_t source_index : all_reachable_files) {
                    if (source_index >= files.size()) {
                        continue;
                    }
                    const auto& key_path = files[source_index].input_file.source.key_path;
                    if (key_path.namespace_ == "file") {
                        std::string abs_path_key = CanonicalPathForComparison(key_path.text);
                        source_abs_paths[abs_path_key] = source_index;
                    }
                }
                for (const auto& output_file : output_files) {
                    std::string abs_path_key = CanonicalPathForComparison(output_file.abs_path);
                    auto it = source_abs_paths.find(abs_path_key);
                    if (it != source_abs_paths.end()) {
                        uint32_t source_index = it->second;
                        std::string hint = " (use \"--allow-overwrite\" to allow this)";
                        log.AddError(nullptr, Range{},
                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_RefusingToOverwriteInput,
                                files[source_index].input_file.source.pretty_paths.Select(
                                    compile_options.LogPathStyle), hint));
                    }
                }
            }

            {
                std::unordered_map<std::string, size_t> output_file_map;
                size_t end = 0;
                for (auto& output_file : output_files) {
                    std::string abs_path_key = CanonicalPathForComparison(output_file.abs_path);
                    auto it = output_file_map.find(abs_path_key);
                    if (it == output_file_map.end()) {
                        output_file_map[abs_path_key] = end;
                        output_files[end] = std::move(output_file);
                        end++;
                        continue;
                    }

                    const auto& existing = output_files[it->second];
                    if (existing.contents == output_file.contents) {
                        continue;
                    }

                    std::string output_path = output_file.abs_path;
                    auto rel = fs->Rel(fs->Cwd(), output_path);
                    if (rel.has_value()) {
                        output_path = *rel;
                    }
                    log.AddError(nullptr, Range{},
                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_TwoOutputFilesSamePath, output_path));
                }
                output_files.resize(end);
            }
        }

        timer.End("Compile phase");
        return {std::move(output_files), std::move(metafile_json)};
    }

}
