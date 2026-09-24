#pragma once

// Public interface of the module that turns import specifiers into concrete
// filesystem paths inside the Guchho bundler. Every import the bundler sees —
// a package name, a relative file, a directory, a "package.json" entry, a
// walking project config, a design asset or a config file — is routed through
// here so the rest of the pipeline can hand the resulting path to the reader
// with a single query.
//
// This header is the consolidated, self-contained declaration surface for the
// resolver. Every publicly-visible shape used by resolution lives here; the
// implementation is split across the translation units under "src/resolver/".
//
// The main entry point is "Resolver::Resolve", which accepts a source
// directory, the raw import text and the kind of import, and returns a
// "ResolveResult" naming the file to load (see that struct). Several layers
// cooperate underneath it:
//
//   - The Guchho config loaders ("GuchhoConfig" family) read the project
//     configuration files that control extension order, externals, CSS
//     loaders and compile settings.
//
//   - The "PackageJSON" / "Pj*" machinery parses "package.json" so the
//     "exports", "imports", "main", "browser", "sideEffects" and "type"
//     fields influence resolution the same way the ecosystem expects.
//
//   - The "TSConfigJSON" machinery reads the project configuration file
    //     while the resolver walks the tree, so "paths", "baseUrl", "jsx" and
    //     strictness settings shape both resolution and the later compilation
    //     pass.
//
//   - The Yarn PnP machinery answers resolutions from a Plug'n'Play
//     manifest when the project installs dependencies without a flat
//     "node_modules" tree.

#include <any>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "guchho/cache.hpp"
#include "guchho/compiler.hpp"
#include "guchho/config.hpp"
#include "guchho/filesystem.hpp"
#include "guchho/helpers.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/logger.hpp"

namespace guchho::resolver {

    ////////////////////////////////////////////////////////////////////////////////
    // Guchho project configuration
    //
    // Reads the "guchho.json" / "guchho.config.json" project configuration
    // format. Discovery walks up the directory tree from a starting directory,
    // parses the first file it may legally use as strict JSON, and maps the
    // documented field names onto the internal "config::Options" structure.
    //
    // "guchho.config.js" is handled here as well. The file is ordinary
    // JavaScript, so the C++ core cannot interpret it directly; it is instead
    // evaluated with an external JavaScript runtime (see "guchho/helpers.hpp"),
    // and the resulting config object is transported back as JSON. That JSON
    // goes through the same shared field-mapping code, so a JS config supports
    // exactly the same fields as a JSON config.
    //
    // Only "guchho_json.cpp" knows how to translate config files into
    // "config::Options"; the rest of the core stays format-independent.
    //
    // Within one directory the supported files are checked in this order:
    // "guchho.config.js", "guchho.config.json", "guchho.json". The primary JS
    // config takes precedence across the whole discovery walk, so a JSON config
    // is only a fallback when no JS config exists anywhere (configs from
    // different directories are never merged). A found config is a partial
    // override on top of the built-in defaults produced by
    // "CreateDefaultGuchhoConfig".

    // The result of loading a Guchho project config file.
    struct GuchhoConfig {
        // The resolved build options: the built-in defaults from
        // "CreateDefaultGuchhoConfig" with the explicitly-specified fields of
        // the found config file applied on top.
        config::Options opts;

        // Build entry points from the "build.entry" field. These are passed to
        // the bundler separately from "opts" (which does not carry entries).
        std::vector<config::EntryPoint> entry_points;

        // Owns the processed defines that "opts.Defines" points at.
        // "config::Options" keeps only a raw pointer, so the storage must move
        // along with this struct for the pointer to stay valid.
        std::unique_ptr<config::ProcessedDefines> defines_owned;

        // Absolute path of the directory containing the config file.
        std::string config_dir;

        // Absolute path of the config file that was loaded (empty when none
        // was found on disk).
        std::string config_path;

        // True when a config file was found and successfully parsed.
        bool found = false;

        // True when a config file was found but could not be parsed. When this
        // is set alongside "found == false", a diagnostic has already been
        // logged to the log sink.
        bool parse_error = false;
    };

    // Creates the built-in default GuchhoConfig for "root_dir". Relative
    // paths in the defaults — the default entry "index.html" and the default
    // output directory "dist" — are resolved against "root_dir". The result
    // carries the documented defaults, has "found == false" and
    // "parse_error == false", and never depends on a config file existing.
    // Config discovery starts from these defaults and overlays only the
    // explicitly-specified fields of a found config file, so this function is
    // the single source of truth for the default configuration.
    //
    // Example:
    //   CreateDefaultGuchhoConfig("C:/app") =>
    //     GuchhoConfig { entry_points  = { { path = "C:/app/index.html" } },
    //                    opts.Outdir   = "C:/app/dist",
    //                    found         = false,
    //                    parse_error   = false }
    GuchhoConfig CreateDefaultGuchhoConfig(const std::string& root_dir);

    // Discovers a Guchho config file starting at "start_dir" and walking up
    // through parent directories. Within each directory the three supported
    // file names are checked in priority order: "guchho.config.js", then
    // "guchho.config.json", then "guchho.json". The primary JS config takes
    // precedence across the whole walk: a "guchho.config.js" found in any
    // directory wins over a valid JSON config from a deeper directory, so a
    // JSON config is only a fallback when no JS config exists anywhere.
    // Config files from multiple directories are never merged. "opts" is used
    // only for diagnostic path-style settings — the configuration base is
    // always the built-in defaults from "CreateDefaultGuchhoConfig". When no
    // config exists anywhere the defaults are returned with "found == false"
    // and "parse_error == false". A config that exists but cannot be read or
    // parsed is FOUND + INVALID ("found == true", "parse_error == true") and
    // never falls back to parent directories or to the defaults.
    //
    // Example:
    //   LoadGuchhoConfig(log, cache, fs, opts, "C:/app/src") when
    //   "C:/app/guchho.json" sets "outdir" =>
    //     GuchhoConfig { opts.Outdir = "C:/app/build", config_dir = "C:/app",
    //                    config_path = "C:/app/guchho.json", found = true }
    GuchhoConfig LoadGuchhoConfig(
        logger::Log&             log,
        cache::JSONCache&        json_cache,
        filesystem::Fs&          fs,
        config::Options&         opts,
        const std::string&       start_dir);

    // Loads a Guchho config from an explicit absolute file path, populating
    // "opts". Returns "found = false" (with a logged diagnostic) when the
    // file does not exist or cannot be read.
    //
    // Example:
    //   LoadGuchhoConfigFromFile(log, cache, fs, opts, "C:/app/guchho.json") =>
    //     GuchhoConfig { config_path = "C:/app/guchho.json", found = true }
    //   LoadGuchhoConfigFromFile(..., "C:/missing.json") =>
    //     GuchhoConfig { found = false }
    GuchhoConfig LoadGuchhoConfigFromFile(
        logger::Log&             log,
        cache::JSONCache&        json_cache,
        filesystem::Fs&          fs,
        config::Options&         opts,
        const std::string&       file_path);

