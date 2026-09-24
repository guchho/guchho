////////////////////////////////////////////////////////////////////////////////
// Bundler scan-phase parsing
//
// Parses each source file into its AST and representation, runs on-load plugins,
// resolves the file's import records (cached per file), and attempts to attach
// the linked source map from a trailing sourceMappingURL comment.
////////////////////////////////////////////////////////////////////////////////

#include "guchho/bundler.hpp"


namespace guchho::bundler {


    std::string SanitizeFilePathForVirtualModulePath(const std::string& path);
    std::string LowestCommonAncestorDirectory(
        Fs& fs, const std::vector<graph::EntryPoint>& entry_points);

    // Case-folds a Unicode code point, but only for the ASCII range. Used for the
    // case-insensitive Windows path comparison below.
    //
    // Input : U'A'.
    // Output: U'a'.
    static char32_t ToASCIILowerRune(char32_t rune) {
        return (rune >= U'A' && rune <= U'Z') ? rune + (U'a' - U'A') : rune;
    }

    // Turns an arbitrary path into a safe file name usable for virtual modules by
    // dropping forbidden characters, collapsing invalid runs into '_' and encoding
    // any remaining rune as UTF-8.
    //
    // Input : "C:\foo\bar?.js".
    // Output: a sanitized name (never empty; "_" when nothing survives).
    std::string SanitizeFilePathForVirtualModulePath(const std::string& path) {
        std::string sb;
        bool needs_gap = false;
        size_t a       = 0;
        while (a < path.size()) {
            auto [rune, width] = helpers::DecodeRuneInString(
                std::string_view(path).substr(a));
            a += static_cast<size_t>(width);
            switch (rune) {
                case 0:
                    break;

                case U'<':
                case U'>':
                case U':':
                case U'"':
                case U'|':
                case U'?':
                case U'*':
                    break;

                default: {
                    if (rune < 0x20) {
                        break;
                    }

                    if (needs_gap) {
                        sb.push_back('_');
                        needs_gap = false;
                    }

                    if (rune < 0x80) {
                        sb.push_back(static_cast<char>(rune));
                    } else if (rune < 0x800) {
                        sb.push_back(static_cast<char>(0xC0 | (rune >> 6)));
                        sb.push_back(static_cast<char>(0x80 | (rune & 0x3F)));
                    } else if (rune < 0x10000) {
                        sb.push_back(static_cast<char>(0xE0 | (rune >> 12)));
                        sb.push_back(static_cast<char>(0x80 | ((rune >> 6) & 0x3F)));
                        sb.push_back(static_cast<char>(0x80 | (rune & 0x3F)));
                    } else {
                        sb.push_back(static_cast<char>(0xF0 | (rune >> 18)));
                        sb.push_back(static_cast<char>(0x80 | ((rune >> 12) & 0x3F)));
                        sb.push_back(static_cast<char>(0x80 | ((rune >> 6) & 0x3F)));
                        sb.push_back(static_cast<char>(0x80 | (rune & 0x3F)));
                    }
                    continue;
                }
            }

            if (!sb.empty()) {
                needs_gap = true;
            }
        }

        if (sb.empty()) {
            return "_";
        }

        return sb;
    }

    // Computes the deepest directory that contains every auto-generated output
    // path, ignoring explicitly-specified output paths.
    //
    // Input : the entry point list.
    // Output: the lowest common directory ("" when there is none).
    std::string LowestCommonAncestorDirectory(
        Fs& fs, const std::vector<graph::EntryPoint>& entry_points)
    {
        std::vector<std::string> abs_paths;
        abs_paths.reserve(entry_points.size());
        for (const graph::EntryPoint& entry_point : entry_points) {
            if (entry_point.output_path_was_auto_generated) {
                abs_paths.push_back(entry_point.output_path);
            }
        }

        if (abs_paths.empty()) {
            return "";
        }

        std::string lowest_abs_dir = fs.Dir(abs_paths[0]);

        for (size_t i = 1; i < abs_paths.size(); i++) {
            const std::string& abs_dir     = abs_paths[i];
            const std::string& lowest_dir  = lowest_abs_dir;
            size_t last_slash              = 0;
            size_t a                       = 0;
            size_t b                       = 0;

            while (true) {
                auto [rune_a, width_a] =
                    helpers::DecodeRuneInString(std::string_view(abs_dir).substr(a));
                auto [rune_b, width_b] = helpers::DecodeRuneInString(
                    std::string_view(lowest_dir).substr(b));
                bool boundary_a =
                    width_a == 0 || rune_a == U'/' || rune_a == U'\\';
                bool boundary_b =
                    width_b == 0 || rune_b == U'/' || rune_b == U'\\';

                if (boundary_a && boundary_b) {
                    if (width_a == 0 || width_b == 0) {
                        lowest_abs_dir = abs_dir.substr(0, a);
                        break;
                    } else {
                        last_slash = a;
                    }
                } else if (boundary_a != boundary_b ||
                           ToASCIILowerRune(rune_a) != ToASCIILowerRune(rune_b))
                {
                    if (last_slash < abs_dir.size() &&
                        abs_dir.substr(0, last_slash).find_first_of("/\\") ==
                            std::string::npos)
                    {
                        last_slash++;
                    }

                    lowest_abs_dir = abs_dir.substr(0, last_slash);
                    break;
                }

                a += static_cast<size_t>(width_a);
                b += static_cast<size_t>(width_b);
            }
        }

        return lowest_abs_dir;
    }

