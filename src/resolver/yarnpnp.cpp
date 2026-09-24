// This file is Guchho's implementation of Yarn's Plug'n'Play (PnP) module
// resolution protocol, the node_modules-free dependency layout that Yarn can
// opt into. Under PnP no module tree is materialised on disk; instead Yarn
// writes a manifest that records, for every installed package, exactly where
// its files live and what each package depends on. Guchho consults that
// manifest to turn a bare import specifier such as "lodash" directly into an
// on-disk path, skipping the usual crawl up the directory tree in search of a
// node_modules folder.
//
// The semantics implemented here follow the published Yarn PnP specification
// (https://yarnpkg.com/advanced/pnp-spec/). Where the specification leaves
// room for interpretation, Guchho mirrors the behaviour of Yarn's own runtime
// resolver so that the paths produced here match what Yarn would resolve, not
// just what the abstract algorithm suggests.

#include "guchho/resolver.hpp"

#include <system_error>
#include <utility>

#include "guchho/cache.hpp"
#include "guchho/logger.hpp"
#include "guchho/helpers.hpp"
#include "guchho/javascript/js_parser.hpp"

namespace guchho::resolver {

    namespace {

        // Renders a single locator component for the debug log. Yarn uses the
        // empty string as a stand-in for the anonymous top-level package, so an
        // empty value is printed as the JSON literal "null" to keep the logged
        // tuple faithful to the shape Yarn itself emits, while everything else
        // is JSON-quoted the same way the rest of the log quotes strings.
        //
        //   Input:  ""            ->  Output: "null"
        //   Input:  "lodash"      ->  Output: "\"lodash\""
        std::string QuoteOrNullIfEmpty(const std::string& str)
        {
            if (!str.empty()) {
                return helpers::QuoteForJSON(str, false);
            }
            return "null";
        }

        // Extracts a string value from a JSON node the same way the shared
        // JSON helpers do, but additionally accepts the "null" literal and
        // folds it into the empty string. PnP manifests lean on null in
        // several spots to mark "the top-level package, which has no name",
        // so walking those tables must not treat null as a parse failure.
        //
        //   Input:  JSON string "your-pkg"  ->  Output: "your-pkg"
        //   Input:  JSON null               ->  Output: ""
        //   Input:  JSON object/number/etc. ->  Output: std::nullopt
        std::optional<std::string> GetStringOrNull(const javascript::Expr& json)
        {
            if (auto* value = std::get_if<std::shared_ptr<javascript::EString>>(&json.data)) {
                return helpers::UTF16ToString((*value)->value);
            }
            if (std::get_if<std::shared_ptr<javascript::ENull>>(&json.data) != nullptr) {
                return std::string{};
            }
            return std::nullopt;
        }

        // Turns the right-hand side of a PnP dependency entry into the
        // locator-shaped structure the resolver works with. Yarn serializes a
        // dependency value in exactly one of three forms: a null literal (an
        // unfulfilled peer dependency), a single string (a plain reference in
        // Yarn's "npm:1.2.3" syntax), or a two-element array holding an alias
        // ident plus a reference ("[ident, reference]"). The span carried in
        // the result points at the original JSON so any later error message
        // can highlight the dependency that caused it.
        //
        //   Input:  null                ->  { ident = "",      reference = "" }
        //   Input:  "npm:2.0.0"         ->  { ident = "",      reference = "npm:2.0.0" }
        //   Input:  ["react", "npm:18"] ->  { ident = "react", reference = "npm:18" }
        std::optional<PnpIdentAndReference> GetDependencyTarget(const javascript::Expr& json)
        {
            if (auto* null = std::get_if<std::shared_ptr<javascript::ENull>>(&json.data)) {
                (void)null;
                return PnpIdentAndReference{.ident     = {},
                                            .reference = {},
                                            .span      = logger::Range{.loc = json.loc, .len = 4}};
            }
            if (auto* str = std::get_if<std::shared_ptr<javascript::EString>>(&json.data)) {
                return PnpIdentAndReference{
                        .ident     = {},
                        .reference = helpers::UTF16ToString((*str)->value),
                        .span      = logger::Range{.loc = json.loc},
                };
            }
            if (auto* array = std::get_if<std::shared_ptr<javascript::EArray>>(&json.data)) {
                if ((*array)->items.size() == 2) {
                    auto name = internal::GetString((*array)->items[0]);
                    auto ref  = internal::GetString((*array)->items[1]);
                    if (name && ref) {
                        return PnpIdentAndReference{
                                .ident     = *name,
                                .reference = *ref,
                                .span      = logger::Range{.loc  = json.loc,
                                                           .len  = (*array)->close_bracket_loc.start + 1 - json.loc.start},
                        };
                    }
                }
            }
            return std::nullopt;
        }

    } // namespace

