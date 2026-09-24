// This file is responsible for reading and interpreting "tsconfig.json", the
// project configuration file that Guchho consults whenever it transpiles
// TypeScript-style source. Guchho does not execute a compiler, so everything
// that can influence code generation — the syntax target, the strict-mode
// switches, the JSX pragmas, and the module path aliasing table — is read
// directly out of the JSON here and lifted into the typed settings structures
// the rest of the pipeline consumes.

#include "guchho/resolver.hpp"

#include "guchho/helpers.hpp"
#include "guchho/javascript/js_helpers.hpp"
#include "guchho/javascript/js_lexer.hpp"
#include "guchho/javascript/js_parser.hpp"
#include "guchho/logger.hpp"

namespace guchho::resolver {

    // Overlays the settings from an "extends" base config onto this one. "extends"
    // inheritance is strictly one directional: a child file picks up everything
    // its base declares, but its own declarations always win. Each field is
    // therefore copied across only when the receiving config does not already
    // define it. The compiler flags and JSX settings get the same treatment
    // through their own merge helpers.
    //
    //   Input:  base = config carrying "target": "es2022", "strict": true
    //            (invoked as myConfig.ApplyExtendedConfig(baseConfig))
    //   Output: myConfig now carries base's target, strict, paths, JSX, and
    //           settings unless the child explicitly overrode those fields.
    void TSConfigJSON::ApplyExtendedConfig(const TSConfigJSON& base)
    {
        if (base.ts_target_key.range.len > 0) {
            ts_target_key = base.ts_target_key;
        }
        if (base.ts_strict.has_value()) {
            ts_strict = base.ts_strict;
        }
        if (base.ts_always_strict.has_value()) {
            ts_always_strict = base.ts_always_strict;
        }
        if (base.base_url.has_value()) {
            base_url = base.base_url;
        }
        if (base.paths != nullptr) {
            paths             = base.paths;
            base_url_for_paths = base.base_url_for_paths;
        }
        config::TSConfigJSXApplyExtendedConfig(jsx_settings, base.jsx_settings);
        config::TSConfigApplyExtendedConfig(settings, base.settings);
    }

    // Reports the effective "alwaysStrict" setting for this config. The field
    // itself wins when it is present; "strict" is used as its fallback default.
    //
    //   Input:  { "strict": false, no "alwaysStrict" }
    //   Output: pointer to a false-valued TSAlwaysStrict structure
    //   Input:  neither field present
    //   Output: nullptr
    const config::TSAlwaysStrict* TSConfigJSON::TSAlwaysStrictOrStrict() const
    {
        if (ts_always_strict.has_value()) {
            return &*ts_always_strict;
        }

        // When "alwaysStrict" is missing it inherits the value of "strict":
        // strict mode implies alwaysStrict, so a config that turns either one
        // on ends up reporting alwaysStrict. With both absent there is no
        // effective value and null is returned.
        return ts_strict.has_value() ? &*ts_strict : nullptr;
    }

    namespace {

        // Expands the "${configDir}" substitution token that newer configs use in
        // "paths" and "baseUrl" values: a value beginning with the token is
        // rebased onto the directory that holds the config file itself. No
        // other substitution is attempted, so anything that does not start with
        // the token is returned verbatim.
        //
        //   Input:  value     = "${configDir}/dist/main.js"
        //           base_path = "/app"
        //   Output: "/app/dist/main.js"
        std::string GetSubstitutedPathWithConfigDirTemplate(filesystem::Fs& fs,                                                            std::string_view value,
                                                            const std::string& base_path)
        {
            constexpr std::string_view kTemplate = "${configDir}";
            if (value.compare(0, kTemplate.size(), kTemplate) == 0) {
                return fs.Join({base_path, "./" + std::string(value.substr(kTemplate.size()))});
            }
            return std::string(value);
        }

