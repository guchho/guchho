#include "guchho/logger.hpp"
#include "guchho/helpers.hpp"

#include <format>
#include <functional>
#include <sstream>
#include <cstdlib>
#include <stdexcept>
#include <algorithm>

namespace guchho::logger{

    // Parses a user-facing message name (e.g. "assert-to-with", "package.json",
    // "css-syntax-error") into one or more MsgID enum values and records
    // each into the `overrides` map with the given log level.
    //
    // This is the primary mechanism by which the --warning flag maps human-readable
    // diagnostic names to internal message identifiers.  A single name may map to
    // multiple MsgIDs (for example, "package.json" covers every package.json-related
    // diagnostic).
    //
    // Input:  str = "empty-glob", logLevel = kWarning
    // Output: overrides[kBundler_EmptyGlob] = kWarning
    void StringToMsgIDs(std::string_view str, LogLevel logLevel, std::unordered_map<MsgID, LogLevel>& overrides)
    {
        #define MATCH(s, e) if (str == s) { overrides[MsgID::e] = logLevel; } else

            // JavaScript
            MATCH("assert-to-with",                 kJS_AssertToWith)
            MATCH("assert-type-json",               kJS_AssertTypeJSON)
            MATCH("assign-to-constant",             kJS_AssignToConstant)
            MATCH("assign-to-define",               kJS_AssignToDefine)
            MATCH("assign-to-import",               kJS_AssignToImport)
            MATCH("bigint",                         kJS_BigInt)
            MATCH("call-import-namespace",          kJS_CallImportNamespace)
            MATCH("class-name-will-throw",          kJS_ClassNameWillThrow)
            MATCH("commonjs-variable-in-esm",       kJS_CommonJSVariableInESM)
            MATCH("delete-super-property",          kJS_DeleteSuperProperty)
            MATCH("direct-eval",                    kJS_DirectEval)
            MATCH("duplicate-case",                 kJS_DuplicateCase)
            MATCH("duplicate-class-member",         kJS_DuplicateClassMember)
            MATCH("duplicate-object-key",           kJS_DuplicateObjectKey)
            MATCH("empty-import-meta",              kJS_EmptyImportMeta)
            MATCH("equals-nan",                     kJS_EqualsNaN)
            MATCH("equals-negative-zero",           kJS_EqualsNegativeZero)
            MATCH("equals-new-object",              kJS_EqualsNewObject)
            MATCH("html-comment-in-js",             kJS_HTMLCommentInJS)
            MATCH("impossible-typeof",              kJS_ImpossibleTypeof)
            MATCH("indirect-require",               kJS_IndirectRequire)
            MATCH("private-name-will-throw",        kJS_PrivateNameWillThrow)
            MATCH("semicolon-after-return",         kJS_SemicolonAfterReturn)
            MATCH("suspicious-boolean-not",         kJS_SuspiciousBooleanNot)
            MATCH("suspicious-define",              kJS_SuspiciousDefine)
            MATCH("suspicious-logical-operator",    kJS_SuspiciousLogicalOperator)
            MATCH("suspicious-nullish-coalescing",  kJS_SuspiciousNullishCoalescing)
            MATCH("this-is-undefined-in-esm",       kJS_ThisIsUndefinedInESM)
            MATCH("unsupported-dynamic-import",     kJS_UnsupportedDynamicImport)
            MATCH("unsupported-jsx-comment",        kJS_UnsupportedJSXComment)
            MATCH("unsupported-regexp",             kJS_UnsupportedRegExp)
            MATCH("unsupported-require-call",       kJS_UnsupportedRequireCall)

            // CSS
            MATCH("css-syntax-error",               kCSS_CSSSyntaxError)
            MATCH("invalid-@charset",               kCSS_InvalidAtCharset)
            MATCH("invalid-@import",                kCSS_InvalidAtImport)
            MATCH("invalid-@layer",                 kCSS_InvalidAtLayer)
            MATCH("invalid-calc",                   kCSS_InvalidCalc)
            MATCH("js-comment-in-css",              kCSS_JSCommentInCSS)
            MATCH("undefined-composes-from",        kCSS_UndefinedComposesFrom)
            MATCH("unsupported-@charset",           kCSS_UnsupportedAtCharset)
            MATCH("unsupported-@namespace",         kCSS_UnsupportedAtNamespace)
            MATCH("unsupported-css-property",       kCSS_UnsupportedCSSProperty)
            MATCH("unsupported-css-nesting",        kCSS_UnsupportedCSSNesting)

            // HTML
            MATCH("html-parse-warning",             kHTML_ParseWarning)
            MATCH("invalid-resource-url",           kHTML_InvalidResourceURL)
            MATCH("import-map-reordered",           kHTML_ImportMapReordered)

            // Bundler
            MATCH("ambiguous-reexport",             kBundler_AmbiguousReexport)
            MATCH("different-path-case",            kBundler_DifferentPathCase)
            MATCH("empty-glob",                     kBundler_EmptyGlob)
            MATCH("ignored-bare-import",            kBundler_IgnoredBareImport)
            MATCH("ignored-dynamic-import",         kBundler_IgnoredDynamicImport)
            MATCH("import-is-undefined",            kBundler_ImportIsUndefined)
            MATCH("require-resolve-not-external",   kBundler_RequireResolveNotExternal)

            // Source maps
            MATCH("invalid-source-mappings",        kSourceMap_InvalidSourceMappings)
            MATCH("missing-source-map",             kSourceMap_MissingSourceMap)
            MATCH("unsupported-source-map-comment", kSourceMap_UnsupportedSourceMapComment)

            if (str == "package.json") {
                for (uint8_t i = static_cast<uint8_t>(MsgID::kPackageJSON_FIRST);
                    i <= static_cast<uint8_t>(MsgID::kPackageJSON_LAST); ++i) {
                    overrides[static_cast<MsgID>(i)] = logLevel;
                }
            }

            else if (str == "tsconfig.json") {
                for (uint8_t i = static_cast<uint8_t>(MsgID::kTSConfigJSON_FIRST);
                    i <= static_cast<uint8_t>(MsgID::kTSConfigJSON_LAST); ++i) {
                    overrides[static_cast<MsgID>(i)] = logLevel;
                }
            }

            else if (str == "guchho.json") {
                for (uint8_t i = static_cast<uint8_t>(MsgID::kGuchhoJSON_FIRST);
                    i <= static_cast<uint8_t>(MsgID::kGuchhoJSON_LAST); ++i) {
                    overrides[static_cast<MsgID>(i)] = logLevel;
                }
            }

            else if (str == "guchho.config.js") {
                for (uint8_t i = static_cast<uint8_t>(MsgID::kGuchhoConfig_FIRST);
                    i <= static_cast<uint8_t>(MsgID::kGuchhoConfig_LAST); ++i) {
                    overrides[static_cast<MsgID>(i)] = logLevel;
                }
            }

            else {
                // অজানা বা অবৈধ Message ID উপেক্ষা করো.
                // কারণ এই কোড লেখার পর Message ID-এর নাম
                // পরিবর্তন বা মুছে ফেলা হয়ে থাকতে পারে.
            }

        #undef MATCH
    }


    // Converts a MsgID enum value to its human-readable string name.
    //
    // This is the inverse of StringToMsgIDs and is used when rendering
    // diagnostics so that users see familiar names like "css-syntax-error"
    // rather than opaque enum values.
    //
    // Input:  MsgID::kJS_DuplicateCase
    // Output: "duplicate-case"
    //
    // For file-scoped message ranges (package.json, tsconfig.json, etc.),
    // returns the config file name instead.
    std::string_view MsgIDToString(MsgID id)
    {
        switch (id) {
            // JavaScript
            case MsgID::kJS_AssertToWith:               return "assert-to-with";
            case MsgID::kJS_AssertTypeJSON:             return "assert-type-json";
            case MsgID::kJS_AssignToConstant:           return "assign-to-constant";
            case MsgID::kJS_AssignToDefine:             return "assign-to-define";
            case MsgID::kJS_AssignToImport:             return "assign-to-import";
            case MsgID::kJS_BigInt:                     return "bigint";
            case MsgID::kJS_CallImportNamespace:        return "call-import-namespace";
            case MsgID::kJS_ClassNameWillThrow:         return "class-name-will-throw";
            case MsgID::kJS_CommonJSVariableInESM:      return "commonjs-variable-in-esm";
            case MsgID::kJS_DeleteSuperProperty:        return "delete-super-property";
            case MsgID::kJS_DirectEval:                 return "direct-eval";
            case MsgID::kJS_DuplicateCase:              return "duplicate-case";
            case MsgID::kJS_DuplicateClassMember:       return "duplicate-class-member";
            case MsgID::kJS_DuplicateObjectKey:         return "duplicate-object-key";
            case MsgID::kJS_EmptyImportMeta:            return "empty-import-meta";
            case MsgID::kJS_EqualsNaN:                  return "equals-nan";
            case MsgID::kJS_EqualsNegativeZero:         return "equals-negative-zero";
            case MsgID::kJS_EqualsNewObject:            return "equals-new-object";
            case MsgID::kJS_HTMLCommentInJS:            return "html-comment-in-js";
            case MsgID::kJS_ImpossibleTypeof:           return "impossible-typeof";
            case MsgID::kJS_IndirectRequire:            return "indirect-require";
            case MsgID::kJS_PrivateNameWillThrow:       return "private-name-will-throw";
            case MsgID::kJS_SemicolonAfterReturn:       return "semicolon-after-return";
            case MsgID::kJS_SuspiciousBooleanNot:       return "suspicious-boolean-not";
            case MsgID::kJS_SuspiciousDefine:           return "suspicious-define";
            case MsgID::kJS_SuspiciousLogicalOperator:  return "suspicious-logical-operator";
            case MsgID::kJS_SuspiciousNullishCoalescing:return "suspicious-nullish-coalescing";
            case MsgID::kJS_ThisIsUndefinedInESM:       return "this-is-undefined-in-esm";
            case MsgID::kJS_UnsupportedDynamicImport:   return "unsupported-dynamic-import";
            case MsgID::kJS_UnsupportedJSXComment:      return "unsupported-jsx-comment";
            case MsgID::kJS_UnsupportedRegExp:          return "unsupported-regexp";
            case MsgID::kJS_UnsupportedRequireCall:     return "unsupported-require-call";

            // CSS
            case MsgID::kCSS_CSSSyntaxError:            return "css-syntax-error";
            case MsgID::kCSS_InvalidAtCharset:          return "invalid-@charset";
            case MsgID::kCSS_InvalidAtImport:           return "invalid-@import";
            case MsgID::kCSS_InvalidAtLayer:            return "invalid-@layer";
            case MsgID::kCSS_InvalidCalc:               return "invalid-calc";
            case MsgID::kCSS_JSCommentInCSS:            return "js-comment-in-css";
            case MsgID::kCSS_UndefinedComposesFrom:     return "undefined-composes-from";
            case MsgID::kCSS_UnsupportedAtCharset:      return "unsupported-@charset";
            case MsgID::kCSS_UnsupportedAtNamespace:    return "unsupported-@namespace";
            case MsgID::kCSS_UnsupportedCSSProperty:    return "unsupported-css-property";
            case MsgID::kCSS_UnsupportedCSSNesting:     return "unsupported-css-nesting";

            // HTML
            case MsgID::kHTML_ParseWarning:             return "html-parse-warning";
            case MsgID::kHTML_InvalidResourceURL:       return "invalid-resource-url";
            case MsgID::kHTML_ImportMapReordered:       return "import-map-reordered";

            // Bundler
            case MsgID::kBundler_AmbiguousReexport:         return "ambiguous-reexport";
            case MsgID::kBundler_DifferentPathCase:         return "different-path-case";
            case MsgID::kBundler_EmptyGlob:                 return "empty-glob";
            case MsgID::kBundler_IgnoredBareImport:         return "ignored-bare-import";
            case MsgID::kBundler_IgnoredDynamicImport:      return "ignored-dynamic-import";
            case MsgID::kBundler_ImportIsUndefined:         return "import-is-undefined";
            case MsgID::kBundler_RequireResolveNotExternal: return "require-resolve-not-external";

            // Source maps
            case MsgID::kSourceMap_InvalidSourceMappings:      return "invalid-source-mappings";
            case MsgID::kSourceMap_MissingSourceMap:           return "missing-source-map";
            case MsgID::kSourceMap_UnsupportedSourceMapComment:return "unsupported-source-map-comment";

            default:
                break;
        }

        if (id >= MsgID::kPackageJSON_FIRST && id <= MsgID::kPackageJSON_LAST) {
            return "package.json";
        }

        if (id >= MsgID::kTSConfigJSON_FIRST && id <= MsgID::kTSConfigJSON_LAST) {
            return "tsconfig.json";
        }

        if (id >= MsgID::kGuchhoJSON_FIRST && id <= MsgID::kGuchhoJSON_LAST) {
            return "guchho.json";
        }

        if (id >= MsgID::kGuchhoConfig_FIRST && id <= MsgID::kGuchhoConfig_LAST) {
            return "guchho.config.js";
        }

        return "";
    }