    // Loads a Guchho config from raw JSON text that is understood to come
    // from "config_path" (used to build diagnostics and to resolve relative
    // paths against the config's directory). "config_path" may be empty for
    // synthetic configs. Returns "parse_error = true" (with a logged
    // diagnostic) when "json_text" is not valid JSON.
    //
    // Example:
    //   LoadGuchhoConfigFromText(log, cache, fs, opts,
    //     "{ \"outdir\": \"build\" }", "C:/app/guchho.json") =>
    //     GuchhoConfig { opts.Outdir = "C:/app/build", found = true }
    GuchhoConfig LoadGuchhoConfigFromText(
        logger::Log&             log,
        cache::JSONCache&        json_cache,
        filesystem::Fs&          fs,
        config::Options&         opts,
        const std::string&       json_text,
        const std::string&       config_path);

    // Applies the given parsed JSON config to "opts". "config_dir" is the
    // absolute directory the config file lives in; every relative path in the
    // config is resolved against it. Parsed "build.entry" values and "define"
    // entries are appended to "entry_points" and "user_defines" respectively,
    // so the caller can feed them back into the bundler. Exposed separately
    // so tests and embedders can feed an already-parsed JSON value.
    //
    // Example:
    //   ApplyGuchhoJsonConfig(log, fs, opts, entries, defines,
    //     { EObject["outdir" -> "dist"] }, "C:/app", src) =>
    //     opts.Outdir == "C:/app/dist"
    void ApplyGuchhoJsonConfig(
        logger::Log&                     log,
        filesystem::Fs&                  fs,
        config::Options&                 opts,
        std::vector<config::EntryPoint>& entry_points,
        std::vector<config::DefineData>& user_defines,
        const javascript::Expr&          json,
        const std::string&               config_dir,
        const logger::Source&            source);

    // The signature of the "guchho.config.js" loader. An alias so tests can
    // substitute a fake that never spawns a child process.
    using GuchhoConfigJSLoader = GuchhoConfig (*)(
        logger::Log&        log,
        cache::JSONCache&   json_cache,
        filesystem::Fs&     fs,
        config::Options&    opts,
        const std::string&  file_path);

    // Loads a "guchho.config.js" file by evaluating it with an external
    // JavaScript runtime. On any failure (runtime missing, evaluation error,
    // non-JSON output) a warning is logged and the returned "GuchhoConfig"
    // has "found = false". The discovery loop treats a found-but-failed JS
    // config as FOUND + INVALID (it never falls back to a JSON config in the
    // same directory or in a parent directory).
    GuchhoConfig LoadGuchhoConfigFromJS(
        logger::Log&       log,
        cache::JSONCache&  json_cache,
        filesystem::Fs&    fs,
        config::Options&   opts,
        const std::string& file_path);

    // Returns the active JS-config loader. Defaults to
    // "LoadGuchhoConfigFromJS". The discovery loop routes "guchho.config.js"
    // files through this so tests can install a deterministic fake.
    GuchhoConfigJSLoader GetGuchhoConfigJSLoader();

    // Installs a fake JS-config loader for tests and returns the previous one.
    // Restore it before the test ends to avoid leaking into other tests.
    GuchhoConfigJSLoader SetGuchhoConfigJSLoader(GuchhoConfigJSLoader loader);

    ////////////////////////////////////////////////////////////////////////////////
    // package.json / exports-imports resolution

    struct SideEffectsData; // Defined below in this header

    struct MainField {
        std::string rel_path;
        logger::Loc key_loc;
    };

    // The ESM resolution algorithm works on typed entries: a value in an
    // "exports"/"imports" map can be a plain string, an array of fallbacks,
    // a nested conditions object, the null literal (meaning "not exported"),
    // or absent entirely. The kind tracks which case the entry represents so
    // later steps can decide how to interpret it.
    enum class PjKind : uint8_t {
        kNull,
        kString,
        kArray,
        kObject,
        kInvalid,
    };

    struct PjMap;
    struct PjEntry;
    struct PjMapEntry;

    // Recursive shape of a parsed "exports" or "imports" value. A string,
    // array or object entry can nest further entries; "map_data" preserves
    // source order (a map would lose it), and "expansion_keys" holds any keys
    // generated while rewriting patterns during matching.
    struct PjEntry {
        std::string             str_data;
        std::vector<PjEntry>    arr_data;
        std::vector<PjMapEntry> map_data; // A map would reorder entries; order matters
        std::vector<PjMapEntry> expansion_keys;
        logger::Range           first_token;
        PjKind                  kind = PjKind::kNull;
    };

    // A single named child inside an object entry: the condition or subpath
    // key plus its value, with the range of the key for diagnostics.
    struct PjMapEntry {
        std::string   key;
        PjEntry       value;
        logger::Range key_range;
    };

    // A parsed top-level "exports" or "imports" map. The root entry holds the
    // whole value; "property_key" and "property_key_loc" identify which
    // package.json property this map came from for error reporting.
    struct PjMap {
        std::unique_ptr<PjEntry> root;
        std::string              property_key;
        logger::Loc              property_key_loc;
    };

    // Status codes produced by the ESM resolution algorithm. The k*NotFound /
    // kInvalid* family are terminal failures; the rest describe a successful
    // match (or a partial one, like "kInexact", that still needs the
    // extension-suffixing step).
    enum class PjStatus : uint8_t {
        kUndefined,
        kUndefinedNoConditionsMatch, // More friendly message when no conditions matched
        kNull,
        kExact,
        kExactEndsWithStar,
        kInexact,        // Try CommonJS-style extension suffixes
        kPackageResolve, // Re-run package resolution on the result

        // Module specifier is an invalid URL, package name or package subpath.
        kInvalidModuleSpecifier,

        // package.json configuration is invalid or contains an invalid configuration.
        kInvalidPackageConfiguration,

        // Package exports or imports define a target module for the package
        // that is an invalid type or string target.
        kInvalidPackageTarget,

        // Package exports do not define or permit a target subpath for the
        // given module.
        kPackagePathNotExported,

        // Package imports do not define the specifier.
        kPackageImportNotDefined,

        // The package or module requested does not exist.
        kModuleNotFound,
        kModuleNotFoundMissingExtension, // User just needs to add the missing extension

        // The resolved path corresponds to a directory, which is not a
        // supported target for module imports.
        kUnsupportedDirectoryImport,
        kUnsupportedDirectoryImportMissingIndex, // User just needs to add "/index.js"
    };

