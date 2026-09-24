// This file handles the "guchho.json" / "guchho.config.json" project
// configuration formats. It discovers the config file by walking up the
// directory tree, parses it as strict JSON, and maps the field names
// documented in "gf.md" onto the internal "config::Options".
//
// It also handles the "guchho.config.js" format: the file is regular
// JavaScript, so it is evaluated with an external runtime (Node) and the
// resulting config object is transported back as JSON, which is then applied
// through the same field mapping used for "guchho.json". See the header
// comment for the full rationale.
//
// Within one directory the supported files are checked in priority order:
// "guchho.config.js", then "guchho.config.json", then "guchho.json". The
// primary JS config takes precedence across the whole discovery walk, so a
// JSON config is only a fallback when no "guchho.config.js" exists in any
// directory; config files are never merged. Config files are partial
// overrides on top of the built-in defaults built by
// "CreateDefaultGuchhoConfig", so an omitted field always keeps its default.
// A config that exists but is invalid is FOUND + INVALID and is never treated
// as "no config".

#include "guchho/resolver.hpp"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "guchho/compat.hpp"
#include "guchho/logger.hpp"
#include "guchho/helpers.hpp"
#include "guchho/javascript/js_parser.hpp"

namespace guchho::resolver {

    namespace {

        std::string Q(std::string_view text)
        {
            return helpers::QuoteForJSON(text, false);
        }

        // The environment variable that can override the Node binary to use.
        // Kept out of the header so only this translation unit depends on it.
        constexpr const char* kNodeEnvName = "GUCHHO_NODE_BIN";

        std::optional<std::string> GetEnvValue(const char* name)
        {
        #ifdef _WIN32
            char*  value = nullptr;
            size_t size  = 0;
            if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
                return std::nullopt;
            }
            std::string result = value;
            free(value);
            return result;
        #else
            if (const char* value = std::getenv(name)) {
                return std::string(value);
            }
            return std::nullopt;
        #endif
        }

        std::string NodeBinary()
        {
            if (auto value = GetEnvValue(kNodeEnvName)) {
                if (!value->empty()) {
                    return *value;
                }
            }
            return "node";
        }

        // Builds the inline bootstrap script. It imports the config file as a
        // module (handling both "export default" and CommonJS "module.exports"
        // through the interop default), and prints the resolved object as JSON
        // on stdout. The config path is embedded as a JSON string so that no
        // shell quoting is involved.
        std::string BuildConfigJSScript(const std::string& config_path)
        {
            const std::string quoted = Q(config_path);
            return
                "const { pathToFileURL } = require('node:url');\n"
                "const configPath = " + quoted + ";\n"
                "(async () => {\n"
                "  const mod = await import(pathToFileURL(configPath).href);\n"
                "  const config = mod ? (mod.default || mod) : {};\n"
                "  process.stdout.write(JSON.stringify(config));\n"
                "})().catch((err) => {\n"
                "  process.stderr.write((err && err.stack) ? err.stack : String(err));\n"
                "  process.exitCode = 1;\n"
                "});\n";
        }

        // An installable fake so the discovery loop can be tested without
        // spawning Node.
        GuchhoConfigJSLoader g_js_loader = LoadGuchhoConfigFromJS;

        bool IsInsideNodeModules(const std::string& path)
        {
            return helpers::IsInsideNodeModules(path);
        }

        std::string PropertyKeyText(const javascript::Property& property)
        {
            if (auto* key = std::get_if<std::shared_ptr<javascript::EString>>(&property.key.data)) {
                return helpers::UTF16ToString((*key)->value);
            }
            return "";
        }

        // True when the path is absolute on the current platform ("/x", "C:\x",
        // "\\server\share"). Root-relative paths are not considered absolute.
        bool IsAbsPath(const std::string& path)
        {
            if (path.empty()) return false;
            if (path[0] == '/' || path[0] == '\\') return true;
            return path.size() >= 3 && path[1] == ':' &&
                   (path[2] == '/' || path[2] == '\\');
        }

        // Joins a possibly-relative path against the config directory. Absolute
        // paths pass through unchanged. Uses forward slashes so the result is
        // stable across platforms (the filesystem layer normalizes later).
        std::string JoinAbsConfigDir(const std::string& config_dir, const std::string& path)
        {
            if (IsAbsPath(path)) return path;
            if (config_dir.empty()) return path;
            if (config_dir.back() == '/' || config_dir.back() == '\\') {
                return config_dir + path;
            }
            return config_dir + "/" + path;
        }

        std::optional<config::Format> ParseFormat(std::string_view text)
        {
            std::string lower = helpers::ToLowerASCII(text);
            if (lower == "esm") return config::Format::kESModule;
            if (lower == "cjs" || lower == "commonjs") return config::Format::kCommonJS;
            if (lower == "iife") return config::Format::kIIFE;
            if (lower == "umd") return config::Format::kUMD;
            if (lower == "amd") return config::Format::kAMD;
            if (lower == "system" || lower == "systemjs") return config::Format::kSystem;
            return std::nullopt;
        }