    // Splits a bare module specifier into the package ident (the part that
    // names the package) and the module path (the subpath inside the package,
    // with its leading slash, or empty when the specifier names the package
    // root itself). Scoped names are the only wrinkle: they begin with
    // "@scope/", so their ident runs to the second "/" rather than the first.
    //
    //   Input:  "pkg"                 ->  ident "pkg",        module_path ""
    //   Input:  "pkg/sub/path.js"     ->  ident "pkg",        module_path "/sub/path.js"
    //   Input:  "@scope/pkg"          ->  ident "@scope/pkg", module_path ""
    //   Input:  "@scope/pkg/x"        ->  ident "@scope/pkg", module_path "/x"
    //
    // A scoped specifier that never reaches a second "/" (e.g. a lone
    // "@scope") is malformed and fails, returning false.
    static bool ParseBareIdentifier(const std::string& specifier, std::string& ident, std::string& module_path)
    {
        size_t slash = specifier.find('/');

        // A scoped specifier must carry both its scope and its package name.
        if (!specifier.empty() && specifier.front() == '@') {
            if (slash == std::string::npos) {
                return false;
            }

            // Broaden the split point from the scope separator to the slash
            // that separates the package name from any following subpath.
            size_t slash2 = specifier.find('/', slash + 1);
            if (slash2 != std::string::npos) {
                ident = specifier.substr(0, slash2);
            } else {
                ident = specifier;
            }
        } else {
            // An unscoped ident ends at the first slash, or at the string end
            // when the specifier is a bare package name.
            if (slash != std::string::npos) {
                ident = specifier.substr(0, slash);
            } else {
                ident = specifier;
            }
        }

        // Whatever remains after the ident is the subpath (possibly empty).
        module_path = specifier.substr(ident.size());

        return true;
    }