    // True when the resolution came up empty because no branch of the
    // conditions object matched, as opposed to a hard failure. Used to pick a
    // friendlier diagnostic wording.
    inline bool PjStatusIsUndefined(PjStatus status)
    {
        return status == PjStatus::kUndefined || status == PjStatus::kUndefinedNoConditionsMatch;
    }

    // One condition that appeared during resolution but never matched, kept
    // for use in error messages about unmatched conditions.
    struct DebugSpan {
        std::string   text;
        logger::Range range;
    };

    // Supporting details attached to a PjStatus so the caller can build a
    // precise diagnostic instead of a generic one.
    struct PjDebug {
        // If the status is "kInvalidPackageTarget" or "kInvalidModuleSpecifier",
        // this is the reason. It always starts with " because".
        std::string invalid_because;

        // If the status is "kUndefinedNoConditionsMatch", this is the set of
        // conditions that didn't match, in the order they appeared in the
        // file. Used for error messages.
        std::vector<DebugSpan> unmatched_conditions;

        // The range of the token to use for error messages.
        logger::Range token;

        // If true, the token is a "null" literal.
        bool is_because_of_null_literal = false;
    };

    // The parsed contents of one "package.json" file, kept alive for the
    // lifetime of the resolver and shared through "DirInfo".
    struct PackageJSON {
        std::string name;
        std::map<std::string, MainField> main_fields;
        javascript::ModuleTypeData module_type_data;

        // Optional "tsconfig" field pointing at a project configuration file
        // that should be loaded instead of guessing at the package root.
        std::string tsconfig;

        // Present if the "browser" field is present. This field is intended to
        // be used by bundlers and lets you redirect the paths of certain 3rd-
        // party modules that don't work in the browser to other modules that
        // shim that functionality. Mapping to an empty optional indicates that
        // the module is disabled.
        //
        // This field contains the original mapping object in "package.json".
        // Note that the non-package "browser" map has to be checked twice to
        // match the expected bundler behavior: once before resolution and once
        // after resolution.
        std::map<std::string, std::optional<std::string>> browser_map;

        // If this has a value, each entry in this set is the absolute path of
        // a file with side effects. Any entry not in this set should be
        // considered to have no side effects, which means import statements
        // for these files can be removed if none of the imports are used.
        std::optional<std::unordered_set<std::string>> side_effects_map;

        // Wildcard entries in the "sideEffects" field require more expensive
        // regular expression matching.
        std::vector<std::regex> side_effects_regexps;

        std::shared_ptr<SideEffectsData> side_effects_data;

        // This represents the "imports" field in this package.json file.
        std::unique_ptr<PjMap> imports_map;

        // This represents the "exports" field in this package.json file.
        std::unique_ptr<PjMap> exports_map;

        logger::Source source;
    };

    // Returns whether all keys of an exports/imports object start with ".";
    // Only such keys are eligible for ESM subpath matching.
    bool PjEntryKeysStartWithDot(const PjEntry& entry);

    // Finds the entry in "entry.map_data" whose key equals "key", or nullptr.
    const PjEntry* PjEntryValueForKey(const PjEntry& entry, const std::string& key);

    // Converts a glob pattern from a "sideEffects" array entry into a string
    // suitable for constructing an ECMAScript-style regular expression object.
    // Returns the pattern and whether it contained a wildcard.
    std::pair<std::string, bool> GlobstarToEscapedRegexp(const std::string& glob);

    // Parses either the "imports" or "exports" field of a package.json into a
    // "PjMap". Returns nullptr when the top-level value is the null literal.
    //
    // Example:
    //   ParseImportsExportsMap(source, log,
    //     { EObject["." -> "./index.js"] }, "exports", loc) =>
    //     PjMap { root = { map_data = { { "." -> { str_data = "./index.js" } } } } }
    std::unique_ptr<PjMap> ParseImportsExportsMap(const logger::Source&    source,
                                                  logger::Log&            log,
                                                  const javascript::Expr& json,
                                                  const std::string&      property_key,
                                                  logger::Loc             property_key_loc);

    // Splits a bare package specifier like "pkg/subpath" into the package
    // name and the subpath. Returns false when the specifier is not a valid
    // package name (e.g. it starts with "." or is empty).
    //
    // Example:
    //   EsmParsePackageName("progress/lib/util") =>
    //     true, package_name = "progress", package_subpath = "./lib/util"
    //   EsmParsePackageName("./local") => false
    bool EsmParsePackageName(const std::string& package_specifier,
                             std::string&       package_name,
                             std::string&       package_subpath);

    // If a path split on "/" or "\" contains any ".", ".." or "node_modules"
    // segment after the first segment, returns that segment.
    std::string_view FindInvalidSegment(std::string_view path);

    ////////////////////////////////////////////////////////////////////////////////
    // tsconfig.json resolution

    // This information is only used for error messages when the "target"
    // setting has an out-of-range value.
    struct TSTargetKey {
        std::string    lower_value;
        logger::Source source;
        logger::Range  range;
    };

    // One verbatim entry from the "paths" mapping: the text plus where it
    // appeared, the latter for diagnostics.
    struct TSConfigPath {
        std::string text;
        logger::Loc loc;
    };

    // The resolved "compilerOptions.paths" table: pattern keys mapped to lists
    // of fallback paths. "source" may differ from the original project config
    // file when the "paths" value was inherited through an "extends" clause.
    struct TSConfigPaths {
        std::unordered_map<std::string, std::vector<TSConfigPath>> map;

        // This may be different from the original configuration file source
        // if the "paths" value is from another file via an "extends" clause.
        logger::Source source;
    };

    // Parsed contents of a project configuration file ("tsconfig.json" /
    // "jsconfig.json") as consumed by the resolver and the compile settings.
    struct TSConfigJSON {
        std::string abs_path;

        // The absolute path of "compilerOptions.baseUrl".
        std::optional<std::string> base_url;

        // This is used when "paths" is set. It's equal to "base_url" except
        // when "base_url" is missing, in which case it behaves as if "base_url"
        // were ".". This implements the "paths without baseUrl" feature:
        // path-style module names can be matched even when no base URL was
        // declared.
        std::string base_url_for_paths;

        // The verbatim values of "compilerOptions.paths". The keys are
        // patterns to match and the values are arrays of fallback paths to
        // search. Each key and each fallback path can optionally have a single
        // "*" wildcard character. If both the key and the value have a
        // wildcard, the substring matched by the wildcard is substituted into
        // the fallback path. The keys represent module-style path names and
        // the fallback paths are relative to the "baseUrl" value in the
        // configuration file.
        std::shared_ptr<TSConfigPaths> paths;

        TSTargetKey ts_target_key;

