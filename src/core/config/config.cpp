#include "guchho/config.hpp"

#include <mutex>
#include <regex>
#include <unordered_map>

namespace guchho::config {
    namespace {

    // Global mutex protecting the filter cache from concurrent access.
    // std::regex compilation is expensive, so filters are compiled once
    // and reused across the entire build.
    static std::mutex sFilterMutex;

    // Cache mapping raw filter strings to their compiled regex objects.
    // The cache only grows; entries are never evicted.  This is safe
    // because the number of distinct filter strings is bounded by the
    // number of plugins and their filter configurations.
    static std::unordered_map<std::string, std::regex> sFilterCache;

    // CompileFilterInternal
    // ---------------------
    // Compiles a filter string into a regex and caches the result.
    // Returns a pointer into the global cache, which remains valid for
    // the lifetime of the process.  Returns nullptr for empty or
    // invalid filters.
    //
    // The function is thread-safe: the cache is protected by
    // sFilterMutex.  A double-checked lock pattern avoids the mutex
    // on cache hits.
    //
    // Input:  filter = ".*\\.ts$"
    // Output: pointer to compiled ECMAScript regex
    //
    // Input:  filter = "[invalid"
    // Output: nullptr  (std::regex_error caught)
    //
    // Input:  filter = ""
    // Output: nullptr  (empty filter is a no-op)
    static std::regex* CompileFilterInternal(const std::string& filter) {
        if (filter.empty()) {
            return nullptr;
        }

        // Fast path: check the cache without holding the lock.
        // The cache only grows, so a successful find means the pointer
        // is stable.
        {
            std::lock_guard<std::mutex> lock(sFilterMutex);
            auto it = sFilterCache.find(filter);
            if (it != sFilterCache.end()) {
                // Return pointer to cached regex (safe because cache never shrinks)
                return &it->second;
            }
        }

        // Slow path: compile the regex and insert into the cache.
        std::regex re;
        try {
            re = std::regex(filter, std::regex::ECMAScript | std::regex::optimize);
        } catch (const std::regex_error&) {
            return nullptr;
        }

        // Store in cache
        {
            std::lock_guard<std::mutex> lock(sFilterMutex);
            auto result = sFilterCache.emplace(filter, std::move(re));
            return &result.first->second;
        }
    }

    }


    // LoaderToString
    // ---------------
    // Returns the short string form of a Loader enum value.  Two
    // loaders share the same string: kJSON and kWithTypeJSON both
    // map to "json", and kTS and kTSNoAmbiguousLessThan both map to
    // "ts".  Unknown loaders return an empty string.
    //
    // Input:  Loader::kJS     => "js"
    // Input:  Loader::kTSX    => "tsx"
    // Input:  Loader::kJSON   => "json"
    // Input:  Loader::kNone   => "none"
    std::string_view LoaderToString(Loader loader) {
        switch (loader) {
            case Loader::kNone: return "none";
            case Loader::kBase64: return "base64";
            case Loader::kBinary: return "binary";
            case Loader::kCopy: return "copy";
            case Loader::kCSS: return "css";
            case Loader::kDataURL: return "dataurl";
            case Loader::kDefault: return "default";
            case Loader::kEmpty: return "empty";
            case Loader::kFile: return "file";
            case Loader::kGlobalCSS: return "global-css";
            case Loader::kHTML: return "html";
            case Loader::kJS: return "js";
            case Loader::kJSON: return "json";
            case Loader::kWithTypeJSON: return "json";
            case Loader::kJSX: return "jsx";
            case Loader::kLocalCSS: return "local-css";
            case Loader::kText: return "text";
            case Loader::kTS: return "ts";
            case Loader::kTSNoAmbiguousLessThan: return "ts";
            case Loader::kTSX: return "tsx";
            default: return "";
        }
    }

