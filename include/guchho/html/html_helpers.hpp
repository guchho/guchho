#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace guchho::html {

// Identifies which XML namespace a node or attribute path belongs to. Guchho
// resolves these values to their full namespace URIs through "NamespaceUri".
// The three attribute-only namespaces (kXlink, kXml, kXmlns) never appear as
// element namespaces; they only matter when looking up special-element status.
enum class NS : uint8_t {
    kHtml,
    kMathml,
    kSvg,
    kXlink,
    kXml,
    kXmlns,
};

// Maps an "NS" value to its canonical namespace URI string used in serialized
// output and tree construction. The returned string_view points at a
// static string literal, so it stays valid for the life of the program.
//
// Input => output examples:
//   NS::kHtml   => "http://www.w3.org/1999/xhtml"
//   NS::kSvg    => "http://www.w3.org/2000/svg"
//   NS::kXmlns  => "http://www.w3.org/2000/xmlns/"
//
// Edge cases: because "NS" is an 8-bit enum, a caller can cast an arbitrary
// byte to it. Every valid enumerator is covered by the switch, and any
// out-of-range value falls through to the final "return \"\";", producing an
// empty string rather than undefined behavior. Callers should treat that empty
// return as "no known namespace".
std::string_view NamespaceUri(NS ns);

// Short circuit for the handful of attribute names Guchho's HTML pipeline
// compares against by identity. Using string_view constants instead of using a
// hashed string keeps those comparisons O(1) when the left side is known to be
// one of these literals (for example when classifying attributes inside the
// tokenizer state machine). This is not a general-purpose registry: any other
// attribute name is handled by ordinary string comparison elsewhere.
namespace attrs {
inline constexpr std::string_view kType = "type";
inline constexpr std::string_view kAction = "action";
inline constexpr std::string_view kEncoding = "encoding";
inline constexpr std::string_view kPrompt = "prompt";
inline constexpr std::string_view kName = "name";
inline constexpr std::string_view kColor = "color";
inline constexpr std::string_view kFace = "face";
inline constexpr std::string_view kSize = "size";
} // namespace attrs

// The three possible document compatibility modes, in increasing leniency:
// kNoQuirks ("no quirks", standards mode), kQuirks ("quirks"),
// kLimitedQuirks ("limited quirks"). The tokenizer and tree builder select the
// mode from the doctype token; the value is ultimately surfaced on the
// document so that CSS-style rendering behavior can branch on it.
enum class DocumentMode : uint8_t {
    kNoQuirks,
    kQuirks,
    kLimitedQuirks,
};

// Small integer identifiers for the HTML element names Guchho knows about.
// Representing names as an enum value instead of comparing strings makes the
// hot classification paths in the tree builder (special elements, formatting
// elements, implied end tags) branch on a single byte. kUnknown is the
// catch-all for any name that is not in the well-known set.
//
// IMPORTANT: the numeric values here are not tied to a name's length or hash;
// they only need to be stable across translation units. "GetTagId" maps a name
// to its id by binary-searching the sorted "kTagNames" table in
// "html_helpers.cpp", whose entries map back to these enumerators directly. The
// table is sorted by *name* (e.g. "image" sorts before "img" while the enum
// declares kImage before kImg), so the enum listing order must never be
// reordered to "match" the table; the association is established by the table
// rows, not by position. Do not add values, reuse values, or remove values
// without keeping that table, its static sort assertion, and every switch over
// "TagId" consistent.
enum class TagId : uint8_t {
    kUnknown = 0,
    kA,
    kAddress,
    kAnnotationXml,
    kApplet,
    kArea,
    kArticle,
    kAside,

    kB,
    kBase,
    kBasefont,
    kBgsound,
    kBig,
    kBlockquote,
    kBody,
    kBr,
    kButton,

    kCaption,
    kCenter,
    kCode,
    kCol,
    kColgroup,