    // Core of Yarn PnP resolution: maps a bare import specifier issued from a
    // known package onto an on-disk directory plus a leftover subpath. This is
    // the resolver's version of Yarn's RESOLVE_TO_UNQUALIFIED procedure, and
    // "unqualified" refers to the fact that a file extension and exact file
    // still need to be pinned down later by the ordinary resolution pipeline.
    //
    // The steps are, in order:
    //   1. Locate the package that owns the importing file (its locator).
    //      If the file belongs to no package, PnP has no say and the caller
    //      falls back to a normal node_modules walk (PnpStatus::kSkipped).
    //   2. Fetch that parent package's dependency table and look up the
    //      requested ident. A miss can be rescued by the top-level package
    //      when "enableTopLevelFallback" is turned on and the parent package
    //      is not on the exclusion list.
    //   3. Resolve the final locator (following alias indirections if Yarn
    //      redirected the dependency to a different package) and read its
    //      recorded physical location off the registry.
    //   4. Join the manifest directory, the package location, and the subpath
    //      into the absolute path that becomes "pkg_dir_path".
    //
    // The outcome is reported through PnpStatus:
    //   - kSuccess                     a package match, output left in pkg_dir_path
    //   - kSkipped                     importer outside every package -> use node_modules
    //   - kErrorDependencyNotFound     the ident is not a dependency of the parent
    //   - kErrorUnfulfilledPeerDependency the ident is declared but uninstalled
    //   - kErrorGeneric                the manifest itself broke an invariant
    //
    //   Input:  specifier  = "lodash"
    //           parent_url = "file:///app/src/index.js"
    //           manifest   = the .pnp.cjs data for /app
    //   Output: kSuccess, pkg_dir_path = "/app/.yarn/cache/lodash-npm-4.17.21-.../node_modules/lodash",
    //           pkg_ident = "lodash", pkg_subpath = ""
    PnpResult ResolverQuery::ResolveToUnqualified(const std::string& specifier,
                                                  const std::string& parent_url,
                                                  PnpData*           manifest)
    {
        // The caller has already located and parsed the manifest governing the
        // importer, so resolution can begin immediately with the specifier.
        if (debug_logs) {
            debug_logs->AddNote("Using Yarn PnP manifest from " +
                                helpers::QuoteForJSON(manifest->abs_path, false));
            debug_logs->AddNote("  Resolving " + helpers::QuoteForJSON(specifier, false) + " in " +
                                helpers::QuoteForJSON(parent_url, false));
        }

        // First break the specifier down into the package ident and the
        // subpath (which may be empty). A malformed scoped specifier is an
        // immediate error.
        std::string ident;
        std::string module_path;
        if (!ParseBareIdentifier(specifier, ident, module_path)) {
            if (debug_logs) {
                debug_logs->AddNote("  Failed to parse specifier " + helpers::QuoteForJSON(specifier, false) +
                                    " into a bare identifier");
            }
            return {.status = PnpStatus::kErrorGeneric};
        }
        if (debug_logs) {
            debug_logs->AddNote("  Parsed bare identifier " + helpers::QuoteForJSON(ident, false) +
                                " and module path " + helpers::QuoteForJSON(module_path, false));
        }

        // Determine which installed package the importing file lives in by
        // walking the manifest's location table.
        LocatorResult parent_locator_result = FindLocator(manifest, parent_url);

        // No package claims the importer, so Plug'n'Play has nothing to say
        // and the caller should retry with the classic node_modules search.
        if (!parent_locator_result.ok) {
            return {.status = PnpStatus::kSkipped};
        }
        const PnpIdentAndReference& parent_locator = parent_locator_result.locator;
        if (debug_logs) {
            debug_logs->AddNote("  Found parent locator: [" + QuoteOrNullIfEmpty(parent_locator.ident) + ", " +
                                QuoteOrNullIfEmpty(parent_locator.reference) + "]");
        }

        // Read the parent package's full metadata, including its dependency
        // table. Every located package must exist in the registry, so a miss
        // here means the manifest is internally inconsistent.
        PackageResult parent_pkg_result = GetPackage(manifest, parent_locator.ident, parent_locator.reference);
        if (!parent_pkg_result.ok) {
            return {.status = PnpStatus::kErrorGeneric};
        }
        const PnpPackage& parent_pkg = parent_pkg_result.pkg;
        if (debug_logs) {
            debug_logs->AddNote("  Found parent package at " +
                                helpers::QuoteForJSON(parent_pkg.package_location, false));
        }

        // Look the requested ident up in the parent package's dependency
        // table. A "dependency" whose value is null marks an unfulfilled peer,
        // which is a late failure rather than a table miss.
        bool                          ok                 = false;
        const PnpIdentAndReference*   reference_or_alias = nullptr;
        PnpIdentAndReference          fallback_reference;
        if (auto it = parent_pkg.package_dependencies.find(ident); it != parent_pkg.package_dependencies.end()) {
            reference_or_alias = &it->second;
            ok                 = true;
        }

        // The ident was not listed. When top-level fallback is enabled the
        // root package gets a chance to supply it, provided the parent package
        // has not been excluded from that mechanism.
        if (!ok || reference_or_alias->reference.empty()) {
            if (debug_logs) {
                debug_logs->AddNote("  Failed to find " + helpers::QuoteForJSON(ident, false) +
                                    " in \"packageDependencies\" of parent package");
            }

            if (manifest->enable_top_level_fallback) {
                if (debug_logs) {
                    debug_logs->AddNote(
                            "  Searching for a fallback because \"enableTopLevelFallback\" is true");
                }

                // Excluded parents are skipped outright, so a fallback does not
                // accidentally re-introduce a dependency the project decided to
                // keep inaccessible from there.
                auto exclusion_it = manifest->fallback_exclusion_list.find(parent_locator.ident);
                bool is_excluded =
                        exclusion_it != manifest->fallback_exclusion_list.end() &&
                        exclusion_it->second.find(parent_locator.reference) != exclusion_it->second.end();
                if (!is_excluded) {
                    // Ask the top-level package and the global fallback pool.
                    LocatorResult fallback = ResolveViaFallback(manifest, ident);

                    // A fallback only counts if it actually names a reference;
                    // an entry that resolves to nothing leaves the miss intact.
                    if (fallback.ok && !fallback.locator.reference.empty()) {
                        fallback_reference = std::move(fallback.locator);
                        reference_or_alias = &fallback_reference;
                        ok                 = true;
                    }
                } else if (debug_logs) {
                    debug_logs->AddNote("    Stopping because [" + QuoteOrNullIfEmpty(parent_locator.ident) + ", " +
                                        QuoteOrNullIfEmpty(parent_locator.reference) +
                                        "] is in \"fallbackExclusionList\"");
                }
            }
        }

        // The ident remains unresolved after all fallback attempts: report it
        // as a missing dependency and let the caller formulate the error.
        if (!ok) {
            PnpResult result;
            result.status      = PnpStatus::kErrorDependencyNotFound;
            result.error_ident = ident;
            result.error_range = parent_pkg.package_dependencies_range;
            return result;
        }

        // The ident was found but its dependency value is null, the signature
        // of an unfulfilled peer dependency. That deserves its own, more
        // specific error than a plain missing dependency.
        if (reference_or_alias->reference.empty()) {
            PnpResult result;
            result.status      = PnpStatus::kErrorUnfulfilledPeerDependency;
            result.error_ident = ident;
            result.error_range = reference_or_alias->span;
            return result;
        }

        if (debug_logs) {
            std::string reference_or_alias_str;
            if (!reference_or_alias->ident.empty()) {
                reference_or_alias_str = "[" + helpers::QuoteForJSON(reference_or_alias->ident, false) + ", " +
                                         helpers::QuoteForJSON(reference_or_alias->reference, false) + "]";
            } else {
                reference_or_alias_str = QuoteOrNullIfEmpty(reference_or_alias->reference);
            }
            debug_logs->AddNote("  Found dependency locator: [" + QuoteOrNullIfEmpty(ident) + ", " +
                                reference_or_alias_str + "]");
        }

        // Fetch the dependency's own metadata. Yarn may have redirected the
        // dependency to a different package altogether (an alias), in which
        // case the alias locator is what gets looked up; otherwise the ident
        // and the gathered reference form the locator directly.
        PackageResult dependency_pkg_result;
        if (!reference_or_alias->ident.empty()) {
            const PnpIdentAndReference& alias = *reference_or_alias;

            dependency_pkg_result = GetPackage(manifest, alias.ident, alias.reference);
            if (!dependency_pkg_result.ok) {
                return {.status = PnpStatus::kErrorGeneric};
            }
        } else {
            dependency_pkg_result = GetPackage(manifest, ident, reference_or_alias->reference);
            if (!dependency_pkg_result.ok) {
                return {.status = PnpStatus::kErrorGeneric};
            }
        }
        const PnpPackage& dependency_pkg = dependency_pkg_result.pkg;
        if (debug_logs) {
            debug_logs->AddNote("  Found package " + helpers::QuoteForJSON(ident, false) + " at " +
                                helpers::QuoteForJSON(dependency_pkg.package_location, false));
        }

        // Assemble the final path from the manifest directory, the package's
        // recorded location, and the original subpath.
        std::string abs_dir_path = manifest->abs_dir_path;
        bool        is_windows   = abs_dir_path.compare(0, 1, "/") != 0;
        if (is_windows) {
            // Windows paths are converted to Unix-style "/C:/..." form for the
            // duration of the join. Yarn stores its cache on one drive (say
            // "C:") while projects often live on another ("D:"), and Yarn then
            // crosses drives with "../C:" segments. Windows does not let a path
            // escape its own drive root, so joining the two styles directly
            // would silently clamp onto "D:" and Guchho would resolve to the
            // wrong location. Temporarily normalising to forward slashes with a
            // leading "/" makes the join behave on every drive layout.
            for (char& c : abs_dir_path) {
                if (c == '\\') {
                    c = '/';
                }
            }
            abs_dir_path = "/" + abs_dir_path;
        }
        std::string pkg_dir_path = internal::PosixPathJoin({abs_dir_path, dependency_pkg.package_location});
        if (is_windows && !pkg_dir_path.empty() && pkg_dir_path.front() == '/') {
            // Once the join is complete, strip the temporary "/" back off so
            // the path once again looks like a native Windows path.
            pkg_dir_path = pkg_dir_path.substr(1);
        }
        if (debug_logs) {
            debug_logs->AddNote("  Resolved " + helpers::QuoteForJSON(specifier, false) + " via Yarn PnP to " +
                                helpers::QuoteForJSON(pkg_dir_path, false) + " with subpath " +
                                helpers::QuoteForJSON(module_path, false));
        }
        return {.status       = PnpStatus::kSuccess,
                .pkg_dir_path = std::move(pkg_dir_path),
                .pkg_ident    = std::move(ident),
                .pkg_subpath  = std::move(module_path)};
    }

