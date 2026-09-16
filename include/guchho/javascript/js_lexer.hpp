#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "guchho/logger.hpp"
#include "guchho/compiler.hpp"
#include "guchho/config.hpp"

namespace guchho::javascript {

    // Every token produced by the JavaScript lexer is represented as one of
    // these enum values. The numeric values are arranged so that a static_cast
    // from T to size_t gives a valid index into the kTokenToString table,
    // enabling efficient token-to-string lookups.
    //
    // Tokens are grouped by category: literals, template fragments, punctuation,
    // assignment operators, private identifiers, identifiers, and reserved words.
    // The lexer produces keyword tokens (kBreak, kIf, etc.) instead of generic
    // kIdentifier tokens for reserved words, which simplifies downstream parsing.
    enum class T : uint8_t {
        kEndOfFile,
        kSyntaxError,
        kHashbang,

        kNoSubstitutionTemplateLiteral,
        kNumericLiteral,
        kStringLiteral,
        kBigIntegerLiteral,

        kTemplateHead,
        kTemplateMiddle,
        kTemplateTail,

        kAmpersand,
        kAmpersandAmpersand,
        kAsterisk,
        kAsteriskAsterisk,
        kAt,
        kBar,
        kBarBar,
        kCaret,
        kCloseBrace,
        kCloseBracket,
        kCloseParen,
        kColon,
        kComma,
        kDot,
        kDotDotDot,
        kEqualsEquals,
        kEqualsEqualsEquals,
        kEqualsGreaterThan,
        kExclamation,
        kExclamationEquals,
        kExclamationEqualsEquals,
        kGreaterThan,
        kGreaterThanEquals,
        kGreaterThanGreaterThan,
        kGreaterThanGreaterThanGreaterThan,
        kLessThan,
        kLessThanEquals,
        kLessThanLessThan,
        kMinus,
        kMinusMinus,
        kOpenBrace,
        kOpenBracket,
        kOpenParen,
        kPercent,
        kPlus,
        kPlusPlus,
        kQuestion,
        kQuestionDot,
        kQuestionQuestion,
        kSemicolon,
        kSlash,
        kTilde,

        kAmpersandAmpersandEquals,
        kAmpersandEquals,
        kAsteriskAsteriskEquals,
        kAsteriskEquals,
        kBarBarEquals,
        kBarEquals,
        kCaretEquals,
        kEquals,
        kGreaterThanGreaterThanEquals,
        kGreaterThanGreaterThanGreaterThanEquals,
        kLessThanLessThanEquals,
        kMinusEquals,
        kPercentEquals,
        kPlusEquals,
        kQuestionQuestionEquals,
        kSlashEquals,

        kPrivateIdentifier,

        kIdentifier,
        kEscapedKeyword,

        kBreak,
        kCase,
        kCatch,
        kClass,
        kConst,
        kContinue,
        kDebugger,
        kDefault,
        kDelete,
        kDo,
        kElse,
        kEnum,
        kExport,
        kExtends,
        kFalse,
        kFinally,
        kFor,
        kFunction,
        kIf,
        kImport,
        kIn,
        kInstanceof,
        kNew,
        kNull,
        kReturn,
        kSuper,
        kSwitch,
        kThis,
        kThrow,
        kTrue,
        kTry,
        kTypeof,
        kVar,
        kVoid,
        kWhile,
        kWith,
    };

    // Maps JSX entity names (e.g., "amp", "lt", "gt") to their corresponding
    // Unicode code points. Used when decoding JSX text content inside attribute
    // values and text children.
    // Example: JSXEntity.at("amp") => '&'
    // Example: JSXEntity.at("nbsp") => 0x00A0
    extern const std::unordered_map<std::string_view, char32_t> JSXEntity;

    // Returns a human-readable string for a token type. Used in error messages
    // and diagnostics to describe what token was expected or found.
    // Example: ToString(T::kIdentifier) => "identifier"
    // Example: ToString(T::kSemicolon) => ";"
    std::string_view ToString(T kind) noexcept;

