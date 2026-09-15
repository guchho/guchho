#pragma once

#include "guchho/logger.hpp"
#include <cstdint>
#include <string_view>

// =============================================================================
// CSS Tokenizer
//
// Converts raw CSS source text into a flat sequence of tokens.  The tokenizer
// follows the CSS Syntax Level 3 specification (the "tokenization" and
// "tokenizer definition" sections) and is designed to be fast, lossless, and
// compatible with nested parsing and source‑map generation.
//
// Usage:
//   guchho::logger::Source src = guchho::logger::Source::Make(...);
//   guchho::logger::Log log;
//   guchho::css::lexer::Options opts;
//   auto result = guchho::css::lexer::Tokenize(log, src, opts);
//   // result.tokens now contains every lexical token in the stylesheet.
//
// The header also exposes a handful of character‑level predicates (IsNameStart,
// IsNameContinue, etc.) that are useful when building or extending parsers on
// top of the raw token stream.
// =============================================================================

namespace guchho::css {

    // -------------------------------------------------------------------------
    // TokenType
    //
    // Discriminator for the different lexical categories defined by CSS Syntax
    // Level 3.  Every token produced by the lexer carries exactly one of these
    // values.  The numeric values are intentionally kept compact (uint8_t) so
    // that a token fits in a small amount of memory and switch statements stay
    // branch‑prediction friendly.
    //
    // Two special entries are worth noting:
    //   kUnterminatedString – produced when a string literal is never closed
    //                        by a matching quote before EOF.  Callers can
    //                        decide whether to emit a diagnostic.
    //   kBadUrl             – analogous for url() tokens whose contents are
    //                        malformed (e.g. an unescaped closing parenthesis).
    // -------------------------------------------------------------------------

    enum class TokenType : std::uint8_t {
        kEndOfFile,

        kAtKeyword,
        kUnterminatedString,
        kBadUrl,
        kCdc,      // The "–‑>"  (CDC) comment delimiter.
        kCdo,      // The "<!‑‑"  (CDO) comment delimiter.
        kCloseBrace,
        kCloseBracket,
        kCloseParen,
        kColon,
        kComma,
        kDelim,
        kDelimAmpersand,
        kDelimAsterisk,
        kDelimBar,
        kDelimCaret,
        kDelimDollar,
        kDelimDot,
        kDelimEquals,
        kDelimExclamation,
        kDelimGreaterThan,
        kDelimLessThan,
        kDelimMinus,
        kDelimPlus,
        kDelimSlash,
        kDelimTilde,
        kDimension,
        kFunction,
        kHash,
        kIdent,
        kNumber,
        kOpenBrace,
        kOpenBracket,
        kOpenParen,
        kPercentage,
        kSemicolon,
        kString,
        kUrl,
        kWhitespace,
        kSymbol,
    };

    // -------------------------------------------------------------------------
    // TokenFlags
    //
    // Bitmask that can be OR‑ed onto a token to record extra information that
    // does not merit its own TokenType.  Currently two bits are defined:
    //
    //   kIsID                              – set when the token is an <id-token>
    //                                        (starts with '#' and the name
    //                                        portion satisfies the CSS custom
    //                                        ident grammar).  Useful for fast
    //                                        ID selectors.
    //   kDidWarnAboutSingleLineComment     – internal bookkeeping flag used to
    //                                        emit at most one diagnostic per
    //                                        source region when the parser
    //                                        encounters a single‑line comment
    //                                        that does not terminate a block
    //                                        comment.
    //
    // Example:
    //   TokenFlags f = kIsID;            // 0b00000001
    //   f |= kDidWarnAboutSingleLineComment; // 0b00000011
    // -------------------------------------------------------------------------

    using TokenFlags = uint8_t;
    inline constexpr TokenFlags kIsID = 1 << 0;
    inline constexpr TokenFlags kDidWarnAboutSingleLineComment = 1 << 1;

    // -------------------------------------------------------------------------
    // IsNameStart
    //
    // Returns true if the Unicode code point `cp` may appear as the first
    // character of a CSS identifier (an <ident-start code point>).  This
    // includes underscore '_', all ASCII letters, any code point ≥ U+0080, and
    // the CSS escape mechanism's backslash.
    //
    // Example:
    //   IsNameStart('a')  == true
    //   IsNameStart('_')  == true
    //   IsNameStart('3')  == false   (digits never start an ident)
    //   IsNameStart(0x4E16)== true   (CJK character – valid ident start)
    // -------------------------------------------------------------------------
    bool IsNameStart(char32_t cp) noexcept;