    // Some MsgID values represent distinct internal diagnostics that share the
    // same external display name.  Because users can only reference these by name
    // (e.g. via --warning=package.json), this function maps a name to the
    // numerically largest MsgID it covers.  This is used for range-based lookups
    // where a single name may correspond to a contiguous block of IDs.
    //
    // Input:  "package.json"
    // Output: MsgID::kPackageJSON_LAST (the highest ID in the package.json range)
    MsgID StringToMaximumMsgID(std::string_view id)
    {
        std::unordered_map<MsgID, LogLevel> overrides;
        MsgID maxID = MsgID::kNone;

        StringToMsgIDs(id, LogLevel::kInfo, overrides);

        for (const auto& [msgID, level] : overrides) {
            if (msgID > maxID) {
                maxID = msgID;
            }
        }

        return maxID;
    }

    // Returns the uppercase display label for a message severity level.
    //
    // Input:  MsgKind::kWarning
    // Output: "WARNING"
    std::string MsgKindToString(MsgKind kind) {
        switch (kind) {
            case MsgKind::kError:   return "ERROR";
            case MsgKind::kWarning: return "WARNING";
            case MsgKind::kInfo:    return "INFO";
            case MsgKind::kNote:    return "NOTE";
            case MsgKind::kDebug:   return "DEBUG";
            case MsgKind::kVerbose: return "VERBOSE";
        }
        return "";
    }

    // Returns a short Unicode icon prefix used to visually label a diagnostic
    // line in terminal output.  Each severity level gets a distinct glyph.
    //
    // On Windows Command Prompt (where full Unicode box-drawing is unsupported),
    // a reduced glyph set is used to avoid rendering artifacts.
    //
    // Input:  MsgKind::kError
    // Output: "✘"  (or "X" on Windows Command Prompt)
    std::string_view MsgKindToIcon(MsgKind kind)
    {
        
        if (IsProbablyWindowsCommandPrompt()) {
            switch (kind) {
                case MsgKind::kError:
                    return "X";
                case MsgKind::kWarning:
                    return "▲";
                case MsgKind::kInfo:
                    return "►";
                case MsgKind::kNote:
                    return "→";
                case MsgKind::kDebug:
                    return "●";
                case MsgKind::kVerbose:
                    return "♦";
            }

            throw std::logic_error("Internal error");
        }

        switch (kind) {
            case MsgKind::kError:
                return "✘";
            case MsgKind::kWarning:
                return "▲";
            case MsgKind::kInfo:
                return "▶";
            case MsgKind::kNote:
                return "→";
            case MsgKind::kDebug:
                return "●";
            case MsgKind::kVerbose:
                return "⬥";
        }

        throw std::logic_error("Internal error");
    }

    // Reads a 32-bit unsigned integer from a byte string in little-endian order.
    //
    // Input:  bytes = { 0x03, 0x00, 0x00, 0x00, ... }
    // Output: 3
    static uint32_t ReadUInt32LE(std::string_view bytes)
    {
        return static_cast<uint32_t>(static_cast<unsigned char>(bytes[0])) |
            (static_cast<uint32_t>(static_cast<unsigned char>(bytes[1])) << 8) |
            (static_cast<uint32_t>(static_cast<unsigned char>(bytes[2])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(bytes[3])) << 24);
    }

    // Decodes the packed binary representation of import attributes into a
    // key/value pair array.  The binary format stores each attribute as a
    // 4-byte little-endian key length, followed by the key bytes, then a
    // 4-byte little-endian value length, followed by the value bytes.
    //
    // Input:  packed_data = "\x06\x00\x00\x00type\x06\x00\x00\x00module"
    // Output: [{key="type", value="module"}]
    //
    // Returns an empty vector if packed_data is empty.
    std::vector<ImportAttribute> ImportAttributes::DecodeIntoArray() const
    {
        std::vector<ImportAttribute> result;

        if (packed_data.empty()) {
            return result;
        }

        std::string_view bytes = packed_data;

        while (!bytes.empty()) {
            uint32_t key_len = ReadUInt32LE(bytes.substr(0, 4));
            std::string key(bytes.substr(4, key_len));
            bytes.remove_prefix(4 + key_len);

            uint32_t value_len = ReadUInt32LE(bytes.substr(0, 4));
            std::string value(bytes.substr(4, value_len));
            bytes.remove_prefix(4 + value_len);

            result.push_back({std::move(key), std::move(value)});
        }

        return result;
    }

    // Decodes packed import attributes into an unordered_map.
    // Convenience wrapper around DecodeIntoArray() for callers that need
    // key-based lookup rather than ordered iteration.
    //
    // If multiple entries share the same key, the last one wins.
    std::unordered_map<std::string, std::string> ImportAttributes::DecodeIntoMap() const
    {
        auto arr = DecodeIntoArray();

        if (arr.empty()) {
            return {};
        }

        std::unordered_map<std::string, std::string> result;
        result.reserve(arr.size());

        for (auto& attr : arr) {
            result[attr.key] = std::move(attr.value);
        }

        return result;
    }

    // Encodes a key/value map into the packed binary format used by
    // ImportAttributes.  Keys are sorted alphabetically to produce a
    // canonical representation that is safe for comparison and hashing.
    //
    // Input:  {"type": "module", "with": "json"}
    // Output: packed bytes: [3]["type"][6]["module"][4]["with"][4]["json"]
    ImportAttributes EncodeImportAttributes(const std::unordered_map<std::string, std::string>& value) {
        if (value.empty()) return {};
        std::vector<std::string> keys;
        keys.reserve(value.size());
        for (const auto& kv : value) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        std::string sb;
        for (const auto& k : keys) {
            const auto& v = value.at(k);
            uint32_t kn = static_cast<uint32_t>(k.size());
            uint32_t vn = static_cast<uint32_t>(v.size());
            sb.append(reinterpret_cast<const char*>(&kn), 4);
            sb.append(k);
            sb.append(reinterpret_cast<const char*>(&vn), 4);
            sb.append(v);
        }
        return {std::move(sb)};
    }

