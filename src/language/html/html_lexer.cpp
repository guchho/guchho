#include "guchho/html/html_lexer.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "guchho/entities.hpp"

namespace guchho::html {

// Guchho HTML tokenizer core.
//
// This translation unit implements the Guchho HTML lexer: the streaming state
// machine that walks an input string one code point at a time and converts it
// into a sequence of tokens (start tags, end tags, comments, doctypes,
// character runs, and end-of-file). The lexer is deliberately split into three
// cooperating layers inside this file:
//
//   1. The Tokenizer drives the outer loop, owns the currently-accumulated
//      token, and decides when to hand completed tokens to the parser.
//   2. The Preprocessor normalizes raw input before the tokenizer ever sees it
//      (newline collapsing, surrogate-pair joining, line/column tracking) and
//      supports retreating so an incomplete chunk of input can be re-read once
//      more data arrives.
//   3. The doctype rules at the bottom of this file classify a parsed
//      DOCTYPE token into a Guchho document mode (no-quirks, limited-quirks,
//      or quirks).
//
// The tokenizer never buffers the whole document tree; it only ever holds one
// partially built tag/comment/doctype token plus a pending run of character
// tokens, which keeps memory flat even for very large documents.

namespace {

constexpr std::u16string_view kDashDashSeq = u"--";
constexpr std::u16string_view kDoctypeSeq = u"doctype";
constexpr std::u16string_view kScriptSeq = u"script";
constexpr std::u16string_view kPublicSeq = u"public";
constexpr std::u16string_view kSystemSeq = u"system";
constexpr std::u16string_view kCdataStartSeq = u"[CDATA[";

// Guchho ASCII classification helpers.
//
// These predicates only answer questions about the 0x00-0x7F range; anything
// outside of it is handled by the Unicode-aware utilities in entities.hpp and
// the code-point tables. They exist so the tokenizer can make cheap, locale-
// independent decisions while scanning tag and attribute names.
//
//   IsAsciiLetter('A')  -> true
//   IsAsciiLetter(u'É') -> false
//   IsAsciiDigit('7')   -> true
//   IsWhitespace('\t')  -> true
//   IsWhitespace(' ')   -> true, IsWhitespace(U+00A0) -> false
//
// Edge case: HTML only treats SPACE, LF, TAB, and FF as tokenizer whitespace;
// vertical tab and non-breaking space are deliberately excluded because the
// HTML tokenization algorithm does not skip them between tag tokens.
bool IsAsciiDigit(char32_t cp) {
    return cp >= static_cast<char32_t>(Cp::kDigit0) && cp <= static_cast<char32_t>(Cp::kDigit9);
}

bool IsAsciiUpper(char32_t cp) {
    return cp >= static_cast<char32_t>(Cp::kLatinCapitalA) && cp <= static_cast<char32_t>(Cp::kLatinCapitalZ);
}

bool IsAsciiLower(char32_t cp) {
    return cp >= static_cast<char32_t>(Cp::kLatinSmallA) && cp <= static_cast<char32_t>(Cp::kLatinSmallZ);
}

bool IsAsciiLetter(char32_t cp) {
    return IsAsciiLower(cp) || IsAsciiUpper(cp);
}

char32_t ToAsciiLower(char32_t cp) {
    return cp + 0x20;
}

bool IsWhitespace(char32_t cp) {
    return cp == static_cast<char32_t>(Cp::kSpace) || cp == static_cast<char32_t>(Cp::kLineFeed) ||
           cp == static_cast<char32_t>(Cp::kTabulation) || cp == static_cast<char32_t>(Cp::kFormFeed);
}

// Predicate for the terminator that must follow the literal word "script" when
// Guchho decides whether a script-data double-escape sequence has begun or
// ended (for example `<!-- <script>` inside a <script> element flips the
// tokenizer between the escaped and double-escaped substates).
//
//   IsScriptDataDoubleEscapeSequenceEnd('>')  -> true
//   IsScriptDataDoubleEscapeSequenceEnd('/')  -> true
//   IsScriptDataDoubleEscapeSequenceEnd('e')  -> false
//
// Edge case: a word like "scripting" does NOT end the sequence because the
// next character after "script" is a letter rather than whitespace, '/', or
// '>'. Getting this wrong would desynchronize the nested-script example from
// the HTML specification, so the check must be exact.
bool IsScriptDataDoubleEscapeSequenceEnd(char32_t cp) {
    return IsWhitespace(cp) || cp == static_cast<char32_t>(Cp::kSolidus) ||
           cp == static_cast<char32_t>(Cp::kGreaterThanSign);
}

// Validates the numeric value produced by a numeric character reference such
// as &#65; or &#x1F600; and returns the Guchho parse-error code that the
// tokenizer must report for disallowed values, or std::nullopt when the value
// is acceptable.
//
//   GetErrorForNumericCharacterReference(65)     -> std::nullopt ('A' is fine)
//   GetErrorForNumericCharacterReference(0)      -> Err::kNullCharacterReference
//   GetErrorForNumericCharacterReference(0xD800) -> Err::kSurrogateCharacterReference
//   GetErrorForNumericCharacterReference(0x110000) -> Err::kCharacterReferenceOutsideUnicodeRange
//
// Edge cases, checked in this exact order so only the first applicable error
// is reported: null, above U+10FFFF, a UTF-16 surrogate, a noncharacter, and
// finally a C0/C1 control code point (U+000D counts as a control here even
// though it would otherwise be a legal line-break character). Note that
// reporting an error does not by itself decide the replacement value; the
// caller applies the replacement rules afterwards.
std::optional<Err> GetErrorForNumericCharacterReference(uint32_t code) {
    if (code == static_cast<uint32_t>(Cp::kNull)) {
        return Err::kNullCharacterReference;
    }
    if (code > 0x10ffff) {
        return Err::kCharacterReferenceOutsideUnicodeRange;
    }
    if (IsSurrogate(code)) {
        return Err::kSurrogateCharacterReference;
    }
    if (IsUndefinedCodePoint(code)) {
        return Err::kNoncharacterCharacterReference;
    }
    if (IsControlCodePoint(code) || code == static_cast<uint32_t>(Cp::kCarriageReturn)) {
        return Err::kControlCharacterReference;
    }

    return std::nullopt;
}

// Converts a slice of UTF-16 code units into UTF-8 and appends the result to
// `out`. Any surrogate pair encountered along the way is first combined into
// its full code point so that astral characters survive the conversion.
//
//   AppendUtf16(out, u"hi")          out gains the two bytes "hi"
//   AppendUtf16(out, u"\uD83D\uDE00") out gains the 4-byte UTF-8 encoding of U+1F600
//
// Edge cases: a high surrogate that is not followed by a low surrogate (or is
// the final unit of the slice) is passed through as-is and ends up encoded as
// a lone surrogate; the tokenizer's preprocessor normally rejects such input
// earlier with a surrogate-in-input-stream error, so this path should not be
// reached for well-formed documents.
void AppendUtf16(std::string& out, std::u16string_view text) {
    for (size_t i = 0; i < text.size(); i++) {
        char32_t cp = text[i];

        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 &&
            text[i + 1] <= 0xdfff) {
            cp = GetSurrogatePairCodePoint(cp, text[++i]);
        }

        AppendCodePoint(out, cp);
    }
}

// Convenience wrapper that materializes a whole UTF-16 slice as an owned UTF-8
// string. Guchho stores token payloads (tag names, attribute values, comment
// data) as UTF-8, while the tokenizer walks input as UTF-16, so this bridge is
// used whenever a completed piece of text is copied into a token field.
//
//   ToUtf8(u"caf\u00E9") -> "café" (2-byte UTF-8 for U+00E9)
//   ToUtf8(u"")          -> ""
//
// Edge case: an empty input yields an empty output without allocating any
// surrogate handling work.
std::string ToUtf8(std::u16string_view text) {
    std::string out;
    AppendUtf16(out, text);
    return out;
}

}

// Encodes a single Unicode code point as UTF-8 and appends it to `out`. This
// is the lowest-level text writer in the Guchho lexer: every other function
// that grows a token's UTF-8 payload funnels through it.
//
//   AppendCodePoint(out, 0x41)     out gains 'A'          (1 byte)
//   AppendCodePoint(out, 0xE9)     out gains 0xC3 0xA9    (2 bytes)
//   AppendCodePoint(out, 0x20AC)   out gains 3 bytes
//   AppendCodePoint(out, 0x1F600)  out gains 4 bytes
//
// Edge cases: the four classic UTF-8 length branches are selected purely by
// magnitude, so code points in the surrogate range (0xD800-0xDFFF) and values
// above 0x10FFFF would be encoded literally if ever passed in; callers are
// expected to have validated or replaced such values beforehand (see
// GetErrorForNumericCharacterReference and the preprocessor's surrogate
// handling). No BOM or overlong-form checks are performed here.
void AppendCodePoint(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
}

