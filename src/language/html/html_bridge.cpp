#include "guchho/html/html_bridge.hpp"
#include "guchho/logger.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "guchho/html/html_lexer.hpp"
#include "guchho/html/html_parser.hpp"
#include "guchho/html/html_helpers.hpp"

namespace guchho::html {

namespace {

// Converts code-unit offsets from the tokenizer's UTF-16 view of the input
// back into byte offsets in the original UTF-8 source, which is what the
// logger's Loc/Range types and the bundler's rewrite pass consume. The map
// is built by walking the source with the same UTF-8 decoding rules that
// ToUtf16() applies, so the two index spaces stay in lockstep: each decoded
// code point appends its starting byte offset, and a surrogate pair (a code
// point at or above U+10000) appends its end offset as well so a range that
// stops in the middle of a pair still closes with a positive length. A final
// sentinel records the byte just past the last code unit for out-of-range
// lookups. ByteOffset clamps any invalid requested offset, so the map never
// indexes out of bounds.
class Utf16OffsetMap {
public:
    static Utf16OffsetMap Build(std::string_view text) {
        Utf16OffsetMap map;
        map.text_size_ = static_cast<int32_t>(text.size());

        const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
        const size_t size = text.size();

        for (size_t i = 0; i < size;) {
            const int32_t start = static_cast<int32_t>(i);
            uint32_t cp = bytes[i];

            if (cp < 0x80) {
                i += 1;
            } else if ((cp & 0xe0) == 0xc0) {
                cp = ((cp & 0x1f) << 6) | (bytes[i + 1] & 0x3fu);
                i += 2;
            } else if ((cp & 0xf0) == 0xe0) {
                cp = ((cp & 0x0fu) << 12) | ((bytes[i + 1] & 0x3fu) << 6) | (bytes[i + 2] & 0x3fu);
                i += 3;
            } else {
                cp = ((cp & 0x07u) << 18) | ((bytes[i + 1] & 0x3fu) << 12) | ((bytes[i + 2] & 0x3fu) << 6) |
                     (bytes[i + 3] & 0x3fu);
                i += 4;
            }

            if (cp >= 0x10000) {
                map.offsets_.push_back(start);
                map.offsets_.push_back(static_cast<int32_t>(i));
            } else {
                map.offsets_.push_back(start);
            }
        }

        map.offsets_.push_back(map.text_size_);
        return map;
    }