    // Picks the MIME type for a data URL: the type for the file extension if there
    // is one, otherwise a content-detection fallback. "; " separators are collapsed
    // to ";".
    //
    // Input : ".png" extension and the file's source.
    // Output: e.g. "image/png;charset=utf-8".
    static std::string GuessMimeType(const std::string& extension, const Source& source) {
        std::string mime_type(helpers::MimeTypeByExtension(extension));
        if (mime_type.empty()) {
            mime_type = helpers::DetectContentType(source.contents);
        }

        size_t pos = 0;
        while ((pos = mime_type.find("; ", pos)) != std::string::npos) {
            mime_type.replace(pos, 2, ";");
        }
        return mime_type;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Plugin plumbing
    //
    // Helpers for running onLoad/onResolve plugin callbacks and funneling plugin
    // error/warning messages into the build log.
    ////////////////////////////////////////////////////////////////////////////////

    // Normalizes a message location's namespace and turns its absolute path into
    // pretty paths so it can be printed.
    //
    // Input : a message location, possibly null.
    // Output: the location is updated in place.
    static void SanitizeLocation(Fs& fs, logger::MsgLocation* loc) {
        if (loc != nullptr) {
            if (loc->namespace_.empty()) {
                loc->namespace_ = "file";
            }
            if (!loc->file.abs.empty()) {
                loc->file = resolver::MakePrettyPaths(
                    fs, Path{.text = loc->file.abs, .namespace_ = loc->namespace_});
            }
        }
    }

    // Logs the messages produced by a plugin callback, filling in the default
    // plugin name and location when missing. Also logs a plugin's thrown error.
    //
    // Input : the messages, thrown error, and the triggering import range.
    // Output: true when at least one error was logged.
    bool LogPluginMessages(
        Fs& fs,
        Log log,
        const std::string& name,
        std::vector<Msg> msgs,
        const std::string& thrown_error,
        const Source* import_source,
        Range import_path_range)
    {
        bool did_log_error = false;
        logger::LineColumnTracker tracker(import_source);

        for (auto& msg : msgs) {
            if (msg.plugin_name.empty()) {
                msg.plugin_name = name;
            }
            if (msg.kind == logger::MsgKind::kError) {
                did_log_error = true;
            }

            for (auto& note : msg.notes) {
                SanitizeLocation(fs, note.location.get());
            }
            if (msg.data.location == nullptr) {
                msg.data.location = tracker.MsgLocationOrNil(import_path_range);
            } else {
                SanitizeLocation(fs, msg.data.location.get());
                if (import_source != nullptr && msg.data.location->file.abs.empty() &&
                    msg.data.location->file.rel.empty())
                {
                    msg.data.location->file = import_source->pretty_paths;
                }
                if (import_source != nullptr) {
                    msg.notes.push_back(tracker.MakeMsgData(import_path_range,
                        "The plugin \"" + name + "\" was triggered by this import"));
                }
            }

            log.add_msg(msg);
        }

        if (!thrown_error.empty()) {
            did_log_error = true;
            Msg thrown_msg;
            thrown_msg.plugin_name = name;
            thrown_msg.kind = logger::MsgKind::kError;
            thrown_msg.data.text = thrown_error;
            thrown_msg.data.location = tracker.MsgLocationOrNil(import_path_range);
            log.add_msg(thrown_msg);
        }

        return did_log_error;
    }

    // Reports an error when an import uses an explicit phase (".defer" or
    // ".source"). These are only allowed for ESM output and non-external imports.
    //
    // Input : the phase, external flag and output format.
    // Output: logs an error when the phase is unsupported.
    static void ReportExplicitPhaseImport(
        Log& log,
        logger::LineColumnTracker* tracker,
        Range r,
        compiler::ImportPhase phase,
        bool is_external,
        config::Format format)
    {
        std::string phase_text;
        switch (phase) {
            case compiler::ImportPhase::kDefer: phase_text = "deferred"; break;
            case compiler::ImportPhase::kSource: phase_text = "source phase"; break;
            default: return;
        }
        if (format != config::Format::kESModule) {
            log.AddError(tracker, r,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_BundlingPhaseImportsNotSupported, phase_text,
                    std::string(config::FormatToString(format))));
        } else if (!is_external) {
            log.AddError(tracker, r,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_BundlingPhaseImportsNotSupportedUnlessExternal, phase_text));
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Source map comment extraction
    //
    // Reads the source map referenced by a trailing "//# sourceMappingURL="
    // comment, supporting data: URLs, file: URLs and relative paths.
    ////////////////////////////////////////////////////////////////////////////////

    // Resolves a "sourceMappingURL" comment to the source map's path and contents.
    //
    // Data and absolute/relative file URLs are supported; unsupported schemes and
    // hosts produce warnings, and a missing map file is reported at debug level.
    //
    // Input : the comment span and resolve directory.
    // Output: the map's path and its contents, or nullopt on failure.
    static std::optional<std::pair<Path, std::string>> ExtractSourceMapFromComment(
        Log& log,
        Fs& fs,
        cache::FSCache& fs_cache,
        const Source& source,
        logger::LineColumnTracker* tracker,
        const javascript::Span& comment,
        const std::string& abs_resolve_dir,
        logger::PathStyle log_path_style)
    {
        std::optional<helpers::DataURL> data_url = helpers::ParseDataURL(comment.text);
        if (data_url.has_value()) {
            std::string err;
            std::optional<std::string> contents = data_url->DecodeData(err);
            if (!contents.has_value()) {
                log.AddID(logger::MsgID::kSourceMap_UnsupportedSourceMapComment, logger::MsgKind::kWarning,
                    tracker, comment.range,
                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_UnsupportedSourceMapCommentWithError, err));
                return std::nullopt;
            }
            Path path = source.key_path;
            path.ignored_suffix = "#sourceMappingURL";
            return std::make_pair(std::move(path), std::move(*contents));
        }

        std::string abs_path;
        std::optional<helpers::URL> comment_url = helpers::ParseURL(comment.text);
        if (!comment_url.has_value()) {
            log.AddID(logger::MsgID::kSourceMap_UnsupportedSourceMapComment, logger::MsgKind::kWarning,
                tracker, comment.range,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_UnsupportedSourceMapComment));
            return std::nullopt;
        }

