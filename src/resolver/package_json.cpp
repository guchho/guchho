// This file implements Guchho's handling of "package.json": the fields that
// control how imports and exports resolve. A single "package.json" can shape
// resolution in several independent ways:
//
//   - The "main", "module", and "browser" fields name the entry point to load
//     when a bare specifier lands on the package as a whole.
//   - The object-literal form of the "browser" field remaps (or outright
//     disables) individual paths when building for the browser platform.
//   - The "sideEffects" field tells the bundler which files may be dropped as
//     dead code and which must be preserved.
//   - The "imports" and "exports" maps implement conditional, ESM-style
//     resolution, where a specifier (prefixed with "#" for imports or with a
//     "." for exports) is matched against a path map gated by conditions such
//     as "import", "require", and "browser".
//
// The work is split into two halves. The first half turns a parsed JSON
// expression tree into the shallow "PjEntry"/"PjMap" structures the resolver
// walks at runtime, validating the shape of the maps along the way. The
// second half implements the per-query resolution steps that take a specifier
// plus a set of active conditions and produce an absolute path. Those query
// methods are declared in "resolver.hpp" but defined here so that everything
// about the "imports" and "exports" maps lives in one place.

#include "guchho/resolver.hpp"

#include <algorithm>

#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/javascript/js_lexer.hpp"

namespace guchho::resolver {

    // Reports whether the map uses dot-prefixed subpath keys (".", "./feature")
    // rather than plain condition names ("import", "require", "default"). A
    // well-formed map is never mixed, because the parser rejects objects that
    // fuse the two styles, so inspecting the first key is sufficient. This
    // distinction decides whether a target object's keys select among
    // conditions (a conditional sugar object) or among exported subpaths.
    //
    //   Input:  entry with keys {".", "./feature"}      ->  true
    //   Input:  entry with keys {"node", "default"}     ->  false
    bool PjEntryKeysStartWithDot(const PjEntry& entry)
    {
        return !entry.map_data.empty() && entry.map_data[0].key.starts_with(".");
    }

    // Linear search for a top-level key in a parsed "imports"/"exports" map,
    // returning a pointer to the value bound to that key. The search is a
    // straight scan rather than a hash lookup because these maps are tiny and
    // the keys that exact matching cares about (such as "." for exports) sit
    // near the front of the list.
    //
    //   Input:  entry keys {".", "./foo"}, key "."     ->  pointer to the "." target
    //   Input:  entry keys {".", "./foo"}, key "./bar" ->  nullptr
    const PjEntry* PjEntryValueForKey(const PjEntry& entry, const std::string& key)
    {
        for (const PjMapEntry& item : entry.map_data) {
            if (item.key == key) {
                return &item.value;
            }
        }
        return nullptr;
    }

    // Turns a "sideEffects" glob pattern into an anchored regular expression
    // and reports whether the pattern contained any wildcard metacharacters.
    // A "**" run that spans a whole path segment (a globstar) matches zero or
    // more nested directories, while a single "*" stays confined to one
    // segment. Every regex metacharacter found in the input is escaped so the
    // expression matches only what the glob semantics intend. Entries with no
    // wildcard are less expensive to test than the regex engine, so the caller
    // keeps this flag to decide how to store the pattern.
    //
    //   Input:  "/app/src/**/index.*"   ->  "^/app/src/(?:[^/]*(?:/|$))*index\.[^/]*$", true
    //   Input:  "/plain/path.js"        ->  "^/plain/path\.js$", false
    std::pair<std::string, bool> GlobstarToEscapedRegexp(const std::string& glob)
    {
        std::string result;
        result += '^';
        bool had_wildcard = false;
        size_t n          = glob.size();

        for (size_t i = 0; i < n; ++i) {
            char c = glob[i];
            switch (c) {
            case '\\': case '^': case '$': case '.': case '+': case '|':
            case '(':  case ')': case '[': case ']': case '{': case '}':
                result += '\\';
                result += c;
                break;

            case '?':
                result += '.';
                had_wildcard = true;
                break;

            case '*': {
                // Count how many consecutive "*"s make up this run and keep
                // the characters that sandwich it, which are needed to decide
                // whether it is a true globstar or an ordinary wildcard.
                int prev_char = -1;
                if (i > 0) {
                    prev_char = static_cast<uint8_t>(glob[i - 1]);
                }
                size_t star_count = 1;
                while (i + 1 < n && glob[i + 1] == '*') {
                    ++star_count;
                    ++i;
                }
                int next_char = -1;
                if (i + 1 < n) {
                    next_char = static_cast<uint8_t>(glob[i + 1]);
                }

                // A globstar is a run of two or more "*"s that spans a whole
                // path segment: it must begin at the start of a segment (or of
                // the entire string) and run all the way to the next "/" or to
                // the end.
                bool is_globstar =
                        star_count > 1 &&                        // multiple "*"'s
                        (prev_char == '/' || prev_char == -1) && // from the start of the segment
                        (next_char == '/' || next_char == -1);   // to the end of the segment

                if (is_globstar) {
                    // A globstar stands for any number of nested directories,
                    // including none at all, so emit a group that consumes zero
                    // or more segments and their trailing slashes.
                    result += "(?:[^/]*(?:/|$))*";
                    ++i; // Skip the "/" so the loop does not re-read it
                } else {
                    // An ordinary "*" matches any characters except "/", so it
                    // never escapes the segment it lives in.
                    result += "[^/]*";
                }

                had_wildcard = true;
                break;
            }

            default:
                result += c;
                break;
            }
        }

        result += '$';
        return {std::move(result), had_wildcard};
    }

    namespace {

        // Collects evidence about a conditional branch inside an
        // "exports"/"imports" object that can never influence resolution,
        // because an earlier sibling already wins for every possible query:
        // either a "default" entry appeared before it, or both "import" and
        // "require" did, which together cover every import kind the resolver
        // knows. The reason string, the source ranges of the offending keys,
        // and any explanatory notes are stored so the warning can later be
        // emitted with full context.
        struct DeadCondition {
            std::string            reason;
            std::vector<logger::Range> ranges;
            std::vector<logger::MsgData> notes;
        };

        // Orders expansion keys from most specific to least specific, the
        // ordering the matching algorithm relies on so that the most precise
        // key always wins when several could match:
        //
        //   - The "base" of a key -- the text up to its first "*", or the
        //     whole key when it has none -- dominates, and the longer base
        //     sorts first.
        //   - Between keys of equal base length, a key containing a "*" (a
        //     real pattern) sorts above a fixed key, which could otherwise
        //     shadow a pattern that matches the same prefix and more.
        //   - Between equal bases that both hold a star, the longer key is
        //     the more specific match and sorts first.
        //
        // Returns true when "a" must be ordered before "b".
        bool ExpansionKeysLess(const PjMapEntry& a, const PjMapEntry& b)
        {
            // Both keys are guaranteed to end in "/" or hold exactly one
            // "*", a property established by the parser before the sort runs.
            const std::string& key_a = a.key;
            const std::string& key_b = b.key;

            // Measure each key's base: the part before its "*" when it has
            // one, otherwise the whole key length.
            size_t star_a = key_a.find('*');
            size_t star_b = key_b.find('*');
            size_t base_length_a = star_a != std::string::npos ? star_a : key_a.size();
            size_t base_length_b = star_b != std::string::npos ? star_b : key_b.size();

            // A longer base pins down more of the path before any wildcard
            // part could come into play, so it is more specific.
            if (base_length_a > base_length_b) {
                return true;
            }
            if (base_length_b > base_length_a) {
                return false;
            }

            // With equal bases a wildcard key outranks a fixed key so that a
            // literal key can never hide a pattern matching the same prefix.
            if (star_a == std::string::npos) {
                return false;
            }
            if (star_b == std::string::npos) {
                return true;
            }

            // Two wildcard keys sharing a base: the longer one covers a
            // longer, therefore more specific, area.
            if (key_a.size() > key_b.size()) {
                return true;
            }
            if (key_b.size() > key_a.size()) {
                return false;
            }

            return false;
        }

    } // namespace