        std::optional<config::Platform> ParsePlatform(std::string_view text)
        {
            std::string lower = helpers::ToLowerASCII(text);
            if (lower == "browser") return config::Platform::kBrowser;
            if (lower == "node") return config::Platform::kNode;
            if (lower == "neutral") return config::Platform::kNeutral;
            return std::nullopt;
        }

        std::optional<config::SourceMap> ParseSourceMap(std::string_view text)
        {
            std::string lower = helpers::ToLowerASCII(text);
            if (lower == "inline") return config::SourceMap::kInline;
            if (lower == "external") return config::SourceMap::kExternalWithoutComment;
            if (lower == "linked") return config::SourceMap::kLinkedWithComment;
            if (lower == "both" || lower == "inline-and-external") {
                return config::SourceMap::kInlineAndExternal;
            }
            return std::nullopt;
        }

        // Converts an "es2020"-style target string into the set of unsupported
        // JS/CSS features Guchho must lower. Unknown engines and "esnext" are
        // handled gracefully (no lowering, debug-level note for bad input).
        void ApplyTarget(config::Options& opts, const std::string& text,
                         const logger::Source& source, logger::Loc value_loc)
        {
            std::string lower = helpers::ToLowerASCII(text);
            opts.OriginalTargetEnv = lower;

            if (lower == "esnext") {
                return;
            }

            size_t i = 0;
            while (i < lower.size() && (lower[i] < '0' || lower[i] > '9')) {
                ++i;
            }
            std::string engine_name = lower.substr(0, i);
            std::string version     = lower.substr(i);

            if (version.empty()) {
                return;
            }

            compat::Engine engine;
            if (engine_name == "es") {
                engine = compat::Engine::kES;
            } else if (engine_name == "chrome") {
                engine = compat::Engine::kChrome;
            } else if (engine_name == "edge") {
                engine = compat::Engine::kEdge;
            } else if (engine_name == "firefox") {
                engine = compat::Engine::kFirefox;
            } else if (engine_name == "safari") {
                engine = compat::Engine::kSafari;
            } else if (engine_name == "ios") {
                engine = compat::Engine::kIOS;
            } else if (engine_name == "node") {
                engine = compat::Engine::kNode;
            } else if (engine_name == "deno") {
                engine = compat::Engine::kDeno;
            } else if (engine_name == "opera") {
                engine = compat::Engine::kOpera;
            } else if (engine_name == "hermes") {
                engine = compat::Engine::kHermes;
            } else if (engine_name == "ie") {
                engine = compat::Engine::kIE;
            } else if (engine_name == "rhino") {
                engine = compat::Engine::kRhino;
            } else {
                // Unrecognized engine: keep the defaults, note it at debug level.
                if (!IsInsideNodeModules(source.key_path.text)) {
                    (void)value_loc;
                }
                return;
            }

            std::vector<uint32_t> parts;
            {
                std::stringstream ss(version);
                std::string       part;
                while (std::getline(ss, part, '.')) {
                    char* end = nullptr;
                    parts.push_back(static_cast<uint32_t>(std::strtoul(part.c_str(), &end, 10)));
                }
            }
            if (parts.empty() || parts[0] == 0) {
                return;
            }

            compat::Semver semver;
            semver.parts = std::move(parts);
            opts.UnsupportedJSFeatures  = compat::UnsupportedJSFeatures({{engine, semver}});
            opts.UnsupportedCSSFeatures = compat::UnsupportedCSSFeatures({{engine, semver}});
        }

        // Parses an output path template such as "chunks/[name]-[hash].js" into
        // the "PathTemplate" vector used by "config::Options".
        std::vector<config::PathTemplate> ParsePathTemplate(const std::string& text)
        {
            std::vector<config::PathTemplate> result;
            size_t                            start = 0;
            while (start < text.size()) {
                size_t mark = text.find('[', start);
                if (mark == std::string::npos) {
                    result.push_back(config::PathTemplate{.Data = text.substr(start)});
                    break;
                }
                if (mark > start) {
                    result.push_back(config::PathTemplate{.Data = text.substr(start, mark - start)});
                }
                size_t close = text.find(']', mark);
                if (close == std::string::npos) {
                    result.push_back(config::PathTemplate{.Data = text.substr(mark)});
                    break;
                }
                std::string name = text.substr(mark + 1, close - mark - 1);
                config::PathPlaceholder placeholder = config::PathPlaceholder::kNoPlaceholder;
                if (name == "dir") {
                    placeholder = config::PathPlaceholder::kDir;
                    result.push_back(config::PathTemplate{.Data = {}, .Placeholder = placeholder});
                } else if (name == "name") {
                    placeholder = config::PathPlaceholder::kName;
                    result.push_back(config::PathTemplate{.Data = {}, .Placeholder = placeholder});
                } else if (name == "hash") {
                    placeholder = config::PathPlaceholder::kHash;
                    result.push_back(config::PathTemplate{.Data = {}, .Placeholder = placeholder});
                } else if (name == "ext") {
                    placeholder = config::PathPlaceholder::kExt;
                    result.push_back(config::PathTemplate{.Data = {}, .Placeholder = placeholder});
                } else {
                    // Unknown placeholder: keep the full "[name]" as literal text.
                    result.push_back(config::PathTemplate{.Data = "[" + name + "]"});
                }
                start = close + 1;
            }
            return result;
        }