        if (!comment_url->scheme.empty() && comment_url->scheme != "file") {
            log.AddID(logger::MsgID::kSourceMap_UnsupportedSourceMapComment, logger::MsgKind::kDebug,
                tracker, comment.range,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_UnsupportedSourceMapCommentScheme, comment_url->scheme));
            return std::nullopt;
        }

        if (!comment_url->host.empty() && comment_url->host != "localhost") {
            log.AddID(logger::MsgID::kSourceMap_UnsupportedSourceMapComment, logger::MsgKind::kWarning,
                tracker, comment.range,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_UnsupportedSourceMapCommentHost, comment_url->host));
            return std::nullopt;
        }

        if (helpers::IsFileURL(comment_url->scheme, comment_url->host, comment_url->path)) {
            abs_path = helpers::FilePathFromFileURL(comment_url->path, fs.Cwd());
        } else if (abs_resolve_dir.empty()) {
            log.AddID(logger::MsgID::kSourceMap_UnsupportedSourceMapComment, logger::MsgKind::kDebug,
                tracker, comment.range,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_UnsupportedSourceMapCommentResolveDir));
            return std::nullopt;
        } else {
            helpers::URL abs_resolve_url = helpers::ParseURL(helpers::FileURLFromFilePath(abs_resolve_dir))
                                               .value_or(helpers::URL{});
            if (!abs_resolve_url.path.empty() && abs_resolve_url.path.back() != '/') {
                abs_resolve_url.path += "/";
            }
            abs_path = helpers::FilePathFromFileURL(abs_resolve_url.ResolveReference(*comment_url).path,
                fs.Cwd());
        }

        Path path{.text = abs_path, .namespace_ = "file"};
        auto contents_result = fs_cache.ReadFile(fs, abs_path);
        if (contents_result.canonical_error == std::errc::no_such_file_or_directory) {
            log.AddID(logger::MsgID::kSourceMap_MissingSourceMap, logger::MsgKind::kDebug, tracker,
                comment.range,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotReadFile_2, abs_path));
            return std::nullopt;
        }
        if (!contents_result.Ok()) {
            PrettyPaths pretty_paths = resolver::MakePrettyPaths(fs, path);
            log.AddID(logger::MsgID::kSourceMap_MissingSourceMap, logger::MsgKind::kWarning, tracker,
                comment.range,
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotReadFileWithError_2,
                    pretty_paths.Select(log_path_style), contents_result.original_error));
            return std::nullopt;
        }
        return std::make_pair(std::move(path), std::move(contents_result.value));
    }

    ////////////////////////////////////////////////////////////////////////////////
    // parseFile
    //
    // Parses a single source file. See ParseFile below for the full pipeline.
    ////////////////////////////////////////////////////////////////////////////////

    // Parses one source file and sends the result down the results channel.
    //
    // The pipeline is: pick the source contents (stdin or on-load plugins),
    // determine the loader (explicit, extension or import attribute), parse with
    // the matching parser (JS/JSX/TS/TSX, CSS, HTML, JSON, text/base64/binary data
    // modules), resolve every import record on the parse thread (with a per-file
    // cache and plugin support), and finally attach the source map referenced by a
    // sourceMappingURL comment. Injectable exports are sent on the inject channel
    // first to avoid deadlock.
    //
    // Input : ParseArgs describing the source and caches.
    // Output: a ParseResult sent on "results" (and possibly an injected file).
    void ParseFile(ParseArgs args) {
        std::string path_for_identifier_name = args.key_path.text;

        auto relative = args.fs->Rel(args.options.AbsOutputBase, path_for_identifier_name);
        if (relative.has_value()) {
            std::string rel = *relative;
            for (;;) {
                std::string next = rel;
                constexpr std::string_view dotdot_slash = "../";
                constexpr std::string_view dotdot_backslash = "..\\";
                if (next.rfind(dotdot_slash, 0) == 0) {
                    next = next.substr(dotdot_slash.size());
                } else if (next.rfind(dotdot_backslash, 0) == 0) {
                    next = next.substr(dotdot_backslash.size());
                }
                if (rel == next) {
                    break;
                }
                rel = next;
            }
            path_for_identifier_name = rel;
        }

        Source source;
        source.index = args.source_index;
        source.key_path = args.key_path;
        source.pretty_paths = args.pretty_paths;
        source.identifier_name = javascript::GenerateNonUniqueNameFromPath(path_for_identifier_name);

        config::Loader loader{config::Loader::kNone};
        std::string abs_resolve_dir;
        std::string plugin_name;
        std::any plugin_data;

        if (args.options.Stdin != nullptr) {
            source.contents = args.options.Stdin->Contents;
            loader = args.options.Stdin->Ldr;
            if (loader == config::Loader::kNone) {
                loader = config::Loader::kJS;
            }
            abs_resolve_dir = args.options.Stdin->AbsResolveDir;
        } else {
            bool ok = false;
            LoaderPluginResult result = RunOnLoadPluginsImpl(
                args.options.Plugins,
                *args.fs,
                args.caches->fs_cache,
                args.log,
                source,
                &args.import_source_copy,
                args.import_path_range,
                args.plugin_data,
                args.options.WatchMode,
                args.options.LogPathStyle,
                ok);
            if (!ok) {
                if (args.inject) {
                    config::InjectedFile injected;
                    injected.SourceData = source;
                    args.inject->Send(std::move(injected));
                }
                args.results->Send(ParseResult{});
                return;
            }
            loader = result.loader;
            abs_resolve_dir = result.abs_resolve_dir;
            plugin_name = result.plugin_name;
            plugin_data = result.plugin_data;
        }

        std::string dir, base, ext;
        logger::PlatformIndependentPathDirBaseExt(source.key_path.text, dir, base, ext);

        if (loader == config::Loader::kDefault) {
            loader = config::LoaderFromFileExtension(args.options.ExtensionToLoader, base + ext);
        }

        if (loader != config::Loader::kCopy && plugin_name.empty()) {
            for (const auto& attr : source.key_path.import_attributes.DecodeIntoArray()) {
                std::string error_text;
                javascript::KeyOrValue error_range{};

                if (attr.key != "type") {
                    error_text = logger::FormatMsg(logger::MsgCat::kBundler_ImportingWithAttr, attr.key);
                    error_range = javascript::KeyOrValue::kKeyRange;
                } else if (attr.value == "json") {
                    loader = config::Loader::kWithTypeJSON;
                    continue;
                } else if (attr.value == "bytes") {
                    loader = config::Loader::kBinary;
                    continue;
                } else if (attr.value == "text") {
                    loader = config::Loader::kText;
                    continue;
                } else {
                    error_text = logger::FormatMsg(logger::MsgCat::kBundler_ImportingWithTypeAttr, attr.value);
                    error_range = javascript::KeyOrValue::kValueRange;
                }

                Range r = args.import_path_range;
                if (args.import_with != nullptr) {
                    const compiler::AssertOrWithEntry* entry =
                        compiler::FindAssertOrWithEntry(args.import_with->entries, attr.key);
                    if (entry != nullptr) {
                        r = javascript::RangeOfImportAssertOrWith(args.import_source_copy, *entry, error_range);
                    }
                }
                logger::LineColumnTracker tracker(&args.import_source_copy);
                args.log.AddError(&tracker, r, error_text);
                if (args.inject) {
                    config::InjectedFile injected;
                    injected.SourceData = source;
                    args.inject->Send(std::move(injected));
                }
                args.results->Send(ParseResult{});
                return;
            }
        }

        if (loader == config::Loader::kEmpty) {
            source.contents.clear();
        }

        ParseResult result;
        result.file.input_file.source = source;
        result.file.input_file.loader = loader;
        result.file.input_file.side_effects = args.side_effects;
        result.file.plugin_data = plugin_data;

        try {
            switch (loader) {
                case config::Loader::kJS:
                case config::Loader::kEmpty: {
                    auto [ast, ok] = args.caches->js_cache.Parse(args.log, source,
                        javascript::OptionsFromConfig(&args.options));
                    if (ast.parts.size() <= 1) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsEmptyAST;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = ok;
                    break;
                }

                case config::Loader::kJSX: {
                    args.options.JSX.Parse = true;
                    auto [ast, ok] = args.caches->js_cache.Parse(args.log, source,
                        javascript::OptionsFromConfig(&args.options));
                    if (ast.parts.size() <= 1) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsEmptyAST;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = ok;
                    break;
                }

                case config::Loader::kTS:
                case config::Loader::kTSNoAmbiguousLessThan: {
                    args.options.TS.Parse = true;
                    args.options.TS.NoAmbiguousLessThan = loader == config::Loader::kTSNoAmbiguousLessThan;
                    auto [ast, ok] = args.caches->js_cache.Parse(args.log, source,
                        javascript::OptionsFromConfig(&args.options));
                    if (ast.parts.size() <= 1) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsEmptyAST;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = ok;
                    break;
                }

                case config::Loader::kTSX: {
                    args.options.TS.Parse = true;
                    args.options.JSX.Parse = true;
                    auto [ast, ok] = args.caches->js_cache.Parse(args.log, source,
                        javascript::OptionsFromConfig(&args.options));
                    if (ast.parts.size() <= 1) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsEmptyAST;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = ok;
                    break;
                }

                case config::Loader::kCSS:
                case config::Loader::kGlobalCSS:
                case config::Loader::kLocalCSS: {
                    css::AST ast = args.caches->css_cache.Parse(args.log, source,
                        css::OptionsFromConfig(loader, args.options));
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::CSSRepr>(graph::CSSRepr{.ast = std::move(ast)})};
                    result.ok = true;
                    break;
                }

                case config::Loader::kHTML: {
                    html::BridgeOptions html_options;
                    html_options.collect_import_records = true;
                    html_options.collect_inline_code = true;
                    auto [ast, ok] = args.caches->html_cache.Parse(args.log, source, html_options);
                    if (!ok) {
                        result.ok = false;
                        break;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::HTMLRepr>(graph::HTMLRepr{.ast = std::move(ast)})};
                    result.ok = true;
                    break;
                }

                case config::Loader::kJSON:
                case config::Loader::kWithTypeJSON: {
                    javascript::JSONOptions json_options;
                    json_options.unsupported_js_features = args.options.UnsupportedJSFeatures;
                    auto [expr, ok] = args.caches->json_cache.Parse(args.log, source, json_options);
                    javascript::AST ast = javascript::LazyExportAST(args.log, source,
                        javascript::OptionsFromConfig(&args.options), expr, nullptr);
                    if (loader == config::Loader::kWithTypeJSON) {
                        ast.exports_kind = javascript::ExportsKind::kESM;
                    }
                    if (!plugin_name.empty()) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureDataFromPlugin;
                    } else {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureData;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = ok;
                    break;
                }

                case config::Loader::kText: {
                    constexpr std::string_view utf8_bom = "\xEF\xBB\xBF";
                    if (source.contents.rfind(utf8_bom, 0) == 0) {
                        source.contents = source.contents.substr(utf8_bom.size());
                    }
                    std::string encoded = helpers::Base64StdEncode(source.contents);
                    javascript::EString string_expr;
                    string_expr.value = helpers::StringToUTF16(source.contents);
                    javascript::AST ast = javascript::LazyExportAST(args.log, source,
                        javascript::OptionsFromConfig(&args.options),
                        javascript::Expr( std::make_shared<javascript::EString>(std::move(string_expr)), {}),
                        nullptr);
                    ast.url_for_css = "data:text/plain;base64," + encoded;
                    if (!plugin_name.empty()) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureDataFromPlugin;
                    } else {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureData;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = true;
                    break;
                }

                case config::Loader::kBase64: {
                    std::string mime_type = GuessMimeType(ext, source);
                    std::string encoded = helpers::Base64StdEncode(source.contents);
                    javascript::EString string_expr;
                    string_expr.value = helpers::StringToUTF16(encoded);
                    javascript::AST ast = javascript::LazyExportAST(args.log, source,
                        javascript::OptionsFromConfig(&args.options),
                        javascript::Expr( std::make_shared<javascript::EString>(std::move(string_expr)), {}),
                        nullptr);
                    ast.url_for_css = "data:" + mime_type + ";base64," + encoded;
                    if (!plugin_name.empty()) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureDataFromPlugin;
                    } else {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureData;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = true;
                    break;
                }

                case config::Loader::kBinary: {
                    std::string encoded = helpers::Base64StdEncode(source.contents);
                    javascript::EString string_expr;
                    string_expr.value = helpers::StringToUTF16(encoded);
                    std::optional<javascript::HelperCall> helper;
                    if (compat::Has(args.options.UnsupportedJSFeatures, compat::JSFeature::kFromBase64)) {
                        helper = javascript::HelperCall{};
                        if (args.options.OutputPlatform == config::Platform::kNode) {
                            helper->Runtime = "__toBinaryNode";
                        } else {
                            helper->Runtime = "__toBinary";
                        }
                    } else {
                        helper = javascript::HelperCall{};
                        helper->Global = {"Uint8Array", "fromBase64"};
                    }
                    javascript::AST ast = javascript::LazyExportAST(args.log, source,
                        javascript::OptionsFromConfig(&args.options),
                        javascript::Expr( std::make_shared<javascript::EString>(std::move(string_expr)), {}),
                        helper.has_value() ? &*helper : nullptr);
                    ast.url_for_css = "data:application/octet-stream;base64," + encoded;
                    if (!plugin_name.empty()) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureDataFromPlugin;
                    } else {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureData;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = true;
                    break;
                }

                case config::Loader::kDataURL: {
                    std::string mime_type = GuessMimeType(ext, source);
                    std::string url = helpers::EncodeStringAsShortestDataURL(mime_type, source.contents);
                    if (source.key_path.ignored_suffix.rfind("#", 0) == 0) {
                        url += source.key_path.ignored_suffix;
                    }
                    javascript::EString string_expr;
                    string_expr.value = helpers::StringToUTF16(url);
                    javascript::AST ast = javascript::LazyExportAST(args.log, source,
                        javascript::OptionsFromConfig(&args.options),
                        javascript::Expr( std::make_shared<javascript::EString>(std::move(string_expr)), {}),
                        nullptr);
                    ast.url_for_css = url;
                    if (!plugin_name.empty()) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureDataFromPlugin;
                    } else {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureData;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = true;
                    break;
                }

                case config::Loader::kFile: {
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer), "%08u", args.source_index);
                    std::string unique_key = args.unique_key_prefix + "A" + buffer;
                    std::string unique_key_path = unique_key + source.key_path.ignored_suffix;
                    javascript::EString string_expr;
                    string_expr.value = helpers::StringToUTF16(unique_key_path);
                    string_expr.contains_unique_key = true;
                    javascript::AST ast = javascript::LazyExportAST(args.log, source,
                        javascript::OptionsFromConfig(&args.options),
                        javascript::Expr( std::make_shared<javascript::EString>(std::move(string_expr)), {}),
                        nullptr);
                    ast.url_for_css = unique_key_path;
                    if (!plugin_name.empty()) {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureDataFromPlugin;
                    } else {
                        result.file.input_file.side_effects.kind =
                            graph::SideEffectsKind::kNoSideEffectsPureData;
                    }
                    result.file.input_file.repr =
                        graph::InputFileRepr{std::make_shared<graph::JSRepr>(graph::JSRepr{.ast = std::move(ast)})};
                    result.ok = true;

                    result.file.input_file.unique_key_for_additional_file = unique_key;
                    break;
                }

                case config::Loader::kCopy: {
                    char buffer[32];
                    snprintf(buffer, sizeof(buffer), "%08u", args.source_index);
                    std::string unique_key = args.unique_key_prefix + "A" + buffer;
                    std::string unique_key_path = unique_key + source.key_path.ignored_suffix;
                    result.file.input_file.repr = graph::InputFileRepr{
                        std::make_shared<graph::CopyRepr>(graph::CopyRepr{.url_for_code = unique_key_path})};
                    result.ok = true;

                    result.file.input_file.unique_key_for_additional_file = unique_key;
                    break;
                }

                default: {
                    std::string message;
                    if (source.key_path.namespace_ == "file" && !ext.empty()) {
                        message = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_NoLoaderConfigured, ext,
                                          source.pretty_paths.Select(args.options.LogPathStyle));
                    } else {
                        message = guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_DoNotKnowHowToLoadPath,
                                  source.pretty_paths.Select(args.options.LogPathStyle));
                    }
                    logger::LineColumnTracker tracker(&args.import_source_copy);
                    args.log.AddError(&tracker, args.import_path_range, message);
                    break;
                }
            }

                if (result.ok) {
                    std::vector<compiler::ImportRecord>* records_ptr = nullptr;
                    if (auto* js = std::get_if<std::shared_ptr<graph::JSRepr>>(
                            &result.file.input_file.repr);
                        js != nullptr && *js != nullptr)
                    {
                        records_ptr = &(*js)->ImportRecords();
                    } else if (auto* css = std::get_if<std::shared_ptr<graph::CSSRepr>>(
                                   &result.file.input_file.repr);
                        css != nullptr && *css != nullptr)
                    {
                        records_ptr = &(*css)->ImportRecords();
                    } else if (auto* html = std::get_if<std::shared_ptr<graph::HTMLRepr>>(
                                   &result.file.input_file.repr);
                        html != nullptr && *html != nullptr)
                    {
                        records_ptr = &(*html)->ImportRecords();
                    }
                if (records_ptr != nullptr && args.options.BuildMode == config::Mode::kBundle &&
                    !args.skip_resolve)
                {
                    std::vector<compiler::ImportRecord> records = *records_ptr;
                    *records_ptr = records;
                    result.resolve_results.resize(records.size());

                    if (!records.empty()) {
                        struct CacheEntry {
                            std::shared_ptr<resolver::ResolveResult> resolve_result;
                            resolver::DebugMeta debug;
                            bool did_log_error{};
                        };
                        using CacheKey =
                            std::tuple<compiler::ImportKind, std::string, std::string>;
                        std::map<CacheKey, CacheEntry> resolver_cache;
                        logger::LineColumnTracker tracker(&source);

                        for (size_t import_record_index = 0; import_record_index < records.size();
                             import_record_index++)
                        {
                            compiler::ImportRecord& record = records[import_record_index];
                            if (record.source_index.IsValid()) {
                                continue;
                            }

                            logger::ImportAttributes attrs;
                            if (record.assert_or_with != nullptr &&
                                record.assert_or_with->keyword ==
                                    compiler::AssertOrWithKeyword::kWith)
                            {
                                std::unordered_map<std::string, std::string> data;
                                data.reserve(record.assert_or_with->entries.size());
                                for (const auto& entry : record.assert_or_with->entries) {
                                    data[helpers::UTF16ToString(entry.key)] =
                                        helpers::UTF16ToString(entry.value);
                                }
                                attrs = logger::EncodeImportAttributes(data);
                            }

                            if (record.glob_pattern != nullptr) {
                                std::string pretty_path =
                                    helpers::GlobPatternToString(record.glob_pattern->parts);
                                std::string phase;
                                switch (record.phase) {
                                    case compiler::ImportPhase::kDefer: phase = ".defer"; break;
                                    case compiler::ImportPhase::kSource: phase = ".source"; break;
                                    default: break;
                                }
                                switch (record.glob_pattern->kind) {
                                    case compiler::ImportKind::kRequire:
                                        pretty_path = "require" + phase + "(\"" + pretty_path + "\")";
                                        break;
                                    case compiler::ImportKind::kDynamic:
                                        pretty_path = "import" + phase + "(\"" + pretty_path + "\")";
                                        break;
                                    default: break;
                                }
logger::Msg glob_warning;
                                    std::optional<std::map<std::string, resolver::ResolveResult>> glob_results =
                                        args.res->ResolveGlob(abs_resolve_dir, record.glob_pattern->parts,
                                            record.glob_pattern->kind, pretty_path, &glob_warning);
                                    if (glob_results.has_value()) {
                                    if (glob_warning.id != logger::MsgID::kNone) {
                                        args.log.AddID(glob_warning.id, glob_warning.kind, &tracker,
                                            record.range, glob_warning.data.text);
                                    }
                                    bool all_are_external = true;
                                    for (auto& [key, one_result] : *glob_results) {
                                        if (!one_result.path_pair.is_external) {
                                            all_are_external = false;
                                        }
                                        one_result.path_pair.primary.import_attributes = attrs;
                                        if (one_result.path_pair.HasSecondary()) {
                                            one_result.path_pair.secondary.import_attributes = attrs;
                                        }
                                    }
                                    GlobResolveResult glob_resolve;
                                    glob_resolve.resolve_results = std::move(*glob_results);
                                    glob_resolve.abs_path = args.fs->Join({abs_resolve_dir, "(glob)"});
                                    glob_resolve.pretty_paths.abs = pretty_path + " in " +
                                                                    result.file.input_file.source.pretty_paths.abs;
                                    glob_resolve.pretty_paths.rel = pretty_path + " in " +
                                                                    result.file.input_file.source.pretty_paths.rel;
                                    glob_resolve.export_alias = record.glob_pattern->export_alias;
                                    result.glob_resolve_results.emplace(
                                        static_cast<uint32_t>(import_record_index),
                                        std::move(glob_resolve));

                                    if (record.phase != compiler::ImportPhase::kEvaluation) {
                                        ReportExplicitPhaseImport(args.log, &tracker, record.range,
                                            record.phase, all_are_external, args.options.OutputFormat);
                                    }
                                } else {
                                    args.log.AddError(&tracker, record.range,
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CouldNotResolve, pretty_path));
                                }
                                continue;
                            }

                            if (compiler::Has(record.flags, compiler::ImportRecordFlags::kIsUnused)) {
                                continue;
                            }

                            CacheKey cache_key{
                                record.kind,
                                record.path.text,
                                attrs.packed_data,
                            };
                            auto cached = resolver_cache.find(cache_key);
                            if (cached != resolver_cache.end()) {
                                result.resolve_results[import_record_index] =
                                    cached->second.resolve_result;
                            } else {
                                OnResolveOutcome outcome = RunOnResolvePlugins(
                                    args.options.Plugins,
                                    args.res,
                                    args.log,
                                    *args.fs,
                                    args.caches->fs_cache,
                                    &source,
                                    record.range,
                                    source.key_path,
                                    record.path.text,
                                    attrs,
                                    record.kind,
                                    abs_resolve_dir,
                                    plugin_data,
                                    args.options.LogPathStyle);
                                if (outcome.resolve_result.has_value()) {
                                    outcome.resolve_result->path_pair.primary.import_attributes = attrs;
                                    if (outcome.resolve_result->path_pair.HasSecondary()) {
                                        outcome.resolve_result->path_pair.secondary.import_attributes = attrs;
                                    }
                                }
                                CacheEntry entry{
                                    .resolve_result = std::make_shared<resolver::ResolveResult>(
                                        std::move(outcome.resolve_result).value_or(resolver::ResolveResult{})),
                                    .debug = std::move(outcome.debug_meta),
                                    .did_log_error = outcome.did_log_error,
                                };
                                if (!outcome.resolve_result.has_value()) {
                                    entry.resolve_result.reset();
                                }
                                resolver_cache[cache_key] = entry;

                                if (record.kind == compiler::ImportKind::kRequireResolve) {
                                    if (entry.resolve_result != nullptr &&
                                        entry.resolve_result->path_pair.is_external) {
                                        result.resolve_results[import_record_index] = entry.resolve_result;
                                    } else if (!compiler::Has(record.flags,
                                                   compiler::ImportRecordFlags::kHandlesImportErrors)) {
                                        args.log.AddID(logger::MsgID::kBundler_RequireResolveNotExternal,
                                            logger::MsgKind::kWarning, &tracker, record.range,
                                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_ShouldBeMarkedExternal, record.path.text));
                                    }
                                    continue;
                                }
                                result.resolve_results[import_record_index] = entry.resolve_result;
                                cached = resolver_cache.find(cache_key);
                                if (cached != resolver_cache.end()) {
                                    CacheEntry& entry_ref = cached->second;

                                    if (entry_ref.resolve_result == nullptr) {
                                        if (!entry_ref.did_log_error &&
                                            !compiler::Has(record.flags,
                                                compiler::ImportRecordFlags::kHandlesImportErrors)) {
                                            ResolveFailureInfo failure =
                                                ResolveFailureErrorTextSuggestionNotes(
                                                    *args.res, record.path.text, record.kind, plugin_name,
                                                    *args.fs, abs_resolve_dir, args.options.OutputPlatform,
                                                    source.pretty_paths,
                                                    entry_ref.debug.modified_import_path,
                                                    args.options.LogPathStyle);
                                            entry_ref.debug.LogErrorMsg(args.log, &source, record.range,
                                                failure.text, failure.suggestion, failure.notes);

                                            entry_ref.did_log_error = true;
                                        } else if (!entry_ref.did_log_error &&
                                                   compiler::Has(record.flags,
                                                       compiler::ImportRecordFlags::kHandlesImportErrors)) {
                                            args.log.AddIDWithNotes(
                                                logger::MsgID::kBundler_IgnoredDynamicImport,
                                                logger::MsgKind::kDebug, &tracker, record.range,
                                                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_IgnoredDynamicImport, record.path.text),
                                                std::vector<MsgData>{tracker.MakeMsgData(
                                                    javascript::RangeOfIdentifier(source, record.error_handler_loc),
                                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_DynamicImportHandlerNote))});
                                        }
                                        continue;
                                    }

if (record.phase != compiler::ImportPhase::kEvaluation) {
                                ReportExplicitPhaseImport(args.log, &tracker, record.range, record.phase,
                                    entry.resolve_result->path_pair.is_external, args.options.OutputFormat);
                            }
                                    continue;
                                }
                                continue;
                            }

                            CacheEntry& entry = cached->second;
                            if (entry.resolve_result == nullptr) {
                                if (!entry.did_log_error &&
                                    !compiler::Has(record.flags,
                                        compiler::ImportRecordFlags::kHandlesImportErrors)) {
                                    ResolveFailureInfo failure = ResolveFailureErrorTextSuggestionNotes(
                                        *args.res, record.path.text, record.kind, plugin_name, *args.fs,
                                        abs_resolve_dir, args.options.OutputPlatform, source.pretty_paths,
                                        entry.debug.modified_import_path, args.options.LogPathStyle);
                                    entry.debug.LogErrorMsg(args.log, &source, record.range, failure.text,
                                        failure.suggestion, failure.notes);

                                    entry.did_log_error = true;
                                    resolver_cache[cache_key] = entry;
                                } else if (!entry.did_log_error &&
                                           compiler::Has(record.flags,
                                               compiler::ImportRecordFlags::kHandlesImportErrors)) {
                                    args.log.AddIDWithNotes(logger::MsgID::kBundler_IgnoredDynamicImport,
                                        logger::MsgKind::kDebug, &tracker, record.range,
                                        guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_IgnoredDynamicImport, record.path.text),
                                        std::vector<MsgData>{tracker.MakeMsgData(
                                            javascript::RangeOfIdentifier(source, record.error_handler_loc),
                                            guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_DynamicImportHandlerNote))});
                                }
                                continue;
                            }