// Decodes a UTF-8 byte string into a UTF-16 code-unit string. The lexer uses
// this when a UTF-8 token field (for example the accumulated tag name) must be
// compared against or fed back into UTF-16 oriented APIs such as the
// preprocessor's lookahead matching.
//
//   ToUtf16("html")     -> u"html"
//   ToUtf16("\xF0\x9F\x98\x80") -> u"\xD83D\uDE00" (U+1F600 as a surrogate pair)
//
// Edge cases: code points at or above U+10000 are split into a high/low
// surrogate pair; the function assumes `text` is well-formed UTF-8 and will
// read continuation bytes without bounds re-checking if the input is
// truncated or malformed, so only lexer-produced (already validated) strings
// should be passed in.
std::u16string ToUtf16(std::string_view text) {
    std::u16string out;

    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const size_t size = text.size();

    for (size_t i = 0; i < size;) {
        uint32_t cp = bytes[i];

        if (cp < 0x80) {
            i += 1;
        } else if ((cp & 0xe0) == 0xc0) {
            cp = ((cp & 0x1f) << 6) | (bytes[i + 1] & 0x3f);
            i += 2;
        } else if ((cp & 0xf0) == 0xe0) {
            cp = ((cp & 0x0f) << 12) | ((bytes[i + 1] & 0x3fu) << 6) | (bytes[i + 2] & 0x3fu);
            i += 3;
        } else {
            cp = ((cp & 0x07) << 18) | ((bytes[i + 1] & 0x3fu) << 12) | ((bytes[i + 2] & 0x3fu) << 6) |
                 (bytes[i + 3] & 0x3fu);
            i += 4;
        }

        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xd800 | (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xdc00 | (cp & 0x3ff)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }

    return out;
}

// Looks up an attribute on a tag token by name and returns a pointer to its
// value, or nullptr when the attribute is absent. The scan runs backwards so
// that when duplicate attribute names appear in the markup, the value that was
// seen last wins the lookup used by Guchho's duplicate detection.
//
//   attrs = [{name:"id", value:"a"}, {name:"id", value:"b"}]
//   GetTokenAttr(token, "id")  -> points at "b"
//   GetTokenAttr(token, "href") -> nullptr
//
// Edge case: attribute names are already lowercased by the tokenizer before
// they are stored, so the comparison here is exact and case-sensitive; a
// caller that passes "ID" for a stored "id" will not match. The returned
// pointer aliases memory owned by `token` and is invalidated as soon as the
// token's attribute list is mutated.
const std::string* GetTokenAttr(const TagToken& token, std::string_view attr_name) {
    for (auto it = token.attrs.rbegin(); it != token.attrs.rend(); ++it) {
        if (it->name == attr_name) {
            return &it->value;
        }
    }

    return nullptr;
}

// Builds a Guchho tokenizer bound to `handler`, which receives every
// completed token and parse error, and opts in or out of source-location
// tracking. When `source_code_location_info` is false the tokenizer skips all
// location bookkeeping and GetCurrentLocation returns nullptr for every token,
// which is the fast path Guchho uses when line/column information is not
// needed downstream.
//
// The constructor primes current_location_ at the very start of the input so
// that the first token (often a character run beginning at line 1, column 1)
// gets a sane location even before any input has been written.
//
// Edge case: the handler must outlive the tokenizer; Guchho's parser owns both
// and destroys the tokenizer first.
Tokenizer::Tokenizer(TokenHandler& handler, bool source_code_location_info)
    : handler_(handler), source_code_location_info_(source_code_location_info) {
    current_location_ = GetCurrentLocation(-1);
}

// Parse-error reporting for the tokenizer layer. Each call materializes a
// ParserError with the current line/column (shifted by `cp_offset` code
// points) and forwards it to the handler's on_parse_error callback, when one is
// installed. The callback is optional: Guchho callers that do not care about
// diagnostics simply leave it unset and every report becomes a no-op.
//
//   ReportError(Err::kEofInTag, 0) -> on_parse_error invoked with the position
//                                     at which the tag was left unterminated
//
// Edge case: cp_offset may be negative (used when the error's true origin is a
// few code points behind the current cursor) but must never push the reported
// column across a line boundary, because errors are always attributed to a
// single line.
void Tokenizer::ReportError(Err code, int32_t cp_offset) {
    if (handler_.on_parse_error) {
        handler_.on_parse_error(preprocessor.GetError(code, cp_offset));
    }
}

// Allocates a fresh Location out of the tokenizer's bump pool and returns a
// raw pointer to it. The pool keeps every Location alive for the whole
// tokenization run so tokens can safely hold non-owning location pointers
// without worrying about lifetimes.
//
// The start line is taken from the preprocessor immediately; columns and
// offsets are left at -1 as a sentinel meaning "not yet positioned", to be
// filled in by GetCurrentLocation or PrepareToken once the exact cursor is
// known.
//
// Edge case: when location tracking is disabled the caller never reaches this
// function, so no memory is allocated for tokens in the fast path.
Location* Tokenizer::NewLocation() {
    auto location = std::make_unique<Location>();
    location->start_line = preprocessor.line;
    location->start_col = -1;
    location->start_offset = -1;
    location->end_line = -1;
    location->end_col = -1;
    location->end_offset = -1;

    Location* result = location.get();
    location_pool_.push_back(std::move(location));
    return result;
}

// Creates a Location whose start column and offset are the current cursor
// shifted back by `offset` code points, letting a token point at the character
// that opened it (for example the '<' of a tag) even though the tokenizer has
// already advanced past that character. Returns nullptr when location tracking
// is disabled.
//
//   input "<div>", cursor on 'd' after seeing '<'
//   GetCurrentLocation(1) -> Location starting at the '<'
//
// Edge cases: offset -1 means "start one code point ahead of the cursor" and is
// used to open the location for the token that is about to begin; the offset
// must never be large enough to cross a line boundary, otherwise the reported
// column would be nonsense. The returned pointer stays valid for the entire
// parse because locations live in the tokenizer's pool.
Location* Tokenizer::GetCurrentLocation(int32_t offset) {
    if (!source_code_location_info_) {
        return nullptr;
    }

    Location* location = NewLocation();
    location->start_col = preprocessor.Col() - offset;
    location->start_offset = preprocessor.Offset() - offset;
    return location;
}

// Decides whether the tokenizer must suspend because the current input chunk
// was exhausted before a complete token could be produced. If so, it rewinds
// the preprocessor by the number of code points consumed since the last
// snapshot, clears the counter, and deactivates the tokenizer so that the next
// Write call resumes from exactly the same position with fresh input.
//
//   chunk ends mid-token -> EnsureHibernation() returns true, cursor rewinds,
//                           RunParsingLoop exits without calling CallState
//   no chunk boundary hit -> returns false, the current code point proceeds
//                            to the state machine
//
// Edge cases: rewinding is only legal within the current chunk, which is why
// AdvanceBy and Consume carefully maintain consumed_after_snapshot_; the gap
// bookkeeping inside the preprocessor makes sure CR/LF pairs collapsed during
// this window are also restored correctly on retreat.
bool Tokenizer::EnsureHibernation() {
    if (!preprocessor.end_of_chunk_hit) {
        return false;
    }

    preprocessor.Retreat(consumed_after_snapshot_);
    consumed_after_snapshot_ = 0;
    active = false;

    return true;
}

// The tokenizer's outer pump. RunParsingLoop pulls one code point at a time
// from the preprocessor and dispatches it to the current state handler until
// the tokenizer becomes inactive (end of input) or is paused by the parser.
//
//   Write(u"<p>hi") -> loop runs for '<', 'p', '>', 'h', 'i'
//                    -> OnStartTag fired at '>', OnCharacter fired for "hi"
//
// Edge cases: the in_loop_ guard makes the function re-entrant so a state
// handler (or a synchronous Resume) can trigger nested work without ever
// running two loops at once; every iteration resets consumed_after_snapshot_
// so EnsureHibernation can rewind exactly the units consumed in that single
// step when the current input chunk runs dry mid-token.
void Tokenizer::RunParsingLoop() {
    if (in_loop_) {
        return;
    }

    in_loop_ = true;

    while (active && !paused_) {
        consumed_after_snapshot_ = 0;

        const char32_t cp = Consume();

        if (!EnsureHibernation()) {
            CallState(cp);
        }
    }

    in_loop_ = false;
}

// Pulls the next normalized code point from the preprocessor while counting it
// against the current loop iteration so a hibernation rewind can undo it. This
// is the only sanctioned way for the state machine to advance the cursor one
// step.
//
//   Consume() at 'a' of "abc" -> returns 'a', cursor now on 'b',
//                                consumed_after_snapshot_ incremented by 1
//
// Edge case: at the end of available input this returns U+0000 (the EOF
// sentinel) and marks end_of_chunk_hit when more input may still arrive, in
// which case EnsureHibernation rewinds before the EOF is ever interpreted.
char32_t Tokenizer::Consume() {
    ++consumed_after_snapshot_;
    return preprocessor.Advance();
}

// Marks the tokenizer as paused. Pause is called by the parser when it needs
// to interleave work (for example switching tree-construction modes) between
// tokens; the outer loop checks paused_ after every dispatched code point and
// exits cleanly, leaving all state intact for a later Resume.
//
//   StateData dispatches a tag -> parser calls Pause() -> loop stops
//                                    after that code point is fully handled
//
// Edge case: pausing while already paused is harmless; the flag is simply set
// again. Pausing never abandons a half-built token: the state machine only
// stops between code points, never in the middle of one.
void Tokenizer::Pause() {
    paused_ = true;
}

// Clears the paused flag and, unless the tokenizer is already inside
// RunParsingLoop (a synchronous resume triggered from within the loop itself),
// restarts the pump so tokenization continues from the preserved state.
//
//   Pause() while on tag-name state -> Resume() -> next code point continues
//                                      exactly where the name left off
//
// Edge cases: resuming a tokenizer that was never paused throws
// std::runtime_error, because a double resume would silently desynchronize the
// parser's expectations; when in_loop_ is already true the flag clear alone is
// sufficient because the active loop will pick it up on its next condition
// check.
void Tokenizer::Resume() {
    if (!paused_) {
        throw std::runtime_error("Parser was already resumed");
    }

    paused_ = false;

    if (in_loop_) {
        return;
    }

    RunParsingLoop();
}

// Appends a new chunk of UTF-16 input (flagging whether it is the final one),
// wires the handler's error callback into the preprocessor, and restarts the
// parsing loop. This is the primary streaming entry point of the Guchho
// tokenizer: callers may feed the document in arbitrarily small pieces.
//
//   Write(u"<p>hi", true) -> tokens for <p> and "hi" are delivered, then EOF
//   Write(u"<p>", false) + Write(u">x", true) -> the tag completes across chunks
//
// Edge case: if a previous chunk left the tokenizer hibernating mid-token,
// Write resumes at the exact rewound position so no input is skipped or
// duplicated; is_last_chunk = false merely means "more may come", which is what
// keeps EnsureHibernation from treating a premature end as real EOF.
void Tokenizer::Write(std::u16string chunk, bool is_last_chunk) {
    active = true;
    preprocessor.on_parse_error = handler_.on_parse_error;
    preprocessor.Write(std::move(chunk), is_last_chunk);
    RunParsingLoop();
}

// Splices a fragment of markup directly in front of the code point that would
// be read next and immediately resumes parsing. Guchho uses this to inject
// content at the current cursor (for example script-driven document.write
// style input) without disturbing text that has already been consumed.
//
//   cursor between "ab" and "cd", InsertHtmlAtCurrentPos(u"XY")
//   -> effective stream becomes "abXYcd" from this point on
//
// Edge cases: the insert point is pos + 1 (the next character to be read), so
// the just-consumed code point is never displaced; like Write, it reactivates
// the tokenizer and clears the hibernation flag so the loop can continue.
void Tokenizer::InsertHtmlAtCurrentPos(std::u16string_view chunk) {
    active = true;
    preprocessor.on_parse_error = handler_.on_parse_error;
    preprocessor.InsertHtmlAtCurrentPos(chunk);
    RunParsingLoop();
}

// Advances the preprocessor by `count` additional code points and charges them
// to the current iteration's consumption counter. Multi-character lookbehind
// decisions (like matching an end-tag name that has already been partially
// consumed) use this to slide the cursor forward in one coordinated step.
//
//   cursor on the first unit of a matched 6-unit pattern -> AdvanceBy(5)
//   leaves the cursor on the last unit of the pattern
//
// Edge case: count must be non-negative and small enough that the resulting
// position stays within the current chunk; over-advancing would break the
// rewind arithmetic that EnsureHibernation relies on.
void Tokenizer::AdvanceBy(int32_t count) {
    consumed_after_snapshot_ += count;
    for (int32_t i = 0; i < count; i++) {
        preprocessor.Advance();
    }
}

// Tests whether the text starting at the current cursor matches `pattern`
// (case-insensitively when requested) and, on success, consumes the rest of the
// match. The very first code point of the pattern has already been consumed by
// the loop before this helper is invoked, so only pattern.size() - 1 further
// units are advanced.
//
//   cursor sitting on 'D' with input "DOCTYPE ..." and pattern u"doctype"
//   case-insensitive -> returns true, cursor lands on the final 'E'
//   cursor on 'X'      -> returns false, cursor unchanged
//
// Edge cases: a pattern that is empty would attempt to advance by -1, so only
// non-empty literal sequences such as "--", "doctype", "public", "system", and
// "[CDATA[" are ever passed; if the pattern runs past the end of available
// input, StartsWith reports a chunk boundary and this returns false so the
// tokenizer can hibernate and retry with more data.
bool Tokenizer::ConsumeSequenceIfMatch(std::u16string_view pattern, bool case_sensitive) {
    if (preprocessor.StartsWith(pattern, case_sensitive)) {
        AdvanceBy(static_cast<int32_t>(pattern.size()) - 1);
        return true;
    }
    return false;
}

// Token construction helpers. Each Create* function resets the reusable token
// slot with a default-constructed value, stamps on the right token type, and
// attaches a source location whose start is backed up by the number of code
// points already consumed for the token's opening delimiter. Guchho reuses a
// single tag/comment/doctype member per tokenizer instead of allocating on
// every token to keep the hot path allocation-free.
//
//   input "  <div>", CreateStartTagToken called on 'd'
//   -> GetCurrentLocation(1) points the token at the '<'
//   input "</div>",  CreateEndTagToken called on 'd'
//   -> GetCurrentLocation(2) points the token at the '<' of "</"
//
// Edge cases: these functions assume any previously built token has already
// been emitted; overwriting an unemitted token would silently drop it. When
// location tracking is off, GetCurrentLocation yields nullptr and the token
// simply carries no position.
void Tokenizer::CreateStartTagToken() {
    tag_token_ = TagToken{};
    tag_token_.type = TokenType::kStartTag;
    tag_token_.location = GetCurrentLocation(1);
}

void Tokenizer::CreateEndTagToken() {
    tag_token_ = TagToken{};
    tag_token_.type = TokenType::kEndTag;
    tag_token_.location = GetCurrentLocation(2);
}

void Tokenizer::CreateCommentToken(int32_t offset) {
    comment_token_ = CommentToken{};
    comment_token_.location = GetCurrentLocation(offset);
}

// Starts a fresh DOCTYPE token, optionally seeding its name with the first
// character that triggered the transition into the doctype-name state. Seeding
// avoids re-examining that character later, which matters because the very
// first name character may need ASCII lowercasing or null replacement before it
// is ever appended.
//
//   input "<!DOCTYPE html>", called at 'h' -> has_name true, name "h" so far
//   input "<!DOCTYPE html>", called with nullptr (no name yet, e.g. EOF)
//   -> has_name false, caller decides whether to force quirks mode
//
// Edge case: the token's location is the current_location_ recorded when the
// markup declaration was recognized, so the token spans the whole
// "<!DOCTYPE ...>" construct rather than starting at the name.
void Tokenizer::CreateDoctypeToken(const std::u16string* initial_name) {
    doctype_token_ = DoctypeToken{};
    doctype_token_.location = current_location_;

    if (initial_name != nullptr) {
        doctype_token_.has_name = true;
        doctype_token_.name = ToUtf8(*initial_name);
    }
}

// Starts a new pending run of character tokens with the given type (normal
// character, whitespace, or null), attaching the current location and copying
// the first code units in. Character tokens are the only tokens the tokenizer
// accumulates across many code points before flushing, which lets consecutive
// text collapse into a single OnCharacter call.
//
//   EmitCodePoint('a') then EmitCodePoint('b') -> one CharacterToken "ab"
//   whitespace then text -> separate runs, flushed when the type changes
//
// Edge case: the token is heap-allocated because character runs can be
// arbitrarily long; current_character_token_ owns it until the run is flushed
// by EmitCurrentCharacterToken.
void Tokenizer::CreateCharacterToken(TokenType type, std::u16string_view chars) {
    current_character_token_ = std::make_unique<CharacterToken>();
    current_character_token_->type = type;
    current_character_token_->location = current_location_;
    AppendUtf16(current_character_token_->chars, chars);
}

// Starts collecting a new attribute for the tag currently being built. The
// first code point of the name (when it is not the NUL sentinel) is written
// immediately, current_attr_index_ is reset to -1 to signal "attribute not yet
// committed to the token", and a location is opened for the attribute so its
// span can be closed later by LeaveAttrValue.
//
//   input `<img src=` -> called at 's' with 's' as attr_name_first_ch,
//                        attribute begins accumulating "src"
//   input `<img =x>`   -> called with '=' (the unexpected-equals error path),
//                         '=' itself becomes part of the attribute name
//
// Edge case: a NUL first character means "start with an empty name and let the
// AttributeName state feed in the real characters one at a time".
void Tokenizer::CreateAttr(char32_t attr_name_first_ch) {
    current_attr_ = Attribute{};

    if (attr_name_first_ch != U'\0') {
        AppendCodePoint(current_attr_.name, attr_name_first_ch);
    }

    current_attr_index_ = -1;
    current_location_ = GetCurrentLocation(0);
}

// Returns a mutable reference to the attribute value currently being written.
// Once the attribute has been committed to the tag token (index >= 0) the
// reference targets the copy inside tag_token_; otherwise it targets the
// staging attribute still being assembled.
//
//   before LeaveAttrName -> writes go into current_attr_.value
//   after  LeaveAttrName -> writes go straight into tag_token_.attrs[i].value
//
// Edge case: character references inside attribute values rely on this single
// sink so that decoded characters land in the right place regardless of whether
// the attribute was committed before or after the reference appeared.
std::string& Tokenizer::CurrentAttrValue() {
    return current_attr_index_ >= 0 ? tag_token_.attrs[static_cast<size_t>(current_attr_index_)].value
                                    : current_attr_.value;
}

// Finalizes the attribute name currently being staged. If the name is not
// already present on the token, the staged attribute is appended, its index is
// recorded, and LeaveAttrValue stamps the end of its source location. If the
// name is a duplicate, Guchho reports Err::kDuplicateAttribute and discards the
// staged attribute by leaving current_attr_index_ at -1, so later value
// characters are silently dropped.
//
//   `<a id=1 id=2>` -> first "id" is kept with value "1"; the second name
//                      triggers kDuplicateAttribute and is not stored
//   `<a href=x>`     -> attribute committed normally, index recorded
//
// Edge case: detection compares against stored (lowercased) names exactly, so
// only genuinely repeated names are rejected; discarding must not disturb the
// attributes that were already accepted.
void Tokenizer::LeaveAttrName() {
    if (GetTokenAttr(tag_token_, current_attr_.name) == nullptr) {
        tag_token_.attrs.push_back(current_attr_);
        current_attr_index_ = static_cast<int32_t>(tag_token_.attrs.size()) - 1;

        LeaveAttrValue();
    } else {
        ReportError(Err::kDuplicateAttribute);
        current_attr_index_ = -1;
    }
}

// Records the end of the current attribute's source location using the
// preprocessor's present line, column, and offset. Called whenever attribute
// scanning finishes (at whitespace, '/', or '>' so that the span covers the
// value as well). Does nothing when location tracking is disabled.
//
//   `<a href="x">` -> location->end_* set to the cursor just after the closing
//                     quote when the tag completes
//
// Edge case: if the attribute was rejected as a duplicate there is no live
// location to close, but calling this is still safe because the guard simply
// skips the write when current_location_ is null.
void Tokenizer::LeaveAttrValue() {
    if (current_location_ != nullptr) {
        current_location_->end_line = preprocessor.line;
        current_location_->end_col = preprocessor.Col();
        current_location_->end_offset = preprocessor.Offset();
    }
}

// Common preamble for every token emission: flushes any pending character run
// so text always reaches the parser before the token that interrupts it, seals
// the outgoing token's end location, and opens a fresh location for whatever
// token begins next. Always returns nullptr so call sites can write
// `location = PrepareToken(...)` when they need the cleared pointer.
//
//   emitting <p> while characters "ab" are pending
//   -> OnCharacter("ab") fires first, then <p>'s end_* fields are written
//
// Edge case: the end position is computed as Col()/Offset() + 1 because the
// current cursor sits on the '>' that closes the token, and the '>' belongs to
// the token being sealed; when location tracking is off everything here is a
// no-op apart from the character flush.
Location* Tokenizer::PrepareToken(Location* next_location) {
    EmitCurrentCharacterToken(next_location);

    if (next_location != nullptr) {
        next_location->end_line = preprocessor.line;
        next_location->end_col = preprocessor.Col() + 1;
        next_location->end_offset = preprocessor.Offset() + 1;
    }

    current_location_ = GetCurrentLocation(-1);
    return nullptr;
}

// Hands the fully assembled tag token to the parser. The tag's textual name is
// resolved to a TagId up front so downstream stages never repeat the string
// lookup, and start tags additionally record last_start_tag_name, which the
// RAWTEXT/RCDATA/script end-tag matching depends on. End tags are validated:
// attributes and a trailing solidus are illegal there and produce dedicated
// parse errors before the token is forwarded.
//
//   input "<BR>"  -> OnStartTag with tag_name "br", tag_id for BR,
//                    last_start_tag_name set to u"br"
//   input "</br id=x>" -> kEndTagWithAttributes reported, then OnEndTag fired
//   input "</br/>"     -> kEndTagWithTrailingSolidus reported, then OnEndTag
//
// Edge case: tags are always lowercased during the TagName state, so both the
// stored name and last_start_tag_name are lowercase regardless of source
// casing.
void Tokenizer::EmitCurrentTagToken() {
    PrepareToken(tag_token_.location);

    tag_token_.tag_id = GetTagId(tag_token_.tag_name);

    if (tag_token_.type == TokenType::kStartTag) {
        last_start_tag_name = ToUtf16(tag_token_.tag_name);
        handler_.OnStartTag(tag_token_);
    } else {
        if (!tag_token_.attrs.empty()) {
            ReportError(Err::kEndTagWithAttributes);
        }

        if (tag_token_.self_closing) {
            ReportError(Err::kEndTagWithTrailingSolidus);
        }

        handler_.OnEndTag(tag_token_);
    }
}

// Seals the comment token's location and delivers it. Used by the real comment
// states as well as the bogus-comment recovery path, all of which funnel into
// the same emission so the parser sees every comment exactly once.
//
//   input "<!-- hi -->" -> OnComment with data " hi "
//   input "<?x>"          -> OnComment with data "?x" (bogus comment)
//
// Edge case: comments that hit EOF before "-->" are still emitted (with an
// kEofInComment error already reported), so no buffered comment text is lost.
void Tokenizer::EmitCurrentComment(CommentToken& ct) {
    PrepareToken(ct.location);
    handler_.OnComment(ct);
}

// Seals the doctype token's location and delivers it to the parser, which will
// later consult IsConformingDoctype/GetDocumentMode to pick the document mode.
// Works for well-formed, truncated, and bogus doctypes alike.
//
//   input "<!DOCTYPE html>" -> OnDoctype with name "html", no ids
//   input "<!DOCTYPE html SYSTEM 'x'>" -> system_id "x"
//
// Edge case: EOF mid-doctype still emits the token (with force_quirks already
// set by the state that detected the truncation) before EOF is reported.
void Tokenizer::EmitCurrentDoctype(DoctypeToken& ct) {
    PrepareToken(ct.location);
    handler_.OnDoctype(ct);
}

// Flushes the pending character run, if any, to the appropriate handler
// callback based on its type, then releases it. The run's end location is
// aligned with the start of the token that interrupted it so the two spans tile
// the input without gaps or overlaps.
//
//   pending run of spaces followed by a tag token
//   -> OnWhitespaceCharacter fired, end_* copied from the tag's start_*
//   pending run of null characters -> OnNullCharacter fired instead
//
// Edge case: when next_location is null (location tracking disabled, or EOF
// with no successor) the location alignment is skipped but the characters are
// still delivered; unknown token types fall through the switch without action.
void Tokenizer::EmitCurrentCharacterToken(Location* next_location) {
    if (current_character_token_) {
        if (next_location && current_character_token_->location) {
            current_character_token_->location->end_line = next_location->start_line;
            current_character_token_->location->end_col = next_location->start_col;
            current_character_token_->location->end_offset = next_location->start_offset;
        }

        switch (current_character_token_->type) {
            case TokenType::kCharacter: {
                handler_.OnCharacter(*current_character_token_);
                break;
            }
            case TokenType::kNullCharacter: {
                handler_.OnNullCharacter(*current_character_token_);
                break;
            }
            case TokenType::kWhitespaceCharacter: {
                handler_.OnWhitespaceCharacter(*current_character_token_);
                break;
            }
            default:
                break;
        }

        current_character_token_ = nullptr;
    }
}

// Produces the terminal EOF token and deactivates the tokenizer. Any pending
// character run is flushed first, so the parser always observes "text, then
// EOF". The EOF token's location is a zero-width point (end equals start)
// because there is no source character to cover.
//
//   input "abc" -> OnCharacter("abc"), then OnEof, active set to false
//   input "<p>" (unterminated tag) -> kEofInTag was reported earlier; OnEof
//                                     still arrives after whatever was emitted
//
// Edge case: calling this with location tracking off still fires OnEof, just
// with a null location; once active is false the outer loop terminates.
void Tokenizer::EmitEofToken() {
    Location* location = GetCurrentLocation(0);

    if (location) {
        location->end_line = location->start_line;
        location->end_col = location->start_col;
        location->end_offset = location->start_offset;
    }

    EmitCurrentCharacterToken(location);

    EofToken token;
    token.location = location;
    handler_.OnEof(token);

    active = false;
}

// Appends a slice of text to the pending character run, matching its type
// first. If a run of a different type is already open (for example whitespace
// followed by a letter), the old run is flushed with a fresh location before a
// new token of the requested type is started, so consecutive text of mixed
// kinds is split into the correct sequence of callbacks.
//
//   'a','b' as kCharacter -> one run "ab"
//   then a space as kWhitespaceCharacter -> run "ab" flushed, new run " "
//
// Edge case: this function never flushes a run of the same type, which is what
// allows arbitrarily long text to accumulate into a single token without
// repeated handler calls.
void Tokenizer::AppendCharToCurrentCharacterToken(TokenType type, std::u16string_view ch) {
    if (current_character_token_) {
        if (current_character_token_->type == type) {
            AppendUtf16(current_character_token_->chars, ch);
            return;
        }

        current_location_ = GetCurrentLocation(0);
        EmitCurrentCharacterToken(current_location_);
    }

    CreateCharacterToken(type, ch);
}

// Classifies a single code point as whitespace, null, or ordinary text,
// encodes it (as one or two UTF-16 units when it lies beyond the BMP), and
// appends it to the pending character run. This is the universal text exit
// used by almost every data-like state.
//
//   EmitCodePoint(' ')  -> whitespace-character run
//   EmitCodePoint('\0') -> null-character run
//   EmitCodePoint(U+1F600) -> character run containing the surrogate pair
//
// Edge case: the classification happens before encoding, so a null never
// silently merges into a normal text run, and the parser can treat U+0000
// distinctly even though the run builder is shared.
void Tokenizer::EmitCodePoint(char32_t cp) {
    const TokenType type = IsWhitespace(cp)
                               ? TokenType::kWhitespaceCharacter
                               : cp == static_cast<char32_t>(Cp::kNull) ? TokenType::kNullCharacter
                                                                        : TokenType::kCharacter;

    std::u16string buf;

    if (cp < 0x10000) {
        buf.push_back(static_cast<char16_t>(cp));
    } else {
        const char32_t offset = cp - 0x10000;
        buf.push_back(static_cast<char16_t>(0xd800 | (offset >> 10)));
        buf.push_back(static_cast<char16_t>(0xdc00 | (offset & 0x3ff)));
    }

    AppendCharToCurrentCharacterToken(type, buf);
}

// Appends text to the pending run as ordinary (non-whitespace, non-null)
// characters. Callers use this when they already know the literal content --
// recovery text like a literal "<", replacement characters, or comment-close
// fragments -- which lets them skip the per-code-point classification that
// EmitCodePoint performs.
//
//   EmitChars(u"<")  -> grows the current character run with '<'
//   EmitChars(u"\uFFFD") -> grows the run with the replacement character
//
// Edge case: if a whitespace or null run is currently open, the run is flushed
// first and a new character-type run begins, keeping callback types clean.
void Tokenizer::EmitChars(std::u16string_view ch) {
    AppendCharToCurrentCharacterToken(TokenType::kCharacter, ch);
}

// Character-reference entry point. Whenever the data-like states or the
// attribute-value states see a '&', they hand control to this function, which
// saves the current state in return_state_ (so the reference resolver knows
// where to hand control back to) and records the position just after the '&'
// for later rewinding if no valid entity materializes.
//
//   in Data state reading "a &amp; b" at '&'
//   -> return_state_ = kData, entity_start_pos_ = cursor on 'a' of "amp;"
//
// Edge cases: the return state is always one of the data-like or attribute
// value states, never an intermediate state, because a '&' can only be
// consumated while scanning literal text; nested references are impossible
// because the resolver runs to completion before handing control back.
void Tokenizer::StartCharacterReference() {
    return_state_ = state;
    state = State::kCharacterReference;
    entity_start_pos_ = preprocessor.pos;
}

// Reports whether the pending character reference occurred inside an
// attribute value rather than in element text. The two contexts differ
// subtly: unquoted attribute values may not legally contain whitespace after a
// resolved reference, and the resolver's named-entity matching rules treat
// attributes (where '='-terminated matches are allowed) differently.
//
//   return_state_ == kAttributeValueDoubleQuoted  -> true
//   return_state_ == kAttributeValueSingleQuoted  -> true
//   return_state_ == kAttributeValueUnquoted      -> true
//   return_state_ == kData                        -> false
//
// Edge case: the three quoted/unquoted attribute states are the complete set
// of attribute contexts in Guchho; the self-closing and after-value states
// never hold a '&', so this predicate cannot be mistaken about ownership.
bool Tokenizer::IsCharacterReferenceInAttribute() const {
    return return_state_ == State::kAttributeValueDoubleQuoted ||
           return_state_ == State::kAttributeValueSingleQuoted ||
           return_state_ == State::kAttributeValueUnquoted;
}

// Directs one resolved code point to its destination: inside an attribute it
// is appended to the value being assembled (bypassing the character-token
// pipeline entirely); in element content it flows through the usual
// EmitCodePoint classification so whitespace/null distinctions are preserved.
//
//   reference &#10; inside an attribute -> attribute value gains a newline
//   reference &#65; in plain text       -> character run gains 'A'
//
// Edge case: the null replacement (U+FFFD) and whitespace code points both
// take this same path, so an attribute value can legally contain characters
// that would be reclassified if they went through EmitCodePoint.
void Tokenizer::FlushCodePointConsumedAsCharacterReference(char32_t cp) {
    if (IsCharacterReferenceInAttribute()) {
        AppendCodePoint(CurrentAttrValue(), cp);
    } else {
        EmitCodePoint(cp);
    }
}

// Decodes a numeric character reference (the "&#..." or "&#x..." forms) that
// the character-reference state just entered. Parses either decimal or
// hexadecimal digits, saturates absurdly large values so the accumulator never
// overflows, and applies the numeric-reference validity/error rules. Writes
// the decoded value(s) into `codepoints` and the count into `cp_count`, then
// returns the number of code units consumed from the input (not counting the
// leading '&').
//
//   input "&#65;xyz"  -> codepoints={65,0}, cp_count=1, returns 3 ('#'+2args+';')
//                        actually returns 4: "#" "6" "5" ";"
//   input "&#x1F600;"-> codepoints={0x1F600,0}, cp_count=1, returns 7 units
//   input "&#;"      -> returns 0, no digits, caller falls back to literal "&#"
//
// Edge cases: a missing semicolon is reported but still consumed and decoded
// as long as digits exist; values of zero, surrogate values, or anything above
// U+10FFFF are replaced with U+FFFD after reporting the matching error; the
// 0x110001 saturation marker guarantees kCharacterReferenceOutsideUnicodeRange
// is always flagged for gratuitously huge inputs rather than wrapping around.
int32_t Tokenizer::DecodeNumericEntity(uint32_t (&codepoints)[2], size_t& cp_count) {
    const std::u16string_view input(preprocessor.html);
    const int32_t size = static_cast<int32_t>(input.size());

    int32_t cursor = preprocessor.pos + 1;

    int radix = 10;
    if (cursor < size && (input[static_cast<size_t>(cursor)] == u'x' ||
                          input[static_cast<size_t>(cursor)] == u'X')) {
        radix = 16;
        cursor++;
    }

    uint64_t value = 0;
    int32_t digits_start = cursor;

    while (cursor < size) {
        const char32_t ch = input[static_cast<size_t>(cursor)];

        int digit = -1;

        if (IsAsciiDigit(ch)) {
            digit = static_cast<int>(ch - U'0');
        } else if (radix == 16 && ch >= U'a' && ch <= U'f') {
            digit = static_cast<int>(ch - U'a') + 10;
        } else if (radix == 16 && ch >= U'A' && ch <= U'F') {
            digit = static_cast<int>(ch - U'A') + 10;
        } else {
            break;
        }

        value = value * static_cast<uint64_t>(radix) + static_cast<uint64_t>(digit);

        if (value > 0x110000) {
            value = 0x110001;
        }

        cursor++;
    }

    if (cursor == digits_start) {
        return 0;
    }

    int32_t consumed = cursor - preprocessor.pos;

    bool matched_semicolon = false;

    if (cursor < size && input[static_cast<size_t>(cursor)] == u';') {
        matched_semicolon = true;
        consumed++;
    }

    if (!matched_semicolon) {
        ReportError(Err::kMissingSemicolonAfterCharacterReference, 1);
    }

    codepoints[0] = codepoints[1] = 0;
    cp_count = 1;

    if (value > 0x110000) {
        value = 0x110000;
    }

    const std::optional<Err> error = GetErrorForNumericCharacterReference(static_cast<uint32_t>(value));

    if (error.has_value()) {
        ReportError(*error, 1);
    }

    if (value == 0 || value > 0x10ffff || IsSurrogate(static_cast<char32_t>(value))) {
        value = 0xfffd;
    }

    codepoints[0] = static_cast<uint32_t>(value);

    return consumed;
}

// The character-reference resolver state. Decides whether the "&..." sequence
// at the cursor is a numeric entity, a named entity, or nothing; emits the
// decoded characters (or a literal '&'); and then hands control back to the
// state that was active before the reference began.
//
//   input "&amp;"-> codepoints {'&'}, text emits '&', control back to kData
//   input "&unknown;" -> kUnknownNamedCharacterReference, literal '&' emitted,
//                        then ambiguous-ampersand/Data state consumes "unknown;"
//   input "&#x41;" -> codepoints {'A'}, 'A' emitted
//
// Edge cases: a failed match rewinds to entity_start_pos_ (before the letters
// were tasted) and emits a bare '&'; if the following character is yet another
// alphanumeric the tokenizer must stay in the ambiguous-ampersand state so a
// dangling "&notit;" is consumed as characters and flagged, otherwise it
// returns exactly to return_state_. On success the cursor is parked on the
// final consumed unit so the next loop iteration naturally continues after the
// entity.
void Tokenizer::StateCharacterReference() {
    const std::u16string_view input(preprocessor.html);
    const int32_t size = static_cast<int32_t>(input.size());
    const int32_t start = preprocessor.pos;

    uint32_t codepoints[2] = {0, 0};
    size_t cp_count = 0;
    int32_t consumed = 0;
    bool named_semicolon = true;

    if (start < size && input[static_cast<size_t>(start)] == u'#') {
        consumed = DecodeNumericEntity(codepoints, cp_count);
    } else {
        consumed = static_cast<int32_t>(
            entities::DecodeNamedEntity(input.substr(static_cast<size_t>(start)),
                                        IsCharacterReferenceInAttribute(), codepoints, cp_count,
                                        named_semicolon));

        if (consumed > 0 && !named_semicolon) {
            ReportError(Err::kMissingSemicolonAfterCharacterReference, 1);
        }
    }

    if (consumed == 0) {
        preprocessor.pos = entity_start_pos_;

        FlushCodePointConsumedAsCharacterReference(static_cast<char32_t>(Cp::kAmpersand));

        state = !IsCharacterReferenceInAttribute() &&
                        IsAsciiAlphanumeric(preprocessor.Peek(1))
                    ? State::kAmbiguousAmpersand
                    : return_state_;
    } else {
        for (size_t i = 0; i < cp_count; i++) {
            FlushCodePointConsumedAsCharacterReference(codepoints[i]);
        }

        preprocessor.pos = entity_start_pos_ + consumed;

        state = return_state_;
    }
}

// The heart of the tokenizer: a single dispatch that feeds the code point just
// read into the handler for whatever state the machine is currently in. Each
// state handler may consume further input, shift `state`, emit tokens, or all
// three before returning control to the loop.
//
//   state == State::kData, cp == '<' -> StateData handles it (shifts to kTagOpen)
//   state == State::kTagName, cp == '>' -> StateTagName emits the tag token
//   state == State::kCharacterReference -> StateCharacterReference() runs with
//                                          no argument (it re-reads input)
//
// Edge cases: the character-reference state is the only handler that takes no
// argument because it restarts its own scan from the recorded position; an
// unknown/unsupported state value raises std::runtime_error, signalling a
// Guchho internal invariant violation rather than a document error, since the
// state field should only ever hold one of the enumerated values listed here.
void Tokenizer::CallState(char32_t cp) {
    switch (state) {
        case State::kData:
            StateData(cp);
            break;
        case State::kRcdata:
            StateRcdata(cp);
            break;
        case State::kRawtext:
            StateRawtext(cp);
            break;
        case State::kScriptData:
            StateScriptData(cp);
            break;
        case State::kPlaintext:
            StatePlaintext(cp);
            break;
        case State::kTagOpen:
            StateTagOpen(cp);
            break;
        case State::kEndTagOpen:
            StateEndTagOpen(cp);
            break;
        case State::kTagName:
            StateTagName(cp);
            break;
        case State::kRcdataLessThanSign:
            StateRcdataLessThanSign(cp);
            break;
        case State::kRcdataEndTagOpen:
            StateRcdataEndTagOpen(cp);
            break;
        case State::kRcdataEndTagName:
            StateRcdataEndTagName(cp);
            break;
        case State::kRawtextLessThanSign:
            StateRawtextLessThanSign(cp);
            break;
        case State::kRawtextEndTagOpen:
            StateRawtextEndTagOpen(cp);
            break;
        case State::kRawtextEndTagName:
            StateRawtextEndTagName(cp);
            break;
        case State::kScriptDataLessThanSign:
            StateScriptDataLessThanSign(cp);
            break;
        case State::kScriptDataEndTagOpen:
            StateScriptDataEndTagOpen(cp);
            break;
        case State::kScriptDataEndTagName:
            StateScriptDataEndTagName(cp);
            break;
        case State::kScriptDataEscapeStart:
            StateScriptDataEscapeStart(cp);
            break;
        case State::kScriptDataEscapeStartDash:
            StateScriptDataEscapeStartDash(cp);
            break;
        case State::kScriptDataEscaped:
            StateScriptDataEscaped(cp);
            break;
        case State::kScriptDataEscapedDash:
            StateScriptDataEscapedDash(cp);
            break;
        case State::kScriptDataEscapedDashDash:
            StateScriptDataEscapedDashDash(cp);
            break;
        case State::kScriptDataEscapedLessThanSign:
            StateScriptDataEscapedLessThanSign(cp);
            break;
        case State::kScriptDataEscapedEndTagOpen:
            StateScriptDataEscapedEndTagOpen(cp);
            break;
        case State::kScriptDataEscapedEndTagName:
            StateScriptDataEscapedEndTagName(cp);
            break;
        case State::kScriptDataDoubleEscapeStart:
            StateScriptDataDoubleEscapeStart(cp);
            break;
        case State::kScriptDataDoubleEscaped:
            StateScriptDataDoubleEscaped(cp);
            break;
        case State::kScriptDataDoubleEscapedDash:
            StateScriptDataDoubleEscapedDash(cp);
            break;
        case State::kScriptDataDoubleEscapedDashDash:
            StateScriptDataDoubleEscapedDashDash(cp);
            break;
        case State::kScriptDataDoubleEscapedLessThanSign:
            StateScriptDataDoubleEscapedLessThanSign(cp);
            break;
        case State::kScriptDataDoubleEscapeEnd:
            StateScriptDataDoubleEscapeEnd(cp);
            break;
        case State::kBeforeAttributeName:
            StateBeforeAttributeName(cp);
            break;
        case State::kAttributeName:
            StateAttributeName(cp);
            break;
        case State::kAfterAttributeName:
            StateAfterAttributeName(cp);
            break;
        case State::kBeforeAttributeValue:
            StateBeforeAttributeValue(cp);
            break;
        case State::kAttributeValueDoubleQuoted:
            StateAttributeValueDoubleQuoted(cp);
            break;
        case State::kAttributeValueSingleQuoted:
            StateAttributeValueSingleQuoted(cp);
            break;
        case State::kAttributeValueUnquoted:
            StateAttributeValueUnquoted(cp);
            break;
        case State::kAfterAttributeValueQuoted:
            StateAfterAttributeValueQuoted(cp);
            break;
        case State::kSelfClosingStartTag:
            StateSelfClosingStartTag(cp);
            break;
        case State::kBogusComment:
            StateBogusComment(cp);
            break;
        case State::kMarkupDeclarationOpen:
            StateMarkupDeclarationOpen(cp);
            break;
        case State::kCommentStart:
            StateCommentStart(cp);
            break;
        case State::kCommentStartDash:
            StateCommentStartDash(cp);
            break;
        case State::kComment:
            StateComment(cp);
            break;
        case State::kCommentLessThanSign:
            StateCommentLessThanSign(cp);
            break;
        case State::kCommentLessThanSignBang:
            StateCommentLessThanSignBang(cp);
            break;
        case State::kCommentLessThanSignBangDash:
            StateCommentLessThanSignBangDash(cp);
            break;
        case State::kCommentLessThanSignBangDashDash:
            StateCommentLessThanSignBangDashDash(cp);
            break;
        case State::kCommentEndDash:
            StateCommentEndDash(cp);
            break;
        case State::kCommentEnd:
            StateCommentEnd(cp);
            break;
        case State::kCommentEndBang:
            StateCommentEndBang(cp);
            break;
        case State::kDoctype:
            StateDoctype(cp);
            break;
        case State::kBeforeDoctypeName:
            StateBeforeDoctypeName(cp);
            break;
        case State::kDoctypeName:
            StateDoctypeName(cp);
            break;
        case State::kAfterDoctypeName:
            StateAfterDoctypeName(cp);
            break;
        case State::kAfterDoctypePublicKeyword:
            StateAfterDoctypePublicKeyword(cp);
            break;
        case State::kBeforeDoctypePublicIdentifier:
            StateBeforeDoctypePublicIdentifier(cp);
            break;
        case State::kDoctypePublicIdentifierDoubleQuoted:
            StateDoctypePublicIdentifierDoubleQuoted(cp);
            break;
        case State::kDoctypePublicIdentifierSingleQuoted:
            StateDoctypePublicIdentifierSingleQuoted(cp);
            break;
        case State::kAfterDoctypePublicIdentifier:
            StateAfterDoctypePublicIdentifier(cp);
            break;
        case State::kBetweenDoctypePublicAndSystemIdentifiers:
            StateBetweenDoctypePublicAndSystemIdentifiers(cp);
            break;
        case State::kAfterDoctypeSystemKeyword:
            StateAfterDoctypeSystemKeyword(cp);
            break;
        case State::kBeforeDoctypeSystemIdentifier:
            StateBeforeDoctypeSystemIdentifier(cp);
            break;
        case State::kDoctypeSystemIdentifierDoubleQuoted:
            StateDoctypeSystemIdentifierDoubleQuoted(cp);
            break;
        case State::kDoctypeSystemIdentifierSingleQuoted:
            StateDoctypeSystemIdentifierSingleQuoted(cp);
            break;
        case State::kAfterDoctypeSystemIdentifier:
            StateAfterDoctypeSystemIdentifier(cp);
            break;
        case State::kBogusDoctype:
            StateBogusDoctype(cp);
            break;
        case State::kCdataSection:
            StateCdataSection(cp);
            break;
        case State::kCdataSectionBracket:
            StateCdataSectionBracket(cp);
            break;
        case State::kCdataSectionEnd:
            StateCdataSectionEnd(cp);
            break;
        case State::kCharacterReference:
            StateCharacterReference();
            break;
        case State::kAmbiguousAmpersand:
            StateAmbiguousAmpersand(cp);
            break;
        default:
            throw std::runtime_error("Unknown state");
    }
}

// The most common state and the one a document begins in. Ordinary code points
// become the pending character run; '<' opens tag parsing; '&' starts a
// character reference. Unlike the raw-text states, a null that reaches data is
// emitted literally (U+0000, not replaced) because in text content the
// specification leaves it in place.
//
//   input "a<b&c" -> run "a", then kTagOpen on '<', run "c" after "b&"
//   input "\0x"   -> kUnexpectedNullCharacter reported, run gains U+0000
//   input EOF     -> EmitEofToken, tokenizer deactivates
//
// Edge case: there is no ">" case here because '>' is an ordinary character in
// the data state; angle brackets only mean something once a tag is being read.
void Tokenizer::StateData(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kTagOpen;
            break;
        }
        case static_cast<char32_t>(Cp::kAmpersand): {
            StartCharacterReference();
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            EmitCodePoint(cp);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            EmitEofToken();
            break;
        }
        default: {
            EmitCodePoint(cp);
        }
    }
}