        std::optional<config::TSAlwaysStrict> ts_strict;
        std::optional<config::TSAlwaysStrict> ts_always_strict;

        config::TSConfigJSX jsx_settings;
        config::TSConfig    settings;

        // Mutates this configuration by inheriting whichever fields the base
        // configuration sets and this one leaves unset.
        void ApplyExtendedConfig(const TSConfigJSON& base);

        // The effective "alwaysStrict" value: an explicit "alwaysStrict"
        // setting wins, otherwise it defaults to the "strict" setting.
        const config::TSAlwaysStrict* TSAlwaysStrictOrStrict() const;
    };

    // Resolves an "extends" value from a configuration file. Returns nullptr
    // when the base configuration could not be loaded.
    using TSConfigExtendsCallback =
        std::function<TSConfigJSON*(const std::string& extends, logger::Range range)>;

    // Parses a project configuration file located at "source", resolving
    // relative paths against "config_dir". Follows "extends" chains through
    // the callback. Returns nullptr on failure with a diagnostic already
    // logged where appropriate.
    // Postcondition: the returned object is owned by the resolver's arena and
    // stays alive for the resolver's lifetime.
    TSConfigJSON* ParseTSConfigJSON(
        logger::Log&                   log,
        const logger::Source&          source,
        cache::JSONCache&              json_cache,
        filesystem::Fs&                fs,
        const std::string&             file_dir,
        const std::string&             config_dir,
        const TSConfigExtendsCallback& extends);

    // Shared with the main resolver loop: validates a "paths" pattern when
    // "baseUrl" is unset. Lazily creates a line/column tracker on the first
    // warning; the caller owns "*tracker" and must delete it afterwards.
    bool IsValidTSConfigPathNoBaseURLPattern(const std::string&          text,
                                             logger::Log&                log,
                                             const logger::Source&       source,
                                             logger::LineColumnTracker*& tracker,
                                             logger::Loc                 loc);

    ////////////////////////////////////////////////////////////////////////////////
    // Yarn PnP
    //
    // Implements dependency resolution from a Yarn Plug'n'Play manifest
    // instead of a flat "node_modules" tree. A ".pnp.data.json" (or the JS
    // wrapper that loads it) is compiled into lookup tables once, then every
    // resolution consults those tables.

    // A locator picks one installed package out of the manifest; when used as
    // a dependency target it can be in one of three states:
    //
    //  1. A reference, to link with the dependency name. In this case "ident"
    //     is empty.
    //
    //  2. An aliased package. In this case neither "ident" nor "reference"
    //     are empty.
    //
    //  3. A missing peer dependency. In this case both "ident" and
    //     "reference" are empty.
    struct PnpIdentAndReference {
        std::string   ident;     // Empty if null
        std::string   reference; // Empty if null
        logger::Range span{};
    };

    // One installed package: its declared dependencies plus the absolute
    // location it was unpacked to on disk.
    struct PnpPackage {
        std::unordered_map<std::string, PnpIdentAndReference> package_dependencies;
        std::string      package_location;
        logger::Range    package_dependencies_range;
        bool             discard_from_lookup = false;
    };

    // Maps a physical unzipped location back to the locator that produced it,
    // so "require.resolve" can be mirrored.
    struct PnpPackageLocatorByLocation {
        PnpIdentAndReference locator;
        bool                 discard_from_lookup = false;
    };

    // The compiled Plug'n'Play manifest. Immutable once built; cached on the
    // resolver and shared by all resolutions that fall inside its scope.
    struct PnpData {
        // Keys are the package idents, values are sets of references.
        // Combining the ident with each individual reference yields the set
        // of affected locators.
        std::unordered_map<std::string, std::unordered_map<std::string, bool>> fallback_exclusion_list;

        // A map of locators that all packages are allowed to access,
        // regardless of whether they list them in their dependencies.
        std::unordered_map<std::string, PnpIdentAndReference> fallback_pool;

        // A nullable regular expression. If set, all project-relative importer
        // paths should be matched against it. If the match succeeds, the
        // resolution should follow the classic Node.js resolution algorithm
        // rather than the Plug'n'Play one. Note that unlike other paths in the
        // manifest, the one checked against this expression won't begin with
        // "./".
        std::unique_ptr<std::regex> ignore_pattern_data;
        std::string                 invalid_ignore_pattern_data;

        // The main part of the data file. This table contains the list of all
        // packages, first keyed by package ident then by package reference.
        // One entry with empty strings in both fields represents the
        // absolute-top-level package.
        std::unordered_map<std::string, std::unordered_map<std::string, PnpPackage>> package_registry_data;

        std::unordered_map<std::string, PnpPackageLocatorByLocation> package_locators_by_locations;

        // If true, should a dependency resolution fail for an importer that
        // isn't explicitly listed in "fallback_exclusion_list", the runtime
        // must first check whether the resolution would succeed for any of the
        // packages in "fallback_pool"; if it would, transparently return this
        // resolution. Note that all dependencies from the top-level package
        // are implicitly part of the fallback pool, even if not listed here.
        bool enable_top_level_fallback = false;

        logger::LineColumnTracker tracker;
        std::string               abs_path;
        std::string               abs_dir_path;
    };

    enum class PnpStatus : uint8_t {
        kErrorGeneric,
        kErrorDependencyNotFound,
        kErrorUnfulfilledPeerDependency,
        kSuccess,
        kSkipped,
    };

    inline bool PnpStatusIsError(PnpStatus status)
    {
        return status < PnpStatus::kSuccess;
    }

    // Outcome of resolving one importer against the PnP tables: the package
    // directory, the package ident, and the leftover subpath inside it.
    struct PnpResult {
        PnpStatus   status{};
        std::string pkg_dir_path{};
        std::string pkg_ident{};
        std::string pkg_subpath{};

        // This is for error messages.
        std::string   error_ident{};
        logger::Range error_range{};
    };

    // Controls how strict the PnP data extraction is about missing files: the
    // loose mode tolerates a missing manifest (resolution then falls back to
    // the classic algorithm), while the strict mode reports it as an error.
    enum class PnpDataMode : uint8_t {
        kIgnoreErrorsAboutMissingFiles,
        kReportErrorsAboutMissingFiles,
    };

    // Compiles the JSON expression of a ".pnp.data.json" or ".pnp.cjs"/".pnp.js"
    // manifest into the lookup tables used by the resolver.
    std::unique_ptr<PnpData> CompileYarnPnPData(const std::string&     abs_path,
                                                const std::string&     abs_dir_path,
                                                const javascript::Expr& json,
                                                const logger::Source&  source);

    ////////////////////////////////////////////////////////////////////////////////
    // Shared resolver internals

    namespace internal {

