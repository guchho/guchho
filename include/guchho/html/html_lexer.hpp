// ===========================================================================
// Guchho HTML lexing layer.
//
// Everything Guchho needs to turn a stream of UTF-16 input code units into a
// flat sequence of tokens lives in this header: the token structures, the
// parse-error catalogue, doctype quirks-mode detection, the input
// preprocessor and the tokenizer state machine. Named character reference
// data is pulled from the core module "guchho/entities.hpp" (mirroring
// "guchho/unicode.hpp"), so this layer never hard-codes entity names.
//
// The tokenizer always consumes a complete input buffer at once: you feed the
// whole document and receive every token back through a TokenHandler. The
// preprocessor normalizes CR/CRLF line endings, tracks one-based lines and
// columns, joins UTF-16 surrogate pairs into single code points and reports
// problematic characters (control, noncharacter or surrogate code points) the
// moment they cross the input boundary.
// ===========================================================================

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "guchho/html/html_helpers.hpp"

namespace guchho::html {

// ===========================================================================
// Token definitions.
// ===========================================================================

// Every kind of object the tokenizer hands to a token handler. Character data
// is split into three distinct types so a handler can cheaply tell apart
// ordinary text, U+0000 and plain whitespace without inspecting the payload.
enum class TokenType : uint8_t {
    kCharacter,
    kNullCharacter,
    kWhitespaceCharacter,
    kStartTag,
    kEndTag,
    kComment,
    kDoctype,
    kEof,
};

// Source span of a token or element.
//
// Line and column numbers are one-based while offsets are zero-based. The
// "end" fields always point just past the last character of the span, so a
// token covers the half-open interval [start_offset, end_offset) and text
// like "ab" spans start_col=1, end_col=3.
struct Location {
    // One-based line index of the first character.
    int32_t start_line = 0;
    // One-based column index of the first character.
    int32_t start_col = 0;
    // Zero-based index of the first character.
    int32_t start_offset = 0;
    // One-based line index of the last character.
    int32_t end_line = 0;
    // One-based column index just past the last character.
    int32_t end_col = 0;
    // Zero-based index just past the last character.
    int32_t end_offset = 0;
};

// A single attribute of a start or end tag.
//
// The name is the lowercased attribute key and the value is the raw,
// entity-decoded attribute payload; both are stored as UTF-8. "prefix" and
// "ns" hold the XML namespace decomposition once the tree builder resolves
// the attribute.
struct Attribute {
    std::string name;
    std::string prefix;
    bool has_prefix = false;
    NS ns = NS::kHtml;

    std::string value;
};

// A <!DOCTYPE ...> token.
//
// The has_* flags distinguish "identifier omitted" from "identifier present
// but empty", which matters for quirks-mode detection. "force_quirks" is
// raised when the doctype contains a malformed or public-identifier-based
// declaration that forbids standards mode.
struct DoctypeToken {
    TokenType type = TokenType::kDoctype;
    Location* location = nullptr;

    bool has_name = false;
    std::string name;
    bool force_quirks = false;
    bool has_public_id = false;
    std::string public_id;
    bool has_system_id = false;
    std::string system_id;
};

// A start or end tag token.
//
// "tag_name" is stored lowercased, matching how attribute names are
// normalized. "tag_id" caches the resolved TagId so the parser does not have
// to re-derive it for every token. "ack_self_closing" marks a self-closing
// token whose slash was acknowledged by its namespace, which suppresses the
// trailing-solidus parse error.
struct TagToken {
    TokenType type = TokenType::kStartTag;
    Location* location = nullptr;

    std::string tag_name;
    // Caches the ID of the tag name.
    TagId tag_id = TagId::kUnknown;
    bool self_closing = false;
    bool ack_self_closing = false;
    std::vector<Attribute> attrs;
};

// A <!-- comment --> token. "data" holds the text between the delimiters.
struct CommentToken {
    TokenType type = TokenType::kComment;
    Location* location = nullptr;