// RCDATA (also known as "text-ish raiseable content"): used for <title> and
// <textarea>. Resembles the data state but treats '<' as the start of a
// possible end tag only, so `<` characters that are not the beginning of an
// end tag are emitted literally. Character references are still decoded here,
// unlike in the stricter raw-text modes below.
//
//   input "x<y"      -> run "x", '<' triggers end-tag probe, failure -> literal "<y"
//   input "\0"       -> replaced with U+FFFD (nulls are not allowed in RCDATA)
//   input "a &amp; b" -> '&' still decodes to "a & b"
//
// Edge case: replacing nulls with U+FFFD rather than emitting U+0000 is what
// distinguishes RCDATA from the plain data state and affects the final text
// the parser receives.
void Tokenizer::StateRcdata(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kAmpersand): {
            StartCharacterReference();
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kRcdataLessThanSign;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            EmitEofToken();
            break;
        }
        default: {
            EmitCodePoint(cp);
        }
    }
}

// RAWTEXT: the mode for <style>, <xmp>, <iframe>, <noembed>, <noframes>. The
// content is taken verbatim as text and only the current element's matching
// end tag can terminate it; character references are NOT decoded here, so "&"
// stays a literal ampersand.
//
//   input "<style> a & b </style>" -> all of " a & b " is character text
//   input "\0" in the middle       -> replaced with U+FFFD and the scan goes on
//   input EOF inside               -> EmitEofToken (raw text just ends)
//
// Edge case: because references are ignored, a question like "&amp;" yields
// the five characters '&','a','m','p',';', which is the correct per-HTML
// behavior for a raw-text element.
void Tokenizer::StateRawtext(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kRawtextLessThanSign;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            EmitEofToken();
            break;
        }
        default: {
            EmitCodePoint(cp);
        }
    }
}