        // Reads a named property out of a parsed JSON object expression,
        // returning the value (or the nil expression) together with the
        // location of its key. Returns nullopt when the value is not an
        // object.
        //
        // Example:
        //   GetProperty({ EObject["foo" -> { EString "x" }] }, "foo") =>
        //     { { EString "x" }, loc }
        inline std::optional<std::pair<javascript::Expr, logger::Loc>> GetProperty(
            const javascript::Expr& json,
            std::string_view        name)
        {
            if (auto* obj = std::get_if<std::shared_ptr<javascript::EObject>>(&json.data)) {
                for (const javascript::Property& prop : (*obj)->properties) {
                    if (auto* key = std::get_if<std::shared_ptr<javascript::EString>>(&prop.key.data)) {
                        if (helpers::UTF16EqualsString((*key)->value, name)) {
                            return std::make_pair(prop.value_or_nil, prop.key.loc);
                        }
                    }
                }
            }
            return std::nullopt;
        }

        // Reads the string value of a JSON expression, or nullopt when the
        // expression is not a string literal.
        //
        // Example:
        //   GetString({ EString "hello" }) => "hello"
        //   GetString({ ENumber 3 }) => nullopt
        inline std::optional<std::string> GetString(const javascript::Expr& json)
        {
            if (auto* value = std::get_if<std::shared_ptr<javascript::EString>>(&json.data)) {
                return helpers::UTF16ToString((*value)->value);
            }
            return std::nullopt;
        }

        // Reads the boolean value of a JSON expression, or nullopt when the
        // expression is not a boolean literal.
        //
        // Example:
        //   GetBool({ EBoolean true }) => true
        //   GetBool({ EString "true" }) => nullopt
        inline std::optional<bool> GetBool(const javascript::Expr& json)
        {
            if (auto* value = std::get_if<std::shared_ptr<javascript::EBoolean>>(&json.data)) {
                return (*value)->value;
            }
            return std::nullopt;
        }

        // Url-decodes a path component. Unlike query unescaping, this does not
        // convert '+' into a space. Fills "error" on failure.
        //
        // Example:
        //   UnescapePath("a%2Fb", out, err) => true, out == "a/b"
        bool UnescapePath(std::string_view text, std::string& out, std::string& error);

        // Cleans a slash-separated path: collapses duplicate and trailing
        // slashes, resolves "." segments and pops ".." segments where the
        // result stays within the same root. Only meaningful for URLs and
        // package subpaths; use "GoFilepath::Clean" for native filesystem
        // paths.
        //
        // Examples:
        //   PosixPathClean("/a/b/../c/") => "/a/c"
        //   PosixPathClean("a//b/./c")   => "a/b/c"
        //   PosixPathClean("../x")       => "../x"
        inline std::string PosixPathClean(std::string_view path)
        {
            if (path.empty()) {
                return ".";
            }

            const bool rooted = path.front() == '/';
            size_t    n       = path.size();

            std::string out;
            size_t      r      = 0;
            size_t      dotdot = 0;

            if (rooted) {
                out += '/';
                r     = 1;
                dotdot = 1;
            }

            while (r < n) {
                if (path[r] == '/') {
                    ++r;
                } else if (path[r] == '.' && (r + 1 == n || path[r + 1] == '/')) {
                    ++r;
                } else if (path[r] == '.' && r + 1 < n && path[r + 1] == '.' &&
                           (r + 2 == n || path[r + 2] == '/')) {
                    r += 2;
                    if (out.size() > dotdot) {
                        // Can backtrack
                        size_t w = out.size() - 1;
                        while (w > dotdot && out[w] != '/') {
                            --w;
                        }
                        out.resize(w);
                    } else if (!rooted) {
                        if (!out.empty()) {
                            out += '/';
                        }
                        out += "..";
                        dotdot = out.size();
                    }
                } else {
                    if ((rooted && out.size() != 1) || (!rooted && !out.empty())) {
                        out += '/';
                    }
                    while (r < n && path[r] != '/') {
                        out += path[r];
                        ++r;
                    }
                }
            }

            if (out.empty()) {
                return ".";
            }
            return out;
        }

        // Joins any number of path elements into a single slash-separated
        // path, separating them with slashes, then cleans the result.
        //
        // Examples:
        //   PosixPathJoin({"a", "b", "..", "c"}) => "a/c"
        //   PosixPathJoin({"..", "x"})           => "../x"
        //   PosixPathJoin({})                    => ""
        inline std::string PosixPathJoin(std::initializer_list<std::string_view> elems)
        {
            std::string joined;
            for (std::string_view elem : elems) {
                if (elem.empty()) {
                    continue;
                }
                if (!joined.empty()) {
                    joined += '/';
                }
                joined.append(elem);
            }
            if (joined.empty()) {
                return "";
            }
            return PosixPathClean(joined);
        }

    } // namespace internal

    ////////////////////////////////////////////////////////////////////////////////
    // Basic result types

    struct Resolver;

    // The default main fields to use for each platform when the user has not
    // configured them explicitly.
    const std::vector<std::string>& DefaultMainFields(config::Platform platform);

    // These are the main fields to use when the "main fields" setting is
    // configured to something unusual, such as something without the "main"
    // field.
    const std::vector<std::string>& MainFieldsForFailure();

    // A mapping used when the resolved file turns out to be a JavaScript
    // source without a matching implementation: the extension is rewritten to
    // the sibling language extension a project author would have shipped
    // first. Each value is the ordered list of alternative extensions to
    // try.
    const std::map<std::string, std::vector<std::string>>& RewrittenFileExtensions();

    // True when "name" is one of the built-in Node.js module names (such as
    // "fs" or "path") that can never appear on disk.
    //
    // Example:
    //   IsBuiltInNodeModule("fs")      => true
    //   IsBuiltInNodeModule("socket.io") => false
    bool IsBuiltInNodeModule(std::string_view name);

    // A resolved path along with an optional secondary path. Either the
    // secondary is empty, or the primary is "module" and the secondary is
    // "main" (used when a package's main fields want two entries, such as a
    // module build plus a server build).
    struct PathPair {
        // Either secondary will be empty, or primary will be "module" and
        // secondary will be "main"
        logger::Path primary;
        logger::Path secondary;

        bool is_external = false;

        bool HasSecondary() const {
            return !secondary.text.empty();
        }

        // Returns pointers to {primary} or {primary, secondary}. The returned
        // pointers are only valid while this object is unchanged.
        std::vector<logger::Path*> Iter() {
            if (HasSecondary()) {
                return {&primary, &secondary};
            }
            return {&primary};
        }
    };

    // Provenance information about why a file was considered side-effect
    // free, used to shape diagnostics and to decide whether an import can be
    // dropped when its bindings are unused.
    struct SideEffectsData {
        std::shared_ptr<logger::Source> source;