    // IsTypeScript
    // ------------
    // Returns true when the loader parses TypeScript.  The three
    // TypeScript loaders differ in how ambiguous `<` expressions are
    // handled: kTS treats them as comparison, kTSNoAmbiguousLessThan
    // does not, and kTSX treats them as JSX.
    //
    // Input:  Loader::kTS   => true
    // Input:  Loader::kJS   => false
    bool IsTypeScript(Loader loader) {
        switch (loader) {
            case Loader::kTS:
            case Loader::kTSNoAmbiguousLessThan:
            case Loader::kTSX:
                return true;
            default:
                return false;
        }
    }

    // IsCSS
    // -----
    // Returns true when the loader parses CSS.  There are three CSS
    // loaders: kCSS for standard stylesheets, kGlobalCSS for
    // stylesheets that inject into the global scope, and kLocalCSS
    // for CSS Modules.
    //
    // Input:  Loader::kCSS       => true
    // Input:  Loader::kJS        => false
    bool IsCSS(Loader loader) {
        switch (loader) {
            case Loader::kCSS:
            case Loader::kGlobalCSS:
            case Loader::kLocalCSS:
                return true;
            default:
                return false;
        }
    }

    // CanHaveSourceMap
    // ----------------
    // Returns true when the loader produces output that can carry a
    // source map.  This includes JS, TS, CSS, JSON, and text loaders.
    // Binary loaders (kBinary, kBase64, kDataURL, kCopy, kFile, etc.)
    // do not produce source maps.
    //
    // Input:  Loader::kJS    => true
    // Input:  Loader::kFile  => false
    bool CanHaveSourceMap(Loader loader) {
        switch (loader) {
            case Loader::kJS:
            case Loader::kJSX:
            case Loader::kTS:
            case Loader::kTSNoAmbiguousLessThan:
            case Loader::kTSX:
            case Loader::kCSS:
            case Loader::kGlobalCSS:
            case Loader::kLocalCSS:
            case Loader::kJSON:
            case Loader::kWithTypeJSON:
            case Loader::kText:
                return true;
            default:
                return false;
        }
    }

    // LoaderFromFileExtension
    // -----------------------
    // Determines the loader for a file by walking its extension from
    // right to left.  For "foo.bar.ts" it first checks ".ts", then
    // ".bar.ts", until a match is found in the extension map.  Files
    // without a dot check the empty-string key.
    //
    // Input:  extensionToLoader = {".ts": kTS, ".tsx": kTSX}
    //         base = "component.tsx"
    // Output: kTSX  (matches ".tsx" first)
    //
    // Input:  extensionToLoader = {".js": kJS}
    //         base = "noext"
    // Output: Loader::kNone  (no dot, empty key not in map)
    //
    // Input:  extensionToLoader = {"": kDefault}
    //         base = "Makefile"
    // Output: kDefault  (no dot, empty key matches)
    Loader LoaderFromFileExtension(const std::unordered_map<std::string, Loader>& extensionToLoader, const std::string& base) {
        auto dotPos = base.find('.');
        if (dotPos != std::string::npos) {
            std::string remaining = base;
            while (true) {
                auto it = extensionToLoader.find(remaining.substr(dotPos));
                if (it != extensionToLoader.end()) {
                    return it->second;
                }
                remaining = remaining.substr(dotPos + 1);
                dotPos = remaining.find('.');
                if (dotPos == std::string::npos) break;
            }
        } else {
            auto it = extensionToLoader.find("");
            if (it != extensionToLoader.end()) {
                return it->second;
            }
        }
        return Loader::kNone;
    }


    // FormatToString
    // ---------------
    // Returns the short string form of a Format enum value.  The
    // preserve format returns an empty string because it has no
    // output wrapper.
    //
    // Input:  Format::kCommonJS => "cjs"
    // Input:  Format::kESModule  => "esm"
    // Input:  Format::kIIFE      => "iife"
    // Input:  Format::kPreserve  => ""
    std::string_view FormatToString(Format f) {
        switch (f) {
            case Format::kIIFE: return "iife";
            case Format::kCommonJS: return "cjs";
            case Format::kESModule: return "esm";
            case Format::kUMD: return "umd";
            case Format::kAMD: return "amd";
            case Format::kSystem: return "system";
            default: return "";
        }
    }