    // Determines which installed package owns a given file by relating its URL
    // back to the manifest directory. Ownership is purely positional: the
    // matching package is the one with the deepest directory prefix that still
    // contains the file. Guchho walks the manifest's location table from the
    // file upward, exactly mirroring Yarn's runtime resolver loop, so the
    // answers agree with what Yarn itself reports rather than with the slower
    // "ideal" walk the specification describes.
    //
    // An "ignorePatternData" check runs first: paths matching the manifest's
    // ignore pattern are deliberately claimed by nothing (Yarn uses this for
    // virtual, workspace-internal paths), so they never take ownership.
    //
    //   Input:  module_url = "file:///app/node_modules/.pnpm/foo@1.0.0/node_modules/foo/index.js"
    //   Output: ok = true, locator = { ident = "foo", reference = "npm:1.0.0" }
    //   Input:  module_url = "file:///tmp/unrelated.js"
    //   Output: ok = false (empty result, file belongs to no package)
    LocatorResult ResolverQuery::FindLocator(PnpData* manifest, const std::string& module_url)
    {
        // Express the file as a path relative to the manifest directory; a
        // failure here means the two are not even on comparable roots.
        auto relative_url_result = r->fs->Rel(manifest->abs_dir_path, module_url);
        if (!relative_url_result) {
            return {};
        }
        std::string relative_url = *relative_url_result;

        // Normalise backslashes to forward slashes so the lookup keys match
        // the "/"-separated locations Yarn records in the manifest.
        for (char& c : relative_url) {
            if (c == '\\') {
                c = '/';
            }
        }

        // The manifest stores ".", "./" and "../" styles; trim a redundant
        // leading "./" now so the path lines up with the recorded keys later.
        if (relative_url.starts_with("./")) {
            relative_url.erase(0, 2);
        }

        // Files matching the ignore pattern are intentionally unowned.
        if (manifest->ignore_pattern_data &&
            std::regex_search(relative_url, *manifest->ignore_pattern_data)) {
            if (debug_logs) {
                debug_logs->AddNote("  Ignoring " + helpers::QuoteForJSON(relative_url, false) +
                                    " because it matches \"ignorePatternData\"");
            }

            return {};
        }

        // Pad the relative path back out to a form the location table uses:
        // it must carry a trailing "/" and begin with a "./" or "../" marker.
        if (!relative_url.ends_with('/')) {
            relative_url += "/";
        }
        if (!relative_url.starts_with("./") && !relative_url.starts_with("../")) {
            relative_url = "./" + relative_url;
        }

        // Walk the directory components from deepest to shallowest, consulting
        // the location table at every level until a still-valid package is
        // found or the path is exhausted. Entries flagged discard_from_lookup
        // are skipped exactly as Yarn would.
        while (true) {
            auto entry_it = manifest->package_locators_by_locations.find(relative_url);
            if (entry_it == manifest->package_locators_by_locations.end() ||
                entry_it->second.discard_from_lookup) {
                // Drop the last path component and try the parent directory.
                size_t last_slash = relative_url.find_last_of('/', relative_url.size() - 2);
                relative_url      = relative_url.substr(0, last_slash + 1);
                if (relative_url.empty()) {
                    break;
                }
                continue;
            }
            LocatorResult result;
            result.locator = entry_it->second.locator;
            result.ok      = true;
            return result;
        }

        return {};
    }