    std::string data;
};

// An end-of-input token, delivered exactly once after all other tokens.
//
// The tokenizer marks itself inactive immediately after emitting it, so a
// packed loop feeding the lexer cannot accidentally continue past the end of
// the buffer.
struct EofToken {
    TokenType type = TokenType::kEof;
    Location* location = nullptr;
};

// A run of consecutive character data.
//
// Consecutive code points sharing a token type are coalesced, so "chars" may
// hold many characters (for example the full text of a paragraph). The run is
// cut wherever the token type would change, such as a whitespace or null
// character inside otherwise ordinary text.
struct CharacterToken {
    TokenType type = TokenType::kCharacter;
    Location* location = nullptr;

    std::string chars;
};

// Returns the value of the most recently added attribute named "attr_name",
// or nullptr when no such attribute exists.
//
// Example: GetTokenAttr(token, "href") on <a href="http://a" href="http://b">
// returns "http://b".
//
// Attributes are scanned from the back because the HTML5 tokenization
// algorithm keeps the *first* value of a duplicated attribute while later
// duplicates produce parse errors; this helper therefore surfaces the value
// that survives for tree building. The returned pointer stays valid as long
// as the token does.
const std::string* GetTokenAttr(const TagToken& token, std::string_view attr_name);

// ===========================================================================
// Parse error codes.
// ===========================================================================

// The catalogue of parse errors Guchho can report, ordered to match the
// WHATWG "parse error" list. Codes are deliberately listed in a stable order
// so callers can switch on them portably and the tree builders can react to
// malformed input without inspecting raw lexer text.
enum class Err : uint8_t {
    kControlCharacterInInputStream,
    kNoncharacterInInputStream,
    kSurrogateInInputStream,
    kNonVoidHtmlElementStartTagWithTrailingSolidus,
    kEndTagWithAttributes,
    kEndTagWithTrailingSolidus,
    kUnexpectedSolidusInTag,
    kUnexpectedNullCharacter,
    kUnexpectedQuestionMarkInsteadOfTagName,
    kInvalidFirstCharacterOfTagName,
    kUnexpectedEqualsSignBeforeAttributeName,
    kMissingEndTagName,
    kUnexpectedCharacterInAttributeName,
    kUnknownNamedCharacterReference,
    kMissingSemicolonAfterCharacterReference,
    kUnexpectedCharacterAfterDoctypeSystemIdentifier,
    kUnexpectedCharacterInUnquotedAttributeValue,
    kEofBeforeTagName,
    kEofInTag,
    kMissingAttributeValue,
    kMissingWhitespaceBetweenAttributes,
    kMissingWhitespaceAfterDoctypePublicKeyword,
    kMissingWhitespaceBetweenDoctypePublicAndSystemIdentifiers,
    kMissingWhitespaceAfterDoctypeSystemKeyword,
    kMissingQuoteBeforeDoctypePublicIdentifier,
    kMissingQuoteBeforeDoctypeSystemIdentifier,
    kMissingDoctypePublicIdentifier,
    kMissingDoctypeSystemIdentifier,
    kAbruptDoctypePublicIdentifier,
    kAbruptDoctypeSystemIdentifier,
    kCdataInHtmlContent,
    kIncorrectlyOpenedComment,
    kEofInScriptHtmlCommentLikeText,
    kEofInDoctype,
    kNestedComment,
    kAbruptClosingOfEmptyComment,
    kEofInComment,
    kIncorrectlyClosedComment,
    kEofInCdata,
    kAbsenceOfDigitsInNumericCharacterReference,
    kNullCharacterReference,
    kSurrogateCharacterReference,
    kCharacterReferenceOutsideUnicodeRange,
    kControlCharacterReference,
    kNoncharacterCharacterReference,
    kMissingWhitespaceBeforeDoctypeName,
    kMissingDoctypeName,
    kInvalidCharacterSequenceAfterDoctypeName,
    kDuplicateAttribute,
    kNonConformingDoctype,
    kMissingDoctype,
    kMisplacedDoctype,
    kEndTagWithoutMatchingOpenElement,
    kClosingOfElementWithOpenChildElements,
    kDisallowedContentInNoscriptInHead,
    kOpenElementsLeftAfterEof,
    kAbandonedHeadElementChild,
    kMisplacedStartTagForHeadElement,
    kNestedNoscriptInHead,
    kEofInElementThatCanContainOnlyText,
};

// A parse error bound to a specific source location.
//
// Because it inherits from Location, a ParserError can be treated as a normal
// span; "code" identifies what went wrong at that span.
struct ParserError : Location {
    Err code = Err::kControlCharacterInInputStream;
};

// ===========================================================================
// Doctype quirks-mode detection.
// ===========================================================================

// Reports whether a doctype token satisfies the conformance requirements.
//
// Example: IsConformingDoctype(token from `<!DOCTYPE html>`) yields true,
// while any doctype carrying a public identifier or a non-HTML name yields
// false.
//
// The declaration is conforming only when it carries the literal name "html"
// and has neither a public nor a system identifier. It is used to decide how
// aggressively the tree builder should tolerate legacy quirks.
bool IsConformingDoctype(const DoctypeToken& token);

// Resolves the rendering mode implied by a doctype token.
//
// Example: GetDocumentMode(`<!DOCTYPE html>`) returns DocumentMode::kNoQuirks;
// GetDocumentMode(`<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">`)
// returns DocumentMode::kQuirks; a transitional system identifier returns
// DocumentMode::kLimitedQuirks.
//
// The decision follows the WHATWG table: matching modern names yield no
// quirks, legacy public identifier prefixes or missing/forbidden system
// identifiers degrade to quirks or limited quirks, and an unknown name with
// both identifiers present stays in standards mode only when the identifiers
// are well formed.
DocumentMode GetDocumentMode(const DoctypeToken& token);

// ===========================================================================
// Input preprocessing.
// ===========================================================================

// Buffers the entire document and answers character-oriented questions for the
// tokenizer: normalized line endings, one-based line/column tracking, UTF-16
// surrogate joining and, at EOF, the synthetic End-of-File code point.
// Unlike a streaming feed, this preprocessor holds the whole input in one
// buffer and never drops consumed regions, so lookahead across any distance is
// always safe.
class Preprocessor {
public:
    // The whole input, consumed from position "pos".
    std::u16string html;
    int32_t pos = -1;
    int32_t line = 1;