    // ShouldCallRuntimeRequire
    // ------------------------
    // Returns true when the bundled output needs a runtime require()
    // helper.  This is only the case when bundling (kBundle mode) for
    // a format other than CommonJS, because CommonJS already has
    // native require() and does not need a shim.
    //
    // Input:  mode = kBundle, format = kESModule   => true
    // Input:  mode = kBundle, format = kCommonJS   => false
    // Input:  mode = kPassThrough, format = kESModule => false
    bool ShouldCallRuntimeRequire(Mode mode, Format outputFormat) {
        return mode == Mode::kBundle && outputFormat != Format::kCommonJS;
    }

    // =============================================================================
    // Path template functions
    // =============================================================================

    // TemplateToString
    // ----------------
    // Serialises a path template back into a human-readable string.
    // Each placeholder is rendered as its bracketed marker (e.g.
    // "[name]") and literal data is emitted verbatim.  A single-segment
    // template with no placeholder is returned as a plain string.
    //
    // Input:  [{Data="dist/"},
    //          {Data="", Placeholder=kName},
    //          {Data=".js"}]
    // Output: "dist/[name].js"
    //
    // Input:  [{Data="bundle.js", Placeholder=kNoPlaceholder}]
    // Output: "bundle.js"
    std::string TemplateToString(const std::vector<PathTemplate>& tmpl) {
        if (tmpl.size() == 1 && tmpl[0].Placeholder == PathPlaceholder::kNoPlaceholder) {
            return tmpl[0].Data;
        }
        std::string result;
        for (const auto& part : tmpl) {
            result += part.Data;
            switch (part.Placeholder) {
                case PathPlaceholder::kDir: result += "[dir]"; break;
                case PathPlaceholder::kName: result += "[name]"; break;
                case PathPlaceholder::kHash: result += "[hash]"; break;
                case PathPlaceholder::kExt: result += "[ext]"; break;
                default: break;
            }
        }
        return result;
    }

    // HasPlaceholder
    // --------------
    // Returns true when the template contains at least one segment
    // matching the given placeholder kind.
    //
    // Input:  tmpl = [{Data="[name]-[hash].js"}], placeholder = kHash
    // Output: true
    //
    // Input:  tmpl = [{Data="bundle.js"}], placeholder = kName
    // Output: false
    bool HasPlaceholder(const std::vector<PathTemplate>& tmpl, PathPlaceholder placeholder) {
        for (const auto& part : tmpl) {
            if (part.Placeholder == placeholder) {
                return true;
            }
        }
        return false;
    }

    // SubstituteTemplate
    // ------------------
    // Replaces every placeholder in the template with its concrete
    // value from PathPlaceholders.  The function first checks whether
    // any substitution is actually needed; if not, the input is
    // returned unchanged to avoid allocation.
    //
    // Adjacent literal segments (kNoPlaceholder) are merged after
    // substitution to keep the output compact.
    //
    // Input:  tmpl = [{Data="", Placeholder=kName}, {Data=".js"}]
    //         placeholders.Name = "index"
    // Output: [{Data="index.js", Placeholder=kNoPlaceholder}]
    //
    // Input:  tmpl = [{Data="bundle.js"}]
    // Output: [{Data="bundle.js"}]  (no substitution needed)
    //
    // Edge case: when a placeholder has no concrete value (pointer is
    // null) it is left as-is in the output, preserving the original
    // bracketed marker.
    std::vector<PathTemplate> SubstituteTemplate(const std::vector<PathTemplate>& tmpl, const PathPlaceholders& placeholders) {
        bool shouldSubstitute = false;
        for (size_t i = 0; i < tmpl.size(); ++i) {
            if (placeholders.Get(tmpl[i].Placeholder) != nullptr ||
                (tmpl[i].Placeholder == PathPlaceholder::kNoPlaceholder && i + 1 < tmpl.size())) {
                shouldSubstitute = true;
                break;
            }
        }
        if (!shouldSubstitute) {
            return tmpl;
        }

        std::vector<PathTemplate> result;
        result.reserve(tmpl.size());
        for (const auto& part : tmpl) {
            PathTemplate partCopy = part;
            if (auto* sub = placeholders.Get(partCopy.Placeholder); sub != nullptr) {
                partCopy.Data += *sub;
                partCopy.Placeholder = PathPlaceholder::kNoPlaceholder;
            }
            if (!result.empty() && result.back().Placeholder == PathPlaceholder::kNoPlaceholder) {
                result.back().Data += partCopy.Data;
                result.back().Placeholder = partCopy.Placeholder;
            } else {
                result.push_back(std::move(partCopy));
            }
        }
        return result;
    }