    int32_t ByteOffset(int32_t utf16_offset) const {
        if (utf16_offset < 0) {
            return 0;
        }
        if (static_cast<size_t>(utf16_offset) >= offsets_.size()) {
            return text_size_;
        }
        return offsets_[static_cast<size_t>(utf16_offset)];
    }

private:
    std::vector<int32_t> offsets_;
    int32_t text_size_ = 0;
};

}  // namespace

// Renders one tokenizer parse-error code as a concrete diagnostic message.
// Every interesting code maps to a dedicated catalog template through
// logger::FormatMsg; anything without a template of its own falls back to the
// generic HTML parse-error message, so unknown or future codes still produce
// something reportable rather than crashing the logger.
std::string ErrDescription(Err code) {
    switch (code) {
        case Err::kControlCharacterInInputStream:
            return logger::FormatMsg(logger::MsgCat::kHTML_ControlCharacterInInputStream);
        case Err::kNoncharacterInInputStream:
            return logger::FormatMsg(logger::MsgCat::kHTML_NoncharacterInInputStream);
        case Err::kSurrogateInInputStream:
            return logger::FormatMsg(logger::MsgCat::kHTML_SurrogateInInputStream);
        case Err::kNonVoidHtmlElementStartTagWithTrailingSolidus:
            return logger::FormatMsg(logger::MsgCat::kHTML_NonVoidHtmlElementStartTagWithTrailingSolidus);
        case Err::kEndTagWithAttributes:
            return logger::FormatMsg(logger::MsgCat::kHTML_EndTagWithAttributes);
        case Err::kEndTagWithTrailingSolidus:
            return logger::FormatMsg(logger::MsgCat::kHTML_EndTagWithTrailingSolidus);
        case Err::kUnexpectedSolidusInTag:
            return logger::FormatMsg(logger::MsgCat::kHTML_UnexpectedSolidusInTag);
        case Err::kUnexpectedNullCharacter:
            return logger::FormatMsg(logger::MsgCat::kHTML_UnexpectedNullCharacter);
        case Err::kUnexpectedQuestionMarkInsteadOfTagName:
            return logger::FormatMsg(logger::MsgCat::kHTML_UnexpectedQuestionMarkInsteadOfTagName);
        case Err::kInvalidFirstCharacterOfTagName:
            return logger::FormatMsg(logger::MsgCat::kHTML_InvalidFirstCharacterOfTagName);
        case Err::kUnexpectedEqualsSignBeforeAttributeName:
            return logger::FormatMsg(logger::MsgCat::kHTML_UnexpectedEqualsSignBeforeAttributeName);
        case Err::kMissingEndTagName:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingEndTagName);
        case Err::kUnexpectedCharacterInAttributeName:
            return logger::FormatMsg(logger::MsgCat::kHTML_UnexpectedCharacterInAttributeName);
        case Err::kUnknownNamedCharacterReference:
            return logger::FormatMsg(logger::MsgCat::kHTML_UnknownNamedCharacterReference);
        case Err::kMissingSemicolonAfterCharacterReference:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingSemicolonAfterCharacterReference);
        case Err::kUnexpectedCharacterAfterDoctypeSystemIdentifier:
            return logger::FormatMsg(logger::MsgCat::kHTML_UnexpectedCharacterAfterDoctypeSystemIdentifier);
        case Err::kUnexpectedCharacterInUnquotedAttributeValue:
            return logger::FormatMsg(logger::MsgCat::kHTML_UnexpectedCharacterInUnquotedAttributeValue);
        case Err::kEofBeforeTagName:
            return logger::FormatMsg(logger::MsgCat::kHTML_EofBeforeTagName);
        case Err::kEofInTag:
            return logger::FormatMsg(logger::MsgCat::kHTML_EofInTag);
        case Err::kMissingAttributeValue:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingAttributeValue);
        case Err::kMissingWhitespaceBetweenAttributes:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingWhitespaceBetweenAttributes);
        case Err::kMissingWhitespaceAfterDoctypePublicKeyword:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingWhitespaceAfterDoctypePublicKeyword);
        case Err::kMissingWhitespaceBetweenDoctypePublicAndSystemIdentifiers:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingWhitespaceBetweenDoctypePublicAndSystemIdentifiers);
        case Err::kMissingWhitespaceAfterDoctypeSystemKeyword:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingWhitespaceAfterDoctypeSystemKeyword);
        case Err::kMissingQuoteBeforeDoctypePublicIdentifier:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingQuoteBeforeDoctypePublicIdentifier);
        case Err::kMissingQuoteBeforeDoctypeSystemIdentifier:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingQuoteBeforeDoctypeSystemIdentifier);
        case Err::kMissingDoctypePublicIdentifier:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingDoctypePublicIdentifier);
        case Err::kMissingDoctypeSystemIdentifier:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingDoctypeSystemIdentifier);
        case Err::kAbruptDoctypePublicIdentifier:
            return logger::FormatMsg(logger::MsgCat::kHTML_AbruptDoctypePublicIdentifier);
        case Err::kAbruptDoctypeSystemIdentifier:
            return logger::FormatMsg(logger::MsgCat::kHTML_AbruptDoctypeSystemIdentifier);
        case Err::kCdataInHtmlContent:
            return logger::FormatMsg(logger::MsgCat::kHTML_CdataInHtmlContent);
        case Err::kIncorrectlyOpenedComment:
            return logger::FormatMsg(logger::MsgCat::kHTML_IncorrectlyOpenedComment);
        case Err::kEofInScriptHtmlCommentLikeText:
            return logger::FormatMsg(logger::MsgCat::kHTML_EofInScriptHtmlCommentLikeText);
        case Err::kEofInDoctype:
            return logger::FormatMsg(logger::MsgCat::kHTML_EofInDoctype);
        case Err::kNestedComment:
            return logger::FormatMsg(logger::MsgCat::kHTML_NestedComment);
        case Err::kAbruptClosingOfEmptyComment:
            return logger::FormatMsg(logger::MsgCat::kHTML_AbruptClosingOfEmptyComment);
        case Err::kEofInComment:
            return logger::FormatMsg(logger::MsgCat::kHTML_EofInComment);
        case Err::kIncorrectlyClosedComment:
            return logger::FormatMsg(logger::MsgCat::kHTML_IncorrectlyClosedComment);
        case Err::kEofInCdata:
            return logger::FormatMsg(logger::MsgCat::kHTML_EofInCdata);
        case Err::kAbsenceOfDigitsInNumericCharacterReference:
            return logger::FormatMsg(logger::MsgCat::kHTML_AbsenceOfDigitsInNumericCharacterReference);
        case Err::kNullCharacterReference:
            return logger::FormatMsg(logger::MsgCat::kHTML_NullCharacterReference);
        case Err::kSurrogateCharacterReference:
            return logger::FormatMsg(logger::MsgCat::kHTML_SurrogateCharacterReference);
        case Err::kCharacterReferenceOutsideUnicodeRange:
            return logger::FormatMsg(logger::MsgCat::kHTML_CharacterReferenceOutsideUnicodeRange);
        case Err::kControlCharacterReference:
            return logger::FormatMsg(logger::MsgCat::kHTML_ControlCharacterReference);
        case Err::kNoncharacterCharacterReference:
            return logger::FormatMsg(logger::MsgCat::kHTML_NoncharacterCharacterReference);
        case Err::kMissingWhitespaceBeforeDoctypeName:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingWhitespaceBeforeDoctypeName);
        case Err::kMissingDoctypeName:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingDoctypeName);
        case Err::kInvalidCharacterSequenceAfterDoctypeName:
            return logger::FormatMsg(logger::MsgCat::kHTML_InvalidCharacterSequenceAfterDoctypeName);
        case Err::kDuplicateAttribute:
            return logger::FormatMsg(logger::MsgCat::kHTML_DuplicateAttribute);
        case Err::kNonConformingDoctype:
            return logger::FormatMsg(logger::MsgCat::kHTML_NonConformingDoctype);
        case Err::kMissingDoctype:
            return logger::FormatMsg(logger::MsgCat::kHTML_MissingDoctype);
        case Err::kMisplacedDoctype:
            return logger::FormatMsg(logger::MsgCat::kHTML_MisplacedDoctype);
        case Err::kEndTagWithoutMatchingOpenElement:
            return logger::FormatMsg(logger::MsgCat::kHTML_EndTagWithoutMatchingOpenElement);
        case Err::kClosingOfElementWithOpenChildElements:
            return logger::FormatMsg(logger::MsgCat::kHTML_ClosingOfElementWithOpenChildElements);
        case Err::kDisallowedContentInNoscriptInHead:
            return logger::FormatMsg(logger::MsgCat::kHTML_DisallowedContentInNoscriptInHead);
        case Err::kOpenElementsLeftAfterEof:
            return logger::FormatMsg(logger::MsgCat::kHTML_OpenElementsLeftAfterEof);
        case Err::kAbandonedHeadElementChild:
            return logger::FormatMsg(logger::MsgCat::kHTML_AbandonedHeadElementChild);
        case Err::kMisplacedStartTagForHeadElement:
            return logger::FormatMsg(logger::MsgCat::kHTML_MisplacedStartTagForHeadElement);
        case Err::kNestedNoscriptInHead:
            return logger::FormatMsg(logger::MsgCat::kHTML_NestedNoscriptInHead);
        case Err::kEofInElementThatCanContainOnlyText:
            return logger::FormatMsg(logger::MsgCat::kHTML_EofInElementThatCanContainOnlyText);
        default:
            return logger::FormatMsg(logger::MsgCat::kHTML_ParseError);
    }
}