    kDd,
    kDesc,
    kDetails,
    kDialog,
    kDir,
    kDiv,
    kDl,
    kDt,

    kEm,
    kEmbed,

    kFieldset,
    kFigcaption,
    kFigure,
    kFont,
    kFooter,
    kForeignObject,
    kForm,
    kFrame,
    kFrameset,

    kH1,
    kH2,
    kH3,
    kH4,
    kH5,
    kH6,
    kHead,
    kHeader,
    kHgroup,
    kHr,
    kHtml,

    kI,
    kImg,
    kImage,
    kInput,
    kIframe,

    kKeygen,

    kLabel,
    kLi,
    kLink,
    kListing,

    kMain,
    kMalignmark,
    kMarquee,
    kMath,
    kMenu,
    kMeta,
    kMglyph,
    kMi,
    kMo,
    kMn,
    kMs,
    kMtext,

    kNav,
    kNobr,
    kNoframes,
    kNoembed,
    kNoscript,

    kObject,
    kOl,
    kOptgroup,
    kOption,

    kP,
    kParam,
    kPlaintext,
    kPre,

    kRb,
    kRp,
    kRt,
    kRtc,
    kRuby,

    kS,
    kScript,
    kSearch,
    kSection,
    kSelect,
    kSource,
    kSmall,
    kSpan,
    kStrike,
    kStrong,
    kStyle,
    kSub,
    kSummary,
    kSup,

    kTable,
    kTbody,
    kTemplate,
    kTextarea,
    kTfoot,
    kTd,
    kTh,
    kThead,
    kTitle,
    kTr,
    kTrack,
    kTt,

    kU,
    kUl,

    kSvg,

    kVar,

    kWbr,

    kXmp,
};

// Resolves a tag name to its numeric "TagId". The lookup is a binary search
// over the name-sorted "kTagNames" table, so it runs in O(log n) rather than a
// linear scan, and it is intentionally case-sensitive: HTML tokenization
// lowercases tag names before this is called.
//
// Input => output examples:
//   GetTagId("div")     => TagId::kDiv
//   GetTagId("annotation-xml") => TagId::kAnnotationXml
//   GetTagId("IMG")     => TagId::kUnknown   (callers must lowercase first)
//   GetTagId("notatag") => TagId::kUnknown
//   GetTagId("")        => TagId::kUnknown
//
// Edge cases: an empty string, a name with different casing, or any other
// string outside the 123 known tokenizer names yields TagId::kUnknown.
// kUnknown is a defensive sentinel, never the name of a real element, so
// every classification switch must include it in its default case.
TagId GetTagId(std::string_view tag_name);

// Reports whether an element affects tree-construction control flow simply by
// being opened or closed. "Special" elements interrupt implied end-tag
// processing, delimit formatting-element reconstruction, and generally stop
// "series of paragraphs" and list item handling in the tree builder. The
// classification is namespace-dependent: a "title" is special in the SVG
// namespace but not in the HTML namespace, and math runs like "mi"/"mo" are
// only special inside MathML.
//
// Input => output examples:
//   IsSpecialElement(TagId::kDiv,  NS::kHtml)  => true
//   IsSpecialElement(TagId::kTitle,NS::kHtml)  => false
//   IsSpecialElement(TagId::kTitle,NS::kSvg)   => true
//   IsSpecialElement(TagId::kDiv,  NS::kSvg)   => false
//   IsSpecialElement(TagId::kDiv,  NS::kXmlns) => false
//
// Edge cases: TagId::kUnknown and every element absent from the three
// namespace cases return false. The attribute namespaces (kXlink, kXml,
// kXmlns) never contain special elements and short-circuit to false. The
// switch is exhaustive over both enums, so a future enumerator added to
// "TagId" or "NS" must be consciously placed in or out of the special sets.
bool IsSpecialElement(TagId tag_id, NS ns);