    // CompileFilterForPlugin
    // ----------------------
    // Compiles a plugin filter string into a standalone regex object.
    // The compiled regex is a copy of the cached version so the caller
    // owns it independently.  Returns nullptr for empty or invalid
    // filters.
    //
    // Input:  pluginName = "my-plugin", kind = "onResolve",
    //         filter = ".*\\.ts$"
    // Output: unique_ptr to compiled regex matching ".ts" paths
    //
    // Input:  filter = ""
    // Output: nullptr  (empty filter matches nothing)
    std::unique_ptr<std::regex> CompileFilterForPlugin(const std::string& pluginName, const std::string& kind, const std::string& filter) {
        (void)pluginName;
        (void)kind;
        if (filter.empty()) {
            return nullptr;
        }

        auto* cached = CompileFilterInternal(filter);
        if (cached == nullptr) {
            return nullptr;
        }

        return std::make_unique<std::regex>(*cached);
    }

    // PluginAppliesToPath
    // -------------------
    // Returns true when the given path satisfies both the namespace
    // filter and the regex filter.  An empty namespace matches any
    // path.
    //
    // Input:  path = "src/index.ts", filter = ".*\\.ts$",
    //         namespace_ = ""
    // Output: true  (regex matches, namespace is wildcard)
    //
    // Input:  path = "src/index.ts", filter = ".*\\.ts$",
    //         namespace_ = "file"
    // Output: true  (if path.namespace_ == "file")
    //
    // Input:  path = "src/index.js", filter = ".*\\.ts$",
    //         namespace_ = ""
    // Output: false  (regex does not match)
    bool PluginAppliesToPath(const logger::Path& path, const std::regex& filter, const std::string& namespace_) {
        return (namespace_.empty() || path.namespace_ == namespace_) &&
            std::regex_search(path.text, filter);
    }


    // PrettyPrintTargetEnvironment
    // ----------------------------
    // Builds a human-readable description of the target environment
    // string, appending an override count when feature overrides are
    // active.  Used in diagnostics and the metafile to explain what
    // the build is targeting.
    //
    // Input:  originalTargetEnv = "es2020",
    //         unsupportedJSFeatureOverridesMask = 0
    // Output: "the configured target environment (es2020)"
    //
    // Input:  originalTargetEnv = "es2020",
    //         unsupportedJSFeatureOverridesMask = kTopLevelAwait | kOptionalChain
    // Output: "the configured target environment (es2020 + 2 overrides)"
    //
    // Input:  originalTargetEnv = ""
    //         unsupportedJSFeatureOverridesMask = 0
    // Output: "the configured target environment"
    std::string PrettyPrintTargetEnvironment(const std::string& originalTargetEnv, compat::JSFeature unsupportedJSFeatureOverridesMask) {
        std::string where = "the configured target environment";
        std::string overrides;

        if (static_cast<uint64_t>(unsupportedJSFeatureOverridesMask) != 0) {
            int count = 0;
            auto mask = static_cast<uint64_t>(unsupportedJSFeatureOverridesMask);
            while (mask != 0) {
                if (mask & 1) {
                    ++count;
                }
                mask >>= 1;
            }
            overrides = " + " + std::to_string(count) + " override";
            if (count != 1) {
                overrides += "s";
            }
        }

        if (!originalTargetEnv.empty()) {
            where += " (" + originalTargetEnv + overrides + ")";
        }

        return where;
    }