    // -------------------------------------------------------------------------
    // IsNameContinue
    //
    // Returns true if `cp` may appear after the first character of a CSS
    // identifier (an <ident code point>).  Superset of IsNameStart – digits
    // are allowed here.
    //
    // Example:
    //   IsNameContinue('0') == true
    //   IsNameContinue('!') == false
    //   IsNameContinue(0x200D) == true  (zero‑width joiner is valid)
    // -------------------------------------------------------------------------
    bool IsNameContinue(char32_t cp) noexcept;

    // -------------------------------------------------------------------------
    // WouldStartIdentifierWithoutEscapes
    //
    // Pure‑ASCII fast path that checks whether `text` looks like the beginning
    // of a CSS identifier without interpreting any escape sequences.  This is
    // used by the lexer's peek logic to decide, before consuming characters,
    // whether the next token is an identifier.
    //
    // Example:
    //   WouldStartIdentifierWithoutEscapes("foo") == true
    //   WouldStartIdentifierWithoutEscapes("123") == false
    //   WouldStartIdentifierWithoutEscapes("-a")  == true
    //   WouldStartIdentifierWithoutEscapes("--")  == true (custom ident)
    // -------------------------------------------------------------------------
    bool WouldStartIdentifierWithoutEscapes(std::string_view text) noexcept;

    // -------------------------------------------------------------------------
    // WouldStartIdentifier
    //
    // Like WouldStartIdentifierWithoutEscapes but works on arbitrary source
    // positions and understands CSS escape sequences.  The lexer uses this to
    // decide whether a '#' hash or '-' dash starts an identifier when escapes
    // are present.
    //
    // Example (assuming source text "\\\\2Dfoo"):
    //   WouldStartIdentifier(source, loc) == true  (escaped '-' is valid)
    // -------------------------------------------------------------------------
    bool WouldStartIdentifier(const guchho::logger::Source& source, guchho::logger::Loc loc);

    // -------------------------------------------------------------------------
    // IsValidEscapeAt
    //
    // Returns true if a backslash at `loc` forms a valid CSS escape sequence
    // according to the spec.  A backslash followed by a newline is always a
    // valid (line‑continuation) escape; otherwise the next code point must not
    // be a hex digit or EOF.
    //
    // Example:
    //   IsValidEscapeAt("\\\\41") == true   (hex escape)
    //   IsValidEscapeAt("\\\\A\\n") == true (line continuation)
    //   IsValidEscapeAt("\\\\") == false    (lone backslash at EOF)
    // -------------------------------------------------------------------------
    bool IsValidEscapeAt(const guchho::logger::Source& source, guchho::logger::Loc loc);

    // -------------------------------------------------------------------------
    // IsNumeric
    //
    // Convenience predicate that returns true for token types that represent
    // numeric values: kNumber, kPercentage, and kDimension.  Useful when
    // building value parsers that need to accept any numeric form.
    //
    // Example:
    //   IsNumeric(TokenType::kNumber)      == true
    //   IsNumeric(TokenType::kDimension)    == true
    //   IsNumeric(TokenType::kPercentage)   == true
    //   IsNumeric(TokenType::kIdent)        == false
    // -------------------------------------------------------------------------
    bool IsNumeric(TokenType kind);

    // -------------------------------------------------------------------------
    // RangeOfIdentifier
    //
    // Starting at `loc` (which must point to the first code point of an
    // identifier), advances through the source and returns the full
    // (inclusive) range of the identifier, including any escape sequences.
    // The returned range is valid for use with Token::DecodedText.
    //
    // Example:
    //   Given source text "foo\\2Dbar" at loc pointing at 'f':
    //     RangeOfIdentifier(source, loc) covers characters 'f' through 'r'.
    // -------------------------------------------------------------------------
    guchho::logger::Range RangeOfIdentifier(const guchho::logger::Source& source, guchho::logger::Loc loc);

    namespace lexer {

        // -----------------------------------------------------------------
        // Token
        //
        // Represents a single lexical token produced by the tokenizer.  The
        // token does **not** own its text – it stores a Range into the
        // original source.  To recover the decoded (escape‑expanded) text
        // call DecodedText(), passing the source contents.
        //
        //   Token t = ...;
        //   std::string decoded = t.DecodedText(source.Contents());
        // -----------------------------------------------------------------

        struct Token {
            guchho::logger::Range range;
            uint16_t unit_offset{};
            TokenType kind{};
            TokenFlags flags{};

            // Returns the decoded (escape‑expanded) text of this token.
            // For most tokens this is just the raw source slice.  For
            // identifiers and strings the function handles CSS escape
            // sequences (e.g. "\\2D" → '-').
            std::string DecodedText(std::string_view contents) const;
        };