// The main body mode of a <script> element. Functionally identical to RAWTEXT
// for its first pass but with a richer sub-state network that recognizes HTML
// comment-looking sequences (<!-- ... -->) and their nesting, so that the
// famous `<!-- <script>` trick can keep the tokenizer from mis-detecting an
// end tag. Nulls are replaced; references are not decoded.
//
//   input "var a=1"        -> run "var a=1"
//   input "x <script> y"   -> '<' probes, then double-escape machinery engages
//   input "\0"             -> replaced with U+FFFD
//
// Edge case: this state never matches an end tag directly; it only hands '<'
// to the less-than probe, and the probe decides whether an end tag is truly
// present before any token is emitted.
void Tokenizer::StateScriptData(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kScriptDataLessThanSign;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            EmitEofToken();
            break;
        }
        default: {
            EmitCodePoint(cp);
        }
    }
}

// PLAINTEXT, entered after the <plaintext> tag: everything until EOF is treated
// as character data. There is no end tag and no character reference handling,
// so the rest of the document is consumed verbatim as text.
//
//   input "<plaintext>1 < 2" -> only "<plaintext>" is a token; "1 < 2" all text
//   input "\0" anywhere      -> U+FFFD replacement, scanning continues
//   input EOF                -> EmitEofToken
//
// Edge case: '<' needs no special handling, which is exactly the point of
// plaintext -- no tag markup is recognized after this point.
void Tokenizer::StatePlaintext(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            EmitEofToken();
            break;
        }
        default: {
            EmitCodePoint(cp);
        }
    }
}

// Entered right after a '<' that was seen in the data state. A letter starts a
// start tag whose name begins accumulating immediately; '<!' goes to markup
// declarations (comments, doctype, CDATA); '</' starts an end tag; '<?' and
// any other character are recovered as bogus comments or literal '<' text plus
// a parse error.
//
//   input "<div> x" -> start-tag path, tag name builds from 'd'
//   input "<?"      -> kUnexpectedQuestionMarkInsteadOfTagName, bogus comment
//   input "<3"      -> kInvalidFirstCharacterOfTagName, emits literal "<", '3'
//                       reprocessed by the data state
//   input "<" + EOF -> kEofBeforeTagName, emits "<", then EOF
//
// Edge case: the first character of a tag must be an ASCII letter; any other
// leading code point never begins a tag in HTML, no matter how many bytes it
// spans.
void Tokenizer::StateTagOpen(char32_t cp) {
    if (IsAsciiLetter(cp)) {
        CreateStartTagToken();
        state = State::kTagName;
        StateTagName(cp);
    } else {
        switch (cp) {
            case static_cast<char32_t>(Cp::kExclamationMark): {
                state = State::kMarkupDeclarationOpen;
                break;
            }
            case static_cast<char32_t>(Cp::kSolidus): {
                state = State::kEndTagOpen;
                break;
            }
            case static_cast<char32_t>(Cp::kQuestionMark): {
                ReportError(Err::kUnexpectedQuestionMarkInsteadOfTagName);
                CreateCommentToken(1);
                state = State::kBogusComment;
                StateBogusComment(cp);
                break;
            }
            case static_cast<char32_t>(Cp::kEof): {
                ReportError(Err::kEofBeforeTagName);
                EmitChars(u"<");
                EmitEofToken();
                break;
            }
            default: {
                ReportError(Err::kInvalidFirstCharacterOfTagName);
                EmitChars(u"<");
                state = State::kData;
                StateData(cp);
            }
        }
    }
}

// The state that follows "</". A letter begins (and immediately accumulates)
// an end-tag name; '>' with nothing before it is reported as a missing name;
// any other character is treated as the start of a bogus comment, since "</x"
// markup that is not a valid end tag should not be shown to the parser.
//
//   input "</div>" -> end-tag token, name builds from 'd'
//   input "</>"    -> kMissingEndTagName, back to the data state
//   input "</%"    -> kInvalidFirstCharacterOfTagName, bogus comment from '%'
//   input "</" EOF -> kEofBeforeTagName, emits literal "</", then EOF
//
// Edge case: the two-character "</" prefix is emitted as literal text only in
// the recovery branches, ensuring malformed markup still surfaces to the user
// as visible characters.
void Tokenizer::StateEndTagOpen(char32_t cp) {
    if (IsAsciiLetter(cp)) {
        CreateEndTagToken();
        state = State::kTagName;
        StateTagName(cp);
    } else {
        switch (cp) {
            case static_cast<char32_t>(Cp::kGreaterThanSign): {
                ReportError(Err::kMissingEndTagName);
                state = State::kData;
                break;
            }
            case static_cast<char32_t>(Cp::kEof): {
                ReportError(Err::kEofBeforeTagName);
                EmitChars(u"</");
                EmitEofToken();
                break;
            }
            default: {
                ReportError(Err::kInvalidFirstCharacterOfTagName);
                CreateCommentToken(2);
                state = State::kBogusComment;
                StateBogusComment(cp);
            }
        }
    }
}

// Accumulates the characters of the tag name. Names are lowercased as they are
// appended (so "DIV" and "div" both yield "div"), and the terminator -- space,
// '/', '>', null, or EOF -- moves the machine on to attribute scanning, the
// self-closing marker, token emission, or error recovery respectively.
//
//   input "<Div CLASS=x>" -> tag name becomes "div", attributes follow
//   input "<br/>y"        -> '/' -> self-closing marker, then '>' emits <br/>
//   input "<a\0>"         -> kUnexpectedNullCharacter, name gains U+FFFD
//   input "<a<EOF>"       -> kEofInTag, EOF emitted
//
// Edge case: ASCII upper-to-lower conversion is arithmetic (cp + 0x20) and is
// intentionally applied only to the ASCII range, never to non-ASCII letters.
void Tokenizer::StateTagName(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            state = State::kBeforeAttributeName;
            break;
        }
        case static_cast<char32_t>(Cp::kSolidus): {
            state = State::kSelfClosingStartTag;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kData;
            EmitCurrentTagToken();
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            tag_token_.tag_name += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInTag);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(tag_token_.tag_name, IsAsciiUpper(cp) ? ToAsciiLower(cp) : cp);
        }
    }
}

// Reached from RCDATA when a '<' was followed by whitespace or text. A '/'
// means a potential end tag and transfers to the end-tag-open probe; any other
// code point means the '<' was genuinely text, so it is emitted literally and
// the current character is reprocessed in the RCDATA state.
//
//   input "a</b" (inside <b>) -> '/', end tag probe begins
//   input "a< b"              -> '<' emitted as text, " b" reprocessed
//
// Edge case: reprocessing the same code point (rather than advancing twice) is
// why CallState can re-dispatch; the '<' must not be consumed twice.
void Tokenizer::StateRcdataLessThanSign(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kSolidus)) {
        state = State::kRcdataEndTagOpen;
    } else {
        EmitChars(u"<");
        state = State::kRcdata;
        StateRcdata(cp);
    }
}

// The "</?" probe for RCDATA. A letter continues toward a possible end tag; a
// non-letter means "</" plus everything else is plain text, so the two
// characters are emitted and the current code point is reprocessed.
//
//   input "</b>" (inside <b>) -> letter 'b', end-tag-name matching begins
//   input "</ 5"              -> "</" emitted as text, '5' reprocessed
//
// Edge case: the same "emitted literal then reprocessed" pattern guarantees
// that when no end tag materializes, the character run faithfully contains
// "</..." exactly as written.
void Tokenizer::StateRcdataEndTagOpen(char32_t cp) {
    if (IsAsciiLetter(cp)) {
        state = State::kRcdataEndTagName;
        StateRcdataEndTagName(cp);
    } else {
        EmitChars(u"</");
        state = State::kRcdata;
        StateRcdata(cp);
    }
}

// Shared engine behind all raw-text and script end-tag detection. Given that
// the scanner is currently examining a candidate end tag ("</"), this checks
// whether the text ahead matches the name of the element whose raw-text
// content we are inside. On success it manufactures the end-tag token, skips
// past the matched name, and leaves the machine in a state appropriate to
// whatever follows the name (attributes, self-closing marker, or '>').
//
//   last_start_tag_name == u"style", input "</style>"
//     -> matches; cursor jumps over "style"; kData + tag emitted
//   last_start_tag_name == u"title", input "</title x>"
//     -> matches; kBeforeAttributeName, attributes are tolerated
//   input "</stylenot>" -> name mismatch -> returns true (caller emits "</"
//                                           and reprocesses)
//
// Edge cases: the comparison is case-insensitive (plus ASCII-fold, since the
// matched name was stored lowercase); capitalizing "STYLE" still closes a
// style element. The /generated/ end tag assigns tag_token_.tag_name from the
// stored lowercase last_start_tag_name, and a case-insensitive fold is done
// via StartsWith's own logic rather than copying source text. Any single code
// point that is not whitespace, '/', or '>' after the matched name aborts the
// end tag so the content is preserved verbatim.
bool Tokenizer::HandleSpecialEndTag(char32_t) {
    if (!preprocessor.StartsWith(last_start_tag_name, false)) {
        return true;
    }

    CreateEndTagToken();
    tag_token_.tag_name = ToUtf8(last_start_tag_name);

    const char32_t cp = preprocessor.Peek(static_cast<int32_t>(last_start_tag_name.size()));

    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            AdvanceBy(static_cast<int32_t>(last_start_tag_name.size()));
            state = State::kBeforeAttributeName;
            return false;
        }
        case static_cast<char32_t>(Cp::kSolidus): {
            AdvanceBy(static_cast<int32_t>(last_start_tag_name.size()));
            state = State::kSelfClosingStartTag;
            return false;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            AdvanceBy(static_cast<int32_t>(last_start_tag_name.size()));
            EmitCurrentTagToken();
            state = State::kData;
            return false;
        }
        default: {
            return true;
        }
    }
}

// Verifies the candidate end tag name character-by-character in RCDATA. On a
// successful HandleSpecialEndTag the cursor has already been parked past the
// name; on failure the literal "</" plus the current character are re-emitted
// as text and RCDATA resumes.
//
//   input "</TITLE>" inside <title> -> end tag matched and emitted
//   input "</titlex>"               -> "</titlex" becomes text
//
// Edge case: only the HandleSpecialEndTag decision may produce a tag token
// here; anything else is guaranteed text, which keeps raw content lossless.
void Tokenizer::StateRcdataEndTagName(char32_t cp) {
    if (HandleSpecialEndTag(cp)) {
        EmitChars(u"</");
        state = State::kRcdata;
        StateRcdata(cp);
    }
}