        // Breaks a dotted JSX pragma string into its individual member names. Every
        // segment must be a legal identifier; the first offender aborts the
        // split and yields an empty vector (plus a warning naming the
        // offending text) so the caller falls back to its default pragma.
        //
        //   Input:  text = "h"        ->  Output: ["h"]
        //   Input:  text = "h.jsx"    ->  Output: ["h", "jsx"]
        //   Input:  text = "a.b.c"    ->  Output: ["a", "b", "c"]
        //   Input:  text = "a.1.b"    ->  Output: []
        std::vector<std::string> ParseMemberExpressionForJSX(logger::Log&                 log,
                                                             const logger::Source&       source,
                                                             logger::LineColumnTracker*  tracker,
                                                             logger::Loc                 loc,
                                                             const std::string&          text)
        {
            if (text.empty()) {
                return {};
            }
            std::vector<std::string> parts;
            size_t                   start = 0;
            while (true) {
                size_t dot = text.find('.', start);
                std::string part = text.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
                if (!javascript::IsIdentifier(part)) {
                    logger::Range warn_range = source.RangeOfString(loc);
                    log.AddID(logger::MsgID::kTSConfigJSON_InvalidJSX, logger::MsgKind::kWarning, tracker,
                              warn_range,
                              logger::FormatMsg(logger::MsgCat::kTSConfig_InvalidJSXMember,
                                                helpers::QuoteForJSON(text, false)));
                    return {};
                }
                parts.push_back(std::move(part));
                if (dot == std::string::npos) {
                    break;
                }
                start = dot + 1;
            }
            return parts;
        }

        // Validates a "paths" key or one of its remapping targets: a pattern may
        // contain at most one '*' wildcard. Patterns drawn from a real config
        // almost always obey this, so violations are flagged with a warning and
        // the offending entry is discarded.
        //
        //   Input:  text = "src/*"  ->  Output: true
        //   Input:  text = "*/*"    ->  Output: true
        //   Input:  text = "*x*"    ->  Output: false
        bool IsValidTSConfigPathPattern(const std::string&         text,
                                        logger::Log&               log,
                                        const logger::Source&      source,
                                        logger::LineColumnTracker* tracker,
                                        logger::Loc                loc)
        {
            bool found_asterisk = false;
            for (char c : text) {
                if (c == '*') {
                    if (found_asterisk) {
                        logger::Range r = source.RangeOfString(loc);
                        log.AddID(logger::MsgID::kTSConfigJSON_InvalidPaths, logger::MsgKind::kWarning,
                                  tracker, r,
                                  logger::FormatMsg(logger::MsgCat::kTSConfig_InvalidPattern,
                                                    helpers::QuoteForJSON(text, false)));
                        return false;
                    }
                    found_asterisk = true;
                }
            }
            return true;
        }

        // True for either path separator style, '/' or '\', both of which are legal
        // inside "tsconfig.json" paths on Windows.
        inline bool IsSlash(char c)
        {
            return c == '/' || c == '\\';
        }

    } // namespace