    // Represents a string that may be a substring of the original source or a
    // separately allocated string. When start is not kInvalidStart, the string
    // is a view into the source buffer (no allocation). When start is
    // kInvalidStart, str contains an independently allocated copy.
    // This optimization avoids heap allocation for the common case where
    // identifiers and keywords are direct slices of the source text.
    struct MaybeSubstring {
        static constexpr uint32_t kInvalidStart = UINT32_MAX;

        std::string str;
        uint32_t start = kInvalidStart;

        bool IsSubstring() const { return start != kInvalidStart; }
    };


    // Distinguishes between different JSON parsing modes. Standard JSON follows
    // the spec at json.org. TypeScript's JSON superset additionally allows
    // comments, trailing commas, and full JavaScript number syntax. The kNotJSON
    // mode is used by the main JavaScript lexer and has no JSON restrictions.
    enum class JSONFlavor : uint8_t {
        kJSON,

        kTSConfigJSON,

        kNotJSON,
    };

    // Flags indicating what kind of comment preceded the current token. These
    // flags are used by the printer and minifier to preserve or strip comments
    // based on annotation content (e.g., @license, @preserve,Pure,BenchPacket).
    enum class CommentBefore : uint8_t {
        kNone = 0,
        kPure = 1 << 0,
        kKey = 1 << 1,
        kNoSideEffects = 1 << 2,
    };