    // Returns true if this path has been marked as disabled via the
    // PathFlags::kPathDisabled flag.  Disabled paths are skipped during
    // module resolution and bundling.
    bool Path::IsDisabled() const {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(PathFlags::kPathDisabled)) != 0;
    }

    // Combines hashes of all Path fields (text, namespace, ignored_suffix,
    // import_attributes, flags) into a single size_t using boost-style
    // hash combining.  This allows Path to be used as a key in
    // std::unordered_map / std::unordered_set.
    size_t PathHash::operator()(const Path& path) const {
        size_t h = std::hash<std::string>{}(path.text);
        h ^= std::hash<std::string>{}(path.namespace_) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(path.ignored_suffix) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(path.import_attributes.packed_data) +
            0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(path.flags));
        return h;
    }

    // Two Source objects are considered equal if they refer to the same file
    // content with the same paths, identifier, key path, and index.
    // This is used for deduplication in the module graph.
    bool Source::operator==(const Source& other) const {
        return pretty_paths.abs == other.pretty_paths.abs &&
               pretty_paths.rel == other.pretty_paths.rel &&
               identifier_name == other.identifier_name &&
               contents == other.contents &&
               key_path == other.key_path &&
               index == other.index;
    }

    // Extracts the raw source text covered by a given Range.
    //
    // Input:  Range{Loc{10}, 5} with contents = "abcdefg...xyz"
    // Output: the 5-byte substring starting at offset 10
    std::string Source::TextForRange(Range r) const {
        return contents.substr(static_cast<size_t>(r.loc.start), static_cast<size_t>(r.len));
    }

    // Walks backward from `loc` through contiguous whitespace characters
    // (space, tab, carriage return, newline) and returns the location of the
    // first non-whitespace character.  Useful for trimming trailing whitespace
    // when computing a range that should exclude it.
    //
    // Input:  loc = Loc{7} in "foo   \nbar" (the '\n')
    // Output: Loc{3} (the last 'o' in "foo")
    Loc Source::LocBeforeWhitespace(Loc loc) const
    {
        while (loc.start > 0) {
            auto [ch, width] =
                helpers::DecodeLastRuneInString(contents.substr(0, static_cast<size_t>(loc.start)));

            if (ch != U' ' &&
                ch != U'\t' &&
                ch != U'\r' &&
                ch != U'\n') {
                break;
            }

            loc.start -= width;
        }

        return loc;
    }

    // Searches backward from `loc` for the last occurrence of the string `op`
    // in the source text preceding that location.  Returns a Range covering
    // the operator if found, or a zero-length range at `loc` if not found.
    //
    // Input:  loc at offset 20, op = "==", source contains "x === y" at offsets 14-17
    // Output: Range{Loc{14}, 2}
    Range Source::RangeOfOperatorBefore(Loc loc, const std::string& op) const {
        auto text = contents.substr(0, static_cast<size_t>(loc.start));
        auto pos = text.rfind(op);
        if (pos != std::string::npos) {
            return {Loc{static_cast<int32_t>(pos)}, static_cast<int32_t>(op.size())};
        }
        return {loc, 0};
    }

    // Searches forward from `loc` for the first occurrence of the string `op`
    // in the source text starting at that location.  Returns a Range covering
    // the operator if found, or a zero-length range at `loc` if not found.
    //
    // Input:  loc at offset 5, op = "=>", source contains "=> {}" at offsets 5-6
    // Output: Range{Loc{5}, 2}
    Range Source::RangeOfOperatorAfter(Loc loc, const std::string& op) const {
        auto text = contents.substr(static_cast<size_t>(loc.start));
        auto pos = text.find(op);
        if (pos != std::string::npos) {
            return {Loc{static_cast<int32_t>(loc.start + static_cast<int32_t>(pos))}, static_cast<int32_t>(op.size())};
        }
        return {loc, 0};
    }

    // Determines the extent of a string literal starting at `loc`.
    // Handles single-quoted, double-quoted, and template literals.
    // For template literals, stops at the first unescaped backtick or
    // at "${" (template expression boundary), whichever comes first.
    // Escape sequences (backslash + next char) are skipped.
    //
    // Input:  loc at offset 0, source = "\"hello\\nworld\""
    // Output: Range{Loc{0}, 13} covering the entire quoted string
    //
    // Returns a zero-length range if no matching closing quote is found.
    Range Source::RangeOfString(Loc loc) const {
        auto text = contents.substr(static_cast<size_t>(loc.start));
        if (text.empty()) return {loc, 0};
        char quote = text[0];
        if (quote == '"' || quote == '\'') {
            for (size_t i = 1; i < text.size(); i++) {
                char c = text[i];
                if (c == quote) {
                    return {loc, static_cast<int32_t>(i + 1)};
                } else if (c == '\\') {
                    i++;
                }
            }
        }
        if (quote == '`') {
            for (size_t i = 1; i < text.size(); i++) {
                char c = text[i];
                if (c == quote) {
                    return {loc, static_cast<int32_t>(i + 1)};
                } else if (c == '\\') {
                    i++;
                } else if (c == '$' && i + 1 < text.size() && text[i + 1] == '{') {
                    break;
                }
            }
        }
        return {loc, 0};
    }

    // Determines the extent of a numeric literal starting at `loc`.
    // Scans forward through characters that could form part of a number:
    // digits 0-9, letters a-z/A-Z (for hex, binary, exponent suffixes),
    // dots (for decimals), and underscores (for numeric separators).
    //
    // Input:  loc at offset 0, source = "123abc"
    // Output: Range{Loc{0}, 6} (the entire token)
    //
    // Input:  loc at offset 0, source = "3.14"
    // Output: Range{Loc{0}, 4}
    Range Source::RangeOfNumber(Loc loc) const {
        auto text = contents.substr(static_cast<size_t>(loc.start));
        Range r{loc, 0};
        if (!text.empty()) {
            char c = text[0];
            if (c >= '0' && c <= '9') {
                r.len = 1;
                while (static_cast<size_t>(r.len) < text.size()) {
                    c = text[static_cast<size_t>(r.len)];
                    if ((c < '0' || c > '9') && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && c != '.' && c != '_') break;
                    r.len++;
                }
            }
        }
        return r;
    }

    // Detects a legacy octal escape sequence (\0 through \377) starting at
    // `loc`.  Requires a leading backslash followed by 2-3 octal digits.
    //
    // Input:  loc at offset 0, source = "\\41x"
    // Output: Range{Loc{0}, 3} covering "\41"
    //
    // Returns a zero-length range if the pattern doesn't match.
    Range Source::RangeOfLegacyOctalEscape(Loc loc) const {
        auto text = contents.substr(static_cast<size_t>(loc.start));
        Range r{loc, 0};
        if (text.size() >= 2 && text[0] == '\\') {
            r.len = 2;
            while (r.len < 4 && static_cast<size_t>(r.len) < text.size()) {
                char c = text[static_cast<size_t>(r.len)];
                if (c < '0' || c > '9') break;
                r.len++;
            }
        }
        return r;
    }

    // Extracts the text content of a block comment (/* ... */) and removes
    // the common leading indentation from all lines after the first.
    //
    // This is used when displaying JSDoc-style comments in diagnostics so
    // that the comment text appears cleanly indented regardless of where
    // in the source it was written.
    //
    // Algorithm:
    //   1. Extract the raw comment text from the Range.
    //   2. Walk backward from the comment start to find the column where
    //      the comment opening delimiter sits (the "initial indent").
    //   3. Split the comment into lines.
    //   4. For every line after the first, measure its leading whitespace.
    //   5. Find the minimum indent across those lines.
    //   6. Strip that minimum indent from each subsequent line.
    //   7. Rejoin the lines with '\n'.
    //
    // Input:  Range covering "    /*\n     * Hello\n     * World\n     */"
    // Output: "    /*\n * Hello\n * World\n */"
    //
    // Edge cases:
    //   - Non-block comments (//) are returned as-is.
    //   - Windows-style \r\n line endings are handled correctly.
    //   - Unicode line separators (U+2028, U+2029) are treated as newlines.
    std::string Source::CommentTextWithoutIndent(const Range& r) const
    {
        std::string text = contents.substr(static_cast<size_t>(r.loc.start), static_cast<size_t>(r.End() - r.loc.start));

        if (text.size() < 2 || !text.starts_with("/*")) {
            return text;
        }

        std::string_view prefix(contents.data(), static_cast<size_t>(r.loc.start));

        // Figure out the initial indent.
        int indent = 0;

        while (!prefix.empty()) {
            auto [c, width] = helpers::DecodeLastRuneInString(prefix);

            if (c == U'\r' ||
                c == U'\n' ||
                c == U'\u2028' ||
                c == U'\u2029') {
                break;
            }

            prefix.remove_suffix(static_cast<size_t>(width));
            ++indent;
        }

        // Split the comment into lines.
        std::vector<std::string> lines;
        size_t start = 0;
        size_t i = 0;

        while (i < text.size()) {
            auto [c, width] = helpers::DecodeWTF8Rune(
                std::string_view(text).substr(i));

            switch (c) {
                case U'\r':
                case U'\n':
                    // Don't double-append for Windows style "\r\n".
                    if (start <= i) {
                        lines.emplace_back(text.substr(start, i - start));
                    }

                    start = i + static_cast<size_t>(width);

                    // Ignore the second part of "\r\n".
                    if (c == U'\r' &&
                        start < text.size() &&
                        text[start] == '\n') {
                        ++start;
                        ++i;
                    }
                    break;

                case U'\u2028':
                case U'\u2029':
                    lines.emplace_back(text.substr(start, i - start));
                    start = i + static_cast<size_t>(width);
                    break;
            }

            i += static_cast<size_t>(width);
        }

        lines.emplace_back(text.substr(start));

        // Find the minimum indent over all lines after the first line.
        for (size_t lineIndex = 1; lineIndex < lines.size(); ++lineIndex) {
            int lineIndent = 0;

            for (char ch : lines[lineIndex]) {
                if (ch != ' ' && ch != '\t') {
                    break;
                }

                ++lineIndent;
            }

            indent = std::min(indent, lineIndent);
        }

        // Trim the indent off of all lines after the first line.
        for (size_t lineIndex = 1; lineIndex < lines.size(); ++lineIndex) {
            lines[lineIndex].erase(0, std::min<size_t>(static_cast<size_t>(indent), lines[lineIndex].size()));
        }

        // Join lines with '\n'.
        std::string result;

        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            if (lineIndex > 0) {
                result.push_back('\n');
            }

            result += lines[lineIndex];
        }

        return result;
    }

    // Returns the byte offset one past the end of this range.
    //
    // Input:  Range{Loc{10}, 5}
    // Output: 15  (covers bytes 10, 11, 12, 13, 14 inclusive)
    int32_t Range::End() const {
        return loc.start + len;
    }

    // Expands this range to also cover range `b`.  If `b` is already fully
    // contained within the current range, the range is unchanged.
    //
    // Input:  this = Range{Loc{10}, 5},  b = Range{Loc{20}, 3}
    // Output: this = Range{Loc{10}, 13}  (now covers bytes 10..22)
    //
    // Edge case: if this range has zero length (*this = {Loc{0}, 0}),
    // it is replaced entirely by `b`.
    void Range::ExpandBy(const Range& b) {
        if (len == 0) {
            *this = b;
        } else {
            int32_t end = End();
            int32_t nEnd = b.End();
            if (nEnd > end) end = nEnd;
            if (b.loc.start < loc.start) loc.start = b.loc.start;
            len = end - loc.start;
        }
    }

    // Splits a file path into three components: directory, base name, and extension.
    //
    // Supports both Unix ('/') and Windows ('\\') path separators.  Preserves
    // the root slash for absolute paths (e.g. "/src" -> dir="/", base="src").
    //
    // Special handling for compound extensions like ".module.css": the entire
    // ".module.css" is treated as the extension rather than just ".css".
    //
    // Input:  "src/components/Button.module.css"
    // Output: dir = "src/components/", base = "Button", ext = ".module.css"
    //
    // Input:  "/home/user/project/index.ts"
    // Output: dir = "/home/user/project/", base = "index", ext = ".ts"
    //
    // Input:  "README"
    // Output: dir = "", base = "README", ext = ""
    void PlatformIndependentPathDirBaseExt(const std::string& path, std::string& dir, std::string& base, std::string& ext) {
        int absRootSlash = -1;

        if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
            absRootSlash = 0;
        } else if (path.size() > 2 && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) {
            char c = path[0];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                absRootSlash = 2;
            }
        }

        std::string p = path;
        while (true) {
            auto i = p.rfind('/');
            auto j = p.rfind('\\');
            size_t slash = (i == std::string::npos) ? j : (j == std::string::npos) ? i : std::max(i, j);

            if (slash == std::string::npos) {
                base = p;
                break;
            }

            int s = static_cast<int>(slash);
            if (s == absRootSlash) {
                dir = p.substr(0, static_cast<size_t>(s + 1));
                base = p.substr(static_cast<size_t>(s + 1));
                break;
            }
            if (static_cast<size_t>(s + 1) != p.size()) {
                dir = p.substr(0, static_cast<size_t>(s));
                base = p.substr(static_cast<size_t>(s + 1));
                break;
            }
            p = p.substr(0, static_cast<size_t>(s));
        }

        auto dot = base.rfind('.');
        if (dot != std::string::npos) {
            ext = base.substr(dot);
            if (ext == ".css") {
                auto dot2 = base.rfind('.', dot - 1);
                if (dot2 != std::string::npos && base.substr(dot2) == ".module.css") {
                    dot = dot2;
                    ext = base.substr(dot);
                }
            }
            base = base.substr(0, dot);
        }
    }

    // Selects the appropriate path representation based on the user's
    // --path-style preference.  Returns the relative path when available
    // and requested, otherwise falls back to the absolute path.
    //
    // Input:  style = kRelPath, rel = "src/index.ts", abs = "/home/user/src/index.ts"
    // Output: "src/index.ts"
    std::string PrettyPaths::Select(PathStyle style) const {
        if (style == PathStyle::kRelPath && !rel.empty()) {
            return rel;
        }
        return abs;
    }

    // Applies a per-message-ID log level override to determine the effective
    // MsgKind for a diagnostic.  If the user specified --warning=kJS_BigInt=error,
    // this function maps kJS_BigInt from its default kind to kError.
    //
    // If no override exists for the given ID, the original kind is returned.
    // If the override maps to the silent level (static_cast<MsgKind>(0)),
    // the message is suppressed entirely.
    //
    // Input:  overrides = {kJS_BigInt: kError}, id = kJS_BigInt, kind = kWarning
    // Output: MsgKind::kError
    //
    // Input:  overrides = {kJS_BigInt: kError}, id = kJS_DuplicateCase, kind = kWarning
    // Output: MsgKind::kWarning  (no override, original kind preserved)
    MsgKind AllowOverride(const std::unordered_map<MsgID, LogLevel>& overrides, MsgID id, MsgKind kind) {
        auto it = overrides.find(id);
        if (it != overrides.end()) {
            switch (it->second) {
                case LogLevel::kVerbose: return MsgKind::kVerbose;
                case LogLevel::kDebug:   return MsgKind::kDebug;
                case LogLevel::kInfo:    return MsgKind::kInfo;
                case LogLevel::kWarning: return MsgKind::kWarning;
                case LogLevel::kError:   return MsgKind::kError;
                default:
                    return static_cast<MsgKind>(0); // silent
            }
        }
        return kind;
    }

    // Parses command-line arguments to build an OutputOptions struct.
    //
    // Recognized flags:
    //   --color=false     -> UseColor::kColorNever
    //   --color=true      -> UseColor::kColorAlways
    //   --log-level=info  -> LogLevel::kInfo
    //   --log-level=warning -> LogLevel::kWarning
    //   --log-level=error -> LogLevel::kError
    //   --log-level=silent -> LogLevel::kSilent
    //
    // Unrecognized arguments are silently ignored.
    // Defaults: color = kColorIfTerminal, log_level = kWarning.
    OutputOptions OutputOptionsForArgs(const std::vector<std::string>& os_args) {
        OutputOptions options;
        options.include_source = true;

        for (const auto& arg : os_args) {
            if (arg == "--color=false") {
                options.color = UseColor::kColorNever;
            } else if (arg == "--color=true" || arg == "--color") {
                options.color = UseColor::kColorAlways;
            } else if (arg == "--log-level=info") {
                options.log_level = LogLevel::kInfo;
            } else if (arg == "--log-level=warning") {
                options.log_level = LogLevel::kWarning;
            } else if (arg == "--log-level=error") {
                options.log_level = LogLevel::kError;
            } else if (arg == "--log-level=silent") {
                options.log_level = LogLevel::kSilent;
            }
        }

        return options;
    }


    // Records an error message.  Unlike AddID, errors are not subject to
    // per-message-ID overrides — they always appear.  If a LineColumnTracker
    // is provided, the message's source location is resolved; otherwise only
    // the text is stored.
    void Log::AddError(LineColumnTracker* tracker, Range r, const std::string& text) {
        Msg msg;
        msg.kind = MsgKind::kError;
        if (tracker) {
            msg.data = tracker->MakeMsgData(r, text);
        } else {
            msg.data.text = text;
        }
        add_msg(std::move(msg));
    }

    // Records a diagnostic message identified by a MsgID.  The message's
    // effective severity is first checked against the override map: if the
    // override maps to silent (kind == 0) the message is dropped entirely.
    //
    // Input:  id = kJS_BigInt, kind = kWarning, text = "Bigint not supported"
    // Output: message is appended with the overridden kind (or the original)
    void Log::AddID(MsgID id, MsgKind kind, LineColumnTracker* tracker, Range r, const std::string& text) {
        MsgKind overrideKind = AllowOverride(overrides, id, kind);
        if (static_cast<uint8_t>(overrideKind) == 0 && overrides.count(id)) return;
        Msg msg;
        msg.id = id;
        msg.kind = overrideKind;
        if (tracker) {
            msg.data = tracker->MakeMsgData(r, text);
        } else {
            msg.data.text = text;
        }
        add_msg(std::move(msg));
    }

    // Records an error message with associated note messages.  Notes provide
    // supplementary context (e.g. "the original declaration is here") and are
    // rendered indented beneath the primary error in terminal output.
    void Log::AddErrorWithNotes(LineColumnTracker* tracker, Range r, const std::string& text, const std::vector<MsgData>& notes) {
        Msg msg;
        msg.kind = MsgKind::kError;
        msg.notes = notes;
        if (tracker) {
            msg.data = tracker->MakeMsgData(r, text);
        } else {
            msg.data.text = text;
        }
        add_msg(std::move(msg));
    }

    // Records a MsgID-identified diagnostic with associated note messages.
    // Subject to the same override/silence logic as AddID.
    void Log::AddIDWithNotes(MsgID id, MsgKind kind, LineColumnTracker* tracker, Range r, const std::string& text, const std::vector<MsgData>& notes) {
        MsgKind overrideKind = AllowOverride(overrides, id, kind);
        if (static_cast<uint8_t>(overrideKind) == 0 && overrides.count(id)) return;
        Msg msg;
        msg.id = id;
        msg.kind = overrideKind;
        msg.notes = notes;
        if (tracker) {
            msg.data = tracker->MakeMsgData(r, text);
        } else {
            msg.data.text = text;
        }
        add_msg(std::move(msg));
    }

    // Re-applies a MsgID to an existing Msg, updating its id and effective
    // kind via the override map.  Used when re-classifying a message that
    // was originally constructed without an ID.
    void Log::AddMsgID(MsgID id, const Msg& msg) {
        MsgKind overrideKind = AllowOverride(overrides, id, msg.kind);
        if (static_cast<uint8_t>(overrideKind) == 0 && overrides.count(id)) return;
        Msg m = msg;
        m.id = id;
        m.kind = overrideKind;
        add_msg(std::move(m));
    }

    // Builds a human-readable pluralized summary string for a count of items.
    //
    // Input:  prefix = "warning", count = 3, shown = 1, someAreMissing = true
    // Output: "1 of 3 warnings"
    //
    // Input:  prefix = "error", count = 1, shown = 1, someAreMissing = false
    // Output: "1 error"
    //
    // When shown < count, the "N of M" prefix is prepended.
    // When someAreMissing is true and count > 1, "all" is prepended instead.
    std::string Plural(const std::string& prefix, int count, int shown, bool someAreMissing) {
        std::string text;
        if (count == 1) {
            text = std::format("{} {}", count, prefix);
        } else {
            text = std::format("{} {}s", count, prefix);
        }
        if (shown < count) {
            text = std::format("{} of {}", shown, text);
        } else if (someAreMissing && count > 1) {
            text = "all " + text;
        }
        return text;
    }

    // Produces the final one-line summary displayed after all diagnostics,
    // e.g. "3 warnings and 2 errors" or "1 error".
    //
    // Input:  errors = 2, warnings = 3, shownErrors = 2, shownWarnings = 1
    // Output: "1 of 3 warnings and 2 errors"
    std::string ErrorAndWarningSummary(int errors, int warnings, int shownErrors, int shownWarnings) {
        bool someAreMissing = shownWarnings < warnings || shownErrors < errors;
        if (errors == 0) {
            return Plural("warning", warnings, shownWarnings, someAreMissing);
        } else if (warnings == 0) {
            return Plural("error", errors, shownErrors, someAreMissing);
        } else {
            return std::format("{} and {}",
                Plural("warning", warnings, shownWarnings, someAreMissing),
                Plural("error", errors, shownErrors, someAreMissing));
        }
    }


    // Convenience helper: creates a one-shot stderr logger, writes a single
    // message, and finalizes it.  Used for quick diagnostic output outside
    // of a build pipeline (e.g. printing a parse error from a standalone tool).
    void PrintMessageToStderr(const std::vector<std::string>& os_args, const Msg& msg) {
        auto log = NewStderrLog(OutputOptionsForArgs(os_args));
        log.add_msg(msg);
        log.done();
    }

    // Prints a plain-text error message to stderr.
    void PrintErrorToStderr(const std::vector<std::string>& os_args, const std::string& text) {
        Msg msg;
        msg.kind = MsgKind::kError;
        msg.data.text = text;
        PrintMessageToStderr(os_args, msg);
    }

    // Prints an error message with an optional note to stderr.
    // The note appears as a "NOTE:" sub-message beneath the error.
    void PrintErrorWithNoteToStderr(const std::vector<std::string>& os_args, const std::string& text, const std::string& note) {
        Msg msg;
        msg.kind = MsgKind::kError;
        msg.data.text = text;
        if (!note.empty()) {
            MsgData noteData;
            noteData.text = note;
            msg.notes.push_back(std::move(noteData));
        }
        PrintMessageToStderr(os_args, msg);
    }

    // Writes colored text to a file descriptor.  The caller provides a
    // callback that receives a Colors struct (containing ANSI escape sequences)
    // and returns the formatted string.  If color is disabled or the fd is not
    // a terminal, the Colors struct contains empty strings so no escapes appear.
    //
    // Input:  fd = 2 (stderr), use_color = kColorIfTerminal
    // Output: writes colored or plain text to stderr
    void PrintTextWithColor(int fd, UseColor use_color, const std::function<std::string(const Colors&)>& callback) {
        bool useColorEscapes = false;
        switch (use_color) {
            case UseColor::kColorNever:
                useColorEscapes = false;
                break;
            case UseColor::kColorAlways:
                useColorEscapes = kSupportsColorEscapes;
                break;
            case UseColor::kColorIfTerminal: {
                auto info = GetTerminalInfo(fd);
                useColorEscapes = info.use_color_escapes;
                break;
            }
        }

        Colors colors;

        if (useColorEscapes) {
            colors = kTerminalColors;
        }
        WriteStringWithColor(fd, callback(colors));
    }


    // Conditional wrapper around PrintTextWithColor that suppresses output
    // when the current log level is more restrictive than the message's level.
    void PrintText(int fd, LogLevel level, const std::vector<std::string>& os_args, const std::function<std::string(const Colors&)>& callback) {
        auto options = OutputOptionsForArgs(os_args);
        if (options.log_level > level) return;
        PrintTextWithColor(fd, options.color, callback);
    }

    //------------------------------------------------------------------------------
    // Wraps HTTPS URLs in `text` with ANSI underline escape sequences so they
    // appear as clickable links in modern terminal emulators.
    //
    // The algorithm:
    //   1. If underline escapes are empty, return text unchanged.
    //   2. Find each "https://" prefix in the text.
    //   3. Extend the URL to the next space (or end of string).
    //   4. Strip trailing punctuation (.,?! )]}) from the URL boundary.
    //   5. Wrap the URL with underline + reset escape sequences.
    //
    // Example:
    //   Input:  "See https://example.com/docs."
    //   Output: "See \033[4mhttps://example.com/docs\033[0m."
    std::string LinkifyText(std::string_view text, std::string_view underline, std::string_view reset)
    {
        if (underline.empty()) {
            return std::string(text);
        }

        static constexpr std::string_view kHTTPS = "https://";

        auto httpsPos = text.find(kHTTPS);
        if (httpsPos == std::string_view::npos) {
            return std::string(text);
        }

        std::string result;
        std::string_view remaining = text;

        while (true) {
            auto pos = remaining.find(kHTTPS);

            if (pos == std::string_view::npos) {
                break;
            }

            auto end = remaining.find(' ', pos);

            if (end == std::string_view::npos) {
                end = remaining.size();
            }

            if (end > pos) {
                char last = remaining[end - 1];

                if (last == '.' || last == ',' ||
                    last == '?' || last == '!' ||
                    last == ')' || last == ']' ||
                    last == '}') {
                    end--;
                }
            }

            result.append(remaining.substr(0, pos));
            result.append(underline);
            result.append(remaining.substr(pos, end - pos));
            result.append(reset);

            remaining.remove_prefix(end);
        }

        result.append(remaining);

        return result;
    }

    // Returns the display width of a Unicode code point in a terminal.
    // Currently a stub that returns 1 for all characters; a full
    // implementation would handle East Asian ambiguous-width characters.
    int RuneWidth(char32_t r) {
        (void)r;
        return 1;
    }

    // Word-wraps `text` to fit within `width` terminal columns.
    // Splits on spaces; a single word longer than `width` is not broken.
    // Returns a vector of lines, each within the width limit.
    //
    // Input:  text = "the quick brown fox", width = 10
    // Output: ["the quick", "brown fox"]
    //
    // Input:  text = "supercalifragilisticexpialidocious", width = 10
    // Output: ["supercalifragilisticexpialidocious"]
    std::vector<std::string> WrapWordsInString(const std::string& text, int width) {
        std::vector<std::string> runs;

        std::string remaining = text;
        while (!remaining.empty()) {
            size_t i = 0;
            int x = 0;
            size_t wordEndI = 0;

            // Skip leading spaces
            while (i < remaining.size() && remaining[i] == ' ') {
                i++;
                x++;
            }

            // Find how many words fit
            while (i < remaining.size()) {
                size_t oldWordEndI = wordEndI;
                size_t wordStartI = i;

                // Find end of word
                while (i < remaining.size()) {
                    auto [c, w] = helpers::DecodeRuneInString(remaining.substr(i));
                    if (c == U' ') break;
                    i += static_cast<size_t>(w);
                    x += RuneWidth(c);
                }
                wordEndI = i;

                if (wordStartI > 0 && x > width) {
                    runs.push_back(remaining.substr(0, oldWordEndI));
                    remaining = remaining.substr(wordStartI);
                    goto nextLine;
                }

                // Skip spaces after word
                while (i < remaining.size() && remaining[i] == ' ') {
                    i++;
                    x++;
                }
            }

            break;
            nextLine:;
        }

        // Remove trailing spaces
        while (!remaining.empty() && remaining.back() == ' ') {
            remaining.pop_back();
        }
        runs.push_back(remaining);
        return runs;
    }


    // Computes the display width of a UTF-8 string in a terminal, summing
    // the RuneWidth of each code point.  The BOM character (U+FEFF) is
    // excluded from the width calculation.
    //
    // Input:  text = "abc"  (3 ASCII characters)
    // Output: 3
    int EstimateWidthInTerminal(const std::string& text) {
        int width = 0;
        for (size_t i = 0; i < text.size(); ) {
            auto [c, w] = helpers::DecodeRuneInString(text.substr(i));
            i += static_cast<size_t>(w);
            if (c != 0xFEFF) {
                width += RuneWidth(c);
            }
        }
        return width;
    }


    // Replaces tab characters in `with_tabs` with spaces, aligning to the
    // next tab stop at multiples of `spacesPerTab`.  Non-tab characters are
    // passed through unchanged.
    //
    // Input:  with_tabs = "a\tb", spacesPerTab = 2
    // Output: "a b"  (tab fills 1 space to reach column 2)
    //
    // Input:  with_tabs = "ab\tcd", spacesPerTab = 4
    // Output: "ab  cd"  (tab fills 2 spaces to reach column 4)
    std::string RenderTabStops(const std::string& with_tabs, int spacesPerTab) {
        if (with_tabs.find('\t') == std::string::npos) return with_tabs;

        std::string result;
        int count = 0;

        for (size_t i = 0; i < with_tabs.size(); ) {
            auto [c, w] = helpers::DecodeRuneInString(with_tabs.substr(i));
            i += static_cast<size_t>(w);
            if (c == U'\t') {
                int spaces = spacesPerTab - count % spacesPerTab;
                for (int s = 0; s < spaces; s++) {
                    result += ' ';
                    count++;
                }
            } else {
                result += static_cast<char>(c);
                count++;
            }
        }

        return result;
    }


    // Builds the left-hand gutter text for a source line in a diagnostic.
    // Produces a right-justified line number with a "│" separator, surrounded
    // by 6 leading spaces for alignment with the message icon.
    //
    // Input:  maxMargin = 3, line = 42
    // Output: "       42 │ "
    std::string MarginWithLineText(int maxMargin, int line)
    {
        std::string number = std::to_string(line);

        int padding = maxMargin - static_cast<int>(number.size());
        if (padding < 0) {
            padding = 0;
        }

        return "      " +
            std::string(static_cast<size_t>(padding), ' ') +
            number +
            " │ ";
    }

    // Builds an empty gutter line (no line number) for continuation lines
    // in a diagnostic.  Uses "│" for intermediate lines and "╵" for the last
    // line to create a visual boundary beneath the source code.
    std::string EmptyMarginText(int maxMargin, bool isLast)
    {
        std::string space(static_cast<size_t>(maxMargin), ' ');

        if (isLast) {
            return "      " + space + " ╵ ";
        }

        return "      " + space + " │ ";
    }

    // Returns true if string `s` begins with `prefix`.
    bool HasPrefix(const std::string& s, const std::string& prefix) {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    // Returns true if string `s` ends with `suffix`.
    bool HasSuffix(const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    // Builds the visual layout for a single diagnostic's source location.
    // This is the core function that determines how the source code snippet,
    // the caret marker (^), and any suggestion/fix are displayed.
    //
    // The function handles:
    //   - Tab stop rendering (tabs are expanded to spaces)
    //   - Long-line truncation with "..." ellipsis when the source line
    //     exceeds the terminal width
    //   - Marker length: a single caret "^" for zero-width locations,
    //     tildes "~~~" for multi-character ranges
    //   - Suggestion text (an optional fix-it displayed beneath the marker)
    //
    // Input:  data with line_text = "  const x = 1;", column = 10, length = 1
    // Output: MsgDetail with source_before = "  const x ", source_marked = "=",
    //         source_after = " 1;", marker = "^", indent = "          "
    MsgDetail DetailStruct(const MsgData& data, PathStyle path_style, const TerminalInfo& terminal_info, int max_margin) {
        auto loc = *data.location;
        auto endOfFirstLine = static_cast<int>(loc.line_text.size());
        auto newlinePos = loc.line_text.find('\n');
        if (newlinePos != std::string::npos) {
            endOfFirstLine = static_cast<int>(newlinePos);
        }

        std::string firstLine = loc.line_text.substr(0, static_cast<size_t>(endOfFirstLine));
        std::string afterFirstLine = loc.line_text.substr(static_cast<size_t>(endOfFirstLine));
        if (!afterFirstLine.empty() && !HasSuffix(afterFirstLine, "\n")) {
            afterFirstLine += "\n";
        }

        if (loc.line < 0) loc.line = 0;
        if (loc.column < 0) loc.column = 0;
        if (loc.length < 0) loc.length = 0;
        if (loc.column > endOfFirstLine) loc.column = endOfFirstLine;
        if (loc.length > endOfFirstLine - loc.column) loc.length = endOfFirstLine - loc.column;

        int spacesPerTab = 2;
        std::string lineText = RenderTabStops(firstLine, spacesPerTab);
        std::string textUpToLoc = RenderTabStops(firstLine.substr(0, static_cast<size_t>(loc.column)), spacesPerTab);
        int markerStart = static_cast<int>(textUpToLoc.size());
        int markerEnd = markerStart;
        std::string indent(static_cast<size_t>(EstimateWidthInTerminal(textUpToLoc)), ' ');
        std::string marker = "^";

        if (loc.length > 0) {
            markerEnd = static_cast<int>(RenderTabStops(
                firstLine.substr(0, static_cast<size_t>(loc.column + loc.length)), spacesPerTab).size());
        }

        if (markerStart > static_cast<int>(lineText.size())) markerStart = static_cast<int>(lineText.size());
        if (markerEnd > static_cast<int>(lineText.size())) markerEnd = static_cast<int>(lineText.size());
        if (markerEnd < markerStart) markerEnd = markerStart;

        int width = terminal_info.width;
        if (width < 1) width = kDefaultTerminalWidth;
        width -= max_margin + kExtraMarginChars;
        if (width < 1) width = 1;

        if (loc.column == endOfFirstLine) {
            width -= 1;
        }

        if (static_cast<int>(lineText.size()) > width) {
            int sliceStart = (markerStart + markerEnd - width) / 2;
            if (sliceStart > markerStart - width / 5) {
                sliceStart = markerStart - width / 5;
            }
            if (sliceStart < 0) sliceStart = 0;
            if (sliceStart > static_cast<int>(lineText.size()) - width) {
                sliceStart = static_cast<int>(lineText.size()) - width;
            }
            int sliceEnd = sliceStart + width;

            std::string slicedLine = lineText.substr(static_cast<size_t>(sliceStart), static_cast<size_t>(width));
            markerStart -= sliceStart;
            markerEnd -= sliceStart;
            if (markerStart < 0) markerStart = 0;
            if (markerEnd > static_cast<int>(slicedLine.size())) markerEnd = static_cast<int>(slicedLine.size());

            if (static_cast<int>(slicedLine.size()) > 3 && sliceStart > 0) {
                slicedLine = "..." + slicedLine.substr(3);
                if (markerStart < 3) markerStart = 3;
            }
            if (static_cast<int>(slicedLine.size()) > 3 && sliceEnd < static_cast<int>(lineText.size())) {
                slicedLine = slicedLine.substr(0, slicedLine.size() - 3) + "...";
                if (markerEnd > static_cast<int>(slicedLine.size()) - 3) {
                    markerEnd = static_cast<int>(slicedLine.size()) - 3;
                }
                if (markerEnd < markerStart) markerEnd = markerStart;
            }

            lineText = slicedLine;
            indent = std::string(static_cast<size_t>(EstimateWidthInTerminal(lineText.substr(0, static_cast<size_t>(markerStart)))), ' ');
        }

        if (markerEnd - markerStart > 1) {
            marker = std::string(static_cast<size_t>(EstimateWidthInTerminal(lineText.substr(static_cast<size_t>(markerStart), static_cast<size_t>(markerEnd - markerStart)))), '~');
        }

        auto margin = MarginWithLineText(max_margin, loc.line);

        return MsgDetail{
            .source_before = margin + lineText.substr(0, static_cast<size_t>(markerStart)),
            .source_marked = lineText.substr(static_cast<size_t>(markerStart), static_cast<size_t>(markerEnd - markerStart)),
            .source_after = lineText.substr(static_cast<size_t>(markerEnd)),
            .indent = indent,
            .marker = marker,
            .suggestion = loc.suggestion,
            .content_after = afterFirstLine,
            .path = loc.file.Select(path_style),
            .line = loc.line,
            .column = loc.column,
        };
    }

    // Formats a single diagnostic message as a complete, ready-to-print string.
    //
    // The output format varies based on whether source location information
    // is available:
    //
    //   With source:  "✘ error: text\n    path:line:col:\n    source_line\n    ^^^ marker\n"
    //   Without source: "✘ error: text\n"
    //
    // Note messages are indented with "  " and rendered without the icon prefix.
    // Plugin names are shown in magenta brackets after the message text.
    // Message IDs are shown in brackets at the end, e.g. "[css-syntax-error]".
    //
    // Colors are applied only when terminal_info.use_color_escapes is true.
    std::string MsgString(bool include_source, PathStyle path_style, const TerminalInfo& terminal_info, MsgID id, MsgKind kind, const MsgData& data, const std::string& plugin_name) {
        if (!include_source) {
            if (data.location) {
                return std::format("{}: {}: {}\n",
                    data.location->file.Select(path_style), MsgKindToString(kind), data.text);
            }
            return std::format("{}: {}\n", MsgKindToString(kind), data.text);
        }

        Colors colors{};
        if (terminal_info.use_color_escapes) {
            colors = kTerminalColors;
        }

        // Note handling
        if (kind == MsgKind::kNote) {
            std::string noteText;
            std::istringstream stream(data.text);
            std::string line;
            while (std::getline(stream, line)) {
                if (terminal_info.width > 2) {
                    int wrapWidth = terminal_info.width;
                    if (!data.disable_maximum_width && wrapWidth > 100) {
                        wrapWidth = 100;
                    }
                    for (const auto& run : WrapWordsInString(line, wrapWidth - 2)) {
                        noteText += "  ";
                        noteText += LinkifyText(run, colors.underline, colors.reset);
                        noteText += '\n';
                    }
                    continue;
                }
                noteText += "  ";
                noteText += LinkifyText(line, colors.underline, colors.reset);
                noteText += '\n';
            }

            if (data.location) {
                auto detail = DetailStruct(data, path_style, terminal_info, 0);
                if (!detail.suggestion.empty()) {
                    noteText += std::format("\n    {}:{}:{}:\n{}{}{}{}{}{}\n{}{}{}{}{}\n{}{}{}{}{}\n{}",
                        detail.path, detail.line, detail.column,
                        colors.dim, detail.source_before, colors.green, detail.source_marked, colors.dim, detail.source_after,
                        EmptyMarginText(0, false), detail.indent, colors.green, detail.marker, colors.dim,
                        EmptyMarginText(0, true), detail.indent, colors.green, detail.suggestion, colors.reset,
                        detail.content_after);
                } else {
                    noteText += std::format("\n    {}:{}:{}:\n{}{}{}{}{}{}\n{}{}{}{}{}\n{}",
                        detail.path, detail.line, detail.column,
                        colors.dim, detail.source_before, colors.green, detail.source_marked, colors.dim, detail.source_after,
                        EmptyMarginText(0, true), detail.indent, colors.green, detail.marker, colors.reset,
                        detail.content_after);
                }
            }
            return noteText;
        }

        std::string iconColor;
        std::string kindColorBrackets;
        std::string kindColorText;

        switch (kind) {
            case MsgKind::kVerbose:
                iconColor = colors.cyan;
                kindColorBrackets = colors.cyan_bg_cyan;
                kindColorText = colors.cyan_bg_black;
                break;
            case MsgKind::kDebug:
                iconColor = colors.green;
                kindColorBrackets = colors.green_bg_green;
                kindColorText = colors.green_bg_white;
                break;
            case MsgKind::kInfo:
                iconColor = colors.blue;
                kindColorBrackets = colors.blue_bg_blue;
                kindColorText = colors.blue_bg_white;
                break;
            case MsgKind::kError:
                iconColor = colors.red;
                kindColorBrackets = colors.red_bg_red;
                kindColorText = colors.red_bg_white;
                break;
            case MsgKind::kWarning:
                iconColor = colors.yellow;
                kindColorBrackets = colors.yellow_bg_yellow;
                kindColorText = colors.yellow_bg_black;
                break;
            default: break;
        }

        std::string location;
        if (data.location) {
            int maxMargin = static_cast<int>(std::to_string(data.location->line).size());
            auto d = DetailStruct(data, path_style, terminal_info, maxMargin);

            if (!d.suggestion.empty()) {
                location = std::format("\n    {}:{}:{}:\n{}{}{}{}{}{}\n{}{}{}{}{}\n{}{}{}{}{}\n{}",
                    d.path, d.line, d.column,
                    colors.dim, d.source_before, colors.green, d.source_marked, colors.dim, d.source_after,
                    EmptyMarginText(maxMargin, false), d.indent, colors.green, d.marker, colors.dim,
                    EmptyMarginText(maxMargin, true), d.indent, colors.green, d.suggestion, colors.reset,
                    d.content_after);
            } else {
                location = std::format("\n    {}:{}:{}:\n{}{}{}{}{}{}\n{}{}{}{}{}\n{}",
                    d.path, d.line, d.column,
                    colors.dim, d.source_before, colors.green, d.source_marked, colors.dim, d.source_after,
                    EmptyMarginText(maxMargin, true), d.indent, colors.green, d.marker, colors.reset,
                    d.content_after);
            }
        }

        std::string pluginName;
        if (!plugin_name.empty()) {
            pluginName = std::format(" {}[{}plugin {}]{}", colors.bold, colors.magenta, plugin_name, colors.reset);
        }

        std::string_view msgIDStr = MsgIDToString(id);
        std::string msgIDSuffix;
        if (!msgIDStr.empty()) {
            msgIDSuffix = std::format(" [{}]", msgIDStr);
        }

        return std::format("{}{} {}[{}{}]{} {}{}{}{}{}{}\n{}",
            iconColor, MsgKindToIcon(kind),
            kindColorBrackets, kindColorText, MsgKindToString(kind), kindColorBrackets, colors.reset,
            colors.bold, data.text, colors.reset, pluginName, msgIDSuffix,
            location);
    }

    // Formats this message and all its notes into a single string.
    //
    // Each note is rendered as its own "NOTE" sub-message.  When source
    // location info is present, a blank line is inserted between consecutive
    // notes to improve readability.
    //
    // Input:  a Msg with kind=kError, text="undeclared variable", 2 notes
    // Output: "✘ error: undeclared variable\n  NOTE: did you mean 'x'?\n  NOTE: declared here\n"
    std::string Msg::String(const OutputOptions& options, const TerminalInfo& terminal_info) const {
        std::string text = MsgString(options.include_source, options.path_style, terminal_info, id, kind, data, plugin_name);

        // Append notes the same way esbuild's "msgToStringDefault" does: each note
        // is formatted as its own message with the "NOTE" kind.
        MsgData old_data;
        for (size_t i = 0; i < notes.size(); i++) {
            if (options.include_source && (i == 0 || old_data.text.find('\n') != std::string::npos || old_data.location != nullptr)) {
                text += "\n";
            }
            text += MsgString(options.include_source, options.path_style, terminal_info, MsgID::kNone, MsgKind::kNote, notes[i], "");
            old_data = notes[i];
        }

        // Add extra spacing between messages if source code is present
        if (options.include_source) {
            text += "\n";
        }
        return text;
    }


    //------------------------------------------------------------------------------
    // Creates a stderr-based logger that writes diagnostics in real time.
    //
    // This is the primary logger used during build processes.  It provides:
    //
    //   - Thread-safe message collection via std::mutex
    //   - Real-time output: errors and info-level messages are written to
    //     stderr immediately; warnings may be deferred to preserve screen
    //     space for errors that follow
    //   - Message count limiting: when --log-limit=N is set, at most N
    //     messages are shown; deferred warnings fill the remaining quota
    //   - Stable sorting: on done(), messages are sorted by file, line,
    //     column, kind, then text for deterministic output
    //   - Summary line: when errors or warnings exist, a trailing line
    //     like "3 warnings and 1 error" is printed
    //
    // Input:  OutputOptions with log_level = kWarning, message_limit = 20
    // Output: Log whose add_msg writes warnings/errors to stderr in real time
    Log NewStderrLog(const OutputOptions& options)
    {
        auto    state = std::make_shared<StderrLogState>();
        state-> options = options;
        state-> terminal_info = GetTerminalInfo(2);
        state-> remainingMessagesBeforeLimit = options.message_limit > 0 ? options.message_limit : 0x7FFFFFFF;

        if (options.color == UseColor::kColorNever) {
            state->terminal_info.use_color_escapes = false;
        } else if (options.color == UseColor::kColorAlways) {
            state->terminal_info.use_color_escapes = kSupportsColorEscapes;
        }

            auto finalizeLog = [state]() {
            for (auto it = state->deferredWarnings.begin();
                state->remainingMessagesBeforeLimit > 0 && it != state->deferredWarnings.end();) {
                state->shownWarnings++;
                WriteStringWithColor(2, it->String(state->options, state->terminal_info));
                it = state->deferredWarnings.erase(it);
                state->remainingMessagesBeforeLimit--;
            }

            int totalErrors = state->errors;
            int totalWarnings = state->warnings;
            if ((state->options.message_limit > 0 && totalErrors + totalWarnings > state->options.message_limit) ||
                (state->options.log_level <= LogLevel::kInfo && (totalWarnings != 0 || totalErrors != 0))) {
                if (state->options.message_limit > 0 && totalErrors + totalWarnings > state->options.message_limit) {
                    WriteStringWithColor(2, std::format("{} shown (disable the message limit with --log-limit=0)\n",
                        ErrorAndWarningSummary(totalErrors, totalWarnings, state->shownErrors, state->shownWarnings)));
                } else if (state->options.log_level <= LogLevel::kInfo && (totalWarnings != 0 || totalErrors != 0)) {
                    WriteStringWithColor(2, std::format("{}\n",
                        ErrorAndWarningSummary(totalErrors, totalWarnings, state->shownErrors, state->shownWarnings)));
                }
            }
        };

        Log log;
        log.level = options.log_level;
        log.overrides = options.overrides;

        log.add_msg = [state, finalizeLog](Msg msg) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->msgs.push_back(msg);

            switch (msg.kind) {
                case MsgKind::kVerbose:
                    if (state->options.log_level <= LogLevel::kVerbose) {
                        WriteStringWithColor(2, msg.String(state->options, state->terminal_info));
                    }
                    break;
                case MsgKind::kDebug:
                    if (state->options.log_level <= LogLevel::kDebug) {
                        WriteStringWithColor(2, msg.String(state->options, state->terminal_info));
                    }
                    break;
                case MsgKind::kInfo:
                    if (state->options.log_level <= LogLevel::kInfo) {
                        WriteStringWithColor(2, msg.String(state->options, state->terminal_info));
                    }
                    break;
                case MsgKind::kError:
                    state->hasErrors = true;
                    if (state->options.log_level <= LogLevel::kError) {
                        state->errors++;
                    }
                    break;
                case MsgKind::kWarning:
                    if (state->options.log_level <= LogLevel::kWarning) {
                        state->warnings++;
                    }
                    break;
                default: break;
            }

            if (state->remainingMessagesBeforeLimit == 0) return;

            switch (msg.kind) {
                case MsgKind::kError:
                    if (state->options.log_level <= LogLevel::kError) {
                        state->shownErrors++;
                        WriteStringWithColor(2, msg.String(state->options, state->terminal_info));
                        state->remainingMessagesBeforeLimit--;
                    }
                    break;
                case MsgKind::kWarning:
                    if (state->options.log_level <= LogLevel::kWarning) {
                        if (state->remainingMessagesBeforeLimit > (state->options.message_limit + 1) / 2) {
                            state->shownWarnings++;
                            WriteStringWithColor(2, msg.String(state->options, state->terminal_info));
                            state->remainingMessagesBeforeLimit--;
                        } else {
                            state->deferredWarnings.push_back(msg);
                        }
                    }
                    break;
                default: break;
            }
        };

        log.has_errors = [state]() -> bool {
            std::lock_guard<std::mutex> lock(state->mutex);
            return state->hasErrors;
        };

        log.peek = [state]() -> std::vector<Msg> {
            std::lock_guard<std::mutex> lock(state->mutex);
            std::stable_sort(state->msgs.begin(), state->msgs.end(),
                [](const Msg& a, const Msg& b) {
                    if (!a.data.location && !b.data.location) return false;
                    if (!a.data.location) return true;
                    if (!b.data.location) return false;
                    auto& ai = *a.data.location;
                    auto& bj = *b.data.location;
                    if (ai.file.abs != bj.file.abs) {
                        return ai.file.abs < bj.file.abs ||
                            (ai.file.abs == bj.file.abs && ai.file.rel < bj.file.rel);
                    }
                    if (ai.line != bj.line) return ai.line < bj.line;
                    if (ai.column != bj.column) return ai.column < bj.column;
                    if (a.kind != b.kind) return a.kind < b.kind;
                    return a.data.text < b.data.text;
                });
            return state->msgs;
        };

        log.done = [state, finalizeLog]() -> std::vector<Msg> {
            std::lock_guard<std::mutex> lock(state->mutex);
            finalizeLog();
            std::stable_sort(state->msgs.begin(), state->msgs.end(),
                [](const Msg& a, const Msg& b) {
                    if (!a.data.location && !b.data.location) return false;
                    if (!a.data.location) return true;
                    if (!b.data.location) return false;
                    auto& ai = *a.data.location;
                    auto& bj = *b.data.location;
                    if (ai.file.abs != bj.file.abs) {
                        return ai.file.abs < bj.file.abs ||
                            (ai.file.abs == bj.file.abs && ai.file.rel < bj.file.rel);
                    }
                    if (ai.line != bj.line) return ai.line < bj.line;
                    if (ai.column != bj.column) return ai.column < bj.column;
                    if (a.kind != b.kind) return a.kind < b.kind;
                    return a.data.text < b.data.text;
                });
            return std::move(state->msgs);
        };

        return log;
    }

    // Creates a deferred logger that collects messages in memory without
    // writing to stderr.  Messages are returned in sorted order when done()
    // is called.
    //
    // Two modes are supported via DeferLogKind:
    //   - kDeferLogAll: stores all messages including verbose/debug
    //   - kDeferLogNoVerboseOrDebug: silently drops verbose and debug messages
    //
    // This logger is used for sub-operations (e.g. parsing a CSS file within
    // a larger bundle) where output should be grouped and sorted with other
    // diagnostics rather than interleaved in real time.
    Log NewDeferLog(DeferLogKind kind, const std::unordered_map<MsgID, LogLevel>& overrides) {
        auto state = std::make_shared<DeferLogState>();

        Log log;
        log.level = LogLevel::kInfo;
        log.overrides = overrides;

        log.add_msg = [state, kind](Msg msg) {
            if (kind == DeferLogKind::kDeferLogNoVerboseOrDebug &&
                (msg.kind == MsgKind::kVerbose || msg.kind == MsgKind::kDebug)) {
                return;
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            if (msg.kind == MsgKind::kError) {
                state->hasErrors = true;
            }
            state->msgs.push_back(std::move(msg));
        };

        log.has_errors = [state]() -> bool {
            std::lock_guard<std::mutex> lock(state->mutex);
            return state->hasErrors;
        };

        log.peek = [state]() -> std::vector<Msg> {
            std::lock_guard<std::mutex> lock(state->mutex);
            return state->msgs;
        };

        log.done = [state]() -> std::vector<Msg> {
            std::lock_guard<std::mutex> lock(state->mutex);
            std::stable_sort(state->msgs.begin(), state->msgs.end(),
                [](const Msg& a, const Msg& b) {
                    if (!a.data.location && !b.data.location) return false;
                    if (!a.data.location) return true;
                    if (!b.data.location) return false;
                    auto& ai = *a.data.location;
                    auto& bj = *b.data.location;
                    if (ai.file.abs != bj.file.abs) {
                        return ai.file.abs < bj.file.abs ||
                            (ai.file.abs == bj.file.abs && ai.file.rel < bj.file.rel);
                    }
                    if (ai.line != bj.line) return ai.line < bj.line;
                    if (ai.column != bj.column) return ai.column < bj.column;
                    if (a.kind != b.kind) return a.kind < b.kind;
                    return a.data.text < b.data.text;
                });
            return std::move(state->msgs);
        };

        return log;
    }


    // Constructs a LineColumnTracker from a Source.  The tracker maintains
    // an internal cursor (offset_, line_) that advances as the caller scans
    // through the file, enabling O(n) total time to resolve many locations
    // in the same file.
    //
    // If source is null, the tracker is in "no-source" mode and
    // MsgLocationOrNil() will always return nullptr.
    LineColumnTracker::LineColumnTracker(const Source* source) {
        if (source) {
            contents_ = source->contents;
            pretty_paths_ = source->pretty_paths;
            has_line_start_ = true;
            has_source_ = true;
        }
    }

    // Advances (or rewinds) the internal cursor to the given byte offset,
    // updating line count and line-start/end state along the way.
    //
    // Forward scan: counts '\n', '\r', '\u2028', '\u2029' as line terminators.
    // Handles "\r\n" as a single terminator (not two).
    // Backward scan: decrements the line counter for each terminator found.
    //
    // The tracker caches line_start_ (first byte of the current line) and
    // line_end_ (first byte of the next line) so that ComputeLineAndColumn
    // can determine the column without re-scanning the entire line.
    void LineColumnTracker::ScanTo(int32_t offset) {
        auto& contents = contents_;
        int32_t i = offset_;

        if (i < offset) {
            while (true) {
                auto [r, w] = helpers::DecodeRuneInString(contents.substr(static_cast<size_t>(i)));
                if (w == 0) break;
                i += static_cast<int32_t>(w);

                if (r == U'\n') {
                    has_line_start_ = true;
                    has_line_end_ = false;
                    line_start_ = i;
                    if (i == static_cast<int32_t>(w) || contents[static_cast<size_t>(i) - static_cast<size_t>(w) - 1] != '\r') {
                        line_++;
                    }
                } else if (r == U'\r' || r == U'\u2028' || r == U'\u2029') {
                    has_line_start_ = true;
                    has_line_end_ = false;
                    line_start_ = i;
                    line_++;
                }

                if (i >= offset) {
                    offset_ = i;
                    return;
                }
            }
        }

        if (i > offset) {
            while (true) {
                auto [r, w] = helpers::DecodeLastRuneInString(std::string_view(contents).substr(0, static_cast<size_t>(i)));
                if (w == 0) break;
                i -= static_cast<int32_t>(w);

                if (r == U'\n') {
                    has_line_start_ = false;
                    has_line_end_ = true;
                    line_end_ = i;
                    if (i == 0 || contents[static_cast<size_t>(i) - 1] != '\r') {
                        line_--;
                    }
                } else if (r == U'\r' || r == U'\u2028' || r == U'\u2029') {
                    has_line_start_ = false;
                    has_line_end_ = true;
                    line_end_ = i;
                    line_--;
                }

                if (i <= offset) {
                    offset_ = i;
                    return;
                }
            }
        }
    }

    // Resolves a byte offset into a 1-based line number, 0-based column,
    // and the byte range of the containing line.
    //
    // First scans to the target offset, then walks backward to find the
    // line start and forward to find the line end.  The column is computed
    // as (offset - line_start) in byte units.
    //
    // Input:  offset = 42
    // Output: line_count = 5 (0-based), column_count = 10, line_start = 32, line_end = 60
    void LineColumnTracker::ComputeLineAndColumn(int offset, int32_t& line_count, int32_t& column_count,
                                                int32_t& line_start, int32_t& line_end) {
        ScanTo(static_cast<int32_t>(offset));

        if (!has_line_start_) {
            int32_t i = offset_;
            while (i > 0) {
                auto [r, w] = helpers::DecodeLastRuneInString(std::string_view(contents_).substr(0, static_cast<size_t>(i)));
                if (w == 0) break;
                if (r == U'\n' || r == U'\r' || r == U'\u2028' || r == U'\u2029') break;
                i -= static_cast<int32_t>(w);
            }
            has_line_start_ = true;
            line_start_ = i;
        }

        if (!has_line_end_) {
            auto& contents = contents_;
            int32_t i = offset_;
            int32_t n = static_cast<int32_t>(contents.size());
            while (i < n) {
                auto [r, w] = helpers::DecodeRuneInString(contents.substr(static_cast<size_t>(i)));
                if (w == 0) break;
                if (r == U'\n' || r == U'\r' || r == U'\u2028' || r == U'\u2029') break;
                i += static_cast<int32_t>(w);
            }
            has_line_end_ = true;
            line_end_ = i;
        }

        line_count = line_;
        column_count = offset - static_cast<int>(line_start_);
        line_start = static_cast<int>(line_start_);
        line_end = static_cast<int>(line_end_);
    }

    // Creates a MsgData by resolving the Range into a full MsgLocation
    // (with file, line, column, and line_text).  Returns the MsgData
    // ready for inclusion in a Msg.
    MsgData LineColumnTracker::MakeMsgData(Range r, const std::string& text) {
        MsgData data;
        data.text = text;
        data.location = MsgLocationOrNil(r);
        return data;
    }

    // Resolves a Range into a MsgLocation struct containing file paths,
    // 1-based line/column, the range length, and the raw line text for
    // the source context display.
    //
    // Returns nullptr if the tracker has no source (null source in constructor).
    //
    // Input:  Range{Loc{42}, 5} in a file with line "  const x = 1;" starting at byte 32
    // Output: MsgLocation{file=..., line=1, column=10, length=5, line_text="  const x = 1;"}
    std::shared_ptr<MsgLocation> LineColumnTracker::MsgLocationOrNil(Range r) {
        if (!has_source_) return nullptr;

        int32_t lineCount, columnCount, lineStart, lineEnd;
        ComputeLineAndColumn(static_cast<int>(r.loc.start), lineCount, columnCount, lineStart, lineEnd);

        auto loc = std::make_shared<MsgLocation>();
        loc->file = pretty_paths_;
        loc->line = static_cast<int>(lineCount + 1);
        loc->column = static_cast<int>(columnCount);
        loc->length = static_cast<int>(r.len);
        loc->line_text = contents_.substr(static_cast<size_t>(lineStart), static_cast<size_t>(lineEnd - lineStart));
        return loc;
    }

    // Prints the build summary table to stderr, listing output files sorted
    // by size (largest first) with optional size warnings for large bundles.
    //
    // The table is truncated to at most half the terminal height.  If there
    // are more entries than fit, "...and N more output files..." is appended.
    //
    // Files exceeding kSizeWarningThreshold bytes are highlighted in yellow
    // with a warning icon.
    //
    // If elapsed_ms is non-null, a "Done in Xms" line is appended.
    void PrintSummary(UseColor use_color, std::vector<SummaryTableEntry>& table, const double* elapsed_ms) {
        PrintTextWithColor(2, use_color, [&](const Colors& colors) -> std::string {
            bool isWinCmd = IsProbablyWindowsCommandPrompt();
            std::string sb;

            if (!table.empty()) {
                TerminalInfo info = GetTerminalInfo(2);

                int maxLength = info.height / 2;
                if (info.height == 0) maxLength = 20;
                else if (maxLength < 5) maxLength = 5;

                int length = static_cast<int>(table.size());

                std::sort(table.begin(), table.end(),
                    [](const SummaryTableEntry& a, const SummaryTableEntry& b) {
                        if (!a.is_source_map && b.is_source_map) return true;
                        if (a.is_source_map && !b.is_source_map) return false;
                        if (a.bytes > b.bytes) return true;
                        if (a.bytes < b.bytes) return false;
                        if (a.dir < b.dir) return true;
                        if (a.dir > b.dir) return false;
                        return a.base < b.base;
                    });

                if (length > maxLength) {
                    table.resize(static_cast<size_t>(maxLength));
                }

                int spacingBetweenColumns = 2;
                bool hasSizeWarning = false;
                int maxPath = 0;
                int maxSize = 0;
                for (const auto& entry : table) {
                    int path = static_cast<int>(entry.dir.size() + entry.base.size());
                    int size = static_cast<int>(entry.size.size()) + spacingBetweenColumns;
                    if (path > maxPath) maxPath = path;
                    if (size > maxSize) maxSize = size;
                    if (!entry.is_source_map && entry.bytes >= kSizeWarningThreshold) {
                        hasSizeWarning = true;
                    }
                }

                std::string margin = "  ";
                int layoutWidth = info.width;
                if (layoutWidth < 1) layoutWidth = kDefaultTerminalWidth;
                layoutWidth -= 2 * static_cast<int>(margin.size());
                if (hasSizeWarning) layoutWidth -= 2;
                if (layoutWidth > maxPath + maxSize) layoutWidth = maxPath + maxSize;

                sb += '\n';

                for (const auto& entry : table) {
                    std::string dir = entry.dir;
                    std::string base = entry.base;
                    int pathWidth = layoutWidth - maxSize;

                    if (static_cast<int>(dir.size() + base.size()) > pathWidth) {
                        if (!dir.empty()) {
                            int n = pathWidth - static_cast<int>(base.size()) - 3;
                            if (n < 1) n = 1;
                            dir = "..." + dir.substr(dir.size() - static_cast<size_t>(n));
                        }
                        if (static_cast<int>(dir.size() + base.size()) > pathWidth) {
                            int n = pathWidth - static_cast<int>(dir.size()) - 3;
                            if (n < 0) n = 0;
                            base = base.substr(0, static_cast<size_t>(n)) + "...";
                        }
                    }

                    int spacer = layoutWidth - static_cast<int>(entry.size.size() + dir.size() + base.size());
                    if (spacer < 0) spacer = 0;

                    std::string_view sizeColor = colors.cyan;
                    std::string sizeWarning;
                    if (!entry.is_source_map && entry.bytes >= kSizeWarningThreshold) {
                        sizeColor = colors.yellow;
                        if (!isWinCmd) {
                            sizeWarning = " \xe2\x9a\xa0\xef\xb8\x8f";
                        }
                    }

                    sb += std::format("{}{}{}{}{}{}{}{}{}{}{}{}\n",
                        margin,
                        colors.dim, dir, colors.reset,
                        colors.bold, base, colors.reset,
                        std::string(static_cast<size_t>(spacer), ' '),
                        sizeColor, entry.size, sizeWarning, colors.reset);
                }

                if (length > maxLength) {
                    std::string plural_s = (length == maxLength + 1) ? "" : "s";
                    sb += std::format("{}{}...and {} more output file{}...{}\n",
                        margin, colors.dim, length - maxLength, plural_s, colors.reset);
                }
            }

            sb += '\n';

            std::string lightningSymbol = "\xe2\x9a\xa1 ";
            if (isWinCmd) lightningSymbol = "";

            if (elapsed_ms) {
                int ms = static_cast<int>(*elapsed_ms);
                sb += std::format("{}{}Done in {}ms{}\n",
                    lightningSymbol, colors.green, ms, colors.reset);
            }

            return sb;
        });
    }


    // String-in-JS table functions


    // Builds a character-by-character mapping table between the inner content
    // of a string literal (after escape processing) and the outer source
    // positions in the original source file.
    //
    // This is used to remap diagnostics that reference positions within a
    // string's evaluated content back to the exact byte offset in the
    // original source, accounting for escape sequences like \n, \x41,
    // \u{1F600}, and line continuations.
    //
    // Input:  outer_contents = "hello\\nworld", outer_string_literal_loc = Loc{0}
    //         inner_contents = "hello\nworld"
    // Output: table mapping each inner byte to its outer Loc
    std::vector<StringInJSTableEntry> GenerateStringInJSTable(
        const std::string& outer_contents, Loc outer_string_literal_loc,
        const std::string& inner_contents) {
        std::vector<StringInJSTableEntry> table;
        int32_t i = 0;
        int32_t n = static_cast<int32_t>(inner_contents.size());
        int32_t line = 1;
        int32_t column = 0;
        Loc loc{static_cast<int32_t>(outer_string_literal_loc.start + 1)};

        while (i < n) {
            for (;;) {
                if (auto [c, w] = helpers::DecodeRuneInString(outer_contents.substr(static_cast<size_t>(loc.start))); c != U'\\') break;
                auto [c, w] = helpers::DecodeRuneInString(outer_contents.substr(static_cast<size_t>(loc.start + 1)));
                if (c == U'\n' || c == U'\r' || c == U'\u2028' || c == U'\u2029') {
                    loc.start += 1 + static_cast<int32_t>(w);
                    if (c == U'\r' && outer_contents[static_cast<size_t>(loc.start)] == '\n') {
                        loc.start++;
                    }
                    continue;
                }
                break;
            }

            auto [c, w] = helpers::DecodeRuneInString(inner_contents.substr(static_cast<size_t>(i)));

            table.push_back({line, column, Loc{i}, loc});
            if (table.size() > 1) {
                auto& last = table[table.size() - 2];
                if (line == last.inner_line && loc.start - column == last.outer_loc.start - last.inner_column) {
                    table.pop_back();
                }
            }

            switch (c) {
                case U'\n':
                case U'\r':
                case U'\u2028':
                case U'\u2029':
                    line++;
                    column = 0;
                    if (c == U'\r' && i + 1 < n && inner_contents[static_cast<size_t>(i + 1)] == '\n') {
                        i++;
                    }
                    break;
                default:
                    column += static_cast<int32_t>(w);
                    break;
            }
            i += static_cast<int32_t>(w);

            auto [oc, ow] = helpers::DecodeRuneInString(outer_contents.substr(static_cast<size_t>(loc.start)));
            if (oc == U'\r' && outer_contents[static_cast<size_t>(loc.start + 1)] == '\n') {
                loc.start += 2;
            } else if (oc != U'\\') {
                loc.start += static_cast<int32_t>(ow);
            } else {
                auto [ec, ew] = helpers::DecodeRuneInString(outer_contents.substr(static_cast<size_t>(loc.start + 1)));
                switch (ec) {
                    case U'x': loc.start += 1 + 2; break;
                    case U'u':
                        loc.start++;
                        if (outer_contents[static_cast<size_t>(loc.start)] == '{') {
                            loc.start++;
                            while (outer_contents[static_cast<size_t>(loc.start)] != '}') loc.start++;
                            loc.start++;
                        } else {
                            loc.start += 4;
                        }
                        break;
                    case U'\n':
                    case U'\r':
                    case U'\u2028':
                    case U'\u2029':
                        break;
                    default:
                        loc.start += 1 + static_cast<int32_t>(ew);
                        break;
                }
            }
        }

        return table;
    }

    // Maps an inner string content location to its corresponding outer source
    // location using the table produced by GenerateStringInJSTable.
    //
    // Uses binary search for O(log n) lookup.
    //
    // Input:  table from GenerateStringInJSTable, inner_loc = Loc{5}
    // Output: Loc{11}  (the byte offset in the original source)
    Loc RemapStringInJSLoc(const std::vector<StringInJSTableEntry>& table, Loc inner_loc) {
        int count = static_cast<int>(table.size());
        int index = 0;

        while (count > 0) {
            int step = count / 2;
            int i = index + step;
            if (i + 1 < static_cast<int>(table.size())) {
                if (table[static_cast<size_t>(i + 1)].inner_loc.start < inner_loc.start) {
                    index = i + 1;
                    count -= step + 1;
                    continue;
                }
            }
            count = step;
        }

        auto entry = table[static_cast<size_t>(index)];
        entry.outer_loc.start += inner_loc.start - entry.inner_loc.start;
        return entry.outer_loc;
    }

    // Wraps an existing Log so that any message whose location falls within
    // a string literal's inner content is remapped to the outer source position.
    //
    // This is used when a CSS-in-JS or template literal parser reports
    // diagnostics using positions within the string's evaluated text.
    // This logger transparently translates those positions back to the
    // original source file so the user sees correct line:column references.
    Log NewStringInJSLog(Log log, LineColumnTracker& outer_tracker, const std::vector<StringInJSTableEntry>& table) {
        auto oldAddMsg = log.add_msg;

        auto remapLineAndColumnToLoc = [&](int32_t line, int32_t column) -> Loc {
            int count = static_cast<int>(table.size());
            int index = 0;

            while (count > 0) {
                int step = count / 2;
                int i = index + step;
                if (i + 1 < static_cast<int>(table.size())) {
                    const auto& entry = table[static_cast<size_t>(i + 1)];
                    if (entry.inner_line < line || (entry.inner_line == line && entry.inner_column < column)) {
                        index = i + 1;
                        count -= step + 1;
                        continue;
                    }
                }
                count = step;
            }

            auto entry = table[static_cast<size_t>(index)];
            entry.outer_loc.start += column - entry.inner_column;
            return entry.outer_loc;
        };

        auto remapData = [&](const MsgData& data) -> MsgData {
            if (!data.location) return data;

            Range r;
            r.loc = remapLineAndColumnToLoc(static_cast<int32_t>(data.location->line),
                                            static_cast<int32_t>(data.location->column));
            if (data.location->length != 0) {
                r.len = remapLineAndColumnToLoc(static_cast<int32_t>(data.location->line),
                                                static_cast<int32_t>(data.location->column + data.location->length)).start
                        - r.loc.start;
            }

            MsgData result = data;
            result.location = outer_tracker.MakeMsgData(r, data.text).location;
            if (result.location) {
                result.location->suggestion = data.location->suggestion;
            }
            return result;
        };

        log.add_msg = [oldAddMsg, remapData](Msg msg) {
            msg.data = remapData(msg.data);
            for (auto& note : msg.notes) {
                note = remapData(note);
            }
            oldAddMsg(msg);
        };

        return log;
    }

}