    // Implements Yarn's RESOLVE_VIA_FALLBACK helper: when a package does not
    // itself list a dependency, the manifest may still supply one centrally.
    // The search consults the top-level package's own dependency table first,
    // then the global "fallbackPool". This is how Yarn lets a single root
    // project nominate one shared resolution for a dependency name across the
    // whole workspace. The caller must already have confirmed that top-level
    // fallback is enabled and that the parent package is not excluded before
    // invoking this.
    //
    //   Input:  manifest whose fallbackPool contains ["react", "npm:18.2.0"], ident = "react"
    //   Output: ok = true, locator = { ident = "", reference = "npm:18.2.0" }
    //   Input:  ident not present anywhere
    //   Output: ok = false (empty result)
    LocatorResult ResolverQuery::ResolveViaFallback(PnpData* manifest, const std::string& ident)
    {
        // The anonymous top-level package is addressed by the empty locator
        // pair "" / ""; it must always resolve, or the manifest is broken.
        PackageResult top_level_pkg_result = GetPackage(manifest, "", "");
        if (!top_level_pkg_result.ok) {
            return {};
        }
        const PnpPackage& top_level_pkg = top_level_pkg_result.pkg;

        // First candidate: a direct dependency entry on the top-level package.
        auto dep_it = top_level_pkg.package_dependencies.find(ident);

        if (dep_it != top_level_pkg.package_dependencies.end()) {
            if (debug_logs) {
                debug_logs->AddNote("    Found fallback for " + helpers::QuoteForJSON(ident, false) +
                                    " in \"packageDependencies\" of top-level package: [" +
                                    QuoteOrNullIfEmpty(dep_it->second.ident) + ", " +
                                    QuoteOrNullIfEmpty(dep_it->second.reference) + "]");
            }
            LocatorResult result;
            result.locator = dep_it->second;
            result.ok      = true;
            return result;
        }

        // Second candidate: the global fallback pool. Whatever it yields (a
        // match or a miss) is the final word.
        auto pool_it = manifest->fallback_pool.find(ident);

        if (debug_logs) {
            if (pool_it != manifest->fallback_pool.end()) {
                debug_logs->AddNote("    Found fallback for " + helpers::QuoteForJSON(ident, false) +
                                    " in \"fallbackPool\": [" + QuoteOrNullIfEmpty(pool_it->second.ident) + ", " +
                                    QuoteOrNullIfEmpty(pool_it->second.reference) + "]");
            } else {
                debug_logs->AddNote("    Failed to find fallback for " + helpers::QuoteForJSON(ident, false) +
                                    " in \"fallbackPool\"");
            }
        }
        if (pool_it == manifest->fallback_pool.end()) {
            return {};
        }
        LocatorResult result;
        result.locator = pool_it->second;
        result.ok      = true;
        return result;
    }

    // Fetches the full metadata for an installed package identified by its
    // [ident, reference] locator. The lookup is a double hash: ident first,
    // then reference within that ident's bucket. A not-ok result is returned
    // when the pair is unknown. The PnP specification guarantees that every
    // locator referenced anywhere else in the manifest exists here, so a miss
    // is evidence of a corrupt manifest and is treated as an invariant
    // violation by callers.
    //
    //   Input:  ident = "foo", reference = "npm:1.0.0"
    //   Output: ok = true, pkg = the installed package for foo@1.0.0
    //   Input:  ident/reference unknown to the registry
    //   Output: ok = false (empty result)
    PackageResult ResolverQuery::GetPackage(PnpData* manifest, const std::string& ident, const std::string& reference)
    {
        if (auto outer_it = manifest->package_registry_data.find(ident);
            outer_it != manifest->package_registry_data.end()) {
            if (auto inner_it = outer_it->second.find(reference); inner_it != outer_it->second.end()) {
                PackageResult result;
                result.pkg = inner_it->second;
                result.ok  = true;
                return result;
            }
        }

        if (debug_logs) {
            // Reaching this point means the manifest referenced a package it
            // never registered, which every row of every table in the manifest
            // is supposed to prevent. Log the offender so a broken manifest can
            // be identified instead of silently misbehaving, then signal the
            // failure to the caller.
            debug_logs->AddNote("  Yarn PnP invariant violation: GET_PACKAGE failed to find a package: [" +
                                QuoteOrNullIfEmpty(ident) + ", " + QuoteOrNullIfEmpty(reference) + "]");
        }
        return {};
    }