    // Checks whether a path pattern is self-evidently absolute or rooted so
    // that it works without a global "baseUrl": "." and "..", "./"- and
    // "../"-style relative prefixes, rooted POSIX or UNC paths, and drive-letter
    // absolute paths all qualify. A bare relative pattern ("folder/*") has no
    // anchor without "baseUrl", so it is rejected with a warning pointing out
    // that the two must be paired. The line/column tracker is handed in by
    // reference and only materialized the first time a warning actually fires.
    //
    //   Input:  text = "./src/*"   ->  Output: true
    //   Input:  text = "/abs/*"    ->  Output: true
    //   Input:  text = "c:/abs/*"  ->  Output: true
    //   Input:  text = "src/*"     ->  Output: false (no baseUrl available)
    bool IsValidTSConfigPathNoBaseURLPattern(const std::string&          text,
                                             logger::Log&                log,
                                             const logger::Source&       source,
                                             logger::LineColumnTracker*& tracker,
                                             logger::Loc                 loc)
    {
            char c0 = '\0';
            char c1 = '\0';
            char c2 = '\0';
            size_t n = text.size();

            if (n > 0) {
                c0 = text[0];
                if (n > 1) {
                    c1 = text[1];
                    if (n > 2) {
                        c2 = text[2];
                    }
                }
            }

            // Relative "." or ".."
            if (c0 == '.' && (n == 1 || (n == 2 && c1 == '.'))) {
                return true;
            }

            // Relative "./" or "../" or ".\\" or "..\\"
            if (c0 == '.' && (IsSlash(c1) || (c1 == '.' && IsSlash(c2)))) {
                return true;
            }

            // Absolute POSIX "/" or UNC "\\"
            if (IsSlash(c0)) {
                return true;
            }

            // Absolute DOS "c:/" or "c:\\"
            if (((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z')) && c1 == ':' && IsSlash(c2)) {
                return true;
            }

            logger::Range r = source.RangeOfString(loc);
            if (tracker == nullptr) {
                tracker = new logger::LineColumnTracker(&source);
            }
            log.AddID(logger::MsgID::kTSConfigJSON_InvalidPaths, logger::MsgKind::kWarning, tracker, r,
                      logger::FormatMsg(logger::MsgCat::kTSConfig_NonRelativePath,
                                        helpers::QuoteForJSON(text, false)));
            return false;
    }

    // Parses one "tsconfig.json" file into the TSConfigJSON settings object that
    // later drives transpilation. Reading honours the "extends" chain via a
    // caller-supplied callback (which performs the file lookup and the actual
    // recursion), and every recognised "compilerOptions" entry is decoded in
    // place: values that matter as paths are absolutized immediately, booleans
    // become explicit tri-states, and source spans are captured so diagnostics
    // can quote the exact config text. A config file that cannot be read as
    // JSON yields null; defects inside installed modules are reported quietly.
    //
    //   Input:  file_dir    = "/app"  (directory of the file being built)
    //           config_dir  = "/app"  (directory of this config file)
    //   Output: a populated TSConfigJSON*, or nullptr when the file is not JSON
    TSConfigJSON* ParseTSConfigJSON(
        logger::Log&                   log,
        const logger::Source&          source,
        cache::JSONCache&              json_cache,
        filesystem::Fs&                fs,
        const std::string&             file_dir,
        const std::string&             config_dir,
        const TSConfigExtendsCallback& extends)
    {
        // Technically "tsconfig.json" defies strict JSON for the sake of the
        // TypeScript toolchain: comments and dangling commas are tolerated and
        // keys may go unquoted. The shared JSON parser is therefore switched
        // into the tsconfig flavor for exactly these files, which accepts the
        // lenient grammar; escape-handling edge cases in exotic configs may
        // still differ from a full config reader, but those are rare in the
        // wild.
        javascript::JSONOptions json_options;
        json_options.flavor = javascript::JSONFlavor::kTSConfigJSON;
        auto [json, ok]     = json_cache.Parse(log, source, json_options);
        if (!ok) {
            return nullptr;
        }

        TSConfigJSON* result = new TSConfigJSON;
        result->abs_path     = source.key_path.text;
        logger::LineColumnTracker tracker(&source);

        // "extends" names a base config to inherit from — a single path or an
        // ordered array of them. Each base is fetched through the callback,
        // folded in, and only then do this file's own fields get a chance to
        // override whatever was inherited.
        if (extends) {
            if (auto extends_json = internal::GetProperty(json, "extends")) {
                javascript::Expr value_json = extends_json->first;
                if (auto value = internal::GetString(value_json)) {
                    TSConfigJSON* base = extends(*value, source.RangeOfString(value_json.loc));
                    if (base != nullptr) {
                        result->ApplyExtendedConfig(*base);
                    }
                } else if (auto* array =
                               std::get_if<std::shared_ptr<javascript::EArray>>(&value_json.data)) {
                    for (const javascript::Expr& item : (*array)->items) {
                        if (auto str = internal::GetString(item)) {
                            TSConfigJSON* base = extends(*str, source.RangeOfString(item.loc));
                            if (base != nullptr) {
                                result->ApplyExtendedConfig(*base);
                            }
                        }
                    }
                }
            }
        }

        // All remaining options live under "compilerOptions". The block is walked
        // field by field, each read defensively so that an unexpected value
        // type merely leaves the compiler default in place.
        if (auto compiler_options_result = internal::GetProperty(json, "compilerOptions")) {
            javascript::Expr compiler_options_json = compiler_options_result->first;

            // "baseUrl" anchors resolution for path patterns that would otherwise be
            // untethered. The value is ${configDir}-substituted and absolutized
            // against the importing file's directory so it is never relative at
            // query time.
            if (auto base_url_result = internal::GetProperty(compiler_options_json, "baseUrl")) {
                javascript::Expr value_json = base_url_result->first;
                if (auto value = internal::GetString(value_json)) {
                    *value = GetSubstitutedPathWithConfigDirTemplate(fs, *value, config_dir);
                    if (!fs.IsAbs(*value)) {
                        *value = fs.Join({file_dir, *value});
                    }
                    result->base_url = std::move(*value);
                }
            }

            // The JSX transform mode, matched case-insensitively against the recognised
            // settings. Unrecognized spellings stay on the default rather than
            // producing a spurious error.
            if (auto jsx_result = internal::GetProperty(compiler_options_json, "jsx")) {
                javascript::Expr value_json = jsx_result->first;
                if (auto value = internal::GetString(value_json)) {
                    std::string lower_value = helpers::ToLowerASCII(*value);
                    if (lower_value == "preserve") {
                        result->jsx_settings.JSX = config::TSJSX::kPreserve;
                    } else if (lower_value == "react-native") {
                        result->jsx_settings.JSX = config::TSJSX::kReactNative;
                    } else if (lower_value == "react") {
                        result->jsx_settings.JSX = config::TSJSX::kReact;
                    } else if (lower_value == "react-jsx") {
                        result->jsx_settings.JSX = config::TSJSX::kReactJSX;
                    } else if (lower_value == "react-jsxdev") {
                        result->jsx_settings.JSX = config::TSJSX::kReactJSXDev;
                    }
                }
            }

            // The element factory's qualified name, e.g. "h" or "React.createElement";
            // split into its member parts by the shared helper below.
            if (auto factory_result = internal::GetProperty(compiler_options_json, "jsxFactory")) {
                javascript::Expr value_json = factory_result->first;
                if (auto value = internal::GetString(value_json)) {
                    result->jsx_settings.JSXFactory = ParseMemberExpressionForJSX(
                        log, source, &tracker, value_json.loc, *value);
                }
            }

            // A second pragma naming the fragment factory used for shorthand JSX
            // fragments; handled exactly like "jsxFactory".
            if (auto fragment_result =
                    internal::GetProperty(compiler_options_json, "jsxFragmentFactory")) {
                javascript::Expr value_json = fragment_result->first;
                if (auto value = internal::GetString(value_json)) {
                    result->jsx_settings.JSXFragmentFactory = ParseMemberExpressionForJSX(
                        log, source, &tracker, value_json.loc, *value);
                }
            }

            // The module whose automatic-runtime exports should back JSX ("react" by
            // default); recorded verbatim for later import rewriting.
            if (auto import_source_result =
                    internal::GetProperty(compiler_options_json, "jsxImportSource")) {
                javascript::Expr value_json = import_source_result->first;
                if (auto value = internal::GetString(value_json)) {
                    result->jsx_settings.JSXImportSource = std::move(*value);
                }
            }

            // Whether legacy decorator syntax (the pre-standard flavor) is accepted.
            // Stored as a tri-state so that true and explicit false remain
            // distinguishable from "unset".
            if (auto decorators_result =
                    internal::GetProperty(compiler_options_json, "experimentalDecorators")) {
                javascript::Expr value_json = decorators_result->first;
                if (auto value = internal::GetBool(value_json)) {
                    result->settings.ExperimentalDecorators =
                            *value ? config::MaybeBool::kTrue : config::MaybeBool::kFalse;
                }
            }

            // Whether class fields use the modern define semantics or the older
            // assignment semantics, kept as a tri-state for the same reason as
            // the decorator flag above.
            if (auto define_fields_result =
                    internal::GetProperty(compiler_options_json, "useDefineForClassFields")) {
                javascript::Expr value_json = define_fields_result->first;
                if (auto value = internal::GetBool(value_json)) {
                    result->settings.UseDefineForClassFields =
                            *value ? config::MaybeBool::kTrue : config::MaybeBool::kFalse;
                }
            }

            // "target" names the ECMA language version that syntactical lowering must
            // not go below.
            if (auto target_result = internal::GetProperty(compiler_options_json, "target")) {
                javascript::Expr value_json = target_result->first;
                logger::Loc      key_loc    = target_result->second;
                if (auto value = internal::GetString(value_json)) {
                    std::string lower_value = helpers::ToLowerASCII(*value);
                    bool        valid       = true;

                    // Recognized spellings from the standard target table.
                    if (lower_value == "es3" || lower_value == "es5" || lower_value == "es6" ||
                        lower_value == "es2015" || lower_value == "es2016" ||
                        lower_value == "es2017" || lower_value == "es2018" ||
                        lower_value == "es2019" || lower_value == "es2020" ||
                        lower_value == "es2021") {
                        result->settings.Target = config::TSTarget::kBelowES2022;
                    } else if (lower_value == "es2022" || lower_value == "es2023" ||
                               lower_value == "es2024" || lower_value == "es2025" ||
                               lower_value == "esnext") {
                        result->settings.Target = config::TSTarget::kAtOrAboveES2022;
                    } else {
                        valid = false;
                        if (!helpers::IsInsideNodeModules(source.key_path.text)) {
                            log.AddID(logger::MsgID::kTSConfigJSON_InvalidTarget,
                                      logger::MsgKind::kWarning, &tracker,
                                      source.RangeOfString(value_json.loc),
                                      logger::FormatMsg(logger::MsgCat::kTSConfig_UnrecognizedTarget,
                                                        helpers::QuoteForJSON(*value, false)));
                        }
                    }

                    if (valid) {
                        TSTargetKey key;
                        key.source      = source;
                        key.range       = source.RangeOfString(key_loc);
                        key.lower_value = std::move(lower_value);
                        result->ts_target_key = std::move(key);
                    }
                }
            }

            // Whether all strictness checks are enabled at once at the implied strict
            // level. Because this flag also feeds "alwaysStrict", its value and
            // source span are preserved rather than reduced to a bare boolean.
            if (auto strict_result = internal::GetProperty(compiler_options_json, "strict")) {
                javascript::Expr value_json = strict_result->first;
                logger::Loc      key_loc    = strict_result->second;
                if (auto value = internal::GetBool(value_json)) {
                    logger::Range value_range =
                            javascript::RangeOfIdentifier(source, value_json.loc);
                    config::TSAlwaysStrict data;
                    data.Name       = "strict";
                    data.Value      = *value;
                    data.SourceData = source;
                    data.RangeData  = logger::Range{key_loc, value_range.End() - key_loc.start};
                    result->ts_strict = std::move(data);
                }
            }

            // Explicitly requires strict, transformation-safe output; keeps the same
            // source-span bookkeeping as "strict".
            if (auto always_strict_result =
                    internal::GetProperty(compiler_options_json, "alwaysStrict")) {
                javascript::Expr value_json = always_strict_result->first;
                logger::Loc      key_loc    = always_strict_result->second;
                if (auto value = internal::GetBool(value_json)) {
                    logger::Range value_range =
                            javascript::RangeOfIdentifier(source, value_json.loc);
                    config::TSAlwaysStrict data;
                    data.Name       = "alwaysStrict";
                    data.Value      = *value;
                    data.SourceData = source;
                    data.RangeData  = logger::Range{key_loc, value_range.End() - key_loc.start};
                    result->ts_always_strict = std::move(data);
                }
            }

            // Legacy control over how unused imports are elided. The three recognized
            // strings map onto their enum values; any other spelling draws a
            // warning but refuses to abort the parse.
            if (auto inuav_result =
                    internal::GetProperty(compiler_options_json, "importsNotUsedAsValues")) {
                javascript::Expr value_json = inuav_result->first;
                if (auto value = internal::GetString(value_json)) {
                    if (*value == "remove") {
                        result->settings.ImportsNotUsedAsValues =
                                config::TSImportsNotUsedAsValues::kRemove;
                    } else if (*value == "preserve") {
                        result->settings.ImportsNotUsedAsValues =
                                config::TSImportsNotUsedAsValues::kPreserve;
                    } else if (*value == "error") {
                        result->settings.ImportsNotUsedAsValues =
                                config::TSImportsNotUsedAsValues::kError;
                    } else {
                        log.AddID(logger::MsgID::kTSConfigJSON_InvalidImportsNotUsedAsValues,
                                  logger::MsgKind::kWarning, &tracker,
                                  source.RangeOfString(value_json.loc),
                                  logger::FormatMsg(logger::MsgCat::kTSConfig_InvalidImportsNotUsedAsValues,
                                                    helpers::QuoteForJSON(*value, false)));
                    }
                }
            }

            // Decides whether imports that are only needed for their values survive when
            // type-only imports are being dropped; stored as a tri-state.
            if (auto pvi_result =
                    internal::GetProperty(compiler_options_json, "preserveValueImports")) {
                javascript::Expr value_json = pvi_result->first;
                if (auto value = internal::GetBool(value_json)) {
                    result->settings.PreserveValueImports =
                            *value ? config::MaybeBool::kTrue : config::MaybeBool::kFalse;
                }
            }

            // Demands that type and value imports stay syntactically distinct rather
            // than being mixed in one statement; stored as a tri-state.
            if (auto vms_result =
                    internal::GetProperty(compiler_options_json, "verbatimModuleSyntax")) {
                javascript::Expr value_json = vms_result->first;
                if (auto value = internal::GetBool(value_json)) {
                    result->settings.VerbatimModuleSyntax =
                            *value ? config::MaybeBool::kTrue : config::MaybeBool::kFalse;
                }
            }

            // The path-mapping table. A remapping only makes sense when a "baseUrl"
            // anchors it, so the anchor is snapshotted alongside the table and
            // carried by the resolver.
            if (auto paths_result = internal::GetProperty(compiler_options_json, "paths")) {
                javascript::Expr value_json = paths_result->first;
                if (auto* paths_obj =
                        std::get_if<std::shared_ptr<javascript::EObject>>(&value_json.data)) {
                    result->base_url_for_paths = file_dir;
                    auto paths_data            = std::make_shared<TSConfigPaths>();
                    paths_data->source         = source;
                    for (const javascript::Property& prop : (*paths_obj)->properties) {
                        auto key = internal::GetString(prop.key);
                        if (!key.has_value()) {
                            continue;
                        }
                        if (!IsValidTSConfigPathPattern(*key, log, source, &tracker, prop.key.loc)) {
                            continue;
                        }

                        // Each "paths" pattern maps to an ordered list of remapping patterns tried
                        // front to back until one matches on disk. The single '*'
                        // in a pattern stands for whatever string the lookup
                        // matched. Example:
                        //
                        //   "compilerOptions": {
                        //     "baseUrl": "projectRoot",
                        //     "paths": { "*": ["*", "generated/*"] }
                        //   }
                        //
                        // A request for "folder1/file2" first tries
                        // "<baseUrl>/folder1/file2" and then, when that is
                        // absent, "<baseUrl>/generated/folder1/file2". Each
                        // target is ${configDir}-substituted and kept with its
                        // own span for diagnostics.
                        if (auto* array =
                                std::get_if<std::shared_ptr<javascript::EArray>>(
                                    &prop.value_or_nil.data)) {
                            for (const javascript::Expr& item : (*array)->items) {
                                if (auto str = internal::GetString(item)) {
                                    if (IsValidTSConfigPathPattern(*str, log, source, &tracker,
                                                                   item.loc)) {
                                        *str = GetSubstitutedPathWithConfigDirTemplate(fs, *str,
                                                                                       config_dir);
                                        paths_data->map[*key].push_back(
                                                TSConfigPath{std::move(*str), item.loc});
                                    }
                                }
                            }
                        } else {
                            log.AddID(logger::MsgID::kTSConfigJSON_InvalidPaths,
                                      logger::MsgKind::kWarning, &tracker,
                                      source.RangeOfString(prop.value_or_nil.loc),
                                      logger::FormatMsg(logger::MsgCat::kTSConfig_SubstitutionsShouldBeArray));
                        }
                    }
                    result->paths = std::move(paths_data);
                }
            }
        }

        // A config that places a known compiler option at the top level of the file
        // instead of inside "compilerOptions" is almost always a mistake, so
        // Guchho flags the first option seen in that mispositioned spot with a
        // warning about nesting. At most one warning is emitted per file to
        // avoid noise.
        if (auto* obj = std::get_if<std::shared_ptr<javascript::EObject>>(&json.data)) {
            for (const javascript::Property& prop : (*obj)->properties) {
                auto* key_data =
                        std::get_if<std::shared_ptr<javascript::EString>>(&prop.key.data);
                if (key_data == nullptr) {
                    continue;
                }
                std::string key = helpers::UTF16ToString((*key_data)->value);
                bool is_compiler_option =
                        key == "alwaysStrict" || key == "baseUrl" ||
                        key == "experimentalDecorators" || key == "importsNotUsedAsValues" ||
                        key == "jsx" || key == "jsxFactory" || key == "jsxFragmentFactory" ||
                        key == "jsxImportSource" || key == "paths" ||
                        key == "preserveValueImports" || key == "strict" || key == "target" ||
                        key == "useDefineForClassFields" || key == "verbatimModuleSyntax";
                if (is_compiler_option) {
                    log.AddIDWithNotes(logger::MsgID::kTSConfigJSON_InvalidTopLevelOption,
                                       logger::MsgKind::kWarning, &tracker,
                                       source.RangeOfString(prop.key.loc),
                                       logger::FormatMsg(logger::MsgCat::kTSConfig_OptionNotNested,
                                                         helpers::QuoteForJSON(key, false)),
                                       {});
                    break;
                }
            }
        }

        return result;
    }

} // namespace guchho::resolver