        // -----------------------------------------------------------------
        // Comment
        //
        // Stores the text and location of a CSS comment that the lexer has
        // chosen to preserve.  "Legal" comments are those that appear in
        // positions where they are semantically meaningful (e.g. license
        // headers at the top of a file, or comments attached to a declaration
        // block).  The token_index_after field allows the parser to link a
        // comment back to the token that immediately follows it.
        // -----------------------------------------------------------------

        struct Comment {
            std::string text;
            guchho::logger::Loc loc;
            uint32_t token_index_after{};
        };

        // -----------------------------------------------------------------
        // Span
        //
        // A simple text + range pair used to represent special comment spans
        // such as source‑map pragmas ("//# sourceMappingURL=…").  The text
        // field contains the raw comment text; range gives its position in
        // the source.
        // -----------------------------------------------------------------

        struct Span {
            std::string text;
            guchho::logger::Range range;
        };

        // -----------------------------------------------------------------
        // TokenizeResult
        //
        // Aggregate returned by the Tokenize() function.  Members:
        //
        //   tokens              – ordered list of every token in the source,
        //                         including whitespace and comments that were
        //                         not stripped.
        //   all_comments        – ranges of every comment encountered, even
        //                         those not marked as "legal".  Useful for
        //                         re‑emitting or stripping all comments.
        //   legal_comments      – comments that the lexer deemed significant
        //                         (e.g. those at the start of a rule or
        //                         declaration).  Preserved for downstream tooling.
        //   source_map_comment  – the first "//# sourceMappingURL=" comment, if
        //                         present.  Only one source‑map comment is
        //                         allowed per file; later ones are reported as
        //                         diagnostics.
        //   approximate_line_count – heuristic line count derived from the
        //                         number of newlines encountered during tokenization.
        //
        // Example:
        //   auto res = Tokenize(log, src, opts);
        //   for (auto& tok : res.tokens) { ... }
        //   std::cout << "Lines: " << res.approximate_line_count << '\n';
        // -----------------------------------------------------------------

        struct TokenizeResult {
            std::vector<Token> tokens;
            std::vector<guchho::logger::Range> all_comments;
            std::vector<Comment> legal_comments;
            Span source_map_comment;
            int32_t approximate_line_count{};
        };

        // -----------------------------------------------------------------
        // Options
        //
        // Tuning knobs for the tokenizer.  Currently only one flag is defined:
        //
        //   record_all_comments – when true the lexer populates
        //                         TokenizeResult::all_comments and preserves
        //                         comment tokens in the token stream.  When
        //                         false (the default) comments are still
        //                         lexed so that they can affect parsing
        //                         context, but they are discarded from the
        //                         final token list to save memory.
        // -----------------------------------------------------------------

        struct Options {
            bool record_all_comments{};
        };

        // -----------------------------------------------------------------
        // ToString(TokenType)
        //
        // Returns a human‑readable name for the given token type.  Primarily
        // useful for diagnostics and debugging.
        //
        // Example:
        //   ToString(TokenType::kIdent) == "kIdent"
        //   ToString(TokenType::kString) == "kString"
        // -----------------------------------------------------------------

        std::string ToString(TokenType kind);

        // -----------------------------------------------------------------
        // Tokenize
        //
        // Entry point for the CSS tokenizer.  Consumes the entire source and
        // returns a TokenizeResult containing all lexical tokens, collected
        // comments, and a source‑map comment if present.
        //
        // Parameters:
        //   log    – logger used to emit diagnostics (e.g. unterminated
        //            strings, bad urls).
        //   source – the CSS source text wrapped in a Source object.
        //   options – see Options above.
        //
        // Returns:
        //   A TokenizeResult.  On a clean source the tokens vector will not
        //   be empty (at minimum an EOF token is emitted).  If the source is
        //   empty the result contains a single kEndOfFile token.
        //
        // Example:
        //   guchho::logger::Source s = guchho::logger::Source::Make("a{color:red}");
        //   guchho::logger::Log l;
        //   auto r = guchho::css::lexer::Tokenize(l, s, {});
        //   // r.tokens: [kIdent("a"), kOpenBrace, kIdent("color"),
        //   //            kColon, kIdent("red"), kSemicolon, kCloseBrace,
        //   //            kEndOfFile]
        // -----------------------------------------------------------------

        TokenizeResult Tokenize(
            guchho::logger::Log& log,
            const guchho::logger::Source& source,
            const Options& options
        );

    }  // namespace lexer

}  // namespace guchho::css