// Mirrors StateRcdataLessThanSign but for RAWTEXT content, where there are no
// character references to worry about. A literal '<' that is not followed by
// '/' stays text.
//
//   input "a < b" in <style> -> '<' text, space reprocessed
//   input "a</STYLE>"        -> '/' -> end-tag probe begins
void Tokenizer::StateRawtextLessThanSign(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kSolidus)) {
        state = State::kRawtextEndTagOpen;
    } else {
        EmitChars(u"<");
        state = State::kRawtext;
        StateRawtext(cp);
    }
}

// The "</?" entry probe into RAWTEXT end-tag matching; identical in structure
// to its RCDATA twin, with text fallback for non-letters.
//
//   input "</STYLE>" in <style> -> 'S' begins the name match
//   input "</ 3"                -> "</" plus '3' as raw text
void Tokenizer::StateRawtextEndTagOpen(char32_t cp) {
    if (IsAsciiLetter(cp)) {
        state = State::kRawtextEndTagName;
        StateRawtextEndTagName(cp);
    } else {
        EmitChars(u"</");
        state = State::kRawtext;
        StateRawtext(cp);
    }
}

// Verifies the matched raw-text end-tag name, sharing HandleSpecialEndTag with
// the RCDATA and script variants.
//
//   input "</style>" in <style>   -> end tag emitted, back to data
//   input "</styl e>" (a space in the name) -> "</styl" text, " e" reprocessed
void Tokenizer::StateRawtextEndTagName(char32_t cp) {
    if (HandleSpecialEndTag(cp)) {
        EmitChars(u"</");
        state = State::kRawtext;
        StateRawtext(cp);
    }
}

// The '<' probe inside script data. A '/' starts end-tag detection; '<!' (the
// beginning of what may become an HTML comment inside the script) is emitted
// and shifts into the escape-start states; anything else is text.
//
//   input "x< y"        -> '<' emitted as text, ' ' reprocessed
//   input "x<!"         -> "<!" emitted, escape-start chain begins
//   input "x</script>"  -> '/' -> end-tag probe
//
// Edge case: '<!' here does NOT open a real comment token for the parser; it
// only enters the script-comment-like bookkeeping states so that the content
// stays one character run.
void Tokenizer::StateScriptDataLessThanSign(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSolidus): {
            state = State::kScriptDataEndTagOpen;
            break;
        }
        case static_cast<char32_t>(Cp::kExclamationMark): {
            state = State::kScriptDataEscapeStart;
            EmitChars(u"<!");
            break;
        }
        default: {
            EmitChars(u"<");
            state = State::kScriptData;
            StateScriptData(cp);
        }
    }
}

// The "</?" entry probe for script data end tags. Identical in spirit to the
// RCDATA/RAWTEXT variants: letters keep hoping for a name match, anything else
// degrades to "</" as script text.
//
//   input "</script>"  -> 's' begins the matched-name check
//   input "</ 7"       -> "</" text, '7' reprocessed
void Tokenizer::StateScriptDataEndTagOpen(char32_t cp) {
    if (IsAsciiLetter(cp)) {
        state = State::kScriptDataEndTagName;
        StateScriptDataEndTagName(cp);
    } else {
        EmitChars(u"</");
        state = State::kScriptData;
        StateScriptData(cp);
    }
}

// Confirms the candidate "</script>" end tag in normal script data using the
// shared HandleSpecialEndTag matcher.
//
//   input "</script>"   -> end tag emitted, back to data
//   input "</scrip t>"  -> "</scrip" stays text, " t" reprocessed
void Tokenizer::StateScriptDataEndTagName(char32_t cp) {
    if (HandleSpecialEndTag(cp)) {
        EmitChars(u"</");
        state = State::kScriptData;
        StateScriptData(cp);
    }
}

// First step after "<!" inside script data: a '-' continues toward a comment
// lookalike signal; anything else forfeits the escape and drops back to plain
// script data, re-processing the current code point there.
//
//   input "<!-"   -> '-' consumed, dash-dash probe
//   input "!x"    -> back to script data, 'x' reprocessed
//
// Edge case: "escape" here refers to the source text <!-...--> behaving like a
// comment; Guchho never actually builds a comment token for it.
void Tokenizer::StateScriptDataEscapeStart(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kHyphenMinus)) {
        state = State::kScriptDataEscapeStartDash;
        EmitChars(u"-");
    } else {
        state = State::kScriptData;
        StateScriptData(cp);
    }
}

// Second '-' of the script comment signal. A second '-' completes "<!-" into
// "--" and fully enters the escaped (comment-like) region; any other code
// point aborts back to script data.
//
//   input "--x"  -> escaped region, "x" handled there
//   input "-x"   -> abort, script data reprocesses 'x'
void Tokenizer::StateScriptDataEscapeStartDash(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kHyphenMinus)) {
        state = State::kScriptDataEscapedDashDash;
        EmitChars(u"-");
    } else {
        state = State::kScriptData;
        StateScriptData(cp);
    }
}

// The main "escaped" region inside a script's HTML-comment-like text: ordinary
// code points are emitted verbatim, a '-' begins the closing-dash track, '<'
// probes for "<script" (double-escape) or an end tag, and null/EOF follow the
// usual replacement/termination rules.
//
//   input "<!-- stuff -->"  -> "stuff " tokens merge into one character run
//   input "<\0"             -> kUnexpectedNullCharacter, replaced with U+FFFD
//   input EOF               -> kEofInScriptHtmlCommentLikeText + EOF
void Tokenizer::StateScriptDataEscaped(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            state = State::kScriptDataEscapedDash;
            EmitChars(u"-");
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kScriptDataEscapedLessThanSign;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInScriptHtmlCommentLikeText);
            EmitEofToken();
            break;
        }
        default: {
            EmitCodePoint(cp);
        }
    }
}

// Dash inside the escaped region. '--' leads toward the region end (-->) but
// a '<' at the first dash still probes for an end tag, since the escaped
// region may contain "</script>" after a dash.
//
//   input "-foo ->" -> "--foo" tracking resumes on the second dash
//   input "-</script>" -> '<' probe -> end tag can close the element
//
// Edge case: reaching this state on a single dash does NOT immediately change
// the escaped/structure decision; the next code point decides.
void Tokenizer::StateScriptDataEscapedDash(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            state = State::kScriptDataEscapedDashDash;
            EmitChars(u"-");
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kScriptDataEscapedLessThanSign;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            state = State::kScriptDataEscaped;
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInScriptHtmlCommentLikeText);
            EmitEofToken();
            break;
        }
        default: {
            state = State::kScriptDataEscaped;
            EmitCodePoint(cp);
        }
    }
}

// Double-dash inside the escaped region. '>' ends the comment-like text and
// returns to ordinary script data; '<' still probes; '-' just stays put.
//
//   input "-- >"     -> '>' closes the escape, script data resumes
//   input "---"      -> third dash is just text
//
// Edge case: '>' is the ONLY code point that closes an escaped region here, so
// "</script>" inside it is governed by the '<' probe first.
void Tokenizer::StateScriptDataEscapedDashDash(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            EmitChars(u"-");
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kScriptDataEscapedLessThanSign;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kScriptData;
            EmitChars(u">");
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            state = State::kScriptDataEscaped;
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInScriptHtmlCommentLikeText);
            EmitEofToken();
            break;
        }
        default: {
            state = State::kScriptDataEscaped;
            EmitCodePoint(cp);
        }
    }
}

// '<' seen inside the escaped region. '/' means a possible end tag; a letter
// could begin the word "script" and flip into the double-escaped variant; any
// other code point makes '<' literal text.
//
//   input "<script>"     -> '<' text, then double-escape matching
//   input "</script>"    -> end-tag probe
//   input "<3"           -> '<' text, '3' reprocessed
//
// Edge case: the double-escape mangling of commented-out "<script>" exists so
// server-side templating of scripts does not accidentally terminate them.
void Tokenizer::StateScriptDataEscapedLessThanSign(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kSolidus)) {
        state = State::kScriptDataEscapedEndTagOpen;
    } else if (IsAsciiLetter(cp)) {
        EmitChars(u"<");
        state = State::kScriptDataDoubleEscapeStart;
        StateScriptDataDoubleEscapeStart(cp);
    } else {
        EmitChars(u"<");
        state = State::kScriptDataEscaped;
        StateScriptDataEscaped(cp);
    }
}

// "</" entry into end-tag detection from the escaped region; a successful
// match closes the script element itself, else the content stays escaped text.
//
//   input "</script>"    -> end tag emitted
//   input "</div>"       -> "</div" remains script text
void Tokenizer::StateScriptDataEscapedEndTagOpen(char32_t cp) {
    if (IsAsciiLetter(cp)) {
        state = State::kScriptDataEscapedEndTagName;
        StateScriptDataEscapedEndTagName(cp);
    } else {
        EmitChars(u"</");
        state = State::kScriptDataEscaped;
        StateScriptDataEscaped(cp);
    }
}

// The final name check before an escaped end tag is accepted.
//
//   input "</script>"  -> HandleSpecialEndTag succeeds, tag emitted
void Tokenizer::StateScriptDataEscapedEndTagName(char32_t cp) {
    if (HandleSpecialEndTag(cp)) {
        EmitChars(u"</");
        state = State::kScriptDataEscaped;
        StateScriptDataEscaped(cp);
    }
}

// While in the escaped region, "<!-- <script>" must be handled so that the
// literal word "script" followed by whitespace/'>' flips the machine into the
// double-escaped region (where "</script>" does NOT close the element). This
// state performs that lookahead.
//
//   input "<script> ..." -> word matched, whole "<script" emitted as characters,
//                           machine moves to double-escaped
//   input "<scrip t>"    -> match fails (no terminator), plain escaped text
//
// Edge cases: matching is case-insensitive, so "<SCRIPT>" works identically;
// the terminator check (IsScriptDataDoubleEscapeSequenceEnd) is what keeps the
// word "scripting" from tripping this branch.
void Tokenizer::StateScriptDataDoubleEscapeStart(char32_t cp) {
    if (preprocessor.StartsWith(kScriptSeq, false) &&
        IsScriptDataDoubleEscapeSequenceEnd(preprocessor.Peek(static_cast<int32_t>(kScriptSeq.size())))) {
        EmitCodePoint(cp);
        for (size_t i = 0; i < kScriptSeq.size(); i++) {
            EmitCodePoint(Consume());
        }

        state = State::kScriptDataDoubleEscaped;
    } else {
        state = State::kScriptDataEscaped;
        StateScriptDataEscaped(cp);
    }
}

// The double-escaped region body: like the escaped region but even "</script>"
// here is only text until the matching "</script>" (terminated by ') is seen,
// after which protection ends. Content is emitted as one run.
//
//   input "<script> </script> ..." -> inside double-escape, "</script>" text
//   input "<\0"                    -> null replaced, run continues
void Tokenizer::StateScriptDataDoubleEscaped(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            state = State::kScriptDataDoubleEscapedDash;
            EmitChars(u"-");
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kScriptDataDoubleEscapedLessThanSign;
            EmitChars(u"<");
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInScriptHtmlCommentLikeText);
            EmitEofToken();
            break;
        }
        default: {
            EmitCodePoint(cp);
        }
    }
}

// Dash inside the double-escaped region; mirrors the escaped-dash logic.
void Tokenizer::StateScriptDataDoubleEscapedDash(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            state = State::kScriptDataDoubleEscapedDashDash;
            EmitChars(u"-");
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kScriptDataDoubleEscapedLessThanSign;
            EmitChars(u"<");
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            state = State::kScriptDataDoubleEscaped;
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInScriptHtmlCommentLikeText);
            EmitEofToken();
            break;
        }
        default: {
            state = State::kScriptDataDoubleEscaped;
            EmitCodePoint(cp);
        }
    }
}

// Double-dash inside the double-escaped region; one '>' clears the region back
// to ordinary script data, '<' keeps probing for the closing "</script>", and
// any additional '-' is just text.
//
//   input "-->"       -> '>' exits double-escape, plain script data resumes
//   input "---"       -> third dash emitted as-is, still double-escaped
void Tokenizer::StateScriptDataDoubleEscapedDashDash(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            EmitChars(u"-");
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            state = State::kScriptDataDoubleEscapedLessThanSign;
            EmitChars(u"<");
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kScriptData;
            EmitChars(u">");
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            state = State::kScriptDataDoubleEscaped;
            EmitChars(u"\uFFFD");
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInScriptHtmlCommentLikeText);
            EmitEofToken();
            break;
        }
        default: {
            state = State::kScriptDataDoubleEscaped;
            EmitCodePoint(cp);
        }
    }
}

// '<' inside the double-escaped region. '/' begins the escape-exit check;
// anything else reverts to regular double-escaped processing.
void Tokenizer::StateScriptDataDoubleEscapedLessThanSign(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kSolidus)) {
        state = State::kScriptDataDoubleEscapeEnd;
        EmitChars(u"/");
    } else {
        state = State::kScriptDataDoubleEscaped;
        StateScriptDataDoubleEscaped(cp);
    }
}

// The mirror of DoubleEscapeStart: checks whether "</script>" (plus a proper
// terminator) has been seen inside the double-escaped region. If so, the
// machine drops back to the escaped region; if not, the text stays
// double-escaped.
//
//   input "</script>" - > escaped region again
//   input "</scriptx>" -> still double-escaped
void Tokenizer::StateScriptDataDoubleEscapeEnd(char32_t cp) {
    if (preprocessor.StartsWith(kScriptSeq, false) &&
        IsScriptDataDoubleEscapeSequenceEnd(preprocessor.Peek(static_cast<int32_t>(kScriptSeq.size())))) {
        EmitCodePoint(cp);
        for (size_t i = 0; i < kScriptSeq.size(); i++) {
            EmitCodePoint(Consume());
        }

        state = State::kScriptDataEscaped;
    } else {
        state = State::kScriptDataDoubleEscaped;
        StateScriptDataDoubleEscaped(cp);
    }
}

// Entry into attribute parsing: skips whitespace, then hands the next code
// point either to the after-name logic ('/','>',EOF), opens an attribute when a
// regular character arrives, or reports an unexpected '=' (which is then
// treated as the very start of an attribute name, per the recovery rules).
//
//   input `class=x`  -> attribute "class" begins
//   input ` =y`      -> '=' reported + becomes the first name character
//   input `/>`       -> defer to StateAfterAttributeName
//
// Edge case: whitespace between tag name and the first attribute is absorbed
// here and never becomes part of the attribute name.
void Tokenizer::StateBeforeAttributeName(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            break;
        }
        case static_cast<char32_t>(Cp::kSolidus):
        case static_cast<char32_t>(Cp::kGreaterThanSign):
        case static_cast<char32_t>(Cp::kEof): {
            state = State::kAfterAttributeName;
            StateAfterAttributeName(cp);
            break;
        }
        case static_cast<char32_t>(Cp::kEqualsSign): {
            ReportError(Err::kUnexpectedEqualsSignBeforeAttributeName);
            CreateAttr(static_cast<char32_t>(Cp::kEqualsSign));
            state = State::kAttributeName;
            break;
        }
        default: {
            CreateAttr(U'\0');
            state = State::kAttributeName;
            StateAttributeName(cp);
        }
    }
}

// Accumulates an attribute's name. Names are ASCII-lowercased as they are
// appended; terminating characters finalize the name (LeaveAttrName) and hand
// off to the after-name or value states. Quotes/apostrophes/less-thans inside
// a name are reported but still kept, matching the tolerant recovery rules.
//
//   input 'CLASS'           -> name "class"
//   input 'id="x"'          -> '=' finalizes "id", value parsing follows
//   input 'a"b'             -> kUnexpectedCharacterInAttributeName, name "a"b"
//   input 'a\0'             -> kUnexpectedNullCharacter, name "a\uFFFD"
//
// Edge case: an explicit '=' is the only thing that starts value collection;
// the after-name state is where space-separated bare attributes terminate.
void Tokenizer::StateAttributeName(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed):
        case static_cast<char32_t>(Cp::kSolidus):
        case static_cast<char32_t>(Cp::kGreaterThanSign):
        case static_cast<char32_t>(Cp::kEof): {
            LeaveAttrName();
            state = State::kAfterAttributeName;
            StateAfterAttributeName(cp);
            break;
        }
        case static_cast<char32_t>(Cp::kEqualsSign): {
            LeaveAttrName();
            state = State::kBeforeAttributeValue;
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark):
        case static_cast<char32_t>(Cp::kApostrophe):
        case static_cast<char32_t>(Cp::kLessThanSign): {
            ReportError(Err::kUnexpectedCharacterInAttributeName);
            AppendCodePoint(current_attr_.name, cp);
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            current_attr_.name += "\uFFFD";
            break;
        }
        default: {
            AppendCodePoint(current_attr_.name, IsAsciiUpper(cp) ? ToAsciiLower(cp) : cp);
        }
    }
}