namespace {

// Resolves the location that best describes "node"'s start tag for
// diagnostics. The dedicated start-tag span is preferred when present and
// positioned; otherwise the node's general source location is used. Returns
// nullptr when neither is available (for example a synthesized node that
// never touched real source), so callers can fall back to an empty range.
const Location* LocationOfTag(const Node& node) {
    if (node.start_tag_location != nullptr &&
        node.start_tag_location->start_offset >= 0) {
        return node.start_tag_location;
    }
    if (node.source_code_location != nullptr &&
        node.source_code_location->start_offset >= 0) {
        return node.source_code_location;
    }
    return nullptr;
}

// Converts a tokenizer location (code-unit offsets) into a byte range in the
// original source using the offset map. Locations without a usable span, and
// spans that shrink when translated (end before start), collapse to a
// zero-length range at the start byte so diagnostics never point backwards.
logger::Range ToLogRange(const Utf16OffsetMap& map, const Location& loc) {
    if (loc.start_offset < 0 || loc.end_offset < 0 || loc.end_offset < loc.start_offset) {
        return logger::Range{logger::Loc{0}, 0};
    }

    const int32_t start = map.ByteOffset(loc.start_offset);
    const int32_t end = map.ByteOffset(loc.end_offset);

    if (end < start) {
        return logger::Range{logger::Loc{start}, 0};
    }

    return logger::Range{logger::Loc{start}, end - start};
}

// True when "value" consists only of whitespace, i.e. it contains no
// non-space characters that could name a resource. Blank attribute values
// (for example an empty "src") are reported as such rather than being turned
// into import records.
bool IsBlank(const std::string& value) {
    for (char c : value) {
        switch (c) {
            case ' ':
            case '\t':
            case '\n':
            case '\f':
            case '\r':
                break;
            default:
                return false;
        }
    }
    return true;
}

// The remaining anonymous helpers below implement the single-url reference
// lookup and the multi-URL harvesters (style url() references and
// srcset/imagesrcset candidates).

// True when a bare URL "path" must be prefixed with "./" to satisfy the
// bundler's package-path resolver (which, unlike a browser, does not treat a
// leading "app.js" as page-relative by default). Paths that already start
// with ".", "/", "\", "#", or "?" are left alone, and a ':' anywhere in the
// first path segment marks the URL as absolute -- a scheme such as "https:"
// or "data:", or a platform volume label -- so scheme-less module references
// ("app.js", "x/y.js", "app.js?v=2") are the only ones rewritten.
bool ShouldPrefixWithDotSlash(std::string_view path) {
    if (path.empty() || path[0] == '.' || path[0] == '/' || path[0] == '\\' ||
        path[0] == '#' || path[0] == '?')
    {
        return false;
    }

    size_t first_segment_end = path.find_first_of("/\\");
    if (first_segment_end == std::string_view::npos) {
        first_segment_end = path.size();
    }
    for (size_t i = 0; i < first_segment_end; i++) {
        if (path[i] == ':') {
            return false;
        }
    }
    return true;
}

// Reports the attribute that gives "element" its single resource URL, or
// nullptr when the element references no external resource of that shape.
// On success "kind_out" receives the compiler import kind to record and
// "attr_out" the attribute name whose value holds the URL. Scripts are
// statement imports (kStmt) keyed by "src"; every other resource element is
// a "url"-kind reference keyed by its resource attribute. Elements whose URL
// is absent or carried elsewhere (a "srcset" list, an inline script body, an
// <input> that is not an image button) report no single URL here.
//
//   <script src="m.js">          -> {kind: kStmt, attr: "src", "m.js"}
//   <link rel="icon" href="f.ico"> -> {kind: kUrl, attr: "href", "f.ico"}
//   <div>                        -> nullptr
const std::string* ResourceURLOfElement(const Node& element,
                                        compiler::ImportKind& kind_out,
                                        std::string_view& attr_out) {
    const std::string_view tag = element.tag_name;

    if (tag == "script") {
        if (const std::string* src = FindAttrValue(element, "src")) {
            kind_out = compiler::ImportKind::kStmt;
            attr_out = "src";
            return src;
        }
        return nullptr;
    }

    if (tag == "link") {
        if (const std::string* href = FindAttrValue(element, "href")) {
            kind_out = compiler::ImportKind::kUrl;
            attr_out = "href";
            return href;
        }
        return nullptr;
    }

    const std::string* value = nullptr;
    std::string_view attr_name;

    if (tag == "img" || tag == "embed" || tag == "track" || tag == "iframe" ||
        tag == "audio") {
        value = FindAttrValue(element, "src");
        attr_name = "src";
    } else if (tag == "video") {
        value = FindAttrValue(element, "src");
        attr_name = "src";
        if (value == nullptr) {
            value = FindAttrValue(element, "poster");
            attr_name = "poster";
        }
    } else if (tag == "source") {
        value = FindAttrValue(element, "src");
        attr_name = "src";
    } else if (tag == "object") {
        value = FindAttrValue(element, "data");
        attr_name = "data";
    } else if (tag == "input") {
        const std::string* type = FindAttrValue(element, "type");
        if (type != nullptr && *type == "image") {
            value = FindAttrValue(element, "src");
            attr_name = "src";
        }
    } else if (tag == "use") {
        value = FindAttrValue(element, "href");
        attr_name = "href";
        if (value == nullptr) {
            value = FindAttrValue(element, "xlink:href");
            attr_name = "xlink:href";
        }
    } else if (tag == "image") {
        value = FindAttrValue(element, "href");
        attr_name = "href";
    }

    if (value != nullptr) {
        kind_out = compiler::ImportKind::kUrl;
        attr_out = attr_name;
    }
    return value;
}

// Harvests url(...) references out of a "style" attribute value, appending
// one kUrl import record per occurrence to "result". Quoted strings and
// nested single/double quotes inside the url(...) argument are respected so
// closing parens inside string values do not truncate the path. Every origin
// records the byte range of exactly that path within "value", because a
// style attribute typically holds several declarations and only the url()
// substring may be rewritten. An empty or blank path produces nothing.
//
//   style="background: url('a.png'); color: url(b.svg)" "a.png" then "b.svg"
//     -> two kUrl records with per-path byte offsets
static void CollectStyleUrlRecords(const Node& element,
                                   const std::string_view value,
                                   const logger::Range& range, bool want_records,
                                   AST& result) {
    if (!want_records) {
        return;
    }
    const size_t n = value.size();
    size_t i = 0;
    while (i < n) {
        const char c = value[i];
        if (c == '\'' || c == '"') {
            const char q = c;
            i++;
            while (i < n && value[i] != q) {
                i++;
            }
            i++;
            continue;
        }
        if (i + 4 <= n && value.compare(i, 4, "url(") == 0) {
            size_t close = i + 4;
            char quote = 0;
            while (close < n) {
                const char cc = value[close];
                if (quote != 0) {
                    if (cc == quote) {
                        quote = 0;
                    }
                } else if (cc == '\'' || cc == '"') {
                    quote = cc;
                } else if (cc == ')') {
                    break;
                }
                close++;
            }
            if (close >= n) {
                i++;
                continue;
            }
            size_t start = i + 4;
            size_t end = close;
            while (start < end && (value[start] == ' ' || value[start] == '\t' ||
                                   value[start] == '\n' || value[start] == '\r')) {
                start++;
            }
            while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                                   value[end - 1] == '\n' ||
                                   value[end - 1] == '\r')) {
                end--;
            }
            size_t path_off = start;
            size_t path_len = end - start;
            if (path_len >= 2 &&
                (value[start] == '\'' || value[start] == '"') &&
                (value[end - 1] == '\'' || value[end - 1] == '"')) {
                path_off = start + 1;
                path_len = end - start - 2;
            }
            std::string path(value.substr(path_off, path_len));
            if (!path.empty()) {
                compiler::ImportRecord record;
                record.kind = compiler::ImportKind::kUrl;
                record.path.text = path;
                if (ShouldPrefixWithDotSlash(record.path.text)) {
                    record.path.text = "./" + record.path.text;
                }
                record.range = range;
                result.import_records.push_back(std::move(record));
                ImportRecordOrigin origin;
                origin.element = &element;
                origin.attr_name = "style";
                origin.value_offset = static_cast<uint32_t>(path_off);
                origin.value_length = static_cast<uint32_t>(path_len);
                result.record_origins.push_back(std::move(origin));
            }
            i = close + 1;
            continue;
        }
        i++;
    }
}