    inline CommentBefore operator|(CommentBefore a, CommentBefore b) {
        return static_cast<CommentBefore>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    inline CommentBefore& operator|=(CommentBefore& a, CommentBefore b) {
        a = a | b;
        return a;
    }

    inline bool Has(CommentBefore flags, CommentBefore flag) {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }

    enum class IdentifierKind : uint8_t {
        kNormal,
        kPrivate,
    };


    // Thrown by the lexer when a panic-level syntax error is encountered that
    // should immediately terminate parsing. This is used for unrecoverable
    // errors like unterminated string literals at end-of-file.
    struct LexerPanic {};

    // The JavaScript lexer converts source code text into a stream of tokens.
    // It handles all JavaScript/TypeScript tokenization including: identifiers,
    // keywords, numeric literals (decimal, hex, octal, binary, BigInt),
    // string literals (single, double, template), regular expressions, JSX,
    // and comments. The lexer supports TypeScript-specific syntax when TS
    // options are provided.
    //
    // The lexer is stateful: calling Next() advances to the next token, and
    // the current token's data is accessible via the public fields (token,
    // identifier, number, etc.). Comments encountered before the current token
    // are collected in comments_before_token and legal_comments_before_token.
    //
    // Usage:
    //   Lexer lexer(log, source, ts_options);
    //   while (lexer.token != T::kEndOfFile) {
    //       // process lexer.token
    //       lexer.Next();
    //   }
    class Lexer {
    public:
        Lexer() = default;

        // Constructs a lexer for the given source code with TypeScript options.
        // The source must remain valid for the lifetime of the lexer.
        Lexer(logger::Log log, logger::Source source, config::TSOptions ts);

        // Creates a lexer in "global name" mode, which only scans a single
        // identifier. Used for resolving global assignment target names.
        static Lexer NewGlobalName(logger::Log log, logger::Source source);

        // Creates a lexer in JSON parsing mode. The flavor parameter controls
        // whether standard JSON, TypeScript JSON (with comments/trailing commas),
        // or regular JavaScript syntax is expected. The error_suffix is appended
        // to error messages for context.
        static Lexer NewJSON(logger::Log log, logger::Source source, JSONFlavor json, std::string error_suffix);

        // Returns the source location (byte offset) of the start of the current token.
        logger::Loc Loc() const;
        // Returns the source range (start + length) of the current token.
        logger::Range Range() const;
        // Returns the raw source text of the current token without any decoding.
        std::string_view Raw() const;
        // Returns the decoded string content of a string literal, with escape
        // sequences resolved. The result is cached for repeated access.
        // Example: for source "\"hello\\nworld\"", returns u"hello\nworld"
        const std::u16string& StringLiteral();

        struct CookedAndRawTemplateContentsResult {
            std::u16string cooked;
            std::string raw;
            bool is_valid;
        };

        // Returns both the cooked (escape-resolved) and raw (as-is) contents
        // of a template literal. The cooked value has escape sequences resolved;
        // the raw value preserves the original source text. is_valid is false
        // if the template contains invalid escape sequences.
        // Example: for "`hello\\nworld`", cooked = u"hello\nworld", raw = "hello\\nworld"
        CookedAndRawTemplateContentsResult CookedAndRawTemplateContents();

        // Returns true if the current token is an identifier or keyword.
        bool IsIdentifierOrKeyword() const;

        // Returns true if the current token is a contextual keyword (e.g.,
        // "use strict", "as", "from", "async", "of", "get", "set").
        // These are identifiers that have special meaning in certain contexts
        // but are not reserved words.
        bool IsContextualKeyword(std::string_view text) const;

        // Expects the current token to be a contextual keyword with the given
        // text. Emits a diagnostic if it doesn't match and advances past it.
        void ExpectContextualKeyword(std::string_view text);

        // Emits a syntax error diagnostic for the current token location.
        [[noreturn]] void SyntaxError();

        // Emits a diagnostic indicating that a specific string was expected
        // at the current position but not found.
        [[noreturn]] void ExpectedString(std::string_view text);

        // Emits a diagnostic indicating that a specific token type was expected
        // at the current position but not found.
        [[noreturn]] void Expected(T expected);

        // Emits a diagnostic for an unexpected token at the current position.
        [[noreturn]] void Unexpected();

        // Expects the current token to be of the given type. If not, emits a
        // diagnostic. Advances past the token regardless.
        void Expect(T expected);

        // Expects a semicolon at the current position. If the semicolon is
        // missing, performs automatic semicolon insertion (ASI) if the next
        // token is a closing brace, end-of-file, or if there was a newline
        // before the current token. Emits a diagnostic if ASI doesn't apply.
        void ExpectOrInsertSemicolon();

        void ExpectLessThan(bool is_inside_jsx_element);

        void ExpectGreaterThan(bool is_inside_jsx_element);

        void ExpectJSXElementChild(T expected);
        void NextJSXElementChild();
        void ExpectInsideJSXElement(T expected);
        void NextInsideJSXElement();

        // Advances the lexer to the next token. This is the main tokenization
        // entry point. After calling Next(), the token field contains the new
        // token type and related fields (identifier, number, etc.) are updated.
        void Next();

        // Rescans the current position as a regular expression literal. This is
        // called after parsing a expression where a "/" could be division or the
        // start of a regex. The lexer looks ahead to determine which interpretation
        // is correct.
        void ScanRegExp();

        // Rescans a "}" token as a template tail (kTemplateTail or
        // kTemplateMiddle). This is needed because "}" can end a template
        // expression or be a regular brace; the parser calls this when it
        // determines we're still inside a template literal.
        void RescanCloseBraceAsTemplateToken();

        void AddRangeErrorWithNotes(logger::Range r, const std::string& text, const std::vector<logger::MsgData>& notes);


        std::vector<logger::Range> legal_comments_before_token;
        std::vector<logger::Range> comments_before_token;
        std::vector<logger::Range> all_comments;
        MaybeSubstring identifier;
        Span jsx_factory_pragma_comment;
        Span jsx_fragment_pragma_comment;
        Span jsx_runtime_pragma_comment;
        Span jsx_import_source_pragma_comment;
        Span source_mapping_url;
        std::string bad_arrow_in_tsx_suggestion;


        std::optional<std::u16string> decoded_string_literal_or_nil;
        std::string encoded_string_literal_text;
        int encoded_string_literal_start = 0;
        std::string error_suffix;

        // The numeric value of the current token when token is kNumericLiteral.
        double number = 0;
        int current = 0;
        int start = 0;
        int end = 0;
        int approximate_newline_count = 0;
        int could_be_bad_arrow_in_tsx = 0;
        logger::Range bad_arrow_in_tsx_range;
        logger::Loc legacy_octal_loc;
        logger::Loc await_keyword_loc;
        logger::Loc fn_or_arrow_start_loc;
        logger::Range previous_backslash_quote_in_jsx;
        logger::Range legacy_html_comment_range;
        int32_t code_point = 0;
        logger::Loc prev_error_loc;
        JSONFlavor json = JSONFlavor::kNotJSON;
        // The type of the current token. This is the primary field checked by
        // callers to determine what kind of token was lexed.
        T token = T::kEndOfFile;
        config::TSOptions ts;
        // True if there was at least one newline between the previous token
        // and the current token. Used for automatic semicolon insertion.
        bool has_newline_before = false;
        CommentBefore has_comment_before = CommentBefore::kNone;
        bool is_legacy_octal_literal = false;
        bool prev_token_was_await_keyword = false;
        bool rescan_close_brace_as_template_token = false;
        bool for_global_name = false;

        bool is_log_disabled = false;

    private:
        struct InitOptions {
            bool for_global_name = false;
            JSONFlavor json = JSONFlavor::kNotJSON;
            std::string error_suffix;
        };

        Lexer(
            logger::Log log, 
            logger::Source source, 
            config::TSOptions ts, 
            InitOptions options
        );

        // Attempts to expand a single "=" into an compound assignment operator
        // (e.g., "=" => "===" when followed by "=="). This handles the case
        // where the source contains "===" or "==" that were split across
        // token boundaries.
        void maybeExpandEquals();

        // Returns the raw identifier text as a MaybeSubstring. If the identifier
        // is a direct slice of the source, IsSubstring() returns true and start
        // gives the offset. Otherwise, str contains an allocated copy.
        MaybeSubstring rawIdentifier();

        // Scans an identifier that may contain Unicode escape sequences
        // (e.g., \u0041 for 'A'). Returns the decoded identifier and its token
        // type. For keywords with escapes like \u0069f, returns kEscapedKeyword.
        std::pair<MaybeSubstring, T> scanIdentifierWithEscapes(IdentifierKind kind);

        // Parses a numeric literal or a dot. Handles decimal integers, floats,
        // hex (0x), octal (0o), binary (0b), and BigInt (n suffix) formats.
        // Also handles the "." token when followed by a digit (which starts a
        // decimal number) versus when it's a member expression operator.
        void parseNumericLiteralOrDot();

        // Decodes escape sequences in a string or template literal. Reports
        // errors for invalid escapes when report_errors is true. The decoded
        // result is appended to the decoded output parameter. Sets ok to false
        // if any invalid escape sequences are encountered.
        void tryToDecodeEscapeSequences(
            int start, std::string_view text, bool report_errors,
            std::u16string& decoded, bool& ok, int& end
        );

        // The core tokenization step. Reads the next token from the source and
        // updates all public fields. This is called by Next() and the constructor.
        void step();

        void addRangeError(logger::Range r, const std::string& text);
        void addRangeErrorWithSuggestion(logger::Range r, const std::string& text, const std::string& suggestion);

        // Scans the text content of a comment (after // or /*) and classifies
        // it based on annotation content. Sets has_comment_before flags for
        // comments containing @license, @preserve, Pure, BenchPacket, etc.
        void scanCommentText();

        logger::Log log_;
        logger::Source source_;
        logger::LineColumnTracker tracker_;
    };


    // Returns the source range of an identifier at the given location. This
    // is used by the parser to get the range of identifiers that were parsed
    // as part of a larger expression.
    logger::Range RangeOfIdentifier(const logger::Source& source, logger::Loc loc);


    enum class KeyOrValue : uint8_t {
        kKeyRange,
        kValueRange,
        kKeyAndValueRange,
    };


    // Returns the source range of an import assert/with entry. The which
    // parameter controls whether the key range, value range, or both are
    // returned. This is used for source map generation in import assertions.
    logger::Range RangeOfImportAssertOrWith(const logger::Source& source, const compiler::AssertOrWithEntry& assert_or_with, KeyOrValue which);

    // Maps JavaScript keyword strings to their corresponding token types.
    // This table is used by the lexer to classify identifiers as keywords.
    // Example: kKeywords.at("if") => T::kIf
    // Example: kKeywords.at("function") => T::kFunction
    extern const std::unordered_map<std::string_view, T> kKeywords;

    // Set of words that are reserved in strict mode JavaScript. These cannot
    // be used as identifiers in strict mode code (modules, classes, "use strict").
    // Example: kStrictModeReservedWords.count("implements") => true
    // Example: kStrictModeReservedWords.count("if") => false (reserved everywhere)
    extern const std::unordered_set<std::string_view> kStrictModeReservedWords;

}