        // If non-empty, this false value came from a plugin.
        std::string plugin_name;

        logger::Range range;

        // If true, "sideEffects" was an array. If false, "sideEffects" was
        // false.
        bool is_side_effects_array_in_json = false;
    };

    // The fully-qualified answer produced by a successful resolution: the
    // concrete files to load, plus the compile/type settings that apply to
    // them.
    struct ResolveResult {
        PathPair path_pair;

        // If this was resolved by a plugin, the plugin gets to store its data
        // here.
        std::any plugin_data;

        std::optional<filesystem::DifferentCase> different_case;

        // If present, any ES6 imports to this file can be considered to have
        // no side effects. This means they should be removed if unused.
        std::shared_ptr<SideEffectsData> primary_side_effects_data;

        // These are from the project configuration file.
        config::TSConfigJSX         tsconfig_jsx;
        const config::TSConfig*     tsconfig         = nullptr;
        const config::TSAlwaysStrict* ts_always_strict = nullptr;

        // This is the "type" field from "package.json".
        javascript::ModuleTypeData module_type_data;
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Debugging support

    // When a resolution fails, the resolver tries to produce a helpful
    // suggestion (often "did you mean this file?"). This selects how much of
    // the matched token should be highlighted in the suggestion.
    enum class SuggestionRange : uint8_t {
        kFull,
        kEnd,
    };

    // Extra material gathered during a failed resolution so the caller can
    // render a diagnostic with notes, a suggestion and a rewritten import
    // path.
    struct DebugMeta {
        std::vector<logger::MsgData> notes;
        std::string                  suggestion_text;
        std::string                  suggestion_message;
        SuggestionRange              suggestion_range = SuggestionRange::kFull;
        std::string                  modified_import_path;

        void LogErrorMsg(logger::Log&                   log,
                         const logger::Source*          source,
                         logger::Range                  r,
                         const std::string&             text,
                         const std::string&             suggestion,
                         const std::vector<logger::MsgData>& extra_notes) const;
    };

    // Checkpoint log entries produced while a single import runs through the
    // resolver. The exporter renders these indented according to the current
    // nesting depth, so the trace reads like a call tree of the resolve
    // attempt.
    struct DebugLogs {
        std::string what;
        std::string indent;
        std::vector<logger::MsgData> notes;

        void AddNote(const std::string& text) {
            notes.push_back(logger::MsgData{
                    .text                   = indent + text,
                    .disable_maximum_width = true,
            });
        }

        void IncreaseIndent() {
            indent += "  ";
        }

        void DecreaseIndent() {
            indent.erase(0, 2);
        }
    };

    // Whether the debug trace for an import was flushed because the resolution
    // failed or because it succeeded.
    enum class FlushMode : uint8_t {
        kDueToFailure,
        kDueToSuccess,
    };

    // RAII guard that indents the debug log for the duration of one resolver
    // step and unindents on exit, even on early returns.
    struct DebugIndentGuard {
        DebugLogs* logs;

        explicit DebugIndentGuard(DebugLogs* l) : logs(l)
        {
            if (logs) {
                logs->IncreaseIndent();
            }
        }
        ~DebugIndentGuard()
        {
            if (logs) {
                logs->DecreaseIndent();
            }
        }
        DebugIndentGuard(const DebugIndentGuard&)            = delete;
        DebugIndentGuard& operator=(const DebugIndentGuard&) = delete;
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Directory info cache

    // Cached facts about one directory on disk, built on demand the first
    // time the resolver touches that directory. Entries are immutable after
    // creation, so parent links can be handed out without re-locking.
    struct DirInfo {
        // These objects are immutable once created, so we can just point to
        // the parent directory and avoid having to lock the cache again.
        DirInfo* parent = nullptr;

        // A pointer to the enclosing DirInfo with a valid "browser" field in
        // package.json. We need this to remap paths after they have been
        // resolved.
        DirInfo* enclosing_browser_scope = nullptr;

        // All relevant information about this directory.
        std::string    abs_path;
        std::string    pnp_manifest_abs_path;
        filesystem::DirEntries entries;
        PackageJSON*   package_json            = nullptr; // Does a "package.json" file exist here?
        PackageJSON*   enclosing_package_json  = nullptr; // ... in this directory or a parent?
        TSConfigJSON*  enclosing_tsconfig_json = nullptr; // ... a project config in this directory or a parent?
        std::string    abs_real_path;                     // If non-empty, the real absolute path, resolving symlinks
        bool           is_node_modules       = false;     // Is the base name "node_modules"?
        bool           has_node_modules      = false;     // Is there a "node_modules" subdirectory?
        bool           is_inside_node_modules = false;    // Is this within a "node_modules" subtree?
    };

    ////////////////////////////////////////////////////////////////////////////////
    // The resolver

    // A set of active conditions (e.g. "import", "require", "browser",
    // "node") used to evaluate the "exports"/"imports" condition objects.
    using ConditionsMap = std::unordered_map<std::string, bool>;

    // Whether "FinalizeImportsExports..." is running during normal package
    // resolution or while resolving an "extends" path from a PnP manifest.
    enum class FinalizeImportsExportsKind : uint8_t {
        kNormal,
        kYarnPnPTSConfigExtends,
    };

    // Which of the two "browser" mappings should a remap lookup consult.
    enum class BrowserPathKind : uint8_t {
        kAbsolutePath,
        kPackagePath,
    };

    // How a reverse resolution attempted to match a built file back onto an
    // exports key: exactly, through a parsed subpath pattern, or by prefix.
    enum class EsmReverseKind : uint8_t {
        kExact,
        kPattern,
        kPrefix,
    };

    // Shared multi-value result shapes: several resolver steps return a value
    // plus a flag, so these small structs carry both (plus any case-mismatch
    // note discovered while checking the filesystem).
    struct LoadResult {
        PathPair                                 pair;
        bool                                     ok = false;
        std::optional<filesystem::DifferentCase> diff_case;
    };

    struct FileResult {
        std::string                              absolute;
        bool                                     ok = false;
        std::optional<filesystem::DifferentCase> diff_case;
    };

    struct SideEffectsResult {
        PathPair                                 pair;
        bool                                     ok = false;
        std::optional<filesystem::DifferentCase> diff_case;
        std::shared_ptr<SideEffectsData>         side_effects;
    };

    // The answer from a "browser" field remap lookup.
    struct BrowserRemapResult {
        // Has a value => remapped to that path; no value with "ok" set => disabled.
        std::optional<std::string> remapped;
        bool                       ok = false;
    };

    struct LocatorResult {
        PnpIdentAndReference locator;
        bool                 ok = false;
    };

    struct PackageResult {
        PnpPackage pkg;
        bool       ok = false;
    };