// Harvests every candidate URL from a "srcset" (<img>, <source>) or
// "imagesrcset" (<link rel="preload">) attribute, appending one kUrl import
// record per candidate to "result". ParseSrcset splits the value into its
// URL-plus-descriptor entries; each origin stores the byte range of the URL
// inside the attribute value so the HTML output pass can rewrite just that
// slice while preserving descriptors and the whitespace between candidates.
// Empty candidates and blank attributes produce nothing.
//
//   srcset="a.jpg 480w, b.jpg 2x" -> two kUrl records with per-URL offsets
static void CollectSrcsetRecords(const Node& element,
                                 const logger::Range& range, bool want_records,
                                 AST& result) {
    if (!want_records) {
        return;
    }
    const std::string* value = nullptr;
    std::string_view attr_name;
    if (element.tag_name == "img" || element.tag_name == "source") {
        value = FindAttrValue(element, "srcset");
        attr_name = "srcset";
    } else if (element.tag_name == "link") {
        value = FindAttrValue(element, "imagesrcset");
        attr_name = "imagesrcset";
    } else {
        return;
    }
    if (value == nullptr || IsBlank(*value)) {
        return;
    }

    for (const SrcsetEntry& entry : ParseSrcset(*value)) {
        if (entry.url.empty()) {
            continue;
        }
        compiler::ImportRecord record;
        record.kind = compiler::ImportKind::kUrl;
        record.path.text = entry.url;
        if (ShouldPrefixWithDotSlash(record.path.text)) {
            record.path.text = "./" + record.path.text;
        }
        record.range = range;
        result.import_records.push_back(std::move(record));
        ImportRecordOrigin origin;
        origin.element = &element;
        origin.attr_name = attr_name;
        origin.value_offset = static_cast<uint32_t>(entry.url_offset);
        origin.value_length = static_cast<uint32_t>(entry.url_length);
        result.record_origins.push_back(std::move(origin));
    }
}

}