    // Compiles a parsed Yarn PnP manifest (a big, deeply nested JSON document)
    // into the flat, hash-addressable tables the resolver queries during a
    // resolve. Doing the flattening once up front means a single module lookup
    // never has to touch raw JSON again; everything below is pure map access.
    //
    // The tables produced are:
    //   - packageRegistryData          locator [ident, reference] -> PnpPackage
    //     with the physical location, dependency table, and source span.
    //   - packageLocatorsByLocations   package directory key -> owning locator,
    //     which is what FindLocator walks to answer "which package owns this file?".
    //   - fallbackPool                 ident -> dependency target offered up by
    //     the top-level fallback mechanism.
    //   - fallbackExclusionList        locators that must never use the fallback.
    //   - ignorePatternData            regex of paths deliberately left unowned.
    //
    // Manifests are machine-generated, so a handful of tolerances are baked in:
    // rows that do not have the expected nested shape are skipped rather than
    // treated as fatal, an unparseable ignore pattern is remembered verbatim so
    // the caller can report it later, and when two packages claim the same
    // physical location the non-discarded one wins (matching Yarn's hydration
    // behaviour).
    //
    //   Input:  abs_path     = "/app/.pnp.cjs"
    //           abs_dir_path = "/app"
    //           json         = parsed manifest expression
    //   Output: fully populated PnpData ready for ResolveToUnqualified
    std::unique_ptr<PnpData> CompileYarnPnPData(const std::string&      abs_path,
                                                const std::string&      abs_dir_path,
                                                const javascript::Expr& json,
                                                const logger::Source&   source)
    {
        auto data             = std::make_unique<PnpData>();
        data->abs_path        = abs_path;
        data->abs_dir_path    = abs_dir_path;
        data->tracker         = logger::LineColumnTracker(&source);

        // Whether dependencies missing from a package may be supplied by the
        // root project (the "enableTopLevelFallback" flag). Defaults to false
        // when the field is absent, matching Yarn's defaults.
        if (auto value = internal::GetProperty(json, "enableTopLevelFallback")) {
            if (auto enable_top_level_fallback = internal::GetBool(value->first)) {
                data->enable_top_level_fallback = *enable_top_level_fallback;
            }
        }

        // "fallbackExclusionList" names the parent packages that must not get
        // the top-level fallback treatment. Each entry is a locator paired with
        // a list of references, and the whole tuple is stored as ident -> set of
        // excluded references.
        if (auto value = internal::GetProperty(json, "fallbackExclusionList")) {
            if (auto* array = std::get_if<std::shared_ptr<javascript::EArray>>(&value->first.data)) {
                for (const javascript::Expr& item : (*array)->items) {
                    auto* tuple = std::get_if<std::shared_ptr<javascript::EArray>>(&item.data);
                    if (!tuple || (*tuple)->items.size() != 2) {
                        continue;
                    }
                    auto ident = GetStringOrNull((*tuple)->items[0]);
                    if (!ident) {
                        continue;
                    }
                    auto* array2 = std::get_if<std::shared_ptr<javascript::EArray>>(&(*tuple)->items[1].data);
                    if (!array2) {
                        continue;
                    }
                    std::unordered_map<std::string, bool> references;
                    for (const javascript::Expr& item2 : (*array2)->items) {
                        if (auto reference = internal::GetString(item2)) {
                            references.emplace(std::move(*reference), true);
                        }
                    }
                    data->fallback_exclusion_list.emplace(*std::move(ident), std::move(references));
                }
            }
        }

        // "fallbackPool" supplies dependency targets for the top-level
        // fallback. Entries are ident -> dependency target pairs; the target
        // may itself be an alias, so it is decoded with the shared dependency
        // parser rather than as a plain string.
        if (auto value = internal::GetProperty(json, "fallbackPool")) {
            if (auto* array = std::get_if<std::shared_ptr<javascript::EArray>>(&value->first.data)) {
                for (const javascript::Expr& item : (*array)->items) {
                    auto* array2 = std::get_if<std::shared_ptr<javascript::EArray>>(&item.data);
                    if (!array2 || (*array2)->items.size() != 2) {
                        continue;
                    }
                    auto ident = internal::GetString((*array2)->items[0]);
                    if (!ident) {
                        continue;
                    }
                    auto dependency_target = GetDependencyTarget((*array2)->items[1]);
                    if (dependency_target) {
                        data->fallback_pool.emplace(*std::move(ident), std::move(*dependency_target));
                    }
                }
            }
        }

        // "ignorePatternData" is a regular expression over relative paths that
        // Yarn uses to declare certain paths (its virtual workspace internals)
        // unowned. Guchho's regex engine is stricter than the source one, and
        // in particular cannot compile the negative lookaheads Yarn emits to
        // rule out "." and ".." segments mid-path. Those segments are never
        // produced by Guchho to begin with, so the offending sequences are
        // stripped out of the pattern before compilation; this keeps the intent
        // of the pattern intact while making it compilable. A pattern that
        // still fails to compile is stored verbatim so the caller can surface
        // the problem on demand.
        if (auto value = internal::GetProperty(json, "ignorePatternData")) {
            if (auto ignore_pattern_data = internal::GetString(value->first)) {
                std::string pattern = *ignore_pattern_data;
                auto        remove_all = [&pattern](std::string_view from) {
                    size_t pos = 0;
                    while ((pos = pattern.find(from, pos)) != std::string::npos) {
                        pattern.erase(pos, from.size());
                    }
                };
                remove_all(R"((?!\.))");
                remove_all(R"((?!(?:^|\/)\.))");
                remove_all(R"((?!\.{1,2}(?:\/|$)))");
                remove_all(R"((?!(?:^|\/)\.{1,2}(?:\/|$)))");

                try {
                    data->ignore_pattern_data = std::make_unique<std::regex>(pattern);
                } catch (const std::regex_error&) {
                    data->invalid_ignore_pattern_data = std::move(pattern);
                }
            }
        }

        // "packageRegistryData" is the heart of the manifest: package locators
        // mapped to their metadata. Each entry is [ident, [[reference, pkg],
        // ...]]; each pkg carries its on-disk location, its dependency list,
        // and an optional discard marker. Beyond building the registry itself,
        // the physical locations are also walked into the reverse
        // "packageLocatorsByLocations" index so FindLocator can answer
        // ownership questions cheaply.
        if (auto value = internal::GetProperty(json, "packageRegistryData")) {
            if (auto* array = std::get_if<std::shared_ptr<javascript::EArray>>(&value->first.data)) {
                for (const javascript::Expr& item : (*array)->items) {
                    auto* tuple = std::get_if<std::shared_ptr<javascript::EArray>>(&item.data);
                    if (!tuple || (*tuple)->items.size() != 2) {
                        continue;
                    }
                    auto package_ident = GetStringOrNull((*tuple)->items[0]);
                    if (!package_ident) {
                        continue;
                    }
                    auto* array2 = std::get_if<std::shared_ptr<javascript::EArray>>(&(*tuple)->items[1].data);
                    if (!array2) {
                        continue;
                    }
                    auto& references = data->package_registry_data[*package_ident];

                    for (const javascript::Expr& item2 : (*array2)->items) {
                        auto* tuple2 = std::get_if<std::shared_ptr<javascript::EArray>>(&item2.data);
                        if (!tuple2 || (*tuple2)->items.size() != 2) {
                            continue;
                        }
                        auto package_reference = GetStringOrNull((*tuple2)->items[0]);
                        if (!package_reference) {
                            continue;
                        }
                        const javascript::Expr& pkg = (*tuple2)->items[1];

                        auto package_location_prop = internal::GetProperty(pkg, "packageLocation");
                        if (!package_location_prop) {
                            continue;
                        }
                        auto package_dependencies_prop = internal::GetProperty(pkg, "packageDependencies");
                        if (!package_dependencies_prop) {
                            continue;
                        }
                        auto package_location = internal::GetString(package_location_prop->first);
                        if (!package_location) {
                            continue;
                        }
                        auto* array3 =
                                std::get_if<std::shared_ptr<javascript::EArray>>(
                                        &package_dependencies_prop->first.data);
                        if (!array3) {
                            continue;
                        }

                        std::unordered_map<std::string, PnpIdentAndReference> deps;
                        bool discard_from_lookup = false;

                        // Reassemble the flat dependency table of this package
                        // from its entry list, reusing the shared handler so
                        // null, string, and alias-shaped targets all decode
                        // consistently.
                        for (const javascript::Expr& dep : (*array3)->items) {
                            auto* array4 = std::get_if<std::shared_ptr<javascript::EArray>>(&dep.data);
                            if (!array4 || (*array4)->items.size() != 2) {
                                continue;
                            }
                            auto ident = internal::GetString((*array4)->items[0]);
                            if (!ident) {
                                continue;
                            }
                            auto dependency_target = GetDependencyTarget((*array4)->items[1]);
                            if (dependency_target) {
                                deps.emplace(*std::move(ident), std::move(*dependency_target));
                            }
                        }

                        // A discarded package may be present on disk but must
                        // never be found by walking up the directory tree; only
                        // direct dependency references may reach it.
                        if (auto discard_value = internal::GetProperty(pkg, "discardFromLookup")) {
                            if (auto discard_bool = internal::GetBool(discard_value->first)) {
                                discard_from_lookup = *discard_bool;
                            }
                        }

                        PnpPackage pnp_pkg;
                        pnp_pkg.package_location          = *package_location;
                        pnp_pkg.package_dependencies      = std::move(deps);
                        pnp_pkg.package_dependencies_range = logger::Range{
                                .loc = package_dependencies_prop->first.loc,
                                .len = (*array3)->close_bracket_loc.start + 1 -
                                       package_dependencies_prop->first.loc.start,
                        };
                        pnp_pkg.discard_from_lookup = discard_from_lookup;

                        // Multiple packages can share one physical directory
                        // (Yarn does this when deduplicating identical
                        // installs). The reverse index keeps the first claimed
                        // locator, or the most authoritative non-discarded one
                        // when they disagree, mirroring how Yarn hydrates its
                        // own runtime state.
                        auto location_it = data->package_locators_by_locations.find(*package_location);
                        if (location_it == data->package_locators_by_locations.end()) {
                            data->package_locators_by_locations.emplace(
                                    *package_location,
                                    PnpPackageLocatorByLocation{
                                            .locator            = PnpIdentAndReference{.ident     = *package_ident,
                                                                                       .reference = *package_reference},
                                            .discard_from_lookup = discard_from_lookup,
                                    });
                        } else {
                            PnpPackageLocatorByLocation& entry = location_it->second;
                            entry.discard_from_lookup          = entry.discard_from_lookup && discard_from_lookup;
                            if (!discard_from_lookup) {
                                entry.locator = PnpIdentAndReference{.ident     = *package_ident,
                                                                     .reference = *package_reference};
                            }
                        }

                        references.emplace(*std::move(package_reference), std::move(pnp_pkg));
                    }
                }
            }
        }

        return data;
    }