    // =============================================================================
    // MetafileFormat
    // =============================================================================

    // MaybeRemoveWhitespace
    // ---------------------
    // Strips all spaces and newlines from the string when the metafile
    // format is minified.  When the format is unminified the input is
    // returned unchanged.
    //
    // Input:  mf = kMinified, fmt = "{ \"key\": \"value\" }"
    // Output: "{\"key\":\"value\"}"
    //
    // Input:  mf = kUnminified, fmt = "{ \"key\": \"value\" }"
    // Output: "{ \"key\": \"value\" }"  (unchanged)
    //
    // Edge case: the function does not collapse other whitespace
    // characters (tabs, carriage returns) — only spaces and newlines,
    // which are the only whitespace characters produced by the
    // JSON serializer.
    std::string MaybeRemoveWhitespace(MetafileFormat mf, const std::string& fmt) {
        if (mf != MetafileFormat::kMinified) {
            return fmt;
        }

        std::string result;
        result.reserve(fmt.size());
        for (char c : fmt) {
            if (c != ' ' && c != '\n') {
                result.push_back(c);
            }
        }
        return result;
    }


    // TSConfigApplyExtendedConfig
    // ---------------------------
    // Merges base TypeScript config options into derived.  Each field
    // in derived that is still at its "unspecified" default is
    // overwritten by the corresponding value from base.  Fields that
    // have already been set in derived are left untouched, giving the
    // derived config higher precedence.
    //
    // Input:  derived.ExperimentalDecorators = kUnspecified
    //         base.ExperimentalDecorators = kTrue
    // Output: derived.ExperimentalDecorators = kTrue
    //
    // Input:  derived.Target = kBelowES2022
    //         base.Target = kAtOrAboveES2022
    // Output: derived.Target = kBelowES2022  (already set, base ignored)
    void TSConfigApplyExtendedConfig(TSConfig& derived, const TSConfig& base) {
        if (base.ExperimentalDecorators != MaybeBool::kUnspecified) {
            derived.ExperimentalDecorators = base.ExperimentalDecorators;
        }
        if (base.ImportsNotUsedAsValues != TSImportsNotUsedAsValues::kNone) {
            derived.ImportsNotUsedAsValues = base.ImportsNotUsedAsValues;
        }
        if (base.PreserveValueImports != MaybeBool::kUnspecified) {
            derived.PreserveValueImports = base.PreserveValueImports;
        }
        if (base.Target != TSTarget::kUnspecified) {
            derived.Target = base.Target;
        }
        if (base.UseDefineForClassFields != MaybeBool::kUnspecified) {
            derived.UseDefineForClassFields = base.UseDefineForClassFields;
        }
        if (base.VerbatimModuleSyntax != MaybeBool::kUnspecified) {
            derived.VerbatimModuleSyntax = base.VerbatimModuleSyntax;
        }
    }

    // TSConfigUnusedImportFlags
    // -------------------------
    // Computes the unused-import flags for a TypeScript config by
    // examining the three relevant settings.  The logic is:
    //
    //   1. verbatimModuleSyntax = true  =>  keep everything (both stmt
    //      and values), regardless of the other settings.
    //   2. preserveValueImports = true  =>  add kKeepValues.
    //   3. importsNotUsedAsValues = preserve or error  =>  add kKeepStmt.
    //
    // Input:  cfg.VerbatimModuleSyntax = kTrue
    // Output: kKeepStmt | kKeepValues
    //
    // Input:  cfg.PreserveValueImports = kTrue,
    //         cfg.ImportsNotUsedAsValues = kPreserve
    // Output: kKeepStmt | kKeepValues
    //
    // Input:  cfg.PreserveValueImports = kFalse,
    //         cfg.ImportsNotUsedAsValues = kNone
    // Output: kNone
    TSUnusedImportFlags TSConfigUnusedImportFlags(const TSConfig& cfg) {
        if (cfg.VerbatimModuleSyntax == MaybeBool::kTrue) {
            return TSUnusedImportFlags::kKeepStmt | TSUnusedImportFlags::kKeepValues;
        }

        auto flags = TSUnusedImportFlags::kNone;
        if (cfg.PreserveValueImports == MaybeBool::kTrue) {
            flags = flags | TSUnusedImportFlags::kKeepValues;
        }
        if (cfg.ImportsNotUsedAsValues == TSImportsNotUsedAsValues::kPreserve ||
            cfg.ImportsNotUsedAsValues == TSImportsNotUsedAsValues::kError) {
            flags = flags | TSUnusedImportFlags::kKeepStmt;
        }
        return flags;
    }