    explicit Preprocessor() = default;

    // Optional parse error sink.
    std::function<void(const ParserError&)> on_parse_error;

    // Set when the last chunk was fed through "Write"; while false, more input
    // may still arrive and the tokenizer must not treat the buffer end as a
    // real document end.
    bool last_chunk_written = false;
    // Set by "Advance"/"Peek"/"StartsWith" when they hit the end of the
    // current buffer while more chunks may follow.
    bool end_of_chunk_hit = false;

    // Replaces the whole input buffer and resets the cursor to before the
    // first character.
    //
    // Example: SetInput(u"<p>Hi") leaves html == "<p>Hi" with pos == -1, so
    // the next Advance() returns '<'.
    //
    // Line, column, gap and surrogate state are reset alongside the buffer so
    // the preprocessor is always reusable for a fresh document.
    void SetInput(std::u16string input);

    // Appends a chunk to the input buffer.
    //
    // Example: Write(u"<p>", false); Write(u"Hi", true) assembles the buffer
    // "<p>Hi" in two calls, preserving positional state across the calls.
    //
    // Unlike SetInput, line/column tracking state is carried over, which lets
    // a caller stream a document in pieces. The is_last_chunk flag marks the
    // final piece so the tokenizer may proceed to EOF afterwards.
    void Write(std::u16string chunk, bool is_last_chunk);

    // Inserts text immediately after the current position without moving the
    // cursor.
    //
    // Example: with pos == 2, InsertHtmlAtCurrentPos(u"!--") splices the text
    // into the buffer so the next Advance() sees it, which lets a handler
    // synthesize markup mid-document.
    void InsertHtmlAtCurrentPos(std::u16string_view chunk);