    // Loads a PnP manifest that is stored as plain JSON (.pnp.data.json) and
    // returns it ready for CompileYarnPnPData. The bytes come through Guchho's
    // filesystem cache, so repeated reads of the same manifest are cheap. Read
    // failures are reported through the active log, except that a missing file
    // is treated as a quiet "not found" when the caller only probed for the
    // manifest's existence. On success the raw JSON expression is kept
    // alongside the source text so diagnostic messages can later quote the
    // original file verbatim.
    //
    //   Input:  pnp_data_path = "/app/.pnp.data.json", mode = kReportErrorsAboutMissingFiles
    //   Output: found = true, source = the file contents, expr = the parsed manifest JSON
    //   Input:  file does not exist
    //   Output: found = false (empty result)
    ResolverQuery::ExtractedYarnPnPData ResolverQuery::ExtractYarnPnPDataFromJSON(const std::string& pnp_data_path,
                                                                                  PnpDataMode mode)
    {
        ExtractedYarnPnPData result;

        filesystem::FsResult<std::string> read = r->caches->fs_cache.ReadFile(*r->fs, pnp_data_path);
        if (debug_logs && !read.original_error.empty()) {
            debug_logs->AddNote("Failed to read file " + helpers::QuoteForJSON(pnp_data_path, false) + ": " +
                                read.original_error);
        }
        if (!read.Ok()) {
            bool is_enoent = read.canonical_error == std::errc::no_such_file_or_directory;
            if (mode == PnpDataMode::kReportErrorsAboutMissingFiles || !is_enoent) {
                std::string read_error = read.original_error;
                if (read_error.empty() && read.canonical_error) {
                    read_error = std::make_error_code(*read.canonical_error).message();
                }
                logger::Path key_path{.text = pnp_data_path, .namespace_ = "file"};
                logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, key_path);
                r->log->AddError(nullptr, logger::Range{},
                                 logger::FormatMsg(logger::MsgCat::kResolver_CannotReadFile,
                                                   pretty_paths.Select(r->options.LogPathStyle), read_error));
            }
            return result;
        }
        if (debug_logs) {
            debug_logs->AddNote("The file " + helpers::QuoteForJSON(pnp_data_path, false) + " exists");
        }