// Settles what happens after a completed attribute name: whitespace is skipped,
// '/' marks the tag self-closing, '=' begins the value, '>' ends the whole tag,
// EOF terminates it, and any other code point starts a brand-new attribute.
//
//   input 'id = x'  -> '=' -> value state
//   input 'a b'     -> 'b' starts a second attribute
//   input 'a/>'     -> '/' -> self-closing marker
//   input 'a>'      -> tag emitted
void Tokenizer::StateAfterAttributeName(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            break;
        }
        case static_cast<char32_t>(Cp::kSolidus): {
            state = State::kSelfClosingStartTag;
            break;
        }
        case static_cast<char32_t>(Cp::kEqualsSign): {
            state = State::kBeforeAttributeValue;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kData;
            EmitCurrentTagToken();
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInTag);
            EmitEofToken();
            break;
        }
        default: {
            CreateAttr(U'\0');
            state = State::kAttributeName;
            StateAttributeName(cp);
        }
    }
}

// Prepares to read an attribute value: whitespace is skipped, then either a
// quote picks the quoted-value submachine, '>' with no value is an error
// (missing attribute value, tag emitted), or the value is unquoted.
//
//   input '="x"'  -> double-quoted value
//   input "='x'"  -> single-quoted value
//   input '=x'    -> unquoted value from 'x'
//   input '>'     -> kMissingAttributeValue, tag emitted
void Tokenizer::StateBeforeAttributeValue(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark): {
            state = State::kAttributeValueDoubleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kApostrophe): {
            state = State::kAttributeValueSingleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kMissingAttributeValue);
            state = State::kData;
            EmitCurrentTagToken();
            break;
        }
        default: {
            state = State::kAttributeValueUnquoted;
            StateAttributeValueUnquoted(cp);
        }
    }
}

// Reads a double-quoted value: the closing '"' returns to the after-value
// state, '&' starts a character reference, a null inside is replaced, EOF
// terminates the tag. Everything else is appended to the value.
//
//   input '"abc"'     -> value "abc"
//   input '"a&amp;b"' -> value "a&b" (reference decoded)
//   input '"\0"'      -> value "a\uFFFD" (null replaced)
void Tokenizer::StateAttributeValueDoubleQuoted(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kQuotationMark): {
            state = State::kAfterAttributeValueQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kAmpersand): {
            StartCharacterReference();
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            CurrentAttrValue() += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInTag);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(CurrentAttrValue(), cp);
        }
    }
}

// Reads a single-quoted attribute value, behaving exactly like the
// double-quoted state but terminating on "'" instead of '"'.
//
//   input "'abc'"  -> value "abc"
//   input "'a&amp;b'" -> value "a&b"
//   input "'\0'"   -> value "a\uFFFD" (null replaced)
//
// Edge case: the first "'" past the opening quote always terminates the value,
// so a literal apostrophe inside a single-quoted value is impossible -- such
// markup must switch to double quotes.
void Tokenizer::StateAttributeValueSingleQuoted(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kApostrophe): {
            state = State::kAfterAttributeValueQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kAmpersand): {
            StartCharacterReference();
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            CurrentAttrValue() += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInTag);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(CurrentAttrValue(), cp);
        }
    }
}

// Reads an unquoted attribute value. Whitespace ends the value (back to the
// before-attribute-name state) and '>' ends the whole tag; '&' is decoded; a
// set of "special" code points (quote, apostrophe, '<', '=', '`') is illegal
// but tolerated with an error and appended anyway.
//
//   input '=abc x' -> value "abc", then a new attribute begins at 'x'
//   input '=abc>'  -> value "abc", tag emitted
//   input '=a<b'   -> kUnexpectedCharacterInUnquotedAttributeValue, "a<b" kept
//
// Edge case: '>' inside an unquoted value always closes the tag; there is no
// way to include it in an unquoted value.
void Tokenizer::StateAttributeValueUnquoted(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            LeaveAttrValue();
            state = State::kBeforeAttributeName;
            break;
        }
        case static_cast<char32_t>(Cp::kAmpersand): {
            StartCharacterReference();
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            LeaveAttrValue();
            state = State::kData;
            EmitCurrentTagToken();
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            CurrentAttrValue() += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark):
        case static_cast<char32_t>(Cp::kApostrophe):
        case static_cast<char32_t>(Cp::kLessThanSign):
        case static_cast<char32_t>(Cp::kEqualsSign):
        case static_cast<char32_t>(Cp::kGraveAccent): {
            ReportError(Err::kUnexpectedCharacterInUnquotedAttributeValue);
            AppendCodePoint(CurrentAttrValue(), cp);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInTag);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(CurrentAttrValue(), cp);
        }
    }
}

// The state after a quoted value ends. Whitespace concludes the attribute and
// begins looking at the next one; '/' marks self-closing; '>' emits the tag;
// and any other code point is an error (missing whitespace between
// attributes) that gets reprocessed as if a brand-new attribute were starting.
//
//   input ' "x" y="z"' -> whitespace legal, next attribute begins
//   input ' "x"/>'     -> '/' -> self-closing marker
//   input ' "x"y'      -> kMissingWhitespaceBetweenAttributes, 'y' starts one
void Tokenizer::StateAfterAttributeValueQuoted(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            LeaveAttrValue();
            state = State::kBeforeAttributeName;
            break;
        }
        case static_cast<char32_t>(Cp::kSolidus): {
            LeaveAttrValue();
            state = State::kSelfClosingStartTag;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            LeaveAttrValue();
            state = State::kData;
            EmitCurrentTagToken();
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInTag);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kMissingWhitespaceBetweenAttributes);
            state = State::kBeforeAttributeName;
            StateBeforeAttributeName(cp);
        }
    }
}

// Handles the '/' that appeared after a tag name or attribute. A following '>'
// marks the tag as self-closing and emits it; anything else is an error and
// the tag resumes attribute scanning.
//
//   input '/>'    -> tag_token_.self_closing = true, tag emitted
//   input '/x'    -> kUnexpectedSolidusInTag, 'x' starts an attribute
//
// Edge case: self-closing is a flag on the token; it does not itself change
// how the element is constructed downstream, but end tags with '/' will later
// be flagged separately.
void Tokenizer::StateSelfClosingStartTag(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            tag_token_.self_closing = true;
            state = State::kData;
            EmitCurrentTagToken();
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInTag);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kUnexpectedSolidusInTag);
            state = State::kBeforeAttributeName;
            StateBeforeAttributeName(cp);
        }
    }
}

// Recovery mode for markup that must be treated as a comment even though it
// did not open with "<!--". Collected until '>' (or EOF), with nulls replaced
// by U+FFFD. The comment token is then emitted normally.
//
//   input "<?php?>"      -> comment data "?php?"
//   input "<!garbage>"   -> comment data "garbage"
//   input "<!-- whatever" -> real comment state handles it, not this one
//
// Edge case: EOF inside a bogus comment still emits the comment (with no
// kEofInComment here, unlike proper comments) and then reports EOF.
void Tokenizer::StateBogusComment(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kData;
            EmitCurrentComment(comment_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            EmitCurrentComment(comment_token_);
            EmitEofToken();
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            comment_token_.data += "\uFFFD";
            break;
        }
        default: {
            AppendCodePoint(comment_token_.data, cp);
        }
    }
}

// The "<!" entry point after a tag-open '<': probes lookahead words to decide
// whether the construct is a comment ("--"), a doctype ("doctype",
// case-insensitive), a CDATA section ("[CDATA[", only honored inside foreign
// content), or something else to swallow as a bogus comment.
//
//   input "<!DOCTYPE html>" -> doctype state, location from before the keyword
//   input "<!-- c -->"      -> comment state (after "--" is consumed)
//   input "<![CDATA[x]]>" in SVG -> real CDATA section
//   input "<![CDATA[x]]>" in HTML  -> kCdataInHtmlContent, bogus comment
//   input "<!weird>"        -> kIncorrectlyOpenedComment, bogus comment
//
// Edge cases: matching "--" and "[CDATA[" is case-sensitive while "doctype"
// folds case; the doctype's location is deliberately opened BEFORE the keyword
// is fully confirmed, because the token must span the entire "<!DOCTYPE ..."
// construct and the exact starting point cannot be re-created later.
void Tokenizer::StateMarkupDeclarationOpen(char32_t cp) {
    if (ConsumeSequenceIfMatch(kDashDashSeq, true)) {
        CreateCommentToken(static_cast<int32_t>(kDashDashSeq.size()) + 1);
        state = State::kCommentStart;
    } else if (ConsumeSequenceIfMatch(kDoctypeSeq, false)) {
        current_location_ = GetCurrentLocation(static_cast<int32_t>(kDoctypeSeq.size()) + 1);
        state = State::kDoctype;
    } else if (ConsumeSequenceIfMatch(kCdataStartSeq, true)) {
        if (in_foreign_node) {
            state = State::kCdataSection;
        } else {
            ReportError(Err::kCdataInHtmlContent);
            CreateCommentToken(static_cast<int32_t>(kCdataStartSeq.size()) + 1);
            comment_token_.data = "[CDATA[";
            state = State::kBogusComment;
        }
    } else {
        ReportError(Err::kIncorrectlyOpenedComment);
        CreateCommentToken(2);
        state = State::kBogusComment;
        StateBogusComment(cp);
    }
}

// Immediately after "<!--": a '-' means the comment is opening with "--";
// '>' closes an empty comment abruptly (an error); anything else begins
// ordinary comment text.
//
//   input "<!--->"    -> kAbruptClosingOfEmptyComment
//   input "<!-- x -->" -> text state
void Tokenizer::StateCommentStart(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            state = State::kCommentStartDash;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kAbruptClosingOfEmptyComment);
            state = State::kData;
            EmitCurrentComment(comment_token_);
            break;
        }
        default: {
            state = State::kComment;
            StateComment(cp);
        }
    }
}

// The "<!--" plus one '-' case: a second '-' moves toward comment-end, '>'
// still closes an empty comment abruptly, EOF is an unterminated comment, and
// any other character is brought back into comment text prefixed by '-'.
//
//   input "<!--->"  -> kAbruptClosingOfEmptyComment
//   input "<!-- -x" -> data "-x" (dash preserved in the comment text)
void Tokenizer::StateCommentStartDash(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            state = State::kCommentEnd;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kAbruptClosingOfEmptyComment);
            state = State::kData;
            EmitCurrentComment(comment_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInComment);
            EmitCurrentComment(comment_token_);
            EmitEofToken();
            break;
        }
        default: {
            comment_token_.data += '-';
            state = State::kComment;
            StateComment(cp);
        }
    }
}

// The main body of a well-formed comment. Dashes enter the end-dash chain,
// '<' opens the nested-comment probe, nulls become U+FFFD, EOF terminates,
// and everything else is collected into the comment data.
//
//   input "<!--hello-->" -> data "hello"
//   input "<!--<x>-->"   -> data "<x>" (angle brackets are text here)
//   input "<!--a\0-->"   -> kUnexpectedNullCharacter, data "a\uFFFD"
void Tokenizer::StateComment(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            state = State::kCommentEndDash;
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            comment_token_.data += '<';
            state = State::kCommentLessThanSign;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            comment_token_.data += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInComment);
            EmitCurrentComment(comment_token_);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(comment_token_.data, cp);
        }
    }
}

// '<' inside comment text. An '!' continues toward a nested-comment
// lookahead sequence ("<!--"); a second '<' is just text; any other character
// falls back into ordinary comment accumulation.
//
//   input "<!--<!---->"  -> nested-comment detection chain (see below)
//   input "<!--<<-->"    -> "<<" both kept as comment text
void Tokenizer::StateCommentLessThanSign(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kExclamationMark): {
            comment_token_.data += '!';
            state = State::kCommentLessThanSignBang;
            break;
        }
        case static_cast<char32_t>(Cp::kLessThanSign): {
            comment_token_.data += '<';
            break;
        }
        default: {
            state = State::kComment;
            StateComment(cp);
        }
    }
}

// Second step of the nested-comment lookahead ("<!-"). Only '-' proceeds
// toward an embedded "<!--"; anything else falls back into comment text.
//
//   input "<! --"  -> '-' seen -> dash tracking (third step)
//   input "<! x"   -> comment text resumes with 'x'
void Tokenizer::StateCommentLessThanSignBang(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kHyphenMinus)) {
        state = State::kCommentLessThanSignBangDash;
    } else {
        state = State::kComment;
        StateComment(cp);
    }
}

// Third step of the nested-comment lookahead ("<!-"). A final '-' reaches the
// nested-comment terminator check; otherwise control merges into the ordinary
// end-dash processing.
void Tokenizer::StateCommentLessThanSignBangDash(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kHyphenMinus)) {
        state = State::kCommentLessThanSignBangDashDash;
    } else {
        state = State::kCommentEndDash;
        StateCommentEndDash(cp);
    }
}

// When "<!--" appears fully inside an already-open comment it signals a
// nesting violation; Guchho flags kNestedComment whenever a character other
// than '>' or EOF confirms the "-->" tail is not a genuine close.
void Tokenizer::StateCommentLessThanSignBangDashDash(char32_t cp) {
    if (cp != static_cast<char32_t>(Cp::kGreaterThanSign) && cp != static_cast<char32_t>(Cp::kEof)) {
        ReportError(Err::kNestedComment);
    }

    state = State::kCommentEnd;
    StateCommentEnd(cp);
}

// One dash away from the end of a comment. A second '-' reaches the end
// state; EOF ends it (with an error); anything else keeps the dash as comment
// data and resumes accumulation.
//
//   input "<!--a-->"   -> after first '-' of the tail probes for the second
//   input "<!--a--x"   -> "-x" remains comment text
void Tokenizer::StateCommentEndDash(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            state = State::kCommentEnd;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInComment);
            EmitCurrentComment(comment_token_);
            EmitEofToken();
            break;
        }
        default: {
            comment_token_.data += '-';
            state = State::kComment;
            StateComment(cp);
        }
    }
}

// The definitive "--" state: '>' closes the comment properly; '!' begins the
// "bang" variant (-->!); an extra '-' is kept; EOF reports an unterminated
// comment; and any other character swallows "--" into the data and resumes
// text collection.
//
//   input "<!--a-->"    -> comment closes, data "a"
//   input "<!--a-->!"   -> kIncorrectlyClosedComment (via bang state)
//   input "<!--a---b"   -> extra "-b" joins the data
void Tokenizer::StateCommentEnd(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kData;
            EmitCurrentComment(comment_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kExclamationMark): {
            state = State::kCommentEndBang;
            break;
        }
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            comment_token_.data += '-';
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInComment);
            EmitCurrentComment(comment_token_);
            EmitEofToken();
            break;
        }
        default: {
            comment_token_.data += "--";
            state = State::kComment;
            StateComment(cp);
        }
    }
}

// The tail variant "-->!": a '-' puts "--!" into the data and returns to the
// end-dash track; '>' properly closes (with kIncorrectlyClosedComment, since
// the spec forbids "-->" immediately before the bang); EOF reports an
// unterminated comment; anything else keeps "--!" plus the character as data.
void Tokenizer::StateCommentEndBang(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kHyphenMinus): {
            comment_token_.data += "--!";
            state = State::kCommentEndDash;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kIncorrectlyClosedComment);
            state = State::kData;
            EmitCurrentComment(comment_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInComment);
            EmitCurrentComment(comment_token_);
            EmitEofToken();
            break;
        }
        default: {
            comment_token_.data += "--!";
            state = State::kComment;
            StateComment(cp);
        }
    }
}

// The beginning of a document type declaration, reached right after the
// "doctype" keyword. Whitespace moves on to the name; '>' is treated as an
// empty doctype; EOF forces quirks mode with a reported error; any other code
// point is a missing-whitespace error but proceeds to the name anyway.
//
//   input "DOCTYPE html>" -> kBeforeDoctypeName with the space absorbed
//   input "DOCTYPE>"      -> abrupt close, handled via before-name
//   input "DOCTYPEhtml>"  -> kMissingWhitespaceBeforeDoctypeName
void Tokenizer::StateDoctype(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            state = State::kBeforeDoctypeName;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kBeforeDoctypeName;
            StateBeforeDoctypeName(cp);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            CreateDoctypeToken(nullptr);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kMissingWhitespaceBeforeDoctypeName);
            state = State::kBeforeDoctypeName;
            StateBeforeDoctypeName(cp);
        }
    }
}