    // Returns the column of the current position on its line.
    //
    // Example: after consuming "ab\nc" up to the 'd', Col() == 2. If the cursor
    // sits just past a gap (for example a surrogate pair), the column of the
    // first gap unit is returned so columns stay stable.
    int32_t Col() const;

    // Returns the zero-based offset of the current position (the raw "pos").
    int32_t Offset() const { return pos; }

    // Builds a ParserError for the given code, located offset units before the
    // current position.
    //
    // Example: GetError(Err::kAbruptClosingOfEmptyComment, 2) locates the
    // error two units behind the cursor, so the error span covers the text
    // that triggered it.
    ParserError GetError(Err code, int32_t cp_offset) const;

    // Checks whether the input starting at the current position (which itself
    // is not included in the pattern) begins with "pattern".
    //
    // Example: at pos pointing at 's' in "script>", StartsWith(u"script", false)
    // returns true and leaves the cursor untouched.
    //
    // When case_sensitive is false, ASCII letters are compared case-folding.
    bool StartsWith(std::u16string_view pattern, bool case_sensitive);

    // Peeks "offset" UTF-16 units ahead of the current position without
    // consuming anything.
    //
    // Example: after consuming the '<' of "<br>", Peek(1) returns 'r' and, at
    // the very end of the buffer, Peek(0) returns Cp::kEof.
    //
    // A carriage return is reported as a line feed, and a carriage return
    // followed by a line feed is reported as a single line feed. Past the
    // buffer end Cp::kEof is returned.
    char32_t Peek(int32_t offset);

    // Advances one UTF-16 unit and returns the code point it covered.
    //
    // Example: from pos == 2 over the pair u"\uD83D\uDE00", Advance() consumes
    // both units and returns U+1F600, positioning the cursor after the pair.
    //
    // CR and CRLF are folded to LF (flagging skip_next_new_line_ so the LF half
    // of CRLF is silently absorbed), surrogate pairs are joined, line counters
    // are updated and problematic characters are reported once.
    char32_t Advance();

    // Moves the cursor back by "count" UTF-16 units.
    //
    // Example: Retreat(1) after consuming 'x' re-exposes 'x' to the next
    // Advance() call, which is how the tokenizer rejects a lookahead that did
    // not match.
    void Retreat(int32_t count);

private:
    // last_gap_pos_ starts at -2 so the very first Col() evaluates to 0.
    int32_t last_gap_pos_ = -2;
    std::vector<int32_t> gap_stack_;
    bool skip_next_new_line_ = false;
    bool is_eol_ = false;
    int32_t line_start_pos_ = 0;

    // last_err_offset_ guards against double-reporting the same boundary
    // character when Advance and Retreat both cross it.
    int32_t last_err_offset_ = -1;