        // Splits a define key like "process.env.NODE_ENV" into its dot parts.
        std::vector<std::string> SplitDefineKey(const std::string& key)
        {
            std::vector<std::string> parts;
            size_t                   start = 0;
            while (start <= key.size()) {
                size_t dot = key.find('.', start);
                if (dot == std::string::npos) {
                    parts.push_back(key.substr(start));
                    break;
                }
                parts.push_back(key.substr(start, dot - start));
                start = dot + 1;
            }
            return parts;
        }

        // Extracts the define replacement expression text from a JSON value.
        // Mirrors how esbuild accepts define values: the JSON string content is
        // itself a JSON expression (e.g. "\"production\"", "true", "42").
        std::optional<std::string> DefineValueText(const javascript::Expr& value)
        {
            if (auto str = internal::GetString(value)) {
                return std::move(*str);
            }
            if (auto boolean = internal::GetBool(value)) {
                return *boolean ? std::string("true") : std::string("false");
            }
            if (auto* number = std::get_if<std::shared_ptr<javascript::ENumber>>(&value.data)) {
                std::stringstream ss;
                ss << (*number)->value;
                return ss.str();
            }
            if (std::get_if<std::shared_ptr<javascript::ENull>>(&value.data) != nullptr) {
                return std::string("null");
            }
            if (auto* array = std::get_if<std::shared_ptr<javascript::EArray>>(&value.data)) {
                std::string out = "[";
                bool        first = true;
                for (const auto& item : (*array)->items) {
                    if (!first) out += ",";
                    first = false;
                    auto sub = DefineValueText(item);
                    if (!sub) return std::nullopt;
                    out += *sub;
                }
                out += "]";
                return out;
            }
            return std::nullopt;
        }

        void WarnUnknownField(logger::Log& log, const logger::Source& source,
                              logger::LineColumnTracker& tracker, logger::Loc loc,
                              const std::string& field)
        {
            logger::Range range = source.RangeOfString(loc);
            log.AddID(logger::MsgID::kGuchhoJSON_UnknownField, logger::MsgKind::kWarning, &tracker,
                      range,
                      logger::FormatMsg(logger::MsgCat::kGuchhoJSON_UnknownField,
                                        Q(field)));
        }

        // The supported config file names, in priority order within a single
        // directory. "guchho.config.js" is the primary filename; between the
        // JSON alternatives "guchho.config.json" outranks "guchho.json".
        constexpr const char* kConfigFileNames[] = {
            "guchho.config.js",
            "guchho.config.json",
            "guchho.json",
        };

        bool IsJSConfigName(const char* name)
        {
            size_t len = std::char_traits<char>::length(name);
            return len >= 3 && name[len - 3] == '.' && name[len - 2] == 'j' && name[len - 1] == 's';
        }

        bool FieldIsKnown(const std::string& key, const char* const* known, size_t count)
        {
            for (size_t i = 0; i < count; ++i) {
                if (key == known[i]) return true;
            }
            return false;
        }