// Immediately before the doctype's name: whitespace is skipped; a letter
// seeds the name (upper-case folded to lower); a null becomes U+FFFD; '>' or
// EOF yield a nameless force-quirks doctype; anything else begins the name
// with the current character.
//
//   input "HTML>"        -> name seeded "h" (coming letters complete it)
//   input ">"            -> kMissingDoctypeName, force_quirks
//   input "\0"           -> kUnexpectedNullCharacter, name begins U+FFFD
void Tokenizer::StateBeforeDoctypeName(char32_t cp) {
    if (IsAsciiUpper(cp)) {
        const std::u16string initial_name(1, static_cast<char16_t>(ToAsciiLower(cp)));
        CreateDoctypeToken(&initial_name);
        state = State::kDoctypeName;
    } else {
        switch (cp) {
            case static_cast<char32_t>(Cp::kSpace):
            case static_cast<char32_t>(Cp::kLineFeed):
            case static_cast<char32_t>(Cp::kTabulation):
            case static_cast<char32_t>(Cp::kFormFeed): {
                break;
            }
            case static_cast<char32_t>(Cp::kNull): {
                ReportError(Err::kUnexpectedNullCharacter);
                const std::u16string initial_name(1, 0xfffd);
                CreateDoctypeToken(&initial_name);
                state = State::kDoctypeName;
                break;
            }
            case static_cast<char32_t>(Cp::kGreaterThanSign): {
                ReportError(Err::kMissingDoctypeName);
                CreateDoctypeToken(nullptr);
                doctype_token_.force_quirks = true;
                EmitCurrentDoctype(doctype_token_);
                state = State::kData;
                break;
            }
            case static_cast<char32_t>(Cp::kEof): {
                ReportError(Err::kEofInDoctype);
                CreateDoctypeToken(nullptr);
                doctype_token_.force_quirks = true;
                EmitCurrentDoctype(doctype_token_);
                EmitEofToken();
                break;
            }
            default: {
                const std::u16string initial_name(1, static_cast<char16_t>(cp));
                CreateDoctypeToken(&initial_name);
                state = State::kDoctypeName;
            }
        }
    }
}

// Accumulates the doctype name, lowercasing ASCII letters as it goes.
// Whitespace ends the name; '>' emits the doctype; a null becomes U+FFFD;
// EOF forces quirks after reporting.
//
//   input "DOCTYPE html>"   -> name "html"
//   input "DOCTYPE HTML>"   -> name "html" (folded)
//   input "DOCTYPE h\0>"    -> name "h\uFFFD"
void Tokenizer::StateDoctypeName(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            state = State::kAfterDoctypeName;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kData;
            EmitCurrentDoctype(doctype_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            doctype_token_.name += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(doctype_token_.name, IsAsciiUpper(cp) ? ToAsciiLower(cp) : cp);
        }
    }
}

// After the doctype name: whitespace is skipped; '>' emits the doctype; EOF
// forces quirks; and the first non-whitespace letter sequence is checked
// against the "public" or "system" keywords, otherwise the doctype becomes
// bogus and force-quirks.
//
//   input " PUBLIC '...'>" -> public-id parsing
//   input " SYSTEM '...'>" -> system-id parsing
//   input " namegarbage>"  -> kInvalidCharacterSequenceAfterDoctypeName
void Tokenizer::StateAfterDoctypeName(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kData;
            EmitCurrentDoctype(doctype_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            if (ConsumeSequenceIfMatch(kPublicSeq, false)) {
                state = State::kAfterDoctypePublicKeyword;
            } else if (ConsumeSequenceIfMatch(kSystemSeq, false)) {
                state = State::kAfterDoctypeSystemKeyword;
            } else {
                ReportError(Err::kInvalidCharacterSequenceAfterDoctypeName);
                doctype_token_.force_quirks = true;
                state = State::kBogusDoctype;
                StateBogusDoctype(cp);
            }
        }
    }
}

// After a confirmed "PUBLIC" keyword: whitespace leads into the identifier
// entry state; a quote directly after the keyword is tolerated with an error
// and then treated as an identifier opener; '>' or EOF become force-quirks;
// and any other character makes the doctype bogus.
//
//   input "PUBLIC '-//W3C//DTD HTML 4.01//EN'>" -> public id parsed
//   input "PUBLIC'x'>" -> kMissingWhitespaceAfterDoctypePublicKeyword, but id
//                         still accepted from "'x'"
void Tokenizer::StateAfterDoctypePublicKeyword(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            state = State::kBeforeDoctypePublicIdentifier;
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark): {
            ReportError(Err::kMissingWhitespaceAfterDoctypePublicKeyword);
            doctype_token_.has_public_id = true;
            doctype_token_.public_id.clear();
            state = State::kDoctypePublicIdentifierDoubleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kApostrophe): {
            ReportError(Err::kMissingWhitespaceAfterDoctypePublicKeyword);
            doctype_token_.has_public_id = true;
            doctype_token_.public_id.clear();
            state = State::kDoctypePublicIdentifierSingleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kMissingDoctypePublicIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kData;
            EmitCurrentDoctype(doctype_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kMissingQuoteBeforeDoctypePublicIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kBogusDoctype;
            StateBogusDoctype(cp);
        }
    }
}

// Immediately before the public identifier: whitespace is skipped; a quote
// opens the identifier collection; '>' or EOF become force-quirks; and any
// other character makes the doctype bogus.
//
//   input " 'x'>"       -> double-quoted public id "x"
//   input ">"           -> kMissingDoctypePublicIdentifier
void Tokenizer::StateBeforeDoctypePublicIdentifier(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark): {
            doctype_token_.has_public_id = true;
            doctype_token_.public_id.clear();
            state = State::kDoctypePublicIdentifierDoubleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kApostrophe): {
            doctype_token_.has_public_id = true;
            doctype_token_.public_id.clear();
            state = State::kDoctypePublicIdentifierSingleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kMissingDoctypePublicIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kData;
            EmitCurrentDoctype(doctype_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kMissingQuoteBeforeDoctypePublicIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kBogusDoctype;
            StateBogusDoctype(cp);
        }
    }
}

// Accumulates a double-quoted public identifier. The closing '"' advances to
// the after-public state; a null is replaced; an abrupt '>' forces quirks
// after emitting; EOF likewise.
//
//   input '"x" SYSTEM "y">' -> public_id "x", then system follows
//   input '"\0">'           -> kUnexpectedNullCharacter, id "U+FFFD"
void Tokenizer::StateDoctypePublicIdentifierDoubleQuoted(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kQuotationMark): {
            state = State::kAfterDoctypePublicIdentifier;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            doctype_token_.public_id += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kAbruptDoctypePublicIdentifier);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            state = State::kData;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(doctype_token_.public_id, cp);
        }
    }
}

// Accumulates a single-quoted public identifier, i.e. the same as the
// double-quoted variant but terminated by "'".
void Tokenizer::StateDoctypePublicIdentifierSingleQuoted(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kApostrophe): {
            state = State::kAfterDoctypePublicIdentifier;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            doctype_token_.public_id += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kAbruptDoctypePublicIdentifier);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            state = State::kData;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(doctype_token_.public_id, cp);
        }
    }
}

// After a public identifier has closed: whitespace moves toward a system
// identifier; '>' emits the doctype; a quote directly begins the system id
// (with a missing-whitespace error); EOF forces quirks.
//
//   input ' "SYSTEM-id">'     -> system id parsed
//   input '">'                -> doctype emitted directly
void Tokenizer::StateAfterDoctypePublicIdentifier(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            state = State::kBetweenDoctypePublicAndSystemIdentifiers;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kData;
            EmitCurrentDoctype(doctype_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark): {
            ReportError(Err::kMissingWhitespaceBetweenDoctypePublicAndSystemIdentifiers);
            doctype_token_.has_system_id = true;
            doctype_token_.system_id.clear();
            state = State::kDoctypeSystemIdentifierDoubleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kApostrophe): {
            ReportError(Err::kMissingWhitespaceBetweenDoctypePublicAndSystemIdentifiers);
            doctype_token_.has_system_id = true;
            doctype_token_.system_id.clear();
            state = State::kDoctypeSystemIdentifierSingleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kMissingQuoteBeforeDoctypeSystemIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kBogusDoctype;
            StateBogusDoctype(cp);
        }
    }
}

// Whitepace between the public and system identifiers: absorbs the gap, lets
// a quote proceed directly into the system id, emits on '>', and so on.
void Tokenizer::StateBetweenDoctypePublicAndSystemIdentifiers(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            EmitCurrentDoctype(doctype_token_);
            state = State::kData;
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark): {
            doctype_token_.has_system_id = true;
            doctype_token_.system_id.clear();
            state = State::kDoctypeSystemIdentifierDoubleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kApostrophe): {
            doctype_token_.has_system_id = true;
            doctype_token_.system_id.clear();
            state = State::kDoctypeSystemIdentifierSingleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kMissingQuoteBeforeDoctypeSystemIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kBogusDoctype;
            StateBogusDoctype(cp);
        }
    }
}

// After a "SYSTEM" keyword: mirrors the PUBLIC-keyword handling (whitespace
// then quote = acceptable; quote directly = error but still accepted; '>' or
// EOF = force-quirks).
void Tokenizer::StateAfterDoctypeSystemKeyword(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            state = State::kBeforeDoctypeSystemIdentifier;
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark): {
            ReportError(Err::kMissingWhitespaceAfterDoctypeSystemKeyword);
            doctype_token_.has_system_id = true;
            doctype_token_.system_id.clear();
            state = State::kDoctypeSystemIdentifierDoubleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kApostrophe): {
            ReportError(Err::kMissingWhitespaceAfterDoctypeSystemKeyword);
            doctype_token_.has_system_id = true;
            doctype_token_.system_id.clear();
            state = State::kDoctypeSystemIdentifierSingleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kMissingDoctypeSystemIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kData;
            EmitCurrentDoctype(doctype_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kMissingQuoteBeforeDoctypeSystemIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kBogusDoctype;
            StateBogusDoctype(cp);
        }
    }
}

// Immediately before the system identifier, mirroring the public-identifier
// entry logic: skip whitespace, begin the id on a quote, treat '>' or EOF as
// force-quirks, and make the doctype bogus on any other character.
//
//   input " 'sys'>"  -> double-quoted system id "sys"
//   input ">"        -> kMissingDoctypeSystemIdentifier
void Tokenizer::StateBeforeDoctypeSystemIdentifier(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            break;
        }
        case static_cast<char32_t>(Cp::kQuotationMark): {
            doctype_token_.has_system_id = true;
            doctype_token_.system_id.clear();
            state = State::kDoctypeSystemIdentifierDoubleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kApostrophe): {
            doctype_token_.has_system_id = true;
            doctype_token_.system_id.clear();
            state = State::kDoctypeSystemIdentifierSingleQuoted;
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kMissingDoctypeSystemIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kData;
            EmitCurrentDoctype(doctype_token_);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kMissingQuoteBeforeDoctypeSystemIdentifier);
            doctype_token_.force_quirks = true;
            state = State::kBogusDoctype;
            StateBogusDoctype(cp);
        }
    }
}

// Accumulates a double-quoted system identifier, ending on '"'. Nulls are
// replaced with U+FFFD; a premature '>' or EOF forces quirks while still
// emitting the doctype.
//
//   input '"file.dtd">' -> system_id "file.dtd"
void Tokenizer::StateDoctypeSystemIdentifierDoubleQuoted(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kQuotationMark): {
            state = State::kAfterDoctypeSystemIdentifier;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            doctype_token_.system_id += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kAbruptDoctypeSystemIdentifier);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            state = State::kData;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(doctype_token_.system_id, cp);
        }
    }
}

// Accumulates a single-quoted system identifier, i.e. the same as the
// double-quoted variant but terminated by "'".
void Tokenizer::StateDoctypeSystemIdentifierSingleQuoted(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kApostrophe): {
            state = State::kAfterDoctypeSystemIdentifier;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            doctype_token_.system_id += "\uFFFD";
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            ReportError(Err::kAbruptDoctypeSystemIdentifier);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            state = State::kData;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            AppendCodePoint(doctype_token_.system_id, cp);
        }
    }
}

// After a system identifier has closed: whitespace is skipped, '>' emits the
// doctype, EOF forces quirks, and any other character (with error) sends the
// doctype to the bogus state.
//
//   input " >"          -> doctype emitted
//   input "x>"          -> kUnexpectedCharacterAfterDoctypeSystemIdentifier
void Tokenizer::StateAfterDoctypeSystemIdentifier(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kSpace):
        case static_cast<char32_t>(Cp::kLineFeed):
        case static_cast<char32_t>(Cp::kTabulation):
        case static_cast<char32_t>(Cp::kFormFeed): {
            break;
        }
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            EmitCurrentDoctype(doctype_token_);
            state = State::kData;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInDoctype);
            doctype_token_.force_quirks = true;
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default: {
            ReportError(Err::kUnexpectedCharacterAfterDoctypeSystemIdentifier);
            state = State::kBogusDoctype;
            StateBogusDoctype(cp);
        }
    }
}

// Swallows whatever follows a malformed doctype until '>' (which emits the
// doctype) or EOF (which emits the doctype and then the end-of-file token).
// Nulls are simply reported, never appended to any value. Called both by the
// main state machine and directly (with the offending code point) from the
// default branches that transition into bogus.
void Tokenizer::StateBogusDoctype(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            EmitCurrentDoctype(doctype_token_);
            state = State::kData;
            break;
        }
        case static_cast<char32_t>(Cp::kNull): {
            ReportError(Err::kUnexpectedNullCharacter);
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            EmitCurrentDoctype(doctype_token_);
            EmitEofToken();
            break;
        }
        default:
            break;
    }
}

// Takes a run of raw text inside a CDATA section, handling optional
// chunk boundaries: ']' arms the bracket state, EOF reports an error, and
// everything else is emitted as-is.
void Tokenizer::StateCdataSection(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kRightSquareBracket): {
            state = State::kCdataSectionBracket;
            break;
        }
        case static_cast<char32_t>(Cp::kEof): {
            ReportError(Err::kEofInCdata);
            EmitEofToken();
            break;
        }
        default: {
            EmitCodePoint(cp);
        }
    }
}

// Looking for the second ']' of a "]]>" termination inside CDATA. If it does
// not arrive, the previously consumed ']' is emitted and the rest re-examined
// in the CDATA section state, so a single stray bracket gives only one token.
void Tokenizer::StateCdataSectionBracket(char32_t cp) {
    if (cp == static_cast<char32_t>(Cp::kRightSquareBracket)) {
        state = State::kCdataSectionEnd;
    } else {
        EmitChars(u"]");
        state = State::kCdataSection;
        StateCdataSection(cp);
    }
}

// Confirms the final '>' of "]]>" and returns to the data state. A second ']'
// emits one bracket and keeps waiting; any other character emits the full
// "]]" prefix that had been tentatively consumed and resumes CDATA content.
void Tokenizer::StateCdataSectionEnd(char32_t cp) {
    switch (cp) {
        case static_cast<char32_t>(Cp::kGreaterThanSign): {
            state = State::kData;
            break;
        }
        case static_cast<char32_t>(Cp::kRightSquareBracket): {
            EmitChars(u"]");
            break;
        }
        default: {
            EmitChars(u"]]");
            state = State::kCdataSection;
            StateCdataSection(cp);
        }
    }
}

// Reached after a named character reference matched during attribute value
// scanning way without a trailing ';'. When the next character is an
// alphanumeric, the reference is flattened back into literal text (it was
// probably a stray '&' in an identifier); otherwise a missing ';' is signaled
// and the saved return state is restored with the current code point
// re-dispatched there.
void Tokenizer::StateAmbiguousAmpersand(char32_t cp) {
    if (IsAsciiAlphanumeric(cp)) {
        FlushCodePointConsumedAsCharacterReference(cp);
    } else {
        if (cp == static_cast<char32_t>(Cp::kSemicolon)) {
            ReportError(Err::kUnknownNamedCharacterReference);
        }

        state = return_state_;
        CallState(cp);
    }
}



// The input stream handling lives on the Preprocessor: it owns the raw
// UTF-16 text, tracks a read cursor plus line/column bookkeeping, normalizes
// newlines (CR becomes LF, and CRLF collapses into a single LF naming a gap
// so Retreat can walk the two code points as one logical newline), combines
// surrogate pairs into astral code points, and reports parse errors at
// unique offsets. The tokenizer calls Advance to pull the next code point,
// Peek/StartsWith for lookahead, and Retreat to rewind lookahead that turned
// out not to match. Chunked feeding is supported: when more chunks are
// expected, running past the current end yields U+0000 (Cp::kEof) with
// end_of_chunk_hit set rather than failing.

// Replaces the current document text and resets the cursor and all line
// bookkeeping so a fresh stream can be tokenized from scratch. Position -1
// means "before the first code unit", matching the first Advance reading the
// first character.
void Preprocessor::SetInput(std::u16string input) {
    html = std::move(input);
    pos = -1;
    line = 1;
    last_gap_pos_ = -2;
    gap_stack_.clear();
    skip_next_new_line_ = false;
    is_eol_ = false;
    line_start_pos_ = 0;
    last_err_offset_ = -1;
    last_chunk_written = true;
    end_of_chunk_hit = false;
}