// Converts "element"'s start tag into a byte range within "source"'s UTF-8
// contents, for diagnostics rendered through a logger::LineColumnTracker bound
// to that same source. Import records already attach this same range; giving
// callers a public accessor lets warnings or notes that are not tied to an
// individual import record (such as the import-map reorder) still point at
// the responsible element. Elements without a usable location yield the empty
// range at file start.
logger::Range RangeOfTag(const logger::Source& source, const Node& element) {
    const Location* loc = LocationOfTag(element);
    if (loc == nullptr) {
        return logger::Range{logger::Loc{0}, 0};
    }
    const Utf16OffsetMap map = Utf16OffsetMap::Build(source.contents);
    return ToLogRange(map, *loc);
}

// Parses "source.contents" as HTML and returns an AST populated according to
// "options". Source positions are tracked (parser source-code locations are
// enabled), and every parse error the tokenizer or tree builder reports is
// routed into "log" using the source byte ranges and line/column tracker.
// Duplicate attributes are treated as a recoverable, downgradable warning
// (browsers keep the first occurrence), reported through a MsgID so it can be
// silenced via --log-override; all other parse errors are hard errors with no
// MsgID, hence not downgradable. The document (or, with parse_fragment, the
// parsed fragment) is then walked: content-bearing <style> elements and
// inline <script> bodies are collected into inline_styles/inline_scripts,
// while resource-bearing elements contribute import records. A "guchho-ignore"
// attribute excludes an element from both collections, and import-map scripts
// are skipped entirely because they describe module resolution without being
// resources themselves. Single-url attributes become one ImportRecord each
// (with "./" normalization for bare relative URLs), and style url() as well
// as srcset/imagesrcset values are harvested per candidate by the dedicated
// helpers, each origin carrying the precise byte range to rewrite.
//
//   options.defaults, source "<img src='a.png'>"
//     -> import_records = [kUrl "a.png"], record_origins[0] = {img, "src"}
AST Parse(logger::Log& log, const logger::Source& source, const BridgeOptions& options) {
    AST result;

    const Utf16OffsetMap map = Utf16OffsetMap::Build(source.contents);
    logger::LineColumnTracker tracker(&source);

    auto to_log_range = [&map](const Location* loc) -> logger::Range {
        if (loc == nullptr) {
            return logger::Range{logger::Loc{0}, 0};
        }
        return ToLogRange(map, *loc);
    };

    ParserOptions parser_options;
    parser_options.source_code_location_info = true;
    parser_options.on_parse_error = [&log, &tracker, &to_log_range](const ParserError& error) {
        if (error.code == Err::kDuplicateAttribute) {
            log.AddID(logger::MsgID::kHTML_ParseWarning, logger::MsgKind::kWarning,
                      &tracker, to_log_range(&error),
                      std::string(ErrDescription(error.code)));
        } else {
                    log.AddError(&tracker, to_log_range(&error),
                         std::string(ErrDescription(error.code)));
        }
    };

    std::unique_ptr<Parser> fragment_parser;
    if (options.parse_fragment) {
        fragment_parser = Parser::GetFragmentParser(nullptr, parser_options);
        fragment_parser->Write(source.contents);
        result.node = fragment_parser->GetFragment();
    } else {
        result.node = Parser::ParseDocument(source.contents, parser_options);
    }

    const bool want_records = options.collect_import_records;
    const bool want_inline = options.collect_inline_code;

    std::function<void(const Node&)> visit = [&](const Node& node) {
        if (!IsElementNode(node)) {
            for (const auto& child : GetChildNodes(node)) {
                visit(*child);
            }
            return;
        }

        if (IsTemplateElement(node)) {
            visit(GetTemplateContent(node));
            return;
        }

        const logger::Range range = to_log_range(LocationOfTag(node));

        const bool ignored = FindAttrValue(node, "guchho-ignore") != nullptr;

        if (want_inline && !ignored && node.tag_name == "style") {
            result.inline_styles.push_back(&node);
        } else if (want_inline && !ignored && node.tag_name == "script" &&
                   !IsImportMapScript(node)) {
            const std::string* src = FindAttrValue(node, "src");
            if (src == nullptr) {
                result.inline_scripts.push_back(&node);
            }
        }

        if (want_records && !ignored && !IsImportMapScript(node)) {
            compiler::ImportKind kind{};
            std::string_view attr_name;
            const std::string* value = ResourceURLOfElement(node, kind, attr_name);

            if (value != nullptr) {
                if (IsBlank(*value)) {
                    log.AddID(logger::MsgID::kHTML_ParseWarning,
                              logger::MsgKind::kWarning,
                              &tracker, range,
                              guchho::logger::FormatMsg(guchho::logger::MsgCat::kHTML_EmptyResourceURL, node.tag_name));
                } else {
                    compiler::ImportRecord record;
                    record.kind = kind;
                    record.path.text = *value;
                    if (ShouldPrefixWithDotSlash(record.path.text)) {
                        record.path.text = "./" + record.path.text;
                    }
                    record.range = range;
                    result.import_records.push_back(std::move(record));
                    result.record_origins.push_back(ImportRecordOrigin{&node, attr_name});
                }
            }

            if (const std::string* style = FindAttrValue(node, "style")) {
                CollectStyleUrlRecords(node, *style, range,
                                       true, result);
            }

            CollectSrcsetRecords(node, range, true, result);
        }

        for (const auto& child : GetChildNodes(node)) {
            visit(*child);
        }
    };

    visit(*result.node);

    return result;
}