    // Parses the JSON value of an "imports" or "exports" property into the tree
    // of "PjEntry" objects the resolver walks. The input may chain arbitrarily:
    // a string, an array of fallbacks, an object of conditions, or an object
    // of subpath keys, each of which may in turn nest further. The returned
    // PjMap also records which keys ended in "/" or contained "*" so that
    // expansion matching does not have to rescan them later, and it validates
    // the mixing rules (dot-keys versus condition keys) while reporting "dead"
    // conditional branches as warnings.
    //
    //   Input:  exports {"./feature": {"import": "./esm.js", "require": "./cjs.js"}}
    //   Output: a PjMap whose "." key holds a condition object with "import"
    //           and "require" branches
    std::unique_ptr<PjMap> ParseImportsExportsMap(const logger::Source&    source,
                                                  logger::Log&            log,
                                                  const javascript::Expr& json,
                                                  const std::string&      property_key,
                                                  logger::Loc             property_key_loc)
    {
        logger::LineColumnTracker tracker(&source);

        struct Visitor {
            const logger::Source&     source;
            logger::Log&              log;
            logger::LineColumnTracker tracker;

            // Converts one JSON expression into its PjEntry: null becomes the
            // explicit "disabled" marker, strings capture their text and range,
            // arrays fold their items recursively, objects build both the keyed
            // map and the expansion-key list, and any other value is rejected
            // as an invalid entry with a warning.
            PjEntry Visit(const javascript::Expr& expr)
            {
                logger::Range first_token;

                if (auto* e = std::get_if<std::shared_ptr<javascript::ENull>>(&expr.data)) {
                    (void) e;
                    PjEntry entry;
                    entry.kind        = PjKind::kNull;
                    entry.first_token = javascript::RangeOfIdentifier(source, expr.loc);
                    return entry;
                }

                if (auto* e = std::get_if<std::shared_ptr<javascript::EString>>(&expr.data)) {
                    PjEntry entry;
                    entry.kind        = PjKind::kString;
                    entry.first_token = source.RangeOfString(expr.loc);
                    entry.str_data    = helpers::UTF16ToString((*e)->value);
                    return entry;
                }

                if (auto* e = std::get_if<std::shared_ptr<javascript::EArray>>(&expr.data)) {
                    PjEntry entry;
                    entry.kind        = PjKind::kArray;
                    entry.first_token = logger::Range{expr.loc, 1};
                    entry.arr_data.reserve((*e)->items.size());
                    for (const javascript::Expr& item : (*e)->items) {
                        entry.arr_data.push_back(Visit(item));
                    }
                    return entry;
                }

                if (auto* e = std::get_if<std::shared_ptr<javascript::EObject>>(&expr.data)) {
                    PjEntry entry;
                    entry.kind        = PjKind::kObject;
                    entry.first_token = logger::Range{expr.loc, 1};
                    entry.map_data.resize((*e)->properties.size());
                    entry.expansion_keys.reserve((*e)->properties.size());
                    bool is_conditional_sugar = false;

                    DeadCondition dead_condition;
                    logger::Range found_default;
                    logger::Range found_import;
                    logger::Range found_require;

                    for (size_t i = 0; i < (*e)->properties.size(); ++i) {
                        const javascript::Property& property = (*e)->properties[i];
                        std::string                 key;
                        if (auto* key_str =
                                std::get_if<std::shared_ptr<javascript::EString>>(&property.key.data)) {
                            key = helpers::UTF16ToString((*key_str)->value);
                        }
                        logger::Range key_range = source.RangeOfString(property.key.loc);

                        // An object must not mix the two keying schemes. A key starting
                        // with a "." addresses a subpath; a key without one is a
                        // condition name (conditional sugar). Mixing the two is
                        // malformed, so it is rejected with a warning that names
                        // both the offending key and a previously seen
                        // incompatible one.
                        bool cur_is_conditional_sugar = !key.starts_with(".");
                        if (i == 0) {
                            is_conditional_sugar = cur_is_conditional_sugar;
                        } else if (is_conditional_sugar != cur_is_conditional_sugar) {
                            const PjMapEntry& prev_entry = entry.map_data[i - 1];
                            log.AddIDWithNotes(
                                    logger::MsgID::kPackageJSON_InvalidImportsOrExports,
                                    logger::MsgKind::kWarning,
                                    &tracker,
                                    key_range,
                                    logger::FormatMsg(logger::MsgCat::kPackageJSON_KeysBothStartAndNotWithDot),
                                    {tracker.MakeMsgData(prev_entry.key_range,
                                                     logger::FormatMsg(logger::MsgCat::kPackageJSON_IncompatibleKeyNote,
                                                              helpers::QuoteForJSON(key, false),
                                                              helpers::QuoteForJSON(prev_entry.key, false)))});
                            PjEntry invalid_entry;
                            invalid_entry.kind        = PjKind::kInvalid;
                            invalid_entry.first_token = entry.first_token;
                            return invalid_entry;
                        }

                        // Record branches that can never win the walk, because a
                        // "default" entry (or both "import" and "require") has
                        // already been seen earlier and will always be chosen.
                        if (found_default.len != 0 ||
                            (found_import.len != 0 && found_require.len != 0)) {
                            dead_condition.ranges.push_back(key_range);
                            // The "default" branch itself is a legitimate
                            // catch-all, so only the branches after it are
                            // worth complaining about.
                            if (dead_condition.reason.empty() && key != "default") {
                                if (found_default.len != 0) {
                                    dead_condition.reason = "\"default\"";
                                    dead_condition.notes  = {
                                            tracker.MakeMsgData(found_default,
                                                            "The \"default\" condition comes earlier and will always be chosen:")};
                                } else {
                                    dead_condition.reason = "both \"import\" and \"require\"";
                                    dead_condition.notes  = {
                                            tracker.MakeMsgData(found_import,
                                                            "The \"import\" condition comes earlier and will be used for all \"import\" statements:"),
                                            tracker.MakeMsgData(found_require,
                                                            "The \"require\" condition comes earlier and will be used for all \"require\" calls:")};
                                }
                            }
                        } else {
                            if (key == "default") {
                                found_default = key_range;
                            } else if (key == "import") {
                                found_import = key_range;
                            } else if (key == "require") {
                                found_require = key_range;
                            }
                        }

                        PjMapEntry map_entry;
                        map_entry.key      = std::move(key);
                        map_entry.key_range = key_range;
                        map_entry.value    = Visit(property.value_or_nil);

                        // Keys that end in "/" or contain a "*" are not plain lookups: they
                        // can match a range of subpaths, so they are collected
                        // separately for the expansion pass.
                        if (map_entry.key.ends_with("/") ||
                            map_entry.key.find('*') != std::string::npos) {
                            entry.expansion_keys.push_back(map_entry);
                        }

                        entry.map_data[i] = std::move(map_entry);
                    }

                    // The expansion keys must be walked most-specific-first so
                    // an exact or longer match shadows a looser one.
                    std::stable_sort(entry.expansion_keys.begin(), entry.expansion_keys.end(),
                                     ExpansionKeysLess);

                    // Emit one warning for each dead branch found above. Inside
                    // node_modules the message is demoted to a debug note,
                    // since published packages are not something the user can
                    // edit.
                    if (!dead_condition.reason.empty()) {
                        logger::MsgKind kind = logger::MsgKind::kWarning;
                        if (helpers::IsInsideNodeModules(source.key_path.text)) {
                            kind = logger::MsgKind::kDebug;
                        }
                        std::string conditions;
                        const char* condition_word = "condition";
                        const char* it_comes_word  = "it comes";
                        if (dead_condition.ranges.size() > 1) {
                            condition_word = "conditions";
                            it_comes_word  = "they come";
                        }
                        for (size_t i = 0; i < dead_condition.ranges.size(); ++i) {
                            if (i > 0) {
                                conditions += " and ";
                            }
                            conditions += source.TextForRange(dead_condition.ranges[i]);
                        }
log.AddIDWithNotes(logger::MsgID::kPackageJSON_DeadCondition, kind, &tracker,
                                           dead_condition.ranges[0],
                                           logger::FormatMsg(logger::MsgCat::kPackageJSON_DeadConditionMessage,
                                                    condition_word, conditions, it_comes_word, dead_condition.reason),
                                           dead_condition.notes);
                    }

                    return entry;
                }

                // Any other JSON value - a boolean, a number, or a raw identifier - is not
                // a legal map entry. The first token of the offending value is
                // reported and the entry is marked invalid so later
                // resolution steps reject the whole map cleanly.
                if (std::holds_alternative<std::shared_ptr<javascript::EBoolean>>(expr.data)) {
                    first_token = javascript::RangeOfIdentifier(source, expr.loc);
                } else if (std::holds_alternative<std::shared_ptr<javascript::ENumber>>(expr.data)) {
                    first_token = source.RangeOfNumber(expr.loc);
                } else {
                    first_token.loc = expr.loc;
                }

                log.AddID(logger::MsgID::kPackageJSON_InvalidImportsOrExports,
                          logger::MsgKind::kWarning, &tracker, first_token,
                          logger::FormatMsg(logger::MsgCat::kPackageJSON_ValueMustBeStringOrArrayOrNull));
                PjEntry entry;
                entry.kind        = PjKind::kInvalid;
                entry.first_token = first_token;
                return entry;
            }
        };

        Visitor visitor{source, log, logger::LineColumnTracker(&source)};
        PjEntry root = visitor.Visit(json);

        // A null top-level value explicitly exports nothing, so no map is
        // produced at all; the caller simply sees no exports map.
        if (root.kind == PjKind::kNull) {
            return nullptr;
        }

        auto result       = std::make_unique<PjMap>();
        result->root      = std::make_unique<PjEntry>(std::move(root));
        result->property_key     = property_key;
        result->property_key_loc = property_key_loc;
        return result;
    }

    // Splits a bare package specifier into its package name and the subpath
    // within that package, the first step of ESM-style resolution. The name
    // is everything up to the first "/" - but for scoped packages ("@scope/")
    // that is both segments. Any leading "." or a name containing "\" or "%"
    // disqualifies the specifier as package-like.
    //
    //   Input:  "lodash/zip"          ->  name "lodash", subpath "./zip"
    //   Input:  "@scope/pkg/util"     ->  name "@scope/pkg", subpath "./util"
    //   Input:  "./local/path"        ->  false (not a package)
    bool EsmParsePackageName(const std::string& package_specifier,
                             std::string&       package_name,
                             std::string&       package_subpath)
    {
        if (package_specifier.empty()) {
            return false;
        }

        size_t slash = package_specifier.find('/');
        if (!package_specifier.starts_with("@")) {
            // Unscoped package: the name is the first segment (or the whole
            // specifier when there is no slash at all).
            if (slash == std::string::npos) {
                slash = package_specifier.size();
            }
            package_name = package_specifier.substr(0, slash);
        } else {
            // Scoped package: the name spans two segments and the specifier
            // must actually have a "/" after the scope.
            if (slash == std::string::npos) {
                return false;
            }
            std::string_view rest(package_specifier.data() + slash + 1,
                                  package_specifier.size() - slash - 1);
            size_t slash2 = rest.find('/');
            if (slash2 == std::string::npos) {
                slash2 = rest.size();
            }
            package_name = package_specifier.substr(0, slash + 1 + slash2);
        }

        if (package_name.starts_with(".") || package_name.find_first_of("\\%") != std::string::npos) {
            return false;
        }

        package_subpath = "." + package_specifier.substr(package_name.size());
        return true;
    }