if (record.phase != compiler::ImportPhase::kEvaluation) {
                                ReportExplicitPhaseImport(args.log, &tracker, record.range, record.phase,
                                    entry.resolve_result->path_pair.is_external, args.options.OutputFormat);
                            }

                            result.resolve_results[import_record_index] = entry.resolve_result;
                        }
                    }
                }

                if (config::CanHaveSourceMap(loader) &&
                    args.options.SourceMapData != config::SourceMap::kNone)
                {
                    javascript::Span source_map_comment_storage;
                    css::lexer::Span css_source_map_comment_storage;
                    bool has_comment = false;
                    bool is_js = false;
                    std::visit([&](auto&& repr) {
                        using ReprType = std::decay_t<decltype(repr)>;
                        if constexpr (std::is_same_v<ReprType, std::shared_ptr<graph::JSRepr>>) {
                            source_map_comment_storage.text = repr->ast.source_map_comment.text;
                            source_map_comment_storage.range = repr->ast.source_map_comment.range;
                            has_comment = true;
                            is_js = true;
                        } else if constexpr (std::is_same_v<ReprType, std::shared_ptr<graph::CSSRepr>>) {
                            css_source_map_comment_storage.text = repr->ast.source_map_comment.text;
                            css_source_map_comment_storage.range = repr->ast.source_map_comment.range;
                            has_comment = true;
                            is_js = false;
                        }
                    }, result.file.input_file.repr);

                    const std::string* comment_text = is_js ? &source_map_comment_storage.text
                                                            : &css_source_map_comment_storage.text;
                    Range comment_range = is_js ? source_map_comment_storage.range
                                                : css_source_map_comment_storage.range;
                    if (!comment_text->empty()) {
                        logger::LineColumnTracker tracker(&source);
                        javascript::Span comment{.text = *comment_text, .range = comment_range};

                        auto extracted = ExtractSourceMapFromComment(args.log, *args.fs,
                            args.caches->fs_cache, source, &tracker, comment, abs_resolve_dir,
                            args.options.LogPathStyle);
                        if (extracted.has_value()) {
                            auto& [path, contents] = *extracted;
                            PrettyPaths pretty_paths = resolver::MakePrettyPaths(*args.fs, path);
                            Log defer_log = logger::NewDeferLog(
                                logger::DeferLogKind::kDeferLogNoVerboseOrDebug, args.log.overrides);

                            Source source_map_source;
                            source_map_source.key_path = path;
                            source_map_source.pretty_paths = pretty_paths;
                            source_map_source.contents = contents;
                            std::unique_ptr<sourcemap::SourceMapData> source_map =
                                javascript::ParseSourceMap(defer_log, source_map_source);

                            std::vector<Msg> msgs = defer_log.done();
                            if (!msgs.empty()) {
                                std::string text;
                                if (path.namespace_ == "file") {
                                    text = "The source map \"" + pretty_paths.Select(args.options.LogPathStyle) +
                                           "\" was referenced by the file \"" +
                                           args.pretty_paths.Select(args.options.LogPathStyle) + "\" here:";
                                } else {
                                    text = "This source map came from the file \"" +
                                           args.pretty_paths.Select(args.options.LogPathStyle) + "\" here:";
                                }
                                MsgData note = tracker.MakeMsgData(comment.range, text);
                                for (auto& msg : msgs) {
                                    msg.notes.push_back(note);
                                    args.log.add_msg(msg);
                                }
                            }

                            if (source_map != nullptr && !args.options.ExcludeSourcesContent) {
                                if (source_map->sources_content.size() < source_map->sources.size()) {
                                    source_map->sources_content.resize(source_map->sources.size());
                                }

                                for (size_t i = 0; i < source_map->sources.size(); i++) {
                                    const std::string& one_source = source_map->sources[i];

                                    if (path.namespace_ == "file" && args.fs->IsAbs(one_source)) {
                                        source_map->sources[i] =
                                            helpers::FileURLFromFilePath(one_source);
                                    }

                                    if (source_map->sources_content[i].value.empty()) {
                                        std::optional<helpers::URL> source_url =
                                            helpers::ParseURL(source_map->sources[i]);
                                        if (source_url.has_value() && helpers::IsFileURL(
                                                                           source_url->scheme,
                                                                           source_url->host,
                                                                           source_url->path))
                                        {
                                            auto file_contents = args.caches->fs_cache.ReadFile(
                                                *args.fs,
                                                helpers::FilePathFromFileURL(source_url->path,
                                                    args.fs->Cwd()));
                                            if (file_contents.Ok()) {
                                                source_map->sources_content[i].value =
                                                    helpers::StringToUTF16(file_contents.value);
                                            }
                                        }
                                    }
                                }
                            }

                            result.file.input_file.input_source_map = std::move(source_map);
                        }
                    }
                }
            }
        } catch (...) {
            args.log.AddErrorWithNotes(nullptr, Range{},
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_PanicParsing,
                    source.pretty_paths.Select(args.options.LogPathStyle)),
                {});
            result.ok = false;
        }

        if (args.inject) {
            std::vector<config::InjectableExport> exports;

            if (auto* repr = TryJSRepr(result.file.input_file.repr); repr != nullptr) {
                std::vector<std::string> aliases;
                aliases.reserve(repr->ast.named_exports.size());
                for (const auto& [alias, named_export] : repr->ast.named_exports) {
                    aliases.push_back(alias);
                }
                std::sort(aliases.begin(), aliases.end());
                exports.reserve(aliases.size());
                for (const auto& alias : aliases) {
                    config::InjectableExport export_entry;
                    export_entry.Alias = alias;
                    export_entry.LocData = repr->ast.named_exports.at(alias).alias_loc;
                    exports.push_back(std::move(export_entry));
                }
            }

            bool is_copy_loader = loader == config::Loader::kCopy;
            if (is_copy_loader && args.skip_resolve) {
                args.log.AddError(nullptr, Range{},
                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kBundler_CannotInjectCopyLoader,
                        source.pretty_paths.Select(args.options.LogPathStyle)));
            }
            config::InjectedFile injected;
            injected.SourceData = source;
            injected.Exports = std::move(exports);
            injected.IsCopyLoader = is_copy_loader;
            args.inject->Send(std::move(injected));
        }

        args.results->Send(std::move(result));
    }

}
