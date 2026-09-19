#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <cctype>

#include "guchho/helpers.hpp"
#include "guchho/logger.hpp"
#include "guchho/compiler.hpp"
#include "guchho/sourcemap.hpp"
#include "guchho/javascript/js_ast.hpp"
#include "guchho/javascript/js_parser.hpp"



namespace guchho::javascript {

namespace {

// Comparator for source map mappings used during stable sorting. When
// columns within the same generated line are out of order (which can happen
// with section-based source maps or certain generator edge cases), this
// comparator ensures mappings are reordered by generated line first, then
// by generated column. The "less or equal" semantics (<= rather than <)
// preserve the original insertion order of equal mappings, which is
// required for std::stable_sort to maintain a stable result.
//
// Example: Given mappings with the same generated_line but columns [5, 3, 7],
// sorting with MappingLess produces [3, 5, 7].
struct MappingLess {
    bool operator()(const sourcemap::Mapping& a, const sourcemap::Mapping& b) const {
        return a.generated_line < b.generated_line ||
               (a.generated_line == b.generated_line && a.generated_column <= b.generated_column);
    }
};

// Determines whether a character is valid within a URL scheme component
// according to RFC 3986. Schemes must start with an alpha character and
// can contain alphanumerics plus '+', '-', or '.'. The ':' delimiter
// and path/query/fragment characters are excluded.
//
// Example: IsURLSchemeChar('h') => true, IsURLSchemeChar('/') => false
bool IsURLSchemeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
}

// Calculates the length of the URL scheme prefix in the given URL string.
// The scheme starts at position 0 and extends up to (but not including)
// the first ':' delimiter. Returns std::string::npos if no valid scheme
// is found — this happens when the string is empty, doesn't start with
// an alphabetic character, or contains '/', '?', '#' before any ':'.
//
// Example: URLSchemeLength("https://example.com") => 5
//          URLSchemeLength("relative/path")       => npos
size_t URLSchemeLength(std::string_view url) {
    if (url.empty() || !std::isalpha(static_cast<unsigned char>(url[0]))) {
        return std::string::npos;
    }

    for (size_t i = 1; i < url.size(); i++) {
        char c = url[i];
        if (c == ':') {
            return i;
        }
        if (c == '/' || c == '?' || c == '#' || !IsURLSchemeChar(c)) {
            break;
        }
    }

    return std::string::npos;
}

// Holds the result of parsing a URL into its standard components as
// defined by RFC 3986. All fields are string_views pointing into the
// original URL string — no copies are made. The has_authority flag
// indicates whether the URL contained "//" after the scheme, which
// distinguishes authority-bearing URLs (like "https://host/path") from
// scheme-relative URLs (like "file:///path").
struct ParsedURL {
    std::string_view scheme;     // e.g. "https" in "https://example.com/path?q=1#frag"
    std::string_view authority;  // e.g. "example.com" in "https://example.com/path"
    std::string_view path;       // e.g. "/path" in "https://example.com/path?q=1"
    std::string_view suffix;     // e.g. "?q=1#frag" — everything after the path
    bool has_authority{};        // true if "//" was present after the scheme
};

// Decomposes a URL string into its component parts following RFC 3986
// structure: [scheme:][//authority][path][?query][#fragment]. If no scheme
// is present, scheme is left empty. If no "//" follows the scheme (or if
// there is no scheme), has_authority is false and authority is empty.
//
// Example:
//   ParseURL("https://example.com/path?q=1#frag")
//     => scheme="https", authority="example.com", path="/path",
//       suffix="?q=1#frag", has_authority=true
//
//   ParseURL("relative/path")
//     => scheme="", authority="", path="relative/path",
//       suffix="", has_authority=false
//
//   ParseURL("file:///src/main.js")
//     => scheme="file", authority="", path="/src/main.js",
//       suffix="", has_authority=true
ParsedURL ParseURL(std::string_view url) {
    ParsedURL result;
    size_t current = 0;

    if (size_t scheme_len = URLSchemeLength(url); scheme_len != std::string::npos) {
        result.scheme = url.substr(0, scheme_len);
        current = scheme_len + 1;
    }

    if (url.substr(current).starts_with("//")) {
        result.has_authority = true;
        current += 2;
        size_t authority_start = current;
        while (current < url.size() && url[current] != '/' && url[current] != '?' && url[current] != '#') {
            current++;
        }
        result.authority = url.substr(authority_start, current - authority_start);
    }

    size_t path_start = current;
    while (current < url.size() && url[current] != '?' && url[current] != '#') {
        current++;
    }
    result.path = url.substr(path_start, current - path_start);
    result.suffix = url.substr(current);
    return result;
}

// Normalizes a URL path by resolving "." (current directory) and ".."
// (parent directory) segments. The `absolute` parameter controls behavior
// when ".." would go above the root: if true, excess ".." segments are
// silently dropped (as with absolute paths); if false, they are kept in
// the result (as with relative paths). Empty segments and "." segments
// are removed. A trailing slash in the input is preserved in the output.
//
// Example:
//   NormalizeURLPath("/a/b/../c", true)   => "/a/c"
//   NormalizeURLPath("/a/b/../c", false)  => "/a/c"
//   NormalizeURLPath("/a/../../c", true)  => "/c"      (excess .. dropped)
//   NormalizeURLPath("/a/../../c", false) => "../c"    (excess .. kept)
//   NormalizeURLPath("a/./b", true)       => "/a/b"
//   NormalizeURLPath("a/", true)          => "/a/"
std::string NormalizeURLPath(std::string_view path, bool absolute) {
    std::vector<std::string_view> parts;
    parts.reserve(8);

    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string_view::npos) {
            end = path.size();
        }