    // Reports the first forbidden path segment in a target or subpath: "." ,
    // "..", or "node_modules" appearing after the leading segment. ESM-style
    // resolution forbids exactly these from maps, so returning a non-empty
    // segment is the signal for an invalid package target or module specifier.
    //
    //   Input:  "./a/../b"   ->  ".."
    //   Input:  "./a/node_modules/b"  ->  "node_modules"
    //   Input:  "./a/b"      ->  ""
    std::string_view FindInvalidSegment(std::string_view path)
    {
        size_t slash = path.find_first_of("/\\");
        if (slash == std::string_view::npos) {
            return {};
        }
        path = path.substr(slash + 1);
        while (!path.empty()) {
            size_t next_slash = path.find_first_of("/\\");
            std::string_view segment = path;
            if (next_slash != std::string_view::npos) {
                segment = path.substr(0, next_slash);
                path    = path.substr(next_slash + 1);
            } else {
                path = {};
            }
            if (segment == "." || segment == ".." || segment == "node_modules") {
                return segment;
            }
        }
        return {};
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Per-query resolution steps for "imports" and "exports"

    namespace {

        // Reports whether a named condition (such as "import", "require", or
        // "browser") is active for the current query. The condition set is
        // built once per resolution type by the resolver, so this is a plain
        // membership test; the boolean handles the rare case of a condition
        // being explicitly disabled.
        inline bool ConditionActive(const ConditionsMap& conditions, const std::string& key)
        {
            auto it = conditions.find(key);
            return it != conditions.end() && it->second;
        }

        // Replaces every occurrence of "from" in "text" with "to", resuming
        // the scan just past each replacement so overlapping matches cannot
        // loop forever. Used to splice the matched subpath into pattern
        // targets in place of "*" and to normalize Windows backslashes to
        // forward slashes.
        inline void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
        {
            if (from.empty()) {
                return;
            }
            size_t pos = 0;
            while ((pos = text.find(from, pos)) != std::string::npos) {
                text.replace(pos, from.size(), to);
                pos += to.size();
            }
        }

    } // namespace

    // Looks up an import path in the nearest enclosing "browser" map (the
    // object-literal form of the "browser" field) and rewrites it accordingly.
    // This only matters for browser-targeted builds, where packages commonly
    // ship browser-specific stand-ins for node-oriented files. A lookup can
    // remap the path to a replacement, or explicitly disable the import by
    // mapping it to nothing. The search tries the exact path, then the path
    // with each configured extension appended, then the path treated as a
    // directory that contains an "index" entry; package paths additionally
    // fall back to their "./"-relative spelling for compatibility with maps
    // written in the browserify style.
    //
    //   Input:  resolve_dir_info for /app/pkg, input_path "./lib/node.js",
    //           browser map {"./lib/node.js": "./lib/browser.js"}
    //   Output: remapped = "./lib/browser.js", ok = true
    BrowserRemapResult ResolverQuery::CheckBrowserMap(DirInfo*         resolve_dir_info,
                                                      const std::string& input_path_orig,
                                                      BrowserPathKind  path_kind)
    {
        // The whole mechanism exists only for browser builds; for any other
        // output platform the call is a harmless no-op.
        if (r->options.OutputPlatform != config::Platform::kBrowser) {
            return {};
        }

        // Without an enclosing "browser" scope there is nothing to remap
        // against, so bail out with an empty (non-matching) result.
        if (!resolve_dir_info->enclosing_browser_scope) {
            if (debug_logs) {
                debug_logs->AddNote("No \"browser\" map found in directory " +
                                    helpers::QuoteForJSON(resolve_dir_info->abs_path, false));
            }
            return {};
        }

        PackageJSON* package_json   = resolve_dir_info->enclosing_browser_scope->package_json;
        auto&        browser_map    = package_json->browser_map;

        BrowserRemapResult result;
        std::string        input_path = input_path_orig;

        // Returns true when "key" is present in the map. A present
        // entry with a value remaps the path; a present entry without a value
        // disables it. On a hit the canonical "input_path" is also updated so
        // the debug trace and later steps talk about the key that matched.
        auto lookup_in_browser_map = [&](const std::string& key) -> bool {
            auto it = browser_map.find(key);
            if (it == browser_map.end()) {
                return false;
            }
            result.ok       = true;
            result.remapped = it->second; // Grab a copy of the optional value
            input_path      = key;
            return true;
        };

        auto check_path = [&](const std::string& path_to_check, bool include_implicit_extensions) -> bool {
            if (debug_logs) {
                debug_logs->AddNote("Checking for " + helpers::QuoteForJSON(path_to_check, false) +
                                    " in the \"browser\" map in " +
                                    helpers::QuoteForJSON(package_json->source.key_path.text, false));
            }

            // First, the exact literal form of the path is tested.
            if (debug_logs) {
                debug_logs->AddNote("  Checking for " + helpers::QuoteForJSON(path_to_check, false));
            }
            if (lookup_in_browser_map(path_to_check)) {
                return true;
            }

            // If that missed, retry with each implicit extension appended in
            // the resolver's configured order, mirroring how node resolution
            // would find the file on disk.
            if (include_implicit_extensions) {
                for (const std::string& ext : r->options.ExtensionOrder) {
                    std::string ext_path = path_to_check + ext;
                    if (debug_logs) {
                        debug_logs->AddNote("  Checking for " + helpers::QuoteForJSON(ext_path, false));
                    }
                    if (lookup_in_browser_map(ext_path)) {
                        return true;
                    }
                }
            }

            // Still nothing: treat the path as a directory and probe for
            // its "index" entry, upgrading bare names to "./index" so the key
            // shape matches how maps are conventionally written.
            std::string index_path = internal::PosixPathJoin({path_to_check, "index"});
            if (IsPackagePath(index_path) && !IsPackagePath(path_to_check)) {
                index_path = "./" + index_path;
            }

            // Test the exact "index" path first, then with implicit
            // extensions, following the same two-pass shape as above.
            if (debug_logs) {
                debug_logs->AddNote("  Checking for " + helpers::QuoteForJSON(index_path, false));
            }
            if (lookup_in_browser_map(index_path)) {
                return true;
            }

            // An "index" key with an explicit extension appended.
            if (include_implicit_extensions) {
                for (const std::string& ext : r->options.ExtensionOrder) {
                    std::string ext_path = index_path + ext;
                    if (debug_logs) {
                        debug_logs->AddNote("  Checking for " + helpers::QuoteForJSON(ext_path, false));
                    }
                    if (lookup_in_browser_map(ext_path)) {
                        return true;
                    }
                }
            }

            return false;
        };

        // Absolute paths are turned into paths relative to the directory that owns
        // the "browser" map, because the map's keys are always written
        // relatively. Normalizing "\\" to "/" keeps the two comparable.
        if (path_kind == BrowserPathKind::kAbsolutePath) {
            auto rel_path = r->fs->Rel(resolve_dir_info->enclosing_browser_scope->abs_path, input_path);
            if (!rel_path) {
                return {};
            }
            input_path = *rel_path;
            ReplaceAll(input_path, "\\", "/");
        }

        if (input_path == ".") {
            // Remapping the package root itself is not supported by any
            // bundler, so it is not supported here either.
            return {};
        }

        // Try the import path first in its package-path form; if that misses
        // and the path does look like a package path, retry it in relative
        // form, which is how browser maps tend to spell their keys.
        if (!check_path(input_path, /*include_implicit_extensions=*/true) && IsPackagePath(input_path)) {
            switch (path_kind) {
            case BrowserPathKind::kAbsolutePath:
                check_path("./" + input_path, /*include_implicit_extensions=*/true);
                break;

            case BrowserPathKind::kPackagePath: {
                // A "browser" map written in the browserify style may use a
                // "./pkg" key to override a package path of "require('pkg')".
                // That is arguably an accident, but it is replicated here for
                // compatibility. Crucially, browserify only honors it within
                // the package that declared the map: a map in some parent
                // package must not remap this package's imports, so the
                // behavior is suppressed whenever a "node_modules" folder
                // separates the resolved file from the map's owner.
                bool is_in_same_package = true;
                for (DirInfo* info = resolve_dir_info; info && info != resolve_dir_info->enclosing_browser_scope;
                     info          = info->parent) {
                    if (info->is_node_modules) {
                        is_in_same_package = false;
                        break;
                    }
                }
                if (is_in_same_package) {
                    std::string relative_path_prefix = "./";

                    // Build the "./"-prefixed key from the importer's location to
                    // the end of the package, keeping any intermediate
                    // subdirectories so nested imports map correctly.
                    auto rel_path =
                            r->fs->Rel(resolve_dir_info->enclosing_browser_scope->abs_path, resolve_dir_info->abs_path);
                    if (rel_path && *rel_path != ".") {
                        ReplaceAll(*rel_path, "\\", "/");
                        relative_path_prefix += *rel_path + "/";
                    }

                    // Browserify lets "require('pkg')" match "./pkg" but not
                    // "./pkg.js", so no implicit extensions are attempted here;
                    // that keeps the compatibility quirk faithful.
                    check_path(relative_path_prefix + input_path, /*include_implicit_extensions=*/false);
                }
                break;
            }
            }
        }

        if (debug_logs) {
            if (result.ok) {
                if (!result.remapped.has_value()) {
                    debug_logs->AddNote("Found " + helpers::QuoteForJSON(input_path, false) +
                                        " marked as disabled");
                } else {
                    debug_logs->AddNote("Found " + helpers::QuoteForJSON(input_path, false) + " mapping to " +
                                        helpers::QuoteForJSON(*result.remapped, false));
                }
            } else {
                debug_logs->AddNote("Failed to find " + helpers::QuoteForJSON(input_path, false));
            }
        }

        return result;
    }

    // Reads and interprets the "package.json" living in "input_path", returning
    // a fully-parsed PackageJSON or nullptr when the file cannot be found,
    // read, or parsed as JSON. Reading goes through the shared file-system
    // cache so repeated lookups of the same package cost a single disk hit.
    // The fields that matter to resolution are pulled out here -- name, type,
    // main/module/browser entry points, the browser remap map, the
    // "sideEffects" claim, and the "imports"/"exports" maps -- while the
    // "PjEntry" trees for the maps are produced by the parser above.
    //
    //   Input:  input_path "/app/pkg"
    //   Output: a PackageJSON whose "main" is "./index.js" and whose exports
    //           map has been parsed into "PjEntry" trees
    PackageJSON* ResolverQuery::ParsePackageJSON(const std::string& input_path)
    {
        std::string package_json_path = r->fs->Join({input_path, "package.json"});
        filesystem::FsResult<std::string> read = r->caches->fs_cache.ReadFile(*r->fs, package_json_path);
        if (debug_logs && !read.original_error.empty()) {
            debug_logs->AddNote("Failed to read file " + helpers::QuoteForJSON(package_json_path, false) + ": " +
                                read.original_error);
        }
        if (!read.Ok()) {
            std::string read_error = read.original_error;
            if (read_error.empty() && read.canonical_error) {
                read_error = std::make_error_code(*read.canonical_error).message();
            }
            logger::Path key_path{.text = package_json_path, .namespace_ = "file"};
            logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, key_path);
            r->log->AddError(nullptr, logger::Range{},
                             logger::FormatMsg(logger::MsgCat::kPackageJSON_CannotReadFile,
                                               pretty_paths.Select(r->options.LogPathStyle), read_error));
            return nullptr;
        }
        if (debug_logs) {
            debug_logs->AddNote("The file " + helpers::QuoteForJSON(package_json_path, false) + " exists");
        }

        logger::Path key_path{.text = package_json_path, .namespace_ = "file"};
        logger::Source json_source{
                .pretty_paths = MakePrettyPaths(*r->fs, key_path),
                .contents     = read.value,
                .key_path     = key_path,
        };
        logger::LineColumnTracker tracker(&json_source);

        auto [json_expr, ok] = r->caches->json_cache.Parse(*r->log, json_source, javascript::JSONOptions{});
        if (!ok) {
            return nullptr;
        }

        auto* package_json = new PackageJSON{};
        package_json->source = json_source;

        // Read the "name" field, which the resolver uses in diagnostics and when
        // assembling package-qualified paths for error messages.
        if (auto name_prop = internal::GetProperty(json_expr, "name")) {
            if (auto name_value = internal::GetString(name_prop->first)) {
                package_json->name = *name_value;
            }
        }

        // Read the "type" field, which decides whether the package's files are
        // treated as ES modules or CommonJS. Only "commonjs" and "module" are
        // valid; anything else is reported so a typo does not silently change
        // how every file in the package is interpreted.
        if (auto type_prop = internal::GetProperty(json_expr, "type")) {
            const javascript::Expr& type_json = type_prop->first;
            if (auto type_value = internal::GetString(type_json)) {
                if (*type_value == "commonjs") {
                    package_json->module_type_data = javascript::ModuleTypeData{
                            .source = &package_json->source,
                            .range  = json_source.RangeOfString(type_json.loc),
                            .type   = javascript::ModuleType::kCommonJS_PackageJSON,
                    };
                } else if (*type_value == "module") {
                    package_json->module_type_data = javascript::ModuleTypeData{
                            .source = &package_json->source,
                            .range  = json_source.RangeOfString(type_json.loc),
                            .type   = javascript::ModuleType::kESM_PackageJSON,
                    };
                } else {
                    std::vector<logger::MsgData> notes;
                    notes.push_back(logger::MsgData{
                            .text = "The \"type\" field must be set to either \"commonjs\" or \"module\"."});
                    logger::MsgKind msg_kind = logger::MsgKind::kWarning;

                    // If the value looks like a path pointing at a type-declaration
                    // file, the author almost certainly meant the "types" field
                    // instead of "type". Customize the message for that case
                    // and drop it to a debug-level note inside published npm
                    // packages, where the mistake is common but not actionable.
                    if (type_value->ends_with(".d.ts")) {
                        notes[0] = tracker.MakeMsgData(json_source.RangeOfString(type_prop->second),
                                                       "TypeScript type declarations use the \"types\" field, not "
                                                       "the \"type\" field:");
                        if (notes[0].location) {
                            notes[0].location->suggestion = "\"types\"";
                        }
                        if (helpers::IsInsideNodeModules(json_source.key_path.text)) {
                            msg_kind = logger::MsgKind::kDebug;
                        }
                    }

                    r->log->AddIDWithNotes(logger::MsgID::kPackageJSON_InvalidType, msg_kind, &tracker,
                                           json_source.RangeOfString(type_json.loc),
                                           logger::FormatMsg(logger::MsgCat::kPackageJSON_InvalidTypeValue,
                                                    helpers::QuoteForJSON(*type_value, false)), notes);
                }
            } else {
                r->log->AddID(logger::MsgID::kPackageJSON_InvalidType, logger::MsgKind::kWarning, &tracker,
                              logger::Range{.loc = type_json.loc},
                              logger::FormatMsg(logger::MsgCat::kPackageJSON_TypeFieldMustBeString));
            }
        }

        // Read the "tsconfig" field, an alternative configuration pointer that
        // lets a package name the config file governing its own compilation.
        if (auto tsconfig_prop = internal::GetProperty(json_expr, "tsconfig")) {
            if (auto tsconfig_value = internal::GetString(tsconfig_prop->first)) {
                package_json->tsconfig = *tsconfig_value;
            }
        }

        // Read the "main" fields. The concrete list of candidate names comes from
        // the resolver options when the user configured them, otherwise it is
        // derived from the target platform; every configured name is read in
        // order, and a second sweep over the fallback names fills any gaps so
        // that a package declaring only "browser" still resolves on Node.
        const std::vector<std::string>& main_fields =
            (r->options.MainFieldsSet || !r->options.MainFields.empty())
                    ? r->options.MainFields
                    : DefaultMainFields(r->options.OutputPlatform);
        for (const std::string& field : main_fields) {
            if (auto main_prop = internal::GetProperty(json_expr, field)) {
                if (auto main = internal::GetString(main_prop->first); main && !main->empty()) {
                    package_json->main_fields[field] = MainField{.rel_path = *main, .key_loc = main_prop->second};
                }
            }
        }
        for (const std::string& field : MainFieldsForFailure()) {
            if (package_json->main_fields.find(field) == package_json->main_fields.end()) {
                if (auto main_prop = internal::GetProperty(json_expr, field)) {
                    if (auto main = internal::GetString(main_prop->first); main && !main->empty()) {
                        package_json->main_fields[field] = MainField{.rel_path = *main, .key_loc = main_prop->second};
                    }
                }
            }
        }

        // Read the "browser" property. A string is just a different entry point,
        // but the object-literal form gives authors independent control over
        // the module system variant and the environment variant of the same
        // code, so one package can serve both targets:
        //
        //   "main": "dist/index.node.cjs.js",
        //   "module": "dist/index.node.esm.js",
        //   "browser": {
        //     "./dist/index.node.cjs.js": "./dist/index.browser.cjs.js",
        //     "./dist/index.node.esm.js": "./dist/index.browser.esm.js"
        //   },
        //
        // This is only parsed when the build actually targets the browser.
        if (auto browser_prop = internal::GetProperty(json_expr, "browser");
            browser_prop && r->options.OutputPlatform == config::Platform::kBrowser) {
            if (auto* browser_obj =
                        std::get_if<std::shared_ptr<javascript::EObject>>(&browser_prop->first.data)) {
                // The value is an object literal, so collect its key/value
                // pairs into the remap map consumed by CheckBrowserMap.
                std::map<std::string, std::optional<std::string>> browser_map;

                // Walk every property: each key is a path (or package name)
                // and its value is the replacement path (a string) or the
                // disable marker (the boolean false).
                for (const javascript::Property& prop : (*browser_obj)->properties) {
                    auto key = internal::GetString(prop.key);
                    if (!key || prop.value_or_nil.data.valueless_by_exception()) {
                        continue;
                    }
                    if (auto str_value = internal::GetString(prop.value_or_nil)) {
                        // A string value is a replacement path for the key.
                        browser_map[*key] = *str_value;
                    } else if (auto bool_value = internal::GetBool(prop.value_or_nil)) {
                        // A boolean false marks the key as disabled: the import
                        // resolves to nothing and is treated as tree-shaken.
                        if (!*bool_value) {
                            browser_map[*key] = std::nullopt;
                        }
                    } else {
                        r->log->AddID(logger::MsgID::kPackageJSON_InvalidBrowser, logger::MsgKind::kWarning,
                                      &tracker, logger::Range{.loc = prop.value_or_nil.loc},
                                      logger::FormatMsg(logger::MsgCat::kPackageJSON_BrowserMappingStringOrBoolean));
                    }
                }

                package_json->browser_map = std::move(browser_map);
            }
        }

        // Read the "sideEffects" claim, which lets the bundler know which modules
        // are safe to omit. Three shapes exist: the boolean false (everything
        // in the package has no side effects), an array of glob patterns (only
        // the listed files and directories have side effects), and anything
        // else, which is reported as invalid.
        if (auto side_effects_prop = internal::GetProperty(json_expr, "sideEffects")) {
            const javascript::Expr& side_effects_json = side_effects_prop->first;
            if (auto* boolean = std::get_if<std::shared_ptr<javascript::EBoolean>>(&side_effects_json.data)) {
                if (!(*boolean)->value) {
                    // The false form declares every file in the package side
                    // effect free, so an empty map with no regexps is
                    // recorded and the "array" flag is cleared.
                    package_json->side_effects_map                                 = std::unordered_set<std::string>{};
                    package_json->side_effects_data                                = std::make_shared<SideEffectsData>();
                    package_json->side_effects_data->is_side_effects_array_in_json = false;
                    package_json->side_effects_data->source = std::make_shared<logger::Source>(package_json->source);
                    package_json->side_effects_data->range =
                            json_source.RangeOfString(side_effects_json.loc);
                }
            } else if (auto* array = std::get_if<std::shared_ptr<javascript::EArray>>(&side_effects_json.data)) {
                // The array form names the files that DO have side effects;
                // every other file in the package is fair game to drop. An
                // entry with no explicit directory is matched at any depth.
                package_json->side_effects_map                                 = std::unordered_set<std::string>{};
                package_json->side_effects_data                                = std::make_shared<SideEffectsData>();
                package_json->side_effects_data->is_side_effects_array_in_json = true;
                package_json->side_effects_data->source = std::make_shared<logger::Source>(package_json->source);
                package_json->side_effects_data->range  = json_source.RangeOfString(side_effects_json.loc);

                for (const javascript::Expr& item_json : (*array)->items) {
                    auto* item = std::get_if<std::shared_ptr<javascript::EString>>(&item_json.data);
                    if (!item || !(*item)) {
                        r->log->AddID(logger::MsgID::kPackageJSON_InvalidSideEffects, logger::MsgKind::kWarning,
                                      &tracker, logger::Range{.loc = item_json.loc},
                                      logger::FormatMsg(logger::MsgCat::kPackageJSON_ExpectedStringInArray));
                        continue;
                    }

                    // A plain entry naming a bare file is normalized to a glob that
                    // matches it at any nesting depth, so "dist/helpers" also
                    // catches "dist/helpers.js" wherever it lives.
                    std::string pattern = helpers::UTF16ToString((*item)->value);
                    if (pattern.find('/') == std::string::npos) {
                        pattern = "**/" + pattern;
                    }
                    // Patterns are matched against absolute, slash-normalized
                    // paths, so the pattern is joined to the package directory
                    // and Windows separators are converted before compiling.
                    std::string abs_pattern = r->fs->Join({input_path, pattern});
                    ReplaceAll(abs_pattern, "\\", "/"); // Avoid problems with Windows-style slashes
                    auto [re, had_wildcard] = GlobstarToEscapedRegexp(abs_pattern);

                    // Patterns containing wildcards cannot be looked up by
                    // exact string, so they are compiled once into a regex and
                    // checked per file; a bogus pattern is simply skipped.
                    if (had_wildcard) {
                        try {
                            package_json->side_effects_regexps.emplace_back(re);
                        } catch (const std::regex_error&) {
                            // Ignore invalid patterns instead of crashing
                        }
                        continue;
                    }

                    // Wildcard-free entries are exact paths: a hash lookup is
                    // far cheaper than running a regex, so they go in the map.
                    package_json->side_effects_map->insert(std::move(abs_pattern));
                }
            } else {
                r->log->AddID(logger::MsgID::kPackageJSON_InvalidSideEffects, logger::MsgKind::kWarning, &tracker,
                              logger::Range{.loc = side_effects_json.loc},
                              logger::FormatMsg(logger::MsgCat::kPackageJSON_SideEffectsBooleanOrArray));
            }
        }

        // Read the "imports" map, the set of "#"-prefixed specifiers that this
        // package makes available to its own files. It must be an object; a
        // top-level value of any other shape is reported.
        if (auto imports_prop = internal::GetProperty(json_expr, "imports")) {
            if (auto imports_map = ParseImportsExportsMap(package_json->source, *r->log, imports_prop->first,
                                                          "imports", imports_prop->second)) {
                if (imports_map->root->kind != PjKind::kObject) {
                    r->log->AddID(logger::MsgID::kPackageJSON_InvalidImportsOrExports, logger::MsgKind::kWarning,
                                  &tracker, imports_map->root->first_token,
                                  logger::FormatMsg(logger::MsgCat::kPackageJSON_ImportsMustBeObject));
                }
                package_json->imports_map = std::move(imports_map);
            }
        }

        // Read the "exports" map, which controls which subpaths of the package are
        // importable from the outside and how they resolve. Unlike "imports",
        // it may legally be a string or array when it only describes the
        // package root, so no extra shape check is needed here.
        if (auto exports_prop = internal::GetProperty(json_expr, "exports")) {
            if (auto exports_map = ParseImportsExportsMap(package_json->source, *r->log, exports_prop->first,
                                                          "exports", exports_prop->second)) {
                package_json->exports_map = std::move(exports_map);
            }
        }

        return package_json;
    }