        logger::Path key_path{.text = pnp_data_path, .namespace_ = "file"};
        result.source = logger::Source{
                .pretty_paths = MakePrettyPaths(*r->fs, key_path),
                .contents     = read.value,
                .key_path     = key_path,
        };
        result.expr = r->caches->json_cache.Parse(*r->log, result.source, javascript::JSONOptions{}).first;
        result.found = true;
        return result;
    }

    // Loads a PnP manifest that ships as an executable script (.pnp.cjs or
    // .pnp.js) rather than as a standalone JSON document. While the file is
    // JavaScript, the manifest itself is a single object literal issued through
    // a module assignment near the top; everything else in the file is runtime
    // scaffolding. Guchho parses the file as a program with the parser
    // configured for exactly this shape, then lifts out the embedded expression
    // directly - no execution, no evaluation, just a parse. This is what makes
    // it safe to read dependency data out of what is nominally an executable
    // file. Read-error handling, caching, and source baggage all match the
    // plain-JSON variant above.
    //
    //   Input:  pnp_data_path = "/app/.pnp.cjs", mode = kSilentAboutMissingFiles
    //   Output: found = true, source = the file contents, expr = the extracted manifest expression
    //   Input:  file is not a supported manifest script
    //   Output: found = false (empty result)
    ResolverQuery::ExtractedYarnPnPData ResolverQuery::TryToExtractYarnPnPDataFromJS(const std::string& pnp_data_path,
                                                                                     PnpDataMode mode)
    {
        ExtractedYarnPnPData result;

        filesystem::FsResult<std::string> read = r->caches->fs_cache.ReadFile(*r->fs, pnp_data_path);
        if (debug_logs && !read.original_error.empty()) {
            debug_logs->AddNote("Failed to read file " + helpers::QuoteForJSON(pnp_data_path, false) + ": " +
                                read.original_error);
        }
        if (!read.Ok()) {
            bool is_enoent = read.canonical_error == std::errc::no_such_file_or_directory;
            if (mode == PnpDataMode::kReportErrorsAboutMissingFiles || !is_enoent) {
                std::string read_error = read.original_error;
                if (read_error.empty() && read.canonical_error) {
                    read_error = std::make_error_code(*read.canonical_error).message();
                }
                logger::Path key_path{.text = pnp_data_path, .namespace_ = "file"};
                logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, key_path);
                r->log->AddError(nullptr, logger::Range{},
                                 logger::FormatMsg(logger::MsgCat::kResolver_CannotReadFile,
                                                   pretty_paths.Select(r->options.LogPathStyle), read_error));
            }
            return result;
        }
        if (debug_logs) {
            debug_logs->AddNote("The file " + helpers::QuoteForJSON(pnp_data_path, false) + " exists");
        }

        logger::Path key_path{.text = pnp_data_path, .namespace_ = "file"};
        result.source = logger::Source{
                .pretty_paths = MakePrettyPaths(*r->fs, key_path),
                .contents     = read.value,
                .key_path     = key_path,
        };

        auto [ast, ok] =
                r->caches->js_cache.Parse(*r->log, result.source, javascript::OptionsForYarnPnP());
        (void)ok;
        result.expr = ast.manifest_for_yarn_pnp;

        if (debug_logs &&
            std::visit([](const auto& ptr) { return ptr != nullptr; }, result.expr.data)) {
            debug_logs->AddNote("  Extracted JSON data from " + helpers::QuoteForJSON(pnp_data_path, false));
        }
        result.found = true;
        return result;
    }

} // namespace guchho::resolver