        std::string_view part = path.substr(start, end - start);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!parts.empty() && parts.back() != "..") {
                    parts.pop_back();
                } else if (!absolute) {
                    parts.push_back(part);
                }
            } else {
                parts.push_back(part);
            }
        }

        if (end == path.size()) {
            break;
        }
        start = end + 1;
    }

    bool trailing_slash = !path.empty() && path.back() == '/';
    std::string result;
    size_t capacity = absolute ? 1 : 0;
    for (std::string_view part : parts) {
        capacity += part.size() + 1;
    }
    result.reserve(capacity);

    if (absolute) {
        result.push_back('/');
    }

    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) {
            result.push_back('/');
        }
        result.append(parts[i]);
    }

    if (trailing_slash && (result.empty() || result.back() != '/')) {
        result.push_back('/');
    }

    return result;
}

// Resolves a relative source URL reference against a base URL, following
// the RFC 3986 resolution algorithm. The base URL provides context (scheme,
// authority, directory path) that the reference inherits when it is relative.
//
// Resolution rules:
//   1. If the reference has its own scheme, it is returned as-is (after
//      normalizing the path) — the base is ignored.
//   2. If the reference has an authority ("//host/path"), it inherits only
//      the scheme from the base.
//   3. If the reference is a pure path:
//      - An absolute reference (starts with "/") replaces the base path.
//      - A relative reference is merged with the base's directory path.
//   4. The merged path is normalized to resolve "." and ".." segments.
//
// Example:
//   ResolveSourceURL("https://cdn.example.com/a/b/c.js", "../d.js")
//     => "https://cdn.example.com/a/d.js"
//
//   ResolveSourceURL("file:///src/lib/utils.js", "types.ts")
//     => "file:///src/lib/types.ts"
//
//   ResolveSourceURL("/base/path/file.js", "/other/file.js")
//     => "/other/file.js"
std::string ResolveSourceURL(std::string_view base, std::string_view ref) {
    ParsedURL ref_url = ParseURL(ref);

    if (!ref_url.scheme.empty()) {
        if (!ref_url.has_authority && !ref_url.path.starts_with("/")) {
            return std::string(ref);
        }

        std::string result;
        result.reserve(ref.size());
        result.append(ref_url.scheme);
        result.push_back(':');
        if (ref_url.has_authority) {
            result.append("//");
            result.append(ref_url.authority);
        }
        result.append(NormalizeURLPath(ref_url.path, ref_url.path.starts_with("/")));
        result.append(ref_url.suffix);
        return result;
    }

    ParsedURL base_url = ParseURL(base);
    std::string result;
    result.reserve(base.size() + ref.size() + 1);

    if (!base_url.scheme.empty()) {
        result.append(base_url.scheme);
        result.push_back(':');
    }

    if (ref_url.has_authority) {
        result.append("//");
        result.append(ref_url.authority);
        result.append(NormalizeURLPath(ref_url.path, ref_url.path.starts_with("/")));
        result.append(ref_url.suffix);
        return result;
    }

    if (base_url.has_authority) {
        result.append("//");
        result.append(base_url.authority);
    }

    std::string path;
    if (ref_url.path.starts_with("/")) {
        path = std::string(ref_url.path);
    } else {
        size_t slash = base_url.path.find_last_of('/');
        if (slash != std::string_view::npos) {
            path.assign(base_url.path.substr(0, slash + 1));
        }
        path.append(ref_url.path);
    }

    result.append(NormalizeURLPath(path, path.starts_with("/")));
    result.append(ref_url.suffix);
    return result;
}

} // namespace