// Appends another chunk to the input (streaming mode). The first chunk
// replaces any existing text via move for efficiency; later chunks are
// appended. Clearing end_of_chunk_hit signals that more input may still
// arrive, so pending-end lookahead can resume.
void Preprocessor::Write(std::u16string chunk, bool is_last_chunk) {
    if (html.empty()) {
        html = std::move(chunk);
    } else {
        html += chunk;
    }

    end_of_chunk_hit = false;
    last_chunk_written = is_last_chunk;
}

// Injects a fragment right after the cursor (at pos + 1) without moving it,
// typically to splice a replacement for a token whose reconstitution is
// needed. Resets end_of_chunk_hit so the inserted text is readable.
void Preprocessor::InsertHtmlAtCurrentPos(std::u16string_view chunk) {
    html.insert(static_cast<size_t>(pos + 1), chunk);

    end_of_chunk_hit = false;
}

// One-based column of the cursor in the current line. When the cursor sits
// exactly on a gap (a code unit skipped as part of a combined sequence such
// as a CRLF newline), the extra code unit is accounted for so columns stay
// aligned with the raw stream.
//
//   text "ab\nc", after consuming "ab"      -> 2
//   text "a\r\nb", cursor inside "..."     -> reflects the hidden CR
int32_t Preprocessor::Col() const {
    return pos - line_start_pos_ + (last_gap_pos_ != pos ? 1 : 0);
}

// Builds a ParserError for the current position, optionally shifted by
// cp_offset code units, so callers can attribute an error to a character
// slightly ahead of (or behind) the cursor. Line, column, and offset all
// describe a zero-width span.
ParserError Preprocessor::GetError(Err code, int32_t cp_offset) const {
    ParserError error;
    error.code = code;
    error.start_line = line;
    error.end_line = line;
    error.start_col = Col() + cp_offset;
    error.end_col = error.start_col;
    error.start_offset = Offset() + cp_offset;
    error.end_offset = error.start_offset;
    return error;
}

// Fires on_parse_error once per code-point offset: the last_err_offset_
// guard prevents a flood of identical reports when several errors pile up at
// the same spot during multi-character lookahead.
void Preprocessor::ReportError(Err code) {
    if (on_parse_error && last_err_offset_ != Offset()) {
        last_err_offset_ = Offset();
        on_parse_error(GetError(code, 0));
    }
}

// Records the current cursor as an opening gap and keeps the child position
// on the gap stack so Retreat can pop gaps back out in reverse order. The
// item pushed is the previous gap, linking each gap to the one enclosing it.
void Preprocessor::AddGap() {
    gap_stack_.push_back(last_gap_pos_);
    last_gap_pos_ = pos;
}

// Turns a code unit that turned out to be a high (or low) surrogate into the
// full astral code point by peeking the partner unit. When the low surrogate
// follows immediately, both code units are consumed and a gap is added so a
// later Retreat steps over the pair as a single unit. If the readable text
// ends right at the high surrogate while more chunks are still expected,
// EOF is reported instead of a premature pair. A surrogate with no partner
// is an isolated-surrogate parse error and is returned unchanged.
//
//   U+D801 U+DC37 -> U+10437
//   U+D800 <end, more chunks pending> -> U+0000 (Eof)
char32_t Preprocessor::ProcessSurrogate(char32_t cp) {
    if (pos != static_cast<int32_t>(html.size()) - 1) {
        const char32_t next_cp = html[static_cast<size_t>(pos + 1)];

        if (IsSurrogatePair(next_cp)) {
            ++pos;
            AddGap();
            return GetSurrogatePairCodePoint(cp, next_cp);
        }
    }

    else if (!last_chunk_written) {
        end_of_chunk_hit = true;
        return static_cast<char32_t>(Cp::kEof);
    }

    ReportError(Err::kSurrogateInInputStream);

    return cp;
}

// Case-sensitive or case-insensitive lookahead for a code-unit pattern at the
// current cursor without consuming anything. Case-insensitive comparison
// lowercases both sides using ASCII case folding. If the pattern would run
// past the buffered text while more chunks are pending, end_of_chunk_hit is
// raised so the caller can stall instead of mis-reading.
//
//   input "</SCR" matching "script", case_sensitive=false -> true
//   input "</sCR" matching "script", case_sensitive=true  -> false
bool Preprocessor::StartsWith(std::u16string_view pattern, bool case_sensitive) {
    if (pos + static_cast<int32_t>(pattern.size()) > static_cast<int32_t>(html.size())) {
        end_of_chunk_hit = !last_chunk_written;
        return false;
    }

    if (case_sensitive) {
        return html.compare(static_cast<size_t>(pos), pattern.size(), pattern.data(), pattern.size()) == 0;
    }

    for (size_t i = 0; i < pattern.size(); i++) {
        const auto cp =
            static_cast<uint32_t>(html[static_cast<size_t>(pos + static_cast<int32_t>(i))]) | 0x20;

        if (cp != static_cast<uint32_t>(pattern[i])) {
            return false;
        }
    }

    return true;
}

// Looks at the code point `offset` ahead of (positive) or behind (negative)
// the cursor without consuming anything. Out-of-range reads return EOF and
// set the chunk-pending flag when applicable. A carriage return is presented
// as CRLF-normalized LF so lookahead sees the same line endings the main
// loop consumes.
//
//   text "abc", cursor at 'a', Peek(1) -> 'b'
//   text "ab",  cursor at 'a', Peek(2) -> Eof
char32_t Preprocessor::Peek(int32_t offset) {
    const int32_t peek_pos = pos + offset;

    if (peek_pos >= static_cast<int32_t>(html.size()) || peek_pos < 0) {
        end_of_chunk_hit = !last_chunk_written;
        return static_cast<char32_t>(Cp::kEof);
    }

    const char32_t code = html[static_cast<size_t>(peek_pos)];

    return code == static_cast<char32_t>(Cp::kCarriageReturn)
               ? static_cast<char32_t>(Cp::kLineFeed)
               : code;
}

// Consumes and returns the next input code point. Newlines are normalized:
// CR yields LF and marks the position to line-bump on the following call,
// while an LF immediately after a CR is swallowed (with a gap) so CRLF reads
// as a single LF. Surrogates at this position are extended through
// ProcessSurrogate. Rare control/noncharacter values are validated for error
// reporting via the common-range fast path.
//
//   text "a\r\nb"             -> 'a', 'LF' (line bump), 'b'
//   text "<div>"              -> '<', then rest of the tag
char32_t Preprocessor::Advance() {
    ++pos;

    if (is_eol_) {
        is_eol_ = false;
        ++line;
        line_start_pos_ = pos;
    }

    if (pos >= static_cast<int32_t>(html.size())) {
        end_of_chunk_hit = !last_chunk_written;
        return static_cast<char32_t>(Cp::kEof);
    }

    char32_t cp = html[static_cast<size_t>(pos)];

    if (cp == static_cast<char32_t>(Cp::kCarriageReturn)) {
        is_eol_ = true;
        skip_next_new_line_ = true;
        return static_cast<char32_t>(Cp::kLineFeed);
    }

    if (cp == static_cast<char32_t>(Cp::kLineFeed)) {
        is_eol_ = true;

        if (skip_next_new_line_) {
            --line;
            skip_next_new_line_ = false;
            AddGap();
            return Advance();
        }
    }

    skip_next_new_line_ = false;

    if (IsSurrogate(cp)) {
        cp = ProcessSurrogate(cp);
    }

    const bool is_common_valid_range =
        !static_cast<bool>(on_parse_error) || (cp > 0x1f && cp < 0x7f) ||
        cp == static_cast<char32_t>(Cp::kLineFeed) ||
        cp == static_cast<char32_t>(Cp::kCarriageReturn) || (cp > 0x9f && cp < 0xFDD0);

    if (!is_common_valid_range) {
        CheckForProblematicCharacters(cp);
    }

    return cp;
}

// Reports the input-stream errors that apply to a rarely-seen code point,
// treating control characters and noncharacters separately so a single point
// produces a single precise diagnostic. The full validation is only reached
// through the narrow path that skips the common ASCII/BMP range.
void Preprocessor::CheckForProblematicCharacters(char32_t cp) {
    if (IsControlCodePoint(cp)) {
        ReportError(Err::kControlCharacterInInputStream);
    } else if (IsUndefinedCodePoint(cp)) {
        ReportError(Err::kNoncharacterInInputStream);
    }
}

// Rewinds the cursor by `count` code units (defaulting to a single unit in
// tokens that were tentatively consumed). Rewinding stops short of any outer
// gap: when the cursor touches a recorded gap, the gap is popped from the
// stack and one more unit is rewound, so combined sequences like a surrogate
// pair or a swallowed CRLF are retreated as one logical step. Line state is
// cleared because stepped-back lines no longer affect the pending column.
void Preprocessor::Retreat(int32_t count) {
    pos -= count;

    while (pos < last_gap_pos_) {
        last_gap_pos_ = gap_stack_.back();
        gap_stack_.pop_back();
        --pos;
    }

    is_eol_ = false;
}




// The doctype rules decide which rendering mode a document deserves based on
// its <!DOCTYPE> declaration. A conforming doctype ("html" with no public id
// and either no system id or the legacy-compat one) selects no-quirks; a
// known set of legacy public identifiers (folded to lower case) select
// quirks mode; and the XHTML 1.0 transitional/frameset identifiers select
// limited-quirks, in each case with extra rules depending on whether a
// system identifier accompanies the public one.


// Recognized names and identifiers. kValidDoctypeName is the only doctype
// name that passed the 'html' bar; kValidSystemId is the sole harmless
// system identifier. The sprinkled legacy public-id lists below are matched
// (lower-cased) against a document's public identifier to decide whether a
// real browser needs quirks or limited-quirks emulation.
namespace {

inline constexpr std::string_view kValidDoctypeName = "html";
inline constexpr std::string_view kValidSystemId = "about:legacy-compat";
inline constexpr std::string_view kQuirksModeSystemId =
    "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd";

inline constexpr std::array<std::string_view, 55> kQuirksModePublicIdPrefixes = {{
    "+//silmaril//dtd html pro v0r11 19970101//",
    "-//as//dtd html 3.0 aswedit + extensions//",
    "-//advasoft ltd//dtd html 3.0 aswedit + extensions//",
    "-//ietf//dtd html 2.0 level 1//",
    "-//ietf//dtd html 2.0 level 2//",
    "-//ietf//dtd html 2.0 strict level 1//",
    "-//ietf//dtd html 2.0 strict level 2//",
    "-//ietf//dtd html 2.0 strict//",
    "-//ietf//dtd html 2.0//",
    "-//ietf//dtd html 2.1e//",
    "-//ietf//dtd html 3.0//",
    "-//ietf//dtd html 3.2 final//",
    "-//ietf//dtd html 3.2//",
    "-//ietf//dtd html 3//",
    "-//ietf//dtd html level 0//",
    "-//ietf//dtd html level 1//",
    "-//ietf//dtd html level 2//",
    "-//ietf//dtd html level 3//",
    "-//ietf//dtd html strict level 0//",
    "-//ietf//dtd html strict level 1//",
    "-//ietf//dtd html strict level 2//",
    "-//ietf//dtd html strict level 3//",
    "-//ietf//dtd html strict//",
    "-//ietf//dtd html//",
    "-//metrius//dtd metrius presentational//",
    "-//microsoft//dtd internet explorer 2.0 html strict//",
    "-//microsoft//dtd internet explorer 2.0 html//",
    "-//microsoft//dtd internet explorer 2.0 tables//",
    "-//microsoft//dtd internet explorer 3.0 html strict//",
    "-//microsoft//dtd internet explorer 3.0 html//",
    "-//microsoft//dtd internet explorer 3.0 tables//",
    "-//netscape comm. corp.//dtd html//",
    "-//netscape comm. corp.//dtd strict html//",
    "-//o'reilly and associates//dtd html 2.0//",
    "-//o'reilly and associates//dtd html extended 1.0//",
    "-//o'reilly and associates//dtd html extended relaxed 1.0//",
    "-//sq//dtd html 2.0 hotmetal + extensions//",
    "-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//",
    "-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
    "-//spyglass//dtd html 2.0 extended//",
    "-//sun microsystems corp.//dtd hotjava html//",
    "-//sun microsystems corp.//dtd hotjava strict html//",
    "-//w3c//dtd html 3 1995-03-24//",
    "-//w3c//dtd html 3.2 draft//",
    "-//w3c//dtd html 3.2 final//",
    "-//w3c//dtd html 3.2//",
    "-//w3c//dtd html 3.2s draft//",
    "-//w3c//dtd html 4.0 frameset//",
    "-//w3c//dtd html 4.0 transitional//",
    "-//w3c//dtd html experimental 19960712//",
    "-//w3c//dtd html experimental 970421//",
    "-//w3c//dtd w3 html//",
    "-//w3o//dtd w3 html 3.0//",
    "-//webtechs//dtd mozilla html 2.0//",
    "-//webtechs//dtd mozilla html//",
}};

inline constexpr std::array<std::string_view, 2> kNoSystemIdExtraPrefixes = {{
    "-//w3c//dtd html 4.01 frameset//",
    "-//w3c//dtd html 4.01 transitional//",
}};

inline constexpr std::array<std::string_view, 3> kQuirksModePublicIds = {{
    "-//w3o//dtd w3 html strict 3.0//en//",
    "-/w3c/dtd html 4.0 transitional/en",
    "html",
}};

inline constexpr std::array<std::string_view, 2> kLimitedQuirksPublicIdPrefixes = {{
    "-//w3c//dtd xhtml 1.0 frameset//",
    "-//w3c//dtd xhtml 1.0 transitional//",
}};

// True when the (already lower-cased) public identifier starts with any of
// the given prefixes, so legacy identifiers that share a common preamble can
// be matched in one pass.
//
//   public_id "-//w3c//dtd html 4.0 transitional//EN"
//      vs kQuirksModePublicIdPrefixes             -> true
template <size_t N>
bool HasPrefix(std::string_view public_id, const std::array<std::string_view, N>& prefixes) {
    for (std::string_view prefix : prefixes) {
        if (public_id.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

// Applies the quirks/limited-quirks prefix tables to a lower-cased public
// identifier. The with_system_id flag suppresses the extra "no system id
// only" prefixes, since those identifiers only force quirks when no system
// identifier is present. When limited_quirks is requested only the XHTML 1.0
// transitional/frameset prefixes apply.
bool HasAnyPrefix(std::string_view public_id, bool with_system_id, bool limited_quirks) {
    if (!limited_quirks) {
        if (HasPrefix(public_id, kQuirksModePublicIdPrefixes)) {
            return true;
        }
        return !with_system_id && HasPrefix(public_id, kNoSystemIdExtraPrefixes);
    }
    if (HasPrefix(public_id, kLimitedQuirksPublicIdPrefixes)) {
        return true;
    }
    return !with_system_id && HasPrefix(public_id, kNoSystemIdExtraPrefixes);
}

// ASCII-only case folding: uppercase letters become their lowercase
// equivalents and everything else is copied verbatim, mirroring how the
// identifier tables are spelled.
std::string ToLowerAscii(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        result += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return result;
}

}

// A doctype is conforming when it has the exact name "html", carries no
// public identifier, and either has no system identifier or only the
// harmless "about:legacy-compat" one. Any deviation means the document is
// not authored against the current HTML spec.
//
//   name "html", no ids                       -> true
//   name "html", public "x", no system id     -> false
bool IsConformingDoctype(const DoctypeToken& token) {
    return token.has_name && token.name == kValidDoctypeName && !token.has_public_id &&
        (!token.has_system_id || token.system_id == kValidSystemId);
}

// Selects the document rendering mode the token requires. Anything with a
// wrong or missing name is quirks. A valid name with the IBM transitional
// system id, or a lower-cased public id that matches the quirks tables, is
// quirks; the XHTML 1.0 prefixes give limited-quirks; and everything else
// that kept its "html" name is no-quirks.
//
//   "<!doctype html>"                       -> kNoQuirks
//   name "html", public "-//w3c//dtd html 4.0 transitional//EN"
//      with no system id                    -> kQuirks
//   name "html", public
//      "-//w3c//dtd xhtml 1.0 transitional//EN"
//      with system id                       -> kLimitedQuirks
DocumentMode GetDocumentMode(const DoctypeToken& token) {
    if (!token.has_name || token.name != kValidDoctypeName) {
        return DocumentMode::kQuirks;
    }

    if (token.has_system_id && ToLowerAscii(token.system_id) == kQuirksModeSystemId) {
        return DocumentMode::kQuirks;
    }

    if (token.has_public_id) {
        std::string public_id = ToLowerAscii(token.public_id);

        for (std::string_view id : kQuirksModePublicIds) {
            if (public_id == id) {
                return DocumentMode::kQuirks;
            }
        }

        bool has_system_id = token.has_system_id;
        if (HasAnyPrefix(public_id, has_system_id, false)) {
            return DocumentMode::kQuirks;
        }
        if (HasAnyPrefix(public_id, has_system_id, true)) {
            return DocumentMode::kLimitedQuirks;
        }
    }

    return DocumentMode::kNoQuirks;
}

}