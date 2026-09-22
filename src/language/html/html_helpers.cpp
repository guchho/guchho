#include "guchho/html/html_helpers.hpp"

#include <algorithm>
#include <array>
#include <string>

// ---------------------------------------------------------------------------
// Tag IDs / namespaces / special elements
//
// This translation unit implements the lookup tables and classification
// predicates that the HTML tokenizer and tree builder query while they run.
// Everything here is pure: no global state is mutated, no I/O happens, and
// every function depends only on its arguments. Keeping these paths cheap and
// branch-friendly is the whole point of reducing element names to small
// integers before the hot loops see them.
// ---------------------------------------------------------------------------

namespace guchho::html {

namespace {

// One row of the static tag-name table: the lowercase element name exactly as
// the tokenizer emits it after lowercasing, paired with its "TagId". The
// struct intentionally holds no owning state and lives entirely in read-only
// memory so the compiler can place the whole table in .rodata and compare
// against it without any runtime initialization cost.
struct TagNameEntry {
    std::string_view name;
    TagId id;
};

// The complete registry of element names Guchho recognizes, kept sorted by
// name so "GetTagId" can binary-search it in O(log n) instead of scanning.
// The count 123 is significant: it is exactly the number of entries in the
// initializer, and the array size is spelled out so that a missing or
// duplicated row would trip the compiler rather than silently shrinking the
// searchable range.
//
// CRITICAL INVARIANT: this table is sorted by "name", NOT by "TagId". The
// enum listing order is deliberately unrelated — for example "image" sorts
// before "img" while the enum declares kImage before kImg — so the table must
// never be reordered to "look like" the enum. The name=>id association comes
// from each row's initializer, not from positional alignment.
//
// Input => output examples (via "GetTagId"):
//   "div"            => TagId::kDiv
//   "annotation-xml" => TagId::kAnnotationXml
//   "foreignObject"  => TagId::kForeignObject  (only this name uses an inner capital)
//   "zzz"            => TagId::kUnknown        (absent from the table)
//
// Edge cases: names must match byte-for-byte, including case. The tokenizer
// lowercases tag names before calling "GetTagId", so mixed-case spellings are
// expected to miss and return kUnknown — that is correct behavior, not a bug
// in this table. When adding a new element, insert it at its alphabetical
// position, bump the array size, and verify the static assertion below still
// compiles.
inline constexpr std::array<TagNameEntry, 123> kTagNames = {{
    {"a", TagId::kA},
    {"address", TagId::kAddress},
    {"annotation-xml", TagId::kAnnotationXml},
    {"applet", TagId::kApplet},
    {"area", TagId::kArea},
    {"article", TagId::kArticle},
    {"aside", TagId::kAside},
    {"b", TagId::kB},
    {"base", TagId::kBase},
    {"basefont", TagId::kBasefont},
    {"bgsound", TagId::kBgsound},
    {"big", TagId::kBig},
    {"blockquote", TagId::kBlockquote},
    {"body", TagId::kBody},
    {"br", TagId::kBr},
    {"button", TagId::kButton},
    {"caption", TagId::kCaption},
    {"center", TagId::kCenter},
    {"code", TagId::kCode},
    {"col", TagId::kCol},
    {"colgroup", TagId::kColgroup},
    {"dd", TagId::kDd},
    {"desc", TagId::kDesc},
    {"details", TagId::kDetails},
    {"dialog", TagId::kDialog},
    {"dir", TagId::kDir},
    {"div", TagId::kDiv},
    {"dl", TagId::kDl},
    {"dt", TagId::kDt},
    {"em", TagId::kEm},
    {"embed", TagId::kEmbed},
    {"fieldset", TagId::kFieldset},
    {"figcaption", TagId::kFigcaption},
    {"figure", TagId::kFigure},
    {"font", TagId::kFont},
    {"footer", TagId::kFooter},
    {"foreignObject", TagId::kForeignObject},
    {"form", TagId::kForm},
    {"frame", TagId::kFrame},
    {"frameset", TagId::kFrameset},
    {"h1", TagId::kH1},
    {"h2", TagId::kH2},
    {"h3", TagId::kH3},
    {"h4", TagId::kH4},
    {"h5", TagId::kH5},
    {"h6", TagId::kH6},
    {"head", TagId::kHead},
    {"header", TagId::kHeader},
    {"hgroup", TagId::kHgroup},
    {"hr", TagId::kHr},
    {"html", TagId::kHtml},
    {"i", TagId::kI},
    {"iframe", TagId::kIframe},
    {"image", TagId::kImage},
    {"img", TagId::kImg},
    {"input", TagId::kInput},
    {"keygen", TagId::kKeygen},
    {"label", TagId::kLabel},
    {"li", TagId::kLi},
    {"link", TagId::kLink},
    {"listing", TagId::kListing},
    {"main", TagId::kMain},
    {"malignmark", TagId::kMalignmark},
    {"marquee", TagId::kMarquee},
    {"math", TagId::kMath},
    {"menu", TagId::kMenu},
    {"meta", TagId::kMeta},
    {"mglyph", TagId::kMglyph},
    {"mi", TagId::kMi},
    {"mn", TagId::kMn},
    {"mo", TagId::kMo},
    {"ms", TagId::kMs},
    {"mtext", TagId::kMtext},
    {"nav", TagId::kNav},
    {"nobr", TagId::kNobr},
    {"noembed", TagId::kNoembed},
    {"noframes", TagId::kNoframes},
    {"noscript", TagId::kNoscript},
    {"object", TagId::kObject},
    {"ol", TagId::kOl},
    {"optgroup", TagId::kOptgroup},
    {"option", TagId::kOption},
    {"p", TagId::kP},
    {"param", TagId::kParam},
    {"plaintext", TagId::kPlaintext},
    {"pre", TagId::kPre},
    {"rb", TagId::kRb},
    {"rp", TagId::kRp},
    {"rt", TagId::kRt},
    {"rtc", TagId::kRtc},
    {"ruby", TagId::kRuby},
    {"s", TagId::kS},
    {"script", TagId::kScript},
    {"search", TagId::kSearch},
    {"section", TagId::kSection},
    {"select", TagId::kSelect},
    {"small", TagId::kSmall},
    {"source", TagId::kSource},
    {"span", TagId::kSpan},
    {"strike", TagId::kStrike},
    {"strong", TagId::kStrong},
    {"style", TagId::kStyle},
    {"sub", TagId::kSub},
    {"summary", TagId::kSummary},
    {"sup", TagId::kSup},
    {"svg", TagId::kSvg},
    {"table", TagId::kTable},
    {"tbody", TagId::kTbody},
    {"td", TagId::kTd},
    {"template", TagId::kTemplate},
    {"textarea", TagId::kTextarea},
    {"tfoot", TagId::kTfoot},
    {"th", TagId::kTh},
    {"thead", TagId::kThead},
    {"title", TagId::kTitle},
    {"tr", TagId::kTr},
    {"track", TagId::kTrack},
    {"tt", TagId::kTt},
    {"u", TagId::kU},
    {"ul", TagId::kUl},
    {"var", TagId::kVar},
    {"wbr", TagId::kWbr},
    {"xmp", TagId::kXmp},
}};

// Compile-time proof that "kTagNames" is sorted by name, which is the sole
// precondition for "GetTagId"'s binary search to be correct. If someone
// reorders or inserts a row out of sequence, this static assertion fails the
// build immediately instead of allowing a subtle search miss at runtime. The
// lambda uses the same strict-less comparator that "std::lower_bound" relies
// on, so the check matches the actual search semantics exactly.
constexpr bool kTagNamesSorted = std::is_sorted(kTagNames.begin(), kTagNames.end(),
    [](const TagNameEntry& a, const TagNameEntry& b) { return a.name < b.name; });
static_assert(kTagNamesSorted, "tag name table must be sorted for binary search");

} // namespace

// Resolves an "NS" enumerator to its canonical namespace URI, a static
// string literal whose lifetime is the whole program, so the returned
// string_view stays valid indefinitely and can be stored and compared
// without any allocation. The tree builder and serializer use these URIs
// when they must name an
// element's namespace explicitly (attribute normalization, foreign-content
// handling, DOM inspection output).
//
// Input => output examples:
//   NS::kHtml   => "http://www.w3.org/1999/xhtml"
//   NS::kMathml => "http://www.w3.org/1998/Math/MathML"
//   NS::kSvg    => "http://www.w3.org/2000/svg"
//   NS::kXlink  => "http://www.w3.org/1999/xlink"
//   NS::kXml    => "http://www.w3.org/XML/1998/namespace"
//   NS::kXmlns  => "http://www.w3.org/2000/xmlns/"
//
// Edge cases: because "NS" is an 8-bit enum, a caller can cast an arbitrary
// byte into it. Every legitimate enumerator is handled by the switch, and any
// out-of-range value falls through to the trailing "return \"\";", which
// yields an empty string rather than undefined behavior. Treat that empty
// result as "unknown namespace" and do not emit it into serialized output.
std::string_view NamespaceUri(NS ns) {
    switch (ns) {
        case NS::kHtml: return "http://www.w3.org/1999/xhtml";
        case NS::kMathml: return "http://www.w3.org/1998/Math/MathML";
        case NS::kSvg: return "http://www.w3.org/2000/svg";
        case NS::kXlink: return "http://www.w3.org/1999/xlink";
        case NS::kXml: return "http://www.w3.org/XML/1998/namespace";
        case NS::kXmlns: return "http://www.w3.org/2000/xmlns/";
    }
    return "";
}

// Translates a lowercase element name into its numeric "TagId" by binary
// searching the sorted "kTagNames" table. This is the single entry point the
// tokenizer uses to convert a name string into a cheap integer that the
// tree builder's classification switches can branch on. It runs in O(log 123)
// comparisons, each comparing short ASCII strings, so it stays off the
// critical path even when called for every start tag in a document.
//
// Input => output examples:
//   GetTagId("div")             => TagId::kDiv
//   GetTagId("annotation-xml")  => TagId::kAnnotationXml
//   GetTagId("foreignObject")   => TagId::kForeignObject
//   GetTagId("notanelement")    => TagId::kUnknown
//   GetTagId("")                => TagId::kUnknown
//
// Edge cases: matching is case-sensitive and byte-exact. Callers must
// lowercase the name first (the tokenizer does), otherwise "DIV" misses and
// returns kUnknown. "std::lower_bound" may land on any element that compares
// not-less than the input, so the code always re-checks exact equality before
// trusting the hit; if the search lands one past the last matching row, or
// the name simply is not present, kUnknown is returned. kUnknown is a
// sentinel that never denotes a real element, so every classification switch
// that consumes "TagId" must route it to a default that behaves conservatively.
TagId GetTagId(std::string_view tag_name) {
    auto it = std::lower_bound(kTagNames.begin(), kTagNames.end(), tag_name,
        [](const TagNameEntry& entry, std::string_view name) { return entry.name < name; });
    if (it != kTagNames.end() && it->name == tag_name) {
        return it->id;
    }
    return TagId::kUnknown;
}

// Reports whether an element belongs to the "special" set for its namespace.
// Special elements interrupt implied end-tag processing, stop formatting-
// element reconstruction, and delimit list-item and paragraph handling in the
// tree builder. The membership test is namespace-sensitive because a name can
// be special in one namespace and ordinary in another: "title" is special
// only under SVG, while the math runs (mi, mo, mn, ms, mtext, annotation-xml)
// are special only under MathML.
//
// Input => output examples:
//   IsSpecialElement(TagId::kDiv,   NS::kHtml)  => true
//   IsSpecialElement(TagId::kSpan,   NS::kHtml)  => false
//   IsSpecialElement(TagId::kTitle,  NS::kHtml)  => false
//   IsSpecialElement(TagId::kTitle,  NS::kSvg)   => true
//   IsSpecialElement(TagId::kMi,     NS::kMathml)=> true
//   IsSpecialElement(TagId::kMi,     NS::kHtml)  => false
//   IsSpecialElement(TagId::kUnknown,NS::kHtml)  => false
//
// Edge cases: the outer switch is exhaustive over both "NS" and "TagId"
// enumerators, but each inner switch still carries a default that returns
// false, so unknown tags and future enumerators degrade safely. The three
// attribute-only namespaces (kXlink, kXml, kXmlns) contain no elements at
// all and short-circuit to false. The local alias "using T = TagId" exists
// purely to keep the long case lists readable; it does not change the type
// being switched on.
bool IsSpecialElement(TagId tag_id, NS ns) {
    using T = TagId;
    switch (ns) {
        case NS::kHtml:
            switch (tag_id) {
                case T::kAddress:
                case T::kApplet:
                case T::kArea:
                case T::kArticle:
                case T::kAside:
                case T::kBase:
                case T::kBasefont:
                case T::kBgsound:
                case T::kBlockquote:
                case T::kBody:
                case T::kBr:
                case T::kButton:
                case T::kCaption:
                case T::kCenter:
                case T::kCol:
                case T::kColgroup:
                case T::kDd:
                case T::kDetails:
                case T::kDir:
                case T::kDiv:
                case T::kDl:
                case T::kDt:
                case T::kEmbed:
                case T::kFieldset:
                case T::kFigcaption:
                case T::kFigure:
                case T::kFooter:
                case T::kForm:
                case T::kFrame:
                case T::kFrameset:
                case T::kH1:
                case T::kH2:
                case T::kH3:
                case T::kH4:
                case T::kH5:
                case T::kH6:
                case T::kHead:
                case T::kHeader:
                case T::kHgroup:
                case T::kHr:
                case T::kHtml:
                case T::kIframe:
                case T::kImg:
                case T::kInput:
                case T::kLi:
                case T::kLink:
                case T::kListing:
                case T::kMain:
                case T::kMarquee:
                case T::kMenu:
                case T::kMeta:
                case T::kNav:
                case T::kNoembed:
                case T::kNoframes:
                case T::kNoscript:
                case T::kObject:
                case T::kOl:
                case T::kP:
                case T::kParam:
                case T::kPlaintext:
                case T::kPre:
                case T::kScript:
                case T::kSection:
                case T::kSelect:
                case T::kSource:
                case T::kStyle:
                case T::kSummary:
                case T::kTable:
                case T::kTbody:
                case T::kTd:
                case T::kTemplate:
                case T::kTextarea:
                case T::kTfoot:
                case T::kTh:
                case T::kThead:
                case T::kTitle:
                case T::kTr:
                case T::kTrack:
                case T::kUl:
                case T::kWbr:
                case T::kXmp:
                    return true;
                default:
                    return false;
            }
        case NS::kMathml:
            switch (tag_id) {
                case T::kMi:
                case T::kMo:
                case T::kMn:
                case T::kMs:
                case T::kMtext:
                case T::kAnnotationXml:
                    return true;
                default:
                    return false;
            }
        case NS::kSvg:
            switch (tag_id) {
                case T::kTitle:
                case T::kForeignObject:
                case T::kDesc:
                    return true;
                default:
                    return false;
            }
        case NS::kXlink:
        case NS::kXml:
        case NS::kXmlns:
            return false;
    }
    return false;
}

// Distinguishes the six numbered heading elements (h1–h6) from everything
// else. The tree builder needs this because numbered headings participate in
// implied end-tag handling and in "reset the insertion mode appropriately",
// neither of which applies to unnumbered headings or to the deprecated h7+
// style names (which do not exist anyway).
//
// Input => output examples:
//   IsNumberedHeader(TagId::kH1)       => true
//   IsNumberedHeader(TagId::kH6)       => true
//   IsNumberedHeader(TagId::kHr)       => false   (horizontal rule, not a heading)
//   IsNumberedHeader(TagId::kHeading)  => false   (no such id; kUnknown => false)
//
// Edge cases: the switch is exhaustive over the heading ids and its default
// covers TagId::kUnknown and every other element, so an unrecognized id
// safely reports false rather than falling into undefined behavior.
bool IsNumberedHeader(TagId tag_id) {
    switch (tag_id) {
        case TagId::kH1:
        case TagId::kH2:
        case TagId::kH3:
        case TagId::kH4:
        case TagId::kH5:
        case TagId::kH6:
            return true;
        default:
            return false;
    }
}

// Reports whether an element's content is raw text that must never be
// entity-unescaped by the tokenizer and never re-escaped on serialization.
// Elements such as "script", "style", and "xmp" have CDATA-like content:
// rewriting "&amp;" to "&" inside them would corrupt scripts, and re-escaping
// on output would double-encode data that was never encoded to begin with.
//
// Input => output examples:
//   HasUnescapedText("style",    false) => true
//   HasUnescapedText("script",   false) => true
//   HasUnescapedText("xmp",      true)  => true
//   HasUnescapedText("noscript", false) => false
//   HasUnescapedText("noscript", true)  => true
//   HasUnescapedText("div",      true)  => false
//
// Edge cases: this predicate keys off the raw "tag_name" string rather than
// "TagId" because the tokenizer consults it before the name has necessarily
// been resolved to an id, and it must work even for names the id table does
// not know. The "noscript" case is conditional: its content stays raw only
// while scripting is enabled; with scripting disabled it is ordinary parseable
// markup that must be tokenized normally. All comparisons are case-sensitive,
// so callers are expected to pass the already-lowercased name the tokenizer
// produced.
bool HasUnescapedText(std::string_view tag_name, bool scripting_enabled) {
    return tag_name == "style" || tag_name == "script" || tag_name == "xmp" || tag_name == "iframe" ||
        tag_name == "noembed" || tag_name == "noframes" || tag_name == "plaintext" ||
        (scripting_enabled && tag_name == "noscript");
}

// True when "cp" is a Unicode noncharacter: a code point reserved forever for
// internal use and forbidden from valid interchange. Such values are not
// valid scalar values and must never be emitted into serialized output; the
// decoder substitutes the replacement character when it finds one.
//
// Input => output examples:
//   IsUndefinedCodePoint(0xfdd0)   => true   (start of the reserved block)
//   IsUndefinedCodePoint(0xfdef)   => true   (end of the reserved block)
//   IsUndefinedCodePoint(0xfffe)   => true   (last two code points of plane 0)
//   IsUndefinedCodePoint(0xffff)   => true
//   IsUndefinedCodePoint(0x1fffe)  => true   (same pattern repeats per plane)
//   IsUndefinedCodePoint(0xfffd)   => false  (the replacement character is legal)
//   IsUndefinedCodePoint(0x110000) => false  (beyond the Unicode range entirely)
//
// Two rules combine here: first, the contiguous U+FDD0–U+FDEF block is
// rejected outright; second, the bitwise test catches every code point whose
// final 10 bits are all ones (ending in FFFE or FFFF on any plane). The
// trailing "cp <= 0x10ffff" guard prevents the mask from accidentally
// classifying values above the Unicode ceiling as noncharacters.
bool IsUndefinedCodePoint(char32_t cp) {
    if (cp >= 0xfdd0 && cp <= 0xfdef) {
        return true;
    }
    return (cp & 0xfffe) == 0xfffe && cp <= 0x10ffff;
}

} // namespace guchho::html