// Quick test for the six numbered heading elements, used by the formatting
// element list and by the "reset the insertion mode appropriately" logic.
//
// Input => output examples:
//   IsNumberedHeader(TagId::kH1) => true
//   IsNumberedHeader(TagId::kH6) => true
//   IsNumberedHeader(TagId::kHr) => false
//   IsNumberedHeader(TagId::kUnknown) => false
bool IsNumberedHeader(TagId tag_id);

// Reports whether an element's raw text content must be reproduced verbatim
// (never entity-unescaped, never re-escaped on output). Elements such as
// "script", "style", and "xmp" have CDATA-like content, so the original byte
// sequence inside them is part of the document's meaning: rewriting "&amp;" to
// "&" there would corrupt the payload.
//
// Input => output examples:
//   HasUnescapedText("script", false) => true
//   HasUnescapedText("style",  false) => true
//   HasUnescapedText("noscript", false) => false
//   HasUnescapedText("noscript", true)  => true
//   HasUnescapedText("scriptingdisabled", true) => false
//
// Edge cases: the decision depends on the *name*, not the "TagId", because
// this predicate is called from code paths that have only the raw start-tag
// name available and may not have resolved it yet. "noscript" is special: it
// only keeps its text raw when scripting is enabled; with scripting disabled
// its content is ordinary parseable markup. Matching is case-sensitive — the
// tokenizer lowercases tag names beforehand, so mixed case must never reach
// this function.
bool HasUnescapedText(std::string_view tag_name, bool scripting_enabled);

// ---------------------------------------------------------------------------
// Unicode code point classification helpers
//
// The classes below describe the individual scalar values the HTML/XML
// tokenizers operate on. Everything here works in terms of *code points*
// (char32_t / the "Cp" enum), never UTF-16 code units, so surrogate handling
// is confined to the few helpers that decode them.
// ---------------------------------------------------------------------------

// Canonical code point values the tokenizer needs to name directly, chosen so
// the hot switch paths compare against named constants instead of magic
// numbers. kEof is deliberately not a code point: it is the sentinel the
// caller passes back to signal "input exhausted", and tokenizer logic must
// check for it before doing anything else, since arithmetic on it would be
// meaningless. The remaining values are their Unicode scalar values; for
// example kSpace is U+0020 (0x20).
enum class Cp : int32_t {
    kEof = -1,
    kNull = 0x00,
    kTabulation = 0x09,
    kCarriageReturn = 0x0d,
    kLineFeed = 0x0a,
    kFormFeed = 0x0c,
    kSpace = 0x20,
    kExclamationMark = 0x21,
    kQuotationMark = 0x22,
    kAmpersand = 0x26,
    kApostrophe = 0x27,
    kHyphenMinus = 0x2d,
    kSolidus = 0x2f,
    kDigit0 = 0x30,
    kDigit9 = 0x39,
    kSemicolon = 0x3b,
    kLessThanSign = 0x3c,
    kEqualsSign = 0x3d,
    kGreaterThanSign = 0x3e,
    kQuestionMark = 0x3f,
    kLatinCapitalA = 0x41,
    kLatinCapitalZ = 0x5a,
    kRightSquareBracket = 0x5d,
    kGraveAccent = 0x60,
    kLatinSmallA = 0x61,
    kLatinSmallZ = 0x7a,
};

// The U+FFFD replacement character, substituted wherever input bytes cannot be
// decoded into a valid Unicode scalar value. The constant is declared here with
// the other code-point vocabulary so every decoding path (character references,
// encoding recovery, surrogate cleanup) uses the same literal.
inline constexpr char32_t kReplacementCharacter = U'\uFFFD';

// Fixed byte strings that tokenizer states must recognize as multi-character
// runs. These are used with repeated character *equality* checks by the
// relevant state machine and are kept as string_view constants so the raw
// sequence stays visibly self-documenting at use sites.
namespace sequences {
inline constexpr std::string_view kDashDash = "--";
inline constexpr std::string_view kCdataStart = "[CDATA[";
inline constexpr std::string_view kDoctype = "doctype";
inline constexpr std::string_view kScript = "script";
inline constexpr std::string_view kPublic = "public";
inline constexpr std::string_view kSystem = "system";
} // namespace sequences