    void ReportError(Err code);
    void AddGap();
    char32_t ProcessSurrogate(char32_t cp);
    void CheckForProblematicCharacters(char32_t cp);
};

// ===========================================================================
// Tokenizer.
// ===========================================================================

// The distinct lexical contexts of the tokenizer, mirroring the HTML5
// tokenization state machine. The name of each state describes which input
// stream construct the lexer is currently inside (data, RCDATA, script data,
// comments, doctype, CDATA, and so on).
enum class State : uint8_t {
    kData,
    kRcdata,
    kRawtext,
    kScriptData,
    kPlaintext,
    kTagOpen,
    kEndTagOpen,
    kTagName,
    kRcdataLessThanSign,
    kRcdataEndTagOpen,
    kRcdataEndTagName,
    kRawtextLessThanSign,
    kRawtextEndTagOpen,
    kRawtextEndTagName,
    kScriptDataLessThanSign,
    kScriptDataEndTagOpen,
    kScriptDataEndTagName,
    kScriptDataEscapeStart,
    kScriptDataEscapeStartDash,
    kScriptDataEscaped,
    kScriptDataEscapedDash,
    kScriptDataEscapedDashDash,
    kScriptDataEscapedLessThanSign,
    kScriptDataEscapedEndTagOpen,
    kScriptDataEscapedEndTagName,
    kScriptDataDoubleEscapeStart,
    kScriptDataDoubleEscaped,
    kScriptDataDoubleEscapedDash,
    kScriptDataDoubleEscapedDashDash,
    kScriptDataDoubleEscapedLessThanSign,
    kScriptDataDoubleEscapeEnd,
    kBeforeAttributeName,
    kAttributeName,
    kAfterAttributeName,
    kBeforeAttributeValue,
    kAttributeValueDoubleQuoted,
    kAttributeValueSingleQuoted,
    kAttributeValueUnquoted,
    kAfterAttributeValueQuoted,
    kSelfClosingStartTag,
    kBogusComment,
    kMarkupDeclarationOpen,
    kCommentStart,
    kCommentStartDash,
    kComment,
    kCommentLessThanSign,
    kCommentLessThanSignBang,
    kCommentLessThanSignBangDash,
    kCommentLessThanSignBangDashDash,
    kCommentEndDash,
    kCommentEnd,
    kCommentEndBang,
    kDoctype,
    kBeforeDoctypeName,
    kDoctypeName,
    kAfterDoctypeName,
    kAfterDoctypePublicKeyword,
    kBeforeDoctypePublicIdentifier,
    kDoctypePublicIdentifierDoubleQuoted,
    kDoctypePublicIdentifierSingleQuoted,
    kAfterDoctypePublicIdentifier,
    kBetweenDoctypePublicAndSystemIdentifiers,
    kAfterDoctypeSystemKeyword,
    kBeforeDoctypeSystemIdentifier,
    kDoctypeSystemIdentifierDoubleQuoted,
    kDoctypeSystemIdentifierSingleQuoted,
    kAfterDoctypeSystemIdentifier,
    kBogusDoctype,
    kCdataSection,
    kCdataSectionBracket,
    kCdataSectionEnd,
    kCharacterReference,
    kAmbiguousAmpersand,
};

// Tokenizer initial states for the different content models.
//
// These act as entry points: feeding a text buffer with one of these states
// pre-selects the tokenizer into data, RCDATA, RAWTEXT, script data, plaintext
// or CDATA mode before the first code unit is consumed.
namespace tokenizer_mode {
inline constexpr State kData = State::kData;
inline constexpr State kRcdata = State::kRcdata;
inline constexpr State kRawtext = State::kRawtext;
inline constexpr State kScriptData = State::kScriptData;
inline constexpr State kPlaintext = State::kPlaintext;
inline constexpr State kCdataSection = State::kCdataSection;
} // namespace tokenizer_mode

// Token sink: the interface every consumer of the tokenizer implements to
// receive tokens as they are produced.
//
// Callbacks arrive in document order and never overlap: each token is fully
// delivered before the next one begins. The optional "on_parse_error" hook
// receives the parse errors encountered along the way; when it is not set,
// errors are silently skipped.
class TokenHandler {
public:
    virtual ~TokenHandler() = default;

    // A comment token (<!-- ... -->).
    virtual void OnComment(CommentToken& token) = 0;
    // A doctype token (<!DOCTYPE ...>).
    virtual void OnDoctype(DoctypeToken& token) = 0;
    // A start tag token (<div ...>).
    virtual void OnStartTag(TagToken& token) = 0;
    // An end tag token (</div>); an end tag never carries useful attrs.
    virtual void OnEndTag(TagToken& token) = 0;
    // The single end-of-input token.
    virtual void OnEof(EofToken& token) = 0;
    // A run of ordinary characters.
    virtual void OnCharacter(CharacterToken& token) = 0;
    // A run of U+0000 characters (always reported as a parse error at source).
    virtual void OnNullCharacter(CharacterToken& token) = 0;
    // A run of whitespace-only characters.
    virtual void OnWhitespaceCharacter(CharacterToken& token) = 0;