    // One step's worth of ESM resolution: the path resolved so far and the
    // status / debug info describing how it got there.
    struct EsmStep {
        std::string resolved_path;
        PjStatus    status{};
        PjDebug     debug;
    };

    struct ReverseResolveResult {
        bool          ok = false;
        std::string   subpath;
        logger::Range token;
    };

    // Classifies why a project configuration file could not be turned into a
    // "TSConfigJSON". "kENOENT" mirrors the missing-file case, "kOther" any
    // other read failure, "kImportCycle" an "extends" loop and
    // "kAlreadyLogged" a failure whose diagnostic was already reported.
    enum class TSConfigError : uint8_t {
        kNone,
        kENOENT,
        kOther,
        kImportCycle,
        kAlreadyLogged,
    };

    struct TSConfigResult {
        TSConfigJSON* result = nullptr;
        TSConfigError error  = TSConfigError::kNone;
        std::string   error_message; // Only set for "kOther"
    };

    // A single resolution of one import path, structured as a transaction
    // object so it can carry scratch state (debug logs, the active import
    // kind, the resolver backing it) without threading them through every
    // parameter list.
    struct ResolverQuery {
        Resolver* r;
        DebugMeta* debug_meta = nullptr;
        DebugLogs* debug_logs = nullptr;
        compiler::ImportKind kind{};

        ////////////////////////////////////////////////////////////////////////////////
        // Implemented in "resolver.cpp"

        void FlushDebugLogs(FlushMode mode);
        DirInfo* DirInfoCached(const std::string& path);
        DirInfo* DirInfoUncached(const std::string& path);
        TSConfigJSON* TSConfigForDir(DirInfo* dir_info);
        TSConfigResult ParseTSConfig(const std::string& file,
                                     std::unordered_map<std::string, bool>* visited,
                                     const std::string& config_dir);
        TSConfigResult ParseTSConfigFromSource(const logger::Source& source,
                                               std::unordered_map<std::string, bool>* visited,
                                               const std::string& config_dir);
        std::optional<ResolveResult> ResolveWithoutSymlinks(const std::string& source_dir,
                                                            DirInfo* source_dir_info,
                                                            const std::string& import_path);
        SideEffectsResult ResolveWithoutRemapping(DirInfo* source_dir_info, const std::string& import_path);
        void FinalizeResolve(ResolveResult& result);
        bool IsExternal(const config::ExternalMatchers& matchers,
                        const std::string& path,
                        compiler::ImportKind import_kind);
        FileResult LoadAsFile(const std::string& path, const std::vector<std::string>& extension_order);
        LoadResult LoadAsIndex(DirInfo* dir_info, const std::vector<std::string>& extension_order);
        LoadResult LoadAsIndexWithBrowserRemapping(DirInfo* dir_info,
                                                   const std::string& path,
                                                   const std::vector<std::string>& extension_order);
        LoadResult LoadAsFileOrDirectory(const std::string& path);
        LoadResult LoadAsDirectory(const std::string& path);
        LoadResult LoadAsMainField(DirInfo* dir_info,
                                   const std::string& path,
                                   const std::vector<std::string>& extension_order);
        LoadResult MatchTSConfigPaths(TSConfigJSON* tsconfig, const std::string& path);
        SideEffectsResult LoadNodeModules(const std::string& import_path, DirInfo* dir_info, bool forbid_imports);
        std::optional<ResolveResult> CheckForBuiltInNodeModules(std::string import_path);

        ////////////////////////////////////////////////////////////////////////////////
        // Implemented in "package_json.cpp"

        BrowserRemapResult CheckBrowserMap(DirInfo* resolve_dir_info,
                                           const std::string& input_path,
                                           BrowserPathKind path_kind);
        PackageJSON* ParsePackageJSON(const std::string& input_path);
        SideEffectsResult LoadPackageImports(const std::string& import_path, DirInfo* dir_info_package_json);
        LoadResult EsmResolveAlgorithm(FinalizeImportsExportsKind finalize_kind,
                                       const std::string& esm_package_name,
                                       const std::string& esm_package_subpath,
                                       PackageJSON* package_json,
                                       const std::string& abs_pkg_path,
                                       const std::string& abs_path);
        LoadResult FinalizeImportsExportsResult(FinalizeImportsExportsKind finalize_kind,
                                                const std::string& abs_dir_path,
                                                const ConditionsMap& conditions,
                                                PjMap& import_export_map,
                                                PackageJSON* package_json,
                                                const std::string& resolved_path,
                                                PjStatus status,
                                                const PjDebug& debug,
                                                const std::string& esm_package_name,
                                                const std::string& esm_package_subpath,
                                                const std::string& abs_import_path);
        EsmStep EsmHandlePostConditions(std::string resolved, PjStatus status, PjDebug debug);
        EsmStep EsmPackageImportsResolve(const std::string& specifier,
                                         const PjEntry& imports,
                                         const ConditionsMap& conditions);
        EsmStep EsmPackageExportsResolve(const std::string& package_url,
                                         const std::string& subpath,
                                         const PjEntry& exports,
                                         const ConditionsMap& conditions);
        EsmStep EsmPackageImportsExportsResolve(const std::string& match_key,
                                                const PjEntry& match_obj,
                                                const std::string& package_url,
                                                bool is_imports,
                                                const ConditionsMap& conditions);
        EsmStep EsmPackageTargetResolve(const std::string& package_url,
                                        const PjEntry& target,
                                        const std::string& subpath,
                                        bool pattern,
                                        bool internal,
                                        const ConditionsMap& conditions);
        ReverseResolveResult EsmPackageExportsReverseResolve(const std::string& query,
                                                             const PjEntry& root,
                                                             const ConditionsMap& conditions);
        ReverseResolveResult EsmPackageImportsExportsReverseResolve(const std::string& query,
                                                                    const PjEntry& match_obj,
                                                                    const ConditionsMap& conditions);
        ReverseResolveResult EsmPackageTargetReverseResolve(const std::string& query,
                                                            const std::string& key,
                                                            const PjEntry& target,
                                                            EsmReverseKind reverse_kind,
                                                            const ConditionsMap& conditions);

        // Internal helpers (bodies live next to their public wrappers)
        SideEffectsResult LoadPackageImportsInner(const std::string& import_path,
                                                  DirInfo* dir_info_package_json);
        LoadResult EsmResolveAlgorithmInner(FinalizeImportsExportsKind finalize_kind,
                                            const std::string& esm_package_name,
                                            const std::string& esm_package_subpath,
                                            PackageJSON* package_json,
                                            const std::string& abs_pkg_path,
                                            const std::string& abs_path);
        EsmStep EsmPackageTargetResolveStringCase(const std::string& package_url,
                                                  const PjEntry& target,
                                                  const std::string& subpath,
                                                  bool pattern,
                                                  bool is_internal);