// True when the code point is an ASCII letter (a-z, A-Z) or digit (0-9). Used
// to validate character reference names, attribute value states, and the
// "ASCII alpha" requirement for things like "isindex" handling.
//
// Input => output examples:
//   IsAsciiAlphanumeric('5') => true
//   IsAsciiAlphanumeric('z') => true
//   IsAsciiAlphanumeric('Z') => true
//   IsAsciiAlphanumeric('_') => false
//   IsAsciiAlphanumeric(0x3b) => false
inline bool IsAsciiAlphanumeric(char32_t cp) {
    return (cp >= 0x30 && cp <= 0x39) || (cp >= 0x41 && cp <= 0x5a) ||
           (cp >= 0x61 && cp <= 0x7a);
}

// True when the code point falls inside either UTF-16 surrogate block
// (U+D800–U+DFFF). Such values are never valid scalar values; when a decoder
// encounters one it must be repaired (yielding the replacement character)
// rather than propagated, because emitting it would break serialization.
//
// Input => output examples:
//   IsSurrogate(0xd800) => true
//   IsSurrogate(0xdfff) => true
//   IsSurrogate(0xd7ff) => false
inline bool IsSurrogate(char32_t cp) {
    return cp >= 0xd800 && cp <= 0xdfff;
}

// True when the code unit is a *low* surrogate (U+DC00–U+DFFF). Together with
// a preceding high surrogate (U+D800–U+DBFF) it forms one decoded code point.
//
// Input => output examples:
//   IsSurrogatePair(0xdc00) => true
//   IsSurrogatePair(0xd800) => false
inline bool IsSurrogatePair(char32_t cp) {
    return cp >= 0xdc00 && cp <= 0xdfff;
}

// Combines a UTF-16 surrogate pair back into a single Unicode code point.
// "cp1" must be a high surrogate and "cp2" a low surrogate; the caller is
// expected to validate that with "IsSurrogate"/"IsSurrogatePair" first, since
// this routine performs no checks and returns a garbage value for malformed
// input.
//
// Input => output examples:
//   GetSurrogatePairCodePoint(0xd83d, 0xde00) => 0x1f600   (😀)
//   GetSurrogatePairCodePoint(0x4d,0x4e)       => garbage (malformed, unchecked)
inline char32_t GetSurrogatePairCodePoint(char32_t cp1, char32_t cp2) {
    return (cp1 - 0xd800) * 0x400 + 0x2400 + cp2;
}

// True for C0/C1 control code points, excluding NULL (U+0000) and the five
// ASCII whitespace characters (space, line feed, carriage return, tab, form
// feed). This is the "not NULL, not ASCII whitespace" control check used when
// deciding whether a second slash after a start-tag name is ignored and where
// the tokenizer must emit parse errors.
//
// Input => output examples:
//   IsControlCodePoint(0x01) => true
//   IsControlCodePoint(0x7f) => true
//   IsControlCodePoint(0x9f) => true
//   IsControlCodePoint(0x00) => false  (NULL is deliberately excluded)
//   IsControlCodePoint(0x20) => false  (space is whitespace, not control)
inline bool IsControlCodePoint(char32_t cp) {
    return (cp != 0x20 && cp != 0x0a && cp != 0x0d && cp != 0x09 && cp != 0x0c && cp >= 0x01 && cp <= 0x1f) ||
        (cp >= 0x7f && cp <= 0x9f);
}