// Copy constructor: duplicates the vector members as usual, then deep-copies
// the tree via CloneNode. CloneNode hands every clone a fresh node_id and,
// during the same traversal (which descends into nested <template> content),
// records "source_id -> clone" in clone_map, so re-pointing the metadata that
// references tree nodes needs no extra walk. The origin records and the
// inline script/style lists are then remapped from the source tree's nodes to
// their clones; without this re-pointing, holding the copied AST would leave
// the lists pointing at the (still living) original tree. Elements that do
// not appear in the clone map (null or unmatched entries) stay null.
AST::AST(const AST& other)
    : import_records(other.import_records),
      record_origins(other.record_origins),
      inline_scripts(other.inline_scripts),
      inline_styles(other.inline_styles) {
    if (other.node == nullptr) {
        return;
    }

    std::unordered_map<uint32_t, Node*> clone_map;
    node = CloneNode(*other.node, nullptr, &clone_map);

    if (import_records.empty() && inline_scripts.empty() && inline_styles.empty()) {
        return;
    }

    auto remap = [&clone_map](const Node* original) -> const Node* {
        if (original == nullptr) {
            return nullptr;
        }
        auto it = clone_map.find(original->node_id);
        return it != clone_map.end() ? it->second : nullptr;
    };

    for (auto& origin : record_origins) {
        origin.element = remap(origin.element);
    }
    for (const Node*& script : inline_scripts) {
        script = remap(script);
    }
    for (const Node*& style : inline_styles) {
        style = remap(style);
    }
}

// Copy assignment via copy-and-swap: the deep copy and any metadata
// re-pointing happen in the copy constructor, and this operator merely steals
// the freshly built temporary's members. Inside the self-check the temporary
// is destroyed at scope exit, releasing the old tree when it was replaced.
AST& AST::operator=(const AST& other) {
    if (this != &other) {
        AST tmp(other);
        node = std::move(tmp.node);
        import_records = std::move(tmp.import_records);
        record_origins = std::move(tmp.record_origins);
        inline_scripts = std::move(tmp.inline_scripts);
        inline_styles = std::move(tmp.inline_styles);
    }
    return *this;
}

}