        ////////////////////////////////////////////////////////////////////////////////
        // Implemented in "yarnpnp.cpp"

        PnpResult ResolveToUnqualified(const std::string& specifier,
                                       const std::string& parent_url,
                                       PnpData* manifest);
        LocatorResult FindLocator(PnpData* manifest, const std::string& module_url);
        LocatorResult ResolveViaFallback(PnpData* manifest, const std::string& ident);
        PackageResult GetPackage(PnpData* manifest, const std::string& ident, const std::string& reference);
        struct ExtractedYarnPnPData {
            javascript::Expr expr;
            logger::Source   source;
            bool             found = false;
        };
        ExtractedYarnPnPData ExtractYarnPnPDataFromJSON(const std::string& pnp_data_path, PnpDataMode mode);
        ExtractedYarnPnPData TryToExtractYarnPnPDataFromJS(const std::string& pnp_data_path, PnpDataMode mode);
    };

    // The resolver object: carries the filesystem and logging sinks, the
    // cached directory info, the compiled PnP manifest if the project uses
    // Plug'n'Play, and the compiled package/config JSON. One instance serves
    // the whole bundling session.
    struct Resolver {
        Resolver(filesystem::Fs& fs_, logger::Log& log_, cache::CacheSet& caches_)
                : fs(&fs_), log(&log_), caches(&caches_) {}

        filesystem::Fs*  fs;
        logger::Log*     log;
        cache::CacheSet* caches;

        TSConfigJSON* ts_config_override = nullptr;

        // Sets that represent the various condition groups for the "exports"
        // field in package.json: the defaults, and the "import" versus
        // "require" variants.
        ConditionsMap esm_conditions_default;
        ConditionsMap esm_conditions_import;
        ConditionsMap esm_conditions_require;

        // A special filtered import order for CSS "@import" imports.
        //
        // The "resolve extensions" setting determines the order of implicit
        // extensions to try when resolving imports with the extension
        // omitted. Sometimes people create a JavaScript file and a CSS file
        // with the same name when they create a component. At a high level, users expect implicit extensions to resolve to the JS file
        // when being imported from JS and to resolve to the CSS file when
        // being imported from CSS.
        //
        // What we do is create a special filtered version of the configured
        // "resolve extensions" order for CSS files that filters out any
        // extension that has been explicitly configured with a non-CSS
        // loader.
        std::vector<std::string> css_extension_order;

        // A special sorted import order for imports inside packages: the
        // language-canonical extensions are sorted after the plain
        // JavaScript extensions so that we load compiled code instead of
        // original sources published to npm.
        std::vector<std::string> node_modules_extension_order;

        // This cache maps a directory path to information about that
        // directory and all parent directories.
        std::unordered_map<std::string, DirInfo*> dir_cache;

        bool      pnp_manifest_was_checked = false;
        PnpData*  pnp_manifest             = nullptr;

        // Ownership for ephemeral objects. Pointers handed out elsewhere
        // ("ts_config_override", "DirInfo::enclosing_tsconfig_json",
        // "pnp_manifest") stay valid for the resolver's lifetime because the
        // arenas below never release them.
        std::vector<std::unique_ptr<TSConfigJSON>> ts_config_arena;
        std::vector<std::unique_ptr<PnpData>>      pnp_manifest_arena;

        config::Options options;

        // This mutex guards access to "dir_cache" which is potentially
        // mutated during path resolution. It is also locked around the whole
        // resolve operation on purpose: reducing parallelism in the resolver
        // helps the rest of the bundler go faster because resolution is
        // typically the bottleneck.
        std::mutex mutex;

        // Resolves one import path relative to "source_dir" and returns the
        // file to load, or nullopt with "debug_meta" filled in for
        // diagnostics. This is the entry point every import statement in the
        // bundler funnels through.
        //
        // Example:
        //   Resolve(r, "C:/app/src", "./util", ImportKind::JS) =>
        //     ResolveResult { path_pair.primary.text = "C:/app/src/util.js" }
        //   Resolve(r, "C:/app/src", "left-pad", ...) =>
        //     ResolveResult { path_pair.primary.text =
        //       "C:/app/node_modules/left-pad/index.js" }
        std::optional<ResolveResult> Resolve(const std::string&   source_dir,
                                             const std::string&   import_path,
                                             compiler::ImportKind kind,
                                             DebugMeta*           debug_meta);

        // Resolves every file matched by a glob pattern relative to
        // "source_dir". Returns nullopt on failure, an empty map for a
        // successful search that produced zero results, and a populated map
        // otherwise.
        //
        // Example:
        //   ResolveGlob(r, "C:/app", { { "src", "**", "*.css" } }, ...) =>
        //     { { "src/a.css" = ResolveResult{...},
        //         "src/b.css" = ResolveResult{...} } }
        std::optional<std::map<std::string, ResolveResult>> ResolveGlob(
                const std::string&                     source_dir,
                const std::vector<helpers::GlobPart>&  import_path_pattern,
                compiler::ImportKind                   kind,
                const std::string&                     pretty_pattern,
                logger::Msg*                           warning);

        // This tries to run "Resolve" on a package path as a relative path.
        // If successful, the user just forgot a leading "./" in front of the
        // path.
        std::optional<ResolveResult> ProbeResolvePackageAsRelative(const std::string&   source_dir,
                                                                   const std::string&   import_path,
                                                                   compiler::ImportKind kind,
                                                                   DebugMeta*           debug_meta);
    };

    ////////////////////////////////////////////////////////////////////////////////
    // Free functions

    // Creates a resolver wired up to "fs", "log" and "caches". Mutates
    // "options" with settings from the configured project configuration
    // override if one is present.
    std::unique_ptr<Resolver> NewResolver(config::APICall call,
                                          filesystem::Fs& fs,
                                          logger::Log& log,
                                          cache::CacheSet& caches,
                                          config::Options* options);

    // Converts "path" into human-presentable, possibly-relative strings used
    // when reporting file locations to the user.
    logger::PrettyPaths MakePrettyPaths(filesystem::Fs& fs, const logger::Path& path);

    // Package paths are loaded from a "node_modules" directory. Non-package
    // paths are relative or absolute paths.
    //
    // Example:
    //   IsPackagePath("./a")  => false
    //   IsPackagePath("../a") => false
    //   IsPackagePath("/a")   => false
    //   IsPackagePath("vue")  => true
    inline bool IsPackagePath(std::string_view path) {
        return path.substr(0, 1) != "/" && path.substr(0, 2) != "./" &&
               path.substr(0, 3) != "../" && path != "." && path != "..";
    }

} // namespace guchho::resolver