// True when "cp" is a Unicode *noncharacter*: a code point permanently
// reserved for internal use and forbidden from interchange. The function
// covers the U+FDD0–U+FDEF range plus the last two code points of every
// plane (U+xFFFE and U+xFFFF, e.g. U+FFFE/U+FFFF and U+1FFFE/U+1FFFF).
// These values fail encoding recovery and must be substituted with the
// replacement character when found in input.
//
// Input => output examples:
//   IsUndefinedCodePoint(0xfddef)      => true
//   IsUndefinedCodePoint(0xffff)       => true
//   IsUndefinedCodePoint(0x1ffff)      => true
//   IsUndefinedCodePoint(0xfffd)       => false
//   IsUndefinedCodePoint(0x110000)     => false
bool IsUndefinedCodePoint(char32_t cp);

// ---------------------------------------------------------------------------
// srcset attribute parsing
//
// Guchho parses image candidate lists from "srcset" attributes so it can
// discover and rewrite every referenced resource URL while leaving width and
// pixel-density descriptors untouched. The grammar processed here is:
//
//   srcset = 1*srcset-entry *( "," srcset-entry )
//   srcset-entry = URL [ descriptor-list ]
//
// where the descriptor list is whitespace/comma-separated tokens such as
// "480w" or "2x". The parser never validates descriptors; it treats them as
// opaque strings to be preserved byte-for-byte.
// ---------------------------------------------------------------------------

// One candidate in a parsed "srcset" value: the URL text plus its optional
// descriptor string. The byte range [url_offset, url_offset + url_length)
// locates "url" within the *original* attribute string (the raw slice that
// produced the owning string via assignment), so a caller that wants to
// rewrite the resource in place can slice the source buffer directly without
// ever touching the descriptor tokens or the whitespace between candidates.
struct SrcsetEntry {
    std::string url;
    std::string descriptor;
    size_t url_offset = 0;
    size_t url_length = 0;
};

// Splits a "srcset" attribute value into its candidate entries, honoring the
// forgiving WHATWG grammar: an empty candidate introduced by a stray top-level
// comma is skipped, all surviving candidates are reported in source order, and
// the URL's byte range inside the input is recorded on each entry.
//
// Input => output examples:
//   ParseSrcset("a.jpg 480w, b.jpg 2x")
//       => [{url:"a.jpg", descriptor:"480w", offset:0, length:5},
//          {url:"b.jpg", descriptor:"2x",  offset:11, length:5}]
//   ParseSrcset("a.jpg, b.jpg")   => two entries, both empty descriptors
//   ParseSrcset("a.jpg, , b.jpg") => two entries (the bare "," candidate is dropped)
//   ParseSrcset("")               => empty vector
//   ParseSrcset("a.jpg,")         => one entry for "a.jpg" (trailing comma is ignored)
//
// Edge cases: internal and trailing whitespace is skipped, a URL never
// contains a comma (the first comma always terminates the URL run), repeated
// commas collapse into at most one dropped candidate, and descriptor tokens
// separated by bare commas are re-joined with a single space so the original
// token set is preserved even though separator detail is lost.
std::vector<SrcsetEntry> ParseSrcset(std::string_view srcset);

// Re-serializes parsed "srcset" entries into a single attribute string. Every
// entry is joined with exactly ", " regardless of how the original was
// spaced, and entries that carried no descriptor render as the bare URL. The
// result is always a valid "srcset" value and round-trips back to the same
// entries through "ParseSrcset".
//
// Input => output examples:
//   SerializeSrcset([{url:"a.jpg",descriptor:"480w"},{url:"b.jpg",descriptor:"2x"}])
//       => "a.jpg 480w, b.jpg 2x"
//   SerializeSrcset([{url:"a.jpg",descriptor:"480w"},{url:"b.jpg"}])
//       => "a.jpg 480w, b.jpg"
//   SerializeSrcset({}) => ""
//
// Edge cases: an empty entry list yields an empty string (no stray separator
// is emitted), and descriptor-less entries must not be given an extra space
// before a non-existent descriptor.
std::string SerializeSrcset(const std::vector<SrcsetEntry>& entries);

} // namespace guchho::html