// ---------------------------------------------------------------------------
// srcset attribute parsing / serialization
//
// Guchho parses "srcset" values so it can discover every image candidate
// URL a page references, then rewrite those URLs in place while leaving each
// width ("480w") or density ("2x") descriptor untouched. The grammar is
// forgiving: stray commas produce empty candidates that are dropped rather
// than treated as errors, and descriptors are carried through as opaque
// text so a round trip never invents or loses information.
// ---------------------------------------------------------------------------

namespace guchho::html {

namespace {

// True when "c" is one of the five ASCII whitespace characters that separate
// "srcset" candidates: space, tab, newline, form feed, carriage return. This
// deliberately excludes vertical tab and every non-ASCII whitespace, matching
// the byte-level definition the parser relies on. Used by both the whitespace
// skipper and the URL/descriptor scanners to decide where a token ends.
bool IsAsciiWhitespace(char c) {
    switch (c) {
        case ' ':
        case '\t':
        case '\n':
        case '\f':
        case '\r':
            return true;
        default:
            return false;
    }
}

// Consumes one descriptor token — a maximal run of characters that is neither
// ASCII whitespace nor a comma — advancing the shared position "pos" past it
// and returning the token as a view into the original buffer. The caller
// owns "pos"; this helper only reads and advances, never rewinds.
//
// Input => output example (with pos = 0 on "480w 2x"):
//   returns "480w", pos becomes 5
//
// Edge cases: if "pos" already sits on whitespace or a comma, the loop body
// never runs, the returned view is empty, and "pos" is unchanged — the caller
// must therefore skip whitespace itself before calling, which is exactly what
// "ParseSrcset" does. If "pos" starts at or past the end of input, the same
// empty-view, no-advance result occurs.
std::string_view CollectDescriptorToken(std::string_view input, size_t& pos) {
    const size_t start = pos;
    while (pos < input.size() && !IsAsciiWhitespace(input[pos]) &&
           input[pos] != ',') {
        pos++;
    }
    return input.substr(start, pos - start);
}

// Advances "pos" past any contiguous run of ASCII whitespace, leaving it on
// the first non-whitespace character or at input.size() if only whitespace
// remains. Safe to call when already positioned on a non-whitespace byte —
// it simply does nothing in that case.
//
// Input => output example (with pos = 0 on "  a.jpg"):
//   pos becomes 2, pointing at 'a'
//
// Edge cases: an empty input or a position already at the end leaves "pos"
// untouched; runs of mixed whitespace (spaces, tabs, newlines) are consumed
// in a single pass rather than one byte per call.
void SkipAsciiWhitespace(std::string_view input, size_t& pos) {
    while (pos < input.size() && IsAsciiWhitespace(input[pos])) {
        pos++;
    }
}

} // namespace

// Splits a "srcset" attribute value into its candidate entries, following the
// forgiving WHATWG-style grammar. The scan runs as a single left-to-right
// pass: skip leading whitespace, consume a URL (everything up to the next
// whitespace or comma), optionally consume a descriptor, then treat a comma
// as the boundary before repeating. Empty candidates introduced by stray or
// trailing commas are skipped silently rather than surfaced as errors, and
// every surviving entry records the byte range its URL occupies in the
// original string so a later rewrite can splice the buffer directly.
//
// Input => output examples:
//   ParseSrcset("a.jpg 480w, b.jpg 2x")
//       => [{url:"a.jpg", descriptor:"480w", url_offset:0,  url_length:5},
//          {url:"b.jpg", descriptor:"2x",   url_offset:11, url_length:5}]
//   ParseSrcset("a.jpg,b.jpg")
//       => two entries, both with empty descriptors
//   ParseSrcset("a.jpg, , b.jpg")
//       => two entries; the bare "," candidate contributes nothing
//   ParseSrcset("a.jpg,")
//       => one entry for "a.jpg" (the trailing comma yields no empty entry)
//   ParseSrcset("")
//       => empty vector
//
// Important behavior: a URL can never contain a comma — the first comma
// always terminates it — so descriptors with internal commas (e.g. a
// malformed "1,2x") get their tokens re-joined with a single space, which
// preserves the token sequence while normalizing separator whitespace. The
// url_offset/url_length pair is measured from the start of the input
// string_view rather than from the owning strings, so callers that pass a
// slice of a larger
// buffer must add their own base offset if they need absolute positions.
// The descriptor text is never validated: unknown suffixes pass through
// untouched, which keeps this function safe to use for pure discovery even
// when the caller intends to re-serialize unchanged.
std::vector<SrcsetEntry> ParseSrcset(std::string_view srcset) {
    std::vector<SrcsetEntry> entries;
    const size_t n = srcset.size();
    size_t pos = 0;

    while (true) {
        SkipAsciiWhitespace(srcset, pos);
        if (pos >= n) {
            break;
        }

        // A comma sitting where a URL should begin marks an empty candidate;
        // consume it and let the next iteration start the following entry.
        if (srcset[pos] == ',') {
            pos++;
            continue;
        }

        // The URL is the maximal run of non-whitespace, non-comma characters
        // starting at "pos". Because a comma can never appear inside a URL,
        // this slice is unambiguous no matter how the input was spaced.
        const size_t url_offset = pos;
        while (pos < n && !IsAsciiWhitespace(srcset[pos]) &&
               srcset[pos] != ',') {
            pos++;
        }
        const size_t url_end = pos;

        // Skipping whitespace here lets a comma that follows the URL be
        // recognized as the terminator of a descriptor-less candidate.
        SkipAsciiWhitespace(srcset, pos);
        if (pos < n && srcset[pos] == ',') {
            pos++;
            SrcsetEntry entry;
            entry.url.assign(srcset.substr(url_offset, url_end - url_offset));
            entry.url_offset = url_offset;
            entry.url_length = url_end - url_offset;
            entries.push_back(std::move(entry));
            continue;
        }

        // Otherwise gather the descriptor: one or more tokens separated by
        // whitespace and/or commas, running until the next top-level comma or
        // end of input. Tokens are re-joined with single spaces, so any
        // original spacing between them is normalized away.
        std::string descriptor;
        while (pos < n && srcset[pos] != ',') {
            SkipAsciiWhitespace(srcset, pos);
            if (pos >= n) {
                break;
            }
            const std::string_view token = CollectDescriptorToken(srcset, pos);
            if (!token.empty()) {
                if (!descriptor.empty()) {
                    descriptor += ' ';
                }
                descriptor.append(token);
            }
        }
        if (pos < n && srcset[pos] == ',') {
            pos++;
        }

        SrcsetEntry entry;
        entry.url.assign(srcset.substr(url_offset, url_end - url_offset));
        entry.descriptor = std::move(descriptor);
        entry.url_offset = url_offset;
        entry.url_length = url_end - url_offset;
        entries.push_back(std::move(entry));
    }

    return entries;
}

// Rebuilds a "srcset" attribute string from previously parsed entries. Each
// entry contributes its URL, followed by a single space and its descriptor if
// one is present; entries are joined with exactly ", ". The result is always
// syntactically valid and, when fed back through "ParseSrcset", reproduces
// the same entries — only the original separator spacing is normalized.
//
// Input => output examples:
//   SerializeSrcset({{url:"a.jpg",descriptor:"480w"},{url:"b.jpg",descriptor:"2x"}})
//       => "a.jpg 480w, b.jpg 2x"
//   SerializeSrcset({{url:"a.jpg",descriptor:"480w"},{url:"b.jpg"}})
//       => "a.jpg 480w, b.jpg"
//   SerializeSrcset({})
//       => ""   (an empty list emits no separator and no stray spaces)
//
// Edge cases: an entry with an empty descriptor must not receive the
// separating space, which is why the space is emitted only inside the
// non-empty-descriptor branch. The function assumes "url" values contain no
// commas or whitespace themselves — which "ParseSrcset" guarantees — because
// it performs no escaping or validation of its own.
std::string SerializeSrcset(const std::vector<SrcsetEntry>& entries) {
    std::string out;
    for (size_t i = 0; i < entries.size(); i++) {
        if (i != 0) {
            out += ", ";
        }
        out += entries[i].url;
        if (!entries[i].descriptor.empty()) {
            out += ' ';
            out += entries[i].descriptor;
        }
    }
    return out;
}

} // namespace guchho::html