    // Resolves a "#specifier" import against the "imports" map of the package
    // that owns the importing file. The debug narration surrounding the real
    // work is confined to this thin wrapper: when verbose logging is on, the
    // trace is indented one level deeper while the inner function runs so the
    // map walk reads as a sub-step of the import itself.
    //
    //   Input:  import_path "#ui/card", package with imports {"#ui/*": "./ui/*"}
    //   Output: a result whose primary text is the remapped absolute path
    SideEffectsResult ResolverQuery::LoadPackageImports(const std::string& import_path, DirInfo* dir_info_package_json)
    {
        PackageJSON* package_json = dir_info_package_json->package_json;

        if (debug_logs) {
            debug_logs->AddNote("Looking for " + helpers::QuoteForJSON(import_path, false) +
                                " in \"imports\" map in " +
                                helpers::QuoteForJSON(package_json->source.key_path.text, false));
            DebugIndentGuard guard(debug_logs);
            return LoadPackageImportsInner(import_path, dir_info_package_json);
        }
        return LoadPackageImportsInner(import_path, dir_info_package_json);
    }

    // The body of "LoadPackageImports" without its debug-log indentation
    // wrapper, kept separate so the wrapper's indent scope is trivial.
    SideEffectsResult ResolverQuery::LoadPackageImportsInner(const std::string& import_path,
                                                             DirInfo*           dir_info_package_json)
    {
        PackageJSON* package_json = dir_info_package_json->package_json;

        // A lone "#" can never name anything, but rejecting it here rather
        // than inside the resolution core means the error message can point at
        // the "imports" map itself, which is far more helpful.
        if (import_path == "#") {
            if (debug_logs) {
                debug_logs->AddNote("The path " + helpers::QuoteForJSON(import_path, false) + " must not equal \"#\".");
            }
            logger::LineColumnTracker tracker(&package_json->source);
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    package_json->imports_map->root->first_token,
                    logger::FormatMsg(logger::MsgCat::kPackageJSON_ImportsMapIgnoredInvalidSpecifier,
                                      helpers::QuoteForJSON(import_path, false))));
            return {};
        }

        // The condition set that governs the map walk depends on how the specifier
        // was imported: import statements and dynamic imports use the "import"
        // condition set, require calls the "require" one.
        const ConditionsMap* conditions = &r->esm_conditions_default;
        switch (kind) {
        case compiler::ImportKind::kStmt:
        case compiler::ImportKind::kDynamic:
            conditions = &r->esm_conditions_import;
            break;
        case compiler::ImportKind::kRequire:
        case compiler::ImportKind::kRequireResolve:
            conditions = &r->esm_conditions_require;
            break;
        default:
            break;
        }

        EsmStep step = EsmPackageImportsResolve(import_path, *package_json->imports_map->root, *conditions);
        step         = EsmHandlePostConditions(std::move(step.resolved_path), step.status, std::move(step.debug));

        if (step.status == PjStatus::kPackageResolve) {
            if (auto builtin = CheckForBuiltInNodeModules(step.resolved_path)) {
                SideEffectsResult result;
                result.pair          = builtin->path_pair;
                result.ok            = true;
                result.side_effects  = builtin->primary_side_effects_data;
                return result;
            }

            // The import path was remapped via "imports" to another specifier
            // that must itself be resolved (possibly to a node built-in).
            // The re-entry disables the "imports" map so this cannot
            // remap forever in a loop.
            SideEffectsResult result = LoadNodeModules(step.resolved_path, dir_info_package_json,
                                                       /*forbid_imports=*/true);
            if (!result.ok) {
                logger::LineColumnTracker tracker(&package_json->source);
                std::vector<logger::MsgData> notes;
                notes.push_back(tracker.MakeMsgData(
                        step.debug.token,
                        logger::FormatMsg(logger::MsgCat::kPackageJSON_RemappedPathCouldNotBeResolved,
                                helpers::QuoteForJSON(step.resolved_path, false))));
                notes.insert(notes.end(), debug_meta->notes.begin(), debug_meta->notes.end());
                debug_meta->notes = std::move(notes);
            }
            return result;
        }

        LoadResult result = FinalizeImportsExportsResult(FinalizeImportsExportsKind::kNormal,
                                                         dir_info_package_json->abs_path, *conditions,
                                                         *package_json->imports_map, package_json,
                                                         step.resolved_path, step.status, step.debug,
                                                         /*esm_package_name=*/"", /*esm_package_subpath=*/"",
                                                         /*abs_import_path=*/"");
        SideEffectsResult side_effects_result;
        side_effects_result.pair     = std::move(result.pair);
        side_effects_result.ok       = result.ok;
        side_effects_result.diff_case = std::move(result.diff_case);
        return side_effects_result;
    }

    // Resolves a subpath of a package through its "exports" map. This is the
    // ESM-style resolution path the resolver takes for bare specifiers that
    // reach a package with an "exports" field: the map decides both whether
    // the subpath is allowed and where its entry point lives on disk.
    //
    //   Input:  esm_package_subpath "./feature", package with
    //           exports {"./feature": "./dist/feature.js"}
    //   Output: a LoadResult whose primary text is the absolute path of
    //           "<pkg>/dist/feature.js"
    LoadResult ResolverQuery::EsmResolveAlgorithm(FinalizeImportsExportsKind finalize_kind,
                                                  const std::string&         esm_package_name,
                                                  const std::string&         esm_package_subpath,
                                                  PackageJSON*               package_json,
                                                  const std::string&         abs_pkg_path,
                                                  const std::string&         abs_path)
    {
        if (debug_logs) {
            debug_logs->AddNote("Looking for " + helpers::QuoteForJSON(esm_package_subpath, false) +
                                " in \"exports\" map in " +
                                helpers::QuoteForJSON(package_json->source.key_path.text, false));
            DebugIndentGuard guard(debug_logs);
            return EsmResolveAlgorithmInner(finalize_kind, esm_package_name, esm_package_subpath, package_json,
                                            abs_pkg_path, abs_path);
        }
        return EsmResolveAlgorithmInner(finalize_kind, esm_package_name, esm_package_subpath, package_json,
                                        abs_pkg_path, abs_path);
    }

    // The body of "EsmResolveAlgorithm" without the debug-log indentation
    // wrapper, kept separate so the wrapper's indent scope is trivial.
    LoadResult ResolverQuery::EsmResolveAlgorithmInner(FinalizeImportsExportsKind finalize_kind,
                                                       const std::string&         esm_package_name,
                                                       const std::string&         esm_package_subpath,
                                                       PackageJSON*               package_json,
                                                       const std::string&         abs_pkg_path,
                                                       const std::string&         abs_path)
    {
        // The same condition selection as imports: import statements and
        // dynamic imports activate the "import" condition, require calls the
        // "require" condition, and entry points count as imports.
        const ConditionsMap* conditions = &r->esm_conditions_default;
        switch (kind) {
        case compiler::ImportKind::kStmt:
        case compiler::ImportKind::kDynamic:
            conditions = &r->esm_conditions_import;
            break;
        case compiler::ImportKind::kRequire:
        case compiler::ImportKind::kRequireResolve:
            conditions = &r->esm_conditions_require;
            break;
        case compiler::ImportKind::kEntryPoint:
            // Treat entry points as imports instead of requires for
            // consistency with the other bundlers: an entry point reaching a
            // package is almost always the package's main entry made by an
            // import-style statement, not a "require()" call.
            conditions = &r->esm_conditions_import;
            break;
        default:
            break;
        }

        // Resolve the subpath against "/" first and only then join it onto the
        // package's absolute directory. Exports resolution thinks in URL-style
        // paths, but the resolver deals in real file paths; keeping the two
        // separate avoids problems with Windows drive letters and prevents any
        // "%" in the absolute directory from being read as a URL escape.
        EsmStep step = EsmPackageExportsResolve("/", esm_package_subpath, *package_json->exports_map->root,
                                                *conditions);
        step         = EsmHandlePostConditions(std::move(step.resolved_path), step.status, std::move(step.debug));

        return FinalizeImportsExportsResult(finalize_kind, abs_pkg_path, *conditions, *package_json->exports_map,
                                            package_json, step.resolved_path, step.status, step.debug,
                                            esm_package_name, esm_package_subpath, abs_path);
    }

    // Turns the outcome of an "imports"/"exports" map walk into a final,
    // filesystem-verified LoadResult. When the walk produced an exact (or
    // pattern-matched) path, the file is looked up on disk, extension rewrites
    // are attempted, and side cases like directories are turned into friendly
    // status codes; when it failed, the failure is converted into log notes
    // and, where possible, a suggestion for the correct import path. Every
    // diagnostic is anchored to the package's own source so messages point at
    // the offending map entry.
    //
    //   Input:  resolved_path "/dist/feature.js", status kExact
    //   Output: result.ok = true, primary.text = "<pkg>/dist/feature.js"
    //   Input:  resolved_path "./feature", status kPackagePathNotExported
    //   Output: result.ok = false plus a "not exported" note and suggestion
    LoadResult ResolverQuery::FinalizeImportsExportsResult(FinalizeImportsExportsKind finalize_kind,
                                                           const std::string&         abs_dir_path,
                                                           const ConditionsMap&       conditions,
                                                           PjMap&                     import_export_map,
                                                           PackageJSON*               package_json,
                                                           const std::string&         resolved_path_orig,
                                                           PjStatus                   status,
                                                           const PjDebug&             debug,
                                                           const std::string&         esm_package_name,
                                                           const std::string&         esm_package_subpath,
                                                           const std::string&         abs_import_path)
    {
        std::string resolved_path = resolved_path_orig;
        std::string missing_suffix;

        // Small factory for the happy path: a "file"-namespaced load result
        // carrying the resolved text and, optionally, a directory-entry
        // case-mismatch marker.
        auto make_ok_result = [](std::string text, std::optional<filesystem::DifferentCase> diff_case) {
            LoadResult result;
            result.pair.primary.text   = std::move(text);
            result.pair.primary.namespace_ = "file";
            result.ok                  = true;
            result.diff_case           = std::move(diff_case);
            return result;
        };

        // A slash-prefixed path is the walk's promise that the entry exists on
        // disk within the package; it still has to be verified here.
        if ((status == PjStatus::kExact || status == PjStatus::kExactEndsWithStar || status == PjStatus::kInexact) &&
            !resolved_path.empty() && resolved_path.front() == '/') {
            std::string abs_resolved_path = r->fs->Join({abs_dir_path, resolved_path});

            switch (status) {
            case PjStatus::kExact:
            case PjStatus::kExactEndsWithStar: {
                if (debug_logs) {
                    debug_logs->AddNote("The resolved path " + helpers::QuoteForJSON(abs_resolved_path, false) +
                                        " is exact");
                }

                // When the "exports" map was consulted only to support
                // tsconfig "extends" traversal under Yarn PnP, skip the
                // recursive directory probe entirely.
                if (finalize_kind == FinalizeImportsExportsKind::kYarnPnPTSConfigExtends) {
                    if (debug_logs) {
                        debug_logs->AddNote("Resolved to " + helpers::QuoteForJSON(abs_resolved_path, false));
                    }
                    return make_ok_result(abs_resolved_path, std::nullopt);
                }

                DirInfo* resolved_dir_info = DirInfoCached(r->fs->Dir(abs_resolved_path));
                std::string base           = r->fs->Base(abs_resolved_path);
                const std::vector<std::string>* extension_order = &r->options.ExtensionOrder;
                if (compiler::ImportKindMustResolveToCSS(kind)) {
                    extension_order = &r->css_extension_order;
                }

                if (!resolved_dir_info) {
                    status = PjStatus::kModuleNotFound;
                } else {
                    auto [entry, diff_case] = resolved_dir_info->entries.Get(base);

                    // The map may point at a ".js" file while the package
                    // ships the same module as source; when the literal entry
                    // is missing, try the known source extensions in place of
                    // the requested one before reporting the miss.
                    if (!entry) {
                        for (const auto& [old_ext, new_exts] : RewrittenFileExtensions()) {
                            if (!base.ends_with(old_ext)) {
                                continue;
                            }
                            size_t last_dot = base.find_last_of('.');
                            for (const std::string& ext : new_exts) {
                                std::string base_with_ext = base.substr(0, last_dot) + ext;
                                std::tie(entry, diff_case) = resolved_dir_info->entries.Get(base_with_ext);
                                if (entry) {
                                    abs_resolved_path = r->fs->Join({resolved_dir_info->abs_path, base_with_ext});
                                    break;
                                }
                            }
                            break;
                        }
                    }

                    if (!entry) {
                        bool ends_with_star = status == PjStatus::kExactEndsWithStar;
                        status              = PjStatus::kModuleNotFound;

                        // The exact file was missing, so produce a helpful status when the
                        // pattern match suggests the author merely forgot the
                        // extension: point at the candidate that does exist.
                        if (ends_with_star) {
                            for (const std::string& ext : *extension_order) {
                                auto [missing_entry, unused] = resolved_dir_info->entries.Get(base + ext);
                                (void)unused;
                                if (missing_entry) {
                                    if (debug_logs) {
                                        debug_logs->AddNote(
                                                "The import " +
                                                helpers::QuoteForJSON(internal::PosixPathJoin(
                                                                              {esm_package_name, esm_package_subpath}),
                                                                      false) +
                                                " is missing the extension " + helpers::QuoteForJSON(ext, false));
                                    }
                                    status         = PjStatus::kModuleNotFoundMissingExtension;
                                    missing_suffix = ext;
                                    break;
                                }
                            }
                        }
                    } else if (entry->Kind(*r->fs) == filesystem::EntryKind::kDir) {
                        if (debug_logs) {
                            debug_logs->AddNote("The path " + helpers::QuoteForJSON(abs_resolved_path, false) +
                                                " is a directory, which is not allowed");
                        }
                        bool ends_with_star = status == PjStatus::kExactEndsWithStar;
                        status              = PjStatus::kUnsupportedDirectoryImport;

                        // The entry is a directory, which imports cannot load directly; when
                        // the pattern allows it, look for an "index" file so the
                        // missing suffix can be suggested.
                        if (ends_with_star) {
                            if (DirInfo* index_dir_info = DirInfoCached(abs_resolved_path)) {
                                for (const std::string& ext : *extension_order) {
                                    std::string index_base      = "index" + ext;
                                    auto [index_entry, unused2] = index_dir_info->entries.Get(index_base);
                                    (void)unused2;
                                    if (index_entry && index_entry->Kind(*r->fs) == filesystem::EntryKind::kFile) {
                                        status         = PjStatus::kUnsupportedDirectoryImportMissingIndex;
                                        missing_suffix = "/" + index_base;
                                        if (debug_logs) {
                                            debug_logs->AddNote(
                                                    "The import " +
                                                    helpers::QuoteForJSON(internal::PosixPathJoin(
                                                                                  {esm_package_name, esm_package_subpath}),
                                                                          false) +
                                                    " is missing the suffix " +
                                                    helpers::QuoteForJSON(missing_suffix, false));
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    } else if (entry->Kind(*r->fs) != filesystem::EntryKind::kFile) {
                        status = PjStatus::kModuleNotFound;
                    } else {
                        if (debug_logs) {
                            debug_logs->AddNote("Resolved to " + helpers::QuoteForJSON(abs_resolved_path, false));
                        }
                        return make_ok_result(abs_resolved_path, std::move(diff_case));
                    }
                }
                break;
            }

            case PjStatus::kInexact: {
                // An expansion key ending in "/" rather than "*" leaves the final
                // suffix open-ended, so the resolved prefix must be probed
                // like a normal import: as a file first, then as a directory
                // with its own "main" or index entries.
                if (debug_logs) {
                    debug_logs->AddNote("The resolved path " + helpers::QuoteForJSON(abs_resolved_path, false) +
                                        " is inexact");
                }
                LoadResult result = LoadAsFileOrDirectory(abs_resolved_path);
                if (result.ok) {
                    return result;
                }
                status = PjStatus::kModuleNotFound;
                break;
            }

            default:
                break;
            }
        }

        if (!resolved_path.empty() && resolved_path.front() == '/') {
            resolved_path = "." + resolved_path;
        }

        // The resolution failed, so the rest of the function converts each
        // status into one or more log notes anchored at the package's source.
        // Where an alternative import path can be determined, a suggestion is
        // attached to the note so the user sees the fix alongside the error.
        logger::LineColumnTracker tracker(&package_json->source);
        switch (status) {
        case PjStatus::kInvalidModuleSpecifier:
            debug_meta->notes.clear();
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    debug.token, logger::FormatMsg(logger::MsgCat::kPackageJSON_ModuleSpecifierInvalid,
                                  helpers::QuoteForJSON(resolved_path, false), debug.invalid_because)));
            break;

        case PjStatus::kInvalidPackageConfiguration:
            debug_meta->notes.clear();
            debug_meta->notes.push_back(
                    tracker.MakeMsgData(debug.token, "The package configuration has an invalid value here:"));
            break;

        case PjStatus::kInvalidPackageTarget: {
            debug_meta->notes.clear();
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    debug.token,
                    resolved_path.empty()
                            ? std::string("The package configuration has an invalid value here:")
                            : logger::FormatMsg(logger::MsgCat::kPackageJSON_PackageTargetInvalid,
                                                helpers::QuoteForJSON(resolved_path, false),
                                                debug.invalid_because)));
            break;
        }

        case PjStatus::kPackagePathNotExported: {
            if (debug.is_because_of_null_literal) {
                debug_meta->notes.clear();
                debug_meta->notes.push_back(tracker.MakeMsgData(
                        debug.token,
                        logger::FormatMsg(logger::MsgCat::kPackageJSON_PathDisabledByPackageAuthor,
                                        helpers::QuoteForJSON(esm_package_subpath, false),
                                        helpers::QuoteForJSON(esm_package_name, false))));
                break;
            }

            debug_meta->notes.clear();
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    debug.token, logger::FormatMsg(logger::MsgCat::kPackageJSON_PathNotExportedByPackage,
                                     helpers::QuoteForJSON(esm_package_subpath, false),
                                     helpers::QuoteForJSON(esm_package_name, false))));

            // The old-style resolution may still find the file even though the
            // "exports" map turned it away. If so, a reverse lookup through
            // the map reveals which subpath WOULD export that file, which is
            // exactly the correction the user needs.
            LoadResult old_result = LoadAsFileOrDirectory(abs_import_path);
            if (old_result.ok && old_result.pair.primary.namespace_ == "file") {
                auto rel_path = r->fs->Rel(abs_dir_path, old_result.pair.primary.text);
                if (rel_path) {
                    std::string rel_path_slashes = *rel_path;
                    ReplaceAll(rel_path_slashes, "\\", "/");
                    std::string query = "." + internal::PosixPathJoin({"/", rel_path_slashes});

                    // A successful reverse lookup means the file IS exported,
                    // just under a different subpath.
                    ReverseResolveResult reverse =
                            EsmPackageExportsReverseResolve(query, *import_export_map.root, conditions);
                    if (reverse.ok) {
                        debug_meta->notes.push_back(tracker.MakeMsgData(
                                reverse.token, logger::FormatMsg(logger::MsgCat::kPackageJSON_FileExportedAtPath,
                                                        helpers::QuoteForJSON(query, false),
                                                        helpers::QuoteForJSON(reverse.subpath, false))));

                        // Attach the corrected import path as an inline
                        // suggestion next to the failing specifier.
                        logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, old_result.pair.primary);
                        std::string actual_import_path =
                                internal::PosixPathJoin({esm_package_name, reverse.subpath});
                        debug_meta->suggestion_text    = helpers::QuoteForJSON(actual_import_path, false);
                        debug_meta->suggestion_message = logger::FormatMsg(
                        logger::MsgCat::kPackageJSON_ImportFromToGetFile,
                        helpers::QuoteForJSON(actual_import_path, false),
                        helpers::QuoteForJSON(pretty_paths.Select(r->options.LogPathStyle),
                                              false));
                    }
                }
            }
            break;
        }

        case PjStatus::kPackageImportNotDefined:
            debug_meta->notes.clear();
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    debug.token, logger::FormatMsg(logger::MsgCat::kPackageJSON_PackageImportNotDefined,
                                     helpers::QuoteForJSON(resolved_path, false))));
            break;

        case PjStatus::kModuleNotFound:
        case PjStatus::kModuleNotFoundMissingExtension:
            debug_meta->notes.clear();
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    debug.token, logger::FormatMsg(logger::MsgCat::kPackageJSON_ModuleNotFound,
                                     helpers::QuoteForJSON(resolved_path, false))));

            // A missing extension is the one case with a precise fix: append the
            // suffix that exists on disk and suggest it inline.
            if (status == PjStatus::kModuleNotFoundMissingExtension) {
                logger::Path suggestion_file{.text = r->fs->Join({abs_dir_path, resolved_path + missing_suffix}),
                                             .namespace_ = "file"};
                logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, suggestion_file);
                std::string actual_import_path =
                        internal::PosixPathJoin({esm_package_name, esm_package_subpath + missing_suffix});
                debug_meta->suggestion_range    = SuggestionRange::kEnd;
                debug_meta->suggestion_text     = missing_suffix;
                debug_meta->suggestion_message  = logger::FormatMsg(
                                                 logger::MsgCat::kPackageJSON_ImportFromToGetFile,
                                                 helpers::QuoteForJSON(actual_import_path, false),
                                                 helpers::QuoteForJSON(pretty_paths.Select(r->options.LogPathStyle),
                                                                       false));
            }
            break;

        case PjStatus::kUnsupportedDirectoryImport:
        case PjStatus::kUnsupportedDirectoryImportMissingIndex: {
            debug_meta->notes.clear();
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    debug.token, logger::FormatMsg(logger::MsgCat::kPackageJSON_ImportingDirectoryForbidden,
                                     helpers::QuoteForJSON(resolved_path, false))));
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    package_json->source.RangeOfString(import_export_map.property_key_loc),
                    logger::FormatMsg(logger::MsgCat::kPackageJSON_PropertyKeyMakesDirectoryForbidden,
                            helpers::QuoteForJSON(import_export_map.property_key, false))));

            // When the directory import failed only because an "index" file
            // exists, suggest appending it inline.
            if (status == PjStatus::kUnsupportedDirectoryImportMissingIndex) {
                logger::Path suggestion_file{.text = r->fs->Join({abs_dir_path, resolved_path + missing_suffix}),
                                             .namespace_ = "file"};
                logger::PrettyPaths pretty_paths = MakePrettyPaths(*r->fs, suggestion_file);
                std::string actual_import_path =
                        internal::PosixPathJoin({esm_package_name, esm_package_subpath + missing_suffix});
                debug_meta->suggestion_range    = SuggestionRange::kEnd;
                debug_meta->suggestion_text     = missing_suffix;
                debug_meta->suggestion_message  = logger::FormatMsg(
                                                 logger::MsgCat::kPackageJSON_ImportFromToGetFile,
                                                 helpers::QuoteForJSON(actual_import_path, false),
                                                 helpers::QuoteForJSON(pretty_paths.Select(r->options.LogPathStyle),
                                                                       false));
            }
            break;
        }

        // The map only offers condition-gated entries and none of them is
        // active. The first note says the subpath is not currently exported;
        // the second lists the conditions that tried and failed. When one of
        // the inactive conditions would flip simply by changing the import
        // style (require vs. import), that remedy is suggested.
        case PjStatus::kUndefinedNoConditionsMatch: {
            std::vector<std::string> keys;
            keys.reserve(conditions.size());
            for (const auto& [key, active] : conditions) {
                (void)active;
                keys.push_back(key);
            }
            std::sort(keys.begin(), keys.end());

            std::vector<std::string> unmatched_conditions;
            unmatched_conditions.reserve(debug.unmatched_conditions.size());
            for (const DebugSpan& key : debug.unmatched_conditions) {
                unmatched_conditions.push_back(key.text);
            }

            debug_meta->notes.clear();
            debug_meta->notes.push_back(tracker.MakeMsgData(
                    import_export_map.root->first_token,
                    logger::FormatMsg(logger::MsgCat::kPackageJSON_PathNotCurrentlyExportedByPackage,
                            helpers::QuoteForJSON(esm_package_subpath, false),
                            helpers::QuoteForJSON(esm_package_name, false))));

            debug_meta->notes.push_back(tracker.MakeMsgData(
                    debug.token,
                    logger::FormatMsg(logger::MsgCat::kPackageJSON_NoConditionsMatch,
                            helpers::StringArrayToQuotedCommaSeparatedString(unmatched_conditions),
                            helpers::StringArrayToQuotedCommaSeparatedString(keys))));

            bool did_suggest_enabling_condition = false;
            for (const DebugSpan& key : debug.unmatched_conditions) {
                if (key.text == "import") {
                    if (kind == compiler::ImportKind::kRequire || kind == compiler::ImportKind::kRequireResolve) {
                        debug_meta->suggestion_message =
                                "Consider using an \"import\" statement to import this file, "
                                "which will work because the \"import\" condition is supported by this package:";
                    }
                } else if (key.text == "require") {
                    if (kind == compiler::ImportKind::kStmt || kind == compiler::ImportKind::kDynamic) {
                        debug_meta->suggestion_message =
                                "Consider using a \"require()\" call to import this file, "
                                "which will work because the \"require\" condition is supported by this package:";
                    }
                } else {
                    // The "types" condition is deliberately not suggested: it exists for
                    // type declarations only, which a bundle never executes.
                    if (!did_suggest_enabling_condition && key.text != "types") {
                        // Guchho exposes build and transform entry points but
                        // no API-level notion of a condition list, so the
                        // suggestion names the condition in the guchho config
                        // phrasing.
                        std::string how = "'Conditions: []string{" + helpers::QuoteForJSON(key.text, false) + "}'";
                        debug_meta->notes.push_back(tracker.MakeMsgData(
                                key.range,
                                logger::FormatMsg(logger::MsgCat::kPackageJSON_ConsiderEnablingCondition,
                                helpers::QuoteForJSON(key.text, false), how)));
                        did_suggest_enabling_condition = true;
                    }
                }
            }
            break;
        }

        default:
            break;
        }

        return {};
    }

    // Runs the validations that follow a successful (exact) match of an
    // "imports"/"exports" map, turning a matched URL-style path into a
    // filesystem-safe one. Percent-encoded separators are rejected outright
    // -- "%2F" or "%5C" in a resolved path means the map tried to smuggle a
    // "/" or "\" past an earlier validation -- and a trailing slash, which
    // would point at a directory, is rejected as an unsupported import.
    //
    //   Input:  resolved "./dist/util.js", status kExact
    //   Output: resolved_path "./dist/util.js", status kExact
    //   Input:  resolved "./dist%2Fevil.js", status kExact
    //   Output: status kInvalidModuleSpecifier
    EsmStep ResolverQuery::EsmHandlePostConditions(std::string resolved, PjStatus status, PjDebug debug)
    {
        if (status != PjStatus::kExact && status != PjStatus::kExactEndsWithStar && status != PjStatus::kInexact) {
            return {std::move(resolved), status, std::move(debug)};
        }

        // Unescape any percent-encoding first, so both the escape checks below
        // and the returned path see the decoded form. A malformed escape
        // sequence is itself an invalid module specifier.
        std::string resolved_path;
        std::string unescape_error;
        if (!internal::UnescapePath(resolved, resolved_path, unescape_error)) {
            if (debug_logs) {
                debug_logs->AddNote("The path " + helpers::QuoteForJSON(resolved, false) +
                                    " contains invalid URL escapes: " + unescape_error);
            }
            return {std::move(resolved), PjStatus::kInvalidModuleSpecifier, std::move(debug)};
        }
        std::string_view found;
        if (resolved.find("%2f") != std::string::npos) {
            found = "%2f";
        } else if (resolved.find("%2F") != std::string::npos) {
            found = "%2F";
        } else if (resolved.find("%5c") != std::string::npos) {
            found = "%5c";
        } else if (resolved.find("%5C") != std::string::npos) {
            found = "%5C";
        }
        if (!found.empty()) {
            if (debug_logs) {
                debug_logs->AddNote("The path " + helpers::QuoteForJSON(resolved, false) +
                                    " is not allowed to contain " + helpers::QuoteForJSON(found, false));
            }
            return {std::move(resolved), PjStatus::kInvalidModuleSpecifier, std::move(debug)};
        }

        // A path ending in a separator is necessarily a directory, which an
        // import cannot address.
        if (resolved_path.ends_with('/') || resolved_path.ends_with('\\')) {
            if (debug_logs) {
                debug_logs->AddNote("The path " + helpers::QuoteForJSON(resolved, false) +
                                    " is not allowed to end with a slash");
            }
            return {std::move(resolved), PjStatus::kUnsupportedDirectoryImport, std::move(debug)};
        }

        // All checks pass: hand back the decoded path for the caller to
        // verify against the filesystem.
        return {std::move(resolved_path), status, std::move(debug)};
    }

    // Resolves a "#"-prefixed specifier through the package's "imports" map.
    // The map is required to be an object; any other shape is a configuration
    // error. When the walk cannot match the specifier at all, the status
    // becomes "not defined" with the map's first token for error anchoring.
    EsmStep ResolverQuery::EsmPackageImportsResolve(const std::string&   specifier,
                                                    const PjEntry&       imports,
                                                    const ConditionsMap& conditions)
    {
        // Guchho rejects a non-object "imports" with a clear message rather
        // than treating it as an empty map, which would silently swallow
        // every "#" import.
        if (imports.kind != PjKind::kObject) {
            return {"", PjStatus::kInvalidPackageConfiguration,
                    PjDebug{.invalid_because = "", .token = imports.first_token}};
        }

        EsmStep step = EsmPackageImportsExportsResolve(specifier, imports, "/", /*is_imports=*/true, conditions);
        if (step.status != PjStatus::kNull && step.status != PjStatus::kUndefined) {
            return step;
        }

        if (debug_logs) {
            debug_logs->AddNote("The package import " + helpers::QuoteForJSON(specifier, false) +
                                " is not defined");
        }
        return {specifier, PjStatus::kPackageImportNotDefined,
                PjDebug{.invalid_because = "", .token = imports.first_token}};
    }

    // Resolves a subpath through the package's "exports" map. A malformed map
    // short-circuits to an invalid-configuration error; otherwise the "." key
    // (the package's own entry) and the subpath keys are handled separately,
    // and any miss becomes "not exported".
    EsmStep ResolverQuery::EsmPackageExportsResolve(const std::string&   package_url,
                                                    const std::string&   subpath,
                                                    const PjEntry&       exports,
                                                    const ConditionsMap& conditions)
    {
        if (exports.kind == PjKind::kInvalid) {
            if (debug_logs) {
                debug_logs->AddNote("Invalid package configuration");
            }
            return {"", PjStatus::kInvalidPackageConfiguration,
                    PjDebug{.invalid_because = "", .token = exports.first_token}};
        }

        PjDebug debug_to_return{.invalid_because = "", .token = exports.first_token};
        if (subpath == ".") {
            // Resolving the package root: a plain string/array is used
            // directly as the entry target, an object is examined for its "."
            // key, and a missing "main_export" (still kNull) falls through to
            // the not-exported answer.
            // "main_export" starts in the kNull state and is only reassigned
            // when a root entry is found; leaving it kNull selects the
            // not-exported outcome below.
            PjEntry main_export;
            if (exports.kind == PjKind::kString || exports.kind == PjKind::kArray ||
                (exports.kind == PjKind::kObject && !PjEntryKeysStartWithDot(exports))) {
                main_export = exports;
            } else if (exports.kind == PjKind::kObject) {
                if (const PjEntry* dot = PjEntryValueForKey(exports, ".")) {
                    if (debug_logs) {
                        debug_logs->AddNote("Using the entry for \".\"");
                    }
                    main_export = *dot;
                }
            }
            if (main_export.kind != PjKind::kNull) {
                EsmStep step =
                        EsmPackageTargetResolve(package_url, main_export, "", /*pattern=*/false, /*internal=*/false,
                                                conditions);
                if (step.status != PjStatus::kNull && step.status != PjStatus::kUndefined) {
                    return step;
                }
                debug_to_return = std::move(step.debug);
            }
        } else if (exports.kind == PjKind::kObject && PjEntryKeysStartWithDot(exports)) {
            EsmStep step = EsmPackageImportsExportsResolve(subpath, exports, package_url, /*is_imports=*/false,
                                                           conditions);
            if (step.status != PjStatus::kNull && step.status != PjStatus::kUndefined) {
                return step;
            }
            debug_to_return = std::move(step.debug);
        }

        if (debug_logs) {
            debug_logs->AddNote("The path " + helpers::QuoteForJSON(subpath, false) + " is not exported");
        }
        return {"", PjStatus::kPackagePathNotExported, std::move(debug_to_return)};
    }

    // Walks a subpath-keyed map (the second form of "imports"/"exports") looking
    // for the best match for "match_key". Two kinds of keys participate: plain
    // keys, which only win by exact equality, and expansion keys (ending in
    // "/" or containing a "*"), which match prefixes and splice the leftover
    // text into the target. Expansion keys were sorted most-specific-first by
    // the parser, so the first hit here is also the correct one.
    //
    //   Input:  match_obj with exact key "./feature" -> "./dist/feature.js",
    //           match_key "./feature"
    //   Output: EsmStep with result = ".../dist/feature.js", status kExact
    EsmStep ResolverQuery::EsmPackageImportsExportsResolve(const std::string&   match_key,
                                                           const PjEntry&       match_obj,
                                                           const std::string&   package_url,
                                                           bool                 is_imports,
                                                           const ConditionsMap& conditions)
    {
        if (debug_logs) {
            debug_logs->AddNote("Checking object path map for " + helpers::QuoteForJSON(match_key, false));
        }

        // An exact key match (no "/" suffix, no "*") resolves the target
        // directly with an empty leftover subpath.
        if (!match_key.ends_with('/') && match_key.find('*') == std::string::npos) {
            if (const PjEntry* target = PjEntryValueForKey(match_obj, match_key)) {
                if (debug_logs) {
                    debug_logs->AddNote("Found exact match for " + helpers::QuoteForJSON(match_key, false));
                }
                return EsmPackageTargetResolve(package_url, *target, "", /*pattern=*/false, /*internal=*/is_imports,
                                               conditions);
            }
        }

        for (const PjMapEntry& expansion : match_obj.expansion_keys) {
            // A "*"-bearing key is a real pattern: split it at the star into
            // the fixed prefix and suffix.
            size_t star = expansion.key.find('*');
            if (star != std::string::npos) {
                std::string pattern_base = expansion.key.substr(0, star);

                // The candidate must start with the prefix (but be longer
                // than the prefix alone, so something is actually captured).
                if (match_key.starts_with(pattern_base)) {
                    // The trailer is whatever follows the "*" in the key.
                    std::string pattern_trailer = expansion.key.substr(star + 1);

                    // The match also has to respect the trailer: either there
                    // is none, or the candidate ends with it and is at least
                    // as long as the key itself.
                    if (pattern_trailer.empty() ||
                        (match_key.ends_with(pattern_trailer) && match_key.size() >= expansion.key.size())) {
                        // The text between the prefix and the trailer is the
                        // subpath that fills the pattern's "*".
                        const PjEntry& target   = expansion.value;
                        std::string    subpath  = match_key.substr(pattern_base.size(),
                                                                  match_key.size() - pattern_base.size() -
                                                                          pattern_trailer.size());
                        if (debug_logs) {
                            debug_logs->AddNote("The key " + helpers::QuoteForJSON(expansion.key, false) +
                                                " matched with " + helpers::QuoteForJSON(subpath, false) +
                                                " left over");
                        }
                        return EsmPackageTargetResolve(package_url, target, subpath, /*pattern=*/true, is_imports,
                                                       conditions);
                    }
                }
            } else {
                // A plain expansion key (ending in "/") matches any candidate
                // that starts with the key itself; the remainder is the
                // subpath, resolved without pattern substitution.
                if (match_key.starts_with(expansion.key)) {
                    const PjEntry& target  = expansion.value;
                    std::string    subpath = match_key.substr(expansion.key.size());
                    if (debug_logs) {
                        debug_logs->AddNote("The key " + helpers::QuoteForJSON(expansion.key, false) +
                                            " matched with " + helpers::QuoteForJSON(subpath, false) +
                                            " left over");
                    }
                    EsmStep step = EsmPackageTargetResolve(package_url, target, subpath, /*pattern=*/false,
                                                           is_imports, conditions);
                    if (step.status == PjStatus::kExact || step.status == PjStatus::kExactEndsWithStar) {
                        // A "/"-terminated key never pins the entry exactly:
                        // the file name still has to be discovered.
                        step.status = PjStatus::kInexact;
                    }
                    return step;
                }
            }

            if (debug_logs) {
                debug_logs->AddNote("The key " + helpers::QuoteForJSON(expansion.key, false) + " did not match");
            }
        }

        if (debug_logs) {
            debug_logs->AddNote("No keys matched " + helpers::QuoteForJSON(match_key, false));
        }
        return {"", PjStatus::kNull, PjDebug{.invalid_because = "", .token = match_obj.first_token}};
    }

    // Resolves one target value (the right-hand side of an "imports"/"exports"
    // entry) given a package URL, the leftover subpath, and the active
    // condition set. The four possible target kinds are treated differently:
    // a string is the literal path, an object selects among nested conditions
    // (or, rarely, subpath keys), an array tries each fallback in order, and
    // null means "explicitly blocked". The result is a path plus a status that
    // later stages translate into success or a specific error.
    //
    //   Input:  target string "./dist/feature.js", subpath "./feature",
    //           pattern false
    //   Output: EsmStep with result ".../dist/feature.js", status kExact
    EsmStep ResolverQuery::EsmPackageTargetResolve(const std::string&   package_url,
                                                   const PjEntry&       target,
                                                   const std::string&   subpath,
                                                   bool                 pattern,
                                                   bool                 is_internal,
                                                   const ConditionsMap& conditions)
    {
        switch (target.kind) {
        case PjKind::kString: {
            if (debug_logs) {
                debug_logs->AddNote("Checking path " + helpers::QuoteForJSON(subpath, false) +
                                    " against target " + helpers::QuoteForJSON(target.str_data, false));
                DebugIndentGuard guard(debug_logs);
                return EsmPackageTargetResolveStringCase(package_url, target, subpath, pattern, is_internal);
            }
            return EsmPackageTargetResolveStringCase(package_url, target, subpath, pattern, is_internal);
        }

        case PjKind::kObject: {
            if (debug_logs) {
                std::vector<std::string> keys;
                keys.reserve(conditions.size());
                for (const auto& [key, active] : conditions) {
                    (void)active;
                    keys.push_back(helpers::QuoteForJSON(key, false));
                }
                std::sort(keys.begin(), keys.end());
                std::string joined;
                for (size_t i = 0; i < keys.size(); ++i) {
                    if (i > 0) {
                        joined += ", ";
                    }
                    joined += keys[i];
                }
                debug_logs->AddNote("Checking condition map for one of [" + joined + "]");
            }

            // The indent guard lives for the whole object walk below so the
            // nested condition resolution reads as one step in the trace.
            DebugIndentGuard indent_guard(debug_logs);

            bool              did_find_map_entry = false;
            const PjMapEntry* last_map_entry     = nullptr;

            // A condition object: walk its keys in order and take the first
            // match. "default" always applies; every other key applies only
            // when the condition is active. A condition that matches but whose
            // own target ultimately resolves to nothing is remembered in case
            // the whole object ends up with no applicable answer.
            for (const PjMapEntry& p : target.map_data) {
                if (p.key == "default" || ConditionActive(conditions, p.key)) {
                    if (debug_logs) {
                        debug_logs->AddNote("The key " + helpers::QuoteForJSON(p.key, false) + " applies");
                    }
                    EsmStep step = EsmPackageTargetResolve(package_url, p.value, subpath, pattern, is_internal,
                                                           conditions);
                    if (PjStatusIsUndefined(step.status)) {
                        did_find_map_entry = true;
                        last_map_entry     = &p;
                        continue;
                    }
                    return step;
                }
                if (debug_logs) {
                    debug_logs->AddNote("The key " + helpers::QuoteForJSON(p.key, false) + " does not apply");
                }
            }

            if (debug_logs) {
                debug_logs->AddNote("No keys in the map were applicable");
            }

            // Guchho adds a specific "no conditions matched" error when the
            // object held no usable condition, instead of the generic
            // "undefined" status, so the user sees which conditions would have
            // activated the target.
            if (!target.map_data.empty() && !PjEntryKeysStartWithDot(target)) {
                const PjEntry* effective_target = &target;
                if (did_find_map_entry && last_map_entry->value.kind == PjKind::kObject &&
                    !last_map_entry->value.map_data.empty() && !PjEntryKeysStartWithDot(last_map_entry->value)) {
                    // When a top-level condition applied but none of its own
                    // sub-conditions did, blame the sub-map: the reported
                    // conditions then show precisely what was missing rather
                    // than the broader top-level set.
                    effective_target = &last_map_entry->value;
                }
                std::vector<DebugSpan> map_keys;
                map_keys.reserve(effective_target->map_data.size());
                for (const PjMapEntry& p : effective_target->map_data) {
                    map_keys.push_back(DebugSpan{.text = p.key, .range = p.key_range});
                }
                PjDebug out_debug;
                out_debug.token                = effective_target->first_token;
                out_debug.unmatched_conditions = std::move(map_keys);
                return {"", PjStatus::kUndefinedNoConditionsMatch, std::move(out_debug)};
            }

            return {"", PjStatus::kUndefined, PjDebug{.invalid_because = "", .token = target.first_token}};
        }

        case PjKind::kArray: {
            // An empty array blocks the target outright, exactly like null.
            if (target.arr_data.empty()) {
                if (debug_logs) {
                    debug_logs->AddNote("The path " + helpers::QuoteForJSON(subpath, false) +
                                        " is set to an empty array");
                }
                return {"", PjStatus::kNull, PjDebug{.invalid_because = "", .token = target.first_token}};
            }

            // The indent guard covers the whole fallback walk below.
            DebugIndentGuard indent_guard(debug_logs);
            if (debug_logs) {
                debug_logs->AddNote("Checking for " + helpers::QuoteForJSON(subpath, false) + " in an array");
            }

            PjStatus last_exception = PjStatus::kUndefined;
            PjDebug  last_debug{};
            last_debug.token = target.first_token;
            // Each array element is a fallback: try them in order and return
            // the first that resolves. Targets that are invalid or explicitly
            // blocked are noted but do not abort the walk.
            for (const PjEntry& target_value : target.arr_data) {
                EsmStep step =
                        EsmPackageTargetResolve(package_url, target_value, subpath, pattern, is_internal, conditions);
                if (step.status == PjStatus::kInvalidPackageTarget || step.status == PjStatus::kNull) {
                    last_exception = step.status;
                    last_debug     = std::move(step.debug);
                    continue;
                }
                if (PjStatusIsUndefined(step.status)) {
                    continue;
                }
                return step;
            }

            // Every fallback failed: report the final blocking result, which
            // is the most informative of the errors that were collected.
            return {"", last_exception, std::move(last_debug)};
        }

        // A null literal explicitly says "this path is blocked": it resolves to
        // nothing and is flagged so the error can explain the author's intent.
        case PjKind::kNull:
            if (debug_logs) {
                debug_logs->AddNote("The path " + helpers::QuoteForJSON(subpath, false) + " is set to null");
            }
            return {"", PjStatus::kNull,
                    PjDebug{.invalid_because = "", .token = target.first_token, .is_because_of_null_literal = true}};

        case PjKind::kInvalid:
            break;
        }

        // An invalid entry or any unrecognized kind lands here: the target is
        // unusable and the map entry points to it for the diagnostic.
        if (debug_logs) {
            debug_logs->AddNote("Invalid package target for path " + helpers::QuoteForJSON(subpath, false));
        }
        return {"", PjStatus::kInvalidPackageTarget, PjDebug{.invalid_because = "", .token = target.first_token}};
    }

    // The string case of "EsmPackageTargetResolve", split into its own
    // function so the outer debug-log indent guard in the string branch stays
    // confined to this function's body.
    EsmStep ResolverQuery::EsmPackageTargetResolveStringCase(const std::string& package_url,
                                                             const PjEntry&      target,
                                                             const std::string&  subpath,
                                                             bool                pattern,
                                                             bool                is_internal)
    {
        // Without pattern substitution, a non-empty subpath can only be
        // appended to a target that ends in "/". Any other combination is an
        // invalid module specifier.
        if (!pattern && !subpath.empty() && !target.str_data.ends_with('/')) {
            if (debug_logs) {
                debug_logs->AddNote("The target " + helpers::QuoteForJSON(target.str_data, false) +
                                    " is invalid because it doesn't end in \"/\"");
            }
            PjDebug out_debug;
            out_debug.token          = target.first_token;
            out_debug.invalid_because = " because it doesn't end in \"/\"";
            return {target.str_data, PjStatus::kInvalidModuleSpecifier, std::move(out_debug)};
        }

        // A string that does not begin with "./" cannot be a relative package
        // target at all.
        if (!target.str_data.starts_with("./")) {
            if (is_internal && !target.str_data.starts_with("../") && !target.str_data.starts_with("/")) {
                // Inside its own package ("imports"), a bare name is allowed
                // and simply re-exports another package: "*" is substituted or
                // the subpath is appended to form the new specifier.
                if (pattern) {
                    std::string result = target.str_data;
                    ReplaceAll(result, "*", subpath);
                    if (debug_logs) {
                        debug_logs->AddNote("Substituted " + helpers::QuoteForJSON(subpath, false) + " for \"*\" in " +
                                            helpers::QuoteForJSON(target.str_data, false) + " to get " +
                                            helpers::QuoteForJSON(result, false));
                    }
                    return {result, PjStatus::kPackageResolve, PjDebug{.invalid_because = "", .token = target.first_token}};
                }
                std::string result = target.str_data + subpath;
                if (debug_logs) {
                    debug_logs->AddNote("Joined " + helpers::QuoteForJSON(target.str_data, false) + " to " +
                                        helpers::QuoteForJSON(subpath, false) + " to get " +
                                        helpers::QuoteForJSON(result, false));
                }
                return {result, PjStatus::kPackageResolve, PjDebug{.invalid_because = "", .token = target.first_token}};
            }
            if (debug_logs) {
                debug_logs->AddNote("The target " + helpers::QuoteForJSON(target.str_data, false) +
                                    " is invalid because it doesn't start with \"./\"");
            }
            PjDebug out_debug;
            out_debug.token          = target.first_token;
            out_debug.invalid_because = " because it doesn't start with \"./\"";
            return {target.str_data, PjStatus::kInvalidPackageTarget, std::move(out_debug)};
        }

        // The target itself may not contain ".", "..", or "node_modules" segments
        // after the leading segment: such targets could escape the package or
        // reach private code, and are an invalid package target.
        std::string_view invalid_segment = FindInvalidSegment(target.str_data);
        if (!invalid_segment.empty()) {
            if (debug_logs) {
                debug_logs->AddNote("The target " + helpers::QuoteForJSON(target.str_data, false) +
                                    " is invalid because it contains invalid segment " +
                                    helpers::QuoteForJSON(invalid_segment, false));
            }
            PjDebug out_debug;
            out_debug.token          = target.first_token;
            out_debug.invalid_because = " because it contains invalid segment " +
                                        helpers::QuoteForJSON(invalid_segment, false);
            return {target.str_data, PjStatus::kInvalidPackageTarget, std::move(out_debug)};
        }

        // The target is valid, so join it onto the package URL to get the
        // resolved-on-disk prefix.
        std::string resolved_target = internal::PosixPathJoin({package_url, target.str_data});

        // The leftover subpath is validated with the same segment rule as the
        // target; a bad segment here is an invalid module specifier.
        invalid_segment = FindInvalidSegment(subpath);
        if (!invalid_segment.empty()) {
            if (debug_logs) {
                debug_logs->AddNote("The path " + helpers::QuoteForJSON(subpath, false) +
                                    " is invalid because it contains invalid segment " +
                                    helpers::QuoteForJSON(invalid_segment, false));
            }
            PjDebug out_debug;
            out_debug.token          = target.first_token;
            out_debug.invalid_because = " because it contains invalid segment " +
                                        helpers::QuoteForJSON(invalid_segment, false);
            return {subpath, PjStatus::kInvalidModuleSpecifier, std::move(out_debug)};
        }

        if (pattern) {
            // Pattern match: substitute the captured subpath for every "*" in
            // the joined target. A target whose final character is "*" is
            // recorded distinctly so the caller can offer extension hints.
            std::string result = resolved_target;
            ReplaceAll(result, "*", subpath);
            if (debug_logs) {
                debug_logs->AddNote("Substituted " + helpers::QuoteForJSON(subpath, false) + " for \"*\" in " +
                                    helpers::QuoteForJSON("." + resolved_target, false) + " to get " +
                                    helpers::QuoteForJSON("." + result, false));
            }
            PjStatus out_status = PjStatus::kExact;
            if (resolved_target.ends_with('*') &&
                resolved_target.find_last_of('*') == resolved_target.size() - 1) {
                out_status = PjStatus::kExactEndsWithStar;
            }
            return {result, out_status, PjDebug{.invalid_because = "", .token = target.first_token}};
        }

        // Ordinary suffix join: append the subpath to the resolved target.
        std::string result = internal::PosixPathJoin({resolved_target, subpath});
        if (debug_logs) {
            debug_logs->AddNote("Joined " + helpers::QuoteForJSON(subpath, false) + " to " +
                                helpers::QuoteForJSON("." + resolved_target, false) + " to get " +
                                helpers::QuoteForJSON("." + result, false));
        }
        return {result, PjStatus::kExact, PjDebug{.invalid_because = "", .token = target.first_token}};
    }

    // Asks the "exports" map the opposite of the normal question: given a
    // path on disk, which subpath would export it. Used to produce the
    // "WAS this file exported under a different name?" suggestion when a
    // direct import was refused.
    ReverseResolveResult ResolverQuery::EsmPackageExportsReverseResolve(const std::string&   query,
                                                                        const PjEntry&       root,
                                                                        const ConditionsMap& conditions)
    {
        if (root.kind == PjKind::kObject && PjEntryKeysStartWithDot(root)) {
            ReverseResolveResult result = EsmPackageImportsExportsReverseResolve(query, root, conditions);
            if (result.ok) {
                return result;
            }
        }

        return {};
    }

    // Walks one subpath-keyed map in reverse. Exact keys are tried first (unless
    // the query itself ends in "*", which can only arise from a pattern), then
    // the expansion keys in their sorted order, first as patterns and then as
    // plain prefixes. The first key that could have produced the query wins.
    ReverseResolveResult ResolverQuery::EsmPackageImportsExportsReverseResolve(const std::string&   query,
                                                                               const PjEntry&       match_obj,
                                                                               const ConditionsMap& conditions)
    {
        if (!query.ends_with('*')) {
            // Exact keys are compared whole before any pattern logic runs.
            for (const PjMapEntry& entry : match_obj.map_data) {
                ReverseResolveResult result = EsmPackageTargetReverseResolve(query, entry.key, entry.value,
                                                                             EsmReverseKind::kExact, conditions);
                if (result.ok) {
                    return result;
                }
            }
        }

        // Expansion keys get two passes: a full pattern match against a
        // "*"-terminated key first, then a loose prefix match.
        for (const PjMapEntry& expansion : match_obj.expansion_keys) {
            if (expansion.key.ends_with('*')) {
                ReverseResolveResult result = EsmPackageTargetReverseResolve(query, expansion.key, expansion.value,
                                                                             EsmReverseKind::kPattern, conditions);
                if (result.ok) {
                    return result;
                }
            }

            ReverseResolveResult result = EsmPackageTargetReverseResolve(query, expansion.key, expansion.value,
                                                                         EsmReverseKind::kPrefix, conditions);
            if (result.ok) {
                return result;
            }
        }

        return {};
    }

    // Answers whether the given "query" path could have been produced by the
    // target of a single map entry, and if so under what subpath. The target
    // may itself be a string (matched one of three ways per "reverse_kind"),
    // a condition object (only active conditions are considered, in order),
    // or a fallback array (each element tried in order).
    ReverseResolveResult ResolverQuery::EsmPackageTargetReverseResolve(const std::string&   query,
                                                                       const std::string&   key,
                                                                       const PjEntry&       target,
                                                                       EsmReverseKind       reverse_kind,
                                                                       const ConditionsMap& conditions)
    {
        switch (target.kind) {
        case PjKind::kString:
            switch (reverse_kind) {
            case EsmReverseKind::kExact:
                if (query == target.str_data) {
                    return ReverseResolveResult{.ok = true, .subpath = key, .token = target.first_token};
                }
                break;

            case EsmReverseKind::kPrefix:
                if (query.starts_with(target.str_data)) {
                    return ReverseResolveResult{
                            .ok = true,
                            .subpath = key + query.substr(target.str_data.size()),
                            .token = target.first_token,
                    };
                }
                break;

            case EsmReverseKind::kPattern: {
                size_t star = target.str_data.find('*');
                // The key's own trailing "*" is stripped from the subpath it
                // reports, since the "*" only stood for the captured text.
                std::string key_without_trailing_star = key;
                if (key_without_trailing_star.ends_with('*')) {
                    key_without_trailing_star.pop_back();
                }

                // A pattern target with no "*" degrades to an exact match.
                if (star == std::string::npos) {
                    if (query == target.str_data) {
                        return ReverseResolveResult{.ok           = true,
                                                    .subpath      = key_without_trailing_star,
                                                    .token        = target.first_token};
                    }
                    break;
                }

                // Tracing honours a single "*": the query must bookend the
                // target's prefix and suffix, and everything in between is
                // the captured text appended to the key.
                std::string prefix = target.str_data.substr(0, star);
                std::string suffix = target.str_data.substr(star + 1);
                if (suffix.find('*') == std::string::npos && query.starts_with(prefix)) {
                    std::string after_prefix = query.substr(prefix.size());
                    if (after_prefix.ends_with(suffix)) {
                        std::string star_data = after_prefix.substr(0, after_prefix.size() - suffix.size());
                        return ReverseResolveResult{.ok      = true,
                                                    .subpath = key_without_trailing_star + star_data,
                                                    .token   = target.first_token};
                    }
                }
                break;
            }
            }
            break;

        case PjKind::kObject:
            // Only conditions that are active (or the "default" catch-all)
            // could have produced the match, so those are the only branches
            // worth tracing.
            for (const PjMapEntry& p : target.map_data) {
                if (p.key == "default" || ConditionActive(conditions, p.key)) {
                    ReverseResolveResult result = EsmPackageTargetReverseResolve(query, key, p.value, reverse_kind,
                                                                                 conditions);
                    if (result.ok) {
                        return result;
                    }
                }
            }
            break;

        case PjKind::kArray:
            // Every fallback is a candidate; the first that accounts for the
            // query wins.
            for (const PjEntry& target_value : target.arr_data) {
                ReverseResolveResult result = EsmPackageTargetReverseResolve(query, key, target_value, reverse_kind,
                                                                             conditions);
                if (result.ok) {
                    return result;
                }
            }
            break;

        default:
            break;
        }

        return {};
    }

} // namespace guchho::resolver