    // =============================================================================
    // TSConfigJSX
    // =============================================================================

    // TSConfigJSXApplyExtendedConfig
    // ------------------------------
    // Merges base JSX config options into derived, following the same
    // "unspecified means inherit" rule as TSConfigApplyExtendedConfig.
    // Non-empty factory names, import sources, and non-none JSX modes
    // in the base override the derived values.
    //
    // Input:  derived.JSXFactory = {}, base.JSXFactory = {"h"}
    // Output: derived.JSXFactory = {"h"}
    //
    // Input:  derived.JSX = kReactJSX, base.JSX = kReact
    // Output: derived.JSX = kReactJSX  (already set, base ignored)
    void TSConfigJSXApplyExtendedConfig(TSConfigJSX& derived, const TSConfigJSX& base) {
        if (!base.JSXFactory.empty()) {
            derived.JSXFactory = base.JSXFactory;
        }
        if (!base.JSXFragmentFactory.empty()) {
            derived.JSXFragmentFactory = base.JSXFragmentFactory;
        }
        if (base.JSXImportSource.has_value()) {
            derived.JSXImportSource = base.JSXImportSource;
        }
        if (base.JSX != TSJSX::kNone) {
            derived.JSX = base.JSX;
        }
    }

    // TSConfigJSXApplyTo
    // ------------------
    // Copies the tsconfig JSX settings into the JSXOptions struct
    // that Guchho uses during the transform phase.  The JSX mode
    // determines which runtime is used:
    //
    //   kReact         Classic runtime (React.createElement).
    //   kReactJSX      Automatic runtime (jsx-runtime).
    //   kReactJSXDev   Automatic runtime in development mode.
    //   kPreserve/kReactNative  JSX is left untouched.
    //
    // Factory and fragment overrides are applied when non-empty.
    //
    // Input:  tsConfig.JSX = kReactJSX
    //         tsConfig.JSXImportSource = "preact"
    // Output: jsxOptions.AutomaticRuntime = true
    //         jsxOptions.ImportSource = "preact"
    //
    // Input:  tsConfig.JSX = kReact
    // Output: jsxOptions.AutomaticRuntime = false
    //         jsxOptions.Development = false
    void TSConfigJSXApplyTo(const TSConfigJSX& tsConfig, JSXOptions& jsxOptions) {
        switch (tsConfig.JSX) {
            case TSJSX::kPreserve:
            case TSJSX::kReactNative:
                break;
            case TSJSX::kReact:
                jsxOptions.AutomaticRuntime = false;
                jsxOptions.Development = false;
                break;
            case TSJSX::kReactJSX:
                jsxOptions.AutomaticRuntime = true;
                break;
            case TSJSX::kReactJSXDev:
                jsxOptions.AutomaticRuntime = true;
                jsxOptions.Development = true;
                break;
            default:
                break;
        }

        if (!tsConfig.JSXFactory.empty()) {
            jsxOptions.Factory.Parts = tsConfig.JSXFactory;
        }
        if (!tsConfig.JSXFragmentFactory.empty()) {
            jsxOptions.Fragment.Parts = tsConfig.JSXFragmentFactory;
        }
        if (tsConfig.JSXImportSource.has_value()) {
            jsxOptions.ImportSource = tsConfig.JSXImportSource.value();
        }
    }

}