// Parses a source map JSON document into a SourceMapData structure.
// Source maps can be either simple (a single top-level object with
// "version", "sources", "mappings", etc.) or sectioned (a top-level
// object with a "sections" array, where each section has an "offset"
// and its own nested source map). This function handles both forms.
//
// The returned SourceMapData contains:
//   - sources: resolved source file paths
//   - sources_content: inline source file contents (if provided)
//   - mappings: decoded VLQ-encoded source location mappings
//   - names: original identifier names referenced by mappings
//
// Returns nullptr on any parse error, schema mismatch, or if the
// resulting source map would be empty (no sources or no mappings).
//
// Example input: {"version":3,"sources":["foo.js"],"mappings":"AAAA"}
// Example output: SourceMapData with one source "foo.js" and one mapping
//                 at generated line 1, column 0 => source foo.js line 1, col 0
std::unique_ptr<sourcemap::SourceMapData> ParseSourceMap(logger::Log log, logger::Source source) {
    JSONOptions options;
    options.error_suffix = " in source map";
    auto [expr, ok] = Parser::ParseJSON(log, source, options);
    if (!ok) {
        return nullptr;
    }

    logger::LineColumnTracker tracker(&source);
    auto* obj_ptr = std::get_if<std::shared_ptr<EObject>>(&expr.data);
    if (obj_ptr == nullptr) {
        log.AddError(&tracker, logger::Range{expr.loc, 0},
            guchho::logger::FormatMsg(guchho::logger::MsgCat::kSourceMap_Invalid));
        return nullptr;
    }
    EObject* obj = obj_ptr->get();

    struct SourceMapSection {
        int32_t line_offset{};
        int32_t column_offset{};
        EObject* source_map{};
    };

    std::vector<SourceMapSection> sections;
    bool has_sections = false;

    for (const Property& prop : obj->properties) {
        auto* key_ptr = std::get_if<std::shared_ptr<EString>>(&prop.key.data);
        if (key_ptr == nullptr || !helpers::UTF16EqualsString((*key_ptr)->value, "sections")) {
            continue;
        }

        if (auto* value_ptr = std::get_if<std::shared_ptr<EArray>>(&prop.value_or_nil.data); value_ptr != nullptr) {
            for (const Expr& item : (*value_ptr)->items) {
                if (auto* element_ptr = std::get_if<std::shared_ptr<EObject>>(&item.data); element_ptr != nullptr) {
                    EObject* element = element_ptr->get();
                    int32_t section_line_offset = 0;
                    int32_t section_column_offset = 0;
                    EObject* section_source_map = nullptr;

                    for (const Property& section_prop : element->properties) {
                        auto* section_key_ptr = std::get_if<std::shared_ptr<EString>>(&section_prop.key.data);
                        if (section_key_ptr == nullptr) {
                            continue;
                        }
                        std::string key_text = helpers::UTF16ToString((*section_key_ptr)->value);

                        if (key_text == "offset") {
                            if (auto* offset_value_ptr = std::get_if<std::shared_ptr<EObject>>(&section_prop.value_or_nil.data); offset_value_ptr != nullptr) {
                                EObject* offset_value = offset_value_ptr->get();
                                for (const Property& offset_prop : offset_value->properties) {
                                    auto* offset_key_ptr = std::get_if<std::shared_ptr<EString>>(&offset_prop.key.data);
                                    if (offset_key_ptr == nullptr) {
                                        continue;
                                    }
                                    std::string offset_key_text = helpers::UTF16ToString((*offset_key_ptr)->value);

                                    if (offset_key_text == "line") {
                                        if (auto* line_value_ptr = std::get_if<std::shared_ptr<ENumber>>(&offset_prop.value_or_nil.data); line_value_ptr != nullptr) {
                                            section_line_offset = static_cast<int32_t>((*line_value_ptr)->value);
                                        }
                                    } else if (offset_key_text == "column") {
                                        if (auto* column_value_ptr = std::get_if<std::shared_ptr<ENumber>>(&offset_prop.value_or_nil.data); column_value_ptr != nullptr) {
                                            section_column_offset = static_cast<int32_t>((*column_value_ptr)->value);
                                        }
                                    }
                                }
                            } else {
                                log.AddError(&tracker, logger::Range{section_prop.value_or_nil.loc, 0},
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kSourceMap_ExpectedOffsetObject));
                                return nullptr;
                            }
                        } else if (key_text == "map") {
                            if (auto* map_value_ptr = std::get_if<std::shared_ptr<EObject>>(&section_prop.value_or_nil.data); map_value_ptr != nullptr) {
                                section_source_map = map_value_ptr->get();
                            } else {
                                log.AddError(&tracker, logger::Range{section_prop.value_or_nil.loc, 0},
                                    guchho::logger::FormatMsg(guchho::logger::MsgCat::kSourceMap_ExpectedMapObject));
                                return nullptr;
                            }
                        }
                    }

                    if (section_source_map != nullptr) {
                        sections.push_back(SourceMapSection{
                            .line_offset = section_line_offset,
                            .column_offset = section_column_offset,
                            .source_map = section_source_map,
                        });
                    }
                }
            }
        } else {
            log.AddError(&tracker, logger::Range{prop.value_or_nil.loc, 0},
                guchho::logger::FormatMsg(guchho::logger::MsgCat::kSourceMap_ExpectedSectionsArray));
            return nullptr;
        }

        has_sections = true;
        break;
    }

    if (!has_sections) {
        sections.push_back(SourceMapSection{
            .line_offset = 0,
            .column_offset = 0,
            .source_map = obj,
        });
    }

    std::unique_ptr<sourcemap::SourceMapData> result = std::make_unique<sourcemap::SourceMapData>();
    std::vector<std::string>& sources = result->sources;
    std::vector<sourcemap::SourceContent>& sources_content = result->sources_content;
    std::vector<sourcemap::Mapping>& mappings = result->mappings;
    std::vector<std::string>& names = result->names;
    int32_t generated_line = 0;
    int32_t generated_column = 0;
    bool need_sort = false;

    for (const SourceMapSection& section : sections) {
        std::vector<Expr> sources_array;
        std::vector<Expr> sources_content_array;
        std::vector<Expr> names_array;
        std::vector<char16_t> mappings_raw;
        int32_t mappings_start = 0;
        std::string source_root;
        bool has_version = false;

        for (const Property& prop : section.source_map->properties) {
            auto* key_ptr = std::get_if<std::shared_ptr<EString>>(&prop.key.data);
            if (key_ptr == nullptr) {
                continue;
            }
            std::string key_text = helpers::UTF16ToString((*key_ptr)->value);

            if (key_text == "version") {
                if (auto* value_ptr = std::get_if<std::shared_ptr<ENumber>>(&prop.value_or_nil.data); value_ptr != nullptr && (*value_ptr)->value == 3) {
                    has_version = true;
                }
            } else if (key_text == "mappings") {
                if (auto* value_ptr = std::get_if<std::shared_ptr<EString>>(&prop.value_or_nil.data); value_ptr != nullptr) {
                    mappings_raw.assign((*value_ptr)->value.begin(), (*value_ptr)->value.end());
                    mappings_start = prop.value_or_nil.loc.start + 1;
                }
            } else if (key_text == "sourceRoot") {
                if (auto* value_ptr = std::get_if<std::shared_ptr<EString>>(&prop.value_or_nil.data); value_ptr != nullptr) {
                    source_root = helpers::UTF16ToString((*value_ptr)->value);
                }
            } else if (key_text == "sources") {
                if (auto* value_ptr = std::get_if<std::shared_ptr<EArray>>(&prop.value_or_nil.data); value_ptr != nullptr) {
                    sources_array = (*value_ptr)->items;
                }
            } else if (key_text == "sourcesContent") {
                if (auto* value_ptr = std::get_if<std::shared_ptr<EArray>>(&prop.value_or_nil.data); value_ptr != nullptr) {
                    sources_content_array = (*value_ptr)->items;
                }
            } else if (key_text == "names") {
                if (auto* value_ptr = std::get_if<std::shared_ptr<EArray>>(&prop.value_or_nil.data); value_ptr != nullptr) {
                    names_array = (*value_ptr)->items;
                }
            }
        }

        // Skip sections that lack a valid version 3 declaration.
        // We only support version 3 source maps per the spec.
        if (!has_version) {
            continue;
        }

        size_t mappings_len = mappings_raw.size();
        size_t sources_len = sources_array.size();
        size_t names_len = names_array.size();

        // Skip sections that have no mappings or no source files —
        // such sections contribute nothing to the final source map.
        if (mappings_len == 0 || sources_len == 0) {
            continue;
        }

        if (section.line_offset < generated_line ||
            (section.line_offset == generated_line && section.column_offset < generated_column)) {
            need_sort = true;
        }

        int32_t line_offset = section.line_offset;
        int32_t column_offset = section.column_offset;
        int32_t source_offset = static_cast<int32_t>(sources.size());
        int32_t name_offset = static_cast<int32_t>(names.size());

        generated_line = line_offset;
        generated_column = column_offset;
        int32_t source_index = source_offset;
        int32_t original_line = 0;
        int32_t original_column = 0;
        int32_t original_name = name_offset;

        size_t current = 0;
        std::string error_text;
        int error_len = 0;

        // Decode the VLQ-encoded mappings string. Each mapping entry
        // consists of 1, 4, or 5 variable-length fields separated by
        // commas (';') indicate new lines in the generated output.
        while (current < mappings_len) {
            // A semicolon advances to the next generated line.
            // Reset column to 0 for the new line.
            if (mappings_raw[current] == u';') {
                generated_line++;
                generated_column = 0;
                current++;
                continue;
            }

            // Decode the first VLQ field: the generated column delta.
            // This is always relative to the previous mapping's generated
            // column on the same line (or 0 at the start of a new line).
            auto [generated_column_delta, i, decoded_ok] = sourcemap::DecodeVLQUTF16(
                std::span<const char16_t>(mappings_raw).subspan(current));
            if (!decoded_ok) {
                error_text = "Missing generated column";
                error_len = i;
                break;
            }
            if (generated_column_delta < 0) {
                // Negative column deltas mean mappings are not in sorted order
                // within this line, which breaks binary search assumptions.
                need_sort = true;
            }
            generated_column += generated_column_delta;
            if ((generated_line == line_offset && generated_column < column_offset) || generated_column < 0) {
                error_text = "Invalid generated column value: " + std::to_string(generated_column);
                error_len = i;
                break;
            }
            current += static_cast<size_t>(i);

            // The source map spec allows mappings with 1, 4, or 5 VLQ fields.
            // A 1-field mapping has only the generated column (no original
            // location info), which is useless for debugging. Skip it and
            // move to the next mapping.
            if (current == mappings_len) {
                break;
            }
            if (mappings_raw[current] == u',') {
                current++;
                continue;
            }
            if (mappings_raw[current] == u';') {
                continue;
            }

            // Decode the second VLQ field: source index delta. This identifies
            // which source file the original location refers to, relative to
            // the current section's source offset.
            auto [source_index_delta, si, source_ok] = sourcemap::DecodeVLQUTF16(
                std::span<const char16_t>(mappings_raw).subspan(current));
            if (!source_ok) {
                error_text = "Missing source index";
                error_len = si;
                break;
            }
            source_index += source_index_delta;
            if (source_index < source_offset || source_index >= source_offset + static_cast<int32_t>(sources_len)) {
                error_text = "Invalid source index value: " + std::to_string(source_index);
                error_len = si;
                break;
            }
            current += static_cast<size_t>(si);

            // Decode the third VLQ field: original line delta. This is the
            // line number in the source file, relative to the current state.
            auto [original_line_delta, li, line_ok] = sourcemap::DecodeVLQUTF16(
                std::span<const char16_t>(mappings_raw).subspan(current));
            if (!line_ok) {
                error_text = "Missing original line";
                error_len = li;
                break;
            }
            original_line += original_line_delta;
            if (original_line < 0) {
                error_text = "Invalid original line value: " + std::to_string(original_line);
                error_len = li;
                break;
            }
            current += static_cast<size_t>(li);

            // Decode the fourth VLQ field: original column delta. This is the
            // column number within the source line.
            auto [original_column_delta, ci, col_ok] = sourcemap::DecodeVLQUTF16(
                std::span<const char16_t>(mappings_raw).subspan(current));
            if (!col_ok) {
                error_text = "Missing original column";
                error_len = ci;
                break;
            }
            original_column += original_column_delta;
            if (original_column < 0) {
                error_text = "Invalid original column value: " + std::to_string(original_column);
                error_len = ci;
                break;
            }
            current += static_cast<size_t>(ci);

            // Decode the optional fifth VLQ field: original name index delta.
            // This field is only present if the mapping starts at the
            // beginning of a named identifier. If the decode fails (not
            // enough fields remain), the mapping has no associated name.
            compiler::Index32 optional_name{};
            auto [name_delta, ni, name_ok] = sourcemap::DecodeVLQUTF16(
                std::span<const char16_t>(mappings_raw).subspan(current));
            if (name_ok) {
                original_name += name_delta;
                if (original_name < name_offset || original_name >= name_offset + static_cast<int32_t>(names_len)) {
                    error_text = "Invalid name index value: " + std::to_string(original_name);
                    error_len = ni;
                    break;
                }
                optional_name = compiler::Index32::Make(static_cast<uint32_t>(original_name));
                current += static_cast<size_t>(ni);
            }

            // After a complete mapping entry, expect either a comma (another
            // mapping on the same line), a semicolon (next line), or end of
            // input. Anything else is a syntax error.
            if (current < mappings_len) {
                char16_t c = mappings_raw[current];
                if (c == u',') {
                    current++;
                } else if (c != u';') {
                    error_text = "Invalid character after mapping: " +
                                 helpers::quoteString(helpers::UTF16ToString(
                                     std::span<const char16_t>(mappings_raw).subspan(current, 1)));
                    error_len = 1;
                    break;
                }
            }

            mappings.push_back(sourcemap::Mapping{
                .generated_line = generated_line,
                .generated_column = generated_column,
                .source_index = source_index,
                .original_line = original_line,
                .original_column = original_column,
                .original_name = optional_name,
            });
        }

        if (!error_text.empty()) {
            logger::Range r{logger::Loc{mappings_start + static_cast<int32_t>(current)}, error_len};
            log.AddID(logger::MsgID::kSourceMap_InvalidSourceMappings, logger::MsgKind::kWarning, &tracker, r,
                      guchho::logger::FormatMsg(guchho::logger::MsgCat::kSourceMap_BadMappings,
                          std::to_string(current), error_text));
            return nullptr;
        }

        for (const Expr& item : sources_array) {
            if (auto* element_ptr = std::get_if<std::shared_ptr<EString>>(&item.data); element_ptr != nullptr) {
                std::string source_url = helpers::UTF16ToString((*element_ptr)->value);
                sources.push_back(source_root.empty() ? source_url : ResolveSourceURL(source_root, source_url));
            } else {
                sources.push_back("");
            }
        }

        if (!sources_content_array.empty()) {
            // The "sources" and "sourcesContent" arrays must stay in sync
            // by index. If a previous section had a shorter sourcesContent
            // array than its sources array (or none at all), pad our
            // aggregated sources_content with empty entries up to the
            // current section's source_offset so indices stay aligned.
            while (sources_content.size() < static_cast<size_t>(source_offset)) {
                sources_content.push_back(sourcemap::SourceContent{});
            }

            for (size_t i = 0; i < sources_content_array.size(); i++) {
                // Cap at the number of sources to keep indices aligned.
                // sources and sourcesContent are separate arrays in the JSON
                // and can have different lengths; we must not let
                // sourcesContent grow past sources.
                if (i == sources_len) {
                    break;
                }

                const Expr& item = sources_content_array[i];
                if (auto* element_ptr = std::get_if<std::shared_ptr<EString>>(&item.data); element_ptr != nullptr) {
                    sources_content.push_back(sourcemap::SourceContent{
                        .quoted = source.TextForRange(source.RangeOfString(item.loc)),
                        .value = (*element_ptr)->value,
                    });
                } else {
                    sources_content.push_back(sourcemap::SourceContent{});
                }
            }
        }

        for (const Expr& item : names_array) {
            if (auto* element_ptr = std::get_if<std::shared_ptr<EString>>(&item.data); element_ptr != nullptr) {
                names.push_back(helpers::UTF16ToString((*element_ptr)->value));
            } else {
                names.push_back("");
            }
        }
    }

    // A source map with no sources or no mappings is not useful.
    if (sources.empty() || mappings.empty()) {
        return nullptr;
    }

    if (need_sort) {
        // Some mappings had columns out of order within a line. This can
        // happen with section-based source maps or certain generator
        // edge cases. Lines are always in order by construction, but
        // columns may not be. Use a stable sort to preserve insertion
        // order of equal mappings.
        std::stable_sort(mappings.begin(), mappings.end(), MappingLess{});
    }

    return result;
}

}