    std::function<void(const ParserError&)> on_parse_error;
};

// Appends a single code point to a UTF-8 string.
//
// Example: AppendCodePoint(out, U'€') appends the bytes 0xE2 0x82 0xAC.
//
// Accepts any code point in 0x0000..0x10FFFF, producing one UTF-8 sequence of
// one to four bytes based on the magnitude of the value.
void AppendCodePoint(std::string& out, char32_t cp);

// Converts a UTF-8 string to UTF-16, pairing surrogate halves for code points
// above U+FFFF.
//
// Example: ToUtf16("😀") returns the two units {0xD83D, 0xDE00}.
//
// The conversion is exact; malformed input is not validated and is decoded
// in a best-effort way. It is used to produce "last_start_tag_name", which
// must be compared against the UTF-16 input buffer.
std::u16string ToUtf16(std::string_view text);

// The tokenizer: a state machine that converts the buffered UTF-16 input into
// a token stream.
//
// Typical usage is to construct the tokenizer with a handler, call Write with
// the entire document and is_last_chunk = true, and observe the handler
// callbacks firing as tokens are produced. The tokenizer owns its handouts
// only transiently: locations are pool-owned (valid for the tokenizer's whole
// lifetime) while token objects passed to the handler are valid only until
// the next token is emitted.
class Tokenizer {
public:
    Preprocessor preprocessor;

    // Indicates that the current adjusted node exists, is not an element in the
    // HTML namespace, and is neither an integration point for MathML nor for
    // HTML. The tokenizer uses this to decide foreign-content handling.
    bool in_foreign_node = false;
    // The lowercased name of the most recent start tag, kept in UTF-16 so it
    // can be matched against the input buffer when spotting appropriate end
    // tags inside RCDATA/RAWTEXT/script data.
    std::u16string last_start_tag_name;
    // True while the tokenizer still has work to do; cleared at EOF.
    bool active = false;
    // The current lexical state.
    State state = State::kData;

    // Builds the tokenizer around "handler" (which may optionally provide
    // on_parse_error) and enables or disables source location tracking.
    Tokenizer(TokenHandler& handler, bool source_code_location_info);

    // Whole-buffer API: feed the entire input, then run to EOF.
    //
    // Example: Write(u"<p>Hello", true) tokenizes immediately into a start tag
    // "p", a character run "Hello", and an EOF token.
    //
    // The state machine runs to completion before returning unless the handler
    // calls Pause(). Passing is_last_chunk = false defers EOF handling until a
    // later Write marks the final chunk.
    void Write(std::u16string chunk, bool is_last_chunk);

    // Suspends the parsing loop mid-run.
    void Pause();

    // Resumes a previously paused tokenizer.
    //
    // Example: after Pause() returns, Resume() continues exactly where the
    // tokenizer stopped. Resuming a tokenizer that was never paused throws.
    void Resume();

    // Injects new markup at the current position and resumes tokenization.
    //
    // Example: while tokenizing "<b>", calling InsertHtmlAtCurrentPos(u"i>")
    // at the right cursor splices "</i" style content into the token stream.
    void InsertHtmlAtCurrentPos(std::u16string_view chunk);

private:
    TokenHandler& handler_;
    bool source_code_location_info_;
    bool paused_ = false;
    bool in_loop_ = false;

    State return_state_ = State::kData;

    // Number of UTF-16 units consumed since the last loop-iteration snapshot;
    // used to retreat to a safe position when a chunk boundary is hit.
    int32_t consumed_after_snapshot_ = 0;

    // Cursor where the current character reference started.
    int32_t entity_start_pos_ = 0;

    // Locations are pool-owned and stay valid for the tokenizer's lifetime.
    Location* current_location_ = nullptr;
    std::vector<std::unique_ptr<Location>> location_pool_;

    std::unique_ptr<CharacterToken> current_character_token_;

    TagToken tag_token_;
    CommentToken comment_token_;
    DoctypeToken doctype_token_;
    Attribute current_attr_;
    // Index of the accepted attribute inside "tag_token_.attrs" once its name
    // is complete; -1 while the name is being accumulated or when the attribute
    // was rejected as a duplicate.
    int32_t current_attr_index_ = -1;

    // Live storage of the current attribute's value.
    std::string& CurrentAttrValue();

    // Errors