        // Warns about unknown properties inside a JSON object.
        void WarnUnknownFields(logger::Log& log, const javascript::Expr& obj,
                               const logger::Source& source,
                               logger::LineColumnTracker& tracker,
                               const std::string& prefix, const char* const* known,
                               size_t count)
        {
            if (auto* object = std::get_if<std::shared_ptr<javascript::EObject>>(&obj.data)) {
                for (const auto& p : (*object)->properties) {
                    std::string key = PropertyKeyText(p);
                    if (!FieldIsKnown(key, known, count)) {
                        WarnUnknownField(log, source, tracker, p.key.loc, prefix + key);
                    }
                }
            }
        }

    } // namespace

    GuchhoConfig CreateDefaultGuchhoConfig(const std::string& root_dir)
    {
        GuchhoConfig result;

        config::Options& opts = result.opts;
        opts.OutputFormat      = config::Format::kESModule;  // "format": "esm"
        opts.OutputPlatform    = config::Platform::kBrowser; // "platform": "browser"
        opts.OriginalTargetEnv = "esnext";                   // "target": "esnext"
        opts.MinifyWhitespace  = true;                       // "minify": true
        opts.MinifyIdentifiers = true;
        opts.MinifySyntax      = true;
        opts.SourceMapData     = config::SourceMap::kNone;   // "sourcemap": false
        opts.CodeSplitting     = false;                      // "splitting": false
        opts.TreeShaking       = true;                       // "treeShaking": true
        opts.AbsOutputDir      = JoinAbsConfigDir(root_dir, "dist"); // "outdir": "dist"
        opts.AbsOutputFile     = "";                         // "outfile": unset

        config::EntryPoint entry;
        entry.InputPath = JoinAbsConfigDir(root_dir, "index.html"); // "entry": "index.html"
        result.entry_points.push_back(std::move(entry));

        // "build.clean" has no "config::Options" field — it is handled by the
        // CLI/build runner — so the default "clean: false" is implicit.

        return result;
    }

    void ApplyGuchhoJsonConfig(
        logger::Log&                     log,
        filesystem::Fs&                  fs,
        config::Options&                 opts,
        std::vector<config::EntryPoint>& entry_points,
        std::vector<config::DefineData>& user_defines,
        const javascript::Expr&          json,
        const std::string&               config_dir,
        const logger::Source&            source)
    {
        logger::LineColumnTracker tracker(&source);

        // Any field whose value is stored in "opts" first. Values that are
        // outside the "config::Options" surface (entries, defines) are appended
        // to the caller-provided vectors.
        auto append_entries = [&](const javascript::Expr& value) {
            if (auto str = internal::GetString(value)) {
                config::EntryPoint ep;
                ep.InputPath = JoinAbsConfigDir(config_dir, *str);
                entry_points.push_back(std::move(ep));
            } else if (auto* arr =
                           std::get_if<std::shared_ptr<javascript::EArray>>(&value.data)) {
                for (const auto& item : (*arr)->items) {
                    if (auto item_str = internal::GetString(item)) {
                        config::EntryPoint ep;
                        ep.InputPath = JoinAbsConfigDir(config_dir, *item_str);
                        entry_points.push_back(std::move(ep));
                    }
                }
            }
        };

        // ---- "build" -----------------------------------------------------
        static const char* kKnownBuildFields[] = {
            "entry", "outdir", "outfile", "format", "platform", "target",
            "minify", "sourcemap", "splitting", "clean", "treeShaking",
        };
        if (auto build_prop = internal::GetProperty(json, "build")) {
            const javascript::Expr& build = build_prop->first;

            if (auto entry = internal::GetProperty(build, "entry")) {
                append_entries(entry->first);
            }
            if (auto outdir = internal::GetProperty(build, "outdir")) {
                if (auto str = internal::GetString(outdir->first)) {
                    opts.AbsOutputDir  = JoinAbsConfigDir(config_dir, *str);
                    opts.AbsOutputFile = "";
                }
            }
            if (auto outfile = internal::GetProperty(build, "outfile")) {
                if (auto str = internal::GetString(outfile->first)) {
                    opts.AbsOutputFile = JoinAbsConfigDir(config_dir, *str);
                    opts.AbsOutputDir  = "";
                }
            }
            if (auto format = internal::GetProperty(build, "format")) {
                if (auto str = internal::GetString(format->first)) {
                    if (auto fmt = ParseFormat(*str)) {
                        opts.OutputFormat = *fmt;
                    } else {
                        log.AddID(logger::MsgID::kGuchhoJSON_InvalidFormat,
                                  logger::MsgKind::kWarning, &tracker,
                                  source.RangeOfString(format->first.loc),
                                  logger::FormatMsg(logger::MsgCat::kGuchhoJSON_InvalidFormat,
                                                    Q(*str)));
                    }
                }
            }
            if (auto platform = internal::GetProperty(build, "platform")) {
                if (auto str = internal::GetString(platform->first)) {
                    if (auto plat = ParsePlatform(*str)) {
                        opts.OutputPlatform = *plat;
                    } else {
                        log.AddID(logger::MsgID::kGuchhoJSON_InvalidPlatform,
                                  logger::MsgKind::kWarning, &tracker,
                                  source.RangeOfString(platform->first.loc),
                                  logger::FormatMsg(logger::MsgCat::kGuchhoJSON_InvalidPlatform,
                                                    Q(*str)));
                    }
                }
            }
            if (auto target = internal::GetProperty(build, "target")) {
                if (auto str = internal::GetString(target->first)) {
                    ApplyTarget(opts, *str, source, target->first.loc);
                }
            }
            if (auto minify = internal::GetProperty(build, "minify")) {
                if (auto value = internal::GetBool(minify->first)) {
                    opts.MinifyWhitespace  = *value;
                    opts.MinifyIdentifiers = *value;
                    opts.MinifySyntax      = *value;
} else if (auto* object =
                           std::get_if<std::shared_ptr<javascript::EObject>>(&minify->first.data)) {
                    // The user supplied an explicit object, so the unmentioned
                    // sub-options default to off rather than inheriting the
                    // built-in "minify: true" defaults.
                    opts.MinifyWhitespace  = false;
                    opts.MinifyIdentifiers = false;
                    opts.MinifySyntax      = false;
                    for (const auto& p : (*object)->properties) {
                        if (auto prop_value = internal::GetBool(p.value_or_nil)) {
                            if (PropertyKeyText(p) == "whitespace") {
                                opts.MinifyWhitespace = *prop_value;
                            } else if (PropertyKeyText(p) == "identifiers") {
                                opts.MinifyIdentifiers = *prop_value;
                            } else if (PropertyKeyText(p) == "syntax") {
                                opts.MinifySyntax = *prop_value;
                            }
                        }
                    }
                }
            }
            if (auto sourcemap = internal::GetProperty(build, "sourcemap")) {
                if (auto boolean = internal::GetBool(sourcemap->first)) {
                    opts.SourceMapData = *boolean ? config::SourceMap::kLinkedWithComment
                                                  : config::SourceMap::kNone;
                } else if (auto str = internal::GetString(sourcemap->first)) {
                    if (auto kind = ParseSourceMap(*str)) {
                        opts.SourceMapData = *kind;
                    } else {
                        log.AddID(logger::MsgID::kGuchhoJSON_InvalidSourcemap,
                                  logger::MsgKind::kWarning, &tracker,
                                  source.RangeOfString(sourcemap->first.loc),
                                  logger::FormatMsg(logger::MsgCat::kGuchhoJSON_InvalidSourcemap,
                                                    Q(*str)));
                    }
                }
            }
            if (auto splitting = internal::GetProperty(build, "splitting")) {
                if (auto value = internal::GetBool(splitting->first)) {
                    opts.CodeSplitting = *value;
                }
            }
            if (auto tree_shaking = internal::GetProperty(build, "treeShaking")) {
                if (auto value = internal::GetBool(tree_shaking->first)) {
                    opts.TreeShaking = *value;
                }
            }
            // "build.clean" is handled by the CLI/build runner, not "opts".

            WarnUnknownFields(log, build, source, tracker, "build.", kKnownBuildFields,
                              sizeof(kKnownBuildFields) / sizeof(kKnownBuildFields[0]));
        }

        // ---- "resolve" ---------------------------------------------------
        static const char* kKnownResolveFields[] = {"extensions", "alias"};
        if (auto resolve_prop = internal::GetProperty(json, "resolve")) {
            const javascript::Expr& resolve = resolve_prop->first;

            if (auto extensions = internal::GetProperty(resolve, "extensions")) {
                if (auto* arr =
                        std::get_if<std::shared_ptr<javascript::EArray>>(&extensions->first.data)) {
                    opts.ExtensionOrder.clear();
                    for (const auto& item : (*arr)->items) {
                        if (auto str = internal::GetString(item)) {
                            if (!str->empty()) {
                                opts.ExtensionOrder.push_back(
                                    (*str)[0] == '.' ? *str : "." + *str);
                            }
                        }
                    }
                }
            }
            if (auto alias = internal::GetProperty(resolve, "alias")) {
                if (auto* object =
                        std::get_if<std::shared_ptr<javascript::EObject>>(&alias->first.data)) {
                    for (const auto& p : (*object)->properties) {
                        std::string key = PropertyKeyText(p);
                        auto        str = internal::GetString(p.value_or_nil);
                        if (!key.empty() && str) {
                            opts.PackageAliases[key] = JoinAbsConfigDir(config_dir, *str);
                        }
                    }
                }
            }

            WarnUnknownFields(log, resolve, source, tracker, "resolve.", kKnownResolveFields,
                              sizeof(kKnownResolveFields) / sizeof(kKnownResolveFields[0]));
        }

        // ---- "css" -------------------------------------------------------
        static const char* kKnownCSSFields[] = {"modules", "minify", "sourceComments"};
        if (auto css_prop = internal::GetProperty(json, "css")) {
            const javascript::Expr& css = css_prop->first;

            if (auto modules = internal::GetProperty(css, "modules")) {
                // CSS modules are not yet wired into the linker.
                (void)modules;
            }
            if (auto minify = internal::GetProperty(css, "minify")) {
                if (auto value = internal::GetBool(minify->first)) {
                    // CSS minification shares the JS minify flags in current Guchho.
                    opts.MinifyWhitespace = opts.MinifyWhitespace || *value;
                    opts.MinifySyntax     = opts.MinifySyntax || *value;
                }
            }
            if (auto source_comments = internal::GetProperty(css, "sourceComments")) {
                // Recorded for future CSS-aware source-comment emission.
                (void)source_comments;
            }

            WarnUnknownFields(log, css, source, tracker, "css.", kKnownCSSFields,
                              sizeof(kKnownCSSFields) / sizeof(kKnownCSSFields[0]));
        }

        // ---- "assets" ----------------------------------------------------
        static const char* kKnownAssetsFields[] = {"copy", "inlineLimit"};
        if (auto assets_prop = internal::GetProperty(json, "assets")) {
            const javascript::Expr& assets = assets_prop->first;
            // Both fields are handled by the bundler output pass; the loader
            // validates their types so configs still fail loudly when wrong.
            if (auto copy = internal::GetProperty(assets, "copy")) {
                (void)internal::GetBool(copy->first);
            }
            if (auto inline_limit = internal::GetProperty(assets, "inlineLimit")) {
                (void)std::get_if<std::shared_ptr<javascript::ENumber>>(&inline_limit->first.data);
            }

            WarnUnknownFields(log, assets, source, tracker, "assets.", kKnownAssetsFields,
                              sizeof(kKnownAssetsFields) / sizeof(kKnownAssetsFields[0]));
        }

        // ---- "define" ----------------------------------------------------
        if (auto define_prop = internal::GetProperty(json, "define")) {
            if (auto* object =
                    std::get_if<std::shared_ptr<javascript::EObject>>(&define_prop->first.data)) {
                for (const auto& p : (*object)->properties) {
                    std::string key = PropertyKeyText(p);
                    if (key.empty()) continue;
                    auto value_text = DefineValueText(p.value_or_nil);
                    if (!value_text) {
                        log.AddID(logger::MsgID::kGuchhoJSON_InvalidFormat,
                                  logger::MsgKind::kWarning, &tracker,
                                  source.RangeOfString(p.value_or_nil.loc),
                                  logger::FormatMsg(logger::MsgCat::kGuchhoJSON_UnsupportedDefineValue,
                                                    Q(key)));
                        continue;
                    }
                    auto [expr, injected] = javascript::ParseDefineExpr(*value_text);
                    if (injected != nullptr) {
                        // Compound expressions are not inlined; esbuild injects
                        // them out-of-line. That path needs an injected file, so
                        // only scalar defines are supported by the config loader.
                        log.AddID(logger::MsgID::kGuchhoJSON_InvalidFormat,
                                  logger::MsgKind::kWarning, &tracker,
                                  source.RangeOfString(p.value_or_nil.loc),
                                  logger::FormatMsg(logger::MsgCat::kGuchhoJSON_UnsupportedCompoundDefineValue,
                                                    Q(key)));
                        continue;
                    }
                    config::DefineData data;
                    data.KeyParts       = SplitDefineKey(key);
                    data.DefineExprData = std::make_shared<config::DefineExpr>(std::move(expr));
                    user_defines.push_back(std::move(data));
                }
            }
        }

        // ---- "external" --------------------------------------------------
        if (auto external_prop = internal::GetProperty(json, "external")) {
            if (auto* arr =
                    std::get_if<std::shared_ptr<javascript::EArray>>(&external_prop->first.data)) {
                for (const auto& item : (*arr)->items) {
                    if (auto str = internal::GetString(item)) {
                        opts.ExternalSettingsData.PreResolve.Exact[*str] = true;
                    }
                }
            } else if (auto str = internal::GetString(external_prop->first)) {
                opts.ExternalSettingsData.PreResolve.Exact[*str] = true;
            }
        }

        // ---- "output" ----------------------------------------------------
        static const char* kKnownOutputFields[] = {
            "entryFileNames", "chunkFileNames", "assetFileNames",
        };
        if (auto output_prop = internal::GetProperty(json, "output")) {
            const javascript::Expr& output = output_prop->first;
            if (auto names = internal::GetProperty(output, "entryFileNames")) {
                if (auto str = internal::GetString(names->first)) {
                    opts.EntryPathTemplate = ParsePathTemplate(*str);
                }
            }
            if (auto names = internal::GetProperty(output, "chunkFileNames")) {
                if (auto str = internal::GetString(names->first)) {
                    opts.ChunkPathTemplate = ParsePathTemplate(*str);
                }
            }
            if (auto names = internal::GetProperty(output, "assetFileNames")) {
                if (auto str = internal::GetString(names->first)) {
                    opts.AssetPathTemplate = ParsePathTemplate(*str);
                }
            }

            WarnUnknownFields(log, output, source, tracker, "output.", kKnownOutputFields,
                              sizeof(kKnownOutputFields) / sizeof(kKnownOutputFields[0]));
        }

        // ---- "logLevel" --------------------------------------------------
        if (auto level = internal::GetProperty(json, "logLevel")) {
            if (auto str = internal::GetString(level->first)) {
                std::string lower = helpers::ToLowerASCII(*str);
                if (lower == "silent" || lower == "error" || lower == "warn" ||
                    lower == "warning" || lower == "info" || lower == "debug") {
                    (void)lower; // The logger level is applied by the CLI/build runner.
                } else {
                    log.AddID(logger::MsgID::kGuchhoJSON_InvalidLogLevel,
                              logger::MsgKind::kWarning, &tracker,
                              source.RangeOfString(level->first.loc),
                              logger::FormatMsg(logger::MsgCat::kGuchhoJSON_InvalidLogLevelValue,
                              Q(*str)));
                }
            }
        }

        // ---- "plugins" ---------------------------------------------------
        // The plugin system is not implemented yet, and JS functions cannot be
        // transported through JSON anyway. Warn so configs do not silently
        // lose behavior.
        if (auto plugins = internal::GetProperty(json, "plugins")) {
            if (auto* arr = std::get_if<std::shared_ptr<javascript::EArray>>(&plugins->first.data)) {
                if (!(*arr)->items.empty()) {
                    log.AddID(logger::MsgID::kGuchhoConfig_PluginsIgnored,
                              logger::MsgKind::kWarning, &tracker,
                              source.RangeOfString(plugins->second),
                              "Guchho plugins are not implemented yet; the \"plugins\" field "
                              "will be ignored");
                }
            } else if (auto* object =
                           std::get_if<std::shared_ptr<javascript::EObject>>(&plugins->first.data)) {
                if (!(*object)->properties.empty()) {
                    log.AddID(logger::MsgID::kGuchhoConfig_PluginsIgnored,
                              logger::MsgKind::kWarning, &tracker,
                              source.RangeOfString(plugins->second),
                              "Guchho plugins are not implemented yet; the \"plugins\" field "
                              "will be ignored");
                }
            }
        }

        // ---- Unknown top-level fields ------------------------------------
        static const char* kKnownTopFields[] = {
            "build", "resolve", "css", "assets", "define", "external",
            "output", "watch", "server", "logLevel", "plugins", "root",
        };
        WarnUnknownFields(log, json, source, tracker, "", kKnownTopFields,
                          sizeof(kKnownTopFields) / sizeof(kKnownTopFields[0]));

        (void)fs;
    }

    GuchhoConfig LoadGuchhoConfigFromText(
        logger::Log&       log,
        cache::JSONCache&  json_cache,
        filesystem::Fs&    fs,
        config::Options&   opts,
        const std::string& json_text,
        const std::string& config_path)
    {
        GuchhoConfig result;
        result.opts = opts;

        logger::Path key_path;
        if (config_path.empty()) {
            key_path.text       = "<guchho config>";
            key_path.namespace_ = "guchho-config";
        } else {
            key_path.text       = config_path;
            key_path.namespace_ = "file";
        }

        logger::Source source;
        source.pretty_paths = MakePrettyPaths(fs, key_path);
        source.key_path     = key_path;
        source.contents     = json_text;

        javascript::JSONOptions json_options;
        json_options.flavor = javascript::JSONFlavor::kJSON;
        auto [json, ok]     = json_cache.Parse(log, source, json_options);
        if (!ok) {
            result.parse_error = true;
            return result;
        }

        std::string config_dir = config_path.empty() ? "" : fs.Dir(config_path);
        result.config_dir      = config_dir;
        result.config_path     = config_path;
        result.found           = true;

        std::vector<config::DefineData> user_defines;
        ApplyGuchhoJsonConfig(log, fs, result.opts, result.entry_points, user_defines,
                              json, config_dir, source);

        result.defines_owned = std::make_unique<config::ProcessedDefines>(
            config::ProcessDefines(user_defines));
        result.opts.Defines = result.defines_owned.get();

        return result;
    }

    GuchhoConfig LoadGuchhoConfigFromFile(
        logger::Log&       log,
        cache::JSONCache&  json_cache,
        filesystem::Fs&    fs,
        config::Options&   opts,
        const std::string& file_path)
    {
        GuchhoConfig result;
        result.opts = opts;

        logger::Path key_path;
        key_path.text       = file_path;
        key_path.namespace_ = "file";

        filesystem::FsResult<std::string> contents = fs.ReadFile(file_path);
        if (!contents.Ok()) {
            // A missing config file at an explicit path is an error, but a
            // missing file during upward discovery is expected, so callers use
            // the debug-level diagnostic path via "LoadGuchhoConfig".
            logger::PrettyPaths pretty = MakePrettyPaths(fs, key_path);
            log.AddID(logger::MsgID::kGuchhoJSON_Missing, logger::MsgKind::kDebug, nullptr,
                      logger::Range{},
                      logger::FormatMsg(logger::MsgCat::kGuchhoJSON_CannotReadFile,
                             std::string(Q(pretty.Select(opts.LogPathStyle))),
                             contents.original_error));
            return result;
        }

        return LoadGuchhoConfigFromText(log, json_cache, fs, opts, contents.value, file_path);
    }

    GuchhoConfig LoadGuchhoConfig(
        logger::Log&       log,
        cache::JSONCache&  json_cache,
        filesystem::Fs&    fs,
        config::Options&   opts,
        const std::string& start_dir)
    {
        // Discovery starts from the built-in defaults rooted at "start_dir".
        // A found config file overlays only its explicitly-specified fields
        // onto these defaults, so an omitted field always keeps its default.
        // The caller's "opts" is only consulted for diagnostic path-style
        // settings; the configuration base is always the built-in defaults.
        GuchhoConfig result = CreateDefaultGuchhoConfig(start_dir);
        result.opts.LogPathStyle       = opts.LogPathStyle;
        result.opts.CodePathStyle      = opts.CodePathStyle;
        result.opts.MetafilePathStyle  = opts.MetafilePathStyle;
        result.opts.SourcemapPathStyle = opts.SourcemapPathStyle;

        std::string dir = start_dir;
        if (!fs.IsAbs(dir)) {
            if (auto abs = fs.Abs(dir)) {
                dir = *abs;
            }
        }

        // The discovery never merges configs and never silently skips a file
        // that exists. The primary "guchho.config.js" takes precedence over
        // the JSON spellings across the whole walk: a JS config found in any
        // parent overrides a valid JSON config from a deeper directory. A
        // JSON config is only a fallback when no JS config exists anywhere.
        // A file that exists but cannot be read or parsed is FOUND + INVALID
        // and stops discovery immediately -- it is never skipped in favor of
        // a parent config or the built-in defaults.
        std::optional<GuchhoConfig> json_fallback;

        auto invalid_found = [&](const std::string& candidate) -> GuchhoConfig {
            GuchhoConfig invalid;
            invalid.opts          = result.opts;
            invalid.entry_points  = result.entry_points;
            invalid.config_dir    = fs.Dir(candidate);
            invalid.config_path   = candidate;
            invalid.found         = true;
            invalid.parse_error   = true;
            return invalid;
        };

        while (true) {
            filesystem::FsResult<filesystem::DirEntries> read = fs.ReadDirectory(dir);
            if (read.Ok()) {
                for (const char* name : kConfigFileNames) {
                    auto [entry, diff_case] = read.value.Get(name);
                    (void)diff_case;
                    if (entry && entry->Kind(fs) == filesystem::EntryKind::kFile) {
                        std::string candidate = fs.Join({dir, name});

                        if (IsJSConfigName(name)) {
                            GuchhoConfig js_result = (GetGuchhoConfigJSLoader())(
                                log, json_cache, fs, result.opts, candidate);
                            if (js_result.found) {
                                if (js_result.entry_points.empty()) {
                                    // "build.entry" was not specified, so the
                                    // default entry (index.html rooted at the
                                    // discovery start) stays in effect.
                                    js_result.entry_points = result.entry_points;
                                }
                                return js_result;
                            }
                            // The file exists but could not be evaluated or
                            // parsed. That is FOUND + INVALID: report it and
                            // stop, never falling through to the other config
                            // files in this directory or to parent directories.
                            return invalid_found(candidate);
                        }

                        GuchhoConfig json_result =
                            LoadGuchhoConfigFromFile(log, json_cache, fs, result.opts, candidate);
                        if (!json_result.found || json_result.parse_error) {
                            // The file exists (confirmed above) but could not
                            // be read or parsed. That is FOUND + INVALID;
                            // never continue to other files or to parents.
                            return invalid_found(candidate);
                        }
                        if (!json_fallback) {
                            // Keep the nearest valid JSON config. It is not
                            // returned yet: a JS config in a parent directory
                            // still takes precedence over it.
                            json_fallback = std::move(json_result);
                            if (json_fallback->entry_points.empty()) {
                                // "build.entry" was not specified, so the
                                // default entry (index.html rooted at the
                                // discovery start) stays in effect.
                                json_fallback->entry_points = result.entry_points;
                            }
                        }
                    }
                }
            }

            std::string parent = fs.Dir(dir);
            if (parent == dir) {
                // Reached the filesystem root: use the nearest valid JSON
                // config found while walking, or the built-in defaults when
                // no config file was seen at all.
                if (json_fallback) {
                    return std::move(*json_fallback);
                }
                return result;
            }
            dir = parent;
        }
    }

    GuchhoConfig LoadGuchhoConfigFromJS(
        logger::Log&       log,
        cache::JSONCache&  json_cache,
        filesystem::Fs&    fs,
        config::Options&   opts,
        const std::string& file_path)
    {
        GuchhoConfig result;
        result.opts = opts;

        logger::Path key_path;
        key_path.text       = file_path;
        key_path.namespace_ = "file";

        logger::PrettyPaths pretty = MakePrettyPaths(fs, key_path);

        // A missing file at an explicit path is reported by the caller:
        // discovery only routes to this loader for files it just found.
        std::string cwd = fs.Dir(file_path);
        std::string node     = NodeBinary();
        std::string script   = BuildConfigJSScript(file_path);
        helpers::ProcessResult run = helpers::RunProcess({node, "-e", script}, cwd);

        if (!run.started) {
            log.AddID(logger::MsgID::kGuchhoConfig_SpawnFailed, logger::MsgKind::kWarning, nullptr,
                      logger::Range{},
                      logger::FormatMsg(logger::MsgCat::kGuchhoConfig_FailedToSpawn,
                                        Q(node), Q(pretty.Select(opts.LogPathStyle))));
            return result;
        }

        if (run.exit_code != 0) {
            std::string detail = run.stderr_data;
            if (!detail.empty() && detail.back() == '\n') {
                detail.pop_back();
            }
            log.AddID(logger::MsgID::kGuchhoConfig_EvalFailed, logger::MsgKind::kWarning, nullptr,
                      logger::Range{},
                      logger::FormatMsg(logger::MsgCat::kGuchhoConfig_FailedToEvaluate,
                                        Q(pretty.Select(opts.LogPathStyle)),
                                        (detail.empty() ? "" : ": " + detail)));
            return result;
        }

        if (run.stdout_data.empty()) {
            log.AddID(logger::MsgID::kGuchhoConfig_InvalidOutput, logger::MsgKind::kWarning,
                      nullptr, logger::Range{},
                      logger::FormatMsg(logger::MsgCat::kGuchhoConfig_DidNotProduceConfigObject,
                                        Q(pretty.Select(opts.LogPathStyle))));
            return result;
        }

        GuchhoConfig parsed = LoadGuchhoConfigFromText(log, json_cache, fs, opts,
                                                       run.stdout_data, file_path);
        if (parsed.parse_error) {
            log.AddID(logger::MsgID::kGuchhoConfig_InvalidOutput, logger::MsgKind::kWarning,
                      nullptr, logger::Range{},
                      logger::FormatMsg(logger::MsgCat::kGuchhoConfig_NotJSONObject,
                                        Q(pretty.Select(opts.LogPathStyle))));
            return parsed;
        }

        return parsed;
    }

    GuchhoConfigJSLoader GetGuchhoConfigJSLoader()
    {
        return g_js_loader;
    }

    GuchhoConfigJSLoader SetGuchhoConfigJSLoader(GuchhoConfigJSLoader loader)
    {
        GuchhoConfigJSLoader previous = g_js_loader;
        g_js_loader                   = loader;
        return previous;
    }

} // namespace guchho::resolver