    // Reports a parse error at the current position (or offset units back).
    void ReportError(Err code, int32_t cp_offset = 0);
    // Returns a fresh location for a token that starts "offset" units back.
    Location* GetCurrentLocation(int32_t offset);
    // Allocates a pool-owned, uninitialized Location.
    Location* NewLocation();

    // Loop

    // Retreats to the pre-chunk position when the buffer ran out mid-iteration.
    bool EnsureHibernation();
    // Runs the state machine until paused, inactive or buffer exhausted.
    void RunParsingLoop();
    // Consumes one UTF-16 unit and returns its (possibly surrogate-joined) code
    // point.
    char32_t Consume();

    // Consumption helpers

    // Skips forward by "count" units, keeping the snapshot count in sync.
    void AdvanceBy(int32_t count);
    // Consumes a pattern if it matches at the current position.
    bool ConsumeSequenceIfMatch(std::u16string_view pattern, bool case_sensitive);

    // Token creation

    void CreateStartTagToken();
    void CreateEndTagToken();
    void CreateCommentToken(int32_t offset);
    void CreateDoctypeToken(const std::u16string* initial_name);
    void CreateCharacterToken(TokenType type, std::u16string_view chars);

    // Tag attributes

    // Begins a fresh pending attribute, seeding the name with one code point if
    // given.
    void CreateAttr(char32_t attr_name_first_ch);
    // Finalizes the pending attribute name, storing or rejecting the attribute.
    void LeaveAttrName();
    // Closes the pending attribute's value location.
    void LeaveAttrValue();

    // Token emission

    // Emits a pending character token and closes the next token's location.
    Location* PrepareToken(Location* next_location);
    void EmitCurrentTagToken();
    void EmitCurrentComment(CommentToken& ct);
    void EmitCurrentDoctype(DoctypeToken& ct);
    // Emits (or coalesces into) the pending character token.
    void EmitCurrentCharacterToken(Location* next_location);
    // Emits the final EOF token and deactivates the tokenizer.
    void EmitEofToken();

    // Characters emission

    // Appends to the current run or starts a new one when the type differs.
    void AppendCharToCurrentCharacterToken(TokenType type, std::u16string_view ch);
    // Emits a single code point, choosing its token type automatically.
    void EmitCodePoint(char32_t cp);
    // Emits an explicit (non-whitespace, non-null) character run.
    void EmitChars(std::u16string_view ch);

    // Character reference helpers

    // Transitions into the character reference state, remembering the caller.
    void StartCharacterReference();
    // True when the character reference is inside an attribute value.
    bool IsCharacterReferenceInAttribute() const;
    // Sends a decoded code point to either the attribute value or character
    // stream, depending on context.
    void FlushCodePointConsumedAsCharacterReference(char32_t cp);
    // Decodes a numeric character reference; "pos" points at the '#'.
    //
    // Example: decoding "&#65;" from the '#' yields codepoints[] = {65},
    // cp_count = 1 and consumed = 2; decoding "&#x41;" yields {65}, 1 and 4.
    //
    // Returns the number of input units consumed after the '&' (including an
    // optional trailing ';'), or 0 when no digits were present. Overflowing
    // values are clamped and reported; surrogates and out-of-range code points
    // map to U+FFFD.
    int32_t DecodeNumericEntity(uint32_t (&codepoints)[2], size_t& cp_count);
    // Drives the character reference state, dispatching to named or numeric
    // decoding and settling the fallback behavior for a failed reference.
    void StateCharacterReference();

    // State machine

    // Dispatches the current code point to the active state handler.
    void CallState(char32_t cp);
    // Matches an appropriate end tag for the last start tag in RCDATA/RAWTEXT/
    // script data; returns false when it fully consumed the end tag.
    bool HandleSpecialEndTag(char32_t cp);

    void StateData(char32_t cp);
    void StateRcdata(char32_t cp);
    void StateRawtext(char32_t cp);
    void StateScriptData(char32_t cp);
    void StatePlaintext(char32_t cp);
    void StateTagOpen(char32_t cp);
    void StateEndTagOpen(char32_t cp);
    void StateTagName(char32_t cp);
    void StateRcdataLessThanSign(char32_t cp);
    void StateRcdataEndTagOpen(char32_t cp);
    void StateRcdataEndTagName(char32_t cp);
    void StateRawtextLessThanSign(char32_t cp);
    void StateRawtextEndTagOpen(char32_t cp);
    void StateRawtextEndTagName(char32_t cp);
    void StateScriptDataLessThanSign(char32_t cp);
    void StateScriptDataEndTagOpen(char32_t cp);
    void StateScriptDataEndTagName(char32_t cp);
    void StateScriptDataEscapeStart(char32_t cp);
    void StateScriptDataEscapeStartDash(char32_t cp);
    void StateScriptDataEscaped(char32_t cp);
    void StateScriptDataEscapedDash(char32_t cp);
    void StateScriptDataEscapedDashDash(char32_t cp);
    void StateScriptDataEscapedLessThanSign(char32_t cp);
    void StateScriptDataEscapedEndTagOpen(char32_t cp);
    void StateScriptDataEscapedEndTagName(char32_t cp);
    void StateScriptDataDoubleEscapeStart(char32_t cp);
    void StateScriptDataDoubleEscaped(char32_t cp);
    void StateScriptDataDoubleEscapedDash(char32_t cp);
    void StateScriptDataDoubleEscapedDashDash(char32_t cp);
    void StateScriptDataDoubleEscapedLessThanSign(char32_t cp);
    void StateScriptDataDoubleEscapeEnd(char32_t cp);
    void StateBeforeAttributeName(char32_t cp);
    void StateAttributeName(char32_t cp);
    void StateAfterAttributeName(char32_t cp);
    void StateBeforeAttributeValue(char32_t cp);
    void StateAttributeValueDoubleQuoted(char32_t cp);
    void StateAttributeValueSingleQuoted(char32_t cp);
    void StateAttributeValueUnquoted(char32_t cp);
    void StateAfterAttributeValueQuoted(char32_t cp);
    void StateSelfClosingStartTag(char32_t cp);
    void StateBogusComment(char32_t cp);
    void StateMarkupDeclarationOpen(char32_t cp);
    void StateCommentStart(char32_t cp);
    void StateCommentStartDash(char32_t cp);
    void StateComment(char32_t cp);
    void StateCommentLessThanSign(char32_t cp);
    void StateCommentLessThanSignBang(char32_t cp);
    void StateCommentLessThanSignBangDash(char32_t cp);
    void StateCommentLessThanSignBangDashDash(char32_t cp);
    void StateCommentEndDash(char32_t cp);
    void StateCommentEnd(char32_t cp);
    void StateCommentEndBang(char32_t cp);
    void StateDoctype(char32_t cp);
    void StateBeforeDoctypeName(char32_t cp);
    void StateDoctypeName(char32_t cp);
    void StateAfterDoctypeName(char32_t cp);
    void StateAfterDoctypePublicKeyword(char32_t cp);
    void StateBeforeDoctypePublicIdentifier(char32_t cp);
    void StateDoctypePublicIdentifierDoubleQuoted(char32_t cp);
    void StateDoctypePublicIdentifierSingleQuoted(char32_t cp);
    void StateAfterDoctypePublicIdentifier(char32_t cp);
    void StateBetweenDoctypePublicAndSystemIdentifiers(char32_t cp);
    void StateAfterDoctypeSystemKeyword(char32_t cp);
    void StateBeforeDoctypeSystemIdentifier(char32_t cp);
    void StateDoctypeSystemIdentifierDoubleQuoted(char32_t cp);
    void StateDoctypeSystemIdentifierSingleQuoted(char32_t cp);
    void StateAfterDoctypeSystemIdentifier(char32_t cp);
    void StateBogusDoctype(char32_t cp);
    void StateCdataSection(char32_t cp);
    void StateCdataSectionBracket(char32_t cp);
    void StateCdataSectionEnd(char32_t cp);
    void StateAmbiguousAmpersand(char32_t cp);
};

} // namespace guchho::html